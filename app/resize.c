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
#include "resize.h"

#define DRAWING_AREA_SIZE 200
#define TEXT_WIDTH 35

typedef struct _ResizePrivate ResizePrivate;

struct _ResizePrivate
{
  GtkWidget *width_text;
  GtkWidget *height_text;
  GtkWidget *ratio_x_text;
  GtkWidget *ratio_y_text;
  GtkWidget *off_x_text;
  GtkWidget *off_y_text;
  GtkWidget *drawing_area;

  double ratio;
  int constrain;
  int old_width, old_height;
  int area_width, area_height;
  int start_x, start_y;
  int orig_x, orig_y;
};

static void resize_draw (Resize *);
static void resize_draw_func (GtkDrawingArea *area, cairo_t *cr,
			      int width, int height, gpointer data);
static int  resize_bound_off_x (Resize *, int);
static int  resize_bound_off_y (Resize *, int);
static void off_x_update (GtkWidget *w, gpointer data);
static void off_y_update (GtkWidget *w, gpointer data);
static void width_update (GtkWidget *w, gpointer data);
static void height_update (GtkWidget *w, gpointer data);
static void ratio_x_update (GtkWidget *w, gpointer data);
static void ratio_y_update (GtkWidget *w, gpointer data);
static void constrain_update (GtkWidget *w, gpointer data);
static void resize_drag_begin (GtkGestureDrag *gesture, double x, double y,
			       gpointer data);
static void resize_drag_update (GtkGestureDrag *gesture, double dx, double dy,
				gpointer data);

/*  Sets an entry's text without running this widget's own handlers.  */
static void
resize_entry_set_text (GtkWidget  *entry,
		       const char *text,
		       gpointer    data)
{
  g_signal_handlers_block_matched (entry, G_SIGNAL_MATCH_DATA,
				   0, 0, NULL, NULL, data);
  gtk_editable_set_text (GTK_EDITABLE (entry), text);
  g_signal_handlers_unblock_matched (entry, G_SIGNAL_MATCH_DATA,
				     0, 0, NULL, NULL, data);
}

/*  A label and an entry in row of the table.  */
static GtkWidget *
resize_entry_new (GtkWidget  *table,
		  int         row,
		  const char *label_text,
		  const char *text,
		  GCallback   callback,
		  Resize     *resize)
{
  GtkWidget *label;
  GtkWidget *entry;

  label = gtk_label_new (label_text);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, row, row + 1,
		     GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 2, 2);

  entry = gtk_entry_new ();
  gimp_table_attach (table, entry, 1, 2, row, row + 1,
		     GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 2, 2);
  gtk_widget_set_size_request (entry, TEXT_WIDTH, -1);
  gtk_editable_set_width_chars (GTK_EDITABLE (entry), 6);
  gtk_editable_set_text (GTK_EDITABLE (entry), text);
  g_signal_connect (entry, "changed", callback, resize);

  return entry;
}


