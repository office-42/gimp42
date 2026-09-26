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
#ifndef __GIMP_RULER_H__
#define __GIMP_RULER_H__

#include <gtk/gtk.h>

/*  The rulers along the image window, in place of GTK 1's GtkHRuler and
 *  GtkVRuler: ticks and numbers for the range [lower, upper] shown across
 *  the ruler's length, and a marker at position.
 */

#define GIMP_TYPE_RULER (gimp_ruler_get_type ())
G_DECLARE_FINAL_TYPE (GimpRuler, gimp_ruler, GIMP, RULER, GtkWidget)

GtkWidget * gimp_ruler_new          (GtkOrientation orientation);
void        gimp_ruler_set_range    (GimpRuler     *ruler,
				     gdouble        lower,
				     gdouble        upper,
				     gdouble        max_size);
void        gimp_ruler_get_range    (GimpRuler     *ruler,
				     gdouble       *lower,
				     gdouble       *upper,
				     gdouble       *max_size);

/*  Moves the marker to where pixel offset lies along the ruler.  */
void        gimp_ruler_set_pointer  (GimpRuler     *ruler,
				     gdouble        offset);

#endif /* __GIMP_RULER_H__ */
