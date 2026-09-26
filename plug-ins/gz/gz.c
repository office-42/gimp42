/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * Copyright (C) 1997 Daniel Risacher, magnus@alum.mit.edu
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

/* Minor changes to support file magic */
/* 4 Oct 1997 -- Risacher */

/* gzip plug-in for the gimp */
/* loosley based on url.c by */
/* Josh MacDonald, jmacd@cs.berkeley.edu */

/* and, very loosely on hrz.c by */
/* Albert Cahalan <acahalan at cs.uml.edu> */

/* This is reads and writes gziped image files for the Gimp
 *
 * It used to pipe the file through an external gzip; it now uses
 * zlib directly, so no gzip program is needed.
 *
 * It should work with file names of the form
 * filename.foo.gz where foo is some already-recognized extension
 *
 * and it also works for names of the form
 * filename.xcfgz - which is equivalent to
 * filename.xcf.gz
 *
 * I added the xcfgz bit because having a default extension of xcf.gz
 * can confuse the file selection dialog box somewhat, forcing the
 * user to type sometimes when he/she otherwise wouldn't need to.
 *
 * I later decided I didn't like it because I don't like to bloat
 * the file-extension namespace.  But I left in the recognition
 * feature/bug so if people want to use files named foo.xcfgz by
 * default, they can just hack their pluginrc file.
 *
 * to do this hack, change :
 *                      "xcf.gz,gz,xcfgz"
 * to
 *                      "xcfgz,gz,xcf.gz"
 *
 *
 * -Dan Risacher, 0430 CDT, 26 May 1997
 */


#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <zlib.h>
#include "libgimp/gimp.h"

#ifdef G_OS_WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

/* Author 1: Josh MacDonald (url.c) */
/* Author 2: Daniel Risacher (gz.c) */

/* According to USAF Lt Steve Werhle, US DoD software development
 * contracts average about $25 USD per source line of code (SLOC).  By
 * that metric, I figure this plug-in is worth about $10,000 USD */
/* But you got it free.   Magic of Gnu. */

static void   query      (void);
static void   run        (char    *name,
                          int      nparams,
                          GParam  *param,
                          int     *nreturn_vals,
                          GParam **return_vals);
static gint32 load_image (char *filename, gint32 run_mode);

static gint save_image (char   *filename,
			gint32  image_ID,
			gint32  drawable_ID,
			gint32 run_mode);

static int valid_file (char* filename) ;
static char* find_extension (char* filename);

static gboolean gz_compress   (const char *src,
			       const char *dest);
static gboolean gz_decompress (const char *src,
			       const char *dest);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
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
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image" },
    { PARAM_DRAWABLE, "drawable", "Drawable to save" },
    { PARAM_STRING, "filename", "The name of the file to save the image in" },
    { PARAM_STRING, "raw_filename", "The name of the file to save the image in" }
  };
  static int nsave_args = sizeof (save_args) / sizeof (save_args[0]);

  gimp_install_procedure ("file_gz_load",
                          "loads files compressed with gzip",
                          "The file is decompressed with zlib.",
                          "Daniel Risacher",
                          "Daniel Risacher, Spencer Kimball and Peter Mattis",
                          "1995-1997",
                          "<Load>/gzip",
			  NULL,
                          PROC_PLUG_IN,
                          nload_args, nload_return_vals,
                          load_args, load_return_vals);

  gimp_install_procedure ("file_gz_save",
                          "saves files compressed with gzip",
                          "The file is compressed with zlib.",
                          "Daniel Risacher",
                          "Daniel Risacher, Spencer Kimball and Peter Mattis",
                          "1995-1997",
                          "<Save>/gzip",
			  "RGB*, GRAY*, INDEXED*",
                          PROC_PLUG_IN,
                          nsave_args, 0,
                          save_args, NULL);
  gimp_register_magic_load_handler ("file_gz_load", "xcf.gz,gz,xcfgz", 
				    "", "0,string,\037\213");
  gimp_register_save_handler ("file_gz_save", "xcf.gz,gz,xcfgz", "");

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
  gint32 image_ID;

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = STATUS_CALLING_ERROR;

  if (strcmp (name, "file_gz_load") == 0)
    {
      image_ID = load_image (param[1].data.d_string,
			     param[0].data.d_int32);
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
	  g_assert (FALSE);
	}
    }
  else if (strcmp (name, "file_gz_save") == 0)
    {
      switch (run_mode)
	{
	case RUN_INTERACTIVE:
	  break;
	case RUN_NONINTERACTIVE:
	  /*  Make sure all the arguments are there!  */
	  if (nparams != 5)
	    status = STATUS_CALLING_ERROR;

	case RUN_WITH_LAST_VALS:
	  break;

	default:
	  break;
	}

      *nreturn_vals = 1;
      if (status != STATUS_SUCCESS)
	values[0].data.d_status = status;
      else if (save_image (param[3].data.d_string,
		      param[1].data.d_int32,
		      param[2].data.d_int32,
		      param[0].data.d_int32 ))
	{
	  values[0].data.d_status = STATUS_SUCCESS;
	}
      else
	values[0].data.d_status = STATUS_EXECUTION_ERROR;
    }
  else
    g_assert (FALSE);
}

