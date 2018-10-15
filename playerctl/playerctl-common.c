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
#include "playerctl-common.h"

gboolean pctl_parse_playback_status(const gchar *status_str, PlayerctlPlaybackStatus *status) {
    if (status_str == NULL) {
        return FALSE;
    }

    if (g_strcmp0(status_str, "Playing") == 0) {
        *status = PLAYERCTL_PLAYBACK_STATUS_PLAYING;
        return TRUE;
    } else if (g_strcmp0(status_str, "Paused") == 0) {
        *status = PLAYERCTL_PLAYBACK_STATUS_PAUSED;
        return TRUE;
    } else {
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

gint pctl_player_name_instance_compare(gchar *name, gchar *instance) {
    gboolean exact_match = (g_strcmp0(name, instance) == 0);
    gboolean instance_match = !exact_match && (g_str_has_prefix(instance, name) &&
            g_str_has_prefix(instance + strlen(name), ".instance"));

    if (exact_match || instance_match) {
        return 0;
    } else {
        return 1;
    }
}
