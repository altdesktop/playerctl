#include "playerctl/playerctl-formatter.h"

#include <assert.h>
#include <glib.h>
#include <inttypes.h>
#include <playerctl/playerctl-player.h>
#include <stdio.h>

#include "playerctl/playerctl-common.h"

#define LENGTH(array) (sizeof array / sizeof array[0])

#define MAX_ARGS 32

#define INFIX_ADD "+"
#define INFIX_SUB "-"
#define INFIX_MUL "*"
#define INFIX_DIV "/"

// clang-format off
G_DEFINE_QUARK(playerctl-formatter-error-quark, playerctl_formatter_error);
// clang-format on

enum token_type {
    TOKEN_VARIABLE,
    TOKEN_STRING,
    TOKEN_FUNCTION,
    TOKEN_NUMBER,
};

struct token {
    enum token_type type;
    gchar *data;
    gdouble numeric_data;
    GList *args;
};

enum parser_state {
    STATE_EXPRESSION = 0,
    STATE_IDENTIFIER,
    STATE_STRING,
    STATE_NUMBER,
};

enum parse_level {
    PARSE_FULL = 0,
    PARSE_NEXT_IDENT,
    PARSE_MULT_DIV,
};

struct _PlayerctlFormatterPrivate {
    GList *tokens;
};

static struct token *token_create(enum token_type type) {
    struct token *token = calloc(1, sizeof(struct token));
    token->type = type;
    return token;
}

static void token_list_destroy(GList *tokens);

static void token_destroy(struct token *token) {
    if (token == NULL) {
        return;
    }

    token_list_destroy(token->args);
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
    if (tokens == NULL) {
        return FALSE;
    }

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
            if (token_list_contains_key(token->args, key)) {
                return TRUE;
            }
        default:
            break;
        }
    }
    return FALSE;
}

static gboolean is_identifier_start_char(gchar c) {
    return g_ascii_isalpha(c) || c == '_';
}

static gboolean is_identifier_char(gchar c) {
    return g_ascii_isalnum(c) || c == '_' || c == ':';
}

static gboolean is_numeric_char(gchar c) {
    return g_ascii_isdigit(c) || c == '.';
}

static gchar *infix_to_identifier(gchar infix) {
    switch (infix) {
    case '+':
        return g_strdup(INFIX_ADD);
    case '-':
        return g_strdup(INFIX_SUB);
    case '*':
        return g_strdup(INFIX_MUL);
    case '/':
        return g_strdup(INFIX_DIV);
    default:
        assert(false && "not reached");
    }
}

