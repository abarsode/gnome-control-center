/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010-2011 Red Hat, Inc.
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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gi18n-lib.h>

#include <gconf/gconf-client.h>

#include <libgnome-desktop/gnome-bg.h>
#include <gsettings-desktop-schemas/gdesktop-enums.h>

#include "cc-background-item.h"
#include "gdesktop-enums-types.h"

#define CC_BACKGROUND_ITEM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CC_TYPE_BACKGROUND_ITEM, CcBackgroundItemPrivate))

struct CcBackgroundItemPrivate
{
        /* properties */
        char            *name;
        char            *filename;
        char            *size;
        GDesktopBackgroundStyle placement;
        GDesktopBackgroundShading shading;
        char            *primary_color;
        char            *secondary_color;
        char            *source_url; /* Used by the Flickr source */
        char            *source_xml; /* Used by the Wallpapers source */
        gboolean         is_deleted;
        CcBackgroundItemFlags flags;

        /* internal */
        GnomeBG         *bg;
        char            *mime_type;
        int              width;
        int              height;
};

enum {
        PROP_0,
        PROP_NAME,
        PROP_FILENAME,
        PROP_PLACEMENT,
        PROP_SHADING,
        PROP_PRIMARY_COLOR,
        PROP_SECONDARY_COLOR,
        PROP_IS_DELETED,
        PROP_SOURCE_URL,
        PROP_SOURCE_XML,
        PROP_FLAGS,
        PROP_SIZE
};

