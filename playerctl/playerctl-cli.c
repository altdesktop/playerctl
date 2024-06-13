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

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <playerctl/playerctl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "playerctl-common.h"
#include "playerctl-formatter.h"
#include "playerctl-player-private.h"

#define LENGTH(array) (sizeof array / sizeof array[0])

// clang-format off
G_DEFINE_QUARK(playerctl-cli-error-quark, playerctl_cli_error);
// clang-format on

/* The CLI will exit with this exit status */
static gint exit_status = 0;
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
/* If true, don't print error messages related to status. */
static gboolean no_status_error_messages;
/* The commands passed on the command line, filled in via G_OPTION_REMAINING. */
static gchar **command_arg = NULL;
/* A format string for printing properties and metadata */
static gchar *format_string_arg = NULL;
/* The formatter for the format string argument if present */
static PlayerctlFormatter *formatter = NULL;
/* Block and follow the command */
static gboolean follow = FALSE;
/* The main loop for the follow command */
static GMainLoop *main_loop = NULL;
/* The last output printed by the cli */
static gchar *last_output = NULL;
/* The manager of all the players we connect to */
static PlayerctlPlayerManager *manager = NULL;
/* List of player names parsed from the --player arg */
static GList *player_names = NULL;
/* List of ignored player names passed from the --ignore-player arg*/
static GList *ignored_player_names = NULL;

/* forward definitions */
static void managed_players_execute_command(GError **error);

/*
 * Sometimes players may notify metadata when nothing we care about has
 * changed, so we have this to avoid printing duplicate lines in follow
 * mode. Prints a newline if output is NULL which denotes that the property has
 * been cleared. Only use this in follow mode.
 *
 * This consumes the output string.
 */
static void cli_print_output(gchar *output) {
    if (output == NULL && last_output == NULL) {
        return;
    }

    if (output == NULL) {
        output = g_strdup("\n");
    }

    if (g_strcmp0(output, last_output) == 0) {
        g_free(output);
        return;
    }

    printf("%s", output);
    fflush(stdout);
    g_free(last_output);
    last_output = output;
}

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

static gchar *get_metadata_formatted(PlayerctlPlayer *player, GError **error) {
    GError *tmp_error = NULL;
    GVariant *metadata = NULL;

    g_return_val_if_fail(formatter != NULL, NULL);

    g_object_get(player, "metadata", &metadata, NULL);
    if (metadata == NULL) {
        return NULL;
    }

    if (g_variant_n_children(metadata) == 0) {
        g_variant_unref(metadata);
        return NULL;
    }

    GVariantDict *context =
        playerctl_formatter_default_template_context(formatter, player, metadata);

    gchar *result = playerctl_formatter_expand_format(formatter, context, &tmp_error);
    if (tmp_error) {
        g_variant_unref(metadata);
        g_variant_dict_unref(context);
        g_propagate_error(error, tmp_error);
        return NULL;
    }

    g_variant_unref(metadata);
    g_variant_dict_unref(context);

    return result;
}

static gboolean playercmd_play(PlayerctlPlayer *player, gchar **argv, gint argc, gchar **output,
                               GError **error) {
    GError *tmp_error = NULL;
    gchar *instance = pctl_player_get_instance(player);

    gboolean can_play = FALSE;
    g_object_get(player, "can-play", &can_play, NULL);

    if (!can_play) {
        g_debug("%s: can-play is false, skipping", instance);
        return FALSE;
    }

    playerctl_player_play(player, &tmp_error);
    if (tmp_error) {
        g_propagate_error(error, tmp_error);
        return FALSE;
    }
    return TRUE;
}

