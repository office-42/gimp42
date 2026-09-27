/*
 * "$Id$"
 *
 *   Print plug-in header file for the GIMP.
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
 * Revision History:
 *
 *   $Log$
 *   Revision 1.7  1998/05/31 06:49:17  yosh
 *   * app/interface.c: plug mem leak in message_box
 *
 *   * plug-ins/mail/mail.c: use g_message
 *
 *   * plug-ins/script-fu/scripts/carve-it.scm: fix for gimp-layer-new params from
 *   Kevin Cozens <kcozens@interlog.com>
 *
 *   * plug-ins/CEL/CEL.c
 *   * plug-ins/pcx/pcx.c
 *   * plug-ins/tiff/tiff.c: updates from author
 *
 *   * plug-ins/png/png.c: correct copyright
 *
 *   * plug-ins/print/print.h: use correct version
 *
 *   -Yosh
 *
 *   Revision 1.12  1998/05/16  14:25:51  mike
 *   Updated for v2.0.2.
 *
 *   Revision 1.11  1998/05/08  19:20:50  mike
 *   Updated for new driver interface.
 *   Added media size, imageable area, and parameter functions.
 *
 *   Revision 1.10  1998/03/01  17:20:48  mike
 *   Updated version number & date.
 *
 *   Revision 1.9  1998/01/21  21:33:47  mike
 *   Added Level 2 PostScript driver.
 *   Replaced Burkes dither with stochastic (random) dither.
 *   Now use Level 2 ASCII85 filter for Level 2 printers.
 *
 *   Revision 1.8  1997/11/14  17:17:59  mike
 *   Updated to dynamically allocate return params in the run() function.
 *
 *   Revision 1.7  1997/10/02  17:57:26  mike
 *   Added gamma/dot gain correction values for all printers.
 *
 *   Revision 1.7  1997/10/02  17:57:26  mike
 *   Added gamma/dot gain correction values for all printers.
 *
 *   Revision 1.6  1997/07/30  20:33:05  mike
 *   Final changes for 1.1 release.
 *
 *   Revision 1.5  1997/07/30  18:47:39  mike
 *   Added scaling, orientation, and offset options.
 *
 *   Revision 1.4  1997/07/26  18:43:04  mike
 *   Updated version number to 1.1.
 *
 *   Revision 1.3  1997/07/03  13:26:46  mike
 *   Updated documentation for 1.0 release.
 *
 *   Revision 1.2  1997/07/02  13:51:53  mike
 *   Added rgb_to_rgb and gray_to_gray conversion functions.
 *   Standardized calling args to conversion functions.
 *
 *   Revision 1.2  1997/07/02  13:51:53  mike
 *   Added rgb_to_rgb and gray_to_gray conversion functions.
 *   Standardized calling args to conversion functions.
 *
 *   Revision 1.1  1997/06/19  02:18:15  mike
 *   Initial revision
 */

/*
 * Include necessary header files...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <gtk/gtk.h>
#include <libgimp/gimp.h>


/*
 * Constants...
 */

#define PLUG_IN_VERSION		"2.0.2 - 16 May 1998"
#define PLUG_IN_NAME		"Print"

#define OUTPUT_GRAY		0	/* Grayscale output */
#define OUTPUT_COLOR		1	/* Color output */

#define ORIENT_AUTO		-1	/* Best orientation */
#define ORIENT_PORTRAIT		0	/* Portrait orientation */
#define ORIENT_LANDSCAPE	1	/* Landscape orientation */

#ifndef MIN
#  define MIN(a,b)		((a) < (b) ? (a) : (b))
#  define MAX(a,b)		((a) > (b) ? (a) : (b))
#endif /* !MIN */

#define LUM_RED			31	/* RGB to grayscale luminance */
#define LUM_GREEN		61
#define LUM_BLUE		8

#define PRINT_CLIP_TO_IMAGE	1	/* Merge type of the application */


/*
 * Printer driver control structure.  See "print.c" for the actual list...
 */

