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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "appenv.h"
#include "buildmenu.h"
#include "colormaps.h"
#include "color_select.h"
#include "color_area.h"
#include "errors.h"
#include "gdisplay.h"
#include "gimage.h"
#include "gimprc.h"
#include "general.h"
#include "image_render.h"
#include "interface.h"
#include "indexed_palette.h"
#include "palette.h"
#include "undo.h"

#define CELL_WIDTH     20
#define CELL_HEIGHT    20
#define P_AREA_WIDTH   (CELL_WIDTH * 16)
#define P_AREA_HEIGHT  (CELL_HEIGHT * 16)

/*  Add these features:
 *
 *  load/save colormaps
 *  requantize
 *  add color--by clicking in the checked region
 *  all changes need to flush colormap lookup cache
 */

typedef struct _IndexedPalette IndexedPalette;

struct _IndexedPalette {
  GtkWidget *shell;
  GtkWidget *vbox;
  GtkWidget *palette;
  GtkWidget *image_option_menu;

  /*  state information  */
  int gimage_id;
  int col_index;
};

/*  indexed palette routines  */
static void indexed_palette_draw (void);
static void indexed_palette_clear (void);
static void indexed_palette_update (int);

/*  indexed palette menu callbacks  */
static void indexed_palette_close_callback (GtkWidget *, gpointer);
static void indexed_palette_select_callback (int, int, int, ColorSelectState, void *);

/*  event callback  */
static void indexed_palette_area_pressed (GtkGestureClick *, int, double, double, gpointer);

/*  create image menu  */
static void image_menu_callback (GtkWidget *, gpointer);
static GtkWidget * create_image_menu (GtkWidget *, int *, int *, MenuItemCallback);

/*  Only one indexed palette  */
static IndexedPalette *indexedP = NULL;

/*  Color select dialog  */
static ColorSelectP color_select = NULL;
static int color_select_active = 0;

static MenuItem indexed_color_ops[] =
{
  { "Close", 'W', GDK_CONTROL_MASK,
    indexed_palette_close_callback, NULL, NULL, NULL },
  { NULL, 0, 0, NULL, NULL, NULL, NULL },
};


/**************************************/
/*  Public indexed palette functions  */
/**************************************/

static void
indexed_palette_ops_clicked (GtkWidget *button,
			     gpointer   data)
{
  MenuItem  *item = data;
  GtkWidget *popover;

  popover = gtk_widget_get_ancestor (button, GTK_TYPE_POPOVER);
  if (popover)
    gtk_popover_popdown (GTK_POPOVER (popover));

  if (item->callback)
    (* item->callback) (button, item->user_data);
}

static gboolean
indexed_palette_ops_shortcut (GtkWidget *widget,
			      GVariant  *args,
			      gpointer   data)
{
  MenuItem *item = data;

  if (item->callback)
    (* item->callback) (item->widget, item->user_data);

  return TRUE;
}

static gboolean
indexed_palette_close_request (GtkWindow *window,
			       gpointer   data)
{
  gtk_widget_set_visible (GTK_WIDGET (window), FALSE);

  return TRUE;
}

