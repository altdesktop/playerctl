#include <assert.h>
#include <gio/gio.h>
#include <stdbool.h>
#include <stdio.h>

#define MPRIS_PATH "/org/mpris/MediaPlayer2"
#define DBUS_NAME "org.freedesktop.DBus"
#define DBUS_INTERFACE "org.freedesktop.DBus"
#define DBUS_PATH "/org/freedesktop/DBus"
#define PLAYER_INTERFACE "org.mpris.MediaPlayer2.Player"
#define ROOT_INTERFACE "org.mpris.MediaPlayer2"
#define PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"
#define PLAYERCTLD_INTERFACE "com.github.altdesktop.playerctld"

struct Player {
    char *unique;
    char *well_known;
    gint64 position;
    GVariant *player_properties;
    GVariant *root_properties;
};

struct PlayerctldContext {
    GMainLoop *loop;
    gint bus_id;
    GDBusConnection *connection;
    GDBusInterfaceInfo *root_interface_info;
    GDBusInterfaceInfo *player_interface_info;
    GDBusInterfaceInfo *playerctld_interface_info;
    GQueue *players;
    GQueue *pending_players;
    struct Player *pending_active;
};

static struct Player *player_new(const char *unique, const char *well_known) {
    struct Player *player = calloc(1, sizeof(struct Player));
    player->unique = g_strdup(unique);
    player->well_known = g_strdup(well_known);
    return player;
}

static void player_set_unique_name(struct Player *player, const char *unique) {
    g_free(player->unique);
    player->unique = g_strdup(unique);
}

static void player_free(struct Player *name) {
    if (name == NULL) {
        return;
    }
    if (name->player_properties != NULL) {
        g_variant_unref(name->player_properties);
    }
    if (name->root_properties != NULL) {
        g_variant_unref(name->root_properties);
    }
    g_free(name->unique);
    g_free(name->well_known);
    free(name);
}

static gint player_compare(gconstpointer a, gconstpointer b) {
    struct Player *fn_a = (struct Player *)a;
    struct Player *fn_b = (struct Player *)b;
    if (fn_a->unique != NULL && fn_b->unique != NULL && strcmp(fn_a->unique, fn_b->unique) != 0) {
        return 1;
    }
    if (fn_a->well_known != NULL && fn_b->well_known != NULL &&
        strcmp(fn_a->well_known, fn_b->well_known) != 0) {
        return 1;
    }

    return 0;
}

static void player_update_properties(struct Player *player, const char *interface_name,
                                     GVariant *properties) {
    GVariantDict cached_properties;
    GVariantIter iter;
    GVariant *child;
    bool is_player_interface = false;  // otherwise, the root interface

    if (g_strcmp0(interface_name, PLAYER_INTERFACE) == 0) {
        g_variant_dict_init(&cached_properties, player->player_properties);
        is_player_interface = true;
    } else if (g_strcmp0(interface_name, ROOT_INTERFACE) == 0) {
        g_variant_dict_init(&cached_properties, player->root_properties);
    } else {
        g_error("cannot update properties for unknown interface: %s", interface_name);
        assert(false);
    }
    if (!g_variant_is_of_type(properties, G_VARIANT_TYPE_VARDICT)) {
        g_error("cannot update properties with unknown type: %s",
                g_variant_get_type_string(properties));
        assert(false);
    }
    g_variant_iter_init(&iter, properties);
    while ((child = g_variant_iter_next_value(&iter))) {
        GVariant *key_variant = g_variant_get_child_value(child, 0);
        const gchar *key = g_variant_get_string(key_variant, 0);
        GVariant *prop_variant = g_variant_get_child_value(child, 1);
        GVariant *prop_value = g_variant_get_variant(prop_variant);
        // printf("key=%s, value=%s\n", key, g_variant_print(prop_value, TRUE));
        if (is_player_interface && g_strcmp0(key, "Position") == 0) {
            // gets cached separately
            player->position = g_variant_get_int64(prop_value);
            goto loop_out;
        }
        GVariant *cache_value = g_variant_dict_lookup_value(&cached_properties, key, NULL);
        if (cache_value != NULL) {
            g_variant_unref(cache_value);
        }
        g_variant_dict_insert_value(&cached_properties, key, prop_value);
    loop_out:
        g_variant_unref(prop_value);
        g_variant_unref(prop_variant);
        g_variant_unref(key_variant);
        g_variant_unref(child);
    }

    if (is_player_interface) {
        if (player->player_properties != NULL) {
            g_variant_unref(player->player_properties);
        }
        player->player_properties = g_variant_ref_sink(g_variant_dict_end(&cached_properties));
    } else {
        if (player->root_properties != NULL) {
            g_variant_unref(player->root_properties);
        }
        player->root_properties = g_variant_ref_sink(g_variant_dict_end(&cached_properties));
    }
}

