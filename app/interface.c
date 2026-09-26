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
#include "appenv.h"
#include "actionarea.h"
#include "app_procs.h"
#include "colormaps.h"
#include "color_area.h"
#include "commands.h"
#include "disp_callbacks.h"
#include "errors.h"
#include "gdisplay.h"
#include "gdisplay_ops.h"
#include "gimage.h"
#include "gimprc.h"
#include "gimpruler.h"
#include "general.h"
#include "interface.h"
#include "menus.h"
#include "tools.h"

#include "pixmaps.h"


/*  local functions  */
static void      tools_select_update   (GtkToggleButton *widget,
					gpointer         data);
static void      tools_double_click    (GtkGestureClick *gesture,
					int              n_press,
					double           x,
					double           y,
					gpointer         data);
static void      gdisplay_destroy      (GtkWidget *widget,
					GDisplay  *display);
static gboolean  gdisplay_delete       (GtkWindow *widget,
					GDisplay  *display);

static void      toolbox_destroy       (GtkWidget *widget,
					gpointer   data);
static gboolean  toolbox_delete        (GtkWindow *widget,
					gpointer   data);

typedef struct _ToolButton ToolButton;

struct _ToolButton
{
  char **icon_data;
  char  *tool_desc;
  gpointer callback_data;
};

static ToolButton tool_data[] =
{
  { (char **) rect_bits,
    "Select rectangular regions",
    (gpointer) RECT_SELECT },
  { (char **) circ_bits,
    "Select elliptical regions",
    (gpointer) ELLIPSE_SELECT },
  { (char **) free_bits,
    "Select hand-drawn regions",
    (gpointer) FREE_SELECT },
  { (char **) fuzzy_bits,
    "Select contiguous regions",
    (gpointer) FUZZY_SELECT },
  { (char **) bezier_bits,
    "Select regions using Bezier curves",
    (gpointer) BEZIER_SELECT },
  { (char **) iscissors_bits,
    "Select shapes from image",
    (gpointer) ISCISSORS },
  { (char **) move_bits,
    "Move layers & selections",
    (gpointer) MOVE },
  { (char **) magnify_bits,
    "Zoom in & out",
    (gpointer) MAGNIFY },
  { (char **) crop_bits,
    "Crop the image",
    (gpointer) CROP },
  { (char **) scale_bits,
    "Transform the layer or selection",
    (gpointer) ROTATE },
  { (char **) horizflip_bits,
    "Flip the layer or selection",
    (gpointer) FLIP_HORZ },
  { (char **) text_bits,
    "Add text to the image",
    (gpointer) TEXT },
  { (char **) colorpicker_bits,
    "Pick colors from the image",
    (gpointer) COLOR_PICKER },
  { (char **) fill_bits,
    "Fill with a color or pattern",
    (gpointer) BUCKET_FILL },
  { (char **) gradient_bits,
    "Fill with a color gradient",
    (gpointer) BLEND },
  { (char **) pencil_bits,
    "Draw sharp pencil strokes",
    (gpointer) PENCIL },
  { (char **) paint_bits,
    "Paint fuzzy brush strokes",
    (gpointer) PAINTBRUSH },
  { (char **) erase_bits,
    "Erase to background or transparency",
    (gpointer) ERASER },
  { (char **) airbrush_bits,
    "Airbrush with variable pressure",
    (gpointer) AIRBRUSH },
  { (char **) clone_bits,
    "Paint using patterns or image regions",
    (gpointer) CLONE },
  { (char **) blur_bits,
    "Blur or sharpen",
    (gpointer) CONVOLVE },
  { NULL,
    NULL,
    (gpointer) BY_COLOR_SELECT },
  { NULL,
    NULL,
    (gpointer) COLOR_BALANCE },
  { NULL,
    NULL,
    (gpointer) BRIGHTNESS_CONTRAST },
  { NULL,
    NULL,
    (gpointer) HUE_SATURATION },
  { NULL,
    NULL,
    (gpointer) POSTERIZE },
  { NULL,
    NULL,
    (gpointer) THRESHOLD },
  { NULL,
    NULL,
    (gpointer) CURVES },
  { NULL,
    NULL,
    (gpointer) LEVELS },
  { NULL,
    NULL,
    (gpointer) HISTOGRAM }
};

/*  The gray levels the letters of the tool icons stand for; '.' is
 *  transparent.
 */
