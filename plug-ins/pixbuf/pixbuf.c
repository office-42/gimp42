/* pixbuf.c -- load the image formats gdk-pixbuf reads that gimp42 has no
 * plug-in of its own for
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

/*  GTK needs gdk-pixbuf, and gdk-pixbuf comes with loaders for more
 *  formats than the GIMP ever had: SVG through librsvg, Apple icons,
 *  animated cursors, X bitmaps, QuickTime images, WMF and EMF through
 *  GDI+ on Windows, and whatever else is installed.  This plug-in
 *  installs one load procedure per format, file_pixbuf_<name>_load,
 *  for every format no other gimp42 plug-in handles.
 *
 *  The formats are found when the plug-in is queried, and the
 *  application queries a plug-in again only when its program file
 *  changes.  A loader installed or removed later is noticed once the
 *  plug-in is rebuilt or reinstalled, or its entry is removed from
 *  pluginrc.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  For GdkPixbufFormat's signature, which has no accessor.  */
#define GDK_PIXBUF_ENABLE_BACKEND
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"


#define PIXBUF_MAX_SIZE       262144	/* per side, as the other loaders */
#define PIXBUF_DEFAULT_SIZE   512	/* scalable image without a size */

typedef struct
{
  gint     width;
  gint     height;
  gboolean keep_aspect;
} RenderVals;


static void     query          (void);
static void     run            (char    *name,
				int      nparams,
				GParam  *param,
				int     *nreturn_vals,
				GParam **return_vals);

static gint32   load_image     (const gchar     *filename,
				GdkPixbufFormat *format,
				GRunModeType     run_mode);
static gboolean render_dialog  (const gchar     *description,
				gint             natural_width,
				gint             natural_height,
				RenderVals      *vals);


GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

MAIN ()


/*  Formats with a gimp42 plug-in of their own, by gdk-pixbuf's name for
 *  them.  (ras is Sun raster: plug-ins/sunras.)
 */
static const gchar *const own_formats[] =
{
  "png", "jpeg", "gif", "bmp", "ico", "tiff", "xpm", "pnm", "tga",
  "webp", "avif", "heif", "jxl", "qoi", "pcx", "ras", "psd", "sgi",
  "xwd", "fits", "xcf",
  NULL
};

/*  File name extensions the gimp42 plug-ins register.  A format keeps
 *  only the extensions not in here, so no two plug-ins claim one; a
 *  format left with none is not installed.
 */
static const gchar *const own_extensions[] =
{
  "png", "jpg", "jpeg", "jpe", "jfif", "gif", "bmp", "dib", "ico", "cur",
  "tif", "tiff", "xpm", "pnm", "ppm", "pgm", "pbm", "pam", "tga", "targa",
  "webp", "heic", "heif", "hif", "avif", "jxl", "qoi", "pcx", "psd",
  "ps", "eps", "pdf", "fit", "fits", "rgb", "bw", "sgi", "icon", "xwd",
  "ras", "im1", "im8", "im24", "im32", "pat", "gbr", "cel", "fli", "g3",
  "hrz", "pix", "matte", "mask", "alpha", "als", "snp", "mpg", "mpeg",
  "xd", "gicon", "xcf", "gz", "bz2", "xcfgz", "xcfbz2",
  NULL
};

static gboolean
in_list (const gchar        *s,
	 const gchar *const *list)
{
  for (; *list; list++)
    if (g_ascii_strcasecmp (s, *list) == 0)
      return TRUE;

  return FALSE;
}

/*  gdk-pixbuf's format name as part of a procedure name: lower case
 *  letters, digits and underscores.
 */
static gchar *
format_proc_name (GdkPixbufFormat *format)
{
  gchar *name = gdk_pixbuf_format_get_name (format);
  gchar *proc;
  gchar *p;

  for (p = name; *p; p++)
    *p = g_ascii_isalnum (*p) ? g_ascii_tolower (*p) : '_';

  proc = g_strdup_printf ("file_pixbuf_%s_load", name);
  g_free (name);

  return proc;
}

/*  The extensions to register for a format, comma separated, or NULL
 *  when the format is not ours to load.
 */
