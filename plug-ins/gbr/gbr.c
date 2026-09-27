/*
 * gbr plug-in version 1.00
 * Loads/saves version 2 GIMP .gbr files, by Tim Newsome <drz@frody.bloke.com>
 * Some bits stolen from the .99.7 source tree.
 * 
 * Added in GBR version 1 support after learning that there wasn't a 
 * tool to read them.  
 * July 6, 1998 by Seth Burgess <sjburges@gimp.org>
 *
 * TODO: Give some better error reporting on not opening files/bad headers
 *       etc. 
 */

#include <setjmp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"
#include "app/brush_header.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif



/* Declare local data types
 */

typedef struct {
	char description[256];
	unsigned int spacing;
} t_info;

t_info info = {   /* Initialize to this, change if non-interactive later */
	"GIMP Brush",     
	10
};

int run_flag = 0;

/* Declare some local functions.
 */
static void   query      (void);
static void   run        (char    *name,
                          int      nparams,
                          GParam  *param,
                          int     *nreturn_vals,
                          GParam **return_vals);
static gint32 load_image (char   *filename);
static gint   save_image (char   *filename,
                          gint32  image_ID,
                          gint32  drawable_ID);

static gint   save_dialog ();
static void close_callback(GtkWidget * widget, gpointer data);
static void ok_callback(GtkWidget * widget, gpointer data);
static void entry_callback(GtkWidget * widget, gpointer data);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};


MAIN ()

static void
query ()
{
  static GParamDef load_args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_STRING, "filename", "The name of the file to load" },
    { PARAM_STRING, "raw_filename", "The name of the file to load" },
  };
  static GParamDef load_return_vals[] =
  {
    { PARAM_IMAGE, "image", "Output image" },
  };
  static int nload_args = sizeof (load_args) / sizeof (load_args[0]);
  static int nload_return_vals = sizeof (load_return_vals) / sizeof (load_return_vals[0]);

  static GParamDef save_args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image" },
    { PARAM_DRAWABLE, "drawable", "Drawable to save" },
    { PARAM_STRING, "filename", "The name of the file to save the image in" },
    { PARAM_STRING, "raw_filename", "The name of the file to save the image in" },
    { PARAM_INT32, "spacing", "Spacing of the brush" },
    { PARAM_STRING, "description", "Short description of the brush" },
  };
  static int nsave_args = sizeof (save_args) / sizeof (save_args[0]);

  gimp_install_procedure ("file_gbr_load",
                          "loads files of the .gbr file format",
                          "FIXME: write help",
                          "Tim Newsome",
                          "Tim Newsome",
                          "1997",
                          "<Load>/GBR",
                          NULL,
                          PROC_PLUG_IN,
                          nload_args, nload_return_vals,
                          load_args, load_return_vals);

  gimp_install_procedure ("file_gbr_save",
                          "saves files in the .gbr file format",
                          "Yeah!",
                          "Tim Newsome",
                          "Tim Newsome",
                          "1997",
                          "<Save>/GBR",
                          "GRAY",
                          PROC_PLUG_IN,
                          nsave_args, 0,
                          save_args, NULL);

  gimp_register_magic_load_handler ("file_gbr_load", "gbr", "", "20,string,GIMP");
  gimp_register_save_handler ("file_gbr_save", "gbr", "");
}

static void
run (char    *name,
     int      nparams,
     GParam  *param,
     int     *nreturn_vals,
     GParam **return_vals)
{
  static GParam values[2];
  GRunModeType run_mode;
  gint32 image_ID;
	GStatusType status = STATUS_SUCCESS;

  run_mode = param[0].data.d_int32;

  *return_vals = values;
  values[0].type = PARAM_STATUS;
  values[0].data.d_status = STATUS_CALLING_ERROR;
  values[1].type = PARAM_IMAGE;
  values[1].data.d_image = -1;

  if (strcmp (name, "file_gbr_load") == 0) {
		image_ID = load_image (param[1].data.d_string);

		if (image_ID != -1) {
			values[0].data.d_status = STATUS_SUCCESS;
			values[1].data.d_image = image_ID;
		} else {
			values[0].data.d_status = STATUS_EXECUTION_ERROR;
		}
		*nreturn_vals = 2;
	}
  else if (strcmp (name, "file_gbr_save") == 0) {
		switch (run_mode) {
			case RUN_INTERACTIVE:
				/*  Possibly retrieve data  */
				gimp_get_data("file_gbr_save", &info);
				if (! save_dialog ())
				  {
				    values[0].data.d_status = STATUS_CANCEL;
				    return;
				  }
				break;
			case RUN_NONINTERACTIVE:  /* FIXME - need a real RUN_NONINTERACTIVE */
				if (nparams != 7)
					status = STATUS_CALLING_ERROR;
				if (status == STATUS_SUCCESS)
					{
					info.spacing = (param[5].data.d_int32);
				    g_strlcpy (info.description, param[6].data.d_string, 256);	
					}
			    break;
			case RUN_WITH_LAST_VALS:
				gimp_get_data ("file_gbr_save", &info);
				break;
		}

		if (save_image (param[3].data.d_string, param[1].data.d_int32,
				param[2].data.d_int32)) {
			gimp_set_data ("file_gbr_save", &info, sizeof(info));
			values[0].data.d_status = STATUS_SUCCESS;
		} else
			values[0].data.d_status = STATUS_EXECUTION_ERROR;
		*nreturn_vals = 1;
	}
}

