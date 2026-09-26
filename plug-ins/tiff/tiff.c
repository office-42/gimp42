/* tiff loading and saving for the GIMP
 *  -Peter Mattis
 * The TIFF loading code has been completely revamped by Nick Lamb
 * njl195@zepler.org.uk -- 18 May 1998
 *
 * The code for this filter is based on "tifftopnm" and "pnmtotiff",
 *  2 programs that are a part of the netpbm package.
 */

/*
** tifftopnm.c - converts a Tagged Image File to a portable anymap
**
** Derived by Jef Poskanzer from tif2ras.c, which is:
**
** Copyright (c) 1990 by Sun Microsystems, Inc.
**
** Author: Patrick J. Naughton
** naughton@wind.sun.com
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted,
** provided that the above copyright notice appear in all copies and that
** both that copyright notice and this permission notice appear in
** supporting documentation.
**
** This file is provided AS IS with no warranties of any kind.  The author
** shall have no liability with respect to the infringement of copyrights,
** trade secrets or any patents by this file or any part thereof.  In no
** event will the author be liable for any lost revenue or profits or
** other special, indirect and consequential damages.
*/

#include <stdlib.h>
#include <string.h>
#include <tiffio.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"


typedef struct
{
  gint  compression;
  gint  fillorder;
} TiffSaveVals;

typedef struct
{
  gint  run;
} TiffSaveInterface;

typedef struct {
  gint32 ID;
  GDrawable *drawable;
  GPixelRgn pixel_rgn;
  guchar *pixels;
  guchar *pixel;
} channel_data;

/* Declare some local functions.
 */
static void   query      (void);
static void   run        (char    *name,
                          int      nparams,
                          GParam  *param,
                          int     *nreturn_vals,
                          GParam **return_vals);
static gint32 load_image (char   *filename);
static void   load_separate (TIFF *tif, GDrawable *drawable,
                             channel_data *channel,
                             unsigned short bps, unsigned short photomet,
                             int rows, int cols, int alpha, int extra);
static void   load_8bit (TIFF *tif, GDrawable *drawable,
                         channel_data *channel,
                         unsigned short bps, unsigned short photomet,
                         int rows, int cols, int alpha, int extra);
static void   load_default (TIFF *tif, GDrawable *drawable,
                            channel_data *channel,
                            unsigned short bps, unsigned short photomet,
                            int rows, int cols, int alpha, int extra);
static gint   save_image (char   *filename,
			  gint32  image,
			  gint32  drawable);

static gint   save_dialog ();

static void   save_close_callback  (GtkWidget *widget,
				    gpointer   data);
static void   save_ok_callback     (GtkWidget *widget,
				    gpointer   data);
static void   save_toggle_update   (GtkWidget *widget,
				    gpointer   data);
static void   comment_entry_callback  (GtkWidget *widget,
				       gpointer   data);

#define DEFAULT_COMMENT "Created with The GIMP"

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

static TiffSaveVals tsvals =
{
  COMPRESSION_LZW,    /*  compression  */
  FILLORDER_LSB2MSB,  /*  fillorder    */
};

static TiffSaveInterface tsint =
{
  FALSE                /*  run  */
};

static char *image_comment= NULL;

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
    { PARAM_INT32, "compression", "Compression type: { NONE (0), LZW (1), PACKBITS (2)" },
    { PARAM_INT32, "fillorder", "Fill Order: { MSB to LSB (0), LSB to MSB (1)" }
  };
  static int nsave_args = sizeof (save_args) / sizeof (save_args[0]);

  gimp_install_procedure ("file_tiff_load",
                          "loads files of the tiff file format",
                          "FIXME: write help for tiff_load",
                          "Spencer Kimball, Peter Mattis & Nick Lamb",
                          "Nick Lamb <njl195@zepler.org.uk>",
                          "1995-1996,1998",
                          "<Load>/Tiff",
			  NULL,
                          PROC_PLUG_IN,
                          nload_args, nload_return_vals,
                          load_args, load_return_vals);

  gimp_install_procedure ("file_tiff_save",
                          "saves files in the tiff file format",
                          "FIXME: write help for tiff_save",
                          "Spencer Kimball & Peter Mattis",
                          "Spencer Kimball & Peter Mattis",
                          "1995-1996",
                          "<Save>/Tiff",
			  "RGB*, GRAY*, INDEXED",
                          PROC_PLUG_IN,
                          nsave_args, 0,
                          save_args, NULL);

  gimp_register_magic_load_handler ("file_tiff_load", "tif,tiff", "",
             "0,string,II*\\0,0,string,MM\\0*");
  gimp_register_save_handler ("file_tiff_save", "tif,tiff", "");
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
  GStatusType status = STATUS_SUCCESS;
#ifdef GIMP_HAVE_PARASITES
  Parasite *parasite;
