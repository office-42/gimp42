/*
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This is a plug-in for the GIMP.
 *
 * Tileit - This plugin will take an image an make repeated
 * copies of it the stepping is 1/(2**n); 1<=n<=6
 *
 * Copyright (C) 1997 Andy Thomas  alt@picnic.demon.co.uk
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
 * 
 * A fair proprotion of this code was taken from the Whirl plug-in
 * which was copyrighted by Federico Mena Quintero (as below).
 * 
 * Whirl plug-in --- distort an image into a whirlpool
 * Copyright (C) 1997 Federico Mena Quintero           
 *
 */

/* Change log:-
 * 0.2  Added new functions to allow "editing" of the tile patten.
 *
 * 0.1 First version released.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"



/***** Magic numbers *****/

#define PREVIEW_SIZE 128 
#define SCALE_WIDTH  80
#define ENTRY_WIDTH  25

/* Even more stuff from Quartics plugins */
#define CHECK_SIZE  8
#define CHECK_DARK  ((int) (1.0 / 3.0 * 255))
#define CHECK_LIGHT ((int) (2.0 / 3.0 * 255))

#define MAX_SEGS 6

/* Variables set in dialog box */
typedef struct data {
    gint numtiles;
} TileItVals;

typedef struct {
  GtkWidget *preview;
  guchar     preview_row[PREVIEW_SIZE * 4];
  gint run;
  guchar * pv_cache;
  gint img_bpp;
  GtkWidget *sel_area;  /* drawn over the preview: explicit tile outline */
} TileItInterface;

static TileItInterface tint =
{
  NULL,  /* Preview */
  {'4','u'},    /* Preview_row */
  FALSE, /* run */
  NULL,
  4,     /* bpp of drawable */
  NULL
};

GDrawable *tileitdrawable;
static gint   tile_width, tile_height;
static GTile *the_tile = NULL;
static gint   img_width, img_height,img_bpp;

static void      query  (void);
static void      run    (gchar    *name,
			 gint      nparams,
			 GParam   *param,
			 gint     *nreturn_vals,
			 GParam  **return_vals);
/* static void      check  (GDrawable * drawable); */

static gint      tileit_dialog (void);
static void      tileit_close_callback (GtkWidget *widget, gpointer   data);
static void      tileit_ok_callback (GtkWidget *widget, gpointer   data);
static void      tileit_scale_update (GtkAdjustment *adjustment, gint *size_val);
static void      tileit_entry_update(GtkWidget *widget, gint *value);
static void      tileit_exp_update(GtkWidget *widget, gpointer value);
static void      tileit_exp_update_f(GtkWidget *widget, gpointer value);
static void      tileit_reset(GtkWidget *widget, gpointer value);
static void      tileit_toggle_update(GtkWidget *widget, gpointer   data);
static void      tileit_hvtoggle_update(GtkWidget *widget, gpointer   data);

static void      do_tiles(void);
static gint      tiles_xy(gint width, gint height,gint x,gint y,gint *nx,gint *ny);
static void      all_update(void);
static void      alt_update(void);
static void      explict_update(gint);

static void      dialog_update_preview(void);
static void	 cache_preview(void);
static void      tileit_sel_draw (GtkDrawingArea *area, cairo_t *cr,
				  int w, int h, gpointer data);
static void      tileit_preview_drag_begin (GtkGestureDrag *gesture,
					    gdouble x, gdouble y,
					    gpointer data);
static void      tileit_preview_drag_update (GtkGestureDrag *gesture,
					     gdouble offset_x, gdouble offset_y,
					     gpointer data);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

/* Values when first invoked */
static TileItVals itvals =
{
  2
};

/* Structures for call backs... */
/* The "explict tile" & family */
typedef enum {
  ALL,
  ALT,
  EXPLICT
} AppliedTo;

typedef struct {
  AppliedTo type;
  gint x; /* X - pos of tile */
  gint y; /* Y - pos of tile */
  GtkWidget *r_label; /* row label */
  GtkWidget *r_entry; /* row entry */
  GtkWidget *c_label; /* column label */
  GtkWidget *c_entry; /* column entry */
  GtkWidget *applybut; /* The apply button */
} Exp_Call;

Exp_Call exp_call = {
  ALL,
  -1,
  -1,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
};

/* The reset button needs to know some toggle widgets.. */

typedef struct {
  GtkWidget *htoggle;
  GtkWidget *vtoggle;
} Reset_Call;

Reset_Call res_call = {
  NULL,
  NULL,
};
  
/* 2D - Array that holds the actions for each tile */
/* Action type on cell */
#define HORIZONTAL 0x1
#define VERTICAL   0x2

gint tileactions[MAX_SEGS][MAX_SEGS];

/* What actions buttons toggled */
gint do_horz = FALSE;
gint do_vert = FALSE;
gint opacity = 100;

