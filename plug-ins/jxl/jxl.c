/*
 *   JPEG XL plug-in for gimp42.
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
 *   main()             - Main entry - just call gimp_main()...
 *   query()            - Respond to a plug-in query...
 *   run()              - Run the plug-in...
 *   load_image()       - Load a JPEG XL image.
 *   save_image()       - Save a drawable as a JPEG XL image.
 *   save_dialog()      - Pop up the save dialog.
 *
 * Loading decodes the first frame (with an animation or a layered image,
 * everything shown at that point, composed by libjxl) to 8 bits per
 * channel: a gray image stays gray, alpha becomes the layer's alpha.
 * Images coded in XYB, which is how lossy JPEG XL is stored, are asked
 * for in sRGB; losslessly stored pixels come as they are.
 *
 * Saving writes the drawable at 8 bits per channel, tagged as sRGB (gray
 * drawables as gray, indexed ones expanded to RGB), either lossy at a
 * quality from 0 to 100 on the JPEG-like scale cjxl uses, which maps to a
 * Butteraugli distance, or lossless.  Effort 1 (fastest) to 9 (smallest)
 * sets how hard the encoder tries.
 *
 * Only libjxl API that exists from 0.7 up to at least 0.11 is used.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>

#include <jxl/decode.h>
#include <jxl/encode.h>
#include <jxl/thread_parallel_runner.h>
#include <jxl/version.h>

#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"


#define PLUG_IN_VERSION		"1.0 - 27 September 2026"
#define SCALE_WIDTH		125

/*  The limit every loader keeps to, whatever the format allows.  */
#define MAX_IMAGE_SIZE		262144

/*  libjxl's own default effort.  */
#define DEFAULT_EFFORT		7


typedef struct
{
  gdouble	quality;	/* 0..100, lossy only */
  gint		lossless;	/* Lossless compression */
  gint		effort;		/* 1 (fast) .. 9 (small) */
} JxlSaveVals;


static void	query (void);
static void	run (char *, int, GParam *, int *, GParam **);
static gint32	load_image (char *filename);
static gint	save_image (char *filename, gint32 image_ID,
			    gint32 drawable_ID);
static gint	save_dialog (void);
static void	save_close_callback (GtkWidget *widget, gpointer data);
static void	save_ok_callback (GtkWidget *widget, gpointer data);
static void	save_quality_update (GtkAdjustment *adjustment,
				     gpointer data);
static void	save_effort_update (GtkAdjustment *adjustment,
				    gpointer data);
static void	save_lossless_update (GtkWidget *widget, gpointer data);


GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

static JxlSaveVals jxlvals =
{
  90.0,    /* quality: distance 1.0, "visually lossless" */
  FALSE,   /* lossless */
  DEFAULT_EFFORT
};

static gint runme = FALSE;

/*  The quality controls, which "Lossless" makes insensitive.  */
static GtkWidget *quality_label = NULL;
static GtkWidget *quality_scale = NULL;


int
main (int   argc,
      char *argv[])
{
  return gimp_main (argc, argv);
}


