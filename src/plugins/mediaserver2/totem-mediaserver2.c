/*
 * Copyright (C) 2010 Igalia S.L.
 *
 * Authors: Juan A. Suarez Romero <jasuarez@oigalia.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 * The Totem project hereby grant permission for non-GPL compatible GStreamer
 * plugins to be used and distributed together with GStreamer and Totem. This
 * permission are above and beyond the permissions granted by the GPL license
 * Totem is covered by.
 *
 * See license_change file for details.
 */

#include <config.h>
#include <glib/gi18n-lib.h>
#include <libpeas/peas-extension-base.h>
#include <libpeas/peas-object-module.h>
#include <libpeas/peas-activatable.h>
#include <media-server2-client.h>
#include <media-server2-observer.h>

#include "totem-plugin.h"
#include "totem-interface.h"
#include "totem-private.h"
#include "totem.h"

#define TOTEM_TYPE_MEDIA_SERVER2_PLUGIN         \
  (totem_media_server2_plugin_get_type ())
#define TOTEM_MEDIA_SERVER2_PLUGIN(o)                                   \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), TOTEM_TYPE_MEDIA_SERVER2_PLUGIN, TotemMediaServer2Plugin))
#define TOTEM_MEDIA_SERVER2_PLUGIN_CLASS(k)                             \
  (G_TYPE_CHECK_CLASS_CAST((k), TOTEM_TYPE_MEDIA_SERVER2_PLUGIN, TotemMediaServer2PluginClass))
#define TOTEM_IS_MEDIA_SERVER2_PLUGIN(o)                                \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), TOTEM_TYPE_MEDIA_SERVER2_PLUGIN))
#define TOTEM_IS_MEDIA_SERVER2_PLUGIN_CLASS(k)                          \
  (G_TYPE_CHECK_CLASS_TYPE ((k), TOTEM_TYPE_MEDIA_SERVER2_PLUGIN))
#define TOTEM_MEDIA_SERVER2_PLUGIN_GET_CLASS(o)                         \
  (G_TYPE_INSTANCE_GET_CLASS ((o), TOTEM_TYPE_MEDIA_SERVER2_PLUGIN, TotemMediaServer2PluginClass))

#define PAGESIZE    25
#define MAX_DEFAULT 300

typedef struct {
  Totem *totem;
  GtkTreeModel *browser_model;
  GtkWidget *browser;
  gulong new_cb_handler;
} TotemMediaServer2PluginPrivate;

TOTEM_PLUGIN_REGISTER (TOTEM_TYPE_MEDIA_SERVER2_PLUGIN, TotemMediaServer2Plugin, totem_media_server2_plugin)

typedef struct {
  gboolean canceled;
  MS2Client *provider;
  TotemMediaServer2PluginPrivate *priv;
  gchar *object_path;
  gchar *tree_path;
  guint offset;
  GtkTreeRowReference *parent_ref;
  gulong cancel_handler;
} BrowseData;

enum {
  MODEL_PROVIDER = 0,
  MODEL_TITLE,
  MODEL_PATH,
  MODEL_TYPE,
  MODEL_URL,
  MODEL_ICON,
};

static gchar *properties[] = { MS2_PROP_DISPLAY_NAME,
                               MS2_PROP_PATH,
                               MS2_PROP_TYPE,
                               MS2_PROP_URLS,
                               NULL };

static guint max_items = MAX_DEFAULT;