void
indexed_palette_create (int gimage_id)
{
  GtkWidget *vbox;
  GtkWidget *frame;
  GtkWidget *util_box;
  GtkWidget *label;
  GtkWidget *ops_button;
  GtkWidget *ops_popover;
  GtkWidget *ops_box;
  GtkWidget *button;
  GtkWidget *hbox;
  GtkEventController *controller;
  GtkGesture *gesture;
  int default_index;
  int i;

  if (!indexedP)
    {
      indexedP = g_malloc (sizeof (IndexedPalette));
      indexedP->gimage_id = -1;

      /*  The shell and main vbox  */
      indexedP->shell = gimp_dialog_new ("Indexed Color Palette");
      gtk_window_set_resizable (GTK_WINDOW (indexedP->shell), FALSE);
      g_signal_connect (indexedP->shell, "close-request",
			G_CALLBACK (indexed_palette_close_request),
			NULL);

      indexedP->vbox = vbox = gimp_vbox_new (FALSE, 1);
      gimp_container_set_border_width (vbox, 1);
      gimp_box_pack_start (gimp_dialog_get_vbox (indexedP->shell), vbox, TRUE, TRUE, 0);

      /*  The hbox to hold the command menu and image option menu box  */
      util_box = gimp_hbox_new (FALSE, 1);
      gimp_box_pack_start (vbox, util_box, FALSE, FALSE, 0);

      /*  The GIMP image option menu  */
      label = gtk_label_new ("Image:");
      gimp_box_pack_start (util_box, label, FALSE, FALSE, 2);
      indexedP->image_option_menu = create_image_menu (NULL, &gimage_id, &default_index,
						       image_menu_callback);
      gimp_box_pack_start (util_box, indexedP->image_option_menu, TRUE, TRUE, 2);
      if (default_index != -1)
	gimp_option_menu_set_history (indexedP->image_option_menu, default_index);

      /*  The indexed palette commands pulldown menu, and its
       *  accelerators, which work in the whole window
       */
      ops_popover = gtk_popover_new ();
      gtk_widget_add_css_class (ops_popover, "menu");
      ops_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
      gtk_popover_set_child (GTK_POPOVER (ops_popover), ops_box);

      controller = gtk_shortcut_controller_new ();
      gtk_shortcut_controller_set_scope (GTK_SHORTCUT_CONTROLLER (controller),
					 GTK_SHORTCUT_SCOPE_GLOBAL);

      for (i = 0; indexed_color_ops[i].label; i++)
	{
	  button = gtk_button_new ();
	  gtk_button_set_has_frame (GTK_BUTTON (button), FALSE);
	  label = gtk_label_new (indexed_color_ops[i].label);
	  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	  gtk_button_set_child (GTK_BUTTON (button), label);
	  gtk_box_append (GTK_BOX (ops_box), button);
	  g_signal_connect (button, "clicked",
			    G_CALLBACK (indexed_palette_ops_clicked),
			    &indexed_color_ops[i]);
	  indexed_color_ops[i].widget = button;
	  indexed_color_ops[i].index = i;

	  if (indexed_color_ops[i].accelerator_key)
	    gtk_shortcut_controller_add_shortcut
	      (GTK_SHORTCUT_CONTROLLER (controller),
	       gtk_shortcut_new (gtk_keyval_trigger_new
				 (gdk_unicode_to_keyval (g_ascii_tolower (indexed_color_ops[i].accelerator_key)),
				  (GdkModifierType) indexed_color_ops[i].accelerator_mods),
				 gtk_callback_action_new (indexed_palette_ops_shortcut,
							  &indexed_color_ops[i], NULL)));
	}

      gtk_widget_add_controller (indexedP->shell, controller);

      ops_button = gtk_menu_button_new ();
      gtk_menu_button_set_label (GTK_MENU_BUTTON (ops_button), "Ops");
      gtk_menu_button_set_always_show_arrow (GTK_MENU_BUTTON (ops_button), TRUE);
      gtk_menu_button_set_popover (GTK_MENU_BUTTON (ops_button), ops_popover);
      gimp_box_pack_start (util_box, ops_button, FALSE, FALSE, 2);

      /*  The palette frame  */
      frame = gtk_frame_new (NULL);
      gimp_box_pack_start (vbox, frame, TRUE, TRUE, 2);
      indexedP->palette = gimp_preview_new (GIMP_PREVIEW_COLOR);
      gimp_preview_size (GIMP_PREVIEW (indexedP->palette), P_AREA_WIDTH, P_AREA_HEIGHT);
      gtk_widget_set_halign (indexedP->palette, GTK_ALIGN_CENTER);
      gtk_widget_set_valign (indexedP->palette, GTK_ALIGN_CENTER);
      gesture = gtk_gesture_click_new ();
      gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 0);
      g_signal_connect (gesture, "pressed",
			G_CALLBACK (indexed_palette_area_pressed),
			NULL);
      gtk_widget_add_controller (indexedP->palette, GTK_EVENT_CONTROLLER (gesture));
      gtk_frame_set_child (GTK_FRAME (frame), indexedP->palette);

      /* some helpful hints */
      hbox = gimp_hbox_new (FALSE, 1);
      gimp_box_pack_start (vbox, hbox, TRUE, TRUE, 1);
      label = gtk_label_new (" Click to select color.  Right-click to edit color");
      gimp_box_pack_start (hbox, label, FALSE, FALSE, 1);

      /*  The action area  */
      gimp_dialog_add_button (indexedP->shell, "Close",
			      G_CALLBACK (indexed_palette_close_callback),
			      indexedP, TRUE);

      gtk_window_present (GTK_WINDOW (indexedP->shell));

      indexed_palette_update (gimage_id);
      indexed_palette_update_image_list ();
    }
  else
    {
      if (!gtk_widget_get_visible (indexedP->shell))
	gtk_window_present (GTK_WINDOW (indexedP->shell));

      indexed_palette_update (gimage_id);
      indexed_palette_update_image_list ();
    }
}