static void
query (void)
{
  static GParamDef load_args[] =
  {
    { PARAM_INT32,  "run_mode",     "Interactive, non-interactive" },
    { PARAM_STRING, "filename",     "The name of the file to load" },
    { PARAM_STRING, "raw_filename", "The name of the file to load" },
  };
  static GParamDef load_return_vals[] =
  {
    { PARAM_IMAGE, "image", "Output image" },
  };
  static int nload_args = sizeof (load_args) / sizeof (load_args[0]);
  static int nload_return_vals = (sizeof (load_return_vals) /
				  sizeof (load_return_vals[0]));

  static GParamDef save_args[] =
  {
    { PARAM_INT32,    "run_mode",     "Interactive, non-interactive" },
    { PARAM_IMAGE,    "image",        "Input image" },
    { PARAM_DRAWABLE, "drawable",     "Drawable to save" },
    { PARAM_STRING,   "filename",     "The name of the file to save the image in" },
    { PARAM_STRING,   "raw_filename", "The name of the file to save the image in" },
    { PARAM_FLOAT,    "quality",      "Quality of lossy compression, on the JPEG-like scale of cjxl (0 - 100)" },
    { PARAM_INT32,    "lossless",     "Use lossless compression (TRUE or FALSE)" },
    { PARAM_INT32,    "effort",       "Encoder effort, from 1 (fastest) to 9 (smallest file); 0 for the default, 7" },
  };
  static int nsave_args = sizeof (save_args) / sizeof (save_args[0]);

  gimp_install_procedure ("file_jxl_load",
			  "Loads files in the JPEG XL file format",
			  "Loads JPEG XL images, bare codestreams or in the "
			  "container format, at 8 bits per channel.  Of an "
			  "animation, the first frame is loaded.",
			  "gimp42",
			  "gimp42",
			  PLUG_IN_VERSION,
			  "<Load>/JPEG XL",
			  NULL,
			  PROC_PLUG_IN,
			  nload_args, nload_return_vals,
			  load_args, load_return_vals);

  gimp_install_procedure ("file_jxl_save",
			  "Saves files in the JPEG XL file format",
			  "Saves the drawable as a lossy or lossless JPEG XL "
			  "image in sRGB, 8 bits per channel.  Quality 90 is "
			  "Butteraugli distance 1.0; lower is smaller.",
			  "gimp42",
			  "gimp42",
			  PLUG_IN_VERSION,
			  "<Save>/JPEG XL",
			  "RGB*, GRAY*, INDEXED*",
			  PROC_PLUG_IN,
			  nsave_args, 0,
			  save_args, NULL);

  /*  A bare codestream, and the ISO BMFF container ("JXL " box).  */
  gimp_register_magic_load_handler ("file_jxl_load", "jxl", "",
				    "0,string,\\377\\012,"
				    "0,string,\\000\\000\\000\\014JXL\\040");
  gimp_register_save_handler ("file_jxl_save", "jxl", "");
}


static void
run (char    *name,
     int      nparams,
     GParam  *param,
     int     *nreturn_vals,
     GParam **return_vals)
{
  static GParam values[2];
  gint32 image_ID;

  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type          = PARAM_STATUS;
  values[0].data.d_status = STATUS_SUCCESS;

  if (strcmp (name, "file_jxl_load") == 0)
    {
      image_ID = load_image (param[1].data.d_string);

      if (image_ID != -1)
	{
	  *nreturn_vals = 2;
	  values[1].type         = PARAM_IMAGE;
	  values[1].data.d_image = image_ID;
	}
      else
	values[0].data.d_status = STATUS_EXECUTION_ERROR;
    }
  else if (strcmp (name, "file_jxl_save") == 0)
    {
      switch (param[0].data.d_int32)
	{
	case RUN_INTERACTIVE:
	  gimp_get_data ("file_jxl_save", &jxlvals);

	  if (!save_dialog ())
	    {
	      /*  Cancelled: nothing was saved.  */
	      values[0].data.d_status = STATUS_CANCEL;
	      return;
	    }
	  break;

	case RUN_NONINTERACTIVE:
	  if (nparams != 8)
	    values[0].data.d_status = STATUS_CALLING_ERROR;
	  else
	    {
	      jxlvals.quality  = param[5].data.d_float;
	      jxlvals.lossless = param[6].data.d_int32 ? TRUE : FALSE;
	      jxlvals.effort   = param[7].data.d_int32;

	      /*  gimp_file_save passes zeros for the arguments it doesn't
	       *  know; take that as the default effort rather than fail.
	       */
	      if (jxlvals.effort == 0)
		jxlvals.effort = DEFAULT_EFFORT;

	      if (jxlvals.quality < 0.0 || jxlvals.quality > 100.0 ||
		  jxlvals.effort < 1 || jxlvals.effort > 9)
		values[0].data.d_status = STATUS_CALLING_ERROR;
	    }
	  break;

	case RUN_WITH_LAST_VALS:
	  gimp_get_data ("file_jxl_save", &jxlvals);
	  break;

	default:
	  break;
	}

      if (values[0].data.d_status == STATUS_SUCCESS)
	{
	  if (save_image (param[3].data.d_string, param[1].data.d_int32,
			  param[2].data.d_int32))
	    gimp_set_data ("file_jxl_save", &jxlvals, sizeof (jxlvals));
	  else
	    values[0].data.d_status = STATUS_EXECUTION_ERROR;
	}
    }
  else
    values[0].data.d_status = STATUS_CALLING_ERROR;
}


/*
 *  Loading
 */

/*  Copies width * height pixels of bpp bytes into a layer, a strip of
 *  tiles at a time.
 */

