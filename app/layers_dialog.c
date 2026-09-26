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
#include "drawable.h"
#include "errors.h"
#include "floating_sel.h"
#include "gdisplay.h"
#include "gimage.h"
#include "gimage_mask.h"
#include "gimprc.h"
#include "general.h"
#include "image_render.h"
#include "interface.h"
#include "layers_dialog.h"
#include "layers_dialogP.h"
#include "ops_buttons.h"
#include "paint_funcs.h"
#include "palette.h"
#include "resize.h"
#include "undo.h"

#include "tools/eye.xbm"
#include "tools/linked.xbm"
#include "tools/layer.xbm"
#include "tools/mask.xbm"

#include "tools/new.xpm"
#include "tools/new_is.xpm"
#include "tools/raise.xpm"
#include "tools/raise_is.xpm"
#include "tools/lower.xpm"
#include "tools/lower_is.xpm"
#include "tools/duplicate.xpm"
#include "tools/duplicate_is.xpm"
#include "tools/delete.xpm"
#include "tools/delete_is.xpm"
#include "tools/anchor.xpm"
#include "tools/anchor_is.xpm"

#include "layer_pvt.h"


#define LAYER_LIST_WIDTH 200
#define LAYER_LIST_HEIGHT 150

#define LAYER_PREVIEW 0
#define MASK_PREVIEW  1
#define FS_PREVIEW 2

/*  The list rows keep their selection in the SELECTED state flag; the
 *  list itself does no selecting, so the GTK 1 select/deselect rules
 *  of the layers and channels lists can be kept exactly.
 */
#define LC_ROW_SELECTED(w) \
  ((gtk_widget_get_state_flags (w) & GTK_STATE_FLAG_SELECTED) != 0)

#define LAYER_WIDGET_KEY "gimp-layer-widget"

typedef struct _LayersDialog LayersDialog;

struct _LayersDialog {
  GtkWidget *vbox;
  GtkWidget *mode_option_menu;
  GtkWidget *layer_list;
  GtkWidget *preserve_trans;
  GtkWidget *mode_box;
  GtkWidget *opacity_box;
  GtkWidget *ops_menu;
  GtkAdjustment *opacity_data;
  GtkWidget *layer_preview;
  double ratio;
  int image_width, image_height;
  int gimage_width, gimage_height;

  /*  state information  */
  int gimage_id;
  Layer * active_layer;
  Channel * active_channel;
  Layer * floating_sel;
  GSList * layer_widgets;
};

typedef struct _LayerWidget LayerWidget;

struct _LayerWidget {
  GtkWidget *eye_widget;
  GtkWidget *linked_widget;
  GtkWidget *clip_widget;
  GtkWidget *layer_preview;
  GtkWidget *mask_preview;
  GtkWidget *list_item;
  GtkWidget *label;

  GImage *gimage;
  Layer *layer;
  cairo_surface_t *layer_pixmap;
  cairo_surface_t *mask_pixmap;
  int active_preview;
  int width, height;

  /*  state information  */
  int layer_mask;
  int apply_mask;
  int edit_mask;
  int show_mask;
  int visited;
};

/*  layers dialog widget routines  */
static void layers_dialog_preview_extents (void);
static void layers_dialog_set_menu_sensitivity (void);
static void layers_dialog_set_active_layer (Layer *);
static void layers_dialog_unset_layer (Layer *);
static void layers_dialog_position_layer (Layer *, int);
static void layers_dialog_add_layer (Layer *);
static void layers_dialog_remove_layer (Layer *);
static void layers_dialog_add_layer_mask (Layer *);
static void layers_dialog_remove_layer_mask (Layer *);
static void layers_list_clear (void);
static void paint_mode_menu_callback (GtkWidget *, gpointer);
static void image_menu_callback (GtkWidget *, gpointer);
static void opacity_scale_update (GtkAdjustment *, gpointer);
static void preserve_trans_update (GtkWidget *, gpointer);

/*  layers dialog menu callbacks  */
static void layers_dialog_new_layer_callback (GtkWidget *, gpointer);
static void layers_dialog_raise_layer_callback (GtkWidget *, gpointer);
static void layers_dialog_lower_layer_callback (GtkWidget *, gpointer);
static void layers_dialog_duplicate_layer_callback (GtkWidget *, gpointer);
static void layers_dialog_delete_layer_callback (GtkWidget *, gpointer);
static void layers_dialog_scale_layer_callback (GtkWidget *, gpointer);
static void layers_dialog_resize_layer_callback (GtkWidget *, gpointer);
static void layers_dialog_add_layer_mask_callback (GtkWidget *, gpointer);
static void layers_dialog_apply_layer_mask_callback (GtkWidget *, gpointer);
static void layers_dialog_anchor_layer_callback (GtkWidget *, gpointer);
static void layers_dialog_merge_layers_callback (GtkWidget *, gpointer);
static void layers_dialog_flatten_image_callback (GtkWidget *, gpointer);
static void layers_dialog_alpha_select_callback (GtkWidget *, gpointer);
static void layers_dialog_mask_select_callback (GtkWidget *, gpointer);
static void layers_dialog_add_alpha_channel_callback (GtkWidget *, gpointer);
static void lc_dialog_close_callback (GtkWidget *, gpointer);
static gboolean lc_dialog_close_request (GtkWindow *, gpointer);
static void lc_dialog_destroy_callback (GtkWidget *, gpointer);

/*  layer widget function prototypes  */
static LayerWidget *layer_widget_get_ID (Layer *);
static LayerWidget *layer_widget_from (GtkWidget *);
static LayerWidget *create_layer_widget (GImage *, Layer *);
static void layer_widget_delete (LayerWidget *);
static void layer_widget_select_update (LayerWidget *);
static void layer_widget_row_pressed (GtkGestureClick *, int, double, double, gpointer);
static void layer_widget_button_begin (GtkGestureDrag *, double, double, gpointer);
static void layer_widget_button_update (GtkGestureDrag *, double, double, gpointer);
static void layer_widget_button_end (GtkGestureDrag *, double, double, gpointer);
static void layer_widget_preview_pressed (GtkGestureClick *, int, double, double, gpointer);
static void layer_widget_preview_draw (GtkDrawingArea *, cairo_t *, int, int, gpointer);
static void layer_widget_eye_draw (GtkDrawingArea *, cairo_t *, int, int, gpointer);
static void layer_widget_linked_draw (GtkDrawingArea *, cairo_t *, int, int, gpointer);
static void layer_widget_clip_draw (GtkDrawingArea *, cairo_t *, int, int, gpointer);
static void layer_widget_boundary_redraw (LayerWidget *, int, cairo_t *);
static void layer_widget_preview_redraw (LayerWidget *, int);
static void layer_widget_no_preview_redraw (LayerWidget *, int, cairo_t *);
static void layer_widget_eye_redraw (LayerWidget *);
static void layer_widget_linked_redraw (LayerWidget *);
static void layer_widget_exclusive_visible (LayerWidget *);
static void layer_widget_layer_flush (GtkWidget *, gpointer);

/*  assorted query dialogs  */
static void layers_dialog_new_layer_query (int);
static void layers_dialog_edit_layer_query (LayerWidget *);
static void layers_dialog_add_mask_query (Layer *);
static void layers_dialog_apply_mask_query (Layer *);
static void layers_dialog_scale_layer_query (Layer *);
static void layers_dialog_resize_layer_query (Layer *);
void        layers_dialog_layer_merge_query (GImage *, int);


/*
 *  Shared data
 */

GtkWidget *lc_shell = NULL;
GtkWidget *lc_subshell = NULL;

/*
 *  Local data
 */
static LayersDialog *layersD = NULL;
static GtkWidget *image_option_menu;

static int suspend_gimage_notify = 0;

static MenuItem layers_ops[] =
{
  { "New Layer", 'N', GDK_CONTROL_MASK,
    layers_dialog_new_layer_callback, NULL, NULL, NULL },
  { "Raise Layer", 'F', GDK_CONTROL_MASK,
    layers_dialog_raise_layer_callback, NULL, NULL, NULL },
  { "Lower Layer", 'B', GDK_CONTROL_MASK,
    layers_dialog_lower_layer_callback, NULL, NULL, NULL },
  { "Duplicate Layer", 'C', GDK_CONTROL_MASK,
    layers_dialog_duplicate_layer_callback, NULL, NULL, NULL },
  { "Delete Layer", 'X', GDK_CONTROL_MASK,
    layers_dialog_delete_layer_callback, NULL, NULL, NULL },
  { "Scale Layer", 'S', GDK_CONTROL_MASK,
    layers_dialog_scale_layer_callback, NULL, NULL, NULL },
  { "Resize Layer", 'R', GDK_CONTROL_MASK,
    layers_dialog_resize_layer_callback, NULL, NULL, NULL },
  { "Add Layer Mask", 0, 0,
    layers_dialog_add_layer_mask_callback, NULL, NULL, NULL },
  { "Apply Layer Mask", 0, 0,
    layers_dialog_apply_layer_mask_callback, NULL, NULL, NULL },
  { "Anchor Layer", 'H', GDK_CONTROL_MASK,
    layers_dialog_anchor_layer_callback, NULL, NULL, NULL },
  { "Merge Visible Layers", 'M', GDK_CONTROL_MASK,
    layers_dialog_merge_layers_callback, NULL, NULL, NULL },
  { "Flatten Image", 0, 0,
    layers_dialog_flatten_image_callback, NULL, NULL, NULL },
  { "Alpha To Selection", 0, 0,
    layers_dialog_alpha_select_callback, NULL, NULL, NULL },
  { "Mask To Selection", 0, 0,
    layers_dialog_mask_select_callback, NULL, NULL, NULL },
  { "Add Alpha Channel", 0, 0,
    layers_dialog_add_alpha_channel_callback, NULL, NULL, NULL },
  { NULL, 0, 0, NULL, NULL, NULL, NULL },
};

/*  the option menu items -- the paint modes  */
static MenuItem option_items[] =
{
  { "Normal", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (NORMAL_MODE), NULL, NULL },
  { "Dissolve", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (DISSOLVE_MODE), NULL, NULL },
  { "Multiply", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (MULTIPLY_MODE), NULL, NULL },
  { "Screen", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (SCREEN_MODE), NULL, NULL },
  { "Overlay", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (OVERLAY_MODE), NULL, NULL },
  { "Difference", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (DIFFERENCE_MODE), NULL, NULL },
  { "Addition", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (ADDITION_MODE), NULL, NULL },
  { "Subtract", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (SUBTRACT_MODE), NULL, NULL },
  { "Darken Only", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (DARKEN_ONLY_MODE), NULL, NULL },
  { "Lighten Only", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (LIGHTEN_ONLY_MODE), NULL, NULL },
  { "Hue", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (HUE_MODE), NULL, NULL },
  { "Saturation", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (SATURATION_MODE), NULL, NULL },
  { "Color", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (COLOR_MODE), NULL, NULL },
  { "Value", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (VALUE_MODE), NULL, NULL },
  { NULL, 0, 0, NULL, NULL, NULL, NULL }
};

/* the ops buttons */
static OpsButton layers_ops_buttons[] =
{
  { new_xpm, new_is_xpm, layers_dialog_new_layer_callback, "New Layer", NULL },
  { raise_xpm, raise_is_xpm, layers_dialog_raise_layer_callback, "Raise Layer", NULL },
  { lower_xpm, lower_is_xpm, layers_dialog_lower_layer_callback, "Lower Layer", NULL },
  { duplicate_xpm, duplicate_is_xpm, layers_dialog_duplicate_layer_callback, "Duplicate Layer", NULL },
  { delete_xpm, delete_is_xpm, layers_dialog_delete_layer_callback, "Delete Layer", NULL },
  { anchor_xpm, anchor_is_xpm, layers_dialog_anchor_layer_callback, "Anchor Layer", NULL },
  { NULL, NULL, NULL, NULL, NULL }
};


/*****************************************************/
/*  Helpers shared with the channels dialog          */
/*****************************************************/

#define LC_MENU_ITEM_KEY "gimp-lc-menu-item"

static void
lc_ops_menu_item_clicked (GtkWidget *button,
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
lc_ops_menu_shortcut (GtkWidget *widget,
		      GVariant  *args,
		      gpointer   data)
{
  MenuItem *item = data;

  if (item->widget && !gtk_widget_is_sensitive (item->widget))
    return TRUE;

  if (item->callback)
    (* item->callback) (item->widget, item->user_data);

  return TRUE;
}

/*  The accelerators work in the whole window while the page holding
 *  them is shown, as the GTK 1 accel group added on "map" did.
 */
static void
lc_ops_menu_map (GtkWidget *widget,
		 gpointer   data)
{
  gtk_shortcut_controller_set_scope (GTK_SHORTCUT_CONTROLLER (data),
				     GTK_SHORTCUT_SCOPE_GLOBAL);
}

static void
lc_ops_menu_unmap (GtkWidget *widget,
		   gpointer   data)
{
  gtk_shortcut_controller_set_scope (GTK_SHORTCUT_CONTROLLER (data),
				     GTK_SHORTCUT_SCOPE_LOCAL);
}

GtkWidget *
lc_ops_menu_new (MenuItem  *items,
		 GtkWidget *parent)
{
  GtkWidget *popover;
  GtkWidget *box;
  GtkWidget *button;
  GtkWidget *label;
  GtkEventController *controller;
  GtkShortcutTrigger *trigger;
  GtkShortcutAction *action;
  int i;

  popover = gtk_popover_new ();
  gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);
  gtk_widget_add_css_class (popover, "menu");

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_popover_set_child (GTK_POPOVER (popover), box);

  controller = gtk_shortcut_controller_new ();
  gtk_shortcut_controller_set_scope (GTK_SHORTCUT_CONTROLLER (controller),
				     GTK_SHORTCUT_SCOPE_LOCAL);

  for (i = 0; items[i].label; i++)
    {
      if (items[i].label[0] == '-')
	{
	  gtk_box_append (GTK_BOX (box),
			  gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
	  continue;
	}

      button = gtk_button_new ();
      gtk_button_set_has_frame (GTK_BUTTON (button), FALSE);
      label = gtk_label_new (items[i].label);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_button_set_child (GTK_BUTTON (button), label);
      gtk_box_append (GTK_BOX (box), button);
      g_object_set_data (G_OBJECT (button), LC_MENU_ITEM_KEY, &items[i]);
      g_signal_connect (button, "clicked",
			G_CALLBACK (lc_ops_menu_item_clicked), &items[i]);

      items[i].widget = button;
      items[i].index = i;

      if (items[i].accelerator_key)
	{
	  trigger = gtk_keyval_trigger_new
	    (gdk_unicode_to_keyval (g_ascii_tolower (items[i].accelerator_key)),
	     (GdkModifierType) items[i].accelerator_mods);
	  action = gtk_callback_action_new (lc_ops_menu_shortcut, &items[i], NULL);
	  gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller),
						gtk_shortcut_new (trigger, action));
	}
    }

  gtk_widget_add_controller (parent, controller);
  g_signal_connect (parent, "map", G_CALLBACK (lc_ops_menu_map), controller);
  g_signal_connect (parent, "unmap", G_CALLBACK (lc_ops_menu_unmap), controller);

  gtk_widget_set_parent (popover, parent);

  return popover;
}

