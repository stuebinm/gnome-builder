/* gbp-flatpak-pipeline-addin.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "gbp-flatpak-pipeline-addin"

#include "gbp-flatpak-pipeline-addin.h"
#include "gbp-flatpak-runtime.h"
#include "gbp-flatpak-transfer.h"
#include "gbp-flatpak-util.h"

enum {
  PREPARE_MKDIRS,
  PREPARE_REMOTES,
  PREPARE_BUILD_INIT,
};

static IdeSubprocessLauncher *
create_subprocess_launcher (void)
{
  IdeSubprocessLauncher *launcher;

  launcher = ide_subprocess_launcher_new (0);
  ide_subprocess_launcher_set_run_on_host (launcher, TRUE);
  ide_subprocess_launcher_set_clear_env (launcher, FALSE);

  return launcher;
}

static gboolean
register_mkdirs_stage (GbpFlatpakPipelineAddin  *self,
                       IdeBuildPipeline         *pipeline,
                       IdeContext               *context,
                       GError                  **error)
{
  g_autoptr(IdeBuildStage) mkdirs = NULL;
  IdeConfiguration *config;
  g_autofree gchar *repo_dir = NULL;
  g_autofree gchar *staging_dir = NULL;
  guint stage_id;

  g_assert (GBP_IS_FLATPAK_PIPELINE_ADDIN (self));
  g_assert (IDE_IS_BUILD_PIPELINE (pipeline));
  g_assert (IDE_IS_CONTEXT (context));

  config = ide_build_pipeline_get_configuration (pipeline);

  mkdirs = ide_build_stage_mkdirs_new (context);

  repo_dir = gbp_flatpak_get_repo_dir (config);
  staging_dir = gbp_flatpak_get_staging_dir (config);

  ide_build_stage_mkdirs_add_path (IDE_BUILD_STAGE_MKDIRS (mkdirs), repo_dir, TRUE, 0750);
  ide_build_stage_mkdirs_add_path (IDE_BUILD_STAGE_MKDIRS (mkdirs), staging_dir, TRUE, 0750);

  stage_id = ide_build_pipeline_connect (pipeline, IDE_BUILD_PHASE_PREPARE, PREPARE_MKDIRS, mkdirs);

  ide_build_pipeline_addin_track (IDE_BUILD_PIPELINE_ADDIN (self), stage_id);

  return TRUE;
}

static gboolean
register_remotes_stage (GbpFlatpakPipelineAddin  *self,
                        IdeBuildPipeline         *pipeline,
                        IdeContext               *context,
                        GError                  **error)
{
  g_autoptr(IdeBuildStage) stage = NULL;
  g_autoptr(IdeSubprocessLauncher) launcher = NULL;
  IdeConfiguration *config;
  const gchar *branch;
  const gchar *platform;
  const gchar *sdk;
  const gchar *repo_name = NULL;
  const gchar *repo_path = NULL;
  guint stage_id;

  g_assert (GBP_IS_FLATPAK_PIPELINE_ADDIN (self));
  g_assert (IDE_IS_BUILD_PIPELINE (pipeline));
  g_assert (IDE_IS_CONTEXT (context));

  config = ide_build_pipeline_get_configuration (pipeline);

  platform = ide_configuration_get_internal_string (config, "flatpak-platform");
  sdk = ide_configuration_get_internal_string (config, "flatpak-sdk");
  branch = ide_configuration_get_internal_string (config, "flatpak-branch");

  if (ide_str_equal0 (platform, "org.gnome.Platform") ||
      ide_str_equal0 (platform, "org.gnome.Sdk") ||
      ide_str_equal0 (sdk, "org.gnome.Platform") ||
      ide_str_equal0 (sdk, "org.gnome.Sdk"))
    {
      if (ide_str_equal0 (branch, "master"))
        {
          repo_name = "gnome-nightly";
          repo_path = "https://sdk.gnome.org/gnome-nightly.flatpakrepo";
        }
      else
        {
          repo_name = "gnome";
          repo_path = "https://sdk.gnome.org/gnome.flatpakrepo";
        }
    }

  if (repo_name == NULL || repo_path == NULL)
    return TRUE;

  launcher = create_subprocess_launcher ();

  ide_subprocess_launcher_push_argv (launcher, "flatpak");
  ide_subprocess_launcher_push_argv (launcher, "remote-add");
  ide_subprocess_launcher_push_argv (launcher, "--user");
  ide_subprocess_launcher_push_argv (launcher, "--if-not-exists");
  ide_subprocess_launcher_push_argv (launcher, "--from");
  ide_subprocess_launcher_push_argv (launcher, repo_name);
  ide_subprocess_launcher_push_argv (launcher, repo_path);

  stage = g_object_new (IDE_TYPE_BUILD_STAGE_LAUNCHER,
                        "launcher", launcher,
                        "context", context,
                        NULL);

  stage_id = ide_build_pipeline_connect (pipeline, IDE_BUILD_PHASE_PREPARE, PREPARE_REMOTES, stage);

  ide_build_pipeline_addin_track (IDE_BUILD_PIPELINE_ADDIN (self), stage_id);

  return TRUE;
}

static gboolean
register_download_stage (GbpFlatpakPipelineAddin  *self,
                         IdeBuildPipeline         *pipeline,
                         IdeContext               *context,
                         GError                  **error)
{
  IdeConfiguration *config;
  const gchar *items[2] = { NULL };
  const gchar *platform;
  const gchar *sdk;
  const gchar *branch;
  guint stage_id;

  g_assert (GBP_IS_FLATPAK_PIPELINE_ADDIN (self));
  g_assert (IDE_IS_BUILD_PIPELINE (pipeline));

  config = ide_build_pipeline_get_configuration (pipeline);
  platform = ide_configuration_get_internal_string (config, "flatpak-platform");
  sdk = ide_configuration_get_internal_string (config, "flatpak-sdk");
  branch = ide_configuration_get_internal_string (config, "flatpak-branch");

  items[0] = platform;
  items[1] = sdk;

  for (guint i = 0; i < G_N_ELEMENTS (items); i++)
    {
      g_autoptr(IdeBuildStage) stage = NULL;
      g_autoptr(IdeTransfer) transfer = NULL;
      const gchar *id = items[i];

      if (id == NULL)
        continue;

      transfer = g_object_new (GBP_TYPE_FLATPAK_TRANSFER,
                               "context", context,
                               "id", id,
                               "branch", branch,
                               NULL);

      stage = g_object_new (IDE_TYPE_BUILD_STAGE_TRANSFER,
                            "context", context,
                            "transfer", transfer,
                            NULL);

      stage_id = ide_build_pipeline_connect (pipeline, IDE_BUILD_PHASE_DOWNLOADS, i, stage);
      ide_build_pipeline_addin_track (IDE_BUILD_PIPELINE_ADDIN (self), stage_id);
    }

  return TRUE;
}

static void
check_if_file_exists (IdeBuildStage    *stage,
                      IdeBuildPipeline *pipeline,
                      GCancellable     *cancellable,
                      const gchar      *manifest_path)
{
  gboolean exists;

  g_assert (IDE_IS_BUILD_STAGE (stage));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_assert (manifest_path != NULL);

  /*
   * TODO: We might want to verify that the manifest information matches
   *       what is in the current pipeline configuration.
   */

  exists = g_file_test (manifest_path, G_FILE_TEST_IS_REGULAR);
  ide_build_stage_set_completed (stage, exists);
}

