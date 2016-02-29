/* ide-task.c
 *
 * Copyright (C) 2016 Ben Iofel <iofelben@gmail.com>
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

#include "ide-task.h"

/**
 * ide_task_return_object:
 * @task: a #GTask
 * @obj: (allow-none) (transfer full): the result
 * @destroy: (allow-none): a #GDestroyNotify function
 *
 * Wrapper for g_task_return_pointer since PyGObject doesn't support gpointer
 */
void ide_task_return_object (GTask          *task,
                             GObject        *obj,
                             GDestroyNotify  destroy)
{
  g_task_return_pointer (task, obj, destroy);
}
