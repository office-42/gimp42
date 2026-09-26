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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <glib/gstdio.h>
#include <gio/gio.h>

#include "appenv.h"
#include "actionarea.h"
#include "install.h"
#include "interface.h"
#include "gimprc.h"


static void install_run (InstallCallback);
static int  install_silently (void);
static void install_help (InstallCallback);
static void help_install_callback (GtkWidget *, gpointer);
static void help_ignore_callback (GtkWidget *, gpointer);
static void help_quit_callback (GtkWidget *, gpointer);
static void install_continue_callback (GtkWidget *, gpointer);
static void install_quit_callback (GtkWidget *, gpointer);

static GtkWidget *help_widget;
static GtkWidget *install_widget;

/*  The installation dialogs cannot be closed from the window manager:
 *  one of their buttons has to be used.
 */
static gboolean
install_close_request (GtkWindow *window,
		       gpointer   data)
{
  return TRUE;
}

/*  The callback travels through the action area's gpointer user data.  */
typedef struct
{
  InstallCallback callback;
} InstallData;

static InstallData install_data;

void
install_verify (InstallCallback install_callback)
{
  int properly_installed = TRUE;
  char *filename;

  filename = gimp_directory ();
  if ('\000' == filename[0])
    {
      g_message ("No home directory--skipping GIMP user installation.");
      (* install_callback) ();
      return;
    }

  if (! g_file_test (filename, G_FILE_TEST_EXISTS))
    properly_installed = FALSE;

  /*  If there is already a proper installation, invoke the callback.
   *  Otherwise set up the user's folder right away: everybody wants
   *  it, so there is nothing to ask.  Only when that fails does the
   *  old dialog appear, to explain and show the log.
   */
  if (properly_installed || install_silently ())
    {
      (* install_callback) ();
    }
  else if (no_interface)
    {
      g_print ("The GIMP could not set up %s for the current user\n", filename);

      (* install_callback) ();
    }
  else
    {
      install_help (install_callback);
    }
}


/*********************/
/*  Local functions  */

/*  A read-only text view in a scrolled window, with the "strong" and
 *  "emphasis" tags the old fonts stood for.
 */
static GtkWidget *
install_text_view_new (GtkWidget      *dialog,
		       int             width,
		       int             height,
		       GtkTextBuffer **buffer)
{
  GtkWidget *scrolled;
  GtkWidget *text;

  scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_ALWAYS);
  gtk_widget_set_size_request (scrolled, width, height);
  gimp_container_set_border_width (scrolled, 2);
  gimp_box_pack_start (gimp_dialog_get_vbox (dialog), scrolled, TRUE, TRUE, 0);

  text = gtk_text_view_new ();
  gtk_text_view_set_editable (GTK_TEXT_VIEW (text), FALSE);
  gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (text), FALSE);
  gtk_text_view_set_left_margin (GTK_TEXT_VIEW (text), 4);
  gtk_text_view_set_right_margin (GTK_TEXT_VIEW (text), 4);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), text);

  *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text));
  gtk_text_buffer_create_tag (*buffer, "strong",
			      "weight", PANGO_WEIGHT_BOLD,
			      "scale", PANGO_SCALE_LARGE,
			      NULL);
  gtk_text_buffer_create_tag (*buffer, "emphasis",
			      "style", PANGO_STYLE_ITALIC,
			      NULL);

  return text;
}

/*  Appends text, with tag or plain when tag is NULL.  */
static void
install_text_insert (GtkTextBuffer *buffer,
		     const char    *tag,
		     const char    *text)
{
  GtkTextIter end;

  /*  Without a log window (the automatic installation) only problems
   *  are worth reporting, and they go to the console.
   */
  if (!buffer)
    {
      if (tag)
	g_printerr ("%s", text);
      return;
    }

  gtk_text_buffer_get_end_iter (buffer, &end);

  if (tag)
    gtk_text_buffer_insert_with_tags_by_name (buffer, &end, text, -1,
					      tag, NULL);
  else
    gtk_text_buffer_insert (buffer, &end, text, -1);
}

