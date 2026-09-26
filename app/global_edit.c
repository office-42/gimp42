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
#include "drawable.h"
#include "floating_sel.h"
#include "gdisplay.h"
#include "gimage.h"
#include "gimage_mask.h"
#include "general.h"
#include "global_edit.h"
#include "interface.h"
#include "layer.h"
#include "paint_funcs.h"
#include "tools.h"
#include "undo.h"

#include "tile_manager_pvt.h"
#include "drawable_pvt.h"


/*  The named paste dialog  */
typedef struct _PasteNamedDlg PasteNamedDlg;
struct _PasteNamedDlg
{
  GtkWidget   *shell;
  GtkWidget   *list;
  int          paste_into;
  GDisplay    *gdisp;
};

/*  The named buffer structure...  */
typedef struct _named_buffer NamedBuffer;

struct _named_buffer
{
  TileManager * buf;
  char *        name;
};


/*  The named buffer list  */
GSList * named_buffers = NULL;

/*  The global edit buffer  */
TileManager * global_buf = NULL;


/*  Crop the buffer to the size of pixels with non-zero transparency */

TileManager *
crop_buffer (TileManager *tiles,
	     int          border)
{
  PixelRegion PR;
  TileManager *new_tiles;
  int bytes, alpha;
  unsigned char * data;
  int empty;
  int x1, y1, x2, y2;
  int x, y;
  int ex, ey;
  int found;
  void * pr;
  unsigned char black[MAX_CHANNELS] = { 0, 0, 0, 0 };

  bytes = tiles->levels[0].bpp;
  alpha = bytes - 1;

  /*  go through and calculate the bounds  */
  x1 = tiles->levels[0].width;
  y1 = tiles->levels[0].height;
  x2 = 0;
  y2 = 0;

  pixel_region_init (&PR, tiles, 0, 0, x1, y1, FALSE);
  for (pr = pixel_regions_register (1, &PR); pr != NULL; pr = pixel_regions_process (pr))
    {
      data = PR.data + alpha;
      ex = PR.x + PR.w;
      ey = PR.y + PR.h;

      for (y = PR.y; y < ey; y++)
	{
	  found = FALSE;
	  for (x = PR.x; x < ex; x++, data+=bytes)
	    if (*data)
	      {
		if (x < x1)
		  x1 = x;
		if (x > x2)
		  x2 = x;
		found = TRUE;
	      }
	  if (found)
	    {
	      if (y < y1)
		y1 = y;
	      if (y > y2)
		y2 = y;
	    }
	}
    }

  x2 = BOUNDS (x2 + 1, 0, tiles->levels[0].width);
  y2 = BOUNDS (y2 + 1, 0, tiles->levels[0].height);

  empty = (x1 == tiles->levels[0].width && y1 == tiles->levels[0].height);

  /*  If there are no visible pixels, return NULL */
  if (empty)
    new_tiles = NULL;
  /*  If no cropping, return original buffer  */
  else if (x1 == 0 && y1 == 0 && x2 == tiles->levels[0].width &&
	   y2 == tiles->levels[0].height && border == 0)
    new_tiles = tiles;
  /*  Otherwise, crop the original area  */
  else
    {
      PixelRegion srcPR, destPR;
      int new_width, new_height;

      new_width = (x2 - x1) + border * 2;
      new_height = (y2 - y1) + border * 2;
      new_tiles = tile_manager_new (new_width, new_height, bytes);

      /*  If there is a border, make sure to clear the new tiles first  */
      if (border)
	{
	  pixel_region_init (&destPR, new_tiles, 0, 0, new_width, border, TRUE);
	  color_region (&destPR, black);
	  pixel_region_init (&destPR, new_tiles, 0, border, border, (y2 - y1), TRUE);
	  color_region (&destPR, black);
	  pixel_region_init (&destPR, new_tiles, new_width - border, border, border, (y2 - y1), TRUE);
	  color_region (&destPR, black);
	  pixel_region_init (&destPR, new_tiles, 0, new_height - border, new_width, border, TRUE);
	  color_region (&destPR, black);
	}

      pixel_region_init (&srcPR, tiles, x1, y1, (x2 - x1), (y2 - y1), FALSE);
      pixel_region_init (&destPR, new_tiles, border, border, (x2 - x1), (y2 - y1), TRUE);

      copy_region (&srcPR, &destPR);

      new_tiles->x = x1;
      new_tiles->y = y1;
    }

  return new_tiles;
}

