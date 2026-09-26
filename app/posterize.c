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
#include "drawable.h"
#include "gdisplay.h"
#include "image_map.h"
#include "interface.h"
#include "posterize.h"

#define TEXT_WIDTH 55

typedef struct _Posterize Posterize;

struct _Posterize
{
  int x, y;    /*  coords for last mouse click  */
};

typedef struct _PosterizeDialog PosterizeDialog;

struct _PosterizeDialog
{
  GtkWidget   *shell;
  GtkWidget   *levels_text;

  GimpDrawable *drawable;
  ImageMap     image_map;
  int          levels;

  gint         preview;
};

/*  posterize action functions  */

static void   posterize_button_press   (Tool *, GimpButtonEvent *, gpointer);
static void   posterize_button_release (Tool *, GimpButtonEvent *, gpointer);
static void   posterize_motion         (Tool *, GimpMotionEvent *, gpointer);
static void   posterize_cursor_update  (Tool *, GimpMotionEvent *, gpointer);
static void   posterize_control        (Tool *, int, gpointer);

static PosterizeDialog *  posterize_new_dialog          (void);
static void               posterize_preview             (PosterizeDialog *);
static void               posterize_ok_callback         (GtkWidget *, gpointer);
static void               posterize_cancel_callback     (GtkWidget *, gpointer);
static void               posterize_preview_update      (GtkWidget *, gpointer);
static void               posterize_levels_text_update  (GtkWidget *, gpointer);
static gint               posterize_delete_callback     (GtkWidget *, gpointer);

static void *posterize_options = NULL;
static PosterizeDialog *posterize_dialog = NULL;

static void       posterize (PixelRegion *, PixelRegion *, void *);
static Argument * posterize_invoker (Argument *);

/*  posterize machinery  */

static void
posterize (PixelRegion *srcPR,
	   PixelRegion *destPR,
	   void        *user_data)
{
  PosterizeDialog *pd;
  unsigned char *src, *s;
  unsigned char *dest, *d;
  int has_alpha, alpha;
  int w, h, b, i;
  double interval, half_interval;
  unsigned char transfer[256];

  pd = (PosterizeDialog *) user_data;

  /*  Set the transfer array  */
  interval = 255.0 / (double) (pd->levels - 1);
  half_interval = interval / 2.0;

  for (i = 0; i < 256; i++)
    transfer[i] = (unsigned char) ((int) (((double) i + half_interval) / interval) * interval);

  h = srcPR->h;
  src = srcPR->data;
  dest = destPR->data;
  has_alpha = (srcPR->bytes == 2 || srcPR->bytes == 4);
  alpha = has_alpha ? srcPR->bytes - 1 : srcPR->bytes;

  while (h--)
    {
      w = srcPR->w;
      s = src;
      d = dest;
      while (w--)
	{
	  for (b = 0; b < alpha; b++)
	    d[b] = transfer[s[b]];

	  if (has_alpha)
	    d[alpha] = s[alpha];

	  s += srcPR->bytes;
	  d += destPR->bytes;
	}

      src += srcPR->rowstride;
      dest += destPR->rowstride;
    }
}


/*  by_color select action functions  */

static void
posterize_button_press (Tool           *tool,
			GimpButtonEvent *bevent,
			gpointer        gdisp_ptr)
{
  GDisplay *gdisp;

  gdisp = gdisp_ptr;
  tool->drawable = gimage_active_drawable (gdisp->gimage);
}

static void
posterize_button_release (Tool           *tool,
			  GimpButtonEvent *bevent,
			  gpointer        gdisp_ptr)
{
}

static void
posterize_motion (Tool           *tool,
		  GimpMotionEvent *mevent,
		  gpointer        gdisp_ptr)
{
}

static void
posterize_cursor_update (Tool           *tool,
			 GimpMotionEvent *mevent,
			 gpointer        gdisp_ptr)
{
  GDisplay *gdisp;

  gdisp = (GDisplay *) gdisp_ptr;
  gdisplay_install_tool_cursor (gdisp, GIMP_CURSOR_TOP_LEFT_ARROW);
}

static void
posterize_control (Tool     *tool,
		   int       action,
		   gpointer  gdisp_ptr)
{
  Posterize * post;

  post = (Posterize *) tool->private;

  switch (action)
    {
    case PAUSE :
      break;
    case RESUME :
      break;
    case HALT :
      if (posterize_dialog)
	{
	  active_tool->preserve = TRUE;
	  image_map_abort (posterize_dialog->image_map);
	  active_tool->preserve = FALSE;
	  posterize_dialog->image_map = NULL;
	  posterize_cancel_callback (NULL, (gpointer) posterize_dialog);
	}
      break;
    }
}

