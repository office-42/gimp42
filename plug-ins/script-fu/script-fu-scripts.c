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
#include "gtk/gtk.h"
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"
#include "siod.h"
#include "script-fu-scripts.h"

#define TEXT_WIDTH  100
#define TEXT_HEIGHT 25
#define COLOR_SAMPLE_WIDTH 100
#define COLOR_SAMPLE_HEIGHT 15

typedef struct
{
  GtkWidget *preview;
  gdouble    color[3];
} SFColor;

typedef union
{
  gint32      sfa_image;
  gint32      sfa_drawable;
  gint32      sfa_layer;
  gint32      sfa_channel;
  SFColor     sfa_color;
  gint32      sfa_toggle;
  gchar *     sfa_value;
} SFArgValue;

typedef struct
{
  GtkWidget ** args_widgets;
  gchar *      script_name;
  gchar *      description;
  gchar *      help;
  gchar *      author;
  gchar *      copyright;
  gchar *      date;
  gchar *      img_types;
  gint         num_args;
  SFArgType *  arg_types;
  gchar **     arg_labels;
  SFArgValue * arg_defaults;
  SFArgValue * arg_values;
  gint32       image_based;
} SFScript;

typedef struct
{
  GtkWidget *cc;
  SFScript  *script;
} SFInterface;

/* External functions
 */
extern long  nlength      (LISP obj);

/*
 *  Local Functions
 */

static void       script_fu_script_proc      (char     *name,
					      int       nparams,
					      GParam   *params,
					      int      *nreturn_vals,
					      GParam  **return_vals);

static SFScript  *script_fu_find_script      (gchar    *script_name);
static void       script_fu_free_script      (SFScript *script);
static void       script_fu_enable_cc        (void);
static void       script_fu_disable_cc       (gint    err_msg);
static void       script_fu_interface        (SFScript *script);
static void       script_fu_color_preview    (GtkWidget *preview,
					      gdouble   *color);
static void       script_fu_ok_callback      (GtkWidget *widget,
					      gpointer   data);
static void       script_fu_close_callback   (GtkWidget *widget,
					      gpointer   data);
static void       script_fu_menu_callback    (gint32     id,
					      gpointer   data);
static void       script_fu_toggle_update    (GtkWidget *widget,
					      gpointer   data);
static void       script_fu_preview_callback (GtkWidget *widget,
					      gpointer   data);
static void       script_fu_color_chosen     (const guchar *rgb,
					      gpointer   data);
static gchar    **script_fu_split_path       (const gchar *path_str);
static void       script_fu_load_script      (const gchar *filename);

/*
 *  Local variables
 */

static SFInterface sf_interface =
{
  NULL,  /*  current command  */
  NULL   /*  active script    */
};

static gint   current_command_enabled = FALSE;
static gint   command_count = 0;
static gint   consec_command_count = 0;
static gchar *last_command = NULL;
static GList *script_list = NULL;

extern char   siod_err_msg[];

/*
 *  Function definitions
 */

void
script_fu_find_scripts ()
{
  GParam *return_vals;
  gint nreturn_vals;
  gchar *path_str;
  gchar **tokens;
  gchar *path;
  gchar *filename;
  const gchar *entry;
  GDir  *dir;
  gint   i;

  /*  Make sure to clear any existing scripts  */
  if (script_list != NULL)
    {
      GList *list;
      SFScript *script;

      list = script_list;
      while (list)
	{
	  script = (SFScript *) list->data;
	  script_fu_free_script (script);
	  list = list->next;
	}

      if (script_list)
	g_list_free (script_list);
      script_list = NULL;
    }

  return_vals = gimp_run_procedure ("gimp_gimprc_query",
				    &nreturn_vals,
				    PARAM_STRING, "script-fu-path",
				    PARAM_END);

  if (return_vals[0].data.d_status == STATUS_SUCCESS &&
      (path_str = return_vals[1].data.d_string) != NULL)
    {
      /* Search through all directories in the path */
      tokens = script_fu_split_path (path_str);

      for (i = 0; tokens[i]; i++)
	{
	  if (*tokens[i] == '\0')
	    continue;

	  if (*tokens[i] == '~')
	    path = g_build_filename (g_get_home_dir (), tokens[i] + 1, NULL);
	  else
	    path = g_strdup (tokens[i]);

	  /* Check if directory exists and if it has any items in it */
	  if (g_file_test (path, G_FILE_TEST_IS_DIR))
	    {
	      /* Open directory */
	      dir = g_dir_open (path, 0, NULL);

	      if (!dir)
		g_message ("error reading script directory \"%s\"", path);
	      else
		{
		  while ((entry = g_dir_read_name (dir)))
		    {
		      if (! g_str_has_suffix (entry, ".scm"))
			continue;

		      filename = g_build_filename (path, entry, NULL);

		      /* Check the file and see that it is not a sub-directory */
		      if (g_file_test (filename, G_FILE_TEST_IS_REGULAR))
			script_fu_load_script (filename);

		      g_free (filename);
		    } /* while */

		  g_dir_close (dir);
		} /* else */
	    } /* if */

	  g_free (path);
	} /* for */

      g_strfreev (tokens);
    }

  gimp_destroy_params (return_vals, nreturn_vals);
}