/* Stuff for the preview bit */
static gint   sel_x1, sel_y1, sel_x2, sel_y2;
static gint   sel_width, sel_height;
static gint   preview_width, preview_height;
static gint   has_alpha;

MAIN ()

static void
query ()
{
  static GParamDef args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image (unused)" },
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
    { PARAM_INT32, "number_of_tiles", "Number of tiles to make" } 
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0;

  gimp_install_procedure ("plug_in_small_tiles",
			  "Tiles image into smaller versions of the orginal",
			  "More here later",
			  "Andy Thomas",
			  "Andy Thomas",
			  "1997",
			  "<Image>/Filters/Map/Small Tiles",
			  "RGB*, GRAY*",
			  PROC_PLUG_IN,
			  nargs, nreturn_vals,
			  args, return_vals);
}

static void
run    (gchar    *name,
	gint      nparams,
	GParam   *param,
	gint     *nreturn_vals,
	GParam  **return_vals)
{
  static GParam values[1];
  GDrawable *drawable;
  GRunModeType run_mode;
  GStatusType status = STATUS_SUCCESS;

  int           pwidth, pheight;


  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;

  tileitdrawable = 
    drawable = 
    gimp_drawable_get (param[2].data.d_drawable);

  tile_width  = gimp_tile_width();
  tile_height = gimp_tile_height();

  gimp_drawable_mask_bounds(drawable->id, &sel_x1, &sel_y1, &sel_x2, &sel_y2);

  sel_width  = sel_x2 - sel_x1;
  sel_height = sel_y2 - sel_y1;
  
  /* Calculate preview size */
  
  if (sel_width > sel_height) {
    pwidth  = MIN(sel_width, PREVIEW_SIZE);
    pheight = sel_height * pwidth / sel_width;
  } else {
    pheight = MIN(sel_height, PREVIEW_SIZE);
    pwidth  = sel_width * pheight / sel_height;
  }
  
  preview_width  = MAX(pwidth, 2);  /* Min size is 2 */
  preview_height = MAX(pheight, 2); 

  switch (run_mode)
    {
    case RUN_INTERACTIVE:
      gimp_get_data ("plug_in_tileit", &itvals);
      if (! tileit_dialog())
	{
	  gimp_drawable_detach (drawable);
	  return;
	}
      break;

    case RUN_NONINTERACTIVE:
      if (nparams != 4)
	status = STATUS_CALLING_ERROR;
      if (status == STATUS_SUCCESS)
	{
	  itvals.numtiles = param[3].data.d_int32;
	}
      break;

    case RUN_WITH_LAST_VALS:
      gimp_get_data ("plug_in_tileit", &itvals);
      break;

    default:
      break;
    }

  if (gimp_drawable_color (drawable->id) || gimp_drawable_gray (drawable->id))
    {
      /* Set the tile cache size */

      gimp_tile_cache_ntiles((drawable->width + gimp_tile_width() - 1) / gimp_tile_width());

      gimp_progress_init ("Tiling ...");

      do_tiles();
   
      if (run_mode != RUN_NONINTERACTIVE)
	gimp_displays_flush ();

      if (run_mode == RUN_INTERACTIVE)
	gimp_set_data ("plug_in_tileit", &itvals, sizeof (TileItVals));
    }
  else
    {
      status = STATUS_EXECUTION_ERROR;
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

/* Build the dialog up. This was the hard part! */
static gint
tileit_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *frame;
  GtkWidget *xframe;
  GtkWidget *overlay;
  GtkWidget *table;
  GtkWidget *table2;
  GtkWidget *table3;
  GtkWidget *table4;
  GtkWidget *label;
  GtkWidget *entry;
  GtkWidget *slider;
  GtkAdjustment *size_data;
  GtkAdjustment *op_data;
  GtkWidget *toggle;
  GtkGesture *drag;
  char buf[256];

  gtk_init ();

  cache_preview(); /* Get the preview image and store it also set has_alpha */

  /* Start buildng the dialog up */
  dlg = gimp_dialog_new ("TileIt");
  g_signal_connect (dlg, "destroy",
		    G_CALLBACK (tileit_close_callback),
		    NULL);

  /*  Action area  */
  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (tileit_ok_callback),
			  dlg, TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);


  /* Start building the frame for the preview area */

  frame = gtk_frame_new ("preview");
  gimp_container_set_border_width (frame, 1);
  table = gimp_table_new (6, 6, FALSE);
  gimp_container_set_border_width (table, 1);
  gtk_frame_set_child (GTK_FRAME (frame), table);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);
  tint.preview = gimp_preview_new (GIMP_PREVIEW_COLOR);
  gimp_preview_size (GIMP_PREVIEW (tint.preview), preview_width, preview_height);

  /* The explicit tile selection is drawn on a drawing area laid over
   * the preview, which also takes the mouse events.
   */
  tint.sel_area = gtk_drawing_area_new ();
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (tint.sel_area),
				  tileit_sel_draw, NULL, NULL);
  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), 0);
  g_signal_connect (drag, "drag-begin",
		    G_CALLBACK (tileit_preview_drag_begin), NULL);
  g_signal_connect (drag, "drag-update",
		    G_CALLBACK (tileit_preview_drag_update), NULL);
  gtk_widget_add_controller (tint.sel_area, GTK_EVENT_CONTROLLER (drag));

  overlay = gtk_overlay_new ();
  gtk_overlay_set_child (GTK_OVERLAY (overlay), tint.preview);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), tint.sel_area);
  gtk_widget_set_halign (overlay, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (overlay, GTK_ALIGN_CENTER);

  xframe = gtk_frame_new(NULL);
  gtk_frame_set_child (GTK_FRAME (xframe), overlay);
  gimp_table_attach (table, xframe, 0, 1, 0, 2, GIMP_EXPAND, GIMP_EXPAND, 0, 0);

  /* Area for buttons etc */
  /* This was built up incrementally... shows does'nt it */

  frame = gtk_frame_new("Flipping");
  gimp_container_set_border_width (frame, 1);
  table2 = gimp_table_new (7, 7, FALSE);
  gimp_container_set_border_width (table2, 1);
  gtk_frame_set_child (GTK_FRAME (frame), table2);

  toggle = gtk_check_button_new_with_label ("Horizontal");
  gimp_table_attach (table2, toggle, 0, 1, 1, 2, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (tileit_hvtoggle_update),
		    &do_horz);
  res_call.htoggle = toggle;

  toggle = gtk_check_button_new_with_label ("Vertical");
  gimp_table_attach (table2, toggle, 1, 2, 1, 2, GIMP_EXPAND, GIMP_EXPAND, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (tileit_hvtoggle_update),
		    &do_vert);
  res_call.vtoggle = toggle;


  xframe = gtk_frame_new("Applied to tile");
  gimp_container_set_border_width (xframe, 10);
  gimp_table_attach (table2, xframe, 0, 2, 2, 3, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);

  /* Table for the inner widgets..*/
  table4 = gimp_table_new (6, 6, FALSE);
  gimp_container_set_border_width (table4, 10);
  gtk_frame_set_child (GTK_FRAME (xframe), table4);

  toggle = gimp_radio_button_new (NULL, "All tiles");
  gimp_table_attach (table4, toggle, 0, 3, 0, 1, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (tileit_toggle_update),
		    GINT_TO_POINTER (ALL));

  toggle = gimp_radio_button_new (toggle, "Alternate tiles");
  gimp_table_attach (table4, toggle, 0, 3, 1, 2, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (tileit_toggle_update),
		    GINT_TO_POINTER (ALT));

  toggle = gimp_radio_button_new (toggle, "Explict tile");
  gimp_table_attach (table4, toggle, 0, 1, 2, 3, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);

  /* Table for the stuff next to the explict button */
  table3 = gimp_table_new (6, 6, FALSE);

  label = gtk_label_new ("Row");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table3, label, 0 , 1, 0, 1, GIMP_FILL | GIMP_EXPAND , GIMP_FILL, 1, 1);
  gtk_widget_set_sensitive(label,FALSE);
  exp_call.r_label = label;

  entry = gtk_entry_new();
  gtk_widget_set_size_request(entry, ENTRY_WIDTH, -1);
  gtk_editable_set_width_chars (GTK_EDITABLE (entry), 2);
  sprintf(buf, "%.1d", 2);
  gtk_editable_set_text(GTK_EDITABLE(entry), buf);
  gimp_table_attach (table3, entry, 2 , 3, 0, 1, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);
  gtk_widget_set_sensitive(entry,FALSE);
  g_signal_connect(entry, "changed",
		   G_CALLBACK (tileit_exp_update_f),
		   &exp_call);
  exp_call.r_entry = entry;

  label = gtk_label_new ("Column");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table3, label, 0 , 1, 1, 2, GIMP_FILL , GIMP_FILL, 1, 1);
  gtk_widget_set_sensitive(label,FALSE);
  exp_call.c_label = label;

  entry = gtk_entry_new();
  gtk_widget_set_size_request(entry, ENTRY_WIDTH, -1);
  gtk_editable_set_width_chars (GTK_EDITABLE (entry), 2);
  sprintf(buf, "%.1d", 2);
  gtk_editable_set_text(GTK_EDITABLE(entry), buf);
  gtk_widget_set_sensitive(entry,FALSE);
  gimp_table_attach (table3, entry, 2 , 3, 1, 2, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);
  g_signal_connect(entry, "changed",
		   G_CALLBACK (tileit_exp_update_f),
		   &exp_call);
  exp_call.c_entry = entry;

  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (tileit_toggle_update),
		    GINT_TO_POINTER (EXPLICT));

  button = gtk_button_new_with_label ("Apply");
  gtk_widget_set_sensitive(button,FALSE);
  gimp_table_attach (table3, button, 3, 4, 0, 3, 0, 0, 1, 1);
  g_signal_connect(button, "clicked",
		   G_CALLBACK (tileit_exp_update),
		   &exp_call);
  exp_call.applybut = button;

  /* Widget for selecting the Opacity */
  sprintf(buf,"Opacity: ");
  label = gtk_label_new (buf);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table4, label, 0, 1, 3, 4, 0, 0, 0, 0);

  op_data = gtk_adjustment_new (100, 0, 100, 1, 1, 0);
  slider = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, op_data);
  gtk_widget_set_size_request (slider, SCALE_WIDTH, -1);
  gimp_table_attach (table4, slider, 1,3 , 3, 4, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);

  gtk_scale_set_draw_value (GTK_SCALE (slider), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE (slider), GTK_POS_LEFT);
  gtk_scale_set_digits (GTK_SCALE (slider), 0);

  g_signal_connect (op_data, "value-changed",
		    G_CALLBACK (tileit_scale_update),
		    &opacity);
  if(!has_alpha)
    gtk_widget_set_sensitive(slider,FALSE);

  entry = gtk_entry_new();
  g_object_set_data(G_OBJECT(entry), "user_data", op_data);
  g_object_set_data(G_OBJECT(op_data), "user_data", entry);
  gtk_widget_set_size_request(entry, 3*ENTRY_WIDTH/2, -1);
  gtk_editable_set_width_chars (GTK_EDITABLE (entry), 3);
  sprintf(buf, "%.1d", opacity);
  gtk_editable_set_text(GTK_EDITABLE(entry), buf);
  g_signal_connect(entry, "changed",
		   G_CALLBACK (tileit_entry_update),
		   &opacity);
  gimp_table_attach(table4, entry, 3, 4, 3, 4, GIMP_FILL, GIMP_FILL, 0, 0);
  if(!has_alpha)
    gtk_widget_set_sensitive(entry,FALSE);

  gimp_table_attach (table4, table3, 1, 2, 2, 3, GIMP_FILL , GIMP_FILL, 0, 0);

  button = gtk_button_new_with_label ("Reset");
  gimp_table_attach (table2, button, 0, 2, 5, 6, 0 , 0, 0, 0);
  g_signal_connect(button, "clicked",
		   G_CALLBACK (tileit_reset),
		   &res_call);

  gimp_table_attach(table, frame, 1, 2, 0, 2, GIMP_EXPAND , GIMP_EXPAND, 0, 0);

  /* Lower frame saying howmany segments */

  frame = gtk_frame_new ("Segment Setting");
  gimp_container_set_border_width (frame, 1);
  table = gimp_table_new (5, 5, FALSE);
  gimp_container_set_border_width (table, 1);
  gtk_frame_set_child (GTK_FRAME (frame), table);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);

  sprintf(buf,"1/(%d**n) ",2);
  label = gtk_label_new (buf);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 1, 2, GIMP_EXPAND, GIMP_FILL, 0, 0);

  size_data = gtk_adjustment_new (itvals.numtiles, 2, MAX_SEGS, 1, 1, 0);
  slider = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, size_data);
  gtk_widget_set_size_request (slider, SCALE_WIDTH, -1);
  gimp_table_attach (table, slider, 2,3 , 1, 2, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);
  gtk_scale_set_draw_value (GTK_SCALE (slider), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE (slider), GTK_POS_LEFT);
  gtk_scale_set_digits (GTK_SCALE (slider), 0);
  g_signal_connect (size_data, "value-changed",
		    G_CALLBACK (tileit_scale_update),
		    &itvals.numtiles);

  entry = gtk_entry_new();
  g_object_set_data(G_OBJECT(entry), "user_data", size_data);
  g_object_set_data(G_OBJECT(size_data), "user_data", entry);
  gtk_widget_set_size_request(entry, ENTRY_WIDTH, -1);
  gtk_editable_set_width_chars (GTK_EDITABLE (entry), 2);
  sprintf(buf, "%.1d", itvals.numtiles);
  gtk_editable_set_text(GTK_EDITABLE(entry), buf);
  g_signal_connect(entry, "changed",
		   G_CALLBACK (tileit_entry_update),
		   &itvals.numtiles);
  gimp_table_attach(table, entry, 3, 4, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0);

  gtk_window_present (GTK_WINDOW (dlg));
  dialog_update_preview();

  gimp_main_loop_run ();

  return tint.run;
}

