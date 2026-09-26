/*
 * "$Id$"
 *
 *   Portable Network Graphics (PNG) plug-in for The GIMP -- an image
 *   manipulation program
 *
 *   Copyright 1997-1998 Michael Sweet (mike@easysw.com) and
 *   Daniel Skarda (0rfelyus@atrey.karlin.mff.cuni.cz).
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
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
 *   save_interlace_update()     - Update the interlacing option.
 *   save_dialog()               - Pop up the save dialog.
 *
 * Revision History:
 *
 *   $Log$
 *   Revision 1.7.2.4  1999/01/02 23:50:31  yosh
 *   Doh, thinko.
 *
 *   -Yosh
 *
 *   Revision 1.7.2.3  1999/01/02 23:11:53  yosh
 *   * ltconfig: cases for Unixware 2.1.2 (from Geoff Clare)
 *   and BSD/OS 4.0 (from Chris P. Ross)
 *
 *   * app/Makefile.am
 *   * plug-ins/script-fu/scripts/Makefile.am: use -DREGEX_MALLOC,
 *   seems to be more portable
 *
 *   * plug-ins/png/png.c: use a default gamma of 2.2 when gamma
 *   correction isn't enabled
 *
 *   -Yosh
 *
 *   Revision 1.7.2.2  1998/11/09 02:26:44  yosh
 *   * Makefile.am
 *   * configure.in: check for GTK+ 1.0.3 or higher, we use stuff
 *   from it. Add a convenience configure option for me.
 *
 *   * gimptool.in: sync with 1.1
 *
 *   * tile_swap.c: ok, a further attempt to get rid of a bunch o'
 *   dialogs
 *
 *   * docs/Makefile.am
 *   * docs/white-paper/Makefile.am: helpers for make dist
 *
 *   * libgimp/gimp.c: match header declaration
 *
 *   * checkerboard.c: avoid a FP exception in psychobilly mode
 *
 *   * plug-ins/cubism/cubism.c
 *   * plug-ins/mosaic/mosaic.c: speedups from 1.1
 *
 *   * plug-ins/png/png.c: bugfix for indexed image, default to level 6
 *   compression
 *
 *   -Yosh
 *
 *   Revision 1.7.2.1  1998/06/06 23:28:13  yosh
 *   * updated despeckle, png, sgi, and sharpen
 *
 *   -Yosh
 *
 *   Revision 1.8  1998/06/06 23:22:17  yosh
 *   * adding Lighting plugin
 *
 *   * updated despeckle, png, sgi, and sharpen
 *
 *   -Yosh
 *
 *   Revision 1.14  1998/05/17 15:54:15  mike
 *   Added gtk_rc_parse(), removed unnecessary variables.
 *
 *   Revision 1.13  1998/04/02  16:00:13  mike
 *   Fixed bug in run() - was looking for 6 arguments and not 7 as advertised.
 *
 *   Revision 1.12  1998/01/04  14:10:09  mike
 *   Fixed paletted image saving bug - wasn't correctly storing the number of
 *   colors and didn't flag the palette as valid.
 *   Removed INDEXEDA support since the current PNG library doesn't support it.
 *
 *   Revision 1.11  1997/11/14  17:17:59  mike
 *   Updated to dynamically allocate return params in the run() function.
 *
 *   Revision 1.10  1997/10/17  13:55:55  mike
 *   Updated author/contact info.
 *   Added typecast for palette information.
 *
 *   Revision 1.9  1997/09/29  19:18:59  mike
 *   Now check for return value of fopen() in case the user picks a file that
 *   doesn't exist or isn't writable.
 *
 *   Revision 1.8  1997/09/29  13:42:13  mike
 *   Updated "magic" string for PNG detection (thanks to Nicholas Lamb)
 *
 *   Revision 1.7  1997/07/25  20:45:24  mike
 *   Fixed image_load_sgi load error bug (causes GIMP hang/crash).
 *
 *   Revision 1.6  1997/06/11  17:49:07  mike
 *   Updated docos for release.
 *
 *   Revision 1.5  1997/06/11  17:39:28  mike
 *   Fixed a few memory leaks - not critical, since this plug-in isn't running
 *   all the time...
 *
 *   Merged with work done by Daniel Skarda - now support compression level
 *   and interlacing on saves.
 *
 *   Fixed indexed image handling (whoops, image types and drawable types are
 *   not the same... d'oh!)
 *
 *   Revision 1.4  1997/06/08  19:34:43  mike
 *   Fixed bug in load_image() and save_image() - would crash if filename
 *   didn't have a '/' in it...
 *
 *   Revision 1.3  1997/06/08  16:34:33  mike
 *   Added actual code to save_image().
 *   Updated docos.
 *
 *   Revision 1.2  1997/06/08  16:02:52  mike
 *   Updated registration to get rid of load handler errors.
 *
 *   Revision 1.1  1997/06/08  15:10:08  mike
 *   Initial revision
 */