void
indexed_palette_update_image_list ()
{
  int default_index;
  int default_id;

  if (! indexedP)
    return;

  default_id = indexedP->gimage_id;
  create_image_menu (indexedP->image_option_menu, &default_id, &default_index,
		     image_menu_callback);

  if (default_index != -1)
    {
      if (! gtk_widget_is_sensitive (indexedP->vbox))
	gtk_widget_set_sensitive (indexedP->vbox, TRUE);
      gimp_option_menu_set_history (indexedP->image_option_menu, default_index);

      indexed_palette_update (default_id);
    }
  else
    {
      if (gtk_widget_is_sensitive (indexedP->vbox))
	{
	  gtk_widget_set_sensitive (indexedP->vbox, FALSE);
	  indexed_palette_clear ();
	}
    }
}

static void
indexed_palette_draw ()
{
  GImage *gimage;
  int i, j, k, l, b;
  int col;
  guchar row[P_AREA_WIDTH * 3];

  if (!indexedP)
    return;
  if ((gimage = gimage_get_ID (indexedP->gimage_id)) == NULL)
    return;

  col = 0;
  for (i = 0; i < 16; i++)
    {
      for (j = 0; j < 16 && col < gimage->num_cols; j++, col++)
	{
	  for (k = 0; k < CELL_WIDTH; k++)
	    for (b = 0; b < 3; b++)
	      row[(j * CELL_WIDTH + k) * 3 + b] = gimage->cmap[col * 3 + b];
	}

      for (k = 0; k < CELL_HEIGHT; k++)
	{
	  for (l = j * CELL_WIDTH; l < 16 * CELL_WIDTH; l++)
	    for (b = 0; b < 3; b++)
	      row[l * 3 + b] = ((((i * CELL_HEIGHT + k) & 0x4) ? (l) : (l + 0x4)) & 0x4) ?
		blend_light_check[0] : blend_dark_check[0];

	  gimp_preview_draw_row (GIMP_PREVIEW (indexedP->palette), row, 0,
				 i * CELL_HEIGHT + k, P_AREA_WIDTH);
	}
    }
}

static void
indexed_palette_clear ()
{
  int i, j;
  int offset;
  guchar row[P_AREA_WIDTH * 3];

  if (!indexedP)
    return;

  for (i = 0; i < P_AREA_HEIGHT; i += 4)
    {
      offset = (i & 0x4) ? 0x4 : 0x0;

      for (j = 0; j < P_AREA_WIDTH; j++)
	{
	  row[j * 3 + 0] = row[j * 3 + 1] = row[j * 3 + 2] =
	    ((j + offset) & 0x4) ? blend_light_check[0] : blend_dark_check[0];
	}

      for (j = 0; j < 4; j++)
	gimp_preview_draw_row (GIMP_PREVIEW (indexedP->palette), row, 0, i + j, P_AREA_WIDTH);
    }
}

static void
indexed_palette_update (int gimage_id)
{
  GImage *gimage;

  if (!indexedP)
    return;
  if ((gimage = gimage_get_ID (gimage_id)) == NULL)
    return;

  if (gimage_base_type (gimage) == INDEXED)
    {
      indexedP->gimage_id = gimage_id;
      indexed_palette_draw ();
    }
}

static void
indexed_palette_close_callback (GtkWidget *w,
				gpointer   client_data)
{
  if (!indexedP)
    return;

  gtk_widget_set_visible (indexedP->shell, FALSE);
}

