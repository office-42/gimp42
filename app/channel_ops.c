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
#include <string.h>
#include <math.h>
#include "appenv.h"
#include "actionarea.h"
#include "channel_ops.h"
#include "drawable.h"
#include "floating_sel.h"
#include "general.h"
#include "gdisplay.h"
#include "interface.h"
#include "palette.h"

#include "channel_pvt.h"

#define ENTRY_WIDTH        60
#define OFFSET_BACKGROUND  0
#define OFFSET_TRANSPARENT 1

typedef struct
{
  GtkWidget *dlg;
  GtkWidget *fill_options;
  GtkWidget *off_x_entry;
  GtkWidget *off_y_entry;

  int        wrap_around;
  int        transparent;
  int        background;
  int        gimage_id;

} OffsetDialog;

/*  Local procedures  */
static void     offset     (GImage *gimage,
			    GimpDrawable *drawable,
			    int     wrap_around,
			    int     fill_type,
			    int     offset_x,
			    int     offset_y);

static void  offset_ok_callback       (GtkWidget *widget,
				       gpointer   data);
static void  offset_cancel_callback   (GtkWidget *widget,
				       gpointer   data);

static gboolean offset_delete_callback (GtkWindow *window,
					gpointer   data);

static void  offset_toggle_update     (GtkWidget *widget,
				       gpointer   data);
static void  offset_wraparound_update (GtkWidget *widget,
				       gpointer   data);
static void  offset_halfheight_update (GtkWidget *widget,
				       gpointer   data);

static Argument * channel_ops_offset_invoker     (Argument *args);


void
channel_ops_offset (void *gimage_ptr)
{
  OffsetDialog *off_d;
  GtkWidget *label;
  GtkWidget *check;
  GtkWidget *push;
  GtkWidget *toggle;
  GtkWidget *vbox;
  GtkWidget *toggle_vbox;
  GtkWidget *table;
  GtkWidget *group = NULL;
  GimpDrawable *drawable;
  GImage *gimage;

  gimage = (GImage *) gimage_ptr;
  drawable = gimage_active_drawable (gimage);

  off_d = g_new (OffsetDialog, 1);
  off_d->wrap_around = TRUE;
  off_d->transparent = drawable_has_alpha (drawable);
  off_d->background = !off_d->transparent;
  off_d->gimage_id = gimage->ID;

  off_d->dlg = gimp_dialog_new ("Offset");

  /* handle the wm close signal */
  g_signal_connect (off_d->dlg, "close-request",
		    G_CALLBACK (offset_delete_callback),
		    off_d);

  /*  Action area  */
  gimp_dialog_add_button (off_d->dlg, "OK",
			  G_CALLBACK (offset_ok_callback), off_d, TRUE);
  gimp_dialog_add_button (off_d->dlg, "Cancel",
				   G_CALLBACK (offset_cancel_callback), off_d,
				   FALSE);

  /*  The vbox for first column of options  */
  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, 1);
  gimp_box_pack_start (gimp_dialog_get_vbox (off_d->dlg), vbox, TRUE, TRUE, 0);

  /*  the table for offsets  */
  table = gimp_table_new (2, 2, FALSE);
  gimp_box_pack_start (vbox, table, TRUE, TRUE, 0);

  label = gtk_label_new ("Offset X:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 0, 1, GIMP_FILL, GIMP_FILL, 2, 2);
  off_d->off_x_entry = gtk_entry_new ();
  gtk_editable_set_text (GTK_EDITABLE (off_d->off_x_entry), "0");
  gtk_widget_set_size_request (off_d->off_x_entry, ENTRY_WIDTH, -1);
  gimp_table_attach (table, off_d->off_x_entry, 1, 2, 0, 1, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 2, 2);

  label = gtk_label_new ("Offset Y:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 1, 2, GIMP_FILL, GIMP_FILL, 2, 2);
  off_d->off_y_entry = gtk_entry_new ();
  gtk_editable_set_text (GTK_EDITABLE (off_d->off_y_entry), "0");
  gtk_widget_set_size_request (off_d->off_y_entry, ENTRY_WIDTH, -1);
  gimp_table_attach (table, off_d->off_y_entry, 1, 2, 1, 2, GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 2, 2);

  /*  the wrap around option  */
  check = gtk_check_button_new_with_label ("Wrap-Around");
  gimp_box_pack_start (vbox, check, FALSE, FALSE, 0);

  /*  The fill options  */
  off_d->fill_options = gtk_frame_new ("Fill Options");
  gimp_box_pack_start (vbox, off_d->fill_options, FALSE, TRUE, 0);
  toggle_vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (toggle_vbox, 5);
  gtk_frame_set_child (GTK_FRAME (off_d->fill_options), toggle_vbox);

  toggle = gimp_radio_button_new (group, "Background");
  group = toggle;
  gimp_box_pack_start (toggle_vbox, toggle, FALSE, FALSE, 0);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), off_d->background);
  g_signal_connect (toggle, "toggled",
		    G_CALLBACK (offset_toggle_update),
		    &off_d->background);

  if (drawable_has_alpha (drawable))
    {
      toggle = gimp_radio_button_new (group, "Transparent");
      gimp_box_pack_start (toggle_vbox, toggle, FALSE, FALSE, 0);
      gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), off_d->transparent);
      g_signal_connect (toggle, "toggled",
			G_CALLBACK (offset_toggle_update),
			&off_d->transparent);
    }

  /*  the by half height and half width offtion */
  push = gtk_button_new_with_label ("Offset by (x/2),(y/2)");
  gimp_box_pack_start (vbox, push, FALSE, FALSE,0);

  /*  Hook up the wrap around  */
  g_signal_connect (check, "toggled",
		    G_CALLBACK (offset_wraparound_update),
		    off_d);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (check), off_d->wrap_around);
  gtk_widget_set_sensitive (off_d->fill_options, !off_d->wrap_around);

  /* Hook up the by half */
  g_signal_connect (push, "clicked",
		    G_CALLBACK (offset_halfheight_update),
		    off_d);

  gtk_window_present (GTK_WINDOW (off_d->dlg));
}


