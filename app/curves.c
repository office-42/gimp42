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
#include <string.h>
#include <math.h>
#include "appenv.h"
#include "actionarea.h"
#include "buildmenu.h"
#include "colormaps.h"
#include "cursorutil.h"
#include "drawable.h"
#include "general.h"
#include "gdisplay.h"
#include "histogram.h"
#include "image_map.h"
#include "interface.h"
#include "curves.h"

#define ROUND(x)  ((int) ((x) + 0.5))

#define GRAPH              0x1
#define XRANGE_TOP         0x2
#define XRANGE_BOTTOM      0x4
#define YRANGE             0x8
#define DRAW               0x10
#define ALL                0xFF

#define GRAPH_WIDTH      256
#define GRAPH_HEIGHT     256
#define XRANGE_WIDTH     256
#define XRANGE_HEIGHT    16
#define YRANGE_WIDTH     16
#define YRANGE_HEIGHT    256
#define RADIUS           3
#define MIN_DISTANCE     8

#define SMOOTH       0
#define GFREE        1

/*  What happened on the graph  */
#define GRAPH_PRESS    0
#define GRAPH_RELEASE  1
#define GRAPH_MOTION   2

typedef struct _Curves Curves;

struct _Curves
{
  int x, y;    /*  coords for last mouse click  */
};

typedef struct _CurvesDialog CurvesDialog;

struct _CurvesDialog
{
  GtkWidget *    shell;
  GtkWidget *    channel_menu;
  GtkWidget *    xrange;
  GtkWidget *    yrange;
  GtkWidget *    graph;
  int            dragging;      /*  a button is held on the graph  */

  GimpDrawable * drawable;
  ImageMap       image_map;
  int            color;
  int            channel;
  gint           preview;

  int            grab_point;
  int            last;
  int            leftmost;
  int            rightmost;
  int            curve_type;
  int            points[5][17][2];
  unsigned char  curve[5][256];
};

typedef double CRMatrix[4][4];

/*  curves action functions  */

static void   curves_button_press   (Tool *, GimpButtonEvent *, gpointer);
static void   curves_button_release (Tool *, GimpButtonEvent *, gpointer);
static void   curves_motion         (Tool *, GimpMotionEvent *, gpointer);
static void   curves_cursor_update  (Tool *, GimpMotionEvent *, gpointer);
static void   curves_control        (Tool *, int, gpointer);

static CurvesDialog *  curves_new_dialog              (void);
static void            curves_update                  (CurvesDialog *, int);
static void            curves_plot_curve              (CurvesDialog *, int, int, int, int);
static void            curves_calculate_curve         (CurvesDialog *);
static void            curves_preview                 (CurvesDialog *);
static void            curves_value_callback          (GtkWidget *, gpointer);
static void            curves_red_callback            (GtkWidget *, gpointer);
static void            curves_green_callback          (GtkWidget *, gpointer);
static void            curves_blue_callback           (GtkWidget *, gpointer);
static void            curves_alpha_callback           (GtkWidget *, gpointer);
static void            curves_smooth_callback         (GtkWidget *, gpointer);
static void            curves_free_callback           (GtkWidget *, gpointer);
static void            curves_reset_callback          (GtkWidget *, gpointer);
static void            curves_ok_callback             (GtkWidget *, gpointer);
static void            curves_cancel_callback         (GtkWidget *, gpointer);
static gint            curves_delete_callback         (GtkWidget *, gpointer);
static void            curves_preview_update          (GtkWidget *, gpointer);
static void            curves_graph_draw              (GtkDrawingArea *, cairo_t *, int, int, gpointer);
static void            curves_graph_events            (CurvesDialog *, int, double, double, guint);
static void            curves_graph_drag_begin        (GtkGestureDrag *, double, double, gpointer);
static void            curves_graph_drag_update       (GtkGestureDrag *, double, double, gpointer);
static void            curves_graph_drag_end          (GtkGestureDrag *, double, double, gpointer);
static void            curves_graph_motion            (GtkEventControllerMotion *, double, double, gpointer);
static void            curves_CR_compose              (CRMatrix, CRMatrix, CRMatrix);

static void *curves_options = NULL;
static CurvesDialog *curves_dialog = NULL;
static CRMatrix CR_basis =
{
  { -0.5,  1.5, -1.5,  0.5 },
  {  1.0, -2.5,  2.0, -0.5 },
  { -0.5,  0.0,  0.5,  0.0 },
  {  0.0,  1.0,  0.0,  0.0 },
};


static void       curves (PixelRegion *, PixelRegion *, void *);
static Argument * curves_spline_invoker (Argument *);
static Argument * curves_explicit_invoker (Argument *);

/*  curves machinery  */

static void
curves (PixelRegion *srcPR,
	PixelRegion *destPR,
	void        *user_data)
{
  CurvesDialog *cd;
  unsigned char *src, *s;
  unsigned char *dest, *d;
  int has_alpha, alpha;
  int w, h;

  cd = (CurvesDialog *) user_data;

  h = srcPR->h;
  src = srcPR->data;
  dest = destPR->data;
  has_alpha = (srcPR->bytes == 2 || srcPR->bytes == 4);
  alpha = has_alpha ? srcPR->bytes - 1 : srcPR->bytes;

  while (h--)
    {
      w = srcPR->w;
      s = src;
      d = dest;
      while (w--)
	{
	  if (cd->color)
	    {
	      /*  The contributions from the individual channel level settings  */
	      d[RED_PIX] = cd->curve[HISTOGRAM_RED][s[RED_PIX]];
	      d[GREEN_PIX] = cd->curve[HISTOGRAM_GREEN][s[GREEN_PIX]];
	      d[BLUE_PIX] = cd->curve[HISTOGRAM_BLUE][s[BLUE_PIX]];

	      /*  The overall changes  */
	      d[RED_PIX] = cd->curve[HISTOGRAM_VALUE][d[RED_PIX]];
	      d[GREEN_PIX] = cd->curve[HISTOGRAM_VALUE][d[GREEN_PIX]];
	      d[BLUE_PIX] = cd->curve[HISTOGRAM_VALUE][d[BLUE_PIX]];
	    }
	  else
	    d[GRAY_PIX] = cd->curve[HISTOGRAM_VALUE][s[GRAY_PIX]];

	  if (has_alpha) {
	    d[alpha] = cd->curve[HISTOGRAM_ALPHA][s[alpha]];
	    /* d[alpha] = s[alpha]; */
	  }

	  s += srcPR->bytes;
	  d += destPR->bytes;
	}

      src += srcPR->rowstride;
      dest += destPR->rowstride;
    }
}

