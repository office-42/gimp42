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
#include "brushes.h"
#include "brush_select.h"
#include "buildmenu.h"
#include "colormaps.h"
#include "disp_callbacks.h"
#include "errors.h"
#include "paint_funcs.h"


#define STD_CELL_WIDTH    24
#define STD_CELL_HEIGHT   24

#define STD_BRUSH_COLUMNS 5
#define STD_BRUSH_ROWS    5

#define MAX_WIN_WIDTH     (STD_CELL_WIDTH * NUM_BRUSH_COLUMNS)
#define MAX_WIN_HEIGHT    (STD_CELL_HEIGHT * NUM_BRUSH_ROWS)
#define MARGIN_WIDTH      3
#define MARGIN_HEIGHT     3

/*  local function prototypes  */
static void brush_popup_open             (BrushSelectP, int, int, GBrushP);
static void brush_popup_close            (BrushSelectP);
static void display_brush                (BrushSelectP, GBrushP, int, int);
static void display_brushes              (BrushSelectP);
static void display_setup                (BrushSelectP);
static void preview_calc_scrollbar       (BrushSelectP);
static void brush_select_show_selected   (BrushSelectP, int, int);
static void update_active_brush_field    (BrushSelectP);
static void brush_select_close_callback  (GtkWidget *, gpointer);
static void brush_select_refresh_callback(GtkWidget *, gpointer);
static void paint_mode_menu_callback     (GtkWidget *, gpointer);
static void brush_select_pressed         (GtkGestureDrag *, double, double, gpointer);
static void brush_select_released        (GtkGestureDrag *, double, double, gpointer);
static gboolean brush_select_scroll      (GtkEventControllerScroll *, double, double, gpointer);
static void brush_select_resize          (GtkDrawingArea *, int, int, gpointer);
static void brush_select_grid_destroy    (GtkWidget *, gpointer);

static gboolean brush_select_delete_callback (GtkWindow *, gpointer);
static void preview_scroll_update        (GtkAdjustment *, gpointer);
static void opacity_scale_update         (GtkAdjustment *, gpointer);
static void spacing_scale_update         (GtkAdjustment *, gpointer);

static void grid_set_size                (BrushSelectP, int, int);
static void grid_draw_row                (BrushSelectP, const guchar *, int, int, int);
static void grid_draw_func               (GtkDrawingArea *, cairo_t *, int, int, gpointer);

/*  the option menu items -- the paint modes  */
static MenuItem option_items[] =
{
  { "Normal", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (NORMAL_MODE), NULL, NULL, 0 },
  { "Dissolve", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (DISSOLVE_MODE), NULL, NULL, 0 },
  { "Behind", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (BEHIND_MODE), NULL, NULL, 0 },
  { "Multiply", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (MULTIPLY_MODE), NULL, NULL, 0 },
  { "Screen", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (SCREEN_MODE), NULL, NULL, 0 },
  { "Overlay", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (OVERLAY_MODE), NULL, NULL, 0 },
  { "Difference", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (DIFFERENCE_MODE), NULL, NULL, 0 },
  { "Addition", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (ADDITION_MODE), NULL, NULL, 0 },
  { "Subtract", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (SUBTRACT_MODE), NULL, NULL, 0 },
  { "Darken Only", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (DARKEN_ONLY_MODE), NULL, NULL, 0 },
  { "Lighten Only", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (LIGHTEN_ONLY_MODE), NULL, NULL, 0 },
  { "Hue", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (HUE_MODE), NULL, NULL, 0 },
  { "Saturation", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (SATURATION_MODE), NULL, NULL, 0 },
  { "Color", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (COLOR_MODE), NULL, NULL, 0 },
  { "Value", 0, 0, paint_mode_menu_callback, GINT_TO_POINTER (VALUE_MODE), NULL, NULL, 0 },
  { NULL, 0, 0, NULL, NULL, NULL, NULL, 0 }
};

static double old_opacity;
static int old_spacing;
static int old_paint_mode;

int NUM_BRUSH_COLUMNS=5;
int NUM_BRUSH_ROWS=5;


/*  Returns a ready option menu (see build_menu) of the paint modes.
 *  Choosing one calls callback with the option menu and the mode as
 *  GINT_TO_POINTER.
 */
