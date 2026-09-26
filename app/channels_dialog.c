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
#include <stdio.h>
#include <stdlib.h>
#include "appenv.h"
#include "buildmenu.h"
#include "channels_dialog.h"
#include "colormaps.h"
#include "color_panel.h"
#include "drawable.h"
#include "errors.h"
#include "gdisplay.h"
#include "gimage.h"
#include "gimage_mask.h"
#include "gimprc.h"
#include "general.h"
#include "interface.h"
#include "layers_dialogP.h"
#include "ops_buttons.h"
#include "paint_funcs.h"
#include "palette.h"
#include "resize.h"

#include "tools/eye.xbm"
#include "tools/channel.xbm"
#include "tools/new.xpm"
#include "tools/new_is.xpm"
#include "tools/raise.xpm"
#include "tools/raise_is.xpm"
#include "tools/lower.xpm"
#include "tools/lower_is.xpm"
#include "tools/duplicate.xpm"
#include "tools/duplicate_is.xpm"
#include "tools/delete.xpm"
#include "tools/delete_is.xpm"

#include "channel_pvt.h"


#define CHANNEL_LIST_WIDTH 200
#define CHANNEL_LIST_HEIGHT 150

#define COMPONENT_BASE_ID 0x10000000

/*  The list rows keep their selection in the SELECTED state flag; the
 *  list itself does no selecting.  Like the GTK 1 list in multiple
 *  selection mode, a click toggles the row it is on.
 */
#define LC_ROW_SELECTED(w) \
  ((gtk_widget_get_state_flags (w) & GTK_STATE_FLAG_SELECTED) != 0)

#define CHANNEL_WIDGET_KEY "gimp-channel-widget"

typedef struct _ChannelWidget ChannelWidget;

struct _ChannelWidget {
  GtkWidget *eye_widget;
  GtkWidget *clip_widget;
  GtkWidget *channel_preview;
  GtkWidget *list_item;
  GtkWidget *label;

  GImage *gimage;
  Channel *channel;
  cairo_surface_t *channel_pixmap;
  ChannelType type;
  int ID;
  int width, height;
  int visited;
};

typedef struct _ChannelsDialog ChannelsDialog;

struct _ChannelsDialog {
  GtkWidget *vbox;
  GtkWidget *channel_list;
  GtkWidget *preview;
  GtkWidget *ops_menu;

  int num_components;
  int base_type;
  ChannelType components[3];
  double ratio;
  int image_width, image_height;
  int gimage_width, gimage_height;

  /*  state information  */
  int gimage_id;
  Channel * active_channel;
  Layer *floating_sel;
  GSList *channel_widgets;
};

/*  channels dialog widget routines  */
static void channels_dialog_preview_extents (void);
static void channels_dialog_set_menu_sensitivity (void);
static void channels_dialog_set_channel (ChannelWidget *);
static void channels_dialog_unset_channel (ChannelWidget *);
static void channels_dialog_position_channel (ChannelWidget *, int);
static void channels_dialog_add_channel (Channel *);
static void channels_dialog_remove_channel (ChannelWidget *);
static void channels_list_clear (void);

/*  channels dialog menu callbacks  */
static void channels_dialog_new_channel_callback (GtkWidget *, gpointer);
static void channels_dialog_raise_channel_callback (GtkWidget *, gpointer);
static void channels_dialog_lower_channel_callback (GtkWidget *, gpointer);
static void channels_dialog_duplicate_channel_callback (GtkWidget *, gpointer);
static void channels_dialog_delete_channel_callback (GtkWidget *, gpointer);
static void channels_dialog_channel_to_sel_callback (GtkWidget *, gpointer);

/*  channel widget function prototypes  */
static ChannelWidget *channel_widget_get_ID (Channel *);
static ChannelWidget *channel_widget_from (GtkWidget *);
static ChannelWidget *create_channel_widget (GImage *, Channel *, ChannelType);
static void channel_widget_delete (ChannelWidget *);
static void channel_widget_select_update (ChannelWidget *);
static void channel_widget_row_pressed (GtkGestureClick *, int, double, double, gpointer);
static void channel_widget_button_begin (GtkGestureDrag *, double, double, gpointer);
static void channel_widget_button_update (GtkGestureDrag *, double, double, gpointer);
static void channel_widget_button_end (GtkGestureDrag *, double, double, gpointer);
static void channel_widget_preview_pressed (GtkGestureClick *, int, double, double, gpointer);
static void channel_widget_preview_draw (GtkDrawingArea *, cairo_t *, int, int, gpointer);
static void channel_widget_eye_draw (GtkDrawingArea *, cairo_t *, int, int, gpointer);
static void channel_widget_preview_redraw (ChannelWidget *);
static void channel_widget_no_preview_redraw (ChannelWidget *, cairo_t *);
static void channel_widget_eye_redraw (ChannelWidget *);
static void channel_widget_exclusive_visible (ChannelWidget *);
static void channel_widget_channel_flush (GtkWidget *, gpointer);

/*  assorted query dialogs  */
static void channels_dialog_new_channel_query (int);
static void channels_dialog_edit_channel_query (ChannelWidget *);


/*  Only one channels dialog  */
static ChannelsDialog *channelsD = NULL;

static int suspend_gimage_notify = 0;

static MenuItem channels_ops[] =
{
  { "New Channel", 'N', GDK_CONTROL_MASK,
    channels_dialog_new_channel_callback, NULL, NULL, NULL },
  { "Raise Channel", 'F', GDK_CONTROL_MASK,
    channels_dialog_raise_channel_callback, NULL, NULL, NULL },
  { "Lower Channel", 'B', GDK_CONTROL_MASK,
    channels_dialog_lower_channel_callback, NULL, NULL, NULL },
  { "Duplicate Channel", 'C', GDK_CONTROL_MASK,
    channels_dialog_duplicate_channel_callback, NULL, NULL, NULL },
  { "Delete Channel", 'X', GDK_CONTROL_MASK,
    channels_dialog_delete_channel_callback, NULL, NULL, NULL },
  { "Channel To Selection", 'S', GDK_CONTROL_MASK,
    channels_dialog_channel_to_sel_callback, NULL, NULL, NULL },
  { NULL, 0, 0, NULL, NULL, NULL, NULL },
};

/* the ops buttons */
static OpsButton channels_ops_buttons[] =
{
  { new_xpm, new_is_xpm, channels_dialog_new_channel_callback, "New Channel", NULL },
  { raise_xpm, raise_is_xpm, channels_dialog_raise_channel_callback, "Raise Channel", NULL },
  { lower_xpm, lower_is_xpm, channels_dialog_lower_channel_callback, "Lower Channel", NULL },
  { duplicate_xpm, duplicate_is_xpm, channels_dialog_duplicate_channel_callback, "Duplicate Channel", NULL },
  { delete_xpm, delete_is_xpm, channels_dialog_delete_channel_callback, "Delete Channel", NULL },
  { NULL, NULL, NULL, NULL, NULL }
};

/**************************************/
/*  Public channels dialog functions  */
/**************************************/