void
lc_ops_menu_popup (GtkWidget *menu,
		   GtkWidget *widget,
		   double     x,
		   double     y)
{
  graphene_point_t in;
  graphene_point_t out;
  GdkRectangle rect;
  GtkWidget *parent;

  if (!menu)
    return;

  parent = gtk_widget_get_parent (menu);

  in = GRAPHENE_POINT_INIT ((float) x, (float) y);
  if (!parent || !gtk_widget_compute_point (widget, parent, &in, &out))
    out = in;

  rect.x = (int) out.x;
  rect.y = (int) out.y;
  rect.width = 1;
  rect.height = 1;

  gtk_popover_set_pointing_to (GTK_POPOVER (menu), &rect);
  gtk_popover_popup (GTK_POPOVER (menu));
}

void
lc_draw_bitmap (GtkWidget           *widget,
		cairo_t             *cr,
		const unsigned char *bits,
		int                  width,
		int                  height,
		int                  x,
		int                  y)
{
  GdkRGBA fg;
  int stride;
  int i, j;

  stride = (width + 7) / 8;

  gtk_widget_get_color (widget, &fg);
  gdk_cairo_set_source_rgba (cr, &fg);

  for (i = 0; i < height; i++)
    for (j = 0; j < width; j++)
      if (bits[i * stride + j / 8] & (1 << (j % 8)))
	cairo_rectangle (cr, x + j, y + i, 1, 1);

  cairo_fill (cr);
}


/************************************/
/*  Public layers dialog functions  */
/************************************/

void
lc_dialog_create (int gimage_id)
{
  GtkWidget *util_box;
  GtkWidget *button;
  GtkWidget *label;
  GtkWidget *notebook;
  GtkWidget *separator;
  GtkWidget *vbox;
  int default_index;

  if (lc_shell == NULL)
    {
      lc_shell = gimp_dialog_new ("Layers & Channels");

      vbox = gimp_dialog_get_vbox (lc_shell);
      gimp_container_set_border_width (vbox, 2);
      g_signal_connect (lc_shell, "close-request",
			G_CALLBACK (lc_dialog_close_request),
			NULL);
      g_signal_connect (lc_shell, "destroy",
			G_CALLBACK (lc_dialog_destroy_callback),
			NULL);

      lc_subshell = gimp_vbox_new (FALSE, 2);
      gimp_box_pack_start (vbox, lc_subshell, TRUE, TRUE, 2);

      /*  The hbox to hold the image option menu box  */
      util_box = gimp_hbox_new (FALSE, 1);
      gimp_box_pack_start (lc_subshell, util_box, FALSE, FALSE, 0);

      /*  The GIMP image option menu  */
      label = gtk_label_new ("Image:");
      gimp_box_pack_start (util_box, label, FALSE, FALSE, 2);
      image_option_menu = create_image_menu (NULL, &gimage_id, &default_index,
					     image_menu_callback);
      gimp_box_pack_start (util_box, image_option_menu, TRUE, TRUE, 2);

      if (default_index != -1)
	gimp_option_menu_set_history (image_option_menu, default_index);

      separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
      gimp_box_pack_start (lc_subshell, separator, FALSE, TRUE, 2);

      /*  The notebook widget  */
      notebook = gtk_notebook_new ();
      gimp_box_pack_start (lc_subshell, notebook, TRUE, TRUE, 0);

      label = gtk_label_new ("Layers");
      gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
				layers_dialog_create (),
				label);

      label = gtk_label_new ("Channels");
      gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
				channels_dialog_create (),
				label);

      gimp_container_set_border_width (gimp_dialog_get_action_area (lc_shell), 1);
      /*  The close button  */
      button = gimp_dialog_add_button (lc_shell, "Close", NULL, NULL, FALSE);
      g_signal_connect (button, "clicked",
			G_CALLBACK (lc_dialog_close_callback),
			NULL);

      gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 0);

      layers_dialog_update (gimage_id);
      channels_dialog_update (gimage_id);

      gtk_window_present (GTK_WINDOW (lc_shell));

      gdisplays_flush ();
    }
  else
    {
      /*  shows the dialog, or raises it when it is already shown  */
      gtk_window_present (GTK_WINDOW (lc_shell));

      layers_dialog_update (gimage_id);
      channels_dialog_update (gimage_id);
      lc_dialog_update_image_list ();
      gdisplays_flush ();
    }
}

void
lc_dialog_update_image_list ()
{
  int default_index;
  int default_id;

  if (lc_shell == NULL)
    return;

  default_id = layersD->gimage_id;
  layersD->gimage_id = -1;		/* ??? */
  create_image_menu (image_option_menu, &default_id, &default_index,
		     image_menu_callback);

  if (default_index != -1)
    {
      if (! gtk_widget_is_sensitive (lc_subshell))
	gtk_widget_set_sensitive (lc_subshell, TRUE);
      gimp_option_menu_set_history (image_option_menu, default_index);

      if (default_id != layersD->gimage_id)
	{
	  layers_dialog_update (default_id);
	  channels_dialog_update (default_id);
	  gdisplays_flush ();
	}
    }
  else
    {
      if (gtk_widget_is_sensitive (lc_subshell))
	gtk_widget_set_sensitive (lc_subshell, FALSE);

      layers_dialog_clear ();
      channels_dialog_clear ();
    }
}


void
lc_dialog_free ()
{
  if (lc_shell == NULL)
    return;

  layers_dialog_free ();
  channels_dialog_free ();

  gtk_window_destroy (GTK_WINDOW (lc_shell));
}

void
lc_dialog_rebuild (int new_preview_size)
{
  int gimage_id;
  int flag;

  gimage_id = -1;

  flag = 0;
  if (lc_shell)
    {
      flag = 1;
      gimage_id = layersD->gimage_id;
      lc_dialog_free ();
    }
  preview_size = new_preview_size;
  render_setup (transparency_type, transparency_size);
  if (flag)
    lc_dialog_create (gimage_id);
}


void
layers_dialog_flush ()
{
  GImage *gimage;
  Layer *layer;
  LayerWidget *lw;
  GSList *list;
  GtkWidget *child;
  GtkWidget *next;
  int gimage_pos;
  int pos;

  if (!layersD)
    return;

  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  /*  Check if the gimage extents have changed  */
  if ((gimage->width != layersD->gimage_width) ||
      (gimage->height != layersD->gimage_height))
    {
      layersD->gimage_id = -1;
      layers_dialog_update (gimage->ID);
    }

  /*  Set all current layer widgets to visited = FALSE  */
  list = layersD->layer_widgets;
  while (list)
    {
      lw = (LayerWidget *) list->data;
      lw->visited = FALSE;
      list = g_slist_next (list);
    }

  /*  Add any missing layers  */
  list = gimage->layers;
  while (list)
    {
      layer = (Layer *) list->data;
      lw = layer_widget_get_ID (layer);

      /*  If the layer isn't in the layer widget list, add it  */
      if (lw == NULL)
	layers_dialog_add_layer (layer);
      else
	lw->visited = TRUE;

      list = g_slist_next (list);
    }

  /*  Remove any extraneous layers  */
  list = layersD->layer_widgets;
  while (list)
    {
      lw = (LayerWidget *) list->data;
      list = g_slist_next (list);
      if (lw->visited == FALSE)
	layers_dialog_remove_layer ((lw->layer));
    }

  /*  Switch positions of items if necessary  */
  list = layersD->layer_widgets;
  pos = 0;
  while (list)
    {
      lw = (LayerWidget *) list->data;
      list = g_slist_next (list);

      if ((gimage_pos = gimage_get_layer_index (gimage, lw->layer)) != pos)
	layers_dialog_position_layer ((lw->layer), gimage_pos);

      pos++;
    }

  /*  Set the active layer  */
  if (layersD->active_layer != gimage->active_layer)
    layersD->active_layer = gimage->active_layer;

  /*  Set the active channel  */
  if (layersD->active_channel != gimage->active_channel)
    layersD->active_channel = gimage->active_channel;

  /*  set the menus if floating sel status has changed  */
  if (layersD->floating_sel != gimage->floating_sel)
    layersD->floating_sel = gimage->floating_sel;

  layers_dialog_set_menu_sensitivity ();

  for (child = gtk_widget_get_first_child (layersD->layer_list);
       child;
       child = next)
    {
      next = gtk_widget_get_next_sibling (child);
      layer_widget_layer_flush (child, NULL);
    }
}


void
layers_dialog_free ()
{
  GSList *list;
  LayerWidget *lw;

  if (layersD == NULL)
    return;

  /*  Free all elements in the layers listbox  */
  layers_list_clear ();

  list = layersD->layer_widgets;
  while (list)
    {
      lw = (LayerWidget *) list->data;
      list = g_slist_next(list);
      layer_widget_delete (lw);
    }
  layersD->layer_widgets = NULL;
  layersD->active_layer = NULL;
  layersD->active_channel = NULL;
  layersD->floating_sel = NULL;

  if (layersD->layer_preview)
    g_object_unref (layersD->layer_preview);

  /*  the ops menu goes with the dialog's widgets  */

  g_free (layersD);
  layersD = NULL;
}


/*************************************/
/*  layers dialog widget routines    */
/*************************************/

GtkWidget *
layers_dialog_create ()
{
  GtkWidget *vbox;
  GtkWidget *util_box;
  GtkWidget *button_box;
  GtkWidget *label;
  GtkWidget *slider;
  GtkWidget *listbox;


  if (!layersD)
    {
      layersD = g_malloc (sizeof (LayersDialog));
      layersD->layer_preview = NULL;
      layersD->gimage_id = -1;
      layersD->active_layer = NULL;
      layersD->active_channel = NULL;
      layersD->floating_sel = NULL;
      layersD->layer_widgets = NULL;

      if (preview_size)
	{
	  /*  a scratch buffer for render_preview (), never shown  */
	  layersD->layer_preview = gimp_preview_new (GIMP_PREVIEW_COLOR);
	  g_object_ref_sink (layersD->layer_preview);
	  gimp_preview_size (GIMP_PREVIEW (layersD->layer_preview), preview_size, preview_size);
	}

      /*  The main vbox  */
      layersD->vbox = vbox = gimp_vbox_new (FALSE, 1);
      gimp_container_set_border_width (vbox, 2);

      /*  The layers commands popup menu  */
      layersD->ops_menu = lc_ops_menu_new (layers_ops, vbox);

      /*  The Mode option menu, and the preserve transparency  */
      layersD->mode_box = util_box = gimp_hbox_new (FALSE, 1);
      gimp_box_pack_start (vbox, util_box, FALSE, FALSE, 0);

      label = gtk_label_new ("Mode:");
      gimp_box_pack_start (util_box, label, FALSE, FALSE, 2);

      layersD->mode_option_menu = build_menu (option_items, NULL);
      gimp_box_pack_start (util_box, layersD->mode_option_menu, FALSE, FALSE, 2);

      layersD->preserve_trans = gtk_check_button_new_with_label ("Keep Trans.");
      gimp_box_pack_start (util_box, layersD->preserve_trans, FALSE, FALSE, 2);
      g_signal_connect (layersD->preserve_trans, "toggled",
			G_CALLBACK (preserve_trans_update),
			layersD);


      /*  Opacity scale  */
      layersD->opacity_box = util_box = gimp_hbox_new (FALSE, 1);
      gimp_box_pack_start (vbox, util_box, FALSE, FALSE, 0);
      label = gtk_label_new ("Opacity:");
      gimp_box_pack_start (util_box, label, FALSE, FALSE, 2);
      layersD->opacity_data = gtk_adjustment_new (100.0, 0.0, 100.0, 1.0, 1.0, 0.0);
      slider = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, layersD->opacity_data);
      gtk_scale_set_draw_value (GTK_SCALE (slider), TRUE);
      gtk_scale_set_digits (GTK_SCALE (slider), 1);
      gtk_scale_set_value_pos (GTK_SCALE (slider), GTK_POS_RIGHT);
      gimp_box_pack_start (util_box, slider, TRUE, TRUE, 0);
      g_signal_connect (layersD->opacity_data, "value-changed",
			G_CALLBACK (opacity_scale_update),
			layersD);


      /*  The layers listbox  */
      listbox = gtk_scrolled_window_new ();
      gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (listbox),
				      GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
      gtk_widget_set_size_request (listbox, LAYER_LIST_WIDTH, LAYER_LIST_HEIGHT);
      gimp_box_pack_start (vbox, listbox, TRUE, TRUE, 2);

      layersD->layer_list = gtk_list_box_new ();
      gtk_list_box_set_selection_mode (GTK_LIST_BOX (layersD->layer_list),
				       GTK_SELECTION_NONE);
      gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (listbox), layersD->layer_list);

      /* The ops buttons */

      button_box = ops_button_box_new (lc_shell, layers_ops_buttons);

      gimp_box_pack_start (vbox, button_box, FALSE, FALSE, 2);
    }

  return layersD->vbox;
}


GtkWidget *
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

  if (!num_items)
    gimp_option_menu_append (option_menu, "none", NULL, NULL);

  *default_id = id;

  return option_menu;
}


