/**************************************************
 * file: megawidget/megawidget.c
 *
 * Copyright (c) 1997 Eric L. Hernes (erich@rrnet.com)
 * All rights reserved.
 *
 * !!
 * !! This file has been hacked to work with the flame plug-in
 * !!
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software withough specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id$ */

/* Functions added by Xavier Bouchoux (Xavier.Bouchoux@ensimag.imag.fr)
 *  mw_value_radio_group_new()
 *     -->  it modifies a user variable to a value associated with the current
 *          button 
 *  mw_ientry_button_new()
 *  mw_fentry_button_new()
 *     --->  Guess...
 *  mw_color_select_button_create()
 *     --> Creates a colored button, wich creates a colorselection window 
 *         (just like ifs compose)
 */


#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "megawidget.h"


static void mw_scale_entry_new(GtkWidget *table, gchar *name,
                               gfloat defval, gfloat lorange,
                               gfloat hirange, gfloat st_inc,
                               gfloat pg_inc, gfloat pgsiz,
                               gpointer variablep, gchar *fmtvar,
                               gint left_a, gint right_a, gint top_a,
                               gint bottom_a, GCallback scale_cb,
                               GCallback entry_cb);
static void ui_ok_callback(GtkWidget *widget, gpointer data);
static void ui_close_callback(GtkWidget *widget, gpointer data);
static void ui_fscale_callback(GtkAdjustment *adj, gpointer data);
static void ui_fentry_callback(GtkEditable *widget, gpointer data);
static void ui_iscale_callback(GtkAdjustment *adj, gpointer data);
static void ui_ientry_callback(GtkEditable *widget, gpointer data);


mw_callback mw_update_cb;

#ifndef NO_PREVIEW
static mw_preview_t *mw_do_preview = NULL;
static gint do_preview = 1;
#endif

GtkWidget *
mw_app_new(gchar *resname, gchar *appname, gint *runpp){
   GtkWidget *dlg;
   GtkWidget *button;

   *runpp = 0;

   gtk_init();

   dlg = gimp_dialog_new(appname);
   g_object_set_data(G_OBJECT(dlg), "runp", runpp);

   g_signal_connect(dlg, "destroy",
                    G_CALLBACK (ui_close_callback),
                    NULL);

   gimp_dialog_add_button(dlg, "OK", G_CALLBACK (ui_ok_callback),
                          dlg, TRUE);

   button = gimp_dialog_add_button(dlg, "Cancel", NULL, NULL, FALSE);
   g_signal_connect_swapped(button, "clicked",
                            G_CALLBACK (gtk_window_destroy),
                            dlg);

   return dlg;
}

void
mw_fscale_entry_new(GtkWidget *table, gchar *name,
                    gfloat lorange, gfloat hirange,
                    gfloat st_inc, gfloat pg_inc, gfloat pgsiz,
                    gint left_a, gint right_a, gint top_a, gint bottom_a,
                    gdouble *var) {
   gchar buffer[40];

   g_snprintf(buffer, sizeof(buffer), "%0.3f", *var);
   mw_scale_entry_new(table, name, *var, lorange, hirange,
                      st_inc, pg_inc, pgsiz, var, buffer,
                      left_a,  right_a,  top_a,  bottom_a,
                      G_CALLBACK (ui_fscale_callback),
                      G_CALLBACK (ui_fentry_callback));
}

void
mw_iscale_entry_new(GtkWidget *table, gchar *name,
                    gint lorange, gint hirange,
                    gint st_inc, gint pg_inc, gint pgsiz,
                    gint left_a, gint right_a, gint top_a, gint bottom_a,
                    gint *varp) {
   gchar buffer[40];

   g_snprintf(buffer, sizeof(buffer), "%d", *varp);

   mw_scale_entry_new(table, name, (gfloat)*varp, (gfloat)lorange,
                      (gfloat)hirange, (gfloat)st_inc, (gfloat)pg_inc,
                      (gfloat)pgsiz, varp, buffer,
                      left_a,  right_a,  top_a,  bottom_a,
                      G_CALLBACK (ui_iscale_callback),
                      G_CALLBACK (ui_ientry_callback));
}