GtkWidget *
channels_dialog_create ()
{
  GtkWidget *vbox;
  GtkWidget *listbox;
  GtkWidget *button_box;

  if (!channelsD)
    {
      channelsD = g_malloc (sizeof (ChannelsDialog));
      channelsD->preview = NULL;
      channelsD->gimage_id = -1;
      channelsD->active_channel = NULL;
      channelsD->floating_sel = NULL;
      channelsD->channel_widgets = NULL;

      if (preview_size)
	{
	  /*  a scratch buffer for render_preview (), never shown  */
	  channelsD->preview = gimp_preview_new (GIMP_PREVIEW_GRAYSCALE);
	  g_object_ref_sink (channelsD->preview);
	  gimp_preview_size (GIMP_PREVIEW (channelsD->preview), preview_size, preview_size);
	}

      /*  The main vbox  */
      channelsD->vbox = vbox = gimp_vbox_new (FALSE, 1);
      gimp_container_set_border_width (vbox, 2);

      /*  The channels commands popup menu  */
      channelsD->ops_menu = lc_ops_menu_new (channels_ops, vbox);

      /*  The channels listbox  */
      listbox = gtk_scrolled_window_new ();
      gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (listbox),
				      GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
      gtk_widget_set_size_request (listbox, CHANNEL_LIST_WIDTH, CHANNEL_LIST_HEIGHT);
      gimp_box_pack_start (vbox, listbox, TRUE, TRUE, 2);

      channelsD->channel_list = gtk_list_box_new ();
      gtk_list_box_set_selection_mode (GTK_LIST_BOX (channelsD->channel_list),
				       GTK_SELECTION_NONE);
      gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (listbox), channelsD->channel_list);

      /* The ops buttons */

      button_box = ops_button_box_new (lc_shell, channels_ops_buttons);

      gimp_box_pack_start (vbox, button_box, FALSE, FALSE, 2);
    }

  return channelsD->vbox;
}


void
channels_dialog_flush ()
{
  GImage *gimage;
  Channel *channel;
  ChannelWidget *cw;
  GSList *list;
  GtkWidget *child;
  GtkWidget *next;
  int gimage_pos;
  int pos;

  if (!channelsD)
    return;

  if (! (gimage = gimage_get_ID (channelsD->gimage_id)))
    return;

  /*  Check if the gimage extents have changed  */
  if ((gimage->width != channelsD->gimage_width) ||
      (gimage->height != channelsD->gimage_height) ||
      (gimage_base_type (gimage) != channelsD->base_type))
    {
      channelsD->gimage_id = -1;
      channels_dialog_update (gimage->ID);
    }

  /*  Set all current channel widgets to visited = FALSE  */
  list = channelsD->channel_widgets;
  while (list)
    {
      cw = (ChannelWidget *) list->data;
      cw->visited = FALSE;
      list = g_slist_next (list);
    }

  /*  Add any missing channels  */
  list = gimage->channels;
  while (list)
    {
      channel = (Channel *) list->data;
      cw = channel_widget_get_ID (channel);

      /*  If the channel isn't in the channel widget list, add it  */
      if (cw == NULL)
	channels_dialog_add_channel (channel);
      else
	cw->visited = TRUE;

      list = g_slist_next (list);
    }

  /*  Remove any extraneous auxillary channels  */
  list = channelsD->channel_widgets;
  while (list)
    {
      cw = (ChannelWidget *) list->data;
      list = g_slist_next (list);

      if (cw->visited == FALSE && cw->type == Auxillary)
	/*  will only be true for auxillary channels  */
	channels_dialog_remove_channel (cw);
    }

  /*  Switch positions of items if necessary  */
  list = channelsD->channel_widgets;
  pos = -channelsD->num_components + 1;
  while (list)
    {
      cw = (ChannelWidget *) list->data;
      list = g_slist_next (list);

      if (cw->type == Auxillary)
	if ((gimage_pos = gimage_get_channel_index (gimage, cw->channel)) != pos)
	  channels_dialog_position_channel (cw, gimage_pos);

      pos++;
    }

  /*  Set the active channel  */
  if (channelsD->active_channel != gimage->active_channel)
    channelsD->active_channel = gimage->active_channel;

  /*  set the menus if floating sel status has changed  */
  if (channelsD->floating_sel != gimage->floating_sel)
    channelsD->floating_sel = gimage->floating_sel;

  channels_dialog_set_menu_sensitivity ();

  for (child = gtk_widget_get_first_child (channelsD->channel_list);
       child;
       child = next)
    {
      next = gtk_widget_get_next_sibling (child);
      channel_widget_channel_flush (child, NULL);
    }
}


/*************************************/
/*  channels dialog widget routines  */
/*************************************/

static void
channels_dialog_append_widget (ChannelWidget *cw,
			       int           *pos)
{
  channelsD->channel_widgets = g_slist_append (channelsD->channel_widgets, cw);
  gtk_list_box_insert (GTK_LIST_BOX (channelsD->channel_list), cw->list_item, (*pos)++);
}

void
channels_dialog_update (int gimage_id)
{
  ChannelWidget *cw;
  GImage *gimage;
  Channel *channel;
  GSList *list;
  int pos;

  if (!channelsD)
    return;
  if (channelsD->gimage_id == gimage_id)
    return;

  channelsD->gimage_id = gimage_id;

  suspend_gimage_notify++;
  /*  Free all elements in the channels listbox  */
  channels_list_clear ();
  suspend_gimage_notify--;

  list = channelsD->channel_widgets;
  while (list)
    {
      cw = (ChannelWidget *) list->data;
      list = g_slist_next (list);
      channel_widget_delete (cw);
    }
  channelsD->channel_widgets = NULL;

  if (! (gimage = gimage_get_ID (channelsD->gimage_id)))
    return;

  /*  Find the preview extents  */
  channels_dialog_preview_extents ();

  channelsD->active_channel = NULL;
  channelsD->floating_sel = NULL;

  /*  The image components  */
  pos = 0;
  switch ((channelsD->base_type = gimage_base_type (gimage)))
    {
    case RGB:
      cw = create_channel_widget (gimage, NULL, Red);
      channels_dialog_append_widget (cw, &pos);
      channelsD->components[0] = Red;

      cw = create_channel_widget (gimage, NULL, Green);
      channels_dialog_append_widget (cw, &pos);
      channelsD->components[1] = Green;

      cw = create_channel_widget (gimage, NULL, Blue);
      channels_dialog_append_widget (cw, &pos);
      channelsD->components[2] = Blue;

      channelsD->num_components = 3;
      break;

    case GRAY:
      cw = create_channel_widget (gimage, NULL, Gray);
      channels_dialog_append_widget (cw, &pos);
      channelsD->components[0] = Gray;

      channelsD->num_components = 1;
      break;

    case INDEXED:
      cw = create_channel_widget (gimage, NULL, Indexed);
      channels_dialog_append_widget (cw, &pos);
      channelsD->components[0] = Indexed;

      channelsD->num_components = 1;
      break;
    }

  /*  The auxillary image channels  */
  list = gimage->channels;
  while (list)
    {
      /*  create a channel list item  */
      channel = (Channel *) list->data;
      cw = create_channel_widget (gimage, channel, Auxillary);
      channels_dialog_append_widget (cw, &pos);

      list = g_slist_next (list);
    }
}


