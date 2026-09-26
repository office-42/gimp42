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
#include <math.h>
#include "appenv.h"
#include "draw_core.h"

/*  Every core in existence, so a canvas can draw the ones shown on it.  */
static GList *draw_cores = NULL;


DrawCore *
draw_core_new (DrawCoreDraw draw_func)
{
  DrawCore * core;

  core = (DrawCore *) g_malloc (sizeof (DrawCore));

  core->draw_func    = draw_func;
  core->draw_state   = INVISIBLE;
  core->canvas       = NULL;
  core->cr           = NULL;
  core->paused_count = 0;
  core->data         = NULL;
  core->line_width   = 1;
  core->line_style   = GIMP_LINE_SOLID;
  core->cap_style    = 0;
  core->join_style   = 0;

  draw_cores = g_list_prepend (draw_cores, core);

  return core;
}


void
draw_core_queue_draw (DrawCore *core)
{
  if (core && core->canvas)
    gtk_widget_queue_draw (core->canvas);
}


void
draw_core_start (DrawCore  *core,
		 GtkWidget *canvas,
		 Tool      *tool)
{
  if (core->draw_state != INVISIBLE)
    draw_core_stop (core, tool);

  if (core->canvas != canvas)
    {
      if (core->canvas)
	g_object_remove_weak_pointer (G_OBJECT (core->canvas),
				      (gpointer *) &core->canvas);
      core->canvas = canvas;
      if (canvas)
	g_object_add_weak_pointer (G_OBJECT (canvas),
				   (gpointer *) &core->canvas);
    }

  core->data  = (void *) tool;
  core->paused_count = 0;  /*  reset pause counter to 0  */

  core->draw_state = VISIBLE;
  draw_core_queue_draw (core);
}


void
draw_core_stop (DrawCore *core,
		Tool     *tool)
{
  if (core->draw_state == INVISIBLE)
    return;

  core->draw_state = INVISIBLE;
  draw_core_queue_draw (core);
}


void
draw_core_resume (DrawCore *core,
		  Tool     *tool)
{
  core->paused_count = (core->paused_count > 0) ? core->paused_count - 1 : 0;
  if (core->paused_count == 0)
    {
      core->draw_state = VISIBLE;
      draw_core_queue_draw (core);
    }
}


void
draw_core_pause (DrawCore *core,
		 Tool     *tool)
{
  if (core->paused_count == 0)
    {
      core->draw_state = INVISIBLE;
      draw_core_queue_draw (core);
    }
  core->paused_count++;
}


void
draw_core_free (DrawCore *core)
{
  if (core)
    {
      draw_cores = g_list_remove (draw_cores, core);

      if (core->canvas)
	{
	  g_object_remove_weak_pointer (G_OBJECT (core->canvas),
					(gpointer *) &core->canvas);
	  gtk_widget_queue_draw (core->canvas);
	}
      g_free (core);
    }
}


void
draw_core_draw (DrawCore *core,
		cairo_t  *cr)
{
  if (!core || core->draw_state != VISIBLE || !core->data)
    return;

  cairo_save (cr);

  /*  White through a difference operator inverts what is underneath,
   *  which is what the X inverting GC did.
   */
  cairo_set_operator (cr, CAIRO_OPERATOR_DIFFERENCE);
  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_set_line_width (cr, MAX (core->line_width, 1));
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_SQUARE);
  cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
  if (core->line_style == GIMP_LINE_ON_OFF_DASH)
    {
      double dashes[2] = { 4.0, 4.0 };

      cairo_set_dash (cr, dashes, 2, 0.0);
    }

  core->cr = cr;
  (* core->draw_func) ((Tool *) core->data);
  core->cr = NULL;

  cairo_restore (cr);
}


void
draw_core_draw_canvas (GtkWidget *canvas,
		       cairo_t   *cr)
{
  GList *list;

  for (list = draw_cores; list; list = list->next)
    {
      DrawCore *core = list->data;

      if (core->canvas == canvas)
	draw_core_draw (core, cr);
    }
}


