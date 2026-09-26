/*
 * Animation Playback plug-in version 0.94.2
 *
 * Adam D. Moss : 1997-98 : adam@gimp.org : adam@foxbox.org
 *
 *
 * This is part of the GIMP package and is released under the GNU
 * Public License.
 */

/*
 * REVISION HISTORY:
 *
 * 98.04.28 : version 0.94.2
 *            Fixed a time-parsing bug.
 *
 * 98.04.05 : version 0.94.0
 *            Improved performance and removed flicker when shaped.
 *            Shaped mode also works with RGB* images now.
 *            Fixed some longstanding potential visual debris.
 *
 * 98.04.04 : version 0.92.0
 *            Improved responsiveness and performance for the new
 *            shaped-animation mode.  Still some flicker.
 *
 * 98.04.02 : version 0.90.0
 *            EXPERIMENTAL wackyness - try dragging the animation
 *            out of the plugin dialog's preview box...
 *            (only works on non-RGB* images for now)
 *
 * 98.03.16 : version 0.85.0
 *            Implemented some more rare opaque/alpha combinations.
 *
 * 98.03.15 : version 0.84.0
 *            Tried to clear up the GTK object/cast warnings.  Only
 *            partially successful.  Could use some help.
 *
 * 97.12.11 : version 0.83.0
 *            GTK's timer logic changed a little... adjusted
 *            plugin to fit.
 *
 * 97.09.16 : version 0.81.7
 *            Fixed progress bar's off-by-one problem with
 *            the new timing.  Fixed erroneous black bars which
 *            were sometimes visible when the first frame was
 *            smaller than the image itself.  Made playback
 *            controls inactive when image doesn't have multiple
 *            frames.  Moved progress bar above control buttons,
 *            it's less distracting there.  More cosmetic stuff.
 *
 * 97.09.15 : version 0.81.0
 *            Now plays INDEXED and GRAY animations.
 *
 * 97.09.15 : version 0.75.0
 *            Next frame is generated ahead of time - results
 *            in more precise timing.
 *
 * 97.09.14 : version 0.70.0
 *            Initial release.  RGB only.
 */

/*
 * GTK 4 PORT:
 *  The frame is kept in preview_data and painted from a cairo image
 *  surface in a GtkDrawingArea's draw function; playback runs from a
 *  g_timeout.  The experimental "drag the animation out of the
 *  dialog" mode, which used an X11 shaped popup window positioned at
 *  the pointer, has been removed: GTK 4 has neither shaped windows
 *  nor a way to place a toplevel at screen coordinates.
 */

/*
 * BUGS:
 *  Gets understandably upset if the source image is deleted
 *    while the animation is playing.  Decent solution welcome.
 *
 *  Any more?  Let me know!
 */

/*
 * TODO:
 *  pdb interface - should we bother?
 *
 *  speedups (caching?  most bottlenecks seem to be in pixelrgns)
 *    -> do pixelrgns properly!
 *
 *  write other half of the user interface (default timing, disposal &c)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"



typedef enum
{
  DISPOSE_UNDEFINED = 0x00,
  DISPOSE_COMBINE   = 0x01,
  DISPOSE_REPLACE   = 0x02
} DisposeType;



/* Declare local functions. */
static void query(void);
static void run(char *name,
		int nparams,
		GParam * param,
		int *nreturn_vals,
		GParam ** return_vals);

static        void do_playback        (void);
static         int parse_ms_tag       (char *str);
static DisposeType parse_disposal_tag (char *str);

static void window_destroy_callback (GtkWidget *widget,
				     gpointer   data);
static void window_close_callback  (GtkWidget *widget,
				    gpointer   data);
static void preview_draw           (GtkDrawingArea *area,
				    cairo_t        *cr,
				    int             w,
				    int             h,
				    gpointer        data);
static void playstop_callback  (GtkWidget *widget,
				gpointer   data);
static void rewind_callback  (GtkWidget *widget,
			      gpointer   data);
static void step_callback  (GtkWidget *widget,
			    gpointer   data);

static DisposeType  get_frame_disposal  (guint whichframe);
static void         render_frame        (gint32 whichframe);
static void         show_frame          (void);
static void         total_alpha_preview (void);
static void         init_preview_misc   (void);



GPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc */
  NULL,  /* quit_proc */
  query, /* query_proc */
  run,   /* run_proc */
};




