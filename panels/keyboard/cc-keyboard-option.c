/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Written by: Rui Matos <rmatos@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <config.h>
#include <glib/gi18n.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-xkb-info.h>

#include "cc-util.h"
#include "cc-keyboard-option.h"

#define CC_TYPE_KEYBOARD_OPTION            (cc_keyboard_option_get_type ())
#define CC_KEYBOARD_OPTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CC_TYPE_KEYBOARD_OPTION, CcKeyboardOption))
#define CC_KEYBOARD_OPTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  CC_TYPE_KEYBOARD_OPTION, CcKeyboardOptionClass))
#define CC_IS_KEYBOARD_OPTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CC_TYPE_KEYBOARD_OPTION))
#define CC_IS_KEYBOARD_OPTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  CC_TYPE_KEYBOARD_OPTION))
#define CC_KEYBOARD_OPTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  CC_TYPE_KEYBOARD_OPTION, CcKeyboardOptionClass))

#define INPUT_SOURCES_SCHEMA "org.gnome.desktop.input-sources"
#define XKB_OPTIONS_KEY "xkb-options"

#define XKB_OPTION_GROUP_LVL3 "lv3"
#define XKB_OPTION_GROUP_COMP "Compose key"

#define INPUT_SWITCHER_SCHEMA "org.gnome.settings-daemon.peripherals.keyboard"
#define INPUT_SWITCHER_KEY "input-sources-switcher"

enum
{
  PROP_0,
  PROP_GROUP,
  PROP_DESCRIPTION
};

enum
{
  CHANGED_SIGNAL,
  LAST_SIGNAL
};

struct _CcKeyboardOption
{
  GObject parent_object;

  gchar *group;
  gchar *description;
  gchar *current_value;
  GtkListStore *store;

  const gchar * const *whitelist;

  gboolean is_xkb;

  gchar *current_description;
};

typedef struct _CcKeyboardOptionClass CcKeyboardOptionClass;
struct _CcKeyboardOptionClass
{
  GObjectClass parent_class;
};

static guint keyboard_option_signals[LAST_SIGNAL] = { 0 };

static GnomeXkbInfo *xkb_info = NULL;
static GSettings *input_sources_settings = NULL;
static GSettings *input_switcher_settings = NULL;
static gchar **current_xkb_options = NULL;

static const gchar *xkb_option_lvl3_whitelist[] = {
  "lv3:switch",
  "lv3:menu_switch",
  "lv3:rwin_switch",
  "lv3:lalt_switch",
  "lv3:ralt_switch",
  "lv3:caps_switch",
  NULL
};

static const gchar *xkb_option_comp_whitelist[] = {
  "compose:ralt",
  "compose:rwin",
  "compose:menu",
  "compose:lctrl",
  "compose:rctrl",
  "compose:caps",
  NULL
};

static GList *objects_list = NULL;

GType cc_keyboard_option_get_type (void);

G_DEFINE_TYPE (CcKeyboardOption, cc_keyboard_option, G_TYPE_OBJECT);

static gboolean
strv_contains (const gchar * const *strv,
               const gchar         *str)
{
  const gchar * const *p = strv;
  for (p = strv; *p; p++)
    if (g_strcmp0 (*p, str) == 0)
      return TRUE;

  return FALSE;
}

static void
reload_setting (CcKeyboardOption *self)
{
  gchar **iter;

  if (!self->is_xkb)
    return;

  for (iter = current_xkb_options; *iter; ++iter)
    if (strv_contains (self->whitelist, *iter))
      {
        if (g_strcmp0 (self->current_value, *iter) != 0)
          {
            g_free (self->current_value);
            self->current_value = g_strdup (*iter);
            g_signal_emit (self, keyboard_option_signals[CHANGED_SIGNAL], 0);
          }
        break;
      }

  if (*iter == NULL && self->current_value != NULL)
    {
      g_clear_pointer (&self->current_value, g_free);
      g_signal_emit (self, keyboard_option_signals[CHANGED_SIGNAL], 0);
    }
}

static void
xkb_options_changed (GSettings *settings,
                     gchar     *key,
                     gpointer   data)
{
  GList *l;

  g_strfreev (current_xkb_options);
  current_xkb_options = g_settings_get_strv (settings, key);

  for (l = objects_list; l; l = l->next)
    reload_setting (CC_KEYBOARD_OPTION (l->data));
}

static void
input_switcher_changed (GSettings *settings,
                        gchar     *key,
                        gpointer   data)
{
  CcKeyboardOption *option = data;

  g_free (option->current_value);
  g_free (option->current_description);
  option->current_value = g_settings_get_string (settings, key);
  option->current_description = NULL;

  g_signal_emit (option, keyboard_option_signals[CHANGED_SIGNAL], 0);
}

