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

/***********************************************************/
/* This file contains some convenience routines for making */
/* plug-in UIs. There's functions for creating dialogs,    */
/* option menus, entry fields, scales and checkbuttons.    */
/***********************************************************/


/***********************************************************/
/* gimp42: ported to GTK 4, on top of the GTK 1 style      */
/* packing helpers in libgimp/gimpwidgets.h.               */
/***********************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include <libgimp/gimpwidgets.h>
#include <gck/gckui.h>

gint _GckAutoShowFlag = TRUE;

/*****************************************************/
/* Put widget into container: boxes pack it with the */
/* given expand/fill/padding, anything else adds it. */
/*****************************************************/

static void _gck_pack(GtkWidget *container, GtkWidget *widget,
                      gint expand, gint fill, gint padding)
{
  if (container == NULL)
    return;

  if (GTK_IS_BOX(container))
    gimp_box_pack_start(container, widget, expand, fill, padding);
  else
    gimp_container_add(container, widget);
}

/***********************************************/
/* A picture of XPM data (was a GdkPixmap+mask) */
/***********************************************/

static GtkWidget *_gck_picture_new(char **xpm_data)
{
  GdkPixbuf *pixbuf;
  GdkTexture *texture;
  GtkWidget *picture;

  pixbuf = gdk_pixbuf_new_from_xpm_data((const char **) xpm_data);
  texture = gdk_texture_new_for_pixbuf(pixbuf);
  picture = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
  gtk_picture_set_can_shrink(GTK_PICTURE(picture), FALSE);

  g_object_unref(texture);
  g_object_unref(pixbuf);

  return (picture);
}

/*************************/
/* Set cursor for widget */
/*************************/

void gck_cursor_set(GtkWidget *widget, const char *cursor_name)
{
  g_function_enter("gck_cursor_set");
  g_assert(widget!=NULL);

  gtk_widget_set_cursor_from_name(widget, cursor_name);

  g_function_leave("gck_cursor_set");
}

/********************************************************/
/* Toggle auto show flag, it is set to TRUE as default. */
/* GTK 4 widgets are visible from the start, so this is */
/* kept only for compatibility.                         */
/********************************************************/

void gck_auto_show(gint flag)
{
  g_function_enter("gck_auto_show");

  if (flag == TRUE || flag == FALSE)
    _GckAutoShowFlag = flag;

  g_function_leave("gck_auto_show");
}

/****************************************/
/* Create application window and visual */
/****************************************/

GckApplicationWindow *gck_application_window_new(char *name)
{
  GckApplicationWindow *appwin;

  g_function_enter("gck_application_window_new");

  appwin = g_new0(GckApplicationWindow, 1);
  appwin->visinfo = gck_visualinfo_new();

  /* Create window widget */
  /* ==================== */

  appwin->widget = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(appwin->widget),name);

  g_function_leave("gck_application_window_new");
  return (appwin);
}

/******************************************************************/
/* Free memory associated with the GckApplicationWindow structure */
/******************************************************************/

void gck_application_window_destroy(GckApplicationWindow *appwin)
{
  g_function_enter("gck_application_window_destroy");
  g_assert(appwin!=NULL);

  gck_visualinfo_destroy(appwin->visinfo);
  gtk_window_destroy(GTK_WINDOW(appwin->widget));
  g_free(appwin);

  g_function_leave("gck_application_window_destroy");
}

/**************************/
/* Create dialog template */
/**************************/