/*  Splits a search path such as gimprc's script-fu-path.  The separator
 *  is G_SEARCHPATH_SEPARATOR (';' on Windows); a gimprc written for Unix
 *  separates with ':', which is accepted on Windows too as long as it is
 *  not the colon of a drive letter ("C:\..." or "C:/...").
 */
static gchar **
script_fu_split_path (const gchar *path_str)
{
#ifdef G_OS_WIN32
  GPtrArray   *array = g_ptr_array_new ();
  const gchar *start = path_str;
  const gchar *p;

  for (p = path_str; ; p++)
    {
      gboolean split = FALSE;

      if (*p == '\0' || *p == G_SEARCHPATH_SEPARATOR)
	split = TRUE;
      else if (*p == ':' &&
	       ! (p - start == 1 && g_ascii_isalpha (*start) &&
		  (p[1] == '\\' || p[1] == '/')))
	split = TRUE;

      if (split)
	{
	  g_ptr_array_add (array, g_strndup (start, p - start));
	  if (*p == '\0')
	    break;
	  start = p + 1;
	}
    }

  g_ptr_array_add (array, NULL);

  return (gchar **) g_ptr_array_free (array, FALSE);
#else
  return g_strsplit (path_str, G_SEARCHPATH_SEPARATOR_S, -1);
#endif
}

/*  Loads one script file through the interpreter.  The name goes into a
 *  Scheme string, so backslashes (Windows paths) and quotes are escaped.
 */
static void
script_fu_load_script (const gchar *filename)
{
  GString     *command;
  const gchar *p;

  command = g_string_new ("(load \"");
  for (p = filename; *p; p++)
    {
      if (*p == '\\' || *p == '"')
	g_string_append_c (command, '\\');
      g_string_append_c (command, *p);
    }
  g_string_append (command, "\")");

  repl_c_string (command->str, 0, 0, 1);

  g_string_free (command, TRUE);
}

