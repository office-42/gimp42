/*
 * "$Id$"
 *
 *   Print plug-in for the GIMP.
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
 *   main()                   - Main entry - just call gimp_main()...
 *   query()                  - Respond to a plug-in query...
 *   run()                    - Run the plug-in...
 *   print_dialog()           - Pop up the print dialog...
 *   dialog_create_ivalue()   - Create an integer value control...
 *   dialog_iscale_update()   - Update the value field using the scale.
 *   dialog_ientry_update()   - Update the value field using the text entry.
 *   print_driver_callback()  - Update the current printer driver...
 *   media_size_callback()    - Update the current media size...
 *   print_command_callback() - Update the print command...
 *   output_type_callback()   - Update the current output type...
 *   print_callback()         - Start the print...
 *   cancel_callback()        - Cancel the print...
 *   close_callback()         - Exit the print dialog application.
 *
 * Revision History:
 *
 *   $Log$
 *   Revision 1.10  1998/05/17 07:16:50  yosh
 *   0.99.31 fun
 *
 *   updated print plugin
 *
 *   -Yosh
 *
 *   Revision 1.21  1998/05/16  18:51:16  mike
 *   Updated LaserJet and PostScript profiles.
 *
 *   Revision 1.20  1998/05/16  18:27:59  mike
 *   Updated brightness LUT generation for correct calibration values.
 *   Updated Stylus Color density - a little too high.
 *
 *   Revision 1.19  1998/05/16  16:45:24  mike
 *   Added RGB/Grayscale gamma correction using the brightness (as well as the
 *   CMY[K] gamma correction for each printer)
 *
 *   Revision 1.18  1998/05/15  21:01:51  mike
 *   Top/left need to be in points.
 *   All code in preview_motion_callback() was still commented out...
 *   Updated DeskJet and Stylus color calibration values.
 *
 *   Revision 1.17  1998/05/11  23:56:05  mike
 *   Miscellaneous portability changes.
 *
 *   Revision 1.16  1998/05/08  20:52:55  mike
 *   Whoops, wasn't showing/hiding PPD file browse button.
 *
 *   Revision 1.15  1998/05/08  19:20:50  mike
 *   Updated for new driver interface.
 *   Added GUI for printer driver setup, PPD files.
 *   Now display file chooser when user selects "print to file"
 *   Added PPI/percent-of-page toggle for scaling.
 *   Added options for media type, resolution, and media source.
 *
 *   Revision 1.14  1998/03/01  17:29:42  mike
 *   Added LPC/LPR/LP/LPSTAT_COMMAND definitions for portability.
 *
 *   Revision 1.13  1998/01/22  15:06:31  mike
 *   Added "file" printer for printing to file.
 *   Now you don't need the "|" in front of print commands.
 *   Now "remembers" last selected printer.
 *
 *   Revision 1.12  1998/01/21  21:33:47  mike
 *   Added Level 2 PostScript driver.
 *   Fixed bug in dialog - didn't display correct output file/command
 *   and driver for the default printer.
 *
 *   Revision 1.11  1997/11/14  17:17:59  mike
 *   Updated to dynamically allocate return params in the run() function.
 *
 *   Revision 1.10  1997/11/12  15:57:48  mike
 *   Minor changes for clean compiles under Digital UNIX.
 *
 *   Revision 1.9  1997/10/22  13:07:20  mike
 *   Fixed typo in run() return status (thanks Michael Schubart!)
 *
 *   Revision 1.8  1997/10/02  17:57:26  mike
 *   Added printrc support.
 *   Added printer list (spooler support).
 *   Added gamma/dot gain correction values for all printers.
 *
 *   Revision 1.8  1997/10/02  17:57:26  mike
 *   Added printrc support.
 *   Added printer list (spooler support).
 *   Added gamma/dot gain correction values for all printers.
 *
 *   Revision 1.7  1997/07/30  20:33:05  mike
 *   Final changes for 1.1 release.
 *
 *   Revision 1.6  1997/07/30  18:47:39  mike
 *   Added scaling, orientation, and offset options.
 *   Added first cut at preview window.
 *
 *   Revision 1.5  1997/07/26  18:38:23  mike
 *   Whoops - wasn't grabbing the colormap for indexed images properly...
 *
 *   Revision 1.4  1997/07/03  13:13:26  mike
 *   Updated documentation for 1.0 release.
 *
 *   Revision 1.3  1997/07/03  13:07:05  mike
 *   Updated EPSON driver short names.
 *   Changed brightness lut formula for better control.
 *
 *   Revision 1.2  1997/07/02  15:22:17  mike
 *   Added GUI with printer/media/output selection controls.
 *
 *   Revision 1.1  1997/07/02  13:51:53  mike
 *   Initial revision
 */

/*
 * gimp42: ported to GTK 4 and GLib 2.  There is no popen() on Windows,
 * and on every platform a job now goes out in one of three ways:
 *
 *   - "File": the printer-language output is written to a file.
 *   - "System Printer": the image is printed through GtkPrintOperation,
 *     i.e. the native print dialog and printer drivers (print-gtk.c).
 *     This is the entry that makes printing work on Windows.
 *   - spooler queues found with lpstat/lpc (UNIX): the driver output is
 *     written to a temporary file and fed to the queue's command
 *     (lp/lpr) on its standard input with GSubprocess.
 */

#include "print.h"
#include <math.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include "libgimp/gimpui.h"


/*
 * Constants for GUI...
 */

#define SCALE_WIDTH		64
#define ENTRY_WIDTH		64
#define PREVIEW_SIZE		220	/* Assuming max media size of 22" */
#define MAX_PLIST		100
#define PLIST_FILE		0	/* plist[0] prints to a file */
#define PLIST_SYSTEM		1	/* plist[1] uses GtkPrintOperation */
#define PLIST_SPECIAL		2	/* Spooler queues start here */
#define SYSTEM_PRINTER		"<system printer>"
					/* output_to for the system printer */


/*
 * Types...
 */

typedef struct		/**** Printer List ****/
{
  char	name[17],		/* Name of printer */
	command[255],		/* Printer command */
	driver[33],		/* Short name of printer driver */
	ppd_file[255];		/* PPD file for printer */
  int	output_type;		/* Color/B&W */
  char	resolution[33],		/* Resolution */
	media_size[33],		/* Media size */
	media_type[33],		/* Media type */
	media_source[33];	/* Media source */
} plist_t;


/*
 * Local functions...
 */

static void	printrc_load(void);
static void	printrc_save(void);
static int	compare_printers(plist_t *p1, plist_t *p2);
static void	get_printers(void);

static void	query(void);
static void	run(char *, int, GParam *, int *, GParam **);
static int	do_print_dialog(void);
static int	run_print_command(char *command, char *filename);
static void	brightness_update(GtkAdjustment *);
static void	brightness_callback(GtkWidget *);
static void	scaling_update(GtkAdjustment *);
static void	scaling_callback(GtkWidget *);
static void	plist_callback(GtkWidget *, gpointer);
static void	media_size_callback(GtkWidget *, gpointer);
static void	media_type_callback(GtkWidget *, gpointer);
static void	media_source_callback(GtkWidget *, gpointer);
static void	resolution_callback(GtkWidget *, gpointer);
static void	output_type_callback(GtkWidget *, gpointer);
static void	orientation_callback(GtkWidget *, gpointer);
static void	print_callback(void);
static void	cancel_callback(void);
static void	close_callback(void);

static void	setup_open_callback(void);
static void	setup_ok_callback(void);
static void	setup_cancel_callback(void);
static gboolean	setup_close_callback(void);
static void	ppd_browse_callback(void);
static void	ppd_file_callback(const gchar *, gpointer);
static void	print_driver_callback(GtkWidget *, gpointer);

static void	file_callback(const gchar *, gpointer);

static void	preview_update(void);
static void	preview_draw(GtkDrawingArea *, cairo_t *, int, int, gpointer);
static void	preview_button_callback(GtkGestureDrag *, double, double,
		                        gpointer);
static void	preview_motion_callback(GtkGestureDrag *, double, double,
		                        gpointer);


/*
 * Globals...
 */

GPlugInInfo	PLUG_IN_INFO =		/* Plug-in information */
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

struct					/* Plug-in variables */
{
  char	output_to[255],		/* Name of file or command to print to */
	short_name[33],		/* Name of printer "driver" */
	ppd_file[255];		/* PPD file */
  gint	output_type;		/* Color or grayscale output */
  char	resolution[33],		/* Resolution */
	media_size[33],		/* Media size */
	media_type[33],		/* Media type */
	media_source[33];	/* Media source */
  gint	brightness;		/* Output brightness */
  float	scaling;		/* Scaling, percent of printable area */
  gint	orientation,		/* Orientation - 0 = port., 1 = land., -1 = auto */
	left,			/* Offset from lower-lefthand corner, points */
	top;			/* ... */
}		vars =
{
	"",			/* Name of file or command to print to */
	"ps2",			/* Name of printer "driver" */
	"",			/* Name of PPD file */
	OUTPUT_COLOR,		/* Color or grayscale output */
	"",			/* Output resolution */
	"",			/* Size of output media */
	"",			/* Type of output media */
	"",			/* Source of output media */
	100,			/* Output brightness */
	100.0,			/* Scaling (100% means entire printable area, */
				/*          -XXX means scale by PPI) */
	-1,			/* Orientation (-1 = automatic) */
	-1,			/* X offset (-1 = center) */
	-1			/* Y offset (-1 = center) */
};

