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
#include <stdio.h>
#include <stdlib.h>
#include "appenv.h"
#include "color_select.h"
#include "colormaps.h"
#include "errors.h"
#include "gimprc.h"

#define XY_DEF_WIDTH       192
#define XY_DEF_HEIGHT      192
#define Z_DEF_WIDTH        15
#define Z_DEF_HEIGHT       192
#define COLOR_AREA_WIDTH   74
#define COLOR_AREA_HEIGHT  20

typedef enum {
  HUE = 0,
  SATURATION,
  VALUE,
  RED,
  GREEN,
  BLUE,
  HUE_SATURATION,
  HUE_VALUE,
  SATURATION_VALUE,
  RED_GREEN,
  RED_BLUE,
  GREEN_BLUE
} ColorSelectFillType;

typedef enum {
  UPDATE_VALUES = 1 << 0,
  UPDATE_POS = 1 << 1,
  UPDATE_XY_COLOR = 1 << 2,
  UPDATE_Z_COLOR = 1 << 3,
  UPDATE_NEW_COLOR = 1 << 4,
  UPDATE_ORIG_COLOR = 1 << 5,
  UPDATE_CALLER = 1 << 6
} ColorSelectUpdateType;

typedef struct _ColorSelectFill ColorSelectFill;
typedef void (*ColorSelectFillUpdateProc) (ColorSelectFill *);

struct _ColorSelectFill {
  unsigned char *buffer;
  int y;
  int width;
  int height;
  int *values;
  ColorSelectFillUpdateProc update;
};

static void color_select_update (ColorSelectP, ColorSelectUpdateType);
static void color_select_update_caller (ColorSelectP);
static void color_select_update_values (ColorSelectP);
static void color_select_update_rgb_values (ColorSelectP);
static void color_select_update_hsv_values (ColorSelectP);
static void color_select_update_pos (ColorSelectP);
static void color_select_update_sliders (ColorSelectP, int);
static void color_select_update_entries (ColorSelectP, int);
static void color_select_update_colors (ColorSelectP, int);

static void color_select_ok_callback (GtkWidget *, gpointer);
static void color_select_cancel_callback (GtkWidget *, gpointer);
static gboolean color_select_delete_callback (GtkWindow *, gpointer);
static void color_select_xy_marker_draw (GtkDrawingArea *, cairo_t *, int, int, gpointer);
static void color_select_xy_set_pos (ColorSelectP, double, double);
static void color_select_xy_pressed (GtkGestureDrag *, double, double, gpointer);
static void color_select_xy_motion (GtkGestureDrag *, double, double, gpointer);
static void color_select_xy_released (GtkGestureDrag *, double, double, gpointer);
static void color_select_z_marker_draw (GtkDrawingArea *, cairo_t *, int, int, gpointer);
static void color_select_z_set_pos (ColorSelectP, double);
static void color_select_z_pressed (GtkGestureDrag *, double, double, gpointer);
static void color_select_z_motion (GtkGestureDrag *, double, double, gpointer);
static void color_select_z_released (GtkGestureDrag *, double, double, gpointer);
static void color_select_color_draw (GtkDrawingArea *, cairo_t *, int, int, gpointer);
static void color_select_slider_update (GtkAdjustment *, gpointer);
static void color_select_entry_update (GtkWidget *, gpointer);
static void color_select_toggle_update (GtkWidget *, gpointer);

static void color_select_image_fill (GtkWidget *, ColorSelectFillType, int *);
static void color_select_paint_inverted (cairo_t *, GtkWidget *,
					 int, int, int, int, int, int);

static void color_select_draw_z_marker (ColorSelectP);
static void color_select_draw_xy_marker (ColorSelectP);

static void color_select_update_red (ColorSelectFill *);
static void color_select_update_green (ColorSelectFill *);
static void color_select_update_blue (ColorSelectFill *);
static void color_select_update_hue (ColorSelectFill *);
static void color_select_update_saturation (ColorSelectFill *);
static void color_select_update_value (ColorSelectFill *);
static void color_select_update_red_green (ColorSelectFill *);
static void color_select_update_red_blue (ColorSelectFill *);
static void color_select_update_green_blue (ColorSelectFill *);
static void color_select_update_hue_saturation (ColorSelectFill *);
static void color_select_update_hue_value (ColorSelectFill *);
static void color_select_update_saturation_value (ColorSelectFill *);

static ColorSelectFillUpdateProc update_procs[] =
{
  color_select_update_hue,
  color_select_update_saturation,
  color_select_update_value,
  color_select_update_red,
  color_select_update_green,
  color_select_update_blue,
  color_select_update_hue_saturation,
  color_select_update_hue_value,
  color_select_update_saturation_value,
  color_select_update_red_green,
  color_select_update_red_blue,
  color_select_update_green_blue,
};

