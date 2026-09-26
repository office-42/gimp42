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
#include <string.h>
#include "appenv.h"
#include "gimprc.h"
#include "info_dialog.h"
#include "interface.h"

/*  static functions  */
static InfoField * info_field_new (InfoDialog *, char *, char *);
static void        update_field (InfoField *);
static gboolean    info_dialog_delete_callback (GtkWindow *, gpointer);

static InfoField *
info_field_new (InfoDialog *idialog,
		char       *title,
		char       *text_ptr)
{
  GtkWidget *label;
  InfoField *field;

  field = (InfoField *) g_malloc (sizeof (InfoField));

  label = gtk_label_new (title);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_label_set_yalign (GTK_LABEL (label), 0.5);
  gtk_box_append (GTK_BOX (idialog->labels), label);

  field->w = gtk_label_new (text_ptr);
  gtk_label_set_xalign (GTK_LABEL (field->w), 0.0);
  gtk_label_set_yalign (GTK_LABEL (field->w), 0.5);
  gtk_box_append (GTK_BOX (idialog->values), field->w);

  field->text_ptr = text_ptr;

  return field;
}

static void
update_field (InfoField *field)
{
  const gchar *old_text;

  /*  only update the field if its new value differs from the old  */
  old_text = gtk_label_get_text (GTK_LABEL (field->w));

  if (strcmp (old_text, field->text_ptr))
    {
      /* set the new value and update somehow */
      gtk_label_set_text (GTK_LABEL (field->w), field->text_ptr);
    }
}

/*  function definitions  */

InfoDialog *
info_dialog_new (char *title)
{
  InfoDialog * idialog;
  GtkWidget *shell;
  GtkWidget *vbox;
  GtkWidget *labels, *values;
  GtkWidget *info_area;

  idialog = (InfoDialog *) g_malloc (sizeof (InfoDialog));
  idialog->field_list = NULL;
  idialog->user_data = NULL;

  /*  The window position (info_x, info_y from gimprc) can no longer be
   *  chosen by the application in GTK 4.
   */
  shell = gimp_dialog_new (title);
  gtk_window_set_hide_on_close (GTK_WINDOW (shell), FALSE);

  g_signal_connect (shell, "close-request",
		    G_CALLBACK (info_dialog_delete_callback),
		    idialog);

  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, 1);
  gimp_box_pack_start (gimp_dialog_get_vbox (shell), vbox, TRUE, TRUE, 0);

  info_area = gimp_hbox_new (FALSE, 1);
  gimp_container_set_border_width (info_area, 5);
  gimp_box_pack_start (vbox, info_area, TRUE, TRUE, 0);

  labels = gimp_vbox_new (FALSE, 1);
  gimp_box_pack_start (info_area, labels, TRUE, TRUE, 0);

  values = gimp_vbox_new (FALSE, 1);
  gimp_box_pack_start (info_area, values, TRUE, TRUE, 0);

  idialog->shell = shell;
  idialog->vbox = vbox;
  idialog->info_area = info_area;
  idialog->labels = labels;
  idialog->values = values;

  return idialog;
}

void
info_dialog_free (InfoDialog *idialog)
{
  GSList *list;

  if (!idialog)
    return;

  /*  Free each item in the field list  */
  list = idialog->field_list;

  while (list)
    {
      g_free (list->data);
      list = g_slist_next (list);
    }

  /*  Free the actual field linked list  */
  g_slist_free (idialog->field_list);

  /*  Destroy the associated widgets  */
  gtk_window_destroy (GTK_WINDOW (idialog->shell));

  /*  Free the info dialog memory  */
  g_free (idialog);
}

void
info_dialog_add_field (InfoDialog *idialog,
		       char       *title,
		       char       *text_ptr)
{
  InfoField * new_field;

  if (!idialog)
    return;

  new_field = info_field_new (idialog, title, text_ptr);
  idialog->field_list = g_slist_prepend (idialog->field_list, (void *) new_field);
}

void
info_dialog_popup (InfoDialog *idialog)
{
  if (!idialog)
    return;

  if (!gtk_widget_get_visible (idialog->shell))
    gtk_window_present (GTK_WINDOW (idialog->shell));
}

void
info_dialog_popdown (InfoDialog *idialog)
{
  if (!idialog)
    return;

  if (gtk_widget_get_visible (idialog->shell))
    gtk_widget_set_visible (idialog->shell, FALSE);
}

void
info_dialog_update (InfoDialog *idialog)
{
  GSList *list;

  if (!idialog)
    return;

  list = idialog->field_list;

  while (list)
    {
      update_field ((InfoField *) list->data);
      list = g_slist_next (list);
    }
}

static gboolean
info_dialog_delete_callback (GtkWindow *w,
			     gpointer   client_data)
{
  info_dialog_popdown ((InfoDialog *) client_data);

  return TRUE;
}
