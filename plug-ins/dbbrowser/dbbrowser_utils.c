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


/*
   dbbrowser_utils.c
   0.08  26th sept 97  by Thomas NOEL <thomas@minet.net>
*/

#include <stdlib.h>
#include <string.h>

#include "dbbrowser_utils.h"

GList *proc_table;

static int
compare_proc_names (const void *a,
		    const void *b);

GtkWidget*
gimp_db_browser(void (* apply_callback) ( gchar     *selected_proc_name,
					  gchar     *selected_scheme_proc_name,
					  gchar     *selected_proc_blurb,
					  gchar     *selected_proc_help,
					  gchar     *selected_proc_author,
					  gchar     *selected_proc_copyright,
					  gchar     *selected_proc_date,
					  int        selected_proc_type,
					  int        selected_nparams,
					  int        selected_nreturn_vals,
					  GParamDef *selected_params,
					  GParamDef *selected_return_vals ) )
  /* create the dialog box */
  /* console_entry != NULL => called from the script-fu-console */

{

  dbbrowser_t* dbbrowser;

  GtkWidget *button;
  GtkWidget *hbox,*searchhbox,*vbox;
  GtkWidget *label;

  dbbrowser = g_new0 (dbbrowser_t, 1);

  dbbrowser->apply_callback = apply_callback;

  /* the dialog box */

  dbbrowser->dlg = gimp_dialog_new ("DB Browser (init)");
  g_signal_connect (dbbrowser->dlg, "destroy",
                    G_CALLBACK (dialog_close_callback),
                    dbbrowser);

  /* hbox : left=list ; right=description */

  hbox = gimp_hbox_new(FALSE, 0);
  gimp_box_pack_start (gimp_dialog_get_vbox (dbbrowser->dlg),
		      hbox, TRUE, TRUE, 0);

  /* left = vbox : the list and the search entry */

  vbox = gimp_vbox_new( FALSE, 0 );
  gimp_container_set_border_width (vbox, 3);
  gimp_box_pack_start (hbox,
		      vbox, FALSE, TRUE, 0);

  /* list : list in a scrolled_win */

  dbbrowser->clist = gtk_list_box_new ();
  dbbrowser->scrolled_win = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (dbbrowser->scrolled_win),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (dbbrowser->clist),
				   GTK_SELECTION_BROWSE);

  gtk_widget_set_size_request (dbbrowser->scrolled_win, DBL_LIST_WIDTH, DBL_HEIGHT);
  g_signal_connect (dbbrowser->clist, "row-selected",
		    G_CALLBACK (procedure_select_callback),
		    dbbrowser);
  gimp_box_pack_start (vbox, dbbrowser->scrolled_win, TRUE, TRUE, 0);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (dbbrowser->scrolled_win),
				 dbbrowser->clist);

  /* search entry */

  searchhbox = gimp_hbox_new(FALSE,0);
  gimp_box_pack_start (vbox,
		      searchhbox, FALSE, TRUE, 0);

  label = gtk_label_new("Search :");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_box_pack_start (searchhbox,
		      label, TRUE, TRUE, 0);

  dbbrowser->search_entry = gtk_entry_new();
  gimp_box_pack_start (searchhbox,
		      dbbrowser->search_entry, TRUE, TRUE, 0);

  /* right = description */

  dbbrowser->descr_scroll = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (dbbrowser->descr_scroll),
				  GTK_POLICY_ALWAYS,
				  GTK_POLICY_ALWAYS
				  );
  gimp_box_pack_start (hbox,
		      dbbrowser->descr_scroll, TRUE, TRUE, 0);
  gtk_widget_set_size_request (dbbrowser->descr_scroll, DBL_WIDTH - DBL_LIST_WIDTH, -1);

  /* buttons in dlg->action_aera */

  gimp_container_set_border_width (gimp_dialog_get_action_area (dbbrowser->dlg), 0);

  dbbrowser->name_button = gtk_button_new_with_label ("Search by name");
  g_signal_connect (dbbrowser->name_button, "clicked",
                    G_CALLBACK (dialog_search_callback), dbbrowser);
  gimp_box_pack_start (gimp_dialog_get_action_area (dbbrowser->dlg),
		      dbbrowser->name_button , TRUE, TRUE, 0);

  dbbrowser->blurb_button = gtk_button_new_with_label ("Search by blurb");
  g_signal_connect (dbbrowser->blurb_button, "clicked",
                    G_CALLBACK (dialog_search_callback), dbbrowser);
  gimp_box_pack_start (gimp_dialog_get_action_area (dbbrowser->dlg),
		      dbbrowser->blurb_button , TRUE, TRUE, 0);

  if (apply_callback) {
    button = gtk_button_new_with_label ("Apply");
    g_signal_connect (button, "clicked",
		      G_CALLBACK (dialog_apply_callback), dbbrowser );
    gimp_box_pack_start (gimp_dialog_get_action_area (dbbrowser->dlg),
			button, TRUE, TRUE, 0);
  }

  button = gtk_button_new_with_label ("Close");
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dbbrowser->dlg);
  gimp_box_pack_start (gimp_dialog_get_action_area (dbbrowser->dlg),
		      button, TRUE, TRUE, 0);


  /* now build the list */

  gtk_window_present (GTK_WINDOW (dbbrowser->dlg));

  /* initialize the "return" value (for "apply") */

  dbbrowser->descr_table = NULL;
  dbbrowser->selected_proc_name = NULL;
  dbbrowser->selected_scheme_proc_name = NULL;
  dbbrowser->selected_proc_blurb = NULL;
  dbbrowser->selected_proc_help = NULL;
  dbbrowser->selected_proc_author = NULL;
  dbbrowser->selected_proc_copyright = NULL;
  dbbrowser->selected_proc_date = NULL;
  dbbrowser->selected_proc_type = 0;
  dbbrowser->selected_nparams = 0;
  dbbrowser->selected_nreturn_vals = 0;
  dbbrowser->selected_params = NULL;
  dbbrowser->selected_return_vals = NULL;

  /* first search (all procedures) */
  dialog_search_callback( NULL, (gpointer)dbbrowser );

  return dbbrowser->dlg;
}


