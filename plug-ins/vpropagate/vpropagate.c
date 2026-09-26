/* vpropagate.c -- This is a plug-in for the GIMP (1.0's API)
 * Author: Shuji Narazaki <narazaki@InetQ.or.jp>
 * Time-stamp: <1998/04/11 19:46:08 narazaki@InetQ.or.jp>
 * Version: 0.89a
 *
 * Copyright (C) 1996-1997 Shuji Narazaki <narazaki@InetQ.or.jp>
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

/* memo
   the initial value of each pixel is the value of the pixel itself.
   To determine whether it is an isolated local peak point, use:
   (self == min && (! modified_flag))   ; modified_flag holds history of update
   In other word, pixel itself is not a neighbor of it.
*/

#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define	PLUG_IN_NAME	"plug_in_vpropagate"
#define SHORT_NAME	"vpropagate"
#define PROGRESS_NAME	"value propagating..."
#define MENU_POSITION	"<Image>/Filters/Distorts/Value Propagate"

typedef guchar CH;
#define	RGB	(1 << 0)
#define GRAY	(1 << 1)
#define WITH_ALPHA	(1 << 2)
#define WO_ALPHA	(1 << 3)
#define num_direction	4
#define Right2Left	0
#define Bottom2Top	1
#define	Left2Right	2
#define Top2Bottom	3

static void	query	(void);
static void	run	(char	*name,
			 int	nparams,
			 GParam	*param,
			 int	*nreturn_vals,
			 GParam **return_vals);

static GStatusType	value_propagate (gint drawable_id);
static void	value_propagate_body (gint drawable_id);
static int	vpropagate_dialog (GImageType image_type);
static void	prepare_row (GPixelRgn *pixel_rgn,
			     guchar    *data,
			     int        x,
			     int        y,
			     int        w);
static void
gtkW_gint_update (GtkWidget *widget,
		      gpointer   data);
static void
gtkW_scale_update (GtkAdjustment *adjustment,
		   double   *data);
static void
gtkW_close_callback (GtkWidget *widget,
		     gpointer   data);
static void
vpropagate_ok_callback (GtkWidget *widget,
			gpointer   data);
static void
gtkW_toggle_update (GtkWidget *widget,
		    gpointer   data);
static GtkWidget *
gtkW_dialog_new (char *name,
		 GCallback ok_callback,
		 GCallback close_callback);
static GtkWidget *
gtkW_table_add_toggle (GtkWidget	*table,
		       gchar	*name,
		       gint	x1,
		       gint	x2,
		       gint	y,
		       GCallback update,
		       gint	*value);
static GtkWidget *
gtkW_vbox_add_radio_button (GtkWidget *vbox,
			    gchar	*name,
			    GtkWidget	*group,
			    GCallback	update,
			    gint	*value);
static void
gtkW_table_add_gint (GtkWidget	*table,
		     gchar	*name,
		     gint	x,
		     gint	y, 
		     GCallback	update,
		     gint	*value,
		     gchar	*buffer);
static void
gtkW_table_add_scale (GtkWidget	*table,
		      gchar	*name,
		      gint	x1,
		      gint	y,
		      GCallback update,
		      gdouble *value,
		      gdouble min,
		      gdouble max,
		      gdouble step);
GtkWidget *gtkW_frame_new (gchar *name, GtkWidget *parent);
GtkWidget *gtkW_hbox_new (GtkWidget *parent);
GtkWidget *gtkW_vbox_new (GtkWidget *parent);

static int	value_difference_check(CH *, CH *, int);
static void	set_value (GImageType, int, CH *, CH *, CH *, void *);
static void	initialize_white (GImageType, int, CH *, CH *, void **);
static void	propagate_white (GImageType, int, CH *, CH *, CH *, void *);
static void	initialize_black (GImageType, int, CH *, CH *, void **);
static void	propagate_black (GImageType, int, CH *, CH *, CH *, void *);
static void	initialize_middle (GImageType, int, CH *, CH *, void **);
static void	propagate_middle (GImageType, int, CH *, CH *, CH *, void *);
static void	set_middle_to_peak (GImageType, int, CH *, CH *, CH *, void *);
static void	set_foreground_to_peak (GImageType, int, CH *, CH *, CH *, void *);
static void	initialize_foreground (GImageType, int, CH *, CH *, void **);
static void	initialize_background (GImageType, int, CH *, CH *, void **);
static void	propagate_a_color (GImageType, int, CH *, CH *, CH *, void *);
static void	propagate_opaque (GImageType, int, CH *, CH *, CH *, void *);
static void	propagate_transparent (GImageType, int, CH *, CH *, CH *, void *);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,				/* init_proc  */
  NULL,				/* quit_proc */
  query,			/* query_proc */
  run,				/* run_proc */
};

#define ENTRY_WIDTH	100
#define UPDATE_STEP	20
#define SCALE_WIDTH	100
#define PROPAGATING_VALUE	1<<0
#define PROPAGATING_ALPHA	1<<1

