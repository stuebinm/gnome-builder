/* ide-build-pipeline.c
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

#define G_LOG_DOMAIN "ide-build-pipeline"

#include <libpeas/peas.h>

#include "ide-context.h"
#include "ide-debug.h"
#include "ide-enums.h"

#include "buildsystem/ide-build-log.h"
#include "buildsystem/ide-build-log-private.h"
#include "buildsystem/ide-build-pipeline.h"
#include "buildsystem/ide-build-pipeline-addin.h"
#include "buildsystem/ide-build-stage-launcher.h"
#include "projects/ide-project.h"
#include "runtimes/ide-runtime.h"
#include "vcs/ide-vcs.h"

/**
 * SECTION:ide-build-pipeline
 * @title: IdeBuildPipeline
 * @short_description: Pluggable build pipeline
 *
 * The #IdeBuildPipeline is responsible for managing the build process
 * for Builder. It consists of multiple build "phases" (see #IdeBuildPhase
 * for the individual phases). An #IdeBuildStage can be attached with
 * a priority to each phase and is the primary mechanism that plugins
 * use to perform their operations in the proper ordering.
 *
 * For example, the flatpak plugin provides its download stage as part of the
 * %IDE_BUILD_PHASE_DOWNLOAD phase. The autotools plugin provides stages to
 * phases such as %IDE_BUILD_PHASE_AUTOGEN, %IDE_BUILD_PHASE_CONFIGURE,
 * %IDE_BUILD_PHASE_BUILD, and %IDE_BUILD_PHASE_INSTALL.
 *
 * If you want ensure a particular phase is performed as part of a build,
 * then fall ide_build_pipeline_request_phase() with the phase you are
 * interested in seeing complete successfully.
 *
 * If your plugin has discovered that something has changed that invalidates a
 * given phase, use ide_build_pipeline_invalidate_phase() to ensure that the
 * phase is re-executed the next time a requested phase of higher precidence
 * is requested.
 *
 * It can be useful to perform operations before or after a given stage (but
 * still be executed as part of that stage) so the %IDE_BUILD_PHASE_BEFORE and
 * %IDE_BUILD_PHASE_AFTER flags may be xor'd with the requested phase.  If more
 * precise ordering is required, you may use the priority parameter to order
 * the operation with regards to other stages in that phase.
 *
 * Transient stages may be added to the pipeline and they will be removed after
 * the ide_build_pipeline_execute_async() operation has completed successfully
 * or has failed. You can mark a stage as trandient with
 * ide_build_stage_set_transient(). This may be useful to perform operations
 * such as an "export tarball" stage which should only run once as determined
 * by the user requesting a "make dist" style operation.
 */

typedef struct
{
  guint          id;
  IdeBuildPhase  phase;
  gint           priority;
  IdeBuildStage *stage;
} PipelineEntry;

struct _IdeBuildPipeline
{
  IdeObject         parent_instance;
  PeasExtensionSet *addins;
  IdeConfiguration *configuration;
  IdeBuildLog      *log;
  gchar            *builddir;
  gchar            *srcdir;
  GArray           *pipeline;
  gint              position;
  IdeBuildPhase     requested_mask;
  guint             seqnum;
  guint             failed : 1;
};

static void ide_build_pipeline_tick (IdeBuildPipeline *self,
                                     GTask            *task);

G_DEFINE_TYPE (IdeBuildPipeline, ide_build_pipeline, IDE_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_CONFIGURATION,
  PROP_PHASE,
  N_PROPS
};

enum {
  STARTED,
  FINISHED,
  N_SIGNALS
};

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];

static void
ide_build_pipeline_release_transients (IdeBuildPipeline *self)
{
  g_assert (IDE_IS_BUILD_PIPELINE (self));

  for (gint i = self->pipeline->len; i >= 0; i--)
    {
      const PipelineEntry *entry = &g_array_index (self->pipeline, PipelineEntry, i);

      if (ide_build_stage_get_transient (entry->stage))
        g_array_remove_index (self->pipeline, i);
    }
}

