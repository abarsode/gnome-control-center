/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"
#include <stdlib.h>

#include <glib-object.h>
#include <glib/gi18n.h>

#include "service.h"
#include "net-connection-editor.h"
#include "cc-network-resources.h"

#define WID(b, w) (GtkWidget *) gtk_builder_get_object (b, w)

enum {
        DONE,
        LAST_SIGNAL
};

enum {
        COLUMN_ICON,
        COLUMN_PULSE,
        COLUMN_PULSE_ID,
        COLUMN_NAME,
        COLUMN_STATE,
        COLUMN_SECURITY_ICON,
        COLUMN_SECURITY,
        COLUMN_TYPE,
        COLUMN_STRENGTH_ICON,
        COLUMN_STRENGTH,
        COLUMN_FAVORITE,
        COLUMN_GDBUSPROXY,
        COLUMN_PROP_ID,
        COLUMN_AUTOCONNECT,
        COLUMN_ETHERNET,
        COLUMN_IPV4,
        COLUMN_IPV6,
        COLUMN_NAMESERVERS,
        COLUMN_DOMAINS,
        COLUMN_PROXY,
        COLUMN_EDITOR,
        COLUMN_LAST
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (NetConnectionEditor, net_connection_editor, G_TYPE_OBJECT)

static void
net_connection_editor_update_apply (NetConnectionEditor *editor)
{

        if (editor->update_proxy || editor->update_ipv4 ||
            editor->update_ipv6 || editor->update_domains ||
            editor->update_nameservers || editor->update_autoconnect)
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "apply_button")), TRUE);
        else
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "apply_button")), FALSE);
}

/* Details section */
void
editor_update_details (NetConnectionEditor *editor)
{
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;

        const gchar *interface;
        const gchar *signal_strength;
        const gchar *security, *ipv4_address, *mac, *def_route;
        gboolean autoconnect, favorite, ret;
        GVariant *ethernet, *ipv4, *nameservers;
        gchar **ns;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_FAVORITE, &favorite,
                            COLUMN_SECURITY, &security,
                            COLUMN_STRENGTH, &signal_strength,
                            COLUMN_AUTOCONNECT, &autoconnect,
                            COLUMN_ETHERNET, &ethernet,
                            COLUMN_IPV4, &ipv4,
                            COLUMN_NAMESERVERS, &nameservers,
                            -1);

        net_connection_editor_update_apply (editor);

        if (favorite) {
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "switch_autoconnect")), TRUE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "button_forget")), TRUE);

                gtk_switch_set_active (GTK_SWITCH (WID (editor->builder, "switch_autoconnect")), autoconnect);
        } else {
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "switch_autoconnect")), FALSE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "button_forget")), FALSE);
        }

        gtk_label_set_text (GTK_LABEL (WID (editor->builder, "label_strength")), signal_strength);

        gtk_label_set_text (GTK_LABEL (WID (editor->builder, "label_security")), g_ascii_strup (security, -1));

        ret = g_variant_lookup (ethernet, "Interface", "s", &interface);
        if (ret)
                gtk_label_set_text (GTK_LABEL (WID (editor->builder, "label_interface")), interface);
        else
                gtk_label_set_text (GTK_LABEL (WID (editor->builder, "label_interface")), "--:--");

        ret = g_variant_lookup (ethernet, "Address", "s", &mac);
        if (ret)
                gtk_label_set_text (GTK_LABEL (WID (editor->builder, "label_mac")), mac);
        else
                gtk_label_set_text (GTK_LABEL (WID (editor->builder, "label_mac")), "N/A");

        ret = g_variant_lookup (ipv4, "Address", "s", &ipv4_address);
        if (ret)
                gtk_label_set_text (GTK_LABEL (WID (editor->builder, "label_ipv4_address")), ipv4_address);
        else
                gtk_label_set_text (GTK_LABEL (WID (editor->builder, "label_ipv4_address")), "N/A");

        ret = g_variant_lookup (ipv4, "Gateway", "s", &def_route);
        if (ret)
                gtk_label_set_text (GTK_LABEL (WID (editor->builder, "label_default_route")), def_route);
        else
                gtk_label_set_text (GTK_LABEL (WID (editor->builder, "label_default_route")), "N/A");

        ns = g_variant_get_strv (nameservers, NULL);
        if (ns && ns[0])
                gtk_label_set_text (GTK_LABEL (WID (editor->builder, "label_dns")), g_strjoinv (",", (gchar **) ns));
        else
                gtk_label_set_text (GTK_LABEL (WID (editor->builder, "label_dns")), "N/A");

        editor->update_autoconnect = FALSE;
        net_connection_editor_update_apply (editor);
}

static void
service_set_autoconnect (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
        NetConnectionEditor *editor = user_data;
        gboolean ac;
        GError *error = NULL;

        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;

        if (!editor)
                return;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            -1);

        if (!service_call_set_property_finish (service, res, &error)) {
                gchar *err = g_dbus_error_get_remote_error (error);

                if (!g_strcmp0 (err, "net.connman.Error.AlreadyEnabled") || !g_strcmp0 (err, "net.connman.Error.AlreadyDisabled"))
                        goto done;

                ac = gtk_switch_get_active (GTK_SWITCH (WID (editor->builder, "switch_autoconnect")));

                gtk_switch_set_active (GTK_SWITCH (WID (editor->builder, "switch_autoconnect")), !ac);

                g_warning ("Could not set Service AutoConnect property: %s", error->message);
done:
                g_error_free (error);
                g_free (err);
                return;
        }
}

static void
autoconnect_toggle (NetConnectionEditor *editor)
{
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;
        gboolean autoconnect, ac;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            COLUMN_AUTOCONNECT, &autoconnect,
                            -1);

        ac = gtk_switch_get_active (GTK_SWITCH (WID (editor->builder, "switch_autoconnect")));
        if (ac == autoconnect)
                editor->update_autoconnect = FALSE;
        else
                editor->update_autoconnect = TRUE;

        net_connection_editor_update_apply (editor);
}

