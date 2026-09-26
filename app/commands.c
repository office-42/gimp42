/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include "appenv.h"
#include "about_dialog.h"
#include "actionarea.h"
#include "app_procs.h"
#include "brightness_contrast.h"
#include "brushes.h"
#include "by_color_select.h"
#include "channels_dialog.h"
#include "colormaps.h"
#include "color_balance.h"
#include "commands.h"
#include "convert.h"
#include "curves.h"
#include "desaturate.h"
#include "channel_ops.h"
#include "drawable.h"
#include "equalize.h"
#include "fileops.h"
#include "floating_sel.h"
#include "gdisplay_ops.h"
#include "general.h"
#include "gimage_cmds.h"
#include "gimage_mask.h"
#include "gimprc.h"
#include "global_edit.h"
#include "gradient.h"
#include "histogram_tool.h"
#include "hue_saturation.h"
#include "image_render.h"
#include "indexed_palette.h"
#include "info_window.h"
#include "interface.h"
#include "invert.h"
#include "layers_dialog.h"
#include "layer_select.h"
#include "levels.h"
#include "menus.h"
#include "palette.h"
#include "patterns.h"
#include "plug_in.h"
#include "posterize.h"
#include "resize.h"
#include "scale.h"
#include "threshold.h"
#include "tips_dialog.h"
#include "tools.h"
#include "undo.h"

/*  external functions  */
extern void layers_dialog_layer_merge_query (GImage *, int);

typedef struct {
  GtkWidget *dlg;
  GtkWidget *height_entry;
  GtkWidget *width_entry;
  int width;
  int height;
  int type;
  int fill_type;
} NewImageValues;

typedef struct
{
  GtkWidget * shell;
  Resize *    resize;
  int         gimage_id;
} ImageResize;

/*  new image local functions  */
static void file_new_ok_callback (GtkWidget *, gpointer);
static void file_new_cancel_callback (GtkWidget *, gpointer);
static gboolean file_new_delete_callback (GtkWindow *, gpointer);
static void file_new_toggle_callback (GtkWidget *, gpointer);

/*  static variables  */
static   int          last_width = 256;
static   int          last_height = 256;
static   int          last_type = RGB;
static   int          last_fill_type = BACKGROUND_FILL;

/*  preferences local functions  */
static void file_prefs_ok_callback (GtkWidget *, GtkWidget *);
static void file_prefs_save_callback (GtkWidget *, GtkWidget *);
static void file_prefs_cancel_callback (GtkWidget *, GtkWidget *);
static gboolean file_prefs_delete_callback (GtkWindow *, gpointer);
static void file_prefs_toggle_callback (GtkWidget *, gpointer);
static void file_prefs_text_callback (GtkWidget *, gpointer);
static void file_prefs_preview_size_callback (GtkWidget *, gpointer);

/*  static variables  */
static   GtkWidget   *prefs_dlg = NULL;
static   int          old_transparency_type;
static   int          old_transparency_size;
static   int          old_levels_of_undo;
static   int          old_marching_speed;
static   int          old_allow_resize_windows;
static   int          old_auto_save;
static   int          old_preview_size;
static   int          old_no_cursor_updating;
static   int          old_show_tool_tips;
static   int          old_cubic_interpolation;
static   int          old_confirm_on_close;
static   int          old_default_width;
static   int          old_default_height;
static   int          old_default_type;
static   int          new_dialog_run;
static   int          old_stingy_memory_use;
static   int          old_tile_cache_size;
static   int          old_install_cmap;
static   int          old_cycled_marching_ants;
static   char *       old_temp_path;
static   char *       old_swap_path;
static   char *       old_brush_path;
static   char *       old_pattern_path;
static   char *       old_palette_path;
static   char *       old_plug_in_path;
static   char *       old_gradient_path;

static   char *       edit_temp_path = NULL;
static   char *       edit_swap_path = NULL;
static   char *       edit_brush_path = NULL;
static   char *       edit_pattern_path = NULL;
static   char *       edit_palette_path = NULL;
static   char *       edit_plug_in_path = NULL;
static   char *       edit_gradient_path = NULL;
static   int          edit_stingy_memory_use;
static   int          edit_tile_cache_size;
static   int          edit_install_cmap;
static   int          edit_cycled_marching_ants;


/*  local functions  */
static void   image_resize_callback (GtkWidget *, gpointer);
static void   image_scale_callback (GtkWidget *, gpointer);
static void   image_cancel_callback (GtkWidget *, gpointer);
static gboolean image_delete_callback (GtkWindow *, gpointer);
static void   gimage_mask_feather_callback (GtkWidget *, gpointer, gpointer);
static void   gimage_mask_border_callback (GtkWidget *, gpointer, gpointer);
static void   gimage_mask_grow_callback (GtkWidget *, gpointer, gpointer);
static void   gimage_mask_shrink_callback (GtkWidget *, gpointer, gpointer);

/*  variables declared in gimage_mask.c--we need them to set up
 *  initial values for the various dialog boxes which query for values
 */
extern double gimage_mask_feather_radius;
extern int    gimage_mask_border_radius;
extern int    gimage_mask_grow_pixels;
extern int    gimage_mask_shrink_pixels;


static void
file_new_ok_callback (GtkWidget *widget,
		      gpointer   data)
{
  NewImageValues *vals;
  GImage *gimage;
  GDisplay *gdisplay;
  Layer *layer;
  int type;

  vals = data;

  vals->width = atoi (gtk_editable_get_text (GTK_EDITABLE (vals->width_entry)));
  vals->height = atoi (gtk_editable_get_text (GTK_EDITABLE (vals->height_entry)));

  gtk_window_destroy (GTK_WINDOW (vals->dlg));

  last_width = vals->width;
  last_height = vals->height;
  last_type = vals->type;
  last_fill_type = vals->fill_type;

  switch (vals->fill_type)
    {
    case BACKGROUND_FILL:
    case FOREGROUND_FILL:
    case WHITE_FILL:
      type = (vals->type == RGB) ? RGB_GIMAGE : GRAY_GIMAGE;
      break;
    case TRANSPARENT_FILL:
      type = (vals->type == RGB) ? RGBA_GIMAGE : GRAYA_GIMAGE;
      break;
    default:
      type = RGB_IMAGE;
      break;
    }

  gimage = gimage_new (vals->width, vals->height, vals->type);

  /*  Make the background (or first) layer  */
  layer = layer_new (gimage->ID, gimage->width, gimage->height,
		     type, "Background", OPAQUE_OPACITY, NORMAL);

  if (layer) {
    /*  add the new layer to the gimage  */
    gimage_disable_undo (gimage);
    gimage_add_layer (gimage, layer, 0);
    gimage_enable_undo (gimage);

    drawable_fill (GIMP_DRAWABLE(layer), vals->fill_type);

    gimage_clean_all (gimage);

    gdisplay = gdisplay_new (gimage, 0x0101);
    (void) gdisplay;
  }

  g_free (vals);
}

static gboolean
file_new_delete_callback (GtkWindow *window,
			  gpointer   data)
{
  file_new_cancel_callback (GTK_WIDGET (window), data);

  return TRUE;
}


static void
file_new_cancel_callback (GtkWidget *widget,
			  gpointer   data)
{
  NewImageValues *vals;

  vals = data;

  gtk_window_destroy (GTK_WINDOW (vals->dlg));
  g_free (vals);
}

static void
file_new_toggle_callback (GtkWidget *widget,
			  gpointer   data)
{
  int *val;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    {
      val = data;
      *val = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "user_data"));
    }
}

/*  A radio button in box for one value of *val, active when it is the
 *  current one.
 */
static GtkWidget *
radio_button_new (GtkWidget  *box,
		  GtkWidget  *group,
		  const char *label,
		  int         value,
		  int        *val,
		  GCallback   callback)
{
  GtkWidget *button;

  button = gimp_radio_button_new (group, label);
  gimp_box_pack_start (box, button, TRUE, TRUE, 0);
  g_object_set_data (G_OBJECT (button), "user_data", GINT_TO_POINTER (value));
  gtk_check_button_set_active (GTK_CHECK_BUTTON (button), *val == value);
  g_signal_connect (button, "toggled", callback, val);

  return button;
}

