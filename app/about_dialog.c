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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <gtk/gtk.h>

#include "libgimp/gimpfeatures.h"

#include "appenv.h"
#include "gimprc.h"
#include "about_dialog.h"
#include "interface.h"

#include "config.h"

#define ANIMATION_STEPS 16
#define ANIMATION_SIZE 2

static int      about_dialog_load_logo (void);
static void     about_dialog_destroy (void);
static void     about_dialog_unmap (void);
static void     about_dialog_logo_map (GtkWidget *widget, gpointer data);
static void     about_dialog_logo_draw (GtkDrawingArea *area, cairo_t *cr,
					int width, int height, gpointer data);
static void     about_dialog_scroll_draw (GtkDrawingArea *area, cairo_t *cr,
					  int width, int height, gpointer data);
static void     about_dialog_button (GtkGestureClick *gesture, int n_press,
				     double x, double y, gpointer data);
static gboolean about_dialog_timer (gpointer data);
static gchar *  about_dialog_find_data_file (const gchar *name);


static GtkWidget *about_dialog = NULL;
static GtkWidget *logo_area = NULL;
static GtkWidget *scroll_area = NULL;
static cairo_surface_t *logo_surface = NULL;
static unsigned char *dissolve_map = NULL;
static int dissolve_width;
static int dissolve_height;
static int logo_width = 0;
static int logo_height = 0;
static int do_animation = 0;
static int do_scrolling = 0;
static int scroll_state = 0;
static int frame = 0;
static int offset = 0;
static guint timer = 0;

static char *scroll_text[] =
{
  "Lauri Alanko",
  "Shawn Amundson",
  "John Beale",
  "Zach Beane",
  "Tom Bech",
  "Marc Bless",
  "Edward Blevins",
  "Roberto Boyd",
  "Seth Burgess",
  "Brent Burton",
  "Francisco Bustamante",
  "Ed Connel",
  "Andreas Dilger",
  "Misha Dynin",
  "Larry Ewing",
  "David Forsyth",
  "Jim Geuther",
  "Scott Goehring",
  "Heiko Goller",
  "Michael Hammel",
  "Christoph Hoegl",
  "Jan Hubicka",
  "Simon Janes",
  "Tim Janik",
  "Tuomas Kuosmanen",
  "Peter Kirchgessner",
  "Nick Lamb",
  "Karl LaRocca",
  "Jens Lautenbacher",
  "Laramie Leavitt",
  "Elliot Lee",
  "Raph Levien",
  "Adrian Likins",
  "Ingo Luetkebohle",
  "Josh MacDonald",
  "Ed Mackey",
  "Marcelo Malheiros",
  "Ian Main",
  "Torsten Martinsen",
  "Federico Mena",
  "Adam D. Moss",
  "Shuji Narazaki",
  "Sven Neumann",
  "Stephen Robert Norris",
  "Erik Nygren",
  "Miles O'Neal",
  "Jay Painter",
  "Mike Phillips",
  "Raphael Quinet",
  "James Robinson",
  "Mike Schaeffer",
  "Tracy Scott",
  "Manish Singh",
  "Nathan Summers",
  "Mike Sweet",
  "Eiichi Takamori",
  "Tristan Tarrant",
  "Owen Taylor",
  "Ian Tester",
  "Andy Thomas",
  "James Wang",
  "Kris Wehner",
  "Matthew Wilson",
};
static int nscroll_texts = sizeof (scroll_text) / sizeof (scroll_text[0]);
static int scroll_text_widths[100] = { 0 };
static int draw_offset = 0;
static int cur_scroll_text = 0;
static int cur_scroll_index = 0;

static int shuffle_array[ sizeof(scroll_text) / sizeof(scroll_text[0]) ];