/*  curves action functions  */

static void
curves_button_press (Tool           *tool,
		     GimpButtonEvent *bevent,
		     gpointer        gdisp_ptr)
{
  GDisplay *gdisp;

  gdisp = gdisp_ptr;
  tool->drawable = gimage_active_drawable (gdisp->gimage);
}

static void
curves_button_release (Tool           *tool,
		       GimpButtonEvent *bevent,
		       gpointer        gdisp_ptr)
{
}

static void
curves_motion (Tool           *tool,
	       GimpMotionEvent *mevent,
	       gpointer        gdisp_ptr)
{
}

static void
curves_cursor_update (Tool           *tool,
		      GimpMotionEvent *mevent,
		      gpointer        gdisp_ptr)
{
  GDisplay *gdisp;

  gdisp = (GDisplay *) gdisp_ptr;
  gdisplay_install_tool_cursor (gdisp, GIMP_CURSOR_TOP_LEFT_ARROW);
}

static void
curves_control (Tool     *tool,
		int       action,
		gpointer  gdisp_ptr)
{
  Curves * _curves;

  _curves = (Curves *) tool->private;

  switch (action)
    {
    case PAUSE :
      break;
    case RESUME :
      break;
    case HALT :
      if (curves_dialog)
	{
	  active_tool->preserve = TRUE;
	  image_map_abort (curves_dialog->image_map);
	  active_tool->preserve = FALSE;
	  curves_dialog->image_map = NULL;
	  curves_cancel_callback (NULL, (gpointer) curves_dialog);
	}
      break;
    }
}

Tool *
tools_new_curves ()
{
  Tool * tool;
  Curves * private;

  /*  The tool options  */
  if (!curves_options)
    curves_options = tools_register_no_options (CURVES, "Curves Options");

  tool = (Tool *) g_malloc (sizeof (Tool));
  private = (Curves *) g_malloc (sizeof (Curves));

  tool->type = CURVES;
  tool->state = INACTIVE;
  tool->scroll_lock = 1;  /*  Disallow scrolling  */
  tool->auto_snap_to = TRUE;
  tool->private = (void *) private;
  tool->button_press_func = curves_button_press;
  tool->button_release_func = curves_button_release;
  tool->motion_func = curves_motion;
  tool->arrow_keys_func = standard_arrow_keys_func;
  tool->cursor_update_func = curves_cursor_update;
  tool->control_func = curves_control;
  tool->preserve = TRUE;
  tool->gdisp_ptr = NULL;
  tool->drawable = NULL;

  return tool;
}

void
tools_free_curves (Tool *tool)
{
  Curves * _curves;

  _curves = (Curves *) tool->private;

  /*  Close the color select dialog  */
  if (curves_dialog)
    curves_cancel_callback (NULL, (gpointer) curves_dialog);

  g_free (_curves);
}

/*  the action area structure  */
static ActionAreaItem action_items[] =
{
  { "Reset", curves_reset_callback, NULL, NULL },
  { "OK", curves_ok_callback, NULL, NULL },
  { "Cancel", curves_cancel_callback, NULL, NULL }
};

static MenuItem channel_items[] =
{
  { "Value", 0, 0, curves_value_callback, NULL, NULL, NULL },
  { "Red", 0, 0, curves_red_callback, NULL, NULL, NULL },
  { "Green", 0, 0, curves_green_callback, NULL, NULL, NULL },
  { "Blue", 0, 0, curves_blue_callback, NULL, NULL, NULL },
  { "Alpha", 0, 0, curves_alpha_callback, NULL, NULL, NULL },
  { NULL, 0, 0, NULL, NULL, NULL, NULL }
};

static MenuItem curve_type_items[] =
{
  { "Smooth", 0, 0, curves_smooth_callback, NULL, NULL, NULL },
  { "Free", 0, 0, curves_free_callback, NULL, NULL, NULL },
  { NULL, 0, 0, NULL, NULL, NULL, NULL }
};

void
curves_initialize (void *gdisp_ptr)
{
  GDisplay *gdisp;
  int i, j;

  gdisp = (GDisplay *) gdisp_ptr;

  if (drawable_indexed (gimage_active_drawable (gdisp->gimage)))
    {
      g_message ("Curves for indexed drawables cannot be adjusted.");
      return;
    }

  /*  The curves dialog  */
  if (!curves_dialog)
    curves_dialog = curves_new_dialog ();

  /*  Initialize the values  */
  curves_dialog->channel = HISTOGRAM_VALUE;
  for (i = 0; i < 5; i++)
    for (j = 0; j < 256; j++)
      curves_dialog->curve[i][j] = j;

  curves_dialog->grab_point = -1;
  for (i = 0; i < 5; i++)
    {
      for (j = 0; j < 17; j++)
	{
	  curves_dialog->points[i][j][0] = -1;
	  curves_dialog->points[i][j][1] = -1;
	}
      curves_dialog->points[i][0][0] = 0;
      curves_dialog->points[i][0][1] = 0;
      curves_dialog->points[i][16][0] = 255;
      curves_dialog->points[i][16][1] = 255;
    }

  curves_dialog->drawable = gimage_active_drawable (gdisp->gimage);
  curves_dialog->color = drawable_color ( (curves_dialog->drawable));
  curves_dialog->image_map = image_map_create (gdisp_ptr, curves_dialog->drawable);

  /* check for alpha channel */
  if (drawable_has_alpha ( (curves_dialog->drawable)))
    menu_item_set_sensitive (&channel_items[4], TRUE);
  else 
    menu_item_set_sensitive (&channel_items[4], FALSE);
  
  /*  hide or show the channel menu based on image type  */
  if (curves_dialog->color)
    for (i = 0; i < 4; i++) 
       menu_item_set_sensitive (&channel_items[i], TRUE);
  else 
    for (i = 1; i < 4; i++) 
       menu_item_set_sensitive (&channel_items[i], FALSE);

  /* set the current selection */
  menu_item_set_active (&channel_items[0]);

  if (!gtk_widget_get_visible (curves_dialog->shell))
    gtk_window_present (GTK_WINDOW (curves_dialog->shell));


  curves_update (curves_dialog, GRAPH | XRANGE_TOP | XRANGE_BOTTOM | YRANGE | DRAW);
}

