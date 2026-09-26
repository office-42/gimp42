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
#ifndef __SELECTION_H__
#define __SELECTION_H__

#include <gtk/gtk.h>
#include "gimpsegment.h"

/*  The marching ants.  The selection keeps the boundary in canvas
 *  coordinates and a phase that a timer advances; the display draws
 *  it with selection_draw () from its draw function.
 */

typedef struct _selection Selection;

struct _selection
{
  GtkWidget *   canvas;          /*  Canvas the ants are drawn on      */
  void *        gdisp;           /*  GDisplay that owns the selection  */

  GimpSegment * segs_in;         /*  segments of area boundary         */
  GimpSegment * segs_out;        /*  segments of area boundary         */
  GimpSegment * segs_layer;      /*  segments of the layer boundary    */
  int           num_segs_in;     /*  number of segments in segs_in     */
  int           num_segs_out;    /*  number of segments in segs_out    */
  int           num_segs_layer;  /*  number of segments in segs_layer  */
  int           index_in;        /*  phase of the marching ants        */
  int           state;           /*  internal drawing state            */
  int           paused;          /*  count of pause requests           */
  int           recalc;          /*  flag to recalculate the selection */
  int           speed;           /*  speed of marching ants            */
  int           hidden;          /*  is the selection hidden?          */
  guint         timer;           /*  timer for successive draws        */
};

/*  Function declarations  */

Selection *  selection_create          (GtkWidget *, gpointer, int, int, int);
void         selection_pause           (Selection *);
void         selection_resume          (Selection *);
void         selection_start           (Selection *, int);
void         selection_invis           (Selection *);
void         selection_layer_invis     (Selection *);
void         selection_hide            (Selection *, void *);
void         selection_free            (Selection *);

/*  Draws the ants; called by the display from its draw function.  */
void         selection_draw            (Selection *, cairo_t *);

#endif  /*  __SELECTION_H__  */
