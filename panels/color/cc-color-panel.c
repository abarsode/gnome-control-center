/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2010 Red Hat, Inc
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
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
#include <colord.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include "cc-color-panel.h"

#define WID(b, w) (GtkWidget *) gtk_builder_get_object (b, w)

G_DEFINE_DYNAMIC_TYPE (CcColorPanel, cc_color_panel, CC_TYPE_PANEL)

#define COLOR_PANEL_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CC_TYPE_COLOR_PANEL, CcColorPanelPrivate))

struct _CcColorPanelPrivate
{
  CdClient      *client;
  CdDevice      *current_device;
  CdSensor      *sensor;
  GCancellable  *cancellable;
  GDBusProxy    *proxy;
  GSettings     *settings;
  GtkBuilder    *builder;
  GtkTreeStore  *list_store_devices;
  GtkWidget     *main_window;
};

enum {
  GCM_PREFS_COLUMN_DEVICE_ID,
  GCM_PREFS_COLUMN_SORT,
  GCM_PREFS_COLUMN_ICON,
  GCM_PREFS_COLUMN_TITLE,
  GCM_PREFS_COLUMN_DEVICE,
  GCM_PREFS_COLUMN_PROFILE,
  GCM_PREFS_COLUMN_STATUS,
  GCM_PREFS_COLUMN_STATUS_IMAGE,
  GCM_PREFS_COLUMN_TOOLTIP,
  GCM_PREFS_COLUMN_RADIO_ACTIVE,
  GCM_PREFS_COLUMN_RADIO_VISIBLE,
  GCM_PREFS_COLUMN_NUM_COLUMNS
};

enum {
  GCM_PREFS_COMBO_COLUMN_TEXT,
  GCM_PREFS_COMBO_COLUMN_PROFILE,
  GCM_PREFS_COMBO_COLUMN_TYPE,
  GCM_PREFS_COMBO_COLUMN_NUM_COLUMNS
};

typedef enum {
  GCM_PREFS_ENTRY_TYPE_PROFILE,
  GCM_PREFS_ENTRY_TYPE_IMPORT
} GcmPrefsEntryType;

#define GCM_SETTINGS_SCHEMA                             "org.gnome.settings-daemon.plugins.color"
#define GCM_SETTINGS_RECALIBRATE_PRINTER_THRESHOLD      "recalibrate-printer-threshold"
#define GCM_SETTINGS_RECALIBRATE_DISPLAY_THRESHOLD      "recalibrate-display-threshold"

static void
gcm_prefs_combobox_add_profile (GtkWidget *widget,
                                CdProfile *profile,
                                GcmPrefsEntryType entry_type,
                                GtkTreeIter *iter)
{
  GtkTreeModel *model;
  GtkTreeIter iter_tmp;
  const gchar *description;

  /* iter is optional */
  if (iter == NULL)
    iter = &iter_tmp;

  /* use description */
  if (entry_type == GCM_PREFS_ENTRY_TYPE_IMPORT)
    {
      /* TRANSLATORS: this is where the user can click and import a profile */
      description = _("Other profile…");
    }
  else
    {
      description = cd_profile_get_title (profile);
    }

  /* also add profile */
  model = gtk_combo_box_get_model (GTK_COMBO_BOX(widget));
  gtk_list_store_append (GTK_LIST_STORE(model), iter);
  gtk_list_store_set (GTK_LIST_STORE(model), iter,
                      GCM_PREFS_COMBO_COLUMN_TEXT, description,
                      GCM_PREFS_COMBO_COLUMN_PROFILE, profile,
                      GCM_PREFS_COMBO_COLUMN_TYPE, entry_type,
                      -1);
}

static void
gcm_prefs_default_cb (GtkWidget *widget, CcColorPanel *prefs)
{
  CdProfile *profile;
  gboolean ret;
  GError *error = NULL;
  CcColorPanelPrivate *priv = prefs->priv;

  /* TODO: check if the profile is already systemwide */
  profile = cd_device_get_default_profile (priv->current_device);
  if (profile == NULL)
    goto out;

  /* install somewhere out of $HOME */
  ret = cd_profile_install_system_wide_sync (profile,
                                             priv->cancellable,
                                             &error);
  if (!ret)
    {
      g_warning ("failed to set profile system-wide: %s",
           error->message);
      g_error_free (error);
      goto out;
    }
out:
  if (profile != NULL)
    g_object_unref (profile);
}

static GFile *
gcm_prefs_file_chooser_get_icc_profile (CcColorPanel *prefs)
{
  GtkWindow *window;
  GtkWidget *dialog;
  GFile *file = NULL;
  GtkFileFilter *filter;
  CcColorPanelPrivate *priv = prefs->priv;

  /* create new dialog */
  window = GTK_WINDOW(gtk_builder_get_object (priv->builder,
                "dialog_assign"));
  /* TRANSLATORS: an ICC profile is a file containing colorspace data */
  dialog = gtk_file_chooser_dialog_new (_("Select ICC Profile File"), window,
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        _("Import"), GTK_RESPONSE_ACCEPT,
                                        NULL);
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dialog), g_get_home_dir ());
  gtk_file_chooser_set_create_folders (GTK_FILE_CHOOSER(dialog), FALSE);
  gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER(dialog), FALSE);

  /* setup the filter */
  filter = gtk_file_filter_new ();
  gtk_file_filter_add_mime_type (filter, "application/vnd.iccprofile");

  /* TRANSLATORS: filter name on the file->open dialog */
  gtk_file_filter_set_name (filter, _("Supported ICC profiles"));
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER(dialog), filter);

  /* setup the all files filter */
  filter = gtk_file_filter_new ();
  gtk_file_filter_add_pattern (filter, "*");
  /* TRANSLATORS: filter name on the file->open dialog */
  gtk_file_filter_set_name (filter, _("All files"));
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER(dialog), filter);

  /* did user choose file */
  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER(dialog));

  /* we're done */
  gtk_widget_destroy (dialog);

  /* or NULL for missing */
  return file;
}

static void
gcm_prefs_calibrate_cb (GtkWidget *widget, CcColorPanel *prefs)
{
  gboolean ret;
  GError *error = NULL;
  guint xid;
  GPtrArray *argv;
  CcColorPanelPrivate *priv = prefs->priv;

  /* get xid */
  xid = gdk_x11_window_get_xid (gtk_widget_get_window (GTK_WIDGET (priv->main_window)));

  /* run with modal set */
  argv = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (argv, g_build_filename (BINDIR, "gcm-calibrate", NULL));
  g_ptr_array_add (argv, g_strdup ("--device"));
  g_ptr_array_add (argv, g_strdup (cd_device_get_id (priv->current_device)));
  g_ptr_array_add (argv, g_strdup ("--parent-window"));
  g_ptr_array_add (argv, g_strdup_printf ("%i", xid));
  ret = g_spawn_async (NULL, (gchar**) argv->pdata, NULL, 0,
                       NULL, NULL, NULL, &error);
  if (!ret)
    {
      g_warning ("failed to run calibrate: %s", error->message);
      g_error_free (error);
    }
  g_ptr_array_unref (argv);
}

static void
gcm_prefs_device_add_cb (GtkWidget *widget, CcColorPanel *prefs)
{
  CcColorPanelPrivate *priv = prefs->priv;

  /* show ui */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "dialog_virtual"));
  gtk_widget_show (widget);
  gtk_window_set_transient_for (GTK_WINDOW (widget),
                                GTK_WINDOW (priv->main_window));

  /* clear entries */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "combobox_virtual_type"));
  gtk_combo_box_set_active (GTK_COMBO_BOX(widget), 0);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "entry_virtual_model"));
  gtk_entry_set_text (GTK_ENTRY (widget), "");
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "entry_virtual_manufacturer"));
  gtk_entry_set_text (GTK_ENTRY (widget), "");
}