static void
layer_set_pixels (gint32        layer_ID,
		  const guchar *pixels,
		  gint          bpp)
{
  GDrawable *drawable;
  GPixelRgn  pixel_rgn;
  gint       tile_height;
  gint       width, height;
  gint       y, rows;

  drawable = gimp_drawable_get (layer_ID);
  width    = drawable->width;
  height   = drawable->height;

  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height,
		       TRUE, FALSE);

  tile_height = gimp_tile_height ();

  for (y = 0; y < height; y += rows)
    {
      rows = MIN (tile_height, height - y);
      gimp_pixel_rgn_set_rect (&pixel_rgn,
			       (guchar *) pixels + (gsize) y * width * bpp,
			       0, y, width, rows);
    }

  gimp_drawable_flush (drawable);
  gimp_drawable_detach (drawable);
}


static gint32
load_image (char *filename)
{
  JxlDecoder       *decoder;
  void             *runner;
  JxlDecoderStatus  status;
  JxlBasicInfo      info;
  JxlPixelFormat    format;
  JxlSignature      signature;
  GError           *error = NULL;
  gchar            *data = NULL;
  gsize             size = 0;
  guchar           *pixels = NULL;
  gsize             pixels_size = 0;
  gboolean          have_info = FALSE;
  gboolean          done = FALSE;
  gchar            *message = NULL;
  gchar            *basename;
  gchar            *progress;
  gint32            image_ID, layer_ID;
  gint              width = 0, height = 0, bpp = 0;
  gboolean          gray = FALSE, alpha = FALSE;

  if (!g_file_get_contents (filename, &data, &size, &error))
    {
      g_message ("JPEG XL: can't open %s: %s", filename, error->message);
      g_error_free (error);
      return -1;
    }

  signature = JxlSignatureCheck ((const uint8_t *) data, size);
  if (signature != JXL_SIG_CODESTREAM && signature != JXL_SIG_CONTAINER)
    {
      g_message ("JPEG XL: %s is not a JPEG XL file", filename);
      g_free (data);
      return -1;
    }

  basename = g_path_get_basename (filename);
  progress = g_strdup_printf ("Loading %s:", basename);
  gimp_progress_init (progress);
  g_free (progress);
  g_free (basename);

  memset (&info, 0, sizeof (info));
  memset (&format, 0, sizeof (format));

  decoder = JxlDecoderCreate (NULL);
  runner  = JxlThreadParallelRunnerCreate (NULL,
				JxlThreadParallelRunnerDefaultNumWorkerThreads ());

  if (!decoder || !runner ||
      JxlDecoderSetParallelRunner (decoder, JxlThreadParallelRunner,
				   runner) != JXL_DEC_SUCCESS ||
      JxlDecoderSubscribeEvents (decoder,
				 JXL_DEC_BASIC_INFO |
				 JXL_DEC_COLOR_ENCODING |
				 JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS ||
      JxlDecoderSetUnpremultiplyAlpha (decoder, JXL_TRUE) != JXL_DEC_SUCCESS ||
      JxlDecoderSetInput (decoder, (const uint8_t *) data,
			  size) != JXL_DEC_SUCCESS)
    {
      message = g_strdup_printf ("JPEG XL: can't set up the decoder for %s",
				 filename);
      goto out;
    }

  /*  All of the file is there: running out of it is an error.  */
  JxlDecoderCloseInput (decoder);

  while (!done)
    {
      status = JxlDecoderProcessInput (decoder);

      switch (status)
	{
	case JXL_DEC_BASIC_INFO:
	  if (JxlDecoderGetBasicInfo (decoder, &info) != JXL_DEC_SUCCESS)
	    {
	      message = g_strdup_printf ("JPEG XL: error while reading %s",
					 filename);
	      goto out;
	    }

	  /*  The decoder turns the image by its orientation and reports
	   *  the size it comes out at.
	   */
	  if (info.xsize < 1 || info.ysize < 1 ||
	      info.xsize > MAX_IMAGE_SIZE || info.ysize > MAX_IMAGE_SIZE ||
	      (info.num_color_channels != 1 && info.num_color_channels != 3))
	    {
	      message = g_strdup_printf ("JPEG XL: %s has unsupported image "
					 "dimensions", filename);
	      goto out;
	    }

	  width  = (gint) info.xsize;
	  height = (gint) info.ysize;
	  gray   = info.num_color_channels == 1;
	  alpha  = info.alpha_bits > 0;
	  bpp    = (gray ? 1 : 3) + (alpha ? 1 : 0);

	  if (!g_size_checked_mul (&pixels_size, (gsize) width * bpp, height))
	    {
	      message = g_strdup_printf ("JPEG XL: %s has unsupported image "
					 "dimensions", filename);
	      goto out;
	    }

	  format.num_channels = bpp;
	  format.data_type    = JXL_TYPE_UINT8;
	  format.endianness   = JXL_NATIVE_ENDIAN;
	  format.align        = 0;

	  have_info = TRUE;
	  break;

	case JXL_DEC_COLOR_ENCODING:
	  {
	    JxlColorEncoding srgb;

	    /*  Only a preference: it applies to XYB-coded (lossy) images,
	     *  which the decoder would otherwise hand out in linear light.
	     *  Other images come in the colour space they were stored in.
	     */
	    JxlColorEncodingSetToSRGB (&srgb, gray ? JXL_TRUE : JXL_FALSE);
	    JxlDecoderSetPreferredColorProfile (decoder, &srgb);
	  }
	  break;

	case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
	  {
	    size_t needed = 0;

	    if (!have_info ||
		JxlDecoderImageOutBufferSize (decoder, &format,
					      &needed) != JXL_DEC_SUCCESS ||
		needed != pixels_size)
	      {
		message = g_strdup_printf ("JPEG XL: error while reading %s",
					   filename);
		goto out;
	      }

	    if (!pixels)
	      {
		pixels = g_try_malloc (pixels_size);
		if (!pixels)
		  {
		    message = g_strdup_printf ("JPEG XL: not enough memory to "
					       "load %s", filename);
		    goto out;
		  }
	      }

	    if (JxlDecoderSetImageOutBuffer (decoder, &format, pixels,
					     pixels_size) != JXL_DEC_SUCCESS)
	      {
		message = g_strdup_printf ("JPEG XL: error while reading %s",
					   filename);
		goto out;
	      }
	  }
	  break;

	case JXL_DEC_FULL_IMAGE:
	  /*  The first frame is all we want.  */
	  if (pixels)
	    done = TRUE;
	  break;

	case JXL_DEC_SUCCESS:
	  if (!pixels)
	    {
	      message = g_strdup_printf ("JPEG XL: %s holds no image",
					 filename);
	      goto out;
	    }
	  done = TRUE;
	  break;

	case JXL_DEC_NEED_MORE_INPUT:
	  message = g_strdup_printf ("JPEG XL: %s is truncated", filename);
	  goto out;

	case JXL_DEC_ERROR:
	default:
	  message = g_strdup_printf ("JPEG XL: error while reading %s",
				     filename);
	  goto out;
	}
    }

 out:
  if (decoder)
    JxlDecoderDestroy (decoder);
  if (runner)
    JxlThreadParallelRunnerDestroy (runner);
  g_free (data);

  if (message)
    {
      g_message ("%s", message);
      g_free (message);
      g_free (pixels);
      return -1;
    }

  gimp_progress_update (0.5);

  image_ID = gimp_image_new (width, height, gray ? GRAY : RGB);
  if (image_ID == -1)
    {
      g_free (pixels);
      return -1;
    }
  gimp_image_set_filename (image_ID, filename);

  layer_ID = gimp_layer_new (image_ID, "Background", width, height,
			     gray ? (alpha ? GRAYA_IMAGE : GRAY_IMAGE)
				  : (alpha ? RGBA_IMAGE : RGB_IMAGE),
			     100, NORMAL_MODE);
  gimp_image_add_layer (image_ID, layer_ID, 0);

  layer_set_pixels (layer_ID, pixels, bpp);
  g_free (pixels);

  gimp_progress_update (1.0);

  return image_ID;
}


/*
 *  Saving
 */

/*  Reads a drawable as gray or RGB, with alpha when it has some; indexed
 *  pixels are expanded to RGB.  Returns NULL when the drawable can't be
 *  read.
 */

static guchar *
drawable_get_pixels (gint32        drawable_ID,
		     const guchar *cmap,
		     gint          ncolors,
		     gint         *width_out,
		     gint         *height_out,
		     gboolean     *gray_out,
		     gboolean     *alpha_out)
{
  GDrawable     *drawable;
  GDrawableType  type;
  GPixelRgn      pixel_rgn;
  guchar        *src, *dst;
  gsize          npixels, src_size, dst_size, i;
  gint           width, height, src_bpp, bpp;
  gboolean       alpha;

  drawable = gimp_drawable_get (drawable_ID);
  type     = gimp_drawable_type (drawable_ID);
  width    = drawable->width;
  height   = drawable->height;
  src_bpp  = drawable->bpp;

  switch (type)
    {
    case RGB_IMAGE:
    case GRAY_IMAGE:
    case INDEXED_IMAGE:
      alpha = FALSE;
      break;
    case RGBA_IMAGE:
    case GRAYA_IMAGE:
    case INDEXEDA_IMAGE:
      alpha = TRUE;
      break;
    default:
      g_message ("JPEG XL: can't save this image type");
      gimp_drawable_detach (drawable);
      return NULL;
    }

  /*  Gray and RGB come as they are; indexed becomes RGB.  */
  if (type == INDEXED_IMAGE || type == INDEXEDA_IMAGE)
    bpp = alpha ? 4 : 3;
  else
    bpp = src_bpp;

  if (width < 1 || height < 1 || src_bpp < 1 || src_bpp > 4 ||
      !g_size_checked_mul (&npixels, width, height) ||
      !g_size_checked_mul (&src_size, npixels, src_bpp) ||
      !g_size_checked_mul (&dst_size, npixels, bpp))
    {
      g_message ("JPEG XL: the drawable is too large");
      gimp_drawable_detach (drawable);
      return NULL;
    }

  src = g_try_malloc (src_size);
  if (!src)
    {
      g_message ("JPEG XL: not enough memory to save the image");
      gimp_drawable_detach (drawable);
      return NULL;
    }

  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height,
		       FALSE, FALSE);
  gimp_pixel_rgn_get_rect (&pixel_rgn, src, 0, 0, width, height);
  gimp_drawable_detach (drawable);

  if (bpp == src_bpp)
    dst = src;
  else
    {
      dst = g_try_malloc (dst_size);
      if (!dst)
	{
	  g_message ("JPEG XL: not enough memory to save the image");
	  g_free (src);
	  return NULL;
	}

      for (i = 0; i < npixels; i++)
	{
	  const guchar *s = src + i * src_bpp;
	  guchar       *d = dst + i * bpp;

	  /*  An index past the colormap reads as black.  */
	  if (cmap && s[0] < ncolors)
	    {
	      d[0] = cmap[s[0] * 3];
	      d[1] = cmap[s[0] * 3 + 1];
	      d[2] = cmap[s[0] * 3 + 2];
	    }
	  else
	    d[0] = d[1] = d[2] = 0;

	  if (alpha)
	    d[3] = s[1];
	}

      g_free (src);
    }

  *width_out  = width;
  *height_out = height;
  *gray_out   = (type == GRAY_IMAGE || type == GRAYA_IMAGE);
  *alpha_out  = alpha;

  return dst;
}


