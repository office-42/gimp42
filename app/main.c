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
#include "config.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#include <glib.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include "libgimp/gimpfeatures.h"

#include "appenv.h"
#include "app_procs.h"
#include "errors.h"
#include "install.h"
#include "tile.h"

static RETSIGTYPE on_signal (int);
static void       init (void);

/* GLOBAL data */
int no_interface;
int no_data;
int no_splash;
int no_splash_image;
int be_verbose;
int use_shm;
int use_debug_handler;
int console_messages;

MessageHandlerType message_handler;

char *prog_name;		/* The path name we are invoked with */
char **batch_cmds;

/* LOCAL data */
static int gimp_argc;
static char **gimp_argv;

#ifdef G_OS_WIN32
/*  The GIMP is a Windows (not console) program, so it has no console
 *  of its own; when started from one, it writes there.
 */
static void
attach_parent_console (void)
{
  /*  Output already going somewhere (a pipe, a file) stays there.  */
  if (GetFileType (GetStdHandle (STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN)
    return;

  if (AttachConsole (ATTACH_PARENT_PROCESS))
    {
      freopen ("CONOUT$", "w", stdout);
      freopen ("CONOUT$", "w", stderr);
    }
}

/*  The plug-ins are programs in their own folder; the DLLs they share
 *  with the GIMP are next to gimp42.exe.  Putting that folder first on
 *  PATH lets them find those DLLs, since they inherit the environment.
 */
static void
add_program_folder_to_path (void)
{
  char *prefix = g_win32_get_package_installation_directory_of_module (NULL);
  char *bindir;
  const char *old_path;
  char *new_path;

  if (!prefix)
    return;

  bindir = g_build_filename (prefix, "bin", NULL);
  old_path = g_getenv ("PATH");
  new_path = old_path ? g_strconcat (bindir, G_SEARCHPATH_SEPARATOR_S, old_path, NULL)
		      : g_strdup (bindir);
  g_setenv ("PATH", new_path, TRUE);

  g_free (new_path);
  g_free (bindir);
  g_free (prefix);
}
#endif

static void
log_message_func (const gchar    *log_domain,
		  GLogLevelFlags  log_level,
		  const gchar    *message,
		  gpointer        data)
{
  message_func ((char *) message);
}

/*
 *  argv processing:
 *      Arguments are either switches, their associated
 *      values, or image files.  As switches and their
 *      associated values are processed, those slots in
 *      the argv[] array are NULLed. We do this because
 *      unparsed args are treated as images to load on
 *      startup.
 *
 *      The general GIMP switches are processed first.  Any args
 *      left are assumed to be image files the GIMP should
 *      display.
 *
 *      The exception is the batch switch.  When this is
 *      encountered, all remaining args are treated as batch
 *      commands.
 */

int
main (int argc, char **argv)
{
  int show_version;
  int show_help;
  int i, j;

#ifdef G_OS_WIN32
  attach_parent_console ();
  add_program_folder_to_path ();
#endif

  /* Initialize variables */
  prog_name = argv[0];

  setlocale (LC_ALL, "");
  setlocale (LC_NUMERIC, "C");  /* must use dot, not comma, as decimal separator */

  no_interface = FALSE;
  no_data = FALSE;
  no_splash = FALSE;
  no_splash_image = FALSE;
  use_shm = FALSE;
  use_debug_handler = FALSE;
  console_messages = FALSE;

  message_handler = CONSOLE;

  batch_cmds = g_new (char*, argc);
  batch_cmds[0] = NULL;

  show_version = FALSE;
  show_help = FALSE;

  for (i = 1; i < argc; i++)
    {
      if ((strcmp (argv[i], "--no-interface") == 0) ||
	  (strcmp (argv[i], "-n") == 0))
	{
	  no_interface = TRUE;
	  argv[i] = NULL;
	}
      else if ((strcmp (argv[i], "--batch") == 0) ||
	       (strcmp (argv[i], "-b") == 0))
	{
	  argv[i] = NULL;
	  for (j = 0, i++ ; i < argc; j++, i++)
	    {
	      batch_cmds[j] = argv[i];
	      argv[i] = NULL;
	    }
	  batch_cmds[j] = NULL;

	  if (batch_cmds[0] == NULL)  /* We need at least one batch command */
	    show_help = TRUE;
	}
      else if ((strcmp (argv[i], "--help") == 0) ||
	       (strcmp (argv[i], "-h") == 0))
	{
	  show_help = TRUE;
	  argv[i] = NULL;
	}
      else if (strcmp (argv[i], "--version") == 0 ||
	       strcmp (argv[i], "-v") == 0)
	{
	  show_version = TRUE;
	  argv[i] = NULL;
	}
      else if (strcmp (argv[i], "--no-data") == 0)
	{
	  no_data = TRUE;
	  argv[i] = NULL;
	}
      else if (strcmp (argv[i], "--no-splash") == 0)
	{
	  no_splash = TRUE;
	  argv[i] = NULL;
	}
      else if (strcmp (argv[i], "--no-splash-image") == 0)
	{
	  no_splash_image = TRUE;
	  argv[i] = NULL;
	}
      else if (strcmp (argv[i], "--verbose") == 0)
	{
	  be_verbose = TRUE;
	  argv[i] = NULL;
	}
      else if (strcmp (argv[i], "--no-shm") == 0)
	{
	  /*  there is no shared memory any more; accepted for scripts  */
	  argv[i] = NULL;
	}
      else if (strcmp (argv[i], "--debug-handlers") == 0)
	{
	  use_debug_handler = TRUE;
	  argv[i] = NULL;
	}
      else if (strcmp (argv[i], "--console-messages") == 0)
	{
	  console_messages = TRUE;
	  argv[i] = NULL;
	}
/*
 *    ANYTHING ELSE starting with a '-' is an error.
 */
      else if (argv[i][0] == '-')
	{
	  show_help = TRUE;
	}
    }

  if (show_version)
    g_print ("GIMP version " GIMP_VERSION "\n");

  if (show_help)
    {
      g_print ("Usage: %s [option ...] [files ...]\n", argv[0]);
      g_print ("Valid options are:\n");
      g_print ("  -h --help              Output this help.\n");
      g_print ("  -v --version           Output version info.\n");
      g_print ("  -b --batch <commands>  Run in batch mode.\n");
      g_print ("  -n --no-interface      Run without a user interface.\n");
      g_print ("  --no-data              Do not load patterns, gradients, palettes, brushes.\n");
      g_print ("  --verbose              Show startup messages.\n");
      g_print ("  --no-splash            Do not show the startup window.\n");
      g_print ("  --no-splash-image      Do not add an image to the startup window.\n");
      g_print ("  --console-messages     Display warnings to console instead of a dialog box.\n");
      g_print ("  --debug-handlers       Enable debugging signal handlers.\n\n");
    }

  if (show_version || show_help)
    exit (0);

  /* Initialize the GTK toolkit */
  if (!no_interface)
    {
      if (!gtk_init_check ())
	{
	  g_printerr ("%s: cannot open a display; try --no-interface\n",
		      prog_name);
	  exit (1);
	}

      /*  gtk_init set the locale from the system; numbers in gimprc
       *  and elsewhere are read with a '.' decimal point.
       */
      setlocale (LC_NUMERIC, "C");
    }

  g_log_set_handler (NULL, G_LOG_LEVEL_MESSAGE, log_message_func, NULL);

  /* Handle some signals */
  signal (SIGINT, on_signal);
  signal (SIGABRT, on_signal);
  signal (SIGSEGV, on_signal);
  signal (SIGTERM, on_signal);
  signal (SIGFPE, on_signal);
#ifndef G_OS_WIN32
  signal (SIGHUP, on_signal);
  signal (SIGQUIT, on_signal);
  signal (SIGBUS, on_signal);
  signal (SIGPIPE, SIG_IGN);
#endif

  /* Keep the command line arguments--for use in gimp_init */
  gimp_argc = argc - 1;
  gimp_argv = argv + 1;

  /* Check the installation */
  install_verify (init);

  /* Main application loop */
  if (!app_exit_finish_done ())
    gimp_main_loop_run ();

  return 0;
}

static void
init ()
{
  /*  Continue initializing  */
  gimp_init (gimp_argc, gimp_argv);
}

static int caught_fatal_sig = 0;

static RETSIGTYPE
on_signal (int sig_num)
{
  if (caught_fatal_sig)
    {
      signal (sig_num, SIG_DFL);
      raise (sig_num);
    }
  caught_fatal_sig = 1;

  switch (sig_num)
    {
#ifndef G_OS_WIN32
    case SIGHUP:
      terminate ("sighup caught");
      break;
    case SIGQUIT:
      terminate ("sigquit caught");
      break;
    case SIGBUS:
      fatal_error ("sigbus caught");
      break;
#endif
    case SIGINT:
      terminate ("sigint caught");
      break;
    case SIGABRT:
      terminate ("sigabrt caught");
      break;
    case SIGSEGV:
      fatal_error ("sigsegv caught");
      break;
    case SIGTERM:
      terminate ("sigterm caught");
      break;
    case SIGFPE:
      fatal_error ("sigfpe caught");
      break;
    default:
      fatal_error ("unknown signal");
      break;
    }
}