GtkWidget	*print_dialog,		/* Print dialog window */
		*media_size,		/* Media size option button */
		*media_type,		/* Media type option button */
		*media_source,		/* Media source option button */
		*resolution,		/* Resolution option button */
		*scaling_scale,		/* Scale widget for scaling */
		*scaling_entry,		/* Text entry widget for scaling */
		*scaling_percent,	/* Scale by percent */
		*scaling_ppi,		/* Scale by pixels-per-inch */
		*brightness_scale,	/* Scale for brightness */
		*brightness_entry,	/* Text entry widget for brightness */
		*output_gray,		/* Output type toggle, black */
		*output_color,		/* Output type toggle, color */
		*setup_dialog,		/* Setup dialog window */
		*printer_driver,	/* Printer driver widget */
		*ppd_file,		/* PPD file entry */
		*ppd_button,		/* PPD file browse button */
		*output_cmd;		/* Output command text entry */

GtkAdjustment	*scaling_adjustment,	/* Adjustment object for scaling */
		*brightness_adjustment;	/* Adjustment object for brightness */

int		num_media_sizes=0;	/* Number of media sizes */
char		**media_sizes;		/* Media size strings */
int		num_media_types=0;	/* Number of media types */
char		**media_types;		/* Media type strings */
int		num_media_sources=0;	/* Number of media sources */
char		**media_sources;	/* Media source strings */
int		num_resolutions=0;	/* Number of resolutions */
char		**resolutions;		/* Resolution strings */

GtkWidget	*preview;		/* Preview drawing area widget */
int		mouse_x,		/* Last mouse X */
		mouse_y;		/* Last mouse Y */
int		image_width,		/* Width of image */
		image_height,		/* Height of image */
		page_left,		/* Left pixel column of page */
		page_top,		/* Top pixel row of page */
		page_width,		/* Width of page on screen */
		page_height,		/* Height of page on screen */
		print_width,		/* Printed width of image */
		print_height;		/* Printed height of image */

int		plist_current = 0,	/* Current system printer */
		plist_count = 0;	/* Number of system printers */
plist_t		plist[MAX_PLIST];	/* System printers */

int		runme = FALSE,		/* True if print should proceed */
		current_printer = 0;	/* Current printer index */

printer_t	printers[] =		/* List of supported printer types */
{
  { "PostScript Level 1",	"ps",		1,	0,	1.000,	1.000,
    ps_parameters,	ps_media_size,	ps_imageable_area,	ps_print },
  { "PostScript Level 2",	"ps2",		1,	1,	1.000,	1.000,
    ps_parameters,	ps_media_size,	ps_imageable_area,	ps_print },
  { "HP DeskJet 500, 520",	"pcl-500",	0,	500,	0.818,	0.786,
    pcl_parameters,	default_media_size,	pcl_imageable_area,	pcl_print },
  { "HP DeskJet 500C, 540C",	"pcl-501",	1,	501,	0.818,	0.786,
    pcl_parameters,	default_media_size,	pcl_imageable_area,	pcl_print },
  { "HP DeskJet 550C, 560C",	"pcl-550",	1,	550,	0.818,	0.786,
    pcl_parameters,	default_media_size,	pcl_imageable_area,	pcl_print },
  { "HP DeskJet 600 series",	"pcl-600",	1,	600,	0.818,	0.786,
    pcl_parameters,	default_media_size,	pcl_imageable_area,	pcl_print },
  { "HP DeskJet 800 series",	"pcl-800",	1,	800,	0.818,	0.786,
    pcl_parameters,	default_media_size,	pcl_imageable_area,	pcl_print },
  { "HP DeskJet 1100C, 1120C",	"pcl-1100",	1,	1100,	0.818,	0.786,
    pcl_parameters,	default_media_size,	pcl_imageable_area,	pcl_print },
  { "HP DeskJet 1200C, 1600C",	"pcl-1200",	1,	1200,	0.818,	0.786,
    pcl_parameters,	default_media_size,	pcl_imageable_area,	pcl_print },
  { "HP LaserJet II series",	"pcl-2",	0,	2,	1.000,	0.596,
    pcl_parameters,	default_media_size,	pcl_imageable_area,	pcl_print },
  { "HP LaserJet III series",	"pcl-3",	0,	3,	1.000,	0.596,
    pcl_parameters,	default_media_size,	pcl_imageable_area,	pcl_print },
  { "HP LaserJet 4 series",	"pcl-4",	0,	4,	1.000,	0.615,
    pcl_parameters,	default_media_size,	pcl_imageable_area,	pcl_print },
  { "HP LaserJet 4V, 4Si",	"pcl-4v",	0,	5,	1.000,	0.615,
    pcl_parameters,	default_media_size,	pcl_imageable_area,	pcl_print },
  { "HP LaserJet 5 series",	"pcl-5",	0,	4,	1.000,	0.615,
    pcl_parameters,	default_media_size,	pcl_imageable_area,	pcl_print },
  { "HP LaserJet 5Si",		"pcl-5si",	0,	5,	1.000,	0.615,
    pcl_parameters,	default_media_size,	pcl_imageable_area,	pcl_print },
  { "HP LaserJet 6 series",	"pcl-6",	0,	4,	1.000,	0.615,
    pcl_parameters,	default_media_size,	pcl_imageable_area,	pcl_print },
  { "EPSON Stylus Color",	"escp2",	1,	0,	0.597,	0.568,
    escp2_parameters,	default_media_size,	escp2_imageable_area,	escp2_print },
  { "EPSON Stylus Color Pro",	"escp2-pro",	1,	1,	0.597,	0.631,
    escp2_parameters,	default_media_size,	escp2_imageable_area,	escp2_print },
  { "EPSON Stylus Color Pro XL","escp2-proxl",	1,	1,	0.597,	0.631,
    escp2_parameters,	default_media_size,	escp2_imageable_area,	escp2_print },
  { "EPSON Stylus Color 1500",	"escp2-1500",	1,	2,	0.597,	0.631,
    escp2_parameters,	default_media_size,	escp2_imageable_area,	escp2_print },
  { "EPSON Stylus Color 400",	"escp2-400",	1,	1,	0.585,	0.646,
    escp2_parameters,	default_media_size,	escp2_imageable_area,	escp2_print },
  { "EPSON Stylus Color 500",	"escp2-500",	1,	1,	0.597,	0.631,
    escp2_parameters,	default_media_size,	escp2_imageable_area,	escp2_print },
  { "EPSON Stylus Color 600",	"escp2-600",	1,	3,	0.585,	0.646,
    escp2_parameters,	default_media_size,	escp2_imageable_area,	escp2_print },
  { "EPSON Stylus Color 800",	"escp2-800",	1,	4,	0.585,	0.646,
    escp2_parameters,	default_media_size,	escp2_imageable_area,	escp2_print },
  { "EPSON Stylus Color 1520",	"escp2-1520",	1,	5,	0.585,	0.646,
    escp2_parameters,	default_media_size,	escp2_imageable_area,	escp2_print },
  { "EPSON Stylus Color 3000",	"escp2-3000",	1,	5,	0.585,	0.646,
    escp2_parameters,	default_media_size,	escp2_imageable_area,	escp2_print }
};


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
    { PARAM_STRING,	"output_to",	"Print command or filename (| to pipe to command)" },
    { PARAM_STRING,	"driver",	"Printer driver short name" },
    { PARAM_STRING,	"ppd_file",	"PPD file" },
    { PARAM_INT32,	"output_type",	"Output type (0 = gray, 1 = color)" },
    { PARAM_STRING,	"resolution",	"Resolution (\"300\", \"720\", etc.)" },
    { PARAM_STRING,	"media_size",	"Media size (\"Letter\", \"A4\", etc.)" },
    { PARAM_STRING,	"media_type",	"Media type (\"Plain\", \"Glossy\", etc.)" },
    { PARAM_STRING,	"media_source",	"Media source (\"Tray1\", \"Manual\", etc.)" },
    { PARAM_INT32,	"brightness",	"Brightness (0-200%)" },
    { PARAM_FLOAT,	"scaling",	"Output scaling (0-100%, -PPI)" },
    { PARAM_INT32,	"orientation",	"Output orientation (-1 = auto, 0 = portrait, 1 = landscape)" },
    { PARAM_INT32,	"left",		"Left offset (points, -1 = centered)" },
    { PARAM_INT32,	"top",		"Top offset (points, -1 = centered)" }
  };
  static int		nargs = sizeof(args) / sizeof(args[0]);


  gimp_install_procedure(
      "file_print",
      "This plug-in prints images from The GIMP.",
      "Prints images to PostScript, PCL, or ESC/P2 printers.",
      "Michael Sweet <mike@easysw.com>",
      "Copyright 1997-1998 by Michael Sweet",
      PLUG_IN_VERSION,
      "<Image>/File/Print",
      "RGB*,GRAY*,INDEXED*",
      PROC_PLUG_IN,
      nargs,
      0,
      args,
      NULL);
}


