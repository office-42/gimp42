/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * IfsCompose is a interface for creating IFS fractals by
 * direct manipulation.
 * Copyright (C) 1997 Owen Taylor
 *
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

/* TODO
 * ----
 *
 * 1. Run in non-interactive mode (need to figure out useful
 *    way for a script to give the 19N paramters for an image).
 *    Perhaps just support saving parameters to a file, script
 *    passes file name.
 * 2. Save settings on a per-layer basis (long term, needs GIMP
 *    support to do properly). Load/save from affine parameters?
 * 3. Figure out if we need multiple phases for supersampled
 *    brushes.
 * 4. (minor) Make undo work correctly when focus is in entry widget.
 */

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"
#include "ifscompose.h"

#ifndef M_PI
#define M_PI    3.14159265358979323846
#endif /* M_PI */

#define SCALE_WIDTH     150
#define ENTRY_WIDTH 60
#define DESIGN_AREA_MAX_SIZE 256

#define PREVIEW_RENDER_CHUNK 10000

#define UNDO_LEVELS 10

typedef enum {
  OP_TRANSLATE,
  OP_ROTATE,			/* or scale */
  OP_STRETCH
} DesignOp;

typedef enum {
  VALUE_PAIR_INT,
  VALUE_PAIR_DOUBLE
} ValuePairType;

typedef struct
{
  GtkAdjustment *adjustment;
  GtkWidget *scale;
  GtkWidget *entry;

  ValuePairType type;

  union {
    gdouble *d;
    gint    *i;
  } data;

  gulong entry_handler_id;
  guint  update_timeout_id;	/* delayed update after the scale moved */
} ValuePair;

typedef struct
{
  IfsComposeVals ifsvals;
  AffElement **elements;
  gint *element_selected;
  gint current_element;
} UndoItem;

typedef struct
{
  IfsColor *color;
  gchar *name;
  GtkWidget *hbox;
  GtkWidget *orig_preview;
  GtkWidget *preview;
  gint fixed_point;

  gint in_change_callback;
} ColorMap;

typedef struct
{
  GtkWidget *dialog;

  ValuePair *iterations_pair;
  ValuePair *subdivide_pair;
  ValuePair *radius_pair;
  ValuePair *memory_pair;
} IfsOptionsDialog;

typedef struct
{
  GtkWidget *area;
  GtkWidget *op_menu;		/* GtkPopoverMenu, child of area */
  gint width;			/* current size of area */
  gint height;

  DesignOp op;
  gdouble op_x;
  gdouble op_y;
  gdouble op_xcenter;
  gdouble op_ycenter;
  gdouble op_center_x;
  gdouble op_center_y;
  guint button_state;
  gint num_selected;
  gdouble drag_start_x;
  gdouble drag_start_y;
} IfsDesignArea;

typedef struct
{
  ValuePair *prob_pair;
  ValuePair *x_pair;
  ValuePair *y_pair;
  ValuePair *scale_pair;
  ValuePair *angle_pair;
  ValuePair *asym_pair;
  ValuePair *shear_pair;
  GtkWidget *flip_check_button;

  ColorMap  *red_cmap;
  ColorMap  *green_cmap;
  ColorMap  *blue_cmap;
  ColorMap  *black_cmap;
  ColorMap  *target_cmap;
  ValuePair *hue_scale_pair;
  ValuePair *value_scale_pair;
  GtkWidget *simple_button;
  GtkWidget *full_button;
  GtkWidget *current_frame;

  GtkWidget *move_button;
  GtkWidget *rotate_button;
  GtkWidget *stretch_button;

  GtkWidget *preview;
  guchar *preview_data;
  gint preview_iterations;
  guint preview_idle_id;

  gint drawable_width,drawable_height;

  AffElement *selected_orig;
  gint current_element;
  AffElementVals current_vals;
  gint auto_preview;

  gint in_update;		/* true if we're currently in
				   update_values() - don't do anything
				   on updates */
} IfsDialog;

typedef struct
{
  gint       run;
} IfsComposeInterface;

/* Declare local functions.
 */
static void      query  (void);
static void      run    (char      *name,
			 int        nparams,
			 GParam    *param,
			 int       *nreturn_vals,
			 GParam   **return_vals);

/*  user interface functions  */
static gint      ifs_compose_dialog     (GDrawable *drawable);
static void      ifs_options_dialog      (void);
static GtkWidget *ifs_compose_trans_page (void);
static GtkWidget *ifs_compose_color_page (void);
static void design_op_menu_popup         (gdouble x, gdouble y);
static void design_op_menu_create        (GtkWidget *window);
static void design_area_create(GtkWidget *window,gint design_width,
			       gint design_height);

/* functions for drawing design window */
static void update_values(void);
static void set_current_element(gint index);
static void design_area_draw(GtkDrawingArea *area, cairo_t *cr,
			     gint width, gint height, gpointer data);
static void design_area_drag_begin(GtkGestureDrag *gesture,
				   gdouble x, gdouble y, gpointer data);
static void design_area_drag_update(GtkGestureDrag *gesture,
				    gdouble offset_x, gdouble offset_y,
				    gpointer data);
static void design_area_drag_end(GtkGestureDrag *gesture,
				 gdouble offset_x, gdouble offset_y,
				 gpointer data);
static void design_area_menu_pressed(GtkGestureClick *gesture, gint n_press,
				     gdouble x, gdouble y, gpointer data);
static void design_area_select_all_callback(GtkWidget *w, gpointer data);
static void design_area_resize(GtkDrawingArea *area, gint width, gint height,
			       gpointer data);
static void design_area_motion(gdouble x, gdouble y);
static void design_area_redraw(void);

/* Undo ring functions */
static void undo_begin(void);
static void undo_update(gint element);
static void undo_exchange(gint el);
static void undo(void);
static void redo(void);

static void recompute_center(int save_undo);
static void recompute_center_cb(GtkWidget *, gpointer);

static void ifs_compose(GDrawable *drawable);

static void color_map_set_preview_color(GtkWidget *preview,
					IfsColor *color);
static ColorMap *color_map_create(gchar *name,IfsColor *orig_color,
				  IfsColor *data, gint fixed_point);
static void color_map_clicked_callback(GtkWidget *widget,ColorMap *colormap);
static void color_map_color_chosen_cb(const guchar *rgb,
				      gpointer      data);
static void color_map_update(ColorMap *color_map);

/* interface functions */
static void simple_color_toggled(GtkWidget *widget,gpointer data);
static void simple_color_set_sensitive(void);
static void val_changed_update (void);
static ValuePair *value_pair_create (gpointer data, gdouble lower, gdouble upper,
	      gboolean create_scale, ValuePairType type);
static void value_pair_update(ValuePair *value_pair);
static void value_pair_entry_callback (GtkWidget   *w,
				       ValuePair   *value_pair);
static void value_pair_destroy_callback (GtkWidget   *widget,
					 ValuePair   *value_pair);
static gboolean value_pair_delayed_update (gpointer data);
static void value_pair_scale_callback   (GtkAdjustment *adjustment,
					 ValuePair *value_pair);

static void auto_preview_callback (GtkWidget *widget, gpointer data);
static void design_op_callback (GtkWidget *widget, gpointer data);
static void design_op_update_callback (GtkWidget *widget, gpointer data);
static void flip_check_button_callback (GtkWidget *widget, gpointer data);
static gboolean preview_idle_render(gpointer data);

static void ifs_options_close_callback (void);
static void ifs_compose_set_defaults (void);
static void ifs_compose_defaults_callback (GtkWidget *widget,
					   gpointer   data);
static void ifs_compose_new_callback (GtkWidget *widget,
				      gpointer   data);
static void ifs_compose_delete_callback (GtkWidget *widget,
					 gpointer   data);
static void ifs_compose_preview_callback (GtkWidget *widget,
					  GtkWidget *preview);

static void ifs_compose_close_callback (GtkWidget *widget,
					GtkWidget **destroyed_widget);
static void ifs_compose_ok_callback (GtkWidget *widget,
				     GtkWidget *window);


/*
 *  Some static variables
 */

IfsDialog *ifsD = 0;
IfsOptionsDialog *ifsOptD = 0;
IfsDesignArea *ifsDesign = 0;

static AffElement **elements = 0;
static gint *element_selected = 0;
static gint element_count = 0;

static UndoItem undo_ring[UNDO_LEVELS];
static gint undo_cur = -1;
static gint undo_num = 0;
static gint undo_start = 0;

/* num_elements = 0, signals not inited */
static IfsComposeVals ifsvals =
{
  0,				/* num_elements */
  50000,			/* iterations */
  4096,				/* max_memory */
  4,				/* subdivide */
  0.75,				/* radius */
  1.0,				/* aspect ratio */
  0.5,				/* center_x */
  0.5,				/* center_y */
};

static
 IfsComposeInterface ifscint =
{
  FALSE,        /* run */
};

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
  static GParamDef args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image" },
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
  };

  static GParamDef *return_vals = NULL;

  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0;

  gimp_install_procedure ("plug_in_ifs_compose",
			  "Create an Iterated Function System Fractal",
   "Interactively create an Iterated Function System fractal."
   "Use the window on the upper left to adjust the component"
   "transformations of the fractal. The operation that is performed"
   "is selected by the buttons underneath the window, or from a"
   "menu popped up by the right mouse button. The fractal will be"
   "rendered with a transparent background if the current image has"
   "a transparent background.",
			  "Owen Taylor",
			  "Owen Taylor",
			  "1997",
			  "<Image>/Filters/Render/IfsCompose",
			  "RGB*, GRAY*",
			  PROC_PLUG_IN,
			  nargs, nreturn_vals,
			  args, return_vals);
}