static void player_update_position_sync(struct Player *player, struct PlayerctldContext *ctx,
                                        GError **error) {
    GError *tmp_error = NULL;
    g_return_if_fail(error == NULL || *error == NULL);
    g_debug("updating position for player unique='%s', well_known='%s'", player->unique,
            player->well_known);
    GVariant *reply = g_dbus_connection_call_sync(
        ctx->connection, player->unique, MPRIS_PATH, PROPERTIES_INTERFACE, "Get",
        g_variant_new("(ss)", PLAYER_INTERFACE, "Position"), NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START,
        -1, NULL, &tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(error, tmp_error);
        return;
    }
    GVariant *position_unwrapped = g_variant_get_child_value(reply, 0);
    GVariant *position_variant = g_variant_get_variant(position_unwrapped);
    player->position = g_variant_get_int64(position_variant);
    g_debug("new position: %ld", player->position);
    g_variant_unref(position_variant);
    g_variant_unref(position_unwrapped);
    g_variant_unref(reply);
}

static GVariant *context_player_names_to_gvariant(struct PlayerctldContext *ctx) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_STRING_ARRAY);
    guint len = g_queue_get_length(ctx->players);
    for (int i = 0; i < len; ++i) {
        struct Player *current = g_queue_pop_head(ctx->players);
        g_variant_builder_add_value(&builder, g_variant_new_string(current->well_known));
        g_queue_push_tail(ctx->players, current);
    }

    return g_variant_builder_end(&builder);
}

static void context_emit_active_player_changed(struct PlayerctldContext *ctx, GError **error) {
    GError *tmp_error = NULL;
    g_return_if_fail(error == NULL || *error == NULL);

    struct Player *player = g_queue_peek_head(ctx->players);

    g_dbus_connection_emit_signal(
        ctx->connection, NULL, MPRIS_PATH, PLAYERCTLD_INTERFACE, "ActivePlayerChangeBegin",
        g_variant_new("(s)", (player != NULL ? player->well_known : "")), &tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(error, tmp_error);
        return;
    }

    if (player != NULL) {
        g_debug("emitting signals for new active player: '%s'", player->well_known);
        GVariant *player_children[3] = {
            g_variant_new_string(PLAYER_INTERFACE),
            player->player_properties,
            g_variant_new_array(G_VARIANT_TYPE_STRING, NULL, 0),
        };
        GVariant *player_properties_tuple = g_variant_new_tuple(player_children, 3);

        g_dbus_connection_emit_signal(ctx->connection, NULL, MPRIS_PATH, PROPERTIES_INTERFACE,
                                      "PropertiesChanged", player_properties_tuple, &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return;
        }

        GVariant *root_children[3] = {
            g_variant_new_string(PLAYER_INTERFACE),
            player->root_properties,
            g_variant_new_array(G_VARIANT_TYPE_STRING, NULL, 0),
        };
        GVariant *root_properties_tuple = g_variant_new_tuple(root_children, 3);
        g_dbus_connection_emit_signal(ctx->connection, NULL, MPRIS_PATH, PROPERTIES_INTERFACE,
                                      "PropertiesChanged", root_properties_tuple, &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return;
        }
        g_debug("sending Seeked signal with position %ld", player->position);
        g_dbus_connection_emit_signal(ctx->connection, NULL, MPRIS_PATH, PLAYER_INTERFACE, "Seeked",
                                      g_variant_new("(x)", player->position), &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return;
        }
    } else {
        g_debug("emitting invalidated property signals, no active player");
        const gchar *const player_properties[] = {
            "CanControl", "CanGoNext", "CanGoPrevious", "CanPause",    "CanPlay",
            "CanSeek",    "Shuffle",   "Metadata",      "MaximumRate", "MinimumRate",
            "Rate",       "Volume",    "Position",      "LoopStatus",  "PlaybackStatus"};
        const gchar *const root_properties[] = {
            "SupportedMimeTypes", "SupportedUriSchemes", "CanQuit",      "CanRaise",
            "CanSetFullScreen",   "HasTrackList",        "DesktopEntry", "Identity"};
        GVariant *player_invalidated = g_variant_new_strv(
            player_properties, sizeof(player_properties) / sizeof(player_properties[0]));
        GVariant *root_invalidated = g_variant_new_strv(
            root_properties, sizeof(root_properties) / sizeof(root_properties[0]));
        GVariant *player_children[3] = {
            g_variant_new_string(PLAYER_INTERFACE),
            g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0),
            player_invalidated,
        };
        GVariant *root_children[3] = {
            g_variant_new_string(ROOT_INTERFACE),
            g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0),
            root_invalidated,
        };
        GVariant *player_invalidated_tuple = g_variant_new_tuple(player_children, 3);
        GVariant *root_invalidated_tuple = g_variant_new_tuple(root_children, 3);

        g_dbus_connection_emit_signal(ctx->connection, NULL, MPRIS_PATH, PROPERTIES_INTERFACE,
                                      "PropertiesChanged", player_invalidated_tuple, &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return;
        }

        g_dbus_connection_emit_signal(ctx->connection, NULL, MPRIS_PATH, PROPERTIES_INTERFACE,
                                      "PropertiesChanged", root_invalidated_tuple, &tmp_error);
        if (tmp_error != NULL) {
            g_propagate_error(error, tmp_error);
            return;
        }
    }

    GVariantDict dict;
    g_variant_dict_init(&dict, NULL);
    g_variant_dict_insert_value(&dict, "PlayerNames", context_player_names_to_gvariant(ctx));

    GVariant *playerctld_children[3] = {
        g_variant_new_string(PLAYERCTLD_INTERFACE),
        g_variant_dict_end(&dict),
        g_variant_new_array(G_VARIANT_TYPE_STRING, NULL, 0),
    };
    GVariant *playerctld_properties = g_variant_new_tuple(playerctld_children, 3);
    g_dbus_connection_emit_signal(ctx->connection, NULL, MPRIS_PATH, PROPERTIES_INTERFACE,
                                  "PropertiesChanged", playerctld_properties, &tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(error, tmp_error);
        return;
    }
    g_dbus_connection_emit_signal(
        ctx->connection, NULL, MPRIS_PATH, PLAYERCTLD_INTERFACE, "ActivePlayerChangeEnd",
        g_variant_new("(s)", (player != NULL ? player->well_known : "")), &tmp_error);
    if (tmp_error != NULL) {
        g_propagate_error(error, tmp_error);
        return;
    }
}