#endif /* GIMP_HAVE_PARASITES */
  gint32 image;

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;
  values[0].type = PARAM_STATUS;
  values[0].data.d_status = STATUS_CALLING_ERROR;

  if (strcmp (name, "file_tiff_load") == 0)
    {
      image = load_image (param[1].data.d_string);

      if (image != -1)
	{
	  *nreturn_vals = 2;
	  values[0].data.d_status = STATUS_SUCCESS;
	  values[1].type = PARAM_IMAGE;
	  values[1].data.d_image = image;
	}
      else
	{
	  values[0].data.d_status = STATUS_EXECUTION_ERROR;
	}
    }
  else if (strcmp (name, "file_tiff_save") == 0)
    {

/* Do this right this time, if POSSIBLE query for parasites, otherwise
   or if there isn't one, choose the DEFAULT_COMMENT */

#ifdef GIMP_HAVE_PARASITES
      int image = param[1].data.d_int32;

      parasite = gimp_image_find_parasite(image, "gimp-comment");
      if (parasite)
        image_comment = g_strdup(parasite->data);
      parasite_free(parasite);
#endif /* GIMP_HAVE_PARASITES */

      if (!image_comment) image_comment = g_strdup(DEFAULT_COMMENT);	  

      switch (run_mode)
	{
	case RUN_INTERACTIVE:
	{
	  /*  Possibly retrieve data  */
	  gimp_get_data ("file_tiff_save", &tsvals);
#ifdef GIMP_HAVE_PARASITES
	  parasite = gimp_image_find_parasite(image, "tiff-save-options");
	  if (parasite)
	  {
	    tsvals.compression = ((TiffSaveVals *)parasite->data)->compression;
	    tsvals.fillorder   = ((TiffSaveVals *)parasite->data)->fillorder;
	  }
	  parasite_free(parasite);
#endif /* GIMP_HAVE_PARASITES */

	  /*  First acquire information with a dialog  */
	  if (! save_dialog ())
	    return;
	} break;

	case RUN_NONINTERACTIVE:
	  /*  Make sure all the arguments are there!  */
	  if (nparams != 7)
	    status = STATUS_CALLING_ERROR;
	  if (status == STATUS_SUCCESS)
	    {
	      switch (param[5].data.d_int32)
		{
		case 0: tsvals.compression = COMPRESSION_NONE;     break;
		case 1: tsvals.compression = COMPRESSION_LZW;      break;
		case 2: tsvals.compression = COMPRESSION_PACKBITS; break;
		default: status = STATUS_CALLING_ERROR; break;
		}
	      switch (param[6].data.d_int32)
		{
		case 0: tsvals.fillorder = FILLORDER_MSB2LSB; break;
		case 1: tsvals.fillorder = FILLORDER_LSB2MSB; break;
		default: status = STATUS_CALLING_ERROR; break;
		}
	    }

	case RUN_WITH_LAST_VALS:
	  /*  Possibly retrieve data  */
	{
	  gimp_get_data ("file_tiff_save", &tsvals);
#ifdef GIMP_HAVE_PARASITES
	  parasite = gimp_image_find_parasite(image, "tiff-save-options");
	  if (parasite)
	  {
	    tsvals.compression = ((TiffSaveVals *)parasite->data)->compression;
	    tsvals.fillorder   = ((TiffSaveVals *)parasite->data)->fillorder;
	  }
	  parasite_free(parasite);
#endif /* GIMP_HAVE_PARASITES */
	}
	  break;

	default:
	  break;
	}

      *nreturn_vals = 1;
      if (save_image (param[3].data.d_string, param[1].data.d_int32, param[2].data.d_int32))
	{
	  /*  Store mvals data  */
	  gimp_set_data ("file_tiff_save", &tsvals, sizeof (TiffSaveVals));

	  values[0].data.d_status = STATUS_SUCCESS;
	}
      else
	values[0].data.d_status = STATUS_EXECUTION_ERROR;
    }
}

