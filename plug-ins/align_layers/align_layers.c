/* align_layers.c -- This is a plug-in for the GIMP (1.0's API)
 * Author: Shuji Narazaki <narazaki@InetQ.or.jp>
 * Time-stamp: <1998/01/17 00:32:23 narazaki@InetQ.or.jp>
 * Version:  0.26
 *
 * Copyright (C) 1997-1998 Shuji Narazaki <narazaki@InetQ.or.jp>
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

#include "libgimp/gimp.h"
#include <gtk/gtk.h>
#include "libgimp/gimpui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define	PLUG_IN_NAME	"plug_in_align_layers"
#define SHORT_NAME	"align_layers"
#define PROGRESS_NAME	"align_layers"
#define MENU_POSITION	"<Image>/Layers/Align Visible Layers"
#define	MAIN_FUNCTION	main_function
#define INTERFACE	align_layers_interface
#define	DIALOG		align_layers_dialog
#define VALS		align_layers_vals
#define OK_CALLBACK	align_layers_ok_callback
#define	PREVIEW_UPDATE	_preview_update
#define PROGRESS_UPDATE_NUM	100

/* gtkWrapper functions */
#define GTKW_ENTRY_WIDTH	40
#define GTKW_SCALE_WIDTH	100
#define GTKW_PREVIEW_WIDTH	50
#define GTKW_PREVIEW_HEIGHT	256
#define	GTKW_BORDER_WIDTH	5
#define GTKW_FLOAT_MIN_ERROR	0.000001
static void	gtkW_close_callback (GtkWidget *widget, gpointer data);
static void	gtkW_toggle_update (GtkWidget *widget, gpointer data);
static void	gtkW_iscale_update (GtkAdjustment *adjustment, gpointer data);
static void	gtkW_ientry_update (GtkWidget *widget, gpointer data);
static GtkWidget *gtkW_dialog_new (char *name, GCallback ok_callback,
				   GCallback close_callback);
static void	gtkW_message_dialog (gint gtk_was_initialized, gchar *message);
static GtkWidget *gtkW_message_dialog_new (char * name);
static void
gtkW_table_add_toggle (GtkWidget	*table,
		       gchar	*name,
		       gint	x,
		       gint	y,
		       GCallback update,
		       gint	*value);
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
typedef struct
{
  gchar *name;
  gpointer data;
} gtkW_menu_item;
static GtkWidget *gtkW_table_add_menu (GtkWidget *parent,
				       gchar *name,
				       int x,
				       int y,
				       GCallback imenu_update,
				       int *val,
				       gtkW_menu_item *item,
				       int item_num);
static void	gtkW_menu_update (GtkWidget *widget, gpointer data);
static GtkWidget *gtkW_frame_new (GtkWidget *parent, gchar *name);
static GtkWidget *gtkW_table_new (GtkWidget *parent, gint col, gint row);
/* end of GtkW */

static void	query	(void);
static void	run	(char	*name,
			 int	nparams,
			 GParam	*param,
			 int	*nreturn_vals,
			 GParam **return_vals);
static GStatusType align_layers (gint32 image_id);
static void align_layers_get_align_offsets (gint32	drawable_id,
					    gint	*x,
					    gint	*y);
static gint	DIALOG (void);
static void	OK_CALLBACK (GtkWidget *widget, gpointer   data);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,				/* init_proc  */
  NULL,				/* quit_proc */
  query,			/* query_proc */
  run,				/* run_proc */
};

/* dialog variables */
gtkW_menu_item h_style_menu [] =
{
#define	H_NONE		0
  { "None", NULL },
#define H_COLLECT	1
  { "Collect", NULL },
#define	LEFT2RIGHT	2
  { "Fill (left to right)", NULL },
#define	RIGHT2LEFT	3
  { "Fill (right to left)", NULL },
#define SNAP2HGRID	4
  { "Snap to grid", NULL }
};

gtkW_menu_item h_base_menu [] =
{
#define H_BASE_LEFT	0
  { "Left edge", NULL },
#define H_BASE_CENTER	1
  { "Center", NULL },
#define	H_BASE_RIGHT	2
  { "Right edge", NULL }
};

