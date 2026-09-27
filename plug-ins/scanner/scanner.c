/* Scanner plug-in for gimp42
 *
 * File > Acquire > Scanner... in the toolbox and in image windows.
 * GIMP 1.0 left scanning to the external xscanimage program; gimp42 does
 * it itself.  On Linux, macOS and the other Unix systems the scanner is
 * reached through SANE, with a dialog of our own (scanner-sane.c): device,
 * source, mode and resolution, a preview on which the scan area is
 * dragged out, every other option of the backend, and any number of
 * pages per session.  On Windows the system's own WIA scanner dialog
 * does all that (scanner-wia.c) and the image it writes is loaded here.
 *
 * Every page becomes a new image in a window of its own, named "Scan 1",
 * "Scan 2", ...; the procedure returns the last one.  Run without the
 * dialog (non-interactively or with the last values), one page is
 * scanned with the scanner and settings used last time.
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
#include <string.h>

#include "libgimp/gimp.h"

#include "scanner.h"

#ifdef G_OS_WIN32
#include <glib/gstdio.h>
#include "scanner-wia.h"
#endif

#define EXTENSION_NAME     "extension_scanner"
#define PLUG_IN_NAME       "plug_in_scanner"
#define SCANNER_BLURB      "Scan images with a scanner"
#ifdef G_OS_WIN32
#define SCANNER_HELP       "Shows the Windows scanner dialog (Windows Image Acquisition), where the scanner, the settings and the area to scan are chosen, and opens the scanned page as a new image. Windows always asks through its dialog, also when the procedure is run non-interactively. The image is returned."
#else
#define SCANNER_HELP       "Scans through SANE. Interactively a dialog shows the scanners SANE finds, their source, mode, resolution and all other options, and a preview on which the area to scan is dragged out; every page scanned opens as a new image and the last one is returned. Non-interactively one page is scanned with the scanner and settings used last time (or the first scanner's defaults) and returned; with the last values it is also displayed."
#endif
#define SCANNER_AUTHOR     "The gimp42 authors"
#define SCANNER_COPYRIGHT  "The gimp42 authors"
#define SCANNER_DATE       "2026"

static void query (void);
static void run   (gchar   *name,
		   gint     nparams,
		   GParam  *param,
		   gint    *nreturn_vals,
		   GParam **return_vals);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc  */
  NULL,    /* quit_proc  */
  query,   /* query_proc */
  run,     /* run_proc   */
};

ScannerVals scanner_vals =
{
  SCANNER_VALS_VERSION,
  0,          /* page */
  "",         /* device */
  "",         /* source */
  "",         /* mode */
  -1.0,       /* resolution */
  FALSE,      /* have_area */
  { 0.0, 0.0, 0.0, 0.0 }
};


MAIN ()