/*  Primitives.  Lines go through pixel centres, so that a one pixel
 *  line lands on exactly the pixels X would have drawn.
 */

static void
draw_core_stroke (DrawCore *core)
{
  cairo_stroke (core->cr);
}

void
draw_core_line (DrawCore *core,
		int       x1,
		int       y1,
		int       x2,
		int       y2)
{
  if (!core->cr)
    return;

  cairo_move_to (core->cr, x1 + 0.5, y1 + 0.5);
  cairo_line_to (core->cr, x2 + 0.5, y2 + 0.5);
  draw_core_stroke (core);
}

void
draw_core_rectangle (DrawCore *core,
		     gboolean  filled,
		     int       x,
		     int       y,
		     int       w,
		     int       h)
{
  if (!core->cr)
    return;

  if (filled)
    {
      cairo_rectangle (core->cr, x, y, w, h);
      cairo_fill (core->cr);
    }
  else
    {
      cairo_rectangle (core->cr, x + 0.5, y + 0.5, w, h);
      draw_core_stroke (core);
    }
}

void
draw_core_arc (DrawCore *core,
	       gboolean  filled,
	       int       x,
	       int       y,
	       int       w,
	       int       h,
	       int       angle1,
	       int       angle2)
{
  double start, end;

  if (!core->cr || w <= 0 || h <= 0)
    return;

  /*  GDK angles run counter-clockwise, cairo's clockwise.  */
  start = -(angle1 / 64.0) * G_PI / 180.0;
  end   = -((angle1 + angle2) / 64.0) * G_PI / 180.0;

  cairo_save (core->cr);
  cairo_translate (core->cr, x + w / 2.0 + (filled ? 0 : 0.5),
		   y + h / 2.0 + (filled ? 0 : 0.5));
  cairo_scale (core->cr, w / 2.0, h / 2.0);
  cairo_new_sub_path (core->cr);
  if (angle2 >= 0)
    cairo_arc_negative (core->cr, 0.0, 0.0, 1.0, start, end);
  else
    cairo_arc (core->cr, 0.0, 0.0, 1.0, start, end);
  if (filled)
    cairo_line_to (core->cr, 0.0, 0.0);
  cairo_restore (core->cr);

  if (filled)
    cairo_fill (core->cr);
  else
    draw_core_stroke (core);
}

void
draw_core_segments (DrawCore    *core,
		    GimpSegment *segs,
		    int          n)
{
  int i;

  if (!core->cr || n <= 0)
    return;

  for (i = 0; i < n; i++)
    {
      cairo_move_to (core->cr, segs[i].x1 + 0.5, segs[i].y1 + 0.5);
      cairo_line_to (core->cr, segs[i].x2 + 0.5, segs[i].y2 + 0.5);
    }
  draw_core_stroke (core);
}

void
draw_core_lines (DrawCore  *core,
		 GimpPoint *points,
		 int        n)
{
  int i;

  if (!core->cr || n <= 0)
    return;

  cairo_move_to (core->cr, points[0].x + 0.5, points[0].y + 0.5);
  for (i = 1; i < n; i++)
    cairo_line_to (core->cr, points[i].x + 0.5, points[i].y + 0.5);
  draw_core_stroke (core);
}

void
draw_core_polygon (DrawCore  *core,
		   gboolean   filled,
		   GimpPoint *points,
		   int        n)
{
  int i;

  if (!core->cr || n <= 0)
    return;

  cairo_move_to (core->cr, points[0].x + 0.5, points[0].y + 0.5);
  for (i = 1; i < n; i++)
    cairo_line_to (core->cr, points[i].x + 0.5, points[i].y + 0.5);
  cairo_close_path (core->cr);

  if (filled)
    cairo_fill (core->cr);
  else
    draw_core_stroke (core);
}

void
draw_core_points (DrawCore  *core,
		  GimpPoint *points,
		  int        n)
{
  int i;

  if (!core->cr)
    return;

  for (i = 0; i < n; i++)
    cairo_rectangle (core->cr, points[i].x, points[i].y, 1, 1);
  cairo_fill (core->cr);
}
