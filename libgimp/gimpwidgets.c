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
#include "config.h"

#include <string.h>

#include "gimpwidgets.h"


/*  Main loops  */

static GSList *main_loops = NULL;

void
gimp_main_loop_run (void)
{
  GMainLoop *loop = g_main_loop_new (NULL, FALSE);

  main_loops = g_slist_prepend (main_loops, loop);
  g_main_loop_run (loop);
  main_loops = g_slist_remove (main_loops, loop);
  g_main_loop_unref (loop);
}

void
gimp_main_loop_quit (void)
{
  if (main_loops)
    g_main_loop_quit (main_loops->data);
}

gint
gimp_main_loop_level (void)
{
  return g_slist_length (main_loops);
}

void
gimp_process_events (void)
{
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);
}


/*  Boxes  */

/*  The first child packed at the end.  Children packed at the start go
 *  before it, children packed at the end go before it and become it.
 */
#define END_ANCHOR_KEY "gimp-box-end-anchor"

GtkWidget *
gimp_hbox_new (gboolean homogeneous,
	       gint     spacing)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, spacing);

  gtk_box_set_homogeneous (GTK_BOX (box), homogeneous);

  return box;
}

GtkWidget *
gimp_vbox_new (gboolean homogeneous,
	       gint     spacing)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, spacing);

  gtk_box_set_homogeneous (GTK_BOX (box), homogeneous);

  return box;
}

static void
gimp_box_child_setup (GtkWidget *box,
		      GtkWidget *child,
		      gboolean   expand,
		      gboolean   fill,
		      guint      padding)
{
  GtkOrientation orientation;

  orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (box));

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      if (expand)
	gtk_widget_set_hexpand (child, TRUE);
      if (expand && !fill)
	gtk_widget_set_halign (child, GTK_ALIGN_CENTER);
      if (padding)
	{
	  gtk_widget_set_margin_start (child, padding);
	  gtk_widget_set_margin_end (child, padding);
	}
    }
  else
    {
      if (expand)
	gtk_widget_set_vexpand (child, TRUE);
      if (expand && !fill)
	gtk_widget_set_valign (child, GTK_ALIGN_CENTER);
      if (padding)
	{
	  gtk_widget_set_margin_top (child, padding);
	  gtk_widget_set_margin_bottom (child, padding);
	}
    }
}

void
gimp_box_pack_start (GtkWidget *box,
		     GtkWidget *child,
		     gboolean   expand,
		     gboolean   fill,
		     guint      padding)
{
  GtkWidget *anchor;

  g_return_if_fail (GTK_IS_BOX (box));
  g_return_if_fail (GTK_IS_WIDGET (child));

  gimp_box_child_setup (box, child, expand, fill, padding);

  anchor = g_object_get_data (G_OBJECT (box), END_ANCHOR_KEY);

  if (anchor && gtk_widget_get_parent (anchor) == box)
    gtk_box_insert_child_after (GTK_BOX (box), child,
				gtk_widget_get_prev_sibling (anchor));
  else
    gtk_box_append (GTK_BOX (box), child);
}

void
gimp_box_pack_end (GtkWidget *box,
		   GtkWidget *child,
		   gboolean   expand,
		   gboolean   fill,
		   guint      padding)
{
  GtkWidget *anchor;

  g_return_if_fail (GTK_IS_BOX (box));
  g_return_if_fail (GTK_IS_WIDGET (child));

  gimp_box_child_setup (box, child, expand, fill, padding);

  anchor = g_object_get_data (G_OBJECT (box), END_ANCHOR_KEY);

  if (anchor && gtk_widget_get_parent (anchor) == box)
    {
      gtk_box_insert_child_after (GTK_BOX (box), child,
				  gtk_widget_get_prev_sibling (anchor));
    }
  else
    {
      /*  A spacer takes up the slack, so that what is packed at the end
       *  sits at the end even when nothing expands.
       */
      GtkWidget *spacer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

      if (gtk_orientable_get_orientation (GTK_ORIENTABLE (box)) ==
	  GTK_ORIENTATION_HORIZONTAL)
	gtk_widget_set_hexpand (spacer, TRUE);
      else
	gtk_widget_set_vexpand (spacer, TRUE);

      gtk_box_append (GTK_BOX (box), spacer);
      gtk_box_append (GTK_BOX (box), child);
    }

  g_object_set_data (G_OBJECT (box), END_ANCHOR_KEY, child);
}