void
layers_dialog_update (int gimage_id)
{
  GImage *gimage;
  Layer *layer;
  LayerWidget *lw;
  GSList *list;
  int pos;

  if (!layersD)
    return;
  if (layersD->gimage_id == gimage_id)
    return;

  layersD->gimage_id = gimage_id;

  suspend_gimage_notify++;

  /*  Free all elements in the layers listbox  */
  layers_list_clear ();

  list = layersD->layer_widgets;
  while (list)
    {
      lw = (LayerWidget *) list->data;
      list = g_slist_next(list);
      layer_widget_delete (lw);
    }
  if (layersD->layer_widgets)
    g_message ("layersD->layer_widgets not empty!");
  layersD->layer_widgets = NULL;

  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    {
      suspend_gimage_notify--;
      return;
    }

  /*  Find the preview extents  */
  layers_dialog_preview_extents ();

  layersD->active_layer = NULL;
  layersD->active_channel = NULL;
  layersD->floating_sel = NULL;

  list = gimage->layers;
  pos = 0;

  while (list)
    {
      /*  create a layer list item  */
      layer = (Layer *) list->data;
      lw = create_layer_widget (gimage, layer);
      layersD->layer_widgets = g_slist_append (layersD->layer_widgets, lw);
      gtk_list_box_insert (GTK_LIST_BOX (layersD->layer_list), lw->list_item, pos++);

      list = g_slist_next (list);
    }

  suspend_gimage_notify--;
}


void
layers_dialog_clear ()
{
  ops_button_box_set_insensitive (layers_ops_buttons);

  layersD->gimage_id = -1;
  layers_list_clear ();
}


static void
layers_list_clear ()
{
  GtkWidget *child;

  if (!layersD)
    return;

  while ((child = gtk_widget_get_first_child (layersD->layer_list)))
    gtk_list_box_remove (GTK_LIST_BOX (layersD->layer_list), child);
}


void
render_preview (TempBuf   *preview_buf,
		GtkWidget *preview_widget,
		int        width,
		int        height,
		int        channel)
{
  unsigned char *src, *s;
  unsigned char *cb;
  unsigned char *buf;
  int a;
  int i, j, b;
  int x1, y1, x2, y2;
  int rowstride;
  int color_buf;
  int color;
  int alpha;
  int has_alpha;
  int image_bytes;
  int offset;

  alpha = ALPHA_PIX;

  /*  Here are the different cases this functions handles correctly:
   *  1)  Offset preview_buf which does not necessarily cover full image area
   *  2)  Color conversion of preview_buf if it is gray and image is color
   *  3)  Background check buffer for transparent preview_bufs
   *  4)  Using the optional "channel" argument, one channel can be extracted
   *      from a multi-channel preview_buf and composited as a grayscale
   *  Prereqs:
   *  1)  Grayscale preview_bufs have bytes == {1, 2}
   *  2)  Color preview_bufs have bytes == {3, 4}
   *  3)  If image is gray, then preview_buf should have bytes == {1, 2}
   */
  color_buf = (gimp_preview_get_preview_type (GIMP_PREVIEW (preview_widget)) == GIMP_PREVIEW_COLOR);
  image_bytes = (color_buf) ? 3 : 1;
  has_alpha = (preview_buf->bytes == 2 || preview_buf->bytes == 4);
  rowstride = preview_buf->width * preview_buf->bytes;

  /*  Determine if the preview buf supplied is color
   *   Generally, if the bytes == {3, 4}, this is true.
   *   However, if the channel argument supplied is not -1, then
   *   the preview buf is assumed to be gray despite the number of
   *   channels it contains
   */
  color = (preview_buf->bytes == 3 || preview_buf->bytes == 4) && (channel == -1);

  if (has_alpha)
    {
      buf = check_buf;
      alpha = (color) ? ALPHA_PIX : ((channel != -1) ? (preview_buf->bytes - 1) : ALPHA_G_PIX);
    }
  else
    buf = empty_buf;

  x1 = BOUNDS (preview_buf->x, 0, width);
  y1 = BOUNDS (preview_buf->y, 0, height);
  x2 = BOUNDS (preview_buf->x + preview_buf->width, 0, width);
  y2 = BOUNDS (preview_buf->y + preview_buf->height, 0, height);

  src = temp_buf_data (preview_buf) + (y1 - preview_buf->y) * rowstride +
    (x1 - preview_buf->x) * preview_buf->bytes;

  /*  One last thing for efficiency's sake:  */
  if (channel == -1)
    channel = 0;

  for (i = 0; i < height; i++)
    {
      if (i & 0x4)
	{
	  offset = 4;
	  cb = buf + offset * 3;
	}
      else
	{
	  offset = 0;
	  cb = buf;
	}

      /*  The interesting stuff between leading & trailing vertical transparency  */
      if (i >= y1 && i < y2)
	{
	  /*  Handle the leading transparency  */
	  for (j = 0; j < x1; j++)
	    for (b = 0; b < image_bytes; b++)
	      temp_buf[j * image_bytes + b] = cb[j * 3 + b];

	  /*  The stuff in the middle  */
	  s = src;
	  for (j = x1; j < x2; j++)
	    {
	      if (color)
		{
		  if (has_alpha)
		    {
		      a = s[alpha] << 8;

		      if ((j + offset) & 0x4)
			{
			  temp_buf[j * 3 + 0] = blend_dark_check [(a | s[RED_PIX])];
			  temp_buf[j * 3 + 1] = blend_dark_check [(a | s[GREEN_PIX])];
			  temp_buf[j * 3 + 2] = blend_dark_check [(a | s[BLUE_PIX])];
			}
		      else
			{
			  temp_buf[j * 3 + 0] = blend_light_check [(a | s[RED_PIX])];
			  temp_buf[j * 3 + 1] = blend_light_check [(a | s[GREEN_PIX])];
			  temp_buf[j * 3 + 2] = blend_light_check [(a | s[BLUE_PIX])];
			}
		    }
		  else
		    {
		      temp_buf[j * 3 + 0] = s[RED_PIX];
		      temp_buf[j * 3 + 1] = s[GREEN_PIX];
		      temp_buf[j * 3 + 2] = s[BLUE_PIX];
		    }
		}
	      else
		{
		  if (has_alpha)
		    {
		      a = s[alpha] << 8;

		      if ((j + offset) & 0x4)
			{
			  if (color_buf)
			    {
			      temp_buf[j * 3 + 0] = blend_dark_check [(a | s[GRAY_PIX])];
			      temp_buf[j * 3 + 1] = blend_dark_check [(a | s[GRAY_PIX])];
			      temp_buf[j * 3 + 2] = blend_dark_check [(a | s[GRAY_PIX])];
			    }
			  else
			    temp_buf[j] = blend_dark_check [(a | s[GRAY_PIX + channel])];
			}
		      else
			{
			  if (color_buf)
			    {
			      temp_buf[j * 3 + 0] = blend_light_check [(a | s[GRAY_PIX])];
			      temp_buf[j * 3 + 1] = blend_light_check [(a | s[GRAY_PIX])];
			      temp_buf[j * 3 + 2] = blend_light_check [(a | s[GRAY_PIX])];
			    }
			  else
			    temp_buf[j] = blend_light_check [(a | s[GRAY_PIX + channel])];
			}
		    }
		  else
		    {
		      if (color_buf)
			{
			  temp_buf[j * 3 + 0] = s[GRAY_PIX];
			  temp_buf[j * 3 + 1] = s[GRAY_PIX];
			  temp_buf[j * 3 + 2] = s[GRAY_PIX];
			}
		      else
			temp_buf[j] = s[GRAY_PIX + channel];
		    }
		}

	      s += preview_buf->bytes;
	    }

	  /*  Handle the trailing transparency  */
	  for (j = x2; j < width; j++)
	    for (b = 0; b < image_bytes; b++)
	      temp_buf[j * image_bytes + b] = cb[j * 3 + b];

	  src += rowstride;
	}
      else
	{
	  for (j = 0; j < width; j++)
	    for (b = 0; b < image_bytes; b++)
	      temp_buf[j * image_bytes + b] = cb[j * 3 + b];
	}

      gimp_preview_draw_row (GIMP_PREVIEW (preview_widget), temp_buf, 0, i, width);
    }
}


cairo_surface_t *
render_preview_surface (GtkWidget *preview_widget,
			int        width,
			int        height)
{
  GimpPreview *preview;
  cairo_surface_t *surface;
  guchar *src;
  guchar *s;
  guchar *dest;
  guint32 *d;
  int src_rowstride;
  int dest_rowstride;
  int bpp;
  int i, j;

  preview = GIMP_PREVIEW (preview_widget);

  width = MINIMUM (width, gimp_preview_get_width (preview));
  height = MINIMUM (height, gimp_preview_get_height (preview));
  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;

  surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24, width, height);
  cairo_surface_flush (surface);

  src = gimp_preview_get_buffer (preview);
  src_rowstride = gimp_preview_get_rowstride (preview);
  bpp = gimp_preview_get_bpp (preview);
  dest = cairo_image_surface_get_data (surface);
  dest_rowstride = cairo_image_surface_get_stride (surface);

  if (src)
    for (i = 0; i < height && i < gimp_preview_get_height (preview); i++)
      {
	s = src + i * src_rowstride;
	d = (guint32 *) (dest + i * dest_rowstride);

	for (j = 0; j < width; j++)
	  {
	    if (bpp >= 3)
	      d[j] = ((guint32) s[0] << 16) | ((guint32) s[1] << 8) | s[2];
	    else
	      d[j] = (guint32) s[0] * 0x010101;

	    s += bpp;
	  }
      }

  cairo_surface_mark_dirty (surface);

  return surface;
}


void
render_fs_preview (GtkWidget *widget,
		   cairo_t   *cr,
		   int        w,
		   int        h)
{
  int x1, y1, x2, y2;
  int foldh, foldw;
  int i;

  cairo_save (cr);
  cairo_set_line_width (cr, 1.0);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_SQUARE);

  x1 = 2;
  y1 = h / 8 + 2;
  x2 = w - w / 8 - 2;
  y2 = h - 2;

  /*  the page behind  */
  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  cairo_rectangle (cr, x1 + 0.5, y1 + 0.5, (x2 - x1), (y2 - y1));
  cairo_stroke (cr);

  foldw = w / 4;
  foldh = h / 4;
  x1 = w / 8 + 2;
  y1 = 2;
  x2 = w - 2;
  y2 = h - h / 8 - 2;

  /*  the page in front, with its corner folded  */
  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_move_to (cr, x1 + foldw, y1);
  cairo_line_to (cr, x1 + foldw, y1 + foldh);
  cairo_line_to (cr, x1, y1 + foldh);
  cairo_line_to (cr, x1, y2);
  cairo_line_to (cr, x2, y2);
  cairo_line_to (cr, x2, y1);
  cairo_close_path (cr);
  cairo_fill (cr);

  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  cairo_move_to (cr, x1 + 0.5, y1 + foldh + 0.5);
  cairo_line_to (cr, x1 + 0.5, y2 + 0.5);
  cairo_line_to (cr, x2 + 0.5, y2 + 0.5);
  cairo_line_to (cr, x2 + 0.5, y1 + 0.5);
  cairo_move_to (cr, x1 + foldw + 0.5, y1 + 0.5);
  cairo_line_to (cr, x2 + 0.5, y1 + 0.5);
  for (i = 0; i < foldw; i++)
    {
      cairo_move_to (cr, x1 + i + 0.5, y1 + foldh + 0.5);
      cairo_line_to (cr, x1 + i + 0.5, ((foldw == 1) ? y1 :
					 (y1 + (foldh - (foldh * i) / (foldw - 1)))) + 0.5);
    }
  cairo_stroke (cr);

  cairo_restore (cr);
}


static void
layers_dialog_preview_extents ()
{
  GImage *gimage;

  if (! layersD)
    return;

  gimage = gimage_get_ID (layersD->gimage_id);

  layersD->gimage_width = gimage->width;
  layersD->gimage_height = gimage->height;

  /*  Get the image width and height variables, based on the gimage  */
  if (gimage->width > gimage->height)
    layersD->ratio = (double) preview_size / (double) gimage->width;
  else
    layersD->ratio = (double) preview_size / (double) gimage->height;

  if (preview_size)
    {
      layersD->image_width = (int) (layersD->ratio * gimage->width);
      layersD->image_height = (int) (layersD->ratio * gimage->height);
      if (layersD->image_width < 1) layersD->image_width = 1;
      if (layersD->image_height < 1) layersD->image_height = 1;
    }
  else
    {
      layersD->image_width = layer_width;
      layersD->image_height = layer_height;
    }
}

static void
layers_dialog_set_menu_sensitivity ()
{
  gint fs;      /*  floating sel  */
  gint ac;      /*  active channel  */
  gint lm;      /*  layer mask  */
  gint gimage;  /*  is there a gimage  */
  gint lp;      /*  layers present  */
  gint alpha;   /*  alpha channel present  */
  Layer *layer;

  lp = FALSE;

  if (! layersD)
    return;

  if ((layer =  (layersD->active_layer)) != NULL)
    lm = (layer->mask) ? TRUE : FALSE;
  else
    lm = FALSE;

  fs = (layersD->floating_sel == NULL);
  ac = (layersD->active_channel == NULL);
  gimage = (gimage_get_ID (layersD->gimage_id) != NULL);
  alpha = layer && layer_has_alpha (layer);

  if (gimage)
    lp = (gimage_get_ID (layersD->gimage_id)->layers != NULL);

  /* new layer */
  gtk_widget_set_sensitive (layers_ops[0].widget, gimage);
  ops_button_set_sensitive (layers_ops_buttons[0], gimage);
  /* raise layer */
  gtk_widget_set_sensitive (layers_ops[1].widget, fs && ac && gimage && lp && alpha);
  ops_button_set_sensitive (layers_ops_buttons[1], fs && ac && gimage && lp && alpha);
  /* lower layer */
  gtk_widget_set_sensitive (layers_ops[2].widget, fs && ac && gimage && lp && alpha);
  ops_button_set_sensitive (layers_ops_buttons[2], fs && ac && gimage && lp && alpha);
  /* duplicate layer */
  gtk_widget_set_sensitive (layers_ops[3].widget, fs && ac && gimage && lp);
  ops_button_set_sensitive (layers_ops_buttons[3], fs && ac && gimage && lp);
  /* delete layer */
  gtk_widget_set_sensitive (layers_ops[4].widget, ac && gimage && lp);
  ops_button_set_sensitive (layers_ops_buttons[4], ac && gimage && lp);
  /* scale layer */
  gtk_widget_set_sensitive (layers_ops[5].widget, ac && gimage && lp);
  /* resize layer */
  gtk_widget_set_sensitive (layers_ops[6].widget, ac && gimage && lp);
  /* add layer mask */
  gtk_widget_set_sensitive (layers_ops[7].widget, fs && ac && gimage && !lm && lp && alpha);
  /* apply layer mask */
  gtk_widget_set_sensitive (layers_ops[8].widget, fs && ac && gimage && lm && lp);
  /* anchor layer */
  gtk_widget_set_sensitive (layers_ops[9].widget, !fs && ac && gimage && lp);
  ops_button_set_sensitive (layers_ops_buttons[5], !fs && ac && gimage && lp);
  /* merge visible layers */
  gtk_widget_set_sensitive (layers_ops[10].widget, fs && ac && gimage && lp);
  /* flatten image */
  gtk_widget_set_sensitive (layers_ops[11].widget, fs && ac && gimage && lp);
  /* alpha select */
  gtk_widget_set_sensitive (layers_ops[12].widget, fs && ac && gimage && lp && alpha);
  /* mask select */
  gtk_widget_set_sensitive (layers_ops[13].widget, fs && ac && gimage && lm && lp);
  /* add alpha */
  gtk_widget_set_sensitive (layers_ops[14].widget, !alpha);

  /* set mode, preserve transparency and opacity to insensitive if there are no layers  */
  gtk_widget_set_sensitive (layersD->preserve_trans, lp);
  gtk_widget_set_sensitive (layersD->opacity_box, lp);
  gtk_widget_set_sensitive (layersD->mode_box, lp);
}