static void
tileit_close_callback (GtkWidget *widget,
			 gpointer   data)
{
  gimp_main_loop_quit ();
}

static void
tileit_ok_callback (GtkWidget *widget,
		      gpointer   data)
{
  tint.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}

/* Draws the outline of the explicitly selected tile over the preview.
 * This used to be XORed straight onto the preview window.
 */
static void
tileit_sel_draw (GtkDrawingArea *area,
		 cairo_t        *cr,
		 int             w,
		 int             h,
		 gpointer        data)
{
  if(exp_call.type == EXPLICT)
    {
      gdouble x,y;
      gdouble width = (gdouble)preview_width/(gdouble)itvals.numtiles;
      gdouble height = (gdouble)preview_height/(gdouble)itvals.numtiles;
      gint i;

      x = width*(exp_call.x - 1);
      y = height*(exp_call.y - 1);

      cairo_set_operator (cr, CAIRO_OPERATOR_DIFFERENCE);
      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
      cairo_set_line_width (cr, 1.0);

      for (i = 0; i < 3; i++)
	cairo_rectangle (cr,
			 (gint)x + i + 0.5,
			 (gint)y + i + 0.5,
			 (gint)width - 2 * i,
			 (gint)height - 2 * i);
      cairo_stroke (cr);
    }
}

