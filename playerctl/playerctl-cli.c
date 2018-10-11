/*
 * This file is part of playerctl.
 *
 * playerctl is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * playerctl is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with playerctl If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright © 2014, Tony Crisci and contributors.
 */

#include <stdbool.h>
#include <gio/gio.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "playerctl.h"

#define LENGTH(array) (sizeof array / sizeof array[0])

#define MPRIS_PREFIX "org.mpris.MediaPlayer2."

G_DEFINE_QUARK(playerctl-cli-error-quark, playerctl_cli_error);

/* A comma separated list of players to control. */
static gchar *player_arg = NULL;
/* A comma separated list of players to ignore. */
static gchar *ignore_player_arg = NULL;
/* If true, control all available media players */
static gboolean select_all_players;
/* If true, list all available players' names and exit. */
static gboolean list_all_players_and_exit;
/* If true, print the version and exit. */
static gboolean print_version_and_exit;
/* The commands passed on the command line, filled in via G_OPTION_REMAINING. */
static gchar **command = NULL;
/* A format string for printing properties and metadata */
static gchar *format_string = NULL;
/* Block and follow the command */
static gboolean follow = FALSE;

/* The main loop for the follow command */
static GMainLoop *main_loop = NULL;

/* The player currently being followed */
PlayerctlPlayer *followed_player = NULL;

struct playercmd_args {
    gchar **argv;
    gint argc;
};

/* Arguments given to the player for the follow command */
static struct playercmd_args *playercmd_args = NULL;

static struct playercmd_args *playercmd_args_create(gchar **argv, gint argc) {
    struct playercmd_args *user_data = calloc(1, sizeof(struct playercmd_args));
    user_data->argc = argc;
    user_data->argv = g_strdupv(argv);
    return user_data;
}

static void playercmd_args_destroy(struct playercmd_args *data) {
    if (data == NULL) {
        return;
    }

    g_strfreev(data->argv);
    free(data);

    return;
}

static gchar *print_gvariant(GVariant *value) {
    GString *printed = g_string_new("");
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING_ARRAY)) {
        gsize prop_count;
        const gchar **prop_strv = g_variant_get_strv(value, &prop_count);

        for (int i = 0; i < prop_count; i += 1) {
            g_string_append(printed, prop_strv[i]);

            if (i != prop_count - 1) {
                g_string_append(printed, ", ");
            }
        }

        g_free(prop_strv);
    } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
        g_string_append(printed, g_variant_get_string(value, NULL));
    } else {
        printed = g_variant_print_string(value, printed, FALSE);
    }

    return g_string_free(printed, FALSE);
}

enum token_type {
    TOKEN_PASSTHROUGH,
    TOKEN_VARIABLE,
    TOKEN_FUNCTION,
};

struct token {
    enum token_type type;
    gchar *data;
    struct token *arg;
};

static struct token *token_create(enum token_type type) {
    struct token *token = calloc(1, sizeof(struct token));
    token->type = type;
    return token;
}

static void token_destroy(struct token *token) {
    if (token == NULL) {
        return;
    }

    token_destroy(token->arg);
    g_free(token->data);
    free(token);
}

static void token_list_destroy(GList *list) {
    g_list_free_full(list, (GDestroyNotify)token_destroy);
}

enum parser_state {
    STATE_INSIDE = 0,
    STATE_PARAMS_OPEN,
    STATE_PARAMS_CLOSED,
    STATE_PASSTHROUGH,
};

#define FORMAT_ERROR "[format error] "