#include <stdio.h>
#include <stdlib.h>

#include <png.h>		/* PNG library definitions */

#include <gtk/gtk.h>
#include <libgimp/gimp.h>
#include "libgimp/gimpui.h"


/*
 * Constants...
 */

#define PLUG_IN_VERSION		"1.1.6 - 17 May 1998"
#define SCALE_WIDTH		125

#define DEFAULT_GAMMA		2.20

/*
 * Structures...
 */

typedef struct
{
  gint	interlaced;
  gint	compression_level;
} PngSaveVals;


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
static void	save_compression_update(GtkAdjustment *, gpointer);
static void	save_interlace_update(GtkWidget *, gpointer);


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

PngSaveVals	pngvals = 
{
  FALSE,
  6
};

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
    { PARAM_INT32,	"interlace",	"Save with interlacing option enabled" },
    { PARAM_INT32,	"compression",	"Compression level" }
  };
  static int		nsave_args = sizeof (save_args) / sizeof (save_args[0]);


  gimp_install_procedure("file_png_load",
      "Loads files in PNG file format",
      "This plug-in loads Portable Network Graphics (PNG) files.",
      "Michael Sweet <mike@easysw.com>, Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>",
      "Michael Sweet <mike@easysw.com>, Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>",
      PLUG_IN_VERSION,
      "<Load>/PNG", NULL, PROC_PLUG_IN, nload_args, nload_return_vals,
      load_args, load_return_vals);

  gimp_install_procedure("file_png_save",
      "Saves files in PNG file format",
      "This plug-in saves Portable Network Graphics (PNG) files.",
      "Michael Sweet <mike@easysw.com>, Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>",
      "Michael Sweet <mike@easysw.com>, Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>",
      PLUG_IN_VERSION,
      "<Save>/PNG", "RGB*,GRAY*,INDEXED*", PROC_PLUG_IN, nsave_args, 0, save_args, NULL);

  gimp_register_magic_load_handler("file_png_load", "png", "", "0,string,\211PNG\r\n\032\n");
  gimp_register_save_handler("file_png_save", "png", "");
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

  *return_vals  = values;

 /*
  * Load or save an image...
  */

  if (strcmp(name, "file_png_load") == 0)
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
  else if (strcmp (name, "file_png_save") == 0)
  {
    *nreturn_vals = 1;

    switch (param[0].data.d_int32)
    {
      case RUN_INTERACTIVE :
         /*
          * Possibly retrieve data...
          */

          gimp_get_data("file_png_save", &pngvals);

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

          if (nparams != 7)
            values[0].data.d_status = STATUS_CALLING_ERROR;
          else
          {
            pngvals.interlaced        = param[5].data.d_int32;
            pngvals.compression_level = param[6].data.d_int32;

            if (pngvals.compression_level < 0 ||
                pngvals.compression_level > 9)
              values[0].data.d_status = STATUS_CALLING_ERROR;
          };
          break;

      case RUN_WITH_LAST_VALS :
         /*
          * Possibly retrieve data...
          */

          gimp_get_data("file_png_save", &pngvals);
          break;

      default :
          break;
    };

    if (values[0].data.d_status == STATUS_SUCCESS)
    {
      if (save_image(param[3].data.d_string, param[1].data.d_int32,
                     param[2].data.d_int32))
        gimp_set_data("file_png_save", &pngvals, sizeof(pngvals));
      else
	values[0].data.d_status = STATUS_EXECUTION_ERROR;
    };
  }
  else
    values[0].data.d_status = STATUS_EXECUTION_ERROR;
}