void
curves_free ()
{
  if (curves_dialog)
    {
      if (curves_dialog->image_map)
	{
	  active_tool->preserve = TRUE;
	  image_map_abort (curves_dialog->image_map);
	  active_tool->preserve = FALSE;
	  curves_dialog->image_map = NULL;
	}
      gtk_window_destroy (GTK_WINDOW (curves_dialog->shell));
    }
}

/**************************/
/*  Select Curves dialog  */
/**************************/

static CurvesDialog *
curves_new_dialog ()
{
  CurvesDialog *cd;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *frame;
  GtkWidget *toggle;
  GtkWidget *option_menu;
  GtkWidget *channel_hbox;
  GtkWidget *table;
  GtkGesture *drag;
  GtkEventController *motion;
  int i, j;

  cd = g_malloc (sizeof (CurvesDialog));
  cd->preview = TRUE;
  cd->curve_type = SMOOTH;
  cd->dragging = FALSE;
  cd->grab_point = -1;
  cd->channel = HISTOGRAM_VALUE;
  for (i = 0; i < 5; i++)
    for (j = 0; j < 256; j++)
      cd->curve[i][j] = j;

  for (i = 0; i < 5; i++)
    channel_items [i].user_data = (gpointer) cd;
  for (i = 0; i < 2; i++)
    curve_type_items [i].user_data = (gpointer) cd;

  /*  The shell and main vbox  */
  cd->shell = gimp_dialog_new ("Curves");

  g_signal_connect (cd->shell, "close-request", G_CALLBACK (curves_delete_callback),
		      cd);
  
  vbox = gimp_vbox_new (FALSE, 2);
  gimp_container_set_border_width (vbox, 2);
  gimp_box_pack_start (gimp_dialog_get_vbox (cd->shell), vbox, TRUE, TRUE, 0);

  /*  The option menu for selecting channels  */
  channel_hbox = gimp_hbox_new (FALSE, 2);
  gimp_box_pack_start (vbox, channel_hbox, FALSE, FALSE, 0);

  label = gtk_label_new ("Modify Curves for Channel: ");
  gimp_box_pack_start (channel_hbox, label, FALSE, FALSE, 0);

  cd->channel_menu = build_menu (channel_items, NULL);
  gimp_box_pack_start (channel_hbox, cd->channel_menu, FALSE, FALSE, 2);

  /*  The table for the yrange and the graph  */
  table = gimp_table_new (2, 2, FALSE);
  gimp_container_set_border_width (table, 2);
  gimp_box_pack_start (vbox, table, FALSE, FALSE, 0);

  /*  The range drawing area  */
  frame = gtk_frame_new (NULL);
  gimp_table_attach (table, frame, 0, 1, 0, 1,
		    GIMP_EXPAND, GIMP_EXPAND, 0, 0);

  cd->yrange = gimp_preview_new (GIMP_PREVIEW_GRAYSCALE);
  gimp_preview_size (GIMP_PREVIEW (cd->yrange), YRANGE_WIDTH, YRANGE_HEIGHT);
  gimp_container_add (frame, cd->yrange);

  /*  The curves graph  */
  frame = gtk_frame_new (NULL);
  gimp_table_attach (table, frame, 1, 2, 0, 1,
		    GIMP_EXPAND | GIMP_SHRINK | GIMP_FILL,
		    GIMP_FILL, 0, 0);

  cd->graph = gtk_drawing_area_new ();
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (cd->graph),
				      GRAPH_WIDTH + RADIUS * 2);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (cd->graph),
				       GRAPH_HEIGHT + RADIUS * 2);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (cd->graph),
				  curves_graph_draw, cd, NULL);

  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), 0);
  g_signal_connect (drag, "drag-begin", G_CALLBACK (curves_graph_drag_begin), cd);
  g_signal_connect (drag, "drag-update", G_CALLBACK (curves_graph_drag_update), cd);
  g_signal_connect (drag, "drag-end", G_CALLBACK (curves_graph_drag_end), cd);
  gtk_widget_add_controller (cd->graph, GTK_EVENT_CONTROLLER (drag));

  motion = gtk_event_controller_motion_new ();
  g_signal_connect (motion, "motion", G_CALLBACK (curves_graph_motion), cd);
  gtk_widget_add_controller (cd->graph, motion);
  gimp_container_add (frame, cd->graph);

  /*  The range drawing area  */
  frame = gtk_frame_new (NULL);
  gimp_table_attach (table, frame, 1, 2, 1, 2,
		    GIMP_EXPAND, GIMP_EXPAND, 0, 0);

  cd->xrange = gimp_preview_new (GIMP_PREVIEW_GRAYSCALE);
  gimp_preview_size (GIMP_PREVIEW (cd->xrange), XRANGE_WIDTH, XRANGE_HEIGHT);
  gimp_container_add (frame, cd->xrange);

  /*  Horizontal box for preview  */
  hbox = gimp_hbox_new (FALSE, 2);
  gimp_box_pack_start (vbox, hbox, FALSE, FALSE, 0);

  /*  The option menu for selecting the drawing method  */
  label = gtk_label_new ("Curve Type: ");
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);

  option_menu = build_menu (curve_type_items, NULL);
  gimp_box_pack_start (hbox, option_menu, FALSE, FALSE, 2);

  /*  The preview toggle  */
  toggle = gtk_check_button_new_with_label ("Preview");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), cd->preview);
  gimp_box_pack_start (hbox, toggle, TRUE, FALSE, 0);
  g_signal_connect (toggle, "toggled", G_CALLBACK (curves_preview_update),
		      cd);


  /*  The action area  */
  action_items[0].user_data = cd;
  action_items[1].user_data = cd;
  action_items[2].user_data = cd;
  {
    int n;

    for (n = 0; n < 3; n++)
      gimp_dialog_add_button (cd->shell, action_items[n].label,
			      G_CALLBACK (action_items[n].callback),
			      action_items[n].user_data, n == 0);
  }

  /*  Fill the range previews  */
  curves_update (cd, XRANGE_TOP | XRANGE_BOTTOM | YRANGE);

  return cd;
}

