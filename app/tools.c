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
#include "appenv.h"
#include "actionarea.h"
#include "airbrush.h"
#include "bezier_select.h"
#include "blend.h"
#include "brightness_contrast.h"
#include "bucket_fill.h"
#include "by_color_select.h"
#include "clone.h"
#include "color_balance.h"
#include "color_picker.h"
#include "convolve.h"
#include "crop.h"
#include "cursorutil.h"
#include "curves.h"
#include "eraser.h"
#include "gdisplay.h"
#include "hue_saturation.h"
#include "ellipse_select.h"
#include "flip_tool.h"
#include "free_select.h"
#include "fuzzy_select.h"
#include "gimprc.h"
#include "histogram_tool.h"
#include "interface.h"
#include "iscissors.h"
#include "levels.h"
#include "magnify.h"
#include "move.h"
#include "paintbrush.h"
#include "pencil.h"
#include "posterize.h"
#include "rect_select.h"
#include "text_tool.h"
#include "threshold.h"
#include "tools.h"
#include "transform_tool.h"

/* Global Data */

Tool * active_tool = NULL;
Layer * active_tool_layer = NULL;

/* Local Data */

static GtkWidget *options_title = NULL;
static GtkWidget *options_stack = NULL;
static ToolType active_tool_type = -1;

static int global_tool_ID = 0;

ToolInfo tool_info[] =
{
  { NULL, "Rect Select", 0 },
  { NULL, "Ellipse Select", 1 },
  { NULL, "Free Select", 2 },
  { NULL, "Fuzzy Select", 3 },
  { NULL, "Bezier Select", 4 },
  { NULL, "Intelligent Scissors", 5 },
  { NULL, "Move", 6 },
  { NULL, "Magnify", 7 },
  { NULL, "Crop", 8 },
  { NULL, "Transform", 9 }, /* rotate */
  { NULL, "Transform", 9 }, /* scale */
  { NULL, "Transform", 9 }, /* shear */
  { NULL, "Transform", 9 }, /* perspective */
  { NULL, "Flip", 10 }, /* horizontal */
  { NULL, "Flip", 10 }, /* vertical */
  { NULL, "Text", 11 },
  { NULL, "Color Picker", 12 },
  { NULL, "Bucket Fill", 13 },
  { NULL, "Blend", 14 },
  { NULL, "Pencil", 15 },
  { NULL, "Paintbrush", 16 },
  { NULL, "Eraser", 17 },
  { NULL, "Airbrush", 18 },
  { NULL, "Clone", 19 },
  { NULL, "Convolve", 20 },

  /*  Non-toolbox tools  */
  { NULL, "By Color Select", 21 },
  { NULL, "Color Balance", 22 },
  { NULL, "Brightness-Contrast", 23 },
  { NULL, "Hue-Saturation", 24 },
  { NULL, "Posterize", 25 },
  { NULL, "Threshold", 26 },
  { NULL, "Curves", 27 },
  { NULL, "Levels", 28 },
  { NULL, "Histogram", 29 }
};


/*  Local function declarations  */

static void tools_options_add_page (GtkWidget *options);
static void tools_options_show_active (void);


/* Function definitions */

/*  Shows the active tool's page in the tool options panel.  */
static void
tools_options_show_active (void)
{
  GtkWidget *options;
  const char *title;

  if (! active_tool || ! options_stack)
    return;

  options = tool_info[(int) active_tool->type].tool_options;
  if (options && gtk_widget_get_parent (options) == options_stack)
    {
      gtk_stack_set_visible_child (GTK_STACK (options_stack), options);

      title = g_object_get_data (G_OBJECT (options), "gimp-options-title");
      gtk_label_set_text (GTK_LABEL (options_title),
			  title ? title : tool_info[(int) active_tool->type].tool_name);
    }
}