static gboolean playercmd_pause(PlayerctlPlayer *player, gchar **argv, gint argc, gchar **output,
                                GError **error) {
    GError *tmp_error = NULL;
    gchar *instance = pctl_player_get_instance(player);

    gboolean can_pause = FALSE;
    g_object_get(player, "can-pause", &can_pause, NULL);

    if (!can_pause) {
        g_debug("%s: player cannot pause", instance);
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
                                     gchar **output, GError **error) {
    GError *tmp_error = NULL;
    gchar *instance = pctl_player_get_instance(player);

    gboolean can_play = FALSE;
    g_object_get(player, "can-play", &can_play, NULL);

    if (!can_play) {
        g_debug("%s: can-play is false, skipping", instance);
        return FALSE;
    }

    playerctl_player_play_pause(player, &tmp_error);
    if (tmp_error) {
        g_propagate_error(error, tmp_error);
        return FALSE;
    }
    return TRUE;
}

static gboolean playercmd_stop(PlayerctlPlayer *player, gchar **argv, gint argc, gchar **output,
                               GError **error) {
    GError *tmp_error = NULL;
    gchar *instance = pctl_player_get_instance(player);

    // XXX there is no CanStop property on the mpris player. CanPlay is supposed
    // to indicate whether there is a current track. If there is no current
    // track, then I assume the player cannot stop.
    gboolean can_play = FALSE;
    g_object_get(player, "can-play", &can_play, NULL);

    if (!can_play) {
        g_debug("%s: can-play is false, skipping", instance);
        return FALSE;
    }

    playerctl_player_stop(player, &tmp_error);
    if (tmp_error) {
        g_propagate_error(error, tmp_error);
        return FALSE;
    }
    return TRUE;
}

static gboolean playercmd_next(PlayerctlPlayer *player, gchar **argv, gint argc, gchar **output,
                               GError **error) {
    GError *tmp_error = NULL;
    gchar *instance = pctl_player_get_instance(player);

    gboolean can_go_next = FALSE;
    g_object_get(player, "can-go-next", &can_go_next, NULL);

    if (!can_go_next) {
        g_debug("%s: player cannot go next", instance);
        return FALSE;
    }

    playerctl_player_next(player, &tmp_error);
    if (tmp_error) {
        g_propagate_error(error, tmp_error);
        return FALSE;
    }
    return TRUE;
}

static gboolean playercmd_previous(PlayerctlPlayer *player, gchar **argv, gint argc, gchar **output,
                                   GError **error) {
    GError *tmp_error = NULL;
    gchar *instance = pctl_player_get_instance(player);

    gboolean can_go_previous = FALSE;
    g_object_get(player, "can-go-previous", &can_go_previous, NULL);

    if (!can_go_previous) {
        g_debug("%s: player cannot go previous", instance);
        return FALSE;
    }

    playerctl_player_previous(player, &tmp_error);
    if (tmp_error) {
        g_propagate_error(error, tmp_error);
        return FALSE;
    }
    return TRUE;
}

static gboolean playercmd_open(PlayerctlPlayer *player, gchar **argv, gint argc, gchar **output,
                               GError **error) {
    const gchar *uri = argv[1];
    GError *tmp_error = NULL;
    gchar *instance = pctl_player_get_instance(player);

    gboolean can_control = FALSE;
    g_object_get(player, "can-control", &can_control, NULL);

    if (!can_control) {
        g_debug("%s: player cannot control", instance);
        return FALSE;
    }

    if (uri) {
        GFile *file = g_file_new_for_commandline_arg(uri);
        gboolean exists = g_file_query_exists(file, NULL);
        gchar *full_uri = NULL;

        if (exists) {
            // it's a file, so pass the absolute path of the file
            full_uri = g_file_get_uri(file);
        } else {
            // it may be some other scheme, just pass the uri directly
            full_uri = g_strdup(uri);
        }

        playerctl_player_open(player, full_uri, &tmp_error);

        g_free(full_uri);
        g_object_unref(file);

        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean playercmd_position(PlayerctlPlayer *player, gchar **argv, gint argc, gchar **output,
                                   GError **error) {
    const gchar *position = argv[1];
    gint64 offset;
    GError *tmp_error = NULL;
    gchar *instance = pctl_player_get_instance(player);

    if (position) {
        if (format_string_arg != NULL) {
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
        if (formatter != NULL) {
            GVariantDict *context =
                playerctl_formatter_default_template_context(formatter, player, NULL);
            gchar *formatted = playerctl_formatter_expand_format(formatter, context, &tmp_error);
            if (tmp_error != NULL) {
                g_propagate_error(error, tmp_error);
                g_variant_dict_unref(context);
                return FALSE;
            }

            *output = g_strdup_printf("%s\n", formatted);

            g_free(formatted);
            g_variant_dict_unref(context);
        } else {
            if (!pctl_player_has_cached_property(player, "Position")) {
                g_debug("%s: player has no cached position, skipping", instance);
                return FALSE;
            }
            g_object_get(player, "position", &offset, NULL);
            *output = g_strdup_printf("%f\n", (double)offset / 1000000.0);
        }
    }

    return TRUE;
}

static gboolean playercmd_volume(PlayerctlPlayer *player, gchar **argv, gint argc, gchar **output,
                                 GError **error) {
    GError *tmp_error = NULL;
    const gchar *volume = argv[1];
    gdouble level;
    gchar *instance = pctl_player_get_instance(player);

    if (volume) {
        if (format_string_arg != NULL) {
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
                            "could not parse volume as a number: %s\n", volume);
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
                            "could not parse volume as a number: %s\n", volume);
                return FALSE;
            }
        }

        gboolean can_control = FALSE;
        g_object_get(player, "can-control", &can_control, NULL);

        if (!can_control) {
            g_debug("%s: player cannot control", instance);
            return FALSE;
        }

        g_debug("%s: setting volume to %f\n", instance, level);
        playerctl_player_set_volume(player, level, &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return FALSE;
        }
    } else {
        if (!pctl_player_has_cached_property(player, "Volume")) {
            g_debug("%s: player has no volume set, skipping", instance);
            return FALSE;
        }

        g_object_get(player, "volume", &level, NULL);

        if (formatter != NULL) {
            GVariantDict *context =
                playerctl_formatter_default_template_context(formatter, player, NULL);
            gchar *formatted = playerctl_formatter_expand_format(formatter, context, &tmp_error);
            if (tmp_error != NULL) {
                g_propagate_error(error, tmp_error);
                return FALSE;
            }
            *output = g_strdup_printf("%s\n", formatted);
            g_free(formatted);
        } else {
            *output = g_strdup_printf("%f\n", level);
        }
    }

    return TRUE;
}

static gboolean playercmd_status(PlayerctlPlayer *player, gchar **argv, gint argc, gchar **output,
                                 GError **error) {
    GError *tmp_error = NULL;
    gchar *instance = pctl_player_get_instance(player);

    if (!pctl_player_has_cached_property(player, "PlaybackStatus")) {
        g_debug("%s: player has no playback status set, skipping", instance);
        return FALSE;
    }

    if (formatter != NULL) {
        GVariantDict *context =
            playerctl_formatter_default_template_context(formatter, player, NULL);
        gchar *formatted = playerctl_formatter_expand_format(formatter, context, &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            g_variant_dict_unref(context);
            return FALSE;
        }

        *output = g_strdup_printf("%s\n", formatted);

        g_variant_dict_unref(context);
        g_free(formatted);
    } else {
        PlayerctlPlaybackStatus status = 0;
        g_object_get(player, "playback-status", &status, NULL);
        const gchar *status_str = pctl_playback_status_to_string(status);
        assert(status_str != NULL);
        *output = g_strdup_printf("%s\n", status_str);
    }

    return TRUE;
}

static gboolean playercmd_shuffle(PlayerctlPlayer *player, gchar **argv, gint argc, gchar **output,
                                  GError **error) {
    GError *tmp_error = NULL;
    gchar *instance = pctl_player_get_instance(player);

    if (argc > 1) {
        gchar *status_str = argv[1];
        gboolean status = FALSE;

        if (strcasecmp(status_str, "on") == 0) {
            status = TRUE;
        } else if (strcasecmp(status_str, "off") == 0) {
            status = FALSE;
        } else if (strcasecmp(status_str, "toggle") == 0) {
            g_object_get(player, "shuffle", &status, NULL);
            status = !status;
        } else {
            g_set_error(error, playerctl_cli_error_quark(), 1,
                        "Got unknown shuffle status: '%s' (expected 'on', "
                        "'off', or 'toggle').",
                        argv[1]);
            return FALSE;
        }

        gboolean can_control = FALSE;
        g_object_get(player, "can-control", &can_control, NULL);
        if (!can_control) {
            g_debug("%s: player cannot control, not setting shuffle", instance);
            return FALSE;
        }

        playerctl_player_set_shuffle(player, status, &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return FALSE;
        }
        g_debug("%s: setting shuffle to %d\n", instance, status);
    } else {
        if (!pctl_player_has_cached_property(player, "Shuffle")) {
            g_debug("%s: player has no shuffle status set, skipping", instance);
            return FALSE;
        }

        if (formatter != NULL) {
            GVariantDict *context =
                playerctl_formatter_default_template_context(formatter, player, NULL);
            gchar *formatted = playerctl_formatter_expand_format(formatter, context, &tmp_error);
            if (tmp_error != NULL) {
                g_propagate_error(error, tmp_error);
                g_variant_dict_unref(context);
                return FALSE;
            }

            *output = g_strdup_printf("%s\n", formatted);

            g_variant_dict_unref(context);
            g_free(formatted);
        } else {
            gboolean status = FALSE;
            g_object_get(player, "shuffle", &status, NULL);
            if (status) {
                *output = g_strdup("On\n");
            } else {
                *output = g_strdup("Off\n");
            }
        }
    }

    return TRUE;
}

static gboolean playercmd_loop(PlayerctlPlayer *player, gchar **argv, gint argc, gchar **output,
                               GError **error) {
    GError *tmp_error = NULL;
    gchar *instance = pctl_player_get_instance(player);

    if (argc > 1) {
        gchar *status_str = argv[1];
        PlayerctlLoopStatus status = 0;
        if (!pctl_parse_loop_status(status_str, &status)) {
            g_set_error(error, playerctl_cli_error_quark(), 1,
                        "Got unknown loop status: '%s' (expected 'none', "
                        "'playlist', or 'track').",
                        argv[1]);
            return FALSE;
        }

        gboolean can_control = FALSE;
        g_object_get(player, "can-control", &can_control, NULL);
        if (!can_control) {
            g_debug("%s: player cannot control", instance);
            return FALSE;
        }

        g_debug("%s: setting loop status to %d\n", instance, status);
        playerctl_player_set_loop_status(player, status, &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return FALSE;
        }
    } else {
        if (formatter != NULL) {
            GVariantDict *context =
                playerctl_formatter_default_template_context(formatter, player, NULL);
            gchar *formatted = playerctl_formatter_expand_format(formatter, context, &tmp_error);
            if (tmp_error != NULL) {
                g_propagate_error(error, tmp_error);
                g_variant_dict_unref(context);
                return FALSE;
            }

            *output = g_strdup_printf("%s\n", formatted);

            g_variant_dict_unref(context);
            g_free(formatted);
        } else {
            if (!pctl_player_has_cached_property(player, "LoopStatus")) {
                g_debug("%s: player has no cached loop status, skipping", instance);
                return FALSE;
            }
            PlayerctlLoopStatus status = 0;
            g_object_get(player, "loop-status", &status, NULL);
            const gchar *status_str = pctl_loop_status_to_string(status);
            assert(status_str != NULL);
            *output = g_strdup_printf("%s\n", status_str);
        }
    }

    return TRUE;
}

static gboolean playercmd_metadata(PlayerctlPlayer *player, gchar **argv, gint argc, gchar **output,
                                   GError **error) {
    g_debug("metadata command for player: %s", pctl_player_get_instance(player));
    GError *tmp_error = NULL;
    gchar *instance = pctl_player_get_instance(player);

    gboolean can_play = FALSE;
    g_object_get(player, "can-play", &can_play, NULL);

    if (!can_play) {
        // XXX: This is gotten from the property cache which may not be up to
        // date in all cases. If there is a bug with a player not printing
        // metadata, look here.
        g_debug("%s: can-play is false, skipping", instance);
        return FALSE;
    }

    if (format_string_arg != NULL) {
        gchar *data = get_metadata_formatted(player, &tmp_error);
        if (tmp_error) {
            g_propagate_error(error, tmp_error);
            return FALSE;
        }
        if (data != NULL) {
            *output = g_strdup_printf("%s\n", data);
            g_free(data);
        } else {
            g_debug("%s: no metadata, skipping", instance);
            return FALSE;
        }
    } else if (argc == 1) {
        gchar *data = playerctl_player_print_metadata_prop(player, NULL, &tmp_error);
        if (tmp_error) {
            g_propagate_error(error, tmp_error);
            return FALSE;
        }

        if (data != NULL) {
            *output = g_strdup_printf("%s\n", data);
            g_free(data);
        } else {
            return FALSE;
        }
    } else {
        *output = g_strdup("");
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
                gchar *tmp = *output;
                *output = g_strdup_printf("%s%s\n", *output, data);
                g_free(tmp);
                g_free(data);
            } else {
                return FALSE;
            }
        }
    }

    return TRUE;
}