/**
 * ide_build_pipeline_get_phase:
 *
 * Gets the current phase that is executing. This is only useful during
 * execution of the pipeline.
 */
IdeBuildPhase
ide_build_pipeline_get_phase (IdeBuildPipeline *self)
{
  g_return_val_if_fail (IDE_IS_BUILD_PIPELINE (self), 0);

  if (self->position < 0)
    return IDE_BUILD_PHASE_NONE;
  else if (self->position < self->pipeline->len)
    return g_array_index (self->pipeline, PipelineEntry, self->position).phase;
  else if (self->failed)
    return IDE_BUILD_PHASE_FAILED;
  else
    return IDE_BUILD_PHASE_FINISHED;
}

/**
 * ide_build_pipeline_get_configuration:
 *
 * Gets the #IdeConfiguration to use for the pipeline.
 *
 * Returns: (transfer none): An #IdeConfiguration
 */
IdeConfiguration *
ide_build_pipeline_get_configuration (IdeBuildPipeline *self)
{
  g_return_val_if_fail (IDE_IS_BUILD_PIPELINE (self), NULL);

  return self->configuration;
}

static void
clear_pipeline_entry (gpointer data)
{
  PipelineEntry *entry = data;

  g_clear_object (&entry->stage);
}

static gint
pipeline_entry_compare (gconstpointer a,
                        gconstpointer b)
{
  const PipelineEntry *entry_a = a;
  const PipelineEntry *entry_b = b;
  gint ret;

  ret = (gint)(entry_a->phase & IDE_BUILD_PHASE_MASK)
      - (gint)(entry_b->phase & IDE_BUILD_PHASE_MASK);

  if (ret == 0)
    {
      gint whence_a = (entry_a->phase & IDE_BUILD_PHASE_WHENCE_MASK);
      gint whence_b = (entry_b->phase & IDE_BUILD_PHASE_WHENCE_MASK);

      if (whence_a != whence_b)
        {
          if (whence_a == IDE_BUILD_PHASE_BEFORE)
            return -1;

          if (whence_b == IDE_BUILD_PHASE_BEFORE)
            return 1;

          if (whence_a == 0)
            return -1;

          if (whence_b == 0)
            return 1;

          g_assert_not_reached ();
        }
    }

  if (ret == 0)
    ret = entry_a->priority - entry_b->priority;

  return ret;
}

static void
ide_build_pipeline_real_started (IdeBuildPipeline *self)
{
  g_assert (IDE_IS_BUILD_PIPELINE (self));
}

static void
ide_build_pipeline_real_finished (IdeBuildPipeline *self,
                                  gboolean          failed)
{
  g_assert (IDE_IS_BUILD_PIPELINE (self));

  /*
   * Now that the build is finished, we can aggressively drop our pipeline
   * stages to help ensure that all references are dropped as soon as
   * possible.
   */

  g_clear_object (&self->addins);
}

static void
ide_build_pipeline_extension_added (PeasExtensionSet *set,
                                    PeasPluginInfo   *plugin_info,
                                    PeasExtension    *exten,
                                    gpointer          user_data)
{
  IdeBuildPipeline *self = user_data;
  IdeBuildPipelineAddin *addin = (IdeBuildPipelineAddin *)exten;

  IDE_ENTRY;

  g_assert (PEAS_IS_EXTENSION_SET (set));
  g_assert (plugin_info != NULL);
  g_assert (IDE_IS_BUILD_PIPELINE_ADDIN (addin));
  g_assert (IDE_IS_BUILD_PIPELINE (self));

  ide_build_pipeline_addin_load (addin, self);

  IDE_EXIT;
}

