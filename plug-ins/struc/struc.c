/*
 * This is the Struc plug-in for the GIMP 0.99
 * Version 1.01
 *
 * Copyright (C) 1997 Karl-Johan Andersson (t96kja@student.tdb.uu.se)
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
 */

/* Don't ask me why its called "Struc". The first .c-file
 * just happend to be called struc.c
 * 
 * Some code for the dialog was taken from Motion Blur plug-in for 
 * GIMP 0.99 by Daniel Skarda (0rfelyus@atrey.karlin.mff.cuni.cz) 
 * 
 * Please send any comments or suggestions to me,
 * Karl-Johan Andersson (t96kja@student.tdb.uu.se)
 * 
 */ 


#include <stdlib.h>
#include <stdio.h>
#include "struc.h"
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

/* --- Typedefs --- */
typedef struct {
  gint direction;
  gint depth;
} StrucValues;
typedef struct {
    gint run;
} StructInterface;

/* --- Declare local functions --- */
static void query (void);
static void run (char      *name,
	         int        nparams,
       	         GParam    *param,
		 int       *nreturn_vals,
	         GParam   **return_vals);
static gint struc_dialog (void);
static void struc_close_callback (GtkWidget *widget, gpointer data);
static void struc_ok_callback (GtkWidget *widget, gpointer data);
static void struc_scale_update (GtkAdjustment *adjustment, gpointer data);
static void struc_toggle_update (GtkWidget *widget, gpointer data);
static void strucpi (GDrawable *drawable);

/* --- Variables --- */

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};
static StrucValues svals =
{
    0,     /* direction*/
    4      /* depth */
};
static StructInterface s_int =
{
  FALSE     /* run */
};


/* --- Functions --- */

MAIN ()

static void query () 
{
  static GParamDef args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image (unused)" },
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
    { PARAM_INT32, "direction", "Light direction (0 - 3)" },
    { PARAM_INT32, "depth", "Texture depth (1 - 50)" },
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0; 

  gimp_install_procedure ("plug_in_apply_canvas",
			  "Adds a canvas texture map to the picture",
			  "This function applies a canvas texture map to the drawable.",
			  "Karl-Johan Andersson", /* Author */
			  "Karl-Johan Andersson", /* Copyright */
			  "1997",
			  "<Image>/Filters/Artistic/Apply Canvas",
			  "RGB*, GRAY*",
			  PROC_PLUG_IN,
			  nargs, nreturn_vals,
			  args, return_vals);
}

static void run (gchar   *name,
                 gint     nparams,
                 GParam  *param,
                 gint    *nreturn_vals,
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
      gimp_get_data ("plug_in_struc", &svals);
      
      /*  First acquire information with a dialog  */
      if (! struc_dialog ())
	{
	  gimp_drawable_detach (drawable);
	  return;
	}
      break;
      
    case RUN_NONINTERACTIVE:
      /*  Make sure all the arguments are there!  */
      if (nparams != 5)
	status = STATUS_CALLING_ERROR;
      if (status == STATUS_SUCCESS)
	{
	  svals.direction = (gint) param[3].data.d_int32;
	  svals.depth = (gint) param[4].data.d_int32;
	}
      if(svals.direction<0 || svals.direction>4) 
	status = STATUS_CALLING_ERROR;
      if(svals.depth<1 || svals.depth>50) 
	status = STATUS_CALLING_ERROR;
      break;
      
    case RUN_WITH_LAST_VALS:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_struc", &svals);
      break;
      
    default:
      break;
    }
  
  if (status == STATUS_SUCCESS)
    {
      /*  Make sure that the drawable is gray or RGB color  */
      if (gimp_drawable_color (drawable->id) || gimp_drawable_gray (drawable->id))
	{
	  gimp_progress_init ("struc");
	  gimp_tile_cache_ntiles (2 * (drawable->width / gimp_tile_width () + 1));
	  
	  strucpi (drawable);
	      
	  if (run_mode != RUN_NONINTERACTIVE)
	    gimp_displays_flush (); 
	  /*  Store data  */
	  if (run_mode == RUN_INTERACTIVE)
	    gimp_set_data ("plug_in_struc", &svals, sizeof (StrucValues));
	}
      else
	{
	  /* gimp_message ("struc: cannot operate on indexed color images"); */
	  status = STATUS_EXECUTION_ERROR;
	}
    }
  
  values[0].data.d_status = status;
  
  gimp_drawable_detach (drawable);
}

