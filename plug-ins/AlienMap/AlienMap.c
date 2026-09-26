/**********************************************************************
 *  AlienMap (Co-)sine color transformation plug-in (Version 1.01)
 *  Daniel Cotting (cotting@mygale.org)
 **********************************************************************
 *  Official Homepage: http://www.mygale.org/~cotting
 **********************************************************************
 *  Homepages under construction: http://www.chez.com/cotting
 *                                http://www.cyberbrain.com/cotting
 *  You won't be able to see anything yet, as I don't really have the 
 *  time to build up these two sites :-( 
 *  Have a look at www.mygale.org/~cotting instead!
 **********************************************************************    
 */

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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"
#include "logo.h"

/***** Macros *****/

#define ALIEN_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define ALIEN_MAX(a, b) (((a) > (b)) ? (a) : (b))


/***** Magic numbers *****/

#define PREVIEW_SIZE 128
#define SCALE_WIDTH  200
#define ENTRY_WIDTH  45

#define SINUS 0
#define COSINUS 1
#define NONE 2

/***** Types *****/
typedef struct {
        gdouble redstretch;
        gdouble greenstretch;
        gdouble bluestretch;
        gint    redmode;
        gint    greenmode;
        gint    bluemode;
} alienmap_vals_t;

typedef struct {
        GtkWidget *preview;
        guchar    *image;
        guchar    *wimage;
        gint run;
} alienmap_interface_t;



/* Declare local functions. */

static void      query  (void);
static void      run    (char      *name,
        		 int        nparams,
        		 GParam    *param,
        		 int       *nreturn_vals,
        		 GParam   **return_vals);

static void      alienmap 	    (GDrawable  *drawable);
static void      alienmap_render_row  (const guchar *src_row,
        			     guchar *dest_row,
        			     gint row,
        			     gint row_width,
        			     gint bytes, double, double, double);
static void      alienmap_get_pixel(int x, int y, guchar *pixel);
void    	 transform           (short int *, short int *, short int *,double, double, double);


static void      build_preview_source_image(void);

static gint      alienmap_dialog(void);
static void      dialog_update_preview(void);
static void      dialog_create_value(char *title, GtkWidget *table, int row, gdouble *value,
        			     int left, int right, const char *desc);
static void      dialog_scale_update(GtkAdjustment *adjustment, gdouble *value);
static void      dialog_entry_update(GtkWidget *widget, gdouble *value);
static void      dialog_close_callback(GtkWidget *widget, gpointer data);
static void      dialog_ok_callback(GtkWidget *widget, gpointer data);
static void      dialog_cancel_callback(GtkWidget *widget, gpointer data);
static void      alienmap_toggle_update    (GtkWidget *widget,
        				    gpointer   data);
void alienmap_logo_dialog (void);



					    
					    
/***** Variables *****/

GtkWidget *maindlg;
GtkWidget *logodlg;

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

static alienmap_interface_t wint = {
        NULL,  /* preview */
        NULL,  /* image */
        NULL,  /* wimage */
        FALSE  /* run */
}; /* wint */

static alienmap_vals_t wvals = {
        128,128,128,COSINUS,SINUS,SINUS,
}; /* wvals */

static GDrawable *drawable;
static gint   tile_width, tile_height;
static gint   img_width, img_height, img_bpp;
static gint   sel_x1, sel_y1, sel_x2, sel_y2;
static gint   sel_width, sel_height;
static gint   preview_width, preview_height;
static GTile *the_tile = NULL;
static double cen_x, cen_y;
static double scale_x, scale_y;

gint do_redsinus;
gint do_redcosinus;
gint do_rednone;

gint do_greensinus;
gint do_greencosinus;
gint do_greennone;

gint do_bluesinus;
gint do_bluecosinus;
gint do_bluenone;
/***** Functions *****/


MAIN ()