enum {
        CHANGED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     cc_background_item_class_init     (CcBackgroundItemClass *klass);
static void     cc_background_item_init           (CcBackgroundItem      *background_item);
static void     cc_background_item_finalize       (GObject               *object);

G_DEFINE_TYPE (CcBackgroundItem, cc_background_item, G_TYPE_OBJECT)

static GEmblem *
get_slideshow_icon (void)
{
	GIcon *themed;
	GEmblem *emblem;
	themed = g_themed_icon_new ("slideshow-emblem");
	emblem = g_emblem_new_with_origin (themed, G_EMBLEM_ORIGIN_DEVICE);
	g_object_unref (themed);
	return emblem;
}

static void
set_bg_properties (CcBackgroundItem *item)
{
        GdkColor pcolor = { 0, 0, 0, 0 };
        GdkColor scolor = { 0, 0, 0, 0 };

        if (item->priv->filename)
                gnome_bg_set_filename (item->priv->bg, item->priv->filename);

        if (item->priv->primary_color != NULL) {
                gdk_color_parse (item->priv->primary_color, &pcolor);
        }
        if (item->priv->secondary_color != NULL) {
                gdk_color_parse (item->priv->secondary_color, &scolor);
        }

        gnome_bg_set_color (item->priv->bg, item->priv->shading, &pcolor, &scolor);
        gnome_bg_set_placement (item->priv->bg, item->priv->placement);
}


gboolean
cc_background_item_changes_with_time (CcBackgroundItem *item)
{
        gboolean changes;

	g_return_val_if_fail (CC_IS_BACKGROUND_ITEM (item), FALSE);

        changes = FALSE;
        if (item->priv->bg != NULL) {
                changes = gnome_bg_changes_with_time (item->priv->bg);
        }
        return changes;
}

static void
update_size (CcBackgroundItem *item)
{
	g_free (item->priv->size);
	item->priv->size = NULL;

	if (item->priv->filename == NULL || g_str_equal (item->priv->filename, "(none)")) {
		item->priv->size = g_strdup ("");
	} else {
		if (gnome_bg_has_multiple_sizes (item->priv->bg) || gnome_bg_changes_with_time (item->priv->bg)) {
			item->priv->size = g_strdup (_("multiple sizes"));
		} else {
			/* translators: 100 × 100px
			 * Note that this is not an "x", but U+00D7 MULTIPLICATION SIGN */
			item->priv->size = g_strdup_printf (_("%d \303\227 %d"),
							    item->priv->width,
							    item->priv->height);
		}
	}
}

GIcon *
cc_background_item_get_frame_thumbnail (CcBackgroundItem             *item,
                                        GnomeDesktopThumbnailFactory *thumbs,
                                        int                           width,
                                        int                           height,
                                        int                           frame)
{
        GdkPixbuf *pixbuf = NULL;
        GIcon *icon = NULL;

	g_return_val_if_fail (CC_IS_BACKGROUND_ITEM (item), NULL);
	g_return_val_if_fail (width > 0 && height > 0, NULL);

        set_bg_properties (item);

        if (frame >= 0)
                pixbuf = gnome_bg_create_frame_thumbnail (item->priv->bg,
                                                          thumbs,
                                                          gdk_screen_get_default (),
                                                          width,
                                                          height,
                                                          frame);
        else
                pixbuf = gnome_bg_create_thumbnail (item->priv->bg,
                                                    thumbs,
                                                    gdk_screen_get_default(),
                                                    width,
                                                    height);

        if (pixbuf != NULL
            && frame != -2
            && gnome_bg_changes_with_time (item->priv->bg)) {
                GEmblem *emblem;

                emblem = get_slideshow_icon ();
                icon = g_emblemed_icon_new (G_ICON (pixbuf), emblem);
                g_object_unref (emblem);
                g_object_unref (pixbuf);
        } else {
                icon = G_ICON (pixbuf);
	}

        gnome_bg_get_image_size (item->priv->bg,
                                 thumbs,
                                 width,
                                 height,
                                 &item->priv->width,
                                 &item->priv->height);

        update_size (item);

        return icon;
}


GIcon *
cc_background_item_get_thumbnail (CcBackgroundItem             *item,
                                  GnomeDesktopThumbnailFactory *thumbs,
                                  int                           width,
                                  int                           height)
{
        return cc_background_item_get_frame_thumbnail (item, thumbs, width, height, -1);
}

static void
update_info (CcBackgroundItem *item,
	     GFileInfo        *_info)
{
        GFile     *file;
        GFileInfo *info;

	if (_info == NULL) {
		file = g_file_new_for_commandline_arg (item->priv->filename);

		info = g_file_query_info (file,
					  G_FILE_ATTRIBUTE_STANDARD_NAME ","
					  G_FILE_ATTRIBUTE_STANDARD_SIZE ","
					  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
					  G_FILE_ATTRIBUTE_TIME_MODIFIED,
					  G_FILE_QUERY_INFO_NONE,
					  NULL,
					  NULL);
		g_object_unref (file);
	} else {
		info = g_object_ref (_info);
	}

        g_free (item->priv->mime_type);
        item->priv->mime_type = NULL;

        if (info == NULL
            || g_file_info_get_content_type (info) == NULL) {
                if (item->priv->filename == NULL) {
                        item->priv->mime_type = g_strdup ("image/x-no-data");
                        g_free (item->priv->name);
                        item->priv->name = g_strdup (_("No Desktop Background"));
                        //item->priv->size = 0;
                }
        } else {
                if (item->priv->name == NULL) {
                        const char *name;

                        g_free (item->priv->name);

                        name = g_file_info_get_name (info);
                        if (g_utf8_validate (name, -1, NULL))
                                item->priv->name = g_strdup (name);
                        else
                                item->priv->name = g_filename_to_utf8 (name,
                                                                       -1,
                                                                       NULL,
                                                                       NULL,
                                                                       NULL);
                }

                item->priv->mime_type = g_strdup (g_file_info_get_content_type (info));

#if 0
                item->priv->size = g_file_info_get_size (info);
                item->priv->mtime = g_file_info_get_attribute_uint64 (info,
                                                                      G_FILE_ATTRIBUTE_TIME_MODIFIED);
#endif
        }

        if (info != NULL)
                g_object_unref (info);

}

static void
on_bg_changed (GnomeBG          *bg,
               CcBackgroundItem *item)
{
        g_signal_emit (item, signals[CHANGED], 0);
}

gboolean
cc_background_item_load (CcBackgroundItem *item,
			 GFileInfo        *info)
{
        g_return_val_if_fail (CC_IS_BACKGROUND_ITEM (item), FALSE);
        g_return_val_if_fail (item->priv->filename != NULL, FALSE);

        update_info (item, info);

        if (item->priv->mime_type != NULL
            && (g_str_has_prefix (item->priv->mime_type, "image/")
                || strcmp (item->priv->mime_type, "application/xml") == 0)) {
                set_bg_properties (item);
        } else {
		return FALSE;
        }

	/* FIXME we should handle XML files as well */
        if (item->priv->mime_type != NULL &&
            g_str_has_prefix (item->priv->mime_type, "image/")) {
		gdk_pixbuf_get_file_info (item->priv->filename,
					  &item->priv->width,
					  &item->priv->height);
		update_size (item);
	}

        return TRUE;
}

static void
_set_name (CcBackgroundItem *item,
           const char       *value)
{
        g_free (item->priv->name);
        item->priv->name = g_strdup (value);
}

const char *
cc_background_item_get_name (CcBackgroundItem *item)
{
	g_return_val_if_fail (CC_IS_BACKGROUND_ITEM (item), NULL);

	return item->priv->name;
}

static void
_set_filename (CcBackgroundItem *item,
               const char       *value)
{
        g_free (item->priv->filename);
        item->priv->filename = g_strdup (value);
}

const char *
cc_background_item_get_filename (CcBackgroundItem *item)
{
	g_return_val_if_fail (CC_IS_BACKGROUND_ITEM (item), NULL);

	return item->priv->filename;
}

static void
_set_placement (CcBackgroundItem        *item,
                GDesktopBackgroundStyle  value)
{
        item->priv->placement = value;
}

static void
_set_shading (CcBackgroundItem          *item,
              GDesktopBackgroundShading  value)
{
        item->priv->shading = value;
}

static void
_set_primary_color (CcBackgroundItem *item,
                    const char       *value)
{
        g_free (item->priv->primary_color);
        item->priv->primary_color = g_strdup (value);
}

const char *
cc_background_item_get_pcolor (CcBackgroundItem *item)
{
	g_return_val_if_fail (CC_IS_BACKGROUND_ITEM (item), NULL);

	return item->priv->primary_color;
}

static void
_set_secondary_color (CcBackgroundItem *item,
                      const char       *value)
{
        g_free (item->priv->secondary_color);
        item->priv->secondary_color = g_strdup (value);
}

const char *
cc_background_item_get_scolor (CcBackgroundItem *item)
{
	g_return_val_if_fail (CC_IS_BACKGROUND_ITEM (item), NULL);

	return item->priv->secondary_color;
}

GDesktopBackgroundStyle
cc_background_item_get_placement (CcBackgroundItem *item)
{
	g_return_val_if_fail (CC_IS_BACKGROUND_ITEM (item), G_DESKTOP_BACKGROUND_STYLE_SCALED);

	return item->priv->placement;
}

GDesktopBackgroundShading
cc_background_item_get_shading (CcBackgroundItem *item)
{
	g_return_val_if_fail (CC_IS_BACKGROUND_ITEM (item), G_DESKTOP_BACKGROUND_SHADING_SOLID);

	return item->priv->shading;
}

static void
_set_is_deleted (CcBackgroundItem *item,
                 gboolean          value)
{
        item->priv->is_deleted = value;
}

static void
_set_source_url (CcBackgroundItem *item,
                 const char       *value)
{
        g_free (item->priv->source_url);
        item->priv->source_url = g_strdup (value);
}

const char *
cc_background_item_get_source_url (CcBackgroundItem *item)
{
	g_return_val_if_fail (CC_IS_BACKGROUND_ITEM (item), NULL);

	return item->priv->source_url;
}

static void
_set_source_xml (CcBackgroundItem *item,
                 const char       *value)
{
        g_free (item->priv->source_xml);
        item->priv->source_xml = g_strdup (value);
}

const char *
cc_background_item_get_source_xml (CcBackgroundItem *item)
{
	g_return_val_if_fail (CC_IS_BACKGROUND_ITEM (item), NULL);

	return item->priv->source_xml;
}

static void
_set_flags (CcBackgroundItem      *item,
            CcBackgroundItemFlags  value)
{
	item->priv->flags = value;
}

CcBackgroundItemFlags
cc_background_item_get_flags (CcBackgroundItem *item)
{
	g_return_val_if_fail (CC_IS_BACKGROUND_ITEM (item), 0);

	return item->priv->flags;
}

const char *
cc_background_item_get_size (CcBackgroundItem *item)
{
	g_return_val_if_fail (CC_IS_BACKGROUND_ITEM (item), NULL);

	return item->priv->size;
}

static void
cc_background_item_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
        CcBackgroundItem *self;

