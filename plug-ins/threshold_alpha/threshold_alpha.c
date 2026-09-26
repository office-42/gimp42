/* threshold_alpha.c -- This is a plug-in for the GIMP (1.0's API)
 * Author: Shuji Narazaki <narazaki@InetQ.or.jp>
 * Time-stamp: <1997/06/08 22:34:26 narazaki@InetQ.or.jp>
 * Version: 0.13
 *
 * Copyright (C) 1997 Shuji Narazaki <narazaki@InetQ.or.jp>
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

#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Replace them with the right ones */
#define	PLUG_IN_NAME	"plug_in_threshold_alpha"
#define SHORT_NAME	"threshold_alpha"
#define PROGRESS_NAME	"threshold_alpha (0.13):coloring transparency..."
#define MENU_POSITION	"<Image>/Image/Alpha/Threshold Alpha"
#define	MAIN_FUNCTION	threshold_alpha
/* you need not change the following names */
#define INTERFACE	threshold_alpha_interface
#define	DIALOG		threshold_alpha_dialog
#define ERROR_DIALOG	threshold_alpha_error_dialog
#define VALS		threshold_alpha_vals
#define OK_CALLBACK	threshold_alpha_ok_callback

static void	query	(void);
static void	run	(char	*name,
			 int	nparams,
			 GParam	*param,
			 int	*nreturn_vals,
			 GParam **return_vals);
static GStatusType	MAIN_FUNCTION (gint32 drawable_id);
static gint	DIALOG (void);
static void	ERROR_DIALOG (gint gtk_was_not_initialized, gchar *message);

static void
OK_CALLBACK (GtkWidget *widget, gpointer   data);

/* gtkWrapper functions */
#define PROGRESS_UPDATE_NUM	100
#define ENTRY_WIDTH	100
#define SCALE_WIDTH	120
static void
gtkW_close_callback (GtkWidget *widget, gpointer   data);
static void
gtkW_iscale_update (GtkAdjustment *adjustment,
		    gpointer       data);
static void
gtkW_ientry_update (GtkWidget *widget,
		    gpointer   data);
static GtkWidget *
gtkW_dialog_new (char *name,
		 GCallback ok_callback,
		 GCallback close_callback);
static GtkWidget *
gtkW_error_dialog_new (char * name);
static void
gtkW_table_add_iscale_entry (GtkWidget	*table,
			     gchar	*name,
			     gint	x,
			     gint	y,
			     GCallback	scale_update,
			     GCallback	entry_update,
			     gint	*value,
			     gdouble	min,
			     gdouble	max,
			     gdouble	step,
			     gchar	*buffer);

static GtkWidget *gtkW_frame_new (GtkWidget *parent, gchar *name);
static GtkWidget *gtkW_table_new (GtkWidget *parent, gint col, gint row);
static GtkWidget *gtkW_hbox_new (GtkWidget *parent);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,				/* init_proc  */
  NULL,				/* quit_proc */
  query,			/* query_proc */
  run,				/* run_proc */
};

typedef struct
{
  gint	threshold;
} ValueType;

static ValueType VALS = 
{
  127
};

typedef struct 
{
  gint run;
} Interface;

static Interface INTERFACE = { FALSE };

MAIN ()

static void
query ()
{
  static GParamDef args [] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive"},
    { PARAM_IMAGE, "image", "Input image (not used)"},
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
    { PARAM_INT32, "threshold", "Threshold" },
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0;
  
  gimp_install_procedure (PLUG_IN_NAME,
			  "",
			  "",
			  "Shuji Narazaki (narazaki@InetQ.or.jp)",
			  "Shuji Narazaki",
			  "1997",
			  MENU_POSITION,
			  "RGBA,GRAYA",
			  PROC_PLUG_IN,
			  nargs, nreturn_vals,
			  args, return_vals);
}

static void
run (char	*name,
     int	nparams,
     GParam	*param,
     int	*nreturn_vals,
     GParam	**return_vals)
{
  static GParam	 values[1];
  GStatusType	status = STATUS_SUCCESS;
  GRunModeType	run_mode;
  gint		drawable_id;
  
  run_mode = param[0].data.d_int32;
  drawable_id = param[2].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;
  
  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;

  switch (run_mode)
    {
    case RUN_INTERACTIVE:
      /* Since a channel might be selected, we must check wheter RGB or not. */
      if (gimp_layer_get_preserve_transparency (drawable_id))
	{
	  ERROR_DIALOG (1, "The layer preserves transparency.");
	  return;
	}
      if (!gimp_drawable_color (drawable_id))
	{
	  ERROR_DIALOG (1, "RGB drawable is not selected.");
	  return;
	}
      gimp_get_data (PLUG_IN_NAME, &VALS);
      if (! DIALOG ())
	return;
      break;
    case RUN_NONINTERACTIVE:
      if (nparams != 4)
	status = STATUS_CALLING_ERROR;
      if (status == STATUS_SUCCESS)
	{
	  VALS.threshold = param[3].data.d_int32;
	} 
      break;
    case RUN_WITH_LAST_VALS:
      gimp_get_data (PLUG_IN_NAME, &VALS);
      break;
    }
  
  if (status == STATUS_SUCCESS)
    status = MAIN_FUNCTION (drawable_id);

  if (run_mode != RUN_NONINTERACTIVE)
    gimp_displays_flush();
  if (run_mode == RUN_INTERACTIVE && status == STATUS_SUCCESS )
    gimp_set_data (PLUG_IN_NAME, &VALS, sizeof (ValueType));

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
}

