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
#include <stdio.h>
#include <string.h>
#include "appenv.h"
#include "patterns.h"
#include "pattern_select.h"
#include "buildmenu.h"
#include "colormaps.h"
#include "disp_callbacks.h"
#include "errors.h"
#include "paint_funcs.h"


#define MIN_CELL_SIZE    32
#define MAX_CELL_SIZE    45


/*
#define STD_PATTERN_COLUMNS 6
#define STD_PATTERN_ROWS    5
*/

#define MAX_WIN_WIDTH     (MIN_CELL_SIZE * NUM_PATTERN_COLUMNS)
#define MAX_WIN_HEIGHT    (MIN_CELL_SIZE * NUM_PATTERN_ROWS)
#define MARGIN_WIDTH      1
#define MARGIN_HEIGHT     1

/*  local function prototypes  */
static void pattern_popup_open               (PatternSelectP, int, int, GPatternP);
static void pattern_popup_close              (PatternSelectP);
static void display_pattern                  (PatternSelectP, GPatternP, int, int);
static void display_patterns                 (PatternSelectP);
static void display_setup                    (PatternSelectP);
static void draw_preview                     (PatternSelectP);
static void preview_calc_scrollbar           (PatternSelectP);
static void pattern_select_show_selected     (PatternSelectP, int, int);
static void update_active_pattern_field      (PatternSelectP);
static void pattern_select_close_callback    (GtkWidget *, gpointer);
static gboolean pattern_select_delete_callback (GtkWindow *, gpointer);
static void pattern_select_refresh_callback  (GtkWidget *, gpointer);
static void pattern_select_pressed           (GtkGestureDrag *, double, double, gpointer);
static void pattern_select_released          (GtkGestureDrag *, double, double, gpointer);
static gboolean pattern_select_scroll        (GtkEventControllerScroll *, double, double, gpointer);
static void pattern_select_resize            (GtkDrawingArea *, int, int, gpointer);
static void pattern_select_grid_destroy      (GtkWidget *, gpointer);
static void pattern_select_scroll_update     (GtkAdjustment *, gpointer);

static void grid_set_size                    (PatternSelectP, int, int);
static void grid_draw_row                    (PatternSelectP, const guchar *, int, int, int);
static void grid_draw_func                   (GtkDrawingArea *, cairo_t *, int, int, gpointer);

gint NUM_PATTERN_COLUMNS = 6;
gint NUM_PATTERN_ROWS    = 5;
gint STD_CELL_SIZE = MIN_CELL_SIZE;

