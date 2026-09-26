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
#ifndef __DISP_CALLBACKS_H__
#define __DISP_CALLBACKS_H__

#include <gtk/gtk.h>

/*  The image window's canvas: its events (a GtkEventControllerLegacy
 *  handler), its "resize" handler and its draw function; data is the
 *  GDisplay.
 */
gboolean gdisplay_canvas_events    (GtkEventControllerLegacy *, GdkEvent *, gpointer);
void     gdisplay_canvas_resize    (GtkDrawingArea *, int, int, gpointer);
void     gdisplay_canvas_draw_func (GtkDrawingArea *, cairo_t *, int, int, gpointer);

/*  The rulers' event handlers (GtkEventControllerLegacy).  */
gboolean gdisplay_hruler_events    (GtkEventControllerLegacy *, GdkEvent *, gpointer);
gboolean gdisplay_vruler_events    (GtkEventControllerLegacy *, GdkEvent *, gpointer);


#endif /*  __DISP_CALLBACKS_H__  */
