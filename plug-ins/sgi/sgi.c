/*
 * "$Id$"
 *
 *   SGI image file plug-in for the GIMP.
 *
 *   Copyright 1997-1998 Michael Sweet (mike@easysw.com)
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms of the GNU General Public License as published by the Free
 *   Software Foundation; either version 2 of the License, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *   for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Contents:
 *
 *   main()                      - Main entry - just call gimp_main()...
 *   query()                     - Respond to a plug-in query...
 *   run()                       - Run the plug-in...
 *   load_image()                - Load a PNG image into a new image window.
 *   save_image()                - Save the specified image to a PNG file.
 *   save_close_callback()       - Close the save dialog window.
 *   save_ok_callback()          - Destroy the save dialog and save the image.
 *   save_compression_callback() - Update the image compression level.
 *   save_dialog()               - Pop up the save dialog.
 *
 * Revision History:
 *
 *   $Log$
 *   Revision 1.8.2.1  1998/06/06 23:28:14  yosh
 *   * updated despeckle, png, sgi, and sharpen
 *
 *   -Yosh
 *
 *   Revision 1.9  1998/06/06 23:22:19  yosh
 *   * adding Lighting plugin
 *
 *   * updated despeckle, png, sgi, and sharpen
 *
 *   -Yosh
 *
 *   Revision 1.5  1998/05/17 16:01:33  mike
 *   Removed signal handler stuff used for debugging.
 *   Added gtk_rc_parse().
 *   Removed extra variables.
 *
 *   Revision 1.4  1998/04/23  17:40:49  mike
 *   Updated to support 16-bit <unsigned> image data.
 *
 *   Revision 1.3  1997/11/14  17:17:59  mike
 *   Updated to dynamically allocate return params in the run() function.
 *   Added warning message about advanced RLE compression not being supported
 *   by SGI.
 *
 *   Revision 1.2  1997/07/25  20:44:05  mike
 *   Fixed image_load_sgi load error bug (causes GIMP hang/crash).
 *
 *   Revision 1.1  1997/06/18  00:55:28  mike
 *   Initial revision
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "sgi.h"		/* SGI image library definitions */

#include <gtk/gtk.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>


/*
 * Constants...
 */

#define PLUG_IN_VERSION		"1.1.1 - 17 May 1998"


/*
 * Local functions...
 */

static void	query(void);
static void	run(char *, int, GParam *, int *, GParam **);
static gint32	load_image(char *);
static gint	save_image (char *, gint32, gint32);
static gint	save_dialog(void);
static void	save_close_callback(GtkWidget *, gpointer);
static void	save_ok_callback(GtkWidget *, gpointer);
static void	save_compression_callback(GtkWidget *, gpointer);


/*
 * Globals...
 */

GPlugInInfo	PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

int		compression = SGI_COMP_RLE;
int		runme = FALSE;


/*
 * 'main()' - Main entry - just call gimp_main()...
 */

int
main(int  argc,		/* I - Number of command-line args */
     char *argv[])	/* I - Command-line args */
{
  return (gimp_main(argc, argv));
}


/*
 * 'query()' - Respond to a plug-in query...
 */