/* parameters */
typedef struct
{
  gint		propagate_mode;
  gint		propagating_channel;
  gdouble	propagating_rate;
  gint		direction_mask;
  gint		lower_limit;
  gint		upper_limit;
  /* gint	channel_mask[3]; */
  /* gint	peak_includes_equals; */
  /* gint	overwrite; */
} VPValueType;

static VPValueType vpvals = 
{
  0,
  3,				/* PROPAGATING_VALUE + PROPAGATING_ALPHA */
  1.0,				/* [0.0:1.0] */
  15,				/* propagate to all 4 directions */
  0,
  255
};

/* dialog vairables */
gint	propagate_alpha;
gint	propagate_value;
gint	direction_mask_vec[4];
gint	channel_mask [] = {1, 1, 1};
gint	peak_max = 1;
gint	peak_min = 1;
gint	peak_includes_equals = 1;
guchar	fore[3];

typedef struct
{
  int applicable_image_type;
  char *name;
  void (*initializer)();
  void (*updater)();
  void (*finalizer)();
  gint	selected;
} ModeParam;

#define num_mode 8
static ModeParam modes[num_mode] = {
  { RGB | GRAY | WITH_ALPHA | WO_ALPHA, "more white (larger value)",
    initialize_white,		propagate_white,	set_value,	FALSE},
  { RGB | GRAY | WITH_ALPHA | WO_ALPHA,	"more black (smaller value)",
    initialize_black,		propagate_black, 	set_value,	FALSE},
  { RGB | GRAY | WITH_ALPHA | WO_ALPHA,	"middle value to peaks",
    initialize_middle,		propagate_middle, 	set_middle_to_peak,
    FALSE},
  { RGB | GRAY | WITH_ALPHA | WO_ALPHA,	"foreground to peaks",
    initialize_middle,		propagate_middle, 	set_foreground_to_peak,
    FALSE},
  { RGB | WITH_ALPHA | WO_ALPHA,	"only foreground",
    initialize_foreground,	propagate_a_color, 	set_value,	FALSE},
  { RGB | WITH_ALPHA | WO_ALPHA,	"only background",
    initialize_background,	propagate_a_color, 	set_value,	FALSE},
  { RGB | GRAY | WITH_ALPHA,		"more opaque",
    NULL,	 		propagate_opaque, 	set_value,	FALSE},
  { RGB | GRAY | WITH_ALPHA,		"more transparent",
    NULL, 			propagate_transparent,	set_value,	FALSE}
};

typedef struct 
{
  gint run;
} VPInterface;

static VPInterface vpropagate_interface = { FALSE };
gint	drawable_id;

MAIN ()

static void
query ()
{
  static GParamDef args [] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive"},
    { PARAM_IMAGE, "image", "Input image (not used)"},
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
    { PARAM_INT32, "propagate-mode", "propagate 0:white, 1:black, 2:middle value 3:foreground to peak, 4:foreground, 5:background, 6:opaque, 7:transparent"},
    { PARAM_INT32, "propagating-channel", "channels which values are propagated"},
    { PARAM_FLOAT, "propagating-rate", "0.0 <= propagatating_rate <= 1.0"},
    { PARAM_INT32, "direction-mask", "0 <= direction-mask <= 15"},
    { PARAM_INT32, "lower-limit", "0 <= lower-limit <= 255"},
    { PARAM_INT32, "upper-limit", "0 <= upper-limit <= 255"},
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0;
  
  gimp_install_procedure (PLUG_IN_NAME,
			  "Propagate values of the layer",
			  "Propagate values of the layer",
			  "Shuji Narazaki (narazaki@InetQ.or.jp)",
			  "Shuji Narazaki",
			  "1996-1997",
			  MENU_POSITION,
			  "RGB*,GRAY*",
			  PROC_PLUG_IN,
			  nargs, nreturn_vals,
			  args, return_vals);
}

static void
run (char	*name,
     int	nparams,
     GParam	*param,
     int	*nreturn_vals,
     GParam	**return_vals)
{
  static GParam	 values[1];
  GStatusType	status = STATUS_SUCCESS;
  GRunModeType	run_mode;
  
  run_mode = param[0].data.d_int32;
  drawable_id = param[2].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;
  
  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;

  switch ( run_mode )
    {
    case RUN_INTERACTIVE:
      gimp_get_data (PLUG_IN_NAME, &vpvals);
      /* building the values of dialog variables from vpvals. */
      modes[vpvals.propagate_mode].selected = TRUE;
      propagate_alpha = (vpvals.propagating_channel & PROPAGATING_ALPHA) ?
	TRUE : FALSE;
      propagate_value = (vpvals.propagating_channel & PROPAGATING_VALUE) ?
	TRUE : FALSE;
      {
	int	i;
	for (i = 0; i < 4; i++)
	  direction_mask_vec[i] = (vpvals.direction_mask & (1 << i))
	    ? TRUE : FALSE;
      }
      if (! vpropagate_dialog (gimp_drawable_type(drawable_id)))
	return;
      break;
    case RUN_NONINTERACTIVE:
      vpvals.propagate_mode = param[3].data.d_int32;
      vpvals.propagating_channel = param[4].data.d_int32;
      vpvals.propagating_rate = param[5].data.d_float;
      vpvals.direction_mask = param[6].data.d_int32;
      vpvals.lower_limit = param[7].data.d_int32;
      vpvals.upper_limit = param[8].data.d_int32;
      break;
    case RUN_WITH_LAST_VALS:
      gimp_get_data (PLUG_IN_NAME, &vpvals);
      break;
    }
  
  status = value_propagate (drawable_id);

  if (run_mode != RUN_NONINTERACTIVE)
    gimp_displays_flush();
  if (run_mode == RUN_INTERACTIVE && status == STATUS_SUCCESS )
    gimp_set_data (PLUG_IN_NAME, &vpvals, sizeof (VPValueType));

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
}

