/*
 *  ScreenShot plug-in v0.9
 *  Sven Neumann, neumanns@uni-duesseldorf.de
 *  1998/06/06
 *
 *  Any suggestions, bug-reports or patches are very welcome.
 *
 *  This plug-in used the X-utility xwd to grab an image from the screen
 *  and the xwd-plug-in created by Peter Kirchgessner (pkirchg@aol.com)
 *  to load this image into the gimp.
 *
 *  gimp42: on Windows the screen or a window is now grabbed with GDI
 *  (BitBlt for the whole screen, PrintWindow for a single window, which
 *  is picked by clicking it, as with xwd).  Elsewhere the screenshot is
 *  taken through the desktop portal (org.freedesktop.portal.Screenshot
 *  over D-Bus), which works on Wayland as well as X11 and lets the user
 *  pick what to grab; the file it writes is loaded with gimp_file_load.
 */

/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
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

/* Revision history
 *  (98/02/18)  v0.1   first development release
 *  (98/02/19)  v0.2   small bugfix
 *  (98/03/09)  v0.3   another one
 *  (98/03/13)  v0.4   cosmetic changes to the dialog
 *  (98/04/02)  v0.5   it works non-interactively now and registers
 *                     itself correctly as extension
 *  (98/04/18)  v0.6   cosmetic change to the dialog
 *  (98/05/28)  v0.7   use g_message for error output
 *  (98/06/04)  v0.8   added delay-time for root window shot
 *  (98/06/06)  v0.9   fixed a stupid bug in the dialog
 */

#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* Defines */
#define PLUG_IN_NAME        "extension_screenshot"
#define PLUG_IN_PRINT_NAME  "Screen Shot"
#define PLUG_IN_VERSION     "v0.9 (98/06/06)"
#define PLUG_IN_MENU_PATH   "<Toolbox>/File/Acquire/Screen Shot..."
#define PLUG_IN_AUTHOR      "Sven Neumann (neumanns@uni-duesseldorf.de)"
#define PLUG_IN_COPYRIGHT   "Sven Neumann"
#define PLUG_IN_DESCRIBTION "Create a screenshot of a single window or the whole screen"
#ifdef G_OS_WIN32
#define PLUG_IN_HELP        "After specifying some options, the user clicks the window to grab (a right click cancels), and the resulting image is loaded into the gimp. Alternatively the whole screen can be grabbed. When called non-interactively it may grab the whole screen or use the window handle (HWND, decimal or 0x-prefixed hex) passed as window_id."
#else
#define PLUG_IN_HELP        "The screenshot is taken through the desktop portal (org.freedesktop.portal.Screenshot), which lets the user choose a window or area when a single window is asked for, and the resulting image is loaded into the gimp. Alternatively the whole screen can be grabbed. When called non-interactively only the whole screen can be grabbed; window_id is not supported."
#endif

#define NUMBER_IN_ARGS 3
#define IN_ARGS { PARAM_INT32,    "run_mode",  "Interactive, non-interactive" },\
                { PARAM_INT32,    "root",      "Root window { TRUE, FALSE }" },\
                { PARAM_STRING,   "window_id", "Window id" }

#define NUMBER_OUT_ARGS 1
#define OUT_ARGS { PARAM_IMAGE,   "image",     "Output image" }

typedef struct {
  gint root;
  gchar *window_id;
  guint delay;
  gint decor;
} ScreenShotValues;

typedef struct {
  GtkWidget *decor_button;
  GtkWidget *delay_box;
  GtkWidget *delay_spinner;
  GtkWidget *single_button;
  GtkWidget *root_button;
  gint run;
} ScreenShotInterface;

static ScreenShotValues shootvals =
{
  FALSE,     /* root window */
  NULL,      /* window ID */
  0,         /* delay */
  TRUE,      /* decorations */
};

static ScreenShotInterface shootint =
{
  NULL, NULL, NULL, NULL, NULL,
  FALSE	    /* run */
};


