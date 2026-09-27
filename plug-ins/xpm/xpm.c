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

/* XPM plugin version 1.2.2 */

/* 1.2.2 fixes bug that generated bad digits on images with more than 20000 colors. (thanks, yanele)
parses gtkrc (thanks, yosh)
doesn't load parameter screen on images that don't have alpha

1.2.1 fixes some minor bugs -- spaces in #XXXXXX strings, small typos in code.

1.2 compute color indexes so that we don't have to use XpmSaveXImage*

Previous...Inherited code from Ray Lehtiniemi, who inherited it from S & P.
*/

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

/* A small, self-contained reader and writer for XPM (version 3) files,
 * taking the place of libXpm, which does not exist everywhere.  The
 * structures keep libXpm's names and meaning so the rest of the plug-in
 * reads as before.
 */

typedef struct
{
  char *string;			/* the cpp characters of this color */
  char *symbolic;		/* s key */
  char *m_color;		/* m key */
  char *g4_color;		/* g4 key */
  char *g_color;		/* g key */
  char *c_color;		/* c key */
} XpmColor;

typedef struct
{
  unsigned int  width;
  unsigned int  height;
  unsigned int  cpp;
  unsigned int  ncolors;
  XpmColor     *colorTable;
  unsigned int *data;
} XpmImage;

/* Returns the quoted strings of an XPM file, in order, skipping C
 * comments.  Escapes are kept as they are; XPM does not use them.
 */
static GPtrArray *
xpm_read_strings (const gchar *contents,
		  gsize        length)
{
  GPtrArray   *strings = g_ptr_array_new_with_free_func (g_free);
  const gchar *p = contents;
  const gchar *end = contents + length;

  while (p < end)
    {
      if (p + 1 < end && p[0] == '/' && p[1] == '*')
	{
	  p += 2;
	  while (p + 1 < end && ! (p[0] == '*' && p[1] == '/'))
	    p++;
	  p += 2;
	}
      else if (*p == '"')
	{
	  const gchar *start = ++p;

	  while (p < end && *p != '"')
	    p++;
	  g_ptr_array_add (strings, g_strndup (start, p - start));
	  p++;
	}
      else
	p++;
    }

  return strings;
}

static gboolean
xpm_is_key (const gchar *token)
{
  return (strcmp (token, "c") == 0 || strcmp (token, "m") == 0 ||
	  strcmp (token, "g") == 0 || strcmp (token, "g4") == 0 ||
	  strcmp (token, "s") == 0);
}

/* Parses the part of a color line after the characters:
 * "key value [key value ...]", where a value may contain spaces.
 */
static void
xpm_parse_color_keys (XpmColor    *color,
		      const gchar *spec)
{
  gchar  **tokens;
  gchar   *key = NULL;
  GString *value = NULL;
  gint     i;

  tokens = g_strsplit_set (spec, " \t", -1);

  for (i = 0; ; i++)
    {
      gchar *token = tokens[i];

      if (token && *token == '\0')
	continue;

      if (token == NULL || (xpm_is_key (token) && value && value->len > 0) ||
	  (xpm_is_key (token) && key == NULL))
	{
	  if (key && value)
	    {
	      gchar *v = g_string_free (value, FALSE);

	      if (strcmp (key, "c") == 0)
		{ g_free (color->c_color); color->c_color = v; }
	      else if (strcmp (key, "m") == 0)
		{ g_free (color->m_color); color->m_color = v; }
	      else if (strcmp (key, "g") == 0)
		{ g_free (color->g_color); color->g_color = v; }
	      else if (strcmp (key, "g4") == 0)
		{ g_free (color->g4_color); color->g4_color = v; }
	      else
		{ g_free (color->symbolic); color->symbolic = v; }
	      value = NULL;
	    }
	  if (token == NULL)
	    break;
	  key = token;
	  value = g_string_new (NULL);
	}
      else if (value)
	{
	  if (value->len > 0)
	    g_string_append_c (value, ' ');
	  g_string_append (value, token);
	}
    }

  if (value)
    g_string_free (value, TRUE);
  g_strfreev (tokens);
}