static gboolean
gcm_prefs_is_profile_suitable_for_device (CdProfile *profile,
            CdDevice *device)
{
  CdProfileKind profile_kind_tmp;
  CdProfileKind profile_kind;
  CdColorspace profile_colorspace;
  CdColorspace device_colorspace = 0;
  gboolean ret = FALSE;
  CdDeviceKind device_kind;

  /* not the right colorspace */
  device_colorspace = cd_device_get_colorspace (device);
  profile_colorspace = cd_profile_get_colorspace (profile);
  if (device_colorspace != profile_colorspace)
    goto out;

  /* not the correct kind */
  device_kind = cd_device_get_kind (device);
  profile_kind_tmp = cd_profile_get_kind (profile);
  profile_kind = cd_device_kind_to_profile_kind (device_kind);
  if (profile_kind_tmp != profile_kind)
    goto out;

  /* success */
  ret = TRUE;
out:
  return ret;
}

static gint
gcm_prefs_combo_sort_func_cb (GtkTreeModel *model,
                              GtkTreeIter *a,
                              GtkTreeIter *b,
                              gpointer user_data)
{
  gint type_a, type_b;
  gchar *text_a;
  gchar *text_b;
  gint retval;

  /* get data from model */
  gtk_tree_model_get (model, a,
                      GCM_PREFS_COMBO_COLUMN_TYPE, &type_a,
                      GCM_PREFS_COMBO_COLUMN_TEXT, &text_a,
                      -1);
  gtk_tree_model_get (model, b,
                      GCM_PREFS_COMBO_COLUMN_TYPE, &type_b,
                      GCM_PREFS_COMBO_COLUMN_TEXT, &text_b,
                      -1);

  /* prefer normal type profiles over the 'Other Profile...' entry */
  if (type_a < type_b)
    retval = -1;
  else if (type_a > type_b)
    retval = 1;
  else
    retval = g_strcmp0 (text_a, text_b);

  g_free (text_a);
  g_free (text_b);
  return retval;
}

static void
gcm_prefs_add_profiles_suitable_for_devices (CcColorPanel *prefs,
                                             GtkWidget *widget,
                                             CdProfile *profile)
{
  CdProfile *profile_tmp;
  gboolean ret;
  GError *error = NULL;
  GPtrArray *profile_array = NULL;
  GtkTreeIter iter;
  GtkTreeModel *model;
  guint i;
  CcColorPanelPrivate *priv = prefs->priv;

  /* clear existing entries */
  model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
  gtk_list_store_clear (GTK_LIST_STORE (model));
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (model),
                                   GCM_PREFS_COMBO_COLUMN_TEXT,
                                   gcm_prefs_combo_sort_func_cb,
                                   model, NULL);

  /* get profiles */
  profile_array = cd_client_get_profiles_sync (priv->client,
                                               priv->cancellable,
                                               &error);
  if (profile_array == NULL)
    {
      g_warning ("failed to get profiles: %s",
           error->message);
      g_error_free (error);
      goto out;
    }

  /* add profiles of the right kind */
  for (i = 0; i < profile_array->len; i++)
    {
      profile_tmp = g_ptr_array_index (profile_array, i);

      /* don't add the current profile */
      if (profile != NULL && cd_profile_equal (profile, profile_tmp))
        continue;

      /* only add correct types */
      ret = gcm_prefs_is_profile_suitable_for_device (profile_tmp,
                                                      priv->current_device);
      if (!ret)
        continue;

      /* add */
      gcm_prefs_combobox_add_profile (widget,
                                      profile_tmp,
                                      GCM_PREFS_ENTRY_TYPE_PROFILE,
                                      &iter);
    }

  /* add a import entry */
  gcm_prefs_combobox_add_profile (widget, NULL, GCM_PREFS_ENTRY_TYPE_IMPORT, NULL);
  gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);
out:
  if (profile_array != NULL)
    g_ptr_array_unref (profile_array);
}

static void
gcm_prefs_profile_add_cb (GtkWidget *widget, CcColorPanel *prefs)
{
  CdProfile *profile = NULL;
  CcColorPanelPrivate *priv = prefs->priv;

  /* add profiles of the right kind */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "combobox_profile"));
  profile = cd_device_get_default_profile (priv->current_device);
  gcm_prefs_add_profiles_suitable_for_devices (prefs, widget, profile);

  /* show the dialog */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "dialog_assign"));
  gtk_widget_show (widget);
  gtk_window_set_transient_for (GTK_WINDOW (widget), GTK_WINDOW (priv->main_window));
  if (profile != NULL)
    g_object_unref (profile);
}

static void
gcm_prefs_profile_remove_cb (GtkWidget *widget, CcColorPanel *prefs)
{
  GtkTreeIter iter;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  gboolean ret = FALSE;
  CdProfile *profile = NULL;
  GError *error = NULL;
  CcColorPanelPrivate *priv = prefs->priv;

  /* get the selected row */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "treeview_devices"));
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    g_assert_not_reached ();

  /* if the profile is default, then we'll have to make the first profile default */
  gtk_tree_model_get (model, &iter,
                      GCM_PREFS_COLUMN_PROFILE, &profile,
                      -1);

  /* just remove it, the list store will get ::changed */
  ret = cd_device_remove_profile_sync (priv->current_device,
                                       profile,
                                       priv->cancellable,
                                       &error);
  if (!ret)
    {
      g_warning ("failed to remove profile: %s", error->message);
      g_error_free (error);
      goto out;
    }
out:
  if (profile != NULL)
    g_object_unref (profile);
  return;
}

static void
gcm_prefs_profile_make_default_internal (CcColorPanel *prefs,
                                         GtkTreeModel *model,
                                         GtkTreeIter *iter_selected)
{
  CdProfile *profile;
  GError *error = NULL;
  gboolean ret = FALSE;
  CcColorPanelPrivate *priv = prefs->priv;

  /* get currentlt selected item */
  gtk_tree_model_get (model, iter_selected,
                      GCM_PREFS_COLUMN_PROFILE, &profile,
                      -1);
  if (profile == NULL)
    goto out;

  /* just set it default */
  ret = cd_device_make_profile_default_sync (priv->current_device,
                                             profile,
                                             priv->cancellable,
                                             &error);
  if (!ret)
    {
      g_warning ("failed to set default profile: %s", error->message);
      g_error_free (error);
      goto out;
    }
out:
  if (profile != NULL)
    g_object_unref (profile);
}

static void
gcm_prefs_profile_view_cb (GtkWidget *widget, CcColorPanel *prefs)
{
  CdProfile *profile = NULL;
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreeSelection *selection;
  gchar *options = NULL;
  GPtrArray *argv = NULL;
  guint xid;
  gboolean ret;
  GError *error = NULL;
  CcColorPanelPrivate *priv = prefs->priv;

  /* get the selected row */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "treeview_devices"));
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    g_assert_not_reached ();

  /* get currentlt selected item */
  gtk_tree_model_get (model, &iter,
                      GCM_PREFS_COLUMN_PROFILE, &profile,
                      -1);

  /* get xid */
  xid = gdk_x11_window_get_xid (gtk_widget_get_window (GTK_WIDGET (priv->main_window)));

  /* open up gcm-viewer as a info pane */
  argv = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (argv, g_build_filename (BINDIR, "gcm-viewer", NULL));
  g_ptr_array_add (argv, g_strdup ("--profile"));
  g_ptr_array_add (argv, g_strdup (cd_profile_get_id (profile)));
  g_ptr_array_add (argv, g_strdup ("--parent-window"));
  g_ptr_array_add (argv, g_strdup_printf ("%i", xid));
  ret = g_spawn_async (NULL, (gchar**) argv->pdata, NULL, 0,
                       NULL, NULL, NULL, &error);
  if (!ret)
    {
      g_warning ("failed to run calibrate: %s", error->message);
      g_error_free (error);
    }

  if (argv != NULL)
    g_ptr_array_unref (argv);
  g_free (options);
  if (profile != NULL)
    g_object_unref (profile);
}

static void
gcm_prefs_button_assign_cancel_cb (GtkWidget *widget, CcColorPanel *prefs)
{
  CcColorPanelPrivate *priv = prefs->priv;
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "dialog_assign"));
  gtk_widget_hide (widget);
}

