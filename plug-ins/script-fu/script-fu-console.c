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
#include <glib/gstdio.h>
#include "gtk/gtk.h"
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"
#include "siod.h"
#include "script-fu-console.h"
#include <plug-ins/dbbrowser/dbbrowser.h>

#define TEXT_WIDTH  400
#define TEXT_HEIGHT 400
#define ENTRY_WIDTH 400

#define BUFSIZE 256

typedef struct
{
  GtkWidget     *console;
  GtkWidget     *cc;
  GtkTextBuffer *buffer;
  GtkTextMark   *end_mark;

  GtkWidget     *browser;
} ConsoleInterface;

/*
 *  Local Functions
 */

static void  script_fu_console_interface (void);
static void  script_fu_close_callback    (GtkWidget        *widget,
					  gpointer          data);
static void  script_fu_browse_callback    (GtkWidget        *widget,
					  gpointer          data);
static void  script_fu_siod_read         (void);
static void  script_fu_console_insert    (const gchar      *text,
					  const gchar      *tag);
static void  script_fu_console_scroll_end (void);
static gint  script_fu_cc_is_empty       (void);
static gboolean script_fu_cc_key_function (GtkEventControllerKey *controller,
					  guint              keyval,
					  guint              keycode,
					  GdkModifierType    state,
					  gpointer           data);

static FILE *script_fu_open_siod_console (void);
static void  script_fu_close_siod_console(void);

/*
 *  Local variables
 */

static ConsoleInterface cint =
{
  NULL,  /*  console  */
  NULL,  /*  current command  */
  NULL,  /*  text buffer  */
  NULL,  /*  end of text mark  */

  NULL   /*  procedure browser  */
};

static GList *history = NULL;
static int    history_len = 0;
static int    history_cur = 0;
static int    history_max = 50;

/*  SIOD writes its output to a FILE.  For the console that is a temporary
 *  file, and whatever was written since the last look is copied into the
 *  console after each evaluation (the interpreter runs in this thread, so
 *  it could not be read any earlier).  This replaces the pipe the output
 *  used to go through, which could fill up and block.
 */
static gchar *siod_output_name = NULL;
static long   siod_output_read = 0;

extern int   siod_verbose_level;
extern char  siod_err_msg[];
extern FILE *siod_output;


/*
 *  Function definitions
 */

void
script_fu_console_run (char     *name,
		       int       nparams,
		       GParam   *params,
		       int      *nreturn_vals,
		       GParam  **return_vals)
{
  static GParam values[1];
  GStatusType status = STATUS_SUCCESS;
  GRunModeType run_mode;

  run_mode = params[0].data.d_int32;

  switch (run_mode)
    {
    case RUN_INTERACTIVE:
      /*  Enable SIOD output  */
      script_fu_open_siod_console ();

      /*  Run the interface  */
      script_fu_console_interface ();

      /*  Clean up  */
      script_fu_close_siod_console ();
      break;

    case RUN_WITH_LAST_VALS:
    case RUN_NONINTERACTIVE:
      status = STATUS_CALLING_ERROR;
      gimp_message ("Script-Fu console mode allows only interactive invocation");
      break;

    default:
      break;
    }

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
}