static gint32 load_image (char *filename) {
  TIFF *tif;
  unsigned short bps, spp, photomet, planar;
  int cols, rows, alpha;
  int image, layer, tile_height;
  unsigned short *redmap, *greenmap, *bluemap;
  guchar cmap[768];
  int image_type= 0, layer_type= 0;
  unsigned short extra, *extra_types;

  int i, j;

  GDrawable *drawable;
  char *name;

  guchar colors[3]= {0, 0, 0};

  channel_data *channel= NULL;

  TiffSaveVals save_vals;
#ifdef GIMP_HAVE_PARASITES
  Parasite *parasite;
#endif /* GIMP_HAVE_PARASITES */
  guint16 tmp;
  tif = TIFFOpen (filename, "r");
  if (!tif) {
    g_message("TIFF Can't open %s\n", filename);
    gimp_quit ();
  }

  name = g_malloc (strlen (filename) + 12);
  sprintf (name, "Loading %s:", filename);
  gimp_progress_init (name);
  g_free (name);

  if (!TIFFGetField (tif, TIFFTAG_BITSPERSAMPLE, &bps))
    bps = 1;

  if (bps > 8) {
    g_message("TIFF Can't handle samples wider than 8-bit\n");
    gimp_quit();
  }

  if (!TIFFGetField (tif, TIFFTAG_PLANARCONFIG, &planar))
    planar = PLANARCONFIG_SEPARATE;
  if (!TIFFGetField (tif, TIFFTAG_SAMPLESPERPIXEL, &spp))
    spp = 1;
  if (!TIFFGetField (tif, TIFFTAG_EXTRASAMPLES, &extra, &extra_types))
    extra = 0;

  if (!TIFFGetField (tif, TIFFTAG_IMAGEWIDTH, &cols)) {
    g_message("TIFF Can't get image width");
    gimp_quit ();
  }

  if (!TIFFGetField (tif, TIFFTAG_IMAGELENGTH, &rows)) {
    g_message("TIFF Can't get image length");
    gimp_quit ();
  }

  if (!TIFFGetField (tif, TIFFTAG_PHOTOMETRIC, &photomet)) {
    g_message("TIFF Can't get photometric\nassuming min-is-black");
    /* old AppleScan software misses out the photometric tag (and
     * incidentally assumes min-is-white, but xv assumes min-is-black,
     * so we follow xv's lead.  It's not much hardship to invert the
     * image later). */
    photomet = PHOTOMETRIC_MINISBLACK;
  }

  /* test if the extrasample represents an associated alpha channel... */
  if (extra > 0 && (extra_types[0] == EXTRASAMPLE_ASSOCALPHA)) {
    alpha = 1;
    --extra;
  } else {
    alpha = 0;
  }

  if (photomet == PHOTOMETRIC_RGB && spp > 3 + extra) {
    alpha= 1;
    extra= spp - 4; 
  } else if (photomet != PHOTOMETRIC_RGB && spp > 1 + extra) {
    alpha= 1;
    extra= spp - 2;
  }

  switch (photomet) {
    case PHOTOMETRIC_MINISBLACK:
    case PHOTOMETRIC_MINISWHITE:
      image_type = GRAY;
      layer_type = (alpha) ? GRAYA_IMAGE : GRAY_IMAGE;
      break;

    case PHOTOMETRIC_RGB:
      image_type = RGB;
      layer_type = (alpha) ? RGBA_IMAGE : RGB_IMAGE;
      break;

    case PHOTOMETRIC_PALETTE:
      image_type = INDEXED;
      layer_type = (alpha) ? INDEXEDA_IMAGE : INDEXED_IMAGE;
      break;

    case PHOTOMETRIC_MASK:
      g_message ("TIFF Can't handle PHOTOMETRIC_MASK");
      gimp_quit ();
      break;
    default:
      g_message ("TIFF Unknown photometric\n Number %d", photomet);
      gimp_quit ();
  }

  if ((image = gimp_image_new (cols, rows, image_type)) == -1) {
    g_message("TIFF Can't create a new image\n");
    gimp_quit ();
  }
  gimp_image_set_filename (image, filename);

  /* attach a parasite containing the compression/fillorder */
  if (!TIFFGetField (tif, TIFFTAG_COMPRESSION, &tmp))
    save_vals.compression = COMPRESSION_NONE;
  else
    save_vals.compression = tmp;
  if (!TIFFGetField (tif, TIFFTAG_FILLORDER, &tmp))
    save_vals.fillorder = FILLORDER_LSB2MSB;
  else
    save_vals.fillorder = tmp;
#ifdef GIMP_HAVE_PARASITES
  parasite = parasite_new("tiff-save-options", 0,
			  sizeof(save_vals), &save_vals);
  gimp_image_attach_parasite(image, parasite);
  parasite_free(parasite);
#endif /* GIMP_HAVE_PARASITES */


  /* Attach a parasite containing the image description.  Pretend to
   * be a gimp comment so other plugins will use this description as
   * an image comment where appropriate. */
#ifdef GIMP_HAVE_PARASITES
  {
    char *img_desc;

    if (TIFFGetField (tif, TIFFTAG_IMAGEDESCRIPTION, &img_desc))
    {
      int len;

      len = strlen(img_desc) + 1;
      len = MIN(len, 241);
      img_desc[len-1] = '\000';

      parasite = parasite_new("gimp-comment", PARASITE_PERSISTENT,
			      len, img_desc);
      gimp_image_attach_parasite(image, parasite);
      parasite_free(parasite);
    }
  }
#endif /* GIMP_HAVE_PARASITES */

  /* any resolution info in the file? */
#ifdef GIMP_HAVE_RESOLUTION_INFO
  {
    float xres=0.0, yres=0.0;
    unsigned short units;

    if (TIFFGetField (tif, TIFFTAG_XRESOLUTION, &xres)) {
      if (TIFFGetField (tif, TIFFTAG_YRESOLUTION, &yres)) {

	if (TIFFGetField (tif, TIFFTAG_RESOLUTIONUNIT, &units)) {
	  switch(units) {
	  case RESUNIT_NONE:
	    /* ImageMagick writes files with this silly resunit */
	    g_message("TIFF warning: resolution units meaningless, "
		      "forcing 72 dpi\n");
	    xres = 72.0;
	    yres = 72.0;
	    break;

	  case RESUNIT_INCH:
	    break;

	  case RESUNIT_CENTIMETER:
	    xres *= 2.54;
	    yres *= 2.54;
	    break;

	  default:
	    g_message("TIFF file error: unknown resolution unit type %d, "
		      "assuming dpi\n", units);
	  }
	} else { /* no res unit tag */
	  /* old AppleScan software produces these */
	  g_message("TIFF warning: resolution specified without\n"
		    "any units tag, assuming dpi\n");
	}
      } else { /* xres but no yres */
	g_message("TIFF warning: no y resolution info, assuming same as x\n");
	yres = xres;
      }

      /* sanity check, since division by zero later could be embarrassing */
      if (xres < 1e-5 || yres < 1e-5) {
	g_message("TIFF: image resolution is zero: forcing 72 dpi\n");
	xres = 72.0;
	yres = 72.0;
      }

      /* now set the new image's resolution info */
      gimp_image_set_resolution (image, xres, yres);
    }

    /* no x res tag => we assume we have no resolution info, so we
     * don't care.  Older versions of this plugin used to write files
     * with no resolution tags at all. */

    /* TODO: haven't caught the case where yres tag is present, but
       not xres.  This is left as an exercise for the reader - they
       should feel free to shoot the author of the broken program
       that produced the damaged TIFF file in the first place. */
  }
#endif /* GIMP_HAVE_RESOLUTION_INFO */


  /* Install colormap for INDEXED images only */
  if (image_type == INDEXED) {
    if (!TIFFGetField (tif, TIFFTAG_COLORMAP, &redmap, &greenmap, &bluemap)) {
      g_message("TIFF Can't get colormaps");
      gimp_quit ();
    }

    for (i = 0, j = 0; i < (1 << bps); i++) {
      cmap[j++] = redmap[i] >> 8;
      cmap[j++] = greenmap[i] >> 8;
      cmap[j++] = bluemap[i] >> 8;
    }
    gimp_image_set_cmap (image, cmap, (1 << bps));
  }

  layer = gimp_layer_new (image, "Background", cols, rows, layer_type,
			     100, NORMAL_MODE);
  gimp_image_add_layer (image, layer, 0);
  drawable = gimp_drawable_get (layer);

  tile_height = gimp_tile_height ();

  if (extra > 0) {
    channel = g_new (channel_data, extra);

    /* Add alpha channels as appropriate */
    for (i= 0; i < extra; ++i) {
      channel[i].ID= gimp_channel_new(image, "TIFF Channel", cols, rows,
                                                            100.0, colors);
      gimp_image_add_channel(image, channel[i].ID, 0);
      channel[i].drawable= gimp_drawable_get (channel[i].ID);
      channel[i].pixels= g_new(guchar, tile_height * cols);

      gimp_pixel_rgn_init (&(channel[i].pixel_rgn), channel[i].drawable, 0, 0,
  			   cols, rows, TRUE, FALSE);
    }
  }

  if (planar == PLANARCONFIG_SEPARATE) {
    load_separate(tif, drawable, channel, bps, photomet,
                  rows, cols, alpha, extra);
  } else if (bps == 8) {
    load_8bit(tif, drawable, channel, bps, photomet, rows, cols, alpha, extra);
  } else {
    load_default(tif, drawable, channel, bps, photomet,
                 rows, cols, alpha, extra);
  }

  gimp_drawable_flush (drawable);
  gimp_drawable_detach (drawable);

  for (i= 0; i < extra; ++i) {
    gimp_drawable_flush (channel[i].drawable);
    gimp_drawable_detach (channel[i].drawable);
  }

  return image;
}

