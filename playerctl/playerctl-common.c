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

#include <stdio.h>
#include <glib.h>
#include <strings.h>
#include "playerctl-common.h"

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

PlayerctlPlayerName *pctl_player_name_new(const gchar *name,
                                          PlayerctlSource source) {
    PlayerctlPlayerName *player_name = g_slice_new(PlayerctlPlayerName);
    player_name->name = g_strdup(name);
    player_name->source = source;
    return player_name;
}


gint pctl_player_name_compare(PlayerctlPlayerName *name_a,
                              PlayerctlPlayerName *name_b) {
    if (name_a->source != name_b->source) {
        return 1;
    }
    return g_strcmp0(name_a->name, name_b->name);
}

gint pctl_player_name_instance_compare(PlayerctlPlayerName *name,
                                       PlayerctlPlayerName *instance) {
    if (name->source != instance->source) {
        return 1;
    }
    return pctl_player_name_string_instance_compare(name->name, instance->name);
}

gint pctl_player_name_string_instance_compare(gchar *name, gchar *instance) {
    gboolean exact_match = (g_strcmp0(name, instance) == 0);
    gboolean instance_match = !exact_match && (g_str_has_prefix(instance, name) &&
            g_str_has_prefix(instance + strlen(name), ".instance"));

    if (exact_match || instance_match) {
        return 0;
    } else {
        return 1;
    }
}

GList *pctl_player_name_find(GList *list, gchar *player_id, PlayerctlSource source) {
    PlayerctlPlayerName player_name = {
        .name = player_id,
        .source = source,
    };

    return g_list_find_custom(list, &player_name,
                              (GCompareFunc)pctl_player_name_compare);
}

GList *pctl_player_name_find_instance(GList *list, gchar *player_id, PlayerctlSource source) {
    PlayerctlPlayerName player_name = {
        .name = player_id,
        .source = source,
    };

    return g_list_find_custom(list, &player_name,
                              (GCompareFunc)pctl_player_name_instance_compare);

}

void pctl_player_name_list_destroy(GList *list) {
    g_list_free_full(list, (GDestroyNotify)playerctl_player_name_free);
}