static struct Player *context_find_player(struct PlayerctldContext *ctx, const char *unique,
                                          const char *well_known) {
    const struct Player find_name = {
        .unique = (char *)unique,
        .well_known = (char *)well_known,
    };
    GList *found = g_queue_find_custom(ctx->players, &find_name, player_compare);
    if (found != NULL) {
        return (struct Player *)found->data;
    }

    found = g_queue_find_custom(ctx->pending_players, &find_name, player_compare);
    if (found != NULL) {
        return (struct Player *)found->data;
    }

    return NULL;
}

static struct Player *context_get_active_player(struct PlayerctldContext *ctx) {
    return g_queue_peek_head(ctx->players);
}

static void context_set_active_player(struct PlayerctldContext *ctx, struct Player *player) {
    g_queue_remove(ctx->players, player);
    g_queue_remove(ctx->pending_players, player);
    g_queue_push_head(ctx->players, player);
    ctx->pending_active = NULL;
}

static void context_add_player(struct PlayerctldContext *ctx, struct Player *player) {
    g_queue_remove(ctx->players, player);
    g_queue_remove(ctx->pending_players, player);
    g_queue_push_tail(ctx->players, player);
}

static void context_add_pending_player(struct PlayerctldContext *ctx, struct Player *player) {
    g_queue_remove(ctx->players, player);
    g_queue_remove(ctx->pending_players, player);
    g_queue_push_tail(ctx->pending_players, player);
}

static void context_remove_player(struct PlayerctldContext *ctx, struct Player *player) {
    g_queue_remove(ctx->players, player);
    g_queue_remove(ctx->pending_players, player);
    if (ctx->pending_active == player) {
        ctx->pending_active = NULL;
    }
}

/**
 * Returns the newly activated player
 */
static struct Player *context_shift_active_player(struct PlayerctldContext *ctx) {
    GError *error = NULL;
    struct Player *previous, *current;

    if (!(previous = current = context_get_active_player(ctx))) {
        return NULL;
    }
    context_remove_player(ctx, previous);
    context_add_player(ctx, previous);
    if ((current = context_get_active_player(ctx)) != previous) {
        player_update_position_sync(current, ctx, &error);
        if (error != NULL) {
            g_warning("could not update player position: %s", error->message);
            g_clear_error(&error);
        }
        context_emit_active_player_changed(ctx, &error);
        if (error != NULL) {
            g_warning("could not emit active player change: %s", error->message);
            g_clear_error(&error);
        }
    }
    return current;
}

static const char *playerctld_introspection_xml =
    "<node>\n"
    "  <interface name=\"com.github.altdesktop.playerctld\">\n"
    "    <method name=\"Shift\">\n"
    "        <arg name=\"Player\" type=\"s\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <property name=\"PlayerNames\" type=\"as\" access=\"read\"/>\n"
    "    <signal name=\"ActivePlayerChangeBegin\">\n"
    "        <arg name=\"Name\" type=\"s\"/>\n"
    "    </signal>\n"
    "    <signal name=\"ActivePlayerChangeEnd\">\n"
    "        <arg name=\"Name\" type=\"s\"/>\n"
    "    </signal>\n"
    "  </interface>\n"
    "</node>\n";

