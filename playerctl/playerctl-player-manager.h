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

#include <playerctl/playerctl-player.h>
#include <glib-object.h>
#include <gio/gio.h>

/**
 * SECTION: playerctl-player-manager
 * @short_description: A class watch for player names appearing and vanishing
 */
#define PLAYERCTL_TYPE_PLAYER_MANAGER (playerctl_player_manager_get_type())
#define PLAYERCTL_PLAYER_MANAGER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), PLAYERCTL_TYPE_PLAYER_MANAGER, PlayerctlPlayerManager))
#define PLAYERCTL_IS_PLAYER_MANAGER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), PLAYERCTL_TYPE_PLAYER_MANAGER))
#define PLAYERCTL_PLAYER_MANAGER_CLASS(klass)                        \
    (G_TYPE_CHECK_CLASS_CAST((klass), PLAYERCTL_TYPE_PLAYER_MANAGER, \
                             PlayerctlPlayerManagerClass))
#define PLAYERCTL_IS_PLAYER_MANAGER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), PLAYERCTL_TYPE_PLAYER_MANAGER))
#define PLAYERCTL_PLAYER_MANAGER_GET_CLASS(obj)                      \
    (G_TYPE_INSTANCE_GET_CLASS((obj), PLAYERCTL_TYPE_PLAYER_MANAGER, \
                               PlayerctlPlayerManagerClass))

typedef struct _PlayerctlPlayerManager PlayerctlPlayerManager;
typedef struct _PlayerctlPlayerManagerClass PlayerctlPlayerManagerClass;
typedef struct _PlayerctlPlayerManagerPrivate PlayerctlPlayerManagerPrivate;

#define PLAYERCTL_TYPE_NAME_EVENT (playerctl_name_event_get_type())

typedef struct _PlayerctlNameEvent PlayerctlNameEvent;

/**
 * PlayerctlNameEvent:
 * @name: the name of the player that has appeared or vanished.
 *
 * Event container for when names of players appear or disapear as the
 * controllable media player applications open and close.
 */
struct _PlayerctlNameEvent {
    gchar *name;
};

void playerctl_name_event_free(PlayerctlNameEvent *event);
PlayerctlNameEvent *playerctl_name_event_copy(PlayerctlNameEvent *event);
GType playerctl_name_event_get_type(void);

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
                                            GCompareDataFunc sort_func,
                                            gpointer *sort_data,
                                            GDestroyNotify notify);

void playerctl_player_manager_move_player_to_top(PlayerctlPlayerManager *manager,
                                                 PlayerctlPlayer *player);

#endif /* __PLAYERCTL_PLAYER_MANAGER_H__ */