static void  query (void);
static void  run (gchar *name,
		  gint nparams,	          /* number of parameters passed in */
		  GParam * param,	  /* parameters passed in */
		  gint *nreturn_vals,     /* number of parameters returned */
		  GParam ** return_vals); /* parameters to be returned */
static void  shoot (gboolean interactive);
static gint  shoot_dialog (void);
static void  shoot_close_callback (GtkWidget *widget,
				   gpointer   data);
static void  shoot_ok_callback (GtkWidget *widget,
				gpointer   data);
static void  shoot_toggle_update (GtkWidget *widget,
				  gpointer   radio_button);
static void  shoot_display_image (gint32 image);
static void  shoot_delay (gint32 delay);

/* Global Variables */
GPlugInInfo PLUG_IN_INFO =
{
  NULL,   /* init_proc  */
  NULL,	  /* quit_proc  */
  query,  /* query_proc */
  run     /* run_proc   */
};

/* the image that will be returned */
gint32    image_ID = -1;

/* Functions */

MAIN ()

static void query (void)
{
  static GParamDef args[] = { IN_ARGS };
  static gint nargs = NUMBER_IN_ARGS;
  static GParamDef return_vals[] = { OUT_ARGS };
  static gint nreturn_vals = NUMBER_OUT_ARGS;

  /* the actual installation of the plugin */
  gimp_install_procedure (PLUG_IN_NAME,
			  PLUG_IN_DESCRIBTION,
			  PLUG_IN_HELP,
			  PLUG_IN_AUTHOR,
			  PLUG_IN_COPYRIGHT,
			  PLUG_IN_VERSION,
			  PLUG_IN_MENU_PATH,
			  NULL,
			  PROC_EXTENSION,
			  nargs,
			  nreturn_vals,
			  args,
			  return_vals);
}

static void
run (gchar *name,		/* name of plugin */
     gint nparams,		/* number of in-paramters */
     GParam * param,		/* in-parameters */
     gint *nreturn_vals,	/* number of out-parameters */
     GParam ** return_vals)	/* out-parameters */
{

  /* Get the runmode from the in-parameters */
  GRunModeType run_mode = param[0].data.d_int32;

  /* status variable, use it to check for errors in invocation usualy only
     during non-interactive calling */
  GStatusType status = STATUS_SUCCESS;

  /*always return at least the status to the caller. */
  static GParam values[2];

  /* initialize the return of the status */
  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
  *nreturn_vals = 1;
  *return_vals = values;

#ifdef G_OS_WIN32
  /* Work in physical pixels, so that the grab is not scaled down or
   * cut off on high-DPI screens.
   */
  {
    typedef BOOL (WINAPI *SetDpiAwarenessContextFunc) (HANDLE);
    SetDpiAwarenessContextFunc set_context;

    set_context = (SetDpiAwarenessContextFunc) (void (*) (void))
      GetProcAddress (GetModuleHandleW (L"user32.dll"),
		      "SetProcessDpiAwarenessContext");
    /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 is ((HANDLE) -4) */
    if (! set_context || ! set_context ((HANDLE) (gintptr) -4))
      SetProcessDPIAware ();
  }
#endif

  /*how are we running today? */
  switch (run_mode)
  {
    case RUN_INTERACTIVE:
      /* Possibly retrieve data from a previous run */
      gimp_get_data (PLUG_IN_NAME, &shootvals);
      shootvals.window_id = NULL;

      /* Get information from the dialog */
      if (!shoot_dialog())
	return;
      break;

    case RUN_NONINTERACTIVE:
      if (nparams == NUMBER_IN_ARGS)
	{
	  shootvals.root      = (gint) param[1].data.d_int32;
	  shootvals.window_id = (gchar*) param[2].data.d_string;
	  shootvals.delay     = 0;
	  shootvals.decor     = FALSE;
	}
      else
	status = STATUS_CALLING_ERROR;
      break;

    case RUN_WITH_LAST_VALS:
      /* Possibly retrieve data from a previous run */
      gimp_get_data (PLUG_IN_NAME, &shootvals);
      shootvals.window_id = NULL;
      break;

    default:
      break;
  } /* switch */

  if (status == STATUS_SUCCESS)
  {
    if (shootvals.root && (shootvals.delay > 0))
      shoot_delay(shootvals.delay);
    /* Run the main function */
    shoot (run_mode != RUN_NONINTERACTIVE);
  }

  status = (image_ID != -1) ? STATUS_SUCCESS : STATUS_EXECUTION_ERROR;

  if (status == STATUS_SUCCESS)
  {
    if (run_mode == RUN_INTERACTIVE)
      {
	/* Store variable states for next run */
	shootvals.window_id = NULL;
	gimp_set_data (PLUG_IN_NAME, &shootvals, sizeof (ScreenShotValues));
	/* display the image */
	shoot_display_image (image_ID);
      }
    /* set return values */
    *nreturn_vals = 2;
    values[1].type = PARAM_IMAGE;
    values[1].data.d_image = image_ID;
  }
  values[0].data.d_status = status;
}


