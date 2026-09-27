/*
 * "$Id$"
 *
 *   System printer support for the Print plug-in for the GIMP.
 *
 *   Copyright 1997-1998 Michael Sweet (mike@easysw.com)
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms of the GNU General Public License as published by the Free
 *   Software Foundation; either version 2 of the License, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *   for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Contents:
 *
 *   gtkprint_query()          - Register the system printing procedures.
 *   gtkprint_run()            - Run "file_print_gtk" or "file_page_setup".
 *   gtkprint_legacy()         - Print for the "System Printer" entry of
 *                               the built-in drivers' dialog.
 *   gtkprint_page_setup()     - Run the system's page setup dialog.
 *   gtkprint_page_setup_done() - The page setup dialog was closed.
 *   gtkprint_job_init()       - Set up a print job.
 *   gtkprint_job_run()        - Run the GtkPrintOperation.
 *   gtkprint_convert()        - Convert the drawable to a cairo surface.
 *   gtkprint_adjust()         - Apply brightness and grayscale to it.
 *   gtkprint_layout()         - Place the image in the printable area.
 *   gtkprint_draw_page()      - Draw the image on the page.
 *   gtkprint_create_widget()  - Create the "Image Settings" tab.
 *   gtkprint_update_widgets() - Show the image settings in the tab.
 *   gtkprint_preview_draw()   - Draw the page preview.
 *
 * gimp42: printing the way applications print now.  File > Print...
 * ("file_print_gtk") opens the system's print dialog directly
 * (GtkPrintOperation: the GTK print dialog with the CUPS and file
 * backends on Linux and macOS, the Windows print dialog on Windows), with
 * an "Image Settings" tab for the size and position of the image,
 * grayscale and brightness, and a preview of the page that follows the
 * paper and orientation chosen in the dialog.  File > Page Setup...
 * ("file_page_setup") runs the system's page setup dialog.  The printer,
 * its options and the page setup are kept in "print-settings" in the
 * user's GIMP directory, the image settings with gimp_set_data().
 *
 * What prints is what the image shows: an image of more than one layer
 * has its visible layers merged in a copy (print_composite()).  When
 * printing starts, the image is converted to a cairo image surface
 * with the drivers' conversion functions (alpha composited over white,
 * indexed images through the colormap, gray stays gray), then the
 * brightness lookup table and grayscale are applied to it.  The preview
 * shows a thumbnail made the same way before the dialog opens.  Sizes
 * and offsets are in points; offsets are from the top-left corner of the
 * printable area (of the paper with "Ignore page margins").
 */

#include "print.h"
#include <math.h>


/*
 * Constants...
 */

#define GTKPRINT_DATA		"file_print_gtk"	/* gimp_set_data() key */
#define GTKPRINT_SETTINGS	"print-settings"	/* Printer & page setup */
#define MAX_SURFACE		32767	/* Largest cairo image surface */
#define THUMB_SIZE		512	/* Largest side of the thumbnail */
#define PREVIEW_SIZE		300	/* Size of the page preview */
#define PREVIEW_PAD		12	/* Space around the paper in it */
#define PPI_MIN			0.1	/* Resolution limits, pixels/inch */
#define PPI_MAX			100000.0
#define BRIGHTNESS_MIN		50	/* Brightness limits, percent */
#define BRIGHTNESS_MAX		200
#define WATCHDOG_TIME		30	/* Seconds to wait for a printer */

#define UNIT_MM			0	/* Units, see units[] */
#define UNIT_CM			1
#define UNIT_INCHES		2
#define UNIT_POINTS		3

#define SPIN_WIDTH		0	/* Spin buttons of the tab */
#define SPIN_HEIGHT		1
#define SPIN_PPI		2
#define SPIN_LEFT		3
#define SPIN_TOP		4
#define NUM_SPINS		5


/*
 * Types...
 */

typedef struct			/**** Unit in the dialog ****/
{
  const char	*name;		/* Name in the unit menu */
  double	points;		/* Points per unit */
  int		digits;		/* Digits shown */
  double	step;		/* Step of the spin buttons */
} gtkprint_unit_t;

typedef struct			/**** Image settings ****/
{
  gint		fit_to_page;	/* Largest size that fits the printable area */
  gdouble	ppi;		/* Resolution when not fitting, pixels/inch */
  gint		unit;		/* Unit shown in the dialog */
  gint		center_x,	/* Center horizontally */
		center_y;	/* Center vertically */
  gdouble	left,		/* Offsets from the printable area, points */
		top;
  gint		full_page;	/* Ignore the page margins */
  gint		grayscale;	/* Print in grayscale */
  gint		brightness;	/* Brightness, percent */
} gtkprint_vals_t;

typedef struct			/**** Print job ****/
{
  gtkprint_vals_t	vals;		/* Image settings */
  GDrawable		*drawable;	/* Drawable to print */
  guchar		*cmap;		/* Colormap (indexed images) */
  int			width,		/* Size of the drawable, pixels */
			height;
  char			*name;		/* Job name */
  cairo_surface_t	*surface,	/* Converted image */
			*thumb_raw,	/* Thumbnail of the image */
			*thumb;		/* ... with brightness/grayscale */
  int			failed;		/* Could not convert the image */
  GtkPrintSettings	*settings;	/* Printer and its options */
  GtkPageSetup		*page_setup;	/* Paper, orientation and margins */
  GtkPrintOperation	*operation;	/* Running print operation */
  guint			watchdog;	/* Timeout waiting for a printer */

  GtkWidget		*preview,	/* "Image Settings" tab: preview */
			*fit,		/* Fit to page */
			*custom,	/* Custom size */
			*unit_menu,	/* Unit drop-down */
			*center_x,	/* Center horizontally */
			*center_y,	/* Center vertically */
			*left_unit,	/* Unit of the left offset */
			*top_unit,	/* Unit of the top offset */
			*full_page,	/* Ignore page margins */
			*grayscale,	/* Print in grayscale */
			*spins[NUM_SPINS];
  GtkAdjustment		*adjs[NUM_SPINS],
			*brightness;
  double		shown[NUM_SPINS];	/* Values given to the spins */
  int			updating;	/* Showing the settings in the tab */
  double		drag_left,	/* Image offsets when a drag starts */
			drag_top;
  int			drag_moved;	/* The drag moved the image */
} gtkprint_job_t;

typedef struct			/**** Page setup dialog result ****/
{
  GtkPageSetup	*page_setup;	/* New page setup, NULL if cancelled */
  int		done;		/* The dialog was closed */
} gtkprint_setup_result_t;

typedef struct			/**** Page in the preview ****/
{
  double	scale,		/* Preview pixels per point */
		paper_x,	/* Paper in the preview, pixels */
		paper_y,
		paper_w,	/* Paper size, points */
		paper_h,
		area_x,		/* Printable area on the paper, points */
		area_y,
		area_w,
		area_h;
} gtkprint_geom_t;


/*
 * Local functions...
 */

static GStatusType	gtkprint_page_setup(GRunModeType run_mode);
static void	gtkprint_page_setup_done(GtkPageSetup *page_setup,
		                         gpointer data);
static void	gtkprint_get_vals(gtkprint_vals_t *vals);
static int	gtkprint_default_unit(void);
static void	gtkprint_load_settings(GtkPrintSettings **settings,
		                       GtkPageSetup **page_setup);
static void	gtkprint_save_settings(GtkPrintSettings *settings,
		                       GtkPageSetup *page_setup);
static void	gtkprint_job_init(gtkprint_job_t *job, gint32 image_ID,
		                  GDrawable *drawable, gtkprint_vals_t *vals);
static GtkPrintOperationResult gtkprint_job_run(gtkprint_job_t *job,
		                  GtkPrintOperationAction action,
		                  const char *filename);
static gboolean	gtkprint_watchdog(gpointer data);
static void	gtkprint_job_free(gtkprint_job_t *job);
static cairo_surface_t *gtkprint_convert(GDrawable *drawable, guchar *cmap,
		                  int max_size, int progress);
static void	gtkprint_store_row(guint32 *pixel, guchar *rgb, int width,
		                   int channels);
static void	gtkprint_adjust(cairo_surface_t *src, cairo_surface_t *dst,
		                int brightness, int grayscale);
static void	gtkprint_make_thumb(gtkprint_job_t *job);
static void	gtkprint_update_thumb(gtkprint_job_t *job);
static void	gtkprint_area(GtkPageSetup *page_setup, int full_page,
		              double *width, double *height);
static void	gtkprint_layout(gtkprint_vals_t *vals, int width, int height,
		                double area_width, double area_height,
		                double *x, double *y, double *w, double *h);
static int	gtkprint_orientation(gtkprint_job_t *job, float scaling);
static void	gtkprint_begin_print(GtkPrintOperation *operation,
		                     GtkPrintContext *context, gpointer data);
static void	gtkprint_draw_page(GtkPrintOperation *operation,
		                   GtkPrintContext *context, gint page_nr,
		                   gpointer data);
static GObject	*gtkprint_create_widget(GtkPrintOperation *operation,
		                        gpointer data);
static void	gtkprint_update_widget(GtkPrintOperation *operation,
		                       GtkWidget *widget,
		                       GtkPageSetup *page_setup,
		                       GtkPrintSettings *settings,
		                       gpointer data);
static void	gtkprint_apply_widget(GtkPrintOperation *operation,
		                      GtkWidget *widget, gpointer data);
static void	gtkprint_widget_destroyed(GtkWidget *widget, gpointer data);
static void	gtkprint_update_widgets(gtkprint_job_t *job);
static void	gtkprint_set_spin(gtkprint_job_t *job, int spin, double value,
		                  double lower, double upper, int digits,
		                  double step, int sensitive);
static void	gtkprint_set_active(GtkWidget *button, int active);
static void	gtkprint_spin_changed(GtkAdjustment *adjustment,
		                      gpointer data);
