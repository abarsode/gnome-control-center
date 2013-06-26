/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010-2012 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2012 Thomas Bechtold <thomasbechtold@jpberlin.de>
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2013 Alok Barsode <alok.barsode@intel.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <stdlib.h>

#include "cc-network-panel.h"
#include "cc-network-resources.h"
#include "panel-cell-renderer-text.h"
#include "panel-cell-renderer-pixbuf.h"

#include "net-connection-editor.h"
#include "manager.h"
#include "technology.h"
#include "service.h"

#define WID(b, w) (GtkWidget *) gtk_builder_get_object (b, w)

static void
editor_done (NetConnectionEditor *editor,
             gboolean success,
             gpointer user_data);


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

enum {
        STATUS_UNAVAILABLE,
        STATUS_OFFLINE,
        STATUS_IDLE,
        STATUS_CONNECTING,
        STATUS_READY,
        STATUS_ONLINE,
        STATUS_TETHERED,
        STATUS_LAST
};

static gint
status_to_int (const gchar *status)
{
        if (g_strcmp0 (status , "offline") == 0)
                return STATUS_OFFLINE;
        else if (g_strcmp0 (status , "idle") == 0)
                return STATUS_IDLE;
        else if (g_strcmp0 (status , "ready") == 0)
                return STATUS_READY;
        else if (g_strcmp0 (status , "online") == 0)
                return STATUS_ONLINE;
        else if (g_strcmp0 (status , "failure") == 0)
                return STATUS_IDLE;
        else if ((g_strcmp0 (status, "association") == 0) || (g_strcmp0 (status, "configuration") == 0))
                return STATUS_CONNECTING;
        else
                return STATUS_UNAVAILABLE;
}

static gboolean cc_service_state_to_icon (const gchar *state)
{
        if ((g_strcmp0 (state, "association") == 0) || (g_strcmp0 (state, "configuration") == 0))
                return TRUE;
        else
                return FALSE;
}

static const gchar *cc_service_security_to_string (const gchar **security)
{
        if (security == NULL)
                return "none";
        else
                return security[0];
}

static const gchar *cc_service_security_to_icon (const gchar **security)
{
        if (security == NULL)
                return NULL;
        
        if (!g_strcmp0 (security[0], "none"))
                return NULL;
        else if (!g_strcmp0 (security[0], "wep") || !g_strcmp0 (security[0], "wps") || !g_strcmp0 (security[0], "psk"))
                return "network-wireless-encrypted-symbolic";
        else if (!g_strcmp0 (security[0], "ieee8021x"))
                return "connman_corporate";
        else
                return NULL;
}

static const gchar *cc_service_strength_to_string (const gchar *type, const gchar strength)
{
        if (!g_strcmp0 (type, "ethernet") || !g_strcmp0 (type, "bluetooth"))
                return "Excellent";

        if (strength > 80)
                return "Excellent";
        else if (strength > 55)
                return "Good";
        else if (strength > 30)
                return "ok";
        else if (strength > 5)
                return "weak";
        else
                return "n/a";
}

static const gchar *cc_service_type_to_icon (const gchar *type, gchar strength)
{
        if (g_strcmp0 (type, "ethernet") == 0)
                return "network-wired-symbolic";
        else if (g_strcmp0 (type, "bluetooth") == 0)
                return "bluetooth-active-symbolic";
        else if (g_strcmp0 (type, "wifi") == 0) {
                if (strength > 80)
                        return "network-wireless-signal-excellent-symbolic";
                else if (strength > 55)
                        return "network-wireless-signal-good-symbolic";
                else if (strength > 30)
                        return "network-wireless-signal-ok-symbolic";
                else
                        return "network-wireless-signal-excellent-symbolic";
        } else if (g_strcmp0 (type, "cellular") == 0) {
                if (strength > 80)
                        return "network-cellular-signal-excellent-symbolic";
                else if (strength > 55)
                        return "network-cellular-signal-good-symbolic";
                else if (strength > 30)
                        return "network-cellular-signal-ok-symbolic";
                else
                        return "network-cellular-signal-excellent-symbolic";
        } else
                return NULL;
}

CC_PANEL_REGISTER (CcNetworkPanel, cc_network_panel)

#define NETWORK_PANEL_PRIVATE(o) \
        (G_TYPE_INSTANCE_GET_PRIVATE ((o), CC_TYPE_NETWORK_PANEL, CcNetworkPanelPrivate))

struct _CcNetworkPanelPrivate
{
        gint            watch_id;

        GtkBuilder      *builder;
        GCancellable    *cancellable;
        ConnManManager         *manager;
        gint            global_state;

        gboolean        offlinemode;

        gboolean        tech_update;
        gboolean        serv_update;

        Technology      *ethernet;
        gint            ethernet_id;
        gboolean        ethernet_powered;

        Technology      *wifi;
        gint            wifi_id;
        gboolean        wifi_powered;

        Technology      *bluetooth;
        gint            bluetooth_id;
        gboolean        bluetooth_powered;

        Technology      *cellular;
        gint            cellular_id;
        gboolean        cellular_powered;

        gint            tech_added_id;
        gint            tech_removed_id;
        gint            mgr_prop_id;
        gint            serv_id;

        GtkDialog       *tethering;
        gboolean        ethernet_tethered;
        gboolean        wifi_tethered;
        gboolean        bluetooth_tethered;

        gboolean        tether_wifi_toggle;
        gboolean        tether_bt_toggle;
        gboolean        tether_ethernet_toggle;

};

GHashTable *services;

static gboolean
clear_list_store (GtkTreeModel *model,
                  GtkTreePath *path,
                  GtkTreeIter *iter,
                  gpointer data)
{
        gint prop_id;
        guint pulse_id;
        Service *service;

        if (!iter || !model)
                return TRUE;

        gtk_tree_model_get (model, iter,
                            COLUMN_PROP_ID,
                            &prop_id,
                            COLUMN_PULSE_ID,
                            &pulse_id,
                            COLUMN_GDBUSPROXY,
                            &service,
                            -1);

        if (service && prop_id)
                g_signal_handler_disconnect (service, prop_id);

        if (pulse_id)
                g_source_remove (pulse_id);

        if (service)
                g_object_unref (service);

        return FALSE;
}

static void
cc_network_panel_dispose (GObject *object)
{
        CcNetworkPanelPrivate *priv = CC_NETWORK_PANEL (object)->priv;
        GtkListStore *liststore;

        if (priv->cancellable != NULL)
                g_cancellable_cancel (priv->cancellable);

        g_clear_object (&priv->cancellable);

        if (priv->watch_id)
                g_bus_unwatch_name (priv->watch_id);

        priv->watch_id = 0;

        if (priv->manager) {
                g_object_unref (priv->manager);
                priv->manager = NULL;
        }

        if (priv->wifi) {
                g_object_unref (priv->wifi);
                priv->wifi = NULL;
        }

        if (priv->bluetooth) {
                g_object_unref (priv->bluetooth);
                priv->bluetooth = NULL;
        }

        if (priv->ethernet) {
                g_object_unref (priv->ethernet);
                priv->ethernet = NULL;
        }

        if (priv->cellular) {
                g_object_unref (priv->cellular);
                priv->cellular = NULL;
        }

        if (priv->builder) {
                liststore = GTK_LIST_STORE (WID (priv->builder, "liststore_services"));

                gtk_tree_model_foreach (GTK_TREE_MODEL (liststore), clear_list_store, NULL);

                gtk_list_store_clear (liststore);

                g_object_unref (priv->builder);
                priv->builder = NULL;
        }

        g_hash_table_remove_all (services);

        G_OBJECT_CLASS (cc_network_panel_parent_class)->dispose (object);
}

static void
cc_network_panel_finalize (GObject *object)
{
        //CcNetworkPanel *panel = CC_NETWORK_PANEL (object);

        G_OBJECT_CLASS (cc_network_panel_parent_class)->finalize (object);
}

static const char *
cc_network_panel_get_help_uri (CcPanel *panel)
{
	return "help:gnome-help/net";
}

static void
cc_network_panel_class_init (CcNetworkPanelClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
	CcPanelClass *panel_class = CC_PANEL_CLASS (klass);

        g_type_class_add_private (klass, sizeof (CcNetworkPanelPrivate));

	panel_class->get_help_uri = cc_network_panel_get_help_uri;

        object_class->dispose = cc_network_panel_dispose;
        object_class->finalize = cc_network_panel_finalize;
}

static void
manager_set_offlinemode (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
        CcNetworkPanel *panel = user_data;
        CcNetworkPanelPrivate *priv = panel->priv;
        gboolean offline;
        GError *error = NULL;
        gint err_code;

        if (!conn_man_manager_call_set_property_finish (priv->manager, res, &error)) {
                g_warning ("Manager: Could not set OfflineMode: %s", error->message);
                err_code = error->code;
                g_error_free (error);

                /* Reset the switch */
                if (err_code != 36) { /* if not AlreadyEnabled */
                        offline = gtk_switch_get_active (GTK_SWITCH (WID (priv->builder, "switch_offline")));
                        gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_offline")), !offline);
                }

                return;
        }
}

static void
offline_switch_toggle (GtkSwitch *sw,
                       GParamSpec *pspec,
                       CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;
        gboolean offline;
        GVariant *value;

        offline = gtk_switch_get_active (sw);

        if (priv->offlinemode == offline)
                return;

        value = g_variant_new_boolean (offline);

        conn_man_manager_call_set_property (priv->manager,
                                   "OfflineMode",
                                   g_variant_new_variant (value),
                                   priv->cancellable,
                                   manager_set_offlinemode,
                                   panel);
}

