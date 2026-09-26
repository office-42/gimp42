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
#include "appenv.h"
#include "colormaps.h"
#include "errors.h"
#include "gdisplay.h"
#include "gimprc.h"
#include "interface.h"
#include "layer_select.h"
#include "layers_dialogP.h"


typedef struct _LayerSelect LayerSelect;

struct _LayerSelect {
  GtkWidget *shell;
  GtkWidget *layer_preview;
  GtkWidget *label;
  cairo_surface_t *layer_pixmap;
  GtkWidget *preview;

  GImage *gimage;
  Layer *current_layer;
  int dirty;
  int image_width, image_height;
  double ratio;
};

/*  layer widget function prototypes  */
static void layer_select_advance (LayerSelect *, int);
static void layer_select_forward (LayerSelect *);
static void layer_select_backward (LayerSelect *);
static void layer_select_end (LayerSelect *, guint32);
static void layer_select_set_gimage (LayerSelect *, GImage *);
static void layer_select_set_layer (LayerSelect *);
static void layer_select_refresh (LayerSelect *);
static gboolean layer_select_key_press (GtkEventControllerKey *, guint, guint,
					GdkModifierType, gpointer);
static void layer_select_key_release (GtkEventControllerKey *, guint, guint,
				      GdkModifierType, gpointer);
static void layer_select_button_press (GtkGestureClick *, int, double, double,
				       gpointer);
static void layer_select_active_notify (GObject *, GParamSpec *, gpointer);
static void preview_draw (GtkDrawingArea *, cairo_t *, int, int, gpointer);
static void preview_redraw (LayerSelect *);

/*
 *  Local variables
 */
LayerSelect *layer_select = NULL;


/**********************/
/*  Public functions  */
/**********************/


void
layer_select_init (GImage  *gimage,
		   int      dir,
		   guint32  time)
{
  GtkWidget *frame1;
  GtkWidget *frame2;
  GtkWidget *hbox;
  GtkEventController *controller;
  GtkGesture *gesture;
  GDisplay *gdisp;

  if (!layer_select)
    {
      layer_select = g_malloc (sizeof (LayerSelect));
      layer_select->layer_pixmap = NULL;
      layer_select->layer_preview = NULL;
      layer_select->preview = NULL;
      layer_select->image_width = layer_select->image_height = 0;
      layer_select_set_gimage (layer_select, gimage);
      layer_select_advance (layer_select, dir);

      if (preview_size)
	{
	  /*  a scratch buffer for render_preview (), never shown  */
	  layer_select->preview = gimp_preview_new (GIMP_PREVIEW_COLOR);
	  g_object_ref_sink (layer_select->preview);
	  gimp_preview_size (GIMP_PREVIEW (layer_select->preview), preview_size, preview_size);
	}

      /*  The shell: a small undecorated window, the GTK 1 popup  */
      layer_select->shell = gtk_window_new ();
      gtk_window_set_title (GTK_WINDOW (layer_select->shell), "Layer Select");
      gtk_window_set_decorated (GTK_WINDOW (layer_select->shell), FALSE);
      gtk_window_set_resizable (GTK_WINDOW (layer_select->shell), FALSE);
      gtk_window_set_hide_on_close (GTK_WINDOW (layer_select->shell), TRUE);

      /*  It takes the keyboard while it is shown: Alt-Tab and Ctrl-Tab
       *  move through the layers, releasing the modifiers picks one.
       */
      controller = gtk_event_controller_key_new ();
      gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
      g_signal_connect (controller, "key-pressed",
			G_CALLBACK (layer_select_key_press), layer_select);
      g_signal_connect (controller, "key-released",
			G_CALLBACK (layer_select_key_release), layer_select);
      gtk_widget_add_controller (layer_select->shell, controller);

      gesture = gtk_gesture_click_new ();
      gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 0);
      g_signal_connect (gesture, "pressed",
			G_CALLBACK (layer_select_button_press), layer_select);
      gtk_widget_add_controller (layer_select->shell, GTK_EVENT_CONTROLLER (gesture));

      /*  Without a keyboard grab, losing the focus ends the selection  */
      g_signal_connect (layer_select->shell, "notify::is-active",
			G_CALLBACK (layer_select_active_notify), layer_select);

      frame1 = gtk_frame_new (NULL);
      gtk_window_set_child (GTK_WINDOW (layer_select->shell), frame1);
      frame2 = gtk_frame_new (NULL);
      gimp_container_set_border_width (frame2, 2);
      gtk_frame_set_child (GTK_FRAME (frame1), frame2);

      hbox = gimp_hbox_new (FALSE, 1);
      gtk_frame_set_child (GTK_FRAME (frame2), hbox);

      /*  The preview  */
      layer_select->layer_preview = gtk_drawing_area_new ();
      gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (layer_select->layer_preview),
					  layer_select->image_width);
      gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (layer_select->layer_preview),
					   layer_select->image_height);
      gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (layer_select->layer_preview),
				      preview_draw, layer_select, NULL);
      gtk_widget_set_halign (layer_select->layer_preview, GTK_ALIGN_CENTER);
      gtk_widget_set_valign (layer_select->layer_preview, GTK_ALIGN_CENTER);
      gimp_box_pack_start (hbox, layer_select->layer_preview, FALSE, FALSE, 0);

      /*  the layer name label */
      layer_select->label = gtk_label_new ("Layer");
      gimp_box_pack_start (hbox, layer_select->label, FALSE, FALSE, 2);
    }
  else
    {
      layer_select_set_gimage (layer_select, gimage);
      layer_select_advance (layer_select, dir);
    }

  layer_select_refresh (layer_select);

  /*  There is no placing a window at the pointer any more; keep it
   *  with the image window instead.
   */
  gdisp = gdisplay_active ();
  if (gdisp && gdisp->shell && GTK_IS_WINDOW (gdisp->shell))
    gtk_window_set_transient_for (GTK_WINDOW (layer_select->shell),
				  GTK_WINDOW (gdisp->shell));

  gtk_window_present (GTK_WINDOW (layer_select->shell));
}