static void
procedure_select_callback (GtkListBox    *list,
			   GtkListBoxRow *row,
			   gpointer       data)
{
  dbbrowser_t *dbbrowser = data;
  gchar *func;

  g_return_if_fail (dbbrowser != NULL);

  if (row == NULL)
    return;

  if ((func = (gchar *) g_object_get_data (G_OBJECT (row), "func")))
      dialog_select (dbbrowser, func);
}

static void
dialog_select (dbbrowser_t *dbbrowser,
	       gchar       *proc_name)
  /* update the description box (right) */
{
  GtkWidget* label;
  gint i,row=0;

  if (dbbrowser->selected_proc_name)
    g_free(dbbrowser->selected_proc_name);
  dbbrowser->selected_proc_name = g_strdup(proc_name);

  if (dbbrowser->selected_scheme_proc_name)
    g_free(dbbrowser->selected_scheme_proc_name);
  dbbrowser->selected_scheme_proc_name =
    g_strdup(proc_name);
  convert_string(dbbrowser->selected_scheme_proc_name);

  if (dbbrowser->selected_proc_blurb) g_free(dbbrowser->selected_proc_blurb);
  if (dbbrowser->selected_proc_help) g_free(dbbrowser->selected_proc_help);
  if (dbbrowser->selected_proc_author) g_free(dbbrowser->selected_proc_author);
  if (dbbrowser->selected_proc_copyright) g_free(dbbrowser->selected_proc_copyright);
  if (dbbrowser->selected_proc_date) g_free(dbbrowser->selected_proc_date);
  if (dbbrowser->selected_params) g_free(dbbrowser->selected_params);
  if (dbbrowser->selected_return_vals) g_free(dbbrowser->selected_return_vals);

  gimp_query_procedure (proc_name,
			&(dbbrowser->selected_proc_blurb),
			&(dbbrowser->selected_proc_help),
			&(dbbrowser->selected_proc_author),
			&(dbbrowser->selected_proc_copyright),
			&(dbbrowser->selected_proc_date),
			&(dbbrowser->selected_proc_type),
			&(dbbrowser->selected_nparams),
			&(dbbrowser->selected_nreturn_vals),
			&(dbbrowser->selected_params),
			&(dbbrowser->selected_return_vals));

  dbbrowser->descr_table = gimp_table_new(
       10 + dbbrowser->selected_nparams + dbbrowser->selected_nreturn_vals ,
       5 , FALSE );

  gtk_grid_set_column_spacing (GTK_GRID (dbbrowser->descr_table), 3);

  /* show the name */

  label = gtk_label_new("Name :");
  gimp_misc_set_alignment (label, 1.0, 0.5);
  gimp_table_attach (dbbrowser->descr_table, label,
		    0, 1, row, row+1, GIMP_FILL, GIMP_FILL, 3, 6);

  label = gtk_entry_new();
  gtk_editable_set_text (GTK_EDITABLE (label),dbbrowser->selected_scheme_proc_name);
  gtk_editable_set_editable (GTK_EDITABLE (label), FALSE);
  gimp_table_attach (dbbrowser->descr_table, label,
		    1, 4, row, row+1, GIMP_FILL, GIMP_FILL, 0, 0);
  row++;

  /* show the description */

  label = gtk_label_new("Blurb :");
  gimp_misc_set_alignment (label, 1.0, 0.5);
  gimp_table_attach (dbbrowser->descr_table, label,
		    0, 1, row, row+1, GIMP_FILL, GIMP_FILL, 3, 0);

  label = gtk_label_new(dbbrowser->selected_proc_blurb);
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (dbbrowser->descr_table, label,
		    1, 4, row, row+1, GIMP_FILL, GIMP_FILL, 0, 0);
  row++;

  label = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL); /* ok, not really a label ... :) */
  gimp_table_attach (dbbrowser->descr_table, label,
		    0, 4, row, row+1, GIMP_FILL, GIMP_FILL, 3, 6);
  row++;

  /* in parameters */
  if (dbbrowser->selected_nparams)
    {
      label = gtk_label_new("In :");
      gimp_misc_set_alignment (label, 1.0, 0.5);
      gimp_table_attach (dbbrowser->descr_table, label,
	   0, 1, row, row+(dbbrowser->selected_nparams),
	   GIMP_FILL, GIMP_FILL, 3, 0);
      for (i=0;i<(dbbrowser->selected_nparams);i++)
	{
	  /* name */
	  label = gtk_label_new((dbbrowser->selected_params[i]).name);
	  gimp_misc_set_alignment (label, 0.0, 0.5);
	  gimp_table_attach (dbbrowser->descr_table, label,
			    1, 2, row, row+1, GIMP_FILL, GIMP_FILL, 0, 0);

	  /* type */
	  label = gtk_label_new(GParamType2char((dbbrowser->selected_params[i]).type));
	  gimp_misc_set_alignment (label, 0.0, 0.5);
	  gimp_table_attach (dbbrowser->descr_table, label,
			    2, 3, row, row+1, GIMP_FILL, GIMP_FILL, 0, 0);

	  /* description */
	  label = gtk_label_new((dbbrowser->selected_params[i]).description);
	  gimp_misc_set_alignment (label, 0.0, 0.0);
	  gimp_table_attach (dbbrowser->descr_table, label,
			    3, 4, row, row+1, GIMP_FILL, GIMP_FILL, 0, 0);

	  row++;
	}
    }

  if ((dbbrowser->selected_nparams) &&
      (dbbrowser->selected_nreturn_vals)) {
    label = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL); /* ok, not really a label ... :) */
    gimp_table_attach (dbbrowser->descr_table, label,
		      0, 4, row, row+1,
		      GIMP_FILL, GIMP_FILL, 3, 6);
    row++;
  }

  /* out parameters */
  if (dbbrowser->selected_nreturn_vals)
    {
      label = gtk_label_new("Out :");
      gimp_misc_set_alignment (label, 1.0, 0.5);
      gimp_table_attach (dbbrowser->descr_table, label,
			0, 1, row, row+(dbbrowser->selected_nreturn_vals),
			GIMP_FILL, GIMP_FILL, 3, 0);
      for (i=0;i<(dbbrowser->selected_nreturn_vals);i++)
	{
	  /* name */
	  label = gtk_label_new((dbbrowser->selected_return_vals[i]).name);
	  gimp_misc_set_alignment (label, 0.0, 0.5);
	  gimp_table_attach (dbbrowser->descr_table, label,
			    1, 2, row, row+1,
			    GIMP_FILL, GIMP_FILL, 0, 0);

	  /* type */
	  label = gtk_label_new(GParamType2char((dbbrowser->selected_return_vals[i]).type));
	  gimp_misc_set_alignment (label, 0.0, 0.5);
	  gimp_table_attach (dbbrowser->descr_table, label,
			    2, 3, row, row+1,
			    GIMP_FILL, GIMP_FILL, 0, 0);

	  /* description */
	  label = gtk_label_new((dbbrowser->selected_return_vals[i]).description);
	  gimp_misc_set_alignment (label, 0.0, 0.5);
	  gimp_table_attach (dbbrowser->descr_table, label,
			    3, 4, row, row+1,
			    GIMP_FILL, GIMP_FILL, 0, 0);
	  row++;

	}
    }

  /* show the author & the copyright */

  if ((dbbrowser->selected_nparams) ||
      (dbbrowser->selected_nreturn_vals)) {
    label = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL); /* ok, not really a label ... :) */
    gimp_table_attach (dbbrowser->descr_table, label,
		      0, 4, row, row+1,
		      GIMP_FILL, GIMP_FILL, 3, 6);
    row++;
  }

  label = gtk_label_new("Author :");
  gimp_misc_set_alignment (label, 1.0, 0.5);
  gimp_table_attach (dbbrowser->descr_table, label,
		    0, 1, row, row+1,
		    GIMP_FILL, GIMP_FILL, 3, 0);

  label = gtk_label_new(dbbrowser->selected_proc_author);
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (dbbrowser->descr_table, label,
		    1, 4,  row, row+1,
		    GIMP_FILL, GIMP_FILL, 0, 0);
  row++;

  label = gtk_label_new("Date :");
  gimp_misc_set_alignment (label, 1.0, 0.5);
  gimp_table_attach (dbbrowser->descr_table, label,
		    0, 1, row, row+1,
		    GIMP_FILL, GIMP_FILL, 3, 0);

  label = gtk_label_new(dbbrowser->selected_proc_date);
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (dbbrowser->descr_table, label,
		    1, 4,  row, row+1,
		    GIMP_FILL, GIMP_FILL, 0, 0);
  row++;

  label = gtk_label_new("Copyright :");
  gimp_misc_set_alignment (label, 1.0, 0.5);
  gimp_table_attach (dbbrowser->descr_table, label,
		    0, 1, row, row+1,
		    GIMP_FILL, GIMP_FILL, 3, 0);

  label = gtk_label_new(dbbrowser->selected_proc_copyright);
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (dbbrowser->descr_table, label,
		    1, 4,  row, row+1,
		    GIMP_FILL, GIMP_FILL, 0, 0);
  row++;

  /*
  label = gtk_label_new("Help :");
  gimp_misc_set_alignment (label, 1.0, 0.5);
  gimp_table_attach (dbbrowser->descr_table, label,
		    0, 1, row, row+1,
		    GIMP_FILL, GIMP_FILL, 3, 0);

  TODO: Add help */

  /* setting the new child destroys the old table */
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (dbbrowser->descr_scroll),
				 dbbrowser->descr_table);
}

