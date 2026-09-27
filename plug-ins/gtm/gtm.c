/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * GTM plug-in --- GIMP Table Magic
 * Allows images to be saved as HTML tables with different colored cells.
 * It doesn't  have very much practical use other than being able to
 * easily design a table by "painting" it in GIMP, or to make small HTML
 * table images/icons.
 *
 * Copyright (C) 1997 Daniel Dunbar
 * Email: ddunbar@diads.com
 * WWW:   http://millennium.diads.com/gimp/
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

/* Version 1.0:
 * Once I first found out that it was possible to have pixel level control
 * of HTML tables I instantly realized that it would be possible, however
 * pointless, to save an image as a, albeit huge, HTML table.
 *
 * One night when I was feeling in an adventourously stupid programming mood
 * I decided to write a program to do it.
 *
 * At first I just wrote a really ugly hack to do it, which I then planned
 * on using once just to see how it worked, and then posting a URL and 
 * laughing about it on #gimp.  As it turns out, tigert thought it actually
 * had potential to be a useful plugin, so I started adding features and
 * and making a nice UI.
 *
 * It's still not very usefull, but I did manage to significantly improve my
 * C programming skills in the process, so it was worth it.
 *
 * If you happen to find it usefull I would appreciate any email about it.
 *                                     - Daniel Dunbar
 *                                       ddunbar@diads.com
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

/* Typedefs */

typedef struct {
  char captiontxt[256];
  char cellcontent[256];
  char clwidth[256];
  char clheight[256];
  gint fulldoc;
  gint caption;
  gint border;
  gint spantags;
  gint tdcomp;
  gint cellpadding;
  gint cellspacing;
} GTMValues;

typedef struct {
  gint run;
} GTMInterface;

/* Variables */

static GTMInterface bint =
{
  FALSE  /* run */
};

static GTMValues gtmvals =
{
  "Made with GIMP Table Magic",  /* caption text */
  "&nbsp;",  /* cellcontent text */
  "",    /* cell width text */
  "",    /* cell height text */
  1,     /* fulldoc */
  0,     /* caption */
  2,     /* border */
  0,     /* spantags */
  0,     /* tdcomp */
  4,     /* cellpadding */
  0      /* cellspacing */
};

/* Declare some local functions */

static void   query      (void);
static void   run        (char    *name,
                          int      nparams,
                          GParam  *param,
                          int     *nreturn_vals,
                          GParam **return_vals);
static gint   save_image (char   *filename,
			  GDrawable  *drawable);
static gint   save_dialog ();

static gint   color_comp (guchar *buffer, guchar *buf2);
static void   gtm_get_pixel (GPixelRgn *pixel_rgn, GDrawable *drawable,
			     guchar *buf, int x, int y);
static void   save_close_callback  (GtkWidget *widget, gpointer   data);
static void   gtm_entry_callback  (GtkWidget *widget, gpointer   data);
static void   gtm_toggle_callback  (GtkWidget *widget, gpointer   data);
static void   save_ok_callback     (GtkWidget *widget, gpointer   data);
static void   gtm_caption_callback     (GtkWidget *widget, gpointer   data);
static void   gtm_cellcontent_callback     (GtkWidget *widget, gpointer   data);
static void   gtm_clwidth_callback     (GtkWidget *widget, gpointer   data);
static void   gtm_clheight_callback     (GtkWidget *widget, gpointer   data);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};


MAIN ()

static void
query ()
{
  static GParamDef save_args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive" },
    { PARAM_IMAGE, "image", "Input image" },
    { PARAM_DRAWABLE, "drawable", "Drawable to save" },
    { PARAM_STRING, "filename", "The name of the file to save the image in" },
    { PARAM_STRING, "raw_filename", "The name of the file to save the image in" },
  };

  static int nsave_args = sizeof (save_args) / sizeof (save_args[0]);

  gimp_install_procedure ("file_GTM_save",
                          "GIMP Table Magic",
                          "Allows you to draw an HTML table in GIMP. See help for more info.",
                          "Daniel Dunbar",
                          "Daniel Dunbar",
                          "1998",
                          "<Save>/HTML",
			  "RGB*, GRAY*",
                          PROC_PLUG_IN,
                          nsave_args, 0,
                          save_args, NULL);

  gimp_register_save_handler ("file_GTM_save", "htm,html", "");
}

