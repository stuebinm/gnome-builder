/* ide-build-pipeline.h
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

#ifndef IDE_BUILD_PIPELINE_H
#define IDE_BUILD_PIPELINE_H

#include <gio/gio.h>

#include "ide-types.h"

#include "buildsystem/ide-build-log.h"
#include "buildsystem/ide-build-stage.h"
#include "buildsystem/ide-configuration.h"
#include "subprocess/ide-subprocess-launcher.h"

G_BEGIN_DECLS

#define IDE_TYPE_BUILD_PIPELINE     (ide_build_pipeline_get_type())
#define IDE_BUILD_PHASE_MASK        (0xFFFFFF)
#define IDE_BUILD_PHASE_WHENCE_MASK (IDE_BUILD_PHASE_BEFORE | IDE_BUILD_PHASE_AFTER)

typedef enum
{
  IDE_BUILD_PHASE_NONE         = 0,
  IDE_BUILD_PHASE_PREPARE      = 1 << 0,
  IDE_BUILD_PHASE_DOWNLOADS    = 1 << 1,
  IDE_BUILD_PHASE_DEPENDENCIES = 1 << 2,
  IDE_BUILD_PHASE_AUTOGEN      = 1 << 3,
  IDE_BUILD_PHASE_CONFIGURE    = 1 << 4,
  IDE_BUILD_PHASE_BUILD        = 1 << 6,
  IDE_BUILD_PHASE_INSTALL      = 1 << 7,
  IDE_BUILD_PHASE_EXPORT       = 1 << 8,
  IDE_BUILD_PHASE_FINAL        = 1 << 9,
  IDE_BUILD_PHASE_BEFORE       = 1 << 28,
  IDE_BUILD_PHASE_AFTER        = 1 << 29,
  IDE_BUILD_PHASE_FINISHED     = 1 << 30,
  IDE_BUILD_PHASE_FAILED       = 1 << 31,
} IdeBuildPhase;

G_DECLARE_FINAL_TYPE (IdeBuildPipeline, ide_build_pipeline, IDE, BUILD_PIPELINE, IdeObject)

IdeConfiguration      *ide_build_pipeline_get_configuration   (IdeBuildPipeline       *self);
const gchar           *ide_build_pipeline_get_builddir        (IdeBuildPipeline       *self);
const gchar           *ide_build_pipeline_get_srcdir          (IdeBuildPipeline       *self);
IdeSubprocessLauncher *ide_build_pipeline_create_launcher     (IdeBuildPipeline       *self,
                                                               GError                **error);
gchar                 *ide_build_pipeline_build_srcdir_path   (IdeBuildPipeline       *self,
                                                               const gchar            *first_part,
                                                               ...) G_GNUC_NULL_TERMINATED;
gchar                 *ide_build_pipeline_build_builddir_path (IdeBuildPipeline       *self,
                                                               const gchar            *first_part,
                                                               ...) G_GNUC_NULL_TERMINATED;
void                   ide_build_pipeline_invalidate_phase    (IdeBuildPipeline       *self,
                                                               IdeBuildPhase           phases);
void                   ide_build_pipeline_request_phase       (IdeBuildPipeline       *self,
                                                               IdeBuildPhase           phase);
guint                  ide_build_pipeline_connect             (IdeBuildPipeline       *self,
                                                               IdeBuildPhase           phase,
                                                               gint                    priority,
                                                               IdeBuildStage          *stage);
guint                  ide_build_pipeline_connect_launcher    (IdeBuildPipeline       *self,
                                                               IdeBuildPhase           phase,
                                                               gint                    priority,
                                                               IdeSubprocessLauncher  *launcher);
void                   ide_build_pipeline_disconnect          (IdeBuildPipeline       *self,
                                                               guint                   stage_id);
IdeBuildStage         *ide_build_pipeline_get_stage_by_id     (IdeBuildPipeline       *self,
                                                               guint                   stage_id);
void                   ide_build_pipeline_execute_async       (IdeBuildPipeline       *self,
                                                               GCancellable           *cancellable,
                                                               GAsyncReadyCallback     callback,
                                                               gpointer                user_data);
gboolean               ide_build_pipeline_execute_finish      (IdeBuildPipeline       *self,
                                                               GAsyncResult           *result,
                                                               GError                **error);

G_END_DECLS

#endif /* IDE_BUILD_PIPELINE_H */