/*  Tables  */

GtkWidget *
gimp_table_new (gint     rows,
		gint     columns,
		gboolean homogeneous)
{
  GtkWidget *grid = gtk_grid_new ();

  gtk_grid_set_row_homogeneous (GTK_GRID (grid), homogeneous);
  gtk_grid_set_column_homogeneous (GTK_GRID (grid), homogeneous);

  return grid;
}

void
gimp_table_attach (GtkWidget         *table,
		   GtkWidget         *child,
		   guint              left_attach,
		   guint              right_attach,
		   guint              top_attach,
		   guint              bottom_attach,
		   GimpAttachOptions  xoptions,
		   GimpAttachOptions  yoptions,
		   guint              xpadding,
		   guint              ypadding)
{
  g_return_if_fail (GTK_IS_GRID (table));
  g_return_if_fail (GTK_IS_WIDGET (child));

  if (xoptions & GIMP_EXPAND)
    gtk_widget_set_hexpand (child, TRUE);
  if (! (xoptions & GIMP_FILL) &&
      gtk_widget_get_halign (child) == GTK_ALIGN_FILL)
    gtk_widget_set_halign (child, GTK_ALIGN_CENTER);

  if (yoptions & GIMP_EXPAND)
    gtk_widget_set_vexpand (child, TRUE);
  if (! (yoptions & GIMP_FILL) &&
      gtk_widget_get_valign (child) == GTK_ALIGN_FILL)
    gtk_widget_set_valign (child, GTK_ALIGN_CENTER);

  if (xpadding)
    {
      gtk_widget_set_margin_start (child, xpadding);
      gtk_widget_set_margin_end (child, xpadding);
    }
  if (ypadding)
    {
      gtk_widget_set_margin_top (child, ypadding);
      gtk_widget_set_margin_bottom (child, ypadding);
    }

  gtk_grid_attach (GTK_GRID (table), child,
		   left_attach, top_attach,
		   MAX (1, (gint) right_attach - (gint) left_attach),
		   MAX (1, (gint) bottom_attach - (gint) top_attach));
}

void
gimp_table_attach_defaults (GtkWidget *table,
			    GtkWidget *child,
			    guint      left_attach,
			    guint      right_attach,
			    guint      top_attach,
			    guint      bottom_attach)
{
  gimp_table_attach (table, child,
		     left_attach, right_attach, top_attach, bottom_attach,
		     GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 0, 0);
}


/*  Containers  */

#define BORDER_KEY "gimp-border-width"

static void
gimp_apply_border (GtkWidget *child,
		   guint      width)
{
  gtk_widget_set_margin_start (child, width);
  gtk_widget_set_margin_end (child, width);
  gtk_widget_set_margin_top (child, width);
  gtk_widget_set_margin_bottom (child, width);
}

static gboolean
gimp_is_single_child_container (GtkWidget *widget)
{
  return (GTK_IS_WINDOW (widget)          ||
	  GTK_IS_FRAME (widget)           ||
	  GTK_IS_BUTTON (widget)          ||
	  GTK_IS_SCROLLED_WINDOW (widget) ||
	  GTK_IS_VIEWPORT (widget)        ||
	  GTK_IS_ASPECT_FRAME (widget)    ||
	  GTK_IS_POPOVER (widget)         ||
	  GTK_IS_EXPANDER (widget)        ||
	  GTK_IS_OVERLAY (widget)         ||
	  GTK_IS_REVEALER (widget));
}