static void
active_tool_free (void)
{
  if (!active_tool)
    return;

  switch (active_tool->type)
    {
    case RECT_SELECT:
      tools_free_rect_select (active_tool);
      break;
    case ELLIPSE_SELECT:
      tools_free_ellipse_select (active_tool);
      break;
    case FREE_SELECT:
      tools_free_free_select (active_tool);
      break;
    case FUZZY_SELECT:
      tools_free_fuzzy_select (active_tool);
      break;
    case BEZIER_SELECT:
      tools_free_bezier_select (active_tool);
      break;
    case ISCISSORS:
      tools_free_iscissors (active_tool);
      break;
    case CROP:
      tools_free_crop (active_tool);
      break;
    case MOVE:
      tools_free_move_tool (active_tool);
      break;
    case MAGNIFY:
      tools_free_magnify (active_tool);
      break;
    case ROTATE:
      tools_free_transform_tool (active_tool);
      break;
    case SCALE:
      tools_free_transform_tool (active_tool);
      break;
    case SHEAR:
      tools_free_transform_tool (active_tool);
      break;
    case PERSPECTIVE:
      tools_free_transform_tool (active_tool);
      break;
    case FLIP_HORZ:
      tools_free_flip_tool (active_tool);
      break;
    case FLIP_VERT:
      tools_free_flip_tool (active_tool);
      break;
    case TEXT:
      tools_free_text (active_tool);
      break;
    case COLOR_PICKER:
      tools_free_color_picker (active_tool);
      break;
    case BUCKET_FILL:
      tools_free_bucket_fill (active_tool);
      break;
    case BLEND:
      tools_free_blend (active_tool);
      break;
    case PENCIL:
      tools_free_pencil (active_tool);
      break;
    case PAINTBRUSH:
      tools_free_paintbrush (active_tool);
      break;
    case ERASER:
      tools_free_eraser (active_tool);
      break;
    case AIRBRUSH:
      tools_free_airbrush (active_tool);
      break;
    case CLONE:
      tools_free_clone (active_tool);
      break;
    case CONVOLVE:
      tools_free_convolve (active_tool);
      break;
    case BY_COLOR_SELECT:
      tools_free_by_color_select (active_tool);
      break;
    case COLOR_BALANCE:
      tools_free_color_balance (active_tool);
      break;
    case BRIGHTNESS_CONTRAST:
      tools_free_brightness_contrast (active_tool);
      break;
    case HUE_SATURATION:
      tools_free_hue_saturation (active_tool);
      break;
    case POSTERIZE:
      tools_free_posterize (active_tool);
      break;
    case THRESHOLD:
      tools_free_threshold (active_tool);
      break;
    case CURVES:
      tools_free_curves (active_tool);
      break;
    case LEVELS:
      tools_free_levels (active_tool);
      break;
    case HISTOGRAM:
      tools_free_histogram_tool (active_tool);
      break;
    default:
      return;
    }

  g_free (active_tool);
  active_tool = NULL;
  active_tool_layer = NULL;
}