static void	gtkprint_toggled(GtkCheckButton *button, gpointer data);
static void	gtkprint_unit_changed(GObject *menu, GParamSpec *pspec,
		                      gpointer data);
static void	gtkprint_brightness_changed(GtkAdjustment *adjustment,
		                            gpointer data);
static void	gtkprint_geometry(gtkprint_job_t *job, int width, int height,
		                  gtkprint_geom_t *geom);
static void	gtkprint_preview_draw(GtkDrawingArea *area, cairo_t *cr,
		                      int width, int height, gpointer data);
static void	gtkprint_drag_begin(GtkGestureDrag *gesture, double x,
		                    double y, gpointer data);
static void	gtkprint_drag_update(GtkGestureDrag *gesture, double x,
		                     double y, gpointer data);


/*
 * Globals...
 */

static const gtkprint_unit_t units[] =	/* Units in the dialog */
{
  { "mm",	72.0 / 25.4,	1,	1.0 },
  { "cm",	72.0 / 2.54,	2,	0.1 },
  { "inches",	72.0,		2,	0.1 },
  { "points",	1.0,		0,	1.0 }
};

static const gtkprint_vals_t defaults =	/* Default image settings */
{
  TRUE,			/* Fit to page */
  72.0,			/* 72 pixels per inch when not fitting */
  UNIT_MM,		/* Unit (inches where the paper is Letter) */
  TRUE,			/* Center horizontally */
  TRUE,			/* Center vertically */
  0.0,			/* Left offset */
  0.0,			/* Top offset */
  FALSE,		/* Keep the page margins */
  FALSE,		/* Print in color */
  100			/* Brightness */
};


/*
 * 'gtkprint_query()' - Register the system printing procedures.
 */

void
gtkprint_query(void)
{
  static GParamDef	print_args[] =
  {
    { PARAM_INT32,	"run_mode",	"Interactive, non-interactive" },
    { PARAM_IMAGE,	"image",	"Input image" },
    { PARAM_DRAWABLE,	"drawable",	"Input drawable" },
    { PARAM_STRING,	"output_file",	"PDF file to write; empty to print to the last used (or default) printer" },
    { PARAM_INT32,	"fit_to_page",	"Print as large as fits the printable area, keeping the aspect ratio (TRUE, FALSE)" },
    { PARAM_FLOAT,	"width_mm",	"Printed width in mm when not fitting (<= 0: from height_mm and the aspect ratio)" },
    { PARAM_FLOAT,	"height_mm",	"Printed height in mm when not fitting (<= 0: from width_mm and the aspect ratio; with both, the image fits in width_mm x height_mm; with neither, 72 pixels per inch)" },
    { PARAM_INT32,	"center",	"Center the image on the page (TRUE, FALSE)" },
    { PARAM_FLOAT,	"left_mm",	"Left offset from the printable area in mm, when not centered" },
    { PARAM_FLOAT,	"top_mm",	"Top offset from the printable area in mm, when not centered" },
    { PARAM_INT32,	"grayscale",	"Print in grayscale (TRUE, FALSE)" },
    { PARAM_INT32,	"brightness",	"Brightness in percent (50-200, 100 = unchanged)" }
  };
  static GParamDef	setup_args[] =
  {
    { PARAM_INT32,	"run_mode",	"Interactive" },
    { PARAM_IMAGE,	"image",	"Input image (unused)" },
    { PARAM_DRAWABLE,	"drawable",	"Input drawable (unused)" }
  };


  gimp_install_procedure(
      "file_print_gtk",
      "Prints the image with the system's print dialog.",
      "Prints the image as it is shown (its visible layers merged) "
      "through the system's printers.  Interactively it opens the "
      "native print dialog, with an \"Image Settings\" tab for the size and "
      "position of the image, grayscale and brightness.  Non-interactively "
      "it prints to the last used (or the default) printer, or writes a PDF "
      "file when output_file is given.  The paper and orientation are the "
      "ones last chosen in File > Page Setup or in the print dialog.",
      "Michael Sweet <mike@easysw.com>, gimp42",
      "Copyright 1997-1998 by Michael Sweet",
      "2026",
      "<Image>/File/Print...",
      "RGB*,GRAY*,INDEXED*",
      PROC_PLUG_IN,
      sizeof(print_args) / sizeof(print_args[0]),
      0,
      print_args,
      NULL);

  gimp_install_procedure(
      "file_page_setup",
      "Sets up the page for printing.",
      "Chooses the paper size, orientation and margins that File > Print "
      "uses, with the system's page setup dialog.",
      "Michael Sweet <mike@easysw.com>, gimp42",
      "Copyright 1997-1998 by Michael Sweet",
      "2026",
      "<Image>/File/Page Setup...",
      "RGB*,GRAY*,INDEXED*",
      PROC_PLUG_IN,
      sizeof(setup_args) / sizeof(setup_args[0]),
      0,
      setup_args,
      NULL);
}


/*
 * 'gtkprint_run()' - Run "file_print_gtk" or "file_page_setup".
 */

void
gtkprint_run(char   *name,		/* I - Name of the procedure */
             int    nparams,		/* I - Number of parameters */
             GParam *param,		/* I - Parameter values */
             int    *nreturn_vals,	/* O - Number of return values */
             GParam **return_vals)	/* O - Return values */
{
  GParam		*values;	/* Return values */
  GRunModeType		run_mode;	/* Current run mode */
  GStatusType		status;		/* Result */
  GDrawable		*drawable;	/* Drawable to print */
  gint32		copy_ID;	/* Copy of image with layers merged */
  GtkPrintOperationAction action;	/* Dialog, printer or PDF file */
  GtkPrintOperationResult result;	/* Result of the print operation */
  gtkprint_vals_t	vals;		/* Image settings */
  gtkprint_job_t	job;		/* Print job */
  char			*filename;	/* PDF file */
  double		width,		/* Size asked for, points */
			height;


  run_mode = param[0].data.d_int32;
  status   = STATUS_SUCCESS;

  values = g_new(GParam, 1);
  values[0].type = PARAM_STATUS;

  *nreturn_vals = 1;
  *return_vals  = values;

  if (strcmp(name, "file_page_setup") == 0)
  {
    values[0].data.d_status = gtkprint_page_setup(run_mode);
    return;
  };

 /*
  * Print what the image shows: its layers merged in a copy when it has
  * more than one...
  */

  gtkprint_get_vals(&vals);

  drawable = gimp_drawable_get(print_composite(param[1].data.d_image,
                                               param[2].data.d_drawable,
                                               &copy_ID));
  filename = NULL;
  action   = GTK_PRINT_OPERATION_ACTION_PRINT;

  switch (run_mode)
  {
    case RUN_INTERACTIVE :
        action = GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG;
        break;

    case RUN_NONINTERACTIVE :
        if (nparams < 12)
        {
          status = STATUS_CALLING_ERROR;
          break;
        };

        filename = param[3].data.d_string;
        if (filename != NULL && filename[0] != '\0')
          action = GTK_PRINT_OPERATION_ACTION_EXPORT;

        vals.fit_to_page = param[4].data.d_int32 != 0;
        width            = param[5].data.d_float * 72.0 / 25.4;
        height           = param[6].data.d_float * 72.0 / 25.4;

       /*
        * The resolution follows from the width or the height; with both,
        * the image fits in width x height.
        */

        if (width > 0.0 && height > 0.0)
          vals.ppi = MAX(drawable->width * 72.0 / width,
                         drawable->height * 72.0 / height);
        else if (width > 0.0)
          vals.ppi = drawable->width * 72.0 / width;
        else if (height > 0.0)
          vals.ppi = drawable->height * 72.0 / height;
        else
          vals.ppi = defaults.ppi;

        vals.ppi        = CLAMP(vals.ppi, PPI_MIN, PPI_MAX);
        vals.center_x   = param[7].data.d_int32 != 0;
        vals.center_y   = vals.center_x;
        vals.left       = param[8].data.d_float * 72.0 / 25.4;
        vals.top        = param[9].data.d_float * 72.0 / 25.4;
        vals.full_page  = FALSE;
        vals.grayscale  = param[10].data.d_int32 != 0;
        vals.brightness = CLAMP(param[11].data.d_int32, BRIGHTNESS_MIN,
                                BRIGHTNESS_MAX);
        break;

    case RUN_WITH_LAST_VALS :
        break;

    default :
        status = STATUS_CALLING_ERROR;
        break;
  };

  if (status == STATUS_SUCCESS)
  {
   /*
    * The dialog needs a display; printing and writing a PDF file work
    * without one.
    */

    if (run_mode == RUN_INTERACTIVE)
      gtk_init();
    else
      gtk_init_check();

    gtkprint_job_init(&job, param[1].data.d_image, drawable, &vals);

    result = gtkprint_job_run(&job, action, filename);

    if (result == GTK_PRINT_OPERATION_RESULT_ERROR)
      status = STATUS_EXECUTION_ERROR;
    else if (result == GTK_PRINT_OPERATION_RESULT_CANCEL)
      status = STATUS_CANCEL;
    else if (result == GTK_PRINT_OPERATION_RESULT_APPLY &&
             run_mode == RUN_INTERACTIVE)
    {
      gtkprint_save_settings(job.settings, job.page_setup);
      gimp_set_data(GTKPRINT_DATA, &job.vals, sizeof(job.vals));
    };

    gtkprint_job_free(&job);
  };

  gimp_drawable_detach(drawable);
  print_composite_done(copy_ID);

  values[0].data.d_status = status;
}


/*
 * 'gtkprint_legacy()' - Print for the "System Printer" entry of the
 *                       built-in drivers' dialog.
 *
 * The old dialog's settings become image settings for the system print
 * dialog: its media size and orientation pick the paper, a scaling in
 * percent of the printable area becomes the matching resolution.  When
 * the image was printed, the image settings go back to the old dialog.
 */