static gint struc_dialog(void)
{
  GtkWidget *dlg;
  GtkWidget *oframe, *iframe;
  GtkWidget *abox, *bbox, *cbox;
  GtkWidget *button, *label;
  GtkWidget *scale;
  GtkAdjustment *adjustment;

  gtk_init ();

  dlg = gimp_dialog_new ("Struc");
  g_signal_connect (dlg, "destroy",
		    G_CALLBACK (struc_close_callback),
		    NULL);
  /* Action area */
  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (struc_ok_callback),
			  dlg, TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  /* Parameter settings */
  abox = gimp_vbox_new (FALSE, 5);
  gimp_container_set_border_width (abox, 5);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg),
		       abox, FALSE, FALSE, 0);

  oframe = gtk_frame_new ("Filter options");
  gimp_box_pack_start (abox,
		       oframe, TRUE, TRUE, 0);

  bbox = gimp_vbox_new (FALSE, 5);
  gimp_container_set_border_width (bbox, 5);
  gtk_frame_set_child (GTK_FRAME (oframe), bbox);

  /* Radio buttons */
  iframe = gtk_frame_new ("Direction");
  gimp_box_pack_start (bbox,
		       iframe, FALSE, FALSE, 0);

  cbox = gimp_vbox_new (FALSE, 5);
  gimp_container_set_border_width (cbox, 5);
  gtk_frame_set_child (GTK_FRAME (iframe), cbox);

  {
    int   i;
    char * name[4]= {"Top-right", "Top-left", "Bottom-left", "Bottom-right"};

    button = NULL;
    for (i=0; i < 4; i++)
      {
	button = gimp_radio_button_new (button, name[i]);
	gtk_check_button_set_active (GTK_CHECK_BUTTON (button),
				     (svals.direction==i));

	g_signal_connect (button, "toggled",
			  G_CALLBACK (struc_toggle_update),
			  GINT_TO_POINTER (i));

	gtk_box_append (GTK_BOX (cbox), button);
      }
  }

  /* Horizontal scale */
  label = gtk_label_new ("Depth");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_box_append (GTK_BOX (bbox), label);

  adjustment = gtk_adjustment_new (svals.depth, 1, 50, 1, 1, 1);
  g_signal_connect (adjustment, "value-changed",
		    G_CALLBACK (struc_scale_update),
		    &(svals.depth));
  scale = gimp_hscale_new (adjustment, 0);
  gtk_widget_set_size_request (scale, 150, 30);
  gtk_box_append (GTK_BOX (bbox), scale);

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return s_int.run;
}

/* Interface functions */
static void struc_close_callback (GtkWidget *widget, gpointer data)
{
  gimp_main_loop_quit ();
}