/* Global widgets'n'stuff */
guchar*    preview_data;
static     GtkWidget* dlg = NULL;
static     GtkWidget* preview = NULL;
static     cairo_surface_t* preview_surface = NULL;
GtkProgressBar* progress;
guint      width,height;
guchar*    preview_alpha1_data;
guchar*    preview_alpha2_data;
gint32     image_id;
gint32     total_frames;
guint      frame_number;
gint32*    layers;
GDrawable* drawable;
gboolean   playing = FALSE;
guint      timer = 0;
GImageType imagetype;
guchar*    palette;
gint       ncolours;





MAIN()

static void query()
{
  static GParamDef args[] =
  {
    {PARAM_INT32, "run_mode", "Interactive, non-interactive"},
    {PARAM_IMAGE, "image", "Input image"},
    {PARAM_DRAWABLE, "drawable", "Input drawable (unused)"},
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof(args) / sizeof(args[0]);
  static int nreturn_vals = 0;

  gimp_install_procedure("plug_in_animationplay",
			 "This plugin allows you to preview a GIMP layer-based animation.",
			 "",
			 "Adam D. Moss <adam@gimp.org>",
			 "Adam D. Moss <adam@gimp.org>",
			 "1997, 1998...",
			 "<Image>/Filters/Animation/Animation Playback",
			 "RGB*, INDEXED*, GRAY*",
			 PROC_PLUG_IN,
			 nargs, nreturn_vals,
			 args, return_vals);
}

static void run(char *name, int n_params, GParam * param, int *nreturn_vals,
		GParam ** return_vals)
{
  static GParam values[1];
  GRunModeType run_mode;
  GStatusType status = STATUS_SUCCESS;

  *nreturn_vals = 1;
  *return_vals = values;

  run_mode = param[0].data.d_int32;

  if (run_mode == RUN_NONINTERACTIVE) {
    if (n_params != 3) {
      status = STATUS_CALLING_ERROR;
    }
  }

  if (status == STATUS_SUCCESS) {

    image_id = param[1].data.d_image;

    do_playback();
    
    if (run_mode != RUN_NONINTERACTIVE)
      gimp_displays_flush();
  }

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
}



static int
parse_ms_tag (char *str)
{
  gint sum = 0;
  gint offset = 0;
  gint length;

  length = strlen(str);

find_another_bra:

  while ((offset<length) && (str[offset]!='('))
    offset++;
  
  if (offset>=length)
    return(-1);

  if (!isdigit(str[++offset]))
    goto find_another_bra;

  do
    {
      sum *= 10;
      sum += str[offset] - '0';
      offset++;
    }
  while ((offset<length) && (isdigit(str[offset])));  

  if (length-offset <= 2)
    return(-3);

  if ((toupper(str[offset]) != 'M') || (toupper(str[offset+1]) != 'S'))
    return(-4);

  return (sum);
}


static DisposeType
parse_disposal_tag (char *str)
{
  gint offset = 0;
  gint length;

  length = strlen(str);

  while ((offset+9)<=length)
    {
      if (strncmp(&str[offset],"(combine)",9)==0) 
	return(DISPOSE_COMBINE);
      if (strncmp(&str[offset],"(replace)",9)==0) 
	return(DISPOSE_REPLACE);
      offset++;
    }

  return (DISPOSE_UNDEFINED); /* FIXME */
}


static void
preview_draw (GtkDrawingArea *area,
	      cairo_t        *cr,
	      int             w,
	      int             h,
	      gpointer        data)
{
  if (preview_surface == NULL)
    return;

  cairo_set_source_surface (cr, preview_surface, 0, 0);
  cairo_paint (cr);
}


static void
build_dialog(GImageType basetype,
	     char*      imagename)
{
  gchar* windowname;

  GtkWidget* button;
  GtkWidget* frame;
  GtkWidget* frame2;
  GtkWidget* vbox;
  GtkWidget* hbox;
  GtkWidget* hbox2;

  gtk_init ();

  windowname = g_strconcat ("Animation Playback: ", imagename, NULL);
  dlg = gimp_dialog_new (windowname);
  g_free(windowname);
  g_signal_connect (dlg, "destroy",
		    G_CALLBACK (window_destroy_callback), NULL);


  /* Action area - 'close' button only. */

  gimp_dialog_add_button (dlg, "Close",
			  G_CALLBACK (window_close_callback), dlg, TRUE);


  {
    /* The 'playback' half of the dialog */

    windowname = g_strconcat ("Playback: ", imagename, NULL);
    frame = gtk_frame_new (windowname);
    g_free(windowname);
    gimp_container_set_border_width (frame, 3);
    gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);

    {
      hbox = gimp_hbox_new (FALSE, 5);
      gimp_container_set_border_width (hbox, 3);
      gtk_frame_set_child (GTK_FRAME (frame), hbox);

      {
	vbox = gimp_vbox_new (FALSE, 5);
	gimp_container_set_border_width (vbox, 3);
	gimp_box_pack_start (hbox, vbox, TRUE, TRUE, 0);

	{
	  progress = GTK_PROGRESS_BAR (gtk_progress_bar_new ());
	  gtk_widget_set_size_request (GTK_WIDGET (progress), 150, 15);
	  gimp_box_pack_start (vbox, GTK_WIDGET (progress), TRUE, TRUE, 0);

	  hbox2 = gimp_hbox_new (FALSE, 0);
	  gimp_box_pack_start (vbox, hbox2, TRUE, TRUE, 0);

	  {
	    button = gtk_button_new_with_label ("Play/Stop");
	    g_signal_connect (button, "clicked",
			      G_CALLBACK (playstop_callback), NULL);
	    gimp_box_pack_start (hbox2, button, TRUE, TRUE, 0);

	    button = gtk_button_new_with_label ("Rewind");
	    g_signal_connect (button, "clicked",
			      G_CALLBACK (rewind_callback), NULL);
	    gimp_box_pack_start (hbox2, button, TRUE, TRUE, 0);

	    button = gtk_button_new_with_label ("Step");
	    g_signal_connect (button, "clicked",
			      G_CALLBACK (step_callback), NULL);
	    gimp_box_pack_start (hbox2, button, TRUE, TRUE, 0);
	  }
	  /* If there aren't multiple frames, playback controls make no
	     sense */
	  if (total_frames<=1) gtk_widget_set_sensitive (hbox2, FALSE);

	  hbox2 = gimp_hbox_new (TRUE, 0);
	  gimp_box_pack_start (vbox, hbox2, FALSE, FALSE, 0);
	  {
	    frame2 = gtk_frame_new (NULL);
	    gtk_widget_set_halign (frame2, GTK_ALIGN_CENTER);
	    gimp_box_pack_start (hbox2, frame2, TRUE, FALSE, 0);

	    {
	      preview_surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
							    width, height);
	      preview = gtk_drawing_area_new ();
	      gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (preview),
						  width);
	      gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (preview),
						   height);
	      gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (preview),
					      preview_draw, NULL, NULL);
	      gtk_frame_set_child (GTK_FRAME (frame2), preview);
	    }
	  }
	}
      }
    }
  }
  gtk_window_present (GTK_WINDOW (dlg));
}