static void
network_set_status (CcNetworkPanel *panel, gint status)
{
        CcNetworkPanelPrivate *priv;
        priv = NETWORK_PANEL_PRIVATE (panel);

        switch (status) {
        case STATUS_UNAVAILABLE:
                gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "icon_status")),
                                      "connman_unavailable",
                                      GTK_ICON_SIZE_BUTTON);
                gtk_label_set_text (GTK_LABEL (WID (priv->builder, "label_status")), _("Unavailable"));
                break;
        case STATUS_OFFLINE:
                gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "icon_status")),
                                      "connman_offline",
                                      GTK_ICON_SIZE_BUTTON);
                gtk_label_set_text (GTK_LABEL (WID (priv->builder, "label_status")), _("In-flight mode"));
                break;
        case STATUS_IDLE:
                gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "icon_status")),
                                      "connman_noconn",
                                      GTK_ICON_SIZE_BUTTON);
                gtk_label_set_text (GTK_LABEL (WID (priv->builder, "label_status")), _("Not Connected"));
                break;
        case STATUS_CONNECTING:
                gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "icon_status")),
                                      "connman_conn",
                                      GTK_ICON_SIZE_BUTTON);
                gtk_label_set_text (GTK_LABEL (WID (priv->builder, "label_status")), _("Connecting"));
                break;
        case STATUS_READY:
                gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "icon_status")),
                                      "connman_online",
                                      GTK_ICON_SIZE_BUTTON);
                gtk_label_set_text (GTK_LABEL (WID (priv->builder, "label_status")), _("Connected"));
                break;
        case STATUS_ONLINE:
                gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "icon_status")),
                                      "connman_online",
                                      GTK_ICON_SIZE_BUTTON);
                gtk_label_set_text (GTK_LABEL (WID (priv->builder, "label_status")), _("Online"));
                break;
        case STATUS_TETHERED:
                gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "icon_status")),
                                      "connman_hotspot",
                                      GTK_ICON_SIZE_BUTTON);
                gtk_label_set_text (GTK_LABEL (WID (priv->builder, "label_status")), _("Hotspot"));
                break;
        default:
                return;
        }
}

static void
panel_set_scan_cb (GObject      *source,
                   GAsyncResult *res,
                   gpointer      user_data)
{
        GError *error = NULL;
        gint err_code;

        if (!technology_call_scan_finish ((Technology *) source, res, &error)) {
                err_code = error->code;

                /* Reset the switch if error is not AlreadyEnabled/AlreadyDisabled */
                if (err_code != 36)
                        g_warning ("Could not set scan wifi: %s", error->message);

                g_error_free (error);
                return;
        }
}

static void
panel_set_scan (CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv;
        priv = NETWORK_PANEL_PRIVATE (panel);

        if (!priv->wifi)
                return;

        technology_call_scan (priv->wifi,
                              priv->cancellable,
                              panel_set_scan_cb,
                              NULL);
}

/* Technology section starts */
/* Ethernet section starts*/
static void
ethernet_property_changed (Technology *ethernet,
                          const gchar *property,
                          GVariant *value,
                          CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;
        gboolean powered;

        if (!g_strcmp0 (property, "Powered")) {
                powered  = g_variant_get_boolean (g_variant_get_variant (value));

                priv->ethernet_powered = powered;

                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_ethernet")), powered);
                gtk_widget_set_sensitive (WID (priv->builder, "switch_tether_ethernet"), powered);
        }

        if (!g_strcmp0 (property, "Tethering")) {
                priv->ethernet_tethered  = g_variant_get_boolean (g_variant_get_variant (value));
                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_tether_ethernet")), priv->ethernet_tethered);
                if (priv->ethernet_tethered)
                        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_ethernet")),
                                                      "connman_hotspot",
                                                      GTK_ICON_SIZE_BUTTON);
                else
                        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_ethernet")),
                                                      "network-wired-symbolic",
                                                      GTK_ICON_SIZE_BUTTON);
        }
}

static void
ethernet_set_powered (GObject      *source,
                      GAsyncResult *res,
                      gpointer      user_data)
{
        CcNetworkPanel *panel = user_data;
        CcNetworkPanelPrivate *priv = panel->priv;
        gboolean powered;
        GError *error = NULL;
        gint err_code;

        if (!priv->ethernet)
                return;

        if (!technology_call_set_property_finish (priv->ethernet, res, &error)) {
                err_code = error->code;

                /* Reset the switch if error is not AlreadyEnabled/AlreadyDisabled */
                if (err_code != 36) {
                        powered = gtk_switch_get_active (GTK_SWITCH (WID (priv->builder, "switch_ethernet")));
                        gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_ethernet")), !powered);
                        g_warning ("Could not set ethernet Powered property: %s", error->message);
                }

                g_error_free (error);
                return;
        }
}

static void
cc_ethernet_switch_toggle (GtkSwitch *sw,
                           GParamSpec *pspec,
                           CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;
        gboolean powered;
        GVariant *value;

        if (priv->ethernet == NULL)
                return;

        powered = gtk_switch_get_active (sw);

        if (priv->ethernet_powered == powered)
                return;

        value = g_variant_new_boolean (powered);

        technology_call_set_property (priv->ethernet,
                                      "Powered",
                                      g_variant_new_variant (value),
                                      priv->cancellable,
                                      ethernet_set_powered,
                                      panel);
}

static void
cc_add_technology_ethernet (const gchar         *path,
                            GVariant            *properties,
                            CcNetworkPanel      *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;

        GError *error = NULL;
        gboolean powered;
        gboolean tethering;

        if (priv->ethernet == NULL) {
                priv->ethernet = technology_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                                    G_DBUS_PROXY_FLAGS_NONE,
                                                                    "net.connman",
                                                                    path,
                                                                    priv->cancellable,
                                                                    &error);

                if (error != NULL) {
                        g_warning ("Could not get proxy for ethernet: %s", error->message);
                        g_error_free (error);
                        return;
                }

                gtk_widget_set_sensitive (WID (priv->builder, "box_ethernet"), TRUE);

                priv->ethernet_id = g_signal_connect (priv->ethernet,
                                                      "property_changed",
                                                      G_CALLBACK (ethernet_property_changed),
                                                      panel);

                priv->tech_update = TRUE;
        }

        if (g_variant_lookup (properties, "Powered", "b", &powered)) {
                priv->ethernet_powered = powered;
                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_ethernet")), powered);
                gtk_widget_set_sensitive (WID (priv->builder, "switch_tether_ethernet"), powered);
        }

        if (g_variant_lookup (properties, "Tethering", "b", &tethering)) {
                priv->ethernet_tethered  = tethering;
                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_tether_ethernet")), priv->ethernet_tethered);
                if (priv->ethernet_tethered)
                        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_ethernet")),
                                                      "connman_hotspot",
                                                      GTK_ICON_SIZE_BUTTON);
                else
                        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_ethernet")),
                                                      "network-wired-symbolic",
                                                      GTK_ICON_SIZE_BUTTON);
        }
}

static void
cc_remove_technology_ethernet (CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;

        if (priv->ethernet == NULL) {
                g_warning ("Unable to remove ethernet technology");
                return;
        }

        g_signal_handler_disconnect (priv->ethernet, priv->ethernet_id);

        g_object_unref (priv->ethernet);
        priv->ethernet = NULL;

        gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_ethernet")), FALSE);
        gtk_widget_set_sensitive (WID (priv->builder, "box_ethernet"), FALSE);
        gtk_widget_set_sensitive (WID (priv->builder, "switch_tether_ethernet"), FALSE);

        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_ethernet")),
                                      "network-wired-symbolic",
                                      GTK_ICON_SIZE_BUTTON);
}

/* Ethernet section ends*/

/* Wifi section starts*/
static void
wifi_property_changed (Technology *wifi,
                       const gchar *property,
                       GVariant *value,
                       CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv;
        gboolean powered;

        priv = panel->priv;

        if (!g_strcmp0 (property, "Powered")) {
                powered  = g_variant_get_boolean (g_variant_get_variant (value));

                priv->wifi_powered = powered;

                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_wifi")), powered);
                gtk_widget_set_sensitive (WID (priv->builder, "switch_tether_wifi"), powered);
        } else if (!g_strcmp0 (property, "Tethering")) {
                priv->wifi_tethered  = g_variant_get_boolean (g_variant_get_variant (value));
                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_tether_wifi")), priv->wifi_tethered);
                if (priv->wifi_tethered)
                        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_wifi")),
                                                      "connman_hotspot",
                                                      GTK_ICON_SIZE_BUTTON);
                else
                        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_wifi")),
                                                      "network-wireless-signal-excellent-symbolic",
                                                      GTK_ICON_SIZE_BUTTON);
        } else if (!g_strcmp0 (property, "TetheringIdentifier")) {
                gtk_entry_set_text (GTK_ENTRY (WID (priv->builder, "entry_ssid")), g_variant_get_string (g_variant_get_variant (value), NULL));
        } else if (!g_strcmp0 (property, "TetheringPassphrase")) {
                gtk_entry_set_text (GTK_ENTRY (WID (priv->builder, "entry_passphrase")), g_variant_get_string (g_variant_get_variant (value), NULL));
        }
}

static void
wifi_set_powered (GObject      *source,
                  GAsyncResult *res,
                  gpointer      user_data)
{
        CcNetworkPanel *panel = user_data;
        CcNetworkPanelPrivate *priv;
        gboolean powered;
        GError *error = NULL;
        gint err_code;

        priv = panel->priv;

        if (!priv->wifi)
                return;

        if (!technology_call_set_property_finish (priv->wifi, res, &error)) {
                err_code = error->code;

                /* Reset the switch if error is not AlreadyEnabled/AlreadyDisabled */
                if (err_code != 36) {
                        powered = gtk_switch_get_active (GTK_SWITCH (WID (priv->builder, "switch_wifi")));
                        gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_wifi")), !powered);
                        g_warning ("Could not set wifi Powered property: %s", error->message);
                }

                g_error_free (error);
                return;
        }
}

static void
cc_wifi_switch_toggle (GtkSwitch *sw,
                       GParamSpec *pspec,
                       CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;
        gboolean powered;
        GVariant *value;

        if (priv->wifi == NULL)
                return;

        powered = gtk_switch_get_active (sw);

        if (priv->wifi_powered == powered)
                return;

        value = g_variant_new_boolean (powered);

        technology_call_set_property (priv->wifi,
                                      "Powered",
                                      g_variant_new_variant (value),
                                      priv->cancellable,
                                      wifi_set_powered,
                                      panel);
}