TileManager *
edit_cut (GImage *gimage,
	  GimpDrawable *drawable)
{
  TileManager *cut;
  TileManager *cropped_cut;
  int empty;

  if (!gimage || drawable == NULL)
    return NULL;

  /*  Start a group undo  */
  undo_push_group_start (gimage, EDIT_CUT_UNDO);

  /*  See if the gimage mask is empty  */
  empty = gimage_mask_is_empty (gimage);

  /*  Next, cut the mask portion from the gimage  */
  cut = gimage_mask_extract (gimage, drawable, TRUE, FALSE);

  /*  Only crop if the gimage mask wasn't empty  */
  if (cut && empty == FALSE)
    {
      cropped_cut = crop_buffer (cut, 0);

      if (cropped_cut != cut)
	tile_manager_destroy (cut);
    }
  else if (cut)
    cropped_cut = cut;
  else
    cropped_cut = NULL;

  /*  end the group undo  */
  undo_push_group_end (gimage);

  if (cropped_cut)
    {
      /*  Free the old global edit buffer  */
      if (global_buf)
	tile_manager_destroy (global_buf);
      /*  Set the global edit buffer  */
      global_buf = cropped_cut;

      return cropped_cut;
    }
  else
    return NULL;
}

TileManager *
edit_copy (GImage *gimage,
	   GimpDrawable *drawable)
{
  TileManager * copy;
  TileManager * cropped_copy;
  int empty;

  if (!gimage || drawable == NULL)
    return NULL;

  /*  See if the gimage mask is empty  */
  empty = gimage_mask_is_empty (gimage);

  /*  First, copy the masked portion of the gimage  */
  copy = gimage_mask_extract (gimage, drawable, FALSE, FALSE);

  /*  Only crop if the gimage mask wasn't empty  */
  if (copy && empty == FALSE)
    {
      cropped_copy = crop_buffer (copy, 0);

      if (cropped_copy != copy)
	tile_manager_destroy (copy);
    }
  else if (copy)
    cropped_copy = copy;
  else
    cropped_copy = NULL;

  if (cropped_copy)
    {
      /*  Free the old global edit buffer  */
      if (global_buf)
	tile_manager_destroy (global_buf);
      /*  Set the global edit buffer  */
      global_buf = cropped_copy;

      return cropped_copy;
    }
  else
    return NULL;
}

int
edit_paste (GImage      *gimage,
	    GimpDrawable *drawable,
	    TileManager *paste,
	    int          paste_into)
{
  Layer * float_layer;
  int x1, y1, x2, y2;
  int cx, cy;

  /*  Make a new floating layer  */
  float_layer = layer_from_tiles (gimage, drawable, paste, "Pasted Layer", OPAQUE_OPACITY, NORMAL);

  if (float_layer)
    {
      /*  Start a group undo  */
      undo_push_group_start (gimage, EDIT_PASTE_UNDO);

      /*  Set the offsets to the center of the image  */
      drawable_offsets ( (drawable), &cx, &cy);
      drawable_mask_bounds ( (drawable), &x1, &y1, &x2, &y2);
      cx += (x1 + x2) >> 1;
      cy += (y1 + y2) >> 1;

      GIMP_DRAWABLE(float_layer)->offset_x = cx - (GIMP_DRAWABLE(float_layer)->width >> 1);
      GIMP_DRAWABLE(float_layer)->offset_y = cy - (GIMP_DRAWABLE(float_layer)->height >> 1);

      /*  If there is a selection mask clear it--
       *  this might not always be desired, but in general,
       *  it seems like the correct behavior.
       */
      if (! gimage_mask_is_empty (gimage) && !paste_into)
	channel_clear (gimage_get_mask (gimage));

      /*  add a new floating selection  */
      floating_sel_attach (float_layer, drawable);

      /*  end the group undo  */
      undo_push_group_end (gimage);

      return GIMP_DRAWABLE(float_layer)->ID;
    }
  else
    return 0;
}

