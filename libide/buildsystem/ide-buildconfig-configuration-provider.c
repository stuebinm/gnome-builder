/* ide-buildconfig-configuration-provider.c
 *
 * Copyright (C) 2016 Matthew Leeds <mleeds@redhat.com>
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

#define G_LOG_DOMAIN "ide-buildconfig-configuration-provider"

#include <gio/gio.h>

#include "ide-context.h"
#include "ide-debug.h"
#include "ide-internal.h"
#include "ide-macros.h"

#include "buildsystem/ide-build-command.h"
#include "buildsystem/ide-build-command-queue.h"
#include "buildsystem/ide-buildconfig-configuration-provider.h"
#include "buildsystem/ide-configuration-manager.h"
#include "buildsystem/ide-configuration-provider.h"
#include "buildsystem/ide-configuration.h"
#include "buildsystem/ide-environment.h"
#include "vcs/ide-vcs.h"

#define DOT_BUILD_CONFIG ".buildconfig"

struct _IdeBuildconfigConfigurationProvider
{
  GObject                  parent_instance;
  IdeConfigurationManager *manager;
  GCancellable            *cancellable;
  GPtrArray               *configurations;
  GKeyFile                *key_file;
};

static void configuration_provider_iface_init (IdeConfigurationProviderInterface *);

G_DEFINE_TYPE_EXTENDED (IdeBuildconfigConfigurationProvider, ide_buildconfig_configuration_provider, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (IDE_TYPE_CONFIGURATION_PROVIDER,
                                               configuration_provider_iface_init))

static void ide_buildconfig_configuration_provider_load (IdeConfigurationProvider *provider, IdeConfigurationManager *manager);
static void ide_buildconfig_configuration_provider_unload (IdeConfigurationProvider *provider, IdeConfigurationManager *manager);

static void
load_string (IdeConfiguration *configuration,
             GKeyFile         *key_file,
             const gchar      *group,
             const gchar      *key,
             const gchar      *property)
{
  g_assert (IDE_IS_CONFIGURATION (configuration));
  g_assert (key_file != NULL);
  g_assert (group != NULL);
  g_assert (key != NULL);

  if (g_key_file_has_key (key_file, group, key, NULL))
    {
      g_auto(GValue) value = G_VALUE_INIT;

      g_value_init (&value, G_TYPE_STRING);
      g_value_take_string (&value, g_key_file_get_string (key_file, group, key, NULL));
      g_object_set_property (G_OBJECT (configuration), property, &value);
    }
}

static void
load_environ (IdeConfiguration *configuration,
              GKeyFile         *key_file,
              const gchar      *group)
{
  IdeEnvironment *environment;
  g_auto(GStrv) keys = NULL;

  g_assert (IDE_IS_CONFIGURATION (configuration));
  g_assert (key_file != NULL);
  g_assert (group != NULL);

  environment = ide_configuration_get_environment (configuration);
  keys = g_key_file_get_keys (key_file, group, NULL, NULL);

  if (keys != NULL)
    {
      guint i;

      for (i = 0; keys [i]; i++)
        {
          g_autofree gchar *value = NULL;

          value = g_key_file_get_string (key_file, group, keys [i], NULL);

          if (value != NULL)
            ide_environment_setenv (environment, keys [i], value);
        }
    }
}

static void
load_command_queue (IdeBuildCommandQueue *cmdq,
                    GKeyFile             *key_file,
                    const gchar          *group,
                    const gchar          *name)

{
  g_auto(GStrv) commands = NULL;

  g_assert (IDE_IS_BUILD_COMMAND_QUEUE (cmdq));
  g_assert (key_file != NULL);
  g_assert (group != NULL);
  g_assert (name != NULL);

  commands = g_key_file_get_string_list (key_file, group, name, NULL, NULL);

  if (commands != NULL)
    {
      for (guint i = 0; commands [i]; i++)
        {
          g_autoptr(IdeBuildCommand) command = NULL;

          command = g_object_new (IDE_TYPE_BUILD_COMMAND,
                                  "command-text", commands [i],
                                  NULL);
          ide_build_command_queue_append (cmdq, command);
        }
    }
}

static gboolean
ide_buildconfig_configuration_provider_load_group (IdeBuildconfigConfigurationProvider  *self,
                                                   GKeyFile                             *key_file,
                                                   const gchar                          *group,
                                                   GError                              **error)
{
  g_autoptr(IdeConfiguration) configuration = NULL;
  g_autofree gchar *env_group = NULL;
  IdeContext *context;

  g_assert (IDE_IS_BUILDCONFIG_CONFIGURATION_PROVIDER (self));
  g_assert (key_file != NULL);
  g_assert (group != NULL);

  context = ide_object_get_context (IDE_OBJECT (self->manager));

  configuration = g_object_new (IDE_TYPE_CONFIGURATION,
                                "id", group,
                                "context", context,
                                NULL);

  load_string (configuration, key_file, group, "config-opts", "config-opts");
  load_string (configuration, key_file, group, "device", "device-id");
  load_string (configuration, key_file, group, "name", "display-name");
  load_string (configuration, key_file, group, "runtime", "runtime-id");
  load_string (configuration, key_file, group, "prefix", "prefix");
  load_string (configuration, key_file, group, "app-id", "app-id");

  if (g_key_file_has_key (key_file, group, "prebuild", NULL))
    {
      g_autoptr(IdeBuildCommandQueue) cmdq = NULL;

      cmdq = ide_build_command_queue_new ();
      load_command_queue (cmdq, key_file, group, "prebuild");
      _ide_configuration_set_prebuild (configuration, cmdq);
    }

  if (g_key_file_has_key (key_file, group, "postbuild", NULL))
    {
      g_autoptr(IdeBuildCommandQueue) cmdq = NULL;

      cmdq = ide_build_command_queue_new ();
      load_command_queue (cmdq, key_file, group, "postbuild");
      _ide_configuration_set_postbuild (configuration, cmdq);
    }

  env_group = g_strdup_printf ("%s.environment", group);

  if (g_key_file_has_group (key_file, env_group))
    load_environ (configuration, key_file, env_group);

  ide_configuration_set_dirty (configuration, FALSE);

  ide_configuration_manager_add (self->manager, configuration);

  g_ptr_array_add (self->configurations, configuration);

  if (g_key_file_get_boolean (key_file, group, "default", NULL))
    ide_configuration_manager_set_current (self->manager, configuration);

  return TRUE;
}

static gboolean
ide_buildconfig_configuration_provider_restore (IdeBuildconfigConfigurationProvider  *self,
                                                GFile                                *file,
                                                GCancellable                         *cancellable,
                                                GError                              **error)
{
  g_autofree gchar *contents = NULL;
  g_auto(GStrv) groups = NULL;
  gsize length = 0;
  guint i;

  IDE_ENTRY;

  g_assert (IDE_IS_BUILDCONFIG_CONFIGURATION_PROVIDER (self));
  g_assert (self->key_file == NULL);
  g_assert (G_IS_FILE (file));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  self->key_file = g_key_file_new ();

  if (!g_file_load_contents (file, cancellable, &contents, &length, NULL, error))
    IDE_RETURN (FALSE);

  if (!g_key_file_load_from_data (self->key_file,
                                  contents,
                                  length,
                                  G_KEY_FILE_KEEP_COMMENTS,
                                  error))
    IDE_RETURN (FALSE);

  groups = g_key_file_get_groups (self->key_file, NULL);

  for (i = 0; groups [i]; i++)
    {
      if (g_str_has_suffix (groups [i], ".environment"))
        continue;

      if (!ide_buildconfig_configuration_provider_load_group (self, self->key_file, groups [i], error))
        IDE_RETURN (FALSE);
    }

  IDE_RETURN (TRUE);
}

static void
ide_buildconfig_configuration_provider_load_worker (GTask        *task,
                                                    gpointer      source_object,
                                                    gpointer      task_data,
                                                    GCancellable *cancellable)
{
  IdeBuildconfigConfigurationProvider *self = source_object;
  g_autoptr(GFile) settings_file = NULL;
  g_autoptr(GError) error = NULL;
  IdeContext *context;
  IdeVcs *vcs;
  GFile *workdir;

  IDE_ENTRY;

  g_assert (G_IS_TASK (task));
  g_assert (IDE_IS_BUILDCONFIG_CONFIGURATION_PROVIDER (self));
  g_assert (IDE_IS_CONFIGURATION_MANAGER (self->manager));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  context = ide_object_get_context (IDE_OBJECT (self->manager));
  vcs = ide_context_get_vcs (context);
  workdir = ide_vcs_get_working_directory (vcs);
  settings_file = g_file_get_child (workdir, DOT_BUILD_CONFIG);

  if (!g_file_query_exists (settings_file, cancellable) ||
      !ide_buildconfig_configuration_provider_restore (self, settings_file, cancellable, &error))
    {
      if (error != NULL)
        g_warning ("Failed to restore configuration: %s", error->message);
    }

  g_task_return_boolean (task, TRUE);

  IDE_EXIT;
}

static void
ide_buildconfig_configuration_provider_load_cb (GObject      *object,
                                                GAsyncResult *result,
                                                gpointer      user_data)
{
  IdeBuildconfigConfigurationProvider *self = (IdeBuildconfigConfigurationProvider *)object;
  GError *error = NULL;

  IDE_ENTRY;

  g_assert (IDE_IS_BUILDCONFIG_CONFIGURATION_PROVIDER (self));
  g_assert (G_IS_TASK (result));

  if (!g_task_propagate_boolean (G_TASK (result), &error))
    {
      g_warning ("%s", error->message);
      g_clear_error (&error);
    }

  IDE_EXIT;
}

static void
ide_buildconfig_configuration_provider_load (IdeConfigurationProvider *provider,
                                             IdeConfigurationManager  *manager)
{
  IdeBuildconfigConfigurationProvider *self = (IdeBuildconfigConfigurationProvider *)provider;
  g_autoptr(GTask) task = NULL;

  IDE_ENTRY;

  g_assert (IDE_IS_BUILDCONFIG_CONFIGURATION_PROVIDER (self));
  g_assert (IDE_IS_CONFIGURATION_MANAGER (manager));

  ide_set_weak_pointer (&self->manager, manager);

  self->cancellable = g_cancellable_new ();
  self->configurations = g_ptr_array_new_with_free_func (g_object_unref);

  task = g_task_new (self, self->cancellable, ide_buildconfig_configuration_provider_load_cb, NULL);
  g_task_run_in_thread (task, ide_buildconfig_configuration_provider_load_worker);

  IDE_EXIT;
}

static void
ide_buildconfig_configuration_provider_unload (IdeConfigurationProvider *provider,
                                               IdeConfigurationManager  *manager)
{
  IdeBuildconfigConfigurationProvider *self = (IdeBuildconfigConfigurationProvider *)provider;

  IDE_ENTRY;

  g_assert (IDE_IS_BUILDCONFIG_CONFIGURATION_PROVIDER (self));
  g_assert (IDE_IS_CONFIGURATION_MANAGER (manager));

  if (self->configurations != NULL)
    {
      for (guint i= 0; i < self->configurations->len; i++)
        {
          IdeConfiguration *configuration = g_ptr_array_index (self->configurations, i);

          ide_configuration_manager_remove (manager, configuration);
        }
    }

  g_clear_pointer (&self->configurations, g_ptr_array_unref);

  if (self->cancellable != NULL)
    g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  ide_clear_weak_pointer (&self->manager);

  IDE_EXIT;
}

static void
ide_buildconfig_configuration_provider_class_init (IdeBuildconfigConfigurationProviderClass *klass)
{
}

static void
ide_buildconfig_configuration_provider_init (IdeBuildconfigConfigurationProvider *self)
{
}

static void
configuration_provider_iface_init (IdeConfigurationProviderInterface *iface)
{
  iface->load = ide_buildconfig_configuration_provider_load;
  iface->unload = ide_buildconfig_configuration_provider_unload;
}
