/* ico.c -- Windows icon (.ico) and cursor (.cur) files
 *
 * gimp42 - a modernization of the GIMP 1.0
 * SPDX-License-Identifier: GPL-2.0-or-later
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
 */

/*  An icon or cursor file is a directory of images of different sizes
 *  and depths.  Each of them becomes a layer of its own size; the image
 *  is as large as the largest, and the layers are stacked from the
 *  largest (on top, so it is what one sees) to the smallest.  Saving
 *  writes every layer as one entry, top layer first.
 *
 *  An entry is either a Windows DIB without its file header -- the
 *  height counts twice, for the colour bitmap and the 1-bit AND mask
 *  that follows it -- or, usually for 256x256, a complete PNG file.
 *
 *  The file is hostile until proven otherwise: every size, offset and
 *  count in it is checked against the data that is really there.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"


#define ICO_TYPE_ICON     1
#define ICO_TYPE_CURSOR   2

#define ICO_HEADER_SIZE   6
#define ICO_ENTRY_SIZE    16
#define ICO_DIB_SIZE      40	/* BITMAPINFOHEADER */

#define ICO_MAX_ENTRY     256	/* largest size a directory entry names */
#define ICO_MAX_SIZE      262144	/* per side, as the other loaders */

/*  Entries may share their data, so a small file can describe a great
 *  many large images; this caps what one file may decode to, 256 MiB of
 *  RGBA, far beyond any real icon.
 */
#define ICO_MAX_PIXELS    ((guint64) 1 << 26)

typedef struct
{
  gint32 hot_spot_x;
  gint32 hot_spot_y;
} CurSaveVals;

typedef struct
{
  gint     width;
  gint     height;
  gint     bpp;		/* bits per pixel as stored in the file */
  gboolean png;		/* PNG-compressed entry */
  gint     hot_x;		/* cursors only */
  gint     hot_y;
  guint    index;		/* position in the directory */
  guchar  *pixels;	/* RGBA, top row first */
} IcoEntry;


static void     query          (void);
static void     run            (char    *name,
				int      nparams,
				GParam  *param,
				int     *nreturn_vals,
				GParam **return_vals);

static gint32   load_image     (const gchar *filename);
static gboolean save_image     (const gchar *filename,
				gint32       image_ID,
				gint         type,
				gint         hot_x,
				gint         hot_y);
static gboolean layers_fit     (gint32       image_ID,
				const gchar *what);
static gboolean cur_save_dialog (gint32 image_ID);


GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

static CurSaveVals cursor_vals =
{
  0, 0     /* hot spot */
};

static gboolean dialog_run = FALSE;


MAIN ()

static void
query (void)
{
  static GParamDef load_args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_STRING, "filename", "The name of the file to load" },
    { PARAM_STRING, "raw_filename", "The name entered" },
  };
  static GParamDef load_return_vals[] =
  {
    { PARAM_IMAGE, "image", "Output image" },
  };
  static int nload_args = sizeof (load_args) / sizeof (load_args[0]);
  static int nload_return_vals = (sizeof (load_return_vals) /
				  sizeof (load_return_vals[0]));

  static GParamDef ico_save_args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image" },
    { PARAM_DRAWABLE, "drawable", "Drawable to save (unused: every layer is saved)" },
    { PARAM_STRING, "filename", "The name of the file to save the image in" },
    { PARAM_STRING, "raw_filename", "The name entered" },
  };
  static int nico_save_args = sizeof (ico_save_args) / sizeof (ico_save_args[0]);

  static GParamDef cur_save_args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image" },
    { PARAM_DRAWABLE, "drawable", "Drawable to save (unused: every layer is saved)" },
    { PARAM_STRING, "filename", "The name of the file to save the image in" },
    { PARAM_STRING, "raw_filename", "The name entered" },
    { PARAM_INT32, "hot_spot_x", "X coordinate of the cursor's hot spot, in image pixels" },
    { PARAM_INT32, "hot_spot_y", "Y coordinate of the cursor's hot spot, in image pixels" },
  };
  static int ncur_save_args = sizeof (cur_save_args) / sizeof (cur_save_args[0]);

  gimp_install_procedure ("file_ico_load",
			  "Loads Windows icon files",
			  "Loads a Windows icon (.ico).  Every image in the "
			  "file becomes an RGBA layer of its own size, named "
			  "after its size and depth; the largest is on top.  "
			  "Entries may be DIBs of 1, 4, 8, 16, 24 or 32 bits "
			  "per pixel or PNG-compressed.",
			  "gimp42",
			  "gimp42",
			  "2026",
			  "<Load>/Windows Icon",
			  NULL,
			  PROC_PLUG_IN,
			  nload_args, nload_return_vals,
			  load_args, load_return_vals);

  gimp_install_procedure ("file_cur_load",
			  "Loads Windows cursor files",
			  "Loads a Windows cursor (.cur) the way file_ico_load "
			  "loads an icon.  The hot spot of the largest image "
			  "becomes the default for the next file_cur_save.",
			  "gimp42",
			  "gimp42",
			  "2026",
			  "<Load>/Windows Cursor",
			  NULL,
			  PROC_PLUG_IN,
			  nload_args, nload_return_vals,
			  load_args, load_return_vals);

  gimp_install_procedure ("file_ico_save",
			  "Saves Windows icon files",
			  "Saves every layer of the image as one entry of a "
			  "Windows icon (.ico), top layer first.  Layers can "
			  "be at most 256x256 pixels.  Entries are written as "
			  "32-bit BGRA with an AND mask; a layer 256 pixels "
			  "wide or high is PNG-compressed.",
			  "gimp42",
			  "gimp42",
			  "2026",
			  "<Save>/Windows Icon",
			  "RGB*, GRAY*, INDEXED*",
			  PROC_PLUG_IN,
			  nico_save_args, 0,
			  ico_save_args, NULL);

  gimp_install_procedure ("file_cur_save",
			  "Saves Windows cursor files",
			  "Saves every layer of the image as one entry of a "
			  "Windows cursor (.cur), as file_ico_save does for "
			  "icons.  The hot spot is given in image pixels; a "
			  "layer of another size gets it scaled to its size.",
			  "gimp42",
			  "gimp42",
			  "2026",
			  "<Save>/Windows Cursor",
			  "RGB*, GRAY*, INDEXED*",
			  PROC_PLUG_IN,
			  ncur_save_args, 0,
			  cur_save_args, NULL);

  /*  A cursor starts 00 00 02 00, as many TGA files do, so it is only
   *  recognised by its extension.
   */
  gimp_register_magic_load_handler ("file_ico_load", "ico", "",
				    "0,long,0x00000100");
  gimp_register_load_handler ("file_cur_load", "cur", "");
  gimp_register_save_handler ("file_ico_save", "ico", "");
  gimp_register_save_handler ("file_cur_save", "cur", "");
}

