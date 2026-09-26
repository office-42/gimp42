/*
 * This is a plug-in for the GIMP.
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
 *
 */

/*
 * Exchange one color with the other (settable threshold to convert from
 * one color-shade to another...might do wonders on certain images, or be
 * totally useless on others).
 * 
 * Author: robert@experimental.net
 * 
 * TODO:
 *	- preview (working on it already :)
 *	- threshold for each channel (not hard to implement, but really
 *	  needs a preview window)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include "libgimp/gimp.h"
#include <gtk/gtk.h>
#include "libgimp/gimpui.h"

/* big scales */
#define	SCALE_WIDTH	225

/* datastructure to store parameters in */
typedef struct
{
	guchar	fromred, fromgreen, fromblue, tored, togreen, toblue;
	guchar	threshold;
	gint32	image;
	gint32	drawable;
}	myParams;

/* lets prototype */
static void	query(void);
static void	run(char *, int, GParam *, int *, GParam **);
static int	doDialog(void);
static void	exchange(GDrawable *);
static void	doLabelAndScale(char *, GtkWidget *, guchar *);

static void	ok_callback(GtkWidget *, gpointer);
static void	close_callback(GtkWidget *, gpointer);
static void	scale_callback(GtkAdjustment *, gpointer);

/* some global variables */
myParams	xargs = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
int		running = 0;

/* lets declare what we want to do */
GPlugInInfo PLUG_IN_INFO =
{
	NULL,			       /* init_proc */
	NULL,			       /* quit_proc */
	query,			       /* query_proc */
	run,			       /* run_proc */
};

/* run program */
MAIN()

/* tell GIMP who we are */
static
void	query()
{
	static GParamDef args[] =
	{
		{ PARAM_INT32, "run_mode", "Interactive" },
		{ PARAM_IMAGE, "image", "Input image" },
		{ PARAM_DRAWABLE, "drawable", "Input drawable" },
		{ PARAM_INT8, "fromred", "Red value (from)" },
		{ PARAM_INT8, "fromgreen", "Green value (from)" },
		{ PARAM_INT8, "fromblue", "Blue value (from)" },
		{ PARAM_INT8, "tored", "Red value (to)" },
		{ PARAM_INT8, "togreen", "Green value (to)" },
		{ PARAM_INT8, "toblue", "Blue value (to)" },
		{ PARAM_INT8, "threshold", "Threshold" },
	};
	static GParamDef *return_vals = NULL;
	static int nargs = sizeof(args) / sizeof(args[0]);
	static int nreturn_vals = 0;

	gimp_install_procedure("plug_in_exchange",
			       "Color exchange",
			       "Exchange one color with another, optionally setting a threshold to convert from one shade to another",
			       "robert@experimental.net",
			       "robert@experimental.net",
			       "June 17th, 1997",
			       "<Image>/Filters/Colors/Color Exchange",
			       "RGB*",
			       PROC_PLUG_IN,
			       nargs, nreturn_vals,
			       args, return_vals);
}

/* main function */
static
void	run(char *name, int nparams, GParam *param, int *nreturn_vals, GParam **return_vals)
{
	static GParam	values[1];
	GRunModeType	runmode;
	GDrawable 	*drawable;
	gint32  	imageID;
	GStatusType 	status = STATUS_SUCCESS;

	*nreturn_vals = 1;
	*return_vals = values;

	values[0].type = PARAM_STATUS;
	values[0].data.d_status = status;

	switch (runmode = param[0].data.d_int32)
	{
		case RUN_INTERACTIVE:




				/* retrieve stored arguments (if any) */
				gimp_get_data("plug_in_exchange", &xargs);
				/* initialize using foreground color */
				gimp_palette_get_foreground(&xargs.fromred, &xargs.fromgreen, &xargs.fromblue);
				if (!xargs.image && !xargs.drawable)
					xargs.threshold = 0;
				/* and initialize some other things */
				xargs.image = param[1].data.d_image;
				xargs.drawable = param[2].data.d_drawable;
				if (!doDialog())
					return;
				break;
		case RUN_WITH_LAST_VALS:
				/* 
				 * instead of recalling the last-set values,
				 * run with the current foreground as 'from'
				 * color, making ALT-F somewhat more useful.
				 */
				gimp_palette_get_foreground(&xargs.fromred, &xargs.fromgreen, &xargs.fromblue);
				break;
		case RUN_NONINTERACTIVE:
		  if(nparams != 10)
		    status = STATUS_EXECUTION_ERROR;
		  if (status == STATUS_SUCCESS)
		    {
		      xargs.fromred = param[3].data.d_int8;
		      xargs.fromgreen = param[4].data.d_int8;
		      xargs.fromblue = param[5].data.d_int8;
		      xargs.tored = param[6].data.d_int8;
		      xargs.togreen = param[7].data.d_int8;
		      xargs.toblue = param[8].data.d_int8;
		      xargs.threshold = param[9].data.d_int32;
		    }
		  break;
			
		default:	
				break;
	}

	/*  Get the specified drawable  */
	drawable = gimp_drawable_get(param[2].data.d_drawable);
	imageID = param[1].data.d_image;

	if (status == STATUS_SUCCESS)
	{
		if (gimp_drawable_color(drawable->id))
		{
			gimp_progress_init("Color exchange...");
			gimp_tile_cache_ntiles(2 * (drawable->width / gimp_tile_width() + 1));
			exchange(drawable);
			/* store our settings */
			gimp_set_data("plug_in_exchange", &xargs, sizeof(myParams));
			/* and flush */
			gimp_displays_flush();
		}
		else
			status = STATUS_EXECUTION_ERROR;
	}
	values[0].data.d_status = status;
	gimp_drawable_detach(drawable);
}

