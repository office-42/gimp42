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
#include "cursorutil.h"
#include "disp_callbacks.h"
#include "gdisplay.h"
#include "general.h"
#include "gimprc.h"
#include "gimpruler.h"
#include "interface.h"
#include "layer_select.h"
#include "menus.h"
#include "move.h"
#include "scale.h"
#include "scroll.h"
#include "tools.h"
#include "gimage.h"


/*  The canvas gets its events through one GtkEventControllerLegacy, so
 *  the dispatch below can stay the event switch it always was.  GTK 4
 *  reports positions relative to the window's surface; they are turned
 *  into canvas coordinates first.
 */

/*  Set while a guide is being dragged out of a ruler: the ruler keeps
 *  getting the pointer events, and passes them on to its display.
 */
static GDisplay *guide_drag_gdisp = NULL;

static gboolean scrolled = FALSE;


static gboolean
event_to_widget (GtkWidget *widget,
		 GdkEvent  *event,
		 double    *x,
		 double    *y)
{
  GtkNative *native;
  graphene_point_t in, out;
  double ex, ey;
  double sx, sy;

  if (!gdk_event_get_position (event, &ex, &ey))
    return FALSE;

  native = gtk_widget_get_native (widget);
  if (!native)
    return FALSE;

  gtk_native_get_surface_transform (native, &sx, &sy);

  in = GRAPHENE_POINT_INIT (ex - sx, ey - sy);
  if (!gtk_widget_compute_point (GTK_WIDGET (native), widget, &in, &out))
    return FALSE;

  *x = out.x;
  *y = out.y;

  return TRUE;
}

static gdouble
event_pressure (GdkEvent *event)
{
  gdouble pressure;

  if (gdk_event_get_axis (event, GDK_AXIS_PRESSURE, &pressure))
    return pressure;

  return 1.0;
}

static void
dispatch_button_press (GDisplay        *gdisp,
		       GimpButtonEvent *bevent)
{
  gint tx, ty;

  switch (bevent->button)
    {
    case 1:
      if (active_tool && ((active_tool->type == MOVE) ||
			  !gimage_is_empty (gdisp->gimage)))
	{
	  if (active_tool->auto_snap_to)
	    {
	      gdisplay_snap_point (gdisp, bevent->x, bevent->y, &tx, &ty);
	      bevent->x = tx;
	      bevent->y = ty;
	    }

	  /* reset the current tool if we're changing drawables */
	  if (active_tool->drawable)
	    {
	      if (((gimage_active_drawable(gdisp->gimage)) !=
		   active_tool->drawable) &&
		  !active_tool->preserve)
		tools_initialize (active_tool->type, gdisp);
	    }
	  else
	    active_tool->drawable = gimage_active_drawable(gdisp->gimage);

	  (* active_tool->button_press_func) (active_tool, bevent, gdisp);
	}
      break;

    case 2:
      scrolled = TRUE;
      start_grab_and_scroll (gdisp, bevent);
      break;

    case 3:
      gdisplay_set_menu_sensitivity (gdisp);
      if (gdisp->popup)
	{
	  GdkRectangle rect = { (int) bevent->x, (int) bevent->y, 1, 1 };

	  gtk_popover_set_pointing_to (GTK_POPOVER (gdisp->popup), &rect);
	  gtk_popover_popup (GTK_POPOVER (gdisp->popup));
	}
      break;

    default:
      break;
    }
}

static void
dispatch_button_release (GDisplay        *gdisp,
			 GimpButtonEvent *bevent)
{
  gint tx, ty;

  switch (bevent->button)
    {
    case 1:
      if (active_tool && ((active_tool->type == MOVE) ||
			  !gimage_is_empty (gdisp->gimage)))
	if (active_tool->state == ACTIVE)
	  {
	    if (active_tool->auto_snap_to)
	      {
		gdisplay_snap_point (gdisp, bevent->x, bevent->y, &tx, &ty);
		bevent->x = tx;
		bevent->y = ty;
	      }

	    (* active_tool->button_release_func) (active_tool, bevent, gdisp);
	  }
      break;

    case 2:
      scrolled = FALSE;
      end_grab_and_scroll (gdisp, bevent);
      break;

    default:
      break;
    }
}

