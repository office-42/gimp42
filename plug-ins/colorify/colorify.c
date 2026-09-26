/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * Colorify. Changes the pixel's luminosity to a specified color
 * Copyright (C) 1997 Francisco Bustamante
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

/* Changes: 

   1.1 
   -Corrected small bug when calling color selection dialog 
   -Added LUTs to speed things a little bit up 

   1.0 
   -First release */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#define PLUG_IN_NAME "Colorify"
#define PLUG_IN_VERSION "1.1"

static void      query (void);
static void      run   (char      *name,
			int        nparams,
			GParam    *param,
			int       *nreturn_vals,
			GParam   **return_vals);

typedef struct {
	guchar color[3];
} ColorifyVals;

typedef struct {
	gint run;
} ColorifyInterface;

typedef struct {
	guchar red;
	guchar green;
	guchar blue;
	GtkWidget *preview;
	gint button_num;
} ButtonInformation;

static ColorifyInterface cint =
{
	FALSE
};

static ColorifyVals cvals =
{
	{255, 255, 255}
};

static ButtonInformation button_info[] =
{
	{255, 0, 0, NULL, 0},
	{255, 255, 0, NULL, 0},
	{0, 255, 0, NULL, 0},
	{0, 255, 255, NULL, 0},
	{0, 0, 255, NULL, 0},
	{255, 0, 255, NULL, 0},
	{255, 255, 255, NULL, 0},
};

GPlugInInfo PLUG_IN_INFO =
{
	NULL,
	NULL,
	query,
	run,
};

gint lum_red_lookup[256], lum_green_lookup[256], lum_blue_lookup[256];
gint final_red_lookup[256], final_green_lookup[256], final_blue_lookup[256];

MAIN ()

static int colorify_dialog (guchar red, guchar green, guchar blue);
static void colorify (GDrawable *drawable);
static void set_preview_color (GtkWidget *preview, guchar red, guchar green, guchar blue);

static void
query (void)
{
	static GParamDef args[] =
	{
		{ PARAM_INT32, "run_mode", "Interactive, non-interactive" },
		{ PARAM_IMAGE, "image", "Input image" },
		{ PARAM_DRAWABLE, "drawable", "Input drawable" },
		{ PARAM_COLOR, "color", "Color to apply"},
	};

	static GParamDef *return_vals  = NULL;
	static int        nargs        = sizeof(args) / sizeof(args[0]),
		          nreturn_vals = 0;

	gimp_install_procedure ("plug_in_colorify",
				"Similar to the \"Color\" mode for layers.",
				"Makes an average of the RGB channels and uses it to set the color",
				"Francisco Bustamante", "Francisco Bustamante",
				"0.0.1", "<Image>/Filters/Colors/Colorify", "RGB",
				PROC_PLUG_IN,
				nargs, nreturn_vals,
				args, return_vals);
}

gint sel_x1, sel_x2, sel_y1, sel_y2, sel_width, sel_height;
GtkWidget *preview;


static void
run (char    *name,
     int      nparams,
     GParam  *param,
     int     *nreturn_vals,
     GParam **return_vals)
{
	GRunModeType run_mode;
	GStatusType status;
	static GParam values[1];
	GDrawable *drawable;

	status = STATUS_SUCCESS;
	run_mode = param[0].data.d_int32;

	values[0].type = PARAM_STATUS;
	values[0].data.d_status = status;

	*nreturn_vals = 1;
	*return_vals = values;

	drawable = gimp_drawable_get (param[2].data.d_drawable);

	gimp_drawable_mask_bounds (drawable->id, &sel_x1, &sel_y1, &sel_x2, &sel_y2);

	sel_width = sel_x2 - sel_x1;
	sel_height = sel_y2 - sel_y1;

	switch (run_mode) {
		case RUN_INTERACTIVE :
			gimp_get_data(PLUG_IN_NAME, &cvals);
			if (!colorify_dialog (cvals.color[0], cvals.color[1], cvals.color[2]))
				return;
			break;

		case RUN_NONINTERACTIVE :
			if (nparams != 4)
				status = STATUS_CALLING_ERROR;
			if (status == STATUS_SUCCESS) {
					cvals.color[0] = param[3].data.d_color.red;
					cvals.color[1] = param[3].data.d_color.green;
					cvals.color[2] = param[3].data.d_color.blue;
			}
			break;

		case RUN_WITH_LAST_VALS :
			/*  Possibly retrieve data  */
			gimp_get_data (PLUG_IN_NAME, &cvals);
			break;

		default :
			break;
	}

	if (status == STATUS_SUCCESS) {
		gimp_progress_init("Colorifying...");

		colorify (drawable);

		if (run_mode == RUN_INTERACTIVE)
			gimp_set_data (PLUG_IN_NAME, &cvals, sizeof (ColorifyVals));

		if (run_mode != RUN_NONINTERACTIVE) {
			gimp_displays_flush ();
		}
	}

	values[0].data.d_status = status;
}