static struct token *tokenize_expression(const gchar *format, gint pos, gint *end,
                                         enum parse_level level, GError **error) {
    GError *tmp_error = NULL;
    int len = strlen(format);
    char buf[1028];
    int buf_len = 0;
    struct token *tok = NULL;

    enum parser_state state = STATE_EXPRESSION;

    if (pos > len - 1) {
        g_set_error(error, playerctl_formatter_error_quark(), 1, "unexpected end of expression");
        return NULL;
    }

    for (int i = pos; i < len; ++i) {
        switch (state) {
        case STATE_EXPRESSION:
            if (format[i] == ' ') {
                continue;
            } else if (format[i] == '(') {
                // ordering parens
                tok = tokenize_expression(format, i + 1, end, PARSE_FULL, &tmp_error);
                if (tmp_error != NULL) {
                    g_propagate_error(error, tmp_error);
                    return NULL;
                }

                if (*end > len - 1 || format[*end] != ')') {
                    g_set_error(error, playerctl_formatter_error_quark(), 1,
                                "expected \")\" (position  %d)", *end);
                    token_destroy(tok);
                    return NULL;
                }
                *end += 1;

                goto loop_out;
            } else if (format[i] == '+' || format[i] == '-') {
                // unary + or -
                struct token *operand =
                    tokenize_expression(format, i + 1, end, PARSE_NEXT_IDENT, &tmp_error);
                if (tmp_error != NULL) {
                    g_propagate_error(error, tmp_error);
                    return NULL;
                }
                tok = token_create(TOKEN_FUNCTION);
                tok->data = infix_to_identifier(format[i]);
                tok->args = g_list_append(tok->args, operand);
                goto loop_out;
            } else if (format[i] == '"') {
                state = STATE_STRING;
                continue;
            } else if (is_numeric_char(format[i])) {
                state = STATE_NUMBER;
                buf[buf_len++] = format[i];
                continue;
            } else if (!is_identifier_start_char(format[i])) {
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
                tok = token_create(TOKEN_STRING);
                buf[buf_len] = '\0';
                tok->data = g_strdup(buf);

                i++;
                while (i < len && format[i] == ' ') {
                    i++;
                }
                *end = i;
                // printf("string: '%s'\n", tok->data);
                goto loop_out;
            } else {
                buf[buf_len++] = format[i];
            }
            break;

        case STATE_NUMBER:
            if (!is_numeric_char(format[i]) || i == len - 2) {
                tok = token_create(TOKEN_NUMBER);
                buf[buf_len] = '\0';
                tok->data = g_strdup(buf);
                char *endptr = NULL;
                gdouble number = strtod(tok->data, &endptr);
                if (endptr == NULL || *endptr != '\0') {
                    g_set_error(error, playerctl_formatter_error_quark(), 1,
                                "invalid number: \"%s\" (position %d)", tok->data, i);
                    token_destroy(tok);
                    return NULL;
                }
                tok->numeric_data = number;
                while (i < len && format[i] == ' ') {
                    i++;
                }
                *end = i;
                // printf("number: '%f'\n", tok->numeric_data);
                goto loop_out;
            } else {
                buf[buf_len++] = format[i];
            }
            break;

        case STATE_IDENTIFIER:
            if (format[i] == '(') {
                tok = token_create(TOKEN_FUNCTION);
                buf[buf_len] = '\0';
                tok->data = g_strdup(buf);
                i += 1;
                // printf("function: '%s'\n", tok->data);

                int nargs = 0;
                while (TRUE) {
                    tok->args = g_list_append(
                        tok->args, tokenize_expression(format, i, end, PARSE_FULL, &tmp_error));

                    nargs++;

                    if (nargs > MAX_ARGS) {
                        g_set_error(error, playerctl_formatter_error_quark(), 1,
                                    "maximum args of %d exceeded", MAX_ARGS);
                        token_destroy(tok);
                        return NULL;
                    }

                    if (tmp_error != NULL) {
                        token_destroy(tok);
                        g_propagate_error(error, tmp_error);
                        return NULL;
                    }

                    while (*end < len && format[*end] == ' ') {
                        *end += 1;
                    }

                    if (format[*end] == ')') {
                        *end += 1;
                        break;
                    } else if (format[*end] == ',') {
                        i = *end + 1;
                        continue;
                    } else {
                        g_set_error(error, playerctl_formatter_error_quark(), 1,
                                    "expecting \")\" (position %d)", *end);
                        token_destroy(tok);
                        return NULL;
                    }
                }
                goto loop_out;
            } else if (!is_identifier_char(format[i])) {
                tok = token_create(TOKEN_VARIABLE);
                buf[buf_len] = '\0';
                tok->data = g_strdup(buf);
                while (i < len && format[i] == ' ') {
                    i++;
                }
                *end = i;
                // printf("variable: '%s' end='%c'\n", tok->data, format[*end]);
                goto loop_out;
            } else {
                buf[buf_len] = format[i];
                ++buf_len;
            }
            break;
        }
    }

loop_out:

    if (tok == NULL) {
        g_set_error(error, playerctl_formatter_error_quark(), 1, "unexpected end of expression");
        return NULL;
    }
    while (*end < len && format[*end] == ' ') {
        *end += 1;
    }
    if (level == PARSE_NEXT_IDENT || *end >= len - 1) {
        return tok;
    }

    gchar infix_id = format[*end];
    while (infix_id == '*' || infix_id == '/' || infix_id == '+' || infix_id == '-') {
        while (infix_id == '*' || infix_id == '/') {
            struct token *operand =
                tokenize_expression(format, *end + 1, end, PARSE_NEXT_IDENT, &tmp_error);
            if (tmp_error != NULL) {
                token_destroy(tok);
                g_propagate_error(error, tmp_error);
                return NULL;
            }

            struct token *operation = token_create(TOKEN_FUNCTION);
            operation->data = infix_to_identifier(infix_id);
            operation->args = g_list_append(operation->args, tok);
            operation->args = g_list_append(operation->args, operand);

            tok = operation;
            infix_id = format[*end];
        }

        if (level == PARSE_MULT_DIV) {
            return tok;
        }

        if (infix_id == '+' || infix_id == '-') {
            struct token *operand =
                tokenize_expression(format, *end + 1, end, PARSE_MULT_DIV, &tmp_error);
            if (tmp_error != NULL) {
                token_destroy(tok);
                g_propagate_error(error, tmp_error);
                return NULL;
            }

            struct token *operation = token_create(TOKEN_FUNCTION);
            operation->data = infix_to_identifier(infix_id);
            operation->args = g_list_append(operation->args, tok);
            operation->args = g_list_append(operation->args, operand);

            tok = operation;
            infix_id = format[*end];
        }
    }

    return tok;
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
        if (format[i] == '{' && i < len + 1 && format[i + 1] == '{') {
            if (buf_len > 0) {
                buf[buf_len] = '\0';
                buf_len = 0;
                struct token *token = token_create(TOKEN_STRING);
                token->data = g_strdup(buf);
                // printf("passthrough: '%s'\n", token->data);
                tokens = g_list_append(tokens, token);
            }

            i += 2;
            int end = 0;
            struct token *token = tokenize_expression(format, i, &end, PARSE_FULL, &tmp_error);
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

            if (i >= len || format[i] != '}' || format[i + 1] != '}') {
                token_list_destroy(tokens);
                g_set_error(error, playerctl_formatter_error_quark(), 1,
                            "expecting \"}}\" (position %d)", i);
                return NULL;
            }
            i += 1;

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

static GVariant *helperfn_lc(struct token *token, GVariant **args, int nargs, GError **error) {
    if (nargs != 1) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "function lc takes exactly one argument (got %d)", nargs);
        return NULL;
    }

    GVariant *value = args[0];
    if (value == NULL) {
        return g_variant_new("s", "");
    }

    gchar *printed = pctl_print_gvariant(value);
    gchar *printed_lc = g_utf8_strdown(printed, -1);
    GVariant *ret = g_variant_new("s", printed_lc);
    g_free(printed);
    g_free(printed_lc);
    return ret;
}