static gboolean
register_build_init_stage (GbpFlatpakPipelineAddin  *self,
                           IdeBuildPipeline         *pipeline,
                           IdeContext               *context,
                           GError                  **error)
{
  g_autoptr(IdeSubprocessLauncher) launcher = NULL;
  g_autoptr(IdeBuildStage) stage = NULL;
  g_autofree gchar *staging_dir = NULL;
  g_autofree gchar *manifest_path = NULL;
  IdeConfiguration *config;
  const gchar *app_id;
  const gchar *platform;
  const gchar *sdk;
  const gchar *branch;
  guint stage_id;

  g_assert (GBP_IS_FLATPAK_PIPELINE_ADDIN (self));
  g_assert (IDE_IS_BUILD_PIPELINE (pipeline));
  g_assert (IDE_IS_CONTEXT (context));

  launcher = create_subprocess_launcher ();

  config = ide_build_pipeline_get_configuration (pipeline);

  staging_dir = gbp_flatpak_get_staging_dir (config);
  platform = ide_configuration_get_internal_string (config, "flatpak-platform");
  app_id = ide_configuration_get_app_id (config);
  sdk = ide_configuration_get_internal_string (config, "flatpak-sdk");
  branch = ide_configuration_get_internal_string (config, "flatpak-branch");

  manifest_path = g_build_filename (staging_dir, "manifest", NULL);

  ide_subprocess_launcher_push_argv (launcher, "flatpak");
  ide_subprocess_launcher_push_argv (launcher, "build-init");
  ide_subprocess_launcher_push_argv (launcher, staging_dir);
  ide_subprocess_launcher_push_argv (launcher, app_id);
  ide_subprocess_launcher_push_argv (launcher, sdk);
  ide_subprocess_launcher_push_argv (launcher, platform);
  ide_subprocess_launcher_push_argv (launcher, branch);

  stage = g_object_new (IDE_TYPE_BUILD_STAGE,
                        "context", context,
                        "launcher", launcher,
                        NULL);

  /*
   * We want to avoid calling build-init if it has already been called.
   * To check if this has happened, we just look for the manifest file
   * located in the directory that had build-init called.
   */
  g_signal_connect_data (stage,
                         "query",
                         G_CALLBACK (check_if_file_exists),
                         g_steal_pointer (&manifest_path),
                         (GClosureNotify)g_free,
                         G_CONNECT_SWAPPED);

  stage_id = ide_build_pipeline_connect (pipeline,
                                         IDE_BUILD_PHASE_PREPARE,
                                         PREPARE_BUILD_INIT,
                                         stage);
  ide_build_pipeline_addin_track (IDE_BUILD_PIPELINE_ADDIN (self), stage_id);

  return TRUE;
}