/* registered function entry */
static GStatusType
value_propagate (gint	drawable_id)
{
  /* check the validness of parameters */
  if (!(vpvals.propagating_channel & (PROPAGATING_VALUE | PROPAGATING_ALPHA)))
    {
      /* gimp_message ("No channel selected."); */
      return STATUS_EXECUTION_ERROR;
    }
  if (vpvals.direction_mask == 0)
    {
      /* gimp_message ("No direction selected."); */
      return STATUS_EXECUTION_ERROR;
    }
  if ( (vpvals.lower_limit < 0) || (255 < vpvals.lower_limit) ||
       (vpvals.upper_limit < 0) || (255 < vpvals.lower_limit) ||
       (vpvals.upper_limit < vpvals.lower_limit) )
    {
      /* gimp_message ("Limit values are not valid."); */
      return STATUS_EXECUTION_ERROR;
    }
  value_propagate_body (drawable_id);
  return STATUS_SUCCESS;
}

static void
value_propagate_body (gint	drawable_id)
{
  GDrawable	*drawable;
  GImageType	dtype;
  ModeParam	operation;
  GPixelRgn	srcRgn, destRgn;
  guchar	*here, *best, *dest;
  guchar	*dest_row, *prev_row, *cur_row, *next_row, *pr, *cr, *nr, *swap;
  int	width, height, bytes, index;
  int	begx, begy, endx, endy, x, y, dx;
  int	left_index, right_index, up_index, down_index;
  void	*tmp;

  /* calculate neighbors' indexes */
  left_index	= (vpvals.direction_mask & (1 << Left2Right)) ? -1 : 0;
  right_index	= (vpvals.direction_mask & (1 << Right2Left)) ?  1 : 0;
  up_index	= (vpvals.direction_mask & (1 << Top2Bottom)) ? -1 : 0;
  down_index	= (vpvals.direction_mask & (1 << Bottom2Top)) ?  1 : 0;
  operation = modes[vpvals.propagate_mode];
  tmp = NULL;

  drawable = gimp_drawable_get (drawable_id);
  dtype = gimp_drawable_type (drawable_id);

  /* Here I use the algorithm of blur.c . */
  gimp_drawable_mask_bounds (drawable->id, &begx, &begy, &endx, &endy);

  width = drawable->width;
  height = drawable->height;
  bytes = drawable->bpp;

  prev_row = (guchar *) malloc ((endx - begx + 2) * bytes);
  cur_row = (guchar *) malloc ((endx - begx + 2) * bytes);
  next_row = (guchar *) malloc ((endx - begx + 2) * bytes);
  dest_row = (guchar *) malloc ((endx - begx) * bytes);

  gimp_pixel_rgn_init (&srcRgn, drawable, 0, 0, width, height, FALSE, FALSE);
  gimp_pixel_rgn_init (&destRgn, drawable, 0, 0, width, height, TRUE, TRUE);

  pr = prev_row + bytes;
  cr = cur_row + bytes;
  nr = next_row + bytes;

  prepare_row (&srcRgn, pr, begx, (0 < begy) ? begy : begy - 1, endx-begx);
  prepare_row (&srcRgn, cr, begx, begy, endx-begx);

  best = (guchar *) malloc (bytes);

  gimp_progress_init (PROGRESS_NAME);
  gimp_palette_get_foreground (fore+0, fore+1, fore+2);

  /* start real job */
  for ( y = begy ; y < endy ; y++) 
    {
      prepare_row (&srcRgn, nr, begx, ((y+1) < endy) ? y+1 : endy, endx-begx);
      for (index = 0; index < (endx - begx) * bytes; index++)
	dest_row[index] = cr[index];
      for ( x = 0 ; x < endx - begx; x++ ) 
	{
	  dest = dest_row + (x * bytes);
	  here = cr + (x * bytes);
	  /* *** copy source value to best value holder *** */
	  memcpy ((void *)best, (void *)here, bytes);
	  if ( operation.initializer )
	    (* operation.initializer)(dtype, bytes, best, here, &tmp);
	  /* *** gather neighbors' values: loop-unfolded version *** */
	  if (up_index == -1)
	    for (dx = left_index ; dx <= right_index ; dx++)
	      (* operation.updater)(dtype, bytes, here, pr+((x+dx)*bytes), best, tmp);
	  for (dx = left_index ; dx <= right_index ; dx++)
	    if ( dx != 0 )
	      (* operation.updater)(dtype, bytes, here, cr+((x+dx)*bytes), best, tmp);
	  if (down_index == 1)
	    for (dx = left_index ; dx <= right_index ; dx++)
	      (* operation.updater)(dtype, bytes, here, nr+((x+dx)*bytes), best, tmp);
	  /* *** store it to dest_row*** */
	  (* operation.finalizer)(dtype, bytes, best, here, dest, tmp);
	}
      /* now store destline to destRgn */
      gimp_pixel_rgn_set_row (&destRgn, dest_row, begx, y, endx - begx);
      /* shift the row pointers  */
      swap = pr;
      pr = cr;
      cr = nr;
      nr = swap;
      /* update each UPDATE_STEP (optimizer must generate cheap code) */ 
      if ((y % 5) == 0) /*(y % (int) ((endy - begy) / UPDATE_STEP)) == 0 */
	gimp_progress_update ((double)y / (double)(endy - begy));
    }
  /*  update the region  */
  gimp_progress_update(1.0);
  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->id, TRUE);
  gimp_drawable_update (drawable->id, begx, begy, endx-begx, endy-begy);
}

