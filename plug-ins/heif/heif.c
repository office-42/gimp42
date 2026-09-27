/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * HEIF and AVIF file plug-in for gimp42, through libheif
 * Copyright (C) 2026 gimp42 contributors
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
 * Loading reads the primary image of a HEIF file (HEIC, AVIF, or anything
 * else libheif has a decoder for) as an 8-bit RGB or RGBA layer; images
 * with more bits per channel are reduced to 8.
 *
 * Saving is two procedures, file_heif_save (HEVC) and file_avif_save
 * (AV1), each registered only when libheif has an encoder for its
 * compression.  Both take a quality (0-100) and a lossless flag.
 * Grayscale and indexed drawables are saved as RGB, with their alpha
 * channel if they have one.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libheif/heif.h>

#include <libgimp/gimp.h>
#include "libgimp/gimpui.h"


#define HEIF_MAX_DIMENSION	262144	/* per side, as the other loaders */
#define SCALE_WIDTH		125

typedef struct
{
  gint quality;		/* 0 - 100 */
  gint lossless;
} HeifSaveVals;

typedef struct
{
  const gchar                  *proc;
  const gchar                  *name;		/* for messages */
  const gchar                  *extensions;
  const gchar                  *menu_path;
  const gchar                  *blurb;
  const gchar                  *help;
  enum heif_compression_format  compression;
} HeifSaveFormat;

static const HeifSaveFormat save_formats[] =
{
  {
    "file_heif_save", "HEIF", "heif,heic", "<Save>/HEIF",
    "Saves files in the HEIF file format (HEVC)",
    "Saves the drawable as a HEIF image compressed with HEVC (H.265), "
    "the format of .heic photos.  Grayscale and indexed drawables are "
    "saved as RGB; an alpha channel is kept.  quality is 0 (smallest "
    "file) to 100 (best); lossless, when not 0, ignores quality and "
    "keeps every pixel exactly.",
    heif_compression_HEVC
  },
  {
    "file_avif_save", "AVIF", "avif", "<Save>/AVIF",
    "Saves files in the AVIF file format (AV1)",
    "Saves the drawable as an AVIF image, HEIF compressed with AV1.  "
    "Grayscale and indexed drawables are saved as RGB; an alpha channel "
    "is kept.  quality is 0 (smallest file) to 100 (best); lossless, "
    "when not 0, ignores quality and keeps every pixel exactly.",
    heif_compression_AV1
  },
};

#define N_SAVE_FORMATS (sizeof (save_formats) / sizeof (save_formats[0]))

static const HeifSaveVals default_save_vals =
{
  50,     /* quality */
  FALSE   /* lossless */
};


static void   query      (void);
static void   run        (char    *name,
			  int      nparams,
			  GParam  *param,
			  int     *nreturn_vals,
			  GParam **return_vals);
static gint32 load_image (const gchar          *filename);
static gint   save_image (const gchar          *filename,
			  gint32                image_ID,
			  gint32                drawable_ID,
			  const HeifSaveFormat *format,
			  const HeifSaveVals   *vals);
static gint   save_dialog (const HeifSaveFormat *format,
			   HeifSaveVals         *vals);


GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

static gint       runme = FALSE;
static GtkWidget *quality_label = NULL;
static GtkWidget *quality_scale = NULL;


MAIN ()

/*  libheif 1.13 and later load their codec plug-ins in heif_init ().  */
static void
heif_plugin_init (void)
{
#if LIBHEIF_HAVE_VERSION(1, 13, 0)
  heif_init (NULL);
#endif
}

static void
heif_plugin_deinit (void)
{
#if LIBHEIF_HAVE_VERSION(1, 13, 0)
  heif_deinit ();
#endif
}

