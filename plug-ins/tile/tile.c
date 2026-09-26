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

/*
 * This filter tiles an image to arbitrary width and height
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#define ENTRY_WIDTH     60
#define ENTRY_HEIGHT    25

typedef struct {
  gint new_width;
  gint new_height;
  gint constrain;
  gint new_image;
} TileVals;

typedef struct {
  GtkWidget *width_entry;
  GtkWidget *height_entry;
  gint orig_width;
  gint orig_height;
  gint new_image;
  gint run;
} TileInterface;

/* Declare a local function.
 */
static void      query  (void);
static void      run    (char      *name,
			 int        nparams,
			 GParam    *param,
			 int       *nreturn_vals,
			 GParam   **return_vals);
static gint32    tile   (gint32     image_id,
			 gint32     drawable_id,
			 gint32    *layer_id);

static gint      tile_dialog          (gint       width,
				       gint       height);

static void      tile_close_callback  (GtkWidget *widget,
				       gpointer   data);
static void      tile_ok_callback     (GtkWidget *widget,
				       gpointer   data);
static void      tile_toggle_update   (GtkWidget *widget,
				       gpointer   data);
static void      tile_entry_update    (GtkWidget *widget,
				       gpointer   data);



GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

static TileVals tvals =
{
  1,        /*  new_width  */
  1,        /*  new_height  */
  TRUE,     /*  constrain  */
  TRUE      /*  new_image  */
};

static TileInterface tint =
{
  NULL,     /*  width_entry  */
  NULL,     /*  height_entry  */
  0,        /*  orig_width  */
  0,        /*  orig_height  */
  TRUE,     /*  new_image  */
  FALSE     /*  run  */
};

MAIN ()

static void
query (void)
{
  static GParamDef args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image (unused)" },
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
    { PARAM_INT32, "new_width", "New (tiled) image width" },
    { PARAM_INT32, "new_height", "New (tiled) image height" },
    { PARAM_INT32, "new_image", "Create a new image?" },
  };
  static GParamDef return_vals[] =
  {
    { PARAM_IMAGE, "new_image", "Output image (N/A if new_image == TRUE)" },
    { PARAM_IMAGE, "new_layer", "Output layer (N/A if new_image == TRUE)" },
  };
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = sizeof (return_vals) / sizeof (return_vals[0]);

  gimp_install_procedure ("plug_in_tile",
			  "Create a new image which is a tiled version of the input drawable",
			  "This function creates a new image with a single layer sized to the specified 'new_width' and 'new_height' parameters.  The specified drawable is tiled into this layer.  The new layer will have the same type as the specified drawable and the new image will have a corresponding base type",
			  "Spencer Kimball & Peter Mattis",
			  "Spencer Kimball & Peter Mattis",
			  "1996-1997",
			  "<Image>/Filters/Map/Tile",
			  "RGB*, GRAY*, INDEXED*",
			  PROC_PLUG_IN,
			  nargs, nreturn_vals,
			  args, return_vals);
}

static void
run (char    *name,
     int      nparams,
     GParam  *param,
     int     *nreturn_vals,
     GParam **return_vals)
{
  static GParam values[3];
  GRunModeType run_mode;
  GStatusType status = STATUS_SUCCESS;
  gint32 new_layer;
  gint width, height;

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 3;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
  values[1].type = PARAM_IMAGE;
  values[1].type = PARAM_LAYER;

  width = gimp_drawable_width (param[2].data.d_drawable);
  height = gimp_drawable_height (param[2].data.d_drawable);

  switch (run_mode)
    {
    case RUN_INTERACTIVE:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_tile", &tvals);

      /*  First acquire information with a dialog  */
      if (! tile_dialog (width, height))
	return;
      break;

    case RUN_NONINTERACTIVE:
      /*  Make sure all the arguments are there!  */
      if (nparams != 6)
	status = STATUS_CALLING_ERROR;
      if (status == STATUS_SUCCESS)
	{
	  tvals.new_width = param[3].data.d_int32;
	  tvals.new_height = param[4].data.d_int32;
	  tvals.new_image = (param[5].data.d_int32) ? TRUE : FALSE;
	}
      if (tvals.new_width < 0 || tvals.new_height < 0)
	status = STATUS_CALLING_ERROR;
      break;

    case RUN_WITH_LAST_VALS:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_tile", &tvals);
      break;

    default:
      break;
    }

  /*  Make sure that the drawable is gray or RGB color  */
  if (status == STATUS_SUCCESS)
    {
      gimp_progress_init ("Tiling...");
      gimp_tile_cache_ntiles (2 * (width + 1) / gimp_tile_width ());

      values[1].data.d_image = tile (param[1].data.d_image, param[2].data.d_drawable, &new_layer);
      values[2].data.d_layer = new_layer;

      /*  Store data  */
      if (run_mode == RUN_INTERACTIVE)
	gimp_set_data ("plug_in_tile", &tvals, sizeof (TileVals));

      if (run_mode != RUN_NONINTERACTIVE)
	{
	  if (tvals.new_image)
	    gimp_display_new (values[1].data.d_image);
	  else
	    gimp_displays_flush ();
	}
    }

  values[0].data.d_status = status;
}