typedef struct
{
  const char *tag;     /*  "strong", "emphasis" or NULL                  */
  const char *text;    /*  NULL: the name of the user's GIMP directory  */
} HelpText;

static const HelpText help_text[] =
{
  { "strong", "The GIMP - GNU Image Manipulation Program\n\n" },
  { "emphasis", "Copyright (C) 1995 Spencer Kimball and Peter Mattis\n" },
  { NULL, "\n" },
  { NULL, "This program is free software; you can redistribute it and/or modify\n" },
  { NULL, "it under the terms of the GNU General Public License as published by\n" },
  { NULL, "the Free Software Foundation; either version 2 of the License, or\n" },
  { NULL, "(at your option) any later version.\n" },
  { NULL, "\n" },
  { NULL, "This program is distributed in the hope that it will be useful,\n" },
  { NULL, "but WITHOUT ANY WARRANTY; without even the implied warranty of\n" },
  { NULL, "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n" },
  { NULL, "See the GNU General Public License for more details.\n" },
  { NULL, "\n" },
  { NULL, "You should have received a copy of the GNU General Public License\n" },
  { NULL, "along with this program; if not, write to the Free Software\n" },
  { NULL, "Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.\n" },
  { NULL, "\n\n" },

  { "strong", "Personal GIMP Installation\n\n" },
  { NULL, "For a proper GIMP installation, a subdirectory called\n" },
  { "emphasis", NULL },
  { NULL, " needs to be created.  This\n" },
  { NULL, "subdirectory will contain a number of important files:\n\n" },
  { "emphasis", "gimprc\n" },
  { NULL, "\t\tThe gimprc is used to store personal preferences\n" },
  { NULL, "\t\tsuch as default GIMP behaviors & plug-in hotkeys.\n" },
  { NULL, "\t\tPaths to search for brushes, palettes, gradients\n" },
  { NULL, "\t\tpatterns, and plug-ins are also configured here.\n" },
  { "emphasis", "pluginrc\n" },
  { NULL, "\t\tPlug-ins and extensions are extern programs run by\n" },
  { NULL, "\t\tthe GIMP which provide additional functionality.\n" },
  { NULL, "\t\tThese programs are searched for at run-time and\n" },
  { NULL, "\t\tinformation about their functionality and mod-times\n" },
  { NULL, "\t\tis cached in this file.  This file is intended to\n" },
  { NULL, "\t\tbe GIMP-readable only, and should not be edited.\n" },
  { "emphasis", "brushes\n" },
  { NULL, "\t\tThis is a subdirectory which can be used to store\n" },
  { NULL, "\t\tuser defined brushes.  The default gimprc file\n" },
  { NULL, "\t\tchecks this subdirectory in addition to the system-\n" },
  { NULL, "\t\twide gimp brushes installation when searching for\n" },
  { NULL, "\t\tbrushes.\n" },
  { "emphasis", "gradients\n" },
  { NULL, "\t\tThis is a subdirectory which can be used to store\n" },
  { NULL, "\t\tuser defined gradients.  The default gimprc file\n" },
  { NULL, "\t\tchecks this subdirectory in addition to the system-\n" },
  { NULL, "\t\twide gimp gradients installation when searching for\n" },
  { NULL, "\t\tgradients.\n" },
  { "emphasis", "gfig\n" },
  { NULL, "\t\tThis is a subdirectory which can be used to store\n" },
  { NULL, "\t\tuser defined figures to be used by the gfig plug-in.\n" },
  { NULL, "\t\tThe default gimprc file checks this subdirectory in\n" },
  { NULL, "\t\taddition to the systemwide gimp gfig installation\n" },
  { NULL, "\t\twhen searching for gfig figures.\n" },
  { "emphasis", "gflares\n" },
  { NULL, "\t\tThis is a subdirectory which can be used to store\n" },
  { NULL, "\t\tuser defined gflares to be used by the gflare plug-in.\n" },
  { NULL, "\t\tThe default gimprc file checks this subdirectory in\n" },
  { NULL, "\t\taddition to the systemwide gimp gflares installation\n" },
  { NULL, "\t\twhen searching for gflares.\n" },
  { "emphasis", "palettes\n" },
  { NULL, "\t\tThis is a subdirectory which can be used to store\n" },
  { NULL, "\t\tuser defined palettes.  The default gimprc file\n" },
  { NULL, "\t\tchecks only this subdirectory (not the system-wide\n" },
  { NULL, "\t\tinstallation) when searching for palettes.  During\n" },
  { NULL, "\t\tinstallation, the system palettes will be copied\n" },
  { NULL, "\t\there.  This is done to allow modifications made to\n" },
  { NULL, "\t\tpalettes during GIMP execution to persist across\n" },
  { NULL, "\t\tsessions.\n" },
  { "emphasis", "patterns\n" },
  { NULL, "\t\tThis is a subdirectory which can be used to store\n" },
  { NULL, "\t\tuser defined patterns.  The default gimprc file\n" },
  { NULL, "\t\tchecks this subdirectory in addition to the system-\n" },
  { NULL, "\t\twide gimp patterns installation when searching for\n" },
  { NULL, "\t\tpatterns.\n" },
  { "emphasis", "plug-ins\n" },
  { NULL, "\t\tThis is a subdirectory which can be used to store\n" },
  { NULL, "\t\tuser created, temporary, or otherwise non-system-\n" },
  { NULL, "\t\tsupported plug-ins.  The default gimprc file\n" },
  { NULL, "\t\tchecks this subdirectory in addition to the system-\n" },
  { NULL, "\t\twide GIMP plug-in directories when searching for\n" },
  { NULL, "\t\tplug-ins.\n" },
  { "emphasis", "scripts\n" },
  { NULL, "\t\tThis subdirectory is used by the GIMP to store \n" },
  { NULL, "\t\tuser created and installed scripts. The default gimprc\n" },
  { NULL, "\t\tfile checks this subdirectory in addition to the system\n" },
  { NULL, "\t\t-wide gimp scripts subdirectory when searching for scripts\n" },
  { "emphasis", "tmp\n" },
  { NULL, "\t\tThis subdirectory is used by the GIMP to temporarily\n" },
  { NULL, "\t\tstore undo buffers to reduce memory usage.  If GIMP is\n" },
  { NULL, "\t\tunceremoniously killed, files may persist in this directory\n" },
  { NULL, "\t\tof the form: gimp<#>.<#>.  These files are useless across\n" },
  { NULL, "\t\tGIMP sessions and can be destroyed with impunity.\n" }
};