static void
gcm_prefs_button_assign_ok_cb (GtkWidget *widget, CcColorPanel *prefs)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  CdProfile *profile = NULL;
  gboolean ret = FALSE;
  GError *error = NULL;
  CcColorPanelPrivate *priv = prefs->priv;

  /* hide window */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "dialog_assign"));
  gtk_widget_hide (widget);

  /* get entry */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "combobox_profile"));
  ret = gtk_combo_box_get_active_iter (GTK_COMBO_BOX(widget), &iter);
  if (!ret)
    goto out;
  model = gtk_combo_box_get_model (GTK_COMBO_BOX(widget));
  gtk_tree_model_get (model, &iter,
                      GCM_PREFS_COMBO_COLUMN_PROFILE, &profile,
                      -1);

  /* just add it, the list store will get ::changed */
  ret = cd_device_add_profile_sync (priv->current_device,
                                    CD_DEVICE_RELATION_HARD,
                                    profile,
                                    priv->cancellable,
                                    &error);
  if (!ret)
    {
      g_warning ("failed to add: %s", error->message);
      g_error_free (error);
      goto out;
    }

  /* make it default */
  ret = cd_device_make_profile_default_sync (priv->current_device,
                                             profile,
                                             priv->cancellable,
                                             &error);
  if (!ret)
    {
      g_warning ("failed to set default: %s", error->message);
      g_error_free (error);
      goto out;
    }
out:
  if (profile != NULL)
    g_object_unref (profile);
}

static gboolean
gcm_prefs_profile_delete_event_cb (GtkWidget *widget,
                                   GdkEvent *event,
                                   CcColorPanel *prefs)
{
  gcm_prefs_button_assign_cancel_cb (widget, prefs);
  return TRUE;
}

static void
gcm_prefs_delete_cb (GtkWidget *widget, CcColorPanel *prefs)
{
  gboolean ret = FALSE;
  GError *error = NULL;
  CcColorPanelPrivate *priv = prefs->priv;

  /* try to delete device */
  ret = cd_client_delete_device_sync (priv->client,
                                      cd_device_get_id (priv->current_device),
                                      priv->cancellable,
                                      &error);
  if (!ret)
    {
      g_warning ("failed to delete device: %s", error->message);
      g_error_free (error);
    }
}

static void
gcm_prefs_treeview_renderer_toggled (GtkCellRendererToggle *cell,
                                     const gchar *path, CcColorPanel *prefs)
{
  gboolean ret;
  GtkTreeModel *model;
  GtkTreeIter iter;
  CcColorPanelPrivate *priv = prefs->priv;

  model = GTK_TREE_MODEL (priv->list_store_devices);
  ret = gtk_tree_model_get_iter_from_string (model, &iter, path);
  if (!ret)
    return;
  gcm_prefs_profile_make_default_internal (prefs, model, &iter);
}

static void
gcm_prefs_add_devices_columns (CcColorPanel *prefs,
             GtkTreeView *treeview)
{
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  CcColorPanelPrivate *priv = prefs->priv;

  gtk_tree_view_set_headers_visible (treeview, TRUE);

  /* --- column for device image and device title --- */
  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_expand (column, TRUE);
  /* TRANSLATORS: column for device list */
  gtk_tree_view_column_set_title (column, _("Device"));

  /* image */
  renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (renderer, "stock-size", GTK_ICON_SIZE_MENU, NULL);
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_add_attribute (column, renderer,
                                      "icon-name", GCM_PREFS_COLUMN_ICON);

  /* option */
  renderer = gtk_cell_renderer_toggle_new ();
  g_signal_connect (renderer, "toggled",
                    G_CALLBACK (gcm_prefs_treeview_renderer_toggled), prefs);
  g_object_set (renderer, "radio", TRUE, NULL);
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_add_attribute (column, renderer,
                                      "active", GCM_PREFS_COLUMN_RADIO_ACTIVE);
  gtk_tree_view_column_add_attribute (column, renderer,
                                      "visible", GCM_PREFS_COLUMN_RADIO_VISIBLE);

  /* text */
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_add_attribute (column, renderer,
                                      "markup", GCM_PREFS_COLUMN_TITLE);
  gtk_tree_view_column_set_expand (column, TRUE);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->list_store_devices),
                                        GCM_PREFS_COLUMN_SORT,
                                        GTK_SORT_DESCENDING);
  gtk_tree_view_append_column (treeview, GTK_TREE_VIEW_COLUMN(column));

  /* --- column for device status --- */
  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_expand (column, TRUE);
  /* TRANSLATORS: column for device list */
  gtk_tree_view_column_set_title (column, _("Calibration"));

  /* image */
  renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (renderer, "stock-size", GTK_ICON_SIZE_MENU, NULL);
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_add_attribute (column, renderer,
                                      "icon-name", GCM_PREFS_COLUMN_STATUS_IMAGE);

  /* text */
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_add_attribute (column, renderer,
                                      "markup", GCM_PREFS_COLUMN_STATUS);
  gtk_tree_view_column_set_expand (column, FALSE);
  gtk_tree_view_append_column (treeview, GTK_TREE_VIEW_COLUMN(column));

  /* tooltip */
  gtk_tree_view_set_tooltip_column (treeview,
                                    GCM_PREFS_COLUMN_TOOLTIP);
}

static void
gcm_prefs_set_calibrate_button_sensitivity (CcColorPanel *prefs)
{
  gboolean ret = FALSE;
  GtkWidget *widget;
  const gchar *tooltip;
  CdDeviceKind kind;
  CcColorPanelPrivate *priv = prefs->priv;

  /* TRANSLATORS: this is when the button is sensitive */
  tooltip = _("Create a color profile for the selected device");

  /* no device selected */
  if (priv->current_device == NULL)
    goto out;

  /* are we a display */
  kind = cd_device_get_kind (priv->current_device);
  if (kind == CD_DEVICE_KIND_DISPLAY)
    {

      /* find whether we have hardware installed */
      if (priv->sensor == NULL) {
        /* TRANSLATORS: this is when the button is insensitive */
        tooltip = _("The measuring instrument is not detected. Please check it is turned on and correctly connected.");
        goto out;
      }

      /* success */
      ret = TRUE;

    }
  else if (kind == CD_DEVICE_KIND_SCANNER ||
           kind == CD_DEVICE_KIND_CAMERA ||
           kind == CD_DEVICE_KIND_WEBCAM)
    {

      /* TODO: find out if we can scan using gnome-scan */
      ret = TRUE;

    }
  else if (kind == CD_DEVICE_KIND_PRINTER)
    {

    /* find whether we have hardware installed */
    if (priv->sensor == NULL)
      {
        /* TRANSLATORS: this is when the button is insensitive */
        tooltip = _("The measuring instrument is not detected. Please check it is turned on and correctly connected.");
        goto out;
      }

    /* find whether we have hardware installed */
    ret = cd_sensor_has_cap (priv->sensor, CD_SENSOR_CAP_PRINTER);
    if (!ret)
      {
        /* TRANSLATORS: this is when the button is insensitive */
        tooltip = _("The measuring instrument does not support printer profiling.");
        goto out;
      }

    /* success */
    ret = TRUE;

    }
  else
    {
      /* TRANSLATORS: this is when the button is insensitive */
      tooltip = _("The device type is not currently supported.");
    }
out:
  /* control the tooltip and sensitivity of the button */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_device_calibrate"));
  gtk_widget_set_tooltip_text (widget, tooltip);
  gtk_widget_set_sensitive (widget, ret);
}

static void
gcm_prefs_device_clicked (CcColorPanel *prefs, CdDevice *device)
{
  GtkWidget *widget;
  CdDeviceMode device_mode;
  CcColorPanelPrivate *priv = prefs->priv;

  if (device == NULL)
    g_assert_not_reached ();

  /* get current device */
  if (priv->current_device != NULL)
    g_object_unref (priv->current_device);
  priv->current_device = g_object_ref (device);

  /* we have a new device */
  g_debug ("selected device is: %s",
           cd_device_get_id (device));

  /* make sure selectable */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "combobox_profile"));
  gtk_widget_set_sensitive (widget, TRUE);

  /* can we delete this device? */
  device_mode = cd_device_get_mode (priv->current_device);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_device_remove"));
  gtk_widget_set_visible (widget, device_mode == CD_DEVICE_MODE_VIRTUAL);

  /* can this device calibrate */
  gcm_prefs_set_calibrate_button_sensitivity (prefs);
}

