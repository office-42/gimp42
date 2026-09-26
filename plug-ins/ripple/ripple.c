/* Ripple --- image filter plug-in for The Gimp image manipulation program
 * Copyright (C) 1997 Brian Degenhardt
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
 * Please direct all comments, questions, bug reports  etc to Brian Degenhardt
 * bdegenha@ucsd.edu
 *
 * You can contact the original The Gimp authors at gimp@xcf.berkeley.edu
 */
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"


/* Some useful macros */
#ifndef M_PI
#define M_PI    3.14159265358979323846
#endif /* M_PI */

#define SCALE_WIDTH 200
#define TILE_CACHE_SIZE 16
#define ENTRY_WIDTH 35

#define HORIZONTAL 0
#define VERTICAL 1

#define SMEAR 0
#define WRAP 1
#define BLACK 2

#define SAWTOOTH 0
#define SINE 1

typedef struct {
    gint period;
    gint amplitude;
    gint orientation;
    gint edges;
    gint waveform;
    gint antialias;
    gint tile;
} RippleValues;

typedef struct {
  gint run;
} RippleInterface;


/* Declare local functions.
 */
static void      query  (void);
static void      run    (gchar    *name,
			 gint      nparams,
			 GParam   *param,
			 gint     *nreturn_vals,
			 GParam  **return_vals);
static void      ripple  (GDrawable * drawable);

static gint      ripple_dialog (void);
static GTile *   ripple_pixel  (GDrawable * drawable,
			       GTile *     tile,
			       gint        x1,
			       gint        y1,
			       gint        x2,
			       gint        y2,
			       gint        x,
			       gint        y,
			       gint *      row,
			       gint *      col,
			       guchar *    pixel);

static void      ripple_close_callback  (GtkWidget *widget,
					gpointer   data);
static void      ripple_ok_callback     (GtkWidget *widget,
					gpointer   data);

static void      ripple_toggle_update    (GtkWidget *widget,
					    gpointer   data);

static void      ripple_ientry_callback   (GtkWidget     *widget,
					     gpointer       data);

static void      ripple_iscale_callback   (GtkAdjustment *adjustment,
					     gpointer       data);

static gdouble displace_amount (gint location);

static guchar    averagetwo (gdouble location, guchar * v);

static guchar    averagefour  (gdouble location,  guchar *v);



/***** Local vars *****/

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

static RippleValues rvals =
{
  20,   /* period  */
  5,    /* amplitude */
  HORIZONTAL, /* orientation */
  WRAP, /* edges */
  SINE, /* waveform */
  TRUE, /* antialias */
  TRUE  /* tile */
};

static RippleInterface rpint =
{
  FALSE   /*  run  */
};

/***** Functions *****/

MAIN ()

static void
query ()
{
  static GParamDef args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image (unused)" },
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
    { PARAM_INT32, "period", "period; number of pixels for one wave to complete" },
    { PARAM_INT32, "amplitude", "amplitude; maximum displacement of wave" },
    { PARAM_INT32, "orientation", "orientation; 0 = Horizontal, 1 = Vertical" },
    { PARAM_INT32, "edges", "edges; 0 = smear, 1 =  wrap, 2 = black" },
    { PARAM_INT32, "waveform", "0 = sawtooth, 1 = sine wave" },
    { PARAM_INT32, "antialias", "antialias; True or False" },
    { PARAM_INT32, "tile", "tile; if this is true, the image will retain it's tilability" },
  };
  static GParamDef *return_vals = NULL;
  static gint nargs = sizeof (args) / sizeof (args[0]);
  static gint nreturn_vals = 0;

  gimp_install_procedure ("plug_in_ripple",
			  "Ripple the contents of the specified drawable",
			  "Ripples the pixels of the specified drawable. Each row or colum will be displaced a certain number of pixels coinciding with the given wave form",
			  "Brian Degenhardt <bdegenha@ucsd.edu>",
			  "Brian Degenhardt",
			  "1997",
			  "<Image>/Filters/Distorts/Ripple",
			  "RGB*, GRAY*",
			  PROC_PLUG_IN,
			  nargs, nreturn_vals,
			  args, return_vals);
}