static GList *tokenize_format(const char *format, GError **error) {
    GList *tokens = NULL;

    int len = strlen(format);
    char buf[1028];
    int buf_len = 0;

    if (len >= 1028) {
        g_set_error(error, playerctl_cli_error_quark(), 1,
                    FORMAT_ERROR "the maximum format string length is 1028");
        return NULL;
    }

    enum parser_state state = STATE_PASSTHROUGH;
    for (int i = 0; i < len; ++i) {
        if (format[i] == '{' && i < len + 1 && format[i+1] == '{') {
            if (state == STATE_INSIDE) {
                g_set_error(error, playerctl_cli_error_quark(), 1,
                            FORMAT_ERROR "unexpected token: \"{{\" (position %d)", i);
                token_list_destroy(tokens);
                return NULL;
            }
            if (buf_len != 0) {
                struct token *token = token_create(TOKEN_PASSTHROUGH);
                buf[buf_len] = '\0';
                token->data = g_strdup(buf);
                tokens = g_list_append(tokens, token);
            }
            i += 1;
            buf_len = 0;
            state = STATE_INSIDE;
        } else if (format[i] == '}' && i < len + 1 && format[i+1] == '}' && state != STATE_PASSTHROUGH) {
            if (state == STATE_PARAMS_OPEN) {
                g_set_error(error, playerctl_cli_error_quark(), 1,
                            FORMAT_ERROR "unexpected token: \"}}\" (expected closing parens: \")\" at position %d)", i);
                token_list_destroy(tokens);
                return NULL;
            }

            if (state != STATE_PARAMS_CLOSED) {
                buf[buf_len] = '\0';
                gchar *name = g_strstrip(g_strdup(buf));
                if (strlen(name) == 0) {
                    g_set_error(error, playerctl_cli_error_quark(), 1,
                                FORMAT_ERROR "got empty template expression at position %d", i);
                    token_list_destroy(tokens);
                    g_free(name);
                    return NULL;
                }

                struct token *token = token_create(TOKEN_VARIABLE);
                token->data = name;
                tokens = g_list_append(tokens, token);
            } else if (buf_len > 0) {
                for (int k = 0; k < buf_len; ++k) {
                    if (buf[k] != ' ') {
                        g_set_error(error, playerctl_cli_error_quark(), 1,
                                    FORMAT_ERROR "got unexpected input after closing parens at position %d", i - buf_len + k);
                        token_list_destroy(tokens);
                        return NULL;
                    }
                }
            }

            i += 1;
            buf_len = 0;
            state = STATE_PASSTHROUGH;
        } else if (format[i] == '(' && state != STATE_PASSTHROUGH) {
            if (state == STATE_PARAMS_OPEN) {
                g_set_error(error, playerctl_cli_error_quark(), 1,
                            FORMAT_ERROR "unexpected token: \"(\" at position %d", i);
                token_list_destroy(tokens);
                return NULL;
            }
            if (state == STATE_PARAMS_CLOSED) {
                g_set_error(error, playerctl_cli_error_quark(), 1,
                            FORMAT_ERROR "unexpected token: \"(\" at position %d", i);
                token_list_destroy(tokens);
                return NULL;
            }
            buf[buf_len] = '\0';
            gchar *name = g_strstrip(g_strdup(buf));
            if (strlen(name) == 0) {
                g_set_error(error, playerctl_cli_error_quark(), 1,
                        FORMAT_ERROR "expected a function name to call at position %d", i);
                token_list_destroy(tokens);
                g_free(name);
                return NULL;
            }
            struct token *token = token_create(TOKEN_FUNCTION);
            token->data = name;
            tokens = g_list_append(tokens, token);
            buf_len = 0;
            state = STATE_PARAMS_OPEN;
        } else if (format[i] == ')' && state != STATE_PASSTHROUGH) {
            if (state != STATE_PARAMS_OPEN) {
                g_set_error(error, playerctl_cli_error_quark(), 1,
                            FORMAT_ERROR "unexpected token: \")\" at position %d", i);
                token_list_destroy(tokens);
                return NULL;
            }
            buf[buf_len] = '\0';
            gchar *name = g_strstrip(g_strdup(buf));
            if (strlen(name) == 0) {
                g_set_error(error, playerctl_cli_error_quark(), 1,
                            FORMAT_ERROR "expected a function parameter at position %d", i);
                token_list_destroy(tokens);
                g_free(name);
                return NULL;
            }
            struct token *token = token_create(TOKEN_VARIABLE);
            token->data = name;

            struct token *fn_token = g_list_last(tokens)->data;
            assert(fn_token != NULL);
            assert(fn_token->type == TOKEN_FUNCTION);
            assert(fn_token->arg == NULL);
            fn_token->arg = token;
            buf_len = 0;
            state = STATE_PARAMS_CLOSED;
        } else {
            buf[buf_len++] = format[i];
        }
    }

    if (state == STATE_INSIDE || state == STATE_PARAMS_CLOSED) {
        g_set_error(error, playerctl_cli_error_quark(), 1,
                    FORMAT_ERROR "unmatched opener \"{{\" (expected a matching \"}}\" at the end)");
        token_list_destroy(tokens);
        return NULL;
    } else if (state == STATE_PARAMS_OPEN) {
        g_set_error(error, playerctl_cli_error_quark(), 1,
                    FORMAT_ERROR "unmatched opener \"(\" (expected a matching \")\")");
        token_list_destroy(tokens);
        return NULL;
    }

    if (buf_len > 0) {
        buf[buf_len] = '\0';
        struct token *token = token_create(TOKEN_PASSTHROUGH);
        token->data = g_strdup(buf);
        tokens = g_list_append(tokens, token);
    }

    return tokens;
}

static gchar *helperfn_lc(GVariant *arg) {
    gchar *printed = print_gvariant(arg);
    gchar *printed_lc = g_utf8_strdown(printed, -1);
    g_free(printed);
    return printed_lc;
}

static gchar *helperfn_uc(GVariant *arg) {
    gchar *printed = print_gvariant(arg);
    gchar *printed_uc = g_utf8_strup(printed, -1);
    g_free(printed);
    return printed_uc;
}

static gchar *helperfn_duration(GVariant *arg) {
    // mpris durations are represented as int64 in microseconds
    if (!g_variant_type_equal(g_variant_get_type(arg), G_VARIANT_TYPE_INT64)) {
        return NULL;
    }

    gint64 duration = g_variant_get_int64(arg);
    gint64 seconds = (duration / 1000000) % 60;
    gint64 minutes = (duration / 1000000 / 60) % 60;
    gint64 hours = (duration / 1000000 / 60 / 60);

    GString *formatted = g_string_new("");

    if (hours != 0) {
        g_string_append_printf(formatted, "%" PRId64 ":%02" PRId64 ":%02" PRId64, hours, minutes, seconds);
    } else {
        g_string_append_printf(formatted, "%" PRId64 ":%02" PRId64, minutes, seconds);
    }

    return g_string_free(formatted, FALSE);
}