ColorSelectP
color_select_new (int                  r,
		  int                  g,
		  int                  b,
		  ColorSelectCallback  callback,
		  void                *client_data,
		  int                  wants_updates)
{
  /*  static char *toggle_titles[6] = { "Hue", "Saturation", "Value", "Red", "Green", "Blue" }; */
  static char *toggle_titles[6] = { "H", "S", "V", "R", "G", "B" };
  static gfloat slider_max_vals[6] = { 360, 100, 100, 255, 255, 255 };
  static gfloat slider_incs[6] = { 0.1, 0.1, 0.1, 1.0, 1.0, 1.0 };

  ColorSelectP csp;
  GtkWidget *main_vbox;
  GtkWidget *main_hbox;
  GtkWidget *xy_frame;
  GtkWidget *z_frame;
  GtkWidget *overlay;
  GtkWidget *colors_frame;
  GtkWidget *colors_hbox;
  GtkWidget *right_vbox;
  GtkWidget *table;
  GtkWidget *slider;
  GtkWidget *group;
  GtkGesture *drag;
  char buffer[16];
  int i;

  csp = g_malloc (sizeof (_ColorSelect));

  csp->callback = callback;
  csp->client_data = client_data;
  csp->z_color_fill = HUE;
  csp->xy_color_fill = SATURATION_VALUE;
  csp->wants_updates = wants_updates;
  csp->xy_marker = NULL;
  csp->z_marker = NULL;

  csp->values[RED] = csp->orig_values[0] = r;
  csp->values[GREEN] = csp->orig_values[1] = g;
  csp->values[BLUE] = csp->orig_values[2] = b;
  color_select_update_hsv_values (csp);
  color_select_update_pos (csp);

  /*  The window position (color_select_x, color_select_y from gimprc)
   *  can no longer be chosen by the application in GTK 4.
   */
  csp->shell = gimp_dialog_new ("Color Selection");
  gtk_window_set_resizable (GTK_WINDOW (csp->shell), FALSE);

  /*  handle the wm close signal */
  g_signal_connect (csp->shell, "close-request",
		    G_CALLBACK (color_select_delete_callback), csp);

  main_vbox = gimp_vbox_new (FALSE, 2);
  gimp_container_set_border_width (main_vbox, 2);
  gimp_box_pack_start (gimp_dialog_get_vbox (csp->shell), main_vbox, TRUE, TRUE, 0);

  main_hbox = gimp_hbox_new (FALSE, 2);
  gimp_box_pack_start (main_vbox, main_hbox, TRUE, TRUE, 2);

  /*  The XY plane: the colors in a preview, the cross hair marker and
   *  the pointer handling in a drawing area laid over it
   */
  xy_frame = gtk_frame_new (NULL);
  gtk_widget_set_valign (xy_frame, GTK_ALIGN_START);
  gimp_box_pack_start (main_hbox, xy_frame, FALSE, FALSE, 2);

  overlay = gtk_overlay_new ();
  gtk_frame_set_child (GTK_FRAME (xy_frame), overlay);

  csp->xy_color = gimp_preview_new (GIMP_PREVIEW_COLOR);
  gimp_preview_size (GIMP_PREVIEW (csp->xy_color), XY_DEF_WIDTH, XY_DEF_HEIGHT);
  gtk_overlay_set_child (GTK_OVERLAY (overlay), csp->xy_color);

  csp->xy_marker = gtk_drawing_area_new ();
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (csp->xy_marker),
				  color_select_xy_marker_draw, csp, NULL);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), csp->xy_marker);

  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), 0);
  g_signal_connect (drag, "drag-begin",
		    G_CALLBACK (color_select_xy_pressed), csp);
  g_signal_connect (drag, "drag-update",
		    G_CALLBACK (color_select_xy_motion), csp);
  g_signal_connect (drag, "drag-end",
		    G_CALLBACK (color_select_xy_released), csp);
  gtk_widget_add_controller (csp->xy_marker, GTK_EVENT_CONTROLLER (drag));

  /*  The Z strip, built the same way  */
  z_frame = gtk_frame_new (NULL);
  gtk_widget_set_valign (z_frame, GTK_ALIGN_START);
  gimp_box_pack_start (main_hbox, z_frame, FALSE, FALSE, 2);

  overlay = gtk_overlay_new ();
  gtk_frame_set_child (GTK_FRAME (z_frame), overlay);

  csp->z_color = gimp_preview_new (GIMP_PREVIEW_COLOR);
  gimp_preview_size (GIMP_PREVIEW (csp->z_color), Z_DEF_WIDTH, Z_DEF_HEIGHT);
  gtk_overlay_set_child (GTK_OVERLAY (overlay), csp->z_color);

  csp->z_marker = gtk_drawing_area_new ();
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (csp->z_marker),
				  color_select_z_marker_draw, csp, NULL);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), csp->z_marker);

  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), 0);
  g_signal_connect (drag, "drag-begin",
		    G_CALLBACK (color_select_z_pressed), csp);
  g_signal_connect (drag, "drag-update",
		    G_CALLBACK (color_select_z_motion), csp);
  g_signal_connect (drag, "drag-end",
		    G_CALLBACK (color_select_z_released), csp);
  gtk_widget_add_controller (csp->z_marker, GTK_EVENT_CONTROLLER (drag));

  /*  The right vertical box with old/new color area and color space sliders  */
  right_vbox = gimp_vbox_new (FALSE, 2);
  gimp_box_pack_start (main_hbox, right_vbox, TRUE, TRUE, 0);

  /*  The old/new color area  */
  colors_frame = gtk_frame_new (NULL);
  gimp_box_pack_start (right_vbox, colors_frame, FALSE, FALSE, 0);

  colors_hbox = gimp_hbox_new (TRUE, 2);
  gtk_frame_set_child (GTK_FRAME (colors_frame), colors_hbox);

  csp->new_color = gtk_drawing_area_new ();
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (csp->new_color), COLOR_AREA_WIDTH);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (csp->new_color), COLOR_AREA_HEIGHT);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (csp->new_color),
				  color_select_color_draw, csp, NULL);
  gimp_box_pack_start (colors_hbox, csp->new_color, TRUE, TRUE, 0);

  csp->orig_color = gtk_drawing_area_new ();
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (csp->orig_color), COLOR_AREA_WIDTH);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (csp->orig_color), COLOR_AREA_HEIGHT);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (csp->orig_color),
				  color_select_color_draw, csp, NULL);
  gimp_box_pack_start (colors_hbox, csp->orig_color, TRUE, TRUE, 0);

  /*  The color space sliders, toggle buttons and entries  */
  table = gimp_table_new (6, 3, FALSE);
  gtk_grid_set_row_spacing (GTK_GRID (table), 3);
  gtk_grid_set_column_spacing (GTK_GRID (table), 3);
  gimp_container_set_border_width (table, 2);
  gimp_box_pack_start (right_vbox, table, TRUE, TRUE, 0);

  group = NULL;
  for (i = 0; i < 6; i++)
    {
      csp->toggles[i] = gimp_radio_button_new (group, toggle_titles[i]);
      group = csp->toggles[i];
      gimp_table_attach (table, csp->toggles[i],
			 0, 1, i, i+1, GIMP_FILL, GIMP_EXPAND, 0, 0);
      g_signal_connect (csp->toggles[i], "toggled",
			G_CALLBACK (color_select_toggle_update),
			csp);

      csp->slider_data[i] = gtk_adjustment_new (csp->values[i], 0.0,
						slider_max_vals[i],
						slider_incs[i],
						1.0, 0.0);

      slider = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, csp->slider_data[i]);
      gtk_scale_set_draw_value (GTK_SCALE (slider), FALSE);
      gtk_widget_set_size_request (slider, 100, -1);
      gimp_table_attach (table, slider, 1, 2, i, i+1,
			 GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND, 0, 0);
      g_signal_connect (csp->slider_data[i], "value-changed",
			G_CALLBACK (color_select_slider_update),
			csp);

      csp->entries[i] = gtk_entry_new ();
      sprintf (buffer, "%d", csp->values[i]);
      gtk_editable_set_text (GTK_EDITABLE (csp->entries[i]), buffer);
      gtk_editable_set_width_chars (GTK_EDITABLE (csp->entries[i]), 4);
      gtk_editable_set_max_width_chars (GTK_EDITABLE (csp->entries[i]), 4);
      gtk_widget_set_size_request (csp->entries[i], 40, -1);
      gimp_table_attach (table, csp->entries[i],
			 2, 3, i, i+1, GIMP_FILL, GIMP_EXPAND, 0, 0);
      g_signal_connect (csp->entries[i], "changed",
			G_CALLBACK (color_select_entry_update),
			csp);
    }

  /*  The action area  */
  if (csp->wants_updates)
    {
      gimp_dialog_add_button (csp->shell, "Close",
			      G_CALLBACK (color_select_ok_callback), csp, TRUE);
      gimp_dialog_add_button (csp->shell, "Revert to Old Color",
			      G_CALLBACK (color_select_cancel_callback), csp, FALSE);
    }
  else
    {
      gimp_dialog_add_button (csp->shell, "OK",
			      G_CALLBACK (color_select_ok_callback), csp, TRUE);
      gimp_dialog_add_button (csp->shell, "Cancel",
			      G_CALLBACK (color_select_cancel_callback), csp, FALSE);
    }

  color_select_image_fill (csp->z_color, csp->z_color_fill, csp->values);
  color_select_image_fill (csp->xy_color, csp->xy_color_fill, csp->values);

  gtk_window_present (GTK_WINDOW (csp->shell));

  return csp;
}