static void
editor_set_autoconnect (NetConnectionEditor *editor)
{
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;
        gboolean autoconnect, ac;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            COLUMN_AUTOCONNECT, &autoconnect,
                            -1);

        ac = gtk_switch_get_active (GTK_SWITCH (WID (editor->builder, "switch_autoconnect")));
        if (ac == autoconnect)
                return;

        service_call_set_property (service,
                                   "AutoConnect",
                                   g_variant_new_variant (g_variant_new_boolean (ac)),
                                   NULL,
                                   service_set_autoconnect,
                                   editor);
}

static void
service_removed (GObject      *source,
                 GAsyncResult *res,
                 gpointer      user_data)
{
        NetConnectionEditor *editor = user_data;
        GError *error = NULL;

        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;

        if (!editor)
                return;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            -1);

        if (!service_call_remove_finish (service, res, &error)) {
                g_warning ("Could not remove Service: %s", error->message);
                g_error_free (error);
        }

        g_signal_emit (editor, signals[DONE], 0, FALSE);
}

static void
forget_service (NetConnectionEditor *editor)
{
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            -1);

        service_call_remove (service,
                             NULL,
                             service_removed,
                             editor);
}
/* Details section Ends */

/* Proxy section */

static void
proxy_setup_entry (NetConnectionEditor *editor, gchar *method)
{
        editor->proxy_method = method;

        if (!g_strcmp0 (method, "direct")) {
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "proxy_url")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "http_proxy_servers")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "http_proxy_port")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "https_proxy_servers")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "https_proxy_port")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "socks_proxy_servers")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "socks_proxy_port")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "socks_proxy_v4")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "socks_proxy_v5")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "proxy_excludes")));

                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_url")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_http_servers")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_https_servers")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_socks_servers")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_excludes")));

        } else if (!g_strcmp0 (method, "auto")) {
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "label_url")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "proxy_url")));

                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_http_servers")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_https_servers")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_socks_servers")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "http_proxy_servers")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "http_proxy_port")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "https_proxy_servers")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "https_proxy_port")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "socks_proxy_servers")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "socks_proxy_port")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "socks_proxy_v4")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "socks_proxy_v5")));

                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_excludes")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "proxy_excludes")));
        } else if (!g_strcmp0 (method, "manual")) {
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_url")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "proxy_url")));

                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "label_http_servers")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "label_https_servers")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "label_socks_servers")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "http_proxy_servers")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "http_proxy_port")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "https_proxy_servers")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "https_proxy_port")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "socks_proxy_servers")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "socks_proxy_port")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "socks_proxy_v4")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "socks_proxy_v5")));

                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "label_excludes")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "proxy_excludes")));
        } else
                g_warning ("Unknown method for proxy");
}

static void
proxy_method_changed (GtkComboBox *combo, gpointer user_data)
{
        NetConnectionEditor *editor = user_data;
        gint active;

        active = gtk_combo_box_get_active (combo);
        if (active == 0) {
                proxy_setup_entry (editor, "direct");

                editor->update_proxy = TRUE;
                net_connection_editor_update_apply (editor);
        } else if (active == 1) {
                proxy_setup_entry (editor, "auto");

                if (gtk_entry_get_text_length (GTK_ENTRY (WID (editor->builder, "proxy_url"))) > 0) {
                        editor->update_proxy = TRUE;
                        net_connection_editor_update_apply (editor);
                }
        } else {
                proxy_setup_entry (editor, "manual");
                if (gtk_entry_get_text_length (GTK_ENTRY (WID (editor->builder, "http_proxy_servers"))) > 0
                    || gtk_entry_get_text_length (GTK_ENTRY (WID (editor->builder, "https_proxy_servers"))) > 0
                    || gtk_entry_get_text_length (GTK_ENTRY (WID (editor->builder, "socks_proxy_servers"))) > 0) {
                        editor->update_proxy = TRUE;
                        net_connection_editor_update_apply (editor);
                }
        }
}

static void
proxy_url_text_changed (NetConnectionEditor *editor)
{
        GtkEntry *entry;
        guint16 len;

        if (g_strcmp0 (editor->proxy_method, "auto") != 0)
                return;

        entry = GTK_ENTRY (WID (editor->builder, "proxy_url"));

        len = gtk_entry_get_text_length (entry);
        if (len == 0)
                editor->update_proxy = FALSE;
        else
                editor->update_proxy = TRUE;

        net_connection_editor_update_apply (editor);
}

static void
http_proxy_servers_text_changed (NetConnectionEditor *editor)
{
        GtkEntry *entry;
        guint16 len;

        if (g_strcmp0 (editor->proxy_method, "manual") != 0)
                return;

        entry = GTK_ENTRY (WID (editor->builder, "http_proxy_servers"));

        len = gtk_entry_get_text_length (entry);
        if (len == 0)
                editor->update_proxy = FALSE;
        else
                editor->update_proxy = TRUE;

        net_connection_editor_update_apply (editor);
}

static void
https_proxy_servers_text_changed (NetConnectionEditor *editor)
{
        GtkEntry *entry;
        guint16 len;

        if (g_strcmp0 (editor->proxy_method, "manual") != 0)
                return;

        entry = GTK_ENTRY (WID (editor->builder, "https_proxy_servers"));

        len = gtk_entry_get_text_length (entry);
        if (len == 0)
                editor->update_proxy = FALSE;
        else
                editor->update_proxy = TRUE;

        net_connection_editor_update_apply (editor);
}

static void
socks_proxy_servers_text_changed (NetConnectionEditor *editor)
{
        GtkEntry *entry;
        guint16 len;

        if (g_strcmp0 (editor->proxy_method, "manual") != 0)
                return;

        entry = GTK_ENTRY (WID (editor->builder, "socks_proxy_servers"));

        len = gtk_entry_get_text_length (entry);
        if (len == 0)
                editor->update_proxy = FALSE;
        else
                editor->update_proxy = TRUE;

        net_connection_editor_update_apply (editor);
}

static void
proxy_excludes_text_changed (NetConnectionEditor *editor)
{
        GtkEntry *entry;
        guint16 len;

        if (g_strcmp0 (editor->proxy_method, "manual") != 0)
                return;

        entry = GTK_ENTRY (WID (editor->builder, "proxy_excludes"));

        len = gtk_entry_get_text_length (entry);
        if (len == 0)
                return;

        editor->update_proxy = TRUE;

        net_connection_editor_update_apply (editor);
}