static void
query ()
{
  static GParamDef args[] =
  {
    { PARAM_INT32,    "run_mode",     "Interactive, non-interactive" },
    { PARAM_IMAGE,    "image",        "Input image" },
    { PARAM_DRAWABLE, "drawable",     "Input drawable" },
    { PARAM_INT8,    "redstretch",   "Red component stretching factor (0-128)" },
    { PARAM_INT8,    "greenstretch", "Green component stretching factor (0-128)" },
    { PARAM_INT8,    "bluestretch",  "Blue component stretching factor (0-128)" },
    { PARAM_INT8,    "redmode",      "Red application mode (0:SIN;1:COS;2:NONE)" },
    { PARAM_INT8,    "greenmode",    "Green application mode (0:SIN;1:COS;2:NONE)" },
    { PARAM_INT8,    "bluemode",     "Blue application mode (0:SIN;1:COS;2:NONE)" },
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0;

  gimp_install_procedure ("plug_in_alienmap",
        		  "AlienMap Color Transformation Plug-In",
        		  "No help yet. Just try it and you'll see!",
        		  "Daniel Cotting (cotting@mygale.org, http://www.mygale.org/~cotting)",
        		  "Daniel Cotting (cotting@mygale.org, http://www.mygale.org/~cotting)",
        		  "1th May 1997",
        		  "<Image>/Filters/Colors/Alien Map",
        		  "RGB*",
        		  PROC_PLUG_IN,
        		  nargs, nreturn_vals,
        		  args, return_vals);
}



void
transform  (short int *r,
            short int *g,
            short int *b, double redstretch, double greenstretch, double bluestretch)
{
  int red, green, blue;
  double pi=atan(1)*4;
  red = *r;
  green = *g;
  blue = *b;
  switch (wvals.redmode)
  {
    case SINUS:
       red    = (int) redstretch*(1.0+sin((red/128.0-1)*pi));
       break;
    case COSINUS:
       red    = (int) redstretch*(1.0+cos((red/128.0-1)*pi));
       break;
    default:
    break;
   }

  switch (wvals.greenmode)
  {
    case SINUS:
       green    = (int) greenstretch*(1.0+sin((green/128.0-1)*pi));
       break;
    case COSINUS:
       green    = (int) greenstretch*(1.0+cos((green/128.0-1)*pi));
       break;
    default:
    break;
   }

  switch (wvals.bluemode)
  {
    case SINUS:
       blue    = (int) bluestretch*(1.0+sin((blue/128.0-1)*pi));
       break;
    case COSINUS:
       blue    = (int) bluestretch*(1.0+cos((blue/128.0-1)*pi));
       break;
    default:
    break;
   }
   
   if (red== 256) {
               red= 255;}
   if (green== 256) {
          green= 255;}
   if (blue== 256) {blue= 255;}
  *r = red;
  *g = green;
  *b = blue;
}


static void
run (char    *name,
     int      nparams,
     GParam  *param,
     int     *nreturn_vals,
     GParam **return_vals)
{
  static GParam values[1];
/*   GDrawable *drawable; */
/*   gint32 image_ID; */
  GRunModeType  run_mode;
  double        xhsiz, yhsiz;
  int   	pwidth, pheight;
  GStatusType status = STATUS_SUCCESS;


  run_mode = param[0].data.d_int32;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;

  *nreturn_vals = 1;
  *return_vals = values;



  /*  Get the specified drawable  */
  drawable = gimp_drawable_get (param[2].data.d_drawable);
/*   image_ID = param[1].data.d_image; */
  tile_width  = gimp_tile_width();
  tile_height = gimp_tile_height();

  img_width  = gimp_drawable_width(drawable->id);
  img_height = gimp_drawable_height(drawable->id);
  img_bpp    = gimp_drawable_bpp(drawable->id);

  gimp_drawable_mask_bounds(drawable->id, &sel_x1, &sel_y1, &sel_x2, &sel_y2);

  sel_width  = sel_x2 - sel_x1;
  sel_height = sel_y2 - sel_y1;

  cen_x = (double) (sel_x2 - 1 + sel_x1) / 2.0;
  cen_y = (double) (sel_y2 - 1 + sel_y1) / 2.0;

  xhsiz = (double) (sel_width - 1) / 2.0;
  yhsiz = (double) (sel_height - 1) / 2.0;

        if (xhsiz < yhsiz) {
        	scale_x = yhsiz / xhsiz;
        	scale_y = 1.0;
        } else if (xhsiz > yhsiz) {
        	scale_x = 1.0;
        	scale_y = xhsiz / yhsiz;
        } else {
        	scale_x = 1.0;
        	scale_y = 1.0;
        } /* else */

        /* Calculate preview size */
        if (sel_width > sel_height) {
        	pwidth  = ALIEN_MIN(sel_width, PREVIEW_SIZE);
        	pheight = sel_height * pwidth / sel_width;
        } else {
        	pheight = ALIEN_MIN(sel_height, PREVIEW_SIZE);
        	pwidth  = sel_width * pheight / sel_height;
        } /* else */

        preview_width  = ALIEN_MAX(pwidth, 2);  /* Min size is 2 */
        preview_height = ALIEN_MAX(pheight, 2);

        /* See how we will run */
        switch (run_mode) {
        	case RUN_INTERACTIVE:
        		/* Possibly retrieve data */

        		gimp_get_data("plug_in_alienmap", &wvals);

        		/* Get information from the dialog */

        		if (!alienmap_dialog())
        			return;

        		break;

        	case RUN_NONINTERACTIVE:
        		/* Make sure all the arguments are present */

        		if (nparams != 9)
        			status = STATUS_CALLING_ERROR;

        		if (status == STATUS_SUCCESS)

        			wvals.redstretch = param[3].data.d_int8;
        			wvals.greenstretch = param[4].data.d_int8;
        			wvals.bluestretch = param[5].data.d_int8;
        			wvals.redmode = param[6].data.d_int8;
        			wvals.greenmode = param[7].data.d_int8;
        			wvals.bluemode = param[8].data.d_int8;


        		break;

        	case RUN_WITH_LAST_VALS:
        		/* Possibly retrieve data */

        		gimp_get_data("plug_in_alienmap", &wvals);
        		break;

        	default:
        		break;
        } /* switch */


  if (status == STATUS_SUCCESS)
    {
      /*  Make sure that the drawable is indexed or RGB color  */
      if (gimp_drawable_color (drawable->id))
        {
          gimp_progress_init ("AlienMap: Transforming ...");

        	/* Set the tile cache size */

        	gimp_tile_cache_ntiles(2*(drawable->width / gimp_tile_width()+1));

        	/* Run! */


/*          gimp_tile_cache_ntiles (2 * (drawable->width / gimp_tile_width ()
        			       + 1));*/
          alienmap (drawable);
        	if (run_mode != RUN_NONINTERACTIVE)
        		gimp_displays_flush();

        	/* Store data */

        	if (run_mode == RUN_INTERACTIVE)
        		gimp_set_data("plug_in_alienmap", &wvals, sizeof(alienmap_vals_t));

        }
      else
        {
/*           gimp_message("This filter only applies on RGB-images"); */
          status = STATUS_EXECUTION_ERROR;
        }
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

/*****/

static void
alienmap_get_pixel(int x, int y, guchar *pixel)
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
        } /* if */

        newcol    = x / tile_width; /* The compiler should optimize this */
        newcoloff = x % tile_width;
        newrow    = y / tile_height;
        newrowoff = y % tile_height;

        if ((col != newcol) || (row != newrow) || (the_tile == NULL)) {

        	if (the_tile != NULL)
        		gimp_tile_unref(the_tile, FALSE);

        	the_tile = gimp_drawable_get_tile(drawable, FALSE, newrow, newcol);
        	gimp_tile_ref(the_tile);
        	col = newcol;
        	row = newrow;
        } /* if */
        p = the_tile->data + the_tile->bpp * (the_tile->ewidth * newrowoff + newcoloff);
        for (i = img_bpp; i; i--)
        	*pixel++ = *p++;

} /* alienmap_get_pixel */



static void
alienmap_render_row (const guchar *src_row,
        	  guchar *dest_row,
        	  gint row,
        	  gint row_width,
        	  gint bytes, double redstretch, double greenstretch, double bluestretch)




{
  gint col, bytenum;

  for (col = 0; col < row_width ; col++)
    {
      short int v1, v2, v3;

      v1 = (short int)src_row[col*bytes];
      v2 = (short int)src_row[col*bytes +1];
      v3 = (short int)src_row[col*bytes +2];

      transform(&v1, &v2, &v3, redstretch, greenstretch, bluestretch);

      dest_row[col*bytes] = (int)v1;
      dest_row[col*bytes +1] = (int)v2;
      dest_row[col*bytes +2] = (int)v3;

      if (bytes>3)
        for (bytenum = 3; bytenum<bytes; bytenum++)
          {
            dest_row[col*bytes+bytenum] = src_row[col*bytes+bytenum];
          }
    }
}





static void
alienmap (GDrawable *drawable)
{
  GPixelRgn srcPR, destPR;
  gint width, height;
  gint bytes;
  guchar *src_row;
  guchar *dest_row;
  gint row;
  gint x1, y1, x2, y2;
  double redstretch,greenstretch,bluestretch;

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
  src_row = (guchar *) malloc ((x2 - x1) * bytes);
  dest_row = (guchar *) malloc ((x2 - x1) * bytes);


  /*  initialize the pixel regions  */
  gimp_pixel_rgn_init (&srcPR, drawable, 0, 0, width, height, FALSE, FALSE);
  gimp_pixel_rgn_init (&destPR, drawable, 0, 0, width, height, TRUE, TRUE);


  redstretch = wvals.redstretch;
  greenstretch = wvals.greenstretch;
  bluestretch = wvals.bluestretch;

  for (row = y1; row < y2; row++)

    {
      gimp_pixel_rgn_get_row (&srcPR, src_row, x1, row, (x2 - x1));

      alienmap_render_row (src_row,
        		dest_row,
        		row,
        		(x2 - x1),
        		bytes,
        		redstretch, greenstretch, bluestretch);

      /*  store the dest  */
      gimp_pixel_rgn_set_row (&destPR, dest_row, x1, row, (x2 - x1));

      if ((row % 10) == 0)
        gimp_progress_update ((double) row / (double) (y2 - y1));
    }

  /*  update the processed region  */
  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->id, TRUE);
  gimp_drawable_update (drawable->id, x1, y1, (x2 - x1), (y2 - y1));

  free (src_row);
  free (dest_row);
}