/*  Makes a new image holding width x height pixels of RGB data.  */
static gint32
shoot_new_image (const guchar *rgb,
		 gint          width,
		 gint          height)
{
  GDrawable *drawable;
  GPixelRgn  pixel_rgn;
  gint32     image;
  gint32     layer;

  image = gimp_image_new (width, height, RGB);
  layer = gimp_layer_new (image, "Screen Shot", width, height,
			  RGB_IMAGE, 100, NORMAL_MODE);
  gimp_image_add_layer (image, layer, 0);

  drawable = gimp_drawable_get (layer);
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height,
		       TRUE, FALSE);
  gimp_pixel_rgn_set_rect (&pixel_rgn, (guchar *) rgb, 0, 0, width, height);
  gimp_drawable_flush (drawable);
  gimp_drawable_detach (drawable);

  return image;
}

#ifdef G_OS_WIN32

/*  GDI capture.  */

/*  A top-down 32 bit DIB section selected into a memory DC.  */
typedef struct
{
  HDC      dc;
  HBITMAP  bitmap;
  HGDIOBJ  old;
  guint32 *bits;
  gint     width;
  gint     height;
} ShootDib;

static gboolean
shoot_dib_new (ShootDib *dib,
	       gint      width,
	       gint      height)
{
  BITMAPINFO info;
  void      *bits = NULL;

  memset (&info, 0, sizeof (info));
  info.bmiHeader.biSize        = sizeof (BITMAPINFOHEADER);
  info.bmiHeader.biWidth       = width;
  info.bmiHeader.biHeight      = -height;   /* top-down */
  info.bmiHeader.biPlanes      = 1;
  info.bmiHeader.biBitCount    = 32;
  info.bmiHeader.biCompression = BI_RGB;

  dib->dc = CreateCompatibleDC (NULL);
  if (! dib->dc)
    return FALSE;

  dib->bitmap = CreateDIBSection (dib->dc, &info, DIB_RGB_COLORS,
				  &bits, NULL, 0);
  if (! dib->bitmap || ! bits)
    {
      DeleteDC (dib->dc);
      return FALSE;
    }

  dib->old    = SelectObject (dib->dc, dib->bitmap);
  dib->bits   = bits;
  dib->width  = width;
  dib->height = height;

  return TRUE;
}

static void
shoot_dib_free (ShootDib *dib)
{
  SelectObject (dib->dc, dib->old);
  DeleteObject (dib->bitmap);
  DeleteDC (dib->dc);
}

/*  Makes an image of the rectangle x, y, width, height of dib, which is
 *  clipped to the dib.
 */
static gint32
shoot_dib_to_image (ShootDib *dib,
		    gint      x,
		    gint      y,
		    gint      width,
		    gint      height)
{
  guchar *rgb;
  guchar *dest;
  gint32  image;
  gint    row, col;

  if (x < 0)
    {
      width += x;
      x = 0;
    }
  if (y < 0)
    {
      height += y;
      y = 0;
    }
  width  = MIN (width, dib->width - x);
  height = MIN (height, dib->height - y);

  if (width <= 0 || height <= 0)
    return -1;

  GdiFlush ();

  rgb  = g_malloc ((gsize) width * height * 3);
  dest = rgb;
  for (row = 0; row < height; row++)
    {
      const guint32 *src = dib->bits + (gsize) (y + row) * dib->width + x;

      for (col = 0; col < width; col++)
	{
	  guint32 pixel = src[col];      /* 0x00RRGGBB */

	  *dest++ = (pixel >> 16) & 0xff;
	  *dest++ = (pixel >> 8) & 0xff;
	  *dest++ = pixel & 0xff;
	}
    }

  image = shoot_new_image (rgb, width, height);
  g_free (rgb);

  return image;
}