static void
parse_server_entry (const gchar *server_entry, gchar **protocol, gchar **server,
                    gint *port)
{
        gchar **host, **url;
        guint host_part = 0;

        if (!server_entry)
                return;

        /* proto://server.example.com:911 */
        host = g_strsplit (server_entry, "://", -1);
        if (g_strv_length (host) < 1) {
                goto host;
        } else if (g_strv_length (host) > 1 && host[1]) {
                /* Got protocol and domain[:port] */
                (*protocol) = g_strdup (host[0]);
                host_part = 1;
        } else {
                /* Got domain[:port]
                 *
                 * According to the GSettings schema for org.gnome.system.proxy
                 * SOCKS is used by default where a proxy for a specific
                 * protocol is not set, so default to settings socks when we
                 * get an entry from connman without a protocol set.
                 */
                (*protocol) = g_strdup ("socks");
        }

        url = g_strsplit (host[host_part], ":", -1);
        if (g_strv_length (url) > 1 && url[1]) {
                (*port) = g_ascii_strtoull (url[1], NULL, 0);
        } else {
                (*port) = 0;
        }
        (*server) = g_strdup (url[0]);

        g_strfreev (url);
host:
        g_strfreev (host);
}

void
editor_update_proxy (NetConnectionEditor *editor)
{
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;

        gboolean ret;
        gchar *method, *url;
        gchar **servers, **excludes;
        GVariant *proxy, *value;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_PROXY, &proxy,
                            -1);

        ret = g_variant_lookup (proxy, "Method", "s", &method);
        if (!ret)
                method = "direct";

        proxy_setup_entry (editor, method);

        if (!g_strcmp0 (method, "direct"))
                gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_proxy_method")), 0);
        else  if (!g_strcmp0 (method, "auto")) {
                gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_proxy_method")), 1);
                g_variant_lookup (proxy, "URL", "s", &url);
                gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "proxy_url")), url);
        } else {
                gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_proxy_method")), 2);

                value = g_variant_lookup_value (proxy, "Servers", G_VARIANT_TYPE_STRING_ARRAY);
                servers = (gchar **) g_variant_get_strv (value, NULL);

                value = g_variant_lookup_value (proxy, "Excludes", G_VARIANT_TYPE_STRING_ARRAY);
                excludes = (gchar **) g_variant_get_strv (value, NULL);

                if (servers != NULL) {
                        gint i, port = 0;
                        gchar *proto = NULL, *server = NULL;
                        gsize num_servers;

                        num_servers = g_strv_length (servers);

                        for (i = 0; i < num_servers; i++) {
                                parse_server_entry (servers[i], &proto, &server, &port);
                                /* The UI only supports a single server for each
                                 * protocol */
                                if (g_strcmp0 (proto, "http") == 0) {
                                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "http_proxy_servers")), server);
                                        gtk_adjustment_set_value (GTK_ADJUSTMENT (WID (editor->builder, "adjustment_http_port")), (gdouble) port);

                                        g_free (server);
                                        g_free (proto);
                                        port = 0;
                                } else if (g_strcmp0 (proto, "https") == 0) {
                                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "https_proxy_servers")), server);
                                        gtk_adjustment_set_value (GTK_ADJUSTMENT (WID (editor->builder, "adjustment_https_port")), (gdouble) port);

                                        g_free (server);
                                        g_free (proto);
                                        port = 0;
                                } else if (g_strcmp0 (proto, "socks") == 0 ||
                                           g_strcmp0 (proto, "socks4") == 0 ||
                                           g_strcmp0 (proto, "socks5") == 0) {
                                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "socks_proxy_servers")), server);
                                        gtk_adjustment_set_value (GTK_ADJUSTMENT (WID (editor->builder, "adjustment_socks_port")), (gdouble) port);
                                        if (g_strcmp0 (proto, "socks4") == 0)
                                                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (WID (editor->builder, "socks_proxy_v4")), TRUE);
                                        else
                                                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (WID (editor->builder, "socks_proxy_v5")), TRUE);

                                        g_free (server);
                                        g_free (proto);
                                        port = 0;
                                }
                        }

                        g_free (servers);
                }

                if (excludes != NULL)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "proxy_excludes")), g_strjoinv (",", (gchar **) excludes));
        }

        editor->update_proxy = FALSE;
        net_connection_editor_update_apply (editor);
}

static void
service_set_proxy (GObject      *source,
                   GAsyncResult *res,
                   gpointer      user_data)
{
        NetConnectionEditor *editor = user_data;
        GError *error = NULL;

        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;

        if (!editor)
                return;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            -1);

        if (!service_call_set_property_finish (service, res, &error)) {
                g_warning ("Could not set proxy: %s", error->message);
                g_error_free (error);
                return;
        }
}

static void
append_string (GString *gstring, const gchar *append, gint port)
{
        gchar *str;

        g_debug ("appending proxy url:%s port:%d", append, port);

        if (gstring->len > 0 && port > 0)
                str = g_strdup_printf (" %s:%d", append, port);
        else if (gstring->len > 0)
                str = g_strdup_printf (" %s", append);
        else if (port > 0)
                str = g_strdup_printf ("%s:%d", append, port);
        else
                str = g_strdup (append);

        g_string_append (gstring, str);
        g_free (str);
}

static gboolean
has_protocol (const gchar *url)
{
        gboolean has = FALSE;
        gchar **components;

        components = g_strsplit (url, "://", 0);

        if (g_strv_length (components) > 1 && components[0]) {
                has = TRUE;
                g_debug ("url %s has protocol %s", url, components[0]);
        }

        g_strfreev (components);

        return has;
}