/*****/

static void
build_preview_source_image(void)
{
        double  left, right, bottom, top;
        double  px, py;
        double  dx, dy;
        int     x, y;
        guchar *p;
        guchar  pixel[4];

        wint.image  = g_malloc(preview_width * preview_height * 3 * sizeof(guchar));
        wint.wimage = g_malloc(preview_width * preview_height * 3 * sizeof(guchar));

        left   = sel_x1;
        right  = sel_x2 - 1;
        bottom = sel_y2 - 1;
        top    = sel_y1;

        dx = (right - left) / (preview_width - 1);
        dy = (bottom - top) / (preview_height - 1);

        py = top;

        p = wint.image;

        for (y = 0; y < preview_height; y++) {
        	px = left;
        	for (x = 0; x < preview_width; x++) {
        		alienmap_get_pixel((int) px, (int) py, pixel);

        		*p++ = pixel[0];
        		*p++ = pixel[1];
        		*p++ = pixel[2];

        		px += dx;
        	} /* for */

        	py += dy;
        } /* for */
} /* build_preview_source_image */


static void
set_tooltip (GtkWidget *widget, const char *desc)
{
  if (desc && desc[0])
    gtk_widget_set_tooltip_text (widget, desc);
}


/*  Adds one radio button of a mode group to vbox.  */
static GtkWidget *
alienmap_add_radio (GtkWidget  *vbox,
		    GtkWidget  *group,
		    const char *label,
		    int        *value,
		    const char *desc)
{
  GtkWidget *toggle;

  toggle = gimp_radio_button_new (group, label);
  gimp_box_pack_start (vbox, toggle, FALSE, FALSE, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (alienmap_toggle_update),
		    value);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), *value);
  set_tooltip (toggle, desc);

  return toggle;
}