void
about_dialog_create (int timeout)
{
  GtkWidget *vbox;
  GtkWidget *aboutframe;
  GtkWidget *label;
  GtkGesture *click;
  PangoLayout *layout;
  PangoRectangle ink, logical;
  gint max_width;
  gint max_height;
  gint i;

  if (!about_dialog)
    {
      if (!about_dialog_load_logo ())
	return;

      about_dialog = gtk_window_new ();
      gtk_window_set_title (GTK_WINDOW (about_dialog), "About the GIMP");
      gtk_window_set_resizable (GTK_WINDOW (about_dialog), FALSE);
      gtk_window_set_hide_on_close (GTK_WINDOW (about_dialog), TRUE);
      g_signal_connect (about_dialog, "destroy",
			G_CALLBACK (about_dialog_destroy), NULL);
      g_signal_connect (about_dialog, "unmap",
			G_CALLBACK (about_dialog_unmap), NULL);

      /*  a click anywhere in the window closes it  */
      click = gtk_gesture_click_new ();
      gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 0);
      g_signal_connect (click, "pressed",
			G_CALLBACK (about_dialog_button), NULL);
      gtk_widget_add_controller (about_dialog, GTK_EVENT_CONTROLLER (click));

      vbox = gimp_vbox_new (FALSE, 1);
      gimp_container_set_border_width (vbox, 1);
      gtk_window_set_child (GTK_WINDOW (about_dialog), vbox);

      aboutframe = gtk_frame_new (NULL);
      gimp_box_pack_start (vbox, aboutframe, TRUE, TRUE, 0);

      logo_area = gtk_drawing_area_new ();
      gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (logo_area), logo_width);
      gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (logo_area), logo_height);
      gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (logo_area),
				      about_dialog_logo_draw, NULL, NULL);
      g_signal_connect (logo_area, "map",
			G_CALLBACK (about_dialog_logo_map), NULL);
      gtk_frame_set_child (GTK_FRAME (aboutframe), logo_area);

      label = gtk_label_new (NULL);
      gtk_label_set_markup (GTK_LABEL (label),
			    "<span size=\"large\">Version " GIMP_VERSION
			    " brought to you by</span>");
      gimp_box_pack_start (vbox, label, FALSE, TRUE, 0);

      label = gtk_label_new (NULL);
      gtk_label_set_markup (GTK_LABEL (label),
			    "<span size=\"large\">Spencer Kimball and Peter Mattis</span>");
      gimp_box_pack_start (vbox, label, FALSE, TRUE, 0);

      aboutframe = gtk_frame_new (NULL);
      gtk_widget_set_halign (aboutframe, GTK_ALIGN_CENTER);
      gtk_widget_set_valign (aboutframe, GTK_ALIGN_CENTER);
      gimp_box_pack_start (vbox, aboutframe, FALSE, TRUE, 0);

      scroll_area = gtk_drawing_area_new ();
      gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (scroll_area),
				      about_dialog_scroll_draw, NULL, NULL);
      gtk_frame_set_child (GTK_FRAME (aboutframe), scroll_area);

      max_width = 0;
      max_height = 0;
      layout = gtk_widget_create_pango_layout (scroll_area, NULL);
      for (i = 0; i < nscroll_texts; i++)
	{
	  pango_layout_set_text (layout, scroll_text[i], -1);
	  pango_layout_get_pixel_extents (layout, &ink, &logical);
	  scroll_text_widths[i] = logical.width;
	  max_width = MAX (max_width, logical.width);
	  max_height = MAX (max_height, logical.height);
	}
      g_object_unref (layout);

      gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (scroll_area),
					  max_width + 10);
      gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (scroll_area),
					   max_height);

      label = gtk_label_new ("Please visit http://www.gimp.org/ for more info");
      gimp_box_pack_start (vbox, label, FALSE, TRUE, 0);
    }

  if (!gtk_widget_get_visible (about_dialog))
    {
      do_animation = TRUE;
      do_scrolling = FALSE;
      scroll_state = 0;
      frame = 0;
      offset = 0;

      for (i = 0; i < nscroll_texts; i++)
	{
	  shuffle_array[i] = i;
	}

      for (i = 0; i < nscroll_texts; i++)
	{
	  int j, k;
	  j = rand() % nscroll_texts;
	  k = rand() % nscroll_texts;
	  if (j != k)
	    {
	      int t;
	      t = shuffle_array[j];
	      shuffle_array[j] = shuffle_array[k];
	      shuffle_array[k] = t;
	    }
	}

      gtk_window_present (GTK_WINDOW (about_dialog));
    }
  else
    {
      gtk_window_present (GTK_WINDOW (about_dialog));
    }
}

/*  Looks for a data file: in $GIMP_DATADIR, in the installed data
 *  directory (found relative to the executable on Windows), then in the
 *  source tree when running from the build directory.
 */