static void
cc_add_technology_wifi (const gchar         *path,
                        GVariant            *properties,
                        CcNetworkPanel      *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;

        GError *error = NULL;
        gboolean powered;
        gboolean tethering;
        gchar *str;

        if (priv->wifi == NULL) {
                priv->wifi = technology_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                                    G_DBUS_PROXY_FLAGS_NONE,
                                                                    "net.connman",
                                                                    path,
                                                                    priv->cancellable,
                                                                    &error);

                if (error != NULL) {
                        g_warning ("Could not get proxy for wifi: %s", error->message);
                        g_error_free (error);
                        return;
                }

                gtk_widget_set_sensitive (WID (priv->builder, "box_wifi"), TRUE);

                priv->wifi_id = g_signal_connect (priv->wifi,
                                                      "property_changed",
                                                      G_CALLBACK (wifi_property_changed),
                                                      panel);

                priv->tech_update = TRUE;
        }

        if (g_variant_lookup (properties, "Powered", "b", &powered)) {
                priv->wifi_powered = powered;
                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_wifi")), powered);
                gtk_widget_set_sensitive (WID (priv->builder, "switch_tether_wifi"), powered);
        }

        if (g_variant_lookup (properties, "Tethering", "b", &tethering)) {
                priv->wifi_tethered  = tethering;
                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_tether_wifi")), priv->wifi_tethered);

                if (priv->wifi_tethered)
                        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_wifi")),
                                                      "connman_hotspot",
                                                      GTK_ICON_SIZE_BUTTON);
                else
                        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_wifi")),
                                                      "network-wireless-signal-excellent-symbolic",
                                                      GTK_ICON_SIZE_BUTTON);
        }

        if (g_variant_lookup (properties, "TetheringIdentifier", "s", &str)) {
                gtk_entry_set_text (GTK_ENTRY (WID (priv->builder, "entry_ssid")), str);
        }

        if (g_variant_lookup (properties, "TetheringPassphrase", "s", &str)) {
                gtk_entry_set_text (GTK_ENTRY (WID (priv->builder, "entry_passphrase")), str);
        }
}

static void
cc_remove_technology_wifi (CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;

        if (priv->wifi == NULL) {
                g_warning ("Unable to remove wifi technology");
                return;
        }

        g_signal_handler_disconnect (priv->wifi, priv->wifi_id);

        g_object_unref (priv->wifi);
        priv->wifi = NULL;

        gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_wifi")), FALSE);
        gtk_widget_set_sensitive (WID (priv->builder, "box_wifi"), FALSE);
        gtk_widget_set_sensitive (WID (priv->builder, "switch_tether_wifi"), FALSE);

        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_wifi")),
                                      "network-wireless-signal-excellent-symbolic",
                                      GTK_ICON_SIZE_BUTTON);
}

/* Wifi section ends*/

/* Bluetooth section starts*/
static void
bluetooth_property_changed (Technology *bluetooth,
                            const gchar *property,
                            GVariant *value,
                            CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;
        gboolean powered;

        if (!g_strcmp0 (property, "Powered")) {
                powered  = g_variant_get_boolean (g_variant_get_variant (value));

                priv->bluetooth_powered = powered;

                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_bluetooth")), powered);

                gtk_widget_set_sensitive (WID (priv->builder, "switch_tether_bt"), powered);
        }
        if (!g_strcmp0 (property, "Tethering")) {
                priv->bluetooth_tethered  = g_variant_get_boolean (g_variant_get_variant (value));
                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_tether_bt")), priv->bluetooth_tethered);

                if (priv->bluetooth_tethered)
                        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_bluetooth")),
                                                      "connman_hotspot",
                                                      GTK_ICON_SIZE_BUTTON);
                else
                        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_bluetooth")),
                                                      "bluetooth-active-symbolic",
                                                      GTK_ICON_SIZE_BUTTON);
        }
}

static void
bluetooth_set_powered (GObject      *source,
                       GAsyncResult *res,
                       gpointer      user_data)
{
        CcNetworkPanel *panel = user_data;
        CcNetworkPanelPrivate *priv = panel->priv;
        gboolean powered;
        GError *error = NULL;
        gint err_code;

        if (!priv->bluetooth)
                return;

        if (!technology_call_set_property_finish (priv->bluetooth, res, &error)) {
                err_code = error->code;

                /* Reset the switch if error is not AlreadyEnabled/AlreadyDisabled */
                if (err_code != 36) {
                        powered = gtk_switch_get_active (GTK_SWITCH (WID (priv->builder, "switch_bluetooth")));
                        gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_bluetooth")), !powered);
                        g_warning ("Could not set bluetooth Powered property: %s", error->message);
                }

                g_error_free (error);
                return;
        }
}

static void
cc_bluetooth_switch_toggle (GtkSwitch *sw,
                            GParamSpec *pspec,
                            CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;
        gboolean powered;
        GVariant *value;

        if (priv->bluetooth == NULL)
                return;

        powered = gtk_switch_get_active (sw);

        if (priv->bluetooth_powered == powered)
                return;

        value = g_variant_new_boolean (powered);

        technology_call_set_property (priv->bluetooth,
                                      "Powered",
                                      g_variant_new_variant (value),
                                      priv->cancellable,
                                      bluetooth_set_powered,
                                      panel);
}

static void
cc_add_technology_bluetooth (const gchar         *path,
                             GVariant            *properties,
                             CcNetworkPanel      *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;

        GError *error = NULL;
        gboolean powered;
        gboolean tethering;

        if (priv->bluetooth == NULL) {
                priv->bluetooth = technology_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                                    G_DBUS_PROXY_FLAGS_NONE,
                                                                    "net.connman",
                                                                    path,
                                                                    priv->cancellable,
                                                                    &error);

                if (error != NULL) {
                        g_warning ("Could not get proxy for bluetooth: %s", error->message);
                        g_error_free (error);
                        return;
                }

                gtk_widget_set_sensitive (WID (priv->builder, "box_bluetooth"), TRUE);

                priv->bluetooth_id = g_signal_connect (priv->bluetooth,
                                                      "property_changed",
                                                      G_CALLBACK (bluetooth_property_changed),
                                                      panel);

                priv->tech_update = TRUE;
        }

        if (g_variant_lookup (properties, "Powered", "b", &powered)) {
                priv->bluetooth_powered = powered;
                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_bluetooth")), powered);
                gtk_widget_set_sensitive (WID (priv->builder, "switch_tether_bt"), powered);
        }

        if (g_variant_lookup (properties, "Tethering", "b", &tethering)) {
                priv->bluetooth_tethered  = tethering;
                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_tether_bt")), priv->bluetooth_tethered);
                if (tethering)
                        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_bluetooth")),
                                                      "connman_hotspot",
                                                      GTK_ICON_SIZE_BUTTON);
                else
                        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_bluetooth")),
                                                      "bluetooth-active-symbolic",
                                                      GTK_ICON_SIZE_BUTTON);
        }
}

static void
cc_remove_technology_bluetooth (CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;

        if (priv->bluetooth == NULL) {
                g_warning ("Unable to remove bluetooth technology");
                return;
        }

        g_signal_handler_disconnect (priv->bluetooth, priv->bluetooth_id);

        g_object_unref (priv->bluetooth);
        priv->bluetooth = NULL;

        gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_bluetooth")), FALSE);
        gtk_widget_set_sensitive (WID (priv->builder, "box_bluetooth"), FALSE);
        gtk_widget_set_sensitive (WID (priv->builder, "switch_tether_bt"), FALSE);

        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_bluetooth")),
                                                      "bluetooth-active-symbolic",
                                                      GTK_ICON_SIZE_BUTTON);

}

/* Bluetooth section ends*/

/* Cellular section starts*/
static void
cellular_property_changed (Technology *cellular,
                           const gchar *property,
                           GVariant *value,
                           CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;
        gboolean powered;

        if (!g_strcmp0 (property, "Powered")) {
                powered  = g_variant_get_boolean (g_variant_get_variant (value));

                priv->cellular_powered = powered;

                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_cellular")), powered);
        }
}

static void
cellular_set_powered (GObject      *source,
                      GAsyncResult *res,
                      gpointer      user_data)
{
        CcNetworkPanel *panel = user_data;
        CcNetworkPanelPrivate *priv = panel->priv;
        gboolean powered;
        GError *error = NULL;
        gint err_code;

        if (!priv->cellular)
                return;

        if (!technology_call_set_property_finish (priv->cellular, res, &error)) {
                err_code = error->code;

                /* Reset the switch if error is not AlreadyEnabled/AlreadyDisabled */
                if (err_code != 36) {
                        powered = gtk_switch_get_active (GTK_SWITCH (WID (priv->builder, "switch_cellular")));
                        gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_cellular")), !powered);
                        g_warning ("Could not set cellular Powered property: %s", error->message);
                }

                g_error_free (error);
                return;
        }
}

static void
cc_cellular_switch_toggle (GtkSwitch *sw,
                           GParamSpec *pspec,
                           CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;
        gboolean powered;
        GVariant *value;

        if (priv->cellular == NULL)
                return;

        powered = gtk_switch_get_active (sw);

        if (priv->cellular_powered == powered)
                return;

        value = g_variant_new_boolean (powered);

        technology_call_set_property (priv->cellular,
                                      "Powered",
                                      g_variant_new_variant (value),
                                      priv->cancellable,
                                      cellular_set_powered,
                                      panel);
}

static void
cc_add_technology_cellular (const gchar         *path,
                            GVariant            *properties,
                            CcNetworkPanel      *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;

        GError *error = NULL;
        gboolean powered;

        if (priv->cellular == NULL) {
                priv->cellular = technology_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                                    G_DBUS_PROXY_FLAGS_NONE,
                                                                    "net.connman",
                                                                    path,
                                                                    priv->cancellable,
                                                                    &error);

                if (error != NULL) {
                        g_warning ("Could not get proxy for cellular: %s", error->message);
                        g_error_free (error);
                        return;
                }

                gtk_widget_set_sensitive (WID (priv->builder, "box_cellular"), TRUE);

                priv->cellular_id = g_signal_connect (priv->cellular,
                                                      "property_changed",
                                                      G_CALLBACK (cellular_property_changed),
                                                      panel);

                priv->tech_update = TRUE;
        }

        if (g_variant_lookup (properties, "Powered", "b", &powered)) {
                priv->cellular_powered = powered;
                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_cellular")), powered);
        }
}

static void
cc_remove_technology_cellular (CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;

        if (priv->cellular == NULL) {
                g_warning ("Unable to remove cellular technology");
                return;
        }

        g_signal_handler_disconnect (priv->cellular, priv->cellular_id);

        g_object_unref (priv->cellular);
        priv->cellular = NULL;

        gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_cellular")), FALSE);
        gtk_widget_set_sensitive (WID (priv->builder, "box_cellular"), FALSE);
}

/* Cellular section ends*/