static void
query (void)
{
  static GParamDef load_args[] =
  {
    { PARAM_INT32,  "run_mode",     "Interactive, non-interactive" },
    { PARAM_STRING, "filename",     "The name of the file to load" },
    { PARAM_STRING, "raw_filename", "The name entered" },
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
    { PARAM_STRING,   "raw_filename", "The name entered" },
    { PARAM_INT32,    "quality",      "Quality, 0 (smallest) - 100 (best)" },
    { PARAM_INT32,    "lossless",     "Lossless compression (TRUE or FALSE); quality is then ignored" },
  };
  static int nsave_args = sizeof (save_args) / sizeof (save_args[0]);
  guint i;

  heif_plugin_init ();

  gimp_install_procedure ("file_heif_load",
			  "Loads files in the HEIF and AVIF file formats",
			  "Loads the primary image of a HEIF file (.heic, "
			  ".heif, .avif) as an RGB or RGBA layer.  Images "
			  "with more than 8 bits per channel are reduced to "
			  "8.  Which compressions can be read depends on the "
			  "decoders libheif has.",
			  "gimp42 contributors",
			  "gimp42 contributors",
			  "2026",
			  "<Load>/HEIF and AVIF",
			  NULL,
			  PROC_PLUG_IN,
			  nload_args, nload_return_vals,
			  load_args, load_return_vals);

  gimp_register_magic_load_handler ("file_heif_load",
				    "heif,heic,hif,avif",
				    "",
				    "4,string,ftypheic,"
				    "4,string,ftypheix,"
				    "4,string,ftypheim,"
				    "4,string,ftypheis,"
				    "4,string,ftyphevc,"
				    "4,string,ftyphevx,"
				    "4,string,ftypmif1,"
				    "4,string,ftypmsf1,"
				    "4,string,ftypavif,"
				    "4,string,ftypavis");

  /*  A save procedure without an encoder behind it would only fail.  */
  for (i = 0; i < N_SAVE_FORMATS; i++)
    {
      const HeifSaveFormat *format = &save_formats[i];

      if (!heif_have_encoder_for_format (format->compression))
	continue;

      gimp_install_procedure ((gchar *) format->proc,
			      (gchar *) format->blurb,
			      (gchar *) format->help,
			      "gimp42 contributors",
			      "gimp42 contributors",
			      "2026",
			      (gchar *) format->menu_path,
			      "RGB*,GRAY*,INDEXED*",
			      PROC_PLUG_IN,
			      nsave_args, 0,
			      save_args, NULL);

      gimp_register_save_handler ((gchar *) format->proc,
				  (gchar *) format->extensions, "");
    }

  heif_plugin_deinit ();
}

static void
run (char    *name,
     int      nparams,
     GParam  *param,
     int     *nreturn_vals,
     GParam **return_vals)
{
  static GParam         values[2];
  const HeifSaveFormat *format = NULL;
  HeifSaveVals          vals;
  gint32                image_ID;
  guint                 i;

  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type          = PARAM_STATUS;
  values[0].data.d_status = STATUS_CALLING_ERROR;

  for (i = 0; i < N_SAVE_FORMATS; i++)
    if (strcmp (name, save_formats[i].proc) == 0)
      format = &save_formats[i];

  heif_plugin_init ();

  if (strcmp (name, "file_heif_load") == 0)
    {
      if (nparams >= 3)
	{
	  image_ID = load_image (param[1].data.d_string);

	  if (image_ID != -1)
	    {
	      *nreturn_vals = 2;
	      values[0].data.d_status = STATUS_SUCCESS;
	      values[1].type          = PARAM_IMAGE;
	      values[1].data.d_image  = image_ID;
	    }
	  else
	    values[0].data.d_status = STATUS_EXECUTION_ERROR;
	}
    }
  else if (format && nparams >= 5)
    {
      vals = default_save_vals;
      values[0].data.d_status = STATUS_SUCCESS;

      switch (param[0].data.d_int32)
	{
	case RUN_INTERACTIVE:
	  gimp_get_data ((gchar *) format->proc, &vals);
	  vals.quality = CLAMP (vals.quality, 0, 100);

	  /*  Cancelled: nothing was saved, which the application must
	   *  not take for success (it would mark the image clean).
	   */
	  if (!save_dialog (format, &vals))
	    values[0].data.d_status = STATUS_EXECUTION_ERROR;
	  break;

	case RUN_NONINTERACTIVE:
	  if (nparams != 7 ||
	      param[5].data.d_int32 < 0 || param[5].data.d_int32 > 100)
	    {
	      values[0].data.d_status = STATUS_CALLING_ERROR;
	    }
	  else
	    {
	      vals.quality  = param[5].data.d_int32;
	      vals.lossless = (param[6].data.d_int32 != 0);
	    }
	  break;

	case RUN_WITH_LAST_VALS:
	  gimp_get_data ((gchar *) format->proc, &vals);
	  vals.quality = CLAMP (vals.quality, 0, 100);
	  break;

	default:
	  values[0].data.d_status = STATUS_CALLING_ERROR;
	  break;
	}

      if (values[0].data.d_status == STATUS_SUCCESS)
	{
	  if (save_image (param[3].data.d_string,
			  param[1].data.d_int32, param[2].data.d_int32,
			  format, &vals))
	    gimp_set_data ((gchar *) format->proc, &vals, sizeof (vals));
	  else
	    values[0].data.d_status = STATUS_EXECUTION_ERROR;
	}
    }

  heif_plugin_deinit ();
}