void
layer_select_update_preview_size ()
{
  if (layer_select != NULL)
    {
      if (layer_select->preview)
	gimp_preview_size (GIMP_PREVIEW (layer_select->preview), preview_size, preview_size);
      if (gtk_widget_get_visible (layer_select->shell))
	{
	  layer_select->dirty = TRUE;
	  layer_select_refresh (layer_select);
	}
    }
}


/***********************/
/*  Private functions  */
/***********************/

static void
layer_select_advance (LayerSelect *layer_select,
		      int          dir)
{
  int index;
  int length;
  int count;
  GSList *list;
  GSList *nth;
  Layer *layer;

  index = 0;

  /*  If there is a floating selection, allow no advancement  */
  if (gimage_floating_sel (layer_select->gimage))
    return;

  count = 0;
  list = layer_select->gimage->layer_stack;
  while (list)
    {
      layer = (Layer *) list->data;
      if (layer == layer_select->current_layer)
	index = count;
      count++;
      list = g_slist_next (list);
    }

  length = g_slist_length (layer_select->gimage->layer_stack);

  if (dir == 1)
    index = (index == length - 1) ? 0 : (index + 1);
  else
    index = (index == 0) ? (length - 1) : (index - 1);

  nth = g_slist_nth (layer_select->gimage->layer_stack, index);

  if (nth)
    {
      layer = (Layer *) nth->data;
      layer_select->current_layer = layer;
    }
}


static void
layer_select_forward (LayerSelect *layer_select)
{
  layer_select_advance (layer_select, 1);
  layer_select->dirty = TRUE;
  layer_select_refresh (layer_select);
}


static void
layer_select_backward (LayerSelect *layer_select)
{
  layer_select_advance (layer_select, -1);
  layer_select->dirty = TRUE;
  layer_select_refresh (layer_select);
}


static void
layer_select_end (LayerSelect *layer_select,
		  guint32      time)
{
  if (!gtk_widget_get_visible (layer_select->shell))
    return;

  gtk_widget_set_visible (layer_select->shell, FALSE);

  /*  only reset the active layer if a new layer was specified  */
  if (layer_select->current_layer != layer_select->gimage->active_layer)
    {
      gimage_set_active_layer (layer_select->gimage, layer_select->current_layer);
      gdisplays_flush ();
    }
}


static void
layer_select_set_gimage (LayerSelect *layer_select,
			 GImage      *gimage)
{
  int image_width, image_height;

  layer_select->gimage = gimage;
  layer_select->current_layer = gimage->active_layer;
  layer_select->dirty = TRUE;

  /*  Get the image width and height variables, based on the gimage  */
  if (gimage->width > gimage->height)
    layer_select->ratio = (double) preview_size / (double) gimage->width;
  else
    layer_select->ratio = (double) preview_size / (double) gimage->height;

  image_width = (int) (layer_select->ratio * gimage->width);
  image_height = (int) (layer_select->ratio * gimage->height);

  if (layer_select->image_width != image_width ||
      layer_select->image_height != image_height)
    {
      layer_select->image_width = image_width;
      layer_select->image_height = image_height;

      if (layer_select->layer_preview)
	{
	  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (layer_select->layer_preview),
					      layer_select->image_width);
	  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (layer_select->layer_preview),
					       layer_select->image_height);
	}

      if (layer_select->layer_pixmap)
	{
	  cairo_surface_destroy (layer_select->layer_pixmap);
	  layer_select->layer_pixmap = NULL;
	}
    }
}