static void
run (char    *name,
     int      nparams,
     GParam  *param,
     int     *nreturn_vals,
     GParam **return_vals)
{
  static GParam values[2];
  GRunModeType  run_mode;
  gint32        image_ID;

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals  = values;
  values[0].type          = PARAM_STATUS;
  values[0].data.d_status = STATUS_CALLING_ERROR;

  if (strcmp (name, "file_ico_load") == 0 ||
      strcmp (name, "file_cur_load") == 0)
    {
      *nreturn_vals = 2;
      values[1].type         = PARAM_IMAGE;
      values[1].data.d_image = -1;

      if (nparams < 3)
	return;

      image_ID = load_image (param[1].data.d_string);
      if (image_ID != -1)
	{
	  values[0].data.d_status = STATUS_SUCCESS;
	  values[1].data.d_image  = image_ID;
	}
      else
	values[0].data.d_status = STATUS_EXECUTION_ERROR;
    }
  else if (strcmp (name, "file_ico_save") == 0)
    {
      if (nparams < 5)
	return;

      if (save_image (param[3].data.d_string, param[1].data.d_image,
		      ICO_TYPE_ICON, 0, 0))
	values[0].data.d_status = STATUS_SUCCESS;
      else
	values[0].data.d_status = STATUS_EXECUTION_ERROR;
    }
  else if (strcmp (name, "file_cur_save") == 0)
    {
      gint32 image = (nparams >= 2) ? param[1].data.d_image : -1;

      if (nparams < 5)
	return;

      switch (run_mode)
	{
	case RUN_INTERACTIVE:
	  gimp_get_data ("file_cur_save", &cursor_vals);
	  /*  Rather no dialog than one for a file that can't be saved.  */
	  if (! layers_fit (image, "Windows Cursor") ||
	      ! cur_save_dialog (image))
	    {
	      values[0].data.d_status = STATUS_EXECUTION_ERROR;
	      return;
	    }
	  break;

	case RUN_NONINTERACTIVE:
	  /*  The hot spot may be left out; it is then the top left.  */
	  cursor_vals.hot_spot_x = (nparams > 5) ? param[5].data.d_int32 : 0;
	  cursor_vals.hot_spot_y = (nparams > 6) ? param[6].data.d_int32 : 0;
	  if (cursor_vals.hot_spot_x < 0 ||
	      cursor_vals.hot_spot_y < 0 ||
	      cursor_vals.hot_spot_x >= (gint32) gimp_image_width (image) ||
	      cursor_vals.hot_spot_y >= (gint32) gimp_image_height (image))
	    {
	      g_message ("Windows Cursor: the hot spot %d,%d is outside "
			 "the image", cursor_vals.hot_spot_x,
			 cursor_vals.hot_spot_y);
	      return;
	    }
	  break;

	case RUN_WITH_LAST_VALS:
	  gimp_get_data ("file_cur_save", &cursor_vals);
	  break;

	default:
	  break;
	}

      if (save_image (param[3].data.d_string, image, ICO_TYPE_CURSOR,
		      cursor_vals.hot_spot_x, cursor_vals.hot_spot_y))
	{
	  gimp_set_data ("file_cur_save", &cursor_vals, sizeof (cursor_vals));
	  values[0].data.d_status = STATUS_SUCCESS;
	}
      else
	values[0].data.d_status = STATUS_EXECUTION_ERROR;
    }
}


