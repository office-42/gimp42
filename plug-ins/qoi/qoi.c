/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QOI ("Quite OK Image") file plug-in for gimp42
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
 * Written from the QOI specification 1.0 (https://qoiformat.org).  A file
 * is a 14 byte header ("qoif", width and height as big-endian 32 bit
 * numbers, 3 or 4 channels, colorspace), a stream of operations each
 * producing one or more pixels, and an 8 byte end marker.  The decoder
 * and the encoder both keep the previous pixel and an array of 64 pixels
 * seen before, indexed by a hash of the colour.
 *
 * Loading makes an RGB or RGBA layer.  Saving writes 4 channels when the
 * drawable has an alpha channel and 3 otherwise; grayscale and indexed
 * drawables are expanded to RGB.  QOI has no options, so there is no
 * dialog.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>

#include <libgimp/gimp.h>


#define QOI_OP_INDEX	0x00	/* 00xxxxxx */
#define QOI_OP_DIFF	0x40	/* 01xxxxxx */
#define QOI_OP_LUMA	0x80	/* 10xxxxxx */
#define QOI_OP_RUN	0xc0	/* 11xxxxxx */
#define QOI_OP_RGB	0xfe	/* 11111110 */
#define QOI_OP_RGBA	0xff	/* 11111111 */
#define QOI_MASK_2	0xc0	/* 11000000 */

#define QOI_HEADER_SIZE	14
#define QOI_MAX_RUN	62
#define QOI_MAX_DIMENSION 262144	/* per side, as the other loaders */

#define QOI_HASH(p) (((guint) (p)[0] * 3 + (guint) (p)[1] * 5 + \
		      (guint) (p)[2] * 7 + (guint) (p)[3] * 11) % 64)

#define QOI_IO_SIZE	65536

static const guchar qoi_end_marker[8] = { 0, 0, 0, 0, 0, 0, 0, 1 };


/*  Buffered byte input: the decoder reads one byte at a time.  */
typedef struct
{
  FILE   *fp;
  gsize   pos;
  gsize   len;
  guchar  buf[QOI_IO_SIZE];
} QoiReader;

/*  Buffered output for the encoder.  */
typedef struct
{
  FILE     *fp;
  gsize     len;
  gboolean  error;
  guchar    buf[QOI_IO_SIZE];
} QoiWriter;


static void   query      (void);
static void   run        (char    *name,
			  int      nparams,
			  GParam  *param,
			  int     *nreturn_vals,
			  GParam **return_vals);
static gint32 load_image (const gchar *filename);
static gint   save_image (const gchar *filename,
			  gint32       image_ID,
			  gint32       drawable_ID);


GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};


MAIN ()

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
  };
  static int nsave_args = sizeof (save_args) / sizeof (save_args[0]);

  gimp_install_procedure ("file_qoi_load",
			  "Loads files in the QOI file format",
			  "Loads QOI (\"Quite OK Image\") files as an RGB "
			  "or RGBA layer.",
			  "gimp42 contributors",
			  "gimp42 contributors",
			  "2026",
			  "<Load>/QOI",
			  NULL,
			  PROC_PLUG_IN,
			  nload_args, nload_return_vals,
			  load_args, load_return_vals);

  gimp_install_procedure ("file_qoi_save",
			  "Saves files in the QOI file format",
			  "Saves the drawable as a QOI (\"Quite OK Image\") "
			  "file: 4 channels when it has an alpha channel, 3 "
			  "otherwise.  Grayscale and indexed drawables are "
			  "saved as RGB.",
			  "gimp42 contributors",
			  "gimp42 contributors",
			  "2026",
			  "<Save>/QOI",
			  "RGB*,GRAY*,INDEXED*",
			  PROC_PLUG_IN,
			  nsave_args, 0,
			  save_args, NULL);

  gimp_register_magic_load_handler ("file_qoi_load", "qoi", "",
				    "0,string,qoif");
  gimp_register_save_handler ("file_qoi_save", "qoi", "");
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
  values[0].data.d_status = STATUS_CALLING_ERROR;

  if (strcmp (name, "file_qoi_load") == 0)
    {
      if (nparams < 3)
	return;

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
  else if (strcmp (name, "file_qoi_save") == 0)
    {
      if (nparams < 5)
	return;

      if (save_image (param[3].data.d_string,
		      param[1].data.d_int32, param[2].data.d_int32))
	values[0].data.d_status = STATUS_SUCCESS;
      else
	values[0].data.d_status = STATUS_EXECUTION_ERROR;
    }
}