void
color_select_show (ColorSelectP csp)
{
  if (csp)
    gtk_window_present (GTK_WINDOW (csp->shell));
}

void
color_select_hide (ColorSelectP csp)
{
  if (csp)
    gtk_widget_set_visible (csp->shell, FALSE);
}

void
color_select_free (ColorSelectP csp)
{
  int i;

  if (csp)
    {
      /*  the adjustments may outlive the window for a moment  */
      for (i = 0; i < 6; i++)
	g_signal_handlers_disconnect_by_data (csp->slider_data[i], csp);

      gtk_window_destroy (GTK_WINDOW (csp->shell));
      g_free (csp);
    }
}

void
color_select_set_color (ColorSelectP csp,
			int          r,
			int          g,
			int          b,
			int          set_current)
{
  if (csp)
    {
      csp->orig_values[0] = r;
      csp->orig_values[1] = g;
      csp->orig_values[2] = b;

      color_select_update_colors (csp, 1);

      if (set_current)
	{
	  csp->values[RED] = r;
	  csp->values[GREEN] = g;
	  csp->values[BLUE] = b;

	  color_select_update_hsv_values (csp);
	  color_select_update_pos (csp);
	  color_select_update_sliders (csp, -1);
	  color_select_update_entries (csp, -1);
	  color_select_update_colors (csp, 0);

	  color_select_update (csp, UPDATE_Z_COLOR);
	  color_select_update (csp, UPDATE_XY_COLOR);
	}
    }
}

static void
color_select_update (ColorSelectP          csp,
		     ColorSelectUpdateType update)
{
  if (csp)
    {
      if (update & UPDATE_POS)
	color_select_update_pos (csp);

      if (update & UPDATE_VALUES)
	{
	  color_select_update_values (csp);
	  color_select_update_sliders (csp, -1);
	  color_select_update_entries (csp, -1);

	  if (!(update & UPDATE_NEW_COLOR))
	    color_select_update_colors (csp, 0);
	}

      if (update & UPDATE_XY_COLOR)
	{
	  color_select_image_fill (csp->xy_color, csp->xy_color_fill, csp->values);
	  color_select_draw_xy_marker (csp);
	}

      if (update & UPDATE_Z_COLOR)
	{
	  color_select_image_fill (csp->z_color, csp->z_color_fill, csp->values);
	  color_select_draw_z_marker (csp);
	}

      if (update & UPDATE_NEW_COLOR)
	color_select_update_colors (csp, 0);

      if (update & UPDATE_ORIG_COLOR)
	color_select_update_colors (csp, 1);

      /*if (update & UPDATE_CALLER)*/
      color_select_update_caller (csp);
    }
}

static void
color_select_update_caller (ColorSelectP csp)
{
  if (csp && csp->wants_updates && csp->callback)
    {
      (* csp->callback) (csp->values[RED],
			 csp->values[GREEN],
			 csp->values[BLUE],
			 COLOR_SELECT_UPDATE,
			 csp->client_data);
    }
}

static void
color_select_update_values (ColorSelectP csp)
{
  if (csp)
    {
      switch (csp->z_color_fill)
	{
	case RED:
	  csp->values[BLUE] = csp->pos[0];
	  csp->values[GREEN] = csp->pos[1];
	  csp->values[RED] = csp->pos[2];
	  break;
	case GREEN:
	  csp->values[BLUE] = csp->pos[0];
	  csp->values[RED] = csp->pos[1];
	  csp->values[GREEN] = csp->pos[2];
	  break;
	case BLUE:
	  csp->values[GREEN] = csp->pos[0];
	  csp->values[RED] = csp->pos[1];
	  csp->values[BLUE] = csp->pos[2];
	  break;
	case HUE:
	  csp->values[VALUE] = csp->pos[0] * 100 / 255;
	  csp->values[SATURATION] = csp->pos[1] * 100 / 255;
	  csp->values[HUE] = csp->pos[2] * 360 / 255;
	  break;
	case SATURATION:
	  csp->values[VALUE] = csp->pos[0] * 100 / 255;
	  csp->values[HUE] = csp->pos[1] * 360 / 255;
	  csp->values[SATURATION] = csp->pos[2] * 100 / 255;
	  break;
	case VALUE:
	  csp->values[SATURATION] = csp->pos[0] * 100 / 255;
	  csp->values[HUE] = csp->pos[1] * 360 / 255;
	  csp->values[VALUE] = csp->pos[2] * 100 / 255;
	  break;
	}

      switch (csp->z_color_fill)
	{
	case RED:
	case GREEN:
	case BLUE:
	  color_select_update_hsv_values (csp);
	  break;
	case HUE:
	case SATURATION:
	case VALUE:
	  color_select_update_rgb_values (csp);
	  break;
	}
    }
}

