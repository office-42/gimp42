/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * Gradient editor module copyight (C) 1996-1997 Federico Mena Quintero
 * federico@nuclecu.unam.mx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURIGHTE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */


/* hof: Hofer Wolfgang, 1998.01.27   avoid resize bug by keeping 
 *                                   preview widgetsize constant.
 * hof: Hofer Wolfgang, 1997.10.17   Added "Load From FG color"
 */

/* Release date: 1997/06/02
 *
 * - Added the following procedural_db calls:
 *     gimp_gradients_get_list
 *     gimp_gradients_get_active
 *     gimp_gradients_set_active
 *     gimp_gradients_sample_uniform
 *     gimp_gradients_sample_custom
 *   Many thanks to Eiichi and Marcelo for their suggestions!
 */


/* Release date: 1997/05/07
 *
 * - Added accelerator keys for the popup functions.  This allows for
 * very fast operation of the editor.
 *
 * - Added Replicate Selection function.
 *
 * - Re-arranged the pop-up menu a bit; it was getting too big.  I am
 * still not entirely happy with it.
 *
 * - Added grad_dump_gradient(); it is useful for debugging.
 */


/* Release date: 1997/04/30
 *
 * - All `dangerous' dialogs now de-sensitize the main editor window.
 * This fixes a lot of potential bugs when the dialogs are active.
 *
 * - Fixed two bugs due to uninitialized variables, one in
 * prev_events() (thanks to Marcelo for pointing it out) and another
 * in cpopup_render_color_box() (me), and removed all warnings due to
 * those.
 *
 * - Removed the printf()'s in the pop-up menu (they were only used
 * for debugging).
 */


/* Release date: 1997/04/22
 *
 * - Added GtkRadioMenuItems to the blending and coloring pop-up
 * menus.  You no longer have to remember the dang type for each
 * segment.
 *
 * - Added midpoint capabilities to sinuosidal and spherical sigments.
 * Many thanks to Marcelo for the patches!
 *
 * - Added a *real* Cancel function to the color pickers.  I don't
 * know why nobody killed me for not having done it before.
 */


/* Release date: 1997/04/21
 *
 * - Re-wrote the old pop-up menu code, which was *horrible*.  The
 * memory leaks *should* go away once I write
 * grad_free_gradient_editor().  I'll do it once I'm finished adding
 * crap to gradient_editor_t.
 *
 * - Added "fetch from" color buttons.  Yeah, we all missed them.  You
 * should shed happiness tears when you see it.
 *
 * - Added an eyedropper function to the preview widget.  You can now
 * click on the preview widget and the foreground color will be set to
 * the gradient's color under the cursor.  This is still missing the
 * eyedropper cursor shape.
 *
 * - You can now invoke the pop-up menu from the preview widget.  Even
 * my hand gets unsteady at times.
 *
 * - Cool functions: Flip selection, Blend colors.  Can't live without
 * them.
 */


/* Special thanks to:
 *
 * Luis Albarran (luis4@mindspring.com) - Nice UI suggestions
 *
 * Miguel de Icaza (miguel@nuclecu.unam.mx) - Pop-up menu suggestion
 *
 * Marcelo Malheiros (malheiro@dca.fee.unicamp.br) - many, many
 * suggestions, nice gradient files
 *
 * Adam Moss (adam@uunet.pipex.com) - idea for the hint bar
 *
 * Everyone on #gimp - many suggestions
 */


/* TODO:
 *
 * - Fix memory leaks: grad_free_gradient_editor() and any others
 * which I may have missed.
 *
 * - Add all of Marcelo's neat suggestions:
 *   - Hue rotate, saturation, brightness, contrast.
 *
 * - Add Save Dirty Gradients function.
 *
 * - Add Reload function.
 *
 * - See that the default-gradient really gets selected on startup.
 *
 * - Provide a way of renaming a gradient (instead of having to do
 *   copy/delete).
 *
 * - Fix the flicker in the hint bar.
 *
 * - Maybe add a little preview of each gradient to the listbox?
 *
 * - Better handling of bogus gradient files and inconsistent
 *   segments.  Do not loop indefinitely in seg_get_segment_at() if
 *   there is a missing segment between two others.
 *
 * - Add a Gradient brush mode (color changes as you move it).
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>

#include "appenv.h"
#include "colormaps.h"
#include "datafiles.h"
#include "errors.h"
#include "general.h"
#include "gimprc.h"
#include "gradient.h"
#include "interface.h"
#include "palette.h"


/***** Magic numbers *****/

#ifndef M_PI
#define M_PI    3.14159265358979323846
#endif /* M_PI */

#define EPSILON 1e-10

#define GRAD_LIST_WIDTH  300
#define GRAD_LIST_HEIGHT 80

#define GRAD_SCROLLBAR_STEP_SIZE 0.05
#define GRAD_SCROLLBAR_PAGE_SIZE 0.75

#define GRAD_CLOSE_BUTTON_WIDTH 45
#define GRAD_PREVIEW_WIDTH      600
#define GRAD_PREVIEW_HEIGHT     64
#define GRAD_CONTROL_HEIGHT     10

#define GRAD_CHECK_SIZE 8

#define GRAD_CHECK_DARK  (1.0 / 3.0)
#define GRAD_CHECK_LIGHT (2.0 / 3.0)

#define GRAD_COLOR_BOX_WIDTH  24
#define GRAD_COLOR_BOX_HEIGHT 16

#define GRAD_NUM_COLORS 10

#define GRAD_MOVE_TIME 150 /* ms between mouse click and detection of movement in gradient control */

#define GRAD_UPDATE_PREVIEW   0x0001
#define GRAD_UPDATE_CONTROL   0x0002
#define GRAD_RESET_CONTROL    0X0004

#define GRAD_NUM_BLENDING_TYPES 5
#define GRAD_NUM_COLORING_TYPES 3


/***** Types *****/

/* Gradient segment type */

typedef enum {
	GRAD_LINEAR = 0,
	GRAD_CURVED,
	GRAD_SINE,
	GRAD_SPHERE_INCREASING,
	GRAD_SPHERE_DECREASING
} grad_type_t;

typedef enum {
	GRAD_RGB = 0,  /* normal RGB */
	GRAD_HSV_CCW,  /* counterclockwise hue */
	GRAD_HSV_CW    /* clockwise hue */
} grad_color_t;

typedef struct _grad_segment_t {
	double       left, middle, right; /* Left pos, midpoint, right pos */
	double       r0, g0, b0, a0;   	/* Left color */
	double       r1, g1, b1, a1;   	/* Right color */
	grad_type_t  type;             	/* Segment's blending function */
	grad_color_t color;             /* Segment's coloring type */

	struct _grad_segment_t *prev, *next; /* For linked list of segments */
} grad_segment_t;


/* Complete gradient type */

typedef struct _gradient_t {
	char           *name;
	grad_segment_t *segments;
	grad_segment_t *last_visited;
	int             dirty;
	char           *filename;
	GtkWidget      *list_item;      /* Its row in the editor's list box */
} gradient_t;


/* Gradient editor type */

typedef enum {
	GRAD_DRAG_NONE = 0,
	GRAD_DRAG_LEFT,
	GRAD_DRAG_MIDDLE,
	GRAD_DRAG_ALL
} control_drag_mode_t;

typedef struct {
	GtkWidget *shell;
	GtkWidget *hint_label;
	GtkWidget *list;
	GtkWidget *scrollbar;
	GtkWidget *preview;
	GtkWidget *control;

	/* Zoom and scrollbar */

	unsigned int   zoom_factor;
	GtkAdjustment *scroll_data;

	/* Instant update */

	int instant_update;

	/* Gradient preview */

	guchar   *preview_rows[2]; /* For caching redraw info */
	gint      preview_last_x;
	int       preview_button_down;

	/* Gradient control */

	grad_segment_t 	    *control_drag_segment;   /* Segment which is being dragged */
	grad_segment_t 	    *control_sel_l;          /* Left segment of selection */
	grad_segment_t 	    *control_sel_r;          /* Right segment of selection */
	control_drag_mode_t  control_drag_mode;      /* What is being dragged? */
	guint32              control_click_time;     /* Time when mouse was pressed */
	int                  control_compress;       /* Compressing/expanding handles */
	gint                 control_last_x;         /* Last mouse position when dragging */
	double               control_last_gx;        /* Last position (wrt gradient) when dragging */
	double               control_orig_pos;       /* Original click position when dragging */
	double               control_drag_start_x;   /* Where the drag gesture began */

	/* Pop-up menu: a GtkPopoverMenu built from a GMenu on every
	 * pop-up, driven by the actions in `actions' (inserted on the
	 * shell as "grad").
	 */

	GtkWidget           *control_main_popup;
	GSimpleActionGroup  *actions;

	/* Split uniformly dialog */

	int split_parts;

	/* Replicate dialog */

	int replicate_times;

	/* Saved colors */

	struct {
		double r, g, b, a;
	} saved_colors[GRAD_NUM_COLORS];

	/* Color dialogs */

	grad_segment_t *left_saved_segments;
	int             left_saved_dirty;

	grad_segment_t *right_saved_segments;
	int             right_saved_dirty;
} gradient_editor_t;


/***** Local functions *****/

/* Gradient editor functions */
static void       ed_fetch_foreground(double *fg_r, double *fg_g, double *fg_b, double *fg_a);
static void       ed_update_editor(int flags);

static GtkWidget *ed_create_button(gchar *label, double xalign, double yalign,
				   GCallback signal_func, gpointer user_data);

static void       ed_set_hint(char *str);

static void       ed_set_list_of_gradients(void);
static void       ed_insert_in_gradients_listbox(gradient_t *grad, int pos, int select);
static void       ed_list_item_update(GtkListBox *box, GtkListBoxRow *row, gpointer data);

static void       ed_initialize_saved_colors(void);

static gboolean   ed_close_callback(GtkWidget *widget, gpointer client_data);
static void       ed_refresh_callback(GtkWidget *widget, gpointer client_data);
static void       ed_new_gradient_callback(GtkWidget *widget, gpointer client_data);
static void       ed_do_new_gradient_callback(GtkWidget *widget, gpointer client_data, gpointer call_data);
static void       ed_copy_gradient_callback(GtkWidget *widget, gpointer client_data);
static void       ed_do_copy_gradient_callback(GtkWidget *widget, gpointer client_data, gpointer call_data);
static void       ed_delete_gradient_callback(GtkWidget *widget, gpointer client_data);
static void       ed_do_delete_gradient_callback(GtkWidget *widget, gpointer client_data);
static void       ed_cancel_delete_gradient_callback(GtkWidget *widget, gpointer client_data);
static gboolean   ed_delete_gradient_close_request(GtkWindow *window, gpointer client_data);
static void       ed_save_pov_callback(GtkWidget *widget, gpointer client_data);
static void       ed_do_save_pov_callback(const gchar *filename, gpointer client_data);
static void       ed_scrollbar_update(GtkAdjustment *adjustment, gpointer data);
static void       ed_zoom_all_callback(GtkWidget *widget, gpointer client_data);
static void       ed_zoom_out_callback(GtkWidget *widget, gpointer client_data);
static void       ed_zoom_in_callback(GtkWidget *widget, gpointer client_data);
static void       ed_instant_update_update(GtkWidget *widget, gpointer data);

/* Gradient preview functions */

static void prev_motion(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer data);
static void prev_leave(GtkEventControllerMotion *controller, gpointer data);
static void prev_drag_begin(GtkGestureDrag *gesture, gdouble x, gdouble y, gpointer data);
static void prev_drag_update(GtkGestureDrag *gesture, gdouble dx, gdouble dy, gpointer data);
static void prev_drag_end(GtkGestureDrag *gesture, gdouble dx, gdouble dy, gpointer data);
static void prev_popup(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data);
static void prev_set_hint(gint x);
static void prev_set_foreground(gint x);
static void prev_update(int recalculate);
static void prev_fill_image(int width, int height, double left, double right);

/* Gradient control functions */

static void   control_motion_event(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer data);
static void   control_leave(GtkEventControllerMotion *controller, gpointer data);
static void   control_drag_begin(GtkGestureDrag *gesture, gdouble x, gdouble y, gpointer data);
static void   control_drag_update(GtkGestureDrag *gesture, gdouble dx, gdouble dy, gpointer data);
static void   control_drag_end(GtkGestureDrag *gesture, gdouble dx, gdouble dy, gpointer data);
static void   control_popup(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data);
static void   control_pointer_moved(gint x, gint y, guint32 time);
static void   control_do_hint(gint x, gint y);
static void   control_button_press(gint x, gint y, guint button, guint state);
static int    control_point_in_handle(gint x, gint y, grad_segment_t *seg, control_drag_mode_t handle);
static void   control_select_single_segment(grad_segment_t *seg);
static void   control_extend_selection(grad_segment_t *seg, double pos);
static void   control_motion(gint x);
static void   control_compress_left(grad_segment_t *range_l, grad_segment_t *range_r,
				    grad_segment_t *drag_seg, double pos);
static void   control_compress_range(grad_segment_t *range_l, grad_segment_t *range_r,
				     double new_l, double new_r);

static double control_move(grad_segment_t *range_l, grad_segment_t *range_r, double delta);

static void   control_update(int recalculate);
static void   control_draw_func(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data);
static void   control_draw(cairo_t *cr, int width, int height, double left, double right);
static void   control_draw_normal_handle(cairo_t *cr, double pos, int height);
static void   control_draw_middle_handle(cairo_t *cr, double pos, int height);
static void   control_draw_handle(cairo_t *cr, double fill, int xpos, int height);
static int    control_calc_p_pos(double pos);
static double control_calc_g_pos(int pos);

/* Control popup functions */

static void       cpopup_create_main_menu(void);
static void       cpopup_do_popup(GtkWidget *parent, double x, double y);
static void       cpopup_adjust_menus(void);
static void       cpopup_check_selection_params(int *equal_blending, int *equal_coloring);
static GtkWidget *cpopup_create_color_box(double r, double g, double b, double a);

static void       cpopup_update_saved_color(int n, double r, double g, double b, double a);
static void       cpopup_load_left_callback(GtkWidget *widget, gpointer data);
static void       cpopup_save_left_callback(GtkWidget *widget, gpointer data);
static void       cpopup_load_right_callback(GtkWidget *widget, gpointer data);
static void       cpopup_save_right_callback(GtkWidget *widget, gpointer data);

static void       cpopup_blending_callback(GtkWidget *widget, gpointer data);
static void       cpopup_coloring_callback(GtkWidget *widget, gpointer data);

static void       cpopup_blend_colors(GtkWidget *widget, gpointer data);
static void       cpopup_blend_opacity(GtkWidget *widget, gpointer data);

static void       cpopup_get_color_dialog_color(GtkWidget *dialog,
						double *r, double *g, double *b, double *a);

static void       cpopup_create_color_dialog(char *title, double r, double g, double b, double a,
					     GCallback color_changed_callback,
					     GCallback ok_callback,
					     GCallback cancel_callback,
					     GCallback delete_callback);

static grad_segment_t *cpopup_save_selection(void);
static void            cpopup_free_selection(grad_segment_t *seg);
static void            cpopup_replace_selection(grad_segment_t *replace_seg);

static void       cpopup_set_left_color_callback(GtkWidget *widget, gpointer data);
static void       cpopup_left_color_changed(GtkWidget *widget, gpointer client_data);
static void       cpopup_left_color_dialog_ok(GtkWidget *widget, gpointer client_data);
static void       cpopup_left_color_dialog_cancel(GtkWidget *widget, gpointer client_data);
static gboolean   cpopup_left_color_dialog_delete(GtkWindow *window, gpointer data);
static void       cpopup_set_right_color_callback(GtkWidget *widget, gpointer data);
static void       cpopup_right_color_changed(GtkWidget *widget, gpointer client_data);
static void       cpopup_right_color_dialog_ok(GtkWidget *widget, gpointer client_data);
static void       cpopup_right_color_dialog_cancel(GtkWidget *widget, gpointer client_data);
static gboolean   cpopup_right_color_dialog_delete(GtkWindow *window, gpointer data);

static void       cpopup_split_midpoint_callback(GtkWidget *widget, gpointer data);
static void       cpopup_split_uniform_callback(GtkWidget *widget, gpointer data);
static void       cpopup_delete_callback(GtkWidget *widget, gpointer data);
static void       cpopup_recenter_callback(GtkWidget *widget, gpointer data);
static void       cpopup_redistribute_callback(GtkWidget *widget, gpointer data);
static void       cpopup_flip_callback(GtkWidget *widget, gpointer data);
static void       cpopup_replicate_callback(GtkWidget *widget, gpointer data);

static void       cpopup_split_uniform_scale_update(GtkAdjustment *adjustment, gpointer data);
static void       cpopup_split_uniform_split_callback(GtkWidget *widget, gpointer client_data);
static void       cpopup_split_uniform_cancel_callback(GtkWidget *widget, gpointer client_data);

static void       cpopup_replicate_scale_update(GtkAdjustment *adjustment, gpointer data);
static void       cpopup_do_replicate_callback(GtkWidget *widget, gpointer client_data);
static void       cpopup_replicate_cancel_callback(GtkWidget *widget, gpointer client_data);

static gboolean   cpopup_dialog_close_request(GtkWindow *window, gpointer data);

static void       cpopup_blend_endpoints(double r0, double g0, double b0, double a0,
					 double r1, double g1, double b1, double a1,
					 int blend_colors, int blend_opacity);
static void 	  cpopup_split_midpoint(grad_segment_t *lseg, grad_segment_t **newl, grad_segment_t **newr);
static void 	  cpopup_split_uniform(grad_segment_t *lseg, int parts,
				       grad_segment_t **newl, grad_segment_t **newr);

/* Gradient functions */

static gradient_t *grad_new_gradient(void);
static void        grad_free_gradient(gradient_t *grad);
static void        grad_free_gradients(void);
static void        grad_load_gradient(char *filename);
static void        grad_save_gradient(gradient_t *grad, char *filename);

static gradient_t *grad_create_default_gradient(void);

static int         grad_insert_in_gradients_list(gradient_t *grad);

static void        grad_dump_gradient(gradient_t *grad, FILE *file);

/* Segment functions */

static grad_segment_t *seg_new_segment(void);
static void            seg_free_segment(grad_segment_t *seg);
static void            seg_free_segments(grad_segment_t *seg);

static grad_segment_t *seg_get_segment_at(gradient_t *grad, double pos);
static grad_segment_t *seg_get_last_segment(grad_segment_t *seg);
static void            seg_get_closest_handle(gradient_t *grad, double pos,
					      grad_segment_t **seg, control_drag_mode_t *handle);

/* Calculation functions */

static double calc_linear_factor(double middle, double pos);
static double calc_curved_factor(double middle, double pos);
static double calc_sine_factor(double middle, double pos);
static double calc_sphere_increasing_factor(double middle, double pos);
static double calc_sphere_decreasing_factor(double middle, double pos);

static void calc_rgb_to_hsv(double *r, double *g, double *b);
static void calc_hsv_to_rgb(double *h, double *s, double *v);

/* Files and paths functions */

static char *build_user_filename(char *name, char *path_str);

/* Procedural database functions */

static Argument *gradients_get_list_invoker(Argument *args);
static Argument *gradients_get_active_invoker(Argument *args);
static Argument *gradients_set_active_invoker(Argument *args);
static Argument *gradients_sample_uniform_invoker(Argument *args);
static Argument *gradients_sample_custom_invoker(Argument *args);


/***** Local variables *****/

static int         num_gradients         = 0;
static GSList     *gradients_list        = NULL; /* The list of gradients */
static gradient_t *curr_gradient         = NULL; /* The active gradient */
static gradient_t *grad_default_gradient = NULL;

static gradient_editor_t *g_editor       = NULL; /* The gradient editor */

static char *blending_types[] = {
	"Linear",
	"Curved",
	"Sinuosidal",
	"Spherical (increasing)",
	"Spherical (decreasing)"
}; /* blending_types */

static char *coloring_types[] = {
	"Plain RGB",
	"HSV (counter-clockwise hue)",
	"HSV (clockwise hue)"
}; /* coloring_types */


/***** Public functions *****/

/*****/

void
gradients_init(int no_data)
{
  if(!no_data)
    datafiles_read_directories(gradient_path, grad_load_gradient, 0);


  if (grad_default_gradient != NULL)
    curr_gradient = grad_default_gradient;
  else if (gradients_list != NULL)
    curr_gradient = (gradient_t *) gradients_list->data;
  else {
		curr_gradient = grad_create_default_gradient();
		curr_gradient->name     = g_strdup("Default");
		curr_gradient->filename = build_user_filename(curr_gradient->name, gradient_path);
		curr_gradient->dirty    = FALSE;

		grad_insert_in_gradients_list(curr_gradient);
	} /* else */
} /* gradients_init */


/*****/

void
gradients_free(void)
{
	grad_free_gradients();
} /* gradients_free */


/*****/

