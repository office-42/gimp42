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
#include "brightness_contrast.h"
#include "drawable.h"
#include "general.h"
#include "gimage_mask.h"
#include "gdisplay.h"
#include "image_map.h"
#include "interface.h"

#define TEXT_WIDTH 45
#define TEXT_HEIGHT 25
#define SLIDER_WIDTH 200
#define SLIDER_HEIGHT 35

#define BRIGHTNESS_SLIDER 0x1
#define CONTRAST_SLIDER   0x2
#define BRIGHTNESS_TEXT   0x4
#define CONTRAST_TEXT     0x8
#define ALL               0xF

typedef struct _BrightnessContrast BrightnessContrast;

struct _BrightnessContrast
{
  int x, y;    /*  coords for last mouse click  */
};

typedef struct _BrightnessContrastDialog BrightnessContrastDialog;

struct _BrightnessContrastDialog
{
  GtkWidget   *shell;
  GtkWidget   *gimage_name;
  GtkWidget   *brightness_text;
  GtkWidget   *contrast_text;
  GtkAdjustment  *brightness_data;
  GtkAdjustment  *contrast_data;

  GimpDrawable *drawable;
  ImageMap     image_map;

  double       brightness;
  double       contrast;

  gint         preview;
};

/*  brightness contrast action functions  */

static void   brightness_contrast_button_press   (Tool *, GimpButtonEvent *, gpointer);
static void   brightness_contrast_button_release (Tool *, GimpButtonEvent *, gpointer);
static void   brightness_contrast_motion         (Tool *, GimpMotionEvent *, gpointer);
static void   brightness_contrast_cursor_update  (Tool *, GimpMotionEvent *, gpointer);
static void   brightness_contrast_control        (Tool *, int, gpointer);

static BrightnessContrastDialog *  brightness_contrast_new_dialog  (void);
static void   brightness_contrast_update                  (BrightnessContrastDialog *, int);
static void   brightness_contrast_preview                 (BrightnessContrastDialog *);
static void   brightness_contrast_ok_callback             (GtkWidget *, gpointer);
static void   brightness_contrast_cancel_callback         (GtkWidget *, gpointer);
static gint   brightness_contrast_delete_callback         (GtkWidget *, gpointer);
static void   brightness_contrast_preview_update          (GtkWidget *, gpointer);
static void   brightness_contrast_brightness_scale_update (GtkAdjustment *, gpointer);
static void   brightness_contrast_contrast_scale_update   (GtkAdjustment *, gpointer);
static void   brightness_contrast_brightness_text_update  (GtkWidget *, gpointer);
static void   brightness_contrast_contrast_text_update    (GtkWidget *, gpointer);

static void *brightness_contrast_options = NULL;
static BrightnessContrastDialog *brightness_contrast_dialog = NULL;

static void       brightness_contrast          (PixelRegion *, PixelRegion *, void *);
static Argument * brightness_contrast_invoker  (Argument *);

/*  brightness contrast machinery  */