static void
cc_add_technology (const gchar          *path,
                   GVariant             *properties,
                   CcNetworkPanel       *panel)
{
        const gchar *type;
        gboolean ret;

        ret = g_variant_lookup (properties, "Type", "s", &type);

        if (!ret)
                return;

        if (!g_strcmp0 (type, "ethernet")) {
                cc_add_technology_ethernet (path, properties, panel);
        } else if (!g_strcmp0 (type, "wifi")) {
                cc_add_technology_wifi (path, properties, panel);
        } else if (!g_strcmp0 (type, "bluetooth")) {
                cc_add_technology_bluetooth (path, properties, panel);
        } else if (!g_strcmp0 (type, "cellular")) {
                cc_add_technology_cellular (path, properties, panel);
        } else {
                g_warning ("Unknown technology type");
                return;
        }
}

static void
cc_remove_technology (const gchar       *path,
                      CcNetworkPanel    *panel)
{
        if (!g_strcmp0 (path, "/net/connman/technology/ethernet")) {
                cc_remove_technology_ethernet (panel);
        } else if (!g_strcmp0 (path, "/net/connman/technology/wifi")) {
                cc_remove_technology_wifi (panel);
        } else if (!g_strcmp0 (path, "/net/connman/technology/bluetooth")) {
                cc_remove_technology_bluetooth (panel);
        } else if (!g_strcmp0 (path, "/net/connman/technology/cellular")) {
                cc_remove_technology_cellular (panel);
        } else {
                g_warning ("Unknown technology type");
                return;
        }
}

static void
manager_get_technologies (GObject        *source,
                          GAsyncResult     *res,
                          gpointer         user_data)
{
        CcNetworkPanel *panel = user_data;
        CcNetworkPanelPrivate *priv = panel->priv;

        GError *error;
        GVariant *result, *array_value, *tuple_value, *properties;
        GVariantIter array_iter, tuple_iter;
        gchar *path;

        error = NULL;
        if (!conn_man_manager_call_get_technologies_finish (priv->manager, &result,
                                           res, &error))
                {
                        /* TODO: display any error in a user friendly way */
                        g_warning ("Manager: Could not get technologies: %s", error->message);
                        g_error_free (error);
                        return;
                }

        /* Result is  (a(oa{sv}))*/
        g_variant_iter_init (&array_iter, result);

        while ((array_value = g_variant_iter_next_value (&array_iter)) != NULL) {
                /* tuple_iter is oa{sv} */
                g_variant_iter_init (&tuple_iter, array_value);

                /* get the object path */
                tuple_value = g_variant_iter_next_value (&tuple_iter);
                g_variant_get (tuple_value, "o", &path);

                /* get the Properties */
                properties = g_variant_iter_next_value (&tuple_iter);

                cc_add_technology (path, properties, panel);

                g_free (path);
                g_variant_unref (array_value);
                g_variant_unref (tuple_value);
                g_variant_unref (properties);
        }

        if (priv->tech_update) {
                priv->tech_update = FALSE;
                conn_man_manager_call_get_technologies (priv->manager, priv->cancellable, manager_get_technologies, panel);
        }
}

static void
manager_technology_added (ConnManManager *manager,
                          const gchar *path,
                          GVariant *properties,
                          CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;

        cc_add_technology (path, properties, panel);

        if (priv->tech_update) {
                priv->tech_update = FALSE;
                conn_man_manager_call_get_technologies(priv->manager, priv->cancellable, manager_get_technologies, panel);
        }
}

static void
manager_technology_removed (ConnManManager *manager,
                          const gchar *path,
                          CcNetworkPanel *panel)
{
        cc_remove_technology (path, panel);
}

/* Technology section ends */

static gboolean
spinner_timeout (gpointer data)
{
        const gchar *path  = data;

        GtkTreeRowReference *row;
        GtkTreeModel *model;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        guint pulse;

        row = g_hash_table_lookup (services, path);

        if (row == NULL)
                return FALSE;

        model =  gtk_tree_row_reference_get_model (row);
        tree_path = gtk_tree_row_reference_get_path (row);
        gtk_tree_model_get_iter (model, &iter, tree_path);

        gtk_tree_model_get (model, &iter, COLUMN_PULSE, &pulse, -1);

        if (pulse == G_MAXUINT)
                pulse = 0;
        else
                pulse++;

        gtk_list_store_set (GTK_LIST_STORE (model),
                            &iter,
                            COLUMN_PULSE, pulse,
                            -1);

        gtk_tree_path_free (tree_path);
        return TRUE;
}
/* Service section ends */

static void
service_property_changed (Service *service,
                          const gchar *property,
                          GVariant *value,
                          CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv;
        GtkListStore *liststore_services = NULL;

        GtkTreeRowReference *row;
        GtkTreePath *tree_path;
        GtkTreeIter iter;

        const gchar *path;
        gchar *type = NULL;
        gchar strength = 0;
        const gchar *state;
        gchar *str;

        gboolean favorite, autoconnect;
        GVariant *ethernet, *ipv4, *ipv6, *nameservers, *proxy, *domains;
        gint id;

        NetConnectionEditor *editor;
        gboolean details = FALSE;
        gboolean update_proxy = FALSE;
        gboolean update_ipv4 = FALSE;
        gboolean update_ipv6 = FALSE;
        gboolean update_domains = FALSE;
        gboolean update_nameservers = FALSE;
        gboolean ret;

        path = g_dbus_proxy_get_object_path ((GDBusProxy *) service);

        priv = (CcNetworkPanelPrivate *)panel->priv;

        liststore_services = GTK_LIST_STORE (WID (priv->builder, "liststore_services"));
        if (liststore_services == NULL)
                return;

        row = g_hash_table_lookup (services, path);

        if (row == NULL)
                return;

        tree_path = gtk_tree_row_reference_get_path (row);

        ret = gtk_tree_model_get_iter ((GtkTreeModel *) liststore_services, &iter, tree_path);

        if (!ret) {
                g_printerr ("no liststore found");
                return;
        }

        if (!g_strcmp0 (property, "Strength")) {
                strength = (gchar ) g_variant_get_byte (g_variant_get_variant (value));

                gtk_tree_model_get (GTK_TREE_MODEL (liststore_services), &iter, COLUMN_TYPE, &type, -1);

                gtk_list_store_set (liststore_services,
                                    &iter,
                                    COLUMN_STRENGTH, cc_service_strength_to_string (type, strength),
                                    -1);

                gtk_list_store_set (liststore_services,
                                    &iter,
                                    COLUMN_STRENGTH_ICON, cc_service_type_to_icon (type, strength),
                                    -1);
                details = TRUE;
        } else if (!g_strcmp0 (property, "State")) {
                state = g_variant_get_string (g_variant_get_variant (value), NULL);

                gtk_tree_model_get (GTK_TREE_MODEL (liststore_services), &iter, COLUMN_STATE, &str, -1);
                g_free (str);

                gtk_list_store_set (liststore_services,
                                    &iter,
                                    COLUMN_STATE, g_strdup (state),
                                    -1);

                gtk_list_store_set (liststore_services,
                                    &iter,
                                    COLUMN_ICON, cc_service_state_to_icon (state),
                                    -1);

                gtk_tree_model_get (GTK_TREE_MODEL (liststore_services), &iter, COLUMN_PULSE_ID, &id, -1);
                if ((g_strcmp0 (state, "association") == 0) || (g_strcmp0 (state, "configuration") == 0)) {
                        if (id == 0) {
                                network_set_status (panel, STATUS_CONNECTING);

                                id = g_timeout_add_full (G_PRIORITY_DEFAULT,
                                                         80,
                                                         spinner_timeout,
                                                         g_strdup (path),
                                                         g_free);

                                gtk_list_store_set (liststore_services,
                                                    &iter,
                                                    COLUMN_PULSE_ID, id,
                                                    COLUMN_PULSE, 0,
                                                    -1);
                        }
                } else {
                        if (id != 0) {
                                g_source_remove (id);
                                gtk_list_store_set (liststore_services,
                                                    &iter,
                                                    COLUMN_PULSE_ID, 0,
                                                    COLUMN_PULSE, 0,
                                                    -1);
                        }

                        network_set_status (panel, priv->global_state);
                }
        } else if (!g_strcmp0 (property, "Favorite")) {
                favorite = g_variant_get_boolean (g_variant_get_variant (value));

                gtk_list_store_set (liststore_services,
                                    &iter,
                                    COLUMN_FAVORITE, favorite,
                                    -1);
                details = TRUE;
        } else if (!g_strcmp0 (property, "AutoConnect")) {
                autoconnect = g_variant_get_boolean (g_variant_get_variant (value));

                gtk_list_store_set (liststore_services,
                                    &iter,
                                    COLUMN_AUTOCONNECT, autoconnect,
                                    -1);
                details = TRUE;
        } else if (!g_strcmp0 (property, "Ethernet")) {
                ethernet = g_variant_get_variant (value);

                gtk_list_store_set (liststore_services,
                                    &iter,
                                    COLUMN_ETHERNET, ethernet,
                                    -1);
                details = TRUE;
        } else if (!g_strcmp0 (property, "IPv4")) {
                ipv4 = g_variant_get_variant (value);

                gtk_list_store_set (liststore_services,
                                    &iter,
                                    COLUMN_IPV4, ipv4,
                                    -1);
                details = TRUE;
                update_ipv4 = TRUE;
        } else if (!g_strcmp0 (property, "IPv6")) {
                ipv6 = g_variant_get_variant (value);

                gtk_list_store_set (liststore_services,
                                    &iter,
                                    COLUMN_IPV6, ipv6,
                                    -1);

                update_ipv6 = TRUE;
        } else if (!g_strcmp0 (property, "Nameservers")) {
                nameservers = g_variant_get_variant (value);

                gtk_list_store_set (liststore_services,
                                    &iter,
                                    COLUMN_NAMESERVERS, nameservers,
                                    -1);
                details = TRUE;
                update_nameservers = TRUE;
        } else if (!g_strcmp0 (property, "Domains")) {
                domains = g_variant_get_variant (value);

                gtk_list_store_set (liststore_services,
                                    &iter,
                                    COLUMN_DOMAINS, domains,
                                    -1);
                update_domains = TRUE;
        } else if (!g_strcmp0 (property, "Proxy")) {
                proxy = g_variant_get_variant (value);

                gtk_list_store_set (liststore_services,
                                    &iter,
                                    COLUMN_PROXY, proxy,
                                    -1);
                update_proxy = TRUE;
        } else {
                g_warning ("Unknown property:%s", property);
                return;
        }

        gtk_tree_model_get (GTK_TREE_MODEL (liststore_services), &iter, COLUMN_EDITOR, &editor, -1);
        if (!editor)
                return;

        if (details)
                editor_update_details (editor);
        if (update_proxy)
                editor_update_proxy (editor);
        if (update_ipv4)
                editor_update_ipv4 (editor);
        if (update_ipv6)
                editor_update_ipv6 (editor);
        if (update_domains)
                editor_update_domains (editor);
        if (update_nameservers)
                editor_update_nameservers (editor);
}

