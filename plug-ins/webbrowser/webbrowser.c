/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * Copyright (C) 1997 Misha Dynin
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

/*
	Web Browser v0.3 -- opens a URL in a web browser

	    by Misha Dynin <misha@xcf.berkeley.edu>

	For more information, see webbrowser.readme, as well as

	    http://www.xcf.berkeley.edu/~misha/gimp/

	The original drove Netscape through its X11 remote control
	protocol (window properties), starting it when it was not
	running.  This version hands the URL to the desktop's default
	browser through GIO, which works on Windows, macOS and any
	freedesktop system.  The "new_window" argument is kept for
	compatibility; whether a new window or tab opens is up to the
	browser.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <gio/gio.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

/* Open a new window with request? */
#define OPEN_URL_NEW_WINDOW	1
#define OPEN_URL_CURRENT_WINDOW	0

static void query (void);
static void run (char *name,
		 int nparams,
		 GParam * param,
		 int *nreturn_vals,
		 GParam ** return_vals);


static gint open_url_dialog (void);
static void close_callback (GtkWidget * widget, gpointer data);
static void ok_callback (GtkWidget * widget, gpointer data);
static void about_callback (GtkWidget * widget, gpointer data);
static void new_window_callback (GtkWidget * widget, gpointer data);

static void url_callback (GtkWidget * widget, gpointer data);
static gint open_url (const char *url, int new_window);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,				/* init_proc */
  NULL,				/* quit_proc */
  query,			/* query_proc */
  run,				/* run_proc */
};


typedef struct
  {
    char url[255];
    int  new_window;
  }
u_info;

static u_info url_info = {
  "https://github.com/office-42/gimp42",	/* Default URL */
  OPEN_URL_NEW_WINDOW,		/* Change to ...CURRENT_WINDOW if
				   you prefer that as the default */
};

static int run_flag = 0;

MAIN ()

static void
query (void)
{

  static GParamDef args[] =
  {
    {PARAM_INT32, "run_mode", "Interactive, non-interactive"},
    {PARAM_STRING, "url", "URL of a document to open"},
    {PARAM_INT32,  "new_window", "Create a new window or use existing one?"},
  };
  static int nargs = sizeof (args) / sizeof (args[0]);
  static GParamDef *return_vals = NULL;
  static int nreturn_vals = 0;

  gimp_install_procedure ("extension_web_browser",
			  "open URL in the web browser",
			  "Opens the URL in the system's default web browser",
			  "Misha Dynin <misha@xcf.berkeley.edu>",
	      "Misha Dynin, Jamie Zawinski, Spencer Kimball & Peter Mattis",
			  "1997",
			  "<Toolbox>/Xtns/Web Browser/Open URL...",
			  "RGB*, GRAY*, INDEXED*",
			  PROC_EXTENSION,
			  nargs, nreturn_vals,
			  args, return_vals);
}


static void
run (char *name,
     int nparams,
     GParam * param,
     int *nreturn_vals,
     GParam ** return_vals)
{
  static GParam values[1];
  GRunModeType run_mode;
  GStatusType status = STATUS_SUCCESS;

  run_mode = param[0].data.d_int32;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;

  *nreturn_vals = 1;
  *return_vals = values;

  if (strcmp (name, "extension_web_browser") == 0)
    {
      switch (run_mode)
	{
	case RUN_INTERACTIVE:
	  /* Possibly retrieve data */
	  gimp_get_data ("extension_web_browser", &url_info);

	  if (!open_url_dialog ())
	    return;
	  break;

	case RUN_NONINTERACTIVE:
	  /*  Make sure all the arguments are there!  */
	  if (nparams != 3)
	    status = STATUS_CALLING_ERROR;
	  if(status == STATUS_SUCCESS)
	    {
	      g_strlcpy (url_info.url, param[1].data.d_string,
			 sizeof (url_info.url));
	      url_info.new_window = param[2].data.d_int32;
	    }
	  break;

	case RUN_WITH_LAST_VALS:
	  gimp_get_data ("extension_web_browser", &url_info);
	  break;

	default:
	  break;
	}

      if (status == STATUS_SUCCESS)
	{
	  if (run_mode == RUN_INTERACTIVE)
	    gimp_set_data ("extension_web_browser", &url_info, sizeof (u_info));

	  if (open_url (url_info.url, url_info.new_window))
	    values[0].data.d_status = STATUS_SUCCESS;
	  else
	    values[0].data.d_status = STATUS_EXECUTION_ERROR;
	}
      else
	values[0].data.d_status = STATUS_EXECUTION_ERROR;
    }
  else
    g_assert (FALSE);
}

