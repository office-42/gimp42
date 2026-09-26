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
#include "gdisplay_ops.h"
#include "scale.h"
#include "scroll.h"
#include "cursorutil.h"
#include "tools.h"


/*  Locally defined functions  */
static int scroll_display (GDisplay *, int, int);

/*  STATIC variables  */
/*  These are the values of the initial pointer grab   */
static int startx, starty;

gint
scrollbar_vert_update (GtkAdjustment *adjustment,
		       gpointer       data)
{
  GDisplay *gdisp;

  gdisp = (GDisplay *) data;

  scroll_display (gdisp, 0, (gtk_adjustment_get_value (adjustment) - gdisp->offset_y));

  return FALSE;
}


gint
scrollbar_horz_update (GtkAdjustment *adjustment,
		       gpointer       data)
{
  GDisplay *gdisp;

  gdisp = (GDisplay *) data;

  scroll_display (gdisp, (gtk_adjustment_get_value (adjustment) - gdisp->offset_x), 0);

  return FALSE;
}

void
start_grab_and_scroll (GDisplay        *gdisp,
		       GimpButtonEvent *bevent)
{
  startx = bevent->x + gdisp->offset_x;
  starty = bevent->y + gdisp->offset_y;

  change_win_cursor (gdisp->canvas, GIMP_CURSOR_FLEUR);
}


void
end_grab_and_scroll (GDisplay        *gdisp,
		     GimpButtonEvent *bevent)
{
  change_win_cursor (gdisp->canvas, gdisp->current_cursor);
}


void
grab_and_scroll (GDisplay        *gdisp,
		 GimpMotionEvent *mevent)
{
  scroll_display (gdisp, (startx - mevent->x - gdisp->offset_x),
		  (starty - mevent->y - gdisp->offset_y));
}


void
scroll_to_pointer_position (GDisplay        *gdisp,
			    GimpMotionEvent *mevent)
{
  int off_x, off_y;

  off_x = off_y = 0;

  /*  The cases for scrolling  */
  if (mevent->x < 0)
    off_x = mevent->x;
  else if (mevent->x > gdisp->disp_width)
    off_x = mevent->x - gdisp->disp_width;
  if (mevent->y < 0)
    off_y = mevent->y;
  else if (mevent->y > gdisp->disp_height)
    off_y = mevent->y - gdisp->disp_height;

  scroll_display (gdisp, off_x, off_y);
}


/*  Moves what is already rendered in the backing surface along with the
 *  scroll, so that only the strips that come into view need rendering.
 */
static void
scroll_backing (GDisplay *gdisp,
		int       x_offset,
		int       y_offset)
{
  cairo_surface_t *copy;
  cairo_t *cr;

  if (!gdisp->backing)
    return;

  copy = cairo_surface_create_similar_image (gdisp->backing,
					     CAIRO_FORMAT_RGB24,
					     cairo_image_surface_get_width (gdisp->backing),
					     cairo_image_surface_get_height (gdisp->backing));
  cr = cairo_create (copy);
  cairo_set_source_surface (cr, gdisp->backing, -x_offset, -y_offset);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  cairo_destroy (cr);

  cairo_surface_destroy (gdisp->backing);
  gdisp->backing = copy;
}


static int
scroll_display (GDisplay *gdisp,
		int       x_offset,
		int       y_offset)
{
  int old_x, old_y;
  int src_x, src_y;

  old_x = gdisp->offset_x;
  old_y = gdisp->offset_y;

  gdisp->offset_x += x_offset;
  gdisp->offset_y += y_offset;

  bounds_checking (gdisp);

  /*  the actual changes in offset  */
  x_offset = (gdisp->offset_x - old_x);
  y_offset = (gdisp->offset_y - old_y);

  if (x_offset || y_offset)
    {
      setup_scale (gdisp);

      /*  reset the old values so that the tool can accurately redraw  */
      gdisp->offset_x = old_x;
      gdisp->offset_y = old_y;

      /*  stop the currently active tool  */
      active_tool_control (PAUSE, (void *) gdisp);

      /*  set the offsets back to the new values  */
      gdisp->offset_x += x_offset;
      gdisp->offset_y += y_offset;

      scroll_backing (gdisp, x_offset, y_offset);

      /*  resume the currently active tool  */
      active_tool_control (RESUME, (void *) gdisp);

      /*  scale the image into the exposed regions  */
      if (x_offset)
	{
	  src_x = (x_offset < 0) ? 0 : gdisp->disp_width - x_offset;
	  src_y = 0;
	  gdisplay_expose_area (gdisp,
				src_x, src_y,
				abs (x_offset), gdisp->disp_height);
	}
      if (y_offset)
	{
	  src_x = 0;
	  src_y = (y_offset < 0) ? 0 : gdisp->disp_height - y_offset;
	  gdisplay_expose_area (gdisp,
				src_x, src_y,
				gdisp->disp_width, abs (y_offset));
	}

      gdisplays_flush ();

      return 1;
    }

  return 0;
}