static void
query(void)
{
  static GParamDef	load_args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_STRING, "filename", "The name of the file to load" },
    { PARAM_STRING, "raw_filename", "The name of the file to load" },
  };
  static GParamDef	load_return_vals[] =
  {
    { PARAM_IMAGE, "image", "Output image" },
  };
  static int		nload_args = sizeof (load_args) / sizeof (load_args[0]);
  static int		nload_return_vals = sizeof (load_return_vals) / sizeof (load_return_vals[0]);
  static GParamDef	save_args[] =
  {
    { PARAM_INT32,	"run_mode",	"Interactive, non-interactive" },
    { PARAM_IMAGE,	"image",	"Input image" },
    { PARAM_DRAWABLE,	"drawable",	"Drawable to save" },
    { PARAM_STRING,	"filename",	"The name of the file to save the image in" },
    { PARAM_STRING,	"raw_filename",	"The name of the file to save the image in" },
    { PARAM_INT32,	"compression",	"Compression level (0 = none, 1 = RLE, 2 = ARLE)" }
  };
  static int		nsave_args = sizeof (save_args) / sizeof (save_args[0]);


  gimp_install_procedure("file_sgi_load",
      "Loads files in SGI image file format",
      "This plug-in loads SGI image files.",
      "Michael Sweet <mike@easysw.com>",
      "Copyright 1997-1998 by Michael Sweet",
      PLUG_IN_VERSION,
      "<Load>/SGI", NULL, PROC_PLUG_IN, nload_args, nload_return_vals,
      load_args, load_return_vals);

  gimp_install_procedure("file_sgi_save",
      "Saves files in SGI image file format",
      "This plug-in saves SGI image files.",
      "Michael Sweet <mike@easysw.com>",
      "Copyright 1997-1998 by Michael Sweet",
      PLUG_IN_VERSION,
      "<Save>/SGI", "RGB*,GRAY*", PROC_PLUG_IN, nsave_args, 0, save_args, NULL);

  gimp_register_magic_load_handler("file_sgi_load", "rgb,bw,sgi,icon", "", "0,short,474");
  gimp_register_save_handler("file_sgi_save", "rgb,bw,sgi,icon", "");
}


/*
 * 'run()' - Run the plug-in...
 */

static void
run(char   *name,		/* I - Name of filter program. */
    int    nparams,		/* I - Number of parameters passed in */
    GParam *param,		/* I - Parameter values */
    int    *nreturn_vals,	/* O - Number of return values */
    GParam **return_vals)	/* O - Return values */
{
  gint32	image_ID;	/* ID of loaded image */
  GParam	*values;	/* Return values */


 /*
  * Initialize parameter data...
  */

  values = g_new(GParam, 2);

  values[0].type          = PARAM_STATUS;
  values[0].data.d_status = STATUS_SUCCESS;

  *return_vals = values;

 /*
  * Load or save an image...
  */

  if (strcmp(name, "file_sgi_load") == 0)
  {
    *nreturn_vals = 2;

    image_ID = load_image(param[1].data.d_string);

    if (image_ID != -1)
    {
      values[1].type         = PARAM_IMAGE;
      values[1].data.d_image = image_ID;
    }
    else
      values[0].data.d_status = STATUS_EXECUTION_ERROR;
  }
  else if (strcmp (name, "file_sgi_save") == 0)
  {
    *nreturn_vals = 1;

    switch (param[0].data.d_int32)
    {
      case RUN_INTERACTIVE :
         /*
          * Possibly retrieve data...
          */

          gimp_get_data("file_sgi_save", &compression);

         /*
          * Then acquire information with a dialog...
          */

          if (!save_dialog())
            return;
          break;

      case RUN_NONINTERACTIVE :
         /*
          * Make sure all the arguments are there!
          */

          if (nparams != 6)
            values[0].data.d_status = STATUS_CALLING_ERROR;
          else
          {
            compression = param[5].data.d_int32;

            if (compression < 0 || compression > 2)
              values[0].data.d_status = STATUS_CALLING_ERROR;
          };
          break;

      case RUN_WITH_LAST_VALS :
         /*
          * Possibly retrieve data...
          */

          gimp_get_data("file_sgi_save", &compression);
          break;

      default :
          break;
    };

    if (values[0].data.d_status == STATUS_SUCCESS)
    {
      if (save_image(param[3].data.d_string, param[1].data.d_int32,
                     param[2].data.d_int32))
        gimp_set_data("file_sgi_save", &compression, sizeof(compression));
      else
	values[0].data.d_status = STATUS_EXECUTION_ERROR;
    };
  }
  else
    values[0].data.d_status = STATUS_EXECUTION_ERROR;
}


/*
 * 'load_image()' - Load a PNG image into a new image window.
 */