static GdkPixbuf *
load_icon (MS2ItemType type)
{
  GdkScreen *screen;
  GtkIconTheme *theme;
  GError *error = NULL;
  gint i;
  enum { ICON_DIRECTORY = 0,
         ICON_AUDIO,
         ICON_VIDEO,
         ICON_IMAGE,
         ICON_DEFAULT };
  const gchar *icon_name[] = { GTK_STOCK_DIRECTORY,
                               "gnome-mime-audio",
                               "gnome-mime-video",
                               "gnome-mime-image",
                               GTK_STOCK_FILE };
  static GdkPixbuf *pixbuf[5] = { NULL };

  switch (type) {
  case MS2_ITEM_TYPE_CONTAINER:
    i = ICON_DIRECTORY;
    break;
  case MS2_ITEM_TYPE_VIDEO:
  case MS2_ITEM_TYPE_MOVIE:
    i = ICON_VIDEO;
    break;
  case MS2_ITEM_TYPE_AUDIO:
  case MS2_ITEM_TYPE_MUSIC:
    i = ICON_AUDIO;
    break;
  case MS2_ITEM_TYPE_IMAGE:
  case MS2_ITEM_TYPE_PHOTO:
    i = ICON_IMAGE;
    break;
  default:
    i = ICON_DEFAULT;
    break;
  }

  if (!pixbuf[i]) {
    screen = gdk_screen_get_default ();
    theme = gtk_icon_theme_get_for_screen (screen);
    pixbuf[i] = gtk_icon_theme_load_icon (theme, icon_name[i], 22, 22, &error);

    if (pixbuf == NULL) {
      g_warning ("Failed to load icon %s: %s", icon_name[i],  error->message);
      g_error_free (error);
    }
  }

  return pixbuf[i];
}

static gboolean
remove_provider_from_model (GtkTreeModel *model,
                            GtkTreePath *path,
                            GtkTreeIter *iter,
                            gpointer user_data)
{
  MS2Client *provider;

  gtk_tree_model_get (model, iter, MODEL_PROVIDER, &provider, -1);
  if (provider == user_data) {
    gtk_tree_store_remove (GTK_TREE_STORE (model), iter);
    g_object_unref (provider);
    return TRUE;
  }

  return FALSE;
}

static void
provider_removed_cb (MS2Client *provider,
                     gpointer user_data)
{
  TotemMediaServer2PluginPrivate *priv = (TotemMediaServer2PluginPrivate *) user_data;

  gtk_tree_model_foreach (priv->browser_model, remove_provider_from_model, provider);
}

static void
get_properties_reply (GObject *source,
                      GAsyncResult *result,
                      gpointer user_data)
{
  GdkPixbuf *icon;
  GHashTable *properties;
  GtkTreeIter iter;
  MS2Client *provider = MS2_CLIENT (source);
  MS2ItemType type;
  TotemMediaServer2PluginPrivate *priv = (TotemMediaServer2PluginPrivate *) user_data;
  const gchar *title;

  properties = ms2_client_get_properties_finish (provider, result, NULL);

  if (properties) {
    title = ms2_client_get_display_name (properties);
    if (!title || title[0] == '\0') {
      title = ms2_client_get_provider_name (provider);
    }

    type =  ms2_client_get_item_type (properties);
    icon = load_icon (type);

    gtk_tree_store_append (GTK_TREE_STORE (priv->browser_model), &iter, NULL);
    gtk_tree_store_set (GTK_TREE_STORE (priv->browser_model),
                        &iter,
                        MODEL_PROVIDER, provider,
                        MODEL_TITLE, title,
                        MODEL_PATH, ms2_client_get_path (properties),
                        MODEL_TYPE, type,
                        MODEL_ICON, icon,
                        -1);

    g_signal_connect (provider,
                      "destroy",
                      (GCallback) provider_removed_cb,
                      priv);

    g_hash_table_unref (properties);
  }

  g_object_unref (source);
}

static void
provider_added_cb (MS2Observer *observer,
                   const gchar *name,
                   gpointer user_data)
{
  MS2Client *provider;
  TotemMediaServer2PluginPrivate *priv = (TotemMediaServer2PluginPrivate *) user_data;

  provider = ms2_client_new (name);
  ms2_client_get_properties_async (provider,
                                   ms2_client_get_root_path (provider),
                                   properties,
                                   get_properties_reply,
                                   priv);
}