static void
offset (GImage *gimage,
	GimpDrawable *drawable,
	int     wrap_around,
	int     fill_type,
	int     offset_x,
	int     offset_y)
{
  PixelRegion srcPR, destPR;
  TileManager *new_tiles;
  int width, height;
  int src_x, src_y;
  int dest_x, dest_y;
  unsigned char fill[MAX_CHANNELS] = { 0 };

  if (!drawable) 
    return;

  width = drawable_width (drawable);
  height = drawable_height (drawable);

  if (wrap_around)
    {
      offset_x %= width;
      offset_y %= height;
    }
  else
    {
      offset_x = BOUNDS (offset_x, -width, width);
      offset_y = BOUNDS (offset_y, -height, height);
    }

  if (offset_x == 0 && offset_y == 0)
    return;

  new_tiles = tile_manager_new (width, height, drawable_bytes (drawable));
  if (offset_x >= 0)
    {
      src_x = 0;
      dest_x = offset_x;
      width = BOUNDS ((width - offset_x), 0, width);
    }
  else
    {
      src_x = -offset_x;
      dest_x = 0;
      width = BOUNDS ((width + offset_x), 0, width);
    }

  if (offset_y >= 0)
    {
      src_y = 0;
      dest_y = offset_y;
      height = BOUNDS ((height - offset_y), 0, height);
    }
  else
    {
      src_y = -offset_y;
      dest_y = 0;
      height = BOUNDS ((height + offset_y), 0, height);
    }

  /*  Copy the center region  */
  if (width && height)
    {
      pixel_region_init (&srcPR, drawable_data (drawable), src_x, src_y, width, height, FALSE);
      pixel_region_init (&destPR, new_tiles, dest_x, dest_y, width, height, TRUE);

      copy_region (&srcPR, &destPR);
    }

  /*  Copy appropriately for wrap around  */
  if (wrap_around == TRUE)
    {
      if (offset_x >= 0 && offset_y >= 0)
	{
	  src_x = drawable_width (drawable) - offset_x;
	  src_y = drawable_height (drawable) - offset_y;
	}
      else if (offset_x >= 0 && offset_y < 0)
	{
	  src_x = drawable_width (drawable) - offset_x;
	  src_y = 0;
	}
      else if (offset_x < 0 && offset_y >= 0)
	{
	  src_x = 0;
	  src_y = drawable_height (drawable) - offset_y;
	}
      else if (offset_x < 0 && offset_y < 0)
	{
	  src_x = 0;
	  src_y = 0;
	}

      dest_x = (src_x + offset_x) % drawable_width (drawable);
      if (dest_x < 0)
	dest_x = drawable_width (drawable) + dest_x;
      dest_y = (src_y + offset_y) % drawable_height (drawable);
      if (dest_y < 0)
	dest_y = drawable_height (drawable) + dest_y;

      /*  intersecting region  */
      if (offset_x != 0 && offset_y != 0)
	{
	  pixel_region_init (&srcPR, drawable_data (drawable), src_x, src_y,
			     ABS (offset_x), ABS (offset_y), FALSE);
	  pixel_region_init (&destPR, new_tiles, dest_x, dest_y, ABS (offset_x), ABS (offset_y), TRUE);
	  copy_region (&srcPR, &destPR);
	}

      /*  X offset  */
      if (offset_x != 0)
	{
	  if (offset_y >= 0)
	    {
	      pixel_region_init (&srcPR, drawable_data (drawable), src_x, 0,
				 ABS (offset_x), drawable_height (drawable) - ABS (offset_y), FALSE);
	      pixel_region_init (&destPR, new_tiles, dest_x, dest_y + offset_y,
				 ABS (offset_x), drawable_height (drawable) - ABS (offset_y), TRUE);
	    }
	  else if (offset_y < 0)
	    {
	      pixel_region_init (&srcPR, drawable_data (drawable), src_x, src_y - offset_y,
				 ABS (offset_x), drawable_height (drawable) - ABS (offset_y), FALSE);
	      pixel_region_init (&destPR, new_tiles, dest_x, 0,
				 ABS (offset_x), drawable_height (drawable) - ABS (offset_y), TRUE);
	    }

	  copy_region (&srcPR, &destPR);
	}

      /*  X offset  */
      if (offset_y != 0)
	{
	  if (offset_x >= 0)
	    {
	      pixel_region_init (&srcPR, drawable_data (drawable), 0, src_y,
				 drawable_width (drawable) - ABS (offset_x), ABS (offset_y), FALSE);
	      pixel_region_init (&destPR, new_tiles, dest_x + offset_x, dest_y,
				 drawable_width (drawable) - ABS (offset_x), ABS (offset_y), TRUE);
	    }
	  else if (offset_x < 0)
	    {
	      pixel_region_init (&srcPR, drawable_data (drawable), src_x - offset_x, src_y,
				 drawable_width (drawable) - ABS (offset_x), ABS (offset_y), FALSE);
	      pixel_region_init (&destPR, new_tiles, 0, dest_y,
				 drawable_width (drawable) - ABS (offset_x), ABS (offset_y), TRUE);
	    }

	  copy_region (&srcPR, &destPR);
	}
    }
  /*  Otherwise, fill the vacated regions  */
  else
    {
      if (fill_type == OFFSET_BACKGROUND)
	{
	  palette_get_background (&fill[0], &fill[1], &fill[2]);
	  if (drawable_has_alpha (drawable))
	    fill[drawable_bytes (drawable) - 1] = OPAQUE_OPACITY;
	}

      if (offset_x >= 0 && offset_y >= 0)
	{
	  dest_x = 0;
	  dest_y = 0;
	}
      else if (offset_x >= 0 && offset_y < 0)
	{
	  dest_x = 0;
	  dest_y = drawable_height (drawable) + offset_y;
	}
      else if (offset_x < 0 && offset_y >= 0)
	{
	  dest_x = drawable_width (drawable) + offset_x;
	  dest_y = 0;
	}
      else if (offset_x < 0 && offset_y < 0)
	{
	  dest_x = drawable_width (drawable) + offset_x;
	  dest_y = drawable_height (drawable) + offset_y;
	}

      /*  intersecting region  */
      if (offset_x != 0 && offset_y != 0)
	{
	  pixel_region_init (&destPR, new_tiles, dest_x, dest_y, ABS (offset_x), ABS (offset_y), TRUE);
	  color_region (&destPR, fill);
	}

      /*  X offset  */
      if (offset_x != 0)
	{
	  if (offset_y >= 0)
	    pixel_region_init (&destPR, new_tiles, dest_x, dest_y + offset_y,
			       ABS (offset_x), drawable_height (drawable) - ABS (offset_y), TRUE);
	  else if (offset_y < 0)
	    pixel_region_init (&destPR, new_tiles, dest_x, 0,
			       ABS (offset_x), drawable_height (drawable) - ABS (offset_y), TRUE);

	  color_region (&destPR, fill);
	}

      /*  X offset  */
      if (offset_y != 0)
	{
	  if (offset_x >= 0)
	    pixel_region_init (&destPR, new_tiles, dest_x + offset_x, dest_y,
			       drawable_width (drawable) - ABS (offset_x), ABS (offset_y), TRUE);
	  else if (offset_x < 0)
	    pixel_region_init (&destPR, new_tiles, 0, dest_y,
			       drawable_width (drawable) - ABS (offset_x), ABS (offset_y), TRUE);

	  color_region (&destPR, fill);
	}
    }

  /*  push an undo  */
  drawable_apply_image (drawable, 0, 0, drawable_width (drawable), drawable_height (drawable),
			drawable_data (drawable), FALSE);

  /*  swap the tiles  */
  drawable->tiles = new_tiles;

  /*  update the drawable  */
  drawable_update (drawable, 0, 0, drawable_width (drawable), drawable_height (drawable));
}