GtkWidget *
create_paint_mode_menu (MenuItemCallback callback)
{
  GtkWidget *menu;
  int i;

  for (i = 0; i <= VALUE_MODE; i++)
    option_items[i].callback = callback;

  menu = build_menu (option_items, NULL);

  return menu;
}


BrushSelectP
brush_select_new ()
{
  BrushSelectP bsp;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *sbar;
  GtkWidget *label;
  GtkWidget *util_box;
  GtkWidget *option_menu;
  GtkWidget *slider;
  GtkGesture *drag;
  GtkEventController *scroll;
  GBrushP active;

  bsp = g_malloc (sizeof (_BrushSelect));
  bsp->redraw = TRUE;
  bsp->scroll_offset = 0;
  bsp->grid_surface = NULL;
  bsp->brush_popup = NULL;
  bsp->brush_preview = NULL;

  /*  The shell and main vbox  */
  bsp->shell = gimp_dialog_new ("Brush Selection");
  gtk_window_set_resizable (GTK_WINDOW (bsp->shell), TRUE);
  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, 2);
  gimp_box_pack_start (gimp_dialog_get_vbox (bsp->shell), vbox, TRUE, TRUE, 0);

  /* handle the wm close signal */
  g_signal_connect (bsp->shell, "close-request",
		    G_CALLBACK (brush_select_delete_callback),
		    bsp);

  /*  The horizontal box containing preview & scrollbar & options box */
  hbox = gimp_hbox_new (FALSE, 2);
  gimp_box_pack_start (vbox, hbox, TRUE, TRUE, 0);
  bsp->frame = gtk_frame_new (NULL);
  gimp_box_pack_start (hbox, bsp->frame, TRUE, TRUE, 0);
  bsp->sbar_data = gtk_adjustment_new (0, 0, MAX_WIN_HEIGHT, 1, 1, MAX_WIN_HEIGHT);
  sbar = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, bsp->sbar_data);
  g_signal_connect (bsp->sbar_data, "value-changed",
		    G_CALLBACK (preview_scroll_update), bsp);
  gimp_box_pack_start (hbox, sbar, FALSE, FALSE, 0);


  /*  Create the brush preview window and the underlying image  */
  /*  Get the maximum brush extents  */

  bsp->cell_width = STD_CELL_WIDTH;
  bsp->cell_height = STD_CELL_HEIGHT;

  bsp->preview = gtk_drawing_area_new ();
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (bsp->preview), MAX_WIN_WIDTH);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (bsp->preview), MAX_WIN_HEIGHT);
  gtk_widget_set_hexpand (bsp->preview, TRUE);
  gtk_widget_set_vexpand (bsp->preview, TRUE);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (bsp->preview),
				  grid_draw_func, bsp, NULL);
  grid_set_size (bsp, MAX_WIN_WIDTH, MAX_WIN_HEIGHT);

  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), 1);
  g_signal_connect (drag, "drag-begin",
		    G_CALLBACK (brush_select_pressed), bsp);
  g_signal_connect (drag, "drag-end",
		    G_CALLBACK (brush_select_released), bsp);
  gtk_widget_add_controller (bsp->preview, GTK_EVENT_CONTROLLER (drag));

  scroll = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  g_signal_connect (scroll, "scroll",
		    G_CALLBACK (brush_select_scroll), bsp);
  gtk_widget_add_controller (bsp->preview, scroll);

  g_signal_connect_after (bsp->preview, "resize",
			  G_CALLBACK (brush_select_resize),
			  bsp);
  g_signal_connect (bsp->preview, "destroy",
		    G_CALLBACK (brush_select_grid_destroy), bsp);

  gtk_frame_set_child (GTK_FRAME (bsp->frame), bsp->preview);

  /*  options box  */
  bsp->options_box = gimp_vbox_new (TRUE, 4);
  gimp_box_pack_start (hbox, bsp->options_box, TRUE, TRUE, 0);

  /*  Create the active brush label  */
  util_box = gimp_hbox_new (FALSE, 5);
  gimp_box_pack_start (bsp->options_box, util_box, FALSE, FALSE, 0);

  bsp->brush_name = gtk_label_new ("Active");
  gimp_box_pack_start (util_box, bsp->brush_name, FALSE, FALSE, 2);
  bsp->brush_size = gtk_label_new ("(0x0)");
  gimp_box_pack_start (util_box, bsp->brush_size, FALSE, FALSE, 2);

  /*  Create the paint mode option menu  */
  old_paint_mode = get_brush_paint_mode ();

  util_box = gimp_hbox_new (FALSE, 2);
  gimp_box_pack_start (bsp->options_box, util_box, FALSE, FALSE, 0);
  label = gtk_label_new ("Mode:");
  gimp_box_pack_start (util_box, label, FALSE, FALSE, 2);
  option_menu = create_paint_mode_menu (paint_mode_menu_callback);
  gimp_box_pack_start (util_box, option_menu, FALSE, FALSE, 2);
  if (old_paint_mode >= NORMAL_MODE && old_paint_mode <= VALUE_MODE)
    menu_item_set_active (&option_items[old_paint_mode]);

  /*  Create the opacity scale widget  */
  old_opacity = get_brush_opacity ();

  util_box = gimp_hbox_new (FALSE, 2);
  gimp_box_pack_start (bsp->options_box, util_box, FALSE, FALSE, 0);
  label = gtk_label_new ("Opacity:");
  gimp_box_pack_start (util_box, label, FALSE, FALSE, 2);
  bsp->opacity_data = gtk_adjustment_new (100.0, 0.0, 100.0, 1.0, 1.0, 0.0);
  slider = gimp_hscale_new (bsp->opacity_data, 1);
  gimp_box_pack_start (util_box, slider, TRUE, TRUE, 0);
  g_signal_connect (bsp->opacity_data, "value-changed",
		    G_CALLBACK (opacity_scale_update), bsp);

  /*  Create the brush spacing scale widget  */
  old_spacing = get_brush_spacing ();

  util_box = gimp_hbox_new (FALSE, 2);
  gimp_box_pack_start (bsp->options_box, util_box, FALSE, FALSE, 0);
  label = gtk_label_new ("Spacing:");
  gimp_box_pack_start (util_box, label, FALSE, FALSE, 2);
  bsp->spacing_data = gtk_adjustment_new (0.0, 0.0, 1000.0, 1.0, 1.0, 0.0);
  slider = gimp_hscale_new (bsp->spacing_data, 1);
  gimp_box_pack_start (util_box, slider, TRUE, TRUE, 0);
  g_signal_connect (bsp->spacing_data, "value-changed",
		    G_CALLBACK (spacing_scale_update), bsp);

  /*  The action area  */
  gimp_dialog_add_button (bsp->shell, "Close",
			  G_CALLBACK (brush_select_close_callback),
			  bsp, TRUE);
  gimp_dialog_add_button (bsp->shell, "Refresh",
			  G_CALLBACK (brush_select_refresh_callback),
			  bsp, FALSE);

  gtk_window_present (GTK_WINDOW (bsp->shell));

  /* calculate the scrollbar */
  if(no_data)
    brushes_init(FALSE);
  /* This is done by the resize handler anyway, which is much better */
  preview_calc_scrollbar (bsp);


  /*  render the brushes into the newly created image structure  */
  display_brushes (bsp);

  /*  update the active selection  */
  active = get_active_brush ();
  if (active)
    {
      int old_value = bsp->redraw;
      bsp->redraw = FALSE;
      brush_select_select (bsp, active->index);
      bsp->redraw = old_value;
    }

  return bsp;
}