static gint32 load_image (char *filename) {
	char *temp;
	int fd;
	BrushHeader ph;
	gchar *buffer;
	gint32 image_ID, layer_ID;
	GDrawable *drawable;
	gint line;
	GPixelRgn pixel_rgn;
	int version_extra;

	temp = g_malloc(strlen (filename) + 11);
	sprintf(temp, "Loading %s:", filename);
	gimp_progress_init(temp);
	g_free (temp);

	fd = open(filename, O_RDONLY | O_BINARY);
	if (fd == -1) {
		return -1;
	}

	if (read(fd, &ph, sizeof(ph)) != sizeof(ph)) {
		close(fd);
		return -1;
	}

  /*  rearrange the bytes in each unsigned int  */
	ph.header_size = g_ntohl(ph.header_size);
	ph.version = g_ntohl(ph.version);
	ph.width = g_ntohl(ph.width);
	ph.height = g_ntohl(ph.height);
	ph.bytes = g_ntohl(ph.bytes);
	ph.magic_number = g_ntohl(ph.magic_number);
	ph.spacing = g_ntohl(ph.spacing);

	/* How much extra to add ot the header seek - 1 needs a bit more */
	version_extra = 0;
	
	if (ph.version == 1) {
		/* Version 1 didn't know about spacing */	
		ph.spacing=25;
	 	/* And we need to rewind the handle a bit too */
		lseek (fd, -8, SEEK_CUR);
		version_extra=8;
		}
	/* Version 1 didn't know about magic either */
	if ((ph.version != 1 && 
			(ph.magic_number != GBRUSH_MAGIC || ph.version != 2)) ||
			ph.header_size <= sizeof(ph)) {
		close(fd);
		return -1;
	}

	if (lseek(fd, ph.header_size - sizeof(ph) + version_extra, SEEK_CUR) !=
			ph.header_size) {
		close(fd);
		return -1; 
	}
 
	/* Now there's just raw data left. */

	/* Reject absurd sizes before multiplying them */
	if (ph.width == 0 || ph.width > 262144 ||
			ph.height == 0 || ph.height > 262144 ||
			ph.bytes == 0 || ph.bytes > 4) {
		close(fd);
		return -1;
	}

 	 /*
	  * Create a new image of the proper size and 
          * associate the filename with it.
	  */

  image_ID = gimp_image_new(ph.width, ph.height, (ph.bytes >= 3) ? RGB : GRAY);
  gimp_image_set_filename(image_ID, filename);

  layer_ID = gimp_layer_new(image_ID, "Background", ph.width, ph.height,
			(ph.bytes >= 3) ? RGB_IMAGE : GRAY_IMAGE, 100, NORMAL_MODE);
	gimp_image_add_layer(image_ID, layer_ID, 0);

  drawable = gimp_drawable_get(layer_ID);
  gimp_pixel_rgn_init(&pixel_rgn, drawable, 0, 0, drawable->width,
			drawable->height, TRUE, FALSE);

	buffer = g_malloc(ph.width * ph.bytes);

	for (line = 0; line < ph.height; line++) {
		if (read(fd, buffer, ph.width * ph.bytes) != (int) (ph.width * ph.bytes)) {
			close(fd);
			g_free(buffer);
			return -1;
		}
		gimp_pixel_rgn_set_row(&pixel_rgn, (guchar *)buffer, 0, line, ph.width);
		gimp_progress_update((double) line / (double) ph.height);
	}

	g_free(buffer);
	close(fd);

	gimp_drawable_flush(drawable);

	return image_ID;
}