static void
load_8bit(TIFF *tif, GDrawable *drawable,
             channel_data *channel,
             unsigned short bps, unsigned short photomet,
             int rows, int cols, int alpha, int extra)
{
  guchar *source, *dest, *s, *d;
  GPixelRgn pixel_rgn;
  int gray_val, red_val, green_val, blue_val, alpha_val;
  int col, row, start, i;
  int tile_height = gimp_tile_height ();

  source= g_new (guchar, TIFFScanlineSize (tif));
  dest = g_new (guchar, tile_height * cols * drawable->bpp);
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, cols, rows, TRUE, FALSE);

  for (start= 0, row = 0; row < rows; ++row) {
    d= dest + cols * (row % tile_height) * drawable->bpp;

    if (TIFFReadScanline (tif, source, row, 0) < 0) {
      g_message("TIFF Bad data read on line %d\n", row);
      gimp_quit ();
    }

    for (i= 0; i < extra; ++i) {
      channel[i].pixel= channel[i].pixels + cols * (row % tile_height);
    }

    s= source;

    for (col = 0; col < cols; col++) {
      switch (photomet) {
        case PHOTOMETRIC_MINISBLACK:
          if (alpha) {
            gray_val= *s++;
            alpha_val= *s++;
            if (alpha_val)
              *d++ = gray_val * 255 / alpha_val;
            else
              *d++ = 0;
            *d++ = alpha_val;
          } else {
            *d++ = *s++;
          }
          break;

        case PHOTOMETRIC_MINISWHITE:
          if (alpha) {
            gray_val= *s++;
            alpha_val= *s++;
            if (alpha_val)
              *d++ = ((255 - gray_val) * 255) / alpha_val;
            else
              *d++ = 0;
            *d++ = alpha_val;
          } else {
            *d++ = ~(*s++);
          }
          break;

        case PHOTOMETRIC_PALETTE:
          *d++= *s++;
          if (alpha) *d++= *s++;
          break;
  
        case PHOTOMETRIC_RGB:
          if (alpha) {
            red_val= *s++;
            green_val= *s++;
            blue_val= *s++;
            alpha_val= *s++;
            if (alpha_val) {
              *d++ = (red_val * 255) / alpha_val;
              *d++ = (green_val * 255) / alpha_val;
              *d++ = (blue_val * 255) / alpha_val;
            } else {
              *d++ = 0;
              *d++ = 0;
              *d++ = 0;
	    }
	    *d++ = alpha_val;
	  } else {
	    *d++ = *s++;
	    *d++ = *s++;
	    *d++ = *s++;
	  }
          break;

        default:
          /* This case was handled earlier */
          g_assert_not_reached();
      }
      for (i= 0; i < extra; ++i) {
        *channel[i].pixel++ = *s++;
      }
    }
    
    if (((row + 1) % tile_height) == 0 || row + 1 == rows) {
      gimp_pixel_rgn_set_rect (&pixel_rgn, dest, 0, start, cols, 1+row-start);
      for (i= 0; alpha + i < extra; ++i) {
	gimp_pixel_rgn_set_rect(&(channel[i].pixel_rgn), channel[i].pixels,
                                0, start, cols, 1+row-start);
      }
      gimp_progress_update ((double) row / (double) rows);
      start= row + 1;
    }
  }
}

/* Step through all <= 8-bit samples in an image */

#define NEXTSAMPLE(var)                       \
  {                                           \
      if (bitsleft == 0)                      \
      {                                       \
	  s++;                                \
	  bitsleft = 8;                       \
      }                                       \
      bitsleft -= bps;                        \
      var = ( *s >> bitsleft ) & maxval;      \
  }

