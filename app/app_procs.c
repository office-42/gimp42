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
#include "config.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gstdio.h>

#ifdef G_OS_WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

#include "libgimp/gimpfeatures.h"

#include "appenv.h"
#include "app_procs.h"
#include "batch.h"
#include "brushes.h"
#include "color_transfer.h"
#include "curves.h"
#include "gdisplay.h"
#include "colormaps.h"
#include "fileops.h"
#include "gimprc.h"
#include "global_edit.h"
#include "gradient.h"
#include "gximage.h"
#include "hue_saturation.h"
#include "image_render.h"
#include "interface.h"
#include "internal_procs.h"
#include "layers_dialog.h"
#include "levels.h"
#include "menus.h"
#include "paint_funcs.h"
#include "palette.h"
#include "patterns.h"
#include "plug_in.h"
#include "procedural_db.h"
#include "temp_buf.h"
#include "tile_swap.h"
#include "tips_dialog.h"
#include "tools.h"
#include "undo.h"
#include "xcf.h"
#include "errors.h"

#define LOGO_WIDTH_MIN 350
#define LOGO_HEIGHT_MIN 110
#define NAME "The GIMP"
#define BROUGHT "brought to you by"
#define AUTHORS "Spencer Kimball and Peter Mattis"

#define SHOW_NEVER 0
#define SHOW_LATER 1
#define SHOW_NOW 2

/*  Function prototype for affirmation dialog when exiting application  */
static void      really_quit_dialog (void);
static Argument* quit_invoker       (Argument *args);
static void make_initialization_status_window(void);
static void destroy_initialization_status_window(void);
static int splash_logo_load (void);
static int splash_logo_load_size (void);


static gint is_app_exit_finish_done = FALSE;

static ProcArg quit_args[] =
{
  { PDB_INT32,
    "kill",
    "Flag specifying whether to kill the gimp process or exit normally" },
};

static ProcRecord quit_proc =
{
  "gimp_quit",
  "Causes the gimp to exit gracefully",
  "The internal procedure which can either be used to make the gimp quit normally, or to have the gimp clean up its resources and exit immediately. The normaly shutdown process allows for querying the user to save any dirty images.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1995-1996",
  PDB_INTERNAL,
  1,
  quit_args,
  0,
  NULL,
  { { quit_invoker } },
};


void
gimp_init (int    gimp_argc,
	   char **gimp_argv)
{
  /* Initialize the application */
  app_init ();

  /* Parse the rest of the command line arguments as images to load */
  if (gimp_argc > 0)
    while (gimp_argc--)
      {
	if (*gimp_argv)
	  file_open (*gimp_argv, *gimp_argv);
	gimp_argv++;
      }

  batch_init ();

  /* Handle showing dialogs with gdk_quit_adds here  */
  if (!no_interface && show_tips)
    tips_dialog_create ();
}


static GtkWidget *logo_area = NULL;
static cairo_surface_t *logo_surface = NULL;
static int logo_width = 0;
static int logo_height = 0;
static int logo_area_width = 0;
static int logo_area_height = 0;
static int show_logo = SHOW_NEVER;
static int max_label_length = 1024;

/*  Opens the splash image, a binary PPM, and reads its header.  */
static FILE *
splash_logo_open (void)
{
  char buf[1024];
  char *filename;
  FILE *fp;

  filename = g_build_filename (gimp_data_directory (), "gimp_splash.ppm", NULL);
  fp = g_fopen (filename, "rb");
  g_free (filename);
  if (!fp)
    return NULL;

  if (!fgets (buf, sizeof (buf), fp) || strncmp (buf, "P6", 2) != 0)
    {
      fclose (fp);
      return NULL;
    }

  /*  a comment line, then the size, then the maximum value  */
  do
    {
      if (!fgets (buf, sizeof (buf), fp))
	{
	  fclose (fp);
	  return NULL;
	}
    }
  while (buf[0] == '#');

  if (sscanf (buf, "%d %d", &logo_width, &logo_height) != 2 ||
      logo_width <= 0 || logo_height <= 0)
    {
      fclose (fp);
      return NULL;
    }

  if (!fgets (buf, sizeof (buf), fp) || strncmp (buf, "255", 3) != 0)
    {
      fclose (fp);
      return NULL;
    }

  return fp;
}

static int
splash_logo_load_size (void)
{
  FILE *fp;

  if (logo_surface)
    return TRUE;

  fp = splash_logo_open ();
  if (!fp)
    return FALSE;

  fclose (fp);
  return TRUE;
}