void
gimp_container_add (GtkWidget *container,
		    GtkWidget *child)
{
  guint border;

  g_return_if_fail (GTK_IS_WIDGET (container));
  g_return_if_fail (GTK_IS_WIDGET (child));

  border = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (container),
						BORDER_KEY));
  if (border && gimp_is_single_child_container (container))
    gimp_apply_border (child, border);

  if (GTK_IS_BOX (container))
    gimp_box_pack_start (container, child, TRUE, TRUE, 0);
  else if (GTK_IS_WINDOW (container))
    gtk_window_set_child (GTK_WINDOW (container), child);
  else if (GTK_IS_FRAME (container))
    gtk_frame_set_child (GTK_FRAME (container), child);
  else if (GTK_IS_BUTTON (container))
    gtk_button_set_child (GTK_BUTTON (container), child);
  else if (GTK_IS_SCROLLED_WINDOW (container))
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (container), child);
  else if (GTK_IS_VIEWPORT (container))
    gtk_viewport_set_child (GTK_VIEWPORT (container), child);
  else if (GTK_IS_ASPECT_FRAME (container))
    gtk_aspect_frame_set_child (GTK_ASPECT_FRAME (container), child);
  else if (GTK_IS_POPOVER (container))
    gtk_popover_set_child (GTK_POPOVER (container), child);
  else if (GTK_IS_EXPANDER (container))
    gtk_expander_set_child (GTK_EXPANDER (container), child);
  else if (GTK_IS_OVERLAY (container))
    gtk_overlay_set_child (GTK_OVERLAY (container), child);
  else if (GTK_IS_REVEALER (container))
    gtk_revealer_set_child (GTK_REVEALER (container), child);
  else if (GTK_IS_LIST_BOX (container))
    gtk_list_box_append (GTK_LIST_BOX (container), child);
  else if (GTK_IS_FLOW_BOX (container))
    gtk_flow_box_append (GTK_FLOW_BOX (container), child);
  else if (GTK_IS_FIXED (container))
    gtk_fixed_put (GTK_FIXED (container), child, 0, 0);
  else if (GTK_IS_GRID (container))
    gtk_grid_attach_next_to (GTK_GRID (container), child, NULL,
			     GTK_POS_BOTTOM, 1, 1);
  else if (GTK_IS_NOTEBOOK (container))
    gtk_notebook_append_page (GTK_NOTEBOOK (container), child, NULL);
  else
    g_warning ("gimp_container_add: cannot add a %s to a %s",
	       G_OBJECT_TYPE_NAME (child), G_OBJECT_TYPE_NAME (container));
}

void
gimp_container_remove (GtkWidget *container,
		       GtkWidget *child)
{
  g_return_if_fail (GTK_IS_WIDGET (container));
  g_return_if_fail (GTK_IS_WIDGET (child));

  if (GTK_IS_BOX (container))
    {
      if (g_object_get_data (G_OBJECT (container), END_ANCHOR_KEY) == child)
	g_object_set_data (G_OBJECT (container), END_ANCHOR_KEY,
			   gtk_widget_get_next_sibling (child));
      gtk_box_remove (GTK_BOX (container), child);
    }
  else if (GTK_IS_WINDOW (container))
    gtk_window_set_child (GTK_WINDOW (container), NULL);
  else if (GTK_IS_FRAME (container))
    gtk_frame_set_child (GTK_FRAME (container), NULL);
  else if (GTK_IS_BUTTON (container))
    gtk_button_set_child (GTK_BUTTON (container), NULL);
  else if (GTK_IS_SCROLLED_WINDOW (container))
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (container), NULL);
  else if (GTK_IS_VIEWPORT (container))
    gtk_viewport_set_child (GTK_VIEWPORT (container), NULL);
  else if (GTK_IS_ASPECT_FRAME (container))
    gtk_aspect_frame_set_child (GTK_ASPECT_FRAME (container), NULL);
  else if (GTK_IS_POPOVER (container))
    gtk_popover_set_child (GTK_POPOVER (container), NULL);
  else if (GTK_IS_EXPANDER (container))
    gtk_expander_set_child (GTK_EXPANDER (container), NULL);
  else if (GTK_IS_OVERLAY (container))
    gtk_overlay_set_child (GTK_OVERLAY (container), NULL);
  else if (GTK_IS_REVEALER (container))
    gtk_revealer_set_child (GTK_REVEALER (container), NULL);
  else if (GTK_IS_LIST_BOX (container))
    gtk_list_box_remove (GTK_LIST_BOX (container), child);
  else if (GTK_IS_FLOW_BOX (container))
    gtk_flow_box_remove (GTK_FLOW_BOX (container), child);
  else if (GTK_IS_FIXED (container))
    gtk_fixed_remove (GTK_FIXED (container), child);
  else if (GTK_IS_GRID (container))
    gtk_grid_remove (GTK_GRID (container), child);
  else if (GTK_IS_NOTEBOOK (container))
    gtk_notebook_remove_page (GTK_NOTEBOOK (container),
			      gtk_notebook_page_num (GTK_NOTEBOOK (container),
						     child));
  else
    gtk_widget_unparent (child);
}