static const guchar pixmap_colors[8] =
{
  0x00, /* a */
  0x24, /* b */
  0x49, /* c */
  0x6D, /* d */
  0x92, /* e */
  0xB6, /* f */
  0xDB, /* g */
  0xFF, /* h */
};

#define NUM_TOOLS (sizeof (tool_data) / sizeof (ToolButton))
#define COLUMNS   3
#define ROWS      7
#define MARGIN    2

/*  Widgets for each tool button--these are used from command.c to activate on
 *  tool selection via both menus and keyboard accelerators.
 */
GtkWidget *tool_widgets[NUM_TOOLS];

/*  The popup shell is a pointer to the gdisplay shell that posted the latest
 *  popup menu.
 */
GtkWidget *popup_shell = NULL;

static GtkWidget *toolbox_shell = NULL;

static void
tools_select_update (GtkToggleButton *w,
		     gpointer         data)
{
  ToolType tool_type;

  tool_type = (ToolType) GPOINTER_TO_INT (data);

  if ((tool_type != -1) && gtk_toggle_button_get_active (w))
    tools_select (tool_type);
}

static void
tools_double_click (GtkGestureClick *gesture,
		    int              n_press,
		    double           x,
		    double           y,
		    gpointer         data)
{
  if (n_press == 2)
    tools_options_dialog_show ();
}

void
tools_select_widget (ToolType type)
{
  int position = tool_info[(int) type].toolbar_position;

  if (position >= 0 && position < (int) NUM_TOOLS && tool_widgets[position])
    {
      GtkToggleButton *button = GTK_TOGGLE_BUTTON (tool_widgets[position]);

      /*  Selecting the tool again restarts it, as activating the
       *  radio button did.
       */
      if (gtk_toggle_button_get_active (button))
	tools_select (type);
      else
	gtk_toggle_button_set_active (button, TRUE);
    }
}

static gboolean
toolbox_delete (GtkWindow *w,
		gpointer   data)
{
  app_exit (0);

  return TRUE;
}

static void
toolbox_destroy (GtkWidget *w,
		 gpointer   data)
{
  app_exit_finish ();
}

static void
gdisplay_destroy (GtkWidget *w,
		  GDisplay  *gdisp)
{
  gdisplay_remove_and_delete (gdisp);
}

static gboolean
gdisplay_delete (GtkWindow *w,
		 GDisplay  *gdisp)
{
  gdisplay_close_window (gdisp, FALSE);

  return TRUE;
}

static void
gdisplay_active_changed (GtkWindow  *window,
			 GParamSpec *pspec,
			 GDisplay   *gdisp)
{
  if (gtk_window_is_active (window))
    gdisplay_set_active (gdisp);
}


GdkTexture *
create_pixmap_texture (char **data,
		       int    width,
		       int    height)
{
  GBytes *bytes;
  GdkTexture *texture;
  guchar *pixels;
  int r, s;

  pixels = g_malloc0 (width * height * 4);

  for (r = 0; r < height; r++)
    for (s = 0; s < width && data[r][s]; s++)
      {
	guchar *p = pixels + (r * width + s) * 4;
	char value = data[r][s];

	if (value >= 'a' && value <= 'h')
	  {
	    p[0] = p[1] = p[2] = pixmap_colors[value - 'a'];
	    p[3] = 0xff;
	  }
      }

  bytes = g_bytes_new_take (pixels, width * height * 4);
  texture = gdk_memory_texture_new (width, height, GDK_MEMORY_R8G8B8A8,
				    bytes, width * 4);
  g_bytes_unref (bytes);

  return texture;
}

GtkWidget*
create_pixmap_widget (char **data,
		      int    width,
		      int    height)
{
  GdkTexture *texture;
  GtkWidget *picture;

  /*  A GtkImage shows the icon at its own size; a GtkPicture would
   *  stretch it to fill the button.
   */
  texture = create_pixmap_texture (data, width, height);
  picture = gtk_image_new_from_paintable (GDK_PAINTABLE (texture));
  gtk_image_set_pixel_size (GTK_IMAGE (picture), MAX (width, height));
  g_object_unref (texture);

  return picture;
}


