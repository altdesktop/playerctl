#include <glib.h>
#include <playerctl/playerctl-player.h>
#include "playerctl/playerctl-formatter.h"
#include "playerctl/playerctl-common.h"
#include <inttypes.h>
#include <assert.h>

#define LENGTH(array) (sizeof array / sizeof array[0])

G_DEFINE_QUARK(playerctl-formatter-error-quark, playerctl_formatter_error);

enum token_type {
    TOKEN_VARIABLE,
    TOKEN_STRING,
    TOKEN_FUNCTION,
};

struct token {
    enum token_type type;
    gchar *data;
    struct token *arg;
};

enum parser_state {
    STATE_EXPRESSION = 0,
    STATE_IDENTIFIER,
    STATE_STRING,
};

struct _PlayerctlFormatterPrivate {
    GList *tokens;
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

static void token_list_destroy(GList *tokens) {
    if (tokens == NULL) {
        return;
    }

    g_list_free_full(tokens, (GDestroyNotify)token_destroy);
}

static gboolean token_list_contains_key(GList *tokens, const gchar *key) {
    GList *t = NULL;
    for (t = tokens; t != NULL; t = t->next) {
        struct token *token = t->data;
        switch (token->type) {
        case TOKEN_VARIABLE:
            if (g_strcmp0(token->data, key) == 0) {
                return TRUE;
            }
            break;
        case TOKEN_FUNCTION:
            if (token->arg != NULL && token->arg->type == TOKEN_VARIABLE &&
                    g_strcmp0(token->arg->data, key) == 0) {
                return TRUE;
            }
            break;
        default:
            break;
        }
    }
    return FALSE;
}

static gboolean is_identifier_char(gchar c) {
    return g_ascii_isalnum(c) || c == '_' || c == ':' || c == '-';
}

static struct token *tokenize_expression(const gchar *format, gint pos, gint *end, GError **error) {
    GError *tmp_error = NULL;
    int len = strlen(format);
    char buf[1028];
    int buf_len = 0;

    enum parser_state state = STATE_EXPRESSION;

    for (int i = pos; i < len; ++i) {
        switch (state) {
        case STATE_EXPRESSION:
            if (format[i] == ' ') {
                continue;
            } else if (format[i] == '"') {
                state = STATE_STRING;
                continue;
            } else if (!is_identifier_char(format[i])) {
                // TODO return NULL to indicate there is no expression
                g_set_error(error, playerctl_formatter_error_quark(), 1,
                            "unexpected \"%c\", expected expression (position  %d)", format[i], i);
                return NULL;
            } else {
                state = STATE_IDENTIFIER;
                buf[buf_len++] = format[i];
                continue;
            }
            break;

        case STATE_STRING:
            if (format[i] == '"') {
                struct token *ret = token_create(TOKEN_STRING);
                buf[buf_len] = '\0';
                ret->data = g_strdup(buf);

                i++;
                while (i < len && format[i] == ' ') {
                    i++;
                }
                *end = i;
                //printf("string: '%s'\n", ret->data);
                return ret;
            } else {
                buf[buf_len++] = format[i];
            }
            break;

        case STATE_IDENTIFIER:
            if (format[i] == '(') {
                struct token *ret = token_create(TOKEN_FUNCTION);
                buf[buf_len] = '\0';
                ret->data = g_strdup(buf);
                i += 1;
                //printf("function: '%s'\n", ret->data);
                ret->arg = tokenize_expression(format, i, end, &tmp_error);

                while (*end < len && format[*end] == ' ') {
                    *end += 1;
                }

                if (tmp_error != NULL) {
                    token_destroy(ret);
                    g_propagate_error(error, tmp_error);
                    return NULL;
                }

                if (format[*end] != ')') {
                    g_set_error(error, playerctl_formatter_error_quark(), 1,
                                "expecting \")\" (position %d)", *end);
                }

                *end += 1;

                return ret;
            } else if (!is_identifier_char(format[i])) {
                struct token *ret = token_create(TOKEN_VARIABLE);
                buf[buf_len] = '\0';
                ret->data = g_strdup(buf);
                while (i < len && format[i] == ' ') {
                    i++;
                }
                *end = i;
                //printf("variable: '%s' end='%c'\n", ret->data, format[*end]);
                return ret;
            } else {
                buf[buf_len] = format[i];
                ++buf_len;
            }
            break;
        }
    }