static gboolean
register_dependencies_stage (GbpFlatpakPipelineAddin  *self,
                             IdeBuildPipeline         *pipeline,
                             IdeContext               *context,
                             GError                  **error)
{
  g_autoptr(IdeBuildStage) stage = NULL;
  g_autoptr(IdeSubprocessLauncher) launcher = NULL;
  g_autofree gchar *staging_dir = NULL;
  g_autofree gchar *stop_at_option = NULL;
  IdeConfiguration *config;
  const gchar *manifest_path;
  const gchar *primary_module;
  guint stage_id;

  g_assert (GBP_IS_FLATPAK_PIPELINE_ADDIN (self));
  g_assert (IDE_IS_BUILD_PIPELINE (pipeline));
  g_assert (IDE_IS_CONTEXT (context));

  config = ide_build_pipeline_get_configuration (pipeline);

  primary_module = ide_configuration_get_internal_string (config, "flatpak-module");
  manifest_path = ide_configuration_get_internal_string (config, "flatpak-manifest");

  /* If there is no manifest, then there are no dependencies
   * to build for this configuration.
   */
  if (manifest_path == NULL)
    return TRUE;

  staging_dir = gbp_flatpak_get_staging_dir (config);

  launcher = create_subprocess_launcher ();

  ide_subprocess_launcher_push_argv (launcher, "flatpak-builder");
  ide_subprocess_launcher_push_argv (launcher, "--ccache");
  ide_subprocess_launcher_push_argv (launcher, "--force-clean");
  stop_at_option = g_strdup_printf ("--stop-at=%s", primary_module);
  ide_subprocess_launcher_push_argv (launcher, stop_at_option);
  ide_subprocess_launcher_push_argv (launcher, staging_dir);
  ide_subprocess_launcher_push_argv (launcher, manifest_path);

  stage = g_object_new (IDE_TYPE_BUILD_STAGE,
                        "context", context,
                        "launcher", launcher,
                        NULL);

  stage_id = ide_build_pipeline_connect (pipeline, IDE_BUILD_PHASE_DEPENDENCIES, 0, stage);
  ide_build_pipeline_addin_track (IDE_BUILD_PIPELINE_ADDIN (self), stage_id);

  return TRUE;
}