int
edit_clear (GImage *gimage,
	    GimpDrawable *drawable)
{
  TileManager *buf_tiles;
  PixelRegion bufPR;
  int x1, y1, x2, y2;
  unsigned char col[MAX_CHANNELS];

  if (!gimage || drawable == NULL)
    return FALSE;

  gimage_get_background (gimage, drawable, col);
  if (drawable_has_alpha (drawable))
    col [drawable_bytes (drawable) - 1] = OPAQUE_OPACITY;

  drawable_mask_bounds (drawable, &x1, &y1, &x2, &y2);

  if (!(x2 - x1) || !(y2 - y1))
    return FALSE;

  buf_tiles = tile_manager_new ((x2 - x1), (y2 - y1), drawable_bytes (drawable));
  pixel_region_init (&bufPR, buf_tiles, 0, 0, (x2 - x1), (y2 - y1), TRUE);
  color_region (&bufPR, col);

  pixel_region_init (&bufPR, buf_tiles, 0, 0, (x2 - x1), (y2 - y1), FALSE);
  gimage_apply_image (gimage, drawable, &bufPR, 1, OPAQUE_OPACITY,
		      ERASE_MODE, NULL, x1, y1);

  /*  update the image  */
  drawable_update (drawable, x1, y1, (x2 - x1), (y2 - y1));

  /*  free the temporary tiles  */
  tile_manager_destroy (buf_tiles);

  return TRUE;
}

int
edit_fill (GImage *gimage,
	   GimpDrawable *drawable)
{
  TileManager *buf_tiles;
  PixelRegion bufPR;
  int x1, y1, x2, y2;
  unsigned char col[MAX_CHANNELS];

  if (!gimage || drawable == NULL)
    return FALSE;

  gimage_get_background (gimage, drawable, col);
  if (drawable_has_alpha (drawable))
    col [drawable_bytes (drawable) - 1] = OPAQUE_OPACITY;

  drawable_mask_bounds (drawable, &x1, &y1, &x2, &y2);

  if (!(x2 - x1) || !(y2 - y1))
    return FALSE;

  buf_tiles = tile_manager_new ((x2 - x1), (y2 - y1), drawable_bytes (drawable));
  pixel_region_init (&bufPR, buf_tiles, 0, 0, (x2 - x1), (y2 - y1), TRUE);
  color_region (&bufPR, col);

  pixel_region_init (&bufPR, buf_tiles, 0, 0, (x2 - x1), (y2 - y1), FALSE);
  gimage_apply_image (gimage, drawable, &bufPR, 1, OPAQUE_OPACITY,
		      NORMAL_MODE, NULL, x1, y1);

  /*  update the image  */
  drawable_update (drawable, x1, y1, (x2 - x1), (y2 - y1));

  /*  free the temporary tiles  */
  tile_manager_destroy (buf_tiles);

  return TRUE;
}

/*  The system clipboard.
 *
 *  Cut and Copy still fill the GIMP's own buffer, and also put the image
 *  on the system clipboard, so it can be pasted into other programs.
 *  Paste takes an image another program put on the clipboard; when the
 *  clipboard is the GIMP's own, it pastes its own buffer, which keeps
 *  the exact pixels (grayscale, alpha) the clipboard's RGBA would lose.
 */

static GdkClipboard *
global_edit_clipboard (void)
{
  GdkDisplay *display;

  if (no_interface)
    return NULL;

  display = gdk_display_get_default ();

  return display ? gdk_display_get_clipboard (display) : NULL;
}