static void
cc_add_service (const gchar         *path,
                GVariant            *properties,
                CcNetworkPanel      *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;
        GError *error = NULL;

        GVariant *value = NULL;
        GtkListStore *liststore_services;
        GtkTreeIter iter;
        GtkTreePath *tree_path;
        GtkTreeRowReference *row;

        Service *service;
        const gchar *name;
        const gchar *state;
        const gchar **security = NULL;
        const gchar *type;
        gint prop_id;
        gint id;

        gchar strength = 0;
        gboolean favorite = FALSE;
        gboolean autoconnect = FALSE;
        GVariant *ethernet, *ipv4, *ipv6, *nameservers, *proxy, *domains;

        /* if found in hash, just update the properties */

        liststore_services = GTK_LIST_STORE (WID (priv->builder, "liststore_services"));

        gtk_list_store_append (liststore_services, &iter);

        value = g_variant_lookup_value (properties, "Name", G_VARIANT_TYPE_STRING);
        if (value == NULL)
                name = g_strdup ("Connect to a Hidden Network");
        else
                name = g_variant_get_string (value, NULL);

        value = g_variant_lookup_value (properties, "State", G_VARIANT_TYPE_STRING);
        state = g_variant_get_string (value, NULL);

        value = g_variant_lookup_value (properties, "Type", G_VARIANT_TYPE_STRING);
        type = g_variant_get_string (value, NULL);

        g_variant_lookup (properties, "Favorite", "b", &favorite);
        g_variant_lookup (properties, "AutoConnect", "b", &autoconnect);

        ethernet = g_variant_lookup_value (properties, "Ethernet", G_VARIANT_TYPE_DICTIONARY);
        ipv4 = g_variant_lookup_value (properties, "IPv4", G_VARIANT_TYPE_DICTIONARY);
        ipv6 = g_variant_lookup_value (properties, "IPv6", G_VARIANT_TYPE_DICTIONARY);
        nameservers = g_variant_lookup_value (properties, "Nameservers", G_VARIANT_TYPE_STRING_ARRAY);
        proxy = g_variant_lookup_value (properties, "Proxy", G_VARIANT_TYPE_DICTIONARY);
        domains = g_variant_lookup_value (properties, "Domains", G_VARIANT_TYPE_STRING_ARRAY);

        if (!g_strcmp0 (type, "wifi")) {
                value = g_variant_lookup_value (properties, "Security", G_VARIANT_TYPE_STRING_ARRAY);
                security = g_variant_get_strv (value, NULL);
        }

        if (!g_strcmp0 (type, "wifi") || !g_strcmp0 (type, "cellular")) {
                value = g_variant_lookup_value (properties, "Strength", G_VARIANT_TYPE_BYTE);
                strength = (gchar ) g_variant_get_byte (value);
        }

        service = service_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  "net.connman",
                                                  path,
                                                  priv->cancellable,
                                                  &error);

        if (error != NULL) {
                g_warning ("could not get proxy for %s: %s", name,  error->message);
                g_error_free (error);
                return;
        }


        prop_id = g_signal_connect (service,
                                    "property_changed",
                                    G_CALLBACK (service_property_changed),
                                    panel);

        gtk_list_store_set (liststore_services,
                            &iter,
                            COLUMN_ICON, cc_service_state_to_icon (state),
                            COLUMN_PULSE, 0,
                            COLUMN_PULSE_ID, 0,
                            COLUMN_NAME, g_strdup (name),
                            COLUMN_STATE, g_strdup (state),
                            COLUMN_SECURITY_ICON, cc_service_security_to_icon (security),
                            COLUMN_SECURITY, cc_service_security_to_string (security),
                            COLUMN_TYPE, g_strdup (type),
                            COLUMN_STRENGTH_ICON, cc_service_type_to_icon (type, strength),
                            COLUMN_STRENGTH, cc_service_strength_to_string (type, strength),
                            COLUMN_FAVORITE, favorite,
                            COLUMN_GDBUSPROXY, service,
                            COLUMN_PROP_ID, prop_id,
                            COLUMN_AUTOCONNECT, autoconnect,
                            COLUMN_ETHERNET, ethernet,
                            COLUMN_IPV4, ipv4,
                            COLUMN_IPV6, ipv6,
                            COLUMN_NAMESERVERS, nameservers,
                            COLUMN_DOMAINS, domains,
                            COLUMN_PROXY, proxy,
                            -1);

        tree_path = gtk_tree_model_get_path ((GtkTreeModel *) liststore_services, &iter);

        row = gtk_tree_row_reference_new ((GtkTreeModel *) liststore_services, tree_path);

        g_hash_table_insert (services,
                             g_strdup (path),
                             gtk_tree_row_reference_copy (row));

        gtk_tree_model_get (GTK_TREE_MODEL (liststore_services), &iter, COLUMN_PULSE_ID, &id, -1);

        if ((g_strcmp0 (state, "association") == 0) || (g_strcmp0 (state, "configuration") == 0)) {
                if (id == 0) {
                        network_set_status (panel, STATUS_CONNECTING);

                        id = g_timeout_add_full (G_PRIORITY_DEFAULT,
                                                 80,
                                                 spinner_timeout,
                                                 g_strdup (path),
                                                 g_free);

                        gtk_list_store_set (liststore_services,
                                            &iter,
                                            COLUMN_PULSE_ID, id,
                                            COLUMN_PULSE, 0,
                                            -1);
                }
        } else {
                if (id != 0) {
                        g_source_remove (id);
                        gtk_list_store_set (liststore_services,
                                            &iter,
                                            COLUMN_PULSE_ID, 0,
                                            COLUMN_PULSE, 0,
                                            -1);
                }

                if (!g_strcmp0 (state, "failure"))
                        network_set_status (panel, priv->global_state);
        }

        gtk_tree_path_free (tree_path);
        gtk_tree_row_reference_free (row);
}

static void
manager_services_changed (ConnManManager *manager,
                          GVariant *added,
                          const gchar *const *removed,
                          CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv = panel->priv;
        GtkListStore *liststore_services;
        gint i;

        GtkTreeRowReference *row;
        GtkTreePath *tree_path;
        GtkTreeIter iter;
        NetConnectionEditor *editor;

        GVariant *array_value, *tuple_value, *properties;
        GVariantIter array_iter, tuple_iter;
        gchar *path;

        gint *new_pos;
        gint elem_size;
        Service *service;
        gint prop_id;
        gchar *state;

        liststore_services = GTK_LIST_STORE (WID (priv->builder, "liststore_services"));

        for (i=0; removed != NULL && removed[i] != NULL; i++) {
                row = g_hash_table_lookup (services, (gconstpointer *)removed[i]);

                if (row == NULL)
                        continue;

                tree_path = gtk_tree_row_reference_get_path (row);

                gtk_tree_model_get_iter ((GtkTreeModel *) liststore_services, &iter, tree_path);

                gtk_tree_model_get (GTK_TREE_MODEL (liststore_services),
                                    &iter,
                                    COLUMN_GDBUSPROXY, &service,
                                    COLUMN_PROP_ID, &prop_id,
                                    COLUMN_STATE, &state,
                                    COLUMN_EDITOR, &editor,
                                    -1);

                g_signal_handler_disconnect (service, prop_id);

                if (editor)
                        editor_done (editor, TRUE, priv);


                if ((g_strcmp0 (state, "association") == 0) || (g_strcmp0 (state, "configuration") == 0))
                        network_set_status (panel, priv->global_state);
                g_free (state);

                gtk_list_store_remove (liststore_services, &iter);

                g_hash_table_remove (services, removed[i]);

                gtk_tree_path_free (tree_path);
        }

        g_variant_iter_init (&array_iter, added);

        elem_size = (gint)g_variant_iter_n_children (&array_iter);
        new_pos = (gint *) g_malloc (elem_size * sizeof (gint));
        i = 0;

        /* Added Services */
        while ((array_value = g_variant_iter_next_value (&array_iter)) != NULL) {
                /* tuple_iter is oa{sv} */
                g_variant_iter_init (&tuple_iter, array_value);

                /* get the object path */
                tuple_value = g_variant_iter_next_value (&tuple_iter);
                g_variant_get (tuple_value, "o", &path);

                /* Found a new item, so add it */
                if (g_hash_table_lookup (services, (gconstpointer *)path) == NULL) {
                        /* get the Properties */
                        properties = g_variant_iter_next_value (&tuple_iter);

                        cc_add_service (path, properties, panel);
                        g_variant_unref (properties);
                }

                row = g_hash_table_lookup (services, (gconstpointer *) path);
                
                if (row == NULL) {
                        g_printerr ("\nSomething bad bad happened here!");
                        return;
                }

                tree_path = gtk_tree_row_reference_get_path (row);
                gint *old_pos = gtk_tree_path_get_indices (tree_path);
                new_pos[i] = old_pos[0];
                i++;

                gtk_tree_path_free (tree_path);

                g_free (path);
                g_variant_unref (array_value);
                g_variant_unref (tuple_value);

        }

        if (new_pos) {
                gtk_list_store_reorder (liststore_services, new_pos);
                g_free (new_pos);
        }

        /* Since we dont have a scan button on our UI, send a scan command */
        panel_set_scan (panel);
}

static void
manager_get_services (GObject        *source,
                      GAsyncResult   *res,
                      gpointer       user_data)
{
        CcNetworkPanel *panel = user_data;
        CcNetworkPanelPrivate *priv = panel->priv;

        GError *error;
        GVariant *result, *array_value, *tuple_value, *properties;
        GVariantIter array_iter, tuple_iter;
        gchar *path;
        gint size;

        error = NULL;
        if (!conn_man_manager_call_get_services_finish (priv->manager, &result,
                                               res, &error))
                {
                        /* TODO: display any error in a user friendly way */
                        g_warning ("Could not get services: %s", error->message);
                        g_error_free (error);
                        return;
                }

        /* Result is  (a(oa{sv}))*/

        g_variant_iter_init (&array_iter, result);

        size = (gint)g_variant_iter_n_children (&array_iter);
        if (size == 0)
                panel_set_scan (panel);

        while ((array_value = g_variant_iter_next_value (&array_iter)) != NULL) {
                /* tuple_iter is oa{sv} */
                g_variant_iter_init (&tuple_iter, array_value);

                /* get the object path */
                tuple_value = g_variant_iter_next_value (&tuple_iter);
                g_variant_get (tuple_value, "o", &path);

                /* get the Properties */
                properties = g_variant_iter_next_value (&tuple_iter);

                cc_add_service (path, properties, panel);

                g_free (path);

                g_variant_unref (array_value);
                g_variant_unref (tuple_value);
                g_variant_unref (properties);
        }

        if (size < 2)
                panel_set_scan (panel);

        /* if (priv->serv_update) { */
        /*         priv->serv_update = FALSE; */
        /*         conn_man_manager_call_get_services (priv->manager, priv->cancellable, manager_get_services, panel); */

}

