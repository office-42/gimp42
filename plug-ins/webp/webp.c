/*
 *   WebP plug-in for gimp42.
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
 *   load_image()       - Load a WebP image, still or animated.
 *   save_image()       - Save a drawable, or all layers as an animation.
 *   save_dialog()      - Pop up the save dialog.
 *
 * A still image loads as one RGB layer, RGBA when the file has alpha.  An
 * animation loads through libwebpdemux's WebPAnimDecoder, which hands out
 * every frame already composed on the full canvas; each becomes a layer
 * named "Frame N (DDDms)", the first frame at the bottom, the way the GIF
 * plug-in names them, so that saving the image again keeps the timing.
 *
 * Saving writes the drawable as a still image, or with "animation" every
 * layer as one frame through libwebpmux's WebPAnimEncoder.  Each frame is
 * the layer at its offsets on a transparent canvas of the image size; a
 * layer whose name says "(combine)" (the GIF plug-in's tag) is laid over
 * the previous frame instead.  A frame lasts as long as the "(NNNms)" tag
 * in its layer name says, 100 ms without one.  Gray and indexed drawables
 * are expanded to RGB, since WebP has neither.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>

#include <webp/decode.h>
#include <webp/encode.h>
#include <webp/demux.h>
#include <webp/mux.h>

#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"


#define PLUG_IN_VERSION		"1.0 - 27 September 2026"
#define SCALE_WIDTH		125

/*  The limit every loader keeps to, whatever the format allows.  */
#define MAX_IMAGE_SIZE		262144

/*  WebP stores a frame's duration in 24 bits.  */
#define DEFAULT_DELAY		100
#define MAX_DELAY		0xffffff


typedef struct
{
  gdouble	quality;	/* 0..100, lossy only */
  gint		lossless;	/* Lossless compression */
  gint		animation;	/* Save all layers as frames */
} WebpSaveVals;


static void	query (void);
static void	run (char *, int, GParam *, int *, GParam **);
static gint32	load_image (char *filename);
static gint32	load_still (char *filename, const guint8 *data, gsize size,
			    const WebPBitstreamFeatures *features);
static gint32	load_animation (char *filename, const guint8 *data,
				gsize size);
static gint	save_image (char *filename, gint32 image_ID,
			    gint32 drawable_ID);
static gint	save_dialog (gint32 image_ID);
static void	save_close_callback (GtkWidget *widget, gpointer data);
static void	save_ok_callback (GtkWidget *widget, gpointer data);
static void	save_quality_update (GtkAdjustment *adjustment,
				     gpointer data);
static void	save_lossless_update (GtkWidget *widget, gpointer data);
static void	save_animation_update (GtkWidget *widget, gpointer data);


GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