        self = CC_BACKGROUND_ITEM (object);

        switch (prop_id) {
        case PROP_NAME:
                _set_name (self, g_value_get_string (value));
                break;
        case PROP_FILENAME:
                _set_filename (self, g_value_get_string (value));
                break;
        case PROP_PLACEMENT:
                _set_placement (self, g_value_get_enum (value));
                break;
        case PROP_SHADING:
                _set_shading (self, g_value_get_enum (value));
                break;
        case PROP_PRIMARY_COLOR:
                _set_primary_color (self, g_value_get_string (value));
                break;
        case PROP_SECONDARY_COLOR:
                _set_secondary_color (self, g_value_get_string (value));
                break;
        case PROP_IS_DELETED:
                _set_is_deleted (self, g_value_get_boolean (value));
                break;
	case PROP_SOURCE_URL:
		_set_source_url (self, g_value_get_string (value));
		break;
	case PROP_SOURCE_XML:
		_set_source_xml (self, g_value_get_string (value));
		break;
	case PROP_FLAGS:
		_set_flags (self, g_value_get_flags (value));
		break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
cc_background_item_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
        CcBackgroundItem *self;

        self = CC_BACKGROUND_ITEM (object);

        switch (prop_id) {
        case PROP_NAME:
                g_value_set_string (value, self->priv->name);
                break;
        case PROP_FILENAME:
                g_value_set_string (value, self->priv->filename);
                break;
        case PROP_PLACEMENT:
                g_value_set_enum (value, self->priv->placement);
                break;
        case PROP_SHADING:
                g_value_set_enum (value, self->priv->shading);
                break;
        case PROP_PRIMARY_COLOR:
                g_value_set_string (value, self->priv->primary_color);
                break;
        case PROP_SECONDARY_COLOR:
                g_value_set_string (value, self->priv->secondary_color);
                break;
        case PROP_IS_DELETED:
                g_value_set_boolean (value, self->priv->is_deleted);
                break;
	case PROP_SOURCE_URL:
		g_value_set_string (value, self->priv->source_url);
		break;
	case PROP_SOURCE_XML:
		g_value_set_string (value, self->priv->source_xml);
		break;
	case PROP_FLAGS:
		g_value_set_flags (value, self->priv->flags);
		break;
	case PROP_SIZE:
		g_value_set_string (value, self->priv->size);
		break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
cc_background_item_constructor (GType                  type,
                                guint                  n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
        CcBackgroundItem      *background_item;

        background_item = CC_BACKGROUND_ITEM (G_OBJECT_CLASS (cc_background_item_parent_class)->constructor (type,
                                                                                                                         n_construct_properties,
                                                                                                                         construct_properties));

        return G_OBJECT (background_item);
}

static void
cc_background_item_class_init (CcBackgroundItemClass *klass)
{
        GObjectClass  *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = cc_background_item_get_property;
        object_class->set_property = cc_background_item_set_property;
        object_class->constructor = cc_background_item_constructor;
        object_class->finalize = cc_background_item_finalize;

        signals [CHANGED]
                = g_signal_new ("changed",
                                G_TYPE_FROM_CLASS (object_class),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL,
                                NULL,
                                g_cclosure_marshal_VOID__VOID,
                                G_TYPE_NONE,
                                0);

        g_object_class_install_property (object_class,
                                         PROP_NAME,
                                         g_param_spec_string ("name",
                                                              "name",
                                                              "name",
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_FILENAME,
                                         g_param_spec_string ("filename",
                                                              "filename",
                                                              "filename",
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_PLACEMENT,
					 g_param_spec_enum ("placement",
							    "placement",
							    "placement",
							    G_DESKTOP_TYPE_DESKTOP_BACKGROUND_STYLE,
							    G_DESKTOP_BACKGROUND_STYLE_SCALED,
							    G_PARAM_READWRITE));

        g_object_class_install_property (object_class,
                                         PROP_SHADING,
                                         g_param_spec_enum ("shading",
							    "shading",
							    "shading",
							    G_DESKTOP_TYPE_DESKTOP_BACKGROUND_SHADING,
							    G_DESKTOP_BACKGROUND_SHADING_SOLID,
							    G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_PRIMARY_COLOR,
                                         g_param_spec_string ("primary-color",
                                                              "primary-color",
                                                              "primary-color",
                                                              "#000000000000",
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_SECONDARY_COLOR,
                                         g_param_spec_string ("secondary-color",
                                                              "secondary-color",
                                                              "secondary-color",
                                                              "#000000000000",
                                                              G_PARAM_READWRITE));

        g_object_class_install_property (object_class,
                                         PROP_IS_DELETED,
                                         g_param_spec_boolean ("is-deleted",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));

        g_object_class_install_property (object_class,
                                         PROP_SOURCE_URL,
                                         g_param_spec_string ("source-url",
                                                              "source-url",
                                                              "source-url",
                                                              NULL,
                                                              G_PARAM_READWRITE));

        g_object_class_install_property (object_class,
                                         PROP_SOURCE_XML,
                                         g_param_spec_string ("source-xml",
                                                              "source-xml",
                                                              "source-xml",
                                                              NULL,
                                                              G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_FLAGS,
					 g_param_spec_flags ("flags",
							     "flags",
							     "flags",
							     G_DESKTOP_TYPE_BACKGROUND_ITEM_FLAGS,
							     0,
							     G_PARAM_READWRITE));

        g_object_class_install_property (object_class,
                                         PROP_SIZE,
                                         g_param_spec_string ("size",
                                                              "size",
                                                              "size",
                                                              NULL,
                                                              G_PARAM_READABLE));


        g_type_class_add_private (klass, sizeof (CcBackgroundItemPrivate));
}

static void
cc_background_item_init (CcBackgroundItem *item)
{
        item->priv = CC_BACKGROUND_ITEM_GET_PRIVATE (item);

        item->priv->bg = gnome_bg_new ();

        g_signal_connect (item->priv->bg,
                          "changed",
                          G_CALLBACK (on_bg_changed),
                          item);

        item->priv->shading = G_DESKTOP_BACKGROUND_SHADING_SOLID;
        item->priv->placement = G_DESKTOP_BACKGROUND_STYLE_SCALED;
        item->priv->primary_color = g_strdup ("#000000000000");
        item->priv->secondary_color = g_strdup ("#000000000000");
        item->priv->flags = 0;
}

static void
cc_background_item_finalize (GObject *object)
{
        CcBackgroundItem *item;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CC_IS_BACKGROUND_ITEM (object));

        item = CC_BACKGROUND_ITEM (object);

        g_return_if_fail (item->priv != NULL);

        g_free (item->priv->name);
        g_free (item->priv->filename);
        g_free (item->priv->primary_color);
        g_free (item->priv->secondary_color);
        g_free (item->priv->mime_type);
        g_free (item->priv->size);

        if (item->priv->bg != NULL)
                g_object_unref (item->priv->bg);

        G_OBJECT_CLASS (cc_background_item_parent_class)->finalize (object);
}

CcBackgroundItem *
cc_background_item_new (const char *filename)
{
        GObject *object;

        object = g_object_new (CC_TYPE_BACKGROUND_ITEM,
                               "filename", filename,
                               NULL);

        return CC_BACKGROUND_ITEM (object);
}

CcBackgroundItem *
cc_background_item_copy (CcBackgroundItem *item)
{
	CcBackgroundItem *ret;

	ret = cc_background_item_new (item->priv->filename);
	ret->priv->name = g_strdup (item->priv->name);
	ret->priv->filename = g_strdup (item->priv->filename);
	ret->priv->size = g_strdup (item->priv->size);
	ret->priv->placement = item->priv->placement;
	ret->priv->shading = item->priv->shading;
	ret->priv->primary_color = g_strdup (item->priv->primary_color);
	ret->priv->secondary_color = g_strdup (item->priv->secondary_color);
	ret->priv->source_url = g_strdup (item->priv->source_url);
	ret->priv->source_xml = g_strdup (item->priv->source_xml);
	ret->priv->is_deleted = item->priv->is_deleted;
	ret->priv->flags = item->priv->flags;

	return ret;
}

static const char *
flags_to_str (CcBackgroundItemFlags flag)
{
	GFlagsClass *fclass;
	GFlagsValue *value;

	fclass = G_FLAGS_CLASS (g_type_class_peek (G_DESKTOP_TYPE_BACKGROUND_ITEM_FLAGS));
	value = g_flags_get_first_value (fclass, flag);

	g_assert (value);

	return value->value_nick;
}

void
cc_background_item_dump (CcBackgroundItem *item)
{
	CcBackgroundItemPrivate *priv;
	GString *flags;
	int i;

	g_return_if_fail (CC_IS_BACKGROUND_ITEM (item));

	priv = item->priv;

	g_debug ("name:\t\t\t%s", priv->name);
	g_debug ("filename:\t\t%s", priv->filename ? priv->filename : "NULL");
	if (priv->size)
		g_debug ("size:\t\t\t'%s'", priv->size);
	flags = g_string_new (NULL);
	for (i = 0; i < 5; i++) {
		if (priv->flags & (1 << i)) {
			g_string_append (flags, flags_to_str (1 << i));
			g_string_append_c (flags, ' ');
		}
	}
	if (flags->len == 0)
		g_string_append (flags, "-none-");
	g_debug ("flags:\t\t\t%s", flags->str);
	g_string_free (flags, TRUE);
	if (priv->primary_color)
		g_debug ("pcolor:\t\t%s", priv->primary_color);
	if (priv->secondary_color)
		g_debug ("scolor:\t\t%s", priv->secondary_color);
	if (priv->source_url)
		g_debug ("source URL:\t\t%s", priv->source_url);
	if (priv->source_xml)
		g_debug ("source XML:\t\t%s", priv->source_xml);
	g_debug ("deleted:\t\t%s", priv->is_deleted ? "yes" : "no");
	if (priv->mime_type)
		g_debug ("mime-type:\t\t%s", priv->mime_type);
	g_debug ("dimensions:\t\t%d x %d", priv->width, priv->height);
	g_debug (" ");
}