static void
color_select_update_rgb_values (ColorSelectP csp)
{
  float h, s, v;
  float f, p, q, t;

  if (csp)
    {
      h = csp->values[HUE];
      s = csp->values[SATURATION] / 100.0;
      v = csp->values[VALUE] / 100.0;

      if (s == 0)
	{
	  csp->values[RED] = v * 255;
	  csp->values[GREEN] = v * 255;
	  csp->values[BLUE] = v * 255;
	}
      else
	{
	  if (h == 360)
	    h = 0;

	  h /= 60;
	  f = h - (int) h;
	  p = v * (1 - s);
	  q = v * (1 - (s * f));
	  t = v * (1 - (s * (1 - f)));

	  switch ((int) h)
	    {
	    case 0:
	      csp->values[RED] = v * 255;
	      csp->values[GREEN] = t * 255;
	      csp->values[BLUE] = p * 255;
	      break;
	    case 1:
	      csp->values[RED] = q * 255;
	      csp->values[GREEN] = v * 255;
	      csp->values[BLUE] = p * 255;
	      break;
	    case 2:
	      csp->values[RED] = p * 255;
	      csp->values[GREEN] = v * 255;
	      csp->values[BLUE] = t * 255;
	      break;
	    case 3:
	      csp->values[RED] = p * 255;
	      csp->values[GREEN] = q * 255;
	      csp->values[BLUE] = v * 255;
	      break;
	    case 4:
	      csp->values[RED] = t * 255;
	      csp->values[GREEN] = p * 255;
	      csp->values[BLUE] = v * 255;
	      break;
	    case 5:
	      csp->values[RED] = v * 255;
	      csp->values[GREEN] = p * 255;
	      csp->values[BLUE] = q * 255;
	      break;
	    }
	}
    }
}

static void
color_select_update_hsv_values (ColorSelectP csp)
{
  int r, g, b;
  float h, s, v;
  int min, max;
  int delta;

  if (csp)
    {
      r = csp->values[RED];
      g = csp->values[GREEN];
      b = csp->values[BLUE];

      if (r > g)
	{
	  if (r > b)
	    max = r;
	  else
	    max = b;

	  if (g < b)
	    min = g;
	  else
	    min = b;
	}
      else
	{
	  if (g > b)
	    max = g;
	  else
	    max = b;

	  if (r < b)
	    min = r;
	  else
	    min = b;
	}

      v = max;

      if (max != 0)
	s = (max - min) / (float) max;
      else
	s = 0;

      if (s == 0)
	h = 0;
      else
	{
	  h = 0;
	  delta = max - min;
	  if (r == max)
	    h = (g - b) / (float) delta;
	  else if (g == max)
	    h = 2 + (b - r) / (float) delta;
	  else if (b == max)
	    h = 4 + (r - g) / (float) delta;
	  h *= 60;

	  if (h < 0)
	    h += 360;
	}

      csp->values[HUE] = h;
      csp->values[SATURATION] = s * 100;
      csp->values[VALUE] = v * 100 / 255;
    }
}

static void
color_select_update_pos (ColorSelectP csp)
{
  if (csp)
    {
      switch (csp->z_color_fill)
	{
	case RED:
	  csp->pos[0] = csp->values[BLUE];
	  csp->pos[1] = csp->values[GREEN];
	  csp->pos[2] = csp->values[RED];
	  break;
	case GREEN:
	  csp->pos[0] = csp->values[BLUE];
	  csp->pos[1] = csp->values[RED];
	  csp->pos[2] = csp->values[GREEN];
	  break;
	case BLUE:
	  csp->pos[0] = csp->values[GREEN];
	  csp->pos[1] = csp->values[RED];
	  csp->pos[2] = csp->values[BLUE];
	  break;
	case HUE:
	  csp->pos[0] = csp->values[VALUE] * 255 / 100;
	  csp->pos[1] = csp->values[SATURATION] * 255 / 100;
	  csp->pos[2] = csp->values[HUE] * 255 / 360;
	  break;
	case SATURATION:
	  csp->pos[0] = csp->values[VALUE] * 255 / 100;
	  csp->pos[1] = csp->values[HUE] * 255 / 360;
	  csp->pos[2] = csp->values[SATURATION] * 255 / 100;
	  break;
	case VALUE:
	  csp->pos[0] = csp->values[SATURATION] * 255 / 100;
	  csp->pos[1] = csp->values[HUE] * 255 / 360;
	  csp->pos[2] = csp->values[VALUE] * 255 / 100;
	  break;
	}
    }
}

static void
color_select_update_sliders (ColorSelectP csp,
			     int          skip)
{
  int i;

  if (csp)
    {
      for (i = 0; i < 6; i++)
	if (i != skip)
	  {
	    g_signal_handlers_block_by_func (csp->slider_data[i],
					     color_select_slider_update, csp);
	    gtk_adjustment_set_value (csp->slider_data[i], (gdouble) csp->values[i]);
	    g_signal_handlers_unblock_by_func (csp->slider_data[i],
					       color_select_slider_update, csp);
	  }
    }
}

static void
color_select_update_entries (ColorSelectP csp,
			     int          skip)
{
  char buffer[16];
  int i;

  if (csp)
    {
      for (i = 0; i < 6; i++)
	if (i != skip)
	  {
	    sprintf (buffer, "%d", csp->values[i]);

	    g_signal_handlers_block_by_func (csp->entries[i],
					     color_select_entry_update, csp);
	    gtk_editable_set_text (GTK_EDITABLE (csp->entries[i]), buffer);
	    g_signal_handlers_unblock_by_func (csp->entries[i],
					       color_select_entry_update, csp);
	  }
    }
}