static void managed_player_properties_callback(PlayerctlPlayer *player, gpointer data) {
    playerctl_player_manager_move_player_to_top(manager, player);
    GError *error = NULL;
    managed_players_execute_command(&error);
}

static gboolean playercmd_tick_callback(gpointer data) {
    GError *tmp_error = NULL;
    managed_players_execute_command(&tmp_error);
    if (tmp_error != NULL) {
        exit_status = 1;
        g_printerr("Error while executing command: %s\n", tmp_error->message);
        g_clear_error(&tmp_error);
        g_main_loop_quit(main_loop);
        return FALSE;
    }
    return TRUE;
}

struct player_command {
    const gchar *name;
    gboolean (*func)(PlayerctlPlayer *player, gchar **argv, gint argc, gchar **output,
                     GError **error);
    gboolean supports_format;
    const gchar *follow_signal;
} player_commands[] = {
    {"open", &playercmd_open, FALSE, NULL},
    {"play", &playercmd_play, FALSE, NULL},
    {"pause", &playercmd_pause, FALSE, NULL},
    {"play-pause", &playercmd_play_pause, FALSE, NULL},
    {"stop", &playercmd_stop, FALSE, NULL},
    {"next", &playercmd_next, FALSE, NULL},
    {"previous", &playercmd_previous, FALSE, NULL},
    {"position", &playercmd_position, TRUE, "seeked"},
    {"volume", &playercmd_volume, TRUE, "volume"},
    {"status", &playercmd_status, TRUE, "playback-status"},
    {"loop", &playercmd_loop, TRUE, "loop-status"},
    {"shuffle", &playercmd_shuffle, TRUE, "shuffle"},
    {"metadata", &playercmd_metadata, TRUE, "metadata"},
};

