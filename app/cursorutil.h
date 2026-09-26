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
#ifndef __CURSORUTIL_H__
#define __CURSORUTIL_H__

#include <gtk/gtk.h>

/*  The cursors the tools ask for, named after the X cursors they were;
 *  cursorutil.c maps them onto CSS cursor names.
 */
typedef enum
{
  GIMP_CURSOR_TOP_LEFT_ARROW,
  GIMP_CURSOR_BOTTOM_RIGHT_CORNER,
  GIMP_CURSOR_TOP_LEFT_CORNER,
  GIMP_CURSOR_CROSS,
  GIMP_CURSOR_DIAMOND_CROSS,
  GIMP_CURSOR_TCROSS,
  GIMP_CURSOR_EXCHANGE,
  GIMP_CURSOR_FLEUR,
  GIMP_CURSOR_HAND2,
  GIMP_CURSOR_ICON,
  GIMP_CURSOR_PENCIL,
  GIMP_CURSOR_SB_DOWN_ARROW,
  GIMP_CURSOR_SB_H_DOUBLE_ARROW,
  GIMP_CURSOR_SB_V_DOUBLE_ARROW,
  GIMP_CURSOR_SIZING,
  GIMP_CURSOR_XTERM,
  GIMP_CURSOR_X_CURSOR,
  GIMP_CURSOR_WATCH
} GimpCursorType;

void change_win_cursor (GtkWidget *widget, GimpCursorType cursortype);
void unset_win_cursor  (GtkWidget *widget);

#endif /*  __CURSORUTIL_H__  */