static int
splash_logo_load (void)
{
  unsigned char *pixelrow;
  unsigned char *data;
  int stride;
  FILE *fp;
  int i, j;

  if (logo_surface)
    return TRUE;

  fp = splash_logo_open ();
  if (!fp)
    return FALSE;

  logo_surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
					     logo_width, logo_height);
  data   = cairo_image_surface_get_data (logo_surface);
  stride = cairo_image_surface_get_stride (logo_surface);
  pixelrow = g_new (guchar, logo_width * 3);

  for (i = 0; i < logo_height; i++)
    {
      guint32 *d = (guint32 *) (data + i * stride);

      if (fread (pixelrow, 1, logo_width * 3, fp) != (size_t) (logo_width * 3))
	{
	  cairo_surface_destroy (logo_surface);
	  logo_surface = NULL;
	  g_free (pixelrow);
	  fclose (fp);
	  return FALSE;
	}

      for (j = 0; j < logo_width; j++)
	d[j] = (0xffu << 24) | (pixelrow[j * 3] << 16) |
	  (pixelrow[j * 3 + 1] << 8) | pixelrow[j * 3 + 2];
    }

  cairo_surface_mark_dirty (logo_surface);

  g_free (pixelrow);
  fclose (fp);
  return TRUE;
}

static void
splash_text_line (GtkWidget  *widget,
		  cairo_t    *cr,
		  const char *text,
		  const char *font,
		  double      y)
{
  PangoLayout *layout;
  PangoFontDescription *desc;
  int w, h;

  layout = gtk_widget_create_pango_layout (widget, text);
  desc = pango_font_description_from_string (font);
  pango_layout_set_font_description (layout, desc);
  pango_font_description_free (desc);

  pango_layout_get_pixel_size (layout, &w, &h);
  cairo_move_to (cr, (logo_area_width - w) / 2.0, y - h);
  pango_cairo_show_layout (cr, layout);

  g_object_unref (layout);
}

static void
splash_draw (GtkDrawingArea *area,
	     cairo_t        *cr,
	     int             width,
	     int             height,
	     gpointer        data)
{
  GtkWidget *widget = GTK_WIDGET (area);
  GdkRGBA fg;

  if (show_logo == SHOW_NOW && logo_surface)
    {
      cairo_set_source_surface (cr, logo_surface,
				(logo_area_width - logo_width) / 2,
				(logo_area_height - logo_height) / 2);
      cairo_paint (cr);
      return;
    }

  gtk_widget_get_color (widget, &fg);
  gdk_cairo_set_source_rgba (cr, &fg);

  splash_text_line (widget, cr, NAME,         "Sans Bold 14", 0.25 * logo_area_height);
  splash_text_line (widget, cr, GIMP_VERSION, "Sans Bold 12", 0.45 * logo_area_height);
  splash_text_line (widget, cr, BROUGHT,      "Sans Bold 12", 0.65 * logo_area_height);
  splash_text_line (widget, cr, AUTHORS,      "Sans Bold 12", 0.80 * logo_area_height);
}

static GtkWidget *win_initstatus = NULL;
static GtkWidget *label1 = NULL;
static GtkWidget *label2 = NULL;
static GtkWidget *pbar = NULL;

static void
destroy_initialization_status_window(void)
{
  if(win_initstatus)
    {
      gtk_window_destroy (GTK_WINDOW (win_initstatus));
      if (logo_surface != NULL)
	cairo_surface_destroy (logo_surface);
      win_initstatus = label1 = label2 = pbar = logo_area = NULL;
      logo_surface = NULL;
    }
}

static gboolean
initialization_status_delete (GtkWindow *window,
			      gpointer   data)
{
  return TRUE;
}

static void
make_initialization_status_window(void)
{
  if (no_interface == FALSE)
    {
      if (no_splash == FALSE)
	{
	  GtkWidget *vbox;

	  win_initstatus = gtk_window_new ();
	  g_signal_connect (win_initstatus, "close-request",
			    G_CALLBACK (initialization_status_delete),
			    NULL);
	  gtk_window_set_title (GTK_WINDOW (win_initstatus), "GIMP Startup");
	  gtk_window_set_resizable (GTK_WINDOW (win_initstatus), FALSE);

	  if (no_splash_image == FALSE && splash_logo_load_size ())
	    {
	      show_logo = SHOW_LATER;
	    }

	  vbox = gimp_vbox_new (FALSE, 4);
	  gimp_container_set_border_width (vbox, 4);
	  gtk_window_set_child (GTK_WINDOW (win_initstatus), vbox);

	  logo_area = gtk_drawing_area_new ();
	  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (logo_area),
					  splash_draw, NULL, NULL);
	  logo_area_width = ( logo_width > LOGO_WIDTH_MIN ) ? logo_width : LOGO_WIDTH_MIN;
	  logo_area_height = ( logo_height > LOGO_HEIGHT_MIN ) ? logo_height : LOGO_HEIGHT_MIN;
	  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (logo_area), logo_area_width);
	  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (logo_area), logo_area_height);
	  gimp_box_pack_start (vbox, logo_area, TRUE, TRUE, 0);

	  label1 = gtk_label_new ("");
	  gimp_box_pack_start (vbox, label1, TRUE, TRUE, 0);
	  label2 = gtk_label_new ("");
	  gtk_label_set_ellipsize (GTK_LABEL (label2), PANGO_ELLIPSIZE_START);
	  gtk_label_set_max_width_chars (GTK_LABEL (label2), 50);
	  gimp_box_pack_start (vbox, label2, TRUE, TRUE, 0);

	  pbar = gtk_progress_bar_new ();
	  gimp_box_pack_start (vbox, pbar, TRUE, TRUE, 0);

	  gtk_window_present (GTK_WINDOW (win_initstatus));
	}
    }
}