static void
draw_explict_sel(void)
{
  if (tint.sel_area)
    gtk_widget_queue_draw (tint.sel_area);
}

static void
exp_need_update(gint nx, gint ny)
{
  gchar buf[256];

  if (nx <= 0 || nx > itvals.numtiles || ny <= 0 || ny > itvals.numtiles)
    return;

  if( nx != exp_call.x ||
       ny != exp_call.y )
    {
      exp_call.x = nx;
      exp_call.y = ny;
      draw_explict_sel();

      sprintf(buf,"%d",nx);
      g_signal_handlers_block_matched (exp_call.c_entry, G_SIGNAL_MATCH_DATA,
				       0, 0, NULL, NULL, &exp_call);
      gtk_editable_set_text(GTK_EDITABLE(exp_call.c_entry), buf);
      g_signal_handlers_unblock_matched (exp_call.c_entry, G_SIGNAL_MATCH_DATA,
					 0, 0, NULL, NULL, &exp_call);
      sprintf(buf,"%d",ny);
      g_signal_handlers_block_matched (exp_call.r_entry, G_SIGNAL_MATCH_DATA,
				       0, 0, NULL, NULL, &exp_call);
      gtk_editable_set_text(GTK_EDITABLE(exp_call.r_entry), buf);
      g_signal_handlers_unblock_matched (exp_call.r_entry, G_SIGNAL_MATCH_DATA,
					 0, 0, NULL, NULL, &exp_call);
    }
}