GckDialogWindow *gck_dialog_window_new(char *name,GckPosition ActionPos,
                                       GCallback ok_pressed_func,
                                       GCallback cancel_pressed_func,
                                       GCallback help_pressed_func)
{
  GckDialogWindow *dialog;
  GtkWidget *mainbox, *frame;

  g_function_enter("gck_dialog_window_new");

  /* Create dialog window */
  /* ==================== */

  dialog = g_new0(GckDialogWindow, 1);

  dialog->widget = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog->widget),name);
  gtk_window_set_resizable(GTK_WINDOW(dialog->widget),TRUE);

  dialog->okbutton=NULL;
  dialog->cancelbutton=NULL;
  dialog->helpbutton=NULL;

  if (ActionPos==GCK_TOP || ActionPos==GCK_BOTTOM)
    mainbox= gck_vbox_new(dialog->widget,FALSE,FALSE,FALSE,0,0,2);
  else
    mainbox= gck_hbox_new(dialog->widget,FALSE,FALSE,FALSE,0,0,2);

  /* Create WorkArea (in a frame) and ActionArea */
  /* =========================================== */

  if (ActionPos==GCK_TOP || ActionPos==GCK_BOTTOM)
    {
      if (ActionPos==GCK_TOP)
        {
          dialog->actionbox=gck_hbox_new(mainbox,TRUE,TRUE,TRUE,5,0,5);
          frame = gck_frame_new(NULL,mainbox,GCK_SHADOW_ETCHED_IN,TRUE,TRUE,0,5);
          dialog->workbox=gck_vbox_new(frame,FALSE,TRUE,TRUE,5,0,5);
        }
      else
        {
          frame = gck_frame_new(NULL,mainbox,GCK_SHADOW_ETCHED_IN,TRUE,TRUE,0,5);
          dialog->workbox=gck_vbox_new(frame,FALSE,TRUE,TRUE,5,0,5);
          dialog->actionbox=gck_hbox_new(mainbox,TRUE,TRUE,TRUE,5,0,5);
        }
    }
  else
    {
      if (ActionPos==GCK_LEFT)
        {
          dialog->actionbox=gck_vbox_new(mainbox,FALSE,FALSE,FALSE,5,0,5);
          frame = gck_frame_new(NULL,mainbox,GCK_SHADOW_ETCHED_IN,TRUE,TRUE,0,5);
          dialog->workbox=gck_vbox_new(frame,FALSE,TRUE,TRUE,5,0,5);
        }
      else
        {
          frame = gck_frame_new(NULL,mainbox,GCK_SHADOW_ETCHED_IN,TRUE,TRUE,0,5);
          dialog->workbox=gck_vbox_new(frame,FALSE,TRUE,TRUE,5,0,5);
          dialog->actionbox=gck_vbox_new(mainbox,FALSE,FALSE,FALSE,5,0,5);
        }
    }

  if (ok_pressed_func!=NULL)
    {
      dialog->okbutton = gck_pushbutton_new("Ok",dialog->actionbox,FALSE,TRUE,0,
                                            ok_pressed_func);
      g_object_set_data(G_OBJECT(dialog->okbutton),"GckDialogWindow",(gpointer)dialog);
    }

  if (cancel_pressed_func!=NULL)
    {
      dialog->cancelbutton = gck_pushbutton_new("Cancel",dialog->actionbox,FALSE,TRUE,0,
                                                cancel_pressed_func);
      g_object_set_data(G_OBJECT(dialog->cancelbutton),"GckDialogWindow",(gpointer)dialog);
    }

  if (help_pressed_func!=NULL)
    {
      dialog->helpbutton = gck_pushbutton_new("Help",dialog->actionbox,FALSE,TRUE,0,
                                              help_pressed_func);
      g_object_set_data(G_OBJECT(dialog->helpbutton),"GckDialogWindow",(gpointer)dialog);
    }

  g_function_leave("gck_dialog_window_new");
  return (dialog);
}

/*************************************************************/
/* Free memory associated with the GckDialogWindow structure */
/*************************************************************/

void gck_dialog_window_destroy(GckDialogWindow * dialog)
{
  g_function_enter("gck_dialog_window_destroy");
  g_assert(dialog!=NULL);

  gtk_window_destroy(GTK_WINDOW(dialog->widget));
  g_free(dialog);

  g_function_leave("gck_dialog_window_destroy");
}

/*****************************/
/* Create vertical separator */
/*****************************/

GtkWidget *gck_vseparator_new(GtkWidget *container)
{
  GtkWidget *separator;

  g_function_enter("gck_vseparator_new");

  separator=gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  if (container!=NULL)
    gimp_container_add(container,separator);

  g_function_leave("gck_vseparator_new");
  return(separator);
}

/*******************************/
/* Create horizontal separator */
/*******************************/