static void
create_color_area (GtkWidget *parent)
{
  GtkWidget *frame;
  GtkWidget *col_area;
  GdkTexture *default_texture;
  GdkTexture *swap_texture;

  default_texture = create_pixmap_texture (default_bits,
					   default_width, default_height);
  swap_texture    = create_pixmap_texture (swap_bits,
					   swap_width, swap_height);

  frame = gtk_frame_new (NULL);
  gimp_box_pack_start (parent, frame, FALSE, FALSE, 0);

  col_area = color_area_create (54, 42, default_texture, swap_texture);
  gtk_widget_set_halign (col_area, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (col_area, GTK_ALIGN_CENTER);
  gimp_container_set_border_width (frame, 3);
  gtk_frame_set_child (GTK_FRAME (frame), col_area);
  gtk_widget_set_tooltip_text (col_area,
			       "Foreground & background colors.  The small black "
			       "and white squares reset colors.  The small arrows swap colors.  Double "
			       "click to change colors.");
}


static void
create_tools (GtkWidget *parent)
{
  GtkWidget *table;
  GtkWidget *button;
  GtkWidget *pixmap;
  GtkWidget *group;
  GtkGesture *click;
  gint i;

  table = gimp_table_new (ROWS, COLUMNS, TRUE);
  gimp_box_pack_start (parent, table, FALSE, FALSE, 0);

  group = NULL;

  for (i = 0; i < 21; i++)
    {
      tool_widgets[i] = button = gtk_toggle_button_new ();
      if (group)
	gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (button),
				     GTK_TOGGLE_BUTTON (group));
      else
	group = button;

      gimp_table_attach (table, button,
			 (i % 3), (i % 3) + 1,
			 (i / 3), (i / 3) + 1,
			 GIMP_FILL, GIMP_FILL, 0, 0);

      pixmap = create_pixmap_widget (tool_data[i].icon_data, 22, 22);
      gtk_button_set_child (GTK_BUTTON (button), pixmap);

      g_signal_connect (button, "toggled",
			G_CALLBACK (tools_select_update),
			tool_data[i].callback_data);

      click = gtk_gesture_click_new ();
      gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 1);
      gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (click),
						  GTK_PHASE_CAPTURE);
      g_signal_connect (click, "pressed",
			G_CALLBACK (tools_double_click),
			tool_data[i].callback_data);
      gtk_widget_add_controller (button, GTK_EVENT_CONTROLLER (click));

      gtk_widget_set_tooltip_text (button, tool_data[i].tool_desc);
    }

  /*  The non-visible tool buttons  */
  for (i = 21; i < NUM_TOOLS; i++)
    {
      tool_widgets[i] = button = gtk_toggle_button_new ();
      g_object_ref_sink (button);
      gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (button),
				   GTK_TOGGLE_BUTTON (group));

      g_signal_connect (button, "toggled",
			G_CALLBACK (tools_select_update),
			tool_data[i].callback_data);
    }
}


void
create_toolbox ()
{
  GtkWidget *window;
  GtkWidget *main_vbox;
  GtkWidget *vbox;
  GtkWidget *menubar;

  window = gtk_window_new ();
  gtk_window_set_title (GTK_WINDOW (window), "The GIMP");
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
  g_signal_connect (window, "close-request",
		    G_CALLBACK (toolbox_delete),
		    NULL);

  g_signal_connect (window, "destroy",
		    G_CALLBACK (toolbox_destroy),
		    NULL);

  main_vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (main_vbox, 1);
  gtk_window_set_child (GTK_WINDOW (window), main_vbox);

  /*  Build the menu bar with menus  */
  menubar = gtk_popover_menu_bar_new_from_model (menus_get_toolbox_model ());
  gimp_box_pack_start (main_vbox, menubar, FALSE, TRUE, 0);

  /*  Install the accelerator table in the main window  */
  menus_install (window, "<Toolbox>");

  vbox = gimp_vbox_new (FALSE, 1);
  gimp_box_pack_start (main_vbox, vbox, TRUE, TRUE, 0);

  create_tools (vbox);
  create_color_area (vbox);

  gtk_window_present (GTK_WINDOW (window));
  toolbox_shell = window;
}

void
toolbox_free ()
{
  int i;

  gtk_window_destroy (GTK_WINDOW (toolbox_shell));
  for (i = 21; i < NUM_TOOLS; i++)
    g_object_unref (tool_widgets[i]);
}

void
toolbox_raise_callback (GtkWidget *widget,
			gpointer  client_data)
{
  gtk_window_present (GTK_WINDOW (toolbox_shell));
}