static void
editor_set_proxy (NetConnectionEditor *editor)
{
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;
        gint active;
        GVariantBuilder *proxyconf = g_variant_builder_new (G_VARIANT_TYPE_DICTIONARY);
        gchar *str;
        gchar **servers = NULL;
        gchar **excludes = NULL;
        gint port;
        GVariant *value;
        GString *proxy_list;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            -1);


        active = gtk_combo_box_get_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_proxy_method")));
        if (active == 0) {
                g_variant_builder_add (proxyconf,"{sv}", "Method", g_variant_new_string ("direct"));
        } else if (active == 1) {
                g_variant_builder_add (proxyconf,"{sv}", "Method", g_variant_new_string ("auto"));
                str = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (editor->builder, "proxy_url")));
                if (str)
                        g_variant_builder_add (proxyconf,"{sv}", "URL", g_variant_new_string (str));
        } else {
                g_variant_builder_add (proxyconf,"{sv}", "Method", g_variant_new_string ("manual"));

                proxy_list = g_string_new ("");
                str = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (editor->builder, "http_proxy_servers")));
                port = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (WID (editor->builder, "http_proxy_port")));
                if (str && strlen (str)) {
                        if (has_protocol (str))
                                append_string (proxy_list, str, port);
                        else {
                                gchar *url = g_strdup_printf ("http://%s", str);
                                append_string (proxy_list, (const gchar *)url, port);
                                g_free (url);
                        }
                }

                str = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (editor->builder, "https_proxy_servers")));
                port = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (WID (editor->builder, "https_proxy_port")));
                if (str && strlen (str)) {
                        if (has_protocol (str))
                                append_string (proxy_list, str, port);
                        else {
                                gchar *url = g_strdup_printf ("https://%s", str);
                                append_string (proxy_list, (const gchar *)url, port);
                                g_free (url);
                        }
                }

                str = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (editor->builder, "socks_proxy_servers")));
                port = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (WID (editor->builder, "socks_proxy_port")));
                if (str && strlen (str)) {
                        if (has_protocol (str))
                                append_string (proxy_list, str, port);
                        else {
                                gchar *url;
                                if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (WID (editor->builder, "socks_proxy_v4"))))
                                        url = g_strdup_printf ("socks4://%s", str);
                                else
                                        /* default to SOCKS5 */
                                        url = g_strdup_printf ("socks5://%s", str);
                                append_string (proxy_list, (const gchar *)url, port);
                                g_free (url);
                        }
                }

                if (proxy_list->len > 0) {
                        servers = g_strsplit (proxy_list->str, " ", -1);
                        g_variant_builder_add (proxyconf,"{sv}", "Servers", g_variant_new_strv ((const gchar * const *) servers, -1));
                }
                g_string_free (proxy_list, TRUE);

                str = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (editor->builder, "proxy_excludes")));
                if (str) {
                        excludes = g_strsplit (str, ",", -1);
                        g_variant_builder_add (proxyconf,"{sv}", "Excludes", g_variant_new_strv ((const gchar * const *) excludes, -1));
                }
        }

        value = g_variant_builder_end (proxyconf);

        service_call_set_property (service,
                                  "Proxy.Configuration",
                                   g_variant_new_variant (value),
                                   NULL,
                                   service_set_proxy,
                                   editor);

        g_variant_builder_unref (proxyconf);

        if (servers)
                g_strfreev (servers);

        if (excludes)
                g_strfreev (excludes);
}

/* Proxy section Ends */

/* IPv4 section */

static void
ipv4_setup_entry (NetConnectionEditor *editor, gchar *method)
{
        editor->ipv4_method = method;

        if (!g_strcmp0 (method, "off")) {
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "ipv4_address")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "ipv4_netmask")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "ipv4_gateway")));

                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_ipv4_addr")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_ipv4_netmask")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_ipv4_gateway")));
                return;
        } else {
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "ipv4_address")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "ipv4_netmask")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "ipv4_gateway")));

                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "label_ipv4_addr")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "label_ipv4_netmask")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "label_ipv4_gateway")));
        }

        if (!g_strcmp0 (method, "dhcp") || !g_strcmp0 (method, "fixed")) {
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "ipv4_address")), FALSE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "ipv4_netmask")), FALSE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "ipv4_gateway")), FALSE);
                return;
        }

        if (!g_strcmp0 (method, "manual")) {
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "ipv4_address")), TRUE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "ipv4_netmask")), TRUE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "ipv4_gateway")), TRUE);
        }
}

static void
ipv4_method_changed (GtkComboBox *combo, gpointer user_data)
{
        NetConnectionEditor *editor = user_data;
        gint active;

        active = gtk_combo_box_get_active (combo);
        if (active == 0) {
                ipv4_setup_entry (editor, "off");
                editor->update_ipv4 = TRUE;
                net_connection_editor_update_apply (editor);
        } else if (active == 1) {
                ipv4_setup_entry (editor, "dhcp");

                editor->update_ipv4 = TRUE;
                net_connection_editor_update_apply (editor);
        } else if (active == 2) {
                ipv4_setup_entry (editor, "manual");
        }
}

static void
ipv4_address_text_changed (NetConnectionEditor *editor)
{
        GtkEntry *entry;
        guint16 len1, len2, len3;

        if (g_strcmp0 (editor->ipv4_method, "manual") != 0)
                return;

        entry = GTK_ENTRY (WID (editor->builder, "ipv4_address"));
        len1 = gtk_entry_get_text_length (entry);

        entry = GTK_ENTRY (WID (editor->builder, "ipv4_netmask"));
        len2 = gtk_entry_get_text_length (entry);

        entry = GTK_ENTRY (WID (editor->builder, "ipv4_gateway"));
        len3 = gtk_entry_get_text_length (entry);

        if (len1 && len2 && len3)
                editor->update_ipv4 = TRUE;
        else
                editor->update_ipv4 = FALSE;

        net_connection_editor_update_apply (editor);
}