static void
run (gchar  *name,
     gint    nparams,
     GParam  *param,
     gint   *nreturn_vals,
     GParam **return_vals)
{
  static GParam values[1];
  GDrawable *drawable;
  GRunModeType run_mode;
  GStatusType status = STATUS_SUCCESS;

  run_mode = param[0].data.d_int32;

  /*  Get the specified drawable  */
  drawable = gimp_drawable_get (param[2].data.d_drawable);

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;

  switch (run_mode)
    {
    case RUN_INTERACTIVE:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_ripple", &rvals);

      /*  First acquire information with a dialog  */
      if (! ripple_dialog ())
	return;
      break;

    case RUN_NONINTERACTIVE:
      /*  Make sure all the arguments are there!  */
      if (nparams != 10)
	status = STATUS_CALLING_ERROR;
      if (status == STATUS_SUCCESS)
	{
	  rvals.period = param[3].data.d_int32;
	  rvals.amplitude = param[4].data.d_int32;
          rvals.orientation = (param[5].data.d_int32) ? VERTICAL : HORIZONTAL;
          rvals.edges = (param[6].data.d_int32);
          rvals.waveform = param[7].data.d_int32;
          rvals.antialias = (param[8].data.d_int32) ? TRUE : FALSE;
          rvals.tile = (param[9].data.d_int32) ? TRUE : FALSE;
        }
      if (status == STATUS_SUCCESS &&
	  (rvals.edges < SMEAR || rvals.edges > BLACK))
          status = STATUS_CALLING_ERROR;
      break;

    case RUN_WITH_LAST_VALS:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_ripple", &rvals);
      break;

    default:
      break;
    }

  if (status == STATUS_SUCCESS)
    {
      /*  Make sure that the drawable is gray or RGB color  */
      if (gimp_drawable_color (drawable->id) || gimp_drawable_gray (drawable->id))
	{
	  gimp_progress_init ("Rippling...");

	  /*  set the tile cache size  */
	  gimp_tile_cache_ntiles (TILE_CACHE_SIZE);

	  /*  run the ripple effect  */
	  ripple (drawable);

	  if (run_mode != RUN_NONINTERACTIVE)
	    gimp_displays_flush ();

	  /*  Store data  */
	  if (run_mode == RUN_INTERACTIVE)
	    gimp_set_data ("plug_in_ripple", &rvals, sizeof (RippleValues));
	}
      else
	{
	  /* gimp_message ("ripple: cannot operate on indexed color images"); */
	  status = STATUS_EXECUTION_ERROR;
	}
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

/*****/

static void
ripple (GDrawable *drawable)
{
  GPixelRgn dest_rgn;
  GTile   * tile = NULL;
  gint      row = -1;
  gint      col = -1;
  gpointer  pr;

  gint    width, height;
  gint    bytes;
  guchar *destline;
  guchar *dest;
  guchar *otherdest;
  guchar  pixel[4][4];
  gint    x1, y1, x2, y2;
  gint    x, y;
  gint    progress, max_progress;
  gdouble needx, needy;

  guchar  values[4];
  guchar  val;

  gint xi, yi;

  gint k;

  /* Get selection area */

  gimp_drawable_mask_bounds (drawable->id, &x1, &y1, &x2, &y2);

  width  = drawable->width;
  height = drawable->height;
  bytes  = drawable->bpp;

  if ( rvals.tile )
  {
      rvals.edges = WRAP;
      rvals.period = width/(width/rvals.period)*(rvals.orientation==HORIZONTAL) + height/(height/rvals.period)*(rvals.orientation==VERTICAL);
  }

  progress     = 0;
  max_progress = (x2 - x1) * (y2 - y1);

/* Ripple the image.  It's a pretty simple algorithm.  If horizontal
     is selected, then every row is displaced a number of pixels that
     follows the pattern of the waveform selected.  The effect is
     just reproduced with columns if vertical is selected.
*/

  gimp_pixel_rgn_init (&dest_rgn, drawable, x1, y1, (x2 - x1), (y2 - y1), TRUE, TRUE);
  for (pr = gimp_pixel_rgns_register (1, &dest_rgn); pr != NULL; pr = gimp_pixel_rgns_process (pr))
  {
      if (rvals.orientation == VERTICAL)
      {
          destline = dest_rgn.data;

          for (x = dest_rgn.x; x < (dest_rgn.x + dest_rgn.w); x++)
          {
              dest = destline;

              for (y = dest_rgn.y; y < (dest_rgn.y + dest_rgn.h); y++)
              {
                  otherdest = dest;

                  needy = y + displace_amount(x);
                  yi = floor(needy);

                      /* Tile the image. */
                  if (rvals.edges == WRAP)
                  {
                      needy = fmod(needy + height, height);
                      yi = (yi + height) % height;
                  }
                      /* Smear out the edges of the image by repeating pixels. */
                  else if (rvals.edges == SMEAR)
                  {
                      if (yi < 0)
                          yi = 0;
                      else if (yi > height - 1)
                          yi = height - 1;
                  }

                  if ( rvals.antialias)
                  {
                      if (yi == height - 1)
                      {
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, x, yi, &row, &col, pixel[0]);

                          for (k = 0; k < bytes; k++)
                              *otherdest++ = pixel[0][k];
                      }
                      else if (needy < 0 && needy > -1)
                      {
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, x, 0, &row, &col, pixel[0]);

                          for (k = 0; k < bytes; k++)
                              *otherdest++ = pixel[0][k];
                      }

                      else if (yi == height - 2 || yi == 0)
                      {
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, x, yi, &row, &col, pixel[0]);
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, x, yi + 1, &row, &col, pixel[1]);

                          for (k = 0; k < bytes; k++)
                          {
                              values[0] = pixel[0][k];
                              values[1] = pixel[1][k];
                              val = averagetwo(needy, values);

                              *otherdest++ = val;
                          } /* for */
                      }
                      else
                      {
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, x, yi, &row, &col, pixel[0]);
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, x, yi + 1, &row, &col, pixel[1]);
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, x, yi - 1, &row, &col, pixel[2]);
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, x, yi + 2, &row, &col, pixel[3]);

                          for (k = 0; k < bytes; k++)
                          {
                              values[0] = pixel[0][k];
                              values[1] = pixel[1][k];
                              values[2] = pixel[2][k];
                              values[3] = pixel[3][k];

                              val = averagefour(needy, values);

                              *otherdest++ = val;
                          } /* for */
                      } /* else */
                  } /* antialias */

                  else
                  {
                      tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, x, yi, &row, &col, pixel[0]);

                      for (k = 0; k < bytes; k++)
                          *otherdest++ = pixel[0][k];
                  }
                  dest += dest_rgn.rowstride;
              } /* for */

              for (k = 0; k < bytes; k++)
                  destline++;
          } /* for */

          progress += dest_rgn.w * dest_rgn.h;
          gimp_progress_update ((double) progress / (double) max_progress);
      }
      else /* HORIZONTAL */
      {
          destline = dest_rgn.data;

          for (y = dest_rgn.y; y < (dest_rgn.y + dest_rgn.h); y++)
          {
              dest = destline;

              for (x = dest_rgn.x; x < (dest_rgn.x + dest_rgn.w); x++)
              {
                  needx = x + displace_amount(y);
                  xi = floor(needx);

                      /* Tile the image. */
                  if (rvals.edges == WRAP)
                  {
                      needx = fmod((needx + width), width);
                      xi = (xi + width) % width;
                  }
                      /* Smear out the edges of the image by repeating pixels. */
                  else if (rvals.edges == SMEAR)
                  {
                      if (xi < 0)
                          xi = 0;
                      else if (xi > width - 1)
                          xi = width - 1;
                  }

                  if ( rvals.antialias)
                  {
                      if (xi == width - 1)
                      {
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, xi, y, &row, &col, pixel[0]);

                          for (k = 0; k < bytes; k++)
                              *dest++ = pixel[0][k];
                      }
                      else if (floor(needx) ==  -1)
                      {
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, 0, y, &row, &col, pixel[0]);

                          for (k = 0; k < bytes; k++)
                              *dest++ = pixel[0][k];
                      }

                      else if (xi == width - 2 || xi == 0)
                      {
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, xi, y, &row, &col, pixel[0]);
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, xi + 1, y, &row, &col, pixel[1]);

                          for (k = 0; k < bytes; k++)
                          {
                              values[0] = pixel[0][k];
                              values[1] = pixel[1][k];
                              val = averagetwo(needx, values);

                              *dest++ = val;
                          } /* for */
                      }
                      else
                      {
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, xi, y, &row, &col, pixel[0]);
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, xi + 1, y, &row, &col, pixel[1]);
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, xi - 1 , y, &row, &col, pixel[2]);
                          tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, xi + 2, y, &row, &col, pixel[3]);

                          for (k = 0; k < bytes; k++)
                          {
                              values[0] = pixel[0][k];
                              values[1] = pixel[1][k];
                              values[2] = pixel[2][k];
                              values[3] = pixel[3][k];

                              val = averagefour(needx, values);

                              *dest++ = val;
                          } /* for */
                      } /* else */
                  } /* antialias */

                  else
                  {
                      tile = ripple_pixel (drawable, tile, x1, y1, x2, y2, xi, y, &row, &col, pixel[0]);

                      for (k = 0; k < bytes; k++)
                          *dest++ = pixel[0][k];
                  }
              } /* for */

              destline += dest_rgn.rowstride;
          } /* for */

          progress += dest_rgn.w * dest_rgn.h;
          gimp_progress_update ((double) progress / (double) max_progress);
      }

  }  /* for  */

  if (tile)
      gimp_tile_unref (tile, FALSE);

      /*  update the region  */
  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->id, TRUE);
  gimp_drawable_update (drawable->id, x1, y1, (x2 - x1), (y2 - y1));
} /* ripple */