static void
color_select_update_colors (ColorSelectP csp,
			    int          which)
{
  /*  the swatches draw themselves from csp in color_select_color_draw  */
  if (csp)
    {
      if (which)
	gtk_widget_queue_draw (csp->orig_color);
      else
	gtk_widget_queue_draw (csp->new_color);
    }
}

static void
color_select_ok_callback (GtkWidget *w,
			  gpointer   client_data)
{
  ColorSelectP csp;

  csp = (ColorSelectP) client_data;
  if (csp)
    {
      if (csp->callback)
	(* csp->callback) (csp->values[RED],
			   csp->values[GREEN],
			   csp->values[BLUE],
			   COLOR_SELECT_OK,
			   csp->client_data);
    }
}

static gboolean
color_select_delete_callback (GtkWindow *w,
			      gpointer   client_data)
{
  color_select_cancel_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}
  

static void
color_select_cancel_callback (GtkWidget *w,
			      gpointer   client_data)
{
  ColorSelectP csp;

  csp = (ColorSelectP) client_data;
  if (csp)
    {
      if (csp->callback)
	(* csp->callback) (csp->orig_values[0],
			   csp->orig_values[1],
			   csp->orig_values[2],
			   COLOR_SELECT_CANCEL,
			   csp->client_data);
    }
}

static void
color_select_xy_marker_draw (GtkDrawingArea *area,
			     cairo_t        *cr,
			     int             width,
			     int             height,
			     gpointer        data)
{
  ColorSelectP csp = data;
  int x, y;

  x = ((XY_DEF_WIDTH - 1) * csp->pos[0]) / 255;
  y = (XY_DEF_HEIGHT - 1) - ((XY_DEF_HEIGHT - 1) * csp->pos[1]) / 255;

  /*  an inverted cross hair, like the XOR lines it replaces: the pixel
   *  where the lines cross is inverted twice
   */
  color_select_paint_inverted (cr, csp->xy_color,
			       0, y, XY_DEF_WIDTH, 1, -1, -1);
  color_select_paint_inverted (cr, csp->xy_color,
			       x, 0, 1, XY_DEF_HEIGHT, x, y);
}

static void
color_select_xy_set_pos (ColorSelectP csp,
			 double       x,
			 double       y)
{
  csp->pos[0] = (x * 255) / (XY_DEF_WIDTH - 1);
  csp->pos[1] = 255 - (y * 255) / (XY_DEF_HEIGHT - 1);

  if (csp->pos[0] < 0)
    csp->pos[0] = 0;
  if (csp->pos[0] > 255)
    csp->pos[0] = 255;
  if (csp->pos[1] < 0)
    csp->pos[1] = 0;
  if (csp->pos[1] > 255)
    csp->pos[1] = 255;

  color_select_draw_xy_marker (csp);
}

static void
color_select_xy_pressed (GtkGestureDrag *gesture,
			 double          x,
			 double          y,
			 gpointer        data)
{
  ColorSelectP csp = data;

  color_select_xy_set_pos (csp, x, y);
  color_select_update (csp, UPDATE_VALUES);
}

static void
color_select_xy_motion (GtkGestureDrag *gesture,
			double          offset_x,
			double          offset_y,
			gpointer        data)
{
  ColorSelectP csp = data;
  double x, y;

  gtk_gesture_drag_get_start_point (gesture, &x, &y);
  color_select_xy_set_pos (csp, x + offset_x, y + offset_y);
  color_select_update (csp, UPDATE_VALUES);
}

static void
color_select_xy_released (GtkGestureDrag *gesture,
			  double          offset_x,
			  double          offset_y,
			  gpointer        data)
{
  ColorSelectP csp = data;
  double x, y;

  gtk_gesture_drag_get_start_point (gesture, &x, &y);
  color_select_xy_set_pos (csp, x + offset_x, y + offset_y);
  color_select_update (csp, UPDATE_VALUES);
}

static void
color_select_z_marker_draw (GtkDrawingArea *area,
			    cairo_t        *cr,
			    int             width,
			    int             height,
			    gpointer        data)
{
  ColorSelectP csp = data;
  int y;

  y = (Z_DEF_HEIGHT - 1) - ((Z_DEF_HEIGHT - 1) * csp->pos[2]) / 255;

  color_select_paint_inverted (cr, csp->z_color,
			       0, y, Z_DEF_WIDTH, 1, -1, -1);
}

static void
color_select_z_set_pos (ColorSelectP csp,
			double       y)
{
  csp->pos[2] = 255 - (y * 255) / (Z_DEF_HEIGHT - 1);
  if (csp->pos[2] < 0)
    csp->pos[2] = 0;
  if (csp->pos[2] > 255)
    csp->pos[2] = 255;

  color_select_draw_z_marker (csp);
}

static void
color_select_z_pressed (GtkGestureDrag *gesture,
			double          x,
			double          y,
			gpointer        data)
{
  ColorSelectP csp = data;

  color_select_z_set_pos (csp, y);
  color_select_update (csp, UPDATE_VALUES);
}

static void
color_select_z_motion (GtkGestureDrag *gesture,
		       double          offset_x,
		       double          offset_y,
		       gpointer        data)
{
  ColorSelectP csp = data;
  double x, y;

  gtk_gesture_drag_get_start_point (gesture, &x, &y);
  color_select_z_set_pos (csp, y + offset_y);
  color_select_update (csp, UPDATE_VALUES);
}

static void
color_select_z_released (GtkGestureDrag *gesture,
			 double          offset_x,
			 double          offset_y,
			 gpointer        data)
{
  ColorSelectP csp = data;
  double x, y;

  gtk_gesture_drag_get_start_point (gesture, &x, &y);
  color_select_z_set_pos (csp, y + offset_y);
  color_select_update (csp, UPDATE_VALUES | UPDATE_XY_COLOR);
}

static void
color_select_color_draw (GtkDrawingArea *area,
			 cairo_t        *cr,
			 int             width,
			 int             height,
			 gpointer        data)
{
  ColorSelectP csp = data;
  int red, green, blue;

  if (GTK_WIDGET (area) == csp->orig_color)
    {
      red = csp->orig_values[0];
      green = csp->orig_values[1];
      blue = csp->orig_values[2];
    }
  else
    {
      red = csp->values[RED];
      green = csp->values[GREEN];
      blue = csp->values[BLUE];
    }

  cairo_set_source_rgb (cr, red / 255.0, green / 255.0, blue / 255.0);
  cairo_paint (cr);
}