Resize *
resize_widget_new (ResizeType type,
		   int        width,
		   int        height)
{
  Resize *resize;
  ResizePrivate *private;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *frame;
  GtkWidget *constrain;
  GtkWidget *table;
  GtkGesture *drag;
  char size[12];
  char ratio_text[12];

  table = NULL;

  resize = g_new (Resize, 1);
  private = g_new0 (ResizePrivate, 1);
  resize->type = type;
  resize->private_part = private;
  resize->width = width;
  resize->height = height;
  resize->ratio_x = 1.0;
  resize->ratio_y = 1.0;
  resize->off_x = 0;
  resize->off_y = 0;
  private->old_width = width;
  private->old_height = height;
  private->constrain = TRUE;

  /*  Get the image width and height variables, based on the gimage  */
  if (width > height)
    private->ratio = (double) DRAWING_AREA_SIZE / (double) width;
  else
    private->ratio = (double) DRAWING_AREA_SIZE / (double) height;
  private->area_width = (int) (private->ratio * width);
  private->area_height = (int) (private->ratio * height);

  switch (type)
    {
    case ScaleWidget:
      resize->resize_widget = gtk_frame_new ("Scale");
      table = gimp_table_new (4, 2, TRUE);
      break;
    case ResizeWidget:
      resize->resize_widget = gtk_frame_new ("Resize");
      table = gimp_table_new (6, 2, TRUE);
      break;
    }

  /*  the main vbox  */
  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, 5);
  gtk_frame_set_child (GTK_FRAME (resize->resize_widget), vbox);

  gimp_container_set_border_width (table, 2);
  gimp_box_pack_start (vbox, table, TRUE, TRUE, 0);

  /*  the width label and entry  */
  g_snprintf (size, sizeof (size), "%d", width);
  private->width_text = resize_entry_new (table, 0, "New width:", size,
					  G_CALLBACK (width_update), resize);

  /*  the height label and entry  */
  g_snprintf (size, sizeof (size), "%d", height);
  private->height_text = resize_entry_new (table, 1, "New height:", size,
					   G_CALLBACK (height_update), resize);

  /*  the x scale ratio label and entry  */
  g_snprintf (ratio_text, sizeof (ratio_text), "%0.4f", resize->ratio_x);
  private->ratio_x_text = resize_entry_new (table, 2, "X ratio:", ratio_text,
					    G_CALLBACK (ratio_x_update), resize);

  /*  the y scale ratio label and entry  */
  g_snprintf (ratio_text, sizeof (ratio_text), "%0.4f", resize->ratio_y);
  private->ratio_y_text = resize_entry_new (table, 3, "Y ratio:", ratio_text,
					    G_CALLBACK (ratio_y_update), resize);

  if (type == ResizeWidget)
    {
      /*  the off_x label and entry  */
      g_snprintf (size, sizeof (size), "%d", 0);
      private->off_x_text = resize_entry_new (table, 4, "X Offset:", size,
					      G_CALLBACK (off_x_update), resize);

      /*  the off_y label and entry  */
      g_snprintf (size, sizeof (size), "%d", 0);
      private->off_y_text = resize_entry_new (table, 5, "Y Offset:", size,
					      G_CALLBACK (off_y_update), resize);
    }

  /*  the constrain toggle button  */
  constrain = gtk_check_button_new_with_label ("Constrain Ratio");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (constrain), private->constrain);
  gimp_box_pack_start (vbox, constrain, FALSE, FALSE, 0);
  g_signal_connect (constrain, "toggled",
		    G_CALLBACK (constrain_update),
		    resize);

  if (type == ResizeWidget)
    {
      /*  frame to hold drawing area  */
      hbox = gimp_hbox_new (FALSE, 1);
      gimp_box_pack_start (vbox, hbox, TRUE, FALSE, 0);
      frame = gtk_frame_new (NULL);
      gimp_container_set_border_width (frame, 2);
      gimp_box_pack_start (hbox, frame, TRUE, FALSE, 0);
      private->drawing_area = gtk_drawing_area_new ();
      gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (private->drawing_area),
					  private->area_width);
      gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (private->drawing_area),
					   private->area_height);
      gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (private->drawing_area),
				      resize_draw_func, resize, NULL);

      /*  dragging the image around sets the offsets  */
      drag = gtk_gesture_drag_new ();
      gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), GDK_BUTTON_PRIMARY);
      g_signal_connect (drag, "drag-begin",
			G_CALLBACK (resize_drag_begin), resize);
      g_signal_connect (drag, "drag-update",
			G_CALLBACK (resize_drag_update), resize);
      gtk_widget_add_controller (private->drawing_area,
				 GTK_EVENT_CONTROLLER (drag));

      g_object_set_data (G_OBJECT (private->drawing_area), "user_data", resize);
      gtk_frame_set_child (GTK_FRAME (frame), private->drawing_area);
    }

  return resize;
}

void
resize_widget_free (Resize *resize)
{
  g_free (resize->private_part);
  g_free (resize);
}

/*  The virtual canvas: the larger of the old and the new size.  */
static void
resize_canvas_size (Resize *resize,
		    int    *w,
		    int    *h)
{
  ResizePrivate *private;

  private = (ResizePrivate *) resize->private_part;

  /*  If we're making the size larger  */
  if (private->old_width <= resize->width)
    *w = resize->width;
  /*  otherwise, if we're making the size smaller  */
  else
    *w = private->old_width * 2 - resize->width;
  /*  If we're making the size larger  */
  if (private->old_height <= resize->height)
    *h = resize->height;
  /*  otherwise, if we're making the size smaller  */
  else
    *h = private->old_height * 2 - resize->height;
}

static void
resize_draw (Resize *resize)
{
  ResizePrivate *private;
  int aw, ah;
  int w, h;

  /*  Only need to draw if it's a resize widget  */
  if (resize->type != ResizeWidget)
    return;

  private = (ResizePrivate *) resize->private_part;

  resize_canvas_size (resize, &w, &h);

  if (w <= 0 || h <= 0)
    return;

  if (w > h)
    private->ratio = (double) DRAWING_AREA_SIZE / (double) w;
  else
    private->ratio = (double) DRAWING_AREA_SIZE / (double) h;

  aw = (int) (private->ratio * w);
  ah = (int) (private->ratio * h);

  if (aw != private->area_width || ah != private->area_height)
    {
      private->area_width = aw;
      private->area_height = ah;
      gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (private->drawing_area),
					  MAX (aw, 1));
      gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (private->drawing_area),
					   MAX (ah, 1));
    }

  gtk_widget_queue_draw (private->drawing_area);
}

