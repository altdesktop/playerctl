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
#include "playerctl.h"

#define LENGTH(array) (sizeof array / sizeof array[0])

G_DEFINE_QUARK(playerctl-cli-error-quark, playerctl_cli_error);

/* The player being controlled. */
static gchar *player_name_list = NULL;
/* If true, control all available media players */
static gboolean select_all_players;
/* If true, list all available players' names and exit. */
static gboolean list_all_players_and_exit;
/* If true, print the version and exit. */
static gboolean print_version_and_exit;
/* The commands passed on the command line, filled in via G_OPTION_REMAINING. */
static gchar **command = NULL;

static GString *list_player_names_on_bus(GError **err, GBusType busType) {
    GError *tmp_error = NULL;

    GDBusProxy *proxy = g_dbus_proxy_new_for_bus_sync(
        busType, G_DBUS_PROXY_FLAGS_NONE, NULL, "org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", NULL, &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        return NULL;
    }

    GVariant *reply = g_dbus_proxy_call_sync(
        proxy, "ListNames", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        g_object_unref(proxy);
        return NULL;
    }

    GString *names_str = g_string_new("");
    GVariant *reply_child = g_variant_get_child_value(reply, 0);
    gsize reply_count;
    const gchar **names = g_variant_get_strv(reply_child, &reply_count);

    size_t offset = strlen("org.mpris.MediaPlayer2.");
    for (int i = 0; i < reply_count; i += 1) {
        if (g_str_has_prefix(names[i], "org.mpris.MediaPlayer2.")) {
            g_string_append_printf(names_str, "%s\n", names[i] + offset);
        }
    }

    g_object_unref(proxy);
    g_variant_unref(reply);
    g_variant_unref(reply_child);
    g_free(names);

    return names_str;
}

static gchar *list_player_names(GError **err) {
    GString *sessionPlayers = list_player_names_on_bus(err, G_BUS_TYPE_SESSION);
    GString *systemPlayers = list_player_names_on_bus(err, G_BUS_TYPE_SYSTEM);

    if (!sessionPlayers && !systemPlayers) {
        return NULL;
    }

    if (!sessionPlayers) {
        return g_string_free(systemPlayers, FALSE);
    }

    if (!systemPlayers) {
        return g_string_free(sessionPlayers, FALSE);
    }

    g_string_append(sessionPlayers, systemPlayers->str);
    g_string_free(systemPlayers, TRUE);
    return g_string_free(sessionPlayers, FALSE);
}

#define PLAYER_COMMAND_FUNC(COMMAND)                \
    GError *tmp_error = NULL;                       \
                                                    \
    playerctl_player_##COMMAND(player, &tmp_error); \
    if (tmp_error) {                                \
        g_propagate_error(error, tmp_error);        \
        return FALSE;                               \
    }                                               \
    return TRUE;

static gboolean playercmd_play(PlayerctlPlayer *player, gchar **argv, gint argc,
                     GError **error) {
    PLAYER_COMMAND_FUNC(play);
}

static gboolean playercmd_pause(PlayerctlPlayer *player, gchar **argv, gint argc,
                     GError **error) {
    PLAYER_COMMAND_FUNC(pause);
}

static gboolean playercmd_play_pause(PlayerctlPlayer *player, gchar **argv, gint argc,
                           GError **error) {
    PLAYER_COMMAND_FUNC(play_pause);
}

static gboolean playercmd_stop(PlayerctlPlayer *player, gchar **argv, gint argc,
                     GError **error) {
    PLAYER_COMMAND_FUNC(stop);
}

static gboolean playercmd_next(PlayerctlPlayer *player, gchar **argv, gint argc,
                     GError **error) {
    PLAYER_COMMAND_FUNC(next);
}

static gboolean playercmd_previous(PlayerctlPlayer *player, gchar **argv, gint argc,
                         GError **error) {
    PLAYER_COMMAND_FUNC(previous);
}

#undef PLAYER_COMMAND_FUNC

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
    const gchar *position = *argv;
    gint64 offset;
    GError *tmp_error = NULL;

    if (position) {
        char *endptr = NULL;
        offset = 1000000.0 * strtod(position, &endptr);

        if (position == endptr) {
            g_set_error(error, playerctl_cli_error_quark(), 1,
                        "Could not parse position as a number: %s\n", position);
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
        printf("%f\n", (double)offset / 1000000.0);
    }

    return TRUE;
}

static gboolean playercmd_volume(PlayerctlPlayer *player, gchar **argv, gint argc,
                                  GError **error) {
    const gchar *volume = *argv;
    gdouble level;

    if (volume) {
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
        g_object_set(player, "volume", level, NULL);
    } else {
        g_object_get(player, "volume", &level, NULL);
        g_print("%f\n", level);
    }

    return TRUE;
}

