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

/* To do this more elegantly, there should be a GtkWidget pixmap_button,  
 * probably derived from a simple hbox. It should keep track of the widgets 
 * sensitivity and draw the related pixmap. This way one could avoid the  
 * need to have a special function to set sensitivity as you'll find below.             
 *                                                      (sven@gimp.org)
 */

#include "appenv.h"
#include "gimprc.h"
#include "ops_buttons.h"


static GtkWidget *
ops_button_image (gchar **xpm_data)
{
  GdkPixbuf *pixbuf;
  GdkTexture *texture;
  GtkWidget *image;

  pixbuf = gdk_pixbuf_new_from_xpm_data ((const char **) xpm_data);
  if (!pixbuf)
    return gtk_image_new ();

  texture = gdk_texture_new_for_pixbuf (pixbuf);
  image = gtk_image_new_from_paintable (GDK_PAINTABLE (texture));
  gtk_image_set_pixel_size (GTK_IMAGE (image),
			    MAX (gdk_pixbuf_get_width (pixbuf),
				 gdk_pixbuf_get_height (pixbuf)));

  g_object_unref (texture);
  g_object_unref (pixbuf);

  return image;
}

GtkWidget *ops_button_box_new (GtkWidget   *parent,
			       OpsButton   *ops_buttons)
{
  GtkWidget *button;
  GtkWidget *button_box;

  button_box = gimp_hbox_new (FALSE, 1);

  while (ops_buttons->xpm_data)
    {
      button = gtk_button_new ();
      gtk_button_set_child (GTK_BUTTON (button),
			    ops_button_image (ops_buttons->xpm_data));
      g_signal_connect_swapped (button, "clicked",
				G_CALLBACK (ops_buttons->callback),
				parent);

      if (ops_buttons->tooltip)
	gtk_widget_set_tooltip_text (button, ops_buttons->tooltip);

      gimp_box_pack_start (button_box, button, TRUE, TRUE, 0);

      ops_buttons->widget = button;

      ops_buttons++;
    }
  return (button_box);
}


void
ops_button_box_set_insensitive (OpsButton *ops_buttons)
{
  while (ops_buttons->widget)
    {
      ops_button_set_sensitive (*ops_buttons, FALSE);
      ops_buttons++;
    }
}


void
ops_button_set_sensitive (OpsButton ops_button,
			  gint      sensitive)
{
  if (ops_button.widget)
    gtk_widget_set_sensitive (ops_button.widget, sensitive != FALSE);
}
