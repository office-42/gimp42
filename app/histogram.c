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
#include <string.h>
#include <math.h>
#include "appenv.h"
#include "drawable.h"
#include "errors.h"
#include "gdisplay.h"
#include "gimage.h"
#include "gimage_mask.h"
#include "histogram.h"
#include "tile_manager.h"

#define WAITING 0
#define WORKING 1

#define WORK_DELAY 1


/*  Local structures  */
typedef struct _HistogramPrivate
{
  HistogramRangeCallback  range_callback;
  void *                  user_data;
  int                     channel;
  HistogramValues         values;
  int                     start;
  int                     end;
} HistogramPrivate;


/**************************/
/*  Function definitions  */

static void
histogram_draw (GtkDrawingArea *area,
		cairo_t        *cr,
		int             alloc_width,
		int             alloc_height,
		gpointer        data)
{
  Histogram *histogram;
  HistogramPrivate *histogram_p;
  double max;
  double log_val;
  int i, x, y;
  int x1, x2;
  int width, height;

  histogram = (Histogram *) data;
  histogram_p = (HistogramPrivate *) histogram->private_part;
  width = alloc_width - 2;
  height = alloc_height - 2;

  if (width <= 0 || height <= 0)
    return;

  /*  find the maximum value  */
  max = 1.0;
  for (i = 0; i < 256; i++)
    {
      if (histogram_p->values[histogram_p->channel][i])
	log_val = log (histogram_p->values[histogram_p->channel][i]);
      else
	log_val = 0;

      if (log_val > max)
	max = log_val;
    }

  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  cairo_set_line_width (cr, 1.0);

  /*  Draw the axis  */
  cairo_move_to (cr, 1, height + 1.5);
  cairo_line_to (cr, width + 1, height + 1.5);

  /*  Draw the spikes  */
  for (i = 0; i < 256; i++)
    {
      x = (width * i) / 256 + 1;
      if (histogram_p->values[histogram_p->channel][i])
	y = (int) ((height * log (histogram_p->values[histogram_p->channel][i])) / max);
      else
	y = 0;
      if (y > 0)
	{
	  cairo_move_to (cr, x + 0.5, height + 2);
	  cairo_line_to (cr, x + 0.5, height + 1 - y);
	}
    }
  cairo_stroke (cr);

  /*  The selected range, inverted  */
  if (histogram_p->start >= 0)
    {
      x1 = (width * MIN (histogram_p->start, histogram_p->end)) / 256 + 1;
      x2 = (width * MAX (histogram_p->start, histogram_p->end)) / 256 + 1;
      cairo_set_operator (cr, CAIRO_OPERATOR_DIFFERENCE);
      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
      cairo_rectangle (cr, x1, 1, (x2 - x1) + 1, height);
      cairo_fill (cr);
      cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
    }
}

static int
histogram_x_to_value (Histogram *histogram,
		      double     x)
{
  int width;
  int value;

  width = gtk_widget_get_width (histogram->histogram_widget) - 2;
  if (width <= 0)
    return 0;

  value = (int) (((x - 1) * 256) / width);

  return BOUNDS (value, 0, 255);
}

static void
histogram_drag_begin (GtkGestureDrag *gesture,
		      double          x,
		      double          y,
		      gpointer        data)
{
  Histogram *histogram;
  HistogramPrivate *histogram_p;

  histogram = (Histogram *) data;
  histogram_p = (HistogramPrivate *) histogram->private_part;

  histogram_p->start = histogram_x_to_value (histogram, x);
  histogram_p->end = histogram_p->start;

  gtk_widget_queue_draw (histogram->histogram_widget);
}

static void
histogram_drag_update (GtkGestureDrag *gesture,
		       double          offset_x,
		       double          offset_y,
		       gpointer        data)
{
  Histogram *histogram;
  HistogramPrivate *histogram_p;
  double start_x, start_y;

  histogram = (Histogram *) data;
  histogram_p = (HistogramPrivate *) histogram->private_part;

  gtk_gesture_drag_get_start_point (gesture, &start_x, &start_y);
  histogram_p->start = histogram_x_to_value (histogram, start_x + offset_x);

  gtk_widget_queue_draw (histogram->histogram_widget);
}

static void
histogram_drag_end (GtkGestureDrag *gesture,
		    double          offset_x,
		    double          offset_y,
		    gpointer        data)
{
  Histogram *histogram;
  HistogramPrivate *histogram_p;

  histogram = (Histogram *) data;
  histogram_p = (HistogramPrivate *) histogram->private_part;

  (* histogram_p->range_callback) (MIN (histogram_p->start, histogram_p->end),
				   MAX (histogram_p->start, histogram_p->end),
				   histogram_p->values,
				   histogram_p->user_data);
}