static void
load_default(TIFF *tif, GDrawable *drawable,
             channel_data *channel,
             unsigned short bps, unsigned short photomet,
             int rows, int cols, int alpha, int extra)
{
  guchar *source, *dest, *s, *d;
  GPixelRgn pixel_rgn;
  int gray_val, red_val, green_val, blue_val, alpha_val;
  int col, row, start, i;
  int bitsleft, maxval = (1 << bps) - 1;
  int tile_height = gimp_tile_height ();

  source= g_new (guchar, TIFFScanlineSize (tif));
  dest = g_new (guchar, tile_height * cols * drawable->bpp);
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, cols, rows, TRUE, FALSE);

  for (start= 0, row = 0; row < rows; ++row) {
    d= dest + cols * (row % tile_height) * drawable->bpp;

    if (TIFFReadScanline (tif, source, row, 0) < 0) {
      g_message("TIFF Bad data read on line %d\n", row);
      gimp_quit ();
    }

    for (i= 0; i < extra; ++i) {
      channel[i].pixel= channel[i].pixels + cols * (row % tile_height);
    }

    /* Set s/bitsleft ready to use NEXTSAMPLE macro */

    s= source;
    bitsleft= 8;

    for (col = 0; col < cols; col++) {
      switch (photomet) {
        case PHOTOMETRIC_MINISBLACK:
          NEXTSAMPLE(gray_val);
          if (alpha) {
            NEXTSAMPLE(alpha_val);
            if (alpha_val)
              *d++ = (gray_val * 65025) / (alpha_val * maxval);
            else
              *d++ = 0;
            *d++ = alpha_val;
          } else {
            *d++ = (gray_val * 255) / maxval;
          }
          break;

        case PHOTOMETRIC_MINISWHITE:
          NEXTSAMPLE(gray_val);
          if (alpha) {
            NEXTSAMPLE(alpha_val);
            if (alpha_val)
              *d++ = ((maxval - gray_val) * 65025) / (alpha_val * maxval);
            else
              *d++ = 0;
            *d++ = alpha_val;
          } else {
            *d++ = ((maxval - gray_val) * 255) / maxval;
          }
          break;

        case PHOTOMETRIC_PALETTE:
          NEXTSAMPLE(*d++);
          if (alpha) {
            NEXTSAMPLE(*d++);
          }
          break;
  
        case PHOTOMETRIC_RGB:
          NEXTSAMPLE(red_val)
          NEXTSAMPLE(green_val)
          NEXTSAMPLE(blue_val)
          if (alpha) {
            NEXTSAMPLE(alpha_val)
            if (alpha_val) {
              *d++ = (red_val * 255) / alpha_val;
              *d++ = (green_val * 255) / alpha_val;
              *d++ = (blue_val * 255) / alpha_val;
            } else {
              *d++ = 0;
              *d++ = 0;
              *d++ = 0;
	    }
	    *d++ = alpha_val;
	  } else {
	    *d++ = red_val;
	    *d++ = green_val;
	    *d++ = blue_val;
	  }
          break;

        default:
          /* This case was handled earlier */
          g_assert_not_reached();
      }
      for (i= 0; i < extra; ++i) {
        NEXTSAMPLE(alpha_val);
        *channel[i].pixel++ = alpha_val;
      }
    }
    
    if (((row + 1) % tile_height) == 0 || row + 1 == rows) {
      gimp_pixel_rgn_set_rect (&pixel_rgn, dest, 0, start, cols, 1+row-start);
      for (i= 0; alpha + i < extra; ++i) {
	gimp_pixel_rgn_set_rect(&(channel[i].pixel_rgn), channel[i].pixels,
                                0, start, cols, 1+row-start);
      }
      gimp_progress_update ((double) row / (double) rows);
      start= row + 1;
    }
  }
}

static void
load_separate(TIFF *tif, GDrawable *drawable,
              channel_data *channel,
              unsigned short bps, unsigned short photomet,
              int rows, int cols, int alpha, int extra)
{
  guchar *source, *dest, *s, *d;
  GPixelRgn pixel_rgn;
  int col, row, start;
  int bitsleft, maxval = (1 << bps) - 1;
  int tile_height = gimp_tile_height ();

  TIFFPrintDirectory(tif, stdout, 0);
  if (photomet != PHOTOMETRIC_RGB) {
    g_message("So far PLANARCONFIG_SEPARATE only supports RGB images");
    gimp_quit();
  }

  source= g_new (guchar, TIFFScanlineSize (tif));
  dest = g_new (guchar, tile_height * cols * drawable->bpp);
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, cols, rows, TRUE, FALSE);

  /* RED channel */
  for (start= 0, row = 0; row < rows; ++row) {
    d= dest + cols * (row % tile_height) * drawable->bpp;

    if (TIFFReadScanline (tif, source, row, 0) < 0) {
      g_message("TIFF Bad data read on line %d\n", row);
      gimp_quit ();
    }

    /* Set s/bitsleft ready to use NEXTSAMPLE macro */

    s= source;
    bitsleft= 8;

    for (col = 0; col < cols; col++) {
      NEXTSAMPLE(d[col * (3 + alpha)])
    }
    
    if (((row + 1) % tile_height) == 0 || row + 1 == rows) {
      gimp_pixel_rgn_set_rect (&pixel_rgn, dest, 0, start, cols, 1+row-start);
      gimp_progress_update ((double) row / (double) rows);
      start= row + 1;
    }
  }

  /* GREEN channel */
  for (start= 0, row = 0; row < rows; ++row) {
    d= dest + cols * (row % tile_height) * drawable->bpp;

    if ((row % tile_height) == 0) {
      if (rows - row < tile_height)
        gimp_pixel_rgn_get_rect(&pixel_rgn, dest, 0, start, cols, rows - row);
      else
        gimp_pixel_rgn_get_rect(&pixel_rgn, dest, 0, start, cols, tile_height);
      gimp_progress_update ((double) row / (double) rows);
    }

    if (TIFFReadScanline (tif, source, row, 1) < 0) {
      g_message("TIFF Bad data read on line %d\n", row);
      gimp_quit ();
    }

    /* Set s/bitsleft ready to use NEXTSAMPLE macro */

    s= source;
    bitsleft= 8;

    for (col = 0; col < cols; col++) {
      NEXTSAMPLE(d[col * (3 + alpha) + 1])
    }
    
    if (((row + 1) % tile_height) == 0 || row + 1 == rows) {
      gimp_pixel_rgn_set_rect (&pixel_rgn, dest, 0, start, cols, 1+row-start);
      gimp_progress_update ((double) row / (double) rows);
      start= row + 1;
    }
  }

  /* BLUE channel */
  for (start= 0, row = 0; row < rows; ++row) {
    d= dest + cols * (row % tile_height) * drawable->bpp;

    if ((row % tile_height) == 0) {
      if (rows - row < tile_height)
        gimp_pixel_rgn_get_rect(&pixel_rgn, dest, 0, start, cols, rows - row);
      else
        gimp_pixel_rgn_get_rect(&pixel_rgn, dest, 0, start, cols, tile_height);
      gimp_progress_update ((double) row / (double) rows);
    }

    if (TIFFReadScanline (tif, source, row, 2) < 0) {
      g_message("TIFF Bad data read on line %d\n", row);
      gimp_quit ();
    }

    /* Set s/bitsleft ready to use NEXTSAMPLE macro */

    s= source;
    bitsleft= 8;

    for (col = 0; col < cols; col++) {
      NEXTSAMPLE(d[col * (3 + alpha) + 2])
    }
    
    if (((row + 1) % tile_height) == 0 || row + 1 == rows) {
      gimp_pixel_rgn_set_rect (&pixel_rgn, dest, 0, start, cols, 1+row-start);
      gimp_progress_update ((double) row / (double) rows);
      start= row + 1;
    }
  }

  /* ALPHA channel */
  if (alpha) {
    for (start= 0, row = 0; row < rows; ++row) {
      d= dest + cols * (row % tile_height) * drawable->bpp;

      if ((row % tile_height) == 0) {
        if (rows - row < tile_height)
          gimp_pixel_rgn_get_rect(&pixel_rgn, dest, 0, start, cols, rows - row);
        else
          gimp_pixel_rgn_get_rect(&pixel_rgn, dest, 0, start, cols, tile_height);
        gimp_progress_update ((double) row / (double) rows);
      }

      if (TIFFReadScanline (tif, source, row, 3) < 0) {
        g_message("TIFF Bad data read on line %d\n", row);
        gimp_quit ();
      }

      /* Set s/bitsleft ready to use NEXTSAMPLE macro */
  
      s= source;
      bitsleft= 8;

      for (col = 0; col < cols; col++) {
        NEXTSAMPLE(d[col * 4 + 3])
      }
    
      if (((row + 1) % tile_height) == 0 || row + 1 == rows) {
        gimp_pixel_rgn_set_rect (&pixel_rgn, dest, 0, start, cols, 1+row-start);
        gimp_progress_update ((double) row / (double) rows);
        start= row + 1;
      }
    }
  }
}