void
brush_select_select (BrushSelectP bsp,
		     int          index)
{
  int row, col;

  update_active_brush_field (bsp);
  row = index / NUM_BRUSH_COLUMNS;
  col = index - row * NUM_BRUSH_COLUMNS;

  brush_select_show_selected (bsp, row, col);
}


void
brush_select_free (BrushSelectP bsp)
{
  if (bsp)
    {
      if (bsp->brush_popup != NULL)
	{
	  gtk_widget_unparent (bsp->brush_popup);
	  bsp->brush_popup = NULL;
	}

      /*  the dialog's callbacks refer to bsp: take them with it  */
      if (bsp->preview)
	g_signal_handlers_disconnect_by_data (bsp->preview, bsp);
      g_signal_handlers_disconnect_by_data (bsp->sbar_data, bsp);
      g_signal_handlers_disconnect_by_data (bsp->opacity_data, bsp);
      g_signal_handlers_disconnect_by_data (bsp->spacing_data, bsp);
      if (bsp->shell)
	gtk_window_destroy (GTK_WINDOW (bsp->shell));

      if (bsp->grid_surface)
	cairo_surface_destroy (bsp->grid_surface);
      g_free (bsp);
    }
}


/*
 *  The grid: a grayscale image the size of the drawing area, filled a
 *  row at a time the way the GtkPreview it replaces was, and painted as
 *  a whole.
 */