static GtkWidget *
ripple_frame_new (GtkWidget   *table,
		  const gchar *title,
		  gint         col,
		  gint         row)
{
  GtkWidget *frame;
  GtkWidget *toggle_vbox;

  frame = gtk_frame_new (title);
  gimp_table_attach (table, frame, col, col + 1, row, row + 1,
		     GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 5, 5);
  toggle_vbox = gimp_vbox_new (FALSE, 5);
  gimp_container_set_border_width (toggle_vbox, 5);
  gtk_frame_set_child (GTK_FRAME (frame), toggle_vbox);

  return toggle_vbox;
}

static GtkWidget *
ripple_toggle_new (GtkWidget   *vbox,
		   GtkWidget   *group,
		   gboolean     radio,
		   const gchar *label,
		   gint        *value)
{
  GtkWidget *toggle;

  if (radio)
    toggle = gimp_radio_button_new (group, label);
  else
    toggle = gtk_check_button_new_with_label (label);
  gimp_box_pack_start (vbox, toggle, FALSE, FALSE, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (ripple_toggle_update), value);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), *value);

  return toggle;
}

static void
ripple_scale_entry_new (GtkWidget   *table,
			const gchar *text,
			gint         row,
			gint        *value)
{
  GtkWidget     *label;
  GtkWidget     *hbox;
  GtkWidget     *scale;
  GtkWidget     *entry;
  GtkAdjustment *scale_data;
  gchar          buffer[32];

  label = gtk_label_new (text);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, row, row + 1,
		     GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 10, 5);

  hbox = gimp_hbox_new (FALSE, 5);
  gimp_table_attach (table, hbox, 1, 2, row, row + 1,
		     GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 0, 0);

  scale_data = gtk_adjustment_new (*value, 0, 200, 1, 1, 0.0);
  g_signal_connect (scale_data, "value-changed",
		    G_CALLBACK (ripple_iscale_callback), value);

  scale = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, scale_data);
  gtk_widget_set_size_request (scale, SCALE_WIDTH, -1);
  gtk_scale_set_digits (GTK_SCALE (scale), 2);
  gtk_scale_set_draw_value (GTK_SCALE (scale), FALSE);
  gimp_box_pack_start (hbox, scale, TRUE, TRUE, 0);

  entry = gtk_entry_new ();
  g_object_set_data (G_OBJECT (entry), "user_data", scale_data);
  g_object_set_data (G_OBJECT (scale_data), "user_data", entry);
  gimp_box_pack_start (hbox, entry, FALSE, TRUE, 0);
  gtk_widget_set_size_request (entry, ENTRY_WIDTH, -1);
  gtk_editable_set_width_chars (GTK_EDITABLE (entry), 4);
  sprintf (buffer, "%d", *value);
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
  g_signal_connect (entry, "changed",
		    G_CALLBACK (ripple_ientry_callback), value);
}