static void
layer_select_set_layer (LayerSelect *layer_select)
{
  Layer *layer;

  if (! (layer =  (layer_select->current_layer)))
    return;

  /*  Set the layer label  */
  gtk_label_set_text (GTK_LABEL (layer_select->label), drawable_name (GIMP_DRAWABLE(layer)));
}


/*  Brings the preview and the label up to date with the current layer
 *  (what the GTK 1 version did on its next expose).
 */
static void
layer_select_refresh (LayerSelect *layer_select)
{
  if (layer_select->dirty)
    {
      /*  If a preview exists, draw it  */
      if (preview_size)
	preview_redraw (layer_select);

      /*  Change the layer name label  */
      layer_select_set_layer (layer_select);

      layer_select->dirty = FALSE;
    }

  if (layer_select->layer_preview)
    gtk_widget_queue_draw (layer_select->layer_preview);
}


static gboolean
layer_select_key_press (GtkEventControllerKey *controller,
			guint                  keyval,
			guint                  keycode,
			GdkModifierType        state,
			gpointer               data)
{
  LayerSelect *ls = data;

  switch (keyval)
    {
    case GDK_KEY_Tab:
    case GDK_KEY_ISO_Left_Tab:
      if (state & GDK_ALT_MASK)
	layer_select_forward (ls);
      else if (state & GDK_CONTROL_MASK)
	layer_select_backward (ls);
      break;
    }

  return TRUE;
}


static void
layer_select_key_release (GtkEventControllerKey *controller,
			  guint                  keyval,
			  guint                  keycode,
			  GdkModifierType        state,
			  gpointer               data)
{
  LayerSelect *ls = data;

  switch (keyval)
    {
    case GDK_KEY_Alt_L: case GDK_KEY_Alt_R:
    case GDK_KEY_Meta_L: case GDK_KEY_Meta_R:
      state &= ~GDK_ALT_MASK;
      break;
    case GDK_KEY_Control_L: case GDK_KEY_Control_R:
      state &= ~GDK_CONTROL_MASK;
      break;
    }

  if (! (state & (GDK_ALT_MASK | GDK_CONTROL_MASK)))
    layer_select_end (ls, GDK_CURRENT_TIME);
}


static void
layer_select_button_press (GtkGestureClick *gesture,
			   int              n_press,
			   double           x,
			   double           y,
			   gpointer         data)
{
  layer_select_end ((LayerSelect *) data, GDK_CURRENT_TIME);
}


static void
layer_select_active_notify (GObject    *object,
			    GParamSpec *pspec,
			    gpointer    data)
{
  if (! gtk_window_is_active (GTK_WINDOW (object)))
    layer_select_end ((LayerSelect *) data, GDK_CURRENT_TIME);
}


static void
preview_draw (GtkDrawingArea *area,
	      cairo_t        *cr,
	      int             width,
	      int             height,
	      gpointer        data)
{
  LayerSelect *ls = data;

  if (!preview_size || !ls->current_layer)
    return;

  if (layer_is_floating_sel (ls->current_layer))
    render_fs_preview (GTK_WIDGET (area), cr, ls->image_width, ls->image_height);
  else if (ls->layer_pixmap)
    {
      cairo_set_source_surface (cr, ls->layer_pixmap, 0, 0);
      cairo_paint (cr);
    }
}


static void
preview_redraw (LayerSelect *layer_select)
{
  Layer * layer;
  TempBuf * preview_buf;
  int w, h;
  int offx, offy;

  if (! (layer =  (layer_select->current_layer)))
    return;

  /*  a floating selection is drawn as an icon by preview_draw ()  */
  if (layer_is_floating_sel (layer) || !layer_select->preview)
    return;
  else
    {
      int off_x, off_y;
      drawable_offsets (GIMP_DRAWABLE(layer), &off_x, &off_y);
      /*  determine width and height  */
      w = (int) (layer_select->ratio * drawable_width (GIMP_DRAWABLE(layer)));
      h = (int) (layer_select->ratio * drawable_height (GIMP_DRAWABLE(layer)));
      offx = (int) (layer_select->ratio * off_x);
      offy = (int) (layer_select->ratio * off_y);

      preview_buf = layer_preview (layer, w, h);
      if (!preview_buf)
	return;
      preview_buf->x = offx;
      preview_buf->y = offy;

      render_preview (preview_buf,
		      layer_select->preview,
		      layer_select->image_width,
		      layer_select->image_height,
		      -1);

      /*  Set the layer pixmap  */
      if (layer_select->layer_pixmap)
	cairo_surface_destroy (layer_select->layer_pixmap);
      layer_select->layer_pixmap = render_preview_surface (layer_select->preview,
							   layer_select->image_width,
							   layer_select->image_height);
    }
}
