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
#ifndef  __LAYERS_DIALOGP_H__
#define  __LAYERS_DIALOGP_H__

#include "buildmenu.h"

GtkWidget *  layers_dialog_create    (void);
GtkWidget *  channels_dialog_create  (void);

/*  Fills option_menu (a gimp_option_menu, created when NULL) with one
 *  item per image and returns it.  Each item calls callback with the
 *  option menu and GINT_TO_POINTER (image ID).
 */
GtkWidget *  create_image_menu       (GtkWidget *option_menu,
				      int *, int *, MenuItemCallback);

void         layers_dialog_update    (int);
void         channels_dialog_update  (int);

void         layers_dialog_clear     (void);
void         channels_dialog_clear   (void);

void         layers_dialog_free      (void);
void         channels_dialog_free    (void);

/*  Draws the floating selection icon into a w x h area of cr.  */
void         render_fs_preview       (GtkWidget *, cairo_t *, int, int);

/*  Renders preview_buf into the GimpPreview preview_widget, which is
 *  used as a scratch buffer; render_preview_surface () then copies its
 *  top-left width x height pixels into a new RGB24 cairo surface.
 */
void         render_preview          (TempBuf *, GtkWidget *, int, int, int);
cairo_surface_t * render_preview_surface (GtkWidget *, int, int);

/*  The right-click menus of the layers and channels lists: a popover
 *  of buttons, one per item (items[i].widget is set to its button),
 *  parented to parent.  The items' accelerators work in the window
 *  holding parent while parent is mapped.
 */
GtkWidget *  lc_ops_menu_new         (MenuItem *, GtkWidget *parent);
void         lc_ops_menu_popup       (GtkWidget *menu, GtkWidget *widget,
				      double x, double y);

/*  Draws an XBM bitmap at x, y in widget's foreground color.  */
void         lc_draw_bitmap          (GtkWidget *widget, cairo_t *cr,
				      const unsigned char *bits,
				      int width, int height, int x, int y);

/*  Main dialog widget  */
extern GtkWidget *lc_shell;

#endif  /*  __LAYERS_DIALOGP_H__  */