struct TemplateHelper {
    const gchar *name;
    gchar *(*func)(GVariant *arg);
} helpers[] = {
    {"lc", &helperfn_lc},
    {"uc", &helperfn_uc},
    {"duration", &helperfn_duration},
};

static gchar *expand_format(const gchar *format, GVariantDict *context, GError **error) {
    GError *tmp_error = NULL;
    GString *expanded;

    GList *tokens = tokenize_format(format, &tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(error, tmp_error);
        return NULL;
    }

    expanded = g_string_new("");
    GList *next = tokens;
    while (next != NULL) {
        struct token *token = next->data;
        switch (token->type) {
        case TOKEN_PASSTHROUGH:
            expanded = g_string_append(expanded, token->data);
            break;
        case TOKEN_VARIABLE:
        {
            gchar *name = token->data;
            if (g_variant_dict_contains(context, name)) {
                GVariant *value = g_variant_dict_lookup_value(context, name, NULL);
                if (value != NULL) {
                    gchar *value_str = print_gvariant(value);
                    expanded = g_string_append(expanded, value_str);
                    g_variant_unref(value);
                    g_free(value_str);
                }
            }
            break;
        }
        case TOKEN_FUNCTION:
        {
            // XXX: functions must have an argument and that argument must be a
            // variable (enforced in the tokenization step)
            assert(token->arg != NULL);
            assert(token->arg->type == TOKEN_VARIABLE);

            gboolean found = FALSE;
            gchar *fn_name = token->data;
            gchar *arg_name = token->arg->data;

            for (int i = 0; i < LENGTH(helpers); ++i) {
                if (g_strcmp0(helpers[i].name, fn_name) == 0) {
                    GVariant *value = g_variant_dict_lookup_value(context, arg_name, NULL);
                    if (value != NULL) {
                        gchar *result = helpers[i].func(value);
                        if (result != NULL) {
                            expanded = g_string_append(expanded, result);
                            g_free(result);
                        }
                        g_variant_unref(value);
                    }
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                g_set_error(error, playerctl_cli_error_quark(), 1,
                            FORMAT_ERROR "unknown template function: %s", fn_name);
                token_list_destroy(tokens);
                g_string_free(expanded, TRUE);
                return NULL;
            }

            break;
        }
        }

        next = next->next;
    }

    token_list_destroy(tokens);
    return g_string_free(expanded, FALSE);
}

static gchar *get_metadata_formatted(PlayerctlPlayer *player, const gchar *format, GError **error) {
    GError *tmp_error = NULL;
    GVariant *metadata = NULL;
    g_object_get(player, "metadata", &metadata, NULL);
    if (metadata == NULL) {
        g_set_error(error, playerctl_cli_error_quark(), 1,
                    "Could not get metadata for player");
        return NULL;
    }

    // set our default properties
    GVariantDict *metadata_dict = g_variant_dict_new(metadata);
    if (!g_variant_dict_contains(metadata_dict, "artist") &&
            g_variant_dict_contains(metadata_dict, "xesam:artist")) {
        GVariant *artist = g_variant_dict_lookup_value(metadata_dict, "xesam:artist", NULL);
        g_variant_dict_insert_value(metadata_dict, "artist", artist);
        g_variant_unref(artist);
    }
    if (!g_variant_dict_contains(metadata_dict, "album") &&
            g_variant_dict_contains(metadata_dict, "xesam:album")) {
        GVariant *album = g_variant_dict_lookup_value(metadata_dict, "xesam:album", NULL);
        g_variant_dict_insert_value(metadata_dict, "album", album);
        g_variant_unref(album);
    }
    if (!g_variant_dict_contains(metadata_dict, "title") &&
            g_variant_dict_contains(metadata_dict, "xesam:title")) {
        GVariant *title = g_variant_dict_lookup_value(metadata_dict, "xesam:title", NULL);
        g_variant_dict_insert_value(metadata_dict, "title", title);
        g_variant_unref(title);
    }

    gchar *result = expand_format(format, metadata_dict, &tmp_error);
    if (tmp_error) {
        g_variant_unref(metadata);
        g_variant_dict_unref(metadata_dict);
        g_propagate_error(error, tmp_error);
        return NULL;
    }

    g_variant_unref(metadata);
    g_variant_dict_unref(metadata_dict);

    return result;
}

static gboolean playercmd_play(PlayerctlPlayer *player, gchar **argv, gint argc,
                               GError **error) {
    GError *tmp_error = NULL;

    gboolean can_play = FALSE;
    g_object_get(player, "can-play", &can_play, NULL);

    if (!can_play) {
        return FALSE;
    }

    playerctl_player_play(player, &tmp_error);
    if (tmp_error) {
        g_propagate_error(error, tmp_error);
        return FALSE;
    }
    return TRUE;
}

static gboolean playercmd_pause(PlayerctlPlayer *player, gchar **argv, gint argc,
                                GError **error) {
    GError *tmp_error = NULL;

    gboolean can_pause = FALSE;
    g_object_get(player, "can-pause", &can_pause, NULL);

    if (!can_pause) {
        return FALSE;
    }

    playerctl_player_pause(player, &tmp_error);
    if (tmp_error) {
        g_propagate_error(error, tmp_error);
        return FALSE;
    }
    return TRUE;
}

static gboolean playercmd_play_pause(PlayerctlPlayer *player, gchar **argv, gint argc,
                                     GError **error) {
    GError *tmp_error = NULL;

    gboolean can_play = FALSE;
    g_object_get(player, "can-play", &can_play, NULL);

    if (!can_play) {
        return FALSE;
    }

    playerctl_player_play_pause(player, &tmp_error);
    if (tmp_error) {
        g_propagate_error(error, tmp_error);
        return FALSE;
    }
    return TRUE;
}

static gboolean playercmd_stop(PlayerctlPlayer *player, gchar **argv, gint argc,
                               GError **error) {
    GError *tmp_error = NULL;

    // XXX there is no CanStop propery on the mpris player. CanPlay is supposed
    // to indicate whether there is a current track. If there is no current
    // track, then I assume the player cannot stop.
    gboolean can_play = FALSE;
    g_object_get(player, "can-play", &can_play, NULL);

    if (!can_play) {
        return FALSE;
    }

    playerctl_player_stop(player, &tmp_error);
    if (tmp_error) {
        g_propagate_error(error, tmp_error);
        return FALSE;
    }
    return TRUE;

}

static gboolean playercmd_next(PlayerctlPlayer *player, gchar **argv, gint argc,
                               GError **error) {
    GError *tmp_error = NULL;

    gboolean can_go_next = FALSE;
    g_object_get(player, "can-go-next", &can_go_next, NULL);

    if (!can_go_next) {
        return FALSE;
    }

    playerctl_player_next(player, &tmp_error);
    if (tmp_error) {
        g_propagate_error(error, tmp_error);
        return FALSE;
    }
    return TRUE;
}

static gboolean playercmd_previous(PlayerctlPlayer *player, gchar **argv, gint argc,
                                   GError **error) {
    GError *tmp_error = NULL;

    gboolean can_go_previous = FALSE;
    g_object_get(player, "can-go-previous", &can_go_previous, NULL);

    if (!can_go_previous) {
        return FALSE;
    }

    playerctl_player_previous(player, &tmp_error);
    if (tmp_error) {
        g_propagate_error(error, tmp_error);
        return FALSE;
    }
    return TRUE;

}

static gboolean playercmd_open(PlayerctlPlayer *player, gchar **argv, gint argc,
                               GError **error) {
    const gchar *uri = *argv;
    GError *tmp_error = NULL;

    if (uri) {
        playerctl_player_open(player,
                              g_file_get_uri(g_file_new_for_commandline_arg(uri)),
                              &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean playercmd_position(PlayerctlPlayer *player, gchar **argv, gint argc,
                                   GError **error) {
    const gchar *position = argv[1];
    gint64 offset;
    GError *tmp_error = NULL;

    if (position) {
        if (format_string != NULL) {
            g_set_error(error, playerctl_cli_error_quark(), 1,
                    "format strings are not supported on command functions.");
            return FALSE;
        }

        char *endptr = NULL;
        offset = 1000000.0 * strtod(position, &endptr);

        if (position == endptr) {
            g_set_error(error, playerctl_cli_error_quark(), 1,
                        "Could not parse position as a number: %s\n", position);
            return FALSE;
        }

        gboolean can_seek = FALSE;
        g_object_get(player, "can-seek", &can_seek, NULL);
        if (!can_seek) {
            return FALSE;
        }

        size_t last = strlen(position) - 1;
        if (position[last] == '+' || position[last] == '-') {
            if (position[last] == '-') {
                offset *= -1;
            }

            playerctl_player_seek(player, offset, &tmp_error);
            if (tmp_error != NULL) {
                g_propagate_error(error, tmp_error);
                return FALSE;
            }
        } else {
            playerctl_player_set_position(player, offset, &tmp_error);
            if (tmp_error != NULL) {
                g_propagate_error(error, tmp_error);
                return FALSE;
            }
        }
    } else {
        g_object_get(player, "position", &offset, NULL);

        if (format_string) {
            GVariantDict *context = g_variant_dict_new(NULL);
            GVariant *position = g_variant_new_int64(offset);
            g_variant_dict_insert_value(context, "position", position);
            gchar *formatted = expand_format(format_string, context, &tmp_error);
            if (tmp_error != NULL) {
                g_propagate_error(error, tmp_error);
                g_variant_dict_unref(context);
                return FALSE;
            }

            printf("%s\n", formatted);

            g_free(formatted);
            g_variant_dict_unref(context);
        } else {
            printf("%f\n", (double)offset / 1000000.0);
        }
    }

    return TRUE;
}

static gboolean playercmd_volume(PlayerctlPlayer *player, gchar **argv, gint argc,
                                 GError **error) {
    const gchar *volume = argv[1];
    gdouble level;

    if (volume) {
        if (format_string != NULL) {
            g_set_error(error, playerctl_cli_error_quark(), 1,
                        "format strings are not supported on command functions.");
            return FALSE;
        }
        char *endptr = NULL;
        size_t last = strlen(volume) - 1;

        if (volume[last] == '+' || volume[last] == '-') {
            gdouble adjustment = strtod(volume, &endptr);

            if (volume == endptr) {
                g_set_error(error, playerctl_cli_error_quark(), 1,
                            "Could not parse volume as a number: %s\n", volume);
                return FALSE;
            }

            if (volume[last] == '-') {
                adjustment *= -1;
            }

            g_object_get(player, "volume", &level, NULL);
            level += adjustment;
        } else {
            level = strtod(volume, &endptr);
            if (volume == endptr) {
                g_set_error(error, playerctl_cli_error_quark(), 1,
                            "Could not parse volume as a number: %s\n", volume);
                return FALSE;
            }
        }

        gboolean can_control = FALSE;
        g_object_get(player, "can-control", &can_control, NULL);

        if (!can_control) {
            return FALSE;
        }

        g_object_set(player, "volume", level, NULL);
    } else {
        g_object_get(player, "volume", &level, NULL);

        if (format_string) {
            GError *tmp_error = NULL;
            GVariantDict *context = g_variant_dict_new(NULL);
            GVariant *volume_variant = g_variant_new_double(level);
            g_variant_dict_insert_value(context, "volume", volume_variant);
            gchar *formatted = expand_format(format_string, context, &tmp_error);
            if (tmp_error != NULL) {
                g_propagate_error(error, tmp_error);
                g_variant_dict_unref(context);
                return FALSE;
            }
            printf("%s\n", formatted);
        } else {
            g_print("%f\n", level);
        }
    }

    return TRUE;
}

static gboolean playercmd_status(PlayerctlPlayer *player, gchar **argv, gint argc,
                                 GError **error) {
    GError *tmp_error = NULL;
    gchar *state = NULL;
    g_object_get(player, "status", &state, NULL);

    if (format_string) {
        GVariantDict *context = g_variant_dict_new(NULL);
        GVariant *status_variant = g_variant_new_string(state);
        g_variant_dict_insert_value(context, "status", status_variant);
        gchar *formatted = expand_format(format_string, context, &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            g_variant_dict_unref(context);
            return FALSE;
        }

        printf("%s\n", formatted);

        g_variant_dict_unref(context);
        g_free(formatted);
    } else {
        printf("%s\n", state ? state : "Not available");
    }

    g_free(state);

    return TRUE;
}

static gboolean playercmd_metadata(PlayerctlPlayer *player, gchar **argv, gint argc,
                                   GError **error) {
    GError *tmp_error = NULL;

    gboolean can_play = FALSE;
    g_object_get(player, "can-play", &can_play, NULL);

    if (!can_play) {
        // skip if no current track
        return FALSE;
    }

    if (format_string != NULL) {
        gchar *data = get_metadata_formatted(player, format_string, &tmp_error);
        if (tmp_error) {
            g_propagate_error(error, tmp_error);
            return FALSE;
        }
        printf("%s\n", data);
        g_free(data);
    } else if (argc == 1) {
        gchar *data = playerctl_player_print_metadata_prop(player, NULL, &tmp_error);
        if (tmp_error) {
            g_propagate_error(error, tmp_error);
            return FALSE;
        }

        if (data != NULL) {
            printf("%s\n", data);
            g_free(data);
        }
    } else {
        for (int i = 1; i < argc; ++i) {
            const gchar *type = argv[i];
            gchar *data;

            if (g_strcmp0(type, "artist") == 0) {
                data = playerctl_player_get_artist(player, &tmp_error);
            } else if (g_strcmp0(type, "title") == 0) {
                data = playerctl_player_get_title(player, &tmp_error);
            } else if (g_strcmp0(type, "album") == 0) {
                data = playerctl_player_get_album(player, &tmp_error);
            } else {
                data = playerctl_player_print_metadata_prop(player, type, &tmp_error);
            }

            if (tmp_error) {
                g_propagate_error(error, tmp_error);
                return FALSE;
            }

            if (data != NULL) {
                printf("%s\n", data);
                g_free(data);
            }
        }
    }

    return TRUE;
}

static void on_metadata_change(PlayerctlPlayer *player, GVariant *metadata,
                               struct playercmd_args *args) {
    GError *tmp_error = NULL;
    gboolean cmd_result = playercmd_metadata(player, args->argv, args->argc, &tmp_error);
    if (tmp_error != NULL) {
        g_printerr("Error while printing metadata: %s\n", tmp_error->message);
        g_clear_error(&tmp_error);
        g_main_loop_quit(main_loop);
    }

    if (!cmd_result) {
        // metadata is empty
        printf("\n");
    }
}

static void playercmd_follow_metadata(PlayerctlPlayer *player,
                                      struct playercmd_args *args) {
    g_signal_connect(G_OBJECT(player), "metadata",
                     G_CALLBACK(on_metadata_change), args);
}

struct PlayerCommand {
    const gchar *name;
    gboolean (*func)(PlayerctlPlayer *player, gchar **argv, gint argc, GError **error);
    gboolean supports_format;
    void (*follow_func)(PlayerctlPlayer *player, struct playercmd_args *args);
} commands[] = {
    {"open", &playercmd_open, FALSE, NULL},
    {"play", &playercmd_play, FALSE, NULL},
    {"pause", &playercmd_pause, FALSE, NULL},
    {"play-pause", &playercmd_play_pause, FALSE, NULL},
    {"stop", &playercmd_stop, FALSE, NULL},
    {"next", &playercmd_next, FALSE, NULL},
    {"previous", &playercmd_previous, FALSE, NULL},
    {"position", &playercmd_position, TRUE, NULL},
    {"volume", &playercmd_volume, TRUE, NULL},
    {"status", &playercmd_status, TRUE, NULL},
    {"metadata", &playercmd_metadata, TRUE, &playercmd_follow_metadata},
};

static const struct PlayerCommand *get_player_command(gchar **argv, gint argc, GError **error) {
    for (int i = 0; i < LENGTH(commands); ++i) {
        if (g_strcmp0(commands[i].name, argv[0]) == 0) {
            if (format_string != NULL && !commands[i].supports_format) {
                g_set_error(error, playerctl_cli_error_quark(), 1,
                            "format strings are not supported on command: %s", argv[0]);
                return NULL;
            }

            if (follow && (commands[i].follow_func == NULL)) {
                g_set_error(error, playerctl_cli_error_quark(), 1,
                            "follow is not supported on command: %s", argv[0]);
                return NULL;
            }

            return &commands[i];
        }
    }

    g_set_error(error, playerctl_cli_error_quark(), 1,
                "Command not recognized: %s", argv[0]);
    return NULL;
}

static const GOptionEntry entries[] = {
    {"player", 'p', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &player_arg,
     "A comma separated list of names of players to control (default: the "
     "first available player)",
     "NAME"},
    {"all-players", 'a', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
     &select_all_players, "Select all available players to be controlled",
     NULL},
    {"ignore-player", 'i', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &ignore_player_arg,
     "A comma separated list of names of players to ignore.", "IGNORE"},
    {"format", 'f', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &format_string,
     "A format string for printing properties and metadata", NULL},
    {"follow", 'F', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &follow,
     "Block and append the query to output when it changes. Exit when the players exit.",
     NULL},
    {"list-all", 'l', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
     &list_all_players_and_exit,
     "List the names of running players that can be controlled", NULL},
    {"version", 'v', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
     &print_version_and_exit, "Print version information", NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &command, NULL,
     "COMMAND"},
    {NULL}};

static gboolean parse_setup_options(int argc, char *argv[], GError **error) {
    static const gchar *description =
        "Available Commands:"
        "\n  play                    Command the player to play"
        "\n  pause                   Command the player to pause"
        "\n  play-pause              Command the player to toggle between "
        "play/pause"
        "\n  stop                    Command the player to stop"
        "\n  next                    Command the player to skip to the next track"
        "\n  previous                Command the player to skip to the previous "
        "track"
        "\n  position [OFFSET][+/-]  Command the player to go to the position or "
        "seek forward/backward OFFSET in seconds"
        "\n  volume [LEVEL][+/-]     Print or set the volume to LEVEL from 0.0 "
        "to 1.0"
        "\n  status                  Get the play status of the player"
        "\n  metadata [KEY...]       Print metadata information for the current "
        "track. If KEY is passed,"
        "\n                          print only those values. KEY may be artist,"
        "title, album, or any key found in the metadata."
        "\n  open [URI]              Command for the player to open given URI."
        "\n                          URI can be either file path or remote URL.";
    static const gchar *summary =
        "  For players supporting the MPRIS D-Bus specification";
    GOptionContext *context = NULL;
    gboolean success;

    context = g_option_context_new("- Controller for media players");
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_set_description(context, description);
    g_option_context_set_summary(context, summary);

    success = g_option_context_parse(context, &argc, &argv, error);

    if (!success) {
        g_option_context_free(context);
        return FALSE;
    }

    if (command == NULL && !print_version_and_exit &&
        !list_all_players_and_exit) {
        gchar *help = g_option_context_get_help(context, TRUE, NULL);
        g_set_error(error, playerctl_cli_error_quark(), 1,
                    "No command entered\n\n%s", help);
        g_option_context_free(context);
        g_free(help);
        return FALSE;
    }

    g_option_context_free(context);
    return TRUE;
}

static GList *parse_player_list(gchar *player_list_arg) {
    GList *players = NULL;
    if (player_list_arg == NULL) {
        return NULL;
    }

    const gchar *delim = ",";
    gchar *token = strtok(player_list_arg, delim);
    while (token != NULL) {
        players = g_list_append(players, g_strdup(g_strstrip(token)));
        token = strtok(NULL, ",");
    }

    return players;
}

static int handle_version_flag() {
    g_print("v%s\n", PLAYERCTL_VERSION_S);
    return 0;
}

static int handle_list_all_flag() {
    GError *tmp_error = NULL;
    GList *player_names = playerctl_list_players(&tmp_error);

    if (tmp_error != NULL) {
        g_printerr("%s\n", tmp_error->message);
        return 1;
    }

    if (player_names == NULL) {
        g_printerr("No players were found\n");
        return 0;
    }

    GList *next = player_names;
    while (next != NULL) {
        gchar *name = next->data;
        printf("%s\n", name);
        next = next->next;
    }

    g_list_free_full(player_names, g_free);
    return 0;
}

static gint player_name_instance_compare(gchar *name, gchar *instance) {
    gboolean exact_match = (g_strcmp0(name, instance) == 0);
    gboolean instance_match = !exact_match && (g_str_has_prefix(instance, name) &&
            g_str_has_prefix(instance + strlen(name), ".instance"));

    if (exact_match || instance_match) {
        return 0;
    } else {
        return 1;
    }
}

static GList *select_players(GList *players, GList *all_players, GList *ignored_players) {
    GList *result = NULL;

    if (players == NULL) {
        // select the players that are not ignored
        GList *all_players_next = all_players;
        while (all_players_next != NULL) {
            gchar *current_name = all_players_next->data;
            gboolean ignored =
                (g_list_find_custom(ignored_players, current_name,
                                    (GCompareFunc)player_name_instance_compare) != NULL);

            if (!ignored && !g_list_find(result, current_name)) {
                result = g_list_append(result, current_name);
            }

            all_players_next = all_players_next->next;
        }

        return result;
    }

    GList *players_next = players;
    while (players_next) {
        gchar *player_name = players_next->data;

        GList *all_players_next = all_players;
        while (all_players_next != NULL) {
            gchar *current_name = all_players_next->data;

            if (player_name_instance_compare(player_name, current_name) == 0) {
                gboolean ignored =
                    (g_list_find_custom(ignored_players, current_name,
                                         (GCompareFunc)player_name_instance_compare) != NULL);
                if (!ignored && !g_list_find(result, current_name)) {
                    result = g_list_append(result, current_name);
                }
            }

            all_players_next = all_players_next->next;
        }

        players_next = players_next->next;
    }

    return result;
}

static gchar *player_id_from_bus_name(const gchar *bus_name) {
    const size_t prefix_len = strlen(MPRIS_PREFIX);

    if (bus_name == NULL ||
            !g_str_has_prefix(bus_name, MPRIS_PREFIX) ||
            strlen(bus_name) <= prefix_len) {
        return NULL;
    }

    return g_strdup(bus_name + prefix_len);
}

struct owner_changed_user_data {
    GList *all_players;
    GList *players;
    GList *ignored_players;
};

static void set_followed_player(PlayerctlPlayer *player, GError **error) {
    GError *tmp_error = NULL;

    if (followed_player != NULL) {
        g_object_unref(followed_player);
        followed_player = NULL;
    }
    gboolean playercmd_result = FALSE;

    if (player != NULL) {
        followed_player = player;
        const struct PlayerCommand *player_cmd =
            get_player_command(playercmd_args->argv, playercmd_args->argc,
                               &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return;
        }

        playercmd_result = player_cmd->func(followed_player, playercmd_args->argv, playercmd_args->argc, &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return;
        }

        player_cmd->follow_func(followed_player, playercmd_args);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return;
        }
    }

    if (!playercmd_result) {
        printf("\n");
    }
}

static void set_followed_player_by_name(gchar *player_name, GError **error) {
    GError *tmp_error = NULL;
    PlayerctlPlayer *player = NULL;

    if (player_name != NULL) {
        player = playerctl_player_new(player_name, &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return;
        }
    }

    set_followed_player(player, &tmp_error);
    if (tmp_error != NULL) {
        g_object_unref(player);
        g_propagate_error(error, tmp_error);
    }
}

static void dbus_name_owner_changed_callback(GDBusProxy *proxy, gchar *sender_name,
                                      gchar *signal_name, GVariant *parameters,
                                      gpointer _data) {
    struct owner_changed_user_data *data = _data;
    GList *selected_players = NULL;
    GError *error = NULL;

    if (g_strcmp0(signal_name, "NameOwnerChanged") != 0) {
        return;
    }

    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(sss)"))) {
        g_warning("Got unknown parameters on org.freedesktop.DBus "
                  "NameOwnerChange signal: %s",
                  g_variant_get_type_string(parameters));
        return;
    }

    GVariant *name_variant = g_variant_get_child_value(parameters, 0);
    const gchar *name = g_variant_get_string(name_variant, NULL);

    gchar *player_id = player_id_from_bus_name(name);

    if (player_id == NULL) {
        g_variant_unref(name_variant);
        return;
    }

    GVariant *previous_owner_variant = g_variant_get_child_value(parameters, 1);
    const gchar *previous_owner = g_variant_get_string(previous_owner_variant, NULL);

    GVariant *new_owner_variant = g_variant_get_child_value(parameters, 2);
    const gchar *new_owner = g_variant_get_string(new_owner_variant, NULL);

    GList *player_entry = NULL;
    if (strlen(new_owner) == 0 && strlen(previous_owner) != 0) {
        // the name has vanished
        player_entry =
            g_list_find_custom(data->all_players, name + strlen(MPRIS_PREFIX), (GCompareFunc)g_strcmp0);

        if (player_entry != NULL) {
            data->all_players = g_list_remove_link(data->all_players, player_entry);
            g_list_free_full(player_entry, g_free);
        }

        if (followed_player != NULL) {
            gchar *followed_player_id = NULL;
            g_object_get(followed_player, "player-id", &followed_player_id, NULL);

            if (g_strcmp0(followed_player_id, player_id) == 0) {
                set_followed_player(NULL, NULL);
            }

            g_free(followed_player_id);
        }
    } else if (strlen(previous_owner) == 0 && strlen(new_owner) != 0) {
        // the name has appeared
        player_entry =
            g_list_find_custom(data->all_players, name + strlen(MPRIS_PREFIX), (GCompareFunc)g_strcmp0);
        if (player_entry == NULL) {
            data->all_players =
                g_list_prepend(data->all_players, g_strdup(name + strlen(MPRIS_PREFIX)));
        }
    }

    selected_players = select_players(data->players, data->all_players, data->ignored_players);
    if (selected_players != NULL) {
        // there is a new candidate for player selection
        gchar *first_selected = selected_players->data;

        if (followed_player == NULL) {
            // not following a player, follow this one
            set_followed_player_by_name(first_selected, &error);
            if (error != NULL) {
                goto out;
            }
        } else {
            gchar *followed_player_id = NULL;
            g_object_get(followed_player, "player-id", &followed_player_id, NULL);

            if (data->players == NULL) {
                // if no player arguments were passed, always follow the most
                // recently opened player.
                if (g_strcmp0(followed_player_id, first_selected) != 0) {
                    set_followed_player_by_name(first_selected, &error);
                    if (error != NULL) {
                        g_free(followed_player_id);
                        goto out;
                    }
                }
            } else {
                // if player arguments were passed, follow the most recently
                // opened player in the order they were passed on the command
                // line.
                GList *next = data->players;
                while (next != NULL) {
                    gchar *name = next->data;

                    GList *match =
                        g_list_find_custom(data->all_players, name,
                                          (GCompareFunc)player_name_instance_compare);

                    if (match != NULL) {
                        gchar *match_name = match->data;
                        if (g_strcmp0(followed_player_id, match_name) != 0) {
                            set_followed_player_by_name(match_name, &error);
                            if (error != NULL) {
                                g_free(followed_player_id);
                                goto out;
                            }
                        }
                        break;
                    }

                    next = next->next;
                }
            }
        }
    }

out:

    if (error != NULL) {
        g_printerr("Could not connect to player: %s\n", error->message);
        g_clear_error(&error);
        g_main_loop_quit(main_loop);
    }
    g_list_free(selected_players);
    g_variant_unref(name_variant);
    g_variant_unref(previous_owner_variant);
    g_variant_unref(new_owner_variant);
}


int main(int argc, char *argv[]) {
    PlayerctlPlayer *player;
    GError *error = NULL;
    guint num_commands = 0;

    GDBusProxy *proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION,
            G_DBUS_PROXY_FLAGS_NONE, NULL, "org.freedesktop.DBus",
            "/org/freedesktop/DBus", "org.freedesktop.DBus", NULL, &error);
    if (error != NULL) {
        g_printerr("%s\n", error->message);
        g_clear_error(&error);
        exit(1);
    }

    // seems to be required to print unicode (see #8)
    setlocale(LC_CTYPE, "");

    if (!parse_setup_options(argc, argv, &error)) {
        g_printerr("%s\n", error->message);
        g_clear_error(&error);
        exit(0);
    }

    if (print_version_and_exit) {
        int result = handle_version_flag();
        exit(result);
    }

    if (list_all_players_and_exit) {
        int result = handle_list_all_flag();
        exit(result);
    }

    num_commands = g_strv_length(command);

    GList *all_players = playerctl_list_players(&error);
    if (error != NULL) {
        g_printerr("%s\n", error->message);
        g_clear_error(&error);
        exit(1);
    }

    const struct PlayerCommand *player_cmd = get_player_command(command, num_commands, &error);
    if (error != NULL) {
        g_printerr("Could not execute command: %s\n", error->message);
        g_clear_error(&error);
        exit(1);
    }

    if (all_players == NULL && !follow) {
        g_printerr("No players were found\n");
        exit(0);
    }

    GList *players = parse_player_list(player_arg);

    GList *ignored_players = parse_player_list(ignore_player_arg);

    GList *selected_players = select_players(players, all_players, ignored_players);

    if (selected_players == NULL && !follow) {
        g_printerr("No players were found\n");
        goto end;
    }

    int status = 0;
    GList *next = selected_players;
    GList *player_objects = NULL;
    playercmd_args = playercmd_args_create(command, num_commands);
    while (next != NULL) {
        gchar *player_name = next->data;

        player = playerctl_player_new(player_name, &error);
        if (error != NULL) {
            g_printerr("Connection to player failed: %s\n", error->message);
            status = 1;
            goto end;
        }

        gboolean result = player_cmd->func(player, command, num_commands, &error);
        if (error != NULL) {
            g_printerr("Could not execute command: %s\n", error->message);
            g_clear_error(&error);
            g_object_unref(player);
            status = 1;
            break;
        }

        if (follow && player_cmd->follow_func) {
            player_cmd->follow_func(player, playercmd_args);
            followed_player = player;
            break;
        }

        g_object_unref(player);

        if (result && !select_all_players) {
            break;
        }

        next = next->next;
    }

end:
    g_list_free(selected_players);

    if (status == 0 && follow) {
        struct owner_changed_user_data *data =
            calloc(1, sizeof(struct owner_changed_user_data));
        data->players = players;
        data->all_players = all_players;
        data->ignored_players = ignored_players;

        g_signal_connect(G_DBUS_PROXY(proxy), "g-signal",
                         G_CALLBACK(dbus_name_owner_changed_callback),
                         data);
        main_loop = g_main_loop_new(NULL, FALSE);
        g_main_loop_run(main_loop);
        free(data);
        playercmd_args_destroy(playercmd_args);
        g_main_loop_unref(main_loop);
    }

    g_list_free_full(all_players, g_free);
    g_list_free_full(players, g_free);
    g_list_free_full(ignored_players, g_free);

    g_list_free_full(player_objects, g_object_unref);

    exit(status);
}