void
editor_update_ipv4 (NetConnectionEditor *editor)
{
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;

        gboolean ret;
        gchar *method, *address, *netmask, *gateway;
        GVariant *ipv4;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_IPV4, &ipv4,
                            -1);

        ret = g_variant_lookup (ipv4, "Method", "s", &method);
        if (!ret)
                method = "off";

        if (!g_strcmp0 (method, "off"))
                gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv4_method")), 0);
        else if (!g_strcmp0 (method, "dhcp")) {
                gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv4_method")), 1);

                ret = g_variant_lookup (ipv4, "Address", "s", &address);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_address")), address);
                else
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_address")), "");

                ret = g_variant_lookup (ipv4, "Netmask", "s", &netmask);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_netmask")), netmask);
                else
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_netmask")), "");

                ret = g_variant_lookup (ipv4, "Gateway", "s", &gateway);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_gateway")), gateway);
                else
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_gateway")), "");
        } else if (!g_strcmp0 (method, "manual")) {
                gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv4_method")), 2);

                ret = g_variant_lookup (ipv4, "Address", "s", &address);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_address")), address);
                else
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_address")), "");

                ret = g_variant_lookup (ipv4, "Netmask", "s", &netmask);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_netmask")), netmask);
                else
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_netmask")), "");

                ret = g_variant_lookup (ipv4, "Gateway", "s", &gateway);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_gateway")), gateway);
                else
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_gateway")), "");
        } else if (!g_strcmp0 (method, "fixed")) {
                gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv4_method")), 2);

                ret = g_variant_lookup (ipv4, "Address", "s", &address);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_address")), address);
                else
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_address")), "");

                ret = g_variant_lookup (ipv4, "Netmask", "s", &netmask);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_netmask")), netmask);
                else
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_netmask")), "");

                ret = g_variant_lookup (ipv4, "Gateway", "s", &gateway);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_gateway")), gateway);
                else
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_gateway")), "");
        }

        ipv4_setup_entry (editor, method);

        if (!g_strcmp0 (method, "fixed"))
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "comboboxtext_ipv4_method")), FALSE);
        else
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "comboboxtext_ipv4_method")), TRUE);

        editor->update_ipv4 = FALSE;
        net_connection_editor_update_apply (editor);
}

static void
service_set_ipv4 (GObject      *source,
                  GAsyncResult *res,
                  gpointer      user_data)
{
        NetConnectionEditor *editor = user_data;
        GError *error = NULL;

        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;

        if (!editor)
                return;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            -1);

        if (!service_call_set_property_finish (service, res, &error)) {
                g_warning ("Could not set ipv4: %s", error->message);
                g_error_free (error);
                return;
        }
}

static void
editor_set_ipv4 (NetConnectionEditor *editor)
{
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;
        gint active;

        GVariantBuilder *ipv4conf = g_variant_builder_new (G_VARIANT_TYPE_DICTIONARY);
        gchar *str;
        GVariant *value;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            -1);

        active = gtk_combo_box_get_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv4_method")));

        if (active == 0) {
                g_variant_builder_add (ipv4conf,"{sv}", "Method", g_variant_new_string ("off"));
        } else if (active == 1) {
                g_variant_builder_add (ipv4conf,"{sv}", "Method", g_variant_new_string ("dhcp"));
        } else {
                g_variant_builder_add (ipv4conf,"{sv}", "Method", g_variant_new_string ("manual"));

                str = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (editor->builder, "ipv4_address")));
                g_variant_builder_add (ipv4conf,"{sv}", "Address", g_variant_new_string (str));

                str = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (editor->builder, "ipv4_netmask")));
                g_variant_builder_add (ipv4conf,"{sv}", "Netmask", g_variant_new_string (str));

                str = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (editor->builder, "ipv4_gateway")));
                g_variant_builder_add (ipv4conf,"{sv}", "Gateway", g_variant_new_string (str));
        }

        value = g_variant_builder_end (ipv4conf);

        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_address")), "");
        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_netmask")), "");
        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv4_gateway")), "");

        service_call_set_property (service,
                                  "IPv4.Configuration",
                                   g_variant_new_variant (value),
                                   NULL,
                                   service_set_ipv4,
                                   editor);

        g_variant_builder_unref (ipv4conf);
}

/* IPv4 section Ends */

/* IPv6 section */

static void
ipv6_setup_entry (NetConnectionEditor *editor, gchar *method)
{
        editor->ipv6_method = method;

        if (!g_strcmp0 (method, "off")) {
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "ipv6_address")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "ipv6_prefix")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "ipv6_gateway")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "comboboxtext_ipv6_privacy")));

                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_ipv6_address")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_ipv6_prefix")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_ipv6_gateway")));
                gtk_widget_hide (GTK_WIDGET (WID (editor->builder, "label_ipv6_privacy")));

                return;
        } else {
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "ipv6_address")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "ipv6_prefix")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "ipv6_gateway")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "comboboxtext_ipv6_privacy")));

                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "label_ipv6_address")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "label_ipv6_prefix")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "label_ipv6_gateway")));
                gtk_widget_show (GTK_WIDGET (WID (editor->builder, "label_ipv6_privacy")));
        }

        if (!g_strcmp0 (method, "auto") ) {
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "comboboxtext_ipv6_privacy")), TRUE);
                gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv6_privacy")), 0);
        } else
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "comboboxtext_ipv6_privacy")), FALSE);

        if (!g_strcmp0 (method, "auto") || !g_strcmp0 (method, "fixed") || !g_strcmp0 (method, "6to4")) {
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "ipv6_address")), FALSE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "ipv6_prefix")), FALSE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "ipv6_gateway")), FALSE);
                return;
        }

        if (!g_strcmp0 (method, "manual")) {
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "ipv6_address")), TRUE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "ipv6_prefix")), TRUE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "ipv6_gateway")), TRUE);
        }
}

static void
ipv6_method_changed (GtkComboBox *combo, gpointer user_data)
{
        NetConnectionEditor *editor = user_data;
        gint active;

        active = gtk_combo_box_get_active (combo);
        if (active == 0) {
                ipv6_setup_entry (editor, "off");
                editor->update_ipv6 = TRUE;
                net_connection_editor_update_apply (editor);
        } else if (active == 1) {
                ipv6_setup_entry (editor, "auto");

                editor->update_ipv6 = TRUE;
                net_connection_editor_update_apply (editor);
        } else if (active == 2) {
                ipv6_setup_entry (editor, "manual");
        }
}