static void
grid_set_size (BrushSelectP bsp,
	       int          width,
	       int          height)
{
  width = MAX (width, 1);
  height = MAX (height, 1);

  if (bsp->grid_surface &&
      bsp->width == width && bsp->height == height)
    return;

  if (bsp->grid_surface)
    cairo_surface_destroy (bsp->grid_surface);

  bsp->grid_surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
						  width, height);
  bsp->width = width;
  bsp->height = height;
}

static void
grid_draw_row (BrushSelectP  bsp,
	       const guchar *src,
	       int           x,
	       int           y,
	       int           w)
{
  guint32 *dest;
  int stride;
  int i;

  if (!bsp->grid_surface || y < 0 || y >= bsp->height)
    return;

  if (x < 0)
    {
      src += -x;
      w += x;
      x = 0;
    }
  if (x + w > bsp->width)
    w = bsp->width - x;
  if (w <= 0)
    return;

  cairo_surface_flush (bsp->grid_surface);
  stride = cairo_image_surface_get_stride (bsp->grid_surface);
  dest = (guint32 *) (cairo_image_surface_get_data (bsp->grid_surface) +
		      y * stride) + x;

  for (i = 0; i < w; i++)
    dest[i] = ((guint32) src[i] << 16) | ((guint32) src[i] << 8) | src[i];

  cairo_surface_mark_dirty (bsp->grid_surface);
}

static void
grid_draw_func (GtkDrawingArea *area,
		cairo_t        *cr,
		int             width,
		int             height,
		gpointer        data)
{
  BrushSelectP bsp = data;

  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_paint (cr);

  if (bsp->grid_surface)
    {
      cairo_set_source_surface (cr, bsp->grid_surface, 0, 0);
      cairo_paint (cr);
    }
}


/*
 *  Local functions
 */
static void
brush_popup_open (BrushSelectP bsp,
		  int          x,
		  int          y,
		  GBrushP      brush)
{
  GdkRectangle rect;
  guchar *src, *buf;

  /* make sure the popup exists and is not visible */
  if (bsp->brush_popup == NULL)
    {
      bsp->brush_popup = gtk_popover_new ();
      gtk_popover_set_has_arrow (GTK_POPOVER (bsp->brush_popup), FALSE);
      gtk_popover_set_autohide (GTK_POPOVER (bsp->brush_popup), FALSE);
      gtk_widget_set_parent (bsp->brush_popup, bsp->preview);
      bsp->brush_preview = gimp_preview_new (GIMP_PREVIEW_GRAYSCALE);
      gtk_popover_set_child (GTK_POPOVER (bsp->brush_popup),
			     bsp->brush_preview);
    }
  else
    {
      gtk_popover_popdown (GTK_POPOVER (bsp->brush_popup));
    }

  /* the popup points at the cell that was clicked */
  rect.x = x;
  rect.y = y;
  rect.width = 1;
  rect.height = 1;
  gtk_popover_set_pointing_to (GTK_POPOVER (bsp->brush_popup), &rect);
  gimp_preview_size (GIMP_PREVIEW (bsp->brush_preview),
		     brush->mask->width, brush->mask->height);

  /*  Draw the brush  */
  buf = g_new (guchar, brush->mask->width);
  src = mask_buf_data (brush->mask);
  for (y = 0; y < brush->mask->height; y++)
    {
      /*  Invert the mask for display.  We're doing this because
       *  a value of 255 in the  mask means it is full intensity.
       *  However, it makes more sense for full intensity to show
       *  up as black in this brush preview window...
       */
      for (x = 0; x < brush->mask->width; x++)
	buf[x] = 255 - src[x];
      gimp_preview_draw_row (GIMP_PREVIEW (bsp->brush_preview), buf, 0, y, brush->mask->width);
      src += brush->mask->width;
    }
  g_free(buf);

  gtk_popover_popup (GTK_POPOVER (bsp->brush_popup));
}