void
file_new_cmd_callback (GtkWidget           *widget,
		       gpointer             callback_data,
		       guint                callback_action)
{
  GDisplay *gdisp;
  NewImageValues *vals;
  GtkWidget *button;
  GtkWidget *label;
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *frame;
  GtkWidget *radio_box;
  char buffer[32];

  if(!new_dialog_run)
    {
      last_width = default_width;
      last_height = default_height;
      last_type = default_type;
      new_dialog_run = 1;
    }

  /*  Before we try to determine the responsible gdisplay,
   *  make sure this wasn't called from the toolbox
   */
  if (callback_action)
    gdisp = gdisplay_active ();
  else
    gdisp = NULL;

  vals = g_malloc (sizeof (NewImageValues));
  vals->fill_type = last_fill_type;

  if (gdisp)
    {
      vals->width = gdisp->gimage->width;
      vals->height = gdisp->gimage->height;
      vals->type = gimage_base_type (gdisp->gimage);
    }
  else
    {
      vals->width = last_width;
      vals->height = last_height;
      vals->type = last_type;
    }

  if (vals->type == INDEXED)
    vals->type = RGB;    /* no indexed images */

  vals->dlg = gimp_dialog_new ("New Image");

  /* handle the wm close signal */
  g_signal_connect (vals->dlg, "close-request",
		    G_CALLBACK (file_new_delete_callback),
		    vals);

  gimp_dialog_add_button (vals->dlg, "OK",
			  G_CALLBACK (file_new_ok_callback), vals, TRUE);
  gimp_dialog_add_button (vals->dlg, "Cancel",
			  G_CALLBACK (file_new_cancel_callback), vals, FALSE);

  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, 2);
  gimp_box_pack_start (gimp_dialog_get_vbox (vals->dlg), vbox, TRUE, TRUE, 0);

  table = gimp_table_new (2, 2, FALSE);
  gtk_grid_set_row_spacing (GTK_GRID (table), 2);
  gtk_grid_set_column_spacing (GTK_GRID (table), 2);
  gimp_box_pack_start (vbox, table, TRUE, TRUE, 0);

  label = gtk_label_new ("Width:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 0, 1,
		     GIMP_FILL, GIMP_FILL, 0, 0);

  label = gtk_label_new ("Height:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 1, 2,
		     GIMP_FILL, GIMP_FILL, 0, 0);

  vals->width_entry = gtk_entry_new ();
  gtk_widget_set_size_request (vals->width_entry, 75, -1);
  g_snprintf (buffer, sizeof (buffer), "%d", vals->width);
  gtk_editable_set_text (GTK_EDITABLE (vals->width_entry), buffer);
  gimp_table_attach (table, vals->width_entry, 1, 2, 0, 1,
		     GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 0, 0);

  vals->height_entry = gtk_entry_new ();
  gtk_widget_set_size_request (vals->height_entry, 75, -1);
  g_snprintf (buffer, sizeof (buffer), "%d", vals->height);
  gtk_editable_set_text (GTK_EDITABLE (vals->height_entry), buffer);
  gimp_table_attach (table, vals->height_entry, 1, 2, 1, 2,
		     GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 0, 0);

  /*  Enter in an entry means OK  */
  gtk_entry_set_activates_default (GTK_ENTRY (vals->width_entry), TRUE);
  gtk_entry_set_activates_default (GTK_ENTRY (vals->height_entry), TRUE);

  frame = gtk_frame_new ("Image Type");
  gimp_box_pack_start (vbox, frame, TRUE, TRUE, 0);

  radio_box = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (radio_box, 2);
  gtk_frame_set_child (GTK_FRAME (frame), radio_box);

  button = radio_button_new (radio_box, NULL, "RGB", RGB, &vals->type,
			     G_CALLBACK (file_new_toggle_callback));
  radio_button_new (radio_box, button, "Grayscale", GRAY, &vals->type,
		    G_CALLBACK (file_new_toggle_callback));

  frame = gtk_frame_new ("Fill Type");
  gimp_box_pack_start (vbox, frame, TRUE, TRUE, 0);

  radio_box = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (radio_box, 2);
  gtk_frame_set_child (GTK_FRAME (frame), radio_box);

  button = radio_button_new (radio_box, NULL, "Background", BACKGROUND_FILL,
			     &vals->fill_type,
			     G_CALLBACK (file_new_toggle_callback));
  radio_button_new (radio_box, button, "White", WHITE_FILL,
		    &vals->fill_type, G_CALLBACK (file_new_toggle_callback));
  radio_button_new (radio_box, button, "Transparent", TRANSPARENT_FILL,
		    &vals->fill_type, G_CALLBACK (file_new_toggle_callback));
  radio_button_new (radio_box, button, "Foreground", FOREGROUND_FILL,
		    &vals->fill_type, G_CALLBACK (file_new_toggle_callback));

  gtk_window_present (GTK_WINDOW (vals->dlg));
}

void
file_open_cmd_callback (GtkWidget *widget,
			gpointer   client_data)
{
  file_open_callback (widget, client_data);
}

void
file_save_cmd_callback (GtkWidget *widget,
			gpointer   client_data)
{
  file_save_callback (widget, client_data);
}

void
file_save_as_cmd_callback (GtkWidget *widget,
			   gpointer   client_data)
{
  file_save_as_callback (widget, client_data);
}


/* Some information regarding preferences, compiled by Raph Levien 11/3/97.

   The following preference items cannot be set on the fly (at least
   according to the existing pref code - it may be that changing them
   so they're set on the fly is not hard).

   temp-path
   swap-path
   brush-path
   pattern-path
   plug-in-path
   palette-path
   gradient-path
   stingy-memory-use
   tile-cache-size
   install-cmap
   cycled-marching-ants

   All of these now have variables of the form edit_temp_path, which
   are copied from the actual variables (e.g. temp_path) the first time
   the dialog box is started.

   Variables of the form old_temp_path represent the values at the
   time the dialog is opened - a cancel copies them back from old to
   the real variables or the edit variables, depending on whether they
   can be set on the fly.

   Here are the remaining issues as I see them:

   Still no settings for default-brush, default-gradient,
   default-palette, default-pattern, gamma-correction, color-cube,
   show-rulers, ruler-units. No widget for confirm-on-close although
   a lot of stuff is there.

   No UI feedback for the fact that some settings won't take effect
   until the next Gimp restart.

   The semantics of "save" are a little funny - it only saves the
   settings that are different from the way they were when the dialog
   was opened. So you can set something, close the window, open it
   up again, click "save" and have nothing happen. To change this
   to more intuitive semantics, we should have a whole set of init_
   variables that are set the first time the dialog is opened (along
   with the edit_ variables that are currently set). Then, the save
   callback checks against the init_ variable rather than the old_.

   */

/* Copy the string from source to destination, freeing the string stored
   in the destination if there is one there already. */
static void
file_prefs_strset (char **dst, char *src)
{
  if (*dst != NULL)
    g_free (*dst);
  *dst = g_strdup (src);
}


/* Duplicate the string, but treat NULL as the empty string. */
static char *
file_prefs_strdup (char *src)
{
  return g_strdup (src == NULL ? "" : src);
}

/* Compare two strings, but treat NULL as the empty string. */
static int
file_prefs_strcmp (char *src1, char *src2)
{
  return strcmp (src1 == NULL ? "" : src1,
		   src2 == NULL ? "" : src2);
}

static void
file_prefs_ok_callback (GtkWidget *widget,
			GtkWidget *dlg)
{

  if (levels_of_undo < 0)
    {
      g_message ("Error: Levels of undo must be zero or greater.");
      levels_of_undo = old_levels_of_undo;
      return;
    }
  if (marching_speed < 50)
    {
      g_message ("Error: Marching speed must be 50 or greater.");
      marching_speed = old_marching_speed;
      return;
    }
  if (default_width < 1)
    {
      g_message ("Error: Default width must be one or greater.");
      default_width = old_default_width;
      return;
    }
  if (default_height < 1)
    {
      g_message ("Error: Default height must be one or greater.");
      default_height = old_default_height;
      return;
    }

  gtk_window_destroy (GTK_WINDOW (dlg));
  prefs_dlg = NULL;

  /*  GTK 4 has no global switch for tool tips: show_tool_tips is saved
   *  in gimprc, but tool tips stay enabled.
   */
}