static void
prepare_row (GPixelRgn *pixel_rgn,
	     guchar    *data,
	     int        x,
	     int        y,
	     int        w)
{
  int b;

  if (y == 0)
    gimp_pixel_rgn_get_row (pixel_rgn, data, x, (y + 1), w);
  else if (y == pixel_rgn->h)
    gimp_pixel_rgn_get_row (pixel_rgn, data, x, (y - 1), w);
  else
    gimp_pixel_rgn_get_row (pixel_rgn, data, x, y, w);

  /*  Fill in edge pixels  */
  for (b = 0; b < pixel_rgn->bpp; b++)
    {
      data[-(gint)pixel_rgn->bpp + b] = data[b];
      data[w * pixel_rgn->bpp + b] = data[(w - 1) * pixel_rgn->bpp + b];
    }
}

static void
set_value (dtype, bytes, best, here, dest, tmp)
     GImageType	dtype;
     int	bytes;
     guchar	*best, *here, *dest;
     void	*tmp;
{
  int	value_chs = 0;
  int	alpha = 0;
  int	ch;

  switch ( dtype )
    {
    case RGB_IMAGE:
      value_chs = 3;
      break;
    case RGBA_IMAGE:
      value_chs = 3;
      alpha = 3;
      break;
    case GRAY_IMAGE:
      value_chs = 1;
      break;
    case GRAYA_IMAGE:
      value_chs = 1;
      alpha = 1;
      break;
    default:
      break;
    }
  for (ch = 0; ch < value_chs; ch++)
    {
      if ( vpvals.propagating_channel & PROPAGATING_VALUE ) /* value channel */
	*dest++ = (CH)(vpvals.propagating_rate * best[ch]
		       + (1.0 - vpvals.propagating_rate) * here[ch]);
      else
	*dest++ = here[ch];
    }
  if ( alpha )
    {
      if ( vpvals.propagating_channel & PROPAGATING_ALPHA ) /* alpha channel */
	*dest++ = (CH)(vpvals.propagating_rate * best[alpha]
		       + (1.0 - vpvals.propagating_rate) * here[alpha]);
      else
	*dest++ = here[alpha];
    }
}

static int
value_difference_check(pos1, pos2, ch)
     CH		*pos1, *pos2;
     int	ch;
{
  int	index;
  int	diff;

  for (index = 0 ; index < ch; index++ )
    if ( channel_mask[index] != 0 )
      {
	diff = abs(pos1[index] - pos2[index]);
	if ( ! ((vpvals.lower_limit <= diff) && (diff <= vpvals.upper_limit)) )
	  return 0;
      }
  return 1;
}

/* mothods for each mode */
static void
initialize_white (dtype, bytes, best, here, tmp)
     GImageType dtype;
     int bytes;
     CH *best, *here;
     void **tmp;
{
  if (*tmp == NULL)
    *tmp = (void *)malloc(sizeof(float));
  *(float *)*tmp = sqrt (channel_mask[0] * here[0] * here[0] 
			 + channel_mask[1] * here[1] * here[1] 
			 + channel_mask[2] * here[2] * here[2]);
}

static void
propagate_white (dtype, bytes, orig, here, best, tmp)
     GImageType dtype;
     int bytes;
     CH *orig, *here, *best;
     void *tmp;
{
  float	v_here;

  switch ( dtype )
    {
    case RGB_IMAGE:
    case RGBA_IMAGE:
      v_here = sqrt (channel_mask[0] * here[0] * here[0] 
		     + channel_mask[1] * here[1] * here[1] 
		     + channel_mask[2] * here[2] * here[2]);
     if ( *(float *)tmp < v_here && value_difference_check(orig, here, 3) )
	{
	  *(float *)tmp = v_here;
	  memcpy(best, here, 3 * sizeof(CH)); /* alpha channel holds old value */
	}
      break;
    case GRAYA_IMAGE:
    case GRAY_IMAGE:
      if ( *best < *here && value_difference_check(orig, here, 1))
	*best = *here;
      break;
    default:
      break;
    }
}