/*  Little-endian fields.  */

static guint
get_u16 (const guchar *p)
{
  return (guint) p[0] | ((guint) p[1] << 8);
}

static guint32
get_u32 (const guchar *p)
{
  return ((guint32) p[0] | ((guint32) p[1] << 8) |
	  ((guint32) p[2] << 16) | ((guint32) p[3] << 24));
}

static void
put_u16 (guchar *p,
	 guint   v)
{
  p[0] = v & 0xff;
  p[1] = (v >> 8) & 0xff;
}

static void
put_u32 (guchar  *p,
	 guint32  v)
{
  p[0] = v & 0xff;
  p[1] = (v >> 8) & 0xff;
  p[2] = (v >> 16) & 0xff;
  p[3] = (v >> 24) & 0xff;
}


/*  Decode one PNG-compressed entry.  The loader is told it is PNG, so
 *  the data cannot pick some other gdk-pixbuf loader, and the size is
 *  read from the IHDR chunk and checked before anything is decoded.
 */
static gboolean
ico_read_png (const guchar *data,
	      gsize         len,
	      guint64       max_pixels,
	      IcoEntry     *e)
{
  static const guchar ihdr[4] = { 'I', 'H', 'D', 'R' };
  GdkPixbufLoader *loader;
  GdkPixbuf       *pixbuf;
  GError          *error = NULL;
  guint32          w, h;
  gint             depth, channels = 0;
  gint             x, y, n, rowstride;
  const guchar    *src;
  guchar          *dest;
  gboolean         ok;

  if (len < 33 || memcmp (data + 12, ihdr, 4) != 0)
    return FALSE;

  w     = ((guint32) data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
  h     = ((guint32) data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
  depth = data[24];
  switch (data[25])
    {
    case 0: channels = 1; break;	/* grey */
    case 2: channels = 3; break;	/* RGB */
    case 3: channels = 1; break;	/* palette */
    case 4: channels = 2; break;	/* grey + alpha */
    case 6: channels = 4; break;	/* RGBA */
    default: return FALSE;
    }

  if (w == 0 || h == 0 || w > ICO_MAX_SIZE || h > ICO_MAX_SIZE ||
      (guint64) w * h > max_pixels)
    return FALSE;

  loader = gdk_pixbuf_loader_new_with_type ("png", &error);
  if (! loader)
    {
      g_message ("Windows Icon: can't decode PNG entries: %s",
		 error ? error->message : "no PNG loader");
      g_clear_error (&error);
      return FALSE;
    }

  /*  A failed write closes the loader itself.  */
  ok = (gdk_pixbuf_loader_write (loader, data, len, NULL) &&
	gdk_pixbuf_loader_close (loader, NULL));

  pixbuf = ok ? gdk_pixbuf_loader_get_pixbuf (loader) : NULL;
  if (! pixbuf ||
      gdk_pixbuf_get_colorspace (pixbuf) != GDK_COLORSPACE_RGB ||
      gdk_pixbuf_get_bits_per_sample (pixbuf) != 8 ||
      gdk_pixbuf_get_width (pixbuf) != (gint) w ||
      gdk_pixbuf_get_height (pixbuf) != (gint) h ||
      (gdk_pixbuf_get_n_channels (pixbuf) != 3 &&
       gdk_pixbuf_get_n_channels (pixbuf) != 4))
    {
      g_object_unref (loader);
      return FALSE;
    }

  n         = gdk_pixbuf_get_n_channels (pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (pixbuf);

  e->pixels = g_try_malloc_n ((gsize) w * h, 4);
  if (! e->pixels)
    {
      g_object_unref (loader);
      return FALSE;
    }

  dest = e->pixels;
  for (y = 0; y < (gint) h; y++)
    {
      src = gdk_pixbuf_read_pixels (pixbuf) + (gsize) y * rowstride;
      for (x = 0; x < (gint) w; x++, src += n, dest += 4)
	{
	  dest[0] = src[0];
	  dest[1] = src[1];
	  dest[2] = src[2];
	  dest[3] = (n == 4) ? src[3] : 255;
	}
    }

  g_object_unref (loader);

  e->width  = w;
  e->height = h;
  e->bpp    = depth * channels;
  e->png    = TRUE;

  return TRUE;
}

/*  Decode one DIB entry: BITMAPINFOHEADER, colour table, the colour
 *  bitmap and the AND mask, both bottom row first with rows padded to
 *  four bytes.
 */
static gboolean
ico_read_dib (const guchar *data,
	      gsize         len,
	      guint64       max_pixels,
	      IcoEntry     *e)
{
  guint32       header_size, compression, ncolors;
  gint32        w, h2, h;
  gint          bpp;
  guint64       xor_stride, and_stride;
  guint64       xor_offset, and_offset;
  guchar        palette[256][3];
  const guchar *row, *mask;
  guchar       *dest;
  gboolean      has_alpha = FALSE;
  gboolean      has_mask;
  gint          x, y;
  guint         i;

  if (len < ICO_DIB_SIZE)
    return FALSE;

  header_size = get_u32 (data);
  w           = (gint32) get_u32 (data + 4);
  h2          = (gint32) get_u32 (data + 8);
  bpp         = get_u16 (data + 14);
  compression = get_u32 (data + 16);
  ncolors     = get_u32 (data + 32);

  /*  BITMAPINFOHEADER or one of its longer successors.  */
  if (header_size < ICO_DIB_SIZE || header_size > len)
    return FALSE;

  /*  The height covers the colour bitmap and the mask.  */
  h = h2 / 2;
  if (w <= 0 || h <= 0 || w > ICO_MAX_SIZE || h > ICO_MAX_SIZE ||
      (guint64) w * h > max_pixels)
    return FALSE;

  if (compression != 0)		/* BI_RGB only */
    return FALSE;

  switch (bpp)
    {
    case 1: case 4: case 8:
      if (ncolors == 0)
	ncolors = 1 << bpp;
      break;
    case 16: case 24: case 32:
      break;			/* a colour table here is only a hint */
    default:
      return FALSE;
    }
  if (ncolors > 256)
    return FALSE;

  xor_stride = (((guint64) w * bpp + 31) / 32) * 4;
  and_stride = (((guint64) w + 31) / 32) * 4;
  xor_offset = (guint64) header_size + (guint64) ncolors * 4;
  and_offset = xor_offset + xor_stride * h;

  if (and_offset > len)
    return FALSE;

  /*  Some writers leave the mask out of 32-bit entries.  */
  has_mask = (and_offset + and_stride * h <= len);

  memset (palette, 0, sizeof (palette));
  for (i = 0; i < ncolors && bpp <= 8; i++)
    {
      const guchar *c = data + header_size + i * 4;

      palette[i][0] = c[2];
      palette[i][1] = c[1];
      palette[i][2] = c[0];
    }

  e->pixels = g_try_malloc_n ((gsize) w * h, 4);
  if (! e->pixels)
    return FALSE;

  for (y = 0; y < h; y++)
    {
      row  = data + xor_offset + xor_stride * (h - 1 - y);
      dest = e->pixels + (gsize) y * w * 4;

      for (x = 0; x < w; x++, dest += 4)
	{
	  guint v;

	  dest[3] = 255;

	  switch (bpp)
	    {
	    case 1:
	    case 4:
	    case 8:
	      {
		guint bit = (guint) x * bpp;

		v = (row[bit / 8] >> (8 - bpp - bit % 8)) & ((1 << bpp) - 1);
		if (v < ncolors)
		  memcpy (dest, palette[v], 3);
		else
		  dest[0] = dest[1] = dest[2] = 0;
	      }
	      break;

	    case 16:		/* 5-5-5 */
	      v = get_u16 (row + x * 2);
	      dest[0] = ((v >> 10) & 0x1f) * 255 / 31;
	      dest[1] = ((v >> 5) & 0x1f) * 255 / 31;
	      dest[2] = (v & 0x1f) * 255 / 31;
	      break;

	    case 24:
	      dest[0] = row[x * 3 + 2];
	      dest[1] = row[x * 3 + 1];
	      dest[2] = row[x * 3];
	      break;

	    case 32:
	      dest[0] = row[x * 4 + 2];
	      dest[1] = row[x * 4 + 1];
	      dest[2] = row[x * 4];
	      dest[3] = row[x * 4 + 3];
	      if (dest[3])
		has_alpha = TRUE;
	      break;
	    }
	}
    }

  /*  A 32-bit entry with a real alpha channel ignores the mask; one
   *  whose alpha is all zero (an old writer) and every other depth
   *  take their transparency from it.
   */
  if (! has_alpha)
    {
      for (y = 0; y < h; y++)
	{
	  mask = has_mask ? data + and_offset + and_stride * (h - 1 - y) : NULL;
	  dest = e->pixels + (gsize) y * w * 4;

	  for (x = 0; x < w; x++, dest += 4)
	    dest[3] = (mask && (mask[x / 8] & (0x80 >> (x % 8)))) ? 0 : 255;
	}
    }

  e->width  = w;
  e->height = h;
  e->bpp    = bpp;
  e->png    = FALSE;

  return TRUE;
}

/*  Larger entries first, then deeper ones, then in file order: the
 *  order of the layers from the top down.
 */
static gint
ico_entry_compare (gconstpointer a,
		   gconstpointer b)
{
  const IcoEntry *ea = a;
  const IcoEntry *eb = b;
  gint64 area_a = (gint64) ea->width * ea->height;
  gint64 area_b = (gint64) eb->width * eb->height;

  if (area_a != area_b)
    return (area_a > area_b) ? -1 : 1;
  if (ea->bpp != eb->bpp)
    return (ea->bpp > eb->bpp) ? -1 : 1;
  return (ea->index < eb->index) ? -1 : (ea->index > eb->index);
}

static gint32
load_image (const gchar *filename)
{
  gchar     *contents = NULL;
  gsize      size = 0;
  GError    *error = NULL;
  gchar     *basename, *progress;
  const guchar *data;
  guint      type, count, n, i;
  IcoEntry  *entries;
  guint      nentries = 0;
  gint       width = 0, height = 0;
  guint64    budget = ICO_MAX_PIXELS;
  gint32     image_ID = -1;

  basename = g_path_get_basename (filename);

  if (! g_file_get_contents (filename, &contents, &size, &error))
    {
      g_message ("Windows Icon: can't open \"%s\": %s", basename,
		 error->message);
      g_error_free (error);
      g_free (basename);
      return -1;
    }
  data = (const guchar *) contents;

  if (size < ICO_HEADER_SIZE || get_u16 (data) != 0)
    goto not_ico;
  type  = get_u16 (data + 2);
  count = get_u16 (data + 4);
  if ((type != ICO_TYPE_ICON && type != ICO_TYPE_CURSOR) || count == 0)
    goto not_ico;

  /*  A cut-off directory still has the entries before the cut.  */
  n = MIN (count, (size - ICO_HEADER_SIZE) / ICO_ENTRY_SIZE);
  if (n == 0)
    goto not_ico;

  progress = g_strdup_printf ("Loading %s:", basename);
  gimp_progress_init (progress);
  g_free (progress);

  entries = g_new0 (IcoEntry, n);

  for (i = 0; i < n; i++)
    {
      const guchar *dir = data + ICO_HEADER_SIZE + i * ICO_ENTRY_SIZE;
      guint32  length = get_u32 (dir + 8);
      guint32  offset = get_u32 (dir + 12);
      IcoEntry *e = &entries[nentries];
      gboolean  ok;

      /*  An entry reaching past the end is read as far as it goes;
       *  the decoders then refuse what is missing.
       */
      if (offset < ICO_HEADER_SIZE || offset >= size)
	continue;
      if (length > size - offset)
	length = size - offset;

      e->index = i;
      e->hot_x = get_u16 (dir + 4);
      e->hot_y = get_u16 (dir + 6);

      if (length >= 8 && memcmp (data + offset, "\211PNG\r\n\032\n", 8) == 0)
	ok = ico_read_png (data + offset, length, budget, e);
      else
	ok = ico_read_dib (data + offset, length, budget, e);

      if (ok)
	{
	  width  = MAX (width, e->width);
	  height = MAX (height, e->height);
	  budget -= (guint64) e->width * e->height;
	  nentries++;
	}
      else
	{
	  g_free (e->pixels);
	  memset (e, 0, sizeof (IcoEntry));
	}

      gimp_progress_update (0.5 * (i + 1) / n);
    }

  if (nentries == 0)
    {
      g_message ("Windows Icon: \"%s\" has no image that can be read",
		 basename);
      g_free (entries);
      g_free (contents);
      g_free (basename);
      return -1;
    }

  if (nentries < count)
    g_message ("Windows Icon: %u of the %u images in \"%s\" could not be "
	       "read", count - nentries, count, basename);

  qsort (entries, nentries, sizeof (IcoEntry), ico_entry_compare);

  image_ID = gimp_image_new (width, height, RGB);
  gimp_image_set_filename (image_ID, (char *) filename);

  /*  From the bottom up, each new layer going on top.  */
  for (i = nentries; i-- > 0; )
    {
      IcoEntry  *e = &entries[i];
      GDrawable *drawable;
      GPixelRgn  pixel_rgn;
      gint32     layer_ID;
      gchar     *name;

      if (e->png)
	name = g_strdup_printf ("%dx%d, %d bpp, PNG", e->width, e->height,
				e->bpp);
      else
	name = g_strdup_printf ("%dx%d, %d bpp", e->width, e->height, e->bpp);

      layer_ID = gimp_layer_new (image_ID, name, e->width, e->height,
				 RGBA_IMAGE, 100, NORMAL_MODE);
      g_free (name);
      gimp_image_add_layer (image_ID, layer_ID, 0);

      drawable = gimp_drawable_get (layer_ID);
      gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, e->width, e->height,
			   TRUE, FALSE);
      gimp_pixel_rgn_set_rect (&pixel_rgn, e->pixels, 0, 0,
			       e->width, e->height);
      gimp_drawable_flush (drawable);
      gimp_drawable_detach (drawable);

      gimp_progress_update (0.5 + 0.5 * (nentries - i) / nentries);
    }

  /*  A cursor's hot spot has no place in the image; it becomes the
   *  default for saving it again.
   */
  if (type == ICO_TYPE_CURSOR)
    {
      CurSaveVals vals;

      vals.hot_spot_x = MIN (entries[0].hot_x, entries[0].width - 1);
      vals.hot_spot_y = MIN (entries[0].hot_y, entries[0].height - 1);
      gimp_set_data ("file_cur_save", &vals, sizeof (vals));
    }

  for (i = 0; i < nentries; i++)
    g_free (entries[i].pixels);
  g_free (entries);
  g_free (contents);
  g_free (basename);

  return image_ID;

 not_ico:
  g_message ("Windows Icon: \"%s\" is not an icon or cursor file", basename);
  g_free (contents);
  g_free (basename);
  return -1;
}


/*  A layer's pixels as RGBA, whatever its type.  */
static guchar *
ico_layer_rgba (gint32        layer_ID,
		const guchar *cmap,
		gint          ncolors)
{
  GDrawable    *drawable;
  GPixelRgn     pixel_rgn;
  GDrawableType type;
  gint          w, h, bpp;
  gsize         i, npixels;
  guchar       *src, *dest;

  drawable = gimp_drawable_get (layer_ID);
  type     = gimp_drawable_type (layer_ID);
  w        = drawable->width;
  h        = drawable->height;
  bpp      = drawable->bpp;
  npixels  = (gsize) w * h;

  src  = g_new (guchar, npixels * bpp);
  dest = g_new (guchar, npixels * 4);

  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, w, h, FALSE, FALSE);
  gimp_pixel_rgn_get_rect (&pixel_rgn, src, 0, 0, w, h);
  gimp_drawable_detach (drawable);

  for (i = 0; i < npixels; i++)
    {
      const guchar *s = src + i * bpp;
      guchar       *d = dest + i * 4;

      switch (type)
	{
	case RGB_IMAGE:
	case RGBA_IMAGE:
	  d[0] = s[0];
	  d[1] = s[1];
	  d[2] = s[2];
	  d[3] = (type == RGBA_IMAGE) ? s[3] : 255;
	  break;

	case GRAY_IMAGE:
	case GRAYA_IMAGE:
	  d[0] = d[1] = d[2] = s[0];
	  d[3] = (type == GRAYA_IMAGE) ? s[1] : 255;
	  break;

	case INDEXED_IMAGE:
	case INDEXEDA_IMAGE:
	  if (cmap && s[0] < ncolors)
	    memcpy (d, cmap + s[0] * 3, 3);
	  else
	    d[0] = d[1] = d[2] = 0;
	  d[3] = (type == INDEXEDA_IMAGE) ? s[1] : 255;
	  break;

	default:
	  d[0] = d[1] = d[2] = d[3] = 0;
	  break;
	}
    }

  g_free (src);

  return dest;
}

/*  One entry as a 32-bit DIB: colour bitmap in BGRA, then an AND mask
 *  that hides what is less than half opaque from programs that ignore
 *  the alpha channel.  Fully transparent pixels are written black, which
 *  is what such programs expect under a set mask bit.
 */
static GByteArray *
ico_encode_dib (const guchar *rgba,
		gint          w,
		gint          h)
{
  GByteArray *out;
  guchar      header[ICO_DIB_SIZE];
  gsize       and_stride = ((w + 31) / 32) * 4;
  gsize       image_size = (gsize) w * h * 4 + and_stride * h;
  guchar     *row;
  gint        x, y;

  memset (header, 0, sizeof (header));
  put_u32 (header, ICO_DIB_SIZE);
  put_u32 (header + 4, w);
  put_u32 (header + 8, h * 2);
  put_u16 (header + 12, 1);	/* planes */
  put_u16 (header + 14, 32);	/* bits per pixel */
  put_u32 (header + 20, image_size);

  out = g_byte_array_sized_new (ICO_DIB_SIZE + image_size);
  g_byte_array_append (out, header, ICO_DIB_SIZE);

  row = g_new (guchar, MAX ((gsize) w * 4, and_stride));

  for (y = h - 1; y >= 0; y--)
    {
      const guchar *s = rgba + (gsize) y * w * 4;

      for (x = 0; x < w; x++, s += 4)
	{
	  if (s[3] == 0)
	    memset (row + x * 4, 0, 4);
	  else
	    {
	      row[x * 4]     = s[2];
	      row[x * 4 + 1] = s[1];
	      row[x * 4 + 2] = s[0];
	      row[x * 4 + 3] = s[3];
	    }
	}
      g_byte_array_append (out, row, w * 4);
    }

  for (y = h - 1; y >= 0; y--)
    {
      const guchar *s = rgba + (gsize) y * w * 4;

      memset (row, 0, and_stride);
      for (x = 0; x < w; x++, s += 4)
	if (s[3] < 128)
	  row[x / 8] |= 0x80 >> (x % 8);
      g_byte_array_append (out, row, and_stride);
    }

  g_free (row);

  return out;
}

static GByteArray *
ico_encode_png (guchar *rgba,
		gint    w,
		gint    h)
{
  GdkPixbuf  *pixbuf;
  GByteArray *out;
  gchar      *buffer = NULL;
  gsize       length = 0;
  GError     *error = NULL;

  pixbuf = gdk_pixbuf_new_from_data (rgba, GDK_COLORSPACE_RGB, TRUE, 8,
				     w, h, w * 4, NULL, NULL);

  if (! gdk_pixbuf_save_to_buffer (pixbuf, &buffer, &length, "png", &error,
				   NULL))
    {
      g_message ("Windows Icon: can't compress an entry as PNG: %s",
		 error->message);
      g_error_free (error);
      g_object_unref (pixbuf);
      return NULL;
    }
  g_object_unref (pixbuf);

  out = g_byte_array_sized_new (length);
  g_byte_array_append (out, (guchar *) buffer, length);
  g_free (buffer);

  return out;
}

/*  Whether every layer can become an entry: at most 256x256 pixels, and
 *  at most 65535 of them.
 */
static gboolean
layers_fit (gint32       image_ID,
	    const gchar *what)
{
  gint32  *layers;
  gint     nlayers = 0;
  gboolean fit = TRUE;
  gint     i;

  layers = gimp_image_get_layers (image_ID, &nlayers);
  if (! layers || nlayers < 1)
    {
      g_message ("%s: the image has no layers", what);
      g_free (layers);
      return FALSE;
    }
  if (nlayers > 65535)
    {
      g_message ("%s: a file holds at most 65535 images", what);
      g_free (layers);
      return FALSE;
    }

  for (i = 0; i < nlayers && fit; i++)
    {
      gint w = gimp_drawable_width (layers[i]);
      gint h = gimp_drawable_height (layers[i]);

      if (w > ICO_MAX_ENTRY || h > ICO_MAX_ENTRY)
	{
	  gchar *name = gimp_layer_get_name (layers[i]);

	  g_message ("%s: the layer \"%s\" is %dx%d pixels; icons and "
		     "cursors can be at most %dx%d.  Scale or crop it first.",
		     what, name ? name : "", w, h,
		     ICO_MAX_ENTRY, ICO_MAX_ENTRY);
	  g_free (name);
	  fit = FALSE;
	}
    }

  g_free (layers);

  return fit;
}

static gboolean
save_image (const gchar *filename,
	    gint32       image_ID,
	    gint         type,
	    gint         hot_x,
	    gint         hot_y)
{
  const gchar *what = (type == ICO_TYPE_CURSOR) ? "Windows Cursor"
						: "Windows Icon";
  gint32      *layers;
  gint         nlayers = 0;
  guchar      *cmap = NULL;
  gint         ncolors = 0;
  GByteArray **blobs;
  guchar       header[ICO_HEADER_SIZE];
  gint         image_w, image_h;
  gchar       *basename, *progress;
  FILE        *fp;
  guint32      offset;
  gboolean     ok = TRUE;
  gint         i;

  /*  Refuse before anything is written.  */
  if (! layers_fit (image_ID, what))
    return FALSE;

  layers = gimp_image_get_layers (image_ID, &nlayers);
  if (! layers || nlayers < 1)
    {
      g_free (layers);
      return FALSE;
    }

  basename = g_path_get_basename (filename);
  progress = g_strdup_printf ("Saving %s:", basename);
  gimp_progress_init (progress);
  g_free (progress);

  if (gimp_image_base_type (image_ID) == INDEXED)
    cmap = gimp_image_get_cmap (image_ID, &ncolors);

  image_w = gimp_image_width (image_ID);
  image_h = gimp_image_height (image_ID);

  blobs = g_new0 (GByteArray *, nlayers);
  for (i = 0; i < nlayers && ok; i++)
    {
      gint    w = gimp_drawable_width (layers[i]);
      gint    h = gimp_drawable_height (layers[i]);
      guchar *rgba = ico_layer_rgba (layers[i], cmap, ncolors);

      if (w >= ICO_MAX_ENTRY || h >= ICO_MAX_ENTRY)
	blobs[i] = ico_encode_png (rgba, w, h);
      else
	blobs[i] = ico_encode_dib (rgba, w, h);
      g_free (rgba);

      ok = (blobs[i] != NULL);
      gimp_progress_update (0.9 * (i + 1) / nlayers);
    }

  if (ok)
    {
      fp = g_fopen (filename, "wb");
      if (! fp)
	{
	  g_message ("%s: can't create \"%s\"", what, basename);
	  ok = FALSE;
	}
      else
	{
	  put_u16 (header, 0);
	  put_u16 (header + 2, type);
	  put_u16 (header + 4, nlayers);
	  ok = fwrite (header, ICO_HEADER_SIZE, 1, fp) == 1;

	  offset = ICO_HEADER_SIZE + ICO_ENTRY_SIZE * nlayers;
	  for (i = 0; i < nlayers && ok; i++)
	    {
	      guchar entry[ICO_ENTRY_SIZE];
	      gint   w = gimp_drawable_width (layers[i]);
	      gint   h = gimp_drawable_height (layers[i]);

	      memset (entry, 0, sizeof (entry));
	      entry[0] = (w >= ICO_MAX_ENTRY) ? 0 : w;
	      entry[1] = (h >= ICO_MAX_ENTRY) ? 0 : h;
	      if (type == ICO_TYPE_CURSOR)
		{
		  /*  The hot spot is in image pixels; a layer of another
		   *  size gets it scaled to its size.
		   */
		  gint x = (image_w > 0) ? (gint) ((gint64) hot_x * w / image_w) : 0;
		  gint y = (image_h > 0) ? (gint) ((gint64) hot_y * h / image_h) : 0;

		  put_u16 (entry + 4, CLAMP (x, 0, w - 1));
		  put_u16 (entry + 6, CLAMP (y, 0, h - 1));
		}
	      else
		{
		  put_u16 (entry + 4, 1);	/* planes */
		  put_u16 (entry + 6, 32);	/* bits per pixel */
		}
	      put_u32 (entry + 8, blobs[i]->len);
	      put_u32 (entry + 12, offset);
	      offset += blobs[i]->len;

	      ok = fwrite (entry, ICO_ENTRY_SIZE, 1, fp) == 1;
	    }

	  for (i = 0; i < nlayers && ok; i++)
	    ok = fwrite (blobs[i]->data, 1, blobs[i]->len, fp) == blobs[i]->len;

	  if (fclose (fp) != 0)
	    ok = FALSE;
	  if (! ok)
	    {
	      g_message ("%s: error while writing \"%s\"", what, basename);
	      g_unlink (filename);
	    }
	}
    }

  gimp_progress_update (1.0);

  for (i = 0; i < nlayers; i++)
    if (blobs[i])
      g_byte_array_free (blobs[i], TRUE);
  g_free (blobs);
  g_free (cmap);
  g_free (layers);
  g_free (basename);

  return ok;
}


/*  The cursor's hot spot.  */

static GtkWidget *hot_x_spin = NULL;
static GtkWidget *hot_y_spin = NULL;

static void
cur_close_callback (GtkWidget *widget,
		    gpointer   data)
{
  gimp_main_loop_quit ();
}

static void
cur_ok_callback (GtkWidget *widget,
		 gpointer   data)
{
  /*  Take in what was typed but not yet entered.  */
  gtk_spin_button_update (GTK_SPIN_BUTTON (hot_x_spin));
  gtk_spin_button_update (GTK_SPIN_BUTTON (hot_y_spin));

  cursor_vals.hot_spot_x =
    gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (hot_x_spin));
  cursor_vals.hot_spot_y =
    gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (hot_y_spin));
  dialog_run = TRUE;

  gtk_window_destroy (GTK_WINDOW (data));
}

