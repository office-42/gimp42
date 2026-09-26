/*
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This is a plug-in for the GIMP.
 *
 * Blinds plug-in. Distort an image as though it was stuck to 
 * window blinds and the blinds where opened/closed.
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
 * 
 * Version 0.5 10 June 1997.
 * Changes required to work with 0.99.10.
 *
 * Version 0.4 20 May 1997.
 * Fixed problem with using this plugin in RUN_NONINTERACTIVE mode
 *
 * Version 0.3 8 May 1997.
 * Make preview work in Quartics words "The Right Way".
 *
 * Allow the background to be transparent.
 *
 * Version 0.2 1 May 1997 (not released).
 * Added patches supplied by Tim Mooney mooney@dogbert.cc.ndsu.NoDak.edu
 * to allow the plug-in to build with Digitals compiler.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#ifndef M_PI
#define M_PI    3.14159265358979323846
#endif /* M_PI */


/***** Magic numbers *****/

/* Don't make preview >255!!! It won't work for horizontal blinds */
#define PREVIEW_SIZE 128 
#define SCALE_WIDTH  150
#define ENTRY_WIDTH  30

/* Even more stuff from Quartics plugins */
#define CHECK_SIZE  8
#define CHECK_DARK  ((int) (1.0 / 3.0 * 255))
#define CHECK_LIGHT ((int) (2.0 / 3.0 * 255))

#define MAX_FANS 10

#define HORIZONTAL 0
#define VERTICAL 1

/* Variables set in dialog box */
typedef struct data {
    gint angledsp;
    gint numsegs;
    gint orientation;
    gint bg_trans;
} BlindVals;

typedef struct {
  GtkWidget *preview;
  guchar     preview_row[PREVIEW_SIZE * 4];
  gint run;
  guchar * pv_cache;
  gint img_bpp;
} BlindsInterface;

static BlindsInterface bint =
{
  NULL,  /* Preview */
  {'4','u'},    /* Preview_row */
  FALSE, /* run */
  NULL,
  4      /* bpp of drawable */
};

/* Array to hold each size of fans. And no there are not each the
 * same size (rounding errors...)
 */

int fanwidths[MAX_FANS];

GDrawable *blindsdrawable;

static void      query  (void);
static void      run    (gchar    *name,
			 gint      nparams,
			 GParam   *param,
			 gint     *nreturn_vals,
			 GParam  **return_vals);

static gint      blinds_dialog (void);
static void      blinds_close_callback (GtkWidget *widget, gpointer   data);
static void      blinds_ok_callback (GtkWidget *widget, gpointer   data);
static void      blinds_scale_update (GtkAdjustment *adjustment, gint  *size_val);
static void      blinds_entry_update(GtkWidget *widget, gint *value);
static void      blinds_toggle_update (GtkWidget *widget, gpointer   data);
static void      blinds_button_update (GtkWidget *widget, gpointer   data);
static void      dialog_update_preview(void);
static void	 cache_preview(void);
static void      apply_blinds(void);
static int       blinds_get_bg(guchar *bg);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

/* Values when first invoked */
static BlindVals bvals =
{
  30,
  3,
  HORIZONTAL,
  FALSE
};