static void
tileit_preview_pick (gdouble x, gdouble y)
{
  gint nx,ny;
  gint twidth = preview_width/itvals.numtiles;
  gint theight = preview_height/itvals.numtiles;

  if (x < 0 || y < 0)
    return;

  nx = x/twidth + 1;
  ny = y/theight + 1;
  exp_need_update(nx,ny);
}

static void
tileit_preview_drag_begin (GtkGestureDrag *gesture,
			   gdouble         x,
			   gdouble         y,
			   gpointer        data)
{
  tileit_preview_pick (x, y);
}

static void
tileit_preview_drag_update (GtkGestureDrag *gesture,
			    gdouble         offset_x,
			    gdouble         offset_y,
			    gpointer        data)
{
  gdouble x, y;

  if (gtk_gesture_drag_get_start_point (gesture, &x, &y))
    tileit_preview_pick (x + offset_x, y + offset_y);
}

static void
tileit_hvtoggle_update(GtkWidget *widget,
                      gpointer   data)
{
  int *toggle_val;

  toggle_val = (int *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
  {
    /* Only do for event that sets a toggle button to true */
    /* This will break if any more toggles are added? */
    *toggle_val = TRUE;
  }
  else
    *toggle_val = FALSE;

  switch(exp_call.type)
    {
    case ALL:
      /* Clear current settings */
      memset(tileactions,0,sizeof(tileactions));
      all_update();
      break;
    case ALT:
      /* Clear current settings */
      memset(tileactions,0,sizeof(tileactions));
      alt_update();
      break;
    case EXPLICT:
      break;
    }
  dialog_update_preview();
}

static void 
explict_update(gint settile)
{
  int x,y;

  /* Make sure bounds are OK */
  y = atoi(gtk_editable_get_text(GTK_EDITABLE(exp_call.r_entry)));
  if(y > itvals.numtiles || y <= 0)
    {
      y = itvals.numtiles;
    }
  x = atoi(gtk_editable_get_text(GTK_EDITABLE(exp_call.c_entry)));
  if(x > itvals.numtiles || x <= 0)
    {
      x = itvals.numtiles;
    }

  /* Set it */
  if(settile == TRUE)
    tileactions[x-1][y-1] = (((do_horz)?HORIZONTAL:0)|((do_vert)?VERTICAL:0));

  exp_call.x = x;
  exp_call.y = y;

}

static void 
all_update(void)
{
  int x,y;
  for(x = 0 ; x < MAX_SEGS; x++)
    for(y = 0 ; y < MAX_SEGS; y++)
      tileactions[x][y] |= (((do_horz)?HORIZONTAL:0)|((do_vert)?VERTICAL:0));
}

static void
alt_update(void)
{
  int x,y;
  for(x = 0 ; x < MAX_SEGS; x++)
    for(y = 0 ; y < MAX_SEGS; y++)
      if(!((x+y)%2))
	tileactions[x][y] |= 
	  (((do_horz)?HORIZONTAL:0)|((do_vert)?VERTICAL:0));
}

static void
tileit_toggle_update(GtkWidget *widget,
                      gpointer   data)
{
  AppliedTo type = (AppliedTo) GPOINTER_TO_INT (data);
  
  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    {
      switch(type)
	{
	case ALL:
	  /* Clear current settings */
	  memset(tileactions,0,sizeof(tileactions));
	  all_update();
	  break;
	case ALT:
	  /* Clear current settings */
	  memset(tileactions,0,sizeof(tileactions));
	  alt_update();
	  break;
	case EXPLICT:
	  /* Make widget active */
	  gtk_widget_set_sensitive(exp_call.r_label,TRUE);
	  gtk_widget_set_sensitive(exp_call.r_entry,TRUE);
	  gtk_widget_set_sensitive(exp_call.c_label,TRUE);
	  gtk_widget_set_sensitive(exp_call.c_entry,TRUE);
	  gtk_widget_set_sensitive(exp_call.applybut,TRUE);
	  explict_update(FALSE);
	  break;
	}
      exp_call.type = type;
    }
  else
    {
      switch(type)
	{
	case ALL:
	  break;
	case ALT:
	  break;
	case EXPLICT:
	  gtk_widget_set_sensitive(exp_call.r_label,FALSE);
	  gtk_widget_set_sensitive(exp_call.r_entry,FALSE);
	  gtk_widget_set_sensitive(exp_call.c_label,FALSE);
	  gtk_widget_set_sensitive(exp_call.c_entry,FALSE);
	  gtk_widget_set_sensitive(exp_call.applybut,FALSE);
	  break;
	}
    }

  dialog_update_preview();
}                  


static void
tileit_scale_update(GtkAdjustment *adjustment, gint *value)
{
  GtkWidget *entry;
  char       buf[256];
  
  if (*value != gtk_adjustment_get_value (adjustment)) {
    *value = gtk_adjustment_get_value (adjustment);

    entry = g_object_get_data(G_OBJECT(adjustment), "user_data");
    sprintf(buf,"%d",*value);

    g_signal_handlers_block_matched (entry, G_SIGNAL_MATCH_DATA,
				     0, 0, NULL, NULL, value);
    gtk_editable_set_text(GTK_EDITABLE(entry), buf);
    g_signal_handlers_unblock_matched (entry, G_SIGNAL_MATCH_DATA,
				       0, 0, NULL, NULL, value);
    
    dialog_update_preview();
  }
} 


static void
tileit_reset(GtkWidget *widget, gpointer data)
{
  Reset_Call *r = (Reset_Call *)data;

  memset(tileactions,0,sizeof(tileactions));

  g_signal_handlers_block_matched (r->htoggle, G_SIGNAL_MATCH_DATA,
				   0, 0, NULL, NULL, &do_horz);
  g_signal_handlers_block_matched (r->vtoggle, G_SIGNAL_MATCH_DATA,
				   0, 0, NULL, NULL, &do_vert);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(r->htoggle),FALSE);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(r->vtoggle),FALSE);
  g_signal_handlers_unblock_matched (r->htoggle, G_SIGNAL_MATCH_DATA,
				     0, 0, NULL, NULL, &do_horz);
  g_signal_handlers_unblock_matched (r->vtoggle, G_SIGNAL_MATCH_DATA,
				     0, 0, NULL, NULL, &do_vert);
  do_horz = do_vert = FALSE; 

  dialog_update_preview();
} 