gtkW_menu_item v_style_menu [] =
{
#define	V_NONE		0
  { "None", NULL },
#define V_COLLECT	1
  { "Collect", NULL },
#define TOP2BOTTOM	2
  { "Fill (top to bottom)", NULL },
#define BOTTOM2TOP	3
  { "Fill (bottom to top)", NULL },
#define SNAP2VGRID	4
  { "Snap to grid", NULL }
};

gtkW_menu_item v_base_menu [] =
{
#define V_BASE_TOP	0
  { "Top edge", NULL },
#define V_BASE_CENTER	1
  { "Center", NULL },
#define V_BASE_BOTTOM	2
  { "Bottom edge", NULL }
};

typedef struct
{
  gint	h_style;
  gint	h_base;
  gint	v_style;
  gint	v_base;
  gint	ignore_bottom;
  gint	base_is_bottom_layer;
  gint	grid_size;
} ValueType;

static ValueType VALS = 
{
  H_NONE, H_BASE_LEFT, V_NONE, V_BASE_TOP, 1, 0, 10
};

typedef struct 
{
  gint run;
} Interface;

static Interface INTERFACE = { FALSE };

/* gint	link_after_alignment = 0;*/

MAIN ()

static void
query ()
{
  static GParamDef args [] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive"},
    { PARAM_IMAGE, "image", "Input image"},
    { PARAM_DRAWABLE, "drawable", "Input drawable (not used)"},
    { PARAM_INT32, "link-afteer-alignment", "Link the visible layers after alignment"},
    { PARAM_INT32, "use-bottom", "use the bottom layer as the base of alignment"},
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0;
  
  gimp_install_procedure (PLUG_IN_NAME,
			  "Align visible layers",
			  "align visible layers",
			  "Shuji Narazaki <narazaki@InetQ.or.jp>",
			  "Shuji Narazaki",
			  "1997",
			  MENU_POSITION,
			  "RGB*,GRAY*",
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
  gint		image_id, layer_num;
  
  run_mode = param[0].data.d_int32;
  image_id = param[1].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;
  
  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;

  switch ( run_mode )
    {
    case RUN_INTERACTIVE:
      gimp_image_get_layers (image_id, &layer_num);
      if (layer_num < 2)
	{
	  gtkW_message_dialog (0, "Error: there are too few layers.");
	  return;
	}
      gimp_get_data (PLUG_IN_NAME, &VALS);
      if (! DIALOG ())
	return;
      break;
    case RUN_NONINTERACTIVE:
      break;
    case RUN_WITH_LAST_VALS:
      gimp_get_data (PLUG_IN_NAME, &VALS);
      break;
    }
  
  status = align_layers (image_id);

  if (run_mode != RUN_NONINTERACTIVE)
    gimp_displays_flush();
  if (run_mode == RUN_INTERACTIVE && status == STATUS_SUCCESS )
    gimp_set_data (PLUG_IN_NAME, &VALS, sizeof (ValueType));

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
}

static GStatusType
align_layers (gint32 image_id)
{
  GParam*	return_vals;
  gint	retvals;
  gint	layer_num = 0;
  gint	visible_layer_num = 1;
  gint	*layers = NULL;
  gint	index, vindex;
  gint	step_x = 0;
  gint	step_y = 0;
  gint	x = 0;
  gint	y = 0;
  gint	orig_x = 0;
  gint	orig_y = 0;
  gint	offset_x = 0;
  gint	offset_y = 0;
  gint	base_x = 0;
  gint	base_y =0;

  layers = gimp_image_get_layers (image_id, &layer_num);
  for (index = 0; index < layer_num; index++)
    if (gimp_layer_get_visible (layers[index])) visible_layer_num++;

  if (VALS.ignore_bottom)
    {
      layer_num--;
      visible_layer_num--;
    }

  if (0 < visible_layer_num)
    {
      gint	unintialzied = 1;
      gint	min_x = 0;
      gint	min_y = 0;
      gint	max_x = 0;
      gint	max_y = 0;
      
      /* 0 is the top layer */
      for (index = 0; index < layer_num; index++)
	if (gimp_layer_get_visible (layers[index]))
	  {
	    gimp_drawable_offsets (layers[index], &orig_x, &orig_y);
	    align_layers_get_align_offsets (layers[index], &offset_x, &offset_y);
	    orig_x += offset_x;
	    orig_y += offset_y;

	    if (unintialzied)
	      {
		base_x = min_x = max_y = orig_x;
		base_y = min_y = max_y = orig_y;
		unintialzied = 0;
	      }
	    else
	      {
		if ( orig_x < min_x ) min_x = orig_x;
		if ( max_x < orig_x ) max_x = orig_x;
		if ( orig_y < min_y ) min_y = orig_y;
		if ( max_y < orig_y ) max_y = orig_y;
	      }
	  }
      if (VALS.base_is_bottom_layer)
	{
	  gimp_drawable_offsets (layers[layer_num], &orig_x, &orig_y);
	  align_layers_get_align_offsets (layers[layer_num], &offset_x, &offset_y);
	  orig_x += offset_x;
	  orig_y += offset_y;
	  base_x = min_x = max_y = orig_x;
	  base_y = min_y = max_y = orig_y;
	}
      if (1 < visible_layer_num)
	{
	  step_x = (max_x - min_x) / (visible_layer_num - 1);
	  step_y = (max_y - min_y) / (visible_layer_num - 1);
	}
      if ( (VALS.h_style == LEFT2RIGHT) || (VALS.h_style == RIGHT2LEFT))
	base_x = min_x;
      if ( (VALS.v_style == TOP2BOTTOM) || (VALS.v_style == BOTTOM2TOP))
	base_y = min_y;
    }

  return_vals = gimp_run_procedure ("gimp_undo_push_group_start",
				    &retvals,
				    PARAM_IMAGE, image_id,
				    PARAM_END);
  gimp_destroy_params (return_vals, retvals);

  for (vindex = -1, index = 0; index < layer_num; index++)
    {
      if (gimp_layer_get_visible (layers[index])) 
	vindex++;
      else 
	continue;

      gimp_drawable_offsets (layers[index], &orig_x, &orig_y);
      align_layers_get_align_offsets (layers[index], &offset_x, &offset_y);
      
      switch (VALS.h_style)
	{
	case H_NONE:
	  x = orig_x;
	  break;
	case H_COLLECT:
	  x = base_x - offset_x;
	  break;
	case LEFT2RIGHT:
	  x = (base_x + vindex * step_x) - offset_x;
	  break;
	case RIGHT2LEFT:
	  x = (base_x + (visible_layer_num - vindex) * step_x) - offset_x;
	  break;
	case SNAP2HGRID:
	  x = VALS.grid_size
	    * (int) ((orig_x + offset_x + VALS.grid_size /2) / VALS.grid_size)
	    - offset_x;
	  break;
	}
      switch (VALS.v_style)
	{
	case V_NONE:
	  y = orig_y;
	  break;
	case V_COLLECT:
	  y = base_y - offset_y;
	  break;
	case TOP2BOTTOM:
	  y = (base_y + vindex * step_y) - offset_y;
	  break;
	case BOTTOM2TOP:
	  y = (base_y + (visible_layer_num - vindex) * step_y) - offset_y;
	  break;
	case SNAP2VGRID:
	  y = VALS.grid_size 
	    * (int) ((orig_y + offset_y + VALS.grid_size / 2) / VALS.grid_size)
	    - offset_y;
	  break;
	}
      gimp_layer_set_offsets (layers[index], x, y);
    }
  return_vals = gimp_run_procedure ("gimp_undo_push_group_end",
				    &retvals,
				    PARAM_IMAGE, image_id,
				    PARAM_END);
  gimp_destroy_params (return_vals, retvals);
  return STATUS_SUCCESS;
}

static void
align_layers_get_align_offsets (gint32	drawable_id,
			       gint	*x,
			       gint	*y)
{
  GDrawable	*layer = gimp_drawable_get (drawable_id);
  
  switch (VALS.h_base)
    {
    case H_BASE_LEFT:
      *x = 0;
      break;
    case H_BASE_CENTER:
      *x = (gint) (layer->width / 2);
      break;
    case H_BASE_RIGHT:
      *x = layer->width;
      break;
    default:
      *x = 0;
      break;
    }
  switch (VALS.v_base)
    {
    case V_BASE_TOP:
      *y = 0;
      break;
    case V_BASE_CENTER:
      *y = (gint) (layer->height / 2);
      break;
    case V_BASE_BOTTOM:
      *y = layer->height;
      break;
    default:
      *y = 0;
      break;
    }
}

/* dialog stuff */
static int
DIALOG (void)
{
  GtkWidget	*dlg;
  GtkWidget	*frame;
  GtkWidget	*table;
  int		index = 0;
  gchar	buffer[10];

  gtk_init ();

  dlg = gtkW_dialog_new (PLUG_IN_NAME,
			 G_CALLBACK (OK_CALLBACK),
			 G_CALLBACK (gtkW_close_callback));

  frame = gtkW_frame_new (gimp_dialog_get_vbox (dlg), "Parameter settings");
  table = gtkW_table_new (frame, 7, 2);
  gtkW_table_add_menu (table, "Horizontal style", 0, index++,
		       G_CALLBACK (gtkW_menu_update),
		       &VALS.h_style,
		       h_style_menu,
		       sizeof (h_style_menu) / sizeof (h_style_menu[0]));
  gtkW_table_add_menu (table, "Horizontal base", 0, index++,
		       G_CALLBACK (gtkW_menu_update),
		       &VALS.h_base,
		       h_base_menu,
		       sizeof (h_base_menu) / sizeof (h_base_menu[0]));
  gtkW_table_add_menu (table, "Vertical style", 0, index++,
		       G_CALLBACK (gtkW_menu_update),
		       &VALS.v_style,
		       v_style_menu,
		       sizeof (v_style_menu) / sizeof (v_style_menu[0]));
  gtkW_table_add_menu (table, "Vertical base", 0, index++,
		       G_CALLBACK (gtkW_menu_update),
		       &VALS.v_base,
		       v_base_menu,
		       sizeof (v_base_menu) / sizeof (v_base_menu[0]));

  gtkW_table_add_toggle (table, "Ignore the bottom layer even if visible",
			 0, index++,
			 G_CALLBACK (gtkW_toggle_update),
			 &VALS.ignore_bottom);
  gtkW_table_add_toggle (table, "Use the (invisible) bottom layer as the base",
			 0, index++,
			 G_CALLBACK (gtkW_toggle_update),
			 &VALS.base_is_bottom_layer);
  gtkW_table_add_iscale_entry (table, "Grid size",
			       0, index++,
			       G_CALLBACK (gtkW_iscale_update),
			       G_CALLBACK (gtkW_ientry_update),
			       &VALS.grid_size, 0, 200, 1, buffer);

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return INTERFACE.run;
}

static void
OK_CALLBACK (GtkWidget *widget,
	      gpointer   data)
{
  INTERFACE.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void
PREVIEW_UPDATE (void)
{
}

/* gtkW functions: gtkW is the abbreviation of gtk Wrapper */
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
  PREVIEW_UPDATE ();
}

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

static void
gtkW_message_dialog (gint gtk_was_initialized, gchar *message)
{
  GtkWidget *dlg;
  GtkWidget *table;
  GtkWidget *label;

  if (! gtk_was_initialized)
    gtk_init ();

  dlg = gtkW_message_dialog_new (PLUG_IN_NAME);

  table = gimp_table_new (1, 1, FALSE);
  gimp_container_set_border_width (table, GTKW_BORDER_WIDTH);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), table, TRUE, TRUE, 0);

  label = gtk_label_new (message);
  gimp_table_attach (table, label, 0, 1, 0, 1, GIMP_FILL | GIMP_EXPAND,
		     0, 0, 0);
  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();
}