void
gimp_container_set_border_width (GtkWidget *widget,
				 guint      width)
{
  GtkWidget *child = NULL;

  g_return_if_fail (GTK_IS_WIDGET (widget));

  if (gimp_is_single_child_container (widget))
    {
      /*  The border goes around the content; remember it for a child
       *  added later.
       */
      g_object_set_data (G_OBJECT (widget), BORDER_KEY,
			 GUINT_TO_POINTER (width));

      if (GTK_IS_WINDOW (widget))
	child = gtk_window_get_child (GTK_WINDOW (widget));
      else if (GTK_IS_FRAME (widget))
	child = gtk_frame_get_child (GTK_FRAME (widget));
      else if (GTK_IS_BUTTON (widget))
	child = gtk_button_get_child (GTK_BUTTON (widget));

      if (child)
	gimp_apply_border (child, width);
    }
  else
    gimp_apply_border (widget, width);
}

void
gimp_misc_set_alignment (GtkWidget *widget,
			 gfloat     xalign,
			 gfloat     yalign)
{
  g_return_if_fail (GTK_IS_WIDGET (widget));

  if (GTK_IS_LABEL (widget))
    {
      gtk_label_set_xalign (GTK_LABEL (widget), xalign);
      gtk_label_set_yalign (GTK_LABEL (widget), yalign);
    }
  else
    {
      gtk_widget_set_halign (widget,
			     xalign < 0.25 ? GTK_ALIGN_START :
			     xalign > 0.75 ? GTK_ALIGN_END : GTK_ALIGN_CENTER);
      gtk_widget_set_valign (widget,
			     yalign < 0.25 ? GTK_ALIGN_START :
			     yalign > 0.75 ? GTK_ALIGN_END : GTK_ALIGN_CENTER);
    }
}

GtkWidget *
gimp_frame_new (const gchar *label)
{
  GtkWidget *frame = gtk_frame_new (label);

  return frame;
}


/*  Dialogs  */

#define DIALOG_VBOX_KEY   "gimp-dialog-vbox"
#define DIALOG_ACTION_KEY "gimp-dialog-action-area"

GtkWidget *
gimp_dialog_new (const gchar *title)
{
  GtkWidget *window;
  GtkWidget *main_vbox;
  GtkWidget *vbox;
  GtkWidget *action_area;

  window = gtk_window_new ();
  if (title)
    gtk_window_set_title (GTK_WINDOW (window), title);

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child (GTK_WINDOW (window), main_vbox);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_vexpand (vbox, TRUE);
  gtk_box_append (GTK_BOX (main_vbox), vbox);

  action_area = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_set_homogeneous (GTK_BOX (action_area), TRUE);
  gtk_widget_set_halign (action_area, GTK_ALIGN_END);
  gimp_apply_border (action_area, 6);
  gtk_box_append (GTK_BOX (main_vbox), action_area);

  g_object_set_data (G_OBJECT (window), DIALOG_VBOX_KEY, vbox);
  g_object_set_data (G_OBJECT (window), DIALOG_ACTION_KEY, action_area);

  return window;
}

GtkWidget *
gimp_dialog_get_vbox (GtkWidget *dialog)
{
  g_return_val_if_fail (GTK_IS_WIDGET (dialog), NULL);

  return g_object_get_data (G_OBJECT (dialog), DIALOG_VBOX_KEY);
}

GtkWidget *
gimp_dialog_get_action_area (GtkWidget *dialog)
{
  g_return_val_if_fail (GTK_IS_WIDGET (dialog), NULL);

  return g_object_get_data (G_OBJECT (dialog), DIALOG_ACTION_KEY);
}