static void
gcm_prefs_profile_clicked (CcColorPanel *prefs, CdProfile *profile, CdDevice *device)
{
  GtkWidget *widget;
  CdDeviceRelation relation;
  CcColorPanelPrivate *priv = prefs->priv;

  /* get profile */
  g_debug ("selected profile = %s",
     cd_profile_get_filename (profile));


  /* find the profile relationship */
  relation = cd_device_get_profile_relation (device,
               profile,
               NULL, NULL);

  /* we can only remove hard relationships */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_profile_remove"));
  if (relation == CD_DEVICE_RELATION_HARD)
    {
      gtk_widget_set_tooltip_text (widget, "");
      gtk_widget_set_sensitive (widget, TRUE);
    }
  else
    {
      /* TRANSLATORS: this is when an auto-added profile cannot be removed */
      gtk_widget_set_tooltip_text (widget, _("Cannot remove automatically added profile"));
      gtk_widget_set_sensitive (widget, FALSE);
    }

  /* allow getting profile info */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
               "toolbutton_profile_view"));
  gtk_widget_set_sensitive (widget, TRUE);
}

static void
gcm_prefs_devices_treeview_clicked_cb (GtkTreeSelection *selection,
                                       CcColorPanel *prefs)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  CdDevice *device = NULL;
  CdProfile *profile = NULL;
  GtkWidget *widget;
  CcColorPanelPrivate *priv = prefs->priv;

  /* get selection */
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    g_assert_not_reached ();

  gtk_tree_model_get (model, &iter,
                      GCM_PREFS_COLUMN_DEVICE, &device,
                      GCM_PREFS_COLUMN_PROFILE, &profile,
                      -1);

  /* device actions */
  if (device != NULL)
    gcm_prefs_device_clicked (prefs, device);
  if (profile != NULL)
    gcm_prefs_profile_clicked (prefs, profile, device);

  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_device_default"));
  gtk_widget_set_visible (widget, FALSE);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_device_add"));
  gtk_widget_set_visible (widget, FALSE);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_device_calibrate"));
  gtk_widget_set_visible (widget, profile == NULL);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_profile_view"));
  gtk_widget_set_visible (widget, profile != NULL);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_profile_remove"));
  gtk_widget_set_visible (widget, profile != NULL);

  if (device != NULL)
    g_object_unref (device);
  if (profile != NULL)
    g_object_unref (profile);
}

static void
gcm_prefs_treeview_row_activated_cb (GtkTreeView *tree_view,
                                     GtkTreePath *path,
                                     GtkTreeViewColumn *column,
                                     CcColorPanel *prefs)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  gboolean ret;
  CcColorPanelPrivate *priv = prefs->priv;

  /* get the iter */
  model = GTK_TREE_MODEL (priv->list_store_devices);
  ret = gtk_tree_model_get_iter (model, &iter, path);
  if (!ret)
    return;

  /* make this profile the default */
  gcm_prefs_profile_make_default_internal (prefs, model, &iter);
}

static const gchar *
gcm_prefs_device_kind_to_sort (CdDeviceKind kind)
{
  if (kind == CD_DEVICE_KIND_DISPLAY)
    return "4";
  if (kind == CD_DEVICE_KIND_SCANNER)
    return "3";
  if (kind == CD_DEVICE_KIND_CAMERA)
    return "2";
  if (kind == CD_DEVICE_KIND_PRINTER)
    return "1";
  return "0";
}

static gchar *
gcm_device_get_title (CdDevice *device)
{
  const gchar *model;
  const gchar *vendor;
  GString *string;

  /* try to get a nice string suitable for display */
  vendor = cd_device_get_vendor (device);
  model = cd_device_get_model (device);
  string = g_string_new ("");

  if (vendor != NULL && model != NULL)
    {
      g_string_append_printf (string, "%s - %s",
                              vendor, model);
      goto out;
    }

  /* just model */
  if (model != NULL)
    {
      g_string_append (string, model);
      goto out;
    }

  /* just vendor */
  if (vendor != NULL)
    {
      g_string_append (string, vendor);
      goto out;
    }

  /* fallback to id */
  g_string_append (string, cd_device_get_id (device));
out:
  return g_string_free (string, FALSE);
}

static void
gcm_prefs_set_combo_simple_text (GtkWidget *combo_box)
{
  GtkCellRenderer *renderer;
  GtkListStore *store;

  store = gtk_list_store_new (GCM_PREFS_COMBO_COLUMN_NUM_COLUMNS,
                              G_TYPE_STRING,
                              CD_TYPE_PROFILE,
                              G_TYPE_UINT);
  gtk_combo_box_set_model (GTK_COMBO_BOX (combo_box),
                           GTK_TREE_MODEL (store));
  g_object_unref (store);

  renderer = gtk_cell_renderer_text_new ();
  g_object_set (renderer,
                "ellipsize", PANGO_ELLIPSIZE_END,
                "wrap-mode", PANGO_WRAP_WORD_CHAR,
                "width-chars", 60,
                NULL);
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), renderer,
                                  "text", GCM_PREFS_COMBO_COLUMN_TEXT,
                                  NULL);
}

static void
gcm_prefs_profile_combo_changed_cb (GtkWidget *widget,
                                    CcColorPanel *prefs)
{
  GFile *file = NULL;
  gboolean ret;
  CdProfile *profile = NULL;
  GtkTreeIter iter;
  GtkTreeModel *model;
  GcmPrefsEntryType entry_type;
  CcColorPanelPrivate *priv = prefs->priv;

  /* no devices */
  if (priv->current_device == NULL)
    return;

  /* no selection */
  ret = gtk_combo_box_get_active_iter (GTK_COMBO_BOX(widget), &iter);
  if (!ret)
    return;

  /* get entry */
  model = gtk_combo_box_get_model (GTK_COMBO_BOX(widget));
  gtk_tree_model_get (model, &iter,
                      GCM_PREFS_COMBO_COLUMN_TYPE, &entry_type,
                      -1);

  /* import */
  if (entry_type == GCM_PREFS_ENTRY_TYPE_IMPORT)
    {
      file = gcm_prefs_file_chooser_get_icc_profile (prefs);
      if (file == NULL)
        {
          g_warning ("failed to get ICC file");
          gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);
          goto out;
        }

      /* add to combobox */
      gtk_list_store_append (GTK_LIST_STORE(model), &iter);
      gtk_list_store_set (GTK_LIST_STORE(model), &iter,
                          GCM_PREFS_COMBO_COLUMN_PROFILE, profile,
                          -1);
      gtk_combo_box_set_active_iter (GTK_COMBO_BOX (widget), &iter);
    }
out:
  if (file != NULL)
    g_object_unref (file);
  if (profile != NULL)
    g_object_unref (profile);
}



static void
gcm_prefs_sensor_coldplug (CcColorPanel *prefs)
{
  GPtrArray *sensors;
  GError *error = NULL;
  CcColorPanelPrivate *priv = prefs->priv;

  /* unref old */
  if (priv->sensor != NULL)
    {
      g_object_unref (priv->sensor);
      priv->sensor = NULL;
    }

  /* no present */
  sensors = cd_client_get_sensors_sync (priv->client, NULL, &error);
  if (sensors == NULL)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      goto out;
    }
  if (sensors->len == 0)
    goto out;

  /* save a copy of the sensor */
  priv->sensor = g_object_ref (g_ptr_array_index (sensors, 0));
out:
  if (sensors != NULL)
    g_ptr_array_unref (sensors);
}

static void
gcm_prefs_client_sensor_changed_cb (CdClient *client,
            CdSensor *sensor,
            CcColorPanel *prefs)
{
  gcm_prefs_sensor_coldplug (prefs);
  gcm_prefs_set_calibrate_button_sensitivity (prefs);
}

