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
#include "errors.h"
#include "gdisplay.h"
#include "gimpruler.h"
#include "gdisplay_ops.h"
#include "gimprc.h"
#include "info_window.h"
#include "scale.h"
#include "tools.h"

void
bounds_checking (GDisplay *gdisp)
{
  int sx, sy;

  sx = SCALE(gdisp, gdisp->gimage->width);
  sy = SCALE(gdisp, gdisp->gimage->height);

  gdisp->offset_x = BOUNDS (gdisp->offset_x, 0,
			    LOWPASS (sx - gdisp->disp_width));

  gdisp->offset_y = BOUNDS (gdisp->offset_y, 0,
			    LOWPASS (sy - gdisp->disp_height));
}


void
resize_display (GDisplay *gdisp,
		int       resize_window,
		int       redisplay)
{
  /* freeze the active tool */
  active_tool_control (PAUSE, (void *) gdisp);

  if (resize_window)
    gdisplay_shrink_wrap (gdisp);

  bounds_checking (gdisp);
  setup_scale (gdisp);

  if (redisplay)
    {
      gdisplay_expose_full (gdisp);
      gdisplays_flush ();
    }

  /* re-enable the active tool */
  active_tool_control (RESUME, (void *) gdisp);
}


void
shrink_wrap_display (GDisplay *gdisp)
{
  /* freeze the active tool */
  active_tool_control (PAUSE, (void *) gdisp);

  gdisplay_shrink_wrap (gdisp);

  bounds_checking (gdisp);
  setup_scale (gdisp);

  gdisplay_expose_full (gdisp);
  gdisplays_flush ();

  /* re-enable the active tool */
  active_tool_control (RESUME, (void *) gdisp);
}


void
change_scale (GDisplay *gdisp,
	      int       dir)
{
  unsigned char scalesrc, scaledest;
  double offset_x, offset_y;
  long sx, sy;

  scalesrc = SCALESRC(gdisp);
  scaledest = SCALEDEST(gdisp);

  offset_x = gdisp->offset_x + (gdisp->disp_width/2.0);
  offset_y = gdisp->offset_y + (gdisp->disp_height/2.0);

  offset_x *= ((double) scalesrc / (double) scaledest);
  offset_y *= ((double) scalesrc / (double) scaledest);

  switch (dir)
    {
    case ZOOMIN :
      if (scalesrc > 1)
	scalesrc--;
      else
	if (scaledest < 0x10)
	  scaledest++;

      break;
    case ZOOMOUT :
      if (scaledest > 1)
	scaledest--;
      else
	if (scalesrc < 0x10)
	  scalesrc++;

      break;
    default :
      scalesrc = dir%100;
      if (scalesrc < 1)
	scalesrc = 1;
      else if (scalesrc > 0x10)
	scalesrc = 0x10;
      scaledest = dir/100;
      if (scaledest < 1)
	scaledest = 1;
      else if (scaledest > 0x10)
	scaledest = 0x10;
      break;
    }

  sx = (gdisp->gimage->width * scaledest) / scalesrc;
  sy = (gdisp->gimage->height * scaledest) / scalesrc;

  /*  The slider value is a short, so make sure we are within its
      range.  If we are trying to scale past it, then stop the scale  */
  if (sx < 0xffff && sy < 0xffff)
    {
      gdisp->scale = (scaledest << 8) + scalesrc;

      /*  set the offsets  */
      offset_x *= ((double) scaledest / (double) scalesrc);
      offset_y *= ((double) scaledest / (double) scalesrc);

      gdisp->offset_x = (int) (offset_x - (gdisp->disp_width / 2));
      gdisp->offset_y = (int) (offset_y - (gdisp->disp_height / 2));

      /*  resize the image  */
      resize_display (gdisp, allow_resize_windows, TRUE);

    }
}


void
setup_scale (GDisplay *gdisp)
{
  gdouble sx, sy;
  gdouble step;
  gdouble hlower, hupper, vlower, vupper;
  gdouble max_size;

  sx = SCALE(gdisp, gdisp->gimage->width);
  sy = SCALE(gdisp, gdisp->gimage->height);
  step = SCALE(gdisp, 1);

  gtk_adjustment_configure (gdisp->hsbdata,
			    gdisp->offset_x,
			    0, sx,
			    step,
			    (gdisp->disp_width / 2),
			    MIN (sx, gdisp->disp_width));

  gtk_adjustment_configure (gdisp->vsbdata,
			    gdisp->offset_y,
			    0, sy,
			    step,
			    (gdisp->disp_height / 2),
			    MIN (sy, gdisp->disp_height));

  max_size = MAXIMUM (gdisp->gimage->width, gdisp->gimage->height);

  hlower = 0;
  hupper = UNSCALE (gdisp, gdisp->disp_width);

  vlower = 0;
  vupper = UNSCALE (gdisp, gdisp->disp_height);

  if (sx < gdisp->disp_width)
    {
      gdisp->disp_xoffset = (gdisp->disp_width - sx) / 2;
      hlower -= UNSCALE (gdisp, (double) gdisp->disp_xoffset);
      hupper -= UNSCALE (gdisp, (double) gdisp->disp_xoffset);
    }
  else
    {
      gdisp->disp_xoffset = 0;
      hlower += UNSCALE (gdisp, (double) gdisp->offset_x);
      hupper += UNSCALE (gdisp, (double) gdisp->offset_x);
    }

  if (sy < gdisp->disp_height)
    {
      gdisp->disp_yoffset = (gdisp->disp_height - sy) / 2;
      vlower -= UNSCALE (gdisp, (double) gdisp->disp_yoffset);
      vupper -= UNSCALE (gdisp, (double) gdisp->disp_yoffset);
    }
  else
    {
      gdisp->disp_yoffset = 0;
      vlower += UNSCALE (gdisp, (double) gdisp->offset_y);
      vupper += UNSCALE (gdisp, (double) gdisp->offset_y);
    }

  gimp_ruler_set_range (GIMP_RULER (gdisp->hrule), hlower, hupper, max_size);
  gimp_ruler_set_range (GIMP_RULER (gdisp->vrule), vlower, vupper, max_size);
}