static gint
save_image (char   *filename,
	    gint32  image_ID,
	    gint32  drawable_ID,
	    gint32  run_mode)
{
  GParam* params;
  gint retvals;
  char* ext;
  char* tmpname;

  ext = find_extension(filename);
  if (0 == *ext) {
    g_message("gz: no sensible extension, saving as gzip'd xcf\n");
    ext = ".xcf";
  }

  /* get a temp name with the right extension and save into it. */

  params = gimp_run_procedure ("gimp_temp_name",
			       &retvals,
			       PARAM_STRING, ext + 1,
			       PARAM_END);

  tmpname = params[1].data.d_string;

  params = gimp_run_procedure ("gimp_file_save",
			       &retvals,
 			       PARAM_INT32, run_mode,
			       PARAM_IMAGE, image_ID,
			       PARAM_DRAWABLE, drawable_ID,
			       PARAM_STRING, tmpname,
			       PARAM_STRING, tmpname,
			       PARAM_END);

  if (params[0].data.d_status == FALSE || !valid_file(tmpname)) {
    g_unlink (tmpname);
    return 0;
  }

  /* and gzip it into the file that was asked for */
  if (! gz_compress (tmpname, filename))
    {
      g_unlink (tmpname);
      return 0;
    }

  g_unlink (tmpname);

  return TRUE;
}

static gint32
load_image (char *filename, gint32 run_mode)
{
  GParam* params;
  gint retvals;
  char* ext;
  char* tmpname;

  ext = find_extension(filename);
  if (0 == *ext) {
    g_message("gz: no sensible extension, attempting to load with file magic\n");
  }

  /* find a temp name */
  params = gimp_run_procedure ("gimp_temp_name",
			       &retvals,
			       PARAM_STRING, *ext ? ext + 1 : ext,
			       PARAM_END);

  tmpname = params[1].data.d_string;

  /* un-gzip into the temp file */
  if (! gz_decompress (filename, tmpname))
    {
      g_unlink (tmpname);
      return -1;
    }

  /* now that we un-gziped it, load the temp file */

  params = gimp_run_procedure ("gimp_file_load",
			       &retvals,
			       PARAM_INT32, run_mode,
			       PARAM_STRING, tmpname,
			       PARAM_STRING, tmpname,
			       PARAM_END);

  g_unlink (tmpname);

  if (params[0].data.d_status == FALSE)
    return -1;
  else
    {
      gimp_image_set_filename (params[1].data.d_int32, filename);
      return params[1].data.d_int32;
    }
}


/* gzip src into dest, like "gzip -cf src > dest" */
static gboolean
gz_compress (const char *src,
	     const char *dest)
{
  FILE   *in;
  gzFile  out;
  int     fd;
  char    buf[16384];
  size_t  n;
  gboolean ok = TRUE;

  in = g_fopen (src, "rb");
  if (!in)
    {
      g_message ("gz: can't open %s: %s\n", src, g_strerror (errno));
      return FALSE;
    }

  fd = g_open (dest, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
  if (fd < 0 || !(out = gzdopen (fd, "wb")))
    {
      g_message ("gz: can't open %s: %s\n", dest, g_strerror (errno));
      if (fd >= 0)
	close (fd);
      fclose (in);
      return FALSE;
    }

  while ((n = fread (buf, 1, sizeof (buf), in)) > 0)
    {
      if (gzwrite (out, buf, (unsigned) n) != (int) n)
	{
	  ok = FALSE;
	  break;
	}
    }
  if (ferror (in))
    ok = FALSE;

  fclose (in);
  if (gzclose (out) != Z_OK)
    ok = FALSE;

  if (!ok)
    g_message ("gz: compressing %s failed\n", dest);

  return ok;
}

/* gunzip src into dest, like "gzip -cfd src > dest" */
static gboolean
gz_decompress (const char *src,
	       const char *dest)
{
  gzFile  in;
  FILE   *out;
  int     fd;
  char    buf[16384];
  int     n;
  gboolean ok = TRUE;

  fd = g_open (src, O_RDONLY | O_BINARY, 0);
  if (fd < 0 || !(in = gzdopen (fd, "rb")))
    {
      g_message ("gz: can't open %s: %s\n", src, g_strerror (errno));
      if (fd >= 0)
	close (fd);
      return FALSE;
    }

  out = g_fopen (dest, "wb");
  if (!out)
    {
      g_message ("gz: can't open %s: %s\n", dest, g_strerror (errno));
      gzclose (in);
      return FALSE;
    }

  while ((n = gzread (in, buf, sizeof (buf))) > 0)
    {
      if (fwrite (buf, 1, n, out) != (size_t) n)
	{
	  ok = FALSE;
	  break;
	}
    }
  if (n < 0)
    ok = FALSE;

  gzclose (in);
  if (fclose (out) != 0)
    ok = FALSE;

  if (!ok)
    g_message ("gz: decompressing %s failed\n", src);

  return ok;
}

static int valid_file (char* filename)
{
  int stat_res;
  GStatBuf buf;

  stat_res = g_stat(filename, &buf);

  if ((0 == stat_res) && (buf.st_size > 0))
    return 1;
  else
    return 0;
}

static char* find_extension (char* filename)
{
  char* filename_copy;
  char* ext;

  /* we never free this copy - aren't we evil! */
  filename_copy = malloc(strlen(filename)+1);
  strcpy(filename_copy, filename);

  /* find the extension, boy! */
  ext = strrchr (filename_copy, '.');

  while (1) {
    if (!ext || ext[1] == 0 || strchr(ext, '/') || strchr(ext, G_DIR_SEPARATOR))
      {
	return "";
      }
    if (0 == strcmp(ext, ".xcfgz")) {
      return ".xcf";  /* we've found it */
    }
    if (0 != strcmp(ext,".gz")) {
      return ext;
    } else {
      /* we found ".gz" so strip it, loop back, and look again */
      *ext = 0;
      ext = strrchr (filename_copy, '.');
    }
  }
}