static void
script_fu_console_interface ()
{
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *label;
  GtkWidget *scrolled_window;
  GtkWidget *hbox;
  GtkEventController *controller;
  GtkTextIter iter;

  gtk_init ();

  dlg = gimp_dialog_new ("Script-Fu Console");
  g_signal_connect (dlg, "destroy",
		    G_CALLBACK (script_fu_close_callback),
		    NULL);
  g_object_add_weak_pointer (G_OBJECT (dlg), (gpointer *) &dlg);
  gimp_container_set_border_width (gimp_dialog_get_vbox (dlg), 2);
  gimp_container_set_border_width (gimp_dialog_get_action_area (dlg), 2);

  /*  Action area  */
  gimp_dialog_add_button (dlg, "Close",
			  G_CALLBACK (script_fu_close_callback), NULL, FALSE);

  /*  The info vbox  */
  label = gtk_label_new ("SIOD Output");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), label, FALSE, TRUE, 0);

  /*  The output text widget  */
  scrolled_window = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gimp_container_set_border_width (scrolled_window, 2);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), scrolled_window,
		       TRUE, TRUE, 0);

  cint.console = gtk_text_view_new ();
  gtk_text_view_set_editable (GTK_TEXT_VIEW (cint.console), FALSE);
  gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (cint.console), FALSE);
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (cint.console), GTK_WRAP_CHAR);
  gtk_widget_set_size_request (cint.console, TEXT_WIDTH, TEXT_HEIGHT);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window),
				 cint.console);

  /*  The fonts the text used to be drawn in  */
  cint.buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (cint.console));
  gtk_text_buffer_create_tag (cint.buffer, "strong",
			      "family", "Sans",
			      "weight", PANGO_WEIGHT_BOLD,
			      "scale", 1.2,
			      NULL);
  gtk_text_buffer_create_tag (cint.buffer, "emphasis",
			      "family", "Sans",
			      "style", PANGO_STYLE_OBLIQUE,
			      NULL);
  gtk_text_buffer_create_tag (cint.buffer, "weak",
			      "family", "Sans",
			      NULL);
  gtk_text_buffer_create_tag (cint.buffer, "normal",
			      "family", "Monospace",
			      NULL);

  gtk_text_buffer_get_end_iter (cint.buffer, &iter);
  cint.end_mark = gtk_text_buffer_create_mark (cint.buffer, "end", &iter,
					       FALSE);

  script_fu_console_insert ("The GIMP - GNU Image Manipulation Program\n\n", "strong");
  script_fu_console_insert ("Copyright (C) 1995 Spencer Kimball and Peter Mattis\n", "emphasis");
  script_fu_console_insert ("\n", "weak");
  script_fu_console_insert ("This program is free software; you can redistribute it and/or modify\n", "weak");
  script_fu_console_insert ("it under the terms of the GNU General Public License as published by\n", "weak");
  script_fu_console_insert ("the Free Software Foundation; either version 2 of the License, or\n", "weak");
  script_fu_console_insert ("(at your option) any later version.\n", "weak");
  script_fu_console_insert ("\n", "weak");
  script_fu_console_insert ("This program is distributed in the hope that it will be useful,\n", "weak");
  script_fu_console_insert ("but WITHOUT ANY WARRANTY; without even the implied warranty of\n", "weak");
  script_fu_console_insert ("MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n", "weak");
  script_fu_console_insert ("See the GNU General Public License for more details.\n", "weak");
  script_fu_console_insert ("\n", "weak");
  script_fu_console_insert ("You should have received a copy of the GNU General Public License\n", "weak");
  script_fu_console_insert ("along with this program; if not, write to the Free Software\n", "weak");
  script_fu_console_insert ("Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.\n", "weak");
  script_fu_console_insert ("\n\n", "weak");
  script_fu_console_insert ("Script-Fu Console - ", "strong");
  script_fu_console_insert ("Interactive Scheme Development\n\n", "emphasis");

  /*  The current command  */
  label = gtk_label_new ("Current Command");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), label, FALSE, TRUE, 0);

  hbox = gimp_hbox_new (FALSE, 0);
  gtk_widget_set_size_request (hbox, ENTRY_WIDTH, -1);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), hbox, FALSE, TRUE, 0);

  cint.cc = gtk_entry_new ();

  gimp_box_pack_start (hbox, cint.cc, TRUE, TRUE, 0);
  gtk_widget_set_size_request (cint.cc, (ENTRY_WIDTH*5)/6, -1);

  /*  Seen before the entry's own key handling, for Return and history  */
  controller = gtk_event_controller_key_new ();
  gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
  g_signal_connect (controller, "key-pressed",
		    G_CALLBACK (script_fu_cc_key_function),
		    NULL);
  gtk_widget_add_controller (cint.cc, controller);

  button = gtk_button_new_with_label ("Browse...");
  gtk_widget_set_size_request (button, (ENTRY_WIDTH)/6, -1);
  gimp_box_pack_start (hbox, button, FALSE, TRUE, 0);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (script_fu_browse_callback),
		    NULL);

  /*  Whatever SIOD printed so far (the welcome message)  */
  script_fu_siod_read ();

  /*  Initialize the history  */
  history = g_list_append (history, NULL);
  history_len = 1;

  gtk_window_present (GTK_WINDOW (dlg));
  gtk_widget_grab_focus (cint.cc);

  gimp_main_loop_run ();

  if (cint.browser)
    gimp_widget_destroy (cint.browser);
  if (dlg)
    gtk_window_destroy (GTK_WINDOW (dlg));

  cint.console = NULL;
  cint.cc = NULL;
  cint.buffer = NULL;
  cint.end_mark = NULL;
}