static void
curves_update (CurvesDialog *cd,
	       int           update)
{
  int i, j;

  if (update & XRANGE_TOP)
    {
      for (i = 0; i < XRANGE_HEIGHT / 2; i++)
	gimp_preview_draw_row (GIMP_PREVIEW (cd->xrange),
			      cd->curve[cd->channel],
			      0, i, XRANGE_WIDTH);

      if (update & DRAW)
	gtk_widget_queue_draw (cd->xrange);
    }
  if (update & XRANGE_BOTTOM)
    {
      unsigned char buf[XRANGE_WIDTH];

      for (i = 0; i < XRANGE_WIDTH; i++)
	buf[i] = i;

      for (i = XRANGE_HEIGHT / 2; i < XRANGE_HEIGHT; i++)
	gimp_preview_draw_row (GIMP_PREVIEW (cd->xrange), buf, 0, i, XRANGE_WIDTH);

      if (update & DRAW)
	gtk_widget_queue_draw (cd->xrange);
    }
  if (update & YRANGE)
    {
      unsigned char buf[YRANGE_WIDTH];

      for (i = 0; i < YRANGE_HEIGHT; i++)
	{
	  for (j = 0; j < YRANGE_WIDTH; j++)
	    buf[j] = (255 - i);

	  gimp_preview_draw_row (GIMP_PREVIEW (cd->yrange), buf, 0, i, YRANGE_WIDTH);

	}

      if (update & DRAW)
	gtk_widget_queue_draw (cd->yrange);
    }
  if ((update & GRAPH) && (update & DRAW))
    gtk_widget_queue_draw (cd->graph);
}

static void
curves_graph_draw (GtkDrawingArea *area,
		   cairo_t        *cr,
		   int             width,
		   int             height,
		   gpointer        data)
{
  CurvesDialog *cd;
  int i;

  cd = (CurvesDialog *) data;

  cairo_set_line_width (cr, 1.0);

  /*  Draw the grid lines  */
  cairo_set_source_rgb (cr, 0.5, 0.5, 0.5);
  for (i = 0; i < 5; i++)
    {
      cairo_move_to (cr, RADIUS, i * (GRAPH_HEIGHT / 4) + RADIUS + 0.5);
      cairo_line_to (cr, GRAPH_WIDTH + RADIUS + 1, i * (GRAPH_HEIGHT / 4) + RADIUS + 0.5);
      cairo_move_to (cr, i * (GRAPH_WIDTH / 4) + RADIUS + 0.5, RADIUS);
      cairo_line_to (cr, i * (GRAPH_WIDTH / 4) + RADIUS + 0.5, GRAPH_HEIGHT + RADIUS + 1);
    }
  cairo_stroke (cr);

  /*  Draw the curve, one point per column  */
  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  for (i = 0; i < 256; i++)
    cairo_rectangle (cr, i + RADIUS, 255 - cd->curve[cd->channel][i] + RADIUS, 1, 1);
  cairo_fill (cr);

  /*  Draw the points  */
  if (cd->curve_type == SMOOTH)
    for (i = 0; i < 17; i++)
      {
	if (cd->points[cd->channel][i][0] != -1)
	  {
	    cairo_arc (cr,
		       cd->points[cd->channel][i][0] + RADIUS,
		       255 - cd->points[cd->channel][i][1] + RADIUS,
		       RADIUS, 0, 2 * G_PI);
	    cairo_fill (cr);
	  }
      }
}

static void
curves_plot_curve (CurvesDialog *cd,
		   int           p1,
		   int           p2,
		   int           p3,
		   int           p4)
{
  CRMatrix geometry;
  CRMatrix tmp1, tmp2;
  CRMatrix deltas;
  double x, dx, dx2, dx3;
  double y, dy, dy2, dy3;
  double d, d2, d3;
  int lastx, lasty;
  int newx, newy;
  int i;

  /* construct the geometry matrix from the segment */
  for (i = 0; i < 4; i++)
    {
      geometry[i][2] = 0;
      geometry[i][3] = 0;
    }

  for (i = 0; i < 2; i++)
    {
      geometry[0][i] = cd->points[cd->channel][p1][i];
      geometry[1][i] = cd->points[cd->channel][p2][i];
      geometry[2][i] = cd->points[cd->channel][p3][i];
      geometry[3][i] = cd->points[cd->channel][p4][i];
    }

  /* subdivide the curve 1000 times */
  /* n can be adjusted to give a finer or coarser curve */
  d = 1.0 / 1000;
  d2 = d * d;
  d3 = d * d * d;

  /* construct a temporary matrix for determining the forward differencing deltas */
  tmp2[0][0] = 0;     tmp2[0][1] = 0;     tmp2[0][2] = 0;    tmp2[0][3] = 1;
  tmp2[1][0] = d3;    tmp2[1][1] = d2;    tmp2[1][2] = d;    tmp2[1][3] = 0;
  tmp2[2][0] = 6*d3;  tmp2[2][1] = 2*d2;  tmp2[2][2] = 0;    tmp2[2][3] = 0;
  tmp2[3][0] = 6*d3;  tmp2[3][1] = 0;     tmp2[3][2] = 0;    tmp2[3][3] = 0;

  /* compose the basis and geometry matrices */
  curves_CR_compose (CR_basis, geometry, tmp1);

  /* compose the above results to get the deltas matrix */
  curves_CR_compose (tmp2, tmp1, deltas);

  /* extract the x deltas */
  x = deltas[0][0];
  dx = deltas[1][0];
  dx2 = deltas[2][0];
  dx3 = deltas[3][0];

  /* extract the y deltas */
  y = deltas[0][1];
  dy = deltas[1][1];
  dy2 = deltas[2][1];
  dy3 = deltas[3][1];

  lastx = BOUNDS (x, 0, 255);
  lasty = BOUNDS (y, 0, 255);

  cd->curve[cd->channel][lastx] = lasty;

  /* loop over the curve */
  for (i = 0; i < 1000; i++)
    {
      /* increment the x values */
      x += dx;
      dx += dx2;
      dx2 += dx3;

      /* increment the y values */
      y += dy;
      dy += dy2;
      dy2 += dy3;

      newx = BOUNDS ((ROUND (x)), 0, 255);
      newy = BOUNDS ((ROUND (y)), 0, 255);

      /* if this point is different than the last one...then draw it */
      if ((lastx != newx) || (lasty != newy))
	cd->curve[cd->channel][newx] = newy;

      lastx = newx;
      lasty = newy;
    }
}

