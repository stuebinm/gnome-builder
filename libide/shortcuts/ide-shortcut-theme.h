/* ide-shortcut-theme.h
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

#ifndef IDE_SHORTCUT_THEME_H
#define IDE_SHORTCUT_THEME_H

#include <gtk/gtk.h>

#include "ide-shortcut-context.h"

G_BEGIN_DECLS

#define IDE_TYPE_SHORTCUT_THEME (ide_shortcut_theme_get_type())

G_DECLARE_DERIVABLE_TYPE (IdeShortcutTheme, ide_shortcut_theme, IDE, SHORTCUT_THEME, GObject)

struct _IdeShortcutThemeClass
{
  GObjectClass parent_class;

  gpointer _reserved1;
  gpointer _reserved2;
  gpointer _reserved3;
  gpointer _reserved4;
  gpointer _reserved5;
  gpointer _reserved6;
  gpointer _reserved7;
  gpointer _reserved8;
};

IdeShortcutTheme   *ide_shortcut_theme_new                  (const gchar        *name);
const gchar        *ide_shortcut_theme_get_name             (IdeShortcutTheme   *self);
IdeShortcutContext *ide_shortcut_theme_find_default_context (IdeShortcutTheme   *self,
                                                             GtkWidget          *widget);
IdeShortcutContext *ide_shortcut_theme_find_context_by_name (IdeShortcutTheme   *self,
                                                             const gchar        *name);
void                ide_shortcut_theme_add_context          (IdeShortcutTheme   *self,
                                                             IdeShortcutContext *context);

G_END_DECLS

#endif /* IDE_SHORTCUT_THEME_H */
