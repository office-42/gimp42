#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "appenv.h"
#include "tips_dialog.h"
#include "gimprc.h"
#include "interface.h"
#include "wilber.h"

#include "config.h"

#define TIPS_FILE_NAME "gimp_tips.txt"

static gboolean tips_dialog_close (GtkWindow *window, gpointer data);
static void tips_dialog_hide (GtkWidget *widget, gpointer data);
static void tips_show_next (GtkWidget *widget, gpointer data);
static void tips_toggle_update (GtkWidget *widget, gpointer data);
static void read_tips_file (char *filename);
static gchar *tips_find_data_file (const gchar *name);

static GtkWidget *tips_dialog = NULL;
static GtkWidget *tips_label;
static char **    tips_text = NULL;
static int        tips_count = 0;
static int        old_show_tips;

void
tips_dialog_create ()
{
  GtkWidget *vbox;
  GtkWidget *hbox1;
  GtkWidget *hbox2;
  GtkWidget *preview;
  GtkWidget *button_close;
  GtkWidget *button_next;
  GtkWidget *button_prev;
  GtkWidget *button_check;
  guchar *   temp;
  guchar *   src;
  guchar *   dest;
  gchar  *   filename;
  int        x;
  int        y;

  if (tips_count == 0)
    {
      filename = tips_find_data_file (TIPS_FILE_NAME);
      read_tips_file (filename);
      g_free (filename);
    }

  if (last_tip >= tips_count || last_tip < 0)
    last_tip = 0;

  if (!tips_dialog)
    {
      tips_dialog = gtk_window_new ();
      gtk_window_set_title (GTK_WINDOW (tips_dialog), "GIMP Tip of the day");
      g_signal_connect (tips_dialog, "close-request",
			G_CALLBACK (tips_dialog_close), NULL);

      vbox = gimp_vbox_new (FALSE, 0);
      gtk_window_set_child (GTK_WINDOW (tips_dialog), vbox);

      hbox1 = gimp_hbox_new (FALSE, 5);
      gimp_container_set_border_width (hbox1, 10);
      gimp_box_pack_start (vbox, hbox1, FALSE, TRUE, 0);

      hbox2 = gimp_hbox_new (FALSE, 5);
      gimp_container_set_border_width (hbox2, 10);
      gimp_box_pack_end (vbox, hbox2, FALSE, TRUE, 0);

      preview = gimp_preview_new (GIMP_PREVIEW_COLOR);
      gimp_preview_size (GIMP_PREVIEW (preview), wilber_width, wilber_height);
      temp = g_malloc (wilber_width * 3);
      src = (guchar *)wilber_data;
      for (y = 0; y < (int) wilber_height; y++)
	{
	  dest = temp;
	  for (x = 0; x < (int) wilber_width; x++)
	    {
	      HEADER_PIXEL(src, dest);
	      dest += 3;
	    }
	  gimp_preview_draw_row (GIMP_PREVIEW (preview), temp,
				 0, y, wilber_width);
	}
      g_free(temp);
      gimp_box_pack_end (hbox1, preview, FALSE, TRUE, 3);

      tips_label = gtk_label_new (tips_text[last_tip]);
      gtk_label_set_justify (GTK_LABEL (tips_label), GTK_JUSTIFY_LEFT);
      gimp_box_pack_start (hbox1, tips_label, TRUE, TRUE, 3);

      button_close = gtk_button_new_with_label ("Close");
      g_signal_connect (button_close, "clicked",
			G_CALLBACK (tips_dialog_hide), NULL);
      gimp_box_pack_end (hbox2, button_close, FALSE, TRUE, 0);

      button_next = gtk_button_new_with_label ("Next Tip");
      g_signal_connect (button_next, "clicked",
			G_CALLBACK (tips_show_next),
			(gpointer) "next");
      gimp_box_pack_end (hbox2, button_next, FALSE, TRUE, 0);

      button_prev = gtk_button_new_with_label ("Prev. Tip");
      g_signal_connect (button_prev, "clicked",
			G_CALLBACK (tips_show_next),
			(gpointer) "prev");
      gimp_box_pack_end (hbox2, button_prev, FALSE, TRUE, 0);

      button_check = gtk_check_button_new_with_label ("Show tip next time");
      gtk_check_button_set_active (GTK_CHECK_BUTTON (button_check),
				   show_tips);
      g_signal_connect (button_check, "toggled",
			G_CALLBACK (tips_toggle_update),
			(gpointer) &show_tips);
      gimp_box_pack_start (hbox2, button_check, FALSE, TRUE, 0);

      old_show_tips = show_tips;
    }

  gtk_window_present (GTK_WINDOW (tips_dialog));
}