static void
resize_draw_func (GtkDrawingArea *area,
		  cairo_t        *cr,
		  int             width,
		  int             height,
		  gpointer        data)
{
  Resize *resize;
  ResizePrivate *private;
  int aw, ah;
  int x, y;
  int w, h;

  resize = (Resize *) data;
  private = (ResizePrivate *) resize->private_part;

  aw = private->area_width;
  ah = private->area_height;

  if (private->old_width <= resize->width)
    x = private->ratio * resize->off_x;
  else
    x = private->ratio * (resize->off_x + private->old_width - resize->width);
  if (private->old_height <= resize->height)
    y = private->ratio * resize->off_y;
  else
    y = private->ratio * (resize->off_y + private->old_height - resize->height);

  w = private->ratio * private->old_width;
  h = private->ratio * private->old_height;

  /*  the image, as a raised box  */
  cairo_set_line_width (cr, 1.0);
  cairo_set_source_rgb (cr, 0.85, 0.85, 0.85);
  cairo_rectangle (cr, x, y, w, h);
  cairo_fill (cr);

  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_move_to (cr, x + 0.5, y + h - 0.5);
  cairo_line_to (cr, x + 0.5, y + 0.5);
  cairo_line_to (cr, x + w - 0.5, y + 0.5);
  cairo_stroke (cr);

  cairo_set_source_rgb (cr, 0.4, 0.4, 0.4);
  cairo_move_to (cr, x + w - 0.5, y + 0.5);
  cairo_line_to (cr, x + w - 0.5, y + h - 0.5);
  cairo_line_to (cr, x + 0.5, y + h - 0.5);
  cairo_stroke (cr);

  /*  If we're making the size smaller  */
  if (private->old_width > resize->width ||
      private->old_height > resize->height)
    {
      if (private->old_width > resize->width)
	{
	  x = private->ratio * (private->old_width - resize->width);
	  w = private->ratio * resize->width;
	}
      else
	{
	  x = -1;
	  w = aw + 2;
	}
      if (private->old_height > resize->height)
	{
	  y = private->ratio * (private->old_height - resize->height);
	  h = private->ratio * resize->height;
	}
      else
	{
	  y = -1;
	  h = ah + 2;
	}

      cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
      cairo_rectangle (cr, x + 0.5, y + 0.5, w, h);
      cairo_stroke (cr);
    }
}

static int
resize_bound_off_x (Resize *resize,
		    int     off_x)
{
  ResizePrivate *private;

  private = (ResizePrivate *) resize->private_part;

  if (private->old_width <= resize->width)
    off_x = BOUNDS (off_x, 0, (resize->width - private->old_width));
  else
    off_x = BOUNDS (off_x, (resize->width - private->old_width), 0);

  return off_x;
}

static int
resize_bound_off_y (Resize *resize,
		    int     off_y)
{
  ResizePrivate *private;

  private = (ResizePrivate *) resize->private_part;

  if (private->old_height <= resize->height)
    off_y = BOUNDS (off_y, 0, (resize->height - private->old_height));
  else
    off_y = BOUNDS (off_y, (resize->height - private->old_height), 0);

  return off_y;
}

static void
constrain_update (GtkWidget *w,
		  gpointer   data)
{
  Resize *resize;
  ResizePrivate *private;

  resize = (Resize *) data;
  private = (ResizePrivate *) resize->private_part;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    private->constrain = TRUE;
  else
    private->constrain = FALSE;
}

static void
off_x_update (GtkWidget *w,
	      gpointer   data)
{
  Resize *resize;
  const char *str;
  int offset;

  resize = (Resize *) data;
  str = gtk_editable_get_text (GTK_EDITABLE (w));

  offset = atoi (str);
  offset = resize_bound_off_x (resize, offset);

  if (offset != resize->off_x)
    {
      resize->off_x = offset;
      resize_draw (resize);
    }
}

static void
off_y_update (GtkWidget *w,
	      gpointer   data)
{
  Resize *resize;
  const char *str;
  int offset;

  resize = (Resize *) data;
  str = gtk_editable_get_text (GTK_EDITABLE (w));

  offset = atoi (str);
  offset = resize_bound_off_y (resize, offset);

  if (offset != resize->off_y)
    {
      resize->off_y = offset;
      resize_draw (resize);
    }
}