static void
initialize_black (image_type, channels, best, here, tmp)
     GImageType image_type;
     int channels;
     CH *best, *here;
     void **tmp;
{
  if (*tmp == NULL)
    *tmp = (void *)malloc(sizeof(float));
  *(float *)*tmp = sqrt (channel_mask[0] * here[0] * here[0] 
			 + channel_mask[1] * here[1] * here[1] 
			 + channel_mask[2] * here[2] * here[2]);
}

static void
propagate_black (image_type, channels, orig, here, best, tmp)
     GImageType	image_type;
     int	channels;
     CH		*orig, *here, *best;
     void	*tmp;
{
  float	v_here;

  switch ( image_type )
    {
    case RGB_IMAGE:
    case RGBA_IMAGE:
      v_here = sqrt (channel_mask[0] * here[0] * here[0] 
		     + channel_mask[1] * here[1] * here[1] 
		     + channel_mask[2] * here[2] * here[2]);
      if ( v_here < *(float *)tmp && value_difference_check(orig, here, 3) )
	{	
	  *(float *)tmp = v_here;
	  memcpy (best, here, 3 * sizeof(CH)); /* alpha channel holds old value */
	}
      break;
    case GRAYA_IMAGE:
    case GRAY_IMAGE:
      if ( *here < *best && value_difference_check(orig, here, 1) )
	*best = *here;
      break;
    default:
      break;
    }
}

typedef struct
{
  short	min_modified;
  short max_modified;
  long	original_value;
  long	minv;
  CH	min[3];
  long	maxv;
  CH	max[3];
} MiddlePacket;

static void
initialize_middle (image_type, channels, best, here, tmp)
     GImageType image_type;
     int channels;
     CH *best, *here;
     void **tmp;
{
  int index;
  MiddlePacket *data;

  if (*tmp == NULL)
    *tmp = (void *)malloc(sizeof(MiddlePacket));
  data = (MiddlePacket *)*tmp;
  for (index = 0; index < channels; index++)
      data->min[index] = data->max[index] = here[index];
  switch ( image_type )
    {
    case RGB_IMAGE:
    case RGBA_IMAGE:
      data->original_value = sqrt (channel_mask[0] * here[0] * here[0]
				   + channel_mask[1] * here[1] * here[1] 
				   + channel_mask[2] * here[2] * here[2]);
      break;
    case GRAYA_IMAGE:
    case GRAY_IMAGE:
      data->original_value = *here;
      break;
    default:
      break;
    }
  data->minv = data->maxv = data->original_value;
  data->min_modified = data->max_modified = 0;
}
  
static void
propagate_middle (image_type, channels, orig, here, best, tmp)
     GImageType	image_type;
     int	channels;
     CH		*orig, *here, *best;
     void	*tmp;
{
  float	v_here;
  MiddlePacket *data;

  data = (MiddlePacket *)tmp;

  switch ( image_type )
    {
    case RGB_IMAGE:
    case RGBA_IMAGE:
      v_here = sqrt (channel_mask[0] * here[0] * here[0] 
		     + channel_mask[1] * here[1] * here[1] 
		     + channel_mask[2] * here[2] * here[2]);
      if ( (v_here <= data->minv) && value_difference_check(orig, here, 3) )
	{	
	  data->minv = v_here;
	  memcpy (data->min, here, 3*sizeof(CH));
	  data->min_modified = 1;
	}
      if ( data->maxv <= v_here && value_difference_check(orig, here, 3) )
	{	
	  data->maxv = v_here;
	  memcpy (data->max, here, 3*sizeof(CH));
	  data->max_modified = 1;
	}
      break;
    case GRAYA_IMAGE:
    case GRAY_IMAGE:
      if ( (*here <= data->min[0]) && value_difference_check(orig, here, 1) )
	{
	  data->min[0] = *here;
	  data->min_modified = 1;
	}
      if ( (data->max[0] <= *here) && value_difference_check(orig, here, 1) )
	{
	  data->max[0] = *here;
	  data->max_modified = 1;
	}
      break;
    default:
      break;
    }
}