void
grad_get_color_at(double pos, double *r, double *g, double *b, double *a)
{
	double          factor;
	grad_segment_t *seg;
	double          seg_len, middle;
	double          h0, s0, v0;
	double          h1, s1, v1;

	/* if there is no gradient return a totally transparent black */
	if (curr_gradient == NULL)
	  {
	    *r = 0; *g = 0; *b = 0; *a = 0;
	    return;
	  }

	if (pos < 0.0)
		pos = 0.0;
	else if (pos > 1.0)
		pos = 1.0;

	seg = seg_get_segment_at(curr_gradient, pos);

	seg_len = seg->right - seg->left;

	if (seg_len < EPSILON) {
		middle = 0.5;
		pos    = 0.5;
	} else {
		middle = (seg->middle - seg->left) / seg_len;
		pos    = (pos - seg->left) / seg_len;
	} /* else */

	switch (seg->type) {
		case GRAD_LINEAR:
			factor = calc_linear_factor(middle, pos);
			break;

		case GRAD_CURVED:
			factor = calc_curved_factor(middle, pos);
			break;

		case GRAD_SINE:
			factor = calc_sine_factor(middle, pos);
			break;

		case GRAD_SPHERE_INCREASING:
			factor = calc_sphere_increasing_factor(middle, pos);
			break;

		case GRAD_SPHERE_DECREASING:
			factor = calc_sphere_decreasing_factor(middle, pos);
			break;

		default:
			grad_dump_gradient(curr_gradient, stderr);
			fatal_error("grad_get_color_at(): aieee, unknown gradient type %d", (int) seg->type);
			factor = 0.0; /* Shut up -Wall */
			break;
	} /* switch */

	/* Calculate color components */

	*a = seg->a0 + (seg->a1 - seg->a0) * factor;

	if (seg->color == GRAD_RGB) {
		*r = seg->r0 + (seg->r1 - seg->r0) * factor;
		*g = seg->g0 + (seg->g1 - seg->g0) * factor;
		*b = seg->b0 + (seg->b1 - seg->b0) * factor;
	} else {
		h0 = seg->r0;
		s0 = seg->g0;
		v0 = seg->b0;

		h1 = seg->r1;
		s1 = seg->g1;
		v1 = seg->b1;

		calc_rgb_to_hsv(&h0, &s0, &v0);
		calc_rgb_to_hsv(&h1, &s1, &v1);

		s0 = s0 + (s1 - s0) * factor;
		v0 = v0 + (v1 - v0) * factor;

		switch (seg->color) {
			case GRAD_HSV_CCW:
				if (h0 < h1)
					h0 = h0 + (h1 - h0) * factor;
				else {
					h0 = h0 + (1.0 - (h0 - h1)) * factor;
					if (h0 > 1.0)
						h0 -= 1.0;
				} /* else */

				break;

			case GRAD_HSV_CW:
				if (h1 < h0)
					h0 = h0 - (h0 - h1) * factor;
				else {
					h0 = h0 - (1.0 - (h1 - h0)) * factor;
					if (h0 < 0.0)
						h0 += 1.0;
				} /* else */

				break;

			default:
				grad_dump_gradient(curr_gradient, stderr);
				fatal_error("grad_get_color_at(): aieee, unknown coloring mode %d",
					    (int) seg->color);
				break;
		} /* switch */

		*r = h0;
		*g = s0;
		*b = v0;

		calc_hsv_to_rgb(r, g, b);
	} /* else */
} /* grad_get_color_at */


/*****/

void
grad_create_gradient_editor(void)
{
	GtkWidget  *topvbox;
	GtkWidget  *vbox;
	GtkWidget  *table;
	GtkWidget  *label;
	GtkWidget  *hbox;
	GtkWidget  *listbox;
	GtkWidget  *gvbox;
	GtkWidget  *button;
	GtkWidget  *frame;
	GtkWidget  *separator;
	GtkEventController *motion;
	GtkGesture *drag;
	GtkGesture *click;

	/* If the editor already exists, just show it */

	if (g_editor) {
		gtk_window_present(GTK_WINDOW(g_editor->shell));

		return;
	} /* if */

	/* Create editor */

	if(no_data)
	  gradients_init(FALSE);
	g_editor = g_new0(gradient_editor_t, 1);

	/* Shell and main vbox */

	g_editor->shell = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(g_editor->shell), "Gradient Editor");

	/* handle window manager close signals */
	g_signal_connect (g_editor->shell, "close-request",
			  G_CALLBACK (ed_close_callback),
			  NULL);

	topvbox = gimp_vbox_new(FALSE, 0);
	gtk_window_set_child(GTK_WINDOW(g_editor->shell), topvbox);

	/* Hint bar and close button */

	table = gimp_table_new(2, 2, FALSE);
	gimp_box_pack_end(topvbox, table, FALSE, FALSE, 0);

	separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gimp_table_attach(table, separator, 0, 1, 0, 1, GIMP_EXPAND | GIMP_FILL, 0, 0, 0);

	g_editor->hint_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(g_editor->hint_label), 0.0);
	gtk_label_set_yalign(GTK_LABEL(g_editor->hint_label), 0.5);
	gtk_label_set_ellipsize(GTK_LABEL(g_editor->hint_label), PANGO_ELLIPSIZE_END);
	gimp_table_attach(table, g_editor->hint_label, 0, 1, 1, 2,
			  GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 8, 2);

	button = ed_create_button("Close", 0.5, 0.5, G_CALLBACK(ed_close_callback), NULL);
	gtk_widget_set_size_request(button, GRAD_CLOSE_BUTTON_WIDTH, -1);
	gimp_table_attach(table, button, 1, 2, 0, 2, GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 8, 0);

	/* Vbox for everything else */

	vbox = gimp_vbox_new(FALSE, 0);
	gimp_container_set_border_width(vbox, 8);
	gimp_box_pack_start(topvbox, vbox, TRUE, TRUE, 0);

	/* Gradients list box */

	label = gtk_label_new("Gradients");
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label), 0.5);
	gimp_box_pack_start(vbox, label, FALSE, FALSE, 0);

	hbox = gimp_hbox_new(FALSE, 8);
	gimp_box_pack_start(vbox, hbox, TRUE, TRUE, 0);

	listbox = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(listbox),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_ALWAYS);
	gtk_widget_set_size_request(listbox, GRAD_LIST_WIDTH, GRAD_LIST_HEIGHT);
	gimp_box_pack_start(hbox, listbox, TRUE, TRUE, 0);

	g_editor->list = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_editor->list), GTK_SELECTION_BROWSE);
	g_signal_connect(g_editor->list, "row-selected",
			 G_CALLBACK(ed_list_item_update), NULL);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(listbox), g_editor->list);

	/* Buttons for gradient functions */

	gvbox = gimp_vbox_new(FALSE, 0);
	gimp_box_pack_end(hbox, gvbox, FALSE, FALSE, 0);

	/* Buttons for gradient functions */

	button = ed_create_button("New gradient", 0.0, 0.5,
				  G_CALLBACK(ed_new_gradient_callback), NULL);
	gimp_box_pack_start(gvbox, button, TRUE, TRUE, 0);

	button = ed_create_button("Copy gradient", 0.0, 0.5,
				  G_CALLBACK(ed_copy_gradient_callback), NULL);
	gimp_box_pack_start(gvbox, button, TRUE, TRUE, 0);

	button = ed_create_button("Delete gradient", 0.0, 0.5,
				  G_CALLBACK(ed_delete_gradient_callback), NULL);
	gimp_box_pack_start(gvbox, button, TRUE, TRUE, 0);

	button = ed_create_button("Save as POV-Ray", 0.0, 0.5,
				  G_CALLBACK(ed_save_pov_callback), NULL);
	gimp_box_pack_start(gvbox, button, TRUE, TRUE, 0);

	button = ed_create_button("Refresh gradients", 0.0, 0.5,
				  G_CALLBACK(ed_refresh_callback), NULL);
	gimp_box_pack_start(gvbox, button, TRUE, TRUE, 0);

	/* Horizontal box for zoom controls, scrollbar, and instant update toggle */

	hbox = gimp_hbox_new(FALSE, 0);
	gimp_box_pack_start(vbox, hbox, FALSE, FALSE, 4);

	/* Zoom buttons */

	button = ed_create_button("Zoom all", 0.5, 0.5, G_CALLBACK(ed_zoom_all_callback), g_editor);
	gimp_box_pack_start(hbox, button, FALSE, FALSE, 0);

	button = ed_create_button("Zoom -", 0.5, 0.5, G_CALLBACK(ed_zoom_out_callback), g_editor);
	gimp_box_pack_start(hbox, button, FALSE, FALSE, 0);

	button = ed_create_button("Zoom +", 0.5, 0.5, G_CALLBACK(ed_zoom_in_callback), g_editor);
	gimp_box_pack_start(hbox, button, FALSE, FALSE, 0);

	/* Scrollbar */

	g_editor->zoom_factor = 1;

	g_editor->scroll_data = gtk_adjustment_new(0.0, 0.0, 1.0,
						   1.0 * GRAD_SCROLLBAR_STEP_SIZE,
						   1.0 * GRAD_SCROLLBAR_PAGE_SIZE,
						   1.0);
	g_object_ref_sink(g_editor->scroll_data);

	g_signal_connect(g_editor->scroll_data, "value-changed",
			 G_CALLBACK(ed_scrollbar_update),
			 g_editor);
	g_signal_connect(g_editor->scroll_data, "changed",
			 G_CALLBACK(ed_scrollbar_update),
			 g_editor);

	g_editor->scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL, g_editor->scroll_data);
	gtk_widget_set_valign(g_editor->scrollbar, GTK_ALIGN_CENTER);
	gimp_box_pack_start(hbox, g_editor->scrollbar, TRUE, TRUE, 4);

	/* Instant update toggle */

	g_editor->instant_update = 1;

	button = gtk_check_button_new_with_label("Instant update");
	gimp_box_pack_end(hbox, button, FALSE, FALSE, 0);
	g_signal_connect(button, "toggled",
			 G_CALLBACK(ed_instant_update_update),
			 g_editor);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(button), TRUE);

	/* hbox for that holds the frame for gradient preview and gradient control;
           this is only here, because resizing the preview doesn't work (and is disabled)
           to keep the preview and controls together */

	hbox = gimp_hbox_new(FALSE, 0);
	gimp_box_pack_start(vbox, hbox, FALSE, FALSE, 0);

	/* Frame for gradient preview and gradient control */

	frame = gtk_frame_new(NULL);
	gimp_box_pack_start(hbox, frame, FALSE, FALSE, 0);

	gvbox = gimp_vbox_new(FALSE, 0);
	gtk_frame_set_child(GTK_FRAME(frame), gvbox);

	/* Gradient preview */

	g_editor->preview_rows[0]     = NULL;
	g_editor->preview_rows[1]     = NULL;
	g_editor->preview_last_x      = 0;
	g_editor->preview_button_down = 0;

	g_editor->preview = gimp_preview_new(GIMP_PREVIEW_COLOR);
	gimp_preview_size(GIMP_PREVIEW(g_editor->preview),
			  GRAD_PREVIEW_WIDTH, GRAD_PREVIEW_HEIGHT);
	gtk_widget_set_halign(g_editor->preview, GTK_ALIGN_START);
	gtk_widget_set_valign(g_editor->preview, GTK_ALIGN_START);

	motion = gtk_event_controller_motion_new();
	g_signal_connect(motion, "motion", G_CALLBACK(prev_motion), g_editor);
	g_signal_connect(motion, "leave", G_CALLBACK(prev_leave), g_editor);
	gtk_widget_add_controller(g_editor->preview, motion);

	drag = gtk_gesture_drag_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
	g_signal_connect(drag, "drag-begin", G_CALLBACK(prev_drag_begin), g_editor);
	g_signal_connect(drag, "drag-update", G_CALLBACK(prev_drag_update), g_editor);
	g_signal_connect(drag, "drag-end", G_CALLBACK(prev_drag_end), g_editor);
	gtk_widget_add_controller(g_editor->preview, GTK_EVENT_CONTROLLER(drag));

	click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
	g_signal_connect(click, "pressed", G_CALLBACK(prev_popup), g_editor);
	gtk_widget_add_controller(g_editor->preview, GTK_EVENT_CONTROLLER(click));

	gimp_box_pack_start(gvbox, g_editor->preview, TRUE, TRUE, 0);

	/* Gradient control */

	g_editor->control_drag_segment            = NULL;
	g_editor->control_sel_l                   = NULL;
	g_editor->control_sel_r                   = NULL;
	g_editor->control_drag_mode               = GRAD_DRAG_NONE;
	g_editor->control_click_time              = 0;
	g_editor->control_compress                = 0;
	g_editor->control_last_x                  = 0;
	g_editor->control_last_gx                 = 0.0;
	g_editor->control_orig_pos                = 0.0;
	g_editor->control_main_popup              = NULL;

	g_editor->control = gtk_drawing_area_new();
	gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(g_editor->control),
					   GRAD_PREVIEW_WIDTH);
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(g_editor->control),
					    GRAD_CONTROL_HEIGHT);
	gtk_widget_set_halign(g_editor->control, GTK_ALIGN_START);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g_editor->control),
				       control_draw_func, g_editor, NULL);

	motion = gtk_event_controller_motion_new();
	g_signal_connect(motion, "motion", G_CALLBACK(control_motion_event), g_editor);
	g_signal_connect(motion, "leave", G_CALLBACK(control_leave), g_editor);
	gtk_widget_add_controller(g_editor->control, motion);

	drag = gtk_gesture_drag_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
	g_signal_connect(drag, "drag-begin", G_CALLBACK(control_drag_begin), g_editor);
	g_signal_connect(drag, "drag-update", G_CALLBACK(control_drag_update), g_editor);
	g_signal_connect(drag, "drag-end", G_CALLBACK(control_drag_end), g_editor);
	gtk_widget_add_controller(g_editor->control, GTK_EVENT_CONTROLLER(drag));

	click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
	g_signal_connect(click, "pressed", G_CALLBACK(control_popup), g_editor);
	gtk_widget_add_controller(g_editor->control, GTK_EVENT_CONTROLLER(click));

	gimp_box_pack_start(gvbox, g_editor->control, FALSE, TRUE, 0);

	/* Initialize other data */

	g_editor->left_saved_segments           = NULL;
	g_editor->left_saved_dirty              = 0;

	g_editor->right_saved_segments          = NULL;
	g_editor->right_saved_dirty             = 0;

	ed_initialize_saved_colors();
	cpopup_create_main_menu();

	/* Show everything */

	if (curr_gradient)
		control_select_single_segment(curr_gradient->segments);

	ed_set_list_of_gradients();

	gtk_window_present(GTK_WINDOW(g_editor->shell));
} /* grad_create_gradient_editor */


/*****/

void
grad_free_gradient_editor(void)
{
	/* FIXME */
} /* grad_free_gradient_editor */


/***** Gradient editor functions *****/

/*****/

static void
ed_fetch_foreground(double *fg_r, double *fg_g, double *fg_b, double *fg_a)
{
	unsigned char r, g, b;

 	palette_get_foreground (&r, &g, &b);

 	*fg_r = (double) r / 255.0;
 	*fg_g = (double) g / 255.0;
 	*fg_b = (double) b / 255.0;
	*fg_a = 1.0;                 /* opacity 100 % */
} /* ed_fetch_foreground */


/*****/

static void
ed_update_editor(int flags)
{
	if (flags & GRAD_UPDATE_PREVIEW)
		prev_update(1);

	if (flags & GRAD_UPDATE_CONTROL)
		control_update(0);

	if (flags & GRAD_RESET_CONTROL)
		control_update(1);
} /* ed_update_editor */


/*****/

static GtkWidget *
ed_create_button(gchar *label, double xalign, double yalign, GCallback signal_func, gpointer user_data)
{
	GtkWidget *button;
	GtkWidget *text;

	button = gtk_button_new();
	text   = gtk_label_new(label);

	gtk_label_set_xalign(GTK_LABEL(text), xalign);
	gtk_label_set_yalign(GTK_LABEL(text), yalign);
	gtk_button_set_child(GTK_BUTTON(button), text);

	if (signal_func != NULL)
		g_signal_connect(button, "clicked", signal_func, user_data);

	return button;
} /* ed_create_button */


/*****/

static void
ed_set_hint(char *str)
{
	gtk_label_set_text(GTK_LABEL(g_editor->hint_label), str);
} /* ed_set_hint */


/*****/

static void
ed_set_list_of_gradients(void)
{
	GSList     *list;
	gradient_t *grad;
	int         n;

	list = gradients_list;
	n    = 0;

	while (list) {
		grad = list->data;

		if (grad == curr_gradient)
			ed_insert_in_gradients_listbox(grad, n, 1);
		else
			ed_insert_in_gradients_listbox(grad, n, 0);

		list = g_slist_next(list);
		n++;
	} /* while */
} /* ed_set_list_of_gradients */


/*****/

static void
ed_insert_in_gradients_listbox(gradient_t *grad, int pos, int select)
{
	GtkWidget *row;
	GtkWidget *label;

	row   = gtk_list_box_row_new();
	label = gtk_label_new(grad->name);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
	g_object_set_data(G_OBJECT(row), "gradient", grad);

	grad->list_item = row;

	gtk_list_box_insert(GTK_LIST_BOX(g_editor->list), row, pos);

	if (select)
		gtk_list_box_select_row(GTK_LIST_BOX(g_editor->list), GTK_LIST_BOX_ROW(row));
} /* ed_insert_in_gradients_listbox */


/*****/

static void
ed_list_item_update(GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
	gradient_t *grad;

	/* If nothing is selected, do nothing */

	if (row == NULL)
		return;

	grad = g_object_get_data(G_OBJECT(row), "gradient");
	if (grad == NULL)
		return;

	/* Update current gradient */

	curr_gradient = grad;

	ed_update_editor(GRAD_UPDATE_PREVIEW | GRAD_RESET_CONTROL);
} /* ed_list_item_update */


/*****/

static void
ed_initialize_saved_colors(void)
{
	g_editor->saved_colors[0].r = 0.0; /* Black */
	g_editor->saved_colors[0].g = 0.0;
	g_editor->saved_colors[0].b = 0.0;
	g_editor->saved_colors[0].a = 1.0;

	g_editor->saved_colors[1].r = 0.5; /* 50% Gray */
	g_editor->saved_colors[1].g = 0.5;
	g_editor->saved_colors[1].b = 0.5;
	g_editor->saved_colors[1].a = 1.0;

	g_editor->saved_colors[2].r = 1.0; /* White */
	g_editor->saved_colors[2].g = 1.0;
	g_editor->saved_colors[2].b = 1.0;
	g_editor->saved_colors[2].a = 1.0;

	g_editor->saved_colors[3].r = 0.0; /* Clear */
	g_editor->saved_colors[3].g = 0.0;
	g_editor->saved_colors[3].b = 0.0;
	g_editor->saved_colors[3].a = 0.0;

	g_editor->saved_colors[4].r = 1.0; /* Red */
	g_editor->saved_colors[4].g = 0.0;
	g_editor->saved_colors[4].b = 0.0;
	g_editor->saved_colors[4].a = 1.0;

	g_editor->saved_colors[5].r = 1.0; /* Yellow */
	g_editor->saved_colors[5].g = 1.0;
	g_editor->saved_colors[5].b = 0.0;
	g_editor->saved_colors[5].a = 1.0;

	g_editor->saved_colors[6].r = 0.0; /* Green */
	g_editor->saved_colors[6].g = 1.0;
	g_editor->saved_colors[6].b = 0.0;
	g_editor->saved_colors[6].a = 1.0;

	g_editor->saved_colors[7].r = 0.0; /* Cyan */
	g_editor->saved_colors[7].g = 1.0;
	g_editor->saved_colors[7].b = 1.0;
	g_editor->saved_colors[7].a = 1.0;

	g_editor->saved_colors[8].r = 0.0; /* Blue */
	g_editor->saved_colors[8].g = 0.0;
	g_editor->saved_colors[8].b = 1.0;
	g_editor->saved_colors[8].a = 1.0;

	g_editor->saved_colors[9].r = 1.0; /* Magenta */
	g_editor->saved_colors[9].g = 0.0;
	g_editor->saved_colors[9].b = 1.0;
	g_editor->saved_colors[9].a = 1.0;
} /* ed_initialize_saved_colors */


/*****/

static gboolean
ed_close_callback(GtkWidget *widget, gpointer client_data)
{
	if (gtk_widget_get_visible(g_editor->shell))
		gtk_widget_set_visible(g_editor->shell, FALSE);

	return TRUE;
} /* ed_close_callback */


/*****/

static void
ed_new_gradient_callback(GtkWidget *widget, gpointer client_data)
{
	query_string_box("New gradient",
			 "Enter a name for the new gradient",
			 "untitled",
			 ed_do_new_gradient_callback, NULL);
} /* ed_new_gradient_callback */


/*****/

static void
ed_do_new_gradient_callback(GtkWidget *widget, gpointer client_data, gpointer call_data)
{
	gradient_t *grad;
	char       *gradient_name;
	int         pos;

	gradient_name = (char *) call_data;

	if (!gradient_name) {
		g_message ("ed_do_new_gradient_callback(): oops, received NULL in call_data");
		return;
	} /* if */

	grad = grad_create_default_gradient();

	grad->name     = gradient_name; /* We don't need to copy since this memory is ours */
	grad->dirty    = 1;
	grad->filename = build_user_filename(grad->name, gradient_path);

	/* Put new gradient in list */

	pos = grad_insert_in_gradients_list(grad);
	curr_gradient = grad;
	ed_insert_in_gradients_listbox(grad, pos, 1);

	ed_update_editor(GRAD_UPDATE_PREVIEW | GRAD_RESET_CONTROL);
} /* ed_do_new_gradient_callback */


/*****/

static void
ed_copy_gradient_callback(GtkWidget *widget, gpointer client_data)
{
	char *name;

	if (curr_gradient == NULL)
               return;

	name = g_strdup_printf("%s copy", curr_gradient->name);

	query_string_box("Copy gradient",
			 "Enter a name for the copied gradient",
			 name,
			 ed_do_copy_gradient_callback, NULL);

	g_free(name);
} /* ed_copy_gradient_callback */


/*****/

static void
ed_do_copy_gradient_callback(GtkWidget *widget, gpointer client_data, gpointer call_data)
{
	gradient_t     *grad;
	char           *gradient_name;
	int             pos;
	grad_segment_t *head, *prev, *cur, *orig;

	gradient_name = (char *) call_data;

	if (!gradient_name) {
		g_message ("ed_do_copy_gradient_callback(): oops, received NULL in call_data");
		return;
	} /* if */

	/* Copy current gradient */

	grad = grad_new_gradient();

	grad->name     = gradient_name; /* We don't need to copy since this memory is ours */
	grad->dirty    = 1;
	grad->filename = build_user_filename(grad->name, gradient_path);

	prev = NULL;
	orig = curr_gradient->segments;
	head = NULL;

	while (orig) {
		cur = seg_new_segment();

		*cur = *orig; /* Copy everything */

		cur->prev = prev;
		cur->next = NULL;

		if (prev)
			prev->next = cur;
		else
			head = cur; /* Remember head */

		prev = cur;
		orig = orig->next;
	} /* while */

	grad->segments = head;

	/* Put new gradient in list */

	pos = grad_insert_in_gradients_list(grad);
	curr_gradient = grad;
	ed_insert_in_gradients_listbox(grad, pos, 1);

	ed_update_editor(GRAD_UPDATE_PREVIEW | GRAD_RESET_CONTROL);
} /* ed_do_copy_gradient_callback */


/*****/