/* Stuff for the preview bit */
static gint   sel_x1, sel_y1, sel_x2, sel_y2;
static gint   sel_width, sel_height;
static gint   preview_width, preview_height;
static gint   do_horizontal;
static gint   do_vertical;
static gint   do_trans;
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
    { PARAM_INT32, "angle_dsp", "Angle of Displacement " },
    { PARAM_INT32, "number_of_segments", "Number of segments in blinds" },
    { PARAM_INT32, "orientation", "orientation; 0 = Horizontal, 1 = Vertical" },
    { PARAM_INT32, "backgndg_trans", "background transparent; FALSE,TRUE" }
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0;

  gimp_install_procedure ("plug_in_blinds",
			  "Adds a blinds effect to the image. Rather like putting the image on a set of window blinds and the closing or opening the blinds",
			  "More here later",
			  "Andy Thomas",
			  "Andy Thomas",
			  "1997",
			  "<Image>/Filters/Distorts/Blinds",
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

  blindsdrawable = 
    drawable = 
    gimp_drawable_get (param[2].data.d_drawable);

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
      gimp_get_data ("plug_in_blinds", &bvals);
      if (! blinds_dialog())
	{
	  gimp_drawable_detach (drawable);
	  return;
	}
      break;

    case RUN_NONINTERACTIVE:
      if (nparams != 7)
	status = STATUS_CALLING_ERROR;
      if (status == STATUS_SUCCESS)
	{
	  bvals.angledsp = param[3].data.d_int32;
	  bvals.numsegs = param[4].data.d_int32;
	  bvals.orientation = param[5].data.d_int32;
	  bvals.bg_trans = param[6].data.d_int32;
	}
      break;

    case RUN_WITH_LAST_VALS:
      gimp_get_data ("plug_in_blinds", &bvals);
      break;

    default:
      break;
    }

  if (gimp_drawable_color (drawable->id) || gimp_drawable_gray (drawable->id))
    {
      gimp_progress_init ("Adding Blinds ...");

      apply_blinds();
   
      if (run_mode != RUN_NONINTERACTIVE)
	gimp_displays_flush ();

      if (run_mode == RUN_INTERACTIVE)
	gimp_set_data ("plug_in_blinds", &bvals, sizeof (BlindVals));
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
blinds_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *frame;
  GtkWidget *xframe;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *entry;
  GtkWidget *slider;
  GtkAdjustment *size_data;
  GtkWidget *toggle_vbox;
  GtkWidget *toggle;
  GtkWidget *orientation_group = NULL;
  char buf[256];


  do_horizontal = (bvals.orientation == HORIZONTAL);
  do_vertical = (bvals.orientation == VERTICAL);
  do_trans = (bvals.bg_trans == TRUE);

  gtk_init ();

  /* Get the stuff for the preview window...*/
  

  cache_preview(); /* Get the preview image and store it also set has_alpha */

  dlg = gimp_dialog_new ("Blinds");
  g_signal_connect (dlg, "destroy",
		      G_CALLBACK (blinds_close_callback),
		      NULL);

  /*  Action area  */
  button = gimp_dialog_add_button (dlg, "OK", NULL, NULL, TRUE);
  g_signal_connect (button, "clicked",
                      G_CALLBACK (blinds_ok_callback),
                      dlg);

  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			     G_CALLBACK (gtk_window_destroy), dlg);


  /* Start building the frame for the preview area */

  frame = gtk_frame_new ("preview");
  gimp_container_set_border_width (frame, 10);
  table = gimp_table_new (5, 5, FALSE); 
  gimp_container_set_border_width (table, 10); 
  gimp_container_add (frame, table); 
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);
  bint.preview = gimp_preview_new (GIMP_PREVIEW_COLOR);
  gimp_preview_size (GIMP_PREVIEW (bint.preview), preview_width, preview_height);
  xframe = gtk_frame_new(NULL);
  gimp_container_add (xframe, bint.preview);
  gimp_table_attach (table, xframe, 0, 1, 0, 2, GIMP_EXPAND , GIMP_EXPAND, 0, 0);


  frame = gtk_frame_new ("Orientation");
  gimp_table_attach (table, frame, 1, 2, 0, 1,
		    GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 5, 5);
  toggle_vbox = gimp_vbox_new (FALSE, 5);
  gimp_container_set_border_width (toggle_vbox, 5);
  gimp_container_add (frame, toggle_vbox);
  
  toggle = gimp_radio_button_new (orientation_group, "Horizontal");
  orientation_group = toggle;
  gimp_box_pack_start (toggle_vbox, toggle, FALSE, FALSE, 0);
  g_signal_connect (toggle, "toggled",
		      G_CALLBACK (blinds_toggle_update),
		      &do_horizontal);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), do_horizontal);

  toggle = gimp_radio_button_new (orientation_group, "Vertical");
  orientation_group = toggle;
  gimp_box_pack_start (toggle_vbox, toggle, FALSE, FALSE, 0);
  g_signal_connect (toggle, "toggled",
		      G_CALLBACK (blinds_toggle_update),
		      &do_vertical);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), do_vertical);

  
  frame = gtk_frame_new ("Background");
  gimp_table_attach (table, frame, 1, 2, 1, 2,
		    GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 5, 5);
  toggle_vbox = gimp_vbox_new (FALSE, 5);
  gimp_container_set_border_width (toggle_vbox, 5);
  gimp_container_add (frame, toggle_vbox);
  
  toggle = gtk_check_button_new_with_label ("Transparent");
  gimp_box_pack_start (toggle_vbox, toggle, FALSE, FALSE, 0);
  g_signal_connect (toggle, "toggled",
		      G_CALLBACK (blinds_button_update),
		      &do_trans);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), do_trans);

  if(!has_alpha)
    {
      gtk_widget_set_sensitive(toggle,FALSE);
      gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), FALSE);
    }



  frame = gtk_frame_new ("Parameter Settings");
  gimp_container_set_border_width (frame, 10);
  table = gimp_table_new (5, 5, FALSE);
  gimp_container_set_border_width (table, 10);
  gimp_container_add (frame, table);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);

  label = gtk_label_new ("Displacement ");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 1, 2, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 4, 0);


  size_data = gtk_adjustment_new (bvals.angledsp, 1, 90, 1, 1, 0);
  slider = gimp_hscale_new (size_data, 1);
  gtk_widget_set_size_request (slider, SCALE_WIDTH, -1);
  gimp_table_attach (table, slider, 2,3 , 1, 2, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);
  gtk_scale_set_value_pos (GTK_SCALE (slider), GTK_POS_LEFT);
  gtk_scale_set_digits (GTK_SCALE (slider), 0);
  g_signal_connect (size_data, "value-changed",
		      G_CALLBACK (blinds_scale_update),
		      &bvals.angledsp);


  entry = gtk_entry_new();
  g_object_set_data (G_OBJECT (entry), "user_data", size_data);
  g_object_set_data (G_OBJECT (size_data), "user_data", entry);
  gtk_widget_set_size_request (entry, ENTRY_WIDTH, -1);
  sprintf(buf, "%.2d", bvals.angledsp);
  gtk_editable_set_text (GTK_EDITABLE (entry), buf);
  g_signal_connect (entry, "changed",
		     G_CALLBACK (blinds_entry_update),
		     &bvals.angledsp);
  gimp_table_attach (table, entry, 3, 4, 1, 2, GIMP_FILL, GIMP_FILL, 4, 0);


  label = gtk_label_new ("Num Segments ");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 2, 3, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);

  size_data = gtk_adjustment_new (bvals.numsegs, 1, MAX_FANS, 2, 1, 0);
  slider = gimp_hscale_new (size_data, 1);
  gtk_widget_set_size_request (slider, SCALE_WIDTH, -1);
  gimp_table_attach (table, slider, 2, 3, 2, 3, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);
  gtk_scale_set_value_pos (GTK_SCALE (slider), GTK_POS_LEFT);
  gtk_scale_set_digits (GTK_SCALE (slider), 0);
  g_signal_connect (size_data, "value-changed",
		      G_CALLBACK (blinds_scale_update),
		      &bvals.numsegs);

  entry = gtk_entry_new();
  g_object_set_data (G_OBJECT (entry), "user_data", size_data);
  g_object_set_data (G_OBJECT (size_data), "user_data", entry);
  gtk_widget_set_size_request (entry, ENTRY_WIDTH, -1);
  sprintf(buf, "%d", bvals.numsegs);
  gtk_editable_set_text (GTK_EDITABLE (entry), buf);
  g_signal_connect (entry, "changed",
		     G_CALLBACK (blinds_entry_update),
		     &bvals.numsegs);
  gimp_table_attach (table, entry, 3, 4, 2, 3, GIMP_FILL, GIMP_FILL, 4, 0);


  gtk_window_present (GTK_WINDOW (dlg));
  dialog_update_preview();

  gimp_main_loop_run ();

  /*  determine orientation  */
  if (do_horizontal)
    bvals.orientation = HORIZONTAL;
  else
    bvals.orientation = VERTICAL;

  /* Use trans bg ? */
  bvals.bg_trans = do_trans;

  return bint.run;
}