/*****/

static gint
alienmap_dialog(void)
{
        GtkWidget  *dialog;
        GtkWidget  *top_table;
        GtkWidget  *frame;
        GtkWidget  *toggle;
        GtkWidget  *toggle_vbox;
        GtkWidget  *table, *table2, *table3;
        GtkWidget  *button;
        do_redsinus = (wvals.redmode == SINUS);
        do_redcosinus = (wvals.redmode == COSINUS);
        do_rednone = (wvals.redmode == NONE);
        do_greensinus = (wvals.greenmode == SINUS);
        do_greencosinus = (wvals.greenmode == COSINUS);
        do_greennone = (wvals.greenmode == NONE);
        do_bluesinus = (wvals.bluemode == SINUS);
        do_bluecosinus = (wvals.bluemode == COSINUS);
        do_bluenone = (wvals.bluemode == NONE);

        gtk_init();

        build_preview_source_image();
        dialog = maindlg = gimp_dialog_new("AlienMap");
        g_signal_connect(dialog, "destroy",
        		 G_CALLBACK(dialog_close_callback),
        		 NULL);

        top_table = gimp_table_new(4, 4, FALSE);
        gimp_container_set_border_width(top_table, 6);
        gtk_grid_set_row_spacing(GTK_GRID(top_table), 4);
        gimp_box_pack_start(gimp_dialog_get_vbox(dialog), top_table, FALSE, FALSE, 0);

        /* Preview */

        frame = gtk_frame_new(NULL);
        gimp_table_attach(top_table, frame, 0, 1, 0, 1, 0, 0, 0, 0);

        wint.preview = gimp_preview_new(GIMP_PREVIEW_COLOR);
        gimp_preview_size(GIMP_PREVIEW(wint.preview), preview_width, preview_height);
        gtk_frame_set_child(GTK_FRAME(frame), wint.preview);
        /* Controls */

        table = gimp_table_new(1, 3, FALSE);
        gimp_table_attach(top_table, table, 0, 4, 1, 2, GIMP_EXPAND | GIMP_FILL, 0, 0, 0);

        dialog_create_value("R", table, 0, &wvals.redstretch,0,128.00000000000, "Change intensity of the red channel");


        table2 = gimp_table_new(1, 3, FALSE);
        gimp_table_attach(top_table, table2, 0, 4, 2, 3, GIMP_EXPAND | GIMP_FILL, 0, 0, 0);

        dialog_create_value("G", table2, 0, &wvals.greenstretch,0,128.0000000000000, "Change intensity of the green channel");


        table3 = gimp_table_new(1, 3, FALSE);
        gimp_table_attach(top_table, table3, 0, 4, 3, 4, GIMP_EXPAND | GIMP_FILL, 0, 0, 0);

        dialog_create_value("B", table3, 0, &wvals.bluestretch,0,128.00000000000000, "Change intensity of the blue channel");

/*  Redmode toggle box  */
    frame = gtk_frame_new ("Red:");
    gimp_table_attach (top_table, frame, 1, 2, 0, 1, GIMP_FILL | GIMP_EXPAND, GIMP_FILL | GIMP_EXPAND, 5, 5);
    toggle_vbox = gimp_vbox_new (FALSE, 5);
    gimp_container_set_border_width (toggle_vbox, 5);
    gtk_frame_set_child (GTK_FRAME (frame), toggle_vbox);

    toggle = alienmap_add_radio (toggle_vbox, NULL, "Sine", &do_redsinus,
				 "Use sine-function for red component");
    toggle = alienmap_add_radio (toggle_vbox, toggle, "Cosine", &do_redcosinus,
				 "Use cosine-function for red component");
    toggle = alienmap_add_radio (toggle_vbox, toggle, "None", &do_rednone,
				 "Red channel: use linear mapping instead of any trigonometrical function");


/*  Greenmode toggle box  */
    frame = gtk_frame_new ("Green:");
    gimp_table_attach (top_table, frame, 2, 3, 0, 1, GIMP_FILL | GIMP_EXPAND, GIMP_FILL | GIMP_EXPAND, 5, 5);
    toggle_vbox = gimp_vbox_new (FALSE, 5);
    gimp_container_set_border_width (toggle_vbox, 5);
    gtk_frame_set_child (GTK_FRAME (frame), toggle_vbox);

    toggle = alienmap_add_radio (toggle_vbox, NULL, "Sine", &do_greensinus,
				 "Use sine-function for green component");
    toggle = alienmap_add_radio (toggle_vbox, toggle, "Cosine", &do_greencosinus,
				 "Use cosine-function for green component");
    toggle = alienmap_add_radio (toggle_vbox, toggle, "None", &do_greennone,
				 "Green channel: use linear mapping instead of any trigonometrical function");


/*  Bluemode toggle box  */
    frame = gtk_frame_new ("Blue:");
    gimp_table_attach (top_table, frame, 3, 4, 0, 1, GIMP_FILL | GIMP_EXPAND, GIMP_FILL | GIMP_EXPAND, 5, 5);
    toggle_vbox = gimp_vbox_new (FALSE, 5);
    gimp_container_set_border_width (toggle_vbox, 5);
    gtk_frame_set_child (GTK_FRAME (frame), toggle_vbox);

    toggle = alienmap_add_radio (toggle_vbox, NULL, "Sine", &do_bluesinus,
				 "Use sine-function for blue component");
    toggle = alienmap_add_radio (toggle_vbox, toggle, "Cosine", &do_bluecosinus,
				 "Use cosine-function for blue component");
    alienmap_add_radio (toggle_vbox, toggle, "None", &do_bluenone,
			"Blue channel: use linear mapping instead of any trigonometrical function");


        /* Buttons */

        gimp_container_set_border_width(gimp_dialog_get_action_area(dialog), 6);

        button = gimp_dialog_add_button(dialog, "OK",
        				G_CALLBACK(dialog_ok_callback),
        				dialog, TRUE);
        set_tooltip(button,"Accept settings and apply filter on image");

        button = gimp_dialog_add_button(dialog, "Cancel",
        				G_CALLBACK(dialog_cancel_callback),
        				dialog, FALSE);
        set_tooltip(button,"Reject any changes and close plug-in");

        button = gimp_dialog_add_button(dialog, "About...", NULL, NULL, FALSE);
        g_signal_connect_swapped(button, "clicked",
        			 G_CALLBACK(alienmap_logo_dialog), NULL);
        set_tooltip(button,"Show information about this plug-in and the author");


        /* Done */

        gtk_window_present(GTK_WINDOW(dialog));
        dialog_update_preview();

        gimp_main_loop_run();
        if (the_tile != NULL) {
        	gimp_tile_unref(the_tile, FALSE);
        	the_tile = NULL;
        } /* if */

        g_free(wint.image);
        g_free(wint.wimage);

        return wint.run;
} /* alienmap_dialog */