static gchar *
about_dialog_find_data_file (const gchar *name)
{
  gchar *dir;
  gchar *candidates[6];
  gchar *path = NULL;
  int n = 0;
  int i;

  candidates[n++] = g_build_filename (gimp_data_directory (), name, NULL);

#ifdef G_OS_WIN32
  dir = g_win32_get_package_installation_directory_of_module (NULL);
#else
  dir = g_strdup (GIMP42_PREFIX);
#endif
  candidates[n++] = g_build_filename (dir, GIMP42_DATADIR_REL, name, NULL);
  g_free (dir);

  candidates[n++] = g_build_filename (GIMP42_PREFIX, GIMP42_DATADIR_REL, name, NULL);
  candidates[n++] = g_build_filename (GIMP_BUILD_SRCDIR, name, NULL);
  candidates[n++] = g_build_filename (GIMP_BUILD_SRCDIR, "data", "images", name, NULL);

  for (i = 0; i < n; i++)
    {
      if (!path && g_file_test (candidates[i], G_FILE_TEST_IS_REGULAR))
	path = candidates[i];
      else
	g_free (candidates[i]);
    }

  return path;
}

/*  Reads the next number from a PPM header, skipping white space and
 *  comments.
 */
static int
about_dialog_ppm_number (FILE *fp,
			 int  *value)
{
  int c;

  do
    {
      c = getc (fp);
      if (c == '#')
	while (c != EOF && c != '\n')
	  c = getc (fp);
    }
  while (c != EOF && isspace (c));

  if (c == EOF || !isdigit (c))
    return FALSE;

  *value = 0;
  while (c != EOF && isdigit (c))
    {
      *value = *value * 10 + (c - '0');
      c = getc (fp);
    }

  /*  c is the single white space character that ends the number  */
  return TRUE;
}

static int
about_dialog_load_logo (void)
{
  gchar *filename;
  unsigned char *pixelrow;
  unsigned char *data;
  FILE *fp;
  int count;
  int stride;
  int maxval;
  int i, j, k;

  if (logo_surface)
    return TRUE;

  filename = about_dialog_find_data_file ("gimp_logo.ppm");
  if (!filename)
    return 0;

  fp = fopen (filename, "rb");
  g_free (filename);
  if (!fp)
    return 0;

  if (getc (fp) != 'P' || getc (fp) != '6' ||
      !about_dialog_ppm_number (fp, &logo_width) ||
      !about_dialog_ppm_number (fp, &logo_height) ||
      !about_dialog_ppm_number (fp, &maxval) ||
      maxval != 255 || logo_width <= 0 || logo_height <= 0)
    {
      fclose (fp);
      return 0;
    }

  logo_surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
					     logo_width, logo_height);
  cairo_surface_flush (logo_surface);
  data = cairo_image_surface_get_data (logo_surface);
  stride = cairo_image_surface_get_stride (logo_surface);
  pixelrow = g_new (guchar, logo_width * 3);

  for (i = 0; i < logo_height; i++)
    {
      guint32 *dest = (guint32 *) (data + i * stride);

      count = fread (pixelrow, sizeof (unsigned char), logo_width * 3, fp);
      if (count != (logo_width * 3))
	{
	  cairo_surface_destroy (logo_surface);
	  logo_surface = NULL;
	  g_free (pixelrow);
	  fclose (fp);
	  return 0;
	}

      for (j = 0; j < logo_width; j++)
	dest[j] = (((guint32) pixelrow[j * 3 + 0] << 16) |
		   ((guint32) pixelrow[j * 3 + 1] << 8) |
		   ((guint32) pixelrow[j * 3 + 2]));
    }
  cairo_surface_mark_dirty (logo_surface);

  g_free (pixelrow);

  fclose (fp);

  dissolve_width = (logo_width / ANIMATION_SIZE)+(logo_width % ANIMATION_SIZE ==0 ?0 : 1);
  dissolve_height = (logo_height / ANIMATION_SIZE)+(logo_height % ANIMATION_SIZE ==0 ?0 : 1);

  dissolve_map = g_new (guchar, dissolve_width * dissolve_height);

  srand (time (NULL));

  for (i = 0, k = 0; i < dissolve_height; i++)
    for (j = 0; j < dissolve_width; j++, k++)
      dissolve_map[k] = rand () % ANIMATION_STEPS;

  return TRUE;
}