GStatusType
gtkprint_legacy(gint32    image_ID,	/* I - Image */
                GDrawable *drawable,	/* I - Drawable to print */
                int       show_dialog,	/* I - Show the system print dialog */
                char      *media_size,	/* I - Media size name */
                int       media_width,	/* I - Media width, points */
                int       media_length,	/* I - Media length, points */
                int       orientation,	/* I - Orientation of image */
                int       *output_type,	/* IO - Color or grayscale */
                int       *brightness,	/* IO - Brightness, percent */
                float     *scaling,	/* IO - Percent of page, or -PPI */
                int       *left,	/* IO - Left offset, points or -1 */
                int       *top)		/* IO - Top offset, points or -1 */
{
  gtkprint_job_t	job;		/* Print job */
  gtkprint_vals_t	vals;		/* Image settings */
  GtkPaperSize		*paper;		/* Paper of the old dialog */
  GtkPrintOperationResult result;	/* Result of the print operation */
  double		area_width,	/* Printable area, points */
			area_height,
			x, y, w, h;	/* Image on the page, points */


  if (show_dialog)
    gtk_init();
  else
    gtk_init_check();

  gtkprint_get_vals(&vals);

  vals.fit_to_page = *scaling >= 0.0;
  if (*scaling < 0.0)
    vals.ppi = CLAMP(-*scaling, PPI_MIN, PPI_MAX);
  vals.center_x    = *left < 0;
  vals.center_y    = *top < 0;
  vals.left        = MAX(*left, 0);
  vals.top         = MAX(*top, 0);
  vals.full_page   = FALSE;
  vals.grayscale   = *output_type == OUTPUT_GRAY;
  vals.brightness  = CLAMP(*brightness, BRIGHTNESS_MIN, BRIGHTNESS_MAX);

  gtkprint_job_init(&job, image_ID, drawable, &vals);

  if (media_size != NULL && media_size[0] != '\0' &&
      media_width > 0 && media_length > 0)
  {
    paper = gtk_paper_size_new_from_ppd(media_size, media_size,
                                        media_width, media_length);
    gtk_page_setup_set_paper_size_and_default_margins(job.page_setup, paper);
    gtk_paper_size_free(paper);
  };

  if (orientation == ORIENT_AUTO)
    orientation = gtkprint_orientation(&job, *scaling);

  gtk_page_setup_set_orientation(job.page_setup,
                                 orientation == ORIENT_LANDSCAPE ?
                                     GTK_PAGE_ORIENTATION_LANDSCAPE :
                                     GTK_PAGE_ORIENTATION_PORTRAIT);

  if (*scaling > 0.0 && *scaling < 100.0)
  {
    gtkprint_area(job.page_setup, FALSE, &area_width, &area_height);
    gtkprint_layout(&job.vals, job.width, job.height, area_width,
                    area_height, &x, &y, &w, &h);

    job.vals.fit_to_page = FALSE;
    job.vals.ppi         = CLAMP(job.width * 72.0 / (w * *scaling / 100.0),
                                 PPI_MIN, PPI_MAX);
  };

  result = gtkprint_job_run(&job, show_dialog ?
                                      GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG :
                                      GTK_PRINT_OPERATION_ACTION_PRINT, NULL);

  if (result == GTK_PRINT_OPERATION_RESULT_APPLY)
  {
    if (show_dialog)
      gtkprint_save_settings(job.settings, job.page_setup);

    *output_type = job.vals.grayscale ? OUTPUT_GRAY : OUTPUT_COLOR;
    *brightness  = job.vals.brightness;
    *scaling     = job.vals.fit_to_page ? 100.0 : -job.vals.ppi;
    *left        = job.vals.center_x ? -1 : (int)(job.vals.left + 0.5);
    *top         = job.vals.center_y ? -1 : (int)(job.vals.top + 0.5);
  };

  gtkprint_job_free(&job);

  if (result == GTK_PRINT_OPERATION_RESULT_ERROR)
    return (STATUS_EXECUTION_ERROR);
  else if (result == GTK_PRINT_OPERATION_RESULT_CANCEL)
    return (STATUS_CANCEL);
  else
    return (STATUS_SUCCESS);
}


/*
 * 'gtkprint_page_setup()' - Run the system's page setup dialog.
 */

static GStatusType
gtkprint_page_setup(GRunModeType run_mode)	/* I - Run mode */
{
  GtkPrintSettings	*settings;	/* Printer and its options */
  GtkPageSetup		*page_setup;	/* Current page setup */
  gtkprint_setup_result_t result;	/* What the dialog gave back */


  if (run_mode == RUN_NONINTERACTIVE)
    return (STATUS_CALLING_ERROR);

  gtk_init();

  gtkprint_load_settings(&settings, &page_setup);

 /*
  * The asynchronous dialog says when it was cancelled (no page setup);
  * the Windows one cannot, and gives back the page setup unchanged.  It
  * may also be done before it returns...
  */

  result.page_setup = NULL;
  result.done       = FALSE;

  gtk_print_run_page_setup_dialog_async(NULL, page_setup, settings,
                                        gtkprint_page_setup_done, &result);

  while (!result.done)
    g_main_context_iteration(NULL, TRUE);

  if (result.page_setup != NULL)
    gtkprint_save_settings(settings, result.page_setup);

  g_object_unref(page_setup);
  g_object_unref(settings);

  if (result.page_setup == NULL)
    return (STATUS_CANCEL);

  g_object_unref(result.page_setup);

  return (STATUS_SUCCESS);
}


/*
 * 'gtkprint_page_setup_done()' - The page setup dialog was closed.
 */

static void
gtkprint_page_setup_done(GtkPageSetup *page_setup,	/* I - New page setup */
                         gpointer     data)		/* I - Result */
{
  gtkprint_setup_result_t *result = data;


  if (page_setup != NULL)
    result->page_setup = gtk_page_setup_copy(page_setup);

  result->done = TRUE;
}


/*
 * 'gtkprint_get_vals()' - Get the image settings of the last print.
 */