void
channels_dialog_clear ()
{
  ops_button_box_set_insensitive (channels_ops_buttons);

  suspend_gimage_notify++;
  channels_list_clear ();
  suspend_gimage_notify--;
}

void
channels_dialog_free ()
{
  GSList *list;
  ChannelWidget *cw;

  if (channelsD == NULL)
    return;

  suspend_gimage_notify++;
  /*  Free all elements in the channels listbox  */
  channels_list_clear ();
  suspend_gimage_notify--;

  list = channelsD->channel_widgets;
  while (list)
    {
      cw = (ChannelWidget *) list->data;
      list = g_slist_next (list);
      channel_widget_delete (cw);
    }
  channelsD->channel_widgets = NULL;
  channelsD->active_channel = NULL;
  channelsD->floating_sel = NULL;

  if (channelsD->preview)
    g_object_unref (channelsD->preview);

  /*  the ops menu goes with the dialog's widgets  */

  g_free (channelsD);
  channelsD = NULL;
}

static void
channels_list_clear ()
{
  GtkWidget *child;

  if (!channelsD)
    return;

  while ((child = gtk_widget_get_first_child (channelsD->channel_list)))
    gtk_list_box_remove (GTK_LIST_BOX (channelsD->channel_list), child);
}

static void
channels_dialog_preview_extents ()
{
  GImage *gimage;

  if (! channelsD)
    return;

  gimage = gimage_get_ID (channelsD->gimage_id);
  channelsD->gimage_width = gimage->width;
  channelsD->gimage_height = gimage->height;

  /*  Get the image width and height variables, based on the gimage  */
  if (gimage->width > gimage->height)
    channelsD->ratio = (double) preview_size / (double) gimage->width;
  else
    channelsD->ratio = (double) preview_size / (double) gimage->height;

  if (preview_size)
    {
      channelsD->image_width = (int) (channelsD->ratio * gimage->width);
      channelsD->image_height = (int) (channelsD->ratio * gimage->height);
      if (channelsD->image_width < 1) channelsD->image_width = 1;
      if (channelsD->image_height < 1) channelsD->image_height = 1;
    }
  else
    {
      channelsD->image_width = channel_width;
      channelsD->image_height = channel_height;
    }
}


static void
channels_dialog_set_menu_sensitivity ()
{
  ChannelWidget *cw;
  gint fs_sensitive;
  gint aux_sensitive;

  cw = channel_widget_get_ID (channelsD->active_channel);
  fs_sensitive = (channelsD->floating_sel != NULL);

  if (cw)
    aux_sensitive = (cw->type == Auxillary);
  else
    aux_sensitive = FALSE;

  /* new channel */
  gtk_widget_set_sensitive (channels_ops[0].widget, !fs_sensitive);
  ops_button_set_sensitive (channels_ops_buttons[0], !fs_sensitive);
  /* raise channel */
  gtk_widget_set_sensitive (channels_ops[1].widget, !fs_sensitive && aux_sensitive);
  ops_button_set_sensitive (channels_ops_buttons[1], !fs_sensitive && aux_sensitive);
  /* lower channel */
  gtk_widget_set_sensitive (channels_ops[2].widget, !fs_sensitive && aux_sensitive);
  ops_button_set_sensitive (channels_ops_buttons[2], !fs_sensitive && aux_sensitive);
  /* duplicate channel */
  gtk_widget_set_sensitive (channels_ops[3].widget, !fs_sensitive && aux_sensitive);
  ops_button_set_sensitive (channels_ops_buttons[3], !fs_sensitive && aux_sensitive);
  /* delete channel */
  gtk_widget_set_sensitive (channels_ops[4].widget, !fs_sensitive && aux_sensitive);
  ops_button_set_sensitive (channels_ops_buttons[4], !fs_sensitive && aux_sensitive);
  /* channel to selection */
  gtk_widget_set_sensitive (channels_ops[5].widget, aux_sensitive);
}


static void
channel_widget_set_selected (ChannelWidget *channel_widget,
			     gboolean       selected)
{
  if (selected == LC_ROW_SELECTED (channel_widget->list_item))
    return;

  if (selected)
    gtk_widget_set_state_flags (channel_widget->list_item,
				GTK_STATE_FLAG_SELECTED, FALSE);
  else
    gtk_widget_unset_state_flags (channel_widget->list_item,
				  GTK_STATE_FLAG_SELECTED);

  gtk_widget_queue_draw (channel_widget->eye_widget);
  gtk_widget_queue_draw (channel_widget->channel_preview);
}


static void
channels_dialog_set_channel (ChannelWidget *channel_widget)
{
  int index;

  if (!channelsD || !channel_widget)
    return;

  /*  Make sure the gimage is not notified of this change  */
  suspend_gimage_notify++;

  if (channel_widget->type == Auxillary)
    {
      /*  turn on the specified auxillary channel  */
      index = gimage_get_channel_index (channel_widget->gimage, channel_widget->channel);
      if ((index >= 0) && !LC_ROW_SELECTED (channel_widget->list_item))
	channel_widget_set_selected (channel_widget, TRUE);
    }
  else
    {
      if (!LC_ROW_SELECTED (channel_widget->list_item))
	channel_widget_set_selected (channel_widget, TRUE);
    }
  suspend_gimage_notify--;
}


static void
channels_dialog_unset_channel (ChannelWidget * channel_widget)
{
  int index;

  if (!channelsD || !channel_widget)
    return;

  /*  Make sure the gimage is not notified of this change  */
  suspend_gimage_notify++;

  if (channel_widget->type == Auxillary)
    {
      /*  turn off the specified auxillary channel  */
      index = gimage_get_channel_index (channel_widget->gimage, channel_widget->channel);
      if ((index >= 0) && LC_ROW_SELECTED (channel_widget->list_item))
	channel_widget_set_selected (channel_widget, FALSE);
    }
  else
    {
      if (LC_ROW_SELECTED (channel_widget->list_item))
	channel_widget_set_selected (channel_widget, FALSE);
    }

  suspend_gimage_notify--;
}


static void
channels_dialog_position_channel (ChannelWidget *channel_widget,
				  int new_index)
{
  if (!channelsD || !channel_widget)
    return;

  /*  Make sure the gimage is not notified of this change  */
  suspend_gimage_notify++;

  /*  Remove the channel from the dialog  */
  if (gtk_widget_get_parent (channel_widget->list_item))
    gtk_list_box_remove (GTK_LIST_BOX (channelsD->channel_list), channel_widget->list_item);
  channelsD->channel_widgets = g_slist_remove (channelsD->channel_widgets, channel_widget);

  suspend_gimage_notify--;

  /*  Add it back at the proper index  */
  gtk_list_box_insert (GTK_LIST_BOX (channelsD->channel_list), channel_widget->list_item,
		       new_index + channelsD->num_components);
  channelsD->channel_widgets = g_slist_insert (channelsD->channel_widgets, channel_widget, new_index + channelsD->num_components);
}