static void
file_prefs_save_callback (GtkWidget *widget,
			  GtkWidget *dlg)
{
  GList *update = NULL; /* options that should be updated in .gimprc */
  GList *remove = NULL; /* options that should be commented out */
  int save_stingy_memory_use;
  int save_tile_cache_size;
  int save_install_cmap;
  int save_cycled_marching_ants;
  gchar *save_temp_path;
  gchar *save_swap_path;
  gchar *save_brush_path;
  gchar *save_pattern_path;
  gchar *save_palette_path;
  gchar *save_plug_in_path;
  gchar *save_gradient_path;
  int restart_notification = FALSE;

  file_prefs_ok_callback (widget, dlg);

  /* Save variables so that we can restore them later */
  save_stingy_memory_use = stingy_memory_use;
  save_tile_cache_size = tile_cache_size;
  save_install_cmap = install_cmap;
  save_cycled_marching_ants = cycled_marching_ants;
  save_temp_path = temp_path;
  save_swap_path = swap_path;
  save_brush_path = brush_path;
  save_pattern_path = pattern_path;
  save_palette_path = palette_path;
  save_plug_in_path = plug_in_path;
  save_gradient_path = gradient_path;

  if (levels_of_undo != old_levels_of_undo)
    update = g_list_append (update, "undo-levels");
  if (marching_speed != old_marching_speed)
    update = g_list_append (update, "marching-ants-speed");
  if (allow_resize_windows != old_allow_resize_windows)
    update = g_list_append (update, "allow-resize-windows");
  if (auto_save != old_auto_save)
    {
      update = g_list_append (update, "auto-save");
      remove = g_list_append (remove, "dont-auto-save");
    }
  if (no_cursor_updating != old_no_cursor_updating)
    {
      update = g_list_append (update, "cursor-updating");
      remove = g_list_append (remove, "no-cursor-updating");
    }
  if (show_tool_tips != old_show_tool_tips)
    {
      update = g_list_append (update, "show-tool-tips");
      remove = g_list_append (remove, "dont-show-tool-tips");
    }
  if (cubic_interpolation != old_cubic_interpolation)
    update = g_list_append (update, "cubic-interpolation");
  if (confirm_on_close != old_confirm_on_close)
    update = g_list_append (update, "confirm-on-close");
  if (default_width != old_default_width ||
      default_height != old_default_height)
    update = g_list_append (update, "default-image-size");
  if (default_type != old_default_type)
    update = g_list_append (update, "default-image-type");
  if (preview_size != old_preview_size)
    update = g_list_append (update, "preview-size");
  if (transparency_type != old_transparency_type)
    update = g_list_append (update, "transparency-type");
  if (transparency_size != old_transparency_size)
    update = g_list_append (update, "transparency-size");
  if (edit_stingy_memory_use != stingy_memory_use)
    {
      update = g_list_append (update, "stingy-memory-use");
      stingy_memory_use = edit_stingy_memory_use;
      restart_notification = TRUE;
    }
  if (edit_tile_cache_size != tile_cache_size)
    {
      update = g_list_append (update, "tile-cache-size");
      tile_cache_size = edit_tile_cache_size;
      restart_notification = TRUE;
    }
  if (edit_install_cmap != old_install_cmap)
    {
      update = g_list_append (update, "install-colormap");
      install_cmap = edit_install_cmap;
      restart_notification = TRUE;
    }
  if (edit_cycled_marching_ants != cycled_marching_ants)
    {
      update = g_list_append (update, "colormap-cycling");
      cycled_marching_ants = edit_cycled_marching_ants;
      restart_notification = TRUE;
    }
  if (file_prefs_strcmp (temp_path, edit_temp_path))
    {
      update = g_list_append (update, "temp-path");
      temp_path = edit_temp_path;
      restart_notification = TRUE;
    }
  if (file_prefs_strcmp (swap_path, edit_swap_path))
    {
      update = g_list_append (update, "swap-path");
      swap_path = edit_swap_path;
      restart_notification = TRUE;
    }
  if (file_prefs_strcmp (brush_path, edit_brush_path))
    {
      update = g_list_append (update, "brush-path");
      brush_path = edit_brush_path;
      restart_notification = TRUE;
    }
  if (file_prefs_strcmp (pattern_path, edit_pattern_path))
    {
      update = g_list_append (update, "pattern-path");
      pattern_path = edit_pattern_path;
      restart_notification = TRUE;
    }
  if (file_prefs_strcmp (palette_path, edit_palette_path))
    {
      update = g_list_append (update, "palette-path");
      palette_path = edit_palette_path;
      restart_notification = TRUE;
    }
  if (file_prefs_strcmp (plug_in_path, edit_plug_in_path))
    {
      update = g_list_append (update, "plug-in-path");
      plug_in_path = edit_plug_in_path;
      restart_notification = TRUE;
    }
  if (file_prefs_strcmp (gradient_path, edit_gradient_path))
    {
      update = g_list_append (update, "gradient-path");
      gradient_path = edit_gradient_path;
      restart_notification = TRUE;
    }
  save_gimprc (&update, &remove);

  /* Restore variables which must not change */
  stingy_memory_use = save_stingy_memory_use;
  tile_cache_size = save_tile_cache_size;
  install_cmap = save_install_cmap;
  cycled_marching_ants = save_cycled_marching_ants;
  temp_path = save_temp_path;
  swap_path = save_swap_path;
  brush_path = save_brush_path;
  pattern_path = save_pattern_path;
  palette_path = save_palette_path;
  plug_in_path = save_plug_in_path;
  gradient_path = save_gradient_path;

  if (restart_notification)
    g_message ("You will need to restart GIMP for these changes to take effect.");

  g_list_free (update);
  g_list_free (remove);
}

static gboolean
file_prefs_delete_callback (GtkWindow *window,
			    gpointer   dlg)
{
  file_prefs_cancel_callback (GTK_WIDGET (window), dlg);

  /* the widget is already destroyed here no need to try again */
  return TRUE;
}

static void
file_prefs_cancel_callback (GtkWidget *widget,
			    GtkWidget *dlg)
{
  gtk_window_destroy (GTK_WINDOW (dlg));
  prefs_dlg = NULL;

  levels_of_undo = old_levels_of_undo;
  marching_speed = old_marching_speed;
  allow_resize_windows = old_allow_resize_windows;
  auto_save = old_auto_save;
  no_cursor_updating = old_no_cursor_updating;
  show_tool_tips = old_show_tool_tips;
  cubic_interpolation = old_cubic_interpolation;
  confirm_on_close = old_confirm_on_close;
  default_width = old_default_width;
  default_height = old_default_height;
  default_type = old_default_type;
  if (preview_size != old_preview_size)
    {
      lc_dialog_rebuild (old_preview_size);
      layer_select_update_preview_size ();
    }

  if ((transparency_type != old_transparency_type) ||
      (transparency_size != old_transparency_size))
    {
      transparency_type = old_transparency_type;
      transparency_size = old_transparency_size;

      render_setup (transparency_type, transparency_size);
      layer_invalidate_previews (-1);
      gimage_invalidate_previews ();
      gdisplays_expose_full ();
      gdisplays_flush ();
    }

  edit_stingy_memory_use = old_stingy_memory_use;
  edit_tile_cache_size = old_tile_cache_size;
  edit_install_cmap = old_install_cmap;
  edit_cycled_marching_ants = old_cycled_marching_ants;
  file_prefs_strset (&edit_temp_path, old_temp_path);
  file_prefs_strset (&edit_swap_path, old_swap_path);
  file_prefs_strset (&edit_brush_path, old_brush_path);
  file_prefs_strset (&edit_pattern_path, old_pattern_path);
  file_prefs_strset (&edit_palette_path, old_palette_path);
  file_prefs_strset (&edit_plug_in_path, old_plug_in_path);
  file_prefs_strset (&edit_gradient_path, old_gradient_path);
}

