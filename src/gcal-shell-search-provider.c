/* gcal-shell-search-provider.c
 *
 * Copyright (C) 2015 Erick Pérez Castellanos <erick.red@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gcal-shell-search-provider.h"
#include "gcal-shell-search-provider-generated.h"

#include "gcal-application.h"
#include "gcal-event.h"
#include "gcal-window.h"
#include "gcal-utils.h"

typedef struct
{
  GDBusMethodInvocation    *invocation;
  gchar                   **terms;
  icaltimetype              date;
} PendingSearch;

typedef struct
{
  GcalShellSearchProvider2 *skel;
  GcalManager              *manager;

  PendingSearch            *pending_search;
  guint                     scheduled_search_id;
  GHashTable               *events;
} GcalShellSearchProviderPrivate;

struct _GcalShellSearchProvider
{
  GObject parent;

  /*< private >*/
  GcalShellSearchProviderPrivate *priv;
};

static void   gcal_subscriber_interface_init (ECalDataModelSubscriberInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GcalShellSearchProvider, gcal_shell_search_provider, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (GcalShellSearchProvider)
                         G_IMPLEMENT_INTERFACE (E_TYPE_CAL_DATA_MODEL_SUBSCRIBER, gcal_subscriber_interface_init));

static gint
sort_event_data (GcalEvent *a,
                 GcalEvent *b,
                 gpointer   user_data)
{
  return gcal_event_compare_with_current (a, b, user_data);
}

static gboolean
execute_search (GcalShellSearchProvider *search_provider)
{
  GcalShellSearchProviderPrivate *priv;

  guint i;
  gchar *search_query;

  icaltimezone *zone;
  time_t range_start, range_end;

  priv = search_provider->priv;

  if (!gcal_manager_load_completed (priv->manager))
    return TRUE;

  zone = gcal_manager_get_system_timezone (priv->manager);
  priv->pending_search->date = icaltime_current_time_with_zone (zone);
  icaltime_adjust (&(priv->pending_search->date), -7, 0, 0, 0); /* -1 weeks from today */
  range_start = icaltime_as_timet_with_zone (priv->pending_search->date, zone);

  icaltime_adjust (&(priv->pending_search->date), 21 * 2, 0, 0, 0); /* +3 weeks from today */
  range_end = icaltime_as_timet_with_zone (priv->pending_search->date, zone);

  gcal_manager_set_shell_search_subscriber (priv->manager, E_CAL_DATA_MODEL_SUBSCRIBER (search_provider),
                                            range_start, range_end);

  search_query = g_strdup_printf ("(or (contains? \"summary\" \"%s\") (contains? \"description\" \"%s\"))",
                                  priv->pending_search->terms[0], priv->pending_search->terms[0]);
  for (i = 1; i < g_strv_length (priv->pending_search->terms); i++)
    {
      gchar *complete_query;
      gchar *second_query = g_strdup_printf ("(or (contains? \"summary\" \"%s\") (contains? \"description\" \"%s\"))",
                                             priv->pending_search->terms[0], priv->pending_search->terms[0]);
      complete_query = g_strdup_printf ("(and %s %s)", search_query, second_query);

      g_free (second_query);
      g_free (search_query);

      search_query = complete_query;
    }

  gcal_manager_set_shell_search_query (priv->manager,  search_query);
  g_free (search_query);

  priv->scheduled_search_id = 0;
  g_application_hold (g_application_get_default ());
  return FALSE;
}

static void
schedule_search (GcalShellSearchProvider *search_provider,
                 GDBusMethodInvocation   *invocation,
                 gchar                  **terms)
{
  GcalShellSearchProviderPrivate *priv = search_provider->priv;

  /* don't attempt searches for a single character */
  if (g_strv_length (terms) == 1 && g_utf8_strlen (terms[0], -1) == 1)
    {
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(as)", NULL));
      return;
    }

  if (priv->pending_search != NULL)
    {
      g_object_unref (priv->pending_search->invocation);
      g_strfreev (priv->pending_search->terms);

      if (priv->scheduled_search_id == 0)
        g_application_release (g_application_get_default ());
    }
  else
    {
      priv->pending_search = g_new0 (PendingSearch, 1);
    }

  if (priv->scheduled_search_id != 0)
    {
      g_source_remove (priv->scheduled_search_id);
      priv->scheduled_search_id = 0;
    }

  priv->pending_search->invocation = g_object_ref (invocation);
  priv->pending_search->terms = g_strdupv (terms);

  if (!gcal_manager_load_completed (priv->manager))
    {
      priv->scheduled_search_id = g_timeout_add_seconds (1, (GSourceFunc) execute_search, search_provider);
      return;
    }

  execute_search (search_provider);
  return;
}