static GVariant *helperfn_uc(struct token *token, GVariant **args, int nargs, GError **error) {
    if (nargs != 1) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "function uc takes exactly one argument (got %d)", nargs);
        return NULL;
    }

    GVariant *value = args[0];
    if (value == NULL) {
        return g_variant_new("s", "");
    }

    gchar *printed = pctl_print_gvariant(value);
    gchar *printed_uc = g_utf8_strup(printed, -1);
    GVariant *ret = g_variant_new("s", printed_uc);
    g_free(printed);
    g_free(printed_uc);
    return ret;
}

static GVariant *helperfn_duration(struct token *token, GVariant **args, int nargs,
                                   GError **error) {
    if (nargs != 1) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "function uc takes exactly one argument (got %d)", nargs);
        return NULL;
    }

    GVariant *value = args[0];
    if (value == NULL) {
        return g_variant_new("s", "");
    }

    gint64 duration;

    if (g_variant_type_equal(g_variant_get_type(value), G_VARIANT_TYPE_INT64)) {
        // mpris specifies all track position values to be int64
        duration = g_variant_get_int64(value);
    } else if (g_variant_type_equal(g_variant_get_type(value), G_VARIANT_TYPE_UINT64)) {
        // XXX: spotify may give uint64
        duration = g_variant_get_uint64(value);
    } else if (g_variant_type_equal(g_variant_get_type(value), G_VARIANT_TYPE_DOUBLE)) {
        // only if supplied by a constant or position value type goes against spec
        duration = g_variant_get_double(value);
    } else {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "function duration can only be called on track position values");
        return NULL;
    }

    gint64 seconds = (duration / 1000000) % 60;
    gint64 minutes = (duration / 1000000 / 60) % 60;
    gint64 hours = (duration / 1000000 / 60 / 60);

    GString *formatted = g_string_new("");

    if (hours != 0) {
        g_string_append_printf(formatted, "%" PRId64 ":%02" PRId64 ":%02" PRId64, hours, minutes,
                               seconds);
    } else {
        g_string_append_printf(formatted, "%" PRId64 ":%02" PRId64, minutes, seconds);
    }

    gchar *formatted_inner = g_string_free(formatted, FALSE);
    GVariant *ret = g_variant_new("s", formatted_inner);
    g_free(formatted_inner);

    return ret;
}

