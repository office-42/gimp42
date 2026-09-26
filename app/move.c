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
#include "draw_core.h"
#include "edit_selection.h"
#include "errors.h"
#include "floating_sel.h"
#include "gimage_mask.h"
#include "gdisplay.h"
#include "gdisplay_ops.h"
#include "move.h"
#include "undo.h"

typedef struct _MoveTool MoveTool;

struct _MoveTool
{
  Layer *layer;
  Guide *guide;
  int guide_disp;
  DrawCore *core;       /*  the guide being dragged, when guides are hidden  */
};

/*  move tool action functions  */

static void   move_tool_button_press      (Tool *, GimpButtonEvent *, gpointer);
static void   move_tool_button_release    (Tool *, GimpButtonEvent *, gpointer);
static void   move_tool_motion            (Tool *, GimpMotionEvent *, gpointer);
static void   move_tool_cursor_update     (Tool *, GimpMotionEvent *, gpointer);
static void   move_tool_control		  (Tool *, int, gpointer);
static void   move_draw_guide             (Tool *);


static void *move_options = NULL;


/*  move action functions  */

static void
move_tool_button_press (Tool           *tool,
			GimpButtonEvent *bevent,
			gpointer        gdisp_ptr)
{
  GDisplay * gdisp;
  MoveTool * move;
  Layer * layer;
  Guide * guide;
  int x, y;

  gdisp = (GDisplay *) gdisp_ptr;
  move = (MoveTool *) tool->private;

  tool->gdisp_ptr = gdisp_ptr;
  move->layer = NULL;
  move->guide = NULL;
  move->guide_disp = -1;

  gdisplay_untransform_coords (gdisp, bevent->x, bevent->y, &x, &y, FALSE, FALSE);
 
  if (bevent->state & GDK_ALT_MASK)
    {
      init_edit_selection (tool, gdisp_ptr, bevent, MaskTranslate);
      tool->state = ACTIVE;
    }
  else if (bevent->state & GDK_SHIFT_MASK)
    {
      init_edit_selection (tool, gdisp_ptr, bevent, LayerTranslate);
      tool->state = ACTIVE;
    }
  else
    {
      if (gdisp->draw_guides && (guide = gdisplay_find_guide (gdisp, bevent->x, bevent->y)))
	{
	  undo_push_guide (gdisp->gimage, guide);

	  move->guide = NULL;

	  gdisplays_expose_guide (gdisp->gimage->ID, guide);
	  gimage_remove_guide (gdisp->gimage, guide);
	  gdisplay_flush (gdisp);
	  gimage_add_guide (gdisp->gimage, guide);

	  move->guide = guide;
	  move->guide_disp = gdisp->ID;

	  tool->scroll_lock = TRUE;
	  tool->state = ACTIVE;

	  move_tool_motion (tool, NULL, gdisp);
	}
      else if ((layer = gimage_pick_correlate_layer (gdisp->gimage, x, y)))
	{
	  /*  If there is a floating selection, and this aint it, use the move tool  */
	  if (gimage_floating_sel (gdisp->gimage) && !layer_is_floating_sel (layer))
	    move->layer = gimage_floating_sel (gdisp->gimage);
	  /*  Otherwise, init the edit selection  */
	  else
	    {
	      gimage_set_active_layer (gdisp->gimage, layer);
	      init_edit_selection (tool, gdisp_ptr, bevent, LayerTranslate);
	    }
	  tool->state = ACTIVE;
	}
    }

  /*  show the guide being dragged  */
  if (tool->state == ACTIVE && move->guide)
    draw_core_start (move->core, gdisp->canvas, tool);
}

/*  The guide being dragged.  The display draws it with the other
 *  guides; this only shows it when guides are hidden, the way the
 *  inverted line used to.
 */
static void
move_draw_guide (Tool *tool)
{
  GDisplay *gdisp;
  MoveTool *move;
  Guide *guide;
  int x1, y1;
  int x2, y2;
  int w, h;
  int x, y;

  gdisp = (GDisplay *) tool->gdisp_ptr;
  move = (MoveTool *) tool->private;
  guide = move->guide;

  if (!gdisp || !guide || gdisp->draw_guides || guide->position == -1)
    return;

  gdisplay_transform_coords (gdisp, gdisp->gimage->width,
			     gdisp->gimage->height, &x2, &y2, FALSE);

  w = gdisp->disp_width;
  h = gdisp->disp_height;

  switch (guide->orientation) {
  case HORIZONTAL_GUIDE:
    gdisplay_transform_coords (gdisp, 0, guide->position, &x1, &y, FALSE);
    if (x1 < 0) x1 = 0;
    if (x2 > w) x2 = w;

    draw_core_line (move->core, x1, y, x2, y);
    break;
  case VERTICAL_GUIDE:
    gdisplay_transform_coords (gdisp, guide->position, 0, &x, &y1, FALSE);
    if (y1 < 0) y1 = 0;
    if (y2 > h) y2 = h;

    draw_core_line (move->core, x, y1, x, y2);
    break;
  }
}