PatternSelectP
pattern_select_new ()
{
  PatternSelectP psp;
  GPatternP active;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *sbar;
  GtkWidget *label_box;
  GtkGesture *drag;
  GtkEventController *scroll;

  psp = g_malloc (sizeof (_PatternSelect));
  psp->preview = NULL;
  psp->grid_surface = NULL;
  psp->pattern_popup = NULL;
  psp->pattern_preview = NULL;
  psp->scroll_offset = 0;

  /*  The shell and main vbox  */
  psp->shell = gimp_dialog_new ("Pattern Selection");
  gtk_window_set_resizable (GTK_WINDOW (psp->shell), TRUE);
  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, 1);
  gimp_box_pack_start (gimp_dialog_get_vbox (psp->shell), vbox, TRUE, TRUE, 0);

  /* handle the wm close event */
  g_signal_connect (psp->shell, "close-request",
		    G_CALLBACK (pattern_select_delete_callback),
		    psp);

  psp->options_box = gimp_vbox_new (FALSE, 1);
  gimp_box_pack_start (vbox, psp->options_box, FALSE, FALSE, 0);

  /*  Create the active pattern label  */
  label_box = gimp_hbox_new (FALSE, 1);
  gimp_container_set_border_width (label_box, 2);
  gimp_box_pack_start (psp->options_box, label_box, FALSE, FALSE, 0);
  psp->pattern_name = gtk_label_new ("Active");
  gimp_box_pack_start (label_box, psp->pattern_name, FALSE, FALSE, 2);
  psp->pattern_size = gtk_label_new ("(0x0)");
  gimp_box_pack_start (label_box, psp->pattern_size, FALSE, FALSE, 5);

  /*  The horizontal box containing preview & scrollbar  */
  hbox = gimp_hbox_new (FALSE, 1);
  gimp_box_pack_start (vbox, hbox, TRUE, TRUE, 0);
  psp->frame = gtk_frame_new (NULL);

  gimp_box_pack_start (hbox, psp->frame, TRUE, TRUE, 0);

  psp->sbar_data = gtk_adjustment_new (0, 0, MAX_WIN_HEIGHT, 1, 1, MAX_WIN_HEIGHT);
  g_signal_connect (psp->sbar_data, "value-changed",
		    G_CALLBACK (pattern_select_scroll_update),
		    psp);
  sbar = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, psp->sbar_data);
  gimp_box_pack_start (hbox, sbar, FALSE, FALSE, 0);

  /*  Create the pattern preview window and the underlying image  */
  psp->cell_width = STD_CELL_SIZE;
  psp->cell_height = STD_CELL_SIZE;

  psp->preview = gtk_drawing_area_new ();
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (psp->preview), MAX_WIN_WIDTH);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (psp->preview), MAX_WIN_HEIGHT);
  gtk_widget_set_hexpand (psp->preview, TRUE);
  gtk_widget_set_vexpand (psp->preview, TRUE);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (psp->preview),
				  grid_draw_func, psp, NULL);
  grid_set_size (psp, MAX_WIN_WIDTH, MAX_WIN_HEIGHT);

  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), 1);
  g_signal_connect (drag, "drag-begin",
		    G_CALLBACK (pattern_select_pressed), psp);
  g_signal_connect (drag, "drag-end",
		    G_CALLBACK (pattern_select_released), psp);
  gtk_widget_add_controller (psp->preview, GTK_EVENT_CONTROLLER (drag));

  scroll = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  g_signal_connect (scroll, "scroll",
		    G_CALLBACK (pattern_select_scroll), psp);
  gtk_widget_add_controller (psp->preview, scroll);

  g_signal_connect_after (psp->preview, "resize",
			  G_CALLBACK (pattern_select_resize),
			  psp);
  g_signal_connect (psp->preview, "destroy",
		    G_CALLBACK (pattern_select_grid_destroy), psp);

  gtk_frame_set_child (GTK_FRAME (psp->frame), psp->preview);

  /*  The action area  */
  gimp_dialog_add_button (psp->shell, "Close",
			  G_CALLBACK (pattern_select_close_callback),
			  psp, TRUE);
  gimp_dialog_add_button (psp->shell, "Refresh",
			  G_CALLBACK (pattern_select_refresh_callback),
			  psp, FALSE);

  gtk_window_present (GTK_WINDOW (psp->shell));

  if(no_data)   /* if patterns are already loaded, dont do it now... */
    patterns_init(FALSE);
  preview_calc_scrollbar (psp);
  display_patterns (psp);


  /*  update the active selection  */
  active = get_active_pattern ();
  if (active)
    pattern_select_select (psp, active->index);

  return psp;
}

void
pattern_select_select (PatternSelectP psp,
		       int            index)
{
  int row, col;

  update_active_pattern_field (psp);
  row = index / NUM_PATTERN_COLUMNS;
  col = index - row * NUM_PATTERN_COLUMNS;

  pattern_select_show_selected (psp, row, col);
}

void
pattern_select_free (PatternSelectP psp)
{
  if (psp)
    {
      if (psp->pattern_popup != NULL)
	{
	  gtk_widget_unparent (psp->pattern_popup);
	  psp->pattern_popup = NULL;
	}

      /*  the dialog's callbacks refer to psp: take them with it  */
      if (psp->preview)
	g_signal_handlers_disconnect_by_data (psp->preview, psp);
      g_signal_handlers_disconnect_by_data (psp->sbar_data, psp);
      if (psp->shell)
	gtk_window_destroy (GTK_WINDOW (psp->shell));

      if (psp->grid_surface)
	cairo_surface_destroy (psp->grid_surface);
      g_free (psp);
    }
}