/* Could avoid almost dup. functions by using a field in the data 
 * passed.  Must still pass the data since used in sig blocking func.
 */

static void
tileit_exp_update(GtkWidget *widget, gpointer applied)
{
  explict_update(TRUE);
  dialog_update_preview();
} 


static void
tileit_exp_update_f(GtkWidget *widget, gpointer applied)
{
  explict_update(FALSE);
  dialog_update_preview();
} 

static void
tileit_entry_update(GtkWidget *widget, gint *value)
{
  GtkAdjustment *adjustment;
  gdouble        new_value;
  
  new_value = atoi(gtk_editable_get_text(GTK_EDITABLE(widget)));
  
  if (*value != new_value) {
    adjustment = g_object_get_data(G_OBJECT(widget), "user_data");

    if ((new_value >= gtk_adjustment_get_lower (adjustment)) &&
	(new_value <= gtk_adjustment_get_upper (adjustment))) {
      *value            = new_value;
      gtk_adjustment_set_value (adjustment, new_value);
      
      dialog_update_preview();
    } 
  } 
} 


/* Cache the preview image - updates are a lot faster. */
/* The preview_cache will contain the small image */

static void
cache_preview()
{
  GPixelRgn src_rgn;
  int y,x;
  guchar *src_rows;
  guchar *p;
  int isgrey = 0;

  gimp_pixel_rgn_init(&src_rgn,tileitdrawable,sel_x1,sel_y1,sel_width,sel_height,FALSE,FALSE);

  src_rows = g_new(guchar ,sel_width*4); 
  p = tint.pv_cache = g_new(guchar ,preview_width*preview_height*4);

  img_width  = gimp_drawable_width(tileitdrawable->id);
  img_height = gimp_drawable_height(tileitdrawable->id);

  tint.img_bpp = gimp_drawable_bpp(tileitdrawable->id);   

  has_alpha = gimp_drawable_has_alpha(tileitdrawable->id);

  if(tint.img_bpp < 3)
    {
      tint.img_bpp = 3 + has_alpha;
    }

  switch ( gimp_drawable_type (tileitdrawable->id) )
    {
    case GRAYA_IMAGE:
    case GRAY_IMAGE:
      isgrey = 1;
      break;
    default:
      isgrey = 0;
      break;
    }

  for (y = 0; y < preview_height; y++) {
  
    gimp_pixel_rgn_get_row(&src_rgn,
			   src_rows,
			   sel_x1,
			   sel_y1 + (y*sel_height)/preview_height,
			   sel_width);
      
    for (x = 0; x < (preview_width); x ++) {
      /* Get the pixels of each col */
      int i;
      for (i = 0 ; i < 3; i++ )
	p[x*tint.img_bpp+i] = src_rows[((x*sel_width)/preview_width)*src_rgn.bpp +((isgrey)?0:i)]; 
      if(has_alpha)
	p[x*tint.img_bpp+3] = src_rows[((x*sel_width)/preview_width)*src_rgn.bpp + ((isgrey)?1:3)];
    }
    p += (preview_width*tint.img_bpp);
  }
  g_free(src_rows);
}