static void
ipv6_address_text_changed (NetConnectionEditor *editor)
{
        GtkEntry *entry;
        guint16 len1, len2, len3;

        if (g_strcmp0 (editor->ipv6_method, "manual") != 0)
                return;

        entry = GTK_ENTRY (WID (editor->builder, "ipv6_address"));
        len1 = gtk_entry_get_text_length (entry);

        entry = GTK_ENTRY (WID (editor->builder, "ipv6_prefix"));
        len2 = gtk_entry_get_text_length (entry);

        entry = GTK_ENTRY (WID (editor->builder, "ipv6_gateway"));
        len3 = gtk_entry_get_text_length (entry);

        if (len1 && len2 && len3)
                editor->update_ipv6 = TRUE;
        else
                editor->update_ipv6 = FALSE;

        net_connection_editor_update_apply (editor);
}

void
editor_update_ipv6 (NetConnectionEditor *editor)
{
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;

        gboolean ret;
        gchar *method, *address, *gateway, *privacy;
        GVariant *ipv6;
        guint8 prefix;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_IPV6, &ipv6,
                            -1);

        ret = g_variant_lookup (ipv6, "Method", "s", &method);
        if (!ret)
                method = "off";

        if (!g_strcmp0 (method, "off"))
                gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv6_method")), 0);
        else if (!g_strcmp0 (method, "auto")) {
                gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv6_method")), 1);

                ret = g_variant_lookup (ipv6, "Privacy", "s", &privacy);
                if (!ret)
                        privacy = "disabled";

                if (!g_strcmp0 (privacy , "enabled"))
                        gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv6_privacy")), 0);
                else if (!g_strcmp0 (privacy , "prefered"))
                        gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv6_privacy")), 2);
                else
                        gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv6_privacy")), 1);

                ret = g_variant_lookup (ipv6, "Address", "s", &address);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_address")), address);
                else
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_address")), "");

                ret = g_variant_lookup (ipv6, "PrefixLength", "y", &prefix);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_prefix")), g_strdup_printf ("%i", prefix));

                ret = g_variant_lookup (ipv6, "Gateway", "s", &gateway);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_gateway")), gateway);
                else
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_gateway")), "");
        } else if (!g_strcmp0 (method, "manual")) {
                gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv6_method")), 2);

                ret = g_variant_lookup (ipv6, "Address", "s", &address);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_address")), address);
                else
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_address")), "");

                ret = g_variant_lookup (ipv6, "PrefixLength", "y", &prefix);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_prefix")), g_strdup_printf ("%i", prefix));

                ret = g_variant_lookup (ipv6, "Gateway", "s", &gateway);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_gateway")), gateway);
                else
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_gateway")), "");
        } else if (!g_strcmp0 (method, "6to4")) {
                gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv6_method")), 3);

                ret = g_variant_lookup (ipv6, "Address", "s", &address);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_address")), address);

                ret = g_variant_lookup (ipv6, "PrefixLength", "y", &prefix);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_prefix")), g_strdup_printf ("%i", prefix));

                ret = g_variant_lookup (ipv6, "Gateway", "s", &gateway);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_gateway")), gateway);
        } else { /* fixed */
                gtk_combo_box_set_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv6_method")), 2);

                ret = g_variant_lookup (ipv6, "Address", "s", &address);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_address")), address);

                ret = g_variant_lookup (ipv6, "PrefixLength", "y", &prefix);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_prefix")), g_strdup_printf ("%i", prefix));

                ret = g_variant_lookup (ipv6, "Gateway", "s", &gateway);
                if (ret)
                        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_gateway")), gateway);
        }

        ipv6_setup_entry (editor, method);

        if (!g_strcmp0 (method, "fixed"))
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "comboboxtext_ipv6_method")), FALSE);
        else
                gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "comboboxtext_ipv6_method")), TRUE);

        editor->update_ipv6 = FALSE;
        net_connection_editor_update_apply (editor);
}

static void
service_set_ipv6 (GObject      *source,
                  GAsyncResult *res,
                  gpointer      user_data)
{
        NetConnectionEditor *editor = user_data;
        GError *error = NULL;

        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;

        if (!editor)
                return;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            -1);

        if (!service_call_set_property_finish (service, res, &error)) {
                g_warning ("Could not set ipv6: %s", error->message);
                g_error_free (error);
                return;
        }
}

static void
editor_set_ipv6 (NetConnectionEditor *editor)
{
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;
        gint active, priv;
        guint8 prefix_length;

        GVariantBuilder *ipv6conf = g_variant_builder_new (G_VARIANT_TYPE_DICTIONARY);
        gchar *str;
        GVariant *value;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            -1);

        active = gtk_combo_box_get_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv6_method")));

        if (active == 0) {
                g_variant_builder_add (ipv6conf,"{sv}", "Method", g_variant_new_string ("off"));
        } else if (active == 1) {
                g_variant_builder_add (ipv6conf,"{sv}", "Method", g_variant_new_string ("auto"));

                priv = gtk_combo_box_get_active (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv6_privacy")));
                if (priv == 0)
                        str = "disabled";
                else if (priv == 1)
                        str = "enabled";
                else
                        str = "prefered";

                g_variant_builder_add (ipv6conf,"{sv}", "Privacy", g_variant_new_string (str));
        } else {
                g_variant_builder_add (ipv6conf,"{sv}", "Method", g_variant_new_string ("manual"));

                str = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (editor->builder, "ipv6_address")));
                g_variant_builder_add (ipv6conf,"{sv}", "Address", g_variant_new_string (str));

                str = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (editor->builder, "ipv6_prefix")));
                prefix_length = (guint8) atoi (str);
                g_variant_builder_add (ipv6conf,"{sv}", "PrefixLength", g_variant_new_byte (prefix_length));

                str = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (editor->builder, "ipv6_gateway")));
                g_variant_builder_add (ipv6conf,"{sv}", "Gateway", g_variant_new_string (str));
        }

        value = g_variant_builder_end (ipv6conf);

        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_address")), "");
        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_prefix")), "");
        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "ipv6_gateway")), "");

        service_call_set_property (service,
                                  "IPv6.Configuration",
                                   g_variant_new_variant (value),
                                   NULL,
                                   service_set_ipv6,
                                   editor);

        g_variant_builder_unref (ipv6conf);
}
/* IPv6 section Ends */

/* Domains section */
static void
domains_text_changed (NetConnectionEditor *editor)
{
        editor->update_domains = TRUE;

        net_connection_editor_update_apply (editor);
}