static void
ed_delete_gradient_callback(GtkWidget *widget, gpointer client_data)
{
	GtkWidget *dialog;
	GtkWidget *vbox;
	GtkWidget *label;
	GtkWidget *button;
	char      *str;

	if (num_gradients <= 1)
		return;

	dialog = gimp_dialog_new("Delete gradient");
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(g_editor->shell));
	g_signal_connect(dialog, "close-request",
			 G_CALLBACK(ed_delete_gradient_close_request), NULL);

	vbox = gimp_vbox_new(FALSE, 0);
	gimp_container_set_border_width(vbox, 8);
	gimp_box_pack_start(gimp_dialog_get_vbox(dialog), vbox, FALSE, FALSE, 0);

	/* Question */

	label = gtk_label_new("Are you sure you want to delete");
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label), 0.0);
	gimp_box_pack_start(vbox, label, FALSE, FALSE, 0);

	str = g_strdup_printf("\"%s\" from the list and from disk?", curr_gradient->name);

	label = gtk_label_new(str);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label), 0.0);
	gimp_box_pack_start(vbox, label, FALSE, FALSE, 0);

	g_free(str);

	/* Buttons */

	button = gimp_dialog_add_button(dialog, "Delete",
					G_CALLBACK(ed_do_delete_gradient_callback),
					dialog, TRUE);

	button = gimp_dialog_add_button(dialog, "Cancel",
					G_CALLBACK(ed_cancel_delete_gradient_callback),
					dialog, FALSE);
	(void) button;

	/* Show! */

	gtk_window_present(GTK_WINDOW(dialog));
	gtk_widget_set_sensitive(g_editor->shell, FALSE);
} /* ed_delete_gradient_callback */


/*****/

static void
ed_do_delete_gradient_callback(GtkWidget *widget, gpointer client_data)
{
	GSList        *tmp;
	int            n;
	gradient_t    *g;
	GtkWidget     *list_item;
	GtkListBoxRow *row;

	gtk_window_destroy(GTK_WINDOW(client_data));
	gtk_widget_set_sensitive(g_editor->shell, TRUE);

	/* See which gradient we will have to select once the current one is deleted */

	n   = 0;
	tmp = gradients_list;

	while (tmp) {
		g = tmp->data;

		if (g == curr_gradient) {
			if (tmp->next == NULL)
				n--; /* Will have to select the *previous* one */

			break; /* We found the one we want */
		} /* if */

		n++; /* Next gradient */
		tmp = g_slist_next(tmp);
	} /* while */

	if (tmp == NULL)
		fatal_error("ed_do_delete_gradient_callback(): aieee, could not find gradient to delete!");

	/* Delete gradient from gradients list */

	list_item = curr_gradient->list_item; /* Remember list item to delete it later */

	gradients_list = g_slist_remove(gradients_list, curr_gradient);
	num_gradients--;

	/* Delete file and free gradient */

	if (curr_gradient->filename)
		g_unlink(curr_gradient->filename);
	grad_free_gradient(curr_gradient);
	curr_gradient = NULL;

	/* Delete gradient from listbox */

	if (list_item)
		gtk_list_box_remove(GTK_LIST_BOX(g_editor->list), list_item);

	/* Select new gradient */

	curr_gradient = g_slist_nth(gradients_list, n)->data;
	row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_editor->list), n);
	if (row)
		gtk_list_box_select_row(GTK_LIST_BOX(g_editor->list), row);

	/* Update! */

	ed_update_editor(GRAD_UPDATE_PREVIEW | GRAD_RESET_CONTROL);
} /* ed_do_delete_gradient_callback */


/*****/

static void
ed_cancel_delete_gradient_callback(GtkWidget *widget, gpointer client_data)
{
	gtk_window_destroy(GTK_WINDOW(client_data));
	gtk_widget_set_sensitive(g_editor->shell, TRUE);
} /* ed_cancel_delete_gradient_callback */


/*****/

static gboolean
ed_delete_gradient_close_request(GtkWindow *window, gpointer client_data)
{
	ed_cancel_delete_gradient_callback(NULL, window);

	return TRUE;
} /* ed_delete_gradient_close_request */


/*****/

static void
ed_save_pov_callback(GtkWidget *widget, gpointer client_data)
{
	if (curr_gradient == NULL) return;

	gtk_widget_set_sensitive(g_editor->shell, FALSE);
	gimp_file_dialog_save(GTK_WINDOW(g_editor->shell), "Save as POV-Ray", NULL,
			      ed_do_save_pov_callback, NULL);
} /* ed_save_pov_callback */


/*****/

static void
ed_refresh_callback(GtkWidget *widget, gpointer client_data)
{
	GSList     *node;
	gradient_t *grad;

	for (node = gradients_list; node; node = g_slist_next(node)) {
		grad = node->data;

		if (grad->list_item) {
			gtk_list_box_remove(GTK_LIST_BOX(g_editor->list), grad->list_item);
			grad->list_item = NULL;
		} /* if */
	}

	grad_free_gradients();

	gradients_init(FALSE);

	if (curr_gradient)
		control_select_single_segment(curr_gradient->segments);

	ed_set_list_of_gradients();

	ed_update_editor(GRAD_UPDATE_PREVIEW | GRAD_RESET_CONTROL);
} /* ed_refresh_callback */

/*****/

static void
ed_do_save_pov_callback(const gchar *filename, gpointer client_data)
{
	FILE           *file;
	grad_segment_t *seg;

	gtk_widget_set_sensitive(g_editor->shell, TRUE);

	if (filename == NULL || curr_gradient == NULL)
		return; /* Cancelled */

	file = g_fopen(filename, "wb");

	if (!file)
		g_message ("ed_do_save_pov_callback(): oops, could not open \"%s\"", filename);
	else {
		fprintf(file, "/* color_map file created by the GIMP */\n");
		fprintf(file, "/* http://www.gimp.org/               */\n");

		fprintf(file, "color_map {\n");

		seg = curr_gradient->segments;

		while (seg) {
			/* Left */

			fprintf(file, "\t[%f color rgbt <%f, %f, %f, %f>]\n",
				seg->left,
				seg->r0, seg->g0, seg->b0, 1.0 - seg->a0);

			/* Middle */

			fprintf(file, "\t[%f color rgbt <%f, %f, %f, %f>]\n",
				seg->middle,
				(seg->r0 + seg->r1) / 2.0,
				(seg->g0 + seg->g1) / 2.0,
				(seg->b0 + seg->b1) / 2.0,
				1.0 - (seg->a0 + seg->a1) / 2.0);

			/* Right */

			fprintf(file, "\t[%f color rgbt <%f, %f, %f, %f>]\n",
				seg->right,
				seg->r1, seg->g1, seg->b1, 1.0 - seg->a1);

			/* Next! */

			seg = seg->next;
		} /* while */

		fprintf(file, "} /* color_map */\n");
		fclose(file);
	} /* else */
} /* ed_do_save_pov_callback */


/*****/

static void
ed_scrollbar_update(GtkAdjustment *adjustment, gpointer data)
{
	char           str[256];

	g_snprintf(str, sizeof(str), "Zoom factor: %d:1    Displaying [%0.6f, %0.6f]",
		   g_editor->zoom_factor, gtk_adjustment_get_value(adjustment),
		   gtk_adjustment_get_value(adjustment) + gtk_adjustment_get_page_size(adjustment));

	ed_set_hint(str);

	ed_update_editor(GRAD_UPDATE_PREVIEW | GRAD_UPDATE_CONTROL);
} /* ed_scrollbar_update */


/*****/

static void
ed_zoom_all_callback(GtkWidget *widget, gpointer client_data)
{
	g_editor->zoom_factor = 1;

	gtk_adjustment_configure(g_editor->scroll_data,
				 0.0, 0.0, 1.0,
				 1.0 * GRAD_SCROLLBAR_STEP_SIZE,
				 1.0 * GRAD_SCROLLBAR_PAGE_SIZE,
				 1.0);
} /* ed_zoom_all_callback */


/*****/

static void
ed_zoom_out_callback(GtkWidget *widget, gpointer client_data)
{
	GtkAdjustment *adjustment;
	double 	       old_value, value;
	double 	       old_page_size, page_size;

	if (g_editor->zoom_factor <= 1)
		return;

	adjustment = g_editor->scroll_data;

	old_value     = gtk_adjustment_get_value(adjustment);
	old_page_size = gtk_adjustment_get_page_size(adjustment);

	g_editor->zoom_factor--;

	page_size = 1.0 / g_editor->zoom_factor;
	value     = old_value - (page_size - old_page_size) / 2.0;

	if (value < 0.0)
		value = 0.0;
	else if ((value + page_size) > 1.0)
		value = 1.0 - page_size;

	gtk_adjustment_configure(adjustment, value, 0.0, 1.0,
				 page_size * GRAD_SCROLLBAR_STEP_SIZE,
				 page_size * GRAD_SCROLLBAR_PAGE_SIZE,
				 page_size);
} /* ed_zoom_out_callback */


/*****/

static void
ed_zoom_in_callback(GtkWidget *widget, gpointer client_data)
{
	GtkAdjustment *adjustment;
	double 	       old_value;
	double 	       old_page_size, page_size;

	adjustment = g_editor->scroll_data;

	old_value     = gtk_adjustment_get_value(adjustment);
	old_page_size = gtk_adjustment_get_page_size(adjustment);

	g_editor->zoom_factor++;

	page_size = 1.0 / g_editor->zoom_factor;

	gtk_adjustment_configure(adjustment,
				 old_value + (old_page_size - page_size) / 2.0,
				 0.0, 1.0,
				 page_size * GRAD_SCROLLBAR_STEP_SIZE,
				 page_size * GRAD_SCROLLBAR_PAGE_SIZE,
				 page_size);
} /* ed_zoom_in_callback */


/*****/

static void
ed_instant_update_update(GtkWidget *widget, gpointer data)
{
	/* GTK 4 scrollbars always update continuously; the toggle
	 * still decides whether the preview follows handle drags.
	 */

	if (gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)))
		g_editor->instant_update = 1;
	else
		g_editor->instant_update = 0;
} /* ed_instant_update_update */


/***** Gradient preview functions *****/

/*****/

static void
prev_motion(GtkEventControllerMotion *controller, gdouble dx, gdouble dy, gpointer data)
{
	gint x;

	/* ignore events when no gradient is present */
	if (curr_gradient == NULL)
	        return;

	/* While button 1 is down the drag gesture takes care of it */
	if (g_editor->preview_button_down)
		return;

	x = (gint) dx;

	if (x != g_editor->preview_last_x) {
		g_editor->preview_last_x = x;
		prev_set_hint(x);
	} /* if */
} /* prev_motion */


/*****/

static void
prev_leave(GtkEventControllerMotion *controller, gpointer data)
{
	if (curr_gradient == NULL)
	        return;

	ed_set_hint("");
} /* prev_leave */


/*****/

static void
prev_drag_begin(GtkGestureDrag *gesture, gdouble x, gdouble y, gpointer data)
{
	if (curr_gradient == NULL)
	        return;

	g_editor->preview_last_x = (gint) x;
	g_editor->preview_button_down = 1;
	prev_set_foreground((gint) x);
} /* prev_drag_begin */


/*****/

static void
prev_drag_update(GtkGestureDrag *gesture, gdouble dx, gdouble dy, gpointer data)
{
	gdouble start_x, start_y;
	gint    x;

	if (curr_gradient == NULL || !g_editor->preview_button_down)
	        return;

	gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
	x = (gint) (start_x + dx);

	if (x != g_editor->preview_last_x) {
		g_editor->preview_last_x = x;
		prev_set_foreground(x);
	} /* if */
} /* prev_drag_update */


/*****/

static void
prev_drag_end(GtkGestureDrag *gesture, gdouble dx, gdouble dy, gpointer data)
{
	gdouble start_x, start_y;
	gint    x;

	if (curr_gradient == NULL || !g_editor->preview_button_down)
	        return;

	gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
	x = (gint) (start_x + dx);

	g_editor->preview_last_x = x;
	g_editor->preview_button_down = 0;
	prev_set_foreground(x);
} /* prev_drag_end */


/*****/

static void
prev_popup(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data)
{
	if (curr_gradient == NULL)
	        return;

	cpopup_do_popup(g_editor->preview, x, y);
} /* prev_popup */


/*****/

static void
prev_set_hint(gint x)
{
	double xpos;
	double r, g, b, a;
	double h, s, v;
	char   str[256];

	xpos = control_calc_g_pos(x);

	grad_get_color_at(xpos, &r, &g, &b, &a);

	h = r;
	s = g;
	v = b;

	calc_rgb_to_hsv(&h, &s, &v);

	g_snprintf(str, sizeof(str), "Position: %0.6f    "
		   "RGB (%0.3f, %0.3f, %0.3f)    "
		   "HSV (%0.3f, %0.3f, %0.3f)    "
		   "Opacity: %0.3f",
		   xpos, r, g, b, h * 360.0, s, v, a);

	ed_set_hint(str);
} /* prev_set_hint */


/*****/

static void
prev_set_foreground(gint x)
{
	double xpos;
	double r, g, b, a;
	char   str[512];

	xpos = control_calc_g_pos(x);
	grad_get_color_at(xpos, &r, &g, &b, &a);

	palette_set_foreground(r * 255.0, g * 255.0, b * 255.0);

	g_snprintf(str, sizeof(str), "Foreground color set to RGB (%d, %d, %d) <-> (%0.3f, %0.3f, %0.3f)",
		   (int) (r * 255.0),
		   (int) (g * 255.0),
		   (int) (b * 255.0),
		   r, g, b);

	ed_set_hint(str);
} /* prev_set_foreground */


/*****/

static void
prev_update(int recalculate)
{
	gsize          rowsiz;
	GtkAdjustment *adjustment;
	int            width, height;

	/* We only update if a gradient is present */

	if (curr_gradient == NULL)
	        return;

	/* hof: do not change preview size on Window resize.
	 *      The original code allows expansion of the preview
	 *      on window resize events. But once expanded, there is no way to shrink
	 *      the window back to the original size.
	 *  A full Bugfix should change the preview size according to the users
	 *  window resize actions.
	 */

	width   = GRAD_PREVIEW_WIDTH;
	height  = GRAD_PREVIEW_HEIGHT;

	if (!g_editor->preview_rows[0] || !g_editor->preview_rows[1]) {
		rowsiz = width * 3 * sizeof(guchar);

		g_editor->preview_rows[0] = g_malloc(rowsiz);
		g_editor->preview_rows[1] = g_malloc(rowsiz);

		recalculate = 1; /* Force recalculation */
	} /* if */

	/* Have to redraw? */

	if (recalculate) {
		adjustment = g_editor->scroll_data;

		prev_fill_image(width, height,
				gtk_adjustment_get_value(adjustment),
				gtk_adjustment_get_value(adjustment) +
				gtk_adjustment_get_page_size(adjustment));
	} /* if */
} /* prev_update */


/*****/

static void
prev_fill_image(int width, int height, double left, double right)
{
	guchar *p0, *p1;
	int     x, y;
	double  dx, cur_x;
	double  r, g, b, a;
	double  c0, c1;

	dx    = (right - left) / (width - 1);
	cur_x = left;
	p0    = g_editor->preview_rows[0];
	p1    = g_editor->preview_rows[1];

	/* Create lines to fill the image */

	for (x = 0; x < width; x++) {
		grad_get_color_at(cur_x, &r, &g, &b, &a);

		if ((x / GRAD_CHECK_SIZE) & 1) {
			c0 = GRAD_CHECK_LIGHT;
			c1 = GRAD_CHECK_DARK;
		} else {
			c0 = GRAD_CHECK_DARK;
			c1 = GRAD_CHECK_LIGHT;
		} /* else */

		*p0++ = (c0 + (r - c0) * a) * 255.0;
		*p0++ = (c0 + (g - c0) * a) * 255.0;
		*p0++ = (c0 + (b - c0) * a) * 255.0;

		*p1++ = (c1 + (r - c1) * a) * 255.0;
		*p1++ = (c1 + (g - c1) * a) * 255.0;
		*p1++ = (c1 + (b - c1) * a) * 255.0;

		cur_x += dx;
	} /* for */

	/* Fill image */

	for (y = 0; y < height; y++)
		if ((y / GRAD_CHECK_SIZE) & 1)
			gimp_preview_draw_row(GIMP_PREVIEW(g_editor->preview),
					      g_editor->preview_rows[1], 0, y, width);
		else
			gimp_preview_draw_row(GIMP_PREVIEW(g_editor->preview),
					      g_editor->preview_rows[0], 0, y, width);
} /* prev_fill_image */


/***** Gradient control functions *****/

/* *** WARNING *** WARNING *** WARNING ***
 *
 * All the event-handling code for the gradient control widget is
 * extremely hairy.  You are not expected to understand it.  If you
 * find bugs, mail me unless you are very brave and you want to fix
 * them yourself ;-)
 */

/*****/

static guint32
control_event_time(gpointer controller)
{
	return gtk_event_controller_get_current_event_time(GTK_EVENT_CONTROLLER(controller));
} /* control_event_time */


/*****/

static void
control_leave(GtkEventControllerMotion *controller, gpointer data)
{
	ed_set_hint("");
} /* control_leave */


/*****/

static void
control_motion_event(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer data)
{
	if (curr_gradient == NULL)
		return;

	/* While a handle is being dragged the drag gesture reports the motion */

	if (g_editor->control_drag_mode != GRAD_DRAG_NONE)
		return;

	control_pointer_moved((gint) x, (gint) y, control_event_time(controller));
} /* control_motion_event */


/*****/

static void
control_pointer_moved(gint x, gint y, guint32 time)
{
	if (x != g_editor->control_last_x) {
		g_editor->control_last_x = x;

		if (g_editor->control_drag_mode != GRAD_DRAG_NONE) {
			if ((time - g_editor->control_click_time) >= GRAD_MOVE_TIME)
				control_motion(x);
		} else {
			ed_update_editor(GRAD_UPDATE_CONTROL);

			control_do_hint(x, y);
		} /* else */
	} /* if */
} /* control_pointer_moved */


/*****/

static void
control_drag_begin(GtkGestureDrag *gesture, gdouble x, gdouble y, gpointer data)
{
	GdkModifierType state;

	if (curr_gradient == NULL)
		return;

	if (g_editor->control_drag_mode == GRAD_DRAG_NONE) {
		state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));

		g_editor->control_last_x       = (gint) x;
		g_editor->control_drag_start_x = x;
		g_editor->control_click_time   = control_event_time(gesture);

		control_button_press((gint) x, (gint) y, 1, state);
	} /* if */
} /* control_drag_begin */


/*****/

static void
control_drag_update(GtkGestureDrag *gesture, gdouble dx, gdouble dy, gpointer data)
{
	gdouble start_x, start_y;

	if (curr_gradient == NULL || g_editor->control_drag_mode == GRAD_DRAG_NONE)
		return;

	gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);

	control_pointer_moved((gint) (start_x + dx), (gint) (start_y + dy),
			      control_event_time(gesture));
} /* control_drag_update */


/*****/

static void
control_drag_end(GtkGestureDrag *gesture, gdouble dx, gdouble dy, gpointer data)
{
	gint            x, y;
	gdouble         start_x, start_y;
	guint32         time;
	grad_segment_t *seg;

	if (curr_gradient == NULL)
		return;

	ed_set_hint("");

	if (g_editor->control_drag_mode != GRAD_DRAG_NONE) {
		gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
		x = (gint) (start_x + dx);
		y = (gint) (start_y + dy);

		time = control_event_time(gesture);

		if ((time - g_editor->control_click_time) >= GRAD_MOVE_TIME)
			ed_update_editor(GRAD_UPDATE_PREVIEW); /* Possible move */
		else
			if ((g_editor->control_drag_mode == GRAD_DRAG_MIDDLE) ||
			    (g_editor->control_drag_mode == GRAD_DRAG_ALL)) {
				seg = g_editor->control_drag_segment;

				if ((g_editor->control_drag_mode == GRAD_DRAG_ALL) &&
				    g_editor->control_compress)
					control_extend_selection(seg, control_calc_g_pos(x));
				else
					control_select_single_segment(seg);

				ed_update_editor(GRAD_UPDATE_CONTROL);
			} /* if */

		g_editor->control_drag_mode = GRAD_DRAG_NONE;
		g_editor->control_compress  = 0;

		control_do_hint(x, y);
	} /* if */
} /* control_drag_end */


/*****/

static void
control_popup(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data)
{
	GdkModifierType state;

	if (curr_gradient == NULL)
		return;

	if (g_editor->control_drag_mode == GRAD_DRAG_NONE) {
		state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));

		g_editor->control_last_x     = (gint) x;
		g_editor->control_click_time = control_event_time(gesture);

		control_button_press((gint) x, (gint) y, 3, state);
	} /* if */
} /* control_popup */


/*****/

static void
control_do_hint(gint x, gint y)
{
	grad_segment_t      *seg;
	control_drag_mode_t  handle;
	int                  in_handle;
	double               pos;

	pos = control_calc_g_pos(x);

	if ((pos < 0.0) || (pos > 1.0))
		return;

	seg_get_closest_handle(curr_gradient, pos, &seg, &handle);

	in_handle = control_point_in_handle(x, y, seg, handle);

	if (in_handle) {
		switch (handle) {
			case GRAD_DRAG_LEFT:
				if (seg != NULL) {
					if (seg->prev != NULL)
						ed_set_hint("Drag: move    Shift+drag: move & compress");
					else
						ed_set_hint("Click: select    Shift+click: extend selection");
				} else
					ed_set_hint("Click: select    Shift+click: extend selection");

				break;

			case GRAD_DRAG_MIDDLE:
				ed_set_hint("Click: select    Shift+click: extend selection    "
					    "Drag: move");

				break;

			default:
				g_message ("control_do_hint: oops, in_handle is true "
					"yet we got handle type %d", (int) handle);
				break;
		} /* switch */
	} else
		ed_set_hint("Click: select    Shift+click: extend selection    "
			    "Drag: move    Shift+drag: move & compress");
} /* control_do_hint */


/*****/