static void
tileit_get_pixel(int x, int y, guchar *pixel)
{
  static gint row  = -1;
  static gint col  = -1;
  
  gint    newcol, newrow;
  gint    newcoloff, newrowoff;
  guchar *p;
  int     i;
  
  if ((x < 0) || (x >= img_width) || (y < 0) || (y >= img_height)) {
    pixel[0] = 0;
    pixel[1] = 0;
    pixel[2] = 0;
    pixel[3] = 0;
    
    return;
  }
  
  newcol    = x / tile_width;
  newcoloff = x % tile_width;
  newrow    = y / tile_height;
  newrowoff = y % tile_height;
  
  if ((col != newcol) || (row != newrow) || (the_tile == NULL)) {
    if (the_tile != NULL)
      gimp_tile_unref(the_tile, FALSE);
    
    the_tile = gimp_drawable_get_tile(tileitdrawable, FALSE, newrow, newcol);
    gimp_tile_ref(the_tile);
    
    col = newcol;
    row = newrow;
  } 
  
  p = the_tile->data + the_tile->bpp * (the_tile->ewidth * newrowoff + newcoloff);
  
  for (i = img_bpp; i; i--)
    *pixel++ = *p++;
}


static void
do_tiles(void)
{
  GPixelRgn dest_rgn;
  gpointer  pr;
  gint      progress, max_progress;
  guchar   *dest_row;
  guchar   *dest;
  gint      row, col;
  guchar    pixel[4];
  int 	    nc,nr;
  int       i;
  
  /* Initialize pixel region */
  
  gimp_pixel_rgn_init(&dest_rgn, tileitdrawable, sel_x1, sel_y1, sel_width, sel_height, TRUE, TRUE);
  
  progress     = 0;
  max_progress = sel_width * sel_height;
  
  img_bpp = gimp_drawable_bpp(tileitdrawable->id);
  
  for (pr = gimp_pixel_rgns_register(1, &dest_rgn);
       pr != NULL; pr = gimp_pixel_rgns_process(pr)) {
    dest_row = dest_rgn.data;
    
    for (row = dest_rgn.y; row < (dest_rgn.y + dest_rgn.h); row++) {
      dest = dest_row;
      
      for (col = dest_rgn.x; col < (dest_rgn.x + dest_rgn.w); col++)
	{
	  int an_action;
	  
	  an_action = 
	    tiles_xy(sel_width,
		     sel_height,
		     col-sel_x1,row-sel_y1,
		     &nc,&nr);
	  tileit_get_pixel(nc+sel_x1,nr+sel_y1,pixel);
	  for (i = 0; i < img_bpp; i++)
	    *dest++ = pixel[i];
	  
	  if(an_action && has_alpha)
	    {
	      dest--;
	      *dest = ((*dest)*opacity)/100;
	      dest++;
	    }
	}
      dest_row += dest_rgn.rowstride;
    } 
    
    progress += dest_rgn.w * dest_rgn.h;
    gimp_progress_update((double) progress / max_progress);
  }
  
  if (the_tile != NULL) {
    gimp_tile_unref(the_tile, FALSE);
    the_tile = NULL;
  }
  
  gimp_drawable_flush(tileitdrawable);
  gimp_drawable_merge_shadow(tileitdrawable->id, TRUE);
  gimp_drawable_update(tileitdrawable->id, sel_x1, sel_y1, sel_width, sel_height);
} 