static void
gtkprint_get_vals(gtkprint_vals_t *vals)	/* O - Image settings */
{
  GParam	*return_vals;		/* Stored data */
  int		nreturn_vals;		/* Number of return values */


  *vals      = defaults;
  vals->unit = gtkprint_default_unit();

  return_vals = gimp_run_procedure("gimp_procedural_db_get_data",
                                   &nreturn_vals,
                                   PARAM_STRING, GTKPRINT_DATA,
                                   PARAM_END);

  if (nreturn_vals >= 3 &&
      return_vals[0].data.d_status == STATUS_SUCCESS &&
      return_vals[1].data.d_int32 == sizeof(gtkprint_vals_t))
    memcpy(vals, return_vals[2].data.d_int8array, sizeof(gtkprint_vals_t));

  gimp_destroy_params(return_vals, nreturn_vals);

  vals->ppi        = CLAMP(vals->ppi, PPI_MIN, PPI_MAX);
  vals->unit       = CLAMP(vals->unit, 0, (int)G_N_ELEMENTS(units) - 1);
  vals->brightness = CLAMP(vals->brightness, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
}


/*
 * 'gtkprint_default_unit()' - Inches where the paper is Letter, else mm.
 */

static int
gtkprint_default_unit(void)
{
  const char	*paper = gtk_paper_size_get_default();


  if (strcmp(paper, GTK_PAPER_NAME_LETTER) == 0 ||
      strcmp(paper, GTK_PAPER_NAME_LEGAL) == 0)
    return (UNIT_INCHES);
  else
    return (UNIT_MM);
}


/*
 * 'gtkprint_load_settings()' - Load the printer and page setup of the last
 *                              print (unref them).
 */

static void
gtkprint_load_settings(GtkPrintSettings **settings,	/* O - Printer */
                       GtkPageSetup     **page_setup)	/* O - Page setup */
{
  GKeyFile	*key_file;		/* print-settings */
  char		*filename;		/* Name of print-settings */


  *settings   = NULL;
  *page_setup = NULL;

  filename = print_user_filename(GTKPRINT_SETTINGS);
  key_file = g_key_file_new();

  if (g_key_file_load_from_file(key_file, filename, G_KEY_FILE_NONE, NULL))
  {
    *settings   = gtk_print_settings_new_from_key_file(key_file, NULL, NULL);
    *page_setup = gtk_page_setup_new_from_key_file(key_file, NULL, NULL);
  };

  g_key_file_free(key_file);
  g_free(filename);

  if (*settings == NULL)
    *settings = gtk_print_settings_new();

  if (*page_setup == NULL)
    *page_setup = gtk_page_setup_new();
}


/*
 * 'gtkprint_save_settings()' - Save the printer and page setup for the
 *                              next print.
 */

static void
gtkprint_save_settings(GtkPrintSettings *settings,	/* I - Printer */
                       GtkPageSetup     *page_setup)	/* I - Page setup */
{
  GKeyFile	*key_file;		/* print-settings */
  char		*filename;		/* Name of print-settings */
  GError	*error = NULL;


  filename = print_user_filename(GTKPRINT_SETTINGS);
  key_file = g_key_file_new();

  if (settings != NULL)
    gtk_print_settings_to_key_file(settings, key_file, NULL);
  if (page_setup != NULL)
    gtk_page_setup_to_key_file(page_setup, key_file, NULL);

  if (!g_key_file_save_to_file(key_file, filename, &error))
  {
    g_message("Print: %s", error->message);
    g_error_free(error);
  };

  g_key_file_free(key_file);
  g_free(filename);
}


/*
 * 'gtkprint_job_init()' - Set up a print job.
 */

static void
gtkprint_job_init(gtkprint_job_t  *job,		/* O - Print job */
                  gint32          image_ID,	/* I - Image */
                  GDrawable       *drawable,	/* I - Drawable to print */
                  gtkprint_vals_t *vals)	/* I - Image settings */
{
  GDrawableType	type;			/* Type of drawable */
  char		*filename;		/* Image file */


  memset(job, 0, sizeof(*job));

  job->vals     = *vals;
  job->drawable = drawable;
  job->width    = drawable->width;
  job->height   = drawable->height;

  gtkprint_load_settings(&job->settings, &job->page_setup);

  filename = gimp_image_get_filename(image_ID);
  if (filename != NULL && filename[0] != '\0')
    job->name = g_path_get_basename(filename);
  else
    job->name = g_strdup("GIMP");
  g_free(filename);

  type = gimp_drawable_type(drawable->id);
  if (type == INDEXED_IMAGE || type == INDEXEDA_IMAGE)
    job->cmap = print_get_cmap(image_ID);

  gimp_tile_cache_ntiles((drawable->width + gimp_tile_width() - 1) /
                         gimp_tile_width() + 1);
}


/*
 * 'gtkprint_job_run()' - Run the GtkPrintOperation.
 */

static GtkPrintOperationResult
gtkprint_job_run(gtkprint_job_t          *job,	/* I - Print job */
                 GtkPrintOperationAction action,	/* I - What to do */
                 const char              *filename)	/* I - PDF file */
{
  GtkPrintOperation	*operation;	/* Print operation */
  GtkPrintOperationResult result;	/* Result */
  GError		*error = NULL;
  const char		*uri,		/* Last "Print to File" file */
			*format;	/* ... and its format */
  char			*last_file;	/* ... as a file name */


 /*
  * Without the dialog, GTK prints to the last printer or the default
  * one, but it only looks among real printers: when the last print went
  * to a PDF file ("Print to File"), write that file again.  Waiting for
  * the printers never ends when a backend cannot reach its server (CUPS
  * not running), so a watchdog gives up after a while.
  */

  last_file = NULL;

  if (action == GTK_PRINT_OPERATION_ACTION_PRINT)
  {
    uri    = gtk_print_settings_get(job->settings,
                                    GTK_PRINT_SETTINGS_OUTPUT_URI);
    format = gtk_print_settings_get(job->settings,
                                    GTK_PRINT_SETTINGS_OUTPUT_FILE_FORMAT);

    if (uri != NULL && (format == NULL || strcmp(format, "pdf") == 0) &&
        (last_file = g_filename_from_uri(uri, NULL, NULL)) != NULL)
    {
      action   = GTK_PRINT_OPERATION_ACTION_EXPORT;
      filename = last_file;
    }
    else
      job->watchdog = g_timeout_add_seconds(WATCHDOG_TIME, gtkprint_watchdog,
                                            job);
  };

  job->operation = operation = gtk_print_operation_new();

  gtk_print_operation_set_job_name(operation, job->name);
  gtk_print_operation_set_n_pages(operation, 1);
  gtk_print_operation_set_unit(operation, GTK_UNIT_POINTS);
  gtk_print_operation_set_use_full_page(operation, job->vals.full_page);
  gtk_print_operation_set_embed_page_setup(operation, TRUE);
  gtk_print_operation_set_print_settings(operation, job->settings);
  gtk_print_operation_set_default_page_setup(operation, job->page_setup);

  if (action == GTK_PRINT_OPERATION_ACTION_EXPORT)
    gtk_print_operation_set_export_filename(operation, filename);

  if (action == GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG)
  {
    gtkprint_make_thumb(job);

    gtk_print_operation_set_custom_tab_label(operation, "Image Settings");
    g_signal_connect(operation, "create-custom-widget",
                     G_CALLBACK(gtkprint_create_widget), job);
    g_signal_connect(operation, "update-custom-widget",
                     G_CALLBACK(gtkprint_update_widget), job);
    g_signal_connect(operation, "custom-widget-apply",
                     G_CALLBACK(gtkprint_apply_widget), job);
  };

  g_signal_connect(operation, "begin-print",
                   G_CALLBACK(gtkprint_begin_print), job);
  g_signal_connect(operation, "draw-page",
                   G_CALLBACK(gtkprint_draw_page), job);

  result = gtk_print_operation_run(operation, action, NULL, &error);

  if (job->watchdog != 0)
  {
    g_source_remove(job->watchdog);
    job->watchdog = 0;
  };

  if (job->failed)
  {
    g_message("Print: there is not enough memory to print this image.");
    result = GTK_PRINT_OPERATION_RESULT_ERROR;
  }
  else if (result == GTK_PRINT_OPERATION_RESULT_CANCEL &&
           action == GTK_PRINT_OPERATION_ACTION_PRINT)
  {
    g_message("Print: no printer was found.");
    result = GTK_PRINT_OPERATION_RESULT_ERROR;
  }
  else if (result == GTK_PRINT_OPERATION_RESULT_ERROR)
  {
    g_message("Print: %s", error != NULL ? error->message : "printing failed");
    g_clear_error(&error);
  }
  else if (result == GTK_PRINT_OPERATION_RESULT_APPLY &&
           action != GTK_PRINT_OPERATION_ACTION_EXPORT)
  {
   /*
    * Keep the printer and the page setup the image was printed with...
    */

    if (gtk_print_operation_get_print_settings(operation) != NULL)
      g_set_object(&job->settings,
                   gtk_print_operation_get_print_settings(operation));
    if (gtk_print_operation_get_default_page_setup(operation) != NULL)
      g_set_object(&job->page_setup,
                   gtk_print_operation_get_default_page_setup(operation));
  };

  job->operation = NULL;
  g_object_unref(operation);
  g_free(last_file);

  return (result);
}


/*
 * 'gtkprint_watchdog()' - Give up waiting for a printer.
 *
 * GTK is still waiting in its own main loop: the plug-in quits, and the
 * procedure fails.
 */

static gboolean
gtkprint_watchdog(gpointer data)	/* I - Print job */
{
  g_message("Print: no printer answered.  Is the printing system "
            "(CUPS) running?");

  gimp_quit();

  return (G_SOURCE_REMOVE);
}


/*
 * 'gtkprint_job_free()' - Free a print job.
 */

static void
gtkprint_job_free(gtkprint_job_t *job)	/* I - Print job */
{
  if (job->surface != NULL)
    cairo_surface_destroy(job->surface);
  if (job->thumb_raw != NULL)
    cairo_surface_destroy(job->thumb_raw);
  if (job->thumb != NULL)
    cairo_surface_destroy(job->thumb);

  g_clear_object(&job->settings);
  g_clear_object(&job->page_setup);
  g_free(job->name);
  g_free(job->cmap);
}


/*
 * 'gtkprint_convert()' - Convert the drawable to a cairo image surface.
 *
 * The drivers' conversion functions composite alpha over white, look
 * indexed pixels up in the colormap and keep gray pixels gray (R = G = B,
 * which cairo's PDF output writes as a gray image); the identity lookup
 * table leaves brightness to gtkprint_adjust().  An image larger than
 * max_size (for printing, cairo's limit of 32767 pixels on a side) is
 * scaled down while it is read, each pixel of the surface the average of
 * the image pixels it covers.
 */

static cairo_surface_t *
gtkprint_convert(GDrawable *drawable,	/* I - Image to print */
                 guchar    *cmap,	/* I - Colormap (indexed images) */
                 int       max_size,	/* I - Largest side of surface */
                 int       progress)	/* I - Show the progress */
{
  cairo_surface_t *surface;		/* Converted image */
  GPixelRgn	rgn;			/* Drawable pixels */
  convert_t	colorfunc;		/* Conversion function */
  guchar	lut[256],		/* Identity lookup table */
		*in,			/* Row of the drawable */
		*out,			/* Converted row */
		*avg,			/* Averaged row (scaled images) */
		*data;			/* Surface pixels */
  guint64	*sums,			/* Sums of pixels (scaled images) */
		count;			/* Number of pixels in a sum */
  int		*xmap,			/* Surface column of each column */
		*xcount,		/* Image columns in each column */
		channels,		/* 3 for color, 1 for gray */
		width, height,		/* Size of the drawable */
		out_width,		/* Size of the surface */
		out_height,
		stride,			/* Bytes per surface row */
		scaled,			/* Surface smaller than drawable */
		x, y, c,
		row,			/* Surface row being summed */
		rows;			/* Image rows in it so far */
  double	scale;			/* Scaling to fit max_size */


  width  = drawable->width;
  height = drawable->height;

  if (cmap != NULL)
  {
    colorfunc = indexed_to_rgb;
    channels  = 3;
  }
  else if (drawable->bpp >= 3)
  {
    colorfunc = rgb_to_rgb;
    channels  = 3;
  }
  else
  {
    colorfunc = gray_to_gray;
    channels  = 1;
  };

  for (x = 0; x < 256; x ++)
    lut[x] = x;

  out_width  = width;
  out_height = height;

  if (width > max_size || height > max_size)
  {
    scale      = MIN((double)max_size / width, (double)max_size / height);
    out_width  = CLAMP((int)(width * scale + 0.5), 1, max_size);
    out_height = CLAMP((int)(height * scale + 0.5), 1, max_size);
  };

 /*
  * If there is not enough memory for the surface, halve it...
  */

  surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, out_width,
                                       out_height);

  while (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS &&
         (out_width > 1 || out_height > 1))
  {
    cairo_surface_destroy(surface);

    out_width  = MAX(out_width / 2, 1);
    out_height = MAX(out_height / 2, 1);
    surface    = cairo_image_surface_create(CAIRO_FORMAT_RGB24, out_width,
                                            out_height);
  };

  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
  {
    cairo_surface_destroy(surface);
    return (NULL);
  };

  gimp_pixel_rgn_init(&rgn, drawable, 0, 0, width, height, FALSE, FALSE);

  in     = g_malloc((gsize)width * drawable->bpp);
  out    = g_malloc((gsize)width * channels);
  data   = cairo_image_surface_get_data(surface);
  stride = cairo_image_surface_get_stride(surface);
  scaled = out_width != width || out_height != height;

  avg    = NULL;
  sums   = NULL;
  xmap   = NULL;
  xcount = NULL;

  if (scaled)
  {
    avg    = g_malloc((gsize)out_width * channels);
    sums   = g_new0(guint64, (gsize)out_width * channels);
    xmap   = g_new(int, width);
    xcount = g_new0(int, out_width);

    for (x = 0; x < width; x ++)
    {
      xmap[x] = (int)((gint64)x * out_width / width);
      xcount[xmap[x]] ++;
    };
  };

  cairo_surface_flush(surface);

  for (y = 0, row = 0, rows = 0; y < height; y ++)
  {
    if (progress && (y & 31) == 0)
      gimp_progress_update((double)y / (double)height);

    gimp_pixel_rgn_get_row(&rgn, in, 0, y, width);
    (*colorfunc)(in, out, width, drawable->bpp, lut, cmap);

    if (!scaled)
    {
      gtkprint_store_row((guint32 *)(data + (gsize)y * stride), out, width,
                         channels);
      continue;
    };

   /*
    * Sum the pixels of each surface row, and store the average when the
    * next surface row starts...
    */

    for (x = 0; x < width; x ++)
      for (c = 0; c < channels; c ++)
        sums[xmap[x] * channels + c] += out[x * channels + c];

    rows ++;

    if (y == height - 1 || (gint64)(y + 1) * out_height / height != row)
    {
      for (x = 0; x < out_width; x ++)
      {
        count = (guint64)xcount[x] * rows;

        for (c = 0; c < channels; c ++)
          avg[x * channels + c] = (sums[x * channels + c] + count / 2) / count;
      };

      gtkprint_store_row((guint32 *)(data + (gsize)row * stride), avg,
                         out_width, channels);

      memset(sums, 0, (gsize)out_width * channels * sizeof(guint64));
      row ++;
      rows = 0;
    };
  };

  cairo_surface_mark_dirty(surface);

  g_free(in);
  g_free(out);
  g_free(avg);
  g_free(sums);
  g_free(xmap);
  g_free(xcount);

  return (surface);
}