static gint32
load_image(char *filename)	/* I - File to load */
{
  int		i,		/* Looping var */
		x,		/* Current X coordinate */
		y,		/* Current Y coordinate */
		image_type,	/* Type of image */
		layer_type,	/* Type of drawable/layer */
		tile_height,	/* Height of tile in GIMP */
		count;		/* Count of rows to put in image */
  sgi_t		*sgip;		/* File pointer */
  gint32	image,		/* Image */
		layer;		/* Layer */
  GDrawable	*drawable;	/* Drawable for layer */
  GPixelRgn	pixel_rgn;	/* Pixel region for layer */
  guchar	**pixels,	/* Pixel rows */
		*pixel,		/* Pixel data */
		*pptr;		/* Current pixel */
  unsigned short **rows;	/* SGI image data */
  char		progress[255];	/* Title for progress display... */


 /*
  * Open the file for reading...
  */

  sgip = sgiOpen(filename, SGI_READ, 0, 0, 0, 0, 0);
  if (sgip == NULL)
  {
    g_print("can't open image file\n");
    gimp_quit();
  };

  if (strrchr(filename, '/') != NULL)
    sprintf(progress, "Loading %s:", strrchr(filename, '/') + 1);
  else
    sprintf(progress, "Loading %s:", filename);

  gimp_progress_init(progress);

 /*
  * Get the image dimensions and create the image...
  */

  switch (sgip->zsize)
  {
    case 1 :	/* Grayscale */
        image_type = GRAY;
        layer_type = GRAY_IMAGE;
        break;

    case 2 :	/* Grayscale + alpha */
        image_type = GRAY;
        layer_type = GRAYA_IMAGE;
        break;

    case 3 :	/* RGB */
        image_type = RGB;
        layer_type = RGB_IMAGE;
        break;

    case 4 :	/* RGBA */
        image_type = RGB;
        layer_type = RGBA_IMAGE;
        break;
  };

  image = gimp_image_new(sgip->xsize, sgip->ysize, image_type);
  if (image == -1)
  {
    g_print("can't allocate new image\n");
    gimp_quit();
  };

  gimp_image_set_filename(image, filename);

 /*
  * Create the "background" layer to hold the image...
  */

  layer = gimp_layer_new(image, "Background", sgip->xsize, sgip->ysize,
                         layer_type, 100, NORMAL_MODE);
  gimp_image_add_layer(image, layer, 0);

 /*
  * Get the drawable and set the pixel region for our load...
  */

  drawable = gimp_drawable_get(layer);

  gimp_pixel_rgn_init(&pixel_rgn, drawable, 0, 0, drawable->width,
                      drawable->height, TRUE, FALSE);

 /*
  * Temporary buffers...
  */

  tile_height = gimp_tile_height();
  pixel       = g_new(guchar, tile_height * sgip->xsize * sgip->zsize);
  pixels      = g_new(guchar *, tile_height);

  for (i = 0; i < tile_height; i ++)
    pixels[i] = pixel + sgip->xsize * sgip->zsize * i;

  rows    = g_new(unsigned short *, sgip->zsize);
  rows[0] = g_new(unsigned short, sgip->xsize * sgip->zsize);

  for (i = 1; i < sgip->zsize; i ++)
    rows[i] = rows[0] + i * sgip->xsize;

 /*
  * Load the image...
  */

  for (y = 0, count = 0;
       y < sgip->ysize;
       y ++, count ++)
  {
    if (count >= tile_height)
    {
      gimp_pixel_rgn_set_rect(&pixel_rgn, pixel, 0, y - count, drawable->width, count);
      count = 0;

      gimp_progress_update((double)y / (double)sgip->ysize);
    };

    for (i = 0; i < sgip->zsize; i ++)
      if (sgiGetRow(sgip, rows[i], sgip->ysize - 1 - y, i) < 0)
        printf("sgiGetRow(sgip, rows[i], %d, %d) failed!\n",
               sgip->ysize - 1 - y, i);

    if (sgip->bpp == 1)
    {
     /*
      * 8-bit (unsigned) pixels...
      */

      for (x = 0, pptr = pixels[count]; x < sgip->xsize; x ++)
	for (i = 0; i < sgip->zsize; i ++, pptr ++)
	  *pptr = rows[i][x];
    }
    else
    {
     /*
      * 16-bit (unsigned) pixels...
      */

      for (x = 0, pptr = pixels[count]; x < sgip->xsize; x ++)
	for (i = 0; i < sgip->zsize; i ++, pptr ++)
	  *pptr = rows[i][x] >> 8;
    };
  };

 /*
  * Do the last n rows (count always > 0)
  */

  gimp_pixel_rgn_set_rect(&pixel_rgn, pixel, 0, y - count, drawable->width, count);

 /*
  * Done with the file...
  */

  sgiClose(sgip);

  free(pixel);
  free(pixels);
  free(rows[0]);
  free(rows);

 /*
  * Update the display...
  */

  gimp_drawable_flush(drawable);
  gimp_drawable_detach(drawable);

  return (image);
}