static void
brush_popup_close (BrushSelectP bsp)
{
  if (bsp->brush_popup != NULL)
    gtk_popover_popdown (GTK_POPOVER (bsp->brush_popup));
}

static void
display_brush (BrushSelectP bsp,
	       GBrushP      brush,
	       int          col,
	       int          row)
{
  TempBuf * brush_buf;
  unsigned char * src, *s;
  unsigned char * buf, *b;
  int width, height;
  int offset_x, offset_y;
  int yend;
  int ystart;
  int i, j;

  buf = (unsigned char *) g_malloc (sizeof (char) * bsp->cell_width);

  brush_buf = brush->mask;

  /*  calculate the offset into the image  */
  width = (brush_buf->width > bsp->cell_width) ? bsp->cell_width :
    brush_buf->width;
  height = (brush_buf->height > bsp->cell_height) ? bsp->cell_height :
    brush_buf->height;

  offset_x = col * bsp->cell_width + ((bsp->cell_width - width) >> 1);
  offset_y = row * bsp->cell_height + ((bsp->cell_height - height) >> 1)
    - bsp->scroll_offset;

  ystart = BOUNDS (offset_y, 0, bsp->height);
  yend = BOUNDS (offset_y + height, 0, bsp->height);

  /*  Get the pointer into the brush mask data  */
  src = mask_buf_data (brush_buf) + (ystart - offset_y) * brush_buf->width;

  for (i = ystart; i < yend; i++)
    {
      /*  Invert the mask for display.  We're doing this because
       *  a value of 255 in the  mask means it is full intensity.
       *  However, it makes more sense for full intensity to show
       *  up as black in this brush preview window...
       */
      s = src;
      b = buf;
      for (j = 0; j < width; j++)
	*b++ = 255 - *s++;

      grid_draw_row (bsp, buf, offset_x, i, width);

      src += brush_buf->width;
    }

  g_free (buf);
}


static void
display_setup (BrushSelectP bsp)
{
  unsigned char * buf;
  int i;

  buf = (unsigned char *) g_malloc (sizeof (char) * bsp->width);

  /*  Set the buffer to white  */
  memset (buf, 255, bsp->width);

  /*  Set the image buffer to white  */
  for (i = 0; i < bsp->height; i++)
    grid_draw_row (bsp, buf, 0, i, bsp->width);

  g_free (buf);
}


static void
display_brushes (BrushSelectP bsp)
{
  GSList * list = brush_list;    /*  the global brush list  */
  int row, col;
  GBrushP brush;

  /*  If there are no brushes, insensitize widgets  */
  if (brush_list == NULL)
    {
      gtk_widget_set_sensitive (bsp->options_box, FALSE);
      return;
    }
  /*  Else, sensitize widgets  */
  else
    gtk_widget_set_sensitive (bsp->options_box, TRUE);

  /*  setup the display area  */
  display_setup (bsp);

  row = col = 0;
  while (list)
    {
      brush = (GBrushP) list->data;

      /*  Display the brush  */
      display_brush (bsp, brush, col, row);

      /*  increment the counts  */
      if (++col == NUM_BRUSH_COLUMNS)
	{
	  row ++;
	  col = 0;
	}

      list = g_slist_next (list);
    }

  if (bsp->redraw)
    gtk_widget_queue_draw (bsp->preview);
}