LISP
script_fu_add_script (LISP a)
{
  SFScript *script;
  GParamDef *args;
  char *val;
  int i;
  gdouble color[3];
  LISP color_list;
  gchar *menu_path = NULL;

  /*  Check the length of a  */
  if (nlength (a) < 7)
    return my_err ("Too few arguments to script-fu-register", NIL);

  /*  Create a new script  */
  script = g_new (SFScript, 1);

  /*  Find the script name  */
  val = get_c_string (car (a));
  script->script_name = g_strdup (val);
  a = cdr (a);

  /*  Find the script description  */
  val = get_c_string (car (a));
  script->description = g_strdup (val);
  a = cdr (a);

  /* Allow scripts with no menus */
  if (strncmp(val, "<None>", 6) != 0)
      menu_path = script->description;

  /*  Find the script help  */
  val = get_c_string (car (a));
  script->help = g_strdup (val);
  a = cdr (a);

  /*  Find the script author  */
  val = get_c_string (car (a));
  script->author = g_strdup (val);
  a = cdr (a);

  /*  Find the script copyright  */
  val = get_c_string (car (a));
  script->copyright = g_strdup (val);
  a = cdr (a);

  /*  Find the script date  */
  val = get_c_string (car (a));
  script->date = g_strdup (val);
  a = cdr (a);

  /*  Find the script image types  */
  if (TYPEP (a, tc_cons))
    {
      val = get_c_string (car (a));
      a = cdr (a);
    }
  else
    {
      val = get_c_string (a);
      a = NIL;
    }
  script->img_types = g_strdup (val);

  /*  Check the supplied number of arguments  */
  script->num_args = nlength (a) / 3;

  args = g_new (GParamDef, script->num_args + 1);
  args[0].type = PARAM_INT32;
  args[0].name = "run_mode";
  args[0].description = "Interactive, non-interactive";

  script->args_widgets = NULL;
  script->arg_types = g_new (SFArgType, script->num_args);
  script->arg_labels = g_new (char *, script->num_args);
  script->arg_defaults = g_new (SFArgValue, script->num_args);
  script->arg_values = g_new (SFArgValue, script->num_args);

  if (script->num_args > 0)
    {
      for (i = 0; i < script->num_args; i++)
	{
	  if (a != NIL)
	    {
	      if (!TYPEP (car (a), tc_flonum))
		return my_err ("script-fu-register: argument types must be integer values", NIL);
	      script->arg_types[i] = get_c_long (car (a));
	      a = cdr (a);
	    }
	  else
	    return my_err ("script-fu-register: missing type specifier", NIL);

	  if (a != NIL)
	    {
	      if (!TYPEP (car (a), tc_string))
		return my_err ("script-fu-register: argument labels must be strings", NIL);
	      script->arg_labels[i] = g_strdup (get_c_string (car (a)));
	      a = cdr (a);
	    }
	  else
	    return my_err ("script-fu-register: missing arguments label", NIL);

	  if (a != NIL)
	    {
	      switch (script->arg_types[i])
		{
		case SF_IMAGE:
		case SF_DRAWABLE:
		case SF_LAYER:
		case SF_CHANNEL:
		  if (!TYPEP (car (a), tc_flonum))
		    return my_err ("script-fu-register: drawable defaults must be integer values", NIL);
		  script->arg_defaults[i].sfa_image = get_c_long (car (a));
		  script->arg_values[i].sfa_image = script->arg_defaults[i].sfa_image;

		  switch (script->arg_types[i])
		    {
		    case SF_IMAGE:
		      args[i + 1].type = PARAM_IMAGE;
		      args[i + 1].name = "image";
		      break;
		    case SF_DRAWABLE:
		      args[i + 1].type = PARAM_DRAWABLE;
		      args[i + 1].name = "drawable";
		      break;
		    case SF_LAYER:
		      args[i + 1].type = PARAM_LAYER;
		      args[i + 1].name = "layer";
		      break;
		    case SF_CHANNEL:
		      args[i + 1].type = PARAM_CHANNEL;
		      args[i + 1].name = "channel";
		      break;
		    default:
		      break;
		    }

		  args[i + 1].description = script->arg_labels[i];
		  break;

		case SF_COLOR:
		  if (!TYPEP (car (a), tc_cons))
		    return my_err ("script-fu-register: color defaults must be a list of 3 integers", NIL);
		  color_list = car (a);
		  color[0] = (gdouble) get_c_long (car (color_list)) / 255.0;
		  color_list = cdr (color_list);
		  color[1] = (gdouble) get_c_long (car (color_list)) / 255.0;
		  color_list = cdr (color_list);
		  color[2] = (gdouble) get_c_long (car (color_list)) / 255.0;
		  memcpy (script->arg_defaults[i].sfa_color.color, color, sizeof (gdouble) * 3);
		  memcpy (script->arg_values[i].sfa_color.color, color, sizeof (gdouble) * 3);
		  script->arg_values[i].sfa_color.preview = NULL;

		  args[i + 1].type = PARAM_COLOR;
		  args[i + 1].name = "color";
		  args[i + 1].description = script->arg_labels[i];
		  break;

		case SF_TOGGLE:
		  if (!TYPEP (car (a), tc_flonum))
		    return my_err ("script-fu-register: toggle default must be an integer value", NIL);
		  script->arg_defaults[i].sfa_toggle = (get_c_long (car (a))) ? TRUE : FALSE;
		  script->arg_values[i].sfa_toggle = script->arg_defaults[i].sfa_toggle;

		  args[i + 1].type = PARAM_INT32;
		  args[i + 1].name = "toggle";
		  args[i + 1].description = script->arg_labels[i];
		  break;

		case SF_VALUE:
		  if (!TYPEP (car (a), tc_string))
		    return my_err ("script-fu-register: value defaults must be string values", NIL);
		  script->arg_defaults[i].sfa_value = g_strdup (get_c_string (car (a)));

		  args[i + 1].type = PARAM_STRING;
		  args[i + 1].name = "value";
		  args[i + 1].description = script->arg_labels[i];
		  break;
		default:
		  break;
		}

	      a = cdr (a);
	    }
	  else
	    return my_err ("script-fu-register: missing default argument", NIL);
	}
    }

  gimp_install_temp_proc (script->script_name,
			  script->description,
			  script->help,
			  script->author,
			  script->copyright,
			  script->date,
			  menu_path,
			  script->img_types,
			  PROC_TEMPORARY,
			  script->num_args + 1, 0,
			  args, NULL,
			  script_fu_script_proc);

  g_free (args);

  script_list = g_list_append (script_list, script);

  return NIL;
}

