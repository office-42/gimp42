/*
 * This is a plugin for the GIMP.
 *
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * Copyright (C) 1996 Torsten Martinsen
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
 * $Id$
 */

/*
 * This filter adds random noise to an image.
 * The amount of noise can be set individually for each RGB channel.
 * This filter does not operate on indexed images.
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#define ENTRY_WIDTH  60
#define SCALE_WIDTH 125
#define TILE_CACHE_SIZE 16

typedef struct
{
  gint    independent;
  gdouble noise[4];     /*  per channel  */
} NoisifyVals;

typedef struct
{
  gint       run;
} NoisifyInterface;

/* Declare local functions.
 */
static void      query  (void);
static void      run    (char      *name,
			 int        nparams,
			 GParam    *param,
			 int       *nreturn_vals,
			 GParam   **return_vals);

static void      noisify        (GDrawable * drawable);
static gint      noisify_dialog (gint        channels);
static gdouble   gauss          (void);

static void      noisify_close_callback  (GtkWidget *widget,
					  gpointer   data);
static void      noisify_ok_callback     (GtkWidget *widget,
					  gpointer   data);
static void      noisify_toggle_update   (GtkWidget *widget,
					  gpointer   data);
static void      noisify_scale_update    (GtkAdjustment *adjustment,
					  double        *scale_val);
static void      noisify_entry_update    (GtkWidget *widget,
					  gdouble *value);
static void      dialog_create_value     (char *title,
					  GtkWidget *table,
					  int row,
					  gdouble *value,
					  double left,
					  double right);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

static NoisifyVals nvals =
{
  TRUE,
  { 0.20, 0.20, 0.20, 0.20 }
};

static NoisifyInterface noise_int =
{
  FALSE     /* run */
};


MAIN ()