static gint32
shoot_screen (void)
{
  ShootDib dib;
  HDC      screen;
  gint     x, y, width, height;
  gint32   image;

  x      = GetSystemMetrics (SM_XVIRTUALSCREEN);
  y      = GetSystemMetrics (SM_YVIRTUALSCREEN);
  width  = GetSystemMetrics (SM_CXVIRTUALSCREEN);
  height = GetSystemMetrics (SM_CYVIRTUALSCREEN);

  if (! shoot_dib_new (&dib, width, height))
    {
      g_message ("screenshot: could not allocate a %dx%d bitmap\n",
		 width, height);
      return -1;
    }

  screen = GetDC (NULL);
  if (! BitBlt (dib.dc, 0, 0, width, height, screen, x, y,
		SRCCOPY | CAPTUREBLT))
    {
      g_message ("screenshot: grabbing the screen failed\n");
      ReleaseDC (NULL, screen);
      shoot_dib_free (&dib);
      return -1;
    }
  ReleaseDC (NULL, screen);

  image = shoot_dib_to_image (&dib, 0, 0, width, height);
  shoot_dib_free (&dib);

  return image;
}

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

#define SHOOT_DWMWA_EXTENDED_FRAME_BOUNDS 9

/*  The visible frame of window, without the invisible resize borders
 *  Windows 10 and later add around it; the window rectangle when DWM
 *  cannot tell.
 */
static void
shoot_get_frame_rect (HWND  window,
		      RECT *rect)
{
  typedef HRESULT (WINAPI *DwmGetWindowAttributeFunc) (HWND, DWORD,
						       PVOID, DWORD);
  static DwmGetWindowAttributeFunc get_attribute = NULL;
  static gboolean                  looked_up     = FALSE;

  GetWindowRect (window, rect);

  if (! looked_up)
    {
      HMODULE dwmapi = LoadLibraryW (L"dwmapi.dll");

      if (dwmapi)
	get_attribute = (DwmGetWindowAttributeFunc) (void (*) (void))
	  GetProcAddress (dwmapi, "DwmGetWindowAttribute");
      looked_up = TRUE;
    }

  if (get_attribute)
    {
      RECT frame;

      if (get_attribute (window, SHOOT_DWMWA_EXTENDED_FRAME_BOUNDS,
			 &frame, sizeof (frame)) == S_OK)
	*rect = frame;
    }
}

static gint32
shoot_window (HWND     window,
	      gboolean decor)
{
  ShootDib dib;
  RECT     window_rect;
  RECT     part;
  gint     width, height;
  gint32   image;

  if (! IsWindow (window))
    {
      g_message ("screenshot: there is no window with id %p\n",
		 (void *) window);
      return -1;
    }

  if (IsIconic (window))
    {
      g_message ("screenshot: the window is minimized\n");
      return -1;
    }

  GetWindowRect (window, &window_rect);
  width  = window_rect.right - window_rect.left;
  height = window_rect.bottom - window_rect.top;

  if (width <= 0 || height <= 0)
    {
      g_message ("screenshot: the window is empty\n");
      return -1;
    }

  if (! shoot_dib_new (&dib, width, height))
    {
      g_message ("screenshot: could not allocate a %dx%d bitmap\n",
		 width, height);
      return -1;
    }

  /* PrintWindow draws the whole window, even where other windows cover
   * it; PW_RENDERFULLCONTENT includes DirectX/DirectComposition content.
   */
  if (! PrintWindow (window, dib.dc, PW_RENDERFULLCONTENT) &&
      ! PrintWindow (window, dib.dc, 0))
    {
      g_message ("screenshot: grabbing the window failed\n");
      shoot_dib_free (&dib);
      return -1;
    }

  if (decor)
    {
      shoot_get_frame_rect (window, &part);
    }
  else
    {
      POINT origin = { 0, 0 };

      GetClientRect (window, &part);
      ClientToScreen (window, &origin);
      OffsetRect (&part, origin.x, origin.y);
    }

  image = shoot_dib_to_image (&dib,
			      part.left - window_rect.left,
			      part.top - window_rect.top,
			      part.right - part.left,
			      part.bottom - part.top);
  shoot_dib_free (&dib);

  return image;
}