static void
ide_build_pipeline_extension_removed (PeasExtensionSet *set,
                                      PeasPluginInfo   *plugin_info,
                                      PeasExtension    *exten,
                                      gpointer          user_data)
{
  IdeBuildPipeline *self = user_data;
  IdeBuildPipelineAddin *addin = (IdeBuildPipelineAddin *)exten;

  IDE_ENTRY;

  g_assert (PEAS_IS_EXTENSION_SET (set));
  g_assert (plugin_info != NULL);
  g_assert (IDE_IS_BUILD_PIPELINE_ADDIN (addin));
  g_assert (IDE_IS_BUILD_PIPELINE (self));

  ide_build_pipeline_addin_unload (addin, self);

  IDE_EXIT;
}

static void
ide_build_pipeline_finalize (GObject *object)
{
  IdeBuildPipeline *self = (IdeBuildPipeline *)object;

  g_clear_object (&self->log);
  g_clear_object (&self->configuration);
  g_clear_pointer (&self->pipeline, g_array_unref);
  g_clear_pointer (&self->srcdir, g_free);
  g_clear_pointer (&self->builddir, g_free);

  G_OBJECT_CLASS (ide_build_pipeline_parent_class)->finalize (object);
}

static void
ide_build_pipeline_dispose (GObject *object)
{
  IdeBuildPipeline *self = IDE_BUILD_PIPELINE (object);

  g_clear_object (&self->addins);

  G_OBJECT_CLASS (ide_build_pipeline_parent_class)->dispose (object);
}

static void
ide_build_pipeline_constructed (GObject *object)
{
  IdeBuildPipeline *self = IDE_BUILD_PIPELINE (object);
  g_autofree gchar *builddir = NULL;
  const gchar *config_id;
  const gchar *project_id;
  IdeProject *project;
  IdeContext *context;
  IdeVcs *vcs;
  GFile *workdir;

  G_OBJECT_CLASS (ide_build_pipeline_parent_class)->constructed (object);

  context = ide_object_get_context (IDE_OBJECT (self));

  project = ide_context_get_project (context);
  project_id = ide_project_get_id (project);

  vcs = ide_context_get_vcs (context);
  workdir = ide_vcs_get_working_directory (vcs);

  config_id = ide_configuration_get_id (self->configuration);

  self->srcdir = g_file_get_path (workdir);

  self->builddir = g_build_filename (g_get_user_cache_dir (),
                                     "gnome-builder",
                                     "builds",
                                     project_id,
                                     config_id,
                                     NULL);

  self->addins = peas_extension_set_new (peas_engine_get_default (),
                                         IDE_TYPE_BUILD_PIPELINE_ADDIN,
                                         "context", context,
                                         NULL);

  g_signal_connect (self->addins,
                    "extension-added",
                    G_CALLBACK (ide_build_pipeline_extension_added),
                    self);

  g_signal_connect (self->addins,
                    "extension-removed",
                    G_CALLBACK (ide_build_pipeline_extension_removed),
                    self);

  peas_extension_set_foreach (self->addins,
                              ide_build_pipeline_extension_added,
                              self);
}