void
tools_select (ToolType type)
{
  if (active_tool)
    active_tool_free ();

  switch (type)
    {
    case RECT_SELECT:
      active_tool = tools_new_rect_select ();
      break;
    case ELLIPSE_SELECT:
      active_tool = tools_new_ellipse_select ();
      break;
    case FREE_SELECT:
      active_tool = tools_new_free_select ();
      break;
    case FUZZY_SELECT:
      active_tool = tools_new_fuzzy_select ();
      break;
    case BEZIER_SELECT:
      active_tool = tools_new_bezier_select ();
      break;
    case ISCISSORS:
      active_tool = tools_new_iscissors ();
      break;
    case MOVE:
      active_tool = tools_new_move_tool ();
      break;
    case MAGNIFY:
      active_tool = tools_new_magnify ();
      break;
    case CROP:
      active_tool = tools_new_crop ();
      break;
    case ROTATE:
      active_tool = tools_new_transform_tool ();
      break;
    case SCALE:
      active_tool = tools_new_transform_tool ();
      break;
    case SHEAR:
      active_tool = tools_new_transform_tool ();
      break;
    case PERSPECTIVE:
      active_tool = tools_new_transform_tool ();
      break;
    case FLIP_HORZ:
      active_tool = tools_new_flip ();
      break;
    case FLIP_VERT:
      active_tool = tools_new_flip ();
      break;
    case TEXT:
      active_tool = tools_new_text ();
      break;
    case COLOR_PICKER:
      active_tool = tools_new_color_picker ();
      break;
    case BUCKET_FILL:
      active_tool = tools_new_bucket_fill ();
      break;
    case BLEND:
      active_tool = tools_new_blend ();
      break;
    case PENCIL:
      active_tool = tools_new_pencil ();
      break;
    case PAINTBRUSH:
      active_tool = tools_new_paintbrush ();
      break;
    case ERASER:
      active_tool = tools_new_eraser ();
      break;
    case AIRBRUSH:
      active_tool = tools_new_airbrush ();
      break;
    case CLONE:
      active_tool = tools_new_clone ();
      break;
    case CONVOLVE:
      active_tool = tools_new_convolve ();
      break;
    case BY_COLOR_SELECT:
      active_tool = tools_new_by_color_select ();
      break;
    case COLOR_BALANCE:
      active_tool = tools_new_color_balance ();
      break;
    case BRIGHTNESS_CONTRAST:
      active_tool = tools_new_brightness_contrast ();
      break;
    case HUE_SATURATION:
      active_tool = tools_new_hue_saturation ();
      break;
    case POSTERIZE:
      active_tool = tools_new_posterize ();
      break;
    case THRESHOLD:
      active_tool = tools_new_threshold ();
      break;
    case CURVES:
      active_tool = tools_new_curves ();
      break;
    case LEVELS:
      active_tool = tools_new_levels ();
      break;
    case HISTOGRAM:
      active_tool = tools_new_histogram_tool ();
      break;
    default:
      return;
    }

  /*  Show the options for the active tool
   */
  tools_options_show_active ();

  /*  Set the paused count variable to 0
   */
  active_tool->paused_count = 0;
  active_tool->gdisp_ptr = NULL;
  active_tool->drawable = NULL;
  active_tool->ID = global_tool_ID++;
  active_tool_type = active_tool->type;
}