static const char *mpris_introspection_xml =
    "<node>\n"
    "  <interface name=\"org.mpris.MediaPlayer2\">\n"
    "    <method name=\"Raise\">\n"
    "    </method>\n"
    "    <method name=\"Quit\">\n"
    "    </method>\n"
    "    <property name=\"CanQuit\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"Fullscreen\" type=\"b\" access=\"readwrite\"/>\n"
    "    <property name=\"CanSetFullscreen\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"CanRaise\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"HasTrackList\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"Identity\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"DesktopEntry\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"SupportedUriSchemes\" type=\"as\" access=\"read\"/>\n"
    "    <property name=\"SupportedMimeTypes\" type=\"as\" access=\"read\"/>\n"
    "  </interface>\n"
    "  <interface name=\"org.mpris.MediaPlayer2.Player\">\n"
    "    <method name=\"Next\">\n"
    "    </method>\n"
    "    <method name=\"Previous\">\n"
    "    </method>\n"
    "    <method name=\"Pause\">\n"
    "    </method>\n"
    "    <method name=\"PlayPause\">\n"
    "    </method>\n"
    "    <method name=\"Stop\">\n"
    "    </method>\n"
    "    <method name=\"Play\">\n"
    "    </method>\n"
    "    <method name=\"Seek\">\n"
    "      <arg type=\"x\" name=\"Offset\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"SetPosition\">\n"
    "      <arg type=\"o\" name=\"TrackId\" direction=\"in\"/>\n"
    "      <arg type=\"x\" name=\"Offset\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"OpenUri\">\n"
    "      <arg type=\"s\" name=\"Uri\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <signal name=\"Seeked\">\n"
    "      <arg type=\"x\" name=\"Position\" direction=\"out\"/>\n"
    "    </signal>\n"
    "    <property name=\"PlaybackStatus\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"LoopStatus\" type=\"s\" access=\"readwrite\"/>\n"
    "    <property name=\"Rate\" type=\"d\" access=\"readwrite\"/>\n"
    "    <property name=\"Shuffle\" type=\"b\" access=\"readwrite\"/>\n"
    "    <property name=\"Metadata\" type=\"a{sv}\" access=\"read\"/>\n"
    "    <property name=\"Volume\" type=\"d\" access=\"readwrite\"/>\n"
    "    <property name=\"Position\" type=\"x\" access=\"read\"/>\n"
    "    <property name=\"MinimumRate\" type=\"d\" access=\"read\"/>\n"
    "    <property name=\"MaximumRate\" type=\"d\" access=\"read\"/>\n"
    "    <property name=\"CanGoNext\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"CanGoPrevious\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"CanPlay\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"CanPause\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"CanSeek\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"CanControl\" type=\"b\" access=\"read\"/>\n"
    "  </interface>\n"
    "</node>\n";

static void proxy_method_call_async_callback(GObject *source_object, GAsyncResult *res,
                                             gpointer user_data) {
    GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION(user_data);
    GDBusConnection *connection = G_DBUS_CONNECTION(source_object);
    GError *error = NULL;
    GDBusMessage *reply = g_dbus_connection_send_message_with_reply_finish(connection, res, &error);
    if (error != NULL) {
        g_dbus_method_invocation_return_gerror(invocation, error);
        g_error_free(error);
        return;
    }
    GVariant *body = g_dbus_message_get_body(reply);
    GDBusMessageType message_type = g_dbus_message_get_message_type(reply);
    switch (message_type) {
    case G_DBUS_MESSAGE_TYPE_METHOD_RETURN:
        g_dbus_method_invocation_return_value(invocation, body);
        break;
    case G_DBUS_MESSAGE_TYPE_ERROR: {
        if (g_variant_n_children(body) > 1) {
            GVariant *error_message_variant = g_variant_get_child_value(body, 1);
            const char *error_message = g_variant_get_string(error_message_variant, 0);
            g_dbus_method_invocation_return_dbus_error(
                invocation, g_dbus_message_get_error_name(reply), error_message);
            g_variant_unref(error_message_variant);
        } else {
            g_dbus_method_invocation_return_dbus_error(
                invocation, g_dbus_message_get_error_name(reply), "Failed to call method");
        }
        break;
    }
    default:
        g_warning("got unexpected message type: %d (this is a dbus spec violation)", message_type);
        break;
    }

    g_object_unref(invocation);
    g_object_unref(reply);
}

