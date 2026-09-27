/* Scanner plug-in for gimp42 -- Windows Image Acquisition
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

/* The COM side of scanning on Windows, kept apart from GLib and GTK: plain
 * C on windows.h and wia.h.
 */

#ifndef __SCANNER_WIA_H__
#define __SCANNER_WIA_H__

#include <stddef.h>
#include <wchar.h>

/*  The file formats WIA may write.  */
enum
{
  SCANNER_WIA_FORMAT_BMP,
  SCANNER_WIA_FORMAT_PNG,
  SCANNER_WIA_FORMAT_JPEG,
  SCANNER_WIA_FORMAT_TIFF,
  SCANNER_WIA_FORMAT_OTHER
};

/*  Shows Windows' own scanner dialog (after asking which scanner, when
 *  there are several) and writes the scanned image to path, asking for
 *  BMP.  *format is set to the format WIA actually wrote.
 *
 *  Returns 1 when the image was written, 0 when the user cancelled and
 *  -1 on errors, with a message in error (UTF-8, at most error_len bytes
 *  including the terminating zero).
 */
int scanner_wia_acquire (const wchar_t *path,
			 int           *format,
			 char          *error,
			 size_t         error_len);

#endif /* __SCANNER_WIA_H__ */