static GStatusType
MAIN_FUNCTION (gint32 drawable_id)
{
  GDrawable	*drawable;
  GPixelRgn	src_rgn, dest_rgn;
  guchar	*src, *dest;
  gpointer	pr;
  gint		x, y, x1, x2, y1, y2;
  gint		gap, total, processed = 0;
  
  drawable = gimp_drawable_get (drawable_id);
  if (! gimp_drawable_has_alpha (drawable_id)) return STATUS_EXECUTION_ERROR;
  gap = 3;
  gimp_drawable_mask_bounds (drawable_id, &x1, &y1, &x2, &y2);
  total = (x2 - x1) * (y2 - y1);

  gimp_tile_cache_ntiles (2 * (drawable->width / gimp_tile_width () + 1));
  gimp_pixel_rgn_init (&src_rgn, drawable, x1, y1, (x2 - x1), (y2 - y1), FALSE, FALSE);
  gimp_pixel_rgn_init (&dest_rgn, drawable, x1, y1, (x2 - x1), (y2 - y1), TRUE, TRUE);

  pr = gimp_pixel_rgns_register (2, &src_rgn, &dest_rgn);
  gimp_progress_init (PROGRESS_NAME);
  for (; pr != NULL; pr = gimp_pixel_rgns_process (pr))
    {
      int	index;
      
      for (y = 0; y < src_rgn.h; y++)
	{
	  src = src_rgn.data + y * src_rgn.rowstride;
	  dest = dest_rgn.data + y * dest_rgn.rowstride;

	  for (x = 0; x < src_rgn.w; x++)
	    {
	      for (index = 0; index < gap; index++)
		*dest++ = *src++;
	      *dest++ = (VALS.threshold < *src++) ? 255 : 0;

	      if ((++processed % (total / PROGRESS_UPDATE_NUM)) == 0)
		gimp_progress_update ((double)processed /(double) total); 
	    }
	}
  }
  gimp_progress_update (1.0);
  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->id, TRUE);
  gimp_drawable_update (drawable->id, x1, y1, (x2 - x1), (y2 - y1));
  gimp_drawable_detach (drawable);
  return STATUS_SUCCESS;
}

/* dialog stuff */
static int
DIALOG (void)
{
  GtkWidget	*dlg;
  GtkWidget	*hbox;
  GtkWidget	*frame;
  GtkWidget	*table;
  static gchar	buffer[32];

  gtk_init ();

  dlg = gtkW_dialog_new (PLUG_IN_NAME,
			 G_CALLBACK (OK_CALLBACK),
			 G_CALLBACK (gtkW_close_callback));

  hbox = gtkW_hbox_new (gimp_dialog_get_vbox (dlg));
  frame = gtkW_frame_new (hbox, "Parameter Settings");
  table = gtkW_table_new (frame, 2, 2);
  gtkW_table_add_iscale_entry (table, "Threshold", 0, 0,
			       G_CALLBACK (gtkW_iscale_update),
			       G_CALLBACK (gtkW_ientry_update),
			       &VALS.threshold,
			       0, 255, 1, buffer);

  gtk_window_present (GTK_WINDOW (dlg));
  gimp_main_loop_run ();

  return INTERFACE.run;
}

static void
ERROR_DIALOG (gint gtk_was_not_initialized, gchar *message)
{
  GtkWidget *dlg;
  GtkWidget *table;
  GtkWidget *label;

  if (gtk_was_not_initialized)
    gtk_init ();

  dlg = gtkW_error_dialog_new (PLUG_IN_NAME);

  table = gimp_table_new (1,1, FALSE);
  gimp_container_set_border_width (table, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), table, TRUE, TRUE, 0);

  label = gtk_label_new (message);
  gimp_table_attach (table, label, 0, 1, 0, 1, GIMP_FILL|GIMP_EXPAND,
		     0, 0, 0);
  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();
}