/*
 * 'run()' - Run the plug-in...
 */

static void
run(char   *name,		/* I - Name of print program. */
    int    nparams,		/* I - Number of parameters passed in */
    GParam *param,		/* I - Parameter values */
    int    *nreturn_vals,	/* O - Number of return values */
    GParam **return_vals)	/* O - Return values */
{
  GDrawable	*drawable;	/* Drawable for image */
  GRunModeType	run_mode;	/* Current run mode */
  FILE		*prn;		/* Print file/command */
  printer_t	*printer;	/* Printer driver entry */
  int		i;		/* Looping var */
  float		brightness,	/* Computed brightness */
		screen_gamma,	/* Screen gamma correction */
		print_gamma,	/* Printer gamma correction */
		density,	/* Printer density */
		pixel;		/* Pixel value */
  char		*tmpname;	/* Temporary file for print commands */
  int		fd,		/* Temporary file descriptor */
		media_width,	/* Media width, points */
		media_length;	/* Media length, points */
  guchar	lut[256];	/* Lookup table for brightness */
  guchar	*cmap;		/* Colormap (indexed images only) */
  int		ncolors;	/* Number of colors in colormap */
  GParam	*values;	/* Return values */


 /*
  * Initialize parameter data...
  */

  run_mode = param[0].data.d_int32;

  values = g_new(GParam, 1);

  values[0].type          = PARAM_STATUS;
  values[0].data.d_status = STATUS_SUCCESS;

  *nreturn_vals = 1;
  *return_vals  = values;

 /*
  * Get drawable...
  */

  drawable = gimp_drawable_get(param[2].data.d_drawable);

  image_width  = drawable->width;
  image_height = drawable->height;

 /*
  * See how we will run
  */

  switch (run_mode)
  {
    case RUN_INTERACTIVE :
       /*
        * Possibly retrieve data...
        */

        gimp_get_data(PLUG_IN_NAME, &vars);

        for (i = 0; i < (sizeof(printers) / sizeof(printers[0])); i ++)
          if (strcmp(printers[i].short_name, vars.short_name) == 0)
            current_printer = i;

       /*
        * Get information from the dialog...
        */

	if (!do_print_dialog())
          return;
        break;

    case RUN_NONINTERACTIVE :
       /*
        * Make sure all the arguments are present...
        */

        if (nparams < 11)
	  values[0].data.d_status = STATUS_CALLING_ERROR;
	else
	{
	 /*
	  * The strings come from the caller: truncate them to the buffers.
	  */

#define PRINT_COPY_PARAM(dst, n) \
	  g_strlcpy((dst), param[n].data.d_string ? param[n].data.d_string : "", \
	            sizeof(dst))

	  PRINT_COPY_PARAM(vars.output_to, 3);
	  PRINT_COPY_PARAM(vars.short_name, 4);
	  PRINT_COPY_PARAM(vars.ppd_file, 5);
	  vars.output_type = param[6].data.d_int32;
	  PRINT_COPY_PARAM(vars.resolution, 7);
	  PRINT_COPY_PARAM(vars.media_size, 8);
	  PRINT_COPY_PARAM(vars.media_type, 9);
	  PRINT_COPY_PARAM(vars.media_source, 10);

#undef PRINT_COPY_PARAM

          if (nparams > 11)
	    vars.brightness = param[11].data.d_int32;
	  else
	    vars.brightness = 100;

          if (nparams > 12)
            vars.scaling = param[12].data.d_float;
          else
            vars.scaling = 100.0;

          if (nparams > 13)
            vars.orientation = param[13].data.d_int32;
          else
            vars.orientation = -1;

          if (nparams > 14)
            vars.left = param[14].data.d_int32;
          else
            vars.left = -1;

          if (nparams > 15)
            vars.top = param[15].data.d_int32;
          else
            vars.top = -1;
	};

        for (i = 0; i < (sizeof(printers) / sizeof(printers[0])); i ++)
          if (strcmp(printers[i].short_name, vars.short_name) == 0)
            current_printer = i;
        break;

    case RUN_WITH_LAST_VALS :
       /*
        * Possibly retrieve data...
        */

	gimp_get_data(PLUG_IN_NAME, &vars);

        for (i = 0; i < (sizeof(printers) / sizeof(printers[0])); i ++)
          if (strcmp(printers[i].short_name, vars.short_name) == 0)
            current_printer = i;
	break;

    default :
        values[0].data.d_status = STATUS_CALLING_ERROR;
        break;;
  };

 /*
  * Without the dialog, output_to says where the job goes: the system
  * printer, a command ("|command"), one of the known queues, or a file.
  */

  if (run_mode != RUN_INTERACTIVE &&
      values[0].data.d_status == STATUS_SUCCESS)
  {
    if (strcmp(vars.output_to, SYSTEM_PRINTER) == 0)
      plist_current = PLIST_SYSTEM;
    else if (vars.output_to[0] == '|')
    {
      memmove(vars.output_to, vars.output_to + 1, strlen(vars.output_to));
      plist_current = PLIST_SPECIAL;
    }
    else
    {
      i = current_printer;
      printrc_load();
      current_printer = i;
    };
  };

 /*
  * Print the image...
  */

  if (values[0].data.d_status == STATUS_SUCCESS)
  {
   /*
    * Set the tile cache size...
    */

    if (drawable->height > drawable->width)
      gimp_tile_cache_ntiles((drawable->height + gimp_tile_width() - 1) /
                             gimp_tile_width() + 1);
    else
      gimp_tile_cache_ntiles((drawable->width + gimp_tile_width() - 1) /
                             gimp_tile_width() + 1);

   /*
    * Open the file, or a temporary file for the print command...
    */

    tmpname = NULL;

    if (plist_current == PLIST_SYSTEM)
      prn = NULL;
    else if (plist_current == PLIST_FILE)
      prn = g_fopen(vars.output_to, "wb");
    else if ((fd = g_file_open_tmp("gimp-print-XXXXXX", &tmpname, NULL)) >= 0)
    {
      g_close(fd, NULL);
      prn = g_fopen(tmpname, "wb");
    }
    else
      prn = NULL;

    if (prn != NULL || plist_current == PLIST_SYSTEM)
    {
     /*
      * Got an output file/command, now compute a brightness lookup table...
      * (the system printer's own driver does the printer calibration)
      */

      printer      = printers + current_printer;
      brightness   = 100.0 / vars.brightness;
      screen_gamma = gimp_gamma() * brightness / 1.7;
      print_gamma  = 1.0 / printer->gamma;
      density      = printer->density;

      if (plist_current == PLIST_SYSTEM)
      {
        print_gamma = 1.0;
        density     = 1.0;
      };

      for (i = 0; i < 256; i ++)
      {
        pixel = 1.0 - pow((float)i / 255.0, screen_gamma);
        pixel = 255.5 - 255.0 * density *
  	        	pow(brightness * pixel, print_gamma);

	if (pixel <= 0.0)
	  lut[i] = 0;
	else if (pixel >= 255.0)
	  lut[i] = 255;
	else
	  lut[i] = (int)pixel;
      };

     /*
      * Is the image an Indexed type?  If so we need the colormap...
      */

      if (gimp_image_base_type(param[1].data.d_image) == INDEXED)
        cmap = gimp_image_get_cmap(param[1].data.d_image, &ncolors);
      else
      {
        cmap    = NULL;
        ncolors = 0;
      };

     /*
      * Finally, call the print driver to send the image to the printer and
      * close the output file/command...
      */

      if (plist_current == PLIST_SYSTEM)
      {
        (*printer->media_size)(printer->model, vars.ppd_file, vars.media_size,
                               &media_width, &media_length);

        if (run_mode != RUN_INTERACTIVE)
          gtk_init();

        if (!gtkprint_print(vars.media_size, media_width, media_length,
                            vars.output_type, vars.orientation, vars.scaling,
                            vars.left, vars.top, run_mode == RUN_INTERACTIVE,
                            drawable, lut, cmap))
          values[0].data.d_status = STATUS_EXECUTION_ERROR;
      }
      else
      {
        (*printer->print)(printer->model, vars.ppd_file, vars.resolution,
                          vars.media_size, vars.media_type, vars.media_source,
                          vars.output_type, vars.orientation, vars.scaling,
                          vars.left, vars.top, 1, prn, drawable, lut, cmap);

        fclose(prn);

        if (tmpname != NULL &&
            !run_print_command(vars.output_to, tmpname))
          values[0].data.d_status = STATUS_EXECUTION_ERROR;
      };
    }
    else
      values[0].data.d_status = STATUS_EXECUTION_ERROR;

    if (tmpname != NULL)
    {
      g_unlink(tmpname);
      g_free(tmpname);
    };

   /*
    * Store data...
    */

    if (run_mode == RUN_INTERACTIVE)
      gimp_set_data(PLUG_IN_NAME, &vars, sizeof(vars));
  };

 /*
  * Detach from the drawable...
  */

  gimp_drawable_detach(drawable);
}


