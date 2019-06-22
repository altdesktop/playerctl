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

#include "playerctl-player-name.h"

/**
 * playerctl_player_name_copy:
 * @name: a #PlayerctlPlayerName
 *
 * Creates a dynamically allocated name name container as a copy of
 * @name.
 *
 * Returns: (transfer full): a newly-allocated copy of @name
 */
PlayerctlPlayerName *playerctl_player_name_copy(PlayerctlPlayerName *name) {
    PlayerctlPlayerName *retval;

    g_return_val_if_fail(name != NULL, NULL);

    retval = g_slice_new0(PlayerctlPlayerName);
    *retval = *name;

    retval->source = name->source;
    retval->instance = g_strdup(name->instance);
    retval->name = g_strdup(name->name);

    return retval;
}

/**
 * playerctl_player_name_free:
 * @name:(allow-none): a #PlayerctlPlayerName
 *
 * Frees @name. If @name is %NULL, it simply returns.
 */
void playerctl_player_name_free(PlayerctlPlayerName *name) {
    if (name == NULL) {
        return;
    }

    g_free(name->instance);
    g_free(name->name);
    g_slice_free(PlayerctlPlayerName, name);
}

G_DEFINE_BOXED_TYPE(PlayerctlPlayerName, playerctl_player_name, playerctl_player_name_copy,
                    playerctl_player_name_free);
