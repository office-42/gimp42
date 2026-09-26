/**
 * aa.c version 1.0
 * A plugin that uses libaa (ftp://ftp.ta.jcu.cz/pub/aa) to save images as
 * ASCII.
 * NOTE: This plugin *requires* aalib 1.2 or later. Earlier versions will
 * not work.
 * Code copied from all over the GIMP source.
 * Tim Newsome <nuisance@cmu.edu>
 */

#include <aalib.h>
#include <string.h>
#include <libgimp/gimp.h>
#include "libgimp/gimpui.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>

/* 
 * Declare some local functions.
 */
static void query(void);
static void run(char *name, int nparams, GParam * param, int *nreturn_vals,
								GParam ** return_vals);
static gint aa_savable(gint32 drawable_ID);
static gint save_aa(int output_type, char *filename, gint32 image,
										gint32 drawable);
static gint gimp2aa(gint32 image, gint32 drawable_ID, aa_context * context);
static gint type_dialog(int selected);
static void type_dialog_close_callback(GtkWidget *widget, gpointer data);
static void type_dialog_ok_callback (GtkWidget *widget, gpointer data);
static void type_dialog_toggle_update (GtkWidget *widget, gpointer data);
static void type_dialog_cancel_callback (GtkWidget *widget, gpointer data);

/* 
 * Some global variables.
 */

GPlugInInfo PLUG_IN_INFO =
{
	NULL,													/* init_proc */
	NULL,													/* quit_proc */
	query,												/* query_proc */
	run,													/* run_proc */
};

/**
 * Type the user selected. (Global for easier UI coding.
 */
static int selected_type = 0;


MAIN()
/**
 * Called by the GIMP to figure out what this plugin does.
 */
		 static void query()
{
	static GParamDef save_args[] =
	{
		{PARAM_INT32, "run_mode", "Interactive, non-interactive"},
		{PARAM_IMAGE, "image", "Input image"},
		{PARAM_DRAWABLE, "drawable", "Drawable to save"},
		{PARAM_STRING, "filename", "The name of the file to save the image in"},
		{PARAM_STRING, "raw_filename", "The name entered"},
		{PARAM_STRING, "file_type", "File type to use"}
	};
	static int nsave_args = sizeof(save_args) / sizeof(save_args[0]);

	gimp_install_procedure("file_aa_save",
												 "Saves files in various text formats",
												 "Saves files in various text formats",
												 "Tim Newsome <nuisance@cmu.edu>",
												 "Tim Newsome <nuisance@cmu.edu>",
												 "1997",
												 "<Save>/AA",
												 "GRAY*",		/* support grayscales */
												 PROC_PLUG_IN,
												 nsave_args, 0,
												 save_args, NULL);

	gimp_register_save_handler("file_aa_save", "ansi,txt,text,html", "");
}

/**
 * Searches aa_formats defined by aalib to find the index of the type
 * specified by string.
 * -1 means it wasn't found.
 */
static int get_type_from_string(char *string)
{
	int type = 0;
	const aa_format *const *p = aa_formats;

	while (*p && strcmp((*p)->formatname, string)) {
		p++;
		type++;
	}

	if (*p == NULL)
		return -1;

	return type;
}

/**
 * Called by the GIMP to run the actual plugin.
 */
static void run(char *name, int nparams, GParam * param, int *nreturn_vals,
								GParam ** return_vals)
{
	static GParam values[2];
	GStatusType status = STATUS_SUCCESS;
	GRunModeType run_mode;
	int output_type = 0;
	static int last_type = 0;

	/* Set us up to return a status. */
	*nreturn_vals = 1;
	*return_vals = values;
	values[0].type = PARAM_STATUS;
	values[0].data.d_status = STATUS_CALLING_ERROR;

	if (!aa_savable(param[2].data.d_int32)) {
		values[0].data.d_status = STATUS_CALLING_ERROR;
		return;
	}
	run_mode = param[0].data.d_int32;

	switch (run_mode) {
	case RUN_INTERACTIVE:
		gimp_get_data("file_aa_save", &last_type);
		output_type = type_dialog(last_type);
		break;

	case RUN_NONINTERACTIVE:
		/*  Make sure all the arguments are there!  */
		if (nparams != 6)
			status = STATUS_CALLING_ERROR;
		else
			output_type = get_type_from_string(param[5].data.d_string);
		break;

	case RUN_WITH_LAST_VALS:
		gimp_get_data("file_aa_save", &last_type);
		output_type = last_type;
		break;

	default:
		break;
	}

	if (output_type < 0) {
		status = STATUS_CALLING_ERROR;
		return;
	}

	if (save_aa(output_type, param[3].data.d_string, param[1].data.d_int32,
							param[2].data.d_int32))
		values[0].data.d_status = STATUS_EXECUTION_ERROR;
	else
		values[0].data.d_status = STATUS_SUCCESS;

	last_type = output_type;
	gimp_set_data("file_aa_save", &last_type, sizeof(last_type));
}

/**
 * The actual save function. What it's all about.
 * The image type has to be GRAY.
 */
