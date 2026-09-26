/***************************************************************************/
/* GCK - The General Convenience Kit. Generally useful conveniece routines */
/* for GIMP plug-in writers and users of the GDK/GTK libraries.            */
/* Copyright (C) 1996 Tom Bech                                             */
/*                                                                         */
/* This program is free software; you can redistribute it and/or modify    */
/* it under the terms of the GNU General Public License as published by    */
/* the Free Software Foundation; either version 2 of the License, or       */
/* (at your option) any later version.                                     */
/*                                                                         */
/* This program is distributed in the hope that it will be useful,         */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of          */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           */
/* GNU General Public License for more details.                            */
/*                                                                         */
/* You should have received a copy of the GNU General Public License       */
/* along with this program; if not, write to the Free Software             */
/* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,   */
/* USA.                                                                    */
/***************************************************************************/

#ifndef __GCKUI_H__
#define __GCKUI_H__

#include "gck.h"

#ifdef __cplusplus
extern "C" {
#endif
/* gimp42: ported to GTK 4.  Widgets are visible when created, so    */
/* gck_auto_show () no longer does anything; hide a widget with      */
/* gtk_widget_set_visible () instead.  Pixmaps are XPM data, drawing */
/* areas take a cairo draw function (add event controllers to them   */
/* for input), and the callbacks are GCallbacks connected swapped,   */
/* as before: they get the widget (the scale, entry, button or       */
/* option menu) as their first argument.  The GtkMenu based          */
/* gck_menu_bar_new () and gck_menu_new () are gone with GtkMenu.    */

void                  gck_cursor_set                 (GtkWidget *widget,
                                                      const char *cursor_name);

void                  gck_auto_show                  (gint flag);

GckApplicationWindow *gck_application_window_new     (char *name);
void                  gck_application_window_destroy (GckApplicationWindow *appwin);

GckDialogWindow      *gck_dialog_window_new          (char *name,
                                                      GckPosition ActionPos,
                                                      GCallback ok_pressed_func,
                                                      GCallback cancel_pressed_func,
                                                      GCallback help_pressed_func);
void                  gck_dialog_window_destroy      (GckDialogWindow *dialog);

GtkWidget            *gck_vseparator_new             (GtkWidget *container);
GtkWidget            *gck_hseparator_new             (GtkWidget *container);

GtkWidget            *gck_frame_new                  (char *name,GtkWidget *container,
                                                      GckShadowType shadowtype,
                                                      gint expand,gint fill,gint padding,
                                                      gint borderwidth);

GtkWidget            *gck_label_new                  (char *name,GtkWidget *container);
GtkWidget            *gck_label_aligned_new          (char *name,GtkWidget *container,
                                                      gdouble xalign,gdouble yalign);

GtkWidget            *gck_drawing_area_new           (GtkWidget *container,
                                                      gint width,gint height,
                                                      GtkDrawingAreaDrawFunc draw_func,
                                                      gpointer data);

GtkWidget            *gck_hscale_new                 (char *name,GtkWidget *container,
                                                      GckScaleValues *svals,
                                                      GCallback value_changed_func);
GtkWidget            *gck_vscale_new                 (char *name,GtkWidget *container,
                                                      GckScaleValues *svals,
                                                      GCallback value_changed_func);

GtkWidget            *gck_entryfield_new             (char *name,GtkWidget *container,
                                                      double initial_value,
                                                      GCallback valuechangedfunc);
GtkWidget            *gck_entryfield_text_new        (char *name,GtkWidget *container,
                                                      char *initial_text,
                                                      GCallback textchangedfunc);

GtkWidget            *gck_pushbutton_new             (char *name,GtkWidget *container,
                                                      gint expand,gint fill,gint padding,
                                                      GCallback button_clicked_func);
GtkWidget            *gck_pushbutton_pixmap_new      (char *name,
                                                      char **xpm_data,
                                                      GtkWidget *container,
                                                      gint expand,gint fill,gint padding,
                                                      GCallback button_clicked_func);
GtkWidget            *gck_togglebutton_pixmap_new    (char *name,
                                                      char **xpm_data,
                                                      GtkWidget *container,
                                                      gint expand,gint fill,gint padding,
                                                      GCallback button_toggled_func);

GtkWidget            *gck_checkbutton_new            (char *name,GtkWidget *container,
                                                      gint value,
                                                      GCallback status_changed_func);
GtkWidget            *gck_radiobutton_new            (char *name,GtkWidget *container,
                                                      GtkWidget *previous,
                                                      GCallback status_changed_func);
GtkWidget            *gck_radiobutton_pixmap_new     (char *name,
                                                      char **xpm_data,
                                                      GtkWidget *container,
                                                      GtkWidget *previous,
                                                      GCallback status_changed_func);

GtkWidget            *gck_pixmap_new                 (char **xpm_data,
                                                      GtkWidget *container);

GtkWidget            *gck_vbox_new                   (GtkWidget *Container,
                                                      gint homogenous,gint expand,gint fill,
                                                      gint spacing,gint padding,
                                                      gint borderwidth);
GtkWidget            *gck_hbox_new                   (GtkWidget *container,
                                                      gint homogenous,gint expand,gint fill,
                                                      gint spacing,gint padding,
                                                      gint borderwidth);

/* The callback gets the option menu; the index of the chosen item is */
/* g_object_get_data (option_menu, "_GckOptionMenuItemID").           */

GtkWidget            *gck_option_menu_new            (char *name,GtkWidget *container,
                                                      gint expand,gint fill,
                                                      gint padding,
                                                      char *item_labels[],
                                                      GCallback item_selected_func,
                                                      gpointer data);
void                  gck_option_menu_set_history    (GtkWidget *option_menu,
                                                      gint index);

GtkWidget            *gck_image_menu_new             (char *name,GtkWidget *container,
                                                      gint expand,gint fill,
                                                      gint padding,
                                                      gint constrain,
                                                      GCallback item_selected_func);

#ifdef __cplusplus
}
#endif

#endif