static void
brightness_contrast (PixelRegion *srcPR,
		     PixelRegion *destPR,
		     void        *user_data)
{
  BrightnessContrastDialog *bcd;
  unsigned char *src, *s;
  unsigned char *dest, *d;
  unsigned char brightness[256];
  unsigned char contrast[256];
  double power;
  int has_alpha;
  int alpha;
  int w, h, b;
  int value;
  int i;

  bcd = (BrightnessContrastDialog *) user_data;

  /*  Set the transfer arrays  (for speed)  */
  h = srcPR->h;
  src = srcPR->data;
  dest = destPR->data;
  has_alpha = (srcPR->bytes == 2 || srcPR->bytes == 4);
  alpha = has_alpha ? srcPR->bytes - 1 : srcPR->bytes;

  if (bcd->brightness < 0)
    for (i = 0; i < 256; i++)
      brightness[i] = (unsigned char) ((i * (255 + bcd->brightness)) / 255);
  else
    for (i = 0; i < 256; i++)
      brightness[i] = (unsigned char) (i + ((255 - i) * bcd->brightness) / 255);

  if (bcd->contrast < 0)
    for (i = 0; i < 256; i++)
      {
	value = (i > 127) ? (255 - i) : i;
	value = (int) (127.0 * pow ((double) (value ? value : 1) / 127.0,
				    (double) (127 + bcd->contrast) / 127.0));
	value = BOUNDS (value, 0, 255);
	contrast[i] = (i > 127) ? (255 - value) : value;
      }
  else
    for (i = 0; i < 256; i++)
      {
	value = (i > 127) ? (255 - i) : i;
	power = (bcd->contrast == 127) ? 127 : 127.0 / (127 - bcd->contrast);
	value = (int) (127.0 * pow ((double) value / 127.0, power));
	value = BOUNDS (value, 0, 255);
	contrast[i] = (i > 127) ? (255 - value) : value;
      }

  while (h--)
    {
      w = srcPR->w;
      s = src;
      d = dest;
      while (w--)
	{
	  for (b = 0; b < alpha; b++)
	    d[b] = contrast[brightness[s[b]]];

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
brightness_contrast_button_press (Tool           *tool,
				  GimpButtonEvent *bevent,
				  gpointer        gdisp_ptr)
{
  GDisplay *gdisp;

  gdisp = gdisp_ptr;
  tool->drawable = gimage_active_drawable (gdisp->gimage);
}

static void
brightness_contrast_button_release (Tool           *tool,
				    GimpButtonEvent *bevent,
				    gpointer        gdisp_ptr)
{
}

static void
brightness_contrast_motion (Tool           *tool,
			    GimpMotionEvent *mevent,
			    gpointer        gdisp_ptr)
{
}

static void
brightness_contrast_cursor_update (Tool           *tool,
				   GimpMotionEvent *mevent,
				   gpointer        gdisp_ptr)
{
  GDisplay *gdisp;

  gdisp = (GDisplay *) gdisp_ptr;
  gdisplay_install_tool_cursor (gdisp, GIMP_CURSOR_TOP_LEFT_ARROW);
}

static void
brightness_contrast_control (Tool     *tool,
			     int       action,
			     gpointer  gdisp_ptr)
{
  switch (action)
    {
    case PAUSE :
      break;
    case RESUME :
      break;
    case HALT :
      if (brightness_contrast_dialog)
	{
	  active_tool->preserve = TRUE;
	  image_map_abort (brightness_contrast_dialog->image_map);
	  active_tool->preserve = TRUE;
	  brightness_contrast_dialog->image_map = NULL;
	  brightness_contrast_cancel_callback (NULL, (gpointer) brightness_contrast_dialog);
	}
      break;
    }
}

Tool *
tools_new_brightness_contrast ()
{
  Tool * tool;
  BrightnessContrast * private;

  /*  The tool options  */
  if (!brightness_contrast_options)
    brightness_contrast_options = tools_register_no_options (BRIGHTNESS_CONTRAST,
							     "Brightness-Contrast Options");

  tool = (Tool *) g_malloc (sizeof (Tool));
  private = (BrightnessContrast *) g_malloc (sizeof (BrightnessContrast));

  tool->type = BRIGHTNESS_CONTRAST;
  tool->state = INACTIVE;
  tool->scroll_lock = 1;  /*  Disallow scrolling  */
  tool->auto_snap_to = TRUE;
  tool->private = (void *) private;
  tool->button_press_func = brightness_contrast_button_press;
  tool->button_release_func = brightness_contrast_button_release;
  tool->motion_func = brightness_contrast_motion;
  tool->arrow_keys_func = standard_arrow_keys_func;
  tool->cursor_update_func = brightness_contrast_cursor_update;
  tool->control_func = brightness_contrast_control;
  tool->preserve = FALSE;
  tool->gdisp_ptr = NULL;
  tool->drawable = NULL;

  return tool;
}

void
tools_free_brightness_contrast (Tool *tool)
{
  BrightnessContrast * bc;

  bc = (BrightnessContrast *) tool->private;

  /*  Close the color select dialog  */
  if (brightness_contrast_dialog)
    brightness_contrast_cancel_callback (NULL, (gpointer) brightness_contrast_dialog);

  g_free (bc);
}

void
brightness_contrast_initialize (void *gdisp_ptr)
{
  GDisplay *gdisp;

  gdisp = (GDisplay *) gdisp_ptr;

  if (drawable_indexed (gimage_active_drawable (gdisp->gimage)))
    {
      g_message ("Brightness-Contrast does not operate on indexed drawables.");
      return;
    }

  /*  The brightness-contrast dialog  */
  if (!brightness_contrast_dialog)
    brightness_contrast_dialog = brightness_contrast_new_dialog ();
  else
    if (!gtk_widget_get_visible (brightness_contrast_dialog->shell))
      gtk_window_present (GTK_WINDOW (brightness_contrast_dialog->shell));

  /*  Initialize dialog fields  */
  brightness_contrast_dialog->image_map = NULL;
  brightness_contrast_dialog->brightness = 0.0;
  brightness_contrast_dialog->contrast = 0.0;

  brightness_contrast_dialog->drawable = gimage_active_drawable (gdisp->gimage);
  brightness_contrast_dialog->image_map = image_map_create (gdisp_ptr,
							    brightness_contrast_dialog->drawable);

  brightness_contrast_update (brightness_contrast_dialog, ALL);
}


/********************************/
/*  Brightness Contrast dialog  */
/********************************/

/*  the action area structure  */
static ActionAreaItem action_items[] =
{
  { "OK", brightness_contrast_ok_callback, NULL, NULL },
  { "Cancel", brightness_contrast_cancel_callback, NULL, NULL }
};

static BrightnessContrastDialog *
brightness_contrast_new_dialog ()
{
  BrightnessContrastDialog *bcd;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *slider;
  GtkWidget *toggle;
  GtkAdjustment *data;

  bcd = g_malloc (sizeof (BrightnessContrastDialog));
  bcd->preview = TRUE;

  /*  The shell and main vbox  */
  bcd->shell = gimp_dialog_new ("Brightness-Contrast");
  
  /* handle wm close signal */
  g_signal_connect (bcd->shell, "close-request", G_CALLBACK (brightness_contrast_delete_callback),
		      bcd);

  vbox = gimp_vbox_new (FALSE, 2);
  gimp_container_set_border_width (vbox, 2);
  gimp_box_pack_start (gimp_dialog_get_vbox (bcd->shell), vbox, TRUE, TRUE, 0);

  /*  The table containing sliders  */
  table = gimp_table_new (2, 3, FALSE);
  gimp_box_pack_start (vbox, table, FALSE, FALSE, 0);

  /*  Create the brightness scale widget  */
  label = gtk_label_new ("Brightness");
  gimp_misc_set_alignment (label, 1.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 0, 1,
		    GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK | GIMP_FILL, 2, 2);

  data = gtk_adjustment_new (0, -127, 127.0, 1.0, 1.0, 0.0);
  bcd->brightness_data = GTK_ADJUSTMENT (data);
  slider = gimp_hscale_new (GTK_ADJUSTMENT (data), 0);
  gtk_widget_set_size_request (slider, SLIDER_WIDTH, SLIDER_HEIGHT);
  gtk_scale_set_digits (GTK_SCALE (slider), 0);
  gimp_table_attach (table, slider, 1, 2, 0, 1,
		    GIMP_EXPAND | GIMP_SHRINK | GIMP_FILL,
		    GIMP_EXPAND | GIMP_SHRINK | GIMP_FILL,
		    2, 2);
  g_signal_connect (data, "value-changed", G_CALLBACK (brightness_contrast_brightness_scale_update),
		      bcd);

  bcd->brightness_text = gtk_entry_new ();
  gtk_widget_set_size_request (bcd->brightness_text, TEXT_WIDTH, TEXT_HEIGHT);
  gimp_table_attach (table, bcd->brightness_text, 2, 3, 0, 1,
		    GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK | GIMP_FILL, 2, 2);
  g_signal_connect (bcd->brightness_text, "changed", G_CALLBACK (brightness_contrast_brightness_text_update),
		      bcd);



  /*  Create the contrast scale widget  */
  label = gtk_label_new ("Contrast");
  gimp_misc_set_alignment (label, 1.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 1, 2,
		    GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK | GIMP_FILL, 2, 2);

  data = gtk_adjustment_new (0, -127.0, 127.0, 1.0, 1.0, 0.0);
  bcd->contrast_data = GTK_ADJUSTMENT (data);
  slider = gimp_hscale_new (GTK_ADJUSTMENT (data), 0);
  gtk_widget_set_size_request (slider, SLIDER_WIDTH, SLIDER_HEIGHT);
  gtk_scale_set_digits (GTK_SCALE (slider), 0);
  gimp_table_attach (table, slider, 1, 2, 1, 2,
		    GIMP_EXPAND | GIMP_SHRINK | GIMP_FILL,
		    GIMP_EXPAND | GIMP_SHRINK | GIMP_FILL,
		    2, 2);
  g_signal_connect (data, "value-changed", G_CALLBACK (brightness_contrast_contrast_scale_update),
		      bcd);

  bcd->contrast_text = gtk_entry_new ();
  gtk_widget_set_size_request (bcd->contrast_text, TEXT_WIDTH, TEXT_HEIGHT);
  gimp_table_attach (table, bcd->contrast_text, 2, 3, 1, 2,
		    GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK | GIMP_FILL, 2, 2);
  g_signal_connect (bcd->contrast_text, "changed", G_CALLBACK (brightness_contrast_contrast_text_update),
		      bcd);



  /*  Horizontal box for preview and preserve luminosity toggle buttons  */
  hbox = gimp_hbox_new (TRUE, 2);
  gimp_box_pack_start (vbox, hbox, FALSE, FALSE, 0);

  /*  The preview toggle  */
  toggle = gtk_check_button_new_with_label ("Preview");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), bcd->preview);
  gimp_box_pack_start (hbox, toggle, TRUE, FALSE, 0);
  g_signal_connect (toggle, "toggled", G_CALLBACK (brightness_contrast_preview_update),
		      bcd);



  /*  The action area  */
  action_items[0].user_data = bcd;
  action_items[1].user_data = bcd;
  {
    int n;

    for (n = 0; n < 2; n++)
      gimp_dialog_add_button (bcd->shell, action_items[n].label,
			      G_CALLBACK (action_items[n].callback),
			      action_items[n].user_data, n == 0);
  }

  gtk_window_present (GTK_WINDOW (bcd->shell));

  return bcd;
}

static void
brightness_contrast_update (BrightnessContrastDialog *bcd,
			    int                       update)
{
  char text[12];

  if (update & BRIGHTNESS_SLIDER)
    {
      gtk_adjustment_set_value (bcd->brightness_data, bcd->brightness);
    }
  if (update & CONTRAST_SLIDER)
    {
      gtk_adjustment_set_value (bcd->contrast_data, bcd->contrast);
    }
  if (update & BRIGHTNESS_TEXT)
    {
      sprintf (text, "%0.0f", bcd->brightness);
      gtk_editable_set_text (GTK_EDITABLE (bcd->brightness_text), text);
    }
  if (update & CONTRAST_TEXT)
    {
      sprintf (text, "%0.0f", bcd->contrast);
      gtk_editable_set_text (GTK_EDITABLE (bcd->contrast_text), text);
    }
}

static void
brightness_contrast_preview (BrightnessContrastDialog *bcd)
{
  if (!bcd->image_map)
    g_message ("brightness_contrast_preview(): No image map");
  active_tool->preserve = TRUE;
  image_map_apply (bcd->image_map, brightness_contrast, (void *) bcd);
  active_tool->preserve = FALSE;
}

static void
brightness_contrast_ok_callback (GtkWidget *widget,
				 gpointer   client_data)
{
  BrightnessContrastDialog *bcd;

  bcd = (BrightnessContrastDialog *) client_data;

  if (gtk_widget_get_visible (bcd->shell))
    gtk_widget_set_visible (bcd->shell, FALSE);

  active_tool->preserve = TRUE;

  if (!bcd->preview)
    image_map_apply (bcd->image_map, brightness_contrast, (void *) bcd);

  if (bcd->image_map)
    image_map_commit (bcd->image_map);

  active_tool->preserve = FALSE;

  bcd->image_map = NULL;
}

static gint
brightness_contrast_delete_callback (GtkWidget *w,
				     gpointer d)
{
  brightness_contrast_cancel_callback (w, d);

  return TRUE;
}

static void
brightness_contrast_cancel_callback (GtkWidget *widget,
				     gpointer   client_data)
{
  BrightnessContrastDialog *bcd;

  bcd = (BrightnessContrastDialog *) client_data;
  if (gtk_widget_get_visible (bcd->shell))
    gtk_widget_set_visible (bcd->shell, FALSE);

  if (bcd->image_map)
    {
      active_tool->preserve = TRUE;
      image_map_abort (bcd->image_map);
      active_tool->preserve = FALSE;
      gdisplays_flush ();
    }

  bcd->image_map = NULL;
}

static void
brightness_contrast_preview_update (GtkWidget *w,
				    gpointer   data)
{
  BrightnessContrastDialog *bcd;

  bcd = (BrightnessContrastDialog *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    {
      bcd->preview = TRUE;
      brightness_contrast_preview (bcd);
    }
  else
    bcd->preview = FALSE;
}

static void
brightness_contrast_brightness_scale_update (GtkAdjustment *adjustment,
					     gpointer       data)
{
  BrightnessContrastDialog *bcd;

  bcd = (BrightnessContrastDialog *) data;

  if (bcd->brightness != gtk_adjustment_get_value (adjustment))
    {
      bcd->brightness = gtk_adjustment_get_value (adjustment);
      brightness_contrast_update (bcd, BRIGHTNESS_TEXT);

      if (bcd->preview)
	brightness_contrast_preview (bcd);
    }
}

static void
brightness_contrast_contrast_scale_update (GtkAdjustment *adjustment,
					   gpointer       data)
{
  BrightnessContrastDialog *bcd;

  bcd = (BrightnessContrastDialog *) data;

  if (bcd->contrast != gtk_adjustment_get_value (adjustment))
    {
      bcd->contrast = gtk_adjustment_get_value (adjustment);
      brightness_contrast_update (bcd, CONTRAST_TEXT);

      if (bcd->preview)
	brightness_contrast_preview (bcd);
    }
}

static void
brightness_contrast_brightness_text_update (GtkWidget *w,
					    gpointer   data)
{
  BrightnessContrastDialog *bcd;
  const char *str;
  int value;

  str = gtk_editable_get_text (GTK_EDITABLE (w));
  bcd = (BrightnessContrastDialog *) data;
  value = BOUNDS (((int) atof (str)), -127, 127);

  if ((int) bcd->brightness != value)
    {
      bcd->brightness = value;
      brightness_contrast_update (bcd, BRIGHTNESS_SLIDER);

      if (bcd->preview)
	brightness_contrast_preview (bcd);
    }
}

static void
brightness_contrast_contrast_text_update (GtkWidget *w,
					  gpointer   data)
{
  BrightnessContrastDialog *bcd;
  const char *str;
  int value;

  str = gtk_editable_get_text (GTK_EDITABLE (w));
  bcd = (BrightnessContrastDialog *) data;
  value = BOUNDS (((int) atof (str)), -127, 127);

  if ((int) bcd->contrast != value)
    {
      bcd->contrast = value;
      brightness_contrast_update (bcd, CONTRAST_SLIDER);

      if (bcd->preview)
	brightness_contrast_preview (bcd);
    }
}


/*  The brightness_contrast procedure definition  */
ProcArg brightness_contrast_args[] =
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
    "brightness",
    "brightness adjustment: (-127 <= brightness <= 127)"
  },
  { PDB_INT32,
    "contrast",
    "constrast adjustment: (-127 <= contrast <= 127)"
  }
};