static void
color_select_slider_update (GtkAdjustment *adjustment,
			    gpointer       data)
{
  ColorSelectP csp;
  int old_values[6];
  int update_z_marker;
  int update_xy_marker;
  int i, j;

  csp = (ColorSelectP) data;

  if (csp)
    {
      for (i = 0; i < 6; i++)
	if (csp->slider_data[i] == adjustment)
	  break;

      for (j = 0; j < 6; j++)
	old_values[j] = csp->values[j];

      csp->values[i] = (int) gtk_adjustment_get_value (adjustment);

      if ((i >= HUE) && (i <= VALUE))
	color_select_update_rgb_values (csp);
      else if ((i >= RED) && (i <= BLUE))
	color_select_update_hsv_values (csp);
      color_select_update_sliders (csp, i);
      color_select_update_entries (csp, -1);

      update_z_marker = 0;
      update_xy_marker = 0;
      for (j = 0; j < 6; j++)
	{
	  if (j == csp->z_color_fill)
	    {
	      if (old_values[j] != csp->values[j])
		update_z_marker = 1;
	    }
	  else
	    {
	      if (old_values[j] != csp->values[j])
		update_xy_marker = 1;
	    }
	}

      if (update_z_marker)
	{
	  color_select_draw_z_marker (csp);
	  color_select_update (csp, UPDATE_POS | UPDATE_XY_COLOR);
	  color_select_draw_z_marker (csp);
	}
      else
	{
	  if (update_z_marker)
	    color_select_draw_z_marker (csp);
	  if (update_xy_marker)
	    color_select_draw_xy_marker (csp);

	  color_select_update (csp, UPDATE_POS);

	  if (update_z_marker)
	    color_select_draw_z_marker (csp);
	  if (update_xy_marker)
	    color_select_draw_xy_marker (csp);
	}

      color_select_update (csp, UPDATE_NEW_COLOR);
    }
}

static void
color_select_entry_update (GtkWidget *w,
			   gpointer   data)
{
  ColorSelectP csp;
  int old_values[6];
  int update_z_marker;
  int update_xy_marker;
  int i, j;

  csp = (ColorSelectP) data;

  if (csp)
    {
      for (i = 0; i < 6; i++)
	if (csp->entries[i] == w)
	  break;

      for (j = 0; j < 6; j++)
	old_values[j] = csp->values[j];

      csp->values[i] = atoi (gtk_editable_get_text (GTK_EDITABLE (csp->entries[i])));
      if (csp->values[i] == old_values[i])
	return;

      if ((i >= HUE) && (i <= VALUE))
	color_select_update_rgb_values (csp);
      else if ((i >= RED) && (i <= BLUE))
	color_select_update_hsv_values (csp);
      color_select_update_entries (csp, i);
      color_select_update_sliders (csp, -1);

      update_z_marker = 0;
      update_xy_marker = 0;
      for (j = 0; j < 6; j++)
	{
	  if (j == csp->z_color_fill)
	    {
	      if (old_values[j] != csp->values[j])
		update_z_marker = 1;
	    }
	  else
	    {
	      if (old_values[j] != csp->values[j])
		update_xy_marker = 1;
	    }
	}

      if (update_z_marker)
	{
	  color_select_draw_z_marker (csp);
	  color_select_update (csp, UPDATE_POS | UPDATE_XY_COLOR);
	  color_select_draw_z_marker (csp);
	}
      else
	{
	  if (update_z_marker)
	    color_select_draw_z_marker (csp);
	  if (update_xy_marker)
	    color_select_draw_xy_marker (csp);

	  color_select_update (csp, UPDATE_POS);

	  if (update_z_marker)
	    color_select_draw_z_marker (csp);
	  if (update_xy_marker)
	    color_select_draw_xy_marker (csp);
	}

      color_select_update (csp, UPDATE_NEW_COLOR);
    }
}

static void
color_select_toggle_update (GtkWidget *w,
			    gpointer   data)
{
  ColorSelectP csp;
  ColorSelectFillType type = HUE;
  int i;

  if (!gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    return;

  csp = (ColorSelectP) data;

  if (csp)
    {
      for (i = 0; i < 6; i++)
	if (w == csp->toggles[i])
	  type = (ColorSelectFillType) i;

      switch (type)
	{
	case HUE:
	  csp->z_color_fill = HUE;
	  csp->xy_color_fill = SATURATION_VALUE;
	  break;
	case SATURATION:
	  csp->z_color_fill = SATURATION;
	  csp->xy_color_fill = HUE_VALUE;
	  break;
	case VALUE:
	  csp->z_color_fill = VALUE;
	  csp->xy_color_fill = HUE_SATURATION;
	  break;
	case RED:
	  csp->z_color_fill = RED;
	  csp->xy_color_fill = GREEN_BLUE;
	  break;
	case GREEN:
	  csp->z_color_fill = GREEN;
	  csp->xy_color_fill = RED_BLUE;
	  break;
	case BLUE:
	  csp->z_color_fill = BLUE;
	  csp->xy_color_fill = RED_GREEN;
	  break;
	default:
	  break;
	}

      color_select_update (csp, UPDATE_POS);
      color_select_update (csp, UPDATE_Z_COLOR | UPDATE_XY_COLOR);
    }
}

static void
color_select_image_fill (GtkWidget           *preview,
			 ColorSelectFillType  type,
			 int                 *values)
{
  ColorSelectFill csf;
  int height;

  csf.width = gimp_preview_get_width (GIMP_PREVIEW (preview));
  csf.height = gimp_preview_get_height (GIMP_PREVIEW (preview));

  csf.buffer = g_malloc (csf.width * 3);

  csf.update = update_procs[type];

  csf.y = -1;
  csf.values = values;

  height = csf.height;
  if (height > 0)
    while (height--)
      {
	(* csf.update) (&csf);
	gimp_preview_draw_row (GIMP_PREVIEW (preview), csf.buffer, 0, csf.y, csf.width);
      }

  g_free (csf.buffer);
}

/*  Paints the w x h area of preview at (x, y) with its colors inverted,
 *  except for the pixel at (skip_x, skip_y), which keeps its color.
 */
static void
color_select_paint_inverted (cairo_t   *cr,
			     GtkWidget *preview,
			     int        x,
			     int        y,
			     int        w,
			     int        h,
			     int        skip_x,
			     int        skip_y)
{
  cairo_surface_t *surface;
  const guchar *buffer;
  const guchar *src;
  guchar *data;
  guint32 *dest;
  int pwidth, pheight;
  int rowstride;
  int stride;
  int i, j;

  pwidth = gimp_preview_get_width (GIMP_PREVIEW (preview));
  pheight = gimp_preview_get_height (GIMP_PREVIEW (preview));
  buffer = gimp_preview_get_buffer (GIMP_PREVIEW (preview));
  rowstride = gimp_preview_get_rowstride (GIMP_PREVIEW (preview));

  if (x < 0)
    {
      w += x;
      x = 0;
    }
  if (y < 0)
    {
      h += y;
      y = 0;
    }
  w = MIN (w, pwidth - x);
  h = MIN (h, pheight - y);
  if (!buffer || w <= 0 || h <= 0)
    return;

  surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24, w, h);
  cairo_surface_flush (surface);
  data = cairo_image_surface_get_data (surface);
  stride = cairo_image_surface_get_stride (surface);

  for (i = 0; i < h; i++)
    {
      src = buffer + (y + i) * rowstride + x * 3;
      dest = (guint32 *) (data + i * stride);

      for (j = 0; j < w; j++, src += 3)
	{
	  if (x + j == skip_x && y + i == skip_y)
	    dest[j] = ((guint32) src[0] << 16) | ((guint32) src[1] << 8) | src[2];
	  else
	    dest[j] = (((guint32) (255 - src[0]) << 16) |
		       ((guint32) (255 - src[1]) << 8) |
		       (guint32) (255 - src[2]));
	}
    }
  cairo_surface_mark_dirty (surface);

  cairo_set_source_surface (cr, surface, x, y);
  cairo_rectangle (cr, x, y, w, h);
  cairo_fill (cr);

  cairo_surface_destroy (surface);
}