static void
curves_calculate_curve (CurvesDialog *cd)
{
  int i;
  int points[17];
  int num_pts;
  int p1, p2, p3, p4;

  switch (cd->curve_type)
    {
    case GFREE:
      break;
    case SMOOTH:
      /*  cycle through the curves  */
      num_pts = 0;
      for (i = 0; i < 17; i++)
	if (cd->points[cd->channel][i][0] != -1)
	  points[num_pts++] = i;

      /*  Initialize boundary curve points */
      if (num_pts != 0)
	{
	  for (i = 0; i < cd->points[cd->channel][points[0]][0]; i++)
	    cd->curve[cd->channel][i] = cd->points[cd->channel][points[0]][1];
	  for (i = cd->points[cd->channel][points[num_pts - 1]][0]; i < 256; i++)
	    cd->curve[cd->channel][i] = cd->points[cd->channel][points[num_pts - 1]][1];
	}

      for (i = 0; i < num_pts - 1; i++)
	{
	  p1 = (i == 0) ? points[i] : points[(i - 1)];
	  p2 = points[i];
	  p3 = points[(i + 1)];
	  p4 = (i == (num_pts - 2)) ? points[(num_pts - 1)] : points[(i + 2)];

	  curves_plot_curve (cd, p1, p2, p3, p4);
	}
      break;
    }
}

static void
curves_preview (CurvesDialog *cd)
{
  if (!cd->image_map)
    g_message ("curves_preview(): No image map");

  active_tool->preserve = TRUE;  /* Going to dirty the display... */

  image_map_apply (cd->image_map, curves, (void *) cd);

  active_tool->preserve = FALSE;  /* All done */
}

static void
curves_value_callback (GtkWidget *w,
		       gpointer   client_data)
{
  CurvesDialog *cd;

  cd = (CurvesDialog *) client_data;

  if (cd->channel != HISTOGRAM_VALUE)
    {
      cd->channel = HISTOGRAM_VALUE;
      curves_update (cd, GRAPH | XRANGE_TOP | DRAW);
    }
}

static void
curves_red_callback (GtkWidget *w,
		     gpointer   client_data)
{
  CurvesDialog *cd;

  cd = (CurvesDialog *) client_data;

  if (cd->channel != HISTOGRAM_RED)
    {
      cd->channel = HISTOGRAM_RED;
      curves_update (cd, GRAPH | XRANGE_TOP | DRAW);
    }
}

static void
curves_green_callback (GtkWidget *w,
		       gpointer   client_data)
{
  CurvesDialog *cd;

  cd = (CurvesDialog *) client_data;

  if (cd->channel != HISTOGRAM_GREEN)
    {
      cd->channel = HISTOGRAM_GREEN;
      curves_update (cd, GRAPH | XRANGE_TOP | DRAW);
    }
}

static void
curves_blue_callback (GtkWidget *w,
		      gpointer   client_data)
{
  CurvesDialog *cd;

  cd = (CurvesDialog *) client_data;

  if (cd->channel != HISTOGRAM_BLUE)
    {
      cd->channel = HISTOGRAM_BLUE;
      curves_update (cd, GRAPH | XRANGE_TOP | DRAW);
    }
}

static void
curves_alpha_callback (GtkWidget *w,
		      gpointer   client_data)
{
  CurvesDialog *cd;

  cd = (CurvesDialog *) client_data;

  if (cd->channel != HISTOGRAM_ALPHA)
    {
      cd->channel = HISTOGRAM_ALPHA;
      curves_update (cd, GRAPH | XRANGE_TOP | DRAW);
    }
}

static void
curves_smooth_callback (GtkWidget *w,
			gpointer   client_data)
{
  CurvesDialog *cd;
  int i, index;

  cd = (CurvesDialog *) client_data;

  if (cd->curve_type != SMOOTH)
    {
      cd->curve_type = SMOOTH;

      /*  pick representative points from the curve and make them control points  */
      for (i = 0; i <= 8; i++)
	{
	  index = BOUNDS ((i * 32), 0, 255);
	  cd->points[cd->channel][i * 2][0] = index;
	  cd->points[cd->channel][i * 2][1] = cd->curve[cd->channel][index];
	}

      curves_calculate_curve (cd);
      curves_update (cd, GRAPH | DRAW);

      if (cd->preview)
	curves_preview (cd);
    }
}

static void
curves_free_callback (GtkWidget *w,
		      gpointer   client_data)
{
  CurvesDialog *cd;

  cd = (CurvesDialog *) client_data;

  if (cd->curve_type != GFREE)
    {
      cd->curve_type = GFREE;
      curves_update (cd, GRAPH | DRAW);
    }
}