static void
about_dialog_destroy (void)
{
  about_dialog = NULL;
  about_dialog_unmap ();
}

static void
about_dialog_unmap (void)
{
  if (timer)
    {
      g_source_remove (timer);
      timer = 0;
    }
}

static void
about_dialog_logo_map (GtkWidget *widget,
		       gpointer   data)
{
  /*  the dissolve starts once the logo is on screen  */
  if (do_animation && !timer)
    timer = g_timeout_add (75, about_dialog_timer, NULL);
}

static void
about_dialog_logo_draw (GtkDrawingArea *area,
			cairo_t        *cr,
			int             width,
			int             height,
			gpointer        data)
{
  int i, j, k;

  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  cairo_paint (cr);

  if (!logo_surface)
    return;

  if (do_animation)
    {
      /*  the cells whose turn has come so far  */
      for (i = 0, k = 0; i < dissolve_height; i++)
	for (j = 0; j < dissolve_width; j++, k++)
	  if (dissolve_map[k] < frame)
	    cairo_rectangle (cr, j * ANIMATION_SIZE, i * ANIMATION_SIZE,
			     ANIMATION_SIZE, ANIMATION_SIZE);
      cairo_clip (cr);
    }

  cairo_set_source_surface (cr, logo_surface, 0, 0);
  cairo_rectangle (cr, 0, 0, logo_width, logo_height);
  cairo_fill (cr);
}

static void
about_dialog_scroll_draw (GtkDrawingArea *area,
			  cairo_t        *cr,
			  int             width,
			  int             height,
			  gpointer        data)
{
  PangoLayout *layout;

  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_paint (cr);

  if (!do_scrolling)
    return;

  layout = gtk_widget_create_pango_layout (GTK_WIDGET (area),
					   scroll_text[cur_scroll_text]);
  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  cairo_move_to (cr, width - draw_offset, 0);
  pango_cairo_show_layout (cr, layout);
  g_object_unref (layout);
}

static void
about_dialog_button (GtkGestureClick *gesture,
		     int              n_press,
		     double           x,
		     double           y,
		     gpointer         data)
{
  if (timer)
    g_source_remove (timer);
  timer = 0;
  frame = 0;

  gtk_widget_set_visible (about_dialog, FALSE);
}

static gboolean
about_dialog_timer (gpointer data)
{
  gboolean return_val;
  int width;

  return_val = G_SOURCE_CONTINUE;

  if (do_animation)
    {
      if (gtk_widget_get_mapped (logo_area))
	{
	  frame += 1;
	  gtk_widget_queue_draw (logo_area);

	  if (frame == ANIMATION_STEPS)
	    {
	      do_animation = FALSE;
	      do_scrolling = TRUE;
	      frame = 0;

	      timer = g_timeout_add (75, about_dialog_timer, NULL);

	      return G_SOURCE_REMOVE;
	    }
	}
    }

  if (do_scrolling)
    {
      width = gtk_widget_get_width (scroll_area);

      switch (scroll_state)
	{
	case 1:
	  scroll_state = 2;
	  timer = g_timeout_add (700, about_dialog_timer, NULL);
	  return_val = G_SOURCE_REMOVE;
	  break;
	case 2:
	  scroll_state = 3;
	  timer = g_timeout_add (75, about_dialog_timer, NULL);
	  return_val = G_SOURCE_REMOVE;
	  break;
	}

      if (offset > (scroll_text_widths[cur_scroll_text] + width))
	{
	  scroll_state = 0;
	  cur_scroll_index += 1;
	  if (cur_scroll_index == nscroll_texts)
	    cur_scroll_index = 0;

	  cur_scroll_text = shuffle_array[cur_scroll_index];

	  offset = 0;
	}

      draw_offset = offset;
      gtk_widget_queue_draw (scroll_area);

      offset += 15;
      if (scroll_state == 0)
	{
	  if (offset > ((width + scroll_text_widths[cur_scroll_text]) / 2))
	    {
	      scroll_state = 1;
	      offset = (width + scroll_text_widths[cur_scroll_text]) / 2;
	    }
	}
    }

  return return_val;
}