static void
channels_dialog_add_channel (Channel *channel)
{
  GImage *gimage;
  ChannelWidget *channel_widget;
  int position;

  if (!channelsD || !channel)
    return;
  if (! (gimage = gimage_get_ID (channelsD->gimage_id)))
    return;

  channel_widget = create_channel_widget (gimage, channel, Auxillary);

  position = gimage_get_channel_index (gimage, channel);
  channelsD->channel_widgets = g_slist_insert (channelsD->channel_widgets, channel_widget,
					       position + channelsD->num_components);
  gtk_list_box_insert (GTK_LIST_BOX (channelsD->channel_list), channel_widget->list_item,
		       position + channelsD->num_components);
}


static void
channels_dialog_remove_channel (ChannelWidget *channel_widget)
{
  if (!channelsD || !channel_widget)
    return;

  /*  Make sure the gimage is not notified of this change  */
  suspend_gimage_notify++;

  /*  Remove the requested channel from the dialog, and delete the
   *  channel_widget
   */
  channel_widget_delete (channel_widget);

  suspend_gimage_notify--;
}


/*******************************/
/*  channels dialog callbacks  */
/*******************************/

static void
channels_dialog_new_channel_callback (GtkWidget *w,
				      gpointer   client_data)
{
  /*  if there is a currently selected gimage, request a new channel
   */
  if (!channelsD)
    return;
  if (channelsD->gimage_id == -1)
    return;

  channels_dialog_new_channel_query (channelsD->gimage_id);
}


static void
channels_dialog_raise_channel_callback (GtkWidget *w,
					gpointer   client_data)
{
  GImage *gimage;

  if (!channelsD)
    return;
  if (! (gimage = gimage_get_ID (channelsD->gimage_id)))
    return;

  if (gimage->active_channel != NULL)
    {
      gimage_raise_channel (gimage, gimage->active_channel);
      gdisplays_flush ();
    }
}


static void
channels_dialog_lower_channel_callback (GtkWidget *w,
					gpointer   client_data)
{
  GImage *gimage;

  if (!channelsD)
    return;
  if (! (gimage = gimage_get_ID (channelsD->gimage_id)))
    return;

  if (gimage->active_channel != NULL)
    {
      gimage_lower_channel (gimage, gimage->active_channel);
      gdisplays_flush ();
    }
}


static void
channels_dialog_duplicate_channel_callback (GtkWidget *w,
					    gpointer   client_data)
{
  GImage *gimage;
  Channel *active_channel;
  Channel *new_channel;

  /*  if there is a currently selected gimage, request a new channel
   */
  if (!channelsD)
    return;
  if (! (gimage = gimage_get_ID (channelsD->gimage_id)))
    return;

  if ((active_channel = gimage_get_active_channel (gimage)))
    {
      new_channel = channel_copy (active_channel);
      gimage_add_channel (gimage, new_channel, -1);
      gdisplays_flush ();
    }
}


static void
channels_dialog_delete_channel_callback (GtkWidget *w,
					 gpointer   client_data)
{
  GImage *gimage;

  /*  if there is a currently selected gimage
   */
  if (!channelsD)
    return;
  if (! (gimage = gimage_get_ID (channelsD->gimage_id)))
    return;

  if (gimage->active_channel != NULL)
    {
      gimage_remove_channel (gimage, gimage->active_channel);
      gdisplays_flush ();
    }
}


static void
channels_dialog_channel_to_sel_callback (GtkWidget *w,
					 gpointer   client_data)
{
  GImage *gimage;

  /*  if there is a currently selected gimage
   */
  if (!channelsD)
    return;
  if (! (gimage = gimage_get_ID (channelsD->gimage_id)))
    return;

  if (gimage->active_channel != NULL)
    {
      gimage_mask_load (gimage, gimage->active_channel);
      gdisplays_flush ();
    }
}


/****************************/
/*  channel widget functions  */
/****************************/

static ChannelWidget *
channel_widget_get_ID (Channel *channel)
{
  ChannelWidget *lw;
  GSList *list;

  if (!channelsD)
    return NULL;

  list = channelsD->channel_widgets;

  while (list)
    {
      lw = (ChannelWidget *) list->data;
      if (lw->channel == channel)
	return lw;

      list = g_slist_next (list);
    }

  return NULL;
}

/*  The channel widget a row, or a widget in a row, belongs to.  NULL
 *  once the channel widget has been deleted.
 */
static ChannelWidget *
channel_widget_from (GtkWidget *widget)
{
  GtkWidget *row;

  if (GTK_IS_LIST_BOX_ROW (widget))
    row = widget;
  else
    row = gtk_widget_get_ancestor (widget, GTK_TYPE_LIST_BOX_ROW);

  if (!row)
    return NULL;

  return (ChannelWidget *) g_object_get_data (G_OBJECT (row), CHANNEL_WIDGET_KEY);
}