void
app_init_update_status(char *label1val,
		       char *label2val,
		       float pct_progress)
{
  char *temp;

  if(no_interface == FALSE && no_splash == FALSE && win_initstatus)
    {
      if(label1val
	 && strcmp(label1val, gtk_label_get_text (GTK_LABEL(label1))))
	{
	  gtk_label_set_text (GTK_LABEL(label1), label1val);
	}
      if(label2val
	 && strcmp(label2val, gtk_label_get_text (GTK_LABEL(label2))))
	{
	  while ( strlen (label2val) > (size_t) max_label_length )
	    {
	      temp = strchr (label2val, G_DIR_SEPARATOR);
	      if (temp == NULL)  /* for sanity */
		break;
	      temp++;
	      label2val = temp;
	    }
	  gtk_label_set_text (GTK_LABEL(label2), label2val);
	}
      if (pct_progress >= 0.0 && pct_progress <= 1.0 &&
	  gtk_progress_bar_get_fraction (GTK_PROGRESS_BAR (pbar)) != pct_progress)
	{
	  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (pbar), pct_progress);
	}

      /*  Let the window show what it has been told.  */
      gimp_process_events ();
    }
}

/* #define RESET_BAR() app_init_update_status("", "", 0) */
#define RESET_BAR()

static void
app_set_icon (void)
{
  char *icons;

  /*  The icon lives in the hicolor theme in share/icons, next to the
   *  data folder (share/gimp42/<version>), wherever the program was
   *  installed; every window takes it.
   */
  icons = g_build_filename (gimp_data_directory (), "..", "..", "icons", NULL);
  gtk_icon_theme_add_search_path
    (gtk_icon_theme_get_for_display (gdk_display_get_default ()), icons);
  gtk_window_set_default_icon_name ("gimp42");
  g_free (icons);
}

void
app_init (void)
{
  char *path;

  if (no_interface == FALSE)
    app_set_icon ();

  make_initialization_status_window();
  app_init_update_status (NULL, NULL, 0.0);

  /*
   *  Initialize the procedural database
   *    We need to do this first because any of the init
   *    procedures might install or query it as needed.
   */
  procedural_db_init ();
  RESET_BAR();
  internal_procs_init ();
  RESET_BAR();
  procedural_db_register (&quit_proc);

  RESET_BAR();
  parse_gimprc ();         /*  parse the local GIMP configuration file  */

  /* Now we are ready to draw the splash-screen-image to the start-up window */
  if (no_interface == FALSE)
    {
      if (no_splash_image == FALSE && show_logo && splash_logo_load ()) {
	show_logo = SHOW_NOW;
	if (logo_area)
	  gtk_widget_queue_draw (logo_area);
	app_init_update_status (NULL, NULL, -1);
      }
    }

  RESET_BAR();
  file_ops_pre_init ();    /*  pre-initialize the file types  */
  RESET_BAR();
  xcf_init ();             /*  initialize the xcf file format routines */

  app_init_update_status ("Looking for data files", "Brushes", 0.00);
  brushes_init (no_data);         /*  initialize the list of gimp brushes  */
  app_init_update_status (NULL, "Patterns", 0.25);
  patterns_init (no_data);        /*  initialize the list of gimp patterns  */
  app_init_update_status (NULL, "Palettes", 0.50);
  palettes_init (no_data);        /*  initialize the list of gimp palettes  */
  app_init_update_status (NULL, "Gradients", 0.75);
  gradients_init (no_data);       /*  initialize the list of gimp gradients  */
  app_init_update_status (NULL, NULL, 1.00);

  plug_in_init ();         /*  initialize the plug in structures  */
  RESET_BAR();
  file_ops_post_init ();   /*  post-initialize the file types  */

  /* Add the swap file  */
  if (swap_path == NULL)
    swap_path = (char *) g_get_tmp_dir ();
  if (!g_file_test (swap_path, G_FILE_TEST_IS_DIR))
    g_mkdir_with_parents (swap_path, 0700);
  {
    char *name = g_strdup_printf ("gimpswap.%ld", (long) getpid ());
    path = g_build_filename (swap_path, name, NULL);
    g_free (name);
  }
  tile_swap_add (path, NULL, NULL);
  g_free (path);

  destroy_initialization_status_window();

  /*  Things to do only if there is an interface  */
  if (no_interface == FALSE)
    {
      get_standard_colormaps ();
      create_toolbox ();
      gximage_init ();
      render_setup (transparency_type, transparency_size);
      tools_options_dialog_new ();
      tools_select (RECT_SELECT);
      message_handler = MESSAGE_BOX;
    }

  color_transfer_init ();
  get_active_brush ();
  get_active_pattern ();
  paint_funcs_setup ();

}