static gint
open_url (const char *url, int new_window)
{
  GError *error = NULL;
  gchar  *uri;
  gchar  *scheme;

  while (isspace ((guchar) *url))
    ++url;

  if (*url == '\0')
    return FALSE;

  /*  Netscape accepted "www.gimp.org" and local file names; the
   *  desktop wants a real URI.
   */
  scheme = g_uri_parse_scheme (url);
  if (scheme)
    uri = g_strdup (url);
  else if (g_path_is_absolute (url))
    uri = g_filename_to_uri (url, NULL, NULL);
  else
    uri = g_strconcat ("http://", url, NULL);
  g_free (scheme);

  if (! uri)
    return FALSE;

  if (! g_app_info_launch_default_for_uri (uri, NULL, &error))
    {
      g_message ("webbrowser: could not open %s: %s", uri,
		 error ? error->message : "unknown error");
      g_clear_error (&error);
      g_free (uri);
      return FALSE;
    }

  g_free (uri);
  return TRUE;
}


static gint
open_url_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *entry;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *button1;
  GtkWidget *button2;

  gtk_init ();

  dlg = gimp_dialog_new ("Open URL");
  g_signal_connect (dlg, "destroy",
		    G_CALLBACK (close_callback), NULL);
  /* action area   */
  /* Okay buton */
  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (ok_callback), dlg, TRUE);

  /* cancel button */
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  /* about button */
  gimp_dialog_add_button (dlg, "About...", G_CALLBACK (about_callback),
			  dlg, FALSE);

  /* table */
  table = gimp_table_new (2, 3, FALSE);
  gimp_container_set_border_width (table, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), table, TRUE, TRUE, 0);

  gtk_grid_set_row_spacing (GTK_GRID (table), 10);
  gtk_grid_set_column_spacing (GTK_GRID (table), 10);

  /*  URL:  Label */
  label = gtk_label_new ("URL:");
  gimp_table_attach (table, label,
		     0, 1, 0, 1,
		     GIMP_EXPAND | GIMP_FILL,
		     GIMP_EXPAND | GIMP_FILL,
		     0, 0);

  /* URL: dialog */
  entry = gtk_entry_new ();
  gimp_table_attach (table, entry,
		     1, 3, 0, 1,
		     GIMP_EXPAND | GIMP_FILL,
		     GIMP_EXPAND | GIMP_FILL,
		     0, 0);
  gtk_widget_set_size_request (entry, 200, -1);
  gtk_editable_set_text (GTK_EDITABLE (entry), url_info.url);
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  g_signal_connect (entry, "changed",
		    G_CALLBACK (url_callback), &url_info.url);

  /* Window label */
  label = gtk_label_new ("Window:");
  gimp_table_attach (table, label,
		     0, 1, 1, 2,
		     GIMP_EXPAND | GIMP_FILL,
		     GIMP_EXPAND | GIMP_FILL,
		     0, 0);

  /* Window radiobutton */
  button1 = gimp_radio_button_new (NULL, "new");
  button2 = gimp_radio_button_new (button1, "current");
  if( url_info.new_window == OPEN_URL_NEW_WINDOW ) {
      gtk_check_button_set_active (GTK_CHECK_BUTTON (button1), TRUE);
  } else {
      gtk_check_button_set_active (GTK_CHECK_BUTTON (button2), TRUE);
  }
  g_signal_connect (button1, "toggled",
		    G_CALLBACK (new_window_callback),
		    (gpointer) "new" );
  g_signal_connect (button2, "toggled",
		    G_CALLBACK (new_window_callback),
		    (gpointer) "current" );

  gimp_table_attach (table, button1,
		     1, 2, 1, 2,
		     GIMP_EXPAND | GIMP_FILL,
		     GIMP_EXPAND | GIMP_FILL,
		     0, 0 );

  gimp_table_attach (table, button2,
		     2, 3, 1, 2,
		     GIMP_EXPAND | GIMP_FILL,
		     GIMP_EXPAND | GIMP_FILL,
		     0, 0 );


  gtk_window_present (GTK_WINDOW (dlg));
  gimp_main_loop_run ();
  return run_flag;

}


static void
close_callback (GtkWidget * widget, gpointer data)
{
  gimp_main_loop_quit ();
}

static void
ok_callback (GtkWidget * widget, gpointer data)
{
  run_flag = 1;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void
about_callback (GtkWidget * widget, gpointer data)
{
  open_url ("http://www.xcf.berkeley.edu/~misha/gimp/", 1);
}

static void
new_window_callback (GtkWidget * widget, gpointer data)
{
    /* Ignore the toggle-off signal, we are only interested in
       what is being set */
    if( ! gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)) ) {
	return;
    }
    if(strcmp (data, "new") == 0)
	url_info.new_window = OPEN_URL_NEW_WINDOW;
    if(strcmp (data, "current") == 0)
	url_info.new_window = OPEN_URL_CURRENT_WINDOW;
}

static void
url_callback (GtkWidget * widget, gpointer data)
{
  g_strlcpy (url_info.url, gtk_editable_get_text (GTK_EDITABLE (widget)),
	     sizeof (url_info.url));
}
