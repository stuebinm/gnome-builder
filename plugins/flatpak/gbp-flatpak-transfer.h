/* gbp-flatpak-transfer.h
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

#ifndef GBP_FLATPAK_TRANSFER_H
#define GBP_FLATPAK_TRANSFER_H

#include <ide.h>

G_BEGIN_DECLS

#define GBP_TYPE_FLATPAK_TRANSFER (gbp_flatpak_transfer_get_type())

G_DECLARE_FINAL_TYPE (GbpFlatpakTransfer, gbp_flatpak_transfer, GBP, FLATPAK_TRANSFER, IdeObject)

GbpFlatpakTransfer *gbp_flatpak_transfer_new (IdeContext  *context,
                                              const gchar *id,
                                              const gchar *arch,
                                              const gchar *branch,
                                              gboolean     force_update);

G_END_DECLS

#endif /* GBP_FLATPAK_TRANSFER_H */