/*
 * 'gtkprint_store_row()' - Store a converted row in a cairo RGB24 surface.
 */

static void
gtkprint_store_row(guint32 *pixel,	/* O - Row of the surface */
                   guchar  *rgb,	/* I - Converted pixels */
                   int     width,	/* I - Number of pixels */
                   int     channels)	/* I - 3 for color, 1 for gray */
{
  int	x;				/* Looping var */


  if (channels == 3)
    for (x = 0; x < width; x ++, rgb += 3)
      pixel[x] = ((guint32)rgb[0] << 16) | ((guint32)rgb[1] << 8) | rgb[2];
  else
    for (x = 0; x < width; x ++)
      pixel[x] = (guint32)rgb[x] * 0x010101;
}


/*
 * 'gtkprint_adjust()' - Apply brightness and grayscale to a converted image.
 *
 * The brightness lookup table is the drivers' (compute_lut()), without
 * their screen gamma and printer calibration: the system's printer
 * drivers calibrate the printer, so at 100% the colors are printed as
 * they are.  src and dst have the same size and may be the same surface.
 */

static void
gtkprint_adjust(cairo_surface_t *src,	/* I - Converted image */
                cairo_surface_t *dst,	/* O - Adjusted image */
                int             brightness,	/* I - Brightness, percent */
                int             grayscale)	/* I - Convert to grayscale */
{
  guchar	lut[256],		/* Brightness lookup table */
		*src_data,		/* Pixels */
		*dst_data;
  guint32	*sp,			/* Row of src */
		*dp,			/* Row of dst */
		r, g, b, v;		/* Pixel */
  int		x, y,
		width, height,
		src_stride,
		dst_stride;


  cairo_surface_flush(src);
  cairo_surface_flush(dst);

  width      = cairo_image_surface_get_width(src);
  height     = cairo_image_surface_get_height(src);
  src_data   = cairo_image_surface_get_data(src);
  dst_data   = cairo_image_surface_get_data(dst);
  src_stride = cairo_image_surface_get_stride(src);
  dst_stride = cairo_image_surface_get_stride(dst);

  compute_lut(lut, brightness, 1.0, 1.0, 1.0);

  for (y = 0; y < height; y ++)
  {
    sp = (guint32 *)(src_data + (gsize)y * src_stride);
    dp = (guint32 *)(dst_data + (gsize)y * dst_stride);

    for (x = 0; x < width; x ++)
    {
      r = (sp[x] >> 16) & 255;
      g = (sp[x] >> 8) & 255;
      b = sp[x] & 255;

      if (grayscale)
      {
        v     = lut[(r * LUM_RED + g * LUM_GREEN + b * LUM_BLUE) / 100];
        dp[x] = v * 0x010101;
      }
      else
        dp[x] = ((guint32)lut[r] << 16) | ((guint32)lut[g] << 8) | lut[b];
    };
  };

  cairo_surface_mark_dirty(dst);
}


/*
 * 'gtkprint_make_thumb()' - Make the thumbnail for the preview.
 */

static void
gtkprint_make_thumb(gtkprint_job_t *job)	/* I - Print job */
{
  job->thumb_raw = gtkprint_convert(job->drawable, job->cmap, THUMB_SIZE,
                                    FALSE);

  if (job->thumb_raw != NULL)
    job->thumb = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                     cairo_image_surface_get_width(job->thumb_raw),
                     cairo_image_surface_get_height(job->thumb_raw));

  gtkprint_update_thumb(job);
}


/*
 * 'gtkprint_update_thumb()' - Show brightness and grayscale in the
 *                             thumbnail.
 */

static void
gtkprint_update_thumb(gtkprint_job_t *job)	/* I - Print job */
{
  if (job->thumb != NULL)
    gtkprint_adjust(job->thumb_raw, job->thumb, job->vals.brightness,
                    job->vals.grayscale);

  if (job->preview != NULL)
    gtk_widget_queue_draw(job->preview);
}


/*
 * 'gtkprint_area()' - Get the size of the printable area.
 *
 * These are the sizes GtkPrintContext gives the page when it is printed,
 * in its orientation.
 */

static void
gtkprint_area(GtkPageSetup *page_setup,	/* I - Page setup */
              int          full_page,	/* I - Ignore the page margins */
              double       *width,	/* O - Width, points */
              double       *height)	/* O - Height, points */
{
  if (full_page)
  {
    *width  = gtk_page_setup_get_paper_width(page_setup, GTK_UNIT_POINTS);
    *height = gtk_page_setup_get_paper_height(page_setup, GTK_UNIT_POINTS);
  }
  else
  {
    *width  = gtk_page_setup_get_page_width(page_setup, GTK_UNIT_POINTS);
    *height = gtk_page_setup_get_page_height(page_setup, GTK_UNIT_POINTS);
  };

  *width  = MAX(*width, 1.0);
  *height = MAX(*height, 1.0);
}


/*
 * 'gtkprint_layout()' - Place the image in the printable area.
 *
 * An offset keeps the image on the page: from 0 to the space left by the
 * image or, when the image is larger than the area, from that (negative)
 * space to 0, which moves the part of the image that is printed.
 */

static void
gtkprint_layout(gtkprint_vals_t *vals,	/* I - Image settings */
                int             width,	/* I - Size of image, pixels */
                int             height,
                double          area_width,	/* I - Printable area, points */
                double          area_height,
                double          *x,	/* O - Position of image, points */
                double          *y,
                double          *w,	/* O - Size of image, points */
                double          *h)
{
  double	space;			/* Room left by the image */


  if (vals->fit_to_page)
  {
    *w = area_width;
    *h = area_width * height / width;

    if (*h > area_height)
    {
      *h = area_height;
      *w = area_height * width / height;
    };
  }
  else
  {
    *w = width * 72.0 / vals->ppi;
    *h = height * 72.0 / vals->ppi;
  };

  space = area_width - *w;
  if (vals->center_x)
    *x = space / 2;
  else
    *x = CLAMP(vals->left, MIN(space, 0.0), MAX(space, 0.0));

  space = area_height - *h;
  if (vals->center_y)
    *y = space / 2;
  else
    *y = CLAMP(vals->top, MIN(space, 0.0), MAX(space, 0.0));
}


/*
 * 'gtkprint_orientation()' - Pick the orientation for the old dialog's
 *                            "Auto" orientation.
 */

static int
gtkprint_orientation(gtkprint_job_t *job,	/* I - Print job */
                     float          scaling)	/* I - Percent, or -PPI */
{
  gtkprint_vals_t vals;			/* Image settings */
  double	pw, ph,			/* Portrait printable area */
		x, y,
		ow, oh,			/* Portrait size of image */
		tw, th;			/* Landscape size of image */


  gtk_page_setup_set_orientation(job->page_setup,
                                 GTK_PAGE_ORIENTATION_PORTRAIT);
  gtkprint_area(job->page_setup, FALSE, &pw, &ph);

  vals             = job->vals;
  vals.fit_to_page = scaling >= 0.0;

  gtkprint_layout(&vals, job->width, job->height, pw, ph, &x, &y, &ow, &oh);
  gtkprint_layout(&vals, job->width, job->height, ph, pw, &x, &y, &tw, &th);

  if (scaling < 0.0)
  {
    if ((ow > pw || oh > ph) && ow <= ph && oh <= pw)
      return (ORIENT_LANDSCAPE);
  }
  else if (tw * th > ow * oh)
    return (ORIENT_LANDSCAPE);

  return (ORIENT_PORTRAIT);
}


