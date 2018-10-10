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
#include <string.h>

#include "playerctl-generated.h"
#include "playerctl-player.h"

#include <stdint.h>

#define MPRIS_PREFIX "org.mpris.MediaPlayer2."

enum {
    PROP_0,

    PROP_PLAYER_NAME,
    PROP_PLAYER_ID,
    PROP_STATUS,
    PROP_VOLUME,
    PROP_METADATA,
    PROP_POSITION,

    PROP_CAN_CONTROL,
    PROP_CAN_PLAY,
    PROP_CAN_PAUSE,
    PROP_CAN_SEEK,
    PROP_CAN_GO_NEXT,
    PROP_CAN_GO_PREVIOUS,

    N_PROPERTIES
};

enum {
    // PROPERTIES_CHANGED,
    PLAY,
    PAUSE,
    STOP,
    METADATA,
    EXIT,
    LAST_SIGNAL
};

static gboolean bus_name_is_valid(gchar *bus_name) {
    if (bus_name == NULL) {
        return FALSE;
    }

    return g_str_has_prefix(bus_name, MPRIS_PREFIX);
}

static GParamSpec *obj_properties[N_PROPERTIES] = {
    NULL,
};

static guint connection_signals[LAST_SIGNAL] = {0};

struct _PlayerctlPlayerPrivate {
    OrgMprisMediaPlayer2Player *proxy;
    gchar *player_name;
    gchar *bus_name;
    GError *init_error;
    gboolean initted;
    GVariant *metadata;
};

static void playerctl_player_properties_changed_callback(
    GDBusProxy *_proxy, GVariant *changed_properties,
    const gchar *const *invalidated_properties, gpointer user_data) {
    PlayerctlPlayer *self = user_data;

    GVariant *metadata =
        g_variant_lookup_value(changed_properties, "Metadata", NULL);
    GVariant *playback_status =
        g_variant_lookup_value(changed_properties, "PlaybackStatus", NULL);

    if (metadata) {
        g_signal_emit(self, connection_signals[METADATA], 0, metadata);
    }

    if (playback_status) {
        const gchar *status_str = g_variant_get_string(playback_status, NULL);

        if (g_strcmp0(status_str, "Playing") == 0) {
            g_signal_emit(self, connection_signals[PLAY], 0);
        } else if (g_strcmp0(status_str, "Paused") == 0) {
            g_signal_emit(self, connection_signals[PAUSE], 0);
        } else if (g_strcmp0(status_str, "Stopped") == 0) {
            g_signal_emit(self, connection_signals[STOP], 0);
        }
    }

    for (int i = 0; invalidated_properties[i] != NULL; i += 1) {
        if (g_strcmp0(invalidated_properties[i], "PlaybackStatus") == 0) {
            g_signal_emit(self, connection_signals[EXIT], 0);
            break;
        }
    }
}