static gboolean
xpm_read_file (const char *filename,
	       XpmImage   *image)
{
  gchar       *contents;
  gsize        length;
  GPtrArray   *strings;
  GHashTable  *lookup = NULL;
  gint         index1[256];
  guint        w, h, ncolors, cpp;
  guint        i, x, y;
  gboolean     ok = FALSE;

  memset (image, 0, sizeof (XpmImage));

  if (! g_file_get_contents (filename, &contents, &length, NULL))
    return FALSE;

  strings = xpm_read_strings (contents, length);
  g_free (contents);

  /* gsize arithmetic: ncolors + h must not wrap around */
  if (strings->len < 1 ||
      sscanf (g_ptr_array_index (strings, 0), "%u %u %u %u",
	      &w, &h, &ncolors, &cpp) != 4 ||
      w == 0 || h == 0 || ncolors == 0 || cpp == 0 || cpp > 8 ||
      w > 65536 || h > 65536 ||
      (gsize) strings->len < 1 + (gsize) ncolors + (gsize) h)
    goto out;

  image->colorTable = g_try_new0 (XpmColor, ncolors);
  image->data       = g_try_new0 (unsigned int, (gsize) w * h);
  if (image->colorTable == NULL || image->data == NULL)
    goto out;

  image->width      = w;
  image->height     = h;
  image->ncolors    = ncolors;
  image->cpp        = cpp;

  if (cpp == 1)
    for (i = 0; i < 256; i++)
      index1[i] = 0;
  else
    lookup = g_hash_table_new (g_str_hash, g_str_equal);

  for (i = 0; i < ncolors; i++)
    {
      const gchar *line = g_ptr_array_index (strings, 1 + i);
      XpmColor    *color = &image->colorTable[i];

      if (strlen (line) < cpp)
	goto out;

      color->string = g_strndup (line, cpp);
      xpm_parse_color_keys (color, line + cpp);

      if (cpp == 1)
	index1[(guchar) color->string[0]] = i;
      else
	g_hash_table_insert (lookup, color->string, GUINT_TO_POINTER (i));
    }

  for (y = 0; y < h; y++)
    {
      const gchar  *line = g_ptr_array_index (strings, 1 + ncolors + y);
      unsigned int *dest = image->data + (gsize) y * w;
      gsize         len  = strlen (line);
      gchar         key[9];

      for (x = 0; x < w && (x + 1) * cpp <= len; x++)
	{
	  if (cpp == 1)
	    dest[x] = index1[(guchar) line[x]];
	  else
	    {
	      memcpy (key, line + x * cpp, cpp);
	      key[cpp] = '\0';
	      dest[x] = GPOINTER_TO_UINT (g_hash_table_lookup (lookup, key));
	    }
	}
    }

  ok = TRUE;

 out:
  if (lookup)
    g_hash_table_destroy (lookup);
  g_ptr_array_free (strings, TRUE);

  return ok;
}

static gboolean
xpm_write_file (const char *filename,
		XpmImage   *image)
{
  FILE        *fp;
  gchar       *base;
  gchar       *p;
  guint        i, x, y;

  fp = fopen (filename, "wb");
  if (! fp)
    return FALSE;

  /* the array is named after the file, as libXpm does */
  base = g_path_get_basename (filename);
  if ((p = strchr (base, '.')) != NULL)
    *p = '\0';
  for (p = base; *p; p++)
    if (! g_ascii_isalnum (*p))
      *p = '_';
  if (! g_ascii_isalpha (base[0]) && base[0] != '_')
    {
      p = g_strconcat ("_", base, NULL);
      g_free (base);
      base = p;
    }

  fprintf (fp, "/* XPM */\nstatic char * %s[] = {\n", base);
  g_free (base);

  fprintf (fp, "\"%u %u %u %u\",\n",
	   image->width, image->height, image->ncolors, image->cpp);

  for (i = 0; i < image->ncolors; i++)
    {
      XpmColor *color = &image->colorTable[i];

      fprintf (fp, "\"%s", color->string);
      if (color->symbolic)
	fprintf (fp, "\ts %s", color->symbolic);
      if (color->m_color)
	fprintf (fp, "\tm %s", color->m_color);
      if (color->g4_color)
	fprintf (fp, "\tg4 %s", color->g4_color);
      if (color->g_color)
	fprintf (fp, "\tg %s", color->g_color);
      if (color->c_color)
	fprintf (fp, "\tc %s", color->c_color);
      fprintf (fp, "\",\n");
    }

  for (y = 0; y < image->height; y++)
    {
      unsigned int *src = image->data + (gsize) y * image->width;

      fputc ('"', fp);
      for (x = 0; x < image->width; x++)
	/* an out-of-range index (e.g. a pixel beyond the colormap)
	 * is written as the first color rather than read past the table */
	fputs (image->colorTable[src[x] < image->ncolors ? src[x] : 0].string,
	       fp);
      fprintf (fp, "\"%s\n", (y + 1 < image->height) ? "," : "");
    }

  fprintf (fp, "};\n");

  return (fclose (fp) == 0);
}

