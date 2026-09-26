/*
 *   Destripe filter for The GIMP -- an image manipulation
 *   program
 *
 *   Copyright 1997 Marc Lehmann, heavily modified from a filter by
 *   Michael Sweet.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Contents:
 *
 *   main()                      - Main entry - just call gimp_main()...
 *   query()                     - Respond to a plug-in query...
 *   run()                       - Run the filter...
 *   destripe()                  - Destripe an image.
 *   destripe_dialog()           - Popup a dialog window...
 *   preview_init()              - Initialize the preview window...
 *   preview_scroll_callback()   - Update the preview when a scrollbar is moved.
 *   preview_update()            - Update the preview window.
 *   preview_exit()              - Free all memory used by the preview window...
 *   dialog_create_ivalue()      - Create an integer value control...
 *   dialog_iscale_update()      - Update the value field using the scale.
 *   dialog_ientry_update()      - Update the value field using the text entry.
 *   dialog_histogram_callback()
 *   dialog_ok_callback()        - Start the filter...
 *   dialog_cancel_callback()    - Cancel the filter...
 *   dialog_close_callback()     - Exit the filter dialog application.
 *
 *   1997/08/16 * Initial Revision.
 *   1998/02/06 * Minor changes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*
 * Constants...
 */

#define PLUG_IN_NAME		"plug_in_destripe"
#define PLUG_IN_VERSION		"0.2"
#define PREVIEW_SIZE		200
#define SCALE_WIDTH		140
#define ENTRY_WIDTH		40
#define MAX_AVG			100

/*
 * Local functions...
 */

static void	query(void);
static void	run(char *, int, GParam *, int *, GParam **);
static void	destripe(void);
static gint	destripe_dialog(void);
static void	dialog_create_ivalue(char *, GtkWidget *, int, gint *, int, int);
static void	dialog_iscale_update(GtkAdjustment *, gint *);
static void	dialog_ientry_update(GtkWidget *, gint *);
static void	dialog_ok_callback(GtkWidget *, gpointer);
static void	dialog_cancel_callback(GtkWidget *, gpointer);
static void	dialog_close_callback(GtkWidget *, gpointer);

static void	preview_init(void);
static void	preview_exit(void);
static void	preview_update(void);
static void	preview_scroll_callback(void);


/*
 * Globals...
 */

GPlugInInfo	PLUG_IN_INFO =
		{
		  NULL,		/* init_proc */
		  NULL,		/* quit_proc */
		  query,	/* query_proc */
		  run		/* run_proc */
		};

GtkWidget	*preview;		/* Preview widget */
int		preview_width,		/* Width of preview widget */
		preview_height,		/* Height of preview widget */
		preview_x1,		/* Upper-left X of preview */
		preview_y1,		/* Upper-left Y of preview */
		preview_x2,		/* Lower-right X of preview */
		preview_y2;		/* Lower-right Y of preview */
GtkAdjustment	*hscroll_data,		/* Horizontal scrollbar data */
		*vscroll_data;		/* Vertical scrollbar data */

GDrawable	*drawable = NULL;	/* Current image */
int		sel_x1,			/* Selection bounds */
		sel_y1,
		sel_x2,
		sel_y2;
int		histogram = FALSE;
int		img_bpp;		/* Bytes-per-pixel in image */
gint		run_filter = FALSE;	/* True if we should run the filter */

int		avg_width = 36;


/*
 * 'main()' - Main entry - just call gimp_main()...
 */

int
main(int  argc,		/* I - Number of command-line args */
     char *argv[])	/* I - Command-line args */
{
  return (gimp_main(argc, argv));
}


/*
 * 'query()' - Respond to a plug-in query...
 */

