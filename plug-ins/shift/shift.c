/* Shift --- image filter plug-in for The Gimp image manipulation program
 * Copyright (C) 1997 Brian Degenhardt and Federico Mena Quintero
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
 * You can contact Federico Mena Quintero at quartic@polloux.fciencias.unam.mx
 * You can contact the original The Gimp authors at gimp@xcf.berkeley.edu
 */

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"


/* Some useful macros */

#define SCALE_WIDTH 200
#define TILE_CACHE_SIZE 16
#define HORIZONTAL 0
#define VERTICAL 1
#define ENTRY_WIDTH 35

typedef struct {
    gint shift_amount;
    gint orientation;
} ShiftValues;

typedef struct {
  gint run;
} ShiftInterface;


/* Declare local functions.
 */
static void      query  (void);
static void      run    (gchar    *name,
			 gint      nparams,
			 GParam   *param,
			 gint     *nreturn_vals,
			 GParam  **return_vals);
static void      shift  (GDrawable * drawable);

static gint      shift_dialog (void);
static GTile *   shift_pixel  (GDrawable * drawable,
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

static void      shift_close_callback  (GtkWidget *widget,
					gpointer   data);
static void      shift_ok_callback     (GtkWidget *widget,
					gpointer   data);

static void      shift_toggle_update    (GtkWidget *widget,
					    gpointer   data);

static void      shift_ientry_callback   (GtkWidget     *widget,
					     gpointer       data);

static void      shift_iscale_callback   (GtkAdjustment *adjustment,
					     gpointer       data);

/***** Local vars *****/

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

static ShiftValues shvals =
{
  5,   /* shift amount  */
  HORIZONTAL
};

static ShiftInterface shint =
{
  FALSE   /*  run  */
};

/***** Functions *****/

MAIN ()

static void
query (void)
{
  static GParamDef args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image (unused)" },
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
    { PARAM_INT32, "shift_amount", "shift amount (0 <= shift_amount_x <= 200)" },
    { PARAM_INT32, "orientation", "vertical, horizontal orientation" },
  };
  static GParamDef *return_vals = NULL;
  static gint nargs = sizeof (args) / sizeof (args[0]);
  static gint nreturn_vals = 0;

  gimp_install_procedure ("plug_in_shift",
			  "Shift the contents of the specified drawable",
			  "Shifts the pixels of the specified drawable. Each row will be displaced a random value of pixels.",
			  "Spencer Kimball and Peter Mattis, ported by Brian Degenhardt and Federico Mena Quintero",
			  "Brian Degenhardt",
			  "1997",
			  "<Image>/Filters/Distorts/Shift",
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
      gimp_get_data ("plug_in_shift", &shvals);

      /*  First acquire information with a dialog  */
      if (! shift_dialog ())
	return;
      break;

    case RUN_NONINTERACTIVE:
      /*  Make sure all the arguments are there!  */
      if (nparams != 5)
	status = STATUS_CALLING_ERROR;
      if (status == STATUS_SUCCESS)
	{
	  shvals.shift_amount = param[3].data.d_int32;
          shvals.orientation = (param[4].data.d_int32) ? HORIZONTAL : VERTICAL;
        }
      if ((status == STATUS_SUCCESS) &&
	  (shvals.shift_amount < 0 || shvals.shift_amount > 200))
     	status = STATUS_CALLING_ERROR;
      break;

    case RUN_WITH_LAST_VALS:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_shift", &shvals);
      break;

    default:
      break;
    }

  if (status == STATUS_SUCCESS)
    {
      /*  Make sure that the drawable is gray or RGB color  */
      if (gimp_drawable_color (drawable->id) || gimp_drawable_gray (drawable->id))
	{
	  gimp_progress_init ("Shifting...");

	  /*  set the tile cache size  */
	  gimp_tile_cache_ntiles (TILE_CACHE_SIZE);

	  /*  run the shift effect  */
	  shift (drawable);

	  if (run_mode != RUN_NONINTERACTIVE)
	    gimp_displays_flush ();

	  /*  Store data  */
	  if (run_mode == RUN_INTERACTIVE)
	    gimp_set_data ("plug_in_shift", &shvals, sizeof (ShiftValues));
	}
      else
	{
	  /* gimp_message ("shift: cannot operate on indexed color images"); */
	  status = STATUS_EXECUTION_ERROR;
	}
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

/*****/

static void
shift (GDrawable *drawable)
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
  gint    seed;

  gint amount;

  gint xdist, ydist;
  gint xi, yi;

  gint k;
  gint mod_value, sub_value;

  /* Get selection area */

  gimp_drawable_mask_bounds (drawable->id, &x1, &y1, &x2, &y2);

  width  = drawable->width;
  height = drawable->height;
  bytes  = drawable->bpp;

  progress     = 0;
  max_progress = (x2 - x1) * (y2 - y1);

  amount = shvals.shift_amount;

  /* Initialize random stuff */
  mod_value = amount + 1;
  sub_value = mod_value / 2;
  seed = time(NULL);

/* Shift the image.  It's a pretty simple algorithm.  If horizontal
     is selected, then every row is shifted a random number of pixels
     in the range of -shift_amount/2 to shift_amount/2.  The effect is
     just reproduced with columns if vertical is selected.  Vertical
     has been added since 0.54 so that the user doesn't have to rotate
     the image to do a vertical shift.
  */

  gimp_pixel_rgn_init (&dest_rgn, drawable, x1, y1, (x2 - x1), (y2 - y1), TRUE, TRUE);
  for (pr = gimp_pixel_rgns_register (1, &dest_rgn); pr != NULL; pr = gimp_pixel_rgns_process (pr))
    {
        if (shvals.orientation == VERTICAL)
        {

            destline = dest_rgn.data;
            srand(seed+dest_rgn.x);

            for (x = dest_rgn.x; x < (dest_rgn.x + dest_rgn.w); x++)
            {
                dest = destline;
                ydist = (rand() % mod_value) - sub_value;
                for (y = dest_rgn.y; y < (dest_rgn.y + dest_rgn.h); y++)
                {
                    otherdest = dest;

                    yi = (y + ydist + height)%height; /*  add width before % because % isn't a true modulo */

                    tile = shift_pixel (drawable, tile, x1, y1, x2, y2, x, yi, &row, &col, pixel[0]);

                    for (k = 0; k < bytes; k++)
                        *otherdest++ = pixel[0][k];
                    dest += dest_rgn.rowstride;
                } /* for */

                for (k = 0; k < bytes; k++)
                    destline++;
            } /* for */

            progress += dest_rgn.w * dest_rgn.h;
            gimp_progress_update ((double) progress / (double) max_progress);
        }
        else
        {
            destline = dest_rgn.data;
            srand(seed+dest_rgn.y);

            for (y = dest_rgn.y; y < (dest_rgn.y + dest_rgn.h); y++)
            {
                dest = destline;
                xdist = (rand() % mod_value) - sub_value;
                for (x = dest_rgn.x; x < (dest_rgn.x + dest_rgn.w); x++)
                {
                    xi = (x + xdist + width)%width; /*  add width before % because % isn't a true modulo */

                    tile = shift_pixel (drawable, tile, x1, y1, x2, y2, xi, y, &row, &col, pixel[0]);

                    for (k = 0; k < bytes; k++)
                        *dest++ = pixel[0][k];
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
} /* shift */


static gint
shift_dialog (void)
{
  GtkWidget *amount_label;
  GtkWidget *amount;
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *toggle;
  GtkWidget *frame;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *entry;
  GtkAdjustment *amount_data;
  gchar buffer[32];
  gint do_horizontal = (shvals.orientation == HORIZONTAL);
  gint do_vertical = (shvals.orientation == VERTICAL);

  gtk_init ();

  dlg = gimp_dialog_new ("Shift");
  g_signal_connect (dlg, "destroy",
		    G_CALLBACK (shift_close_callback), NULL);

  /*  Action area  */
  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (shift_ok_callback),
			  dlg, TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  /*  parameter settings  */
  frame = gtk_frame_new ("Parameter Settings");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);
  vbox = gimp_vbox_new (FALSE, 5);
  gimp_container_set_border_width (vbox, 10);
  gtk_frame_set_child (GTK_FRAME (frame), vbox);

  toggle = gimp_radio_button_new (NULL, "Shift Horizontally");
  gimp_box_pack_start (vbox, toggle, TRUE, TRUE, 0);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), do_horizontal);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (shift_toggle_update), &do_horizontal);

  toggle = gimp_radio_button_new (toggle, "Shift Vertically");
  gimp_box_pack_start (vbox, toggle, TRUE, TRUE, 0);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), do_vertical);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (shift_toggle_update), &do_vertical);

  hbox = gimp_hbox_new (FALSE, 5);
  gimp_box_pack_start (vbox, hbox, TRUE, TRUE, 0);

  amount_label = gtk_label_new ("Shift Amount:");
  gtk_label_set_xalign (GTK_LABEL (amount_label), 0.0);
  gimp_box_pack_start (hbox, amount_label, TRUE, TRUE, 0);

  amount_data = gtk_adjustment_new (shvals.shift_amount, 0, 200, 1, 1, 0.0);
  g_signal_connect (amount_data, "value-changed",
		    G_CALLBACK (shift_iscale_callback), &shvals.shift_amount);

  amount = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, amount_data);
  gtk_widget_set_size_request (amount, SCALE_WIDTH, -1);
  gtk_scale_set_digits (GTK_SCALE (amount), 0);
  gtk_scale_set_draw_value (GTK_SCALE (amount), FALSE);
  gimp_box_pack_start (hbox, amount, TRUE, TRUE, 0);

  entry = gtk_entry_new ();
  g_object_set_data (G_OBJECT (entry), "user_data", amount_data);
  g_object_set_data (G_OBJECT (amount_data), "user_data", entry);
  gimp_box_pack_start (hbox, entry, FALSE, TRUE, 0);
  gtk_widget_set_size_request (entry, ENTRY_WIDTH, -1);
  gtk_editable_set_width_chars (GTK_EDITABLE (entry), 4);
  g_snprintf (buffer, sizeof (buffer), "%d", shvals.shift_amount);
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
  g_signal_connect (entry, "changed",
		    G_CALLBACK (shift_ientry_callback), &shvals.shift_amount);

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  if (do_horizontal)
      shvals.orientation = HORIZONTAL;
  else
      shvals.orientation = VERTICAL;

  return shint.run;
}

/*****/

static GTile *
shift_pixel (GDrawable * drawable,
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




/*  Shift interface functions  */

static void
shift_close_callback (GtkWidget *widget,
		      gpointer   data)
{
  gimp_main_loop_quit ();
}

static void
shift_ok_callback (GtkWidget *widget,
		   gpointer   data)
{
  shint.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void
shift_toggle_update (GtkWidget *widget,
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
shift_ientry_callback (GtkWidget *widget,
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
shift_iscale_callback (GtkAdjustment *adjustment,
		       gpointer       data)
{
  GtkWidget *entry;
  gchar buffer[32];
  int *val;
  gint value;

  val = data;
  value = (int) gtk_adjustment_get_value (adjustment);
  if (*val != value)
    {
      *val = value;
      entry = g_object_get_data (G_OBJECT (adjustment), "user_data");
      g_snprintf (buffer, sizeof (buffer), "%d", value);
      gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
    }
}