/*
 *  The grid: an RGB image the size of the drawing area, filled a row at a
 *  time the way the GtkPreview it replaces was, and painted as a whole.
 */
static void
grid_set_size (PatternSelectP psp,
	       int            width,
	       int            height)
{
  width = MAX (width, 1);
  height = MAX (height, 1);

  if (psp->grid_surface &&
      psp->width == width && psp->height == height)
    return;

  if (psp->grid_surface)
    cairo_surface_destroy (psp->grid_surface);

  psp->grid_surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
						  width, height);
  psp->width = width;
  psp->height = height;
}

static void
grid_draw_row (PatternSelectP  psp,
	       const guchar   *src,
	       int             x,
	       int             y,
	       int             w)
{
  guint32 *dest;
  int stride;
  int i;

  if (!psp->grid_surface || y < 0 || y >= psp->height)
    return;

  if (x < 0)
    {
      src += -x * 3;
      w += x;
      x = 0;
    }
  if (x + w > psp->width)
    w = psp->width - x;
  if (w <= 0)
    return;

  cairo_surface_flush (psp->grid_surface);
  stride = cairo_image_surface_get_stride (psp->grid_surface);
  dest = (guint32 *) (cairo_image_surface_get_data (psp->grid_surface) +
		      y * stride) + x;

  for (i = 0; i < w; i++, src += 3)
    dest[i] = ((guint32) src[0] << 16) | ((guint32) src[1] << 8) | src[2];

  cairo_surface_mark_dirty (psp->grid_surface);
}

static void
grid_draw_func (GtkDrawingArea *area,
		cairo_t        *cr,
		int             width,
		int             height,
		gpointer        data)
{
  PatternSelectP psp = data;

  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_paint (cr);

  if (psp->grid_surface)
    {
      cairo_set_source_surface (cr, psp->grid_surface, 0, 0);
      cairo_paint (cr);
    }
}

/*
 *  Local functions
 */
static void
pattern_popup_open (PatternSelectP psp,
		    int            x,
		    int            y,
		    GPatternP      pattern)
{
  GdkRectangle rect;
  guchar *src, *buf;

  /* make sure the popup exists and is not visible */
  if (psp->pattern_popup == NULL)
    {
      psp->pattern_popup = gtk_popover_new ();
      gtk_popover_set_has_arrow (GTK_POPOVER (psp->pattern_popup), FALSE);
      gtk_popover_set_autohide (GTK_POPOVER (psp->pattern_popup), FALSE);
      gtk_widget_set_parent (psp->pattern_popup, psp->preview);
      psp->pattern_preview = gimp_preview_new (GIMP_PREVIEW_COLOR);
      gtk_popover_set_child (GTK_POPOVER (psp->pattern_popup),
			     psp->pattern_preview);
    }
  else
    {
      gtk_popover_popdown (GTK_POPOVER (psp->pattern_popup));
    }

  /* the popup points at the cell that was clicked */
  rect.x = x;
  rect.y = y;
  rect.width = 1;
  rect.height = 1;
  gtk_popover_set_pointing_to (GTK_POPOVER (psp->pattern_popup), &rect);
  gimp_preview_size (GIMP_PREVIEW (psp->pattern_preview),
		     pattern->mask->width, pattern->mask->height);

  /*  Draw the pattern  */
  buf = g_new (guchar, pattern->mask->width * 3);
  src = temp_buf_data (pattern->mask);
  for (y = 0; y < pattern->mask->height; y++)
    {
      if (pattern->mask->bytes == 1)
	for (x = 0; x < pattern->mask->width; x++)
	  {
	    buf[x*3+0] = src[x];
	    buf[x*3+1] = src[x];
	    buf[x*3+2] = src[x];
	  }
      else
	for (x = 0; x < pattern->mask->width; x++)
	  {
	    buf[x*3+0] = src[x*3+0];
	    buf[x*3+1] = src[x*3+1];
	    buf[x*3+2] = src[x*3+2];
	  }
      gimp_preview_draw_row (GIMP_PREVIEW (psp->pattern_preview), buf, 0, y, pattern->mask->width);
      src += pattern->mask->width * pattern->mask->bytes;
    }
  g_free(buf);

  gtk_popover_popup (GTK_POPOVER (psp->pattern_popup));
}

