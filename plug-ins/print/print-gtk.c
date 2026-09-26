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
 *   gtkprint_print() - Print an image through GtkPrintOperation.
 *
 * The other drivers write a printer language to a file or a spooler
 * command, which is how the plug-in reached printers on UNIX.  Windows
 * (and any system without lp/lpr) has neither, so this "driver" hands the
 * image to the system's printers with GtkPrintOperation: the image is
 * converted with the same brightness lookup table and colour functions as
 * the other drivers, and placed on the page with the same scaling,
 * orientation and offsets (all in points from the top-left corner of the
 * imageable area).
 */

#include "print.h"


typedef struct
{
  cairo_surface_t *surface;	/* Converted image */
  int		width,		/* Width of image in pixels */
		height;		/* Height of image in pixels */
  int		orientation;	/* Requested orientation */
  float		scaling;	/* Scaling, percent or -PPI */
  int		left,		/* Left offset, points (-1 = centered) */
		top;		/* Top offset, points (-1 = centered) */
} gtkprint_job_t;


/*
 * 'gtkprint_size()' - Compute the printed size of the image on a page.
 */

static void
gtkprint_size(gtkprint_job_t *job,		/* I - Print job */
              double         page_width,	/* I - Imageable width */
              double         page_height,	/* I - Imageable height */
              double         *out_width,	/* O - Printed width */
              double         *out_height)	/* O - Printed height */
{
  if (job->scaling < 0.0)
  {
    *out_width  = job->width * -72.0 / job->scaling;
    *out_height = job->height * -72.0 / job->scaling;
  }
  else
  {
    *out_width  = page_width * job->scaling / 100.0;
    *out_height = *out_width * job->height / job->width;
    if (*out_height > page_height)
    {
      *out_height = page_height * job->scaling / 100.0;
      *out_width  = *out_height * job->width / job->height;
    };
  };
}


/*
 * 'gtkprint_request_page_setup()' - Pick the orientation for the page.
 */

static void
gtkprint_request_page_setup(GtkPrintOperation *operation,
                            GtkPrintContext   *context,
                            gint              page_nr,
                            GtkPageSetup      *setup,
                            gpointer          data)
{
  gtkprint_job_t *job = data;
  double	pw, ph,			/* Portrait imageable area */
		ow, oh,			/* Portrait size of image */
		tw, th;			/* Landscape size of image */
  int		orientation;


  gtk_page_setup_set_orientation(setup, GTK_PAGE_ORIENTATION_PORTRAIT);

  orientation = job->orientation;

  if (orientation == ORIENT_AUTO)
  {
    pw = gtk_page_setup_get_page_width(setup, GTK_UNIT_POINTS);
    ph = gtk_page_setup_get_page_height(setup, GTK_UNIT_POINTS);

    gtkprint_size(job, pw, ph, &ow, &oh);
    gtkprint_size(job, ph, pw, &tw, &th);

    if (job->scaling < 0.0)
    {
      if ((ow > pw && oh < pw) || (oh > ph && ow < ph))
        orientation = ORIENT_LANDSCAPE;
      else
        orientation = ORIENT_PORTRAIT;
    }
    else if (tw * th > ow * oh)
      orientation = ORIENT_LANDSCAPE;
    else
      orientation = ORIENT_PORTRAIT;
  };

  if (orientation == ORIENT_LANDSCAPE)
    gtk_page_setup_set_orientation(setup, GTK_PAGE_ORIENTATION_LANDSCAPE);
}


/*
 * 'gtkprint_draw_page()' - Draw the image on the page.
 */

static void
gtkprint_draw_page(GtkPrintOperation *operation,
                   GtkPrintContext   *context,
                   gint              page_nr,
                   gpointer          data)
{
  gtkprint_job_t *job = data;
  cairo_t	*cr;
  double	pw, ph,			/* Imageable area */
		ow, oh,			/* Printed size of image */
		x, y;			/* Position of image */


  cr = gtk_print_context_get_cairo_context(context);
  pw = gtk_print_context_get_width(context);
  ph = gtk_print_context_get_height(context);

  gtkprint_size(job, pw, ph, &ow, &oh);

  if (job->left < 0)
    x = (pw - ow) / 2;
  else
    x = MIN(job->left, pw - ow);

  if (job->top < 0)
    y = (ph - oh) / 2;
  else
    y = MIN(job->top, ph - oh);

  cairo_save(cr);
  cairo_translate(cr, x, y);
  cairo_scale(cr, ow / job->width, oh / job->height);
  cairo_set_source_surface(cr, job->surface, 0, 0);
  cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
  cairo_rectangle(cr, 0, 0, job->width, job->height);
  cairo_fill(cr);
  cairo_restore(cr);
}


/*
 * 'gtkprint_convert()' - Convert the drawable to a cairo image surface.
 */