static void
file_prefs_toggle_callback (GtkWidget *widget,
			    gpointer   data)
{
  int *val;
  int active;

  active = gtk_check_button_get_active (GTK_CHECK_BUTTON (widget));

  if (data==&allow_resize_windows)
    allow_resize_windows = active;
  else if (data==&auto_save)
    auto_save = active;
  else if (data==&no_cursor_updating)
    no_cursor_updating = active;
  else if (data==&show_tool_tips)
    show_tool_tips = active;
  else if (data==&cubic_interpolation)
    cubic_interpolation = active;
  else if (data==&confirm_on_close)
    confirm_on_close = active;
  else if (data==&edit_stingy_memory_use)
    edit_stingy_memory_use = active;
  else if (data==&edit_install_cmap)
    edit_install_cmap = active;
  else if (data==&edit_cycled_marching_ants)
    edit_cycled_marching_ants = active;
  else if (data==&default_type)
    {
      if (active)
	default_type = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget),
							   "user_data"));
    }
  else if (active)
    {
      val = data;
      *val = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "user_data"));
      render_setup (transparency_type, transparency_size);
      layer_invalidate_previews (-1);
      gimage_invalidate_previews ();
      gdisplays_expose_full ();
      gdisplays_flush ();
    }
}

static void
file_prefs_preview_size_callback (GtkWidget *widget,
                                  gpointer   data)
{
  lc_dialog_rebuild (GPOINTER_TO_INT (data));
  layer_select_update_preview_size ();
}

static void
file_prefs_text_callback (GtkWidget *widget,
			  gpointer   data)
{
  int *val;

  val = data;
  *val = atoi (gtk_editable_get_text (GTK_EDITABLE (widget)));
}

static void
file_prefs_string_callback (GtkWidget *widget,
			    gpointer   data)
{
  gchar **val;

  val = data;
  file_prefs_strset (val, (char *) gtk_editable_get_text (GTK_EDITABLE (widget)));
}

/*  A check button for a boolean preference, packed into box.  */
static GtkWidget *
file_prefs_check_new (GtkWidget  *box,
		      const char *label,
		      int         state,
		      int        *val,
		      gboolean    expand)
{
  GtkWidget *button;

  button = gtk_check_button_new_with_label (label);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (button), state);
  gimp_box_pack_start (box, button, expand, expand, 0);
  g_signal_connect (button, "toggled",
		    G_CALLBACK (file_prefs_toggle_callback),
		    val);

  return button;
}

/*  A labelled entry for an integer preference in a new row of box.  */
static void
file_prefs_int_entry_new (GtkWidget  *box,
			  const char *label_text,
			  int         value,
			  int        *val)
{
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *entry;
  char buffer[32];

  hbox = gimp_hbox_new (FALSE, 2);
  gimp_box_pack_start (box, hbox, FALSE, FALSE, 0);

  label = gtk_label_new (label_text);
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);

  entry = gtk_entry_new ();
  gtk_widget_set_size_request (entry, 75, -1);
  g_snprintf (buffer, sizeof (buffer), "%d", value);
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
  gimp_box_pack_start (hbox, entry, FALSE, FALSE, 0);
  g_signal_connect (entry, "changed",
		    G_CALLBACK (file_prefs_text_callback),
		    val);
}

/*  A radio button for one value of the preference *val.  */
static GtkWidget *
file_prefs_radio_new (GtkWidget  *box,
		      GtkWidget  *group,
		      const char *label,
		      int         value,
		      int        *val)
{
  return radio_button_new (box, group, label, value, val,
			   G_CALLBACK (file_prefs_toggle_callback));
}

/*  An outer frame, one notebook page.  */
static GtkWidget *
file_prefs_page_new (GtkWidget  *notebook,
		     const char *title,
		     const char *tab)
{
  GtkWidget *out_frame;
  GtkWidget *vbox;

  out_frame = gtk_frame_new (title);
  gimp_container_set_border_width (out_frame, 10);

  vbox = gimp_vbox_new (FALSE, 2);
  gimp_container_set_border_width (vbox, 1);
  gtk_frame_set_child (GTK_FRAME (out_frame), vbox);

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), out_frame,
			    gtk_label_new (tab));

  return vbox;
}