/*
 * 'run_print_command()' - Send a print file to a spooler command.
 *
 * The command reads the job on its standard input, as it did when the
 * plug-in wrote to it through popen().
 */

static int
run_print_command(char *command,	/* I - Print command */
                  char *filename)	/* I - File holding the job */
{
  gchar		**argv;			/* Command arguments */
  gchar		*contents;		/* Print job */
  gsize		length;			/* Length of print job */
  GBytes	*job;			/* Print job for the command */
  GSubprocess	*process;		/* Print command */
  GError	*error = NULL;
  int		status;


  if (!g_shell_parse_argv(command, NULL, &argv, &error))
  {
    g_message("Print: bad print command \"%s\": %s", command, error->message);
    g_error_free(error);
    return (FALSE);
  };

  if (!g_file_get_contents(filename, &contents, &length, &error))
  {
    g_message("Print: %s", error->message);
    g_error_free(error);
    g_strfreev(argv);
    return (FALSE);
  };

  job     = g_bytes_new_take(contents, length);
  process = g_subprocess_newv((const gchar * const *)argv,
                              G_SUBPROCESS_FLAGS_STDIN_PIPE, &error);
  status  = FALSE;

  if (process != NULL &&
      g_subprocess_communicate(process, job, NULL, NULL, NULL, &error) &&
      g_subprocess_get_successful(process))
    status = TRUE;

  if (error != NULL)
  {
    g_message("Print: \"%s\" failed: %s", command, error->message);
    g_error_free(error);
  };

  if (process != NULL)
    g_object_unref(process);
  g_bytes_unref(job);
  g_strfreev(argv);

  return (status);
}


/*
 * 'dialog_label()' - Add a right-aligned label to a table.
 */

static void
dialog_label(GtkWidget *table,		/* I - Table */
             char      *text,		/* I - Label text */
             int       left,		/* I - Column */
             int       top)		/* I - Row */
{
  GtkWidget	*label;


  label = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(label), 1.0);
  gtk_label_set_yalign(GTK_LABEL(label), 0.5);
  gimp_table_attach(table, label, left, left + 1, top, top + 1,
                    GIMP_FILL, GIMP_FILL, 0, 0);
}


/*
 * 'dialog_box()' - Add a box to a table.
 */

static GtkWidget *
dialog_box(GtkWidget *table,		/* I - Table */
           int       spacing,		/* I - Spacing in box */
           int       left,		/* I - First column */
           int       right,		/* I - Last column + 1 */
           int       top)		/* I - Row */
{
  GtkWidget	*box;


  box = gimp_hbox_new(FALSE, spacing);
  gimp_table_attach(table, box, left, right, top, top + 1,
                    GIMP_FILL, GIMP_FILL, 0, 0);

  return (box);
}


/*
 * 'do_print_dialog()' - Pop up the print dialog...
 */