void
editor_update_domains (NetConnectionEditor *editor)
{
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;

        gchar **dom;
        GVariant *domains;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_DOMAINS, &domains,
                            -1);

        dom = g_variant_get_strv (domains, NULL);
        if (dom && dom[0])
                gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "entry_domains")), g_strjoinv (",", (gchar **) dom));
        else
                gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "entry_domains")), "");

        editor->update_domains = FALSE;
        net_connection_editor_update_apply (editor);
}

static void
service_set_domains (GObject      *source,
                     GAsyncResult *res,
                     gpointer      user_data)
{
        NetConnectionEditor *editor = user_data;
        GError *error = NULL;

        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;

        if (!editor)
                return;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            -1);

        if (!service_call_set_property_finish (service, res, &error)) {
                g_warning ("Could not set domains: %s", error->message);
                g_error_free (error);
                return;
        }
}

static void
editor_set_domains (NetConnectionEditor *editor)
{
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;

        gchar *str;
        gchar **domains = NULL;
        GVariant *value;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            -1);

        str = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (editor->builder, "entry_domains")));
        if (str)
                domains = g_strsplit (str, ",", -1);

        value = g_variant_new_strv ((const gchar * const *) domains, -1);

        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "entry_domains")), "");

        service_call_set_property (service,
                                  "Domains.Configuration",
                                   g_variant_new_variant (value),
                                   NULL,
                                   service_set_domains,
                                   editor);
}

/* Domains section Ends */

/* Nameservers section */
static void
nameservers_text_changed (NetConnectionEditor *editor)
{
        GtkEntry *entry;
        guint16 len;

        entry = GTK_ENTRY (WID (editor->builder, "entry_nameservers"));
        len = gtk_entry_get_text_length (entry);

        if (len > 0)
                editor->update_nameservers = TRUE;
        else
                editor->update_nameservers = FALSE;

        net_connection_editor_update_apply (editor);
}

void
editor_update_nameservers (NetConnectionEditor *editor)
{
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;

        gchar **dom;
        GVariant *nameservers;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_NAMESERVERS, &nameservers,
                            -1);

        dom = g_variant_get_strv (nameservers, NULL);
        if (dom && dom[0])
                gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "entry_nameservers")), g_strjoinv (",", (gchar **) dom));
        else
                gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "entry_nameservers")), "");

        editor->update_nameservers = FALSE;
        net_connection_editor_update_apply (editor);
}

static void
service_set_nameservers (GObject      *source,
                     GAsyncResult *res,
                     gpointer      user_data)
{
        NetConnectionEditor *editor = user_data;
        GError *error = NULL;

        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;

        if (!editor)
                return;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            -1);

        if (!service_call_set_property_finish (service, res, &error)) {
                g_warning ("Could not set nameservers: %s", error->message);
                g_error_free (error);
                return;
        }
}

static void
editor_set_nameservers (NetConnectionEditor *editor)
{
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;

        gchar *str;
        gchar **nameservers = NULL;
        GVariant *value;

        model =  gtk_tree_row_reference_get_model (editor->service_row);
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter,
                            COLUMN_GDBUSPROXY, &service,
                            -1);

        str = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (editor->builder, "entry_nameservers")));
        if (str)
                nameservers = g_strsplit (str, ",", -1);

        value = g_variant_new_strv ((const gchar * const *) nameservers, -1);

        gtk_entry_set_text (GTK_ENTRY (WID (editor->builder, "entry_nameservers")), "");

        service_call_set_property (service,
                                  "Nameservers.Configuration",
                                   g_variant_new_variant (value),
                                   NULL,
                                   service_set_nameservers,
                                   editor);
}

/* Nameservers section Ends */

static void
cancel_editing (NetConnectionEditor *editor)
{
        g_signal_emit (editor, signals[DONE], 0, FALSE);
}

static void
apply_edits (NetConnectionEditor *editor)
{

        if (editor->update_proxy)
               editor_set_proxy (editor);
        if (editor->update_ipv4)
               editor_set_ipv4 (editor);
        if (editor->update_ipv6)
               editor_set_ipv6 (editor);
        if (editor->update_domains)
               editor_set_domains (editor);
        if (editor->update_nameservers)
               editor_set_nameservers (editor);
        if (editor->update_autoconnect)
               editor_set_autoconnect (editor);

        gtk_widget_set_sensitive (GTK_WIDGET (WID (editor->builder, "apply_button")), FALSE);
        gtk_widget_hide (GTK_WIDGET (editor->window));
}

static void
selection_changed (GtkTreeSelection *selection, NetConnectionEditor *editor)
{
        GtkWidget *widget;
        GtkTreeModel *model;
        GtkTreeIter iter;
        gint page;

        gtk_tree_selection_get_selected (selection, &model, &iter);
        gtk_tree_model_get (model, &iter, 1, &page, -1);

        widget = GTK_WIDGET (gtk_builder_get_object (editor->builder,
                                                     "details_notebook"));
        gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), page);
}

