/* max_rgb.c -- This is a plug-in for the GIMP (1.0's API)
 * Author: Shuji Narazaki <narazaki@InetQ.or.jp>
 * Time-stamp: <1997/10/23 23:40:20 narazaki@InetQ.or.jp>
 * Version: 0.35
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
#define	PLUG_IN_NAME	"plug_in_max_rgb"
#define SHORT_NAME	"max_rgb"
#define PROGRESS_NAME	"max_rgb: scanning..."
#define MENU_POSITION	"<Image>/Filters/Colors/Max RGB"
#define	MAIN_FUNCTION	max_rgb
/* you need not change the following names */
#define INTERFACE	max_rgb_interface
#define	DIALOG		max_rgb_dialog
#define ERROR_DIALOG	max_rgb_error_dialog
#define VALS		max_rgb_vals
#define OK_CALLBACK	_max_rgbok_callback

static void	query	(void);
static void	run	(char	*name,
			 int	nparams,
			 GParam	*param,
			 int	*nreturn_vals,
			 GParam **return_vals);
static GStatusType	MAIN_FUNCTION (gint32 drawable_id);
static gint	DIALOG ();
static void	ERROR_DIALOG (gint gtk_was_not_initialized, gchar *message);

static void
OK_CALLBACK (GtkWidget *widget, gpointer   data);

/* gtkWrapper functions */ 
#define PROGRESS_UPDATE_NUM	100
#define ENTRY_WIDTH	100
#define SCALE_WIDTH	100
static void
gtkW_close_callback (GtkWidget *widget, gpointer   data);
static void
gtkW_toggle_update (GtkWidget *widget, gpointer   data);
static GtkWidget *
gtkW_dialog_new (char *name,
		 GCallback ok_callback,
		 GCallback close_callback);
static GtkWidget *
gtkW_error_dialog_new (char * name);
static GtkWidget *
gtkW_vbox_add_radio_button (GtkWidget *vbox,
			    gchar	*name,
			    GtkWidget	*group,
			    GCallback	update,
			    gint	*value);
GtkWidget *gtkW_check_button_new (GtkWidget	*parent,
				  gchar	*name,
				  GCallback update,
				  gint	*value);
GtkWidget *gtkW_frame_new (GtkWidget *parent, gchar *name);
GtkWidget *gtkW_table_new (GtkWidget *parent, gint col, gint row);
GtkWidget *gtkW_hbox_new (GtkWidget *parent);
GtkWidget *gtkW_vbox_new (GtkWidget *parent);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,				/* init_proc  */
  NULL,				/* quit_proc */
  query,			/* query_proc */
  run,				/* run_proc */
};

typedef struct
{
  gint		max_p;		/* gint, gdouble, and so on */
} ValueType;

static ValueType VALS = 
{
  1
};

typedef struct 
{
  gint run;
} Interface;

static Interface INTERFACE = { FALSE };

gint	hold_max;
gint	hold_min;

MAIN ()

