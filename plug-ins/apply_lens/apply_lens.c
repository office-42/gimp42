/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * Apply lens plug-in --- makes your selected part of the image look like it
 *                        is viewed under a solid lens.
 * Copyright (C) 1997 Morten Eriksen
 * mortene@pvv.ntnu.no
 * (If you do anything cool with this plug-in, or have ideas for
 * improvements (which aren't on my ToDo-list) - send me an email).
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

/*
 * Compile with (on Linux):
 * gcc -I/usr/local/include -I/usr/local/include/glib -o apply_lens apply_lens.c -L/usr/local/lib -L/usr/X11/lib -lgtk -lgdk -lgimp -lglib -lXext -lX11 -lm
 *
 */

/* Version 0.1:
 * 
 * First release. No known serious bugs, and basically does what you want.
 * All fancy features postponed until the next release, though. :)
 *
 */

/*
  TO DO:
  - antialiasing
  - preview image
  - adjustable (R, G, B and A) filter
  - optimize for speed!
  - refraction index warning dialog box when value < 1.0
  - use "true" lens with specified thickness
  - option to apply inverted lens
  - adjustable "c" value in the ellipsoid formula
  - radiobuttons for "ellipsoid" or "only horiz" and "only vert" (like in the
    Ad*b* Ph*t*sh*p Spherify plug-in..)
  - clean up source code
 */

#include <stdlib.h>
#include <math.h>

#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#ifndef M_PI
#define M_PI    3.14159265358979323846
#endif /* M_PI */

#define ENTRY_WIDTH 100

/* Declare local functions.
 */
static void query(void);
static void run(char *name, int nparams,
		GParam *param,
		int *nreturn_vals,
		GParam **return_vals);

static void drawlens(GDrawable *drawable);
static gint lens_dialog(GDrawable *drawable);

GPlugInInfo PLUG_IN_INFO =
{
  NULL, /* init_proc */
  NULL, /* quit_proc */
  query, /* query_proc */
  run, /* run_proc */
};

typedef struct
{
  gdouble refraction;
  gint keep_surr, use_bkgr, set_transparent;
} LensValues;

static LensValues lvals =
{
  /* Lens refraction value */
  1.7,
  /* Surroundings options */
  TRUE, FALSE, FALSE
};

typedef struct
{
  gint run;
} LensInterface;

static LensInterface bint =
{
  FALSE  /*  run  */
};

MAIN()

static void
query(void)
{
  static GParamDef args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image (unused)" },
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
    { PARAM_FLOAT, "refraction", "Lens refraction index" },
    { PARAM_INT32, "keep_surroundings", "Keep lens surroundings" },
    { PARAM_INT32, "set_background", "Set lens surroundings to bkgr value" },
    { PARAM_INT32, "set_transparent", "Set lens surroundings transparent" },
  };

  static GParamDef *return_vals = NULL;
  static int nargs = sizeof(args)/ sizeof(args[0]);
  static int nreturn_vals = 0;

  gimp_install_procedure("plug_in_applylens",
			 "Apply a lens effect",
			 "This plug-in uses Snell's law to draw an ellipsoid lens over the image",
			 "Morten Eriksen",
			 "Morten Eriksen",
			 "1997",
			 "<Image>/Filters/Glass Effects/Apply Lens",
			 "RGB*, GRAY*, INDEXED*",
			 PROC_PLUG_IN,
			 nargs, nreturn_vals,
			 args, return_vals);
}

static void
run(char *name,
    int nparams,
    GParam *param,
    int *nreturn_vals,
    GParam **return_vals)
{
  static GParam values[1];
  GDrawable *drawable;
  GRunModeType run_mode;
  GStatusType status = STATUS_SUCCESS;

  run_mode = param[0].data.d_int32;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
  
  *nreturn_vals = 1;
  *return_vals = values;
  
  drawable = gimp_drawable_get(param[2].data.d_drawable);

  switch(run_mode) {
  case RUN_INTERACTIVE:
    gimp_get_data("plug_in_applylens", &lvals);
    if(!lens_dialog(drawable)) return;
    break;

  case RUN_NONINTERACTIVE:
    if(nparams != 7) status = STATUS_CALLING_ERROR;
    if(status == STATUS_SUCCESS) {
      lvals.refraction = param[3].data.d_float;
      lvals.keep_surr = param[4].data.d_int32;
      lvals.use_bkgr = param[5].data.d_int32;
      lvals.set_transparent = param[6].data.d_int32;
    }

    if(status == STATUS_SUCCESS && (lvals.refraction < 1.0))
      status = STATUS_CALLING_ERROR;
    break;

  case RUN_WITH_LAST_VALS:
    gimp_get_data ("plug_in_applylens", &lvals);
    break;
    
  default:
    break;
  }

  gimp_tile_cache_ntiles(2 *(drawable->width / gimp_tile_width() + 1));
  gimp_progress_init("Applying lens...");
  drawlens(drawable);

  if(run_mode != RUN_NONINTERACTIVE)
    gimp_displays_flush();
  if(run_mode == RUN_INTERACTIVE)
    gimp_set_data("plug_in_applylens", &lvals, sizeof(LensValues));

  values[0].data.d_status = status;
  
  gimp_drawable_detach(drawable);
}

/*
  Ellipsoid formula: x^2/a^2 + y^2/b^2 + z^2/c^2 = 1
 */
static void
find_projected_pos(gfloat a, gfloat b,
		   gfloat x, gfloat y,
		   gfloat *projx, gfloat *projy)
{
  gfloat c;
  gfloat n[3];
  gfloat nxangle, nyangle, theta1, theta2;
  gfloat ri1 = 1.0, ri2 = lvals.refraction;

  /* PARAM */
  c = MIN(a, b);

  n[0] = x;
  n[1] = y;
  n[2] = sqrt((1-x*x/(a*a)-y*y/(b*b))*(c*c));

  nxangle = acos(n[0]/sqrt(n[0]*n[0]+n[2]*n[2]));
  theta1 = M_PI/2 - nxangle;
  theta2 = asin(sin(theta1)*ri1/ri2);
  theta2 = M_PI/2 - nxangle - theta2;
  *projx = x - tan(theta2)*n[2];

  nyangle = acos(n[1]/sqrt(n[1]*n[1]+n[2]*n[2]));
  theta1 = M_PI/2 - nyangle;
  theta2 = asin(sin(theta1)*ri1/ri2);
  theta2 = M_PI/2 - nyangle - theta2;
  *projy = y - tan(theta2)*n[2];
}

static void
drawlens(GDrawable *drawable)
{
  GPixelRgn srcPR, destPR;
  gint width, height;
  gint bytes;
  gint row;
  gint x1, y1, x2, y2;
  guchar *src, *dest;
  gint i, col;
  gfloat regionwidth, regionheight, dx, dy, xsqr, ysqr;
  gfloat a, b, asqr, bsqr, x, y;
  glong pixelpos, pos;
  guchar bgr_red, bgr_blue, bgr_green, alphaval;
  GDrawableType drawtype = gimp_drawable_type(drawable->id);

  gimp_palette_get_background(&bgr_red, &bgr_green, &bgr_blue);

  gimp_drawable_mask_bounds(drawable->id, &x1, &y1, &x2, &y2);
  regionwidth = x2-x1;
  a = regionwidth/2;
  regionheight = y2-y1;
  b = regionheight/2;

  asqr = a*a;
  bsqr = b*b;

  width = drawable->width;
  height = drawable->height;
  bytes = drawable->bpp;

  gimp_pixel_rgn_init(&srcPR, drawable, 0, 0, width, height, FALSE, FALSE);
  gimp_pixel_rgn_init(&destPR, drawable, 0, 0, width, height, TRUE, TRUE);

  src = g_malloc((x2-x1)*(y2-y1)*bytes);
  dest = g_malloc((x2-x1)*(y2-y1)*bytes);
  gimp_pixel_rgn_get_rect(&srcPR, src, x1, y1, regionwidth, regionheight);

  for(col = 0; col < regionwidth; col++) {
    dx = (gfloat)col - a + 0.5;
    xsqr = dx*dx;
    for(row = 0; row < regionheight; row++) {
      pixelpos = (col+row*regionwidth)*bytes;
      dy = -((gfloat)row - b) - 0.5;
      ysqr = dy*dy;
      if(ysqr < (bsqr - (bsqr*xsqr)/asqr)) {
	find_projected_pos(a, b, dx, dy, &x, &y);
	y = -y;
	pos = ((gint)(y+b)*regionwidth + (gint)(x+a)) * bytes;

	for(i = 0; i < bytes; i++) {
	  dest[pixelpos+i] = src[pos+i];
	}
      }
      else {
	if(lvals.keep_surr) {
	  for(i = 0; i < bytes; i++) {
	    dest[pixelpos+i] = src[pixelpos+i];
	  }
	}
	else {
	  if(lvals.set_transparent) alphaval = 0;
	  else alphaval = 255;

	  switch(drawtype) {
	  case INDEXEDA_IMAGE:
	    dest[pixelpos+1] = alphaval;
	  case INDEXED_IMAGE:
	    dest[pixelpos+0] = 0;
	    break;

	  case RGBA_IMAGE:
	    dest[pixelpos+3] = alphaval;
	  case RGB_IMAGE:
	    dest[pixelpos+0] = bgr_red;
	    dest[pixelpos+1] = bgr_green;
	    dest[pixelpos+2] = bgr_blue;
	    break;

	  case GRAYA_IMAGE:
	    dest[pixelpos+1] = alphaval;
	  case GRAY_IMAGE:
	    dest[pixelpos+0] = bgr_red;
	    break;
	  }
	}
      }
    }

      
    if(((gint)(regionwidth-col) % 5) == 0)
      gimp_progress_update((gdouble)col/(gdouble)regionwidth);
  }

  gimp_pixel_rgn_set_rect(&destPR, dest, x1, y1, regionwidth, regionheight);
  g_free(src);
  g_free(dest);

  gimp_drawable_flush(drawable);
  gimp_drawable_merge_shadow(drawable->id, TRUE);
  gimp_drawable_update(drawable->id, x1, y1,(x2 - x1),(y2 - y1));
}

static void
lens_close_callback(GtkWidget *widget,
		    gpointer   data)
{
  gimp_main_loop_quit();
}

static void
lens_ok_callback(GtkWidget *widget,
		 gpointer   data)
{
  bint.run = TRUE;
  gtk_window_destroy(GTK_WINDOW (data));
}

static void
lens_toggle_update(GtkWidget *widget,
		   gpointer   data)
{
  int *toggle_val;

  toggle_val = (int *)data;

  if(gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    *toggle_val = TRUE;
  else
    *toggle_val = FALSE;
}

static void
lens_entry_callback(GtkWidget *widget,
		    gpointer   data)
{
  lvals.refraction = atof(gtk_editable_get_text(GTK_EDITABLE(widget)));
  if(lvals.refraction < 1.0) lvals.refraction = 1.0;
}


static gint
lens_dialog(GDrawable *drawable)
{
  GtkWidget *dlg;
  GtkWidget *label;
  GtkWidget *entry;
  GtkWidget *button;
  GtkWidget *toggle;
  GtkWidget *frame;
  GtkWidget *vbox;
  GtkWidget *hbox;
  gchar buffer[12];
  GtkWidget *group = NULL;
  GDrawableType drawtype;

  drawtype = gimp_drawable_type(drawable->id);

  gtk_init();

  dlg = gimp_dialog_new("Lens effect");
  g_signal_connect(dlg, "destroy",
		   G_CALLBACK(lens_close_callback),
		   NULL);

  gimp_dialog_add_button(dlg, "OK", G_CALLBACK(lens_ok_callback),
			 dlg, TRUE);
  button = gimp_dialog_add_button(dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped(button, "clicked",
			   G_CALLBACK(gtk_window_destroy),
			   dlg);

  frame = gtk_frame_new("Parameter Settings");
  gimp_container_set_border_width(frame, 10);
  gimp_box_pack_start(gimp_dialog_get_vbox(dlg), frame, TRUE, TRUE, 0);
  vbox = gimp_vbox_new(FALSE, 5);
  gimp_container_set_border_width(vbox, 10);
  gtk_frame_set_child(GTK_FRAME(frame), vbox);

  toggle = gimp_radio_button_new(group,
				 "Keep original surroundings");
  group = toggle;
  gtk_box_append(GTK_BOX(vbox), toggle);
  g_signal_connect(toggle, "toggled",
		   G_CALLBACK(lens_toggle_update),
		   &lvals.keep_surr);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(toggle), lvals.keep_surr);

  toggle =
    gimp_radio_button_new(group,
			  drawtype == INDEXEDA_IMAGE ||
			  drawtype == INDEXED_IMAGE ?
			  "Set surroundings to index 0" :
			  "Set surroundings to background color");
  gtk_box_append(GTK_BOX(vbox), toggle);
  g_signal_connect(toggle, "toggled",
		   G_CALLBACK(lens_toggle_update),
		   &lvals.use_bkgr);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(toggle), lvals.use_bkgr);

  if((drawtype == INDEXEDA_IMAGE) ||
     (drawtype == GRAYA_IMAGE) ||
     (drawtype == RGBA_IMAGE)) {
    toggle = gimp_radio_button_new(group,
				   "Make surroundings transparent");
    gtk_box_append(GTK_BOX(vbox), toggle);
    g_signal_connect(toggle, "toggled",
		     G_CALLBACK(lens_toggle_update),
		     &lvals.set_transparent);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(toggle),
				lvals.set_transparent);
  }


  hbox = gimp_hbox_new(FALSE, 5);
  gimp_box_pack_start(vbox, hbox, TRUE, TRUE, 0);

  label = gtk_label_new("Lens refraction index: ");
  gimp_box_pack_start(hbox, label, TRUE, FALSE, 0);

  entry = gtk_entry_new();
  gimp_box_pack_start(hbox, entry, TRUE, TRUE, 0);
  gtk_widget_set_size_request(entry, ENTRY_WIDTH, -1);
  gtk_editable_set_width_chars(GTK_EDITABLE(entry), 6);
  sprintf(buffer, "%.2f", lvals.refraction);
  gtk_editable_set_text(GTK_EDITABLE(entry), buffer);
  g_signal_connect(entry, "changed",
		   G_CALLBACK(lens_entry_callback),
		   NULL);

  gtk_window_present(GTK_WINDOW(dlg));

  gimp_main_loop_run();

  return bint.run;
}