GtkWidget *gck_hseparator_new(GtkWidget *container)
{
  GtkWidget *separator;

  g_function_enter("gck_hseparator_new");

  separator=gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  if (container!=NULL)
    gimp_container_add(container,separator);

  g_function_leave("gck_hseparator_new");
  return(separator);
}

/*************************/
/* Create frame template */
/*************************/

GtkWidget *gck_frame_new(char *name, GtkWidget * container, GckShadowType shadowtype,
                         gint expand, gint fill, gint padding, gint borderwidth)
{
  static GtkCssProvider *provider = NULL;
  GtkWidget *frame;

  g_function_enter("gck_frame_new");

  frame = gtk_frame_new(name);

  /* A frame without a shadow draws no border */
  /* ======================================== */

  if (shadowtype == GCK_SHADOW_NONE)
    {
      if (provider == NULL)
        {
          provider = gtk_css_provider_new();
          gtk_css_provider_load_from_string(provider,
            "frame.gck-shadow-none > border { border-style: none; box-shadow: none; }");
          gtk_style_context_add_provider_for_display(gdk_display_get_default(),
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }

      gtk_widget_add_css_class(frame, "gck-shadow-none");
    }

  gimp_container_set_border_width(frame, borderwidth);

  _gck_pack(container, frame, expand, fill, padding);

  g_function_leave("gck_frame_new");
  return (frame);
}

/*************************/
/* Create label template */
/*************************/

GtkWidget *gck_label_new(char *name, GtkWidget *container)
{
  GtkWidget *label;

  g_function_enter("gck_label_new");

  if (name == NULL)
    label = gtk_label_new(" ");
  else
    label = gtk_label_new(name);

  _gck_pack(container, label, FALSE, FALSE, 0);

  g_function_leave("gck_label_new");
  return (label);
}

GtkWidget *gck_label_aligned_new(char *name, GtkWidget * container,
                                 gdouble xalign, gdouble yalign)
{
  GtkWidget *label;

  g_function_enter("gck_label_aligned_new");

  if (name == NULL)
    label = gtk_label_new(" ");
  else
    label = gtk_label_new(name);

  gtk_label_set_xalign(GTK_LABEL(label),xalign);
  gtk_label_set_yalign(GTK_LABEL(label),yalign);

  _gck_pack(container, label, FALSE, FALSE, 0);

  g_function_leave("gck_label_aligned_new");
  return (label);
}

/*****************************************************/
/* Create drawing area.  Input is up to the caller:  */
/* add event controllers to the returned widget.     */
/*****************************************************/

GtkWidget *gck_drawing_area_new(GtkWidget *container,gint width,gint height,
                                GtkDrawingAreaDrawFunc draw_func,gpointer data)
{
  GtkWidget *drawingarea;

  g_function_enter("gck_drawing_area_new");

  drawingarea = gtk_drawing_area_new();
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(drawingarea),width);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(drawingarea),height);

  if (draw_func!=NULL)
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawingarea),
                                   draw_func,data,NULL);

  _gck_pack(container, drawingarea, FALSE, FALSE, 0);

  g_function_leave("gck_drawing_area_new");
  return(drawingarea);
}


GtkWidget *gck_pixmap_new (char **xpm_data,
                           GtkWidget *container)
{
  GtkWidget *pixmap;

  g_function_enter("gck_pixmap_new");
  g_assert(xpm_data != NULL);

  pixmap = _gck_picture_new(xpm_data);
  gtk_widget_set_halign(pixmap, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(pixmap, GTK_ALIGN_CENTER);
  gimp_container_set_border_width(pixmap, 2);

  if (container!=NULL)
    gimp_container_add(container,pixmap);

  g_function_leave("gck_pixmap_new");
  return(pixmap);
}

/**************************/
/* Create scale templates */
/**************************/

static GtkWidget *_gck_scale_new(char *name, GtkWidget *container,
                                 GckScaleValues *svals,
                                 GCallback value_changed_func,
                                 GtkOrientation orientation)
{
  GtkWidget *label, *scale;
  GtkAdjustment *adjustment;

  g_assert(svals!=NULL);

  if (name != NULL && container != NULL)
    {
      label = gtk_label_new(name);
      gimp_container_add(container, label);
    }

  adjustment = gtk_adjustment_new(svals->value, svals->lower,
	                          svals->upper, svals->step_inc,
                                  svals->page_inc, svals->page_size);

  scale = gtk_scale_new(orientation, adjustment);
  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    gtk_widget_set_size_request(scale, svals->size, -1);
  else
    gtk_widget_set_size_request(scale, -1, svals->size);
  gtk_scale_set_digits(GTK_SCALE(scale), 1);
  gtk_scale_set_draw_value(GTK_SCALE(scale), svals->draw_value_flag);
  gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_TOP);

  if (value_changed_func!=NULL)
    g_signal_connect_swapped(adjustment,"value-changed",
      value_changed_func,(gpointer)scale);

  _gck_pack(container, scale, FALSE, FALSE, 0);

  return (scale);
}

