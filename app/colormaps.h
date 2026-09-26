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
#ifndef __COLORMAPS_H__
#define __COLORMAPS_H__

#include "gimage.h"               /* For the image types  */

/*  Under X the GIMP allocated a visual, a colormap and pixel values for
 *  everything it drew.  With cairo there is nothing to allocate: colors
 *  are RGB.  What is left is the one call app_procs makes at startup.
 */

void   get_standard_colormaps (void);

#endif  /*  __COLORMAPS_H__  */
