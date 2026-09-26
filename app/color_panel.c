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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "appenv.h"
#include "color_panel.h"
#include "color_select.h"
#include "colormaps.h"

typedef struct _ColorPanelPrivate ColorPanelPrivate;

struct _ColorPanelPrivate
{
  GtkWidget *drawing_area;

  ColorSelectP color_select;
  int color_select_active;
};

static void color_panel_draw (ColorPanel *);
static void color_panel_draw_func (GtkDrawingArea *, cairo_t *, int, int, gpointer);
static void color_panel_pressed (GtkGestureClick *, gint, gdouble, gdouble, gpointer);
static void color_panel_select_callback (int, int, int, ColorSelectState, void *);


ColorPanel *
color_panel_new (unsigned char *initial,
		 int            width,
		 int            height)
{
  ColorPanel *color_panel;
  ColorPanelPrivate *private;
  GtkGesture *click;
  int i;

  color_panel = g_new (ColorPanel, 1);
  private = g_new (ColorPanelPrivate, 1);
  private->color_select = NULL;
  private->color_select_active = 0;
  color_panel->private_part = private;

  /*  set the initial color  */
  for (i = 0; i < 3; i++)
    color_panel->color[i] = (initial) ? initial[i] : 0;

  color_panel->color_panel_widget = gtk_frame_new (NULL);

  /*  drawing area  */
  private->drawing_area = gtk_drawing_area_new ();
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (private->drawing_area), width);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (private->drawing_area), height);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (private->drawing_area),
				  color_panel_draw_func, color_panel, NULL);

  click = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 1);
  g_signal_connect (click, "pressed",
		    G_CALLBACK (color_panel_pressed), color_panel);
  gtk_widget_add_controller (private->drawing_area, GTK_EVENT_CONTROLLER (click));

  g_object_set_data (G_OBJECT (private->drawing_area), "user_data", color_panel);
  gtk_frame_set_child (GTK_FRAME (color_panel->color_panel_widget), private->drawing_area);

  return color_panel;
}

void
color_panel_free (ColorPanel *color_panel)
{
  ColorPanelPrivate *private;

  private = (ColorPanelPrivate *) color_panel->private_part;

  /* make sure we hide and free color_select */
  if (private->color_select)
    {
      color_select_hide (private->color_select);
      color_select_free (private->color_select);
    }

  /*  The drawing area may outlive us for a moment: stop drawing from
   *  the freed panel.
   */
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (private->drawing_area),
				  NULL, NULL, NULL);

  g_free (color_panel->private_part);
  g_free (color_panel);
}

static void
color_panel_draw (ColorPanel *color_panel)
{
  ColorPanelPrivate *private;

  private = (ColorPanelPrivate *) color_panel->private_part;
  gtk_widget_queue_draw (private->drawing_area);
}

static void
color_panel_draw_func (GtkDrawingArea *area,
		       cairo_t        *cr,
		       int             width,
		       int             height,
		       gpointer        data)
{
  ColorPanel *color_panel = (ColorPanel *) data;

  cairo_set_source_rgb (cr,
			color_panel->color[0] / 255.0,
			color_panel->color[1] / 255.0,
			color_panel->color[2] / 255.0);
  cairo_rectangle (cr, 0, 0, width, height);
  cairo_fill (cr);
}

static void
color_panel_pressed (GtkGestureClick *gesture,
		     gint             n_press,
		     gdouble          x,
		     gdouble          y,
		     gpointer         data)
{
  ColorPanel *color_panel;
  ColorPanelPrivate *private;

  color_panel = (ColorPanel *) data;
  private = (ColorPanelPrivate *) color_panel->private_part;

  if (! private->color_select)
    {
      private->color_select = color_select_new (color_panel->color[0],
						color_panel->color[1],
						color_panel->color[2],
						color_panel_select_callback,
						color_panel,
						FALSE);
      private->color_select_active = 1;
    }
  else
    {
      if (! private->color_select_active)
	color_select_show (private->color_select);
      color_select_set_color (private->color_select,
			      color_panel->color[0],
			      color_panel->color[1],
			      color_panel->color[2], 1);
    }
}

static void
color_panel_select_callback (int   r,
			     int   g,
			     int   b,
			     ColorSelectState state,
			     void *client_data)
{
  ColorPanel *color_panel;
  ColorPanelPrivate *private;

  color_panel = (ColorPanel *) client_data;
  private = (ColorPanelPrivate *) color_panel->private_part;

  if (private->color_select)
    {
      switch (state) {
      case COLOR_SELECT_UPDATE:
	break;
      case COLOR_SELECT_OK:
	color_panel->color[0] = r;
	color_panel->color[1] = g;
	color_panel->color[2] = b;

	color_panel_draw (color_panel);
	/* Fallthrough */
      case COLOR_SELECT_CANCEL:
	color_select_hide (private->color_select);
	private->color_select_active = 0;
      }
    }
}