static void
run (char    *name,
     int      nparams,
     GParam  *param,
     int     *nreturn_vals,
     GParam **return_vals)
{
  static GParam values[1];
  GDrawable *active_drawable;
  GRunModeType run_mode;
  GStatusType status = STATUS_SUCCESS;

  run_mode = param[0].data.d_int32;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;

  *nreturn_vals = 1;
  *return_vals = values;

  /*  getchar(); */

  /*  Get the active drawable  */
  active_drawable = gimp_drawable_get (param[2].data.d_drawable);

  switch (run_mode)
    {
    case RUN_INTERACTIVE:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_ifs_compose_vals", &ifsvals);

      if (ifsvals.num_elements != 0)
        {
	  AffElementVals *element_vals = g_new(AffElementVals,
					       ifsvals.num_elements);
	  IfsColor color = {{0.0,0.0,0.0}};
	  int i;
	  elements = g_new(AffElement *,ifsvals.num_elements);
	  gimp_get_data ("plug_in_ifs_compose_elements", element_vals);

	  for (i=0;i<ifsvals.num_elements;i++)
	    {
	      elements[i] = aff_element_new(0.0,0.0,color,element_count++);
	      elements[i]->v = element_vals[i];
	    }
	  g_free(element_vals);
	}

      /*  First acquire information with a dialog  */
      if (! ifs_compose_dialog (active_drawable))
	return;
      break;

    case RUN_NONINTERACTIVE:
      /*  Make sure all the arguments are there!  */
      status = STATUS_CALLING_ERROR;
      break;

    case RUN_WITH_LAST_VALS:
      /*  Possibly retrieve data  */
      status = STATUS_CALLING_ERROR;
/*      gimp_get_data ("plug_in_ifs_compose", &mvals); */
      break;

    default:
      break;
    }

  /*  Render the fractal  */
  if ((status == STATUS_SUCCESS) &&
      (gimp_drawable_color (active_drawable->id) || gimp_drawable_gray (active_drawable->id)))
    {
      /*  set the tile cache size so that the gaussian blur works well  */
      gimp_tile_cache_ntiles (2 * (MAX (active_drawable->width, active_drawable->height) /
				   gimp_tile_width () + 1));

      /*  run the effect  */
      ifs_compose (active_drawable);

      /*  If the run mode is interactive, flush the displays  */
      if (run_mode != RUN_NONINTERACTIVE)
	gimp_displays_flush ();

      /*  Store data for next invocation */
     if (run_mode == RUN_INTERACTIVE)
       {
	 AffElementVals *element_vals = g_new(AffElementVals,
					      ifsvals.num_elements);
	 int i;

	 for (i=0;i<ifsvals.num_elements;i++)
	   element_vals[i] = elements[i]->v;

	 gimp_set_data ("plug_in_ifs_compose_vals", &ifsvals,
			sizeof (IfsComposeVals));
	 gimp_set_data ("plug_in_ifs_compose_elements", element_vals,
			sizeof (AffElementVals)*ifsvals.num_elements);

	 g_free(element_vals);
       }
    }
  else if (status == STATUS_SUCCESS)
    {
      /* gimp_message ("mosaic: cannot operate on indexed color images"); */
      status = STATUS_EXECUTION_ERROR;
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (active_drawable);
}

/* Puts a label into table, aligned as gtk_misc_set_alignment () did. */
static GtkWidget *
table_attach_label (GtkWidget   *table,
		    const gchar *text,
		    gfloat       xalign,
		    gfloat       yalign,
		    gint         left,
		    gint         right,
		    gint         top,
		    gint         bottom)
{
  GtkWidget *label;

  label = gtk_label_new (text);
  gtk_label_set_xalign (GTK_LABEL (label), xalign);
  gtk_label_set_yalign (GTK_LABEL (label), yalign);
  gimp_table_attach (table, label, left, right, top, bottom,
		     GIMP_FILL, GIMP_FILL, 4, 0);

  return label;
}

static GtkWidget *
ifs_compose_trans_page (void)
{
  GtkWidget *vbox;
  GtkWidget *table;

  vbox = gimp_vbox_new(FALSE, 0);
  gimp_container_set_border_width(vbox, 4);

  table = gimp_table_new(3, 6, FALSE);
  gtk_grid_set_row_spacing(GTK_GRID(table),6);
  gimp_box_pack_start(vbox, table, TRUE, TRUE, 0);

  /* X */

  table_attach_label (table, "X", 0.0, 1.0, 0, 1, 0, 1);

  ifsD->x_pair = value_pair_create(&ifsD->current_vals.x, 0.0, 1.0, FALSE,
				   VALUE_PAIR_DOUBLE);
  gimp_table_attach(table, ifsD->x_pair->entry,1,2,0,1,
		    GIMP_FILL,GIMP_FILL,4,0);

  /* Y */

  table_attach_label (table, "Y", 0.0, 1.0, 0, 1, 1, 2);

  ifsD->y_pair = value_pair_create(&ifsD->current_vals.y, 0.0, 1.0, FALSE,
				   VALUE_PAIR_DOUBLE);
  gimp_table_attach(table, ifsD->y_pair->entry,1,2,1,2,
		    GIMP_FILL,GIMP_FILL,4,0);

  /* Scale */

  table_attach_label (table, "Scale", 0.0, 1.0, 2, 3, 0, 1);

  ifsD->scale_pair = value_pair_create(&ifsD->current_vals.scale, 0.0,1.0,
				       FALSE, VALUE_PAIR_DOUBLE);
  gimp_table_attach(table, ifsD->scale_pair->entry,3,4,0,1,
		    GIMP_FILL,GIMP_FILL,4,0);

  /* Angle */

  table_attach_label (table, "Angle", 0.0, 1.0, 2, 3, 1, 2);

  ifsD->angle_pair = value_pair_create(&ifsD->current_vals.theta,-180,180,
				       FALSE, VALUE_PAIR_DOUBLE);
  gimp_table_attach(table, ifsD->angle_pair->entry,3,4,1,2,
		    GIMP_FILL,GIMP_FILL,4,0);

  /* Asym */

  table_attach_label (table, "Asymmetry", 0.0, 1.0, 4, 5, 0, 1);

  ifsD->asym_pair = value_pair_create(&ifsD->current_vals.asym,0.10,10.0,
				      FALSE, VALUE_PAIR_DOUBLE);
  gimp_table_attach(table, ifsD->asym_pair->entry,5,6,0,1,
		    GIMP_FILL,GIMP_FILL,4,0);

  /* Shear */

  table_attach_label (table, "Shear", 0.0, 1.0, 4, 5, 1, 2);

  ifsD->shear_pair = value_pair_create(&ifsD->current_vals.shear,-10.0,10.0,
				       FALSE, VALUE_PAIR_DOUBLE);
  gimp_table_attach(table, ifsD->shear_pair->entry,5,6,1,2,
		    GIMP_FILL,GIMP_FILL,4,0);

  /* Flip */

  ifsD->flip_check_button = gtk_check_button_new_with_label("Flip");
  gimp_table_attach(table, ifsD->flip_check_button,0,1,2,3,
		    GIMP_FILL,GIMP_FILL,4,0);
  g_signal_connect(ifsD->flip_check_button, "toggled",
		   G_CALLBACK(flip_check_button_callback),NULL);

  return vbox;
}

static GtkWidget *
ifs_compose_color_page (void)
{
  GtkWidget *vbox;
  GtkWidget *table;
  IfsColor color;

  vbox = gimp_vbox_new(FALSE, 0);
  gimp_container_set_border_width(vbox, 4);

  table = gimp_table_new(3, 5, FALSE);
  gtk_grid_set_row_spacing(GTK_GRID(table),6);
  gimp_box_pack_start(vbox, table, TRUE, TRUE, 0);

  /* Simple color control section */

  ifsD->simple_button = gimp_radio_button_new (NULL, "Simple");
  gimp_table_attach(table, ifsD->simple_button, 0, 1, 0, 2,
		    GIMP_FILL, GIMP_FILL, 4, 0);
  g_signal_connect (ifsD->simple_button, "toggled",
		    G_CALLBACK (simple_color_toggled), NULL);

  color.vals[0] = 1.0;
  color.vals[1] = 0.0;
  color.vals[2] = 0.0;
  ifsD->target_cmap = color_map_create("IfsCompose: Target",NULL,
				       &ifsD->current_vals.target_color,TRUE);
  gimp_table_attach(table, ifsD->target_cmap->hbox, 1, 2, 0, 2,
		    GIMP_FILL, 0, 4, 0);

  table_attach_label (table, "Scale hue by:", 1.0, 0.5, 2, 3, 0, 1);

  ifsD->hue_scale_pair = value_pair_create(&ifsD->current_vals.hue_scale,
				       0.0,1.0, TRUE, VALUE_PAIR_DOUBLE);
  gimp_table_attach(table, ifsD->hue_scale_pair->scale, 3, 4, 0, 1,
		    GIMP_FILL, GIMP_FILL, 4, 0);
  gimp_table_attach(table, ifsD->hue_scale_pair->entry, 4, 5, 0, 1,
		    GIMP_FILL, GIMP_FILL, 4, 0);

  table_attach_label (table, "Scale value by:", 1.0, 0.5, 2, 3, 1, 2);

  ifsD->value_scale_pair = value_pair_create(&ifsD->current_vals.value_scale,
				       0.0,1.0, TRUE, VALUE_PAIR_DOUBLE);
  gimp_table_attach(table, ifsD->value_scale_pair->scale,
		    3, 4, 1, 2, GIMP_FILL, GIMP_FILL, 4, 0);
  gimp_table_attach(table, ifsD->value_scale_pair->entry,
		    4, 5, 1, 2, GIMP_FILL, GIMP_FILL, 4, 0);

  /* Full color control section */

  ifsD->full_button = gimp_radio_button_new (ifsD->simple_button, "Full");
  gimp_table_attach(table, ifsD->full_button, 0, 1, 2, 3,
		    GIMP_FILL, GIMP_FILL, 4, 0);

  color.vals[0] = 1.0;
  color.vals[1] = 0.0;
  color.vals[2] = 0.0;
  ifsD->red_cmap = color_map_create("IfsCompose: Red",&color,
				    &ifsD->current_vals.red_color,FALSE);
  gimp_table_attach(table, ifsD->red_cmap->hbox, 1, 2, 2, 3,
		    GIMP_FILL, GIMP_FILL, 4, 0);

  color.vals[0] = 0.0;
  color.vals[1] = 1.0;
  color.vals[2] = 0.0;
  ifsD->green_cmap = color_map_create("IfsCompose: Green",&color,
				    &ifsD->current_vals.green_color,FALSE);
  gimp_table_attach(table, ifsD->green_cmap->hbox, 2, 3, 2, 3,
		    GIMP_FILL, GIMP_FILL, 4, 0);

  color.vals[0] = 0.0;
  color.vals[1] = 0.0;
  color.vals[2] = 2.0;
  ifsD->blue_cmap = color_map_create("IfsCompose: Blue",&color,
				    &ifsD->current_vals.blue_color,FALSE);
  gimp_table_attach(table, ifsD->blue_cmap->hbox, 3, 4, 2, 3,
		    GIMP_FILL, GIMP_FILL, 4, 0);

  color.vals[0] = 0.0;
  color.vals[1] = 0.0;
  color.vals[2] = 0.0;
  ifsD->black_cmap = color_map_create("IfsCompose: Black",&color,
				    &ifsD->current_vals.black_color,FALSE);
  gimp_table_attach(table, ifsD->black_cmap->hbox, 4, 5, 2, 3,
		    GIMP_FILL, GIMP_FILL, 4, 0);

  return vbox;
}

/* A frame drawn around child, inside an aspect frame of the given ratio;
   GTK 4 aspect frames have no border of their own. */
static GtkWidget *
ifs_aspect_frame_new (GtkWidget *child,
		      gdouble    ratio)
{
  GtkWidget *aspect_frame;
  GtkWidget *frame;

  aspect_frame = gtk_aspect_frame_new (0.5, 0.5, ratio, FALSE);
  frame = gtk_frame_new (NULL);
  gtk_frame_set_child (GTK_FRAME (frame), child);
  gtk_aspect_frame_set_child (GTK_ASPECT_FRAME (aspect_frame), frame);

  return aspect_frame;
}

static gint
ifs_compose_dialog (GDrawable *drawable)
{
  GtkWidget *dlg;
  GtkWidget *label;
  GtkWidget *button;
  GtkWidget *check_button;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *util_hbox;
  GtkWidget *main_vbox;
  GtkWidget *aspect_frame;
  GtkWidget *notebook;
  GtkWidget *page;

  gint design_width, design_height;

  design_width = drawable->width;
  design_height = drawable->height;

  if (design_width > design_height)
    {
      if (design_width > DESIGN_AREA_MAX_SIZE)
	{
	  design_height = design_height * DESIGN_AREA_MAX_SIZE / design_width;
	  design_width = DESIGN_AREA_MAX_SIZE;
	}
    }
  else
    {
      if (design_height > DESIGN_AREA_MAX_SIZE)
	{
	  design_width = design_width * DESIGN_AREA_MAX_SIZE / design_height;
	  design_height = DESIGN_AREA_MAX_SIZE;
	}
    }

  ifsD = g_new(IfsDialog,1);
  ifsD->auto_preview = TRUE;
  ifsD->drawable_width = drawable->width;
  ifsD->drawable_height = drawable->height;

  ifsD->selected_orig = NULL;

  ifsD->preview_data = NULL;
  ifsD->preview_iterations = 0;
  ifsD->preview_idle_id = 0;

  ifsD->in_update = 0;

  gtk_init ();

  dlg = gimp_dialog_new ("IfsCompose");
  g_signal_connect (dlg, "destroy",
		    G_CALLBACK (ifs_compose_close_callback),
		    &dlg);

  /*  Action area  */

  gimp_dialog_add_button (dlg, "New",
			  G_CALLBACK (ifs_compose_new_callback), dlg, FALSE);
  gimp_dialog_add_button (dlg, "Delete",
			  G_CALLBACK (ifs_compose_delete_callback), dlg, FALSE);
  gimp_dialog_add_button (dlg, "Defaults",
			  G_CALLBACK (ifs_compose_defaults_callback), NULL,
			  FALSE);
  gimp_dialog_add_button (dlg, "OK",
			  G_CALLBACK (ifs_compose_ok_callback), dlg, TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  /*  The main vbox */
  main_vbox = gimp_vbox_new (FALSE, 0);
  gimp_container_set_border_width (main_vbox, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), main_vbox, TRUE, TRUE, 0);

  /*  The design area */
  hbox = gimp_hbox_new (FALSE, 5);
  gimp_box_pack_start (main_vbox, hbox, TRUE, TRUE, 0);

  design_area_create(dlg,design_width,design_height);
  aspect_frame = ifs_aspect_frame_new (ifsDesign->area,
				       (gdouble)design_width/design_height);
  gimp_box_pack_start (hbox, aspect_frame, TRUE, TRUE, 0);

  /* the preview */

  ifsD->preview = gimp_preview_new (GIMP_PREVIEW_COLOR);
  gimp_preview_size (GIMP_PREVIEW (ifsD->preview),design_width,design_height);
  aspect_frame = ifs_aspect_frame_new (ifsD->preview,
				       (gdouble)design_width/design_height);
  gimp_box_pack_start (hbox, aspect_frame, TRUE, TRUE, 0);

  /* Iterations and preview options */

  hbox = gimp_hbox_new(FALSE,1);
  gimp_box_pack_start (main_vbox, hbox, FALSE, FALSE, 5);

  util_hbox = gimp_hbox_new(FALSE,5);
  gtk_box_append(GTK_BOX(hbox), util_hbox);

  /* the three operation buttons form a group: exactly one is active */
  ifsD->move_button = gtk_toggle_button_new_with_label("Move");
  gimp_box_pack_start (util_hbox, ifsD->move_button, TRUE, TRUE, 0);

  ifsD->rotate_button = gtk_toggle_button_new_with_label("Rotate/Scale");
  gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (ifsD->rotate_button),
			       GTK_TOGGLE_BUTTON (ifsD->move_button));
  gimp_box_pack_start (util_hbox, ifsD->rotate_button, TRUE, TRUE, 0);

  ifsD->stretch_button = gtk_toggle_button_new_with_label("Stretch");
  gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (ifsD->stretch_button),
			       GTK_TOGGLE_BUTTON (ifsD->move_button));
  gimp_box_pack_start (util_hbox, ifsD->stretch_button, TRUE, TRUE, 0);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ifsD->move_button),TRUE);

  g_signal_connect(ifsD->move_button,"toggled",
		   G_CALLBACK(design_op_callback),
		   GINT_TO_POINTER(OP_TRANSLATE));
  g_signal_connect(ifsD->rotate_button,"toggled",
		   G_CALLBACK(design_op_callback),
		   GINT_TO_POINTER(OP_ROTATE));
  g_signal_connect(ifsD->stretch_button,"toggled",
		   G_CALLBACK(design_op_callback),
		   GINT_TO_POINTER(OP_STRETCH));

  util_hbox = gimp_hbox_new(FALSE,5);
  gtk_widget_set_halign (util_hbox, GTK_ALIGN_END);
  gtk_widget_set_valign (util_hbox, GTK_ALIGN_CENTER);
  gimp_box_pack_start (hbox, util_hbox, TRUE, TRUE, 0);

  button = gtk_button_new_with_label ("Render Options");
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (ifs_options_dialog),
			    NULL);
  gimp_box_pack_start (util_hbox, button, TRUE, TRUE, 0);

  button = gtk_button_new_with_label ("Preview");
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (ifs_compose_preview_callback),
			    ifsD->preview);
  gimp_box_pack_start (util_hbox, button, TRUE, TRUE, 0);

  check_button = gtk_check_button_new_with_label ("Auto");
  gtk_box_append (GTK_BOX (util_hbox), check_button);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (check_button),
			       ifsD->auto_preview);
  g_signal_connect (check_button, "toggled",
		    G_CALLBACK (auto_preview_callback),
		    NULL);

  /* The current transformation frame */

  ifsD->current_frame = gtk_frame_new(NULL);
  gtk_box_append(GTK_BOX(main_vbox),ifsD->current_frame);

  vbox = gimp_vbox_new(FALSE,0);
  gimp_container_set_border_width (vbox, 5);
  gtk_frame_set_child (GTK_FRAME(ifsD->current_frame), vbox);

  /* The notebook */

  notebook = gtk_notebook_new();
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
  gimp_box_pack_start(vbox,notebook,FALSE,FALSE,5);

  page = ifs_compose_trans_page();
  label = gtk_label_new("Spatial Transformation");
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, label);

  page = ifs_compose_color_page();
  label = gtk_label_new("Color Transformation");
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, label);

  /* The probability entry */

  hbox = gimp_hbox_new(FALSE,5);
  gimp_box_pack_start (vbox, hbox, FALSE, FALSE, 5);
  label = gtk_label_new ("Relative Probability:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_box_append(GTK_BOX (hbox), label);

  ifsD->prob_pair = value_pair_create(&ifsD->current_vals.prob,0.0,5.0, TRUE,
				      VALUE_PAIR_DOUBLE);
  gimp_box_pack_start (hbox, ifsD->prob_pair->scale, TRUE, TRUE, 0);
  gimp_box_pack_start (hbox, ifsD->prob_pair->entry, FALSE, TRUE, 0);

  if (ifsvals.num_elements == 0)
    {
      ifs_compose_set_defaults();
      if (ifsD->auto_preview)
	ifs_compose_preview_callback(NULL, ifsD->preview);
    }
  else
    {
      int i;
      gdouble ratio = (gdouble)ifsD->drawable_height/ifsD->drawable_width;

      element_selected = g_new(gint, ifsvals.num_elements);
      element_selected[0] = TRUE;
      for (i=1;i<ifsvals.num_elements;i++)
	element_selected[i] = FALSE;

      if (ratio != ifsvals.aspect_ratio)
	{
	  /* Adjust things so that what fit onto the old image, fits
	     onto the new image */
	  Aff2 t1,t2,t3;
	  gdouble x_offset, y_offset;
	  gdouble center_x, center_y;
	  gdouble scale;

	  if (ratio < ifsvals.aspect_ratio)
	    {
	      scale = ratio/ifsvals.aspect_ratio;
	      x_offset = (1-scale)/2;
	      y_offset = 0;
	    }
	  else
	    {
	      scale = 1;
	      x_offset = 0;
	      y_offset = (ratio - ifsvals.aspect_ratio)/2;
	    }
	  aff2_scale(&t1,scale,0);
	  aff2_translate(&t2,x_offset,y_offset);
	  aff2_compose(&t3,&t2,&t1);
	  aff2_invert(&t1,&t3);

	  aff2_apply(&t3,ifsvals.center_x,ifsvals.center_y,&center_x,
		     &center_y);

	  for (i=0;i<ifsvals.num_elements;i++)
	    {
	      aff_element_compute_trans(elements[i],1,ifsvals.aspect_ratio,
					ifsvals.center_x,ifsvals.center_y);
	      aff2_compose(&t2,&elements[i]->trans,&t1);
	      aff2_compose(&elements[i]->trans,&t3,&t2);
	      aff_element_decompose_trans(elements[i],&elements[i]->trans,
					   1,ifsvals.aspect_ratio,
					   center_x,center_y);
	    }
	  ifsvals.center_x = center_x;
	  ifsvals.center_y = center_y;

	  ifsvals.aspect_ratio = ratio;
	}

      for (i=0;i<ifsvals.num_elements;i++)
	aff_element_compute_color_trans(elements[i]);

      /* compute the spatial transformations and boundaries for the
	 initial size; they are recomputed when the design area is
	 resized */
      for (i=0;i<ifsvals.num_elements;i++)
	aff_element_compute_trans(elements[i],
				  ifsDesign->width, ifsDesign->height,
				  ifsvals.center_x, ifsvals.center_y);
      for (i=0;i<ifsvals.num_elements;i++)
	aff_element_compute_boundary(elements[i],
				     ifsDesign->width, ifsDesign->height,
				     elements, ifsvals.num_elements);

      set_current_element(0);
      if (ifsD->auto_preview)
	ifs_compose_preview_callback(NULL, ifsD->preview);

      ifsD->selected_orig = g_new(AffElement,ifsvals.num_elements);
    }

  gtk_window_present (GTK_WINDOW (dlg));
  gimp_main_loop_run ();

  if (ifsD->preview_idle_id)
    {
      g_source_remove (ifsD->preview_idle_id);
      ifsD->preview_idle_id = 0;
    }

  if (dlg)
    gtk_window_destroy (GTK_WINDOW (dlg));

  if (ifsOptD)
    {
      gtk_window_destroy (GTK_WINDOW (ifsOptD->dialog));
      g_free (ifsOptD);
      ifsOptD = NULL;
    }

  g_free(ifsD->preview_data);
  g_free(ifsD);
  ifsD = NULL;

  return ifscint.run;
}