int
do_print_dialog(void)
{
  int		i;		/* Looping var */
  char		s[100];		/* Text string */
  GtkWidget	*dialog,	/* Dialog window */
		*table,		/* Table "container" for controls */
		*button,	/* OK/Cancel buttons */
		*scale,		/* Scale widget */
		*entry,		/* Text entry widget */
		*option,	/* Option menu button */
		*box;		/* Box container */
  GtkAdjustment	*scale_data;	/* Scale data (limits) */
  GtkGesture	*drag;		/* Dragging in the preview */
  static char	*orients[] =	/* Orientation strings */
  {
    "Auto",
    "Portrait",
    "Landscape"
  };


 /*
  * Initialize the program's display...
  */

  gtk_init();

 /*
  * Get printrc options...
  */

  printrc_load();

 /*
  * Print dialog window...
  */

  print_dialog = dialog = gimp_dialog_new("Print " PLUG_IN_VERSION);
  g_signal_connect(dialog, "destroy",
		   G_CALLBACK(close_callback), NULL);

 /*
  * Top-level table for dialog...
  */

  table = gimp_table_new(9, 4, FALSE);
  gimp_container_set_border_width(table, 6);
  gtk_grid_set_column_spacing(GTK_GRID(table), 4);
  gtk_grid_set_row_spacing(GTK_GRID(table), 8);
  gtk_box_append(GTK_BOX(gimp_dialog_get_vbox(dialog)), table);

 /*
  * Drawing area for page preview...
  */

  preview = gtk_drawing_area_new();
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(preview), PREVIEW_SIZE);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(preview), PREVIEW_SIZE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(preview), preview_draw,
                                 NULL, NULL);
  gimp_table_attach(table, preview, 0, 2, 0, 7,
                    GIMP_FILL, GIMP_FILL, 0, 0);

  drag = gtk_gesture_drag_new();
  g_signal_connect(drag, "drag-begin",
		   G_CALLBACK(preview_button_callback), NULL);
  g_signal_connect(drag, "drag-update",
		   G_CALLBACK(preview_motion_callback), NULL);
  gtk_widget_add_controller(preview, GTK_EVENT_CONTROLLER(drag));

 /*
  * Media size option menu...
  */

  dialog_label(table, "Media Size:", 2, 1);
  box = dialog_box(table, 0, 3, 4, 1);

  media_size = option = gimp_option_menu_new();
  gtk_box_append(GTK_BOX(box), option);

 /*
  * Media type option menu...
  */

  dialog_label(table, "Media Type:", 2, 2);
  box = dialog_box(table, 0, 3, 4, 2);

  media_type = option = gimp_option_menu_new();
  gtk_box_append(GTK_BOX(box), option);

 /*
  * Media source option menu...
  */

  dialog_label(table, "Media Source:", 2, 3);
  box = dialog_box(table, 0, 3, 4, 3);

  media_source = option = gimp_option_menu_new();
  gtk_box_append(GTK_BOX(box), option);

 /*
  * Orientation option menu...
  */

  dialog_label(table, "Orientation:", 2, 4);
  box = dialog_box(table, 0, 3, 4, 4);

  option = gimp_option_menu_new();
  for (i = 0; i < (int)(sizeof(orients) / sizeof(orients[0])); i ++)
    gimp_option_menu_append(option, orients[i],
                            G_CALLBACK(orientation_callback),
                            GINT_TO_POINTER(i - 1));
  gtk_box_append(GTK_BOX(box), option);
  gimp_option_menu_set_history(option, vars.orientation + 1);

 /*
  * Resolution option menu...
  */

  dialog_label(table, "Resolution:", 2, 5);
  box = dialog_box(table, 0, 3, 4, 5);

  resolution = option = gimp_option_menu_new();
  gtk_box_append(GTK_BOX(box), option);

 /*
  * Output type toggles...
  */

  dialog_label(table, "Output Type:", 2, 6);
  box = dialog_box(table, 8, 3, 4, 6);

  output_gray = button = gimp_radio_button_new(NULL, "B&W");
  if (vars.output_type == 0)
    gtk_check_button_set_active(GTK_CHECK_BUTTON(button), TRUE);
  g_signal_connect(button, "toggled",
		   G_CALLBACK(output_type_callback),
		   GINT_TO_POINTER(OUTPUT_GRAY));
  gtk_box_append(GTK_BOX(box), button);

  output_color = button = gimp_radio_button_new(output_gray, "Color");
  if (vars.output_type == 1)
    gtk_check_button_set_active(GTK_CHECK_BUTTON(button), TRUE);
  g_signal_connect(button, "toggled",
		   G_CALLBACK(output_type_callback),
		   GINT_TO_POINTER(OUTPUT_COLOR));
  gtk_box_append(GTK_BOX(box), button);

 /*
  * Scaling...
  */

  dialog_label(table, "Scaling:", 0, 7);
  box = dialog_box(table, 8, 1, 4, 7);

  if (vars.scaling < 0.0)
    scaling_adjustment = scale_data =
	gtk_adjustment_new(-vars.scaling, 50.0, 1201.0, 1.0, 1.0, 1.0);
  else
    scaling_adjustment = scale_data =
	gtk_adjustment_new(vars.scaling, 5.0, 101.0, 1.0, 1.0, 1.0);

  g_signal_connect(scale_data, "value-changed",
		   G_CALLBACK(scaling_update), NULL);

  scaling_scale = scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, scale_data);
  gtk_box_append(GTK_BOX(box), scale);
  gtk_widget_set_size_request(scale, 200, -1);
  gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);

  scaling_entry = entry = gtk_entry_new();
  sprintf(s, "%.1f", fabs(vars.scaling));
  gtk_editable_set_text(GTK_EDITABLE(entry), s);
  g_signal_connect(entry, "changed",
                   G_CALLBACK(scaling_callback), NULL);
  gtk_box_append(GTK_BOX(box), entry);
  gtk_editable_set_width_chars(GTK_EDITABLE(entry), 6);
  gtk_widget_set_size_request(entry, 60, -1);

  scaling_percent = button = gimp_radio_button_new(NULL, "Percent");
  if (vars.scaling > 0.0)
    gtk_check_button_set_active(GTK_CHECK_BUTTON(button), TRUE);
  g_signal_connect(button, "toggled",
                   G_CALLBACK(scaling_callback), NULL);
  gtk_box_append(GTK_BOX(box), button);

  scaling_ppi = button = gimp_radio_button_new(scaling_percent, "PPI");
  if (vars.scaling < 0.0)
    gtk_check_button_set_active(GTK_CHECK_BUTTON(button), TRUE);
  g_signal_connect(button, "toggled",
                   G_CALLBACK(scaling_callback), NULL);
  gtk_box_append(GTK_BOX(box), button);

 /*
  * Brightness slider...
  */

  dialog_label(table, "Brightness:", 0, 8);
  box = dialog_box(table, 8, 1, 4, 8);

  brightness_adjustment = scale_data =
      gtk_adjustment_new((float)vars.brightness, 50.0, 201.0, 1.0, 1.0, 1.0);

  g_signal_connect(scale_data, "value-changed",
		   G_CALLBACK(brightness_update), NULL);

  brightness_scale = scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL,
                                           scale_data);
  gtk_box_append(GTK_BOX(box), scale);
  gtk_widget_set_size_request(scale, 200, -1);
  gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);

  brightness_entry = entry = gtk_entry_new();
  sprintf(s, "%d", vars.brightness);
  gtk_editable_set_text(GTK_EDITABLE(entry), s);
  g_signal_connect(entry, "changed",
		   G_CALLBACK(brightness_callback), NULL);
  gtk_box_append(GTK_BOX(box), entry);
  gtk_editable_set_width_chars(GTK_EDITABLE(entry), 4);
  gtk_widget_set_size_request(entry, 40, -1);

 /*
  * Printer option menu...
  */

  dialog_label(table, "Printer:", 2, 0);
  box = dialog_box(table, 8, 3, 4, 0);

  option = gimp_option_menu_new();
  for (i = 0; i < plist_count; i ++)
    gimp_option_menu_append(option, plist[i].name,
                            G_CALLBACK(plist_callback),
                            GINT_TO_POINTER(i));
  gtk_box_append(GTK_BOX(box), option);
  gimp_option_menu_set_history(option, plist_current);

  button = gtk_button_new_with_label(" Setup ");
  gtk_box_append(GTK_BOX(box), button);
  g_signal_connect(button, "clicked",
		   G_CALLBACK(setup_open_callback), NULL);

 /*
  * Print, cancel buttons...
  */

  gimp_dialog_add_button(dialog, " Print ", G_CALLBACK(print_callback),
                         NULL, TRUE);
  button = gimp_dialog_add_button(dialog, " Cancel ", NULL, NULL, FALSE);
  g_signal_connect_swapped(button, "clicked",
                           G_CALLBACK(cancel_callback), NULL);

 /*
  * Setup dialog window...  It is hidden, not destroyed, when closed.
  */

  setup_dialog = dialog = gimp_dialog_new("Setup");
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(print_dialog));
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_hide_on_close(GTK_WINDOW(dialog), TRUE);
  g_signal_connect(dialog, "close-request",
		   G_CALLBACK(setup_close_callback), NULL);

 /*
  * Top-level table for dialog...
  */

  table = gimp_table_new(3, 2, FALSE);
  gimp_container_set_border_width(table, 6);
  gtk_grid_set_column_spacing(GTK_GRID(table), 4);
  gtk_grid_set_row_spacing(GTK_GRID(table), 8);
  gtk_box_append(GTK_BOX(gimp_dialog_get_vbox(dialog)), table);

 /*
  * Printer driver option menu...
  */

  dialog_label(table, "Driver:", 0, 0);

  printer_driver = option = gimp_option_menu_new();
  for (i = 0; i < (int)(sizeof(printers) / sizeof(printers[0])); i ++)
    gimp_option_menu_append(option, printers[i].long_name,
                            G_CALLBACK(print_driver_callback),
                            GINT_TO_POINTER(i));
  gimp_table_attach(table, option, 1, 2, 0, 1, GIMP_FILL, GIMP_FILL, 0, 0);

 /*
  * PPD file...
  */

  dialog_label(table, "PPD File:", 0, 1);
  box = dialog_box(table, 8, 1, 2, 1);

  ppd_file = entry = gtk_entry_new();
  gimp_box_pack_start(box, entry, TRUE, TRUE, 0);

  ppd_button = button = gtk_button_new_with_label(" Browse ");
  gtk_box_append(GTK_BOX(box), button);
  g_signal_connect(button, "clicked",
		   G_CALLBACK(ppd_browse_callback), NULL);

 /*
  * Print command...
  */

  dialog_label(table, "Command:", 0, 2);

  output_cmd = entry = gtk_entry_new();
  gimp_table_attach(table, entry, 1, 2, 2, 3, GIMP_FILL, GIMP_FILL, 0, 0);

 /*
  * OK, cancel buttons...
  */

  gimp_dialog_add_button(dialog, " OK ", G_CALLBACK(setup_ok_callback),
                         NULL, TRUE);
  gimp_dialog_add_button(dialog, " Cancel ", G_CALLBACK(setup_cancel_callback),
                         NULL, FALSE);

 /*
  * Show the main dialog and wait for the user to do something...
  */

  plist_callback(NULL, GINT_TO_POINTER(plist_current));

  gtk_window_present(GTK_WINDOW(print_dialog));

  gimp_main_loop_run();

 /*
  * Set printrc options...
  */

  printrc_save();

 /*
  * Return ok/cancel...
  */

  return (runme);
}


/*
 * 'brightness_update()' - Update the brightness field using the scale.
 */

static void
brightness_update(GtkAdjustment *adjustment)	/* I - New value */
{
  char	s[255];					/* Text buffer */


  if (vars.brightness != gtk_adjustment_get_value(adjustment))
  {
    vars.brightness = gtk_adjustment_get_value(adjustment);

    sprintf(s, "%d", vars.brightness);

    g_signal_handlers_block_by_func(brightness_entry,
                                    G_CALLBACK(brightness_callback), NULL);
    gtk_editable_set_text(GTK_EDITABLE(brightness_entry), s);
    g_signal_handlers_unblock_by_func(brightness_entry,
                                      G_CALLBACK(brightness_callback), NULL);

    preview_update();
  };
}


/*
 * 'brightness_callback()' - Update the brightness scale using the text entry.
 */

static void
brightness_callback(GtkWidget *widget)	/* I - Entry widget */
{
  gint		new_value;		/* New scaling value */


  new_value = atoi(gtk_editable_get_text(GTK_EDITABLE(widget)));

  if (vars.brightness != new_value)
  {
    if ((new_value >= gtk_adjustment_get_lower(brightness_adjustment)) &&
	(new_value < gtk_adjustment_get_upper(brightness_adjustment)))
      gtk_adjustment_set_value(brightness_adjustment, new_value);
  };
}


/*
 * 'scaling_update()' - Update the scaling field using the scale.
 */

static void
scaling_update(GtkAdjustment *adjustment)	/* I - New value */
{
  char	s[255];					/* Text buffer */
  gdouble value = gtk_adjustment_get_value(adjustment);


  if (vars.scaling != value)
  {
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(scaling_ppi)))
      vars.scaling = -value;
    else
      vars.scaling = value;

    sprintf(s, "%.1f", value);

    g_signal_handlers_block_by_func(scaling_entry,
                                    G_CALLBACK(scaling_callback), NULL);
    gtk_editable_set_text(GTK_EDITABLE(scaling_entry), s);
    g_signal_handlers_unblock_by_func(scaling_entry,
                                      G_CALLBACK(scaling_callback), NULL);

    preview_update();
  };
}


/*
 * 'scaling_callback()' - Update the scaling scale using the text entry.
 */

static void
scaling_callback(GtkWidget *widget)	/* I - Entry widget */
{
  gfloat	new_value;		/* New scaling value */


  if (widget == scaling_entry)
  {
    new_value = atof(gtk_editable_get_text(GTK_EDITABLE(widget)));

    if (vars.scaling != new_value)
    {
      if ((new_value >= gtk_adjustment_get_lower(scaling_adjustment)) &&
	  (new_value < gtk_adjustment_get_upper(scaling_adjustment)))
	gtk_adjustment_set_value(scaling_adjustment, new_value);
    };
  }
  else if (!gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)))
  {
   /*
    * The radio button that was just turned off - the other one does the
    * work...
    */
  }
  else if (widget == scaling_ppi)
  {
    vars.scaling = 0.0;
    gtk_adjustment_configure(scaling_adjustment, 72.0, 50.0, 1201.0,
                             1.0, 1.0, 1.0);
    scaling_update(scaling_adjustment);
  }
  else if (widget == scaling_percent)
  {
    vars.scaling = 0.0;
    gtk_adjustment_configure(scaling_adjustment, 100.0, 5.0, 101.0,
                             1.0, 1.0, 1.0);
    scaling_update(scaling_adjustment);
  };
}