/* do the exchanging */
static
void	exchange(GDrawable *drawable)
{
	GPixelRgn	srcPR, destPR;
	guchar 		*src_row;
	guchar 		*dest_row;
	gint    	width, height;
	gint    	x, y, bpp;
	gint    	x1, y1, x2, y2;

	/* 
	 * Get the input area. This is the bounding box of the selection in
	 * the image (or the entire image if there is no selection). Only
	 * operating on the input area is simply an optimization. It doesn't
	 * need to be done for correct operation. (It simply makes it go
	 * faster, since fewer pixels need to be operated on).
	 */
	gimp_drawable_mask_bounds(drawable->id, &x1, &y1, &x2, &y2);

        /* 
         * Get the size of the input image. (This will/must be the same
         * as the size of the output image.
         */
	width = drawable->width;
	height = drawable->height;
	bpp = drawable->bpp;

	/* allocate row buffers */
	src_row = (guchar *) malloc((x2 - x1) * bpp);
	dest_row = (guchar *) malloc((x2 - x1) * bpp);

	/* initialize the pixel regions */
	gimp_pixel_rgn_init(&srcPR, drawable, 0, 0, width, height, FALSE, FALSE);
	gimp_pixel_rgn_init(&destPR, drawable, 0, 0, width, height, TRUE, TRUE);

	for (y = y1; y < y2; y++)
	{
		gimp_pixel_rgn_get_row(&srcPR, src_row, x1, y, (x2 - x1));
		for (x = x1; x < x2; x++)
		{
			guchar	red, green, blue,
				minred, mingreen, minblue,
				maxred, maxgreen, maxblue;
			int	rest, wanted = 0,
				redx = 0, greenx = 0, bluex = 0;

			/* get boundary values */
			minred = MAX((int) xargs.fromred - xargs.threshold, 0);
			mingreen = MAX((int) xargs.fromgreen - xargs.threshold, 0);
			minblue = MAX((int) xargs.fromblue - xargs.threshold, 0);

			maxred = MIN((int) xargs.fromred + xargs.threshold, 255);
			maxgreen = MIN((int) xargs.fromgreen + xargs.threshold, 255);
			maxblue = MIN((int) xargs.fromblue + xargs.threshold, 255);
			
			/* get current pixel values */
			red = src_row[(x-x1) * bpp];
			green = src_row[(x-x1) * bpp + 1];
			blue = src_row[(x-x1) * bpp + 2];

			/* 
			 * check if we want this pixel (does it fall between
			 * our boundary?)
			 */
			if (red >= minred && red <= maxred &&
			    green >= mingreen && green <= maxgreen &&
			    blue >= minblue && blue <= maxblue)
			{
				redx = red - xargs.fromred;
				greenx = green - xargs.fromgreen;
				bluex = blue - xargs.fromblue;
				wanted = 1;
			}

			/* exchange if needed */
			dest_row[(x-x1) * bpp] = wanted ? MAX(MIN(xargs.tored + redx, 255), 0) : src_row[(x-x1) * bpp];
			dest_row[(x-x1) * bpp + 1] = wanted ? MAX(MIN(xargs.togreen + greenx, 255), 0) : src_row[(x-x1) * bpp + 1];
			dest_row[(x-x1) * bpp + 2] = wanted ? MAX(MIN(xargs.toblue + bluex, 255), 0) : src_row[(x-x1) * bpp + 2];

			/* copy rest (most likely alpha-channel) */
			for (rest = 3; rest < bpp; rest++)
				dest_row[(x-x1) * bpp + rest] = src_row[(x-x1) * bpp + rest];
		}
		/* store the dest */
		gimp_pixel_rgn_set_row(&destPR, dest_row, x1, y, (x2 - x1));
		/* and tell the user what we're doing */
		if ((y % 10) == 0)
			gimp_progress_update((double) y / (double) (y2 - y1));
	}
	/* update the processed region */
	gimp_drawable_flush(drawable);
	gimp_drawable_merge_shadow(drawable->id, TRUE);
	gimp_drawable_update(drawable->id, x1, y1, (x2 - x1), (y2 - y1));
	/* and clean up */
	free(src_row);
	free(dest_row);
}

