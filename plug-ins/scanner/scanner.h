/* Scanner plug-in for gimp42 -- shared declarations
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

/* scanner.c registers the procedures, keeps the settings between runs
 * and turns scanned pixels into images.  The scanning itself is done by
 * scanner-sane.c (SANE: Linux, macOS and other Unix systems) or by the
 * WIA glue in scanner.c (Windows).
 */

#ifndef __SCANNER_H__
#define __SCANNER_H__

#include <glib.h>

#define SCANNER_DATA_KEY     "extension_scanner"
#define SCANNER_VALS_VERSION 1

/*  What is remembered between runs (gimp_get_data/gimp_set_data): the
 *  device used last time and the values of its common options, plus a
 *  page counter for the image names.  Strings are empty and numbers
 *  negative when the device has no such option.
 */
typedef struct
{
  gint32  version;
  gint32  page;			/* images scanned so far: "Scan 1", ... */
  gchar   device[256];		/* SANE device name */
  gchar   source[128];
  gchar   mode[128];
  gdouble resolution;		/* dpi */
  gint32  have_area;
  gdouble area[4];		/* tl-x, tl-y, br-x, br-y, in the options' unit */
} ScannerVals;

extern ScannerVals scanner_vals;

/*  Saves scanner_vals for the next run.  */
void     scanner_vals_save       (void);

/*  Makes a new image from 8 bit data: bpp 1 is gray, 3 is RGB.  It is
 *  named "Scan N" after the page counter, which it advances.  Returns
 *  the image, or -1.
 */
gint32   scanner_image_new       (const guchar *data,
				  gint          width,
				  gint          height,
				  gint          bpp);

/*  Opens a window on image.  */
void     scanner_image_display   (gint32        image);

#ifndef G_OS_WIN32

/*  scanner-sane.c.  Both return the last image scanned, or -1.  */

/*  Shows the scanner dialog, where any number of pages can be scanned;
 *  each one is displayed as it arrives.
 */
gint32   scanner_sane_dialog     (void);

/*  Scans one page with the device and settings used last time, or with
 *  the first device's defaults.  With progress, reports it through
 *  gimp_progress_update.
 */
gint32   scanner_sane_scan       (gboolean      progress);

#endif /* ! G_OS_WIN32 */

#endif /* __SCANNER_H__ */
