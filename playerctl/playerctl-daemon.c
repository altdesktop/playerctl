#include <assert.h>
#include <gio/gio.h>
#include <stdbool.h>
#include <stdio.h>

struct UserData {
    GMainLoop *loop;
    gint bus_id;
    GDBusConnection *connection;
    GDBusInterfaceInfo *root_interface_info;
    GDBusInterfaceInfo *player_interface_info;
    GQueue *player_names;
};

struct FullName {
    char *unique;
    char *well_known;
};

static struct FullName *full_name_new(const char *unique, const char *well_known) {
    struct FullName *name = calloc(1, sizeof(struct FullName));
    name->unique = g_strdup(unique);
    name->well_known = g_strdup(well_known);
    return name;
}

static void full_name_free(struct FullName *name) {
    if (name == NULL) {
        return;
    }

    g_free(name->unique);
    g_free(name->well_known);
    free(name);
}

static gint full_name_compare(gconstpointer a, gconstpointer b) {
    struct FullName *fn_a = (struct FullName *)a;
    struct FullName *fn_b = (struct FullName *)b;
    if (fn_a->unique != NULL && fn_b->unique != NULL && strcmp(fn_a->unique, fn_b->unique) != 0) {
        return 1;
    }
    if (fn_a->well_known != NULL && fn_b->well_known != NULL &&
        strcmp(fn_a->well_known, fn_b->well_known) != 0) {
        return 1;
    }

    return 0;
}

static const char *introspection_xml =
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
    GError *error = NULL;
    struct UserData *ud = user_data;
    if (g_queue_get_length(ud->player_names) == 0) {
        g_dbus_method_invocation_return_dbus_error(invocation,
                                                   "org.dubstepdish.playerctld.NoActivePlayer",
                                                   "No player is being controlled by playerctld");
        return;
    }

    GDBusMessage *message =
        g_dbus_message_copy(g_dbus_method_invocation_get_message(invocation), &error);
    if (error != NULL) {
        g_dbus_method_invocation_return_gerror(invocation, error);
        return;
    }

    struct FullName *full_name = g_queue_peek_head(ud->player_names);

    g_dbus_message_set_destination(message, full_name->unique);

    g_object_ref(invocation);
    g_dbus_connection_send_message_with_reply(ud->connection, message,
                                              G_DBUS_SEND_MESSAGE_FLAGS_NONE, -1, NULL, NULL,
                                              proxy_method_call_async_callback, invocation);

    g_object_unref(message);
}

static GDBusInterfaceVTable vtable_player = {player_method_call_proxy_callback, NULL, NULL, {0}};

static GDBusInterfaceVTable vtable_root = {player_method_call_proxy_callback, NULL, NULL, {0}};