static void
brush_select_show_selected (BrushSelectP bsp,
			    int          row,
			    int          col)
{
  static int old_row = 0;
  static int old_col = 0;
  unsigned char * buf;
  int yend;
  int ystart;
  int offset_x, offset_y;
  int i;

  buf = (unsigned char *) g_malloc (sizeof (char) * bsp->cell_width);

  if (old_col != col || old_row != row)
    {
      /*  remove the old selection  */
      offset_x = old_col * bsp->cell_width;
      offset_y = old_row * bsp->cell_height - bsp->scroll_offset;

      ystart = BOUNDS (offset_y , 0, bsp->height);
      yend = BOUNDS (offset_y + bsp->cell_height, 0, bsp->height);

      /*  set the buf to white  */
      memset (buf, 255, bsp->cell_width);

      for (i = ystart; i < yend; i++)
	{
	  if (i == offset_y || i == (offset_y + bsp->cell_height - 1))
	    grid_draw_row (bsp, buf, offset_x, i, bsp->cell_width);
	  else
	    {
	      grid_draw_row (bsp, buf, offset_x, i, 1);
	      grid_draw_row (bsp, buf, offset_x + bsp->cell_width - 1, i, 1);
	    }
	}
    }

  /*  make the new selection  */
  offset_x = col * bsp->cell_width;
  offset_y = row * bsp->cell_height - bsp->scroll_offset;

  ystart = BOUNDS (offset_y , 0, bsp->height);
  yend = BOUNDS (offset_y + bsp->cell_height, 0, bsp->height);

  /*  set the buf to black  */
  memset (buf, 0, bsp->cell_width);

  for (i = ystart; i < yend; i++)
    {
      if (i == offset_y || i == (offset_y + bsp->cell_height - 1))
	grid_draw_row (bsp, buf, offset_x, i, bsp->cell_width);
      else
	{
	  grid_draw_row (bsp, buf, offset_x, i, 1);
	  grid_draw_row (bsp, buf, offset_x + bsp->cell_width - 1, i, 1);
	}
    }

  /*  the grid is painted whole on the next frame  */
  gtk_widget_queue_draw (bsp->preview);

  old_row = row;
  old_col = col;

  g_free (buf);
}


static void
preview_calc_scrollbar (BrushSelectP bsp)
{
  int num_rows;
  int page_size;
  int max;
  int offs;

  offs = bsp->scroll_offset;
  num_rows = (num_brushes + NUM_BRUSH_COLUMNS - 1) / NUM_BRUSH_COLUMNS;
  max = num_rows * bsp->cell_width;
  if (!num_rows) num_rows = 1;
  page_size = gtk_widget_get_height (bsp->preview);
  if (page_size <= 0)
    page_size = bsp->height;
  page_size = ((page_size < max) ? page_size : max);

  bsp->scroll_offset = offs;
  gtk_adjustment_configure (bsp->sbar_data,
			    bsp->scroll_offset,
			    0, max,
			    bsp->cell_width,
			    (page_size >> 1),
			    page_size);
}

static void
brush_select_resize (GtkDrawingArea *area,
		     int             width,
		     int             height,
		     gpointer        data)
{
  BrushSelectP bsp = data;
  GBrushP active;

  NUM_BRUSH_COLUMNS = MAX (1, width / STD_CELL_WIDTH);
  NUM_BRUSH_ROWS = (num_brushes + NUM_BRUSH_COLUMNS - 1) / NUM_BRUSH_COLUMNS;

  grid_set_size (bsp, width, height);

  /*  recalculate scrollbar extents  */
  preview_calc_scrollbar (bsp);

  /*  render the brushes into the newly created image structure  */
  display_brushes (bsp);

  /*  update the active selection  */
  active = get_active_brush ();
  if (active)
    brush_select_select (bsp, active->index);

  /*  update the display  */
  if (bsp->redraw)
    gtk_widget_queue_draw (bsp->preview);
}

static void
update_active_brush_field (BrushSelectP bsp)
{
  GBrushP brush;
  char buf[32];

  brush = get_active_brush ();

  if (!brush)
    return;

  /*  Set brush name  */
  gtk_label_set_text (GTK_LABEL (bsp->brush_name), brush->name);

  /*  Set brush size  */
  sprintf (buf, "(%d X %d)", brush->mask->width, brush->mask->height);
  gtk_label_set_text (GTK_LABEL (bsp->brush_size), buf);

  /*  Set brush spacing  */
  gtk_adjustment_set_value (bsp->spacing_data, get_brush_spacing ());
}