/*
** pnmtotiff.c - converts a portable anymap to a Tagged Image File
**
** Derived by Jef Poskanzer from ras2tif.c, which is:
**
** Copyright (c) 1990 by Sun Microsystems, Inc.
**
** Author: Patrick J. Naughton
** naughton@wind.sun.com
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted,
** provided that the above copyright notice appear in all copies and that
** both that copyright notice and this permission notice appear in
** supporting documentation.
**
** This file is provided AS IS with no warranties of any kind.  The author
** shall have no liability with respect to the infringement of copyrights,
** trade secrets or any patents by this file or any part thereof.  In no
** event will the author be liable for any lost revenue or profits or
** other special, indirect and consequential damages.
*/

static gint save_image (char *filename, gint32 image, gint32 layer) {
  TIFF *tif;
  unsigned short red[256];
  unsigned short grn[256];
  unsigned short blu[256];
  int cols, col, rows, row, i;
  long g3options;
  long rowsperstrip;
  unsigned short compression;
  unsigned short fillorder;
  unsigned short extra_samples[1];
  int alpha;
  short predictor;
  short photometric;
  short samplesperpixel;
  short bitspersample;
  int bytesperrow;
  guchar *t, *src, *data;
  guchar *cmap;
  int colors;
  int success;
  GDrawable *drawable;
  GDrawableType drawable_type;
  GPixelRgn pixel_rgn;
  int tile_height;
  int y, yend;
  char *name;

  compression = tsvals.compression;
  fillorder = tsvals.fillorder;

  g3options = 0;
  predictor = 0;
  rowsperstrip = 0;

  tif = TIFFOpen (filename, "w");
  if (!tif) {
    g_print ("Can't write image to\n%s", filename);
    return 0;
  }

  name = malloc (strlen (filename) + 11);
  sprintf (name, "Saving %s:", filename);
  gimp_progress_init (name);
  free (name);

  drawable = gimp_drawable_get (layer);
  drawable_type = gimp_drawable_type (layer);
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, drawable->width, drawable->height, FALSE, FALSE);

  cols = drawable->width;
  rows = drawable->height;

  switch (drawable_type)
    {
    case RGB_IMAGE:
      predictor = 2;
      samplesperpixel = 3;
      bitspersample = 8;
      photometric = PHOTOMETRIC_RGB;
      bytesperrow = cols * 3;
      alpha = 0;
      break;
    case GRAY_IMAGE:
      samplesperpixel = 1;
      bitspersample = 8;
      photometric = PHOTOMETRIC_MINISBLACK;
      bytesperrow = cols;
      alpha = 0;
      break;
    case RGBA_IMAGE:
      predictor = 2;
      samplesperpixel = 4;
      bitspersample = 8;
      photometric = PHOTOMETRIC_RGB;
      bytesperrow = cols * 4;
      alpha = 1;
      break;
    case GRAYA_IMAGE:
      samplesperpixel = 2;
      bitspersample = 8;
      photometric = PHOTOMETRIC_MINISBLACK;
      bytesperrow = cols * 2;
      alpha = 1;
      break;
    case INDEXED_IMAGE:
      samplesperpixel = 1;
      bitspersample = 8;
      photometric = PHOTOMETRIC_PALETTE;
      bytesperrow = cols;
      alpha = 0;

      cmap = gimp_image_get_cmap (image, &colors);

      for (i = 0; i < colors; i++)
	{
	  red[i] = *cmap++ * 65535 / 255;
	  grn[i] = *cmap++ * 65535 / 255;
	  blu[i] = *cmap++ * 65535 / 255;
	}
      break;
    case INDEXEDA_IMAGE:
      return 0;
     default:
       return 0;
    }

  if (rowsperstrip == 0)
    rowsperstrip = (8 * 1024) / bytesperrow;
  if (rowsperstrip == 0)
    rowsperstrip = 1;

  /* Set TIFF parameters. */
  TIFFSetField (tif, TIFFTAG_IMAGEWIDTH, cols);
  TIFFSetField (tif, TIFFTAG_IMAGELENGTH, rows);
  TIFFSetField (tif, TIFFTAG_BITSPERSAMPLE, bitspersample);
  TIFFSetField (tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField (tif, TIFFTAG_COMPRESSION, compression);
  if ((compression == COMPRESSION_LZW) && (predictor != 0))
    TIFFSetField (tif, TIFFTAG_PREDICTOR, predictor);
  if (alpha)
    {
      extra_samples [0] = EXTRASAMPLE_ASSOCALPHA;
      TIFFSetField (tif, TIFFTAG_EXTRASAMPLES, 1, extra_samples);
    }
  TIFFSetField (tif, TIFFTAG_PHOTOMETRIC, photometric);
  TIFFSetField (tif, TIFFTAG_FILLORDER, fillorder);
  TIFFSetField (tif, TIFFTAG_DOCUMENTNAME, filename);
  TIFFSetField (tif, TIFFTAG_SAMPLESPERPIXEL, samplesperpixel);
  TIFFSetField (tif, TIFFTAG_ROWSPERSTRIP, rowsperstrip);
  /* TIFFSetField( tif, TIFFTAG_STRIPBYTECOUNTS, rows / rowsperstrip ); */
  TIFFSetField (tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);

#ifdef GIMP_HAVE_RESOLUTION_INFO
  /* resolution fields */
  {
      float xresolution;
      float yresolution;

      gimp_image_get_resolution (image, &xresolution, &yresolution);

      if (xresolution > 1e-5 && yresolution > 1e-5)
      {
	  TIFFSetField (tif, TIFFTAG_XRESOLUTION, xresolution);
	  TIFFSetField (tif, TIFFTAG_YRESOLUTION, yresolution);
	  TIFFSetField (tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
      }
  }
#endif /* GIMP_HAVE_RESOLUTION_INFO */

  /* do we have a comment?  If so, create a new parasite to hold it,
   * and attach it to the image. The attach function automatically
   * detaches a previous incarnation of the parasite. */
#ifdef GIMP_HAVE_PARASITES
  if (image_comment && *image_comment != '\000')
  {
    Parasite *parasite;

    TIFFSetField (tif, TIFFTAG_IMAGEDESCRIPTION, image_comment);
    parasite = parasite_new ("gimp-comment", 1,
			      strlen(image_comment)+1, image_comment);
    gimp_image_attach_parasite (image, parasite);
    parasite_free (parasite);
  }
#endif /* GIMP_HAVE_PARASITES */

  if (drawable_type == INDEXED_IMAGE)
    TIFFSetField (tif, TIFFTAG_COLORMAP, red, grn, blu);

  /* array to rearrange data */
  tile_height = gimp_tile_height ();
  src = g_new (guchar, bytesperrow * tile_height);
  data = g_new (guchar, bytesperrow);

  /* Now write the TIFF data. */
  for (y = 0; y < rows; y = yend)
    {
      yend = y + tile_height;
      yend = MIN (yend, rows);

      gimp_pixel_rgn_get_rect (&pixel_rgn, src, 0, y, cols, yend - y);

      for (row = y; row < yend; row++)
	{
	  t = src + bytesperrow * (row - y);

	  switch (drawable_type)
	    {
	    case INDEXED_IMAGE:
	      success = (TIFFWriteScanline (tif, t, row, 0) >= 0);
	      break;
	    case GRAY_IMAGE:
	      success = (TIFFWriteScanline (tif, t, row, 0) >= 0);
	      break;
	    case GRAYA_IMAGE:
	      for (col = 0; col < cols*samplesperpixel; col+=samplesperpixel)
		{
		  /* pre-multiply gray by alpha */
		  data[col + 0] = (t[col + 0] * t[col + 1]) / 255;
		  data[col + 1] = t[col + 1];  /* alpha channel */
		}
	      success = (TIFFWriteScanline (tif, data, row, 0) >= 0);
	      break;
	    case RGB_IMAGE:
	      success = (TIFFWriteScanline (tif, t, row, 0) >= 0);
	      break;
	    case RGBA_IMAGE:
	      for (col = 0; col < cols*samplesperpixel; col+=samplesperpixel)
		{
		  /* pre-multiply rgb by alpha */
		  data[col+0] = t[col + 0] * t[col + 3] / 255;
		  data[col+1] = t[col + 1] * t[col + 3] / 255;
		  data[col+2] = t[col + 2] * t[col + 3] / 255;
		  data[col+3] = t[col + 3];  /* alpha channel */
		}
	      success = (TIFFWriteScanline (tif, data, row, 0) >= 0);
	      break;
	    default:
	      success = FALSE;
	      break;
	    }

	  if (!success) {
	      g_message("TIFF Failed a scanline write on row %d", row);
	      return 0;
	  }
	}

      gimp_progress_update ((double) row / (double) rows);
    }

  TIFFFlushData (tif);
  TIFFClose (tif);

  gimp_drawable_detach (drawable);
  g_free (data);

  return 1;
}

static gint
save_dialog ()
{
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *toggle;
  GtkWidget *frame;
  GtkWidget *toggle_vbox;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *entry;
  GtkWidget *group;
  gint use_none = (tsvals.compression == COMPRESSION_NONE);
  gint use_lzw = (tsvals.compression == COMPRESSION_LZW);
  gint use_packbits = (tsvals.compression == COMPRESSION_PACKBITS);
  gint use_lsb2msb = (tsvals.fillorder == FILLORDER_LSB2MSB);
  gint use_msb2lsb = (tsvals.fillorder == FILLORDER_MSB2LSB);


  gtk_init ();

  dlg = gimp_dialog_new ("Save as Tiff");
  g_signal_connect (dlg, "destroy",
		      G_CALLBACK (save_close_callback),
		      NULL);

  /*  Action area  */
  button = gimp_dialog_add_button (dlg, "OK", NULL, NULL, TRUE);
  g_signal_connect (button, "clicked",
                      G_CALLBACK (save_ok_callback),
                      dlg);

  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			     G_CALLBACK (gtk_window_destroy), dlg);

  /* hbox for compression and fillorder settings */
  hbox = gimp_hbox_new (FALSE, 5);

  /*  compression  */
  frame = gtk_frame_new ("Compression");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (hbox, frame, TRUE, FALSE, 0);
  toggle_vbox = gimp_vbox_new (FALSE, 5);
  gimp_container_set_border_width (toggle_vbox, 5);
  gimp_container_add (frame, toggle_vbox);

  group = NULL;
  toggle = gimp_radio_button_new (group, "None");
  group = toggle;
  gimp_box_pack_start (toggle_vbox, toggle, FALSE, FALSE, 0);
  g_signal_connect (toggle, "toggled",
		      G_CALLBACK (save_toggle_update),
		      &use_none);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), use_none);

  toggle = gimp_radio_button_new (group, "LZW");
  group = toggle;
  gimp_box_pack_start (toggle_vbox, toggle, FALSE, FALSE, 0);
  g_signal_connect (toggle, "toggled",
		      G_CALLBACK (save_toggle_update),
		      &use_lzw);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), use_lzw);

  toggle = gimp_radio_button_new (group, "Pack Bits");
  group = toggle;
  gimp_box_pack_start (toggle_vbox, toggle, FALSE, FALSE, 0);
  g_signal_connect (toggle, "toggled",
		      G_CALLBACK (save_toggle_update),
		      &use_packbits);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), use_packbits);


  /*  fillorder  */
  frame = gtk_frame_new ("Fill Order");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (hbox, frame, TRUE, FALSE, 0);
  toggle_vbox = gimp_vbox_new (FALSE, 5);
  gimp_container_set_border_width (toggle_vbox, 5);
  gimp_container_add (frame, toggle_vbox);

  group = NULL;
  toggle = gimp_radio_button_new (group, "LSB to MSB");
  group = toggle;
  gimp_box_pack_start (toggle_vbox, toggle, FALSE, FALSE, 0);
  g_signal_connect (toggle, "toggled",
		      G_CALLBACK (save_toggle_update),
		      &use_lsb2msb);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), use_lsb2msb);

  toggle = gimp_radio_button_new (group, "MSB to LSB");
  group = toggle;
  gimp_box_pack_start (toggle_vbox, toggle, FALSE, FALSE, 0);
  g_signal_connect (toggle, "toggled",
		      G_CALLBACK (save_toggle_update),
		      &use_msb2lsb);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), use_msb2lsb);



  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), hbox, FALSE, TRUE, 0);


  /* comment entry */
  frame = gtk_frame_new(NULL);
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, FALSE, TRUE, 0);

  hbox = gimp_hbox_new (FALSE, 5);
  label = gtk_label_new ("Comment: ");
  gimp_box_pack_start (hbox, label, FALSE, TRUE, 0);
  entry = gtk_entry_new ();
  gimp_box_pack_start (hbox, entry, TRUE, TRUE, 0);
  gtk_editable_set_text (GTK_EDITABLE (entry), image_comment);
  g_signal_connect (entry, "changed",
                      G_CALLBACK (comment_entry_callback),
                      NULL);

  gimp_container_add (frame, hbox);

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  if (use_none)
    tsvals.compression = COMPRESSION_NONE;
  else if (use_lzw)
    tsvals.compression = COMPRESSION_LZW;
  else if (use_packbits)
    tsvals.compression = COMPRESSION_PACKBITS;

  if (use_lsb2msb)
    tsvals.fillorder = FILLORDER_LSB2MSB;
  else if (use_msb2lsb)
    tsvals.fillorder = FILLORDER_MSB2LSB;

  return tsint.run;
}


/*  Save interface functions  */

static void
save_close_callback (GtkWidget *widget,
		     gpointer   data)
{
  gimp_main_loop_quit ();
}

static void
save_ok_callback (GtkWidget *widget,
		  gpointer   data)
{
  tsint.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void
save_toggle_update (GtkWidget *widget,
		    gpointer   data)
{
  int *toggle_val;

  toggle_val = (int *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    *toggle_val = TRUE;
  else
    *toggle_val = FALSE;
}

static void
comment_entry_callback (GtkWidget *widget,
			gpointer   data)
{
  int len;
  const char *text;

  text = gtk_editable_get_text (GTK_EDITABLE (widget));
  len = strlen(text);

  /* Temporary kludge for overlength strings - just return */
  if (len > 240)
    {
      g_message ("TIFF save: Your comment string is too long.\n");
      return;
    }

  g_free(image_comment);
  image_comment = g_strdup(text);

  /* g_print ("COMMENT: %s\n", image_comment); */
}