static void
xpm_free_image (XpmImage *image)
{
  guint i;

  for (i = 0; image->colorTable && i < image->ncolors; i++)
    {
      g_free (image->colorTable[i].string);
      g_free (image->colorTable[i].symbolic);
      g_free (image->colorTable[i].m_color);
      g_free (image->colorTable[i].g4_color);
      g_free (image->colorTable[i].g_color);
      g_free (image->colorTable[i].c_color);
    }
  g_free (image->colorTable);
  g_free (image->data);
  memset (image, 0, sizeof (XpmImage));
}

/* Parses an X color specification: a name from the X color database,
 * #rgb, #rrggbb, #rrrgggbbb or #rrrrggggbbbb.
 */
static gboolean
xpm_parse_color (const char *spec,
		 guchar     *r,
		 guchar     *g,
		 guchar     *b)
{
  PangoColor color;
  GdkRGBA    rgba;

  if (pango_color_parse (&color, spec))
    {
      *r = color.red >> 8;
      *g = color.green >> 8;
      *b = color.blue >> 8;
      return TRUE;
    }
  if (gdk_rgba_parse (&rgba, spec))
    {
      *r = (guchar) (rgba.red * 255.0 + 0.5);
      *g = (guchar) (rgba.green * 255.0 + 0.5);
      *b = (guchar) (rgba.blue * 255.0 + 0.5);
      return TRUE;
    }
  return FALSE;
}

static const char linenoise [] =
" .+@#$%&*=-;>,')!~{]^/(_:<[}|1234567890abcdefghijklmnopqrstuvwxyz\
ABCDEFGHIJKLMNOPQRSTUVWXYZ`";

#define SCALE_WIDTH 125

/* Structs for the save dialog */
typedef struct
{
  gdouble threshold;
} XpmSaveVals;

typedef struct
{
  gint  run;
} XpmSaveInterface;


typedef struct
{
	guchar r;
	guchar g;
	guchar b;
} rgbkey;

/* whether the image is color or not.  global so I only have to pass one user value
to the GHFunc */
  int       color;
/* bytes per pixel.  global so I only have to pass one user value
to the GHFunc */
  int        cpp;

/* Declare local functions */
static void   query      (void);
static void   run        (char    *name,
                          int      nparams,
                          GParam  *param,
                          int     *nreturn_vals,
                          GParam **return_vals);

static gint32
load_image   (char   *filename);

static void
parse_colors (XpmImage  *xpm_image,
              guchar   **cmap);

static void
parse_image  (gint32    image_ID,
              XpmImage *xpm_image,
              guchar   *cmap);

static gint
save_image   (char   *filename,
              gint32  image_ID,
              gint32  drawable_ID);


static gint
save_dialog (void);

static void
save_close_callback  (GtkWidget *widget,
                      gpointer   data);

static void
save_ok_callback     (GtkWidget *widget,
                      gpointer   data);

static void
save_scale_update    (GtkAdjustment *adjustment,
                      double        *scale_val);




GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

static XpmSaveVals xpmvals = 
{
  0.50  /* alpha threshold */
};

static XpmSaveInterface xpmint =
{
  FALSE   /*  run  */
};



MAIN ()