void
file_pref_cmd_callback (GtkWidget *widget,
			gpointer   client_data)
{
  GtkWidget *button;
  GtkWidget *frame;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *abox;
  GtkWidget *label;
  GtkWidget *radio_box;
  GtkWidget *entry;
  GtkWidget *optionmenu;
  GtkWidget *notebook;
  GtkWidget *table;
  GtkWidget *group;
  char buffer[32];
  char *transparencies[] =
  {
    "Light Checks",
    "Mid-Tone Checks",
    "Dark Checks",
    "White Only",
    "Gray Only",
    "Black Only",
  };
  char *checks[] =
  {
    "Small Checks",
    "Medium Checks",
    "Large Checks",
  };
  int transparency_vals[] =
  {
    LIGHT_CHECKS,
    GRAY_CHECKS,
    DARK_CHECKS,
    WHITE_ONLY,
    GRAY_ONLY,
    BLACK_ONLY,
  };
  int check_vals[] =
  {
    SMALL_CHECKS,
    MEDIUM_CHECKS,
    LARGE_CHECKS,
  };
  struct {
    char *label;
    char **mpath;
  } dirs[] =
    {
      {"Temp dir:", &edit_temp_path},
      {"Swap dir:", &edit_swap_path},
      {"Brushes dir:", &edit_brush_path},
      {"Gradients dir:", &edit_gradient_path},
      {"Patterns dir:", &edit_pattern_path},
      {"Palette dir:", &edit_palette_path},
      {"Plug-in dir:", &edit_plug_in_path}
    };
    struct {
      char *label;
      int size;
    } preview_sizes[] =
    {
      {"None",0},
      {"Small",32},
      {"Medium",64},
      {"Large",128}
    };
  int ntransparencies = sizeof (transparencies) / sizeof (transparencies[0]);
  int nchecks = sizeof (checks) / sizeof (checks[0]);
  int ndirs = sizeof(dirs) / sizeof (dirs[0]);
  int npreview_sizes = sizeof(preview_sizes) / sizeof (preview_sizes[0]);
  int i;

  if (prefs_dlg)
    {
      gtk_window_present (GTK_WINDOW (prefs_dlg));
      return;
    }

  if (edit_temp_path == NULL)
    {
      /* first time dialog is opened - copy config vals to edit
	 variables. */
      edit_temp_path = file_prefs_strdup (temp_path);
      edit_swap_path = file_prefs_strdup (swap_path);
      edit_brush_path = file_prefs_strdup (brush_path);
      edit_pattern_path = file_prefs_strdup (pattern_path);
      edit_palette_path = file_prefs_strdup (palette_path);
      edit_plug_in_path = file_prefs_strdup (plug_in_path);
      edit_gradient_path = file_prefs_strdup (gradient_path);
      edit_stingy_memory_use = stingy_memory_use;
      edit_tile_cache_size = tile_cache_size;
      edit_install_cmap = install_cmap;
      edit_cycled_marching_ants = cycled_marching_ants;
    }
  old_transparency_type = transparency_type;
  old_transparency_size = transparency_size;
  old_levels_of_undo = levels_of_undo;
  old_marching_speed = marching_speed;
  old_allow_resize_windows = allow_resize_windows;
  old_auto_save = auto_save;
  old_preview_size = preview_size;
  old_no_cursor_updating = no_cursor_updating;
  old_show_tool_tips = show_tool_tips;
  old_cubic_interpolation = cubic_interpolation;
  old_confirm_on_close = confirm_on_close;
  old_default_width = default_width;
  old_default_height = default_height;
  old_default_type = default_type;
  old_stingy_memory_use = edit_stingy_memory_use;
  old_tile_cache_size = edit_tile_cache_size;
  old_install_cmap = edit_install_cmap;
  old_cycled_marching_ants = edit_cycled_marching_ants;
  file_prefs_strset (&old_temp_path, edit_temp_path);
  file_prefs_strset (&old_swap_path, edit_swap_path);
  file_prefs_strset (&old_brush_path, edit_brush_path);
  file_prefs_strset (&old_pattern_path, edit_pattern_path);
  file_prefs_strset (&old_palette_path, edit_palette_path);
  file_prefs_strset (&old_plug_in_path, edit_plug_in_path);
  file_prefs_strset (&old_gradient_path, edit_gradient_path);

  prefs_dlg = gimp_dialog_new ("Preferences");

  /* handle the wm close signal */
  g_signal_connect (prefs_dlg, "close-request",
		    G_CALLBACK (file_prefs_delete_callback),
		    prefs_dlg);

  /* Action area */
  gimp_dialog_add_button (prefs_dlg, "OK",
			  G_CALLBACK (file_prefs_ok_callback), prefs_dlg,
			  FALSE);
  gimp_dialog_add_button (prefs_dlg, "Save",
			  G_CALLBACK (file_prefs_save_callback), prefs_dlg,
			  TRUE);
  gimp_dialog_add_button (prefs_dlg, "Cancel",
			  G_CALLBACK (file_prefs_cancel_callback), prefs_dlg,
			  FALSE);

  notebook = gtk_notebook_new ();
  gimp_box_pack_start (gimp_dialog_get_vbox (prefs_dlg), notebook, TRUE, TRUE, 0);

  /* Display page */
  vbox = file_prefs_page_new (notebook, "Display settings", "Display");

  hbox = gimp_hbox_new (FALSE, 2);
  gimp_box_pack_start (vbox, hbox, TRUE, TRUE, 0);

  frame = gtk_frame_new ("Image size");
  gimp_box_pack_start (hbox, frame, TRUE, TRUE, 0);

  abox = gimp_vbox_new (FALSE, 2);
  gimp_container_set_border_width (abox, 1);
  gtk_frame_set_child (GTK_FRAME (frame), abox);

  table = gimp_table_new (2, 2, FALSE);
  gtk_grid_set_row_spacing (GTK_GRID (table), 2);
  gtk_grid_set_column_spacing (GTK_GRID (table), 2);
  gimp_box_pack_start (abox, table, TRUE, TRUE, 0);

  label = gtk_label_new ("Width:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 0, 1,
		     GIMP_FILL, GIMP_FILL, 0, 0);

  label = gtk_label_new ("Height:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 1, 2,
		     GIMP_FILL, GIMP_FILL, 0, 0);

  entry = gtk_entry_new ();
  gtk_widget_set_size_request (entry, 25, -1);
  gtk_editable_set_width_chars (GTK_EDITABLE (entry), 5);
  g_snprintf (buffer, sizeof (buffer), "%d", default_width);
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
  gimp_table_attach (table, entry, 1, 2, 0, 1,
		     GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 0, 0);
  g_signal_connect (entry, "changed",
		    G_CALLBACK (file_prefs_text_callback),
		    &default_width);

  entry = gtk_entry_new ();
  gtk_widget_set_size_request (entry, 25, -1);
  gtk_editable_set_width_chars (GTK_EDITABLE (entry), 5);
  g_snprintf (buffer, sizeof (buffer), "%d", default_height);
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
  gimp_table_attach (table, entry, 1, 2, 1, 2,
		     GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 0, 0);
  g_signal_connect (entry, "changed",
		    G_CALLBACK (file_prefs_text_callback),
		    &default_height);

  frame = gtk_frame_new ("Image type");
  gimp_box_pack_start (hbox, frame, TRUE, TRUE, 0);

  radio_box = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (radio_box, 2);
  gtk_frame_set_child (GTK_FRAME (frame), radio_box);

  button = file_prefs_radio_new (radio_box, NULL, "RGB", RGB, &default_type);
  file_prefs_radio_new (radio_box, button, "Grayscale", GRAY, &default_type);

  hbox = gimp_hbox_new (FALSE, 2);
  gimp_box_pack_start (vbox, hbox, TRUE, TRUE, 0);

  label = gtk_label_new ("Preview size:");
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);

  optionmenu = gimp_option_menu_new ();
  for (i = 0; i < npreview_sizes; i++)
    gimp_option_menu_append (optionmenu, preview_sizes[i].label,
			     G_CALLBACK (file_prefs_preview_size_callback),
			     GINT_TO_POINTER (preview_sizes[i].size));
  gimp_box_pack_start (hbox, optionmenu, TRUE, TRUE, 0);
  for (i = 0; i < npreview_sizes; i++)
    if (preview_size==preview_sizes[i].size)
      gimp_option_menu_set_history (optionmenu, i);

  file_prefs_check_new (vbox, "Cubic interpolation", cubic_interpolation,
			&cubic_interpolation, TRUE);

  hbox = gimp_hbox_new (FALSE, 2);
  gimp_box_pack_start (vbox, hbox, TRUE, TRUE, 0);

  frame = gtk_frame_new ("Transparency Type");
  gimp_box_pack_start (hbox, frame, TRUE, TRUE, 0);

  radio_box = gimp_vbox_new (FALSE, 2);
  gimp_container_set_border_width (radio_box, 2);
  gtk_frame_set_child (GTK_FRAME (frame), radio_box);

  group = NULL;
  for (i = 0; i < ntransparencies; i++)
    {
      button = file_prefs_radio_new (radio_box, group, transparencies[i],
				     transparency_vals[i], &transparency_type);
      if (!group)
	group = button;
    }

  frame = gtk_frame_new ("Check Size");
  gimp_box_pack_start (hbox, frame, TRUE, TRUE, 0);

  radio_box = gimp_vbox_new (FALSE, 2);
  gimp_container_set_border_width (radio_box, 2);
  gtk_frame_set_child (GTK_FRAME (frame), radio_box);

  group = NULL;
  for (i = 0; i < nchecks; i++)
    {
      button = file_prefs_radio_new (radio_box, group, checks[i],
				     check_vals[i], &transparency_size);
      if (!group)
	group = button;
    }

  /* Interface */
  vbox = file_prefs_page_new (notebook, "Interface settings", "Interface");

  file_prefs_int_entry_new (vbox, "Levels of undo:", levels_of_undo,
			    &levels_of_undo);

  file_prefs_check_new (vbox, "Resize window on zoom", allow_resize_windows,
			&allow_resize_windows, FALSE);

  /* Don't show the Auto-save button until we really
     have auto-saving in the gimp.

     file_prefs_check_new (vbox, "Auto save", auto_save, &auto_save, FALSE);
  */

  file_prefs_check_new (vbox, "Disable cursor updating", no_cursor_updating,
			&no_cursor_updating, FALSE);

  file_prefs_check_new (vbox, "Show tool tips", show_tool_tips,
			&show_tool_tips, FALSE);

  file_prefs_int_entry_new (vbox, "Marching ants speed:", marching_speed,
			    &marching_speed);

  /* Environment */
  vbox = file_prefs_page_new (notebook, "Environment settings", "Environment");
  gtk_widget_set_size_request (gtk_widget_get_parent (vbox), 320, 200);

  file_prefs_check_new (vbox, "Conservative memory usage", stingy_memory_use,
			&edit_stingy_memory_use, FALSE);

  file_prefs_int_entry_new (vbox, "Tile cache size (bytes):",
			    old_tile_cache_size, &edit_tile_cache_size);

  file_prefs_check_new (vbox, "Install colormap (8-bit only)", install_cmap,
			&edit_install_cmap, FALSE);

  button = file_prefs_check_new (vbox, "Colormap cycling (8-bit only)",
				 cycled_marching_ants,
				 &edit_cycled_marching_ants, FALSE);
  /*  there are no 8-bit visuals any more  */
  gtk_widget_set_sensitive (button, FALSE);

  /* Directories */
  vbox = file_prefs_page_new (notebook, "Directories settings", "Directories");
  gtk_widget_set_size_request (gtk_widget_get_parent (vbox), 320, 200);

  table = gimp_table_new (ndirs+1, 2, FALSE);
  gtk_grid_set_row_spacing (GTK_GRID (table), 2);
  gtk_grid_set_column_spacing (GTK_GRID (table), 2);
  gimp_box_pack_start (vbox, table, TRUE, TRUE, 0);

  for (i = 0; i < ndirs; i++)
    {
      label = gtk_label_new (dirs[i].label);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gimp_table_attach (table, label, 0, 1, i, i+1,
			 GIMP_FILL, GIMP_FILL, 0, 0);

      entry = gtk_entry_new ();
      gtk_widget_set_size_request (entry, 25, -1);
      gtk_editable_set_text (GTK_EDITABLE (entry), *(dirs[i].mpath));
      g_signal_connect (entry, "changed",
			G_CALLBACK (file_prefs_string_callback),
			dirs[i].mpath);
      gimp_table_attach (table, entry, 1, 2, i, i+1,
			 GIMP_EXPAND | GIMP_FILL, 0, 0, 0);
    }

  gtk_window_present (GTK_WINDOW (prefs_dlg));
}