/*  An edit buffer (gray, gray + alpha, RGB or RGBA) as a texture.  */
static GdkTexture *
global_edit_buffer_to_texture (TileManager *tiles)
{
  PixelRegion srcPR;
  GdkTexture *texture;
  GBytes *bytes;
  guchar *pixels;
  guchar *row;
  int width, height, bpp;
  int x, y;

  width  = tiles->levels[0].width;
  height = tiles->levels[0].height;
  bpp    = tiles->levels[0].bpp;

  if (width <= 0 || height <= 0 || bpp < 1 || bpp > 4)
    return NULL;

  pixels = g_malloc ((gsize) width * height * 4);
  row = g_malloc (width * bpp);

  pixel_region_init (&srcPR, tiles, 0, 0, width, height, FALSE);

  for (y = 0; y < height; y++)
    {
      guchar *d = pixels + (gsize) y * width * 4;

      pixel_region_get_row (&srcPR, 0, y, width, row, 1);

      for (x = 0; x < width; x++, d += 4)
	{
	  const guchar *p = row + x * bpp;

	  switch (bpp)
	    {
	    case 1: d[0] = d[1] = d[2] = p[0]; d[3] = 255;  break;
	    case 2: d[0] = d[1] = d[2] = p[0]; d[3] = p[1]; break;
	    case 3: d[0] = p[0]; d[1] = p[1]; d[2] = p[2]; d[3] = 255;  break;
	    case 4: d[0] = p[0]; d[1] = p[1]; d[2] = p[2]; d[3] = p[3]; break;
	    }
	}
    }

  g_free (row);

  bytes = g_bytes_new_take (pixels, (gsize) width * height * 4);
  texture = gdk_memory_texture_new (width, height, GDK_MEMORY_R8G8B8A8,
				    bytes, width * 4);
  g_bytes_unref (bytes);

  return texture;
}

/*  A texture from the clipboard as an RGBA edit buffer.  */
static TileManager *
global_edit_texture_to_buffer (GdkTexture *texture)
{
  GdkTextureDownloader *downloader;
  PixelRegion destPR;
  TileManager *tiles;
  guchar *pixels;
  gsize stride;
  int width, height;
  int y;

  width  = gdk_texture_get_width (texture);
  height = gdk_texture_get_height (texture);
  if (width <= 0 || height <= 0)
    return NULL;

  stride = (gsize) width * 4;
  pixels = g_malloc (stride * height);

  downloader = gdk_texture_downloader_new (texture);
  gdk_texture_downloader_set_format (downloader, GDK_MEMORY_R8G8B8A8);
  gdk_texture_downloader_download_into (downloader, pixels, stride);
  gdk_texture_downloader_free (downloader);

  tiles = tile_manager_new (width, height, 4);
  tiles->x = 0;
  tiles->y = 0;

  pixel_region_init (&destPR, tiles, 0, 0, width, height, TRUE);
  for (y = 0; y < height; y++)
    pixel_region_set_row (&destPR, 0, y, width, pixels + y * stride);

  g_free (pixels);

  return tiles;
}

static void
global_edit_export (TileManager *tiles)
{
  GdkClipboard *clipboard = global_edit_clipboard ();
  GdkTexture *texture;

  if (!clipboard || !tiles)
    return;

  texture = global_edit_buffer_to_texture (tiles);
  if (texture)
    {
      gdk_clipboard_set_texture (clipboard, texture);
      g_object_unref (texture);
    }
}

int
global_edit_cut (void *gdisp_ptr)
{
  GDisplay *gdisp;

  /*  stop any active tool  */
  gdisp = (GDisplay *) gdisp_ptr;
  active_tool_control (HALT, gdisp_ptr);

  if (!edit_cut (gdisp->gimage, gimage_active_drawable (gdisp->gimage)))
    return FALSE;
  else
    {
      global_edit_export (global_buf);

      /*  flush the display  */
      gdisplays_flush ();
      return TRUE;
    }
}

