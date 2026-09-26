/* Sparkle --- image filter plug-in for The Gimp image manipulation program
 * Copyright (C) 1996 by John Beale;  ported to Gimp by Michael J. Hammel
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
 *
 * You can contact Michael at mjhammel@csn.net
 * Note: set tabstops to 3 to make this more readable.
 */

/*
 * Sparkle -  simulate pixel bloom and diffraction effects
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#ifndef M_PI
#define M_PI    3.14159265358979323846
#endif /* M_PI */

#define SCALE_WIDTH 125
#define TILE_CACHE_SIZE 16
#define MAX_CHANNELS 4
#define PSV 2  /* point spread value */
#define EPSILON 0.001
#define SQR(a) ((a) * (a))

typedef struct
{
  gdouble lum_threshold;
  gdouble flare_inten;
  gdouble spike_len;
  gdouble spike_pts;
  gdouble spike_angle;
} SparkleVals;

typedef struct
{
  gint       run;
} SparkleInterface;

/* Declare local functions.
 */
static void      query  (void);
static void      run    (char      *name,
			 int        nparams,
			 GParam    *param,
			 int       *nreturn_vals,
			 GParam   **return_vals);

static gint      sparkle_dialog        (void);
static void      sparkle_add_scale     (GtkWidget   *table,
					gint         row,
					const gchar *text,
					gdouble     *value,
					gdouble      lower,
					gdouble      upper,
					gdouble      step,
					gdouble      page,
					gint         digits);

static gint      compute_luminosity    (guchar *   pixel,
					gint       gray,
					gint       has_alpha);
static gint      compute_lum_threshold (GDrawable * drawable,
					gdouble    percentile);
static void      sparkle               (GDrawable * drawable,
					gint       threshold);

static void      fspike                (GPixelRgn * dest_rgn,
					gint       gray,
					gint       x1,
					gint       y1,
					gint       x2,
					gint       y2,
					gdouble    xr,
					gdouble    yr,
					gdouble    inten,
					gdouble    length,
					gdouble    angle);
static GTile*    rpnt                  (GDrawable * drawable,
					GTile *    tile,
					gint       x1,
					gint       y1,
					gint       x2,
					gint       y2,
					gdouble    xr,
					gdouble    yr,
					gint *     row,
					gint *     col,
					gint       bytes,
					gdouble *  inten);
static void      rgb_to_hsl            (gdouble    r,
					gdouble    g,
					gdouble    b,
					gdouble *  h,
					gdouble *  s,
					gdouble *  l);
static void      hsl_to_rgb            (gdouble    h,
					gdouble    sl,
					gdouble    l,
					gdouble *  r,
					gdouble *  g,
					gdouble *  b);

static void      sparkle_close_callback  (GtkWidget *widget,
					  gpointer   data);
static void      sparkle_ok_callback     (GtkWidget *widget,
					  gpointer   data);
static void      sparkle_scale_update    (GtkAdjustment *adjustment,
					  double        *scale_val);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

static SparkleVals svals =
{
  0.001,	/* luminosity threshold */
  0.5,		/* flare intensity */
  20.0,		/* spike length */
  6.0,		/* spike points */
  15.0		/* spike angle */
};

static SparkleInterface sint =
{
  FALSE          /* run */
};

static gint num_sparkles;


MAIN ()

static void
query ()
{
  static GParamDef args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image (unused)" },
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
    { PARAM_FLOAT, "lum_threshold", "Luminosity threshold (0.0 - 1.0)" },
    { PARAM_FLOAT, "flare_inten", "Flare intensity (0.0 - 1.0)" },
    { PARAM_FLOAT, "spike_len", "Spike length (in pixels)" },
    { PARAM_INT32, "spike_pts", "# of spike points" },
    { PARAM_FLOAT, "spike_angle", "Spike angle (0.0-360.0 degrees)" }
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0;

  gimp_install_procedure ("plug_in_sparkle",
			  "Simulates pixel bloom and diffraction effects",
			  "More here later",
			  "John Beale, & (ported to GIMP v0.54) Michael J. Hammel & (ported to GIMP v1.0) Spencer Kimball",
			  "John Beale",
			  "1996",
			  "<Image>/Filters/Light Effects/Sparkle",
			  "RGB*, GRAY*",
			  PROC_PLUG_IN,
			  nargs, nreturn_vals,
			  args, return_vals);
}