void
file_close_cmd_callback (GtkWidget *widget,
			 gpointer   client_data)
{
  GDisplay *gdisp;

  gdisp = gdisplay_active ();

  gdisplay_close_window (gdisp, FALSE);
}

void
file_quit_cmd_callback (GtkWidget *widget,
			gpointer   client_data)
{
  app_exit (0);
}

void
edit_cut_cmd_callback (GtkWidget *widget,
		       gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  global_edit_cut (gdisp);
}

void
edit_copy_cmd_callback (GtkWidget *widget,
			gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  global_edit_copy (gdisp);
}

void
edit_paste_cmd_callback (GtkWidget *widget,
			 gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  global_edit_paste (gdisp, 0);
}

void
edit_paste_into_cmd_callback (GtkWidget *widget,
			      gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  global_edit_paste (gdisp, 1);
}

void
edit_clear_cmd_callback (GtkWidget *widget,
			 gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  edit_clear (gdisp->gimage, gimage_active_drawable (gdisp->gimage));

  gdisplays_flush ();
}

void
edit_fill_cmd_callback (GtkWidget *widget,
			gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  edit_fill (gdisp->gimage, gimage_active_drawable (gdisp->gimage));

  gdisplays_flush ();
}

void
edit_stroke_cmd_callback (GtkWidget *widget,
			  gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  gimage_mask_stroke (gdisp->gimage, gimage_active_drawable (gdisp->gimage));

  gdisplays_flush ();
}

void
edit_undo_cmd_callback (GtkWidget *widget,
			gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  undo_pop (gdisp->gimage);
}

void
edit_redo_cmd_callback (GtkWidget *widget,
			gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  undo_redo (gdisp->gimage);
}

void
edit_named_cut_cmd_callback (GtkWidget *widget,
			     gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  named_edit_cut (gdisp);
}

void
edit_named_copy_cmd_callback (GtkWidget *widget,
			      gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  named_edit_copy (gdisp);
}

void
edit_named_paste_cmd_callback (GtkWidget *widget,
			       gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  named_edit_paste (gdisp);
}

void
select_toggle_cmd_callback (GtkWidget *widget,
			    gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  selection_hide (gdisp->select, (void *) gdisp);
  gdisplays_flush ();
}

void
select_invert_cmd_callback (GtkWidget *widget,
			    gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  gimage_mask_invert (gdisp->gimage);
  gdisplays_flush ();
}

void
select_all_cmd_callback (GtkWidget *widget,
			 gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  gimage_mask_all (gdisp->gimage);
  gdisplays_flush ();
}

void
select_none_cmd_callback (GtkWidget *widget,
			  gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  gimage_mask_none (gdisp->gimage);
  gdisplays_flush ();
}

void
select_float_cmd_callback (GtkWidget *widget,
			   gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  gimage_mask_float (gdisp->gimage, gimage_active_drawable (gdisp->gimage), 0, 0);
  gdisplays_flush ();
}

void
select_sharpen_cmd_callback (GtkWidget *widget,
			     gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  gimage_mask_sharpen (gdisp->gimage);
  gdisplays_flush ();
}

void
select_border_cmd_callback (GtkWidget *widget,
			    gpointer   client_data)
{
  GDisplay * gdisp;
  char initial[16];

  gdisp = gdisplay_active ();

  g_snprintf (initial, sizeof (initial), "%d", gimage_mask_border_radius);
  query_string_box ("Border Selection", "Border selection by:", initial,
		    gimage_mask_border_callback, GINT_TO_POINTER (gdisp->gimage->ID));
}

void
select_feather_cmd_callback (GtkWidget *widget,
			     gpointer   client_data)
{
  GDisplay * gdisp;
  char initial[16];

  gdisp = gdisplay_active ();

  g_snprintf (initial, sizeof (initial), "%f", gimage_mask_feather_radius);
  query_string_box ("Feather Selection", "Feather selection by:", initial,
		    gimage_mask_feather_callback, GINT_TO_POINTER (gdisp->gimage->ID));
}

void
select_grow_cmd_callback (GtkWidget *widget,
			  gpointer   client_data)
{
  GDisplay * gdisp;
  char initial[16];

  gdisp = gdisplay_active ();

  g_snprintf (initial, sizeof (initial), "%d", gimage_mask_grow_pixels);
  query_string_box ("Grow Selection", "Grow selection by:", initial,
		    gimage_mask_grow_callback, GINT_TO_POINTER (gdisp->gimage->ID));
}

void
select_shrink_cmd_callback (GtkWidget *widget,
			    gpointer   client_data)
{
  GDisplay * gdisp;
  char initial[16];

  gdisp = gdisplay_active ();

  g_snprintf (initial, sizeof (initial), "%d", gimage_mask_shrink_pixels);
  query_string_box ("Shrink Selection", "Shrink selection by:", initial,
		    gimage_mask_shrink_callback, GINT_TO_POINTER (gdisp->gimage->ID));
}

void
select_by_color_cmd_callback (GtkWidget *widget,
			      gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  tools_select_widget (BY_COLOR_SELECT);
  by_color_select_initialize ((void *) gdisp->gimage);

  gdisp = gdisplay_active ();

  active_tool->drawable = gimage_active_drawable (gdisp->gimage);
}

void
select_save_cmd_callback (GtkWidget *widget,
			  gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  gimage_mask_save (gdisp->gimage);
  gdisplays_flush ();
}

void
view_zoomin_cmd_callback (GtkWidget *widget,
			  gpointer   client_data)
{
  GDisplay *gdisp;

  gdisp = gdisplay_active ();

  change_scale (gdisp, ZOOMIN);
}

void
view_zoomout_cmd_callback (GtkWidget *widget,
			   gpointer   client_data)
{
  GDisplay *gdisp;

  gdisp = gdisplay_active ();

  change_scale (gdisp, ZOOMOUT);
}

static void
view_zoom_val (GtkWidget *widget,
	       gpointer   client_data,
	       int        val)
{
  GDisplay *gdisp;

  gdisp = gdisplay_active ();

  change_scale (gdisp, val);
}

void
view_zoom_16_1_callback (GtkWidget *widget,
			 gpointer   client_data)
{
  view_zoom_val (widget, client_data, 1601);
}

void
view_zoom_8_1_callback (GtkWidget *widget,
			gpointer   client_data)
{
  view_zoom_val (widget, client_data, 801);
}

void
view_zoom_4_1_callback (GtkWidget *widget,
			gpointer   client_data)
{
  view_zoom_val (widget, client_data, 401);
}

void
view_zoom_2_1_callback (GtkWidget *widget,
			gpointer   client_data)
{
  view_zoom_val (widget, client_data, 201);
}

void
view_zoom_1_1_callback (GtkWidget *widget,
			gpointer   client_data)
{
  view_zoom_val (widget, client_data, 101);
}

void
view_zoom_1_2_callback (GtkWidget *widget,
			gpointer   client_data)
{
  view_zoom_val (widget, client_data, 102);
}

void
view_zoom_1_4_callback (GtkWidget *widget,
			gpointer   client_data)
{
  view_zoom_val (widget, client_data, 104);
}

void
view_zoom_1_8_callback (GtkWidget *widget,
			gpointer   client_data)
{
  view_zoom_val (widget, client_data, 108);
}

void
view_zoom_1_16_callback (GtkWidget *widget,
			 gpointer   client_data)
{
  view_zoom_val (widget, client_data, 116);
}

void
view_window_info_cmd_callback (GtkWidget *widget,
			       gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  if (! gdisp->window_info_dialog)
    gdisp->window_info_dialog = info_window_create ((void *) gdisp);

  info_dialog_popup (gdisp->window_info_dialog);
}