GtkWidget *
gimp_dialog_add_button (GtkWidget   *dialog,
			const gchar *label,
			GCallback    callback,
			gpointer     data,
			gboolean     is_default)
{
  GtkWidget *button;

  button = gtk_button_new_with_mnemonic (label);
  gtk_box_append (GTK_BOX (gimp_dialog_get_action_area (dialog)), button);

  if (callback)
    g_signal_connect (button, "clicked", callback, data);

  if (is_default)
    gtk_window_set_default_widget (GTK_WINDOW (dialog), button);

  return button;
}

void
gimp_widget_show_toplevel (GtkWidget *widget)
{
  g_return_if_fail (GTK_IS_WIDGET (widget));

  if (GTK_IS_WINDOW (widget))
    gtk_window_present (GTK_WINDOW (widget));
  else
    gtk_widget_set_visible (widget, TRUE);
}

void
gimp_widget_destroy (GtkWidget *widget)
{
  GtkWidget *parent;

  g_return_if_fail (GTK_IS_WIDGET (widget));

  if (GTK_IS_WINDOW (widget))
    {
      gtk_window_destroy (GTK_WINDOW (widget));
      return;
    }

  parent = gtk_widget_get_parent (widget);
  if (parent)
    gimp_container_remove (parent, widget);
}


/*  Radio buttons  */

GtkWidget *
gimp_radio_button_new (GtkWidget   *group,
		       const gchar *label)
{
  GtkWidget *button;

  button = label ? gtk_check_button_new_with_label (label)
		 : gtk_check_button_new ();

  if (group)
    gtk_check_button_set_group (GTK_CHECK_BUTTON (button),
				GTK_CHECK_BUTTON (group));
  else
    gtk_check_button_set_active (GTK_CHECK_BUTTON (button), TRUE);

  return button;
}


/*  Option menus  */

typedef struct
{
  GCallback  callback;
  gpointer   data;
  gboolean   sensitive;
} OptionItem;

typedef struct
{
  GtkStringList *strings;
  GArray        *items;
  guint          last;
  gboolean       setting;
} OptionMenu;

#define OPTION_MENU_KEY "gimp-option-menu"

static void
option_menu_free (gpointer data)
{
  OptionMenu *om = data;

  g_array_free (om->items, TRUE);
  g_free (om);
}

static void
option_menu_selected (GtkDropDown *dropdown,
		      GParamSpec  *pspec,
		      OptionMenu  *om)
{
  guint       selected = gtk_drop_down_get_selected (dropdown);
  OptionItem *item;

  if (om->setting || selected == GTK_INVALID_LIST_POSITION ||
      selected >= om->items->len)
    return;

  item = &g_array_index (om->items, OptionItem, selected);

  if (!item->sensitive)
    {
      om->setting = TRUE;
      gtk_drop_down_set_selected (dropdown, om->last);
      om->setting = FALSE;
      return;
    }

  om->last = selected;

  if (item->callback)
    ((GimpOptionMenuCallback) item->callback) (GTK_WIDGET (dropdown),
					       item->data);
}

GtkWidget *
gimp_option_menu_new (void)
{
  OptionMenu *om = g_new0 (OptionMenu, 1);
  GtkWidget  *dropdown;

  om->strings = gtk_string_list_new (NULL);
  om->items   = g_array_new (FALSE, TRUE, sizeof (OptionItem));

  dropdown = gtk_drop_down_new (G_LIST_MODEL (om->strings), NULL);
  g_object_set_data_full (G_OBJECT (dropdown), OPTION_MENU_KEY,
			  om, option_menu_free);
  g_signal_connect (dropdown, "notify::selected",
		    G_CALLBACK (option_menu_selected), om);

  return dropdown;
}

