/* The GIMP -- an image manipulation program * Copyright (C) 1995 Spencer
 * Kimball and Peter Mattis * * This program is free software; you can
 * redistribute it and/or modify * it under the terms of the GNU General
 * Public License as published by * the Free Software Foundation; either
 * version 2 of the License, or * (at your option) any later version. * *
 * This program is distributed in the hope that it will be useful, * but
 * WITHOUT ANY WARRANTY; without even the implied warranty of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the * GNU
 * General Public License for more details. * * You should have received a
 * copy of the GNU General Public License * along with this program; if not,
 * write to the Free Software * Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA. */
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

/* Declare local functions. */
static void query (void);
static void run (char *name,
		 int nparams,
		 GParam * param,
		 int *nreturn_vals,
		 GParam ** return_vals);
static gint dialog (void);

static void doit (GDrawable * drawable);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,				/* init_proc */
  NULL,				/* quit_proc */
  query,			/* query_proc */
  run,				/* run_proc */
};

gint bytes;
gint sx1, sy1, sx2, sy2;
int run_flag = 0;

typedef struct
  {
    gint width, height;
    gint x_offset, y_offset;
  }
config;

config my_config =
{
  16, 16,			/* width, height */
  0, 0,				/* x_offset, y_offset */
};


MAIN ()

static void
query (void)
{
  static GParamDef args[] =
  {
    {PARAM_INT32, "run_mode", "Interactive, non-interactive"},
    {PARAM_IMAGE, "image", "Input image (unused)"},
    {PARAM_DRAWABLE, "drawable", "Input drawable"},
    {PARAM_INT32, "width", "Width"},
    {PARAM_INT32, "height", "Height"},
    {PARAM_INT32, "x_offset", "X Offset"},
    {PARAM_INT32, "y_offset", "Y Offset"},
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0;

  gimp_install_procedure ("plug_in_grid",
			  "Draws a grid.",
			  "",
			  "Tim Newsome",
			  "Tim Newsome",
			  "1997",
			  "<Image>/Filters/Render/Grid",
			  "RGB*, GRAY*",
			  PROC_PLUG_IN,
			  nargs, nreturn_vals,
			  args, return_vals);
}

static void
run (char *name, int n_params, GParam * param, int *nreturn_vals,
     GParam ** return_vals)
{
  static GParam values[1];
  GDrawable *drawable;
  GRunModeType run_mode;
  GStatusType status = STATUS_SUCCESS;

  *nreturn_vals = 1;
  *return_vals = values;

  run_mode = param[0].data.d_int32;

  if (run_mode == RUN_NONINTERACTIVE)
    {
      if (n_params != 7)
	{
	  status = STATUS_CALLING_ERROR;
	}
      if( status == STATUS_SUCCESS)
	{
	  my_config.width = param[3].data.d_int32;
	  my_config.height = param[4].data.d_int32;
	  my_config.x_offset = param[5].data.d_int32;
	  my_config.y_offset = param[6].data.d_int32;
	}
    }
  else
    {
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_grid", &my_config);

      if (run_mode == RUN_INTERACTIVE)
	{
	  /* Oh boy. We get to do a dialog box, because we can't really expect the
	   * user to set us up with the right values using gdb.
	   */
	  if (!dialog ())
	    {
	      /* The dialog was closed, or something similarly evil happened. */
	      status = STATUS_EXECUTION_ERROR;
	    }
	}
    }

  if (my_config.width <= 0 || my_config.height <= 0)
    {
      status = STATUS_EXECUTION_ERROR;
    }

  if (status == STATUS_SUCCESS)
    {
      /*  Get the specified drawable  */
      drawable = gimp_drawable_get (param[2].data.d_drawable);

      /*  Make sure that the drawable is gray or RGB color  */
      if (gimp_drawable_color (drawable->id) || gimp_drawable_gray (drawable->id))
	{
	  gimp_progress_init ("Drawing Grid...");
	  gimp_tile_cache_ntiles (2 * (drawable->width / gimp_tile_width () + 1));

	  srand (time (NULL));
	  doit (drawable);

	  if (run_mode != RUN_NONINTERACTIVE)
	    gimp_displays_flush ();

	  if (run_mode == RUN_INTERACTIVE)
	    gimp_set_data ("plug_in_grid", &my_config, sizeof (my_config));
	}
      else
	{
	  status = STATUS_EXECUTION_ERROR;
	}
      gimp_drawable_detach (drawable);
    }

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
}

static void
doit (GDrawable * drawable)
{
  GPixelRgn srcPR, destPR;
  gint width, height;
  int w, h, b;
  guchar *copybuf;
  guchar color[4] =
  {0, 0, 0, 0};

  /* Get the input area. This is the bounding box of the selection in
   *  the image (or the entire image if there is no selection). Only
   *  operating on the input area is simply an optimization. It doesn't
   *  need to be done for correct operation. (It simply makes it go
   *  faster, since fewer pixels need to be operated on).
   */
  gimp_drawable_mask_bounds (drawable->id, &sx1, &sy1, &sx2, &sy2);

  /* Get the size of the input image. (This will/must be the same
   *  as the size of the output image.
   */
  width = drawable->width;
  height = drawable->height;
  bytes = drawable->bpp;

  if (gimp_drawable_has_alpha (drawable->id))
    {
      color[bytes - 1] = 0xff;
    }

  /*  initialize the pixel regions  */
  gimp_pixel_rgn_init (&srcPR, drawable, 0, 0, width, height, FALSE, FALSE);
  gimp_pixel_rgn_init (&destPR, drawable, 0, 0, width, height, TRUE, TRUE);

  /* First off, copy the old one to the new one. */
  copybuf = malloc (width * bytes);

  for (h = sy1; h < sy2; h++)
    {
      gimp_pixel_rgn_get_row (&srcPR, copybuf, sx1, h, (sx2-sx1));
      if ((h - my_config.y_offset) % my_config.height == 0)
	{ /* Draw row */
	  for (w = sx1; w < sx2; w++)
	    {
	      for (b = 0; b < bytes; b++)
		{
		  copybuf[(w-sx1) * bytes + b] = color[b];
		}
	    }
	}
      else
	{
	  for (w = sx1; w < sx2; w++)
	    {
	      if ((w - my_config.x_offset) % my_config.width == 0)
		{
		  for (b = 0; b < bytes; b++)
		    {
		      copybuf[(w-sx1) * bytes + b] = color[b];
		    }
		}
	    }
	}
      gimp_pixel_rgn_set_row (&destPR, copybuf, sx1, h , (sx2-sx1) );
      gimp_progress_update ((double) h / (double) (sy2 - sy1));
    }
  free (copybuf);

  /*  update the timred region  */
  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->id, TRUE);
  gimp_drawable_update (drawable->id, sx1, sy1, sx2 - sx1, sy2 - sy1);
}