static void
control_button_press(gint x, gint y, guint button, guint state)
{
	grad_segment_t      *seg;
	control_drag_mode_t  handle;
	double               xpos;
	int                  in_handle;

	/* See which button was pressed */

	switch (button) {
		case 1:
			break;

		case 3:
			cpopup_do_popup(g_editor->control, x, y);
			return;

		default:
			return;
	} /* switch */

	/* Find the closest handle */

	xpos = control_calc_g_pos(x);

	seg_get_closest_handle(curr_gradient, xpos, &seg, &handle);

	in_handle = control_point_in_handle(x, y, seg, handle);

	/* Now see what we have */

	if (in_handle)
		switch (handle) {
			case GRAD_DRAG_LEFT:
				if (seg != NULL) {
					/* Left handle of some segment */

					if (state & GDK_SHIFT_MASK) {
						if (seg->prev != NULL) {
							g_editor->control_drag_mode    = GRAD_DRAG_LEFT;
							g_editor->control_drag_segment = seg;
							g_editor->control_compress     = 1;
						} else {
							control_extend_selection(seg, xpos);

							ed_update_editor(GRAD_UPDATE_CONTROL);
						} /* else */
					} else
						if (seg->prev != NULL) {
							g_editor->control_drag_mode    = GRAD_DRAG_LEFT;
							g_editor->control_drag_segment = seg;
						} else {
							control_select_single_segment(seg);

							ed_update_editor(GRAD_UPDATE_CONTROL);
						} /* else */

					return;
				} else {
					/* Right handle of last segment */

					seg = seg_get_last_segment(curr_gradient->segments);

					if (state & GDK_SHIFT_MASK) {
						control_extend_selection(seg, xpos);

						ed_update_editor(GRAD_UPDATE_CONTROL);
					} else {
						control_select_single_segment(seg);

						ed_update_editor(GRAD_UPDATE_CONTROL);
					} /* else */

					return;
				} /* else */

				break;

			case GRAD_DRAG_MIDDLE:
				if (state & GDK_SHIFT_MASK) {
					control_extend_selection(seg, xpos);

					ed_update_editor(GRAD_UPDATE_CONTROL);
				} else {
					g_editor->control_drag_mode    = GRAD_DRAG_MIDDLE;
					g_editor->control_drag_segment = seg;
				} /* else */

				return;

			default:
				g_message ("control_button_press(): oops, in_handle is true "
					"yet we got handle type %d", (int) handle);
				return;
		} /* switch */
	else {
		seg = seg_get_segment_at(curr_gradient, xpos);

		g_editor->control_drag_mode    = GRAD_DRAG_ALL;
		g_editor->control_drag_segment = seg;
		g_editor->control_last_gx      = xpos;
		g_editor->control_orig_pos     = xpos;

		if (state & GDK_SHIFT_MASK)
			g_editor->control_compress = 1;

		return;
	} /* else */
} /* control_button_press */


/*****/

static int
control_point_in_handle(gint x, gint y, grad_segment_t *seg, control_drag_mode_t handle)
{
	gint handle_pos;

	switch (handle) {
		case GRAD_DRAG_LEFT:
			if (seg)
				handle_pos = control_calc_p_pos(seg->left);
			else {
				seg = seg_get_last_segment(curr_gradient->segments);

				handle_pos = control_calc_p_pos(seg->right);
			} /* else */

			break;

		case GRAD_DRAG_MIDDLE:
			handle_pos = control_calc_p_pos(seg->middle);
			break;

		default:
			g_message ("control_point_in_handle(): oops, can not handle drag mode %d",
				(int) handle);
			return 0;
	} /* switch */

	y /= 2;

	if ((x >= (handle_pos - y)) && (x <= (handle_pos + y)))
		return 1;
	else
		return 0;
} /* control_point_in_handle */


/*****/

static void
control_select_single_segment(grad_segment_t *seg)
{
	g_editor->control_sel_l = seg;
	g_editor->control_sel_r = seg;
} /* control_select_single_segment */


/*****/

static void
control_extend_selection(grad_segment_t *seg, double pos)
{
	if (fabs(pos - g_editor->control_sel_l->left) < fabs(pos - g_editor->control_sel_r->right))
		g_editor->control_sel_l = seg;
	else
		g_editor->control_sel_r = seg;
} /* control_extend_selection */


/*****/

static void
control_motion(gint x)
{
	grad_segment_t *seg;
	double          pos;
	double          delta;
	char            str[256];

	seg = g_editor->control_drag_segment;

	switch (g_editor->control_drag_mode) {
		case GRAD_DRAG_LEFT:
			pos = control_calc_g_pos(x);

			if (!g_editor->control_compress)
				seg->prev->right = seg->left = BOUNDS(pos,
								      seg->prev->middle + EPSILON,
								      seg->middle - EPSILON);
			else
				control_compress_left(g_editor->control_sel_l,
						      g_editor->control_sel_r,
						      seg, pos);

			sprintf(str, "Handle position: %0.6f", seg->left);
			ed_set_hint(str);

			break;

		case GRAD_DRAG_MIDDLE:
			pos = control_calc_g_pos(x);
			seg->middle = BOUNDS(pos, seg->left + EPSILON, seg->right - EPSILON);

			sprintf(str, "Handle position: %0.6f", seg->middle);
			ed_set_hint(str);

			break;

		case GRAD_DRAG_ALL:
			pos    = control_calc_g_pos(x);
			delta  = pos - g_editor->control_last_gx;

			if ((seg->left >= g_editor->control_sel_l->left) &&
			    (seg->right <= g_editor->control_sel_r->right))
				delta = control_move(g_editor->control_sel_l, g_editor->control_sel_r, delta);
			else
				delta = control_move(seg, seg, delta);

			g_editor->control_last_gx += delta;

			sprintf(str, "Distance: %0.6f",
				g_editor->control_last_gx - g_editor->control_orig_pos);
			ed_set_hint(str);

			break;

		default:
			fatal_error("control_motion(): aieee, attempt to move bogus handle %d",
				    (int) g_editor->control_drag_mode);
			break;
	} /* switch */

	curr_gradient->dirty = 1;

	if (g_editor->instant_update)
		ed_update_editor(GRAD_UPDATE_PREVIEW | GRAD_UPDATE_CONTROL);
	else
		ed_update_editor(GRAD_UPDATE_CONTROL);
} /* control_motion */


/*****/

static void
control_compress_left(grad_segment_t *range_l, grad_segment_t *range_r,
		      grad_segment_t *drag_seg, double pos)
{
	grad_segment_t *seg;
	double          lbound, rbound;
	int             k;

	/* Check what we have to compress */

	if (!((drag_seg->left >= range_l->left) &&
	      ((drag_seg->right <= range_r->right) || (drag_seg == range_r->next)))) {
		/* We are compressing a segment outside the selection */

		range_l = range_r = drag_seg;
	} /* else */

	/* Calculate left bound for dragged hadle */

	if (drag_seg == range_l)
		lbound = range_l->prev->left + 2.0 * EPSILON;
	else {
		/* Count number of segments to the left of the dragged handle */

		seg = drag_seg;
		k   = 0;

		while (seg != range_l) {
			k++;
			seg = seg->prev;
		} /* while */

		/* 2*k handles have to fit */

		lbound = range_l->left + 2.0 * k * EPSILON;
	} /* else */

	/* Calculate right bound for dragged handle */

	if (drag_seg == range_r->next)
		rbound = range_r->next->right - 2.0 * EPSILON;
	else {
		/* Count number of segments to the right of the dragged handle */

		seg = drag_seg;
		k   = 1;

		while (seg != range_r) {
			k++;
			seg = seg->next;
		} /* while */

		/* 2*k handles have to fit */

		rbound = range_r->right - 2.0 * k * EPSILON;
	} /* else */

	/* Calculate position */

	pos = BOUNDS(pos, lbound, rbound);

	/* Compress segments to the left of the handle */

	if (drag_seg == range_l)
		control_compress_range(range_l->prev, range_l->prev, range_l->prev->left, pos);
	else
		control_compress_range(range_l, drag_seg->prev, range_l->left, pos);

	/* Compress segments to the right of the handle */

	if (drag_seg != range_r->next)
		control_compress_range(drag_seg, range_r, pos, range_r->right);
	else
		control_compress_range(drag_seg, drag_seg, pos, drag_seg->right);
} /* control_compress_left */


/*****/

static void
control_compress_range(grad_segment_t *range_l, grad_segment_t *range_r,
		       double new_l, double new_r)
{
	double          orig_l, orig_r;
	double          scale;
	grad_segment_t *seg, *aseg;

	orig_l = range_l->left;
	orig_r = range_r->right;

	scale = (new_r - new_l) / (orig_r - orig_l);

	seg = range_l;

	do {
		seg->left   = new_l + (seg->left - orig_l) * scale;
		seg->middle = new_l + (seg->middle - orig_l) * scale;
		seg->right  = new_l + (seg->right - orig_l) * scale;

		/* Next */

		aseg = seg;
		seg  = seg->next;
	} while (aseg != range_r);
} /* control_compress_range */


/*****/

static double
control_move(grad_segment_t *range_l, grad_segment_t *range_r, double delta)
{
	double          lbound, rbound;
	int             is_first, is_last;
	grad_segment_t *seg, *aseg;

	/* First or last segments in gradient? */

	is_first = (range_l->prev == NULL);
	is_last  = (range_r->next == NULL);

	/* Calculate drag bounds */

	if (!g_editor->control_compress) {
		if (!is_first)
			lbound = range_l->prev->middle + EPSILON;
		else
			lbound = range_l->left + EPSILON;

		if (!is_last)
			rbound = range_r->next->middle - EPSILON;
		else
			rbound = range_r->right - EPSILON;
	} else {
		if (!is_first)
			lbound = range_l->prev->left + 2.0 * EPSILON;
		else
			lbound = range_l->left + EPSILON;

		if (!is_last)
			rbound = range_r->next->right - 2.0 * EPSILON;
		else
			rbound = range_r->right - EPSILON;
	} /* if */

	/* Fix the delta if necessary */

	if (delta < 0.0) {
		if (!is_first) {
			if (range_l->left + delta < lbound)
				delta = lbound - range_l->left;
		} else
			if (range_l->middle + delta < lbound)
				delta = lbound - range_l->middle;
	} else {
		if (!is_last) {
			if (range_r->right + delta > rbound)
				delta = rbound - range_r->right;
		} else
			if (range_r->middle + delta > rbound)
				delta = rbound - range_r->middle;
	} /* else */

	/* Move all the segments inside the range */

	seg = range_l;

	do {
		if (!((seg == range_l) && is_first))
			seg->left   += delta;

		seg->middle += delta;

		if (!((seg == range_r) && is_last))
			seg->right  += delta;

		/* Next */

		aseg = seg;
		seg  = seg->next;
	} while (aseg != range_r);

	/* Fix the segments that surround the range */

	if (!is_first)
	  {
		if (!g_editor->control_compress)
			range_l->prev->right = range_l->left;
		else
			control_compress_range(range_l->prev, range_l->prev,
					       range_l->prev->left, range_l->left);
	  }
	
	if (!is_last)
	  {
		if (!g_editor->control_compress)
			range_r->next->left = range_r->right;
		else
			control_compress_range(range_r->next, range_r->next,
					       range_r->right, range_r->next->right);
	  }
	
	return delta;
} /* control_move */


/*****/

static void
control_update(int recalculate)
{
	/* We only update if a gradient is present */

	if (curr_gradient == NULL)
	        return;

	/* Have to reset the selection? */

	if (recalculate || (g_editor->control_sel_l == NULL))
		control_select_single_segment(curr_gradient->segments);

	/* Keep the pop-up's actions (and their shortcuts) in step with
	 * the selection.
	 */

	cpopup_adjust_menus();

	gtk_widget_queue_draw(g_editor->control);
} /* control_update */


/*****/

static void
control_draw_func(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
	if (curr_gradient == NULL || g_editor->control_sel_l == NULL)
		return;

	/* As long as we have that ugly workaround in prev_update() the
	 * control does not change its size either.  The handles are as
	 * tall as the preview; only their tips show in the control.
	 */

	control_draw(cr,
		     GRAD_PREVIEW_WIDTH, GRAD_PREVIEW_HEIGHT,
		     gtk_adjustment_get_value(g_editor->scroll_data),
		     gtk_adjustment_get_value(g_editor->scroll_data) +
		     gtk_adjustment_get_page_size(g_editor->scroll_data));
} /* control_draw_func */


/*****/

static void
control_draw(cairo_t *cr, int width, int height, double left, double right)
{
	int 		     sel_l, sel_r;
	double               g_pos;
	grad_segment_t      *seg;
	control_drag_mode_t  handle;

	/* Clear the background */

	cairo_set_source_rgb(cr, 0.84, 0.84, 0.84);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_fill(cr);

	/* Draw selection */

	sel_l = control_calc_p_pos(g_editor->control_sel_l->left);
	sel_r = control_calc_p_pos(g_editor->control_sel_r->right);

	cairo_set_source_rgb(cr, 0.55, 0.55, 0.55);
	cairo_rectangle(cr, sel_l, 0, sel_r - sel_l + 1, height);
	cairo_fill(cr);

	/* Draw handles */

	seg = curr_gradient->segments;

	while (seg) {
		control_draw_normal_handle(cr, seg->left, height);
		control_draw_middle_handle(cr, seg->middle, height);

		/* Draw right handle only if this is the last segment */

		if (seg->next == NULL)
			control_draw_normal_handle(cr, seg->right, height);

		/* Next! */

		seg = seg->next;
	} /* while */

	/* Draw the handle which is closest to the mouse position */

	g_pos = control_calc_g_pos(g_editor->control_last_x);

	seg_get_closest_handle(curr_gradient, BOUNDS(g_pos, 0.0, 1.0), &seg, &handle);

	switch (handle) {
		case GRAD_DRAG_LEFT:
			if (seg)
				control_draw_normal_handle(cr, seg->left, height);
			else {
				seg = seg_get_last_segment(curr_gradient->segments);
				control_draw_normal_handle(cr, seg->right, height);
			} /* else */

			break;

		case GRAD_DRAG_MIDDLE:
			control_draw_middle_handle(cr, seg->middle, height);
			break;

		default:
			break;
	} /* switch */
} /* control_draw */


/*****/

static void
control_draw_normal_handle(cairo_t *cr, double pos, int height)
{
	control_draw_handle(cr, 0.0, control_calc_p_pos(pos), height);
} /* control_draw_normal_handle */


/*****/

static void
control_draw_middle_handle(cairo_t *cr, double pos, int height)
{
	control_draw_handle(cr, 0.93, control_calc_p_pos(pos), height);
} /* control_draw_middle_handle */


/*****/

static void
control_draw_handle(cairo_t *cr, double fill, int xpos, int height)
{
	int y;
	int left, right, bottom;

	/* The inside, a row at a time */

	cairo_set_source_rgb(cr, fill, fill, fill);

	for (y = 0; y < height; y++)
		cairo_rectangle(cr, xpos - y / 2, y, (y / 2) * 2 + 1, 1);

	cairo_fill(cr);

	/* The border */

	bottom = height - 1;
	left   = xpos - bottom / 2;
	right  = xpos + bottom / 2;

	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	cairo_set_line_width(cr, 1.0);

	cairo_move_to(cr, xpos + 0.5, 0.5);
	cairo_line_to(cr, left + 0.5, bottom + 0.5);
	cairo_move_to(cr, xpos + 0.5, 0.5);
	cairo_line_to(cr, right + 0.5, bottom + 0.5);
	cairo_move_to(cr, left + 0.5, bottom + 0.5);
	cairo_line_to(cr, right + 0.5, bottom + 0.5);
	cairo_stroke(cr);
} /* control_draw_handle */


/*****/

static int
control_calc_p_pos(double pos)
{
	gint           pwidth;
	GtkAdjustment *adjustment;

	/* Calculate the position (in widget's coordinates) of the
	 * requested point from the gradient.  Rounding is done to
	 * minimize mismatches between the rendered gradient preview
	 * and the gradient control's handles.
	 */

	adjustment = g_editor->scroll_data;
	pwidth     = GRAD_PREVIEW_WIDTH;

	return (int) ((pwidth - 1) * (pos - gtk_adjustment_get_value(adjustment)) /
		      gtk_adjustment_get_page_size(adjustment) + 0.5);
} /* control_calc_p_pos */


/*****/

static double
control_calc_g_pos(int pos)
{
	gint   	       pwidth;
	GtkAdjustment *adjustment;

	/* Calculate the gradient position that corresponds to widget's coordinates */

	adjustment = g_editor->scroll_data;
	pwidth     = GRAD_PREVIEW_WIDTH;

	return gtk_adjustment_get_page_size(adjustment) * pos / (pwidth - 1) +
		gtk_adjustment_get_value(adjustment);
} /* control_calc_g_pos */


/***** Control popup functions *****/

/* The pop-up menu is a GtkPopoverMenu on a GMenu that is rebuilt every
 * time it pops up (its labels depend on the selection).  Every entry
 * activates an action in the "grad" group inserted on the editor's
 * shell; the same actions carry the editor's keyboard shortcuts.  The
 * entries that show a color are custom buttons with a color box.
 */

typedef void (*CpopupCallback) (GtkWidget *widget, gpointer data);

typedef struct {
	const char     *name;
	CpopupCallback  callback;
} cpopup_action_t;

static const cpopup_action_t cpopup_plain_actions[] = {
	{ "left-color",     cpopup_set_left_color_callback },
	{ "right-color",    cpopup_set_right_color_callback },
	{ "split-midpoint", cpopup_split_midpoint_callback },
	{ "split-uniform",  cpopup_split_uniform_callback },
	{ "delete",         cpopup_delete_callback },
	{ "recenter",       cpopup_recenter_callback },
	{ "redistribute",   cpopup_redistribute_callback },
	{ "flip",           cpopup_flip_callback },
	{ "replicate",      cpopup_replicate_callback },
	{ "blend-colors",   cpopup_blend_colors },
	{ "blend-opacity",  cpopup_blend_opacity },
};

static const cpopup_action_t cpopup_int_actions[] = {
	{ "load-left",  cpopup_load_left_callback },
	{ "save-left",  cpopup_save_left_callback },
	{ "load-right", cpopup_load_right_callback },
	{ "save-right", cpopup_save_right_callback },
	{ "blending",   cpopup_blending_callback },
	{ "coloring",   cpopup_coloring_callback },
};

/* Keyboard shortcuts, as the GTK 1 accelerators were */

typedef struct {
	guint            keyval;
	GdkModifierType  mods;
	const char      *action;
	int              target; /* -1: the action takes no parameter */
} cpopup_shortcut_t;

static const cpopup_shortcut_t cpopup_shortcuts[] = {
	{ GDK_KEY_l, 0,                "grad.left-color",     -1 },
	{ GDK_KEY_l, GDK_CONTROL_MASK, "grad.load-left",       0 },
	{ GDK_KEY_l, GDK_ALT_MASK,     "grad.load-left",       1 },
	{ GDK_KEY_f, GDK_CONTROL_MASK, "grad.load-left",       2 },
	{ GDK_KEY_r, 0,                "grad.right-color",    -1 },
	{ GDK_KEY_r, GDK_CONTROL_MASK, "grad.load-right",      0 },
	{ GDK_KEY_r, GDK_ALT_MASK,     "grad.load-right",      1 },
	{ GDK_KEY_f, GDK_ALT_MASK,     "grad.load-right",      2 },
	{ GDK_KEY_s, 0,                "grad.split-midpoint", -1 },
	{ GDK_KEY_u, 0,                "grad.split-uniform",  -1 },
	{ GDK_KEY_d, 0,                "grad.delete",         -1 },
	{ GDK_KEY_c, 0,                "grad.recenter",       -1 },
	{ GDK_KEY_c, GDK_CONTROL_MASK, "grad.redistribute",   -1 },
	{ GDK_KEY_f, 0,                "grad.flip",           -1 },
	{ GDK_KEY_m, 0,                "grad.replicate",      -1 },
	{ GDK_KEY_b, 0,                "grad.blend-colors",   -1 },
	{ GDK_KEY_b, GDK_CONTROL_MASK, "grad.blend-opacity",  -1 },
};


/*****/

static void
cpopup_plain_action_activate(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	const cpopup_action_t *entry = data;

	if (curr_gradient == NULL || g_editor->control_sel_l == NULL)
		return;

	(* entry->callback) (NULL, NULL);
} /* cpopup_plain_action_activate */


/*****/

static void
cpopup_int_action_activate(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	const cpopup_action_t *entry = data;

	if (curr_gradient == NULL || g_editor->control_sel_l == NULL || parameter == NULL)
		return;

	(* entry->callback) (NULL, GINT_TO_POINTER(g_variant_get_int32(parameter)));
} /* cpopup_int_action_activate */


/*****/

static void
cpopup_create_main_menu(void)
{
	GSimpleAction      *action;
	GtkEventController *controller;
	GtkShortcut        *shortcut;
	int                 i;

	g_editor->actions = g_simple_action_group_new();

	for (i = 0; i < (int) G_N_ELEMENTS(cpopup_plain_actions); i++) {
		action = g_simple_action_new(cpopup_plain_actions[i].name, NULL);
		g_signal_connect(action, "activate",
				 G_CALLBACK(cpopup_plain_action_activate),
				 (gpointer) &cpopup_plain_actions[i]);
		g_action_map_add_action(G_ACTION_MAP(g_editor->actions), G_ACTION(action));
		g_object_unref(action);
	} /* for */

	for (i = 0; i < (int) G_N_ELEMENTS(cpopup_int_actions); i++) {
		if (strcmp(cpopup_int_actions[i].name, "blending") == 0 ||
		    strcmp(cpopup_int_actions[i].name, "coloring") == 0)
			/* Radio items: the state is the current type, -1 when it varies */
			action = g_simple_action_new_stateful(cpopup_int_actions[i].name,
							      G_VARIANT_TYPE_INT32,
							      g_variant_new_int32(0));
		else
			action = g_simple_action_new(cpopup_int_actions[i].name,
						     G_VARIANT_TYPE_INT32);

		g_signal_connect(action, "activate",
				 G_CALLBACK(cpopup_int_action_activate),
				 (gpointer) &cpopup_int_actions[i]);
		g_action_map_add_action(G_ACTION_MAP(g_editor->actions), G_ACTION(action));
		g_object_unref(action);
	} /* for */

	gtk_widget_insert_action_group(g_editor->shell, "grad",
				       G_ACTION_GROUP(g_editor->actions));

	/* Shortcuts */

	controller = gtk_shortcut_controller_new();

	for (i = 0; i < (int) G_N_ELEMENTS(cpopup_shortcuts); i++) {
		shortcut = gtk_shortcut_new(gtk_keyval_trigger_new(cpopup_shortcuts[i].keyval,
								   cpopup_shortcuts[i].mods),
					    gtk_named_action_new(cpopup_shortcuts[i].action));

		if (cpopup_shortcuts[i].target >= 0)
			gtk_shortcut_set_arguments(shortcut,
						   g_variant_new_int32(cpopup_shortcuts[i].target));

		gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller), shortcut);
	} /* for */

	gtk_widget_add_controller(g_editor->shell, controller);
} /* cpopup_create_main_menu */


/*****/

/* Adds a plain item to menu, with the accelerator it shows */

static void
cpopup_menu_append(GMenu *menu, const char *label, const char *action, const char *accel)
{
	GMenuItem *item;

	item = g_menu_item_new(label, action);

	if (accel)
		g_menu_item_set_attribute(item, "accel", "s", accel);

	g_menu_append_item(menu, item);
	g_object_unref(item);
} /* cpopup_menu_append */