/*
 * 'plist_build_menu()' - Build an option menu for the given parameters...
 */

static void
plist_build_menu(GtkWidget *option,				/* I - Option button */
                 int       num_items,				/* I - Number of items */
                 char      **items,				/* I - Menu items */
                 char      *cur_item,				/* I - Current item */
                 void      (*callback)(GtkWidget *, gpointer))	/* I - Callback */
{
  int		i;	/* Looping var */


  gimp_option_menu_clear(option);

  if (num_items == 0)
  {
    gtk_widget_set_visible(option, FALSE);
    return;
  };

  for (i = 0; i < num_items; i ++)
    gimp_option_menu_append(option, items[i], G_CALLBACK(callback),
                            GINT_TO_POINTER(i));

#ifdef DEBUG
  printf("cur_item = \'%s\'\n", cur_item);
#endif /* DEBUG */

  for (i = 0; i < num_items; i ++)
  {
#ifdef DEBUG
    printf("item[%d] = \'%s\'\n", i, items[i]);
#endif /* DEBUG */

    if (strcmp(items[i], cur_item) == 0)
    {
      gimp_option_menu_set_history(option, i);
      break;
    };
  };

  if (i == num_items)
  {
    gimp_option_menu_set_history(option, 0);
    (*callback)(option, GINT_TO_POINTER(0));
  };

  gtk_widget_set_visible(option, TRUE);
}


/*
 * 'plist_callback()' - Update the current system printer...
 */

static void
plist_callback(GtkWidget *widget,	/* I - Driver option menu */
               gpointer  data)		/* I - Data */
{
  int		i;			/* Looping var */
  printer_t	*printer;		/* Printer driver entry */
  plist_t	*p;


  plist_current = GPOINTER_TO_INT(data);
  p             = plist + plist_current;

  if (p->driver[0] != '\0')
  {
    strcpy(vars.short_name, p->driver);

    for (i = 0; i < (sizeof(printers) / sizeof(printers[0])); i ++)
      if (strcmp(printers[i].short_name, vars.short_name) == 0)
      {
        current_printer = i;
        break;
      };
  };

  strcpy(vars.ppd_file, p->ppd_file);
  strcpy(vars.media_size, p->media_size);
  strcpy(vars.media_type, p->media_type);
  strcpy(vars.media_source, p->media_source);
  strcpy(vars.resolution, p->resolution);
  strcpy(vars.output_to, p->command);

  if (p->output_type == OUTPUT_GRAY)
    gtk_check_button_set_active(GTK_CHECK_BUTTON(output_gray), TRUE);
  else
    gtk_check_button_set_active(GTK_CHECK_BUTTON(output_color), TRUE);

 /*
  * Now get option parameters...
  */

  printer = printers + current_printer;

  if (num_media_sizes > 0)
  {
    for (i = 0; i < num_media_sizes; i ++)
      g_free(media_sizes[i]);
    g_free(media_sizes);
  };

  media_sizes = (*(printer->parameters))(printer->model,
                                         p->ppd_file,
                                         "PageSize", &num_media_sizes);
  if (vars.media_size[0] == '\0')
    strcpy(vars.media_size, media_sizes[0]);
  plist_build_menu(media_size, num_media_sizes, media_sizes,
                   p->media_size, media_size_callback);

  if (num_media_types > 0)
  {
    for (i = 0; i < num_media_types; i ++)
      g_free(media_types[i]);
    g_free(media_types);
  };

  media_types = (*(printer->parameters))(printer->model,
                                         p->ppd_file,
                                         "MediaType", &num_media_types);
  if (vars.media_type[0] == '\0' && media_types != NULL)
    strcpy(vars.media_type, media_types[0]);
  plist_build_menu(media_type, num_media_types, media_types,
                   p->media_type, media_type_callback);

  if (num_media_sources > 0)
  {
    for (i = 0; i < num_media_sources; i ++)
      g_free(media_sources[i]);
    g_free(media_sources);
  };

  media_sources = (*(printer->parameters))(printer->model,
                                           p->ppd_file,
                                           "InputSlot", &num_media_sources);
  if (vars.media_source[0] == '\0' && media_sources != NULL)
    strcpy(vars.media_source, media_sources[0]);
  plist_build_menu(media_source, num_media_sources, media_sources,
                   p->media_source, media_source_callback);

  if (num_resolutions > 0)
  {
    for (i = 0; i < num_resolutions; i ++)
      g_free(resolutions[i]);
    g_free(resolutions);
  };

  resolutions = (*(printer->parameters))(printer->model,
                                         p->ppd_file,
                                         "Resolution", &num_resolutions);
  if (vars.resolution[0] == '\0' && resolutions != NULL)
    strcpy(vars.resolution, resolutions[0]);
  plist_build_menu(resolution, num_resolutions, resolutions,
                   p->resolution, resolution_callback);

  preview_update();
}


/*
 * 'media_size_callback()' - Update the current media size...
 */

static void
media_size_callback(GtkWidget *widget,		/* I - Media size option menu */
                    gpointer  data)		/* I - Data */
{
  strcpy(vars.media_size, media_sizes[GPOINTER_TO_INT(data)]);
  strcpy(plist[plist_current].media_size, media_sizes[GPOINTER_TO_INT(data)]);
  vars.left       = -1;
  vars.top        = -1;

  preview_update();
}


/*
 * 'media_type_callback()' - Update the current media type...
 */

static void
media_type_callback(GtkWidget *widget,		/* I - Media type option menu */
                    gpointer  data)		/* I - Data */
{
  strcpy(vars.media_type, media_types[GPOINTER_TO_INT(data)]);
  strcpy(plist[plist_current].media_type, media_types[GPOINTER_TO_INT(data)]);
}


/*
 * 'media_source_callback()' - Update the current media source...
 */

static void
media_source_callback(GtkWidget *widget,	/* I - Media source option menu */
                      gpointer  data)		/* I - Data */
{
  strcpy(vars.media_source, media_sources[GPOINTER_TO_INT(data)]);
  strcpy(plist[plist_current].media_source,
         media_sources[GPOINTER_TO_INT(data)]);
}


/*
 * 'resolution_callback()' - Update the current resolution...
 */

static void
resolution_callback(GtkWidget *widget,		/* I - Media size option menu */
                    gpointer  data)		/* I - Data */
{
  strcpy(vars.resolution, resolutions[GPOINTER_TO_INT(data)]);
  strcpy(plist[plist_current].resolution, resolutions[GPOINTER_TO_INT(data)]);
}


/*
 * 'orientation_callback()' - Update the current media size...
 */

static void
orientation_callback(GtkWidget *widget,		/* I - Orientation option menu */
                    gpointer  data)		/* I - Data */
{
  vars.orientation = GPOINTER_TO_INT(data);
  vars.left        = -1;
  vars.top         = -1;

  preview_update();
}


/*
 * 'output_type_callback()' - Update the current output type...
 */

static void
output_type_callback(GtkWidget *widget,	/* I - Output type button */
                     gpointer  data)	/* I - Data */
{
  if (gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)))
  {
    vars.output_type = GPOINTER_TO_INT(data);
    plist[plist_current].output_type = GPOINTER_TO_INT(data);
  };
}


/*
 * 'print_callback()' - Start the print...
 */

static void
print_callback(void)
{
  if (plist_current > PLIST_FILE)
  {
    runme = TRUE;

    gtk_window_destroy(GTK_WINDOW(print_dialog));
  }
  else
    gimp_file_dialog_save(GTK_WINDOW(print_dialog), "Print To File?",
                          vars.output_to[0] ? vars.output_to : NULL,
                          file_callback, NULL);
}


/*
 * 'cancel_callback()' - Cancel the print...
 */

static void
cancel_callback(void)
{
  gtk_window_destroy(GTK_WINDOW(print_dialog));
}


/*
 * 'close_callback()' - Exit the print dialog application.
 */

static void
close_callback(void)
{
  gimp_main_loop_quit();
}


static void
setup_open_callback(void)
{
  int	i;	/* Looping var */


  for (i = 0; i < (int)(sizeof(printers) / sizeof(printers[0])); i ++)
    if (strcmp(plist[plist_current].driver, printers[i].short_name) == 0)
    {
      current_printer = i;
      break;
    };

  gimp_option_menu_set_history(printer_driver, current_printer);
  print_driver_callback(printer_driver, GINT_TO_POINTER(current_printer));

  gtk_editable_set_text(GTK_EDITABLE(ppd_file), plist[plist_current].ppd_file);

  gtk_editable_set_text(GTK_EDITABLE(output_cmd), plist[plist_current].command);

  gtk_widget_set_visible(output_cmd, plist_current >= PLIST_SPECIAL);

  gtk_window_present(GTK_WINDOW(setup_dialog));
}