static void
set_middle_to_peak (image_type, channels, best, here, dest, tmp)
     GImageType	image_type;
     int	channels;
     CH		*best, *here, *dest;
     void	*tmp;
{
  int	value_chs = 0;
  int	alpha = 0;
  int	ch;
  MiddlePacket	*data;

  data = (MiddlePacket *)tmp;
  if (! ((peak_min & (data->minv == data->original_value))
	 || (peak_max & (data->maxv == data->original_value))) )
    return;
  if ((! peak_includes_equals)
      && ((peak_min & (! data->min_modified))
	  || (peak_max & (! data->max_modified))))
      return;

  switch ( image_type )
    {
    case RGB_IMAGE:
      value_chs = 3;
      break;
    case RGBA_IMAGE:
      value_chs = 3;
      alpha = 3;
      break;
    case GRAY_IMAGE:
      value_chs = 1;
      break;
    case GRAYA_IMAGE:
      value_chs = 1;
      alpha = 1;
      break;
    default:
      break;
    }
  for (ch = 0; ch < value_chs; ch++)
    {
      if ( vpvals.propagating_channel & PROPAGATING_VALUE ) /* value channel */
	*dest++ = (CH)(vpvals.propagating_rate * 0.5 * (data->min[ch] + data->max[ch])
		       + (1.0 - vpvals.propagating_rate) * here[ch]);
      else
	*dest++ = here[ch];
    }
  if ( alpha )
    {
      if ( vpvals.propagating_channel & PROPAGATING_ALPHA ) /* alpha channel */
	*dest++ = (CH)(vpvals.propagating_rate * best[alpha]
		       + (1.0 - vpvals.propagating_rate) * here[alpha]);
      else
	*dest++ = here[alpha];
    }
}

static void
set_foreground_to_peak (image_type, channels, best, here, dest, tmp)
     GImageType	image_type;
     int	channels;
     CH		*best, *here, *dest;
     void	*tmp;
{
  int	value_chs = 0;
  int	alpha = 0;
  int	ch;
  MiddlePacket	*data;

  data = (MiddlePacket *)tmp;
  if (! ((peak_min & (data->minv == data->original_value))
	 || (peak_max & (data->maxv == data->original_value))) )
    return;
  if (peak_includes_equals
      && ((peak_min & (! data->min_modified))
	  || (peak_max & (! data->max_modified))))
      return;

  switch ( image_type )
    {
    case RGB_IMAGE:
      value_chs = 3;
      break;
    case RGBA_IMAGE:
      value_chs = 3;
      alpha = 3;
      break;
    case GRAY_IMAGE:
      value_chs = 1;
      break;
    case GRAYA_IMAGE:
      value_chs = 1;
      alpha = 1;
      break;
    default:
      break;
    }
  for (ch = 0; ch < value_chs; ch++)
    {
      if ( vpvals.propagating_channel & PROPAGATING_VALUE ) /* value channel */
	*dest++ = (CH)(vpvals.propagating_rate*fore[ch]
		       + (1.0 - vpvals.propagating_rate)*here[ch]);
      else
	*dest++ = here[ch];
    }
}

static void
initialize_foreground (image_type, channels, best, here, tmp)
     GImageType image_type;
     int channels;
     CH *best, *here;
     void **tmp;
{
  CH *ch;

  if (*tmp == NULL)
    {
      *tmp = (void *)malloc(3 * sizeof(CH));
      ch = (CH *)*tmp;
      gimp_palette_get_foreground (&ch[0], &ch[1], &ch[2]);
    }
}

static void
initialize_background (image_type, channels, best, here, tmp)
     GImageType image_type;
     int channels;
     CH *best, *here;
     void **tmp;
{
  CH *ch;

  if (*tmp == NULL)
    {
      *tmp = (void *)malloc(3 * sizeof(CH));
      ch = (CH *)*tmp;
      gimp_palette_get_background (&ch[0], &ch[1], &ch[2]);
    }
}

static void
propagate_a_color (image_type, channels, orig, here, best, tmp)
     GImageType	image_type;
     int	channels;
     CH		*orig, *here, *best;
     void	*tmp;
{
  CH *fg = (CH *)tmp;

  switch ( image_type )
    {
    case RGB_IMAGE:
    case RGBA_IMAGE:
      if ( here[0] == fg[0] && here[1] == fg[1] && here[2] == fg[2] &&
	   value_difference_check(orig, here, 3) )
	{	
	  memcpy (best, here, 3 * sizeof(CH)); /* alpha channel holds old value */
	}
      break;
    case GRAYA_IMAGE:
    case GRAY_IMAGE:
      break;
    default:
      break;
    }
}

static void
propagate_opaque (image_type, channels, orig, here, best, tmp)
     GImageType	image_type;
     int	channels;
     CH		*orig, *here, *best;
     void	*tmp;
{
  switch ( image_type )
    {
    case RGBA_IMAGE:
      if ( best[3] < here[3] && value_difference_check(orig, here, 3) )
	memcpy(best, here, channels * sizeof(CH));
      break;
    case GRAYA_IMAGE:
      if ( best[1] < here[1] && value_difference_check(orig, here, 1))
	memcpy(best, here, channels * sizeof(CH));
      break;
    default:
      break;
    }
}

static void
propagate_transparent (image_type, channels, orig, here, best, tmp)
     GImageType	image_type;
     int	channels;
     CH		*orig, *here, *best;
     void	*tmp;
{
  switch ( image_type )
    {
    case RGBA_IMAGE:
      if ( here[3] < best[3] && value_difference_check(orig, here, 3))
	memcpy(best, here, channels * sizeof(CH));
      break;
    case GRAYA_IMAGE:
      if ( here[1] < best[1] && value_difference_check(orig, here, 1))
	memcpy(best, here, channels * sizeof(CH));
      break;
    default:
      break;
    }
}