static void
pattern_popup_close (PatternSelectP psp)
{
  if (psp->pattern_popup != NULL)
    gtk_popover_popdown (GTK_POPOVER (psp->pattern_popup));
}

static void
display_pattern (PatternSelectP psp,
		 GPatternP      pattern,
		 int            col,
		 int            row)
{
  TempBuf * pattern_buf;
  unsigned char * src, *s;
  unsigned char * buf, *b;
  int cell_width, cell_height;
  int width, height;
  int rowstride;
  int offset_x, offset_y;
  int yend;
  int ystart;
  int i, j;

  buf = (unsigned char *) g_malloc (sizeof (char) * psp->cell_width * 3);

  pattern_buf = pattern->mask;

  /*  calculate the offset into the image  */
  cell_width = psp->cell_width - 2*MARGIN_WIDTH;
  cell_height = psp->cell_height - 2*MARGIN_HEIGHT;
  width = (pattern_buf->width > cell_width) ? cell_width :
    pattern_buf->width;
  height = (pattern_buf->height > cell_height) ? cell_height :
    pattern_buf->height;

  offset_x = col * psp->cell_width + ((cell_width - width) >> 1) + MARGIN_WIDTH;
  offset_y = row * psp->cell_height + ((cell_height - height) >> 1)
    - psp->scroll_offset + MARGIN_HEIGHT;

  ystart = BOUNDS (offset_y, 0, psp->height);
  yend = BOUNDS (offset_y + height, 0, psp->height);

  /*  Get the pointer into the pattern mask data  */
  rowstride = pattern_buf->width * pattern_buf->bytes;
  src = temp_buf_data (pattern_buf) + (ystart - offset_y) * rowstride;

  for (i = ystart; i < yend; i++)
    {
      s = src;
      b = buf;

      if (pattern_buf->bytes == 1)
	for (j = 0; j < width; j++)
	  {
	    *b++ = *s;
	    *b++ = *s;
	    *b++ = *s++;
	  }
      else
	for (j = 0; j < width; j++)
	  {
	    *b++ = *s++;
	    *b++ = *s++;
	    *b++ = *s++;
	  }

      grid_draw_row (psp, buf, offset_x, i, width);

      src += rowstride;
    }

  g_free (buf);
}

static void
display_setup (PatternSelectP psp)
{
  unsigned char * buf;
  int i;

  buf = (unsigned char *) g_malloc (sizeof (char) * psp->width * 3);

  /*  Set the buffer to white  */
  memset (buf, 255, psp->width * 3);

  /*  Set the image buffer to white  */
  for (i = 0; i < psp->height; i++)
    grid_draw_row (psp, buf, 0, i, psp->width);

  g_free (buf);
}

static void
display_patterns (PatternSelectP psp)
{
  GSList *list = pattern_list;    /*  the global pattern list  */
  int row, col;
  GPatternP pattern;

  /*  If there are no patterns, insensitize widgets  */
  if (pattern_list == NULL)
    {
      gtk_widget_set_sensitive (psp->options_box, FALSE);
      return;
    }
  /*  Else, sensitize widgets  */
  else
    gtk_widget_set_sensitive (psp->options_box, TRUE);

  /*  setup the display area  */
  display_setup (psp);

  row = col = 0;
  while (list)
    {
      pattern = (GPatternP) list->data;

      /*  Display the pattern  */
      display_pattern (psp, pattern, col, row);

      /*  increment the counts  */
      if (++col == NUM_PATTERN_COLUMNS)
	{
	  row ++;
	  col = 0;
	}

      list = g_slist_next (list);
    }

  gtk_widget_queue_draw (psp->preview);
}