static const gchar *
gcm_prefs_device_kind_to_icon_name (CdDeviceKind kind)
{
  if (kind == CD_DEVICE_KIND_DISPLAY)
    return "video-display";
  if (kind == CD_DEVICE_KIND_SCANNER)
    return "scanner";
  if (kind == CD_DEVICE_KIND_PRINTER)
    return "printer";
  if (kind == CD_DEVICE_KIND_CAMERA)
    return "camera-photo";
  if (kind == CD_DEVICE_KIND_CAMERA)
    return "camera-photo";
  if (kind == CD_DEVICE_KIND_WEBCAM)
    return "camera-web";
  return "image-missing";
}

static gboolean
gcm_prefs_profile_is_based_from_edid (CdProfile *profile)
{
  if (cd_profile_get_kind (profile) != CD_PROFILE_KIND_DISPLAY_DEVICE)
    return FALSE;
  if (cd_profile_get_metadata_item (profile, "EDID_md5") == NULL)
    return FALSE;
  return TRUE;
}

static GString *
gcm_prefs_get_profile_age_as_string (CdProfile *profile)
{
  gint64 age;
  GString *string;

  if (profile == NULL)
    {
      /* TRANSLATORS: this is when there is no profile for the device */
      string = g_string_new (_("No profile"));
      goto out;
    }

  /* days */
  age = cd_profile_get_age (profile);
  if (age == 0)
    {
      string = g_string_new (NULL);
      goto out;
    }
  age /= 60 * 60 * 24;
  string = g_string_new ("");

  /* approximate years */
  if (age > 365)
    {
      age /= 365;
      g_string_append_printf (string, ngettext (
                              "%i year",
                              "%i years",
                              age), (guint) age);
      goto out;
    }

  /* approximate months */
  if (age > 30)
    {
      age /= 30;
      g_string_append_printf (string, ngettext (
                              "%i month",
                              "%i months",
                              age), (guint) age);
      goto out;
    }

  /* approximate weeks */
  if (age > 7)
    {
      age /= 7;
      g_string_append_printf (string, ngettext (
                              "%i week",
                              "%i weeks",
                              age), (guint) age);
      goto out;
    }

  /* fallback */
  g_string_append_printf (string, _("Less than 1 week"));
out:
  return string;
}

static gchar *
gcm_prefs_get_profile_created_for_sort (CdProfile *profile)
{
  gint64 created;
  gchar *string = NULL;
  GDateTime *dt = NULL;

  /* get profile age */
  created = cd_profile_get_created (profile);
  if (created == 0)
    goto out;
  dt = g_date_time_new_from_unix_utc (created);
  /* note: this is not shown in the UI, just used for sorting */
  string = g_date_time_format (dt, "%Y%m%d");
out:
  if (dt != NULL)
    g_date_time_unref (dt);
  return string;
}

static gchar *
gcm_prefs_get_profile_title (CdProfile *profile)
{
  CdColorspace colorspace;
  const gchar *title;
  gchar *string;

  g_return_val_if_fail (profile != NULL, NULL);

  /* add profile description */
  title = cd_profile_get_title (profile);
  if (title != NULL)
    {
      string = g_strdup (title);
      goto out;
    }

  /* some meta profiles do not have ICC profiles */
  colorspace = cd_profile_get_colorspace (profile);
  if (colorspace == CD_COLORSPACE_RGB)
    {
      string = g_strdup (C_("Colorspace fallback", "Default RGB"));
      goto out;
    }
  if (colorspace == CD_COLORSPACE_CMYK)
    {
      string = g_strdup (C_("Colorspace fallback", "Default CMYK"));
      goto out;
    }
  if (colorspace == CD_COLORSPACE_GRAY)
    {
      string = g_strdup (C_("Colorspace fallback", "Default Gray"));
      goto out;
    }

  /* fall back to ID, ick */
  string = g_strdup (cd_profile_get_id (profile));
out:
  return string;
}

static void
gcm_prefs_device_remove_profiles_phase1 (CcColorPanel *prefs, GtkTreeIter *parent)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  gboolean ret;
  CcColorPanelPrivate *priv = prefs->priv;

  /* get first element */
  model = GTK_TREE_MODEL (priv->list_store_devices);
  ret = gtk_tree_model_iter_children (model, &iter, parent);
  if (!ret)
    return;

  /* mark to be removed */
  do {
    gtk_tree_store_set (priv->list_store_devices, &iter,
                        GCM_PREFS_COLUMN_DEVICE_ID, NULL,
                        -1);
  } while (gtk_tree_model_iter_next (model, &iter));
}

static void
gcm_prefs_device_remove_profiles_phase2 (CcColorPanel *prefs, GtkTreeIter *parent)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  gchar *id_tmp;
  gboolean ret;
  CcColorPanelPrivate *priv = prefs->priv;

  /* get first element */
  model = GTK_TREE_MODEL (priv->list_store_devices);
  ret = gtk_tree_model_iter_children (model, &iter, parent);
  if (!ret)
    return;

  /* remove the other elements */
  do
    {
      gtk_tree_model_get (model, &iter,
              GCM_PREFS_COLUMN_DEVICE_ID, &id_tmp,
              -1);
      if (id_tmp == NULL)
        ret = gtk_tree_store_remove (priv->list_store_devices, &iter);
      else
        ret = gtk_tree_model_iter_next (model, &iter);
      g_free (id_tmp);
    } while (ret);
}

static GtkTreeIter *
get_iter_for_profile (GtkTreeModel *model, CdProfile *profile, GtkTreeIter *parent)
{
  const gchar *id;
  GtkTreeIter iter_tmp;
  gboolean ret;
  GtkTreeIter *iter = NULL;
  CdProfile *profile_tmp;

  /* get first element */
  ret = gtk_tree_model_iter_children (model, &iter_tmp, parent);
  if (!ret)
    goto out;

  /* remove the other elements */
  id = cd_profile_get_id (profile);
  do
    {
      gtk_tree_model_get (model, &iter_tmp,
              GCM_PREFS_COLUMN_PROFILE, &profile_tmp,
              -1);
      if (g_strcmp0 (id, cd_profile_get_id (profile_tmp)) == 0)
        iter = &iter_tmp;
      g_object_unref (profile_tmp);
    } while (iter == NULL && gtk_tree_model_iter_next (model, &iter_tmp));
out:
  return iter;
}