static void
install_help (InstallCallback callback)
{
  static ActionAreaItem action_items[] =
  {
    { "Install", help_install_callback, NULL, NULL },
    { "Ignore", help_ignore_callback, NULL, NULL },
    { "Quit", help_quit_callback, NULL, NULL }
  };
  GtkTextBuffer *buffer;
  int i;

  install_data.callback = callback;

  help_widget = gimp_dialog_new ("GIMP Installation");
  g_signal_connect (help_widget, "close-request",
		    G_CALLBACK (install_close_request),
		    NULL);

  action_items[0].user_data = &install_data;
  action_items[1].user_data = &install_data;
  action_items[2].user_data = &install_data;
  build_action_area (help_widget, action_items, 3, 0);

  install_text_view_new (help_widget, 450, 475, &buffer);

  for (i = 0; i < (int) G_N_ELEMENTS (help_text); i++)
    install_text_insert (buffer, help_text[i].tag,
			 help_text[i].text ? help_text[i].text
					   : gimp_directory ());

  gtk_window_present (GTK_WINDOW (help_widget));
}

static void
help_install_callback (GtkWidget *w,
		       gpointer   client_data)
{
  InstallCallback callback;

  callback = ((InstallData *) client_data)->callback;
  gtk_window_destroy (GTK_WINDOW (help_widget));
  install_run (callback);
}