/*****/

/* Adds a placeholder to menu for a custom color item called id */

static void
cpopup_menu_append_custom(GMenu *menu, const char *id)
{
	GMenuItem *item;

	item = g_menu_item_new(NULL, NULL);
	g_menu_item_set_attribute(item, "custom", "s", id);
	g_menu_append_item(menu, item);
	g_object_unref(item);
} /* cpopup_menu_append_custom */


/*****/

/* Adds a radio item to menu, for action with the given target */

static void
cpopup_menu_append_radio(GMenu *menu, const char *label, const char *action, int target)
{
	GMenuItem *item;

	item = g_menu_item_new(label, NULL);
	g_menu_item_set_action_and_target_value(item, action, g_variant_new_int32(target));
	g_menu_append_item(menu, item);
	g_object_unref(item);
} /* cpopup_menu_append_radio */


/*****/

static void
cpopup_color_item_clicked(GtkWidget *button, gpointer data)
{
	GtkWidget *popover;

	popover = gtk_widget_get_ancestor(button, GTK_TYPE_POPOVER);
	if (popover)
		gtk_popover_popdown(GTK_POPOVER(popover));
} /* cpopup_color_item_clicked */


/*****/

/* A menu entry showing a color box and a label.  Clicking it activates
 * action (with target, unless it is negative).
 */

static GtkWidget *
cpopup_create_color_item(const char *label, const char *action, int target,
			 double r, double g, double b, double a)
{
	GtkWidget *button;
	GtkWidget *hbox;
	GtkWidget *wlabel;

	button = gtk_button_new();
	gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);

	hbox = gimp_hbox_new(FALSE, 0);
	gtk_button_set_child(GTK_BUTTON(button), hbox);

	gimp_box_pack_start(hbox, cpopup_create_color_box(r, g, b, a), FALSE, FALSE, 2);

	wlabel = gtk_label_new(label);
	gtk_label_set_xalign(GTK_LABEL(wlabel), 0.0);
	gimp_box_pack_start(hbox, wlabel, TRUE, TRUE, 4);

	if (target >= 0)
		gtk_actionable_set_action_target_value(GTK_ACTIONABLE(button),
						       g_variant_new_int32(target));
	gtk_actionable_set_action_name(GTK_ACTIONABLE(button), action);

	g_signal_connect(button, "clicked", G_CALLBACK(cpopup_color_item_clicked), NULL);

	return button;
} /* cpopup_create_color_item */


/*****/

static void
cpopup_color_box_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
	double *color = data;
	int     x, y;
	double  c;

	/* Black border, checks with the color composited over them */

	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	cairo_rectangle(cr, 0, 0, GRAD_COLOR_BOX_WIDTH, GRAD_COLOR_BOX_HEIGHT);
	cairo_fill(cr);

	for (y = 1; y < (GRAD_COLOR_BOX_HEIGHT - 1); y += GRAD_CHECK_SIZE - (y % GRAD_CHECK_SIZE))
		for (x = 1; x < (GRAD_COLOR_BOX_WIDTH - 1); x += GRAD_CHECK_SIZE - (x % GRAD_CHECK_SIZE)) {
			if (((x / GRAD_CHECK_SIZE) ^ (y / GRAD_CHECK_SIZE)) & 1)
				c = GRAD_CHECK_LIGHT;
			else
				c = GRAD_CHECK_DARK;

			cairo_set_source_rgb(cr,
					     c + (color[0] - c) * color[3],
					     c + (color[1] - c) * color[3],
					     c + (color[2] - c) * color[3]);
			cairo_rectangle(cr, x, y,
					MIN(GRAD_CHECK_SIZE - (x % GRAD_CHECK_SIZE), (GRAD_COLOR_BOX_WIDTH - 1) - x),
					MIN(GRAD_CHECK_SIZE - (y % GRAD_CHECK_SIZE), (GRAD_COLOR_BOX_HEIGHT - 1) - y));
			cairo_fill(cr);
		} /* for */
} /* cpopup_color_box_draw */


/*****/

static GtkWidget *
cpopup_create_color_box(double r, double g, double b, double a)
{
	GtkWidget *box;
	double    *color;

	color = g_new(double, 4);
	color[0] = r;
	color[1] = g;
	color[2] = b;
	color[3] = a;

	box = gtk_drawing_area_new();
	gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(box), GRAD_COLOR_BOX_WIDTH);
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(box), GRAD_COLOR_BOX_HEIGHT);
	gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(box), cpopup_color_box_draw,
				       color, g_free);

	return box;
} /* cpopup_create_color_box */


/*****/

/* Builds a "Load from" submenu for the left (is_left) or right endpoint */

static GMenu *
cpopup_create_load_menu(GtkWidget *popover_widgets[], int *n_widgets, int is_left)
{
	GMenu          *menu;
	GMenu          *section;
	grad_segment_t *seg;
	double          fg_r, fg_g, fg_b, fg_a;
	const char     *action;
	char           *id;
	char            str[256];
	int             i;

	menu   = g_menu_new();
	action = is_left ? "grad.load-left" : "grad.load-right";

	/* Colors to fetch */

	section = g_menu_new();

	for (i = 0; i < 3; i++) {
		id = g_strdup_printf("%s-%d", action, i);
		cpopup_menu_append_custom(section, id);

		switch (i) {
			case 0:
				if (is_left) {
					if (g_editor->control_sel_l->prev != NULL)
						seg = g_editor->control_sel_l->prev;
					else
						seg = seg_get_last_segment(g_editor->control_sel_l);

					popover_widgets[*n_widgets] =
						cpopup_create_color_item("Left neighbor's right endpoint",
									 action, i,
									 seg->r1, seg->g1, seg->b1, seg->a1);
				} else {
					if (g_editor->control_sel_r->next != NULL)
						seg = g_editor->control_sel_r->next;
					else
						seg = curr_gradient->segments;

					popover_widgets[*n_widgets] =
						cpopup_create_color_item("Right neighbor's left endpoint",
									 action, i,
									 seg->r0, seg->g0, seg->b0, seg->a0);
				} /* else */
				break;

			case 1:
				if (is_left)
					popover_widgets[*n_widgets] =
						cpopup_create_color_item("Right endpoint", action, i,
									 g_editor->control_sel_r->r1,
									 g_editor->control_sel_r->g1,
									 g_editor->control_sel_r->b1,
									 g_editor->control_sel_r->a1);
				else
					popover_widgets[*n_widgets] =
						cpopup_create_color_item("Left endpoint", action, i,
									 g_editor->control_sel_l->r0,
									 g_editor->control_sel_l->g0,
									 g_editor->control_sel_l->b0,
									 g_editor->control_sel_l->a0);
				break;

			default:
				ed_fetch_foreground(&fg_r, &fg_g, &fg_b, &fg_a);
				popover_widgets[*n_widgets] =
					cpopup_create_color_item("FG color", action, i,
								 fg_r, fg_g, fg_b, fg_a);
				break;
		} /* switch */

		g_object_set_data_full(G_OBJECT(popover_widgets[*n_widgets]), "custom-id", id, g_free);
		(*n_widgets)++;
	} /* for */

	g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
	g_object_unref(section);

	/* Saved colors */

	section = g_menu_new();

	for (i = 0; i < GRAD_NUM_COLORS; i++) {
		id = g_strdup_printf("%s-%d", action, i + 3);
		cpopup_menu_append_custom(section, id);

		g_snprintf(str, sizeof(str), "RGBA (%0.3f, %0.3f, %0.3f, %0.3f)",
			   g_editor->saved_colors[i].r,
			   g_editor->saved_colors[i].g,
			   g_editor->saved_colors[i].b,
			   g_editor->saved_colors[i].a);

		popover_widgets[*n_widgets] =
			cpopup_create_color_item(str, action, i + 3,
						 g_editor->saved_colors[i].r,
						 g_editor->saved_colors[i].g,
						 g_editor->saved_colors[i].b,
						 g_editor->saved_colors[i].a);
		g_object_set_data_full(G_OBJECT(popover_widgets[*n_widgets]), "custom-id", id, g_free);
		(*n_widgets)++;
	} /* for */

	g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
	g_object_unref(section);

	return menu;
} /* cpopup_create_load_menu */


/*****/

/* Builds a "Save to" submenu for the left (is_left) or right endpoint */

static GMenu *
cpopup_create_save_menu(GtkWidget *popover_widgets[], int *n_widgets, int is_left)
{
	GMenu      *menu;
	const char *action;
	char       *id;
	char        str[256];
	int         i;

	menu   = g_menu_new();
	action = is_left ? "grad.save-left" : "grad.save-right";

	for (i = 0; i < GRAD_NUM_COLORS; i++) {
		id = g_strdup_printf("%s-%d", action, i);
		cpopup_menu_append_custom(menu, id);

		g_snprintf(str, sizeof(str), "RGBA (%0.3f, %0.3f, %0.3f, %0.3f)",
			   g_editor->saved_colors[i].r,
			   g_editor->saved_colors[i].g,
			   g_editor->saved_colors[i].b,
			   g_editor->saved_colors[i].a);

		popover_widgets[*n_widgets] =
			cpopup_create_color_item(str, action, i,
						 g_editor->saved_colors[i].r,
						 g_editor->saved_colors[i].g,
						 g_editor->saved_colors[i].b,
						 g_editor->saved_colors[i].a);
		g_object_set_data_full(G_OBJECT(popover_widgets[*n_widgets]), "custom-id", id, g_free);
		(*n_widgets)++;
	} /* for */

	return menu;
} /* cpopup_create_save_menu */


/*****/

static void
cpopup_do_popup(GtkWidget *parent, double x, double y)
{
	GtkWidget   *popover_widgets[2 * (GRAD_NUM_COLORS + 3) + 2 * GRAD_NUM_COLORS + 2];
	int          n_widgets;
	GMenu       *menu;
	GMenu       *section;
	GMenu       *submenu;
	GMenu       *sel_ops;
	GtkWidget   *popover;
	GdkRectangle rect;
	int          single;
	int          equal_blending, equal_coloring;
	int          i;

	if (curr_gradient == NULL || g_editor->control_sel_l == NULL)
		return;

	cpopup_adjust_menus();

	single = (g_editor->control_sel_l == g_editor->control_sel_r);
	cpopup_check_selection_params(&equal_blending, &equal_coloring);

	n_widgets = 0;
	menu      = g_menu_new();

	/* Left endpoint */

	section = g_menu_new();

	cpopup_menu_append_custom(section, "left-color");
	popover_widgets[n_widgets] =
		cpopup_create_color_item("Left endpoint's color", "grad.left-color", -1,
					 g_editor->control_sel_l->r0,
					 g_editor->control_sel_l->g0,
					 g_editor->control_sel_l->b0,
					 g_editor->control_sel_l->a0);
	g_object_set_data_full(G_OBJECT(popover_widgets[n_widgets]), "custom-id",
			       g_strdup("left-color"), g_free);
	n_widgets++;

	submenu = cpopup_create_load_menu(popover_widgets, &n_widgets, TRUE);
	g_menu_append_submenu(section, "Load from", G_MENU_MODEL(submenu));
	g_object_unref(submenu);

	submenu = cpopup_create_save_menu(popover_widgets, &n_widgets, TRUE);
	g_menu_append_submenu(section, "Save to", G_MENU_MODEL(submenu));
	g_object_unref(submenu);

	g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
	g_object_unref(section);

	/* Right endpoint */

	section = g_menu_new();

	cpopup_menu_append_custom(section, "right-color");
	popover_widgets[n_widgets] =
		cpopup_create_color_item("Right endpoint's color", "grad.right-color", -1,
					 g_editor->control_sel_r->r1,
					 g_editor->control_sel_r->g1,
					 g_editor->control_sel_r->b1,
					 g_editor->control_sel_r->a1);
	g_object_set_data_full(G_OBJECT(popover_widgets[n_widgets]), "custom-id",
			       g_strdup("right-color"), g_free);
	n_widgets++;

	submenu = cpopup_create_load_menu(popover_widgets, &n_widgets, FALSE);
	g_menu_append_submenu(section, "Load from", G_MENU_MODEL(submenu));
	g_object_unref(submenu);

	submenu = cpopup_create_save_menu(popover_widgets, &n_widgets, FALSE);
	g_menu_append_submenu(section, "Save to", G_MENU_MODEL(submenu));
	g_object_unref(submenu);

	g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
	g_object_unref(section);

	/* Blending function and coloring type */

	section = g_menu_new();

	submenu = g_menu_new();
	for (i = 0; i < GRAD_NUM_BLENDING_TYPES; i++)
		cpopup_menu_append_radio(submenu, blending_types[i], "grad.blending", i);
	if (!equal_blending)
		cpopup_menu_append_radio(submenu, "(Varies)", "grad.blending", -1);
	g_menu_append_submenu(section,
			      single ?
			      "Blending function for segment" :
			      "Blending function for selection",
			      G_MENU_MODEL(submenu));
	g_object_unref(submenu);

	submenu = g_menu_new();
	for (i = 0; i < GRAD_NUM_COLORING_TYPES; i++)
		cpopup_menu_append_radio(submenu, coloring_types[i], "grad.coloring", i);
	if (!equal_coloring)
		cpopup_menu_append_radio(submenu, "(Varies)", "grad.coloring", -1);
	g_menu_append_submenu(section,
			      single ?
			      "Coloring type for segment" :
			      "Coloring type for selection",
			      G_MENU_MODEL(submenu));
	g_object_unref(submenu);

	g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
	g_object_unref(section);

	/* Operations */

	section = g_menu_new();

	cpopup_menu_append(section,
			   single ? "Split segment at midpoint" : "Split segments at midpoints",
			   "grad.split-midpoint", "s");
	cpopup_menu_append(section,
			   single ? "Split segment uniformly" : "Split segments uniformly",
			   "grad.split-uniform", "u");
	cpopup_menu_append(section,
			   single ? "Delete segment" : "Delete selection",
			   "grad.delete", "d");
	cpopup_menu_append(section,
			   single ? "Re-center segment's midpoint" : "Re-center midpoints in selection",
			   "grad.recenter", "c");
	cpopup_menu_append(section,
			   single ? "Re-distribute handles in segment" : "Re-distribute handles in selection",
			   "grad.redistribute", "<Control>c");

	g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
	g_object_unref(section);

	/* Selection ops */

	section = g_menu_new();
	sel_ops = g_menu_new();

	submenu = g_menu_new();
	cpopup_menu_append(submenu, single ? "Flip segment" : "Flip selection",
			   "grad.flip", "f");
	cpopup_menu_append(submenu, single ? "Replicate segment" : "Replicate selection",
			   "grad.replicate", "m");
	g_menu_append_section(sel_ops, NULL, G_MENU_MODEL(submenu));
	g_object_unref(submenu);

	submenu = g_menu_new();
	cpopup_menu_append(submenu, "Blend endpoints' colors", "grad.blend-colors", "b");
	cpopup_menu_append(submenu, "Blend endpoints' opacity", "grad.blend-opacity", "<Control>b");
	g_menu_append_section(sel_ops, NULL, G_MENU_MODEL(submenu));
	g_object_unref(submenu);

	g_menu_append_submenu(section, "Selection operations", G_MENU_MODEL(sel_ops));
	g_object_unref(sel_ops);

	g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
	g_object_unref(section);

	/* Replace the previous pop-up */

	if (g_editor->control_main_popup) {
		gtk_widget_unparent(g_editor->control_main_popup);
		g_editor->control_main_popup = NULL;
	} /* if */

	popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
	g_object_unref(menu);

	gtk_widget_set_parent(popover, parent);
	gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
	gtk_widget_set_halign(popover, GTK_ALIGN_START);

	for (i = 0; i < n_widgets; i++)
		gtk_popover_menu_add_child(GTK_POPOVER_MENU(popover), popover_widgets[i],
					   g_object_get_data(G_OBJECT(popover_widgets[i]), "custom-id"));

	rect.x      = (int) x;
	rect.y      = (int) y;
	rect.width  = 1;
	rect.height = 1;
	gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);

	g_editor->control_main_popup = popover;

	gtk_popover_popup(GTK_POPOVER(popover));
} /* cpopup_do_popup */


/*****/

static void
cpopup_set_action_enabled(const char *name, gboolean enabled)
{
	GAction *action;

	action = g_action_map_lookup_action(G_ACTION_MAP(g_editor->actions), name);
	if (action)
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action), enabled);
} /* cpopup_set_action_enabled */


/*****/

static void
cpopup_adjust_menus(void)
{
	GAction *action;
	int      equal_blending, equal_coloring;

	if (g_editor == NULL || g_editor->actions == NULL ||
	    g_editor->control_sel_l == NULL || g_editor->control_sel_r == NULL)
		return;

	/* Adjust blending and coloring menus */

	cpopup_check_selection_params(&equal_blending, &equal_coloring);

	action = g_action_map_lookup_action(G_ACTION_MAP(g_editor->actions), "blending");
	g_simple_action_set_state(G_SIMPLE_ACTION(action),
				  g_variant_new_int32(equal_blending ?
						      (int) g_editor->control_sel_l->type : -1));

	action = g_action_map_lookup_action(G_ACTION_MAP(g_editor->actions), "coloring");
	g_simple_action_set_state(G_SIMPLE_ACTION(action),
				  g_variant_new_int32(equal_coloring ?
						      (int) g_editor->control_sel_l->color : -1));

	/* Can invoke delete? */

	if ((g_editor->control_sel_l->prev == NULL) && (g_editor->control_sel_r->next == NULL))
		cpopup_set_action_enabled("delete", FALSE);
	else
		cpopup_set_action_enabled("delete", TRUE);

	/* Can invoke blend colors / opacity? */

	if (g_editor->control_sel_l == g_editor->control_sel_r) {
		cpopup_set_action_enabled("blend-colors", FALSE);
		cpopup_set_action_enabled("blend-opacity", FALSE);
	} else {
		cpopup_set_action_enabled("blend-colors", TRUE);
		cpopup_set_action_enabled("blend-opacity", TRUE);
	} /* else */
} /* cpopup_adjust_menus */


/*****/

static void
cpopup_check_selection_params(int *equal_blending, int *equal_coloring)
{
	grad_type_t     type;
	grad_color_t    color;
	int             etype, ecolor;
	grad_segment_t *seg, *aseg;

	type  = g_editor->control_sel_l->type;
	color = g_editor->control_sel_l->color;

	etype  = 1;
	ecolor = 1;

	seg = g_editor->control_sel_l;

	do {
		etype  = etype && (seg->type == type);
		ecolor = ecolor && (seg->color == color);

		aseg = seg;
		seg  = seg->next;
	} while (aseg != g_editor->control_sel_r);

	if (equal_blending)
		*equal_blending = etype;

	if (equal_coloring)
		*equal_coloring = ecolor;
} /* cpopup_check_selection_params */


/*****/

static void
cpopup_update_saved_color(int n, double r, double g, double b, double a)
{
	/* The pop-up is rebuilt from saved_colors every time it is shown */

	g_editor->saved_colors[n].r = r;
	g_editor->saved_colors[n].g = g;
	g_editor->saved_colors[n].b = b;
	g_editor->saved_colors[n].a = a;
} /* cpopup_update_saved_color */


/*****/

static void
cpopup_load_left_callback(GtkWidget *widget, gpointer data)
{
	grad_segment_t *seg;
	double          fg_r, fg_g, fg_b;
	double          fg_a;
	int             n = GPOINTER_TO_INT(data);

	switch (n) {
		case 0: /* Fetch from left neighbor's right endpoint */
			if (g_editor->control_sel_l->prev != NULL)
				seg = g_editor->control_sel_l->prev;
			else
				seg = seg_get_last_segment(g_editor->control_sel_l);

			cpopup_blend_endpoints(seg->r1, seg->g1, seg->b1, seg->a1,
					       g_editor->control_sel_r->r1,
					       g_editor->control_sel_r->g1,
					       g_editor->control_sel_r->b1,
					       g_editor->control_sel_r->a1,
					       TRUE, TRUE);
			break;

		case 1: /* Fetch from right endpoint */
			cpopup_blend_endpoints(g_editor->control_sel_r->r1,
					       g_editor->control_sel_r->g1,
					       g_editor->control_sel_r->b1,
					       g_editor->control_sel_r->a1,
					       g_editor->control_sel_r->r1,
					       g_editor->control_sel_r->g1,
					       g_editor->control_sel_r->b1,
					       g_editor->control_sel_r->a1,
					       TRUE, TRUE);
			break;

		case 2: /* Fetch from FG color */
	                ed_fetch_foreground(&fg_r, &fg_g, &fg_b, &fg_a);
			cpopup_blend_endpoints(fg_r,
					       fg_g,
					       fg_b,
					       fg_a,
					       g_editor->control_sel_r->r1,
					       g_editor->control_sel_r->g1,
					       g_editor->control_sel_r->b1,
					       g_editor->control_sel_r->a1,
					       TRUE, TRUE);
			break;

		default: /* Load a color */
			if (n < 3 || n >= GRAD_NUM_COLORS + 3)
				return;

			cpopup_blend_endpoints(g_editor->saved_colors[n - 3].r,
					       g_editor->saved_colors[n - 3].g,
					       g_editor->saved_colors[n - 3].b,
					       g_editor->saved_colors[n - 3].a,
					       g_editor->control_sel_r->r1,
					       g_editor->control_sel_r->g1,
					       g_editor->control_sel_r->b1,
					       g_editor->control_sel_r->a1,
					       TRUE, TRUE);
			break;
	} /* switch */

	curr_gradient->dirty = 1;
	ed_update_editor(GRAD_UPDATE_PREVIEW);
} /* cpopup_load_left_callback */


/*****/

static void
cpopup_save_left_callback(GtkWidget *widget, gpointer data)
{
	int n = GPOINTER_TO_INT(data);

	if (n < 0 || n >= GRAD_NUM_COLORS)
		return;

	cpopup_update_saved_color(n,
				  g_editor->control_sel_l->r0,
				  g_editor->control_sel_l->g0,
				  g_editor->control_sel_l->b0,
				  g_editor->control_sel_l->a0);
} /* cpopup_save_left_callback */


/*****/