static void player_method_call_proxy_callback(GDBusConnection *connection, const char *sender,
                                              const char *object_path, const char *interface_name,
                                              const char *method_name, GVariant *parameters,
                                              GDBusMethodInvocation *invocation,
                                              gpointer user_data) {
    g_debug("got method call: sender=%s, object_path=%s, interface_name=%s, method_name=%s", sender,
            object_path, interface_name, method_name);
    GError *error = NULL;
    struct PlayerctldContext *ctx = user_data;
    struct Player *active_player = context_get_active_player(ctx);
    if (active_player == NULL) {
        g_debug("no active player, returning error");
        g_dbus_method_invocation_return_dbus_error(
            invocation, "com.github.altdesktop.playerctld.NoActivePlayer",
            "No player is being controlled by playerctld");
        return;
    }

    GDBusMessage *message =
        g_dbus_message_copy(g_dbus_method_invocation_get_message(invocation), &error);
    if (error != NULL) {
        g_warning("could not copy message");
        g_dbus_method_invocation_return_gerror(invocation, error);
        return;
    }

    g_debug("sending command '%s.%s' to player '%s'", interface_name, method_name,
            active_player->well_known);

    g_dbus_message_set_destination(message, active_player->unique);

    g_object_ref(invocation);
    g_dbus_connection_send_message_with_reply(ctx->connection, message,
                                              G_DBUS_SEND_MESSAGE_FLAGS_NONE, -1, NULL, NULL,
                                              proxy_method_call_async_callback, invocation);

    g_object_unref(message);
}

static void playerctld_method_call_func(GDBusConnection *connection, const char *sender,
                                        const char *object_path, const char *interface_name,
                                        const char *method_name, GVariant *parameters,
                                        GDBusMethodInvocation *invocation, gpointer user_data) {
    g_debug("got method call: sender=%s, object_path=%s, interface_name=%s, method_name=%s", sender,
            object_path, interface_name, method_name);

    struct PlayerctldContext *ctx = user_data;
    struct Player *active_player;

    if (strcmp(method_name, "Shift") == 0) {
        if ((active_player = context_shift_active_player(ctx))) {
            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(s)", active_player->well_known));
        } else {
            g_debug("no active player, returning error");
            g_dbus_method_invocation_return_dbus_error(
                invocation, "com.github.altdesktop.playerctld.NoActivePlayer",
                "No player is being controlled by playerctld");
        }
    } else {
        g_dbus_method_invocation_return_dbus_error(invocation,
                                                   "com.github.altdesktop.playerctld.InvalidMethod",
                                                   "This method is not valid");
    }
}

static GVariant *playerctld_get_property_func(GDBusConnection *connection, const gchar *sender,
                                              const gchar *object_path, const gchar *interface_name,
                                              const gchar *property_name, GError **error,
                                              gpointer user_data) {
    struct PlayerctldContext *ctx = user_data;
    if (g_strcmp0(property_name, "PlayerNames") != 0) {
        // glib library error
        g_error("unknown property: %s", property_name);
        assert(false);
    }

    return context_player_names_to_gvariant(ctx);
}

static GDBusInterfaceVTable vtable_player = {player_method_call_proxy_callback, NULL, NULL, {0}};

static GDBusInterfaceVTable vtable_root = {player_method_call_proxy_callback, NULL, NULL, {0}};

static GDBusInterfaceVTable vtable_playerctld = {
    playerctld_method_call_func, playerctld_get_property_func, NULL, {0}};

static void on_bus_acquired(GDBusConnection *connection, const char *name, gpointer user_data) {
    GError *error = NULL;
    struct PlayerctldContext *ctx = user_data;

    g_dbus_connection_register_object(connection, MPRIS_PATH, ctx->root_interface_info,
                                      &vtable_root, user_data, NULL, &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_clear_error(&error);
    }

    g_dbus_connection_register_object(connection, MPRIS_PATH, ctx->player_interface_info,
                                      &vtable_player, user_data, NULL, &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_clear_error(&error);
    }

    g_dbus_connection_register_object(connection, MPRIS_PATH, ctx->playerctld_interface_info,
                                      &vtable_playerctld, user_data, NULL, &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_clear_error(&error);
    }
}

static void on_name_lost(GDBusConnection *connection, const char *name, gpointer user_data) {
    struct PlayerctldContext *ctx = user_data;

    if (connection) {
        pid_t pid = getpid();
        char *name = g_strdup_printf("org.mpris.MediaPlayer2.playerctld.instance%d", pid);
        ctx->bus_id = g_bus_own_name(G_BUS_TYPE_SESSION, name, G_BUS_NAME_OWNER_FLAGS_NONE, NULL,
                                     NULL, NULL, &ctx, NULL);
        g_free(name);
    }
}

static bool well_known_name_is_managed(const char *name) {
    return g_str_has_prefix(name, "org.mpris.MediaPlayer2.") &&
           !g_str_has_prefix(name, "org.mpris.MediaPlayer2.playerctld");
}