/*****/

static void
dialog_update_preview(void)
{
        double  left, right, bottom, top;
        double  dx, dy;
        int  px, py;
        int     x, y;
        double  redstretch, greenstretch, bluestretch;
        short int r,g,b;
        double  scale_x, scale_y;
        guchar *p_ul, *i, *p;

        left   = sel_x1;
        right  = sel_x2 - 1;
        bottom = sel_y2 - 1;
        top    = sel_y1;
        dx = (right - left) / (preview_width - 1);
        dy = (bottom - top) / (preview_height - 1);

        redstretch = wvals.redstretch;
        greenstretch = wvals.greenstretch;
        bluestretch = wvals.bluestretch;

        scale_x = (double) (preview_width - 1) / (right - left);
        scale_y = (double) (preview_height - 1) / (bottom - top);

        py = 0;

        p_ul = wint.wimage;

        for (y = 0; y < preview_height; y++) {
        	px = 0;

        	for (x = 0; x < preview_width; x++) {
        	       i = wint.image + 3 * (preview_width * py + px);
        	       r = *i++;
        	       g = *i++;
        	       b = *i;
        	       transform(&r,&g,&b,redstretch, greenstretch, bluestretch);
        	       p_ul[0] = r;
        	       p_ul[1] = g;
        	       p_ul[2] = b;
        	       p_ul += 3;
        	       px += 1; /* dx; */
        	} /* for */
        	py +=1; /* dy; */
        } /* for */

        p = wint.wimage;

        for (y = 0; y < preview_height; y++) {
        	gimp_preview_draw_row(GIMP_PREVIEW(wint.preview), p, 0, y, preview_width);
        	p += preview_width * 3;
        } /* for */
} /* dialog_update_preview */