/*
 * 'save_image()' - Save the specified image to a PNG file.
 */

static gint
save_image(char   *filename,	/* I - File to save to */
	   gint32 image_ID,	/* I - Image to save */
	   gint32 drawable_ID)	/* I - Current drawable */
{
  int		i, j,		/* Looping var */
		x,		/* Current X coordinate */
		y,		/* Current Y coordinate */
		image_type,	/* Type of image */
		layer_type,	/* Type of drawable/layer */
		tile_height,	/* Height of tile in GIMP */
		count,		/* Count of rows to put in image */
		zsize;		/* Number of channels in file */
  sgi_t		*sgip;		/* File pointer */
  GDrawable	*drawable;	/* Drawable for layer */
  GPixelRgn	pixel_rgn;	/* Pixel region for layer */
  guchar	**pixels,	/* Pixel rows */
		*pixel,		/* Pixel data */
		*pptr;		/* Current pixel */
  unsigned short **rows;	/* SGI image data */
  char		progress[255];	/* Title for progress display... */


 /*
  * Get the drawable for the current image...
  */

  drawable = gimp_drawable_get(drawable_ID);

  gimp_pixel_rgn_init(&pixel_rgn, drawable, 0, 0, drawable->width,
                      drawable->height, FALSE, FALSE);

  switch (gimp_drawable_type(drawable_ID))
  {
    case GRAY_IMAGE :
        zsize = 1;
        break;
    case GRAYA_IMAGE :
        zsize = 2;
        break;
    case RGB_IMAGE :
        zsize = 3;
        break;
    case RGBA_IMAGE :
        zsize = 4;
        break;
  };

 /*
  * Open the file for writing...
  */

  sgip = sgiOpen(filename, SGI_WRITE, compression, 1, drawable->width,
                 drawable->height, zsize);
  if (sgip == NULL)
  {
    g_print("can't create image file\n");
    gimp_quit();
  };

  if (strrchr(filename, '/') != NULL)
    sprintf(progress, "Saving %s:", strrchr(filename, '/') + 1);
  else
    sprintf(progress, "Saving %s:", filename);

  gimp_progress_init(progress);

 /*
  * Allocate memory for "tile_height" rows...
  */

  tile_height = gimp_tile_height();
  pixel       = g_new(guchar, tile_height * drawable->width * zsize);
  pixels      = g_new(guchar *, tile_height);

  for (i = 0; i < tile_height; i ++)
    pixels[i]= pixel + drawable->width * zsize * i;

  rows    = g_new(unsigned short *, sgip->zsize);
  rows[0] = g_new(unsigned short, sgip->xsize * sgip->zsize);

  for (i = 1; i < sgip->zsize; i ++)
    rows[i] = rows[0] + i * sgip->xsize;

 /*
  * Save the image...
  */

  for (y = 0; y < drawable->height; y += count)
  {
   /*
    * Grab more pixel data...
    */

    if ((y + tile_height) >= drawable->height)
      count = drawable->height - y;
    else
      count = tile_height;

    gimp_pixel_rgn_get_rect(&pixel_rgn, pixel, 0, y, drawable->width, count);

   /*
    * Convert to shorts and write each color plane separately...
    */

    for (i = 0, pptr = pixels[0]; i < count; i ++)
    {
      for (x = 0; x < drawable->width; x ++)
        for (j = 0; j < zsize; j ++, pptr ++)
          rows[j][x] = *pptr;

      for (j = 0; j < zsize; j ++)
        sgiPutRow(sgip, rows[j], drawable->height - 1 - y - i, j);
    };

    gimp_progress_update((double)y / (double)drawable->height);
  };

 /*
  * Done with the file...
  */

  sgiClose(sgip);

  free(pixel);
  free(pixels);
  free(rows[0]);
  free(rows);

  return (1);
}