/*  The markers are drawn by the overlays' draw functions: these only
 *  ask for that to happen.
 */
static void
color_select_draw_z_marker (ColorSelectP csp)
{
  if (csp->z_marker)
    gtk_widget_queue_draw (csp->z_marker);
}

static void
color_select_draw_xy_marker (ColorSelectP csp)
{
  if (csp->xy_marker)
    gtk_widget_queue_draw (csp->xy_marker);
}

static void
color_select_update_red (ColorSelectFill *csf)
{
  unsigned char *p;
  int i, r;

  p = csf->buffer;

  csf->y += 1;
  r = (csf->height - csf->y + 1) * 255 / csf->height;

  if (r < 0)
    r = 0;
  if (r > 255)
    r = 255;

  for (i = 0; i < csf->width; i++)
    {
      *p++ = r;
      *p++ = 0;
      *p++ = 0;
    }
}

static void
color_select_update_green (ColorSelectFill *csf)
{
  unsigned char *p;
  int i, g;

  p = csf->buffer;

  csf->y += 1;
  g = (csf->height - csf->y + 1) * 255 / csf->height;

  if (g < 0)
    g = 0;
  if (g > 255)
    g = 255;

  for (i = 0; i < csf->width; i++)
    {
      *p++ = 0;
      *p++ = g;
      *p++ = 0;
    }
}

static void
color_select_update_blue (ColorSelectFill *csf)
{
  unsigned char *p;
  int i, b;

  p = csf->buffer;

  csf->y += 1;
  b = (csf->height - csf->y + 1) * 255 / csf->height;

  if (b < 0)
    b = 0;
  if (b > 255)
    b = 255;

  for (i = 0; i < csf->width; i++)
    {
      *p++ = 0;
      *p++ = 0;
      *p++ = b;
    }
}

static void
color_select_update_hue (ColorSelectFill *csf)
{
  unsigned char *p;
  float h, f;
  int r, g, b;
  int i;

  p = csf->buffer;

  csf->y += 1;
  h = csf->y * 360 / csf->height;

  h = 360 - h;

  if (h < 0)
    h = 0;
  if (h >= 360)
    h = 0;

  h /= 60;
  f = (h - (int) h) * 255;

  r = g = b = 0;

  switch ((int) h)
    {
    case 0:
      r = 255;
      g = f;
      b = 0;
      break;
    case 1:
      r = 255 - f;
      g = 255;
      b = 0;
      break;
    case 2:
      r = 0;
      g = 255;
      b = f;
      break;
    case 3:
      r = 0;
      g = 255 - f;
      b = 255;
      break;
    case 4:
      r = f;
      g = 0;
      b = 255;
      break;
    case 5:
      r = 255;
      g = 0;
      b = 255 - f;
      break;
    }

  for (i = 0; i < csf->width; i++)
    {
      *p++ = r;
      *p++ = g;
      *p++ = b;
    }
}

static void
color_select_update_saturation (ColorSelectFill *csf)
{
  unsigned char *p;
  int s;
  int i;

  p = csf->buffer;

  csf->y += 1;
  s = csf->y * 255 / csf->height;

  if (s < 0)
    s = 0;
  if (s > 255)
    s = 255;

  s = 255 - s;

  for (i = 0; i < csf->width; i++)
    {
      *p++ = s;
      *p++ = s;
      *p++ = s;
    }
}

static void
color_select_update_value (ColorSelectFill *csf)
{
  unsigned char *p;
  int v;
  int i;

  p = csf->buffer;

  csf->y += 1;
  v = csf->y * 255 / csf->height;

  if (v < 0)
    v = 0;
  if (v > 255)
    v = 255;

  v = 255 - v;

  for (i = 0; i < csf->width; i++)
    {
      *p++ = v;
      *p++ = v;
      *p++ = v;
    }
}

static void
color_select_update_red_green (ColorSelectFill *csf)
{
  unsigned char *p;
  int i, r, b;
  float g, dg;

  p = csf->buffer;

  csf->y += 1;
  b = csf->values[BLUE];
  r = (csf->height - csf->y + 1) * 255 / csf->height;

  if (r < 0)
    r = 0;
  if (r > 255)
    r = 255;

  g = 0;
  dg = 255.0 / csf->width;

  for (i = 0; i < csf->width; i++)
    {
      *p++ = r;
      *p++ = g;
      *p++ = b;

      g += dg;
    }
}