#ifndef NO_PREVIEW
struct mwPreview *
mw_preview_build_virgin(GDrawable *drw) {
   struct mwPreview *mwp;

   mwp = (struct mwPreview *)malloc(sizeof(struct mwPreview));
   if (drw->width > drw->height) {
      mwp->scale = (gdouble)drw->width/(gdouble)PREVIEW_SIZE;
      mwp->width = PREVIEW_SIZE;
      mwp->height = (drw->height)/(mwp->scale);
   } else {
      mwp->scale = (gdouble)drw->height/(gdouble)PREVIEW_SIZE;
      mwp->height = PREVIEW_SIZE;
      mwp->width = (drw->width)/(mwp->scale);
   }

   mwp->bpp = 3;
   mwp->bits = NULL;
   return(mwp);
}

struct mwPreview *
mw_preview_build(GDrawable *drw) {
   struct mwPreview *mwp;
   gint x, y, b;
   guchar *bc, *drwBits;
   GPixelRgn pr;

   mwp= mw_preview_build_virgin(drw);

   gimp_pixel_rgn_init(&pr, drw, 0, 0, drw->width, drw->height, FALSE, FALSE);
   drwBits = (guchar *)malloc(drw->width * drw->bpp);

   mwp->bpp = 3;
   bc = mwp->bits = (guchar *)malloc(mwp->width*mwp->height*mwp->bpp);
   for(y=0;y<mwp->height;y++) {
      gimp_pixel_rgn_get_row(&pr, drwBits, 0, (int)(y*mwp->scale), drw->width);
      for(x=0;x<mwp->width;x++) {
         for(b=0;b<mwp->bpp;b++)
            *bc++=*(drwBits+((gint)(x*mwp->scale)*drw->bpp)+b);
      }
   }
   free(drwBits);
   return(mwp);
}

static void
ui_toggle_callback(GtkCheckButton *button, gpointer data){
   gint *toggle_val = (gint *) data;

   *toggle_val = gtk_check_button_get_active(button);
}

GtkWidget *
mw_preview_new(GtkWidget *parent, struct mwPreview *mwp, mw_preview_t *fcn){
   GtkWidget *preview;
   GtkWidget *frame;
   GtkWidget *pframe;
   GtkWidget *vbox;
   GtkWidget *button;

   frame = gtk_frame_new("Preview");
   gimp_container_set_border_width(frame, 10);
   gimp_box_pack_start(parent, frame, FALSE, FALSE, 0);

   vbox = gimp_vbox_new(FALSE, 2);
   gimp_container_set_border_width(vbox, 2);
   gtk_frame_set_child(GTK_FRAME(frame), vbox);

   pframe = gtk_frame_new(NULL);
   gimp_container_set_border_width(pframe, 3);
   gimp_box_pack_start(vbox, pframe, FALSE, FALSE, 0);

   preview = gimp_preview_new(GIMP_PREVIEW_COLOR);
   gimp_preview_size(GIMP_PREVIEW(preview), mwp->width, mwp->height);
   gtk_frame_set_child(GTK_FRAME(pframe), preview);
   mw_do_preview = fcn;

   button=gtk_check_button_new_with_label("Do Preview");
   gtk_check_button_set_active(GTK_CHECK_BUTTON(button), do_preview);
   g_signal_connect(button, "toggled",
                    G_CALLBACK (ui_toggle_callback),
                    &do_preview);
   gimp_box_pack_start(vbox, button, FALSE, FALSE, 0);

   return preview;
}
#endif /* NO_PREVIEW */

/* internals */
static void
mw_scale_entry_new(GtkWidget *table, gchar *name,
                   gfloat defval, gfloat lorange, gfloat hirange,
                   gfloat st_inc, gfloat pg_inc, gfloat pgsiz,
                   gpointer variablep, gchar *fmtvar,
                   gint left_a, gint right_a, gint top_a, gint bottom_a,
                   GCallback scale_cb, GCallback entry_cb) {
   GtkWidget *label;
   GtkWidget *hbox;
   GtkWidget *scale;
   GtkWidget *entry;
   GtkAdjustment *adjustment;

   label = gtk_label_new(name);
   gtk_label_set_xalign(GTK_LABEL(label), 0.0);
   gtk_label_set_yalign(GTK_LABEL(label), 0.5);

   gimp_table_attach(table, label,
                     left_a, right_a, top_a, bottom_a,
                     GIMP_FILL, GIMP_FILL, 0, 0);

   hbox = gimp_hbox_new(FALSE, 5);
   gimp_table_attach(table, hbox,
                     left_a+1, right_a+1, top_a, bottom_a,
                     GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 0, 0);

   adjustment = gtk_adjustment_new(defval, lorange, hirange, st_inc,
                                   pg_inc, pgsiz);
   g_signal_connect(adjustment, "value-changed",
                    scale_cb,
                    variablep);

   scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
   gtk_widget_set_size_request(scale, 140, -1);
   gtk_scale_set_digits(GTK_SCALE(scale), 2);
   gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
   gimp_box_pack_start(hbox, scale, TRUE, TRUE, 0);

   entry = gtk_entry_new();
   g_object_set_data(G_OBJECT(entry), "user_data", adjustment);
   g_object_set_data(G_OBJECT(adjustment), "user_data", entry);
   gimp_box_pack_start(hbox, entry, FALSE, TRUE, 0);
   gtk_widget_set_size_request(entry, 75, -1);
   gtk_editable_set_width_chars(GTK_EDITABLE(entry), 6);

   gtk_editable_set_text(GTK_EDITABLE(entry), fmtvar);
   g_signal_connect(entry, "changed",
                    entry_cb,
                    variablep);
}