static void
curves_reset_callback (GtkWidget *widget,
		       gpointer   client_data)
{
  CurvesDialog *cd;
  int i;

  cd = (CurvesDialog *) client_data;

  /*  Initialize the values  */
  for (i = 0; i < 256; i++)
    cd->curve[cd->channel][i] = i;

  cd->grab_point = -1;
  for (i = 0; i < 17; i++)
    {
      cd->points[cd->channel][i][0] = -1;
      cd->points[cd->channel][i][1] = -1;
    }
  cd->points[cd->channel][0][0] = 0;
  cd->points[cd->channel][0][1] = 0;
  cd->points[cd->channel][16][0] = 255;
  cd->points[cd->channel][16][1] = 255;

  curves_update (cd, GRAPH | XRANGE_TOP | DRAW);
  if (cd->preview)
    curves_preview (cd);
}

static void
curves_ok_callback (GtkWidget *widget,
		    gpointer   client_data)
{
  CurvesDialog *cd;

  cd = (CurvesDialog *) client_data;

  if (gtk_widget_get_visible (cd->shell))
    gtk_widget_set_visible (cd->shell, FALSE);

  active_tool->preserve = TRUE;  /* We're about to dirty... */

  if (!cd->preview)
    image_map_apply (cd->image_map, curves, (void *) cd);

  if (cd->image_map)
    image_map_commit (cd->image_map);

  active_tool->preserve = FALSE;

  cd->image_map = NULL;
}

static void
curves_cancel_callback (GtkWidget *widget,
			gpointer   client_data)
{
  CurvesDialog *cd;

  cd = (CurvesDialog *) client_data;
  if (gtk_widget_get_visible (cd->shell))
    gtk_widget_set_visible (cd->shell, FALSE);

  if (cd->image_map)
    {
      active_tool->preserve = TRUE;
      image_map_abort (cd->image_map);
      active_tool->preserve = FALSE;
      gdisplays_flush ();
    }

  cd->image_map = NULL;
}

static gint 
curves_delete_callback (GtkWidget *w,
			gpointer data) 
{
  curves_cancel_callback (w, data);

  return TRUE;
}
static void
curves_preview_update (GtkWidget *w,
		       gpointer   data)
{
  CurvesDialog *cd;

  cd = (CurvesDialog *) data;
  
  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    {
      cd->preview = TRUE;
      curves_preview (cd);
    }
  else
    cd->preview = FALSE;
}

static void
curves_graph_events (CurvesDialog *cd,
		     int           type,
		     double        event_x,
		     double        event_y,
		     guint         state)
{
  static GimpCursorType cursor_type = GIMP_CURSOR_TOP_LEFT_ARROW;
  GimpCursorType new_type;
  int i;
  int tx, ty;
  int x, y;
  int closest_point;
  int distance;
  int x1, x2, y1, y2;

  new_type      = GIMP_CURSOR_X_CURSOR;
  closest_point = 0;

  /*  get the pointer position  */
  tx = (int) event_x;
  ty = (int) event_y;
  x = BOUNDS ((tx - RADIUS), 0, 255);
  y = BOUNDS ((ty - RADIUS), 0, 255);

  distance = G_MAXINT;
  for (i = 0; i < 17; i++)
    {
      if (cd->points[cd->channel][i][0] != -1)
	if (abs (x - cd->points[cd->channel][i][0]) < distance)
	  {
	    distance = abs (x - cd->points[cd->channel][i][0]);
	    closest_point = i;
	  }
    }
  if (distance > MIN_DISTANCE)
    closest_point = (x + 8) / 16;

  switch (type)
    {
    case GRAPH_PRESS:
      new_type = GIMP_CURSOR_TCROSS;

      switch (cd->curve_type)
	{
	case SMOOTH:
	  /*  determine the leftmost and rightmost points  */
	  cd->leftmost = -1;
	  for (i = closest_point - 1; i >= 0; i--)
	    if (cd->points[cd->channel][i][0] != -1)
	      {
		cd->leftmost = cd->points[cd->channel][i][0];
		break;
	      }
	  cd->rightmost = 256;
	  for (i = closest_point + 1; i < 17; i++)
	    if (cd->points[cd->channel][i][0] != -1)
	      {
		cd->rightmost = cd->points[cd->channel][i][0];
		break;
	      }

	  cd->grab_point = closest_point;
	  cd->points[cd->channel][cd->grab_point][0] = x;
	  cd->points[cd->channel][cd->grab_point][1] = 255 - y;

	  curves_calculate_curve (cd);
	  break;

	case GFREE:
	  cd->curve[cd->channel][x] = 255 - y;
	  cd->grab_point = x;
	  cd->last = y;
	  break;
	}

      curves_update (cd, GRAPH | XRANGE_TOP | DRAW);
      break;

    case GRAPH_RELEASE:
      new_type = GIMP_CURSOR_FLEUR;
      cd->grab_point = -1;

      if (cd->preview)
	curves_preview (cd);
      break;

    case GRAPH_MOTION:
      switch (cd->curve_type)
	{
	case SMOOTH:
	  /*  If no point is grabbed...  */
	  if (cd->grab_point == -1)
	    {
	      if (cd->points[cd->channel][closest_point][0] != -1)
		new_type = GIMP_CURSOR_FLEUR;
	      else
		new_type = GIMP_CURSOR_TCROSS;
	    }
	  /*  Else, drag the grabbed point  */
	  else
	    {
	      new_type = GIMP_CURSOR_TCROSS;

	      cd->points[cd->channel][cd->grab_point][0] = -1;

	      if (x > cd->leftmost && x < cd->rightmost)
		{
		  closest_point = (x + 8) / 16;
		  if (cd->points[cd->channel][closest_point][0] == -1)
		    cd->grab_point = closest_point;
		  cd->points[cd->channel][cd->grab_point][0] = x;
		  cd->points[cd->channel][cd->grab_point][1] = 255 - y;
		}

	      curves_calculate_curve (cd);
	      curves_update (cd, GRAPH | XRANGE_TOP | DRAW);
	    }
	  break;

	case GFREE:
	  if (cd->grab_point != -1)
	    {
	      if (cd->grab_point > x)
		{
		  x1 = x;
		  x2 = cd->grab_point;
		  y1 = y;
		  y2 = cd->last;
		}
	      else
		{
		  x1 = cd->grab_point;
		  x2 = x;
		  y1 = cd->last;
		  y2 = y;
		}

	      if (x2 != x1)
		for (i = x1; i <= x2; i++)
		  cd->curve[cd->channel][i] = 255 - (y1 + ((y2 - y1) * (i - x1)) / (x2 - x1));
	      else
		cd->curve[cd->channel][x] = 255 - y;

	      cd->grab_point = x;
	      cd->last = y;

	      curves_update (cd, GRAPH | XRANGE_TOP | DRAW);
	    }

	  if (state & GDK_BUTTON1_MASK)
	    new_type = GIMP_CURSOR_TCROSS;
	  else
	    new_type = GIMP_CURSOR_PENCIL;
	  break;
	}

      if (new_type != cursor_type)
	{
	  cursor_type = new_type;
	  change_win_cursor (cd->graph, cursor_type);
	}
      break;

    default:
      break;
    }
}

