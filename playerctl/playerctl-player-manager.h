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
 * Copyright © 2014, Tony Crisci
 */

#ifndef __PLAYERCTL_PLAYER_MANAGER_H__
#define __PLAYERCTL_PLAYER_MANAGER_H__

#if !defined(__PLAYERCTL_INSIDE__) && !defined(PLAYERCTL_COMPILATION)
#error "Only <playerctl/playerctl.h> can be included directly."
#endif

#include <glib-object.h>
#include <playerctl/playerctl-player.h>

/**
 * SECTION: playerctl-player-manager
 * @short_description: A class to watch for players appearing and vanishing.
 *
 * The #PlayerctlPlayerManager is a class to watch for players appearing and
 * vanishing. When a player opens and is available to control by `playerctl`,
 * the #PlayerctlPlayerManager::name-appeared event will be emitted on the
 * manager during the main loop. You can inspect this #PlayerctlPlayerName to
 * see if you want to manage it. If you do, create a #PlayerctlPlayer from it
 * with the playerctl_player_new_from_name() function. The manager is also
 * capable of keeping an up-to-date list of players you want it to manage in
 * the #PlayerctlPlayerManager:players list. These players are connected and
 * should be able to be controlled. Managing players is optional, and you can
 * do so manually if you like.
 *
 * When the player disconnects, the #PlayerctlPlayerManager::name-vanished
 * event will be emitted. If the player is managed and is going to be removed
 * from the list, the #PlayerctlPlayerManager::player-vanished event will also
 * be emitted. After this event, the player will be cleaned up and removed from
 * the manager.
 *
 * The manager has other features such as being able to keep the players in a
 * sorted order and moving a player to the top of the list. The
 * #PlayerctlPlayerManager:player-names will always be in the order that they
 * were known to appear after the manager was created.
 *
 * For examples on how to use the manager, see the `examples` folder in the git
 * repository.
 */
#define PLAYERCTL_TYPE_PLAYER_MANAGER (playerctl_player_manager_get_type())
#define PLAYERCTL_PLAYER_MANAGER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), PLAYERCTL_TYPE_PLAYER_MANAGER, PlayerctlPlayerManager))
#define PLAYERCTL_IS_PLAYER_MANAGER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), PLAYERCTL_TYPE_PLAYER_MANAGER))
#define PLAYERCTL_PLAYER_MANAGER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), PLAYERCTL_TYPE_PLAYER_MANAGER, PlayerctlPlayerManagerClass))
#define PLAYERCTL_IS_PLAYER_MANAGER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), PLAYERCTL_TYPE_PLAYER_MANAGER))
#define PLAYERCTL_PLAYER_MANAGER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), PLAYERCTL_TYPE_PLAYER_MANAGER, PlayerctlPlayerManagerClass))

typedef struct _PlayerctlPlayerManager PlayerctlPlayerManager;
typedef struct _PlayerctlPlayerManagerClass PlayerctlPlayerManagerClass;
typedef struct _PlayerctlPlayerManagerPrivate PlayerctlPlayerManagerPrivate;

struct _PlayerctlPlayerManager {
    /* Parent instance structure */
    GObject parent_instance;

    /* Private members */
    PlayerctlPlayerManagerPrivate *priv;
};

struct _PlayerctlPlayerManagerClass {
    /* Parent class structure */
    GObjectClass parent_class;
};

GType playerctl_player_manager_get_type(void);

PlayerctlPlayerManager *playerctl_player_manager_new(GError **err);

void playerctl_player_manager_manage_player(PlayerctlPlayerManager *manager,
                                            PlayerctlPlayer *player);

void playerctl_player_manager_set_sort_func(PlayerctlPlayerManager *manager,
                                            GCompareDataFunc sort_func, gpointer *sort_data,
                                            GDestroyNotify notify);

void playerctl_player_manager_move_player_to_top(PlayerctlPlayerManager *manager,
                                                 PlayerctlPlayer *player);

#endif /* __PLAYERCTL_PLAYER_MANAGER_H__ */