static void
ui_close_callback(GtkWidget *widget, gpointer data){
   gimp_main_loop_quit();
}

static void
ui_ok_callback(GtkWidget *widget, gpointer data){
   gint *rp;
   rp = g_object_get_data(G_OBJECT(data), "runp");
   *rp=1;
   gtk_window_destroy(GTK_WINDOW(data));
}

static void
ui_fscale_callback(GtkAdjustment *adj, gpointer data){
  GtkWidget *ent;
  double *nval;
  gchar buf[40];
  double value = gtk_adjustment_get_value(adj);

  nval = (double*)data;

  if (*nval != value) {
    *nval = value;
    ent = g_object_get_data(G_OBJECT(adj), "user_data");
    g_snprintf(buf, sizeof(buf), "%0.2f", value);

    g_signal_handlers_block_matched(ent, G_SIGNAL_MATCH_DATA,
                                    0, 0, NULL, NULL, data);
    gtk_editable_set_text(GTK_EDITABLE(ent), buf);
    g_signal_handlers_unblock_matched(ent, G_SIGNAL_MATCH_DATA,
                                      0, 0, NULL, NULL, data);

    if (mw_update_cb) (*mw_update_cb)(data);

#ifndef NO_PREVIEW
    if (do_preview && mw_do_preview!=NULL) (*mw_do_preview)(NULL);
#endif
  }
}

static void
ui_fentry_callback(GtkEditable *widget, gpointer data){
  GtkAdjustment *adjustment;
  double new_val;
  double *val;

  val = (double*)data;
  new_val = atof(gtk_editable_get_text(widget));

  if (*val != new_val) {
    adjustment = g_object_get_data(G_OBJECT(widget), "user_data");

    if ((new_val >= gtk_adjustment_get_lower(adjustment)) &&
	(new_val <= gtk_adjustment_get_upper(adjustment))) {
      *val = new_val;
      gtk_adjustment_set_value(adjustment, new_val);
    }
    if (mw_update_cb) (*mw_update_cb)(data);
#ifndef NO_PREVIEW
    if (do_preview && mw_do_preview!=NULL) (*mw_do_preview)(NULL);
#endif
  }
}

static void
ui_iscale_callback(GtkAdjustment *adj, gpointer data){
  GtkWidget *ent;
  gint *nval;
  gchar buf[40];
  gint value = (gint) gtk_adjustment_get_value(adj);

  nval = (gint*)data;

  if (*nval != value) {
    *nval = value;
    ent = g_object_get_data(G_OBJECT(adj), "user_data");
    g_snprintf(buf, sizeof(buf), "%d", value);
    gtk_editable_set_text(GTK_EDITABLE(ent), buf);
    if (mw_update_cb) (*mw_update_cb)(data);
#ifndef NO_PREVIEW
    if (do_preview && mw_do_preview!=NULL) (*mw_do_preview)(NULL);
#endif
  }
}

static void
ui_ientry_callback(GtkEditable *widget, gpointer data){
  GtkAdjustment *adj;
  gint new_val;
  gint *val;

  val = (gint *)data;
  new_val = strtol(gtk_editable_get_text(widget), NULL, 0);

  if (*val != new_val) {
    adj = g_object_get_data(G_OBJECT(widget), "user_data");

    if ((new_val >= gtk_adjustment_get_lower(adj)) &&
	(new_val <= gtk_adjustment_get_upper(adj))) {
      *val = new_val;
      gtk_adjustment_set_value(adj, new_val);
    }
    if (mw_update_cb) (*mw_update_cb)(data);
#ifndef NO_PREVIEW
    if (do_preview && mw_do_preview!=NULL) (*mw_do_preview)(NULL);
#endif
  }
}



/* end of megawidget/megawidget.c */