static void
run (char    *name,
     int      nparams,
     GParam  *param,
     int     *nreturn_vals,
     GParam **return_vals)
{
  static GParam values[1];
  GDrawable *drawable;
  GRunModeType run_mode;
  GStatusType status = STATUS_SUCCESS;
  gint threshold;

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;

  switch (run_mode)
    {
    case RUN_INTERACTIVE:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_sparkle", &svals);

      /*  First acquire information with a dialog  */
      if (! sparkle_dialog ())
	return;
      break;

    case RUN_NONINTERACTIVE:
      /*  Make sure all the arguments are there!  */
      if (nparams != 8)
	status = STATUS_CALLING_ERROR;
      if (status == STATUS_SUCCESS)
	{
	  svals.lum_threshold = param[3].data.d_float;
	  svals.flare_inten = param[4].data.d_float;
	  svals.spike_len = param[5].data.d_float;
	  svals.spike_pts = param[6].data.d_int32;
	  svals.spike_angle = param[7].data.d_float;
	}
      if (status == STATUS_SUCCESS &&
	  (svals.lum_threshold < 0.0 || svals.lum_threshold > 1.0))
	status = STATUS_CALLING_ERROR;
      if (status == STATUS_SUCCESS &&
	  (svals.flare_inten < 0.0 || svals.flare_inten > 1.0))
	status = STATUS_CALLING_ERROR;
      if (status == STATUS_SUCCESS &&
	  (svals.spike_pts < 0))
	status = STATUS_CALLING_ERROR;
      if (status == STATUS_SUCCESS &&
	  (svals.spike_angle < 0.0 || svals.spike_angle > 360.0))
	status = STATUS_CALLING_ERROR;
      break;

    case RUN_WITH_LAST_VALS:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_sparkle", &svals);
      break;

    default:
      break;
    }

  /*  Get the specified drawable  */
  drawable = gimp_drawable_get (param[2].data.d_drawable);

  /*  Make sure that the drawable is gray or RGB color  */
  if (gimp_drawable_color (drawable->id) || gimp_drawable_gray (drawable->id))
    {
      gimp_progress_init ("Sparkling...");
      gimp_tile_cache_ntiles (TILE_CACHE_SIZE);

      /*  compute the luminosity which exceeds the luminosity threshold  */
      threshold = compute_lum_threshold (drawable, svals.lum_threshold);
      sparkle (drawable, threshold);

      if (run_mode != RUN_NONINTERACTIVE)
	gimp_displays_flush ();

      /*  Store mvals data  */
      if (run_mode == RUN_INTERACTIVE)
	gimp_set_data ("plug_in_sparkle", &svals, sizeof (SparkleVals));
    }
  else
    {
      /* gimp_message ("blur: cannot operate on indexed color images"); */
      status = STATUS_EXECUTION_ERROR;
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

static gint
sparkle_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *frame;
  GtkWidget *table;

  gtk_init ();

  dlg = gimp_dialog_new ("Sparkle");
  g_signal_connect (dlg, "destroy",
		    G_CALLBACK (sparkle_close_callback), NULL);

  /*  Action area  */
  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (sparkle_ok_callback),
			  dlg, TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  /*  parameter settings  */
  frame = gtk_frame_new ("Parameter Settings");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);
  table = gimp_table_new (5, 2, FALSE);
  gimp_container_set_border_width (table, 10);
  gtk_frame_set_child (GTK_FRAME (frame), table);

  sparkle_add_scale (table, 0, "Luminosity Threshold", &svals.lum_threshold,
		     0.0, 0.1, 0.001, 0.001, 3);
  sparkle_add_scale (table, 1, "Flare Intensity", &svals.flare_inten,
		     0.0, 1.0, 0.01, 0.01, 2);
  sparkle_add_scale (table, 2, "Spike Length", &svals.spike_len,
		     1.0, 100.0, 1.0, 1.0, 1);
  sparkle_add_scale (table, 3, "Spike Points", &svals.spike_pts,
		     0.0, 16.0, 1.0, 1.0, 0);
  sparkle_add_scale (table, 4, "Spike Angle", &svals.spike_angle,
		     0.0, 360.0, 5.0, 5.0, 1);

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return sint.run;
}