int
global_edit_copy (void *gdisp_ptr)
{
  GDisplay *gdisp;

  gdisp = (GDisplay *) gdisp_ptr;

  if (!edit_copy (gdisp->gimage, gimage_active_drawable (gdisp->gimage)))
    return FALSE;
  else
    {
      global_edit_export (global_buf);
      return TRUE;
    }
}

typedef struct
{
  int display_ID;
  int paste_into;
} PasteRequest;

static int
global_edit_paste_buffer (GDisplay *gdisp,
			  int       paste_into)
{
  if (!global_buf ||
      !edit_paste (gdisp->gimage, gimage_active_drawable (gdisp->gimage),
		   global_buf, paste_into))
    return FALSE;

  /*  flush the display  */
  gdisplays_flush ();
  return TRUE;
}

static void
global_edit_paste_texture_ready (GObject      *source,
				 GAsyncResult *result,
				 gpointer      data)
{
  PasteRequest *request = data;
  GdkTexture *texture;
  GDisplay *gdisp;

  texture = gdk_clipboard_read_texture_finish (GDK_CLIPBOARD (source),
					       result, NULL);
  if (texture)
    {
      TileManager *tiles = global_edit_texture_to_buffer (texture);

      if (tiles)
	{
	  if (global_buf)
	    tile_manager_destroy (global_buf);
	  global_buf = tiles;
	}
      g_object_unref (texture);
    }

  /*  The display may have been closed while the clipboard was read.  */
  gdisp = gdisplay_get_ID (request->display_ID);
  if (gdisp && gimage_active_drawable (gdisp->gimage))
    {
      active_tool_control (HALT, gdisp);
      global_edit_paste_buffer (gdisp, request->paste_into);
    }

  g_free (request);
}

int
global_edit_paste (void *gdisp_ptr,
		   int   paste_into)
{
  GDisplay *gdisp;
  GdkClipboard *clipboard;

  /*  stop any active tool  */
  gdisp = (GDisplay *) gdisp_ptr;
  active_tool_control (HALT, gdisp_ptr);

  clipboard = global_edit_clipboard ();

  /*  Another program's image: read it, then paste.  */
  if (clipboard && !gdk_clipboard_is_local (clipboard))
    {
      PasteRequest *request = g_new0 (PasteRequest, 1);

      request->display_ID = gdisp->ID;
      request->paste_into = paste_into;

      gdk_clipboard_read_texture_async (clipboard, NULL,
					global_edit_paste_texture_ready,
					request);
      return TRUE;
    }

  return global_edit_paste_buffer (gdisp, paste_into);
}

void
global_edit_free ()
{
  if (global_buf)
    tile_manager_destroy (global_buf);

  global_buf = NULL;
}

/*********************************************/
/*        Named buffer operations            */

static void
set_list_of_named_buffers (GtkWidget *list_widget)
{
  GSList *list;
  NamedBuffer *nb;
  GtkWidget *label;
  GtkWidget *row;

  gtk_list_box_remove_all (GTK_LIST_BOX (list_widget));
  list = named_buffers;

  while (list)
    {
      nb = (NamedBuffer *) list->data;
      list = g_slist_next (list);

      label = gtk_label_new (nb->name);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_list_box_append (GTK_LIST_BOX (list_widget), label);
      row = gtk_widget_get_parent (label);
      g_object_set_data (G_OBJECT (row), "user_data", (gpointer) nb);
    }

  /*  browse mode: there is always a selected buffer  */
  row = GTK_WIDGET (gtk_list_box_get_row_at_index (GTK_LIST_BOX (list_widget), 0));
  if (row)
    gtk_list_box_select_row (GTK_LIST_BOX (list_widget), GTK_LIST_BOX_ROW (row));
}