static void
gcm_prefs_device_set_model_by_iter (CcColorPanel *prefs, CdDevice *device, GtkTreeIter *iter)
{
  GString *status;
  const gchar *status_image = NULL;
  const gchar *tooltip = NULL;
  CdProfile *profile = NULL;
  gint age;
  GPtrArray *profiles;
  CdProfile *profile_tmp;
  guint i;
  gchar *title_tmp;
  GString *date_tmp;
  gchar *sort_tmp;
  GtkTreeIter iter_tmp;
  GtkTreeIter *iter_tmp_p;
  guint threshold = 0;
  CcColorPanelPrivate *priv = prefs->priv;

  /* set status */
  profile = cd_device_get_default_profile (device);
  if (profile == NULL)
    {
      status = g_string_new (_("Uncalibrated"));
      g_string_prepend (status, "<span foreground='gray'><i>");
      g_string_append (status, "</i></span>");
      tooltip = _("This device is not color managed.");
      goto skip;
    }

  /* autogenerated printer defaults */
  if (cd_device_get_kind (device) == CD_DEVICE_KIND_PRINTER &&
      cd_profile_get_filename (profile) == NULL)
    {
      status = g_string_new (_("Uncalibrated"));
      g_string_prepend (status, "<span foreground='gray'><i>");
      g_string_append (status, "</i></span>");
      tooltip = _("This device is using manufacturing calibrated data.");
      goto skip;
    }

  /* autogenerated profiles are crap */
  if (cd_profile_get_kind (profile) == CD_PROFILE_KIND_DISPLAY_DEVICE &&
      !cd_profile_get_has_vcgt (profile))
    {
      status = g_string_new (_("Uncalibrated"));
      g_string_prepend (status, "<span foreground='gray'><i>");
      g_string_append (status, "</i></span>");
      tooltip = _("This device does not have a profile suitable for whole-screen color correction.");
      goto skip;
    }

  /* yay! */
  status = gcm_prefs_get_profile_age_as_string (profile);

  /* greater than the calibration threshold for the device type */
  age = cd_profile_get_age (profile);
  age /= 60 * 60 * 24;
  if (cd_device_get_kind (device) == CD_DEVICE_KIND_DISPLAY)
    {
      g_settings_get (priv->settings,
                      GCM_SETTINGS_RECALIBRATE_DISPLAY_THRESHOLD,
                      "u",
                      &threshold);
    }
  else if (cd_device_get_kind (device) == CD_DEVICE_KIND_DISPLAY)
    {
      g_settings_get (priv->settings,
                      GCM_SETTINGS_RECALIBRATE_PRINTER_THRESHOLD,
                      "u",
                      &threshold);
    }
  if (threshold > 0 && age > threshold)
    {
      status_image = "dialog-warning-symbolic";
      tooltip = _("This device has an old profile that may no longer be accurate.");
    }
skip:
  /* save to store */
  gtk_tree_store_set (priv->list_store_devices, iter,
                      GCM_PREFS_COLUMN_STATUS, status->str,
                      GCM_PREFS_COLUMN_STATUS_IMAGE, status_image,
                      GCM_PREFS_COLUMN_TOOLTIP, tooltip,
                      -1);

  /* remove old profiles */
  gcm_prefs_device_remove_profiles_phase1 (prefs, iter);

  /* add profiles */
  profiles = cd_device_get_profiles (device);
  if (profiles == NULL)
    goto out;
  for (i = 0; i < profiles->len; i++)
    {
      profile_tmp = g_ptr_array_index (profiles, i);
      title_tmp = gcm_prefs_get_profile_title (profile_tmp);

      /* don't show details for EDID profiles */
      if (gcm_prefs_profile_is_based_from_edid (profile_tmp))
        {
          date_tmp = g_string_new ("Not specified");
          g_string_prepend (date_tmp, "<span foreground='gray'><i>");
          g_string_append (date_tmp, "</i></span>");
        }
      else
        {
          date_tmp = gcm_prefs_get_profile_age_as_string (profile_tmp);
        }
      sort_tmp = gcm_prefs_get_profile_created_for_sort (profile_tmp);

      /* get an existing profile, or create a new one */
      iter_tmp_p = get_iter_for_profile (GTK_TREE_MODEL (priv->list_store_devices),
          profile_tmp, iter);
      if (iter_tmp_p == NULL)
        {
          gtk_tree_store_append (priv->list_store_devices, &iter_tmp, iter);
          iter_tmp_p = &iter_tmp;
        }

      gtk_tree_store_set (priv->list_store_devices, iter_tmp_p,
                          GCM_PREFS_COLUMN_DEVICE, device,
                          GCM_PREFS_COLUMN_PROFILE, profile_tmp,
                          GCM_PREFS_COLUMN_DEVICE_ID, cd_device_get_id (device),
                          GCM_PREFS_COLUMN_SORT, sort_tmp,
                          GCM_PREFS_COLUMN_STATUS, date_tmp->str,
                          GCM_PREFS_COLUMN_TITLE, title_tmp,
                          GCM_PREFS_COLUMN_RADIO_VISIBLE, TRUE,
                          GCM_PREFS_COLUMN_RADIO_ACTIVE, i==0,
                          -1);
      g_free (title_tmp);
      g_free (sort_tmp);
      g_string_free (date_tmp, TRUE);
    }

  /* remove old profiles that no longer exist */
  gcm_prefs_device_remove_profiles_phase2 (prefs, iter);
out:
  g_string_free (status, TRUE);
  if (profiles != NULL)
    g_ptr_array_unref (profiles);
  if (profile != NULL)
    g_object_unref (profile);
}

static void
gcm_prefs_device_changed_cb (CdDevice *device, CcColorPanel *prefs)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  const gchar *id;
  gchar *id_tmp;
  gboolean ret;
  CcColorPanelPrivate *priv = prefs->priv;

  /* get first element */
  model = GTK_TREE_MODEL (priv->list_store_devices);
  ret = gtk_tree_model_get_iter_first (model, &iter);
  if (!ret)
    return;

  /* get the other elements */
  id = cd_device_get_id (device);
  do
    {
      gtk_tree_model_get (model, &iter,
              GCM_PREFS_COLUMN_DEVICE_ID, &id_tmp,
              -1);
      if (g_strcmp0 (id_tmp, id) == 0)
        gcm_prefs_device_set_model_by_iter (prefs, device, &iter);
      g_free (id_tmp);
    } while (gtk_tree_model_iter_next (model, &iter));
}

static void
gcm_prefs_add_device (CcColorPanel *prefs, CdDevice *device)
{
  CdDeviceKind kind;
  const gchar *icon_name;
  const gchar *id;
  gchar *sort = NULL;
  gchar *title = NULL;
  GtkTreeIter parent;
  CcColorPanelPrivate *priv = prefs->priv;

  /* get icon */
  kind = cd_device_get_kind (device);
  icon_name = gcm_prefs_device_kind_to_icon_name (kind);

  /* italic for non-connected devices */
  title = gcm_device_get_title (device);

  /* create sort order */
  sort = g_strdup_printf ("%s%s",
        gcm_prefs_device_kind_to_sort (kind),
        title);

  /* watch for changes to update the status icons */
  g_signal_connect (device, "changed",
                    G_CALLBACK (gcm_prefs_device_changed_cb), prefs);

  /* add to list */
  id = cd_device_get_id (device);
  g_debug ("add %s to device list", id);
  gtk_tree_store_append (priv->list_store_devices, &parent, NULL);
  gtk_tree_store_set (priv->list_store_devices, &parent,
          GCM_PREFS_COLUMN_DEVICE, device,
          GCM_PREFS_COLUMN_DEVICE_ID, id,
          GCM_PREFS_COLUMN_SORT, sort,
          GCM_PREFS_COLUMN_TITLE, title,
          GCM_PREFS_COLUMN_ICON, icon_name,
          -1);
  gcm_prefs_device_set_model_by_iter (prefs, device, &parent);
  g_free (sort);
  g_free (title);
}

static void
gcm_prefs_remove_device (CcColorPanel *prefs, CdDevice *cd_device)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  const gchar *id;
  gchar *id_tmp;
  gboolean ret;
  CcColorPanelPrivate *priv = prefs->priv;

  /* remove */
  id = cd_device_get_id (cd_device);

  /* get first element */
  model = GTK_TREE_MODEL (priv->list_store_devices);
  ret = gtk_tree_model_get_iter_first (model, &iter);
  if (!ret)
    return;

  /* get the other elements */
  do
    {
      gtk_tree_model_get (model, &iter,
              GCM_PREFS_COLUMN_DEVICE_ID, &id_tmp,
              -1);
      if (g_strcmp0 (id_tmp, id) == 0)
        {
          gtk_list_store_remove (GTK_LIST_STORE(model), &iter);
          g_free (id_tmp);
          break;
        }
      g_free (id_tmp);
    } while (gtk_tree_model_iter_next (model, &iter));
}

static void
gcm_prefs_device_added_cb (CdClient *client,
                           CdDevice *device,
                           CcColorPanel *prefs)
{
  /* remove the saved device if it's already there */
  gcm_prefs_remove_device (prefs, device);

  /* add the device */
  gcm_prefs_add_device (prefs, device);
}

static void
gcm_prefs_changed_cb (CdClient *client,
                      CdDevice *device,
                      CcColorPanel *prefs)
{
  g_debug ("changed: %s (doing nothing)", cd_device_get_id (device));
}

static void
gcm_prefs_device_removed_cb (CdClient *client,
                             CdDevice *device,
                             CcColorPanel *prefs)
{
  GtkTreeIter iter;
  GtkTreeSelection *selection;
  GtkWidget *widget;
  gboolean ret;
  CcColorPanelPrivate *priv = prefs->priv;

  /* remove from the UI */
  gcm_prefs_remove_device (prefs, device);

  /* select the first device */
  ret = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (priv->list_store_devices), &iter);
  if (!ret)
    return;

  /* click it */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "treeview_devices"));
  gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
                           GTK_TREE_MODEL (priv->list_store_devices));
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
  gtk_tree_selection_select_iter (selection, &iter);
}