static void
query(void)
{
  static GParamDef	args[] =
  {
    { PARAM_INT32,	"run_mode",	"Interactive, non-interactive" },
    { PARAM_IMAGE,	"image",	"Input image" },
    { PARAM_DRAWABLE,	"drawable",	"Input drawable" },
    { PARAM_INT32,	"avg_width",	"Averaging filter width (default = 36)" }
  };
  static GParamDef	*return_vals = NULL;
  static int		nargs        = sizeof(args) / sizeof(args[0]),
			nreturn_vals = 0;


  gimp_install_procedure(PLUG_IN_NAME,
      "Destripe filter, used to remove vertical stripes caused by cheap scanners.",
      "This plug-in tries to remove vertical stripes from an image.",
      "Marc Lehmann <pcg@goof.com>", "Marc Lehmann <pcg@goof.com>",
      PLUG_IN_VERSION,
      "<Image>/Filters/Enhance/Destripe",
      "RGB*, GRAY*",
      PROC_PLUG_IN, nargs, nreturn_vals, args, return_vals);
}


/*
 * 'run()' - Run the filter...
 */

static void
run(char   *name,		/* I - Name of filter program. */
    int    nparams,		/* I - Number of parameters passed in */
    GParam *param,		/* I - Parameter values */
    int    *nreturn_vals,	/* O - Number of return values */
    GParam **return_vals)	/* O - Return values */
{
  GRunModeType	run_mode;	/* Current run mode */
  GStatusType	status;		/* Return status */
  static GParam	values[1];	/* Return values */


 /*
  * Initialize parameter data...
  */

  status   = STATUS_SUCCESS;
  run_mode = param[0].data.d_int32;

  values[0].type          = PARAM_STATUS;
  values[0].data.d_status = status;

  *nreturn_vals = 1;
  *return_vals  = values;

 /*
  * Get drawable information...
  */

  drawable = gimp_drawable_get(param[2].data.d_drawable);

  gimp_drawable_mask_bounds(drawable->id, &sel_x1, &sel_y1, &sel_x2, &sel_y2);

  img_bpp       = gimp_drawable_bpp(drawable->id);

 /*
  * See how we will run
  */

  switch (run_mode)
  {
    case RUN_INTERACTIVE :
       /*
        * Possibly retrieve data...
        */

        gimp_get_data(PLUG_IN_NAME, &avg_width);

       /*
        * Get information from the dialog...
        */

	if (!destripe_dialog())
          return;
        break;

    case RUN_NONINTERACTIVE :
       /*
        * Make sure all the arguments are present...
        */

        if (nparams != 4)
	  status = STATUS_CALLING_ERROR;
	else
	  avg_width = param[3].data.d_int32;
        break;

    case RUN_WITH_LAST_VALS :
       /*
        * Possibly retrieve data...
        */

	gimp_get_data(PLUG_IN_NAME, &avg_width);
	break;

    default :
        status = STATUS_CALLING_ERROR;
        break;
  };

 /*
  * Destripe the image...
  */

  if (status == STATUS_SUCCESS)
  {
    if ((gimp_drawable_color(drawable->id) ||
	 gimp_drawable_gray(drawable->id)))
    {
     /*
      * Set the tile cache size...
      */

      gimp_tile_cache_ntiles((drawable->width + gimp_tile_width() - 1) /
                             gimp_tile_width());

     /*
      * Run!
      */

      destripe();

     /*
      * If run mode is interactive, flush displays...
      */

      if (run_mode != RUN_NONINTERACTIVE)
        gimp_displays_flush();

     /*
      * Store data...
      */

      if (run_mode == RUN_INTERACTIVE)
        gimp_set_data(PLUG_IN_NAME, &avg_width, sizeof(avg_width));
    }
    else
      status = STATUS_EXECUTION_ERROR;
  };

 /*
  * Reset the current run status...
  */

  values[0].data.d_status = status;

 /*
  * Detach from the drawable...
  */

  gimp_drawable_detach(drawable);
}

static inline void
preview_draw_row (int x, int y, int w, guchar *row)
{
  guchar *rgb = g_new(guchar, w * 3);
  guchar *rgb_ptr;
  int i;
  
  switch (img_bpp)
  {
    case 1:
    case 2:
        for (i = 0, rgb_ptr = rgb; i < w; i++, row += img_bpp, rgb_ptr += 3)
          rgb_ptr[0] = rgb_ptr[1] = rgb_ptr[2] = *row;

	gimp_preview_draw_row(GIMP_PREVIEW(preview), rgb, x, y, w);
        break;

    case 3:
	gimp_preview_draw_row(GIMP_PREVIEW(preview), row, x, y, w);
        break;

    case 4:
        for (i = 0, rgb_ptr = rgb; i < w; i++, row += 4, rgb_ptr += 3)
          rgb_ptr[0] = row[0],
          rgb_ptr[1] = row[1],
          rgb_ptr[2] = row[2];

	gimp_preview_draw_row(GIMP_PREVIEW(preview), rgb, x, y, w);
        break;
  }
  g_free(rgb);
}