/* Calls g_markup_escape_text to replace the text with appropriately escaped
characters for XML */
static GVariant *helperfn_markup_escape(struct token *token, GVariant **args, int nargs,
                                        GError **error) {
    if (nargs != 1) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "function markup_escape takes exactly one argument (got %d)", nargs);
        return NULL;
    }

    GVariant *value = args[0];
    if (value == NULL) {
        return g_variant_new("s", "");
    }

    gchar *printed = pctl_print_gvariant(value);
    gchar *escaped = g_markup_escape_text(printed, -1);
    GVariant *ret = g_variant_new("s", escaped);
    g_free(escaped);
    g_free(printed);
    return ret;
}

static GVariant *helperfn_default(struct token *token, GVariant **args, int nargs, GError **error) {
    if (nargs != 2) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "function default takes exactly two arguments (got %d)", nargs);
        return NULL;
    }

    if (args[0] == NULL && args[1] == NULL) {
        return NULL;
    }

    if (args[0] == NULL) {
        g_variant_ref(args[1]);
        return args[1];
    } else {
        if (g_variant_is_of_type(args[0], G_VARIANT_TYPE_STRING_ARRAY) &&
            strlen(pctl_print_gvariant(args[0])) == 0) {
            g_variant_ref(args[1]);
            return args[1];
        }
        g_variant_ref(args[0]);
        return args[0];
    }
}

static GVariant *helperfn_emoji(struct token *token, GVariant **args, int nargs, GError **error) {
    if (nargs != 1) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "function emoji takes exactly one argument (got %d)", nargs);
        return NULL;
    }

    GVariant *value = args[0];
    if (value == NULL) {
        return g_variant_new("s", "");
    }

    struct token *arg_token = g_list_first(token->args)->data;

    if (arg_token->type != TOKEN_VARIABLE) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "the emoji function can only be called with a variable");
        return NULL;
    }

    gchar *key = arg_token->data;

    if (g_strcmp0(key, "status") == 0 && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
        const gchar *status_str = g_variant_get_string(value, NULL);
        PlayerctlPlaybackStatus status = 0;
        if (pctl_parse_playback_status(status_str, &status)) {
            switch (status) {
            case PLAYERCTL_PLAYBACK_STATUS_PLAYING:
                return g_variant_new("s", "▶️");
            case PLAYERCTL_PLAYBACK_STATUS_STOPPED:
                return g_variant_new("s", "⏹️");
            case PLAYERCTL_PLAYBACK_STATUS_PAUSED:
                return g_variant_new("s", "⏸️");
            }
        }
    } else if (g_strcmp0(key, "volume") == 0 &&
               g_variant_is_of_type(value, G_VARIANT_TYPE_DOUBLE)) {
        const gdouble volume = g_variant_get_double(value);
        if (volume < 0.3333) {
            return g_variant_new("s", "🔈");
        } else if (volume < 0.6666) {
            return g_variant_new("s", "🔉");
        } else {
            return g_variant_new("s", "🔊");
        }
    }

    g_variant_ref(value);
    return value;
}