static ChannelWidget *
create_channel_widget (GImage      *gimage,
		       Channel     *channel,
		       ChannelType  type)
{
  ChannelWidget *channel_widget;
  GtkWidget *list_item;
  GtkWidget *hbox;
  GtkWidget *vbox;
  GtkGesture *gesture;

  list_item = gtk_list_box_row_new ();
  g_object_ref_sink (list_item);
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (list_item), FALSE);

  /*  create the channel widget and add it to the list  */
  channel_widget = (ChannelWidget *) g_malloc (sizeof (ChannelWidget));
  channel_widget->gimage = gimage;
  channel_widget->channel = channel;
  channel_widget->channel_preview = NULL;
  channel_widget->channel_pixmap = NULL;
  channel_widget->type = type;
  channel_widget->ID = (type == Auxillary) ? GIMP_DRAWABLE(channel)->ID : (COMPONENT_BASE_ID + type);
  channel_widget->list_item = list_item;
  channel_widget->width = -1;
  channel_widget->height = -1;
  channel_widget->visited = TRUE;

  /*  Need to let the list item know about the channel_widget  */
  g_object_set_data (G_OBJECT (list_item), CHANNEL_WIDGET_KEY, channel_widget);

  /*  clicks on the row toggle it, pop up the menu, or edit the channel  */
  gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 0);
  g_signal_connect (gesture, "pressed",
		    G_CALLBACK (channel_widget_row_pressed), NULL);
  gtk_widget_add_controller (list_item, GTK_EVENT_CONTROLLER (gesture));

  vbox = gimp_vbox_new (FALSE, 1);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (list_item), vbox);

  hbox = gimp_hbox_new (FALSE, 1);
  gimp_box_pack_start (vbox, hbox, FALSE, FALSE, 1);

  /* Create the visibility toggle button */
  channel_widget->eye_widget = gtk_drawing_area_new ();
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (channel_widget->eye_widget), eye_width);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (channel_widget->eye_widget), eye_height);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (channel_widget->eye_widget),
				  channel_widget_eye_draw, NULL, NULL);
  gtk_widget_set_halign (channel_widget->eye_widget, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (channel_widget->eye_widget, GTK_ALIGN_CENTER);
  gesture = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 0);
  g_signal_connect (gesture, "drag-begin",
		    G_CALLBACK (channel_widget_button_begin), NULL);
  g_signal_connect (gesture, "drag-update",
		    G_CALLBACK (channel_widget_button_update), NULL);
  g_signal_connect (gesture, "drag-end",
		    G_CALLBACK (channel_widget_button_end), NULL);
  gtk_widget_add_controller (channel_widget->eye_widget, GTK_EVENT_CONTROLLER (gesture));
  gimp_box_pack_start (hbox, channel_widget->eye_widget, FALSE, TRUE, 2);

  /*  The preview  */
  channel_widget->channel_preview = gtk_drawing_area_new ();
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (channel_widget->channel_preview),
				      channelsD->image_width);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (channel_widget->channel_preview),
				       channelsD->image_height);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (channel_widget->channel_preview),
				  channel_widget_preview_draw, NULL, NULL);
  gtk_widget_set_halign (channel_widget->channel_preview, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (channel_widget->channel_preview, GTK_ALIGN_CENTER);
  gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 3);
  g_signal_connect (gesture, "pressed",
		    G_CALLBACK (channel_widget_preview_pressed), NULL);
  gtk_widget_add_controller (channel_widget->channel_preview, GTK_EVENT_CONTROLLER (gesture));
  gimp_box_pack_start (hbox, channel_widget->channel_preview, FALSE, FALSE, 2);

  /*  the channel name label */
  switch (channel_widget->type)
    {
    case Red:       channel_widget->label = gtk_label_new ("Red"); break;
    case Green:     channel_widget->label = gtk_label_new ("Green"); break;
    case Blue:      channel_widget->label = gtk_label_new ("Blue"); break;
    case Gray:      channel_widget->label = gtk_label_new ("Gray"); break;
    case Indexed:   channel_widget->label = gtk_label_new ("Indexed"); break;
    case Auxillary: channel_widget->label = gtk_label_new (GIMP_DRAWABLE(channel)->name); break;
    default:        channel_widget->label = gtk_label_new (NULL); break;
    }

  gimp_box_pack_start (hbox, channel_widget->label, FALSE, FALSE, 2);

  channel_widget->clip_widget = NULL;

  return channel_widget;
}


static void
channel_widget_delete (ChannelWidget *channel_widget)
{
  if (channel_widget->channel_pixmap)
    cairo_surface_destroy (channel_widget->channel_pixmap);

  /*  Remove the channel widget from the list  */
  channelsD->channel_widgets = g_slist_remove (channelsD->channel_widgets, channel_widget);

  /*  Release the widget  */
  g_object_set_data (G_OBJECT (channel_widget->list_item), CHANNEL_WIDGET_KEY, NULL);
  if (gtk_widget_get_parent (channel_widget->list_item))
    gtk_list_box_remove (GTK_LIST_BOX (channelsD->channel_list), channel_widget->list_item);
  g_object_unref (channel_widget->list_item);
  g_free (channel_widget);
}


static void
channel_widget_select_update (ChannelWidget *channel_widget)
{
  if (channel_widget == NULL)
    return;

  if (suspend_gimage_notify == 0)
    {
      if (channel_widget->type == Auxillary)
	{
	  if (LC_ROW_SELECTED (channel_widget->list_item))
	    /*  set the gimage's active channel to be this channel  */
	    gimage_set_active_channel (channel_widget->gimage, channel_widget->channel);
	  else
	    /*  unset the gimage's active channel  */
	    gimage_unset_active_channel (channel_widget->gimage);

	  gdisplays_flush ();
	}
      else if (channel_widget->type != Auxillary)
	{
	  if (LC_ROW_SELECTED (channel_widget->list_item))
	    gimage_set_component_active (channel_widget->gimage, channel_widget->type, TRUE);
	  else
	    gimage_set_component_active (channel_widget->gimage, channel_widget->type, FALSE);
	}
    }
}


static void
channel_widget_row_pressed (GtkGestureClick *gesture,
			    int              n_press,
			    double           x,
			    double           y,
			    gpointer         data)
{
  GtkWidget *row;
  ChannelWidget *channel_widget;
  guint button;

  row = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  if (! (channel_widget = channel_widget_from (row)) || !channelsD)
    return;

  button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));

  if (button == 3)
    {
      lc_ops_menu_popup (channelsD->ops_menu, row, x, y);
      return;
    }

  if (button != 1)
    return;

  /*  A double click on an auxillary channel edits it; its second
   *  press does not toggle the channel again.
   */
  if (n_press == 2 && channel_widget->type == Auxillary)
    {
      channels_dialog_edit_channel_query (channel_widget);
      return;
    }

  channel_widget_set_selected (channel_widget, !LC_ROW_SELECTED (row));
  channel_widget_select_update (channel_widget);
}


/*  The eye toggle: pressing toggles; moving out of the toggle with the
 *  button held toggles back, moving in again toggles again; the change
 *  is committed when the button is released.
 */
static int button_down = 0;
static int button_inside = FALSE;
static GtkWidget *click_widget = NULL;
static int old_state;
static int exclusive;
static double click_x, click_y;

static int
channel_widget_visible (ChannelWidget *channel_widget)
{
  switch (channel_widget->type)
    {
    case Auxillary:
      return GIMP_DRAWABLE(channel_widget->channel)->visible;
    default:
      return gimage_get_component_visible (channel_widget->gimage, channel_widget->type);
    }
}

static void
channel_widget_toggle_visible (ChannelWidget *channel_widget)
{
  int visible;

  visible = channel_widget_visible (channel_widget);

  if (channel_widget->type == Auxillary)
    GIMP_DRAWABLE(channel_widget->channel)->visible = !visible;
  else
    gimage_set_component_visible (channel_widget->gimage, channel_widget->type, !visible);

  channel_widget_eye_redraw (channel_widget);
}

static void
channel_widget_button_begin (GtkGestureDrag *gesture,
			     double          x,
			     double          y,
			     gpointer        data)
{
  GtkWidget *widget;
  ChannelWidget *channel_widget;
  GdkModifierType state;
  guint button;

  widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  if (! (channel_widget = channel_widget_from (widget)) || !channelsD)
    return;

  gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);

  button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
  state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));

  if (button == 3)
    {
      button_down = 0;
      lc_ops_menu_popup (channelsD->ops_menu, widget, x, y);
      return;
    }

  button_down = 1;
  button_inside = TRUE;
  click_widget = widget;
  click_x = x;
  click_y = y;

  if (widget == channel_widget->eye_widget)
    {
      old_state = channel_widget_visible (channel_widget);

      /*  If this was a shift-click, make all/none visible  */
      if (state & GDK_SHIFT_MASK)
	{
	  exclusive = TRUE;
	  channel_widget_exclusive_visible (channel_widget);
	}
      else
	{
	  exclusive = FALSE;
	  channel_widget_toggle_visible (channel_widget);
	}
    }
}