struct GetPropertiesUserData {
    struct PlayerctldContext *ctx;
    const char *interface_name;
    struct Player *player;
};

static void active_player_get_properties_async_callback(GObject *source_object, GAsyncResult *res,
                                                        gpointer user_data) {
    GDBusConnection *connection = G_DBUS_CONNECTION(source_object);
    struct GetPropertiesUserData *data = user_data;
    GError *error = NULL;
    GVariant *body = g_dbus_connection_call_finish(connection, res, &error);

    if (error != NULL) {
        g_warning("could not get properties for active player: %s", error->message);
        g_clear_error(&error);
        goto out;
    }

    g_debug("got all properties response for name='%s', interface '%s'", data->player->well_known,
            data->interface_name);
    // g_debug("%s", g_variant_print(body_value, TRUE));

    GVariant *body_value = g_variant_get_child_value(body, 0);
    player_update_properties(data->player, data->interface_name, body_value);
    g_variant_unref(body_value);
    g_variant_unref(body);

    if (data->player->player_properties == NULL || data->player->root_properties == NULL) {
        // wait for both requests to complete before setting the player
        // nonpending
        goto out;
    }

    if (data->player == data->ctx->pending_active) {
        context_set_active_player(data->ctx, data->player);
        context_emit_active_player_changed(data->ctx, &error);
        if (error != NULL) {
            g_warning("could not emit properties changed signal for active player: %s",
                      error->message);
            context_remove_player(data->ctx, data->player);
            g_clear_error(&error);
            goto out;
        }
    } else {
        context_add_player(data->ctx, data->player);
    }

out:
    free(data);
}

static void name_owner_changed_signal_callback(GDBusConnection *connection,
                                               const gchar *sender_name, const gchar *object_path,
                                               const gchar *interface_name,
                                               const gchar *signal_name, GVariant *parameters,
                                               gpointer user_data) {
    struct PlayerctldContext *ctx = user_data;
    GError *error = NULL;
    GVariant *name_variant = g_variant_get_child_value(parameters, 0);
    GVariant *new_owner_variant = g_variant_get_child_value(parameters, 2);
    const gchar *name = g_variant_get_string(name_variant, 0);
    const gchar *new_owner = g_variant_get_string(new_owner_variant, 0);

    if (!well_known_name_is_managed(name)) {
        goto out;
    }

    g_debug("got name owner changed signal: name='%s', owner='%s'", name, new_owner);

    if (strlen(new_owner) > 0) {
        g_debug("player name appeared: unique=%s, well_known=%s", new_owner, name);
        // see if it's already managed
        struct Player *player = context_find_player(ctx, NULL, name);
        if (player != NULL) {
            g_debug("player already managed, setting to active");
            player_set_unique_name(player, new_owner);
            if (player != context_get_active_player(ctx)) {
                context_set_active_player(ctx, player);
                player_update_position_sync(player, ctx, &error);
                if (error != NULL) {
                    g_warning("could not update player position: %s", error->message);
                    g_clear_error(&error);
                }
                context_emit_active_player_changed(ctx, &error);
                if (error != NULL) {
                    g_warning("could not emit active player change: %s", error->message);
                    g_clear_error(&error);
                }
            }
            goto out;
        }

        g_debug("setting player to pending active");
        player = player_new(new_owner, name);
        context_add_pending_player(ctx, player);
        ctx->pending_active = player;

        struct GetPropertiesUserData *player_data = calloc(1, sizeof(struct GetPropertiesUserData));
        player_data->interface_name = PLAYER_INTERFACE;
        player_data->player = player;
        player_data->ctx = ctx;
        g_dbus_connection_call(connection, new_owner, MPRIS_PATH, PROPERTIES_INTERFACE, "GetAll",
                               g_variant_new("(s)", PLAYER_INTERFACE), NULL,
                               G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                               active_player_get_properties_async_callback, player_data);

        struct GetPropertiesUserData *root_data = calloc(1, sizeof(struct GetPropertiesUserData));
        root_data->interface_name = ROOT_INTERFACE;
        root_data->player = player;
        root_data->ctx = ctx;
        g_dbus_connection_call(connection, new_owner, MPRIS_PATH, PROPERTIES_INTERFACE, "GetAll",
                               g_variant_new("(s)", ROOT_INTERFACE), NULL,
                               G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                               active_player_get_properties_async_callback, root_data);
    } else {
        struct Player *player = context_find_player(ctx, NULL, name);
        if (player == NULL) {
            g_debug("%s", "name not found in queue");
            goto out;
        }

        bool is_active = (player == context_get_active_player(ctx));

        g_debug("removing name from players: unique=%s, well_known=%s", player->unique,
                player->well_known);

        context_remove_player(ctx, player);
        player_free(player);

        if (!is_active) {
            goto out;
        }

        struct Player *active_player = context_get_active_player(ctx);
        if (active_player != NULL) {
            player_update_position_sync(active_player, ctx, &error);
            if (error != NULL) {
                active_player->position = 0l;
                g_warning("could not update player position for player '%s': %s",
                          active_player->well_known, error->message);
                g_clear_error(&error);
            }
        }
        context_emit_active_player_changed(ctx, &error);
        if (error != NULL) {
            g_warning("could not emit player properties changed signal: %s", error->message);
            g_clear_error(&error);
        }
    }

out:
    g_variant_unref(name_variant);
    g_variant_unref(new_owner_variant);
}

