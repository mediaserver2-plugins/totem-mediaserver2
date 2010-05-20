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

#define PAGESIZE    25
#define MAX_DEFAULT 300

typedef struct {
  TotemPlugin parent;
  Totem *totem;
  GtkTreeModel *browser_model;
  GtkWidget *browser;
} TotemMediaServer1Plugin;

typedef struct {
  TotemPluginClass parent_class;
} TotemMediaServer1PluginClass;

typedef struct {
  gboolean canceled;
  MS1Client *provider;
  TotemMediaServer1Plugin *plugin;
  gchar *object_path;
  gchar *tree_path;
  guint offset;
  GtkTreeIter *parent_iter;
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

static gchar *properties[] = { MS1_PROP_DISPLAY_NAME,
                               MS1_PROP_PATH,
                               MS1_PROP_TYPE,
                               MS1_PROP_URLS,
                               NULL };

static guint max_items = MAX_DEFAULT;

G_MODULE_EXPORT GType register_totem_plugin (GTypeModule *module);
GType totem_media_server1_plugin_get_type (void);

static gboolean impl_activate (TotemPlugin *plugin, TotemObject *totem, GError **error);
static void impl_deactivate (TotemPlugin *plugin, TotemObject *totem);
static GtkWidget* configure_plugin (TotemPlugin *plugin);

TOTEM_PLUGIN_REGISTER (TotemMediaServer1Plugin, totem_media_server1_plugin)

static void
totem_media_server1_plugin_class_init (TotemMediaServer1PluginClass *klass)
{
  TotemPluginClass *plugin_class = TOTEM_PLUGIN_CLASS (klass);
  plugin_class->activate = impl_activate;
  plugin_class->deactivate = impl_deactivate;
  plugin_class->create_configure_dialog = configure_plugin;
}

static void
totem_media_server1_plugin_init (TotemMediaServer1Plugin *plugin)
{
}

static void
max_items_changed_cb (GtkSpinButton *spin,
                      gpointer user_data)
{
  max_items = (guint) gtk_spin_button_get_value (spin);
}

static GtkWidget *
configure_plugin (TotemPlugin *plugin)
{
  GtkWidget *content_area;
  GtkWidget *label;
  GtkWidget *spin;
  static GtkWidget *dialog = NULL;

  if (!dialog) {
    dialog = gtk_dialog_new_with_buttons ("Configure MediaServer1 Browser",
                                          NULL,
                                          0,
                                          GTK_STOCK_OK,
                                          GTK_RESPONSE_ACCEPT,
                                          NULL);

    content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    label = gtk_label_new ("Max. items to retrieve:");
    gtk_container_add (GTK_CONTAINER (content_area), label);
    spin = gtk_spin_button_new_with_range (0, G_MAXDOUBLE, 5);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), max_items);
    gtk_container_add (GTK_CONTAINER (content_area), spin);

    g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_hide), NULL);
    g_signal_connect (spin, "value-changed", G_CALLBACK (max_items_changed_cb), NULL);
  }

  gtk_widget_show_all (dialog);

  return dialog;
}