/* Get the xy pos and any action */
static gint
tiles_xy(gint width,
	 gint height,
	 gint x,
	 gint y,
	 gint *nx,
	 gint *ny)
{
  gint px,py;
  gint rnum,cnum; 
  gint actiontype;
  gdouble rnd = 1 - (1.0/(gdouble)itvals.numtiles) +0.01;

  rnum = y*itvals.numtiles/height;

  py = (y*itvals.numtiles)%height;
  px = (x*itvals.numtiles)%width; 
  cnum = x*itvals.numtiles/width;
      
  if((actiontype = tileactions[cnum][rnum]))
    {
      if(actiontype & HORIZONTAL)
	{
	  gdouble pyr;
	  pyr =  height - y - 1 + rnd;
	  py = ((int)(pyr*(gdouble)itvals.numtiles))%height;
	}
      
      if(actiontype & VERTICAL)
	{
	  gdouble pxr;
	  pxr = width - x - 1 + rnd;
	  px = ((int)(pxr*(gdouble)itvals.numtiles))%width; 
	}
    }
  
  *nx = px;
  *ny = py;

  return(actiontype);
}


/* Given a row then srink it down a bit */
static void
do_tiles_preview(guchar *dest_row, 
	    guchar *src_rows,
	    gint width,
	    gint dh,
	    gint height,
	    gint bpp)
{
  gint x;
  gint i;
  gint px,py;
  gint rnum,cnum; 
  gint actiontype;
  gdouble rnd = 1 - (1.0/(gdouble)itvals.numtiles) +0.01;

  rnum = dh*itvals.numtiles/height;

  for (x = 0; x < width; x ++) 
    {
      
      py = (dh*itvals.numtiles)%height;
      
      px = (x*itvals.numtiles)%width; 
      cnum = x*itvals.numtiles/width;
      
      if((actiontype = tileactions[cnum][rnum]))
	{
	  if(actiontype & HORIZONTAL)
	    {
	      gdouble pyr;
	      pyr =  height - dh - 1 + rnd;
	      py = ((int)(pyr*(gdouble)itvals.numtiles))%height;
	    }
	  
	  if(actiontype & VERTICAL)
	    {
	      gdouble pxr;
	      pxr = width - x - 1 + rnd;
	      px = ((int)(pxr*(gdouble)itvals.numtiles))%width; 
	    }
	}

      for (i = 0 ; i < bpp; i++ )
	dest_row[x*tint.img_bpp+i] = 
	  src_rows[(px + (py*width))*bpp+i]; 

      if(has_alpha && actiontype)
	dest_row[x*tint.img_bpp + (bpp - 1)] = 
	  (dest_row[x*tint.img_bpp + (bpp - 1)]*opacity)/100;

    }
}

static void
dialog_update_preview(void)
{

  int     y;
  gint check,check_0,check_1;  

  for (y = 0; y < preview_height; y++) {
    
    if ((y / CHECK_SIZE) & 1) {
      check_0 = CHECK_DARK;
      check_1 = CHECK_LIGHT;
    } else {
      check_0 = CHECK_LIGHT;
      check_1 = CHECK_DARK;
    }

    do_tiles_preview(tint.preview_row,
		tint.pv_cache,
		preview_width,
		y,
		preview_height,
		tint.img_bpp);

    if(tint.img_bpp > 3)
      {
	int i,j;
	for (i = 0, j = 0 ; i < sizeof(tint.preview_row); i += 4, j += 3 )
	  {
	    gint alphaval;
	    if (((i/4) / CHECK_SIZE) & 1)
	      check = check_0;
	    else
	      check = check_1;
	    
	    alphaval = tint.preview_row[i + 3];
	    
	    tint.preview_row[j] = 
	      check + (((tint.preview_row[i] - check)*alphaval)/255);
	    tint.preview_row[j + 1] = 
	      check + (((tint.preview_row[i + 1] - check)*alphaval)/255);
	    tint.preview_row[j + 2] = 
	      check + (((tint.preview_row[i + 2] - check)*alphaval)/255);
	  }
      }
    
    gimp_preview_draw_row(GIMP_PREVIEW(tint.preview), tint.preview_row, 0, y, preview_width);
  }

  draw_explict_sel();
}