static void
indexed_palette_select_callback (int   r,
				 int   g,
				 int   b,
				 ColorSelectState state,
				 void *client_data)
{
  GImage *gimage;

  if (!indexedP)
    return;

  if ((gimage = gimage_get_ID (indexedP->gimage_id)) == NULL)
    return;

  if (color_select )
    {
      switch (state) {
      case COLOR_SELECT_UPDATE:
	break;
      case COLOR_SELECT_OK:
	gimage->cmap[indexedP->col_index * 3 + 0] = r;
	gimage->cmap[indexedP->col_index * 3 + 1] = g;
	gimage->cmap[indexedP->col_index * 3 + 2] = b;

	gdisplays_update_full (gimage->ID);
	indexed_palette_draw ();
	/* Fallthrough */
      case COLOR_SELECT_CANCEL:
	color_select_hide (color_select);
	color_select_active = FALSE;
      }
    }
}

static void
indexed_palette_area_pressed (GtkGestureClick *gesture,
			      int              n_press,
			      double           x,
			      double           y,
			      gpointer         data)
{
  GImage *gimage;
  guint button;
  guchar r, g, b;

  if (!indexedP)
    return;

  if ((gimage = gimage_get_ID (indexedP->gimage_id)) == NULL)
    return;

  if (x < 0 || y < 0 || x >= P_AREA_WIDTH || y >= P_AREA_HEIGHT)
    return;

  button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));

  if (button == 1)
    {
      indexedP->col_index = 16 * ((int) y / CELL_HEIGHT) + ((int) x / CELL_WIDTH);
      r = gimage->cmap[indexedP->col_index * 3 + 0];
      g = gimage->cmap[indexedP->col_index * 3 + 1];
      b = gimage->cmap[indexedP->col_index * 3 + 2];
      if (active_color == FOREGROUND)
	palette_set_foreground (r, g, b);
      else if (active_color == BACKGROUND)
	palette_set_background (r, g, b);
    }

  if (button == 3)
    {
      indexedP->col_index = 16 * ((int) y / CELL_HEIGHT) + ((int) x / CELL_WIDTH);
      r = gimage->cmap[indexedP->col_index * 3 + 0];
      g = gimage->cmap[indexedP->col_index * 3 + 1];
      b = gimage->cmap[indexedP->col_index * 3 + 2];

      if (! color_select)
	{
	  color_select = color_select_new (r, g, b, indexed_palette_select_callback, NULL, FALSE);
	  color_select_active = 1;
	}
      else
	{
	  if (! color_select_active)
	    color_select_show (color_select);
	  color_select_set_color (color_select, r, g, b, 1);
	}
    }
}

static void
image_menu_callback (GtkWidget *w,
		     gpointer   client_data)
{
  if (!indexedP)
    return;
  if (gimage_get_ID (GPOINTER_TO_INT (client_data)) != NULL)
    {
      indexed_palette_update (GPOINTER_TO_INT (client_data));
    }
}

static GtkWidget *
create_image_menu (GtkWidget        *option_menu,
		   int              *default_id,
		   int              *default_index,
		   MenuItemCallback  callback)
{
  extern GSList *image_list;

  GImage *gimage;
  char *menu_item_label;
  char *image_name;
  GSList *tmp;
  int num_items = 0;
  int id;

  id = -1;

  *default_index = -1;

  if (option_menu)
    gimp_option_menu_clear (option_menu);
  else
    option_menu = gimp_option_menu_new ();

  tmp = image_list;
  while (tmp)
    {
      gimage = tmp->data;
      tmp = g_slist_next (tmp);

      if (gimage_base_type (gimage) == INDEXED)
	{
	  id = -1;

	  /*  make sure the default index gets set to _something_, if possible  */
	  if (*default_index == -1)
	    {
	      id = gimage->ID;
	      *default_index = num_items;
	    }

	  if (gimage->ID == *default_id)
	    {
	      id = *default_id;
	      *default_index = num_items;
	    }

	  image_name = prune_filename (gimage_filename (gimage));
	  menu_item_label = g_strdup_printf ("%s-%d", image_name, gimage->ID);
	  gimp_option_menu_append (option_menu, menu_item_label,
				   G_CALLBACK (callback),
				   GINT_TO_POINTER (gimage->ID));
	  g_free (menu_item_label);
	  num_items ++;
	}
    }

  if (!num_items)
    gimp_option_menu_append (option_menu, "none", NULL, NULL);

  *default_id = id;

  return option_menu;
}