static void playerctl_player_initable_iface_init(GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE(PlayerctlPlayer, playerctl_player, G_TYPE_OBJECT,
                        G_ADD_PRIVATE(PlayerctlPlayer) G_IMPLEMENT_INTERFACE(
                            G_TYPE_INITABLE,
                            playerctl_player_initable_iface_init));

G_DEFINE_QUARK(playerctl-player-error-quark, playerctl_player_error);

static GVariant *playerctl_player_get_metadata(PlayerctlPlayer *self,
                                               GError **err) {
    GVariant *metadata;
    GError *tmp_error = NULL;

    g_main_context_iteration(NULL, FALSE);
    metadata = org_mpris_media_player2_player_dup_metadata(self->priv->proxy);

    if (!metadata) {
        // XXX: Ugly spotify workaround. Spotify does not seem to use the property
        // cache. We have to get the properties directly.
        GVariant *call_reply = g_dbus_proxy_call_sync(
            G_DBUS_PROXY(self->priv->proxy), "org.freedesktop.DBus.Properties.Get",
            g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player", "Metadata"),
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, &tmp_error);

        if (tmp_error != NULL) {
            g_propagate_error(err, tmp_error);
            return NULL;
        }

        GVariant *call_reply_properties = g_variant_get_child_value(call_reply, 0);

        metadata = g_variant_get_child_value(call_reply_properties, 0);

        g_variant_unref(call_reply);
        g_variant_unref(call_reply_properties);
    }

    return metadata;
}

static void playerctl_player_set_property(GObject *object, guint property_id,
                                          const GValue *value,
                                          GParamSpec *pspec) {
    PlayerctlPlayer *self = PLAYERCTL_PLAYER(object);

    switch (property_id) {
    case PROP_PLAYER_NAME:
        g_free(self->priv->player_name);
        self->priv->player_name = g_strdup(g_value_get_string(value));
        break;

    case PROP_VOLUME:
        org_mpris_media_player2_player_set_volume(self->priv->proxy,
                                                  g_value_get_double(value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void playerctl_player_get_property(GObject *object, guint property_id,
                                          GValue *value, GParamSpec *pspec) {
    PlayerctlPlayer *self = PLAYERCTL_PLAYER(object);

    switch (property_id) {
    case PROP_PLAYER_NAME:
        g_value_set_string(value, self->priv->player_name);
        break;

    case PROP_PLAYER_ID:
        if (!bus_name_is_valid(self->priv->bus_name)) {
            return;
        }

        g_value_set_string(value, self->priv->bus_name + strlen(MPRIS_PREFIX));
        break;

    case PROP_STATUS:
        if (self->priv->proxy) {
            g_main_context_iteration(NULL, FALSE);
            g_value_set_string(value,
                               org_mpris_media_player2_player_get_playback_status(
                                   self->priv->proxy));
        } else {
            g_value_set_string(value, "");
        }
        break;

    case PROP_METADATA: {
        GVariant *metadata = NULL;

        if (self->priv->proxy) {
            metadata = playerctl_player_get_metadata(self, NULL);
        }

        if (self->priv->metadata != NULL) {
            g_variant_unref(self->priv->metadata);
        }

        self->priv->metadata = metadata;
        g_value_set_variant(value, metadata);
        break;
    }

    case PROP_VOLUME:
        if (self->priv->proxy) {
            g_main_context_iteration(NULL, FALSE);
            g_value_set_double(value, org_mpris_media_player2_player_get_volume(
                                          self->priv->proxy));
        } else {
            g_value_set_double(value, 0);
        }
        break;

    case PROP_POSITION:
        if (self->priv->proxy) {
            // XXX: the position cache seems to never be updated after the
            // connection is made so we have to call the method directly here or it
            // won't work.
            GError *tmp_error = NULL;
            GVariant *call_reply = g_dbus_proxy_call_sync(
                G_DBUS_PROXY(self->priv->proxy),
                "org.freedesktop.DBus.Properties.Get",
                g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player", "Position"),
                G_DBUS_CALL_FLAGS_NONE, -1, NULL, &tmp_error);

            if (tmp_error) {
                g_warning("Get position property failed. Using cache. (%s)",
                          tmp_error->message);
                gint64 cached_position =
                    org_mpris_media_player2_player_get_position(self->priv->proxy);
                g_value_set_int64(value, cached_position);
                break;
            }

            GVariant *call_reply_properties =
                g_variant_get_child_value(call_reply, 0);
            GVariant *call_reply_unboxed =
                g_variant_get_variant(call_reply_properties);

            gint64 position = g_variant_get_int64(call_reply_unboxed);
            g_value_set_int64(value, position);

            g_variant_unref(call_reply);
            g_variant_unref(call_reply_properties);
            g_variant_unref(call_reply_unboxed);
        } else {
            g_value_set_int64(value, 0);
        }
        break;

    case PROP_CAN_CONTROL:
        if (self->priv->proxy == NULL) {
            g_value_set_boolean(value, FALSE);
            break;
        }
        g_main_context_iteration(NULL, FALSE);
        g_value_set_boolean(value,
                            org_mpris_media_player2_player_get_can_control(self->priv->proxy));
        break;

    case PROP_CAN_PLAY:
        if (self->priv->proxy == NULL) {
            g_value_set_boolean(value, FALSE);
            break;
        }
        g_main_context_iteration(NULL, FALSE);
        g_value_set_boolean(value,
                            org_mpris_media_player2_player_get_can_play(self->priv->proxy));
        break;

    case PROP_CAN_PAUSE:
        if (self->priv->proxy == NULL) {
            g_value_set_boolean(value, FALSE);
            break;
        }
        g_main_context_iteration(NULL, FALSE);
        g_value_set_boolean(value,
                            org_mpris_media_player2_player_get_can_pause(self->priv->proxy));
        break;

    case PROP_CAN_SEEK:
        if (self->priv->proxy == NULL) {
            g_value_set_boolean(value, FALSE);
            break;
        }
        g_main_context_iteration(NULL, FALSE);
        g_value_set_boolean(value,
                            org_mpris_media_player2_player_get_can_seek(self->priv->proxy));
        break;

    case PROP_CAN_GO_NEXT:
        if (self->priv->proxy == NULL) {
            g_value_set_boolean(value, FALSE);
            break;
        }
        g_main_context_iteration(NULL, FALSE);
        g_value_set_boolean(value,
                            org_mpris_media_player2_player_get_can_go_next(self->priv->proxy));
        break;

    case PROP_CAN_GO_PREVIOUS:
        if (self->priv->proxy == NULL) {
            g_value_set_boolean(value, FALSE);
            break;
        }
        g_main_context_iteration(NULL, FALSE);
        g_value_set_boolean(value,
                            org_mpris_media_player2_player_get_can_go_previous(self->priv->proxy));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void playerctl_player_constructed(GObject *gobject) {
    PlayerctlPlayer *self = PLAYERCTL_PLAYER(gobject);

    self->priv->init_error = NULL;

    g_initable_init((GInitable *)self, NULL, &self->priv->init_error);

    G_OBJECT_CLASS(playerctl_player_parent_class)->constructed(gobject);
}

static void playerctl_player_dispose(GObject *gobject) {
    PlayerctlPlayer *self = PLAYERCTL_PLAYER(gobject);

    g_clear_error(&self->priv->init_error);
    g_clear_object(&self->priv->proxy);

    G_OBJECT_CLASS(playerctl_player_parent_class)->dispose(gobject);
}

static void playerctl_player_finalize(GObject *gobject) {
    PlayerctlPlayer *self = PLAYERCTL_PLAYER(gobject);

    g_free(self->priv->player_name);
    g_free(self->priv->bus_name);

    G_OBJECT_CLASS(playerctl_player_parent_class)->finalize(gobject);
}

static void playerctl_player_class_init(PlayerctlPlayerClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->set_property = playerctl_player_set_property;
    gobject_class->get_property = playerctl_player_get_property;
    gobject_class->constructed = playerctl_player_constructed;
    gobject_class->dispose = playerctl_player_dispose;
    gobject_class->finalize = playerctl_player_finalize;

    obj_properties[PROP_PLAYER_NAME] = g_param_spec_string(
        "player-name", "Player name", "The name of the player mpris player",
        NULL, /* default */
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_PLAYER_ID] = g_param_spec_string(
        "player-id", "Player ID", "An ID that identifies this player on the bus",
        NULL, /* default */
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_STATUS] =
        g_param_spec_string("status", "Player status",
                            "The play status of the player", NULL, /* default */
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_VOLUME] = g_param_spec_double(
        "volume", "Player volume", "The volume level of the player", 0, 100, 0,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_POSITION] = g_param_spec_int64(
        "position", "Player position",
        "The position in the current track of the player", 0, INT64_MAX, 0,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_METADATA] = g_param_spec_variant(
        "metadata", "Player metadata",
        "The metadata of the currently playing track", G_VARIANT_TYPE_VARIANT,
        NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_CAN_CONTROL] = g_param_spec_boolean(
        "can-control", "Can control", "Whether the player can be controlled by playerctl", FALSE,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_CAN_PLAY] = g_param_spec_boolean(
        "can-play", "Can play", "Whether the player can start playing", FALSE,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_CAN_PAUSE] = g_param_spec_boolean(
        "can-pause", "Can pause", "Whether the player can pause", FALSE,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_CAN_SEEK] = g_param_spec_boolean(
        "can-seek", "Can seek", "Whether the player can seek", FALSE,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_CAN_GO_NEXT] = g_param_spec_boolean(
        "can-go-next", "Can go next",
        "Whether the player can go to the next track", FALSE,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_CAN_GO_PREVIOUS] = g_param_spec_boolean(
        "can-go-previous", "Can go previous",
        "Whether the player can go to the previous track", FALSE,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(gobject_class, N_PROPERTIES,
                                      obj_properties);

#if 0
  /* not implemented yet */
  connection_signals[PROPERTIES_CHANGED] = g_signal_new(
      "properties-changed",                 /* signal_name */
      PLAYERCTL_TYPE_PLAYER,                /* itype */
      G_SIGNAL_RUN_LAST,                    /* signal_flags */
      0,                                    /* class_offset */
      NULL,                                 /* accumulator */
      NULL,                                 /* accu_data */
      g_cclosure_marshal_VOID__VARIANT,     /* c_marshaller */
      G_TYPE_NONE,                          /* return_type */
      1,                                    /* n_params */
      G_TYPE_VARIANT);
#endif

    connection_signals[PLAY] =
        g_signal_new("play",                        /* signal_name */
                     PLAYERCTL_TYPE_PLAYER,         /* itype */
                     G_SIGNAL_RUN_FIRST,            /* signal_flags */
                     0,                             /* class_offset */
                     NULL,                          /* accumulator */
                     NULL,                          /* accu_data */
                     g_cclosure_marshal_VOID__VOID, /* c_marshaller */
                     G_TYPE_NONE,                   /* return_type */
                     0);                            /* n_params */

    connection_signals[PAUSE] =
        g_signal_new("pause",                       /* signal_name */
                     PLAYERCTL_TYPE_PLAYER,         /* itype */
                     G_SIGNAL_RUN_FIRST,            /* signal_flags */
                     0,                             /* class_offset */
                     NULL,                          /* accumulator */
                     NULL,                          /* accu_data */
                     g_cclosure_marshal_VOID__VOID, /* c_marshaller */
                     G_TYPE_NONE,                   /* return_type */
                     0);                            /* n_params */

    connection_signals[STOP] =
        g_signal_new("stop",                        /* signal_name */
                     PLAYERCTL_TYPE_PLAYER,         /* itype */
                     G_SIGNAL_RUN_FIRST,            /* signal_flags */
                     0,                             /* class_offset */
                     NULL,                          /* accumulator */
                     NULL,                          /* accu_data */
                     g_cclosure_marshal_VOID__VOID, /* c_marshaller */
                     G_TYPE_NONE,                   /* return_type */
                     0);                            /* n_params */

    connection_signals[METADATA] =
        g_signal_new("metadata",                       /* signal_name */
                     PLAYERCTL_TYPE_PLAYER,            /* itype */
                     G_SIGNAL_RUN_FIRST,               /* signal_flags */
                     0,                                /* class_offset */
                     NULL,                             /* accumulator */
                     NULL,                             /* accu_data */
                     g_cclosure_marshal_VOID__VARIANT, /* c_marshaller */
                     G_TYPE_NONE,                      /* return_type */
                     1,                                /* n_params */
                     G_TYPE_VARIANT);

    connection_signals[EXIT] =
        g_signal_new("exit",                        /* signal_name */
                     PLAYERCTL_TYPE_PLAYER,         /* itype */
                     G_SIGNAL_RUN_FIRST,            /* signal_flags */
                     0,                             /* class_offset */
                     NULL,                          /* accumulator */
                     NULL,                          /* accu_data */
                     g_cclosure_marshal_VOID__VOID, /* c_marshaller */
                     G_TYPE_NONE,                   /* return_type */
                     0);                            /* n_params */
}

static void playerctl_player_init(PlayerctlPlayer *self) {
    self->priv = playerctl_player_get_instance_private(self);
}

static GList *list_player_names_on_bus(GBusType bus_type, GError **err) {
    GError *tmp_error = NULL;
    GList *players = NULL;

    GDBusProxy *proxy = g_dbus_proxy_new_for_bus_sync(
        bus_type, G_DBUS_PROXY_FLAGS_NONE, NULL, "org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", NULL, &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        return NULL;
    }

    GVariant *reply = g_dbus_proxy_call_sync(
        proxy, "ListNames", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        g_object_unref(proxy);
        return NULL;
    }

    GVariant *reply_child = g_variant_get_child_value(reply, 0);
    gsize reply_count;
    const gchar **names = g_variant_get_strv(reply_child, &reply_count);

    size_t offset = strlen(MPRIS_PREFIX);
    for (int i = 0; i < reply_count; i += 1) {
        if (g_str_has_prefix(names[i], MPRIS_PREFIX)) {
            players = g_list_append(players, g_strdup(names[i] + offset));
        }
    }

    g_object_unref(proxy);
    g_variant_unref(reply);
    g_variant_unref(reply_child);
    g_free(names);

    return players;
}

static gint player_name_instance_compare(gchar *instance, gchar *name) {
    gboolean exact_match = (g_strcmp0(name, instance) == 0);
    gboolean instance_match = !exact_match && (g_str_has_prefix(instance, name) &&
            g_str_has_prefix(instance + strlen(name), ".instance"));

    if (exact_match || instance_match) {
        return 0;
    } else {
        return 1;
    }
}

/*
 * Get the matching bus name for this player name. Bus name will be like:
 * "org.mpris.MediaPlayer2.{PLAYER_NAME}[.instance{NUM}]"
 * Pass a NULL player_name to get the first name on the bus
 * Returns NULL if no matching bus name is found on the bus.
 * Returns an error if there was a problem listing the names on the bus.
 */
static gchar *bus_name_for_player_name(gchar *player_name, GBusType bus_type, GError **err) {
    gchar *bus_name = NULL;
    GError *tmp_error = NULL;

    g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

    GList *names = list_player_names_on_bus(bus_type, &tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        return NULL;
    }

    if (names == NULL) {
        return NULL;
    }

    if (player_name == NULL) {
        gchar *name = names->data;
        bus_name = g_strdup_printf(MPRIS_PREFIX "%s", name);
        g_list_free_full(names, g_free);
        return bus_name;
    }

    GList *exact_match =
        g_list_find_custom(names, player_name, (GCompareFunc)g_strcmp0);
    if (exact_match != NULL) {
        gchar *name = exact_match->data;
        bus_name = g_strdup_printf(MPRIS_PREFIX "%s", name);
        g_list_free_full(names, g_free);
        return bus_name;
    }

    GList *instance_match =
        g_list_find_custom(names, player_name, (GCompareFunc)player_name_instance_compare);
    if (instance_match != NULL) {
        gchar *name = instance_match->data;
        bus_name = g_strdup_printf(MPRIS_PREFIX "%s", name);
        g_list_free_full(names, g_free);
        return bus_name;
    }

    return NULL;
}

static gboolean playerctl_player_initable_init(GInitable *initable,
                                               GCancellable *cancellable,
                                               GError **err) {
    GError *tmp_error = NULL;
    PlayerctlPlayer *player = PLAYERCTL_PLAYER(initable);
    GBusType bus_type = G_BUS_TYPE_SESSION;

    if (player->priv->initted) {
        return TRUE;
    }

    g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

    player->priv->bus_name =
        bus_name_for_player_name(player->priv->player_name, bus_type,
                                 &tmp_error);
    if (tmp_error) {
        g_propagate_error(err, tmp_error);
        return FALSE;
    }

    if (player->priv->bus_name == NULL) {
        bus_type = G_BUS_TYPE_SYSTEM;
        player->priv->bus_name =
            bus_name_for_player_name(player->priv->player_name,
                                     bus_type, &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(err, tmp_error);
            return FALSE;
        }
    }

    if (player->priv->bus_name == NULL) {
        g_set_error(err, playerctl_player_error_quark(), 1,
                    "Player not found");
        return FALSE;
    }

    if (player->priv->player_name == NULL) {
        /* org.mpris.MediaPlayer2.{NAME}[.instance{NUM}] */
        int offset = strlen(MPRIS_PREFIX);
        gchar **split = g_strsplit(player->priv->bus_name + offset, ".instance", 1);
        player->priv->player_name = g_strdup(split[0]);
        g_strfreev(split);
    }

    player->priv->proxy = org_mpris_media_player2_player_proxy_new_for_bus_sync(
        bus_type, G_DBUS_PROXY_FLAGS_NONE, player->priv->bus_name,
        "/org/mpris/MediaPlayer2", NULL, &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        return FALSE;
    }

    g_signal_connect(player->priv->proxy, "g-properties-changed",
                     G_CALLBACK(playerctl_player_properties_changed_callback),
                     player);

    player->priv->initted = TRUE;
    return TRUE;
}

static void playerctl_player_initable_iface_init(GInitableIface *iface) {
    iface->init = playerctl_player_initable_init;
}

/**
 * playerctl_list_players:
 * @err: The location of a GError or NULL
 *
 * Lists all the players that can be controlled by Playerctl.
 *
 * Returns:(transfer full) (element-type utf8): A list of player names.
 */
GList *playerctl_list_players(GError **err) {
    GError *tmp_error = NULL;

    g_return_val_if_fail(err == NULL || *err == NULL, NULL);

    GList *session_players = list_player_names_on_bus(G_BUS_TYPE_SESSION, &tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        return NULL;
    }

    GList *system_players = list_player_names_on_bus(G_BUS_TYPE_SYSTEM, &tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        return NULL;
    }

    if (system_players) {
        GList *next = system_players;
        while (next) {
            gchar *player = next->data;
            if (g_list_find_custom(session_players, player, (GCompareFunc)g_strcmp0)) {
                g_free(player);
            } else {
                session_players = g_list_append(session_players, player);
            }
            next = next->next;
        }
        g_list_free(system_players);
    }

    return session_players;
}

/**
 * playerctl_player_new:
 * @name: (allow-none): The name to use to find the bus name of the player
 * @err: The location of a GError or NULL
 *
 * Allocates a new #PlayerctlPlayer and tries to connect to the bus name
 * "org.mpris.MediaPlayer2.[name]"
 *
 * Returns:(transfer full): A new #PlayerctlPlayer connected to the bus name or
 * NULL if an error occurred
 */
PlayerctlPlayer *playerctl_player_new(const gchar *name, GError **err) {
    GError *tmp_error = NULL;
    PlayerctlPlayer *player;

    player = g_initable_new(PLAYERCTL_TYPE_PLAYER, NULL, &tmp_error,
                            "player-name", name, NULL);

    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        return NULL;
    }

    return player;
}

/**
 * playerctl_player_on:
 * @self: a #PlayerctlPlayer
 * @event: the event to subscribe to
 * @callback: the callback to run on the event
 *
 * A convenience function for bindings to subscribe to an event with a callback
 *
 * Returns: (transfer none): the #PlayerctlPlayer for chaining
 */
PlayerctlPlayer *playerctl_player_on(PlayerctlPlayer *self, const gchar *event,
                                     GClosure *callback, GError **err) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(event != NULL, NULL);
    g_return_val_if_fail(callback != NULL, NULL);
    g_return_val_if_fail(err == NULL || *err == NULL, NULL);

    if (self->priv->init_error != NULL) {
        g_propagate_error(err, g_error_copy(self->priv->init_error));
        return self;
    }

    g_closure_ref(callback);
    g_closure_sink(callback);

    g_signal_connect_closure(self, event, callback, TRUE);

    return self;
}

#define PLAYER_COMMAND_FUNC(COMMAND)                                        \
    GError *tmp_error = NULL;                                               \
                                                                            \
    g_return_val_if_fail(self != NULL, NULL);                               \
    g_return_val_if_fail(err == NULL || *err == NULL, NULL);                \
                                                                            \
    if (self->priv->init_error != NULL) {                                   \
        g_propagate_error(err, g_error_copy(self->priv->init_error));       \
        return self;                                                        \
    }                                                                       \
                                                                            \
    org_mpris_media_player2_player_call_##COMMAND##_sync(self->priv->proxy, \
                                                         NULL, &tmp_error); \
                                                                            \
    if (tmp_error != NULL) {                                                \
        g_propagate_error(err, tmp_error);                                  \
    }                                                                       \
                                                                            \
    return self;

/**
 * playerctl_player_play_pause:
 * @self: a #PlayerctlPlayer
 * @err (allow-none): the location of a GError or NULL
 *
 * Command the player to play if it is paused or pause if it is playing
 *
 * Returns: (transfer none): the #PlayerctlPlayer for chaining
 */
PlayerctlPlayer *playerctl_player_play_pause(PlayerctlPlayer *self,
                                             GError **err) {
    PLAYER_COMMAND_FUNC(play_pause);
}

/**
 * playerctl_player_open:
 * @self: a #PlayerctlPlayer
 * @err (allow-none): the location of a GError or NULL
 *
 * Command the player to open given URI
 *
 * Returns: (transfer none): the #PlayerctlPlayer for chaining
 */
PlayerctlPlayer *playerctl_player_open(PlayerctlPlayer *self, gchar *uri,
                                       GError **err) {
    GError *tmp_error = NULL;

    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(err == NULL || *err == NULL, NULL);

    if (self->priv->init_error != NULL) {
        g_propagate_error(err, g_error_copy(self->priv->init_error));
        return self;
    }
    org_mpris_media_player2_player_call_open_uri_sync(self->priv->proxy, uri,
                                                      NULL, &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        return self;
    }

    return self;
}

/**
 * playerctl_player_play:
 * @self: a #PlayerctlPlayer
 * @err (allow-none): the location of a GError or NULL
 *
 * Command the player to play
 *
 * Returns: (transfer none): the #PlayerctlPlayer for chaining
 */
PlayerctlPlayer *playerctl_player_play(PlayerctlPlayer *self, GError **err) {
    PLAYER_COMMAND_FUNC(play);
}

/**
 * playerctl_player_pause:
 * @self: a #PlayerctlPlayer
 * @err (allow-none): the location of a GError or NULL
 *
 * Command the player to pause
 *
 * Returns: (transfer none): the #PlayerctlPlayer for chaining
 */
PlayerctlPlayer *playerctl_player_pause(PlayerctlPlayer *self, GError **err) {
    PLAYER_COMMAND_FUNC(pause);
}

/**
 * playerctl_player_stop:
 * @self: a #PlayerctlPlayer
 * @err (allow-none): the location of a GError or NULL
 *
 * Command the player to stop
 *
 * Returns: (transfer none): the #PlayerctlPlayer for chaining
 */
PlayerctlPlayer *playerctl_player_stop(PlayerctlPlayer *self, GError **err) {
    PLAYER_COMMAND_FUNC(stop);
}

/**
 * playerctl_player_seek:
 * @self: a #PlayerctlPlayer
 * @err (allow-none): the location of a GError or NULL
 *
 * Command the player to seek
 *
 * Returns: (transfer none): the #PlayerctlPlayer for chaining
 */
PlayerctlPlayer *playerctl_player_seek(PlayerctlPlayer *self, gint64 offset,
                                       GError **err) {
    GError *tmp_error = NULL;

    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(err == NULL || *err == NULL, NULL);

    if (self->priv->init_error != NULL) {
        g_propagate_error(err, g_error_copy(self->priv->init_error));
        return self;
    }

    org_mpris_media_player2_player_call_seek_sync(self->priv->proxy, offset, NULL,
                                                  &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        return self;
    }

    return self;
}

/**
 * playerctl_player_next:
 * @self: a #PlayerctlPlayer
 * @err: (allow-none): the location of a GError or NULL
 *
 * Command the player to go to the next track
 *
 * Returns: (transfer none): the #PlayerctlPlayer for chaining
 */
PlayerctlPlayer *playerctl_player_next(PlayerctlPlayer *self, GError **err) {
    PLAYER_COMMAND_FUNC(next);
}

/**
 * playerctl_player_previous:
 * @self: a #PlayerctlPlayer
 * @err: (allow-none): the location of a GError or NULL
 *
 * Command the player to go to the previous track
 *
 * Returns: (transfer none): the #PlayerctlPlayer for chaining
 */
PlayerctlPlayer *playerctl_player_previous(PlayerctlPlayer *self,
                                           GError **err) {
    PLAYER_COMMAND_FUNC(previous);
}

static gchar *print_gvariant(GVariant *variant) {
    GString *prop = g_string_new("");
    if (g_variant_type_equal(g_variant_get_type(variant), G_VARIANT_TYPE_STRING_ARRAY)) {
        gsize prop_count;
        const gchar **prop_strv = g_variant_get_strv(variant, &prop_count);

        for (int i = 0; i < prop_count; i += 1) {
            g_string_append(prop, prop_strv[i]);

            if (i != prop_count - 1) {
                g_string_append(prop, ", ");
            }
        }

        g_free(prop_strv);
    } else if (g_variant_is_of_type(variant, G_VARIANT_TYPE_STRING)) {
        g_string_append(prop, g_variant_get_string(variant, NULL));
    } else if (g_variant_is_of_type(variant, G_VARIANT_TYPE_VARIANT)) {
        int len = g_variant_n_children(variant);
        for (int i = 0; i < len; ++i) {
            GVariant *child_value = g_variant_get_child_value(variant, i);
            gchar *child_value_str = print_gvariant(child_value);
            g_string_append(prop, child_value_str);
            g_free(child_value_str);
            if (i != len - 1) {
                g_string_append(prop, ", ");
            }
        }
    } else {
        prop = g_variant_print_string(variant, prop, FALSE);
    }

    return g_string_free(prop, FALSE);
}

static gchar *print_metadata_table(GVariant *metadata, gchar *player_name) {
    GVariantIter iter;
    GVariant *child;
    GString *table = g_string_new("");
	const gchar *fmt = "%-5s %-25s %s\n";

    if (g_strcmp0(g_variant_get_type_string(metadata), "a{sv}") != 0) {
        return NULL;
    }

    g_variant_iter_init (&iter, metadata);
    while ((child = g_variant_iter_next_value(&iter))) {
        GVariant *key_variant = g_variant_get_child_value(child, 0);
        const gchar *key = g_variant_get_string(key_variant, 0);
        GVariant *value_variant = g_variant_lookup_value(metadata, key, NULL);

		if (g_variant_is_container(value_variant)) {
			// only go depth 1
			int len = g_variant_n_children(value_variant);
			for (int i = 0; i < len; ++i) {
				GVariant *child_value = g_variant_get_child_value(value_variant, i);
				gchar *child_value_str = print_gvariant(child_value);
				g_string_append_printf(table, fmt, player_name, key, child_value_str);
				g_free(child_value_str);
				g_variant_unref(child_value);
			}
		} else {
			gchar *value = print_gvariant(value_variant);
			g_string_append_printf(table, fmt, player_name, key, value);
			g_free(value);
		}

        g_variant_unref(child);
        g_variant_unref(key_variant);
        g_variant_unref(value_variant);
    }

    if (table->len == 0) {
        g_string_free(table, TRUE);
        return NULL;
    }
    // cut off the last newline
    table = g_string_truncate(table, table->len - 1);

    return g_string_free(table, FALSE);
}

/**
 * playerctl_player_print_metadata_prop:
 * @self: a #PlayerctlPlayer
 * @property: (allow-none): the property from the metadata to print
 * @err: (allow-none): the location of a GError or NULL
 *
 * Gets the given property from the metadata of the current track. If property
 * is null, prints all the metadata properties. Returns NULL string if no
 * track is playing.
 *
 * Returns: (transfer full): The artist from the metadata of the current track
 */
gchar *playerctl_player_print_metadata_prop(PlayerctlPlayer *self,
                                            const gchar *property,
                                            GError **err) {
    GError *tmp_error = NULL;

    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(err == NULL || *err == NULL, NULL);

    if (self->priv->init_error != NULL) {
        g_propagate_error(err, g_error_copy(self->priv->init_error));
        return NULL;
    }

    GVariant *metadata = playerctl_player_get_metadata(self, &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        return NULL;
    }

    if (!metadata) {
        return NULL;
    }

    if (!property) {
        gchar *res = print_metadata_table(metadata, self->priv->player_name);
        g_variant_unref(metadata);
        return res;
    }

    GVariant *prop_variant = g_variant_lookup_value(metadata, property, NULL);
    g_variant_unref(metadata);

    if (!prop_variant) {
        return NULL;
    }

    gchar *prop = print_gvariant(prop_variant);
    g_variant_unref(prop_variant);
    return prop;
}

/**
 * playerctl_player_get_artist:
 * @self: a #PlayerctlPlayer
 * @err: (allow-none): the location of a GError or NULL
 *
 * Gets the artist from the metadata of the current track, or NULL if no
 * track is playing.
 *
 * Returns: (transfer full): The artist from the metadata of the current track
 */
gchar *playerctl_player_get_artist(PlayerctlPlayer *self, GError **err) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(err == NULL || *err == NULL, NULL);

    if (self->priv->init_error != NULL) {
        g_propagate_error(err, g_error_copy(self->priv->init_error));
        return NULL;
    }

    return playerctl_player_print_metadata_prop(self, "xesam:artist", NULL);
}

/**
 * playerctl_player_get_title:
 * @self: a #PlayerctlPlayer
 * @err: (allow-none): the location of a GError or NULL
 *
 * Gets the title from the metadata of the current track, or NULL if
 * no track is playing.
 *
 * Returns: (transfer full): The title from the metadata of the current track
 */
gchar *playerctl_player_get_title(PlayerctlPlayer *self, GError **err) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(err == NULL || *err == NULL, NULL);

    if (self->priv->init_error != NULL) {
        g_propagate_error(err, g_error_copy(self->priv->init_error));
        return NULL;
    }

    return playerctl_player_print_metadata_prop(self, "xesam:title", NULL);
}

/**
 * playerctl_player_get_album:
 * @self: a #PlayerctlPlayer
 * @err: (allow-none): the location of a GError or NULL
 *
 * Gets the album from the metadata of the current track, or NULL if
 * no track is playing.
 *
 * Returns: (transfer full): The album from the metadata of the current track
 */
gchar *playerctl_player_get_album(PlayerctlPlayer *self, GError **err) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(err == NULL || *err == NULL, NULL);

    if (self->priv->init_error != NULL) {
        g_propagate_error(err, g_error_copy(self->priv->init_error));
        return NULL;
    }

    return playerctl_player_print_metadata_prop(self, "xesam:album", NULL);
}

/**
 * playerctl_player_set_position
 * @self: a #PlayerctlPlayer
 * @err: (allow-none): the location of a GError or NULL
 *
 * Sets the position of the current track to the given position in microseconds.
 */
void playerctl_player_set_position(PlayerctlPlayer *self, gint64 position,
                                   GError **err) {
    GError *tmp_error = NULL;

    g_return_if_fail(self != NULL);
    g_return_if_fail(err == NULL || *err == NULL);

    if (self->priv->init_error != NULL) {
        g_propagate_error(err, g_error_copy(self->priv->init_error));
    }

    // calling the function requires the track id
    GVariant *metadata = playerctl_player_get_metadata(self, &tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
        return;
    }

    GVariant *track_id_variant = g_variant_lookup_value(
        metadata, "mpris:trackid", G_VARIANT_TYPE_OBJECT_PATH);

    if (track_id_variant == NULL) {
        // XXX some players set this as a string, which is against the protocol,
        // but a lot of them do it and I don't feel like fixing it on all the
        // players in the world.
        track_id_variant = g_variant_lookup_value(metadata, "mpris:trackid",
                                                  G_VARIANT_TYPE_STRING);
    }

    g_variant_unref(metadata);
    if (track_id_variant == NULL) {
        tmp_error = g_error_new(playerctl_player_error_quark(), 1,
                                "Could not get track id to set position");
        g_propagate_error(err, tmp_error);
        return;
    }

    const gchar *track_id = g_variant_get_string(track_id_variant, NULL);

    org_mpris_media_player2_player_call_set_position_sync(
        self->priv->proxy, track_id, position, NULL, &tmp_error);
    g_variant_unref(track_id_variant);
    if (tmp_error != NULL) {
        g_propagate_error(err, tmp_error);
    }
}