static void
query ()
{
  static GParamDef args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image (unused)" },
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
    { PARAM_INT32, "independent", "Noise in channels independent" },
    { PARAM_FLOAT, "noise_1", "Noise in the first channel (red, gray)" },
    { PARAM_FLOAT, "noise_2", "Noise in the second channel (green, gray_alpha)" },
    { PARAM_FLOAT, "noise_3", "Noise in the third channel (blue)" },
    { PARAM_FLOAT, "noise_4", "Noise in the fourth channel (alpha)" }
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0;

  gimp_install_procedure ("plug_in_noisify",
			  "Adds random noise to a drawable's channels",
			  "More here later",
			  "Torsten Martinsen",
			  "Torsten Martinsen",
			  "1996",
			  "<Image>/Filters/Noise/Noisify",
			  "RGB*, GRAY*",
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
  static GParam values[1];
  GDrawable *drawable;
  GRunModeType run_mode;
  GStatusType status = STATUS_SUCCESS;

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;

  /*  Get the specified drawable  */
  drawable = gimp_drawable_get (param[2].data.d_drawable);

  switch (run_mode)
    {
    case RUN_INTERACTIVE:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_noisify", &nvals);

      /*  First acquire information with a dialog  */
      if (! noisify_dialog (drawable->bpp))
	{
	  gimp_drawable_detach (drawable);
	  return;
	}
      break;

    case RUN_NONINTERACTIVE:
      /*  Make sure all the arguments are there!  */
      if (nparams != 8)
	status = STATUS_CALLING_ERROR;
      if (status == STATUS_SUCCESS)
	{
	  nvals.independent = (param[3].data.d_int32) ? TRUE : FALSE;
	  nvals.noise[0] = param[4].data.d_float;
	  nvals.noise[1] = param[5].data.d_float;
	  nvals.noise[2] = param[6].data.d_float;
	  nvals.noise[3] = param[7].data.d_float;
	}
      break;

    case RUN_WITH_LAST_VALS:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_noisify", &nvals);
      break;

    default:
      break;
    }

  /*  Make sure that the drawable is gray or RGB color  */
  if (gimp_drawable_color (drawable->id) || gimp_drawable_gray (drawable->id))
    {
      gimp_progress_init ("Adding Noise...");
      gimp_tile_cache_ntiles (TILE_CACHE_SIZE);

      /*  seed the random number generator  */
      srand (time (NULL));

      /*  compute the luminosity which exceeds the luminosity threshold  */
      noisify (drawable);

      if (run_mode != RUN_NONINTERACTIVE)
	gimp_displays_flush ();

      /*  Store data  */
      if (run_mode == RUN_INTERACTIVE)
	gimp_set_data ("plug_in_noisify", &nvals, sizeof (NoisifyVals));
    }
  else
    {
      /* gimp_message ("blur: cannot operate on indexed color images"); */
      status = STATUS_EXECUTION_ERROR;
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

static void
noisify (GDrawable *drawable)
{
  GPixelRgn src_rgn, dest_rgn;
  guchar *src_row, *dest_row;
  guchar *src, *dest;
  gint row, col, b;
  gint x1, y1, x2, y2, p;
  gint noise;
  gint progress, max_progress;
  gpointer pr;

  /* initialize */

  noise = 0;

  gimp_drawable_mask_bounds (drawable->id, &x1, &y1, &x2, &y2);
  gimp_pixel_rgn_init (&src_rgn, drawable, x1, y1, (x2 - x1), (y2 - y1), FALSE, FALSE);
  gimp_pixel_rgn_init (&dest_rgn, drawable, x1, y1, (x2 - x1), (y2 - y1), TRUE, TRUE);

  /* Initialize progress */
  progress = 0;
  max_progress = (x2 - x1) * (y2 - y1);

  for (pr = gimp_pixel_rgns_register (2, &src_rgn, &dest_rgn); pr != NULL; pr = gimp_pixel_rgns_process (pr))
    {
      src_row = src_rgn.data;
      dest_row = dest_rgn.data;

      for (row = 0; row < src_rgn.h; row++)
	{
	  src = src_row;
	  dest = dest_row;

	  for (col = 0; col < src_rgn.w; col++)
	    {
	      if (nvals.independent == FALSE)
		noise = (gint) (nvals.noise[0] * gauss() * 127);

	      for (b = 0; b < src_rgn.bpp; b++)
		{
		  if (nvals.independent == TRUE)
		    noise = (gint) (nvals.noise[b] * gauss() * 127);

		  
		  p = src[b] + noise;
		  if (p < 0)
		    p = 0;
		  else if (p > 255)
		    p = 255;
		  if (nvals.noise[b] != 0)
		    dest[b] = p;
		  
		}
	      src += src_rgn.bpp;
	      dest += dest_rgn.bpp;
	    }

	  src_row += src_rgn.rowstride;
	  dest_row += dest_rgn.rowstride;
	}

      /* Update progress */
      progress += src_rgn.w * src_rgn.h;
      gimp_progress_update ((double) progress / (double) max_progress);
    }

  /*  update the blurred region  */
  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->id, TRUE);
  gimp_drawable_update (drawable->id, x1, y1, (x2 - x1), (y2 - y1));
}

static gint
noisify_dialog (gint channels)
{
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *toggle;
  GtkWidget *frame;
  GtkWidget *table;
  gchar buffer[32];
  int i;

  gtk_init ();

  dlg = gimp_dialog_new ("Noisify");
  g_signal_connect (dlg, "destroy",
		    G_CALLBACK (noisify_close_callback), NULL);

  /*  Action area  */
  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (noisify_ok_callback),
			  dlg, TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  /*  parameter settings  */
  frame = gtk_frame_new ("Parameter Settings");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);
  table = gimp_table_new (channels + 1, 3, FALSE);
  gimp_container_set_border_width (table, 10);
  gtk_frame_set_child (GTK_FRAME (frame), table);

  toggle = gtk_check_button_new_with_label ("Independent");
  gimp_table_attach (table, toggle, 0, 2, 0, 1, GIMP_FILL, 0, 0, 0);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), nvals.independent);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (noisify_toggle_update),
		    &nvals.independent);

  /*  for (i = 0; i < channels; i++)
   {
      sprintf (buffer, "Channel #%d", i);
      dialog_create_value(buffer, table, i+1, &nvals.noise[i], 0.0, 1.0);
    }
    */
  
  if (channels == 1) 
    {
      sprintf (buffer, "Gray");
      dialog_create_value(buffer, table, 1, &nvals.noise[0], 0.0, 1.0);
    }

  else if (channels == 2)
    {
      sprintf (buffer, "Gray");
      dialog_create_value(buffer, table, 1, &nvals.noise[0], 0.0, 1.0);
      sprintf (buffer, "Alpha");
      dialog_create_value(buffer, table, 2, &nvals.noise[1], 0.0, 1.0);
    }
  
  else if (channels == 3)
    {
      sprintf (buffer, "Red");
      dialog_create_value(buffer, table, 1, &nvals.noise[0], 0.0, 1.0);
      sprintf (buffer, "Green");
      dialog_create_value(buffer, table, 2, &nvals.noise[1], 0.0, 1.0);
      sprintf (buffer, "Blue");
      dialog_create_value(buffer, table, 3, &nvals.noise[2], 0.0, 1.0);
    }

  else if (channels == 4)
    {
      sprintf (buffer, "Red");
      dialog_create_value(buffer, table, 1, &nvals.noise[0], 0.0, 1.0);
      sprintf (buffer, "Green");
      dialog_create_value(buffer, table, 2, &nvals.noise[1], 0.0, 1.0);
      sprintf (buffer, "Blue");
      dialog_create_value(buffer, table, 3, &nvals.noise[2], 0.0, 1.0);
      sprintf (buffer, "Alpha");
      dialog_create_value(buffer, table, 4, &nvals.noise[3], 0.0, 1.0);
    }
  
  else
    {
      for (i = 0; i < channels; i++)
	{
	  sprintf (buffer, "Channel #%d", i);
	  dialog_create_value(buffer, table, i+1, &nvals.noise[i], 0.0, 1.0);
	}
    }
  

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return noise_int.run;
}