/*
 *  Loading
 */

static gint
qoi_read_byte (QoiReader *reader)
{
  if (reader->pos == reader->len)
    {
      reader->len = fread (reader->buf, 1, sizeof (reader->buf), reader->fp);
      reader->pos = 0;
      if (reader->len == 0)
	return -1;
    }

  return reader->buf[reader->pos++];
}

static guint32
qoi_read_be32 (const guchar *p)
{
  return ((guint32) p[0] << 24 | (guint32) p[1] << 16 |
	  (guint32) p[2] << 8  | (guint32) p[3]);
}

/*  Decodes npixels pixels into dest, channels bytes each.  px, index and
 *  run carry the decoder state from one call to the next.  Returns FALSE
 *  when the data ends before the last pixel.
 */
static gboolean
qoi_decode_pixels (QoiReader *reader,
		   guchar    *dest,
		   gsize      npixels,
		   gint       channels,
		   guchar     px[4],
		   guchar     index[64][4],
		   gint      *run)
{
  gsize i;
  gint  b1, b2, b3, b4, b5;
  gint  vg;

  for (i = 0; i < npixels; i++)
    {
      if (*run > 0)
	{
	  (*run)--;
	}
      else
	{
	  b1 = qoi_read_byte (reader);
	  if (b1 < 0)
	    return FALSE;

	  if (b1 == QOI_OP_RGB)
	    {
	      b2 = qoi_read_byte (reader);
	      b3 = qoi_read_byte (reader);
	      b4 = qoi_read_byte (reader);
	      if (b4 < 0 || b3 < 0 || b2 < 0)
		return FALSE;
	      px[0] = b2;
	      px[1] = b3;
	      px[2] = b4;
	    }
	  else if (b1 == QOI_OP_RGBA)
	    {
	      b2 = qoi_read_byte (reader);
	      b3 = qoi_read_byte (reader);
	      b4 = qoi_read_byte (reader);
	      b5 = qoi_read_byte (reader);
	      if (b5 < 0 || b4 < 0 || b3 < 0 || b2 < 0)
		return FALSE;
	      px[0] = b2;
	      px[1] = b3;
	      px[2] = b4;
	      px[3] = b5;
	    }
	  else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX)
	    {
	      memcpy (px, index[b1], 4);
	    }
	  else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF)
	    {
	      px[0] += ((b1 >> 4) & 0x03) - 2;
	      px[1] += ((b1 >> 2) & 0x03) - 2;
	      px[2] += ( b1       & 0x03) - 2;
	    }
	  else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA)
	    {
	      b2 = qoi_read_byte (reader);
	      if (b2 < 0)
		return FALSE;
	      vg = (b1 & 0x3f) - 32;
	      px[0] += vg - 8 + ((b2 >> 4) & 0x0f);
	      px[1] += vg;
	      px[2] += vg - 8 +  (b2       & 0x0f);
	    }
	  else /* QOI_OP_RUN: this pixel and up to 61 more */
	    {
	      *run = b1 & 0x3f;
	    }

	  memcpy (index[QOI_HASH (px)], px, 4);
	}

      memcpy (dest, px, channels);
      dest += channels;
    }

  return TRUE;
}

