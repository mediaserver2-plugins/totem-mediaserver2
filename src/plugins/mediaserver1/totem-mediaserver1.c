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
#include <media-server1-client.h>
#include <media-server1-observer.h>

#include "totem-plugin.h"
#include "totem-interface.h"
#include "totem-private.h"
#include "totem.h"

#define TOTEM_TYPE_MEDIA_SERVER1_PLUGIN         \
  (totem_media_server1_plugin_get_type ())
#define TOTEM_MEDIA_SERVER1_PLUGIN(o)                                   \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), TOTEM_TYPE_MEDIA_SERVER1_PLUGIN, TotemMediaServer1Plugin))
#define TOTEM_MEDIA_SERVER1_PLUGIN_CLASS(k)                             \
  (G_TYPE_CHECK_CLASS_CAST((k), TOTEM_TYPE_MEDIA_SERVER1_PLUGIN, TotemMediaServer1PluginClass))
#define TOTEM_IS_MEDIA_SERVER1_PLUGIN(o)                                \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), TOTEM_TYPE_MEDIA_SERVER1_PLUGIN))
#define TOTEM_IS_MEDIA_SERVER1_PLUGIN_CLASS(k)                          \
  (G_TYPE_CHECK_CLASS_TYPE ((k), TOTEM_TYPE_MEDIA_SERVER1_PLUGIN))
#define TOTEM_MEDIA_SERVER1_PLUGIN_GET_CLASS(o)                         \
  (G_TYPE_INSTANCE_GET_CLASS ((o), TOTEM_TYPE_MEDIA_SERVER1_PLUGIN, TotemMediaServer1PluginClass))

typedef struct {
  TotemPlugin parent;
  Totem *totem;
  GtkTreeModel *browser_model;
  GtkWidget *browser;
} TotemMediaServer1Plugin;

typedef struct {
  TotemPluginClass parent_class;
} TotemMediaServer1PluginClass;

enum {
  MODEL_PROVIDER = 0,
  MODEL_TITLE,
  MODEL_PATH,
  MODEL_TYPE,
  MODEL_URL,
};

static gchar *properties[] = { MS1_PROP_DISPLAY_NAME,
                               MS1_PROP_PATH,
                               MS1_PROP_TYPE,
                               MS1_PROP_URLS,
                               NULL };

G_MODULE_EXPORT GType register_totem_plugin (GTypeModule *module);
GType totem_media_server1_plugin_get_type (void);

static gboolean impl_activate (TotemPlugin *plugin, TotemObject *totem, GError **error);

TOTEM_PLUGIN_REGISTER (TotemMediaServer1Plugin, totem_media_server1_plugin)

static void
totem_media_server1_plugin_class_init (TotemMediaServer1PluginClass *klass)
{
  TotemPluginClass *plugin_class = TOTEM_PLUGIN_CLASS (klass);
  plugin_class->activate = impl_activate;
}

static void
totem_media_server1_plugin_init (TotemMediaServer1Plugin *plugin)
{
}

static gboolean
remove_provider_from_model (GtkTreeModel *model,
                            GtkTreePath *path,
                            GtkTreeIter *iter,
                            gpointer user_data)
{
  MS1Client *provider;

  gtk_tree_model_get (model, iter, MODEL_PROVIDER, &provider, -1);
  if (provider == user_data) {
    gtk_tree_store_remove (GTK_TREE_STORE (model), iter);
    return TRUE;
  }

  return FALSE;
}

static void
provider_removed_cb (MS1Client *provider,
                     gpointer user_data)
{
  TotemMediaServer1Plugin *self = TOTEM_MEDIA_SERVER1_PLUGIN (user_data);

  gtk_tree_model_foreach (self->browser_model, remove_provider_from_model, provider);
}

static void
provider_added_cb (MS1Observer *observer,
                   const gchar *name,
                   gpointer user_data)
{
  GHashTable *result;
  GtkTreeIter iter;
  MS1Client *provider;
  TotemMediaServer1Plugin *self = TOTEM_MEDIA_SERVER1_PLUGIN (user_data);
  const gchar *title;

  provider = ms1_client_new (name);
  result = ms1_client_get_properties (provider,
                                      ms1_client_get_root_path (provider),
                                      properties,
                                      NULL);

  if (result) {
    title = ms1_client_get_display_name (result);
    if (!title || title[0] == '\0') {
      title = name;
    }

    gtk_tree_store_append (GTK_TREE_STORE (self->browser_model), &iter, NULL);
    gtk_tree_store_set (GTK_TREE_STORE (self->browser_model),
                        &iter,
                        MODEL_PROVIDER, provider,
                        MODEL_TITLE, title,
                        MODEL_PATH, ms1_client_get_path (result),
                        MODEL_TYPE, ms1_client_get_item_type (result),
                        -1);

    g_signal_connect (provider,
                      "destroy",
                      (GCallback) provider_removed_cb,
                      self);

    g_hash_table_unref (result);
  }

  g_object_unref (provider);
}