void
script_fu_report_cc (gchar *command)
{
  if (last_command && strcmp (last_command, command) == 0)
    {
      char *new_command;

      new_command = g_strdup_printf ("%s <%d>", command, ++consec_command_count);
      if (current_command_enabled == TRUE)
	gtk_editable_set_text (GTK_EDITABLE (sf_interface.cc), new_command);
      g_free (new_command);
      g_free (last_command);
    }
  else
    {
      consec_command_count = 1;
      if (current_command_enabled == TRUE)
	gtk_editable_set_text (GTK_EDITABLE (sf_interface.cc), command);
      if (last_command)
	g_free (last_command);
    }
  last_command = g_strdup (command);
  command_count++;

  /*  Let the current command field redraw while the script runs; the
   *  dialog is insensitive meanwhile (see script_fu_ok_callback).
   */
  if (current_command_enabled == TRUE)
    gimp_process_events ();
}

static void
script_fu_script_proc (char     *name,
		       int       nparams,
		       GParam   *params,
		       int      *nreturn_vals,
		       GParam  **return_vals)
{
  static GParam values[1];
  GStatusType status = STATUS_SUCCESS;
  GRunModeType run_mode;
  SFScript *script;
  int min_args;

  run_mode = params[0].data.d_int32;

  if (! (script = script_fu_find_script (name)))
    status = STATUS_CALLING_ERROR;
  else
    {
      if (script->num_args == 0)
	run_mode = RUN_NONINTERACTIVE;

      switch (run_mode)
	{
	case RUN_INTERACTIVE:
	case RUN_WITH_LAST_VALS:
	  /*  Determine whether the script is image based (runs on an image)  */
	  if (strncmp (script->description, "<Image>", 7) == 0)
	    {
	      script->arg_values[0].sfa_image = params[1].data.d_image;
	      script->arg_values[1].sfa_drawable = params[2].data.d_drawable;
	      script->image_based = TRUE;
	    }
	  else
	    script->image_based = FALSE;

	  /*  First acquire information with a dialog  */
	  /*  Skip this part if the script takes no parameters */ 
	  min_args = (script->image_based) ? 2 : 0;
	  if (script->num_args > min_args) {
	    script_fu_interface (script); 
	    break;
	  }

	case RUN_NONINTERACTIVE:
	  /*  Make sure all the arguments are there!  */
	  if (nparams != (script->num_args + 1))
	    status = STATUS_CALLING_ERROR;
	  if (status == STATUS_SUCCESS)
	    {
	      gint err_msg;
	      GString *command;
	      int i;

	      /*  built with a GString: an SF_VALUE string can be any length  */
	      command = g_string_new ("(");
	      g_string_append (command, script->script_name);

	      for (i = 0; i < script->num_args; i++)
		{
		  g_string_append_c (command, ' ');

		  switch (script->arg_types[i])
		    {
		    case SF_IMAGE:
		    case SF_DRAWABLE:
		    case SF_LAYER:
		    case SF_CHANNEL:
		      g_string_append_printf (command, "%d",
					      params[i + 1].data.d_image);
		      break;
		    case SF_COLOR:
		      g_string_append_printf (command, "'(%d %d %d)",
					      params[i + 1].data.d_color.red,
					      params[i + 1].data.d_color.green,
					      params[i + 1].data.d_color.blue);
		      break;
		    case SF_TOGGLE:
		      g_string_append (command,
				       (params[i + 1].data.d_int32) ? "TRUE" : "FALSE");
		      break;
		    case SF_VALUE:
		      if (params[i + 1].data.d_string)
			g_string_append (command, params[i + 1].data.d_string);
		      else
			g_string_append (command, "\"\"");
		      break;
		    default:
		      break;
		    }
		}

	      g_string_append_c (command, ')');

	      /*  run the command through the interpreter  */
	      err_msg = (repl_c_string (command->str, 0, 0, 1) != 0) ? TRUE : FALSE;

	      g_string_free (command, TRUE);
	    }
	  break;

	default:
	  break;
	}
    }

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
}

