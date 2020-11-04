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

#include "playerctl-common.h"

#include <glib.h>
#include <stdio.h>
#include <strings.h>

gboolean pctl_parse_playback_status(const gchar *status_str, PlayerctlPlaybackStatus *status) {
    if (status_str == NULL) {
        return FALSE;
    }

    if (strcasecmp(status_str, "Playing") == 0) {
        *status = PLAYERCTL_PLAYBACK_STATUS_PLAYING;
        return TRUE;
    } else if (strcasecmp(status_str, "Paused") == 0) {
        *status = PLAYERCTL_PLAYBACK_STATUS_PAUSED;
        return TRUE;
    } else if (strcasecmp(status_str, "Stopped") == 0) {
        *status = PLAYERCTL_PLAYBACK_STATUS_STOPPED;
        return TRUE;
    }

    return FALSE;
}

const gchar *pctl_playback_status_to_string(PlayerctlPlaybackStatus status) {
    switch (status) {
    case PLAYERCTL_PLAYBACK_STATUS_PLAYING:
        return "Playing";
    case PLAYERCTL_PLAYBACK_STATUS_PAUSED:
        return "Paused";
    case PLAYERCTL_PLAYBACK_STATUS_STOPPED:
        return "Stopped";
    }
    return NULL;
}

const gchar *pctl_loop_status_to_string(PlayerctlLoopStatus status) {
    switch (status) {
    case PLAYERCTL_LOOP_STATUS_NONE:
        return "None";
    case PLAYERCTL_LOOP_STATUS_TRACK:
        return "Track";
    case PLAYERCTL_LOOP_STATUS_PLAYLIST:
        return "Playlist";
    }
    return NULL;
}

gboolean pctl_parse_loop_status(const gchar *status_str, PlayerctlLoopStatus *status) {
    if (status_str == NULL) {
        return FALSE;
    }

    if (strcasecmp(status_str, "None") == 0) {
        *status = PLAYERCTL_LOOP_STATUS_NONE;
        return TRUE;
    } else if (strcasecmp(status_str, "Track") == 0) {
        *status = PLAYERCTL_LOOP_STATUS_TRACK;
        return TRUE;
    } else if (strcasecmp(status_str, "Playlist") == 0) {
        *status = PLAYERCTL_LOOP_STATUS_PLAYLIST;
        return TRUE;
    }

    return FALSE;
}