/*
 *  Interface callbacks
 */

static void
offset_ok_callback (GtkWidget *widget,
		    gpointer   data)
{
  OffsetDialog *off_d;
  GImage *gimage;
  GimpDrawable *drawable;
  int offset_x, offset_y;
  int fill_type;

  off_d = (OffsetDialog *) data;
  if ((gimage = gimage_get_ID (off_d->gimage_id)) != NULL)
    {
      drawable = gimage_active_drawable (gimage);

      offset_x = (int) atof (gtk_editable_get_text (GTK_EDITABLE (off_d->off_x_entry)));
      offset_y = (int) atof (gtk_editable_get_text (GTK_EDITABLE (off_d->off_y_entry)));

      if (off_d->transparent)
	fill_type = OFFSET_TRANSPARENT;
      else
	fill_type = OFFSET_BACKGROUND;

      offset (gimage, drawable, off_d->wrap_around, fill_type, offset_x, offset_y);
      gdisplays_flush ();
    }

  gtk_window_destroy (GTK_WINDOW (off_d->dlg));
  g_free (off_d);
}

static gboolean
offset_delete_callback (GtkWindow *window,
			gpointer   data)
{
  offset_cancel_callback (GTK_WIDGET (window), data);

  return TRUE;
}

static void
offset_cancel_callback (GtkWidget *widget,
			gpointer   data)
{
  OffsetDialog *off_d;

  off_d = (OffsetDialog *) data;
  gtk_window_destroy (GTK_WINDOW (off_d->dlg));
  g_free (off_d);
}