static void
dialog_close_callback (GtkWidget *widget,
		       gpointer   data)
     /* end of the dialog */
{
  dbbrowser_t* dbbrowser = data;

  /* called when the dialog box is destroyed (the Close button destroys it) */
  if (! dbbrowser->apply_callback) {
    /* we are in the plug_in : kill the gtk application */
    gimp_main_loop_quit ();
  }
}

static void
dialog_apply_callback (GtkWidget *widget,
		       gpointer   data)
     /* end of the dialog */
{
  dbbrowser_t* dbbrowser = data;

  (dbbrowser->apply_callback)( dbbrowser->selected_proc_name,
			       dbbrowser->selected_scheme_proc_name,
			       dbbrowser->selected_proc_blurb,
			       dbbrowser->selected_proc_help,
			       dbbrowser->selected_proc_author,
			       dbbrowser->selected_proc_copyright,
			       dbbrowser->selected_proc_date,
			       dbbrowser->selected_proc_type,
			       dbbrowser->selected_nparams,
			       dbbrowser->selected_nreturn_vals,
			       dbbrowser->selected_params,
			       dbbrowser->selected_return_vals );
}

static void
dialog_search_callback (GtkWidget *widget,
			gpointer   data)
     /* search in the whole db */
{
  char **proc_list;
  int num_procs;
  int i;
  dbbrowser_t* dbbrowser = data;
  gchar *func_name, *label;
  const gchar *query_text;
  GString *query;
  GtkListBox *list = GTK_LIST_BOX (dbbrowser->clist);
  GtkListBoxRow *row;
  GtkWidget *row_label;

  /* no selection callbacks while the list is rebuilt */
  g_signal_handlers_block_by_func (list, procedure_select_callback, dbbrowser);
  while ((row = gtk_list_box_get_row_at_index (list, 0)) != NULL)
    gtk_list_box_remove (list, GTK_WIDGET (row));

  /* search */

  if ( widget == (dbbrowser->name_button) )
    {
      gtk_window_set_title (GTK_WINDOW (dbbrowser->dlg),
			    "DB Browser (by name - please wait)");

      query = g_string_new ("");
      query_text = gtk_editable_get_text (GTK_EDITABLE (dbbrowser->search_entry));

      while (*query_text)
	{
	  if ((*query_text == '_') || (*query_text == '-'))
	    g_string_append (query, "[-_]");
	  else
	    g_string_append_c (query, *query_text);

	  query_text++;
	}

      gimp_query_database (query->str,
			   ".*", ".*", ".*", ".*", ".*", ".*",
			   &num_procs, &proc_list);

      g_string_free (query, TRUE);
    }
  else if ( widget == (dbbrowser->blurb_button) )
    {
      gtk_window_set_title (GTK_WINDOW (dbbrowser->dlg),
			    "DB Browser (by blurb - please wait)");
      gimp_query_database (".*",
			   (gchar *) gtk_editable_get_text (GTK_EDITABLE (dbbrowser->search_entry) ),
			   ".*", ".*", ".*", ".*", ".*",
			   &num_procs, &proc_list);
    }
  else {
      gtk_window_set_title (GTK_WINDOW (dbbrowser->dlg),
			    "DB Browser (please wait)");
      gimp_query_database (".*", ".*", ".*", ".*", ".*", ".*", ".*",
			   &num_procs, &proc_list);
  }

  /* the list is shown sorted by name */
  qsort (proc_list, num_procs, sizeof (char *), compare_proc_names);

  for (i = 0; i < num_procs; i++) {
    label = g_strdup(proc_list[i]);
    convert_string(label);
    row_label = gtk_label_new (label);
    gtk_label_set_xalign (GTK_LABEL (row_label), 0.0);
    g_free (label);

    row = GTK_LIST_BOX_ROW (gtk_list_box_row_new ());
    gtk_list_box_row_set_child (row, row_label);
    func_name = g_strdup (proc_list[i]);
    g_object_set_data_full (G_OBJECT (row), "func", func_name, g_free);
    gtk_list_box_append (list, GTK_WIDGET (row));
  }

  g_signal_handlers_unblock_by_func (list, procedure_select_callback, dbbrowser);

  if (num_procs > 0)
    gtk_list_box_select_row (list, gtk_list_box_get_row_at_index (list, 0));

  /*
  if (num_procs != 0) {
    gchar *insert_name, *label_name;
    int i,j,savej;

    for (i = 0; i < num_procs ; i++) {

      insert_name=g_strdup(proc_list[0]); savej=0;
      for (j = 0; j < num_procs ; j++) {
	if (strcmp(proc_list[j],insert_name)<0) {
	  g_free(insert_name);
	  insert_name=g_strdup(proc_list[j]);
	  savej=j;
	}
      }

      proc_list[savej][0]='\255';

      label_name = g_strdup( insert_name );
      convert_string( label_name );
      gtk_clist_append (GTK_CLIST (dbbrowser->clist), &label_name);

      if (i==0) dialog_select( dbbrowser , insert_name );

      g_free(label_name);
    }
  }
  */

  if ( dbbrowser->clist ) {
    ;
  }

  g_free( proc_list );

  gtk_window_set_title (GTK_WINDOW (dbbrowser->dlg),
			"DB Browser");

}