static void
layer_widget_queue_draw (LayerWidget *layer_widget)
{
  gtk_widget_queue_draw (layer_widget->eye_widget);
  gtk_widget_queue_draw (layer_widget->linked_widget);
  gtk_widget_queue_draw (layer_widget->layer_preview);
  gtk_widget_queue_draw (layer_widget->mask_preview);
}

static void
layer_widget_set_selected (LayerWidget *layer_widget,
			   gboolean     selected)
{
  if (selected == LC_ROW_SELECTED (layer_widget->list_item))
    return;

  if (selected)
    gtk_widget_set_state_flags (layer_widget->list_item,
				GTK_STATE_FLAG_SELECTED, FALSE);
  else
    gtk_widget_unset_state_flags (layer_widget->list_item,
				  GTK_STATE_FLAG_SELECTED);

  layer_widget_queue_draw (layer_widget);
}

/*  The layers list selects one row at a time  */
static void
layers_list_select (LayerWidget *layer_widget)
{
  GSList *list;
  LayerWidget *lw;

  for (list = layersD->layer_widgets; list; list = g_slist_next (list))
    {
      lw = (LayerWidget *) list->data;
      if (lw != layer_widget)
	layer_widget_set_selected (lw, FALSE);
    }

  layer_widget_set_selected (layer_widget, TRUE);
}


static void
layers_dialog_set_active_layer (Layer * layer)
{
  LayerWidget *layer_widget;
  int index;

  layer_widget = layer_widget_get_ID (layer);
  if (!layersD || !layer_widget)
    return;

  /*  Make sure the gimage is not notified of this change  */
  suspend_gimage_notify++;

  index = gimage_get_layer_index (layer_widget->gimage, layer);
  if ((index >= 0) && !LC_ROW_SELECTED (layer_widget->list_item))
    layers_list_select (layer_widget);

  suspend_gimage_notify--;
}


static void
layers_dialog_unset_layer (Layer * layer)
{
  LayerWidget *layer_widget;
  int index;

  layer_widget = layer_widget_get_ID (layer);
  if (!layersD || !layer_widget)
    return;

  /*  Make sure the gimage is not notified of this change  */
  suspend_gimage_notify++;

  index = gimage_get_layer_index (layer_widget->gimage, layer);
  if ((index >= 0) && LC_ROW_SELECTED (layer_widget->list_item))
    layer_widget_set_selected (layer_widget, FALSE);

  suspend_gimage_notify--;
}


static void
layers_dialog_position_layer (Layer * layer,
			      int new_index)
{
  LayerWidget *layer_widget;

  layer_widget = layer_widget_get_ID (layer);
  if (!layersD || !layer_widget)
    return;

  /*  Make sure the gimage is not notified of this change  */
  suspend_gimage_notify++;

  /*  Remove the layer from the dialog  */
  if (gtk_widget_get_parent (layer_widget->list_item))
    gtk_list_box_remove (GTK_LIST_BOX (layersD->layer_list), layer_widget->list_item);
  layersD->layer_widgets = g_slist_remove (layersD->layer_widgets, layer_widget);

  suspend_gimage_notify--;

  /*  Add it back at the proper index  */
  gtk_list_box_insert (GTK_LIST_BOX (layersD->layer_list), layer_widget->list_item, new_index);
  layersD->layer_widgets = g_slist_insert (layersD->layer_widgets, layer_widget, new_index);
}


static void
layers_dialog_add_layer (Layer *layer)
{
  GImage *gimage;
  LayerWidget *layer_widget;
  int position;

  if (!layersD || !layer)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  layer_widget = create_layer_widget (gimage, layer);

  position = gimage_get_layer_index (gimage, layer);
  layersD->layer_widgets = g_slist_insert (layersD->layer_widgets, layer_widget, position);
  gtk_list_box_insert (GTK_LIST_BOX (layersD->layer_list), layer_widget->list_item, position);
}


static void
layers_dialog_remove_layer (Layer * layer)
{
  LayerWidget *layer_widget;

  layer_widget = layer_widget_get_ID (layer);

  if (!layersD || !layer_widget)
    return;

  /*  Make sure the gimage is not notified of this change  */
  suspend_gimage_notify++;

  /*  Remove the requested layer from the dialog, and delete the
   *  layer widget
   */
  layer_widget_delete (layer_widget);

  suspend_gimage_notify--;
}


static void
layers_dialog_add_layer_mask (Layer * layer)
{
  LayerWidget *layer_widget;

  layer_widget = layer_widget_get_ID (layer);
  if (!layersD || !layer_widget)
    return;

  if (! gtk_widget_get_visible (layer_widget->mask_preview))
    gtk_widget_set_visible (layer_widget->mask_preview, TRUE);

  layer_widget->active_preview = MASK_PREVIEW;

  gtk_widget_queue_draw (layer_widget->layer_preview);
}


static void
layers_dialog_remove_layer_mask (Layer * layer)
{
  LayerWidget *layer_widget;

  layer_widget = layer_widget_get_ID (layer);
  if (!layersD || !layer_widget)
    return;

  if (gtk_widget_get_visible (layer_widget->mask_preview))
    gtk_widget_set_visible (layer_widget->mask_preview, FALSE);

  layer_widget->active_preview = LAYER_PREVIEW;

  gtk_widget_queue_draw (layer_widget->layer_preview);
}


static void
paint_mode_menu_callback (GtkWidget *w,
			  gpointer   client_data)
{
  GImage *gimage;
  Layer *layer;
  int mode;

  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;
  if (! (layer =  (gimage->active_layer)))
    return;

  /*  If the layer has an alpha channel, set the transparency and redraw  */
  if (layer_has_alpha (layer))
    {
      mode = GPOINTER_TO_INT (client_data);
      if (layer->mode != mode)
	{
	  layer->mode = mode;

	  drawable_update (GIMP_DRAWABLE(layer), 0, 0, GIMP_DRAWABLE(layer)->width, GIMP_DRAWABLE(layer)->height);
	  gdisplays_flush ();
	}
    }
}


static void
image_menu_callback (GtkWidget *w,
		     gpointer   client_data)
{
  if (!lc_shell)
    return;
  if (gimage_get_ID (GPOINTER_TO_INT (client_data)) != NULL)
    {
      layers_dialog_update (GPOINTER_TO_INT (client_data));
      channels_dialog_update (GPOINTER_TO_INT (client_data));
      gdisplays_flush ();
    }
}


static void
opacity_scale_update (GtkAdjustment *adjustment,
		      gpointer       data)
{
  GImage *gimage;
  Layer *layer;
  int opacity;

  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  if (! (layer =  (gimage->active_layer)))
    return;

  /*  add the 0.001 to insure there are no subtle rounding errors  */
  opacity = (int) (gtk_adjustment_get_value (adjustment) * 2.55 + 0.001);
  if (layer->opacity != opacity)
    {
      layer->opacity = opacity;

      drawable_update (GIMP_DRAWABLE(layer), 0, 0, GIMP_DRAWABLE(layer)->width, GIMP_DRAWABLE(layer)->height);
      gdisplays_flush ();
    }
}


static void
preserve_trans_update (GtkWidget *w,
		       gpointer   data)
{
  GImage *gimage;
  Layer *layer;

  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  if (! (layer =  (gimage->active_layer)))
    return;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    layer->preserve_trans = 1;
  else
    layer->preserve_trans = 0;
}


/*****************************/
/*  layers dialog callbacks  */
/*****************************/

static void
layers_dialog_new_layer_callback (GtkWidget *w,
				  gpointer   client_data)
{
  GImage *gimage;
  Layer *layer;

  /*  if there is a currently selected gimage, request a new layer
   */
  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  /*  If there is a floating selection, the new command transforms
   *  the current fs into a new layer
   */
  if ((layer = gimage_floating_sel (gimage)))
    {
      floating_sel_to_layer (layer);

      gdisplays_flush ();
    }
  else
    layers_dialog_new_layer_query (layersD->gimage_id);
}


static void
layers_dialog_raise_layer_callback (GtkWidget *w,
				    gpointer   client_data)
{
  GImage *gimage;

  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  gimage_raise_layer (gimage, gimage->active_layer);
  gdisplays_flush ();
}


static void
layers_dialog_lower_layer_callback (GtkWidget *w,
				    gpointer   client_data)
{
  GImage *gimage;

  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  gimage_lower_layer (gimage, gimage->active_layer);
  gdisplays_flush ();
}


static void
layers_dialog_duplicate_layer_callback (GtkWidget *w,
					gpointer   client_data)
{
  GImage *gimage;
  Layer *active_layer;
  Layer *new_layer;

  /*  if there is a currently selected gimage, request a new layer
   */
  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  /*  Start a group undo  */
  undo_push_group_start (gimage, EDIT_PASTE_UNDO);

  active_layer = gimage_get_active_layer (gimage);
  new_layer = layer_copy (active_layer, TRUE);
  gimage_add_layer (gimage, new_layer, -1);

  /*  end the group undo  */
  undo_push_group_end (gimage);

  gdisplays_flush ();
}


static void
layers_dialog_delete_layer_callback (GtkWidget *w,
				     gpointer   client_data)
{
  GImage *gimage;
  Layer *layer;

  /*  if there is a currently selected gimage
   */
  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  if (! (layer = gimage_get_active_layer (gimage)))
    return;

  /*  if the layer is a floating selection, take special care  */
  if (layer_is_floating_sel (layer))
    floating_sel_remove (layer);
  else
    gimage_remove_layer (gimage, gimage->active_layer);

  gdisplays_flush ();
}


static void
layers_dialog_scale_layer_callback (GtkWidget *w,
				    gpointer   client_data)
{
  GImage *gimage;

  /*  if there is a currently selected gimage
   */
  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  layers_dialog_scale_layer_query (gimage->active_layer);
}


static void
layers_dialog_resize_layer_callback (GtkWidget *w,
				     gpointer   client_data)
{
  GImage *gimage;

  /*  if there is a currently selected gimage
   */
  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  layers_dialog_resize_layer_query (gimage->active_layer);
}


static void
layers_dialog_add_layer_mask_callback (GtkWidget *w,
				       gpointer   client_data)
{
  GImage *gimage;

  /*  if there is a currently selected gimage
   */
  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  layers_dialog_add_mask_query (gimage->active_layer);
}


static void
layers_dialog_apply_layer_mask_callback (GtkWidget *w,
					 gpointer   client_data)
{
  GImage *gimage;
  Layer *layer;

  /*  if there is a currently selected gimage
   */
  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  /*  Make sure there is a layer mask to apply  */
  if ((layer =  (gimage->active_layer)) != NULL)
    {
      if (layer->mask)
	layers_dialog_apply_mask_query (gimage->active_layer);
    }
}


static void
layers_dialog_anchor_layer_callback (GtkWidget *w,
				     gpointer   client_data)
{
  GImage *gimage;

  /*  if there is a currently selected gimage
   */
  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  floating_sel_anchor (gimage_get_active_layer (gimage));
  gdisplays_flush ();
}


static void
layers_dialog_merge_layers_callback (GtkWidget *w,
				     gpointer   client_data)
{
  GImage *gimage;

  /*  if there is a currently selected gimage
   */
  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  layers_dialog_layer_merge_query (gimage, TRUE);
}


static void
layers_dialog_flatten_image_callback (GtkWidget *w,
				      gpointer   client_data)
{
  GImage *gimage;

  /*  if there is a currently selected gimage
   */
  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  gimage_flatten (gimage);
  gdisplays_flush ();
}


static void
layers_dialog_alpha_select_callback (GtkWidget *w,
				     gpointer   client_data)
{
  GImage *gimage;

  /*  if there is a currently selected gimage
   */
  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  gimage_mask_layer_alpha (gimage, gimage->active_layer);
  gdisplays_flush ();
}


static void
layers_dialog_mask_select_callback (GtkWidget *w,
				    gpointer   client_data)
{
  GImage *gimage;

  /*  if there is a currently selected gimage
   */
  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  gimage_mask_layer_mask (gimage, gimage->active_layer);
  gdisplays_flush ();
}


static void
layers_dialog_add_alpha_channel_callback (GtkWidget *w,
					  gpointer   client_data)
{
  GImage *gimage;
  Layer *layer;

  /*  if there is a currently selected gimage
   */
  if (!layersD)
    return;
  if (! (gimage = gimage_get_ID (layersD->gimage_id)))
    return;

  if (! (layer = gimage_get_active_layer (gimage)))
    return;

   /*  Add an alpha channel  */
  layer_add_alpha (layer);
  gdisplays_flush ();
}


static void
lc_dialog_close_callback (GtkWidget *w,
			  gpointer   client_data)
{
  if (layersD)
    layersD->gimage_id = -1;

  if (lc_shell)
    gtk_widget_set_visible (lc_shell, FALSE);
}

static gboolean
lc_dialog_close_request (GtkWindow *window,
			 gpointer   client_data)
{
  lc_dialog_close_callback (GTK_WIDGET (window), client_data);

  return TRUE;
}

static void
lc_dialog_destroy_callback (GtkWidget *w,
			    gpointer   client_data)
{
  if (lc_shell == w)
    lc_shell = NULL;
}


/****************************/
/*  layer widget functions  */
/****************************/

