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

/* Small helpers shared by the application and the plug-ins, for the
 * things every GIMP dialog does and GTK 4 no longer spells the way GTK 1
 * did: packing with expand/fill/padding, tables, dialogs with an action
 * area, option menus and modal main loops.  docs/PORTING.md says which
 * GTK 1 call maps to which.
 */

#ifndef __GIMP_WIDGETS_H__
#define __GIMP_WIDGETS_H__

#include <gtk/gtk.h>

#include <libgimp/gimppreview.h>

G_BEGIN_DECLS

/*  Main loops.  gimp_main_loop_run () takes the place of gtk_main ():
 *  it runs a (possibly nested) loop until the matching
 *  gimp_main_loop_quit ().
 */
void        gimp_main_loop_run     (void);
void        gimp_main_loop_quit    (void);
gint        gimp_main_loop_level   (void);

/*  Runs pending events without blocking, the old
 *  "while (gtk_events_pending ()) gtk_main_iteration ();".
 */
void        gimp_process_events    (void);

/*  Boxes, with GTK 1's expand / fill / padding semantics.  */
GtkWidget * gimp_hbox_new          (gboolean     homogeneous,
				    gint         spacing);
GtkWidget * gimp_vbox_new          (gboolean     homogeneous,
				    gint         spacing);
void        gimp_box_pack_start    (GtkWidget   *box,
				    GtkWidget   *child,
				    gboolean     expand,
				    gboolean     fill,
				    guint        padding);
void        gimp_box_pack_end      (GtkWidget   *box,
				    GtkWidget   *child,
				    gboolean     expand,
				    gboolean     fill,
				    guint        padding);

/*  Tables are GtkGrids; the attach options keep GTK 1's meaning.  */
typedef enum
{
  GIMP_EXPAND = 1 << 0,
  GIMP_SHRINK = 1 << 1,
  GIMP_FILL   = 1 << 2
} GimpAttachOptions;

GtkWidget * gimp_table_new         (gint         rows,
				    gint         columns,
				    gboolean     homogeneous);
void        gimp_table_attach      (GtkWidget   *table,
				    GtkWidget   *child,
				    guint        left_attach,
				    guint        right_attach,
				    guint        top_attach,
				    guint        bottom_attach,
				    GimpAttachOptions xoptions,
				    GimpAttachOptions yoptions,
				    guint        xpadding,
				    guint        ypadding);
void        gimp_table_attach_defaults (GtkWidget *table,
				    GtkWidget   *child,
				    guint        left_attach,
				    guint        right_attach,
				    guint        top_attach,
				    guint        bottom_attach);

/*  Adds child to any single-child or box container.  */
void        gimp_container_add     (GtkWidget   *container,
				    GtkWidget   *child);
void        gimp_container_remove  (GtkWidget   *container,
				    GtkWidget   *child);
/*  Border width as margins on the container's content.  */
void        gimp_container_set_border_width (GtkWidget *widget,
				    guint        width);

/*  gtk_misc_set_alignment (): labels align their text, anything else
 *  aligns itself in the space it is given.
 */
void        gimp_misc_set_alignment (GtkWidget  *widget,
				    gfloat       xalign,
				    gfloat       yalign);

/*  A frame with its content inset the way GTK 1 frames were.  */
GtkWidget * gimp_frame_new         (const gchar *label);

/*  Dialogs: a window holding a content box above an action area.  */
GtkWidget * gimp_dialog_new        (const gchar *title);
GtkWidget * gimp_dialog_get_vbox   (GtkWidget   *dialog);
GtkWidget * gimp_dialog_get_action_area (GtkWidget *dialog);

/*  Adds a button to the action area.  callback is connected to
 *  "clicked" with data; is_default makes it the window's default
 *  widget.
 */
GtkWidget * gimp_dialog_add_button (GtkWidget   *dialog,
				    const gchar *label,
				    GCallback    callback,
				    gpointer     data,
				    gboolean     is_default);

/*  Shows a toplevel: gtk_window_present () for windows, visibility
 *  for anything else.
 */
void        gimp_widget_show_toplevel (GtkWidget *widget);

/*  Destroys a toplevel window, or unparents any other widget.  */
void        gimp_widget_destroy    (GtkWidget   *widget);

/*  Radio buttons are grouped check buttons.  group is any button
 *  already in the group, or NULL for the first.
 */
GtkWidget * gimp_radio_button_new  (GtkWidget   *group,
				    const gchar *label);

/*  Option menus, on GtkDropDown.  The callback of the chosen item is
 *  called with the option menu and the item's data.
 */
typedef void (* GimpOptionMenuCallback) (GtkWidget *option_menu,
					 gpointer   data);

GtkWidget * gimp_option_menu_new   (void);
void        gimp_option_menu_append (GtkWidget  *option_menu,
				    const gchar *label,
				    GCallback    callback,
				    gpointer     data);
void        gimp_option_menu_clear (GtkWidget   *option_menu);
void        gimp_option_menu_set_history (GtkWidget *option_menu,
				    gint         index);
gint        gimp_option_menu_get_history (GtkWidget *option_menu);
gpointer    gimp_option_menu_get_item_data (GtkWidget *option_menu,
				    gint         index);
void        gimp_option_menu_set_item_sensitive (GtkWidget *option_menu,
				    gint         index,
				    gboolean     sensitive);

/*  A horizontal scale on adjustment, with the value drawn above it
 *  and the given number of digits.
 */
GtkWidget * gimp_hscale_new        (GtkAdjustment *adjustment,
				    gint         digits);

/*  Shows message in a small window with an OK button.  */
void        gimp_message_box_show  (GtkWindow   *parent,
				    const gchar *title,
				    const gchar *message);

/*  Asks for a file with a GtkFileDialog.  callback gets the chosen
 *  path, or NULL when the dialog was cancelled.
 */
typedef void (* GimpFileCallback) (const gchar *filename,
				   gpointer     data);

void        gimp_file_dialog_open  (GtkWindow   *parent,
				    const gchar *title,
				    const gchar *initial,
				    GimpFileCallback callback,
				    gpointer     data);
void        gimp_file_dialog_save  (GtkWindow   *parent,
				    const gchar *title,
				    const gchar *initial,
				    GimpFileCallback callback,
				    gpointer     data);

/*  Asks for a color with a GtkColorDialog.  callback gets the chosen
 *  color as 0..255 components, or is not called at all when the dialog
 *  was cancelled.
 */
typedef void (* GimpColorCallback) (const guchar *rgb,
				    gpointer      data);

void        gimp_color_dialog_run  (GtkWindow   *parent,
				    const gchar *title,
				    const guchar *initial_rgb,
				    GimpColorCallback callback,
				    gpointer     data);

G_END_DECLS

#endif /* __GIMP_WIDGETS_H__ */
