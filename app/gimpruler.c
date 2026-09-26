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
#include <string.h>
#include "gimpruler.h"

#define RULER_SIZE    16
#define MINIMUM_INCR   5
#define MAXIMUM_SUBDIVIDE 5

struct _GimpRuler
{
  GtkWidget       parent_instance;

  GtkOrientation  orientation;
  gdouble         lower;
  gdouble         upper;
  gdouble         max_size;
  gdouble         pointer;      /*  in pixels along the ruler, -1: none  */
};

G_DEFINE_FINAL_TYPE (GimpRuler, gimp_ruler, GTK_TYPE_WIDGET)

/*  The spacing of ticks, in ruler units, from finest to coarsest.  */
static const gdouble ruler_scale[] =
{ 1, 2, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 25000 };
static const gint ruler_subdivide[MAXIMUM_SUBDIVIDE] = { 1, 5, 10, 50, 100 };

static void
gimp_ruler_measure (GtkWidget      *widget,
		    GtkOrientation  orientation,
		    int             for_size,
		    int            *minimum,
		    int            *natural,
		    int            *minimum_baseline,
		    int            *natural_baseline)
{
  GimpRuler *ruler = GIMP_RULER (widget);

  if (orientation == ruler->orientation)
    *minimum = *natural = 1;
  else
    *minimum = *natural = RULER_SIZE;
}

static void
gimp_ruler_snapshot (GtkWidget   *widget,
		     GtkSnapshot *snapshot)
{
  GimpRuler   *ruler = GIMP_RULER (widget);
  gboolean     horizontal = (ruler->orientation == GTK_ORIENTATION_HORIZONTAL);
  int          width  = gtk_widget_get_width (widget);
  int          height = gtk_widget_get_height (widget);
  int          length = horizontal ? width : height;
  int          depth  = horizontal ? height : width;
  cairo_t     *cr;
  GdkRGBA      fg;
  PangoLayout *layout;
  gdouble      lower, upper, increment;
  gdouble      start, end, cur;
  gint         scale, i, text_size;
  char         unit_str[32];

  if (length <= 0 || depth <= 0)
    return;

  cr = gtk_snapshot_append_cairo (snapshot,
				  &GRAPHENE_RECT_INIT (0, 0, width, height));

  gtk_widget_get_color (widget, &fg);
  gdk_cairo_set_source_rgba (cr, &fg);
  cairo_set_line_width (cr, 1.0);

  /*  the base line  */
  if (horizontal)
    cairo_rectangle (cr, 0, height - 1, width, 1);
  else
    cairo_rectangle (cr, width - 1, 0, 1, height);
  cairo_fill (cr);

  lower = ruler->lower;
  upper = ruler->upper;
  if (upper <= lower)
    {
      cairo_destroy (cr);
      return;
    }

  increment = (gdouble) length / (upper - lower);

  /*  Big enough ticks for the widest number to fit between them.  */
  g_snprintf (unit_str, sizeof (unit_str), "%d", (int) ceil (MAX (ruler->max_size, fabs (upper))));
  text_size = strlen (unit_str) * 7 + 2;

  for (scale = 0; scale < (gint) G_N_ELEMENTS (ruler_scale) - 1; scale++)
    if (ruler_scale[scale] * fabs (increment) > text_size)
      break;

  layout = gtk_widget_create_pango_layout (widget, NULL);
  {
    PangoFontDescription *desc = pango_font_description_from_string ("Sans 7");
    pango_layout_set_font_description (layout, desc);
    pango_font_description_free (desc);
  }

  for (i = MAXIMUM_SUBDIVIDE - 1; i >= 0; i--)
    {
      gdouble subd_incr = ruler_scale[scale] / ruler_subdivide[i];
      int     tick_length;

      if (subd_incr * fabs (increment) <= MINIMUM_INCR)
	continue;

      tick_length = depth / (i + 1) - 1;

      start = floor (lower / subd_incr) * subd_incr;
      end   = ceil (upper / subd_incr) * subd_incr;

      for (cur = start; cur <= end; cur += subd_incr)
	{
	  int pos = (int) floor ((cur - lower) * increment);

	  if (horizontal)
	    cairo_rectangle (cr, pos, height - tick_length, 1, tick_length);
	  else
	    cairo_rectangle (cr, width - tick_length, pos, tick_length, 1);
	  cairo_fill (cr);

	  /*  numbers on the major ticks  */
	  if (i == 0)
	    {
	      g_snprintf (unit_str, sizeof (unit_str), "%d", (int) cur);
	      pango_layout_set_text (layout, unit_str, -1);

	      if (horizontal)
		{
		  cairo_move_to (cr, pos + 2, 0);
		  pango_cairo_show_layout (cr, layout);
		}
	      else
		{
		  int d;

		  /*  one digit under the other  */
		  for (d = 0; unit_str[d]; d++)
		    {
		      char digit[2] = { unit_str[d], 0 };

		      pango_layout_set_text (layout, digit, -1);
		      cairo_move_to (cr, 1, pos + 1 + d * 8);
		      pango_cairo_show_layout (cr, layout);
		    }
		}
	    }
	}
    }

  g_object_unref (layout);

  /*  the pointer marker  */
  if (ruler->pointer >= 0)
    {
      int p = (int) ruler->pointer;
      int s = depth / 3;

      if (horizontal)
	{
	  cairo_move_to (cr, p - s / 2.0, height - s - 1);
	  cairo_line_to (cr, p + s / 2.0, height - s - 1);
	  cairo_line_to (cr, p, height - 1);
	}
      else
	{
	  cairo_move_to (cr, width - s - 1, p - s / 2.0);
	  cairo_line_to (cr, width - s - 1, p + s / 2.0);
	  cairo_line_to (cr, width - 1, p);
	}
      cairo_close_path (cr);
      cairo_fill (cr);
    }

  cairo_destroy (cr);
}

static void
gimp_ruler_class_init (GimpRulerClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->measure  = gimp_ruler_measure;
  widget_class->snapshot = gimp_ruler_snapshot;
}

static void
gimp_ruler_init (GimpRuler *ruler)
{
  ruler->orientation = GTK_ORIENTATION_HORIZONTAL;
  ruler->pointer = -1;
}

GtkWidget *
gimp_ruler_new (GtkOrientation orientation)
{
  GimpRuler *ruler = g_object_new (GIMP_TYPE_RULER, NULL);

  ruler->orientation = orientation;

  return GTK_WIDGET (ruler);
}

void
gimp_ruler_set_range (GimpRuler *ruler,
		      gdouble    lower,
		      gdouble    upper,
		      gdouble    max_size)
{
  g_return_if_fail (GIMP_IS_RULER (ruler));

  ruler->lower    = lower;
  ruler->upper    = upper;
  ruler->max_size = max_size;

  gtk_widget_queue_draw (GTK_WIDGET (ruler));
}

void
gimp_ruler_get_range (GimpRuler *ruler,
		      gdouble   *lower,
		      gdouble   *upper,
		      gdouble   *max_size)
{
  g_return_if_fail (GIMP_IS_RULER (ruler));

  if (lower)
    *lower = ruler->lower;
  if (upper)
    *upper = ruler->upper;
  if (max_size)
    *max_size = ruler->max_size;
}

void
gimp_ruler_set_pointer (GimpRuler *ruler,
			gdouble    offset)
{
  g_return_if_fail (GIMP_IS_RULER (ruler));

  if (ruler->pointer != offset)
    {
      ruler->pointer = offset;
      gtk_widget_queue_draw (GTK_WIDGET (ruler));
    }
}