/*  The JPEG-like quality scale of cjxl, as a Butteraugli distance: 90 is
 *  1.0 ("visually lossless"), 100 would be 0.  The distance is kept from
 *  0.1, since 0 alone is not lossless, to 15, the most libjxl 0.7 takes.
 */

static float
quality_to_distance (gdouble quality)
{
  gdouble distance;

#if JPEGXL_NUMERIC_VERSION >= JPEGXL_COMPUTE_NUMERIC_VERSION (0, 9, 0)
  distance = JxlEncoderDistanceFromQuality ((float) quality);
#else
  if (quality >= 100.0)
    distance = 0.0;
  else if (quality >= 30.0)
    distance = 0.1 + (100.0 - quality) * 0.09;
  else
    distance = (53.0 / 3000.0 * quality * quality -
		23.0 / 20.0 * quality + 25.0);
#endif

  return (float) CLAMP (distance, 0.1, 15.0);
}


static gint
save_image (char   *filename,
	    gint32  image_ID,
	    gint32  drawable_ID)
{
  JxlEncoder             *encoder = NULL;
  JxlEncoderFrameSettings *settings;
  JxlEncoderStatus        status;
  JxlBasicInfo            info;
  JxlColorEncoding        color;
  JxlPixelFormat          format;
  void                   *runner = NULL;
  guchar                 *cmap = NULL;
  gint                    ncolors = 0;
  guchar                 *pixels = NULL;
  guint8                 *output = NULL;
  gsize                   output_size, used;
  uint8_t                *next_out;
  size_t                  avail_out;
  gint                    width, height;
  gboolean                gray, alpha;
  gint                    channels;
  gchar                  *basename;
  gchar                  *progress;
  gboolean                ok = FALSE;
  FILE                   *fp;

  basename = g_path_get_basename (filename);
  progress = g_strdup_printf ("Saving %s:", basename);
  gimp_progress_init (progress);
  g_free (progress);
  g_free (basename);

  if (gimp_image_base_type (image_ID) == INDEXED)
    {
      cmap = gimp_image_get_cmap (image_ID, &ncolors);
      ncolors = CLAMP (ncolors, 0, 256);
    }

  pixels = drawable_get_pixels (drawable_ID, cmap, ncolors,
				&width, &height, &gray, &alpha);
  g_free (cmap);
  if (!pixels)
    return FALSE;

  channels = (gray ? 1 : 3) + (alpha ? 1 : 0);

  encoder = JxlEncoderCreate (NULL);
  runner  = JxlThreadParallelRunnerCreate (NULL,
				JxlThreadParallelRunnerDefaultNumWorkerThreads ());
  if (!encoder || !runner ||
      JxlEncoderSetParallelRunner (encoder, JxlThreadParallelRunner,
				   runner) != JXL_ENC_SUCCESS)
    {
      g_message ("JPEG XL: can't set up the encoder");
      goto out;
    }

  /*  With alpha_bits set, the one extra channel is alpha; libjxl
   *  describes it itself.
   */
  JxlEncoderInitBasicInfo (&info);
  info.xsize                    = width;
  info.ysize                    = height;
  info.bits_per_sample          = 8;
  info.exponent_bits_per_sample = 0;
  info.num_color_channels       = gray ? 1 : 3;
  info.num_extra_channels       = alpha ? 1 : 0;
  info.alpha_bits               = alpha ? 8 : 0;
  info.alpha_exponent_bits      = 0;
  /*  Lossless must keep the pixels in their own colour space, lossy
   *  compresses best in XYB.
   */
  info.uses_original_profile    = jxlvals.lossless ? JXL_TRUE : JXL_FALSE;

  JxlColorEncodingSetToSRGB (&color, gray ? JXL_TRUE : JXL_FALSE);

  if (JxlEncoderSetBasicInfo (encoder, &info) != JXL_ENC_SUCCESS ||
      JxlEncoderSetColorEncoding (encoder, &color) != JXL_ENC_SUCCESS)
    {
      g_message ("JPEG XL: can't encode an image like this one");
      goto out;
    }

  settings = JxlEncoderFrameSettingsCreate (encoder, NULL);
  if (!settings ||
      JxlEncoderFrameSettingsSetOption (settings,
					JXL_ENC_FRAME_SETTING_EFFORT,
					CLAMP (jxlvals.effort, 1, 9))
	!= JXL_ENC_SUCCESS)
    {
      g_message ("JPEG XL: can't set up the encoder");
      goto out;
    }

  if (jxlvals.lossless)
    {
      status = JxlEncoderSetFrameDistance (settings, 0.0f);
      if (status == JXL_ENC_SUCCESS)
	status = JxlEncoderSetFrameLossless (settings, JXL_TRUE);
    }
  else
    status = JxlEncoderSetFrameDistance (settings,
				quality_to_distance (jxlvals.quality));

  if (status != JXL_ENC_SUCCESS)
    {
      g_message ("JPEG XL: can't set up the encoder");
      goto out;
    }

  format.num_channels = channels;
  format.data_type    = JXL_TYPE_UINT8;
  format.endianness   = JXL_NATIVE_ENDIAN;
  format.align        = 0;

  if (JxlEncoderAddImageFrame (settings, &format, pixels,
			       (gsize) width * height * channels)
      != JXL_ENC_SUCCESS)
    {
      g_message ("JPEG XL: error %d while encoding %s",
		 (int) JxlEncoderGetError (encoder), filename);
      goto out;
    }

  JxlEncoderCloseInput (encoder);

  g_free (pixels);
  pixels = NULL;

  gimp_progress_update (0.5);

  output_size = 65536;
  output      = g_malloc (output_size);
  next_out    = output;
  avail_out   = output_size;

  while ((status = JxlEncoderProcessOutput (encoder, &next_out,
					    &avail_out))
	 == JXL_ENC_NEED_MORE_OUTPUT)
    {
      used         = next_out - output;
      output_size *= 2;
      output       = g_realloc (output, output_size);
      next_out     = output + used;
      avail_out    = output_size - used;
    }

  if (status != JXL_ENC_SUCCESS)
    {
      g_message ("JPEG XL: error %d while encoding %s",
		 (int) JxlEncoderGetError (encoder), filename);
      goto out;
    }

  used = next_out - output;

  fp = g_fopen (filename, "wb");
  if (!fp)
    {
      g_message ("JPEG XL: can't create %s", filename);
      goto out;
    }

  ok = fwrite (output, 1, used, fp) == used;
  if (fclose (fp) != 0)
    ok = FALSE;

  if (!ok)
    g_message ("JPEG XL: error while writing %s", filename);

 out:
  if (encoder)
    JxlEncoderDestroy (encoder);
  if (runner)
    JxlThreadParallelRunnerDestroy (runner);
  g_free (pixels);
  g_free (output);

  return ok;
}