static void
script_fu_close_callback (GtkWidget *widget,
			  gpointer   data)
{
  gimp_main_loop_quit ();
}

void apply_callback( gchar *proc_name,
			   gchar *scheme_proc_name,
			   gchar *proc_blurb,
			   gchar *proc_help,
			   gchar *proc_author,
			   gchar *proc_copyright,
			   gchar *proc_date,
			   int proc_type,
			   int nparams,
			   int nreturn_vals,
			   GParamDef *params,
			   GParamDef *return_vals )
{
  GString *text;
  gint i;

  if (proc_name==NULL) return;
  if (cint.cc == NULL) return;

  text = g_string_new ("(");
  g_string_append (text, scheme_proc_name);
  if ((nparams!=0) && (params!=NULL))
    {
      for (i=0;i<nparams;i++) {
	g_string_append (text, " ");
	g_string_append (text, params[i].name);
      }
      g_string_append (text, ")");
    }
  gtk_editable_set_text (GTK_EDITABLE (cint.cc), text->str);
  g_string_free (text, TRUE);
}

static void
script_fu_browse_callback(GtkWidget *widget,
			  gpointer   data)
{
  /*  The browser goes away with the console  */
  if (cint.browser)
    gimp_widget_destroy (cint.browser);

  cint.browser = gimp_db_browser (apply_callback);
  if (cint.browser)
    g_object_add_weak_pointer (G_OBJECT (cint.browser),
			       (gpointer *) &cint.browser);
}

static void
script_fu_console_insert (const gchar *text,
			  const gchar *tag)
{
  GtkTextIter iter;
  gchar      *valid;

  valid = g_utf8_make_valid (text, -1);

  gtk_text_buffer_get_end_iter (cint.buffer, &iter);
  gtk_text_buffer_insert_with_tags_by_name (cint.buffer, &iter, valid, -1,
					    tag, NULL);
  g_free (valid);
}

static void
script_fu_console_scroll_end (void)
{
  GtkTextIter iter;

  gtk_text_buffer_get_end_iter (cint.buffer, &iter);
  gtk_text_buffer_move_mark (cint.buffer, cint.end_mark, &iter);
  gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW (cint.console), cint.end_mark,
				0.0, FALSE, 0.0, 1.0);
}

/*  Copies what SIOD wrote since the last call into the console.  */
static void
script_fu_siod_read (void)
{
  char   read_buffer[BUFSIZE];
  size_t count;
  gboolean any = FALSE;

  if (siod_output == stdout || siod_output == NULL || cint.buffer == NULL)
    return;

  fflush (siod_output);
  if (fseek (siod_output, siod_output_read, SEEK_SET) == 0)
    {
      while ((count = fread (read_buffer, 1, BUFSIZE - 1, siod_output)) > 0)
	{
	  read_buffer[count] = '\0';
	  script_fu_console_insert (read_buffer, "weak");
	  any = TRUE;
	}

      siod_output_read = ftell (siod_output);
    }

  /*  Further output is appended  */
  fseek (siod_output, 0, SEEK_END);

  if (any)
    script_fu_console_scroll_end ();
}

static gint
script_fu_cc_is_empty ()
{
  const char *str;

  if ((str = gtk_editable_get_text (GTK_EDITABLE (cint.cc))) == NULL)
    return TRUE;

  while (*str)
    {
      if (*str != ' ' && *str != '\t' && *str != '\n')
	return FALSE;

      str ++;
    }

  return TRUE;
}

