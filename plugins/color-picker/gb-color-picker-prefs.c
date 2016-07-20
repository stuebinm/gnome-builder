/* gb-color-picker-prefs.c
 *
 * Copyright (C) 2016 Sebastien Lafargue <slafargue@gnome.org>
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

#include <glib/gi18n.h>

#include "preferences/ide-preferences-switch.h"
#include "ide.h"

#include "gb-color-picker-prefs.h"
#include "gb-color-picker-prefs-list.h"
#include "gb-color-picker-prefs-palette-list.h"
#include "gb-color-picker-prefs-palette-row.h"
#include "gb-color-picker-workbench-addin.h"
#include "gb-color-picker-workbench-addin-private.h"

struct _GbColorPickerPrefs
{
  GObject                         parent_instance;

  GtkWidget                      *components_page;
  GtkWidget                      *color_strings_page;
  GtkWidget                      *palettes_page;
  GtkWidget                      *palettes_list_page;

  GstyleColorPanel               *panel;
  GbColorPickerWorkbenchAddin    *addin;
  GstylePaletteWidget            *palette_widget;
  GListStore                     *palettes_store;
  GbColorPickerPrefsPaletteList  *palettes_box;
  GtkListBox                     *palettes_listbox;

  GtkWidget                      *load_palette_button;
  GtkWidget                      *save_palette_button;
  GtkWidget                      *generate_palette_button;
  GtkWidget                      *preview;
  GtkWidget                      *preview_placeholder;
  GtkWidget                      *preview_title;
  GtkWidget                      *preview_palette_widget;

  GtkFileFilter                  *all_files_filter;
  GtkFileFilter                  *gstyle_files_filter;
  GtkFileFilter                  *gpl_files_filter;
  GtkFileFilter                  *builder_files_filter;

  GSettings                      *plugin_settings;
  GSettings                      *components_settings;
};

G_DEFINE_TYPE (GbColorPickerPrefs, gb_color_picker_prefs, G_TYPE_OBJECT)

enum
{
  PROP_0,
  PROP_ADDIN,
  PROP_PANEL,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

GtkWidget *
gb_color_picker_prefs_get_page (GbColorPickerPrefs    *self,
                                GstyleColorPanelPrefs  prefs_type)
{
  g_return_val_if_fail (GB_IS_COLOR_PICKER_PREFS (self), NULL);

  if (prefs_type == GSTYLE_COLOR_PANEL_PREFS_COMPONENTS)
    return self->components_page;
  else if (prefs_type == GSTYLE_COLOR_PANEL_PREFS_COLOR_STRINGS)
    return self->color_strings_page;
  else if (prefs_type == GSTYLE_COLOR_PANEL_PREFS_PALETTES)
    return self->palettes_page;
  else if (prefs_type == GSTYLE_COLOR_PANEL_PREFS_PALETTES_LIST)
    return self->palettes_list_page;
  else
    g_assert_not_reached ();
}

static GVariant *
string_to_variant (const gchar *str)
{
  GVariant *variant;
  g_autoptr(GError) error = NULL;

  g_assert (!ide_str_empty0 (str));

  variant = g_variant_parse (NULL, str, NULL, NULL, &error);
  if (variant == NULL)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      return NULL;
    }

  g_variant_ref_sink (variant);
  return variant;
}

static void
palette_update_preview_cb (GbColorPickerPrefs *self,
                           GtkDialog          *dialog)
{
  g_autoptr (GFile) file = NULL;
  GstylePalette *palette;

  g_assert (GB_IS_COLOR_PICKER_PREFS (self));
  g_assert (GTK_IS_DIALOG (dialog));

  gstyle_palette_widget_remove_all (GSTYLE_PALETTE_WIDGET (self->preview_palette_widget));
  file = gtk_file_chooser_get_preview_file (GTK_FILE_CHOOSER (dialog));
  if (file != NULL)
    {
      palette = gstyle_palette_new_from_file (file, NULL, NULL);
      if (palette != NULL)
        {
          gstyle_palette_widget_add (GSTYLE_PALETTE_WIDGET (self->preview_palette_widget), palette);
          gstyle_palette_widget_show_palette (GSTYLE_PALETTE_WIDGET (self->preview_palette_widget), palette);
          gtk_label_set_text (GTK_LABEL (self->preview_title), gstyle_palette_get_name (palette));

          return;
        }
    }

  gtk_label_set_text (GTK_LABEL (self->preview_title), "");
}

static void
palette_dialog_add_preview (GbColorPickerPrefs *self,
                            GtkWidget          *dialog)
{
  gtk_file_chooser_set_preview_widget (GTK_FILE_CHOOSER (dialog), self->preview);
  gtk_file_chooser_set_use_preview_label (GTK_FILE_CHOOSER (dialog), FALSE);

  g_signal_connect_object (dialog, "update-preview", G_CALLBACK (palette_update_preview_cb), self, G_CONNECT_SWAPPED);
}

static void
file_dialog_add_filters (GbColorPickerPrefs *self,
                         GtkWidget          *dialog)
{
  g_assert (GB_IS_COLOR_PICKER_PREFS (self));

  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), self->all_files_filter);
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), self->gstyle_files_filter);
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), self->gpl_files_filter);
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), self->builder_files_filter);
}

static GtkWidget *
create_file_load_dialog (GbColorPickerPrefs   *self)
{
  GtkWindow *toplevel;
  GtkWidget *dialog;

  g_assert (GB_IS_COLOR_PICKER_PREFS (self));
  g_assert (self->panel != NULL && GSTYLE_IS_COLOR_PANEL (self->panel));

  toplevel = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self->panel)));
  dialog = gtk_file_chooser_dialog_new (_("Load palette"),
                                        toplevel,
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        _("Open"),
                                        GTK_RESPONSE_OK,
                                        NULL);

  gtk_dialog_add_button (GTK_DIALOG (dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  file_dialog_add_filters (self, dialog);
  palette_dialog_add_preview (self, dialog);

  return dialog;
}

static GtkWidget *
create_file_save_dialog (GbColorPickerPrefs   *self,
                         GstylePalette        *palette)
{
  GtkWindow *toplevel;
  GtkWidget *dialog;
  g_autofree gchar *name = NULL;

  g_assert (GB_IS_COLOR_PICKER_PREFS (self));
  g_assert (self->panel != NULL && GSTYLE_IS_COLOR_PANEL (self->panel));

  toplevel = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self->panel)));
  dialog = gtk_file_chooser_dialog_new (_("Save palette"),
                                        toplevel,
                                        GTK_FILE_CHOOSER_ACTION_SAVE,
                                        _("Save"),
                                        GTK_RESPONSE_OK,
                                        NULL);

  gtk_dialog_add_button (GTK_DIALOG (dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  file_dialog_add_filters (self, dialog);
  palette_dialog_add_preview (self, dialog);

  name = g_strdup_printf ("%s.xml", gstyle_palette_get_name (palette));
  gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), name);
  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);

  return dialog;
}

static void
palette_load_dialog_cb (GbColorPickerPrefs *self,
                        gint                response_id,
                        GtkDialog          *dialog)
{
  g_autoptr (GFile) file = NULL;
  GstylePalette *palette;
  const gchar *palette_name;
  GError *error = NULL;

  g_assert (GB_IS_COLOR_PICKER_PREFS (self));
  g_assert (GTK_IS_DIALOG (dialog));

  if (response_id == GTK_RESPONSE_OK)
    {
      /* TODO: check for file, not dir */
      file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
      if (file != NULL)
        {
          palette = gstyle_palette_new_from_file (file, NULL, &error);
          if (palette == NULL)
            {
              g_warning ("Can't load the palette: %s", error->message);
              g_error_free (error);
            }
          else
            {
              if (!gstyle_palette_widget_add (self->palette_widget, palette))
                {
                  palette_name = gstyle_palette_get_name (palette);
                  g_warning ("The palette named '%s' already exist in the list", palette_name);
                }
              else
                gstyle_palette_widget_show_palette (self->palette_widget, palette);

              g_object_unref (palette);
            }
        }
    }

  gstyle_palette_widget_remove_all (GSTYLE_PALETTE_WIDGET (self->preview_palette_widget));
  gtk_file_chooser_set_preview_widget (GTK_FILE_CHOOSER (dialog), NULL);
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
load_palette_button_clicked_cb (GbColorPickerPrefs *self,
                                GtkButton          *button)
{
  GtkWidget *dialog;

  g_assert (GB_IS_COLOR_PICKER_PREFS (self));
  g_assert (GTK_IS_BUTTON (button));

  dialog = create_file_load_dialog (self);
  g_signal_connect_object (dialog, "response", G_CALLBACK (palette_load_dialog_cb), self, G_CONNECT_SWAPPED);
  gtk_widget_show (dialog);
}