static void colorify_row (guchar *row,
			  gint width);
static void close_callback (GtkWidget *widget,
			    gpointer data);
static void colorify_ok_callback (GtkWidget *widget,
				  gpointer data);
static void custom_color_callback (GtkWidget *widget,
				   gpointer data);
static void predefined_color_callback (GtkWidget *widget,
				       gpointer data);
static void color_changed (const guchar *rgb,
			   gpointer data);

static void
colorify (GDrawable *drawable)
{
	GPixelRgn source_region, dest_region;
	guchar *row;
	gint y = 0;
	gint progress = 0;
	gint i = 0;

	for (i = 0; i < 256; i ++) {
		lum_red_lookup[i] = i * 0.30;
		lum_green_lookup[i] = i * 0.59;
		lum_blue_lookup[i] = i * 0.11;
		final_red_lookup[i] = i * cvals.color[0] / 255;
		final_green_lookup[i] = i * cvals.color[1] / 255;
		final_blue_lookup[i] = i * cvals.color[2] / 255;
	}

	row = g_malloc (sel_width * 3 * sizeof(guchar));

	gimp_pixel_rgn_init (&source_region, drawable, sel_x1, sel_y1, sel_width, sel_height, FALSE, FALSE);
	gimp_pixel_rgn_init (&dest_region, drawable, sel_x1, sel_y1, sel_width, sel_height, TRUE, TRUE);

	for (y = sel_y1; y < sel_y2; y++) {
		gimp_pixel_rgn_get_row (&source_region, row, sel_x1, y, sel_width);

		colorify_row (row, sel_width);

		gimp_pixel_rgn_set_row (&dest_region, row, sel_x1, y, sel_width);
		gimp_progress_update ((double) ++progress / sel_height);
		
	}

	g_free (row);

	gimp_drawable_flush (drawable);
 	gimp_drawable_merge_shadow (drawable->id, TRUE); 
	gimp_drawable_update (drawable->id, sel_x1, sel_y1, sel_width, sel_height);
}

static void
colorify_row (guchar *row,
	      gint width)
{
	gint cur_x;
	gint lum; /* luminosity */
	guchar *current = row;

	for (cur_x = 0; cur_x < width; cur_x++) {
		lum = lum_red_lookup[current[0]] + lum_green_lookup[current[1]] + lum_blue_lookup[current[2]];

		current[0] = final_red_lookup[lum];
		current[1] = final_green_lookup[lum];
		current[2] = final_blue_lookup[lum];
		
		current += 3;
	}
}