static LayerWidget *
layer_widget_get_ID (Layer * ID)
{
  LayerWidget *lw;
  GSList *list;

  if (!layersD)
    return NULL;

  list = layersD->layer_widgets;

  while (list)
    {
      lw = (LayerWidget *) list->data;
      if (lw->layer == ID)
	return lw;

      list = g_slist_next(list);
    }

  return NULL;
}

/*  The layer widget a row, or a widget in a row, belongs to.  NULL
 *  once the layer widget has been deleted.
 */
static LayerWidget *
layer_widget_from (GtkWidget *widget)
{
  GtkWidget *row;

  if (GTK_IS_LIST_BOX_ROW (widget))
    row = widget;
  else
    row = gtk_widget_get_ancestor (widget, GTK_TYPE_LIST_BOX_ROW);

  if (!row)
    return NULL;

  return (LayerWidget *) g_object_get_data (G_OBJECT (row), LAYER_WIDGET_KEY);
}


static GtkWidget *
layer_widget_drawing_area (int width,
			   int height,
			   GtkDrawingAreaDrawFunc draw_func)
{
  GtkWidget *area;

  area = gtk_drawing_area_new ();
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (area), width);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (area), height);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (area), draw_func, NULL, NULL);
  gtk_widget_set_halign (area, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (area, GTK_ALIGN_CENTER);

  return area;
}

static void
layer_widget_add_button_events (GtkWidget *widget)
{
  GtkGesture *gesture;

  gesture = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 0);
  g_signal_connect (gesture, "drag-begin",
		    G_CALLBACK (layer_widget_button_begin), NULL);
  g_signal_connect (gesture, "drag-update",
		    G_CALLBACK (layer_widget_button_update), NULL);
  g_signal_connect (gesture, "drag-end",
		    G_CALLBACK (layer_widget_button_end), NULL);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (gesture));
}

static void
layer_widget_add_preview_events (GtkWidget *widget)
{
  GtkGesture *gesture;

  gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 0);
  g_signal_connect (gesture, "pressed",
		    G_CALLBACK (layer_widget_preview_pressed), NULL);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (gesture));
}

static LayerWidget *
create_layer_widget (GImage *gimage,
		     Layer  *layer)
{
  LayerWidget *layer_widget;
  GtkWidget *list_item;
  GtkWidget *hbox;
  GtkWidget *vbox;
  GtkGesture *gesture;

  list_item = gtk_list_box_row_new ();
  g_object_ref_sink (list_item);
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (list_item), FALSE);

  /*  create the layer widget and add it to the list  */
  layer_widget = (LayerWidget *) g_malloc (sizeof (LayerWidget));
  layer_widget->gimage = gimage;
  layer_widget->layer = layer;
  layer_widget->layer_preview = NULL;
  layer_widget->mask_preview = NULL;
  layer_widget->layer_pixmap = NULL;
  layer_widget->mask_pixmap = NULL;
  layer_widget->list_item = list_item;
  layer_widget->width = -1;
  layer_widget->height = -1;
  layer_widget->layer_mask = (layer->mask != NULL);
  layer_widget->apply_mask = layer->apply_mask;
  layer_widget->edit_mask = layer->edit_mask;
  layer_widget->show_mask = layer->show_mask;
  layer_widget->visited = TRUE;

  if (layer->mask)
    layer_widget->active_preview = (layer->edit_mask) ? MASK_PREVIEW : LAYER_PREVIEW;
  else
    layer_widget->active_preview = LAYER_PREVIEW;

  /*  Need to let the list item know about the layer_widget  */
  g_object_set_data (G_OBJECT (list_item), LAYER_WIDGET_KEY, layer_widget);

  /*  clicks on the row select it, pop up the menu, or edit the layer  */
  gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 0);
  g_signal_connect (gesture, "pressed",
		    G_CALLBACK (layer_widget_row_pressed), NULL);
  gtk_widget_add_controller (list_item, GTK_EVENT_CONTROLLER (gesture));

  vbox = gimp_vbox_new (FALSE, 1);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (list_item), vbox);

  hbox = gimp_hbox_new (FALSE, 1);
  gimp_box_pack_start (vbox, hbox, FALSE, FALSE, 1);

  /* Create the visibility toggle button */
  layer_widget->eye_widget = layer_widget_drawing_area (eye_width, eye_height,
							layer_widget_eye_draw);
  layer_widget_add_button_events (layer_widget->eye_widget);
  gimp_box_pack_start (hbox, layer_widget->eye_widget, FALSE, TRUE, 2);

  /* Create the link toggle button */
  layer_widget->linked_widget = layer_widget_drawing_area (eye_width, eye_height,
							   layer_widget_linked_draw);
  layer_widget_add_button_events (layer_widget->linked_widget);
  gimp_box_pack_start (hbox, layer_widget->linked_widget, FALSE, TRUE, 2);

  layer_widget->layer_preview =
    layer_widget_drawing_area (layersD->image_width + 4, layersD->image_height + 4,
			       layer_widget_preview_draw);
  layer_widget_add_preview_events (layer_widget->layer_preview);
  gimp_box_pack_start (hbox, layer_widget->layer_preview, FALSE, FALSE, 2);

  layer_widget->mask_preview =
    layer_widget_drawing_area (layersD->image_width + 4, layersD->image_height + 4,
			       layer_widget_preview_draw);
  layer_widget_add_preview_events (layer_widget->mask_preview);
  gimp_box_pack_start (hbox, layer_widget->mask_preview, FALSE, FALSE, 2);
  gtk_widget_set_visible (layer_widget->mask_preview, layer->mask != NULL);

  /*  the layer name label */
  if (layer_is_floating_sel (layer))
    layer_widget->label = gtk_label_new ("Floating Selection");
  else
    layer_widget->label = gtk_label_new (GIMP_DRAWABLE(layer)->name);
  gimp_box_pack_start (hbox, layer_widget->label, FALSE, FALSE, 2);

  layer_widget->clip_widget = layer_widget_drawing_area (1, 2, layer_widget_clip_draw);
  gtk_widget_set_halign (layer_widget->clip_widget, GTK_ALIGN_FILL);
  layer_widget_add_button_events (layer_widget->clip_widget);
  gimp_box_pack_start (vbox, layer_widget->clip_widget, FALSE, FALSE, 0);
  gtk_widget_set_visible (layer_widget->clip_widget, FALSE);

  return layer_widget;
}


static void
layer_widget_delete (LayerWidget *layer_widget)
{
  if (layer_widget->layer_pixmap)
    cairo_surface_destroy (layer_widget->layer_pixmap);
  if (layer_widget->mask_pixmap)
    cairo_surface_destroy (layer_widget->mask_pixmap);

  /*  Remove the layer widget from the list  */
  layersD->layer_widgets = g_slist_remove (layersD->layer_widgets, layer_widget);

  /*  Release the widget  */
  g_object_set_data (G_OBJECT (layer_widget->list_item), LAYER_WIDGET_KEY, NULL);
  if (gtk_widget_get_parent (layer_widget->list_item))
    gtk_list_box_remove (GTK_LIST_BOX (layersD->layer_list), layer_widget->list_item);
  g_object_unref (layer_widget->list_item);
  g_free (layer_widget);
}


static void
layer_widget_select_update (LayerWidget *layer_widget)
{
  if (layer_widget == NULL)
    return;

  /*  Is the list item being selected?  */
  if (!LC_ROW_SELECTED (layer_widget->list_item))
    return;

  /*  Only notify the gimage of an active layer change if necessary  */
  if (suspend_gimage_notify == 0)
    {
      /*  set the gimage's active layer to be this layer  */
      gimage_set_active_layer (layer_widget->gimage, layer_widget->layer);

      gdisplays_flush ();
    }
}


static void
layer_widget_row_pressed (GtkGestureClick *gesture,
			  int              n_press,
			  double           x,
			  double           y,
			  gpointer         data)
{
  GtkWidget *row;
  LayerWidget *layer_widget;
  guint button;

  row = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  if (! (layer_widget = layer_widget_from (row)) || !layersD)
    return;

  button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));

  if (button == 3)
    {
      lc_ops_menu_popup (layersD->ops_menu, row, x, y);
      return;
    }

  if (!LC_ROW_SELECTED (row))
    {
      layers_list_select (layer_widget);
      layer_widget_select_update (layer_widget);
    }

  /*  the flush above may have deleted the layer widget  */
  if (n_press == 2 && button == 1 && (layer_widget = layer_widget_from (row)))
    layers_dialog_edit_layer_query (layer_widget);
}


/*  The eye and chain toggles: pressing toggles; moving out of the
 *  toggle with the button held toggles back, moving in again toggles
 *  again; the change is committed when the button is released.
 */
static int button_down = 0;
static int button_inside = FALSE;
static GtkWidget *click_widget = NULL;
static int old_state;
static int exclusive;
static double click_x, click_y;

static void
layer_widget_button_toggle (LayerWidget *layer_widget,
			    GtkWidget   *widget)
{
  if (widget == layer_widget->eye_widget)
    {
      if (exclusive)
	{
	  layer_widget_exclusive_visible (layer_widget);
	}
      else
	{
	  GIMP_DRAWABLE(layer_widget->layer)->visible = !GIMP_DRAWABLE(layer_widget->layer)->visible;
	  layer_widget_eye_redraw (layer_widget);
	}
    }
  else if (widget == layer_widget->linked_widget)
    {
      layer_widget->layer->linked = !layer_widget->layer->linked;
      layer_widget_linked_redraw (layer_widget);
    }
}

static void
layer_widget_button_begin (GtkGestureDrag *gesture,
			   double          x,
			   double          y,
			   gpointer        data)
{
  GtkWidget *widget;
  LayerWidget *layer_widget;
  GdkModifierType state;
  guint button;

  widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  if (! (layer_widget = layer_widget_from (widget)) || !layersD)
    return;

  gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);

  button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
  state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));

  if (button == 3)
    {
      button_down = 0;
      lc_ops_menu_popup (layersD->ops_menu, widget, x, y);
      return;
    }

  button_down = 1;
  button_inside = TRUE;
  click_widget = widget;
  click_x = x;
  click_y = y;

  if (widget == layer_widget->eye_widget)
    {
      old_state = GIMP_DRAWABLE(layer_widget->layer)->visible;

      /*  If this was a shift-click, make all/none visible  */
      if (state & GDK_SHIFT_MASK)
	{
	  exclusive = TRUE;
	  layer_widget_exclusive_visible (layer_widget);
	}
      else
	{
	  exclusive = FALSE;
	  GIMP_DRAWABLE(layer_widget->layer)->visible = !GIMP_DRAWABLE(layer_widget->layer)->visible;
	  layer_widget_eye_redraw (layer_widget);
	}
    }
  else if (widget == layer_widget->linked_widget)
    {
      old_state = layer_widget->layer->linked;
      layer_widget->layer->linked = !layer_widget->layer->linked;
      layer_widget_linked_redraw (layer_widget);
    }
}

static void
layer_widget_button_update (GtkGestureDrag *gesture,
			    double          offset_x,
			    double          offset_y,
			    gpointer        data)
{
  GtkWidget *widget;
  LayerWidget *layer_widget;
  double x, y;
  int inside;

  widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  if (!button_down || widget != click_widget ||
      ! (layer_widget = layer_widget_from (widget)))
    return;

  x = click_x + offset_x;
  y = click_y + offset_y;
  inside = (x >= 0 && y >= 0 &&
	    x < gtk_widget_get_width (widget) &&
	    y < gtk_widget_get_height (widget));

  if (inside != button_inside)
    {
      button_inside = inside;
      layer_widget_button_toggle (layer_widget, widget);
    }
}

static void
layer_widget_button_end (GtkGestureDrag *gesture,
			 double          offset_x,
			 double          offset_y,
			 gpointer        data)
{
  GtkWidget *widget;
  LayerWidget *layer_widget;

  widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  if (!button_down || widget != click_widget)
    return;

  button_down = 0;
  click_widget = NULL;

  if (! (layer_widget = layer_widget_from (widget)))
    return;

  if (widget == layer_widget->eye_widget)
    {
      if (exclusive)
	{
	  gimage_invalidate_preview (layer_widget->gimage);
	  gdisplays_update_area (layer_widget->gimage->ID, 0, 0,
				 layer_widget->gimage->width,
				 layer_widget->gimage->height);
	  gdisplays_flush ();
	}
      else if (old_state != GIMP_DRAWABLE(layer_widget->layer)->visible)
	{
	  /*  Invalidate the gimage preview  */
	  drawable_update (GIMP_DRAWABLE(layer_widget->layer), 0, 0,
			   GIMP_DRAWABLE(layer_widget->layer)->width,
			   GIMP_DRAWABLE(layer_widget->layer)->height);
	  gdisplays_flush ();
	}
    }
}


static void
layer_widget_preview_pressed (GtkGestureClick *gesture,
			      int              n_press,
			      double           x,
			      double           y,
			      gpointer         data)
{
  GtkWidget *widget;
  LayerWidget *layer_widget;
  GdkModifierType state;
  guint button;
  int preview_type;

  widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  if (! (layer_widget = layer_widget_from (widget)) || !layersD)
    return;

  if (widget == layer_widget->layer_preview)
    preview_type = LAYER_PREVIEW;
  else if (widget == layer_widget->mask_preview && gtk_widget_get_visible (widget))
    preview_type = MASK_PREVIEW;
  else
    return;

  if (layer_is_floating_sel (layer_widget->layer))
    preview_type = FS_PREVIEW;

  button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
  state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));

  if (button == 3)
    {
      gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
      lc_ops_menu_popup (layersD->ops_menu, widget, x, y);
      return;
    }

  /*  Control-button press disables the application of the mask  */
  if (state & GDK_CONTROL_MASK)
    {
      if (preview_type == MASK_PREVIEW)
	{
	  gimage_set_layer_mask_apply (layer_widget->gimage, GIMP_DRAWABLE(layer_widget->layer)->ID);
	  gdisplays_flush ();
	}
    }
  /*  Alt-button press makes the mask visible instead of the layer  */
  else if (state & GDK_ALT_MASK)
    {
      if (preview_type == MASK_PREVIEW)
	{
	  gimage_set_layer_mask_show (layer_widget->gimage, GIMP_DRAWABLE(layer_widget->layer)->ID);
	  gdisplays_flush ();
	}
    }
  else if (layer_widget->active_preview != preview_type)
    {
      gimage_set_layer_mask_edit (layer_widget->gimage, layer_widget->layer,
				  (preview_type == MASK_PREVIEW) ? 1 : 0);
      gdisplays_flush ();
    }

  /*  the press goes on to the row, which selects the layer  */
}