static gint
ripple_dialog (void)
{
    GtkWidget *dlg;
    GtkWidget *button;
    GtkWidget *toggle;
    GtkWidget *hbox;
    GtkWidget *toggle_vbox;
    GtkWidget *main_vbox;
    GtkWidget *frame;
    GtkWidget *table;
    gint do_horizontal = (rvals.orientation == HORIZONTAL);
    gint do_vertical = (rvals.orientation == VERTICAL);

    gint do_smear = (rvals.edges == SMEAR);
    gint do_wrap = (rvals.edges == WRAP);
    gint do_black = (rvals.edges == BLACK);

    gint do_sawtooth = (rvals.waveform == SAWTOOTH);
    gint do_sine = (rvals.waveform == SINE);

    gtk_init ();

    dlg = gimp_dialog_new ("Ripple");
    g_signal_connect (dlg, "destroy",
		      G_CALLBACK (ripple_close_callback), NULL);

        /*  Action area  */
    gimp_dialog_add_button (dlg, "OK", G_CALLBACK (ripple_ok_callback),
			    dlg, TRUE);
    button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
    g_signal_connect_swapped (button, "clicked",
			      G_CALLBACK (gtk_window_destroy), dlg);

        /*  The main vbox  */
    main_vbox = gimp_vbox_new (FALSE, 5);
    gimp_container_set_border_width (main_vbox, 10);
    gimp_box_pack_start (gimp_dialog_get_vbox (dlg), main_vbox, TRUE, TRUE, 0);

        /*  The hbox for first row of options  */
    hbox = gimp_hbox_new (FALSE, 5);
    gimp_box_pack_start (main_vbox, hbox, TRUE, TRUE, 0);

        /* The table to hold the four frames of options */
    table = gimp_table_new (2, 2, FALSE);
    gimp_container_set_border_width (table, 10);
    gimp_box_pack_start (hbox, table, TRUE, TRUE, 0);

        /* Options section */
    toggle_vbox = ripple_frame_new (table, "Options", 0, 0);
    ripple_toggle_new (toggle_vbox, NULL, FALSE, "Antialiasing",
		       &rvals.antialias);
    ripple_toggle_new (toggle_vbox, NULL, FALSE, "Retain Tilability",
		       &rvals.tile);

        /*  Orientation toggle box  */
    toggle_vbox = ripple_frame_new (table, "Orientation", 1, 0);
    toggle = ripple_toggle_new (toggle_vbox, NULL, TRUE, "Horizontal",
				&do_horizontal);
    ripple_toggle_new (toggle_vbox, toggle, TRUE, "Vertical", &do_vertical);

        /*  Edges toggle box  */
    toggle_vbox = ripple_frame_new (table, "Edges", 0, 1);
    toggle = ripple_toggle_new (toggle_vbox, NULL, TRUE, "Wrap", &do_wrap);
    ripple_toggle_new (toggle_vbox, toggle, TRUE, "Smear", &do_smear);
    ripple_toggle_new (toggle_vbox, toggle, TRUE, "Black", &do_black);

        /*  Wave type toggle box  */
    toggle_vbox = ripple_frame_new (table, "Wave Type", 1, 1);
    toggle = ripple_toggle_new (toggle_vbox, NULL, TRUE, "Sawtooth",
				&do_sawtooth);
    ripple_toggle_new (toggle_vbox, toggle, TRUE, "Sine", &do_sine);

  /*  parameter settings  */
  frame = gtk_frame_new ("Parameter Settings");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (main_vbox, frame, TRUE, TRUE, 0);
  table = gimp_table_new (2, 2, FALSE);
  gimp_container_set_border_width (table, 10);
  gtk_frame_set_child (GTK_FRAME (frame), table);

  ripple_scale_entry_new (table, "Period", 0, &rvals.period);
  ripple_scale_entry_new (table, "Amplitude", 1, &rvals.amplitude);

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

 /*  determine orientation  */
  if (do_horizontal)
    rvals.orientation = HORIZONTAL;
  else
    rvals.orientation = VERTICAL;

  /*  determine edges  */
  if (do_smear)
    rvals.edges = SMEAR;
  else if (do_wrap)
    rvals.edges = WRAP;
  else if (do_black)
    rvals.edges = BLACK;

  /*  determine wave form  */
  if (do_sawtooth)
    rvals.waveform = SAWTOOTH;
  else if (do_sine)
    rvals.waveform = SINE;

  return rpint.run;
}