static void
destripe_rect (int sel_x1, int sel_y1, int sel_x2, int sel_y2, int do_preview)
{
  GPixelRgn src_rgn;	/* source image region */
  GPixelRgn dst_rgn;	/* destination image region */
  guchar *src_rows;	/* image data */
  double progress, progress_inc;
  int sel_width = sel_x2 - sel_x1;
  int sel_height = sel_y2 - sel_y1;
  long *hist, *corr;	/* "histogram" data */
  int tile_width = gimp_tile_width ();
  int i, x, y, ox, cols;
  
  /* initialize */

  progress = 0.0;
  progress_inc = 0.0;

  /*
   * Let the user know what we're doing...
   */

  if (!do_preview)
    {
      gimp_progress_init ("Destriping...");
      
      progress = 0;
      progress_inc = 0.5 * tile_width / sel_width;
    }
  
 /*
  * Setup for filter...
  */

  gimp_pixel_rgn_init(&src_rgn, drawable, sel_x1, sel_y1, sel_width, sel_height, FALSE, FALSE);
  gimp_pixel_rgn_init(&dst_rgn, drawable, sel_x1, sel_y1, sel_width, sel_height, TRUE, TRUE);
  
  hist = g_new (long, sel_width * img_bpp);
  corr = g_new (long, sel_width * img_bpp);
  src_rows = g_malloc (tile_width * sel_height * img_bpp * sizeof (guchar));
  
  memset (hist, 0, sel_width * img_bpp * sizeof (long));
  
  /*
   * collect "histogram" data.
   */
  
  for (ox = sel_x1; ox < sel_x2; ox += tile_width)
    {
      guchar *rows = src_rows;
      
      cols = sel_x2 - ox;
      if (cols > tile_width)
        cols = tile_width;
      
      gimp_pixel_rgn_get_rect (&src_rgn, rows, ox, sel_y1, cols, sel_height);
      
      for (y = 0; y < sel_height; y++)
        {
          long *h = hist + (ox - sel_x1) * img_bpp;
          guchar *row_end = rows + cols * img_bpp;
          
          while (rows < row_end)
            *h++ += *rows++;
        }
      
      if (!do_preview)
        gimp_progress_update (progress += progress_inc);

    }
  
  /*
   * average out histogram
   */
  
  if (1)
    {
      int extend = (avg_width >> 1) * img_bpp;
      
      for (i = 0; i < MIN (3, img_bpp); i++)
        {
          long *h = hist - extend + i;
          long *c = corr - extend + i;
          long sum = 0;
          int cnt = 0;
          
          for (x = -extend; x < sel_width * img_bpp; x += img_bpp)
            {
              if (x + extend <  sel_width * img_bpp) { sum += h[ extend]; cnt++; };
              if (x - extend >=                   0) { sum -= h[-extend]; cnt--; };
              if (x          >=                   0) { *c = ((sum/cnt - *h) << 10) / *h; };
              
              h += img_bpp;
              c += img_bpp;
            }
        }
    }
  else
    {
      for (i = 0; i < MIN (3, img_bpp); i++)
        {
          long *h = hist + i + sel_width * img_bpp - img_bpp;
          long *c = corr + i + sel_width * img_bpp - img_bpp;
          long i = *h;
          *c = 0;
          
          do
            {
              h -= img_bpp;
              c -= img_bpp;
              
              if (*h - i > avg_width && i - *h > avg_width)
                i = *h;
              
              *c = (i-128) << 10 / *h;
            }
          while (h > hist);
          
        }
    }
  
  /*
   * remove stripes.
   */
  
  for (ox = sel_x1; ox < sel_x2; ox += tile_width)
    {
      guchar *rows = src_rows;
      
      cols = sel_x2 - ox;
      if (cols > tile_width)
        cols = tile_width;
      
      gimp_pixel_rgn_get_rect (&src_rgn, rows, ox, sel_y1, cols, sel_height);
      
      if (!do_preview)
        gimp_progress_update (progress += progress_inc);
      
      for (y = 0; y < sel_height; y++)
        {
          long *c = corr + (ox - sel_x1) * img_bpp;
          guchar *row_end = rows + cols * img_bpp;
          
          if (histogram)
            while (rows < row_end)
              {
                *rows = MIN (255, MAX (0, 128 + (*rows * *c >> 10)));
                c++; rows++;
              }
          else
            while (rows < row_end)
              {
                *rows = MIN (255, MAX (0, *rows + (*rows * *c >> 10) ));
                c++; rows++;
              }
          
          if (do_preview)
            preview_draw_row (ox - sel_x1, y, cols, rows - cols * img_bpp);
        }
      
      if (!do_preview)
        {
          gimp_pixel_rgn_set_rect (&dst_rgn, src_rows, ox, sel_y1, cols, sel_height);
          gimp_progress_update (progress += progress_inc);
        }
    }
  
  g_free(src_rows);

  /*
   * Update the screen...
   */
  
  if (!do_preview)
    {
      gimp_drawable_flush (drawable);
      gimp_drawable_merge_shadow (drawable->id, TRUE);
      gimp_drawable_update (drawable->id, sel_x1, sel_y1, sel_width, sel_height);
    }
  g_free (hist);
  g_free (corr);
}