static void
query ()
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
  static int nload_return_vals = sizeof (load_return_vals) / sizeof (load_return_vals[0]);

  static GParamDef save_args[] =
  {
    { PARAM_INT32,    "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE,    "image", "Input image" },
    { PARAM_DRAWABLE, "drawable", "Drawable to save" },
    { PARAM_STRING,   "filename", "The name of the file to save the image in" },
    { PARAM_STRING,   "raw_filename", "The name of the file to save the image in" },
  };
  static int nsave_args = sizeof (save_args) / sizeof (save_args[0]);

  gimp_install_procedure ("file_xpm_load",
                          "loads files of the xpm file format",
                          "FIXME: write help for xpm_load",
                          "Spencer Kimball & Peter Mattis & Ray Lehtiniemi",
                          "Spencer Kimball & Peter Mattis",
                          "1997",
                          "<Load>/Xpm",
                          NULL,
                          PROC_PLUG_IN,
                          nload_args, nload_return_vals,
                          load_args, load_return_vals);
  
  gimp_install_procedure ("file_xpm_save",
                          "saves files in the xpm file format (if you're on a 16 bit display...)",
                          "FIXME: write help for xpm",
                          "Spencer Kimball & Peter Mattis & Ray Lehtiniemi & Nathan Summers",
                          "Spencer Kimball & Peter Mattis",
                          "1997",
                          "<Save>/Xpm",
                          "RGB*, GRAY*, INDEXED*",
                          PROC_PLUG_IN,
                          nsave_args, 0,
                          save_args, NULL);

  gimp_register_magic_load_handler ("file_xpm_load", "xpm", "<Load>/Xpm",
		  "0,string,/* XPM */");
  gimp_register_save_handler ("file_xpm_save", "xpm", "<Save>/Xpm");
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
  gint32 image_ID;
  GStatusType status = STATUS_SUCCESS;

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = STATUS_CALLING_ERROR;

  if (strcmp (name, "file_xpm_load") == 0)
    {
      image_ID = load_image (param[1].data.d_string);

      if (image_ID != -1)
        {
          *nreturn_vals = 2;
          values[0].data.d_status = STATUS_SUCCESS;
          values[1].type = PARAM_IMAGE;
          values[1].data.d_image = image_ID;
        }
      else
        {
          values[0].data.d_status = STATUS_EXECUTION_ERROR;
        }
    }
  else if (strcmp (name, "file_xpm_save") == 0)
    {
      switch (run_mode)
	{
	case RUN_INTERACTIVE:
	  /*  Possibly retrieve data  */
	  gimp_get_data ("file_xpm_save", &xpmvals);

	  /*  First acquire information with a dialog  */
	  if (gimp_drawable_has_alpha(param[2].data.d_int32))
		  if (! save_dialog ())
		    {
		      values[0].data.d_status = STATUS_CANCEL;
		      return;
		    }
	  break;

	case RUN_NONINTERACTIVE:
	  /*  Make sure all the arguments are there!  */
	  /* file_xpm_save has no threshold argument: param[4] is the
	   * raw filename, so never read it as a float (and with fewer
	   * arguments it would be past the end of the array) */
	  if (nparams < 4)
	    status = STATUS_CALLING_ERROR;

	case RUN_WITH_LAST_VALS:
	  /*  Possibly retrieve data  */
	  gimp_get_data ("file_xpm_save", &xpmvals);
	  break;

	default:
	  break;
	}
      *nreturn_vals = 1;
      if (status != STATUS_SUCCESS)
	{
	  values[0].data.d_status = status;
	  return;
	}
      if (save_image (param[3].data.d_string,
                      param[1].data.d_int32,
                      param[2].data.d_int32))
	{
	  gimp_set_data ("file_xpm_save", &xpmvals, sizeof (XpmSaveVals));
	  values[0].data.d_status = STATUS_SUCCESS;
	}
      else
        values[0].data.d_status = STATUS_EXECUTION_ERROR;
    }
  else
    g_assert (FALSE);
}




static gint32
load_image (char *filename)
{
  XpmImage  xpm_image;
  guchar   *cmap;
  gint32    image_ID;
  char     *name;

  /* put up a progress bar */
  name = g_strdup_printf ("Loading %s:", filename);
  gimp_progress_init (name);
  g_free (name);

  /* read the raw file */
  if (! xpm_read_file (filename, &xpm_image))
    {
      xpm_free_image (&xpm_image);
      return -1;
    }

  /* parse out the colors into a cmap */
  parse_colors (&xpm_image, &cmap);
  if (cmap == NULL)
    gimp_quit();
  
  /* create the new image */
  image_ID = gimp_image_new (xpm_image.width,
                             xpm_image.height,
                             RGB);

  /* name it */
  gimp_image_set_filename (image_ID, filename);

  /* fill it */
  parse_image(image_ID, &xpm_image, cmap);
  
  /* clean up and exit */
  g_free(cmap);
  xpm_free_image (&xpm_image);

  return image_ID;
}