Tool *
tools_new_posterize ()
{
  Tool * tool;
  Posterize * private;

  /*  The tool options  */
  if (!posterize_options)
    posterize_options = tools_register_no_options (POSTERIZE, "Posterize Options");

  /*  The posterize dialog  */
  if (!posterize_dialog)
    posterize_dialog = posterize_new_dialog ();
  else
    if (!gtk_widget_get_visible (posterize_dialog->shell))
      gtk_window_present (GTK_WINDOW (posterize_dialog->shell));

  tool = (Tool *) g_malloc (sizeof (Tool));
  private = (Posterize *) g_malloc (sizeof (Posterize));

  tool->type = POSTERIZE;
  tool->state = INACTIVE;
  tool->scroll_lock = 1;  /*  Disallow scrolling  */
  tool->auto_snap_to = TRUE;
  tool->private = (void *) private;
  tool->button_press_func = posterize_button_press;
  tool->button_release_func = posterize_button_release;
  tool->motion_func = posterize_motion;
  tool->arrow_keys_func = standard_arrow_keys_func;
  tool->cursor_update_func = posterize_cursor_update;
  tool->control_func = posterize_control;
  tool->preserve = FALSE;
  tool->gdisp_ptr = NULL;
  tool->drawable = NULL;

  return tool;
}

void
tools_free_posterize (Tool *tool)
{
  Posterize * post;

  post = (Posterize *) tool->private;

  /*  Close the color select dialog  */
  if (posterize_dialog)
    posterize_cancel_callback (NULL, (gpointer) posterize_dialog);

  g_free (post);
}

void
posterize_initialize (void *gdisp_ptr)
{
  GDisplay *gdisp;

  gdisp = (GDisplay *) gdisp_ptr;

  if (drawable_indexed (gimage_active_drawable (gdisp->gimage)))
    {
      g_message ("Posterize does not operate on indexed drawables.");
      return;
    }

  /*  The posterize dialog  */
  if (!posterize_dialog)
    posterize_dialog = posterize_new_dialog ();
  else
    if (!gtk_widget_get_visible (posterize_dialog->shell))
      gtk_window_present (GTK_WINDOW (posterize_dialog->shell));
  posterize_dialog->drawable = gimage_active_drawable (gdisp->gimage);
  posterize_dialog->image_map = image_map_create (gdisp_ptr, posterize_dialog->drawable);
  if (posterize_dialog->preview)
    posterize_preview (posterize_dialog);
}


/****************************/
/*  Select by Color dialog  */
/****************************/

/*  the action area structure  */
static ActionAreaItem action_items[] =
{
  { "OK", posterize_ok_callback, NULL, NULL },
  { "Cancel", posterize_cancel_callback, NULL, NULL }
};

static PosterizeDialog *
posterize_new_dialog ()
{
  PosterizeDialog *pd;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *toggle;

  pd = g_malloc (sizeof (PosterizeDialog));
  pd->preview = TRUE;
  pd->levels = 3;

  /*  The shell and main vbox  */
  pd->shell = gimp_dialog_new ("Posterize");

  g_signal_connect (pd->shell, "close-request", G_CALLBACK (posterize_delete_callback),
		      pd);

  vbox = gimp_vbox_new (FALSE, 2);
  gimp_container_set_border_width (vbox, 2);
  gimp_box_pack_start (gimp_dialog_get_vbox (pd->shell), vbox, TRUE, TRUE, 0);

  /*  Horizontal box for levels text widget  */
  hbox = gimp_hbox_new (TRUE, 2);
  gimp_box_pack_start (vbox, hbox, FALSE, FALSE, 0);

  label = gtk_label_new ("Posterize Levels: ");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_box_pack_start (hbox, label, TRUE, FALSE, 0);

  /*  levels text  */
  pd->levels_text = gtk_entry_new ();
  gtk_editable_set_text (GTK_EDITABLE (pd->levels_text), "3");
  gtk_widget_set_size_request (pd->levels_text, TEXT_WIDTH, 25);
  gimp_box_pack_start (hbox, pd->levels_text, TRUE, FALSE, 0);
  g_signal_connect (pd->levels_text, "changed", G_CALLBACK (posterize_levels_text_update),
		      pd);

  /*  Horizontal box for preview  */
  hbox = gimp_hbox_new (TRUE, 2);
  gimp_box_pack_start (vbox, hbox, FALSE, FALSE, 0);

  /*  The preview toggle  */
  toggle = gtk_check_button_new_with_label ("Preview");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), pd->preview);
  gimp_box_pack_start (hbox, toggle, TRUE, FALSE, 0);
  g_signal_connect (toggle, "toggled", G_CALLBACK (posterize_preview_update),
		      pd);


  /*  The action area  */
  action_items[0].user_data = pd;
  action_items[1].user_data = pd;
  {
    int n;

    for (n = 0; n < 2; n++)
      gimp_dialog_add_button (pd->shell, action_items[n].label,
			      G_CALLBACK (action_items[n].callback),
			      action_items[n].user_data, n == 0);
  }

  gtk_window_present (GTK_WINDOW (pd->shell));

  return pd;
}