static void
layer_widget_preview_draw (GtkDrawingArea *area,
			   cairo_t        *cr,
			   int             width,
			   int             height,
			   gpointer        data)
{
  GtkWidget *widget;
  cairo_surface_t **pixmap;
  LayerWidget *layer_widget;
  int valid;
  int preview_type;

  widget = GTK_WIDGET (area);
  if (! (layer_widget = layer_widget_from (widget)) || !layersD)
    return;

  if (widget == layer_widget->layer_preview)
    {
      preview_type = LAYER_PREVIEW;
      pixmap = &layer_widget->layer_pixmap;
      valid = GIMP_DRAWABLE(layer_widget->layer)->preview_valid;
    }
  else if (widget == layer_widget->mask_preview && layer_widget->layer->mask)
    {
      preview_type = MASK_PREVIEW;
      pixmap = &layer_widget->mask_pixmap;
      valid = GIMP_DRAWABLE(layer_widget->layer->mask)->preview_valid;
    }
  else
    return;

  if (layer_is_floating_sel (layer_widget->layer))
    preview_type = FS_PREVIEW;

  if (!preview_size && preview_type != FS_PREVIEW)
    layer_widget_no_preview_redraw (layer_widget, preview_type, cr);
  else
    {
      if (!valid || !*pixmap)
	layer_widget_preview_redraw (layer_widget, preview_type);

      if (*pixmap)
	{
	  cairo_set_source_surface (cr, *pixmap, 2, 2);
	  cairo_rectangle (cr, 2, 2, layersD->image_width, layersD->image_height);
	  cairo_fill (cr);
	}
    }

  /*  The boundary indicating whether layer or mask is active  */
  layer_widget_boundary_redraw (layer_widget, preview_type, cr);
}

static void
layer_widget_boundary_redraw (LayerWidget *layer_widget,
			      int          preview_type,
			      cairo_t     *cr)
{
  GtkWidget *widget;
  GdkRGBA c1, c2;
  GdkRGBA green = { 0.0, 1.0, 0.0, 1.0 };
  GdkRGBA red = { 1.0, 0.0, 0.0, 1.0 };
  gboolean draw1;
  gboolean draw2;

  if (preview_type == LAYER_PREVIEW)
    widget = layer_widget->layer_preview;
  else if (preview_type == MASK_PREVIEW)
    widget = layer_widget->mask_preview;
  else
    return;

  /*  The active one of layer and mask is outlined in the foreground
   *  color of the row (so it stands out on a selected row too); the
   *  other one is not outlined.
   */
  gtk_widget_get_color (widget, &c1);
  draw1 = (layer_widget->active_preview == preview_type);

  c2 = c1;
  draw2 = draw1;
  if (preview_type == MASK_PREVIEW)
    {
      if (layer_widget->layer->show_mask)
	{
	  c2 = green;
	  draw2 = TRUE;
	}
      else if (! layer_widget->layer->apply_mask)
	{
	  c2 = red;
	  draw2 = TRUE;
	}
    }

  cairo_save (cr);
  cairo_set_line_width (cr, 1.0);

  if (draw1)
    {
      gdk_cairo_set_source_rgba (cr, &c1);
      cairo_rectangle (cr, 0.5, 0.5,
		       layersD->image_width + 3,
		       layersD->image_height + 3);
      cairo_stroke (cr);
    }

  if (draw2)
    {
      gdk_cairo_set_source_rgba (cr, &c2);
      cairo_rectangle (cr, 1.5, 1.5,
		       layersD->image_width + 1,
		       layersD->image_height + 1);
      cairo_stroke (cr);
    }

  cairo_restore (cr);
}

static void
layer_widget_preview_redraw (LayerWidget *layer_widget,
			     int          preview_type)
{
  TempBuf *preview_buf;
  cairo_surface_t **pixmap;
  cairo_t *cr;
  GtkWidget *widget;
  int offx, offy;

  preview_buf = NULL;
  pixmap = NULL;
  widget = NULL;

  switch (preview_type)
    {
    case LAYER_PREVIEW:
    case FS_PREVIEW:
      widget = layer_widget->layer_preview;
      pixmap = &layer_widget->layer_pixmap;
      break;
    case MASK_PREVIEW:
      widget = layer_widget->mask_preview;
      pixmap = &layer_widget->mask_pixmap;
      break;
    default:
      return;
    }

  /*  If this is a floating selection preview, draw the preview  */
  if (preview_type == FS_PREVIEW)
    {
      if (*pixmap)
	cairo_surface_destroy (*pixmap);
      *pixmap = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
					    layersD->image_width,
					    layersD->image_height);
      cr = cairo_create (*pixmap);
      render_fs_preview (widget, cr, layersD->image_width, layersD->image_height);
      cairo_destroy (cr);
    }
  /*  otherwise, ask the layer or mask for the preview  */
  else
    {
      if (!layersD->layer_preview)
	return;

      /*  determine width and height  */
      layer_widget->width = (int) (layersD->ratio * GIMP_DRAWABLE(layer_widget->layer)->width);
      layer_widget->height = (int) (layersD->ratio * GIMP_DRAWABLE(layer_widget->layer)->height);
      if (layer_widget->width < 1) layer_widget->width = 1;
      if (layer_widget->height < 1) layer_widget->height = 1;
      offx = (int) (layersD->ratio * GIMP_DRAWABLE(layer_widget->layer)->offset_x);
      offy = (int) (layersD->ratio * GIMP_DRAWABLE(layer_widget->layer)->offset_y);

      switch (preview_type)
	{
	case LAYER_PREVIEW:
	  preview_buf = layer_preview (layer_widget->layer,
				       layer_widget->width,
				       layer_widget->height);

	  break;
	case MASK_PREVIEW:
	  preview_buf = layer_mask_preview (layer_widget->layer,
					    layer_widget->width,
					    layer_widget->height);
	  break;
	}

      if (!preview_buf)
	return;

      preview_buf->x = offx;
      preview_buf->y = offy;

      render_preview (preview_buf,
		      layersD->layer_preview,
		      layersD->image_width,
		      layersD->image_height,
		      -1);

      if (*pixmap)
	cairo_surface_destroy (*pixmap);
      *pixmap = render_preview_surface (layersD->layer_preview,
					layersD->image_width,
					layersD->image_height);
    }
}


static void
layer_widget_no_preview_redraw (LayerWidget *layer_widget,
				int          preview_type,
				cairo_t     *cr)
{
  GtkWidget *widget;
  const unsigned char *bits;
  int width, height;

  switch (preview_type)
    {
    case LAYER_PREVIEW:
      widget = layer_widget->layer_preview;
      bits = layer_bits;
      width = layer_width;
      height = layer_height;
      break;
    case MASK_PREVIEW:
      widget = layer_widget->mask_preview;
      bits = mask_bits;
      width = mask_width;
      height = mask_height;
      break;
    default:
      return;
    }

  /*  the row draws the background for the normal, selected and
   *  insensitive states; the icon is drawn in the matching foreground
   */
  lc_draw_bitmap (widget, cr, bits, width, height, 2, 2);
}


static void
layer_widget_eye_draw (GtkDrawingArea *area,
		       cairo_t        *cr,
		       int             width,
		       int             height,
		       gpointer        data)
{
  LayerWidget *layer_widget;

  if (! (layer_widget = layer_widget_from (GTK_WIDGET (area))))
    return;

  if (GIMP_DRAWABLE(layer_widget->layer)->visible)
    lc_draw_bitmap (GTK_WIDGET (area), cr, eye_bits, eye_width, eye_height, 0, 0);
}

static void
layer_widget_linked_draw (GtkDrawingArea *area,
			  cairo_t        *cr,
			  int             width,
			  int             height,
			  gpointer        data)
{
  LayerWidget *layer_widget;

  if (! (layer_widget = layer_widget_from (GTK_WIDGET (area))))
    return;

  if (layer_widget->layer->linked)
    lc_draw_bitmap (GTK_WIDGET (area), cr, linked_bits, linked_width, linked_height, 0, 0);
}

static void
layer_widget_clip_draw (GtkDrawingArea *area,
			cairo_t        *cr,
			int             width,
			int             height,
			gpointer        data)
{
  GdkRGBA fg;

  gtk_widget_get_color (GTK_WIDGET (area), &fg);
  gdk_cairo_set_source_rgba (cr, &fg);
  cairo_paint (cr);
}

static void
layer_widget_eye_redraw (LayerWidget *layer_widget)
{
  gtk_widget_queue_draw (layer_widget->eye_widget);
}

static void
layer_widget_linked_redraw (LayerWidget *layer_widget)
{
  gtk_widget_queue_draw (layer_widget->linked_widget);
}


static void
layer_widget_exclusive_visible (LayerWidget *layer_widget)
{
  GSList *list;
  LayerWidget *lw;
  int visible = FALSE;

  if (!layersD)
    return;

  /*  First determine if _any_ other layer widgets are set to visible  */
  list = layersD->layer_widgets;
  while (list)
    {
      lw = (LayerWidget *) list->data;
      if (lw != layer_widget)
	visible |= GIMP_DRAWABLE(lw->layer)->visible;

      list = g_slist_next (list);
    }

  /*  Now, toggle the visibility for all layers except the specified one  */
  list = layersD->layer_widgets;
  while (list)
    {
      lw = (LayerWidget *) list->data;
      if (lw != layer_widget)
	GIMP_DRAWABLE(lw->layer)->visible = !visible;
      else
	GIMP_DRAWABLE(lw->layer)->visible = TRUE;

      layer_widget_eye_redraw (lw);

      list = g_slist_next (list);
    }
}


static void
layer_widget_layer_flush (GtkWidget *widget,
			  gpointer   client_data)
{
  LayerWidget *layer_widget;
  Layer *layer;
  const char *name;
  const char *label_name;
  int update_layer_preview = FALSE;
  int update_mask_preview = FALSE;

  if (! (layer_widget = layer_widget_from (widget)))
    return;
  layer = layer_widget->layer;

  /*  Set sensitivity  */

  /*  to false if there is a floating selection, and this aint it  */
  if (! layer_is_floating_sel (layer_widget->layer) && layersD->floating_sel != NULL)
    {
      if (gtk_widget_get_sensitive (layer_widget->list_item))
	gtk_widget_set_sensitive (layer_widget->list_item, FALSE);
    }
  /*  to true if there is a floating selection, and this is it  */
  if (layer_is_floating_sel (layer_widget->layer) && layersD->floating_sel != NULL)
    {
      if (! gtk_widget_get_sensitive (layer_widget->list_item))
	gtk_widget_set_sensitive (layer_widget->list_item, TRUE);
    }
  /*  to true if there is not floating selection  */
  else if (layersD->floating_sel == NULL)
    {
      if (! gtk_widget_get_sensitive (layer_widget->list_item))
	gtk_widget_set_sensitive (layer_widget->list_item, TRUE);
    }

  /*  if there is an active channel, unselect layer  */
  if (layersD->active_channel != NULL)
    layers_dialog_unset_layer (layer_widget->layer);
  /*  otherwise, if this is the active layer, set  */
  else if (layersD->active_layer == layer_widget->layer)
    {
      layers_dialog_set_active_layer (layersD->active_layer);
      /*  set the data widgets to reflect this layer's values
       *  1)  The opacity slider
       *  2)  The paint mode menu
       *  3)  The preserve trans button
       */
      gtk_adjustment_set_value (layersD->opacity_data,
				(gdouble) layer_widget->layer->opacity / 2.55);
      gimp_option_menu_set_history (layersD->mode_option_menu,
				    /*  Kludge to deal with the absence of behind */
				    ((layer_widget->layer->mode > BEHIND_MODE) ?
				     layer_widget->layer->mode - 1 : layer_widget->layer->mode));
      gtk_check_button_set_active (GTK_CHECK_BUTTON (layersD->preserve_trans),
				   (layer_widget->layer->preserve_trans) ? TRUE : FALSE);
    }

  if (layer_is_floating_sel (layer_widget->layer))
    name = "Floating Selection";
  else
    name = GIMP_DRAWABLE(layer_widget->layer)->name;

  /*  we need to set the name label if necessary  */
  label_name = gtk_label_get_text (GTK_LABEL (layer_widget->label));
  if (strcmp (name, label_name))
    gtk_label_set_text (GTK_LABEL (layer_widget->label), name);

  /*  show the layer mask preview if necessary  */
  if (layer_widget->layer->mask == NULL && layer_widget->layer_mask)
    {
      layer_widget->layer_mask = FALSE;
      layers_dialog_remove_layer_mask (layer_widget->layer);
    }
  else if (layer_widget->layer->mask != NULL && !layer_widget->layer_mask)
    {
      layer_widget->layer_mask = TRUE;
      layers_dialog_add_layer_mask (layer_widget->layer);
    }

  /*  Update the previews  */
  update_layer_preview = (! GIMP_DRAWABLE(layer)->preview_valid);

  if (layer->mask)
    {
      update_mask_preview = (! GIMP_DRAWABLE(layer->mask)->preview_valid);

      if (layer->apply_mask != layer_widget->apply_mask)
	{
	  layer_widget->apply_mask = layer->apply_mask;
	  update_mask_preview = TRUE;
	}
      if (layer->show_mask != layer_widget->show_mask)
	{
	  layer_widget->show_mask = layer->show_mask;
	  update_mask_preview = TRUE;
	}
      if (layer->edit_mask != layer_widget->edit_mask)
	{
	  layer_widget->edit_mask = layer->edit_mask;

	  if (layer->edit_mask == TRUE)
	    layer_widget->active_preview = MASK_PREVIEW;
	  else
	    layer_widget->active_preview = LAYER_PREVIEW;

	  /*  The boundary indicating whether layer or mask is active  */
	  gtk_widget_queue_draw (layer_widget->layer_preview);
	  gtk_widget_queue_draw (layer_widget->mask_preview);
	}
    }

  if (update_layer_preview)
    gtk_widget_queue_draw (layer_widget->layer_preview);
  if (update_mask_preview)
    gtk_widget_queue_draw (layer_widget->mask_preview);
}


/*
 *  Small helpers for the query dialogs
 */