static GVariant *helperfn_trunc(struct token *token, GVariant **args, int nargs, GError **error) {
    if (nargs != 2) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "function trunc takes exactly two arguments (got %d)", nargs);
        return NULL;
    }

    GVariant *value = args[0];
    GVariant *len = args[1];
    if (value == NULL || len == NULL) {
        return g_variant_new("s", "");
    }

    if (!g_variant_type_equal(g_variant_get_type(len), G_VARIANT_TYPE_DOUBLE)) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "function trunc's length parameter can only be called with an int");
        return NULL;
    }

    gchar *orig = pctl_print_gvariant(value);
    gchar *trunc = g_utf8_substring(orig, 0, g_variant_get_double(len));

    GString *formatted = g_string_new(trunc);
    if (g_utf8_strlen(trunc, 256) < g_utf8_strlen(orig, 256)) {
        g_string_append(formatted, "…");
    }

    gchar *formatted_inner = g_string_free(formatted, FALSE);
    GVariant *ret = g_variant_new("s", formatted_inner);
    g_free(formatted_inner);
    g_free(trunc);
    g_free(orig);

    return ret;
}

static gboolean is_valid_numeric_type(GVariant *value) {
    // This is all the types we know about for numeric operations. May be
    // expanded at a later time. MPRIS only uses INT64 and DOUBLE as numeric
    // types. Formatter constants are always DOUBLE. All other types are for
    // player workarounds.
    if (value == NULL) {
        return FALSE;
    }

    if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64)) {
        return TRUE;
    } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT64)) {
        return TRUE;
    } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_DOUBLE)) {
        return TRUE;
    }

    return FALSE;
}

static gdouble get_double_value(GVariant *value) {
    // Keep this in sync with above is_value_numeric_type()

    if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64)) {
        return (gdouble)g_variant_get_int64(value);
    } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT64)) {
        return (gdouble)g_variant_get_uint64(value);
    } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_DOUBLE)) {
        return g_variant_get_double(value);
    } else {
        assert(FALSE && "not reached");
    }
    return 0.0;
}

static GVariant *infixfn_add(struct token *token, GVariant **args, int nargs, GError **error) {
    if (nargs == 1) {
        // unary addition
        if (!is_valid_numeric_type(args[0])) {
            g_set_error(error, playerctl_formatter_error_quark(), 1,
                        "Got unsupported operand type for unary +: '%s'",
                        g_variant_get_type_string(args[0]));
            return NULL;
        }
        g_variant_ref(args[0]);
        return args[0];
    }

    if (nargs != 2) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "Addition takes two arguments (got %d). This is a bug in Playerctl.", nargs);
        return NULL;
    }

    if (args[0] == NULL || args[1] == NULL) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "Got unsupported operand type for +: NULL");
        return NULL;
    }

    if (!is_valid_numeric_type(args[0]) || !is_valid_numeric_type(args[1])) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "Got unsupported operand types for +: '%s' and '%s'",
                    g_variant_get_type_string(args[0]), g_variant_get_type_string(args[1]));
        return NULL;
    }

    if (g_variant_is_of_type(args[0], G_VARIANT_TYPE_INT64) &&
        g_variant_is_of_type(args[1], G_VARIANT_TYPE_INT64)) {
        gint64 val0 = g_variant_get_int64(args[0]);
        gint64 val1 = g_variant_get_int64(args[1]);
        gint64 result = val0 + val1;

        if ((val0 > 0 && val1 > 0 && result < 0) || (val0 < 0 && val1 < 0 && result > 0)) {
            g_set_error(error, playerctl_formatter_error_quark(), 1, "Numeric overflow detected");
            return NULL;
        }

        return g_variant_new("x", result);
    }

    gdouble val0 = get_double_value(args[0]);
    gdouble val1 = get_double_value(args[1]);
    gdouble result = val0 + val1;

    return g_variant_new("d", result);
}