static void
load_providers (TotemMediaServer1Plugin *self)
{
  MS1Observer *observer;
  gchar **providers;
  gchar **provider;

  observer = ms1_observer_get_instance ();
  g_signal_connect (observer, "new", G_CALLBACK (provider_added_cb), self);

  /* Load running providers */
  providers = ms1_client_get_providers ();
  if (providers) {
    for (provider = providers; *provider; provider++) {
      provider_added_cb (observer, *provider, self);
    }
  }
}

static void
browse_cb (GtkTreeView *tree_view,
           GtkTreePath *path,
           GtkTreeViewColumn *column,
           gpointer user_data)
{
  GList *child;
  GList *children;
  GtkTreeIter iter;
  GtkTreeIter iter_child;
  GtkTreeModel *model;
  MS1Client *provider;
  MS1ItemType type;
  TotemMediaServer1Plugin *self = TOTEM_MEDIA_SERVER1_PLUGIN (user_data);
  gchar **urls;
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

  if (type == MS1_ITEM_TYPE_CONTAINER) {
    children =
      ms1_client_list_children (provider, object_path, 0, 10, properties, NULL);

    for (child = children; child; child = g_list_next (child)) {
      urls = ms1_client_get_urls (child->data);
      if (urls && urls[0]) {
        url = urls[0];
      } else {
        url = NULL;
      }
      gtk_tree_store_append (GTK_TREE_STORE (self->browser_model),
                             &iter_child,
                             &iter);
      gtk_tree_store_set (GTK_TREE_STORE (self->browser_model),
                          &iter_child,
                          MODEL_PROVIDER, provider,
                          MODEL_TITLE, ms1_client_get_display_name (child->data),
                          MODEL_PATH, ms1_client_get_path (child->data),
                          MODEL_TYPE, ms1_client_get_item_type (child->data),
                          MODEL_URL, url,
                          -1);
    }

    /* Free data */
    g_list_foreach (children, (GFunc) g_hash_table_unref, NULL);
    g_list_free (children);

    gtk_tree_view_expand_row (GTK_TREE_VIEW (self->browser), path, FALSE);
  } else {
    totem_add_to_playlist_and_play (self->totem, url, title, TRUE);
  }
}

static void
setup_ui (TotemMediaServer1Plugin *self)
{
  GtkWidget *box;
  GtkWidget *scroll;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *col;

  box = gtk_vbox_new (FALSE, 5);
  scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  self->browser = gtk_tree_view_new ();
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (self->browser), FALSE);
  col = gtk_tree_view_column_new ();
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (col, renderer, FALSE);
  gtk_tree_view_column_add_attribute (col,
                                      renderer,
                                      "text",
                                      MODEL_TITLE);
  gtk_tree_view_insert_column (GTK_TREE_VIEW (self->browser), col, -1);
  gtk_container_add (GTK_CONTAINER (scroll), self->browser);
  gtk_container_add (GTK_CONTAINER (box), scroll);

  self->browser_model =
    GTK_TREE_MODEL (gtk_tree_store_new (5,
                                        G_TYPE_OBJECT,   /* Provider */
                                        G_TYPE_STRING,   /* Name */
                                        G_TYPE_STRING,   /* Path */
                                        G_TYPE_INT,      /* Type */
                                        G_TYPE_STRING)); /* URL */
  gtk_tree_view_set_model (GTK_TREE_VIEW (self->browser), self->browser_model);

  g_signal_connect (self->browser,
                    "row-activated",
                    G_CALLBACK (browse_cb),
                    self);

  gtk_widget_show_all (box);
  totem_add_sidebar_page (self->totem, "mediaserver1", "MediaServer1", box);
}

static gboolean
impl_activate (TotemPlugin *plugin,
               TotemObject *totem,
               GError **error)
{
  TotemMediaServer1Plugin *self = TOTEM_MEDIA_SERVER1_PLUGIN (plugin);

  self->totem = g_object_ref (totem);
  setup_ui (self);
  load_providers(self);
  return TRUE;
}
