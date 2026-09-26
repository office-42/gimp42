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
#include "appenv.h"
#include "cursorutil.h"

static const char *cursor_names[] =
{
  "default",       /*  TOP_LEFT_ARROW       */
  "se-resize",     /*  BOTTOM_RIGHT_CORNER  */
  "nw-resize",     /*  TOP_LEFT_CORNER      */
  "crosshair",     /*  CROSS                */
  "crosshair",     /*  DIAMOND_CROSS        */
  "crosshair",     /*  TCROSS               */
  "alias",         /*  EXCHANGE             */
  "move",          /*  FLEUR                */
  "pointer",       /*  HAND2                */
  "copy",          /*  ICON                 */
  "crosshair",     /*  PENCIL               */
  "s-resize",      /*  SB_DOWN_ARROW        */
  "ew-resize",     /*  SB_H_DOUBLE_ARROW    */
  "ns-resize",     /*  SB_V_DOUBLE_ARROW    */
  "nwse-resize",   /*  SIZING               */
  "text",          /*  XTERM                */
  "not-allowed",   /*  X_CURSOR             */
  "wait",          /*  WATCH                */
};

void
change_win_cursor (GtkWidget      *widget,
		   GimpCursorType  cursortype)
{
  if (!widget)
    return;

  if ((guint) cursortype >= G_N_ELEMENTS (cursor_names))
    cursortype = GIMP_CURSOR_TOP_LEFT_ARROW;

  gtk_widget_set_cursor_from_name (widget, cursor_names[cursortype]);
}

void
unset_win_cursor (GtkWidget *widget)
{
  if (widget)
    gtk_widget_set_cursor (widget, NULL);
}