static SFScript *
script_fu_find_script (gchar *script_name)
{
  GList *list;
  SFScript *script;

  list = script_list;
  while (list)
    {
      script = (SFScript *) list->data;
      if (! strcmp (script->script_name, script_name))
	return script;

      list = list->next;
    }

  return NULL;
}

static void
script_fu_free_script (SFScript *script)
{
  int i;

  /*  Uninstall the temporary procedure for this script  */
  gimp_uninstall_temp_proc (script->script_name);

  if (script)
    {
      g_free (script->script_name);
      g_free (script->description);
      g_free (script->help);
      g_free (script->author);
      g_free (script->copyright);
      g_free (script->date);
      g_free (script->img_types);
      g_free (script->arg_types);

      for (i = 0; i < script->num_args; i++)
	{
	  g_free (script->arg_labels[i]);
	  switch (script->arg_types[i])
	    {
	    case SF_IMAGE:
	    case SF_DRAWABLE:
	    case SF_LAYER:
	    case SF_CHANNEL:
	    case SF_COLOR:
	      break;
	    case SF_VALUE:
	      g_free (script->arg_defaults[i].sfa_value);
	      break;
	    default:
	      break;
	    }
	}

      g_free (script->arg_labels);
      g_free (script->arg_defaults);
      g_free (script->arg_values);

      g_free (script);
    }
}

static void
script_fu_enable_cc ()
{
  current_command_enabled = TRUE;
}

static void
script_fu_disable_cc (gint err_msg)
{
  if (err_msg)
    g_message ("Script-Fu Error\n%s\n"
              "If this happens while running a logo script,\n"
              "you might not have the font it wants installed on your system",
              siod_err_msg);

  current_command_enabled = FALSE;

  if (last_command)
    g_free (last_command);
  last_command = NULL;
  command_count = 0;
  consec_command_count = 0;
}

static gboolean script_fu_running = FALSE;

static gboolean
script_fu_close_request (GtkWindow *window,
			 gpointer   data)
{
  /*  Keep the dialog while its script runs  */
  return script_fu_running;
}