/*  Picking a window by clicking it, like xwd does.  A low-level mouse
 *  hook swallows the click so that it does not reach the window.
 */
enum
{
  PICK_WAITING,
  PICK_PRESSED,
  PICK_CANCELLING,
  PICK_DONE,
  PICK_CANCELLED
};

static POINT pick_point;
static gint  pick_state;

static LRESULT CALLBACK
shoot_pick_hook (int    code,
		 WPARAM wparam,
		 LPARAM lparam)
{
  if (code == HC_ACTION)
    {
      MSLLHOOKSTRUCT *info = (MSLLHOOKSTRUCT *) lparam;

      switch (wparam)
	{
	case WM_LBUTTONDOWN:
	  if (pick_state == PICK_WAITING)
	    {
	      pick_point = info->pt;
	      pick_state = PICK_PRESSED;
	      return 1;
	    }
	  break;

	case WM_LBUTTONUP:
	  if (pick_state == PICK_PRESSED)
	    {
	      pick_state = PICK_DONE;
	      PostQuitMessage (0);
	      return 1;
	    }
	  break;

	case WM_RBUTTONDOWN:
	  if (pick_state == PICK_WAITING)
	    {
	      pick_state = PICK_CANCELLING;
	      return 1;
	    }
	  break;

	case WM_RBUTTONUP:
	  if (pick_state == PICK_CANCELLING)
	    {
	      pick_state = PICK_CANCELLED;
	      PostQuitMessage (0);
	      return 1;
	    }
	  break;

	default:
	  break;
	}
    }

  return CallNextHookEx (NULL, code, wparam, lparam);
}

/*  Waits for a click and returns the toplevel window under it, or NULL
 *  when the user cancelled or nothing was clicked within a minute.
 */
static HWND
shoot_pick_window (void)
{
  HHOOK    hook;
  UINT_PTR timer;
  MSG      msg;
  HWND     window;

  pick_state = PICK_WAITING;

  hook = SetWindowsHookExW (WH_MOUSE_LL, shoot_pick_hook,
			    GetModuleHandleW (NULL), 0);
  if (! hook)
    {
      g_message ("screenshot: cannot watch the mouse to pick a window\n");
      return NULL;
    }

  timer = SetTimer (NULL, 0, 60 * 1000, NULL);

  while (GetMessageW (&msg, NULL, 0, 0) > 0)
    {
      if (msg.message == WM_TIMER && msg.hwnd == NULL &&
	  msg.wParam == timer)
	break;

      TranslateMessage (&msg);
      DispatchMessageW (&msg);
    }

  KillTimer (NULL, timer);
  UnhookWindowsHookEx (hook);

  if (pick_state != PICK_DONE)
    return NULL;

  window = WindowFromPoint (pick_point);
  if (window)
    window = GetAncestor (window, GA_ROOT);

  return window;
}

/* The main ScreenShot function */
static void
shoot (gboolean interactive)
{
  HWND window;

  if (shootvals.root == TRUE)
    {
      image_ID = shoot_screen ();
      return;
    }

  if (shootvals.window_id != NULL && shootvals.window_id[0] != '\0')
    {
      gchar   *end;
      guint64  id;

      id = g_ascii_strtoull (shootvals.window_id, &end, 0);
      if (end == shootvals.window_id || *end != '\0')
	{
	  g_message ("screenshot: \"%s\" is not a window handle\n",
		     shootvals.window_id);
	  return;
	}
      window = (HWND) (guintptr) id;
    }
  else if (interactive)
    {
      window = shoot_pick_window ();
      if (! window)
	return;
    }
  else
    {
      g_message ("screenshot: no window id given\n");
      return;
    }

  image_ID = shoot_window (window, shootvals.decor);
}