static gboolean playercmd_status(PlayerctlPlayer *player, gchar **argv, gint argc,
                       GError **error) {
    gchar *state = NULL;

    g_object_get(player, "status", &state, NULL);
    printf("%s\n", state ? state : "Not available");
    g_free(state);

    return TRUE;
}

static gboolean playercmd_metadata(PlayerctlPlayer *player, gchar **argv, gint argc,
                             GError **error) {
    if (argc == 0) {
        GError *tmp_error = NULL;
        gchar *data = playerctl_player_print_metadata_prop(player, NULL, &tmp_error);

        printf("%s\n", data);
        g_free(data);
    } else {
        for (int i = 0; i < argc; ++i) {
            const gchar *type = argv[i];
            GError *tmp_error = NULL;
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

            printf("%s\n", data);
            g_free(data);
        }
    }

    return TRUE;
}

struct PlayerCommand {
    const gchar *name;
    gboolean (*func)(PlayerctlPlayer *player, gchar **argv, gint argc, GError **error);
} commands[] = {
    {"open", &playercmd_open},
    {"play", &playercmd_play},
    {"pause", &playercmd_pause},
    {"play-pause", &playercmd_play_pause},
    {"stop", &playercmd_stop},
    {"next", &playercmd_next},
    {"previous", &playercmd_previous},
    {"position", &playercmd_position},
    {"volume", &playercmd_volume},
    {"status", &playercmd_status},
    {"metadata", &playercmd_metadata},
};

static gboolean handle_player_command(PlayerctlPlayer *player, gchar **command,
                                      gint num_commands, GError **error) {
    if (num_commands < 1) {
        return FALSE;
    }

    for (int i = 0; i < LENGTH(commands); i++) {
        if (g_strcmp0(commands[i].name, command[0]) == 0) {
            // Do not pass the command's name to the function that processes it.
            return commands[i].func(player, command + 1, num_commands - 1, error);
        }
    }
    g_set_error(error, playerctl_cli_error_quark(), 1,
                "Command not recognized: %s", command[0]);
    return FALSE;
}

static const GOptionEntry entries[] = {
    {"player", 'p', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &player_name_list,
     "A comma separated list of names of players to control (default: the "
     "first available player)",
     "NAME"},
    {"all-players", 'a', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
     &select_all_players, "Select all available players to be controlled",
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

int main(int argc, char *argv[]) {
    PlayerctlPlayer *player;
    GError *error = NULL;
    int exit_status = 0;
    gint num_commands = 0;

    // seems to be required to print unicode (see #8)
    setlocale(LC_CTYPE, "");

    if (!parse_setup_options(argc, argv, &error)) {
        g_printerr("%s\n", error->message);
        exit_status = 0;
        goto end;
    }

    if (print_version_and_exit) {
        g_print("v%s\n", PLAYERCTL_VERSION_S);
        exit_status = 0;
        goto end;
    }

    if (list_all_players_and_exit) {
        gchar *player_names = list_player_names(&error);

        if (error) {
            g_printerr("%s\n", error->message);
            exit_status = 1;
            goto end;
        }

        if (player_names[0] == '\0') {
            g_printerr("No players were found\n");
        } else {
            g_print("%s", player_names);
        }
        g_free(player_names);

        exit_status = 0;
        goto end;
    }

    gchar *player_names = player_name_list;
    if (select_all_players) {
        player_names = list_player_names(&error);

        if (error) {
            g_printerr("%s\n", error->message);
            exit_status = 1;
            goto end;
        }
    }

    const gchar *delim = ",\n";
    const gboolean multiple_names = player_names != NULL;
    gchar *player_name = multiple_names ? strtok(player_names, delim) : NULL;

    // count the extra arguments given
    while (command[num_commands] != NULL) {
        ++num_commands;
    }

    for (;;) {
        player = playerctl_player_new(player_name, &error);

        if (error != NULL) {
            g_printerr("Connection to player failed: %s\n", error->message);
            exit_status = 1;
            goto loopend;
        }

        if (!handle_player_command(player, command, num_commands, &error)) {
            g_printerr("Could not execute command: %s\n", error->message);
            exit_status = 1;
        }

    loopend:
        if (player != NULL) {
            g_object_unref(player);
        }
        g_clear_error(&error);
        error = NULL;

        if (!multiple_names) {
            return exit_status;
        }

        player_name = strtok(NULL, delim);
        if (!player_name) {
            return exit_status;
        }
    }

end:
    g_clear_error(&error);

    return exit_status;
}
