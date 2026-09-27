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

/* Scanning on Windows goes through WIA 1.0, which every Windows since XP
 * has: IWiaDevMgr::GetImageDlg shows the system's scanner dialog, with
 * its own preview, area selection and settings, and writes the image to
 * a file.  Only plain Win32 and COM here; scanner.c does the rest.
 */

#define COBJMACROS

#include <windows.h>
#include <ole2.h>
#include <wia.h>
#include <sti.h>

#include <stdio.h>
#include <string.h>

#include "scanner-wia.h"

/*  The GUIDs, from the Windows SDK's wia.h and wiadef.h.  They are
 *  defined here because MinGW has no libwiaguid; the names are private
 *  so that they cannot clash with a library that does define them.
 */
static const CLSID scanner_clsid_wia_dev_mgr =
  { 0xa1f4e726, 0x8cf1, 0x11d1, { 0xbf, 0x92, 0x00, 0x60, 0x08, 0x1e, 0xd8, 0x11 } };
static const IID scanner_iid_wia_dev_mgr =
  { 0x5eb2502a, 0x8cf1, 0x11d1, { 0xbf, 0x92, 0x00, 0x60, 0x08, 0x1e, 0xd8, 0x11 } };
static const GUID scanner_wia_img_fmt_bmp =
  { 0xb96b3cab, 0x0728, 0x11d3, { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
static const GUID scanner_wia_img_fmt_jpeg =
  { 0xb96b3cae, 0x0728, 0x11d3, { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
static const GUID scanner_wia_img_fmt_png =
  { 0xb96b3caf, 0x0728, 0x11d3, { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
static const GUID scanner_wia_img_fmt_tiff =
  { 0xb96b3cb1, 0x0728, 0x11d3, { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

static void
scanner_wia_error (char       *error,
		   size_t      error_len,
		   const char *message,
		   HRESULT     hr)
{
  if (! error || error_len == 0)
    return;

  if (hr == S_OK)
    snprintf (error, error_len, "%s", message);
  else
    snprintf (error, error_len, "%s (error 0x%08lx).", message,
	      (unsigned long) hr);
}

/*  What went wrong, in words, for the WIA errors a user can do something
 *  about.
 */
static const char *
scanner_wia_describe (HRESULT hr)
{
  switch (hr)
    {
    case WIA_ERROR_PAPER_JAM:
      return "The paper is jammed in the scanner's document feeder.";
    case WIA_ERROR_PAPER_EMPTY:
      return "There are no documents in the scanner's document feeder.";
    case WIA_ERROR_PAPER_PROBLEM:
      return "There is a problem with the paper in the document feeder.";
    case WIA_ERROR_OFFLINE:
      return "The scanner is offline. Check that it is connected and "
	     "switched on.";
    case WIA_ERROR_BUSY:
      return "The scanner is busy. Try again in a moment.";
    case WIA_ERROR_WARMING_UP:
      return "The scanner is warming up. Try again in a moment.";
    case WIA_ERROR_USER_INTERVENTION:
      return "The scanner needs attention. Check it for messages.";
    case WIA_ERROR_DEVICE_COMMUNICATION:
      return "Communication with the scanner failed. Check the cable or "
	     "the network connection.";
    case WIA_ERROR_DEVICE_LOCKED:
      return "The scanner is locked. Unlock its scan head.";
    case E_OUTOFMEMORY:
      return "There is not enough memory for the scan.";
    default:
      return NULL;
    }
}

int
scanner_wia_acquire (const wchar_t *path,
		     int           *format,
		     char          *error,
		     size_t         error_len)
{
  IWiaDevMgr *manager  = NULL;
  BSTR        filename = NULL;
  GUID        file_format;
  HRESULT     init;
  HRESULT     hr;
  int         result   = -1;

  if (error && error_len > 0)
    error[0] = '\0';
  if (format)
    *format = SCANNER_WIA_FORMAT_OTHER;

  /* The WIA dialogs need a single-threaded apartment.  */
  init = CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);
  if (FAILED (init) && init != RPC_E_CHANGED_MODE)
    {
      scanner_wia_error (error, error_len, "COM could not be initialized",
			 init);
      return -1;
    }

  hr = CoCreateInstance (&scanner_clsid_wia_dev_mgr, NULL,
			 CLSCTX_LOCAL_SERVER, &scanner_iid_wia_dev_mgr,
			 (void **) &manager);
  if (FAILED (hr) || ! manager)
    {
      scanner_wia_error (error, error_len,
			 "Windows Image Acquisition is not available. Check "
			 "that the Windows Image Acquisition (WIA) service "
			 "is running", hr);
      goto out;
    }

  filename = SysAllocString (path);
  if (! filename)
    {
      scanner_wia_error (error, error_len,
			 "There is not enough memory for the scan.", S_OK);
      goto out;
    }

  file_format = scanner_wia_img_fmt_bmp;

  hr = IWiaDevMgr_GetImageDlg (manager,
			       NULL,			/* no parent window */
			       StiDeviceTypeScanner,
			       0,			/* flags */
			       WIA_INTENT_NONE,
			       NULL,			/* ask for the device */
			       filename,
			       &file_format);

  if (hr == S_OK)
    {
      result = 1;

      if (format)
	{
	  if (IsEqualGUID (&file_format, &scanner_wia_img_fmt_bmp))
	    *format = SCANNER_WIA_FORMAT_BMP;
	  else if (IsEqualGUID (&file_format, &scanner_wia_img_fmt_png))
	    *format = SCANNER_WIA_FORMAT_PNG;
	  else if (IsEqualGUID (&file_format, &scanner_wia_img_fmt_jpeg))
	    *format = SCANNER_WIA_FORMAT_JPEG;
	  else if (IsEqualGUID (&file_format, &scanner_wia_img_fmt_tiff))
	    *format = SCANNER_WIA_FORMAT_TIFF;
	}
    }
  else if (hr == S_FALSE)
    {
      /* The user cancelled.  */
      result = 0;
    }
  else if (hr == WIA_S_NO_DEVICE_AVAILABLE)
    {
      scanner_wia_error (error, error_len,
			 "No scanners were found. Check that the scanner is "
			 "connected and switched on, and that Windows "
			 "recognises it.", S_OK);
    }
  else if (scanner_wia_describe (hr))
    {
      scanner_wia_error (error, error_len, scanner_wia_describe (hr), S_OK);
    }
  else
    {
      scanner_wia_error (error, error_len, "Scanning failed", hr);
    }

 out:
  if (filename)
    SysFreeString (filename);
  if (manager)
    IWiaDevMgr_Release (manager);
  if (SUCCEEDED (init))
    CoUninitialize ();

  return result;
}