#else  /* ! G_OS_WIN32 */

/*  Desktop portal capture.  */

typedef struct
{
  GMainLoop *loop;
  guint      response;
  gchar     *uri;
} PortalResponse;

static void
shoot_portal_response (GDBusConnection *connection,
		       const gchar     *sender_name,
		       const gchar     *object_path,
		       const gchar     *interface_name,
		       const gchar     *signal_name,
		       GVariant        *parameters,
		       gpointer         data)
{
  PortalResponse *response = data;
  GVariant       *results  = NULL;

  g_variant_get (parameters, "(u@a{sv})", &response->response, &results);
  if (results)
    {
      g_variant_lookup (results, "uri", "s", &response->uri);
      g_variant_unref (results);
    }

  g_main_loop_quit (response->loop);
}

/*  Asks the portal for a screenshot and returns the URI of the file it
 *  wrote, or NULL.
 */
static gchar *
shoot_portal (gboolean interactive)
{
  GDBusConnection *connection;
  GVariantBuilder  options;
  GVariant        *ret;
  PortalResponse   response = { NULL, 2, NULL };
  GError          *error    = NULL;
  gchar           *token;
  gchar           *sender;
  gchar           *handle;
  guint            subscription;

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (! connection)
    {
      g_message ("screenshot: no session bus to reach the desktop portal: %s\n",
		 error->message);
      g_error_free (error);
      return NULL;
    }

  token  = g_strdup_printf ("gimp42_screenshot_%u", g_random_int ());
  sender = g_strdup (g_dbus_connection_get_unique_name (connection) + 1);
  g_strdelimit (sender, ".", '_');
  handle = g_strdup_printf ("/org/freedesktop/portal/desktop/request/%s/%s",
			    sender, token);

  response.loop = g_main_loop_new (NULL, FALSE);

  /* Subscribe before the call, so the response cannot be missed.  */
  subscription =
    g_dbus_connection_signal_subscribe (connection,
					"org.freedesktop.portal.Desktop",
					"org.freedesktop.portal.Request",
					"Response",
					handle,
					NULL,
					G_DBUS_SIGNAL_FLAGS_NONE,
					shoot_portal_response,
					&response, NULL);

  g_variant_builder_init (&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&options, "{sv}", "handle_token",
			 g_variant_new_string (token));
  g_variant_builder_add (&options, "{sv}", "interactive",
			 g_variant_new_boolean (interactive));
  g_variant_builder_add (&options, "{sv}", "modal",
			 g_variant_new_boolean (FALSE));

  ret = g_dbus_connection_call_sync (connection,
				     "org.freedesktop.portal.Desktop",
				     "/org/freedesktop/portal/desktop",
				     "org.freedesktop.portal.Screenshot",
				     "Screenshot",
				     g_variant_new ("(sa{sv})", "", &options),
				     G_VARIANT_TYPE ("(o)"),
				     G_DBUS_CALL_FLAGS_NONE,
				     -1, NULL, &error);
  if (ret)
    {
      g_variant_unref (ret);
      g_main_loop_run (response.loop);
    }
  else
    {
      g_message ("screenshot: the desktop portal cannot take a screenshot: %s\n",
		 error->message);
      g_error_free (error);
    }

  g_dbus_connection_signal_unsubscribe (connection, subscription);
  g_main_loop_unref (response.loop);
  g_object_unref (connection);
  g_free (handle);
  g_free (sender);
  g_free (token);

  if (response.response != 0)
    {
      g_free (response.uri);
      return NULL;
    }

  return response.uri;
}