static void
script_fu_interface (SFScript *script)
{
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *label;
  GtkWidget *table;
  gchar *title;
  int start_args;
  int i;

  static gint gtk_initted = FALSE;

  if (!gtk_initted)
    {
      gtk_init ();

      gtk_initted = TRUE;
    }

  sf_interface.script = script;

  title = g_strdup_printf ("Script-Fu: %s", script->description);

  dlg = gimp_dialog_new (title);
  g_free (title);
  g_signal_connect (dlg, "destroy",
		    G_CALLBACK (script_fu_close_callback),
		    NULL);
  g_signal_connect (dlg, "close-request",
		    G_CALLBACK (script_fu_close_request),
		    NULL);
  gimp_container_set_border_width (gimp_dialog_get_vbox (dlg), 2);
  gimp_container_set_border_width (gimp_dialog_get_action_area (dlg), 2);

  /*  Action area  */
  gimp_dialog_add_button (dlg, "OK",
			  G_CALLBACK (script_fu_ok_callback), dlg, TRUE);

  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy),
			    dlg);

  /*  The info vbox  */
  label = gtk_label_new ("Script Arguments");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), label, FALSE, TRUE, 0);

  /*  The argument table  */
  table = gimp_table_new (script->num_args, 2, FALSE);
  gimp_container_set_border_width (table, 4);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), table, TRUE, TRUE, 0);

  script->args_widgets = g_new0 (GtkWidget *, script->num_args);

  start_args = (script->image_based) ? 2 : 0;

  for (i = start_args; i < script->num_args; i++)
    {
      label = gtk_label_new (script->arg_labels[i]);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gimp_table_attach (table, label,
			 0, 1, i, i + 1, GIMP_FILL, GIMP_FILL, 4, 2);

      switch (script->arg_types[i])
	{
	case SF_IMAGE:
	  script->args_widgets[i] =
	    gimp_image_menu_new (NULL, script_fu_menu_callback,
				 &script->arg_values[i].sfa_image,
				 script->arg_defaults[i].sfa_image);
	  break;
	case SF_DRAWABLE:
	  script->args_widgets[i] =
	    gimp_drawable_menu_new (NULL, script_fu_menu_callback,
				    &script->arg_values[i].sfa_drawable,
				    script->arg_defaults[i].sfa_drawable);
	  break;
	case SF_LAYER:
	  script->args_widgets[i] =
	    gimp_layer_menu_new (NULL, script_fu_menu_callback,
				 &script->arg_values[i].sfa_layer,
				 script->arg_defaults[i].sfa_layer);
	  break;
	case SF_CHANNEL:
	  script->args_widgets[i] =
	    gimp_channel_menu_new (NULL, script_fu_menu_callback,
				   &script->arg_values[i].sfa_channel,
				   script->arg_defaults[i].sfa_channel);
	  break;

	case SF_COLOR:
	  script->args_widgets[i] = gtk_button_new ();

	  script->arg_values[i].sfa_color.preview = gimp_preview_new (GIMP_PREVIEW_COLOR);
	  gimp_preview_size (GIMP_PREVIEW (script->arg_values[i].sfa_color.preview),
			     COLOR_SAMPLE_WIDTH, COLOR_SAMPLE_HEIGHT);
	  gtk_button_set_child (GTK_BUTTON (script->args_widgets[i]),
				script->arg_values[i].sfa_color.preview);
	  /*  The pointer is cleared when the dialog goes away  */
	  g_object_add_weak_pointer (G_OBJECT (script->arg_values[i].sfa_color.preview),
				     (gpointer *) &script->arg_values[i].sfa_color.preview);

	  script_fu_color_preview (script->arg_values[i].sfa_color.preview,
				   script->arg_values[i].sfa_color.color);

	  g_signal_connect (script->args_widgets[i], "clicked",
			    G_CALLBACK (script_fu_preview_callback),
			    &script->arg_values[i].sfa_color);
	  break;

	case SF_TOGGLE:
	  gtk_label_set_text (GTK_LABEL (label), "Script Toggle");
	  script->args_widgets[i] = gtk_check_button_new_with_label (script->arg_labels[i]);
	  gtk_check_button_set_active (GTK_CHECK_BUTTON (script->args_widgets[i]),
				       script->arg_values[i].sfa_toggle);
	  g_signal_connect (script->args_widgets[i], "toggled",
			    G_CALLBACK (script_fu_toggle_update),
			    &script->arg_values[i].sfa_toggle);
	  break;

	case SF_VALUE:
	  script->args_widgets[i] = gtk_entry_new ();
	  gtk_widget_set_size_request (script->args_widgets[i], TEXT_WIDTH, -1);
	  gtk_editable_set_text (GTK_EDITABLE (script->args_widgets[i]),
				 script->arg_defaults[i].sfa_value);
	  break;
	default:
	  break;
	}

      if (script->args_widgets[i])
	gimp_table_attach (table, script->args_widgets[i],
			   1, 2, i, i + 1, 0, 0, 4, 2);
    }

  /*  The current command  */
  label = gtk_label_new ("Current Command");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), label, FALSE, TRUE, 0);
  sf_interface.cc = gtk_entry_new ();
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), sf_interface.cc, FALSE, TRUE, 0);

  sf_interface.script = script;
  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  sf_interface.cc = NULL;
  g_free (script->args_widgets);
  script->args_widgets = NULL;
}

static void
script_fu_color_preview (GtkWidget *preview,
			 gdouble   *color)
{
  if (preview == NULL)
    return;

  gimp_preview_fill (GIMP_PREVIEW (preview),
		     (guchar) (255.999 * color[0]),
		     (guchar) (255.999 * color[1]),
		     (guchar) (255.999 * color[2]));
}