void
gimp_option_menu_append (GtkWidget   *option_menu,
			 const gchar *label,
			 GCallback    callback,
			 gpointer     data)
{
  OptionMenu *om = g_object_get_data (G_OBJECT (option_menu),
				      OPTION_MENU_KEY);
  OptionItem  item;

  g_return_if_fail (om != NULL);

  item.callback  = callback;
  item.data      = data;
  item.sensitive = TRUE;
  g_array_append_val (om->items, item);

  om->setting = TRUE;
  gtk_string_list_append (om->strings, label);
  if (om->items->len == 1)
    {
      gtk_drop_down_set_selected (GTK_DROP_DOWN (option_menu), 0);
      om->last = 0;
    }
  om->setting = FALSE;
}

void
gimp_option_menu_clear (GtkWidget *option_menu)
{
  OptionMenu *om = g_object_get_data (G_OBJECT (option_menu),
				      OPTION_MENU_KEY);

  g_return_if_fail (om != NULL);

  om->setting = TRUE;
  gtk_string_list_splice (om->strings, 0,
			  g_list_model_get_n_items (G_LIST_MODEL (om->strings)),
			  NULL);
  g_array_set_size (om->items, 0);
  om->last = 0;
  om->setting = FALSE;
}

void
gimp_option_menu_set_history (GtkWidget *option_menu,
			      gint       index)
{
  OptionMenu *om = g_object_get_data (G_OBJECT (option_menu),
				      OPTION_MENU_KEY);

  g_return_if_fail (om != NULL);

  if (index < 0 || (guint) index >= om->items->len)
    return;

  om->setting = TRUE;
  gtk_drop_down_set_selected (GTK_DROP_DOWN (option_menu), index);
  om->last = index;
  om->setting = FALSE;
}

gint
gimp_option_menu_get_history (GtkWidget *option_menu)
{
  guint selected = gtk_drop_down_get_selected (GTK_DROP_DOWN (option_menu));

  return (selected == GTK_INVALID_LIST_POSITION) ? -1 : (gint) selected;
}

gpointer
gimp_option_menu_get_item_data (GtkWidget *option_menu,
				gint       index)
{
  OptionMenu *om = g_object_get_data (G_OBJECT (option_menu),
				      OPTION_MENU_KEY);

  g_return_val_if_fail (om != NULL, NULL);

  if (index < 0 || (guint) index >= om->items->len)
    return NULL;

  return g_array_index (om->items, OptionItem, index).data;
}

void
gimp_option_menu_set_item_sensitive (GtkWidget *option_menu,
				     gint       index,
				     gboolean   sensitive)
{
  OptionMenu *om = g_object_get_data (G_OBJECT (option_menu),
				      OPTION_MENU_KEY);

  g_return_if_fail (om != NULL);

  if (index < 0 || (guint) index >= om->items->len)
    return;

  g_array_index (om->items, OptionItem, index).sensitive = sensitive;
}


/*  Scales  */

GtkWidget *
gimp_hscale_new (GtkAdjustment *adjustment,
		 gint           digits)
{
  GtkWidget *scale;

  scale = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, adjustment);
  gtk_scale_set_draw_value (GTK_SCALE (scale), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE (scale), GTK_POS_TOP);
  gtk_scale_set_digits (GTK_SCALE (scale), digits);

  return scale;
}


/*  Message boxes  */

static void
message_box_ok (GtkWidget *button,
		GtkWidget *window)
{
  gtk_window_destroy (GTK_WINDOW (window));
}

void
gimp_message_box_show (GtkWindow   *parent,
		       const gchar *title,
		       const gchar *message)
{
  GtkWidget *window;
  GtkWidget *label;
  GtkWidget *button;

  window = gimp_dialog_new (title ? title : "GIMP Message");
  if (parent)
    gtk_window_set_transient_for (GTK_WINDOW (window), parent);
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);

  label = gtk_label_new (message);
  gtk_label_set_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 60);
  gtk_label_set_selectable (GTK_LABEL (label), TRUE);
  gimp_apply_border (label, 12);
  gtk_box_append (GTK_BOX (gimp_dialog_get_vbox (window)), label);

  button = gimp_dialog_add_button (window, "_OK",
				   G_CALLBACK (message_box_ok), window, TRUE);
  gtk_widget_grab_focus (button);

  gtk_window_present (GTK_WINDOW (window));
}


/*  File dialogs  */