static void
channel_widget_button_update (GtkGestureDrag *gesture,
			      double          offset_x,
			      double          offset_y,
			      gpointer        data)
{
  GtkWidget *widget;
  ChannelWidget *channel_widget;
  double x, y;
  int inside;

  widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  if (!button_down || widget != click_widget ||
      ! (channel_widget = channel_widget_from (widget)))
    return;

  x = click_x + offset_x;
  y = click_y + offset_y;
  inside = (x >= 0 && y >= 0 &&
	    x < gtk_widget_get_width (widget) &&
	    y < gtk_widget_get_height (widget));

  if (inside != button_inside)
    {
      button_inside = inside;

      if (widget == channel_widget->eye_widget)
	{
	  if (exclusive)
	    channel_widget_exclusive_visible (channel_widget);
	  else
	    channel_widget_toggle_visible (channel_widget);
	}
    }
}

static void
channel_widget_button_end (GtkGestureDrag *gesture,
			   double          offset_x,
			   double          offset_y,
			   gpointer        data)
{
  GtkWidget *widget;
  ChannelWidget *channel_widget;
  int width, height;

  widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  if (!button_down || widget != click_widget)
    return;

  button_down = 0;
  click_widget = NULL;

  if (! (channel_widget = channel_widget_from (widget)))
    return;

  switch (channel_widget->type)
    {
    case Auxillary:
      width = GIMP_DRAWABLE(channel_widget->channel)->width;
      height = GIMP_DRAWABLE(channel_widget->channel)->height;
      break;
    default:
      width = channel_widget->gimage->width;
      height = channel_widget->gimage->height;
      break;
    }

  if (widget == channel_widget->eye_widget)
    {
      if (exclusive)
	{
	  gdisplays_update_area (channel_widget->gimage->ID, 0, 0, width, height);
	  gdisplays_flush ();
	}
      else if (old_state != channel_widget_visible (channel_widget))
	{
	  gdisplays_update_area (channel_widget->gimage->ID, 0, 0, width, height);
	  gdisplays_flush ();
	}
    }
}


static void
channel_widget_preview_pressed (GtkGestureClick *gesture,
				int              n_press,
				double           x,
				double           y,
				gpointer         data)
{
  GtkWidget *widget;

  widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  if (!channel_widget_from (widget) || !channelsD)
    return;

  gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
  lc_ops_menu_popup (channelsD->ops_menu, widget, x, y);
}


static void
channel_widget_preview_draw (GtkDrawingArea *area,
			     cairo_t        *cr,
			     int             width,
			     int             height,
			     gpointer        data)
{
  ChannelWidget *channel_widget;
  int valid;

  if (! (channel_widget = channel_widget_from (GTK_WIDGET (area))) || !channelsD)
    return;

  if (!preview_size)
    channel_widget_no_preview_redraw (channel_widget, cr);
  else
    {
      switch (channel_widget->type)
	{
	case Auxillary:
	  valid = GIMP_DRAWABLE(channel_widget->channel)->preview_valid;
	  break;
	default:
	  valid = gimage_preview_valid (channel_widget->gimage, channel_widget->type);
	  break;
	}

      if (!valid || !channel_widget->channel_pixmap)
	channel_widget_preview_redraw (channel_widget);

      if (channel_widget->channel_pixmap)
	{
	  cairo_set_source_surface (cr, channel_widget->channel_pixmap, 0, 0);
	  cairo_paint (cr);
	}
    }
}


static void
channel_widget_preview_redraw (ChannelWidget *channel_widget)
{
  TempBuf * preview_buf;
  int width, height;
  int channel;

  if (!channelsD->preview)
    return;

  /*  determine width and height  */
  switch (channel_widget->type)
    {
    case Auxillary:
      width = GIMP_DRAWABLE(channel_widget->channel)->width;
      height = GIMP_DRAWABLE(channel_widget->channel)->height;
      channel_widget->width = (int) (channelsD->ratio * width);
      channel_widget->height = (int) (channelsD->ratio * height);
      preview_buf = channel_preview (channel_widget->channel,
				     channel_widget->width,
				     channel_widget->height);
      break;
    default:
      width = channel_widget->gimage->width;
      height = channel_widget->gimage->height;
      channel_widget->width = (int) (channelsD->ratio * width);
      channel_widget->height = (int) (channelsD->ratio * height);
      preview_buf = gimage_composite_preview (channel_widget->gimage,
					      channel_widget->type,
					      channel_widget->width,
					      channel_widget->height);
      break;
    }

  if (!preview_buf)
    return;

  switch (channel_widget->type)
    {
    case Red:       channel = RED_PIX; break;
    case Green:     channel = GREEN_PIX; break;
    case Blue:      channel = BLUE_PIX; break;
    case Gray:      channel = GRAY_PIX; break;
    case Indexed:   channel = INDEXED_PIX; break;
    case Auxillary: channel = -1; break;
    default:        channel = -1; break;
    }

  render_preview (preview_buf,
		  channelsD->preview,
		  channelsD->image_width,
		  channelsD->image_height,
		  channel);

  if (channel_widget->channel_pixmap)
    cairo_surface_destroy (channel_widget->channel_pixmap);
  channel_widget->channel_pixmap = render_preview_surface (channelsD->preview,
							   channelsD->image_width,
							   channelsD->image_height);
}


static void
channel_widget_no_preview_redraw (ChannelWidget *channel_widget,
				  cairo_t       *cr)
{
  /*  the row draws the background for the normal, selected and
   *  insensitive states; the icon is drawn in the matching foreground
   */
  lc_draw_bitmap (channel_widget->channel_preview, cr,
		  channel_bits, channel_width, channel_height, 0, 0);
}


static void
channel_widget_eye_draw (GtkDrawingArea *area,
			 cairo_t        *cr,
			 int             width,
			 int             height,
			 gpointer        data)
{
  ChannelWidget *channel_widget;

  if (! (channel_widget = channel_widget_from (GTK_WIDGET (area))))
    return;

  if (channel_widget_visible (channel_widget))
    lc_draw_bitmap (GTK_WIDGET (area), cr, eye_bits, eye_width, eye_height, 0, 0);
}

static void
channel_widget_eye_redraw (ChannelWidget *channel_widget)
{
  gtk_widget_queue_draw (channel_widget->eye_widget);
}