static void
design_area_create(GtkWidget *window,gint design_width,gint design_height)
{
  GtkGesture *drag;
  GtkGesture *click;

  ifsDesign = g_new(IfsDesignArea,1);

  ifsDesign->op = OP_TRANSLATE;
  ifsDesign->button_state = 0;
  ifsDesign->width = design_width;
  ifsDesign->height = design_height;
  ifsDesign->drag_start_x = 0.0;
  ifsDesign->drag_start_y = 0.0;

  ifsDesign->area = gtk_drawing_area_new();
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (ifsDesign->area),
				      design_width);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (ifsDesign->area),
				       design_height);
  gtk_widget_set_focusable (ifsDesign->area, TRUE);

  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (ifsDesign->area),
				  design_area_draw, NULL, NULL);
  g_signal_connect (ifsDesign->area, "resize",
		    G_CALLBACK (design_area_resize), NULL);

  /* button 1 manipulates the elements */
  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), GDK_BUTTON_PRIMARY);
  g_signal_connect (drag, "drag-begin",
		    G_CALLBACK (design_area_drag_begin), NULL);
  g_signal_connect (drag, "drag-update",
		    G_CALLBACK (design_area_drag_update), NULL);
  g_signal_connect (drag, "drag-end",
		    G_CALLBACK (design_area_drag_end), NULL);
  gtk_widget_add_controller (ifsDesign->area, GTK_EVENT_CONTROLLER (drag));

  /* button 3 pops up the operations menu */
  click = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click),
				 GDK_BUTTON_SECONDARY);
  g_signal_connect (click, "pressed",
		    G_CALLBACK (design_area_menu_pressed), NULL);
  gtk_widget_add_controller (ifsDesign->area, GTK_EVENT_CONTROLLER (click));

  design_op_menu_create(window);
}