typedef struct
{
  GimpFileCallback callback;
  gpointer         data;
  gboolean         save;
} FileRequest;

static void
file_dialog_done (GObject      *source,
		  GAsyncResult *result,
		  gpointer      user_data)
{
  FileRequest *request = user_data;
  GFile       *file;
  gchar       *path = NULL;

  if (request->save)
    file = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (source), result, NULL);
  else
    file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source), result, NULL);

  if (file)
    {
      path = g_file_get_path (file);
      g_object_unref (file);
    }

  request->callback (path, request->data);

  g_free (path);
  g_free (request);
}

static void
gimp_file_dialog_run (GtkWindow        *parent,
		      const gchar      *title,
		      const gchar      *initial,
		      GimpFileCallback  callback,
		      gpointer          data,
		      gboolean          save)
{
  GtkFileDialog *dialog;
  FileRequest   *request;

  dialog = gtk_file_dialog_new ();
  if (title)
    gtk_file_dialog_set_title (dialog, title);

  if (initial && *initial)
    {
      GFile *file = g_file_new_for_path (initial);

      if (g_file_test (initial, G_FILE_TEST_IS_DIR))
	gtk_file_dialog_set_initial_folder (dialog, file);
      else
	gtk_file_dialog_set_initial_file (dialog, file);

      g_object_unref (file);
    }

  request = g_new0 (FileRequest, 1);
  request->callback = callback;
  request->data     = data;
  request->save     = save;

  if (save)
    gtk_file_dialog_save (dialog, parent, NULL, file_dialog_done, request);
  else
    gtk_file_dialog_open (dialog, parent, NULL, file_dialog_done, request);

  g_object_unref (dialog);
}

void
gimp_file_dialog_open (GtkWindow        *parent,
		       const gchar      *title,
		       const gchar      *initial,
		       GimpFileCallback  callback,
		       gpointer          data)
{
  gimp_file_dialog_run (parent, title, initial, callback, data, FALSE);
}

void
gimp_file_dialog_save (GtkWindow        *parent,
		       const gchar      *title,
		       const gchar      *initial,
		       GimpFileCallback  callback,
		       gpointer          data)
{
  gimp_file_dialog_run (parent, title, initial, callback, data, TRUE);
}


/*  Color dialogs  */

typedef struct
{
  GimpColorCallback callback;
  gpointer          data;
} ColorRequest;

static void
color_dialog_done (GObject      *source,
		   GAsyncResult *result,
		   gpointer      user_data)
{
  ColorRequest *request = user_data;
  GdkRGBA      *rgba;

  rgba = gtk_color_dialog_choose_rgba_finish (GTK_COLOR_DIALOG (source),
					      result, NULL);
  if (rgba)
    {
      guchar rgb[3];

      rgb[0] = CLAMP (rgba->red   * 255.0 + 0.5, 0, 255);
      rgb[1] = CLAMP (rgba->green * 255.0 + 0.5, 0, 255);
      rgb[2] = CLAMP (rgba->blue  * 255.0 + 0.5, 0, 255);

      request->callback (rgb, request->data);
      gdk_rgba_free (rgba);
    }

  g_free (request);
}

void
gimp_color_dialog_run (GtkWindow         *parent,
		       const gchar       *title,
		       const guchar      *initial_rgb,
		       GimpColorCallback  callback,
		       gpointer           data)
{
  GtkColorDialog *dialog;
  ColorRequest   *request;
  GdkRGBA         rgba = { 0.0, 0.0, 0.0, 1.0 };

  if (initial_rgb)
    {
      rgba.red   = initial_rgb[0] / 255.0;
      rgba.green = initial_rgb[1] / 255.0;
      rgba.blue  = initial_rgb[2] / 255.0;
    }

  dialog = gtk_color_dialog_new ();
  gtk_color_dialog_set_with_alpha (dialog, FALSE);
  if (title)
    gtk_color_dialog_set_title (dialog, title);

  request = g_new0 (ColorRequest, 1);
  request->callback = callback;
  request->data     = data;

  gtk_color_dialog_choose_rgba (dialog, parent, &rgba, NULL,
				color_dialog_done, request);
  g_object_unref (dialog);
}