static gboolean
get_initial_result_set_cb (GcalShellSearchProvider  *search_provider,
                           GDBusMethodInvocation    *invocation,
                           gchar                   **terms,
                           GcalShellSearchProvider2 *skel)
{
  schedule_search (search_provider, invocation, terms);
  return TRUE;
}

static gboolean
get_subsearch_result_set_cb (GcalShellSearchProvider  *search_provider,
                             GDBusMethodInvocation    *invocation,
                             gchar                   **previous_results,
                             gchar                   **terms,
                             GcalShellSearchProvider2 *skel)
{
  schedule_search (search_provider, invocation, terms);
  return TRUE;
}

static gboolean
get_result_metas_cb (GcalShellSearchProvider  *search_provider,
                     GDBusMethodInvocation    *invocation,
                     gchar                   **results,
                     GcalShellSearchProvider2 *skel)
{
  GcalShellSearchProviderPrivate *priv;
  GDateTime *local_datetime;
  GVariantBuilder abuilder, builder;
  GVariant *icon_variant;
  GcalEvent *event;
  GdkPixbuf *gicon;
  gchar *uuid, *desc;
  gchar *start_date;
  gint i;

  priv = search_provider->priv;

  g_variant_builder_init (&abuilder, G_VARIANT_TYPE ("aa{sv}"));
  for (i = 0; i < g_strv_length (results); i++)
    {
      uuid = results[i];
      event = g_hash_table_lookup (priv->events, uuid);

      g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_add (&builder, "{sv}", "id", g_variant_new_string (uuid));
      g_variant_builder_add (&builder, "{sv}", "name", g_variant_new_string (gcal_event_get_summary (event)));

      gicon = get_circle_pixbuf_from_color (gcal_event_get_color (event), 128);
      icon_variant = g_icon_serialize (G_ICON (gicon));
      g_variant_builder_add (&builder, "{sv}", "icon", icon_variant);
      g_object_unref (gicon);
      g_variant_unref (icon_variant);

      local_datetime = g_date_time_to_local (gcal_event_get_date_start (event));

      /* FIXME: respect 24h time format */
      start_date = g_date_time_format (local_datetime, gcal_event_get_all_day (event) ? "%x" : "%c");

      if (gcal_event_get_location (event))
        desc = g_strconcat (start_date, ". ", gcal_event_get_location (event), NULL);
      else
        desc = g_strdup (start_date);

      g_variant_builder_add (&builder, "{sv}", "description", g_variant_new_string (desc));
      g_variant_builder_add_value (&abuilder, g_variant_builder_end (&builder));
    }
  g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", &abuilder));

  return TRUE;
}

static gboolean
activate_result_cb (GcalShellSearchProvider  *search_provider,
                    GDBusMethodInvocation    *invocation,
                    gchar                    *result,
                    gchar                   **terms,
                    guint32                   timestamp,
                    GcalShellSearchProvider2 *skel)
{
  GcalShellSearchProviderPrivate *priv;
  GApplication *application;
  GcalEvent *event;
  GDateTime *dtstart;

  priv = search_provider->priv;
  application = g_application_get_default ();

  event = gcal_manager_get_event_from_shell_search (priv->manager, result);
  dtstart = gcal_event_get_date_start (event);

  gcal_application_set_uuid (GCAL_APPLICATION (application), result);
  gcal_application_set_initial_date (GCAL_APPLICATION (application), dtstart);

  g_application_activate (application);

  g_clear_object (&event);

  return TRUE;
}

static gboolean
launch_search_cb (GcalShellSearchProvider  *search_provider,
                  GDBusMethodInvocation    *invocation,
                  gchar                   **terms,
                  guint32                   timestamp,
                  GcalShellSearchProvider2 *skel)
{
  GApplication *application;
  gchar *terms_joined;
  GList *windows;

  application = g_application_get_default ();
  g_application_activate (application);

  terms_joined = g_strjoinv (" ", terms);
  windows = g_list_reverse (gtk_application_get_windows (GTK_APPLICATION (application)));
  if (windows != NULL)
    {
      gcal_window_set_search_mode (GCAL_WINDOW (windows->data), TRUE);
      gcal_window_set_search_query (GCAL_WINDOW (windows->data), terms_joined);

      g_list_free (windows);
    }

  g_free (terms_joined);
  return TRUE;
}

static gboolean
query_completed_cb (GcalShellSearchProvider *search_provider,
                    GcalManager             *manager)
{
  GcalShellSearchProviderPrivate *priv = search_provider->priv;
  GList *events, *l;
  GVariantBuilder builder;
  time_t current_time_t;

  g_hash_table_remove_all (priv->events);

  events = gcal_manager_get_shell_search_events (priv->manager);
  if (events == NULL)
    {
      g_dbus_method_invocation_return_value (priv->pending_search->invocation, g_variant_new ("(as)", NULL));
      goto out;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));

  current_time_t = time (NULL);
  events = g_list_sort_with_data (events, (GCompareDataFunc) sort_event_data, &current_time_t);
  for (l = events; l != NULL; l = g_list_next (l))
    {
      const gchar *uid;

      uid = gcal_event_get_uid (l->data);

      if (g_hash_table_contains (priv->events, uid))
        continue;

      g_variant_builder_add (&builder, "s", uid);

      g_hash_table_insert (priv->events, g_strdup (uid), l->data);
    }
  g_list_free (events);

  g_dbus_method_invocation_return_value (priv->pending_search->invocation, g_variant_new ("(as)", &builder));