static gint32
load_image (const gchar *filename)
{
  QoiReader    *reader = NULL;
  FILE         *fp;
  guchar        header[QOI_HEADER_SIZE];
  guint32       width, height;
  gint          channels, colorspace;
  gint32        image = -1, layer;
  GDrawable    *drawable = NULL;
  GPixelRgn     pixel_rgn;
  guchar       *buf = NULL;
  guchar        px[4] = { 0, 0, 0, 255 };
  guchar        index[64][4];
  gint          run = 0;
  gint          tile_height;
  guint32       y, rows;
  gchar        *basename, *progress;
  gboolean      ok = TRUE;

  fp = g_fopen (filename, "rb");
  if (!fp)
    {
      g_message ("QOI: can't open \"%s\"", filename);
      return -1;
    }

  if (fread (header, 1, sizeof (header), fp) != sizeof (header) ||
      memcmp (header, "qoif", 4) != 0)
    {
      g_message ("QOI: \"%s\" is not a QOI file", filename);
      fclose (fp);
      return -1;
    }

  width      = qoi_read_be32 (header + 4);
  height     = qoi_read_be32 (header + 8);
  channels   = header[12];
  colorspace = header[13];

  if (width == 0 || height == 0 ||
      width > QOI_MAX_DIMENSION || height > QOI_MAX_DIMENSION)
    {
      g_message ("QOI: \"%s\" has unsupported image dimensions (%u x %u)",
		 filename, width, height);
      fclose (fp);
      return -1;
    }

  if ((channels != 3 && channels != 4) || colorspace > 1)
    {
      g_message ("QOI: \"%s\" has an invalid header", filename);
      fclose (fp);
      return -1;
    }

  tile_height = gimp_tile_height ();
  if (tile_height < 1)
    tile_height = 1;

  /*  at most 64 rows of at most 262144 pixels of 4 bytes  */
  buf    = g_try_malloc ((gsize) tile_height * width * channels);
  reader = g_try_new (QoiReader, 1);
  if (!buf || !reader)
    {
      g_message ("QOI: out of memory loading \"%s\"", filename);
      g_free (buf);
      g_free (reader);
      fclose (fp);
      return -1;
    }

  reader->fp  = fp;
  reader->pos = 0;
  reader->len = 0;

  basename = g_path_get_basename (filename);
  progress = g_strdup_printf ("Loading %s:", basename);
  gimp_progress_init (progress);
  g_free (progress);
  g_free (basename);

  image = gimp_image_new (width, height, RGB);
  if (image == -1)
    {
      g_message ("QOI: can't create a new image");
      g_free (buf);
      g_free (reader);
      fclose (fp);
      return -1;
    }

  gimp_image_set_filename (image, (gchar *) filename);

  layer = gimp_layer_new (image, "Background", width, height,
			  (channels == 4) ? RGBA_IMAGE : RGB_IMAGE,
			  100, NORMAL_MODE);
  if (layer == -1)
    {
      g_message ("QOI: can't create a new layer");
      gimp_image_delete (image);
      g_free (buf);
      g_free (reader);
      fclose (fp);
      return -1;
    }
  gimp_image_add_layer (image, layer, 0);

  drawable = gimp_drawable_get (layer);
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0,
		       drawable->width, drawable->height, TRUE, FALSE);

  memset (index, 0, sizeof (index));

  for (y = 0; y < height; y += rows)
    {
      rows = MIN ((guint32) tile_height, height - y);

      if (!qoi_decode_pixels (reader, buf, (gsize) rows * width, channels,
			      px, index, &run))
	{
	  ok = FALSE;
	  break;
	}

      gimp_pixel_rgn_set_rect (&pixel_rgn, buf, 0, y, width, rows);
      gimp_progress_update ((gdouble) (y + rows) / (gdouble) height);
    }

  g_free (buf);
  g_free (reader);
  fclose (fp);

  if (!ok)
    {
      g_message ("QOI: \"%s\" is truncated or corrupt", filename);
      gimp_drawable_detach (drawable);
      gimp_image_delete (image);
      return -1;
    }

  gimp_drawable_flush (drawable);
  gimp_drawable_detach (drawable);

  return image;
}


/*
 *  Saving
 */

static void
qoi_flush (QoiWriter *writer)
{
  if (writer->len > 0 && !writer->error &&
      fwrite (writer->buf, 1, writer->len, writer->fp) != writer->len)
    writer->error = TRUE;

  writer->len = 0;
}