static void
help_ignore_callback (GtkWidget *w,
		      gpointer   client_data)
{
  InstallCallback callback;

  callback = ((InstallData *) client_data)->callback;
  gtk_window_destroy (GTK_WINDOW (help_widget));
  (* callback) ();
}

static void
help_quit_callback (GtkWidget *w,
		    gpointer   client_data)
{
  gtk_window_destroy (GTK_WINDOW (help_widget));
  exit (0);
}


/*  The user installation.  This used to be the shell script
 *  DATADIR/user_install; it is done here so that it works where there
 *  is no shell.  Every step is logged the way the script echoed it.
 */

static int
install_mkdir (GtkTextBuffer *log,
	       const char    *dir,
	       gboolean       with_parents)
{
  char *msg;
  int err;

  msg = g_strdup_printf ("mkdir %s\n", dir);
  install_text_insert (log, NULL, msg);
  g_free (msg);

  if (with_parents)
    err = g_mkdir_with_parents (dir, 0755);
  else
    err = g_mkdir (dir, 0755);

  if (err != 0 && ! g_file_test (dir, G_FILE_TEST_IS_DIR))
    {
      msg = g_strdup_printf ("  %s: %s\n", dir, g_strerror (errno));
      install_text_insert (log, "emphasis", msg);
      g_free (msg);
      return FALSE;
    }

  return TRUE;
}

static int
install_copy (GtkTextBuffer *log,
	      const char    *src,
	      const char    *dest)
{
  GFile *src_file;
  GFile *dest_file;
  GError *error = NULL;
  char *msg;
  int success;

  msg = g_strdup_printf ("cp %s %s\n", src, dest);
  install_text_insert (log, NULL, msg);
  g_free (msg);

  src_file = g_file_new_for_path (src);
  dest_file = g_file_new_for_path (dest);

  success = g_file_copy (src_file, dest_file, G_FILE_COPY_OVERWRITE,
			 NULL, NULL, NULL, &error);
  if (! success)
    {
      msg = g_strdup_printf ("  %s\n", error->message);
      install_text_insert (log, "emphasis", msg);
      g_free (msg);
      g_error_free (error);
    }

  g_object_unref (src_file);
  g_object_unref (dest_file);

  return success;
}

/*  Copies the files in src_dir (not its subdirectories) into dest_dir.  */
static void
install_copy_dir_files (GtkTextBuffer *log,
			const char    *src_dir,
			const char    *dest_dir)
{
  GDir *dir;
  const char *name;
  char *src;
  char *dest;
  char *msg;

  msg = g_strdup_printf ("cp %s%s* %s\n", src_dir, G_DIR_SEPARATOR_S, dest_dir);
  install_text_insert (log, NULL, msg);
  g_free (msg);

  dir = g_dir_open (src_dir, 0, NULL);
  if (! dir)
    {
      msg = g_strdup_printf ("  %s: %s\n", src_dir, g_strerror (errno));
      install_text_insert (log, "emphasis", msg);
      g_free (msg);
      return;
    }

  while ((name = g_dir_read_name (dir)) != NULL)
    {
      src = g_build_filename (src_dir, name, NULL);

      if (g_file_test (src, G_FILE_TEST_IS_REGULAR))
	{
	  GFile *src_file = g_file_new_for_path (src);
	  GFile *dest_file;
	  GError *error = NULL;

	  dest = g_build_filename (dest_dir, name, NULL);
	  dest_file = g_file_new_for_path (dest);

	  if (! g_file_copy (src_file, dest_file, G_FILE_COPY_OVERWRITE,
			     NULL, NULL, NULL, &error))
	    {
	      msg = g_strdup_printf ("  %s\n", error->message);
	      install_text_insert (log, "emphasis", msg);
	      g_free (msg);
	      g_error_free (error);
	    }

	  g_object_unref (src_file);
	  g_object_unref (dest_file);
	  g_free (dest);
	}

      g_free (src);
    }

  g_dir_close (dir);
}