static void
design_op_action_cb (GSimpleAction *action,
		     GVariant      *parameter,
		     gpointer       data)
{
  design_op_update_callback (NULL, data);
}

static void
design_select_all_action_cb (GSimpleAction *action,
			     GVariant      *parameter,
			     gpointer       data)
{
  design_area_select_all_callback (NULL, NULL);
}

static void
design_recompute_center_action_cb (GSimpleAction *action,
				   GVariant      *parameter,
				   gpointer       data)
{
  recompute_center_cb (NULL, NULL);
}

static void
design_undo_action_cb (GSimpleAction *action,
		       GVariant      *parameter,
		       gpointer       data)
{
  undo ();
}

static void
design_redo_action_cb (GSimpleAction *action,
		       GVariant      *parameter,
		       gpointer       data)
{
  redo ();
}

static void
design_op_menu_add_action (GSimpleActionGroup *group,
			   const gchar        *name,
			   GCallback           callback,
			   gpointer            data)
{
  GSimpleAction *action;

  action = g_simple_action_new (name, NULL);
  g_signal_connect (action, "activate", callback, data);
  g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
  g_object_unref (action);
}

static void
design_op_menu_add_item (GMenu       *menu,
			 const gchar *label,
			 const gchar *action,
			 const gchar *accel)
{
  GMenuItem *item;

  item = g_menu_item_new (label, action);
  g_menu_item_set_attribute (item, "accel", "s", accel);
  g_menu_append_item (menu, item);
  g_object_unref (item);
}

static void
design_op_menu_add_shortcut (GtkEventController *controller,
			     const gchar        *trigger,
			     const gchar        *action)
{
  gtk_shortcut_controller_add_shortcut
    (GTK_SHORTCUT_CONTROLLER (controller),
     gtk_shortcut_new (gtk_shortcut_trigger_parse_string (trigger),
		       gtk_named_action_new (action)));
}

static void
design_area_destroy_cb (GtkWidget *widget,
			gpointer   data)
{
  if (ifsDesign->op_menu)
    {
      gtk_widget_unparent (ifsDesign->op_menu);
      ifsDesign->op_menu = NULL;
    }
}

static void
design_op_menu_create(GtkWidget *window)
{
  GSimpleActionGroup *group;
  GMenu *menu;
  GMenu *section;
  GtkEventController *shortcuts;

  group = g_simple_action_group_new ();
  design_op_menu_add_action (group, "move",
			     G_CALLBACK (design_op_action_cb),
			     GINT_TO_POINTER (OP_TRANSLATE));
  design_op_menu_add_action (group, "rotate",
			     G_CALLBACK (design_op_action_cb),
			     GINT_TO_POINTER (OP_ROTATE));
  design_op_menu_add_action (group, "stretch",
			     G_CALLBACK (design_op_action_cb),
			     GINT_TO_POINTER (OP_STRETCH));
  design_op_menu_add_action (group, "select-all",
			     G_CALLBACK (design_select_all_action_cb), NULL);
  design_op_menu_add_action (group, "recompute-center",
			     G_CALLBACK (design_recompute_center_action_cb),
			     NULL);
  design_op_menu_add_action (group, "undo",
			     G_CALLBACK (design_undo_action_cb), NULL);
  design_op_menu_add_action (group, "redo",
			     G_CALLBACK (design_redo_action_cb), NULL);
  gtk_widget_insert_action_group (window, "ifs", G_ACTION_GROUP (group));
  g_object_unref (group);

  menu = g_menu_new ();

  section = g_menu_new ();
  design_op_menu_add_item (section, "Move", "ifs.move", "m");
  design_op_menu_add_item (section, "Rotate/Scale", "ifs.rotate", "r");
  design_op_menu_add_item (section, "Stretch", "ifs.stretch", "s");
  g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
  g_object_unref (section);

  section = g_menu_new ();
  design_op_menu_add_item (section, "Select All", "ifs.select-all",
			   "<Control>a");
  design_op_menu_add_item (section, "Recompute Center",
			   "ifs.recompute-center", "<Alt>r");
  design_op_menu_add_item (section, "Undo", "ifs.undo", "<Control>z");
  design_op_menu_add_item (section, "Redo", "ifs.redo", "<Control>r");
  g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
  g_object_unref (section);

  ifsDesign->op_menu = gtk_popover_menu_new_from_model (G_MENU_MODEL (menu));
  gtk_popover_set_has_arrow (GTK_POPOVER (ifsDesign->op_menu), FALSE);
  gtk_widget_set_halign (ifsDesign->op_menu, GTK_ALIGN_START);
  gtk_widget_set_parent (ifsDesign->op_menu, ifsDesign->area);
  g_object_unref (menu);

  g_signal_connect (ifsDesign->area, "destroy",
		    G_CALLBACK (design_area_destroy_cb), NULL);

  /* The plain-key accelerators only work while the design area has the
     focus, so they don't get in the way of typing into the entries;
     the ones with modifiers work anywhere in the window. */
  shortcuts = gtk_shortcut_controller_new ();
  design_op_menu_add_shortcut (shortcuts, "m", "ifs.move");
  design_op_menu_add_shortcut (shortcuts, "r", "ifs.rotate");
  design_op_menu_add_shortcut (shortcuts, "s", "ifs.stretch");
  gtk_widget_add_controller (ifsDesign->area, shortcuts);

  shortcuts = gtk_shortcut_controller_new ();
  gtk_shortcut_controller_set_scope (GTK_SHORTCUT_CONTROLLER (shortcuts),
				     GTK_SHORTCUT_SCOPE_LOCAL);
  design_op_menu_add_shortcut (shortcuts, "<Control>a", "ifs.select-all");
  design_op_menu_add_shortcut (shortcuts, "<Alt>r", "ifs.recompute-center");
  design_op_menu_add_shortcut (shortcuts, "<Control>z", "ifs.undo");
  design_op_menu_add_shortcut (shortcuts, "<Control>r", "ifs.redo");
  gtk_widget_add_controller (window, shortcuts);
}

static void
design_op_menu_popup(gdouble x, gdouble y)
{
  GdkRectangle rect;

  rect.x = x;
  rect.y = y;
  rect.width = 1;
  rect.height = 1;
  gtk_popover_set_pointing_to (GTK_POPOVER (ifsDesign->op_menu), &rect);
  gtk_popover_popup (GTK_POPOVER (ifsDesign->op_menu));
}

static void
ifs_options_dialog (void)
{
  GtkWidget *table;

  if (!ifsOptD)
    {
      ifsOptD = g_new(IfsOptionsDialog,1);

      ifsOptD->dialog = gimp_dialog_new("IfsCompose Options");
      /* closing the window only hides it, like the Close button */
      gtk_window_set_hide_on_close (GTK_WINDOW (ifsOptD->dialog), TRUE);

      /* Action area */

      gimp_dialog_add_button (ifsOptD->dialog, "Close",
			      G_CALLBACK (ifs_options_close_callback),
			      NULL, TRUE);

      /* Table of options */

      table = gimp_table_new(4,3,FALSE);
      gimp_container_set_border_width(table,10);
      gtk_grid_set_row_spacing(GTK_GRID(table), 4);
      gtk_grid_set_column_spacing(GTK_GRID(table), 4);
      gtk_box_append(GTK_BOX(gimp_dialog_get_vbox(ifsOptD->dialog)), table);

      table_attach_label (table, "Max. Memory:", 1.0, 0.5, 0, 1, 0, 1);

      ifsOptD->memory_pair = value_pair_create(&ifsvals.max_memory,
					       1,1000000,FALSE,
					       VALUE_PAIR_INT);
      gimp_table_attach(table, ifsOptD->memory_pair->entry,
			1, 2, 0, 1, GIMP_FILL, GIMP_FILL, 4, 0);

      table_attach_label (table, "Iterations:", 1.0, 0.5, 0, 1, 1, 2);

      ifsOptD->iterations_pair = value_pair_create(&ifsvals.iterations,1,10000000,FALSE,
						VALUE_PAIR_INT);
      gimp_table_attach(table, ifsOptD->iterations_pair->entry,
			1, 2, 1, 2, GIMP_FILL, GIMP_FILL, 4, 0);

      table_attach_label (table, "Subdivide:", 1.0, 0.5, 0, 1, 2, 3);

      ifsOptD->subdivide_pair = value_pair_create(&ifsvals.subdivide,1,10,
						  FALSE,
						  VALUE_PAIR_INT);
      gimp_table_attach(table, ifsOptD->subdivide_pair->entry,
			1, 2, 2, 3, GIMP_FILL, GIMP_FILL, 4, 0);

      table_attach_label (table, "Spot Radius:", 1.0, 0.5, 0, 1, 3, 4);

      ifsOptD->radius_pair = value_pair_create(&ifsvals.radius,0,5,
					       TRUE,
					       VALUE_PAIR_DOUBLE);
      gimp_table_attach(table, ifsOptD->radius_pair->scale,
			1, 2, 3, 4, GIMP_FILL, GIMP_FILL, 4, 0);
      gimp_table_attach(table, ifsOptD->radius_pair->entry,
			2, 3, 3, 4, GIMP_FILL, GIMP_FILL, 4, 0);

      value_pair_update(ifsOptD->iterations_pair);
      value_pair_update(ifsOptD->subdivide_pair);
      value_pair_update(ifsOptD->memory_pair);
      value_pair_update(ifsOptD->radius_pair);

      gtk_window_present (GTK_WINDOW (ifsOptD->dialog));
    }
  else
    {
      if (!gtk_widget_get_visible (ifsOptD->dialog))
	gtk_window_present (GTK_WINDOW (ifsOptD->dialog));
    }
}