/*  The size of the monitor the toolbox is on, or of the first one.  */
static void
interface_monitor_size (int *width,
			int *height)
{
  GdkDisplay *display = gdk_display_get_default ();
  GdkMonitor *monitor = NULL;
  GdkRectangle geometry = { 0, 0, 1024, 768 };

  if (toolbox_shell && gtk_widget_get_native (toolbox_shell))
    {
      GdkSurface *surface = gtk_native_get_surface (gtk_widget_get_native (toolbox_shell));

      if (surface)
	monitor = gdk_display_get_monitor_at_surface (display, surface);
    }

  if (!monitor)
    {
      GListModel *monitors = gdk_display_get_monitors (display);

      if (g_list_model_get_n_items (monitors) > 0)
	{
	  monitor = g_list_model_get_item (monitors, 0);
	  g_object_unref (monitor);
	}
    }

  if (monitor)
    gdk_monitor_get_geometry (monitor, &geometry);

  *width = geometry.width;
  *height = geometry.height;
}

static void
canvas_unparent_popup (GtkWidget *canvas,
		       GtkWidget *popup)
{
  gtk_widget_unparent (popup);
}

void
create_display_shell (int   gdisp_id,
		      int   width,
		      int   height,
		      char *title,
		      int   type)
{
  GDisplay *gdisp;
  GtkWidget *table;
  GtkEventController *controller;
  int n_width, n_height;
  int s_width, s_height;
  int scalesrc, scaledest;

  /*  Get the gdisplay  */
  if (! (gdisp = gdisplay_get_ID (gdisp_id)))
    return;

  /*  adjust the initial scale -- so that window fits on screen */
  {
    interface_monitor_size (&s_width, &s_height);

    /*  leave room for the window frame, rulers, scrollbars and taskbar  */
    s_width  -= 64;
    s_height -= 128;

    scalesrc = gdisp->scale & 0x00ff;
    scaledest = gdisp->scale >> 8;

    n_width = (width * scaledest) / scalesrc;
    n_height = (height * scaledest) / scalesrc;

    /*  Limit to the size of the screen...  */
    while (n_width > s_width || n_height > s_height)
      {
	if (scaledest > 1)
	  scaledest--;
	else
	  if (scalesrc < 0xff)
	    scalesrc++;
	  else
	    break;

	n_width = (width * scaledest) / scalesrc;
	n_height = (height * scaledest) / scalesrc;
      }

    gdisp->scale = (scaledest << 8) + scalesrc;
  }

  /*  The adjustment datums  */
  gdisp->hsbdata = gtk_adjustment_new (0, 0, width, 1, 1, width);
  gdisp->vsbdata = gtk_adjustment_new (0, 0, height, 1, 1, height);

  /*  The toplevel shell */
  gdisp->shell = gtk_window_new ();
  gtk_window_set_title (GTK_WINDOW (gdisp->shell), title);
  g_object_set_data (G_OBJECT (gdisp->shell), "user_data", gdisp);
  g_signal_connect (gdisp->shell, "close-request",
		    G_CALLBACK (gdisplay_delete),
		    gdisp);
  g_signal_connect (gdisp->shell, "destroy",
		    G_CALLBACK (gdisplay_destroy),
		    gdisp);
  g_signal_connect (gdisp->shell, "notify::is-active",
		    G_CALLBACK (gdisplay_active_changed),
		    gdisp);

  /*  the menu actions and accelerators for images  */
  menus_install (gdisp->shell, "<Image>");

  /*  the table containing all widgets  */
  table = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (table), 1);
  gtk_grid_set_row_spacing (GTK_GRID (table), 1);
  gimp_container_set_border_width (table, 2);
  gtk_window_set_child (GTK_WINDOW (gdisp->shell), table);

  /*  the corner between the rulers opens the image menu  */
  gdisp->origin = gtk_menu_button_new ();
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (gdisp->origin),
				  menus_get_image_model ());
  gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (gdisp->origin),
				 "pan-end-symbolic");
  gtk_menu_button_set_has_frame (GTK_MENU_BUTTON (gdisp->origin), FALSE);
  gtk_widget_set_size_request (gdisp->origin, 16, 16);
  gtk_widget_add_css_class (gdisp->origin, "flat");
  gtk_widget_set_tooltip_text (gdisp->origin, "Image menu");

  /*  scrollbars, rulers, canvas  */
  gdisp->hrule = gimp_ruler_new (GTK_ORIENTATION_HORIZONTAL);
  controller = gtk_event_controller_legacy_new ();
  g_signal_connect (controller, "event",
		    G_CALLBACK (gdisplay_hruler_events), gdisp);
  gtk_widget_add_controller (gdisp->hrule, controller);

  gdisp->vrule = gimp_ruler_new (GTK_ORIENTATION_VERTICAL);
  controller = gtk_event_controller_legacy_new ();
  g_signal_connect (controller, "event",
		    G_CALLBACK (gdisplay_vruler_events), gdisp);
  gtk_widget_add_controller (gdisp->vrule, controller);

  gdisp->hsb = gtk_scrollbar_new (GTK_ORIENTATION_HORIZONTAL, gdisp->hsbdata);
  gtk_widget_set_focusable (gdisp->hsb, FALSE);
  gdisp->vsb = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, gdisp->vsbdata);
  gtk_widget_set_focusable (gdisp->vsb, FALSE);

  gdisp->canvas = gtk_drawing_area_new ();
  gtk_widget_set_size_request (gdisp->canvas, 32, 32);
  gtk_widget_set_hexpand (gdisp->canvas, TRUE);
  gtk_widget_set_vexpand (gdisp->canvas, TRUE);
  gtk_widget_set_focusable (gdisp->canvas, TRUE);
  gtk_widget_set_can_focus (gdisp->canvas, TRUE);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (gdisp->canvas),
				  gdisplay_canvas_draw_func, gdisp, NULL);
  g_signal_connect (gdisp->canvas, "resize",
		    G_CALLBACK (gdisplay_canvas_resize), gdisp);
  controller = gtk_event_controller_legacy_new ();
  g_signal_connect (controller, "event",
		    G_CALLBACK (gdisplay_canvas_events), gdisp);
  gtk_widget_add_controller (gdisp->canvas, controller);
  g_object_set_data (G_OBJECT (gdisp->canvas), "user_data", gdisp);

  /*  pack all the widgets  */
  gtk_grid_attach (GTK_GRID (table), gdisp->origin, 0, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (table), gdisp->hrule,  1, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (table), gdisp->vrule,  0, 1, 1, 1);
  gtk_grid_attach (GTK_GRID (table), gdisp->canvas, 1, 1, 1, 1);
  gtk_grid_attach (GTK_GRID (table), gdisp->hsb,    0, 2, 2, 1);
  gtk_grid_attach (GTK_GRID (table), gdisp->vsb,    2, 0, 1, 2);

  /*  the popup menu  */
  gdisp->popup = gtk_popover_menu_new_from_model (menus_get_image_model ());
  gtk_popover_set_has_arrow (GTK_POPOVER (gdisp->popup), FALSE);
  gtk_widget_set_halign (gdisp->popup, GTK_ALIGN_START);
  gtk_widget_set_parent (gdisp->popup, gdisp->canvas);
  g_signal_connect (gdisp->canvas, "destroy",
		    G_CALLBACK (canvas_unparent_popup), gdisp->popup);

  if (!show_rulers)
    {
      gtk_widget_set_visible (gdisp->origin, FALSE);
      gtk_widget_set_visible (gdisp->hrule, FALSE);
      gtk_widget_set_visible (gdisp->vrule, FALSE);
    }

  /*  room for the image, the rulers and the scrollbars  */
  gtk_window_set_default_size (GTK_WINDOW (gdisp->shell),
			       n_width + 40, n_height + 40);

  gtk_window_present (GTK_WINDOW (gdisp->shell));

  /*  set the focus to the canvas area  */
  gtk_widget_grab_focus (gdisp->canvas);

  gdisplay_set_active (gdisp);
}