static void
parse_colors (XpmImage *xpm_image, guchar **cmap)
{
  int       i, j;

  /* alloc a buffer to hold the parsed colors */
  *cmap = g_new(guchar, sizeof (guchar) * 4 * xpm_image->ncolors);

  if ((*cmap) != NULL)
    {
      /* default to black transparent */
      memset((void*)(*cmap), 0, sizeof (guchar) * 4 * xpm_image->ncolors);
      
      /* parse each color in the file */
      for (i = 0, j = 0; i < xpm_image->ncolors; i++)
        {
          char     *colorspec = "None";
          XpmColor *xpm_color;
          guchar    r, g, b;

          xpm_color = &(xpm_image->colorTable[i]);
        
          /* pick the best spec available */
          if (xpm_color->c_color)
            colorspec = xpm_color->c_color;
          else if (xpm_color->g_color)
            colorspec = xpm_color->g_color;
          else if (xpm_color->g4_color)
            colorspec = xpm_color->g4_color;
          else if (xpm_color->m_color)
            colorspec = xpm_color->m_color;
        
          /* parse if it's not transparent.  the assumption is that
             g_new will memset the buffer to zeros */
          if (g_ascii_strcasecmp (colorspec, "None") != 0) {
            if (! xpm_parse_color (colorspec, &r, &g, &b))
              r = g = b = 0;
            (*cmap)[j++] = r;
            (*cmap)[j++] = g;
            (*cmap)[j++] = b;
            (*cmap)[j++] = 255;
          } else {
            j += 4;
          }
        }
    }
}



static void
parse_image(gint32 image_ID, XpmImage *xpm_image, guchar *cmap)
{
  int        tile_height;
  int        scanlines;
  int        val;  
  guchar    *buf;
  guchar    *dest;
  unsigned int *src;
  GPixelRgn  pixel_rgn;
  GDrawable *drawable;
  gint32     layer_ID;
  int        i,j;
    

  layer_ID = gimp_layer_new (image_ID,
                             "Color",
                             xpm_image->width,
                             xpm_image->height,
                             RGBA_IMAGE,
                             100,
                             NORMAL_MODE);
    
  gimp_image_add_layer (image_ID, layer_ID, 0);

  drawable = gimp_drawable_get (layer_ID);
    
  gimp_pixel_rgn_init (&pixel_rgn, drawable,
                       0, 0,
                       drawable->width, drawable->height,
                       TRUE, FALSE);

  tile_height = gimp_tile_height ();
    
  buf  = g_new (guchar, tile_height * xpm_image->width * 4);

  if (buf != NULL)
    {
      src  = xpm_image->data;
      for (i = 0; i < xpm_image->height; i+=tile_height)
        {
          dest = buf;
          scanlines = MIN(tile_height, xpm_image->height - i);
          j = scanlines * xpm_image->width;
          while (j--) {
            {
              val = *(src++) * 4;
              *(dest)   = cmap[val];
              *(dest+1) = cmap[val+1];
              *(dest+2) = cmap[val+2];
              *(dest+3) = cmap[val+3];
              dest += 4;
            }
          
            if ((j % 100) == 0)
              gimp_progress_update ((double) i / (double) xpm_image->height);
          }
        
          gimp_pixel_rgn_set_rect (&pixel_rgn, buf,
                                   0, i,
                                   drawable->width, scanlines);
        
        }
  
      g_free(buf);
    }
  

      gimp_drawable_detach (drawable);
}


guint rgbhash (rgbkey *c)
{
	  return ((guint)c->r) ^ ((guint)c->g) ^ ((guint)c->b);
}

guint compare (rgbkey *c1, rgbkey *c2)
{
	return (c1->r == c2->r)&&(c1->g == c2->g)&&(c1->b == c2->b);
}
	

