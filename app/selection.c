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
#include "boundary.h"
#include "errors.h"
#include "gdisplay.h"
#include "gdisplay_ops.h"
#include "gimage_mask.h"
#include "gimprc.h"
#include "selection.h"

/*  The possible internal drawing states...  */
#define SEL_INVISIBLE     0
#define SEL_INTRO         1
#define SEL_MARCHING      2

#define INITIAL_DELAY     15  /* in milleseconds */


/* static function prototypes */
static void    selection_transform_segs    (Selection *, BoundSeg *, GimpSegment *, int);
static void    selection_generate_segs     (Selection *);
static void    selection_free_segs         (Selection *);
static gboolean selection_march_ants       (gpointer);
static gboolean selection_start_marching   (gpointer);


/*********************************/
/*  Local function definitions   */
/*********************************/

static void
selection_queue_draw (Selection *select)
{
  if (select->canvas)
    gtk_widget_queue_draw (select->canvas);
}

static void
selection_stop_timer (Selection *select)
{
  if (select->timer)
    {
      g_source_remove (select->timer);
      select->timer = 0;
    }
}

static void
selection_path_segs (cairo_t     *cr,
		     GimpSegment *segs,
		     int          n)
{
  int i;

  for (i = 0; i < n; i++)
    {
      cairo_move_to (cr, segs[i].x1 + 0.5, segs[i].y1 + 0.5);
      cairo_line_to (cr, segs[i].x2 + 0.5, segs[i].y2 + 0.5);
    }
}

void
selection_draw (Selection *select,
		cairo_t   *cr)
{
  double dashes[2] = { 4.0, 4.0 };

  if (!select || select->hidden || select->state == SEL_INVISIBLE)
    return;

  if (select->recalc)
    {
      selection_free_segs (select);
      selection_generate_segs (select);
      select->recalc = FALSE;
    }

  cairo_save (cr);
  cairo_set_line_width (cr, 1.0);
  cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_SQUARE);

  /*  The active layer's boundary: yellow and black dashes, standing
   *  still.
   */
  if (select->segs_layer)
    {
      selection_path_segs (cr, select->segs_layer, select->num_segs_layer);
      cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
      cairo_set_dash (cr, NULL, 0, 0.0);
      cairo_stroke_preserve (cr);
      cairo_set_source_rgb (cr, 1.0, 1.0, 0.0);
      cairo_set_dash (cr, dashes, 2, 0.0);
      cairo_stroke (cr);
    }

  /*  Selected area outside the current layer: gray and white.  */
  if (select->segs_out)
    {
      selection_path_segs (cr, select->segs_out, select->num_segs_out);
      cairo_set_source_rgb (cr, 0.5, 0.5, 0.5);
      cairo_set_dash (cr, NULL, 0, 0.0);
      cairo_stroke_preserve (cr);
      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
      cairo_set_dash (cr, dashes, 2, 0.0);
      cairo_stroke (cr);
    }

  /*  The marching ants proper: black and white, moving with index_in.  */
  if (select->segs_in)
    {
      selection_path_segs (cr, select->segs_in, select->num_segs_in);
      cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
      cairo_set_dash (cr, NULL, 0, 0.0);
      cairo_stroke_preserve (cr);
      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
      cairo_set_dash (cr, dashes, 2, (double) (8 - (select->index_in & 7)));
      cairo_stroke (cr);
    }

  cairo_restore (cr);
}


static void
selection_transform_segs (Selection   *select,
			  BoundSeg    *src_segs,
			  GimpSegment *dest_segs,
			  int          num_segs)
{
  GDisplay * gdisp;
  int x, y;
  int i;

  gdisp = (GDisplay *) select->gdisp;

  for (i = 0; i < num_segs; i++)
    {
      gdisplay_transform_coords (gdisp, src_segs[i].x1, src_segs[i].y1,
				 &x, &y, 0);

      dest_segs[i].x1 = x;
      dest_segs[i].y1 = y;

      gdisplay_transform_coords (gdisp, src_segs[i].x2, src_segs[i].y2,
				 &x, &y, 0);

      dest_segs[i].x2 = x;
      dest_segs[i].y2 = y;

      /*  If this segment is a closing segment && the segments lie inside
       *  the region, OR if this is an opening segment and the segments
       *  lie outside the region...
       *  we need to transform it by one display pixel
       */
      if (!src_segs[i].open)
	{
	  /*  If it is vertical  */
	  if (dest_segs[i].x1 == dest_segs[i].x2)
	    {
	      dest_segs[i].x1 -= 1;
	      dest_segs[i].x2 -= 1;
	    }
	  else
	    {
	      dest_segs[i].y1 -= 1;
	      dest_segs[i].y2 -= 1;
	    }
	}
    }
}