/*
 *  A text string query box
 */

typedef struct _QueryBox QueryBox;

struct _QueryBox
{
  GtkWidget *qbox;
  GtkWidget *entry;
  QueryFunc callback;
  gpointer data;
};

static void query_box_cancel_callback (GtkWidget *, gpointer);
static void query_box_ok_callback (GtkWidget *, gpointer);
static gboolean query_box_delete_callback (GtkWindow *, gpointer);

GtkWidget *
query_string_box (char        *title,
		  char        *message,
		  char        *initial,
		  QueryFunc    callback,
		  gpointer     data)
{
  QueryBox  *query_box;
  GtkWidget *qbox;
  GtkWidget *vbox;
  GtkWidget *label;
  GtkWidget *entry;

  query_box = (QueryBox *) g_malloc (sizeof (QueryBox));

  qbox = gimp_dialog_new (title);
  gtk_window_set_resizable (GTK_WINDOW (qbox), FALSE);
  g_signal_connect (qbox, "close-request",
		    G_CALLBACK (query_box_delete_callback),
		    query_box);

  gimp_dialog_add_button (qbox, "OK", G_CALLBACK (query_box_ok_callback),
			  query_box, TRUE);
  gimp_dialog_add_button (qbox, "Cancel", G_CALLBACK (query_box_cancel_callback),
			  query_box, FALSE);

  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, 2);
  gimp_box_pack_start (gimp_dialog_get_vbox (qbox), vbox, TRUE, TRUE, 0);

  label = gtk_label_new (message);
  gimp_box_pack_start (vbox, label, TRUE, FALSE, 0);

  entry = gtk_entry_new ();
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  gimp_box_pack_start (vbox, entry, TRUE, TRUE, 0);
  if (initial)
    gtk_editable_set_text (GTK_EDITABLE (entry), initial);

  query_box->qbox = qbox;
  query_box->entry = entry;
  query_box->callback = callback;
  query_box->data = data;

  gtk_window_present (GTK_WINDOW (qbox));
  gtk_widget_grab_focus (entry);

  return qbox;
}