static gboolean
script_fu_cc_key_function (GtkEventControllerKey *controller,
			   guint                  keyval,
			   guint                  keycode,
			   GdkModifierType        state,
			   gpointer               data)
{
  GList *list;
  int direction = 0;

  switch (keyval)
    {
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
      if (script_fu_cc_is_empty ())
	return TRUE;

      list = g_list_nth (history, (g_list_length (history) - 1));
      if (list->data)
	g_free (list->data);
      list->data = g_strdup (gtk_editable_get_text (GTK_EDITABLE (cint.cc)));

      script_fu_console_insert ("=> ", "strong");
      script_fu_console_insert (gtk_editable_get_text (GTK_EDITABLE (cint.cc)),
				"normal");
      script_fu_console_insert ("\n\n", "normal");
      script_fu_console_scroll_end ();

      gtk_editable_set_text (GTK_EDITABLE (cint.cc), "");

      repl_c_string ((char *) list->data, 0, 0, 1);
      gimp_displays_flush ();

      /*  Show what the evaluation printed  */
      script_fu_siod_read ();

      history = g_list_append (history, NULL);
      if (history_len == history_max)
	{
	  if (history->data)
	    g_free (history->data);
	  history = g_list_delete_link (history, history);
	}
      else
	history_len++;
      history_cur = g_list_length (history) - 1;

      return TRUE;
      break;

    case GDK_KEY_KP_Up:
    case GDK_KEY_Up:
      direction = -1;
      break;

    case GDK_KEY_KP_Down:
    case GDK_KEY_Down:
      direction = 1;
      break;

    case GDK_KEY_P:
    case GDK_KEY_p:
      if (state & GDK_CONTROL_MASK)
	direction = -1;
      break;

    case GDK_KEY_N:
    case GDK_KEY_n:
      if (state & GDK_CONTROL_MASK)
	direction = 1;
      break;

    default:
      break;
    }

  if (direction)
    {
      /*  Make sure we keep track of the current one  */
      if (history_cur == g_list_length (history) - 1)
	{
	  list = g_list_nth (history, history_cur);
	  if (list->data)
	    g_free (list->data);
	  list->data = g_strdup (gtk_editable_get_text (GTK_EDITABLE (cint.cc)));
	}

      history_cur += direction;
      if (history_cur < 0)
	history_cur = 0;
      if (history_cur >= history_len)
	history_cur = history_len - 1;

      list = g_list_nth (history, history_cur);
      gtk_editable_set_text (GTK_EDITABLE (cint.cc),
			     list->data ? (char *) list->data : "");
      gtk_editable_set_position (GTK_EDITABLE (cint.cc), -1);

      return TRUE;
    }

  return FALSE;
}


static FILE *
script_fu_open_siod_console ()
{
  gint fd;

  if (siod_output == stdout)
    {
      fd = g_file_open_tmp ("script-fu-XXXXXX", &siod_output_name, NULL);

      if (fd < 0)
	{
	  gimp_message ("Unable to open SIOD output file");
	}
      else if ((siod_output = fdopen (fd, "w+b")) == NULL)
	{
	  gimp_message ("Unable to open a stream on the SIOD output file");
	  g_close (fd, NULL);
	  g_unlink (siod_output_name);
	  g_free (siod_output_name);
	  siod_output_name = NULL;
	  siod_output = stdout;
	}
      else
	{
	  siod_output_read = 0;
	  siod_verbose_level = 2;
	  print_welcome ();
	}
    }

  return siod_output;
}

static void
script_fu_close_siod_console ()
{
  if (siod_output != stdout)
    fclose (siod_output);
  siod_output = stdout;

  if (siod_output_name)
    {
      g_unlink (siod_output_name);
      g_free (siod_output_name);
      siod_output_name = NULL;
    }
}

void
script_fu_eval_run (char     *name,
		    int       nparams,
		    GParam   *params,
		    int      *nreturn_vals,
		    GParam  **return_vals)
{
  static GParam values[1];
  GStatusType status = STATUS_SUCCESS;
  GRunModeType run_mode;

  run_mode = params[0].data.d_int32;

  switch (run_mode)
    {
    case RUN_NONINTERACTIVE:
      if (repl_c_string (params[1].data.d_string, 0, 0, 1) != 0)
	status = STATUS_EXECUTION_ERROR;
      break;

    case RUN_INTERACTIVE:
    case RUN_WITH_LAST_VALS:
      status = STATUS_CALLING_ERROR;
      gimp_message ("Script-Fu evaluate mode allows only noninteractive invocation");
      break;

    default:
      break;
    }

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
}