static void on_bus_acquired(GDBusConnection *connection, const char *name, gpointer user_data) {
    GError *error = NULL;
    struct UserData *ud = user_data;

    g_dbus_connection_register_object(connection, "/org/mpris/MediaPlayer2",
                                      ud->root_interface_info, &vtable_root, user_data, NULL,
                                      &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }

    g_dbus_connection_register_object(connection, "/org/mpris/MediaPlayer2",
                                      ud->player_interface_info, &vtable_player, user_data, NULL,
                                      &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
}

static void on_name_lost(GDBusConnection *connection, const char *name, gpointer user_data) {
    struct UserData *ud = user_data;

    if (connection) {
        pid_t pid = getpid();
        char *name = g_strdup_printf("org.mpris.MediaPlayer2.playerctld.instance%d", pid);
        ud->bus_id = g_bus_own_name(G_BUS_TYPE_SESSION, name, G_BUS_NAME_OWNER_FLAGS_NONE, NULL,
                                    NULL, NULL, &ud, NULL);
        g_free(name);
    }
}

static bool well_known_name_is_mpris(const char *name) {
    return g_str_has_prefix(name, "org.mpris.MediaPlayer2.") &&
           !g_str_has_suffix(name, ".playerctld");
}

static void name_owner_changed_signal_callback(GDBusConnection *connection,
                                               const gchar *sender_name, const gchar *object_path,
                                               const gchar *interface_name,
                                               const gchar *signal_name, GVariant *parameters,
                                               gpointer user_data) {
    struct UserData *ud = user_data;

    GVariant *name_variant = g_variant_get_child_value(parameters, 0);
    GVariant *new_owner_variant = g_variant_get_child_value(parameters, 2);
    const gchar *name = g_variant_get_string(name_variant, 0);
    const gchar *new_owner = g_variant_get_string(new_owner_variant, 0);

    if (!well_known_name_is_mpris(name)) {
        goto out;
    }

    g_debug("got name owner changed signal: name=%s, owner=%s", name, new_owner);

    if (strlen(new_owner) > 0) {
        g_debug("adding name to queue: unique=%s, well_known=%s", new_owner, name);
        struct FullName *full_name = full_name_new(new_owner, name);
        g_queue_push_head(ud->player_names, full_name);
    } else {
        const struct FullName find_name = {
            .unique = (char *)new_owner,
            .well_known = (char *)name,
        };
        GList *found = g_queue_find_custom(ud->player_names, &find_name, full_name_compare);
        if (found != NULL) {
            struct FullName *full_name = (struct FullName *)found->data;
            g_debug("removing name from queue: unique=%s, well_known=%s", full_name->unique,
                    full_name->well_known);
            g_queue_remove(ud->player_names, full_name);
            full_name_free(full_name);
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
    struct UserData *ud = user_data;
    const struct FullName find_name = {
        .unique = (char *)sender_name,
        .well_known = NULL,
    };
    GList *found = g_queue_find_custom(ud->player_names, &find_name, full_name_compare);
    if (found == NULL) {
        return;
    }

    if (g_strcmp0(interface_name, "org.mpris.MediaPlayer2.Player") != 0 &&
        g_strcmp0(interface_name, "org.freedesktop.DBus.Properties") != 0) {
        return;
    }
    g_debug("got player signal: sender=%s, object_path=%s, interface_name=%s, signal_name=%s",
            sender_name, object_path, interface_name, signal_name);

    struct FullName *full_name = found->data;
    g_debug("new active player: %s", full_name->well_known);
    g_queue_unlink(ud->player_names, found);
    g_queue_push_head_link(ud->player_names, found);

    GError *error = NULL;
    g_dbus_connection_emit_signal(ud->connection, NULL, object_path, interface_name, signal_name,
                                  parameters, &error);
    if (error != NULL) {
        g_debug("could not emit signal: %s", error->message);
        g_error_free(error);
    }
}

int main(int argc, char *argv[]) {
    struct UserData ud = {0};
    GError *error = NULL;
    GDBusNodeInfo *introspection_data = NULL;
    ud.player_names = g_queue_new();

    ud.loop = g_main_loop_new(NULL, FALSE);

    // Load introspection data and split into separate interfaces
    introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, &error);
    if (error != NULL) {
        g_printerr("%s", error->message);
        return 1;
    }
    ud.root_interface_info =
        g_dbus_node_info_lookup_interface(introspection_data, "org.mpris.MediaPlayer2");
    ud.player_interface_info =
        g_dbus_node_info_lookup_interface(introspection_data, "org.mpris.MediaPlayer2.Player");

    ud.connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (error != NULL) {
        g_printerr("%s", error->message);
        return 1;
    }

    GVariant *names_reply = g_dbus_connection_call_sync(
        ud.connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "ListNames", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if (error != NULL) {
        g_warning("could not call ListNames: %s", error->message);
        return 1;
    }
    GVariant *names_reply_value = g_variant_get_child_value(names_reply, 0);
    gsize nnames;
    const gchar **names = g_variant_get_strv(names_reply_value, &nnames);
    for (int i = 0; i < nnames; ++i) {
        if (g_str_has_prefix(names[i], "org.mpris.MediaPlayer2.") &&
            !g_str_has_suffix(names[i], ".playerctld")) {
            // TODO: make async
            GVariant *owner_reply = g_dbus_connection_call_sync(
                ud.connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                "org.freedesktop.DBus", "GetNameOwner", g_variant_new("(s)", names[i]), NULL,
                G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
            if (error != NULL) {
                g_warning("could not get owner for name %s: %s", names[i], error->message);
                g_error_free(error);
                continue;
            }

            GVariant *owner_reply_value = g_variant_get_child_value(owner_reply, 0);
            const gchar *owner = g_variant_get_string(owner_reply_value, 0);

            struct FullName *full_name = full_name_new(owner, names[i]);

            g_queue_push_head(ud.player_names, full_name);
            g_variant_unref(owner_reply_value);
            g_variant_unref(owner_reply);
        }
    }

    g_free(names);
    g_variant_unref(names_reply_value);
    g_variant_unref(names_reply);

    g_dbus_connection_signal_subscribe(ud.connection, "org.freedesktop.DBus",
                                       "org.freedesktop.DBus", "NameOwnerChanged",
                                       "/org/freedesktop/DBus", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                       name_owner_changed_signal_callback, &ud, NULL);

    g_dbus_connection_signal_subscribe(ud.connection, NULL, NULL, NULL, "/org/mpris/MediaPlayer2",
                                       NULL, G_DBUS_SIGNAL_FLAGS_NONE, player_signal_proxy_callback,
                                       &ud, NULL);

    ud.bus_id = g_bus_own_name_on_connection(ud.connection, "org.mpris.MediaPlayer2.playerctld",
                                             G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_acquired,
                                             on_name_lost, &ud, NULL);

    g_main_loop_run(ud.loop);
    g_bus_unown_name(ud.bus_id);
    g_main_loop_unref(ud.loop);
    g_dbus_node_info_unref(introspection_data);
    g_queue_free_full(ud.player_names, (GDestroyNotify)full_name_free);

    return 0;
}