static cairo_surface_t *
gtkprint_convert(GDrawable *drawable,	/* I - Image to print */
                 int       output_type,	/* I - Color or grayscale */
                 guchar    *lut,	/* I - Brightness lookup table */
                 guchar    *cmap)	/* I - Colormap (indexed images) */
{
  cairo_surface_t *surface;
  GPixelRgn	rgn;
  convert_t	colorfunc;
  guchar	*in,
		*out,
		*row;
  guint32	*pixel;
  int		x, y,
		stride;


  if (drawable->bpp < 3 && cmap == NULL)
    output_type = OUTPUT_GRAY;

  if (output_type == OUTPUT_COLOR)
    colorfunc = (drawable->bpp >= 3) ? rgb_to_rgb : indexed_to_rgb;
  else if (drawable->bpp >= 3)
    colorfunc = rgb_to_gray;
  else if (cmap == NULL)
    colorfunc = gray_to_gray;
  else
    colorfunc = indexed_to_gray;

  surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, drawable->width,
                                       drawable->height);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
  {
    cairo_surface_destroy(surface);
    return (NULL);
  };

  gimp_pixel_rgn_init(&rgn, drawable, 0, 0, drawable->width, drawable->height,
                      FALSE, FALSE);

  in     = g_malloc(drawable->width * drawable->bpp);
  out    = g_malloc(drawable->width * 3);
  row    = cairo_image_surface_get_data(surface);
  stride = cairo_image_surface_get_stride(surface);

  cairo_surface_flush(surface);

  for (y = 0; y < drawable->height; y ++, row += stride)
  {
    if ((y & 15) == 0)
      gimp_progress_update((double)y / (double)drawable->height);

    gimp_pixel_rgn_get_row(&rgn, in, 0, y, drawable->width);
    (*colorfunc)(in, out, drawable->width, drawable->bpp, lut, cmap);

    pixel = (guint32 *)row;

    if (output_type == OUTPUT_COLOR)
      for (x = 0; x < drawable->width; x ++)
        pixel[x] = (out[3 * x] << 16) | (out[3 * x + 1] << 8) | out[3 * x + 2];
    else
      for (x = 0; x < drawable->width; x ++)
        pixel[x] = (out[x] << 16) | (out[x] << 8) | out[x];
  };

  cairo_surface_mark_dirty(surface);

  g_free(in);
  g_free(out);

  return (surface);
}


/*
 * 'gtkprint_print()' - Print an image through GtkPrintOperation.
 */

int
gtkprint_print(char      *media_size,	/* I - Media size name */
               int       media_width,	/* I - Media width, points */
               int       media_length,	/* I - Media length, points */
               int       output_type,	/* I - Output type (color/grayscale) */
               int       orientation,	/* I - Orientation of image */
               float     scaling,	/* I - Scaling of image */
               int       left,		/* I - Left offset of image (points) */
               int       top,		/* I - Top offset of image (points) */
               int       show_dialog,	/* I - Show the system print dialog */
               GDrawable *drawable,	/* I - Image to print */
               guchar    *lut,		/* I - Brightness lookup table */
               guchar    *cmap)	/* I - Colormap (for indexed images) */
{
  GtkPrintOperation	*operation;
  GtkPrintOperationResult result;
  GtkPageSetup		*setup;
  GtkPaperSize		*paper;
  gtkprint_job_t	job;
  GError		*error = NULL;


  gimp_progress_init("Printing...");

  job.surface     = gtkprint_convert(drawable, output_type, lut, cmap);
  job.width       = drawable->width;
  job.height      = drawable->height;
  job.orientation = orientation;
  job.scaling     = scaling;
  job.left        = left;
  job.top         = top;

  if (job.surface == NULL)
    return (FALSE);

  operation = gtk_print_operation_new();
  gtk_print_operation_set_job_name(operation, "GIMP Print");
  gtk_print_operation_set_n_pages(operation, 1);
  gtk_print_operation_set_unit(operation, GTK_UNIT_POINTS);
  gtk_print_operation_set_use_full_page(operation, FALSE);

  setup = gtk_page_setup_new();
  if (media_size != NULL && media_size[0] != '\0' &&
      media_width > 0 && media_length > 0)
  {
    paper = gtk_paper_size_new_custom(media_size, media_size,
                                      media_width, media_length,
                                      GTK_UNIT_POINTS);
    gtk_page_setup_set_paper_size_and_default_margins(setup, paper);
    gtk_paper_size_free(paper);
  };
  gtk_print_operation_set_default_page_setup(operation, setup);
  g_object_unref(setup);

  g_signal_connect(operation, "request-page-setup",
                   G_CALLBACK(gtkprint_request_page_setup), &job);
  g_signal_connect(operation, "draw-page",
                   G_CALLBACK(gtkprint_draw_page), &job);

  result = gtk_print_operation_run(operation,
                                   show_dialog ?
                                       GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG :
                                       GTK_PRINT_OPERATION_ACTION_PRINT,
                                   NULL, &error);

  if (result == GTK_PRINT_OPERATION_RESULT_ERROR)
  {
    g_message("Print: %s", error ? error->message : "printing failed");
    g_clear_error(&error);
  };

  gimp_progress_update(1.0);

  g_object_unref(operation);
  cairo_surface_destroy(job.surface);

  return (result == GTK_PRINT_OPERATION_RESULT_APPLY ||
          result == GTK_PRINT_OPERATION_RESULT_IN_PROGRESS ||
          result == GTK_PRINT_OPERATION_RESULT_CANCEL);
}


/*
 * End of "$Id$".
 */
