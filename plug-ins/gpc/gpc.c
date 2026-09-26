/****************************************************************************
 * This is a convenience library for plugins for the GIMP v 0.99.8 or later.
 * Documentation is available at http://www.rru.com/~meo/gimp/ .
 *
 * Copyright (C) 1997, 1998 Miles O'Neal <meo@rru.com> http://www.rru.com/~meo/
 * GUI may include GTK code from:
 *    alienmap (Copyright (C) 1996, 1997 Daniel Cotting)
 *    plasma   (Copyright (C) 1996 Stephen Norris),
 *    oilify   (Copyright (C) 1996 Torsten Martinsen),
 *    ripple   (Copyright (C) 1997 Brian Degenhardt) and
 *    whirl    (Copyright (C) 1997 Federico Mena Quintero).
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
 *
 ****************************************************************************/

/****************************************************************************
 * gpc: GTK Plug-in Convenience library
 *
 * history
 *     1.4 - 30 Apr 1998 MEO
 *         added man page
 *     1.3 - 29 Apr 1998 MEO
 *         GTK 1.0 port (minor tooltips change)
 *         restored tooltips to action buttons
 *     1.2 - 11 Feb 1998 MEO
 *         added basic comments
 *     1.1 -  3 Feb 1998 MEO
 *         removed tooltips from action buttons
 *     1.0 -  2 Feb 1998 MEO
 *         FCS
 *
 * Please send any patches or suggestions to the author: meo@rru.com .
 *
 ****************************************************************************/

/*
 * gimp42: ported to GTK 4.  Tooltips are plain widget tooltips (the
 * theme picks their colours), tables are the GtkGrids of gimpwidgets.h
 * and radio groups are grouped check buttons; the GSList a caller keeps
 * for a group holds the buttons already in it.
 */

#include <stdlib.h>
#include <time.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"
#include "gpc.h"


/*
 *  TOGGLE UPDATE callback - toggles the TOGGLE widget's data
 */
static void
gpc_toggle_update(GtkWidget *widget, gpointer data) {
    int *toggle_val;

    toggle_val = (int *) data;

    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)))
      *toggle_val = TRUE;
    else
      *toggle_val = FALSE;
}
/*
 *  DESTROY callback - quit this plug-in
 */
void
gpc_close_callback(GtkWidget *widget, gpointer data) {
    gimp_main_loop_quit();
}

/*
 *  CANCEL BUTTON callback - go away without saving state, etc.
 */
void
gpc_cancel_callback(GtkWidget *widget, gpointer data) {
    gtk_window_destroy(GTK_WINDOW(data));
}

/*
 *  SCALE UPDATE callback - update the SCALE widget's data
 */
void
gpc_scale_update(GtkAdjustment *adjustment, double *scale_val) {
    *scale_val = gtk_adjustment_get_value(adjustment);
}


/*
 *  TEXT UPDATE callback - update the TEXT widget's data
 */
void
gpc_text_update(GtkWidget *widget, gpointer data) {
  gint *text_val;

  text_val = (gint *) data;

  *text_val = atoi(gtk_editable_get_text(GTK_EDITABLE(widget)));
}



/*
 *  TOOLTIPS ROUTINES
 */

/*
 *  TOOLTIP INITIALIZATION
 *
 *  GTK 4 tooltips need no setup and take their colours from the theme.
 */
void
gpc_setup_tooltips(GtkWidget *parent)
{
}


/*
 *  SET TOOLTIP for a widget
 */
void
gpc_set_tooltip(GtkWidget *widget, const char *tip)
{
    if (tip && tip[0])
        gtk_widget_set_tooltip_text(widget, tip);
}


/*
 *  ADD ACTION BUTTON to a dialog
 */
void
gpc_add_action_button(char *label, GCallback callback, GtkWidget *dialog,
    char *tip)
{
    GtkWidget *button;

    button = gimp_dialog_add_button(dialog, label, callback, dialog, TRUE);
    gpc_set_tooltip(button, tip);
}


/*
 *  ADD RADIO BUTTON to a dialog
 */
void
gpc_add_radio_button(GSList **group, char *label, GtkWidget *box,
    gint *value, char *tip)
{
    GtkWidget *toggle;

    toggle = gimp_radio_button_new(*group ? GTK_WIDGET((*group)->data) : NULL,
        label);
    *group = g_slist_prepend(*group, toggle);
    gtk_box_append(GTK_BOX(box), toggle);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(toggle), *value);
    g_signal_connect(toggle, "toggled",
        G_CALLBACK(gpc_toggle_update), value);
    gpc_set_tooltip(toggle, tip);
}


/*
 *  ADD LABEL widget to a dialog at given location
 */
void
gpc_add_label(char *value, GtkWidget *table, int left, int right,
    int top, int bottom)
{
    GtkWidget *label;

    label = gtk_label_new(value);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_yalign(GTK_LABEL(label), 0.5);
    gimp_table_attach(table, label, left, right, top, bottom,
        GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 5, 0);
}


/*
 *  ADD HORIZONTAL SCALE widget to a dialog at given location
 */
void
gpc_add_hscale(GtkWidget *table, int width, float low, float high,
    gdouble *val, int left, int right, int top, int bottom, char *tip)
{
    GtkWidget *scale;
    GtkAdjustment *scale_data;

    scale_data = gtk_adjustment_new(*val, low, high, 1.0, 1.0, 0.0);
    scale = gimp_hscale_new(scale_data, 0);
    gtk_widget_set_size_request(scale, width, -1);
    gimp_table_attach(table, scale, left, right, top, bottom,
        GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);
    g_signal_connect(scale_data, "value-changed",
        G_CALLBACK(gpc_scale_update), val);
    gpc_set_tooltip(scale, tip);
}

