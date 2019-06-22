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

#include <glib-object.h>
#include <glib.h>

/**
 * SECTION: playerctl-player-name
 * @short_description: Contains connection information that fully qualifies a
 * potential connection to a player.
 *
 * Contains connection information that fully qualifies a potential connection
 * to a player. You should not have to construct one of these directly. You can
 * list the names that are available to control from the
 * playerctl_list_players() function or use the
 * #PlayerctlPlayerManager:player-names property from a
 * #PlayerctlPlayerManager.
 *
 * Once you have gotten a player name like this, you can check the type of
 * player with the "name" property to see if you are interested in connecting
 * to it. If you are, you can pass it directly to the
 * playerctl_player_new_from_name() function to get a #PlayerctlPlayer that is
 * connected to this name and ready to command and query.
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
 * @name: the name of the type of player.
 * @instance: the complete name and instance of the player.
 * @source: the source of the player name.
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