static void
load_providers (TotemMediaServer2PluginPrivate *priv)
{
  MS2Observer *observer;
  gchar **providers;
  gchar **provider;

  observer = ms2_observer_get_instance ();
  g_signal_connect (observer, "new", G_CALLBACK (provider_added_cb), priv);

  /* Load running providers */
  providers = ms2_client_get_providers ();
  if (providers) {
    for (provider = providers; *provider; provider++) {
      provider_added_cb (observer, *provider, priv);
    }
  }
}

static void
cancel_browse (MS2Client *provider,
               gpointer user_data)
{
  BrowseData *data = (BrowseData *) user_data;

  data->canceled = TRUE;
}

static void
list_children_reply (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
  BrowseData *data = (BrowseData *) user_data;
  GList *child;
  GList *children;
  GdkPixbuf *icon;
  GtkTreeIter iter, parent_iter;
  MS2Client *provider = MS2_CLIENT (source);
  MS2ItemType type;
  gchar **urls;
  GtkTreePath *path, *parent_path = NULL;
  gchar *url;
  gint remaining = PAGESIZE;

  if (!data->canceled) {
    children = ms2_client_list_children_finish (provider, result, NULL);
    for (child = children; child && data->offset < max_items; child = g_list_next (child)) {
      urls = ms2_client_get_urls (child->data);
      if (urls && urls[0]) {
        url = urls[0];
      } else {
        url = NULL;
      }

      type = ms2_client_get_item_type (child->data);
      icon = load_icon (type);

      parent_path = gtk_tree_row_reference_get_path (data->parent_ref);
      if (!parent_path) {
        data->canceled = TRUE;
        goto free_data;
      }

      gtk_tree_model_get_iter (data->priv->browser_model, &parent_iter, parent_path);
      gtk_tree_store_append (GTK_TREE_STORE (data->priv->browser_model),
                             &iter,
                             &parent_iter);
      gtk_tree_store_set (GTK_TREE_STORE (data->priv->browser_model),
                          &iter,
                          MODEL_PROVIDER, data->provider,
                          MODEL_TITLE, ms2_client_get_display_name (child->data),
                          MODEL_PATH, ms2_client_get_path (child->data),
                          MODEL_TYPE, type,
                          MODEL_URL, url,
                          MODEL_ICON, icon,
                          -1);

      /* Expand only first time*/
      if (data->offset == 0) {
        path = gtk_tree_path_new_from_string (data->tree_path);
        gtk_tree_view_expand_row (GTK_TREE_VIEW (data->priv->browser),
                                  path,
                                  FALSE);
        gtk_tree_path_free (path);
      }

      remaining--;
      data->offset++;
    }

  free_data:
    /* Free data */
    if (parent_path) {
      gtk_tree_path_free (parent_path);
    }
    g_list_foreach (children, (GFunc) g_hash_table_unref, NULL);
    g_list_free (children);
  }

  /* Check if it was canceled, there is no more elements or we reached
     maximum */
  if (data->canceled || remaining > 0 || data->offset >= max_items) {
    g_signal_handler_disconnect (data->provider, data->cancel_handler);
    g_object_unref (data->provider);
    g_free (data->object_path);
    g_free (data->tree_path);
    gtk_tree_row_reference_free (data->parent_ref);
    g_slice_free (BrowseData, data);
  } else {
    /* Continue browsing */
    ms2_client_list_children_async (data->provider,
                                    data->object_path,
                                    data->offset,
                                    PAGESIZE,
                                    properties,
                                    list_children_reply,
                                    data);
  }
}