static void
cpopup_load_right_callback(GtkWidget *widget, gpointer data)
{
	grad_segment_t *seg;
	double          fg_r, fg_g, fg_b;
	double          fg_a;
	int             n = GPOINTER_TO_INT(data);

	switch (n) {
		case 0: /* Fetch from right neighbor's left endpoint */
			if (g_editor->control_sel_r->next != NULL)
				seg = g_editor->control_sel_r->next;
			else
				seg = curr_gradient->segments;

			cpopup_blend_endpoints(g_editor->control_sel_r->r0,
					       g_editor->control_sel_r->g0,
					       g_editor->control_sel_r->b0,
					       g_editor->control_sel_r->a0,
					       seg->r0, seg->g0, seg->b0, seg->a0,
					       TRUE, TRUE);
			break;

		case 1: /* Fetch from left endpoint */
			cpopup_blend_endpoints(g_editor->control_sel_l->r0,
					       g_editor->control_sel_l->g0,
					       g_editor->control_sel_l->b0,
					       g_editor->control_sel_l->a0,
					       g_editor->control_sel_l->r0,
					       g_editor->control_sel_l->g0,
					       g_editor->control_sel_l->b0,
					       g_editor->control_sel_l->a0,
					       TRUE, TRUE);
			break;

		case 2: /* Fetch from FG color */
	                ed_fetch_foreground(&fg_r, &fg_g, &fg_b, &fg_a);
			cpopup_blend_endpoints(g_editor->control_sel_l->r0,
					       g_editor->control_sel_l->g0,
					       g_editor->control_sel_l->b0,
					       g_editor->control_sel_l->a0,
					       fg_r,
					       fg_g,
					       fg_b,
					       fg_a,
					       TRUE, TRUE);
			break;

		default: /* Load a color */
			if (n < 3 || n >= GRAD_NUM_COLORS + 3)
				return;

			cpopup_blend_endpoints(g_editor->control_sel_l->r0,
					       g_editor->control_sel_l->g0,
					       g_editor->control_sel_l->b0,
					       g_editor->control_sel_l->a0,
					       g_editor->saved_colors[n - 3].r,
					       g_editor->saved_colors[n - 3].g,
					       g_editor->saved_colors[n - 3].b,
					       g_editor->saved_colors[n - 3].a,
					       TRUE, TRUE);
			break;
	} /* switch */

	curr_gradient->dirty = 1;
	ed_update_editor(GRAD_UPDATE_PREVIEW);
} /* cpopup_load_right_callback */


/*****/

static void
cpopup_save_right_callback(GtkWidget *widget, gpointer data)
{
	int n = GPOINTER_TO_INT(data);

	if (n < 0 || n >= GRAD_NUM_COLORS)
		return;

	cpopup_update_saved_color(n,
				  g_editor->control_sel_r->r1,
				  g_editor->control_sel_r->g1,
				  g_editor->control_sel_r->b1,
				  g_editor->control_sel_r->a1);
} /* cpopup_save_right_callback */


/*****/

static void
cpopup_blending_callback(GtkWidget *widget, gpointer data)
{
	grad_type_t     type;
	grad_segment_t *seg, *aseg;
	int             n = GPOINTER_TO_INT(data);

	if (n < 0 || n >= GRAD_NUM_BLENDING_TYPES)
		return; /* "(Varies)" does nothing */

	type = (grad_type_t) n;
	seg  = g_editor->control_sel_l;

	do {
		seg->type = type;

		aseg = seg;
		seg  = seg->next;
	} while (aseg != g_editor->control_sel_r);

	curr_gradient->dirty = 1;
	ed_update_editor(GRAD_UPDATE_PREVIEW);
	cpopup_adjust_menus();
} /* cpopup_blending_callback */


/*****/

static void
cpopup_coloring_callback(GtkWidget *widget, gpointer data)
{
	grad_color_t    color;
	grad_segment_t *seg, *aseg;
	int             n = GPOINTER_TO_INT(data);

	if (n < 0 || n >= GRAD_NUM_COLORING_TYPES)
		return; /* "(Varies)" does nothing */

	color = (grad_color_t) n;
	seg   = g_editor->control_sel_l;

	do {
		seg->color = color;

		aseg = seg;
		seg  = seg->next;
	} while (aseg != g_editor->control_sel_r);

	curr_gradient->dirty = 1;
	ed_update_editor(GRAD_UPDATE_PREVIEW);
	cpopup_adjust_menus();
} /* cpopup_coloring_callback */


/*****/

static void
cpopup_blend_colors(GtkWidget *widget, gpointer data)
{
	cpopup_blend_endpoints(g_editor->control_sel_l->r0,
			       g_editor->control_sel_l->g0,
			       g_editor->control_sel_l->b0,
			       g_editor->control_sel_l->a0,
			       g_editor->control_sel_r->r1,
			       g_editor->control_sel_r->g1,
			       g_editor->control_sel_r->b1,
			       g_editor->control_sel_r->a1,
			       TRUE, FALSE);

	curr_gradient->dirty = 1;
	ed_update_editor(GRAD_UPDATE_PREVIEW);
} /* cpopup_blend_colors */


/*****/

static void
cpopup_blend_opacity(GtkWidget *widget, gpointer data)
{
	cpopup_blend_endpoints(g_editor->control_sel_l->r0,
			       g_editor->control_sel_l->g0,
			       g_editor->control_sel_l->b0,
			       g_editor->control_sel_l->a0,
			       g_editor->control_sel_r->r1,
			       g_editor->control_sel_r->g1,
			       g_editor->control_sel_r->b1,
			       g_editor->control_sel_r->a1,
			       FALSE, TRUE);

	curr_gradient->dirty = 1;
	ed_update_editor(GRAD_UPDATE_PREVIEW);
} /* cpopup_blend_opacity */


/*****/

/* The endpoint color dialogs: a swatch over red, green, blue and
 * opacity sliders (0..1), with OK and Cancel.  Every change is
 * reported to the "color changed" callback right away, as GTK 1's
 * color selection did.
 */

static const char *cpopup_color_channel_names[4] = {
	"Red", "Green", "Blue", "Opacity"
};

static void
cpopup_get_color_dialog_color(GtkWidget *dialog,
			      double *r, double *g, double *b, double *a)
{
	GtkAdjustment **adj;

	adj = g_object_get_data(G_OBJECT(dialog), "channels");

	*r = gtk_adjustment_get_value(adj[0]);
	*g = gtk_adjustment_get_value(adj[1]);
	*b = gtk_adjustment_get_value(adj[2]);
	*a = gtk_adjustment_get_value(adj[3]);
} /* cpopup_get_color_dialog_color */


/*****/

static void
cpopup_color_dialog_swatch_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
	GtkWidget *dialog = data;
	double     r, g, b, a;
	int        x, y;
	double     c;

	cpopup_get_color_dialog_color(dialog, &r, &g, &b, &a);

	for (y = 0; y < height; y += GRAD_CHECK_SIZE)
		for (x = 0; x < width; x += GRAD_CHECK_SIZE) {
			c = (((x / GRAD_CHECK_SIZE) ^ (y / GRAD_CHECK_SIZE)) & 1) ?
				GRAD_CHECK_LIGHT : GRAD_CHECK_DARK;

			cairo_set_source_rgb(cr, c + (r - c) * a, c + (g - c) * a, c + (b - c) * a);
			cairo_rectangle(cr, x, y, GRAD_CHECK_SIZE, GRAD_CHECK_SIZE);
			cairo_fill(cr);
		} /* for */
} /* cpopup_color_dialog_swatch_draw */


/*****/

static void
cpopup_color_dialog_value_changed(GtkAdjustment *adjustment, gpointer data)
{
	GtkWidget *dialog = data;
	GCallback  changed;

	gtk_widget_queue_draw(GTK_WIDGET(g_object_get_data(G_OBJECT(dialog), "swatch")));

	changed = (GCallback) g_object_get_data(G_OBJECT(dialog), "color-changed");
	if (changed)
		((CpopupCallback) changed) (NULL, dialog);
} /* cpopup_color_dialog_value_changed */


/*****/

static void
cpopup_create_color_dialog(char *title, double r, double g, double b, double a,
			   GCallback color_changed_callback,
			   GCallback ok_callback,
			   GCallback cancel_callback,
			   GCallback delete_callback)
{
	GtkWidget      *window;
	GtkWidget      *vbox;
	GtkWidget      *frame;
	GtkWidget      *swatch;
	GtkWidget      *table;
	GtkWidget      *label;
	GtkWidget      *scale;
	GtkAdjustment **adj;
	double          values[4];
	int             i;

	window = gimp_dialog_new(title);
	gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(g_editor->shell));
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

	vbox = gimp_vbox_new(FALSE, 4);
	gimp_container_set_border_width(vbox, 8);
	gimp_box_pack_start(gimp_dialog_get_vbox(window), vbox, TRUE, TRUE, 0);

	/* The color */

	frame = gtk_frame_new(NULL);
	gimp_box_pack_start(vbox, frame, FALSE, FALSE, 0);

	swatch = gtk_drawing_area_new();
	gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(swatch), 240);
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(swatch), 48);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(swatch),
				       cpopup_color_dialog_swatch_draw, window, NULL);
	gtk_frame_set_child(GTK_FRAME(frame), swatch);
	g_object_set_data(G_OBJECT(window), "swatch", swatch);

	/* The channels */

	values[0] = r;
	values[1] = g;
	values[2] = b;
	values[3] = a;

	adj = g_new(GtkAdjustment *, 4);
	g_object_set_data_full(G_OBJECT(window), "channels", adj, g_free);

	table = gimp_table_new(4, 2, FALSE);
	gtk_grid_set_column_spacing(GTK_GRID(table), 4);
	gimp_box_pack_start(vbox, table, TRUE, TRUE, 0);

	for (i = 0; i < 4; i++) {
		label = gtk_label_new(cpopup_color_channel_names[i]);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gimp_table_attach(table, label, 0, 1, i, i + 1, GIMP_FILL, GIMP_FILL, 0, 0);

		adj[i] = gtk_adjustment_new(BOUNDS(values[i], 0.0, 1.0), 0.0, 1.0, 0.01, 0.1, 0.0);

		scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj[i]);
		gtk_scale_set_digits(GTK_SCALE(scale), 3);
		gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
		gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_RIGHT);
		gtk_widget_set_size_request(scale, 200, -1);
		gimp_table_attach(table, scale, 1, 2, i, i + 1,
				  GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 0, 0);
	} /* for */

	g_object_set_data(G_OBJECT(window), "color-changed", (gpointer) color_changed_callback);

	for (i = 0; i < 4; i++)
		g_signal_connect(adj[i], "value-changed",
				 G_CALLBACK(cpopup_color_dialog_value_changed), window);

	/* Buttons and closing */

	gimp_dialog_add_button(window, "OK", ok_callback, window, TRUE);
	gimp_dialog_add_button(window, "Cancel", cancel_callback, window, FALSE);

	g_signal_connect(window, "close-request", delete_callback, NULL);

	gtk_window_present(GTK_WINDOW(window));
} /* cpopup_create_color_dialog */


/*****/

static grad_segment_t *
cpopup_save_selection(void)
{
	grad_segment_t *seg, *prev, *tmp;
	grad_segment_t *oseg, *oaseg;

	prev = NULL;
	oseg = g_editor->control_sel_l;
	tmp  = NULL;

	do {
		seg = seg_new_segment();

		*seg = *oseg; /* Copy everything */

		if (prev == NULL)
			tmp = seg; /* Remember first segment */
		else
			prev->next = seg;

		seg->prev = prev;
		seg->next = NULL;

		prev  = seg;
		oaseg = oseg;
		oseg  = oseg->next;
	} while (oaseg != g_editor->control_sel_r);

	return tmp;
} /* cpopup_save_selection */


/*****/

static void
cpopup_free_selection(grad_segment_t *seg)
{
	seg_free_segments(seg);
} /* cpopup_free_selection */


/*****/

static void
cpopup_replace_selection(grad_segment_t *replace_seg)
{
	grad_segment_t *lseg, *rseg;
	grad_segment_t *replace_last;

	/* Remember left and right segments */

	lseg = g_editor->control_sel_l->prev;
	rseg = g_editor->control_sel_r->next;

	replace_last = seg_get_last_segment(replace_seg);

	/* Free old selection */

	g_editor->control_sel_r->next = NULL;

	seg_free_segments(g_editor->control_sel_l);

	/* Link in new segments */

	if (lseg)
		lseg->next = replace_seg;
	else
		curr_gradient->segments = replace_seg;

	replace_seg->prev = lseg;

	if (rseg)
		rseg->prev = replace_last;

	replace_last->next = rseg;

	g_editor->control_sel_l = replace_seg;
	g_editor->control_sel_r = replace_last;

	curr_gradient->last_visited = NULL; /* Force re-search */
} /* cpopup_replace_selection */


/*****/

static void
cpopup_set_left_color_callback(GtkWidget *widget, gpointer data)
{
	g_editor->left_saved_dirty    = curr_gradient->dirty;
	g_editor->left_saved_segments = cpopup_save_selection();

	cpopup_create_color_dialog("Left endpoint color",
				   g_editor->control_sel_l->r0,
				   g_editor->control_sel_l->g0,
				   g_editor->control_sel_l->b0,
				   g_editor->control_sel_l->a0,
				   G_CALLBACK(cpopup_left_color_changed),
				   G_CALLBACK(cpopup_left_color_dialog_ok),
				   G_CALLBACK(cpopup_left_color_dialog_cancel),
				   G_CALLBACK(cpopup_left_color_dialog_delete));

	gtk_widget_set_sensitive(g_editor->shell, FALSE);
} /* cpopup_set_left_color_callback */


/*****/

static void
cpopup_left_color_changed(GtkWidget *widget, gpointer client_data)
{
	double r, g, b, a;

	cpopup_get_color_dialog_color(GTK_WIDGET(client_data), &r, &g, &b, &a);

	cpopup_blend_endpoints(r, g, b, a,
			       g_editor->control_sel_r->r1,
			       g_editor->control_sel_r->g1,
			       g_editor->control_sel_r->b1,
			       g_editor->control_sel_r->a1,
			       TRUE, TRUE);

	ed_update_editor(GRAD_UPDATE_PREVIEW);
} /* cpopup_left_color_changed */


/*****/

static void
cpopup_left_color_dialog_ok(GtkWidget *widget, gpointer client_data)
{
	double r, g, b, a;

	cpopup_get_color_dialog_color(GTK_WIDGET(client_data), &r, &g, &b, &a);

	cpopup_blend_endpoints(r, g, b, a,
			       g_editor->control_sel_r->r1,
			       g_editor->control_sel_r->g1,
			       g_editor->control_sel_r->b1,
			       g_editor->control_sel_r->a1,
			       TRUE, TRUE);

	curr_gradient->dirty = 1;
	cpopup_free_selection(g_editor->left_saved_segments);
	g_editor->left_saved_segments = NULL;
	ed_update_editor(GRAD_UPDATE_PREVIEW);

	g_signal_handlers_disconnect_by_func(client_data,
					     G_CALLBACK(cpopup_left_color_dialog_delete), NULL);
	gtk_window_destroy(GTK_WINDOW(client_data));
	gtk_widget_set_sensitive(g_editor->shell, TRUE);
} /* cpopup_left_color_dialog_ok */


/*****/

static void
cpopup_left_color_dialog_cancel(GtkWidget *widget, gpointer client_data)
{
	curr_gradient->dirty = g_editor->left_saved_dirty;
	cpopup_replace_selection(g_editor->left_saved_segments);
	g_editor->left_saved_segments = NULL;
	ed_update_editor(GRAD_UPDATE_PREVIEW);

	g_signal_handlers_disconnect_by_func(client_data,
					     G_CALLBACK(cpopup_left_color_dialog_delete), NULL);
	gtk_window_destroy(GTK_WINDOW(client_data));
	gtk_widget_set_sensitive(g_editor->shell, TRUE);
} /* cpopup_left_color_dialog_cancel */


/*****/

static gboolean
cpopup_left_color_dialog_delete(GtkWindow *window, gpointer data)
{
	curr_gradient->dirty = g_editor->left_saved_dirty;
	cpopup_replace_selection(g_editor->left_saved_segments);
	g_editor->left_saved_segments = NULL;
	ed_update_editor(GRAD_UPDATE_PREVIEW);

	gtk_widget_set_sensitive(g_editor->shell, TRUE);
	return FALSE;
} /* cpopup_left_color_dialog_delete */

/*****/

static void
cpopup_set_right_color_callback(GtkWidget *widget, gpointer data)
{
	g_editor->right_saved_dirty    = curr_gradient->dirty;
	g_editor->right_saved_segments = cpopup_save_selection();

	cpopup_create_color_dialog("Right endpoint color",
				   g_editor->control_sel_r->r1,
				   g_editor->control_sel_r->g1,
				   g_editor->control_sel_r->b1,
				   g_editor->control_sel_r->a1,
				   G_CALLBACK(cpopup_right_color_changed),
				   G_CALLBACK(cpopup_right_color_dialog_ok),
				   G_CALLBACK(cpopup_right_color_dialog_cancel),
				   G_CALLBACK(cpopup_right_color_dialog_delete));

	gtk_widget_set_sensitive(g_editor->shell, FALSE);
} /* cpopup_set_right_color_callback */


/*****/

static void
cpopup_right_color_changed(GtkWidget *widget, gpointer client_data)
{
	double r, g, b, a;

	cpopup_get_color_dialog_color(GTK_WIDGET(client_data), &r, &g, &b, &a);

	cpopup_blend_endpoints(g_editor->control_sel_l->r0,
			       g_editor->control_sel_l->g0,
			       g_editor->control_sel_l->b0,
			       g_editor->control_sel_l->a0,
			       r, g, b, a,
			       TRUE, TRUE);

	ed_update_editor(GRAD_UPDATE_PREVIEW);
} /* cpopup_right_color_changed */


/*****/

static void
cpopup_right_color_dialog_ok(GtkWidget *widget, gpointer client_data)
{
	double r, g, b, a;

	cpopup_get_color_dialog_color(GTK_WIDGET(client_data), &r, &g, &b, &a);

	cpopup_blend_endpoints(g_editor->control_sel_l->r0,
			       g_editor->control_sel_l->g0,
			       g_editor->control_sel_l->b0,
			       g_editor->control_sel_l->a0,
			       r, g, b, a,
			       TRUE, TRUE);

	curr_gradient->dirty = 1;
	cpopup_free_selection(g_editor->right_saved_segments);
	g_editor->right_saved_segments = NULL;
	ed_update_editor(GRAD_UPDATE_PREVIEW);

	g_signal_handlers_disconnect_by_func(client_data,
					     G_CALLBACK(cpopup_right_color_dialog_delete), NULL);
	gtk_window_destroy(GTK_WINDOW(client_data));
	gtk_widget_set_sensitive(g_editor->shell, TRUE);
} /* cpopup_right_color_dialog_ok */


/*****/

static void
cpopup_right_color_dialog_cancel(GtkWidget *widget, gpointer client_data)
{
	curr_gradient->dirty = g_editor->right_saved_dirty;
	cpopup_replace_selection(g_editor->right_saved_segments);
	g_editor->right_saved_segments = NULL;
	ed_update_editor(GRAD_UPDATE_PREVIEW);

	g_signal_handlers_disconnect_by_func(client_data,
					     G_CALLBACK(cpopup_right_color_dialog_delete), NULL);
	gtk_window_destroy(GTK_WINDOW(client_data));
	gtk_widget_set_sensitive(g_editor->shell, TRUE);
} /* cpopup_right_color_dialog_cancel */


/*****/

static gboolean
cpopup_right_color_dialog_delete(GtkWindow *window, gpointer data)
{
	curr_gradient->dirty = g_editor->right_saved_dirty;
	cpopup_replace_selection(g_editor->right_saved_segments);
	g_editor->right_saved_segments = NULL;
	ed_update_editor(GRAD_UPDATE_PREVIEW);

	gtk_widget_set_sensitive(g_editor->shell, TRUE);
	return FALSE;
} /* cpopup_right_color_dialog_delete */


/*****/

static void
cpopup_split_midpoint_callback(GtkWidget *widget, gpointer data)
{
	grad_segment_t *seg, *lseg, *rseg;

	seg = g_editor->control_sel_l;

	do {
		cpopup_split_midpoint(seg, &lseg, &rseg);
		seg = rseg->next;
	} while (lseg != g_editor->control_sel_r);

	g_editor->control_sel_r = rseg;

	curr_gradient->last_visited = NULL; /* Force re-search */
	curr_gradient->dirty = 1;
	ed_update_editor(GRAD_UPDATE_PREVIEW | GRAD_UPDATE_CONTROL);
} /* cpopup_split_midpoint_callback */


/*****/

static void
cpopup_split_uniform_callback(GtkWidget *widget, gpointer data)
{
	GtkWidget     *dialog;
	GtkWidget     *vbox;
	GtkWidget     *label;
	GtkWidget     *scale;
	GtkAdjustment *scale_data;

	/* Create dialog window */

	dialog = gimp_dialog_new((g_editor->control_sel_l == g_editor->control_sel_r) ?
				 "Split segment uniformly" :
				 "Split segments uniformly");
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(g_editor->shell));
	g_signal_connect(dialog, "close-request",
			 G_CALLBACK(cpopup_dialog_close_request), NULL);

	vbox = gimp_vbox_new(FALSE, 0);
	gimp_container_set_border_width(vbox, 8);
	gimp_box_pack_start(gimp_dialog_get_vbox(dialog), vbox, FALSE, FALSE, 0);

	/* Instructions */

	label = gtk_label_new("Please select the number of uniform parts");
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label), 0.0);
	gimp_box_pack_start(vbox, label, FALSE, FALSE, 0);

	label = gtk_label_new((g_editor->control_sel_l == g_editor->control_sel_r) ?
			      "in which you want to split the selected segment" :
			      "in which you want to split the segments in the selection");
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label), 0.0);
	gimp_box_pack_start(vbox, label, FALSE, FALSE, 0);

	/* Scale */

	g_editor->split_parts = 2;
	scale_data = gtk_adjustment_new(2.0, 2.0, 21.0, 1.0, 1.0, 1.0);

	scale = gimp_hscale_new(scale_data, 0);
	gimp_box_pack_start(vbox, scale, FALSE, TRUE, 8);

	g_signal_connect(scale_data, "value-changed",
			 G_CALLBACK(cpopup_split_uniform_scale_update),
			 NULL);

	/* Buttons */

	gimp_dialog_add_button(dialog, "Split",
			       G_CALLBACK(cpopup_split_uniform_split_callback),
			       dialog, TRUE);
	gimp_dialog_add_button(dialog, "Cancel",
			       G_CALLBACK(cpopup_split_uniform_cancel_callback),
			       dialog, FALSE);

	/* Show! */

	gtk_window_present(GTK_WINDOW(dialog));
	gtk_widget_set_sensitive(g_editor->shell, FALSE);
} /* cpopup_split_uniform_callback */


/*****/

