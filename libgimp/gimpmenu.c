/* LIBGIMP - The GIMP Library
 * Copyright (C) 1995-1997 Peter Mattis and Spencer Kimball
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include <stdio.h>
#include <string.h>

#include "gimp.h"
#include "gimpui.h"

/*  The menus are option menus (see gimpwidgets.h): each item's data is the
 *  ID it stands for, and choosing one calls the GimpMenuCallback given at
 *  creation with that ID.
 */

typedef struct
{
  GimpMenuCallback callback;
  gpointer         data;
} MenuInfo;

#define MENU_INFO_KEY "gimp-menu-info"

static const char* gimp_base_name     (const char *str);
static void        gimp_menu_callback (GtkWidget  *option_menu,
				       gpointer    id);

static GtkWidget *
gimp_menu_start (GimpMenuCallback callback,
		 gpointer         data)
{
  GtkWidget *option_menu;
  MenuInfo  *info;

  option_menu = gimp_option_menu_new ();

  info = g_new0 (MenuInfo, 1);
  info->callback = callback;
  info->data     = data;
  g_object_set_data_full (G_OBJECT (option_menu), MENU_INFO_KEY, info, g_free);

  return option_menu;
}

static void
gimp_menu_add (GtkWidget  *option_menu,
	       const char *label,
	       gint32      id)
{
  gimp_option_menu_append (option_menu, label,
			   G_CALLBACK (gimp_menu_callback),
			   GINT_TO_POINTER (id));
}

static void
gimp_menu_finish (GtkWidget *option_menu,
		  int        n_items)
{
  if (n_items == 0)
    {
      gimp_option_menu_append (option_menu, "none", NULL, NULL);
      gimp_option_menu_set_item_sensitive (option_menu, 0, FALSE);
      gtk_widget_set_sensitive (option_menu, FALSE);
    }
}

static char *
gimp_image_label (gint32 image_ID)
{
  char *filename;
  char *label;

  filename = gimp_image_get_filename (image_ID);
  label = g_strdup_printf ("%s-%d", gimp_base_name (filename), image_ID);
  g_free (filename);

  return label;
}

GtkWidget*
gimp_image_menu_new (GimpConstraintFunc constraint,
		     GimpMenuCallback   callback,
		     gpointer           data,
		     gint32             active_image)
{
  GtkWidget *menu;
  char *label;
  gint32 *images;
  int nimages;
  int i, k;

  menu = gimp_menu_start (callback, data);

  images = gimp_query_images (&nimages);
  for (i = 0, k = 0; i < nimages; i++)
    if (!constraint || (* constraint) (images[i], -1, data))
      {
	label = gimp_image_label (images[i]);
	gimp_menu_add (menu, label, images[i]);
	g_free (label);

	if (images[i] == active_image)
	  gimp_option_menu_set_history (menu, k);

	k += 1;
      }

  gimp_menu_finish (menu, k);

  if (images)
    {
      if (active_image == -1)
	active_image = images[0];
      (* callback) (active_image, data);
    }
  g_free (images);

  return menu;
}

GtkWidget*
gimp_layer_menu_new (GimpConstraintFunc constraint,
		     GimpMenuCallback   callback,
		     gpointer           data,
		     gint32             active_layer)
{
  GtkWidget *menu;
  char *name;
  char *image_label;
  char *label;
  gint32 *images;
  gint32 *layers;
  gint32 layer;
  int nimages;
  int nlayers;
  int i, j, k;

  menu = gimp_menu_start (callback, data);

  layer = -1;

  images = gimp_query_images (&nimages);
  for (i = 0, k = 0; i < nimages; i++)
    if (!constraint || (* constraint) (images[i], -1, data))
      {
	image_label = gimp_image_label (images[i]);

	layers = gimp_image_get_layers (images[i], &nlayers);
	for (j = 0; j < nlayers; j++)
	  if (!constraint || (* constraint) (images[i], layers[j], data))
	    {
	      name = gimp_layer_get_name (layers[j]);
	      label = g_strdup_printf ("%s/%s", image_label, name);
	      g_free (name);

	      gimp_menu_add (menu, label, layers[j]);
	      g_free (label);

	      if (layers[j] == active_layer)
		{
		  layer = active_layer;
		  gimp_option_menu_set_history (menu, k);
		}
	      else if (layer == -1)
		layer = layers[j];

	      k += 1;
	    }
	g_free (layers);

	g_free (image_label);
      }
  g_free (images);

  gimp_menu_finish (menu, k);

  if (layer != -1)
    (* callback) (layer, data);

  return menu;
}