static gchar *
format_extensions (GdkPixbufFormat *format)
{
  GString *list;
  gchar  **extensions;
  gchar   *name;
  gint     i;

  if (gdk_pixbuf_format_is_disabled (format))
    return NULL;

  name = gdk_pixbuf_format_get_name (format);
  if (! name || in_list (name, own_formats))
    {
      g_free (name);
      return NULL;
    }
  g_free (name);

  list = g_string_new (NULL);
  extensions = gdk_pixbuf_format_get_extensions (format);
  for (i = 0; extensions && extensions[i]; i++)
    {
      const gchar *e = extensions[i];
      const gchar *p;
      gboolean     plain = (*e != '\0');

      /*  A magic or extension list is split at spaces and commas.  */
      for (p = e; *p; p++)
	if (! g_ascii_isalnum (*p) && *p != '.' && *p != '_' && *p != '-')
	  plain = FALSE;

      if (plain && ! in_list (e, own_extensions))
	{
	  if (list->len)
	    g_string_append_c (list, ',');
	  g_string_append (list, e);
	}
    }
  g_strfreev (extensions);

  if (list->len == 0)
    {
      g_string_free (list, TRUE);
      return NULL;
    }

  return g_string_free (list, FALSE);
}

/*  The format's signature as load handler magics, where that can be
 *  said simply: anchored patterns of full relevance with at least four
 *  bytes that must match.  Each run of such bytes is one string test;
 *  the tests of one pattern are joined ("offset&" ands a test with the
 *  next one), the patterns are alternatives.  Everything but letters is
 *  written as an octal escape, since the list is split at spaces and
 *  commas.
 */
static gchar *
format_magics (GdkPixbufFormat *format)
{
  GdkPixbufModulePattern *pattern;
  GString *magics = g_string_new (NULL);

  for (pattern = format->signature; pattern && pattern->prefix; pattern++)
    {
      const gchar *prefix = pattern->prefix;
      const gchar *mask   = pattern->mask;
      gint         len    = strlen (prefix);
      gint         exact  = 0, i;
      gboolean     usable = TRUE;

      if (pattern->relevance < 100 || len == 0)
	continue;
      if (mask && (gint) strlen (mask) < len)
	continue;

      /*  '*' (unanchored), '!' (must differ) and 'n' (non-zero) cannot
       *  be said with a string test.
       */
      for (i = 0; i < len && mask; i++)
	if (mask[i] != ' ' && mask[i] != 'x' && mask[i] != 'z')
	  usable = FALSE;
      if (! usable)
	continue;

      for (i = 0; i < len; i++)
	if (! mask || mask[i] != 'x')
	  exact++;
      if (exact < 4)
	continue;

      for (i = 0; i < len; )
	{
	  gint next;

	  if (mask && mask[i] == 'x')
	    {
	      i++;
	      continue;
	    }

	  /*  Is there another run after this one?  */
	  for (next = i; next < len && ! (mask && mask[next] == 'x'); next++)
	    ;
	  while (next < len && mask && mask[next] == 'x')
	    next++;

	  if (magics->len)
	    g_string_append_c (magics, ',');
	  g_string_append_printf (magics, "%d%s,string,", i,
				  (next < len) ? "&" : "");

	  for (; i < len && ! (mask && mask[i] == 'x'); i++)
	    {
	      guchar c = (mask && mask[i] == 'z') ? 0 : (guchar) prefix[i];

	      if (g_ascii_isalpha (c))
		g_string_append_c (magics, c);
	      else
		g_string_append_printf (magics, "\\%03o", c);
	    }
	}
    }

  if (magics->len == 0)
    {
      g_string_free (magics, TRUE);
      return NULL;
    }

  return g_string_free (magics, FALSE);
}

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
  GSList     *formats, *list;
  GHashTable *installed;

  installed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  formats   = gdk_pixbuf_get_formats ();

  for (list = formats; list; list = list->next)
    {
      GdkPixbufFormat *format = list->data;
      gchar *extensions, *magics;
      gchar *proc, *description, *menu_path, *blurb, *help, *p;

      extensions = format_extensions (format);
      if (! extensions)
	continue;

      proc = format_proc_name (format);
      if (g_hash_table_contains (installed, proc))
	{
	  /*  Two loaders for one format: the first one gdk-pixbuf lists
	   *  is the one it uses, and the one run () finds.
	   */
	  g_free (proc);
	  g_free (extensions);
	  continue;
	}

      description = gdk_pixbuf_format_get_description (format);
      if (! description || ! *description)
	{
	  g_free (description);
	  description = gdk_pixbuf_format_get_name (format);
	}
      /*  The menu path's last part names the file dialog's filter.  */
      for (p = description; *p; p++)
	if (*p == '/')
	  *p = '-';

      menu_path = g_strdup_printf ("<Load>/%s", description);
      blurb     = g_strdup_printf ("Loads %s files through gdk-pixbuf",
				   description);
      help      = g_strdup_printf ("Loads %s files (%s) with the gdk-pixbuf "
				   "loader installed for them.%s",
				   description, extensions,
				   gdk_pixbuf_format_is_scalable (format)
				   ? "  The image is rendered at the size "
				     "chosen in a dialog, or at its natural "
				     "size when run non-interactively." : "");

      gimp_install_procedure (proc, blurb, help,
			      "gimp42", "gimp42", "2026",
			      menu_path, NULL, PROC_PLUG_IN,
			      nload_args, nload_return_vals,
			      load_args, load_return_vals);

      magics = format_magics (format);
      gimp_register_magic_load_handler (proc, extensions, "",
					magics ? magics : "");

      g_free (magics);
      g_free (help);
      g_free (blurb);
      g_free (menu_path);
      g_free (description);
      g_hash_table_add (installed, proc);
      g_free (extensions);
    }

  g_slist_free (formats);
  g_hash_table_destroy (installed);
}