static void
setup_ok_callback(void)
{
  strcpy(vars.short_name, printers[current_printer].short_name);
  strcpy(plist[plist_current].driver, printers[current_printer].short_name);

  if (plist_current >= PLIST_SPECIAL)
  {
    g_strlcpy(vars.output_to, gtk_editable_get_text(GTK_EDITABLE(output_cmd)),
              sizeof(vars.output_to));
    strcpy(plist[plist_current].command, vars.output_to);
  };

  g_strlcpy(vars.ppd_file, gtk_editable_get_text(GTK_EDITABLE(ppd_file)),
            sizeof(vars.ppd_file));
  strcpy(plist[plist_current].ppd_file, vars.ppd_file);

  plist_callback(NULL, GINT_TO_POINTER(plist_current));

  gtk_widget_set_visible(setup_dialog, FALSE);
}


static void
setup_cancel_callback(void)
{
  gtk_widget_set_visible(setup_dialog, FALSE);
}


static gboolean
setup_close_callback(void)
{
  setup_cancel_callback();

  return (TRUE);
}


/*
 * 'print_driver_callback()' - Update the current printer driver...
 */

static void
print_driver_callback(GtkWidget *widget,	/* I - Driver option menu */
                      gpointer  data)		/* I - Data */
{
  gboolean	ps;


  current_printer = GPOINTER_TO_INT(data);

  ps = strncmp(printers[current_printer].short_name, "ps", 2) == 0;

  gtk_widget_set_visible(ppd_file, ps);
  gtk_widget_set_visible(ppd_button, ps);
}


static void
ppd_browse_callback(void)
{
  const char	*current = gtk_editable_get_text(GTK_EDITABLE(ppd_file));


  gimp_file_dialog_open(GTK_WINDOW(setup_dialog), "PPD File?",
                        current[0] ? current : NULL,
                        ppd_file_callback, NULL);
}


static void
ppd_file_callback(const gchar *filename,	/* I - Chosen file or NULL */
                  gpointer    data)
{
  if (filename != NULL)
    gtk_editable_set_text(GTK_EDITABLE(ppd_file), filename);
}


static void
file_callback(const gchar *filename,	/* I - Chosen file or NULL */
              gpointer    data)
{
  if (filename != NULL)
  {
    g_strlcpy(vars.output_to, filename, sizeof(vars.output_to));

    runme = TRUE;
  };

  gtk_window_destroy(GTK_WINDOW(print_dialog));
}


/*
 * 'preview_update()' - Redraw the page preview.
 */

static void
preview_update(void)
{
  if (preview != NULL)
    gtk_widget_queue_draw(preview);
}


/*
 * 'preview_draw()' - Draw the page and the image on it.
 */

static void
preview_draw(GtkDrawingArea *area,
             cairo_t        *cr,
             int            area_width,
             int            area_height,
             gpointer       data)
{
  int		temp,		/* Swapping variable */
		orient,		/* True orientation of page */
		tw0, tw1,	/* Temporary page_widths */
		th0, th1,	/* Temporary page_heights */
		ta0, ta1;	/* Temporary areas */
  int		left, right,	/* Imageable area */
		top, bottom,
		width, length;	/* Physical width */
  printer_t	*p;		/* Current printer driver */
  GdkRGBA	color;		/* Foreground color */


  p = printers + current_printer;

  (*p->imageable_area)(p->model, vars.ppd_file, vars.media_size, &left, &right,
                       &bottom, &top);

  page_width  = 10 * (right - left) / 72;
  page_height = 10 * (top - bottom) / 72;

  (*p->media_size)(p->model, vars.ppd_file, vars.media_size, &width, &length);

  width  = 10 * width / 72;
  length = 10 * length / 72;

  ta0 = ta1 = 0;

  if (vars.scaling < 0)
  {
    tw0 = -image_width * 10 / vars.scaling;
    th0 = tw0 * image_height / image_width;
    tw1 = tw0;
    th1 = th0;
  }
  else
  {
    tw0 = page_width * vars.scaling / 100;
    th0 = tw0 * image_height / image_width;
    if (th0 > page_height)
    {
      th0 = page_height;
      tw0 = th0 * image_width / image_height;
    };
    ta0 = tw0 * th0;

    tw1 = page_height * vars.scaling / 100;
    th1 = tw1 * image_height / image_width;
    if (th1 > page_width)
    {
      th1 = page_width;
      tw1 = th1 * image_width / image_height;
    };
    ta1 = tw1 * th1;
  };

  if (vars.orientation == ORIENT_AUTO)
  {
    if (vars.scaling < 0)
    {
      if ((th0 > page_height && tw0 <= page_height) ||
          (tw0 > page_width && th0 <= page_width))
        orient = ORIENT_LANDSCAPE;
      else
        orient = ORIENT_PORTRAIT;
    }
    else
    {
      if (ta0 >= ta1)
	orient = ORIENT_PORTRAIT;
      else
	orient = ORIENT_LANDSCAPE;
    };
  }
  else
    orient = vars.orientation;

  if (orient == ORIENT_LANDSCAPE)
  {
    temp         = page_width;
    page_width   = page_height;
    page_height  = temp;
    temp         = width;
    width        = length;
    length       = temp;
    print_width  = tw1;
    print_height = th1;
  }
  else
  {
    print_width  = tw0;
    print_height = th0;
  };

  page_left = (PREVIEW_SIZE - page_width) / 2;
  page_top  = (PREVIEW_SIZE - page_height) / 2;

  gtk_widget_get_color(GTK_WIDGET(area), &color);
  gdk_cairo_set_source_rgba(cr, &color);
  cairo_set_line_width(cr, 1.0);

  cairo_rectangle(cr, (PREVIEW_SIZE - width) / 2 + 0.5,
                  (PREVIEW_SIZE - length) / 2 + 0.5,
                  width, length);
  cairo_stroke(cr);

  if (vars.left < 0)
    left = (page_width - print_width) / 2;
  else
  {
    left = 10 * vars.left / 72;

    if (left > (page_width - print_width))
    {
      left      = page_width - print_width;
      vars.left = 72 * left / 10;
    };
  };

  if (vars.top < 0)
    top = (page_height - print_height) / 2;
  else
  {
    top  = 10 * vars.top / 72;

    if (top > (page_height - print_height))
    {
      top      = page_height - print_height;
      vars.top = 72 * top / 10;
    };
  };

  cairo_rectangle(cr, page_left + left, page_top + top,
                  print_width, print_height);
  cairo_fill(cr);
}


static void
preview_button_callback(GtkGestureDrag *gesture,
                        double         x,
                        double         y,
                        gpointer       data)
{
  mouse_x = x;
  mouse_y = y;
}


static void
preview_motion_callback(GtkGestureDrag *gesture,
                        double         offset_x,
                        double         offset_y,
                        gpointer       data)
{
  double	start_x,
		start_y;
  int		x, y;


  gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);

  x = start_x + offset_x;
  y = start_y + offset_y;

  if (vars.left < 0 || vars.top < 0)
  {
    vars.left = 72 * (page_width - print_width) / 20;
    vars.top  = 72 * (page_height - print_height) / 20;
  };

  vars.left += 72 * (x - mouse_x) / 10;
  vars.top  += 72 * (y - mouse_y) / 10;

  if (vars.left < 0)
    vars.left = 0;

  if (vars.top < 0)
    vars.top = 0;

  preview_update();

  mouse_x = x;
  mouse_y = y;
}


/*
 * 'printrc_filename()' - Get the name of the printrc file (g_free it).
 *
 * The file lives in the user's GIMP directory, next to its gtkrc.
 */

static char *
printrc_filename(void)
{
  char		*dir,		/* GIMP directory */
		*filename;	/* printrc file */


  dir      = g_path_get_dirname(gimp_gtkrc());
  filename = g_build_filename(dir, "printrc", NULL);
  g_free(dir);

  return (filename);
}


#if defined(LPC_COMMAND) || defined(LPSTAT_COMMAND)
/*
 * 'print_quote_name()' - Quote a queue name for a print command, which is
 *                        split with g_shell_parse_argv(), if it needs it.
 */

static char *
print_quote_name(const char *name)	/* I - Queue name */
{
  if (strpbrk(name, " \t\n'\"\\") != NULL)
    return (g_shell_quote(name));
  else
    return (g_strdup(name));
}
#endif


/*
 * 'printrc_copy_field()' - Copy one field of a printrc line, truncating
 *                          it to the size of the destination.
 */

