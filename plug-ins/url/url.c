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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include "libgimp/gimp.h"

/* Author: Josh MacDonald. */

/* The file used to be fetched by running wget.  It is now copied with
 * GIO, which handles every scheme it has a backend for (http, ftp, ...
 * through gvfs where that is installed).  When GIO cannot open the URI,
 * curl (shipped with Windows 10 and later) or wget is run instead.
 */

static void     query          (void);
static void     run            (char    *name,
				int      nparams,
				GParam  *param,
				int     *nreturn_vals,
				GParam **return_vals);
static gint32   load_image     (char    *filename);
static gboolean fetch_with_gio (const gchar *uri,
				const gchar *tmpname,
				GError     **error);
static gboolean fetch_with_tool (const gchar *uri,
				 const gchar *tmpname,
				 GError     **error);

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

  gimp_install_procedure ("file_url_load",
                          "loads files given a URL",
                          "Downloads with GIO; falls back on curl or GNU Wget when GIO cannot handle the URL.",
                          "Spencer Kimball & Peter Mattis",
                          "Spencer Kimball & Peter Mattis",
                          "1995-1997",
                          "<Load>/URL",
			  NULL,
                          PROC_PLUG_IN,
                          nload_args, nload_return_vals,
                          load_args, load_return_vals);

  gimp_register_load_handler ("file_url_load", "", "http:,ftp:");
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

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = STATUS_CALLING_ERROR;

  if (strcmp (name, "file_url_load") == 0)
    {
      image_ID = load_image (param[2].data.d_string);
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
  else
    g_assert (FALSE);
}

static gint32
load_image (char *filename)
{
  GParam* params;
  gint retvals;
  char* ext = strrchr (filename, '.');
  char* tmpname;
  GError *error = NULL;

  if (!ext || ext[1] == 0 || strchr(ext, '/'))
    {
      g_message ("url: can't open URL without an extension\n");
      return -1;
    }

  params = gimp_run_procedure ("gimp_temp_name",
			       &retvals,
			       PARAM_STRING, ext + 1,
			       PARAM_END);

  tmpname = g_strdup (params[1].data.d_string);
  gimp_destroy_params (params, retvals);

  if (! fetch_with_gio (filename, tmpname, &error))
    {
      GError *tool_error = NULL;

      if (! fetch_with_tool (filename, tmpname, &tool_error))
	{
	  g_message ("url: could not fetch URL %s: %s\n", filename,
		     tool_error ? tool_error->message :
		     (error ? error->message : "unknown error"));
	  g_clear_error (&tool_error);
	  g_clear_error (&error);
	  g_unlink (tmpname);
	  g_free (tmpname);
	  return -1;
	}
    }
  g_clear_error (&error);

  params = gimp_run_procedure ("gimp_file_load",
			       &retvals,
			       PARAM_INT32, 0,
			       PARAM_STRING, tmpname,
			       PARAM_STRING, tmpname,
			       PARAM_END);

  g_unlink (tmpname);
  g_free (tmpname);

  if (params[0].data.d_status == FALSE)
    return -1;
  else
    {
      gimp_image_set_filename (params[1].data.d_int32, NULL);
      return params[1].data.d_int32;
    }
}

static gboolean
fetch_with_gio (const gchar *uri,
		const gchar *tmpname,
		GError     **error)
{
  GFile    *src;
  GFile    *dest;
  gboolean  success;

  src  = g_file_new_for_uri (uri);
  dest = g_file_new_for_path (tmpname);

  success = g_file_copy (src, dest,
			 G_FILE_COPY_OVERWRITE | G_FILE_COPY_TARGET_DEFAULT_PERMS,
			 NULL, NULL, NULL, error);

  g_object_unref (src);
  g_object_unref (dest);

  return success;
}

/*  Runs curl, or wget when there is no curl, to download uri into
 *  tmpname.
 */
static gboolean
fetch_with_tool (const gchar *uri,
		 const gchar *tmpname,
		 GError     **error)
{
  GSubprocess *proc;
  gchar       *tool;

  tool = g_find_program_in_path ("curl");
  if (tool)
    {
      proc = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
			       G_SUBPROCESS_FLAGS_STDERR_SILENCE,
			       error,
			       tool, "-f", "-s", "-S", "-L",
			       "-o", tmpname, uri, NULL);
    }
  else
    {
      tool = g_find_program_in_path ("wget");
      if (! tool)
	{
	  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		       "neither GIO, curl nor wget can fetch this URL");
	  return FALSE;
	}

      proc = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
			       G_SUBPROCESS_FLAGS_STDERR_SILENCE,
			       error,
			       tool, "-q", uri, "-O", tmpname, NULL);
    }

  if (! proc)
    {
      g_free (tool);
      return FALSE;
    }

  if (! g_subprocess_wait_check (proc, NULL, error))
    {
      g_object_unref (proc);
      g_free (tool);
      return FALSE;
    }

  g_object_unref (proc);
  g_free (tool);

  return TRUE;
}
