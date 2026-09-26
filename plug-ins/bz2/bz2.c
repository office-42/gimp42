/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * Copyright (C) 1997 Daniel Risacher
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

/* bzip2 plug-in for the gimp */
/* this is almost exactly the same as the gz(ip) plugin by */
/* Dan Risacher & Josh, so feel free to go look there. */
/* GZ plugin adapted to BZ2 by Adam. I've left all other */
/* Error checking added by srn. */
/* credits intact since it was only a super-wussy mod. */

/* This is reads and writes bzip2ed image files for the Gimp
 *
 * You need to have bzip2 installed for it to work.
 *
 * It should work with file names of the form
 * filename.foo.bz2 where foo is some already-recognized extension
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#ifdef HAVE_LIBBZ2
#include <bzlib.h>
#else
#include <gio/gio.h>
#endif
#include "libgimp/gimp.h"


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
static char* find_extension (char* filename) ;
static gboolean bz2_compress   (const char *src, const char *dest);
static gboolean bz2_decompress (const char *src, const char *dest);

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

  gimp_install_procedure ("file_bz2_load",
                          "loads files compressed with bzip2",
                          "You need to have bzip2 installed.",
                          "Daniel Risacher",
                          "Daniel Risacher, Spencer Kimball and Peter Mattis",
                          "1995-1997",
                          "<Load>/bzip2",
			  NULL,
                          PROC_PLUG_IN,
                          nload_args, nload_return_vals,
                          load_args, load_return_vals);

  gimp_install_procedure ("file_bz2_save",
                          "saves files compressed with bzip2",
                          "You need to have bzip2 installed",
                          "Daniel Risacher",
                          "Daniel Risacher, Spencer Kimball and Peter Mattis",
                          "1995-1997",
                          "<Save>/bzip2",
			  "RGB*, GRAY*, INDEXED*",
                          PROC_PLUG_IN,
                          nsave_args, 0,
                          save_args, NULL);

  gimp_register_load_handler ("file_bz2_load", "xcf.bz2,bz2,xcfbz2", "");
  gimp_register_save_handler ("file_bz2_save", "xcf.bz2,bz2,xcfbz2", "");

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

  if (strcmp (name, "file_bz2_load") == 0)
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
  else if (strcmp (name, "file_bz2_save") == 0)
    {
      switch (run_mode)
	{
	case RUN_INTERACTIVE:
	  break;
	case RUN_NONINTERACTIVE:
	  /*  Make sure all the arguments are there!  */
	  if (nparams != 4)
	    status = STATUS_CALLING_ERROR;

	case RUN_WITH_LAST_VALS:
	  break;

	default:
	  break;
	}

      *nreturn_vals = 1;
      if (save_image (param[3].data.d_string,
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

  if (NULL == (ext = find_extension(filename))) return FALSE;

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
    return FALSE;
  }

/*   if (! file_save(image_ID, tmpname, tmpname)) { */
/*     g_unlink (tmpname); */
/*     return -1; */
/*   } */

  /* and bzip2 it into the real file */
  if (! bz2_compress (tmpname, filename))
    {
      g_message ("bz2: could not compress %s into %s\n", tmpname, filename);
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

  if (NULL == (ext = find_extension(filename))) return -1;

  /* find a temp name */
  params = gimp_run_procedure ("gimp_temp_name",
			       &retvals,
			       PARAM_STRING, ext + 1,
			       PARAM_END);

  tmpname = params[1].data.d_string;

  /* un-bzip2 into it */
  if (! bz2_decompress (filename, tmpname))
    {
      g_message ("bz2: could not decompress %s\n", filename);
      g_unlink (tmpname);
      return -1;
    }

  /* now that we un-bzip2ed it, load the temp file */

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
    if (!ext || ext[1] == 0 || strchr(ext, '/') || strchr(ext, '\\'))
      {
	g_message ("bz2: can't open bzip2ed file without a sensible extension\n");
	return NULL;
      }
    if (0 == strcmp(ext, ".xcfbz2")) {
      return ".xcf";  /* we've found it */
    }
    if (0 != strcmp(ext,".bz2")) {
      /* the extension becomes part of a temp file name: letters and
	 digits only (no ':' for a Windows stream name and the like) */
      const char *p;

      for (p = ext + 1; *p; p++)
	if (!g_ascii_isalnum (*p))
	  {
	    g_message ("bz2: can't open bzip2ed file without a sensible extension\n");
	    return NULL;
	  }
      return ext;
    } else {
      /* we found ".bz2" so strip it, loop back, and look again */
      *ext = 0;
      ext = strrchr (filename_copy, '.');
    }
  }
}

/* The original forked "bzip2 -cf" / "bzip2 -cfd" with stdout redirected
 * into the destination file.  With libbz2 the (de)compression happens in
 * this process, so nothing has to be installed and nothing is spawned;
 * without it the bzip2 program is run through GSubprocess.
 */

#ifdef HAVE_LIBBZ2