/*****/

static void
dialog_create_value(char *title, GtkWidget *table, int row, gdouble *value,
        	    int left, int right, const char *desc)
{
        GtkWidget     *label;
        GtkWidget     *scale;
        GtkWidget     *entry;
        GtkAdjustment *scale_data;
        char           buf[256];

        label = gtk_label_new(title);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gimp_table_attach(table, label, 0, 1, row, row + 1, GIMP_FILL, GIMP_FILL, 4, 0);


        scale_data = gtk_adjustment_new(*value, left, right,
        				(right - left) / 128,
        				(right - left) / 128,
        				0);

        g_signal_connect(scale_data, "value-changed",
        		 G_CALLBACK(dialog_scale_update),
        		 value);

        scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, scale_data);
        gtk_widget_set_size_request(scale, SCALE_WIDTH, -1);
        gimp_table_attach(table, scale, 1, 2, row, row + 1, GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 0, 0);
        gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
        gtk_scale_set_digits(GTK_SCALE(scale), 3);
        set_tooltip(scale,desc);

        entry = gtk_entry_new();
        g_object_set_data(G_OBJECT(entry), "user_data", scale_data);
        g_object_set_data(G_OBJECT(scale_data), "user_data", entry);
        gtk_widget_set_size_request(entry, ENTRY_WIDTH, -1);
        sprintf(buf, "%0.2f", *value);
        gtk_editable_set_text(GTK_EDITABLE(entry), buf);
        g_signal_connect(entry, "changed",
        		 G_CALLBACK(dialog_entry_update),
        		 value);
        gimp_table_attach(table, entry, 2, 3, row, row + 1, GIMP_FILL, GIMP_FILL, 4, 0);
	set_tooltip(entry,desc);

} /* dialog_create_value */