/* The main ScreenShot function */
static void
shoot (gboolean interactive)
{
  GParam *params;
  gint    retvals;
  gchar  *uri;
  gchar  *filename;

  if (shootvals.root != TRUE && ! interactive)
    {
      g_message ("screenshot: grabbing a window by its id is not supported "
		 "on this platform\n");
      return;
    }

  /* A single window is chosen by the user in the portal's own dialog.  */
  uri = shoot_portal (shootvals.root != TRUE);
  if (! uri)
    return;

  filename = g_filename_from_uri (uri, NULL, NULL);
  g_free (uri);
  if (! filename)
    {
      g_message ("screenshot: the portal did not return a local file\n");
      return;
    }

  /* The portal saves the screenshot where the user keeps pictures;
   * load it from there and leave the file alone.
   */
  params = gimp_run_procedure ("gimp_file_load",
			       &retvals,
			       PARAM_INT32, 1,
			       PARAM_STRING, filename,
			       PARAM_STRING, filename,
			       PARAM_END);
  if (params[0].data.d_status == STATUS_SUCCESS && retvals > 1)
    image_ID = params[1].data.d_image;
  gimp_destroy_params (params, retvals);

  g_free (filename);
}

#endif /* G_OS_WIN32 */


/* ScreenShot dialog */

static gint
shoot_dialog (void)
{
  GtkWidget *dialog;
  GtkWidget *frame;
  GtkWidget *vbox;
  GtkWidget *button;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkAdjustment *adj;
  gint radio_pressed[2];
  gint decorations;
  guint delay;

  radio_pressed[0] = (shootvals.root == FALSE);
  radio_pressed[1] = (shootvals.root == TRUE);
  decorations = shootvals.decor;
  delay = shootvals.delay;

  /* Init GTK  */
  gtk_init ();

  /* Main Dialog */
  dialog = gimp_dialog_new (PLUG_IN_PRINT_NAME);
  g_signal_connect (dialog, "destroy",
		    G_CALLBACK (shoot_close_callback), NULL);

  /*  Action area  */
  gimp_dialog_add_button (dialog, "Grab",
			  G_CALLBACK (shoot_ok_callback), dialog, TRUE);
  button = gimp_dialog_add_button (dialog, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy), dialog);

  /*  Single Window */
  frame = gtk_frame_new (NULL);
  gimp_container_set_border_width (frame, 4);
  gimp_box_pack_start (gimp_dialog_get_vbox (dialog), frame, TRUE, TRUE, 0);

  vbox = gimp_vbox_new (FALSE, 4);
  gimp_container_set_border_width (vbox, 4);
  gtk_frame_set_child (GTK_FRAME (frame), vbox);

  hbox = gimp_hbox_new (FALSE, 4);
  gimp_box_pack_start (vbox, hbox, TRUE, TRUE, 0);
  shootint.single_button = gimp_radio_button_new (NULL, "Grab a single window");
#ifdef G_OS_WIN32
  gtk_widget_set_tooltip_text (shootint.single_button,
			       "After Grab, click the window to grab; "
			       "a right click cancels");
#else
  gtk_widget_set_tooltip_text (shootint.single_button,
			       "The desktop asks which window or area to grab");
#endif
  gtk_check_button_set_active (GTK_CHECK_BUTTON (shootint.single_button),
			       radio_pressed[0]);
  gimp_box_pack_start (hbox, shootint.single_button, FALSE, FALSE, 0);
  g_signal_connect (shootint.single_button, "toggled",
		    G_CALLBACK (shoot_toggle_update), &radio_pressed[0]);

  /* with decorations */
  hbox = gimp_hbox_new (FALSE, 4);
  gimp_box_pack_end (vbox, hbox, TRUE, TRUE, 0);
  shootint.decor_button = gtk_check_button_new_with_label ("Include decorations");
  g_signal_connect (shootint.decor_button, "toggled",
		    G_CALLBACK (shoot_toggle_update), &decorations);
  gimp_box_pack_end (hbox, shootint.decor_button, FALSE, FALSE, 0);
#ifndef G_OS_WIN32
  /* The portal decides whether the frame is included.  */
  gtk_widget_set_visible (hbox, FALSE);