static void
query (void)
{
  static GParamDef extension_args[] =
  {
    { PARAM_INT32,    "run_mode", "Interactive, non-interactive" }
  };
  static GParamDef extension_return_vals[] =
  {
    { PARAM_IMAGE,    "image",    "The scanned image" }
  };
  static GParamDef plug_in_args[] =
  {
    { PARAM_INT32,    "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE,    "image",    "Input image (unused)" },
    { PARAM_DRAWABLE, "drawable", "Input drawable (unused)" }
  };
  static GParamDef plug_in_return_vals[] =
  {
    { PARAM_IMAGE,    "new_image", "The scanned image" }
  };

  gimp_install_procedure (EXTENSION_NAME,
			  SCANNER_BLURB,
			  SCANNER_HELP,
			  SCANNER_AUTHOR,
			  SCANNER_COPYRIGHT,
			  SCANNER_DATE,
			  "<Toolbox>/File/Acquire/Scanner...",
			  NULL,
			  PROC_EXTENSION,
			  G_N_ELEMENTS (extension_args),
			  G_N_ELEMENTS (extension_return_vals),
			  extension_args,
			  extension_return_vals);

  gimp_install_procedure (PLUG_IN_NAME,
			  SCANNER_BLURB,
			  SCANNER_HELP " The image and drawable arguments are ignored.",
			  SCANNER_AUTHOR,
			  SCANNER_COPYRIGHT,
			  SCANNER_DATE,
			  "<Image>/File/Acquire/Scanner...",
			  "",
			  PROC_PLUG_IN,
			  G_N_ELEMENTS (plug_in_args),
			  G_N_ELEMENTS (plug_in_return_vals),
			  plug_in_args,
			  plug_in_return_vals);
}


/*  Settings kept between runs.  */

static void
scanner_vals_load (void)
{
  GParam *params;
  gint    nparams;

  params = gimp_run_procedure ("gimp_procedural_db_get_data",
			       &nparams,
			       PARAM_STRING, SCANNER_DATA_KEY,
			       PARAM_END);

  /* gimp_get_data () would copy whatever length was stored; only take
   * data that has the layout this version writes.
   */
  if (nparams >= 3 &&
      params[0].data.d_status == STATUS_SUCCESS &&
      params[1].data.d_int32 == (gint32) sizeof (ScannerVals))
    {
      ScannerVals vals;

      memcpy (&vals, params[2].data.d_int8array, sizeof (ScannerVals));
      if (vals.version == SCANNER_VALS_VERSION)
	{
	  scanner_vals = vals;
	  scanner_vals.device[sizeof (scanner_vals.device) - 1] = '\0';
	  scanner_vals.source[sizeof (scanner_vals.source) - 1] = '\0';
	  scanner_vals.mode[sizeof (scanner_vals.mode) - 1] = '\0';
	}
    }

  gimp_destroy_params (params, nparams);
}

void
scanner_vals_save (void)
{
  scanner_vals.version = SCANNER_VALS_VERSION;
  gimp_set_data (SCANNER_DATA_KEY, &scanner_vals, sizeof (ScannerVals));
}


/*  Images  */

/*  The name of the next page, advancing the counter.  */
static gchar *
scanner_next_name (void)
{
  scanner_vals.page++;

  return g_strdup_printf ("Scan %d", (gint) scanner_vals.page);
}

gint32
scanner_image_new (const guchar *data,
		   gint          width,
		   gint          height,
		   gint          bpp)
{
  GDrawable *drawable;
  GPixelRgn  pixel_rgn;
  gint32     image;
  gint32     layer;
  gchar     *name;
  gint       tile_height;
  gint       y;

  if (width <= 0 || height <= 0 || (bpp != 1 && bpp != 3))
    return -1;

  image = gimp_image_new (width, height, (bpp == 1) ? GRAY : RGB);
  if (image == -1)
    return -1;

  layer = gimp_layer_new (image, "Background", width, height,
			  (bpp == 1) ? GRAY_IMAGE : RGB_IMAGE,
			  100, NORMAL_MODE);
  gimp_image_add_layer (image, layer, 0);

  drawable = gimp_drawable_get (layer);
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height,
		       TRUE, FALSE);

  tile_height = gimp_tile_height ();
  for (y = 0; y < height; y += tile_height)
    {
      gint rows = MIN (tile_height, height - y);

      gimp_pixel_rgn_set_rect (&pixel_rgn,
			       (guchar *) data + (gsize) y * width * bpp,
			       0, y, width, rows);
    }

  gimp_drawable_flush (drawable);
  gimp_drawable_detach (drawable);

  name = scanner_next_name ();
  gimp_image_set_filename (image, name);
  g_free (name);

  return image;
}

void
scanner_image_display (gint32 image)
{
  gimp_display_new (image);
  gimp_displays_flush ();
}


#ifdef G_OS_WIN32

/*  Windows: the WIA dialog writes the page to a temporary file, which is
 *  loaded with the file plug-in for the format it chose.
 */
static gint32
scanner_wia_load (const gchar *filename,
		  const gchar *procedure)
{
  GParam *params;
  gint    nparams;
  gint32  image = -1;

  params = gimp_run_procedure ((gchar *) procedure,
			       &nparams,
			       PARAM_INT32, RUN_NONINTERACTIVE,
			       PARAM_STRING, filename,
			       PARAM_STRING, filename,
			       PARAM_END);
  if (nparams >= 2 && params[0].data.d_status == STATUS_SUCCESS)
    image = params[1].data.d_image;
  gimp_destroy_params (params, nparams);

  return image;
}