static void
width_update (GtkWidget *w,
	      gpointer   data)
{
  Resize *resize;
  ResizePrivate *private;
  const char *str;
  double ratio;
  int new_height;
  char size[12];
  char ratio_text[12];

  resize = (Resize *) data;
  private = (ResizePrivate *) resize->private_part;
  str = gtk_editable_get_text (GTK_EDITABLE (w));

  resize->width = atoi (str);

  ratio = (double) resize->width / (double) private->old_width;
  resize->ratio_x = ratio;
  g_snprintf (ratio_text, sizeof (ratio_text), "%0.4f", ratio);  

  resize_entry_set_text (private->ratio_x_text, ratio_text, data);

  if (resize->type == ResizeWidget)
    {
      resize->off_x = resize_bound_off_x (resize, (resize->width - private->old_width) / 2);
      g_snprintf (size, sizeof (size), "%d", resize->off_x);

      resize_entry_set_text (private->off_x_text, size, data);
    }

  if (private->constrain && resize->width != 0)
    {
      private->constrain = FALSE;
      new_height = (int) (private->old_height * ratio);
      if (new_height == 0) new_height = 1;

      if (new_height != resize->height)
	{
	  resize->height = new_height;
	  g_snprintf (size, sizeof (size), "%d", resize->height);

	  resize_entry_set_text (private->height_text, size, data);

	  resize->ratio_y = ratio;

	  resize_entry_set_text (private->ratio_y_text, ratio_text, data);

	  if (resize->type == ResizeWidget)
	    {
	      resize->off_y = resize_bound_off_y (resize, (resize->height - private->old_height) / 2);
	      g_snprintf (size, sizeof (size), "%d", resize->off_y);

	      resize_entry_set_text (private->off_y_text, size, data);
	    }
	}

      private->constrain = TRUE;
    }

  resize_draw (resize);
}

static void
height_update (GtkWidget *w,
	       gpointer   data)
{
  Resize *resize;
  ResizePrivate *private;
  const char *str;
  double ratio;
  int new_width;
  char size[12];
  char ratio_text[12];

  resize = (Resize *) data;
  private = (ResizePrivate *) resize->private_part;
  str = gtk_editable_get_text (GTK_EDITABLE (w));

  resize->height = atoi (str);

  ratio = (double) resize->height / (double) private->old_height;
  resize->ratio_y = ratio;
  g_snprintf (ratio_text, sizeof (ratio_text), "%0.4f", ratio);

  resize_entry_set_text (private->ratio_y_text, ratio_text, data);
  if (resize->type == ResizeWidget)
    {
      resize->off_y = resize_bound_off_y (resize, (resize->height - private->old_height) / 2);
      g_snprintf (size, sizeof (size), "%d", resize->off_y);

      resize_entry_set_text (private->off_y_text, size, data);
    }

  if (private->constrain && resize->height != 0)
    {
      private->constrain = FALSE;
      ratio = (double) resize->height / (double) private->old_height;
      new_width = (int) (private->old_width * ratio);
      if (new_width == 0) new_width = 1;

      if (new_width != resize->width)
	{
	  resize->width = new_width;
	  g_snprintf (size, sizeof (size), "%d", resize->width);

	  resize_entry_set_text (private->width_text, size, data);
	  
	  resize->ratio_x = ratio;

	  resize_entry_set_text (private->ratio_x_text, ratio_text, data);

	  if (resize->type == ResizeWidget)
	    {
	      resize->off_x = resize_bound_off_x (resize, (resize->width - private->old_width) / 2);
	      g_snprintf (size, sizeof (size), "%d", resize->off_x);

	      resize_entry_set_text (private->off_x_text, size, data);
	    }
	}

      private->constrain = TRUE;
    }

  resize_draw (resize);
}