/* show our dialog */
static
int	doDialog(void)
{
	GtkWidget	*dialog;
	GtkWidget	*button;
	GtkWidget	*frame;
	GtkWidget	*table;
	GtkWidget	*mainbox;
	GtkWidget	*tobox;
	GtkWidget	*frombox;
	int		framenumber;

	gtk_init();

	/* set up the dialog */
	dialog = gimp_dialog_new("Color Exchange");
	g_signal_connect(dialog, "destroy",
			 G_CALLBACK(close_callback),
			 NULL);

	/* lets create some buttons */
	gimp_dialog_add_button(dialog, "Ok",
			       G_CALLBACK(ok_callback), dialog, TRUE);
	button = gimp_dialog_add_button(dialog, "Cancel", NULL, NULL, FALSE);
	g_signal_connect_swapped(button, "clicked",
				 G_CALLBACK(gtk_window_destroy),
				 dialog);

	/* do some boxes here */
	mainbox = gimp_vbox_new(FALSE, 5);
	gimp_container_set_border_width(mainbox, 10);
	gimp_box_pack_start(gimp_dialog_get_vbox(dialog), mainbox, TRUE, TRUE, 0);
	frombox = gimp_hbox_new(FALSE, 5);
	gimp_container_set_border_width(frombox, 10);
	gimp_box_pack_start(mainbox, frombox, TRUE, TRUE, 0);
	tobox = gimp_hbox_new(FALSE, 5);
	gimp_container_set_border_width(tobox, 10);
	gimp_box_pack_start(mainbox, tobox, TRUE, TRUE, 0);

	/* and our scales */
	for (framenumber = 0; framenumber < 2; framenumber++)
	{
		frame = gtk_frame_new(framenumber ? "To color" : "From color");
		gimp_container_set_border_width(frame, 10);
		gimp_box_pack_start(framenumber ? tobox : frombox,
		                    frame, TRUE, TRUE, 0);
		table = gimp_table_new(8, 2, FALSE);
		gimp_container_set_border_width(table, 10);
		gtk_frame_set_child(GTK_FRAME(frame), table);
		doLabelAndScale("Red", table, framenumber ? &xargs.tored : &xargs.fromred);
		doLabelAndScale("Green", table, framenumber ? &xargs.togreen : &xargs.fromgreen);
		doLabelAndScale("Blue", table, framenumber ? &xargs.toblue : &xargs.fromblue);
		if (!framenumber)
			doLabelAndScale("Threshold", table, &xargs.threshold);
	}

	/* show everything */
	gtk_window_present(GTK_WINDOW(dialog));
	gimp_main_loop_run();

	return running;
}

static
void	doLabelAndScale(char *labelname, GtkWidget *table, guchar *dest)
{
	static	int	idx = -1;
	GtkWidget	*label, *scale;
	GtkAdjustment	*scale_data;

	idx++;
	label = gtk_label_new(labelname);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gimp_table_attach(table, label, 0, 1, idx, idx + 1, GIMP_FILL, 0, 5, 0);
	scale_data = gtk_adjustment_new(*dest, 0.0, 255.0, 1.0, 1.0, 0.0);
	/* just need 1:1 resolution on scales */
	scale = gimp_hscale_new(scale_data, 0);
	gtk_widget_set_size_request(scale, SCALE_WIDTH, -1);
	gimp_table_attach(table, scale, 1, 2, idx, idx + 1, GIMP_FILL, 0, 0, 0);
	g_signal_connect(scale_data, "value-changed",
			 G_CALLBACK(scale_callback),
			 dest);
}

static
void	close_callback(GtkWidget *widget, gpointer data)
{
	gimp_main_loop_quit();
}

static
void	ok_callback(GtkWidget *widget, gpointer data)
{
	running = 1;
	gtk_window_destroy(GTK_WINDOW(data));
}

static
void	scale_callback(GtkAdjustment *adj, gpointer data)
{
	guchar	*val = data;

	*val = (guchar) gtk_adjustment_get_value(adj);
}