/*
 * 'save_close_callback()' - Close the save dialog window.
 */

static void
save_close_callback(GtkWidget *w,	/* I - Close button */
                    gpointer  data)	/* I - Callback data */
{
  gimp_main_loop_quit();
}


/*
 * 'save_ok_callback()' - Destroy the save dialog and save the image.
 */

static void
save_ok_callback(GtkWidget *w,		/* I - OK button */
                 gpointer  data)	/* I - Callback data */
{
  runme = TRUE;

  gtk_window_destroy(GTK_WINDOW(data));
}


/*
 * 'save_compression_callback()' - Update the image compression level.
 */

static void
save_compression_callback(GtkWidget *w,		/* I - Compression button */
                          gpointer  data)	/* I - Callback data */
{
  if (gtk_check_button_get_active(GTK_CHECK_BUTTON(w)))
    compression = GPOINTER_TO_INT(data);
}


/*
 * 'save_dialog()' - Pop up the save dialog.
 */

static gint
save_dialog(void)
{
  int		i;		/* Looping var */
  GtkWidget	*dlg,		/* Dialog window */
		*button,	/* OK/cancel/compression buttons */
		*frame,		/* Frame for dialog */
		*vbox;		/* Box for compression types */
  GtkWidget	*group;		/* Button grouping for compression */
  static char	*types[] =	/* Compression types... */
		{
		  "No Compression",
		  "RLE Compression",
		  "Aggressive RLE\n(Not supported by SGI)"
		};


  gtk_init();

 /*
  * Open a dialog window...
  */

  dlg = gimp_dialog_new("SGI - " PLUG_IN_VERSION);
  g_signal_connect(dlg, "destroy",
                   G_CALLBACK(save_close_callback), NULL);

 /*
  * OK/cancel buttons...
  */

  gimp_dialog_add_button(dlg, "OK", G_CALLBACK(save_ok_callback), dlg, TRUE);

  button = gimp_dialog_add_button(dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped(button, "clicked",
                           G_CALLBACK(gtk_window_destroy), dlg);

 /*
  * Compression type...
  */

  frame = gtk_frame_new("Parameter Settings");
  gimp_container_set_border_width(frame, 10);
  gimp_box_pack_start(gimp_dialog_get_vbox(dlg), frame, TRUE, TRUE, 0);

  vbox = gimp_vbox_new(FALSE, 0);
  gimp_container_set_border_width(vbox, 4);
  gtk_frame_set_child(GTK_FRAME(frame), vbox);

  group = NULL;

  for (i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i ++)
  {
    button = gimp_radio_button_new(group, types[i]);
    group  = button;
    if (i == compression)
      gtk_check_button_set_active(GTK_CHECK_BUTTON(button), TRUE);

    g_signal_connect(button, "toggled",
		     G_CALLBACK(save_compression_callback),
		     GINT_TO_POINTER(i));
    gtk_box_append(GTK_BOX(vbox), button);
  };

 /*
  * Show everything and go...
  */

  gtk_window_present(GTK_WINDOW(dlg));

  gimp_main_loop_run();

  return (runme);
}

/*
 * End of "$Id$".
 */