static void
channel_widget_exclusive_visible (ChannelWidget *channel_widget)
{
  GSList *list;
  ChannelWidget *cw;
  int visible = FALSE;

  if (!channelsD)
    return;

  /*  First determine if _any_ other channel widgets are set to visible  */
  list = channelsD->channel_widgets;
  while (list)
    {
      cw = (ChannelWidget *) list->data;
      if (cw != channel_widget)
	{
	  switch (cw->type)
	    {
	    case Auxillary:
	      visible |= GIMP_DRAWABLE(cw->channel)->visible;
	      break;
	    default:
	      visible |= gimage_get_component_visible (cw->gimage, cw->type);
	      break;
	    }
	}

      list = g_slist_next (list);
    }

  /*  Now, toggle the visibility for all channels except the specified one  */
  list = channelsD->channel_widgets;
  while (list)
    {
      cw = (ChannelWidget *) list->data;
      if (cw != channel_widget)
	switch (cw->type)
	  {
	  case Auxillary:
	    GIMP_DRAWABLE(cw->channel)->visible = !visible;
	    break;
	  default:
	    gimage_set_component_visible (cw->gimage, cw->type, !visible);
	    break;
	  }
      else
	switch (cw->type)
	  {
	  case Auxillary:
	    GIMP_DRAWABLE(cw->channel)->visible = TRUE;
	    break;
	  default:
	    gimage_set_component_visible (cw->gimage, cw->type, TRUE);
	    break;
	  }

      channel_widget_eye_redraw (cw);

      list = g_slist_next (list);
    }
}


static void
channel_widget_channel_flush (GtkWidget *widget,
			      gpointer   client_data)
{
  ChannelWidget *channel_widget;
  int update_preview;

  if (! (channel_widget = channel_widget_from (widget)))
    return;

  /***  Sensitivity  ***/

  /*  If there is a floating selection...  */
  if (channelsD->floating_sel != NULL)
    {
      /*  to insensitive if this is an auxillary channel  */
      if (channel_widget->type == Auxillary)
	{
	  if (gtk_widget_get_sensitive (channel_widget->list_item))
	    gtk_widget_set_sensitive (channel_widget->list_item, FALSE);
	}
      /*  to sensitive otherwise  */
      else
	{
	  if (! gtk_widget_get_sensitive (channel_widget->list_item))
	    gtk_widget_set_sensitive (channel_widget->list_item, TRUE);
	}
    }
  else
    {
      /*  to insensitive if there is an active channel, and this is a component channel  */
      if (channel_widget->type != Auxillary && channelsD->active_channel != NULL)
	{
	  if (gtk_widget_get_sensitive (channel_widget->list_item))
	    gtk_widget_set_sensitive (channel_widget->list_item, FALSE);
	}
      /*  to sensitive otherwise  */
      else
	{
	  if (! gtk_widget_get_sensitive (channel_widget->list_item))
	    gtk_widget_set_sensitive (channel_widget->list_item, TRUE);
	}
    }

  /***  Selection  ***/

  /*  If this is an auxillary channel  */
  if (channel_widget->type == Auxillary)
    {
      /*  select if this is the active channel  */
      if (channelsD->active_channel == (channel_widget->channel))
	channels_dialog_set_channel (channel_widget);
      /*  unselect if this is not the active channel  */
      else
	channels_dialog_unset_channel (channel_widget);
    }
  else
    {
      /*  If the component is active, select. otherwise, deselect  */
      if (gimage_get_component_active (channel_widget->gimage, channel_widget->type))
	channels_dialog_set_channel (channel_widget);
      else
	channels_dialog_unset_channel (channel_widget);
    }

  switch (channel_widget->type)
    {
    case Auxillary:
      update_preview = !GIMP_DRAWABLE(channel_widget->channel)->preview_valid;
      break;
    default:
      update_preview = !gimage_preview_valid (channel_widget->gimage, channel_widget->type);
      break;
    }

  if (update_preview)
    gtk_widget_queue_draw (channel_widget->channel_preview);
}


/*
 *  The new channel query dialog
 */

typedef struct _NewChannelOptions NewChannelOptions;

struct _NewChannelOptions {
  GtkWidget *query_box;
  GtkWidget *name_entry;
  ColorPanel *color_panel;

  int gimage_id;
  double opacity;
};

static char *channel_name = NULL;
static unsigned char channel_color[3] = {0, 0, 0};

static void
new_channel_query_ok_callback (GtkWidget *w,
			       gpointer   client_data)
{
  NewChannelOptions *options;
  Channel *new_channel;
  GImage *gimage;
  int i;

  options = (NewChannelOptions *) client_data;
  if (channel_name)
    g_free (channel_name);
  channel_name = g_strdup (gtk_editable_get_text (GTK_EDITABLE (options->name_entry)));

  if ((gimage = gimage_get_ID (options->gimage_id)))
    {
      new_channel = channel_new (gimage->ID, gimage->width, gimage->height,
				 channel_name, (int) (255 * options->opacity) / 100,
				 options->color_panel->color);
      drawable_fill (GIMP_DRAWABLE(new_channel), TRANSPARENT_FILL);

      for (i = 0; i < 3; i++)
	channel_color[i] = options->color_panel->color[i];

      gimage_add_channel (gimage, new_channel, -1);
      gdisplays_flush ();
    }

  color_panel_free (options->color_panel);
  gtk_window_destroy (GTK_WINDOW (options->query_box));
  g_free (options);
}

static void
new_channel_query_cancel_callback (GtkWidget *w,
				   gpointer   client_data)
{
  NewChannelOptions *options;

  options = (NewChannelOptions *) client_data;
  color_panel_free (options->color_panel);
  gtk_window_destroy (GTK_WINDOW (options->query_box));
  g_free (options);
}

static gboolean
new_channel_query_delete_callback (GtkWindow *w,
				   gpointer   client_data)
{
  new_channel_query_cancel_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}

static void
new_channel_query_scale_update (GtkAdjustment *adjustment,
				gpointer       data)
{
  double *scale_val = (double *) data;

  *scale_val = gtk_adjustment_get_value (adjustment);
}

static void
channels_dialog_new_channel_query (int gimage_id)
{
  NewChannelOptions *options;
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *opacity_scale;
  GtkAdjustment *opacity_scale_data;

  /*  the new options structure  */
  options = (NewChannelOptions *) g_malloc (sizeof (NewChannelOptions));
  options->gimage_id = gimage_id;
  options->opacity = 50.0;
  options->color_panel = color_panel_new (channel_color, 48, 64);

  /*  the dialog  */
  options->query_box = gimp_dialog_new ("New Channel Options");

  /* handle the wm close signal */
  g_signal_connect (options->query_box, "close-request",
		    G_CALLBACK (new_channel_query_delete_callback),
		    options);

  /*  the main vbox  */
  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, 1);
  gimp_box_pack_start (gimp_dialog_get_vbox (options->query_box), vbox, TRUE, TRUE, 0);

  /*  The table  */
  table = gimp_table_new (2, 3, FALSE);
  gimp_box_pack_start (vbox, table, TRUE, TRUE, 0);

  /*  the name entry hbox, label and entry  */
  label = gtk_label_new ("Channel name: ");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 0, 1,
		     GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 0, 1);

  options->name_entry = gtk_entry_new ();
  gtk_widget_set_size_request (options->name_entry, 75, -1);
  gimp_table_attach (table, options->name_entry, 1, 2, 0, 1,
		     GIMP_EXPAND | GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 1, 1);
  gtk_editable_set_text (GTK_EDITABLE (options->name_entry), (channel_name ? channel_name : "New Channel"));

  /*  the opacity scale  */
  label = gtk_label_new ("Fill Opacity: ");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 1, 2,
		     GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 0, 1);

  opacity_scale_data = gtk_adjustment_new (options->opacity, 0.0, 100.0, 1.0, 1.0, 0.0);
  opacity_scale = gimp_hscale_new (opacity_scale_data, 1);
  gimp_table_attach (table, opacity_scale, 1, 2, 1, 2,
		     GIMP_EXPAND | GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 1, 1);
  g_signal_connect (opacity_scale_data, "value-changed",
		    G_CALLBACK (new_channel_query_scale_update),
		    &options->opacity);

  /*  the color panel  */
  gimp_table_attach (table, options->color_panel->color_panel_widget,
		     2, 3, 0, 2, GIMP_EXPAND, GIMP_EXPAND, 4, 2);

  gimp_dialog_add_button (options->query_box, "OK",
			  G_CALLBACK (new_channel_query_ok_callback), options, TRUE);
  gimp_dialog_add_button (options->query_box, "Cancel",
			  G_CALLBACK (new_channel_query_cancel_callback), options, FALSE);

  gtk_window_present (GTK_WINDOW (options->query_box));
}