/*  The buffer of the selected row, or NULL.  */
static NamedBuffer *
named_buffer_get_selected (PasteNamedDlg *pn_dlg)
{
  GtkListBoxRow *row;

  row = gtk_list_box_get_selected_row (GTK_LIST_BOX (pn_dlg->list));
  if (!row)
    return NULL;

  return (NamedBuffer *) g_object_get_data (G_OBJECT (row), "user_data");
}

static void
named_buffer_paste_callback (GtkWidget *w,
			     gpointer   client_data)
{
  PasteNamedDlg *pn_dlg;
  NamedBuffer *nb;

  pn_dlg = (PasteNamedDlg *) client_data;

  nb = named_buffer_get_selected (pn_dlg);
  if (nb)
    edit_paste (pn_dlg->gdisp->gimage,
		gimage_active_drawable (pn_dlg->gdisp->gimage),
		nb->buf, pn_dlg->paste_into);

  /*  Destroy the box  */
  gtk_window_destroy (GTK_WINDOW (pn_dlg->shell));

  g_free (pn_dlg);

  /*  flush the display  */
  gdisplays_flush ();
}

static void
named_buffer_delete_callback (GtkWidget *w,
			      gpointer   client_data)
{
  PasteNamedDlg *pn_dlg;
  NamedBuffer * nb;

  pn_dlg = (PasteNamedDlg *) client_data;

  nb = named_buffer_get_selected (pn_dlg);
  if (nb)
    {
      named_buffers = g_slist_remove (named_buffers, (void *) nb);
      g_free (nb->name);
      tile_manager_destroy (nb->buf);
      g_free (nb);
    }

  set_list_of_named_buffers (pn_dlg->list);
}

static void
named_buffer_cancel_callback (GtkWidget *w,
			      gpointer   client_data)
{
  PasteNamedDlg *pn_dlg;

  pn_dlg = (PasteNamedDlg *) client_data;

  /*  Destroy the box  */
  gtk_window_destroy (GTK_WINDOW (pn_dlg->shell));

  g_free (pn_dlg);
}

static gboolean
named_buffer_dialog_delete_callback (GtkWindow *w,
				     gpointer   client_data)
{
  named_buffer_cancel_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}

static void
named_buffer_paste_into_update (GtkWidget *w,
				gpointer   client_data)
{
  PasteNamedDlg *pn_dlg;

  pn_dlg = (PasteNamedDlg *) client_data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    pn_dlg->paste_into = FALSE;
  else
    pn_dlg->paste_into = TRUE;
}

static void
paste_named_buffer (GDisplay *gdisp)
{
  static ActionAreaItem action_items[3] =
  {
    { "Paste", named_buffer_paste_callback, NULL, NULL },
    { "Delete", named_buffer_delete_callback, NULL, NULL },
    { "Cancel", named_buffer_cancel_callback, NULL, NULL }
  };
  PasteNamedDlg *pn_dlg;
  GtkWidget *vbox;
  GtkWidget *label;
  GtkWidget *paste_into;
  GtkWidget *listbox;

  pn_dlg = (PasteNamedDlg *) g_malloc (sizeof (PasteNamedDlg));
  pn_dlg->gdisp = gdisp;
  pn_dlg->paste_into = TRUE;	/*  "Replace Current Selection" is off  */

  pn_dlg->shell = gimp_dialog_new ("Paste Named Buffer");

  g_signal_connect (pn_dlg->shell, "close-request",
		    G_CALLBACK (named_buffer_dialog_delete_callback),
		    pn_dlg);

  vbox = gimp_vbox_new (FALSE, 1);
  gimp_container_set_border_width (vbox, 1);
  gimp_box_pack_start (gimp_dialog_get_vbox (pn_dlg->shell), vbox, TRUE, TRUE, 0);

  label = gtk_label_new ("Select a buffer to paste:");
  gimp_box_pack_start (vbox, label, FALSE, FALSE, 0);

  listbox = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (listbox),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
  gimp_box_pack_start (vbox, listbox, TRUE, TRUE, 0);
  gtk_widget_set_size_request (listbox, 125, 150);

  pn_dlg->list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (pn_dlg->list), GTK_SELECTION_BROWSE);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (listbox), pn_dlg->list);
  set_list_of_named_buffers (pn_dlg->list);

  paste_into = gtk_check_button_new_with_label ("Replace Current Selection");
  gimp_box_pack_start (vbox, paste_into, FALSE, FALSE, 0);
  g_signal_connect (paste_into, "toggled",
		    G_CALLBACK (named_buffer_paste_into_update),
		    pn_dlg);

  action_items[0].user_data = pn_dlg;
  action_items[1].user_data = pn_dlg;
  action_items[2].user_data = pn_dlg;
  build_action_area (pn_dlg->shell, action_items, 3, 0);

  gtk_window_present (GTK_WINDOW (pn_dlg->shell));
}

