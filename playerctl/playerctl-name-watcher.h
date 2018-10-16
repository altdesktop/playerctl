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

#ifndef __PLAYERCTL_NAME_WATCHER_H__
#define __PLAYERCTL_NAME_WATCHER_H__

#if !defined(__PLAYERCTL_INSIDE__) && !defined(PLAYERCTL_COMPILATION)
#error "Only <playerctl/playerctl.h> can be included directly."
#endif

#include <playerctl/playerctl-player.h>
#include <glib-object.h>
#include <gio/gio.h>

/**
 * SECTION: playerctl-name-watcher
 * @short_description: A class watch for player names appearing and vanishing
 */
#define PLAYERCTL_TYPE_NAME_WATCHER (playerctl_name_watcher_get_type())
#define PLAYERCTL_NAME_WATCHER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), PLAYERCTL_TYPE_NAME_WATCHER, PlayerctlNameWatcher))
#define PLAYERCTL_IS_NAME_WATCHER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), PLAYERCTL_TYPE_NAME_WATCHER))
#define PLAYERCTL_NAME_WATCHER_CLASS(klass)                        \
    (G_TYPE_CHECK_CLASS_CAST((klass), PLAYERCTL_TYPE_NAME_WATCHER, \
                             PlayerctlNameWatcherClass))
#define PLAYERCTL_IS_NAME_WATCHER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), PLAYERCTL_TYPE_NAME_WATCHER))
#define PLAYERCTL_NAME_WATCHER_GET_CLASS(obj)                      \
    (G_TYPE_INSTANCE_GET_CLASS((obj), PLAYERCTL_TYPE_NAME_WATCHER, \
                               PlayerctlNameWatcherClass))

typedef struct _PlayerctlNameWatcher PlayerctlNameWatcher;
typedef struct _PlayerctlNameWatcherClass PlayerctlNameWatcherClass;
typedef struct _PlayerctlNameWatcherPrivate PlayerctlNameWatcherPrivate;

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

struct _PlayerctlNameWatcher {
    /* Parent instance structure */
    GObject parent_instance;

    /* Private members */
    PlayerctlNameWatcherPrivate *priv;
};

struct _PlayerctlNameWatcherClass {
    /* Parent class structure */
    GObjectClass parent_class;
};

GType playerctl_name_watcher_get_type(void);

PlayerctlNameWatcher *playerctl_name_watcher_new(GError **err);

PlayerctlNameWatcher *playerctl_name_watcher_new_for_bus(GError **err,
                                                         GBusType bus_type);

void playerctl_name_watcher_set_sort_func(PlayerctlNameWatcher *watcher,
                                          GCompareDataFunc sort_func,
                                          gpointer *sort_data,
                                          GDestroyNotify notify);

void playerctl_name_watcher_move_player_to_top(PlayerctlNameWatcher *watcher,
                                               PlayerctlPlayer *player);

#endif /* __PLAYERCTL_NAME_WATCHER_H__ */