typedef struct
{
  char	*long_name,			/* Long name for UI */
	*short_name;			/* Short name for printrc file */
  int	color,				/* TRUE if supports color */
	model;				/* Model number */
  float	gamma,				/* Gamma correction */
	density;			/* Ink "density" or black level */
  char	**(*parameters)(int model, char *ppd_file, char *name, int *count);
					/* Parameter names */
  void	(*media_size)(int model, char *ppd_file, char *media_size,
                      int *width, int *length);
  void	(*imageable_area)(int model, char *ppd_file, char *media_size,
                          int *left, int *right, int *bottom, int *top);
  void	(*print)(int model, char *ppd_file, char *resolution,
                 char *media_size, char *media_type, char *media_source,
                 int output_type, int orientation, float scaling, int left,
                 int top, int copies, FILE *prn, GDrawable *drawable,
                 guchar *lut, guchar *cmap);	/* Print function */
} printer_t;

typedef void (*convert_t)(guchar *in, guchar *out, int width, int bpp,
                          guchar *lut, guchar *cmap);


/*
 * Prototypes...
 */

extern void	dither_black(guchar *, int, int, int, unsigned char *);
extern void	dither_cmyk(guchar *, int, int, int, unsigned char *,
		            unsigned char *, unsigned char *, unsigned char *);
extern void	gray_to_gray(guchar *, guchar *, int, int, guchar *, guchar *);
extern void	indexed_to_gray(guchar *, guchar *, int, int, guchar *, guchar *);
extern void	indexed_to_rgb(guchar *, guchar *, int, int, guchar *, guchar *);
extern void	rgb_to_gray(guchar *, guchar *, int, int, guchar *, guchar *);
extern void	rgb_to_rgb(guchar *, guchar *, int, int, guchar *, guchar *);

extern void	default_media_size(int model, char *ppd_file, char *media_size,
		                   int *width, int *length);
extern void	compute_lut(guchar *lut, int brightness, float gamma,
		            float print_gamma, float density);
extern guchar	*print_get_cmap(gint32 image_ID);
extern gint32	print_composite(gint32 image_ID, gint32 drawable_ID,
		                gint32 *copy_ID);
extern void	print_composite_done(gint32 copy_ID);
extern char	*print_user_filename(const char *name);

extern char	**escp2_parameters(int model, char *ppd_file, char *name,
		                   int *count);
extern void	escp2_imageable_area(int model, char *ppd_file, char *media_size,
		                     int *left, int *right, int *bottom, int *top);
extern void	escp2_print(int model, char *ppd_file, char *resolution,
		            char *media_size, char *media_type, char *media_source,
		            int output_type, int orientation, float scaling,
		            int left, int top, int copies, FILE *prn,
		            GDrawable *drawable, guchar *lut, guchar *cmap);

extern char	**pcl_parameters(int model, char *ppd_file, char *name,
		                 int *count);
extern void	pcl_imageable_area(int model, char *ppd_file, char *media_size,
		                   int *left, int *right, int *bottom, int *top);
extern void	pcl_print(int model, char *ppd_file, char *resolution,
		          char *media_size, char *media_type, char *media_source,
		          int output_type, int orientation, float scaling,
		          int left, int top, int copies, FILE *prn,
		          GDrawable *drawable, guchar *lut, guchar *cmap);

extern char	**ps_parameters(int model, char *ppd_file, char *name,
		                int *count);
extern void	ps_media_size(int model, char *ppd_file, char *media_size,
		              int *width, int *length);
extern void	ps_imageable_area(int model, char *ppd_file, char *media_size,
		                  int *left, int *right, int *bottom, int *top);
extern void	ps_print(int model, char *ppd_file, char *resolution,
		         char *media_size, char *media_type, char *media_source,
		         int output_type, int orientation, float scaling,
		         int left, int top, int copies, FILE *prn,
		         GDrawable *drawable, guchar *lut, guchar *cmap);

/*
 * The system printers, through GtkPrintOperation (print-gtk.c): the
 * "file_print_gtk" and "file_page_setup" procedures, and the "System
 * Printer" entry of file_print's own dialog, which hands its settings
 * over to the system print dialog (and gets them back when the image
 * was printed).
 */

extern void	gtkprint_query(void);
extern void	gtkprint_run(char *name, int nparams, GParam *param,
		             int *nreturn_vals, GParam **return_vals);
extern GStatusType gtkprint_legacy(gint32 image_ID, GDrawable *drawable,
		                   int show_dialog, char *media_size,
		                   int media_width, int media_length,
		                   int orientation, int *output_type,
		                   int *brightness, float *scaling, int *left,
		                   int *top);


/*
 * End of "$Id$".
 */