/*
 * 'destripe()' - Destripe an image.
 *
 */

static void
destripe (void)
{
  destripe_rect (sel_x1, sel_y1, sel_x2, sel_y2, 0);
}

static void
dialog_histogram_callback(GtkWidget *widget,	/* I - Toggle button */
                          gpointer  data)	/* I - Data */
{
  histogram = !histogram;
  preview_update ();
}

/*
 * 'destripe_dialog()' - Popup a dialog window for the filter box size...
 */

static gint
destripe_dialog(void)
{
  GtkWidget	*dialog,	/* Dialog window */
		*table,		/* Table "container" for controls */
		*ptable,	/* Preview table */
		*ftable,	/* Filter table */
		*frame,		/* Frame for preview */
		*scrollbar,	/* Horizontal + vertical scroller */
		*button;


 /*
  * Initialize the program's display...
  */

  gtk_init();

 /*
  * Dialog window...
  */

  dialog = gimp_dialog_new("Destripe");
  g_signal_connect(dialog, "destroy",
		   G_CALLBACK(dialog_close_callback),
		   NULL);

 /*
  * Top-level table for dialog...
  */

  table = gimp_table_new(3, 3, FALSE);
  gimp_container_set_border_width(table, 6);
  gtk_grid_set_row_spacing(GTK_GRID(table), 4);
  gimp_box_pack_start(gimp_dialog_get_vbox(dialog), table, FALSE, FALSE, 0);

 /*
  * Preview window...
  */

  ptable = gimp_table_new(2, 2, FALSE);
  gimp_table_attach(table, ptable, 0, 2, 0, 1, 0, 0, 0, 0);

  frame = gtk_frame_new(NULL);
  gimp_table_attach(ptable, frame, 0, 1, 0, 1, 0, 0, 0, 0);

  preview_width  = MIN(sel_x2 - sel_x1, PREVIEW_SIZE);
  preview_height = MIN(sel_y2 - sel_y1, PREVIEW_SIZE);

  preview = gimp_preview_new(GIMP_PREVIEW_COLOR);
  gimp_preview_size(GIMP_PREVIEW(preview), preview_width, preview_height);
  gtk_frame_set_child(GTK_FRAME(frame), preview);

  hscroll_data = gtk_adjustment_new(0, 0, sel_x2 - sel_x1 - 1, 1.0,
				    MIN(preview_width, sel_x2 - sel_x1),
				    MIN(preview_width, sel_x2 - sel_x1));

  g_signal_connect(hscroll_data, "value-changed",
		   G_CALLBACK(preview_scroll_callback), NULL);

  scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL, hscroll_data);
  gimp_table_attach(ptable, scrollbar, 0, 1, 1, 2, GIMP_FILL, 0, 0, 0);

  vscroll_data = gtk_adjustment_new(0, 0, sel_y2 - sel_y1 - 1, 1.0,
				    MIN(preview_height, sel_y2 - sel_y1),
				    MIN(preview_height, sel_y2 - sel_y1));

  g_signal_connect(vscroll_data, "value-changed",
		   G_CALLBACK(preview_scroll_callback), NULL);

  scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, vscroll_data);
  gimp_table_attach(ptable, scrollbar, 1, 2, 0, 1, 0, GIMP_FILL, 0, 0);

  preview_init();

 /*
  * Filter type controls...
  */

  ftable = gimp_table_new(4, 1, FALSE);
  gimp_container_set_border_width(ftable, 4);
  gimp_table_attach(table, ftable, 2, 3, 0, 1, 0, 0, 0, 0);

  button = gtk_check_button_new_with_label("Histogram");
  gimp_table_attach(ftable, button, 0, 1, 0, 1,
		    GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 0, 0);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(button),
                              histogram ? TRUE : FALSE);
  g_signal_connect(button, "toggled",
		   G_CALLBACK(dialog_histogram_callback),
		   NULL);

 /*
  * Box size (radius) control...
  */

  dialog_create_ivalue("Width", table, 2, &avg_width, 2, MAX_AVG);

 /*
  * OK, cancel buttons...
  */

  gimp_container_set_border_width(gimp_dialog_get_action_area(dialog), 6);

  gimp_dialog_add_button(dialog, "OK", G_CALLBACK(dialog_ok_callback),
			 dialog, TRUE);
  gimp_dialog_add_button(dialog, "Cancel", G_CALLBACK(dialog_cancel_callback),
			 dialog, FALSE);

 /*
  * Show it and wait for the user to do something...
  */

  gtk_window_present(GTK_WINDOW(dialog));

  preview_update();

  gimp_main_loop_run();

 /*
  * Free the preview data...
  */

  preview_exit();

 /*
  * Return ok/cancel...
  */

  return (run_filter);
}