static GVariant *infixfn_sub(struct token *token, GVariant **args, int nargs, GError **error) {
    if (nargs == 1) {
        // unary addition
        if (g_variant_is_of_type(args[0], G_VARIANT_TYPE_INT64)) {
            gint64 value = g_variant_get_int64(args[0]);
            return g_variant_new("x", value * -1);
        } else if (g_variant_is_of_type(args[0], G_VARIANT_TYPE_DOUBLE)) {
            gdouble value = g_variant_get_double(args[0]);
            return g_variant_new("d", value * -1);
        } else {
            g_set_error(error, playerctl_formatter_error_quark(), 1,
                        "Got unsupported operand type for unary -: '%s'",
                        g_variant_get_type_string(args[0]));
            return NULL;
        }
    }

    if (nargs != 2) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "Subtraction takes two arguments (got %d). This is a bug in Playerctl.", nargs);
        return NULL;
    }

    if (args[0] == NULL || args[1] == NULL) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "Got unsupported operand type for -: NULL");
        return NULL;
    }

    if (!is_valid_numeric_type(args[0]) || !is_valid_numeric_type(args[1])) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "Got unsupported operand types for -: '%s' and '%s'",
                    g_variant_get_type_string(args[0]), g_variant_get_type_string(args[1]));
        return NULL;
    }

    if (g_variant_is_of_type(args[0], G_VARIANT_TYPE_INT64) &&
        g_variant_is_of_type(args[1], G_VARIANT_TYPE_INT64)) {
        gint64 val0 = g_variant_get_int64(args[0]);
        gint64 val1 = g_variant_get_int64(args[1]);
        gint64 result = val0 - val1;

        if ((val0 > 0 && val1 < 0 && result < 0) || (val0 < 0 && val1 > 0 && result > 0)) {
            g_set_error(error, playerctl_formatter_error_quark(), 1, "Numeric overflow detected");
            return NULL;
        }

        return g_variant_new("x", result);
    }

    gdouble val0 = get_double_value(args[0]);
    gdouble val1 = get_double_value(args[1]);
    gdouble result = val0 - val1;

    return g_variant_new("d", result);
}

static GVariant *infixfn_mul(struct token *token, GVariant **args, int nargs, GError **error) {
    if (nargs != 2) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "Multiplication takes two arguments (got %d). This is a bug in Playerctl.",
                    nargs);
        return NULL;
    }
    if (!is_valid_numeric_type(args[0]) || !is_valid_numeric_type(args[1])) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "Got unsupported operand types for *: '%s' and '%s'",
                    g_variant_get_type_string(args[0]), g_variant_get_type_string(args[1]));
        return NULL;
    }
    if (g_variant_is_of_type(args[0], G_VARIANT_TYPE_INT64) &&
        g_variant_is_of_type(args[1], G_VARIANT_TYPE_INT64)) {
        gint64 val0 = g_variant_get_int64(args[0]);
        gint64 val1 = g_variant_get_int64(args[1]);
        gint64 result = val0 * val1;

        if (val0 != 0 && val1 / val0 != val1) {
            g_set_error(error, playerctl_formatter_error_quark(), 1, "Numeric overflow detected");
            return NULL;
        }

        return g_variant_new("x", result);
    }

    gdouble val0 = get_double_value(args[0]);
    gdouble val1 = get_double_value(args[1]);
    gdouble result = val0 * val1;

    return g_variant_new("d", result);
}