static void
posterize_preview (PosterizeDialog *pd)
{
  if (!pd->image_map)
    g_message ("posterize_preview(): No image map");
  active_tool->preserve = TRUE;
  image_map_apply (pd->image_map, posterize, (void *) pd);
  active_tool->preserve = FALSE;
}

static void
posterize_ok_callback (GtkWidget *widget,
		       gpointer   client_data)
{
  PosterizeDialog *pd;

  pd = (PosterizeDialog *) client_data;

  if (gtk_widget_get_visible (pd->shell))
    gtk_widget_set_visible (pd->shell, FALSE);

  active_tool->preserve = TRUE;

  if (!pd->preview)
    image_map_apply (pd->image_map, posterize, (void *) pd);

  if (pd->image_map)
    image_map_commit (pd->image_map);

  active_tool->preserve = FALSE;

  pd->image_map = NULL;
}

static gint 
posterize_delete_callback (GtkWidget *w, gpointer data)
{
  posterize_cancel_callback (w, data);

  return TRUE;
}

static void
posterize_cancel_callback (GtkWidget *widget,
			   gpointer   client_data)
{
  PosterizeDialog *pd;

  pd = (PosterizeDialog *) client_data;
  if (gtk_widget_get_visible (pd->shell))
    gtk_widget_set_visible (pd->shell, FALSE);

  if (pd->image_map)
    {
      active_tool->preserve = TRUE;
      image_map_abort (pd->image_map);
      active_tool->preserve = FALSE;
      gdisplays_flush ();
    }

  pd->image_map = NULL;
}

static void
posterize_preview_update (GtkWidget *w,
			  gpointer   data)
{
  PosterizeDialog *pd;

  pd = (PosterizeDialog *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    {
      pd->preview = TRUE;
      posterize_preview (pd);
    }
  else
    pd->preview = FALSE;
}

static void
posterize_levels_text_update (GtkWidget *w,
			      gpointer   data)
{
  PosterizeDialog *pd;
  const char *str;
  int value;

  pd = (PosterizeDialog *) data;
  str = gtk_editable_get_text (GTK_EDITABLE (w));
  value = BOUNDS (((int) atof (str)), 2, 256);

  if (value != pd->levels)
    {
      pd->levels = value;
      if (pd->preview)
	posterize_preview (pd);
    }
}


/*  The posterize procedure definition  */
ProcArg posterize_args[] =
{
  { PDB_IMAGE,
    "image",
    "the image"
  },
  { PDB_DRAWABLE,
    "drawable",
    "the drawable"
  },
  { PDB_INT32,
    "levels",
    "levels of posterization: (2 <= levels <= 255)"
  }
};

ProcRecord posterize_proc =
{
  "gimp_posterize",
  "Posterize the specified drawable",
  "This procedures reduces the number of shades allows in each intensity channel to the specified 'levels' parameter.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1997",
  PDB_INTERNAL,

  /*  Input arguments  */
  3,
  posterize_args,

  /*  Output arguments  */
  0,
  NULL,

  /*  Exec method  */
  { { posterize_invoker } },
};


static Argument *
posterize_invoker (Argument *args)
{
  PixelRegion srcPR, destPR;
  int success = TRUE;
  PosterizeDialog pd;
  GImage *gimage;
  GimpDrawable *drawable;
  int levels;
  int int_value;
  int x1, y1, x2, y2;
  void *pr;

  drawable = NULL;
  levels = 0;

  /*  the gimage  */
  if (success)
    {
      int_value = args[0].value.pdb_int;
      if (! (gimage = gimage_get_ID (int_value)))
	success = FALSE;
    }
  /*  the drawable  */
  if (success)
    {
      int_value = args[1].value.pdb_int;
      drawable = drawable_get_ID (int_value);
      if (drawable == NULL || gimage != drawable_gimage (drawable))
	success = FALSE;
    }
  /*  make sure the drawable is not indexed color  */
  if (success)
    success = ! drawable_indexed (drawable);
    
  /*  levels  */
  if (success)
    {
      int_value = args[2].value.pdb_int;
      if (int_value >= 2 && int_value < 256)
	levels = int_value;
      else
	success = FALSE;
    }

  /*  arrange to modify the levels  */
  if (success)
    {
      pd.levels = levels;

      /*  The application should occur only within selection bounds  */
      drawable_mask_bounds (drawable, &x1, &y1, &x2, &y2);

      pixel_region_init (&srcPR, drawable_data (drawable), x1, y1, (x2 - x1), (y2 - y1), FALSE);
      pixel_region_init (&destPR, drawable_shadow (drawable), x1, y1, (x2 - x1), (y2 - y1), TRUE);

      for (pr = pixel_regions_register (2, &srcPR, &destPR); pr != NULL; pr = pixel_regions_process (pr))
	posterize (&srcPR, &destPR, (void *) &pd);

      drawable_merge_shadow (drawable, TRUE);
      drawable_update (drawable, x1, y1, (x2 - x1), (y2 - y1));
    }

  return procedural_db_return_args (&posterize_proc, success);
}