/*  Makes room for n (at most 5) more bytes.  */
static inline guchar *
qoi_reserve (QoiWriter *writer,
	     gsize      n)
{
  if (writer->len + n > sizeof (writer->buf))
    qoi_flush (writer);

  return writer->buf + writer->len;
}

static void
qoi_write_bytes (QoiWriter    *writer,
		 const guchar *data,
		 gsize         n)
{
  memcpy (qoi_reserve (writer, n), data, n);
  writer->len += n;
}

static void
qoi_write_be32 (guchar  *p,
		guint32  v)
{
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}

static void
qoi_emit_run (QoiWriter *writer,
	      gint       run)
{
  guchar op = QOI_OP_RUN | (run - 1);

  qoi_write_bytes (writer, &op, 1);
}

/*  Encodes one pixel.  prev, index and run carry the encoder state;
 *  last says it is the image's last pixel, which ends any run.
 */
static void
qoi_encode_pixel (QoiWriter    *writer,
		  const guchar  px[4],
		  guchar        prev[4],
		  guchar        index[64][4],
		  gint         *run,
		  gboolean      last)
{
  guchar *out;
  guint   pos;

  if (memcmp (px, prev, 4) == 0)
    {
      (*run)++;
      if (*run == QOI_MAX_RUN || last)
	{
	  qoi_emit_run (writer, *run);
	  *run = 0;
	}
      return;
    }

  if (*run > 0)
    {
      qoi_emit_run (writer, *run);
      *run = 0;
    }

  pos = QOI_HASH (px);
  out = qoi_reserve (writer, 5);

  if (memcmp (index[pos], px, 4) == 0)
    {
      out[0] = QOI_OP_INDEX | pos;
      writer->len += 1;
    }
  else
    {
      memcpy (index[pos], px, 4);

      if (px[3] == prev[3])
	{
	  gint vr   = (gint8) (px[0] - prev[0]);
	  gint vg   = (gint8) (px[1] - prev[1]);
	  gint vb   = (gint8) (px[2] - prev[2]);
	  gint vg_r = vr - vg;
	  gint vg_b = vb - vg;

	  if (vr > -3 && vr < 2 && vg > -3 && vg < 2 && vb > -3 && vb < 2)
	    {
	      out[0] = (QOI_OP_DIFF | ((vr + 2) << 4) |
			((vg + 2) << 2) | (vb + 2));
	      writer->len += 1;
	    }
	  else if (vg_r > -9 && vg_r < 8 && vg > -33 && vg < 32 &&
		   vg_b > -9 && vg_b < 8)
	    {
	      out[0] = QOI_OP_LUMA | (vg + 32);
	      out[1] = ((vg_r + 8) << 4) | (vg_b + 8);
	      writer->len += 2;
	    }
	  else
	    {
	      out[0] = QOI_OP_RGB;
	      out[1] = px[0];
	      out[2] = px[1];
	      out[3] = px[2];
	      writer->len += 4;
	    }
	}
      else
	{
	  out[0] = QOI_OP_RGBA;
	  memcpy (out + 1, px, 4);
	  writer->len += 5;
	}
    }

  memcpy (prev, px, 4);
}