static void
move_tool_button_release (Tool           *tool,
			  GimpButtonEvent *bevent,
			  gpointer        gdisp_ptr)
{
  MoveTool * move;
  GDisplay * gdisp;
  int remove_guide;
  int x1, y1;
  int x2, y2;

  gdisp = (GDisplay *) gdisp_ptr;
  move = (MoveTool *) tool->private;


  tool->state = INACTIVE;

  if (move->guide)
    {
      tool->scroll_lock = FALSE;

      remove_guide = FALSE;
      gdisplay_untransform_coords (gdisp, 0, 0, &x1, &y1, FALSE, FALSE);
      gdisplay_untransform_coords (gdisp, gdisp->disp_width, gdisp->disp_height, &x2, &y2, FALSE, FALSE);

      if (x1 < 0) x1 = 0;
      if (y1 < 0) y1 = 0;
      if (x2 > gdisp->gimage->width) x2 = gdisp->gimage->width;
      if (y2 > gdisp->gimage->height) y2 = gdisp->gimage->height;

      switch (move->guide->orientation)
	{
	case HORIZONTAL_GUIDE:
	  if ((move->guide->position < y1) || (move->guide->position > y2))
	    remove_guide = TRUE;
	  break;
	case VERTICAL_GUIDE:
	  if ((move->guide->position < x1) || (move->guide->position > x2))
	    remove_guide = TRUE;
	  break;
	}

      gdisplays_expose_guide (gdisp->gimage->ID, move->guide);

      draw_core_stop (move->core, tool);

      if (remove_guide)
	{
	  move->guide->position = -1;
	  move->guide = NULL;
	  move->guide_disp = -1;
	}
      else
	{
	  move_tool_motion (tool, NULL, gdisp_ptr);
	}

      selection_resume (gdisp->select);
      gdisplays_flush ();

      if (move->guide)
	gdisplay_draw_guide (gdisp, move->guide, TRUE);
    }
  else
    {
      /*  First take care of the case where the user "cancels" the action  */
      if (! (bevent->state & GDK_BUTTON3_MASK))
	{
	  if (move->layer)
	    {
	      floating_sel_anchor (move->layer);
	      gdisplays_flush ();
	    }
	}
    }
}

static void
move_tool_motion (Tool           *tool,
		  GimpMotionEvent *mevent,
		  gpointer        gdisp_ptr)

{
  GDisplay *gdisp;
  MoveTool *private;
  int x, y;

  gdisp = gdisp_ptr;
  private = tool->private;

  if (private->guide && mevent)
    {
      gdisplay_untransform_coords (gdisp, mevent->x, mevent->y,
				   &x, &y, TRUE, FALSE);

      if (private->guide->orientation == HORIZONTAL_GUIDE)
	private->guide->position = y;
      else
	private->guide->position = x;

      /*  the display draws the guides, the core draws hidden ones  */
      gtk_widget_queue_draw (gdisp->canvas);
      draw_core_queue_draw (private->core);
    }
}