/*
 *  The save dialog
 */

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
  runme = TRUE;

  gtk_window_destroy (GTK_WINDOW (data));
}


static void
save_quality_update (GtkAdjustment *adjustment,
		     gpointer       data)
{
  jxlvals.quality = gtk_adjustment_get_value (adjustment);
}


static void
save_effort_update (GtkAdjustment *adjustment,
		    gpointer       data)
{
  jxlvals.effort = (gint) (gtk_adjustment_get_value (adjustment) + 0.5);
}


static void
save_lossless_update (GtkWidget *widget,
		      gpointer   data)
{
  jxlvals.lossless = gtk_check_button_get_active (GTK_CHECK_BUTTON (widget));

  gtk_widget_set_sensitive (quality_label, !jxlvals.lossless);
  gtk_widget_set_sensitive (quality_scale, !jxlvals.lossless);
}


static gint
save_dialog (void)
{
  GtkWidget     *dlg;
  GtkWidget     *button;
  GtkWidget     *frame;
  GtkWidget     *table;
  GtkWidget     *toggle;
  GtkWidget     *label;
  GtkWidget     *scale;
  GtkAdjustment *adjustment;

  gtk_init ();

  dlg = gimp_dialog_new ("Save as JPEG XL");
  g_signal_connect (dlg, "destroy", G_CALLBACK (save_close_callback), NULL);

  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (save_ok_callback), dlg,
			  TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  frame = gtk_frame_new ("Parameter Settings");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);

  table = gimp_table_new (3, 2, FALSE);
  gimp_container_set_border_width (table, 10);
  gimp_container_add (frame, table);

  quality_label = gtk_label_new ("Quality");
  gimp_misc_set_alignment (quality_label, 0.0, 0.5);
  gimp_table_attach (table, quality_label, 0, 1, 0, 1, GIMP_FILL, 0, 5, 0);

  adjustment = gtk_adjustment_new (jxlvals.quality, 0.0, 100.0,
				   1.0, 10.0, 0.0);
  quality_scale = gimp_hscale_new (adjustment, 0);
  gtk_widget_set_size_request (quality_scale, SCALE_WIDTH, -1);
  gimp_table_attach (table, quality_scale, 1, 2, 0, 1,
		     GIMP_EXPAND | GIMP_FILL, 0, 0, 0);
  g_signal_connect (adjustment, "value-changed",
		    G_CALLBACK (save_quality_update), NULL);

  toggle = gtk_check_button_new_with_label ("Lossless");
  gimp_table_attach (table, toggle, 0, 2, 1, 2, GIMP_FILL, 0, 0, 0);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), jxlvals.lossless);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (save_lossless_update), NULL);
  gtk_widget_set_sensitive (quality_label, !jxlvals.lossless);
  gtk_widget_set_sensitive (quality_scale, !jxlvals.lossless);

  label = gtk_label_new ("Effort");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 2, 3, GIMP_FILL, 0, 5, 0);

  adjustment = gtk_adjustment_new (CLAMP (jxlvals.effort, 1, 9), 1.0, 9.0,
				   1.0, 1.0, 0.0);
  scale = gimp_hscale_new (adjustment, 0);
  gtk_widget_set_size_request (scale, SCALE_WIDTH, -1);
  gtk_widget_set_tooltip_text (scale, "1 is fastest, 9 gives the "
			       "smallest file");
  gimp_table_attach (table, scale, 1, 2, 2, 3,
		     GIMP_EXPAND | GIMP_FILL, 0, 0, 0);
  g_signal_connect (adjustment, "value-changed",
		    G_CALLBACK (save_effort_update), NULL);

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return runme;
}