static WebpSaveVals webpvals =
{
  90.0,    /* quality */
  FALSE,   /* lossless */
  TRUE     /* animation */
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
    { PARAM_FLOAT,    "quality",      "Quality of lossy compression (0 - 100)" },
    { PARAM_INT32,    "lossless",     "Use lossless compression (TRUE or FALSE)" },
    { PARAM_INT32,    "animation",    "Save all layers as an animation, the delays taken from \"(NNNms)\" in the layer names (TRUE or FALSE)" },
  };
  static int nsave_args = sizeof (save_args) / sizeof (save_args[0]);

  gimp_install_procedure ("file_webp_load",
			  "Loads files in the WebP file format",
			  "Loads still and animated WebP images.  An "
			  "animation becomes one layer per frame, the first "
			  "frame at the bottom, each named \"Frame N (DDDms)\" "
			  "with its duration.",
			  "gimp42",
			  "gimp42",
			  PLUG_IN_VERSION,
			  "<Load>/WebP",
			  NULL,
			  PROC_PLUG_IN,
			  nload_args, nload_return_vals,
			  load_args, load_return_vals);

  gimp_install_procedure ("file_webp_save",
			  "Saves files in the WebP file format",
			  "Saves the drawable as a lossy or lossless WebP "
			  "image, or with \"animation\" every layer as a "
			  "frame of an animated WebP: bottom layer first, "
			  "each lasting as many milliseconds as the "
			  "\"(NNNms)\" in its name says (100 without one).  "
			  "A layer named with \"(combine)\" is drawn over "
			  "the previous frame, any other on a clear canvas.",
			  "gimp42",
			  "gimp42",
			  PLUG_IN_VERSION,
			  "<Save>/WebP",
			  "RGB*, GRAY*, INDEXED*",
			  PROC_PLUG_IN,
			  nsave_args, 0,
			  save_args, NULL);

  gimp_register_magic_load_handler ("file_webp_load", "webp", "",
				    "8,string,WEBP");
  gimp_register_save_handler ("file_webp_save", "webp", "");
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

  if (strcmp (name, "file_webp_load") == 0)
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
  else if (strcmp (name, "file_webp_save") == 0)
    {
      switch (param[0].data.d_int32)
	{
	case RUN_INTERACTIVE:
	  gimp_get_data ("file_webp_save", &webpvals);

	  if (!save_dialog (param[1].data.d_int32))
	    {
	      /*  Cancelled: nothing was saved.  */
	      values[0].data.d_status = STATUS_EXECUTION_ERROR;
	      return;
	    }
	  break;

	case RUN_NONINTERACTIVE:
	  if (nparams != 8)
	    values[0].data.d_status = STATUS_CALLING_ERROR;
	  else
	    {
	      webpvals.quality   = param[5].data.d_float;
	      webpvals.lossless  = param[6].data.d_int32 ? TRUE : FALSE;
	      webpvals.animation = param[7].data.d_int32 ? TRUE : FALSE;

	      if (webpvals.quality < 0.0 || webpvals.quality > 100.0)
		values[0].data.d_status = STATUS_CALLING_ERROR;
	    }
	  break;

	case RUN_WITH_LAST_VALS:
	  gimp_get_data ("file_webp_save", &webpvals);
	  break;

	default:
	  break;
	}

      if (values[0].data.d_status == STATUS_SUCCESS)
	{
	  if (save_image (param[3].data.d_string, param[1].data.d_int32,
			  param[2].data.d_int32))
	    gimp_set_data ("file_webp_save", &webpvals, sizeof (webpvals));
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

/*  Copies a buffer of width * height pixels of src_bpp bytes into a layer
 *  of dst_bpp bytes per pixel, a strip of tiles at a time; with fewer
 *  bytes in the layer, the pixels' last byte (alpha) is left out.
 */

static void
layer_set_pixels (gint32        layer_ID,
		  const guchar *pixels,
		  gint          src_bpp,
		  gint          dst_bpp)
{
  GDrawable *drawable;
  GPixelRgn  pixel_rgn;
  guchar    *strip = NULL;
  gint       tile_height;
  gint       width, height;
  gint       y, rows;

  drawable = gimp_drawable_get (layer_ID);
  width    = drawable->width;
  height   = drawable->height;

  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height,
		       TRUE, FALSE);

  tile_height = gimp_tile_height ();
  if (src_bpp != dst_bpp)
    strip = g_new (guchar, (gsize) tile_height * width * dst_bpp);

  for (y = 0; y < height; y += rows)
    {
      const guchar *src = pixels + (gsize) y * width * src_bpp;

      rows = MIN (tile_height, height - y);

      if (strip)
	{
	  gsize i, n = (gsize) rows * width;

	  for (i = 0; i < n; i++)
	    memcpy (strip + i * dst_bpp, src + i * src_bpp, dst_bpp);

	  gimp_pixel_rgn_set_rect (&pixel_rgn, strip, 0, y, width, rows);
	}
      else
	gimp_pixel_rgn_set_rect (&pixel_rgn, (guchar *) src,
				 0, y, width, rows);
    }

  g_free (strip);

  gimp_drawable_flush (drawable);
  gimp_drawable_detach (drawable);
}


static gint32
load_image (char *filename)
{
  WebPBitstreamFeatures features;
  GError  *error = NULL;
  gchar   *data  = NULL;
  gsize    size  = 0;
  gchar   *basename;
  gchar   *progress;
  gint32   image_ID;

  if (!g_file_get_contents (filename, &data, &size, &error))
    {
      g_message ("WebP: can't open %s: %s", filename, error->message);
      g_error_free (error);
      return -1;
    }

  if (WebPGetFeatures ((const uint8_t *) data, size,
		       &features) != VP8_STATUS_OK)
    {
      g_message ("WebP: %s is not a valid WebP file", filename);
      g_free (data);
      return -1;
    }

  basename = g_path_get_basename (filename);
  progress = g_strdup_printf ("Loading %s:", basename);
  gimp_progress_init (progress);
  g_free (progress);
  g_free (basename);

  if (features.has_animation)
    image_ID = load_animation (filename, (const guint8 *) data, size);
  else
    image_ID = load_still (filename, (const guint8 *) data, size,
			   &features);

  g_free (data);

  return image_ID;
}


static gint32
load_still (char                        *filename,
	    const guint8                *data,
	    gsize                        size,
	    const WebPBitstreamFeatures *features)
{
  gint32   image_ID, layer_ID;
  guchar  *pixels;
  gsize    buffer_size;
  gint     width  = features->width;
  gint     height = features->height;
  gint     bpp    = features->has_alpha ? 4 : 3;
  gboolean ok;

  if (width < 1 || height < 1 ||
      width > MAX_IMAGE_SIZE || height > MAX_IMAGE_SIZE ||
      !g_size_checked_mul (&buffer_size, (gsize) width * bpp, height))
    {
      g_message ("WebP: %s has unsupported image dimensions", filename);
      return -1;
    }

  pixels = g_try_malloc (buffer_size);
  if (!pixels)
    {
      g_message ("WebP: not enough memory to load %s", filename);
      return -1;
    }

  if (bpp == 4)
    ok = WebPDecodeRGBAInto (data, size, pixels, buffer_size,
			     width * bpp) != NULL;
  else
    ok = WebPDecodeRGBInto (data, size, pixels, buffer_size,
			    width * bpp) != NULL;

  if (!ok)
    {
      g_message ("WebP: error while reading %s", filename);
      g_free (pixels);
      return -1;
    }

  gimp_progress_update (0.5);

  image_ID = gimp_image_new (width, height, RGB);
  if (image_ID == -1)
    {
      g_free (pixels);
      return -1;
    }
  gimp_image_set_filename (image_ID, filename);

  layer_ID = gimp_layer_new (image_ID, "Background", width, height,
			     bpp == 4 ? RGBA_IMAGE : RGB_IMAGE,
			     100, NORMAL_MODE);
  gimp_image_add_layer (image_ID, layer_ID, 0);

  layer_set_pixels (layer_ID, pixels, bpp, bpp);
  g_free (pixels);

  gimp_progress_update (1.0);

  return image_ID;
}


static gboolean
pixels_opaque (const guchar *rgba,
	       gsize         npixels)
{
  gsize i;

  for (i = 0; i < npixels; i++)
    if (rgba[i * 4 + 3] != 255)
      return FALSE;

  return TRUE;
}


static gint32
load_animation (char         *filename,
		const guint8 *data,
		gsize         size)
{
  WebPAnimDecoderOptions options;
  WebPAnimDecoder *decoder;
  WebPAnimInfo     info;
  WebPData         webp_data;
  gint32           image_ID = -1;
  gsize            npixels, canvas_size;
  gint             width, height;
  gint             frame = 0;
  gint             previous_timestamp = 0;

  if (!WebPAnimDecoderOptionsInit (&options))
    return -1;

  options.color_mode  = MODE_RGBA;
  options.use_threads = 0;

  webp_data.bytes = data;
  webp_data.size  = size;

  decoder = WebPAnimDecoderNew (&webp_data, &options);
  if (!decoder)
    {
      g_message ("WebP: %s is not a valid WebP animation", filename);
      return -1;
    }

  if (!WebPAnimDecoderGetInfo (decoder, &info))
    {
      g_message ("WebP: %s is not a valid WebP animation", filename);
      WebPAnimDecoderDelete (decoder);
      return -1;
    }

  width  = (gint) info.canvas_width;
  height = (gint) info.canvas_height;

  /*  The decoder hands out frames of canvas_size bytes.  */
  if (info.canvas_width < 1 || info.canvas_height < 1 ||
      info.canvas_width > MAX_IMAGE_SIZE ||
      info.canvas_height > MAX_IMAGE_SIZE ||
      !g_size_checked_mul (&npixels, width, height) ||
      !g_size_checked_mul (&canvas_size, npixels, 4))
    {
      g_message ("WebP: %s has unsupported image dimensions", filename);
      WebPAnimDecoderDelete (decoder);
      return -1;
    }

  if (info.frame_count < 1)
    {
      g_message ("WebP: the animation in %s has no frames", filename);
      WebPAnimDecoderDelete (decoder);
      return -1;
    }

  while (WebPAnimDecoderHasMoreFrames (decoder))
    {
      uint8_t  *canvas;
      int       timestamp;
      gint      delay;
      gboolean  alpha;
      gchar    *layer_name;
      gint32    layer_ID;

      if (!WebPAnimDecoderGetNext (decoder, &canvas, &timestamp))
	{
	  g_message ("WebP: error while reading frame %d of %s",
		     frame + 1, filename);
	  break;
	}

      if (image_ID == -1)
	{
	  image_ID = gimp_image_new (width, height, RGB);
	  if (image_ID == -1)
	    break;
	  gimp_image_set_filename (image_ID, filename);
	}

      frame++;

      delay = timestamp - previous_timestamp;
      delay = CLAMP (delay, 0, MAX_DELAY);
      previous_timestamp = timestamp;

      /*  The canvas starts out transparent, so even a file without alpha
       *  can have transparent frames; only those get an alpha channel.
       */
      alpha = !pixels_opaque (canvas, npixels);

      layer_name = g_strdup_printf ("Frame %d (%dms)", frame, delay);
      layer_ID = gimp_layer_new (image_ID, layer_name, width, height,
				 alpha ? RGBA_IMAGE : RGB_IMAGE,
				 100, NORMAL_MODE);
      g_free (layer_name);

      /*  Each frame goes on top: the first ends up at the bottom.  */
      gimp_image_add_layer (image_ID, layer_ID, 0);
      layer_set_pixels (layer_ID, canvas, 4, alpha ? 4 : 3);

      gimp_progress_update ((gdouble) frame / (gdouble) info.frame_count);
    }

  WebPAnimDecoderDelete (decoder);

  /*  The frames before a broken one are kept; without any, image_ID is
   *  still -1.
   */
  return image_ID;
}


/*
 *  Saving
 */

/*  Reads a drawable as RGB, or as RGBA when it has alpha or force_alpha
 *  is set; gray and indexed pixels are expanded.  Returns NULL when the
 *  drawable can't be read.
 */

static guchar *
drawable_get_rgb (gint32        drawable_ID,
		  const guchar *cmap,
		  gint          ncolors,
		  gboolean      force_alpha,
		  gint         *width_out,
		  gint         *height_out,
		  gint         *bpp_out)
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
      g_message ("WebP: can't save this image type");
      gimp_drawable_detach (drawable);
      return NULL;
    }

  bpp = (alpha || force_alpha) ? 4 : 3;

  if (width < 1 || height < 1 || src_bpp < 1 || src_bpp > 4 ||
      !g_size_checked_mul (&npixels, width, height) ||
      !g_size_checked_mul (&src_size, npixels, src_bpp) ||
      !g_size_checked_mul (&dst_size, npixels, bpp))
    {
      g_message ("WebP: the drawable is too large");
      gimp_drawable_detach (drawable);
      return NULL;
    }

  src = g_try_malloc (src_size);
  if (!src)
    {
      g_message ("WebP: not enough memory to save the image");
      gimp_drawable_detach (drawable);
      return NULL;
    }

  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height,
		       FALSE, FALSE);
  gimp_pixel_rgn_get_rect (&pixel_rgn, src, 0, 0, width, height);
  gimp_drawable_detach (drawable);

  if ((type == RGB_IMAGE || type == RGBA_IMAGE) && src_bpp == bpp)
    dst = src;
  else
    {
      dst = g_try_malloc (dst_size);
      if (!dst)
	{
	  g_message ("WebP: not enough memory to save the image");
	  g_free (src);
	  return NULL;
	}

      for (i = 0; i < npixels; i++)
	{
	  const guchar *s = src + i * src_bpp;
	  guchar       *d = dst + i * bpp;
	  guchar        a = alpha ? s[src_bpp - 1] : 255;

	  switch (type)
	    {
	    case RGB_IMAGE:
	    case RGBA_IMAGE:
	      d[0] = s[0];
	      d[1] = s[1];
	      d[2] = s[2];
	      break;

	    case GRAY_IMAGE:
	    case GRAYA_IMAGE:
	      d[0] = d[1] = d[2] = s[0];
	      break;

	    default:
	      /*  An index past the colormap reads as black.  */
	      if (cmap && s[0] < ncolors)
		{
		  d[0] = cmap[s[0] * 3];
		  d[1] = cmap[s[0] * 3 + 1];
		  d[2] = cmap[s[0] * 3 + 2];
		}
	      else
		d[0] = d[1] = d[2] = 0;
	      break;
	    }

	  if (bpp == 4)
	    d[3] = a;
	}

      g_free (src);
    }

  *width_out  = width;
  *height_out = height;
  *bpp_out    = bpp;

  return dst;
}