static GdkPixbuf *
load_icon (MS1ItemType type)
{
  GdkScreen *screen;
  GtkIconTheme *theme;
  GdkPixbuf *pixbuf;
  GError *error = NULL;
  const gchar *icon;

  switch (type) {
  case MS1_ITEM_TYPE_CONTAINER:
    icon = GTK_STOCK_DIRECTORY;
    break;
  case MS1_ITEM_TYPE_VIDEO:
  case MS1_ITEM_TYPE_MOVIE:
    icon = "gnome-mime-video";
    break;
  case MS1_ITEM_TYPE_AUDIO:
  case MS1_ITEM_TYPE_MUSIC:
    icon = "gnome-mime-audio";
    break;
  case MS1_ITEM_TYPE_IMAGE:
  case MS1_ITEM_TYPE_PHOTO:
    icon = "gnome-mime-image";
    break;
  default:
    icon = GTK_STOCK_FILE;
    break;
  }

  screen = gdk_screen_get_default ();
  theme = gtk_icon_theme_get_for_screen (screen);
  pixbuf = gtk_icon_theme_load_icon (theme, icon, 22, 22, &error);

  if (pixbuf == NULL) {
    g_warning ("Failed to load icon %s: %s", icon,  error->message);
    g_error_free (error);
  }

  return pixbuf;
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
get_properties_reply (GObject *source,
                      GAsyncResult *result,
                      gpointer user_data)
{
  GdkPixbuf *icon;
  GHashTable *properties;
  GtkTreeIter iter;
  MS1Client *provider = MS1_CLIENT (source);
  MS1ItemType type;
  TotemMediaServer1Plugin *self = TOTEM_MEDIA_SERVER1_PLUGIN (user_data);
  const gchar *title;

  properties = ms1_client_get_properties_finish (provider, result, NULL);

  if (properties) {
    title = ms1_client_get_display_name (properties);
    if (!title || title[0] == '\0') {
      title = ms1_client_get_provider_name (provider);
    }

    type =  ms1_client_get_item_type (properties);
    icon = load_icon (type);

    gtk_tree_store_append (GTK_TREE_STORE (self->browser_model), &iter, NULL);
    gtk_tree_store_set (GTK_TREE_STORE (self->browser_model),
                        &iter,
                        MODEL_PROVIDER, provider,
                        MODEL_TITLE, title,
                        MODEL_PATH, ms1_client_get_path (properties),
                        MODEL_TYPE, type,
                        MODEL_ICON, icon,
                        -1);

    g_signal_connect (provider,
                      "destroy",
                      (GCallback) provider_removed_cb,
                      self);

    g_object_unref (icon);
    g_hash_table_unref (properties);
  }

  g_object_unref (source);
}

static void
provider_added_cb (MS1Observer *observer,
                   const gchar *name,
                   gpointer user_data)
{
  MS1Client *provider;
  TotemMediaServer1Plugin *self = TOTEM_MEDIA_SERVER1_PLUGIN (user_data);

  provider = ms1_client_new (name);
  ms1_client_get_properties_async (provider,
                                   ms1_client_get_root_path (provider),
                                   properties,
                                   get_properties_reply,
                                   self);
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
cancel_browse (MS1Client *provider,
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
  GtkTreeIter iter;
  MS1Client *provider = MS1_CLIENT (source);
  MS1ItemType type;
  gchar **urls;
  GtkTreePath *path;
  gchar *url;
  gint remaining = PAGESIZE;

  if (!data->canceled) {
    children = ms1_client_list_children_finish (provider, result, NULL);
    for (child = children; child && data->offset < max_items; child = g_list_next (child)) {
      urls = ms1_client_get_urls (child->data);
      if (urls && urls[0]) {
        url = urls[0];
      } else {
        url = NULL;
      }

      type = ms1_client_get_item_type (child->data);
      icon = load_icon (type);

      gtk_tree_store_append (GTK_TREE_STORE (data->plugin->browser_model),
                             &iter,
                             data->parent_iter);
      gtk_tree_store_set (GTK_TREE_STORE (data->plugin->browser_model),
                          &iter,
                          MODEL_PROVIDER, data->provider,
                          MODEL_TITLE, ms1_client_get_display_name (child->data),
                          MODEL_PATH, ms1_client_get_path (child->data),
                          MODEL_TYPE, type,
                          MODEL_URL, url,
                          MODEL_ICON, icon,
                          -1);
      g_object_unref (icon);

      /* Expand only first time*/
      if (data->offset == 0) {
        path = gtk_tree_path_new_from_string (data->tree_path);
        gtk_tree_view_expand_row (GTK_TREE_VIEW (data->plugin->browser),
                                  path,
                                  FALSE);
        gtk_tree_path_free (path);
      }

      remaining--;
      data->offset++;
    }

    /* Free data */
    g_list_foreach (children, (GFunc) g_hash_table_unref, NULL);
    g_list_free (children);
  }

  /* Check if it was canceled, there is no more elements or we reached
     maximum */
  if (data->canceled || remaining > 0 || data->offset >= max_items) {
    g_signal_handler_disconnect (data->provider, data->cancel_handler);
    g_object_unref (data->provider);
    g_object_unref (data->plugin);
    g_free (data->object_path);
    g_free (data->tree_path);
    g_slice_free (GtkTreeIter, data->parent_iter);
    g_slice_free (BrowseData, data);
  } else {
    /* Continue browsing */
    ms1_client_list_children_async (data->provider,
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
  GtkTreeIter *iter;
  GtkTreeModel *model;
  MS1Client *provider;
  MS1ItemType type;
  TotemMediaServer1Plugin *self = TOTEM_MEDIA_SERVER1_PLUGIN (user_data);
  gchar *object_path;
  gchar *title;
  gchar *url;

  model = gtk_tree_view_get_model (tree_view);
  iter = g_slice_new (GtkTreeIter);
  gtk_tree_model_get_iter (model, iter, path);
  gtk_tree_model_get (model, iter,
                      MODEL_PROVIDER, &provider,
                      MODEL_PATH, &object_path,
                      MODEL_TYPE, &type,
                      MODEL_URL, &url,
                      MODEL_TITLE, &title,
                      -1);

  if (type == MS1_ITEM_TYPE_CONTAINER) {
    data = g_slice_new (BrowseData);
    data->canceled = FALSE;
    data->offset = 0;
    data->parent_iter = iter;
    data->object_path = g_strdup (object_path);
    data->tree_path = gtk_tree_path_to_string (path);
    data->plugin = g_object_ref (self);
    data->provider = g_object_ref (provider);
    data->cancel_handler = g_signal_connect (provider,
                                             "destroy",
                                             G_CALLBACK (cancel_browse),
                                             data);

    ms1_client_list_children_async (data->provider,
                                    data->object_path,
                                    data->offset,
                                    PAGESIZE,
                                    properties,
                                    list_children_reply,
                                    data);
  } else {
    g_slice_free (GtkTreeIter, iter);
    totem_add_to_playlist_and_play (self->totem, url, title, TRUE);
  }
}

static void
setup_ui (TotemMediaServer1Plugin *self)
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
  self->browser = gtk_tree_view_new ();
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (self->browser), FALSE);
  col = gtk_tree_view_column_new ();
  render_pixbuf = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (col, render_pixbuf, FALSE);
  gtk_tree_view_column_add_attribute (col, render_pixbuf, "pixbuf", MODEL_ICON);
  render_text = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (col, render_text, FALSE);
  gtk_tree_view_column_add_attribute (col, render_text, "text", MODEL_TITLE);
  gtk_tree_view_insert_column (GTK_TREE_VIEW (self->browser), col, -1);
  gtk_container_add (GTK_CONTAINER (scroll), self->browser);
  gtk_container_add (GTK_CONTAINER (box), scroll);

  self->browser_model =
    GTK_TREE_MODEL (gtk_tree_store_new (6,
                                        G_TYPE_OBJECT,     /* Provider */
                                        G_TYPE_STRING,     /* Name */
                                        G_TYPE_STRING,     /* Path */
                                        G_TYPE_INT,        /* Type */
                                        G_TYPE_STRING,     /* URL */
                                        GDK_TYPE_PIXBUF)); /* Icon */
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

static void
impl_deactivate (TotemPlugin *plugin,
                 TotemObject *totem)
{
  TotemMediaServer1Plugin *self = TOTEM_MEDIA_SERVER1_PLUGIN (plugin);

  totem_remove_sidebar_page (totem, "mediaserver1");
  g_object_unref (self->totem);
  g_object_unref (self->browser_model);
}