static void
dispatch_motion (GDisplay        *gdisp,
		 GimpMotionEvent *mevent)
{
  gint tx, ty;

  if (active_tool && ((active_tool->type == MOVE) ||
		      !gimage_is_empty (gdisp->gimage)) &&
      (mevent->state & GDK_BUTTON1_MASK))
    {
      if (active_tool->state == ACTIVE)
	{
	  /*  if the first mouse button is down, check for automatic
	   *  scrolling...
	   */
	  if (!active_tool->scroll_lock)
	    {
	      if (mevent->x < 0 || mevent->y < 0 ||
		  mevent->x > gdisp->disp_width ||
		  mevent->y > gdisp->disp_height)
		scroll_to_pointer_position (gdisp, mevent);
	    }

	  if (active_tool->auto_snap_to)
	    {
	      gdisplay_snap_point (gdisp, mevent->x, mevent->y, &tx, &ty);
	      mevent->x = tx;
	      mevent->y = ty;
	    }

	  (* active_tool->motion_func) (active_tool, mevent, gdisp);
	}
    }
  else if ((mevent->state & GDK_BUTTON2_MASK) && scrolled)
    {
      grab_and_scroll (gdisp, mevent);
    }
}

static gboolean
dispatch_key_press (GDisplay     *gdisp,
		    GimpKeyEvent *kevent,
		    guint        *state)
{
  gboolean return_val = FALSE;

  switch (kevent->keyval)
    {
    case GDK_KEY_Left: case GDK_KEY_Right:
    case GDK_KEY_Up: case GDK_KEY_Down:
      if (active_tool && !gimage_is_empty (gdisp->gimage))
	(* active_tool->arrow_keys_func) (active_tool, kevent, gdisp);
      return_val = TRUE;
      break;

    case GDK_KEY_Tab:
      if (kevent->state & GDK_ALT_MASK && !gimage_is_empty (gdisp->gimage))
	layer_select_init (gdisp->gimage, 1, kevent->time);
      if (kevent->state & GDK_CONTROL_MASK && !gimage_is_empty (gdisp->gimage))
	layer_select_init (gdisp->gimage, -1, kevent->time);
      return_val = TRUE;
      break;

      /*  Update the state based on modifiers being pressed  */
    case GDK_KEY_Alt_L: case GDK_KEY_Alt_R:
      *state |= GDK_ALT_MASK;
      break;
    case GDK_KEY_Shift_L: case GDK_KEY_Shift_R:
      *state |= GDK_SHIFT_MASK;
      break;
    case GDK_KEY_Control_L: case GDK_KEY_Control_R:
      *state |= GDK_CONTROL_MASK;
      break;
    }

  /*  We need this here in case of accelerators  */
  gdisplay_set_menu_sensitivity (gdisp);

  return return_val;
}

static void
dispatch_key_release (GDisplay     *gdisp,
		      GimpKeyEvent *kevent,
		      guint        *state)
{
  switch (kevent->keyval)
    {
    case GDK_KEY_Alt_L: case GDK_KEY_Alt_R:
      *state &= ~GDK_ALT_MASK;
      break;
    case GDK_KEY_Shift_L: case GDK_KEY_Shift_R:
      *state &= ~GDK_SHIFT_MASK;
      break;
    case GDK_KEY_Control_L: case GDK_KEY_Control_R:
      *state &= ~GDK_CONTROL_MASK;
      break;
    }
}

static void
dispatch_scroll (GDisplay *gdisp,
		 GdkEvent *event)
{
  GdkScrollDirection direction;
  GdkModifierType    state;
  double dx = 0.0, dy = 0.0;

  state = gdk_event_get_modifier_state (event);
  direction = gdk_scroll_event_get_direction (event);

  switch (direction)
    {
    case GDK_SCROLL_UP:     dy = -1.0; break;
    case GDK_SCROLL_DOWN:   dy =  1.0; break;
    case GDK_SCROLL_LEFT:   dx = -1.0; break;
    case GDK_SCROLL_RIGHT:  dx =  1.0; break;
    case GDK_SCROLL_SMOOTH: gdk_scroll_event_get_deltas (event, &dx, &dy); break;
    }

  /*  Control and the wheel zooms, as in every other image program  */
  if (state & GDK_CONTROL_MASK)
    {
      if (dy < 0)
	change_scale (gdisp, ZOOMIN);
      else if (dy > 0)
	change_scale (gdisp, ZOOMOUT);
      return;
    }

  if (state & GDK_SHIFT_MASK)
    {
      dx = dy;
      dy = 0.0;
    }

  if (dx != 0.0)
    gtk_adjustment_set_value (gdisp->hsbdata,
			      gtk_adjustment_get_value (gdisp->hsbdata) +
			      dx * gdisp->disp_width / 8.0);
  if (dy != 0.0)
    gtk_adjustment_set_value (gdisp->vsbdata,
			      gtk_adjustment_get_value (gdisp->vsbdata) +
			      dy * gdisp->disp_height / 8.0);
}