static void
sparkle_add_scale (GtkWidget   *table,
		   gint         row,
		   const gchar *text,
		   gdouble     *value,
		   gdouble      lower,
		   gdouble      upper,
		   gdouble      step,
		   gdouble      page,
		   gint         digits)
{
  GtkWidget     *label;
  GtkWidget     *scale;
  GtkAdjustment *scale_data;

  label = gtk_label_new (text);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, row, row + 1, GIMP_FILL, 0, 5, 0);

  scale_data = gtk_adjustment_new (*value, lower, upper, step, page, 0.0);
  scale = gimp_hscale_new (scale_data, digits);
  gtk_widget_set_size_request (scale, SCALE_WIDTH, -1);
  gimp_table_attach (table, scale, 1, 2, row, row + 1, GIMP_FILL, 0, 0, 0);
  g_signal_connect (scale_data, "value-changed",
		    G_CALLBACK (sparkle_scale_update), value);
}

static gint
compute_luminosity (guchar *pixel,
		    gint    gray,
		    gint    has_alpha)
{
  if (gray)
    {
      if (has_alpha)
	return (pixel[0] * pixel[1]) / 255;
      else
	return pixel[0];
    }
  else
    {
      gint min, max;

      min = MIN (pixel[0], pixel[1]);
      min = MIN (min, pixel[2]);
      max = MAX (pixel[0], pixel[1]);
      max = MAX (max, pixel[2]);

      if (has_alpha)
	return ((min + max) * pixel[3]) / 510;
      else
	return (min + max) / 2;
    }
}

static gint
compute_lum_threshold (GDrawable *drawable,
		       gdouble    percentile)
{
  GPixelRgn src_rgn;
  gpointer pr;
  guchar *data;
  gint values[256];
  gint total, sum;
  gint size;
  gint gray;
  gint has_alpha;
  gint i;
  gint x1, y1, x2, y2;

  /*  zero out the luminosity values array  */
  for (i = 0; i < 256; i++)
    values[i] = 0;

  gimp_drawable_mask_bounds (drawable->id, &x1, &y1, &x2, &y2);
  gray = gimp_drawable_gray (drawable->id);
  has_alpha = gimp_drawable_has_alpha (drawable->id);

  gimp_pixel_rgn_init (&src_rgn, drawable, x1, y1, (x2 - x1), (y2 - y1), FALSE, FALSE);

  for (pr = gimp_pixel_rgns_register (1, &src_rgn); pr != NULL; pr = gimp_pixel_rgns_process (pr))
    {
      data = src_rgn.data;
      size = src_rgn.w * src_rgn.h;

      while (size--)
	{
	  values [compute_luminosity (data, gray, has_alpha)]++;
	  data += src_rgn.bpp;
	}
    }

  total = (x2 - x1) * (y2 - y1);
  sum = 0;

  for (i = 255; i >= 0; i--)
    {
      sum += values[i];
      if ((gdouble) sum / (gdouble) total > percentile)
	{
	  num_sparkles = sum;
	  return i;
	}
    }

  return 0;
}