/***************************************************
 * GUI stuff
 */

static void
close_callback (GtkWidget * widget, gpointer data)
{
  gimp_main_loop_quit ();
}

static void
ok_callback (GtkWidget * widget, gpointer data)
{
  run_flag = 1;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void
entry_callback (GtkWidget * widget, gpointer data)
{
  const gchar *text = gtk_editable_get_text (GTK_EDITABLE (widget));

  if (data == &my_config.width)
    my_config.width = atof (text);
  else if (data == &my_config.height)
    my_config.height = atoi (text);
  else if (data == &my_config.x_offset)
    my_config.x_offset = atoi (text);
  else if (data == &my_config.y_offset)
    my_config.y_offset = atoi (text);
}

static void
dialog_label (GtkWidget *table, const gchar *text,
	      gint left, gint top)
{
  GtkWidget *label;

  label = gtk_label_new (text);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, left, left + 1, top, top + 1,
		     GIMP_FILL, GIMP_FILL, 0, 0);
}

static void
dialog_entry (GtkWidget *table, gint *value,
	      gint left, gint top)
{
  GtkWidget *entry;
  gchar buffer[12];

  entry = gtk_entry_new ();
  gimp_table_attach (table, entry, left, left + 1, top, top + 1,
		     GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 0, 0);
  gtk_widget_set_size_request (entry, 50, -1);
  g_snprintf (buffer, sizeof (buffer), "%i", *value);
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
  g_signal_connect (entry, "changed",
		    G_CALLBACK (entry_callback), value);
}

static gint
dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *table;

  gtk_init ();

  dlg = gimp_dialog_new ("Grid");
  g_signal_connect (dlg, "destroy",
		    G_CALLBACK (close_callback), NULL);

  /*  Action area  */
  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (ok_callback), dlg, TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  /* The main table */
  /* Set its size (y, x) */
  table = gimp_table_new (4, 3, FALSE);
  gimp_container_set_border_width (table, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), table, TRUE, TRUE, 0);

  gtk_grid_set_row_spacing (GTK_GRID (table), 10);
  gtk_grid_set_column_spacing (GTK_GRID (table), 10);

  /* The X/Y labels */
  dialog_label (table, "X", 1, 1);
  dialog_label (table, "Y", 2, 1);

  /* The width and height entries */
  dialog_label (table, "Size:", 0, 2);
  dialog_entry (table, &my_config.width, 1, 2);
  dialog_entry (table, &my_config.height, 2, 2);

  /* The x_offset and y_offset entries */
  dialog_label (table, "Offset:", 0, 3);
  dialog_entry (table, &my_config.x_offset, 1, 3);
  dialog_entry (table, &my_config.y_offset, 2, 3);

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return run_flag;
}