/*
 * 'preview_init()' - Initialize the preview window...
 */

static void
preview_init(void)
{
 /*
  * Setup for preview filter...
  */

  preview_x1 = sel_x1;
  preview_y1 = sel_y1;
  preview_x2 = preview_x1 + MIN(preview_width, sel_x2 - sel_x1);
  preview_y2 = preview_y1 + MIN(preview_height, sel_y2 -sel_y1);
}


/*
 * 'preview_scroll_callback()' - Update the preview when a scrollbar is moved.
 */

static void
preview_scroll_callback(void)
{
  preview_x1 = sel_x1 + gtk_adjustment_get_value(hscroll_data);
  preview_y1 = sel_y1 + gtk_adjustment_get_value(vscroll_data);
  preview_x2 = preview_x1 + MIN(preview_width, sel_x2 - sel_x1);
  preview_y2 = preview_y1 + MIN(preview_height, sel_y2 - sel_y1);

  preview_update();
}


/*
 * 'preview_update()' - Update the preview window.
 */

static void
preview_update(void)
{
  destripe_rect (preview_x1, preview_y1, preview_x2, preview_y2, 1);
}


/*
 * 'preview_exit()' - Free all memory used by the preview window...
 */

static void
preview_exit(void)
{
}


/*
 * 'dialog_create_ivalue()' - Create an integer value control...
 */