static void
curves_graph_drag_begin (GtkGestureDrag *gesture,
			 double          x,
			 double          y,
			 gpointer        data)
{
  CurvesDialog *cd;

  cd = (CurvesDialog *) data;
  cd->dragging = TRUE;
  curves_graph_events (cd, GRAPH_PRESS, x, y, GDK_BUTTON1_MASK);
}

static void
curves_graph_drag_update (GtkGestureDrag *gesture,
			  double          offset_x,
			  double          offset_y,
			  gpointer        data)
{
  double start_x, start_y;

  gtk_gesture_drag_get_start_point (gesture, &start_x, &start_y);
  curves_graph_events ((CurvesDialog *) data, GRAPH_MOTION,
		       start_x + offset_x, start_y + offset_y, GDK_BUTTON1_MASK);
}

static void
curves_graph_drag_end (GtkGestureDrag *gesture,
		       double          offset_x,
		       double          offset_y,
		       gpointer        data)
{
  CurvesDialog *cd;
  double start_x, start_y;

  cd = (CurvesDialog *) data;
  gtk_gesture_drag_get_start_point (gesture, &start_x, &start_y);
  cd->dragging = FALSE;
  curves_graph_events (cd, GRAPH_RELEASE,
		       start_x + offset_x, start_y + offset_y, 0);
}

/*  Pointer motion with no button held: only updates the cursor  */
static void
curves_graph_motion (GtkEventControllerMotion *controller,
		     double                    x,
		     double                    y,
		     gpointer                  data)
{
  CurvesDialog *cd;

  cd = (CurvesDialog *) data;
  if (cd->dragging)
    return;

  curves_graph_events (cd, GRAPH_MOTION, x, y, 0);
}

static void
curves_CR_compose (CRMatrix a,
		   CRMatrix b,
		   CRMatrix ab)
{
  int i, j;

  for (i = 0; i < 4; i++)
    {
      for (j = 0; j < 4; j++)
        {
          ab[i][j] = (a[i][0] * b[0][j] +
                      a[i][1] * b[1][j] +
                      a[i][2] * b[2][j] +
                      a[i][3] * b[3][j]);
        }
    }
}

/*
 *  The curves procedure definitions
 */


/*  Procedure for defining the curve with a spline  */
ProcArg curves_spline_args[] =
{
  { PDB_IMAGE,
    "image",
    "the image"
  },
  { PDB_DRAWABLE,
    "drawable",
    "the drawable"
  },
  { PDB_INT32,
    "channel",
    "the channel to modify: { VALUE (0), RED (1), GREEN (2), BLUE (3), ALPHA (4), GRAY (0) }"
  },
  { PDB_INT32,
    "num_points",
    "the number of values in the control point array ( 3 < num_points <= 32 )"
  },
  { PDB_INT8ARRAY,
    "control_pts",
    "the spline control points: { cp1.x, cp1.y, cp2.x, cp2.y, ... }"
  }
};

ProcRecord curves_spline_proc =
{
  "gimp_curves_spline",
  "Modifies the intensity curve(s) for specified drawable",
  "Modifies the intensity mapping for one channel in the specified drawable.  The drawable must be either grayscale or RGB, and the channel can be either an intensity component, or the value.  The 'control_pts' parameter is an array of integers which define a set of control points which describe a Catmull Rom spline which yields the final intensity curve.  Use the 'gimp_curves_explicit' function to explicitly modify intensity levels.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1995-1996",
  PDB_INTERNAL,

  /*  Input arguments  */
  5,
  curves_spline_args,

  /*  Output arguments  */
  0,
  NULL,

  /*  Exec method  */
  { { curves_spline_invoker } },
};