/*
 * Palette images with transparency: libpng hands out one index per pixel,
 * the layer holds index and alpha.  Each row buffer is 2 * width bytes; the
 * indices sit in its first half.
 */

static void
png_add_palette_alpha(guchar       **rows,	/* I/O - Row buffers */
                      int            num,	/* I - Number of rows */
                      png_uint_32    width,	/* I - Pixels per row */
                      const guchar  *alpha)	/* I - Alpha per index */
{
  int		y;
  png_uint_32	x;

  for (y = 0; y < num; y ++)
  {
    guchar *row = rows[y];

    /*  back to front, so nothing is overwritten before it is read  */
    for (x = width; x > 0; x --)
    {
      guchar index = row[x - 1];

      row[(x - 1) * 2]     = index;
      row[(x - 1) * 2 + 1] = alpha[index];
    }
  }
}

/*
 * The reverse, for the later passes of an interlaced image: the rows already
 * loaded go back to one index per pixel for libpng to fill in.
 */

static void
png_pack_indices(guchar       *pixel,	/* I - First row */
                 guchar      **rows,	/* I/O - Row buffers */
                 int           num,	/* I - Number of rows */
                 png_uint_32   width)	/* I - Pixels per row */
{
  int		y;
  png_uint_32	x;

  for (y = 0; y < num; y ++)
  {
    guchar *row = rows[y];

    for (x = 0; x < width; x ++)
      row[x] = row[x * 2];
  }
}


/*
 * 'load_image()' - Load a PNG image into a new image window.
 */