static void player_signal_proxy_callback(GDBusConnection *connection, const gchar *sender_name,
                                         const gchar *object_path, const gchar *interface_name,
                                         const gchar *signal_name, GVariant *parameters,
                                         gpointer user_data) {
    GError *error = NULL;
    struct PlayerctldContext *ctx = user_data;
    struct Player *player = context_find_player(ctx, sender_name, NULL);
    if (player == NULL) {
        return;
    }

    if (g_strcmp0(interface_name, PLAYER_INTERFACE) != 0 &&
        g_strcmp0(interface_name, PROPERTIES_INTERFACE) != 0) {
        return;
    }
    g_debug("got player signal: sender=%s, object_path=%s, interface_name=%s, signal_name=%s",
            sender_name, object_path, interface_name, signal_name);

    if (player == ctx->pending_active) {
        // TODO buffer seeked signals
        return;
    }

    bool is_properties_changed = (g_strcmp0(signal_name, "PropertiesChanged") == 0);

    if (is_properties_changed) {
        GVariant *interface = g_variant_get_child_value(parameters, 0);
        GVariant *properties = g_variant_get_child_value(parameters, 1);
        player_update_properties(player, g_variant_get_string(interface, 0), properties);
        g_variant_unref(interface);
        g_variant_unref(properties);
    }

    if (player != context_get_active_player(ctx)) {
        g_debug("new active player: %s", player->well_known);
        context_set_active_player(ctx, player);
        player_update_position_sync(player, ctx, &error);
        if (error != NULL) {
            player->position = 0l;
            g_warning("could not update player position: %s", error->message);
            g_clear_error(&error);
        }
        context_emit_active_player_changed(ctx, &error);
        if (error != NULL) {
            g_warning("could not emit all properties changed signal: %s", error->message);
            g_clear_error(&error);
        }
    }

    g_dbus_connection_emit_signal(ctx->connection, NULL, object_path, interface_name, signal_name,
                                  parameters, &error);
    if (error != NULL) {
        g_debug("could not emit signal: %s", error->message);
        g_clear_error(&error);
    }
}

static gchar **command_arg = NULL;

static const GOptionEntry entries[] = {
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &command_arg, NULL, "COMMAND"},
    {NULL},
};

static gboolean parse_setup_options(int argc, char **argv, GError **error) {
    static const gchar *description = "Available Commands:"
                                      "\n  shift                   Shift to next player";

    GOptionContext *context;
    gboolean success;

    context = g_option_context_new("- Playerctl Daemon");
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_set_description(context, description);

    success = g_option_context_parse(context, &argc, &argv, error);

    if (success && command_arg && g_strcmp0(command_arg[0], "shift") != 0) {
        gchar *help = g_option_context_get_help(context, TRUE, NULL);
        printf("%s\n", help);
        g_option_context_free(context);
        g_free(help);
        exit(1);
    }

    g_option_context_free(context);
    return success;
}