/*  Handles event at canvas position (x, y).  */
static gboolean
gdisplay_dispatch_event (GDisplay *gdisp,
			 GdkEvent *event,
			 double    x,
			 double    y)
{
  GimpButtonEvent bevent;
  GimpMotionEvent mevent;
  GimpKeyEvent    kevent;
  guint state = 0;
  gboolean return_val = FALSE;
  gboolean update_cursor = TRUE;

  switch (gdk_event_get_event_type (event))
    {
    case GDK_BUTTON_PRESS:
      bevent.type     = GIMP_BUTTON_PRESS;
      bevent.time     = gdk_event_get_time (event);
      bevent.x        = x;
      bevent.y        = y;
      bevent.pressure = event_pressure (event);
      bevent.state    = state = gdk_event_get_modifier_state (event);
      bevent.button   = gdk_button_event_get_button (event);

      gdisplay_set_active (gdisp);
      gtk_widget_grab_focus (gdisp->canvas);

      dispatch_button_press (gdisp, &bevent);
      return_val = TRUE;
      break;

    case GDK_BUTTON_RELEASE:
      bevent.type     = GIMP_BUTTON_RELEASE;
      bevent.time     = gdk_event_get_time (event);
      bevent.x        = x;
      bevent.y        = y;
      bevent.pressure = event_pressure (event);
      bevent.button   = gdk_button_event_get_button (event);
      bevent.state    = state = gdk_event_get_modifier_state (event);

      dispatch_button_release (gdisp, &bevent);
      return_val = TRUE;

      /*  the release state still has the released button in it  */
      if (bevent.button >= 1 && bevent.button <= 5)
	state &= ~(GDK_BUTTON1_MASK << (bevent.button - 1));
      break;

    case GDK_MOTION_NOTIFY:
      mevent.time     = gdk_event_get_time (event);
      mevent.x        = x;
      mevent.y        = y;
      mevent.pressure = event_pressure (event);
      mevent.state    = state = gdk_event_get_modifier_state (event);
      mevent.is_hint  = FALSE;

      gimp_ruler_set_pointer (GIMP_RULER (gdisp->hrule), x);
      gimp_ruler_set_pointer (GIMP_RULER (gdisp->vrule), y);

      dispatch_motion (gdisp, &mevent);
      break;

    case GDK_KEY_PRESS:
      kevent.time   = gdk_event_get_time (event);
      kevent.state  = state = gdk_event_get_modifier_state (event);
      kevent.keyval = gdk_key_event_get_keyval (event);

      return_val = dispatch_key_press (gdisp, &kevent, &state);
      update_cursor = FALSE;
      break;

    case GDK_KEY_RELEASE:
      kevent.time   = gdk_event_get_time (event);
      kevent.state  = state = gdk_event_get_modifier_state (event);
      kevent.keyval = gdk_key_event_get_keyval (event);

      dispatch_key_release (gdisp, &kevent, &state);
      return_val = TRUE;
      update_cursor = FALSE;
      break;

    case GDK_SCROLL:
      dispatch_scroll (gdisp, event);
      return_val = TRUE;
      update_cursor = FALSE;
      break;

    default:
      update_cursor = FALSE;
      break;
    }

  if (update_cursor && no_cursor_updating == 0)
    {
      if (active_tool && !gimage_is_empty (gdisp->gimage) &&
	  !(state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK)))
	{
	  GimpMotionEvent me;

	  me.time = gdk_event_get_time (event);
	  me.x = x;  me.y = y;
	  me.pressure = 1.0;
	  me.state = state;
	  me.is_hint = FALSE;
	  (* active_tool->cursor_update_func) (active_tool, &me, gdisp);
	}
      else if (gimage_is_empty (gdisp->gimage))
	gdisplay_install_tool_cursor (gdisp, GIMP_CURSOR_TOP_LEFT_ARROW);
    }

  return return_val;
}