static void
new_named_buffer (TileManager *tiles,
		  char        *name)
{
  PixelRegion srcPR, destPR;
  NamedBuffer *nb;

  if (! tiles) return;

  nb = (NamedBuffer *) g_malloc (sizeof (NamedBuffer));

  nb->buf = tile_manager_new (tiles->levels[0].width, tiles->levels[0].height, tiles->levels[0].bpp);
  pixel_region_init (&srcPR, tiles, 0, 0, tiles->levels[0].width, tiles->levels[0].height, FALSE);
  pixel_region_init (&destPR, nb->buf, 0, 0, tiles->levels[0].width, tiles->levels[0].height, TRUE);
  copy_region (&srcPR, &destPR);

  nb->name = g_strdup ((char *) name);
  named_buffers = g_slist_append (named_buffers, (void *) nb);
}

static void
cut_named_buffer_callback (GtkWidget *w,
			   gpointer   client_data,
			   gpointer   call_data)
{
  TileManager *new_tiles;
  GDisplay *gdisp;
  char *name;

  gdisp = (GDisplay *) client_data;
  name = g_strdup ((char *) call_data);
  
  new_tiles = edit_cut (gdisp->gimage, gimage_active_drawable (gdisp->gimage));
  if (new_tiles) 
    new_named_buffer (new_tiles, name);
  gdisplays_flush ();
}

int
named_edit_cut (void *gdisp_ptr)
{
  GDisplay *gdisp;

  /*  stop any active tool  */
  gdisp = (GDisplay *) gdisp_ptr;
  active_tool_control (HALT, gdisp_ptr);

  query_string_box ("Cut Named", "Enter a name for this buffer", NULL,
		    cut_named_buffer_callback, gdisp);
  return TRUE;
}

static void
copy_named_buffer_callback (GtkWidget *w,
			    gpointer   client_data,
			    gpointer   call_data)
{
  TileManager *new_tiles;
  GDisplay *gdisp;
  char *name;

  gdisp = (GDisplay *) client_data;
  name = g_strdup ((char *) call_data);
  
  new_tiles = edit_copy (gdisp->gimage, gimage_active_drawable (gdisp->gimage));
  if (new_tiles) 
    new_named_buffer (new_tiles, name);
}

int
named_edit_copy (void *gdisp_ptr)
{
  GDisplay *gdisp;

  gdisp = (GDisplay *) gdisp_ptr;
  
  query_string_box ("Copy Named", "Enter a name for this buffer", NULL,
		    copy_named_buffer_callback, gdisp);
  return TRUE;
}

int
named_edit_paste (void *gdisp_ptr)
{
  paste_named_buffer ((GDisplay *) gdisp_ptr);

  gdisplays_flush();

  return TRUE;
}

void
named_buffers_free ()
{
  GSList *list;
  NamedBuffer * nb;

  list = named_buffers;

  while (list)
    {
      nb = (NamedBuffer *) list->data;
      tile_manager_destroy (nb->buf);
      g_free (nb->name);
      g_free (nb);
      list = g_slist_next (list);
    }

  g_slist_free (named_buffers);
  named_buffers = NULL;
}
