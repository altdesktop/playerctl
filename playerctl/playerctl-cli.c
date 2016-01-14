/* vim:ts=2:sw=2:expandtab
 *
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
 * Copyright © 2014, Tony Crisci
 */

#include "playerctl.h"
#include <gio/gio.h>
#include <locale.h>

static char *player_name = NULL;
static gboolean list_all_opt = FALSE;
static gboolean version_opt = FALSE;
static char **command = NULL;

static char *description = "Available Commands:"
"\n  play                  Command the player to play"
"\n  pause                 Command the player to pause"
"\n  play-pause            Command the player to toggle between play/pause"
"\n  stop                  Command the player to stop"
"\n  next                  Command the player to skip to the next track"
"\n  previous              Command the player to skip to the previous track"
"\n  volume [LEVEL][+/-]   Print or set the volume to LEVEL from 0.0 to 1.0"
"\n                          volume +0.1 (increase volume by 10%)"
"\n                          volume -0.2 (decrease volume by 20%)"
"\n  status                Get the play status of the player"
"\n  metadata [KEY]        Print metadata information for the current track. Print only value of KEY if passed";

static char *summary = "  For true players only: spotify, vlc, audacious, bmp, xmms2, and others.";

static GOptionEntry entries[] = {
  { "player", 'p', 0, G_OPTION_ARG_STRING, &player_name, "The name of the player to control (default: the first available player)", "NAME" },
  { "list-all", 'l', 0, G_OPTION_ARG_NONE, &list_all_opt, "List the names of running players that can be controlled", NULL},
  { "version", 'V', 0, G_OPTION_ARG_NONE, &version_opt, "Print version information and exit", NULL},
  { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &command, NULL, "COMMAND" },
  { NULL }
};

/* returns a newline delimitted list of player names */
static gchar *list_player_names(GError **err);

int main (int argc, char *argv[])
{
  // seems to be required to print unicode (see #8)
  setlocale(LC_CTYPE, "");

  GOptionContext *context = NULL;
  GError *error = NULL;

  context = g_option_context_new("- Controller for MPRIS players");
  g_option_context_add_main_entries(context, entries, NULL);
  g_option_context_set_description(context, description);
  g_option_context_set_summary(context, summary);

  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("Option parsing failed: %s\n", error->message);
    return 1;
  }

  if (version_opt) {
    g_print("v%s\n", PLAYERCTL_VERSION_S);
    return 0;
  }

  if (list_all_opt) {
    gchar *player_names = list_player_names(&error);

    if (error != NULL) {
      g_printerr("Could not list players: %s\n", error->message);
      return 1;
    }

    if (player_names[0] == '\0')
      g_printerr("%s\n", "No players were found");
    else
      g_print("%s", player_names);

    return 0;
  }

  if (command == NULL) {
    g_print("%s", g_option_context_get_help(context, TRUE, NULL));
    return 0;
  }

  PlayerctlPlayer *player = playerctl_player_new(player_name, &error);

  if (error != NULL) {
    g_printerr("Connection to player failed: %s\n", error->message);
    return 1;
  }

  if (g_strcmp0(command[0], "volume") == 0) {
    /* VOLUME */
    gdouble level;

    if (command[1]) {
      /* set */

      if(g_str_has_suffix(command[1], "+") || g_str_has_suffix(command[1], "-")) {
        /* increase or decrease current volume */
        gdouble adjustment = g_ascii_strtod(command[1], NULL);

        if(g_str_has_suffix(command[1], "-")) {
            adjustment *= -1;
        }

        g_object_get(player, "volume", &level, NULL);

        level += adjustment;
      } else {
        /* set exact */
        level = g_ascii_strtod(command[1], NULL);
      }

      g_object_set(player, "volume", level, NULL);
    } else {
      /* get */
      g_object_get(player, "volume", &level, NULL);
      g_print("%g\n", level);
    }
  } else if (g_strcmp0(command[0], "play") == 0) {
    /* PLAY */
    playerctl_player_play(player, &error);
  } else if (g_strcmp0(command[0], "pause") == 0) {
    /* PAUSE */
    playerctl_player_pause(player, &error);
  } else if (g_strcmp0(command[0], "play-pause") == 0) {
    /* PLAY-PAUSE */
    playerctl_player_play_pause(player, &error);
  } else if (g_strcmp0(command[0], "stop") == 0) {
    /* STOP */
    playerctl_player_stop(player, &error);
  } else if (g_strcmp0(command[0], "next") == 0) {
    /* NEXT */
    playerctl_player_next(player, &error);
  } else if (g_strcmp0(command[0], "previous") == 0) {
    /* PREVIOUS */
    playerctl_player_previous(player, &error);
  } else if (g_strcmp0(command[0], "metadata") == 0) {
    /* METADATA */
    gchar *value = NULL;
    if (g_strcmp0(command[1], "artist") == 0)
      value = playerctl_player_get_artist(player, &error);
    else if (g_strcmp0(command[1], "title") == 0)
      value = playerctl_player_get_title(player, &error);
    else if (g_strcmp0(command[1], "album") == 0)
      value = playerctl_player_get_album(player, &error);
    else
       value = playerctl_player_print_metadata_prop(player, command[1], &error);

    g_print("%s", value);

    g_free(value);
  } else if (g_strcmp0(command[0], "status") == 0) {
    /* STATUS */
    gchar *status = NULL;
    g_object_get(player, "status", &status, NULL);

    if (status) {
      g_print("%s\n", status);
    } else {
      g_print("Not available\n");
    }

    g_free(status);
  } else {
    /* unrecognized command */
    g_print("%s", g_option_context_get_help(context, TRUE, NULL));
  }

  if (error != NULL) {
    g_printerr("An error occurred: %s\n", error->message);
    return 1;
  }

  return 0;
}

static gchar *list_player_names(GError **err)
{
  GString *names_str = g_string_new("");
  GError *tmp_error = NULL;

  GDBusProxy *proxy = g_dbus_proxy_new_for_bus_sync(
      G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_NONE,
      NULL,
      "org.freedesktop.DBus",
      "/org/freedesktop/DBus",
      "org.freedesktop.DBus",
      NULL,
      &tmp_error);

    if (tmp_error != NULL) {
      g_propagate_error(err, tmp_error);
      return NULL;
    }

    GVariant *reply = g_dbus_proxy_call_sync(proxy,
        "ListNames",
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &tmp_error);

    if (tmp_error != NULL) {
      g_propagate_error(err, tmp_error);
      g_object_unref(proxy);
      return NULL;
    }

    GVariant *reply_child = g_variant_get_child_value(reply, 0);
    gsize reply_count;
    const gchar** names = g_variant_get_strv(reply_child, &reply_count);

    for (int i = 0; i < reply_count; i += 1) {
      if (g_str_has_prefix(names[i], "org.mpris.MediaPlayer2")) {
        gchar **bus_name_split = g_strsplit(names[i], ".", 4);
        g_string_append_printf(names_str, "%s\n", bus_name_split[3]);
        g_strfreev(bus_name_split);
      }
    }

    g_object_unref(proxy);
    g_variant_unref(reply);
    g_variant_unref(reply_child);
    g_free(names);

    return g_string_free(names_str, FALSE);
}