static gint32
tile (gint32     image_id,
      gint32     drawable_id,
      gint32    *layer_id)
{
  GPixelRgn src_rgn, dest_rgn;
  GDrawable *drawable, *new_layer;
  GImageType image_type;
  gint32 new_image_id;
  gint old_width, old_height;
  gint width, height;
  gint i, j, k;
  gint progress, max_progress;
  gint nreturn_vals;
  gpointer pr;

  /* initialize */

  image_type = RGB;
  new_image_id = 0;

  old_width = gimp_drawable_width (drawable_id);
  old_height = gimp_drawable_height (drawable_id);

  if (tvals.new_image)
    {
      /*  create  a new image  */
      switch (gimp_drawable_type (drawable_id))
	{
	case RGB_IMAGE : case RGBA_IMAGE:
	  image_type = RGB;
	  break;
	case GRAY_IMAGE : case GRAYA_IMAGE:
	  image_type = GRAY;
	  break;
	case INDEXED_IMAGE : case INDEXEDA_IMAGE:
	  image_type = INDEXED;
	  break;
	}

      new_image_id = gimp_image_new (tvals.new_width, tvals.new_height, image_type);
      *layer_id = gimp_layer_new (new_image_id, "Background",
				  tvals.new_width, tvals.new_height,
				  gimp_drawable_type (drawable_id),
				  100, NORMAL_MODE);
      gimp_image_add_layer (new_image_id, *layer_id, 0);
      new_layer = gimp_drawable_get (*layer_id);

      /*  Get the specified drawable  */
      drawable = gimp_drawable_get (drawable_id);
    }
  else
    {
      gimp_run_procedure ("gimp_undo_push_group_start", &nreturn_vals,
			  PARAM_IMAGE, image_id,
			  PARAM_END);

      gimp_run_procedure ("gimp_image_resize", &nreturn_vals,
			  PARAM_IMAGE, image_id,
			  PARAM_INT32, tvals.new_width,
			  PARAM_INT32, tvals.new_height,
			  PARAM_INT32, 0,
			  PARAM_INT32, 0,
			  PARAM_END);

      if (gimp_drawable_layer (drawable_id))
	gimp_run_procedure ("gimp_layer_resize", &nreturn_vals,
			    PARAM_LAYER, drawable_id,
			    PARAM_INT32, tvals.new_width,
			    PARAM_INT32, tvals.new_height,
			    PARAM_INT32, 0,
			    PARAM_INT32, 0,
			    PARAM_END);

      /*  Get the specified drawable  */
      drawable = gimp_drawable_get (drawable_id);
      new_layer = drawable;
    }

  /*  progress  */
  progress = 0;
  max_progress = tvals.new_width * tvals.new_height;

  /*  tile...  */
  for (i = 0; i < tvals.new_height; i += old_height)
    {
      height = old_height;
      if (height + i > tvals.new_height)
	height = tvals.new_height - i;

      for (j = 0; j < tvals.new_width; j += old_width)
	{
	  width = old_width;
	  if (width + j > tvals.new_width)
	    width = tvals.new_width - j;

	  gimp_pixel_rgn_init (&src_rgn, drawable, 0, 0, width, height, FALSE, FALSE);
	  gimp_pixel_rgn_init (&dest_rgn, new_layer, j, i, width, height, TRUE, FALSE);

	  for (pr = gimp_pixel_rgns_register (2, &src_rgn, &dest_rgn); pr != NULL;
	       pr = gimp_pixel_rgns_process (pr))
	    {
	      for (k = 0; k < src_rgn.h; k++)
		memcpy (dest_rgn.data + k * dest_rgn.rowstride,
			src_rgn.data + k * src_rgn.rowstride,
			src_rgn.w * src_rgn.bpp);

	      progress += src_rgn.w * src_rgn.h;
	      gimp_progress_update ((double) progress / (double) max_progress);
	    }
	}
    }

  /*  copy the colormap, if necessary  */
  if (image_type == INDEXED && tvals.new_image)
    {
      int ncols;
      guchar *cmap;

      cmap = gimp_image_get_cmap (image_id, &ncols);
      gimp_image_set_cmap (new_image_id, cmap, ncols);
      g_free (cmap);
    }

  if (tvals.new_image)
    {
      gimp_drawable_flush (new_layer);
      gimp_drawable_detach (new_layer);
    }
  else
    gimp_run_procedure ("gimp_undo_push_group_end", &nreturn_vals,
			PARAM_IMAGE, image_id,
			PARAM_END);

  gimp_drawable_flush (drawable);
  gimp_drawable_detach (drawable);

  return new_image_id;
}