static void
brush_select_pressed (GtkGestureDrag *gesture,
		      double          x,
		      double          y,
		      gpointer        data)
{
  BrushSelectP bsp = data;
  GBrushP brush;
  int row, col, index;

  col = x / bsp->cell_width;
  row = (y + bsp->scroll_offset) / bsp->cell_height;
  if (col < 0 || col >= NUM_BRUSH_COLUMNS || row < 0)
    return;
  index = row * NUM_BRUSH_COLUMNS + col;

  /*  Get the brush and display the popup brush preview  */
  if ((brush = get_brush_by_index (index)))
    {
      /*  Make this brush the active brush  */
      select_brush (brush);

      /*  Show the brush popup window if the brush is too large  */
      if (brush->mask->width > bsp->cell_width ||
	  brush->mask->height > bsp->cell_height)
	brush_popup_open (bsp, x, y, brush);
    }
}

static void
brush_select_released (GtkGestureDrag *gesture,
		       double          offset_x,
		       double          offset_y,
		       gpointer        data)
{
  /*  Close the brush popup window  */
  brush_popup_close ((BrushSelectP) data);
}

static gboolean
brush_select_scroll (GtkEventControllerScroll *controller,
		     double                    dx,
		     double                    dy,
		     gpointer                  data)
{
  BrushSelectP bsp = data;
  double value;

  value = gtk_adjustment_get_value (bsp->sbar_data) +
    dy * gtk_adjustment_get_step_increment (bsp->sbar_data);
  gtk_adjustment_set_value (bsp->sbar_data, value);

  return TRUE;
}

static void
brush_select_grid_destroy (GtkWidget *widget,
			   gpointer   data)
{
  BrushSelectP bsp = data;

  if (bsp->brush_popup)
    {
      gtk_widget_unparent (bsp->brush_popup);
      bsp->brush_popup = NULL;
    }
  bsp->preview = NULL;
}

static gboolean
brush_select_delete_callback (GtkWindow *w,
			      gpointer   data)
{
  brush_select_close_callback (GTK_WIDGET (w), data);

  return TRUE;
}

static void
brush_select_close_callback (GtkWidget *w,
			     gpointer   client_data)
{
  BrushSelectP bsp;

  bsp = (BrushSelectP) client_data;

  old_paint_mode = get_brush_paint_mode ();
  old_opacity = get_brush_opacity ();
  old_spacing = get_brush_spacing ();

  if (gtk_widget_get_visible (bsp->shell))
    gtk_widget_set_visible (bsp->shell, FALSE);
}


static void
brush_select_refresh_callback (GtkWidget *w,
			       gpointer   client_data)
{
  BrushSelectP bsp;
  GBrushP active;

  bsp = (BrushSelectP) client_data;

  /*  re-init the brush list  */
  brushes_init(FALSE);

  /*  recalculate scrollbar extents  */
  preview_calc_scrollbar (bsp);

  /*  render the brushes into the newly created image structure  */
  display_brushes (bsp);

  /*  update the active selection  */
  active = get_active_brush ();
  if (active)
    brush_select_select (bsp, active->index);

  /*  update the display  */
  if (bsp->redraw)
    gtk_widget_queue_draw (bsp->preview);
}


static void
preview_scroll_update (GtkAdjustment *adjustment,
		       gpointer       data)
{
  BrushSelectP bsp;
  GBrushP active;
  int row, col;

  bsp = data;

  if (bsp && bsp->preview)
    {
      bsp->scroll_offset = gtk_adjustment_get_value (adjustment);
      display_brushes (bsp);

      active = get_active_brush ();
      if (active)
	{
	  row = active->index / NUM_BRUSH_COLUMNS;
	  col = active->index - row * NUM_BRUSH_COLUMNS;
	  brush_select_show_selected (bsp, row, col);
	}

      if (bsp->redraw)
	gtk_widget_queue_draw (bsp->preview);
    }
}

static void
paint_mode_menu_callback (GtkWidget *w,
			  gpointer   client_data)
{
  set_brush_paint_mode (GPOINTER_TO_INT (client_data));
}


static void
opacity_scale_update (GtkAdjustment *adjustment,
		      gpointer       data)
{
  set_brush_opacity (gtk_adjustment_get_value (adjustment) / 100.0);
}


static void
spacing_scale_update (GtkAdjustment *adjustment,
		      gpointer       data)
{
  set_brush_spacing ((int) gtk_adjustment_get_value (adjustment));
}