/*  The frame delay in a layer name: the number in the first "(NNNms)",
 *  the way the GIF plug-in writes it, or -1 without one.
 */

static gint
parse_ms_tag (const gchar *name)
{
  const gchar *p;

  for (p = strchr (name, '('); p; p = strchr (p + 1, '('))
    {
      const gchar *q = p + 1;
      gint         value = 0;

      if (!g_ascii_isdigit (*q))
	continue;

      while (g_ascii_isdigit (*q))
	{
	  value = value * 10 + (*q - '0');
	  if (value > MAX_DELAY)
	    value = MAX_DELAY;
	  q++;
	}

      while (*q == ' ')
	q++;

      if (g_ascii_tolower (q[0]) == 'm' && g_ascii_tolower (q[1]) == 's')
	return value;
    }

  return -1;
}


/*  Lays an RGBA layer at (x, y) over an RGBA canvas, its alpha scaled by
 *  opacity (0..255).  Where the canvas is transparent, the layer's pixels
 *  are copied as they are, so a layer on a clear canvas comes out
 *  unchanged.
 */

static void
composite_layer (guchar       *canvas,
		 gint          canvas_width,
		 gint          canvas_height,
		 const guchar *layer,
		 gint          layer_width,
		 gint          layer_height,
		 gint          x,
		 gint          y,
		 gint          opacity)
{
  gint64 x0, y0, x1, y1;
  gint64 row, col;

  x0 = MAX (0, (gint64) x);
  y0 = MAX (0, (gint64) y);
  x1 = MIN ((gint64) canvas_width,  (gint64) x + layer_width);
  y1 = MIN ((gint64) canvas_height, (gint64) y + layer_height);

  for (row = y0; row < y1; row++)
    {
      guchar       *d = canvas + ((gsize) row * canvas_width + x0) * 4;
      const guchar *s = layer + ((gsize) (row - y) * layer_width +
				 (gsize) (x0 - x)) * 4;

      for (col = x0; col < x1; col++, d += 4, s += 4)
	{
	  gint sa = (s[3] * opacity + 127) / 255;
	  gint da = d[3];

	  if (sa == 255 || da == 0)
	    {
	      d[0] = s[0];
	      d[1] = s[1];
	      d[2] = s[2];
	      d[3] = (guchar) sa;
	    }
	  else if (sa > 0)
	    {
	      /*  alpha of the result, times 255  */
	      gint a = sa * 255 + da * (255 - sa);
	      gint c;

	      for (c = 0; c < 3; c++)
		d[c] = (guchar) ((s[c] * sa * 255 + d[c] * da * (255 - sa) + a / 2) / a);
	      d[3] = (guchar) ((a + 127) / 255);
	    }
	}
    }
}