static void
run (char    *name,
     int      nparams,
     GParam  *param,
     int     *nreturn_vals,
     GParam **return_vals)
{
  static GParam values[2];
  GDrawable *drawable;

  drawable = gimp_drawable_get (param[2].data.d_int32);

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = STATUS_CALLING_ERROR;

  gimp_get_data ("file_GTM_save", &gtmvals);

  if (save_dialog ()) {
    save_image (param[3].data.d_string, drawable);
    values[0].data.d_status = STATUS_SUCCESS;  
  }
  else
    values[0].data.d_status = STATUS_CANCEL;

  gimp_set_data ("file_GTM_save", &gtmvals, sizeof (GTMValues));
}

static gint
save_image (char   *filename,
	    GDrawable  *drawable)
{
  int row,col, cols, rows, x, y;
  int colcount, colspan, rowspan;
  /* This works only in gcc - not allowed according */
  /* to ANSI C */
  /*int palloc[drawable->width][drawable->height];*/
  int *palloc;
  guchar *buffer, *buf2;
  gchar *width, *height;
  GPixelRgn pixel_rgn;
  char *name;

  FILE *fp;

  palloc = g_new (int, (gsize) drawable->width * drawable->height);

  fp = fopen(filename, "w");
  if (fp == NULL) {
    g_free (palloc);
    return FALSE;
  }
  if (gtmvals.fulldoc) {
    fprintf (fp,"<HTML>\n<HEAD><TITLE>%s</TITLE></HEAD>\n<BODY>\n",filename);
    fprintf (fp,"<H1>%s</H1>\n",filename);
  }
  fprintf (fp,"<TABLE BORDER=%d CELLPADDING=%d CELLSPACING=%d>\n",gtmvals.border,gtmvals.cellpadding,gtmvals.cellspacing);
  if (gtmvals.caption)
    fprintf (fp,"<CAPTION>%s</CAPTION>\n",gtmvals.captiontxt); 

  name = g_malloc (strlen (filename) + 11);
  sprintf (name, "Saving %s:", filename);
  gimp_progress_init (name);
  g_free (name);

  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, drawable->width, drawable->height, FALSE, FALSE);

  cols = drawable->width;
  rows = drawable->height;
  /* always room for R, G, B (and alpha), even for gray drawables */
  buffer = g_new0(guchar, MAX (drawable->bpp, 4));
  buf2 = g_new0(guchar, MAX (drawable->bpp, 4));

  width = malloc (2);
  height = malloc (2);
  sprintf(width," ");
  sprintf(height," ");
  if (strcmp (gtmvals.clwidth, "") != 0) {
    width = malloc (strlen (gtmvals.clwidth) + 11);
    sprintf(width," WIDTH=\"%s\"",gtmvals.clwidth);
  }
  if (strcmp (gtmvals.clheight, "") != 0) {
    height = malloc (strlen (gtmvals.clheight) + 13);
    sprintf(height," HEIGHT=\"%s\" ",gtmvals.clheight);
  }  
  
  /* Initialize array to hold ROWSPAN and COLSPAN cell allocation table */

  for (row=0; row < rows; row++)
    for (col=0; col < cols; col++)
      palloc[drawable->width * row + col]=1;

  colspan=0;
  rowspan=0;

  for (y = 0; y < rows; y++) {
    fprintf (fp,"   <TR>\n");
    for (x = 0; x < cols; x++) {
      gtm_get_pixel(&pixel_rgn, drawable, buffer, x, y);

      /* Determine ROWSPAN and COLSPAN */

      if (gtmvals.spantags) { 
	col=x;
	row=y;
	colcount=0;
	colspan=0;
	rowspan=0;
	gtm_get_pixel(&pixel_rgn, drawable, buf2, col, row);
	
	while (row < drawable->height && color_comp(buffer,buf2) && palloc[drawable->width * row + col] == 1) {
	  while (col < drawable->width && color_comp(buffer,buf2) && palloc[drawable->width * row + col] == 1) {
	    colcount++;
	    col++;
	    gtm_get_pixel(&pixel_rgn, drawable, buf2, col, row);
	  }
	  
	  if (colcount != 0) {
	    row++;
	    rowspan++;
	  }
	  
	  if (colcount < colspan || colspan == 0)
	    colspan=colcount;
	  
	  col=x;
	  colcount=0;
	  gtm_get_pixel(&pixel_rgn, drawable, buf2, col, row);
	}
	
	if (colspan > 1 || rowspan > 1) {
	  for (row=0; row < rowspan; row++)
	    for (col=0; col < colspan; col++)
	      palloc[drawable->width * (row+y) + (col+x)]=0;
	  palloc[drawable->width * y + x]=2;
	}
      }

      if (palloc[drawable->width * y + x]==1)
	fprintf (fp,"      <TD%s%sBGCOLOR=#%02x%02x%02x>",width,height,buffer[0],buffer[1],buffer[2]);

      if (palloc[drawable->width * y + x]==2)
	fprintf (fp,"      <TD ROWSPAN=\"%d\" COLSPAN=\"%d\"%s%sBGCOLOR=#%02x%02x%02x>",rowspan,colspan,width,height,buffer[0],buffer[1],buffer[2]);

      if (palloc[drawable->width * y + x]!=0) {
	if (gtmvals.tdcomp)
	  fprintf (fp,"%s</TD>\n",gtmvals.cellcontent);
	else 
	  fprintf (fp,"\n      %s\n      </TD>\n",gtmvals.cellcontent);
      }
    }
    fprintf (fp,"   </TR>\n");
    gimp_progress_update ((double) y / (double) rows);
  }

  if (gtmvals.fulldoc)
    fprintf (fp,"</TABLE></BODY></HTML>\n");  
  else fprintf (fp,"</TABLE>\n");
  fclose(fp);
  gimp_drawable_detach (drawable);
  free(width);
  free(height);
  g_free(palloc);
  g_free(buffer);
  g_free(buf2);

  free(palloc);

  return 1;
}