static gboolean
tips_dialog_close (GtkWindow *window,
		   gpointer   data)
{
  tips_dialog_hide (GTK_WIDGET (window), data);

  return TRUE;
}

static void
tips_dialog_hide (GtkWidget *widget,
		  gpointer data)
{
  GList *update = NULL; /* options that should be updated in .gimprc */
  GList *remove = NULL; /* options that should be commented out */

  gtk_widget_set_visible (tips_dialog, FALSE);

  update = g_list_append (update, "last-tip-shown"); /* always save this */
  if (show_tips != old_show_tips)
    {
      update = g_list_append (update, "show-tips");
      remove = g_list_append (remove, "dont-show-tips");
      old_show_tips = show_tips;
    }
  last_tip++;
  save_gimprc (&update, &remove);
  last_tip--;
  g_list_free (update);
  g_list_free (remove);
}

static void
tips_show_next (GtkWidget *widget,
		gpointer  data)
{
  if (!strcmp ((char *)data, "prev"))
    {
      last_tip--;
      if (last_tip < 0)
	last_tip = tips_count - 1;
    }
  else
    {
      last_tip++;
      if (last_tip >= tips_count)
	last_tip = 0;
    }
  gtk_label_set_text (GTK_LABEL (tips_label), tips_text[last_tip]);
}

static void
tips_toggle_update (GtkWidget *widget,
		    gpointer   data)
{
  int *toggle_val;

  toggle_val = (int *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
    *toggle_val = TRUE;
  else
    *toggle_val = FALSE;
}

/*  Looks for a data file: in $GIMP_DATADIR, in the installed data
 *  directory (found relative to the executable on Windows), then in the
 *  source tree when running from the build directory.  Returns the
 *  first candidate when none exists, so the caller's error handling
 *  still applies.
 */
static gchar *
tips_find_data_file (const gchar *name)
{
  gchar *dir;
  gchar *path;
  gchar *first = NULL;
  gchar *candidates[5];
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
  candidates[n++] = g_build_filename (GIMP_BUILD_SRCDIR, "data", name, NULL);

  path = NULL;
  for (i = 0; i < n; i++)
    {
      if (!path && g_file_test (candidates[i], G_FILE_TEST_IS_REGULAR))
	path = candidates[i];
      else if (i == 0)
	first = candidates[i];
      else
	g_free (candidates[i]);
    }

  if (path)
    {
      g_free (first);
      return path;
    }

  return first;
}

static void
store_tip (char *str)
{
  tips_count++;
  tips_text = g_realloc(tips_text, sizeof(char *) * tips_count);
  tips_text[tips_count - 1] = str;
}

static void
read_tips_file (char *filename)
{
  FILE *fp;
  char *tip = NULL;
  char *str = NULL;
  size_t len;

  fp = fopen (filename, "rb");
  if (!fp)
    {
      store_tip ("Your GIMP tips file appears to be missing!\n"
		 "There should be a file called " TIPS_FILE_NAME " in the\n"
		 "GIMP data directory.  Please check your installation.");
      return;
    }

  str = g_new (char, 1024);
  while (!feof (fp))
    {
      if (!fgets (str, 1024, fp))
	continue;

      /*  the file is read in binary mode: drop a CR before the LF  */
      len = strlen (str);
      if (len >= 2 && str[len - 2] == '\r' && str[len - 1] == '\n')
	{
	  str[len - 2] = '\n';
	  str[len - 1] = '\0';
	}

      if (str[0] == '#' || str[0] == '\n')
	{
	  if (tip != NULL)
	    {
	      tip[strlen (tip) - 1] = '\000';
	      store_tip (tip);
	      tip = NULL;
	    }
	}
      else
	{
	  if (tip == NULL)
	    {
	      tip = g_malloc (strlen (str) + 1);
	      strcpy (tip, str);
	    }
	  else
	    {
	      tip = g_realloc (tip, strlen (tip) + strlen (str) + 1);
	      strcat (tip, str);
	    }
	}
    }
  if (tip != NULL)
    store_tip (tip);
  g_free (str);
  fclose (fp);
}