static void do_playback(void)
{
  int i;

  width     = gimp_image_width(image_id);
  height    = gimp_image_height(image_id);
  layers    = gimp_image_get_layers (image_id, &total_frames);
  imagetype = gimp_image_base_type(image_id);

  if (imagetype == INDEXED)
    {
      /* Pixel values may exceed the colourmap size, so always keep a
	 full 256-entry palette to index into. */
      guchar *cmap = gimp_image_get_cmap(image_id, &ncolours);

      palette = g_malloc0(768);
      if (cmap != NULL)
	{
	  if (ncolours > 256)
	    ncolours = 256;
	  if (ncolours > 0)
	    memcpy(palette, cmap, ncolours * 3);
	  g_free(cmap);
	}
    }
  else if (imagetype == GRAY)
    {
      /* This is a bit sick, until this plugin ever gets
	 real GRAY support (not worth it?) */
      palette = g_malloc(768);
      for (i=0;i<256;i++)
      {
          palette[i*3] = palette[i*3+1] = palette[i*3+2] = i;
      }
      ncolours = 256;
    }


  frame_number = 0;

  /* cache hint "cache nothing", since we iterate over every
     tile in every layer. */
  gimp_tile_cache_size (0);

  init_preview_misc();
  build_dialog(gimp_image_base_type(image_id),
               gimp_image_get_filename(image_id));

  /* Make sure that whole preview is dirtied with pure-alpha */
  total_alpha_preview();

  render_frame(0);
  show_frame();

  gimp_main_loop_run ();

  if (preview_surface)
    {
      cairo_surface_destroy (preview_surface);
      preview_surface = NULL;
    }
}


/* Rendering Functions */