static void
ifs_compose(GDrawable *drawable)
{
  gint i,j;
  GDrawableType type = gimp_drawable_type(drawable->id);
  gchar buffer[128];

  gint width = drawable->width;
  gint height = drawable->height;
  gint num_bands,band_height,band_y,band_no;
  guchar *data;
  guchar *mask = NULL;
  guchar *nhits;
  guchar rc,gc,bc;

  num_bands = ceil((gdouble)(width*height*SQR(ifsvals.subdivide)*5)
		   / (1024 * ifsvals.max_memory));
  band_height = height / num_bands;
  if (band_height > height)
    band_height = height;

  mask = g_new(guchar,width*band_height*SQR(ifsvals.subdivide));
  data = g_new(guchar,width*band_height*SQR(ifsvals.subdivide)*3);
  nhits = g_new(guchar,width*band_height*SQR(ifsvals.subdivide));
  gimp_palette_get_background ( &rc, &gc, &bc );

  band_y = 0;
  for (band_no = 0; band_no < num_bands; band_no++)
    {
      guchar *ptr;
      guchar *maskptr;
      guchar *dest;
      guchar *destrow;
      guchar maskval;
      GPixelRgn dest_rgn;
      gint progress;
      gint max_progress;

      gpointer pr;

      sprintf(buffer,"Rendering IFS (%d/%d)...",band_no+1,num_bands);
      gimp_progress_init(buffer);

      /* render the band to a buffer */
      if (band_y + band_height > height)
	band_height = height - band_y;

      /* we don't need to clear data since we store nhits */
      memset(mask, 0, width*band_height*SQR(ifsvals.subdivide));
      memset(nhits, 0, width*band_height*SQR(ifsvals.subdivide));

      ifs_render(elements, ifsvals.num_elements, width, height, ifsvals.iterations,
		 &ifsvals, band_y, band_height, data, mask, nhits, FALSE);

      /* transfer the image to the drawable */

      sprintf(buffer,"Copying IFS to image (%d/%d)...",band_no+1,num_bands);
      gimp_progress_init(buffer);

      progress = 0;
      max_progress = band_height * width;

      gimp_pixel_rgn_init (&dest_rgn, drawable, 0, band_y,
			   width, band_height, TRUE, TRUE);

      for (pr = gimp_pixel_rgns_register (1, &dest_rgn); pr != NULL; pr = gimp_pixel_rgns_process (pr))
	{
	  destrow = dest_rgn.data;

	  for (j = dest_rgn.y; j < (dest_rgn.y + dest_rgn.h); j++)
	    {
	      dest = destrow;

	      for (i = dest_rgn.x; i < (dest_rgn.x + dest_rgn.w); i++)
		{
		  /* Accumulate a reduced pixel */

		  gint ii,jj;
		  gint rtot=0;
		  gint btot=0;
		  gint gtot=0;
		  gint mtot=0;
		  for (jj=0;jj<ifsvals.subdivide;jj++)
		    {
		      ptr = data + 3 *
			(((j-band_y)*ifsvals.subdivide+jj)*ifsvals.subdivide*width +
			 i*ifsvals.subdivide);

		      maskptr = mask +
			((j-band_y)*ifsvals.subdivide+jj)*ifsvals.subdivide*width +
			i*ifsvals.subdivide;
		      for (ii=0;ii<ifsvals.subdivide;ii++)
			{
			  maskval = *maskptr++;
			  mtot += maskval;
			  rtot += maskval* *ptr++;
			  gtot += maskval* *ptr++;
			  btot += maskval* *ptr++;
			}
		    }
		  if (mtot)
		    {
		      rtot /= mtot;
		      gtot /= mtot;
		      btot /= mtot;
		      mtot /= SQR(ifsvals.subdivide);
		    }
		  /* and store it */
		  switch (type)
		    {
 		    case GRAY_IMAGE:
		      *dest++ = (mtot*(rtot+btot+gtot)+
				 (255-mtot)*(rc+gc+bc))/(3*255);
		      break;
		    case GRAYA_IMAGE:
		      *dest++ = (rtot+btot+gtot)/3;
		      *dest++ = mtot;
		      break;
		    case RGB_IMAGE:
		      *dest++ = (mtot*rtot + (255-mtot)*rc)/255;
		      *dest++ = (mtot*gtot + (255-mtot)*gc)/255;
		      *dest++ = (mtot*btot + (255-mtot)*bc)/255;
		      break;
		    case RGBA_IMAGE:
		      *dest++ = rtot;
		      *dest++ = gtot;
		      *dest++ = btot;
		      *dest++ = mtot;
		      break;
		    case INDEXED_IMAGE:
		    case INDEXEDA_IMAGE:
		      g_error("Indexed images not supported by IfsCompose");
		      break;
		    }
		}
	      destrow += dest_rgn.rowstride;;
	    }
	  progress += dest_rgn.w * dest_rgn.h;
	  gimp_progress_update ((gdouble) progress / (gdouble) max_progress);
	}
      band_y += band_height;
    }

  g_free(mask);
  g_free(data);
  g_free(nhits);

  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->id, TRUE);
  gimp_drawable_update (drawable->id,0,0,width,height);
}

static void
update_values(void)
{
  ifsD->in_update = TRUE;

  ifsD->current_vals = elements[ifsD->current_element]->v;
  ifsD->current_vals.theta *= 180/M_PI;

  value_pair_update(ifsD->prob_pair);
  value_pair_update(ifsD->x_pair);
  value_pair_update(ifsD->y_pair);
  value_pair_update(ifsD->scale_pair);
  value_pair_update(ifsD->angle_pair);
  value_pair_update(ifsD->asym_pair);
  value_pair_update(ifsD->shear_pair);
  color_map_update(ifsD->red_cmap);
  color_map_update(ifsD->green_cmap);
  color_map_update(ifsD->blue_cmap);
  color_map_update(ifsD->black_cmap);
  color_map_update(ifsD->target_cmap);
  value_pair_update(ifsD->hue_scale_pair);
  value_pair_update(ifsD->value_scale_pair);
  if (elements[ifsD->current_element]->v.simple_color)
    gtk_check_button_set_active (GTK_CHECK_BUTTON (ifsD->simple_button),
				 TRUE);
  else
    gtk_check_button_set_active (GTK_CHECK_BUTTON (ifsD->full_button),
				 TRUE);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(ifsD->flip_check_button),
			      elements[ifsD->current_element]->v.flip);

  ifsD->in_update = FALSE;

  simple_color_set_sensitive();
}

static void
set_current_element(gint index)
{
  ifsD->current_element = index;

  gtk_frame_set_label(GTK_FRAME(ifsD->current_frame),elements[index]->name);

  update_values();
}

static void
design_area_draw (GtkDrawingArea *area,
		  cairo_t        *cr,
		  gint            width,
		  gint            height,
		  gpointer        data)
{
  gint i;
  gint cx,cy;
  GdkRGBA fg;
  PangoLayout *layout;

  gtk_widget_get_color (GTK_WIDGET (area), &fg);

  /* draw an indicator for the center */

  cx = ifsvals.center_x * width;
  cy = ifsvals.center_y * width;
  gdk_cairo_set_source_rgba (cr, &fg);
  cairo_set_line_width (cr, 1.0);
  cairo_move_to (cr, cx - 10, cy + 0.5);
  cairo_line_to (cr, cx + 10 + 1, cy + 0.5);
  cairo_move_to (cr, cx + 0.5, cy - 10);
  cairo_line_to (cr, cx + 0.5, cy + 10 + 1);
  cairo_stroke (cr);

  layout = gtk_widget_create_pango_layout (GTK_WIDGET (area), NULL);

  for (i=0;i<ifsvals.num_elements;i++)
    {
      aff_element_draw(elements[i], element_selected[i],
		       width, height, cr, &fg, &fg, layout);
    }

  g_object_unref (layout);
}

static void
design_area_resize (GtkDrawingArea *area,
		    gint            width,
		    gint            height,
		    gpointer        data)
{
  int i;

  ifsDesign->width = width;
  ifsDesign->height = height;

  for (i=0;i<ifsvals.num_elements;i++)
    aff_element_compute_trans(elements[i],width,height,
			      ifsvals.center_x, ifsvals.center_y);
  for (i=0;i<ifsvals.num_elements;i++)
    aff_element_compute_boundary(elements[i],width,height,
				 elements, ifsvals.num_elements);
}

static void
design_area_menu_pressed (GtkGestureClick *gesture,
			  gint             n_press,
			  gdouble          x,
			  gdouble          y,
			  gpointer         data)
{
  gtk_widget_grab_focus (ifsDesign->area);

  if (!(ifsDesign->button_state & GDK_BUTTON1_MASK))
    design_op_menu_popup (x, y);
}