static void
query ()
{
  static GParamDef args [] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive"},
    { PARAM_IMAGE, "image", "Input image (not used)"},
    { PARAM_DRAWABLE, "drawable", "Input drawable"},
    { PARAM_INT32, "max_p", "1 for maximizing, 0 for minimizing"}
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0;
  
  gimp_install_procedure (PLUG_IN_NAME,
			  "Return an image in which each pixel holds only the channel that has the maximum value in three (red, green, blue) channels, and other channels are zero-cleared",
			  "the help is not yet written for this plug-in",
			  "Shuji Narazaki (narazaki@InetQ.or.jp)",
			  "Shuji Narazaki",
			  "1997",
			  MENU_POSITION,
			  "RGB*",
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
  GStatusType	status = STATUS_EXECUTION_ERROR;
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
      gimp_get_data (PLUG_IN_NAME, &VALS);
      hold_max = VALS.max_p;
      hold_min = VALS.max_p ? 0 : 1;
      /* Since a channel might be selected, we must check wheter RGB or not. */
      if (!gimp_drawable_color(drawable_id))
	{
	  ERROR_DIALOG (1, "RGB drawable is not selected.");
	  return;
	}
      if (! DIALOG ())
	return;
      break;
    case RUN_NONINTERACTIVE:
      /* You must copy the values of parameters to VALS or dialog variables. */
      break;
    case RUN_WITH_LAST_VALS:
      gimp_get_data (PLUG_IN_NAME, &VALS);
      break;
    }
  
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
  gint		init_value, flag;

  init_value = (VALS.max_p > 0) ? 0 : 255;
  flag = (0 < VALS.max_p) ? 1 : -1;

  drawable = gimp_drawable_get (drawable_id);
  gap = (gimp_drawable_has_alpha (drawable_id)) ? 1 : 0;
  gimp_drawable_mask_bounds (drawable_id, &x1, &y1, &x2, &y2);
  total = (x2 - x1) * (y2 - y1);

  gimp_tile_cache_ntiles (2 * (drawable->width / gimp_tile_width () + 1));
  gimp_pixel_rgn_init (&src_rgn, drawable, x1, y1, (x2 - x1), (y2 - y1), FALSE, FALSE);
  gimp_pixel_rgn_init (&dest_rgn, drawable, x1, y1, (x2 - x1), (y2 - y1), TRUE, TRUE);

  pr = gimp_pixel_rgns_register (2, &src_rgn, &dest_rgn);
  gimp_progress_init (PROGRESS_NAME);
  for (; pr != NULL; pr = gimp_pixel_rgns_process (pr))
    {
      for (y = 0; y < src_rgn.h; y++)
	{
	  src = src_rgn.data + y * src_rgn.rowstride;
	  dest = dest_rgn.data + y * dest_rgn.rowstride;

	  for (x = 0; x < src_rgn.w; x++)
	    {
	      gint	ch, max_ch = 0;
	      guchar	max, tmp_value;
	      
	      max = init_value;
	      for (ch = 0; ch < 3; ch++)
		if (flag * max <= flag * (tmp_value = (*src++)))
		  {
		  if (max == tmp_value)
		    max_ch += 1 << ch;
		  else
		    {
		      max_ch = 1 << ch; /* clear memories of old channels */
		      max = tmp_value;
		    }
		  }
	      for ( ch = 0; ch < 3; ch++)
		*dest++ = (guchar)(((max_ch & (1 << ch)) > 0) ? max : 0);
	      if (gap) *dest++=*src++;
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
DIALOG ()
{
  GtkWidget	*dlg;
  GtkWidget	*hbox;
  GtkWidget	*vbox;
  GtkWidget	*frame;
  GtkWidget	*group = NULL;

  gtk_init ();
  
  dlg = gtkW_dialog_new (PLUG_IN_NAME,
			 G_CALLBACK (OK_CALLBACK),
			 G_CALLBACK (gtkW_close_callback));
  
  hbox = gtkW_hbox_new ((gimp_dialog_get_vbox (dlg)));
  frame = gtkW_frame_new (hbox, "Parameter Settings");
  /*
  table = gtkW_table_new (frame, 2, 2);
  gtkW_table_add_toggle (table, "Hold the maximal channel", 0, 2, 1,
			 G_CALLBACK (gtkW_toggle_update), &VALS.max_p);
  */
  vbox = gtkW_vbox_new (frame);
  group = gtkW_vbox_add_radio_button (vbox, "Hold the maximal channels", group,
				      G_CALLBACK (gtkW_toggle_update),
				      &hold_max);
  group = gtkW_vbox_add_radio_button (vbox, "Hold the minimal channels", group,
				      G_CALLBACK (gtkW_toggle_update),
				      &hold_min);

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
    {
      gtk_init ();
    }
  
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
  VALS.max_p = hold_max;
  INTERFACE.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}

/* VFtext interface functions  */

static void
gtkW_close_callback (GtkWidget *widget,
		     gpointer   data)
{
  gimp_main_loop_quit ();
}

static void
gtkW_toggle_update (GtkWidget *widget,
		    gpointer   data)
{
  int *toggle_val;

  toggle_val = (int *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    *toggle_val = TRUE;
  else
    *toggle_val = FALSE;
}

/* gtkW is the abbreviation of gtk Wrapper */
static GtkWidget *
gtkW_dialog_new (char * name,
		 GCallback ok_callback,
		 GCallback close_callback)
{
  GtkWidget *dlg, *button;
  
  dlg = gimp_dialog_new (name);
  g_signal_connect (dlg, "destroy",
		      G_CALLBACK (gtkW_close_callback), NULL);

  /* Action Area */
  button = gtk_button_new_with_label ("OK");
  g_signal_connect (button, "clicked",
		      G_CALLBACK (ok_callback), dlg);
  gimp_box_pack_start (gimp_dialog_get_action_area (dlg), button,
		      TRUE, TRUE, 0);
  gtk_window_set_default_widget (GTK_WINDOW (dlg), button);

  button = gtk_button_new_with_label ("Cancel");
  g_signal_connect_swapped (button, "clicked",
			     G_CALLBACK (gtk_window_destroy),
			     dlg);
  gimp_box_pack_start (gimp_dialog_get_action_area (dlg), button,
		      TRUE, TRUE, 0);
  gtk_window_present (GTK_WINDOW (dlg));

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
  button = gtk_button_new_with_label ("OK");
  g_signal_connect (button, "clicked",
		      G_CALLBACK (gtkW_close_callback), dlg);
  gimp_box_pack_start (gimp_dialog_get_action_area (dlg), button,
		      TRUE, TRUE, 0);
  gtk_window_set_default_widget (GTK_WINDOW (dlg), button);

  return dlg;
}

GtkWidget *
gtkW_table_new (GtkWidget *parent, gint col, gint row)
{
  GtkWidget	*table;
  
  table = gimp_table_new (col,row, FALSE);
  gimp_container_set_border_width (table, 10);
  gimp_container_add (parent, table);
  return table;
}

GtkWidget *
gtkW_hbox_new (GtkWidget *parent)
{
  GtkWidget	*hbox;
  
  hbox = gimp_hbox_new (FALSE, 5);
  gimp_container_set_border_width (hbox, 5);
  gimp_box_pack_start (parent, hbox, FALSE, TRUE, 0);

  return hbox;
}

GtkWidget *
gtkW_vbox_new (GtkWidget *parent)
{
  GtkWidget *vbox;
  
  vbox = gimp_vbox_new (FALSE, 5);
  gimp_container_set_border_width (vbox, 10);
  /* gimp_box_pack_start (parent, vbox, TRUE, TRUE, 0); */
  gimp_container_add (parent, vbox);

  return vbox;
}

GtkWidget *
gtkW_check_button_new (GtkWidget	*parent,
		       gchar	*name,
		       GCallback update,
		       gint	*value)
{
  GtkWidget *toggle;
  
  toggle = gtk_check_button_new_with_label (name);
  g_signal_connect (toggle, "toggled",
		      G_CALLBACK (update),
		      value);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), *value);
  gimp_container_add (parent, toggle);
  return toggle;
}

GtkWidget *
gtkW_frame_new (GtkWidget *parent,
		gchar *name)
{
  GtkWidget *frame;
  
  frame = gtk_frame_new (name);
  gimp_container_set_border_width (frame, 5);
  gimp_box_pack_start (parent, frame, FALSE, FALSE, 0);
  return frame;
}

static GtkWidget *
gtkW_vbox_add_radio_button (GtkWidget *vbox,
			    gchar	*name,
			    GtkWidget	*group,
			    GCallback	update,
			    gint	*value)
{
  GtkWidget *toggle;
  
  toggle = gimp_radio_button_new (group, name);
  group = toggle;
  gimp_box_pack_start (vbox, toggle, FALSE, FALSE, 0);
  g_signal_connect (toggle, "toggled",
		      G_CALLBACK (update), value);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), *value);
  return group;
}
/* end of max_rgb.c */