static gboolean
register_build_finish_stage (GbpFlatpakPipelineAddin  *self,
                             IdeBuildPipeline         *pipeline,
                             IdeContext               *context,
                             GError                  **error)
{
  g_autoptr(IdeBuildStage) stage = NULL;
  g_autoptr(IdeSubprocessLauncher) launcher = NULL;
  g_autofree gchar *staging_dir = NULL;
  g_autofree gchar *export_path = NULL;
  IdeConfiguration *config;
  const gchar *manifest_path;
  const gchar *primary_module;
  guint stage_id;

  g_assert (GBP_IS_FLATPAK_PIPELINE_ADDIN (self));
  g_assert (IDE_IS_BUILD_PIPELINE (pipeline));
  g_assert (IDE_IS_CONTEXT (context));

  config = ide_build_pipeline_get_configuration (pipeline);

  primary_module = ide_configuration_get_internal_string (config, "flatpak-module");
  manifest_path = ide_configuration_get_internal_string (config, "flatpak-manifest");

  /* If there is no manifest, then there are no dependencies
   * to build for this configuration.
   */
  if (manifest_path == NULL)
    return TRUE;

  staging_dir = gbp_flatpak_get_staging_dir (config);
  export_path = g_build_filename (staging_dir, "export", NULL);

  launcher = create_subprocess_launcher ();

  ide_subprocess_launcher_push_argv (launcher, "flatpak");
  ide_subprocess_launcher_push_argv (launcher, "build-finish");

  if (!ide_str_empty0 (command))
    {
      g_autofree gchar *command_option = NULL;

      command_option = g_strdup_printf ("--command=%s", command);
      ide_subprocess_launcher_push_argv (launcher, command_option);
    }

  ide_subprocess_launcher_push_args (launcher, (const gchar * const *)finish_args[i]);
  ide_subprocess_launcher_push_argv (launcher, staging_dir);

  stage = g_object_new (IDE_TYPE_BUILD_STAGE,
                        "context", context,
                        "launcher", launcher,
                        NULL);

  /*
   * If the export directory is found, we already performed the build-finish
   * and we do not need to run this operation again. So check if the file
   * exists and update IdeBuildStage:completed.
   */
  g_signal_connect_data (stage,
                         "query",
                         G_CALLBACK (check_if_file_exists),
                         g_steal_pointer (&export_path),
                         (GClosureNotify)g_free,
                         G_CONNECT_SWAPPED);

  stage_id = ide_build_pipeline_connect (pipeline, IDE_BUILD_PHASE_EXPORT, 0, stage);
  ide_build_pipeline_addin_track (IDE_BUILD_PIPELINE_ADDIN (self), stage_id);

  return TRUE;
}

static void
gbp_flatpak_pipeline_addin_load (IdeBuildPipelineAddin *addin,
                                 IdeBuildPipeline      *pipeline)
{
  GbpFlatpakPipelineAddin *self = (GbpFlatpakPipelineAddin *)addin;
  g_autoptr(GError) error = NULL;
  IdeConfiguration *config;
  IdeContext *context;
  IdeRuntime *runtime;

  g_assert (GBP_IS_FLATPAK_PIPELINE_ADDIN (self));
  g_assert (IDE_IS_BUILD_PIPELINE (pipeline));

  config = ide_build_pipeline_get_configuration (pipeline);
  runtime = ide_configuration_get_runtime (config);

  if (!GBP_IS_FLATPAK_RUNTIME (runtime))
    return;

  context = ide_object_get_context (IDE_OBJECT (self));

  if (!register_mkdirs_stage (self, pipeline, context, &error) ||
      !register_remotes_stage (self, pipeline, context, &error) ||
      !register_build_init_stage (self, pipeline, context, &error) ||
      !register_download_stage (self, pipeline, context, &error) ||
      !register_dependencies_stage (self, pipeline, context, &error) ||
      !register_build_finish_stage (self, pipeline, context, &error))
    g_warning ("%s", error->message);
}

/* GObject boilerplate */

static void
build_pipeline_addin_iface_init (IdeBuildPipelineAddinInterface *iface)
{
  iface->load = gbp_flatpak_pipeline_addin_load;
}

struct _GbpFlatpakPipelineAddin { IdeObject parent_instance; };

G_DEFINE_TYPE_WITH_CODE (GbpFlatpakPipelineAddin, gbp_flatpak_pipeline_addin, IDE_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (IDE_TYPE_BUILD_PIPELINE_ADDIN,
                                                build_pipeline_addin_iface_init))

static void
gbp_flatpak_pipeline_addin_class_init (GbpFlatpakPipelineAddinClass *klass)
{
}

static void
gbp_flatpak_pipeline_addin_init (GbpFlatpakPipelineAddin *self)
{
}