static void
sparkle (GDrawable *drawable,
	 gint       threshold)
{
  GPixelRgn src_rgn, dest_rgn;
  guchar *src, *dest;
  gdouble nfrac, length, inten;
  gint cur_progress, max_progress;
  gint x1, y1, x2, y2;
  gint size, lum, x, y, b;
  gint gray;
  gint has_alpha, alpha;
  gpointer pr;
  guchar *tmp1;

  gimp_drawable_mask_bounds (drawable->id, &x1, &y1, &x2, &y2);
  gray = gimp_drawable_gray (drawable->id);
  has_alpha = gimp_drawable_has_alpha (drawable->id);
  alpha = (has_alpha) ? drawable->bpp - 1 : drawable->bpp;

  /* initialize the progress dialog */
  cur_progress = 0;
  max_progress = num_sparkles;

  gimp_pixel_rgn_init (&src_rgn, drawable, x1, y1, (x2 - x1), (y2 - y1), FALSE, FALSE);
  gimp_pixel_rgn_init (&dest_rgn, drawable, x1, y1, (x2 - x1), (y2 - y1), TRUE, TRUE);

  for (pr = gimp_pixel_rgns_register (2, &src_rgn, &dest_rgn); pr != NULL; pr = gimp_pixel_rgns_process (pr))
    {
      src = src_rgn.data;
      dest = dest_rgn.data;

      size = src_rgn.w * src_rgn.h;

      while (size --)
	{
	  if(has_alpha && src[alpha] == 0)
	     {
	       memset(dest, 0, alpha * sizeof(guchar));
	       dest += alpha;
	     }
	  else
	    {
	      for (b = 0, tmp1 = src; b < alpha; b++)
		{
		  *dest++ = *tmp1++;
		}
	    }
	  if (has_alpha)
	    *dest++ = src[alpha];

	  src += src_rgn.bpp;
	}
    }

  /* add effects to new image based on intensity of old pixels */
  gimp_pixel_rgn_init (&src_rgn, drawable, x1, y1, (x2 - x1), (y2 - y1), FALSE, FALSE);
  gimp_pixel_rgn_init (&dest_rgn, drawable, x1, y1, (x2 - x1), (y2 - y1), TRUE, TRUE);

  for (pr = gimp_pixel_rgns_register (1, &src_rgn); pr != NULL; pr = gimp_pixel_rgns_process (pr))
    {
      src = src_rgn.data;

      for (y = 0; y < src_rgn.h; y++)
	for (x = 0; x < src_rgn.w; x++)
	  {
	    lum = compute_luminosity (src, gray, has_alpha);
	    if (lum >= threshold)
	      {
		nfrac = fabs ((gdouble) (lum + 1 - threshold) / (gdouble) (256 - threshold));

		length = svals.spike_len * pow (nfrac, 0.8);
		inten = svals.flare_inten * pow (nfrac, 1.0);

		/* fspike im x,y intens rlength angle */
		if (svals.spike_pts > 0)
		  {
		    /* major spikes */
		    fspike (&dest_rgn, gray, x1, y1, x2, y2,
			    x + src_rgn.x, y + src_rgn.y,
			    inten, length, svals.spike_angle);

		    /* minor spikes */
		    fspike (&dest_rgn, gray, x1, y1, x2, y2,
			    x + src_rgn.x, y + src_rgn.y,
			    inten * 0.7, length * 0.7,
			    (svals.spike_angle + 180.0 / svals.spike_pts));
		  }

		cur_progress ++;
		if ((cur_progress % 5) == 0)
		  gimp_progress_update ((double) cur_progress / (double) max_progress);
	      }

	    src += src_rgn.bpp;
	  }
    }

  gimp_progress_update (1.0);

  /*  update the blurred region  */
  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->id, TRUE);
  gimp_drawable_update (drawable->id, x1, y1, (x2 - x1), (y2 - y1));
}