static void
service_connect_cb (GObject *source_object,
                    GAsyncResult *res,
                    gpointer user_data)
{
        GError *error = NULL;
        Service *service = user_data;

        service_call_connect_finish (service, res, &error);
        if (error != NULL) {
                g_warning ("Couldn't Connect to service: %s", error->message);
                g_error_free (error);
                return;
        }
}

static void
service_disconnect_cb (GObject *source_object,
                    GAsyncResult *res,
                    gpointer user_data)
{
        GError *error = NULL;
        Service *service = user_data;

        service_call_disconnect_finish (service, res, &error);
        if (error != NULL) {
                g_warning ("Couldn't Disconnect from service: %s", error->message);
                g_error_free (error);
                return;
        }
}

static void
activate_service_cb (PanelCellRendererText *cell,
                     const gchar *path,
                     CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv;
        GtkListStore *liststore_services;

        GtkTreePath *tree_path;
        GtkTreeIter iter;
        Service *service;
        gchar *state;

        priv = NETWORK_PANEL_PRIVATE (panel);

        liststore_services = GTK_LIST_STORE (WID (priv->builder, "liststore_services"));

        tree_path = gtk_tree_path_new_from_string (path);
 
        gtk_tree_model_get_iter ((GtkTreeModel *) liststore_services, &iter, tree_path);

        gtk_tree_model_get (GTK_TREE_MODEL (liststore_services), &iter, COLUMN_GDBUSPROXY, &service, COLUMN_STATE, &state, -1);

        gtk_tree_path_free (tree_path);

        if (!g_strcmp0 (state, "online") || !g_strcmp0 (state, "ready"))
                service_call_disconnect (service, priv->cancellable, service_disconnect_cb, service);
        else if (!g_strcmp0 (state, "idle") || !g_strcmp0 (state, "failure"))
                service_call_connect (service, NULL, service_connect_cb, service);

        if (!g_strcmp0 (state, "failure"))
                panel_set_scan (panel);
}

static void
editor_done (NetConnectionEditor *editor,
             gboolean success,
             gpointer user_data)
{
        GtkListStore *liststore_services;
        GtkTreePath *tree_path;
        GtkTreeIter iter;

        CcNetworkPanelPrivate *priv = user_data;

        if (editor->window)
                gtk_widget_destroy (editor->window);

        liststore_services = GTK_LIST_STORE (WID (priv->builder, "liststore_services"));
        tree_path = gtk_tree_row_reference_get_path (editor->service_row);

        gtk_tree_model_get_iter ((GtkTreeModel *) liststore_services, &iter, tree_path);

        gtk_list_store_set (liststore_services, &iter, COLUMN_EDITOR, NULL, -1);

        gtk_tree_path_free (tree_path);

        gtk_tree_row_reference_free (editor->service_row);

        g_object_unref (editor);
}

static void
activate_settings_cb (PanelCellRendererPixbuf *cell,
                      const gchar *path,
                      CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv;

        GtkWidget *window;
        NetConnectionEditor *editor;
        GtkTreePath *tree_path;
        GtkTreeIter iter;
        GtkTreeRowReference *row;
        GtkListStore *liststore_services;

        priv = NETWORK_PANEL_PRIVATE (panel);

        liststore_services = GTK_LIST_STORE (WID (priv->builder, "liststore_services"));

        tree_path = gtk_tree_path_new_from_string (path);
        gtk_tree_model_get_iter ((GtkTreeModel *) liststore_services, &iter, tree_path);

        gtk_tree_model_get (GTK_TREE_MODEL (liststore_services), &iter, COLUMN_EDITOR, &editor, -1);

        if (editor) {
                gtk_window_present (GTK_WINDOW (editor->window));

                gtk_tree_path_free (tree_path);
                g_object_unref (editor);
                return;
        }

        row = gtk_tree_row_reference_new (GTK_TREE_MODEL (liststore_services), tree_path);

        window = cc_shell_get_toplevel (cc_panel_get_shell (CC_PANEL (panel)));

        editor = net_connection_editor_new (GTK_WINDOW (window), row);

        gtk_list_store_set (liststore_services, &iter, COLUMN_EDITOR, editor, -1);

        gtk_tree_path_free (tree_path);

        g_signal_connect (editor, "done", G_CALLBACK (editor_done), priv);
}

/* Service section ends */

/* Tethering section */

static void
tether_apply_update (CcNetworkPanelPrivate *priv)
{
        gboolean wifi;
        GtkEntry *entry;
        guint16 len_ssid, len_pass;

        wifi = gtk_switch_get_active (GTK_SWITCH (WID (priv->builder, "switch_tether_wifi")));

        if (priv->tether_wifi_toggle) {
                entry = GTK_ENTRY (WID (priv->builder, "entry_ssid"));
                len_ssid = gtk_entry_get_text_length (entry);

                entry = GTK_ENTRY (WID (priv->builder, "entry_passphrase"));
                len_pass = gtk_entry_get_text_length (entry);

                if (wifi) {
                        if (len_ssid > 0 && len_pass >=8)
                                gtk_widget_set_sensitive (GTK_WIDGET (WID (priv->builder, "button_apply")), TRUE);
                        else
                                gtk_widget_set_sensitive (GTK_WIDGET (WID (priv->builder, "button_apply")), FALSE);
                        return;
                }
        }

        gtk_widget_set_sensitive (GTK_WIDGET (WID (priv->builder, "button_apply")), TRUE);
}

static void
cc_set_tethering_image (CcNetworkPanelPrivate *priv)
{
        gboolean wifi, bluetooth, ethernet;

        wifi = gtk_switch_get_active (GTK_SWITCH (WID (priv->builder, "switch_tether_wifi")));
        bluetooth = gtk_switch_get_active (GTK_SWITCH (WID (priv->builder, "switch_tether_bt")));
        ethernet = gtk_switch_get_active (GTK_SWITCH (WID (priv->builder, "switch_tether_ethernet")));

        if (wifi && bluetooth && ethernet)
                gtk_image_set_from_file (GTK_IMAGE (WID (priv->builder, "image_tethering")), TETHERDIR "/tethering_all.png");
        else if (wifi && bluetooth)
                gtk_image_set_from_file (GTK_IMAGE (WID (priv->builder, "image_tethering")), TETHERDIR "/tethering_bt_wifi.png");
        else if (bluetooth && ethernet)
                gtk_image_set_from_file (GTK_IMAGE (WID (priv->builder, "image_tethering")), TETHERDIR "/tethering_bt_ethernet.png");
        else if (wifi && ethernet)
                gtk_image_set_from_file (GTK_IMAGE (WID (priv->builder, "image_tethering")), TETHERDIR "/tethering_wifi_ethernet.png");
        else if (wifi)
                gtk_image_set_from_file (GTK_IMAGE (WID (priv->builder, "image_tethering")), TETHERDIR "/tethering_wifi.png");
        else if (ethernet)
                gtk_image_set_from_file (GTK_IMAGE (WID (priv->builder, "image_tethering")), TETHERDIR "/tethering_ethernet.png");
        else if (bluetooth)
                gtk_image_set_from_file (GTK_IMAGE (WID (priv->builder, "image_tethering")), TETHERDIR "/tethering_bt.png");
        else
                gtk_image_set_from_file (GTK_IMAGE (WID (priv->builder, "image_tethering")), TETHERDIR "/tethering_inactive.png");
}

static void
cc_wifi_tether_switch_toggle (GtkSwitch *sw,
                              GParamSpec *pspec,
                              CcNetworkPanelPrivate *priv)
{
        gboolean enable;

        enable = gtk_switch_get_active (sw);

        cc_set_tethering_image (priv);

        if (enable == priv->wifi_tethered)
                return;

        if (enable) {
                gtk_widget_set_sensitive (GTK_WIDGET (WID (priv->builder, "entry_ssid")), TRUE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (priv->builder, "entry_passphrase")), TRUE);
        } else {
                gtk_widget_set_sensitive (GTK_WIDGET (WID (priv->builder, "entry_ssid")), FALSE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (priv->builder, "entry_passphrase")), FALSE);
        }

        priv->tether_wifi_toggle = TRUE;
        tether_apply_update (priv);
}

static void
cc_bt_tether_switch_toggle (GtkSwitch *sw,
                            GParamSpec *pspec,
                            CcNetworkPanelPrivate *priv)
{
        gboolean enable;

        enable = gtk_switch_get_active (sw);

        cc_set_tethering_image (priv);

        if (enable == priv->bluetooth_tethered)
                return;

        priv->tether_bt_toggle = TRUE;
        tether_apply_update (priv);
}

static void
cc_ethernet_tether_switch_toggle (GtkSwitch *sw,
                                  GParamSpec *pspec,
                                  CcNetworkPanelPrivate *priv)
{
        gboolean enable;

        enable = gtk_switch_get_active (sw);

        cc_set_tethering_image (priv);

        if (enable == priv->ethernet_tethered)
                return;

        priv->tether_ethernet_toggle = TRUE;
        tether_apply_update (priv);
}

static void
cancel_tethering (CcNetworkPanelPrivate *priv)
{
        gtk_widget_hide (GTK_WIDGET (priv->tethering));
}

static void
bluetooth_set_tethered (GObject      *source,
                        GAsyncResult *res,
                        gpointer      user_data)
{
        CcNetworkPanelPrivate *priv = user_data;
        GError *error = NULL;

        if (!priv->bluetooth)
                return;

        if (!technology_call_set_property_finish (priv->bluetooth, res, &error)) {
                g_warning ("Could not set bluetooth Tethering property: %s", error->message);
                g_error_free (error);
                return;
        }
}

