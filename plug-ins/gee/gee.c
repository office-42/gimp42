/*
 * Adam D. Moss : 1998 : adam@gimp.org : adam@foxbox.org
 *
 * This is part of the GIMP package and is released under the GNU
 * Public License.
 */

/*
 *     Version 1.01 : 98.04.19
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#include "glib.h"
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#include "config.h"



/* Declare local functions. */
static void query(void);
static void run(char *name,
		int nparams,
		GParam * param,
		int *nreturn_vals,
		GParam ** return_vals);

static void do_playback (void);

static gboolean window_delete_callback (GtkWindow *window,
					gpointer   data);
static void window_close_callback  (GtkWidget *widget);
static gboolean step_callback  (gpointer   data);
static void toggle_feedbacktype  (GtkGestureClick *gesture,
				  gint             n_press,
				  gdouble          x,
				  gdouble          y,
				  gpointer         data);
static void pointer_motion       (GtkEventControllerMotion *controller,
				  gdouble                   x,
				  gdouble                   y,
				  gpointer                  data);

static void         render_frame        (void);
static void         show_frame          (void);
static void         init_preview_misc   (void);



GPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc */
  NULL,  /* quit_proc */
  query, /* query_proc */
  run,   /* run_proc */
};




static const guint  width = 256;
static const guint height = 256;


/* Global widgets'n'stuff */
static guchar*    seed_data;
static guchar*    preview_data1;
static guchar*    preview_data2;
static GtkWidget* preview = NULL;
static gint32     image_id;
static gint32     total_frames;
static gint32*    layers;
static GDrawable* drawable;
static GImageType imagetype;
static guchar*    palette;
static gint       ncolours;

static guint      timeout_tag;
static GtkWidget* window;
static gdouble    pointer_x = 128.0;
static gdouble    pointer_y = 128.0;
static gboolean   feedbacktype = FALSE;
static gboolean   rgb_mode;



MAIN()