/*
 *  Loading
 */

static void
heif_report_decode_error (const gchar       *filename,
			  struct heif_error  err)
{
  const gchar *detail = err.message ? err.message : "unknown error";

  if (err.code == heif_error_Unsupported_feature &&
      err.subcode == heif_suberror_Unsupported_codec)
    g_message ("HEIF: can't decode \"%s\": libheif has no decoder for "
	       "its compression (%s)", filename, detail);
  else
    g_message ("HEIF: can't decode \"%s\": %s", filename, detail);
}

static gint32
load_image (const gchar *filename)
{
  GMappedFile                  *mapped;
  GError                       *error = NULL;
  const gchar                  *data;
  gsize                         size;
  struct heif_context          *ctx = NULL;
  struct heif_image_handle     *handle = NULL;
  struct heif_image            *img = NULL;
  struct heif_decoding_options *options;
  struct heif_error             err;
  enum heif_chroma              chroma;
  const guint8                 *plane;
  gint                          stride = 0;
  gint                          width, height;
  gboolean                      has_alpha;
  gboolean                      deep = FALSE;	/* 16 bits per sample */
  gint                          bits = 8;
  guint                         maxval = 255;
  gint                          channels, sample_bytes;
  gint32                        image = -1, layer;
  GDrawable                    *drawable;
  GPixelRgn                     pixel_rgn;
  guchar                       *buf = NULL;
  gint                          tile_height;
  gint                          x, y, row, rows;
  gchar                        *basename, *progress;

  mapped = g_mapped_file_new (filename, FALSE, &error);
  if (!mapped)
    {
      g_message ("HEIF: can't open \"%s\": %s", filename, error->message);
      g_error_free (error);
      return -1;
    }

  data = g_mapped_file_get_contents (mapped);
  size = g_mapped_file_get_length (mapped);
  if (!data || size < 16)
    {
      g_message ("HEIF: \"%s\" is not a HEIF file", filename);
      goto out;
    }

  basename = g_path_get_basename (filename);
  progress = g_strdup_printf ("Loading %s:", basename);
  gimp_progress_init (progress);
  g_free (progress);
  g_free (basename);

  ctx = heif_context_alloc ();
  if (!ctx)
    {
      g_message ("HEIF: out of memory loading \"%s\"", filename);
      goto out;
    }

  err = heif_context_read_from_memory_without_copy (ctx, data, size, NULL);
  if (err.code == heif_error_Ok)
    err = heif_context_get_primary_image_handle (ctx, &handle);
  if (err.code != heif_error_Ok)
    {
      g_message ("HEIF: can't read \"%s\": %s", filename,
		 err.message ? err.message : "unknown error");
      goto out;
    }

  width  = heif_image_handle_get_width (handle);
  height = heif_image_handle_get_height (handle);
  if (width < 1 || height < 1 ||
      width > HEIF_MAX_DIMENSION || height > HEIF_MAX_DIMENSION)
    {
      g_message ("HEIF: \"%s\" has unsupported image dimensions (%d x %d)",
		 filename, width, height);
      goto out;
    }

  has_alpha = heif_image_handle_has_alpha_channel (handle) ? TRUE : FALSE;
  channels  = has_alpha ? 4 : 3;
  chroma    = has_alpha ? heif_chroma_interleaved_RGBA
			: heif_chroma_interleaved_RGB;

  /*  8 bits per channel, which libheif converts deeper images to.  */
  options = heif_decoding_options_alloc ();
  if (options && options->version >= 2)
    options->convert_hdr_to_8bit = 1;

  err = heif_decode_image (handle, &img, heif_colorspace_RGB, chroma,
			   options);

  /*  Should that fail for a deeper image, take 16 bits and reduce them
   *  here.
   */
  if (err.code != heif_error_Ok &&
      heif_image_handle_get_luma_bits_per_pixel (handle) > 8)
    {
      struct heif_error err16;

      if (options && options->version >= 2)
	options->convert_hdr_to_8bit = 0;

      img    = NULL;
      chroma = has_alpha ? heif_chroma_interleaved_RRGGBBAA_LE
			 : heif_chroma_interleaved_RRGGBB_LE;
      err16  = heif_decode_image (handle, &img, heif_colorspace_RGB, chroma,
				  options);
      if (err16.code == heif_error_Ok)
	{
	  err  = err16;
	  deep = TRUE;
	}
      else
	img = NULL;
    }

  heif_decoding_options_free (options);

  if (err.code != heif_error_Ok || !img)
    {
      img = NULL;
      heif_report_decode_error (filename, err);
      goto out;
    }

  gimp_progress_update (0.5);

  /*  Trust the decoded image, not the file, for the buffer's layout.  */
  sample_bytes = deep ? 2 : 1;
  width  = heif_image_get_width (img, heif_channel_interleaved);
  height = heif_image_get_height (img, heif_channel_interleaved);
  plane  = heif_image_get_plane_readonly (img, heif_channel_interleaved,
					  &stride);

  if (!plane ||
      heif_image_get_colorspace (img) != heif_colorspace_RGB ||
      heif_image_get_chroma_format (img) != chroma ||
      width < 1 || height < 1 ||
      width > HEIF_MAX_DIMENSION || height > HEIF_MAX_DIMENSION ||
      stride < width * channels * sample_bytes)
    {
      g_message ("HEIF: libheif returned an unexpected image for \"%s\"",
		 filename);
      goto out;
    }

  if (deep)
    {
      bits = heif_image_get_bits_per_pixel_range (img,
						  heif_channel_interleaved);
      if (bits < 1 || bits > 16)
	bits = 16;
      maxval = (1u << bits) - 1;
    }

  tile_height = gimp_tile_height ();
  if (tile_height < 1)
    tile_height = 1;

  buf = g_try_malloc ((gsize) tile_height * width * channels);
  if (!buf)
    {
      g_message ("HEIF: out of memory loading \"%s\"", filename);
      goto out;
    }

  image = gimp_image_new (width, height, RGB);
  if (image == -1)
    {
      g_message ("HEIF: can't create a new image");
      goto out;
    }

  gimp_image_set_filename (image, (gchar *) filename);

  layer = gimp_layer_new (image, "Background", width, height,
			  has_alpha ? RGBA_IMAGE : RGB_IMAGE,
			  100, NORMAL_MODE);
  if (layer == -1)
    {
      g_message ("HEIF: can't create a new layer");
      gimp_image_delete (image);
      image = -1;
      goto out;
    }
  gimp_image_add_layer (image, layer, 0);

  drawable = gimp_drawable_get (layer);
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0,
		       drawable->width, drawable->height, TRUE, FALSE);

  for (y = 0; y < height; y += rows)
    {
      rows = MIN (tile_height, height - y);

      for (row = 0; row < rows; row++)
	{
	  const guint8 *src  = plane + (gsize) (y + row) * stride;
	  guchar       *dest = buf + (gsize) row * width * channels;

	  if (!deep)
	    {
	      memcpy (dest, src, (gsize) width * channels);
	    }
	  else
	    {
	      /*  little-endian samples of 'bits' bits  */
	      for (x = 0; x < width * channels; x++)
		{
		  guint v = src[2 * x] | (guint) src[2 * x + 1] << 8;

		  if (v > maxval)
		    v = maxval;
		  dest[x] = (v * 255 + maxval / 2) / maxval;
		}
	    }
	}

      gimp_pixel_rgn_set_rect (&pixel_rgn, buf, 0, y, width, rows);
      gimp_progress_update (0.5 + 0.5 * (gdouble) (y + rows) / height);
    }

  gimp_drawable_flush (drawable);
  gimp_drawable_detach (drawable);

 out:
  g_free (buf);
  if (img)
    heif_image_release (img);
  if (handle)
    heif_image_handle_release (handle);
  if (ctx)
    heif_context_free (ctx);
  g_mapped_file_unref (mapped);	/* after the context, which reads it */

  return image;
}