static void
OK_CALLBACK (GtkWidget *widget,
	      gpointer   data)
{
  INTERFACE.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void
gtkW_close_callback (GtkWidget *widget,
		     gpointer   data)
{
  gimp_main_loop_quit ();
}

/* gtkW is the abbreviation of gtk Wrapper */
static GtkWidget *
gtkW_dialog_new (char * name,
		 GCallback ok_callback,
		 GCallback close_callback)
{
  GtkWidget *dlg, *button;

  dlg = gimp_dialog_new (name);
  g_signal_connect (dlg, "destroy", close_callback, NULL);

  /* Action Area */
  gimp_dialog_add_button (dlg, "OK", ok_callback, dlg, TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  return dlg;
}

static GtkWidget *
gtkW_error_dialog_new (char * name)
{
  GtkWidget *dlg, *button;

  dlg = gimp_dialog_new (name);
  g_signal_connect (dlg, "destroy",
		    G_CALLBACK (gtkW_close_callback), NULL);

  /* Action Area */
  button = gimp_dialog_add_button (dlg, "OK", NULL, NULL, TRUE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  return dlg;
}

static GtkWidget *
gtkW_table_new (GtkWidget *parent, gint col, gint row)
{
  GtkWidget	*table;

  table = gimp_table_new (col,row, FALSE);
  gimp_container_set_border_width (table, 10);
  gimp_container_add (parent, table);

  return table;
}

static GtkWidget *
gtkW_hbox_new (GtkWidget *parent)
{
  GtkWidget	*hbox;

  hbox = gimp_hbox_new (FALSE, 5);
  gimp_container_set_border_width (hbox, 5);
  gimp_box_pack_start (parent, hbox, FALSE, TRUE, 0);

  return hbox;
}

static GtkWidget *
gtkW_frame_new (GtkWidget *parent,
		gchar *name)
{
  GtkWidget *frame;

  frame = gtk_frame_new (name);
  gimp_container_set_border_width (frame, 5);
  gimp_box_pack_start (parent, frame, FALSE, FALSE, 0);
  return frame;
}

static void
gtkW_table_add_iscale_entry (GtkWidget	*table,
			     gchar	*name,
			     gint	x,
			     gint	y,
			     GCallback	scale_update,
			     GCallback	entry_update,
			     gint	*value,
			     gdouble	min,
			     gdouble	max,
			     gdouble	step,
			     gchar	*buffer)
{
  GtkAdjustment *adjustment;
  GtkWidget *label, *hbox, *scale, *entry;

  label = gtk_label_new (name);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, x, x+1, y, y+1,
		     GIMP_FILL|GIMP_EXPAND, GIMP_FILL, 5, 0);

  hbox = gimp_hbox_new (FALSE, 5);
  gimp_table_attach (table, hbox, x+1, x+2, y, y+1,
		     GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 0, 0);

  adjustment = gtk_adjustment_new (*value, min, max, step, step, 0.0);

  scale = gimp_hscale_new (adjustment, 0);
  gtk_widget_set_size_request (scale, SCALE_WIDTH, -1);
  gimp_box_pack_start (hbox, scale, TRUE, TRUE, 0);
  g_signal_connect (adjustment, "value-changed", scale_update, value);

  entry = gtk_entry_new ();
  g_object_set_data (G_OBJECT (entry), "user_data", adjustment);
  g_object_set_data (G_OBJECT (adjustment), "user_data", entry);
  gimp_box_pack_start (hbox, entry, TRUE, TRUE, 0);
  gtk_widget_set_size_request (entry, ENTRY_WIDTH/3, -1);
  gtk_editable_set_width_chars (GTK_EDITABLE (entry), 4);
  sprintf (buffer, "%d", *value);
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
  g_signal_connect (entry, "changed", entry_update, value);
}

static void
gtkW_iscale_update (GtkAdjustment *adjustment,
		    gpointer       data)
{
  GtkWidget *entry;
  gchar buffer[32];
  int *val;

  val = data;
  if (*val != (int) gtk_adjustment_get_value (adjustment))
    {
      *val = gtk_adjustment_get_value (adjustment);
      entry = g_object_get_data (G_OBJECT (adjustment), "user_data");
      sprintf (buffer, "%d", *val);
      gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
    }
}

static void
gtkW_ientry_update (GtkWidget *widget,
		    gpointer   data)
{
  GtkAdjustment *adjustment;
  int new_val;
  int *val;

  val = data;
  new_val = atoi (gtk_editable_get_text (GTK_EDITABLE (widget)));

  if (*val != new_val)
    {
      adjustment = g_object_get_data (G_OBJECT (widget), "user_data");

      if ((new_val >= gtk_adjustment_get_lower (adjustment)) &&
	  (new_val <= gtk_adjustment_get_upper (adjustment)))
	{
	  *val = new_val;
	  gtk_adjustment_set_value (adjustment, new_val);
	}
    }
}

/* end of threshold_alpha.c */