static void struc_ok_callback (GtkWidget *widget, gpointer data)
{
  s_int.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void struc_scale_update (GtkAdjustment *adjustment, gpointer data)
{
  gint *dptr = (gint*) data;
  *dptr = (gint) gtk_adjustment_get_value (adjustment);
}

static void struc_toggle_update (GtkWidget *widget, gpointer data)
{
   if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
     svals.direction = GPOINTER_TO_INT (data);
}

/* Filter function */
static void strucpi (GDrawable *drawable)
{
  GPixelRgn srcPR, destPR;
  gint width, height;
  gint bytes;
  guchar *dest, *d;
  guchar *cur_row;
  gint row, col, rrow, rcol;
  gint x1, y1, x2, y2, varde;
  gint xm, ym, offs;
  gfloat mult;
      
  /* Get the input area. This is the bounding box of the selection in
   *  the image (or the entire image if there is no selection). Only
   *  operating on the input area is simply an optimization. It doesn't
   *  need to be done for correct operation. (It simply makes it go
   *  faster, since fewer pixels need to be operated on).
   */
  gimp_drawable_mask_bounds (drawable->id, &x1, &y1, &x2, &y2);

  /* Get the size of the input image. (This will/must be the same
   *  as the size of the output image.
   */
  width = drawable->width;
  height = drawable->height;
  bytes = drawable->bpp;
  
  /*  allocate row buffers  */
  cur_row  = (guchar *) malloc ((x2 - x1) * bytes);
  dest = (guchar *) malloc ((x2 - x1) * bytes);

  /*  initialize the pixel regions  */
  gimp_pixel_rgn_init (&srcPR, drawable, 0, 0, width, height, FALSE, FALSE);
  gimp_pixel_rgn_init (&destPR, drawable, 0, 0, width, height, TRUE, TRUE);

  mult=(gfloat)svals.depth*0.25;
  switch(svals.direction) 
    {
    case 0:
      xm=1;
      ym=128;
      offs=0;
      break;
    case 1:
      xm=-1;
      ym=128;
      offs=127;
      break;
    case 2:
      xm=128;
      ym=1;
      offs=0;
      break;
    case 3:
      xm=128;
      ym=-1;
      offs=128;
      break;
    default:
      xm=1;
      ym=128;
      offs=0;
      break;
    }

  /*  Loop through the rows */
  rrow = 0; rcol = 0;
  for (row = y1; row < y2; row++)
    {
      gimp_pixel_rgn_get_row (&srcPR, cur_row, x1, row, (x2-x1));
      d = dest;
      rcol = 0;
      switch (bytes) {
      case 1:  /* Grayscale */
      case 2:  /* Grayscale alpha */
	for (col = 0; col < (x2 - x1) * bytes; col+=bytes)
	  {
	    varde = cur_row[col] + mult*sdata[rcol*xm+rrow*ym+offs];
	    if (varde > 255 ) varde = 255;
	    if (varde < 0) varde = 0;
	    *d++ = (guchar)varde;
	    if (bytes == 2) 
	      *d++ = cur_row[col+1];
	    rcol++;
	    if (rcol == 128) rcol = 0;
	  }
	break;
      case 3:  /* RGB */
      case 4:  /* RGB alpha */
	for (col = 0; col < (x2 - x1) * bytes; col+=bytes)
	  {
	    varde = cur_row[col] + mult*sdata[rcol*xm+rrow*ym+offs];
	    if (varde > 255 ) varde = 255;
	    if (varde < 0) varde = 0;
	    *d++ = (guchar)varde;
	    varde = cur_row[col+1] + mult*sdata[rcol*xm+rrow*ym+offs];
	    if (varde > 255 ) varde = 255;
	    if (varde < 0) varde = 0;
	    *d++ = (guchar)varde;
	    varde = cur_row[col+2] + mult*sdata[rcol*xm+rrow*ym+offs];
	    if (varde > 255 ) varde = 255;
	    if (varde < 0) varde = 0;
	    *d++ = (guchar)varde;
	    if (bytes == 4)
	      *d++ = cur_row[col+3];
	    rcol++;
	    if (rcol == 128) rcol = 0;
	  }
	break;
      }
      /*  store the dest  */
      gimp_pixel_rgn_set_row (&destPR, dest, x1, row, (x2 - x1));

      rrow++;
      if (rrow == 128) rrow = 0;

      if ((row % 5) == 0)
	gimp_progress_update ((double) row / (double) (y2 - y1));
    }

  /*  update the textured region  */
  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->id, TRUE);
  gimp_drawable_update (drawable->id, x1, y1, (x2 - x1), (y2 - y1));

  free (cur_row);
  free (dest);
}