out:
  g_object_unref (priv->pending_search->invocation);
  g_strfreev (priv->pending_search->terms);
  g_clear_pointer (&(priv->pending_search), g_free);
  g_application_release (g_application_get_default ());
  return FALSE;
}

static void
gcal_shell_search_provider_component_changed (ECalDataModelSubscriber *subscriber,
                                              ECalClient              *client,
                                              ECalComponent           *comp)
{
  ;
}

static void
gcal_shell_search_provider_component_removed (ECalDataModelSubscriber *subscriber,
                                              ECalClient              *client,
                                              const gchar             *uid,
                                              const gchar             *rid)
{
  ;
}

static void
gcal_shell_search_provider_freeze (ECalDataModelSubscriber *subscriber)
{
  ;
}

static void
gcal_shell_search_provider_thaw (ECalDataModelSubscriber *subscriber)
{
  ;
}

static void
gcal_shell_search_provider_finalize (GObject *object)
{
  GcalShellSearchProviderPrivate *priv = GCAL_SHELL_SEARCH_PROVIDER (object)->priv;

  g_hash_table_destroy (priv->events);
  g_clear_object (&priv->skel);
  G_OBJECT_CLASS (gcal_shell_search_provider_parent_class)->finalize (object);
}

static void
gcal_subscriber_interface_init (ECalDataModelSubscriberInterface *iface)
{
  iface->component_added = gcal_shell_search_provider_component_changed;
  iface->component_modified = gcal_shell_search_provider_component_changed;
  iface->component_removed = gcal_shell_search_provider_component_removed;
  iface->freeze = gcal_shell_search_provider_freeze;
  iface->thaw = gcal_shell_search_provider_thaw;
}

static void
gcal_shell_search_provider_class_init (GcalShellSearchProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gcal_shell_search_provider_finalize;
}

static void
gcal_shell_search_provider_init (GcalShellSearchProvider *self)
{
  GcalShellSearchProviderPrivate *priv = gcal_shell_search_provider_get_instance_private (self);

  priv->events = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  priv->skel = gcal_shell_search_provider2_skeleton_new ();

  g_signal_connect_swapped (priv->skel, "handle-get-initial-result-set", G_CALLBACK (get_initial_result_set_cb), self);
  g_signal_connect_swapped (priv->skel, "handle-get-subsearch-result-set", G_CALLBACK (get_subsearch_result_set_cb), self);
  g_signal_connect_swapped (priv->skel, "handle-get-result-metas", G_CALLBACK (get_result_metas_cb), self);
  g_signal_connect_swapped (priv->skel, "handle-activate-result", G_CALLBACK (activate_result_cb), self);
  g_signal_connect_swapped (priv->skel, "handle-launch-search", G_CALLBACK (launch_search_cb), self);

  self->priv = priv;
}

GcalShellSearchProvider*
gcal_shell_search_provider_new (void)
{
  return g_object_new (GCAL_TYPE_SHELL_SEARCH_PROVIDER, NULL);
}

gboolean
gcal_shell_search_provider_dbus_export (GcalShellSearchProvider *search_provider,
                                        GDBusConnection         *connection,
                                        const gchar             *object_path,
                                        GError                 **error)
{
  GcalShellSearchProviderPrivate *priv = GCAL_SHELL_SEARCH_PROVIDER (search_provider)->priv;

  return g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (priv->skel), connection, object_path, error);
}

void
gcal_shell_search_provider_dbus_unexport (GcalShellSearchProvider *search_provider,
                                          GDBusConnection         *connection,
                                          const gchar             *object_path)
{
  GcalShellSearchProviderPrivate *priv = GCAL_SHELL_SEARCH_PROVIDER (search_provider)->priv;

  if (g_dbus_interface_skeleton_has_connection (G_DBUS_INTERFACE_SKELETON (priv->skel), connection))
    g_dbus_interface_skeleton_unexport_from_connection (G_DBUS_INTERFACE_SKELETON (priv->skel), connection);
}

void
gcal_shell_search_provider_connect (GcalShellSearchProvider *search_provider,
                                    GcalManager             *manager)
{
  search_provider->priv->manager = manager;

  gcal_manager_setup_shell_search (manager, E_CAL_DATA_MODEL_SUBSCRIBER (search_provider));
  g_signal_connect_swapped (manager, "query-completed", G_CALLBACK (query_completed_cb), search_provider);
}