static void
render_frame(gint32 whichframe)
{
  GPixelRgn pixel_rgn;
  static guchar *rawframe = NULL;
  static gint rawwidth=0, rawheight=0, rawbpp=0;
  gint rawx=0, rawy=0;
  guchar* srcptr;
  guchar* destptr;
  gint i,j; /* imaginative loop variables */
  DisposeType dispose;


  if (whichframe >= total_frames)
    {
      printf("playback: Asked for frame number %d in a %d-frame animation!\n",
	     (int) (whichframe+1), (int) total_frames);
      exit(-1);
    }

  drawable = gimp_drawable_get (layers[total_frames-(whichframe+1)]);

  dispose = get_frame_disposal(frame_number);

  /* Image has been closed/etc since we got the layer list? */
  /* FIXME - How do we tell if a gimp_drawable_get() fails? */
  if (gimp_drawable_width(drawable->id)==0)
    {
      window_close_callback(NULL, NULL);
    }

  if (((dispose==DISPOSE_REPLACE)||(whichframe==0)) &&
      gimp_drawable_has_alpha(drawable->id))
    {
      total_alpha_preview();
    }


  /* only get a new 'raw' drawable-data buffer if this and
     the previous raw buffer were different sizes*/

  if ((rawwidth*rawheight*rawbpp)
      !=
      ((gimp_drawable_width(drawable->id)*
	gimp_drawable_height(drawable->id)*
	gimp_drawable_bpp(drawable->id))))
    {
      if (rawframe != NULL) g_free(rawframe);
      rawframe = g_malloc((gimp_drawable_width(drawable->id)) *
			  (gimp_drawable_height(drawable->id)) *
			  (gimp_drawable_bpp(drawable->id)));
    }
	
  rawwidth = gimp_drawable_width(drawable->id);
  rawheight = gimp_drawable_height(drawable->id);
  rawbpp = gimp_drawable_bpp(drawable->id);


  /* Initialise and fetch the whole raw new frame */

  gimp_pixel_rgn_init (&pixel_rgn,
		       drawable,
		       0, 0,
		       drawable->width, drawable->height,
		       FALSE,
		       FALSE);
  gimp_pixel_rgn_get_rect (&pixel_rgn,
			   rawframe,
			   0, 0,
			   drawable->width, drawable->height);
  /*  gimp_pixel_rgns_register (1, &pixel_rgn);*/

  gimp_drawable_offsets (drawable->id,
			 &rawx,
			 &rawy);


  /* render... into preview_data; show_frame () puts it on screen. */

  switch (imagetype)
    {
    case RGB:
      if ((rawwidth==width) &&
	  (rawheight==height) &&
	  (rawx==0) &&
	  (rawy==0))
	{
	  /* --- These cases are for the best cases,  in        --- */
	  /* --- which this frame is the same size and position --- */
	  /* --- as the preview buffer itself                   --- */

	  if (gimp_drawable_has_alpha (drawable->id))
	    { /* alpha */
	      destptr = preview_data;
	      srcptr  = rawframe;

	      i = rawwidth*rawheight;
	      while (--i)
		{
		  if (!(*(srcptr+3)&128))
		    {
		      srcptr  += 4;
		      destptr += 3;
		      continue;
		    }
		  *(destptr++) = *(srcptr++);
		  *(destptr++) = *(srcptr++);
		  *(destptr++) = *(srcptr++);
		  srcptr++;
		}
	    }
	  else /* no alpha */
	    {
	      if ((rawwidth==width)&&(rawheight==height))
		{
		  memcpy(preview_data, rawframe, width*height*3);
		}
	    }
	}
      else
	{
	  /* --- These are suboptimal catch-all cases for when  --- */
	  /* --- this frame is bigger/smaller than the preview  --- */
	  /* --- buffer, and/or offset within it.               --- */

	  if (gimp_drawable_has_alpha (drawable->id))
	    { /* alpha */

	      srcptr = rawframe;

	      for (j=rawy; j<rawheight+rawy; j++)
		{
		  for (i=rawx; i<rawwidth+rawx; i++)
		    {
		      if ((i>=0 && i<width) &&
			  (j>=0 && j<height))
			{
			  if (*(srcptr+3)&128)
			    {
			      preview_data[(j*width+i)*3   ] = *(srcptr);
			      preview_data[(j*width+i)*3 +1] = *(srcptr+1);
			      preview_data[(j*width+i)*3 +2] = *(srcptr+2);
			    }
			}

		      srcptr += 4;
		    }
		}
	    }
	  else
	    {
	      /* noalpha */

	      srcptr = rawframe;

	      for (j=rawy; j<rawheight+rawy; j++)
		{
		  for (i=rawx; i<rawwidth+rawx; i++)
		    {
		      if ((i>=0 && i<width) &&
			  (j>=0 && j<height))
			{
			  preview_data[(j*width+i)*3   ] = *(srcptr);
			  preview_data[(j*width+i)*3 +1] = *(srcptr+1);
			  preview_data[(j*width+i)*3 +2] = *(srcptr+2);
			}

		      srcptr += 3;
		    }
		}
	    }
	}
      break;

    case GRAY:
    case INDEXED:
      if ((rawwidth==width) &&
	  (rawheight==height) &&
	  (rawx==0) &&
	  (rawy==0))
	{
	  /* --- These cases are for the best cases,  in        --- */
	  /* --- which this frame is the same size and position --- */
	  /* --- as the preview buffer itself                   --- */

	  if (gimp_drawable_has_alpha (drawable->id))
	    { /* alpha */
	      destptr = preview_data;
	      srcptr  = rawframe;

	      i = rawwidth*rawheight;
	      while (--i)
		{
		  if (!(*(srcptr+1)))
		    {
		      srcptr  += 2;
		      destptr += 3;
		      continue;
		    }

		  *(destptr++) = palette[3*(*(srcptr))];
		  *(destptr++) = palette[1+3*(*(srcptr))];
		  *(destptr++) = palette[2+3*(*(srcptr))];
		  srcptr+=2;
		}
	    }
	  else /* no alpha */
	    {
	      destptr = preview_data;
	      srcptr  = rawframe;

	      i = rawwidth*rawheight;
	      while (--i)
		{
		  *(destptr++) = palette[3*(*(srcptr))];
		  *(destptr++) = palette[1+3*(*(srcptr))];
		  *(destptr++) = palette[2+3*(*(srcptr))];
		  srcptr++;
		}
	    }
	}
      else
	{
	  /* --- These are suboptimal catch-all cases for when  --- */
	  /* --- this frame is bigger/smaller than the preview  --- */
	  /* --- buffer, and/or offset within it.               --- */

	  if (gimp_drawable_has_alpha (drawable->id))
	    { /* alpha */

	      srcptr = rawframe;

	      for (j=rawy; j<rawheight+rawy; j++)
		{
		  for (i=rawx; i<rawwidth+rawx; i++)
		    {
		      if ((i>=0 && i<width) &&
			  (j>=0 && j<height))
			{
			  if (*(srcptr+1))
			    {
			      preview_data[(j*width+i)*3   ] =
				palette[3*(*(srcptr))];
			      preview_data[(j*width+i)*3 +1] =
				palette[1+3*(*(srcptr))];
			      preview_data[(j*width+i)*3 +2] =
				palette[2+3*(*(srcptr))];
			    }
			}

		      srcptr += 2;
		    }
		}
	    }
	  else
	    {
	      /* noalpha */

	      srcptr = rawframe;

	      for (j=rawy; j<rawheight+rawy; j++)
		{
		  for (i=rawx; i<rawwidth+rawx; i++)
		    {
		      if ((i>=0 && i<width) &&
			  (j>=0 && j<height))
			{
			  preview_data[(j*width+i)*3   ] =
			    palette[3*(*(srcptr))];
			  preview_data[(j*width+i)*3 +1] =
			    palette[1+3*(*(srcptr))];
			  preview_data[(j*width+i)*3 +2] =
			    palette[2+3*(*(srcptr))];
			}

		      srcptr ++;
		    }
		}
	    }
	}
      break;

    default:
      break;
    }

  /* clean up */  
  gimp_drawable_detach(drawable);
}