/*
 *  The edit channel attributes dialog
 */

typedef struct _EditChannelOptions EditChannelOptions;

struct _EditChannelOptions {
  GtkWidget *query_box;
  GtkWidget *name_entry;

  ChannelWidget *channel_widget;
  Channel *channel;
  int gimage_id;
  ColorPanel *color_panel;
  double opacity;
};

static void
edit_channel_query_ok_callback (GtkWidget *w,
				gpointer   client_data)
{
  EditChannelOptions *options;
  ChannelWidget *channel_widget;
  Channel *channel;
  int opacity;
  int update = FALSE;
  int i;

  options = (EditChannelOptions *) client_data;
  channel = options->channel;
  opacity = (int) (255 * options->opacity) / 100;


  if (gimage_get_ID (options->gimage_id)) {

    /*  Set the new channel name  */
    if (GIMP_DRAWABLE(channel)->name)
      g_free (GIMP_DRAWABLE(channel)->name);
    GIMP_DRAWABLE(channel)->name = g_strdup (gtk_editable_get_text (GTK_EDITABLE (options->name_entry)));

    /*  the channel widget may have gone while the dialog was up  */
    if ((channel_widget = channel_widget_get_ID (channel)))
      gtk_label_set_text (GTK_LABEL (channel_widget->label), GIMP_DRAWABLE(channel)->name);

    if (channel->opacity != opacity)
      {
	channel->opacity = opacity;
	update = TRUE;
      }
    for (i = 0; i < 3; i++)
      if (options->color_panel->color[i] != channel->col[i])
	{
	  channel->col[i] = options->color_panel->color[i];
	  update = TRUE;
	}

    if (update)
      {
	drawable_update (GIMP_DRAWABLE(channel), 0, 0, GIMP_DRAWABLE(channel)->width, GIMP_DRAWABLE(channel)->height);
	gdisplays_flush ();
      }
  }
  color_panel_free (options->color_panel);
  gtk_window_destroy (GTK_WINDOW (options->query_box));
  g_free (options);
}

static void
edit_channel_query_cancel_callback (GtkWidget *w,
				    gpointer   client_data)
{
  EditChannelOptions *options;

  options = (EditChannelOptions *) client_data;

  color_panel_free (options->color_panel);
  gtk_window_destroy (GTK_WINDOW (options->query_box));
  g_free (options);
}

static gboolean
edit_channel_query_delete_callback (GtkWindow *w,
				    gpointer   client_data)
{
  edit_channel_query_cancel_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}

static void
channels_dialog_edit_channel_query (ChannelWidget *channel_widget)
{
  EditChannelOptions *options;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *opacity_scale;
  GtkAdjustment *opacity_scale_data;
  int i;

  /*  the new options structure  */
  options = (EditChannelOptions *) g_malloc (sizeof (EditChannelOptions));
  options->channel_widget = channel_widget;
  options->channel = channel_widget->channel;
  options->gimage_id = channel_widget->gimage->ID;
  options->opacity = (double) channel_widget->channel->opacity / 2.55;
  for (i = 0; i < 3; i++)
    channel_color[i] =  channel_widget->channel->col[i];

  options->color_panel = color_panel_new (channel_color, 48, 64);

  /*  the dialog  */
  options->query_box = gimp_dialog_new ("Edit Channel Attributes");

  /* deal with the wm close signal */
  g_signal_connect (options->query_box, "close-request",
		    G_CALLBACK (edit_channel_query_delete_callback),
		    options);

  /*  the main vbox  */
  vbox = gimp_vbox_new (FALSE, 1);
  gimp_box_pack_start (gimp_dialog_get_vbox (options->query_box), vbox, TRUE, TRUE, 0);

  /*  The table  */
  table = gimp_table_new (2, 2, FALSE);
  gimp_box_pack_start (vbox, table, TRUE, TRUE, 0);

  /*  the name entry hbox, label and entry  */
  hbox = gimp_hbox_new (FALSE, 1);
  gimp_table_attach (table, hbox, 0, 1, 0, 1,
		     GIMP_EXPAND | GIMP_FILL, 0, 2, 2);
  label = gtk_label_new ("Channel name:");
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);
  options->name_entry = gtk_entry_new ();
  gimp_box_pack_start (hbox, options->name_entry, TRUE, TRUE, 0);
  gtk_editable_set_text (GTK_EDITABLE (options->name_entry), GIMP_DRAWABLE(channel_widget->channel)->name);

  /*  the opacity scale  */
  hbox = gimp_hbox_new (FALSE, 1);
  gimp_table_attach (table, hbox, 0, 1, 1, 2,
		     GIMP_EXPAND | GIMP_FILL, 0, 2, 2);

  label = gtk_label_new ("Fill Opacity");
  gimp_box_pack_start (hbox, label, FALSE, FALSE, 0);

  opacity_scale_data = gtk_adjustment_new (options->opacity, 0.0, 100.0, 1.0, 1.0, 0.0);
  opacity_scale = gimp_hscale_new (opacity_scale_data, 1);
  gimp_box_pack_start (hbox, opacity_scale, TRUE, TRUE, 0);
  g_signal_connect (opacity_scale_data, "value-changed",
		    G_CALLBACK (new_channel_query_scale_update),
		    &options->opacity);

  /*  the color panel  */
  gimp_table_attach (table, options->color_panel->color_panel_widget,
		     1, 2, 0, 2, GIMP_EXPAND, GIMP_EXPAND, 4, 2);

  gimp_dialog_add_button (options->query_box, "OK",
			  G_CALLBACK (edit_channel_query_ok_callback), options, TRUE);
  gimp_dialog_add_button (options->query_box, "Cancel",
			  G_CALLBACK (edit_channel_query_cancel_callback), options, FALSE);

  gtk_window_present (GTK_WINDOW (options->query_box));
}