GtkWidget *gck_hscale_new(char *name, GtkWidget *container,
	                  GckScaleValues *svals,
                          GCallback value_changed_func)
{
  GtkWidget *scale;

  g_function_enter("gck_hscale_new");

  scale = _gck_scale_new(name, container, svals, value_changed_func,
                         GTK_ORIENTATION_HORIZONTAL);

  g_function_leave("gck_hscale_new");
  return (scale);
}

GtkWidget *gck_vscale_new(char *name, GtkWidget * container,
	                  GckScaleValues *svals,
                          GCallback value_changed_func)
{
  GtkWidget *scale;

  g_function_enter("gck_vscale_new");

  scale = _gck_scale_new(name, container, svals, value_changed_func,
                         GTK_ORIENTATION_VERTICAL);

  g_function_leave("gck_vscale_new");
  return (scale);
}

/**************************************/
/* Create (text) entry field template */
/**************************************/

GtkWidget *gck_entryfield_text_new (char *name, GtkWidget *container,
                                    char *initial_text,
                                    GCallback text_changed_func)
{
  GtkWidget *entry, *label=NULL, *hbox=NULL;

  g_function_enter("gck_entryfield_text_new");

  if (name!=NULL)
    {
      hbox = gimp_hbox_new(FALSE, 0);
      _gck_pack(container, hbox, FALSE, FALSE, 0);
      gimp_container_set_border_width(hbox, 2);

      label = gtk_label_new(name);
      gimp_container_add(hbox, label);
   }

  entry = gtk_entry_new();

  if (initial_text!=NULL)
    gtk_editable_set_text(GTK_EDITABLE(entry), initial_text);

  if (hbox!=NULL)
    gimp_container_add(hbox, entry);
  else if (container!=NULL)
    gimp_container_add(container, entry);

  if (text_changed_func!=NULL)
    g_signal_connect_swapped(entry,"changed",text_changed_func,(gpointer)entry);

  g_object_set_data(G_OBJECT(entry),"EntryLabel",(gpointer)label);

  g_function_leave("gck_entryfield_text_new");

  return (entry);
}

/****************************************/
/* Create (double) entry field template */
/****************************************/

GtkWidget *gck_entryfield_new(char *name, GtkWidget *container,
                              double initial_value,
                              GCallback value_changed_func)
{
  char buffer[64];

  g_snprintf(buffer, sizeof(buffer), "%f", initial_value);

  return (gck_entryfield_text_new(name,container,buffer,value_changed_func));
}

/*******************************/
/* Create push button template */
/*******************************/

GtkWidget *gck_pushbutton_new(char *name, GtkWidget *container,
                              gint expand, gint fill, gint padding,
                              GCallback button_clicked_func)
{
  GtkWidget *button;

  g_function_enter("gck_pushbutton_new");

  if (name != NULL)
    button = gtk_button_new_with_label(name);
  else
    button = gtk_button_new();

  _gck_pack(container, button, expand, fill, padding);

  if (button_clicked_func != NULL)
    g_signal_connect_swapped(button,"clicked",button_clicked_func,(gpointer)button);

  g_function_leave("gck_pushbutton_new");
  return (button);
}

/**********************************************************/
/* Fill a button with a picture and/or a label; a picture */
/* and a label go side by side in a box.                  */
/**********************************************************/