static void
palette_save_dialog_cb (GbColorPickerPrefs *self,
                        gint                response_id,
                        GtkDialog          *dialog)
{
  GstylePalette *selected_palette;
  g_autoptr (GFile) file = NULL;
  const gchar *palette_name;
  GError *error = NULL;

  g_assert (GB_IS_COLOR_PICKER_PREFS (self));
  g_assert (GTK_IS_DIALOG (dialog));

  if (response_id == GTK_RESPONSE_OK)
    {
      selected_palette = gstyle_palette_widget_get_selected_palette (self->palette_widget);
      file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
      if (file != NULL)
        if (!gstyle_palette_save_to_xml (selected_palette, file, &error))
        {
          palette_name = gstyle_palette_get_name (selected_palette);
          g_warning ("Can't save the palette anmed '%s': %s",
                     palette_name,
                     error->message);

          g_error_free (error);
        }
    }

  gstyle_palette_widget_remove_all (GSTYLE_PALETTE_WIDGET (self->preview_palette_widget));
  gtk_file_chooser_set_preview_widget (GTK_FILE_CHOOSER (dialog), NULL);
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
save_palette_button_clicked_cb (GbColorPickerPrefs *self,
                                GtkButton          *button)
{
  GtkWidget *dialog;
  GstylePalette *selected_palette;

  g_assert (GB_IS_COLOR_PICKER_PREFS (self));
  g_assert (GTK_IS_BUTTON (button));

  selected_palette = gstyle_palette_widget_get_selected_palette (self->palette_widget);
  dialog = create_file_save_dialog (self, selected_palette);
  g_signal_connect_object (dialog, "response", G_CALLBACK (palette_save_dialog_cb), self, G_CONNECT_SWAPPED);
  gtk_widget_show (dialog);
}

static void
generate_palette_button_clicked_cb (GbColorPickerPrefs *self,
                                    GtkButton          *button)
{
  IdeEditorView *view;
  GtkTextBuffer *buffer;
  GstylePalette *palette;

  g_assert (GB_IS_COLOR_PICKER_PREFS (self));
  g_assert (GTK_IS_BUTTON (button));

  view = IDE_EDITOR_VIEW (self->addin->active_view);
  buffer = GTK_TEXT_BUFFER (ide_editor_view_get_document (view));

  palette = gstyle_palette_new_from_buffer (buffer, NULL, NULL, NULL, NULL);
  if (palette != NULL)
    {
      gstyle_palette_widget_add (self->palette_widget, palette);
      g_object_unref (palette);
    }
}

static void
gb_color_picker_prefs_row_closed_cb (GbColorPickerPrefs *self,
                                     const gchar        *palette_id)
{
  g_assert (GB_IS_COLOR_PICKER_PREFS (self));

  gstyle_palette_widget_remove_by_id (self->palette_widget, palette_id);
}

static void
gb_color_picker_prefs_row_name_changed_cb (GbColorPickerPrefs *self,
                                           const gchar        *palette_id,
                                           const gchar        *name)
{
  GstylePalette *palette;

  g_assert (GB_IS_COLOR_PICKER_PREFS (self));

  palette = gstyle_palette_widget_get_palette_by_id  (self->palette_widget, palette_id);
  gstyle_palette_set_name (palette, name);
  gstyle_color_panel_show_palette (self->panel, palette);
}

static GtkWidget *
create_palette_list_item (gpointer item,
                          gpointer user_data)
{
  GbColorPickerPrefs *self = (GbColorPickerPrefs *)user_data;
  GstylePalette *palette = (GstylePalette *)item;
  GtkWidget *row;
  const gchar *name;
  g_autofree gchar *target;

  g_assert (GB_IS_COLOR_PICKER_PREFS (self));
  g_assert (GSTYLE_IS_PALETTE (palette));

  name = gstyle_palette_get_name (palette);
  target = g_strdup_printf ("\"%s\"", gstyle_palette_get_id (palette));
  row = g_object_new (GB_TYPE_COLOR_PICKER_PREFS_PALETTE_ROW,
                      "visible", TRUE,
                      "key", "selected-palette-id",
                      "schema-id", "org.gnome.builder.plugins.color_picker_plugin",
                      "palette-name", name,
                      "target", string_to_variant (target),
                      NULL);

  g_signal_connect_object (row, "closed", G_CALLBACK (gb_color_picker_prefs_row_closed_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (row, "name-changed", G_CALLBACK (gb_color_picker_prefs_row_name_changed_cb), self, G_CONNECT_SWAPPED);

  return row;
}

GstyleColorPanel *
gb_color_picker_prefs_get_panel (GbColorPickerPrefs *self)
{
  g_return_val_if_fail (GB_IS_COLOR_PICKER_PREFS (self), NULL);

  return self->panel;
}

static void
gb_color_picker_prefs_bind_settings (GbColorPickerPrefs *self)
{
  g_return_if_fail (GB_IS_COLOR_PICKER_PREFS (self));

  g_settings_bind (self->plugin_settings,"selected-palette-id",
                   self->palette_widget, "selected-palette-id",
                   G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (self->components_settings,"hsv-visible",
                   self->panel, "hsv-visible",
                   G_SETTINGS_BIND_GET);

  g_settings_bind (self->components_settings,"lab-visible",
                   self->panel, "lab-visible",
                   G_SETTINGS_BIND_GET);

  g_settings_bind (self->components_settings,"rgb-visible",
                   self->panel, "rgb-visible",
                   G_SETTINGS_BIND_GET);

  g_settings_bind (self->components_settings,"rgb-unit",
                   self->panel, "rgb-unit",
                   G_SETTINGS_BIND_GET);

  g_settings_bind (self->plugin_settings,"strings-visible",
                   self->panel, "strings-visible",
                   G_SETTINGS_BIND_GET);

  g_settings_bind (self->plugin_settings,"filter",
                   self->panel, "filter",
                   G_SETTINGS_BIND_GET);
}

static void
gb_color_picker_prefs_unbind_settings (GbColorPickerPrefs *self)
{
  g_return_if_fail (GB_IS_COLOR_PICKER_PREFS (self));

  g_settings_unbind (self->palette_widget, "selected-palette-id");
  g_settings_unbind (self->panel, "hsv-visible");
  g_settings_unbind (self->panel, "lab-visible");
  g_settings_unbind (self->panel, "rgb-visible");
  g_settings_unbind (self->panel, "rgb-unit");
  g_settings_unbind (self->panel, "string-visible");
  g_settings_unbind (self->panel, "filter");
}

void
gb_color_picker_prefs_set_panel (GbColorPickerPrefs *self,
                                 GstyleColorPanel   *panel)
{
  g_return_if_fail (GB_IS_COLOR_PICKER_PREFS (self));
  g_return_if_fail (panel == NULL || GSTYLE_IS_COLOR_PANEL (panel));

  if (self->panel != panel)
    {
      if (self->panel)
        {
          gb_color_picker_prefs_unbind_settings (self);
          gstyle_color_panel_set_prefs_pages (self->panel, NULL, NULL, NULL, NULL);
          gtk_list_box_bind_model (GTK_LIST_BOX (self->palettes_listbox), NULL, NULL, NULL, NULL);
          self->panel = NULL;
          self->palette_widget = NULL;
        }

      self->panel = panel;
      if (panel != NULL && GSTYLE_IS_COLOR_PANEL (panel))
        {
          self->palette_widget = gstyle_color_panel_get_palette_widget (self->panel);
          self->palettes_store = gstyle_palette_widget_get_store (self->palette_widget);
          gtk_list_box_bind_model (GTK_LIST_BOX (self->palettes_listbox),
                                   G_LIST_MODEL (self->palettes_store),
                                   create_palette_list_item, self, NULL);

          gstyle_color_panel_set_prefs_pages (panel,
                                              gb_color_picker_prefs_get_page (self, GSTYLE_COLOR_PANEL_PREFS_COMPONENTS),
                                              gb_color_picker_prefs_get_page (self, GSTYLE_COLOR_PANEL_PREFS_COLOR_STRINGS),
                                              gb_color_picker_prefs_get_page (self, GSTYLE_COLOR_PANEL_PREFS_PALETTES),
                                              gb_color_picker_prefs_get_page (self, GSTYLE_COLOR_PANEL_PREFS_PALETTES_LIST));

          gb_color_picker_prefs_bind_settings (self);
        }

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_PANEL]);
    }
}

static void
gb_color_picker_prefs_palette_added_cb (GbColorPickerPrefs            *self,
                                        GbColorPickerPrefsPaletteList *palette_box)
{
  GstylePalette *palette;

  g_assert (GB_IS_COLOR_PICKER_PREFS (self));
  g_assert (GB_IS_COLOR_PICKER_PREFS_PALETTE_LIST (palette_box));

  palette = gstyle_palette_new ();
  gstyle_palette_widget_add (self->palette_widget, palette);
  g_object_unref (palette);
}

GbColorPickerPrefs *
gb_color_picker_prefs_new (void)
{
  return g_object_new (GB_TYPE_COLOR_PICKER_PREFS, NULL);
}

static void
gb_color_picker_prefs_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  GbColorPickerPrefs *self = GB_COLOR_PICKER_PREFS (object);

  switch (prop_id)
    {
    case PROP_ADDIN:
      g_value_set_object (value, self->addin);
      break;

    case PROP_PANEL:
      g_value_set_object (value, gb_color_picker_prefs_get_panel (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gb_color_picker_prefs_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  GbColorPickerPrefs *self = GB_COLOR_PICKER_PREFS (object);

  switch (prop_id)
    {
    case PROP_ADDIN:
      self->addin = g_value_get_object (value);
      break;

    case PROP_PANEL:
      gb_color_picker_prefs_set_panel (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gb_color_picker_prefs_finalize (GObject *object)
{
  GbColorPickerPrefs *self = (GbColorPickerPrefs *)object;

  gb_color_picker_prefs_set_panel (self, NULL);

  g_clear_object (&self->components_page);
  g_clear_object (&self->color_strings_page);
  g_clear_object (&self->palettes_page);
  g_clear_object (&self->palettes_list_page);

  g_clear_object (&self->all_files_filter);
  g_clear_object (&self->gstyle_files_filter);
  g_clear_object (&self->gpl_files_filter);
  g_clear_object (&self->builder_files_filter);

  g_clear_object (&self->preview);
  g_clear_object (&self->plugin_settings);
  g_clear_object (&self->components_settings);

  G_OBJECT_CLASS (gb_color_picker_prefs_parent_class)->finalize (object);
}

static void
gb_color_picker_prefs_class_init (GbColorPickerPrefsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gb_color_picker_prefs_finalize;
  object_class->set_property = gb_color_picker_prefs_set_property;
  object_class->get_property = gb_color_picker_prefs_get_property;

  properties [PROP_PANEL] =
    g_param_spec_object ("panel",
                         "panel",
                         "Color panel",
                         GSTYLE_TYPE_COLOR_PANEL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_ADDIN] =
    g_param_spec_object ("addin",
                         "addin",
                         "Colorpicker worbench addin",
                         GB_TYPE_COLOR_PICKER_WORKBENCH_ADDIN,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
gb_color_picker_prefs_init (GbColorPickerPrefs *self)
{
  GtkBuilder *builder;
  GtkWidget *palettes_placeholder;

  g_type_ensure (GB_TYPE_COLOR_PICKER_PREFS_LIST);
  g_type_ensure (GB_TYPE_COLOR_PICKER_PREFS_PALETTE_LIST);

  builder = gtk_builder_new_from_resource ("/org/gnome/builder/plugins/color-picker-plugin/gtk/color-picker-prefs.ui");

  self->palettes_box = GB_COLOR_PICKER_PREFS_PALETTE_LIST (gtk_builder_get_object (builder, "palettes_box"));
  palettes_placeholder = GTK_WIDGET (gtk_builder_get_object (builder, "palettes_placeholder"));
  self->palettes_listbox = gb_color_picker_prefs_palette_list_get_list_box (self->palettes_box);
  gtk_list_box_set_placeholder (self->palettes_listbox, palettes_placeholder);

  g_signal_connect_object (self->palettes_box, "added",
                           G_CALLBACK (gb_color_picker_prefs_palette_added_cb),
                           self, G_CONNECT_SWAPPED);

  self->load_palette_button = GTK_WIDGET (gtk_builder_get_object (builder, "load_palette_button"));
  g_signal_connect_swapped (self->load_palette_button, "clicked", G_CALLBACK (load_palette_button_clicked_cb), self);

  self->save_palette_button = GTK_WIDGET (gtk_builder_get_object (builder, "save_palette_button"));
  g_signal_connect_swapped (self->save_palette_button, "clicked", G_CALLBACK (save_palette_button_clicked_cb), self);

  self->generate_palette_button = GTK_WIDGET (gtk_builder_get_object (builder, "generate_palette_button"));
  g_signal_connect_swapped (self->generate_palette_button, "clicked", G_CALLBACK (generate_palette_button_clicked_cb), self);

  self->all_files_filter = g_object_ref_sink (gtk_file_filter_new ());
  gtk_file_filter_set_name (self->all_files_filter, _("All files"));
  gtk_file_filter_add_pattern (self->all_files_filter, "*.*");

  self->gstyle_files_filter = g_object_ref_sink (gtk_file_filter_new ());
  gtk_file_filter_set_name (self->gstyle_files_filter, _("All supported palettes formats"));
  gtk_file_filter_add_pattern (self->gstyle_files_filter, "*.gpl");
  gtk_file_filter_add_pattern (self->gstyle_files_filter, "*.xml");

  self->gpl_files_filter = g_object_ref_sink (gtk_file_filter_new ());
  gtk_file_filter_set_name (self->gpl_files_filter, _("GIMP palette"));
  gtk_file_filter_add_pattern (self->gpl_files_filter, "*.gpl");

  self->builder_files_filter = g_object_ref_sink (gtk_file_filter_new ());
  gtk_file_filter_set_name (self->builder_files_filter, _("GNOME Builder palette"));
  gtk_file_filter_add_pattern (self->builder_files_filter, "*.xml");

  self->components_page = GTK_WIDGET (gtk_builder_get_object (builder, "components_page"));
  g_object_ref_sink (self->components_page);
  self->color_strings_page = GTK_WIDGET (gtk_builder_get_object (builder, "colorstrings_page"));
  g_object_ref_sink (self->color_strings_page);
  self->palettes_page = GTK_WIDGET (gtk_builder_get_object (builder, "palettes_page"));
  g_object_ref_sink (self->palettes_page);
  self->palettes_list_page = GTK_WIDGET (gtk_builder_get_object (builder, "paletteslist_page"));
  g_object_ref_sink (self->palettes_list_page);

  g_object_unref (builder);

  builder = gtk_builder_new_from_resource ("/org/gnome/builder/plugins/color-picker-plugin/gtk/color-picker-preview.ui");
  self->preview = GTK_WIDGET (gtk_builder_get_object (builder, "preview"));
  g_object_ref_sink (self->preview);
  self->preview_palette_widget = GTK_WIDGET (gtk_builder_get_object (builder, "preview_palette_widget"));
  self->preview_title = GTK_WIDGET (gtk_builder_get_object (builder, "preview_title"));

  self->preview_placeholder = GTK_WIDGET (gtk_builder_get_object (builder, "preview_placeholder"));
  gstyle_palette_widget_set_placeholder (GSTYLE_PALETTE_WIDGET (self->preview_palette_widget), self->preview_placeholder);

  g_object_unref (builder);

  self->plugin_settings = g_settings_new ("org.gnome.builder.plugins.color_picker_plugin");
  self->components_settings = g_settings_new ("org.gnome.builder.plugins.color_picker_plugin.components");
}