static void
run (char    *name,
     int      nparams,
     GParam  *param,
     int     *nreturn_vals,
     GParam **return_vals)
{
  static GParam    values[2];
  GdkPixbufFormat *format = NULL;
  GSList          *formats, *list;
  gint32           image_ID;

  *nreturn_vals = 2;
  *return_vals  = values;
  values[0].type          = PARAM_STATUS;
  values[0].data.d_status = STATUS_CALLING_ERROR;
  values[1].type          = PARAM_IMAGE;
  values[1].data.d_image  = -1;

  if (nparams < 3)
    return;

  formats = gdk_pixbuf_get_formats ();
  for (list = formats; list && ! format; list = list->next)
    {
      gchar *proc = format_proc_name (list->data);

      if (strcmp (proc, name) == 0)
	format = list->data;
      g_free (proc);
    }
  g_slist_free (formats);

  if (! format)
    {
      g_message ("gdk-pixbuf: no loader for %s any more", name);
      values[0].data.d_status = STATUS_EXECUTION_ERROR;
      return;
    }

  image_ID = load_image (param[1].data.d_string, format,
			 param[0].data.d_int32);
  if (image_ID != -1)
    {
      values[0].data.d_status = STATUS_SUCCESS;
      values[1].data.d_image  = image_ID;
    }
  else
    values[0].data.d_status = STATUS_EXECUTION_ERROR;
}


/*  Loading through the loader of one format, whatever the file is
 *  called.
 */

typedef struct
{
  gint     width;		/* size to render at, 0 for the natural one */
  gint     height;
  gint     natural_width;	/* set once the loader knows it */
  gint     natural_height;
  gboolean size_only;		/* stop as soon as the size is known */
} TypedLoad;

static void
typed_size_prepared (GdkPixbufLoader *loader,
		     gint             width,
		     gint             height,
		     gpointer         data)
{
  TypedLoad *load = data;

  load->natural_width  = width;
  load->natural_height = height;

  if (load->size_only)
    gdk_pixbuf_loader_set_size (loader, 0, 0);
  else if (load->width > 0 && load->height > 0)
    gdk_pixbuf_loader_set_size (loader, load->width, load->height);
}

static GdkPixbuf *
typed_load (const gchar     *filename,
	    GdkPixbufFormat *format,
	    TypedLoad       *load,
	    GError         **error)
{
  GdkPixbufLoader *loader;
  GdkPixbuf       *pixbuf = NULL;
  gchar           *name;
  guchar           buffer[65536];
  gsize            n;
  gboolean         ok = TRUE;
  FILE            *fp;

  name   = gdk_pixbuf_format_get_name (format);
  loader = gdk_pixbuf_loader_new_with_type (name, error);
  g_free (name);
  if (! loader)
    return NULL;

  g_signal_connect (loader, "size-prepared",
		    G_CALLBACK (typed_size_prepared), load);

  fp = g_fopen (filename, "rb");
  if (! fp)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		   "can't open the file");
      gdk_pixbuf_loader_close (loader, NULL);
      g_object_unref (loader);
      return NULL;
    }

  /*  A failed write closes the loader itself.  */
  while (ok && (n = fread (buffer, 1, sizeof (buffer), fp)) > 0)
    {
      ok = gdk_pixbuf_loader_write (loader, buffer, n, error);
      if (load->size_only && load->natural_width > 0)
	break;
    }
  fclose (fp);

  if (ok)
    ok = gdk_pixbuf_loader_close (loader, load->size_only ? NULL : error);

  if (ok && ! load->size_only)
    {
      pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
      if (pixbuf)
	g_object_ref (pixbuf);
      else
	g_set_error (error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
		     "the file holds no image");
    }

  g_object_unref (loader);

  return pixbuf;
}