/*****/

static GTile *
ripple_pixel (GDrawable * drawable,
	     GTile *     tile,
	     gint        x1,
	     gint        y1,
	     gint        x2,
	     gint        y2,
	     gint        x,
	     gint        y,
	     gint *      row,
	     gint *      col,
	     guchar *    pixel)
{
  static guchar empty_pixel[4] = {0, 0, 0, 0};
  guchar *data;
  gint b;

  if (x >= x1 && y >= y1 && x < x2 && y < y2)
    {
      if ((x >> 6 != *col) || (y >> 6 != *row))
	{
	  *col = x / 64;
	  *row = y / 64;
	  if (tile)
	    gimp_tile_unref (tile, FALSE);
	  tile = gimp_drawable_get_tile (drawable, FALSE, *row, *col);
	  gimp_tile_ref (tile);
	}

      data = tile->data + tile->bpp * (tile->ewidth * (y % 64) + (x % 64));
    }
  else
    data = empty_pixel;

  for (b = 0; b < drawable->bpp; b++)
    pixel[b] = data[b];

  return tile;
}




/*  Ripple interface functions  */

static void
ripple_close_callback (GtkWidget *widget,
		      gpointer   data)
{
  gimp_main_loop_quit ();
}

static void
ripple_ok_callback (GtkWidget *widget,
		   gpointer   data)
{
  rpint.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void
ripple_toggle_update (GtkWidget *widget,
			gpointer   data)
{
  int *toggle_val;

  toggle_val = (int *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    *toggle_val = TRUE;
  else
    *toggle_val = FALSE;
}

static void
ripple_ientry_callback (GtkWidget *widget,
			  gpointer   data)
{
  GtkAdjustment *adjustment;
  int new_val;
  int *val;

  val = data;
  new_val = atoi (gtk_editable_get_text (GTK_EDITABLE (widget)));

  if (*val != new_val)
    {
      adjustment = g_object_get_data (G_OBJECT (widget), "user_data");

      if ((new_val >= gtk_adjustment_get_lower (adjustment)) &&
	  (new_val <= gtk_adjustment_get_upper (adjustment)))
	{
	  *val = new_val;
	  gtk_adjustment_set_value (adjustment, new_val);
	}
    }
}

static void
ripple_iscale_callback (GtkAdjustment *adjustment,
			  gpointer       data)
{
  GtkWidget *entry;
  gchar buffer[32];
  int *val;

  val = data;
  if (*val != (int) gtk_adjustment_get_value (adjustment))
    {
      *val = gtk_adjustment_get_value (adjustment);
      entry = g_object_get_data (G_OBJECT (adjustment), "user_data");
      sprintf (buffer, "%d", *val);
      gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
    }
}

static guchar
averagetwo (gdouble location, guchar *v)
{
  location = fmod(location, 1.0);

  return (guchar) ((1.0 - location) * v[0] + location * v[1]);
} /* averagetwo */

static guchar
averagefour (gdouble location, guchar *v)
{
    location = fmod(location, 1.0);

    return ((1.0 - location) * (v[0] + v[2]) + location * (v[1] + v[3]))/2;
}



static gdouble
displace_amount (gint location)
{
    switch (rvals.waveform)
    {
        case SINE:
            return rvals.amplitude*sin(location*(2*M_PI)/(double)rvals.period);
        case SAWTOOTH:
            return floor(rvals.amplitude*(fabs((((location%rvals.period)/(double)rvals.period)*4)-2)-1));
    }
    return 0;
}