static gint save_dialog ()
{
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *frame;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *entry;
  GtkWidget *toggle;
  gchar buffer[32];

  bint.run=FALSE;


  gtk_init ();

  dlg = gimp_dialog_new ("GIMP HTML Magic");
  g_signal_connect (dlg, "destroy",
                      G_CALLBACK (save_close_callback),
                      NULL);

  /*  Action area  */
  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (save_ok_callback),
			  dlg, TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
                             G_CALLBACK (gtk_window_destroy), dlg);

  /* HTML Page Options */

  frame = gtk_frame_new ("HTML Page Options");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);

  toggle = gtk_check_button_new_with_label ("Generate Full HTML Document");
  gimp_container_add (frame, toggle);
  g_signal_connect (toggle, "toggled",
		      G_CALLBACK (gtm_toggle_callback), 
		      &gtmvals.fulldoc);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), gtmvals.fulldoc);
  gtk_widget_set_tooltip_text (toggle, "If checked GTM will output a full HTML document with <HTML>, <BODY>, etc. tags instead of just the table html.");


  /* HTML Table Creation Options */

  frame = gtk_frame_new ("Table Creation Options");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);

  table = gimp_table_new (3, 2, FALSE);
  gimp_container_set_border_width (table, 10);
  gimp_container_add (frame, table);

  toggle = gtk_check_button_new_with_label ("Use Cellspan");
  gimp_table_attach (table, toggle, 0, 1, 0, 1, GIMP_FILL, 0, 5, 0);
  g_signal_connect (toggle, "toggled",
		     G_CALLBACK (gtm_toggle_callback), 
		     &gtmvals.spantags);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), gtmvals.spantags);
  gtk_widget_set_tooltip_text (toggle, "If checked GTM will replace any rectangular sections of identically colored blocks with one large cell with ROWSPAN and COLSPAN values.");

  toggle = gtk_check_button_new_with_label ("Compress TD tags");
  gimp_table_attach (table, toggle, 1, 2, 0, 1, GIMP_FILL, 0, 5, 0);
  g_signal_connect (toggle, "toggled",
		     G_CALLBACK (gtm_toggle_callback), 
		     &gtmvals.tdcomp);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), gtmvals.tdcomp);
  gtk_widget_set_tooltip_text (toggle, "Checking this tag will cause GTM to leave no whitespace between the TD tags and the cellcontent.  This is only necessary for pixel level positioning control.");

  toggle = gtk_check_button_new_with_label ("Caption");
  gimp_table_attach (table, toggle, 0, 1, 1, 2, GIMP_FILL, 0, 5, 0);
  g_signal_connect (toggle, "toggled",
		     G_CALLBACK (gtm_toggle_callback), 
		     &gtmvals.caption);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), gtmvals.caption);
  gtk_widget_set_tooltip_text (toggle, "Check if you would like to have the table captioned.");

  entry = gtk_entry_new ();
  gimp_table_attach (table, entry, 1, 2, 1, 2, GIMP_FILL, 0, 5, 0);
  gtk_widget_set_size_request (entry, 100, -1);
  g_signal_connect (entry, "changed",
                    G_CALLBACK (gtm_caption_callback),
                    NULL);
  gtk_editable_set_text (GTK_EDITABLE (entry), gtmvals.captiontxt);
  gtk_widget_set_tooltip_text (entry, "The text for the table caption.");

  label = gtk_label_new ("Cell Content");
  gimp_table_attach (table, label, 0, 1, 2, 3, GIMP_FILL, 0, 5, 0);

  entry = gtk_entry_new ();
  gimp_table_attach (table, entry, 1, 2, 2, 3, GIMP_FILL, 0, 5, 0);
  gtk_widget_set_size_request (entry, 100, -1);
  g_signal_connect (entry, "changed",
                    G_CALLBACK (gtm_cellcontent_callback),
                    NULL);
  gtk_editable_set_text (GTK_EDITABLE (entry), gtmvals.cellcontent);
  gtk_widget_set_tooltip_text (entry, "The text to go into each cell.");

 
  /* HTML Table Options */

  frame = gtk_frame_new ("Table Options");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);

  table = gimp_table_new (5, 4, FALSE);
  gimp_container_set_border_width (table, 10);
  gimp_container_add (frame, table);

  label = gtk_label_new ("Border");
  gimp_table_attach (table, label, 0, 1, 0, 1, GIMP_FILL, 0, 5, 0);

  entry = gtk_entry_new ();
  gimp_table_attach (table, entry, 1, 2, 0, 1, GIMP_FILL, GIMP_FILL, 0, 0);
  gtk_widget_set_size_request (entry, 35, -1);
  g_signal_connect (entry, "changed",
                      G_CALLBACK (gtm_entry_callback),
                      &gtmvals.border);
  sprintf(buffer, "%d", gtmvals.border);
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
  gtk_widget_set_tooltip_text (entry, "The number of pixels in the table border.  Can only be a number.");

  label = gtk_label_new ("Width");
  gimp_table_attach (table, label, 0, 1, 1, 2, GIMP_FILL, 0, 5, 0);
    
  entry = gtk_entry_new ();
  gimp_table_attach (table, entry, 1, 2, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0);
  gtk_widget_set_size_request (entry, 35, -1);
  g_signal_connect (entry, "changed",
                      G_CALLBACK (gtm_clwidth_callback),
                      NULL);
  gtk_editable_set_text (GTK_EDITABLE (entry), gtmvals.clwidth);
  gtk_widget_set_tooltip_text (entry, "The width for each table cell.  Can be a number or a percent.");

  label = gtk_label_new ("Height");
  gimp_table_attach (table, label, 0, 1, 2, 3, GIMP_FILL, 0, 5, 0);

  entry = gtk_entry_new ();
  gimp_table_attach (table, entry, 1, 2, 2, 3, GIMP_FILL, GIMP_FILL, 0, 0);
  gtk_widget_set_size_request (entry, 35, -1);
  g_signal_connect (entry, "changed",
                      G_CALLBACK (gtm_clheight_callback),
                      NULL);
  gtk_editable_set_text (GTK_EDITABLE (entry), gtmvals.clheight);
  gtk_widget_set_tooltip_text (entry, "The height for each table cell.  Can be a number or a percent.");

  label = gtk_label_new ("Cell-Padding");
  gimp_table_attach (table, label, 2, 3, 0, 1, GIMP_FILL, 0, 5, 0);

  entry = gtk_entry_new ();
  gimp_table_attach (table, entry, 3, 4, 0, 1, GIMP_FILL, GIMP_FILL, 0, 0);
  gtk_widget_set_size_request (entry, 35, -1);
  g_signal_connect (entry, "changed",
                      G_CALLBACK (gtm_entry_callback),
                      &gtmvals.cellpadding);
  sprintf(buffer, "%d", gtmvals.cellpadding);
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
  gtk_widget_set_tooltip_text (entry, "The amount of cellpadding.  Can only be a number.");


  label = gtk_label_new ("Cell-Spacing");
  gimp_table_attach (table, label, 2, 3, 1, 2, GIMP_FILL, 0, 5, 0);

  entry = gtk_entry_new ();
  gimp_table_attach (table, entry, 3, 4, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0);
  gtk_widget_set_size_request (entry, 35, -1);
  g_signal_connect (entry, "changed",
                      G_CALLBACK (gtm_entry_callback),
                      &gtmvals.cellspacing);
  sprintf(buffer, "%d", gtmvals.cellspacing);
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
  gtk_widget_set_tooltip_text (entry, "The amount of cellspacing.  Can only be a number.");


  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return bint.run;
}