static gint32
load_image(char *filename)	/* I - File to load */
{
  int		i,		/* Looping var */
		bpp,		/* Bytes per pixel */
		layer_bpp,	/* Bytes per pixel in the layer */
		image_type,	/* Type of image */
		layer_type,	/* Type of drawable/layer */
		num_passes,	/* Number of interlace passes in file */
		pass,		/* Current pass in file */
		tile_height,	/* Height of tile in GIMP */
		begin,		/* Beginning tile row */
		end,		/* Ending tile row */
		num;		/* Number of rows to load */
  FILE		*fp;		/* File pointer */
  gint32	image,		/* Image */
		layer;		/* Layer */
  GDrawable	*drawable;	/* Drawable for layer */
  GPixelRgn	pixel_rgn;	/* Pixel region for layer */
  png_structp	pp;		/* PNG read pointer */
  png_infop	info;		/* PNG info pointers */
  guchar	** volatile pixels = NULL,	/* Pixel rows */
		* volatile pixel = NULL;	/* Pixel data */
  char		*progress;	/* Title for progress display... */
  gchar		*basename;
  png_uint_32	width, height;	/* Image dimensions */
  int		bit_depth, color_type;
  png_color_8p	sig_bit;
  png_colorp	palette;
  int		num_palette;
  png_bytep	trans = NULL;	/* Palette transparency (tRNS) */
  int		num_trans = 0;
  guchar	trans_alpha[256];	/* Alpha of each palette entry */


 /*
  * Open the file and initialize the PNG read "engine"...
  */

  fp = fopen(filename, "rb");
  if (fp == NULL)
    return (-1);

  pp   = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  info = pp ? png_create_info_struct(pp) : NULL;
  if (info == NULL)
  {
    png_destroy_read_struct(pp ? &pp : NULL, NULL, NULL);
    fclose(fp);
    return (-1);
  }

  if (setjmp(png_jmpbuf(pp)))
  {
    g_message("PNG: error while reading %s", filename);
    png_destroy_read_struct(&pp, &info, NULL);
    g_free(pixel);
    g_free(pixels);
    fclose(fp);
    return (-1);
  }

  png_init_io(pp, fp);

  basename = g_path_get_basename(filename);
  progress = g_strdup_printf("Loading %s:", basename);
  gimp_progress_init(progress);
  g_free(progress);
  g_free(basename);

 /*
  * Get the image dimensions and create the image...
  */

  png_read_info(pp, info);

  width      = png_get_image_width(pp, info);
  height     = png_get_image_height(pp, info);
  bit_depth  = png_get_bit_depth(pp, info);
  color_type = png_get_color_type(pp, info);

 /*
  * I have no idea why this used to be the way it was, luckily
  * most people don't use 2bit or 4bit indexed images with PNG
  */

  if (bit_depth < 8)
  {
    png_set_packing(pp);
    if (color_type != PNG_COLOR_TYPE_PALETTE) {
      png_set_expand(pp);

      if (png_get_sBIT(pp, info, &sig_bit) & PNG_INFO_sBIT)
        png_set_shift(pp, sig_bit);
    }
  }
  else if (bit_depth == 16)
  {
    /*  The GIMP works in 8 bits per channel; round, don't truncate.  */
#ifdef PNG_READ_SCALE_16_TO_8_SUPPORTED
    png_set_scale_16(pp);
#else
    png_set_strip_16(pp);
#endif
  }

 /*
  * Transparency.  A palette image keeps its indices and gets an alpha
  * channel made from its tRNS table below; a grey or RGB image with a
  * transparent colour key (tRNS) gets a real alpha channel.
  */

  if (png_get_valid(pp, info, PNG_INFO_tRNS))
  {
    if (color_type == PNG_COLOR_TYPE_PALETTE)
    {
      png_get_tRNS(pp, info, &trans, &num_trans, NULL);
      memset(trans_alpha, 255, sizeof (trans_alpha));
      for (i = 0; i < num_trans && i < 256; i ++)
        trans_alpha[i] = trans[i];
    }
    else if (bit_depth >= 8)
      png_set_tRNS_to_alpha(pp);
  }

 /*
  * Turn on interlace handling...
  */

  if (png_get_interlace_type(pp, info) != PNG_INTERLACE_NONE)
    num_passes = png_set_interlace_handling(pp);
  else
    num_passes = 1;

  png_read_update_info(pp, info);

 /*
  * The transforms above may change the colour type (e.g. png_set_expand
  * turns a tRNS chunk into an alpha channel), so size the row buffers
  * from the updated info rather than from the file header.
  */

  color_type = png_get_color_type(pp, info);

  if (width == 0 || height == 0 || width > 262144 || height > 262144)
  {
    g_message("PNG: %s has unsupported image dimensions", filename);
    png_destroy_read_struct(&pp, &info, NULL);
    fclose(fp);
    return (-1);
  }

  bpp = 1;
  image_type = GRAY;
  layer_type = GRAY_IMAGE;

  switch (color_type)
  {
    case PNG_COLOR_TYPE_RGB :		/* RGB */
        bpp        = 3;
        image_type = RGB;
        layer_type = RGB_IMAGE;
        break;

    case PNG_COLOR_TYPE_RGB_ALPHA :	/* RGBA */
        bpp        = 4;
        image_type = RGB;
        layer_type = RGBA_IMAGE;
        break;

    case PNG_COLOR_TYPE_GRAY :		/* Grayscale */
        bpp        = 1;
        image_type = GRAY;
        layer_type = GRAY_IMAGE;
        break;

    case PNG_COLOR_TYPE_GRAY_ALPHA :	/* Grayscale + alpha */
        bpp        = 2;
        image_type = GRAY;
        layer_type = GRAYA_IMAGE;
        break;

    case PNG_COLOR_TYPE_PALETTE :	/* Indexed */
        bpp        = 1;
        image_type = INDEXED;
        layer_type = (num_trans > 0) ? INDEXEDA_IMAGE : INDEXED_IMAGE;
        break;
  };

  if (png_get_rowbytes(pp, info) > (png_size_t) width * bpp)
  {
    g_message("PNG: unsupported pixel format in %s", filename);
    png_destroy_read_struct(&pp, &info, NULL);
    fclose(fp);
    return (-1);
  }

  image = gimp_image_new(width, height, image_type);
  if (image == -1)
  {
    g_print("can't allocate new image\n");
    gimp_quit();
  };

  gimp_image_set_filename(image, filename);

 /*
  * Load the colormap as necessary...
  */

  if ((color_type & PNG_COLOR_MASK_PALETTE) &&
      png_get_PLTE(pp, info, &palette, &num_palette))
    gimp_image_set_cmap(image, (guchar *)palette, num_palette);

 /*
  * Create the "background" layer to hold the image...
  */

  layer = gimp_layer_new(image, "Background", width, height,
                         layer_type, 100, NORMAL_MODE);
  gimp_image_add_layer(image, layer, 0);

 /*
  * Get the drawable and set the pixel region for our load...
  */

  drawable = gimp_drawable_get(layer);

  gimp_pixel_rgn_init(&pixel_rgn, drawable, 0, 0, drawable->width,
                      drawable->height, TRUE, FALSE);

 /*
  * Temporary buffer...
  */

  tile_height = gimp_tile_height ();

  /*  A palette image with transparency is stored as index + alpha, two
   *  bytes to libpng's one.
   */
  layer_bpp   = (layer_type == INDEXEDA_IMAGE) ? 2 : bpp;
  pixel       = g_new(guchar, (gsize) tile_height * width * layer_bpp);
  pixels      = g_new(guchar *, tile_height);

  for (i = 0; i < tile_height; i ++)
    pixels[i] = pixel + (gsize) width * layer_bpp * i;

  for (pass = 0; pass < num_passes; pass ++)
  {
   /*
    * This works if you are only reading one row at a time...
    */

    for (begin = 0, end = tile_height;
         begin < height;
         begin += tile_height, end += tile_height)
    {
      if (end > height)
        end = height;

      num = end - begin;
	
      if (pass != 0) /* to handle interlaced PiNGs */
      {
        gimp_pixel_rgn_get_rect(&pixel_rgn, pixel, 0, begin, drawable->width, num);
        if (layer_bpp == 2)
          png_pack_indices(pixel, pixels, num, width);
      }

      png_read_rows(pp, pixels, NULL, num);

      if (layer_bpp == 2)
        png_add_palette_alpha(pixels, num, width, trans_alpha);

      gimp_pixel_rgn_set_rect(&pixel_rgn, pixel, 0, begin, drawable->width, num);

      gimp_progress_update(((double)pass + (double)end / (double)height) /
                           (double)num_passes);
    };
  };

 /*
  * Done with the file...
  */

  png_read_end(pp, info);
  png_destroy_read_struct(&pp, &info, NULL);

  g_free(pixel);
  g_free(pixels);

  fclose(fp);

 /*
  * Update the display...
  */

  gimp_drawable_flush(drawable);
  gimp_drawable_detach(drawable);

  return (image);
}