static void
ide_build_pipeline_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  IdeBuildPipeline *self = IDE_BUILD_PIPELINE (object);

  switch (prop_id)
    {
    case PROP_CONFIGURATION:
      g_value_set_object (value, ide_build_pipeline_get_configuration (self));
      break;

    case PROP_PHASE:
      g_value_set_flags (value, ide_build_pipeline_get_phase (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_build_pipeline_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  IdeBuildPipeline *self = IDE_BUILD_PIPELINE (object);

  switch (prop_id)
    {
    case PROP_CONFIGURATION:
      self->configuration = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_build_pipeline_class_init (IdeBuildPipelineClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ide_build_pipeline_constructed;
  object_class->dispose = ide_build_pipeline_dispose;
  object_class->finalize = ide_build_pipeline_finalize;
  object_class->get_property = ide_build_pipeline_get_property;
  object_class->set_property = ide_build_pipeline_set_property;

  /**
   * IdeBuildPipeline:configuration:
   *
   * The configuration to use for the build pipeline.
   */
  properties [PROP_CONFIGURATION] =
    g_param_spec_object ("configuration",
                         "Configuration",
                         "Configuration",
                         IDE_TYPE_CONFIGURATION,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  /**
   * IdeBuildPipeline:phase:
   *
   * The current build phase during execution of the pipeline.
   */
  properties [PROP_PHASE] =
    g_param_spec_flags ("phase",
                        "Phase",
                        "The phase that is being executed",
                        IDE_TYPE_BUILD_PHASE,
                        IDE_BUILD_PHASE_NONE,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * IdeBuildPipeline::started:
   * @self: An @IdeBuildPipeline
   *
   * This signal is emitted when the pipeline has started executing in
   * response to ide_build_pipeline_execute_async() being called.
   */
  signals [STARTED] =
    g_signal_new_class_handler ("started",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST,
                                G_CALLBACK (ide_build_pipeline_real_started),
                                NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * IdeBuildPipeline::finished:
   * @self: An #IdeBuildPipeline
   * @failed: If the build was a failure
   *
   * This signal is emitted when the build process has finished executing.
   * If the build failed to complete all requested stages, then @failed will
   * be set to %TRUE, otherwise %FALSE.
   */
  signals [FINISHED] =
    g_signal_new_class_handler ("finished",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST,
                                G_CALLBACK (ide_build_pipeline_real_finished),
                                NULL, NULL, NULL,
                                G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
ide_build_pipeline_init (IdeBuildPipeline *self)
{
  self->position = -1;

  self->pipeline = g_array_new (FALSE, FALSE, sizeof (PipelineEntry));
  g_array_set_clear_func (self->pipeline, clear_pipeline_entry);

  self->log = ide_build_log_new ();
}

static void
ide_build_pipeline_stage_execute_cb (GObject      *object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  IdeBuildStage *stage = (IdeBuildStage *)object;
  IdeBuildPipeline *self;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (IDE_IS_BUILD_STAGE (stage));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (IDE_IS_BUILD_PIPELINE (self));

  if (!ide_build_stage_execute_finish (stage, result, &error))
    {
      self->failed = TRUE;
      g_task_return_error (task, g_steal_pointer (&error));
      ide_build_pipeline_release_transients (self);
      return;
    }

  ide_build_stage_set_completed (stage, TRUE);

  ide_build_pipeline_tick (self, task);
}

static void
ide_build_pipeline_tick (IdeBuildPipeline *self,
                         GTask            *task)
{
  GCancellable *cancellable;

  IDE_ENTRY;

  g_assert (IDE_IS_BUILD_PIPELINE (self));
  g_assert (G_IS_TASK (task));

  cancellable = g_task_get_cancellable (task);

  for (self->position++; self->position < self->pipeline->len; self->position++)
    {
      const PipelineEntry *entry = &g_array_index (self->pipeline, PipelineEntry, self->position);

      g_assert (entry->stage != NULL);
      g_assert (IDE_IS_BUILD_STAGE (entry->stage));

      if (ide_build_stage_get_completed (entry->stage))
        continue;

      if ((entry->phase & IDE_BUILD_PHASE_MASK) & self->requested_mask)
        {
          ide_build_stage_execute_async (entry->stage,
                                         self,
                                         cancellable,
                                         ide_build_pipeline_stage_execute_cb,
                                         g_object_ref (task));
          g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PHASE]);
          IDE_EXIT;
        }
    }

  g_task_return_boolean (task, TRUE);

  ide_build_pipeline_release_transients (self);

  IDE_EXIT;
}

static void
ide_build_pipeline_notify_completed (IdeBuildPipeline *self,
                                     GParamSpec       *pspec,
                                     GTask            *task)
{
  IDE_ENTRY;

  g_assert (IDE_IS_BUILD_PIPELINE (self));
  g_assert (G_IS_TASK (task));

  g_signal_emit (self, signals [FINISHED], 0, self->failed);

  IDE_EXIT;
}

/**
 * ide_build_pipeline_execute_async:
 * @self: A @IdeBuildPipeline
 * @cancellable: (nullable): A #GCancellable or %NULL
 * @callback: a callback to execute upon completion
 * @user_data: data for @callback
 *
 * Asynchronously starts the build pipeline.
 *
 * Any phase that has been invalidated up to the requested phase
 * will be executed until a stage has failed.
 *
 * Upon completion, @callback will be executed and should call
 * ide_build_pipeline_execute_finish() to get the status of the
 * operation.
 */
void
ide_build_pipeline_execute_async (IdeBuildPipeline    *self,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(GFile) builddir = NULL;
  g_autoptr(GError) error = NULL;

  IDE_ENTRY;

  g_return_if_fail (IDE_IS_BUILD_PIPELINE (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  self->failed = FALSE;

  g_signal_emit (self, signals [STARTED], 0);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, ide_build_pipeline_execute_async);

  g_signal_connect_object (task,
                           "notify::completed",
                           G_CALLBACK (ide_build_pipeline_notify_completed),
                           self,
                           G_CONNECT_SWAPPED);

  /*
   * Before we make any progoress, ensure the build directory is created
   * so that pipeline stages need not worry about it. We'll just do this
   * synchronously because if we can't do directory creation fast, well
   * then we are pretty much screwed anyway.
   */

  builddir = g_file_new_for_path (self->builddir);

  if (!g_file_make_directory_with_parents (builddir, cancellable, &error) &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  ide_build_pipeline_tick (self, task);

  IDE_EXIT;
}

/**
 * ide_build_pipeline_execute_finish:
 *
 * Returns: %TRUE if successful; otherwise %FALSE and @error is set.
 */
gboolean
ide_build_pipeline_execute_finish (IdeBuildPipeline  *self,
                                   GAsyncResult      *result,
                                   GError           **error)
{
  gboolean ret;

  IDE_ENTRY;

  g_return_val_if_fail (IDE_IS_BUILD_PIPELINE (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  ret = g_task_propagate_boolean (G_TASK (result), error);

  IDE_RETURN (ret);
}

/**
 * ide_build_pipeline_connect:
 * @self: A #IdeBuildPipeline
 * @phase: An #IdeBuildPhase
 * @priority: an optional priority for sorting within the phase
 * @stage: An #IdeBuildStage
 *
 * Insert @stage into the pipeline as part of the phase denoted by @phase.
 *
 * If priority is non-zero, it will be used to sort the stage among other
 * stages that are part of the same phase.
 *
 * Returns: A stage_id that may be passed to ide_build_pipeline_disconnect().
 */
guint
ide_build_pipeline_connect (IdeBuildPipeline *self,
                            IdeBuildPhase     phase,
                            gint              priority,
                            IdeBuildStage    *stage)
{
  GFlagsClass *klass;
  guint ret = 0;

  IDE_ENTRY;

  g_return_val_if_fail (IDE_IS_BUILD_PIPELINE (self), 0);
  g_return_val_if_fail (IDE_IS_BUILD_STAGE (stage), 0);
  g_return_val_if_fail ((phase & IDE_BUILD_PHASE_MASK) != IDE_BUILD_PHASE_NONE, 0);
  g_return_val_if_fail ((phase & IDE_BUILD_PHASE_WHENCE_MASK) == 0 ||
                        (phase & IDE_BUILD_PHASE_WHENCE_MASK) == IDE_BUILD_PHASE_BEFORE ||
                        (phase & IDE_BUILD_PHASE_WHENCE_MASK) == IDE_BUILD_PHASE_AFTER, 0);

  if G_UNLIKELY (self->position != -1)
    {
      g_warning ("Cannot insert stage into pipeline after execution, ignoring");
      IDE_RETURN (0);
    }

  klass = g_type_class_ref (IDE_TYPE_BUILD_PHASE);

  for (guint i = 0; i < klass->n_values; i++)
    {
      const GFlagsValue *value = &klass->values[i];

      if ((phase & IDE_BUILD_PHASE_MASK) == value->value)
        {
          PipelineEntry entry = { 0 };

          IDE_TRACE_MSG ("Adding stage to pipeline with phase %s and priority %d",
                         value->value_nick, priority);

          entry.id = ++self->seqnum;
          entry.phase = phase;
          entry.priority = priority;
          entry.stage = g_object_ref (stage);

          g_array_append_val (self->pipeline, entry);
          g_array_sort (self->pipeline, pipeline_entry_compare);

          ret = entry.id;

          ide_build_stage_set_log_observer (stage,
                                            ide_build_log_observer,
                                            g_object_ref (self->log),
                                            g_object_unref);

          IDE_GOTO (cleanup);
        }
    }

  g_warning ("No such pipeline phase %02x", phase);

cleanup:
  g_type_class_unref (klass);

  IDE_RETURN (ret);
}

/**
 * ide_build_pipeline_connect_launcher:
 * @self: A #IdeBuildPipeline
 * @phase: An #IdeBuildPhase
 * @priority: an optional priority for sorting within the phase
 * @launcher: An #IdeSubprocessLauncher
 *
 * This creates a new stage that will spawn a process using @launcher and log
 * the output of stdin/stdout.
 *
 * It is a programmer error to modify @launcher after passing it to this
 * function.
 *
 * Returns: A stage_id that may be passed to ide_build_pipeline_remove().
 */
guint
ide_build_pipeline_connect_launcher (IdeBuildPipeline      *self,
                                     IdeBuildPhase          phase,
                                     gint                   priority,
                                     IdeSubprocessLauncher *launcher)
{
  g_autoptr(IdeBuildStage) stage = NULL;
  IdeContext *context;

  g_return_val_if_fail (IDE_IS_BUILD_PIPELINE (self), 0);
  g_return_val_if_fail ((phase & IDE_BUILD_PHASE_MASK) != IDE_BUILD_PHASE_NONE, 0);
  g_return_val_if_fail ((phase & IDE_BUILD_PHASE_WHENCE_MASK) == 0 ||
                        (phase & IDE_BUILD_PHASE_WHENCE_MASK) == IDE_BUILD_PHASE_BEFORE ||
                        (phase & IDE_BUILD_PHASE_WHENCE_MASK) == IDE_BUILD_PHASE_AFTER, 0);

  context = ide_object_get_context (IDE_OBJECT (self));
  stage = ide_build_stage_launcher_new (context, launcher);

  return ide_build_pipeline_connect (self, phase, priority, stage);
}

/**
 * ide_build_pipeline_request_phase:
 * @self: An #IdeBuildPipeline
 * @phase: An #IdeBuildPhase
 *
 * Requests that the next execution of the pipeline will build up to @phase
 * including all stages that were previously invalidated.
 */
void
ide_build_pipeline_request_phase (IdeBuildPipeline *self,
                                  IdeBuildPhase     phase)
{
  GFlagsClass *klass;

  IDE_ENTRY;

  g_return_if_fail (IDE_IS_BUILD_PIPELINE (self));
  g_return_if_fail ((phase & IDE_BUILD_PHASE_MASK) != IDE_BUILD_PHASE_NONE);

  /*
   * You can only request basic phases. That does not include modifiers
   * like BEFORE, AFTER, FAILED, FINISHED.
   */
  phase &= IDE_BUILD_PHASE_MASK;

  if (self->position != -1)
    {
      g_warning ("Cannot request phase after execution has started");
      IDE_EXIT;
    }

  klass = g_type_class_ref (IDE_TYPE_BUILD_PHASE);

  for (guint i = 0; i < klass->n_values; i++)
    {
      const GFlagsValue *value = &klass->values[i];

      if (phase == value->value)
        {
          IDE_TRACE_MSG ("requesting pipeline phase %s", value->value_nick);
          /*
           * Each flag is a power of two, so we can simply subtract one
           * to get a mask of all the previous phases.
           */
          self->requested_mask |= phase | (phase - 1);
          IDE_GOTO (cleanup);
        }
    }

  g_warning ("No such phase %02x", (guint)phase);

cleanup:
  g_type_class_unref (klass);

  IDE_EXIT;
}

/**
 * ide_build_pipeline_get_builddir:
 * @self: An #IdeBuildPipeline
 *
 * Gets the "builddir" to be used for the build process. This is generally
 * the location that build systems will use for out-of-tree builds.
 *
 * Returns: the path of the build directory
 */
const gchar *
ide_build_pipeline_get_builddir (IdeBuildPipeline *self)
{
  g_return_val_if_fail (IDE_IS_BUILD_PIPELINE (self), NULL);

  return self->builddir;
}

/**
 * ide_build_pipeline_get_srcdir:
 * @self: An #IdeBuildPipeline
 *
 * Gets the "srcdir" of the project. This is equivalent to the
 * IdeVcs:working-directory property as a string.
 *
 * Returns: the path of the source directory
 */
const gchar *
ide_build_pipeline_get_srcdir (IdeBuildPipeline *self)
{
  g_return_val_if_fail (IDE_IS_BUILD_PIPELINE (self), NULL);

  return self->srcdir;
}

static gchar *
ide_build_pipeline_build_path_va_list (const gchar *prefix,
                                       const gchar *first_part,
                                       va_list      args)
{
  g_autoptr(GPtrArray) ar = NULL;

  g_assert (prefix != NULL);
  g_assert (first_part != NULL);

  ar = g_ptr_array_new ();
  g_ptr_array_add (ar, (gchar *)prefix);
  do
    g_ptr_array_add (ar, (gchar *)first_part);
  while (NULL != (first_part = va_arg (args, const gchar *)));
  g_ptr_array_add (ar, NULL);

  return g_build_filenamev ((gchar **)ar->pdata);
}

/**
 * ide_build_pipeline_build_srcdir_path:
 *
 * This is a convenience function to create a new path that starts with
 * the source directory of the project.
 *
 * This is functionally equivalent to calling g_build_filename() with the
 * working directory of the source tree.
 *
 * Returns: (transfer full): A newly allocated string.
 */
gchar *
ide_build_pipeline_build_srcdir_path (IdeBuildPipeline *self,
                                      const gchar      *first_part,
                                      ...)
{
  gchar *ret;
  va_list args;

  g_return_val_if_fail (IDE_IS_BUILD_PIPELINE (self), NULL);
  g_return_val_if_fail (first_part != NULL, NULL);

  va_start (args, first_part);
  ret = ide_build_pipeline_build_path_va_list (self->srcdir, first_part, args);
  va_end (args);

  return ret;
}

/**
 * ide_build_pipeline_build_builddir_path:
 *
 * This is a convenience function to create a new path that starts with
 * the build directory for this build configuration.
 *
 * This is functionally equivalent to calling g_build_filename() with the
 * result of ide_build_pipeline_get_builddir() as the first parameter.
 *
 * Returns: (transfer full): A newly allocated string.
 */
gchar *
ide_build_pipeline_build_builddir_path (IdeBuildPipeline *self,
                                        const gchar      *first_part,
                                        ...)
{
  gchar *ret;
  va_list args;

  g_return_val_if_fail (IDE_IS_BUILD_PIPELINE (self), NULL);
  g_return_val_if_fail (first_part != NULL, NULL);

  va_start (args, first_part);
  ret = ide_build_pipeline_build_path_va_list (self->builddir, first_part, args);
  va_end (args);

  return ret;
}

/**
 * ide_build_pipeline_disconnect:
 * @self: An #IdeBuildPipeline
 * @stage_id: An identifier returned from adding a stage
 *
 * This removes the stage matching @stage_id. You are returned a @stage_id when
 * inserting a stage with functions such as ide_build_pipeline_connect()
 * or ide_build_pipeline_connect_launcher().
 *
 * Plugins should use this function to remove their stages when the plugin
 * is unloading.
 */
void
ide_build_pipeline_disconnect (IdeBuildPipeline *self,
                               guint             stage_id)
{
  g_return_if_fail (IDE_IS_BUILD_PIPELINE (self));
  g_return_if_fail (self->pipeline != NULL);
  g_return_if_fail (stage_id != 0);

  for (guint i = 0; i < self->pipeline->len; i++)
    {
      const PipelineEntry *entry = &g_array_index (self->pipeline, PipelineEntry, i);

      if (entry->id == stage_id)
        {
          g_array_remove_index (self->pipeline, i);
          break;
        }
    }
}

/**
 * ide_build_pipeline_invalidate_phase:
 * @self: An #IdeBuildPipeline
 * @phases: The phases to invalidate
 *
 * Invalidates the phases matching @phases flags.
 *
 * If the requested phases include the phases invalidated here, the next
 * execution of the pipeline will execute thse phases.
 *
 * This should be used by plugins to ensure a particular phase is re-executed
 * upon discovering its state is no longer valid. Such an example might be
 * invalidating the %IDE_BUILD_PHASE_AUTOGEN phase when the an autotools
 * projects autogen.sh file has been changed.
 */
void
ide_build_pipeline_invalidate_phase (IdeBuildPipeline *self,
                                     IdeBuildPhase     phases)
{
  g_return_if_fail (IDE_IS_BUILD_PIPELINE (self));

  for (guint i = 0; i < self->pipeline->len; i++)
    {
      const PipelineEntry *entry = &g_array_index (self->pipeline, PipelineEntry, i);

      if ((entry->phase & IDE_BUILD_PHASE_MASK) & phases)
        ide_build_stage_set_completed (entry->stage, FALSE);
    }
}

/**
 * ide_build_pipeline_get_stage_by_id:
 * @self: An #IdeBuildPipeline
 * @stage_id: the identfier of the stage
 *
 * Gets the stage matching the identifier @stage_id as returned from
 * ide_build_pipeline_connect().
 *
 * Returns: (transfer none) (nullable): An #IdeBuildStage or %NULL if the
 *   stage could not be found.
 */
IdeBuildStage *
ide_build_pipeline_get_stage_by_id (IdeBuildPipeline *self,
                                    guint             stage_id)
{
  g_return_val_if_fail (IDE_IS_BUILD_PIPELINE (self), NULL);

  for (guint i = 0; i < self->pipeline->len; i++)
    {
      const PipelineEntry *entry = &g_array_index (self->pipeline, PipelineEntry, i);

      if (entry->id == stage_id)
        return entry->stage;
    }

  return NULL;
}

/**
 * ide_build_pipeline_create_launcher:
 * @self: An #IdeBuildPipeline
 *
 * This is a convenience function to create a new #IdeSubprocessLauncher
 * using the configuration and runtime associated with the pipeline.
 *
 * Returns: (transfer full): An #IdeSubprocessLauncher.
 */
IdeSubprocessLauncher *
ide_build_pipeline_create_launcher (IdeBuildPipeline  *self,
                                    GError           **error)
{
  IdeRuntime *runtime;

  g_return_val_if_fail (IDE_IS_BUILD_PIPELINE (self), NULL);

  runtime = ide_configuration_get_runtime (self->configuration);

  if (runtime == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "The runtime %s is missing",
                   ide_configuration_get_runtime_id (self->configuration));
      return NULL;
    }

  return ide_runtime_create_launcher (runtime, error);
}