static void
gcm_prefs_get_devices_cb (GObject *object,
                          GAsyncResult *res,
                          gpointer user_data)
{
  CcColorPanel *prefs = (CcColorPanel *) user_data;
  CdClient *client = CD_CLIENT (object);
  CdDevice *device;
  GError *error = NULL;
  GPtrArray *devices;
  GtkTreePath *path;
  GtkWidget *widget;
  guint i;
  CcColorPanelPrivate *priv = prefs->priv;

  /* get devices and add them */
  devices = cd_client_get_devices_finish (client, res, &error);
  if (devices == NULL)
    {
      g_warning ("failed to add connected devices: %s",
                 error->message);
      g_error_free (error);
      goto out;
    }
  for (i = 0; i < devices->len; i++)
    {
      device = g_ptr_array_index (devices, i);
      gcm_prefs_add_device (prefs, device);
    }

  /* set the cursor on the first device */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "treeview_devices"));
  path = gtk_tree_path_new_from_string ("0");
  gtk_tree_view_set_cursor (GTK_TREE_VIEW (widget), path, NULL, FALSE);
  gtk_tree_path_free (path);
out:
  if (devices != NULL)
    g_ptr_array_unref (devices);
}

static void
gcm_prefs_button_virtual_add_cb (GtkWidget *widget, CcColorPanel *prefs)
{
  CdDeviceKind device_kind;
  CdDevice *device;
  const gchar *model;
  const gchar *manufacturer;
  gchar *device_id;
  GError *error = NULL;
  GHashTable *device_props;
  CcColorPanelPrivate *priv = prefs->priv;

  /* get device details */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "combobox_virtual_type"));
  device_kind = gtk_combo_box_get_active (GTK_COMBO_BOX(widget)) + 2;
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "entry_virtual_model"));
  model = gtk_entry_get_text (GTK_ENTRY (widget));
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "entry_virtual_manufacturer"));
  manufacturer = gtk_entry_get_text (GTK_ENTRY (widget));

  /* create device */
  device_id = g_strdup_printf ("%s-%s-%s",
                               cd_device_kind_to_string (device_kind),
                               manufacturer,
                               model);
  device_props = g_hash_table_new_full (g_str_hash, g_str_equal,
                                        g_free, g_free);
  g_hash_table_insert (device_props,
                       g_strdup ("Kind"),
                       g_strdup (cd_device_kind_to_string (device_kind)));
  g_hash_table_insert (device_props,
                       g_strdup ("Mode"),
                       g_strdup (cd_device_mode_to_string (CD_DEVICE_MODE_VIRTUAL)));
  g_hash_table_insert (device_props,
                       g_strdup ("Colorspace"),
                       g_strdup (cd_colorspace_to_string (CD_COLORSPACE_RGB)));
  g_hash_table_insert (device_props,
                       g_strdup ("Model"),
                       g_strdup (model));
  g_hash_table_insert (device_props,
                       g_strdup ("Vendor"),
                       g_strdup (manufacturer));
  device = cd_client_create_device_sync (priv->client,
                                         device_id,
                                         CD_OBJECT_SCOPE_DISK,
                                         device_props,
                                         priv->cancellable,
                                         &error);
  if (device == NULL)
    {
      g_warning ("Failed to add create virtual device: %s",
                 error->message);
      g_error_free (error);
      goto out;
    }
out:
  g_hash_table_unref (device_props);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "dialog_virtual"));
  gtk_widget_hide (widget);
  g_free (device_id);
}

static void
gcm_prefs_button_virtual_cancel_cb (GtkWidget *widget, CcColorPanel *prefs)
{
  CcColorPanelPrivate *priv = prefs->priv;
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "dialog_virtual"));
  gtk_widget_hide (widget);
}

static gboolean
gcm_prefs_virtual_delete_event_cb (GtkWidget *widget,
                                   GdkEvent *event,
                                   CcColorPanel *prefs)
{
  gcm_prefs_button_virtual_cancel_cb (widget, prefs);
  return TRUE;
}

static const gchar *
cd_device_kind_to_localised_string (CdDeviceKind device_kind)
{
  if (device_kind == CD_DEVICE_KIND_DISPLAY)
    return C_("Device kind", "Display");
  if (device_kind == CD_DEVICE_KIND_SCANNER)
    return C_("Device kind", "Scanner");
  if (device_kind == CD_DEVICE_KIND_PRINTER)
    return C_("Device kind", "Printer");
  if (device_kind == CD_DEVICE_KIND_CAMERA)
    return C_("Device kind", "Camera");
  if (device_kind == CD_DEVICE_KIND_WEBCAM)
    return C_("Device kind", "Webcam");
  return NULL;
}

static void
gcm_prefs_setup_virtual_combobox (GtkWidget *widget)
{
  guint i;
  const gchar *text;

  for (i=CD_DEVICE_KIND_SCANNER; i<CD_DEVICE_KIND_LAST; i++)
    {
      text = cd_device_kind_to_localised_string (i);
      if (text == NULL)
        continue;
      gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT(widget), text);
    }
  gtk_combo_box_set_active (GTK_COMBO_BOX (widget), CD_DEVICE_KIND_PRINTER - 2);
}

static gboolean
gcm_prefs_virtual_set_from_file (CcColorPanel *prefs, GFile *file)
{
  /* TODO: use GCM to get the EXIF data */
  return FALSE;
}

static void
gcm_prefs_virtual_drag_data_received_cb (GtkWidget *widget,
                                         GdkDragContext *context,
                                         gint x, gint y,
                                         GtkSelectionData *data,
                                         guint info,
                                         guint _time,
                                         CcColorPanel *prefs)
{
  const guchar *filename;
  gchar **filenames = NULL;
  GFile *file = NULL;
  guint i;
  gboolean ret;

  /* get filenames */
  filename = gtk_selection_data_get_data (data);
  if (filename == NULL)
    {
      gtk_drag_finish (context, FALSE, FALSE, _time);
      goto out;
    }

  /* import this */
  g_debug ("dropped: %p (%s)", data, filename);

  /* split, as multiple drag targets are accepted */
  filenames = g_strsplit_set ((const gchar *)filename, "\r\n", -1);
  for (i = 0; filenames[i] != NULL; i++)
    {
      /* blank entry */
      if (filenames[i][0] == '\0')
        continue;

      /* check this is a parsable file */
      g_debug ("trying to set %s", filenames[i]);
      file = g_file_new_for_uri (filenames[i]);
      ret = gcm_prefs_virtual_set_from_file (prefs, file);
      if (!ret)
        {
          g_debug ("%s did not set from file correctly",
                   filenames[i]);
          gtk_drag_finish (context, FALSE, FALSE, _time);
          goto out;
        }
      g_object_unref (file);
      file = NULL;
    }

  gtk_drag_finish (context, TRUE, FALSE, _time);
out:
  if (file != NULL)
    g_object_unref (file);
  g_strfreev (filenames);
}

static void
gcm_prefs_setup_drag_and_drop (GtkWidget *widget)
{
  GtkTargetEntry entry;

  /* setup a dummy entry */
  entry.target = g_strdup ("text/plain");
  entry.flags = GTK_TARGET_OTHER_APP;
  entry.info = 0;

  gtk_drag_dest_set (widget,
                     GTK_DEST_DEFAULT_ALL,
                     &entry,
                     1,
                     GDK_ACTION_MOVE | GDK_ACTION_COPY);
  g_free (entry.target);
}

static void
cc_color_panel_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
cc_color_panel_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
cc_color_panel_dispose (GObject *object)
{
  CcColorPanelPrivate *priv = CC_COLOR_PANEL (object)->priv;

  if (priv->settings)
    {
      g_object_unref (priv->settings);
      priv->settings = NULL;
    }
  if (priv->cancellable != NULL)
    {
      g_cancellable_cancel (priv->cancellable);
      g_object_unref (priv->cancellable);
      priv->cancellable = NULL;
    }
  if (priv->builder != NULL)
    {
      g_object_unref (priv->builder);
      priv->builder = NULL;
    }
  if (priv->client != NULL)
    {
      g_object_unref (priv->client);
      priv->client = NULL;
    }
  if (priv->current_device != NULL)
    {
      g_object_unref (priv->current_device);
      priv->current_device = NULL;
    }
  if (priv->sensor != NULL)
    {
      g_object_unref (priv->sensor);
      priv->sensor = NULL;
    }

  G_OBJECT_CLASS (cc_color_panel_parent_class)->dispose (object);
}