static void
move_tool_cursor_update (Tool           *tool,
			 GimpMotionEvent *mevent,
			 gpointer        gdisp_ptr)
{
  MoveTool *move;
  GDisplay *gdisp;
  Guide *guide;
  Layer *layer;
  int x, y;

  move = tool->private;
  gdisp = (GDisplay *) gdisp_ptr;
  gdisplay_untransform_coords (gdisp, mevent->x, mevent->y, &x, &y, FALSE, FALSE);

  if (mevent->state & GDK_ALT_MASK)
    gdisplay_install_tool_cursor (gdisp, GIMP_CURSOR_DIAMOND_CROSS);
  else if (mevent->state & GDK_SHIFT_MASK)
    gdisplay_install_tool_cursor (gdisp, GIMP_CURSOR_FLEUR);
  else
    {
      if (gdisp->draw_guides && (guide = gdisplay_find_guide (gdisp, mevent->x, mevent->y)))
	{
	  tool->gdisp_ptr = gdisp_ptr;
	  gdisplay_install_tool_cursor (gdisp, GIMP_CURSOR_HAND2);

	  if (tool->state != ACTIVE)
	    {
	      if (move->guide)
		{
		  gdisp = gdisplay_get_ID (move->guide_disp);
		  if (gdisp)
		    gdisplay_draw_guide (gdisp, move->guide, FALSE);
		}

	      gdisp = gdisp_ptr;
	      gdisplay_draw_guide (gdisp, guide, TRUE);
	      move->guide = guide;
	      move->guide_disp = gdisp->ID;
	    }
	}
      else if ((layer = gimage_pick_correlate_layer (gdisp->gimage, x, y)))
	{
	  /*  if there is a floating selection, and this aint it...  */
	  if (gimage_floating_sel (gdisp->gimage) && !layer_is_floating_sel (layer))
	    gdisplay_install_tool_cursor (gdisp, GIMP_CURSOR_SB_DOWN_ARROW);
	  else if (layer == gdisp->gimage->active_layer)
	    gdisplay_install_tool_cursor (gdisp, GIMP_CURSOR_FLEUR);
	  else
	    gdisplay_install_tool_cursor (gdisp, GIMP_CURSOR_HAND2);
	}
      else
	gdisplay_install_tool_cursor (gdisp, GIMP_CURSOR_TOP_LEFT_ARROW);
    }
}


static void
move_tool_control (Tool     *tool,
		   int       action,
		   gpointer  gdisp_ptr)
{
  MoveTool *move;

  move = tool->private;

  switch (action)
    {
    case PAUSE :
      break;
    case RESUME :
      if (move->guide)
	gdisplay_draw_guide (gdisp_ptr, move->guide, TRUE);
      break;
    case HALT :
      draw_core_stop (move->core, tool);
      break;
    }
}

void
move_tool_start_hguide (Tool *tool,
			void *data)
{
  MoveTool *private;
  GDisplay *gdisp;

  gdisp = data;

  selection_pause (gdisp->select);

  tool->gdisp_ptr = gdisp;
  tool->scroll_lock = TRUE;

  private = tool->private;
  private->guide = gimage_add_hguide (gdisp->gimage);

  tool->state = ACTIVE;

  draw_core_start (private->core, gdisp->canvas, tool);

  undo_push_guide (gdisp->gimage, private->guide);
}

void
move_tool_start_vguide (Tool *tool,
			void *data)
{
  MoveTool *private;
  GDisplay *gdisp;

  gdisp = data;

  selection_pause (gdisp->select);

  tool->gdisp_ptr = gdisp;
  tool->scroll_lock = TRUE;

  private = tool->private;
  private->guide = gimage_add_vguide (gdisp->gimage);

  tool->state = ACTIVE;

  draw_core_start (private->core, gdisp->canvas, tool);

  undo_push_guide (gdisp->gimage, private->guide);
}

Tool *
tools_new_move_tool ()
{
  Tool * tool;
  MoveTool * private;

  if (! move_options)
    move_options = tools_register_no_options (MOVE, "Move Tool Options");

  tool = (Tool *) g_malloc (sizeof (Tool));
  private = (MoveTool *) g_malloc (sizeof (MoveTool));

  tool->type = MOVE;
  tool->state = INACTIVE;
  tool->scroll_lock = 0;   /*  Allow scrolling  */
  tool->auto_snap_to = FALSE;
  tool->private = (void *) private;
  tool->button_press_func = move_tool_button_press;
  tool->button_release_func = move_tool_button_release;
  tool->motion_func = move_tool_motion;
  tool->arrow_keys_func = edit_sel_arrow_keys_func;
  tool->cursor_update_func = move_tool_cursor_update;
  tool->control_func = move_tool_control;
  tool->preserve = TRUE;

  private->layer = NULL;
  private->guide = NULL;
  private->guide_disp = -1;
  private->core = draw_core_new (move_draw_guide);

  return tool;
}


void
tools_free_move_tool (Tool *tool)
{
  MoveTool * move;

  move = (MoveTool *) tool->private;

  if (tool->gdisp_ptr)
    {
      if (move->guide)
	gdisplay_draw_guide (tool->gdisp_ptr, move->guide, FALSE);
    }

  if (move->core->draw_state != INVISIBLE)
    draw_core_stop (move->core, tool);
  draw_core_free (move->core);
  g_free (move);
}
