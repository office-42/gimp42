/*
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This is a plug-in for the GIMP.
 *
 * Generates images containing vector type drawings.
 *
 * Copyright (C) 1997 Andy Thomas  alt@picnic.demon.co.uk
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
 * Some of this code was taken from the Whirl plug-in
 * which was copyrighted by Federico Mena Quintero (as below).
 * 
 * Whirl plug-in --- distort an image into a whirlpool
 * Copyright (C) 1997 Federico Mena Quintero           
 *
 */

/* Change log:-
 * 0.9 First public release. 
 * 0.95 Second release.
 * 
 * 0.96 Added patch from  Rob Saunders that introduces a isometric type grid
 *      Removed use of gtk_idle* stuff on position update. Not required.
 *
 * 1.0  Fixed to work with the new gtk+-0.99.4 (tooltips stuff has changed).  
 * 
 * 1.1  Fixed crashes when objects not fully defined
 * 
 * 1.2  More bug fixes and prevent gtk warning when creating new figs
 * 
 * 1.3  Portability fixes and fixed bug reports 257 and 258 from and 81 & 101 & 133
 *      http://www.wilberworks.com/bugs.cgi
 */

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"
#include "pix_data.h"


/* If you have NOT applied the patch to the GIMP (See readme) remove the
 * following #define
 */

#define HAVE_PATCHED 1 

#ifdef HAVE_PATCHED
#define GFIG_LCC 1
#else
#define GFIG_LCC 2
#endif /* HAVE_PATCHED */


/***** Magic numbers *****/

#define PREVIEW_SIZE 650
#define SCALE_WIDTH  120
#define ENTRY_WIDTH  28

/* Even more stuff from Quartics plugins */
#define CHECK_SIZE  8
#define CHECK_DARK  ((int) (1.0 / 3.0 * 255))
#define CHECK_LIGHT ((int) (2.0 / 3.0 * 255))

#define MIN_GRID 10
#define MAX_GRID 50
#define MAX_UNDO 10
#define MIN_UNDO 1
#define MAX_LOAD_LINE 256
#define SMALL_PREVIEW_SZ 48
#define BRUSH_PREVIEW_SZ 32
#define GFIG_HEADER "GFIG Version 0.1\n"


#ifndef TRUE
#define TRUE 1
#endif /* TRUE */

#ifndef FALSE
#define FALSE 0
#endif /* FALSE */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef W_OK
#define W_OK 2
#endif


GDrawable *gfig_select_drawable;
GtkWidget *gfig_preview;
GtkWidget *pic_preview;
GtkWidget *gfig_gtk_list;
GtkWidget *gfig_top_level;           /* The main dialog */
GtkWidget *gfig_hruler;              /* Rulers beside the preview */
GtkWidget *gfig_vruler;
cairo_surface_t *gfig_back_surface;  /* Back buffer of the preview image */
static cairo_t *gfig_cr = NULL;      /* Set while a draw function runs */
static gint gfig_ruler_x = -1;       /* Pointer position shown on rulers */
static gint gfig_ruler_y = -1;
gint32 gfig_image;
gint32 gfig_drawable;
GtkWidget *brush_page_pw;


static gint   tile_width, tile_height;
static gint   img_width, img_height,img_bpp,real_img_bpp;

static void      query  (void);
static void      run    (gchar    *name,
			 gint      nparams,
			 GParam   *param,
			 gint     *nreturn_vals,
			 GParam  **return_vals);
/* GTK 4 has no GdkPoint */
typedef struct
{
  gint x;
  gint y;
} GfigPoint;

static gint      gfig_dialog (void);
static void      gfig_clear_selection(gint32 ID);
static void      gfig_close_callback (GtkWidget *widget,gpointer   data);
static void      gfig_ok_callback (GtkWidget *widget,gpointer   data);
static void      gfig_paint_callback (GtkWidget *widget,gpointer   data);
static void      gfig_clear_callback (GtkWidget *widget,gpointer   data);
static void      gfig_undo_callback (GtkWidget *widget,gpointer   data);
static void      gfig_preview_draw (GtkDrawingArea *area, cairo_t *cr,
				    gint width, gint height, gpointer data);
static void      pic_preview_draw (GtkDrawingArea *area, cairo_t *cr,
				   gint width, gint height, gpointer data);
static void      gfig_preview_drag_begin (GtkGestureDrag *gesture,
					  gdouble x, gdouble y, gpointer data);
static void      gfig_preview_drag_end (GtkGestureDrag *gesture,
					gdouble x, gdouble y, gpointer data);
static void      gfig_preview_motion (GtkEventControllerMotion *controller,
				      gdouble x, gdouble y, gpointer data);
static gboolean  gfig_preview_key_press (GtkEventControllerKey *controller,
					 guint keyval, guint keycode,
					 GdkModifierType state, gpointer data);
static void      gfig_preview_key_release (GtkEventControllerKey *controller,
					   guint keyval, guint keycode,
					   GdkModifierType state, gpointer data);
static void      gfig_entry_update(GtkWidget *widget, gint *value);
/*static void      gfig_entry_update_fp(GtkWidget *widget, gdouble *value);*/
static void      gfig_scale_update(GtkAdjustment *adjustment, gint *value);
static void      gfig_scale_update_fp(GtkAdjustment *adjustment, gdouble *value);
static void      gfig_scale_update_scale(GtkAdjustment *adjustment, gdouble *value);
static void      gfig_toggle_update(GtkWidget *widget,gpointer   data);
static void      gfig_scale2img_update(GtkWidget *widget,gpointer   data);
static gint      gfig_scale_x(gint x);
static gint      gfig_scale_y(gint y);
static gint      gfig_invscale_x(gint x);
static gint      gfig_invscale_y(gint y);
static void      gfig_set_grid_source(cairo_t *cr, gint gctype);
static void      gfig_cancel_callback(GtkWidget *widget,gpointer   data);
static void      gfig_pos_enable(GtkWidget *widget, gpointer data);


static void      list_button_press(GtkGestureClick *gesture, gint n_press,
				   gdouble x, gdouble y, gpointer data);
static void      save_button_press(GtkWidget *widget,gpointer   data);
static void      load_button_press(GtkWidget *widget,gpointer   data);
static void      new_button_press(GtkWidget *widget,gpointer   data);
static void      gfig_delete_gfig_callback(GtkWidget *widget,gpointer   data);
static void      delete_button_press_ok(GtkWidget *widget,gpointer   data);
static void      delete_button_press_cancel(GtkWidget *widget,gpointer   data);
static void      edit_button_press(GtkWidget *widget,gpointer data);
static void      merge_button_press(GtkWidget *widget,gpointer   data);
static void      rescan_button_press(GtkWidget *widget,gpointer   data);

static void      do_gfig(void);
static void      dialog_update_preview(void);
static void      draw_grid_clear(GtkWidget *widget,gpointer   data);
static void      toggle_show_image(GtkWidget *widget,gpointer   data);
static void      toggle_tooltips(GtkWidget *widget,gpointer   data);
static void      toggle_obj_type(GtkWidget *widget,gpointer   data);
static void      draw_grid(GtkWidget *widget,gpointer   data);
static void      find_grid_pos(GfigPoint *p,GfigPoint *gp, guint state);
static gint      calculate_point_to_line_distance(GfigPoint *p, GfigPoint *A, GfigPoint *B, GfigPoint *I);

/* Drawing on the preview (and on the small preview when drawing_pic).
 * These draw with the cairo context of the draw function that is
 * running; outside a draw function they only ask for a redraw, the
 * draw function then paints the whole current state.
 */
static void      gfig_queue_draw (void);
static void      gfig_draw_line (gint x1, gint y1, gint x2, gint y2);
static void      gfig_draw_rectangle (gint filled, gint x, gint y,
				      gint width, gint height);
static void      gfig_draw_arc (gint filled, gint x, gint y,
				gint width, gint height,
				gint angle1, gint angle2);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

/* The types of an object */
/* Also includes actions that can be performed on objects */

typedef enum DobjType {
  LINE,
  CIRCLE,
  ELLIPSE,
  ARC,
  POLY,
  STAR,
  SPIRAL,
  BEZIER,
  MOVE_OBJ,
  MOVE_POINT,
  COPY_OBJ,
  MOVE_COPY_OBJ,
  DEL_OBJ,
  NULL_OPER
} DOBJTYPE;

typedef enum Gridtype {
  RECT_GRID = 0,
  POLAR_GRID,
  ISO_GRID
} GRIDTYPE;

typedef enum DrawonLayers {
  SINGLE_LAYER = 0,
  ORIGINAL_LAYER,
  MULTI_LAYER
} DRAWONLAYERS;

typedef enum LayersBGType {
  LAYER_TRANS_BG = 0,
  LAYER_BG_BG,
  LAYER_WHITE_BG,
  LAYER_COPY_BG
} DRAWLAYERBG;

typedef enum PaintType {
  PAINT_BRUSH_TYPE = 0,
  PAINT_SELECTION_TYPE,
  PAINT_SELECTION_FILL_TYPE
} PAINTTYPE;

typedef enum BrshType {
  BRUSH_BRUSH_TYPE=0,
  BRUSH_PENCIL_TYPE,
  BRUSH_AIRBRUSH_TYPE,
  BRUSH_PATTERN_TYPE
} BRUSH_TYPE;


#define GRID_TYPE_MENU 1
#define GRID_RENDER_MENU 2
#define GRID_IGNORE 0 
#define GRID_HIGHTLIGHT 1
#define GRID_RESTORE 2

#define GFIG_BLACK_GC -2
#define GFIG_WHITE_GC -3
#define GFIG_GREY_GC -4

#define PAINT_LAYERS_MENU 1
#define PAINT_BGS_MENU 2
#define PAINT_TYPE_MENU 3

#define SELECT_TYPE_MENU 1
#define SELECT_ARCTYPE_MENU 2
#define SELECT_TYPE_MENU_FILL 3
#define SELECT_TYPE_MENU_WHEN 4

#define OBJ_SELECT_GT 1
#define OBJ_SELECT_LT 2
#define OBJ_SELECT_EQ 4


typedef struct GfigOpts
{
  gint gridspacing;
  GRIDTYPE gridtype;
  gint drawgrid;
  gint snap2grid;
  gint lockongrid;
  gint showcontrol;
} GFIGOPTS;

/* Must keep in step with the above */
typedef struct GfigOptWidgets
{
  void * gridspacing;
  GtkWidget * gridtypemenu;
  GtkWidget * drawgrid;
  GtkWidget * snap2grid;
  GtkWidget * lockongrid;
  GtkWidget * showcontrol;
} GFIGOPTWIDGETS;

static GFIGOPTWIDGETS gfig_opt_widget;

typedef struct SelectItVals 
{
  GFIGOPTS opts;
  gint showimage;
  gint maxundo;
  gint showpos;
  gdouble brushfade;
  gdouble airbrushpressure;
  gint showtooltips;
  DRAWONLAYERS onlayers;
  DRAWLAYERBG onlayerbg;
  PAINTTYPE painttype;
  gint reverselines;
  gint scaletoimage;
  gdouble scaletoimagefp;
  gint approxcircles;
  BRUSH_TYPE brshtype;
  DOBJTYPE otype;
} SelectItVals;

/* Values when first invoked */
static SelectItVals selvals =
{
  {
    MIN_GRID + (MAX_GRID - MIN_GRID)/2, /* Gridspacing */
    RECT_GRID, /* Default to rectangle type */
    0,  /* drawgrid */
    0,  /* snap2grid */
    0,  /* lockongrid */
    1,  /* show control points */
  },
  0,  /* show image */
  MIN_UNDO + (MAX_UNDO - MIN_UNDO)/2,  /* Max level of undos */
  FALSE, /* Show pos updates */
  0.0, /* Brush fade */
  20.0, /* Air bursh pressure */
  TRUE,  /* show Tool tips */
  ORIGINAL_LAYER, /* Draw all objects on one layer */
  LAYER_TRANS_BG, /* New layers background */
  PAINT_BRUSH_TYPE, /* Default to use brushes */
  FALSE, /* reverse lines */
  TRUE, /* Scale to image when painting */
  1.0, /* Scale to image fp */
  FALSE, /* Approx circles by drawing lines */
  BRUSH_BRUSH_TYPE, /* Default to use a brush */
  LINE /* Initial object type */
};

typedef enum Selection_Type {
  ADD=0,
  SUBTRACT=1,
  REPLACE=2,
  INTERSECT=3
} SELECTION_TYPE;
    

typedef enum Arc_Type { 
  ARC_SEGMENT,
  ARC_SECTOR
} ARC_TYPE;

typedef enum Fill_Type {
  FILL_FOREGROUND = 0,
  FILL_BACKGROUND = 1,
  FILL_PATTERN = 2
} FILL_TYPE;

typedef enum Fill_When {
  FILL_EACH = 0,
  FILL_AFTER
} FILL_WHEN;

struct selection_option {
  SELECTION_TYPE type; /* ADD etc .. */
  gint antia; /* Boolean for Antia */
  gint feather; /* Feather it ? */
  gdouble feather_radius; /* Radius to feather */
  ARC_TYPE as_pie; /* Arc type selection segment/sector */
  FILL_TYPE fill_type; /* Fill type for selection */
  FILL_WHEN fill_when; /* Fill on each selection or after all? */
  gdouble fill_opacity; /* You can guess this one */
} selopt = {
  ADD, /* type */
  FALSE, /* Antia */
  FALSE, /* Feather */
  10.0, /* feather radius */
  ARC_SEGMENT,  /* Arc as a segment */
  FILL_PATTERN, /* Fill as pattern */
  FILL_EACH, /* Fill after each selection */
  100.0, /* Max opacity */
};


GList *gfig_path_list = NULL;
GList *gfig_list = NULL;
static gint line_no;

static gint poly_num_sides = 3; /* Default to three sided object */
static gint star_num_sides = 3; /* Default to three sided object */
static gint spiral_num_turns = 4; /* Default to 4 turns */
static gint spiral_toggle = 0; /* 0 = clockwise -1 = anti-clockwise */
static gint bezier_closed = 0; /* Closed curve 0 = false 1 = true */
static gint bezier_line_frame = 0; /* Show frame = false 1 = true */

static gint obj_show_single = -1; /* -1 all >= 0 object number */

/* Structures etc for the objects */
/* Points used to draw the object  */

typedef struct DobjPoints {
  struct DobjPoints * next;
  GfigPoint pnt;
  gint found_me;
} DOBJPOINTS;


struct Dobject; /* fwd declaration for DOBJFUNC */

typedef void            (*DOBJFUNC)(struct Dobject *);
typedef struct Dobject *(*DOBJGENFUNC)(struct Dobject *);
typedef struct Dobject *(*DOBJLOADFUNC)(FILE *);
typedef void            (*DOBJSAVEFUNC)(struct Dobject *, FILE *);

/* The object itself */
typedef struct Dobject {
  DOBJTYPE type; /* What is the type? */
  gpointer     type_data; /* Extra data needed by the object */
  DOBJPOINTS * points; /* List of points */
  DOBJFUNC  drawfunc; /* How do I draw myself */
  DOBJFUNC  paintfunc; /* Draw me on canvas */
  DOBJGENFUNC  copyfunc;  /* copy */
  DOBJLOADFUNC loadfunc;  /* Load this type of object */
  DOBJSAVEFUNC savefunc;  /* Save me out */
} DOBJECT;


static DOBJECT *obj_creating; /* Object we are creating */
static DOBJECT *tmp_line; /* Needed when drawing lines */
static DOBJECT *tmp_bezier; /* Neeed when drawing bezier curves */

typedef struct DAllObjs {
  struct DAllObjs * next; 
  DOBJECT * obj; /* Object on list */
} DALLOBJS;

/* States of the object */
#define GFIG_OK       0x0
#define GFIG_MODIFIED 0x1
#define GFIG_READONLY 0x2

typedef struct DFigObj {
  gchar * name;     /* Trailing name of file  */
  gchar * filename; /* Filename itself */
  gchar * draw_name;/* Name of the drawing */
  gfloat version;     /* Version number of data file */
  GFIGOPTS opts;    /* Options enforced when fig saved */
  DALLOBJS * obj_list; /* Objects that make up this list */
  gint obj_status;    /* See above for possible values */
  GtkWidget *list_item;
  GtkWidget *label_widget;
  GtkWidget *pixmap_widget;
} GFIGOBJ;  


typedef struct BrushDesc {
  gchar * bname; /* name of the brush */
  gint32 width;  /* Width of brush */
  gint32 height;  /* Height of brush */
  guchar *pv_buf; /* Buffer where brush placed */
  gint16 x_off;
  gint16 y_off;
  gint bpp; /* Depth - should ALWAYS be the same for all BRUSHDESC */
} BRUSHDESC;

static GFIGOBJ *current_obj;
static DOBJECT *operation_obj;
static GfigPoint *move_all_pnt; /* Point moving all from */
static GFIGOBJ *pic_obj;
static DALLOBJS *undo_table[MAX_UNDO];
static gint need_to_scale;
static gint32 brush_image_ID = -1;

GtkWidget * undo_widget;
GtkWidget *delete_frame_to_freeze; /* Top preview frame window */
GtkWidget *progress_widget; /* Progress widget */
GtkWidget *fade_out_hbox; /* Fade out widget in brush page */
GtkWidget *pressure_hbox; /* Pressure widget in brush page */
GtkWidget *pencil_hbox; /* Dummy widget in brush page */
GtkWidget *x_pos_label; /* X pos marker */
GtkWidget *y_pos_label; /* Y pos marker */
GtkWidget *obj_size_label; /* Size of object showing */
GtkWidget *brush_page_widget; /* Widget for the brush part of notebook */
GtkWidget *select_page_widget; /* Widget for the selection part of notebook */

static gint undo_water_mark = -1; /* Last slot filled in -1 = no undo */
static gint drawing_pic = FALSE; /* If true drawing to the small preview */
GtkWidget *status_label_dname;
GtkWidget *status_label_fname;
GFIGOBJ * gfig_obj_for_menu; /* More static data - need to know which object was selected*/
GtkWidget *save_button;
static GSList *gfig_tooltip_widgets = NULL; /* Widgets that have a tool tip */


/* Don't up just like BIGGG source files? */

void object_start(GfigPoint *pnt,gint);
void object_operation(GfigPoint *pnt,gint);
void object_operation_start(GfigPoint *pnt,gint shift_down);
void object_operation_end(GfigPoint *pnt,gint);
void object_end(GfigPoint *pnt,gint shift_down);
void object_update(GfigPoint * pnt);
static void add_to_all_obj(GFIGOBJ * fobj,DOBJECT *obj);
void d_delete_dobjpoints(DOBJPOINTS *);
DOBJECT * d_new_line(gint x, gint y);
DOBJECT * d_new_circle(gint x, gint y);
DALLOBJS * copy_all_objs(DALLOBJS *objs);
static void setup_undo(void);
static void      d_pnt_add_line(DOBJECT *obj, gint x, gint y, gint pos);
GFIGOBJ * gfig_load (gchar *filename, gchar *name);
static void free_all_objs(DALLOBJS * objs);
static void draw_objects(DALLOBJS *objs,gint show_single);
DOBJECT * d_load_line(FILE *from);
DOBJECT * d_load_circle(FILE *from);
char * get_line(gchar *buf,gint s,FILE * from,gint init);
GFIGOBJ * gfig_new(void);
static void clear_undo(void);
void list_button_update(GFIGOBJ *obj);
static void prepend_to_all_obj(GFIGOBJ *fobj,DALLOBJS *nobj);
static void gfig_update_stat_labels(void);
void gfig_obj_modified(GFIGOBJ *obj,gint stat_type);
static void gridtype_menu_callback (GtkWidget *widget, gpointer data);
void draw_one_obj(DOBJECT * obj);
void d_save_poly(DOBJECT * obj, FILE *to);
DOBJECT * d_load_poly(FILE *from);
static void d_draw_poly(DOBJECT *obj);
static void d_paint_poly(DOBJECT *obj);
DOBJECT * d_copy_poly(DOBJECT * obj);
DOBJECT * d_new_poly(gint x, gint y);
void d_update_poly(GfigPoint *pnt);
void d_poly_start(GfigPoint *pnt,gint shift_down);
void d_poly_end(GfigPoint *pnt,gint shift_down);
void d_save_star(DOBJECT * obj, FILE *to);
DOBJECT * d_load_star(FILE *from);
static void d_draw_star(DOBJECT *obj);
static void d_paint_star(DOBJECT *obj);
DOBJECT * d_copy_star(DOBJECT * obj);
DOBJECT * d_new_star(gint x, gint y);
void d_update_star(GfigPoint *pnt);
void d_star_start(GfigPoint *pnt,gint shift_down);
void d_star_end(GfigPoint *pnt,gint shift_down);
DOBJECT * d_load_spiral(FILE *from);
static void d_draw_spiral(DOBJECT *obj);
static void d_paint_spiral(DOBJECT *obj);
DOBJECT * d_copy_spiral(DOBJECT * obj);
DOBJECT * d_new_spiral(gint x, gint y);
void d_update_spiral(GfigPoint *pnt);
void d_spiral_start(GfigPoint *pnt,gint shift_down);
void d_spiral_end(GfigPoint *pnt,gint shift_down);

DOBJECT * d_load_bezier(FILE *from);
static void d_draw_bezier(DOBJECT *obj);
static void d_paint_bezier(DOBJECT *obj);
DOBJECT * d_copy_bezier(DOBJECT * obj);
DOBJECT * d_new_bezier(gint x, gint y);
void d_update_bezier(GfigPoint *pnt);
void d_bezier_start(GfigPoint *pnt,gint shift_down);
void d_bezier_end(GfigPoint *pnt,gint shift_down);


static void new_obj_2edit(GFIGOBJ *obj);
DOBJECT * d_new_ellipse(gint x, gint y);
DOBJECT * d_load_ellipse(FILE *from);
DOBJECT * d_new_arc(gint x, gint y);
DOBJECT * d_load_arc(FILE *from);
gint load_options(GFIGOBJ *gfig,FILE *fp);
gint gfig_obj_counts(DALLOBJS * objs);
static void about_button_press(GtkWidget *widget,gpointer   data);
static void reload_button_press(GtkWidget *widget,gpointer   data);
static void gfig_brush_fill_preview_xy(GtkWidget *pw,gint x ,gint y);
static void gfig_set_tooltip (GtkWidget *widget, const gchar *tip);
static GtkWidget * gfig_new_pixmap (GtkWidget *list, char **pixdata);
static void create_warn_dialog (const gchar *msg);


/* globals */

/* Grid colours: the GTK 1 widget state backgrounds, and a few more */
#define GFIG_NORMAL_GC      0
#define GFIG_ACTIVE_GC      1
#define GFIG_PRELIGHT_GC    2
#define GFIG_SELECTED_GC    3
#define GFIG_INSENSITIVE_GC 4

gint gfig_run;
gint grid_gc_type = GFIG_NORMAL_GC;
guchar *pv_cache = NULL;
guchar preview_row[PREVIEW_SIZE*4];

/* Stuff for the preview bit */
static gint   sel_x1, sel_y1, sel_x2, sel_y2;
static gint   sel_width, sel_height;
static gint   preview_width, preview_height;
static gint   has_alpha;
static gdouble scale_x_factor,scale_y_factor;
static gdouble org_scale_x_factor,org_scale_y_factor;

MAIN ()

static void
query ()
{
  static GParamDef args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image (unused)" },
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
    { PARAM_INT32, "dummy", "dummy" } 
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0;

  gimp_install_procedure ("plug_in_gfig",
			  "Create Geometrical shapes with the Gimp",
			  "More here later",
			  "Andy Thomas",
			  "Andy Thomas",
			  "1997",
			  "<Image>/Filters/Render/Gfig",
			  "RGB*, GRAY*",
			  PROC_PLUG_IN,
			  nargs, nreturn_vals,
			  args, return_vals);
}

static void
run    (gchar    *name,
	gint      nparams,
	GParam   *param,
	gint     *nreturn_vals,
	GParam  **return_vals)
{
  GParam * values = g_new(GParam, 1);
  GDrawable *drawable;
  GRunModeType run_mode;
  GStatusType status = STATUS_SUCCESS;

  int           pwidth, pheight;

  /*kill(getpid(),19);*/

  run_mode = param[0].data.d_int32;
  gfig_image = param[1].data.d_image;
  gfig_drawable = param[2].data.d_drawable;

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;

  gfig_select_drawable = 
    drawable = 
    gimp_drawable_get (param[2].data.d_drawable);

  tile_width  = gimp_tile_width();
  tile_height = gimp_tile_height();

  /* TMP Hack - clear any selections */
  gfig_clear_selection(gfig_image);

  gimp_drawable_mask_bounds(drawable->id, &sel_x1, &sel_y1, &sel_x2, &sel_y2);

  sel_width  = sel_x2 - sel_x1;
  sel_height = sel_y2 - sel_y1;

  /* Calculate preview size */
  
  if (sel_width > sel_height) {
    pwidth  = MIN(sel_width, PREVIEW_SIZE);
    pheight = sel_height * pwidth / sel_width;
  } else {
    pheight = MIN(sel_height, PREVIEW_SIZE);
    pwidth  = sel_width * pheight / sel_height;
  }
  
  preview_width  = MAX(pwidth, 2);  /* Min size is 2 */
  preview_height = MAX(pheight, 2); 

  org_scale_x_factor = scale_x_factor = (gdouble)sel_width/(gdouble)preview_width;
  org_scale_y_factor = scale_y_factor = (gdouble)sel_height/(gdouble)preview_height;
  
  switch (run_mode)
    {
    case RUN_INTERACTIVE:
      /*gimp_get_data ("plug_in_gfig", &selvals);*/
      if (!gfig_dialog())
	{
	  gimp_drawable_detach (drawable);
	  return;
	}
      break;

    case RUN_NONINTERACTIVE:
      status = STATUS_CALLING_ERROR;
      break;

    case RUN_WITH_LAST_VALS:
      /*gimp_get_data ("plug_in_gfig", &selvals);*/
      break;

    default:
      break;
    }

  if (gimp_drawable_color (drawable->id) || gimp_drawable_gray (drawable->id))
    {
      /* Set the tile cache size */

      gimp_tile_cache_ntiles((drawable->width + gimp_tile_width() - 1) / gimp_tile_width());

      do_gfig();
   
      if (run_mode != RUN_NONINTERACTIVE)
	gimp_displays_flush ();

#if 0
      if (run_mode == RUN_INTERACTIVE)
	gimp_set_data ("plug_in_gfig", &selvals, sizeof (SelectItVals));
#endif /* 0 */
    }
  else
    {
      status = STATUS_EXECUTION_ERROR;
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

/* From testgtk */
static void
ok_warn_window(GtkWidget * widget, 
		    gpointer   data)
{
  gimp_widget_destroy (GTK_WIDGET (data));
}

static void
create_warn_dialog (const gchar *msg)
{
  GtkWidget *window = NULL;
  GtkWidget *label;

  window = gimp_dialog_new ("Warning");
  if (gfig_top_level)
    gtk_window_set_transient_for (GTK_WINDOW (window),
				  GTK_WINDOW (gfig_top_level));

  gimp_dialog_add_button (window, "OK", G_CALLBACK (ok_warn_window),
			  window, TRUE);

  label = gtk_label_new(msg);
  gimp_container_set_border_width (label, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (window), label, TRUE, TRUE, 0);
  gtk_window_present (GTK_WINDOW (window));
}


static void
gfig_clear_selection(gint32 image_ID)
{
  GParam *return_vals;
  int nreturn_vals;

  /* Clear any selection - needed because drawing circles/ellipses 
   * uses the selection method and will confuse the output.
   */
  return_vals = gimp_run_procedure ("gimp_selection_clear",
                                    &nreturn_vals,
                                    PARAM_IMAGE, image_ID,
                                    PARAM_END);
  
  gimp_destroy_params (return_vals, nreturn_vals);
}

/*
 *	Query gimprc for gfig-path, and parse it.
 *	This code is based on script_fu_find_scripts ()
 *      and the Gflare plugin.
 */

void
plug_in_parse_gfig_path()
{
  GParam *return_vals;
  gint nreturn_vals;
  gchar *path_string;
  const gchar *home;
  gchar *path;
  gchar **tokens;
  gchar *token;
  gint   i;
  gchar buf[256];

  if(gfig_path_list)
    g_list_free_full(gfig_path_list, g_free);
  
  gfig_path_list = NULL;
  
  return_vals = gimp_run_procedure ("gimp_gimprc_query",
				    &nreturn_vals,
				    PARAM_STRING, "gfig-path",
				    PARAM_END);
  
  if (return_vals[0].data.d_status != STATUS_SUCCESS || return_vals[1].data.d_string == NULL)
    {
      g_warning("No gfig-path in gimprc: gfig_path_list is NULL\nSee README file");
      create_warn_dialog("No gfig-path in gimprc: gfig_path_list is NULL. See README file\n");
#if 0
      /*"You need to add an entry like\n"
       *		   "(gfig-path \"${gimp_dir}/gfig:${gimp_data_dir}/gfig\n"
       *"to your ~/.gimprc/gimprc file\n");
       */
#endif /* 0 */
      gimp_destroy_params (return_vals, nreturn_vals);
      return;
    }
  
  path_string = g_strdup (return_vals[1].data.d_string);
  gimp_destroy_params (return_vals, nreturn_vals);
  
  /* Set local path to contain temp_path, where (supposedly)
   * there may be working files.
   */
  home = g_get_home_dir ();

  /* Search through all directories in the  path */

  tokens = g_strsplit (path_string, G_SEARCHPATH_SEPARATOR_S, -1);

  for (i = 0; tokens[i]; i++)
    {
      token = tokens[i];

      if (*token == '\0')
	continue;

      if (*token == '~')
	path = g_build_filename (home, token + 1, NULL);
      else
	path = g_strdup (token);

      /* Check if directory exists */
      if (g_file_test (path, G_FILE_TEST_IS_DIR))
	{
#ifdef DEBUG
	  printf("Added `%s' to gfig_path_list\n", path);
#endif /* DEBUG */
	  gfig_path_list = g_list_append (gfig_path_list, path);
	}
      else
	{
	  g_snprintf(buf, sizeof (buf),
		     "gfig-path misconfigured - \nPath `%.100s' not found\n", path);
	  g_warning("%s", buf);
	  create_warn_dialog(buf);
	  g_free (path);
	}
    }
  g_strfreev (tokens);
  g_free (path_string);
}


/*
  Translate SPACE to "\\040", etc.
  Taken from gflare plugin
 */
void
gfig_name_encode (gchar *dest, gchar *src)
{
  /* at most MAX_LOAD_LINE - 1 output bytes (plus the terminator) */
  int	cnt = MAX_LOAD_LINE - 1;

  while (*src && cnt > 0)
    {
      guchar c = (guchar) *src;

      if (iscntrl (c) || isspace (c) || c == '\\')
	{
	  if (cnt < 4)
	    break;
	  g_snprintf (dest, 5, "\\%03o", c);
	  dest += 4;
	  cnt -= 4;
	  src++;
	}
      else
	{
	  *dest++ = *src++;
	  cnt--;
	}
    }
  *dest = '\0';
}

/*
  Translate "\\040" to SPACE, etc.
 */
void
gfig_name_decode (gchar *dest, gchar *src)
{
  int	cnt = MAX_LOAD_LINE - 1;
  int	tmp;

  while (*src && cnt--)
    {
      if (*src == '\\' && *(src+1) && *(src+2) && *(src+3))
	{
	  sscanf (src+1, "%3o", &tmp);
	  *dest++ = tmp;
	  src += 4;
	}
      else
	*dest++ = *src++;
    }
  *dest = '\0';
}


/*
 * Load all gfig, which are founded in gfig-path-list, into gfig_list.
 * gfig-path-list must be initialized first. (plug_in_parse_gfig_path ())
 * based on code from Gflare.
 */

gint
gfig_list_pos(GFIGOBJ *gfig)
{
  GFIGOBJ *g;
  int n;
  GList *tmp;

  n = 0;
  tmp = gfig_list;
  
  while (tmp) 
    {
      g = tmp->data;
      
      if (strcmp (gfig->draw_name, g->draw_name) <= 0)
	break;
      n++;
      tmp = tmp->next;
    }

  return(n);
}

gint
gfig_list_insert (GFIGOBJ *gfig)
{
  int		n;

  /*
   *	Insert gfigs in alphabetical order
   */

  n = gfig_list_pos(gfig);

  gfig_list = g_list_insert (gfig_list, gfig, n);

#ifdef DEBUG
  printf("gfig_list_insert %s => %d\n", gfig->draw_name, n);
#endif /* DEBUG */

  return n;
}

void
gfig_free(GFIGOBJ * gfig)
{
  g_assert (gfig != NULL);

  if(gfig->obj_list)
    free_all_objs(gfig->obj_list);
  if(gfig->name)
    g_free(gfig->name);
  if(gfig->filename)
    g_free(gfig->filename);
  if(gfig->draw_name)
    g_free(gfig->draw_name);
  g_free (gfig);
}

void
gfig_free_everything(GFIGOBJ * gfig)
{
  g_assert (gfig != NULL);

  if(gfig->filename)
    {
#ifdef DEBUG
      printf("Removing filename '%s'\n",gfig->filename);
#endif /* DEBUG */
      g_remove(gfig->filename);
    }
  gfig_free(gfig);
}

void
gfig_list_free_all ()
{
  GList * list;
  GFIGOBJ * gfig;

  list = gfig_list;
  while (list)
    {
      gfig = (GFIGOBJ *) list->data;
      gfig_free (gfig);
      list = list->next;
    }

  g_list_free (gfig_list);
  gfig_list = NULL;
}


void
gfig_list_load_all(GList *plist)
{
  GFIGOBJ  * gfig;
  GList    * list;
  gchar	   * path;
  gchar	   * filename;
  GDir	   * dir;
  const gchar *dir_ent;

  /*  Make sure to clear any existing gfigs  */
  current_obj = pic_obj = NULL;
  gfig_list_free_all ();

  list = plist;
  while (list)
    {
      path = list->data;
      list = list->next;

      /* Open directory */
      dir = g_dir_open (path, 0, NULL);

      if (!dir)
	g_warning("error reading GFig directory \"%s\"", path);
      else
	{
	  while ((dir_ent = g_dir_read_name (dir)))
	    {
	      filename = g_build_filename (path, dir_ent, NULL);

	      /* Check the file and see that it is not a sub-directory */
	      if (g_file_test (filename, G_FILE_TEST_IS_REGULAR))
		{
		  gfig = gfig_load (filename, (gchar *) dir_ent);

		  if (gfig)
		    {
		      /* Read only ?*/
		      if(g_access(filename,W_OK))
			gfig->obj_status |= GFIG_READONLY;

		      gfig_list_insert (gfig);
		    }
		}

	      g_free (filename);
	    } /* while */
	  g_dir_close (dir);
	} /* else */
    }

  if(!gfig_list)
    {
      /* lets have at least one! */
      gfig = gfig_new();
      gfig->draw_name = g_strdup("First gfig");
      gfig_list_insert(gfig);
    }
  pic_obj = current_obj = gfig_list->data;  /* set to first entry */

}

GFIGOBJ *
gfig_new(void)
{
  GFIGOBJ * new;

  new = g_new0(GFIGOBJ,1);
  return(new);
}

/* The drawing code dereferences a fixed number of points for most
 * object types; make sure objects loaded from a file have them. */
static gint
gfig_obj_has_enough_points(DOBJECT *obj)
{
  gint needed;
  gint count = 0;
  DOBJPOINTS *pnt;

  switch(obj->type)
    {
    case CIRCLE:
    case ELLIPSE:
    case POLY:
    case SPIRAL:
      needed = 2;
      break;
    case ARC:
    case STAR:
      needed = 3;
      break;
    default:
      needed = 1;
      break;
    }

  for(pnt = obj->points; pnt && count < needed; pnt = pnt->next)
    count++;

  return(count >= needed);
}

void
gfig_load_objs(GFIGOBJ *gfig,gint load_count,FILE *fp)
{
  DOBJECT *obj;
  gchar load_buf[MAX_LOAD_LINE];

  /* Loading object */
  /*kill(getpid(),19);*/
  /* Read first line */
  while(load_count-- > 0)
    {
      obj = NULL;
      if(!get_line(load_buf,MAX_LOAD_LINE,fp,0))
	break; /* EOF - the object count check will catch it */

      if(!strcmp(load_buf,"<LINE>"))
	{
	  obj = d_load_line(fp);
	}
      else if(!strcmp(load_buf,"<CIRCLE>"))
	{
	  obj = d_load_circle(fp);
	}
      else if(!strcmp(load_buf,"<ELLIPSE>"))
	{
	  obj = d_load_ellipse(fp);
	}
      else if(!strcmp(load_buf,"<POLY>"))
	{
	  obj = d_load_poly(fp);
	}
      else if(!strcmp(load_buf,"<STAR>"))
	{
	  obj = d_load_star(fp);
	}
      else if(!strcmp(load_buf,"<SPIRAL>"))
	{
	  obj = d_load_spiral(fp);
	}
      else if(!strcmp(load_buf,"<BEZIER>"))
	{
	  obj = d_load_bezier(fp);
	}
      else if(!strcmp(load_buf,"<ARC>"))
	{
	  obj = d_load_arc(fp);
	}
      else
	{
	  g_warning("Unknown obj type file %s line %d\n",gfig->filename,line_no);
	}
      
      if(obj && !gfig_obj_has_enough_points(obj))
	{
	  g_warning("Too few points for object in file %s line %d\n",
		    gfig->filename,line_no);
	  obj = NULL;
	}

      if(obj)
	{
	  add_to_all_obj(gfig,obj);
	}
    }
}

GFIGOBJ *
gfig_load (gchar *filename, gchar *name)
{
  GFIGOBJ * gfig;
  FILE * fp;
  gchar load_buf[MAX_LOAD_LINE];
  gchar str_buf[MAX_LOAD_LINE];
  gint chk_count;
  gint load_count = 0;
  
  g_assert (filename != NULL);

#ifdef DEBUG
  printf("Loading %s(%s)\n",filename,name);
#endif /* DEBUG */

  fp = g_fopen (filename, "r");
  if (!fp)
    {
      g_warning ("Error opening: %s", filename);
      return NULL;
    }

  gfig = gfig_new();

  gfig->name = g_strdup(name);
  gfig->filename = g_strdup(filename);


  /* HEADER
   * draw_name
   * version
   * obj_list
   */

  get_line(load_buf,MAX_LOAD_LINE,fp,1);

  if(strncmp(GFIG_HEADER,load_buf,strlen(load_buf)))
    {
      gchar err[256];
      g_snprintf(err,sizeof(err),"File '%s' is not a gfig file",gfig->filename);
      fclose(fp);
      create_warn_dialog(err);
      return(NULL);
    }
  
  get_line(load_buf,MAX_LOAD_LINE,fp,0);
  str_buf[0] = 0;
  sscanf(load_buf,"Name: %100s",str_buf);
  gfig_name_decode(load_buf,str_buf);
  gfig->draw_name = g_strdup(load_buf);

  get_line(load_buf,MAX_LOAD_LINE,fp,0);
  sscanf(load_buf,"Version: %f",&gfig->version);

  get_line(load_buf,MAX_LOAD_LINE,fp,0);
  sscanf(load_buf,"ObjCount: %d",&load_count);

  if(load_options(gfig,fp))
    {
      /* waste some mem */
      gchar err[256];
      fclose(fp);
      g_snprintf(err,sizeof(err),
	      "File '%s' corrupt file - Line %d Option section incorrect",
	      filename,
	      line_no);
      create_warn_dialog(err);
      return(NULL);
    }

  /*return(NULL);*/

  gfig_load_objs(gfig,load_count,fp);

  /* Check count ? */
  
  chk_count = gfig_obj_counts(gfig->obj_list);

  if(chk_count != load_count)
    {
      /* waste some mem */
      gchar err[256];
      fclose(fp);
      g_snprintf(err,sizeof(err),"File '%s' corrupt file - Line %d Object count to small",
	      filename,
	      line_no);
      create_warn_dialog(err);
      return(NULL);
    }

  fclose(fp);

  if(!pic_obj)
    pic_obj = gfig;

  gfig->obj_status = GFIG_OK;

  return(gfig);
}

void
save_options(FILE *fp)
{
  /* Save options */
  fprintf(fp,"<OPTIONS>\n");
  fprintf(fp,"GridSpacing: %d\n",selvals.opts.gridspacing);
  if(selvals.opts.gridtype == RECT_GRID)
     fprintf(fp,"GridType: RECT_GRID\n");
  else if(selvals.opts.gridtype == POLAR_GRID)
     fprintf(fp,"GridType: POLAR_GRID\n");
  else if(selvals.opts.gridtype == ISO_GRID)
     fprintf(fp,"GridType: ISO_GRID\n");
  else fprintf(fp,"GridType: RECT_GRID\n"); /* If in doubt, default to RECT_GRID */
  fprintf(fp,"DrawGrid: %s\n",(selvals.opts.drawgrid)?"TRUE":"FALSE");
  fprintf(fp,"Snap2Grid: %s\n",(selvals.opts.snap2grid)?"TRUE":"FALSE");
  fprintf(fp,"LockOnGrid: %s\n",(selvals.opts.lockongrid)?"TRUE":"FALSE");
  /*  fprintf(fp,"ShowImage: %s\n",(selvals.opts.showimage)?"TRUE":"FALSE");*/
  fprintf(fp,"ShowControl: %s\n",(selvals.opts.showcontrol)?"TRUE":"FALSE");
  fprintf(fp,"</OPTIONS>\n");
}

gint
load_bool(gchar * opt_buf,gint *toset)
{
  if(!strcmp(opt_buf,"TRUE"))
    *toset = 1;
  else if(!strcmp(opt_buf,"FALSE"))
    *toset = 0;
  else
    return(-1);

  return(0);
}

static void
update_options(GFIGOBJ *old_obj)
{
  /* Save old vals */
  if(selvals.opts.gridspacing != old_obj->opts.gridspacing)
    {
      old_obj->opts.gridspacing = selvals.opts.gridspacing;
    }
  if(selvals.opts.gridtype != old_obj->opts.gridtype)
    {
      old_obj->opts.gridtype = selvals.opts.gridtype;
    }
  if(selvals.opts.drawgrid != old_obj->opts.drawgrid)
    {
      old_obj->opts.drawgrid = selvals.opts.drawgrid;
    }
  if(selvals.opts.snap2grid != old_obj->opts.snap2grid)
    {
      old_obj->opts.snap2grid = selvals.opts.snap2grid;
    }
  if(selvals.opts.lockongrid != old_obj->opts.lockongrid)
    {
      old_obj->opts.lockongrid = selvals.opts.lockongrid;
    }
  if(selvals.opts.showcontrol != old_obj->opts.showcontrol)
    {
      old_obj->opts.showcontrol = selvals.opts.showcontrol;
    }

  /* New vals */
  if(selvals.opts.gridspacing != current_obj->opts.gridspacing)
    {
      /*selvals.opts.gridspacing = current_obj->opts.gridspacing;*/
      gtk_adjustment_set_value (GTK_ADJUSTMENT (gfig_opt_widget.gridspacing),
				current_obj->opts.gridspacing);
    }
  if(selvals.opts.drawgrid != current_obj->opts.drawgrid)
    {
      gtk_check_button_set_active (GTK_CHECK_BUTTON (gfig_opt_widget.drawgrid),current_obj->opts.drawgrid);
    }
  if(selvals.opts.snap2grid != current_obj->opts.snap2grid)
    {
      gtk_check_button_set_active (GTK_CHECK_BUTTON (gfig_opt_widget.snap2grid),current_obj->opts.snap2grid);
    }
  if(selvals.opts.lockongrid != current_obj->opts.lockongrid)
    {
      gtk_check_button_set_active (GTK_CHECK_BUTTON (gfig_opt_widget.lockongrid),current_obj->opts.lockongrid);
    }
  if(selvals.opts.showcontrol != current_obj->opts.showcontrol)
    {
      gtk_check_button_set_active (GTK_CHECK_BUTTON (gfig_opt_widget.showcontrol),current_obj->opts.showcontrol);
    }
  if(selvals.opts.gridtype != current_obj->opts.gridtype)
    {
      gimp_option_menu_set_history (gfig_opt_widget.gridtypemenu,
				    current_obj->opts.gridtype);

      gridtype_menu_callback (gfig_opt_widget.gridtypemenu,
			      gimp_option_menu_get_item_data (gfig_opt_widget.gridtypemenu,
							      current_obj->opts.gridtype));
#ifdef DEBUG
      printf("Gridtype set in options to ");
      if (current_obj->opts.gridtype == RECT_GRID)
	 printf("RECT_GRID\n");
      else if (current_obj->opts.gridtype == POLAR_GRID)
	 printf("POLAR_GRID\n");
      else if (current_obj->opts.gridtype == ISO_GRID)
	 printf("ISO_GRID\n");
      else printf("NONE\n");
#endif /* DEBUG */
    }
}

gint
load_options(GFIGOBJ *gfig,FILE *fp)
{
  gchar load_buf[MAX_LOAD_LINE];
  gchar str_buf[MAX_LOAD_LINE];
  gchar opt_buf[MAX_LOAD_LINE];

  get_line(load_buf,MAX_LOAD_LINE,fp,0);

#ifdef DEBUG
  printf("load '%s'\n",load_buf);
#endif /* DEBUG */

  if(strcmp(load_buf,"<OPTIONS>"))
    return(-1);
  
  get_line(load_buf,MAX_LOAD_LINE,fp,0);

#ifdef DEBUG
  printf("opt line '%s'\n",load_buf);
#endif /* DEBUG */

  while(strcmp(load_buf,"</OPTIONS>"))
    {
      /* Get option name */
      str_buf[0] = opt_buf[0] = 0;
#ifdef DEBUG
      printf("num = %d\n",sscanf(load_buf,"%s %s",str_buf,opt_buf));

      printf("option %s val %s\n",str_buf,opt_buf);
#else
      sscanf(load_buf,"%s %s",str_buf,opt_buf);
#endif /* DEBUG */

      if(!strcmp(str_buf,"GridSpacing:"))
	{
	  /* Value is decimal */
	  int sp = 0;
	  sp = atoi(opt_buf);
	  if(sp <= 0)
	    return(-1);
	  gfig->opts.gridspacing = sp;
	}
      else if(!strcmp(str_buf,"DrawGrid:"))
	{
	  /* Value is bool */
	  if(load_bool(opt_buf,&gfig->opts.drawgrid))
	    return(-1);
	}
      else if(!strcmp(str_buf,"Snap2Grid:"))
	{
	  /* Value is bool */
	  if(load_bool(opt_buf,&gfig->opts.snap2grid))
	    return(-1);
	}
      else if(!strcmp(str_buf,"LockOnGrid:"))
	{
	  /* Value is bool */
	  if(load_bool(opt_buf,&gfig->opts.lockongrid))
	    return(-1);
	}
      else if(!strcmp(str_buf,"ShowControl:"))
	{
	  /* Value is bool */
	  if(load_bool(opt_buf,&gfig->opts.showcontrol))
	    return(-1);
	}
      else if(!strcmp(str_buf,"GridType:"))
	{
	  /* Value is string */
	  if(!strcmp(opt_buf,"RECT_GRID"))
	    gfig->opts.gridtype = RECT_GRID;
	  else if(!strcmp(opt_buf,"POLAR_GRID"))
	    gfig->opts.gridtype = POLAR_GRID;
	  else if(!strcmp(opt_buf,"ISO_GRID"))
	    gfig->opts.gridtype = ISO_GRID;
	  else
	    return(-1);
	}

      if(!get_line(load_buf,MAX_LOAD_LINE,fp,0))
	return(-1); /* EOF inside the option section */
#ifdef DEBUG
      printf("opt line '%s'\n",load_buf);
#endif /* DEBUG */
    }  
  return(0);
}

gint
gfig_obj_counts(DALLOBJS * objs)
{
  gint count = 0;

  while(objs)
    {
      count++;
      objs = objs->next;
    }

  return(count);
}

void
gfig_save_callbk()
{
  FILE *fp;
  DALLOBJS * objs;
  gint count = 0;
  gchar * savename;
  gchar conv_buf[MAX_LOAD_LINE*3 +1];

  savename = current_obj->filename;

  fp = g_fopen (savename, "w+");
  
  if (!fp)
    {
      gchar errbuf[256];
      sprintf(errbuf,"Error opening '%.100s' could not save", savename);
      create_warn_dialog(errbuf);
      g_warning ("%s", errbuf);
      return;
    }

  /* Write header out */
  fputs(GFIG_HEADER,fp);
  
  /* 
   * draw_name 
   * version
   * obj_list
   *
   */

  gfig_name_encode(conv_buf,current_obj->draw_name);
  fprintf(fp,"Name: %s\n",conv_buf);
  fprintf(fp,"Version: %f\n",current_obj->version);
  objs = current_obj->obj_list;

  count = gfig_obj_counts(objs);

  fprintf(fp,"ObjCount: %d\n",count);
  
  save_options(fp);

  objs = current_obj->obj_list;
  while(objs)
    {
      objs->obj->savefunc(objs->obj,fp);
      objs = objs->next;
    }

  if(ferror(fp))
    create_warn_dialog("Failed to write file\n");
  else
    {
      gfig_obj_modified(current_obj,GFIG_OK);
      current_obj->obj_status &= ~(GFIG_MODIFIED|GFIG_READONLY);
    }

  fclose(fp);

  gfig_update_stat_labels();
}

static void
file_selection_ok (const gchar *filenamebuf,
		   gpointer     data)
{
  GFIGOBJ *obj = (GFIGOBJ *) data;
  GFIGOBJ *real_current;

  if (!filenamebuf)
    return; /* Cancelled */

#ifdef DEBUG
  g_print ("name selected '%s'\n", filenamebuf);
#endif /* DEBUG */

  /* Get the name */
  if(strlen(filenamebuf) == 0)
    {
      create_warn_dialog("Save:- No filename given");
      return;
    }

  /* Check if directory exists */
  if (g_file_test (filenamebuf, G_FILE_TEST_IS_DIR))
    {
      /* Can't save to directory */
      create_warn_dialog("Save:- Can't save to a directory");
      return;
    }

  g_free (obj->filename);
  obj->filename = g_strdup(filenamebuf);

  real_current = current_obj;
  current_obj = obj;
  gfig_save_callbk();
  current_obj = real_current;
}

static void
create_file_selection (GFIGOBJ *obj, gchar *tpath)
{
  const gchar *initial;

  if(tpath)
    initial = tpath;
  else if(gfig_path_list)
    /* Last path is where usually saved to */
    initial = g_list_last (gfig_path_list)->data;
  else
    initial = g_get_tmp_dir ();

  gimp_file_dialog_save (GTK_WINDOW (gfig_top_level), "Save gfig drawing",
			 initial, file_selection_ok, obj);
}

static void
gfig_save(void)
{
  /* Save the current object */
  if(!current_obj->filename)
   {

     create_file_selection(current_obj,NULL);
     return;
    }
  gfig_save_callbk();
}


/* Cache the preview image - updates are a lot faster. */
/* The preview_cache will contain the small image */

static void
cache_preview()
{
  GPixelRgn src_rgn;
  int y,x;
  guchar *src_rows;
  guchar *p;
  int isgrey = 0;

  gimp_pixel_rgn_init(&src_rgn,gfig_select_drawable,sel_x1,sel_y1,sel_width,sel_height,FALSE,FALSE);

  src_rows = g_new(guchar ,sel_width*4); 
  p = pv_cache = g_new(guchar ,preview_width*preview_height*4);

  real_img_bpp = gimp_drawable_bpp(gfig_select_drawable->id);   

  has_alpha = gimp_drawable_has_alpha(gfig_select_drawable->id);

  if(real_img_bpp < 3)
    {
      img_bpp = 3 + has_alpha;
    }
  else
    {
      img_bpp = real_img_bpp;
    }

  switch ( gimp_drawable_type (gfig_select_drawable->id) )
    {
    case GRAYA_IMAGE:
    case GRAY_IMAGE:
      isgrey = 1;
    default:
      break;
    }

  /*memset(p,-1,preview_width*preview_height*4); return;*/

  for (y = 0; y < preview_height; y++) {
  
    gimp_pixel_rgn_get_row(&src_rgn,
			   src_rows,
			   sel_x1,
			   sel_y1 + (y*sel_height)/preview_height,
			   sel_width);
      
    for (x = 0; x < (preview_width); x ++) {
      /* Get the pixels of each col */
      int i;
      for (i = 0 ; i < 3; i++ )
	p[x*img_bpp+i] = src_rows[((x*sel_width)/preview_width)*src_rgn.bpp +((isgrey)?0:i)]; 
      if(has_alpha)
	p[x*img_bpp+3] = src_rows[((x*sel_width)/preview_width)*src_rgn.bpp + ((isgrey)?1:3)];
    }
    p += (preview_width*img_bpp);
  }
  g_free(src_rows);
}

static void
refill_cache()
{
  if (gfig_top_level)
    gtk_widget_set_cursor_from_name (gfig_top_level, "wait");

  cache_preview();

  if (gfig_top_level)
    gtk_widget_set_cursor (gfig_top_level, NULL);

  toggle_obj_type(NULL,GINT_TO_POINTER(selvals.otype));
}

/* An XPM as a paintable, for GtkPicture */
static GdkPaintable *
gfig_paintable_from_xpm (char **pixdata)
{
  GdkPixbuf  *pixbuf;
  GdkTexture *texture;

  pixbuf = gdk_pixbuf_new_from_xpm_data ((const char **) pixdata);
  texture = gdk_texture_new_for_pixbuf (pixbuf);
  g_object_unref (pixbuf);

  return GDK_PAINTABLE (texture);
}

static void
gfig_set_pixmap(GFIGOBJ *obj,char **pixdata)
{
  GdkPaintable *paintable;

  if (!obj->pixmap_widget)
    return;

  paintable = gfig_paintable_from_xpm (pixdata);
  gtk_picture_set_paintable (GTK_PICTURE (obj->pixmap_widget), paintable);
  g_object_unref (paintable);
}


static GtkWidget*
gfig_new_pixmap(GtkWidget *list, char **pixdata)
{
  GtkWidget    *pixmap_widget;
  GdkPaintable *paintable;

  paintable = gfig_paintable_from_xpm (pixdata);
  pixmap_widget = gtk_picture_new_for_paintable (paintable);
  gtk_picture_set_can_shrink (GTK_PICTURE (pixmap_widget), FALSE);
  gtk_widget_set_halign (pixmap_widget, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (pixmap_widget, GTK_ALIGN_CENTER);
  g_object_unref (paintable);

  return(pixmap_widget);
}

/* A GtkListBox row showing a pixmap and a label */
static GtkWidget*
gfig_list_item_new_with_label_and_pixmap (GFIGOBJ *obj, gchar *label, GtkWidget *pix_widget)
{
  GtkWidget *list_item;
  GtkWidget *label_widget;
  GtkWidget *hbox;

  hbox = gimp_hbox_new(FALSE,1);

  list_item = gtk_list_box_row_new ();
  label_widget = gtk_label_new (label);
  gtk_label_set_xalign (GTK_LABEL (label_widget), 0.0);

  gimp_box_pack_start(hbox,pix_widget,FALSE,FALSE,0);
  gimp_box_pack_start(hbox,label_widget,TRUE,TRUE,0);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (list_item), hbox);

  obj->label_widget = label_widget;
  obj->pixmap_widget = pix_widget;
  obj->list_item = list_item;

  return list_item;
}

void
gfig_obj_modified(GFIGOBJ *obj,gint stat_type)
{
  /* stat_type is the status we want to change to */
  /* Change pixmap if not already in correct state */

  g_assert(obj != NULL);

  if(obj->obj_status == stat_type)
    return;

  /* Set the new one up */
  if(stat_type == GFIG_MODIFIED)
    gfig_set_pixmap(obj,Floppy6_xpm);
  else
    gfig_set_pixmap(obj,blank_xpm);
}

static void
select_button_press(GtkWidget *widget,
		    gpointer data)
{
  gint type = GPOINTER_TO_INT(data);
  gint count = 0;
  DALLOBJS * objs;

  if(current_obj)
    {
      objs = current_obj->obj_list;

      while(objs)
	{
	  objs = objs->next;
	  count++;
	}
    }

  switch(type)
    {
    case OBJ_SELECT_LT:
      obj_show_single--;
      if(obj_show_single < 0)
	obj_show_single = count - 1;
      break;
    case OBJ_SELECT_GT:
      obj_show_single++;
      if(obj_show_single >= count)
	obj_show_single = 0;
      break;
    case OBJ_SELECT_EQ:
      obj_show_single = -1; /* Reset to show all */
      break;
    default:
      break;
    }

  draw_grid_clear(widget,data);
}

static GtkWidget *
obj_select_buttons(void)
{
  GtkWidget *button;
  GtkWidget *hbox,*vbox;

  hbox = gimp_hbox_new(FALSE, 0);
  vbox = gimp_vbox_new(FALSE, 0);

  button = gtk_button_new_with_label ("<");
  gimp_box_pack_start (hbox, button, TRUE, TRUE, 0);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (select_button_press),
		    GINT_TO_POINTER (OBJ_SELECT_LT));

  button = gtk_button_new_with_label (">");
  gimp_box_pack_start (hbox, button, TRUE, TRUE, 0);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (select_button_press),
		    GINT_TO_POINTER (OBJ_SELECT_GT));

  gimp_box_pack_start (vbox, hbox, TRUE, TRUE, 0);

  button = gtk_button_new_with_label ("==");
  gimp_box_pack_start (vbox, button, TRUE, TRUE, 0);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (select_button_press),
		    GINT_TO_POINTER (OBJ_SELECT_EQ));

  return(vbox);
}

/* A toggle button showing a pixmap; the buttons made with the same
 * group work like radio buttons.
 */
static GtkWidget *
but_with_pix(char **pixdata, GtkWidget **group, gint baction)
{
  GtkWidget * button;
  GtkWidget * pixmap_widget;

  button = gtk_toggle_button_new();
  if (*group)
    gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (button),
				 GTK_TOGGLE_BUTTON (*group));
  else
    {
      *group = button;
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
    }

  g_signal_connect (button, "toggled",
		    G_CALLBACK (toggle_obj_type),
		    GINT_TO_POINTER (baction));

  pixmap_widget = gfig_new_pixmap(button,pixdata);
  gtk_button_set_child (GTK_BUTTON (button), pixmap_widget);
  return(button);
}


static GtkWidget *
small_preview(GtkWidget * list)
{
  GtkWidget * label;
  GtkWidget * frame;
  GtkWidget * button;
  GtkWidget * vbox;

  vbox = gimp_vbox_new(FALSE, 0);
  gimp_container_set_border_width (vbox, 4);

  label = gtk_label_new("Prev");
  gimp_box_pack_start (vbox, label, FALSE, FALSE, 0);

  frame = gtk_frame_new (NULL);
  gtk_widget_set_halign (frame, GTK_ALIGN_CENTER);
  gimp_box_pack_start (vbox, frame, FALSE, FALSE, 0);

  /* White, with the objects drawn on it */
  pic_preview = gtk_drawing_area_new ();
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (pic_preview),
				      SMALL_PREVIEW_SZ);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (pic_preview),
				       SMALL_PREVIEW_SZ);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (pic_preview),
				  pic_preview_draw, NULL, NULL);
  gtk_frame_set_child (GTK_FRAME (frame), pic_preview);

  /* More Buttons */

  button = gtk_button_new_with_label ("Edit");
  gimp_box_pack_start (vbox, button, FALSE, FALSE, 0);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (edit_button_press),
		    (gpointer) list);
  gfig_set_tooltip(button,"Edit Gfig object collection");

  button = gtk_button_new_with_label ("Merge");
  gimp_box_pack_start (vbox, button, FALSE, FALSE, 0);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (merge_button_press),
		    (gpointer)list);
  gfig_set_tooltip(button,"Merge Gfig Object collection into the current edit session");

  return(vbox);
}

/* Spiral direction menu */
static void
spiral_dir_callback (GtkWidget *option_menu,
		     gpointer   data)
{
  gint *which_way = g_object_get_data (G_OBJECT (option_menu), "which_way");

  if (which_way)
    *which_way = GPOINTER_TO_INT (data);
}

/* Special case for now - options on poly/star/spiral button */

static void
num_sides_dialog (gchar * d_title,
		  gint * num_sides,
		  gint * which_way,
		  gint adj_min,
		  gint adj_max)
{
  GtkWidget *window = NULL;
  GtkWidget *label;
  GtkWidget *hbox;
  GtkAdjustment *size_data;
  GtkWidget *slider;
  GtkWidget *entry;
  gchar buf[512];

  window = gimp_dialog_new (d_title);
  if (gfig_top_level)
    gtk_window_set_transient_for (GTK_WINDOW (window),
				  GTK_WINDOW (gfig_top_level));

  gimp_dialog_add_button (window, "Close", G_CALLBACK (ok_warn_window),
			  window, TRUE);

  hbox = gimp_hbox_new(FALSE, 0);
  gimp_container_set_border_width (hbox, 4);
  label = gtk_label_new("Number of sides/points/turns:-");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);

  gimp_box_pack_start (hbox, label, TRUE, TRUE, 1);

  size_data = gtk_adjustment_new (*num_sides, adj_min, adj_max, 5, 1, 0);
  slider = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, size_data);
  gtk_scale_set_draw_value (GTK_SCALE (slider), TRUE);
  gtk_widget_set_size_request (slider, 100, -1);
  gimp_box_pack_start (hbox, slider,TRUE,TRUE,0);
  gtk_scale_set_value_pos (GTK_SCALE (slider), GTK_POS_LEFT);
  gtk_scale_set_digits (GTK_SCALE (slider), 0);
  g_signal_connect (size_data, "value-changed",
		    G_CALLBACK (gfig_scale_update),
		    num_sides);

  entry = gtk_entry_new();
  g_object_set_data (G_OBJECT (entry), "user_data", size_data);
  g_object_set_data (G_OBJECT (size_data), "user_data", entry);
  gtk_editable_set_width_chars (GTK_EDITABLE (entry), 4);
  g_snprintf(buf, sizeof (buf), "%d", *num_sides);
  gtk_editable_set_text (GTK_EDITABLE (entry), buf);
  g_signal_connect (entry, "changed",
		    G_CALLBACK (gfig_entry_update),
		    num_sides);
  gimp_box_pack_start (hbox, entry, TRUE, TRUE, 0);

  if(which_way)
    {
      GtkWidget *option_menu;

      /* Add special toggle for spiral */
      option_menu = gimp_option_menu_new ();
      g_object_set_data (G_OBJECT (option_menu), "which_way", which_way);

      gimp_option_menu_append (option_menu, "Clockwise",
			       G_CALLBACK (spiral_dir_callback),
			       GINT_TO_POINTER (0));
      gimp_option_menu_append (option_menu, "Anti-Clockwise",
			       G_CALLBACK (spiral_dir_callback),
			       GINT_TO_POINTER (1));
      gimp_option_menu_set_history (option_menu, *which_way ? 1 : 0);
      gimp_box_pack_start (hbox, option_menu,TRUE,TRUE,0);
    }

  gimp_box_pack_start (gimp_dialog_get_vbox (window), hbox, TRUE, TRUE, 0);
  gtk_window_present (GTK_WINDOW (window));
}

static void
bezier_dialog (void)
{
  GtkWidget *window = NULL;
  GtkWidget *vbox;
  GtkWidget *toggle;

  window = gimp_dialog_new ("Bezier settings");
  if (gfig_top_level)
    gtk_window_set_transient_for (GTK_WINDOW (window),
				  GTK_WINDOW (gfig_top_level));

  gimp_dialog_add_button (window, "Close", G_CALLBACK (ok_warn_window),
			  window, TRUE);

  vbox = gimp_vbox_new(FALSE, 0);
  gimp_container_set_border_width (vbox, 4);

  toggle = gtk_check_button_new_with_label ("Closed");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle),bezier_closed);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (gfig_toggle_update),
		    (gpointer)&bezier_closed);
  gfig_set_tooltip(toggle,"Close curve on completion");
  gimp_box_pack_start (vbox, toggle, TRUE, TRUE, 0);

  toggle = gtk_check_button_new_with_label ("Show line frame");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle),bezier_line_frame);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (gfig_toggle_update),
		    (gpointer)&bezier_line_frame);
  gfig_set_tooltip(toggle,"Draws lines between the control points. Only during curve creation");
  gimp_box_pack_start (vbox, toggle, TRUE, TRUE, 0);

  gimp_box_pack_start (gimp_dialog_get_vbox (window), vbox, TRUE, TRUE, 0);
  gtk_window_present (GTK_WINDOW (window));
}

/* Double clicks on the object buttons open their options */
static void
poly_button_press (GtkGestureClick *gesture,
		   gint             n_press,
		   gdouble          x,
		   gdouble          y,
		   gpointer         data)
{
  if (n_press == 2)
    num_sides_dialog("Regular polygon number of sides",&poly_num_sides,NULL,3,200);
}

static void
star_button_press (GtkGestureClick *gesture,
		   gint             n_press,
		   gdouble          x,
		   gdouble          y,
		   gpointer         data)
{
  if (n_press == 2)
    num_sides_dialog("Star number of points",&star_num_sides,NULL,3,200);
}

static void
spiral_button_press (GtkGestureClick *gesture,
		     gint             n_press,
		     gdouble          x,
		     gdouble          y,
		     gpointer         data)
{
  if (n_press == 2)
    num_sides_dialog("Spiral number of points",&spiral_num_turns,&spiral_toggle,1,20);
}

static void
bezier_button_press (GtkGestureClick *gesture,
		     gint             n_press,
		     gdouble          x,
		     gdouble          y,
		     gpointer         data)
{
  if (n_press == 2)
    bezier_dialog();
}

/* Button 1 presses on widget, seen before the widget handles them */
static void
gfig_add_press_handler (GtkWidget *widget,
			GCallback  callback)
{
  GtkGesture *gesture;

  gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 1);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture),
					      GTK_PHASE_CAPTURE);
  g_signal_connect (gesture, "pressed", callback, NULL);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (gesture));
}

static GtkWidget *
draw_buttons(GtkWidget *ww)
{
  GtkWidget * frame;
  GtkWidget * button;
  GtkWidget * vbox;
  GtkWidget * group;

  frame = gtk_frame_new ("Ops");
  gimp_container_set_border_width (frame, 1);

  /* Create group */
  group = NULL;
  vbox = gimp_vbox_new(FALSE, 0);
  gtk_frame_set_child (GTK_FRAME (frame), vbox);
  gimp_container_set_border_width (vbox, 2);

  /* Put buttons in */
  button = but_with_pix(line_xpm,&group,LINE);
  gimp_box_pack_start (vbox, button, TRUE, TRUE, 0);
  gfig_set_tooltip(button,"Create line");

  button = but_with_pix(circle_xpm,&group,CIRCLE);
  gimp_box_pack_start (vbox, button, TRUE, TRUE, 0);
  gfig_set_tooltip(button,"Create circle");

  button = but_with_pix(ellipse_xpm,&group,ELLIPSE);
  gimp_box_pack_start (vbox, button, TRUE, TRUE, 0);
  gfig_set_tooltip(button,"Create ellipse");

  button = but_with_pix(curve_xpm,&group,ARC);
  gimp_box_pack_start (vbox, button, TRUE, TRUE, 0);
  gfig_set_tooltip(button,"Create arch");

  button = but_with_pix(poly_xpm,&group,POLY);
  gimp_box_pack_start (vbox, button, TRUE, TRUE, 0);
  gfig_add_press_handler (button, G_CALLBACK (poly_button_press));
  gfig_set_tooltip(button,"Create reg polygon");

  button = but_with_pix(star_xpm,&group,STAR);
  gimp_box_pack_start (vbox, button, TRUE, TRUE, 0);
  gfig_add_press_handler (button, G_CALLBACK (star_button_press));
  gfig_set_tooltip(button,"Create star");

  button = but_with_pix(spiral_xpm,&group,SPIRAL);
  gimp_box_pack_start (vbox, button, TRUE, TRUE, 0);
  gfig_add_press_handler (button, G_CALLBACK (spiral_button_press));
  gfig_set_tooltip(button,"Create spiral");

  button = but_with_pix(bezier_xpm,&group,BEZIER);
  gimp_box_pack_start (vbox, button, TRUE, TRUE, 0);
  gfig_add_press_handler (button, G_CALLBACK (bezier_button_press));
  gfig_set_tooltip(button,"Create bezier curve. Shift + Button ends object creation.");

  button = but_with_pix(move_obj_xpm,&group,MOVE_OBJ);
  gimp_box_pack_start (vbox, button, TRUE, TRUE, 0);
  gfig_set_tooltip(button,"Move an object");

  button = but_with_pix(move_point_xpm,&group,MOVE_POINT);
  gimp_box_pack_start (vbox, button, TRUE, TRUE, 0);
  gfig_set_tooltip(button,"Move a single point");

  button = but_with_pix(copy_obj_xpm,&group,COPY_OBJ);
  gimp_box_pack_start (vbox, button, TRUE, TRUE, 0);
  gfig_set_tooltip(button,"Copy an object");

  button = but_with_pix(delete_xpm,&group,DEL_OBJ);
  gimp_box_pack_start (vbox, button, TRUE, TRUE, 0);
  gfig_set_tooltip(button,"Delete an object");

  button = obj_select_buttons();
  gimp_box_pack_start (vbox, button, TRUE, TRUE, 0);

  return(frame);
}

/* Brush preview stuff */
static void brush_list_select (BRUSHDESC *bdesc);

/* Dragging in the brush preview scrolls the brush */
static void
gfig_brush_preview_drag_update (GtkGestureDrag *gesture,
				gdouble         offset_x,
				gdouble         offset_y,
				gpointer        data)
{
  GtkWidget *widget;
  gint *last = data; /* last offset x,y */
  gint  ox = (gint) offset_x;
  gint  oy = (gint) offset_y;

  widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));

  if (!g_object_get_data (G_OBJECT (widget), "user_data"))
    return;

  gfig_brush_fill_preview_xy(widget,last[0] - ox,last[1] - oy);
  last[0] = ox;
  last[1] = oy;
}

static void
gfig_brush_preview_drag_begin (GtkGestureDrag *gesture,
			       gdouble         x,
			       gdouble         y,
			       gpointer        data)
{
  gint *last = data;

  last[0] = last[1] = 0;
}

static void
gfig_brush_update_preview(GtkWidget *widget,gpointer data)
{
  GtkWidget *pw = (GtkWidget*)data;
  BRUSHDESC *bdesc;

  /* Must update the dialog area */
  /* Use the same brush as already set in the dialog */
  bdesc = g_object_get_data (G_OBJECT (pw), "user_data");
  if (bdesc)
    brush_list_select(bdesc);
}

static void
gfig_brush_menu_callback(GtkWidget *widget, gpointer data)
{
  BRUSH_TYPE btype = (BRUSH_TYPE)GPOINTER_TO_INT(data);

  switch(btype)
    {
    case BRUSH_BRUSH_TYPE:
      selvals.brshtype = btype;
      gtk_widget_set_visible(pressure_hbox, FALSE);
      gtk_widget_set_visible(pencil_hbox, FALSE);
      gtk_widget_set_visible(fade_out_hbox, TRUE);
      break;
    case BRUSH_PENCIL_TYPE:
      selvals.brshtype = btype;
      gtk_widget_set_visible(fade_out_hbox, FALSE);
      gtk_widget_set_visible(pressure_hbox, FALSE);
      gtk_widget_set_visible(pencil_hbox, TRUE);
      break;
    case BRUSH_AIRBRUSH_TYPE:
      selvals.brshtype = btype;
      gtk_widget_set_visible(fade_out_hbox, FALSE);
      gtk_widget_set_visible(pencil_hbox, FALSE);
      gtk_widget_set_visible(pressure_hbox, TRUE);
      break;
    case BRUSH_PATTERN_TYPE:
      selvals.brshtype = btype;
      gtk_widget_set_visible(fade_out_hbox, FALSE);
      gtk_widget_set_visible(pressure_hbox, FALSE);
      gtk_widget_set_visible(pencil_hbox, TRUE);
      break;
    default:
      create_warn_dialog("Internal error - invalid brush type");
      break;
    }
  gfig_brush_update_preview(widget,
			    (gpointer)g_object_get_data (G_OBJECT (widget), "user_data"));
}


static GtkWidget *
gfig_brush_preview(GtkWidget **pv)
{
  static gint drag_last[2];
  GtkWidget *option_menu;
  GtkGesture *drag;
  GtkWidget * frame;
  GtkWidget * hbox;
  GtkWidget * vbox;
  /* Returns a new preview widget for a brush */

  hbox = gimp_hbox_new(FALSE, 0);
  gimp_container_set_border_width (hbox, 4);

  frame = gtk_frame_new (NULL);
  gtk_widget_set_valign (frame, GTK_ALIGN_CENTER);

  *pv = gimp_preview_new(GIMP_PREVIEW_COLOR);
  gimp_preview_size(GIMP_PREVIEW(*pv), BRUSH_PREVIEW_SZ, BRUSH_PREVIEW_SZ);
  gtk_frame_set_child (GTK_FRAME (frame), *pv);

  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), 1);
  g_signal_connect (drag, "drag-begin",
		    G_CALLBACK (gfig_brush_preview_drag_begin), drag_last);
  g_signal_connect (drag, "drag-update",
		    G_CALLBACK (gfig_brush_preview_drag_update), drag_last);
  gtk_widget_add_controller (*pv, GTK_EVENT_CONTROLLER (drag));

  /* Fill with white */
  gimp_preview_fill (GIMP_PREVIEW (*pv), 255, 255, 255);

  /* Now the buttons */
  vbox = gimp_vbox_new(FALSE, 0);
  gimp_container_set_border_width (vbox, 4);

  option_menu = gimp_option_menu_new ();
  g_object_set_data (G_OBJECT (option_menu), "user_data", (gpointer) *pv);

  gimp_option_menu_append (option_menu, "Brush",
			   G_CALLBACK (gfig_brush_menu_callback),
			   GINT_TO_POINTER (BRUSH_BRUSH_TYPE));
  gimp_option_menu_append (option_menu, "Airbrush",
			   G_CALLBACK (gfig_brush_menu_callback),
			   GINT_TO_POINTER (BRUSH_AIRBRUSH_TYPE));
  gimp_option_menu_append (option_menu, "Pencil",
			   G_CALLBACK (gfig_brush_menu_callback),
			   GINT_TO_POINTER (BRUSH_PENCIL_TYPE));
  gimp_option_menu_append (option_menu, "Pattern",
			   G_CALLBACK (gfig_brush_menu_callback),
			   GINT_TO_POINTER (BRUSH_PATTERN_TYPE));

  gtk_widget_set_valign (option_menu, GTK_ALIGN_CENTER);
  gimp_box_pack_start (vbox, option_menu, TRUE, FALSE, 0);
  gfig_set_tooltip(option_menu,"Use the brush/pencil or the airbrush when drawing on the image. Pattern paints with currently selected brush with a pattern. Only applies to circles/ellipses if Approx. Circles/Ellipses toggle is set.");

  gimp_box_pack_start (hbox, vbox, TRUE, TRUE, 0);
  gimp_box_pack_start (hbox, frame, TRUE, TRUE, 0);

  return(hbox);
}

static void
gfig_brush_fill_preview_xy(GtkWidget *pw,gint x1 ,gint y1)
{
  gint row_count;
  BRUSHDESC *bdesc = (BRUSHDESC*)g_object_get_data (G_OBJECT (pw), "user_data");

  /* Adjust start position */
  bdesc->x_off += x1;
  bdesc->y_off += y1;

  if(bdesc->y_off < 0)
    bdesc->y_off = 0;
  if(bdesc->y_off > (bdesc->height - BRUSH_PREVIEW_SZ))
    bdesc->y_off = bdesc->height - BRUSH_PREVIEW_SZ;

  if(bdesc->x_off < 0)
    bdesc->x_off = 0;
  if(bdesc->x_off > (bdesc->width - BRUSH_PREVIEW_SZ))
    bdesc->x_off = bdesc->width - BRUSH_PREVIEW_SZ;

  /* Given an x and y fill preview in correctly offsetted */
  for(row_count = 0; row_count < BRUSH_PREVIEW_SZ; row_count++)
    gimp_preview_draw_row(GIMP_PREVIEW(pw),
			  &bdesc->pv_buf[bdesc->x_off*bdesc->bpp
					 + (bdesc->width
					    *bdesc->bpp
					    *(row_count + bdesc->y_off))],
			  0,
			  row_count,
			  BRUSH_PREVIEW_SZ);
}

static void
gfig_brush_fill_preview(GtkWidget *pw,gint32 layer_ID, BRUSHDESC * bdesc)
{
  GPixelRgn src_rgn;
  GDrawable *brushdrawable;
  gint bcount = 3;

  if(bdesc->pv_buf)
    {
      g_free(bdesc->pv_buf); /* Free old area */
    }

  brushdrawable = gimp_drawable_get(layer_ID);

  bdesc->bpp = bcount;
  
  /* Fill the preview with the current brush name */
  gimp_pixel_rgn_init(&src_rgn,brushdrawable,0,0,bdesc->width,bdesc->height,FALSE,FALSE);
  
  bdesc->pv_buf = g_new(guchar ,bdesc->width*bdesc->height*bcount);
  bdesc->x_off = bdesc->y_off = 0; /* Start from top left */

  gimp_pixel_rgn_get_rect(&src_rgn,bdesc->pv_buf,0,0,bdesc->width,bdesc->height);

  /* Dump the pv_buf into the preview area */
  gfig_brush_fill_preview_xy(pw,0,0);
}

void
mygimp_brush_set(gchar *bname)
{
  GParam *return_vals;
  int nreturn_vals;

  return_vals = gimp_run_procedure ("gimp_brushes_set_brush",
				    &nreturn_vals,
				    PARAM_STRING, bname,
				    PARAM_END);

  if (return_vals[0].data.d_status != STATUS_SUCCESS)
    {
      create_warn_dialog("Can't set brush...(1)");
    }

  gimp_destroy_params (return_vals, nreturn_vals);
}

static gchar *
mygimp_brush_get (void)
{
  GParam *return_vals;
  int nreturn_vals;
  static gchar saved_bname[1024]; /* required to be static - returned from proc */

  return_vals = gimp_run_procedure ("gimp_brushes_get_brush",
                                    &nreturn_vals,
				    PARAM_END);

  if (return_vals[0].data.d_status == STATUS_SUCCESS)
    {
      g_strlcpy(saved_bname,return_vals[1].data.d_string,sizeof(saved_bname));
    }
  else
    {
      saved_bname[0] = '\0';
    }

  gimp_destroy_params (return_vals, nreturn_vals);

  return(saved_bname);
}

static void
mygimp_brush_info(gint32 *width,
		  gint32 *height)
{
  GParam *return_vals;
  int nreturn_vals;
 
  return_vals = gimp_run_procedure ("gimp_brushes_get_brush",
                                    &nreturn_vals,
				    PARAM_END);

  if (return_vals[0].data.d_status == STATUS_SUCCESS)
    {
      *width = MAX(return_vals[2].data.d_int32,32);
      *height = MAX(return_vals[3].data.d_int32,32);
    }
  else
    {
      create_warn_dialog("Failed to get brush info");
      *width = *height = 48;
    }

  gimp_destroy_params (return_vals, nreturn_vals);
}          

static void
gfig_brush_img_del(void)
{
  gimp_image_delete(brush_image_ID);
  brush_image_ID = -1;
}

static gint32
gfig_gen_brush_preview(BRUSHDESC *bdesc)
{
  /* Given the name of a brush then paint it and return the ID of the image 
   * the preview can be got from
   */
  GParam *return_vals = NULL;
  int nreturn_vals;
  static gint32 layer_ID = -1;
  guchar fR,fG,fB;
  guchar bR,bG,bB;
  gchar *saved_bname;
  gint32 width,height;
  gdouble line_pnts[2];

  if(brush_image_ID == -1)
    {
      /* Create a new image */
      brush_image_ID = gimp_image_new(48,48,0);
      if(brush_image_ID < 0)
	{
	  create_warn_dialog("Failed to generate brush preview");
	  return(-1);
	}
      if((layer_ID = gimp_layer_new(brush_image_ID,
				    "Brush preview",
				    48,
				    48,
				    0, /* RGB type */
				    100.0, /* opacity */
				    0 /* mode */)) < 0)
	{
	  create_warn_dialog("Error in creating layer for brush preview\n");
	  return(-1);
	}
      gimp_image_add_layer(brush_image_ID,layer_ID,-1);
    }

  /* Need this later to delete it */

  /* Store foreground & backgroud colours set to black/white
   * paint with brush
   * restore colours
   */
  
  gimp_palette_get_foreground(&fR,&fG,&fB);
  gimp_palette_get_background(&bR,&bG,&bB);
  saved_bname = mygimp_brush_get();

  gimp_palette_set_background((guchar)-1,(guchar)-1,(guchar)-1);
  gimp_palette_set_foreground(0,0,0);
  mygimp_brush_set(bdesc->bname);

  mygimp_brush_info(&width,&height);
  bdesc->width = width;
  bdesc->height = height;
  line_pnts[0] = (gdouble)width/2;
  line_pnts[1] = (gdouble)height/2;

  gimp_layer_resize(layer_ID,width,height,0,0);
  gimp_image_resize(brush_image_ID,width,height,0,0);

  gimp_drawable_fill(layer_ID,1); /* Clear... Fill with white ... */

  /* Blob of paint */

  switch(selvals.brshtype)
	 {
	 case BRUSH_BRUSH_TYPE:
	   return_vals = gimp_run_procedure ("gimp_paintbrush", &nreturn_vals,
					     PARAM_IMAGE, brush_image_ID,
					     PARAM_DRAWABLE, layer_ID,
					     PARAM_FLOAT,0.0,
					     PARAM_INT32,2*GFIG_LCC,/* GIMP BUG should be 2!!!!*/
					     PARAM_FLOATARRAY, &line_pnts[0],  
					     PARAM_END);
	   break;
	 case BRUSH_PENCIL_TYPE:
	   return_vals = gimp_run_procedure ("gimp_pencil", &nreturn_vals,
					     PARAM_IMAGE, brush_image_ID,
					     PARAM_DRAWABLE, layer_ID,
					     PARAM_INT32,2,
					     PARAM_FLOATARRAY, &line_pnts[0],  
					     PARAM_END);
	   break;
	 case BRUSH_AIRBRUSH_TYPE:
	   return_vals = gimp_run_procedure ("gimp_airbrush", &nreturn_vals,
					     PARAM_IMAGE, brush_image_ID,
					     PARAM_DRAWABLE, layer_ID,
					     PARAM_FLOAT,(gdouble)selvals.airbrushpressure,
					     PARAM_INT32,2,
					     PARAM_FLOATARRAY, &line_pnts[0],  
					     PARAM_END);
	   break;
	 case BRUSH_PATTERN_TYPE:
	   return_vals = gimp_run_procedure ("gimp_clone", &nreturn_vals,
					     PARAM_IMAGE, brush_image_ID,
					     PARAM_DRAWABLE, layer_ID,
					     PARAM_DRAWABLE, layer_ID,
					     PARAM_INT32, 1,
					     PARAM_FLOAT,(gdouble)0.0,
					     PARAM_FLOAT,(gdouble)0.0,
					     PARAM_INT32,2,
					     PARAM_FLOATARRAY, &line_pnts[0],  
					     PARAM_END);
	   break;
	 default:
	   break;
	 }  

  gimp_destroy_params (return_vals, nreturn_vals);

  gimp_palette_set_background(bR,bG,bB);  
  gimp_palette_set_foreground(fR,fG,fB);
  mygimp_brush_set(saved_bname);

  return(layer_ID);
}

static void
brush_list_select (BRUSHDESC *bdesc)
{
  gint32 layer_ID;

  if((layer_ID = gfig_gen_brush_preview(bdesc)) != -1)
    {
      g_object_set_data (G_OBJECT (brush_page_pw), "user_data", (gpointer)bdesc);
      gfig_brush_fill_preview(brush_page_pw,layer_ID,bdesc);
    }
}

static void
brush_list_row_activated (GtkListBox    *list,
			  GtkListBoxRow *row,
			  gpointer       data)
{
  BRUSHDESC *bdesc = g_object_get_data (G_OBJECT (row), "user_data");

  if (bdesc)
    brush_list_select (bdesc);
}

/* Build the dialog up. This was the hard part! */


static GtkWidget *page_menu_bg;
static GtkWidget *page_menu_layers;

/* The option menus of the paint page: the menu type is set on the
 * option menu as "menu_type", data is the value of the chosen item.
 */
static void
paint_menu_callback (GtkWidget *widget, gpointer data)
{
  gint mtype = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget),
						   "menu_type"));
  gint value = GPOINTER_TO_INT (data);

  if(mtype == PAINT_LAYERS_MENU)
    {
#ifdef DEBUG
      printf("layer type set to %s\n",
	     ((DRAWONLAYERS)value == SINGLE_LAYER)?"SINGLE_LAYER":"MULTI_LAYER");
#endif /* DEBUG */
      selvals.onlayers = (DRAWONLAYERS)value;
      /* Type only meaningful if creating new layers */
      if(selvals.onlayers == ORIGINAL_LAYER)
	gtk_widget_set_sensitive(page_menu_bg,FALSE);
      else
	gtk_widget_set_sensitive(page_menu_bg,TRUE);
    }
  else if(mtype == PAINT_BGS_MENU)
    {
#ifdef DEBUG
      printf("BG type = %d\n", value);
#endif /* DEBUG */
      selvals.onlayerbg = (DRAWLAYERBG)value;
    }
  else if(mtype == PAINT_TYPE_MENU)
    {
#ifdef DEBUG
      printf("Got type menu = %d\n", value);
#endif /* DEBUG */
      selvals.painttype = (PAINTTYPE)value;
      switch(selvals.painttype)
	{
	case PAINT_BRUSH_TYPE:
	  gtk_widget_set_sensitive(select_page_widget,FALSE);
	  gtk_widget_set_sensitive(brush_page_widget,TRUE);
	  gtk_widget_set_sensitive(page_menu_layers,TRUE);
	  if(selvals.onlayers == ORIGINAL_LAYER)
	    gtk_widget_set_sensitive(page_menu_bg,FALSE);
	  else
	    gtk_widget_set_sensitive(page_menu_bg,TRUE);
	  break;
	case PAINT_SELECTION_TYPE:
	  gtk_widget_set_sensitive(select_page_widget,TRUE);
	  gtk_widget_set_sensitive(brush_page_widget,FALSE);
	  gtk_widget_set_sensitive(page_menu_layers,FALSE);
	  gtk_widget_set_sensitive(page_menu_bg,FALSE);
	  break;
	case PAINT_SELECTION_FILL_TYPE:
	  gtk_widget_set_sensitive(select_page_widget,TRUE);
	  gtk_widget_set_sensitive(brush_page_widget,FALSE);
	  gtk_widget_set_sensitive(page_menu_layers,TRUE);
	  if(selvals.onlayers == ORIGINAL_LAYER)
	    gtk_widget_set_sensitive(page_menu_bg,FALSE);
	  else
	    gtk_widget_set_sensitive(page_menu_bg,TRUE);
	  break;
	default:
	  break;
	}
    }
}

/* An option menu whose items all go to paint_menu_callback */
static GtkWidget *
paint_page_option_menu (gint           mtype,
			const gchar  **labels,
			const gint    *values,
			gint           n_items)
{
  GtkWidget *option_menu;
  gint       i;

  option_menu = gimp_option_menu_new ();
  g_object_set_data (G_OBJECT (option_menu), "menu_type",
		     GINT_TO_POINTER (mtype));

  for (i = 0; i < n_items; i++)
    gimp_option_menu_append (option_menu, labels[i],
			     G_CALLBACK (paint_menu_callback),
			     GINT_TO_POINTER (values[i]));

  return option_menu;
}

static GtkWidget *
paint_page_menu_bgs(void)
{
  static const gchar *labels[] = { "Transparent", "Background", "White", "Copy" };
  static const gint   values[] = { LAYER_TRANS_BG, LAYER_BG_BG,
				   LAYER_WHITE_BG, LAYER_COPY_BG };

  return paint_page_option_menu (PAINT_BGS_MENU, labels, values,
				 G_N_ELEMENTS (labels));
}


static GtkWidget *
paint_page_menu_type(void)
{
  static const gchar *labels[] = { "Brush", "Selection", "Selection+Fill" };
  static const gint   values[] = { PAINT_BRUSH_TYPE, PAINT_SELECTION_TYPE,
				   PAINT_SELECTION_FILL_TYPE };

  return paint_page_option_menu (PAINT_TYPE_MENU, labels, values,
				 G_N_ELEMENTS (labels));
}

static GtkWidget *
paint_page_menu_layers(void)
{
  static const gchar *labels[] = { "Original", "New", "Multiple" };
  static const gint   values[] = { ORIGINAL_LAYER, SINGLE_LAYER, MULTI_LAYER };

  return paint_page_option_menu (PAINT_LAYERS_MENU, labels, values,
				 G_N_ELEMENTS (labels));
}

static GtkWidget *
paint_page()
{
  GtkWidget *table;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *toggle;
  GtkWidget *page_menu_type;
  GtkWidget *scale_scale;
  GtkAdjustment *scale_scale_data;

  vbox = gimp_vbox_new(FALSE, 0);
  gimp_container_set_border_width (vbox, 4);

  table = gimp_table_new (6, 6, FALSE);
  gtk_grid_set_row_spacing (GTK_GRID (table),6);
  gimp_container_set_border_width (table, 1);
  gimp_box_pack_start (vbox, table, TRUE, TRUE, 0);

  /* Put buttons in */
  label = gtk_label_new ("Using:-");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0);

  page_menu_type = paint_page_menu_type();
  gfig_set_tooltip(page_menu_type,"Draw type. Either a brush or a selection. See brush page or selection page for more options");
  /* Default is original */
  gimp_table_attach (table, page_menu_type, 1, 2, 1, 2, GIMP_FILL , GIMP_FILL, 0, 0);

  label = gtk_label_new ("draw on:-");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 2, 3, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0);
  page_menu_layers = paint_page_menu_layers();
  gfig_set_tooltip(page_menu_layers,"Draw all objects on one layer (original or new) or one object per layer");
  gimp_table_attach (table, page_menu_layers, 3, 4, 1, 2, GIMP_FILL , GIMP_FILL, 0, 0);
  if(gimp_drawable_channel(gfig_drawable))
      gtk_widget_set_sensitive(page_menu_layers,FALSE);


  label = gtk_label_new ("with BG of:-");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 2, 3, GIMP_FILL, GIMP_FILL, 0, 0);

  page_menu_bg = paint_page_menu_bgs();
  gfig_set_tooltip(page_menu_bg,"Layer background type. Copy causes previous layer to be copied before the draw is performed");
  /* Default is original */
  gtk_widget_set_sensitive(page_menu_bg,FALSE);
  gimp_table_attach (table, page_menu_bg, 1, 2, 2, 3, GIMP_FILL , GIMP_FILL, 0, 0);


  toggle = gtk_check_button_new_with_label ("Reverse line ");
  gimp_table_attach (table, toggle, 0, 1, 3, 4, GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (gfig_toggle_update),
		    (gpointer)&selvals.reverselines);
  gfig_set_tooltip(toggle,"Draw lines in reverse order");

  toggle = gtk_check_button_new_with_label ("Scale to image ");
  gimp_table_attach (table, toggle, 1, 2, 3, 4, GIMP_FILL, GIMP_FILL, 0, 0);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle),selvals.scaletoimage);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (gfig_scale2img_update),
		    (gpointer)&selvals.scaletoimage);
  gfig_set_tooltip(toggle,"Scale drawings to images size");

  hbox = gimp_hbox_new (FALSE, 1);
  scale_scale_data = gtk_adjustment_new (1.0, 0.1, 5.0, 0.01, 0.01, 0.0);
  scale_scale = gimp_hscale_new (scale_scale_data, 2);
  gimp_box_pack_start (hbox, scale_scale, TRUE, TRUE, 0);
  g_signal_connect (scale_scale_data, "value-changed",
		    G_CALLBACK (gfig_scale_update_scale),
		    &selvals.scaletoimagefp);

  gimp_table_attach (table, hbox, 2, 4, 3, 4, GIMP_FILL|GIMP_EXPAND , GIMP_FILL, 0, 0);
  gtk_widget_set_sensitive(GTK_WIDGET(scale_scale),FALSE);
  g_object_set_data (G_OBJECT (toggle), "user_data", (gpointer)scale_scale_data);
  g_object_set_data (G_OBJECT (scale_scale_data), "user_data", (gpointer)scale_scale);

  toggle = gtk_check_button_new_with_label ("Approx. Circles/Ellipses ");
  gimp_table_attach (table, toggle, 0, 2, 4, 5, GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (gfig_toggle_update),
		    (gpointer)&selvals.approxcircles);
  gfig_set_tooltip(toggle,"Approx. circles & ellipses using lines. Allows the use of brush fading with these types of objects.");

  return(vbox);
}

static void
gfig_get_brushes(GtkWidget *list)
{
  GtkWidget *list_item;
  GtkWidget *label;
  GtkListBoxRow *row;
  gint list_item2sel = 0;
  gint nreturn_vals;
  GParam *return_vals;
  gint num_brs;
  gchar **brush_names;
  gchar *current_bname;
  gint i;
  BRUSHDESC *fbdesc = (BRUSHDESC *)-1;

  current_bname = mygimp_brush_get();

  return_vals = gimp_run_procedure ("gimp_brushes_list", &nreturn_vals,
				    PARAM_END);

  if(return_vals[0].data.d_status != STATUS_SUCCESS)
    {
      create_warn_dialog("No brushes to select");
      return;
    }

  num_brs = return_vals[1].data.d_int32;

  brush_names = return_vals[2].data.d_stringarray;

  for( i = 0 ; i < num_brs; i++)
    {
      BRUSHDESC *bdesc = g_malloc0(sizeof(BRUSHDESC));
      bdesc->bpp = 3;

      list_item = gtk_list_box_row_new ();
      label = gtk_label_new (brush_names[i]);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (list_item), label);
      gtk_list_box_append (GTK_LIST_BOX (list), list_item);
      bdesc->bname = g_strdup(brush_names[i]);
      g_object_set_data (G_OBJECT (list_item), "user_data", bdesc);

      if(!strcmp(brush_names[i],current_bname))
	{
	  fbdesc = bdesc;
	  list_item2sel = i;
	}
    }

  g_signal_connect (list, "row-activated",
		    G_CALLBACK (brush_list_row_activated), NULL);

  if(fbdesc != (BRUSHDESC*)-1)
    {
      /* First item selected by default - unselected it ! */
      brush_list_select(fbdesc);
    }

  gimp_destroy_params (return_vals, nreturn_vals);
  row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (list), list_item2sel);
  if (row)
    gtk_list_box_select_row (GTK_LIST_BOX (list), row);
}



/* A horizontal scale with its value drawn at pos */
static GtkWidget *
gfig_hscale_new (GtkAdjustment   *adjustment,
		 gint             digits,
		 GtkPositionType  pos)
{
  GtkWidget *scale;

  scale = gimp_hscale_new (adjustment, digits);
  gtk_scale_set_value_pos (GTK_SCALE (scale), pos);

  return scale;
}

static GtkWidget *
brush_page()
{
  GtkWidget *list_frame;
  GtkWidget *list;
  GtkWidget *scrolled_win;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *pw;
  GtkWidget *scale;
  GtkAdjustment *fade_out_scale_data;
  GtkAdjustment *pressure_scale_data;
  GtkWidget *vbox;

  vbox = gimp_vbox_new(FALSE, 0);
  gimp_container_set_border_width (vbox, 4);

  table = gimp_table_new (6, 6, FALSE);
  gtk_grid_set_row_spacing (GTK_GRID (table),6);
  gimp_container_set_border_width (table, 1);
  gimp_box_pack_start (vbox, table, TRUE, TRUE, 0);

  /* Fade option */
 /*  the fade-out scale  From GIMP itself*/
  fade_out_hbox = gimp_hbox_new (FALSE, 1);

  label = gtk_label_new ("Fade Out:");
  gimp_box_pack_start (fade_out_hbox, label, FALSE, FALSE, 0);

  fade_out_scale_data = gtk_adjustment_new (0.0, 0.0, 3000.0, 1.0, 1.0, 0.0);
  scale = gimp_hscale_new (fade_out_scale_data, 1);
  gimp_box_pack_start (fade_out_hbox, scale, TRUE, TRUE, 0);
  g_signal_connect (fade_out_scale_data, "value-changed",
		    G_CALLBACK (gfig_scale_update_fp),
		    &selvals.brushfade);
  gimp_table_attach (table, fade_out_hbox, 1, 2, 0, 1, GIMP_FILL|GIMP_EXPAND , GIMP_FILL, 0, 0);

  pressure_hbox = gimp_hbox_new (FALSE, 1);
  label = gtk_label_new ("Pressure:");
  gimp_box_pack_start (pressure_hbox, label, FALSE, FALSE, 0);

  pressure_scale_data = gtk_adjustment_new (20.0, 0.0, 100.0, 1.0, 1.0, 0.0);
  scale = gimp_hscale_new (pressure_scale_data, 1);
  gimp_box_pack_start (pressure_hbox, scale, TRUE, TRUE, 0);
  g_signal_connect (pressure_scale_data, "value-changed",
		    G_CALLBACK (gfig_scale_update_fp),
		    &selvals.airbrushpressure);
  gimp_table_attach (table, pressure_hbox, 1, 2, 0, 1, GIMP_FILL|GIMP_EXPAND , GIMP_FILL, 0, 0);
  gtk_widget_set_visible (pressure_hbox, FALSE);

  pencil_hbox = gimp_hbox_new (FALSE, 1);
  label = gtk_label_new ("No options...");
  gimp_box_pack_start (pencil_hbox, label, FALSE, FALSE, 0);
  gimp_table_attach (table, pencil_hbox, 1, 2, 0, 1,GIMP_FILL|GIMP_EXPAND, GIMP_FILL, 0, 0);
  gtk_widget_set_visible (pencil_hbox, FALSE);


  /* Preview widget */
  pw = gfig_brush_preview(&brush_page_pw);
  gimp_table_attach (table, pw, 0, 1, 0, 1, 0, 0, 0, 0);

  g_signal_connect (pressure_scale_data, "value-changed",
		    G_CALLBACK (gfig_brush_update_preview),
		    (gpointer)brush_page_pw);

  /* Brush list */
  list_frame = gtk_frame_new(NULL);

  scrolled_win = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_win),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scrolled_win, -1, 100);
  gtk_frame_set_child (GTK_FRAME (list_frame), scrolled_win);

  list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_SINGLE);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_win), list);
  gimp_table_attach (table, list_frame, 0, 4, 1, 5, GIMP_FILL|GIMP_EXPAND , GIMP_FILL|GIMP_EXPAND, 0, 0);

  /* Get brush list and insert in table */
  gfig_get_brushes(list);

  return(vbox);
}

/* The option menus of the select page: the menu type is set on the
 * option menu as "menu_type", data is the value of the chosen item.
 */
static void
select_menu_callback (GtkWidget *widget, gpointer data)
{
  gint mtype = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget),
						   "menu_type"));
  gint value = GPOINTER_TO_INT (data);

  if(mtype == SELECT_TYPE_MENU)
    {
      selopt.type = (SELECTION_TYPE)value;
    }
  else if(mtype == SELECT_ARCTYPE_MENU)
    {
      selopt.as_pie = (ARC_TYPE)value;
    }
  else if(mtype == SELECT_TYPE_MENU_FILL)
    {
      selopt.fill_type = (FILL_TYPE)value;
    }
  else if(mtype == SELECT_TYPE_MENU_WHEN)
    {
      selopt.fill_when = (FILL_WHEN)value;
    }
}

static GtkWidget *
select_page_option_menu (gint           mtype,
			 const gchar  **labels,
			 const gint    *values,
			 gint           n_items)
{
  GtkWidget *option_menu;
  gint       i;

  option_menu = gimp_option_menu_new ();
  g_object_set_data (G_OBJECT (option_menu), "menu_type",
		     GINT_TO_POINTER (mtype));

  for (i = 0; i < n_items; i++)
    gimp_option_menu_append (option_menu, labels[i],
			     G_CALLBACK (select_menu_callback),
			     GINT_TO_POINTER (values[i]));

  return option_menu;
}

static GtkWidget *
select_page_menu_fill_when(void)
{
  static const gchar *labels[] = { "Each selection", "All selections" };
  static const gint   values[] = { FILL_EACH, FILL_AFTER };

  return select_page_option_menu (SELECT_TYPE_MENU_WHEN, labels, values,
				  G_N_ELEMENTS (labels));
}



static GtkWidget *
select_page_menu_fill_type(void)
{
  static const gchar *labels[] = { "Pattern", "Foreground", "Background" };
  static const gint   values[] = { FILL_PATTERN, FILL_FOREGROUND,
				   FILL_BACKGROUND };

  return select_page_option_menu (SELECT_TYPE_MENU_FILL, labels, values,
				  G_N_ELEMENTS (labels));
}


static GtkWidget *
select_page_menu_type(void)
{
  static const gchar *labels[] = { "Add", "Sub", "Replace", "Intersect" };
  static const gint   values[] = { ADD, SUBTRACT, REPLACE, INTERSECT };

  return select_page_option_menu (SELECT_TYPE_MENU, labels, values,
				  G_N_ELEMENTS (labels));
}

static GtkWidget *
select_page_menu_arctype(void)
{
  static const gchar *labels[] = { "Segment", "Sector" };
  static const gint   values[] = { ARC_SEGMENT, ARC_SECTOR };

  return select_page_option_menu (SELECT_ARCTYPE_MENU, labels, values,
				  G_N_ELEMENTS (labels));
}


static GtkWidget *
select_page()
{
  GtkWidget *menu;
  GtkWidget *label;
  GtkWidget *toggle;
  GtkWidget *hbox;
  GtkWidget *scale;
  GtkAdjustment *scale_data;
  GtkWidget *table;
  GtkWidget *vbox;

  vbox = gimp_vbox_new(FALSE, 0);
  gimp_container_set_border_width (vbox, 4);

  table = gimp_table_new (7, 7, FALSE);
  gtk_grid_set_row_spacing (GTK_GRID (table),6);
  gimp_container_set_border_width (table, 1);
  gimp_box_pack_start (vbox, table, TRUE, TRUE, 0);

  /* The secltion settings -
   * 1) Type (option menu)
   * 2) Anti A (toggle)
   * 3) Feather (toggle)
   * 4) F radius (slider)
   * 5) Fill type (option menu)
   * 6) Opacity (slider)
   * 7) When to fill (toggle)
   * 8) Arc as segment/sector
   */

  /* Put widgets in */
  /* 1 */
  hbox = gimp_hbox_new (FALSE, 1);
  label = gtk_label_new ("Selection type:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);

  menu = select_page_menu_type();
  gimp_box_pack_start (hbox, menu, TRUE, TRUE, 0);
  gimp_table_attach (table, hbox, 1, 2, 0, 1, GIMP_FILL, GIMP_FILL, 0, 0);


  /* 2 */
  toggle = gtk_check_button_new_with_label ("Antialiasing");
  gimp_table_attach (table, toggle, 0, 1, 0, 1, GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (gfig_toggle_update),
		    (gpointer)&selopt.antia);

  /* 3 */
  toggle = gtk_check_button_new_with_label ("Feather");
  gimp_table_attach (table, toggle, 0, 1, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (gfig_toggle_update),
		    (gpointer)&selopt.feather);

  /* 4 */
  hbox = gimp_hbox_new (FALSE, 1);

  label = gtk_label_new ("Feather Radius:");
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);

  scale_data = gtk_adjustment_new (selopt.feather_radius, 0.0, 100.0, 1.0, 1.0, 0.0);
  scale = gimp_hscale_new (scale_data, 1);
  gimp_box_pack_start (hbox, scale, TRUE, TRUE, 0);
  g_signal_connect (scale_data, "value-changed",
		    G_CALLBACK (gfig_scale_update_fp),
		    &selopt.feather_radius);
  gimp_table_attach (table, hbox, 1, 3, 1, 2, GIMP_FILL|GIMP_EXPAND , GIMP_FILL, 0, 0);


  /* 5 */
  hbox = gimp_hbox_new (FALSE, 1);
  label = gtk_label_new ("Fill type:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);

  menu = select_page_menu_fill_type();
  gimp_box_pack_start (hbox, menu, TRUE, TRUE, 0);
  gimp_table_attach (table, hbox, 0, 1, 2, 3, GIMP_FILL, GIMP_FILL, 0, 0);

  /* 6 */
  hbox = gimp_hbox_new (FALSE, 1);

  label = gtk_label_new ("Fill Opacity:");
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);

  scale_data = gtk_adjustment_new (selopt.fill_opacity, 0.0, 100.0, 1.0, 1.0, 0.0);
  scale = gimp_hscale_new (scale_data, 1);
  gimp_box_pack_start (hbox, scale, TRUE, TRUE, 0);
  g_signal_connect (scale_data, "value-changed",
		    G_CALLBACK (gfig_scale_update_fp),
		    &selopt.fill_opacity);
  gimp_table_attach (table, hbox, 1, 3, 2, 3, GIMP_FILL|GIMP_EXPAND , GIMP_FILL, 0, 0);


  /* 7 */
  hbox = gimp_hbox_new (FALSE, 1);
  label = gtk_label_new ("Fill after: ");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);

  menu = select_page_menu_fill_when();
  gimp_box_pack_start (hbox, menu, TRUE, TRUE, 0);
  gimp_table_attach (table, hbox, 0, 1, 4, 5, GIMP_FILL, GIMP_FILL, 0, 0);

  /* 8 */
  hbox = gimp_hbox_new (FALSE, 1);
  label = gtk_label_new ("Arc as: ");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);

  menu = select_page_menu_arctype();
  gimp_box_pack_start (hbox, menu, TRUE, TRUE, 0);
  gimp_table_attach (table, hbox, 0, 1, 5, 6, GIMP_FILL, GIMP_FILL, 0, 0);

  return(vbox);
}

/* The grid option menus: the menu type is set on the option menu as
 * "menu_type", data is the value of the chosen item.
 */
static void
gridtype_menu_callback (GtkWidget *widget, gpointer data)
{
  gint mtype = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget),
						   "menu_type"));

  if(mtype == GRID_TYPE_MENU)
    {
#ifdef DEBUG
       printf("Gridtype set to ");
      if (current_obj->opts.gridtype == RECT_GRID)
	 printf("RECT_GRID\n");
      else if (current_obj->opts.gridtype == POLAR_GRID)
	 printf("POLAR_GRID\n");
      else if (current_obj->opts.gridtype == ISO_GRID)
	 printf("ISO_GRID\n");
      else printf("NONE\n");
#endif /* DEBUG */
      selvals.opts.gridtype = (GRIDTYPE)GPOINTER_TO_INT (data);
    }
  else
    {
      grid_gc_type = GPOINTER_TO_INT (data);
    }

  draw_grid_clear(widget,0);
}

static GtkWidget *
option_page_menu_gridtype(void)
{
  GtkWidget	*option_menu;

  gfig_opt_widget.gridtypemenu = option_menu = gimp_option_menu_new ();
  g_object_set_data (G_OBJECT (option_menu), "menu_type",
		     GINT_TO_POINTER (GRID_TYPE_MENU));

  gimp_option_menu_append (option_menu, "Rectangle",
			   G_CALLBACK (gridtype_menu_callback),
			   GINT_TO_POINTER (RECT_GRID));
  gimp_option_menu_append (option_menu, "Polar",
			   G_CALLBACK (gridtype_menu_callback),
			   GINT_TO_POINTER (POLAR_GRID));
  gimp_option_menu_append (option_menu, "Isometric",
			   G_CALLBACK (gridtype_menu_callback),
			   GINT_TO_POINTER (ISO_GRID));

  return option_menu;
}

static GtkWidget *
option_page_menu_gridrender(void)
{
  static const gchar *labels[] = { "Normal", "Black", "White", "Grey",
				   "Darker", "Lighter", "Very Dark" };
  static const gint   values[] = { GFIG_NORMAL_GC, GFIG_BLACK_GC,
				   GFIG_WHITE_GC, GFIG_GREY_GC,
				   GFIG_ACTIVE_GC, GFIG_PRELIGHT_GC,
				   GFIG_SELECTED_GC };
  GtkWidget	*option_menu;
  gint           i;

  option_menu = gimp_option_menu_new ();
  g_object_set_data (G_OBJECT (option_menu), "menu_type",
		     GINT_TO_POINTER (GRID_RENDER_MENU));

  for (i = 0; i < G_N_ELEMENTS (labels); i++)
    gimp_option_menu_append (option_menu, labels[i],
			     G_CALLBACK (gridtype_menu_callback),
			     GINT_TO_POINTER (values[i]));

  return option_menu;
}

static GtkWidget *
options_page()
{
  GtkWidget *table;
  GtkWidget *menu;
  GtkWidget *toggle;
  GtkWidget *slider;
  GtkWidget *entry;
  GtkWidget *label;
  GtkWidget *button;
  GtkWidget *vbox;
  GtkAdjustment *size_data;
  char buf[256];

  vbox = gimp_vbox_new(FALSE, 0);
  gimp_container_set_border_width (vbox, 4);

  table = gimp_table_new (6, 6, FALSE);
  gtk_grid_set_row_spacing (GTK_GRID (table),6);
  gimp_container_set_border_width (table, 1);
  gimp_box_pack_start (vbox, table, TRUE, TRUE, 0);

  /* Put buttons in */
  toggle = gtk_check_button_new_with_label ("Show image");
  gimp_table_attach (table, toggle, 0, 1, 0, 1, GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (gfig_toggle_update),
		    (gpointer)&selvals.showimage);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (toggle_show_image),
		    GINT_TO_POINTER (1));

  button = gtk_button_new_with_label ("Reload image");
  gimp_table_attach (table, button, 1, 2, 0, 1, 0, 0, 0, 0);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (reload_button_press),
		    NULL);

  toggle = gtk_check_button_new_with_label ("Hide cntr pnts ");
  gimp_table_attach (table, toggle, 0, 1, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (gfig_toggle_update),
		    (gpointer)&selvals.opts.showcontrol);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (toggle_show_image),
		    GINT_TO_POINTER (1));

  g_snprintf(buf,sizeof (buf),"Grid type:");
  label = gtk_label_new (buf);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 2, 3, 0, 1, GIMP_EXPAND, GIMP_FILL, 0, 0);
  menu = option_page_menu_gridtype();
  gimp_table_attach (table, menu, 3, 4, 0, 1, GIMP_FILL, GIMP_FILL, 0, 0);

  g_snprintf(buf,sizeof (buf),"Grid Colour:");
  label = gtk_label_new (buf);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 2, 3, 1, 2, GIMP_EXPAND, GIMP_FILL, 0, 0);
  menu = option_page_menu_gridrender();
  gimp_table_attach (table, menu, 3, 4, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0);


  gfig_opt_widget.showcontrol = toggle;

  g_snprintf(buf,sizeof (buf),"Max Undo:");
  label = gtk_label_new (buf);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 2, 3, GIMP_EXPAND, GIMP_FILL, 0, 0);

  size_data = gtk_adjustment_new (selvals.maxundo, MIN_UNDO, MAX_UNDO,5, 1, 0);
  slider = gfig_hscale_new (size_data, 0, GTK_POS_LEFT);
  gtk_widget_set_size_request (slider, SCALE_WIDTH, -1);
  gimp_table_attach (table, slider, 1, 4, 2, 3, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);
  g_signal_connect (size_data, "value-changed",
		    G_CALLBACK (gfig_scale_update),
		    &selvals.maxundo);

  entry = gtk_entry_new();
  g_object_set_data (G_OBJECT (entry), "user_data", size_data);
  g_object_set_data (G_OBJECT (size_data), "user_data", entry);
  gtk_editable_set_width_chars (GTK_EDITABLE (entry), 4);
  g_snprintf(buf, sizeof (buf), "%d", selvals.maxundo);
  gtk_editable_set_text (GTK_EDITABLE (entry), buf);
  g_signal_connect (entry, "changed",
		    G_CALLBACK (gfig_entry_update),
		    &selvals.maxundo);
  gimp_table_attach (table, entry, 4, 5, 2, 3, GIMP_FILL, GIMP_FILL, 0, 0);

  toggle = gtk_check_button_new_with_label ("Show tool tips ");
  gimp_table_attach (table, toggle, 0, 1, 3, 4, GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (gfig_toggle_update),
		    (gpointer)&selvals.showtooltips);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (toggle_tooltips),
		    (gpointer)&selvals.showtooltips);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle),selvals.showtooltips);

  toggle = gtk_check_button_new_with_label ("Show pos");
  gimp_table_attach (table, toggle, 1, 2, 3, 4, GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (gfig_toggle_update),
		    (gpointer)&selvals.showpos);
  g_signal_connect_after (toggle, "toggled",
			  G_CALLBACK (gfig_pos_enable),
			  GINT_TO_POINTER (1));

  button = gtk_button_new_with_label ("About");
  gimp_table_attach (table, button, 3, 4, 4, 5, GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (about_button_press),
		    NULL);

  return(vbox);
}

static GtkWidget *
grid_frame()
{
  GtkWidget *frame;
  GtkWidget *table;
  GtkWidget *toggle;
  GtkWidget *label;
  GtkWidget *slider;
  GtkAdjustment *size_data;
  GtkWidget *entry;

  char buf[256];

  frame = gtk_frame_new ("Grid");
  gimp_container_set_border_width (frame, 1);

  table = gimp_table_new (7, 7, FALSE);
  gtk_frame_set_child (GTK_FRAME (frame), table);

  toggle = gtk_check_button_new_with_label ("Snap to grid");
  gimp_table_attach (table, toggle, 1, 2, 3, 4, GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (gfig_toggle_update),
		    (gpointer)&selvals.opts.snap2grid);
  gfig_opt_widget.snap2grid = toggle;

  toggle = gtk_check_button_new_with_label ("Display grid");
  gimp_table_attach (table, toggle, 0, 1, 3, 4, GIMP_FILL, GIMP_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (gfig_toggle_update),
		    (gpointer)&selvals.opts.drawgrid);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (draw_grid_clear),
		    GINT_TO_POINTER (1));
  gfig_opt_widget.drawgrid = toggle;


  toggle = gtk_check_button_new_with_label ("Lock on grid");
  gimp_table_attach (table, toggle, 2, 3, 3, 4, GIMP_FILL | GIMP_EXPAND, GIMP_FILL |GIMP_EXPAND, 0, 0);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (gfig_toggle_update),
		    (gpointer)&selvals.opts.lockongrid);

  gfig_opt_widget.lockongrid = toggle;

  label = gtk_label_new ("Grid spacing ");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 4, 5, GIMP_EXPAND, GIMP_FILL, 0, 0);

  size_data = gtk_adjustment_new (selvals.opts.gridspacing, MIN_GRID, MAX_GRID, (MAX_GRID + MIN_GRID)/2, 1, 0);
  slider = gfig_hscale_new (size_data, 0, GTK_POS_LEFT);
  gtk_widget_set_size_request (slider, SCALE_WIDTH, -1);
  gimp_table_attach (table, slider, 1,3 , 4, 5, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 0);
  g_signal_connect (size_data, "value-changed",
		    G_CALLBACK (gfig_scale_update),
		    &selvals.opts.gridspacing);
  g_signal_connect (size_data, "value-changed",
		    G_CALLBACK (draw_grid_clear),
		    GINT_TO_POINTER (0));
  gfig_opt_widget.gridspacing = size_data;

  entry = gtk_entry_new();
  g_object_set_data (G_OBJECT (entry), "user_data", size_data);
  g_object_set_data (G_OBJECT (size_data), "user_data", entry);
  gtk_editable_set_width_chars (GTK_EDITABLE (entry), 4);
  g_snprintf(buf, sizeof (buf), "%d", selvals.opts.gridspacing);
  gtk_editable_set_text (GTK_EDITABLE (entry), buf);
  g_signal_connect (entry, "changed",
		    G_CALLBACK (gfig_entry_update),
		    &selvals.opts.gridspacing);
  gimp_table_attach (table, entry, 3, 4, 4, 5, GIMP_FILL, GIMP_FILL, 0, 0);

  return(frame);
}

/* Removes all rows of a GtkListBox */
static void
clear_list_items(GtkWidget *list)
{
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (list)))
    gtk_list_box_remove (GTK_LIST_BOX (list), child);
}

/* The row of the gfig list: remembers its object, clicks on it go to
 * list_button_press.
 */
static void
gfig_list_setup_row (GtkWidget *list_item,
		     GFIGOBJ   *g)
{
  GtkGesture *gesture;

  g_object_set_data (G_OBJECT (list_item), "user_data", (gpointer)g);

  gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 0);
  g_signal_connect (gesture, "pressed",
		    G_CALLBACK (list_button_press), (gpointer)g);
  gtk_widget_add_controller (list_item, GTK_EVENT_CONTROLLER (gesture));
}

/* The object of the selected row of the list, or NULL */
static GFIGOBJ *
gfig_list_selected_obj (GtkWidget *list)
{
  GtkListBoxRow *row;

  row = gtk_list_box_get_selected_row (GTK_LIST_BOX (list));
  if (!row)
    return NULL;

  return g_object_get_data (G_OBJECT (row), "user_data");
}

static void
build_list_items(GtkWidget *list)
{
  GList *tmp = gfig_list;
  GtkWidget *list_item;
  GtkWidget *list_pix;
  GFIGOBJ *g;

  while(tmp)
    {
      g = tmp->data;

      if(g->obj_status & GFIG_READONLY)
	list_pix = gfig_new_pixmap(list,mini_cross_xpm);
      else
	list_pix = gfig_new_pixmap(list,blank_xpm);

      list_item = gfig_list_item_new_with_label_and_pixmap(g,g->draw_name,list_pix);
      gfig_list_setup_row (list_item, g);
      gtk_list_box_append (GTK_LIST_BOX (list), list_item);

      tmp = tmp->next;
    }
}

static GtkWidget *
add_objects_list ()
{
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *frame;
  GtkWidget *list_frame;
  GtkWidget *scrolled_win;
  GtkWidget *list;
  GtkWidget *button;
  GtkListBoxRow *row;

  frame = gtk_frame_new("Object");

  table = gimp_table_new (5, 4, FALSE);

  delete_frame_to_freeze = list_frame = gtk_frame_new(NULL);

  scrolled_win = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_win),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scrolled_win, 150, -1);
  gtk_frame_set_child (GTK_FRAME (list_frame), scrolled_win);

  gfig_gtk_list = list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_BROWSE);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_win), list);

  /* Load saved objects */
  gfig_list_load_all(gfig_path_list);

  /* Put list in */
  build_list_items(list);

  /* Browse selection: the first one is selected */
  row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (list), 0);
  if (row)
    gtk_list_box_select_row (GTK_LIST_BOX (list), row);

  /* Put buttons in */
  button = gtk_button_new_with_label ("Rescan");
  g_signal_connect (button, "clicked",
		    G_CALLBACK (rescan_button_press),
		    NULL);
  gfig_set_tooltip(button,"Select directory and rescan Gfig object collection");
  gimp_table_attach (table, button, 2, 3, 0, 1, GIMP_FILL, GIMP_FILL,  0, 0);

  button = gtk_button_new_with_label ("Load");
  g_signal_connect (button, "clicked",
		    G_CALLBACK (load_button_press),
		    list);
  gfig_set_tooltip(button,"Load a single Gfig object collection");
  gimp_table_attach (table, button, 2, 3, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0);

  button = gtk_button_new_with_label ("New");
  g_signal_connect (button, "clicked",
		    G_CALLBACK (new_button_press),
		    "New gfig obj");
  gfig_set_tooltip(button,"Create a new Gfig object collection for editing");
  gimp_table_attach (table, button, 2, 3, 2, 3, GIMP_FILL, GIMP_FILL, 0, 0);


  button = gtk_button_new_with_label ("Delete");
  g_signal_connect (button, "clicked",
		    G_CALLBACK (gfig_delete_gfig_callback),
		    (gpointer)list);
  gfig_set_tooltip(button,"Delete currently selected Gfig Object collection");
  gimp_table_attach (table, button, 2, 3, 3, 4, GIMP_FILL, GIMP_FILL, 0, 0);

  /* Attach the frame for the list Show the widgets */

  gimp_table_attach (table, list_frame, 1, 2, 0, 4, GIMP_FILL|GIMP_EXPAND , GIMP_FILL|GIMP_EXPAND, 1, 1);

  vbox = small_preview(list);
  gimp_table_attach (table, vbox, 0, 1, 0, 4, 0, 0, 0, 0);

  gtk_frame_set_child (GTK_FRAME (frame), table);
  return (frame);
}

static gint x_pos_val;
static gint y_pos_val;
static gint pos_tag = -1;

static void
gfig_pos_enable(GtkWidget *widget, gpointer data)
{
  gint enable = selvals.showpos;
  gtk_widget_set_sensitive(GTK_WIDGET(x_pos_label),enable);
  gtk_widget_set_sensitive(GTK_WIDGET(y_pos_label),enable);
}

static void
gfig_pos_update_labels(gpointer data)
{
  static gchar buf[256];

  /*gtk_idle_remove(pos_tag);*/
  pos_tag = -1;  

  if(x_pos_val < 0)
    sprintf(buf," X:%.3d ",x_pos_val);
  else
    sprintf(buf," X:  %.3d ",x_pos_val);

  gtk_label_set_text(GTK_LABEL(x_pos_label),buf);

  if(y_pos_val < 0)
    sprintf(buf," Y:%.3d ",y_pos_val);
  else
    sprintf(buf," Y:  %.3d ",y_pos_val);

  gtk_label_set_text(GTK_LABEL(y_pos_label),buf);
}

static void
gfig_pos_update(gint x , gint y)
{
  gint update;

  if(x_pos_val != x || y_pos_val != y)
    update = 1;
  else
    update = 0;

  x_pos_val = x;
  y_pos_val = y;

  if(update && pos_tag == -1 && selvals.showpos)
    {
      /*pos_tag = gtk_idle_add((GtkFunction)gfig_pos_update_labels,NULL);*/
      gfig_pos_update_labels(NULL);
    }
}

#if 0 /* NOT USED */
static void
gfig_obj_size_update(gint sz)
{
  static gchar buf[256];
  
  sprintf(buf,"%6d",sz);
  gtk_label_set_text(GTK_LABEL(obj_size_label),buf);
}  

static GtkWidget *
gfig_obj_size_label(void)
{
  GtkWidget *label;
  GtkWidget *hbox;
  gchar buf[256];

  hbox = gimp_hbox_new (FALSE,0);

  /* Position labels */
  label = gtk_label_new("Size:- ");
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);

  obj_size_label = gtk_label_new("");
  gimp_misc_set_alignment (obj_size_label, 0.5, 0.5);    
  gimp_box_pack_start (hbox, obj_size_label, FALSE, FALSE, 0);


  sprintf(buf,"%6d",0);
  gtk_label_set_text(GTK_LABEL(obj_size_label),buf);

  return(hbox);
}

#endif /* NOT USED */

static GtkWidget *
gfig_pos_labels(void)
{
  GtkWidget *label;
  GtkWidget *hbox;
  GtkWidget *vbox;
  gchar buf[256];

  hbox = gimp_hbox_new (FALSE, 0);
  vbox = gimp_vbox_new (FALSE, 0);

  /* Position labels */
  label = gtk_label_new("XY Pos:- ");
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);

  x_pos_label = gtk_label_new("");
  gimp_container_add (vbox, x_pos_label);

  y_pos_label = gtk_label_new("");
  gimp_container_add (vbox, y_pos_label);

  gimp_container_set_border_width (hbox, 1);

  gimp_box_pack_start (hbox, vbox, FALSE, FALSE, 0);

  sprintf(buf," X:  %.3d ",0);
  gtk_label_set_text(GTK_LABEL(x_pos_label),buf);
  sprintf(buf," Y:  %.3d ",0);
  gtk_label_set_text(GTK_LABEL(y_pos_label),buf);

  return(hbox);
}

static GtkWidget *
make_pos_info(void)
{
  GtkWidget * xframe;
  GtkWidget * hbox;
  GtkWidget * label;

  xframe = gtk_frame_new("Obj Details");
  hbox = gimp_hbox_new (TRUE, 1);

  gimp_container_add (xframe, hbox);  

  /* Add labels */
  label = gfig_pos_labels();
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);
  gfig_pos_enable(NULL,NULL);

#if 0
  label = gfig_obj_size_label();
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);
#endif /* 0 */

  return(xframe);
}


static GtkWidget *
make_status(void)
{
  GtkWidget * xframe;
  GtkWidget * table;
  GtkWidget * label;

  xframe = gtk_frame_new("Collection Details");

  table = gimp_table_new (6, 6, FALSE);

  label = gtk_label_new("Draw name:");
  gimp_misc_set_alignment (label, 0.5,0.5);
  gtk_label_set_justify(GTK_LABEL(label),GTK_JUSTIFY_RIGHT);
  gimp_table_attach (table, label, 1, 2, 0, 1, 0 , GIMP_FILL, 0, 0);

  
  label = gtk_label_new("Filename:");
  gimp_misc_set_alignment (label, 0.5,0.5);
  gtk_label_set_justify(GTK_LABEL(label),GTK_JUSTIFY_RIGHT);
  gimp_table_attach (table, label, 1, 2, 1, 2, 0 , GIMP_FILL, 0, 0);

  status_label_dname = gtk_label_new("<None>");
  gimp_misc_set_alignment (label, 0.5,0.5);
  gimp_table_attach (table, status_label_dname, 2, 4, 0, 1, GIMP_FILL|GIMP_EXPAND, 0, 0, 0);

  status_label_fname = gtk_label_new("<None>");
  gimp_misc_set_alignment (label, 0.5,0.5);
  gimp_table_attach (table, status_label_fname, 2, 4, 1, 2, GIMP_FILL|GIMP_EXPAND, 0, 0, 0);

#if 0
  label = gtk_label_new("Painting:");
  gimp_misc_set_alignment (label, 0.5,0.5);
  gimp_table_attach (table, label, 0, 2, 2, 3, 0 , GIMP_FILL|GIMP_EXPAND, 0, 0);

  progress_widget = gtk_progress_bar_new();
  gimp_table_attach (table, progress_widget, 2, 4, 2, 3, 0 , 0, 0, 0);
#endif /* 0 */

  gimp_container_add (xframe, table);  

  return(xframe);
}

/* The rulers beside the preview: GTK 4 has no GtkRuler, so these are
 * drawing areas with pixel ticks and a marker at the pointer position.
 */
#define RULER_SIZE 16

static void
gfig_ruler_draw (GtkDrawingArea *area,
		 cairo_t        *cr,
		 gint            width,
		 gint            height,
		 gpointer        data)
{
  gboolean     horizontal = GPOINTER_TO_INT (data);
  gint         length = horizontal ? width : height;
  gint         thick = horizontal ? height : width;
  gint         pos = horizontal ? gfig_ruler_x : gfig_ruler_y;
  gint         i;
  PangoLayout *layout;
  PangoFontDescription *font;
  gchar        buf[16];

  cairo_set_source_rgb (cr, 0.85, 0.85, 0.85);
  cairo_paint (cr);

  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  cairo_set_line_width (cr, 1.0);

  /* Base line */
  if (horizontal)
    {
      cairo_move_to (cr, 0, height - 0.5);
      cairo_line_to (cr, width, height - 0.5);
    }
  else
    {
      cairo_move_to (cr, width - 0.5, 0);
      cairo_line_to (cr, width - 0.5, height);
    }

  /* Ticks */
  for (i = 0; i < length; i += 5)
    {
      gint tick;

      if (i % 50 == 0)
	tick = thick / 2;
      else if (i % 10 == 0)
	tick = thick / 4;
      else
	tick = thick / 8;

      if (horizontal)
	{
	  cairo_move_to (cr, i + 0.5, height);
	  cairo_line_to (cr, i + 0.5, height - tick);
	}
      else
	{
	  cairo_move_to (cr, width, i + 0.5);
	  cairo_line_to (cr, width - tick, i + 0.5);
	}
    }
  cairo_stroke (cr);

  /* Numbers every 100 pixels */
  layout = pango_cairo_create_layout (cr);
  font = pango_font_description_from_string ("Sans 6");
  pango_layout_set_font_description (layout, font);
  pango_font_description_free (font);

  for (i = 0; i < length; i += 100)
    {
      g_snprintf (buf, sizeof (buf), "%d", i);
      pango_layout_set_text (layout, buf, -1);

      cairo_save (cr);
      if (horizontal)
	cairo_move_to (cr, i + 2, 0);
      else
	{
	  cairo_move_to (cr, 0, i + 2);
	  cairo_rotate (cr, G_PI / 2);
	  cairo_rel_move_to (cr, 0, -thick + 2);
	}
      pango_cairo_show_layout (cr, layout);
      cairo_restore (cr);
    }
  g_object_unref (layout);

  /* The pointer position */
  if (pos >= 0)
    {
      if (horizontal)
	{
	  cairo_move_to (cr, pos - thick / 4.0, height / 2.0);
	  cairo_line_to (cr, pos + thick / 4.0, height / 2.0);
	  cairo_line_to (cr, pos, height);
	}
      else
	{
	  cairo_move_to (cr, width / 2.0, pos - thick / 4.0);
	  cairo_line_to (cr, width / 2.0, pos + thick / 4.0);
	  cairo_line_to (cr, width, pos);
	}
      cairo_close_path (cr);
      cairo_fill (cr);
    }
}

static GtkWidget *
gfig_ruler_new (gboolean horizontal)
{
  GtkWidget *ruler;

  ruler = gtk_drawing_area_new ();
  if (horizontal)
    {
      gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (ruler),
					  preview_width);
      gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (ruler),
					   RULER_SIZE);
    }
  else
    {
      gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (ruler),
					  RULER_SIZE);
      gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (ruler),
					   preview_height);
    }
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (ruler), gfig_ruler_draw,
				  GINT_TO_POINTER (horizontal), NULL);

  return ruler;
}

static void
gfig_rulers_update (gint x,
		    gint y)
{
  if (gfig_ruler_x != x && gfig_hruler)
    gtk_widget_queue_draw (gfig_hruler);
  if (gfig_ruler_y != y && gfig_vruler)
    gtk_widget_queue_draw (gfig_vruler);

  gfig_ruler_x = x;
  gfig_ruler_y = y;
}

static void
gfig_preview_leave (GtkEventControllerMotion *controller,
		    gpointer                  data)
{
  gfig_rulers_update (-1, -1);
}

static GtkWidget *
make_preview(void)
{
  GtkWidget * xframe;
  GtkWidget * vbox;
  GtkWidget * hbox;
  GtkWidget * table;
  GtkGesture *drag;
  GtkEventController *controller;


  gfig_preview = gtk_drawing_area_new ();
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (gfig_preview),
				      preview_width);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (gfig_preview),
				       preview_height);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (gfig_preview),
				  gfig_preview_draw, NULL, NULL);
  gtk_widget_set_halign (gfig_preview, GTK_ALIGN_START);
  gtk_widget_set_valign (gfig_preview, GTK_ALIGN_START);
  gtk_widget_set_focusable (gfig_preview, TRUE);
  gtk_widget_set_cursor_from_name (gfig_preview, "crosshair");

  /* Presses and releases of any button */
  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), 0);
  g_signal_connect (drag, "drag-begin",
		    G_CALLBACK (gfig_preview_drag_begin), NULL);
  g_signal_connect (drag, "drag-end",
		    G_CALLBACK (gfig_preview_drag_end), NULL);
  gtk_widget_add_controller (gfig_preview, GTK_EVENT_CONTROLLER (drag));

  controller = gtk_event_controller_motion_new ();
  g_signal_connect (controller, "motion",
		    G_CALLBACK (gfig_preview_motion), NULL);
  g_signal_connect (controller, "leave",
		    G_CALLBACK (gfig_preview_leave), NULL);
  gtk_widget_add_controller (gfig_preview, controller);

  controller = gtk_event_controller_key_new ();
  g_signal_connect (controller, "key-pressed",
		    G_CALLBACK (gfig_preview_key_press), NULL);
  g_signal_connect (controller, "key-released",
		    G_CALLBACK (gfig_preview_key_release), NULL);
  gtk_widget_add_controller (gfig_preview, controller);

  xframe = gtk_frame_new(NULL);


  table = gimp_table_new (3, 3, FALSE);
  gimp_table_attach (table, gfig_preview, 1, 2, 1, 2, GIMP_FILL ,GIMP_FILL , 0, 0);
  gtk_frame_set_child (GTK_FRAME (xframe), table);

  gfig_hruler = gfig_ruler_new (TRUE);
  gimp_table_attach (table, gfig_hruler, 1, 2, 0, 1, GIMP_FILL, GIMP_FILL, 0,0);

  gfig_vruler = gfig_ruler_new (FALSE);
  gimp_table_attach (table, gfig_vruler, 0, 1, 1, 2, GIMP_FILL , GIMP_FILL, 0,0);



  vbox = gimp_vbox_new (FALSE, 0);
  hbox = gimp_hbox_new (FALSE, 0);
  gimp_box_pack_start (hbox, xframe, FALSE, FALSE, 0);

  gimp_container_set_border_width (vbox, 2);
  gimp_box_pack_start (vbox, hbox, FALSE, FALSE, 0);

  xframe = make_pos_info();
  gimp_box_pack_start (vbox, xframe, TRUE, TRUE, 0);

  xframe = make_status();
  gimp_box_pack_start (vbox, xframe, TRUE, TRUE, 0);


  return(vbox);
}

/* The grid colours.  The GTK 1 widget state backgrounds of the default
 * style, black, white and a grey stipple.
 */
static void
gfig_set_grid_source (cairo_t *cr,
		      gint     gctype)
{
  static cairo_pattern_t *grey_pattern = NULL;

  switch(gctype)
    {
    case GFIG_BLACK_GC:
      cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
      break;
    case GFIG_WHITE_GC:
      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
      break;
    case GFIG_GREY_GC:
      if (!grey_pattern)
	{
	  /* gray80 on gray50, every other pixel */
	  cairo_surface_t *surface;
	  guint32         *pixels;
	  gint             stride;

	  surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24, 2, 2);
	  cairo_surface_flush (surface);
	  pixels = (guint32 *) cairo_image_surface_get_data (surface);
	  stride = cairo_image_surface_get_stride (surface) / 4;
	  pixels[0] = 0x00cccccc;
	  pixels[1] = 0x007f7f7f;
	  pixels[stride] = 0x007f7f7f;
	  pixels[stride + 1] = 0x00cccccc;
	  cairo_surface_mark_dirty (surface);

	  grey_pattern = cairo_pattern_create_for_surface (surface);
	  cairo_pattern_set_extend (grey_pattern, CAIRO_EXTEND_REPEAT);
	  cairo_pattern_set_filter (grey_pattern, CAIRO_FILTER_NEAREST);
	  cairo_surface_destroy (surface);
	}
      cairo_set_source (cr, grey_pattern);
      break;
    case GFIG_NORMAL_GC:
    case GFIG_INSENSITIVE_GC:
      cairo_set_source_rgb (cr, 0xd6 / 255.0, 0xd6 / 255.0, 0xd6 / 255.0);
      break;
    case GFIG_ACTIVE_GC:
      cairo_set_source_rgb (cr, 0xc3 / 255.0, 0xc3 / 255.0, 0xc3 / 255.0);
      break;
    case GFIG_PRELIGHT_GC:
      cairo_set_source_rgb (cr, 0xea / 255.0, 0xea / 255.0, 0xea / 255.0);
      break;
    case GFIG_SELECTED_GC:
      cairo_set_source_rgb (cr, 0.0, 0.0, 0x9c / 255.0);
      break;
    default:
      g_warning("Unknown type for grid colouring\n");
      cairo_set_source_rgb (cr, 0xea / 255.0, 0xea / 255.0, 0xea / 255.0);
      break;
    }
}


static gint
gfig_dialog (void)
{
  GtkWidget *button;
  GtkWidget *frame;
  GtkWidget *xframe;
  GtkWidget *oframe;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *notebook;
  GtkWidget *page;
  GtkWidget *top_level_dlg;

  gtk_init ();

  /* And my bit */
  plug_in_parse_gfig_path();

  /*cache_preview(); Get the preview image and store it also set has_alpha */

  img_width  = gimp_drawable_width(gfig_select_drawable->id);
  img_height = gimp_drawable_height(gfig_select_drawable->id);

  /* Start buildng the dialog up */
  gfig_top_level = top_level_dlg = gimp_dialog_new ("Gfig");
  g_signal_connect (top_level_dlg, "destroy",
		    G_CALLBACK (gfig_close_callback),
		    NULL);

  /*  Action area  */
  gimp_dialog_add_button (top_level_dlg, "Done",
			  G_CALLBACK (gfig_ok_callback),
			  top_level_dlg, TRUE);

  gimp_dialog_add_button (top_level_dlg, "Paint",
			  G_CALLBACK (gfig_paint_callback),
			  top_level_dlg, FALSE);

  save_button = gimp_dialog_add_button (top_level_dlg, "Save",
					G_CALLBACK (save_button_press),
					top_level_dlg, FALSE);

  gimp_dialog_add_button (top_level_dlg, "Clear",
			  G_CALLBACK (gfig_clear_callback),
			  top_level_dlg, FALSE);

  undo_widget = button = gimp_dialog_add_button (top_level_dlg, "Undo",
						 G_CALLBACK (gfig_undo_callback),
						 top_level_dlg, FALSE);
  gtk_widget_set_sensitive(button,FALSE);

  gimp_dialog_add_button (top_level_dlg, "Cancel",
			  G_CALLBACK (gfig_cancel_callback),
			  top_level_dlg, FALSE);

  /* Start building the frame for the preview area */
  frame = gtk_frame_new ("preview");
  gimp_container_set_border_width (frame, 1);
  table = gimp_table_new (6, 6, FALSE);
  gimp_container_set_border_width (table, 1);
  gtk_frame_set_child (GTK_FRAME (frame), table);
  gimp_box_pack_start (gimp_dialog_get_vbox (top_level_dlg), frame, TRUE, TRUE, 0);

  /* Preview itself */
  xframe = make_preview();
  gimp_table_attach (table, xframe, 1, 2, 0, 2, GIMP_FILL, GIMP_FILL, 0, 0);


  /* Add buttons beside the preview frame */
  xframe = draw_buttons(top_level_dlg);
  gimp_table_attach (table, xframe, 0,1, 0, 2, GIMP_FILL, GIMP_FILL, 0, 0);

  frame = gtk_frame_new ("Settings");
  gimp_container_set_border_width (frame, 1);

  gimp_table_attach (table, frame, 2, 3, 0, 2, GIMP_FILL , GIMP_FILL, 0, 0);
  table = gimp_table_new (7, 7, FALSE);

  gimp_container_set_border_width (table, 1);
  gtk_grid_set_row_spacing (GTK_GRID (table),1);
  gtk_frame_set_child (GTK_FRAME (frame), table);

  /* listbox + entry */
  oframe = add_objects_list();
  gimp_table_attach (table, oframe, 0,6, 0 , 1, GIMP_EXPAND|GIMP_FILL, GIMP_FILL, 0, 0);

  /* Grid entry */

  xframe = grid_frame();
  gimp_table_attach (table, xframe, 0,6, 3, 4, GIMP_EXPAND|GIMP_FILL, GIMP_FILL, 0, 0);

  /* The notebook */
  notebook = gtk_notebook_new();
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
  gimp_table_attach (table, notebook, 0, 6, 5, 6, GIMP_FILL|GIMP_EXPAND, GIMP_FILL|GIMP_EXPAND, 0, 0);

  page = paint_page();
  label = gtk_label_new("Paint");
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, label);

  brush_page_widget = brush_page();
  label = gtk_label_new("Brush");
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), brush_page_widget, label);


  /* Sometime maybe allow all objects to be done by selections - this
   * would adjust the selection options.
   */
  select_page_widget = select_page();
  label = gtk_label_new("Select");
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), select_page_widget, label);
  gtk_widget_set_sensitive(select_page_widget,FALSE);


  page = options_page();
  label = gtk_label_new("Options");
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, label);



  gtk_window_present (GTK_WINDOW (top_level_dlg));
  dialog_update_preview();
  gfig_update_stat_labels();


  gimp_main_loop_run ();

  return gfig_run;
}

static void
gfig_close_callback (GtkWidget *widget,
			 gpointer   data)
{
  gfig_top_level = NULL;
  gfig_brush_img_del(); /* Delete the brush image in the gimp */
  gimp_main_loop_quit ();
}

static void
done_ok_window(GtkWidget * widget,
		    gpointer   data)
{
  GtkWidget *warn_window = g_object_get_data (G_OBJECT (widget), "warn_window");

  gfig_run = TRUE;
  if (warn_window)
    gtk_window_destroy (GTK_WINDOW (warn_window));
  gimp_widget_destroy (GTK_WIDGET (data));
}

static void
done_warn_dialog (GtkWidget *w,gint count)
{
  GtkWidget *window = NULL;
  GtkWidget *label;
  GtkWidget *button;
  gchar buf[128];

  window = gimp_dialog_new ("Warning");
  gtk_window_set_transient_for (GTK_WINDOW (window), GTK_WINDOW (w));

  button = gimp_dialog_add_button (window, "OK", G_CALLBACK (done_ok_window),
				   (gpointer)w, TRUE);
  g_object_set_data (G_OBJECT (button), "warn_window", window);

  gimp_dialog_add_button (window, "Cancel", G_CALLBACK (ok_warn_window),
			  (gpointer)window, FALSE);

  label = gtk_label_new("Unsaved Gfig objects - continue with exiting?");
  gimp_container_set_border_width (label, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (window), label, TRUE, TRUE, 0);

  g_snprintf(buf,sizeof (buf),"Number objects unsaved = %d\n",count);
  label = gtk_label_new(buf);
  gimp_container_set_border_width (label, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (window), label, TRUE, TRUE, 0);
  gtk_window_present (GTK_WINDOW (window));
}

static void
gfig_ok_callback (GtkWidget *widget,
		      gpointer   data)
{
  /* Check if outstanding saves */
  GList * list;
  GFIGOBJ * gfig;
  gint count = 0;

  list = gfig_list;
  while (list)
    {
      gfig = (GFIGOBJ *) list->data;
      if(gfig->obj_status & GFIG_MODIFIED)
	count++;
      list = list->next;
    }

  if(count)
    done_warn_dialog(GTK_WIDGET(data),count);
  else
    {
      gfig_run = TRUE;
      gtk_window_destroy (GTK_WINDOW (data));
    }
  gfig_brush_img_del();
}

static void
gfig_cancel_callback(GtkWidget *widget,
		      gpointer   data)
{
  gtk_window_destroy(GTK_WINDOW(data));
  gfig_brush_img_del();
}


/* Update the bits we put on the screen */
static void
update_draw_area(GtkWidget *widget,GdkEvent *event)
{
  /* The draw function paints the image, the grid and the objects */
  if (gfig_preview)
    gtk_widget_queue_draw (gfig_preview);
}

static void draw_creating (void);

static void
gfig_preview_draw (GtkDrawingArea *area,
		   cairo_t        *cr,
		   gint            width,
		   gint            height,
		   gpointer        data)
{
  if (gfig_back_surface)
    {
      cairo_set_source_surface (cr, gfig_back_surface, 0, 0);
      cairo_paint (cr);
    }
  else
    {
      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
      cairo_paint (cr);
    }

  gfig_cr = cr;
  cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
  cairo_set_line_width (cr, 1.0);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);

  draw_grid (NULL, NULL);

  /* The objects are inverted over what is below them, as they were
   * with the GDK_INVERT GC.
   */
  cairo_set_operator (cr, CAIRO_OPERATOR_DIFFERENCE);
  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);

  if (current_obj)
    {
      gint saved_scale = selvals.scaletoimage;

      /* While an object is being made scaletoimage is forced on, the
       * objects already made still use the real setting.
       */
      if (need_to_scale)
	selvals.scaletoimage = 0;
      draw_objects(current_obj->obj_list,TRUE);
      selvals.scaletoimage = saved_scale;
    }
  draw_creating ();

  gfig_cr = NULL;
}

static void
pic_preview_draw (GtkDrawingArea *area,
		  cairo_t        *cr,
		  gint            width,
		  gint            height,
		  gpointer        data)
{
  /* White, objects in black */
  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_paint (cr);

  if(pic_obj)
    {
      gfig_cr = cr;
      cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
      cairo_set_line_width (cr, 1.0);
      cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
      drawing_pic = TRUE;
      draw_objects(pic_obj->obj_list,FALSE);
      drawing_pic = FALSE;
      gfig_cr = NULL;
    }
}

static gint
adjust_pic_coords(gint coord,gint ratio)
{
  /*return((SMALL_PREVIEW_SZ * coord)/PREVIEW_SIZE);*/
  static gint pratio = -1;

  if(pratio == -1)
    {
      pratio = MAX(preview_width,preview_height);
    }

  return((SMALL_PREVIEW_SZ * coord)/pratio);
}

/* Drawing primitives, see gfig_queue_draw () */

static void
gfig_queue_draw (void)
{
  GtkWidget *widget = drawing_pic ? pic_preview : gfig_preview;

  if (widget)
    gtk_widget_queue_draw (widget);
}

static void
gfig_draw_line (gint x1, gint y1, gint x2, gint y2)
{
  if (!gfig_cr)
    {
      gfig_queue_draw ();
      return;
    }

  cairo_move_to (gfig_cr, x1 + 0.5, y1 + 0.5);
  cairo_line_to (gfig_cr, x2 + 0.5, y2 + 0.5);
  cairo_stroke (gfig_cr);
}

static void
gfig_draw_rectangle (gint filled,
		     gint x,
		     gint y,
		     gint width,
		     gint height)
{
  if (!gfig_cr)
    {
      gfig_queue_draw ();
      return;
    }

  if (filled)
    {
      cairo_rectangle (gfig_cr, x, y, width, height);
      cairo_fill (gfig_cr);
    }
  else
    {
      cairo_rectangle (gfig_cr, x + 0.5, y + 0.5, width, height);
      cairo_stroke (gfig_cr);
    }
}

/* Angles in 1/64ths of a degree, counter-clockwise from 3 o'clock,
 * angle2 is the extent: the old gdk_draw_arc ().
 */
static void
gfig_draw_arc (gint filled,
	       gint x,
	       gint y,
	       gint width,
	       gint height,
	       gint angle1,
	       gint angle2)
{
  gdouble start;
  gdouble end;

  if (!gfig_cr)
    {
      gfig_queue_draw ();
      return;
    }

  if (width <= 0 || height <= 0)
    return;

  start = -(angle1 / 64.0) * G_PI / 180.0;
  end   = -((angle1 + angle2) / 64.0) * G_PI / 180.0;

  cairo_new_path (gfig_cr);
  cairo_save (gfig_cr);
  if (filled)
    cairo_translate (gfig_cr, x + width / 2.0, y + height / 2.0);
  else
    cairo_translate (gfig_cr, x + width / 2.0 + 0.5, y + height / 2.0 + 0.5);
  cairo_scale (gfig_cr, width / 2.0, height / 2.0);
  if (filled)
    cairo_move_to (gfig_cr, 0.0, 0.0);
  if (angle2 >= 0)
    cairo_arc_negative (gfig_cr, 0.0, 0.0, 1.0, start, end);
  else
    cairo_arc (gfig_cr, 0.0, 0.0, 1.0, start, end);
  if (filled)
    cairo_close_path (gfig_cr);
  cairo_restore (gfig_cr);

  if (filled)
    cairo_fill (gfig_cr);
  else
    cairo_stroke (gfig_cr);
}

/* Converts the pointer position to a point like the old event
 * handler did.
 */
static void
gfig_preview_button_press (guint           button,
			   GdkModifierType state,
			   gdouble         x,
			   gdouble         y)
{
  GfigPoint point;

  point.x = x;
  point.y = y;

  g_assert(need_to_scale == 0); /* If not out of step some how */

  /* Start drawing of object */
  if(selvals.otype >= MOVE_OBJ)
    {
      if(!selvals.scaletoimage)
	{
	  point.x = gfig_invscale_x(point.x);
	  point.y = gfig_invscale_y(point.y);
	}
      object_operation_start(&point,state & GDK_SHIFT_MASK);

      /* If constraining save start pnt */
      if(selvals.opts.snap2grid)
	{
	  /* Save point to constained point ... if button 3 down */
	  if(button == 3)
	    {
	      find_grid_pos(&point,&point,FALSE);
	    }
	}
    }
  else
    {
      if(selvals.opts.snap2grid)
	{
	  find_grid_pos(&point,&point,FALSE);
	}
      object_start(&point,state & GDK_SHIFT_MASK);
    }

  gtk_widget_queue_draw (gfig_preview);
}

static void
gfig_preview_button_release (guint           button,
			     GdkModifierType state,
			     gdouble         x,
			     gdouble         y)
{
  GfigPoint point;

  point.x = x;
  point.y = y;

  if(selvals.opts.snap2grid)
    find_grid_pos(&point,&point,button == 3);

  /* Still got shift down ?*/
  if(selvals.otype >= MOVE_OBJ)
    {
      if(!selvals.scaletoimage)
	{
	  point.x = gfig_invscale_x(point.x);
	  point.y = gfig_invscale_y(point.y);
	}
      object_operation_end(&point,state & GDK_SHIFT_MASK);
    }
  else
    {
      if(obj_creating)
	{
	  object_end(&point,state & GDK_SHIFT_MASK);
	}
      else
	return;
    }

  gtk_widget_queue_draw (gfig_preview);

  /* make small preview reflect changes ?*/
  list_button_update(current_obj);
}

static void
gfig_preview_drag_begin (GtkGestureDrag *gesture,
			 gdouble         x,
			 gdouble         y,
			 gpointer        data)
{
  guint           button;
  GdkModifierType state;

  button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
  state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));

  gtk_widget_grab_focus (gfig_preview);

  gfig_preview_button_press (button, state, x, y);
}

static void
gfig_preview_drag_end (GtkGestureDrag *gesture,
		       gdouble         offset_x,
		       gdouble         offset_y,
		       gpointer        data)
{
  guint           button;
  GdkModifierType state;
  gdouble         x, y;

  button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
  state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));
  gtk_gesture_drag_get_start_point (gesture, &x, &y);

  gfig_preview_button_release (button, state, x + offset_x, y + offset_y);
}

static void
gfig_preview_motion (GtkEventControllerMotion *controller,
		     gdouble                   x,
		     gdouble                   y,
		     gpointer                  data)
{
  GfigPoint point;
  GdkModifierType state;

  state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (controller));

  point.x = x;
  point.y = y;

  gfig_rulers_update (point.x, point.y);

  if(selvals.opts.snap2grid)
    find_grid_pos(&point,&point,state & GDK_BUTTON3_MASK);

  if(selvals.otype >= MOVE_OBJ)
    {
      /* Moving objects around */
      if(!selvals.scaletoimage)
	{
	  point.x = gfig_invscale_x(point.x);
	  point.y = gfig_invscale_y(point.y);
	}
      object_operation(&point,state & GDK_SHIFT_MASK);
      gfig_pos_update(point.x,point.y);
      return;
    }

  if(obj_creating)
    {
      object_update(&point);
      gtk_widget_queue_draw (gfig_preview);
    }
  gfig_pos_update(point.x,point.y);
}

/* While a key is down all objects are shown */
static gint tmp_show_single = -1;
static gboolean key_is_down = FALSE;

static gboolean
gfig_preview_key_press (GtkEventControllerKey *controller,
			guint                  keyval,
			guint                  keycode,
			GdkModifierType        state,
			gpointer               data)
{
  if (key_is_down)
    return FALSE; /* Auto repeat */

  key_is_down = TRUE;

  if((tmp_show_single = obj_show_single) != -1)
    {
      obj_show_single = -1;
      draw_grid_clear(NULL,NULL); /*Args not used */
    }
  return FALSE;
}

static void
gfig_preview_key_release (GtkEventControllerKey *controller,
			  guint                  keyval,
			  guint                  keycode,
			  GdkModifierType        state,
			  gpointer               data)
{
  key_is_down = FALSE;

  if(tmp_show_single != -1)
    {
      obj_show_single = tmp_show_single;
      tmp_show_single = -1;
      draw_grid_clear(NULL,NULL); /*Args not used */
    }
}


/*
 *  The edit gfig name attributes dialog
 *  Modified from Gimp source - layer edit.
 */

typedef struct _GfigListOptions {
  GtkWidget *query_box;
  GtkWidget *name_entry;
  GtkWidget *list_entry;
  GFIGOBJ * obj;
  gint created;
} GfigListOptions;

static GtkWidget *
gfig_list_add(GFIGOBJ *obj)
{
  gint pos;
  GtkWidget *list_item;
  GtkWidget *list_pix;

  list_pix = gfig_new_pixmap(gfig_gtk_list,Floppy6_xpm);
  list_item = gfig_list_item_new_with_label_and_pixmap(obj,obj->draw_name,list_pix);

  gfig_list_setup_row (list_item, obj);

  pos = gfig_list_insert(obj);

  gtk_list_box_insert (GTK_LIST_BOX (gfig_gtk_list), list_item, pos);
  gtk_list_box_select_row (GTK_LIST_BOX (gfig_gtk_list),
			   GTK_LIST_BOX_ROW (list_item));

  return(list_item);
}

static void
gfig_list_ok_callback (GtkWidget *w,
		       gpointer   client_data)
{
  GfigListOptions *options;
  GtkWidget *list;

  options = (GfigListOptions *) client_data;
  list = options->list_entry;

  /*  Set the new layer name  */
#ifdef DEBUG
  printf("Found obj %s\n",options->obj->draw_name);
#endif /* DEBUG */
  if (options->obj->draw_name)
    {
      g_free(options->obj->draw_name);
    }
  options->obj->draw_name = g_strdup (gtk_editable_get_text (GTK_EDITABLE (options->name_entry)));
#ifdef DEBUG
  printf("NEW name %s\n",options->obj->draw_name);
#endif /* DEBUG */

  /* Need to reorder the list */
  if (gtk_widget_get_parent (list) == gfig_gtk_list)
    gtk_list_box_remove (GTK_LIST_BOX (gfig_gtk_list), list);

  /* remove/Add again */
  gfig_list = g_list_remove(gfig_list,options->obj);
  gfig_list_add(options->obj);

  options->obj->obj_status |= GFIG_MODIFIED;

  options->created = FALSE;
  gtk_window_destroy (GTK_WINDOW (options->query_box));

  gfig_update_stat_labels();
}

static void
gfig_list_cancel_callback (GtkWidget *w,
			   gpointer   client_data)
{
  GfigListOptions *options;

  options = (GfigListOptions *) client_data;
  if(options->created)
    {
      /* We are creating an entry so if cancelled must del the list item as well */
      options->created = FALSE;
      delete_button_press_ok(w,gfig_gtk_list);
    }

  gtk_window_destroy (GTK_WINDOW (options->query_box));
}

static void
gfig_list_options_free (gpointer data)
{
  g_free (data);
}

static void
gfig_dialog_edit_list (GtkWidget *lwidget,GFIGOBJ *obj,gint created)
{
  GfigListOptions *options;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *label;

  /*  the new options structure  */
  options = (GfigListOptions *) g_malloc (sizeof (GfigListOptions));
  options->list_entry = lwidget;
  options->obj = obj;
  options->created = created;

  /*  the dialog  */
  options->query_box = gimp_dialog_new ("Edit Gfig entry name");
  if (gfig_top_level)
    gtk_window_set_transient_for (GTK_WINDOW (options->query_box),
				  GTK_WINDOW (gfig_top_level));
  g_object_set_data_full (G_OBJECT (options->query_box), "gfig-options",
			  options, gfig_list_options_free);

  /*  the main vbox  */
  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, 2);
  gimp_box_pack_start (gimp_dialog_get_vbox (options->query_box), vbox, TRUE, TRUE, 0);

  /*  the name entry hbox, label and entry  */
  hbox = gimp_hbox_new (FALSE, 1);
  gimp_box_pack_start (vbox, hbox, FALSE, FALSE, 0);
  label = gtk_label_new ("Gfig object name:");
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);
  options->name_entry = gtk_entry_new ();
  gimp_box_pack_start (hbox, options->name_entry, TRUE, TRUE, 0);
  gtk_editable_set_text (GTK_EDITABLE (options->name_entry),obj->draw_name);
  gtk_entry_set_activates_default (GTK_ENTRY (options->name_entry), TRUE);

  gimp_dialog_add_button (options->query_box, "OK",
			  G_CALLBACK (gfig_list_ok_callback),
			  options, TRUE);

  gimp_dialog_add_button (options->query_box, "Cancel",
			  G_CALLBACK (gfig_list_cancel_callback),
			  options, FALSE);

  gtk_window_present (GTK_WINDOW (options->query_box));
}

static void
gfig_rescan_cancel_callback (GtkWidget *w,
			     gpointer   client_data)
{
  gtk_window_destroy (GTK_WINDOW (client_data));
}

static GList *rescan_list = NULL;

static void
gfig_rescan_ok_callback (GtkWidget *w,
		       gpointer   client_data)
{
  GList *list;
  GtkListBoxRow *row;

  list = rescan_list;
  while (list)
    {
#ifdef DEBUG
      printf("(ADD) list->data = %s\n",(gchar *)list->data);
#endif /* DEBUG */
      list = list->next;
    }
  list = gfig_path_list;
  while (list)
    {
#ifdef DEBUG
      printf("(CONF) list->data = %s\n",(gchar *)list->data);
#endif /* DEBUG */
      rescan_list = g_list_append(rescan_list,g_strdup(list->data));
      list = list->next;
    }
  clear_list_items(gfig_gtk_list);
  gfig_list_load_all(rescan_list);
  build_list_items(gfig_gtk_list);
  row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (gfig_gtk_list), 0);
  if (row)
    gtk_list_box_select_row (GTK_LIST_BOX (gfig_gtk_list), row);
  list_button_update(current_obj);
  gtk_window_destroy (GTK_WINDOW (client_data));
}

/* Asks for a directory: GtkFileDialog's folder selection, which the
 * shared gimp_file_dialog_* helpers do not offer.  callback gets the
 * path or NULL.
 */
typedef struct
{
  GimpFileCallback callback;
  gpointer         data;
} GfigFolderRequest;

static void
gfig_folder_dialog_done (GObject      *source,
			 GAsyncResult *result,
			 gpointer      user_data)
{
  GfigFolderRequest *request = user_data;
  GFile             *file;
  gchar             *path = NULL;

  file = gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (source),
					       result, NULL);
  if (file)
    {
      path = g_file_get_path (file);
      g_object_unref (file);
    }

  request->callback (path, request->data);

  g_free (path);
  g_free (request);
}

static void
gfig_folder_dialog (GtkWindow        *parent,
		    const gchar      *title,
		    GimpFileCallback  callback,
		    gpointer          data)
{
  GtkFileDialog     *dialog;
  GfigFolderRequest *request;

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, title);

  request = g_new0 (GfigFolderRequest, 1);
  request->callback = callback;
  request->data     = data;

  gtk_file_dialog_select_folder (dialog, parent, NULL,
				 gfig_folder_dialog_done, request);
  g_object_unref (dialog);
}

static void
gfig_rescan_file_selection_ok(const gchar *filenamebuf,
			      gpointer     data)
{
  GtkWidget *list_item;
  GtkWidget *label;
  GtkWidget *lw = (GtkWidget *)data;

  if (!filenamebuf)
    return; /* Cancelled */

  if (!g_file_test (filenamebuf, G_FILE_TEST_IS_DIR))
    {
     g_warning("Entry %.100s is not a directory\n",filenamebuf);
    }
  else
    {
      list_item = gtk_list_box_row_new ();
      label = gtk_label_new (filenamebuf);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (list_item), label);

      gtk_list_box_prepend (GTK_LIST_BOX (lw), list_item);

      rescan_list = g_list_prepend(rescan_list,g_strdup(filenamebuf));
    }
}

static void
gfig_rescan_add_entry_callback (GtkWidget *w,
		       gpointer   client_data)
{
  /* Call up the file sel dialouge */
  gfig_folder_dialog (GTK_WINDOW (gtk_widget_get_root (w)), "Add Gfig path",
		      gfig_rescan_file_selection_ok, client_data);
}

static void
gfig_rescan_list (void)
{
  GtkWidget *vbox;
  GtkWidget *dlg;
  GtkWidget *list_frame;
  GtkWidget *scrolled_win;
  GtkWidget *list_widget;
  GList *list;

  /*  the dialog  */
  dlg = gimp_dialog_new ("Rescan for Gfig objects");
  if (gfig_top_level)
    gtk_window_set_transient_for (GTK_WINDOW (dlg),
				  GTK_WINDOW (gfig_top_level));

  /*  the main vbox  */
  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, 2);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), vbox, TRUE, TRUE, 0);

  /* path list */
  list_frame = gtk_frame_new(NULL);

  scrolled_win = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_win),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scrolled_win, 300, 120);
  gtk_frame_set_child (GTK_FRAME (list_frame), scrolled_win);

  list_widget = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_widget), GTK_SELECTION_BROWSE);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_win), list_widget);
  gimp_box_pack_start (vbox, list_frame, TRUE, TRUE, 0);

  list = gfig_path_list;
  while (list)
    {
      GtkWidget *list_item;
      GtkWidget *label;

      list_item = gtk_list_box_row_new ();
      label = gtk_label_new (list->data);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (list_item), label);
      gtk_list_box_append (GTK_LIST_BOX (list_widget), list_item);
      list = list->next;
    }

  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (gfig_rescan_ok_callback),
			  (gpointer)dlg, TRUE);

  /* Clear the old list out */
  if((list = rescan_list))
    {
      while (list)
	{
	  g_free(list->data);
	  list = list->next;
	}

      g_list_free(rescan_list);
      rescan_list = NULL;
    }

  gimp_dialog_add_button (dlg, "Add Dir",
			  G_CALLBACK (gfig_rescan_add_entry_callback),
			  (gpointer)list_widget, FALSE);

  g_object_set_data (G_OBJECT (dlg), "user_data",(gpointer)list_widget);

  gimp_dialog_add_button (dlg, "Cancel",
			  G_CALLBACK (gfig_rescan_cancel_callback),
			  (gpointer)dlg, FALSE);

  gtk_window_present (GTK_WINDOW (dlg));
}


void
list_button_update(GFIGOBJ *obj)
{
  g_return_if_fail (obj != NULL);
  pic_obj = (GFIGOBJ *)obj;
  /* The small preview draws pic_obj */
  if (pic_preview)
    gtk_widget_queue_draw (pic_preview);
}


static void
gfig_load_file_selection_ok(const gchar *filename,
			    gpointer     data)
{
  GFIGOBJ * gfig;
  GFIGOBJ * current_saved;

  if (!filename)
    return; /* Cancelled */

#ifdef DEBUG
  printf("Loading file '%s'\n",filename);
#endif /* DEBUG */

  if (g_file_test (filename, G_FILE_TEST_IS_REGULAR))
    {
      /* Hack - current object MUST be NULL to prevent setup_undo()
       * from kicking in.
       */
      current_saved = current_obj;
      current_obj = NULL;
      gfig = gfig_load ((gchar *) filename, (gchar *) filename);
      current_obj = current_saved;

      if (gfig)
	{
	  /* Read only ?*/
	  if(g_access(filename,W_OK))
	    gfig->obj_status |= GFIG_READONLY;

	  gfig_list_add(gfig);
	  new_obj_2edit(gfig);
	}
    }
}

static void
load_button_press(GtkWidget *widget,
		  gpointer   data)
{
  /* Load a single object */
  gimp_file_dialog_open (GTK_WINDOW (gfig_top_level), "Load Gfig obj",
			 NULL, gfig_load_file_selection_ok, NULL);
}

#if 0 /* NOT USED */
static void 
mygimp_edit_clear(gint32 image_ID, gint32 layer_ID)
{
  GParam *return_vals;
  int nreturn_vals;
  return_vals = gimp_run_procedure ("gimp_edit_clear",
                                    &nreturn_vals,
                                    PARAM_IMAGE, image_ID,
                                    PARAM_DRAWABLE, layer_ID,
                                    PARAM_END);
  gimp_destroy_params (return_vals, nreturn_vals);    
}
#endif /* NOT USED */

static gint32
mygimp_layer_copy (gint32 layer_ID)
{
  GParam *return_vals;
  int nreturn_vals;

  return_vals = gimp_run_procedure ("gimp_layer_copy",
                                    &nreturn_vals,
                                    PARAM_LAYER, layer_ID,
				    PARAM_INT32, 0,
                                    PARAM_END);

  layer_ID = -1;
  if (return_vals[0].data.d_status == STATUS_SUCCESS)
    layer_ID = return_vals[1].data.d_layer;

  gimp_destroy_params (return_vals, nreturn_vals);

  return layer_ID;
}

static void
paint_layer_copy(gchar *new_name)
{
  gint32 old_drawable = gfig_drawable;
  if((gfig_drawable = mygimp_layer_copy(gfig_drawable)) < 0)
    {
      g_warning("Error in copy layer for onlayers\n");
      gfig_drawable = old_drawable;
      return;
    }

  gimp_layer_set_name(gfig_drawable,new_name);
  gimp_image_add_layer(gfig_image,gfig_drawable,-1);
}

static void
paint_layer_new(gchar *new_name)
{
  gint32 layer_id;
  gint32 fill_type;
  int isgrey = 0;

  switch ( gimp_drawable_type (gfig_select_drawable->id) )
  {
  case GRAYA_IMAGE:
  case GRAY_IMAGE:
    isgrey = 2;
  default:
    break;
  }

  if((layer_id = gimp_layer_new(gfig_image,
				new_name,
				img_width,
				img_height,
				1 + isgrey, /* RGBA or GRAYA type */
				100.0, /* opacity */
				0 /* mode */)) < 0)
    printf("Error in creating\n");
  else
    {
      gimp_image_add_layer(gfig_image,layer_id,-1);
      gimp_drawable_fill(layer_id,1);
    }
  
  gfig_drawable = layer_id;
  
  switch(selvals.onlayerbg)
    {
    case LAYER_TRANS_BG:
      fill_type = 2;
      break;
    case LAYER_BG_BG:
      fill_type = 0;
      break;
    case LAYER_WHITE_BG:
      fill_type = 1;
      break;
    case LAYER_COPY_BG:
    default:
      fill_type = 1;
      g_warning("Paint layer new internal error %d\n",selvals.onlayerbg);
      break;
    }
  /* Have to clear layer out since creating transparent layer
   * seems to leave rubbish in it.
   */

  gimp_drawable_fill(layer_id,fill_type);

}

static void
paint_layer_fill()
{
  GParam *return_vals;
  int nreturn_vals;

  return_vals = gimp_run_procedure ("gimp_bucket_fill",
                                    &nreturn_vals,
				    PARAM_IMAGE, gfig_image,
				    PARAM_DRAWABLE, gfig_drawable,
				    PARAM_INT32, selopt.fill_type, /* Fill mode */
				    PARAM_INT32, 0, /* NORMAL */
				    PARAM_FLOAT, (gdouble)selopt.fill_opacity, /* Fill opacity */
				    PARAM_FLOAT, (gdouble)0.0, /* threshold - ignored */
				    PARAM_INT32, 0, /* Sample merged - ignored */
				    PARAM_FLOAT, (gdouble)0.0, /* x - ignored */
				    PARAM_FLOAT, (gdouble)0.0, /* y - ignored */
                                    PARAM_END);

  gimp_destroy_params (return_vals, nreturn_vals);
}
       
static void
gfig_paint_callback(GtkWidget *widget,
		  gpointer   data)
{
  DALLOBJS * objs;
  gint layer_count = 0;
  gchar buf[128];
  gint ccount = 0;
  BRUSHDESC *bdesc;

  objs = current_obj->obj_list;


  /* Set the brush up */
  bdesc = g_object_get_data (G_OBJECT (brush_page_pw), "user_data");

  if(bdesc)
    mygimp_brush_set(bdesc->bname);

  while(objs)
    {

      if(ccount == obj_show_single || obj_show_single == -1)
	{
	  sprintf(buf,"Gfig Layer %d",layer_count++);
	  
	  if(selvals.painttype != PAINT_SELECTION_TYPE)
	    {
	      switch(selvals.onlayers)
		{
		case SINGLE_LAYER:
		  if(layer_count == 1)
		    {
		      if(selvals.onlayerbg == LAYER_COPY_BG)
			paint_layer_copy(buf);
		      else
			paint_layer_new(buf);
		    }
		  break;
		case MULTI_LAYER:
		  if(selvals.onlayerbg == LAYER_COPY_BG)
		    paint_layer_copy(buf);
		  else
		    paint_layer_new(buf);
		  break;
		case ORIGINAL_LAYER:
		  /* Just use the given layer */
		  break;
		default:
		  g_warning("Error in onlayers val %d\n",selvals.onlayers);
		  break;
		}
	    }
	  
	  objs->obj->paintfunc(objs->obj);
	  
	  /* Fill layer if required */
	  if(selvals.painttype == PAINT_SELECTION_FILL_TYPE 
	     && selopt.fill_when == FILL_EACH)
	    paint_layer_fill();
	}

      objs = objs->next;
      
      ccount++;
    }

  /* Fill layer if required */
  if(selvals.painttype == PAINT_SELECTION_FILL_TYPE 
     && selopt.fill_when == FILL_AFTER)
    paint_layer_fill();

  gimp_displays_flush();
}

static void
reload_button_press(GtkWidget *widget,
		    gpointer   data)
{
  refill_cache();
  draw_grid_clear(widget,data);
}

static void
about_button_press(GtkWidget *widget,
		   gpointer   data)
{
  /* Display the about box */
  static const gchar *lines[] =
  {
    "Gfig - GIMP plug-in",
    "Release 1.3",
    "Andy Thomas",
    "Email alt@picnic.demon.co.uk",
    "http://www.picnic.demon.co.uk/",
    "Isometric grid By Rob Saunders"
  };
  GtkWidget *window = NULL;
  GtkWidget *label;
  GtkWidget *hbox;
  GtkWidget *vbox;
  GtkWidget *pm;
  gint       i;

  window = gimp_dialog_new ("About");
  if (gfig_top_level)
    gtk_window_set_transient_for (GTK_WINDOW (window),
				  GTK_WINDOW (gfig_top_level));

  gimp_dialog_add_button (window, "OK", G_CALLBACK (ok_warn_window),
			  window, TRUE);

  /* Bits and bobs */
  pm = gfig_new_pixmap(window,rulers_comp_xpm);

  hbox = gimp_hbox_new(FALSE,1);

  vbox = gimp_vbox_new(FALSE,1);

  gimp_box_pack_start (hbox, pm, TRUE, TRUE, 0);
  gimp_box_pack_start (hbox, vbox, TRUE, TRUE, 0);
  gimp_box_pack_start (gimp_dialog_get_vbox (window), hbox, TRUE, TRUE, 0);

  for (i = 0; i < G_N_ELEMENTS (lines); i++)
    {
      label = gtk_label_new(lines[i]);
      gimp_container_set_border_width (label, 2);
      gimp_box_pack_start (vbox, label, FALSE, FALSE, 0);
    }

  gtk_window_present (GTK_WINDOW (window));
}


static void
save_button_press(GtkWidget *widget,
		  gpointer   data)
{
  gfig_save();  /* Save current object */
}

static void
rescan_button_press(GtkWidget *widget,
		    gpointer   data)
{
  gfig_rescan_list();
}

static GtkWidget *
new_gfig_obj(gchar * name)
{
  GFIGOBJ * gfig;
  GtkWidget * new_list_item;
  /* Create a new entry */

  gfig = gfig_new();

  if(!name)
    name = "New gfig obj";

  gfig->draw_name = g_strdup(name);

  /* Leave options as before */
  pic_obj = current_obj = gfig;

  new_list_item = gfig_list_add(gfig);

  tmp_bezier = obj_creating = tmp_line = NULL;

  /* Redraw areas */
  update_draw_area(gfig_preview,NULL);
  list_button_update(gfig);
  return(new_list_item);
}

static void
new_button_press(GtkWidget *widget,
		 gpointer   data)
{
  GtkWidget * new_list_item;

  new_list_item = new_gfig_obj((gchar*)data);
  gfig_dialog_edit_list(new_list_item,current_obj,TRUE);
}

static GtkWidget *delete_dialog = NULL;

static void
delete_dialog_destroyed(GtkWidget *widget,
			gpointer   data)
{
  if (delete_frame_to_freeze)
    gtk_widget_set_sensitive(delete_frame_to_freeze,TRUE);
  delete_dialog = NULL;
}

static void
delete_button_press_cancel(GtkWidget *widget,
			   gpointer   data)
{
  if (delete_dialog)
    gtk_window_destroy(GTK_WINDOW (delete_dialog));
  delete_dialog = NULL;
}

static void
delete_button_press_ok(GtkWidget *widget,
		       gpointer   data)
{
  gint pos;
  GtkListBoxRow *selrow;
  GtkListBoxRow *row;
  GFIGOBJ * sel_obj;
  GtkWidget *list = (GtkWidget *)data;

#ifdef DEBUG
  printf("Delete button pressed\n");
#endif /* DEBUG */
  /* Must update which object we are editing */
  /* Get the list and which item is selected */
  /* Only allow single selections */

  selrow = gtk_list_box_get_selected_row (GTK_LIST_BOX (list));
  if (!selrow)
    return;

  sel_obj = (GFIGOBJ *)g_object_get_data (G_OBJECT (selrow), "user_data");

  pos = gtk_list_box_row_get_index (selrow);
#ifdef DEBUG
  printf("delete pos = %d\n",pos);
#endif /* DEBUG */

  /* Delete the current  item + asssociated file */
  gtk_list_box_remove (GTK_LIST_BOX (gfig_gtk_list), GTK_WIDGET (selrow));
  /* Shadow copy for ordering info */
  gfig_list = g_list_remove(gfig_list,sel_obj);

  if(sel_obj == current_obj)
    {
      clear_undo();
    }

  /* Free current obj */
  gfig_free_everything(sel_obj);


  /* Select previous one */
  pos--;

  if(pos < 0 && g_list_length(gfig_list) == 0)
    {
      /* Warning - we have a problem here
       * since we are not really "creating an entry"
       * why call gfig_new?
       */
      new_button_press(NULL,NULL);
      pos = 0;
    }
  else if (pos < 0)
    pos = 0;

  if(delete_dialog)
    gtk_window_destroy(GTK_WINDOW (delete_dialog));

  delete_dialog = NULL;

  row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (gfig_gtk_list), pos);
  if (row)
    gtk_list_box_select_row (GTK_LIST_BOX (gfig_gtk_list), row);

  current_obj = g_list_nth(gfig_list,pos)->data;

  update_draw_area(gfig_preview,NULL);

  list_button_update(current_obj);

  gfig_update_stat_labels();
}

static void
gfig_delete_gfig_callback(GtkWidget *widget,
			  gpointer   data)
{
  GtkWidget *vbox;
  GtkWidget *label;
  char      *str;
  GtkWidget *list = (GtkWidget *)data;
  GFIGOBJ * sel_obj;


  sel_obj = gfig_list_selected_obj (list);
  if(delete_dialog || !sel_obj)
    return;

  delete_dialog = gimp_dialog_new("Delete gfig drawing");
  if (gfig_top_level)
    gtk_window_set_transient_for (GTK_WINDOW (delete_dialog),
				  GTK_WINDOW (gfig_top_level));
  g_signal_connect (delete_dialog, "destroy",
		    G_CALLBACK (delete_dialog_destroyed), NULL);

  vbox = gimp_vbox_new(FALSE, 0);
  gimp_container_set_border_width (vbox, 8);
  gimp_box_pack_start (gimp_dialog_get_vbox (delete_dialog), vbox,
		       FALSE, FALSE, 0);

  /* Question */

  label = gtk_label_new("Are you sure you want to delete");
  gimp_misc_set_alignment (label, 0.0, 0.0);
  gimp_box_pack_start (vbox, label, FALSE, FALSE, 0);

  str = g_strdup_printf ("\"%s\" from the list and from disk?", sel_obj->draw_name);

  label = gtk_label_new(str);
  gimp_misc_set_alignment (label, 0.0, 0.0);
  gimp_box_pack_start (vbox, label, FALSE, FALSE, 0);

  g_free(str);

  /* Buttons */
  gimp_dialog_add_button (delete_dialog, "Delete",
			  G_CALLBACK (delete_button_press_ok), data, TRUE);

  gimp_dialog_add_button (delete_dialog, "Cancel",
			  G_CALLBACK (delete_button_press_cancel), data, FALSE);

  /* Show! */

  gtk_widget_set_sensitive(GTK_WIDGET(delete_frame_to_freeze), FALSE);
  gtk_window_present (GTK_WINDOW (delete_dialog));
}

static void
gfig_update_stat_labels()
{
  gchar str[45];

  if(current_obj->draw_name)
    g_snprintf(str,sizeof (str),"%.34s",current_obj->draw_name);
  else
    g_snprintf(str,sizeof (str),"<NONE>");

  gtk_label_set_text(GTK_LABEL(status_label_dname),str);

  if(current_obj->filename)
    {
      gint slen;
      const gchar *hm = g_get_home_dir ();
      gchar *dfn = g_strdup(current_obj->filename);

      if(hm && strlen (hm) > 0 && !strncmp(dfn,hm,strlen(hm)))
	 {
	   gchar *tmp = g_strconcat ("~", &dfn[strlen(hm)], NULL);

	   g_free (dfn);
	   dfn = tmp;
	 }
      if((slen = strlen(dfn)) > 40)
	{
	  strncpy(str,dfn,19);
	  str[19] = '\0';
	  strcat(str,"...");
	  strncat(str,&dfn[slen - 21],19);
	  str[40] ='\0';
	}
      else
	g_snprintf(str,sizeof (str),"%.40s",dfn);
      g_free(dfn);
    }
  else
    g_snprintf(str,sizeof (str),"<NONE>");

  gtk_label_set_text(GTK_LABEL(status_label_fname),str);

}

static void
new_obj_2edit(GFIGOBJ *obj)
{
  GFIGOBJ * old_current = current_obj;

  /* Clear undo levels */
  /* redraw the preview */
  /* Set up options as define in the selected object */

  clear_undo();

  /* Point at this one */
  current_obj = obj;

  /* Show all objects to start with */
  obj_show_single = -1;

  /* Change options */
  update_options(old_current);

  /* If have old object and NOT scaleing currently then force
   * back to saved coord type.
   */

  gfig_update_stat_labels();

  /* redraw with new */
  update_draw_area(gfig_preview,NULL);
  /* And preview */
  list_button_update(current_obj);

  if(obj->obj_status & GFIG_READONLY)
    {
      create_warn_dialog("Editing read-only object - you will not be able to save it");
      gtk_widget_set_sensitive(save_button,FALSE);
    }
  else
    {
      gtk_widget_set_sensitive(save_button,TRUE);
    }
}

static void
edit_button_press(GtkWidget *widget,
		  gpointer data)
{
  GFIGOBJ * sel_obj;
  GtkWidget *list = (GtkWidget *)data;

#ifdef DEBUG
  printf("Edit button pressed\n");
#endif /* DEBUG */
  /* Must update which object we are editing */
  /* Get the list and which item is selected */
  /* Only allow single selections */

  sel_obj = gfig_list_selected_obj (list);

  if(sel_obj)
    new_obj_2edit(sel_obj);
  else
    g_warning("Internal error - list item has null object!");
}

static void
merge_button_press(GtkWidget *widget,
		   gpointer   data)
{
  GFIGOBJ * sel_obj;
  DALLOBJS * obj_copies;
  GtkWidget *list = (GtkWidget *)data;

#ifdef DEBUG
  printf("Merge button pressed\n");
#endif /* DEBUG */
  /* Must update which object we are editing */
  /* Get the list and which item is selected */
  /* Only allow single selections */

  sel_obj = gfig_list_selected_obj (list);
  if(sel_obj && sel_obj->obj_list && sel_obj != current_obj)
    {
      /* Copy list tag onto current & redraw */
      obj_copies = copy_all_objs(sel_obj->obj_list);
      prepend_to_all_obj(current_obj,obj_copies);

      /* redraw all */
      update_draw_area(gfig_preview,NULL);
      /* And preview */
      list_button_update(current_obj);
    }
}


static void
gfig_save_menu_callback(GtkWidget *widget, gpointer data)
{
  GFIGOBJ * real_current = current_obj;
  /* Fiddle the current object and save it */
  /* What happens if we get a redraw here ? */

  current_obj = gfig_obj_for_menu;

  gfig_save();  /* Save current object */

  current_obj = real_current;
}

static void
gfig_edit_menu_callback(GtkWidget *widget, gpointer data)
{
  new_obj_2edit(gfig_obj_for_menu);
}

static void
gfig_rename_menu_callback(GtkWidget *widget, gpointer data)
{
  create_file_selection(gfig_obj_for_menu,gfig_obj_for_menu->filename);
}

static void
gfig_copy_menu_callback(GtkWidget *widget, gpointer data)
{
  /* Create new entry with name + copy at end & copy object into it */
  gchar *new_name = g_strdup_printf ("%s copy", gfig_obj_for_menu->draw_name);

  new_gfig_obj(new_name);
  g_free(new_name);

  /* Copy objs across */
  current_obj->obj_list = copy_all_objs(gfig_obj_for_menu->obj_list);
  current_obj->opts = gfig_obj_for_menu->opts; /* Structure copy */

  /* redraw all */
  update_draw_area(gfig_preview,NULL);
  /* And preview */
  list_button_update(current_obj);
}

/* The popup menu of the list: a popover holding a button per item */

static void
gfig_op_menu_item_clicked (GtkWidget *button,
			   gpointer   data)
{
  GtkWidget *popover = gtk_widget_get_ancestor (button, GTK_TYPE_POPOVER);
  GCallback  callback = (GCallback) data;

  if (popover)
    gtk_popover_popdown (GTK_POPOVER (popover));

  ((void (*) (GtkWidget *, gpointer)) callback) (button, NULL);
}

static gboolean
gfig_op_menu_unparent (gpointer data)
{
  GtkWidget *popover = data;

  if (gtk_widget_get_parent (popover))
    gtk_widget_unparent (popover);
  g_object_unref (popover);

  return G_SOURCE_REMOVE;
}

static void
gfig_op_menu_closed (GtkPopover *popover,
		     gpointer    data)
{
  /* The buttons may still be running their callbacks */
  g_idle_add (gfig_op_menu_unparent, g_object_ref (popover));
}

static GtkWidget *
gfig_op_menu_add_item (GtkWidget   *box,
		       const gchar *label,
		       GCallback    callback)
{
  GtkWidget *button;

  button = gtk_button_new_with_label (label);
  gtk_button_set_has_frame (GTK_BUTTON (button), FALSE);
  gtk_label_set_xalign (GTK_LABEL (gtk_button_get_child (GTK_BUTTON (button))),
			0.0);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (gfig_op_menu_item_clicked), (gpointer) callback);
  gtk_box_append (GTK_BOX (box), button);

  return button;
}

static void
gfig_op_menu_popup(GtkWidget *widget, gdouble x, gdouble y, GFIGOBJ *obj)
{
  GtkWidget   *popover;
  GtkWidget   *box;
  GtkWidget   *save_menu_item;
  GdkRectangle rect;

  gfig_obj_for_menu = obj; /* Static data again!*/

  popover = gtk_popover_new ();
  gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);
  gtk_widget_set_parent (popover, widget);
  g_signal_connect (popover, "closed",
		    G_CALLBACK (gfig_op_menu_closed), NULL);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_popover_set_child (GTK_POPOVER (popover), box);

  save_menu_item = gfig_op_menu_add_item (box, "Save",
					  G_CALLBACK (gfig_save_menu_callback));
  gfig_op_menu_add_item (box, "Save as...",
			 G_CALLBACK (gfig_rename_menu_callback));
  gfig_op_menu_add_item (box, "Copy",
			 G_CALLBACK (gfig_copy_menu_callback));
  gfig_op_menu_add_item (box, "Edit",
			 G_CALLBACK (gfig_edit_menu_callback));

  if(obj->obj_status & GFIG_READONLY)
    {
      gtk_widget_set_sensitive(save_menu_item,FALSE);
    }
  else
    {
      gtk_widget_set_sensitive(save_menu_item,TRUE);
    }

  rect.x = x;
  rect.y = y;
  rect.width = 1;
  rect.height = 1;
  gtk_popover_set_pointing_to (GTK_POPOVER (popover), &rect);
  gtk_popover_popup (GTK_POPOVER (popover));
}


static void
list_button_press(GtkGestureClick *gesture,
		  gint             n_press,
		  gdouble          x,
		  gdouble          y,
		  gpointer         data)
{
  GtkWidget *widget;
  guint      button;

  widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));

  if (n_press == 1)
    {
#ifdef DEBUG
      printf("Single button press\n");
#endif /* DEBUG */
      if(button == 3)
	{
#ifdef DEBUG
	  printf("Popup on '%s'\n",((GFIGOBJ *)data)->draw_name);
#endif /* DEBUG */
	  gfig_op_menu_popup(widget,x,y,(GFIGOBJ *)data);
	  return;
	}
      list_button_update((GFIGOBJ *)data);
    }
  else if (n_press == 2)
    {
#ifdef DEBUG
      printf("Two button press\n");
#endif /* DEBUG */
      gfig_dialog_edit_list(widget,data,FALSE);
    }
}

static void
gfig_entry_update(GtkWidget *widget, gint *value)
{
  GtkAdjustment *adjustment;
  gdouble        new_value;

  new_value = atoi(gtk_editable_get_text (GTK_EDITABLE (widget)));

  if (*value != new_value) {
    adjustment = g_object_get_data (G_OBJECT (widget), "user_data");

    if ((new_value >= gtk_adjustment_get_lower (adjustment)) &&
	(new_value <= gtk_adjustment_get_upper (adjustment))) {
      *value            = new_value;
      gtk_adjustment_set_value (adjustment, new_value);

      /*dialog_update_preview();*/
    }
  }
}

static void
gfig_scale_update(GtkAdjustment *adjustment, gint *value)
{
  GtkWidget *entry;
  char       buf[256];

  if (*value != gtk_adjustment_get_value (adjustment)) {
    *value = gtk_adjustment_get_value (adjustment);

    entry = g_object_get_data (G_OBJECT (adjustment), "user_data");
    if (entry)
      {
	g_snprintf(buf,sizeof (buf),"%d",*value);

	g_signal_handlers_block_matched (entry, G_SIGNAL_MATCH_DATA,
					 0, 0, NULL, NULL, value);
	gtk_editable_set_text (GTK_EDITABLE (entry), buf);
	g_signal_handlers_unblock_matched (entry, G_SIGNAL_MATCH_DATA,
					   0, 0, NULL, NULL, value);
      }

    /*dialog_update_preview(); */
  }
}


static void
gfig_scale_update_scale(GtkAdjustment *adjustment, gdouble *value)
{

  if (*value != gtk_adjustment_get_value (adjustment)) {
    *value = gtk_adjustment_get_value (adjustment);
    if(!selvals.scaletoimage)
      {
	scale_x_factor = (1/(*value)) * org_scale_x_factor;
	scale_y_factor = (1/(*value)) * org_scale_y_factor;
	update_draw_area(gfig_preview,NULL);
      }
  }
}


static void
gfig_scale_update_fp(GtkAdjustment *adjustment, gdouble *value)
{
  GtkWidget *entry;
  char       buf[256];

  if (*value != gtk_adjustment_get_value (adjustment)) {
    *value = gtk_adjustment_get_value (adjustment);

    entry = g_object_get_data (G_OBJECT (adjustment), "user_data");
    if(entry)
      {
	g_snprintf(buf,sizeof (buf),"%0.3f",*value);

	g_signal_handlers_block_matched (entry, G_SIGNAL_MATCH_DATA,
					 0, 0, NULL, NULL, value);
	gtk_editable_set_text (GTK_EDITABLE (entry), buf);
	g_signal_handlers_unblock_matched (entry, G_SIGNAL_MATCH_DATA,
					   0, 0, NULL, NULL, value);
      }

    /*dialog_update_preview(); */
  }
}

/* Use to toggle the toggles */
static void
gfig_scale2img_update(GtkWidget *widget,
                      gpointer   data)
{
  gint *val = (gint *)data;
  GtkAdjustment *adjustment;
  GtkWidget *sw;

  adjustment = g_object_get_data (G_OBJECT (widget), "user_data");
  sw = g_object_get_data (G_OBJECT (adjustment), "user_data");

  if(*val)
    {
      *val = 0;
      gtk_widget_set_sensitive(GTK_WIDGET(sw),TRUE);
    }
  else
    {
      scale_x_factor = org_scale_x_factor;
      scale_y_factor = org_scale_y_factor;
      gtk_adjustment_set_value (adjustment, 1.0);
      *val = 1;
      gtk_widget_set_sensitive(GTK_WIDGET(sw),FALSE);
    }
  update_draw_area(gfig_preview,NULL);
}


/* Use to toggle the toggles */
static void
gfig_toggle_update(GtkWidget *widget,
                      gpointer   data)
{
  gint *val = (gint *)data;

  if(*val)
    *val = 0;
  else
    *val = 1;
}

/* Given a row then srink it down a bit */
static void
do_gfig_preview(guchar *dest_row, 
	    guchar *src_row,
	    gint width,
	    gint dh,
	    gint height,
	    gint bpp)
{
  memcpy(dest_row,src_row,width*bpp);
}

/* Copies a row of RGB pixels into the preview back buffer */
static void
gfig_preview_put_row (guchar *row,
		      gint    y)
{
  guint32 *dest;
  gint     x;

  if (!gfig_back_surface)
    gfig_back_surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
						    preview_width,
						    preview_height);

  dest = (guint32 *) (cairo_image_surface_get_data (gfig_back_surface)
		      + y * cairo_image_surface_get_stride (gfig_back_surface));

  for (x = 0; x < preview_width; x++, row += 3)
    dest[x] = ((guint32) row[0] << 16) | ((guint32) row[1] << 8) | row[2];
}

static void
dialog_update_preview(void)
{
  gint y;
  gint check,check_0,check_1;

  if (!gfig_back_surface)
    gfig_back_surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
						    preview_width,
						    preview_height);
  cairo_surface_flush (gfig_back_surface);

  if(!selvals.showimage)
    {
      memset(preview_row,-1,preview_width*4);
      for (y = 0; y < preview_height; y++) {
	gfig_preview_put_row(preview_row, y);
      }
      cairo_surface_mark_dirty (gfig_back_surface);
      update_draw_area(gfig_preview,NULL);
      return;
    }

  if(!pv_cache)
    {
      refill_cache();
    }

  for (y = 0; y < preview_height; y++) {

    if ((y / CHECK_SIZE) & 1) {
      check_0 = CHECK_DARK;
      check_1 = CHECK_LIGHT;
    } else {
      check_0 = CHECK_LIGHT;
      check_1 = CHECK_DARK;
    }

    do_gfig_preview(preview_row,
		    pv_cache+y*preview_width*img_bpp,
		    preview_width,
		    y,
		    preview_height,
		    img_bpp);

    if(img_bpp > 3)
      {
	int i,j;
	for (i = 0, j = 0 ; i < sizeof(preview_row); i += 4, j += 3 )
	  {
	    gint alphaval;
	    if (((i/4) / CHECK_SIZE) & 1)
	      check = check_0;
	    else
	      check = check_1;

	    alphaval = preview_row[i + 3];

	    preview_row[j] =
	      check + (((preview_row[i] - check)*alphaval)/255);
	    preview_row[j + 1] =
	      check + (((preview_row[i + 1] - check)*alphaval)/255);
	    preview_row[j + 2] =
	      check + (((preview_row[i + 2] - check)*alphaval)/255);
	  }
      }

    gfig_preview_put_row(preview_row, y);
  }

  cairo_surface_mark_dirty (gfig_back_surface);
  update_draw_area(gfig_preview,NULL);
}

static gint 
get_num_radials()
{
  gint gridsp = MAX_GRID + MIN_GRID;
  /* select number of radials to draw */
  /* Either have 16 32 or 48 */
  /* correspond to GRID_MAX, midway and GRID_MIN */

  return(gridsp - selvals.opts.gridspacing);
}

#define SQ_SIZE 8

static gint 
inside_sqr(GfigPoint *cpnt, GfigPoint *testpnt)
{
  /* Return TRUE if testpnt is near cpnt */
  gint16 x = cpnt->x;
  gint16 y = cpnt->y;
  gint16 tx = testpnt->x;
  gint16 ty = testpnt->y;

#ifdef DEBUG
  printf("Testing if (%x,%x) is near (%x,%x)\n",tx,ty,x,y);
#endif /* DEBUG */
  return(abs(x - tx) <= SQ_SIZE && abs (y - ty) < SQ_SIZE );
}

/* find_grid_pos - Given an x,y point return the grid position of it */
/* return the new position in the passed point */

static void
find_grid_pos(GfigPoint *p,GfigPoint *gp,guint is_butt3)
{
  gint16 x = p->x;
  gint16 y = p->y;
  static GfigPoint cons_pnt;
  static gdouble cons_radius;
  static gdouble cons_ang;
  static gboolean cons_center;
  
  if(selvals.opts.gridtype == RECT_GRID)
    {
      if(p->x % selvals.opts.gridspacing > selvals.opts.gridspacing/2)
	x += selvals.opts.gridspacing;
      
      if(p->y % selvals.opts.gridspacing > selvals.opts.gridspacing/2)
	y += selvals.opts.gridspacing;
      
      gp->x = (x/selvals.opts.gridspacing)*selvals.opts.gridspacing;
      gp->y = (y/selvals.opts.gridspacing)*selvals.opts.gridspacing;

      if(is_butt3)
	{
	  if(abs(gp->x - cons_pnt.x) < abs(gp->y - cons_pnt.y))
	    gp->x = cons_pnt.x;
	  else
	    gp->y = cons_pnt.y;
	}
      else
	{
	  /* Store the point since might be used later */
	  cons_pnt = *gp; /* Structure copy */
	}
    }
  else if(selvals.opts.gridtype == POLAR_GRID)
    { 
      gdouble ang_grid;
      gdouble ang_radius;
      gdouble real_radius;
      gdouble real_angle;
      gdouble rounded_angle;
      gint rounded_radius;
      gint16 shift_x = x - preview_width/2;
      gint16 shift_y = -y + preview_height/2;

      real_radius = ang_radius = sqrt((shift_y*shift_y) + (shift_x*shift_x));

      /* round radius */
      rounded_radius = (gint)(rint(ang_radius/selvals.opts.gridspacing))*selvals.opts.gridspacing;
      if(rounded_radius <= 0 || real_radius <=0)
	{
	  /* DEAD CENTER */
	  gp->x = preview_width/2;
	  gp->y = preview_height/2;
	  if (!is_butt3) cons_center = TRUE;
#ifdef DEBUG
	  printf("Dead center\n");
#endif /* DEBUG */
	  return;
	}

      ang_grid = 2*M_PI/get_num_radials();

      real_angle = atan2(shift_y,shift_x);
      if(real_angle < 0)
	real_angle += 2*M_PI;

      rounded_angle = (rint((real_angle/ang_grid)))*ang_grid;
#ifdef DEBUG
      printf("real_ang = %f ang_gid = %f rounded_angle = %f rounded radius = %d\n",
	     real_angle,ang_grid,rounded_angle,rounded_radius);

      printf("preview_width = %d preview_height = %d\n",preview_width,preview_height);
#endif /* DEBUG */
      gp->x = (gint)rint((rounded_radius*cos(rounded_angle))) + preview_width/2;
      gp->y = -(gint)rint((rounded_radius*sin(rounded_angle))) + preview_height/2;

      if(is_butt3)
	{
	  if(!cons_center)
	    {
	      if(fabs(rounded_angle - cons_ang) > ang_grid/2)
		{
		  gp->x = (gint)rint((cons_radius*cos(rounded_angle))) + preview_width/2;
		  gp->y = -(gint)rint((cons_radius*sin(rounded_angle))) + preview_height/2;
		}
	      else
		{
		  gp->x = (gint)rint((rounded_radius*cos(cons_ang))) + preview_width/2;
		  gp->y = -(gint)rint((rounded_radius*sin(cons_ang))) + preview_height/2;
		}
	    }
	}
      else
	{
	  cons_radius = rounded_radius;
	  cons_ang = rounded_angle;
	  cons_center = FALSE;
	}
    }
   else if(selvals.opts.gridtype == ISO_GRID)
     {
	if(is_butt3)
	  {
	     static GfigPoint b_pnt;
	     static GfigPoint i_pnt;
	     static GfigPoint ii_pnt;
	     gint d;
	     gint dd;

	     b_pnt.x = cons_pnt.x;
	     b_pnt.y = cons_pnt.y + preview_width;
	     d = calculate_point_to_line_distance(p, &cons_pnt, &b_pnt, &i_pnt);

	     b_pnt.x = cons_pnt.x;
	     b_pnt.y = cons_pnt.y - preview_width;
	     dd = calculate_point_to_line_distance(p, &cons_pnt, &b_pnt, &ii_pnt);
	     if (dd < d)
	     {
		i_pnt.x = ii_pnt.x;
		i_pnt.y = ii_pnt.y;
		d = dd;
	     }

	     b_pnt.x = cons_pnt.x + preview_width;
	     b_pnt.y = cons_pnt.y + preview_width/2;
	     dd = calculate_point_to_line_distance(p, &cons_pnt, &b_pnt, &ii_pnt);
	     if (dd < d)
	     {
		i_pnt.x = ii_pnt.x;
		i_pnt.y = ii_pnt.y;
		d = dd;
	     }

	     b_pnt.x = cons_pnt.x + preview_width;
	     b_pnt.y = cons_pnt.y - preview_width/2;
	     dd = calculate_point_to_line_distance(p, &cons_pnt, &b_pnt, &ii_pnt);
	     if (dd < d)
	     {
		i_pnt.x = ii_pnt.x;
		i_pnt.y = ii_pnt.y;
		d = dd;
	     }

	     b_pnt.x = cons_pnt.x - preview_width;
	     b_pnt.y = cons_pnt.y + preview_width/2;
	     dd = calculate_point_to_line_distance(p, &cons_pnt, &b_pnt, &ii_pnt);
	     if (dd < d)
	     {
		i_pnt.x = ii_pnt.x;
		i_pnt.y = ii_pnt.y;
		d = dd;
	     }

	     b_pnt.x = cons_pnt.x - preview_width;
	     b_pnt.y = cons_pnt.y - preview_width/2;
	     dd = calculate_point_to_line_distance(p, &cons_pnt, &b_pnt, &ii_pnt);
	     if (dd < d)
	     {
		i_pnt.x = ii_pnt.x;
		i_pnt.y = ii_pnt.y;
		d = dd;
	     }

	     x = i_pnt.x;
	     y = i_pnt.y;
	  }

	if(x % selvals.opts.gridspacing > selvals.opts.gridspacing/2)
	  x += selvals.opts.gridspacing;

	gp->x = (x/selvals.opts.gridspacing)*selvals.opts.gridspacing;

	if (((gp->x/selvals.opts.gridspacing) % 2) != 0)
	  {
	     y -= selvals.opts.gridspacing/2;

	     if(y % selvals.opts.gridspacing > selvals.opts.gridspacing/2)
	       y += selvals.opts.gridspacing;
	
	     gp->y = (selvals.opts.gridspacing/2) + ((y/selvals.opts.gridspacing)*selvals.opts.gridspacing);
	  }
	else
	  {
	     if(y % selvals.opts.gridspacing > selvals.opts.gridspacing/2)
	       y += selvals.opts.gridspacing;
	
	     gp->y = (y/selvals.opts.gridspacing)*selvals.opts.gridspacing;
	  }

	if (!is_butt3)
	  {
	     /* Store the point since it might be used later */
	     cons_pnt = *gp; /* Structure copy */
	  }
     }
}

/* Calculate distance from a point to a line
 * Taken from the newsgroup comp.graphics.algorithms FAQ. */
static int
calculate_point_to_line_distance(GfigPoint *p, GfigPoint *A, GfigPoint *B, GfigPoint *I)
{
   gint L2;
   gint L;

   L2 = ((B->x - A->x)*(B->x - A->x)) + ((B->y - A->y)*(B->y - A->y));
   L = (gint) sqrt(L2);

   /* gint r; */
   /* gint s; */
   /* r = ((A->y - p->y)*(A->y - B->y) - (A->x - p->x)*(B->x - A->x))/L2; */
   /* s = ((A->y - p->y)*(B->x - A->x) - (A->x - p->x)*(B->y - A->y))/L2; */

   /* Let I be the point of perpendicular projection of C onto AB. */

   I->x = A->x + (((A->y - p->y)*(A->y - B->y) - (A->x - p->x)*(B->x - A->x))*(B->x - A->x))/L2;
   I->y = A->y + (((A->y - p->y)*(A->y - B->y) - (A->x - p->x)*(B->x - A->x))*(B->y - A->y))/L2;

   return abs((((A->y - p->y)*(B->x - A->x)) - ((A->x - p->x)*(B->y - A->y)))*L);
}

/* Given a point x,y draw a circle */
static void
draw_circle(GfigPoint *p)
{
  if(!selvals.opts.showcontrol || drawing_pic)
    return;

  gfig_draw_arc (0,
		p->x - SQ_SIZE/2,
		p->y - SQ_SIZE/2,
		SQ_SIZE,
		SQ_SIZE,
		0,
		360*64);
}


/* Given a point x,y draw a square around it */
static void
draw_sqr(GfigPoint *p)
{
  if(!selvals.opts.showcontrol || drawing_pic)
    return;

  gfig_draw_rectangle (0,
		     gfig_scale_x((gint)p->x) - SQ_SIZE/2,
		     gfig_scale_y((gint)p->y) - SQ_SIZE/2,
		     (gint)SQ_SIZE,
		     (gint)SQ_SIZE);
}

/* Draw the grid on the screen
 */

static void
draw_grid_clear(GtkWidget *widget,
	  gpointer   data)
{
  /* wipe slate and start again: the draw function paints the grid and
   * the objects over the new image
   */
  dialog_update_preview();
  update_draw_area(gfig_preview,NULL);
}

/* Tool tips: GTK 4 has no GtkTooltips group, so the widgets with a tip
 * are remembered to turn them all on or off.
 */
static gboolean gfig_tooltips_enabled = TRUE;

static void
gfig_tooltip_widget_gone (gpointer  data,
			  GObject  *where_the_object_was)
{
  gfig_tooltip_widgets = g_slist_remove (gfig_tooltip_widgets,
					 where_the_object_was);
}

static void
gfig_set_tooltip (GtkWidget   *widget,
		  const gchar *tip)
{
  gtk_widget_set_tooltip_text (widget, tip);
  gtk_widget_set_has_tooltip (widget, gfig_tooltips_enabled);

  if (!g_slist_find (gfig_tooltip_widgets, widget))
    {
      gfig_tooltip_widgets = g_slist_prepend (gfig_tooltip_widgets, widget);
      g_object_weak_ref (G_OBJECT (widget), gfig_tooltip_widget_gone, NULL);
    }
}

static void
toggle_tooltips(GtkWidget *widget,
	  gpointer   data)
{
  gint tt = *((gint *)data);
  GSList *list;

  gfig_tooltips_enabled = tt ? FALSE : TRUE;

  for (list = gfig_tooltip_widgets; list; list = list->next)
    gtk_widget_set_has_tooltip (GTK_WIDGET (list->data),
				gfig_tooltips_enabled);
}


static void
toggle_show_image(GtkWidget *widget,
	  gpointer   data)
{
  /* wipe slate and start again */
  draw_grid_clear(widget,data);
}


static void
toggle_obj_type(GtkWidget *widget,
	  gpointer   data)
{
  const gchar *ctype = "crosshair";
  DOBJTYPE     otype = (DOBJTYPE) GPOINTER_TO_INT (data);

  /* The object buttons emit toggled when they go up too */
  if (widget && GTK_IS_TOGGLE_BUTTON (widget) &&
      !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
    return;

  if(selvals.otype != otype)
    {
      /* Mem leak */
      obj_creating = NULL;
      tmp_line = NULL;
      tmp_bezier = NULL;

      if(otype < MOVE_OBJ)
	{
	  obj_show_single = -1; /* Cancel select preview */
	}
      /* Update draw areas */
      update_draw_area(gfig_preview,NULL);
      /* And preview */
      if (current_obj)
	list_button_update(current_obj);
    }

  selvals.otype = otype;

  switch(selvals.otype)
    {
    case LINE:
    case CIRCLE:
    case ELLIPSE:
    case ARC:
    case POLY:
    case STAR:
    case SPIRAL:
    case BEZIER:
    default:
      ctype = "crosshair";
      break;
    case MOVE_OBJ:
    case MOVE_POINT:
    case COPY_OBJ:
    case MOVE_COPY_OBJ:
      ctype = "move";
      break;
    case DEL_OBJ:
      ctype = "not-allowed";
      break;
    }

  if (gfig_preview)
    gtk_widget_set_cursor_from_name (gfig_preview, ctype);
}

static void
draw_grid_polar(void)
{
  gint step;
  gint loop;
  gint radius;
  gint max_rad;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble ang_radius;
  /* Pick center and draw concentric circles */

  gint grid_x_center = preview_width/2;
  gint grid_y_center = preview_height/2;

  step = selvals.opts.gridspacing;
  max_rad = sqrt(preview_width*preview_width + preview_height*preview_height)/2;

  for(loop = 0 ; loop < max_rad ; loop += step)
    {
      radius = loop;

      gfig_draw_arc (0,
		    grid_x_center - radius,
		    grid_y_center - radius,
		    radius*2,
		    radius*2,
		    0,
		    360*64);
    }

  /* Lines */
  ang_grid = 2*M_PI/get_num_radials();
  ang_radius = sqrt((preview_width*preview_width) + (preview_height*preview_height))/2;

  for(loop = 0 ; loop <= get_num_radials() ; loop++)
    {
      gint lx,ly;

      ang_loop = loop * ang_grid;
	
      lx = (gint)rint(ang_radius * cos(ang_loop));
      ly = (gint)rint(ang_radius * sin(ang_loop));

      gfig_draw_line ((gint)lx + (preview_width)/2,
		    -(gint)ly + (preview_height)/2,
		    (gint)(preview_width)/2,
		    (gint)(preview_height)/2);
    }
}

static void
draw_grid_sq(void)
{
  gint step;
  gint loop;

  /* Draw the horizontal lines */
  step = selvals.opts.gridspacing;

  for(loop = 0 ; loop < preview_height ; loop += step)
    {
        gfig_draw_line ((gint)0,
		(gint)loop,
		(gint)preview_width,
		(gint)loop);
    }

  /* Draw the vertical lines */

  for(loop = 0 ; loop < preview_width ; loop += step)
    {
        gfig_draw_line ((gint)loop,
		(gint)0,
		(gint)loop,
		(gint)preview_height);
    }
}

static void
draw_grid_iso(void)
{
   gint step;
   gint loop;

   gint diagonal_start;
   gint diagonal_end;
   gint diagonal_width;
   gint diagonal_height;
   
   step = selvals.opts.gridspacing;
   
   /* Draw the vertical lines */
   for (loop = 0 ; loop < preview_width ; loop += step)
     {
	gfig_draw_line ((gint)loop,
		      (gint)0,
		      (gint)loop,
		      (gint)preview_height);
     }

   diagonal_start = preview_width/2;
   diagonal_start = diagonal_start - (diagonal_start % step);
   diagonal_start = -diagonal_start;
   
   diagonal_end = preview_height + (preview_width/2);
   diagonal_end = diagonal_end - (diagonal_end % step);
   
   diagonal_width = preview_width;
   diagonal_height = diagonal_width/2;
   
   /* Draw diagonal lines */
   for (loop = diagonal_start ; loop < diagonal_end ; loop += step)
     {
	gfig_draw_line ((gint)0,
		      (gint)loop,
		      (gint)diagonal_width,
		      (gint)loop + diagonal_height);

	gfig_draw_line ((gint)0,
		      (gint)loop,
		      (gint)diagonal_width,
		      (gint)loop - diagonal_height);
     }
}

static void
draw_grid(GtkWidget *widget,
	  gpointer   data)
{
  /* Get the size of the preview and calc where the lines go */
  /* Draw in prelight to start with... */
  /* Always start in the upper left corner for rect.
   */

  if((preview_width < selvals.opts.gridspacing &&
     preview_height < selvals.opts.gridspacing ) ||
     drawing_pic)
    {
      /* Don't draw if they don't fit */
      return;
    }

  if(!selvals.opts.drawgrid)
    return;

  /* Only the draw function of the preview can draw */
  if (!gfig_cr)
    {
      gfig_queue_draw ();
      return;
    }

  cairo_save (gfig_cr);
  cairo_set_operator (gfig_cr, CAIRO_OPERATOR_OVER);
  gfig_set_grid_source (gfig_cr, grid_gc_type);

  if(selvals.opts.gridtype == RECT_GRID)
    draw_grid_sq();
  else if(selvals.opts.gridtype == POLAR_GRID)
    draw_grid_polar();
  else if(selvals.opts.gridtype == ISO_GRID)
    draw_grid_iso();

  cairo_restore (gfig_cr);
}

static void
do_gfig(void)
{
  /* Not sure if requre post proc - leave stub in */
}

/* This could belong in a separate file ... but makes it easier to lump into
 * one when compiling the plugin.
 */

/* Stuff for the generation/deletion of objects. */

/* Objects are easy one they are created - you just go down the object 
 * list calling the draw function for each object but... when they 
 * are been created we have to be a little more careful. When 
 * the first point is placed on the canvas we create the object, 
 * the mouse position then defines the next point that can move around.
 * careful how we draw this position.
 */

static void
free_one_obj(DOBJECT *obj)
{
  d_delete_dobjpoints(obj->points);
  g_free(obj);
}

static void
free_all_objs(DALLOBJS * objs)
{
  /* Free all objects */
  DALLOBJS * next;
  
  while(objs)
    {
      free_one_obj(objs->obj);
      next = objs->next;
      g_free(objs);
      objs = next;
    }
}

char *
get_line(gchar *buf,gint s,FILE * from,gint init)
{
  gint slen;
  char * ret;

  if(init)
    line_no = 1;
  else
    line_no++;

  do
    {
      ret = fgets(buf,s,from);
    } while(ret && !ferror(from) && buf[0] == '#');

  if(!ret)
    {
      /* EOF or error: don't hand back stale data */
      buf[0] = 0;
      return(NULL);
    }

  slen = strlen(buf);

  /* The last newline is a pain */
  if(slen > 0)
    buf[slen - 1] = '\0';
  
  if(ferror(from))
    {
      g_warning("Error reading file");
      return(0);
    }

#ifdef DEBUG
  printf("Processing line '%s'\n",buf);
#endif /* DEBUG */

  return(ret);
}

static void
gfig_clear_callback (GtkWidget *widget,
		      gpointer   data)
{
  /* Make sure we can get back - if we have some objects to get back to */
  if(!current_obj->obj_list)
    return;

  setup_undo();
  /* Free all objects */
  free_all_objs(current_obj->obj_list);
  current_obj->obj_list = NULL;
  obj_creating = NULL;
  tmp_line = NULL;
  tmp_bezier = NULL;
  update_draw_area(gfig_preview,NULL);
  /* And preview */
  list_button_update(current_obj);
}


static void
gfig_undo_callback (GtkWidget *widget,
			  gpointer   data)
{
  if(undo_water_mark >= 0)
    {
      /* Free current objects an reinstate previous */
      free_all_objs(current_obj->obj_list);
      current_obj->obj_list = NULL;
      tmp_bezier = tmp_line = obj_creating = NULL;
      current_obj->obj_list = undo_table[undo_water_mark];
      undo_water_mark--;
      /* Update the screen */
      update_draw_area(gfig_preview,NULL);
      /* And preview */
      list_button_update(current_obj);
      gfig_obj_modified(current_obj,GFIG_MODIFIED);
      current_obj->obj_status |= GFIG_MODIFIED;
  }

  if(undo_water_mark < 0)
    gtk_widget_set_sensitive(widget,FALSE);
}

static void
clear_undo()
{
  int lv;

  for(lv = undo_water_mark; lv >= 0; lv--) 
    {
      if(undo_table[lv])
	free_all_objs(undo_table[lv]);
      undo_table[lv] = NULL;
    }

  undo_water_mark = -1;
  gtk_widget_set_sensitive(undo_widget,FALSE);
}

static void
setup_undo()
{
  /* Copy object list to undo buffer */
#if DEBUG
  printf("setup undo level [%d]\n",undo_water_mark);
#endif /*DEBUG*/  

  if(!current_obj)
    {
      /* If no current_obj must be loading -> no undo */
      return;
    }

  if(undo_water_mark >= selvals.maxundo - 1)
    {
      int loop;
      /* the little one in the bed said "roll over".. */
      if(undo_table[0])
	free_one_obj(undo_table[0]->obj);
      for(loop = 0 ; loop < undo_water_mark ; loop++)
	{
	  undo_table[loop] = undo_table[loop + 1];
	}
    }
  else
    {
      undo_water_mark++;
    }
  undo_table[undo_water_mark] = copy_all_objs(current_obj->obj_list);
  gtk_widget_set_sensitive(undo_widget,TRUE);
  
  gfig_obj_modified(current_obj,GFIG_MODIFIED);
  current_obj->obj_status |= GFIG_MODIFIED;
}

/* Given a number of float co-ords adjust for scaling back to org size */
/* Size is number of PAIRS of points */
/* FP + int varients */

void
scale_to_orginal_x(gdouble *list)
{
  *list *= scale_x_factor;
}

static gint
gfig_scale_x(gint x)
{
  if(!selvals.scaletoimage)
    return((gint)(x*(1/scale_x_factor)));
  else
    return(x);
}

static gint
gfig_invscale_x(gint x)
{
  if(!selvals.scaletoimage)
    return((gint)(x*(scale_x_factor)));
  else
    return(x);
}

void
scale_to_orginal_y(gdouble *list)
{
  *list *= scale_y_factor;
}

static gint
gfig_scale_y(gint y)
{
  if(!selvals.scaletoimage)
    return((gint)(y*(1/scale_y_factor)));
  else
    return(y);
}

static gint
gfig_invscale_y(gint y)
{
  if(!selvals.scaletoimage)
    return((gint)(y*(scale_y_factor)));
  else
    return(y);
}


/* Pairs x followed by y */
void
scale_to_original_xy(gdouble *list,gint size)
{
  int i;
  for (i = 0 ; i < size*2 ; i +=2)
    {
      scale_to_orginal_x(&list[i]);
      scale_to_orginal_y(&list[i+1]);
    }
}

/* Pairs x followed by y */
void
scale_to_xy(gdouble *list,gint size)
{
  int i;
  for (i = 0 ; i < size*2 ; i +=2)
    {
      list[i] *= (org_scale_x_factor/scale_x_factor);
      list[i + 1] *= (org_scale_y_factor/scale_y_factor);
    }
}


/* Given an list of PAIRS of doubles reverse the list */
/* Size is number of pairs to swap */
void
reverse_pairs_list(gdouble *list, gint size)
{
  int i;
  struct cs { 
    gdouble i1; 
    gdouble i2;
  } copyit,*orglist;

  orglist = (struct cs *)list;

  /* Uses struct copies */
  for(i = 0 ; i < size/2 ; i++)
    {
      copyit = orglist[i];
      orglist[i] = orglist[size - 1 - i];
      orglist[size - 1 - i] = copyit;
    }
}


/* Delete a list of points */
void
d_delete_dobjpoints(DOBJPOINTS * pnts)
{
  DOBJPOINTS * next;
  DOBJPOINTS * pnt2del = pnts;

  while(pnt2del)
    {
      next = pnt2del->next;
      g_free(pnt2del);
      pnt2del = next;
    }
}

DOBJPOINTS *
d_copy_dobjpoints(DOBJPOINTS * pnts)
{
  DOBJPOINTS * ret = NULL;
  DOBJPOINTS * head = NULL;
  DOBJPOINTS * newpnt;
  DOBJPOINTS * pnt2copy = pnts;

  while(pnt2copy)
    {
      newpnt = g_malloc0(sizeof(DOBJPOINTS));
      newpnt->pnt.x = pnt2copy->pnt.x;
      newpnt->pnt.y = pnt2copy->pnt.y;

      if(!ret)
	head = ret = newpnt;
      else
	{
	  head->next = newpnt;
	  head = newpnt;
	}
      pnt2copy = pnt2copy->next;
    }

  return(ret);
}

static gint
scan_obj_points(DOBJPOINTS *opnt,GfigPoint *pnt)
{
  while(opnt)
    {
      if(inside_sqr(&opnt->pnt,pnt))
	{
	  opnt->found_me = TRUE;
	  return(TRUE);
	}
      opnt->found_me = FALSE;
      opnt = opnt->next;
    }
  return(FALSE);
}

static DOBJECT *
get_nearest_objs(GFIGOBJ * obj,GfigPoint *pnt)
{
  /* Nearest object to given point or NULL */
  DALLOBJS *all;
  DOBJECT  *test_obj;
  gint count = 0;

  if(!obj)
    return(NULL);

  all = obj->obj_list;

  while(all)
    {
      test_obj = all->obj;

      if(count == obj_show_single || obj_show_single == -1)
	if(scan_obj_points(test_obj->points,pnt))
	  {
	    return(test_obj);
	  }
      all = all->next;
      count++;
    }
  return(NULL);
}

static void
scale_obj_points(DOBJPOINTS *opnt,gdouble scale_x,gdouble scale_y)
{
  while(opnt)
    {
      opnt->pnt.x = (gint)(opnt->pnt.x * scale_x);
      opnt->pnt.y = (gint)(opnt->pnt.y * scale_y);
      opnt = opnt->next;
    }
}

static void
remove_obj_from_list(GFIGOBJ *obj,DOBJECT *del_obj)
{
  /* Nearest object to given point or NULL */
  DALLOBJS *all;
  DALLOBJS  *prev_all = NULL;
  
  g_assert(del_obj != NULL);

  all = obj->obj_list;

  while(all)
    {
      if(all->obj == del_obj)
	{
	  /* Found the one to delete */
#ifdef DEBUG
	  printf("Found the one to delete\n");
#endif /* DEBUG */

	  if(prev_all)
	    prev_all->next = all->next;
	  else
	    obj->obj_list = all->next;

	  /* Draw obj (which will actually undraw it! */
	  del_obj->drawfunc(del_obj);

	  free_one_obj(del_obj);
	  g_free(all);

	  if(obj_show_single != -1)
	    {
	      /* We've just deleted the only visible one */
	      draw_grid_clear(NULL,NULL); /*Args not used */
	      obj_show_single = -1; /* Show all again */
	    }
	  return;
	}
      prev_all = all;
      all = all->next;
    }
  g_warning("Hey where has the object gone ?");
}

static DOBJPOINTS *
get_diffs(DOBJECT *obj,gint16 *xdiff, gint16 *ydiff, GfigPoint *to_pnt)
{
  DOBJPOINTS *spnt;

  g_assert(obj != NULL);

  spnt = obj->points;
  
  if(!spnt)
    return(NULL); /* no-line */
  
  /* Slow slow slowwwwww....*/
  while(spnt)
    {
      if(spnt->found_me)
	{
	  *xdiff = spnt->pnt.x - to_pnt->x;
	  *ydiff = spnt->pnt.y - to_pnt->y;
	  return(spnt);
	}
      spnt = spnt->next;
    }
  return(NULL);
}

static void
update_pnts(DOBJECT *obj,gint16 xdiff, gint16 ydiff)
{
  DOBJPOINTS *spnt;

  g_assert(obj != NULL);

  /* Update all pnts */
  spnt = obj->points;

  if(!spnt)
    return; /* no-line */
  
  /* Go around all the points drawing a line from one to the next */
  while(spnt)
    {
      spnt->pnt.x = spnt->pnt.x - xdiff;
      spnt->pnt.y = spnt->pnt.y - ydiff;
      spnt = spnt->next;
    }
}


static void
do_move_all_obj(GfigPoint *to_pnt)
{
  /* Move all objects in one go */
  /* Undraw/then draw in new pos */
  DALLOBJS *all;
  DOBJECT *obj;
  gint16 xdiff = 0;
  gint16 ydiff = 0;
  
  xdiff = move_all_pnt->x - to_pnt->x;
  ydiff = move_all_pnt->y - to_pnt->y;
  
  if(!xdiff && !ydiff)
    return;
  
  all = current_obj->obj_list;

  while(all)
    {
      obj = all->obj;

      /* undraw ! */
      draw_one_obj(obj);
      
      update_pnts(obj,xdiff,ydiff);
      
      /* Draw in new pos */
      draw_one_obj(obj);

      all = all->next;
    }

  *move_all_pnt = *to_pnt; /* Structure copy */
}


static void
do_move_obj(DOBJECT *obj,GfigPoint *to_pnt)
{
  /* Move the whole line - undraw the line to start with */
  /* Then draw in new pos */
  gint16 xdiff = 0;
  gint16 ydiff = 0;
  
  get_diffs(obj,&xdiff,&ydiff,to_pnt);
  
  if(!xdiff && !ydiff)
    return;
  
  /* undraw ! */
  draw_one_obj(obj);
  
  update_pnts(obj,xdiff,ydiff);
  
  /* Draw in new pos */
  draw_one_obj(obj);
  
}

static void
do_move_obj_pnt(DOBJECT *obj,GfigPoint *to_pnt)
{
  /* Move the whole line - undraw the line to start with */
  /* Then draw in new pos */
  DOBJPOINTS *spnt;
  gint16 xdiff = 0;
  gint16 ydiff = 0;
  
  spnt = get_diffs(obj,&xdiff,&ydiff,to_pnt);
  
  if((!xdiff && !ydiff) || !spnt)
    return;
  
  /* undraw ! */
  draw_one_obj(obj);

  spnt->pnt.x = spnt->pnt.x - xdiff;
  spnt->pnt.y = spnt->pnt.y - ydiff;
  
  /* Draw in new pos */
  draw_one_obj(obj);
  
}

/* Save a line away to the specified stream */

void
d_save_line(DOBJECT * obj, FILE *to)
{
  DOBJPOINTS * spnt;

  spnt = obj->points;

  if(!spnt)
    return; /* End-of-line */

  fprintf(to,"<LINE>\n");

  while(spnt)
    {
      fprintf(to,"%d %d\n",
	      (gint)spnt->pnt.x,
	      (gint)spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf(to,"</LINE>\n");
}

/* Load a line from the specified stream */

DOBJECT *
d_load_line(FILE *from)
{
  DOBJECT *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];

#ifdef DEBUG
  printf("Load line called\n");
#endif /* DEBUG */

  while(get_line(buf,MAX_LOAD_LINE,from,0))
    {
      if(sscanf(buf,"%d %d",&xpnt,&ypnt) != 2)
	{
	  /* Must be the end */
	  if(strcmp("</LINE>",buf))
	    {
	      g_warning("[%d] Internal load error while loading line",
			line_no);
	      return(NULL);
	    }
	  return(new_obj);
	}

      if(!new_obj)
	new_obj = d_new_line(xpnt,ypnt);
      else
	d_pnt_add_line(new_obj,xpnt,ypnt,-1);
    }

  return(new_obj);
}

DOBJECT *
d_copy_line(DOBJECT *obj)
{
  DOBJECT *nl;

  if(!obj)
    return(NULL);

  g_assert(obj->type == LINE);

  nl = d_new_line(obj->points->pnt.x,obj->points->pnt.y);
  
  nl->points->next = d_copy_dobjpoints(obj->points->next);

  return(nl);
}

/* Draw the given line -- */
void
d_draw_line(DOBJECT * obj)
{
  DOBJPOINTS * spnt;
  DOBJPOINTS * epnt;

  spnt = obj->points;

  if(!spnt)
    return; /* End-of-line */

  epnt = spnt->next;

  while(spnt && epnt)
    {
#if DEBUG
      printf("Drawing line 0x%x (%x,%x) -> (%x,%x)\n",spnt,
	     (gint)spnt->pnt.x,
	     (gint)spnt->pnt.y,
	     (gint)epnt->pnt.x,
	     (gint)epnt->pnt.y);
#endif /* DEBUG */

      draw_sqr(&spnt->pnt);
      /* Go around all the points drawing a line from one to the next */
      if(drawing_pic)
	{
	  gfig_draw_line (adjust_pic_coords((gint)spnt->pnt.x,preview_width),
			adjust_pic_coords((gint)spnt->pnt.y,preview_height),
			adjust_pic_coords((gint)epnt->pnt.x,preview_width),
			adjust_pic_coords((gint)epnt->pnt.y,preview_height));
	}
      else
	{
	  gfig_draw_line (gfig_scale_x((gint)spnt->pnt.x),
			gfig_scale_y((gint)spnt->pnt.y),
			gfig_scale_x((gint)epnt->pnt.x),
			gfig_scale_y((gint)epnt->pnt.y));
	}
      spnt = epnt;
      epnt = epnt->next;
    }
  draw_sqr(&spnt->pnt);
}

static void 
d_paint_line(DOBJECT *obj)
{
  DOBJPOINTS * spnt;
  gdouble *line_pnts;
  GParam *return_vals = NULL;
  gint nreturn_vals;
  gint seg_count = 0;
  gint i = 0;

  spnt = obj->points;

  /* count */

  while(spnt)
    {
      seg_count++;
      spnt = spnt->next;
    }

  spnt = obj->points;

  if(!spnt || !seg_count)
    return; /* no-line */

  /* The second *2 to get around bug in GIMP */
  line_pnts = g_malloc0(GFIG_LCC*(2*seg_count + 1)*sizeof(gdouble));
  
  /* Go around all the points drawing a line from one to the next */
  while(spnt)
    {
      line_pnts[i++] = spnt->pnt.x;
      line_pnts[i++] = spnt->pnt.y;
      spnt = spnt->next;
    }

  /* Reverse line if approp */
  if(selvals.reverselines)
    reverse_pairs_list(&line_pnts[0],i/2);

  /* Scale before drawing */
  if(selvals.scaletoimage)
    scale_to_original_xy(&line_pnts[0],i/2);
  else
    scale_to_xy(&line_pnts[0],i/2);

  /* One go */
  if(selvals.painttype == PAINT_BRUSH_TYPE)
    {
      switch(selvals.brshtype)
	{
	case BRUSH_BRUSH_TYPE:
	  return_vals = gimp_run_procedure ("gimp_paintbrush", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_FLOAT,(gdouble)selvals.brushfade,
					    PARAM_INT32,seg_count*2*GFIG_LCC,/* GIMP BUG should be 2!!!!*/
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_PENCIL_TYPE:
	  return_vals = gimp_run_procedure ("gimp_pencil", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_INT32,seg_count*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_AIRBRUSH_TYPE:
	  return_vals = gimp_run_procedure ("gimp_airbrush", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_FLOAT,(gdouble)selvals.airbrushpressure,
					    PARAM_INT32,seg_count*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_PATTERN_TYPE:
	  return_vals = gimp_run_procedure ("gimp_clone", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_INT32, 1,
					    PARAM_FLOAT,(gdouble)0.0,
					    PARAM_FLOAT,(gdouble)0.0,
					    PARAM_INT32,seg_count*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	default:
	  break;
	}
    }
  else 
    {
      /* We want to do a selection */
      return_vals = gimp_run_procedure ("gimp_free_select", &nreturn_vals,
					PARAM_IMAGE, gfig_image,
					PARAM_INT32,seg_count*2,
					PARAM_FLOATARRAY, &line_pnts[0],  
					PARAM_INT32,selopt.type,
					PARAM_INT32,selopt.antia,
					PARAM_INT32,selopt.feather,
					PARAM_FLOAT,(gdouble)selopt.feather_radius,
					PARAM_END);
    }

  gimp_destroy_params (return_vals, nreturn_vals);

  g_free(line_pnts);
}


/* Create a new line object. starting at the x,y point might add styles 
 * later.
 */

DOBJECT *
d_new_line(gint x, gint y)
{
  DOBJECT *nobj;
  DOBJPOINTS *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = (DOBJPOINTS *)g_malloc0(sizeof(DOBJPOINTS));

#if DEBUG
  printf("New line start at (%x,%x)\n",x,y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = (DOBJECT *)g_malloc0(sizeof(DOBJECT));

  nobj->type = LINE;
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_line;
  nobj->loadfunc  = d_load_line;
  nobj->savefunc  = d_save_line;
  nobj->paintfunc = d_paint_line;
  nobj->copyfunc  = d_copy_line;

  return(nobj);
}

/* You guessed it delete the object !*/
void
d_delete_line(DOBJECT * obj)
{
  g_assert(obj != NULL);
  /* First free the list of points - then the object itself */
  d_delete_dobjpoints(obj->points);
  g_free(obj);
}


/* Add a point to a line (given x,y)
 * pos = 0 = head
 * pos = -1 = tail
 * 0 < pos = nth position
 */
 
static void
d_pnt_add_line(DOBJECT *obj, gint x, gint y, gint pos)
{
  DOBJPOINTS *npnts = (DOBJPOINTS *)g_malloc0(sizeof(DOBJPOINTS));

  g_assert(obj != NULL);

  npnts->pnt.x = x;
  npnts->pnt.y = y;
  
  if(!pos)
    {
      /* Add to head */
      npnts->next = obj->points;
      obj->points = npnts;
    }
  else
    {
      DOBJPOINTS *pnt = obj->points;

      /* Go down chain until the end if pos */
      while(pos < 0 || pos-- > 0)
	{
	  if(!(pnt->next) || !pos)
	    {
	      npnts->next = pnt->next;
	      pnt->next = npnts;
	      break;
	    }
	  else
	    {
	      pnt = pnt->next;
	    }
	}
    }
}

/* Update end point of line */
void
d_update_line(GfigPoint *pnt)
{
  DOBJPOINTS *spnt, *epnt;
  /* Replace the end point of the segment being drawn; the draw
   * function shows it (see draw_creating ()).
   * always dealing with the static object.
   */

  /* Get start of segments */
  spnt = obj_creating->points;

  if(!spnt)
    return; /* No points */

  if((epnt = spnt->next))
    {
      g_free(epnt);
    }

  epnt = (DOBJPOINTS *)g_malloc0(sizeof(DOBJPOINTS));

  epnt->pnt.x = pnt->x;
  epnt->pnt.y = pnt->y;

  spnt->next = epnt;

  gfig_queue_draw ();
}

void
d_line_start(GfigPoint *pnt,gint shift_down)
{
  if(!obj_creating || !shift_down)
    {
      /* Draw square on point */
      /* Must delete obj_creating if we have one */
      obj_creating = d_new_line(pnt->x,pnt->y);
    }
  else
    {
      /* Contniuation */
      d_update_line(pnt);	
    }
}

void
d_line_end(GfigPoint *pnt,gint shift_down)
{
  /* Undraw the last circle */
  draw_circle(pnt);

  if(shift_down)
    {
      if(tmp_line)
	{
	  GfigPoint tmp_pnt = *pnt;

	  if(need_to_scale)
	    {
	      tmp_pnt.x = (gint)(pnt->x * scale_x_factor);
	      tmp_pnt.y = (gint)(pnt->y * scale_y_factor);
	    }

	  d_pnt_add_line(tmp_line,tmp_pnt.x,tmp_pnt.y,-1);
	  free_one_obj(obj_creating);
	  /* Must free obj_creating */
	}
      else
	{
	  tmp_line = obj_creating;
	  add_to_all_obj(current_obj,obj_creating);
	}
      
      obj_creating = d_new_line(pnt->x,pnt->y);
    }
  else
    {
      if(tmp_line)
	{
	  GfigPoint tmp_pnt = *pnt;

	  if(need_to_scale)
	    {
	      tmp_pnt.x = (gint)(pnt->x * scale_x_factor);
	      tmp_pnt.y = (gint)(pnt->y * scale_y_factor);
	    }

	  d_pnt_add_line(tmp_line,tmp_pnt.x,tmp_pnt.y,-1);
	  free_one_obj(obj_creating);
	  /* Must free obj_creating */
	}
      else
	{
	  add_to_all_obj(current_obj,obj_creating);
	}
      obj_creating = NULL;
      tmp_line = NULL;
    }
  /*update_draw_area(gfig_preview,NULL);*/
}

/* Save a circle away to the specified stream */

void
d_save_circle(DOBJECT * obj, FILE *to)
{
  DOBJPOINTS * spnt;

  spnt = obj->points;

  if(!spnt)
    return;

  fprintf(to,"<CIRCLE>\n");

  while(spnt)
    {
      fprintf(to,"%d %d\n",
	      (gint)spnt->pnt.x,
	      (gint)spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf(to,"</CIRCLE>\n");
}

/* Load a circle from the specified stream */

DOBJECT *
d_load_circle(FILE *from)
{
  DOBJECT *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];

#ifdef DEBUG
  printf("Load circle called\n");
#endif /* DEBUG */

  while(get_line(buf,MAX_LOAD_LINE,from,0))
    {
      if(sscanf(buf,"%d %d",&xpnt,&ypnt) != 2)
	{
	  /* Must be the end */
	  if(strcmp("</CIRCLE>",buf))
	    {
	      g_warning("[%d] Internal load error while loading circle",
			line_no);
	      return(NULL);
	    }
	  return(new_obj);
	}

      if(!new_obj)
	new_obj = d_new_circle(xpnt,ypnt);
      else
	{
	  DOBJPOINTS *edge_pnt;
	  /* Circles only have two points */
	  edge_pnt = (DOBJPOINTS *)g_malloc0(sizeof(DOBJPOINTS));
	  
	  edge_pnt->pnt.x = xpnt;
	  edge_pnt->pnt.y = ypnt;
	  
	  new_obj->points->next = edge_pnt;
	}
    }
  g_warning("[%d] Not enough points for circle",line_no);
  return(NULL);
}

static void
d_draw_circle(DOBJECT * obj)
{
  DOBJPOINTS * center_pnt;
  DOBJPOINTS * edge_pnt;
  gdouble radius;

  center_pnt = obj->points;

  if(!center_pnt)
    return; /* End-of-line */

  edge_pnt = center_pnt->next;

  if(!edge_pnt)
    {
      g_warning("Internal error - circle no edge pnt");
    }

  radius = sqrt(((center_pnt->pnt.x - edge_pnt->pnt.x) *
		 (center_pnt->pnt.x - edge_pnt->pnt.x)) +
		((center_pnt->pnt.y - edge_pnt->pnt.y) *
		 (center_pnt->pnt.y - edge_pnt->pnt.y)));

  draw_sqr(&center_pnt->pnt);
  draw_sqr(&edge_pnt->pnt);

  if(drawing_pic)
    {
      gfig_draw_arc (0,
		    adjust_pic_coords(center_pnt->pnt.x - radius,
				      preview_width),
		    adjust_pic_coords(center_pnt->pnt.y - radius,
				      preview_height),
		    adjust_pic_coords(radius * 2,
				      preview_width),
		    adjust_pic_coords(radius * 2,
				      preview_height),
		    0,
		    360*64);
    }
  else
    {
      gfig_draw_arc (0,
		    gfig_scale_x(center_pnt->pnt.x - (gint)rint(radius)),
		    gfig_scale_y(center_pnt->pnt.y - (gint)rint(radius)),
		    gfig_scale_x((gint)rint(radius) * 2),
		    gfig_scale_y((gint)rint(radius) * 2),
		    0,
		    360*64);
    }
}

static void
d_paint_circle(DOBJECT *obj)
{
  GParam *return_vals;
  gint nreturn_vals;
  DOBJPOINTS * center_pnt;
  DOBJPOINTS * edge_pnt;
  gint radius;
  gdouble dpnts[4];


  g_assert(obj != NULL);

  if(selvals.approxcircles)
    {
      obj->type_data = GINT_TO_POINTER(600);
#ifdef DEBUG
      printf("Painting circle as polygon\n");
#endif /* DEBUG */
      d_paint_poly(obj);
      return;
    }      


  /* Drawing circles is hard .
   * 1) select circle
   * 2) stroke it
   */
  center_pnt = obj->points;

  if(!center_pnt)
    return; /* End-of-line */

  edge_pnt = center_pnt->next;

  if(!edge_pnt)
    {
      g_error("Internal error - circle no edge pnt");
    }

  radius = (gint)sqrt(((center_pnt->pnt.x - edge_pnt->pnt.x) *
		 (center_pnt->pnt.x - edge_pnt->pnt.x)) +
		((center_pnt->pnt.y - edge_pnt->pnt.y) *
		 (center_pnt->pnt.y - edge_pnt->pnt.y)));

  dpnts[0] = (gdouble)center_pnt->pnt.x - radius;
  dpnts[1] = (gdouble)center_pnt->pnt.y - radius;
  dpnts[3] = dpnts[2] = (gdouble)radius*2;

  /* Scale before drawing */
  if(selvals.scaletoimage)
    scale_to_original_xy(&dpnts[0],2);
  else
    scale_to_xy(&dpnts[0],2);

  return_vals = gimp_run_procedure ("gimp_ellipse_select", &nreturn_vals,
				    PARAM_IMAGE, gfig_image,
				    PARAM_FLOAT,dpnts[0],
				    PARAM_FLOAT,dpnts[1],
				    PARAM_FLOAT,dpnts[2],
				    PARAM_FLOAT,dpnts[3],
				    PARAM_INT32,selopt.type,
				    PARAM_INT32,selopt.antia,
				    PARAM_INT32,selopt.feather,
				    PARAM_FLOAT,(gdouble)selopt.feather_radius,
				    PARAM_END);
  
  gimp_destroy_params (return_vals, nreturn_vals);

  /* Is selection all we need ? */
  if(selvals.painttype == PAINT_SELECTION_TYPE)
    return;

  return_vals = gimp_run_procedure ("gimp_edit_stroke", &nreturn_vals,
				    PARAM_IMAGE, gfig_image,
				    PARAM_DRAWABLE, gfig_drawable,
				    PARAM_END);

  gimp_destroy_params (return_vals, nreturn_vals);

  return_vals = gimp_run_procedure ("gimp_selection_clear", &nreturn_vals,
				    PARAM_IMAGE, gfig_image,
				    PARAM_END);
  
  gimp_destroy_params (return_vals, nreturn_vals);

}

DOBJECT *
d_copy_circle(DOBJECT * obj)
{
  DOBJECT *nc;

#if DEBUG
  printf("Copy circle\n");
#endif /*DEBUG*/
  if(!obj)
    return(NULL);

  g_assert(obj->type == CIRCLE);

  nc = d_new_circle(obj->points->pnt.x,obj->points->pnt.y);

  nc->points->next = d_copy_dobjpoints(obj->points->next);

#if DEBUG
  printf("Circle (%x,%x) to (%x,%x)\n",
	 nc->points->pnt.x,obj->points->pnt.y,
	 nc->points->next->pnt.x,obj->points->next->pnt.y);
  printf("Done copy\n");
#endif /*DEBUG*/
  return(nc);
}

DOBJECT *
d_new_circle(gint x, gint y)
{
  DOBJECT *nobj;
  DOBJPOINTS *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = (DOBJPOINTS *)g_malloc0(sizeof(DOBJPOINTS));

#if DEBUG
  printf("New circle start at (%x,%x)\n",x,y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = (DOBJECT *)g_malloc0(sizeof(DOBJECT));

  nobj->type = CIRCLE;
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_circle;
  nobj->loadfunc  = d_load_circle;
  nobj->savefunc  = d_save_circle;
  nobj->paintfunc = d_paint_circle;
  nobj->copyfunc  = d_copy_circle;

  return(nobj);
}

void
d_update_circle(GfigPoint *pnt)
{
  DOBJPOINTS *center_pnt, *edge_pnt;
  gdouble radius;

  /* Undraw last one then draw new one */
  center_pnt = obj_creating->points;
  
  if(!center_pnt)
    return; /* No points */
  
  if((edge_pnt = center_pnt->next))
    {
      /* Undraw current */
      draw_circle(&edge_pnt->pnt);
      radius = sqrt(((center_pnt->pnt.x - edge_pnt->pnt.x) *
			   (center_pnt->pnt.x - edge_pnt->pnt.x)) +
			  ((center_pnt->pnt.y - edge_pnt->pnt.y) *
			   (center_pnt->pnt.y - edge_pnt->pnt.y)));
      
      gfig_draw_arc (0,
		    center_pnt->pnt.x - (gint)rint(radius),
		    center_pnt->pnt.y - (gint)rint(radius),
		    (gint)rint(radius) * 2,
		    (gint)rint(radius) * 2,
		    0,
		    360*64);
    }

  draw_circle(pnt);

  edge_pnt = (DOBJPOINTS *)g_malloc0(sizeof(DOBJPOINTS));

  edge_pnt->pnt.x = pnt->x;
  edge_pnt->pnt.y = pnt->y;

  radius = sqrt(((center_pnt->pnt.x - edge_pnt->pnt.x) *
		       (center_pnt->pnt.x - edge_pnt->pnt.x)) +
		      ((center_pnt->pnt.y - edge_pnt->pnt.y) *
		       (center_pnt->pnt.y - edge_pnt->pnt.y)));
  
  gfig_draw_arc (0,
		center_pnt->pnt.x - (gint)rint(radius),
		center_pnt->pnt.y - (gint)rint(radius),
		(gint)rint(radius) * 2,
		(gint)rint(radius) * 2,
		0,
		360*64);

  center_pnt->next = edge_pnt;
}

void
d_circle_start(GfigPoint *pnt,gint shift_down)
{
  obj_creating = d_new_circle(pnt->x, pnt->y);
}

void
d_circle_end(GfigPoint *pnt, gint shift_down)
{
  /* Under contrl point */
  if(!obj_creating->points->next)
    {
      /* No circle created */
      free_one_obj(obj_creating);
    }
  else
    {
      draw_circle(pnt);
      add_to_all_obj(current_obj,obj_creating);
    }

  obj_creating = NULL;
}

/* Save an ellipse away to the specified stream */

void
d_save_ellipse(DOBJECT * obj, FILE *to)
{
  DOBJPOINTS * spnt;

  spnt = obj->points;

  if(!spnt)
    return;

  fprintf(to,"<ELLIPSE>\n");

  while(spnt)
    {
      fprintf(to,"%d %d\n",
	      (gint)spnt->pnt.x,
	      (gint)spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf(to,"</ELLIPSE>\n");
}

/* Load a circle from the specified stream */

DOBJECT *
d_load_ellipse(FILE *from)
{
  DOBJECT *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];

#ifdef DEBUG
  printf("Load ellipse called\n");
#endif /* DEBUG */

  while(get_line(buf,MAX_LOAD_LINE,from,0))
    {
      if(sscanf(buf,"%d %d",&xpnt,&ypnt) != 2)
	{
	  /* Must be the end */
	  if(strcmp("</ELLIPSE>",buf))
	    {
	      g_warning("[%d] Internal load error while loading ellipse",
			line_no);
	      return(NULL);
	    }
	  return(new_obj);
	}

      if(!new_obj)
	new_obj = d_new_ellipse(xpnt,ypnt);
      else
	{
	  DOBJPOINTS *edge_pnt;
	  /* Circles only have two points */
	  edge_pnt = (DOBJPOINTS *)g_malloc0(sizeof(DOBJPOINTS));
	  
	  edge_pnt->pnt.x = xpnt;
	  edge_pnt->pnt.y = ypnt;
	  
	  new_obj->points->next = edge_pnt;
	}
    }
  g_warning("[%d] Not enough points for ellipse",line_no);
  return(NULL);
}

static void
d_draw_ellipse(DOBJECT * obj)
{
  DOBJPOINTS * center_pnt;
  DOBJPOINTS * edge_pnt;
  gint bound_wx;
  gint bound_wy;
  gint top_x;
  gint top_y;

  center_pnt = obj->points;

  if(!center_pnt)
    return; /* End-of-line */

  edge_pnt = center_pnt->next;

  if(!edge_pnt)
    {
      g_warning("Internal error - ellipse no edge pnt");
    }

  draw_sqr(&center_pnt->pnt);
  draw_sqr(&edge_pnt->pnt);

  bound_wx = abs(center_pnt->pnt.x - edge_pnt->pnt.x)*2;
  bound_wy = abs(center_pnt->pnt.y - edge_pnt->pnt.y)*2;

  if(edge_pnt->pnt.x > center_pnt->pnt.x)
    top_x = 2*center_pnt->pnt.x - edge_pnt->pnt.x;
  else
    top_x = edge_pnt->pnt.x;
  
  if(edge_pnt->pnt.y > center_pnt->pnt.y)
    top_y = 2*center_pnt->pnt.y - edge_pnt->pnt.y;
  else
    top_y = edge_pnt->pnt.y;

  if(drawing_pic)
    {
      gfig_draw_arc (0,
		    adjust_pic_coords(top_x,
				      preview_width),
		    adjust_pic_coords(top_y,
				      preview_height),
		    adjust_pic_coords(bound_wx,
				      preview_width),
		    adjust_pic_coords(bound_wy,
				      preview_height),
		    0,
		    360*64);
    }
  else
    {
      gfig_draw_arc (0,
		    gfig_scale_x(top_x),
		    gfig_scale_y(top_y),
		    gfig_scale_x(bound_wx),
		    gfig_scale_y(bound_wy),
		    0,
		    360*64);
    }
}

static void
d_paint_approx_ellipse(DOBJECT *obj)
{
  /* first point center */
  /* Next point is radius */
  gdouble *line_pnts;
  GParam *return_vals = NULL;
  gint nreturn_vals;
  gint seg_count = 0;
  gint i = 0;
  DOBJPOINTS * center_pnt;
  DOBJPOINTS * radius_pnt;
  gdouble a_axis;
  gdouble b_axis;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble radius;
  gint loop;
  GfigPoint first_pnt = { 0, 0 }, last_pnt = { 0, 0 };
  gint first = 1;

  g_assert(obj != NULL);

  /* count - add one to close polygon */
  seg_count = 600;

  center_pnt = obj->points;

  if(!center_pnt || !seg_count)
    return; /* no-line */

  /* The second 2* to get around bug in GIMP */
  line_pnts = g_malloc0(GFIG_LCC*(2*seg_count + 1)*sizeof(gdouble));
  
  /* Go around all the points drawing a line from one to the next */

  radius_pnt = center_pnt->next; /* this defines the vetices */

  /* Have center and radius - get lines */
  a_axis = ((gdouble)(radius_pnt->pnt.x - center_pnt->pnt.x));
  b_axis = ((gdouble)(radius_pnt->pnt.y - center_pnt->pnt.y));

  /* Lines */
  ang_grid = 2*M_PI/(gdouble)(gint)600;

  for(loop = 0 ; loop < (gint)600 ; loop++)
    {
      gdouble lx,ly;
      GfigPoint calc_pnt;
      
      ang_loop = (gdouble)loop * ang_grid;

      radius = a_axis*b_axis/(sqrt(cos(ang_loop)*cos(ang_loop)*(b_axis*b_axis - a_axis*a_axis) + a_axis*a_axis));
	
      lx = radius * cos(ang_loop);
      ly = radius * sin(ang_loop);

      calc_pnt.x = (gint)rint(lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)rint(ly + center_pnt->pnt.y);

      /* Miss out duped pnts */
      if(!first)
	{
	  if(calc_pnt.x == last_pnt.x && calc_pnt.y == last_pnt.y)
	    {
	      continue;
	    }
	}

      last_pnt.x = line_pnts[i++] = calc_pnt.x;
      last_pnt.y = line_pnts[i++] = calc_pnt.y;

      if(first)
	{
	  first_pnt.x = calc_pnt.x;
	  first_pnt.y = calc_pnt.y;
	  first = 0;
	}
    }

  line_pnts[i++] = first_pnt.x;
  line_pnts[i++] = first_pnt.y;

  /* Reverse line if approp */
  if(selvals.reverselines)
    reverse_pairs_list(&line_pnts[0],i/2);

  /* Scale before drawing */
  if(selvals.scaletoimage)
    scale_to_original_xy(&line_pnts[0],i/2);
  else
    scale_to_xy(&line_pnts[0],i/2);

  /* One go */
  if(selvals.painttype == PAINT_BRUSH_TYPE)
    {
      switch(selvals.brshtype)
	{
	case BRUSH_BRUSH_TYPE:
	  return_vals = gimp_run_procedure ("gimp_paintbrush", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_FLOAT,(gdouble)selvals.brushfade,
					    PARAM_INT32,(i/2)*2*GFIG_LCC,/* GIMP BUG should be 2!!!!*/
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_PENCIL_TYPE:
	  return_vals = gimp_run_procedure ("gimp_pencil", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_AIRBRUSH_TYPE:
	  return_vals = gimp_run_procedure ("gimp_airbrush", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_FLOAT,(gdouble)selvals.airbrushpressure,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_PATTERN_TYPE:
	  return_vals = gimp_run_procedure ("gimp_clone", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_INT32, 1,
					    PARAM_FLOAT,(gdouble)0.0,
					    PARAM_FLOAT,(gdouble)0.0,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	default:
	  break;
	}
    }
  else
    {
      /* We want to do a selection */
      return_vals = gimp_run_procedure ("gimp_free_select", &nreturn_vals,
					PARAM_IMAGE, gfig_image,
					PARAM_INT32,(i/2)*2,
					PARAM_FLOATARRAY, &line_pnts[0],  
					PARAM_INT32,selopt.type,
					PARAM_INT32,selopt.antia,
					PARAM_INT32,selopt.feather,
					PARAM_FLOAT,(gdouble)selopt.feather_radius,
					PARAM_END);

    }

  gimp_destroy_params (return_vals, nreturn_vals);

  g_free(line_pnts);
}



static void
d_paint_ellipse(DOBJECT *obj)
{
  GParam *return_vals;
  gint nreturn_vals;
  DOBJPOINTS * center_pnt;
  DOBJPOINTS * edge_pnt;
  gint bound_wx;
  gint bound_wy;
  gint top_x;
  gint top_y;
  gdouble dpnts[4];

  /* Drawing ellipse is hard .
   * 1) select circle
   * 2) stroke it
   */

  g_assert(obj != NULL);

  if(selvals.approxcircles)
    {
#ifdef DEBUG
      printf("Painting ellipse as polygon\n");
#endif /* DEBUG */
      d_paint_approx_ellipse(obj);
      return;
    }      

  center_pnt = obj->points;

  if(!center_pnt)
    return; /* End-of-line */

  edge_pnt = center_pnt->next;

  if(!edge_pnt)
    {
      g_error("Internal error - ellipse no edge pnt");
    }

  bound_wx = abs(center_pnt->pnt.x - edge_pnt->pnt.x)*2;
  bound_wy = abs(center_pnt->pnt.y - edge_pnt->pnt.y)*2;

  if(edge_pnt->pnt.x > center_pnt->pnt.x)
    top_x = 2*center_pnt->pnt.x - edge_pnt->pnt.x;
  else
    top_x = edge_pnt->pnt.x;
  
  if(edge_pnt->pnt.y > center_pnt->pnt.y)
    top_y = 2*center_pnt->pnt.y - edge_pnt->pnt.y;
  else
    top_y = edge_pnt->pnt.y;

  dpnts[0] = (gdouble)top_x;
  dpnts[1] = (gdouble)top_y;
  dpnts[2] = (gdouble)bound_wx;
  dpnts[3] = (gdouble)bound_wy;

  /* Scale before drawing */
  if(selvals.scaletoimage)
    scale_to_original_xy(&dpnts[0],2);
  else
    scale_to_xy(&dpnts[0],2);


  return_vals = gimp_run_procedure ("gimp_ellipse_select", &nreturn_vals,
				    PARAM_IMAGE, gfig_image,
				    PARAM_FLOAT,dpnts[0],
				    PARAM_FLOAT,dpnts[1],
				    PARAM_FLOAT,dpnts[2],
				    PARAM_FLOAT,dpnts[3],
				    PARAM_INT32,selopt.type,
				    PARAM_INT32,selopt.antia,
				    PARAM_INT32,selopt.feather,
				    PARAM_FLOAT,(gdouble)selopt.feather_radius,
				    PARAM_END);
  
  gimp_destroy_params (return_vals, nreturn_vals);

  /* Is selection all we need ? */
  if(selvals.painttype == PAINT_SELECTION_TYPE)
    return;

  return_vals = gimp_run_procedure ("gimp_edit_stroke", &nreturn_vals,
				    PARAM_IMAGE, gfig_image,
				    PARAM_DRAWABLE, gfig_drawable,
				    PARAM_END);

  gimp_destroy_params (return_vals, nreturn_vals);

  return_vals = gimp_run_procedure ("gimp_selection_clear", &nreturn_vals,
				    PARAM_IMAGE, gfig_image,
				    PARAM_END);
  
  gimp_destroy_params (return_vals, nreturn_vals);

}

DOBJECT *
d_copy_ellipse(DOBJECT * obj)
{
  DOBJECT *nc;

#if DEBUG
  printf("Copy ellipse\n");
#endif /*DEBUG*/
  if(!obj)
    return(NULL);

  g_assert(obj->type == ELLIPSE);

  nc = d_new_ellipse(obj->points->pnt.x,obj->points->pnt.y);

  nc->points->next = d_copy_dobjpoints(obj->points->next);

#if DEBUG
  printf("Ellipse (%x,%x) to (%x,%x)\n",
	 nc->points->pnt.x,obj->points->pnt.y,
	 nc->points->next->pnt.x,obj->points->next->pnt.y);
  printf("Done copy\n");
#endif /*DEBUG*/
  return(nc);
}

DOBJECT *
d_new_ellipse(gint x, gint y)
{
  DOBJECT *nobj;
  DOBJPOINTS *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = (DOBJPOINTS *)g_malloc0(sizeof(DOBJPOINTS));

#if DEBUG
  printf("New ellipse start at (%x,%x)\n",x,y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = (DOBJECT *)g_malloc0(sizeof(DOBJECT));

  nobj->type = ELLIPSE;
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_ellipse;
  nobj->loadfunc  = d_load_ellipse;
  nobj->savefunc  = d_save_ellipse;
  nobj->paintfunc = d_paint_ellipse;
  nobj->copyfunc  = d_copy_ellipse;

  return(nobj);
}

void
d_update_ellipse(GfigPoint *pnt)
{
  DOBJPOINTS *center_pnt, *edge_pnt;
  gint bound_wx;
  gint bound_wy;
  gint top_x;
  gint top_y;

  /* Undraw last one then draw new one */
  center_pnt = obj_creating->points;
  
  if(!center_pnt)
    return; /* No points */

  
  if((edge_pnt = center_pnt->next))
    {
      /* Undraw current */
      bound_wx = abs(center_pnt->pnt.x - edge_pnt->pnt.x)*2;
      bound_wy = abs(center_pnt->pnt.y - edge_pnt->pnt.y)*2;
      
      if(edge_pnt->pnt.x > center_pnt->pnt.x)
	top_x = 2*center_pnt->pnt.x - edge_pnt->pnt.x;
      else
	top_x = edge_pnt->pnt.x;
      
      if(edge_pnt->pnt.y > center_pnt->pnt.y)
	top_y = 2*center_pnt->pnt.y - edge_pnt->pnt.y;
      else
	top_y = edge_pnt->pnt.y;

      draw_circle(&edge_pnt->pnt);
      
      gfig_draw_arc (0,
		    top_x,
		    top_y,
		    bound_wx,
		    bound_wy,
		    0,
		    360*64);
    }

  draw_circle(pnt);

  edge_pnt = (DOBJPOINTS *)g_malloc0(sizeof(DOBJPOINTS));

  edge_pnt->pnt.x = pnt->x;
  edge_pnt->pnt.y = pnt->y;

  bound_wx = abs(center_pnt->pnt.x - edge_pnt->pnt.x)*2;
  bound_wy = abs(center_pnt->pnt.y - edge_pnt->pnt.y)*2;

  if(edge_pnt->pnt.x > center_pnt->pnt.x)
    top_x = 2*center_pnt->pnt.x - edge_pnt->pnt.x;
  else
    top_x = edge_pnt->pnt.x;
  
  if(edge_pnt->pnt.y > center_pnt->pnt.y)
    top_y = 2* center_pnt->pnt.y - edge_pnt->pnt.y;
  else
    top_y = edge_pnt->pnt.y;
  
  gfig_draw_arc (0,
		top_x,
		top_y,
		bound_wx,
		bound_wy,
		0,
		360*64);
  
  center_pnt->next = edge_pnt;
}

void
d_ellipse_start(GfigPoint *pnt,gint shift_down)
{
  obj_creating = d_new_ellipse(pnt->x, pnt->y);
}

void
d_ellipse_end(GfigPoint *pnt, gint shift_down)
{
  /* Under contrl point */
  if(!obj_creating->points->next)
    {
      /* No circle created */
      free_one_obj(obj_creating);
    }
  else
    {
      draw_circle(pnt);
      add_to_all_obj(current_obj,obj_creating);
    }

  obj_creating = NULL;
}

/* Normal polygon */

void
d_save_poly(DOBJECT * obj, FILE *to)
{
  DOBJPOINTS * spnt;
  
  spnt = obj->points;

  if(!spnt)
    return; /* End-of-line */

  fprintf(to,"<POLY>\n");

  while(spnt)
    {
      fprintf(to,"%d %d\n",
	      (gint)spnt->pnt.x,
	      (gint)spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf(to,"<EXTRA>\n");
  fprintf(to,"%d\n</EXTRA>\n",GPOINTER_TO_INT(obj->type_data));
  fprintf(to,"</POLY>\n");

}

/* Load a circle from the specified stream */

DOBJECT *
d_load_poly(FILE *from)
{
  DOBJECT *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];

#ifdef DEBUG
  printf("Load poly called\n");
#endif /* DEBUG */

  while(get_line(buf,MAX_LOAD_LINE,from,0))
    {
      if(sscanf(buf,"%d %d",&xpnt,&ypnt) != 2)
	{
	  /* Must be the end */
	  if(!strcmp("<EXTRA>",buf))
	    {
	      gint nsides = 3;
	      /* Number of sides - data item */
	      if(!new_obj)
		{
		  g_warning("[%d] Internal load error while loading poly (extra area)",
			    line_no);
		  return(NULL);
		}
	      get_line(buf,MAX_LOAD_LINE,from,0);
	      if(sscanf(buf,"%d",&nsides) != 1)
		{
		  g_warning("[%d] Internal load error while loading poly (extra area scanf)",
			    line_no);
		  return(NULL);
		}
	      if(nsides < 3 || nsides > 200)
		{
		  g_warning("[%d] Invalid value while loading poly",
			    line_no);
		  return(NULL);
		}
	      new_obj->type_data = GINT_TO_POINTER(nsides);
	      get_line(buf,MAX_LOAD_LINE,from,0);
	      if(strcmp("</EXTRA>",buf))
		{
		  g_warning("[%d] Internal load error while loading poly",
			    line_no);
		  return(NULL);
		} 
	      /* Go around and read the last line */
	      continue;
	    }
	  else if(strcmp("</POLY>",buf))
	    {
	      g_warning("[%d] Internal load error while loading poly",
			line_no);
	      return(NULL);
	    }
	  return(new_obj);
	}
      
      if(!new_obj)
	new_obj = d_new_poly(xpnt,ypnt);
      else
	d_pnt_add_line(new_obj,xpnt,ypnt,-1);
    }
  return(new_obj);
}

static void
d_draw_poly(DOBJECT *obj)
{
  DOBJPOINTS * center_pnt;
  DOBJPOINTS * radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble radius;
  gdouble offset_angle;
  gint loop;
  GfigPoint start_pnt = { 0, 0 };
  GfigPoint first_pnt = { 0, 0 };
  gint do_line = 0;

  center_pnt = obj->points;

  if(!center_pnt)
    return; /* End-of-line */

  /* First point is the center */
  /* Just draw a control point around it */

  draw_sqr(&center_pnt->pnt);

  /* Next point defines the radius */
  radius_pnt = center_pnt->next; /* this defines the vetices */

  if(!radius_pnt)
    {
#ifdef DEBUG
      g_warning("Internal error in polygon - no vertice point \n");
#endif /* DEBUG */
      return;
    }

  /* Other control point */
  draw_sqr(&radius_pnt->pnt);

  /* Have center and radius - draw polygon */

  shift_x = radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = radius_pnt->pnt.y - center_pnt->pnt.y;

  radius = sqrt((shift_x*shift_x) + (shift_y*shift_y));

  /* Lines */
  ang_grid = 2*M_PI/(gdouble)GPOINTER_TO_INT(obj->type_data);
  offset_angle = atan2(shift_y,shift_x);

  for(loop = 0 ; loop < GPOINTER_TO_INT(obj->type_data) ; loop++)
    {
      gdouble lx,ly;
      GfigPoint calc_pnt;

      ang_loop = (gdouble)loop * ang_grid + offset_angle;
	
      lx = radius * cos(ang_loop);
      ly = radius * sin(ang_loop);

      calc_pnt.x = (gint)rint(lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)rint(ly + center_pnt->pnt.y);

      if(do_line)
	{

	  /* Miss out points that come to the same location */
	  if(calc_pnt.x == start_pnt.x && calc_pnt.y == start_pnt.y)
	    continue;

	  if(drawing_pic)
	    {
	      gfig_draw_line (adjust_pic_coords(calc_pnt.x,
					      preview_width),
			    adjust_pic_coords(calc_pnt.y,
					      preview_height),
			    adjust_pic_coords(start_pnt.x,
					      preview_width),
			    adjust_pic_coords(start_pnt.y,
					      preview_height));
	    }
	  else
	    {
	      gfig_draw_line (gfig_scale_x(calc_pnt.x),
			    gfig_scale_y(calc_pnt.y),
			    gfig_scale_x(start_pnt.x),
			    gfig_scale_y(start_pnt.y));
	    }
	}
      else
	{
	  do_line = 1;
	  first_pnt.x = calc_pnt.x;
	  first_pnt.y = calc_pnt.y;
	}
      start_pnt.x = calc_pnt.x;
      start_pnt.y = calc_pnt.y;
    }

  /* Join up */
  if(drawing_pic)
    {
      gfig_draw_line (adjust_pic_coords(first_pnt.x,preview_width),
		adjust_pic_coords(first_pnt.y,preview_width),
		adjust_pic_coords(start_pnt.x,preview_width),
		adjust_pic_coords(start_pnt.y,preview_width));
    }
  else
    {
      gfig_draw_line (gfig_scale_x(first_pnt.x),
		gfig_scale_y(first_pnt.y),
		gfig_scale_x(start_pnt.x),
		gfig_scale_y(start_pnt.y));
    }
}

static void
d_paint_poly(DOBJECT *obj)
{
  /* first point center */
  /* Next point is radius */
  gdouble *line_pnts;
  GParam *return_vals = NULL;
  gint nreturn_vals;
  gint seg_count = 0;
  gint i = 0;
  DOBJPOINTS * center_pnt;
  DOBJPOINTS * radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble radius;
  gdouble offset_angle;
  gint loop;
  GfigPoint first_pnt = { 0, 0 }, last_pnt = { 0, 0 };
  gint first = 1;

  g_assert(obj != NULL);

  /* count - add one to close polygon */
  seg_count = GPOINTER_TO_INT(obj->type_data) + 1;

  center_pnt = obj->points;

  if(!center_pnt || !seg_count || !center_pnt->next)
    return; /* no-line */

  /* The second 2* to get around bug in GIMP */
  line_pnts = g_malloc0(GFIG_LCC*(2*seg_count + 1)*sizeof(gdouble));
  
  /* Go around all the points drawing a line from one to the next */

  radius_pnt = center_pnt->next; /* this defines the vetices */

  /* Have center and radius - get lines */
  shift_x = radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = radius_pnt->pnt.y - center_pnt->pnt.y;

  radius = sqrt((shift_x*shift_x) + (shift_y*shift_y));

  /* Lines */
  ang_grid = 2*M_PI/(gdouble)GPOINTER_TO_INT(obj->type_data);
  offset_angle = atan2(shift_y,shift_x);

  for(loop = 0 ; loop < GPOINTER_TO_INT(obj->type_data) ; loop++)
    {
      gdouble lx,ly;
      GfigPoint calc_pnt;
      
      ang_loop = (gdouble)loop * ang_grid + offset_angle;
	
      lx = radius * cos(ang_loop);
      ly = radius * sin(ang_loop);

      calc_pnt.x = (gint)rint(lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)rint(ly + center_pnt->pnt.y);

      /* Miss out duped pnts */
      if(!first)
	{
	  if(calc_pnt.x == last_pnt.x && calc_pnt.y == last_pnt.y)
	    {
	      continue;
	    }
	}

      last_pnt.x = line_pnts[i++] = calc_pnt.x;
      last_pnt.y = line_pnts[i++] = calc_pnt.y;

      if(first)
	{
	  first_pnt.x = calc_pnt.x;
	  first_pnt.y = calc_pnt.y;
	  first = 0;
	}
    }

  line_pnts[i++] = first_pnt.x;
  line_pnts[i++] = first_pnt.y;

  /* Reverse line if approp */
  if(selvals.reverselines)
    reverse_pairs_list(&line_pnts[0],i/2);

  /* Scale before drawing */
  if(selvals.scaletoimage)
    scale_to_original_xy(&line_pnts[0],i/2);
  else
    scale_to_xy(&line_pnts[0],i/2);

  /* One go */
  if(selvals.painttype == PAINT_BRUSH_TYPE)
    {
      switch(selvals.brshtype)
	{
	case BRUSH_BRUSH_TYPE:
	  return_vals = gimp_run_procedure ("gimp_paintbrush", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_FLOAT,(gdouble)selvals.brushfade,
					    PARAM_INT32,(i/2)*2*GFIG_LCC,/* GIMP BUG should be 2!!!!*/
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_PENCIL_TYPE:
	  return_vals = gimp_run_procedure ("gimp_pencil", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_AIRBRUSH_TYPE:
	  return_vals = gimp_run_procedure ("gimp_airbrush", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_FLOAT,(gdouble)selvals.airbrushpressure,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_PATTERN_TYPE:
	  return_vals = gimp_run_procedure ("gimp_clone", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_INT32, 1,
					    PARAM_FLOAT,(gdouble)0.0,
					    PARAM_FLOAT,(gdouble)0.0,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	default:
	  break;
	}
    }
  else
    {
      /* We want to do a selection */
      return_vals = gimp_run_procedure ("gimp_free_select", &nreturn_vals,
					PARAM_IMAGE, gfig_image,
					PARAM_INT32,(i/2)*2,
					PARAM_FLOATARRAY, &line_pnts[0],  
					PARAM_INT32,selopt.type,
					PARAM_INT32,selopt.antia,
					PARAM_INT32,selopt.feather,
					PARAM_FLOAT,(gdouble)selopt.feather_radius,
					PARAM_END);
    }

  gimp_destroy_params (return_vals, nreturn_vals);

  g_free(line_pnts);
}

static void
d_poly2lines(DOBJECT *obj)
{
  /* first point center */
  /* Next point is radius */
  DOBJPOINTS * center_pnt;
  DOBJPOINTS * radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble radius;
  gdouble offset_angle;
  gint loop;
  GfigPoint first_pnt = { 0, 0 }, last_pnt = { 0, 0 };
  gint first = 1;

  g_assert(obj != NULL);

#ifdef DEBUG
  printf("d_poly2lines --- \n");
#endif /* DEBUG */

  /* count - add one to close polygon */

  center_pnt = obj->points;

  if(!center_pnt)
    return; /* no-line */

  /* Undraw it to start with - removes control points */ 
  obj->drawfunc(obj);

  /* NULL out these points free later */
  obj->points = NULL;

  /* Go around all the points creating line points */

  radius_pnt = center_pnt->next; /* this defines the vertices */

  /* Have center and radius - get lines */
  shift_x = radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = radius_pnt->pnt.y - center_pnt->pnt.y;

  radius = sqrt((shift_x*shift_x) + (shift_y*shift_y));

  /* Lines */
  ang_grid = 2*M_PI/(gdouble)GPOINTER_TO_INT(obj->type_data);
  offset_angle = atan2(shift_y,shift_x);

  for(loop = 0 ; loop < GPOINTER_TO_INT(obj->type_data) ; loop++)
    {
      gdouble lx,ly;
      GfigPoint calc_pnt;
      
      ang_loop = (gdouble)loop * ang_grid + offset_angle;
	
      lx = radius * cos(ang_loop);
      ly = radius * sin(ang_loop);

      calc_pnt.x = (gint)rint(lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)rint(ly + center_pnt->pnt.y);

      if(!first)
	{
	  if(calc_pnt.x == last_pnt.x && calc_pnt.y == last_pnt.y)
	    {
	      continue;
	    }
	}

      d_pnt_add_line(obj,calc_pnt.x,calc_pnt.y,0);

      last_pnt.x = calc_pnt.x;
      last_pnt.y = calc_pnt.y;

      if(first)
	{
	  first_pnt.x = calc_pnt.x;
	  first_pnt.y = calc_pnt.y;
	  first = 0;
	}
    }

  d_pnt_add_line(obj,first_pnt.x,first_pnt.y,0);
  /* Free old pnts */
  d_delete_dobjpoints(center_pnt);

  /* hey we're a line now */
  obj->type = LINE;
  obj->drawfunc  = d_draw_line;
  obj->loadfunc  = d_load_line;
  obj->savefunc  = d_save_line;
  obj->paintfunc = d_paint_line;
  obj->copyfunc  = d_copy_line;

  /* draw it + control pnts */
  obj->drawfunc(obj);
}

static void
d_star2lines(DOBJECT *obj)
{
  /* first point center */
  /* Next point is radius */
  DOBJPOINTS * center_pnt;
  DOBJPOINTS * outer_radius_pnt;
  DOBJPOINTS * inner_radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble outer_radius;
  gdouble inner_radius;
  gdouble offset_angle;
  gint loop;
  GfigPoint first_pnt = { 0, 0 }, last_pnt = { 0, 0 };
  gint first = 1;

  g_assert(obj != NULL);

#ifdef DEBUG
  printf("d_star2lines --- \n");
#endif /* DEBUG */

  /* count - add one to close polygon */

  center_pnt = obj->points;

  if(!center_pnt)
    return; /* no-line */

  /* Undraw it to start with - removes control points */ 
  obj->drawfunc(obj);

  /* NULL out these points free later */
  obj->points = NULL;

  /* Go around all the points creating line points */
  /* Next point defines the radius */
  outer_radius_pnt = center_pnt->next; /* this defines the vetices */

  if(!outer_radius_pnt)
    {
#ifdef DEBUG
      g_warning("Internal error in star - no outer vertice point \n");
#endif /* DEBUG */
      return;
    }

  inner_radius_pnt = outer_radius_pnt->next; /* this defines the vetices */

  if(!inner_radius_pnt)
    {
#ifdef DEBUG
      g_warning("Internal error in star - no inner vertice point \n");
#endif /* DEBUG */
      return;
    }

  shift_x = outer_radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = outer_radius_pnt->pnt.y - center_pnt->pnt.y;

  outer_radius = sqrt((shift_x*shift_x) + (shift_y*shift_y));

  /* Lines */
  ang_grid = 2*M_PI/(2.0*(gdouble)GPOINTER_TO_INT(obj->type_data));
  offset_angle = atan2(shift_y,shift_x);

  shift_x = inner_radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = inner_radius_pnt->pnt.y - center_pnt->pnt.y;

  inner_radius = sqrt((shift_x*shift_x) + (shift_y*shift_y));

  for(loop = 0 ; loop < 2*GPOINTER_TO_INT(obj->type_data) ; loop++)
    {
      gdouble lx,ly;
      GfigPoint calc_pnt;
      
      ang_loop = (gdouble)loop * ang_grid + offset_angle;

      if(loop%2)
	{
	  lx = inner_radius * cos(ang_loop);
	  ly = inner_radius * sin(ang_loop);
	}
      else
	{
	  lx = outer_radius * cos(ang_loop);
	  ly = outer_radius * sin(ang_loop);
	}

      calc_pnt.x = (gint)rint(lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)rint(ly + center_pnt->pnt.y);

      if(!first)
	{
	  if(calc_pnt.x == last_pnt.x && calc_pnt.y == last_pnt.y)
	    {
	      continue;
	    }
	}

      d_pnt_add_line(obj,calc_pnt.x,calc_pnt.y,0);

      last_pnt.x = calc_pnt.x;
      last_pnt.y = calc_pnt.y;

      if(first)
	{
	  first_pnt.x = calc_pnt.x;
	  first_pnt.y = calc_pnt.y;
	  first = 0;
	}
    }

  d_pnt_add_line(obj,first_pnt.x,first_pnt.y,0);
  /* Free old pnts */
  d_delete_dobjpoints(center_pnt);

  /* hey we're a line now */
  obj->type = LINE;
  obj->drawfunc  = d_draw_line;
  obj->loadfunc  = d_load_line;
  obj->savefunc  = d_save_line;
  obj->paintfunc = d_paint_line;
  obj->copyfunc  = d_copy_line;

  /* draw it + control pnts */
  obj->drawfunc(obj);
}

DOBJECT *
d_copy_poly(DOBJECT * obj)
{
  DOBJECT *np;

#if DEBUG
  printf("Copy poly\n");
#endif /*DEBUG*/
  if(!obj)
    return(NULL);

  g_assert(obj->type == POLY);

  np = d_new_poly(obj->points->pnt.x,obj->points->pnt.y);

  np->points->next = d_copy_dobjpoints(obj->points->next);

  np->type_data = obj->type_data;

#if DEBUG
  printf("Done poly copy\n");
#endif /*DEBUG*/
  return(np);
}

DOBJECT *
d_new_poly(gint x, gint y)
{
  DOBJECT *nobj;
  DOBJPOINTS *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = (DOBJPOINTS *)g_malloc0(sizeof(DOBJPOINTS));

#if DEBUG
  printf("New POLY start at (%x,%x)\n",x,y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = (DOBJECT *)g_malloc0(sizeof(DOBJECT));

  nobj->type = POLY;
  nobj->type_data = GINT_TO_POINTER(3); /* Default to three sides */
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_poly;
  nobj->loadfunc  = d_load_poly;
  nobj->savefunc  = d_save_poly;
  nobj->paintfunc = d_paint_poly;
  nobj->copyfunc  = d_copy_poly;

  return(nobj);
}

void
d_update_poly(GfigPoint *pnt)
{
  DOBJPOINTS *center_pnt, *edge_pnt;
  gint saved_cnt_pnt = selvals.opts.showcontrol;

  /* Undraw last one then draw new one */
  center_pnt = obj_creating->points;
  
  if(!center_pnt)
    return; /* No points */

  /* Leave the first pnt alone -
   * Edge point defines "radius"
   * Only undraw if already have edge point.
   */

  /* Hack - turn off cnt points in draw routine 
   * Looking back over the other update routines I could
   * use this trick again and cut down on code size!
   */


  if((edge_pnt = center_pnt->next))
    {
      /* Undraw */
      draw_circle(&edge_pnt->pnt);
      selvals.opts.showcontrol = 0;
      d_draw_poly(obj_creating);

      edge_pnt->pnt.x = pnt->x;
      edge_pnt->pnt.y = pnt->y;
    }
  else
    {
      /* Radius is a few pixels away */
      /* First edge point */
      d_pnt_add_line(obj_creating,pnt->x,pnt->y,-1);
      edge_pnt = center_pnt->next;
    }

  /* draw it */
  selvals.opts.showcontrol = 0;
  d_draw_poly(obj_creating);
  selvals.opts.showcontrol = saved_cnt_pnt;

  /* Realy draw the control points */
  draw_circle(&edge_pnt->pnt);
}

/* first point is center 
 * next defines the radius
 */

void
d_poly_start(GfigPoint *pnt,gint shift_down)
{
  gint16 x,y;
  /* First is center point */
  obj_creating = d_new_poly(x = pnt->x, y = pnt->y);
  obj_creating->type_data = GINT_TO_POINTER(poly_num_sides);
}

void
d_poly_end(GfigPoint *pnt, gint shift_down)
{
  draw_circle(pnt);
  add_to_all_obj(current_obj,obj_creating);
  obj_creating = NULL;
}

/* ARC stuff */
/* Distance between two lines */
static double
dist(double x1,double y1, double x2, double y2)
{

  double s1 = x1 - x2;
  double s2 = y1 - y2;

  return(sqrt((s1*s1) + (s2*s2)));
}

/* Mid point of line returned */
static void
mid_point(double x1,double y1,double x2, double y2,double *mx,double *my)
{
  *mx = ((double)(x1 - x2))/2.0 + (double)x2;
  *my = ((double)(y1 - y2))/2.0 + (double)y2;
}

/* Careful about infinite grads */
static double
line_grad(double x1,double y1, double x2, double y2)
{
  double dx,dy;
  
  dx = x1 - x2;
  dy = y1 - y2;

  if(dx == 0.0)
    return (0.0); /* Infinite ! */

  return(dy/dx);
}

/* Constant of line that goes through x,y with grad lgrad */
static double
line_cons(double x, double y, double lgrad)
{
  return(y - lgrad*x);
}

/*Get grad & const for perpend. line to given points */
static void
line_definition(double x1, double y1, double x2, double y2, double *lgrad, double *lconst)
{
  double grad1;
  double midx,midy;

  grad1 = line_grad(x1,y1,x2,y2);

  if(grad1 == 0.0)
    {
#ifdef DEBUG
      printf("Infinite grad....\n");
#endif /* DEBUG */
      return;
    }

  mid_point(x1,y1,x2,y2,&midx,&midy);

  /* Invert grad for perpen gradient */

  *lgrad = -1.0/grad1;
  
  *lconst = line_cons(midx,midy,*lgrad);
}

/* Arch details 
 * Given three points get arc radius and the co-ords 
 * of center point.
 */

static void
arc_details(GfigPoint *vert_a,GfigPoint *vert_b, GfigPoint *vert_c,GfigPoint *center_pnt, gdouble *radius)
{
  /* Only vertices are in whole numbers - everything else is in doubles */
  double ax,ay;
  double bx,by;
  double cx,cy;

  double len_a,len_b,len_c;
  double sum_sides2;
  double area;
  double circumcircle_R;
  double line1_grad = 0.0,line1_const = 0.0;
  double line2_grad = 0.0,line2_const = 0.0;
  double inter_x=0.0,inter_y=0.0;
  int got_x=0,got_y=0;

  ax = (double)(vert_a->x);
  ay = (double)(vert_a->y);
  bx = (double)(vert_b->x);
  by = (double)(vert_b->y);
  cx = (double)(vert_c->x);
  cy = (double)(vert_c->y);

#ifdef DEBUG
  printf("Vertices (%f,%f),(%f,%f),(%f,%f)\n",ax,ay,bx,by,cx,cy);
#endif /* DEBUG */

  len_a = dist(ax,ay,bx,by);
  len_b = dist(bx,by,cx,cy);
  len_c = dist(cx,cy,ax,ay);
#ifdef DEBUG
  printf("len_a = %f, len_b = %f, len_c = %f\n",len_a,len_b,len_c);
#endif /* DEBUG */


  sum_sides2 = (fabs(len_a) + fabs(len_b) + fabs(len_c))/2;
#ifdef DEBUG
  printf("Sum sides / 2 = %f\n",sum_sides2);
#endif /* DEBUG */

  /* Area */
  area = sqrt(sum_sides2*(sum_sides2 - len_a)*(sum_sides2 - len_b)*(sum_sides2 - len_c));
#ifdef DEBUG
  printf("Area of triangle = %f\n",area);
#endif /* DEBUG */
  
  /* Circumcircle */
  circumcircle_R = len_a*len_b*len_c/(4*area);
  *radius = circumcircle_R;
#ifdef DEBUG
  printf("Circumcircle radius = %f\n",circumcircle_R);
#endif /* DEBUG */

  /* Deal with exceptions - I hate exceptions */

  if(ax == bx || ax == cx || cx == bx)
    {
      /* vert line -> mid point gives inter_x */
      if( ax == bx && bx == cx)
	{
	  /* Straight line */
	  double miny = ay;
	  double maxy = ay;

	  if(by > maxy)
	    maxy = by;
	  
	  if(by < miny)
	    miny = by;

	  if(cy > maxy)
	    maxy = cy;

	  if(cy < miny)
	    miny = cy;

	  inter_y = (maxy - miny)/2 + miny;
	}
      else if(ax == bx)
	{
	  inter_y = (ay - by)/2 + by;
	}
      else if(bx == cx)
	{
	  inter_y = (by - cy)/2 + cy;
	}
      else
	{
	  inter_y = (cy - ay)/2 + ay;
	}
      got_y = 1;
    }

  if(ay == by || by == cy || ay == cy)
    {
      /* Horz line -> midpoint gives inter_y */
      if( ax == bx && bx == cx)
	{
	  /* Straight line */
	  double minx = ax;
	  double maxx = ax;

	  if(bx > maxx)
	    maxx = bx;
	  
	  if(bx < minx)
	    minx = bx;

	  if(cx > maxx)
	    maxx = cx;

	  if(cx < minx)
	    minx = cx;

	  inter_x = (maxx - minx)/2 + minx;
	}
      else if(ay == by)
	{
	  inter_x = (ax - bx)/2 + bx;
	}
      else if(by == cy)
	{
	  inter_x = (bx - cx)/2 + cx;
	}
      else
	{
	  inter_x = (cx - ax)/2 + ax;
	}
      got_x = 1;
    }

  if(!got_x || !got_y)
    {
      /* At least two of the lines are not parallel to the axis */
      /*first line */
      if(ax != bx && ay != by)
	line_definition(ax,ay,bx,by,&line1_grad,&line1_const);
      else
	line_definition(ax,ay,cx,cy,&line1_grad,&line1_const);
      /* second line */
      if(bx != cx && by != cy)
	line_definition(bx,by,cx,cy,&line2_grad,&line2_const);
      else
	line_definition(ax,ay,cx,cy,&line2_grad,&line2_const);
    }

  /* Intersection point */

  if(!got_x)
    inter_x = /*rint*/((line2_const - line1_const)/(line1_grad - line2_grad));
  if(!got_y)
    inter_y = /*rint*/((line1_grad * inter_x + line1_const));

#ifdef DEBUG
  printf("Intersection point is (%f,%f)\n",inter_x,inter_y);
#endif /* DEBUG */

  center_pnt->x = (gint16)inter_x;
  center_pnt->y = (gint16)inter_y;
}

static gdouble
arc_angle(GfigPoint *pnt, GfigPoint *center)
{
  /* Get angle (in degress) of point given origin of center */
  gint16 shift_x;
  gint16 shift_y;
  gdouble offset_angle;

  shift_x = pnt->x - center->x;
  shift_y = -pnt->y + center->y;
  offset_angle = atan2(shift_y,shift_x);
#ifdef DEBUG
  printf("offset_ang = %f\n",offset_angle);
#endif /* DEBUG */
  if(offset_angle < 0)
    offset_angle += 2*M_PI;

  return(offset_angle*360/(2*M_PI));
}

void
d_save_arc(DOBJECT * obj, FILE *to)
{
  DOBJPOINTS * spnt;

  spnt = obj->points;

  if(!spnt)
    return;

  fprintf(to,"<ARC>\n");

  while(spnt)
    {
      fprintf(to,"%d %d\n",
	      (gint)spnt->pnt.x,
	      (gint)spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf(to,"</ARC>\n");
}

/* Load a circle from the specified stream */

DOBJECT *
d_load_arc(FILE *from)
{
  DOBJECT *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];
  gint num_pnts = 0;

#ifdef DEBUG
  printf("Load arc called\n");
#endif /* DEBUG */

  while(get_line(buf,MAX_LOAD_LINE,from,0))
    {
      if(sscanf(buf,"%d %d",&xpnt,&ypnt) != 2)
	{
	  /* Must be the end */
	  if(strcmp("</ARC>",buf) || num_pnts != 3)
	    {
	      g_warning("[%d] Internal load error while loading arc",
			line_no);
	      return(NULL);
	    }
	  return(new_obj);
	}
      
      num_pnts++;

      if(!new_obj)
	new_obj = d_new_arc(xpnt,ypnt);
      else
	{
	  d_pnt_add_line(new_obj,xpnt,ypnt,-1);
	}
    }
  g_warning("[%d] Not enough points for arc",line_no);
  return(NULL);
}

static void
arc_drawing_details(DOBJECT *obj,
		    gdouble *minang,
		    GfigPoint *center_pnt,
		    gdouble *arcang,
		    gdouble *radius,
		    gint draw_cnts,
		    gint do_scale)
{
  DOBJPOINTS * pnt1 = NULL;
  DOBJPOINTS * pnt2 = NULL;
  DOBJPOINTS * pnt3 = NULL;
  DOBJPOINTS dpnts[3];
  gdouble ang1,ang2,ang3;
  gdouble maxang;

  pnt1 = obj->points;

  if(!pnt1)
    return; /* Not fully drawn */

  pnt2 = pnt1->next;

  if(!pnt2)
    return; /* Not fully drawn */

  pnt3 = pnt2->next;

  if(!pnt3)
    return; /* Still not fully drawn */

  if(draw_cnts)
    {
      draw_sqr(&pnt1->pnt);
      draw_sqr(&pnt2->pnt);
      draw_sqr(&pnt3->pnt);
    }

  if(do_scale)
    {
      /* Adjust pnts for scaling */
      /* Warning struct copies here! and casting to double <-> int */
      /* Too complex fix me - to much hacking */
      gdouble xy[2];
      int j;

      dpnts[0] = *pnt1;
      dpnts[1] = *pnt2;
      dpnts[2] = *pnt3;

      pnt1 = &dpnts[0];
      pnt2 = &dpnts[1];
      pnt3 = &dpnts[2];

      for(j = 0 ; j < 3; j++)
	{
	  xy[0] = dpnts[j].pnt.x;
	  xy[1] = dpnts[j].pnt.y;
	  if(selvals.scaletoimage)
	    scale_to_original_xy(&xy[0],1);
	  else
	    scale_to_xy(&xy[0],1);
	  dpnts[j].pnt.x = xy[0];
	  dpnts[j].pnt.y = xy[1];
	}
    }

  arc_details(&pnt1->pnt,&pnt2->pnt,&pnt3->pnt,center_pnt,radius);
  
  ang1 = arc_angle(&pnt1->pnt,center_pnt);
  ang2 = arc_angle(&pnt2->pnt,center_pnt);
  ang3 = arc_angle(&pnt3->pnt,center_pnt);

  /* Find min/max angle */

  maxang = ang1;

  if(ang3 > maxang)
    maxang = ang3;
  
  *minang = ang1;

  if(ang3 < *minang)
    *minang = ang3;

  if (ang2 > *minang && ang2 < maxang)
    *arcang = maxang - *minang;
  else
    *arcang = maxang - *minang - 360;
}

static void
d_draw_arc(DOBJECT * obj)
{
  GfigPoint center_pnt;
  gdouble radius,minang,arcang;

  g_assert(obj != NULL);

  if(!obj)
    return;

  arc_drawing_details(obj,&minang,&center_pnt,&arcang,&radius,TRUE,FALSE);
  
#ifdef DEBUG
  printf("Min ang = %f Arc ang = %f\n",minang,arcang);
#endif /* DEBUG */

  if(drawing_pic)
    {
      gfig_draw_arc (0,
		    adjust_pic_coords(center_pnt.x - (gint)radius,
				      preview_width),
		    adjust_pic_coords(center_pnt.y - (gint)radius,
				      preview_height),
		    adjust_pic_coords((gint)(radius * 2),
				      preview_width),
		    adjust_pic_coords((gint)(radius * 2),
				      preview_height),
		    (gint)(minang*64),
		    (gint)(arcang*64));
    }
  else
    {
      gfig_draw_arc (0,
		    gfig_scale_x(center_pnt.x - (gint)radius),
		    gfig_scale_y(center_pnt.y - (gint)radius),
		    gfig_scale_x((gint)(radius * 2)),
		    gfig_scale_y((gint)(radius * 2)),
		    (gint)(minang*64),
		    (gint)(arcang*64));
    }
}

static void
d_paint_arc(DOBJECT *obj)
{
  /* first point center */
  /* Next point is radius */
  gdouble *line_pnts;
  GParam *return_vals = NULL;
  gint nreturn_vals;
  gint seg_count = 0;
  gint i = 0;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble radius;
  gint loop;
  GfigPoint last_pnt = { 0, 0 };
  gint first = 1;
  GfigPoint center_pnt;
  gdouble minang,arcang;

  g_assert(obj != NULL);

  if(!obj)
    return;

  /* No cnt pnts & must scale */
  arc_drawing_details(obj,&minang,&center_pnt,&arcang,&radius,FALSE,TRUE);

#ifdef DEBUG
  printf("Paint Min ang = %f Arc ang = %f\n",minang,arcang);
#endif /* DEBUG */

  seg_count = 360; /* Should make a smoth-ish curve */

  /* The second 2* to get around bug in GIMP */
  /* +3 because we MIGHT do pie selection */
  line_pnts = g_malloc0(GFIG_LCC*(2*seg_count + 3)*sizeof(gdouble));

  /* Lines */
  ang_grid = 2*M_PI/(gdouble)360;

  if(arcang < 0.0)
    {
      /* Swap - since we always draw anti-clock wise */
      minang += arcang;
      arcang = -arcang;
    }

  minang = minang * (2*M_PI/360); /* min ang is in degrees - need in rads*/

  for(loop = 0 ; loop < abs((gint)arcang) ; loop++)
    {
      gdouble lx,ly;
      GfigPoint calc_pnt;
      
      ang_loop = (gdouble)loop * ang_grid + minang;

      lx = radius * cos(ang_loop);
      ly = -radius * sin(ang_loop); /* y grows down screen and angs measured from x clockwise */

      calc_pnt.x = (gint)rint(lx + center_pnt.x);
      calc_pnt.y = (gint)rint(ly + center_pnt.y);

      /* Miss out duped pnts */
      if(!first)
	{
	  if(calc_pnt.x == last_pnt.x && calc_pnt.y == last_pnt.y)
	    {
	      continue;
	    }
	}

      last_pnt.x = line_pnts[i++] = calc_pnt.x;
      last_pnt.y = line_pnts[i++] = calc_pnt.y;

      if(first)
	{
	  first = 0;
	}
    }

  /* Reverse line if approp */
  if(selvals.reverselines)
    reverse_pairs_list(&line_pnts[0],i/2);

  /* One go */
  if(selvals.painttype == PAINT_BRUSH_TYPE)
    {
      switch(selvals.brshtype)
	{
	case BRUSH_BRUSH_TYPE:
	  return_vals = gimp_run_procedure ("gimp_paintbrush", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_FLOAT,(gdouble)selvals.brushfade,
					    PARAM_INT32,(i/2)*2*GFIG_LCC,/* GIMP BUG should be 2!!!!*/
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_PENCIL_TYPE:
	  return_vals = gimp_run_procedure ("gimp_pencil", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_AIRBRUSH_TYPE:
	  return_vals = gimp_run_procedure ("gimp_airbrush", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_FLOAT,(gdouble)selvals.airbrushpressure,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_PATTERN_TYPE:
	  return_vals = gimp_run_procedure ("gimp_clone", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_INT32, 1,
					    PARAM_FLOAT,(gdouble)0.0,
					    PARAM_FLOAT,(gdouble)0.0,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	default:
	  break;
	}
    }
  else
    {
      if(selopt.as_pie)
	{
	  /* Add center point - cause a pie like selection... */
	  line_pnts[i++] = center_pnt.x;
	  line_pnts[i++] = center_pnt.y;
	}

      /* We want to do a selection */
      return_vals = gimp_run_procedure ("gimp_free_select", &nreturn_vals,
					PARAM_IMAGE, gfig_image,
					PARAM_INT32,(i/2)*2,
					PARAM_FLOATARRAY, &line_pnts[0],  
					PARAM_INT32,selopt.type,
					PARAM_INT32,selopt.antia,
					PARAM_INT32,selopt.feather,
					PARAM_FLOAT,(gdouble)selopt.feather_radius,
					PARAM_END);
    }
  
  gimp_destroy_params (return_vals, nreturn_vals);

  g_free(line_pnts);
}

DOBJECT *
d_copy_arc(DOBJECT * obj)
{
  DOBJECT *nc;

#if DEBUG
  printf("Copy ellipse\n");
#endif /*DEBUG*/
  if(!obj)
    return(NULL);

  g_assert(obj->type == ARC);

  nc = d_new_arc(obj->points->pnt.x,obj->points->pnt.y);

  nc->points->next = d_copy_dobjpoints(obj->points->next);

#if DEBUG
  printf("Arc (%x,%x),(%x,%x),(%x,%x)\n",
	 nc->points->pnt.x,obj->points->pnt.y,
	 nc->points->next->pnt.x,obj->points->next->pnt.y,
	 nc->points->next->next->pnt.x,obj->points->next->next->pnt.y);
  printf("Done copy\n");
#endif /*DEBUG*/
  return(nc);
}

DOBJECT *
d_new_arc(gint x, gint y)
{
  DOBJECT *nobj;
  DOBJPOINTS *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = (DOBJPOINTS *)g_malloc0(sizeof(DOBJPOINTS));

#if DEBUG
  printf("New arc start at (%x,%x)\n",x,y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = (DOBJECT *)g_malloc0(sizeof(DOBJECT));

  nobj->type = ARC;
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_arc;
  nobj->loadfunc  = d_load_arc;
  nobj->savefunc  = d_save_arc;
  nobj->paintfunc = d_paint_arc;
  nobj->copyfunc  = d_copy_arc;

  return(nobj);
}

void
d_update_arc(GfigPoint *pnt)
{
  DOBJPOINTS * pnt1 = NULL;
  DOBJPOINTS * pnt2 = NULL;
  DOBJPOINTS * pnt3 = NULL;

  /* First two points as line only become arch when third
   * point is placed on canvas.
   */

  pnt1 = obj_creating->points;

  if(!pnt1 ||
     !(pnt2 = pnt1->next) ||
     !(pnt3 = pnt2->next))
    {
      d_update_line(pnt);
      return; /* Not fully drawn */
    }

  /* Update a real curve */
  /* Nothing to be done ... */
}

void
d_arc_start(GfigPoint *pnt,gint shift_down)
{
  /* Draw lines to start with -- then convert to an arc */
  if(!tmp_line)
    draw_sqr(pnt);
  d_line_start(pnt,TRUE); /* TRUE means multiple pointed line */
}

void
d_arc_end(GfigPoint *pnt, gint shift_down)
{
  /* Under contrl point */
  if(!tmp_line ||
     !tmp_line->points ||
     !tmp_line->points->next)
    {
      /* No arc created  - yet */
      /* Must have three points */
#ifdef DEBUG
      printf("No arc created yet\n");
#endif /* DEBUG */
      d_line_end(pnt,TRUE);
    }
  else
    {
      /* Complete arc */
      /* Convert to an arc ... */
      tmp_line->type = ARC;
      tmp_line->drawfunc  = d_draw_arc;
      tmp_line->loadfunc  = d_load_arc;
      tmp_line->savefunc  = d_save_arc;
      tmp_line->paintfunc = d_paint_arc;
      tmp_line->copyfunc  = d_copy_arc;
      d_line_end(pnt,FALSE);
      /*d_draw_line(newarc);  Should undraw line */
      if(need_to_scale)
	{
	  selvals.scaletoimage = 0;
	}
      /*d_draw_arc(newarc);*/
      update_draw_area(gfig_preview,NULL);
      if(need_to_scale)
	{
	  selvals.scaletoimage = 1;
	}

    }
}
/*XXXXXXXXXXXXXXXXXXXXXXX*/
/* Star shape */

void
d_save_star(DOBJECT * obj, FILE *to)
{
  DOBJPOINTS * spnt;
  
  spnt = obj->points;

  if(!spnt)
    return; /* End-of-line */

  fprintf(to,"<STAR>\n");

  while(spnt)
    {
      fprintf(to,"%d %d\n",
	      (gint)spnt->pnt.x,
	      (gint)spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf(to,"<EXTRA>\n");
  fprintf(to,"%d\n</EXTRA>\n",GPOINTER_TO_INT(obj->type_data));
  fprintf(to,"</STAR>\n");
}

/* Load a circle from the specified stream */

DOBJECT *
d_load_star(FILE *from)
{
  DOBJECT *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];

#ifdef DEBUG
  printf("Load star called\n");
#endif /* DEBUG */

  while(get_line(buf,MAX_LOAD_LINE,from,0))
    {
      if(sscanf(buf,"%d %d",&xpnt,&ypnt) != 2)
	{
	  /* Must be the end */
	  if(!strcmp("<EXTRA>",buf))
	    {
	      gint nsides = 3;
	      /* Number of sides - data item */
	      if(!new_obj)
		{
		  g_warning("[%d] Internal load error while loading star (extra area)",
			    line_no);
		  return(NULL);
		}
	      get_line(buf,MAX_LOAD_LINE,from,0);
	      if(sscanf(buf,"%d",&nsides) != 1)
		{
		  g_warning("[%d] Internal load error while loading star (extra area scanf)",
			    line_no);
		  return(NULL);
		}
	      if(nsides < 3 || nsides > 200)
		{
		  g_warning("[%d] Invalid value while loading star",
			    line_no);
		  return(NULL);
		}
	      new_obj->type_data = GINT_TO_POINTER(nsides);
	      get_line(buf,MAX_LOAD_LINE,from,0);
	      if(strcmp("</EXTRA>",buf))
		{
		  g_warning("[%d] Internal load error while loading star",
			    line_no);
		  return(NULL);
		} 
	      /* Go around and read the last line */
	      continue;
	    }
	  else if(strcmp("</STAR>",buf))
	    {
	      g_warning("[%d] Internal load error while loading star",
			line_no);
	      return(NULL);
	    }
	  return(new_obj);
	}
      
      if(!new_obj)
	new_obj = d_new_star(xpnt,ypnt);
      else
	d_pnt_add_line(new_obj,xpnt,ypnt,-1);
    }
  return(new_obj);
}

static void
d_draw_star(DOBJECT *obj)
{
  DOBJPOINTS * center_pnt;
  DOBJPOINTS * outer_radius_pnt;
  DOBJPOINTS * inner_radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble outer_radius;
  gdouble inner_radius;
  gdouble offset_angle;
  gint loop;
  GfigPoint start_pnt = { 0, 0 };
  GfigPoint first_pnt = { 0, 0 };
  gint do_line = 0;

  center_pnt = obj->points;

  if(!center_pnt)
    return; /* End-of-line */

  /* First point is the center */
  /* Just draw a control point around it */

  draw_sqr(&center_pnt->pnt);

  /* Next point defines the radius */
  outer_radius_pnt = center_pnt->next; /* this defines the vetices */

  if(!outer_radius_pnt)
    {
#ifdef DEBUG
      g_warning("Internal error in star - no outer vertice point \n");
#endif /* DEBUG */
      return;
    }

  inner_radius_pnt = outer_radius_pnt->next; /* this defines the vetices */

  if(!inner_radius_pnt)
    {
#ifdef DEBUG
      g_warning("Internal error in star - no inner vertice point \n");
#endif /* DEBUG */
      return;
    }

  /* Other control points */
  draw_sqr(&outer_radius_pnt->pnt);
  draw_sqr(&inner_radius_pnt->pnt);

  /* Have center and radius - draw star */

  shift_x = outer_radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = outer_radius_pnt->pnt.y - center_pnt->pnt.y;

  outer_radius = sqrt((shift_x*shift_x) + (shift_y*shift_y));

  /* Lines */
  ang_grid = 2*M_PI/(2.0*(gdouble)GPOINTER_TO_INT(obj->type_data));
  offset_angle = atan2(shift_y,shift_x);

  shift_x = inner_radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = inner_radius_pnt->pnt.y - center_pnt->pnt.y;

  inner_radius = sqrt((shift_x*shift_x) + (shift_y*shift_y));

  for(loop = 0 ; loop < 2*GPOINTER_TO_INT(obj->type_data) ; loop++)
    {
      gdouble lx,ly;
      GfigPoint calc_pnt;

      ang_loop = (gdouble)loop * ang_grid + offset_angle;
	
      if(loop%2)
	{
	  lx = inner_radius * cos(ang_loop);
	  ly = inner_radius * sin(ang_loop);
	}
      else
	{
	  lx = outer_radius * cos(ang_loop);
	  ly = outer_radius * sin(ang_loop);
	}

      calc_pnt.x = (gint)rint(lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)rint(ly + center_pnt->pnt.y);

      if(do_line)
	{

	  /* Miss out points that come to the same location */
	  if(calc_pnt.x == start_pnt.x && calc_pnt.y == start_pnt.y)
	    continue;

	  if(drawing_pic)
	    {
	      gfig_draw_line (adjust_pic_coords(calc_pnt.x,
					      preview_width),
			    adjust_pic_coords(calc_pnt.y,
					      preview_height),
			    adjust_pic_coords(start_pnt.x,
					      preview_width),
			    adjust_pic_coords(start_pnt.y,
					      preview_height));
	    }
	  else
	    {
      gfig_draw_line (gfig_scale_x(calc_pnt.x),
			    gfig_scale_y(calc_pnt.y),
			    gfig_scale_x(start_pnt.x),
			    gfig_scale_y(start_pnt.y));
	    }
	}
      else
	{
	  do_line = 1;
	  first_pnt.x = calc_pnt.x;
	  first_pnt.y = calc_pnt.y;
	}
      start_pnt.x = calc_pnt.x;
      start_pnt.y = calc_pnt.y;
    }

  /* Join up */
  if(drawing_pic)
    {
      gfig_draw_line (adjust_pic_coords(first_pnt.x,preview_width),
		adjust_pic_coords(first_pnt.y,preview_width),
		adjust_pic_coords(start_pnt.x,preview_width),
		adjust_pic_coords(start_pnt.y,preview_width));
    }
  else
    {
      gfig_draw_line (gfig_scale_x(first_pnt.x),
		gfig_scale_y(first_pnt.y),
		gfig_scale_x(start_pnt.x),
		gfig_scale_y(start_pnt.y));
    }
}

static void
d_paint_star(DOBJECT *obj)
{
  /* first point center */
  /* Next point is radius */
  gdouble *line_pnts;
  GParam *return_vals = NULL;
  gint nreturn_vals;
  gint seg_count = 0;
  gint i = 0;
  DOBJPOINTS * center_pnt;
  DOBJPOINTS * outer_radius_pnt;
  DOBJPOINTS * inner_radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble outer_radius;
  gdouble inner_radius;

  gdouble offset_angle;
  gint loop;
  GfigPoint first_pnt = { 0, 0 }, last_pnt = { 0, 0 };
  gint first = 1;

  g_assert(obj != NULL);

  /* count - add one to close polygon */
  seg_count = 2*GPOINTER_TO_INT(obj->type_data) + 1;

  center_pnt = obj->points;

  if(!center_pnt || !seg_count)
    return; /* no-line */

  /* The second 2* to get around bug in GIMP */
  line_pnts = g_malloc0(GFIG_LCC*(2*seg_count + 1)*sizeof(gdouble));
  
  /* Go around all the points drawing a line from one to the next */
  /* Next point defines the radius */
  outer_radius_pnt = center_pnt->next; /* this defines the vetices */

  if(!outer_radius_pnt)
    {
#ifdef DEBUG
      g_warning("Internal error in star - no outer vertice point \n");
#endif /* DEBUG */
      return;
    }

  inner_radius_pnt = outer_radius_pnt->next; /* this defines the vetices */

  if(!inner_radius_pnt)
    {
#ifdef DEBUG
      g_warning("Internal error in star - no inner vertice point \n");
#endif /* DEBUG */
      return;
    }

  shift_x = outer_radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = outer_radius_pnt->pnt.y - center_pnt->pnt.y;

  outer_radius = sqrt((shift_x*shift_x) + (shift_y*shift_y));

  /* Lines */
  ang_grid = 2*M_PI/(2.0*(gdouble)GPOINTER_TO_INT(obj->type_data));
  offset_angle = atan2(shift_y,shift_x);

  shift_x = inner_radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = inner_radius_pnt->pnt.y - center_pnt->pnt.y;

  inner_radius = sqrt((shift_x*shift_x) + (shift_y*shift_y));

  for(loop = 0 ; loop < 2*GPOINTER_TO_INT(obj->type_data) ; loop++)
    {
      gdouble lx,ly;
      GfigPoint calc_pnt;
      
      ang_loop = (gdouble)loop * ang_grid + offset_angle;
	
      if(loop%2)
	{
	  lx = inner_radius * cos(ang_loop);
	  ly = inner_radius * sin(ang_loop);
	}
      else
	{
	  lx = outer_radius * cos(ang_loop);
	  ly = outer_radius * sin(ang_loop);
	}

      calc_pnt.x = (gint)rint(lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)rint(ly + center_pnt->pnt.y);

      /* Miss out duped pnts */
      if(!first)
	{
	  if(calc_pnt.x == last_pnt.x && calc_pnt.y == last_pnt.y)
	    {
	      continue;
	    }
	}

      last_pnt.x = line_pnts[i++] = calc_pnt.x;
      last_pnt.y = line_pnts[i++] = calc_pnt.y;

      if(first)
	{
	  first_pnt.x = calc_pnt.x;
	  first_pnt.y = calc_pnt.y;
	  first = 0;
	}
    }

  line_pnts[i++] = first_pnt.x;
  line_pnts[i++] = first_pnt.y;

  /* Reverse line if approp */
  if(selvals.reverselines)
    reverse_pairs_list(&line_pnts[0],i/2);

  /* Scale before drawing */
  if(selvals.scaletoimage)
    scale_to_original_xy(&line_pnts[0],i/2);
  else
    scale_to_xy(&line_pnts[0],i/2);

  /* One go */
    /* One go */
  if(selvals.painttype == PAINT_BRUSH_TYPE)
    {
      switch(selvals.brshtype)
	{
	case BRUSH_BRUSH_TYPE:
	  return_vals = gimp_run_procedure ("gimp_paintbrush", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_FLOAT,(gdouble)selvals.brushfade,
					    PARAM_INT32,(i/2)*2*GFIG_LCC,/* GIMP BUG should be 2!!!!*/
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_PENCIL_TYPE:
	  return_vals = gimp_run_procedure ("gimp_pencil", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_AIRBRUSH_TYPE:
	  return_vals = gimp_run_procedure ("gimp_airbrush", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_FLOAT,(gdouble)selvals.airbrushpressure,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_PATTERN_TYPE:
	  return_vals = gimp_run_procedure ("gimp_clone", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_INT32, 1,
					    PARAM_FLOAT,(gdouble)0.0,
					    PARAM_FLOAT,(gdouble)0.0,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	default:
	  break;
	}
    }
  else
    {
      /* We want to do a selection */
      return_vals = gimp_run_procedure ("gimp_free_select", &nreturn_vals,
					PARAM_IMAGE, gfig_image,
					PARAM_INT32,(i/2)*2,
					PARAM_FLOATARRAY, &line_pnts[0],  
					PARAM_INT32,selopt.type,
					PARAM_INT32,selopt.antia,
					PARAM_INT32,selopt.feather,
					PARAM_FLOAT,(gdouble)selopt.feather_radius,
					PARAM_END);

    }

  gimp_destroy_params (return_vals, nreturn_vals);

  g_free(line_pnts);
}

DOBJECT *
d_copy_star(DOBJECT * obj)
{
  DOBJECT *np;

#if DEBUG
  printf("Copy star\n");
#endif /*DEBUG*/
  if(!obj)
    return(NULL);

  g_assert(obj->type == STAR);

  np = d_new_star(obj->points->pnt.x,obj->points->pnt.y);

  np->points->next = d_copy_dobjpoints(obj->points->next);

  np->type_data = obj->type_data;

#if DEBUG
  printf("Done star copy\n");
#endif /*DEBUG*/
  return(np);
}

DOBJECT *
d_new_star(gint x, gint y)
{
  DOBJECT *nobj;
  DOBJPOINTS *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = (DOBJPOINTS *)g_malloc0(sizeof(DOBJPOINTS));

#if DEBUG
  printf("New STAR start at (%x,%x)\n",x,y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = (DOBJECT *)g_malloc0(sizeof(DOBJECT));

  nobj->type = STAR;
  nobj->type_data = GINT_TO_POINTER(3); /* Default to three sides 6 points*/
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_star;
  nobj->loadfunc  = d_load_star;
  nobj->savefunc  = d_save_star;
  nobj->paintfunc = d_paint_star;
  nobj->copyfunc  = d_copy_star;

  return(nobj);
}

void
d_update_star(GfigPoint *pnt)
{
  DOBJPOINTS *center_pnt, *inner_pnt, *outer_pnt;
  gint saved_cnt_pnt = selvals.opts.showcontrol;

  /* Undraw last one then draw new one */
  center_pnt = obj_creating->points;
  
  if(!center_pnt)
    return; /* No points */

  /* Leave the first pnt alone -
   * Edge point defines "radius"
   * Only undraw if already have edge point.
   */

  /* Hack - turn off cnt points in draw routine 
   * Looking back over the other update routines I could
   * use this trick again and cut down on code size!
   */


  if((outer_pnt = center_pnt->next))
    {
      /* Undraw */
      inner_pnt = outer_pnt->next;
      draw_circle(&inner_pnt->pnt);
      draw_circle(&outer_pnt->pnt);
      selvals.opts.showcontrol = 0;
      d_draw_star(obj_creating);
      outer_pnt->pnt.x = pnt->x;
      outer_pnt->pnt.y = pnt->y;
      inner_pnt->pnt.x = pnt->x + (2*(center_pnt->pnt.x - pnt->x))/3;
      inner_pnt->pnt.y = pnt->y + (2*(center_pnt->pnt.y - pnt->y))/3;
    }
  else
    {
      /* Radius is a few pixels away */
      /* First edge point */
      d_pnt_add_line(obj_creating,pnt->x,pnt->y,-1);
      outer_pnt = center_pnt->next;
      /* Inner radius */
      d_pnt_add_line(obj_creating,
		     pnt->x + (2*(center_pnt->pnt.x - pnt->x))/3,
		     pnt->y + (2*(center_pnt->pnt.y - pnt->y))/3,
		     -1);
      inner_pnt = outer_pnt->next;
    }

  /* draw it */
  selvals.opts.showcontrol = 0;
  d_draw_star(obj_creating);
  selvals.opts.showcontrol = saved_cnt_pnt;

  /* Realy draw the control points */
  draw_circle(&outer_pnt->pnt);
  draw_circle(&inner_pnt->pnt);
}

/* first point is center 
 * next defines the radius
 */

void
d_star_start(GfigPoint *pnt,gint shift_down)
{
  gint16 x,y;
  /* First is center point */
  obj_creating = d_new_star(x = pnt->x, y = pnt->y);
  obj_creating->type_data = GINT_TO_POINTER(star_num_sides);
}

void
d_star_end(GfigPoint *pnt, gint shift_down)
{
  draw_circle(pnt);
  add_to_all_obj(current_obj,obj_creating);
  obj_creating = NULL;
}


/* Spiral */

void
d_save_spiral(DOBJECT * obj, FILE *to)
{
  DOBJPOINTS * spnt;
  
  spnt = obj->points;

  if(!spnt)
    return; /* End-of-line */

  fprintf(to,"<SPIRAL>\n");

  while(spnt)
    {
      fprintf(to,"%d %d\n",
	      (gint)spnt->pnt.x,
	      (gint)spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf(to,"<EXTRA>\n");
  fprintf(to,"%d\n</EXTRA>\n",GPOINTER_TO_INT(obj->type_data));
  fprintf(to,"</SPIRAL>\n");

}

/* Load a spiral from the specified stream */

DOBJECT *
d_load_spiral(FILE *from)
{
  DOBJECT *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];

#ifdef DEBUG
  printf("Load spiral called\n");
#endif /* DEBUG */

  while(get_line(buf,MAX_LOAD_LINE,from,0))
    {
      if(sscanf(buf,"%d %d",&xpnt,&ypnt) != 2)
	{
	  /* Must be the end */
	  if(!strcmp("<EXTRA>",buf))
	    {
	      gint nsides = 3;
	      /* Number of sides - data item */
	      if(!new_obj)
		{
		  g_warning("[%d] Internal load error while loading spiral (extra area)",
			    line_no);
		  return(NULL);
		}
	      get_line(buf,MAX_LOAD_LINE,from,0);
	      if(sscanf(buf,"%d",&nsides) != 1)
		{
		  g_warning("[%d] Internal load error while loading spiral (extra area scanf)",
			    line_no);
		  return(NULL);
		}
	      if(nsides == 0 || nsides < -20 || nsides > 20)
		{
		  g_warning("[%d] Invalid value while loading spiral",
			    line_no);
		  return(NULL);
		}
	      new_obj->type_data = GINT_TO_POINTER(nsides);
	      get_line(buf,MAX_LOAD_LINE,from,0);
	      if(strcmp("</EXTRA>",buf))
		{
		  g_warning("[%d] Internal load error while loading spiral",
			    line_no);
		  return(NULL);
		} 
	      /* Go around and read the last line */
	      continue;
	    }
	  else if(strcmp("</SPIRAL>",buf))
	    {
	      g_warning("[%d] Internal load error while loading spiral",
			line_no);
	      return(NULL);
	    }
	  return(new_obj);
	}
      
      if(!new_obj)
	new_obj = d_new_spiral(xpnt,ypnt);
      else
	d_pnt_add_line(new_obj,xpnt,ypnt,-1);
    }
  return(new_obj);
}

static void
d_draw_spiral(DOBJECT *obj)
{
  DOBJPOINTS * center_pnt;
  DOBJPOINTS * radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble radius;
  gdouble offset_angle;
  gdouble sp_cons;
  gint loop;
  GfigPoint start_pnt = { 0, 0 };
  gint do_line = 0;
  gint clock_wise = 1;

  center_pnt = obj->points;

  if(!center_pnt)
    return; /* End-of-line */

  /* First point is the center */
  /* Just draw a control point around it */

  draw_sqr(&center_pnt->pnt);

  /* Next point defines the radius */
  radius_pnt = center_pnt->next; /* this defines the vetices */

  if(!radius_pnt)
    {
#ifdef DEBUG
      g_warning("Internal error in spiral - no vertice point \n");
#endif /* DEBUG */
      return;
    }

  /* Other control point */
  draw_sqr(&radius_pnt->pnt);

  /* Have center and radius - draw spiral */

  shift_x = radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = radius_pnt->pnt.y - center_pnt->pnt.y;

  radius = sqrt((shift_x*shift_x) + (shift_y*shift_y));

  offset_angle = atan2(shift_y,shift_x);

  clock_wise = (GPOINTER_TO_INT(obj->type_data))/(abs(GPOINTER_TO_INT(obj->type_data)));

  if(offset_angle < 0)
    offset_angle += 2*M_PI;

  sp_cons = radius/(GPOINTER_TO_INT(obj->type_data) * 2 * M_PI + offset_angle);
  /* Lines */
  ang_grid = 2.0*M_PI/(gdouble)180;


  for(loop = 0 ; loop <= abs(GPOINTER_TO_INT(obj->type_data)*180) + clock_wise*(gint)rint(offset_angle/ang_grid) ; loop++)
    {
      gdouble lx,ly;
      GfigPoint calc_pnt;

      ang_loop = (gdouble)loop * ang_grid;
	
      lx = sp_cons * ang_loop * cos(ang_loop)*clock_wise;
      ly = sp_cons * ang_loop * sin(ang_loop);

      calc_pnt.x = (gint)rint(lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)rint(ly + center_pnt->pnt.y);

      if(do_line)
	{

	  /* Miss out points that come to the same location */
	  if(calc_pnt.x == start_pnt.x && calc_pnt.y == start_pnt.y)
	    continue;

	  if(drawing_pic)
	    {
	      gfig_draw_line (adjust_pic_coords(calc_pnt.x,
					      preview_width),
			    adjust_pic_coords(calc_pnt.y,
					      preview_height),
			    adjust_pic_coords(start_pnt.x,
					      preview_width),
			    adjust_pic_coords(start_pnt.y,
					      preview_height));
	    }
	  else
	    {
	      gfig_draw_line (gfig_scale_x(calc_pnt.x),
			    gfig_scale_y(calc_pnt.y),
			    gfig_scale_x(start_pnt.x),
			    gfig_scale_y(start_pnt.y));
	    }
	}
      else
	{
	  do_line = 1;
	}
      start_pnt.x = calc_pnt.x;
      start_pnt.y = calc_pnt.y;
    }
}

static void
d_paint_spiral(DOBJECT *obj)
{
  /* first point center */
  /* Next point is radius */
  gdouble *line_pnts;
  GParam *return_vals = NULL;
  gint nreturn_vals;
  gint seg_count = 0;
  gint i = 0;
  DOBJPOINTS * center_pnt;
  DOBJPOINTS * radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble radius;
  gdouble offset_angle;
  gdouble sp_cons;
  gint loop;
  GfigPoint last_pnt = { 0, 0 };
  gint clock_wise = 1;

  g_assert(obj != NULL);

  center_pnt = obj->points;

  if(!center_pnt || !center_pnt->next)
    return; /* no-line */

  /* Go around all the points drawing a line from one to the next */

  radius_pnt = center_pnt->next; /* this defines the vetices */

  /* Have center and radius - get lines */
  shift_x = radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = radius_pnt->pnt.y - center_pnt->pnt.y;

  radius = sqrt((shift_x*shift_x) + (shift_y*shift_y));

  clock_wise = (GPOINTER_TO_INT(obj->type_data))/(abs(GPOINTER_TO_INT(obj->type_data)));

  offset_angle = atan2(shift_y,shift_x);

  if(offset_angle < 0)
    offset_angle += 2*M_PI;

  sp_cons = radius/(GPOINTER_TO_INT(obj->type_data) * 2 * M_PI + offset_angle);
  /* Lines */
  ang_grid = 2.0*M_PI/(gdouble)180;


  /* count - */
  seg_count = abs(GPOINTER_TO_INT(obj->type_data)*180) + clock_wise*(gint)rint(offset_angle/ang_grid);

  /* The second 2* to get around bug in GIMP */
  line_pnts = g_malloc0(GFIG_LCC*(2*seg_count + 3)*sizeof(gdouble));

  for(loop = 0 ; loop <= seg_count; loop++)
    {
      gdouble lx,ly;
      GfigPoint calc_pnt;

      ang_loop = (gdouble)loop * ang_grid;
	
      lx = sp_cons * ang_loop * cos(ang_loop)*clock_wise;
      ly = sp_cons * ang_loop * sin(ang_loop);

      calc_pnt.x = (gint)rint(lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)rint(ly + center_pnt->pnt.y);

      /* Miss out duped pnts */
      if(!loop)
	{
	  if(calc_pnt.x == last_pnt.x && calc_pnt.y == last_pnt.y)
	    {
	      continue;
	    }
	}

      last_pnt.x = line_pnts[i++] = calc_pnt.x;
      last_pnt.y = line_pnts[i++] = calc_pnt.y;
    }

  /* Reverse line if approp */
  if(selvals.reverselines)
    reverse_pairs_list(&line_pnts[0],i/2);

  /* Scale before drawing */
  if(selvals.scaletoimage)
    scale_to_original_xy(&line_pnts[0],i/2);
  else
    scale_to_xy(&line_pnts[0],i/2);

  /* One go */
  /* One go */
  if(selvals.painttype == PAINT_BRUSH_TYPE)
    {
      switch(selvals.brshtype)
	{
	case BRUSH_BRUSH_TYPE:
	  return_vals = gimp_run_procedure ("gimp_paintbrush", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_FLOAT,(gdouble)selvals.brushfade,
					    PARAM_INT32,(i/2)*2*GFIG_LCC,/* GIMP BUG should be 2!!!!*/
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_PENCIL_TYPE:
	  return_vals = gimp_run_procedure ("gimp_pencil", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_AIRBRUSH_TYPE:
	  return_vals = gimp_run_procedure ("gimp_airbrush", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_FLOAT,(gdouble)selvals.airbrushpressure,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_PATTERN_TYPE:
	  return_vals = gimp_run_procedure ("gimp_clone", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_INT32, 1,
					    PARAM_FLOAT,(gdouble)0.0,
					    PARAM_FLOAT,(gdouble)0.0,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	default:
	  break;
	}
    }
  else
    {
      /* We want to do a selection */
      return_vals = gimp_run_procedure ("gimp_free_select", &nreturn_vals,
					PARAM_IMAGE, gfig_image,
					PARAM_INT32,(i/2)*2,
					PARAM_FLOATARRAY, &line_pnts[0],  
					PARAM_INT32,selopt.type,
					PARAM_INT32,selopt.antia,
					PARAM_INT32,selopt.feather,
					PARAM_FLOAT,(gdouble)selopt.feather_radius,
					PARAM_END);

    }

  gimp_destroy_params (return_vals, nreturn_vals);

  g_free(line_pnts);
}

DOBJECT *
d_copy_spiral(DOBJECT * obj)
{
  DOBJECT *np;

#if DEBUG
  printf("Copy spiral\n");
#endif /*DEBUG*/
  if(!obj)
    return(NULL);

  g_assert(obj->type == SPIRAL);

  np = d_new_spiral(obj->points->pnt.x,obj->points->pnt.y);

  np->points->next = d_copy_dobjpoints(obj->points->next);

  np->type_data = obj->type_data;

#if DEBUG
  printf("Done spiral copy\n");
#endif /*DEBUG*/
  return(np);
}

DOBJECT *
d_new_spiral(gint x, gint y)
{
  DOBJECT *nobj;
  DOBJPOINTS *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = (DOBJPOINTS *)g_malloc0(sizeof(DOBJPOINTS));

#if DEBUG
  printf("New SPIRAL start at (%x,%x)\n",x,y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = (DOBJECT *)g_malloc0(sizeof(DOBJECT));

  nobj->type = SPIRAL;
  nobj->type_data = GINT_TO_POINTER(4); /* Default to four turns */
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_spiral;
  nobj->loadfunc  = d_load_spiral;
  nobj->savefunc  = d_save_spiral;
  nobj->paintfunc = d_paint_spiral;
  nobj->copyfunc  = d_copy_spiral;

  return(nobj);
}

void
d_update_spiral(GfigPoint *pnt)
{
  DOBJPOINTS *center_pnt, *edge_pnt;
  gint saved_cnt_pnt = selvals.opts.showcontrol;

  /* Undraw last one then draw new one */
  center_pnt = obj_creating->points;
  
  if(!center_pnt)
    return; /* No points */

  /* Leave the first pnt alone -
   * Edge point defines "radius"
   * Only undraw if already have edge point.
   */

  /* Hack - turn off cnt points in draw routine 
   * Looking back over the other update routines I could
   * use this trick again and cut down on code size!
   */


  if((edge_pnt = center_pnt->next))
    {
      /* Undraw */
      draw_circle(&edge_pnt->pnt);
      selvals.opts.showcontrol = 0;
      d_draw_spiral(obj_creating);

      edge_pnt->pnt.x = pnt->x;
      edge_pnt->pnt.y = pnt->y;
    }
  else
    {
      /* Radius is a few pixels away */
      /* First edge point */
      d_pnt_add_line(obj_creating,pnt->x,pnt->y,-1);
      edge_pnt = center_pnt->next;
    }

  /* draw it */
  selvals.opts.showcontrol = 0;
  d_draw_spiral(obj_creating);
  selvals.opts.showcontrol = saved_cnt_pnt;

  /* Realy draw the control points */
  draw_circle(&edge_pnt->pnt);
}

/* first point is center 
 * next defines the radius
 */

void
d_spiral_start(GfigPoint *pnt,gint shift_down)
{
  gint16 x,y;
  /* First is center point */
  obj_creating = d_new_spiral(x = pnt->x, y = pnt->y);
  obj_creating->type_data = GINT_TO_POINTER((spiral_num_turns*((spiral_toggle == 0)?1:-1)));
}

void
d_spiral_end(GfigPoint *pnt, gint shift_down)
{
  draw_circle(pnt);
  add_to_all_obj(current_obj,obj_creating);
  obj_creating = NULL;
}

/* Stuff for bezier curves... */

void
d_save_bezier(DOBJECT * obj, FILE *to)
{
  DOBJPOINTS * spnt;
  
  spnt = obj->points;

  if(!spnt)
    return; /* End-of-line */

  fprintf(to,"<BEZIER>\n");

  while(spnt)
    {
      fprintf(to,"%d %d\n",
	      (gint)spnt->pnt.x,
	      (gint)spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf(to,"<EXTRA>\n");
  fprintf(to,"%d\n</EXTRA>\n",GPOINTER_TO_INT(obj->type_data));
  fprintf(to,"</BEZIER>\n");

}

/* Load a bezier from the specified stream */

DOBJECT *
d_load_bezier(FILE *from)
{
  DOBJECT *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];

#ifdef DEBUG
  printf("Load bezier called\n");
#endif /* DEBUG */

  while(get_line(buf,MAX_LOAD_LINE,from,0))
    {
      if(sscanf(buf,"%d %d",&xpnt,&ypnt) != 2)
	{
	  /* Must be the end */
	  if(!strcmp("<EXTRA>",buf))
	    {
	      gint nsides = 3;
	      /* Number of sides - data item */
	      if(!new_obj)
		{
		  g_warning("[%d] Internal load error while loading bezier (extra area)",
			    line_no);
		  return(NULL);
		}
	      get_line(buf,MAX_LOAD_LINE,from,0);
	      if(sscanf(buf,"%d",&nsides) != 1)
		{
		  g_warning("[%d] Internal load error while loading bezier (extra area scanf)",
			    line_no);
		  return(NULL);
		}
	      new_obj->type_data = GINT_TO_POINTER(nsides);
	      get_line(buf,MAX_LOAD_LINE,from,0);
	      if(strcmp("</EXTRA>",buf))
		{
		  g_warning("[%d] Internal load error while loading bezier",
			    line_no);
		  return(NULL);
		} 
	      /* Go around and read the last line */
	      continue;
	    }
	  else if(strcmp("</BEZIER>",buf))
	    {
	      g_warning("[%d] Internal load error while loading bezier",
			line_no);
	      return(NULL);
	    }
	  return(new_obj);
	}
      
      if(!new_obj)
	new_obj = d_new_bezier(xpnt,ypnt);
      else
	d_pnt_add_line(new_obj,xpnt,ypnt,-1);
    }
  return(new_obj);
}


#define FP_PNT_MAX  10

static int fp_pnt_cnt = 0;
static int fp_pnt_chunk = 0;
static gdouble *fp_pnt_pnts = NULL;


static void
fp_pnt_start()
{
  fp_pnt_cnt = 0;
}

/* Add a line segment to collection array */
static void
fp_pnt_add(gdouble p1, gdouble p2, gdouble p3, gdouble p4)
{
  if(!fp_pnt_pnts)
    {
      fp_pnt_pnts = g_malloc0(FP_PNT_MAX*sizeof(gdouble));
      fp_pnt_chunk = 1;
    }

  if(((fp_pnt_cnt+4)/FP_PNT_MAX) >= fp_pnt_chunk)
    {
      /* more space pls */
      fp_pnt_chunk++;
      fp_pnt_pnts = (gdouble *)g_realloc(fp_pnt_pnts,sizeof(gdouble)*fp_pnt_chunk*FP_PNT_MAX);
    }

  fp_pnt_pnts[fp_pnt_cnt++] = p1;
  fp_pnt_pnts[fp_pnt_cnt++] = p2;
  fp_pnt_pnts[fp_pnt_cnt++] = p3;
  fp_pnt_pnts[fp_pnt_cnt++] = p4;
}

static gdouble *
d_bz_get_array(gint *sz)
{
  *sz = fp_pnt_cnt;
  return (fp_pnt_pnts);
}


static void
d_bz_line()
{
  gint i,x0,y0,x1,y1; 

  g_assert((fp_pnt_cnt%4) == 0);

  for(i = 0 ; i < fp_pnt_cnt; i+=4)
    {
      x0 = (gint)fp_pnt_pnts[i];
      y0 = (gint)fp_pnt_pnts[i+1];
      x1 = (gint)fp_pnt_pnts[i+2];
      y1 = (gint)fp_pnt_pnts[i+3];

      if(drawing_pic)
	{
	  gfig_draw_line (adjust_pic_coords((gint)x0,
					  preview_width),
			adjust_pic_coords((gint)y0,
					  preview_height),
			adjust_pic_coords((gint)x1,
					  preview_width),
			adjust_pic_coords((gint)y1,
					  preview_height));
	}
      else
	{
	  gfig_draw_line (gfig_scale_x((gint)x0),
			gfig_scale_y((gint)y0),
			gfig_scale_x((gint)x1),
			gfig_scale_y((gint)y1));
	}
    }
}

/*  Return points to plot */
/* Terminate by point with DBL_MAX,DBL_MAX */
typedef gdouble (*fp_pnt)[2];

void
DrawBezier (gdouble (*points)[2], gint np, gdouble mid, gint depth)
{
  gint i,j,x0=0,y0=0,x1,y1; 
  fp_pnt left;
  fp_pnt right;
  
    if (depth==0) /* draw polyline */
      {
	for (i=0; i<np; i++)
	  {
	    x1=(int) points[i][0];
	    y1=(int) points[i][1];
	    if(i > 0 && (x1 != x0 || y1 != y0))
	      {
		/* Add pnts up */
		fp_pnt_add((gdouble)x0,(gdouble)y0,(gdouble)x1,(gdouble)y1);
	      }
	    x0=x1;
	    y0=y1;
	  }
      }
    else /* subdivide control points at mid */
      {
	left = (fp_pnt)g_new(gdouble,np*2);
	right = (fp_pnt)g_new(gdouble,np*2);
	for (i=0; i<np; i++)
	  {
	    right[i][0]=points[i][0];
	    right[i][1]=points[i][1];
	  } 
	left[0][0]=right[0][0];
	left[0][1]=right[0][1];
	for (j=np-1; j>=1; j--)
	  {
	    for (i=0; i<j; i++)
	      {
		right[i][0]=(1-mid)*right[i][0]+mid*right[i+1][0];
		right[i][1]=(1-mid)*right[i][1]+mid*right[i+1][1];
	      }
	    left[np-j][0]=right[0][0];
	    left[np-j][1]=right[0][1];
	  }
	if (depth>0)
	  {
	    DrawBezier(left,np,mid,depth-1);
	    DrawBezier(right,np,mid,depth-1);
	    g_free(left);
	    g_free(right);
	  }
      }
}


static void
d_draw_bezier(DOBJECT *obj)
{
  DOBJPOINTS * spnt;
  gint seg_count = 0;
  gint i = 0;
  gdouble (*line_pnts)[2];

  spnt = obj->points;

  /* First count the number of points */

  /* count */
  while(spnt)
    {
      seg_count++;
      spnt = spnt->next;
    }

  spnt = obj->points;

  if(!spnt || !seg_count)
    return; /* no-line */

  /* The second *2 to get around bug in GIMP */
  line_pnts = (fp_pnt)g_malloc0(GFIG_LCC*(2*seg_count + 1)*sizeof(gdouble));

  /* Go around all the points drawing a line from one to the next */
  while(spnt)
    {
      draw_sqr(&spnt->pnt);
      line_pnts[i][0] = spnt->pnt.x;
      line_pnts[i++][1] = spnt->pnt.y;
      spnt = spnt->next;
    }

  /* Generate an array of doubles which are the control points */

  if(!drawing_pic && bezier_line_frame && tmp_bezier == obj)
    {
      fp_pnt_start();
      DrawBezier(line_pnts,seg_count,0.5,0);
      d_bz_line();
    }

  fp_pnt_start();
  DrawBezier(line_pnts,seg_count,0.5,3);
  d_bz_line();
  /*bezier4(line_pnts,seg_count,20);*/

  g_free(line_pnts);
}

static void
d_paint_bezier(DOBJECT *obj)
{
  gdouble *line_pnts;
  gdouble (*bz_line_pnts)[2];
  DOBJPOINTS * spnt;
  gint seg_count = 0;

  GParam *return_vals = NULL;
  gint nreturn_vals;
  gint i=0;

  spnt = obj->points;

  /* First count the number of points */

  /* count */
  while(spnt)
    {
      seg_count++;
      spnt = spnt->next;
    }

  spnt = obj->points;

  if(!spnt || !seg_count)
    return; /* no-line */

  /* The second *2 to get around bug in GIMP */
  bz_line_pnts = (fp_pnt)g_malloc0(GFIG_LCC*(2*seg_count + 1)*sizeof(gdouble));

  /* Go around all the points drawing a line from one to the next */
  while(spnt)
    {
      bz_line_pnts[i][0] = spnt->pnt.x;
      bz_line_pnts[i++][1] = spnt->pnt.y;
      spnt = spnt->next;
    }

  fp_pnt_start();
  DrawBezier(bz_line_pnts,seg_count,0.5,5);
  line_pnts = d_bz_get_array(&i);

  /* Reverse line if approp */
  if(selvals.reverselines)
    reverse_pairs_list(&line_pnts[0],i/2);

  /* Scale before drawing */
  if(selvals.scaletoimage)
    scale_to_original_xy(&line_pnts[0],i/2);
  else
    scale_to_xy(&line_pnts[0],i/2);

  if(selvals.painttype == PAINT_BRUSH_TYPE)
    {
      switch(selvals.brshtype)
	{
	case BRUSH_BRUSH_TYPE:
	  return_vals = gimp_run_procedure ("gimp_paintbrush", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_FLOAT,(gdouble)selvals.brushfade,
					    PARAM_INT32,(i/2)*2*GFIG_LCC,/* GIMP BUG should be 2!!!!*/
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_PENCIL_TYPE:
	  return_vals = gimp_run_procedure ("gimp_pencil", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_AIRBRUSH_TYPE:
	  return_vals = gimp_run_procedure ("gimp_airbrush", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_FLOAT,(gdouble)selvals.airbrushpressure,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	case BRUSH_PATTERN_TYPE:
	  return_vals = gimp_run_procedure ("gimp_clone", &nreturn_vals,
					    PARAM_IMAGE, gfig_image,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_DRAWABLE, gfig_drawable,
					    PARAM_INT32, 1,
					    PARAM_FLOAT,(gdouble)0.0,
					    PARAM_FLOAT,(gdouble)0.0,
					    PARAM_INT32,(i/2)*2,
					    PARAM_FLOATARRAY, &line_pnts[0],  
					    PARAM_END);
	  break;
	default:
	  break;
	}
    }
  else
    {
      /* We want to do a selection */
      return_vals = gimp_run_procedure ("gimp_free_select", &nreturn_vals,
					PARAM_IMAGE, gfig_image,
					PARAM_INT32,(i/2)*2,
					PARAM_FLOATARRAY, &line_pnts[0],  
					PARAM_INT32,selopt.type,
					PARAM_INT32,selopt.antia,
					PARAM_INT32,selopt.feather,
					PARAM_FLOAT,(gdouble)selopt.feather_radius,
					PARAM_END);

    }

  gimp_destroy_params (return_vals, nreturn_vals);

  g_free(bz_line_pnts);
  /* Don't free line_pnts - may need again */
}

DOBJECT *
d_copy_bezier(DOBJECT * obj)
{
  DOBJECT *np;

#if DEBUG
  printf("Copy bezier\n");
#endif /*DEBUG*/
  if(!obj)
    return(NULL);

  g_assert(obj->type == BEZIER);

  np = d_new_bezier(obj->points->pnt.x,obj->points->pnt.y);

  np->points->next = d_copy_dobjpoints(obj->points->next);

  np->type_data = obj->type_data;

#if DEBUG
  printf("Done bezier copy\n");
#endif /*DEBUG*/
  return(np);
}

DOBJECT *
d_new_bezier(gint x, gint y)
{
  DOBJECT *nobj;
  DOBJPOINTS *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = (DOBJPOINTS *)g_malloc0(sizeof(DOBJPOINTS));

#if DEBUG
  printf("New BEZIER start at (%x,%x)\n",x,y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = (DOBJECT *)g_malloc0(sizeof(DOBJECT));

  nobj->type = BEZIER;
  nobj->type_data = GINT_TO_POINTER(4); /* Default to four turns */
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_bezier;
  nobj->loadfunc  = d_load_bezier;
  nobj->savefunc  = d_save_bezier;
  nobj->paintfunc = d_paint_bezier;
  nobj->copyfunc  = d_copy_bezier;

  return(nobj);
}

void
d_update_bezier(GfigPoint *pnt)
{
  DOBJPOINTS *s_pnt, *l_pnt;
  gint saved_cnt_pnt = selvals.opts.showcontrol;

  g_assert(tmp_bezier != NULL);

  /* Undraw last one then draw new one */
  s_pnt = tmp_bezier->points;
  
  if(!s_pnt)
    return; /* No points */

  /* Hack - turn off cnt points in draw routine 
   */

  if((l_pnt = s_pnt->next))
    {
      /* Undraw */
      while(l_pnt->next)
	{
	  l_pnt = l_pnt->next;
	}

      draw_circle(&l_pnt->pnt);
      selvals.opts.showcontrol = 0;
      d_draw_bezier(tmp_bezier);
      l_pnt->pnt.x = pnt->x;
      l_pnt->pnt.y = pnt->y;
    }
  else
    {
      /* Radius is a few pixels away */
      /* First edge point */
      d_pnt_add_line(tmp_bezier,pnt->x,pnt->y,-1);
      l_pnt = s_pnt->next;
    }

  /* draw it */
  selvals.opts.showcontrol = 0;
  d_draw_bezier(tmp_bezier);
  selvals.opts.showcontrol = saved_cnt_pnt;

  /* Realy draw the control points */
  draw_circle(&l_pnt->pnt);
}

/* first point is center 
 * next defines the radius
 */

void
d_bezier_start(GfigPoint *pnt,gint shift_down)
{
  gint16 x,y;
  /* First is center point */
  if(!tmp_bezier)
    {
      /* New curve */
      tmp_bezier = obj_creating = d_new_bezier(x = pnt->x, y = pnt->y);
    }
}

void
d_bezier_end(GfigPoint *pnt, gint shift_down)
{
  DOBJPOINTS *l_pnt;

  if(!tmp_bezier)
    {
      tmp_bezier = obj_creating;
    }
  
  l_pnt = tmp_bezier->points->next;

  if(!l_pnt) 
    return;

  if(shift_down)
    {
      /* Undraw circle on last pnt */
      while(l_pnt->next)
	{
	  l_pnt = l_pnt->next;
	}

      if(l_pnt)
	{
	  draw_circle(&l_pnt->pnt);
	  draw_sqr(&l_pnt->pnt);

	  if(bezier_closed)
	    {
	      gint tmp_frame = bezier_line_frame;
	      /* if closed then add first point */
	      d_draw_bezier(tmp_bezier);
	      d_pnt_add_line(tmp_bezier,
			     tmp_bezier->points->pnt.x,
			     tmp_bezier->points->pnt.y,-1);
	      /* Final has no frame */
	      bezier_line_frame = 0; /* False */
	      d_draw_bezier(tmp_bezier);
	      bezier_line_frame = tmp_frame; /* What is was */
	    }
	  else if(bezier_line_frame)
	    {
	      d_draw_bezier(tmp_bezier);
	      bezier_line_frame = 0; /* False */
	      d_draw_bezier(tmp_bezier);
	      bezier_line_frame = 1; /* What is was */
	    }

	  add_to_all_obj(current_obj,obj_creating);
	}

      /* small mem leak if !l_pnt ? */
      tmp_bezier = NULL;
      obj_creating = NULL;
    }
  else
    {
      if(!tmp_bezier->points->next)
	{
	  draw_circle(&tmp_bezier->points->pnt);
	  draw_sqr(&tmp_bezier->points->pnt);
	}

      d_draw_bezier(tmp_bezier);
      d_pnt_add_line(tmp_bezier,pnt->x,pnt->y,-1);
      d_draw_bezier(tmp_bezier);
    }
}


/* copy objs */
DALLOBJS *
copy_all_objs(DALLOBJS *objs)
{
  DALLOBJS * nobj;
  DALLOBJS * new_all_objs = NULL;
  DALLOBJS * ret = NULL;

  while(objs)
    {
      nobj = g_malloc0(sizeof(DALLOBJS));

     if(!ret)
	{
	  ret = new_all_objs = nobj;
	}
      else
	{
	  new_all_objs->next = nobj;
	  new_all_objs = nobj;
	}

      nobj->obj = (DOBJECT *)objs->obj->copyfunc(objs->obj);

      objs = objs->next;
    }

  return(ret);
}

/* Screen refresh */
void
draw_one_obj(DOBJECT * obj)
{
  obj->drawfunc(obj);
}

void
draw_objects(DALLOBJS * objs,gint show_single)
{
  /* Show_single - only one object to draw Unless shift 
   * is down in whcih case show all.
   */

  gint count = 0;

  while(objs)
    {
      if(!show_single || count == obj_show_single || obj_show_single == -1)
	draw_one_obj(objs->obj);
      objs = objs->next;
      count++;
    }
}

/* Draws the object being created, as the update functions used to
 * leave it on the screen: the control point where it was started, the
 * shape so far and a circle on the point that follows the pointer.
 * Only called from the draw function of the preview.
 */
static void
draw_creating (void)
{
  DOBJPOINTS *first;
  DOBJPOINTS *last;
  DOBJPOINTS *p;
  gint saved_scale;
  gint saved_cnt;

  if (!obj_creating || !obj_creating->points)
    return;

  saved_scale = selvals.scaletoimage;
  saved_cnt = selvals.opts.showcontrol;

  /* Objects being created are in preview coordinates */
  selvals.scaletoimage = 1;

  first = obj_creating->points;
  for (last = first; last->next; last = last->next)
    ;

  switch (obj_creating->type)
    {
    case LINE: /* Also the lines of an arc being made */
      /* A continued line already shows its end point */
      if (!tmp_line)
	draw_sqr (&first->pnt);
      if (first->next)
	{
	  gfig_draw_line (first->pnt.x, first->pnt.y,
			  first->next->pnt.x, first->next->pnt.y);
	  draw_circle (&first->next->pnt);
	}
      break;

    case CIRCLE:
    case ELLIPSE:
    case POLY:
    case STAR:
    case SPIRAL:
      draw_sqr (&first->pnt);
      if (first->next)
	{
	  selvals.opts.showcontrol = 0;
	  obj_creating->drawfunc (obj_creating);
	  selvals.opts.showcontrol = saved_cnt;

	  draw_circle (&first->next->pnt);
	  if (obj_creating->type == STAR && first->next->next)
	    draw_circle (&first->next->next->pnt);
	}
      break;

    case BEZIER:
      for (p = first; p; p = p->next)
	if (p != last || p == first)
	  draw_sqr (&p->pnt);

      selvals.opts.showcontrol = 0;
      d_draw_bezier (obj_creating);
      selvals.opts.showcontrol = saved_cnt;

      if (last != first)
	draw_circle (&last->pnt);
      break;

    default:
      break;
    }

  selvals.scaletoimage = saved_scale;
}

static void
prepend_to_all_obj(GFIGOBJ *fobj,DALLOBJS *nobj)
{
  DALLOBJS *cobj;

  setup_undo(); /* Remember ME */

  if(!fobj->obj_list)
    {
      fobj->obj_list = nobj;
      return;
    }

  cobj = fobj->obj_list;

  while(cobj->next)
    {
      cobj = cobj->next;
    }

  cobj->next = nobj;
}

static void
add_to_all_obj(GFIGOBJ * fobj,DOBJECT *obj)
{
  DALLOBJS *nobj;
  
  nobj = g_malloc0(sizeof(DALLOBJS));

  nobj->obj = obj;

  if(need_to_scale)
    scale_obj_points(obj->points,scale_x_factor,scale_y_factor);

  prepend_to_all_obj(fobj,nobj);
}

void
object_operation_start(GfigPoint *pnt,gint shift_down)
{
  DOBJECT *new_obj;

  /* Find point in given object list */
  operation_obj = get_nearest_objs(current_obj,pnt);

  /* Special case if shift down && move obj then moving all objs */

  if(shift_down && selvals.otype == MOVE_OBJ)
    {
      move_all_pnt = g_malloc0(sizeof(*move_all_pnt));
      *move_all_pnt = *pnt; /* Structure copy */
      setup_undo();
      return;
    }

  if(!operation_obj)
    return;/* None to work on */


  setup_undo();

  switch(selvals.otype)
    {
    case MOVE_OBJ:
      if(operation_obj->type == BEZIER)
	{
	  d_draw_bezier(operation_obj);
	  tmp_bezier = operation_obj;
	  d_draw_bezier(operation_obj);
	}
      break;
    case MOVE_POINT:
      if(operation_obj->type == BEZIER)
	{
	  d_draw_bezier(operation_obj);
	  tmp_bezier = operation_obj;
	  d_draw_bezier(operation_obj);
	}
      /* If shift is down the break into sep lines */
      if((operation_obj->type == POLY  
	  || operation_obj->type == STAR)
	 && shift_down)
	{
	  switch(operation_obj->type)
	    {
	    case POLY:
	      d_poly2lines(operation_obj);
	      break;
	    case STAR:
	      d_star2lines(operation_obj);
	      break;
	    default:
	      break;
	    }
	  /* Re calc which object point we are lookin at */
	  scan_obj_points(operation_obj->points,pnt);
	}
      break;
    case COPY_OBJ:
      /* Copy the "operation object" */
      /* Then bung us into "copy/move" mode */
#ifdef DEBUG
      printf("In copy obj\n");
#endif /* DEBUG */
      new_obj = (DOBJECT *)operation_obj->copyfunc(operation_obj);
      if(new_obj)
	{
	  scan_obj_points(new_obj->points,pnt);
	  add_to_all_obj(current_obj,new_obj);
	  operation_obj = new_obj;
	  selvals.otype = MOVE_COPY_OBJ;
	  new_obj->drawfunc(new_obj);
	}
      break;
    case DEL_OBJ:
      remove_obj_from_list(current_obj,operation_obj);
      break;
    case MOVE_COPY_OBJ: /* Never when button down */
    default:
      g_warning("Internal error selvals.otype object operation start");
      break;
    }
}

void
object_operation_end(GfigPoint *pnt,gint shift_down)
{
  if(selvals.otype != DEL_OBJ && operation_obj && operation_obj->type == BEZIER)
    {
      d_draw_bezier(operation_obj);
      tmp_bezier = NULL; /* use as switch */
      d_draw_bezier(operation_obj);
    }

  operation_obj = NULL;

  if(move_all_pnt)
    {
      g_free(move_all_pnt);
      move_all_pnt = 0;
    }

  /* Special case - if copying mode MUST be copy when button up received */
  if(selvals.otype == MOVE_COPY_OBJ)
    selvals.otype = COPY_OBJ;


}

/* Move object around */
void
object_operation(GfigPoint *to_pnt,gint shift_down)
{
  /* Must do diffent things depending on object type */
  /* but must have object to operate on! */

  /* Special case - if shift own and move_obj then move ALL objects */
  if(move_all_pnt && shift_down && selvals.otype == MOVE_OBJ)
    {
      do_move_all_obj(to_pnt);
      return;
    }

  if(!operation_obj)
    return;

  switch(selvals.otype)
    {
    case MOVE_OBJ:
    case MOVE_COPY_OBJ:
      switch(operation_obj->type)
	{
	case LINE:
	case CIRCLE:
	case ELLIPSE:
	case POLY:
	case ARC:
	case STAR:
	case SPIRAL:
	case BEZIER:
	  do_move_obj(operation_obj,to_pnt);
	  break;
	default:
	  /* Internal error */
	  g_warning("Internal error in operation_obj->type");
	  break;
	}
      break;
    case MOVE_POINT:
      switch(operation_obj->type)
	{
	case LINE:
	case CIRCLE:
	case ELLIPSE:
	case POLY:
	case ARC:
	case STAR:
	case SPIRAL:
	case BEZIER:
	  do_move_obj_pnt(operation_obj,to_pnt);
	  break;
	default:
	  /* Internal error */
	  g_warning("Internal error in operation_obj->type");
	  break;
	}
      break;
    case DEL_OBJ:
      break;
    case COPY_OBJ: /* Should have been changed to MOVE_COPY_OBJ */
    default:
      g_warning("Internal error selvals.otype");
      break;
    }
}

/* First button press -- start drawing object */
void
object_start(GfigPoint *pnt,gint shift_down)
{
  /* start for the current object */
  if(!selvals.scaletoimage)
    {
      need_to_scale = 1;
      selvals.scaletoimage = 1;
    }
  else
    {
      need_to_scale = 0;
    }

  switch(selvals.otype)
    {
    case LINE:
      /* Shift means we are still drawing */
      if(!shift_down || !obj_creating)
	draw_sqr(pnt);
      d_line_start(pnt,shift_down);
      break;
    case CIRCLE:
      draw_sqr(pnt);
      d_circle_start(pnt,shift_down);
      break;
    case ELLIPSE:
      draw_sqr(pnt);
      d_ellipse_start(pnt,shift_down);
      break;
    case POLY:
      draw_sqr(pnt);
      d_poly_start(pnt,shift_down);
      break;
    case ARC:
      d_arc_start(pnt,shift_down);
      break;
    case STAR:
      draw_sqr(pnt);
      d_star_start(pnt,shift_down);
      break;
    case SPIRAL:
      draw_sqr(pnt);
      d_spiral_start(pnt,shift_down);
      break;
    case BEZIER:
      if(!tmp_bezier)
	draw_sqr(pnt);
      d_bezier_start(pnt,shift_down);
      break;
    default:
      /* Internal error */
      break;
    }
}
  
/* Real object now !*/
void
object_end(GfigPoint *pnt,gint shift_down)
{
  /* end for the current object */
  /* Add onto global object list */

  /* If shift is down may carry on drawing */
  switch(selvals.otype)
    {
    case LINE:
      d_line_end(pnt,shift_down);
      draw_sqr(pnt);
      break;
    case CIRCLE:
      draw_sqr(pnt);
      d_circle_end(pnt,shift_down);
      break;
    case ELLIPSE:
      draw_sqr(pnt);
      d_ellipse_end(pnt,shift_down);
      break;
    case POLY:
      draw_sqr(pnt);
      d_poly_end(pnt,shift_down);
      break;
    case STAR:
      draw_sqr(pnt);
      d_star_end(pnt,shift_down);
      break;
    case ARC:
      draw_sqr(pnt);
      d_arc_end(pnt,shift_down);
      break;
    case SPIRAL:
      draw_sqr(pnt);
      d_spiral_end(pnt,shift_down);
      break;
    case BEZIER:
      d_bezier_end(pnt,shift_down);
      break;
    default:
      /* Internal error */
      break;
    }

  if(need_to_scale)
    {
      need_to_scale = 0;
      selvals.scaletoimage = 0;
    }
}

void
object_update(GfigPoint * pnt)
{
  /* update for the current object */
  /* New position xy */
  switch(selvals.otype)
    {
    case LINE:
      d_update_line(pnt);
      break;
    case CIRCLE:
      d_update_circle(pnt);
      break;
    case ELLIPSE:
      d_update_ellipse(pnt);
      break;
    case POLY:
      d_update_poly(pnt);
      break;
    case STAR:
      d_update_star(pnt);
      break;
    case ARC:
      d_update_arc(pnt);
      break;
    case SPIRAL:
      d_update_spiral(pnt);
      break;
    case BEZIER:
      d_update_bezier(pnt);
      break;
    default:
      /* Internal error */
      break;
    }
}