static void
ethernet_set_tethered (GObject      *source,
                       GAsyncResult *res,
                       gpointer      user_data)
{
        CcNetworkPanelPrivate *priv = user_data;
        GError *error = NULL;

        if (!priv->ethernet)
                return;

        if (!technology_call_set_property_finish (priv->ethernet, res, &error)) {
                g_warning ("Could not set ethernet Tethering property: %s", error->message);
                g_error_free (error);
                return;
        }
}

static void
wifi_set_tethered (GObject      *source,
                   GAsyncResult *res,
                   gpointer      user_data)
{
        CcNetworkPanelPrivate *priv = user_data;
        GError *error = NULL;

        if (!priv->wifi)
                return;

        if (!technology_call_set_property_finish (priv->wifi, res, &error)) {
                g_warning ("Could not set wifi Tethering property: %s", error->message);
                g_error_free (error);
                return;
        }
}

static void
wifi_set_passphrase (GObject      *source,
                     GAsyncResult *res,
                     gpointer      user_data)
{
        CcNetworkPanelPrivate *priv = user_data;
        GError *error = NULL;

        if (!priv->wifi)
                return;

        if (!technology_call_set_property_finish (priv->wifi, res, &error)) {
                g_warning ("Could not set wifi Tethering passphrase: %s", error->message);
                g_error_free (error);
                return;
        }

        if (priv->wifi_tethered == gtk_switch_get_active (GTK_SWITCH (WID (priv->builder, "switch_tether_wifi"))))
                return;

        technology_call_set_property (priv->wifi,
                                      "Tethering",
                                      g_variant_new_variant (g_variant_new_boolean (TRUE)),
                                      NULL,
                                      wifi_set_tethered,
                                      priv);
}

static void
wifi_set_ssid (GObject      *source,
               GAsyncResult *res,
               gpointer      user_data)
{
        CcNetworkPanelPrivate *priv = user_data;
        GError *error = NULL;
        const gchar *passphrase;

        if (!priv->wifi)
                return;

        if (!technology_call_set_property_finish (priv->wifi, res, &error)) {
                g_warning ("Could not set wifi Tethering ssid: %s", error->message);
                g_error_free (error);
                return;
        }

        passphrase = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (priv->builder, "entry_passphrase")));
        technology_call_set_property (priv->wifi,
                                      "TetheringPassphrase",
                                      g_variant_new_variant (g_variant_new_string (passphrase)),
                                      NULL,
                                      wifi_set_passphrase,
                                      priv);
}


static void
apply_tethering (CcNetworkPanelPrivate *priv)
{
        gboolean wifi, bluetooth, ethernet;
        const gchar *ssid;

        gtk_widget_hide (GTK_WIDGET (priv->tethering));

        wifi = gtk_switch_get_active (GTK_SWITCH (WID (priv->builder, "switch_tether_wifi")));
        bluetooth = gtk_switch_get_active (GTK_SWITCH (WID (priv->builder, "switch_tether_bt")));
        ethernet = gtk_switch_get_active (GTK_SWITCH (WID (priv->builder, "switch_tether_ethernet")));

        ssid = (gchar *) gtk_entry_get_text (GTK_ENTRY (WID (priv->builder, "entry_ssid")));
        if (priv->tether_bt_toggle)
                technology_call_set_property (priv->bluetooth,
                                              "Tethering",
                                              g_variant_new_variant (g_variant_new_boolean (bluetooth)),
                                              NULL,
                                              bluetooth_set_tethered,
                                              priv);
        if (priv->tether_ethernet_toggle)
                technology_call_set_property (priv->ethernet,
                                              "Tethering",
                                              g_variant_new_variant (g_variant_new_boolean (ethernet)),
                                              NULL,
                                              ethernet_set_tethered,
                                              priv);

        if (priv->tether_wifi_toggle) {
                if (wifi) {
                        technology_call_set_property (priv->wifi,
                                                      "TetheringIdentifier",
                                                      g_variant_new_variant (g_variant_new_string (ssid)),
                                                      NULL,
                                                      wifi_set_ssid,
                                                      priv);
                        return;
                } else
                        technology_call_set_property (priv->wifi,
                                                      "Tethering",
                                                      g_variant_new_variant (g_variant_new_boolean (wifi)),
                                                      NULL,
                                                      wifi_set_tethered,
                                                      priv);
        }
}

static void
entry_fields_text_changed (CcNetworkPanelPrivate *priv)
{
        tether_apply_update (priv);
}

static void
cc_setup_hotspot (GtkButton *button, gpointer user_data)
{
        CcNetworkPanel *panel = user_data;
        CcNetworkPanelPrivate *priv = panel->priv;
        GtkWindow *window;
        GtkButton *btn;

        if (!priv->tethering) {
                window = cc_shell_get_toplevel (cc_panel_get_shell (CC_PANEL (panel)));

                priv->tethering = GTK_DIALOG (WID (priv->builder, "dialog_tethering"));

                g_signal_connect (priv->tethering, "delete-event", G_CALLBACK (gtk_widget_hide_on_delete), NULL);

                gtk_window_set_transient_for (GTK_WINDOW (priv->tethering), window);

                btn = GTK_WIDGET (WID (priv->builder, "button_cancel"));
                g_signal_connect_swapped (btn, "clicked",
                                          G_CALLBACK (cancel_tethering), priv);

                btn = GTK_WIDGET (WID (priv->builder, "button_apply"));
                g_signal_connect_swapped (btn, "clicked",
                                          G_CALLBACK (apply_tethering), priv);

                g_signal_connect (GTK_SWITCH (WID (priv->builder, "switch_tether_wifi")),
                                  "notify::active",
                                  G_CALLBACK (cc_wifi_tether_switch_toggle),
                                  priv);

                g_signal_connect (GTK_SWITCH (WID (priv->builder, "switch_tether_bt")),
                                  "notify::active",
                                  G_CALLBACK (cc_bt_tether_switch_toggle),
                                  priv);

                g_signal_connect (GTK_SWITCH (WID (priv->builder, "switch_tether_ethernet")),
                                  "notify::active",
                                  G_CALLBACK (cc_ethernet_tether_switch_toggle),
                                  priv);

                g_signal_connect_swapped (WID (priv->builder, "entry_ssid"),
                                          "changed",
                                          G_CALLBACK (entry_fields_text_changed),
                                          priv);

                g_signal_connect_swapped (WID (priv->builder, "entry_passphrase"),
                                          "changed",
                                          G_CALLBACK (entry_fields_text_changed),
                                          priv);
        }

        if (priv->wifi_tethered) {
                gtk_widget_set_sensitive (GTK_WIDGET (WID (priv->builder, "button_apply")), TRUE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (priv->builder, "entry_ssid")), TRUE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (priv->builder, "entry_passphrase")), TRUE);
        } else {
                gtk_widget_set_sensitive (GTK_WIDGET (WID (priv->builder, "button_apply")), FALSE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (priv->builder, "entry_ssid")), FALSE);
                gtk_widget_set_sensitive (GTK_WIDGET (WID (priv->builder, "entry_passphrase")), FALSE);
        }

        priv->tether_wifi_toggle = FALSE;
        priv->tether_bt_toggle = FALSE;
        priv->tether_ethernet_toggle = FALSE;

        gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_tether_wifi")), priv->wifi_tethered);
        gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_tether_bt")), priv->bluetooth_tethered);
        gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_tether_ethernet")), priv->ethernet_tethered);

        cc_set_tethering_image (priv);

        gtk_window_present (GTK_WINDOW (priv->tethering));
}

/* Tethering section ends */

static void
on_manager_property_changed (ConnManManager *manager,
                             const gchar *property,
                             GVariant *value,
                             CcNetworkPanel *panel)
{
        GVariant *var;
        gboolean offlinemode;
        const gchar *state;

        CcNetworkPanelPrivate *priv;

        priv = NETWORK_PANEL_PRIVATE (panel);
        var = g_variant_get_variant (value);

        if (!g_strcmp0 (property, "OfflineMode")) {
                offlinemode = g_variant_get_boolean (var);
                priv->offlinemode = offlinemode;
                gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_offline")), offlinemode);
        }

        if (!g_strcmp0 (property, "State")) {
                state = g_variant_get_string (var, NULL);
                priv->global_state = status_to_int (state);
                network_set_status (panel, priv->global_state);
        }
}

static void
manager_get_properties (GObject      *source,
                        GAsyncResult *res,
                        gpointer      user_data)
{
        CcNetworkPanel *panel = user_data;
        CcNetworkPanelPrivate *priv = panel->priv;

        GError *error;
        GVariant *result = NULL;
        GVariant *value = NULL;
        gboolean offlinemode;
        const gchar *state;

        error = NULL;
        if (!conn_man_manager_call_get_properties_finish (priv->manager, &result,
                                                 res, &error))
                {
                        /* TODO: display any error in a user friendly way */
                        g_warning ("Could not get manager properties: %s", error->message);
                        g_error_free (error);
                        return;
                }

        value = g_variant_lookup_value (result, "OfflineMode", G_VARIANT_TYPE_BOOLEAN);
        offlinemode = g_variant_get_boolean (value);

        priv->offlinemode = offlinemode;

        gtk_switch_set_active (GTK_SWITCH (WID (priv->builder, "switch_offline")), offlinemode);

        value = g_variant_lookup_value (result, "State", G_VARIANT_TYPE_STRING);
        state = g_variant_get_string (value, NULL);

        priv->global_state = status_to_int (state);
        network_set_status (panel, priv->global_state);
}

static void
manager_created_cb (GObject *source_object,
                    GAsyncResult *res,
                    gpointer user_data)
{
        CcNetworkPanel *panel = user_data;
        CcNetworkPanelPrivate *priv = NETWORK_PANEL_PRIVATE (panel);

        GError *error = NULL;

        g_clear_object (&priv->manager);

        priv->manager = conn_man_manager_proxy_new_for_bus_finish (res, &error);
        if (error != NULL) {
                g_warning ("Couldn't contact connmand service: %s", error->message);
                g_error_free (error);
                return;
        }

        priv->tech_update = FALSE;
        priv->serv_update = FALSE;

        priv->mgr_prop_id = g_signal_connect (priv->manager, "property_changed",
                          G_CALLBACK (on_manager_property_changed), panel);

        conn_man_manager_call_get_properties (priv->manager, priv->cancellable, manager_get_properties, panel);

        priv->tech_added_id = g_signal_connect (priv->manager, "technology_added",
                                                G_CALLBACK (manager_technology_added), panel);

        priv->tech_removed_id = g_signal_connect (priv->manager, "technology_removed",
                                                  G_CALLBACK (manager_technology_removed), panel);

        conn_man_manager_call_get_technologies (priv->manager, priv->cancellable, manager_get_technologies, panel);

        priv->serv_id = g_signal_connect (priv->manager, "services_changed",
                                          G_CALLBACK (manager_services_changed), panel);

        conn_man_manager_call_get_services (priv->manager, priv->cancellable, manager_get_services, panel);
}