static void _gck_button_fill(GtkWidget *button, char *name, char **xpm_data)
{
  GtkWidget *cont;

  if (name!=NULL && xpm_data!=NULL)
    cont=gck_hbox_new(button, FALSE,FALSE,TRUE,0,0,1);
  else
    cont=button;

  if (xpm_data!=NULL)
    gck_pixmap_new(xpm_data,cont);

  if (name != NULL)
    gck_label_new(name,cont);
}

GtkWidget *gck_pushbutton_pixmap_new(char *name,
                                     char **xpm_data,
                                     GtkWidget *container,
                                     gint expand,gint fill,gint padding,
                                     GCallback button_clicked_func)
{
  GtkWidget *button;

  g_function_enter("gck_pushbutton_pixmap_new");

  button = gtk_button_new();

  _gck_pack(container, button, expand, fill, padding);

  if (button_clicked_func != NULL)
    g_signal_connect_swapped(button,"clicked",button_clicked_func,(gpointer)button);

  _gck_button_fill(button, name, xpm_data);

  g_function_leave("gck_pushbutton_pixmap_new");
  return (button);
}

GtkWidget *gck_togglebutton_pixmap_new(char *name,
                                       char **xpm_data,
                                       GtkWidget *container,
                                       gint expand,gint fill,gint padding,
                                       GCallback button_toggled_func)
{
  GtkWidget *button;

  g_function_enter("gck_togglebutton_pixmap_new");

  button = gtk_toggle_button_new();

  _gck_pack(container, button, expand, fill, padding);

  if (button_toggled_func != NULL)
    g_signal_connect_swapped(button,"toggled",button_toggled_func,(gpointer)button);

  _gck_button_fill(button, name, xpm_data);

  g_function_leave("gck_togglebutton_pixmap_new");
  return (button);
}

/********************************/
/* Create check button template */
/********************************/

GtkWidget *gck_checkbutton_new(char *name, GtkWidget *container,
                               gint value,
                               GCallback status_changed_func)
{
  GtkWidget *button;

  g_function_enter("gck_checkbutton_new");

  if (name == NULL)
    button = gtk_check_button_new();
  else
    button = gtk_check_button_new_with_label(name);

  _gck_pack(container, button, TRUE, TRUE, 0);

  gtk_check_button_set_active(GTK_CHECK_BUTTON(button),value);

  if (status_changed_func!=NULL)
    g_signal_connect_swapped(button,"toggled",status_changed_func,(gpointer)button);

  g_function_leave("gck_checkbutton_new");
  return (button);
}

/********************************/
/* Create radio-button template */
/********************************/

GtkWidget *gck_radiobutton_new(char *name,
                               GtkWidget *container,
                               GtkWidget *previous,
                               GCallback status_changed_func)
{
  GtkWidget *button;

  g_function_enter("gck_radiobutton_new");

  button = gimp_radio_button_new(previous, name);

  _gck_pack(container, button, TRUE, TRUE, 0);

  if (status_changed_func != NULL)
    g_signal_connect_swapped(button,"toggled",status_changed_func,(gpointer)button);

  g_function_leave("gck_radiobutton_new");
  return (button);
}

/**************************************************************/
/* Radio buttons drawn as buttons (GTK 1's draw_indicator off) */
/* are grouped toggle buttons in GTK 4.                        */
/**************************************************************/

GtkWidget *gck_radiobutton_pixmap_new(char *name,
                                      char **xpm_data,
                                      GtkWidget *container,
                                      GtkWidget *previous,
                                      GCallback status_changed_func)
{
  GtkWidget *button;

  g_function_enter("gck_radiobutton_pixmap_new");

  button = gtk_toggle_button_new();

  if (previous != NULL)
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(button),
                                GTK_TOGGLE_BUTTON(previous));

  _gck_pack(container, button, TRUE, TRUE, 0);

  _gck_button_fill(button, name, xpm_data);

  if (status_changed_func != NULL)
    g_signal_connect_swapped(button,"toggled",status_changed_func,(gpointer)button);

  g_function_leave("gck_radiobutton_pixmap_new");
  return (button);
}

/********************************/
/* Create vertical box template */
/********************************/