static void
show_frame(void)
{
  guchar *dest;
  guchar *src;
  gint    stride;
  gint    x, y;

  if (dlg == NULL || preview_surface == NULL)
    return;

  /* Copy the preview buffer into the surface the draw function paints */
  cairo_surface_flush (preview_surface);
  dest   = cairo_image_surface_get_data (preview_surface);
  stride = cairo_image_surface_get_stride (preview_surface);
  src    = preview_data;

  for (y=0;y<height;y++)
    {
      guint32 *row = (guint32 *) (dest + y * stride);

      for (x=0;x<width;x++)
	{
	  row[x] = ((guint32) src[0] << 16) | ((guint32) src[1] << 8) | src[2];
	  src += 3;
	}
    }
  cairo_surface_mark_dirty (preview_surface);

  /* Tell GTK to physically draw the preview */
  gtk_widget_queue_draw (preview);

  /* update the dialog's progress bar */
  gtk_progress_bar_set_fraction (progress,
				 CLAMP ((float)frame_number/(float)(total_frames-0.999),
					0.0, 1.0));
}


static void
init_preview_misc(void)
{
  int i;

  preview_data = g_malloc(width*height*3);
  preview_alpha1_data = g_malloc(width*3);
  preview_alpha2_data = g_malloc(width*3);

  for (i=0;i<width;i++)
    {
      if (i&8)
	{
	  preview_alpha1_data[i*3 +0] =
	  preview_alpha1_data[i*3 +1] =
	  preview_alpha1_data[i*3 +2] = 102;
	  preview_alpha2_data[i*3 +0] =
	  preview_alpha2_data[i*3 +1] =
	  preview_alpha2_data[i*3 +2] = 154;
	}
      else
	{
	  preview_alpha1_data[i*3 +0] =
	  preview_alpha1_data[i*3 +1] =
	  preview_alpha1_data[i*3 +2] = 154;
	  preview_alpha2_data[i*3 +0] =
	  preview_alpha2_data[i*3 +1] =
	  preview_alpha2_data[i*3 +2] = 102;
	}
    }
}