static void
design_area_drag_begin (GtkGestureDrag *gesture,
			gdouble         x,
			gdouble         y,
			gpointer        data)
{
  gint i;
  gdouble width = ifsDesign->width;
  gint old_current;
  GdkModifierType state;

  gtk_widget_grab_focus(ifsDesign->area);

  if (ifsDesign->button_state & GDK_BUTTON1_MASK)
    return;

  state = gtk_event_controller_get_current_event_state
    (GTK_EVENT_CONTROLLER (gesture));

  ifsDesign->drag_start_x = x;
  ifsDesign->drag_start_y = y;

  old_current = ifsD->current_element;
  ifsD->current_element = -1;

  /* Find out where the button press was */
  for (i=0;i<ifsvals.num_elements;i++)
    {
      if ( ipolygon_contains(elements[i]->click_boundary,x,y) )
	{
	  set_current_element(i);
	  break;
	}
    }

  /* if the user started manipulating an object, set up a new
     position on the undo ring */
  if (ifsD->current_element >= 0)
    undo_begin();

  if (!(state & GDK_SHIFT_MASK)
      && ( (ifsD->current_element<0)
	   || !element_selected[ifsD->current_element] ))
    {
      for (i=0;i<ifsvals.num_elements;i++)
	element_selected[i] = 0;
    }

  if (ifsD->current_element >= 0)
    {
      ifsDesign->button_state |= GDK_BUTTON1_MASK;

      element_selected[ifsD->current_element] = TRUE;

      ifsDesign->num_selected = 0;
      ifsDesign->op_xcenter = 0.0;
      ifsDesign->op_ycenter = 0.0;
      for (i=0;i<ifsvals.num_elements;i++)
	{
	  if (element_selected[i])
	    {
	      ifsD->selected_orig[i] = *elements[i];
	      ifsDesign->op_xcenter += elements[i]->v.x;
	      ifsDesign->op_ycenter += elements[i]->v.y;
	      ifsDesign->num_selected++;
	      undo_update(i);
	    }
	}
      ifsDesign->op_xcenter /= ifsDesign->num_selected;
      ifsDesign->op_ycenter /= ifsDesign->num_selected;
      ifsDesign->op_x = x/width;
      ifsDesign->op_y = y/width;
      ifsDesign->op_center_x = ifsvals.center_x;
      ifsDesign->op_center_y = ifsvals.center_y;
    }
  else
    {
      ifsD->current_element = old_current;
      element_selected[old_current] = TRUE;
    }

  design_area_redraw();
}

static void
design_area_drag_update (GtkGestureDrag *gesture,
			 gdouble         offset_x,
			 gdouble         offset_y,
			 gpointer        data)
{
  design_area_motion (ifsDesign->drag_start_x + offset_x,
		      ifsDesign->drag_start_y + offset_y);
}

static void
design_area_drag_end (GtkGestureDrag *gesture,
		      gdouble         offset_x,
		      gdouble         offset_y,
		      gpointer        data)
{
  if (ifsDesign->button_state & GDK_BUTTON1_MASK)
    {
      ifsDesign->button_state &= ~GDK_BUTTON1_MASK;
      if (ifsD->auto_preview)
	ifs_compose_preview_callback(NULL, ifsD->preview);
    }
}

static void
design_area_motion (gdouble x,
		    gdouble y)
{
  gint i;
  gdouble xo;
  gdouble yo;
  gdouble xn;
  gdouble yn;
  gdouble width = ifsDesign->width;
  gdouble height = ifsDesign->height;

  Aff2 trans,t1,t2,t3;

  if (!(ifsDesign->button_state & GDK_BUTTON1_MASK)) return;

  xo = (ifsDesign->op_x - ifsDesign->op_xcenter);
  yo = (ifsDesign->op_y - ifsDesign->op_ycenter);
  xn = x/width - ifsDesign->op_xcenter;
  yn = y/width - ifsDesign->op_ycenter;

  switch (ifsDesign->op)
    {
    case OP_ROTATE:
      {
	aff2_translate(&t1,-ifsDesign->op_xcenter*width,
		       -ifsDesign->op_ycenter*width);
	aff2_scale(&t2,
		   sqrt((SQR(xn)+SQR(yn))/(SQR(xo)+SQR(yo))),
		   0);
	aff2_compose(&t3, &t2, &t1);
	aff2_rotate(&t1, - atan2(yn,xn) + atan2(yo,xo));
	aff2_compose(&t2, &t1, &t3);
	aff2_translate(&t3,ifsDesign->op_xcenter*width,
		       ifsDesign->op_ycenter*width);
	aff2_compose(&trans, &t3, &t2);
	break;
      }
    case OP_STRETCH:
      {
	aff2_translate(&t1,-ifsDesign->op_xcenter*width,
		       -ifsDesign->op_ycenter*width);
	aff2_compute_stretch(&t2, xo, yo, xn, yn);
	aff2_compose(&t3, &t2, &t1);
	aff2_translate(&t1,ifsDesign->op_xcenter*width,
		       ifsDesign->op_ycenter*width);
	aff2_compose(&trans, &t1, &t3);
	break;
      }
    case OP_TRANSLATE:
    default:
      {
	aff2_translate(&trans,(xn-xo)*width,(yn-yo)*width);
	break;
      }
    }

  for (i=0;i<ifsvals.num_elements;i++)
    if (element_selected[i])
      {
	if (ifsDesign->num_selected == ifsvals.num_elements)
	  {
	    gdouble cx,cy;
	    aff2_invert(&t1, &trans);
	    aff2_compose(&t2, &trans, &ifsD->selected_orig[i].trans);
	    aff2_compose(&elements[i]->trans, &t2, &t1);

	    cx = ifsDesign->op_center_x * width;
	    cy = ifsDesign->op_center_y * width;
	    aff2_apply(&trans,cx,cy,&cx,&cy);
	    ifsvals.center_x = cx / width;
	    ifsvals.center_y = cy / width;
	  }
	else
	  {
	    aff2_compose(&elements[i]->trans, &trans,
			 &ifsD->selected_orig[i].trans);
	  }
	aff_element_decompose_trans(elements[i],&elements[i]->trans,
				    width,height, ifsvals.center_x,
				    ifsvals.center_y);
	aff_element_compute_trans(elements[i],width,height,
			      ifsvals.center_x, ifsvals.center_y);
      }

  update_values();
  design_area_redraw();
}

static void
design_area_redraw(void)
{
  gint i;
  gdouble width = ifsDesign->width;
  gdouble height = ifsDesign->height;

  for (i=0;i<ifsvals.num_elements;i++)
    {
      aff_element_compute_boundary(elements[i],width,height,
				   elements,ifsvals.num_elements);
    }
  gtk_widget_queue_draw(ifsDesign->area);
}


/* Undo ring functions */
static void
undo_begin(void)
{
  gint i,j;
  gint to_delete;
  gint new_index;

  if (undo_cur == UNDO_LEVELS-1)
    {
      to_delete = 1;
      undo_start = (undo_start + 1)%UNDO_LEVELS;
    }
  else
    {
      undo_cur++;
      to_delete = undo_num - undo_cur;
    }

  undo_num = undo_num - to_delete + 1;
  new_index = (undo_start+undo_cur)%UNDO_LEVELS;

  /* remove any redo elements or the oldest element if necessary */
  for (j=new_index;to_delete>0;j=(j+1)%UNDO_LEVELS,to_delete--)
    {
      for (i=0;i<undo_ring[j].ifsvals.num_elements;i++)
	if (undo_ring[j].elements[i])
	  aff_element_free(undo_ring[j].elements[i]);
      g_free(undo_ring[j].elements);
      g_free(undo_ring[j].element_selected);
    }

  undo_ring[new_index].ifsvals = ifsvals;
  undo_ring[new_index].elements = g_new(AffElement *,ifsvals.num_elements);
  undo_ring[new_index].element_selected = g_new(gint,ifsvals.num_elements);
  undo_ring[new_index].current_element = ifsD->current_element;

  for (i=0;i<ifsvals.num_elements;i++)
    {
      undo_ring[new_index].elements[i] = NULL;
      undo_ring[new_index].element_selected[i] = element_selected[i];
    }
}

static void
undo_update(gint el)
{
  AffElement *elem;
  /* initialize */

  /* the first saved state of an element in this undo step is kept */
  if (undo_ring[(undo_start+undo_cur)%UNDO_LEVELS].elements[el])
    return;

  undo_ring[(undo_start+undo_cur)%UNDO_LEVELS].elements[el]
    = elem = g_new(AffElement,1);

  *elem = *elements[el];
  elem->draw_boundary = NULL;
  elem->click_boundary = NULL;
}

static void
undo_exchange(gint el)
{
  gint i;
  AffElement **telements;
  gint *tselected;
  IfsComposeVals tifsvals;
  gint tcurrent;
  gdouble width = ifsDesign->width;
  gdouble height = ifsDesign->height;

  /* swap the arrays and values*/
  telements = elements;
  elements = undo_ring[el].elements;
  undo_ring[el].elements = telements;

  tifsvals = ifsvals;
  ifsvals = undo_ring[el].ifsvals;
  undo_ring[el].ifsvals = tifsvals;

  tselected = element_selected;
  element_selected = undo_ring[el].element_selected;
  undo_ring[el].element_selected = tselected;

  tcurrent = ifsD->current_element;
  ifsD->current_element = undo_ring[el].current_element;
  undo_ring[el].current_element = tcurrent;

  /* now swap back any unchanged elements */
  for (i=0;i<ifsvals.num_elements;i++)
    if (!elements[i])
      {
	elements[i] = undo_ring[el].elements[i];
	undo_ring[el].elements[i] = 0;
      }
    else
      aff_element_compute_trans(elements[i],width,height,
				ifsvals.center_x,ifsvals.center_y);

  set_current_element(ifsD->current_element);

  design_area_redraw();

  if (ifsD->auto_preview)
    ifs_compose_preview_callback(NULL, ifsD->preview);
}

static void
undo(void)
{
  if (undo_cur >= 0)
    {
      undo_exchange((undo_start+undo_cur)%UNDO_LEVELS);
      undo_cur--;
    }
}

static void
redo(void)
{
  if (undo_cur != undo_num - 1)
    {
      undo_cur++;
      undo_exchange((undo_start+undo_cur)%UNDO_LEVELS);
    }
}

static void
design_area_select_all_callback(GtkWidget *w, gpointer data)
{
  gint i;

  for (i=0;i<ifsvals.num_elements;i++)
    element_selected[i] = TRUE;

  design_area_redraw();
}

/*  Interface functions  */

static void
val_changed_update (void)
{
  gdouble width = ifsDesign->width;
  gdouble height = ifsDesign->height;
  AffElement *cur = elements[ifsD->current_element];

  if (ifsD->in_update)
    return;

  undo_begin();
  undo_update(ifsD->current_element);

  cur->v = ifsD->current_vals;
  cur->v.theta *= M_PI/180.0;
  aff_element_compute_trans(cur,width,height,
			      ifsvals.center_x, ifsvals.center_y);
  aff_element_compute_color_trans(cur);

  design_area_redraw();

  if (ifsD->auto_preview)
    ifs_compose_preview_callback(NULL, ifsD->preview);
}

/* Pseudo-widget representing a color mapping */

#define COLOR_SAMPLE_SIZE 30

static void
color_map_set_preview_color(GtkWidget *preview, IfsColor *color)
{
  gint i;
  guchar buf[3*COLOR_SAMPLE_SIZE];

  for (i=0;i<COLOR_SAMPLE_SIZE;i++)
    {
      buf[3*i] = (guint)(255.999*color->vals[0]);
      buf[3*i+1] = (guint)(255.999*color->vals[1]);
      buf[3*i+2] = (guint)(255.999*color->vals[2]);
    }
  for (i=0;i<COLOR_SAMPLE_SIZE;i++)
    gimp_preview_draw_row(GIMP_PREVIEW(preview),buf,0,i,COLOR_SAMPLE_SIZE);
}