void set_XpmImage(XpmColor *array, guint index, char *colorstring)
{
	char *p;
	int i, charnum, indtemp;
	
	indtemp=index;
	array[index].string = p = g_new(char, cpp+1);
	
	/*convert the index number to base sizeof(linenoise)-1 */
	for(i=0; i<cpp; ++i)
	{
		charnum = indtemp%(sizeof(linenoise)-1);
		indtemp = indtemp / (sizeof (linenoise)-1);
		*p++=linenoise[charnum];
	}
	/* *p++=linenoise[indtemp]; */
	
	*p='\0'; /* C and its stupid null-terminated strings...*/
		
			
	array[index].symbolic = NULL;
	array[index].m_color = NULL;
	array[index].g4_color = NULL;
	if (color)
	{
		array[index].g_color = NULL;
		array[index].c_color = colorstring;
	} else {	
		array[index].c_color = NULL;
		array[index].g_color = colorstring;
	}
}

void create_colormap_from_hash (gpointer gkey, gpointer value, gpointer
		user_data)
{
	rgbkey *key = gkey;
	char *string = g_new(char, 8);
	sprintf(string, "#%02X%02X%02X", (int)key->r, (int)key->g, (int)key->b);
	set_XpmImage(user_data, *((int *) value), string);
}

static gint
save_image (char   *filename,
            gint32  image_ID,
            gint32  drawable_ID)
{
  GDrawable *drawable;
    
  int       width;
  int       height;
  int       alpha;
  int 	    ncolors=1;
  int	    *indexno;
  int 	    indexed;
  
  XpmColor  *colormap;
  XpmImage  *image;
    
  guint   *ibuff   = NULL;
  /*guint   *mbuff   = NULL;*/
  GPixelRgn  pixel_rgn;
  guchar    *buffer;
  guchar    *data;

  GHashTable *hash = NULL;
  
  int        i, j, k;
  int        threshold = 255 * xpmvals.threshold;

  gint      rc = FALSE;

  /* get some basic stats about the image */
  switch (gimp_drawable_type (drawable_ID)) {
  case RGBA_IMAGE:
  case INDEXEDA_IMAGE:
  case GRAYA_IMAGE:
    alpha = 1;
    break;
  case RGB_IMAGE:
  case INDEXED_IMAGE:
  case GRAY_IMAGE:
    alpha = 0;
    break;
  default:
    return FALSE;
  }

  switch (gimp_drawable_type (drawable_ID)) {
  case GRAYA_IMAGE:
  case GRAY_IMAGE:
    color = 0;
    break;
  case RGBA_IMAGE:
  case RGB_IMAGE:
  case INDEXED_IMAGE:
  case INDEXEDA_IMAGE:	  	  
    color = 1;
    break;
  default:
    return FALSE;
  }

  switch (gimp_drawable_type (drawable_ID)) {
  case GRAYA_IMAGE:
  case GRAY_IMAGE:
  case RGBA_IMAGE:
  case RGB_IMAGE:
    indexed = 0;
    break;
  case INDEXED_IMAGE:
  case INDEXEDA_IMAGE:	  	  
    indexed = 1;
    break;
  default:
    return FALSE;
  }

  drawable = gimp_drawable_get (drawable_ID);
  width    = drawable->width;
  height   = drawable->height;

  /* allocate buffers making the assumption that ibuff and mbuff
     are 32 bit aligned... */
  if ((ibuff = g_try_new(guint, (gsize) width * height)) == NULL)
    goto cleanup;

  /*if ((mbuff = g_new(guint, width*height)) == NULL)
    goto cleanup;*/
  
  if ((hash = g_hash_table_new_full ((GHashFunc) rgbhash, (GEqualFunc) compare, g_free, g_free)) == NULL)
    goto cleanup;
  
  /* put up a progress bar */
  {
    char *name = g_strdup_printf ("Saving %s:", filename);
    gimp_progress_init (name);
    g_free (name);
  }

  
  /* allocate a pixel region to work with */
  if ((buffer = g_new(guchar, gimp_tile_height()*width*drawable->bpp)) == NULL)
    return 0;
  gimp_pixel_rgn_init (&pixel_rgn, drawable,
                       0, 0,
                       width, height,
                       TRUE, FALSE);

  /* process each row of tiles */
  for (i = 0; i < height; i+=gimp_tile_height())
    {
      int scanlines;
  
      /* read the next row of tiles */
      scanlines = MIN(gimp_tile_height(), height - i);
      gimp_pixel_rgn_get_rect(&pixel_rgn, buffer, 0, i, width, scanlines);
      data = buffer;
      
      /* process each pixel row */
      for (j=0; j<scanlines; j++)
        {
          /* go to the start of this row in each image */
          guint *idata = ibuff + (i+j) * width;
          /*guint *mdata = mbuff + (i+j) * width;*/

          /* do each pixel in the row */
          for (k=0; k<width; k++)
            {
	      rgbkey pixel;
	      rgbkey *key = &pixel;
	      guchar a;

              /* get pixel data */
              key->r = *(data++);
	      key->g = color && !indexed ? *(data++) : key->r;
	      key->b = color && !indexed ? *(data++) : key->r;
	      a = alpha ? *(data++) : 255;

	      if (a < threshold)
		      *(idata++) = 0;
	      else
		      if (indexed)
			      *(idata++) = (key->r)+1;
		      else {
		      	indexno = g_hash_table_lookup(hash, key);
			      if (!indexno) {
				      indexno = g_new(int, 1);
				      *indexno = ncolors++;
				      g_hash_table_insert(hash, g_memdup2 (key, sizeof (rgbkey)), indexno);
		      		}
		      *(idata++) = *indexno;
		  }
            }
      
          /* kick the progress bar */
          gimp_progress_update ((double) (i+j) / (double) height);
        }
    
  } 
  g_free(buffer);

  if (indexed)
  {
	  guchar *cmap;
	  cmap = gimp_image_get_cmap(image_ID, &ncolors);
	  ncolors++; /* for transparency */
  	  colormap = g_new(XpmColor, ncolors);
	  cpp=(double)1.0+(double)log(ncolors)/(double)log(sizeof(linenoise)-1.0);
	  set_XpmImage(colormap, 0, "None");
	  for (i=0; i<ncolors-1; i++)
	  {
		  char *string;
		  guchar r, g, b;
		  r = *(cmap++);
		  g = *(cmap++);
		  b = *(cmap++);
		  string = g_new(char, 8);
	          sprintf(string, "#%02X%02X%02X", (int)r, (int)g, (int)b);
		  set_XpmImage(colormap, i+1, string);
	  } 
  }else
  {
	  colormap = g_new(XpmColor, ncolors);
	  
	  cpp=(double)1.0+(double)log(ncolors)/(double)log(sizeof(linenoise)-1.0);
	  set_XpmImage(colormap, 0, "None");

	  g_hash_table_foreach (hash, create_colormap_from_hash, colormap);
  }
    
  image = g_new0(XpmImage, 1);
  
  image->width=width;
  image->height=height;
  image->ncolors=ncolors;
  image->cpp=cpp;
  image->colorTable=colormap;	    
  image->data = ibuff;
  
      /* do the save */
      rc = xpm_write_file (filename, image);
      g_free (image);

  
 cleanup:

  /* clean up resources */  
  gimp_drawable_detach (drawable);
  
  
  if (ibuff) g_free(ibuff);
  /*if (mbuff) g_free(mbuff);*/
  if (hash)  g_hash_table_destroy(hash);
  
  return rc;
}