static GtkWidget *
gtkW_message_dialog_new (char * name)
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

  table = gimp_table_new (col, row, FALSE);
  gimp_container_set_border_width (table, GTKW_BORDER_WIDTH);
  gimp_container_add (parent, table);

  return table;
}

static GtkWidget *
gtkW_frame_new (GtkWidget *parent,
		gchar *name)
{
  GtkWidget *frame;

  frame = gtk_frame_new (name);
  gimp_container_set_border_width (frame, GTKW_BORDER_WIDTH);
  if (parent != NULL)
    gimp_box_pack_start (parent, frame, FALSE, FALSE, 0);

  return frame;
}

static void
gtkW_table_add_toggle (GtkWidget	*table,
		       gchar	*name,
		       gint	x,
		       gint	y,
		       GCallback update,
		       gint	*value)
{
  GtkWidget *toggle;

  toggle = gtk_check_button_new_with_label (name);
  gimp_table_attach (table, toggle, x, x + 2, y, y + 1,
		     GIMP_FILL | GIMP_EXPAND, 0, 0, 0);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), *value);
  g_signal_connect (toggle, "toggled", update, value);
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
  gimp_table_attach (table, label, x, x + 1, y, y + 1,
		     GIMP_FILL | GIMP_EXPAND, 0, 5, 0);

  hbox = gimp_hbox_new (FALSE, 5);
  gimp_table_attach (table, hbox, x + 1, x + 2, y, y + 1,
		     GIMP_EXPAND | GIMP_FILL, 0, 0, 0);

  adjustment = gtk_adjustment_new (*value, min, max, step, step, 0.0);

  scale = gimp_hscale_new (adjustment, 0);
  gtk_widget_set_size_request (scale, GTKW_SCALE_WIDTH, -1);
  gimp_box_pack_start (hbox, scale, TRUE, TRUE, 0);
  g_signal_connect (adjustment, "value-changed", scale_update, value);

  entry = gtk_entry_new ();
  g_object_set_data (G_OBJECT (entry), "user_data", adjustment);
  g_object_set_data (G_OBJECT (adjustment), "user_data", entry);
  gimp_box_pack_start (hbox, entry, TRUE, TRUE, 0);
  gtk_widget_set_size_request (entry, GTKW_ENTRY_WIDTH, -1);
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
      sprintf (buffer, "%d", (int) gtk_adjustment_get_value (adjustment));
      gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
      PREVIEW_UPDATE ();
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
	  PREVIEW_UPDATE ();
	}
    }
}

