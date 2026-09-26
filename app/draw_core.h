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
#ifndef __DRAW_CORE_H__
#define __DRAW_CORE_H__

#include "gimpsegment.h"
#include "tools.h"

/*  drawing states  */
#define INVISIBLE   0
#define VISIBLE     1

/*  A tool's on-canvas feedback (rubber bands, handles, outlines).
 *
 *  Under X this was drawn with an inverting GC straight onto the window
 *  and erased by drawing it again.  Now the display redraws the canvas
 *  from scratch every frame: draw_core_start/stop/pause/resume only
 *  decide whether the core is shown and ask for a redraw, and the
 *  display calls draw_func from its draw function, with core->cr set,
 *  whenever the core is visible.  draw_func must therefore only draw;
 *  the draw_core_* primitives below draw with the inverting look.
 */

typedef struct _draw_core  DrawCore;
typedef void (* DrawCoreDraw) (Tool *);

struct _draw_core
{
  GtkWidget *     canvas;       /*  canvas the core is shown on            */
  cairo_t *       cr;           /*  only valid while draw_func runs        */

  int             draw_state;   /*  Current state in the drawing process    */

  int             line_width;   /**/
  int             line_style;   /*  line attributes: GIMP_LINE_*            */
  int             cap_style;    /**/
  int             join_style;   /**/

  int             paused_count; /*  count to keep track of multiple pauses  */

  gpointer        data;         /*  data to pass to draw_func               */

  DrawCoreDraw    draw_func;    /*  Member function for actual drawing      */
};

/*  line styles  */
#define GIMP_LINE_SOLID        0
#define GIMP_LINE_ON_OFF_DASH  1

/*  draw core functions  */

DrawCore *    draw_core_new          (DrawCoreDraw);
void          draw_core_start        (DrawCore *, GtkWidget *canvas, Tool *);
void          draw_core_stop         (DrawCore *, Tool *);
void          draw_core_pause        (DrawCore *, Tool *);
void          draw_core_resume       (DrawCore *, Tool *);
void          draw_core_free         (DrawCore *);

/*  Called by the display from its draw function: draws core, or every
 *  visible core on canvas.
 */
void          draw_core_draw         (DrawCore *, cairo_t *cr);
void          draw_core_draw_canvas  (GtkWidget *canvas, cairo_t *cr);

/*  Asks the canvas the core is on for a new frame.  */
void          draw_core_queue_draw   (DrawCore *);

/*  Drawing primitives for draw_func, in canvas coordinates, with GDK's
 *  pixel conventions (a 1 pixel line from x1 to x2 covers both ends; a
 *  rectangle outline of width w covers w + 1 pixels; arc angles are in
 *  64ths of a degree, counter-clockwise from 3 o'clock).
 */
void          draw_core_line         (DrawCore *, int x1, int y1, int x2, int y2);
void          draw_core_rectangle    (DrawCore *, gboolean filled,
				      int x, int y, int w, int h);
void          draw_core_arc          (DrawCore *, gboolean filled,
				      int x, int y, int w, int h,
				      int angle1, int angle2);
void          draw_core_segments     (DrawCore *, GimpSegment *segs, int n);
void          draw_core_lines        (DrawCore *, GimpPoint *points, int n);
void          draw_core_polygon      (DrawCore *, gboolean filled,
				      GimpPoint *points, int n);
void          draw_core_points       (DrawCore *, GimpPoint *points, int n);

#endif  /*  __DRAW_CORE_H__  */