static Argument *
curves_spline_invoker (Argument *args)
{
  PixelRegion srcPR, destPR;
  int success = TRUE;
  int int_value;
  CurvesDialog cd;
  GImage *gimage;
  int channel;
  int num_cp;
  unsigned char *control_pts;
  int x1, y1, x2, y2;
  int i, j;
  void *pr;
  GimpDrawable *drawable;

  /*  the gimage  */
  if (success)
    {
      int_value = args[0].value.pdb_int;
      if (! (gimage = gimage_get_ID (int_value)))
	success = FALSE;
    }
  /*  the drawable  */
  if (success)
    {
      int_value = args[1].value.pdb_int;
      drawable =  drawable_get_ID (int_value);
      if (drawable == NULL || gimage != drawable_gimage (drawable))
	success = FALSE;
    }
  /*  make sure the drawable is not indexed color  */
  if (success)
    success = ! drawable_indexed (drawable);
  
    
  /*  channel  */
  if (success)
    {
      int_value = args[2].value.pdb_int;
      if (success)
	{
	  if (drawable_gray (drawable))
	    {
	      if (int_value != 0)
		success = FALSE;
	    }
	  else if (drawable_color (drawable))
	    {
	      if (int_value < 0 || int_value > 3)
		success = FALSE;
	    }
	  else
	    success = FALSE;
	}
      channel = int_value;
    }
  if (success)
    {
      num_cp = args[3].value.pdb_int;
      if (num_cp < 4 || num_cp > 32 || (num_cp & 0x1))
	success = FALSE;
    }
  /*  control points  */
  if (success)
    {
      control_pts = (unsigned char *) args[4].value.pdb_pointer;
    }

  /*  arrange to modify the curves  */
  if (success)
    {
      for (i = 0; i < 5; i++)
	for (j = 0; j < 256; j++)
	  cd.curve[i][j] = j;

      for (i = 0; i < 5; i++)
	for (j = 0; j < 17; j++)
	  {
	    cd.points[i][j][0] = -1;
	    cd.points[i][j][1] = -1;
	  }

      cd.channel = channel;
      cd.color = drawable_color (drawable);
      cd.curve_type = SMOOTH;

      for (j = 0; j < num_cp / 2; j++)
	{
	  cd.points[cd.channel][j][0] = control_pts[j * 2];
	  cd.points[cd.channel][j][1] = control_pts[j * 2 + 1];
	}
      curves_calculate_curve (&cd);

      /*  The application should occur only within selection bounds  */
      drawable_mask_bounds (drawable, &x1, &y1, &x2, &y2);

      pixel_region_init (&srcPR, drawable_data (drawable), x1, y1, (x2 - x1), (y2 - y1), FALSE);
      pixel_region_init (&destPR, drawable_shadow (drawable), x1, y1, (x2 - x1), (y2 - y1), TRUE);

      for (pr = pixel_regions_register (2, &srcPR, &destPR); pr != NULL; pr = pixel_regions_process (pr))
	curves (&srcPR, &destPR, (void *) &cd);

      drawable_merge_shadow (drawable, TRUE);
      drawable_update (drawable, x1, y1, (x2 - x1), (y2 - y1));
    }

  return procedural_db_return_args (&curves_spline_proc, success);
}

/*  Procedure for explicitly defining the curve  */
ProcArg curves_explicit_args[] =
{
  { PDB_IMAGE,
    "image",
    "the image"
  },
  { PDB_DRAWABLE,
    "drawable",
    "the drawable"
  },
  { PDB_INT32,
    "channel",
    "the channel to modify: { VALUE (0), RED (1), GREEN (2), BLUE (3), GRAY (0) }"
  },
  { PDB_INT32,
    "num_bytes",
    "the number of bytes in the new curve (always 256)"
  },
  { PDB_INT8ARRAY,
    "curve",
    "the explicit curve"
  }
};

ProcRecord curves_explicit_proc =
{
  "gimp_curves_explicit",
  "Modifies the intensity curve(s) for specified drawable",
  "Modifies the intensity mapping for one channel in the specified drawable.  The drawable must be either grayscale or RGB, and the channel can be either an intensity component, or the value.  The 'curve' parameter is an array of bytes which explicitly defines how each pixel value in the drawable will be modified.  Use the 'gimp_curves_spline' function to modify intensity levels with Catmull Rom splines.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1995-1996",
  PDB_INTERNAL,

  /*  Input arguments  */
  5,
  curves_explicit_args,

  /*  Output arguments  */
  0,
  NULL,

  /*  Exec method  */
  { { curves_explicit_invoker } },
};


static Argument *
curves_explicit_invoker (Argument *args)
{
  PixelRegion srcPR, destPR;
  int success = TRUE;
  int int_value;
  CurvesDialog cd;
  GImage *gimage;
  int channel;
  unsigned char *curve;
  int x1, y1, x2, y2;
  int i, j;
  void *pr;
  GimpDrawable *drawable;

  /*  the gimage  */
  if (success)
    {
      int_value = args[0].value.pdb_int;
      if (! (gimage = gimage_get_ID (int_value)))
	success = FALSE;
    }
  /*  the drawable  */
  if (success)
    {
      int_value = args[1].value.pdb_int;
      drawable =  drawable_get_ID (int_value);
      if (drawable == NULL || gimage != drawable_gimage (drawable))
	success = FALSE;
    }
  /*  make sure the drawable is not indexed color  */
  if (success)
    success = ! drawable_indexed (drawable);
  
  /*  channel  */
  if (success)
    {
      int_value = args[2].value.pdb_int;
      if (success)
	{
	  if (drawable_gray (drawable))
	    {
	      if (int_value != 0)
		success = FALSE;
	    }
	  else if (drawable_color (drawable))
	    {
	      if (int_value < 0 || int_value > 3)
		success = FALSE;
	    }
	  else
	    success = FALSE;
	}
      channel = int_value;
    }
  /*  the number of bytes  */
  if (success)
    {
      int_value = args[3].value.pdb_int;
      if (int_value != 256)
	success = FALSE;
    }
  /*  the curve  */
  if (success)
    {
      curve = (unsigned char *) args[4].value.pdb_pointer;
    }

  /*  arrange to modify the curves  */
  if (success)
    {
      for (i = 0; i < 5; i++)
	for (j = 0; j < 256; j++)
	  cd.curve[i][j] = j;

      cd.channel = channel;
      cd.color = drawable_color (drawable);

      for (j = 0; j < 256; j++)
	cd.curve[cd.channel][j] = curve[j];

      /*  The application should occur only within selection bounds  */
      drawable_mask_bounds (drawable, &x1, &y1, &x2, &y2);

      pixel_region_init (&srcPR, drawable_data (drawable), x1, y1, (x2 - x1), (y2 - y1), FALSE);
      pixel_region_init (&destPR, drawable_shadow (drawable), x1, y1, (x2 - x1), (y2 - y1), TRUE);

      for (pr = pixel_regions_register (2, &srcPR, &destPR); pr != NULL; pr = pixel_regions_process (pr))
	curves (&srcPR, &destPR, (void *) &cd);

      drawable_merge_shadow (drawable, TRUE);
      drawable_update (drawable, x1, y1, (x2 - x1), (y2 - y1));
    }

  return procedural_db_return_args (&curves_explicit_proc, success);
}