int
app_exit_finish_done (void)
{
  return is_app_exit_finish_done;
}

void
app_exit_finish (void)
{
  if (app_exit_finish_done ())
    return;
  is_app_exit_finish_done = TRUE;

  message_handler = CONSOLE;

  lc_dialog_free ();
  gdisplays_delete ();
  global_edit_free ();
  named_buffers_free ();
  swapping_free ();
  brushes_free ();
  patterns_free ();
  palettes_free ();
  gradients_free ();
  hue_saturation_free ();
  curves_free ();
  levels_free ();
  brush_select_dialog_free ();
  pattern_select_dialog_free ();
  palette_free ();
  paint_funcs_free ();
  plug_in_kill ();
  procedural_db_free ();
  menus_quit ();
  tile_swap_exit ();

  /*  Things to do only if there is an interface  */
  if (no_interface == FALSE)
    {
      gximage_free ();
      render_free ();
      tools_options_dialog_free ();
    }
  gimp_main_loop_quit ();
}

void
app_exit (int kill_it)
{
  /*  If it's the user's perogative, and there are dirty images  */
  if (kill_it == 0 && gdisplays_dirty () && no_interface == FALSE)
    really_quit_dialog ();
  else if (no_interface == FALSE)
    toolbox_free ();
  else
    app_exit_finish ();
}

/********************************************************
 *   Routines to query exiting the application          *
 ********************************************************/

static void
really_quit_callback (GtkButton *button,
		      GtkWidget *dialog)
{
  gtk_window_destroy (GTK_WINDOW (dialog));
  toolbox_free ();
}

static void
really_quit_cancel_callback (GtkWidget *widget,
			     GtkWidget *dialog)
{
  menus_set_sensitive ("<Toolbox>/File/Quit", TRUE);
  menus_set_sensitive ("<Image>/File/Quit", TRUE);
  gtk_window_destroy (GTK_WINDOW (dialog));
}

static gboolean
really_quit_delete_callback (GtkWindow *window,
			     gpointer   client_data)
{
  really_quit_cancel_callback (GTK_WIDGET (window), (GtkWidget *) client_data);

  return TRUE;
}

static void
really_quit_dialog ()
{
  GtkWidget *dialog;
  GtkWidget *label;

  menus_set_sensitive ("<Toolbox>/File/Quit", FALSE);
  menus_set_sensitive ("<Image>/File/Quit", FALSE);

  dialog = gimp_dialog_new ("Really Quit?");
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  g_signal_connect (dialog, "close-request",
		    G_CALLBACK (really_quit_delete_callback),
		    dialog);

  gimp_dialog_add_button (dialog, "Yes", G_CALLBACK (really_quit_callback),
			  dialog, TRUE);
  gimp_dialog_add_button (dialog, "No", G_CALLBACK (really_quit_cancel_callback),
			  dialog, FALSE);

  label = gtk_label_new ("Some files unsaved.  Quit the GIMP?");
  gtk_widget_set_margin_start (label, 10);
  gtk_widget_set_margin_end (label, 10);
  gtk_widget_set_margin_top (label, 10);
  gtk_widget_set_margin_bottom (label, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dialog), label, TRUE, TRUE, 0);

  gtk_window_present (GTK_WINDOW (dialog));
}

static Argument*
quit_invoker (Argument *args)
{
  Argument *return_args;
  int kill_it;

  kill_it = args[0].value.pdb_int;
  app_exit (kill_it);

  return_args = procedural_db_return_args (&quit_proc, TRUE);

  return return_args;
}