int playercmd_shift(GDBusConnection *connection) {
    GError *error = NULL;

    g_dbus_connection_call_sync(connection, "org.mpris.MediaPlayer2.playerctld", MPRIS_PATH,
                                PLAYERCTLD_INTERFACE, "Shift", NULL, NULL,
                                G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL, &error);
    g_object_unref(connection);
    if (error != NULL) {
        g_printerr("Cannot shift: %s\n", error->message);
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    struct PlayerctldContext ctx = {0};
    GError *error = NULL;

    if (!parse_setup_options(argc, argv, &error)) {
        g_printerr("%s\n", error->message);
        g_clear_error(&error);
        exit(0);
    }

    ctx.connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (error != NULL) {
        g_printerr("%s", error->message);
        return 1;
    }

    g_debug("connected to dbus: %s", g_dbus_connection_get_unique_name(ctx.connection));

    if (command_arg && g_strcmp0(command_arg[0], "shift") == 0) {
        return playercmd_shift(ctx.connection);
    }

    GDBusNodeInfo *mpris_introspection_data = NULL;
    GDBusNodeInfo *playerctld_introspection_data = NULL;
    ctx.players = g_queue_new();
    ctx.pending_players = g_queue_new();
    ctx.loop = g_main_loop_new(NULL, FALSE);

    // Load introspection data and split into separate interfaces
    mpris_introspection_data = g_dbus_node_info_new_for_xml(mpris_introspection_xml, &error);
    if (error != NULL) {
        g_printerr("%s", error->message);
        return 1;
    }
    playerctld_introspection_data =
        g_dbus_node_info_new_for_xml(playerctld_introspection_xml, &error);
    if (error != NULL) {
        g_printerr("%s", error->message);
        return 1;
    }
    ctx.root_interface_info =
        g_dbus_node_info_lookup_interface(mpris_introspection_data, ROOT_INTERFACE);
    ctx.player_interface_info =
        g_dbus_node_info_lookup_interface(mpris_introspection_data, PLAYER_INTERFACE);
    ctx.playerctld_interface_info = g_dbus_node_info_lookup_interface(
        playerctld_introspection_data, "com.github.altdesktop.playerctld");

    GVariant *names_reply = g_dbus_connection_call_sync(
        ctx.connection, DBUS_NAME, DBUS_PATH, DBUS_INTERFACE, "ListNames", NULL, NULL,
        G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL, &error);
    if (error != NULL) {
        g_warning("could not call ListNames: %s", error->message);
        return 1;
    }
    GVariant *names_reply_value = g_variant_get_child_value(names_reply, 0);
    gsize nnames;
    const gchar **names = g_variant_get_strv(names_reply_value, &nnames);
    for (int i = 0; i < nnames; ++i) {
        if (well_known_name_is_managed(names[i])) {
            GVariant *owner_reply =
                g_dbus_connection_call_sync(ctx.connection, DBUS_NAME, DBUS_PATH, DBUS_INTERFACE,
                                            "GetNameOwner", g_variant_new("(s)", names[i]), NULL,
                                            G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL, &error);
            if (error != NULL) {
                g_warning("could not get owner for name %s: %s", names[i], error->message);
                g_clear_error(&error);
                continue;
            }

            GVariant *owner_reply_value = g_variant_get_child_value(owner_reply, 0);
            const gchar *owner = g_variant_get_string(owner_reply_value, 0);

            struct Player *player = player_new(owner, names[i]);

            GVariant *reply = g_dbus_connection_call_sync(
                ctx.connection, player->unique, MPRIS_PATH, PROPERTIES_INTERFACE, "GetAll",
                g_variant_new("(s)", PLAYER_INTERFACE), NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, -1,
                NULL, &error);
            if (error != NULL) {
                g_warning("could not get player properties for player: %s", player->well_known);
                player_free(player);
                g_clear_error(&error);
                continue;
            }

            GVariant *properties = g_variant_get_child_value(reply, 0);
            player_update_properties(player, PLAYER_INTERFACE, properties);
            g_variant_unref(properties);
            g_variant_unref(reply);

            reply = g_dbus_connection_call_sync(ctx.connection, player->unique, MPRIS_PATH,
                                                PROPERTIES_INTERFACE, "GetAll",
                                                g_variant_new("(s)", ROOT_INTERFACE), NULL,
                                                G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL, &error);
            if (error != NULL) {
                g_warning("could not get root properties for player: %s", player->well_known);
                player_free(player);
                g_clear_error(&error);
                continue;
            }

            properties = g_variant_get_child_value(reply, 0);
            player_update_properties(player, ROOT_INTERFACE, properties);
            g_variant_unref(properties);
            g_variant_unref(reply);

            g_debug("found player: %s", player->well_known);
            g_queue_push_head(ctx.players, player);
            g_variant_unref(owner_reply_value);
            g_variant_unref(owner_reply);
        }
    }

    g_free(names);
    g_variant_unref(names_reply_value);
    g_variant_unref(names_reply);

    g_dbus_connection_signal_subscribe(
        ctx.connection, DBUS_NAME, DBUS_INTERFACE, "NameOwnerChanged", DBUS_PATH, NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, name_owner_changed_signal_callback, &ctx, NULL);

    g_dbus_connection_signal_subscribe(ctx.connection, NULL, NULL, NULL, MPRIS_PATH, NULL,
                                       G_DBUS_SIGNAL_FLAGS_NONE, player_signal_proxy_callback, &ctx,
                                       NULL);

    ctx.bus_id = g_bus_own_name_on_connection(ctx.connection, "org.mpris.MediaPlayer2.playerctld",
                                              G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_acquired,
                                              on_name_lost, &ctx, NULL);

    g_main_loop_run(ctx.loop);
    g_bus_unown_name(ctx.bus_id);
    g_main_loop_unref(ctx.loop);
    g_dbus_node_info_unref(mpris_introspection_data);
    g_dbus_node_info_unref(playerctld_introspection_data);
    g_queue_free_full(ctx.players, (GDestroyNotify)player_free);
    g_object_unref(ctx.connection);

    return 0;
}