static int
install_user_files (GtkTextBuffer *log,
		    const char    *data_dir,
		    const char    *user_dir)
{
  static const char *subdirs[] =
  {
    "brushes", "gradients", "palettes", "patterns", "plug-ins",
    "gfig", "tmp", "scripts", "gflares"
  };
  char *src;
  char *dest;
  int i;

  /*  1) Create the user's GIMP directory  */
  if (! install_mkdir (log, user_dir, TRUE))
    return FALSE;

  /*  2) Copy the system gimprc_user file (and the old gtkrc)  */
  src = g_build_filename (data_dir, "gimprc_user", NULL);
  dest = g_build_filename (user_dir, "gimprc", NULL);
  install_copy (log, src, dest);
  g_free (src);
  g_free (dest);

  src = g_build_filename (data_dir, "gtkrc", NULL);
  if (g_file_test (src, G_FILE_TEST_EXISTS))
    {
      dest = g_build_filename (user_dir, "gtkrc", NULL);
      install_copy (log, src, dest);
      g_free (dest);
    }
  g_free (src);

  /*  3) and 4) Create the subdirectories, tmp among them  */
  for (i = 0; i < (int) G_N_ELEMENTS (subdirs); i++)
    {
      dest = g_build_filename (user_dir, subdirs[i], NULL);
      install_mkdir (log, dest, FALSE);
      g_free (dest);
    }

  /*  5) Copy the palette files in the system palette directory  */
  src = g_build_filename (data_dir, "palettes", NULL);
  dest = g_build_filename (user_dir, "palettes", NULL);
  install_copy_dir_files (log, src, dest);
  g_free (src);
  g_free (dest);

  return TRUE;
}

/*  The installation without any window; TRUE when it worked.  */
static int
install_silently (void)
{
  char *data_dir = gimp_data_directory ();

  if (! g_file_test (data_dir, G_FILE_TEST_IS_DIR))
    return FALSE;

  return install_user_files (NULL, data_dir, gimp_directory ());
}

static void
install_run (InstallCallback callback)
{
  static ActionAreaItem action_items[] =
  {
    { "Continue", install_continue_callback, NULL, NULL },
    { "Quit", install_quit_callback, NULL, NULL }
  };
  GtkTextBuffer *buffer;
  char *data_dir;
  int success = TRUE;

  install_data.callback = callback;

  install_widget = gimp_dialog_new ("Installation Log");
  g_signal_connect (install_widget, "close-request",
		    G_CALLBACK (install_close_request),
		    NULL);

  action_items[0].user_data = &install_data;
  action_items[1].user_data = &install_data;
  build_action_area (install_widget, action_items, 2, 0);

  install_text_view_new (install_widget, 384, 256, &buffer);

  install_text_insert (buffer, "strong", "User Installation Log\n\n");

  data_dir = gimp_data_directory ();

  if (! g_file_test (data_dir, G_FILE_TEST_IS_DIR))
    {
      install_text_insert (buffer, NULL, data_dir);
      install_text_insert (buffer, NULL,
			   " does not exist.  Cannot install.\n");
      success = FALSE;
    }
  else
    success = install_user_files (buffer, data_dir, gimp_directory ());

  if (success)
    install_text_insert (buffer, NULL, "\nInstallation successful!\n");
  else
    install_text_insert (buffer, NULL,
			 "\nInstallation failed.  Contact system administrator.\n");

  gtk_window_present (GTK_WINDOW (install_widget));
}

static void
install_continue_callback (GtkWidget *w,
			   gpointer   client_data)
{
  InstallCallback callback;

  callback = ((InstallData *) client_data)->callback;
  gtk_window_destroy (GTK_WINDOW (install_widget));
  (* callback) ();
}

static void
install_quit_callback (GtkWidget *w,
		       gpointer   client_data)
{
  gtk_window_destroy (GTK_WINDOW (install_widget));
  exit (0);
}