static void
net_connection_editor_init (NetConnectionEditor *editor)
{
        GError *error = NULL;
        GtkTreeSelection *selection;
        GtkWidget *button;

        editor->builder = gtk_builder_new ();

        gtk_builder_add_from_resource (editor->builder,
                                       "/org/gnome/control-center/network/connection-editor.ui",
                                       &error);
        if (error != NULL) {
                g_warning ("Could not load ui file: %s", error->message);
                g_error_free (error);
                return;
        }

        editor->window = GTK_WIDGET (gtk_builder_get_object (editor->builder, "dialog_ce"));
        selection = GTK_TREE_SELECTION (gtk_builder_get_object (editor->builder,
                                                                "treeview-selection"));
        g_signal_connect (selection, "changed",
                          G_CALLBACK (selection_changed), editor);

        button = GTK_WIDGET (gtk_builder_get_object (editor->builder, "cancel_button"));
        g_signal_connect_swapped (button, "clicked",
                                  G_CALLBACK (cancel_editing), editor);

        g_signal_connect_swapped (editor->window, "delete-event",
                                  G_CALLBACK (cancel_editing), editor);

        button = GTK_WIDGET (gtk_builder_get_object (editor->builder, "apply_button"));
        g_signal_connect_swapped (button, "clicked",
                                  G_CALLBACK (apply_edits), editor);

        button = GTK_WIDGET (gtk_builder_get_object (editor->builder, "switch_autoconnect"));
        g_signal_connect_swapped (button, "notify::active",
                                  G_CALLBACK (autoconnect_toggle), editor);

        button = GTK_WIDGET (gtk_builder_get_object (editor->builder, "button_forget"));
        g_signal_connect_swapped (button, "clicked",
                                  G_CALLBACK (forget_service), editor);

        g_signal_connect (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_proxy_method")),
                          "changed",
                          G_CALLBACK (proxy_method_changed),
                          editor);

        g_signal_connect_swapped (WID (editor->builder, "proxy_url"),
                                  "changed",
                                  G_CALLBACK (proxy_url_text_changed),
                                  editor);

        g_signal_connect_swapped (WID (editor->builder, "http_proxy_servers"),
                                  "changed",
                                  G_CALLBACK (http_proxy_servers_text_changed),
                                  editor);

        g_signal_connect_swapped (WID (editor->builder, "adjustment_http_port"),
                                  "value-changed",
                                  G_CALLBACK (http_proxy_servers_text_changed),
                                  editor);

        g_signal_connect_swapped (WID (editor->builder, "https_proxy_servers"),
                                  "changed",
                                  G_CALLBACK (https_proxy_servers_text_changed),
                                  editor);

        g_signal_connect_swapped (WID (editor->builder, "adjustment_https_port"),
                                  "value-changed",
                                  G_CALLBACK (https_proxy_servers_text_changed),
                                  editor);

        g_signal_connect_swapped (WID (editor->builder, "socks_proxy_servers"),
                                  "changed",
                                  G_CALLBACK (socks_proxy_servers_text_changed),
                                  editor);

        g_signal_connect_swapped (WID (editor->builder, "adjustment_socks_port"),
                                  "value-changed",
                                  G_CALLBACK (socks_proxy_servers_text_changed),
                                  editor);

        g_signal_connect_swapped (WID (editor->builder, "socks_proxy_v4"),
                                  "toggled",
                                  G_CALLBACK (socks_proxy_servers_text_changed),
                                  editor);

        g_signal_connect_swapped (WID (editor->builder, "socks_proxy_v5"),
                                  "toggled",
                                  G_CALLBACK (socks_proxy_servers_text_changed),
                                  editor);

        g_signal_connect_swapped (WID (editor->builder, "proxy_excludes"),
                                  "changed",
                                  G_CALLBACK (proxy_excludes_text_changed),
                                  editor);

        g_signal_connect (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv4_method")),
                          "changed",
                          G_CALLBACK (ipv4_method_changed),
                          editor);

        g_signal_connect_swapped (WID (editor->builder, "ipv4_address"),
                                  "changed",
                                  G_CALLBACK (ipv4_address_text_changed),
                                  editor);

        g_signal_connect_swapped (WID (editor->builder, "ipv4_netmask"),
                                  "changed",
                                  G_CALLBACK (ipv4_address_text_changed),
                                  editor);

        g_signal_connect_swapped (WID (editor->builder, "ipv4_gateway"),
                                  "changed",
                                  G_CALLBACK (ipv4_address_text_changed),
                                  editor);

        g_signal_connect (GTK_COMBO_BOX (WID (editor->builder, "comboboxtext_ipv6_method")),
                          "changed",
                          G_CALLBACK (ipv6_method_changed),
                          editor);

        g_signal_connect_swapped (WID (editor->builder, "ipv6_address"),
                                  "changed",
                                  G_CALLBACK (ipv6_address_text_changed),
                                  editor);

        g_signal_connect_swapped (WID (editor->builder, "ipv6_prefix"),
                                  "changed",
                                  G_CALLBACK (ipv6_address_text_changed),
                                  editor);

        g_signal_connect_swapped (WID (editor->builder, "ipv6_gateway"),
                                  "changed",
                                  G_CALLBACK (ipv6_address_text_changed),
                                  editor);

        g_signal_connect_swapped (WID (editor->builder, "entry_domains"),
                                  "changed",
                                  G_CALLBACK (domains_text_changed),
                                  editor);
        g_signal_connect_swapped (WID (editor->builder, "entry_nameservers"),
                                  "changed",
                                  G_CALLBACK (nameservers_text_changed),
                                  editor);
}

static void
net_connection_editor_finalize (GObject *object)
{
        NetConnectionEditor *editor = NET_CONNECTION_EDITOR (object);

        if (editor->window) {
                gtk_widget_destroy (editor->window);
                editor->window = NULL;
        }
        g_clear_object (&editor->parent_window);
        g_clear_object (&editor->builder);

        G_OBJECT_CLASS (net_connection_editor_parent_class)->finalize (object);
}

static void
net_connection_editor_class_init (NetConnectionEditorClass *class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (class);

        g_resources_register (cc_network_get_resource ());

        object_class->finalize = net_connection_editor_finalize;

        signals[DONE] = g_signal_new ("done",
                                      G_OBJECT_CLASS_TYPE (object_class),
                                      G_SIGNAL_RUN_FIRST,
                                      G_STRUCT_OFFSET (NetConnectionEditorClass, done),
                                      NULL, NULL,
                                      NULL,
                                      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

NetConnectionEditor *
net_connection_editor_new (GtkWindow *parent_window,
                           GtkTreeRowReference *row)
{
        NetConnectionEditor *editor;

        editor = g_object_new (NET_TYPE_CONNECTION_EDITOR, NULL);

        if (parent_window) {
                editor->parent_window = g_object_ref (parent_window);
                gtk_window_set_transient_for (GTK_WINDOW (editor->window),
                                              parent_window);
        }

        editor->service_row = row;

        editor_update_details (editor);
        editor_update_proxy (editor);
        editor_update_ipv4 (editor);
        editor_update_ipv6 (editor);
        editor_update_domains (editor);
        editor_update_nameservers (editor);

        gtk_window_present (GTK_WINDOW (editor->window));

        return editor;
}
