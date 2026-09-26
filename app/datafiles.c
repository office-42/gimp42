/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * Datafiles module copyight (C) 1996 Federico Mena Quintero
 * federico@nuclecu.unam.mx
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
#include <sys/types.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib/gstdio.h>
#include "datafiles.h"
#include "errors.h"
#include "general.h"
#include "gimprc.h"


/***** Functions *****/

/*  A search path from gimprc: folders separated by the platform's
 *  separator (';' on Windows) or by ':', the separator the GIMP always
 *  used.  A ':' that is the colon of a drive letter ("C:\...") does not
 *  separate anything.  A leading '~' is the home folder.
 */
GList *
datafiles_parse_path (const char *path_str)
{
  GList *list = NULL;
  const char *start;
  const char *p;

  if (!path_str)
    return NULL;

  start = p = path_str;
  while (TRUE)
    {
      gboolean end = (*p == '\0');
      gboolean sep = (*p == G_SEARCHPATH_SEPARATOR);

      if (*p == ':' && !sep)
	{
	  /*  a drive letter: exactly one letter since the last separator,
	   *  followed by a slash
	   */
	  gboolean drive = ((p - start) == 1 &&
			    g_ascii_isalpha (start[0]) &&
			    (p[1] == '/' || p[1] == '\\'));
	  sep = !drive;
	}

      if (end || sep)
	{
	  if (p > start)
	    {
	      char *dir = g_strndup (start, p - start);

	      if (dir[0] == '~')
		{
		  char *expanded = g_build_filename (g_get_home_dir (),
						     dir + 1, NULL);
		  g_free (dir);
		  dir = expanded;
		}

	      list = g_list_append (list, dir);
	    }

	  if (end)
	    break;

	  start = p + 1;
	}

      p++;
    }

  return list;
}

void
datafiles_free_path (GList *path)
{
  g_list_free_full (path, g_free);
}

static int filestat_valid = 0;
static GStatBuf filestat;

static gboolean
datafiles_is_executable (const char *filename)
{
#ifdef G_OS_WIN32
  /*  Windows marks nothing executable; programs are known by name.  */
  const char *ext = strrchr (filename, '.');

  return (ext && (g_ascii_strcasecmp (ext, ".exe") == 0 ||
		  g_ascii_strcasecmp (ext, ".com") == 0));
#else
  return g_file_test (filename, G_FILE_TEST_IS_EXECUTABLE);
#endif
}

void
datafiles_read_directories (char *path_str,
			    datafile_loader_t loader_func,
			    int flags)
{
  GList *path;
  GList *list;

  if (path_str == NULL)
    return;

  path = datafiles_parse_path (path_str);

  /* Search through all directories in the path */
  for (list = path; list; list = list->next)
    {
      const char *dirname = list->data;
      const char *name;
      GDir *dir;

      if (!g_file_test (dirname, G_FILE_TEST_IS_DIR))
	continue;

      dir = g_dir_open (dirname, 0, NULL);
      if (!dir)
	{
	  g_message ("error reading datafiles directory \"%s\"", dirname);
	  continue;
	}

      while ((name = g_dir_read_name (dir)))
	{
	  char *filename = g_build_filename (dirname, name, NULL);

	  /* Check the file and see that it is not a sub-directory */
	  if (g_stat (filename, &filestat) == 0 &&
	      S_ISREG (filestat.st_mode) &&
	      (!(flags & MODE_EXECUTABLE) || datafiles_is_executable (filename)))
	    {
	      filestat_valid = 1;
	      (*loader_func) (filename);
	      filestat_valid = 0;
	    }

	  g_free (filename);
	}

      g_dir_close (dir);
    }

  datafiles_free_path (path);
} /* datafiles_read_directories */

time_t
datafile_atime ()
{
  if (filestat_valid)
    return filestat.st_atime;
  return 0;
}

time_t
datafile_mtime ()
{
  if (filestat_valid)
    return filestat.st_mtime;
  return 0;
}

time_t
datafile_ctime ()
{
  if (filestat_valid)
    return filestat.st_ctime;
  return 0;
}