static void
selection_generate_segs (Selection *select)
{
  GDisplay * gdisp;
  BoundSeg *segs_in;
  BoundSeg *segs_out;
  BoundSeg *segs_layer;

  gdisp = (GDisplay *) select->gdisp;

  /*  Ask the gimage for the boundary of its selected region...
   *  Then transform that information into a new buffer of segments
   */
  gimage_mask_boundary (gdisp->gimage, &segs_in, &segs_out,
			&select->num_segs_in, &select->num_segs_out);
  if (select->num_segs_in)
    {
      select->segs_in = g_new (GimpSegment, select->num_segs_in);
      selection_transform_segs (select, segs_in, select->segs_in, select->num_segs_in);
    }
  else
    select->segs_in = NULL;

  /*  Possible secondary boundary representation  */
  if (select->num_segs_out)
    {
      select->segs_out = g_new (GimpSegment, select->num_segs_out);
      selection_transform_segs (select, segs_out, select->segs_out, select->num_segs_out);
    }
  else
    select->segs_out = NULL;

  /*  The active layer's boundary  */
  gimage_layer_boundary (gdisp->gimage, &segs_layer, &select->num_segs_layer);
  if (select->num_segs_layer)
    {
      select->segs_layer = g_new (GimpSegment, select->num_segs_layer);
      selection_transform_segs (select, segs_layer, select->segs_layer, select->num_segs_layer);
    }
  else
    select->segs_layer = NULL;

  g_free (segs_layer);
}


static void
selection_free_segs (Selection *select)
{
  g_free (select->segs_in);
  g_free (select->segs_out);
  g_free (select->segs_layer);

  select->segs_in        = NULL;
  select->num_segs_in    = 0;
  select->segs_out       = NULL;
  select->num_segs_out   = 0;
  select->segs_layer     = NULL;
  select->num_segs_layer = 0;
}


static gboolean
selection_start_marching (gpointer data)
{
  Selection * select;

  select = (Selection *) data;

  select->index_in = 0;

  /*  Make sure the state is set to marching  */
  select->state = SEL_MARCHING;

  selection_queue_draw (select);

  /*  Reset the timer  */
  select->timer = g_timeout_add (select->speed,
				 selection_march_ants,
				 (gpointer) select);

  return G_SOURCE_REMOVE;
}

static gboolean
selection_march_ants (gpointer data)
{
  Selection * select;

  select = (Selection *) data;

  /*  advance the phase of the ants  */
  select->index_in++;

  /*  only the ants move; skip the redraw when there are none  */
  if (select->num_segs_in && !select->hidden)
    selection_queue_draw (select);

  return G_SOURCE_CONTINUE;
}

/*********************************/
/*  Public function definitions  */
/*********************************/

Selection *
selection_create (GtkWidget *canvas,
		  gpointer   gdisp_ptr,
		  int        size,
		  int        width,
		  int        speed)
{
  Selection * new;

  new = g_new0 (Selection, 1);

  new->canvas         = canvas;
  new->gdisp          = gdisp_ptr;
  new->state          = SEL_INVISIBLE;
  new->recalc         = TRUE;
  new->speed          = MAX (speed, 10);
  new->hidden         = FALSE;

  return new;
}


void
selection_pause (Selection *select)
{
  if (select->state != SEL_INVISIBLE)
    selection_stop_timer (select);

  select->paused ++;
}


void
selection_resume (Selection *select)
{
  if (select->paused == 1)
    {
      select->state = SEL_INTRO;
      selection_stop_timer (select);
      select->timer = g_timeout_add (INITIAL_DELAY, selection_start_marching,
				     (gpointer) select);
    }

  select->paused--;
}


void
selection_start (Selection *select,
		 int        recalc)
{
  /*  A call to selection_start with recalc == TRUE means that
   *  we want to recalculate the selection boundary--usually
   *  after scaling or panning the display, or modifying the
   *  selection in some way.  If recalc == FALSE, the already
   *  calculated boundary is simply redrawn.
   */
  if (recalc)
    select->recalc = TRUE;

  /*  If this selection is paused, do not start it  */
  if (select->paused > 0)
    return;

  selection_stop_timer (select);

  select->state = SEL_INTRO;  /*  The state before the first draw  */
  select->timer = g_timeout_add (INITIAL_DELAY, selection_start_marching,
				 (gpointer) select);
}


void
selection_invis (Selection *select)
{
  if (select->state != SEL_INVISIBLE)
    {
      selection_stop_timer (select);
      select->state = SEL_INVISIBLE;
    }

  selection_queue_draw (select);
}


void
selection_layer_invis (Selection *select)
{
  if (select->state != SEL_INVISIBLE)
    {
      selection_stop_timer (select);
      select->state = SEL_INVISIBLE;
    }

  selection_queue_draw (select);
}


void
selection_hide (Selection *select,
		void      *gdisp_ptr)
{
  selection_invis (select);
  selection_layer_invis (select);

  /*  toggle the visibility  */
  select->hidden = select->hidden ? FALSE : TRUE;

  selection_start (select, TRUE);
}


void
selection_free (Selection *select)
{
  selection_stop_timer (select);
  selection_free_segs (select);
  g_free (select);
}