static void
total_alpha_preview(void)
{
  int i;

  for (i=0;i<height;i++)
    {
      if (i&8)
	memcpy(&preview_data[i*3*width], preview_alpha1_data, 3*width);
      else
	memcpy(&preview_data[i*3*width], preview_alpha2_data, 3*width);
    }
}



/* Util. */

static void
remove_timer(void)
{
  if (timer)
    {
      g_source_remove (timer);
      timer = 0;
    }
}

static void
do_step(void)
{
  frame_number = (frame_number+1)%total_frames;
  render_frame(frame_number);
}

static guint32
get_frame_duration (guint whichframe)
{
  gchar* layer_name;
  gint   duration;

  layer_name = gimp_layer_get_name(layers[total_frames-(whichframe+1)]);
  duration = parse_ms_tag(layer_name);
  g_free(layer_name);

  if (duration < 0) duration = 125; /* FIXME for default-if-not-said  */
  if (duration == 0) duration = 125; /* FIXME - 0-wait is nasty */

  return ((guint32) duration);
}

static DisposeType
get_frame_disposal (guint whichframe)
{
  gchar* layer_name;
  DisposeType disposal;

  layer_name = gimp_layer_get_name(layers[total_frames-(whichframe+1)]);
  disposal = parse_disposal_tag(layer_name);
  g_free(layer_name);

  return(disposal);
}



/*  Callbacks  */

static void
window_destroy_callback (GtkWidget *widget,
			 gpointer   data)
{
  if (playing)
    playstop_callback(NULL, NULL);
  remove_timer();

  dlg = NULL;
  preview = NULL;

  gimp_main_loop_quit ();
}

static void
window_close_callback (GtkWidget *widget,
		       gpointer   data)
{
  if (dlg)
    gtk_window_destroy (GTK_WINDOW (dlg));
}

static gboolean
advance_frame_callback (gpointer data)
{
  /* this source is finished; the next one is scheduled below */
  timer = 0;

  if (dlg == NULL)
    return G_SOURCE_REMOVE;

  timer = g_timeout_add (get_frame_duration(frame_number),
			 advance_frame_callback, NULL);
  show_frame();
  do_step();

  return G_SOURCE_REMOVE;
}

static void
playstop_callback (GtkWidget *widget,
		   gpointer   data)
{
  if (!playing)
    { /* START PLAYING */
      playing = TRUE;
      remove_timer();
      timer = g_timeout_add (0, advance_frame_callback, NULL);
    }
  else
    { /* STOP PLAYING */
      playing = FALSE;
      remove_timer();
    }
}

static void
rewind_callback (GtkWidget *widget,
		 gpointer   data)
{
  playing = FALSE;
  remove_timer();

  frame_number = 0;
  render_frame(frame_number);
  show_frame();
}

static void
step_callback (GtkWidget *widget,
	       gpointer   data)
{
  playing = FALSE;
  remove_timer();

  do_step();
  show_frame();
}