static ColorMap *
color_map_create(gchar *name, IfsColor *orig_color, IfsColor *data,
		 gint fixed_point)
{
  GtkWidget *frame;
  GtkWidget *label;
  GtkWidget *button;

  ColorMap *color_map = g_new(ColorMap,1);
  color_map->name = name;
  color_map->color = data;
  color_map->fixed_point = fixed_point;

  color_map->in_change_callback = FALSE;

  color_map->hbox = gimp_hbox_new(FALSE,2);

  frame = gtk_frame_new(NULL);
  gtk_widget_set_valign (frame, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(color_map->hbox),frame);

  color_map->orig_preview = gimp_preview_new(GIMP_PREVIEW_COLOR);
  gimp_preview_size(GIMP_PREVIEW(color_map->orig_preview),
		    COLOR_SAMPLE_SIZE,COLOR_SAMPLE_SIZE);
  gtk_frame_set_child (GTK_FRAME(frame),color_map->orig_preview);

  if (fixed_point)
    color_map_set_preview_color(color_map->orig_preview,data);
  else
    color_map_set_preview_color(color_map->orig_preview,orig_color);

  label = gtk_label_new("=>");
  gtk_box_append(GTK_BOX(color_map->hbox),label);

  button = gtk_button_new();
  gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(color_map->hbox),button);

  color_map->preview = gimp_preview_new(GIMP_PREVIEW_COLOR);
  gimp_preview_size(GIMP_PREVIEW(color_map->preview),
		    COLOR_SAMPLE_SIZE,COLOR_SAMPLE_SIZE);
  gtk_button_set_child (GTK_BUTTON(button),color_map->preview);

  color_map_set_preview_color(color_map->preview,data);

  g_signal_connect(button,"clicked",
		   G_CALLBACK (color_map_clicked_callback),
		   color_map);

  return color_map;
}

/* The color is chosen in a modal GtkColorDialog; unlike GTK 1's color
   selection dialog it has no live updates, so the new color is applied
   once the dialog is confirmed. */
static void
color_map_clicked_callback(GtkWidget *widget,
			   ColorMap *color_map)
{
  guchar rgb[3];
  gint i;

  for (i = 0; i < 3; i++)
    rgb[i] = CLAMP ((gint) (255.999 * color_map->color->vals[i]), 0, 255);

  gimp_color_dialog_run (GTK_WINDOW (gtk_widget_get_root (widget)),
			 color_map->name, rgb,
			 color_map_color_chosen_cb, color_map);
}

static void
color_map_color_chosen_cb(const guchar *rgb,
			  gpointer      data)
{
  ColorMap *color_map = data;
  gint i;

  if (!ifsD)
    return;

  undo_begin();
  undo_update(ifsD->current_element);

  color_map->in_change_callback = TRUE;

  for (i = 0; i < 3; i++)
    color_map->color->vals[i] = rgb[i] / 255.0;

  elements[ifsD->current_element]->v = ifsD->current_vals;
  elements[ifsD->current_element]->v.theta *= M_PI/180.0;
  aff_element_compute_color_trans(elements[ifsD->current_element]);

  update_values();

  if (ifsD->auto_preview)
    ifs_compose_preview_callback(NULL,ifsD->preview);

  color_map->in_change_callback = FALSE;
}

static void
color_map_update(ColorMap *color_map)
{
  color_map_set_preview_color(color_map->preview,color_map->color);
  if (color_map->fixed_point)
    color_map_set_preview_color(color_map->orig_preview,color_map->color);
}

static void
simple_color_set_sensitive(void)
{
  gint sc = elements[ifsD->current_element]->v.simple_color;

  gtk_widget_set_sensitive(ifsD->target_cmap->hbox,sc);
  gtk_widget_set_sensitive(ifsD->hue_scale_pair->scale,sc);
  gtk_widget_set_sensitive(ifsD->hue_scale_pair->entry,sc);
  gtk_widget_set_sensitive(ifsD->value_scale_pair->scale,sc);
  gtk_widget_set_sensitive(ifsD->value_scale_pair->entry,sc);

  gtk_widget_set_sensitive(ifsD->red_cmap->hbox,!sc);
  gtk_widget_set_sensitive(ifsD->green_cmap->hbox,!sc);
  gtk_widget_set_sensitive(ifsD->blue_cmap->hbox,!sc);
  gtk_widget_set_sensitive(ifsD->black_cmap->hbox,!sc);
}

static void
simple_color_toggled(GtkWidget *widget,gpointer data)
{
  AffElement *cur = elements[ifsD->current_element];
  cur->v.simple_color = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
  ifsD->current_vals.simple_color = cur->v.simple_color;
  if (cur->v.simple_color)
    {
      aff_element_compute_color_trans(cur);
      val_changed_update();
    }
  simple_color_set_sensitive();
}

/* Generic mechanism for scale/entry combination (possibly without
   scale) */

static ValuePair *
value_pair_create (gpointer data, gdouble lower, gdouble upper,
		   gboolean create_scale, ValuePairType type)
{

  ValuePair *value_pair = g_new(ValuePair,1);
  value_pair->data.d = data;
  value_pair->type = type;
  value_pair->update_timeout_id = 0;

  value_pair->adjustment = gtk_adjustment_new (1.0, lower, upper,
					  (upper-lower)/100, (upper-lower)/10,
					  0.0);
  /* We need to sink the adjustment, since we may not create a scale for
   * it, so nobody will assume the initial refcount
   */
  g_object_ref_sink (value_pair->adjustment);
  g_signal_connect (value_pair->adjustment, "value-changed",
		    G_CALLBACK (value_pair_scale_callback),
		    value_pair);

  if (create_scale)
    {
      value_pair->scale = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL,
					 value_pair->adjustment);
      gtk_widget_set_hexpand (value_pair->scale, TRUE);
      gtk_widget_set_size_request (value_pair->scale, SCALE_WIDTH / 2, -1);

      if (type == VALUE_PAIR_INT)
	  gtk_scale_set_digits (GTK_SCALE (value_pair->scale), 0);
      else
	  gtk_scale_set_digits (GTK_SCALE (value_pair->scale), 2);

      gtk_scale_set_draw_value (GTK_SCALE (value_pair->scale), FALSE);
    }
  else
    value_pair->scale = NULL;

  /* We destroy the value pair when the entry is destroyed, so
   * we don't need to hold a refcount on the entry
   */

  value_pair->entry = gtk_entry_new ();
  gtk_editable_set_width_chars (GTK_EDITABLE (value_pair->entry), 6);
  gtk_widget_set_size_request (value_pair->entry, ENTRY_WIDTH, -1);
  value_pair->entry_handler_id =
    g_signal_connect (value_pair->entry, "changed",
		      G_CALLBACK (value_pair_entry_callback), value_pair);
  g_signal_connect (value_pair->entry, "destroy",
		    G_CALLBACK (value_pair_destroy_callback), value_pair);

  return value_pair;
}

static void
value_pair_update(ValuePair *value_pair)
{
  gchar buffer[32];

  if (value_pair->type == VALUE_PAIR_INT)
    {
      gtk_adjustment_set_value (value_pair->adjustment, *value_pair->data.i);
      sprintf (buffer, "%d", *value_pair->data.i);
    }
  else
    {
      gtk_adjustment_set_value (value_pair->adjustment, *value_pair->data.d);
      sprintf (buffer, "%0.2f", *value_pair->data.d);
    }

  g_signal_handler_block(value_pair->entry, value_pair->entry_handler_id);
  gtk_editable_set_text (GTK_EDITABLE (value_pair->entry), buffer);
  g_signal_handler_unblock(value_pair->entry, value_pair->entry_handler_id);
}

/* GTK 1 applied a moved scale when the button was released
   (GTK_UPDATE_DELAYED); here the change is applied once the scale has
   been still for a moment. */
static gboolean
value_pair_delayed_update (gpointer data)
{
  ValuePair *value_pair = data;

  value_pair->update_timeout_id = 0;
  val_changed_update();

  return G_SOURCE_REMOVE;
}

static void
value_pair_scale_callback (GtkAdjustment *adjustment,
			   ValuePair *value_pair)
{
  gchar buffer[32];
  gint changed = FALSE;
  gdouble value = gtk_adjustment_get_value (adjustment);

  if (value_pair->type == VALUE_PAIR_DOUBLE)
    {
      if (*value_pair->data.d != value)
	{
	  changed = TRUE;
	  *value_pair->data.d = value;
	  sprintf (buffer, "%0.2f", value);
	}
    }
  else
    {
      if (*value_pair->data.i != (gint)value)
	{
	  changed = TRUE;
	  *value_pair->data.i = value;
	  sprintf (buffer, "%d", (gint)value);
	}
    }
  if (changed)
    {
      g_signal_handler_block(value_pair->entry,
			     value_pair->entry_handler_id);
      gtk_editable_set_text (GTK_EDITABLE (value_pair->entry), buffer);
      g_signal_handler_unblock(value_pair->entry,
			       value_pair->entry_handler_id);

      /* only a scale moves the adjustment by itself */
      if (value_pair->scale)
	{
	  if (value_pair->update_timeout_id)
	    g_source_remove (value_pair->update_timeout_id);
	  value_pair->update_timeout_id =
	    g_timeout_add (300, value_pair_delayed_update, value_pair);
	}
    }
}

static void
value_pair_entry_callback (GtkWidget   *widget,
			   ValuePair   *value_pair)
{
  GtkAdjustment *adjustment = value_pair->adjustment;
  gdouble new_value;
  gdouble old_value;

  if (value_pair->type == VALUE_PAIR_INT)
    {
      old_value = *value_pair->data.i;
      new_value = atoi(gtk_editable_get_text(GTK_EDITABLE(widget)));
    }
  else
    {
      old_value = *value_pair->data.d;
      new_value = atof(gtk_editable_get_text(GTK_EDITABLE(widget)));
    }

  if (floor(0.5+old_value*10000) != floor(0.5+new_value*10000))
    {
      if ((new_value >= gtk_adjustment_get_lower (adjustment)) &&
	  (new_value <= gtk_adjustment_get_upper (adjustment)))
	{
	  if (value_pair->type == VALUE_PAIR_INT)
	    *value_pair->data.i = new_value;
	  else
	    *value_pair->data.d = new_value;
	  gtk_adjustment_set_value (adjustment, new_value);

	  val_changed_update();
	}
    }
}

static void
value_pair_destroy_callback (GtkWidget   *widget,
			     ValuePair   *value_pair)
{
  if (value_pair->update_timeout_id)
    {
      g_source_remove (value_pair->update_timeout_id);
      value_pair->update_timeout_id = 0;
    }
  g_object_unref (value_pair->adjustment);
}