void
view_toggle_rulers_cmd_callback (GtkWidget *widget,
				 gpointer   client_data)
{
  GDisplay * gdisp;

  int show;

  gdisp = gdisplay_active ();
  if (!gdisp)
    return;

  show = menus_get_state ("<Image>/View/Toggle Rulers");

  if (show != gtk_widget_get_visible (gdisp->origin))
    {
      gtk_widget_set_visible (gdisp->origin, show);
      gtk_widget_set_visible (gdisp->hrule, show);
      gtk_widget_set_visible (gdisp->vrule, show);
    }
}

void
view_toggle_guides_cmd_callback (GtkWidget *widget,
				 gpointer   client_data)
{
  GDisplay * gdisp;
  int old_val;

  gdisp = gdisplay_active ();
  if (!gdisp)
    return;

  old_val = gdisp->draw_guides;
  gdisp->draw_guides = menus_get_state ("<Image>/View/Toggle Guides");

  if ((old_val != gdisp->draw_guides) && gdisp->gimage->guides)
    {
      gdisplay_expose_full (gdisp);
      gdisplays_flush ();
    }
}

void
view_snap_to_guides_cmd_callback (GtkWidget *widget,
				  gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  if (!gdisp)
    return;

  gdisp->snap_to_guides = menus_get_state ("<Image>/View/Snap To Guides");
}

void
view_new_view_cmd_callback (GtkWidget *widget,
			    gpointer   client_data)
{
  GDisplay *gdisp;

  gdisp = gdisplay_active ();

  gdisplay_new_view (gdisp);
}

void
view_shrink_wrap_cmd_callback (GtkWidget *widget,
			       gpointer   client_data)
{
  GDisplay *gdisp;

  gdisp = gdisplay_active ();

  if (gdisp)
    shrink_wrap_display (gdisp);
}

void
image_equalize_cmd_callback (GtkWidget *widget,
			     gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  image_equalize ((void *) gdisp->gimage);
  gdisplays_flush ();
}

void
image_invert_cmd_callback (GtkWidget *widget,
			   gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  image_invert ((void *) gdisp->gimage);
  gdisplays_flush ();
}

void
image_posterize_cmd_callback (GtkWidget *widget,
			      gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  tools_select_widget (POSTERIZE);
  posterize_initialize ((void *) gdisp);

  gdisp = gdisplay_active ();

  active_tool->drawable = gimage_active_drawable (gdisp->gimage);
}

void
image_threshold_cmd_callback (GtkWidget *widget,
			      gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  tools_select_widget (THRESHOLD);
  threshold_initialize ((void *) gdisp);

  gdisp = gdisplay_active ();

  active_tool->drawable = gimage_active_drawable (gdisp->gimage);
}

void
image_color_balance_cmd_callback (GtkWidget *widget,
				  gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  tools_select_widget (COLOR_BALANCE);
  color_balance_initialize ((void *) gdisp);

  gdisp = gdisplay_active ();

  active_tool->drawable = gimage_active_drawable (gdisp->gimage);

}

void
image_brightness_contrast_cmd_callback (GtkWidget *widget,
					gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  tools_select_widget (BRIGHTNESS_CONTRAST);
  brightness_contrast_initialize ((void *) gdisp);

  gdisp = gdisplay_active ();

  active_tool->drawable = gimage_active_drawable (gdisp->gimage);
}

void
image_hue_saturation_cmd_callback (GtkWidget *widget,
				   gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  tools_select_widget (HUE_SATURATION);
  hue_saturation_initialize ((void *) gdisp);

  gdisp = gdisplay_active ();

  active_tool->drawable = gimage_active_drawable (gdisp->gimage);
}

void
image_curves_cmd_callback (GtkWidget *widget,
			   gpointer client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  tools_select_widget (CURVES);
  curves_initialize ((void *) gdisp);

  gdisp = gdisplay_active ();

  active_tool->drawable = gimage_active_drawable (gdisp->gimage);
}

void
image_levels_cmd_callback (GtkWidget *widget,
			   gpointer client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  tools_select_widget (LEVELS);
  levels_initialize ((void *) gdisp);

  gdisp = gdisplay_active ();

  active_tool->drawable = gimage_active_drawable (gdisp->gimage);
}

void
image_desaturate_cmd_callback (GtkWidget *widget,
			       gpointer client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  image_desaturate ((void *) gdisp->gimage);
  gdisplays_flush ();
}

void
channel_ops_duplicate_cmd_callback (GtkWidget *widget,
				    gpointer client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  channel_ops_duplicate ((void *) gdisp->gimage);
}

void
channel_ops_offset_cmd_callback (GtkWidget *widget,
				 gpointer client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  channel_ops_offset ((void *) gdisp->gimage);
}

void
image_convert_rgb_cmd_callback (GtkWidget *widget,
				gpointer client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  convert_to_rgb ((void *) gdisp->gimage);
}

void
image_convert_grayscale_cmd_callback (GtkWidget *widget,
				      gpointer client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  convert_to_grayscale ((void *) gdisp->gimage);
}

void
image_convert_indexed_cmd_callback (GtkWidget *widget,
				    gpointer client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  convert_to_indexed ((void *) gdisp->gimage);
}

void
image_resize_cmd_callback (GtkWidget *widget,
			   gpointer client_data)
{
  static ActionAreaItem action_items[2] =
  {
    { "OK", image_resize_callback, NULL, NULL },
    { "Cancel", image_cancel_callback, NULL, NULL }
  };
  GDisplay * gdisp;
  GtkWidget *vbox;
  ImageResize *image_resize;

  gdisp = gdisplay_active ();

  /*  the ImageResize structure  */
  image_resize = (ImageResize *) g_malloc (sizeof (ImageResize));
  image_resize->gimage_id = gdisp->gimage->ID;
  image_resize->resize = resize_widget_new (ResizeWidget, gdisp->gimage->width, gdisp->gimage->height);

  /*  the dialog  */
  image_resize->shell = gimp_dialog_new ("Image Resize");
  gtk_window_set_resizable (GTK_WINDOW (image_resize->shell), FALSE);

  /* handle the wm close signal */
  g_signal_connect (image_resize->shell, "close-request",
		    G_CALLBACK (image_delete_callback),
		    image_resize);

  /*  the main vbox  */
  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, 1);
  gimp_box_pack_start (gimp_dialog_get_vbox (image_resize->shell), vbox, TRUE, TRUE, 0);
  gimp_box_pack_start (vbox, image_resize->resize->resize_widget, FALSE, FALSE, 0);

  action_items[0].user_data = image_resize;
  action_items[1].user_data = image_resize;
  build_action_area (image_resize->shell, action_items, 2, 0);

  gtk_window_present (GTK_WINDOW (image_resize->shell));
}

void
image_scale_cmd_callback (GtkWidget *widget,
			  gpointer client_data)
{
  static ActionAreaItem action_items[2] =
  {
    { "OK", image_scale_callback, NULL, NULL },
    { "Cancel", image_cancel_callback, NULL, NULL }
  };
  GDisplay * gdisp;
  GtkWidget *vbox;
  ImageResize *image_scale;

  gdisp = gdisplay_active ();

  /*  the ImageResize structure  */
  image_scale = (ImageResize *) g_malloc (sizeof (ImageResize));
  image_scale->gimage_id = gdisp->gimage->ID;
  image_scale->resize = resize_widget_new (ScaleWidget, gdisp->gimage->width, gdisp->gimage->height);

  /*  the dialog  */
  image_scale->shell = gimp_dialog_new ("Image Scale");
  gtk_window_set_resizable (GTK_WINDOW (image_scale->shell), FALSE);

  /* handle the wm close signal */
  g_signal_connect (image_scale->shell, "close-request",
		    G_CALLBACK (image_delete_callback),
		    image_scale);

  /*  the main vbox  */
  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, 1);
  gimp_box_pack_start (gimp_dialog_get_vbox (image_scale->shell), vbox, TRUE, TRUE, 0);
  gimp_box_pack_start (vbox, image_scale->resize->resize_widget, FALSE, FALSE, 0);

  action_items[0].user_data = image_scale;
  action_items[1].user_data = image_scale;
  build_action_area (image_scale->shell, action_items, 2, 0);

  gtk_window_present (GTK_WINDOW (image_scale->shell));
}