#endif

  /* Root Window */
  frame = gtk_frame_new (NULL);
  gimp_container_set_border_width (frame, 4);
  gimp_box_pack_start (gimp_dialog_get_vbox (dialog), frame, TRUE, TRUE, 0);

  vbox = gimp_vbox_new (FALSE, 4);
  gimp_container_set_border_width (vbox, 4);
  gtk_frame_set_child (GTK_FRAME (frame), vbox);

  hbox = gimp_hbox_new (FALSE, 4);
  gimp_box_pack_start (vbox, hbox, TRUE, TRUE, 0);

  shootint.root_button = gimp_radio_button_new (shootint.single_button,
						"Grab the whole screen");
  gimp_box_pack_start (hbox, shootint.root_button, FALSE, FALSE, 0);
  g_signal_connect (shootint.root_button, "toggled",
		    G_CALLBACK (shoot_toggle_update), &radio_pressed[1]);

  /* with delay */
  shootint.delay_box = gimp_hbox_new (FALSE, 4);
  gimp_box_pack_end (vbox, shootint.delay_box, TRUE, TRUE, 0);

  label = gtk_label_new ("after ");
  gimp_box_pack_start (shootint.delay_box, label, TRUE, TRUE, 0);

  adj = gtk_adjustment_new ((gdouble) delay, 0.0, 100.0, 1.0, 5.0, 0.0);
  shootint.delay_spinner = gtk_spin_button_new (adj, 0, 0);
  gimp_box_pack_start (shootint.delay_box, shootint.delay_spinner,
		       FALSE, TRUE, 0);

  label = gtk_label_new (" seconds delay");
  gimp_box_pack_start (shootint.delay_box, label, TRUE, TRUE, 0);

  gtk_check_button_set_active (GTK_CHECK_BUTTON (shootint.decor_button),
			       decorations);
  gtk_widget_set_sensitive (shootint.decor_button, radio_pressed[0]);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (shootint.root_button),
			       radio_pressed[1]);
  gtk_widget_set_sensitive (shootint.delay_box, radio_pressed[1]);

  gtk_window_present (GTK_WINDOW (dialog));

  gimp_main_loop_run ();

  /* Let the dialog disappear from the screen before grabbing it.  */
  if (shootint.run)
    {
      gimp_process_events ();
      g_usleep (G_USEC_PER_SEC / 4);
    }

  shootvals.root = radio_pressed[1];
  shootvals.decor = decorations;

  return shootint.run;
}


/*  ScreenShot interface functions  */

static void
shoot_close_callback (GtkWidget *widget,
		       gpointer   data)
{
  gimp_main_loop_quit ();
}

static void
shoot_ok_callback (GtkWidget *widget,
		    gpointer   data)
{
  shootint.run = TRUE;
  shootvals.delay = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(shootint.delay_spinner));
  gtk_window_destroy (GTK_WINDOW (data));
}

static void
shoot_toggle_update (GtkWidget *widget,
		     gpointer   radio_button)
{
  gint *toggle_val;

  toggle_val = (gint *) radio_button;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    *toggle_val = TRUE;
  else
    *toggle_val = FALSE;

  if (widget == shootint.single_button)
    {
      gtk_widget_set_sensitive (shootint.decor_button, *toggle_val);
      gtk_widget_set_sensitive (shootint.delay_box, !*toggle_val);
    }
  if (widget == shootint.root_button)
    {
      gtk_widget_set_sensitive (shootint.decor_button, !*toggle_val);
      gtk_widget_set_sensitive (shootint.delay_box, *toggle_val);
    }
}

/* Delay functions */

static void
shoot_delay (gint delay)
{
  /* Nothing needs to run meanwhile: the dialog is gone already.  */
  g_usleep ((gulong) delay * G_USEC_PER_SEC);
}

/* Display function */

static void
shoot_display_image (gint32 image)
{
  GParam *params;
  gint retvals;

  params = gimp_run_procedure ("gimp_display_new",
			       &retvals,
			       PARAM_IMAGE, image,
			       PARAM_END);
  gimp_destroy_params (params, retvals);
}