static void
offset_toggle_update (GtkWidget *widget,
		      gpointer   data)
{
  int *toggle_val;

  toggle_val = (int *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    *toggle_val = TRUE;
  else
    *toggle_val = FALSE;
}

static void
offset_wraparound_update (GtkWidget *widget,
			  gpointer   data)
{
  OffsetDialog *off_d;

  off_d = (OffsetDialog *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    off_d->wrap_around = TRUE;
  else
    off_d->wrap_around = FALSE;

  gtk_widget_set_sensitive (off_d->fill_options, !off_d->wrap_around);
}

static void
offset_halfheight_update (GtkWidget *widget,
			  gpointer data)
{
  OffsetDialog *off_d;
  GImage *gimage;
  gchar buffer[16];

  off_d = (OffsetDialog *) data;
  gimage = gimage_get_ID (off_d->gimage_id);

  g_snprintf (buffer, sizeof (buffer), "%d", gimage->width / 2);
  gtk_editable_set_text (GTK_EDITABLE (off_d->off_x_entry), buffer);

  g_snprintf (buffer, sizeof (buffer), "%d", gimage->height / 2);
  gtk_editable_set_text (GTK_EDITABLE (off_d->off_y_entry), buffer);
}



/*
 *  Procedure database functions and data structures
 */

/*
 *  Procedure database functions and data structures
 */

/*  The offset procedure definition  */
ProcArg channel_ops_offset_args[] =
{
  { PDB_IMAGE,
    "image",
    "the image"
  },
  { PDB_DRAWABLE,
    "drawable",
    "the drawable to offset"
  },
  { PDB_INT32,
    "wrap_around",
    "wrap image around or fill vacated regions"
  },
  { PDB_INT32,
    "fill_type",
    "fill vacated regions of drawable with background or transparent: { OFFSET_BACKGROUND (0), OFFSET_TRANSPARENT (1) }"
  },
  { PDB_INT32,
    "offset_x",
    "offset by this amount in X direction"
  },
  { PDB_INT32,
    "offset_y",
    "offset by this amount in Y direction"
  }
};

ProcRecord channel_ops_offset_proc =
{
  "gimp_channel_ops_offset",
  "Offset the drawable by the specified amounts in the X and Y directions",
  "This procedure offsets the specified drawable by the amounts specified by 'offset_x' and 'offset_y'.  If 'wrap_around' is set to TRUE, then portions of the drawable which are offset out of bounds are wrapped around.  Alternatively, the undefined regions of the drawable can be filled with transparency or the background color, as specified by the 'fill_type' parameter.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1997",
  PDB_INTERNAL,

  /*  Input arguments  */
  6,
  channel_ops_offset_args,

  /*  Output arguments  */
  0,
  NULL,

  /*  Exec method  */
  { { channel_ops_offset_invoker } },
};


static Argument *
channel_ops_offset_invoker (Argument *args)
{
  int success = TRUE;
  int int_value;
  GImage *gimage;
  GimpDrawable *drawable;
  int wrap_around;
  int fill_type;
  int offset_x;
  int offset_y;

  /*  the gimage  */
  if (success)
    {
      int_value = args[0].value.pdb_int;
      if (! (gimage = gimage_get_ID (int_value)))
	success = FALSE;
    }
  if (success)
    {
      int_value = args[1].value.pdb_int;
      drawable = drawable_get_ID (int_value);
      if (drawable == NULL || gimage != drawable_gimage (drawable))
	success = FALSE;
    }
  if (success)
    {
      wrap_around = (args[2].value.pdb_int) ? TRUE : FALSE;
    }
  if (success)
    {
      fill_type = args[3].value.pdb_int;
      if (fill_type < OFFSET_BACKGROUND || fill_type > OFFSET_TRANSPARENT)
	success = FALSE;
    }
  if (success)
    {
      offset_x = args[4].value.pdb_int;
      offset_y = args[5].value.pdb_int;
    }

  if (success)
    offset (gimage, drawable, wrap_around, fill_type, offset_x, offset_y);

  return procedural_db_return_args (&channel_ops_offset_proc, success);
}