GtkWidget *gck_vbox_new(GtkWidget * container,
                        gint homogenous, gint expand, gint fill,
	                gint spacing, gint padding, gint borderwidth)
{
  GtkWidget *vbox;

  g_function_enter("gck_vbox_new");

  vbox = gimp_vbox_new(homogenous, spacing);

  _gck_pack(container, vbox, expand, fill, padding);

  gimp_container_set_border_width(vbox, borderwidth);

  g_function_leave("gck_vbox_new");
  return (vbox);
}

/**********************************/
/* Create horizontal box template */
/**********************************/

GtkWidget *gck_hbox_new(GtkWidget * container,
                        gint homogenous, gint expand, gint fill,
	                gint spacing, gint padding, gint borderwidth)
{
  GtkWidget *hbox;

  g_function_enter("gck_hbox_new");

  hbox = gimp_hbox_new(homogenous, spacing);

  _gck_pack(container, hbox, expand, fill, padding);

  gimp_container_set_border_width(hbox, borderwidth);

  g_function_leave("gck_hbox_new");
  return (hbox);
}

/**********************/
/* Create option menu */
/**********************/

typedef struct
{
  GCallback item_selected_func;
  gpointer data;
} _GckOptionMenu;

static void _gck_option_menu_selected(GtkWidget *option_menu, gpointer item_id)
{
  _GckOptionMenu *menu;

  menu = g_object_get_data(G_OBJECT(option_menu), "_GckOptionMenu");

  g_object_set_data(G_OBJECT(option_menu), "_GckOptionMenuItemID", item_id);

  if (menu != NULL && menu->item_selected_func != NULL)
    ((void (*)(GtkWidget *, gpointer)) menu->item_selected_func)(option_menu,
                                                                 menu->data);
}

GtkWidget *gck_option_menu_new(char *name, GtkWidget *container,
                               gint expand, gint fill, gint padding,
                               char *item_labels[],
                               GCallback item_selected_func,
                               gpointer data)
{
  GtkWidget *optionmenu,*cont;
  _GckOptionMenu *menu;
  gint i = 0;

  g_function_enter("gck_option_menu_new");

  optionmenu = gimp_option_menu_new();

  menu = g_new0(_GckOptionMenu, 1);
  menu->item_selected_func = item_selected_func;
  menu->data = data;
  g_object_set_data_full(G_OBJECT(optionmenu), "_GckOptionMenu", menu, g_free);
  g_object_set_data(G_OBJECT(optionmenu), "_GckOptionMenuItemID", GINT_TO_POINTER(0));

  if (name!=NULL)
    {
      cont=gck_hbox_new(container,FALSE,FALSE,FALSE,5,0,0);
      gck_label_new(name,cont);
    }
  else
    cont=container;

  _gck_pack(cont, optionmenu, expand, fill, padding);

  while (item_labels[i] != NULL)
    {
      gimp_option_menu_append(optionmenu, item_labels[i],
                              G_CALLBACK(_gck_option_menu_selected),
                              GINT_TO_POINTER(i));
      i++;
    }

  g_function_leave("gck_option_menu_new");
  return (optionmenu);
}

void gck_option_menu_set_history(GtkWidget *option_menu, gint index)
{
  g_function_enter("gck_option_menu_set_history");

  gimp_option_menu_set_history(option_menu, index);
  g_object_set_data(G_OBJECT(option_menu), "_GckOptionMenuItemID",
                    GINT_TO_POINTER(index));

  g_function_leave("gck_option_menu_set_history");
}

/**************************************************/
/* Create option menu with image names. Constrain */
/* shown types with mask given in "Constrain".    */
/**************************************************/

GtkWidget *gck_image_menu_new(char *name,
                              GtkWidget * container,
                              gint expand, gint fill,
                              gint padding, gint Constrain,
                              GCallback item_selected_func)
{
  GtkWidget *imagemenu = NULL;

  g_function_enter("gck_image_menu_new");

  /* Umm.. Hmm... I think I'll rather wait for Pete's */
  /* procedural database stuff :)                     */

  g_function_leave("gck_image_menu_new");
  return (imagemenu);
}