static GtkWidget *
lc_query_dialog_new (const char *title,
		     GCallback   close_request,
		     gpointer    data)
{
  GtkWidget *dialog;

  dialog = gimp_dialog_new (title);

  /* handle the wm close signal */
  g_signal_connect (dialog, "close-request", close_request, data);

  return dialog;
}

static GtkWidget *
lc_query_dialog_vbox (GtkWidget *dialog,
		      int        border)
{
  GtkWidget *vbox;

  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, border);
  gimp_box_pack_start (gimp_dialog_get_vbox (dialog), vbox, TRUE, TRUE, 0);

  return vbox;
}


/*
 *  The new layer query dialog
 */

typedef struct _NewLayerOptions NewLayerOptions;

struct _NewLayerOptions {
  GtkWidget *query_box;
  GtkWidget *name_entry;
  GtkWidget *xsize_entry;
  GtkWidget *ysize_entry;
  int fill_type;
  int xsize;
  int ysize;

  int gimage_id;
};

static int fill_type = TRANSPARENT_FILL;
static char *layer_name = NULL;

static void
new_layer_query_ok_callback (GtkWidget *w,
			     gpointer   client_data)
{
  NewLayerOptions *options;
  Layer *layer;
  GImage *gimage;

  options = (NewLayerOptions *) client_data;
  if (layer_name)
    g_free (layer_name);
  layer_name = g_strdup (gtk_editable_get_text (GTK_EDITABLE (options->name_entry)));
  fill_type = options->fill_type;
  options->xsize = atoi (gtk_editable_get_text (GTK_EDITABLE (options->xsize_entry)));
  options->ysize = atoi (gtk_editable_get_text (GTK_EDITABLE (options->ysize_entry)));

  if ((gimage = gimage_get_ID (options->gimage_id)))
    {
      /*  Start a group undo  */
      undo_push_group_start (gimage, EDIT_PASTE_UNDO);

      layer = layer_new (gimage->ID, options->xsize, options->ysize,
			 gimage_base_type_with_alpha (gimage),
			 layer_name, OPAQUE_OPACITY, NORMAL_MODE);
      if (layer)
	{
	  drawable_fill (GIMP_DRAWABLE(layer), fill_type);
	  gimage_add_layer (gimage, layer, -1);

	  /*  Endx the group undo  */
	  undo_push_group_end (gimage);

	  gdisplays_flush ();
	}
      else
	{
	  g_message ("new_layer_query_ok_callback: could not allocate new layer");
	}
    }

  gtk_window_destroy (GTK_WINDOW (options->query_box));
  g_free (options);
}

static void
new_layer_query_cancel_callback (GtkWidget *w,
				 gpointer   client_data)
{
  NewLayerOptions *options;

  options = (NewLayerOptions *) client_data;
  gtk_window_destroy (GTK_WINDOW (options->query_box));
  g_free (options);
}

static gboolean
new_layer_query_delete_callback (GtkWindow *w,
				 gpointer   client_data)
{
  new_layer_query_cancel_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}