void
image_histogram_cmd_callback (GtkWidget *widget,
			      gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();
  tools_select_widget (HISTOGRAM);
  histogram_tool_initialize ((void *) gdisp);

  gdisp = gdisplay_active ();

  active_tool->drawable = gimage_active_drawable (gdisp->gimage);
}

void
layers_raise_cmd_callback (GtkWidget *widget,
			   gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  gimage_raise_layer (gdisp->gimage, gdisp->gimage->active_layer);
  gdisplays_flush ();
}

void
layers_lower_cmd_callback (GtkWidget *widget,
			   gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  gimage_lower_layer (gdisp->gimage, gdisp->gimage->active_layer);
  gdisplays_flush ();
}

void
layers_anchor_cmd_callback (GtkWidget *widget,
			    gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  floating_sel_anchor (gimage_get_active_layer (gdisp->gimage));
  gdisplays_flush ();
}

void
layers_merge_cmd_callback (GtkWidget *widget,
			   gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  layers_dialog_layer_merge_query (gdisp->gimage, TRUE);
}

void
layers_flatten_cmd_callback (GtkWidget *widget,
			     gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  gimage_flatten (gdisp->gimage);
  gdisplays_flush ();
}

void
layers_alpha_select_cmd_callback (GtkWidget *widget,
				  gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  gimage_mask_layer_alpha (gdisp->gimage, gdisp->gimage->active_layer);
  gdisplays_flush ();
}

void
layers_mask_select_cmd_callback (GtkWidget *widget,
				 gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  gimage_mask_layer_mask (gdisp->gimage, gdisp->gimage->active_layer);
  gdisplays_flush ();
}

void
layers_add_alpha_channel_cmd_callback (GtkWidget *widget,
				       gpointer   client_data)
{
  GDisplay * gdisp;

  gdisp = gdisplay_active ();

  layer_add_alpha ( gdisp->gimage->active_layer);
  gdisplays_flush ();
}

void
tools_default_colors_cmd_callback (GtkWidget *widget,
				   gpointer   client_data)
{
  palette_set_default_colors ();
}

void
tools_swap_colors_cmd_callback (GtkWidget *widget,
				gpointer   client_data)
{
  palette_swap_colors ();
}

void
tools_select_cmd_callback (GtkWidget           *widget,
			   gpointer             callback_data,
			   guint                callback_action)
{
  GDisplay * gdisp;

  /*  Activate the approriate widget  */
  tools_select_widget (callback_action);

  gdisp = gdisplay_active ();

  active_tool->drawable = gimage_active_drawable (gdisp->gimage);
}

void
filters_repeat_cmd_callback (GtkWidget           *widget,
			     gpointer             callback_data,
			     guint                callback_action)
{
  plug_in_repeat (callback_action);
}

void
dialogs_brushes_cmd_callback (GtkWidget *widget,
			      gpointer   client_data)
{
  create_brush_dialog ();
}

void
dialogs_patterns_cmd_callback (GtkWidget *widget,
			       gpointer   client_data)
{
  create_pattern_dialog ();
}

void
dialogs_palette_cmd_callback (GtkWidget *widget,
			      gpointer   client_data)
{
  palette_create ();
}

void
dialogs_gradient_editor_cmd_callback(GtkWidget *widget,
				     gpointer   client_data)
{
  grad_create_gradient_editor ();
} /* dialogs_gradient_editor_cmd_callback */

void
dialogs_lc_cmd_callback (GtkWidget *widget,
			 gpointer   client_data)
{
  GDisplay *gdisp;

  gdisp = gdisplay_active ();

  lc_dialog_create (gdisp->gimage->ID);
}

void
dialogs_indexed_palette_cmd_callback (GtkWidget *widget,
				      gpointer   client_data)
{
  GDisplay *gdisp;

  gdisp = gdisplay_active ();

  indexed_palette_create (gdisp->gimage->ID);
}

void
dialogs_tools_options_cmd_callback (GtkWidget *widget,
				    gpointer   client_data)
{
  tools_options_dialog_show ();
}

void
about_dialog_cmd_callback (GtkWidget *widget,
                           gpointer   client_data)
{
  about_dialog_create (FALSE);
}

void
tips_dialog_cmd_callback (GtkWidget *widget,
			  gpointer   client_data)
{
  tips_dialog_create ();
}


/****************************************************/
/**           LOCAL FUNCTIONS                      **/
/****************************************************/


/*********************/
/*  Local functions  */
/*********************/

static void
image_resize_callback (GtkWidget *w,
		       gpointer   client_data)
{
  ImageResize *image_resize;
  GImage *gimage;

  image_resize = (ImageResize *) client_data;
  if ((gimage = gimage_get_ID (image_resize->gimage_id)) != NULL)
    {
      if (image_resize->resize->width > 0 &&
	  image_resize->resize->height > 0)
	{
	  gimage_resize (gimage,
			 image_resize->resize->width,
			 image_resize->resize->height,
			 image_resize->resize->off_x,
			 image_resize->resize->off_y);
	  gdisplays_flush ();
	}
      else
	{
	  g_message ("Resize Error: Both width and height must be greater than zero.");
	  return;
	}
    }

  gtk_window_destroy (GTK_WINDOW (image_resize->shell));
  resize_widget_free (image_resize->resize);
  g_free (image_resize);
}

static void
image_scale_callback (GtkWidget *w,
		      gpointer   client_data)
{
  ImageResize *image_scale;
  GImage *gimage;

  image_scale = (ImageResize *) client_data;
  if ((gimage = gimage_get_ID (image_scale->gimage_id)) != NULL)
    {
      if (image_scale->resize->width > 0 &&
	  image_scale->resize->height > 0)
	{
	  gimage_scale (gimage,
			image_scale->resize->width,
			image_scale->resize->height);
	  gdisplays_flush ();
	}
      else
	{
	  g_message ("Scale Error: Both width and height must be greater than zero.");
	  return;
	}
    }

  gtk_window_destroy (GTK_WINDOW (image_scale->shell));
  resize_widget_free (image_scale->resize);
  g_free (image_scale);
}

static gboolean
image_delete_callback (GtkWindow *w,
		       gpointer client_data)
{
  image_cancel_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}


static void
image_cancel_callback (GtkWidget *w,
		       gpointer   client_data)
{
  ImageResize *image_resize;

  image_resize = (ImageResize *) client_data;

  gtk_window_destroy (GTK_WINDOW (image_resize->shell));
  resize_widget_free (image_resize->resize);
  g_free (image_resize);
}

static void
gimage_mask_feather_callback (GtkWidget *w,
			      gpointer   client_data,
			      gpointer   call_data)
{
  GImage *gimage;
  double feather_radius;

  if (!(gimage = gimage_get_ID (GPOINTER_TO_INT (client_data))))
    return;

  feather_radius = atof (call_data);

  gimage_mask_feather (gimage, feather_radius);
  gdisplays_flush ();
}


static void
gimage_mask_border_callback (GtkWidget *w,
			     gpointer   client_data,
			     gpointer   call_data)
{
  GImage *gimage;
  int border_radius;

  if (!(gimage = gimage_get_ID (GPOINTER_TO_INT (client_data))))
    return;

  border_radius = atoi (call_data);

  gimage_mask_border (gimage, border_radius);
  gdisplays_flush ();
}


static void
gimage_mask_grow_callback (GtkWidget *w,
			   gpointer   client_data,
			   gpointer   call_data)
{
  GImage *gimage;
  int grow_pixels;

  if (!(gimage = gimage_get_ID (GPOINTER_TO_INT (client_data))))
    return;

  grow_pixels = atoi (call_data);

  gimage_mask_grow (gimage, grow_pixels);
  gdisplays_flush ();
}


static void
gimage_mask_shrink_callback (GtkWidget *w,
			     gpointer   client_data,
			     gpointer   call_data)
{
  GImage *gimage;
  int shrink_pixels;

  if (!(gimage = gimage_get_ID (GPOINTER_TO_INT (client_data))))
    return;

  shrink_pixels = atoi (call_data);

  gimage_mask_shrink (gimage, shrink_pixels);
  gdisplays_flush ();
}