Histogram *
histogram_create (int                     width,
		  int                     height,
		  HistogramRangeCallback  range_callback,
		  void                   *user_data)
{
  Histogram *histogram;
  HistogramPrivate *histogram_p;
  GtkGesture *drag;
  int i, j;

  histogram = (Histogram *) g_malloc (sizeof (Histogram));
  histogram->histogram_widget = gtk_drawing_area_new ();
  /*  Keep our own reference: the histogram may outlive its dialog  */
  g_object_ref_sink (histogram->histogram_widget);
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (histogram->histogram_widget), width + 2);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (histogram->histogram_widget), height + 2);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (histogram->histogram_widget),
				  histogram_draw, histogram, NULL);

  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), 0);
  g_signal_connect (drag, "drag-begin", G_CALLBACK (histogram_drag_begin), histogram);
  g_signal_connect (drag, "drag-update", G_CALLBACK (histogram_drag_update), histogram);
  g_signal_connect (drag, "drag-end", G_CALLBACK (histogram_drag_end), histogram);
  gtk_widget_add_controller (histogram->histogram_widget, GTK_EVENT_CONTROLLER (drag));
  g_object_set_data (G_OBJECT (histogram->histogram_widget), "histogram-drag", drag);

  g_object_set_data (G_OBJECT (histogram->histogram_widget), "user_data", histogram);

  /*  The private details of the histogram  */
  histogram_p = (HistogramPrivate *) g_malloc (sizeof (HistogramPrivate));
  histogram->private_part = (void *) histogram_p;
  histogram_p->range_callback = range_callback;
  histogram_p->user_data = user_data;
  histogram_p->channel = HISTOGRAM_VALUE;
  histogram_p->start = 0;
  histogram_p->end = 255;

  /*  Initialize the values array  */
  for (j = 0; j < 5; j++)
    for (i = 0; i < 256; i++)
      histogram_p->values[j][i] = 0.0;

  return histogram;
}

void
histogram_free (Histogram  *histogram)
{
  GtkWidget *widget;
  GtkEventController *drag;

  widget = histogram->histogram_widget;

  /*  Nothing may call back into the histogram once it is gone  */
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (widget), NULL, NULL, NULL);
  drag = g_object_get_data (G_OBJECT (widget), "histogram-drag");
  if (drag)
    {
      g_signal_handlers_disconnect_matched (drag, G_SIGNAL_MATCH_DATA,
					    0, 0, NULL, NULL, histogram);
      gtk_widget_remove_controller (widget, drag);
    }
  g_object_set_data (G_OBJECT (widget), "histogram-drag", NULL);
  g_object_set_data (G_OBJECT (widget), "user_data", NULL);

  gimp_widget_destroy (widget);
  g_object_unref (widget);

  g_free (histogram->private_part);
  g_free (histogram);
}

void
histogram_update (Histogram         *histogram,
		  GimpDrawable      *drawable,
		  HistogramInfoFunc  info_func,
		  void              *user_data)
{
  HistogramPrivate *histogram_p;
  GImage *gimage;
  Channel *mask;
  PixelRegion srcPR, maskPR;
  int no_mask;
  void *pr;
  int x1, y1, x2, y2;
  int off_x, off_y;
  int i, j;

  histogram_p = (HistogramPrivate *) histogram->private_part;

  /*  Make sure the drawable is still valid  */
  if (! (gimage = drawable_gimage ( (drawable))))
    return;

  /*  The information collection should occur only within selection bounds  */
  no_mask = (drawable_mask_bounds ( (drawable), &x1, &y1, &x2, &y2) == FALSE);
  drawable_offsets ( (drawable), &off_x, &off_y);

  /*  Configure the src from the drawable data  */
  pixel_region_init (&srcPR, drawable_data ( (drawable)),
		     x1, y1, (x2 - x1), (y2 - y1), FALSE);

  /*  Configure the mask from the gimage's selection mask  */
  mask = gimage_get_mask (gimage);
  pixel_region_init (&maskPR, drawable_data (GIMP_DRAWABLE(mask)),
		     x1 + off_x, y1 + off_y, (x2 - x1), (y2 - y1), FALSE);

  /*  Initialize the values array  */
  for (j = 0; j < 4; j++)
    for (i = 0; i < 256; i++)
      histogram_p->values[j][i] = 0.0;

  /*  Apply the image transformation to the pixels  */
  if (no_mask)
    for (pr = pixel_regions_register (1, &srcPR); pr != NULL; pr = pixel_regions_process (pr))
      (* info_func) (&srcPR, NULL, histogram_p->values, user_data);
  else
    for (pr = pixel_regions_register (2, &srcPR, &maskPR); pr != NULL; pr = pixel_regions_process (pr))
      (* info_func) (&srcPR, &maskPR, histogram_p->values, user_data);

  /*  Make sure the histogram is updated  */
  gtk_widget_queue_draw (histogram->histogram_widget);

  /*  Give a range callback  */
  (* histogram_p->range_callback) (MIN (histogram_p->start, histogram_p->end),
				   MAX (histogram_p->start, histogram_p->end),
				   histogram_p->values,
				   histogram_p->user_data);
}

void
histogram_range (Histogram *histogram,
		 int        start,
		 int        end)
{
  HistogramPrivate *histogram_p;

  histogram_p = (HistogramPrivate *) histogram->private_part;

  histogram_p->start = start;
  histogram_p->end = end;
  gtk_widget_queue_draw (histogram->histogram_widget);
}

void
histogram_channel (Histogram *histogram,
		   int        channel)
{
  HistogramPrivate *histogram_p;

  histogram_p = (HistogramPrivate *) histogram->private_part;
  histogram_p->channel = channel;
  gtk_widget_queue_draw (histogram->histogram_widget);

  /*  Give a range callback  */
  (* histogram_p->range_callback) (MIN (histogram_p->start, histogram_p->end),
				   MAX (histogram_p->start, histogram_p->end),
				   histogram_p->values,
				   histogram_p->user_data);
}

HistogramValues *
histogram_values (Histogram *histogram)
{
  HistogramPrivate *histogram_p;

  histogram_p = (HistogramPrivate *) histogram->private_part;

  return &histogram_p->values;
}