/* dialog stuff */

static int
vpropagate_dialog (GImageType image_type)
{
  GtkWidget	*dlg;
  GtkWidget	*hbox;
  GtkWidget	*frame;
  GtkWidget	*table;
  GtkWidget *sep;
  static gchar	buffer[3][10];

  gtk_init ();
  
  dlg = gtkW_dialog_new (PLUG_IN_NAME,
			 G_CALLBACK (vpropagate_ok_callback),
			 G_CALLBACK (gtkW_close_callback));
  
  hbox = gtkW_hbox_new((gimp_dialog_get_vbox (dlg)));

  /* Propagate Mode */
  {
    GtkWidget *frame;
    GtkWidget *toggle_vbox;
    GtkWidget *group = NULL;
    gint	index = 0;

    frame = gtkW_frame_new ("Propagate Mode", hbox);
    toggle_vbox = gtkW_vbox_new (frame);
    
    for (index = 0; index < num_mode ; index++ )
      group = gtkW_vbox_add_radio_button (toggle_vbox, modes[index].name, group,
					  G_CALLBACK (gtkW_toggle_update),
					  &modes[index].selected);
  }

  /* Parameter settings */
  frame = gtkW_frame_new ("Parameter Settings", hbox);
  table = gimp_table_new (9,2, FALSE); /* 4 raw, 2 columns(name and value) */
  
  gimp_container_set_border_width (table, 10);
  gimp_container_add (frame, table);

  gtkW_table_add_gint (table, "Lower threshold", 0, 0,
		       G_CALLBACK (gtkW_gint_update),
		       &vpvals.lower_limit, buffer[0]);
  gtkW_table_add_gint (table, "Upper threshold", 0, 1,
		       G_CALLBACK (gtkW_gint_update),
		       &vpvals.upper_limit, buffer[1]);
  gtkW_table_add_scale (table, "Propagating Rate", 0, 2, 
			G_CALLBACK (gtkW_scale_update),
			&vpvals.propagating_rate, 0.0, 1.0, 0.01);

  sep = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
  gimp_table_attach (table, sep, 0, 2, 3, 4, GIMP_FILL, 0, 0, 0);

  gtkW_table_add_toggle (table, "to Left", 0, 1, 4,
			 G_CALLBACK (gtkW_toggle_update),
			 &direction_mask_vec[Right2Left]);
  gtkW_table_add_toggle (table, "to Right", 1, 2, 4,
			 G_CALLBACK (gtkW_toggle_update),
			 &direction_mask_vec[Left2Right]);
  gtkW_table_add_toggle (table, "to Top", 0, 1, 5,
			 G_CALLBACK (gtkW_toggle_update),
			 &direction_mask_vec[Bottom2Top]);
  gtkW_table_add_toggle (table, "to Bottom", 1, 2, 5,
			 G_CALLBACK (gtkW_toggle_update),
			 &direction_mask_vec[Top2Bottom]);
  if ((image_type == RGBA_IMAGE) | (image_type == GRAYA_IMAGE))
    {
      GtkWidget *sep;

      sep = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
      gimp_table_attach (table, sep, 0, 2, 6, 7, GIMP_FILL, 0, 0, 0);

      {
	GtkWidget *toggle;
	
	toggle = gtkW_table_add_toggle (table, "Propagating Alpha Channel",
					0, 2, 7,
					G_CALLBACK (gtkW_toggle_update),
					&propagate_alpha);
	if (gimp_layer_get_preserve_transparency (drawable_id))
	  {
	    gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), 0);
	    gtk_widget_set_sensitive (toggle, FALSE);
	  }
      }
      gtkW_table_add_toggle (table, "Propagating Value Channel", 0, 2, 8,
			     G_CALLBACK (gtkW_toggle_update),
			     &propagate_value);
    }
  gtk_window_present (GTK_WINDOW (dlg));
  
  gimp_main_loop_run ();

  return vpropagate_interface.run;
}

/* VFtext interface functions  */
static void
gtkW_gint_update (GtkWidget *widget,
		      gpointer   data)
{
  *(gint *)data = (gint)atof (gtk_editable_get_text (GTK_EDITABLE (widget)));
}

static void
gtkW_scale_update (GtkAdjustment *adjustment,
		   gdouble	 *scale_val)
{
  *scale_val = (gdouble) gtk_adjustment_get_value (adjustment);
}

static void
gtkW_close_callback (GtkWidget *widget,
		     gpointer   data)
{
  gimp_main_loop_quit ();
}