static void
browse_cb (GtkTreeView *tree_view,
           GtkTreePath *path,
           GtkTreeViewColumn *column,
           gpointer user_data)
{
  BrowseData *data;
  GtkTreeIter iter;
  GtkTreeModel *model;
  MS2Client *provider;
  MS2ItemType type;
  TotemMediaServer2PluginPrivate *priv = (TotemMediaServer2PluginPrivate *) user_data;
  gchar *object_path;
  gchar *title;
  gchar *url;

  model = gtk_tree_view_get_model (tree_view);
  gtk_tree_model_get_iter (model, &iter, path);

  gtk_tree_model_get (model, &iter,
                      MODEL_PROVIDER, &provider,
                      MODEL_PATH, &object_path,
                      MODEL_TYPE, &type,
                      MODEL_URL, &url,
                      MODEL_TITLE, &title,
                      -1);

  if (type == MS2_ITEM_TYPE_CONTAINER) {
    if (gtk_tree_model_iter_has_child (model, &iter)) {
      gtk_tree_view_expand_row (tree_view, path, FALSE);
    } else {
      data = g_slice_new (BrowseData);
      data->canceled = FALSE;
      data->offset = 0;
      data->parent_ref = gtk_tree_row_reference_new (model, path);
      data->object_path = g_strdup (object_path);
      data->tree_path = gtk_tree_path_to_string (path);
      data->priv = priv;
      data->provider = g_object_ref (provider);
      data->cancel_handler = g_signal_connect (provider,
                                               "destroy",
                                               G_CALLBACK (cancel_browse),
                                               data);

      ms2_client_list_children_async (data->provider,
                                      data->object_path,
                                      data->offset,
                                      PAGESIZE,
                                      properties,
                                      list_children_reply,
                                      data);
    }
  } else {
    if (url) {
      totem_add_to_playlist_and_play (priv->totem, url, title, TRUE);
    }
  }
}

static void
setup_ui (TotemMediaServer2PluginPrivate *priv)
{
  GtkWidget *box;
  GtkWidget *scroll;
  GtkCellRenderer *render_text;
  GtkCellRenderer *render_pixbuf;
  GtkTreeViewColumn *col;

  box = gtk_vbox_new (FALSE, 5);
  scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  priv->browser = gtk_tree_view_new ();
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (priv->browser), FALSE);
  col = gtk_tree_view_column_new ();
  render_pixbuf = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (col, render_pixbuf, FALSE);
  gtk_tree_view_column_add_attribute (col, render_pixbuf, "pixbuf", MODEL_ICON);
  render_text = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (col, render_text, FALSE);
  gtk_tree_view_column_add_attribute (col, render_text, "text", MODEL_TITLE);
  gtk_tree_view_insert_column (GTK_TREE_VIEW (priv->browser), col, -1);
  gtk_container_add (GTK_CONTAINER (scroll), priv->browser);
  gtk_container_add (GTK_CONTAINER (box), scroll);

  priv->browser_model =
    GTK_TREE_MODEL (gtk_tree_store_new (6,
                                        G_TYPE_OBJECT,     /* Provider */
                                        G_TYPE_STRING,     /* Name */
                                        G_TYPE_STRING,     /* Path */
                                        G_TYPE_INT,        /* Type */
                                        G_TYPE_STRING,     /* URL */
                                        GDK_TYPE_PIXBUF)); /* Icon */
  gtk_tree_view_set_model (GTK_TREE_VIEW (priv->browser), priv->browser_model);

  g_signal_connect (priv->browser,
                    "row-activated",
                    G_CALLBACK (browse_cb),
                    priv);

  gtk_widget_show_all (box);
  totem_add_sidebar_page (priv->totem, "mediaserver2", "MediaServer2", box);
}

static void
impl_activate (PeasActivatable *plugin)
{
  TotemMediaServer2Plugin *self = TOTEM_MEDIA_SERVER2_PLUGIN (plugin);
  TotemMediaServer2PluginPrivate *priv = self->priv;
  priv->totem = g_object_ref (g_object_get_data (G_OBJECT (plugin), "object"));
  setup_ui (priv);
  load_providers(priv);
}

static void
impl_deactivate (PeasActivatable *plugin)
{
  MS2Observer *observer;
  TotemMediaServer2Plugin *self = TOTEM_MEDIA_SERVER2_PLUGIN (plugin);
  TotemMediaServer2PluginPrivate *priv = self->priv;

  observer = ms2_observer_get_instance ();
  g_signal_handlers_disconnect_by_func (observer, provider_added_cb, priv);
  totem_remove_sidebar_page (priv->totem, "mediaserver2");
  g_object_unref (priv->totem);
}