static void
fspike (GPixelRgn *dest_rgn,
	gint       gray,
	gint       x1,
	gint       y1,
	gint       x2,
	gint       y2,
	gdouble    xr,
	gdouble    yr,
	gdouble    inten,
	gdouble    length,
	gdouble    angle)
{
  gdouble xrt, yrt, dx, dy;
  gdouble rpos;
  gdouble in[MAX_CHANNELS];
  gdouble val[MAX_CHANNELS];
  gdouble ho = 1.0, so = 1.0, vo = 1.0;
  gdouble theta, efac;
  gdouble sfac;
  gdouble *gd_tmp1, *gd_tmp2;
  guchar *guc_tmp;
  GTile *tile = NULL;
  gint row, col;
  gint i;
  gint bytes;
  gint x, y;
  gint ok;
  gint b;
  guchar pixel[MAX_CHANNELS];

  theta = angle;
  efac = 2.0;    /* exponent on intensity falloff with radius */
  bytes = dest_rgn->bpp;
  row = -1;
  col = -1;

  /* draw the major spikes */
  for (i = 0; i < svals.spike_pts; i++)
    {
      x = (int) (xr + 0.5);
      y = (int) (yr + 0.5);

      gimp_pixel_rgn_get_pixel (dest_rgn, pixel, x, y);

      for (b = 0, gd_tmp1 = val, guc_tmp = pixel; b < bytes; b++)
	*gd_tmp1++ = (gdouble) (gint)(*guc_tmp++) / 255.0;

      /*  increase saturation to full for color image  */
      if (! gray)
	{
	  rgb_to_hsl (val[0], val[1], val[2], &ho, &so, &vo);
	  hsl_to_rgb (ho, 1.0, vo, &val[0], &val[1], &val[2]);
	}

      dx = 0.2 * cos (theta * M_PI / 180.0);
      dy = 0.2 * sin (theta * M_PI / 180.0);
      xrt = xr;
      yrt = yr;
      rpos = 0.2;

      do
	{
	  sfac = exp (-pow (rpos / length, efac));
	  sfac = sfac * inten;

	  ok = FALSE;
	  for (b = 0, gd_tmp1 = in, gd_tmp2 = val; b < bytes;
	       b++, gd_tmp1++, gd_tmp2++)
	    {
	      *gd_tmp1 = 0.2 * (*gd_tmp2) * sfac;
	      if (*gd_tmp1 > 0.01)
		ok = TRUE;
	    }

	  tile = rpnt (dest_rgn->drawable, tile, x1, y1, x2, y2, xrt, yrt, &row, &col, bytes, in);
	  tile = rpnt (dest_rgn->drawable, tile, x1, y1, x2, y2, xrt + 1, yrt, &row, &col, bytes, in);
	  tile = rpnt (dest_rgn->drawable, tile, x1, y1, x2, y2, xrt + 1, yrt + 1, &row, &col, bytes, in);
	  tile = rpnt (dest_rgn->drawable, tile, x1, y1, x2, y2, xrt, yrt + 1, &row, &col, bytes, in);

	  xrt += dx;
	  yrt += dy;
	  rpos += 0.2;
	} while (ok);

      theta += 360.0 / svals.spike_pts;
    }

  if (tile)
    gimp_tile_unref (tile, TRUE);
}