static GVariant *infixfn_div(struct token *token, GVariant **args, int nargs, GError **error) {
    if (nargs != 2) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "Division takes two arguments (got %d). This is a bug in Playerctl.", nargs);
        return NULL;
    }
    if (!is_valid_numeric_type(args[0]) || !is_valid_numeric_type(args[1])) {
        g_set_error(error, playerctl_formatter_error_quark(), 1,
                    "Got unsupported operand types for /: '%s' and '%s'",
                    g_variant_get_type_string(args[0]), g_variant_get_type_string(args[1]));
        return NULL;
    }

    if (g_variant_is_of_type(args[0], G_VARIANT_TYPE_INT64) &&
        g_variant_is_of_type(args[1], G_VARIANT_TYPE_INT64)) {
        gint64 val0 = g_variant_get_int64(args[0]);
        gint64 val1 = g_variant_get_int64(args[1]);

        if (val1 == 0) {
            g_set_error(error, playerctl_formatter_error_quark(), 1, "Divide by zero error");
            return NULL;
        }

        gint64 result = val0 / val1;

        return g_variant_new("x", result);
    }

    gdouble val0 = get_double_value(args[0]);
    gdouble val1 = get_double_value(args[1]);

    if (val1 == 0.0) {
        g_set_error(error, playerctl_formatter_error_quark(), 1, "Divide by zero error");
        return NULL;
    }

    gdouble result = val0 / val1;

    return g_variant_new("d", result);
}

struct template_function {
    const gchar *name;
    GVariant *(*func)(struct token *token, GVariant **args, int nargs, GError **error);
} template_functions[] = {
    {"lc", &helperfn_lc},
    {"uc", &helperfn_uc},
    {"duration", &helperfn_duration},
    {"markup_escape", &helperfn_markup_escape},
    {"default", &helperfn_default},
    {"emoji", &helperfn_emoji},
    {"trunc", &helperfn_trunc},
    {INFIX_ADD, &infixfn_add},
    {INFIX_SUB, &infixfn_sub},
    {INFIX_MUL, &infixfn_mul},
    {INFIX_DIV, &infixfn_div},
};

static GVariant *expand_token(struct token *token, GVariantDict *context, GError **error) {
    GError *tmp_error = NULL;

    switch (token->type) {
    case TOKEN_STRING:
        return g_variant_new("s", token->data);

    case TOKEN_NUMBER:
        return g_variant_new("d", token->numeric_data);

    case TOKEN_VARIABLE:
        if (g_variant_dict_contains(context, token->data)) {
            return g_variant_dict_lookup_value(context, token->data, NULL);
        } else {
            return NULL;
        }

    case TOKEN_FUNCTION: {
        // TODO lift required arg assumption
        assert(token->args != NULL);

        GVariant *ret = NULL;
        int nargs = 0;
        GVariant *args[MAX_ARGS + 1];

        GList *t;
        for (t = token->args; t != NULL; t = t->next) {
            struct token *arg_token = t->data;
            assert(nargs < MAX_ARGS);
            args[nargs++] = expand_token(arg_token, context, &tmp_error);
            if (tmp_error != NULL) {
                g_propagate_error(error, tmp_error);
                goto func_out;
            }
        }

        for (gsize i = 0; i < LENGTH(template_functions); ++i) {
            if (g_strcmp0(template_functions[i].name, token->data) == 0) {
                ret = template_functions[i].func(token, args, nargs, &tmp_error);
                if (tmp_error != NULL) {
                    g_propagate_error(error, tmp_error);
                    goto func_out;
                }

                goto func_out;
            }
        }
        g_set_error(error, playerctl_formatter_error_quark(), 1, "unknown template function: %s",
                    token->data);
    func_out:
        for (int i = 0; i < nargs; ++i) {
            if (args[i] != NULL) {
                g_variant_unref(args[i]);
            }
        }
        return ret;
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

GVariantDict *playerctl_formatter_default_template_context(PlayerctlFormatter *formatter,
                                                           PlayerctlPlayer *player,
                                                           GVariant *base) {
    return get_default_template_context(player, base);
}

gchar *playerctl_formatter_expand_format(PlayerctlFormatter *formatter, GVariantDict *context,
                                         GError **error) {
    GError *tmp_error = NULL;
    gchar *expanded = expand_format(formatter->priv->tokens, context, &tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(error, tmp_error);
        return NULL;
    }

    return expanded;
}