static void
connman_appeared_cb (GDBusConnection *connection,
                     const gchar *name,
                     const gchar *name_owner,
                     gpointer user_data)
{
        GtkWidget *widget;
        CcNetworkPanel *panel = user_data;
        CcNetworkPanelPrivate *priv;

        if (strcmp (name, "net.connman"))
                return;

        priv = NETWORK_PANEL_PRIVATE (panel);

        widget = GTK_WIDGET (WID (priv->builder, "vbox1"));
        gtk_widget_set_sensitive (widget, TRUE);

        conn_man_manager_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                   G_DBUS_PROXY_FLAGS_NONE,
                                   "net.connman",
                                   "/",
                                   priv->cancellable,
                                   manager_created_cb,
                                   panel);
}

static void
connman_disappeared_cb (GDBusConnection *connection,
                        const gchar *name,
                        gpointer user_data)
{
        GtkWidget *widget;
        CcNetworkPanel *panel = user_data;
        CcNetworkPanelPrivate *priv;

        if (strcmp (name, "net.connman"))
                return;

        priv = NETWORK_PANEL_PRIVATE (panel);

        network_set_status (panel, STATUS_UNAVAILABLE);

        widget = GTK_WIDGET (WID (priv->builder, "vbox1"));
        gtk_widget_set_sensitive (widget, FALSE);

        if (priv->manager) {
                if (priv->mgr_prop_id)
                        g_signal_handler_disconnect (priv->manager, priv->mgr_prop_id);
                if (priv->tech_added_id)
                        g_signal_handler_disconnect (priv->manager, priv->tech_added_id);
                if (priv->tech_removed_id)
                        g_signal_handler_disconnect (priv->manager, priv->tech_removed_id);
                if (priv->serv_id)
                        g_signal_handler_disconnect (priv->manager, priv->serv_id);

                g_object_unref (priv->manager);
                priv->manager = NULL;
        }
}

static void
set_service_name (GtkTreeViewColumn *col,
                  GtkCellRenderer   *renderer,
                  GtkTreeModel      *model,
                  GtkTreeIter       *iter,
                  gpointer           user_data)
{
        gboolean fav;
        const gchar *name, *state;
        gchar *uniname;

        gtk_tree_model_get (model, iter, COLUMN_FAVORITE, &fav, -1);
        gtk_tree_model_get (model, iter, COLUMN_NAME, &name, -1);
        gtk_tree_model_get (model, iter, COLUMN_STATE, &state, -1);

        if ((g_strcmp0 (state, "ready") == 0) || (g_strcmp0 (state, "online") == 0))
                uniname =  g_strdup_printf ("%s \u2713", name);
        else
                uniname = g_strdup (name);

        if (fav) {
                g_object_set (renderer,
                      "weight", PANGO_WEIGHT_BOLD,
                      "weight-set", TRUE,
                      "text", uniname,
                      NULL);
        } else {
                g_object_set (renderer,
                      "weight", PANGO_WEIGHT_ULTRALIGHT,
                      "weight-set", TRUE,
                      "text", uniname,
                      NULL);
        }

        g_free (uniname);
}

static void
cc_setup_service_columns (CcNetworkPanel *panel)
{
        CcNetworkPanelPrivate *priv;

        GtkCellRenderer *renderer1;
        GtkCellRenderer *renderer2;
        GtkCellRenderer *renderer3;
        GtkCellRenderer *renderer4;
        GtkCellRenderer *renderer5;
        GtkTreeViewColumn *column;
        GtkCellArea *area;

        priv =  NETWORK_PANEL_PRIVATE (panel);

        column = GTK_TREE_VIEW_COLUMN (WID (priv->builder, "treeview_list_column"));
        area = gtk_cell_layout_get_area (GTK_CELL_LAYOUT (column));


        /* Column1 : Spinner (Online/Ready) */
        renderer1 = gtk_cell_renderer_spinner_new ();

        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (column), renderer1, FALSE);
        gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (column), renderer1, "active", COLUMN_ICON, "pulse", COLUMN_PULSE, NULL);

        gtk_cell_area_cell_set (area, renderer1, "align", TRUE, NULL);

        /* Column2 : Type(with strength) */
        renderer2 = gtk_cell_renderer_pixbuf_new ();

        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (column), renderer2, FALSE);
        gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (column), renderer2, "icon-name", COLUMN_STRENGTH_ICON, NULL);
        g_object_set (renderer2,
                      "follow-state", TRUE,
                      "xpad", 1,
                      "ypad", 6,
                      NULL);

        gtk_cell_area_cell_set (area, renderer2, "align", TRUE, NULL);

        /* Column3 : The Name */
        renderer3 = panel_cell_renderer_text_new ();
        g_object_set (renderer3,
                      "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
                      "ellipsize", PANGO_ELLIPSIZE_END,
                      NULL);
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (column), renderer3, TRUE);

        gtk_tree_view_column_set_cell_data_func (column, renderer3, set_service_name, NULL, NULL);

        gtk_cell_area_cell_set (area, renderer3,
                                "align", TRUE,
                                "expand", TRUE,
                                NULL);

        g_signal_connect (renderer3, "activate",
                          G_CALLBACK (activate_service_cb), panel);

        /* Column4 : Security */
        renderer4 = gtk_cell_renderer_pixbuf_new ();

        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (column), renderer4, FALSE);
        gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (column), renderer4, "icon-name", COLUMN_SECURITY_ICON, NULL);
        g_object_set (renderer4,
                      "follow-state", TRUE,
                      "xpad", 6,
                      "ypad", 6,
                      NULL);

        gtk_cell_area_cell_set (area, renderer4, "align", TRUE, NULL);

        /* Column5 : Security */
        renderer5 = panel_cell_renderer_pixbuf_new ();
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (column), renderer5, FALSE);
        g_object_set (renderer5,
                      "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
                      "follow-state", TRUE,
                      "visible", TRUE,
                      "icon-name", "connman_settings",
                      "xpad", 10,
                      "ypad", 6,
                      NULL);

        gtk_cell_area_cell_set (area, renderer5, "align", TRUE, NULL);

        g_signal_connect (renderer5, "activate",
                          G_CALLBACK (activate_settings_cb), panel);

        gtk_cell_area_add_focus_sibling (area, renderer3, renderer1);
        gtk_cell_area_add_focus_sibling (area, renderer3, renderer2);
        gtk_cell_area_add_focus_sibling (area, renderer3, renderer4);
}

static void
cc_network_panel_init (CcNetworkPanel *panel)
{
        GError *error = NULL;
        GtkWidget *widget;
        CcNetworkPanelPrivate *priv;

        priv = panel->priv = NETWORK_PANEL_PRIVATE (panel);
        g_resources_register (cc_network_get_resource ());

        panel->priv->builder = gtk_builder_new ();
        gtk_builder_add_from_resource (priv->builder,
                                       "/org/gnome/control-center/network/network.ui",
                                       &error);
        if (error != NULL) {
                g_warning ("Could not load interface file: %s", error->message);
                g_error_free (error);
                return;
        }

        priv->cancellable = g_cancellable_new ();

        widget = GTK_WIDGET (WID (priv->builder, "vbox1"));
        gtk_widget_reparent (widget, (GtkWidget *) panel);

        gtk_label_set_text (GTK_LABEL (WID (priv->builder, "label_offline")), _("In-flight Mode"));

        gtk_label_set_text (GTK_LABEL (WID (priv->builder, "label_ethernet")), _("Ethernet"));
        gtk_label_set_text (GTK_LABEL (WID (priv->builder, "label_wifi")), _("Wireless"));
        gtk_label_set_text (GTK_LABEL (WID (priv->builder, "label_bluetooth")), _("Bluetooth"));
        gtk_label_set_text (GTK_LABEL (WID (priv->builder, "label_cellular")), _("Cellular"));

        gtk_button_set_label (GTK_BUTTON (WID (priv->builder, "button_vpn")), _("Add a VPN"));
        gtk_button_set_label (GTK_BUTTON (WID (priv->builder, "button_hotspot")), _("Create Hotspot"));

        gtk_image_set_from_icon_name (GTK_IMAGE (WID (priv->builder, "image_hotspot")),
                                      "connman_hotspot", GTK_ICON_SIZE_BUTTON);

        g_signal_connect (GTK_SWITCH (WID (priv->builder, "switch_offline")),
                          "notify::active",
                          G_CALLBACK (offline_switch_toggle),
                          panel);

        g_signal_connect (GTK_SWITCH (WID (priv->builder, "switch_ethernet")),
                          "notify::active",
                          G_CALLBACK (cc_ethernet_switch_toggle),
                          panel);

        g_signal_connect (GTK_SWITCH (WID (priv->builder, "switch_wifi")),
                          "notify::active",
                          G_CALLBACK (cc_wifi_switch_toggle),
                          panel);

        g_signal_connect (GTK_SWITCH (WID (priv->builder, "switch_bluetooth")),
                          "notify::active",
                          G_CALLBACK (cc_bluetooth_switch_toggle),
                          panel);

        g_signal_connect (GTK_SWITCH (WID (priv->builder, "switch_cellular")),
                          "notify::active",
                          G_CALLBACK (cc_cellular_switch_toggle),
                          panel);

        g_signal_connect (GTK_BUTTON (WID (priv->builder, "button_hotspot")),
                          "clicked",
                          G_CALLBACK (cc_setup_hotspot),
                          panel);

        services = g_hash_table_new_full (g_str_hash,
                                                g_str_equal,
                                                (GDestroyNotify) g_free,
                                                (GDestroyNotify) gtk_tree_row_reference_free
                                                );

        cc_setup_service_columns (panel);

        priv->watch_id = g_bus_watch_name (G_BUS_TYPE_SYSTEM,
                                           "net.connman",
                                           G_BUS_NAME_WATCHER_FLAGS_NONE,
                                           connman_appeared_cb,
                                           connman_disappeared_cb,
                                           panel,
                                           NULL);
}