GtkWidget*
gimp_channel_menu_new (GimpConstraintFunc constraint,
		       GimpMenuCallback   callback,
		       gpointer           data,
		       gint32             active_channel)
{
  GtkWidget *menu;
  char *name;
  char *image_label;
  char *label;
  gint32 *images;
  gint32 *channels;
  gint32 channel;
  int nimages;
  int nchannels;
  int i, j, k;

  menu = gimp_menu_start (callback, data);

  channel = -1;

  images = gimp_query_images (&nimages);
  for (i = 0, k = 0; i < nimages; i++)
    if (!constraint || (* constraint) (images[i], -1, data))
      {
	image_label = gimp_image_label (images[i]);

	channels = gimp_image_get_channels (images[i], &nchannels);
	for (j = 0; j < nchannels; j++)
	  if (!constraint || (* constraint) (images[i], channels[j], data))
	    {
	      name = gimp_channel_get_name (channels[j]);
	      label = g_strdup_printf ("%s/%s", image_label, name);
	      g_free (name);

	      gimp_menu_add (menu, label, channels[j]);
	      g_free (label);

	      if (channels[j] == active_channel)
		{
		  channel = active_channel;
		  gimp_option_menu_set_history (menu, k);
		}
	      else if (channel == -1)
		channel = channels[j];

	      k += 1;
	    }
	g_free (channels);

	g_free (image_label);
      }
  g_free (images);

  gimp_menu_finish (menu, k);

  if (channel != -1)
    (* callback) (channel, data);

  return menu;
}

GtkWidget*
gimp_drawable_menu_new (GimpConstraintFunc constraint,
			GimpMenuCallback   callback,
			gpointer           data,
			gint32             active_drawable)
{
  GtkWidget *menu;
  char *name;
  char *image_label;
  char *label;
  gint32 *images;
  gint32 *layers;
  gint32 *channels;
  gint32 drawable;
  int nimages;
  int nlayers;
  int nchannels;
  int i, j, k;

  menu = gimp_menu_start (callback, data);

  drawable = -1;

  images = gimp_query_images (&nimages);
  for (i = 0, k = 0; i < nimages; i++)
    if (!constraint || (* constraint) (images[i], -1, data))
      {
	image_label = gimp_image_label (images[i]);

	layers = gimp_image_get_layers (images[i], &nlayers);
	for (j = 0; j < nlayers; j++)
	  if (!constraint || (* constraint) (images[i], layers[j], data))
	    {
	      name = gimp_layer_get_name (layers[j]);
	      label = g_strdup_printf ("%s/%s", image_label, name);
	      g_free (name);

	      gimp_menu_add (menu, label, layers[j]);
	      g_free (label);

	      if (layers[j] == active_drawable)
		{
		  drawable = active_drawable;
		  gimp_option_menu_set_history (menu, k);
		}
	      else if (drawable == -1)
		drawable = layers[j];

	      k += 1;
	    }
	g_free (layers);

	channels = gimp_image_get_channels (images[i], &nchannels);
	for (j = 0; j < nchannels; j++)
	  if (!constraint || (* constraint) (images[i], channels[j], data))
	    {
	      name = gimp_channel_get_name (channels[j]);
	      label = g_strdup_printf ("%s/%s", image_label, name);
	      g_free (name);

	      gimp_menu_add (menu, label, channels[j]);
	      g_free (label);

	      if (channels[j] == active_drawable)
		{
		  drawable = active_drawable;
		  gimp_option_menu_set_history (menu, k);
		}
	      else if (drawable == -1)
		drawable = channels[j];

	      k += 1;
	    }
	g_free (channels);

	g_free (image_label);
      }
  g_free (images);

  gimp_menu_finish (menu, k);

  if (drawable != -1)
    (* callback) (drawable, data);

  return menu;
}


static const char*
gimp_base_name (const char *str)
{
  const char *t;
  const char *b;

  t = strrchr (str, '/');
  b = strrchr (str, '\\');
  if (b > t)
    t = b;
  if (!t)
    return str;
  return t+1;
}

static void
gimp_menu_callback (GtkWidget *option_menu,
		    gpointer   id)
{
  MenuInfo *info;

  info = g_object_get_data (G_OBJECT (option_menu), MENU_INFO_KEY);

  if (info && info->callback)
    (* info->callback) (GPOINTER_TO_INT (id), info->data);
}