static const struct player_command *get_player_command(gchar **argv, gint argc, GError **error) {
    for (gsize i = 0; i < LENGTH(player_commands); ++i) {
        const struct player_command command = player_commands[i];
        if (g_strcmp0(command.name, argv[0]) == 0) {
            if (format_string_arg != NULL && !command.supports_format) {
                g_set_error(error, playerctl_cli_error_quark(), 1,
                            "format strings are not supported on command: %s", argv[0]);
                return NULL;
            }

            if (follow && (command.follow_signal == NULL)) {
                g_set_error(error, playerctl_cli_error_quark(), 1,
                            "follow is not supported on command: %s", argv[0]);
                return NULL;
            }

            return &player_commands[i];
        }
    }

    g_set_error(error, playerctl_cli_error_quark(), 1, "Command not recognized: %s", argv[0]);

    return NULL;
}

static const GOptionEntry entries[] = {
    {"player", 'p', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &player_arg,
     "A comma separated list of names of players to control (default: the "
     "first available player)",
     "NAME"},
    {"all-players", 'a', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &select_all_players,
     "Select all available players to be controlled", NULL},
    {"ignore-player", 'i', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &ignore_player_arg,
     "A comma separated list of names of players to ignore.", "IGNORE"},
    {"format", 'f', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &format_string_arg,
     "A format string for printing properties and metadata", NULL},
    {"follow", 'F', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &follow,
     "Block and append the query to output when it changes for the most recently updated player.",
     NULL},
    {"list-all", 'l', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &list_all_players_and_exit,
     "List the names of running players that can be controlled", NULL},
    {"no-messages", 's', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &no_status_error_messages,
     "Suppress diagnostic messages", NULL},
    {"version", 'v', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &print_version_and_exit,
     "Print version information", NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &command_arg, NULL, "COMMAND"},
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
        "\n                          URI can be either file path or remote URL."
        "\n  loop [STATUS]           Print or set the loop status."
        "\n                          Can be \"None\", \"Track\", or \"Playlist\"."
        "\n  shuffle [STATUS]        Print or set the shuffle status."
        "\n                          Can be \"On\", \"Off\", or \"Toggle\".";

    static const gchar *summary = "  For players supporting the MPRIS D-Bus specification";
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

    if (command_arg == NULL && !print_version_and_exit && !list_all_players_and_exit) {
        gchar *help = g_option_context_get_help(context, TRUE, NULL);
        printf("%s\n", help);
        g_option_context_free(context);
        g_free(help);
        exit(1);
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

static gboolean name_is_selected(const gchar *name) {
    if (ignored_player_names != NULL) {
        gboolean ignored =
            (g_list_find_custom(ignored_player_names, name,
                                (GCompareFunc)pctl_player_name_string_instance_compare) != NULL);
        if (ignored) {
            return FALSE;
        }
    }

    if (player_names != NULL) {
        gboolean selected =
            (g_list_find_custom(player_names, name,
                                (GCompareFunc)pctl_player_name_string_instance_compare) != NULL);
        if (!selected) {
            return FALSE;
        }
    }

    return TRUE;
}

static int handle_version_flag() {
    g_print("v%s\n", PLAYERCTL_VERSION_S);
    return 0;
}

static int handle_list_all_flag() {
    GError *tmp_error = NULL;
    GList *player_names_list = playerctl_list_players(&tmp_error);

    player_names_list =
        g_list_sort_with_data(player_names_list, player_name_compare_func, (gpointer)player_names);

    if (tmp_error != NULL) {
        g_printerr("%s\n", tmp_error->message);
        return 1;
    }

    gboolean one_selected = FALSE;
    GList *l = NULL;
    for (l = player_names_list; l != NULL; l = l->next) {
        PlayerctlPlayerName *name = l->data;
        if (name_is_selected(name->instance)) {
            one_selected = TRUE;
            printf("%s\n", name->instance);
        }
    }

    if (!one_selected && !no_status_error_messages) {
        g_printerr("No players found\n");
    }

    pctl_player_name_list_destroy(player_names_list);
    return 0;
}

static void managed_players_execute_command(GError **error) {
    GError *tmp_error = NULL;

    const struct player_command *player_cmd =
        get_player_command(playercmd_args->argv, playercmd_args->argc, &tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(error, tmp_error);
        return;
    }
    g_debug("executing command: %s", player_cmd->name);
    assert(player_cmd->func != NULL);

    gboolean did_command = FALSE;
    GList *players = NULL;
    g_object_get(manager, "players", &players, NULL);
    GList *l = NULL;
    for (l = players; l != NULL; l = l->next) {
        PlayerctlPlayer *player = PLAYERCTL_PLAYER(l->data);
        assert(player != NULL);
        gchar *output = NULL;

        gboolean result = player_cmd->func(player, playercmd_args->argv, playercmd_args->argc,
                                           &output, &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            g_free(output);
            return;
        }

        if (output != NULL) {
            cli_print_output(output);
        }
        did_command = did_command || result;

        if (result) {
            break;
        }
    }

    if (!did_command) {
        cli_print_output(NULL);
    }
}

static void name_appeared_callback(PlayerctlPlayerManager *manager, PlayerctlPlayerName *name,
                                   gpointer *data) {
    if (!name_is_selected(name->instance)) {
        return;
    }

    g_debug("a selected name appeared: %s (source=%d)", name->instance, name->source);

    // make sure we are not managing the player already
    GList *players = NULL;
    g_object_get(manager, "players", &players, NULL);
    for (GList *l = players; l != NULL; l = l->next) {
        PlayerctlPlayer *player = PLAYERCTL_PLAYER(l->data);
        gchar *instance = pctl_player_get_instance(player);
        PlayerctlSource source = 0;
        g_object_get(player, "source", &source, NULL);

        if (source == name->source && g_strcmp0(instance, name->instance) == 0) {
            g_debug("this player is already managed: %s (source=%d)", name->instance, name->source);
            return;
        }
    }

    GError *error = NULL;
    PlayerctlPlayer *player = playerctl_player_new_from_name(name, &error);
    if (error != NULL) {
        exit_status = 1;
        g_printerr("Could not connect to player: %s\n", error->message);
        g_clear_error(&error);
        g_main_loop_quit(main_loop);
        return;
    }

    playerctl_player_manager_manage_player(manager, player);
    g_object_unref(player);
}

static void init_managed_player(PlayerctlPlayer *player, const struct player_command *player_cmd) {
    assert(player_cmd->follow_signal != NULL);
    g_signal_connect(G_OBJECT(player), player_cmd->follow_signal,
                     G_CALLBACK(managed_player_properties_callback), playercmd_args);

    if (formatter != NULL) {
        for (gsize i = 0; i < LENGTH(player_commands); ++i) {
            const struct player_command cmd = player_commands[i];
            if (&cmd != player_cmd && cmd.follow_signal != NULL &&
                g_strcmp0(cmd.name, "metadata") != 0 &&
                playerctl_formatter_contains_key(formatter, cmd.name)) {
                g_signal_connect(G_OBJECT(player), cmd.follow_signal,
                                 G_CALLBACK(managed_player_properties_callback), playercmd_args);
            }
        }
    }
}

static void player_appeared_callback(PlayerctlPlayerManager *manager, PlayerctlPlayer *player,
                                     gpointer *data) {
    GError *error = NULL;
    const struct player_command *player_cmd =
        get_player_command(playercmd_args->argv, playercmd_args->argc, &error);
    if (error != NULL) {
        exit_status = 1;
        g_printerr("Could not get player command: %s\n", error->message);
        g_clear_error(&error);
        g_main_loop_quit(main_loop);
        return;
    }

    init_managed_player(player, player_cmd);

    managed_players_execute_command(&error);
    if (error != NULL) {
        exit_status = 1;
        g_printerr("Could not execute command: %s\n", error->message);
        g_clear_error(&error);
        g_main_loop_quit(main_loop);
        return;
    }
}

static void player_vanished_callback(PlayerctlPlayerManager *manager, PlayerctlPlayer *player,
                                     gpointer *data) {
    GError *error = NULL;

    managed_players_execute_command(&error);
    if (error != NULL) {
        exit_status = 1;
        g_printerr("Could not execute command: %s\n", error->message);
        g_clear_error(&error);
        g_main_loop_quit(main_loop);
        return;
    }
}

gint player_name_string_compare_func(gconstpointer a, gconstpointer b, gpointer user_data) {
    const gchar *name_a = a;
    const gchar *name_b = b;
    GList *names = user_data;

    if (g_strcmp0(name_a, name_b) == 0) {
        return 0;
    }

    int a_match_index = -1;
    int b_match_index = -1;

    int any_index = INT_MAX;
    int i = 0;
    GList *l = NULL;
    for (l = names; l != NULL; l = l->next) {
        gchar *name = l->data;

        if (g_strcmp0(name, "%any") == 0) {
            if (any_index == INT_MAX) {
                any_index = i;
            }
            ++i;
            continue;
        }

        if (pctl_player_name_string_instance_compare(name, name_a) == 0) {
            if (a_match_index == -1) {
                a_match_index = i;
            }
        }

        if (pctl_player_name_string_instance_compare(name, name_b) == 0) {
            if (b_match_index == -1) {
                b_match_index = i;
            }
        }
        ++i;
    }

    if (a_match_index == -1 && b_match_index == -1) {
        // neither are in the list
        return 0;
    } else if (a_match_index == -1) {
        // b is in the list
        return (b_match_index < any_index ? 1 : -1);
    } else if (b_match_index == -1) {
        // a is in the list
        return (a_match_index < any_index ? -1 : 1);
    } else if (a_match_index == b_match_index) {
        // preserve order
        return 0;
    } else {
        return (a_match_index < b_match_index ? -1 : 1);
    }
}

gint player_name_compare_func(gconstpointer a, gconstpointer b, gpointer user_data) {
    const PlayerctlPlayerName *name_a = a;
    const PlayerctlPlayerName *name_b = b;
    return player_name_string_compare_func(name_a->instance, name_b->instance, user_data);
}

gint player_compare_func(gconstpointer a, gconstpointer b, gpointer user_data) {
    PlayerctlPlayer *player_a = PLAYERCTL_PLAYER(a);
    PlayerctlPlayer *player_b = PLAYERCTL_PLAYER(b);
    gchar *name_a = NULL;
    gchar *name_b = NULL;
    g_object_get(player_a, "player-name", &name_a, NULL);
    g_object_get(player_b, "player-name", &name_b, NULL);
    gint result = player_name_string_compare_func(name_a, name_b, user_data);
    g_free(name_a);
    g_free(name_b);
    return result;
}

int main(int argc, char *argv[]) {
    g_debug("playerctl version %s", PLAYERCTL_VERSION_S);
    GError *error = NULL;
    guint num_commands = 0;
    GList *available_players = NULL;

    // seems to be required to print unicode (see #8)
    setlocale(LC_CTYPE, "");

    if (!parse_setup_options(argc, argv, &error)) {
        g_printerr("%s\n", error->message);
        g_clear_error(&error);
        exit(0);
    }

    player_names = parse_player_list(player_arg);
    ignored_player_names = parse_player_list(ignore_player_arg);

    if (print_version_and_exit) {
        int result = handle_version_flag();
        exit(result);
    }

    if (list_all_players_and_exit) {
        int result = handle_list_all_flag();
        exit(result);
    }

    num_commands = g_strv_length(command_arg);

    const struct player_command *player_cmd = get_player_command(command_arg, num_commands, &error);
    if (error != NULL) {
        g_printerr("Could not execute command: %s\n", error->message);
        g_clear_error(&error);
        exit(1);
    }

    if (format_string_arg != NULL) {
        formatter = playerctl_formatter_new(format_string_arg, &error);
        if (error != NULL) {
            g_printerr("Could not execute command: %s\n", error->message);
            g_clear_error(&error);
            exit(1);
        }
    }

    playercmd_args = playercmd_args_create(command_arg, num_commands);

    manager = playerctl_player_manager_new(&error);
    if (error != NULL) {
        g_printerr("Could not connect to players: %s\n", error->message);
        exit_status = 1;
        goto end;
    }

    if (player_names != NULL && !select_all_players) {
        playerctl_player_manager_set_sort_func(manager, player_compare_func, (gpointer)player_names,
                                               NULL);
    }

    g_object_get(manager, "player-names", &available_players, NULL);
    available_players = g_list_copy(available_players);
    available_players =
        g_list_sort_with_data(available_players, player_name_compare_func, (gpointer)player_names);

    PlayerctlPlayerName playerctld_name = {
        .instance = "playerctld",
        .source = PLAYERCTL_SOURCE_DBUS_SESSION,
    };
    if (name_is_selected("playerctld") &&
        (g_list_find_custom(player_names, "playerctld", (GCompareFunc)g_strcmp0)) != NULL &&
        (g_list_find_custom(available_players, &playerctld_name,
                            (GCompareFunc)pctl_player_name_compare) == NULL)) {
        // playerctld is not ignored, was specified exactly in the list of
        // players, and is not in the list of available players. Add it to the
        // list and try to autostart it.
        g_debug("%s", "playerctld was selected explicitly, it may autostart");
        available_players = g_list_append(
            available_players, pctl_player_name_new("playerctld", PLAYERCTL_SOURCE_DBUS_SESSION));
        available_players = g_list_sort_with_data(available_players, player_name_compare_func,
                                                  (gpointer)player_names);
    }

    gboolean has_selected = FALSE;
    gboolean did_command = FALSE;
    GList *l = NULL;
    for (l = available_players; l != NULL; l = l->next) {
        PlayerctlPlayerName *name = l->data;
        g_debug("found player: %s", name->instance);
        if (!name_is_selected(name->instance)) {
            continue;
        }
        has_selected = TRUE;

        PlayerctlPlayer *player = playerctl_player_new_from_name(name, &error);
        if (error != NULL) {
            g_printerr("Could not connect to player: %s\n", error->message);
            exit_status = 1;
            goto end;
        }

        if (follow) {
            playerctl_player_manager_manage_player(manager, player);
            init_managed_player(player, player_cmd);
        } else {
            gchar *output = NULL;
            g_debug("executing command %s", player_cmd->name);
            gboolean result = player_cmd->func(player, command_arg, num_commands, &output, &error);
            if (error != NULL) {
                g_printerr("Could not execute command: %s\n", error->message);
                exit_status = 1;
                g_object_unref(player);
                goto end;
            }
            if (result) {
                did_command = TRUE;
                if (output != NULL) {
                    printf("%s", output);
                    fflush(stdout);
                    g_free(output);
                }

                if (!select_all_players) {
                    g_object_unref(player);
                    goto end;
                }
            }
        }

        g_object_unref(player);
    }

    if (!follow) {
        if (!has_selected) {
            if (!no_status_error_messages) {
                g_printerr("No players found\n");
            }
            exit_status = 1;
            goto end;
        } else if (!did_command) {
            if (!no_status_error_messages) {
                g_printerr("No player could handle this command\n");
            }
            exit_status = 1;
            goto end;
        }
    } else {
        managed_players_execute_command(&error);
        if (error != NULL) {
            g_printerr("Connection to player failed: %s\n", error->message);
            exit_status = 1;
            goto end;
        }

        g_signal_connect(PLAYERCTL_PLAYER_MANAGER(manager), "name-appeared",
                         G_CALLBACK(name_appeared_callback), NULL);
        g_signal_connect(PLAYERCTL_PLAYER_MANAGER(manager), "player-appeared",
                         G_CALLBACK(player_appeared_callback), NULL);
        g_signal_connect(PLAYERCTL_PLAYER_MANAGER(manager), "player-vanished",
                         G_CALLBACK(player_vanished_callback), NULL);

        if (formatter != NULL && playerctl_formatter_contains_key(formatter, "position")) {
            g_timeout_add(1000, playercmd_tick_callback, NULL);
        }

        main_loop = g_main_loop_new(NULL, FALSE);
        g_main_loop_run(main_loop);
        g_main_loop_unref(main_loop);
    }

end:
    if (available_players != NULL) {
        g_list_free(available_players);
    }
    playercmd_args_destroy(playercmd_args);
    if (manager != NULL) {
        g_object_unref(manager);
    }
    playerctl_formatter_destroy(formatter);
    g_free(last_output);
    g_list_free_full(player_names, g_free);
    g_list_free_full(ignored_player_names, g_free);

    exit(exit_status);
}