/*
 * Return a Gaussian (aka normal) random variable.
 *
 * Adapted from ppmforge.c, which is part of PBMPLUS.
 * The algorithm comes from:
 * 'The Science Of Fractal Images'. Peitgen, H.-O., and Saupe, D. eds.
 * Springer Verlag, New York, 1988.
 */
static gdouble
gauss ()
{
  gint i;
  gdouble sum = 0.0;

  for (i = 0; i < 4; i++)
    sum += rand () & 0x7FFF;

  return sum * 5.28596089837e-5 - 3.46410161514;
}


/*  Noisify interface functions  */

static void
noisify_close_callback (GtkWidget *widget,
			gpointer   data)
{
  gimp_main_loop_quit ();
}

static void
noisify_ok_callback (GtkWidget *widget,
		     gpointer   data)
{
  noise_int.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void
noisify_toggle_update (GtkWidget *widget,
		       gpointer   data)
{
  int *toggle_val;

  toggle_val = (int *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    *toggle_val = TRUE;
  else
    *toggle_val = FALSE;
}


/*
 * Thanks to Quartic for these.
 */
static void
dialog_create_value(char *title, GtkWidget *table, int row, gdouble *value, double left, double right)
{
	GtkWidget *label;
	GtkWidget *scale;
	GtkWidget *entry;
	GtkAdjustment *scale_data;
	char       buf[256];

	label = gtk_label_new(title);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gimp_table_attach(table, label, 0, 1, row, row + 1, GIMP_FILL, GIMP_FILL, 4, 0);

	scale_data = gtk_adjustment_new(*value, left, right,
					(right - left) / 200.0,
					(right - left) / 200.0,
					0.0);

	g_signal_connect(scale_data, "value-changed",
			 G_CALLBACK (noisify_scale_update),
			 value);

	scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, scale_data);
	gtk_widget_set_size_request(scale, SCALE_WIDTH, -1);
	gimp_table_attach(table, scale, 1, 2, row, row + 1, GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 0, 0);
	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
	gtk_scale_set_digits(GTK_SCALE(scale), 3);

	entry = gtk_entry_new();
	g_object_set_data(G_OBJECT(entry), "user_data", scale_data);
	g_object_set_data(G_OBJECT(scale_data), "user_data", entry);
	gtk_widget_set_size_request(entry, ENTRY_WIDTH, -1);
	gtk_editable_set_width_chars(GTK_EDITABLE(entry), 6);
	sprintf(buf, "%0.2f", *value);
	gtk_editable_set_text(GTK_EDITABLE(entry), buf);
	g_signal_connect(entry, "changed",
			 G_CALLBACK (noisify_entry_update),
			 value);
	gimp_table_attach(table, entry, 2, 3, row, row + 1, GIMP_FILL, GIMP_FILL, 4, 0);
}

static void
noisify_entry_update(GtkWidget *widget, gdouble *value)
{
	GtkAdjustment *adjustment;
	gdouble        new_value;

	new_value = atof(gtk_editable_get_text(GTK_EDITABLE(widget)));

	if (*value != new_value) {
		adjustment = g_object_get_data(G_OBJECT(widget), "user_data");

		if ((new_value >= gtk_adjustment_get_lower(adjustment)) &&
		    (new_value <= gtk_adjustment_get_upper(adjustment))) {
			*value = new_value;
			gtk_adjustment_set_value(adjustment, new_value);
		} /* if */
	} /* if */
}

static void
noisify_scale_update (GtkAdjustment *adjustment, gdouble *value)
{
	GtkWidget *entry;
	char       buf[256];

	if (*value != gtk_adjustment_get_value(adjustment)) {
		*value = gtk_adjustment_get_value(adjustment);

		entry = g_object_get_data(G_OBJECT(adjustment), "user_data");
		sprintf(buf, "%0.2f", *value);

		g_signal_handlers_block_by_func(entry, noisify_entry_update, value);
		gtk_editable_set_text(GTK_EDITABLE(entry), buf);
		g_signal_handlers_unblock_by_func(entry, noisify_entry_update, value);
	} /* if */
}