static void
cc_color_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_color_panel_parent_class)->finalize (object);
}

static void
cc_color_panel_class_init (CcColorPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (CcColorPanelPrivate));

  object_class->get_property = cc_color_panel_get_property;
  object_class->set_property = cc_color_panel_set_property;
  object_class->dispose = cc_color_panel_dispose;
  object_class->finalize = cc_color_panel_finalize;
}

static void
cc_color_panel_class_finalize (CcColorPanelClass *klass)
{
}

static void
cc_color_panel_init (CcColorPanel *prefs)
{
  CcColorPanelPrivate *priv;
  gboolean ret;
  gchar *text = NULL;
  GError *error = NULL;
  GtkStyleContext *context;
  GtkTreeSelection *selection;
  GtkWidget *widget;

  priv = prefs->priv = COLOR_PANEL_PRIVATE (prefs);

  priv->builder = gtk_builder_new ();
  gtk_builder_add_from_file (priv->builder,
                             GNOMECC_UI_DIR "/color.ui",
                             &error);

  if (error != NULL)
    {
      g_warning ("Could not load interface file: %s", error->message);
      g_error_free (error);
      return;
    }

  priv->cancellable = g_cancellable_new ();

  /* setup defaults */
  priv->settings = g_settings_new (GCM_SETTINGS_SCHEMA);

  /* create list stores */
  priv->list_store_devices = gtk_tree_store_new (GCM_PREFS_COLUMN_NUM_COLUMNS,
                                                 G_TYPE_STRING,
                                                 G_TYPE_STRING,
                                                 G_TYPE_STRING,
                                                 G_TYPE_STRING,
                                                 CD_TYPE_DEVICE,
                                                 CD_TYPE_PROFILE,
                                                 G_TYPE_STRING,
                                                 G_TYPE_STRING,
                                                 G_TYPE_STRING,
                                                 G_TYPE_BOOLEAN,
                                                 G_TYPE_BOOLEAN);

  /* assign buttons */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_profile_add"));
  g_signal_connect (widget, "clicked",
                    G_CALLBACK (gcm_prefs_profile_add_cb), prefs);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_profile_remove"));
  g_signal_connect (widget, "clicked",
                    G_CALLBACK (gcm_prefs_profile_remove_cb), prefs);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_profile_view"));
  g_signal_connect (widget, "clicked",
                    G_CALLBACK (gcm_prefs_profile_view_cb), prefs);

  /* create device tree view */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "treeview_devices"));
  gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
                           GTK_TREE_MODEL (priv->list_store_devices));
  gtk_tree_view_set_enable_tree_lines (GTK_TREE_VIEW (widget), TRUE);
  gtk_tree_view_set_level_indentation (GTK_TREE_VIEW (widget), 0);
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
  g_signal_connect (selection, "changed",
                    G_CALLBACK (gcm_prefs_devices_treeview_clicked_cb),
                    prefs);
  g_signal_connect (GTK_TREE_VIEW (widget), "row-activated",
                    G_CALLBACK (gcm_prefs_treeview_row_activated_cb),
                    prefs);

  /* add columns to the tree view */
  gcm_prefs_add_devices_columns (prefs, GTK_TREE_VIEW (widget));

  /* force to be at least ~6 rows high */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "scrolledwindow_devices"));
  gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (widget),
                                              200);

  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_device_default"));
  g_signal_connect (widget, "clicked",
                    G_CALLBACK (gcm_prefs_default_cb), prefs);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_device_remove"));
  g_signal_connect (widget, "clicked",
                    G_CALLBACK (gcm_prefs_delete_cb), prefs);
  gtk_widget_set_sensitive (widget, FALSE);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_device_add"));
  g_signal_connect (widget, "clicked",
                    G_CALLBACK (gcm_prefs_device_add_cb), prefs);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbutton_device_calibrate"));
  g_signal_connect (widget, "clicked",
                    G_CALLBACK (gcm_prefs_calibrate_cb), prefs);

  /* make devices toolbar sexy */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "scrolledwindow_devices"));
  context = gtk_widget_get_style_context (widget);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);

  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "toolbar_devices"));
  context = gtk_widget_get_style_context (widget);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_INLINE_TOOLBAR);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  /* set up virtual dialog */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "dialog_virtual"));
  g_signal_connect (widget, "delete-event",
                    G_CALLBACK (gcm_prefs_virtual_delete_event_cb),
                    prefs);
  g_signal_connect (widget, "drag-data-received",
                    G_CALLBACK (gcm_prefs_virtual_drag_data_received_cb),
                    prefs);
  gcm_prefs_setup_drag_and_drop (widget);

  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "button_virtual_add"));
  g_signal_connect (widget, "clicked",
                    G_CALLBACK (gcm_prefs_button_virtual_add_cb),
                    prefs);
  gtk_widget_set_sensitive (widget, FALSE);

  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "button_virtual_cancel"));
  g_signal_connect (widget, "clicked",
                    G_CALLBACK (gcm_prefs_button_virtual_cancel_cb),
                    prefs);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "combobox_virtual_type"));
  gcm_prefs_setup_virtual_combobox (widget);

  /* set up assign dialog */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "dialog_assign"));
  g_signal_connect (widget, "delete-event",
                    G_CALLBACK (gcm_prefs_profile_delete_event_cb), prefs);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "button_assign_cancel"));
  g_signal_connect (widget, "clicked",
                    G_CALLBACK (gcm_prefs_button_assign_cancel_cb), prefs);
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "button_assign_ok"));
  g_signal_connect (widget, "clicked",
                    G_CALLBACK (gcm_prefs_button_assign_ok_cb), prefs);

  /* setup icc profiles list */
  widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                               "combobox_profile"));
  gcm_prefs_set_combo_simple_text (widget);
  gtk_widget_set_sensitive (widget, FALSE);
  g_signal_connect (G_OBJECT (widget), "changed",
                    G_CALLBACK (gcm_prefs_profile_combo_changed_cb), prefs);

  /* use a device client array */
  priv->client = cd_client_new ();
  g_signal_connect (priv->client, "device-added",
                    G_CALLBACK (gcm_prefs_device_added_cb), prefs);
  g_signal_connect (priv->client, "device-removed",
                    G_CALLBACK (gcm_prefs_device_removed_cb), prefs);
  g_signal_connect (priv->client, "changed",
                    G_CALLBACK (gcm_prefs_changed_cb), prefs);

  /* connect to colord */
  ret = cd_client_connect_sync (priv->client,
                                priv->cancellable,
                                &error);
  if (!ret)
    {
      g_warning ("failed to connect to colord: %s", error->message);
      g_error_free (error);
      goto out;
    }

  /* get devices async */
  cd_client_get_devices (priv->client,
                         priv->cancellable,
                         gcm_prefs_get_devices_cb,
                         prefs);

  /* use the color sensor */
  g_signal_connect (priv->client, "sensor-added",
                    G_CALLBACK (gcm_prefs_client_sensor_changed_cb),
                    prefs);
  g_signal_connect (priv->client, "sensor-removed",
                    G_CALLBACK (gcm_prefs_client_sensor_changed_cb),
                    prefs);

out:
  g_free (text);

  /* set calibrate button sensitivity */
  gcm_prefs_sensor_coldplug (prefs);
  gcm_prefs_set_calibrate_button_sensitivity (prefs);

  widget = WID (priv->builder, "dialog-vbox1");
  gtk_widget_reparent (widget, (GtkWidget *) prefs);
  priv->main_window = gtk_widget_get_toplevel (widget);
}

void
cc_color_panel_register (GIOModule *module)
{
  cc_color_panel_register_type (G_TYPE_MODULE (module));
  g_io_extension_point_implement (CC_SHELL_PANEL_EXTENSION_POINT,
                                  CC_TYPE_COLOR_PANEL,
                                  "color", 0);
}