static void
cpopup_delete_callback(GtkWidget *widget, gpointer data)
{
	grad_segment_t *lseg, *rseg, *seg, *aseg, *next;
	double          join;

	/* Remember segments to the left and to the right of the selection */

	lseg = g_editor->control_sel_l->prev;
	rseg = g_editor->control_sel_r->next;

	/* Cannot delete all the segments in the gradient */

	if ((lseg == NULL) && (rseg == NULL))
		return;

	/* Calculate join point */

	join = (g_editor->control_sel_l->left + g_editor->control_sel_r->right) / 2.0;

	if (lseg == NULL)
		join = 0.0;
	else if (rseg == NULL)
		join = 1.0;

	/* Move segments */

	if (lseg != NULL)
		control_compress_range(lseg, lseg, lseg->left, join);

	if (rseg != NULL)
		control_compress_range(rseg, rseg, join, rseg->right);

	/* Link */

	if (lseg)
		lseg->next = rseg;

	if (rseg)
		rseg->prev = lseg;

	/* Delete old segments */

	seg = g_editor->control_sel_l;

	do {
		next = seg->next;
		aseg = seg;

		seg_free_segment(seg);

		seg = next;
	} while (aseg != g_editor->control_sel_r);

	/* Change selection */

	if (rseg) {
		g_editor->control_sel_l = rseg;
		g_editor->control_sel_r = rseg;
	} else {
		g_editor->control_sel_l = lseg;
		g_editor->control_sel_r = lseg;
	} /* else */

	if (lseg == NULL)
		curr_gradient->segments = rseg;

	/* Done */

	curr_gradient->last_visited = NULL; /* Force re-search */
	curr_gradient->dirty = 1;

	ed_update_editor(GRAD_UPDATE_PREVIEW | GRAD_UPDATE_CONTROL);
} /* cpopup_delete_callback */


/*****/

static void
cpopup_recenter_callback(GtkWidget *wiodget, gpointer data)
{
	grad_segment_t *seg, *aseg;

	seg = g_editor->control_sel_l;

	do {
		seg->middle = (seg->left + seg->right) / 2.0;

		aseg = seg;
		seg  = seg->next;
	} while (aseg != g_editor->control_sel_r);

	curr_gradient->dirty = 1;
	ed_update_editor(GRAD_UPDATE_PREVIEW | GRAD_UPDATE_CONTROL);
} /* cpopup_recenter_callback */


/*****/

static void
cpopup_redistribute_callback(GtkWidget *widget, gpointer data)
{
	grad_segment_t *seg, *aseg;
	double          left, right, seg_len;
	int             num_segs;
	int             i;

	/* Count number of segments in selection */

	num_segs = 0;
	seg      = g_editor->control_sel_l;

	do {
		num_segs++;
		aseg = seg;
		seg  = seg->next;
	} while (aseg != g_editor->control_sel_r);

	/* Calculate new segment length */

	left    = g_editor->control_sel_l->left;
	right   = g_editor->control_sel_r->right;
	seg_len = (right - left) / num_segs;

	/* Redistribute */

	seg = g_editor->control_sel_l;

	for (i = 0; i < num_segs; i++) {
		seg->left   = left + i * seg_len;
		seg->right  = left + (i + 1) * seg_len;
		seg->middle = (seg->left + seg->right) / 2.0;

		seg = seg->next;
	} /* for */

	/* Fix endpoints to squish accumulative error */

	g_editor->control_sel_l->left  = left;
	g_editor->control_sel_r->right = right;

	/* Done */

	curr_gradient->dirty = 1;
	ed_update_editor(GRAD_UPDATE_PREVIEW | GRAD_UPDATE_CONTROL);
} /* cpopup_redistribute_callback */


/*****/

static void
cpopup_flip_callback(GtkWidget *widget, gpointer data)
{
	grad_segment_t *oseg, *oaseg;
	grad_segment_t *seg, *prev, *tmp;
	grad_segment_t *lseg, *rseg;
	double          left, right;

	left  = g_editor->control_sel_l->left;
	right = g_editor->control_sel_r->right;

	/* Build flipped segments */

	prev = NULL;
	oseg = g_editor->control_sel_r;
	tmp  = NULL;

	do {
		seg = seg_new_segment();

		if (prev == NULL) {
			seg->left = left;
			tmp = seg; /* Remember first segment */
		} else
			seg->left = left + right - oseg->right;

		seg->middle = left + right - oseg->middle;
		seg->right  = left + right - oseg->left;

		seg->r0 = oseg->r1;
		seg->g0 = oseg->g1;
		seg->b0 = oseg->b1;
		seg->a0 = oseg->a1;

		seg->r1 = oseg->r0;
		seg->g1 = oseg->g0;
		seg->b1 = oseg->b0;
		seg->a1 = oseg->a0;

		switch (oseg->type) {
			case GRAD_SPHERE_INCREASING:
				seg->type = GRAD_SPHERE_DECREASING;
				break;

			case GRAD_SPHERE_DECREASING:
				seg->type = GRAD_SPHERE_INCREASING;
				break;

			default:
				seg->type = oseg->type;
		} /* switch */

		switch (oseg->color) {
			case GRAD_HSV_CCW:
				seg->color = GRAD_HSV_CW;
				break;

			case GRAD_HSV_CW:
				seg->color = GRAD_HSV_CCW;
				break;

			default:
				seg->color = oseg->color;
		} /* switch */

		seg->prev = prev;
		seg->next = NULL;

		if (prev)
			prev->next = seg;

		prev = seg;

		oaseg = oseg;
		oseg  = oseg->prev; /* Move backwards! */
	} while (oaseg != g_editor->control_sel_l);

	seg->right = right; /* Squish accumulative error */

	/* Free old segments */

	lseg = g_editor->control_sel_l->prev;
	rseg = g_editor->control_sel_r->next;

	oseg = g_editor->control_sel_l;

	do {
		oaseg = oseg->next;
		seg_free_segment(oseg);
		oseg = oaseg;
	} while (oaseg != rseg);

	/* Link in new segments */

	if (lseg)
		lseg->next = tmp;
	else
		curr_gradient->segments = tmp;

	tmp->prev = lseg;

	seg->next = rseg;

	if (rseg)
		rseg->prev = seg;

	/* Reset selection */

	g_editor->control_sel_l = tmp;
	g_editor->control_sel_r = seg;

	/* Done */

	curr_gradient->last_visited = NULL; /* Force re-search */
	curr_gradient->dirty = 1;

	ed_update_editor(GRAD_UPDATE_PREVIEW | GRAD_UPDATE_CONTROL);
} /* cpopup_flip_callback */


/*****/

static void
cpopup_replicate_callback(GtkWidget *widget, gpointer data)
{
	GtkWidget     *dialog;
	GtkWidget     *vbox;
	GtkWidget     *label;
	GtkWidget     *scale;
	GtkAdjustment *scale_data;

	/* Create dialog window */

	dialog = gimp_dialog_new((g_editor->control_sel_l == g_editor->control_sel_r) ?
				 "Replicate segment" :
				 "Replicate selection");
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(g_editor->shell));
	g_signal_connect(dialog, "close-request",
			 G_CALLBACK(cpopup_dialog_close_request), NULL);

	vbox = gimp_vbox_new(FALSE, 0);
	gimp_container_set_border_width(vbox, 8);
	gimp_box_pack_start(gimp_dialog_get_vbox(dialog), vbox, FALSE, FALSE, 0);

	/* Instructions */

	label = gtk_label_new("Please select the number of times");
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label), 0.0);
	gimp_box_pack_start(vbox, label, FALSE, FALSE, 0);

	label = gtk_label_new((g_editor->control_sel_l == g_editor->control_sel_r) ?
			      "you want to replicate the selected segment" :
			      "you want to replicate the selection");
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label), 0.0);
	gimp_box_pack_start(vbox, label, FALSE, FALSE, 0);

	/* Scale */

	g_editor->replicate_times = 2;
	scale_data = gtk_adjustment_new(2.0, 2.0, 21.0, 1.0, 1.0, 1.0);

	scale = gimp_hscale_new(scale_data, 0);
	gimp_box_pack_start(vbox, scale, FALSE, TRUE, 8);

	g_signal_connect(scale_data, "value-changed",
			 G_CALLBACK(cpopup_replicate_scale_update),
			 NULL);

	/* Buttons */

	gimp_dialog_add_button(dialog, "Replicate",
			       G_CALLBACK(cpopup_do_replicate_callback),
			       dialog, TRUE);
	gimp_dialog_add_button(dialog, "Cancel",
			       G_CALLBACK(cpopup_replicate_cancel_callback),
			       dialog, FALSE);

	/* Show! */

	gtk_window_present(GTK_WINDOW(dialog));
	gtk_widget_set_sensitive(g_editor->shell, FALSE);
} /* cpopup_replicate_callback */


/*****/

static gboolean
cpopup_dialog_close_request(GtkWindow *window, gpointer data)
{
	/* Closing the split / replicate dialogs is like pressing Cancel */

	gtk_window_destroy(window);
	gtk_widget_set_sensitive(g_editor->shell, TRUE);

	return TRUE;
} /* cpopup_dialog_close_request */


/*****/

static void
cpopup_split_uniform_scale_update(GtkAdjustment *adjustment, gpointer data)
{
	g_editor->split_parts = (int) (gtk_adjustment_get_value(adjustment) + 0.5); /* We have to round */
} /* cpopup_split_uniform_scale_update */


/*****/

static void
cpopup_split_uniform_split_callback(GtkWidget *widget, gpointer client_data)
{
	grad_segment_t *seg, *aseg, *lseg, *rseg, *lsel;

	gtk_window_destroy(GTK_WINDOW(client_data));
	gtk_widget_set_sensitive(g_editor->shell, TRUE);

	seg  = g_editor->control_sel_l;
	lsel = NULL;

	do {
		aseg = seg;

		cpopup_split_uniform(seg, g_editor->split_parts, &lseg, &rseg);

		if (seg == g_editor->control_sel_l)
			lsel = lseg;

		seg = rseg->next;
	} while (aseg != g_editor->control_sel_r);

	g_editor->control_sel_l = lsel;
	g_editor->control_sel_r = rseg;

	curr_gradient->last_visited = NULL; /* Force re-search */
	curr_gradient->dirty = 1;
	ed_update_editor(GRAD_UPDATE_PREVIEW | GRAD_UPDATE_CONTROL);
} /* cpopup_split_uniform_split_callback */


/*****/

static void
cpopup_split_uniform_cancel_callback(GtkWidget *widget, gpointer client_data)
{
	gtk_window_destroy(GTK_WINDOW(client_data));
	gtk_widget_set_sensitive(g_editor->shell, TRUE);
} /* cpopup_split_uniform_cancel_callback */


/*****/

static void
cpopup_replicate_scale_update(GtkAdjustment *adjustment, gpointer data)
{
	g_editor->replicate_times = (int) (gtk_adjustment_get_value(adjustment) + 0.5); /* We have to round */
} /* cpopup_replicate_scale_update */


/*****/

static void
cpopup_do_replicate_callback(GtkWidget *widget, gpointer client_data)
{
	double          sel_left, sel_right, sel_len;
	double          new_left;
	double          factor;
	grad_segment_t *prev, *seg, *tmp;
	grad_segment_t *oseg, *oaseg;
	grad_segment_t *lseg, *rseg;
	int             i;

	gtk_window_destroy(GTK_WINDOW(client_data));
	gtk_widget_set_sensitive(g_editor->shell, TRUE);

	/* Remember original parameters */

	sel_left  = g_editor->control_sel_l->left;
	sel_right = g_editor->control_sel_r->right;
	sel_len   = sel_right - sel_left;

	factor = 1.0 / g_editor->replicate_times;

	/* Build replicated segments */

	prev = NULL;
	seg  = NULL;
	tmp  = NULL;

	for (i = 0; i < g_editor->replicate_times; i++) {
		/* Build one cycle */

		new_left  = sel_left + i * factor * sel_len;

		oseg = g_editor->control_sel_l;

		do {
			seg = seg_new_segment();

			if (prev == NULL) {
				seg->left = sel_left;
				tmp = seg; /* Remember first segment */
			} else
				seg->left = new_left + factor * (oseg->left - sel_left);

			seg->middle = new_left + factor * (oseg->middle - sel_left);
			seg->right  = new_left + factor * (oseg->right - sel_left);

			seg->r0 = oseg->r0;
			seg->g0 = oseg->g0;
			seg->b0 = oseg->b0;
			seg->a0 = oseg->a0;

			seg->r1 = oseg->r1;
			seg->g1 = oseg->g1;
			seg->b1 = oseg->b1;
			seg->a1 = oseg->a1;

			seg->type  = oseg->type;
			seg->color = oseg->color;

			seg->prev = prev;
			seg->next = NULL;

			if (prev)
				prev->next = seg;

			prev = seg;

			oaseg = oseg;
			oseg  = oseg->next;
		} while (oaseg != g_editor->control_sel_r);
	} /* for */

	seg->right = sel_right; /* Squish accumulative error */

	/* Free old segments */

	lseg = g_editor->control_sel_l->prev;
	rseg = g_editor->control_sel_r->next;

	oseg = g_editor->control_sel_l;

	do {
		oaseg = oseg->next;
		seg_free_segment(oseg);
		oseg = oaseg;
	} while (oaseg != rseg);

	/* Link in new segments */

	if (lseg)
		lseg->next = tmp;
	else
		curr_gradient->segments = tmp;

	tmp->prev = lseg;

	seg->next = rseg;

	if (rseg)
		rseg->prev = seg;

	/* Reset selection */

	g_editor->control_sel_l = tmp;
	g_editor->control_sel_r = seg;

	/* Done */

	curr_gradient->last_visited = NULL; /* Force re-search */
	curr_gradient->dirty = 1;

	ed_update_editor(GRAD_UPDATE_PREVIEW | GRAD_UPDATE_CONTROL);
} /* cpopup_do_replicate_callback */


/*****/

static void
cpopup_replicate_cancel_callback(GtkWidget *widget, gpointer client_data)
{
	gtk_window_destroy(GTK_WINDOW(client_data));
	gtk_widget_set_sensitive(g_editor->shell, TRUE);
} /* cpopup_replicate_cancel_callback */


/*****/

static void
cpopup_blend_endpoints(double r0, double g0, double b0, double a0,
		       double r1, double g1, double b1, double a1,
		       int blend_colors, int blend_opacity)
{
	double          dr, dg, db, da;
	double          left, len;
	grad_segment_t *seg, *aseg;

	dr = r1 - r0;
	dg = g1 - g0;
	db = b1 - b0;
	da = a1 - a0;

	left  = g_editor->control_sel_l->left;
	len   = g_editor->control_sel_r->right - left;

	seg = g_editor->control_sel_l;

	do {
		if (blend_colors) {
			seg->r0 = r0 + (seg->left - left) / len * dr;
			seg->g0 = g0 + (seg->left - left) / len * dg;
			seg->b0 = b0 + (seg->left - left) / len * db;

			seg->r1 = r0 + (seg->right - left) / len * dr;
			seg->g1 = g0 + (seg->right - left) / len * dg;
			seg->b1 = b0 + (seg->right - left) / len * db;
		} /* if */

		if (blend_opacity) {
			seg->a0 = a0 + (seg->left - left) / len * da;
			seg->a1 = a0 + (seg->right - left) / len * da;
		} /* if */

		aseg = seg;
		seg = seg->next;
	} while (aseg != g_editor->control_sel_r);
} /* cpopup_blend_endpoints */


/*****/

static void
cpopup_split_midpoint(grad_segment_t *lseg, grad_segment_t **newl, grad_segment_t **newr)
{
	double          r, g, b, a;
	grad_segment_t *newseg;

	/* Get color at original segment's midpoint */

	grad_get_color_at(lseg->middle, &r, &g, &b, &a);

	/* Create a new segment and insert it in the list */

	newseg = seg_new_segment();

	newseg->prev = lseg;
	newseg->next = lseg->next;

	lseg->next = newseg;

	if (newseg->next)
		newseg->next->prev = newseg;

	/* Set coordinates of new segment */

	newseg->left   = lseg->middle;
	newseg->right  = lseg->right;
	newseg->middle = (newseg->left + newseg->right) / 2.0;

	/* Set coordinates of original segment */

	lseg->right  = newseg->left;
	lseg->middle = (lseg->left + lseg->right) / 2.0;

	/* Set colors of both segments */

	newseg->r1 = lseg->r1;
	newseg->g1 = lseg->g1;
	newseg->b1 = lseg->b1;
	newseg->a1 = lseg->a1;

	lseg->r1 = newseg->r0 = r;
	lseg->g1 = newseg->g0 = g;
	lseg->b1 = newseg->b0 = b;
	lseg->a1 = newseg->a0 = a;

	/* Set parameters of new segment */

	newseg->type  = lseg->type;
	newseg->color = lseg->color;

	/* Done */

	*newl = lseg;
	*newr = newseg;
} /* cpopup_split_midpoint */


/*****/

static void
cpopup_split_uniform(grad_segment_t *lseg, int parts,
		     grad_segment_t **newl, grad_segment_t **newr)
{
	grad_segment_t *seg, *prev, *tmp;
	double          seg_len;
	int             i;

	seg_len = (lseg->right - lseg->left) / parts; /* Length of divisions */

	seg  = NULL;
	prev = NULL;
	tmp  = NULL;

	for (i = 0; i < parts; i++) {
		seg = seg_new_segment();

		if (i == 0)
			tmp = seg; /* Remember first segment */

		seg->left   = lseg->left + i * seg_len;
		seg->right  = lseg->left + (i + 1) * seg_len;
		seg->middle = (seg->left + seg->right) / 2.0;

		grad_get_color_at(seg->left, &seg->r0, &seg->g0, &seg->b0, &seg->a0);
		grad_get_color_at(seg->right, &seg->r1, &seg->g1, &seg->b1, &seg->a1);

		seg->type  = lseg->type;
		seg->color = lseg->color;

		seg->prev = prev;
		seg->next = NULL;

		if (prev)
			prev->next = seg;

		prev = seg;
	} /* for */

	/* Fix edges */

	tmp->r0 = lseg->r0;
	tmp->g0 = lseg->g0;
	tmp->b0 = lseg->b0;
	tmp->a0 = lseg->a0;

	seg->r1 = lseg->r1;
	seg->g1 = lseg->g1;
	seg->b1 = lseg->b1;
	seg->a1 = lseg->a1;

	tmp->left  = lseg->left;
	seg->right = lseg->right; /* To squish accumulative error */

	/* Link in list */

	tmp->prev = lseg->prev;
	seg->next = lseg->next;

	if (lseg->prev)
		lseg->prev->next = tmp;
	else
		curr_gradient->segments = tmp; /* We are on leftmost segment */

	if (lseg->next)
		lseg->next->prev = seg;

	curr_gradient->last_visited = NULL; /* Force re-search */

	/* Done */

	*newl = tmp;
	*newr = seg;

	/* Delete old segment */

	seg_free_segment(lseg);
} /* cpopup_split_uniform */


/***** Gradient functions *****/

/*****/

static gradient_t *
grad_new_gradient(void)
{
	gradient_t *grad;

	grad = g_malloc(sizeof(gradient_t));

	grad->name     	   = NULL;
	grad->segments 	   = NULL;
	grad->last_visited = NULL;
	grad->dirty        = 0;
	grad->filename     = NULL;
	grad->list_item    = NULL;

	return grad;
} /* grad_new_gradient */


/*****/

static void
grad_free_gradient(gradient_t *grad)
{
	g_assert(grad != NULL);

	if (grad->name)
		g_free(grad->name);

	if (grad->segments)
		seg_free_segments(grad->segments);

	if (grad->filename)
		g_free(grad->filename);

	g_free(grad);
} /* grad_free_gradient */


/*****/

static void
grad_free_gradients(void)
{
	GSList     *node;
	gradient_t *grad;

	node = gradients_list;

	while (node) {
		grad = node->data;

		/* If gradient has dirty flag set, save it */

		if (grad->dirty)
			grad_save_gradient(grad, grad->filename);

		grad_free_gradient(grad);

		node = g_slist_next(node);
	} /* while */

	g_slist_free(gradients_list);

	num_gradients  = 0;
	gradients_list = NULL;
	curr_gradient  = NULL;
	grad_default_gradient = NULL;
} /* grad_free_gradients */


/*****/

/* Reads "left middle right r0 g0 b0 a0 r1 g1 b1 a1 type coloring" from
 * line, independent of the locale's decimal point.  Returns the number
 * of fields read, as sscanf() would.
 */

static int
grad_parse_segment(const char *line, grad_segment_t *seg, int *type, int *color)
{
	double *fields[11];
	char   *end;
	int     i;

	fields[0]  = &seg->left;
	fields[1]  = &seg->middle;
	fields[2]  = &seg->right;
	fields[3]  = &seg->r0;
	fields[4]  = &seg->g0;
	fields[5]  = &seg->b0;
	fields[6]  = &seg->a0;
	fields[7]  = &seg->r1;
	fields[8]  = &seg->g1;
	fields[9]  = &seg->b1;
	fields[10] = &seg->a1;

	for (i = 0; i < 11; i++) {
		*fields[i] = g_ascii_strtod(line, &end);
		if (end == line)
			return i;
		line = end;
	} /* for */

	*type = (int) strtol(line, &end, 10);
	if (end == line)
		return 11;
	line = end;

	*color = (int) strtol(line, &end, 10);
	if (end == line)
		return 12;

	return 13;
} /* grad_parse_segment */


/*****/

static void
grad_load_gradient(char *filename)
{
	FILE           *file;
	gradient_t     *grad;
	grad_segment_t *seg, *prev;
	int             num_segments;
	int             i;
	int             type, color;
	char            line[1024];

	g_assert(filename != NULL);

	/* Binary mode: the files may have either line ending */

	file = g_fopen(filename, "rb");
	if (!file)
		return;

	if (!fgets(line, 1024, file))
		line[0] = '\0';
	g_strchomp(line);
	if (strcmp(line, "GIMP Gradient") != 0) {
		fclose(file);
		return;
	} /* if */

	grad = grad_new_gradient();

	grad->filename = g_strdup(filename);
	grad->name     = g_strdup(prune_filename(filename));

	if (!fgets(line, 1024, file))
		line[0] = '\0';
	num_segments = atoi(line);

	if (num_segments < 1) {
		g_message ("grad_load_gradient(): invalid number of segments in \"%s\"", filename);
		fclose(file);
		grad_free_gradient(grad);
		return;
	} /* if */

	prev = NULL;

	for (i = 0; i < num_segments; i++) {
		seg = seg_new_segment();
		seg->prev = prev;

		if (prev)
			prev->next = seg;
		else
			grad->segments = seg;

		if (!fgets(line, 1024, file))
			line[0] = '\0';

		if (grad_parse_segment(line, seg, &type, &color) != 13) {
			g_message ("grad_load_gradient(): badly formatted "
				   "gradient segment %d in \"%s\" --- bad things may "
				   "happen soon", i, filename);
		} else {
			seg->type  = (grad_type_t) type;
			seg->color = (grad_color_t) color;
		} /* else */

		prev = seg;
	} /* for */

	fclose(file);

	grad_insert_in_gradients_list(grad);

	/* Check if this gradient is the default one */

	if (default_gradient && strcmp(default_gradient, grad->name) == 0)
		grad_default_gradient = grad;
} /* grad_load_gradient */