static gint32
load_image (const gchar     *filename,
	    GdkPixbufFormat *format,
	    GRunModeType     run_mode)
{
  GdkPixbuf   *pixbuf, *oriented;
  GError      *error = NULL;
  GDrawable   *drawable;
  GPixelRgn    pixel_rgn;
  gint32       image_ID, layer_ID;
  gchar       *basename, *progress, *description;
  gint         natural_w = 0, natural_h = 0;
  gboolean     sniffed;
  gint         width, height, n_channels, rowstride, bpp;
  gint         tile_height, y, row, nrows;
  const guchar *pixels;
  guchar      *buffer;

  basename    = g_path_get_basename (filename);
  description = gdk_pixbuf_format_get_description (format);

  /*  gdk-pixbuf picks a loader by the file's name and contents, and may
   *  not know a file that came here by its magic under another name.
   *  That one goes to the loader of the format it was found as.
   */
  sniffed = (gdk_pixbuf_get_file_info (filename, &natural_w,
				       &natural_h) != NULL);
  if (! sniffed)
    {
      TypedLoad info = { 0, 0, 0, 0, TRUE };

      typed_load (filename, format, &info, NULL);
      natural_w = info.natural_width;
      natural_h = info.natural_height;
    }

  if (gdk_pixbuf_format_is_scalable (format))
    {
      RenderVals vals;

      vals.width       = (natural_w > 0) ? natural_w : PIXBUF_DEFAULT_SIZE;
      vals.height      = (natural_h > 0) ? natural_h : PIXBUF_DEFAULT_SIZE;
      vals.keep_aspect = TRUE;

      /*  Keep a huge natural size within bounds, and its proportions.  */
      if (vals.width > PIXBUF_MAX_SIZE || vals.height > PIXBUF_MAX_SIZE)
	{
	  gdouble f = (gdouble) PIXBUF_MAX_SIZE / MAX (vals.width, vals.height);

	  vals.width  = CLAMP ((gint) (vals.width * f + 0.5), 1, PIXBUF_MAX_SIZE);
	  vals.height = CLAMP ((gint) (vals.height * f + 0.5), 1, PIXBUF_MAX_SIZE);
	}

      if (run_mode == RUN_INTERACTIVE)
	{
	  gimp_get_data ("file_pixbuf_load", &vals.keep_aspect);
	  if (! render_dialog (description, natural_w, natural_h, &vals))
	    {
	      g_free (description);
	      g_free (basename);
	      return -1;
	    }
	  gimp_set_data ("file_pixbuf_load", &vals.keep_aspect,
			 sizeof (vals.keep_aspect));
	}

      progress = g_strdup_printf ("Loading %s:", basename);
      gimp_progress_init (progress);
      g_free (progress);

      if (sniffed)
	pixbuf = gdk_pixbuf_new_from_file_at_scale (filename,
						    vals.width, vals.height,
						    FALSE, &error);
      else
	{
	  TypedLoad load = { vals.width, vals.height, 0, 0, FALSE };

	  pixbuf = typed_load (filename, format, &load, &error);
	}
    }
  else
    {
      if (natural_w > PIXBUF_MAX_SIZE || natural_h > PIXBUF_MAX_SIZE)
	{
	  g_message ("%s: \"%s\" is too large (%dx%d)", description,
		     basename, natural_w, natural_h);
	  g_free (description);
	  g_free (basename);
	  return -1;
	}

      progress = g_strdup_printf ("Loading %s:", basename);
      gimp_progress_init (progress);
      g_free (progress);

      if (sniffed)
	pixbuf = gdk_pixbuf_new_from_file (filename, &error);
      else
	{
	  TypedLoad load = { 0, 0, 0, 0, FALSE };

	  pixbuf = typed_load (filename, format, &load, &error);
	}
    }

  /*  EXIF and TIFF orientation tags.  */
  if (pixbuf)
    {
      oriented = gdk_pixbuf_apply_embedded_orientation (pixbuf);
      g_object_unref (pixbuf);
      pixbuf = oriented;
    }

  if (! pixbuf)
    {
      g_message ("%s: can't load \"%s\": %s", description, basename,
		 error ? error->message : "out of memory");
      g_clear_error (&error);
      g_free (description);
      g_free (basename);
      return -1;
    }

  width      = gdk_pixbuf_get_width (pixbuf);
  height     = gdk_pixbuf_get_height (pixbuf);
  n_channels = gdk_pixbuf_get_n_channels (pixbuf);
  rowstride  = gdk_pixbuf_get_rowstride (pixbuf);

  if (gdk_pixbuf_get_colorspace (pixbuf) != GDK_COLORSPACE_RGB ||
      gdk_pixbuf_get_bits_per_sample (pixbuf) != 8 ||
      (n_channels != 3 && n_channels != 4) ||
      n_channels != (gdk_pixbuf_get_has_alpha (pixbuf) ? 4 : 3) ||
      width < 1 || height < 1 ||
      width > PIXBUF_MAX_SIZE || height > PIXBUF_MAX_SIZE)
    {
      g_message ("%s: \"%s\" has an unsupported size or pixel format",
		 description, basename);
      g_object_unref (pixbuf);
      g_free (description);
      g_free (basename);
      return -1;
    }

  bpp = n_channels;

  image_ID = gimp_image_new (width, height, RGB);
  gimp_image_set_filename (image_ID, (char *) filename);

  layer_ID = gimp_layer_new (image_ID, "Background", width, height,
			     (bpp == 4) ? RGBA_IMAGE : RGB_IMAGE,
			     100, NORMAL_MODE);
  gimp_image_add_layer (image_ID, layer_ID, 0);

  drawable = gimp_drawable_get (layer_ID);
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height,
		       TRUE, FALSE);

  /*  A tile's height of rows at a time.  The pixbuf's rows are
   *  rowstride apart, and its last row may be shorter than that.
   */
  tile_height = gimp_tile_height ();
  pixels      = gdk_pixbuf_read_pixels (pixbuf);
  buffer      = g_new (guchar, (gsize) tile_height * width * bpp);

  for (y = 0; y < height; y += tile_height)
    {
      nrows = MIN (tile_height, height - y);

      for (row = 0; row < nrows; row++)
	memcpy (buffer + (gsize) row * width * bpp,
		pixels + (gsize) (y + row) * rowstride,
		(gsize) width * bpp);

      gimp_pixel_rgn_set_rect (&pixel_rgn, buffer, 0, y, width, nrows);
      gimp_progress_update ((gdouble) (y + nrows) / height);
    }

  g_free (buffer);
  gimp_drawable_flush (drawable);
  gimp_drawable_detach (drawable);

  g_object_unref (pixbuf);
  g_free (description);
  g_free (basename);

  return image_ID;
}