static gboolean
cur_save_dialog (gint32 image_ID)
{
  GtkWidget     *dlg, *button, *frame, *table, *label;
  GtkAdjustment *adj;
  gint           w = MAX (1, (gint) gimp_image_width (image_ID));
  gint           h = MAX (1, (gint) gimp_image_height (image_ID));

  gtk_init ();

  dlg = gimp_dialog_new ("Save as Windows Cursor");
  g_signal_connect (dlg, "destroy", G_CALLBACK (cur_close_callback), NULL);

  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (cur_ok_callback), dlg, TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  frame = gtk_frame_new ("Hot Spot");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);

  table = gimp_table_new (2, 2, FALSE);
  gimp_container_set_border_width (table, 10);
  gtk_grid_set_row_spacing (GTK_GRID (table), 5);
  gtk_grid_set_column_spacing (GTK_GRID (table), 10);
  gimp_container_add (frame, table);

  label = gtk_label_new ("X:");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 0, 1, GIMP_FILL, GIMP_FILL, 0, 0);

  adj = gtk_adjustment_new (CLAMP (cursor_vals.hot_spot_x, 0, w - 1),
			    0, w - 1, 1, 10, 0);
  hot_x_spin = gtk_spin_button_new (adj, 1, 0);
  gimp_table_attach (table, hot_x_spin, 1, 2, 0, 1,
		     GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 0, 0);

  label = gtk_label_new ("Y:");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0);

  adj = gtk_adjustment_new (CLAMP (cursor_vals.hot_spot_y, 0, h - 1),
			    0, h - 1, 1, 10, 0);
  hot_y_spin = gtk_spin_button_new (adj, 1, 0);
  gimp_table_attach (table, hot_y_spin, 1, 2, 1, 2,
		     GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 0, 0);

#if GTK_CHECK_VERSION (4, 14, 0)
  /*  Enter in a field saves.  */
  gtk_spin_button_set_activates_default (GTK_SPIN_BUTTON (hot_x_spin), TRUE);
  gtk_spin_button_set_activates_default (GTK_SPIN_BUTTON (hot_y_spin), TRUE);
#endif

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return dialog_run;
}