static void
cc_keyboard_option_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  CcKeyboardOption *self;

  self = CC_KEYBOARD_OPTION (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      g_value_set_string (value, self->group);
      break;
    case PROP_DESCRIPTION:
      g_value_set_string (value, self->description);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cc_keyboard_option_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  CcKeyboardOption *self;

  self = CC_KEYBOARD_OPTION (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      self->group = g_value_dup_string (value);
      break;
    case PROP_DESCRIPTION:
      self->description = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cc_keyboard_option_init (CcKeyboardOption *self)
{
}

static void
cc_keyboard_option_finalize (GObject *object)
{
  CcKeyboardOption *self = CC_KEYBOARD_OPTION (object);

  g_clear_pointer (&self->group, g_free);
  g_clear_pointer (&self->description, g_free);
  g_clear_pointer (&self->current_value, g_free);
  g_clear_pointer (&self->current_description, g_free);
  g_clear_object (&self->store);

  G_OBJECT_CLASS (cc_keyboard_option_parent_class)->finalize (object);
}

static void
cc_keyboard_option_constructed (GObject *object)
{
  GtkTreeIter iter;
  GList *options, *l;
  gchar *option_id;
  CcKeyboardOption *self = CC_KEYBOARD_OPTION (object);

  G_OBJECT_CLASS (cc_keyboard_option_parent_class)->constructed (object);

  self->is_xkb = TRUE;

  if (self->group == NULL)
    self->is_xkb = FALSE;
  else if (g_str_equal (self->group, XKB_OPTION_GROUP_LVL3))
    self->whitelist = xkb_option_lvl3_whitelist;
  else if (g_str_equal (self->group, XKB_OPTION_GROUP_COMP))
    self->whitelist = xkb_option_comp_whitelist;
  else
    g_assert_not_reached ();

  self->store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);

  if (self->is_xkb)
    {
      gtk_list_store_insert_with_values (self->store, &iter, -1,
                                         XKB_OPTION_DESCRIPTION_COLUMN, _("Disabled"),
                                         XKB_OPTION_ID_COLUMN, NULL,
                                         -1);
      options = gnome_xkb_info_get_options_for_group (xkb_info, self->group);
      for (l = options; l; l = l->next)
        {
          option_id = l->data;
          if (strv_contains (self->whitelist, option_id))
            {
              gtk_list_store_insert_with_values (self->store, &iter, -1,
                                                 XKB_OPTION_DESCRIPTION_COLUMN,
                                                 gnome_xkb_info_description_for_option (xkb_info, self->group, option_id),
                                                 XKB_OPTION_ID_COLUMN, option_id,
                                                 -1);
            }
        }
      g_list_free (options);

      reload_setting (self);
    }
  else
    {
      gint i;
      for (i = 0; cc_input_switcher_options[i].value; i++)
        {
          gtk_list_store_insert_with_values (self->store, NULL, -1,
                                             XKB_OPTION_DESCRIPTION_COLUMN, _(cc_input_switcher_options[i].description),
                                             XKB_OPTION_ID_COLUMN, cc_input_switcher_options[i].value,
                                             -1);
        }
    }
}

static void
cc_keyboard_option_class_init (CcKeyboardOptionClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = cc_keyboard_option_get_property;
  gobject_class->set_property = cc_keyboard_option_set_property;
  gobject_class->finalize = cc_keyboard_option_finalize;
  gobject_class->constructed = cc_keyboard_option_constructed;

  g_object_class_install_property (gobject_class,
                                   PROP_GROUP,
                                   g_param_spec_string ("group",
                                                        "group",
                                                        "xkb option group identifier",
                                                        NULL,
                                                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class,
                                   PROP_DESCRIPTION,
                                   g_param_spec_string ("description",
                                                        "description",
                                                        "translated option description",
                                                        NULL,
                                                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  keyboard_option_signals[CHANGED_SIGNAL] = g_signal_new ("changed",
                                                          CC_TYPE_KEYBOARD_OPTION,
                                                          G_SIGNAL_RUN_LAST,
                                                          0, NULL, NULL, NULL,
                                                          G_TYPE_NONE,
                                                          0);
}

GList *
cc_keyboard_option_get_all (void)
{
  CcKeyboardOption *option;

  if (objects_list)
    return objects_list;

  xkb_info = gnome_xkb_info_new ();

  input_sources_settings = g_settings_new (INPUT_SOURCES_SCHEMA);

  g_signal_connect (input_sources_settings, "changed::" XKB_OPTIONS_KEY,
                    G_CALLBACK (xkb_options_changed), NULL);

  xkb_options_changed (input_sources_settings, XKB_OPTIONS_KEY, NULL);

  objects_list = g_list_prepend (objects_list,
                                 g_object_new (CC_TYPE_KEYBOARD_OPTION,
                                               "group", XKB_OPTION_GROUP_LVL3,
                                               "description", _("Alternative Characters Key"),
                                               NULL));
  objects_list = g_list_prepend (objects_list,
                                 g_object_new (CC_TYPE_KEYBOARD_OPTION,
                                               "group", XKB_OPTION_GROUP_COMP,
                                               "description", _("Compose Key"),
                                               NULL));

  option = g_object_new (CC_TYPE_KEYBOARD_OPTION,
                         "description", _("Modifiers-only switch to next source"),
                         NULL);

  input_switcher_settings = g_settings_new (INPUT_SWITCHER_SCHEMA);
  g_signal_connect (input_switcher_settings, "changed::" INPUT_SWITCHER_KEY,
                    G_CALLBACK (input_switcher_changed), option);
  input_switcher_changed (input_switcher_settings, INPUT_SWITCHER_KEY, option);

  objects_list = g_list_prepend (objects_list, option);

  return objects_list;
}

const gchar *
cc_keyboard_option_get_description (CcKeyboardOption *self)
{
  g_return_val_if_fail (CC_IS_KEYBOARD_OPTION (self), NULL);

  return self->description;
}

GtkListStore *
cc_keyboard_option_get_store (CcKeyboardOption *self)
{
  g_return_val_if_fail (CC_IS_KEYBOARD_OPTION (self), NULL);

  return self->store;
}

const gchar *
cc_keyboard_option_get_current_value_description (CcKeyboardOption *self)
{
  g_return_val_if_fail (CC_IS_KEYBOARD_OPTION (self), NULL);

  if (self->is_xkb)
    {
      if (!self->current_value)
        return _("Disabled");

      return gnome_xkb_info_description_for_option (xkb_info, self->group, self->current_value);
    }
  else
    {
      GtkTreeIter iter;
      gboolean ret;
      gchar *desc;
      gchar *id;

      if (self->current_description)
        return self->current_description;

      ret = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (self->store), &iter);
      while (ret)
        {
          gtk_tree_model_get (GTK_TREE_MODEL (self->store), &iter,
                              XKB_OPTION_DESCRIPTION_COLUMN, &desc,
                              XKB_OPTION_ID_COLUMN, &id,
                              -1);
          if (g_strcmp0 (self->current_value, id) == 0)
            {
              g_free (id);
              g_free (self->current_description);
              self->current_description = desc;
              break;
            }
          g_free (id);
          g_free (desc);
          ret = gtk_tree_model_iter_next (GTK_TREE_MODEL (self->store), &iter);
        }

      return self->current_description;
    }
}

static void
remove_value (const gchar *value)
{
  gchar **p;

  for (p = current_xkb_options; *p; ++p)
    if (g_str_equal (*p, value))
      {
        g_free (*p);
        break;
      }

  for (++p; *p; ++p)
    *(p - 1) = *p;

  *(p - 1) = NULL;
}

static void
add_value (const gchar *value)
{
  gchar **new_xkb_options;
  gchar **a, **b;

  new_xkb_options = g_new0 (gchar *, g_strv_length (current_xkb_options) + 2);

  a = new_xkb_options;
  for (b = current_xkb_options; *b; ++a, ++b)
    *a = g_strdup (*b);

  *a = g_strdup (value);

  g_strfreev (current_xkb_options);
  current_xkb_options = new_xkb_options;
}

static void
replace_value (const gchar *old,
               const gchar *new)
{
  gchar **iter;

  if (g_str_equal (old, new))
    return;

  for (iter = current_xkb_options; *iter; ++iter)
    if (g_str_equal (*iter, old))
      {
        g_free (*iter);
        *iter = g_strdup (new);
        break;
      }
}

void
cc_keyboard_option_set_selection (CcKeyboardOption *self,
                                  GtkTreeIter      *iter)
{
  g_return_if_fail (CC_IS_KEYBOARD_OPTION (self));

  if (self->is_xkb)
    {
      gchar *new_value = NULL;

      gtk_tree_model_get (GTK_TREE_MODEL (self->store), iter,
                          XKB_OPTION_ID_COLUMN, &new_value,
                          -1);

      if (!new_value)
        {
          if (self->current_value)
            remove_value (self->current_value);
        }
      else
        {
          if (self->current_value)
            replace_value (self->current_value, new_value);
          else
            add_value (new_value);
        }

      g_settings_set_strv (input_sources_settings, XKB_OPTIONS_KEY,
                           (const gchar * const *) current_xkb_options);

      g_free (new_value);
    }
  else
    {
      gchar *value = NULL;
      gchar *description = NULL;

      gtk_tree_model_get (GTK_TREE_MODEL (self->store), iter,
                          XKB_OPTION_ID_COLUMN, &value,
                          XKB_OPTION_DESCRIPTION_COLUMN, &description,
                          -1);

      g_free (self->current_value);
      g_free (self->current_description);
      self->current_value = value;
      self->current_description = description;

      g_settings_set_string (input_switcher_settings, INPUT_SWITCHER_KEY, self->current_value);
    }
}

void
cc_keyboard_option_clear_all (void)
{
  GList *l;

  for (l = objects_list; l; l = l->next)
    g_object_unref (l->data);

  g_clear_pointer (&objects_list, g_list_free);
  g_clear_pointer (&current_xkb_options, g_strfreev);
  g_clear_object (&input_sources_settings);
  g_clear_object (&input_switcher_settings);
  g_clear_object (&xkb_info);
}