/* utils ... */

static int
compare_proc_names (const void *a,
		    const void *b)
{
  return strcmp (*(char * const *) a, *(char * const *) b);
}

static void
convert_string (char *str)
{
  while (*str)
    {
      if (*str == '_') *str = '-';
      str++;
    }
}

static char*
GParamType2char(GParamType t)
{
  switch (t) {
  case PARAM_INT32: return "INT32";
  case PARAM_INT16: return "INT16";
  case PARAM_INT8: return "INT8";
  case PARAM_FLOAT: return "FLOAT";
  case PARAM_STRING: return "STRING";
  case PARAM_INT32ARRAY: return "INT32ARRAY";
  case PARAM_INT16ARRAY: return "INT16ARRAY";
  case PARAM_INT8ARRAY: return "INT8ARRAY";
  case PARAM_FLOATARRAY: return "FLOATARRAY";
  case PARAM_STRINGARRAY: return "STRINGARRAY";
  case PARAM_COLOR: return "COLOR";
  case PARAM_REGION: return "REGION";
  case PARAM_DISPLAY: return "DISPLAY";
  case PARAM_IMAGE: return "IMAGE";
  case PARAM_LAYER: return "LAYER";
  case PARAM_CHANNEL: return "CHANNEL";
  case PARAM_DRAWABLE: return "DRAWABLE";
  case PARAM_SELECTION: return "SELECTION";
  case PARAM_BOUNDARY: return "BOUNDARY";
  case PARAM_PATH: return "PATH";
  case PARAM_STATUS: return "STATUS";
  case PARAM_END: return "END";
  default: return "UNKNOWN?";
  }
}