/*
 * 'gtkprint_begin_print()' - Convert the image, with its brightness and
 *                            grayscale.
 */

static void
gtkprint_begin_print(GtkPrintOperation *operation,	/* I - Print operation */
                     GtkPrintContext   *context,	/* I - Print context */
                     gpointer          data)		/* I - Print job */
{
  gtkprint_job_t *job = data;


  if (job->watchdog != 0)
  {
    g_source_remove(job->watchdog);
    job->watchdog = 0;
  };

  if (job->surface != NULL)
    return;

  gimp_progress_init("Printing...");

  job->surface = gtkprint_convert(job->drawable, job->cmap, MAX_SURFACE,
                                  TRUE);

  if (job->surface == NULL)
  {
    job->failed = TRUE;
    gtk_print_operation_cancel(operation);
    return;
  };

  if (job->vals.brightness != 100 || job->vals.grayscale)
    gtkprint_adjust(job->surface, job->surface, job->vals.brightness,
                    job->vals.grayscale);

  gimp_progress_update(1.0);
}


/*
 * 'gtkprint_draw_page()' - Draw the image on the page.
 */

static void
gtkprint_draw_page(GtkPrintOperation *operation,	/* I - Print operation */
                   GtkPrintContext   *context,		/* I - Print context */
                   gint              page_nr,		/* I - Page number */
                   gpointer          data)		/* I - Print job */
{
  gtkprint_job_t *job = data;
  cairo_t	*cr;			/* Page */
  double	area_width,		/* Printable area, points */
		area_height,
		x, y, w, h;		/* Image on the page, points */


  if (job->surface == NULL)
    return;

  cr          = gtk_print_context_get_cairo_context(context);
  area_width  = gtk_print_context_get_width(context);
  area_height = gtk_print_context_get_height(context);

  gtkprint_layout(&job->vals, job->width, job->height, area_width,
                  area_height, &x, &y, &w, &h);

  cairo_save(cr);
  cairo_rectangle(cr, 0, 0, area_width, area_height);
  cairo_clip(cr);
  cairo_translate(cr, x, y);
  cairo_scale(cr, w / cairo_image_surface_get_width(job->surface),
              h / cairo_image_surface_get_height(job->surface));
  cairo_set_source_surface(cr, job->surface, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);
}


/*
 * 'gtkprint_heading()' - Add a section heading to the tab.
 */

static void
gtkprint_heading(GtkWidget  *grid,	/* I - Grid of the tab */
                 const char *text,	/* I - Heading */
                 int        row)	/* I - Row */
{
  GtkWidget	*label;			/* Heading */
  char		*markup;		/* Bold heading */


  markup = g_markup_printf_escaped("<b>%s</b>", text);
  label  = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(label), markup);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0);
  if (row > 0)
    gtk_widget_set_margin_top(label, 12);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 3, 1);
  g_free(markup);
}


/*
 * 'gtkprint_spin()' - Add a labelled spin button to the tab.
 */

static GtkWidget *
gtkprint_spin(gtkprint_job_t *job,	/* I - Print job */
              GtkWidget      *grid,	/* I - Grid of the tab */
              const char     *text,	/* I - Label, with mnemonic */
              int            spin,	/* I - SPIN_ number */
              int            row)	/* I - Row */
{
  GtkWidget	*label;			/* Label */


  label = gtk_label_new_with_mnemonic(text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0);
  gtk_widget_set_margin_start(label, 24);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

  job->adjs[spin]  = gtk_adjustment_new(1.0, 0.0, 2.0, 1.0, 10.0, 0.0);
  job->spins[spin] = gtk_spin_button_new(job->adjs[spin], 1.0, 0);
  gtk_editable_set_width_chars(GTK_EDITABLE(job->spins[spin]), 9);
  gtk_label_set_mnemonic_widget(GTK_LABEL(label), job->spins[spin]);
  gtk_grid_attach(GTK_GRID(grid), job->spins[spin], 1, row, 1, 1);

  g_signal_connect(job->adjs[spin], "value-changed",
                   G_CALLBACK(gtkprint_spin_changed), job);

  return (label);
}


/*
 * 'gtkprint_check()' - Add a check button to the tab.
 */

static GtkWidget *
gtkprint_check(gtkprint_job_t *job,	/* I - Print job */
               GtkWidget      *grid,	/* I - Grid of the tab */
               const char     *text,	/* I - Label, with mnemonic */
               int            row)	/* I - Row */
{
  GtkWidget	*button;		/* Check button */


  button = gtk_check_button_new_with_mnemonic(text);
  gtk_grid_attach(GTK_GRID(grid), button, 0, row, 3, 1);
  g_signal_connect(button, "toggled", G_CALLBACK(gtkprint_toggled), job);

  return (button);
}


/*
 * 'gtkprint_create_widget()' - Create the "Image Settings" tab.
 */

static GObject *
gtkprint_create_widget(GtkPrintOperation *operation,	/* I - Print operation */
                       gpointer          data)		/* I - Print job */
{
  gtkprint_job_t *job = data;
  GtkWidget	*hbox,			/* Preview and settings */
		*grid,			/* Settings */
		*label,			/* Label */
		*scale;			/* Brightness scale */
  GtkGesture	*drag;			/* Moving the image */
  const char	*names[G_N_ELEMENTS(units) + 1];
					/* Names of units */
  char		*text;			/* Size of image */
  int		i,			/* Looping var */
		row;			/* Row of grid */


  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_set_margin_start(hbox, 12);
  gtk_widget_set_margin_end(hbox, 12);
  gtk_widget_set_margin_top(hbox, 12);
  gtk_widget_set_margin_bottom(hbox, 12);
  g_signal_connect(hbox, "destroy", G_CALLBACK(gtkprint_widget_destroyed),
                   job);

 /*
  * Page preview; dragging the image moves it...
  */

  job->preview = gtk_drawing_area_new();
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(job->preview),
                                     PREVIEW_SIZE);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(job->preview),
                                      PREVIEW_SIZE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(job->preview),
                                 gtkprint_preview_draw, job, NULL);
  gtk_widget_set_hexpand(job->preview, TRUE);
  gtk_widget_set_vexpand(job->preview, TRUE);
  gtk_widget_set_cursor_from_name(job->preview, "move");
  gtk_widget_set_tooltip_text(job->preview,
                              "Drag the image to move it on the page");
  gtk_box_append(GTK_BOX(hbox), job->preview);

  drag = gtk_gesture_drag_new();
  g_signal_connect(drag, "drag-begin", G_CALLBACK(gtkprint_drag_begin), job);
  g_signal_connect(drag, "drag-update", G_CALLBACK(gtkprint_drag_update),
                   job);
  gtk_widget_add_controller(job->preview, GTK_EVENT_CONTROLLER(drag));

  grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
  gtk_widget_set_valign(grid, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(hbox), grid);

 /*
  * Size...
  */

  row = 0;
  gtkprint_heading(grid, "Size", row ++);

  text  = g_strdup_printf("Image: %d \303\227 %d pixels", job->width,
                          job->height);
  label = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0);
  gtk_widget_add_css_class(label, "dim-label");
  gtk_grid_attach(GTK_GRID(grid), label, 0, row ++, 3, 1);
  g_free(text);

  job->fit    = gtkprint_check(job, grid, "_Fit to page", row ++);
  job->custom = gtkprint_check(job, grid, "C_ustom size:", row ++);
  gtk_check_button_set_group(GTK_CHECK_BUTTON(job->custom),
                             GTK_CHECK_BUTTON(job->fit));

  gtkprint_spin(job, grid, "_Width:", SPIN_WIDTH, row);

  for (i = 0; i < (int)G_N_ELEMENTS(units); i ++)
    names[i] = units[i].name;
  names[i] = NULL;

  job->unit_menu = gtk_drop_down_new_from_strings(names);
  gtk_widget_set_valign(job->unit_menu, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(job->unit_menu, 110, -1);	/* Keep it steady */
  gtk_grid_attach(GTK_GRID(grid), job->unit_menu, 2, row ++, 1, 1);
  g_signal_connect(job->unit_menu, "notify::selected",
                   G_CALLBACK(gtkprint_unit_changed), job);

  gtkprint_spin(job, grid, "_Height:", SPIN_HEIGHT, row ++);
  gtkprint_spin(job, grid, "_Resolution:", SPIN_PPI, row);

  label = gtk_label_new("pixels/inch");
  gtk_label_set_xalign(GTK_LABEL(label), 0.0);
  gtk_grid_attach(GTK_GRID(grid), label, 2, row ++, 1, 1);

 /*
  * Position...
  */

  gtkprint_heading(grid, "Position", row ++);

  job->center_x = gtkprint_check(job, grid, "Center _horizontally", row ++);
  gtkprint_spin(job, grid, "_Left:", SPIN_LEFT, row);

  job->left_unit = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(job->left_unit), 0.0);
  gtk_grid_attach(GTK_GRID(grid), job->left_unit, 2, row ++, 1, 1);

  job->center_y = gtkprint_check(job, grid, "Center _vertically", row ++);
  gtkprint_spin(job, grid, "_Top:", SPIN_TOP, row);

  job->top_unit = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(job->top_unit), 0.0);
  gtk_grid_attach(GTK_GRID(grid), job->top_unit, 2, row ++, 1, 1);

 /*
  * Options...
  */

  gtkprint_heading(grid, "Options", row ++);

  job->full_page = gtkprint_check(job, grid, "_Ignore page margins", row ++);
  gtk_widget_set_tooltip_text(job->full_page,
                              "Place the image from the edges of the paper. "
                              "Most printers cannot print all the way to "
                              "the edges.");

  job->grayscale = gtkprint_check(job, grid, "Print in _grayscale", row ++);

  label = gtk_label_new_with_mnemonic("_Brightness:");
  gtk_label_set_xalign(GTK_LABEL(label), 0.0);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

  job->brightness = gtk_adjustment_new(job->vals.brightness, BRIGHTNESS_MIN,
                                       BRIGHTNESS_MAX, 1.0, 10.0, 0.0);
  scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, job->brightness);
  gtk_scale_set_digits(GTK_SCALE(scale), 0);
  gtk_range_set_round_digits(GTK_RANGE(scale), 0);
  gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
  gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_RIGHT);
  gtk_scale_add_mark(GTK_SCALE(scale), 100.0, GTK_POS_BOTTOM, NULL);
  gtk_widget_set_hexpand(scale, TRUE);
  gtk_label_set_mnemonic_widget(GTK_LABEL(label), scale);
  gtk_grid_attach(GTK_GRID(grid), scale, 1, row ++, 2, 1);
  g_signal_connect(job->brightness, "value-changed",
                   G_CALLBACK(gtkprint_brightness_changed), job);

  gtkprint_update_widgets(job);

  return (G_OBJECT(hbox));
}


