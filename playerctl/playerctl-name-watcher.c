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

#include <gio/gio.h>
#include <glib-object.h>
#include "playerctl/playerctl-common.h"
#include "playerctl/playerctl-name-watcher.h"
#include "playerctl/playerctl-marshal.h"
#include <playerctl/playerctl-player.h>

enum {
    PROP_0,
    PROP_PLAYERS,
    PROP_PLAYER_NAMES,
    PROP_BUS_TYPE,
    N_PROPERTIES,
};

enum {
    NAME_APPEARED,
    NAME_VANISHED,
    PLAYER_APPEARED,
    PLAYER_VANISHED,
    LAST_SIGNAL,
};

static GParamSpec *obj_properties[N_PROPERTIES] = {
    NULL,
};

static guint connection_signals[LAST_SIGNAL] = {0};

struct _PlayerctlNameWatcherPrivate {
    gboolean initted;
    GError *init_error;
    GDBusProxy *proxy;
    GList *player_names;
    GList *players;
    GBusType bus_type;
    GCompareDataFunc sort_func;
    gpointer *sort_data;
    GDestroyNotify sort_notify;
};

static void playerctl_name_watcher_initable_iface_init(GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE(PlayerctlNameWatcher, playerctl_name_watcher, G_TYPE_OBJECT,
                        G_ADD_PRIVATE(PlayerctlNameWatcher) G_IMPLEMENT_INTERFACE(
                            G_TYPE_INITABLE,
                            playerctl_name_watcher_initable_iface_init));


static void playerctl_name_watcher_set_property(GObject *object, guint property_id,
                                          const GValue *value,
                                          GParamSpec *pspec) {
    PlayerctlNameWatcher *watcher = PLAYERCTL_NAME_WATCHER(object);

    switch (property_id) {
    case PROP_BUS_TYPE:
        watcher->priv->bus_type = g_value_get_enum(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void playerctl_name_watcher_get_property(GObject *object, guint property_id,
                                          GValue *value, GParamSpec *pspec) {
    PlayerctlNameWatcher *watcher = PLAYERCTL_NAME_WATCHER(object);
    switch (property_id) {
    case PROP_PLAYERS:
        g_value_set_pointer(value, watcher->priv->players);
        break;
    case PROP_PLAYER_NAMES:
        g_value_set_pointer(value, watcher->priv->player_names);
        break;
    case PROP_BUS_TYPE:
        g_value_set_enum(value, watcher->priv->bus_type);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void playerctl_name_watcher_constructed(GObject *gobject) {
    PlayerctlNameWatcher *watcher = PLAYERCTL_NAME_WATCHER(gobject);
    g_initable_init(G_INITABLE(watcher), NULL, &watcher->priv->init_error);
    G_OBJECT_CLASS(playerctl_name_watcher_parent_class)->constructed(gobject);
}

static void playerctl_name_watcher_dispose(GObject *gobject) {
    PlayerctlNameWatcher *watcher = PLAYERCTL_NAME_WATCHER(gobject);

    g_clear_error(&watcher->priv->init_error);
    g_clear_object(&watcher->priv->proxy);

    G_OBJECT_CLASS(playerctl_name_watcher_parent_class)->dispose(gobject);
}

static void playerctl_name_watcher_finalize(GObject *gobject) {
    PlayerctlNameWatcher *watcher = PLAYERCTL_NAME_WATCHER(gobject);

    g_list_free_full(watcher->priv->player_names, g_free);
    g_list_free_full(watcher->priv->players, g_object_unref);

    G_OBJECT_CLASS(playerctl_name_watcher_parent_class)->finalize(gobject);
}


/**
 * playerctl_name_event_copy:
 * @event: a #PlayerctlNameEvent
 *
 * Creates a dynamically allocated name event container as a copy of
 * @event.
 *
 * Returns: (transfer full): a newly-allocated copy of @event
 */
PlayerctlNameEvent *playerctl_name_event_copy(PlayerctlNameEvent *event) {
    PlayerctlNameEvent *retval;

    g_return_val_if_fail(event != NULL, NULL);

    retval = g_slice_new0(PlayerctlNameEvent);
    *retval = *event;

    retval->name = g_strdup(event->name);

    return retval;
}

/**
 * playerctl_name_event_free:
 * @event: (allow-none): a #PlayerctlNameEvent
 *
 * Frees @event. If @event is %NULL, it simply returns.
 */
void playerctl_name_event_free(PlayerctlNameEvent *event) {
    if (event == NULL) {
        return;
    }

    g_free(event->name);
    g_slice_free(PlayerctlNameEvent, event);
}

G_DEFINE_BOXED_TYPE(PlayerctlNameEvent, playerctl_name_event,
    playerctl_name_event_copy, playerctl_name_event_free);


static void playerctl_name_watcher_class_init(PlayerctlNameWatcherClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->set_property = playerctl_name_watcher_set_property;
    gobject_class->get_property = playerctl_name_watcher_get_property;
    gobject_class->constructed = playerctl_name_watcher_constructed;
    gobject_class->dispose = playerctl_name_watcher_dispose;
    gobject_class->finalize = playerctl_name_watcher_finalize;

    /**
     * PlayerctlNameWatcher:players: (transfer none) (type GList(PlayerctlPlayer)):
     *
     * A list of players that are currently managed by this class.
     */
    obj_properties[PROP_PLAYERS] =
       g_param_spec_pointer("players",
                            "players",
                            "A list of player objects managed by this watcher",
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * PlayerctlNameWatcher:player-names: (transfer none) (type GList(utf8)):
     *
     * A list of player names that are currently available to control.
     */
    obj_properties[PROP_PLAYER_NAMES] =
       g_param_spec_pointer("player-names",
                            "player names",
                            "A list of player names that are currently available to control.",
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_BUS_TYPE] =
        g_param_spec_enum("bus-type", "Bus type",
                          "The bus type to watch names on",
                          g_bus_type_get_type(),
                          G_BUS_TYPE_SESSION,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(gobject_class, N_PROPERTIES,
                                      obj_properties);

    /**
    * PlayerctlNameWatcher::name-appeared:
    * @self: the #PlayerctlNameWatcher on which the signal was emitted
    * @event: A #PlayerctlNameEvent containing information about the name appearing.
    *
    * Returns: (allow-none) (transfer none): A #PlayerctlPlayer to be managed by this class.
    */
    connection_signals[NAME_APPEARED] =
        g_signal_new("name-appeared",
                     PLAYERCTL_TYPE_NAME_WATCHER,
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_user_marshal_OBJECT__BOXED,
                     PLAYERCTL_TYPE_PLAYER,
                     1,
                     PLAYERCTL_TYPE_NAME_EVENT);

    connection_signals[NAME_VANISHED] =
        g_signal_new("name-vanished",
                     PLAYERCTL_TYPE_NAME_WATCHER,
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__BOXED,
                     G_TYPE_NONE,
                     1,
                     PLAYERCTL_TYPE_NAME_EVENT);

    connection_signals[PLAYER_APPEARED] =
        g_signal_new("player-appeared",
                     PLAYERCTL_TYPE_NAME_WATCHER,
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__OBJECT,
                     G_TYPE_NONE,
                     1,
                     PLAYERCTL_TYPE_PLAYER);

    connection_signals[PLAYER_VANISHED] =
        g_signal_new("player-vanished",
                     PLAYERCTL_TYPE_NAME_WATCHER,
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__OBJECT,
                     G_TYPE_NONE,
                     1,
                     PLAYERCTL_TYPE_PLAYER);
}

static void playerctl_name_watcher_init(PlayerctlNameWatcher *watcher) {
    watcher->priv = playerctl_name_watcher_get_instance_private(watcher);
}

static gchar *player_id_from_bus_name(const gchar *bus_name) {
    const size_t prefix_len = strlen(MPRIS_PREFIX);

    if (bus_name == NULL ||
            !g_str_has_prefix(bus_name, MPRIS_PREFIX) ||
            strlen(bus_name) <= prefix_len) {
        return NULL;
    }

    return g_strdup(bus_name + prefix_len);
}

static void watcher_add_managed_player(PlayerctlNameWatcher *watcher,
                                       PlayerctlPlayer *player) {
    GList *l = NULL;
    for (l = watcher->priv->players; l != NULL; l = l->next) {
        PlayerctlPlayer *current = PLAYERCTL_PLAYER(l->data);
        if (player == current) {
            return;
        }
    }

    if (watcher->priv->sort_func) {
        watcher->priv->players =
            g_list_insert_sorted_with_data(watcher->priv->players, player,
                                           watcher->priv->sort_func,
                                           watcher->priv->sort_data);
    } else {
        watcher->priv->players = g_list_prepend(watcher->priv->players, player);
    }

    g_signal_emit(watcher, connection_signals[PLAYER_APPEARED], 0, player);
}

static void watcher_remove_managed_player_by_name(PlayerctlNameWatcher *watcher,
                                                  gchar *player_name) {
    GList *l = NULL;
    for (l = watcher->priv->players; l != NULL; l = l->next) {
        PlayerctlPlayer *player = PLAYERCTL_PLAYER(l->data);
        gchar *id = NULL;
        g_object_get(player, "player-id", &id, NULL);
        if (g_strcmp0(id, player_name) == 0) {
            g_signal_emit(watcher, connection_signals[PLAYER_VANISHED], 0, player);
            watcher->priv->players = g_list_remove_link(watcher->priv->players, l);
            g_list_free_full(l, g_object_unref);
            g_free(id);
            break;
        }
        g_free(id);
    }
}

static void dbus_name_owner_changed_callback(GDBusProxy *proxy, gchar *sender_name,
                                             gchar *signal_name, GVariant *parameters,
                                             gpointer *data) {
    PlayerctlNameWatcher *watcher = PLAYERCTL_NAME_WATCHER(data);

    if (g_strcmp0(signal_name, "NameOwnerChanged") != 0) {
        return;
    }

    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(sss)"))) {
        g_warning("Got unknown parameters on org.freedesktop.DBus "
                  "NameOwnerChange signal: %s",
                  g_variant_get_type_string(parameters));
        return;
    }

    GVariant *name_variant = g_variant_get_child_value(parameters, 0);
    const gchar *name = g_variant_get_string(name_variant, NULL);
    gchar *player_id = player_id_from_bus_name(name);

    if (player_id == NULL) {
        g_variant_unref(name_variant);
        return;
    }

    GVariant *previous_owner_variant = g_variant_get_child_value(parameters, 1);
    const gchar *previous_owner = g_variant_get_string(previous_owner_variant, NULL);

    GVariant *new_owner_variant = g_variant_get_child_value(parameters, 2);
    const gchar *new_owner = g_variant_get_string(new_owner_variant, NULL);

    GList *player_entry = NULL;
    if (strlen(new_owner) == 0 && strlen(previous_owner) != 0) {
        // the name has vanished
        player_entry =
            g_list_find_custom(watcher->priv->player_names, player_id,
                               (GCompareFunc)g_strcmp0);
        if (player_entry != NULL) {
            watcher->priv->player_names =
                g_list_remove_link(watcher->priv->player_names, player_entry);
            watcher_remove_managed_player_by_name(watcher, player_entry->data);
            PlayerctlNameEvent *event = g_slice_new(PlayerctlNameEvent);
            event->name = g_strdup(player_entry->data);
            g_signal_emit(watcher, connection_signals[NAME_VANISHED], 0,
                          event);
            g_list_free_full(player_entry, g_free);
        }
    } else if (strlen(previous_owner) == 0 && strlen(new_owner) != 0) {
        // the name has appeared
        player_entry =
            g_list_find_custom(watcher->priv->player_names, player_id,
                               (GCompareFunc)g_strcmp0);
        if (player_entry == NULL) {
            watcher->priv->player_names = g_list_prepend(watcher->priv->player_names, g_strdup(player_id));
            PlayerctlPlayer *player = NULL;
            PlayerctlNameEvent *event = g_slice_new(PlayerctlNameEvent);
            event->name = g_strdup(player_id);
            g_signal_emit(watcher, connection_signals[NAME_APPEARED], 0,
                          event, &player);
            watcher_add_managed_player(watcher, player);
        }
    }

    g_free(player_id);
    g_variant_unref(name_variant);
    g_variant_unref(previous_owner_variant);
    g_variant_unref(new_owner_variant);
}

static gboolean playerctl_name_watcher_initable_init(GInitable *initable,
                                               GCancellable *cancellable,
                                               GError **error) {
    GError *tmp_error = NULL;
    PlayerctlNameWatcher *watcher = PLAYERCTL_NAME_WATCHER(initable);

    if (watcher->priv->initted) {
        return TRUE;
    }

    watcher->priv->proxy =
        g_dbus_proxy_new_for_bus_sync(watcher->priv->bus_type,
                                      G_DBUS_PROXY_FLAGS_NONE, NULL,
                                      "org.freedesktop.DBus",
                                      "/org/freedesktop/DBus",
                                      "org.freedesktop.DBus", NULL,
                                      &tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(error, tmp_error);
        return FALSE;
    }

    watcher->priv->player_names = playerctl_list_players(&tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(error, tmp_error);
        return FALSE;
    }

    g_signal_connect(G_DBUS_PROXY(watcher->priv->proxy), "g-signal",
                     G_CALLBACK(dbus_name_owner_changed_callback),
                     watcher);

    watcher->priv->initted = TRUE;

    return TRUE;
}

static void playerctl_name_watcher_initable_iface_init(GInitableIface *iface) {
    iface->init = playerctl_name_watcher_initable_init;
}

PlayerctlNameWatcher *playerctl_name_watcher_new(GError **err) {
    GError *tmp_error = NULL;

    PlayerctlNameWatcher *watcher =
        g_initable_new(PLAYERCTL_TYPE_NAME_WATCHER, NULL, &tmp_error, NULL);
    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        return NULL;
    }

    return watcher;
}

PlayerctlNameWatcher *playerctl_name_watcher_new_for_bus(GError **err, GBusType bus_type) {
    GError *tmp_error = NULL;

    PlayerctlNameWatcher *watcher =
        g_initable_new(PLAYERCTL_TYPE_NAME_WATCHER, NULL, &tmp_error,
                       "bus-type", bus_type, NULL);
    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        return NULL;
    }

    return watcher;
}

void playerctl_name_watcher_set_sort_func(PlayerctlNameWatcher *watcher,
                                          GCompareDataFunc sort_func,
                                          gpointer *sort_data,
                                          GDestroyNotify notify) {
    // TODO figure out how to make this work with the bindings
    watcher->priv->sort_func = sort_func;
    watcher->priv->sort_data = sort_data;
    watcher->priv->sort_notify = notify;

    watcher->priv->players =
        g_list_sort_with_data(watcher->priv->players, sort_func, sort_data);
}

void playerctl_name_watcher_move_player_to_top(PlayerctlNameWatcher *watcher,
                                               PlayerctlPlayer *player) {
    GList *l;
    for (l = watcher->priv->players; l != NULL; l = l->next) {
        PlayerctlPlayer *current = PLAYERCTL_PLAYER(l->data);
        if (current == player) {
            watcher->priv->players = g_list_remove_link(watcher->priv->players, l);
            watcher->priv->players = g_list_prepend(watcher->priv->players, l);
            break;
        }
    }
}