static gint save_image (char *filename, gint32 image_ID, gint32 drawable_ID) {
	int fd;
	BrushHeader ph;
	unsigned char *buffer;
	GDrawable *drawable;
	gint line;
	GPixelRgn pixel_rgn;
	char *temp;

	if (gimp_drawable_type(drawable_ID) != GRAY_IMAGE)
		return FALSE;

	temp = g_malloc(strlen (filename) + 10);
	sprintf(temp, "Saving %s:", filename);
	gimp_progress_init(temp);
	g_free(temp);

	drawable = gimp_drawable_get(drawable_ID);
	gimp_pixel_rgn_init(&pixel_rgn, drawable, 0, 0, drawable->width,
			drawable->height, FALSE, FALSE);

	fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, 0644);
	if (fd == -1) {
		printf("Unable to open %s\n", filename);
		return 0;
	}

	ph.header_size = g_htonl(sizeof(ph) + strlen(info.description) + 1);
	ph.version = g_htonl(2);
	ph.width = g_htonl(drawable->width);
	ph.height = g_htonl(drawable->height);
	ph.bytes = g_htonl(drawable->bpp);
	ph.magic_number = g_htonl(GBRUSH_MAGIC);
	ph.spacing = g_htonl(info.spacing);

	if (write(fd, &ph, sizeof(ph)) != sizeof(ph)) {
		close(fd);
		return 0;
	}

	if (write(fd, info.description, strlen(info.description) + 1) !=
			strlen(info.description) + 1) {
		close(fd);
		return 0;
	}

	buffer = g_malloc(drawable->width * drawable->bpp);
	if (buffer == NULL) {
		close(fd);
		return 0;
	}
	for (line = 0; line < drawable->height; line++) {
		gimp_pixel_rgn_get_row(&pixel_rgn, buffer, 0, line, drawable->width);
		if (write(fd, buffer, drawable->width * drawable->bpp) !=
				drawable->width * drawable->bpp) {
			close(fd);
			return 0;
		}
		gimp_progress_update((double) line / (double) drawable->height);
	}
	g_free(buffer);

	close(fd);

	return 1;
}


static gint save_dialog()
{
	GtkWidget *dlg;
	GtkWidget *button;
	GtkWidget *label;
	GtkWidget *entry;
	GtkWidget *table;
	gchar buffer[12];

	gtk_init();

	dlg = gimp_dialog_new("Save As Brush");
	g_signal_connect(dlg, "destroy",
			 G_CALLBACK(close_callback), NULL);

	/*  Action area  */
	gimp_dialog_add_button(dlg, "OK", G_CALLBACK(ok_callback), dlg, TRUE);
	button = gimp_dialog_add_button(dlg, "Cancel", NULL, NULL, FALSE);
	g_signal_connect_swapped(button, "clicked",
				 G_CALLBACK(gtk_window_destroy), dlg);

	/* The main table */
	/* Set its size (y, x) */
	table = gimp_table_new(2, 2, FALSE);
	gimp_container_set_border_width(table, 10);
	gimp_box_pack_start(gimp_dialog_get_vbox(dlg), table, TRUE, TRUE, 0);

	gtk_grid_set_row_spacing(GTK_GRID(table), 10);
	gtk_grid_set_column_spacing(GTK_GRID(table), 10);

	/**********************
	 * label
	 **********************/
	label = gtk_label_new("Spacing:");
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gimp_table_attach(table, label, 0, 1, 0, 1, GIMP_FILL, GIMP_FILL, 0, 0);

	/************************
	 * The entry
	 ************************/
	entry = gtk_entry_new();
	gimp_table_attach(table, entry, 1, 2, 0, 1, GIMP_EXPAND | GIMP_FILL,
			  GIMP_EXPAND | GIMP_FILL, 0, 0);
	gtk_widget_set_size_request(entry, 200, -1);
	sprintf(buffer, "%i", info.spacing);
	gtk_editable_set_text(GTK_EDITABLE(entry), buffer);
	g_signal_connect(entry, "changed",
			 G_CALLBACK(entry_callback), &info.spacing);

	/**********************
	 * label
	 **********************/
	label = gtk_label_new("Description:");
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gimp_table_attach(table, label, 0, 1, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0);

	/************************
	 * The entry
	 ************************/
	entry = gtk_entry_new();
	gimp_table_attach(table, entry, 1, 2, 1, 2, GIMP_EXPAND | GIMP_FILL,
			  GIMP_EXPAND | GIMP_FILL, 0, 0);
	gtk_widget_set_size_request(entry, 200, -1);
	gtk_editable_set_text(GTK_EDITABLE(entry), info.description);
	g_signal_connect(entry, "changed",
			 G_CALLBACK(entry_callback), info.description);

	gtk_window_present(GTK_WINDOW(dlg));

	gimp_main_loop_run();

	return run_flag;
}

static void close_callback(GtkWidget * widget, gpointer data)
{
	gimp_main_loop_quit();
}

static void ok_callback(GtkWidget * widget, gpointer data)
{
	run_flag = 1;
	gtk_window_destroy(GTK_WINDOW(data));
}

static void entry_callback(GtkWidget * widget, gpointer data)
{
	if (data == info.description)
		{
			strncpy(info.description,
				gtk_editable_get_text(GTK_EDITABLE(widget)), 255);
			info.description[255] = 0;
		}
	else if (data == &info.spacing)
		info.spacing = atoi(gtk_editable_get_text(GTK_EDITABLE(widget)));
}