static gboolean
webp_config_init (WebPConfig *config)
{
  if (!WebPConfigInit (config))
    return FALSE;

  if (webpvals.lossless)
    {
      /*  Lossless keeps the colour of transparent pixels as well.  */
      config->lossless = 1;
      config->exact    = 1;
    }
  else
    {
      config->lossless = 0;
      config->quality  = (float) CLAMP (webpvals.quality, 0.0, 100.0);
    }

  return WebPValidateConfig (config);
}


static gboolean
save_still (char         *filename,
	    gint32        drawable_ID,
	    const guchar *cmap,
	    gint          ncolors,
	    guint8      **output,
	    gsize        *output_size)
{
  WebPConfig       config;
  WebPPicture      picture;
  WebPMemoryWriter writer;
  guchar          *pixels;
  gint             width, height, bpp;
  gboolean         ok;

  pixels = drawable_get_rgb (drawable_ID, cmap, ncolors, FALSE,
			     &width, &height, &bpp);
  if (!pixels)
    return FALSE;

  if (width > WEBP_MAX_DIMENSION || height > WEBP_MAX_DIMENSION)
    {
      g_message ("WebP: images can be at most %d pixels wide and high",
		 WEBP_MAX_DIMENSION);
      g_free (pixels);
      return FALSE;
    }

  if (!webp_config_init (&config) || !WebPPictureInit (&picture))
    {
      g_message ("WebP: can't set up the encoder");
      g_free (pixels);
      return FALSE;
    }

  picture.use_argb = 1;
  picture.width    = width;
  picture.height   = height;

  if (bpp == 4)
    ok = WebPPictureImportRGBA (&picture, pixels, width * bpp);
  else
    ok = WebPPictureImportRGB (&picture, pixels, width * bpp);

  g_free (pixels);

  if (!ok)
    {
      g_message ("WebP: not enough memory to save the image");
      WebPPictureFree (&picture);
      return FALSE;
    }

  gimp_progress_update (0.5);

  WebPMemoryWriterInit (&writer);
  picture.writer     = WebPMemoryWrite;
  picture.custom_ptr = &writer;

  ok = WebPEncode (&config, &picture);
  if (!ok)
    {
      g_message ("WebP: error %d while encoding %s",
		 (int) picture.error_code, filename);
      WebPPictureFree (&picture);
      WebPMemoryWriterClear (&writer);
      return FALSE;
    }

  WebPPictureFree (&picture);

  /*  Hand over libwebp's buffer as a copy, so that the caller frees
   *  every output the same way.
   */
  *output      = g_memdup2 (writer.mem, writer.size);
  *output_size = writer.size;
  WebPMemoryWriterClear (&writer);

  return TRUE;
}