static gboolean
bz2_compress (const char *src,
	      const char *dest)
{
  FILE    *in;
  FILE    *out;
  BZFILE  *bz;
  int      bzerror;
  char     buf[16384];
  size_t   n;
  gboolean ok = TRUE;

  if (! (in = g_fopen (src, "rb")))
    {
      g_message ("bz2: can't open %s: %s\n", src, g_strerror (errno));
      return FALSE;
    }
  if (! (out = g_fopen (dest, "wb")))
    {
      g_message ("bz2: can't open %s: %s\n", dest, g_strerror (errno));
      fclose (in);
      return FALSE;
    }

  bz = BZ2_bzWriteOpen (&bzerror, out, 9, 0, 0);
  if (bzerror != BZ_OK)
    ok = FALSE;

  while (ok && (n = fread (buf, 1, sizeof (buf), in)) > 0)
    {
      BZ2_bzWrite (&bzerror, bz, buf, (int) n);
      if (bzerror != BZ_OK)
	ok = FALSE;
    }
  if (ferror (in))
    ok = FALSE;

  if (bz)
    BZ2_bzWriteClose (&bzerror, bz, ! ok, NULL, NULL);
  if (bzerror != BZ_OK)
    ok = FALSE;

  fclose (in);
  if (fclose (out) != 0)
    ok = FALSE;

  return ok;
}

static gboolean
bz2_decompress (const char *src,
		const char *dest)
{
  FILE    *in;
  FILE    *out;
  BZFILE  *bz;
  int      bzerror = BZ_OK;
  char     buf[16384];
  int      n;
  gboolean ok = TRUE;

  if (! (in = g_fopen (src, "rb")))
    {
      g_message ("bz2: can't open %s: %s\n", src, g_strerror (errno));
      return FALSE;
    }
  if (! (out = g_fopen (dest, "wb")))
    {
      g_message ("bz2: can't open %s: %s\n", dest, g_strerror (errno));
      fclose (in);
      return FALSE;
    }

  /*  Like bzip2 -d, handle files made of several concatenated streams.  */
  while (ok)
    {
      void *unused;
      int   nunused;

      bz = BZ2_bzReadOpen (&bzerror, in, 0, 0, NULL, 0);
      if (bzerror != BZ_OK)
	{
	  ok = FALSE;
	  break;
	}

      do
	{
	  n = BZ2_bzRead (&bzerror, bz, buf, sizeof (buf));
	  if ((bzerror == BZ_OK || bzerror == BZ_STREAM_END) && n > 0)
	    if (fwrite (buf, 1, n, out) != (size_t) n)
	      ok = FALSE;
	}
      while (ok && bzerror == BZ_OK);

      if (bzerror != BZ_STREAM_END)
	{
	  ok = FALSE;
	  BZ2_bzReadClose (&bzerror, bz);
	  break;
	}

      BZ2_bzReadGetUnused (&bzerror, bz, &unused, &nunused);
      BZ2_bzReadClose (&bzerror, bz);

      if (nunused == 0)
	{
	  int c = fgetc (in);

	  if (c == EOF)
	    break;
	  ungetc (c, in);
	}
      else
	{
	  /*  Put the bytes read past the end of the stream back.  */
	  if (fseek (in, -(long) nunused, SEEK_CUR) != 0)
	    ok = FALSE;
	}
    }

  fclose (in);
  if (fclose (out) != 0)
    ok = FALSE;

  return ok;
}

#else  /* ! HAVE_LIBBZ2 */

static gboolean
bz2_run (const char *flags,
	 const char *src,
	 const char *dest)
{
  GSubprocess       *proc;
  GFile             *file;
  GFileOutputStream *out = NULL;
  GError            *error = NULL;
  gchar             *bzip2;
  gboolean           ok = FALSE;

  bzip2 = g_find_program_in_path ("bzip2");
  if (! bzip2)
    {
      g_message ("bz2: the bzip2 program was not found in the PATH\n");
      return FALSE;
    }

  /*  bzip2 writes to its stdout, which is copied into dest (setting a
   *  subprocess's stdout to a file path only exists on Unix).
   */
  /*  "--": a file name starting with '-' is not an option  */
  proc = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE, &error,
			   bzip2, flags, "--", src, NULL);
  if (proc)
    {
      file = g_file_new_for_path (dest);
      out = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE,
			    NULL, &error);
      g_object_unref (file);

      if (out &&
	  g_output_stream_splice (G_OUTPUT_STREAM (out),
				  g_subprocess_get_stdout_pipe (proc),
				  G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
				  G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
				  NULL, &error) >= 0 &&
	  g_subprocess_wait_check (proc, NULL, &error))
	ok = TRUE;

      if (! out)
	g_subprocess_force_exit (proc);
      g_clear_object (&out);
      g_object_unref (proc);
    }

  if (! ok)
    {
      g_message ("bz2: bzip2 failed on %s: %s\n", src,
		 error ? error->message : "unknown error");
      g_clear_error (&error);
    }

  g_free (bzip2);

  return ok;
}

static gboolean
bz2_compress (const char *src,
	      const char *dest)
{
  return bz2_run ("-cf", src, dest);
}

static gboolean
bz2_decompress (const char *src,
		const char *dest)
{
  return bz2_run ("-cfd", src, dest);
}

#endif /* HAVE_LIBBZ2 */