static void
color_select_update_red_blue (ColorSelectFill *csf)
{
  unsigned char *p;
  int i, r, g;
  float b, db;

  p = csf->buffer;

  csf->y += 1;
  g = csf->values[GREEN];
  r = (csf->height - csf->y + 1) * 255 / csf->height;

  if (r < 0)
    r = 0;
  if (r > 255)
    r = 255;

  b = 0;
  db = 255.0 / csf->width;

  for (i = 0; i < csf->width; i++)
    {
      *p++ = r;
      *p++ = g;
      *p++ = b;

      b += db;
    }
}

static void
color_select_update_green_blue (ColorSelectFill *csf)
{
  unsigned char *p;
  int i, g, r;
  float b, db;

  p = csf->buffer;

  csf->y += 1;
  r = csf->values[RED];
  g = (csf->height - csf->y + 1) * 255 / csf->height;

  if (g < 0)
    g = 0;
  if (g > 255)
    g = 255;

  b = 0;
  db = 255.0 / csf->width;

  for (i = 0; i < csf->width; i++)
    {
      *p++ = r;
      *p++ = g;
      *p++ = b;

      b += db;
    }
}

static void
color_select_update_hue_saturation (ColorSelectFill *csf)
{
  unsigned char *p;
  float h, v, s, ds;
  int f;
  int i;

  p = csf->buffer;

  csf->y += 1;
  h = 360 - (csf->y * 360 / csf->height);

  if (h < 0)
    h = 0;
  if (h > 359)
    h = 359;

  h /= 60;
  f = (h - (int) h) * 255;

  s = 0;
  ds = 1.0 / csf->width;

  v = csf->values[VALUE] / 100.0;

  switch ((int) h)
    {
    case 0:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * 255;
	  *p++ = v * (255 - (s * (255 - f)));
	  *p++ = v * 255 * (1 - s);

	  s += ds;
	}
      break;
    case 1:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * (255 - s * f);
	  *p++ = v * 255;
	  *p++ = v * 255 * (1 - s);

	  s += ds;
	}
      break;
    case 2:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * 255 * (1 - s);
	  *p++ = v *255;
	  *p++ = v * (255 - (s * (255 - f)));

	  s += ds;
	}
      break;
    case 3:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * 255 * (1 - s);
	  *p++ = v * (255 - s * f);
	  *p++ = v * 255;

	  s += ds;
	}
      break;
    case 4:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * (255 - (s * (255 - f)));
	  *p++ = v * (255 * (1 - s));
	  *p++ = v * 255;

	  s += ds;
	}
      break;
    case 5:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * 255;
	  *p++ = v * 255 * (1 - s);
	  *p++ = v * (255 - s * f);

	  s += ds;
	}
      break;
    }
}

static void
color_select_update_hue_value (ColorSelectFill *csf)
{
  unsigned char *p;
  float h, v, dv, s;
  int f;
  int i;

  p = csf->buffer;

  csf->y += 1;
  h = 360 - (csf->y * 360 / csf->height);

  if (h < 0)
    h = 0;
  if (h > 359)
    h = 359;

  h /= 60;
  f = (h - (int) h) * 255;

  v = 0;
  dv = 1.0 / csf->width;

  s = csf->values[SATURATION] / 100.0;

  switch ((int) h)
    {
    case 0:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * 255;
	  *p++ = v * (255 - (s * (255 - f)));
	  *p++ = v * 255 * (1 - s);

	  v += dv;
	}
      break;
    case 1:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * (255 - s * f);
	  *p++ = v * 255;
	  *p++ = v * 255 * (1 - s);

	  v += dv;
	}
      break;
    case 2:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * 255 * (1 - s);
	  *p++ = v *255;
	  *p++ = v * (255 - (s * (255 - f)));

	  v += dv;
	}
      break;
    case 3:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * 255 * (1 - s);
	  *p++ = v * (255 - s * f);
	  *p++ = v * 255;

	  v += dv;
	}
      break;
    case 4:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * (255 - (s * (255 - f)));
	  *p++ = v * (255 * (1 - s));
	  *p++ = v * 255;

	  v += dv;
	}
      break;
    case 5:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * 255;
	  *p++ = v * 255 * (1 - s);
	  *p++ = v * (255 - s * f);

	  v += dv;
	}
      break;
    }
}

static void
color_select_update_saturation_value (ColorSelectFill *csf)
{
  unsigned char *p;
  float h, v, dv, s;
  int f;
  int i;

  p = csf->buffer;

  csf->y += 1;
  s = (float) csf->y / csf->height;

  if (s < 0)
    s = 0;
  if (s > 1)
    s = 1;

  s = 1 - s;

  h = (float) csf->values[HUE];
  if (h >= 360)
    h -= 360;
  h /= 60;
  f = (h - (int) h) * 255;

  v = 0;
  dv = 1.0 / csf->width;

  switch ((int) h)
    {
    case 0:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * 255;
	  *p++ = v * (255 - (s * (255 - f)));
	  *p++ = v * 255 * (1 - s);

	  v += dv;
	}
      break;
    case 1:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * (255 - s * f);
	  *p++ = v * 255;
	  *p++ = v * 255 * (1 - s);

	  v += dv;
	}
      break;
    case 2:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * 255 * (1 - s);
	  *p++ = v *255;
	  *p++ = v * (255 - (s * (255 - f)));

	  v += dv;
	}
      break;
    case 3:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * 255 * (1 - s);
	  *p++ = v * (255 - s * f);
	  *p++ = v * 255;

	  v += dv;
	}
      break;
    case 4:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * (255 - (s * (255 - f)));
	  *p++ = v * (255 * (1 - s));
	  *p++ = v * 255;

	  v += dv;
	}
      break;
    case 5:
      for (i = 0; i < csf->width; i++)
	{
	  *p++ = v * 255;
	  *p++ = v * 255 * (1 - s);
	  *p++ = v * (255 - s * f);

	  v += dv;
	}
      break;
    }
}