static gint
save_image (const gchar *filename,
	    gint32       image_ID,
	    gint32       drawable_ID)
{
  QoiWriter     *writer;
  FILE          *fp;
  GDrawable     *drawable;
  GDrawableType  type;
  GPixelRgn      pixel_rgn;
  guchar         header[QOI_HEADER_SIZE];
  guchar         palette[256 * 3];
  guchar        *cmap;
  guchar        *buf;
  guchar         px[4];
  guchar         prev[4] = { 0, 0, 0, 255 };
  guchar         index[64][4];
  gint           ncolors = 0;
  gint           bpp, channels;
  gint           run = 0;
  gint           tile_height;
  gint           width, height;
  gint           x, y, row, rows;
  gboolean       indexed = FALSE;
  gboolean       ok;
  gchar         *basename, *progress;

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
      g_message ("QOI: can't save this image type");
      gimp_drawable_detach (drawable);
      return FALSE;
    }

  if (width < 1 || height < 1 ||
      bpp != ((type == RGB_IMAGE)  ? 3 :
	      (type == RGBA_IMAGE) ? 4 :
	      (type == GRAYA_IMAGE || type == INDEXEDA_IMAGE) ? 2 : 1))
    {
      g_message ("QOI: can't save this drawable");
      gimp_drawable_detach (drawable);
      return FALSE;
    }

  /*  Every index maps to a colour: entries past the colormap are black.  */
  memset (palette, 0, sizeof (palette));
  if (indexed)
    {
      cmap = gimp_image_get_cmap (image_ID, &ncolors);
      if (cmap)
	memcpy (palette, cmap, CLAMP (ncolors, 0, 256) * 3);
      g_free (cmap);
    }

  tile_height = gimp_tile_height ();
  if (tile_height < 1)
    tile_height = 1;

  buf    = g_try_malloc ((gsize) tile_height * width * bpp);
  writer = g_try_new (QoiWriter, 1);
  if (!buf || !writer)
    {
      g_message ("QOI: out of memory saving \"%s\"", filename);
      g_free (buf);
      g_free (writer);
      gimp_drawable_detach (drawable);
      return FALSE;
    }

  fp = g_fopen (filename, "wb");
  if (!fp)
    {
      g_message ("QOI: can't open \"%s\" for writing", filename);
      g_free (buf);
      g_free (writer);
      gimp_drawable_detach (drawable);
      return FALSE;
    }

  writer->fp    = fp;
  writer->len   = 0;
  writer->error = FALSE;

  basename = g_path_get_basename (filename);
  progress = g_strdup_printf ("Saving %s:", basename);
  gimp_progress_init (progress);
  g_free (progress);
  g_free (basename);

  memcpy (header, "qoif", 4);
  qoi_write_be32 (header + 4, width);
  qoi_write_be32 (header + 8, height);
  header[12] = channels;
  header[13] = 0;			/* sRGB with linear alpha */
  qoi_write_bytes (writer, header, sizeof (header));

  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height,
		       FALSE, FALSE);

  memset (index, 0, sizeof (index));
  px[3] = 255;

  for (y = 0; y < height && !writer->error; y += rows)
    {
      rows = MIN (tile_height, height - y);
      gimp_pixel_rgn_get_rect (&pixel_rgn, buf, 0, y, width, rows);

      for (row = 0; row < rows; row++)
	{
	  const guchar *src = buf + (gsize) row * width * bpp;

	  for (x = 0; x < width; x++, src += bpp)
	    {
	      switch (type)
		{
		case RGB_IMAGE:
		  memcpy (px, src, 3);
		  break;
		case RGBA_IMAGE:
		  memcpy (px, src, 4);
		  break;
		case GRAY_IMAGE:
		  px[0] = px[1] = px[2] = src[0];
		  break;
		case GRAYA_IMAGE:
		  px[0] = px[1] = px[2] = src[0];
		  px[3] = src[1];
		  break;
		case INDEXED_IMAGE:
		  memcpy (px, palette + src[0] * 3, 3);
		  break;
		case INDEXEDA_IMAGE:
		  memcpy (px, palette + src[0] * 3, 3);
		  px[3] = src[1];
		  break;
		}

	      qoi_encode_pixel (writer, px, prev, index, &run,
				y + row == height - 1 && x == width - 1);
	    }
	}

      gimp_progress_update ((gdouble) (y + rows) / (gdouble) height);
    }

  qoi_write_bytes (writer, qoi_end_marker, sizeof (qoi_end_marker));
  qoi_flush (writer);

  ok = !writer->error;
  if (fclose (fp) != 0)
    ok = FALSE;

  if (!ok)
    g_message ("QOI: error while writing \"%s\"", filename);

  g_free (buf);
  g_free (writer);
  gimp_drawable_detach (drawable);

  return ok;
}