static void
pattern_select_show_selected (PatternSelectP psp,
			      int            row,
			      int            col)
{
  static int old_row = 0;
  static int old_col = 0;
  unsigned char * buf;
  int yend;
  int ystart;
  int offset_x, offset_y;
  int i;

  buf = (unsigned char *) g_malloc (sizeof (char) * psp->cell_width * 3);

  if (old_col != col || old_row != row)
    {
      /*  remove the old selection  */
      offset_x = old_col * psp->cell_width;
      offset_y = old_row * psp->cell_height - psp->scroll_offset;

      ystart = BOUNDS (offset_y , 0, psp->height);
      yend = BOUNDS (offset_y + psp->cell_height, 0, psp->height);

      /*  set the buf to white  */
      memset (buf, 255, psp->cell_width * 3);

      for (i = ystart; i < yend; i++)
	{
	  if (i == offset_y || i == (offset_y + psp->cell_height - 1))
	    grid_draw_row (psp, buf, offset_x, i, psp->cell_width);
	  else
	    {
	      grid_draw_row (psp, buf, offset_x, i, 1);
	      grid_draw_row (psp, buf, offset_x + psp->cell_width - 1, i, 1);
	    }
	}
    }

  /*  make the new selection  */
  offset_x = col * psp->cell_width;
  offset_y = row * psp->cell_height - psp->scroll_offset;

  ystart = BOUNDS (offset_y , 0, psp->height);
  yend = BOUNDS (offset_y + psp->cell_height, 0, psp->height);

  /*  set the buf to black  */
  memset (buf, 0, psp->cell_width * 3);

  for (i = ystart; i < yend; i++)
    {
      if (i == offset_y || i == (offset_y + psp->cell_height - 1))
	grid_draw_row (psp, buf, offset_x, i, psp->cell_width);
      else
	{
	  grid_draw_row (psp, buf, offset_x, i, 1);
	  grid_draw_row (psp, buf, offset_x + psp->cell_width - 1, i, 1);
	}
    }

  gtk_widget_queue_draw (psp->preview);

  old_row = row;
  old_col = col;

  g_free (buf);
}

static void
draw_preview (PatternSelectP psp)
{
  /*  Draw the image buf to the preview window  */
  gtk_widget_queue_draw (psp->preview);
}

static void
preview_calc_scrollbar (PatternSelectP psp)
{
  int num_rows;
  int page_size;
  int max;

  psp->scroll_offset = 0;
  num_rows = (num_patterns + NUM_PATTERN_COLUMNS - 1) / NUM_PATTERN_COLUMNS;
  max = num_rows * psp->cell_width;
  if (!num_rows) num_rows = 1;
  page_size = gtk_widget_get_height (psp->preview);
  if (page_size <= 0)
    page_size = psp->height;

  gtk_adjustment_configure (psp->sbar_data,
			    psp->scroll_offset,
			    0, max,
			    psp->cell_width,
			    (page_size >> 1),
			    (page_size < max) ? page_size : max);
}

static void
update_active_pattern_field (PatternSelectP psp)
{
  GPatternP pattern;
  char buf[32];

  pattern = get_active_pattern ();

  if (!pattern)
    return;

  /*  Set pattern name  */
  gtk_label_set_text (GTK_LABEL (psp->pattern_name), pattern->name);

  /*  Set pattern size  */
  sprintf (buf, "(%d X %d)", pattern->mask->width, pattern->mask->height);
  gtk_label_set_text (GTK_LABEL (psp->pattern_size), buf);
}

static void
pattern_select_resize (GtkDrawingArea *area,
		       int             width,
		       int             height,
		       gpointer        data)
{
  PatternSelectP psp = data;
  GPatternP active;
/* calculate the best-fit approximation... */
  gint wid;
  gint now;

  wid = width;
  if (wid < MIN_CELL_SIZE)
    wid = MIN_CELL_SIZE;

  for(now = MIN_CELL_SIZE, STD_CELL_SIZE = MIN_CELL_SIZE;
      now < MAX_CELL_SIZE; ++now)
    {
      if ((wid % now) < (wid % STD_CELL_SIZE)) STD_CELL_SIZE = now;
      if ((wid % STD_CELL_SIZE) == 0)
        break;
    }

  NUM_PATTERN_COLUMNS = wid / STD_CELL_SIZE;
  NUM_PATTERN_ROWS = (gint) (num_patterns + NUM_PATTERN_COLUMNS-1) / NUM_PATTERN_COLUMNS;

  psp->cell_width = STD_CELL_SIZE;
  psp->cell_height = STD_CELL_SIZE;

  grid_set_size (psp, width, height);

  /*  recalculate scrollbar extents  */
  preview_calc_scrollbar (psp);

  /*  render the patterns into the newly created image structure  */
  display_patterns (psp);

  /*  update the active selection  */
  active = get_active_pattern ();
  if (active)
    pattern_select_select (psp, active->index);

  /*  update the display  */
  draw_preview (psp);
}