static gboolean
save_animation (char         *filename,
		gint32        image_ID,
		const gint32 *layers,
		gint          nlayers,
		const guchar *cmap,
		gint          ncolors,
		guint8      **output,
		gsize        *output_size)
{
  WebPAnimEncoderOptions options;
  WebPAnimEncoder *encoder;
  WebPConfig       config;
  WebPData         webp_data;
  guchar          *canvas;
  gsize            canvas_size;
  gint             width, height;
  gint64           timestamp = 0;
  gint             i;

  width  = gimp_image_width (image_ID);
  height = gimp_image_height (image_ID);

  if (width < 1 || height < 1 ||
      width > WEBP_MAX_DIMENSION || height > WEBP_MAX_DIMENSION)
    {
      g_message ("WebP: images can be at most %d pixels wide and high",
		 WEBP_MAX_DIMENSION);
      return FALSE;
    }

  if (!webp_config_init (&config) || !WebPAnimEncoderOptionsInit (&options))
    {
      g_message ("WebP: can't set up the encoder");
      return FALSE;
    }

  options.anim_params.loop_count = 0;	/* forever */

  encoder = WebPAnimEncoderNew (width, height, &options);
  if (!encoder)
    {
      g_message ("WebP: can't set up the encoder");
      return FALSE;
    }

  canvas_size = (gsize) width * height * 4;
  canvas = g_try_malloc0 (canvas_size);
  if (!canvas)
    {
      g_message ("WebP: not enough memory to save the image");
      WebPAnimEncoderDelete (encoder);
      return FALSE;
    }

  /*  gimp_image_get_layers () lists the top layer first; the bottom
   *  layer is the first frame.
   */
  for (i = nlayers - 1; i >= 0; i--)
    {
      WebPPicture  picture;
      guchar      *pixels;
      gchar       *layer_name;
      gint         layer_width, layer_height, bpp;
      gint         offset_x, offset_y;
      gint         opacity;
      gint         delay;
      gboolean     combine;
      gboolean     ok;

      layer_name = gimp_layer_get_name (layers[i]);
      delay   = layer_name ? parse_ms_tag (layer_name) : -1;
      combine = layer_name && strstr (layer_name, "(combine)") != NULL;
      g_free (layer_name);

      if (delay < 0)
	delay = DEFAULT_DELAY;

      pixels = drawable_get_rgb (layers[i], cmap, ncolors, TRUE,
				 &layer_width, &layer_height, &bpp);
      if (!pixels)
	{
	  g_free (canvas);
	  WebPAnimEncoderDelete (encoder);
	  return FALSE;
	}

      gimp_drawable_offsets (layers[i], &offset_x, &offset_y);
      opacity = (gint) (gimp_layer_get_opacity (layers[i]) * 255.0 / 100.0
			+ 0.5);
      opacity = CLAMP (opacity, 0, 255);

      if (!combine)
	memset (canvas, 0, canvas_size);

      composite_layer (canvas, width, height,
		       pixels, layer_width, layer_height,
		       offset_x, offset_y, opacity);
      g_free (pixels);

      if (!WebPPictureInit (&picture))
	ok = FALSE;
      else
	{
	  picture.use_argb = 1;
	  picture.width    = width;
	  picture.height   = height;

	  ok = (WebPPictureImportRGBA (&picture, canvas, width * 4) &&
		WebPAnimEncoderAdd (encoder, &picture, (int) timestamp,
				    &config));
	  WebPPictureFree (&picture);
	}

      if (!ok)
	{
	  const char *message = WebPAnimEncoderGetError (encoder);

	  g_message ("WebP: error while encoding %s: %s", filename,
		     (message && *message) ? message : "out of memory");
	  g_free (canvas);
	  WebPAnimEncoderDelete (encoder);
	  return FALSE;
	}

      timestamp += delay;
      if (timestamp > G_MAXINT)
	{
	  g_message ("WebP: the animation in %s is too long", filename);
	  g_free (canvas);
	  WebPAnimEncoderDelete (encoder);
	  return FALSE;
	}

      gimp_progress_update ((gdouble) (nlayers - i) / (gdouble) nlayers);
    }

  g_free (canvas);

  WebPDataInit (&webp_data);

  if (!WebPAnimEncoderAdd (encoder, NULL, (int) timestamp, NULL) ||
      !WebPAnimEncoderAssemble (encoder, &webp_data))
    {
      const char *message = WebPAnimEncoderGetError (encoder);

      g_message ("WebP: error while encoding %s: %s", filename,
		 (message && *message) ? message : "out of memory");
      WebPDataClear (&webp_data);
      WebPAnimEncoderDelete (encoder);
      return FALSE;
    }

  WebPAnimEncoderDelete (encoder);

  *output      = g_memdup2 (webp_data.bytes, webp_data.size);
  *output_size = webp_data.size;
  WebPDataClear (&webp_data);

  return TRUE;
}


