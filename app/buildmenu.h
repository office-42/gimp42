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
#ifndef __BUILDMENU_H__
#define __BUILDMENU_H__

#include <gtk/gtk.h>

/* Structures */
typedef struct _MenuItem   MenuItem;

typedef void (*MenuItemCallback) (GtkWidget *widget,
				  gpointer   user_data);

struct _MenuItem
{
  char *label;
  char  accelerator_key;          /*  unused  */
  int   accelerator_mods;         /*  unused  */
  MenuItemCallback callback;
  gpointer user_data;
  MenuItem *subitems;             /*  unused  */
  GtkWidget *widget;              /*  the option menu the item is in  */
  int index;                      /*  its position there              */
};

/* Function declarations */

/*  Builds an option menu (see gimp_option_menu_new) from items, which
 *  end with one whose label is NULL.  Labels starting with '-' were
 *  separators and are left out.  Choosing an item calls its callback
 *  with the option menu and the item's user_data.  Pack the result
 *  directly; there is no separate GtkOptionMenu any more.
 */
GtkWidget *  build_menu (MenuItem *items, gpointer unused);

/*  Makes one item of such a menu (in)sensitive.  */
void         menu_item_set_sensitive (MenuItem *item, int sensitive);

/*  Shows item as the chosen one, without calling its callback.  */
void         menu_item_set_active (MenuItem *item);

#endif /* BUILDMENU_H */