static void
dialog_create_ivalue(char      *title,	/* I - Label for control */
                     GtkWidget *table,	/* I - Table container to use */
                     int       row,	/* I - Row # for container */
                     gint      *value,	/* I - Value holder */
                     int       left,	/* I - Minimum value for slider */
                     int       right)	/* I - Maximum value for slider */
{
  GtkWidget	*label,		/* Control label */
		*scale,		/* Scale widget */
		*entry;		/* Text widget */
  GtkAdjustment	*scale_data;	/* Scale data */
  char		buf[256];	/* String buffer */


 /*
  * Label...
  */

  label = gtk_label_new(title);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0);
  gtk_label_set_yalign(GTK_LABEL(label), 1.0);
  gimp_table_attach(table, label, 0, 1, row, row + 1, GIMP_FILL, GIMP_FILL, 4, 0);

 /*
  * Scale...
  */

  scale_data = gtk_adjustment_new(*value, left, right, 1.0, 1.0, 1.0);

  g_signal_connect(scale_data, "value-changed",
		   G_CALLBACK(dialog_iscale_update),
		   value);

  scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, scale_data);
  gtk_widget_set_size_request(scale, SCALE_WIDTH, -1);
  gimp_table_attach(table, scale, 1, 2, row, row + 1, GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 0, 0);
  gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);

 /*
  * Text entry...
  */

  entry = gtk_entry_new();
  g_object_set_data(G_OBJECT(entry), "user_data", scale_data);
  g_object_set_data(G_OBJECT(scale_data), "user_data", entry);
  gtk_widget_set_size_request(entry, ENTRY_WIDTH, -1);
  gtk_editable_set_width_chars(GTK_EDITABLE(entry), 4);
  sprintf(buf, "%d", *value);
  gtk_editable_set_text(GTK_EDITABLE(entry), buf);
  g_signal_connect(entry, "changed",
		   G_CALLBACK(dialog_ientry_update),
		   value);
  gimp_table_attach(table, entry, 2, 3, row, row + 1, GIMP_FILL, GIMP_FILL, 4, 0);
}


/*
 * 'dialog_iscale_update()' - Update the value field using the scale.
 */

static void
dialog_iscale_update(GtkAdjustment *adjustment,	/* I - New value */
                     gint          *value)	/* I - Current value */
{
  GtkWidget	*entry;		/* Text entry widget */
  char		buf[256];	/* Text buffer */


  if (*value != gtk_adjustment_get_value(adjustment))
  {
    *value = gtk_adjustment_get_value(adjustment);

    entry = g_object_get_data(G_OBJECT(adjustment), "user_data");
    sprintf(buf, "%d", *value);

    g_signal_handlers_block_matched(entry, G_SIGNAL_MATCH_DATA,
				    0, 0, NULL, NULL, value);
    gtk_editable_set_text(GTK_EDITABLE(entry), buf);
    g_signal_handlers_unblock_matched(entry, G_SIGNAL_MATCH_DATA,
				      0, 0, NULL, NULL, value);

    preview_update();
  };
}


/*
 * 'dialog_ientry_update()' - Update the value field using the text entry.
 */

static void
dialog_ientry_update(GtkWidget *widget,	/* I - Entry widget */
                     gint      *value)	/* I - Current value */
{
  GtkAdjustment	*adjustment;
  gint		new_value;


  new_value = atoi(gtk_editable_get_text(GTK_EDITABLE(widget)));

  if (*value != new_value)
  {
    adjustment = g_object_get_data(G_OBJECT(widget), "user_data");

    if ((new_value >= gtk_adjustment_get_lower(adjustment)) &&
	(new_value <= gtk_adjustment_get_upper(adjustment)))
    {
      *value = new_value;

      gtk_adjustment_set_value(adjustment, new_value);

      preview_update();
    };
  };
}

/*
 * 'dialog_ok_callback()' - Start the filter...
 */

static void
dialog_ok_callback(GtkWidget *widget,	/* I - OK button widget */
                   gpointer  data)	/* I - Dialog window */
{
  run_filter = TRUE;
  gtk_window_destroy(GTK_WINDOW(data));
}


/*
 * 'dialog_cancel_callback()' - Cancel the filter...
 */

static void
dialog_cancel_callback(GtkWidget *widget,	/* I - Cancel button widget */
                       gpointer  data)		/* I - Dialog window */
{
  gtk_window_destroy(GTK_WINDOW(data));
}


/*
 * 'dialog_close_callback()' - Exit the filter dialog application.
 */

static void
dialog_close_callback(GtkWidget *widget,	/* I - Dialog window */
                      gpointer  data)		/* I - Dialog window */
{
  gimp_main_loop_quit();
}