static gint
save_image (char   *filename,
	    gint32  image_ID,
	    gint32  drawable_ID)
{
  guint8   *output = NULL;
  gsize     output_size = 0;
  guchar   *cmap = NULL;
  gint      ncolors = 0;
  gint32   *layers;
  gint      nlayers = 0;
  gchar    *basename;
  gchar    *progress;
  gboolean  ok;
  FILE     *fp;

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

  layers = gimp_image_get_layers (image_ID, &nlayers);

  if (webpvals.animation && layers && nlayers > 1)
    ok = save_animation (filename, image_ID, layers, nlayers,
			 cmap, ncolors, &output, &output_size);
  else
    ok = save_still (filename, drawable_ID, cmap, ncolors,
		     &output, &output_size);

  g_free (layers);
  g_free (cmap);

  if (!ok)
    return FALSE;

  fp = g_fopen (filename, "wb");
  if (!fp)
    {
      g_message ("WebP: can't create %s", filename);
      g_free (output);
      return FALSE;
    }

  ok = fwrite (output, 1, output_size, fp) == output_size;
  if (fclose (fp) != 0)
    ok = FALSE;
  g_free (output);

  if (!ok)
    {
      g_message ("WebP: error while writing %s", filename);
      return FALSE;
    }

  return TRUE;
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
  webpvals.quality = gtk_adjustment_get_value (adjustment);
}