static GTile *
rpnt (GDrawable *drawable,
      GTile     *tile,
      gint       x1,
      gint       y1,
      gint       x2,
      gint       y2,
      gdouble    xr,
      gdouble    yr,
      gint      *row,
      gint      *col,
      gint       bytes,
      gdouble   *inten)
{
  gint x, y, b;
  gdouble dx, dy, rs, fac;
  gdouble val;
  guchar *pixel;
  guchar *guc_tmp;
  gdouble *gd_tmp;

  x = (int) (xr);	/* integer coord. to upper left of real point */
  y = (int) (yr);

  if (x >= x1 && y >= y1 && x < x2 && y < y2)
    {
      if ((x >> 6 != *col) || (y >> 6 != *row))
	{
	  *col = x >> 6;
	  *row = y >> 6;
	  if (tile)
	    gimp_tile_unref (tile, TRUE);
	  tile = gimp_drawable_get_tile (drawable, TRUE, *row, *col);
	  gimp_tile_ref (tile);
	}

      pixel = tile->data + tile->bpp * (tile->ewidth * (y % 64) + (x % 64));
      dx = xr - x; dy = yr - y;
      rs = dx * dx + dy * dy;
      fac = exp (-rs / PSV);

      for (b = 0, guc_tmp = pixel, gd_tmp = inten; b < bytes;
	   b++, guc_tmp++, gd_tmp++)
	{
	  val = (*gd_tmp) * fac;
	  val += (gdouble) *guc_tmp / 255.0;
	  if (val > 1.0) val = 1.0;
	  *guc_tmp = (guchar) (val * 255.0);
	}
    }

  return tile;
}


/*
 * RGB-HSL transforms.
 * Ken Fishkin, Pixar Inc., January 1989.
 */

/*
 * given r,g,b on [0 ... 1],
 * return (h,s,l) on [0 ... 1]
 */

static void
rgb_to_hsl (gdouble  r,
	    gdouble  g,
	    gdouble  b,
	    gdouble *h,
	    gdouble *s,
	    gdouble *l)
{
  gdouble v;
  gdouble m;
  gdouble vm;
  gdouble r2, g2, b2;

  v = MAX(r,g);
  v = MAX(v,b);
  m = MIN(r,g);
  m = MIN(m,b);

  if ((*l = (m + v) / 2.0) <= 0.0)
    return;
  if ((*s = vm = v - m) > 0.0)
    {
      *s /= (*l <= 0.5) ? (v + m ) :
	(2.0 - v - m) ;
    }
  else
    return;

  r2 = (v - r) / vm;
  g2 = (v - g) / vm;
  b2 = (v - b) / vm;

  if (r == v)
    *h = (g == m ? 5.0 + b2 : 1.0 - g2);
  else if (g == v)
    *h = (b == m ? 1.0 + r2 : 3.0 - b2);
  else
    *h = (r == m ? 3.0 + g2 : 5.0 - r2);

  *h /= 6;
}

/*
 * given h,s,l on [0..1],
 * return r,g,b on [0..1]
 */

static void
hsl_to_rgb (gdouble  h,
	    gdouble  sl,
	    gdouble  l,
	    gdouble *r,
	    gdouble *g,
	    gdouble *b)
{
  gdouble v;

  v = (l <= 0.5) ? (l * (1.0 + sl)) : (l + sl - l * sl);
  if (v <= 0)
    {
      *r = *g = *b = 0.0;
    }
  else
    {
      gdouble m;
      gdouble sv;
      gint sextant;
      gdouble fract, vsf, mid1, mid2;

      m = l + l - v;
      sv = (v - m ) / v;
      h *= 6.0;
      sextant = h;
      fract = h - sextant;
      vsf = v * sv * fract;
      mid1 = m + vsf;
      mid2 = v - vsf;
      switch (sextant)
	{
	case 0: *r = v; *g = mid1; *b = m; break;
	case 1: *r = mid2; *g = v; *b = m; break;
	case 2: *r = m; *g = v; *b = mid1; break;
	case 3: *r = m; *g = mid2; *b = v; break;
	case 4: *r = mid1; *g = m; *b = v; break;
	case 5: *r = v; *g = m; *b = mid2; break;
	}
    }
}


/*  Sparkle interface functions  */

static void
sparkle_close_callback (GtkWidget *widget,
			gpointer   data)
{
  gimp_main_loop_quit ();
}

static void
sparkle_ok_callback (GtkWidget *widget,
		     gpointer   data)
{
  sint.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void
sparkle_scale_update (GtkAdjustment *adjustment,
		     double        *scale_val)
{
  *scale_val = gtk_adjustment_get_value (adjustment);
}