static int
colorify_dialog (guchar red,
		 guchar green,
		 guchar blue)
{
	GtkWidget *dialog;
	GtkWidget *label;
	GtkWidget *button;
	GtkWidget *frame;
	GtkWidget *table;
	GtkWidget *group;
	gint i;

	gtk_init ();

	dialog = gimp_dialog_new ("Colorify");
	g_signal_connect (dialog, "destroy",
			  G_CALLBACK (close_callback),
			  NULL);

	gimp_dialog_add_button (dialog, "Ok",
				G_CALLBACK (colorify_ok_callback),
				dialog, TRUE);

	button = gimp_dialog_add_button (dialog, "Cancel", NULL, NULL, FALSE);
	g_signal_connect_swapped (button, "clicked",
				  G_CALLBACK (gtk_window_destroy),
				  dialog);

	frame = gtk_frame_new ("Color");
	gimp_container_set_border_width (frame, 10);
	gimp_box_pack_start (gimp_dialog_get_vbox (dialog), frame, TRUE, TRUE, 0);

	table = gimp_table_new (2, 7, TRUE);
	gimp_container_set_border_width (table, 10);
	gtk_frame_set_child (GTK_FRAME (frame), table);
	gtk_grid_set_row_spacing (GTK_GRID (table), 5);
	gtk_grid_set_column_spacing (GTK_GRID (table), 5);

	label = gtk_label_new ("Custom Color: ");
	gimp_table_attach (table, label, 4, 6, 0, 1,  GIMP_FILL, GIMP_FILL, 0, 0);

	/*  The colour buttons are a group of toggle buttons showing a
	 *  swatch, like GTK 1's radio buttons without an indicator.
	 */
	button = gtk_toggle_button_new ();
	group = button;
	gtk_widget_set_size_request (button, 35, 35);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (custom_color_callback),
			  dialog);
	gimp_table_attach (table, button, 6, 7, 0, 1, GIMP_FILL, GIMP_FILL, 0, 0);

	preview = gimp_preview_new (GIMP_PREVIEW_COLOR);
	gimp_preview_size (GIMP_PREVIEW (preview), 30, 30);
	set_preview_color (preview, cvals.color[0], cvals.color[1], cvals.color[2]);
	gtk_button_set_child (GTK_BUTTON (button), preview);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);

	for(i = 0; i < 7; i++) {
		button = gtk_toggle_button_new ();
		gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (button),
					     GTK_TOGGLE_BUTTON (group));
		button_info[i].preview = gimp_preview_new (GIMP_PREVIEW_COLOR);
		gimp_preview_size (GIMP_PREVIEW (button_info[i].preview),
				   30, 30);
		gtk_button_set_child (GTK_BUTTON (button), button_info[i].preview);
		set_preview_color (button_info[i].preview,
				   button_info[i].red,
				   button_info[i].green,
				   button_info[i].blue);
		button_info[i].button_num = i;
		g_signal_connect (button, "clicked",
				  G_CALLBACK (predefined_color_callback),
				  &button_info[i].button_num);

		gimp_table_attach (table, button, i, i + 1, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0);
	}

	gtk_window_present (GTK_WINDOW (dialog));

	gimp_main_loop_run ();
	return cint.run;
}

static void
close_callback (GtkWidget *widget,
		gpointer data)
{
	gimp_main_loop_quit ();
}

static void
colorify_ok_callback (GtkWidget *widget,
		      gpointer data)
{
	cint.run = TRUE;
	gtk_window_destroy (GTK_WINDOW (data));
}

static void
set_preview_color (GtkWidget *preview,
		   guchar red,
		   guchar green,
		   guchar blue)
{
	gimp_preview_fill (GIMP_PREVIEW (preview), red, green, blue);
}

static void
custom_color_callback (GtkWidget *widget,
		       gpointer data)
{
	gimp_color_dialog_run (GTK_WINDOW (data), "Colorify Custom Color",
			       cvals.color, color_changed, NULL);
}

static void
predefined_color_callback (GtkWidget *widget,
			   gpointer data)
{
	gint *num;

	num = (gint *) data;

	cvals.color[0] = button_info[*num].red;
	cvals.color[1] = button_info[*num].green;
	cvals.color[2] = button_info[*num].blue;
}

static void
color_changed (const guchar *rgb,
	       gpointer data)
{
	cvals.color[0] = rgb[0];
	cvals.color[1] = rgb[1];
	cvals.color[2] = rgb[2];

	set_preview_color (preview, cvals.color[0], cvals.color[1], cvals.color[2]);
}