/*  The size to render a scalable image at.  */

typedef struct
{
  GtkWidget  *dialog;
  GtkWidget  *width_spin;
  GtkWidget  *height_spin;
  GtkWidget  *aspect_check;
  gdouble     ratio;		/* width / height */
  gboolean    updating;
  gboolean    run;
  RenderVals *vals;
} RenderDialog;

static void
render_close_callback (GtkWidget *widget,
		       gpointer   data)
{
  gimp_main_loop_quit ();
}

static void
render_ok_callback (GtkWidget *widget,
		    gpointer   data)
{
  RenderDialog *rd = data;

  /*  Take in what was typed but not yet entered.  */
  gtk_spin_button_update (GTK_SPIN_BUTTON (rd->width_spin));
  gtk_spin_button_update (GTK_SPIN_BUTTON (rd->height_spin));

  rd->vals->width =
    gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (rd->width_spin));
  rd->vals->height =
    gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (rd->height_spin));
  rd->vals->keep_aspect =
    gtk_check_button_get_active (GTK_CHECK_BUTTON (rd->aspect_check));
  rd->run = TRUE;

  gtk_window_destroy (GTK_WINDOW (rd->dialog));
}

static void
render_width_changed (GtkSpinButton *spin,
		      gpointer       data)
{
  RenderDialog *rd = data;
  gdouble       h;

  if (rd->updating ||
      ! gtk_check_button_get_active (GTK_CHECK_BUTTON (rd->aspect_check)))
    return;

  h = gtk_spin_button_get_value (spin) / rd->ratio;
  rd->updating = TRUE;
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (rd->height_spin),
			     CLAMP (floor (h + 0.5), 1, PIXBUF_MAX_SIZE));
  rd->updating = FALSE;
}