/*
 *  Saving
 */

static struct heif_error
heif_write_callback (struct heif_context *ctx,
		     const void          *data,
		     size_t               size,
		     void                *userdata)
{
  FILE              *fp = userdata;
  struct heif_error  err;

  err.code    = heif_error_Ok;
  err.subcode = heif_suberror_Unspecified;
  err.message = "Success";

  if (size > 0 && fwrite (data, 1, size, fp) != size)
    {
      err.code    = heif_error_Encoding_error;
      err.subcode = heif_suberror_Cannot_write_output_data;
      err.message = "Can't write the file";
    }

  return err;
}

/*  Copies the drawable into img's interleaved RGB(A) plane.  */
static gboolean
heif_fill_image (GDrawable     *drawable,
		 GDrawableType  type,
		 const guchar  *palette,
		 guint8        *plane,
		 gint           stride,
		 gint           channels)
{
  GPixelRgn  pixel_rgn;
  guchar    *buf;
  gint       width  = drawable->width;
  gint       height = drawable->height;
  gint       bpp    = drawable->bpp;
  gint       tile_height;
  gint       x, y, row, rows;

  tile_height = gimp_tile_height ();
  if (tile_height < 1)
    tile_height = 1;

  buf = g_try_malloc ((gsize) tile_height * width * bpp);
  if (!buf)
    return FALSE;

  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height,
		       FALSE, FALSE);

  for (y = 0; y < height; y += rows)
    {
      rows = MIN (tile_height, height - y);
      gimp_pixel_rgn_get_rect (&pixel_rgn, buf, 0, y, width, rows);

      for (row = 0; row < rows; row++)
	{
	  const guchar *src  = buf + (gsize) row * width * bpp;
	  guint8       *dest = plane + (gsize) (y + row) * stride;

	  switch (type)
	    {
	    case RGB_IMAGE:
	    case RGBA_IMAGE:
	      memcpy (dest, src, (gsize) width * bpp);
	      break;

	    case GRAY_IMAGE:
	    case GRAYA_IMAGE:
	      for (x = 0; x < width; x++, src += bpp, dest += channels)
		{
		  dest[0] = dest[1] = dest[2] = src[0];
		  if (channels == 4)
		    dest[3] = src[1];
		}
	      break;

	    case INDEXED_IMAGE:
	    case INDEXEDA_IMAGE:
	      for (x = 0; x < width; x++, src += bpp, dest += channels)
		{
		  memcpy (dest, palette + src[0] * 3, 3);
		  if (channels == 4)
		    dest[3] = src[1];
		}
	      break;
	    }
	}

      gimp_progress_update (0.2 * (gdouble) (y + rows) / height);
    }

  g_free (buf);

  return TRUE;
}

