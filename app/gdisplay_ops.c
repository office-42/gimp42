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
#include <stdio.h>
#include <string.h>
#include "appenv.h"
#include "actionarea.h"
#include "cursorutil.h"
#include "fileops.h"
#include "gdisplay_ops.h"
#include "general.h"
#include "gimage.h"
#include "interface.h"
#include "menus.h"
#include "scale.h"
#include "gimprc.h"


static void gdisplay_close_warning_callback (GtkWidget *, gpointer);
static void gdisplay_cancel_warning_callback (GtkWidget *, gpointer);
static void gdisplay_close_warning_dialog   (char *, GDisplay *);

static GtkWidget *warning_dialog = NULL;

/*
 *  This file is for operations on the gdisplay object
 */

void
gdisplay_new_view (GDisplay *gdisp)
{
  GDisplay *new_gdisp;

  /* make sure the image has been fully loaded... */
  if (gdisp->gimage)
    {
      new_gdisp = gdisplay_new (gdisp->gimage, gdisp->scale);
      new_gdisp->scale = gdisp->scale;
      new_gdisp->offset_x = gdisp->offset_x;
      new_gdisp->offset_y = gdisp->offset_y;
    }
}


void
gdisplay_close_window (GDisplay *gdisp,
		       int       kill_it)
{
  /*  If the image has been modified, give the user a chance to save
   *  it before nuking it--this only applies if its the last view
   *  to an image canvas.  (a gimage with ref_count = 1)
   */
  if (!kill_it && (gdisp->gimage->ref_count == 1) &&
      (gdisp->gimage->dirty > 0) && confirm_on_close )
    gdisplay_close_warning_dialog (prune_filename (gimage_filename (gdisp->gimage)), gdisp);
  else
    {
      /* If POPUP_SHELL references this shell, then reset it. */
      if (popup_shell == gdisp->shell)
	popup_shell = NULL;
      gtk_window_destroy (GTK_WINDOW (gdisp->shell));
    }
}


/*  The size of the monitor the display is on (or a sensible guess
 *  before it has one).
 */
static void
gdisplay_screen_size (GDisplay *gdisp,
		      gint     *s_width,
		      gint     *s_height)
{
  GdkDisplay *display;
  GdkSurface *surface;
  GdkMonitor *monitor = NULL;
  GdkRectangle geometry;

  *s_width = 1024;
  *s_height = 768;

  display = gtk_widget_get_display (gdisp->shell);
  surface = gtk_native_get_surface (GTK_NATIVE (gdisp->shell));

  if (display && surface)
    monitor = gdk_display_get_monitor_at_surface (display, surface);

  if (!monitor && display)
    {
      GListModel *monitors = gdk_display_get_monitors (display);

      if (g_list_model_get_n_items (monitors) > 0)
	{
	  monitor = g_list_model_get_item (monitors, 0);
	  g_object_unref (monitor);	/*  the list keeps it alive  */
	}
    }

  if (monitor)
    {
      gdk_monitor_get_geometry (monitor, &geometry);
      if (geometry.width > 0 && geometry.height > 0)
	{
	  *s_width = geometry.width;
	  *s_height = geometry.height;
	}
    }
}

/*  What the window adds around the canvas: rulers, scrollbars and
 *  borders.
 */
static void
gdisplay_border_size (GDisplay *gdisp,
		      gint     *border_x,
		      gint     *border_y)
{
  gint shell_width, shell_height;

  shell_width = gtk_widget_get_width (gdisp->shell);
  shell_height = gtk_widget_get_height (gdisp->shell);

  if (shell_width > gdisp->disp_width && shell_height > gdisp->disp_height)
    {
      *border_x = shell_width - gdisp->disp_width;
      *border_y = shell_height - gdisp->disp_height;
    }
  else
    {
      /*  not allocated yet: the same allowance the display starts with  */
      *border_x = 40;
      *border_y = 40;
    }
}

/*  Resizes the display's window so that its canvas becomes width x
 *  height.  The canvas' "resize" handler picks up the size it really
 *  gets.
 */
static void
gdisplay_set_canvas_size (GDisplay *gdisp,
			  gint      width,
			  gint      height,
			  gint      border_x,
			  gint      border_y)
{
  gtk_window_set_default_size (GTK_WINDOW (gdisp->shell),
			       width + border_x, height + border_y);
}


void
gdisplay_shrink_wrap (GDisplay *gdisp)
{
  gint disp_width, disp_height;
  gint width, height;
  gint max_auto_width, max_auto_height;
  gint border_x, border_y;
  int s_width, s_height;

  gdisplay_screen_size (gdisp, &s_width, &s_height);

  width = SCALE (gdisp, gdisp->gimage->width);
  height = SCALE (gdisp, gdisp->gimage->height);

  disp_width = gdisp->disp_width;
  disp_height = gdisp->disp_height;

  gdisplay_border_size (gdisp, &border_x, &border_y);

  max_auto_width = (s_width - border_x) * 0.75;
  max_auto_height = (s_height - border_y) * 0.75;

  /*  If 1) the projected width & height are smaller than screen size, &
   *     2) the current display size isn't already the desired size, expand
   */
  if (((width + border_x) < s_width || (height + border_y) < s_height) &&
      (width != disp_width || height != disp_height))
    {
      width = ((width + border_x) < s_width) ? width : max_auto_width;
      height = ((height + border_y) < s_height) ? height : max_auto_height;

      gdisplay_set_canvas_size (gdisp, width, height, border_x, border_y);

      /*  Set the new disp_width and disp_height values  */
      gdisp->disp_width = width;
      gdisp->disp_height = height;
    }
  /*  If the projected width is greater than current, but less than
   *  3/4 of the screen size, expand automagically
   */
  else if ((width > disp_width || height > disp_height) &&
	   (disp_width < max_auto_width || disp_height < max_auto_height))
    {
      max_auto_width = MINIMUM (max_auto_width, width);
      max_auto_height = MINIMUM (max_auto_height, height);

      gdisplay_set_canvas_size (gdisp, max_auto_width, max_auto_height,
				border_x, border_y);

      /*  Set the new disp_width and disp_height values  */
      gdisp->disp_width = max_auto_width;
      gdisp->disp_height = max_auto_height;
    }
  /*  Otherwise, reexpose by hand to reflect changes  */
  else
    gdisplay_expose_full (gdisp);

  /*  If the width or height of the display has changed, recalculate
   *  the display offsets...
   */
  if (disp_width != gdisp->disp_width ||
      disp_height != gdisp->disp_height)
    {
      gdisp->offset_x += (disp_width - gdisp->disp_width) / 2;
      gdisp->offset_y += (disp_height - gdisp->disp_height) / 2;
      bounds_checking (gdisp);
    }
}