    g_set_error(error, playerctl_formatter_error_quark(), 1,
                "unexpected end of format string");
    return NULL;
}

static GList *tokenize_format(const char *format, GError **error) {
    GError *tmp_error = NULL;
    GList *tokens = NULL;

    if (format == NULL) {
        return NULL;
    }

    int len = strlen(format);
    char buf[1028];
    int buf_len = 0;

    if (len >= 1028) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "the maximum format string length is 1028");
        return NULL;
    }

    for (int i = 0; i < len; ++i) {
        if (format[i] == '{' && i < len + 1 && format[i+1] == '{') {
            if (buf_len > 0) {
                buf[buf_len] = '\0';
                buf_len = 0;
                struct token *token = token_create(TOKEN_STRING);
                token->data = g_strdup(buf);
                //printf("passthrough: '%s'\n", token->data);
                tokens = g_list_append(tokens, token);
            }

            i += 2;
            int end = 0;
            struct token *token = tokenize_expression(format, i, &end, &tmp_error);
            if (tmp_error != NULL) {
                token_list_destroy(tokens);
                g_propagate_error(error, tmp_error);
                return NULL;
            }
            tokens = g_list_append(tokens, token);
            i = end;

            while (i < len && format[i] == ' ') {
                i++;
            }

            if (i >= len || format[i] != '}' || format[i+1] != '}') {
                token_list_destroy(tokens);
                g_set_error(error, playerctl_formatter_error_quark(), 1,
                            "expecting \"}}\" (position %d)", i);
                return NULL;
            }
            i += 1;

        } else if (format[i] == '}' && i < len + 1 && format[i+1] == '}') {
            assert(FALSE && "TODO");
        } else {
            buf[buf_len++] = format[i];
        }
    }

    if (buf_len > 0) {
        buf[buf_len] = '\0';
        struct token *token = token_create(TOKEN_STRING);
        token->data = g_strdup(buf);
        tokens = g_list_append(tokens, token);
    }

    return tokens;
}

static gchar *helperfn_lc(gchar *key, GVariant *value) {
    gchar *printed = pctl_print_gvariant(value);
    gchar *printed_lc = g_utf8_strdown(printed, -1);
    g_free(printed);
    return printed_lc;
}

static gchar *helperfn_uc(gchar *key, GVariant *value) {
    if (value == NULL) {
        return g_strdup("");
    }

    gchar *printed = pctl_print_gvariant(value);
    gchar *printed_uc = g_utf8_strup(printed, -1);
    g_free(printed);
    return printed_uc;
}