static void
new_layer_background_callback (GtkWidget *w,
			       gpointer   client_data)
{
  NewLayerOptions *options;

  if (!gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    return;
  options = (NewLayerOptions *) client_data;
  options->fill_type = BACKGROUND_FILL;
}

static void
new_layer_foreground_callback (GtkWidget *w,
			       gpointer   client_data)
{
  NewLayerOptions *options;

  if (!gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    return;
  options = (NewLayerOptions *) client_data;
  options->fill_type = FOREGROUND_FILL;
}

static void
new_layer_white_callback (GtkWidget *w,
			  gpointer   client_data)
{
  NewLayerOptions *options;

  if (!gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    return;
  options = (NewLayerOptions *) client_data;
  options->fill_type = WHITE_FILL;
}

static void
new_layer_transparent_callback (GtkWidget *w,
				gpointer   client_data)
{
  NewLayerOptions *options;

  if (!gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    return;
  options = (NewLayerOptions *) client_data;
  options->fill_type = TRANSPARENT_FILL;
}

static void
layers_dialog_new_layer_query (int gimage_id)
{
  GImage *gimage;
  NewLayerOptions *options;
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *radio_frame;
  GtkWidget *radio_box;
  GtkWidget *radio_button;
  int i;
  int initial_fill;
  char size[12];
  char *button_names[4] =
  {
    "Background",
    "White",
    "Transparent",
    "Foreground"
  };
  void (* button_callbacks[4]) (GtkWidget *, gpointer) =
  {
    new_layer_background_callback,
    new_layer_white_callback,
    new_layer_transparent_callback,
    new_layer_foreground_callback
  };

  gimage = gimage_get_ID (gimage_id);

  /*  the new options structure  */
  options = (NewLayerOptions *) g_malloc (sizeof (NewLayerOptions));
  options->fill_type = fill_type;
  options->gimage_id = gimage_id;

  /*  the dialog  */
  options->query_box = lc_query_dialog_new ("New Layer Options",
					    G_CALLBACK (new_layer_query_delete_callback),
					    options);

  /*  the main vbox  */
  vbox = lc_query_dialog_vbox (options->query_box, 2);

  table = gimp_table_new (3, 2, FALSE);
  gimp_box_pack_start (vbox, table, TRUE, TRUE, 0);

  /*  the name entry hbox, label and entry  */
  label = gtk_label_new ("Layer name:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 0, 1,
		     GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 0, 1);

  options->name_entry = gtk_entry_new ();
  gtk_widget_set_size_request (options->name_entry, 75, -1);
  gimp_table_attach (table, options->name_entry, 1, 2, 0, 1,
		     GIMP_EXPAND | GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 1, 1);
  gtk_editable_set_text (GTK_EDITABLE (options->name_entry), (layer_name ? layer_name : "New Layer"));

  /*  the xsize entry hbox, label and entry  */
  sprintf (size, "%d", gimage->width);
  label = gtk_label_new ("Layer width:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 1, 2,
		     GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 0, 1);
  options->xsize_entry = gtk_entry_new ();
  gtk_widget_set_size_request (options->xsize_entry, 75, -1);
  gimp_table_attach (table, options->xsize_entry, 1, 2, 1, 2,
		     GIMP_EXPAND | GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 1, 1);
  gtk_editable_set_text (GTK_EDITABLE (options->xsize_entry), size);

  /*  the ysize entry hbox, label and entry  */
  sprintf (size, "%d", gimage->height);
  label = gtk_label_new ("Layer height:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 2, 3,
		     GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 0, 1);
  options->ysize_entry = gtk_entry_new ();
  gtk_widget_set_size_request (options->ysize_entry, 75, -1);
  gimp_table_attach (table, options->ysize_entry, 1, 2, 2, 3,
		     GIMP_EXPAND | GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 1, 1);
  gtk_editable_set_text (GTK_EDITABLE (options->ysize_entry), size);

  /*  the radio frame and box  */
  radio_frame = gtk_frame_new ("Layer Fill Type");
  gimp_box_pack_start (vbox, radio_frame, FALSE, FALSE, 0);

  radio_box = gimp_vbox_new (FALSE, 1);
  gtk_frame_set_child (GTK_FRAME (radio_frame), radio_box);

  /*  the radio buttons  */
  initial_fill = options->fill_type;
  radio_button = NULL;
  for (i = 0; i < 4; i++)
    {
      radio_button = gimp_radio_button_new (radio_button, button_names[i]);
      gimp_box_pack_start (radio_box, radio_button, FALSE, FALSE, 0);
      g_signal_connect (radio_button, "toggled",
			G_CALLBACK (button_callbacks[i]),
			options);

      /*  set the correct radio button  */
      if (i == initial_fill)
	gtk_check_button_set_active (GTK_CHECK_BUTTON (radio_button), TRUE);
    }
  options->fill_type = initial_fill;

  gimp_dialog_add_button (options->query_box, "OK",
			  G_CALLBACK (new_layer_query_ok_callback), options, TRUE);
  gimp_dialog_add_button (options->query_box, "Cancel",
			  G_CALLBACK (new_layer_query_cancel_callback), options, FALSE);

  gtk_window_present (GTK_WINDOW (options->query_box));
}


/*
 *  The edit layer attributes dialog
 */

typedef struct _EditLayerOptions EditLayerOptions;

struct _EditLayerOptions {
  GtkWidget *query_box;
  GtkWidget *name_entry;
  int        layer_ID;
};

static void
edit_layer_query_ok_callback (GtkWidget *w,
			      gpointer   client_data)
{
  EditLayerOptions *options;
  Layer *layer;

  options = (EditLayerOptions *) client_data;

  if ((layer = layer_get_ID (options->layer_ID)))
    {
      /*  Set the new layer name  */
      if (GIMP_DRAWABLE(layer)->name)
	{
	  /*  If the layer is a floating selection, make it a channel  */
	  if (layer_is_floating_sel (layer))
	    {
	      floating_sel_to_layer (layer);
	    }

	  g_free (GIMP_DRAWABLE(layer)->name);
	}
      GIMP_DRAWABLE(layer)->name = g_strdup (gtk_editable_get_text (GTK_EDITABLE (options->name_entry)));
    }

  gdisplays_flush ();

  gtk_window_destroy (GTK_WINDOW (options->query_box));

  g_free (options);
}

static void
edit_layer_query_cancel_callback (GtkWidget *w,
				  gpointer   client_data)
{
  EditLayerOptions *options;

  options = (EditLayerOptions *) client_data;
  gtk_window_destroy (GTK_WINDOW (options->query_box));
  g_free (options);
}

static gboolean
edit_layer_query_delete_callback (GtkWindow *w,
				  gpointer   client_data)
{
  edit_layer_query_cancel_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}

static void
layers_dialog_edit_layer_query (LayerWidget *layer_widget)
{
  EditLayerOptions *options;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *label;

  /*  the new options structure  */
  options = (EditLayerOptions *) g_malloc (sizeof (EditLayerOptions));
  options->layer_ID = drawable_ID (GIMP_DRAWABLE (layer_widget->layer));
  /*  the dialog  */
  options->query_box = lc_query_dialog_new ("Edit Layer Attributes",
					    G_CALLBACK (edit_layer_query_delete_callback),
					    options);

  /*  the main vbox  */
  vbox = lc_query_dialog_vbox (options->query_box, 2);

  /*  the name entry hbox, label and entry  */
  hbox = gimp_hbox_new (FALSE, 1);
  gimp_box_pack_start (vbox, hbox, FALSE, FALSE, 0);
  label = gtk_label_new ("Layer name:");
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);
  options->name_entry = gtk_entry_new ();
  gimp_box_pack_start (hbox, options->name_entry, TRUE, TRUE, 0);
  gtk_editable_set_text (GTK_EDITABLE (options->name_entry),
			 ((layer_is_floating_sel (layer_widget->layer) ?
			   "Floating Selection" : GIMP_DRAWABLE(layer_widget->layer)->name)));

  gimp_dialog_add_button (options->query_box, "OK",
			  G_CALLBACK (edit_layer_query_ok_callback), options, TRUE);
  gimp_dialog_add_button (options->query_box, "Cancel",
			  G_CALLBACK (edit_layer_query_cancel_callback), options, FALSE);

  gtk_window_present (GTK_WINDOW (options->query_box));
}


/*
 *  The add mask query dialog
 */

typedef struct _AddMaskOptions AddMaskOptions;

struct _AddMaskOptions {
  GtkWidget *query_box;
  Layer * layer;
  AddMaskType add_mask_type;
};

static void
add_mask_query_ok_callback (GtkWidget *w,
			    gpointer   client_data)
{
  AddMaskOptions *options;
  GImage *gimage;
  LayerMask *mask;
  Layer *layer;

  options = (AddMaskOptions *) client_data;
  if ((layer =  (options->layer)) &&
      (gimage = gimage_get_ID (GIMP_DRAWABLE(layer)->gimage_ID)))
    {
      mask = layer_create_mask (layer, options->add_mask_type);
      gimage_add_layer_mask (gimage, layer, mask);
      gdisplays_flush ();
    }

  gtk_window_destroy (GTK_WINDOW (options->query_box));
  g_free (options);
}

static void
add_mask_query_cancel_callback (GtkWidget *w,
				gpointer   client_data)
{
  AddMaskOptions *options;

  options = (AddMaskOptions *) client_data;
  gtk_window_destroy (GTK_WINDOW (options->query_box));
  g_free (options);
}

static gboolean
add_mask_query_delete_callback (GtkWindow *w,
				gpointer   client_data)
{
  add_mask_query_cancel_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}

static void
fill_white_callback (GtkWidget *w,
		     gpointer   client_data)
{
  AddMaskOptions *options;

  if (!gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    return;
  options = (AddMaskOptions *) client_data;
  options->add_mask_type = WhiteMask;
}

static void
fill_black_callback (GtkWidget *w,
		     gpointer   client_data)
{
  AddMaskOptions *options;

  if (!gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    return;
  options = (AddMaskOptions *) client_data;
  options->add_mask_type = BlackMask;
}

static void
fill_alpha_callback (GtkWidget *w,
		     gpointer   client_data)
{
  AddMaskOptions *options;

  if (!gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    return;
  options = (AddMaskOptions *) client_data;
  options->add_mask_type = AlphaMask;
}

static void
layers_dialog_add_mask_query (Layer *layer)
{
  AddMaskOptions *options;
  GtkWidget *vbox;
  GtkWidget *label;
  GtkWidget *radio_frame;
  GtkWidget *radio_box;
  GtkWidget *radio_button;
  int i;
  char *button_names[3] =
  {
    "White (Full Opacity)",
    "Black (Full Transparency)",
    "Layer's Alpha Channel"
  };
  void (* button_callbacks[3]) (GtkWidget *, gpointer) =
  {
    fill_white_callback,
    fill_black_callback,
    fill_alpha_callback
  };

  /*  the new options structure  */
  options = (AddMaskOptions *) g_malloc (sizeof (AddMaskOptions));
  options->layer = layer;
  options->add_mask_type = WhiteMask;

  /*  the dialog  */
  options->query_box = lc_query_dialog_new ("Add Mask Options",
					    G_CALLBACK (add_mask_query_delete_callback),
					    options);

  /*  the main vbox  */
  vbox = lc_query_dialog_vbox (options->query_box, 2);

  /*  the name entry hbox, label and entry  */
  label = gtk_label_new ("Initialize Layer Mask To:");
  gimp_box_pack_start (vbox, label, FALSE, FALSE, 0);

  /*  the radio frame and box  */
  radio_frame = gtk_frame_new (NULL);
  gimp_box_pack_start (vbox, radio_frame, FALSE, FALSE, 0);

  radio_box = gimp_vbox_new (FALSE, 1);
  gtk_frame_set_child (GTK_FRAME (radio_frame), radio_box);

  /*  the radio buttons  */
  radio_button = NULL;
  for (i = 0; i < 3; i++)
    {
      radio_button = gimp_radio_button_new (radio_button, button_names[i]);
      gimp_box_pack_start (radio_box, radio_button, FALSE, FALSE, 0);
      g_signal_connect (radio_button, "toggled",
			G_CALLBACK (button_callbacks[i]),
			options);
    }

  gimp_dialog_add_button (options->query_box, "OK",
			  G_CALLBACK (add_mask_query_ok_callback), options, TRUE);
  gimp_dialog_add_button (options->query_box, "Cancel",
			  G_CALLBACK (add_mask_query_cancel_callback), options, FALSE);

  gtk_window_present (GTK_WINDOW (options->query_box));
}


/*
 *  The apply layer mask dialog
 */

typedef struct _ApplyMaskOptions ApplyMaskOptions;

struct _ApplyMaskOptions {
  GtkWidget *query_box;
  Layer * layer;
};

static void
apply_mask_query_apply_callback (GtkWidget *w,
				 gpointer   client_data)
{
  ApplyMaskOptions *options;

  options = (ApplyMaskOptions *) client_data;

  gimage_remove_layer_mask (drawable_gimage (GIMP_DRAWABLE(options->layer)),
			    options->layer, APPLY);
  gtk_window_destroy (GTK_WINDOW (options->query_box));
  g_free (options);
}

static void
apply_mask_query_discard_callback (GtkWidget *w,
				   gpointer   client_data)
{
  ApplyMaskOptions *options;

  options = (ApplyMaskOptions *) client_data;

  gimage_remove_layer_mask (drawable_gimage (GIMP_DRAWABLE(options->layer)),
			    options->layer, DISCARD);
  gtk_window_destroy (GTK_WINDOW (options->query_box));
  g_free (options);
}

static void
apply_mask_query_cancel_callback (GtkWidget *w,
				  gpointer   client_data)
{
  ApplyMaskOptions *options;

  options = (ApplyMaskOptions *) client_data;
  gtk_window_destroy (GTK_WINDOW (options->query_box));
  g_free (options);
}

static gboolean
apply_mask_query_delete_callback (GtkWindow *w,
				  gpointer   client_data)
{
  apply_mask_query_cancel_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}

static void
layers_dialog_apply_mask_query (Layer *layer)
{
  ApplyMaskOptions *options;
  GtkWidget *vbox;
  GtkWidget *label;

  /*  the new options structure  */
  options = (ApplyMaskOptions *) g_malloc (sizeof (ApplyMaskOptions));
  options->layer = layer;

  /*  the dialog  */
  options->query_box = lc_query_dialog_new ("Layer Mask Options",
					    G_CALLBACK (apply_mask_query_delete_callback),
					    options);

  /*  the main vbox  */
  vbox = lc_query_dialog_vbox (options->query_box, 2);

  /*  the name entry hbox, label and entry  */
  label = gtk_label_new ("Apply layer mask?");
  gimp_box_pack_start (vbox, label, FALSE, FALSE, 0);

  gimp_dialog_add_button (options->query_box, "Apply",
			  G_CALLBACK (apply_mask_query_apply_callback), options, TRUE);
  gimp_dialog_add_button (options->query_box, "Discard",
			  G_CALLBACK (apply_mask_query_discard_callback), options, FALSE);
  gimp_dialog_add_button (options->query_box, "Cancel",
			  G_CALLBACK (apply_mask_query_cancel_callback), options, FALSE);

  gtk_window_present (GTK_WINDOW (options->query_box));
}


/*
 *  The scale layer dialog
 */

typedef struct _ScaleLayerOptions ScaleLayerOptions;

struct _ScaleLayerOptions {
  GtkWidget *query_box;
  Layer * layer;

  Resize *resize;
};


static void
scale_layer_query_ok_callback (GtkWidget *w,
			       gpointer   client_data)
{
  ScaleLayerOptions *options;
  GImage *gimage;
  Layer *layer;

  options = (ScaleLayerOptions *) client_data;

  if (options->resize->width > 0 && options->resize->height > 0 &&
      (layer =  (options->layer)))
    {
      if ((gimage = gimage_get_ID (GIMP_DRAWABLE(layer)->gimage_ID)) != NULL)
	{
	  undo_push_group_start (gimage, LAYER_SCALE_UNDO);

	  if (layer_is_floating_sel (layer))
	    floating_sel_relax (layer, TRUE);

	  layer_scale (layer, options->resize->width, options->resize->height, TRUE);

	  if (layer_is_floating_sel (layer))
	    floating_sel_rigor (layer, TRUE);

	  undo_push_group_end (gimage);

	  gdisplays_flush ();
	}

      gtk_window_destroy (GTK_WINDOW (options->query_box));
      resize_widget_free (options->resize);
      g_free (options);
    }
  else
    g_message ("Invalid width or height.  Both must be positive.");
}

static void
scale_layer_query_cancel_callback (GtkWidget *w,
				   gpointer   client_data)
{
  ScaleLayerOptions *options;

  options = (ScaleLayerOptions *) client_data;
  gtk_window_destroy (GTK_WINDOW (options->query_box));
  resize_widget_free (options->resize);
  g_free (options);
}

static gboolean
scale_layer_query_delete_callback (GtkWindow *w,
				   gpointer   client_data)
{
  scale_layer_query_cancel_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}

static void
layers_dialog_scale_layer_query (Layer *layer)
{
  ScaleLayerOptions *options;
  GtkWidget *vbox;

  /*  the new options structure  */
  options = (ScaleLayerOptions *) g_malloc (sizeof (ScaleLayerOptions));
  options->layer = layer;
  options->resize = resize_widget_new (ScaleWidget,
				       drawable_width (GIMP_DRAWABLE(layer)),
				       drawable_height (GIMP_DRAWABLE(layer)));

  /*  the dialog  */
  options->query_box = lc_query_dialog_new ("Scale Layer",
					    G_CALLBACK (scale_layer_query_delete_callback),
					    options);
  gtk_window_set_resizable (GTK_WINDOW (options->query_box), FALSE);

  /*  the main vbox  */
  vbox = lc_query_dialog_vbox (options->query_box, 2);
  gimp_box_pack_start (vbox, options->resize->resize_widget, FALSE, FALSE, 0);

  gimp_dialog_add_button (options->query_box, "OK",
			  G_CALLBACK (scale_layer_query_ok_callback), options, TRUE);
  gimp_dialog_add_button (options->query_box, "Cancel",
			  G_CALLBACK (scale_layer_query_cancel_callback), options, FALSE);

  gtk_window_present (GTK_WINDOW (options->query_box));
}


/*
 *  The resize layer dialog
 */

typedef struct _ResizeLayerOptions ResizeLayerOptions;

struct _ResizeLayerOptions {
  GtkWidget *query_box;
  Layer *layer;

  Resize *resize;
};

static void
resize_layer_query_ok_callback (GtkWidget *w,
				gpointer   client_data)
{
  ResizeLayerOptions *options;
  GImage *gimage;
  Layer *layer;

  options = (ResizeLayerOptions *) client_data;

  if (options->resize->width > 0 && options->resize->height > 0 &&
      (layer = (options->layer)))
    {
      if ((gimage = gimage_get_ID (GIMP_DRAWABLE(layer)->gimage_ID)) != NULL)
	{
	  undo_push_group_start (gimage, LAYER_RESIZE_UNDO);

	  if (layer_is_floating_sel (layer))
	    floating_sel_relax (layer, TRUE);

	  layer_resize (layer,
			options->resize->width, options->resize->height,
			options->resize->off_x, options->resize->off_y);

	  if (layer_is_floating_sel (layer))
	    floating_sel_rigor (layer, TRUE);

	  undo_push_group_end (gimage);

	  gdisplays_flush ();
	}

      gtk_window_destroy (GTK_WINDOW (options->query_box));
      resize_widget_free (options->resize);
      g_free (options);
    }
  else
    g_message ("Invalid width or height.  Both must be positive.");
}

static void
resize_layer_query_cancel_callback (GtkWidget *w,
				    gpointer   client_data)
{
  ResizeLayerOptions *options;

  options = (ResizeLayerOptions *) client_data;
  gtk_window_destroy (GTK_WINDOW (options->query_box));
  resize_widget_free (options->resize);
  g_free (options);
}

static gboolean
resize_layer_query_delete_callback (GtkWindow *w,
				    gpointer   client_data)
{
  resize_layer_query_cancel_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}

static void
layers_dialog_resize_layer_query (Layer *layer)
{
  ResizeLayerOptions *options;
  GtkWidget *vbox;

  /*  the new options structure  */
  options = (ResizeLayerOptions *) g_malloc (sizeof (ResizeLayerOptions));
  options->layer = layer;
  options->resize = resize_widget_new (ResizeWidget,
				       drawable_width (GIMP_DRAWABLE(layer)),
				       drawable_height (GIMP_DRAWABLE(layer)));

  /*  the dialog  */
  options->query_box = lc_query_dialog_new ("Resize Layer",
					    G_CALLBACK (resize_layer_query_delete_callback),
					    options);
  gtk_window_set_resizable (GTK_WINDOW (options->query_box), FALSE);

  /*  the main vbox  */
  vbox = lc_query_dialog_vbox (options->query_box, 2);
  gimp_box_pack_start (vbox, options->resize->resize_widget, FALSE, FALSE, 0);

  gimp_dialog_add_button (options->query_box, "OK",
			  G_CALLBACK (resize_layer_query_ok_callback), options, TRUE);
  gimp_dialog_add_button (options->query_box, "Cancel",
			  G_CALLBACK (resize_layer_query_cancel_callback), options, FALSE);

  gtk_window_present (GTK_WINDOW (options->query_box));
}


/*
 *  The layer merge dialog
 */

typedef struct _LayerMergeOptions LayerMergeOptions;

struct _LayerMergeOptions {
  GtkWidget *query_box;
  int gimage_id;
  int merge_visible;
  MergeType merge_type;
};

static void
layer_merge_query_ok_callback (GtkWidget *w,
			       gpointer   client_data)
{
  LayerMergeOptions *options;
  GImage *gimage;

  options = (LayerMergeOptions *) client_data;
  if (! (gimage = gimage_get_ID (options->gimage_id)))
    return;

  if (options->merge_visible)
    gimage_merge_visible_layers (gimage, options->merge_type);

  gdisplays_flush ();
  gtk_window_destroy (GTK_WINDOW (options->query_box));
  g_free (options);
}

static void
layer_merge_query_cancel_callback (GtkWidget *w,
				   gpointer   client_data)
{
  LayerMergeOptions *options;

  options = (LayerMergeOptions *) client_data;
  gtk_window_destroy (GTK_WINDOW (options->query_box));
  g_free (options);
}

static gboolean
layer_merge_query_delete_callback (GtkWindow *w,
				   gpointer   client_data)
{
  layer_merge_query_cancel_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}

static void
expand_as_necessary_callback (GtkWidget *w,
			      gpointer   client_data)
{
  LayerMergeOptions *options;

  if (!gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    return;
  options = (LayerMergeOptions *) client_data;
  options->merge_type = ExpandAsNecessary;
}

static void
clip_to_image_callback (GtkWidget *w,
			gpointer   client_data)
{
  LayerMergeOptions *options;

  if (!gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    return;
  options = (LayerMergeOptions *) client_data;
  options->merge_type = ClipToImage;
}

static void
clip_to_bottom_layer_callback (GtkWidget *w,
			       gpointer   client_data)
{
  LayerMergeOptions *options;

  if (!gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    return;
  options = (LayerMergeOptions *) client_data;
  options->merge_type = ClipToBottomLayer;
}

void
layers_dialog_layer_merge_query (GImage *gimage,
				 int     merge_visible)  /*  if 0, anchor active layer  */
{
  LayerMergeOptions *options;
  GtkWidget *vbox;
  GtkWidget *label;
  GtkWidget *radio_frame;
  GtkWidget *radio_box;
  GtkWidget *radio_button;
  int i;
  char *button_names[3] =
  {
    "Expanded as necessary",
    "Clipped to image",
    "Clipped to bottom layer"
  };
  void (* button_callbacks[3]) (GtkWidget *, gpointer) =
  {
    expand_as_necessary_callback,
    clip_to_image_callback,
    clip_to_bottom_layer_callback
  };

  /*  the new options structure  */
  options = (LayerMergeOptions *) g_malloc (sizeof (LayerMergeOptions));
  options->gimage_id = gimage->ID;
  options->merge_visible = merge_visible;
  options->merge_type = ExpandAsNecessary;

  /*  the dialog  */
  options->query_box = lc_query_dialog_new ("Layer Merge Options",
					    G_CALLBACK (layer_merge_query_delete_callback),
					    options);

  /*  the main vbox  */
  vbox = lc_query_dialog_vbox (options->query_box, 2);

  /*  the name entry hbox, label and entry  */
  if (merge_visible)
    label = gtk_label_new ("Final, merged layer should be:");
  else
    label = gtk_label_new ("Final, anchored layer should be:");

  gimp_box_pack_start (vbox, label, FALSE, FALSE, 0);

  /*  the radio frame and box  */
  radio_frame = gtk_frame_new (NULL);
  gimp_box_pack_start (vbox, radio_frame, FALSE, FALSE, 0);

  radio_box = gimp_vbox_new (FALSE, 1);
  gtk_frame_set_child (GTK_FRAME (radio_frame), radio_box);

  /*  the radio buttons  */
  radio_button = NULL;
  for (i = 0; i < 3; i++)
    {
      radio_button = gimp_radio_button_new (radio_button, button_names[i]);
      gimp_box_pack_start (radio_box, radio_button, FALSE, FALSE, 0);
      g_signal_connect (radio_button, "toggled",
			G_CALLBACK (button_callbacks[i]),
			options);
    }

  gimp_dialog_add_button (options->query_box, "OK",
			  G_CALLBACK (layer_merge_query_ok_callback), options, TRUE);
  gimp_dialog_add_button (options->query_box, "Cancel",
			  G_CALLBACK (layer_merge_query_cancel_callback), options, FALSE);

  gtk_window_present (GTK_WINDOW (options->query_box));
}