/*
 * An INDEXEDA image's transparent pixels need a palette entry to point at.
 * With room in the palette that is a new entry at the end; otherwise it is
 * an entry no opaque pixel uses, or entry 0 as a last resort.
 */

static int
png_find_transparent_index(GPixelRgn *region,	/* I - The drawable's pixels */
                           GDrawable *drawable,	/* I - The drawable */
                           gint      *num_colors)	/* I/O - Palette size */
{
  gboolean	used[256];
  guchar	*row;
  int		x, y, i;

  if (*num_colors < 256)
    return (*num_colors)++;

  memset(used, 0, sizeof (used));
  row = g_new(guchar, drawable->width * 2);

  for (y = 0; y < drawable->height; y ++)
  {
    gimp_pixel_rgn_get_row(region, row, 0, y, drawable->width);
    for (x = 0; x < drawable->width; x ++)
      if (row[x * 2 + 1] >= 128)
        used[row[x * 2]] = TRUE;
  }

  g_free(row);

  for (i = 0; i < 256; i ++)
    if (!used[i])
      return i;

  return 0;
}


/*
 * 'save_image()' - Save the specified image to a PNG file.
 */

static gint
save_image(char   *filename,	/* I - File to save to */
	   gint32 image_ID,	/* I - Image to save */
	   gint32 drawable_ID)	/* I - Current drawable */
{
  int		i,		/* Looping var */
		bpp,		/* Bytes per pixel */
		type,		/* Type of drawable/layer */
		num_passes,	/* Number of interlace passes in file */
		pass,		/* Current pass in file */
		tile_height,	/* Height of tile in GIMP */
		begin,		/* Beginning tile row */
		end,		/* Ending tile row */
		num;		/* Number of rows to load */
  FILE		*fp;		/* File pointer */
  GDrawable	*drawable;	/* Drawable for layer */
  GPixelRgn	pixel_rgn;	/* Pixel region for layer */
  png_structp	pp;		/* PNG read pointer */
  png_infop	info;		/* PNG info pointer */
  gint		num_colors;	/* Number of colors in colormap */
  guchar	** volatile pixels = NULL,	/* Pixel rows */
		* volatile pixel = NULL;	/* Pixel data */
  char		*progress;	/* Title for progress display... */
  gchar		*basename;
  int		color_type;	/* PNG colour type */
  int		bit_depth = 8;	/* Bits per sample in the file */
  guchar	*cmap = NULL;	/* Colormap of an indexed image */
  png_color	palette[256];	/* Colormap as written */
  png_byte	trans[256];	/* Palette transparency (tRNS) */
  int		num_trans = 0;
  int		trans_index = -1;	/* Index transparent pixels get */
  guchar	*row_out = NULL;	/* One indexed row, for INDEXEDA */
  png_text	text;		/* Software tag */

 /*
  * Open the file and initialize the PNG write "engine"...
  */

  fp = fopen(filename, "wb");
  if (fp == NULL)
    return (0);

  pp   = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  info = pp ? png_create_info_struct(pp) : NULL;
  if (info == NULL)
  {
    png_destroy_write_struct(pp ? &pp : NULL, NULL);
    fclose(fp);
    return (0);
  }

  if (setjmp(png_jmpbuf(pp)))
  {
    g_message("PNG: error while writing %s", filename);
    png_destroy_write_struct(&pp, &info);
    g_free(pixel);
    g_free(pixels);
    fclose(fp);
    return (0);
  }

  png_init_io(pp, fp);

  basename = g_path_get_basename(filename);
  progress = g_strdup_printf("Saving %s:", basename);
  gimp_progress_init(progress);
  g_free(progress);
  g_free(basename);

 /*
  * Get the drawable for the current image...
  */

  drawable = gimp_drawable_get(drawable_ID);
  type     = gimp_drawable_type(drawable_ID);

  gimp_pixel_rgn_init(&pixel_rgn, drawable, 0, 0, drawable->width,
                      drawable->height, FALSE, FALSE);

 /*
  * Set the image dimensions and save the image...
  */

  png_set_compression_level(pp, pngvals.compression_level);

  switch (type)
  {
    case RGB_IMAGE :
        color_type       = PNG_COLOR_TYPE_RGB;
        bpp              = 3;
        break;
    case RGBA_IMAGE :
        color_type       = PNG_COLOR_TYPE_RGB_ALPHA;
        bpp              = 4;
        break;
    case GRAY_IMAGE :
        color_type       = PNG_COLOR_TYPE_GRAY;
        bpp              = 1;
        break;
    case GRAYA_IMAGE :
        color_type       = PNG_COLOR_TYPE_GRAY_ALPHA;
        bpp              = 2;
        break;
    case INDEXED_IMAGE :
        color_type       = PNG_COLOR_TYPE_PALETTE;
        bpp              = 1;
        break;
    case INDEXEDA_IMAGE :
        color_type       = PNG_COLOR_TYPE_PALETTE;
        bpp              = 2;
        break;
    default :
        g_message("PNG: can't save this image type");
        png_destroy_write_struct(&pp, &info);
        fclose(fp);
        return (0);
  };

  if (color_type == PNG_COLOR_TYPE_PALETTE)
  {
    cmap = gimp_image_get_cmap(image_ID, &num_colors);
    if (num_colors < 1)
      num_colors = 1;
    if (num_colors > 256)
      num_colors = 256;
    memset(palette, 0, sizeof (palette));
    if (cmap)
      memcpy(palette, cmap, num_colors * 3);

    /*  Transparency becomes a palette entry of its own: a new one when
     *  the palette has room, otherwise one no opaque pixel uses.
     */
    if (type == INDEXEDA_IMAGE)
    {
      trans_index = png_find_transparent_index(&pixel_rgn, drawable,
                                               &num_colors);
      memset(trans, 255, sizeof (trans));
      trans[trans_index] = 0;
      num_trans = trans_index + 1;
    }

    /*  Small palettes need fewer bits per pixel.  */
    if (num_colors <= 2)
      bit_depth = 1;
    else if (num_colors <= 4)
      bit_depth = 2;
    else if (num_colors <= 16)
      bit_depth = 4;
  }

  png_set_IHDR(pp, info, drawable->width, drawable->height, bit_depth,
               color_type,
               pngvals.interlaced ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  /*  The GIMP's pixels are sRGB, which is what everything today assumes;
   *  say so rather than guess a gamma from the display settings.
   */
  png_set_sRGB_gAMA_and_cHRM(pp, info, PNG_sRGB_INTENT_PERCEPTUAL);

  if (color_type == PNG_COLOR_TYPE_PALETTE)
  {
    png_set_PLTE(pp, info, palette, num_colors);
    if (num_trans > 0)
      png_set_tRNS(pp, info, trans, num_trans, NULL);
  }

  memset(&text, 0, sizeof (text));
  text.compression = PNG_TEXT_COMPRESSION_NONE;
  text.key         = "Software";
  text.text        = "GIMP42";
  png_set_text(pp, info, &text, 1);

  png_write_info(pp, info);

  /*  Rows go to libpng one byte per index; it packs them to bit_depth.  */
  if (bit_depth < 8)
    png_set_packing(pp);

 /*
  * Turn on interlace handling...
  */

  if (pngvals.interlaced)
    num_passes = png_set_interlace_handling(pp);
  else
     num_passes = 1;

 /*
  * Allocate memory for "tile_height" rows and save the image...
  */

  tile_height = gimp_tile_height();
  pixel       = g_new(guchar, (gsize) tile_height * drawable->width * bpp);
  pixels      = g_new(guchar *, tile_height);

  for (i = 0; i < tile_height; i ++)
    pixels[i]= pixel + (gsize) drawable->width * bpp * i;

  if (type == INDEXEDA_IMAGE)
    row_out = g_new(guchar, drawable->width);

  for (pass = 0; pass < num_passes; pass ++)
  {
   /*
    * This works if you are only writing one row at a time...
    */

    for (begin = 0, end = tile_height;
         begin < drawable->height;
         begin += tile_height, end += tile_height)
    {
      if (end > drawable->height)
        end = drawable->height;

      num = end - begin;

      gimp_pixel_rgn_get_rect(&pixel_rgn, pixel, 0, begin, drawable->width, num);

      if (type == INDEXEDA_IMAGE)
      {
        int y, x;

        /*  index + alpha to one index, transparent pixels to trans_index  */
        for (y = 0; y < num; y ++)
        {
          for (x = 0; x < drawable->width; x ++)
            row_out[x] = (pixels[y][x * 2 + 1] < 128) ? trans_index
                                                      : pixels[y][x * 2];
          png_write_row(pp, row_out);
        }
      }
      else
        png_write_rows(pp, pixels, num);

      gimp_progress_update(((double)pass + (double)end / (double)drawable->height) /
                           (double)num_passes);
    };
  };

  png_write_end(pp, info);
  png_destroy_write_struct(&pp, &info);

  g_free(pixel);
  g_free(pixels);
  g_free(cmap);
  g_free(row_out);

 /*
  * Done with the file...
  */

  fclose(fp);

  return (1);
}


/*
 * 'save_close_callback()' - Close the save dialog window.
 */

static void
save_close_callback(GtkWidget *widget,	/* I - Close button */
                    gpointer  data)	/* I - Callback data */
{
  gimp_main_loop_quit ();
}


/*
 * 'save_ok_callback()' - Destroy the save dialog and save the image.
 */

static void
save_ok_callback(GtkWidget *widget,	/* I - OK button */
                 gpointer  data)	/* I - Callback data */
{
  runme = TRUE;

  gtk_window_destroy (GTK_WINDOW (data));
}


/*
 * 'save_compression_callback()' - Update the image compression level.
 */

static void
save_compression_update(GtkAdjustment *adjustment,	/* I - Scale adjustment */
                        gpointer      data)		/* I - Callback data */
{
  pngvals.compression_level = (gint32)gtk_adjustment_get_value (adjustment);
}


/*
 * 'save_interlace_update()' - Update the interlacing option.
 */

static void
save_interlace_update(GtkWidget *widget,	/* I - Interlace toggle button */
                      gpointer  data)		/* I - Callback data  */
{
  pngvals.interlaced = gtk_check_button_get_active (GTK_CHECK_BUTTON (widget));
}


/*
 * 'save_dialog()' - Pop up the save dialog.
 */

static gint
save_dialog(void)
{
  GtkWidget	*dlg,		/* Dialog window */
		*button,	/* OK/cancel buttons */
		*frame,		/* Frame for dialog */
		*table,		/* Table for dialog options */
		*toggle,	/* Interlace toggle button */
		*label,		/* Label for controls */
		*scale;		/* Compression level scale */
  GtkAdjustment	*scale_data;	/* Scale data */


 /*
  * Fake the command-line args and open a window...
  */


  gtk_init ();

 /*
  * Open a dialog window...
  */

  dlg = gimp_dialog_new ("PNG Options");
  g_signal_connect (dlg, "destroy",
                     G_CALLBACK (save_close_callback), NULL);

 /*
  * OK/cancel buttons...
  */

  button = gtk_button_new_with_label("OK");
  g_signal_connect (button, "clicked",
                     G_CALLBACK (save_ok_callback),
                     dlg);
  gimp_box_pack_start (gimp_dialog_get_action_area (dlg), button, TRUE, TRUE, 0);
  gtk_window_set_default_widget (GTK_WINDOW (dlg), button);

  button = gtk_button_new_with_label ("Cancel");
  g_signal_connect_swapped (button, "clicked", G_CALLBACK (gtk_window_destroy), dlg);
  gimp_box_pack_start (gimp_dialog_get_action_area (dlg), button, TRUE, TRUE, 0);

 /*
  * Compression level, interlacing controls...
  */

  frame = gtk_frame_new("Parameter Settings");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);

  table = gimp_table_new(2, 2, FALSE);
  gimp_container_set_border_width (table, 10);
  gimp_container_add (frame, table);

  toggle = gtk_check_button_new_with_label("Interlace");
  gimp_table_attach (table, toggle, 0, 2, 0, 1, GIMP_FILL, 0, 0, 0);
  g_signal_connect (toggle, "toggled",
                     G_CALLBACK (save_interlace_update), NULL);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), pngvals.interlaced);

  label = gtk_label_new("Compression level");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 1, 2, GIMP_FILL, 0, 5, 0);

  scale_data = gtk_adjustment_new(pngvals.compression_level, 1.0, 9.0, 1.0, 1.0, 0.0);
  scale      = gimp_hscale_new (GTK_ADJUSTMENT(scale_data), 1);
  gtk_widget_set_size_request (scale, SCALE_WIDTH, -1);
  gimp_table_attach (table, scale, 1, 2, 1, 2, GIMP_FILL, 0, 0, 0);
  gtk_scale_set_value_pos (GTK_SCALE (scale), GTK_POS_TOP);
  gtk_scale_set_digits(GTK_SCALE (scale), 1);
  g_signal_connect (scale_data, "value-changed",
                     G_CALLBACK (save_compression_update), NULL);
  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return (runme);
}

/*
 * End of "$Id$".
 */
