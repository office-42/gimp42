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
#include "appenv.h"
#include "color_area.h"
#include "color_select.h"
#include "colormaps.h"
#include "palette.h"

#define FORE_AREA 0
#define BACK_AREA 1
#define SWAP_AREA 2
#define DEF_AREA  3

/*  Global variables  */
int active_color = 0;

/*  Static variables  */
static GtkWidget *color_area = NULL;
static GdkTexture *default_texture = NULL;
static GdkTexture *swap_texture = NULL;
static ColorSelectP color_select = NULL;
static int color_select_active = 0;
static int edit_color;
static unsigned char revert_fg_r, revert_fg_g, revert_fg_b;
static unsigned char revert_bg_r, revert_bg_g, revert_bg_b;

/*  Local functions  */
static int
color_area_target (int x,
		   int y)
{
  int rect_w, rect_h;
  int width, height;

  width = gtk_widget_get_width (color_area);
  height = gtk_widget_get_height (color_area);

  rect_w = width * 0.65;
  rect_h = height * 0.65;

  /*  foreground active  */
  if (x > 0 && x < rect_w &&
      y > 0 && y < rect_h)
    return FORE_AREA;
  else if (x > (width - rect_w) && x < width &&
	   y > (height - rect_h) && y < height)
    return BACK_AREA;
  else if (x > 0 && x < (width - rect_w) &&
	   y > rect_h && y < height)
    return DEF_AREA;
  else if (x > rect_w && x < width &&
	   y > 0 && y < (height - rect_h))
    return SWAP_AREA;
  else
    return -1;
}

/*  A raised (out) or sunken (in) bevel around a swatch.  */
static void
color_area_draw_shadow (cairo_t *cr,
			gboolean sunken,
			int      x,
			int      y,
			int      w,
			int      h)
{
  double light = sunken ? 0.3 : 1.0;
  double dark  = sunken ? 1.0 : 0.3;

  cairo_set_line_width (cr, 1.0);

  cairo_set_source_rgb (cr, light, light, light);
  cairo_move_to (cr, x + 0.5, y + h - 0.5);
  cairo_line_to (cr, x + 0.5, y + 0.5);
  cairo_line_to (cr, x + w - 0.5, y + 0.5);
  cairo_stroke (cr);

  cairo_set_source_rgb (cr, dark, dark, dark);
  cairo_move_to (cr, x + w - 0.5, y + 0.5);
  cairo_line_to (cr, x + w - 0.5, y + h - 0.5);
  cairo_line_to (cr, x + 0.5, y + h - 0.5);
  cairo_stroke (cr);
}

static void
color_area_draw_texture (cairo_t    *cr,
			 GdkTexture *texture,
			 int         x,
			 int         y)
{
  cairo_surface_t *surface;
  int w, h;

  if (!texture)
    return;

  w = gdk_texture_get_width (texture);
  h = gdk_texture_get_height (texture);
  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, w, h);
  gdk_texture_download (texture, cairo_image_surface_get_data (surface),
			cairo_image_surface_get_stride (surface));
  cairo_surface_mark_dirty (surface);

  cairo_set_source_surface (cr, surface, x, y);
  cairo_paint (cr);
  cairo_surface_destroy (surface);
}

static void
color_area_draw_func (GtkDrawingArea *area,
		      cairo_t        *cr,
		      int             width,
		      int             height,
		      gpointer        data)
{
  unsigned char r, g, b;
  int rect_w, rect_h;

  rect_w = width * 0.65;
  rect_h = height * 0.65;

  palette_get_background (&r, &g, &b);
  cairo_set_source_rgb (cr, r / 255.0, g / 255.0, b / 255.0);
  cairo_rectangle (cr, (width - rect_w), (height - rect_h), rect_w, rect_h);
  cairo_fill (cr);
  color_area_draw_shadow (cr, active_color != FOREGROUND,
			  (width - rect_w), (height - rect_h), rect_w, rect_h);

  palette_get_foreground (&r, &g, &b);
  cairo_set_source_rgb (cr, r / 255.0, g / 255.0, b / 255.0);
  cairo_rectangle (cr, 0, 0, rect_w, rect_h);
  cairo_fill (cr);
  color_area_draw_shadow (cr, active_color == FOREGROUND,
			  0, 0, rect_w, rect_h);

  if (default_texture)
    color_area_draw_texture (cr, default_texture, 0,
			     height - gdk_texture_get_height (default_texture));
  if (swap_texture)
    color_area_draw_texture (cr, swap_texture,
			     width - gdk_texture_get_width (swap_texture), 0);
}

static void
color_area_draw (void)
{
  if (color_area)
    gtk_widget_queue_draw (color_area);
}

static void
color_area_select_callback (int   r,
			    int   g,
			    int   b,
			    ColorSelectState state,
			    void *client_data)
{
  if (color_select)
    {
      switch (state) {
      case COLOR_SELECT_OK:
	color_select_hide (color_select);
	color_select_active = 0;
	/* Fallthrough */
      case COLOR_SELECT_UPDATE:
	if (edit_color == FOREGROUND)
	  palette_set_foreground (r, g, b);
	else
	  palette_set_background (r, g, b);
	break;
      case COLOR_SELECT_CANCEL:
	color_select_hide (color_select);
	color_select_active = 0;
	palette_set_foreground (revert_fg_r, revert_fg_g, revert_fg_b);
	palette_set_background (revert_bg_r, revert_bg_g, revert_bg_b);
      }
    }
}

static void
color_area_edit (void)
{
  unsigned char r, g, b;

  if (!color_select_active)
    {
      palette_get_foreground (&revert_fg_r, &revert_fg_g, &revert_fg_b);
      palette_get_background (&revert_bg_r, &revert_bg_g, &revert_bg_b);
    }
  if (active_color == FOREGROUND)
    {
      palette_get_foreground (&r, &g, &b);
      edit_color = FOREGROUND;
    }
  else
    {
      palette_get_background (&r, &g, &b);
      edit_color = BACKGROUND;
    }

  if (! color_select)
    {
      color_select = color_select_new (r, g, b, color_area_select_callback, NULL, TRUE);
      color_select_active = 1;
    }
  else
    {
      if (! color_select_active)
	color_select_show (color_select);
      color_select_set_color (color_select, r, g, b, 1);
    }
}

static void
color_area_pressed (GtkGestureClick *gesture,
		    int              n_press,
		    double           x,
		    double           y,
		    gpointer         data)
{
  int target;

  switch ((target = color_area_target (x, y)))
    {
    case FORE_AREA:
    case BACK_AREA:
      if (target == active_color)
	color_area_edit ();
      else
	{
	  active_color = target;
	  color_area_draw ();
	}
      break;
    case SWAP_AREA:
      palette_swap_colors();
      color_area_draw ();
      break;
    case DEF_AREA:
      palette_set_default_colors();
      color_area_draw ();
      break;
    }
}

GtkWidget *
color_area_create (int         width,
		   int         height,
		   GdkTexture *default_tex,
		   GdkTexture *swap_tex)
{
  GtkGesture *click;

  color_area = gtk_drawing_area_new ();
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (color_area), width);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (color_area), height);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (color_area),
				  color_area_draw_func, NULL, NULL);

  click = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 1);
  g_signal_connect (click, "pressed", G_CALLBACK (color_area_pressed), NULL);
  gtk_widget_add_controller (color_area, GTK_EVENT_CONTROLLER (click));

  default_texture = default_tex;
  swap_texture    = swap_tex;

  return color_area;
}

void
color_area_update ()
{
  color_area_draw ();
}