static void
ratio_x_update (GtkWidget *w,
		gpointer   data)
{
  Resize *resize;
  ResizePrivate *private;
  const char *str;
  int new_width;
  int new_height;
  char size[12];
  char ratio_text[12];
  
  resize = (Resize *) data;
  private = (ResizePrivate *) resize->private_part;
  str = gtk_editable_get_text (GTK_EDITABLE (w));

  resize->ratio_x = atof (str);

  new_width = (int) ((double) private->old_width * resize->ratio_x);

  if (new_width != resize->width)
    {
      resize->width = new_width;
      g_snprintf (size, sizeof (size), "%d", new_width);

      resize_entry_set_text (private->width_text, size, data);

      if (resize->type == ResizeWidget)
	{
	  resize->off_x = resize_bound_off_x (resize, (resize->width - private->old_width) / 2);
	  g_snprintf (size, sizeof (size), "%d", resize->off_x);
	  
	  resize_entry_set_text (private->off_x_text, size, data);
	}
    }

  if (private->constrain && resize->width != 0)
    {
      private->constrain = FALSE;

      resize->ratio_y = resize->ratio_x;

      new_height = (int) (private->old_height * resize->ratio_y);
      if (new_height == 0) new_height = 1;

      if (new_height != resize->height)
	{
	  resize->height = new_height;

	  g_snprintf (size, sizeof (size), "%d", resize->height);

	  resize_entry_set_text (private->height_text, size, data);
	  
	  g_snprintf (ratio_text, sizeof (ratio_text), "%0.4f", resize->ratio_y);  
	  
	  resize_entry_set_text (private->ratio_y_text, ratio_text, data);

	  if (resize->type == ResizeWidget)
	    {
	      resize->off_y = resize_bound_off_y (resize, (resize->height - private->old_height) / 2);
	      g_snprintf (size, sizeof (size), "%d", resize->off_y);

	      resize_entry_set_text (private->off_y_text, size, data);
	    }
	}

      private->constrain = TRUE;
    }

  resize_draw (resize);
}

static void
ratio_y_update (GtkWidget *w,
		gpointer   data)
{
  Resize *resize;
  ResizePrivate *private;
  const char *str;
  int new_width;
  int new_height;
  char size[12];
  char ratio_text[12];
  
  resize = (Resize *) data;
  private = (ResizePrivate *) resize->private_part;
  str = gtk_editable_get_text (GTK_EDITABLE (w));

  resize->ratio_y = atof (str);

  new_height = (int) ((double) private->old_height * resize->ratio_y);

  if (new_height != resize->height)
    {
      resize->height = new_height;
      g_snprintf (size, sizeof (size), "%d", new_height);

      resize_entry_set_text (private->height_text, size, data);

      if (resize->type == ResizeWidget)
	{
	  resize->off_y = resize_bound_off_y (resize, (resize->height - private->old_height) / 2);
	  g_snprintf (size, sizeof (size), "%d", resize->off_y);
	  
	  resize_entry_set_text (private->off_y_text, size, data);
	}
    }

  if (private->constrain && resize->height != 0)
    {
      private->constrain = FALSE;

      resize->ratio_x = resize->ratio_y;

      new_width = (int) (private->old_width * resize->ratio_x);
      if (new_width == 0) new_width = 1;

      if (new_width != resize->width)
	{
	  resize->width = new_width;

	  g_snprintf (size, sizeof (size), "%d", resize->width);

	  resize_entry_set_text (private->width_text, size, data);
	  
	  g_snprintf (ratio_text, sizeof (ratio_text), "%0.4f", resize->ratio_x);  
	  
	  resize_entry_set_text (private->ratio_x_text, ratio_text, data);

	  if (resize->type == ResizeWidget)
	    {
	      resize->off_x = resize_bound_off_x (resize, (resize->width - private->old_width) / 2);
	      g_snprintf (size, sizeof (size), "%d", resize->off_x);

	      resize_entry_set_text (private->off_x_text, size, data);
	    }
	}

      private->constrain = TRUE;
    }

  resize_draw (resize);
}

static void
resize_drag_begin (GtkGestureDrag *gesture,
		   double          x,
		   double          y,
		   gpointer        data)
{
  Resize *resize;
  ResizePrivate *private;

  resize = (Resize *) data;
  private = (ResizePrivate *) resize->private_part;

  private->orig_x = resize->off_x;
  private->orig_y = resize->off_y;
  private->start_x = x;
  private->start_y = y;
}

static void
resize_drag_update (GtkGestureDrag *gesture,
		    double          dx,
		    double          dy,
		    gpointer        data)
{
  Resize *resize;
  ResizePrivate *private;
  int off_x, off_y;
  char size[12];

  resize = (Resize *) data;
  private = (ResizePrivate *) resize->private_part;

  /*  X offset  */
  off_x = private->orig_x + (int) dx / private->ratio;
  off_x = resize_bound_off_x (resize, off_x);
  g_snprintf (size, sizeof (size), "%d", off_x);
  gtk_editable_set_text (GTK_EDITABLE (private->off_x_text), size);

  /*  Y offset  */
  off_y = private->orig_y + (int) dy / private->ratio;
  off_y = resize_bound_off_y (resize, off_y);
  g_snprintf (size, sizeof (size), "%d", off_y);
  gtk_editable_set_text (GTK_EDITABLE (private->off_y_text), size);
}