static gint
save_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *label;
  GtkWidget *button;
  GtkWidget *scale;
  GtkWidget *frame;
  GtkWidget *table;
  GtkAdjustment *scale_data;

  gtk_init ();

  dlg = gimp_dialog_new ("Save as Xpm");
  g_signal_connect (dlg, "destroy",
		    G_CALLBACK (save_close_callback),
		    NULL);

  /*  Action area  */
  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (save_ok_callback),
			  dlg, TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  /*  parameter settings  */
  frame = gtk_frame_new ("Parameter Settings");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);

  table = gimp_table_new (1, 2, FALSE);
  gimp_container_set_border_width (table, 10);
  gtk_frame_set_child (GTK_FRAME (frame), table);

  label = gtk_label_new ("Alpha Threshold");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 0, 1, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 5, 0);
  scale_data = gtk_adjustment_new (xpmvals.threshold, 0.0, 1.0, 0.01, 0.01, 0.0);
  scale = gimp_hscale_new (scale_data, 2);
  gtk_widget_set_size_request (scale, SCALE_WIDTH, -1);
  gimp_table_attach (table, scale, 1, 2, 0, 1, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);
  g_signal_connect (scale_data, "value-changed",
		    G_CALLBACK (save_scale_update),
		    &xpmvals.threshold);

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return xpmint.run;
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
  xpmint.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void
save_scale_update (GtkAdjustment *adjustment,
		   double        *scale_val)
{
  *scale_val = gtk_adjustment_get_value (adjustment);
}