static void query(void)
{
  static GParamDef args[] =
  {
    {PARAM_INT32, "run_mode", "Always interactive"},
    {PARAM_IMAGE, "image", "Input Image"},
    {PARAM_DRAWABLE, "drawable", "Input Drawable"},
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof(args) / sizeof(args[0]);
  static int nreturn_vals = 0;

  gimp_install_procedure("plug_in_the_egg",
			 "A big hello from the GIMP team!",
			 "",
			 "Adam D. Moss <adam@gimp.org>",
			 "Adam D. Moss <adam@gimp.org>",
			 "1998",
			 /*"<Image>/Filters/Animation/The Egg",*/
			 NULL,
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

  SRAND_FUNC (time(0));

  run_mode = param[0].data.d_int32;

  /*  if (run_mode == RUN_NONINTERACTIVE) {*/
    if (n_params != 3)
      {
	status = STATUS_CALLING_ERROR;
      }
    /*  }*/

  if (status == STATUS_SUCCESS)
    {
      drawable = gimp_drawable_get (param[2].data.d_drawable);
      image_id = param[1].data.d_image;
      
      do_playback();
      /*    if (run_mode != RUN_NONINTERACTIVE)
	    gimp_displays_flush();*/
    }

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
}



static void
build_dialog(GImageType basetype,
	     char*      imagename)
{
  GtkWidget* dlg;
  GtkWidget* button;
  GtkWidget* frame;
  GtkWidget* frame2;
  GtkWidget* vbox;
  GtkWidget* hbox;
  GtkWidget* hbox2;
  GtkGesture* click;
  GtkEventController* motion;

  gtk_init ();

  dlg = gimp_dialog_new ("GEE!  The GIMP E'er Egg!");
  window = dlg;
  g_signal_connect (dlg, "close-request",
		    G_CALLBACK (window_delete_callback),
		    NULL);


  /* Action area - 'close' button only. */

  button = gimp_dialog_add_button (dlg, "** Thank you for choosing GIMP **",
				   NULL, NULL, TRUE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (window_close_callback),
			    dlg);


  {
    /* The 'playback' half of the dialog */

    frame = gtk_frame_new (NULL);
    gimp_container_set_border_width (frame, 3);
    gimp_box_pack_start (gimp_dialog_get_vbox (dlg),
			 frame, TRUE, TRUE, 0);

    {
      hbox = gimp_hbox_new (FALSE, 5);
      gimp_container_set_border_width (hbox, 3);
      gtk_frame_set_child (GTK_FRAME (frame), hbox);

      {
	vbox = gimp_vbox_new (FALSE, 5);
	gimp_container_set_border_width (vbox, 3);
	gimp_box_pack_start (hbox, vbox, TRUE, TRUE, 0);

	{
	  hbox2 = gimp_hbox_new (TRUE, 0);
	  gimp_box_pack_start (vbox, hbox2, FALSE, FALSE, 0);
	  {
	    frame2 = gtk_frame_new (NULL);
	    gimp_box_pack_start (hbox2, frame2, FALSE, FALSE, 0);

	    {
	      preview = gimp_preview_new (rgb_mode?
					  GIMP_PREVIEW_COLOR:
					  GIMP_PREVIEW_GRAYSCALE);
	      gimp_preview_size (GIMP_PREVIEW (preview), width, height);
	      gtk_frame_set_child (GTK_FRAME (frame2), preview);

	      click = gtk_gesture_click_new ();
	      g_signal_connect (click, "pressed",
				G_CALLBACK (toggle_feedbacktype), NULL);
	      gtk_widget_add_controller (preview,
					 GTK_EVENT_CONTROLLER (click));

	      /* The animation follows the pointer anywhere in the
	       * window, as it did with gdk_window_get_pointer (). */
	      motion = gtk_event_controller_motion_new ();
	      g_signal_connect (motion, "motion",
				G_CALLBACK (pointer_motion), NULL);
	      gtk_widget_add_controller (dlg, motion);
	    }
	  }
	}
      }
    }
  }
  gtk_window_present (GTK_WINDOW (dlg));

  timeout_tag = g_timeout_add (20, step_callback, NULL);
}



static void do_playback(void)
{
  layers    = gimp_image_get_layers (image_id, &total_frames);
  imagetype = gimp_image_base_type(image_id);

  if (imagetype == INDEXED)
    palette = gimp_image_get_cmap(image_id, &ncolours);

  /* cache hint */
  gimp_tile_cache_ntiles (MAX(drawable->width,drawable->height)/
			  MIN(gimp_tile_width(),gimp_tile_height())
			  +1);

  init_preview_misc();
  build_dialog(gimp_image_base_type(image_id),
               gimp_image_get_filename(image_id));

  
  render_frame();
  show_frame();

  gimp_main_loop_run ();
}


/* Rendering Functions */

/* Adam's silly algorithm. */
static void domap1(unsigned char *src, unsigned char *dest,
	    int bx, int by, int cx, int cy)
{
#ifdef __AAARGH_GNUC__
  unsigned int dy __attribute__ ((aligned));
  signed int bycxmcybx __attribute__ ((aligned));
  signed int bx2,by2 __attribute__ ((aligned));
  signed int cx2,cy2 __attribute__ ((aligned));
  unsigned int __attribute__ ((aligned)) basesx;
  unsigned int __attribute__ ((aligned)) basesy;
#else
  unsigned int dy;
  signed int bycxmcybx;
  signed int bx2,by2;
  signed int cx2,cy2;
  unsigned int basesx;
  unsigned int basesy;
#endif

  bycxmcybx = (by*cx-cy*bx);

  /* A little sub-pixel jitter to liven things up. */
  basesx = (((RAND_FUNC ()%89)<<19)/bycxmcybx) + ((-128-((128*256)/(cx+bx)))<<11);
  basesy = (((RAND_FUNC ()%89)<<19)/bycxmcybx) + ((-128-((128*256)/(cy+by)))<<11);

  bx2 = ((bx)<<19)/bycxmcybx;
  cx2 = ((cx)<<19)/bycxmcybx;
  by2 = ((by)<<19)/bycxmcybx;
  cy2 = ((cy)<<19)/bycxmcybx;

  for (dy=0;dy<256;dy++)
    {
#ifdef __AAARGH_GNUC__
      unsigned int __attribute__ ((aligned)) sx;
      unsigned int __attribute__ ((aligned)) sy;
      unsigned int __attribute__ ((aligned)) dx;
#else
      unsigned int sx;
      unsigned int sy;
      unsigned int dx;
#endif

      sx = (basesx-=bx2);
      sy = (basesy+=cx2);

      dx = 256;
      do
	{
	  *dest++ = (*(src +
		   (
		    (
		     ((255&(
			    (sx>>11)
			    )))
		     |
		     ((((255&(
			      (sy>>11)
			      ))<<8)))
		     )
		    )));
	  ;
	  sx += by2;
	  sy -= cy2;
	}
      while (--dx);
    }
}

/* 3bypp variant */
static void domap3(unsigned char *src, unsigned char *dest,
	    int bx, int by, int cx, int cy)
{
#ifdef __AAARGH_GNUC__
  unsigned int dy __attribute__ ((aligned));
  signed int bycxmcybx __attribute__ ((aligned));
  signed int bx2,by2 __attribute__ ((aligned));
  signed int cx2,cy2 __attribute__ ((aligned));
  unsigned int __attribute__ ((aligned)) basesx;
  unsigned int __attribute__ ((aligned)) basesy;
#else
  unsigned int dy;
  signed int bycxmcybx;
  signed int bx2,by2;
  signed int cx2,cy2;
  unsigned int basesx;
  unsigned int basesy;
#endif

  bycxmcybx = (by*cx-cy*bx);

  /* A little sub-pixel jitter to liven things up. */
  basesx = (((RAND_FUNC ()%89)<<19)/bycxmcybx) + ((-128-((128*256)/(cx+bx)))<<11);
  basesy = (((RAND_FUNC ()%89)<<19)/bycxmcybx) + ((-128-((128*256)/(cy+by)))<<11);

  bx2 = ((bx)<<19)/bycxmcybx;
  cx2 = ((cx)<<19)/bycxmcybx;
  by2 = ((by)<<19)/bycxmcybx;
  cy2 = ((cy)<<19)/bycxmcybx;

  for (dy=0;dy<256;dy++)
    {
#ifdef __AAARGH_GNUC__
      unsigned int __attribute__ ((aligned)) sx;
      unsigned int __attribute__ ((aligned)) sy;
      unsigned int __attribute__ ((aligned)) dx;
#else
      unsigned int sx;
      unsigned int sy;
      unsigned int dx;      
#endif

      sx = (basesx-=bx2);
      sy = (basesy+=cx2);

      dx = 256;
      do
	{
	  unsigned char* addr;

	  addr = src + 3*
	    (
	     (
	      ((255&(
		     (sx>>11)
		     )))
	      |
	      ((((255&(
		       (sy>>11)
		       ))<<8)))
	      )
	     );

	  *dest++ = *(addr);
	  *dest++ = *(addr+1);
	  *dest++ = *(addr+2);

	  sx += by2;
	  sy -= cy2;
	}
      while (--dx);
    }
}


static void
render_frame(void)
{
  int i;
  static int frame = 0;
  unsigned char* tmp;
  static gint xp=128, yp=128;
  gint rxp, ryp;
  gint pixels;

  pixels = width*height*(rgb_mode?3:1);

  tmp = preview_data2;
  preview_data2 = preview_data1;
  preview_data1 = tmp;

  if (frame==0)
    {
      for (i=0;i<pixels;i++)
	{
	  preview_data2[i] =
	    preview_data1[i] =
	    seed_data[i];
	}
    }

  rxp = (gint) pointer_x;
  ryp = (gint) pointer_y;

  if ((abs(rxp)>60)||(abs(ryp)>60))
    {
      xp = rxp;
      yp = ryp;
    }

  if (rgb_mode)
    {
      domap3(preview_data2, preview_data1,
	     -(yp-xp)/2, xp+yp
	     ,
	     xp+yp, (yp-xp)/2
	     );

      for (i=0;i<height;i++)
	{
	  gimp_preview_draw_row (GIMP_PREVIEW (preview),
				&preview_data1[i*width*3],
				0, i, width);
	}

      if (frame != 0)
	{
	  if (feedbacktype)
	    {
	      for (i=0;i<pixels;i++)
		{
		  int t;
		  t = preview_data1[i] + seed_data[i] - 128;
		  preview_data1[i] = CLAMP(t,0,255);
		}
	    }
	  else
	    {
	      for (i=0;i<pixels;i++)
		{
		  preview_data1[i] = (preview_data1[i]*2 + seed_data[i]) /3;
		}
	    }	
	}
    }
  else /* GRAYSCALE */
    {
      domap1(preview_data2, preview_data1,
	     -(yp-xp)/2, xp+yp
	     ,
	     xp+yp, (yp-xp)/2
	     );

      for (i=0;i<height;i++)
	{
	  gimp_preview_draw_row (GIMP_PREVIEW (preview),
				&preview_data1[i*width],
				0, i, width);
	}

      if (frame != 0)
	{
	  if (feedbacktype)
	    {
	      for (i=0;i<pixels;i++)
		{
		  int t;
		  t = preview_data1[i] + seed_data[i] - 128;
		  preview_data1[i] = CLAMP(t,0,255);
		}
	    }
	  else
	    {
	      for (i=0;i<pixels;i++)
		{
		  preview_data1[i] = (preview_data1[i]*2 + seed_data[i]) /3;
		}
	    }	
	}
    }

  frame++;
}


static void
show_frame(void)
{
  /* The preview redraws itself after gimp_preview_draw_row ().  */
}


static void
init_preview_misc(void)
{
  GPixelRgn pixel_rgn;
  int i;
  gboolean has_alpha;

  if ((imagetype == RGB)||(imagetype == INDEXED))
    rgb_mode = TRUE;
  else
    rgb_mode = FALSE;

  has_alpha = gimp_drawable_has_alpha(drawable->id);

  seed_data = g_malloc(width*height*4);
  preview_data1 = g_malloc(width*height*(rgb_mode?3:1));
  preview_data2 = g_malloc(width*height*(rgb_mode?3:1));

  if ((drawable->width<256) || (drawable->height<256))
    {
      for (i=0;i<256;i++)
	{
	  if (i < drawable->height)
	    {
	      gimp_pixel_rgn_init (&pixel_rgn,
				   drawable,
				   drawable->width>256?
				   (drawable->width/2-128):0,
				   (drawable->height>256?
				   (drawable->height/2-128):0)+i,
				   MIN(256,drawable->width),
				   1,
				   FALSE,
				   FALSE);
	      gimp_pixel_rgn_get_rect (&pixel_rgn,
				       &seed_data[(256*i +
						 (
						  (
						   drawable->width<256 ?
						   (256-drawable->width)/2 :
						   0
						   )
						  +
						  (
						   drawable->height<256 ?
						   (256-drawable->height)/2 :
						   0
						   ) * 256
						  )) *
						 gimp_drawable_bpp
						 (drawable->id)
				       ],
				       drawable->width>256?
				       (drawable->width/2-128):0,
				       (drawable->height>256?
				       (drawable->height/2-128):0)+i,
				       MIN(256,drawable->width),
				       1);
	    }
	}
    }
  else
    {
      gimp_pixel_rgn_init (&pixel_rgn,
			   drawable,
			   drawable->width>256?(drawable->width/2-128):0,
			   drawable->height>256?(drawable->height/2-128):0,
			   MIN(256,drawable->width),
			   MIN(256,drawable->height),
			   FALSE,
			   FALSE);
      gimp_pixel_rgn_get_rect (&pixel_rgn,
			       seed_data,
			       drawable->width>256?(drawable->width/2-128):0,
			       drawable->height>256?(drawable->height/2-128):0,
			       MIN(256,drawable->width),
			       MIN(256,drawable->height));
    }

  gimp_drawable_detach(drawable);


  /* convert the image data of varying types into flat grey or rgb. */
  switch (imagetype)
    {
    case INDEXED:
      if (has_alpha)
	{
	  for (i=width*height;i>0;i--)
	    {
	      seed_data[3*(i-1)+2] =
		((palette[3*(seed_data[(i-1)*2])+2]*seed_data[(i-1)*2+1])/255)
		+ ((255-seed_data[(i-1)*2+1])*(RAND_FUNC ()%256))/255;
	      seed_data[3*(i-1)+1] =
		((palette[3*(seed_data[(i-1)*2])+1]*seed_data[(i-1)*2+1])/255)
		+ ((255-seed_data[(i-1)*2+1])*(RAND_FUNC ()%256))/255;
	      seed_data[3*(i-1)+0] =
		((palette[3*(seed_data[(i-1)*2])+0]*seed_data[(i-1)*2+1])/255)
		+ ((255-seed_data[(i-1)*2+1])*(RAND_FUNC ()%256))/255;
	    }
	}
      else
	{
	  for (i=width*height;i>0;i--)
	    {
	      seed_data[3*(i-1)+2] = palette[3*(seed_data[i-1])+2];
	      seed_data[3*(i-1)+1] = palette[3*(seed_data[i-1])+1];
	      seed_data[3*(i-1)+0] = palette[3*(seed_data[i-1])+0];
	    }
	}
      break;
    case GRAY:
      if (has_alpha)
	{
	  for (i=0;i<width*height;i++)
	    {
	      seed_data[i] =
		(seed_data[i*2]*seed_data[i*2+1])/255
		+ ((255-seed_data[i*2+1])*(RAND_FUNC ()%256))/255;
	    }
	}
      break;
    case RGB:
      if (has_alpha)
	{
	  for (i=0;i<width*height;i++)
	    {
	      seed_data[i*3+2] =
		(seed_data[i*4+2]*seed_data[i*4+3])/255
		+ ((255-seed_data[i*4+3])*(RAND_FUNC ()%256))/255;
	      seed_data[i*3+1] =
		(seed_data[i*4+1]*seed_data[i*4+3])/255
		+ ((255-seed_data[i*4+3])*(RAND_FUNC ()%256))/255;
	      seed_data[i*3+0] =
		(seed_data[i*4+0]*seed_data[i*4+3])/255
		+ ((255-seed_data[i*4+3])*(RAND_FUNC ()%256))/255;
	    }
	}
      break;
    default:
      break;
    }
}



/* Util. */

static int
do_step(void)
{
  render_frame();

  return(1);
}



/*  Callbacks  */

static void
stop_playback (void)
{
  if (timeout_tag)
    {
      g_source_remove (timeout_tag);
      timeout_tag = 0;
      gimp_main_loop_quit ();
    }
}

static gboolean
window_delete_callback (GtkWindow *window,
			gpointer   data)
{
  stop_playback ();

  return FALSE;
}

static void
window_close_callback (GtkWidget *widget)
{
  stop_playback ();

  if (widget)
    gtk_window_destroy (GTK_WINDOW (widget));
}

static void
toggle_feedbacktype (GtkGestureClick *gesture,
		     gint             n_press,
		     gdouble          x,
		     gdouble          y,
		     gpointer         data)
{
  feedbacktype = !feedbacktype;
}

static void
pointer_motion (GtkEventControllerMotion *controller,
		gdouble                   x,
		gdouble                   y,
		gpointer                  data)
{
  graphene_point_t in = GRAPHENE_POINT_INIT ((float) x, (float) y);
  graphene_point_t out;

  /* Pointer position relative to the preview, as before. */
  if (preview && gtk_widget_compute_point (window, preview, &in, &out))
    {
      pointer_x = out.x;
      pointer_y = out.y;
    }
}


static gboolean
step_callback (gpointer   data)
{
  do_step();
  show_frame();

  return G_SOURCE_CONTINUE;
}