static void
render_height_changed (GtkSpinButton *spin,
		       gpointer       data)
{
  RenderDialog *rd = data;
  gdouble       w;

  if (rd->updating ||
      ! gtk_check_button_get_active (GTK_CHECK_BUTTON (rd->aspect_check)))
    return;

  w = gtk_spin_button_get_value (spin) * rd->ratio;
  rd->updating = TRUE;
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (rd->width_spin),
			     CLAMP (floor (w + 0.5), 1, PIXBUF_MAX_SIZE));
  rd->updating = FALSE;
}

/*  Turning "Keep aspect ratio" on keeps the ratio the two sizes have
 *  then; the natural one comes back by typing it in.
 */
static void
render_aspect_toggled (GtkCheckButton *check,
		       gpointer        data)
{
  RenderDialog *rd = data;

  if (gtk_check_button_get_active (check))
    rd->ratio =
      gtk_spin_button_get_value (GTK_SPIN_BUTTON (rd->width_spin)) /
      MAX (1.0, gtk_spin_button_get_value (GTK_SPIN_BUTTON (rd->height_spin)));
}

static gboolean
render_dialog (const gchar *description,
	       gint         natural_width,
	       gint         natural_height,
	       RenderVals  *vals)
{
  RenderDialog   rd;
  GtkWidget     *dlg, *button, *frame, *table, *label;
  GtkAdjustment *adj;
  gchar         *title, *text;

  gtk_init ();

  memset (&rd, 0, sizeof (rd));
  rd.vals  = vals;
  rd.ratio = (gdouble) vals->width / MAX (1, vals->height);

  title = g_strdup_printf ("Load %s", description);
  dlg = rd.dialog = gimp_dialog_new (title);
  g_free (title);
  g_signal_connect (dlg, "destroy", G_CALLBACK (render_close_callback), NULL);

  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (render_ok_callback), &rd,
			  TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  frame = gtk_frame_new ("Render Size");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);

  table = gimp_table_new (4, 2, FALSE);
  gimp_container_set_border_width (table, 10);
  gtk_grid_set_row_spacing (GTK_GRID (table), 5);
  gtk_grid_set_column_spacing (GTK_GRID (table), 10);
  gimp_container_add (frame, table);

  if (natural_width > 0 && natural_height > 0)
    text = g_strdup_printf ("Natural size: %d x %d pixels",
			    natural_width, natural_height);
  else
    text = g_strdup ("The file does not give a size.");
  label = gtk_label_new (text);
  g_free (text);
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 2, 0, 1, GIMP_FILL, GIMP_FILL, 0, 0);

  label = gtk_label_new ("Width:");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0);

  adj = gtk_adjustment_new (vals->width, 1, PIXBUF_MAX_SIZE, 1, 10, 0);
  rd.width_spin = gtk_spin_button_new (adj, 1, 0);
  gimp_table_attach (table, rd.width_spin, 1, 2, 1, 2,
		     GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 0, 0);

  label = gtk_label_new ("Height:");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 2, 3, GIMP_FILL, GIMP_FILL, 0, 0);

  adj = gtk_adjustment_new (vals->height, 1, PIXBUF_MAX_SIZE, 1, 10, 0);
  rd.height_spin = gtk_spin_button_new (adj, 1, 0);
  gimp_table_attach (table, rd.height_spin, 1, 2, 2, 3,
		     GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 0, 0);

#if GTK_CHECK_VERSION (4, 14, 0)
  /*  Enter in a size field loads.  */
  gtk_spin_button_set_activates_default (GTK_SPIN_BUTTON (rd.width_spin), TRUE);
  gtk_spin_button_set_activates_default (GTK_SPIN_BUTTON (rd.height_spin), TRUE);
#endif

  rd.aspect_check = gtk_check_button_new_with_label ("Keep aspect ratio");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (rd.aspect_check),
			       vals->keep_aspect);
  gimp_table_attach (table, rd.aspect_check, 0, 2, 3, 4,
		     GIMP_FILL, GIMP_FILL, 0, 0);

  g_signal_connect (rd.width_spin, "value-changed",
		    G_CALLBACK (render_width_changed), &rd);
  g_signal_connect (rd.height_spin, "value-changed",
		    G_CALLBACK (render_height_changed), &rd);
  g_signal_connect (rd.aspect_check, "toggled",
		    G_CALLBACK (render_aspect_toggled), &rd);

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return rd.run;
}