static gint save_aa(int output_type, char *filename, gint32 image,
										gint32 drawable_ID)
{
	aa_savedata savedata =
	{NULL, NULL};
	aa_context *context = NULL;
	aa_format format;
	GDrawable *drawable = NULL;

	/*fprintf(stderr, "save %s\n", filename); */

	drawable = gimp_drawable_get(drawable_ID);
	memcpy(&format, aa_formats[output_type], sizeof(format));
	format.width = drawable->width / 2;
	format.height = drawable->height / 2;

	/*fprintf(stderr, "save_aa %i x %i\n", format.width, format.height); */

	/* Get a libaa context which will save its output to filename. */
	savedata.name = filename;
	savedata.format = &format;

	context = aa_init(&save_d, &aa_defparams, &savedata);
	if (context == NULL)
		return 1;

	gimp2aa(image, drawable_ID, context);
	aa_flush(context);
	aa_close(context);

	/*fprintf(stderr, "Success!\n"); */

	return 0;
}

static gint gimp2aa(gint32 image, gint32 drawable_ID, aa_context * context)
{
	int width, height, x, y;
	guchar *buffer;
	GDrawable *drawable = NULL;
	GPixelRgn pixel_rgn;
	aa_renderparams *renderparams = NULL;
	int bpp;

	width = aa_imgwidth(context);
	height = aa_imgheight(context);
	/*fprintf(stderr, "gimp2aa %i x %i\n", width, height); */

	drawable = gimp_drawable_get(drawable_ID);

	bpp = drawable->bpp;
	buffer = g_new(guchar, width * bpp);
	if (buffer == NULL)
		return 1;

	gimp_pixel_rgn_init(&pixel_rgn, drawable, 0, 0, drawable->width,
											drawable->height, FALSE, FALSE);

	for (y = 0; y < height; y++) {
		gimp_pixel_rgn_get_row(&pixel_rgn, buffer, 0, y, width);
		for (x = 0; x < width; x++) {
			/* Just copy one byte. If it's indexed that's all we need. Otherwise
			 * it'll be the most significant one. */
			aa_putpixel(context, x, y, buffer[x * bpp]);
		}
	}

	renderparams = aa_getrenderparams();
	renderparams->dither = AA_FLOYD_S;
	aa_render(context, renderparams, 0, 0, aa_scrwidth(context),
						aa_scrheight(context));

	return 0;
}

static gint aa_savable(gint32 drawable_ID)
{
	GDrawableType drawable_type;

	drawable_type = gimp_drawable_type(drawable_ID);

	if (drawable_type != GRAY_IMAGE && drawable_type != GRAYA_IMAGE)
		return 0;

	return 1;
}

/* 
 * User Interface dialog thingie.
 */

static gint type_dialog(int selected) {
	GtkWidget *dlg;
	GtkWidget *toggle;
	GtkWidget *frame;
	GtkWidget *toggle_vbox;
	GtkWidget *group;


	gtk_init ();

	/* Create the actual window. */
	dlg = gimp_dialog_new ("Save as text");
	g_signal_connect (dlg, "destroy",
										 G_CALLBACK (type_dialog_close_callback), NULL);

	/*  Action area  */
	gimp_dialog_add_button (dlg, "OK", G_CALLBACK (type_dialog_ok_callback),
				dlg, TRUE);
	gimp_dialog_add_button (dlg, "Cancel",
				G_CALLBACK (type_dialog_cancel_callback), dlg, FALSE);

	/*  file save type  */
	frame = gtk_frame_new("Data Formatting");
	gimp_container_set_border_width (frame, 10);
	gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, FALSE, TRUE, 0);
	toggle_vbox = gimp_vbox_new(FALSE, 5);
	gimp_container_set_border_width (toggle_vbox, 5);
	gimp_container_add (frame, toggle_vbox);

	group = NULL;
	{
		const aa_format *const *p = aa_formats;
		int current = 0;

		while (*p != NULL) {
			toggle = gimp_radio_button_new (group, (*p)->formatname);
			group = toggle;
			gimp_box_pack_start (toggle_vbox, toggle, FALSE, FALSE, 0);
			g_signal_connect (toggle, "toggled",
			  G_CALLBACK (type_dialog_toggle_update),
			  (gpointer) (*p)->formatname);
			if (current == selected)
				gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), 1);
			else
				gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), 0);

			p++;
			current++;
		}
	}


  gtk_window_present (GTK_WINDOW (dlg));

	gimp_main_loop_run ();

	return selected_type;
}

/*
 * Callbacks for the dialog.
 */

static void type_dialog_close_callback(GtkWidget *widget, gpointer data) {
  gimp_main_loop_quit ();
}

static void type_dialog_ok_callback (GtkWidget *widget, gpointer   data) {
  gtk_window_destroy (GTK_WINDOW (data));
}

static void type_dialog_cancel_callback (GtkWidget *widget, gpointer   data) {
	selected_type = -1;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void type_dialog_toggle_update (GtkWidget *widget, gpointer data) {
	selected_type = get_type_from_string((char *)data);
}