static gint
tile_dialog (gint width, gint height)
{
  GtkWidget *dlg;
  GtkWidget *label;
  GtkWidget *button;
  GtkWidget *toggle;
  GtkWidget *frame;
  GtkWidget *table;
  char buffer[12];


  gtk_init ();

  tint.orig_width = width;
  tint.orig_height = height;
  tvals.new_width = width;
  tvals.new_height = height;

  dlg = gimp_dialog_new ("Tile");
  g_signal_connect (dlg, "destroy",
		      G_CALLBACK (tile_close_callback),
		      NULL);

  /*  Action area  */
  button = gtk_button_new_with_label ("OK");
  g_signal_connect (button, "clicked",
                      G_CALLBACK (tile_ok_callback),
                      dlg);
  gimp_box_pack_start (gimp_dialog_get_action_area (dlg), button, TRUE, TRUE, 0);
  gtk_window_set_default_widget (GTK_WINDOW (dlg), button);

  button = gtk_button_new_with_label ("Cancel");
  g_signal_connect_swapped (button, "clicked", G_CALLBACK (gtk_window_destroy), dlg);
  gimp_box_pack_start (gimp_dialog_get_action_area (dlg), button, TRUE, TRUE, 0);

  /*  parameter settings  */
  frame = gtk_frame_new ("Tile to New Size:");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);

  table = gimp_table_new (4, 2, FALSE);
  gimp_container_set_border_width (table, 10);
  gimp_container_add (frame, table);
  gtk_grid_set_row_spacing (GTK_GRID (table), 5);
  gtk_grid_set_row_spacing (GTK_GRID (table), 5);
  gtk_grid_set_column_spacing (GTK_GRID (table), 5);

  label = gtk_label_new ("Width: ");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 0, 1, GIMP_FILL, GIMP_FILL, 0, 0);

  tint.width_entry = gtk_entry_new ();
  gtk_widget_set_size_request (tint.width_entry, ENTRY_WIDTH, ENTRY_HEIGHT);
  sprintf (buffer, "%d", width);
  gtk_editable_set_text (GTK_EDITABLE (tint.width_entry), buffer);
  gimp_table_attach (table, tint.width_entry,
		    1, 2, 0, 1, GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (tint.width_entry, "changed",
		      G_CALLBACK (tile_entry_update),
		      &tvals.new_width);

  label = gtk_label_new ("Height: ");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0);

  tint.height_entry = gtk_entry_new ();
  gtk_widget_set_size_request (tint.height_entry, ENTRY_WIDTH, ENTRY_HEIGHT);
  sprintf (buffer, "%d", height);
  gtk_editable_set_text (GTK_EDITABLE (tint.height_entry), buffer);
  gimp_table_attach (table, tint.height_entry,
		    1, 2, 1, 2, GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (tint.height_entry, "changed",
		      G_CALLBACK (tile_entry_update),
		      &tvals.new_height);

  toggle = gtk_check_button_new_with_label ("Constrain Ratio");
  gimp_table_attach (table, toggle,
		    0, 2, 2, 3, GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		      G_CALLBACK (tile_toggle_update),
		      &tvals.constrain);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), tvals.constrain);

  toggle = gtk_check_button_new_with_label ("New Image");
  gimp_table_attach (table, toggle,
		    0, 2, 3, 4, GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		      G_CALLBACK (tile_toggle_update),
		      &tvals.new_image);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), tvals.new_image);

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return tint.run;
}


/*  Tile interface functions  */

static void
tile_close_callback (GtkWidget *widget,
		     gpointer   data)
{
  gimp_main_loop_quit ();
}

static void
tile_ok_callback (GtkWidget *widget,
		  gpointer   data)
{
  tint.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void
tile_toggle_update (GtkWidget *widget,
		    gpointer   data)
{
  gint *toggle_val;

  toggle_val = (int *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    *toggle_val = TRUE;
  else
    *toggle_val = FALSE;
}


static void
tile_entry_update (GtkWidget *widget,
		   gpointer   data)
{
  static gchar buf[32];
  gint val;

  val = atoi (gtk_editable_get_text (GTK_EDITABLE (widget)));

  if (tvals.constrain)
    {
      if ((tint.orig_width != 0) && (tint.orig_height != 0))
	{
	  if (widget == tint.width_entry && tvals.new_width != val)
	    {
	      tvals.new_width = val;

	      tvals.new_height = (int) ((tvals.new_width * tint.orig_height) / tint.orig_width);
	      sprintf (buf, "%d", tvals.new_height);
	      gtk_editable_set_text (GTK_EDITABLE (tint.height_entry), buf);
	    }
	  else if (widget == tint.height_entry && tvals.new_height != val)
	    {
	      tvals.new_height = val;

	      tvals.new_width = (int) ((tvals.new_height * tint.orig_width) / tint.orig_height);
	      sprintf (buf, "%d", tvals.new_width);
	      gtk_editable_set_text (GTK_EDITABLE (tint.width_entry), buf);
	    }
	}
    }
  else
    *((int *) data) = val;
}