static void
design_op_callback (GtkWidget *widget, gpointer data)
{
  /* the buttons are grouped: only the one that became active matters */
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
    ifsDesign->op = (DesignOp) GPOINTER_TO_INT (data);
}

static void
design_op_update_callback (GtkWidget *widget, gpointer data)
{
  DesignOp op = (DesignOp) GPOINTER_TO_INT (data);

  if (op != ifsDesign->op)
    {
      switch (op)
	{
	case OP_TRANSLATE:
	  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ifsD->move_button),
				       TRUE);
	  break;
	case OP_ROTATE:
	  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ifsD->rotate_button),
				       TRUE);
	  break;
	case OP_STRETCH:
	  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ifsD->stretch_button),
				       TRUE);
	  break;
	}
    }
}

static void
recompute_center_cb(GtkWidget *w, gpointer data)
{
  recompute_center(TRUE);
}

static void
recompute_center(int save_undo)
{
  int i;
  gdouble x,y;
  gdouble center_x = 0.0;
  gdouble center_y = 0.0;

  gdouble width = ifsDesign->width;
  gdouble height = ifsDesign->height;

  if (save_undo)
    undo_begin();

  for (i=0;i<ifsvals.num_elements;i++)
    {
      if (save_undo)
	undo_update(i);
      aff_element_compute_trans(elements[i],1,ifsvals.aspect_ratio,
				ifsvals.center_x, ifsvals.center_y);
      aff2_fixed_point(&elements[i]->trans,&x,&y);
      center_x += x;
      center_y += y;
    }

  ifsvals.center_x = center_x/ifsvals.num_elements;
  ifsvals.center_y = center_y/ifsvals.num_elements;

  for (i=0;i<ifsvals.num_elements;i++)
    {
	aff_element_decompose_trans(elements[i],&elements[i]->trans,
				    1,ifsvals.aspect_ratio,
				    ifsvals.center_x, ifsvals.center_y);
    }

  if (width > 1 && height > 1)
    {
      for (i=0;i<ifsvals.num_elements;i++)
	aff_element_compute_trans(elements[i],width,height,
				  ifsvals.center_x, ifsvals.center_y);
      design_area_redraw();
      update_values();
    }
}

static void
auto_preview_callback (GtkWidget *widget,
		       gpointer data)
{
  if (ifsD->auto_preview)
    {
      ifsD->auto_preview = 0;
    }
  else
    {
      ifsD->auto_preview = 1;
      ifs_compose_preview_callback(NULL, ifsD->preview);
    }
}

static void
flip_check_button_callback (GtkWidget *widget,
		      gpointer data)
{
  ifsD->current_vals.flip = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
  val_changed_update();
}

static void
ifs_options_close_callback (void)
{
  if (ifsOptD)
    gtk_widget_set_visible(ifsOptD->dialog, FALSE);
}

static void
ifs_compose_set_defaults (void)
{
  gint i;
  IfsColor color;
  guchar rc,bc,gc;

  gimp_palette_get_foreground (&rc,&gc,&bc);

  color.vals[0] = (gdouble)rc/255;
  color.vals[1] = (gdouble)gc/255;
  color.vals[2] = (gdouble)bc/255;

  ifsvals.aspect_ratio = (gdouble)ifsD->drawable_height/ifsD->drawable_width;

  for (i=0;i<ifsvals.num_elements;i++)
    aff_element_free(elements[i]);

  ifsvals.num_elements = 3;
  elements = g_realloc(elements, ifsvals.num_elements*sizeof(AffElement *));
  element_selected = g_realloc(element_selected,
			       ifsvals.num_elements*sizeof(gint));

  elements[0] = aff_element_new(0.3,0.37*ifsvals.aspect_ratio,color,
				element_count++);
  element_selected[0] = FALSE;
  elements[1] = aff_element_new(0.7,0.37*ifsvals.aspect_ratio,color,
				element_count++);
  element_selected[1] = FALSE;
  elements[2] = aff_element_new(0.5,0.7*ifsvals.aspect_ratio,color,
				element_count++);
  element_selected[2] = FALSE;

  ifsvals.center_x = 0.5;
  ifsvals.center_y = 0.5*ifsvals.aspect_ratio;
  ifsvals.iterations = ifsD->drawable_height*ifsD->drawable_width;

  ifsvals.subdivide = 3;
  ifsvals.max_memory = 4096;

  if (ifsOptD)
    {
      value_pair_update(ifsOptD->iterations_pair);
      value_pair_update(ifsOptD->subdivide_pair);
      value_pair_update(ifsOptD->radius_pair);
      value_pair_update(ifsOptD->memory_pair);
    }

  ifsvals.radius = 0.7;

  set_current_element(0);
  element_selected[0] = TRUE;
  recompute_center(FALSE);

  if (ifsD->selected_orig)
    g_free(ifsD->selected_orig);

  ifsD->selected_orig = g_new(AffElement,ifsvals.num_elements);
}

static void
ifs_compose_defaults_callback (GtkWidget *widget,
			       gpointer data)
{
  gint i;
  gdouble width = ifsDesign->width;
  gdouble height = ifsDesign->height;

  undo_begin();
  for (i=0;i<ifsvals.num_elements;i++)
    undo_update(i);

  ifs_compose_set_defaults();

  if (ifsD->auto_preview)
    ifs_compose_preview_callback(NULL, ifsD->preview);

  for (i=0;i<ifsvals.num_elements;i++)
    aff_element_compute_trans(elements[i],width,height,
			      ifsvals.center_x, ifsvals.center_y);

  design_area_redraw();
}

static void
ifs_compose_new_callback (GtkWidget *widget,
			  gpointer   data)
{
  IfsColor color;
  guchar rc,bc,gc;
  gint i;
  gdouble width = ifsDesign->width;
  gdouble height = ifsDesign->height;
  AffElement *elem;

  undo_begin();

  gimp_palette_get_foreground (&rc,&gc,&bc);

  color.vals[0] = (gdouble)rc/255;
  color.vals[1] = (gdouble)gc/255;
  color.vals[2] = (gdouble)bc/255;

  elem = aff_element_new(0.5, 0.5*height/width,color,
			 element_count++);

  ifsvals.num_elements++;
  elements = g_realloc(elements, ifsvals.num_elements*sizeof(AffElement *));
  element_selected = g_realloc(element_selected,
			       ifsvals.num_elements*sizeof(gint));

  for (i=0;i<ifsvals.num_elements-1;i++)
    element_selected[i] = FALSE;
  element_selected[ifsvals.num_elements-1] = TRUE;

  elements[ifsvals.num_elements-1] = elem;
  set_current_element(ifsvals.num_elements-1);

  ifsD->selected_orig = g_realloc(ifsD->selected_orig,
				  ifsvals.num_elements*sizeof(AffElement));
  aff_element_compute_trans(elem,width,height,
			      ifsvals.center_x, ifsvals.center_y);

  design_area_redraw();

  if (ifsD->auto_preview)
    ifs_compose_preview_callback(NULL, ifsD->preview);
}

static void
ifs_compose_delete_callback (GtkWidget *widget,
			     gpointer   data)
{
  gint i;
  gint new_current;
  if (ifsvals.num_elements <= 2)
    return;

  undo_begin();
  undo_update(ifsD->current_element);

  aff_element_free(elements[ifsD->current_element]);

  if (ifsD->current_element < ifsvals.num_elements-1)
    {
      undo_update(ifsvals.num_elements-1);
      elements[ifsD->current_element] = elements[ifsvals.num_elements-1];
      new_current = ifsD->current_element;
    }
  else
    new_current = ifsvals.num_elements-2;

  ifsvals.num_elements--;

  for (i=0;i<ifsvals.num_elements;i++)
    if (element_selected[i])
      {
	new_current = i;
	break;
      }

  element_selected[new_current] = TRUE;
  set_current_element(new_current);

  design_area_redraw();

  if (ifsD->auto_preview)
    ifs_compose_preview_callback(NULL, ifsD->preview);
}

static void
ifs_compose_close_callback (GtkWidget *widget,
			    GtkWidget **destroyed_widget)
{
  *destroyed_widget = NULL;
  gimp_main_loop_quit ();
}

static gboolean
preview_idle_render (gpointer data)
{
  gint i;
  gint width = gimp_preview_get_width (GIMP_PREVIEW (ifsD->preview));
  gint height = gimp_preview_get_height (GIMP_PREVIEW (ifsD->preview));

  gint iterations = PREVIEW_RENDER_CHUNK;
  if (iterations > ifsD->preview_iterations)
    iterations = ifsD->preview_iterations;

  for (i=0;i<ifsvals.num_elements;i++)
    aff_element_compute_trans(elements[i], width, height,
			      ifsvals.center_x, ifsvals.center_y);

  ifs_render(elements,ifsvals.num_elements,width,height,
	     iterations,&ifsvals,0,height,
	     ifsD->preview_data,NULL,NULL,TRUE);

  for (i=0;i<ifsvals.num_elements;i++)
    aff_element_compute_trans(elements[i],
			      ifsDesign->width,
			      ifsDesign->height,
			      ifsvals.center_x, ifsvals.center_y);

  ifsD->preview_iterations -= iterations;

  for (i = 0; i < height; i++)
    gimp_preview_draw_row (GIMP_PREVIEW (ifsD->preview),
			   ifsD->preview_data + i * width * 3,
			   0, i, width);

  if (ifsD->preview_iterations != 0)
    return G_SOURCE_CONTINUE;

  ifsD->preview_idle_id = 0;
  return G_SOURCE_REMOVE;
}

static void
ifs_compose_preview_callback (GtkWidget *widget,
			      GtkWidget *preview)
{
  /* Expansion isn't really supported for previews */
  gint i;
  gint width = gimp_preview_get_width (GIMP_PREVIEW (ifsD->preview));
  gint height = gimp_preview_get_height (GIMP_PREVIEW (ifsD->preview));
  guchar rc,gc,bc;
  guchar *ptr;

  if (!ifsD->preview_data)
    ifsD->preview_data = g_new(guchar,3*width*height);

  gimp_palette_get_background ( &rc, &gc, &bc );

  ptr = ifsD->preview_data;
  for (i=0;i<width*height;i++)
    {
      *ptr++ = rc;
      *ptr++ = gc;
      *ptr++ = bc;
    }

  if (ifsD->preview_idle_id == 0)
    ifsD->preview_idle_id = g_idle_add (preview_idle_render, NULL);

  ifsD->preview_iterations = ifsvals.iterations*((gdouble)width*height/
				 (ifsD->drawable_width*ifsD->drawable_height));
}

static void
ifs_compose_ok_callback (GtkWidget *widget,
			 GtkWidget *window)
{
  ifscint.run = TRUE;

  gtk_window_destroy (GTK_WINDOW (window));
}