static gint
save_image (const gchar          *filename,
	    gint32                image_ID,
	    gint32                drawable_ID,
	    const HeifSaveFormat *format,
	    const HeifSaveVals   *vals)
{
  GDrawable                     *drawable;
  GDrawableType                  type;
  struct heif_context           *ctx = NULL;
  struct heif_encoder           *encoder = NULL;
  struct heif_image             *img = NULL;
  struct heif_color_profile_nclx *nclx = NULL;
  struct heif_encoding_options  *options = NULL;
  struct heif_writer             writer;
  struct heif_error              err;
  guint8                        *plane;
  gint                           stride = 0;
  guchar                         palette[256 * 3];
  guchar                        *cmap;
  gint                           ncolors = 0;
  gint                           width, height, bpp, channels;
  gboolean                       indexed = FALSE;
  gboolean                       success = FALSE;
  FILE                          *fp;
  gchar                         *basename, *progress;

  drawable = gimp_drawable_get (drawable_ID);
  type     = gimp_drawable_type (drawable_ID);
  width    = drawable->width;
  height   = drawable->height;
  bpp      = drawable->bpp;

  switch (type)
    {
    case RGB_IMAGE:
    case GRAY_IMAGE:
      channels = 3;
      break;
    case RGBA_IMAGE:
    case GRAYA_IMAGE:
      channels = 4;
      break;
    case INDEXED_IMAGE:
      channels = 3;
      indexed  = TRUE;
      break;
    case INDEXEDA_IMAGE:
      channels = 4;
      indexed  = TRUE;
      break;
    default:
      g_message ("%s: can't save this image type", format->name);
      goto out;
    }

  if (width < 1 || height < 1 ||
      bpp != ((type == RGB_IMAGE)  ? 3 :
	      (type == RGBA_IMAGE) ? 4 :
	      (type == GRAYA_IMAGE || type == INDEXEDA_IMAGE) ? 2 : 1))
    {
      g_message ("%s: can't save this drawable", format->name);
      goto out;
    }

  if (!heif_have_encoder_for_format (format->compression))
    {
      g_message ("%s: can't save \"%s\": libheif has no %s encoder",
		 format->name, filename,
		 format->compression == heif_compression_AV1 ? "AV1" : "HEVC");
      goto out;
    }

  ctx = heif_context_alloc ();
  if (!ctx)
    {
      g_message ("%s: out of memory saving \"%s\"", format->name, filename);
      goto out;
    }

  err = heif_context_get_encoder_for_format (ctx, format->compression,
					     &encoder);
  if (err.code != heif_error_Ok)
    {
      g_message ("%s: can't save \"%s\": no encoder (%s)",
		 format->name, filename,
		 err.message ? err.message : "unknown error");
      encoder = NULL;
      goto out;
    }

  basename = g_path_get_basename (filename);
  progress = g_strdup_printf ("Saving %s:", basename);
  gimp_progress_init (progress);
  g_free (progress);
  g_free (basename);

  /*  Every index maps to a colour: entries past the colormap are black.  */
  memset (palette, 0, sizeof (palette));
  if (indexed)
    {
      cmap = gimp_image_get_cmap (image_ID, &ncolors);
      if (cmap)
	memcpy (palette, cmap, CLAMP (ncolors, 0, 256) * 3);
      g_free (cmap);
    }

  err = heif_image_create (width, height, heif_colorspace_RGB,
			   (channels == 4) ? heif_chroma_interleaved_RGBA
					   : heif_chroma_interleaved_RGB,
			   &img);
  if (err.code != heif_error_Ok)
    {
      img = NULL;
      g_message ("%s: can't save \"%s\": %s", format->name, filename,
		 err.message ? err.message : "unknown error");
      goto out;
    }

  err = heif_image_add_plane (img, heif_channel_interleaved,
			      width, height, 8);
  plane = heif_image_get_plane (img, heif_channel_interleaved, &stride);
  if (err.code != heif_error_Ok || !plane ||
      stride < width * channels)
    {
      g_message ("%s: can't save \"%s\": out of memory",
		 format->name, filename);
      goto out;
    }

  if (!heif_fill_image (drawable, type, palette, plane, stride, channels))
    {
      g_message ("%s: out of memory saving \"%s\"", format->name, filename);
      goto out;
    }

  if (vals->lossless)
    {
      /*  Lossless needs full chroma resolution and the colour left as
       *  RGB (the identity matrix), otherwise the conversion to YCbCr
       *  loses what the codec keeps.  Not every encoder has a "chroma"
       *  parameter; those that lack it do not subsample.
       */
      heif_encoder_set_lossless (encoder, 1);
      heif_encoder_set_parameter_string (encoder, "chroma", "444");

      nclx = heif_nclx_color_profile_alloc ();
      if (nclx)
	{
	  nclx->color_primaries          = heif_color_primaries_ITU_R_BT_709_5;
	  nclx->transfer_characteristics = heif_transfer_characteristic_IEC_61966_2_1;
	  nclx->matrix_coefficients      = heif_matrix_coefficients_RGB_GBR;
	  nclx->full_range_flag          = 1;
	  heif_image_set_nclx_color_profile (img, nclx);
	}
    }
  else
    {
      heif_encoder_set_lossless (encoder, 0);
      heif_encoder_set_lossy_quality (encoder, CLAMP (vals->quality, 0, 100));
    }

  options = heif_encoding_options_alloc ();
#if LIBHEIF_HAVE_VERSION(1, 11, 0)
  if (options && nclx && options->version >= 4)
    options->output_nclx_profile = nclx;
#endif

  gimp_progress_update (0.2);

  err = heif_context_encode_image (ctx, img, encoder, options, NULL);
  if (err.code != heif_error_Ok)
    {
      g_message ("%s: can't encode \"%s\": %s", format->name, filename,
		 err.message ? err.message : "unknown error");
      goto out;
    }

  gimp_progress_update (0.9);

  /*  Only now that the image is encoded, replace the file.  */
  fp = g_fopen (filename, "wb");
  if (!fp)
    {
      g_message ("%s: can't open \"%s\" for writing", format->name, filename);
      goto out;
    }

  writer.writer_api_version = 1;
  writer.write              = heif_write_callback;

  err = heif_context_write (ctx, &writer, fp);
  success = (err.code == heif_error_Ok);
  if (fclose (fp) != 0)
    success = FALSE;

  if (!success)
    g_message ("%s: error while writing \"%s\"", format->name, filename);

  gimp_progress_update (1.0);

 out:
  if (options)
    heif_encoding_options_free (options);
  if (nclx)
    heif_nclx_color_profile_free (nclx);
  if (img)
    heif_image_release (img);
  if (encoder)
    heif_encoder_release (encoder);
  if (ctx)
    heif_context_free (ctx);
  gimp_drawable_detach (drawable);

  return success;
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
  HeifSaveVals *vals = data;

  vals->quality = (gint) (gtk_adjustment_get_value (adjustment) + 0.5);
}