static gchar *helperfn_duration(gchar *key, GVariant *value) {
    if (value == NULL) {
        return g_strdup("");
    }

    // mpris durations are represented as int64 in microseconds
    if (!g_variant_type_equal(g_variant_get_type(value), G_VARIANT_TYPE_INT64)) {
        return NULL;
    }

    gint64 duration = g_variant_get_int64(value);
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

/* Calls g_markup_escape_text to replace the text with appropriately escaped
characters for XML */
static gchar *helperfn_markup_escape(gchar *key, GVariant *value) {
    if (value == NULL) {
        return g_strdup("");
    }

    gchar *printed = pctl_print_gvariant(value);
    gchar *escaped = g_markup_escape_text(printed, -1);
    g_free(printed);
    return escaped;
}

static gchar *helperfn_emoji(gchar *key, GVariant *value) {
    g_warning("The emoji() helper function is undocumented and experimental and will change in a future release.");
    if (value == NULL) {
        return g_strdup("");
    }

    if (g_strcmp0(key, "status") == 0 &&
            g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
        const gchar *status_str = g_variant_get_string(value, NULL);
        PlayerctlPlaybackStatus status = 0;
        if (pctl_parse_playback_status(status_str, &status)) {
            switch (status) {
            case PLAYERCTL_PLAYBACK_STATUS_PLAYING:
                return g_strdup("▶️");
            case PLAYERCTL_PLAYBACK_STATUS_STOPPED:
                return g_strdup("⏹️");
            case PLAYERCTL_PLAYBACK_STATUS_PAUSED:
                return g_strdup("⏸️");
            }
        }
    } else if (g_strcmp0(key, "volume") == 0 &&
            g_variant_is_of_type(value, G_VARIANT_TYPE_DOUBLE)) {
        const gdouble volume = g_variant_get_double(value);
        if (volume < 0.3333) {
            return g_strdup("🔈");
        } else if (volume < 0.6666) {
            return g_strdup("🔉");
        } else {
            return g_strdup("🔊");
        }
    }

    return pctl_print_gvariant(value);
}

struct template_helper {
    const gchar *name;
    gchar *(*func)(gchar *key, GVariant *value);
} helpers[] = {
    {"lc", &helperfn_lc},
    {"uc", &helperfn_uc},
    {"duration", &helperfn_duration},
    {"markup_escape", &helperfn_markup_escape},
    // EXPERIMENTAL
    {"emoji", &helperfn_emoji},
};

static GVariant *expand_token(struct token *token, GVariantDict *context, GError **error) {
    GError *tmp_error = NULL;

    switch (token->type) {
    case TOKEN_STRING:
        return g_variant_new("s", token->data);

    case TOKEN_VARIABLE:
        if (g_variant_dict_contains(context, token->data)) {
            return g_variant_dict_lookup_value(context, token->data, NULL);
        } else {
            return NULL;
        }

    case TOKEN_FUNCTION:
        {
            assert(token->arg != NULL);
            gchar *arg_name = NULL;
            if (token->type == TOKEN_VARIABLE) {
                arg_name = token->data;
            }

            GVariant *value = expand_token(token->arg, context, &tmp_error);

            if (tmp_error != NULL) {
                g_propagate_error(error, tmp_error);
                return NULL;
            }

            for (gsize i = 0; i < LENGTH(helpers); ++i) {
                if (g_strcmp0(helpers[i].name, token->data) == 0) {
                    gchar *result = helpers[i].func(arg_name, value);
                    if (value != NULL) {
                        g_variant_unref(value);
                    }
                    return g_variant_new("s", result);
                }
            }

            if (value != NULL) {
                g_variant_unref(value);
            }
            g_set_error(error, playerctl_formatter_error_quark(), 1,
                        "unknown template function: %s", token->data);
            return NULL;
        }
    }

    assert(FALSE && "not reached");
    return NULL;
}

static gchar *expand_format(GList *tokens, GVariantDict *context, GError **error) {
    GError *tmp_error = NULL;
    GString *expanded;

    expanded = g_string_new("");
    GList *t = tokens;
    for (t = tokens; t != NULL; t = t->next) {
        GVariant *value = expand_token(t->data, context, &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return NULL;
        }

        if (value != NULL) {
            gchar *result = pctl_print_gvariant(value);
            expanded = g_string_append(expanded, result);
            g_free(result);
            g_variant_unref(value);
        }
    }
    return g_string_free(expanded, FALSE);
}

static GVariantDict *get_default_template_context(PlayerctlPlayer *player, GVariant *base) {
    GVariantDict *context = g_variant_dict_new(base);
    if (!g_variant_dict_contains(context, "artist") &&
            g_variant_dict_contains(context, "xesam:artist")) {
        GVariant *artist = g_variant_dict_lookup_value(context, "xesam:artist", NULL);
        g_variant_dict_insert_value(context, "artist", artist);
        g_variant_unref(artist);
    }
    if (!g_variant_dict_contains(context, "album") &&
            g_variant_dict_contains(context, "xesam:album")) {
        GVariant *album = g_variant_dict_lookup_value(context, "xesam:album", NULL);
        g_variant_dict_insert_value(context, "album", album);
        g_variant_unref(album);
    }
    if (!g_variant_dict_contains(context, "title") &&
            g_variant_dict_contains(context, "xesam:title")) {
        GVariant *title = g_variant_dict_lookup_value(context, "xesam:title", NULL);
        g_variant_dict_insert_value(context, "title", title);
        g_variant_unref(title);
    }
    if (!g_variant_dict_contains(context, "playerName")) {
        gchar *player_name = NULL;
        g_object_get(player, "player-name", &player_name, NULL);
        GVariant *player_name_variant = g_variant_new_string(player_name);
        g_variant_dict_insert_value(context, "playerName", player_name_variant);
        g_free(player_name);
    }
    if (!g_variant_dict_contains(context, "playerInstance")) {
        gchar *instance = NULL;
        g_object_get(player, "player-instance", &instance, NULL);
        GVariant *player_instance_variant = g_variant_new_string(instance);
        g_variant_dict_insert_value(context, "playerInstance", player_instance_variant);
        g_free(instance);
    }
    if (!g_variant_dict_contains(context, "shuffle")) {
        gboolean shuffle = FALSE;
        g_object_get(player, "shuffle", &shuffle, NULL);
        GVariant *shuffle_variant = g_variant_new_boolean(shuffle);
        g_variant_dict_insert_value(context, "shuffle", shuffle_variant);
    }
    if (!g_variant_dict_contains(context, "status")) {
        PlayerctlPlaybackStatus status = 0;
        g_object_get(player, "playback-status", &status, NULL);
        const gchar *status_str = pctl_playback_status_to_string(status);
        GVariant *status_variant = g_variant_new_string(status_str);
        g_variant_dict_insert_value(context, "status", status_variant);
    }
    if (!g_variant_dict_contains(context, "loop")) {
        PlayerctlLoopStatus status = 0;
        g_object_get(player, "loop-status", &status, NULL);
        const gchar *status_str = pctl_loop_status_to_string(status);
        GVariant *status_variant = g_variant_new_string(status_str);
        g_variant_dict_insert_value(context, "loop", status_variant);
    }
    if (!g_variant_dict_contains(context, "volume")) {
        gdouble level = 0.0;
        g_object_get(player, "volume", &level, NULL);
        GVariant *volume_variant = g_variant_new_double(level);
        g_variant_dict_insert_value(context, "volume", volume_variant);
    }
    if (!g_variant_dict_contains(context, "position")) {
        gint64 position = 0;
        g_object_get(player, "position", &position, NULL);
        GVariant *position_variant = g_variant_new_int64(position);
        g_variant_dict_insert_value(context, "position", position_variant);
    }

    return context;
}

PlayerctlFormatter *playerctl_formatter_new(const gchar *format, GError **error) {
    GError *tmp_error = NULL;
    GList *tokens = tokenize_format(format, &tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(error, tmp_error);
        return NULL;
    }

    PlayerctlFormatter *formatter = calloc(1, sizeof(PlayerctlFormatter));
    formatter->priv = calloc(1, sizeof(PlayerctlFormatterPrivate));
    formatter->priv->tokens = tokens;

    return formatter;
}

void playerctl_formatter_destroy(PlayerctlFormatter *formatter) {
    if (formatter == NULL) {
        return;
    }

    token_list_destroy(formatter->priv->tokens);
    free(formatter->priv);
    free(formatter);
}

gboolean playerctl_formatter_contains_key(PlayerctlFormatter *formatter, const gchar *key) {
    return token_list_contains_key(formatter->priv->tokens, key);
}

GVariantDict *playerctl_formatter_default_template_context(
        PlayerctlFormatter *formatter, PlayerctlPlayer *player, GVariant *base) {
    return get_default_template_context(player, base);
}

gchar *playerctl_formatter_expand_format(PlayerctlFormatter *formatter, GVariantDict *context, GError **error) {
    GError *tmp_error = NULL;
    gchar *expanded = expand_format(formatter->priv->tokens, context, &tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(error, tmp_error);
        return NULL;
    }

    return expanded;
}