/*****/

static void
dialog_scale_update(GtkAdjustment *adjustment, gdouble *value)
{
        GtkWidget *entry;
        char       buf[256];

        if (*value != gtk_adjustment_get_value(adjustment)) {
        	*value = gtk_adjustment_get_value(adjustment);

        	entry = g_object_get_data(G_OBJECT(adjustment), "user_data");
        	sprintf(buf, "%0.2f", *value);

        	g_signal_handlers_block_matched(entry, G_SIGNAL_MATCH_DATA,
        					0, 0, NULL, NULL, value);
        	gtk_editable_set_text(GTK_EDITABLE(entry), buf);
        	g_signal_handlers_unblock_matched(entry, G_SIGNAL_MATCH_DATA,
        					  0, 0, NULL, NULL, value);

        	dialog_update_preview();
        } /* if */
} /* dialog_scale_update */
/*****/

static void
dialog_entry_update(GtkWidget *widget, gdouble *value)
{
        GtkAdjustment *adjustment;
        gdouble        new_value;

        new_value = atof(gtk_editable_get_text(GTK_EDITABLE(widget)));

        if (*value != new_value) {
        	adjustment = g_object_get_data(G_OBJECT(widget), "user_data");

        	if ((new_value >= gtk_adjustment_get_lower(adjustment)) &&
        	    (new_value <= gtk_adjustment_get_upper(adjustment))) {
        		*value  	  = new_value;
        		gtk_adjustment_set_value(adjustment, new_value);

        		dialog_update_preview();
        	} /* if */
        } /* if */
} /* dialog_entry_update */


static void
dialog_close_callback(GtkWidget *widget, gpointer data)
{
        /* was gtk_quit_add_destroy (1, logodlg) */
        if (logodlg)
        	gtk_window_destroy(GTK_WINDOW(logodlg));
        gimp_main_loop_quit();
} /* dialog_close_callback */


/*****/

static void
dialog_ok_callback(GtkWidget *widget, gpointer data)
{
        wint.run = TRUE;
        gtk_window_destroy(GTK_WINDOW(data));
} /* dialog_ok_callback */


/*****/

static void
dialog_cancel_callback(GtkWidget *widget, gpointer data)
{
        gtk_window_destroy(GTK_WINDOW(data));
} /* dialog_cancel_callback */