gchar *pctl_print_gvariant(GVariant *value) {
    GString *printed = g_string_new("");
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING_ARRAY)) {
        gsize prop_count;
        const gchar **prop_strv = g_variant_get_strv(value, &prop_count);

        for (gsize i = 0; i < prop_count; i += 1) {
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

GBusType pctl_source_to_bus_type(PlayerctlSource source) {
    switch (source) {
    case PLAYERCTL_SOURCE_DBUS_SESSION:
        return G_BUS_TYPE_SESSION;
    case PLAYERCTL_SOURCE_DBUS_SYSTEM:
        return G_BUS_TYPE_SYSTEM;
    default:
        return G_BUS_TYPE_NONE;
    }
}

PlayerctlSource pctl_bus_type_to_source(GBusType bus_type) {
    switch (bus_type) {
    case G_BUS_TYPE_SESSION:
        return PLAYERCTL_SOURCE_DBUS_SESSION;
    case G_BUS_TYPE_SYSTEM:
        return PLAYERCTL_SOURCE_DBUS_SYSTEM;
    default:
        g_warning("could not convert bus type to source: %d\n", bus_type);
        return PLAYERCTL_SOURCE_NONE;
    }
}

PlayerctlPlayerName *pctl_player_name_new(const gchar *instance, PlayerctlSource source) {
    PlayerctlPlayerName *player_name = g_slice_new(PlayerctlPlayerName);
    gchar **split = g_strsplit(instance, ".", 2);
    player_name->name = g_strdup(split[0]);
    g_strfreev(split);
    player_name->instance = g_strdup(instance);
    player_name->source = source;
    return player_name;
}

gint pctl_player_name_compare(PlayerctlPlayerName *name_a, PlayerctlPlayerName *name_b) {
    if (name_a->source != name_b->source) {
        return 1;
    }
    return g_strcmp0(name_a->instance, name_b->instance);
}

gint pctl_player_name_instance_compare(PlayerctlPlayerName *name, PlayerctlPlayerName *instance) {
    if (name->source != instance->source) {
        return 1;
    }
    return pctl_player_name_string_instance_compare(name->instance, instance->instance);
}

gint pctl_player_name_string_instance_compare(const gchar *name, const gchar *instance) {
    if (g_strcmp0(name, "%any") == 0 || g_strcmp0(instance, "%any") == 0) {
        return 0;
    }

    gboolean exact_match = (g_strcmp0(name, instance) == 0);
    gboolean instance_match = !exact_match && (g_str_has_prefix(instance, name) &&
                                               g_str_has_prefix(instance + strlen(name), "."));

    if (exact_match || instance_match) {
        return 0;
    } else {
        return 1;
    }
}

GList *pctl_player_name_find(GList *list, gchar *player_id, PlayerctlSource source) {
    PlayerctlPlayerName player_name = {
        .instance = player_id,
        .source = source,
    };

    return g_list_find_custom(list, &player_name, (GCompareFunc)pctl_player_name_compare);
}

GList *pctl_player_name_find_instance(GList *list, gchar *player_id, PlayerctlSource source) {
    PlayerctlPlayerName player_name = {
        .instance = player_id,
        .source = source,
    };

    return g_list_find_custom(list, &player_name, (GCompareFunc)pctl_player_name_instance_compare);
}

void pctl_player_name_list_destroy(GList *list) {
    g_list_free_full(list, (GDestroyNotify)playerctl_player_name_free);
}

GList *pctl_list_player_names_on_bus(GBusType bus_type, GError **err) {
    GError *tmp_error = NULL;
    GList *players = NULL;

    GDBusProxy *proxy = g_dbus_proxy_new_for_bus_sync(
        bus_type, G_DBUS_PROXY_FLAGS_NONE, NULL, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", NULL, &tmp_error);

    if (tmp_error != NULL) {
        if (tmp_error->domain == G_IO_ERROR && tmp_error->code == G_IO_ERROR_NOT_FOUND) {
            // XXX: This means the dbus socket address is not found which may
            // mean that the bus is not running or the address was set
            // incorrectly. I think we can pass through here because it is true
            // that there are no names on the bus that is supposed to be at
            // this socket path. But we need a better way of dealing with this case.
            g_warning("D-Bus socket address not found, unable to list player names");
            g_clear_error(&tmp_error);
            return NULL;
        }
        g_propagate_error(err, tmp_error);
        return NULL;
    }

    g_debug("Getting list of player names from D-Bus");
    GVariant *reply = g_dbus_proxy_call_sync(proxy, "ListNames", NULL, G_DBUS_CALL_FLAGS_NONE, -1,
                                             NULL, &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        g_object_unref(proxy);
        return NULL;
    }

    GVariant *reply_child = g_variant_get_child_value(reply, 0);
    gsize reply_count;
    const gchar **names = g_variant_get_strv(reply_child, &reply_count);

    size_t offset = strlen(MPRIS_PREFIX);
    for (gsize i = 0; i < reply_count; i += 1) {
        if (g_str_has_prefix(names[i], MPRIS_PREFIX)) {
            PlayerctlPlayerName *player_name =
                pctl_player_name_new(names[i] + offset, pctl_bus_type_to_source(bus_type));
            players = g_list_append(players, player_name);
        }
    }

    g_object_unref(proxy);
    g_variant_unref(reply);
    g_variant_unref(reply_child);
    g_free(names);

    return players;
}