static void
blinds_close_callback (GtkWidget *widget,
			 gpointer   data)
{
  gimp_main_loop_quit ();
}

static void
blinds_ok_callback (GtkWidget *widget,
		      gpointer   data)
{
  bint.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}


static void
blinds_toggle_update(GtkWidget *widget,
                      gpointer   data)
{
  int *toggle_val;

  toggle_val = (int *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
  {
    /* Only do for event that sets a toggle button to true */
    /* This will break if any more toggles are added? */
    *toggle_val = TRUE;
    dialog_update_preview();
  }
  else
    *toggle_val = FALSE;
}                  

static void
blinds_button_update(GtkWidget *widget,
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
  
  dialog_update_preview();

}                  


static void
blinds_scale_update(GtkAdjustment *adjustment, gint *value)
{
	GtkWidget *entry;
	char       buf[256];
	
	if (*value != gtk_adjustment_get_value (adjustment)) {
		*value = gtk_adjustment_get_value (adjustment);
		
		entry = g_object_get_data (G_OBJECT (adjustment), "user_data");
		sprintf(buf,"%d",*value);
		
		g_signal_handlers_block_matched (entry, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, value);
		gtk_editable_set_text (GTK_EDITABLE (entry), buf);
		g_signal_handlers_unblock_matched (entry, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, value);
		
		dialog_update_preview();
	}
} 

static void
blinds_entry_update(GtkWidget *widget, gint *value)
{
	GtkAdjustment *adjustment;
	gdouble        new_value;

	new_value = atoi(gtk_editable_get_text (GTK_EDITABLE (widget)));

	if (*value != new_value) {
		adjustment = g_object_get_data (G_OBJECT (widget), "user_data");

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
cache_preview (void)
{
  GPixelRgn src_rgn;
  int y,x;
  guchar *src_rows;
  guchar *p;
  int isgrey = 0;

  gimp_pixel_rgn_init(&src_rgn,blindsdrawable,sel_x1,sel_y1,sel_width,sel_height,FALSE,FALSE);

  src_rows = g_new(guchar ,sel_width*4); 
  p = bint.pv_cache = g_new(guchar ,preview_width*preview_height*4);

  bint.img_bpp = gimp_drawable_bpp(blindsdrawable->id);   

  has_alpha = gimp_drawable_has_alpha(blindsdrawable->id);

  if(bint.img_bpp < 3)
    {
      bint.img_bpp = 3 + has_alpha;
    }

  switch ( gimp_drawable_type (blindsdrawable->id) )
    {
    case GRAYA_IMAGE:
    case GRAY_IMAGE:
      isgrey = 1;
    default:
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
	p[x*bint.img_bpp+i] = src_rows[((x*sel_width)/preview_width)*src_rgn.bpp +((isgrey)?0:i)]; 
      if(has_alpha)
	p[x*bint.img_bpp+3] = src_rows[((x*sel_width)/preview_width)*src_rgn.bpp + ((isgrey)?1:3)];
    }
    p += (preview_width*bint.img_bpp);
  }
  g_free(src_rows);
}


static void 
blindsapply(guchar *srow,guchar *drow,gint width,gint bpp,guchar *bg)
{
  guchar *src;
  guchar *dst;
  int i,j,k;
  double ang;
  int available;

  /* Make the row 'shrink' around points along its length */
  /* The bvals.numsegs determins how many segments to slip it in to */
  /* The angle is the conceptual 'rotation' of each of these segments */

  /* Note the row is considered to be made up of a two dim array actual
   * pixel locations and the RGB colour at these locations.
   */

  /* In the process copy the src row to the destination row */

  /* Fill in with background color ? */
  for (i = 0 ; i < width ; i++)
    {
      dst = &drow[i*bpp];

      for (j = 0 ; j < bpp; j++)
	{
	  dst[j] = bg[j];
	}
    }

  /* Apply it */

  available = width;
  for ( i = 0 ; i < bvals.numsegs; i++)
  {

	/* Width of segs are variable */
	fanwidths[i] = available/(bvals.numsegs - i);
        available -= fanwidths[i];
  }

  /* do center points  first - just for fun...*/
  available = 0;
  for (k = 1 ; k <= bvals.numsegs; k++)
    {
      int point;

      point = available + fanwidths[k-1]/2;

      available += fanwidths[k-1];

      src = &srow[point*bpp];
      dst = &drow[point*bpp];
  
      /* Copy pixels across */
      
      for (j = 0 ; j < bpp; j++)
	{
	  dst[j] = src[j];
	}
    }

  /* Disp for each point */
  ang = (bvals.angledsp*2*M_PI)/360; /* Angle in rads */
  ang = (1-fabs(cos(ang)));

  available = 0;
  for(k = 0 ; k < bvals.numsegs; k++)
    {
      int dx; /* Amount to move by */
      int fw;

      for (i = 0 ; i < (fanwidths[k]/2) ; i++)
	{
	  /* Copy pixels across of left half of fan */
	  fw = fanwidths[k]/2;
	  dx = (int)(ang * ((double)(fw - (double)(i%fw))));

	  src = &srow[(available+i)*bpp];      
	  dst = &drow[(available+i+dx)*bpp];
	  
	  for (j = 0 ; j < bpp; j++)
	    {
	      dst[j] = src[j];
	    }

	  /* Right side */
	  j = i + 1;
	  src = &srow[(available + fanwidths[k] - j - (fanwidths[k]%2))*bpp];      
	  dst = &drow[(available + fanwidths[k] - j - (fanwidths[k]%2) - dx)*bpp];

	  for (j = 0 ; j < bpp; j++)
	    {
	      dst[j] = src[j];
	    }

	}

      available += fanwidths[k];
    }
}

static int
blinds_get_bg(guchar *bg)
{
  GParam *return_vals;
  gint nreturn_vals;
  /* Get the background color */
  int retval = FALSE; /*Return TRUE if of GREYA type */


  switch ( gimp_drawable_type (blindsdrawable->id) )
    {
    case RGBA_IMAGE:
      bg[3] = (do_trans == TRUE)?0:255;
      break;
    case RGB_IMAGE :

      return_vals = gimp_run_procedure ("gimp_palette_get_foreground",
					&nreturn_vals,
					PARAM_END);

      return_vals = gimp_run_procedure ("gimp_palette_get_background",
					&nreturn_vals,
					PARAM_END);

      if (return_vals[0].data.d_status == STATUS_SUCCESS)
	{
	  bg[0] = return_vals[1].data.d_color.red;
	  bg[1] = return_vals[1].data.d_color.green;
	  bg[2] = return_vals[1].data.d_color.blue;
	}
      else
	{
	  bg[0] = 0;
	  bg[1] = 0;
	  bg[2] = 0;
	}
      break;
    case GRAYA_IMAGE:
      retval = TRUE;
      bg[3] = (do_trans == TRUE)?0:255;
      break;
    case GRAY_IMAGE:
      bg[2] = 0;
      bg[1] = 0;
      bg[0] = 0;
      break;
    default:
      break;
    }

  return retval;
}

static void
dialog_update_preview(void)
{

  int     y;
  guchar *p;
  guchar bg[4];
  gint check,check_0,check_1;
  
  p = bint.pv_cache;

  blinds_get_bg(bg);

  if(do_vertical)
    {
      for (y = 0; y < preview_height; y++) {
	
	/* bint.preview_row - this row contains the row to apply the action to */
	
	blindsapply(p,bint.preview_row,preview_width,bint.img_bpp,bg);

	if ((y / CHECK_SIZE) & 1) {
	  check_0 = CHECK_DARK;
	  check_1 = CHECK_LIGHT;
	} else {
	  check_0 = CHECK_LIGHT;
	  check_1 = CHECK_DARK;
	}

	if(bint.img_bpp > 3)
	  {
	    int i,j;
	    for (i = 0, j = 0 ; i < sizeof(bint.preview_row); i += 4, j += 3 )
	      {
		gint alphaval;
		if (((i/4) / CHECK_SIZE) & 1)
		  check = check_0;
		else
		  check = check_1;

		alphaval = bint.preview_row[i + 3];

 		bint.preview_row[j] = 
		  check + (((bint.preview_row[i] - check)*alphaval)/255);
 		bint.preview_row[j + 1] = 
		  check + (((bint.preview_row[i + 1] - check)*alphaval)/255);
 		bint.preview_row[j + 2] = 
		  check + (((bint.preview_row[i + 2] - check)*alphaval)/255);
	      }
	  }
	
	gimp_preview_draw_row (GIMP_PREVIEW (bint.preview), bint.preview_row, 0, y, preview_width);
	
	p += preview_width*bint.img_bpp;
      } 
    }
  else
    {
      /* Horizontal blinds */
      /* Apply the blinds algo to a single column -
       * this act as a transfomation matrix for the 
       * rows. Make row 0 invalid so we can find it again!
       */
      int i;
      int loop1,loop2;
      guchar *sr = g_new(guchar,(preview_height)*4);
      guchar *dr = g_new(guchar,(preview_height)*4);
      guchar dummybg[4];
      /* Copy into here after translation*/
      guchar copy_row[PREVIEW_SIZE*4]; 

      memset(dummybg,0,4);
      memset(dr,0,(preview_height)*4); /* all dr rows are background rows */

      /* Fill in with background color ? */
      for (i = 0 ; i < preview_width ; i++)
	{
	  int j;
	  int bd = bint.img_bpp;
	  guchar *dst;
	  dst = &bint.preview_row[i*bd];
	  
	  for (j = 0 ; j < bd; j++)
	    {
	      dst[j] = bg[j];
	    }
	}

      for ( y = 0 ; y < preview_height; y++)
	{
	  sr[y] = y+1;
	}

      /* Bit of a fiddle since blindsapply really works on an image
       * row not a set of bytes. - preview can't be > 255
       * or must make dr sr int rows. 
       */
      blindsapply(sr,dr,preview_height,1,dummybg);

      for (y = 0; y < preview_height; y++) {

	if ((y / CHECK_SIZE) & 1) {
	  check_0 = CHECK_DARK;
	  check_1 = CHECK_LIGHT;
	} else {
	  check_0 = CHECK_LIGHT;
	  check_1 = CHECK_DARK;
	}

	if(dr[y] == 0)
	  {
	    /* Draw background line */
	    p = bint.preview_row;
	  }
	else
	  {
	    /* Draw line from src */
	    p = bint.pv_cache + (preview_width*bint.img_bpp*(dr[y]-1));
	  }

	if(bint.img_bpp > 3)
	  {
	    /* Take account of alpha channel HERE*/
	    for (loop1 = 0, loop2 = 0 ; 
		 loop1 < preview_width*bint.img_bpp; 
		 loop1 += 4, loop2 += 3)
	      {
		gint alphaval;
		if (((loop1/4) / CHECK_SIZE) & 1)
		  check = check_0;
		else
		  check = check_1;
		
		alphaval = p[loop1 + 3];
		
		copy_row[loop2] = 
		  check + (((p[loop1] - check)*alphaval)/255);
		copy_row[loop2 + 1] = 
		  check + (((p[loop1 + 1] - check)*alphaval)/255);
		copy_row[loop2 + 2] = 
		  check + (((p[loop1 + 2] - check)*alphaval)/255);
	      }
	    p = &copy_row[0];
	  }
	gimp_preview_draw_row (GIMP_PREVIEW (bint.preview), p, 0, y, preview_width);
      } 
      g_free(sr);
      g_free(dr);
    }
  
  gtk_widget_queue_draw (bint.preview);
}

/* STEP tells us how many rows/columns to gulp down in one go... */
/* Note all the "4" literals around here are to do with the depths
 * of the images. Makes it easier to deal with for my small brain.
 */

#define STEP 40

static void
apply_blinds(void)
{
  GPixelRgn des_rgn;
  GPixelRgn src_rgn;
  guchar *src_rows,*des_rows;
  int x,y;
  guchar bg[4];

  /* Adjust aplha channel if GREYA */
  if(blinds_get_bg(bg) == TRUE)
    bg[1] = (do_trans == TRUE)?0:255;

  gimp_pixel_rgn_init(&src_rgn,blindsdrawable,sel_x1,sel_y1,sel_width,sel_height,FALSE,FALSE);
  gimp_pixel_rgn_init(&des_rgn,blindsdrawable,sel_x1,sel_y1,sel_width,sel_height,TRUE,TRUE);

  src_rows = g_new(guchar ,(MAX(sel_width,sel_height))*4*STEP); 
  des_rows = g_new(guchar ,(MAX(sel_width,sel_height))*4*STEP); 

  if(do_vertical)
    {

      for (y = 0; y < sel_height; y += STEP) 
	{
	  int rr;
	  int step;
	  
	  if((y + STEP) > sel_height)
	    step = sel_height - y;
	  else
	    step = STEP;
	  
	  gimp_pixel_rgn_get_rect(&src_rgn,
				  src_rows,
				  sel_x1,
				  sel_y1 + y,
				  sel_width,
				  step);
	  
	  /* OK I could make this better */
	  for(rr = 0 ; rr < STEP; rr++)
	    blindsapply(src_rows+(sel_width*rr*src_rgn.bpp),
			des_rows+(sel_width*rr*src_rgn.bpp),
			sel_width,src_rgn.bpp,bg);
	  
	  gimp_pixel_rgn_set_rect(&des_rgn,
				  des_rows,
				  sel_x1,
				  sel_y1 + y,
				  sel_width,
				  step);
	  
	  gimp_progress_update((double)y/(double)sel_height);
	}
    }
  else
    {
      /* Horizontal blinds */
      /* Apply the blinds algo to a single column -
       * this act as a transfomation matrix for the 
       * rows. Make row 0 invalid so we can find it again!
       */
      int i;
      gint *sr = g_new(gint,(sel_height)*4);
      gint *dr = g_new(gint,(sel_height)*4);
      guchar *dst = g_new(guchar,STEP*4);
      guchar dummybg[4];

      memset(dummybg,0,4);
      memset(dr,0,(sel_height)*4); /* all dr rows are background rows */
      for ( y = 0 ; y < sel_height; y++)
	{
	  sr[y] = y+1;
	}

      /* Hmmm. does this work portably? */
      /* This "swaps the intergers around that are held in in the
       * sr & dr arrays. 
       */
      blindsapply((guchar*)sr,(guchar*)dr,sel_height,sizeof(gint),dummybg);

      /* Fill in with background color ? */
      for (i = 0 ; i < STEP ; i++)
	{
	  int j;
	  guchar *bgdst;
	  bgdst = &dst[i*src_rgn.bpp];
	  
	  for (j = 0 ; j < src_rgn.bpp; j++)
	    {
	      bgdst[j] = bg[j];
	    }
	}

      for (x = 0; x < sel_width; x += STEP) 
	{
	  int rr;
	  int step;
	  guchar *p;
	  
	  if((x + STEP) > sel_width)
	    step = sel_width - x;
	  else
	    step = STEP;
	  
	  gimp_pixel_rgn_get_rect(&src_rgn,
				  src_rows,
				  sel_x1 + x,
				  sel_y1,
				  step,
				  sel_height);
	  
	  /* OK I could make this better */
	  for(rr = 0 ; rr < sel_height; rr++)
	    {
		if(dr[rr] == 0)
	  	{
	    	   /* Draw background line */
	    	   p = dst;
	  	}
		else
	  	{
	    	   /* Draw line from src */
	    	   p = src_rows + (step*src_rgn.bpp*(dr[rr]-1));
	  	}
		memcpy(des_rows+(rr*step*src_rgn.bpp),p,step*src_rgn.bpp);
	    }

	  gimp_pixel_rgn_set_rect(&des_rgn,
				  des_rows,
				  sel_x1 + x,
				  sel_y1,
				  step,
				  sel_height);
	  
	  gimp_progress_update((double)x/(double)sel_width);
	}

      g_free(dst);
      g_free(sr);
      g_free(dr);
    }

  g_free(src_rows);
  g_free(des_rows);

  gimp_drawable_flush(blindsdrawable);
  gimp_drawable_merge_shadow(blindsdrawable->id, TRUE);
  gimp_drawable_update(blindsdrawable->id, sel_x1, sel_y1, sel_width, sel_height);  
  
}