/*****/

static void
grad_save_gradient(gradient_t *grad, char *filename)
{
	FILE           *file;
	int             num_segments;
	grad_segment_t *seg;

	g_assert(grad != NULL);

	if (!filename) {
		g_message ("grad_save_gradient(): can not save gradient with NULL filename");
		return;
	} /* if */

	file = g_fopen(filename, "wb");
	if (!file) {
		g_message ("grad_save_gradient(): can't open \"%s\"", filename);
		return;
	} /* if */

	/* File format is:
	 *
	 *   GIMP Gradient
	 *   number_of_segments
	 *   left middle right r0 g0 b0 a0 r1 g1 b1 a1 type coloring
	 *   left middle right r0 g0 b0 a0 r1 g1 b1 a1 type coloring
	 *   ...
	 */

	fprintf(file, "GIMP Gradient\n");

	/* Count number of segments */

	num_segments = 0;
	seg          = grad->segments;

	while (seg) {
		num_segments++;
		seg = seg->next;
	} /* while */

	/* Write rest of file */

	fprintf(file, "%d\n", num_segments);

	for (seg = grad->segments; seg; seg = seg->next) {
		double values[11];
		char   buf[G_ASCII_DTOSTR_BUF_SIZE];
		int    i;

		values[0]  = seg->left;
		values[1]  = seg->middle;
		values[2]  = seg->right;
		values[3]  = seg->r0;
		values[4]  = seg->g0;
		values[5]  = seg->b0;
		values[6]  = seg->a0;
		values[7]  = seg->r1;
		values[8]  = seg->g1;
		values[9]  = seg->b1;
		values[10] = seg->a1;

		/* "%f", but always with a '.' whatever the locale */

		for (i = 0; i < 11; i++)
			fprintf(file, "%s ", g_ascii_formatd(buf, sizeof(buf), "%f", values[i]));

		fprintf(file, "%d %d\n", (int) seg->type, (int) seg->color);
	} /* for */

	fclose(file);

	grad->dirty = 0;
} /* grad_save_gradient */


/*****/

static gradient_t *
grad_create_default_gradient(void)
{
	gradient_t *grad;

	grad = grad_new_gradient();
	grad->segments = seg_new_segment();

	return grad;
} /* grad_create_default_gradient */


/*****/

static int
grad_insert_in_gradients_list(gradient_t *grad)
{
	GSList     *tmp;
	gradient_t *g;
	int         n;

	/* We insert gradients in alphabetical order.  Find the index
	 * of the gradient after which we will insert the current one.
	 */

	n   = 0;
	tmp = gradients_list;

	while (tmp) {
		g = tmp->data;

		if (strcmp(grad->name, g->name) <= 0)
			break; /* We found the one we want */

		n++;
		tmp = g_slist_next(tmp);
	} /* while */

	num_gradients++;
	gradients_list = g_slist_insert(gradients_list, grad, n);

	return n;
} /* grad_insert_in_gradients_list */


/*****/

static void
grad_dump_gradient(gradient_t *grad, FILE *file)
{
	grad_segment_t *seg;

	fprintf(file, "Name: \"%s\"\n", grad->name);
	fprintf(file, "Dirty: %d\n", grad->dirty);
	fprintf(file, "Filename: \"%s\"\n", grad->filename);

	seg = grad->segments;

	while (seg) {
		fprintf(file, "%c%p | %f %f %f | %f %f %f %f | %f %f %f %f | %d %d | %p %p\n",
			(seg == grad->last_visited) ? '>' : ' ',
			seg,
			seg->left, seg->middle, seg->right,
			seg->r0, seg->g0, seg->b0, seg->a0,
			seg->r1, seg->g1, seg->b1, seg->a1,
			(int) seg->type,
			(int) seg->color,
			seg->prev, seg->next);

		seg = seg->next;
	} /* while */
} /* grad_dump_gradient */


/***** Segment functions *****/

/*****/

static grad_segment_t *
seg_new_segment(void)
{
	grad_segment_t *seg;

	seg = g_malloc(sizeof(grad_segment_t));

	seg->left   = 0.0;
	seg->middle = 0.5;
	seg->right  = 1.0;

	seg->r0 = seg->g0 = seg->b0 = 0.0;
	seg->r1 = seg->g1 = seg->b1 = seg->a0 = seg->a1 = 1.0;

	seg->type  = GRAD_LINEAR;
	seg->color = GRAD_RGB;

	seg->prev = seg->next = NULL;

	return seg;
} /* seg_new_segment */


/*****/

static void
seg_free_segment(grad_segment_t *seg)
{
	g_assert(seg != NULL);

	g_free(seg);
} /* seg_free_segment */


/*****/

static void
seg_free_segments(grad_segment_t *seg)
{
	grad_segment_t *tmp;

	g_assert(seg != NULL);

	while (seg) {
		tmp = seg->next;
		seg_free_segment(seg);
		seg = tmp;
	} /* while */
} /* seg_free_segments */


/*****/

static grad_segment_t *
seg_get_segment_at(gradient_t *grad, double pos)
{
	grad_segment_t *seg;

	g_assert(grad != NULL);

	pos = BOUNDS(pos, 0.0, 1.0); /* to handle FP imprecision at the edges of the gradient */

	if (grad->last_visited)
		seg = grad->last_visited;
	else
		seg = grad->segments;

	while (seg)
		if (pos >= seg->left) {
			if (pos <= seg->right) {
				grad->last_visited = seg; /* for speed */
				return seg;
			} else
				seg = seg->next;
		} else
			seg = seg->prev;

	/* Oops: we should have found a segment, but we didn't */

	grad_dump_gradient(curr_gradient, stderr);
	fatal_error("seg_get_segment_at(): aieee, no matching segment for position %0.15f", pos);
	return NULL; /* To shut up -Wall */
} /* seg_get_segment_at */


/*****/

static grad_segment_t *
seg_get_last_segment(grad_segment_t *seg)
{
	if (!seg)
		return NULL;

	while (seg->next)
		seg = seg->next;

	return seg;
} /* seg_get_last_segment */


/*****/

static void
seg_get_closest_handle(gradient_t *grad, double pos,
		       grad_segment_t **seg, control_drag_mode_t *handle)
{
	double l_delta, m_delta, r_delta;

	*seg = seg_get_segment_at(grad, pos);

	m_delta = fabs(pos - (*seg)->middle);

	if (pos < (*seg)->middle) {
		l_delta = fabs(pos - (*seg)->left);

		if (l_delta < m_delta)
			*handle = GRAD_DRAG_LEFT;
		else
			*handle = GRAD_DRAG_MIDDLE;
	} else {
		r_delta = fabs(pos - (*seg)->right);

		if (m_delta < r_delta)
			*handle = GRAD_DRAG_MIDDLE;
		else {
			*seg = (*seg)->next;
			*handle = GRAD_DRAG_LEFT;
		} /* else */
	} /* else */
} /* seg_get_closest_handle */


/***** Calculation functions *****/

/*****/

static double
calc_linear_factor(double middle, double pos)
{
	if (pos <= middle) {
		if (middle < EPSILON)
			return 0.0;
		else
			return 0.5 * pos / middle;
	} else {
		pos -= middle;
		middle = 1.0 - middle;

		if (middle < EPSILON)
			return 1.0;
		else
			return 0.5 + 0.5 * pos / middle;
	} /* else */
} /* calc_linear_factor */


/*****/

static double
calc_curved_factor(double middle, double pos)
{
	if (middle < EPSILON)
		middle = EPSILON;

	return pow(pos, log(0.5) / log(middle));
} /* calc_curved_factor */


/*****/

static double
calc_sine_factor(double middle, double pos)
{
	pos = calc_linear_factor(middle, pos);

	return (sin((-M_PI / 2.0) + M_PI * pos) + 1.0) / 2.0;
} /* calc_sine_factor */


/*****/

static double
calc_sphere_increasing_factor(double middle, double pos)
{
	pos = calc_linear_factor(middle, pos) - 1.0;

	return sqrt(1.0 - pos * pos); /* Works for convex increasing and concave decreasing */
} /* calc_sphere_increasing_factor */


/*****/

static double
calc_sphere_decreasing_factor(double middle, double pos)
{
	pos = calc_linear_factor(middle, pos);

	return 1.0 - sqrt(1.0 - pos * pos); /* Works for convex decreasing and concave increasing */
} /* calc_sphere_decreasing_factor */


/*****/

static void
calc_rgb_to_hsv(double *r, double *g, double *b)
{
	double red, green, blue;
	double h, s, v;
	double min, max;
	double delta;

	red   = *r;
	green = *g;
	blue  = *b;

	h = 0.0; /* Shut up -Wall */

	if (red > green) {
		if (red > blue)
			max = red;
		else
			max = blue;

		if (green < blue)
			min = green;
		else
			min = blue;
	} else {
		if (green > blue)
			max = green;
		else
			max = blue;

		if (red < blue)
			min = red;
		else
			min = blue;
	} /* else */

	v = max;

	if (max != 0.0)
		s = (max - min) / max;
	else
		s = 0.0;

	if (s == 0.0)
		h = 0.0;
	else {
		delta = max - min;

		if (red == max)
			h = (green - blue) / delta;
		else if (green == max)
			h = 2 + (blue - red) / delta;
		else if (blue == max)
			h = 4 + (red - green) / delta;

		h /= 6.0;

		if (h < 0.0)
			h += 1.0;
		else if (h > 1.0)
			h -= 1.0;
	} /* else */

	*r = h;
	*g = s;
	*b = v;
} /* calc_rgb_to_hsv */


/*****/

static void
calc_hsv_to_rgb(double *h, double *s, double *v)
{
	double hue, saturation, value;
	double f, p, q, t;

	if (*s == 0.0) {
		*h = *v;
		*s = *v;
		*v = *v; /* heh */
	} else {
		hue        = *h * 6.0;
		saturation = *s;
		value      = *v;

		if (hue == 6.0)
			hue = 0.0;

		f = hue - (int) hue;
		p = value * (1.0 - saturation);
		q = value * (1.0 - saturation * f);
		t = value * (1.0 - saturation * (1.0 - f));

		switch ((int) hue) {
			case 0:
				*h = value;
				*s = t;
				*v = p;
				break;

			case 1:
				*h = q;
				*s = value;
				*v = p;
				break;

			case 2:
				*h = p;
				*s = value;
				*v = t;
				break;

			case 3:
				*h = p;
				*s = q;
				*v = value;
				break;

			case 4:
				*h = t;
				*s = p;
				*v = value;
				break;

			case 5:
				*h = value;
				*s = p;
				*v = q;
				break;
		} /* switch */
	} /* else */
} /* calc_hsv_to_rgb */


/***** Files and paths functions *****/

/*****/

static char *
build_user_filename(char *name, char *path_str)
{
	char **tokens;
	char  *path;
	char  *filename;

	g_assert(name != NULL);

	if (!path_str)
		return NULL; /* Perhaps this is not a good idea */

	/* Get the first path specified in the list */

	tokens   = g_strsplit(path_str, G_SEARCHPATH_SEPARATOR_S, 2);
	filename = NULL;

	if (tokens[0] && tokens[0][0]) {
		if (tokens[0][0] == '~')
			path = g_build_filename(g_get_home_dir(), tokens[0] + 1, NULL);
		else
			path = g_strdup(tokens[0]);

		filename = g_build_filename(path, name, NULL);

		g_free(path);
	} /* if */

	g_strfreev(tokens);

	return filename;
} /* build_user_filename */


/***** Procedural database declarations and functions *****/

/***** gradients_get_list *****/

ProcArg gradients_get_list_out_args[] = {
	{ PDB_INT32,
	  "num_gradients",
	  "The number of loaded gradients"
	},
	{ PDB_STRINGARRAY,
	  "gradient_names",
	  "The list of gradient names"
	}
}; /* gradients_get_list_out_args */


ProcRecord gradients_get_list_proc = {
	"gimp_gradients_get_list",
	"Retrieve the list of loaded gradients",
	"This procedure returns a list of the gradients that are currently loaded "
	"in the gradient editor.  You can later use the gimp_gradients_set_active "
	"function to set the active gradient.",
	"Federico Mena Quintero",
	"Federico Mena Quintero",
	"1997",
	PDB_INTERNAL,

	/* Input arguments */

	0,
	NULL,

	/* Output arguments */

	sizeof(gradients_get_list_out_args) / sizeof(gradients_get_list_out_args[0]),
	gradients_get_list_out_args,

	/* Exec method */

	{ { gradients_get_list_invoker } }
}; /* gradients_get_list_proc */


static Argument *
gradients_get_list_invoker(Argument *args)
{
	Argument   *return_args;
	gradient_t *grad;
	GSList     *list;
	char      **gradients;
	int         i;
	int         success;

	gradients = g_malloc(sizeof(char *) * num_gradients);

	list = gradients_list;

	success = (list != NULL);

	i = 0;

	while (list) {
		grad           = list->data;
		gradients[i++] = g_strdup(grad->name);
		list           = g_slist_next(list);
	} /* while */

	return_args = procedural_db_return_args(&gradients_get_list_proc, success);

	if (success) {
		return_args[1].value.pdb_int = num_gradients;
		return_args[2].value.pdb_pointer = gradients;
	} /* if */

	return return_args;
} /* gradients_get_list_invoker */


/***** gradients_get_active *****/

ProcArg gradients_get_active_out_args[] = {
	{ PDB_STRING,
	  "name",
	  "The name of the active gradient"
	}
}; /* gradients_get_active_out_args */


ProcRecord gradients_get_active_proc = {
	"gimp_gradients_get_active",
	"Retrieve the name of the active gradient",
	"This procedure returns the name of the active gradient in hte gradient editor.",
	"Federico Mena Quintero",
	"Federico Mena Quintero",
	"1997",
	PDB_INTERNAL,

	/* Input arguments */

	0,
	NULL,

	/* Output arguments */

	sizeof(gradients_get_active_out_args) / sizeof(gradients_get_active_out_args[0]),
	gradients_get_active_out_args,

	/* Exec method */

	{ { gradients_get_active_invoker } }
}; /* gradients_get_active_proc */


Argument *
gradients_get_active_invoker(Argument *args)
{
	Argument *return_args;
	int       success;

	success = (curr_gradient != NULL);

	return_args = procedural_db_return_args(&gradients_get_active_proc, success);

	if (success)
		return_args[1].value.pdb_pointer = g_strdup(curr_gradient->name);

	return return_args;
} /* gradients_get_active_invoker */


/***** gradients_set_active *****/

ProcArg gradients_set_active_args[] = {
	{ PDB_STRING,
	  "name",
	  "The name of the gradient to set"
	}
}; /* gradients_set_active_args */


ProcRecord gradients_set_active_proc = {
	"gimp_gradients_set_active",
	"Sets the specified gradient as the active gradient",
	"This procedure lets you set the specified gradient as the active "
	"or \"current\" one.  The name is simply a string which corresponds "
	"to one of the loaded gradients in the gradient editor.  If no "
	"matching gradient is found, this procedure will return an error. "
	"Otherwise, the specified gradient will become active and will be used "
	"for subsequent custom gradient operations.",
	"Federico Mena Quintero",
	"Federico Mena Quintero",
	"1997",
	PDB_INTERNAL,

	/* Input arguments */

	sizeof(gradients_set_active_args) / sizeof(gradients_set_active_args[0]),
	gradients_set_active_args,

	/* Output arguments */

	0,
	NULL,

	/* Exec method */

	{ { gradients_set_active_invoker } }
}; /* gradients_set_active_proc */


static Argument *
gradients_set_active_invoker(Argument *args)
{
	char       *name;
	GSList     *list;
	gradient_t *grad;
	int         success;

	name = args[0].value.pdb_pointer;

	success = (name != NULL);

	if (success) {
		/* See if we have a gradient with the specified name */

		list = gradients_list;

		success = FALSE;

		while (list) {
			grad = list->data;

			if (strcmp(grad->name, name) == 0) {
				/* We found it! */

				success = TRUE;

				if (g_editor != NULL && grad->list_item != NULL) {
					/* Select that gradient in the listbox */
					curr_gradient = grad;
					gtk_list_box_select_row(GTK_LIST_BOX(g_editor->list),
								GTK_LIST_BOX_ROW(grad->list_item));
				} else
					/* Just update the current gradient */
					curr_gradient = grad;

				break;
			} /* if */

			list = g_slist_next(list);
		} /* while */
	} /* if */

	return procedural_db_return_args(&gradients_set_active_proc, success);
} /* gradients_set_active_invoker */


/***** gradients_sample_uniform *****/

ProcArg gradients_sample_uniform_args[] = {
	{ PDB_INT32,
	  "num_samples",
	  "The number of samples to take"
	}
}; /* gradients_sample_uniform_args */


ProcArg gradients_sample_uniform_out_args[] = {
	{ PDB_INT32,
	  "array_length",
	  "Length of the color_samples array (4 * num_samples)"
	},
	{ PDB_FLOATARRAY,
	  "color_samples",
	  "Color samples: { R1, G1, B1, A1, ..., Rn, Gn, Bn, An }"
	}
}; /* gradients_sample_uniform_out_args */


ProcRecord gradients_sample_uniform_proc = {
	"gimp_gradients_sample_uniform",
	"Sample the active gradient in uniform parts",
	"This procedure samples the active gradient from the gradient editor in the "
	"specified number of uniform parts.  It returns a list of floating-point values "
	"which correspond to the RGBA values for each sample.  The minimum number of "
	"samples to take is 2, in which case the returned colors will correspond to the "
	"{ 0.0, 1.0 } positions in the gradient.  For example, if the number of samples "
	"is 3, the procedure will return the colors at positions { 0.0, 0.5, 1.0 }.",
	"Federico Mena Quintero",
	"Federico Mena Quintero",
	"1997",
	PDB_INTERNAL,

	/* Input arguments */

	sizeof(gradients_sample_uniform_args) / sizeof(gradients_sample_uniform_args[0]),
	gradients_sample_uniform_args,

	/* Output arguments */

	sizeof(gradients_sample_uniform_out_args) / sizeof(gradients_sample_uniform_out_args[0]),
	gradients_sample_uniform_out_args,

	/* Exec method */

	{ { gradients_sample_uniform_invoker } }
}; /* gradients_sample_uniform_proc */


static Argument *
gradients_sample_uniform_invoker(Argument *args)
{
	Argument *return_args;
	gdouble  *values, *pv;
	double    pos, delta;
	double    r, g, b, a;
	int       i;
	int       success;

	i = args[0].value.pdb_int;

	success = (i >= 2);

	return_args = procedural_db_return_args(&gradients_sample_uniform_proc, success);

	if (success) {
		pos   = 0.0;
		delta = 1.0 / (i - 1);

		values = g_malloc(i * 4 * sizeof(gdouble));
		pv     = values;

		return_args[1].value.pdb_int = i * 4;

		while (i--) {
			grad_get_color_at(pos, &r, &g, &b, &a);

			*pv++ = r;
			*pv++ = g;
			*pv++ = b;
			*pv++ = a;

			pos += delta;
		} /* while */

		return_args[2].value.pdb_pointer = values;
	} /* if */

	return return_args;
} /* gradients_sample_uniform_invoker */


/***** gradients_sample_custom *****/

ProcArg gradients_sample_custom_args[] = {
	{ PDB_INT32,
	  "num_samples",
	  "The number of samples to take"
	},
	{ PDB_FLOATARRAY,
	  "positions",
	  "The list of positions to sample along the gradient"
	}
}; /* gradients_sample_custom_args */


ProcArg gradients_sample_custom_out_args[] = {
	{ PDB_INT32,
	  "array_length",
	  "Length of the color_samples array (4 * num_samples)"
	},
	{ PDB_FLOATARRAY,
	  "color_samples",
	  "Color samples: { R1, G1, B1, A1, ..., Rn, Gn, Bn, An }"
	}
}; /* gradients_sample_custom_out_args */


ProcRecord gradients_sample_custom_proc = {
	"gimp_gradients_sample_custom",
	"Sample the active gradient in custom positions",
	"This procedure samples the active gradient from the gradient editor in "
	"the specified number of points.  The procedure will sample the gradient "
	"in the specified positions from the list.  The left endpoint of the "
	"gradient corresponds to position 0.0, and the right endpoint corresponds "
	"to 1.0.  The procedure returns a list of floating-point values which "
	"correspond to the RGBA values for each sample.",
	"Federico Mena Quintero",
	"Federico Mena Quintero",
	"1997",
	PDB_INTERNAL,

	/* Input arguments */

	sizeof(gradients_sample_custom_args) / sizeof(gradients_sample_custom_args[0]),
	gradients_sample_custom_args,

	/* Output arguments */

	sizeof(gradients_sample_custom_out_args) / sizeof(gradients_sample_custom_out_args[0]),
	gradients_sample_custom_out_args,

	/* Exec method */

	{ { gradients_sample_custom_invoker } }
}; /* gradients_sample_custom_proc */


static Argument *
gradients_sample_custom_invoker(Argument *args)
{
	Argument *return_args;
	gdouble  *values, *pv;
	gdouble  *pos;
	double    r, g, b, a;
	int       i;
	int       success;

	i = args[0].value.pdb_int;

	success = (i > 0);

	return_args = procedural_db_return_args(&gradients_sample_custom_proc, success);

	if (success) {
		pos = args[1].value.pdb_pointer;

		values = g_malloc(i * 4 * sizeof(gdouble));
		pv     = values;

		return_args[1].value.pdb_int = i * 4;

		while (i--) {
			grad_get_color_at(*pos, &r, &g, &b, &a);

			*pv++ = r;
			*pv++ = g;
			*pv++ = b;
			*pv++ = a;

			pos++;
		} /* while */

		return_args[2].value.pdb_pointer = values;
	} /* if */

	return return_args;
} /* gradients_sample_custom_invoker */