static void
pattern_select_pressed (GtkGestureDrag *gesture,
			double          x,
			double          y,
			gpointer        data)
{
  PatternSelectP psp = data;
  GPatternP pattern;
  int row, col, index;

  col = x / psp->cell_width;
  row = (y + psp->scroll_offset) / psp->cell_height;
  if (col < 0 || col >= NUM_PATTERN_COLUMNS || row < 0)
    return;
  index = row * NUM_PATTERN_COLUMNS + col;

  /*  Get the pattern and display the popup pattern preview  */
  if ((pattern = get_pattern_by_index (index)))
    {
      /*  Make this pattern the active pattern  */
      select_pattern (pattern);
      /*  Show the pattern popup window if the pattern is too large  */
      if (pattern->mask->width > psp->cell_width ||
	  pattern->mask->height > psp->cell_height)
	pattern_popup_open (psp, x, y, pattern);
    }
}

static void
pattern_select_released (GtkGestureDrag *gesture,
			 double          offset_x,
			 double          offset_y,
			 gpointer        data)
{
  /*  Close the pattern popup window  */
  pattern_popup_close ((PatternSelectP) data);
}

static gboolean
pattern_select_scroll (GtkEventControllerScroll *controller,
		       double                    dx,
		       double                    dy,
		       gpointer                  data)
{
  PatternSelectP psp = data;
  double value;

  value = gtk_adjustment_get_value (psp->sbar_data) +
    dy * gtk_adjustment_get_step_increment (psp->sbar_data);
  gtk_adjustment_set_value (psp->sbar_data, value);

  return TRUE;
}

static void
pattern_select_grid_destroy (GtkWidget *widget,
			     gpointer   data)
{
  PatternSelectP psp = data;

  if (psp->pattern_popup)
    {
      gtk_widget_unparent (psp->pattern_popup);
      psp->pattern_popup = NULL;
    }
  psp->preview = NULL;
}

static gboolean
pattern_select_delete_callback (GtkWindow *w,
				gpointer   client_data)
{
  pattern_select_close_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}

static void
pattern_select_close_callback (GtkWidget *w,
			       gpointer   client_data)
{
  PatternSelectP psp;
  psp = (PatternSelectP) client_data;
  if (gtk_widget_get_visible (psp->shell))
    gtk_widget_set_visible (psp->shell, FALSE);
}

static void
pattern_select_refresh_callback (GtkWidget *w,
				 gpointer   client_data)
{
  PatternSelectP psp;
  GPatternP active;

  psp = (PatternSelectP) client_data;

  /*  re-init the pattern list  */
  patterns_init (FALSE);

  /*  recalculate scrollbar extents  */
  preview_calc_scrollbar (psp);

  /*  render the patterns into the newly created image structure  */
  display_patterns (psp);

  /*  update the active selection  */
  active = get_active_pattern ();
  if (active)
    pattern_select_select (psp, active->index);

  /*  update the display  */
  draw_preview (psp);
}

static void
pattern_select_scroll_update (GtkAdjustment *adjustment,
			      gpointer       data)
{
  PatternSelectP psp;
  GPatternP active;
  int row, col;

  psp = data;

  if (psp && psp->preview)
    {
      psp->scroll_offset = gtk_adjustment_get_value (adjustment);
      display_patterns (psp);

      active = get_active_pattern ();
      if (active)
	{
	  row = active->index / NUM_PATTERN_COLUMNS;
	  col = active->index - row * NUM_PATTERN_COLUMNS;
	  pattern_select_show_selected (psp, row, col);
	}

      draw_preview (psp);
    }
}