static GtkWidget *
gtkW_table_add_menu (GtkWidget *table,
		     gchar *name,
		     int x,
		     int y,
		     GCallback menu_update,
		     int *val,
		     gtkW_menu_item *item,
		     int item_num)
{
  GtkWidget *label;
  GtkWidget *option_menu;
  gint i;

  label = gtk_label_new (name);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, x, x + 1, y, y + 1,
		     GIMP_FILL | GIMP_EXPAND, 0, GTKW_BORDER_WIDTH, 0);

  option_menu = gimp_option_menu_new ();
  g_object_set_data (G_OBJECT (option_menu), "gtkW_value", val);
  for (i = 0; i < item_num; i++)
    gimp_option_menu_append (option_menu, item[i].name,
			     menu_update, GINT_TO_POINTER (i));
  gimp_option_menu_set_history (option_menu, *val);
  gimp_table_attach (table, option_menu, x + 1, x + 2, y, y + 1,
		     GIMP_FILL | GIMP_EXPAND, 0, 0, 0);

  return option_menu;
}

static void
gtkW_menu_update (GtkWidget *widget,
		  gpointer   data)
{
  gint *val = g_object_get_data (G_OBJECT (widget), "gtkW_value");

  if (val && *val != GPOINTER_TO_INT (data))
    {
      *val = GPOINTER_TO_INT (data);
      PREVIEW_UPDATE ();
    }
}
/* end of gtkW functions */
/* end of align_layers.c */