/*
 * 'gtkprint_update_widget()' - The page setup changed in the dialog.
 */

static void
gtkprint_update_widget(GtkPrintOperation *operation,	/* I - Print operation */
                       GtkWidget         *widget,	/* I - The tab */
                       GtkPageSetup      *page_setup,	/* I - Page setup */
                       GtkPrintSettings  *settings,	/* I - Printer */
                       gpointer          data)		/* I - Print job */
{
  gtkprint_job_t *job = data;


  if (page_setup != NULL)
  {
    g_object_unref(job->page_setup);
    job->page_setup = gtk_page_setup_copy(page_setup);
  };

  gtkprint_update_widgets(job);
}


/*
 * 'gtkprint_apply_widget()' - Take the values typed in the tab.
 */

static void
gtkprint_apply_widget(GtkPrintOperation *operation,	/* I - Print operation */
                      GtkWidget         *widget,	/* I - The tab */
                      gpointer          data)		/* I - Print job */
{
  gtkprint_job_t *job = data;
  int		i;			/* Looping var */


  for (i = 0; i < NUM_SPINS; i ++)
    if (job->spins[i] != NULL && gtk_widget_is_sensitive(job->spins[i]))
      gtk_spin_button_update(GTK_SPIN_BUTTON(job->spins[i]));
}


/*
 * 'gtkprint_widget_destroyed()' - Forget the widgets of the tab.
 */

static void
gtkprint_widget_destroyed(GtkWidget *widget,	/* I - The tab */
                          gpointer  data)	/* I - Print job */
{
  gtkprint_job_t *job = data;
  int		i;			/* Looping var */


  job->preview = NULL;

  for (i = 0; i < NUM_SPINS; i ++)
    job->spins[i] = NULL;
}


/*
 * 'gtkprint_update_widgets()' - Show the image settings in the tab.
 *
 * The settings (and the page setup) are the only state: every change
 * updates them, then all of the tab is shown from them.
 */

static void
gtkprint_update_widgets(gtkprint_job_t *job)	/* I - Print job */
{
  const gtkprint_unit_t *unit;		/* Unit shown */
  double	area_width,		/* Printable area, points */
		area_height,
		x, y, w, h;		/* Image on the page, points */
  int		fit;			/* Fit to page */


  if (job->preview == NULL)
    return;

  unit = units + job->vals.unit;
  fit  = job->vals.fit_to_page;

  gtkprint_area(job->page_setup, job->vals.full_page, &area_width,
                &area_height);
  gtkprint_layout(&job->vals, job->width, job->height, area_width,
                  area_height, &x, &y, &w, &h);

  job->updating ++;

  gtkprint_set_active(fit ? job->fit : job->custom, TRUE);

  if (gtk_drop_down_get_selected(GTK_DROP_DOWN(job->unit_menu)) !=
      (guint)job->vals.unit)
    gtk_drop_down_set_selected(GTK_DROP_DOWN(job->unit_menu), job->vals.unit);

  gtkprint_set_spin(job, SPIN_WIDTH, w / unit->points,
                    job->width * 72.0 / PPI_MAX / unit->points,
                    job->width * 72.0 / PPI_MIN / unit->points,
                    unit->digits, unit->step, !fit);
  gtkprint_set_spin(job, SPIN_HEIGHT, h / unit->points,
                    job->height * 72.0 / PPI_MAX / unit->points,
                    job->height * 72.0 / PPI_MIN / unit->points,
                    unit->digits, unit->step, !fit);
  gtkprint_set_spin(job, SPIN_PPI, job->width * 72.0 / w, PPI_MIN, PPI_MAX,
                    2, 1.0, !fit);

  gtkprint_set_active(job->center_x, job->vals.center_x);
  gtkprint_set_spin(job, SPIN_LEFT, x / unit->points,
                    MIN(area_width - w, 0.0) / unit->points,
                    MAX(area_width - w, 0.0) / unit->points,
                    unit->digits, unit->step, !job->vals.center_x);
  gtk_label_set_text(GTK_LABEL(job->left_unit), unit->name);

  gtkprint_set_active(job->center_y, job->vals.center_y);
  gtkprint_set_spin(job, SPIN_TOP, y / unit->points,
                    MIN(area_height - h, 0.0) / unit->points,
                    MAX(area_height - h, 0.0) / unit->points,
                    unit->digits, unit->step, !job->vals.center_y);
  gtk_label_set_text(GTK_LABEL(job->top_unit), unit->name);

  gtkprint_set_active(job->full_page, job->vals.full_page);
  gtkprint_set_active(job->grayscale, job->vals.grayscale);
  gtk_adjustment_set_value(job->brightness, job->vals.brightness);

  job->updating --;

  gtk_widget_queue_draw(job->preview);
}


/*
 * 'gtkprint_set_spin()' - Show a value in a spin button.
 */

static void
gtkprint_set_spin(gtkprint_job_t *job,	/* I - Print job */
                  int            spin,	/* I - SPIN_ number */
                  double         value,	/* I - Value */
                  double         lower,	/* I - Range */
                  double         upper,
                  int            digits,	/* I - Digits shown */
                  double         step,	/* I - Step */
                  int            sensitive)	/* I - Can be changed */
{
  gtk_adjustment_configure(job->adjs[spin], value, lower, upper, step,
                           step * 10.0, 0.0);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(job->spins[spin]), digits);
  gtk_widget_set_sensitive(job->spins[spin], sensitive);

  job->shown[spin] = gtk_adjustment_get_value(job->adjs[spin]);
}


/*
 * 'gtkprint_set_active()' - Set a check button if it changed.
 */

static void
gtkprint_set_active(GtkWidget *button,	/* I - Check button */
                    int       active)	/* I - State */
{
  if (gtk_check_button_get_active(GTK_CHECK_BUTTON(button)) != (active != 0))
    gtk_check_button_set_active(GTK_CHECK_BUTTON(button), active);
}


/*
 * 'gtkprint_spin_changed()' - A size, the resolution or an offset changed.
 */

static void
gtkprint_spin_changed(GtkAdjustment *adjustment,	/* I - Spin value */
                      gpointer      data)		/* I - Print job */
{
  gtkprint_job_t *job = data;
  double	value,			/* New value */
		points,			/* ... in points */
		scale;			/* 10^digits */
  int		spin;			/* SPIN_ number */


  if (job->updating)
    return;

  for (spin = 0; spin < NUM_SPINS; spin ++)
    if (job->adjs[spin] == adjustment)
      break;

  if (spin == NUM_SPINS || job->spins[spin] == NULL)
    return;

 /*
  * A spin button sets the value it shows, rounded to its digits, when it
  * loses the focus: that is no change...
  */

  value = gtk_adjustment_get_value(adjustment);
  scale = pow(10.0, gtk_spin_button_get_digits(GTK_SPIN_BUTTON(job->spins[spin])));

  if (floor(value * scale + 0.5) == floor(job->shown[spin] * scale + 0.5))
    return;

  points = value * units[job->vals.unit].points;

  switch (spin)
  {
    case SPIN_WIDTH :
        job->vals.ppi = job->width * 72.0 / MAX(points, 0.001);
        break;
    case SPIN_HEIGHT :
        job->vals.ppi = job->height * 72.0 / MAX(points, 0.001);
        break;
    case SPIN_PPI :
        job->vals.ppi = value;
        break;
    case SPIN_LEFT :
        job->vals.left = points;
        break;
    case SPIN_TOP :
        job->vals.top = points;
        break;
  };

  job->vals.ppi = CLAMP(job->vals.ppi, PPI_MIN, PPI_MAX);

  gtkprint_update_widgets(job);
}


/*
 * 'gtkprint_toggled()' - A check button of the tab changed.
 */

static void
gtkprint_toggled(GtkCheckButton *button,	/* I - Check button */
                 gpointer       data)		/* I - Print job */
{
  gtkprint_job_t *job = data;
  int		active;			/* State of button */
  double	area_width,		/* Printable area, points */
		area_height,
		x, y, w, h;		/* Image on the page, points */


  if (job->updating)
    return;

  active = gtk_check_button_get_active(button);

  gtkprint_area(job->page_setup, job->vals.full_page, &area_width,
                &area_height);
  gtkprint_layout(&job->vals, job->width, job->height, area_width,
                  area_height, &x, &y, &w, &h);

  if (button == GTK_CHECK_BUTTON(job->fit) ||
      button == GTK_CHECK_BUTTON(job->custom))
  {
    if (!active)
      return;			/* The other button of the pair does it */

    job->vals.fit_to_page = button == GTK_CHECK_BUTTON(job->fit);
  }
  else if (button == GTK_CHECK_BUTTON(job->center_x))
  {
    job->vals.center_x = active;
    job->vals.left     = x;	/* Not centered: stays where it is */
  }
  else if (button == GTK_CHECK_BUTTON(job->center_y))
  {
    job->vals.center_y = active;
    job->vals.top      = y;
  }
  else if (button == GTK_CHECK_BUTTON(job->full_page))
  {
    job->vals.full_page = active;

    if (job->operation != NULL)
      gtk_print_operation_set_use_full_page(job->operation, active);
  }
  else if (button == GTK_CHECK_BUTTON(job->grayscale))
  {
    job->vals.grayscale = active;

    gtkprint_update_thumb(job);
  };

  gtkprint_update_widgets(job);
}