static void
vpropagate_ok_callback (GtkWidget *widget,
			gpointer   data)
{
  gint index;

  for (index = 0; index < num_mode; index++ )
    if (modes[index].selected)
      {
	vpvals.propagate_mode = index;
	break;
      }
  {
    gint	i, result;
    for (i = result = 0; i < 4; i++)
      result |= (direction_mask_vec[i] ? 1 : 0) << i;
    vpvals.direction_mask = result;
  }
  vpvals.propagating_channel = (propagate_alpha ? PROPAGATING_ALPHA : 0) |
    (propagate_value ? PROPAGATING_VALUE : 0);
  vpropagate_interface.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void
gtkW_toggle_update (GtkWidget *widget,
		    gpointer   data)
{
  int *toggle_val;

  toggle_val = (int *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    *toggle_val = TRUE;
  else
    *toggle_val = FALSE;
}

/* gtkW is the abbreviation of gtk Wrapper */
static GtkWidget *
gtkW_dialog_new (char * name,
		 GCallback ok_callback,
		 GCallback close_callback)
{
  GtkWidget *dlg, *button;
  
  dlg = gimp_dialog_new (name);
  g_signal_connect (dlg, "destroy",
		    close_callback, NULL);

  /* Action Area */
  gimp_dialog_add_button (dlg, "OK", ok_callback, dlg, TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dlg);

  return dlg;
}

GtkWidget *
gtkW_hbox_new (GtkWidget *parent)
{
  GtkWidget	*hbox;
  
  hbox = gimp_hbox_new (FALSE, 5);
  gimp_container_set_border_width (hbox, 5);
  gimp_box_pack_start (parent, hbox, FALSE, TRUE, 0);
  return hbox;
}

GtkWidget *
gtkW_vbox_new (GtkWidget *parent)
{
  GtkWidget *vbox;
  
  vbox = gimp_vbox_new (FALSE, 5);
  gimp_container_set_border_width (vbox, 10);
  /* gimp_box_pack_start (parent, vbox, TRUE, TRUE, 0); */
  gimp_container_add (parent, vbox);
  return vbox;
}

GtkWidget *
gtkW_frame_new (gchar *name,
		GtkWidget *parent)
{
  GtkWidget *frame;
  
  frame = gtk_frame_new (name);
  gimp_container_set_border_width (frame, 5);
  gimp_box_pack_start (parent, frame, FALSE, FALSE, 0);
  return frame;
}

static GtkWidget *
gtkW_table_add_toggle (GtkWidget	*table,
		       gchar	*name,
		       gint	x1,
		       gint	x2,
		       gint	y,
		       GCallback update,
		       gint	*value)
{
  GtkWidget *toggle;
  
  toggle = gtk_check_button_new_with_label(name);
  gimp_table_attach (table, toggle, x1, x2, y, y+1,
		    GIMP_FILL|GIMP_EXPAND, 0, 0, 0);
  g_signal_connect (toggle, "toggled",
		      update,
		      value);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), *value);
  return toggle;
}

static GtkWidget *
gtkW_vbox_add_radio_button (GtkWidget *vbox,
			    gchar	*name,
			    GtkWidget	*group,
			    GCallback	update,
			    gint	*value)
{
  GtkWidget *toggle;
  
  toggle = gimp_radio_button_new (group, name);
  group = toggle;
  gimp_box_pack_start (vbox, toggle, FALSE, FALSE, 0);
  g_signal_connect (toggle, "toggled",
		      update, value);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), *value);
  return group;
}

static void
gtkW_table_add_gint (GtkWidget	*table,
		     gchar	*name,
		     gint	x,
		     gint	y, 
		     GCallback	update,
		     gint	*value,
		     gchar	*buffer)
{
  GtkWidget *label, *entry;
  
  label = gtk_label_new (name);
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, x, x+1, y, y+1,
		    GIMP_FILL|GIMP_EXPAND, GIMP_FILL, 5, 0);

  entry = gtk_entry_new();
  gimp_table_attach (table, entry, x+1, x+2, y, y+1,
		    GIMP_FILL|GIMP_EXPAND, GIMP_FILL, 10, 0);
  gtk_widget_set_size_request (entry, ENTRY_WIDTH, -1);
  sprintf (buffer, "%d", *(gint *)value);
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
  g_signal_connect (entry, "changed",
		      update, value);
}

static void
gtkW_table_add_scale (GtkWidget	*table,
		      gchar	*name,
		      gint	x,
		      gint	y,
		      GCallback update,
		      gdouble *value,
		      gdouble min,
		      gdouble max,
		      gdouble step)
{
  GtkAdjustment *scale_data;
  GtkWidget *label, *scale;
  
  label = gtk_label_new (name);
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, x, x+1, y, y+1,
		    GIMP_FILL|GIMP_EXPAND, GIMP_FILL, 5, 0);

  scale_data = gtk_adjustment_new (*value, min, max, step, step, 0.0);

  scale = gimp_hscale_new (scale_data, 1);
  gtk_widget_set_size_request (scale, SCALE_WIDTH, -1);
  gimp_table_attach (table, scale, x+1, x+2, y, y+1, 
		    GIMP_FILL|GIMP_EXPAND, GIMP_FILL, 10, 5);
  gtk_scale_set_value_pos (GTK_SCALE (scale), GTK_POS_TOP);
  gtk_scale_set_digits (GTK_SCALE (scale), 2);
  g_signal_connect (scale_data, "value-changed",
		      update, value);
}

/* end of vpropagate.c */