/* Fetch one pixel as R, G, B; gray values are replicated.  Coordinates
 * outside the drawable leave the buffer unchanged. */
static void gtm_get_pixel (GPixelRgn *pixel_rgn, GDrawable *drawable,
			   guchar *buf, int x, int y)
{
  if (x < 0 || y < 0 || x >= drawable->width || y >= drawable->height)
    return;

  gimp_pixel_rgn_get_pixel (pixel_rgn, buf, x, y);
  if (drawable->bpp < 3)
    buf[1] = buf[2] = buf[0];
}

static gint color_comp (guchar *buffer, guchar *buf2) {
  if (buffer[0] == buf2[0] && buffer[1] == buf2[1] && buffer[2] == buf2[2])
    return 1;
  else
    return 0;
}  


/*  Save interface functions  */

static void gtm_toggle_callback (GtkWidget *widget, gpointer   data)
{
  int *toggle_val;

  toggle_val = (int *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    *toggle_val = TRUE;
  else
    *toggle_val = FALSE;
}

static void gtm_entry_callback (GtkWidget *widget, gpointer data)
{
  gint *text_val;

  text_val = (gint*)data;
  *text_val = atoi (gtk_editable_get_text (GTK_EDITABLE (widget)));
}

static void save_close_callback (GtkWidget *widget, gpointer   data)
{
  gimp_main_loop_quit ();
}

static void save_ok_callback (GtkWidget *widget, gpointer   data)
{
  bint.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void gtm_caption_callback (GtkWidget *widget, gpointer   data)
{
  g_strlcpy(gtmvals.captiontxt, gtk_editable_get_text (GTK_EDITABLE (widget)), sizeof (gtmvals.captiontxt));
}

static void gtm_cellcontent_callback (GtkWidget *widget, gpointer   data)
{
  g_strlcpy(gtmvals.cellcontent, gtk_editable_get_text (GTK_EDITABLE (widget)), sizeof (gtmvals.cellcontent));
}

static void gtm_clwidth_callback (GtkWidget *widget, gpointer   data)
{
  g_strlcpy(gtmvals.clwidth, gtk_editable_get_text (GTK_EDITABLE (widget)), sizeof (gtmvals.clwidth));
}

static void gtm_clheight_callback (GtkWidget *widget, gpointer   data)
{
  g_strlcpy(gtmvals.clheight, gtk_editable_get_text (GTK_EDITABLE (widget)), sizeof (gtmvals.clheight));
}