ProcRecord brightness_contrast_proc =
{
  "gimp_brightness_contrast",
  "Modify brightness/contrast in the specified drawable",
  "This procedures allows the brightness and contrast of the specified drawable to be modified.  Both 'brightness' and 'contrast' parameters are defined between -127 and 127.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1997",
  PDB_INTERNAL,

  /*  Input arguments  */
  4,
  brightness_contrast_args,

  /*  Output arguments  */
  0,
  NULL,

  /*  Exec method  */
  { { brightness_contrast_invoker } },
};


static Argument *
brightness_contrast_invoker (Argument *args)
{
  PixelRegion srcPR, destPR;
  int success = TRUE;
  int int_value;
  BrightnessContrastDialog bcd;
  GImage *gimage;
  int brightness;
  int contrast;
  int x1, y1, x2, y2;
  void *pr;
  GimpDrawable *drawable;

  drawable    = NULL;
  brightness  = 0;
  contrast    = 0;

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

  /*  brightness  */
  if (success)
    {
      int_value = args[2].value.pdb_int;
      if (int_value < -127 || int_value > 127)
	success = FALSE;
      else
	brightness = int_value;
    }
  /*  contrast  */
  if (success)
    {
      int_value = args[3].value.pdb_int;
      if (int_value < -127 || int_value > 127)
	success = FALSE;
      else
	contrast = int_value;
    }

  /*  arrange to modify the brightness/contrast  */
  if (success)
    {
      bcd.brightness = brightness;
      bcd.contrast = contrast;

      /*  The application should occur only within selection bounds  */
      drawable_mask_bounds (drawable, &x1, &y1, &x2, &y2);

      pixel_region_init (&srcPR, drawable_data (drawable), x1, y1, (x2 - x1), (y2 - y1), FALSE);
      pixel_region_init (&destPR, drawable_shadow (drawable), x1, y1, (x2 - x1), (y2 - y1), TRUE);

      for (pr = pixel_regions_register (2, &srcPR, &destPR); pr != NULL; pr = pixel_regions_process (pr))
	brightness_contrast (&srcPR, &destPR, (void *) &bcd);

      drawable_merge_shadow (drawable, TRUE);
      drawable_update (drawable, x1, y1, (x2 - x1), (y2 - y1));
    }

  return procedural_db_return_args (&brightness_contrast_proc, success);
}
