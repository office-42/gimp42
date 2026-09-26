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
#include "gximage.h"

/*  The scratch buffer image_render.c renders a chunk of the display into:
 *  GXIMAGE_WIDTH x GXIMAGE_HEIGHT pixels of packed RGB.  gximage_put ()
 *  copies a chunk into a display's backing surface.
 */

static guchar *gximage_data = NULL;

void
gximage_init ()
{
  gximage_data = g_malloc (GXIMAGE_WIDTH * GXIMAGE_HEIGHT * 3);
}

void
gximage_free ()
{
  g_free (gximage_data);
  gximage_data = NULL;
}

guchar*
gximage_get_data ()
{
  return gximage_data;
}

int
gximage_get_bpp ()
{
  return 3;
}

int
gximage_get_bpl ()
{
  return 3 * GXIMAGE_WIDTH;
}

int
gximage_get_byte_order ()
{
  return GXIMAGE_MSB_FIRST;
}

void
gximage_put (cairo_surface_t *surface,
	     int              x,
	     int              y,
	     int              w,
	     int              h)
{
  guchar *dest;
  int     stride;
  int     width, height;
  int     row, col;

  if (!surface)
    return;

  width  = cairo_image_surface_get_width (surface);
  height = cairo_image_surface_get_height (surface);

  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > width)  w = width - x;
  if (y + h > height) h = height - y;
  if (w <= 0 || h <= 0)
    return;

  cairo_surface_flush (surface);
  dest   = cairo_image_surface_get_data (surface);
  stride = cairo_image_surface_get_stride (surface);

  for (row = 0; row < h; row++)
    {
      const guchar *s = gximage_data + row * GXIMAGE_WIDTH * 3;
      guint32      *d = (guint32 *) (dest + (y + row) * stride) + x;

      for (col = 0; col < w; col++, s += 3)
	d[col] = (0xffu << 24) | (s[0] << 16) | (s[1] << 8) | s[2];
    }

  cairo_surface_mark_dirty_rectangle (surface, x, y, w, h);
}