gboolean
gdisplay_canvas_events (GtkEventControllerLegacy *controller,
			GdkEvent                 *event,
			gpointer                  data)
{
  GDisplay *gdisp = data;
  double x = 0.0, y = 0.0;

  /*  Nothing to do until the canvas has its size (and the display its
   *  selection); see gdisplay_canvas_resize.
   */
  if (!gdisp->select)
    return FALSE;

  switch (gdk_event_get_event_type (event))
    {
    case GDK_BUTTON_PRESS:
    case GDK_BUTTON_RELEASE:
    case GDK_MOTION_NOTIFY:
    case GDK_SCROLL:
      if (!event_to_widget (gdisp->canvas, event, &x, &y))
	return FALSE;
      break;

    case GDK_KEY_PRESS:
    case GDK_KEY_RELEASE:
      break;

    default:
      return FALSE;
    }

  return gdisplay_dispatch_event (gdisp, event, x, y);
}

void
gdisplay_canvas_resize (GtkDrawingArea *area,
			int             width,
			int             height,
			gpointer        data)
{
  GDisplay *gdisp = data;

  /*  The first size the canvas gets is when the display comes to life.  */
  if (!gdisp->select)
    {
      gdisp->disp_width = width;
      gdisp->disp_height = height;

      /*  create the selection object  */
      gdisp->select = selection_create (gdisp->canvas, gdisp,
					gdisp->gimage->height,
					gdisp->gimage->width, marching_speed);

      /*  set up the scrollbar observers  */
      g_signal_connect (gdisp->hsbdata, "value-changed",
			G_CALLBACK (scrollbar_horz_update), gdisp);
      g_signal_connect (gdisp->vsbdata, "value-changed",
			G_CALLBACK (scrollbar_vert_update), gdisp);

      /*  setup scale properly  */
      setup_scale (gdisp);

      gdisplay_expose_full (gdisp);
      gdisplays_flush ();
      return;
    }

  if ((gdisp->disp_width != width) || (gdisp->disp_height != height))
    {
      gdisp->disp_width = width;
      gdisp->disp_height = height;
      resize_display (gdisp, 0, FALSE);
      gdisplay_expose_full (gdisp);
      gdisplays_flush ();
    }
}

void
gdisplay_canvas_draw_func (GtkDrawingArea *area,
			   cairo_t        *cr,
			   int             width,
			   int             height,
			   gpointer        data)
{
  gdisplay_canvas_draw ((GDisplay *) data, cr, width, height);
}


/*  Rulers: pressing button 1 on one pulls out a new guide, which then
 *  follows the pointer (the move tool does the work) until the button
 *  is released.
 */
static gboolean
gdisplay_ruler_events (GtkEventControllerLegacy *controller,
		       GdkEvent                 *event,
		       GDisplay                 *gdisp,
		       gboolean                  horizontal)
{
  GdkEventType type = gdk_event_get_event_type (event);
  double x, y;

  if (!gdisp->select)
    return FALSE;

  if (type == GDK_BUTTON_PRESS && gdk_button_event_get_button (event) == 1)
    {
      gdisplay_set_active (gdisp);
      tools_select_widget (MOVE);
      if (horizontal)
	move_tool_start_hguide (active_tool, gdisp);
      else
	move_tool_start_vguide (active_tool, gdisp);
      guide_drag_gdisp = gdisp;
      return TRUE;
    }

  if (guide_drag_gdisp != gdisp)
    return FALSE;

  if (type == GDK_MOTION_NOTIFY || type == GDK_BUTTON_RELEASE)
    {
      if (!event_to_widget (gdisp->canvas, event, &x, &y))
	return FALSE;

      gdisplay_dispatch_event (gdisp, event, x, y);

      if (type == GDK_BUTTON_RELEASE)
	guide_drag_gdisp = NULL;

      return TRUE;
    }

  return FALSE;
}

gboolean
gdisplay_hruler_events (GtkEventControllerLegacy *controller,
			GdkEvent                 *event,
			gpointer                  data)
{
  return gdisplay_ruler_events (controller, event, data, TRUE);
}

gboolean
gdisplay_vruler_events (GtkEventControllerLegacy *controller,
			GdkEvent                 *event,
			gpointer                  data)
{
  return gdisplay_ruler_events (controller, event, data, FALSE);
}
