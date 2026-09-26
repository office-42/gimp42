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
#ifndef __MENUS_H__
#define __MENUS_H__

#include <gtk/gtk.h>

/*  Menus are addressed by path, as they were with GTK 1's item
 *  factories: "<Toolbox>/File/New", "<Image>/Filters/Blur/Blur".  Behind
 *  them are GMenu models and one action group; windows show them as a
 *  menu bar (the toolbox) or a popover (the image popup), and get the
 *  accelerators through menus_install ().
 */

/*  Called with the widget that was activated (always NULL now), the
 *  entry's callback_data and its callback_action.  Callbacks that only
 *  take the first two arguments may be used as well.
 */
typedef void (* MenuCallback) (GtkWidget *widget,
			       gpointer   callback_data,
			       guint      callback_action);

typedef struct _MenuEntry MenuEntry;

struct _MenuEntry
{
  char         *path;             /*  "<Image>/File/Save"                   */
  char         *accelerator;      /*  "<control>S", "equal", NULL           */
  MenuCallback  callback;
  guint         callback_action;
  char         *item_type;        /*  NULL, "<Separator>", "<ToggleItem>"   */
  gpointer      callback_data;
};

void          menus_create         (MenuEntry   *entries,
				    int          nmenu_entries);
void          menus_set_sensitive  (char        *path,
				    int          sensitive);
void          menus_set_state      (char        *path,
				    int          state);
int           menus_get_state      (char        *path);
void          menus_destroy        (char        *path);
void          menus_quit           (void);

/*  The menu models, for a GtkPopoverMenuBar or GtkPopoverMenu.  */
GMenuModel *  menus_get_toolbox_model (void);
GMenuModel *  menus_get_image_model   (void);

/*  Gives window the menu actions and the accelerators of factory
 *  ("<Toolbox>" or "<Image>").
 */
void          menus_install        (GtkWidget   *window,
				    const char  *factory);

#endif /* MENUS_H */