void
tools_initialize (ToolType type, GDisplay *gdisp_ptr)
{
  GDisplay *gdisp;

  /* If you're wondering... only these dialog type tools have init functions */
  gdisp = gdisp_ptr;

  if (active_tool)
    active_tool_free ();

  switch (type)
    {
    case RECT_SELECT:
      active_tool = tools_new_rect_select ();
      break;
    case ELLIPSE_SELECT:
      active_tool = tools_new_ellipse_select ();
      break;
    case FREE_SELECT:
      active_tool = tools_new_free_select ();
      break;
    case FUZZY_SELECT:
      active_tool = tools_new_fuzzy_select ();
      break;
    case BEZIER_SELECT:
      active_tool = tools_new_bezier_select ();
      break;
    case ISCISSORS:
      active_tool = tools_new_iscissors ();
      break;
    case MOVE:
      active_tool = tools_new_move_tool ();
      break;
    case MAGNIFY:
      active_tool = tools_new_magnify ();
      break;
    case CROP:
      active_tool = tools_new_crop ();
      break;
    case ROTATE:
      active_tool = tools_new_transform_tool ();
      break;
    case SCALE:
      active_tool = tools_new_transform_tool ();
      break;
    case SHEAR:
      active_tool = tools_new_transform_tool ();
      break;
    case PERSPECTIVE:
      active_tool = tools_new_transform_tool ();
      break;
    case FLIP_HORZ:
      active_tool = tools_new_flip ();
      break;
    case FLIP_VERT:
      active_tool = tools_new_flip ();
      break;
    case TEXT:
      active_tool = tools_new_text ();
      break;
    case COLOR_PICKER:
      active_tool = tools_new_color_picker ();
      break;
    case BUCKET_FILL:
      active_tool = tools_new_bucket_fill ();
      break;
    case BLEND:
      active_tool = tools_new_blend ();
      break;
    case PENCIL:
      active_tool = tools_new_pencil ();
      break;
    case PAINTBRUSH:
      active_tool = tools_new_paintbrush ();
      break;
    case ERASER:
      active_tool = tools_new_eraser ();
      break;
    case AIRBRUSH:
      active_tool = tools_new_airbrush ();
      break;
    case CLONE:
      active_tool = tools_new_clone ();
      break;
    case CONVOLVE:
      active_tool = tools_new_convolve ();
      break;
    case BY_COLOR_SELECT:
      if (gdisp) {
	active_tool = tools_new_by_color_select ();
	by_color_select_initialize (gdisp->gimage);
      } else {
	active_tool = tools_new_rect_select ();
      }
      break;
    case COLOR_BALANCE:
      if (gdisp) {
	active_tool = tools_new_color_balance ();
	color_balance_initialize (gdisp);
      } else {
	active_tool = tools_new_rect_select ();
      }
      break;
    case BRIGHTNESS_CONTRAST:
      if (gdisp) {
	active_tool = tools_new_brightness_contrast ();
	brightness_contrast_initialize (gdisp);
      } else {
	active_tool = tools_new_rect_select ();
      }
      break;
    case HUE_SATURATION:
      if (gdisp) {
	active_tool = tools_new_hue_saturation ();
	hue_saturation_initialize (gdisp);
      } else {
	active_tool = tools_new_rect_select ();
      }
      break;
    case POSTERIZE:
      if (gdisp) {
	active_tool = tools_new_posterize ();
	posterize_initialize (gdisp);
      } else {
	active_tool = tools_new_rect_select ();
      }
      break;
    case THRESHOLD:
      if (gdisp) {
	active_tool = tools_new_threshold ();
	threshold_initialize (gdisp);
      } else {
	active_tool = tools_new_rect_select ();
      }
      break;
    case CURVES:
      if (gdisp) {
	active_tool = tools_new_curves ();
	curves_initialize (gdisp);
      } else {
	active_tool = tools_new_rect_select ();
      }
      break;
    case LEVELS:
      if (gdisp) {
	active_tool = tools_new_levels ();
	levels_initialize (gdisp);
      } else {
	active_tool = tools_new_rect_select ();
      }
      break;
    case HISTOGRAM:
      if (gdisp) {
	active_tool = tools_new_histogram_tool ();
	histogram_tool_initialize (gdisp);
      } else {
	active_tool = tools_new_rect_select ();
      }
      break;
    default:
      break;
    }

  /*  Show the options for the active tool
   */
  if (! active_tool)
    return;
  tools_options_show_active ();

  /*  Set the paused count variable to 0
   */
  if (gdisp)
    active_tool->drawable = gimage_active_drawable (gdisp->gimage);
  else
    active_tool->drawable = NULL;
  active_tool->gdisp_ptr = NULL;
  active_tool->ID = global_tool_ID++;
  active_tool_type = active_tool->type;
}

/*  The tool options live in the toolbox, below the tools: one page per
 *  tool in options_stack, and selecting a tool shows its page.  (They
 *  used to be a separate Tool Options dialog, which was easy to miss.)
 *  Every options widget starts with a label naming it ("Airbrush
 *  Options"); the panel shows that in its heading instead.
 */
GtkWidget *
tools_options_panel_new (void)
{
  GtkWidget *vbox;
  GtkWidget *scrolled;
  guint i;

  vbox = gimp_vbox_new (FALSE, 0);
  gtk_widget_add_css_class (vbox, "tool-options");

  options_title = gtk_label_new ("Tool Options");
  gtk_label_set_xalign (GTK_LABEL (options_title), 0.0);
  gtk_label_set_ellipsize (GTK_LABEL (options_title), PANGO_ELLIPSIZE_END);
  gtk_widget_add_css_class (options_title, "heading");
  gtk_widget_set_margin_start (options_title, 6);
  gtk_widget_set_margin_end (options_title, 6);
  gtk_widget_set_margin_top (options_title, 6);
  gtk_widget_set_margin_bottom (options_title, 4);
  gtk_box_append (GTK_BOX (vbox), options_title);

  scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
				  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gimp_box_pack_start (vbox, scrolled, TRUE, TRUE, 0);

  options_stack = gtk_stack_new ();
  gtk_stack_set_hhomogeneous (GTK_STACK (options_stack), FALSE);
  gtk_stack_set_vhomogeneous (GTK_STACK (options_stack), FALSE);
  gtk_widget_set_valign (options_stack, GTK_ALIGN_START);
  gtk_widget_set_margin_start (options_stack, 6);
  gtk_widget_set_margin_end (options_stack, 6);
  gtk_widget_set_margin_bottom (options_stack, 6);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), options_stack);

  /*  options registered before the panel existed  */
  for (i = 0; i < sizeof (tool_info) / sizeof (tool_info[0]); i++)
    if (tool_info[i].tool_options &&
	gtk_widget_get_parent (tool_info[i].tool_options) == NULL)
      tools_options_add_page (tool_info[i].tool_options);
  tools_options_show_active ();

  return vbox;
}

