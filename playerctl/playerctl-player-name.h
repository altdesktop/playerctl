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
 * Copyright © 2014, Tony Crisci and contributors
 */

#ifndef __PLAYERCTL_PLAYER_NAME_H__
#define __PLAYERCTL_PLAYER_NAME_H__

#include <glib.h>
#include <glib-object.h>

/**
 * SECTION: playerctl-player-name
 * @short_description: A box that contains connection information for a player.
 */

/**
 * PlayerctlSource
 * @PLAYERCTL_SOURCE_NONE: Only for unitialized players. Source will be chosen automatically.
 * @PLAYERCTL_SOURCE_DBUS_SESSION: The player is on the DBus session bus.
 * @PLAYERCTL_SOURCE_DBUS_SYSTEM: The player is on the DBus system bus.
 *
 * The source of the name used to control the player.
 *
 */
typedef enum {
    PLAYERCTL_SOURCE_NONE,
    PLAYERCTL_SOURCE_DBUS_SESSION,
    PLAYERCTL_SOURCE_DBUS_SYSTEM,
} PlayerctlSource;

typedef struct _PlayerctlPlayerName PlayerctlPlayerName;

#define PLAYERCTL_TYPE_PLAYER_NAME (playerctl_player_name_get_type())

void playerctl_player_name_free(PlayerctlPlayerName *name);
PlayerctlPlayerName *playerctl_player_name_copy(PlayerctlPlayerName *name);
GType playerctl_player_name_get_type(void);

/**
 * PlayerctlPlayerName:
 * @name: the name of the player that has appeared or vanished.
 * @source: the source of the player name
 *
 * Event container for when names of players appear or disapear as the
 * controllable media player applications open and close.
 */
struct _PlayerctlPlayerName {
    gchar *name;
    gchar *instance;
    PlayerctlSource source;
};

#endif /* __PLAYERCTL_PLAYER_NAME_H__ */