static gboolean
query_box_delete_callback (GtkWindow *w,
			   gpointer   client_data)
{
  query_box_cancel_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}

static void
query_box_cancel_callback (GtkWidget *w,
			   gpointer   client_data)
{
  QueryBox *query_box;

  query_box = (QueryBox *) client_data;

  /*  Destroy the box  */
  gtk_window_destroy (GTK_WINDOW (query_box->qbox));

  g_free (query_box);
}

static void
query_box_ok_callback (GtkWidget *w,
		       gpointer   client_data)
{
  QueryBox *query_box;
  char *string;

  query_box = (QueryBox *) client_data;

  /*  Get the entry data  */
  string = g_strdup (gtk_editable_get_text (GTK_EDITABLE (query_box->entry)));

  /*  Call the user defined callback  */
  (* query_box->callback) (w, query_box->data, (gpointer) string);

  /*  Destroy the box  */
  gtk_window_destroy (GTK_WINDOW (query_box->qbox));

  g_free (query_box);
}


/*
 *  Message Boxes...
 */

typedef struct _MessageBox MessageBox;

struct _MessageBox
{
  GtkWidget          *mbox;
  MessageBoxCallback  callback;
  gpointer            data;
};

static void message_box_close_callback (GtkWidget *, gpointer);
static gboolean message_box_delete_callback (GtkWindow *, gpointer);

GtkWidget *
message_box (char               *message,
	     MessageBoxCallback  callback,
	     gpointer            data)
{
  MessageBox *msg_box;
  GtkWidget *mbox;
  GtkWidget *label;

  if (!message)
    return NULL;

  msg_box = (MessageBox *) g_malloc (sizeof (MessageBox));

  mbox = gimp_dialog_new ("GIMP Message");
  gtk_window_set_resizable (GTK_WINDOW (mbox), FALSE);
  g_signal_connect (mbox, "close-request",
		    G_CALLBACK (message_box_delete_callback),
		    msg_box);

  gimp_dialog_add_button (mbox, "OK", G_CALLBACK (message_box_close_callback),
			  msg_box, TRUE);

  label = gtk_label_new (message);
  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
  gtk_label_set_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 70);
  gtk_label_set_selectable (GTK_LABEL (label), TRUE);
  gimp_container_set_border_width (label, 8);
  gimp_box_pack_start (gimp_dialog_get_vbox (mbox), label, TRUE, FALSE, 0);

  msg_box->mbox = mbox;
  msg_box->callback = callback;
  msg_box->data = data;

  gtk_window_present (GTK_WINDOW (mbox));

  return mbox;
}

static gboolean
message_box_delete_callback (GtkWindow *w,
			     gpointer   client_data)
{
  message_box_close_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}


static void
message_box_close_callback (GtkWidget *w,
			    gpointer   client_data)
{
  MessageBox *msg_box;

  msg_box = (MessageBox *) client_data;

  /*  If there is a valid callback, invoke it  */
  if (msg_box->callback)
    (* msg_box->callback) (w, msg_box->data);

  /*  Destroy the box  */
  gtk_window_destroy (GTK_WINDOW (msg_box->mbox));

  g_free (msg_box);
}


/*  The progress area under the tools was never built; plug-ins show
 *  their progress in the image window's info instead.  These stay for
 *  the procedures that call them.
 */
void
progress_start ()
{
}

void
progress_update (float percentage)
{
}

void
progress_step ()
{
}

void
progress_end ()
{
}