static void
save_lossless_update (GtkWidget *widget,
		      gpointer   data)
{
  HeifSaveVals *vals = data;

  vals->lossless = gtk_check_button_get_active (GTK_CHECK_BUTTON (widget));

  gtk_widget_set_sensitive (quality_label, !vals->lossless);
  gtk_widget_set_sensitive (quality_scale, !vals->lossless);
}

static gint
save_dialog (const HeifSaveFormat *format,
	     HeifSaveVals         *vals)
{
  GtkWidget     *dlg;
  GtkWidget     *button;
  GtkWidget     *frame;
  GtkWidget     *table;
  GtkWidget     *toggle;
  GtkAdjustment *adjustment;
  gchar         *title;

  gtk_init ();

  title = g_strdup_printf ("%s Options", format->name);
  dlg = gimp_dialog_new (title);
  g_free (title);
  g_signal_connect (dlg, "destroy", G_CALLBACK (save_close_callback), NULL);

  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (save_ok_callback), dlg,
			  TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  frame = gtk_frame_new ("Parameter Settings");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);

  table = gimp_table_new (2, 2, FALSE);
  gimp_container_set_border_width (table, 10);
  gimp_container_add (frame, table);

  quality_label = gtk_label_new ("Quality");
  gimp_misc_set_alignment (quality_label, 0.0, 0.5);
  gimp_table_attach (table, quality_label, 0, 1, 0, 1, GIMP_FILL, 0, 5, 0);

  adjustment = gtk_adjustment_new (vals->quality, 0.0, 100.0, 1.0, 10.0, 0.0);
  quality_scale = gimp_hscale_new (adjustment, 0);
  gtk_widget_set_size_request (quality_scale, SCALE_WIDTH, -1);
  gimp_table_attach (table, quality_scale, 1, 2, 0, 1,
		     GIMP_EXPAND | GIMP_FILL, 0, 0, 0);
  g_signal_connect (adjustment, "value-changed",
		    G_CALLBACK (save_quality_update), vals);

  toggle = gtk_check_button_new_with_label ("Lossless");
  gimp_table_attach (table, toggle, 0, 2, 1, 2, GIMP_FILL, 0, 0, 0);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), vals->lossless);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (save_lossless_update), vals);
  gtk_widget_set_tooltip_text (toggle,
			       "Keep every pixel exactly; the quality "
			       "setting is then not used");

  gtk_widget_set_sensitive (quality_label, !vals->lossless);
  gtk_widget_set_sensitive (quality_scale, !vals->lossless);

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return runme;
}