/*  Shows the tool options: "Tool Options..." in the menus and double
 *  clicking a tool bring the toolbox forward.
 */
void
tools_options_show (void)
{
  if (options_stack)
    {
      toolbox_raise_callback (NULL, NULL);
      tools_options_show_active ();
    }
}

/*  The panel goes with the toolbox; forget it.  */
void
tools_options_free (void)
{
  options_stack = NULL;
  options_title = NULL;
}

static void
tools_options_add_page (GtkWidget *options)
{
  GtkWidget *first = gtk_widget_get_first_child (options);

  if (first && GTK_IS_LABEL (first) &&
      ! g_object_get_data (G_OBJECT (options), "gimp-options-title"))
    {
      g_object_set_data_full (G_OBJECT (options), "gimp-options-title",
			      g_strdup (gtk_label_get_text (GTK_LABEL (first))),
			      g_free);
      gtk_widget_set_visible (first, FALSE);
    }

  gtk_stack_add_child (GTK_STACK (options_stack), options);
}


void
tools_register_options (ToolType   type,
			GtkWidget *options)
{
  /*  need to check whether the widget is already a page...this can
   *  happen because some tools share options such as the
   *  transformation tools
   */
  if (options_stack && gtk_widget_get_parent (options) == NULL)
    tools_options_add_page (options);

  tool_info [(int) type].tool_options = options;
}

void *
tools_register_no_options (ToolType  tool_type,
			   char     *tool_title)
{
  GtkWidget *vbox;
  GtkWidget *label;

  /*  the main vbox  */
  vbox = gimp_vbox_new (FALSE, 1);

  /*  the main label  */
  label = gtk_label_new (tool_title);
  gtk_box_append (GTK_BOX (vbox), label);

  /*  this tool has no special options  */
  label = gtk_label_new ("This tool has no options.");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_add_css_class (label, "dim-label");
  gtk_box_append (GTK_BOX (vbox), label);

  /*  Register this selection options widget with the main tools options dialog  */
  tools_register_options (tool_type, vbox);

  return GINT_TO_POINTER (1);
}

void
active_tool_control (int   action,
		     void *gdisp_ptr)
{
  if (active_tool)
    {
      if (active_tool->gdisp_ptr == gdisp_ptr)
	{
	  switch (action)
	    {
	    case PAUSE :
	      if (active_tool->state == ACTIVE)
		{
		  if (! active_tool->paused_count)
		    {
		      active_tool->state = PAUSED;
		      (* active_tool->control_func) (active_tool, action, gdisp_ptr);
		    }
		}
	      active_tool->paused_count++;

	      break;
	    case RESUME :
	      active_tool->paused_count--;
	      if (active_tool->state == PAUSED)
		{
		  if (! active_tool->paused_count)
		    {
		      active_tool->state = ACTIVE;
		      (* active_tool->control_func) (active_tool, action, gdisp_ptr);
		    }
		}
	      break;
	    case HALT :
	      active_tool->state = INACTIVE;
	      (* active_tool->control_func) (active_tool, action, gdisp_ptr);
	      break;
	    case DESTROY :
              active_tool_free();
              break;
	    }
	}
      else if (action == HALT)
	active_tool->state = INACTIVE;
    }
}


void
standard_arrow_keys_func (Tool        *tool,
			  GimpKeyEvent *kevent,
			  gpointer     gdisp_ptr)
{
}
