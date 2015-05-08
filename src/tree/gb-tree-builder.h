/* gb-tree-builder.h
 *
 * Copyright (C) 2011 Christian Hergert <chris@dronelabs.com>
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

#ifndef GB_TREE_BUILDER_H
#define GB_TREE_BUILDER_H

#include <glib-object.h>

#include "gb-tree-node.h"

G_BEGIN_DECLS

#define GB_TYPE_TREE_BUILDER            (gb_tree_builder_get_type())

G_DECLARE_DERIVABLE_TYPE (GbTreeBuilder, gb_tree_builder, GB, TREE_BUILDER, GInitiallyUnowned)

struct _GbTreeBuilderClass
{
  GInitiallyUnownedClass parent_class;

  void     (*added)           (GbTreeBuilder *builder,
                               GtkWidget     *tree);
  void     (*removed)         (GbTreeBuilder *builder,
                               GtkWidget     *tree);
  void     (*build_node)      (GbTreeBuilder *builder,
                               GbTreeNode    *node);
  gboolean (*node_activated)  (GbTreeBuilder *builder,
                               GbTreeNode    *node);
  void     (*node_selected)   (GbTreeBuilder *builder,
                               GbTreeNode    *node);
  void     (*node_unselected) (GbTreeBuilder *builder,
                               GbTreeNode    *node);
  void     (*node_popup)      (GbTreeBuilder *builder,
                               GbTreeNode    *node,
                               GMenu         *menu);
};

GtkWidget *gb_tree_builder_get_tree        (GbTreeBuilder *builder);
void       gb_tree_builder_build_node      (GbTreeBuilder *builder,
                                            GbTreeNode    *node);
gboolean   gb_tree_builder_node_activated  (GbTreeBuilder *builder,
                                            GbTreeNode    *node);
void       gb_tree_builder_node_popup      (GbTreeBuilder *builder,
                                            GbTreeNode    *node,
                                            GMenu         *menu);
void       gb_tree_builder_node_selected   (GbTreeBuilder *builder,
                                            GbTreeNode    *node);
void       gb_tree_builder_node_unselected (GbTreeBuilder *builder,
                                            GbTreeNode    *node);

G_END_DECLS

#endif /* GB_TREE_BUILDER_H */