int
gdisplay_resize_image (GDisplay *gdisp)
{
  int sx, sy;
  int width, height;
  int border_x, border_y;

  /*  Calculate the width and height of the new canvas  */
  sx = SCALE (gdisp, gdisp->gimage->width);
  sy = SCALE (gdisp, gdisp->gimage->height);
  width = HIGHPASS (sx, gdisp->disp_width);
  height = HIGHPASS (sy, gdisp->disp_height);

  /* if the new dimensions of the ximage are different than the old...resize */
  if (width != gdisp->disp_width || height != gdisp->disp_height)
    {
      gdisplay_border_size (gdisp, &border_x, &border_y);

      /*  adjust the gdisplay offsets -- we need to set them so that the
       *  center of our viewport is at the center of the image.
       */
      gdisp->offset_x = (sx / 2) - (width / 2);
      gdisp->offset_y = (sy / 2) - (height / 2);

      gdisp->disp_width = width;
      gdisp->disp_height = height;

      gdisplay_set_canvas_size (gdisp, width, height, border_x, border_y);
    }

  return 1;
}


/********************************************************
 *   Routines to query before closing a dirty image     *
 ********************************************************/

static void
gdisplay_close_warning_callback (GtkWidget *w,
				 gpointer   client_data)
{
  GDisplay *gdisp;
  GtkWidget *mbox;

  menus_set_sensitive ("<Image>/File/Close", TRUE);
  mbox = (GtkWidget *) client_data;
  gdisp = (GDisplay *) g_object_get_data (G_OBJECT (mbox), "user_data");

  /* If POPUP_SHELL references this shell, then reset it. */
  if (popup_shell == gdisp->shell)
    popup_shell = NULL;

  gtk_window_destroy (GTK_WINDOW (gdisp->shell));
  gtk_window_destroy (GTK_WINDOW (mbox));
}


static void
gdisplay_cancel_warning_callback (GtkWidget *w,
				  gpointer   client_data)
{
  GtkWidget *mbox;

  menus_set_sensitive ("<Image>/File/Close", TRUE);
  mbox = (GtkWidget *) client_data;
  gtk_window_destroy (GTK_WINDOW (mbox));
}

static gboolean
gdisplay_delete_warning_callback (GtkWindow *window,
				  gpointer   client_data)
{
  menus_set_sensitive ("<Image>/File/Close", TRUE);

  return FALSE;
}

static void
gdisplay_destroy_warning_callback (GtkWidget *widget,
				   gpointer   client_data)
{
  warning_dialog = NULL;
}

static void
gdisplay_close_warning_dialog (char     *image_name,
			       GDisplay *gdisp)
{
  static ActionAreaItem mbox_action_items[2] =
  {
    { "Close", gdisplay_close_warning_callback, NULL, NULL },
    { "Cancel", gdisplay_cancel_warning_callback, NULL, NULL }
  };
  GtkWidget *mbox;
  GtkWidget *vbox;
  GtkWidget *label;
  char *warning_buf;

  /* FIXUP this will raise any prexsisting close dialogs, which can be a
     a bit confusing if you tried to close a new window because you had
     forgotten the old dialog was still around */
  /* If a warning dialog already exists raise the window and get out */
  if (warning_dialog != NULL)
    {
      gtk_window_present (GTK_WINDOW (warning_dialog));
      return;
    }

  menus_set_sensitive ("<Image>/File/Close", FALSE);

  /* should this be image_window or the actual image name??? */
  warning_dialog = mbox = gimp_dialog_new (image_name);
  gtk_window_set_transient_for (GTK_WINDOW (mbox), GTK_WINDOW (gdisp->shell));
  g_object_set_data (G_OBJECT (mbox), "user_data", gdisp);

  g_signal_connect (mbox, "close-request",
		    G_CALLBACK (gdisplay_delete_warning_callback),
		    mbox);

  g_signal_connect (mbox, "destroy",
		    G_CALLBACK (gdisplay_destroy_warning_callback),
		    mbox);

  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, 1);
  gimp_box_pack_start (gimp_dialog_get_vbox (mbox), vbox, TRUE, TRUE, 0);

  warning_buf = g_strdup_printf ("Changes made to %s.  Close anyway?",
				 image_name);
  label = gtk_label_new (warning_buf);
  gimp_box_pack_start (vbox, label, TRUE, FALSE, 0);
  g_free (warning_buf);

  mbox_action_items[0].user_data = mbox;
  mbox_action_items[1].user_data = mbox;
  build_action_area (mbox, mbox_action_items, 2, 0);

  gtk_window_present (GTK_WINDOW (mbox));
}