static void
alienmap_toggle_update (GtkWidget *widget,
        		gpointer   data)
{
  int *toggle_val;

  toggle_val = (int *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    *toggle_val = TRUE;
  else
    *toggle_val = FALSE;

  if (do_redsinus)
    wvals.redmode = SINUS;
  else if (do_redcosinus)
    wvals.redmode = COSINUS;
  else if (do_rednone)
    wvals.redmode = NONE;

  if (do_greensinus)
    wvals.greenmode = SINUS;
  else if (do_greencosinus)
    wvals.greenmode = COSINUS;
  else if (do_greennone)
    wvals.greenmode = NONE;

  if (do_bluesinus)
    wvals.bluemode = SINUS;
  else if (do_bluecosinus)
    wvals.bluemode = COSINUS;
  else if (do_bluenone)
    wvals.bluemode = NONE;

  /* the preview does not exist yet while the buttons are being made */
  if (wint.preview)
    dialog_update_preview();

}

static void
alienmap_logo_destroyed (GtkWidget *widget,
			 gpointer   data)
{
  logodlg = NULL;
}

void
alienmap_logo_dialog(void)
{
  GtkWidget *xlabel;
  GtkWidget *xbutton;
  GtkWidget *xlogo_box;
  GtkWidget *xpreview;
  GtkWidget *xframe,*xframe2;
  GtkWidget *xvbox;
  GtkWidget *xhbox;
  char *text;
  guchar *temp,*temp2;
  guchar *datapointer;
  gint y,x;

  if (!logodlg)
    {
      logodlg = gimp_dialog_new("About Alien Map");
      g_signal_connect(logodlg,
		       "destroy",
		       G_CALLBACK (alienmap_logo_destroyed),
		       NULL);
      /* closing only hides the window, as gtk_widget_hide_on_delete did */
      gtk_window_set_hide_on_close(GTK_WINDOW(logodlg), TRUE);

      xbutton = gimp_dialog_add_button(logodlg, "OK", NULL, NULL, TRUE);
      g_signal_connect_swapped (xbutton, "clicked",
				G_CALLBACK (gtk_window_close),
				logodlg);
      set_tooltip(xbutton,"This closes the information box");

      xframe = gtk_frame_new(NULL);
      gimp_container_set_border_width(xframe, 10);
      gimp_box_pack_start(gimp_dialog_get_vbox(logodlg), xframe, TRUE, TRUE, 0);
      xvbox = gimp_vbox_new(FALSE, 5);
      gimp_container_set_border_width(xvbox, 10);
      gtk_frame_set_child(GTK_FRAME(xframe), xvbox);

      /*  The logo frame & drawing area  */
      xhbox = gimp_hbox_new (FALSE, 5);
      gimp_box_pack_start (xvbox, xhbox, FALSE, TRUE, 0);

      xlogo_box = gimp_vbox_new (FALSE, 0);
      gimp_box_pack_start (xhbox, xlogo_box, FALSE, FALSE, 0);

      xframe2 = gtk_frame_new (NULL);
      gimp_box_pack_start (xlogo_box, xframe2, FALSE, FALSE, 0);

      xpreview = gimp_preview_new (GIMP_PREVIEW_COLOR);
      gimp_preview_size (GIMP_PREVIEW (xpreview), logo_width, logo_height);
      temp = g_malloc((logo_width+10)*3);
      datapointer=header_data+logo_width*logo_height-1;
      for (y = 0; y < logo_height; y++){
	temp2=temp;
	for (x = 0; x< logo_width; x++) {
	  HEADER_PIXEL(datapointer,temp2); temp2+=3;}
	gimp_preview_draw_row (GIMP_PREVIEW (xpreview),
			       temp,
			       0, y, logo_width);
      }
      g_free(temp);
      gtk_frame_set_child (GTK_FRAME (xframe2), xpreview);

      xhbox = gimp_hbox_new(FALSE, 5);
      gimp_box_pack_start(xvbox, xhbox, TRUE, TRUE, 0);
      text = "\nCotting Software Productions\n"
	"Bahnhofstrasse 31\n"
	"CH-3066 Stettlen (Switzerland)\n\n"
	"cotting@mygale.org\n"
	"http://www.mygale.org/~cotting\n\n"
	"AlienMap Plug-In for the GIMP\n"
	"Version 1.01\n";
      xlabel = gtk_label_new(text);
      gimp_box_pack_start(xhbox, xlabel, TRUE, FALSE, 0);

      gtk_window_present(GTK_WINDOW(logodlg));
    }
  else
    {
      gtk_window_present (GTK_WINDOW (logodlg));
    }
}