static gint32
scanner_wia_scan (void)
{
  static const gchar *loaders[] =
  {
    "file_bmp_load",	/* SCANNER_WIA_FORMAT_BMP */
    "file_png_load",	/* SCANNER_WIA_FORMAT_PNG */
    "file_jpeg_load",	/* SCANNER_WIA_FORMAT_JPEG */
    "file_tiff_load",	/* SCANNER_WIA_FORMAT_TIFF */
  };
  gchar     *basename;
  gchar     *filename;
  gunichar2 *wide;
  gchar      error[1024];
  gint       format = SCANNER_WIA_FORMAT_OTHER;
  gint       result;
  gint32     image  = -1;

  basename = g_strdup_printf ("gimp42-scan-%08x%08x.bmp",
			      g_random_int (), g_random_int ());
  filename = g_build_filename (g_get_tmp_dir (), basename, NULL);
  g_free (basename);

  wide = g_utf8_to_utf16 (filename, -1, NULL, NULL, NULL);
  if (! wide)
    {
      g_message ("Scanner: the temporary folder's name cannot be used: %s\n",
		 filename);
      g_free (filename);
      return -1;
    }

  result = scanner_wia_acquire ((const wchar_t *) wide, &format,
				error, sizeof (error));
  g_free (wide);

  if (result < 0)
    {
      g_message ("Scanner: %s\n", error);
    }
  else if (result > 0)
    {
      if (format >= 0 && format < (gint) G_N_ELEMENTS (loaders))
	image = scanner_wia_load (filename, loaders[format]);
      if (image == -1)
	image = scanner_wia_load (filename, "gimp_file_load");

      if (image == -1)
	{
	  g_message ("Scanner: the scanned image could not be loaded\n");
	}
      else
	{
	  gint32 *layers;
	  gint    nlayers;
	  gchar  *name;

	  /* Not the temporary file's name.  */
	  name = scanner_next_name ();
	  gimp_image_set_filename (image, name);
	  g_free (name);

	  layers = gimp_image_get_layers (image, &nlayers);
	  if (nlayers == 1)
	    gimp_layer_set_name (layers[0], "Background");
	  g_free (layers);
	}
    }

  g_remove (filename);
  g_free (filename);

  return image;
}

#endif /* G_OS_WIN32 */


static void
run (gchar   *name,
     gint     nparams,
     GParam  *param,
     gint    *nreturn_vals,
     GParam **return_vals)
{
  static GParam values[2];
  GRunModeType  run_mode;
  gint32        image = -1;

  *nreturn_vals = 1;
  *return_vals  = values;
  values[0].type          = PARAM_STATUS;
  values[0].data.d_status = STATUS_SUCCESS;

  if (nparams < 1 ||
      (strcmp (name, EXTENSION_NAME) != 0 && strcmp (name, PLUG_IN_NAME) != 0))
    {
      values[0].data.d_status = STATUS_CALLING_ERROR;
      return;
    }

  run_mode = param[0].data.d_int32;

  scanner_vals_load ();

#ifdef G_OS_WIN32
  /* WIA can only be asked through its dialog.  */
  image = scanner_wia_scan ();
  if (image != -1 && run_mode != RUN_NONINTERACTIVE)
    scanner_image_display (image);
#else
  switch (run_mode)
    {
    case RUN_INTERACTIVE:
      /* The dialog displays every page as it is scanned.  */
      image = scanner_sane_dialog ();
      break;

    case RUN_WITH_LAST_VALS:
      image = scanner_sane_scan (TRUE);
      if (image != -1)
	scanner_image_display (image);
      break;

    case RUN_NONINTERACTIVE:
    default:
      image = scanner_sane_scan (FALSE);
      break;
    }
#endif

  /* The page counter, and the settings the dialog left.  */
  scanner_vals_save ();

  /*  Interactively, no image means the user closed the dialog without
   *  scanning (any error has been shown to them already).
   */
  if (image == -1)
    {
      values[0].data.d_status = (run_mode == RUN_INTERACTIVE) ?
				STATUS_CANCEL : STATUS_EXECUTION_ERROR;
      return;
    }

  *nreturn_vals = 2;
  values[1].type          = PARAM_IMAGE;
  values[1].data.d_image  = image;
}
