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
#include "buildmenu.h"

GtkWidget *
build_menu (MenuItem *items,
	    gpointer  unused)
{
  GtkWidget *option_menu;
  int index = 0;

  option_menu = gimp_option_menu_new ();

  while (items->label)
    {
      if (items->label[0] != '-')
	{
	  gimp_option_menu_append (option_menu, items->label,
				   G_CALLBACK (items->callback),
				   items->user_data);
	  items->widget = option_menu;
	  items->index = index++;
	}
      else
	{
	  items->widget = NULL;
	  items->index = -1;
	}

      items++;
    }

  return option_menu;
}

void
menu_item_set_sensitive (MenuItem *item,
			 int       sensitive)
{
  if (item->widget)
    gimp_option_menu_set_item_sensitive (item->widget, item->index, sensitive);
}

void
menu_item_set_active (MenuItem *item)
{
  if (item->widget)
    gimp_option_menu_set_history (item->widget, item->index);
}