/*
 * 'gtkprint_unit_changed()' - Another unit was chosen.
 */

static void
gtkprint_unit_changed(GObject    *menu,		/* I - Unit drop-down */
                      GParamSpec *pspec,	/* I - "selected" */
                      gpointer   data)		/* I - Print job */
{
  gtkprint_job_t *job = data;
  guint		unit;			/* Chosen unit */


  unit = gtk_drop_down_get_selected(GTK_DROP_DOWN(menu));

  if (job->updating || unit >= G_N_ELEMENTS(units))
    return;

  job->vals.unit = unit;

  gtkprint_update_widgets(job);
}


/*
 * 'gtkprint_brightness_changed()' - The brightness changed.
 */

static void
gtkprint_brightness_changed(GtkAdjustment *adjustment,	/* I - Brightness */
                            gpointer      data)		/* I - Print job */
{
  gtkprint_job_t *job = data;
  int		brightness;		/* New brightness */


  brightness = (int)(gtk_adjustment_get_value(adjustment) + 0.5);

  if (job->updating || brightness == job->vals.brightness)
    return;

  job->vals.brightness = brightness;

  gtkprint_update_thumb(job);
}


/*
 * 'gtkprint_geometry()' - Place the page in the preview.
 */

static void
gtkprint_geometry(gtkprint_job_t  *job,		/* I - Print job */
                  int             width,	/* I - Size of the preview */
                  int             height,
                  gtkprint_geom_t *geom)	/* O - Page in the preview */
{
  GtkPageSetup	*page_setup = job->page_setup;


  geom->paper_w = gtk_page_setup_get_paper_width(page_setup, GTK_UNIT_POINTS);
  geom->paper_h = gtk_page_setup_get_paper_height(page_setup, GTK_UNIT_POINTS);
  geom->paper_w = MAX(geom->paper_w, 1.0);
  geom->paper_h = MAX(geom->paper_h, 1.0);

 /*
  * The margins are those of the paper in portrait orientation: turned
  * like GtkPrintContext turns them...
  */

  if (job->vals.full_page)
  {
    geom->area_x = 0.0;
    geom->area_y = 0.0;
  }
  else switch (gtk_page_setup_get_orientation(page_setup))
  {
    case GTK_PAGE_ORIENTATION_LANDSCAPE :
        geom->area_x = gtk_page_setup_get_bottom_margin(page_setup,
                                                        GTK_UNIT_POINTS);
        geom->area_y = gtk_page_setup_get_left_margin(page_setup,
                                                      GTK_UNIT_POINTS);
        break;
    case GTK_PAGE_ORIENTATION_REVERSE_PORTRAIT :
        geom->area_x = gtk_page_setup_get_right_margin(page_setup,
                                                       GTK_UNIT_POINTS);
        geom->area_y = gtk_page_setup_get_bottom_margin(page_setup,
                                                        GTK_UNIT_POINTS);
        break;
    case GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE :
        geom->area_x = gtk_page_setup_get_top_margin(page_setup,
                                                     GTK_UNIT_POINTS);
        geom->area_y = gtk_page_setup_get_right_margin(page_setup,
                                                       GTK_UNIT_POINTS);
        break;
    default :
        geom->area_x = gtk_page_setup_get_left_margin(page_setup,
                                                      GTK_UNIT_POINTS);
        geom->area_y = gtk_page_setup_get_top_margin(page_setup,
                                                     GTK_UNIT_POINTS);
        break;
  };

  gtkprint_area(page_setup, job->vals.full_page, &geom->area_w, &geom->area_h);

  geom->scale   = MIN((width - 2 * PREVIEW_PAD) / geom->paper_w,
                      (height - 2 * PREVIEW_PAD) / geom->paper_h);
  geom->scale   = MAX(geom->scale, 0.01);
  geom->paper_x = floor((width - geom->paper_w * geom->scale) / 2);
  geom->paper_y = floor((height - geom->paper_h * geom->scale) / 2);
}


/*
 * 'gtkprint_preview_draw()' - Draw the paper, the printable area and the
 *                             image where it will print.
 */

static void
gtkprint_preview_draw(GtkDrawingArea *area,	/* I - Preview */
                      cairo_t        *cr,	/* I - Drawing */
                      int            width,	/* I - Size of preview */
                      int            height,
                      gpointer       data)	/* I - Print job */
{
  gtkprint_job_t *job = data;
  gtkprint_geom_t geom;			/* Page in the preview */
  double	s,			/* Pixels per point */
		ax, ay,			/* Printable area, pixels */
		x, y, w, h;		/* Image on the page, points */
  static const double dash[] = { 3.0, 3.0 };


  gtkprint_geometry(job, width, height, &geom);
  gtkprint_layout(&job->vals, job->width, job->height, geom.area_w,
                  geom.area_h, &x, &y, &w, &h);

  s  = geom.scale;
  ax = geom.paper_x + geom.area_x * s;
  ay = geom.paper_y + geom.area_y * s;

 /*
  * Paper, with a shadow...
  */

  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.3);
  cairo_rectangle(cr, geom.paper_x + 3, geom.paper_y + 3,
                  geom.paper_w * s, geom.paper_h * s);
  cairo_fill(cr);

  cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
  cairo_rectangle(cr, geom.paper_x, geom.paper_y, geom.paper_w * s,
                  geom.paper_h * s);
  cairo_fill(cr);

 /*
  * The image, clipped to the printable area...
  */

  if (job->thumb != NULL)
  {
    cairo_save(cr);
    cairo_rectangle(cr, ax, ay, geom.area_w * s, geom.area_h * s);
    cairo_clip(cr);
    cairo_translate(cr, ax + x * s, ay + y * s);
    cairo_scale(cr, w * s / cairo_image_surface_get_width(job->thumb),
                h * s / cairo_image_surface_get_height(job->thumb));
    cairo_set_source_surface(cr, job->thumb, 0, 0);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
    cairo_rectangle(cr, 0, 0, cairo_image_surface_get_width(job->thumb),
                    cairo_image_surface_get_height(job->thumb));
    cairo_fill(cr);
    cairo_restore(cr);
  };

 /*
  * Outlines: the image, the printable area (dashed) and the paper...
  */

  cairo_set_line_width(cr, 1.0);

  cairo_set_source_rgba(cr, 0.2, 0.4, 0.8, 0.8);
  cairo_rectangle(cr, floor(ax + x * s) + 0.5, floor(ay + y * s) + 0.5,
                  MAX(floor(w * s) - 1.0, 0.0), MAX(floor(h * s) - 1.0, 0.0));
  cairo_stroke(cr);

  if (!job->vals.full_page)
  {
    cairo_save(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.4);
    cairo_set_dash(cr, dash, 2, 0.0);
    cairo_rectangle(cr, floor(ax) + 0.5, floor(ay) + 0.5,
                    MAX(floor(geom.area_w * s) - 1.0, 0.0),
                    MAX(floor(geom.area_h * s) - 1.0, 0.0));
    cairo_stroke(cr);
    cairo_restore(cr);
  };

  cairo_set_source_rgb(cr, 0.45, 0.45, 0.45);
  cairo_rectangle(cr, geom.paper_x + 0.5, geom.paper_y + 0.5,
                  floor(geom.paper_w * s) - 1.0, floor(geom.paper_h * s) - 1.0);
  cairo_stroke(cr);
}


/*
 * 'gtkprint_drag_begin()' - Start moving the image.
 */

static void
gtkprint_drag_begin(GtkGestureDrag *gesture,	/* I - Drag gesture */
                    double         start_x,	/* I - Start of drag */
                    double         start_y,
                    gpointer       data)	/* I - Print job */
{
  gtkprint_job_t *job = data;
  double	area_width,		/* Printable area, points */
		area_height,
		w, h;			/* Size of image, points */


  gtkprint_area(job->page_setup, job->vals.full_page, &area_width,
                &area_height);
  gtkprint_layout(&job->vals, job->width, job->height, area_width,
                  area_height, &job->drag_left, &job->drag_top, &w, &h);

  job->drag_moved = FALSE;
}


/*
 * 'gtkprint_drag_update()' - Move the image; it is no longer centered.
 */

static void
gtkprint_drag_update(GtkGestureDrag *gesture,	/* I - Drag gesture */
                     double         offset_x,	/* I - Distance dragged */
                     double         offset_y,
                     gpointer       data)	/* I - Print job */
{
  gtkprint_job_t *job = data;
  gtkprint_geom_t geom;			/* Page in the preview */
  double	x, y, w, h;		/* Image on the page, points */


  if (job->preview == NULL ||
      (!job->drag_moved && offset_x == 0.0 && offset_y == 0.0))
    return;

  job->drag_moved = TRUE;

  gtkprint_geometry(job, gtk_widget_get_width(job->preview),
                    gtk_widget_get_height(job->preview), &geom);

  job->vals.center_x = FALSE;
  job->vals.center_y = FALSE;
  job->vals.left     = job->drag_left + offset_x / geom.scale;
  job->vals.top      = job->drag_top + offset_y / geom.scale;

  gtkprint_layout(&job->vals, job->width, job->height, geom.area_w,
                  geom.area_h, &x, &y, &w, &h);

  job->vals.left = x;
  job->vals.top  = y;

  gtkprint_update_widgets(job);
}


/*
 * End of "$Id$".
 */