static void
script_fu_ok_callback (GtkWidget *widget,
		       gpointer   data)
{
  GtkWidget *dlg = GTK_WIDGET (data);
  SFScript *script;
  gint err_msg;
  GString *cmd;
  char *command;
  int i;

  if ((script = sf_interface.script) == NULL)
    return;

  /*  built with a GString: no fixed buffers for the values, whatever the
   *  script's defaults or the user typed
   */
  cmd = g_string_new ("(");
  g_string_append (cmd, script->script_name);

  for (i = 0; i < script->num_args; i++)
    {
      g_string_append_c (cmd, ' ');

      switch (script->arg_types[i])
	{
	case SF_IMAGE:
	case SF_DRAWABLE:
	case SF_LAYER:
	case SF_CHANNEL:
	  g_string_append_printf (cmd, "%d", script->arg_values[i].sfa_image);
	  break;
	case SF_COLOR:
	  g_string_append_printf (cmd, "'(%d %d %d)",
				  (gint32) (script->arg_values[i].sfa_color.color[0] * 255.999),
				  (gint32) (script->arg_values[i].sfa_color.color[1] * 255.999),
				  (gint32) (script->arg_values[i].sfa_color.color[2] * 255.999));
	  break;
	case SF_TOGGLE:
	  g_string_append (cmd, (script->arg_values[i].sfa_toggle) ? "TRUE" : "FALSE");
	  break;
	case SF_VALUE:
	  g_string_append (cmd, gtk_editable_get_text (GTK_EDITABLE (script->args_widgets[i])));
	  break;
	default:
	  break;
	}
    }

  g_string_append_c (cmd, ')');
  command = g_string_free (cmd, FALSE);

  /*  The dialog stays up showing the current command while the script
   *  runs, but takes no input.
   */
  script_fu_running = TRUE;
  gtk_widget_set_sensitive (gimp_dialog_get_action_area (dlg), FALSE);
  for (i = 0; i < script->num_args; i++)
    if (script->args_widgets[i])
      gtk_widget_set_sensitive (script->args_widgets[i], FALSE);

  /*  enable the current command field  */
  script_fu_enable_cc ();

  /*  run the command through the interpreter  */
  err_msg = (repl_c_string (command, 0, 0, 1) != 0) ? TRUE : FALSE;

  /*  disable the current command field  */
  script_fu_disable_cc (err_msg);

  script_fu_running = FALSE;

  g_free (command);

  /*  Closing the dialog ends its main loop (script_fu_close_callback)  */
  gtk_window_destroy (GTK_WINDOW (dlg));
}

static void
script_fu_close_callback (GtkWidget *widget,
			  gpointer   data)
{
  gimp_main_loop_quit ();
}

static void
script_fu_menu_callback  (gint32     id,
			  gpointer   data)
{
  *((gint32 *) data) = id;
}

static void
script_fu_toggle_update (GtkWidget *widget,
			 gpointer   data)
{
  int *toggle_val;

  toggle_val = (int *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    *toggle_val = TRUE;
  else
    *toggle_val = FALSE;
}

static void
script_fu_preview_callback (GtkWidget *widget,
			    gpointer   data)
{
  SFColor *color;
  guchar   rgb[3];
  GtkRoot *root;

  color = (SFColor *) data;

  rgb[0] = (guchar) (color->color[0] * 255.999);
  rgb[1] = (guchar) (color->color[1] * 255.999);
  rgb[2] = (guchar) (color->color[2] * 255.999);

  root = gtk_widget_get_root (widget);

  gimp_color_dialog_run (GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL,
			 "Script-Fu Color Picker", rgb,
			 script_fu_color_chosen, color);
}

static void
script_fu_color_chosen (const guchar *rgb,
			gpointer      data)
{
  SFColor *color;

  color = (SFColor *) data;

  /*  The dialog the button was in may be gone already  */
  if (color->preview == NULL)
    return;

  color->color[0] = rgb[0] / 255.0;
  color->color[1] = rgb[1] / 255.0;
  color->color[2] = rgb[2] / 255.0;

  script_fu_color_preview (color->preview, color->color);
}