static void
printrc_copy_field(char       *dst,	/* O - Destination */
                   size_t     dstsize,	/* I - Size of destination */
                   const char *src,	/* I - Start of field */
                   size_t     len)	/* I - Length of field */
{
  if (len > dstsize - 1)
    len = dstsize - 1;

  memcpy(dst, src, len);
  dst[len] = '\0';
}


/*
 * 'printrc_load()' - Load the printer resource configuration file.
 */

static void
printrc_load(void)
{
  int		i;		/* Looping var */
  FILE		*fp;		/* Printrc file */
  char		*filename;	/* Printrc filename */
  char		line[1024],	/* Line in printrc file */
		*lineptr,	/* Pointer in line */
		*commaptr;	/* Pointer to next comma */
  plist_t	*p,		/* Current printer */
		key;		/* Search key */


 /*
  * Get the printer list...
  */

  get_printers();

 /*
  * Generate the filename for the current user...
  */

  filename = printrc_filename();
  fp       = g_fopen(filename, "r");
  g_free(filename);

  if (fp != NULL)
  {
   /*
    * File exists - read the contents and update the printer list...
    */

    while (fgets(line, sizeof(line), fp) != NULL)
    {
      if (line[0] == '#')
        continue;	/* Comment */

     /*
      * Read the command-delimited printer definition data.  Note that
      * we can't use sscanf because %[^,] fails if the string is empty...
      */

      if ((commaptr = strchr(line, ',')) == NULL)
        continue;	/* Skip old printer definitions */

      printrc_copy_field(key.name, sizeof(key.name), line, commaptr - line);
      lineptr = commaptr + 1;

      if ((commaptr = strchr(lineptr, ',')) == NULL)
        continue;	/* Skip bad printer definitions */

      printrc_copy_field(key.command, sizeof(key.command), lineptr, commaptr - lineptr);
      lineptr = commaptr + 1;

      if ((commaptr = strchr(lineptr, ',')) == NULL)
        continue;	/* Skip bad printer definitions */

      printrc_copy_field(key.driver, sizeof(key.driver), lineptr, commaptr - lineptr);
      lineptr = commaptr + 1;

      if ((commaptr = strchr(lineptr, ',')) == NULL)
        continue;	/* Skip bad printer definitions */

      printrc_copy_field(key.ppd_file, sizeof(key.ppd_file), lineptr, commaptr - lineptr);
      lineptr = commaptr + 1;

      if ((commaptr = strchr(lineptr, ',')) == NULL)
        continue;	/* Skip bad printer definitions */

      key.output_type = atoi(lineptr);
      lineptr = commaptr + 1;

      if ((commaptr = strchr(lineptr, ',')) == NULL)
        continue;	/* Skip bad printer definitions */

      printrc_copy_field(key.resolution, sizeof(key.resolution), lineptr, commaptr - lineptr);
      lineptr = commaptr + 1;

      if ((commaptr = strchr(lineptr, ',')) == NULL)
        continue;	/* Skip bad printer definitions */

      printrc_copy_field(key.media_size, sizeof(key.media_size), lineptr, commaptr - lineptr);
      lineptr = commaptr + 1;

      if ((commaptr = strchr(lineptr, ',')) == NULL)
        continue;	/* Skip bad printer definitions */

      printrc_copy_field(key.media_type, sizeof(key.media_type), lineptr, commaptr - lineptr);
      lineptr = commaptr + 1;

      g_strlcpy(key.media_source, lineptr, sizeof(key.media_source));
      g_strchomp(key.media_source);	/* Drop NL (and CR) */

      for (i = 1, p = plist + 1; i < plist_count; i ++, p ++)
        if (compare_printers(&key, p) == 0)
        {
          memcpy(p, &key, sizeof(plist_t));
          break;
        };
    };

    fclose(fp);
  };

 /*
  * Select the current printer as necessary...
  */

  if (vars.output_to[0] != '\0')
  {
    for (i = 0; i < plist_count; i ++)
      if (strcmp(vars.output_to, plist[i].command) == 0)
        break;

    if (i < plist_count)
      plist_current = i;
  };
}


/*
 * 'printrc_save()' - Save the current printer resource configuration.
 */

static void
printrc_save(void)
{
  FILE		*fp;		/* Printrc file */
  char		*filename;	/* Printrc filename */
  int		i;		/* Looping var */
  plist_t	*p;		/* Current printer */


 /*
  * Generate the filename for the current user...
  */

  filename = printrc_filename();
  fp       = g_fopen(filename, "w");
  g_free(filename);

  if (fp != NULL)
  {
   /*
    * Write the contents of the printer list...
    */

    fputs("#PRINTRC " PLUG_IN_VERSION "\n", fp);

    for (i = 1, p = plist + 1; i < plist_count; i ++, p ++)
      fprintf(fp, "%s,%s,%s,%s,%d,%s,%s,%s,%s\n",
              p->name, p->command, p->driver, p->ppd_file, p->output_type,
              p->resolution, p->media_size, p->media_type, p->media_source);

    fclose(fp);
  };
}


/*
 * 'compare_printers()' - Compare system printer names for qsort().
 */

static int
compare_printers(plist_t *p1,	/* I - First printer to compare */
                 plist_t *p2)	/* I - Second printer to compare */
{
  return (strcmp(p1->name, p2->name));
}


/*
 * 'get_printers()' - Get a complete list of printers from the spooler.
 */

static void
get_printers(void)
{
  int	i;
  char	defname[17];
#if defined(LPC_COMMAND) || defined(LPSTAT_COMMAND)
  char	*output,
	**lines,
	*line,
	*quoted,
	name[17];
  int	j;
#endif


  defname[0] = '\0';

  memset(plist, 0, sizeof(plist));
  strcpy(plist[PLIST_FILE].name, "File");
  plist[PLIST_FILE].command[0] = '\0';
  strcpy(plist[PLIST_FILE].driver, "ps2");
  plist[PLIST_FILE].output_type = OUTPUT_COLOR;

  strcpy(plist[PLIST_SYSTEM].name, "System Printer");
  strcpy(plist[PLIST_SYSTEM].command, SYSTEM_PRINTER);
  strcpy(plist[PLIST_SYSTEM].driver, "ps2");
  plist[PLIST_SYSTEM].output_type = OUTPUT_COLOR;
  plist_count = PLIST_SPECIAL;

#ifdef LPC_COMMAND
  if (g_spawn_command_line_sync(LPC_COMMAND " status", &output, NULL, NULL,
                                NULL))
  {
    lines = g_strsplit(output, "\n", -1);

    for (j = 0; lines[j] != NULL && plist_count < MAX_PLIST; j ++)
    {
      line = lines[j];

      if (strchr(line, ':') != NULL)
      {
        *strchr(line, ':') = '\0';
        g_strlcpy(plist[plist_count].name, line,
                  sizeof(plist[plist_count].name));
        /* the command is split with g_shell_parse_argv: quote the name */
        quoted = print_quote_name(plist[plist_count].name);
        g_snprintf(plist[plist_count].command,
                   sizeof(plist[plist_count].command),
                   LPR_COMMAND " -P%s -l", quoted);
        g_free(quoted);
        strcpy(plist[plist_count].driver, "ps2");
        plist[plist_count].output_type = OUTPUT_COLOR;
        plist_count ++;
      };
    };

    g_strfreev(lines);
    g_free(output);
  };
#endif /* LPC_COMMAND */

#ifdef LPSTAT_COMMAND
  if (g_spawn_command_line_sync(LPSTAT_COMMAND " -d -p", &output, NULL, NULL,
                                NULL))
  {
    lines = g_strsplit(output, "\n", -1);

    for (j = 0; lines[j] != NULL && plist_count < MAX_PLIST; j ++)
    {
      line = lines[j];

      if (sscanf(line, "printer %16s", name) == 1)
      {
	strcpy(plist[plist_count].name, name);
	quoted = print_quote_name(name);
	g_snprintf(plist[plist_count].command,
	           sizeof(plist[plist_count].command),
	           LP_COMMAND " -s -d%s", quoted);
	g_free(quoted);
        strcpy(plist[plist_count].driver, "ps2");
        plist[plist_count].output_type = OUTPUT_COLOR;
        plist_count ++;
      }
      else
        sscanf(line, "system default destination: %16s", defname);
    };

    g_strfreev(lines);
    g_free(output);
  };
#endif /* LPSTAT_COMMAND */

  if (plist_count > PLIST_SPECIAL + 1)
    qsort(plist + PLIST_SPECIAL, plist_count - PLIST_SPECIAL, sizeof(plist_t),
          (int (*)(const void *, const void *))compare_printers);

  if (defname[0] != '\0' && vars.output_to[0] == '\0')
  {
    for (i = 0; i < plist_count; i ++)
      if (strcmp(defname, plist[i].name) == 0)
        break;

    if (i < plist_count)
      plist_current = i;
  }
  else if (vars.output_to[0] == '\0')
    plist_current = PLIST_SYSTEM;	/* No spooler default */
}


/*
 * End of "$Id$".
 */
