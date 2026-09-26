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
#include "info_dialog.h"
#include "info_window.h"
#include "gdisplay.h"
#include "general.h"
#include "gximage.h"
#include "interface.h"

#define MAX_BUF 256

typedef struct _InfoWinData InfoWinData;
struct _InfoWinData
{
  char dimensions_str[MAX_BUF];
  char scale_str[MAX_BUF];
  char color_type_str[MAX_BUF];
  char visual_class_str[MAX_BUF];
  char visual_depth_str[MAX_BUF];
  char shades_str[MAX_BUF];
};

static void
get_shades (GDisplay *gdisp,
	    char     *buf)
{
  /*  The display is always drawn through cairo in 24 bit RGB  */
  switch (gimage_base_type (gdisp->gimage))
    {
    case GRAY:
      sprintf (buf, "%d", 256);
      break;
    case RGB:
      sprintf (buf, "256 / 256 / 256");
      break;
    case INDEXED:
      sprintf (buf, "%d", gdisp->gimage->num_cols);
      break;
    default:
      buf[0] = '\0';
      break;
    }
}

static void
info_window_close_callback (GtkWidget *w,
			    gpointer   client_data)
{
  info_dialog_popdown ((InfoDialog *) client_data);
}

  /*  displays information:
   *    image name
   *    image width, height
   *    zoom ratio
   *    image color type
   *    Display info:
   *      visual class
   *      visual depth
   *      shades of color/gray
   */

InfoDialog *
info_window_create (void *gdisp_ptr)
{
  InfoDialog *info_win;
  GDisplay *gdisp;
  InfoWinData *iwd;
  char * title, * title_buf;
  int type;

  gdisp = (GDisplay *) gdisp_ptr;

  title = prune_filename (gimage_filename (gdisp->gimage));
  type = gimage_base_type (gdisp->gimage);

  /*  allocate the title buffer  */
  title_buf = g_strdup_printf ("%s: Window Info", title);

  /*  create the info dialog  */
  info_win = info_dialog_new (title_buf);
  g_free (title_buf);

  iwd = (InfoWinData *) g_malloc (sizeof (InfoWinData));
  info_win->user_data = iwd;
  iwd->dimensions_str[0] = '\0';
  iwd->scale_str[0] = '\0';
  iwd->color_type_str[0] = '\0';
  iwd->visual_class_str[0] = '\0';
  iwd->visual_depth_str[0] = '\0';
  iwd->shades_str[0] = '\0';

  /*  add the information fields  */
  info_dialog_add_field (info_win, "Dimensions (w x h): ", iwd->dimensions_str);
  info_dialog_add_field (info_win, "Scale Ratio: ", iwd->scale_str);
  info_dialog_add_field (info_win, "Display Type: ", iwd->color_type_str);
  info_dialog_add_field (info_win, "Visual Class: ", iwd->visual_class_str);
  info_dialog_add_field (info_win, "Visual Depth: ", iwd->visual_depth_str);
  if (type == RGB)
    info_dialog_add_field (info_win, "Shades of Color: ", iwd->shades_str);
  else if (type == INDEXED)
    info_dialog_add_field (info_win, "Shades: ", iwd->shades_str);
  else if (type == GRAY)
    info_dialog_add_field (info_win, "Shades of Gray: ", iwd->shades_str);

  /*  update the fields  */
  info_window_update (info_win, gdisp_ptr);

  /* Create the action area  */
  gimp_dialog_add_button (info_win->shell, "Close",
			  G_CALLBACK (info_window_close_callback),
			  info_win, TRUE);

  return info_win;
}

void
info_window_free (InfoDialog *info_win)
{
  g_free (info_win->user_data);
  info_dialog_free (info_win);
}

void
info_window_update (InfoDialog *info_win,
		    void       *gdisp_ptr)
{
  GDisplay *gdisp;
  InfoWinData *iwd;
  int type;
  int flat;

  gdisp = (GDisplay *) gdisp_ptr;
  iwd = (InfoWinData *) info_win->user_data;

  /*  width and height  */
  sprintf (iwd->dimensions_str, "%d x %d",
	   (int) gdisp->gimage->width, (int) gdisp->gimage->height);

  /*  zoom ratio  */
  sprintf (iwd->scale_str, "%d:%d",
	   SCALEDEST (gdisp), SCALESRC (gdisp));

  type = gimage_base_type (gdisp->gimage);
  flat = gimage_is_flat (gdisp->gimage);

  /*  color type  */
  if (type == RGB && flat)
    sprintf (iwd->color_type_str, "%s", "RGB Color");
  else if (type == GRAY && flat)
    sprintf (iwd->color_type_str, "%s", "Grayscale");
  else if (type == INDEXED && flat)
    sprintf (iwd->color_type_str, "%s", "Indexed Color");
  if (type == RGB && !flat)
    sprintf (iwd->color_type_str, "%s", "RGB-alpha Color");
  else if (type == GRAY && !flat)
    sprintf (iwd->color_type_str, "%s", "Grayscale-alpha");
  else if (type == INDEXED && !flat)
    sprintf (iwd->color_type_str, "%s", "Indexed-alpha Color");

  /*  visual class: there are no X visuals any more, the display is
   *  rendered as true color through cairo
   */
  sprintf (iwd->visual_class_str, "%s", "True Color");

  /*  visual depth  */
  sprintf (iwd->visual_depth_str, "%d", 24);

  /*  pure color shades  */
  get_shades (gdisp, iwd->shades_str);

  info_dialog_update (info_win);
}