static void
save_lossless_update (GtkWidget *widget,
		      gpointer   data)
{
  webpvals.lossless = gtk_check_button_get_active (GTK_CHECK_BUTTON (widget));

  gtk_widget_set_sensitive (quality_label, !webpvals.lossless);
  gtk_widget_set_sensitive (quality_scale, !webpvals.lossless);
}


static void
save_animation_update (GtkWidget *widget,
		       gpointer   data)
{
  webpvals.animation = gtk_check_button_get_active (GTK_CHECK_BUTTON (widget));
}


static gint
save_dialog (gint32 image_ID)
{
  GtkWidget     *dlg;
  GtkWidget     *button;
  GtkWidget     *frame;
  GtkWidget     *table;
  GtkWidget     *toggle;
  GtkAdjustment *adjustment;
  gint32        *layers;
  gint           nlayers = 0;

  layers = gimp_image_get_layers (image_ID, &nlayers);
  g_free (layers);

  gtk_init ();

  dlg = gimp_dialog_new ("Save as WebP");
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

  adjustment = gtk_adjustment_new (webpvals.quality, 0.0, 100.0,
				   1.0, 10.0, 0.0);
  quality_scale = gimp_hscale_new (adjustment, 0);
  gtk_widget_set_size_request (quality_scale, SCALE_WIDTH, -1);
  gimp_table_attach (table, quality_scale, 1, 2, 0, 1,
		     GIMP_EXPAND | GIMP_FILL, 0, 0, 0);
  g_signal_connect (adjustment, "value-changed",
		    G_CALLBACK (save_quality_update), NULL);

  toggle = gtk_check_button_new_with_label ("Lossless");
  gimp_table_attach (table, toggle, 0, 2, 1, 2, GIMP_FILL, 0, 0, 0);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), webpvals.lossless);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (save_lossless_update), NULL);
  gtk_widget_set_sensitive (quality_label, !webpvals.lossless);
  gtk_widget_set_sensitive (quality_scale, !webpvals.lossless);

  toggle = gtk_check_button_new_with_label ("Save layers as animation");
  gimp_table_attach (table, toggle, 0, 2, 2, 3, GIMP_FILL, 0, 0, 0);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), webpvals.animation);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (save_animation_update), NULL);
  gtk_widget_set_sensitive (toggle, nlayers > 1);

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return runme;
}
