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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>
#include "appenv.h"
#include "buildmenu.h"
#include "colormaps.h"
#include "color_area.h"
#include "color_select.h"
#include "datafiles.h"
#include "errors.h"
#include "general.h"
#include "gimprc.h"
#include "interface.h"
#include "palette.h"

#define ENTRY_WIDTH  14
#define ENTRY_HEIGHT 10
#define SPACING 1
#define COLUMNS 16
#define ROWS 16

#define PREVIEW_WIDTH ((ENTRY_WIDTH * COLUMNS) + (SPACING * (COLUMNS + 1)))
#define PREVIEW_HEIGHT ((ENTRY_HEIGHT * ROWS) + (SPACING * (ROWS + 1)))

typedef struct _Palette _Palette, *PaletteP;

struct _Palette {
  GtkWidget *shell;
  GtkWidget *vbox;
  GtkWidget *frame;
  GtkWidget *option_menu;
  GtkWidget *color_area;
  GtkWidget *color_name;
  GtkWidget *palette_ops;
  cairo_surface_t *surface;       /*  the rendered entries  */
  GtkAdjustment *sbar_data;
  PaletteEntriesP entries;
  PaletteEntryP color;
  ColorSelectP color_select;
  int color_select_active;
  int scroll_offset;
  int updating;
};

typedef void (*PaletteOpCallback) (GtkWidget *, gpointer);

typedef struct
{
  char *label;
  PaletteOpCallback callback;
} PaletteOp;

static void palette_create_palette_menu (PaletteP, PaletteEntriesP);
static PaletteEntryP palette_add_entry (PaletteEntriesP, char *, int, int, int);
static void palette_delete_entry (PaletteP);
static void palette_calc_scrollbar (PaletteP);

static void palette_entries_load (char *);
static GSList * palette_entries_insert_list (GSList *, PaletteEntriesP);
static void palette_entries_delete (char *);
static void palette_entries_save (PaletteEntriesP, char *);
static void palette_entries_free (PaletteEntriesP);
static void palette_entry_free (PaletteEntryP);
static void palette_entries_set_callback (GtkWidget *, gpointer);

static void palette_change_color (int, int, int, int);
static void palette_color_area_draw (GtkDrawingArea *, cairo_t *, int, int, gpointer);
static void palette_color_area_pressed (GtkGestureClick *, gint, gdouble, gdouble, gpointer);
static void palette_scroll_update (GtkAdjustment *, gpointer);
static void palette_new_callback (GtkWidget *, gpointer);
static void palette_delete_callback (GtkWidget *, gpointer);
static void palette_refresh_callback (GtkWidget *, gpointer);
static void palette_edit_callback (GtkWidget *, gpointer);
static void palette_close_callback (GtkWidget *, gpointer);
static gboolean palette_dialog_delete_callback (GtkWindow *, gpointer);
static void palette_new_entries_callback (GtkWidget *, gpointer);
static void palette_add_entries_callback (GtkWidget *, gpointer, gpointer);
/* static void palette_merge_entries_callback (GtkWidget *, gpointer); */
static void palette_delete_entries_callback (GtkWidget *, gpointer);
static void palette_select_callback (int, int, int, ColorSelectState, void *);

static void palette_draw_entries (PaletteP);
static void palette_draw_current_entry (PaletteP);
static void palette_update_current_entry (PaletteP);

GSList *palette_entries_list = NULL;

static PaletteP         palette = NULL;
static PaletteEntriesP  default_palette_entries = NULL;
static int              num_palette_entries = 0;
static unsigned char    foreground[3] = { 0, 0, 0 };
static unsigned char    background[3] = { 255, 255, 255 };

/*  Color select dialog  */
/* static ColorSelectP color_select = NULL;
static int color_select_active = 0; */

/*  The buttons of the action area  */
static PaletteOp action_items[] =
{
  { "New", palette_new_callback },
  { "Edit", palette_edit_callback },
  { "Delete", palette_delete_callback },
  { "Close", palette_close_callback },
};

/*  The "Ops" pulldown  */
static PaletteOp palette_ops[] =
{
  { "New Palette", palette_new_entries_callback },
  { "Delete Palette", palette_delete_entries_callback },
  { "Refresh Palettes", palette_refresh_callback },
  { "Close", palette_close_callback },
  { NULL, NULL },
};

void
palettes_init (int no_data)
{
  palette_init_palettes (no_data);
}

void
palettes_free ()
{
  palette_free_palettes ();
}

static void
palette_ops_item_clicked (GtkWidget *button,
			  gpointer   data)
{
  PaletteOp *op = (PaletteOp *) data;
  GtkWidget *popover;

  popover = gtk_widget_get_ancestor (button, GTK_TYPE_POPOVER);
  if (popover)
    gtk_popover_popdown (GTK_POPOVER (popover));

  if (op->callback)
    (* op->callback) (button, palette);
}

/*  The "Ops" menu: a menu button whose popover holds one button per
 *  operation (it was a menu bar with a single pulldown).
 */
static GtkWidget *
palette_ops_menu_new (void)
{
  GtkWidget *menu_button;
  GtkWidget *popover;
  GtkWidget *box;
  GtkWidget *button;
  int i;

  menu_button = gtk_menu_button_new ();
  gtk_menu_button_set_label (GTK_MENU_BUTTON (menu_button), "Ops");

  popover = gtk_popover_new ();
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_popover_set_child (GTK_POPOVER (popover), box);

  for (i = 0; palette_ops[i].label; i++)
    {
      button = gtk_button_new_with_label (palette_ops[i].label);
      gtk_button_set_has_frame (GTK_BUTTON (button), FALSE);
      gtk_label_set_xalign (GTK_LABEL (gtk_button_get_child (GTK_BUTTON (button))), 0.0);
      g_signal_connect (button, "clicked",
			G_CALLBACK (palette_ops_item_clicked), &palette_ops[i]);
      gtk_box_append (GTK_BOX (box), button);
    }

  gtk_menu_button_set_popover (GTK_MENU_BUTTON (menu_button), popover);

  return menu_button;
}

void
palette_create ()
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *sbar;
  GtkWidget *frame;
  GtkWidget *options_box;
  GtkGesture *click;
  int i;

  if (!palette)
    {
      palette = g_malloc (sizeof (_Palette));

      palette->entries = default_palette_entries;
      palette->color = NULL;
      palette->color_select = NULL;
      palette->color_select_active = 0;
      palette->scroll_offset = 0;
      palette->updating = FALSE;
      palette->surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
						     PREVIEW_WIDTH,
						     PREVIEW_HEIGHT);

      /*  The shell and main vbox  */
      palette->shell = gimp_dialog_new ("Color Palette");
      gtk_window_set_resizable (GTK_WINDOW (palette->shell), FALSE);
      gtk_window_set_hide_on_close (GTK_WINDOW (palette->shell), TRUE);
      vbox = gimp_vbox_new (FALSE, 1);
      gimp_container_set_border_width (vbox, 1);
      gimp_box_pack_start (gimp_dialog_get_vbox (palette->shell), vbox, TRUE, TRUE, 0);
      palette->vbox = vbox;

      /* handle the wm close event */
      g_signal_connect (palette->shell, "close-request",
			G_CALLBACK (palette_dialog_delete_callback),
			palette);

      /*  The palette options box  */
      options_box = gimp_hbox_new (FALSE, 1);
      gimp_box_pack_start (vbox, options_box, FALSE, FALSE, 0);

      /*  The palette commands pulldown menu  */
      palette->palette_ops = palette_ops_menu_new ();
      gimp_box_pack_start (options_box, palette->palette_ops, FALSE, FALSE, 0);

      /*  The option menu  */
      palette->option_menu = gimp_option_menu_new ();
      gimp_box_pack_start (options_box, palette->option_menu, TRUE, TRUE, 0);

      /*  The active color name  */
      palette->color_name = gtk_entry_new ();
      gtk_editable_set_text (GTK_EDITABLE (palette->color_name), "Active Color Name");
      gimp_box_pack_start (vbox, palette->color_name, FALSE, FALSE, 0);

      /*  The horizontal box containing preview & scrollbar  */
      hbox = gimp_hbox_new (FALSE, 1);
      gimp_box_pack_start (vbox, hbox, TRUE, TRUE, 0);
      frame = gtk_frame_new (NULL);
      gtk_widget_set_valign (frame, GTK_ALIGN_START);
      gimp_box_pack_start (hbox, frame, FALSE, FALSE, 0);
      palette->frame = frame;
      palette->sbar_data = gtk_adjustment_new (0, 0, PREVIEW_HEIGHT, 1, 1, PREVIEW_HEIGHT);
      g_signal_connect (palette->sbar_data, "value-changed",
			G_CALLBACK (palette_scroll_update),
			palette);
      sbar = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, palette->sbar_data);
      gimp_box_pack_start (hbox, sbar, FALSE, FALSE, 0);

      /*  Create the color area  */
      palette->color_area = gtk_drawing_area_new ();
      gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (palette->color_area),
					  PREVIEW_WIDTH);
      gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (palette->color_area),
					   PREVIEW_HEIGHT);
      gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (palette->color_area),
				      palette_color_area_draw, palette, NULL);
      click = gtk_gesture_click_new ();
      gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 0);
      g_signal_connect (click, "pressed",
			G_CALLBACK (palette_color_area_pressed), palette);
      gtk_widget_add_controller (palette->color_area, GTK_EVENT_CONTROLLER (click));
      gtk_frame_set_child (GTK_FRAME (frame), palette->color_area);

      if(no_data)
	 palettes_init(FALSE);

      /*  The action area  */
      for (i = 0; i < (int) G_N_ELEMENTS (action_items); i++)
	gimp_dialog_add_button (palette->shell, action_items[i].label,
				G_CALLBACK (action_items[i].callback),
				palette, i == 0);

      gtk_window_present (GTK_WINDOW (palette->shell));

      palette_create_palette_menu (palette, default_palette_entries);
      palette_calc_scrollbar (palette);
    }
  else
    {
      gtk_window_present (GTK_WINDOW (palette->shell));
    }
}

void
palette_free ()
{
  if (palette)
    {
      if (palette->color_select)
	color_select_free (palette->color_select);

      gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (palette->color_area),
				      NULL, NULL, NULL);
      g_signal_handlers_disconnect_by_data (palette->sbar_data, palette);
      g_signal_handlers_disconnect_by_data (palette->shell, palette);
      gtk_window_destroy (GTK_WINDOW (palette->shell));
      cairo_surface_destroy (palette->surface);

      g_free (palette);

      palette = NULL;
    }
}

void
palette_get_foreground (unsigned char *r,
			unsigned char *g,
			unsigned char *b)
{
  *r = foreground[0];
  *g = foreground[1];
  *b = foreground[2];
}

void
palette_get_background (unsigned char *r,
			unsigned char *g,
			unsigned char *b)
{
  *r = background[0];
  *g = background[1];
  *b = background[2];
}

void
palette_set_foreground (int r,
			int g,
			int b)
{
  /*  Foreground  */
  foreground[0] = r;
  foreground[1] = g;
  foreground[2] = b;

  if (no_interface == FALSE)
    color_area_update ();
}

void
palette_set_background (int r,
			int g,
			int b)
{
  /*  Background  */
  background[0] = r;
  background[1] = g;
  background[2] = b;

  if (no_interface == FALSE)
    color_area_update ();
}

void
palette_set_default_colors (void)
{
  palette_set_foreground (0, 0, 0);
  palette_set_background (255, 255, 255);
}


void
palette_swap_colors (void)
{
  unsigned char fg_r, fg_g, fg_b;
  unsigned char bg_r, bg_g, bg_b;

  palette_get_foreground (&fg_r, &fg_g, &fg_b);
  palette_get_background (&bg_r, &bg_g, &bg_b);

  palette_set_foreground (bg_r, bg_g, bg_b);
  palette_set_background (fg_r, fg_g, fg_b);
}

void
palette_init_palettes (int no_data)
{
  if(!no_data)
    datafiles_read_directories (palette_path, palette_entries_load, 0);

}


void
palette_free_palettes (void)
{
  GSList *list;
  PaletteEntriesP entries;

  list = palette_entries_list;

  while (list)
    {
      entries = (PaletteEntriesP) list->data;

      /*  If the palette has been changed, save it, if possible  */
      if (entries->changed)
	/*  save the palette  */
	palette_entries_save (entries, entries->filename);

      palette_entries_free (entries);
      list = g_slist_next (list);
    }
  g_slist_free (palette_entries_list);

  if (palette)
    {
      palette->entries = NULL;
      palette->color = NULL;
      palette->color_select = NULL;
      palette->color_select_active = 0;
      palette->scroll_offset = 0;
    }
  num_palette_entries = 0;
  palette_entries_list = NULL;
}


/*****************************************/
/*         Local functions               */
/*****************************************/

static void
palette_create_palette_menu (PaletteP        palette,
			     PaletteEntriesP default_entries)
{
  GSList *list;
  PaletteEntriesP p_entries = NULL;
  PaletteEntriesP found_entries = NULL;
  int i = 0;
  int default_index = -1;

  gimp_option_menu_clear (palette->option_menu);

  list = palette_entries_list;
  while (list)
    {
      p_entries = (PaletteEntriesP) list->data;
      list = g_slist_next (list);

      /*  to make sure we get something!  */
      if (p_entries == NULL)
	{
	  found_entries = p_entries;
	  default_index = i;
	}

      if (p_entries == default_entries)
	{
	  found_entries = default_entries;
	  default_index = i;
	}

      gimp_option_menu_append (palette->option_menu, p_entries->name,
			       G_CALLBACK (palette_entries_set_callback),
			       (gpointer) p_entries);

      i++;
    }

  if (i == 0)
    {
      gimp_option_menu_append (palette->option_menu, "none", NULL, NULL);

      /*  Set the action area and option menus to insensitive  */
      gtk_widget_set_sensitive (palette->option_menu, FALSE);
      gtk_widget_set_sensitive (gimp_dialog_get_action_area (palette->shell), FALSE);
    }
  else
    {
      /*  Make sure the action area and option menus are sensitive  */
      gtk_widget_set_sensitive (palette->option_menu, TRUE);
      gtk_widget_set_sensitive (gimp_dialog_get_action_area (palette->shell), TRUE);
    }

  /*  Clear the color area  */
  gtk_widget_queue_draw (palette->color_area);

  /*  Set the current item of the option menu to reflect
   *  the default palette.  Need to refresh here too
   */
  if (default_index != -1)
    {
      gimp_option_menu_set_history (palette->option_menu, default_index);
      palette_entries_set_callback (NULL, found_entries);
    }
}

/*  Strips the line end, "\n" or "\r\n", off str.  */
static void
palette_chomp (char *str)
{
  size_t len = strlen (str);

  while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r'))
    str[--len] = '\0';
}

static void
palette_entries_load (char *filename)
{
  PaletteEntriesP entries;
  char            str[512];
  char           *tok;
  FILE           *fp;
  int             r, g, b;

  r = g = b = 0;

  entries = (PaletteEntriesP) g_malloc (sizeof (_PaletteEntries));

  entries->filename = g_strdup (filename);
  entries->name = g_strdup (prune_filename (filename));
  entries->colors = NULL;
  entries->n_colors = 0;

  /*  Open the requested file  */

  if (!(fp = g_fopen (filename, "rb")))
    {
      palette_entries_free (entries);
      return;
    }

  if (!fgets (str, sizeof (str), fp))
    str[0] = '\0';
  palette_chomp (str);
  if (strcmp (str, "GIMP Palette"))
    {
      fclose (fp);
      palette_entries_free (entries);
      return;
    }

  while (!feof (fp))
    {
      if (!fgets (str, 512, fp))
	continue;

      palette_chomp (str);

      if (str[0] != '#' && str[0] != '\0')
	{
	  tok = strtok (str, " \t");
	  if (tok)
	    r = atoi (tok);

	  tok = strtok (NULL, " \t");
	  if (tok)
	    g = atoi (tok);

	  tok = strtok (NULL, " \t");
	  if (tok)
	    b = atoi (tok);

	  tok = strtok (NULL, "");

	  palette_add_entry (entries, tok, r, g, b);
	} /* if */
    } /* while */

  /*  Clean up  */

  fclose (fp);
  entries->changed = 0;

  palette_entries_list = palette_entries_insert_list(palette_entries_list, entries);

  /* Check if the current palette is the default one */
  if (default_palette && strcmp(default_palette, prune_filename(filename)) == 0)
    default_palette_entries = entries;
}

static void
palette_entries_delete (char *filename)
{
  if (filename)
    g_unlink (filename);
}

static GSList *
palette_entries_insert_list (GSList *        list,
			     PaletteEntriesP entries)
{
  /*  add it to the list  */
  num_palette_entries++;
  return g_slist_append (list, (void *) entries);
}

static void
palette_entries_save (PaletteEntriesP  palette,
		      char            *filename)
{
  FILE * fp;
  GSList * list;
  PaletteEntryP entry;

  if (! filename)
    return;

  /*  Open the requested file  */
  if (! (fp = g_fopen (filename, "wb")))
    {
      g_message ("can't save palette \"%s\"\n", filename);
      return;
    }

  fprintf (fp, "GIMP Palette\n");
  fprintf (fp, "# %s -- GIMP Palette file\n", palette->name);

  list = palette->colors;
  while (list)
    {
      entry = (PaletteEntryP) list->data;
      fprintf (fp, "%d %d %d\t%s\n", entry->color[0], entry->color[1],
	       entry->color[2], entry->name);
      list = g_slist_next (list);
    }

  /*  Clean up  */
  fclose (fp);
}

static void
palette_entries_free (PaletteEntriesP entries)
{
  PaletteEntryP entry;
  GSList * list;

  list = entries->colors;
  while (list)
    {
      entry = (PaletteEntryP) list->data;
      palette_entry_free (entry);
      list = list->next;
    }
  g_slist_free (entries->colors);

  g_free (entries->name);
  if (entries->filename)
    g_free (entries->filename);
  g_free (entries);
}

static void
palette_entry_free (PaletteEntryP entry)
{
  if (entry->name)
    g_free (entry->name);

  g_free (entry);
}

static void
palette_entries_set_callback (GtkWidget *w,
			      gpointer   client_data)
{
  PaletteEntriesP pal;

  if (palette)
    {
      pal = (PaletteEntriesP) client_data;

      palette->entries = pal;
      palette->color = NULL;
      if (palette->color_select_active)
	{
	  palette->color_select_active = 0;
	  color_select_hide (palette->color_select);
	}
      palette->color_select = NULL;
      palette->scroll_offset = 0;

      palette_calc_scrollbar (palette);
      palette_draw_entries (palette);
      palette_draw_current_entry (palette);
    }
}

static void
palette_change_color (int r,
		      int g,
		      int b,
		      int state)
{
  if (palette && palette->entries)
    {
      switch (state)
	{
	case COLOR_NEW:
	  palette->color = palette_add_entry (palette->entries, "Untitled", r, g, b);

	  palette_calc_scrollbar (palette);
	  palette_draw_entries (palette);
	  palette_draw_current_entry (palette);
	  break;

	case COLOR_UPDATE_NEW:
	  if (palette->color)
	    {
	      palette->color->color[0] = r;
	      palette->color->color[1] = g;
	      palette->color->color[2] = b;
	    }
	  palette_draw_entries (palette);
	  palette_draw_current_entry (palette);
	  break;

	default:
	  break;
	}
    }

  if (active_color == FOREGROUND)
    palette_set_foreground (r, g, b);
  else if (active_color == BACKGROUND)
    palette_set_background (r, g, b);
}

void
palette_set_active_color (int r,
			  int g,
			  int b,
			  int state)
{
  palette_change_color (r, g, b, state);
}

/*  Paints the rendered entries, then outlines the current one with an
 *  inverting rectangle.
 */
static void
palette_color_area_draw (GtkDrawingArea *area,
			 cairo_t        *cr,
			 int             width,
			 int             height,
			 gpointer        data)
{
  PaletteP palette = (PaletteP) data;
  PaletteEntryP entry;
  int entry_width;
  int entry_height;
  int row, col;
  int x, y;

  if (!palette || !palette->entries)
    return;

  cairo_set_source_surface (cr, palette->surface, 0, 0);
  cairo_paint (cr);

  if (palette->updating || !palette->color)
    return;

  entry = palette->color;

  row = entry->position / COLUMNS;
  col = entry->position % COLUMNS;

  entry_width = (PREVIEW_WIDTH - (SPACING * (COLUMNS + 1))) / COLUMNS;
  entry_height = (PREVIEW_HEIGHT - (SPACING * (ROWS + 1))) / ROWS;

  x = col * (entry_width + SPACING);
  y = row * (entry_height + SPACING);
  y -= palette->scroll_offset;

  cairo_set_operator (cr, CAIRO_OPERATOR_DIFFERENCE);
  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_set_line_width (cr, 1.0);
  cairo_rectangle (cr, x + 0.5, y + 0.5,
		   entry_width + SPACING, entry_height + SPACING);
  cairo_stroke (cr);
}

static void
palette_color_area_pressed (GtkGestureClick *gesture,
			    gint             n_press,
			    gdouble          x,
			    gdouble          y,
			    gpointer         data)
{
  PaletteP palette = (PaletteP) data;
  GSList *tmp_link;
  int r, g, b;
  int width, height;
  int entry_width;
  int entry_height;
  int row, col;
  int pos;

  width = PREVIEW_WIDTH;
  height = PREVIEW_HEIGHT;
  entry_width = ((width - (SPACING * (COLUMNS + 1))) / COLUMNS) + SPACING;
  entry_height = ((height - (SPACING * (ROWS + 1))) / ROWS) + SPACING;

  col = (x - 1) / entry_width;
  row = (palette->scroll_offset + y - 1) / entry_height;
  pos = row * COLUMNS + col;

  if (gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture)) == 1 &&
      palette->entries && col >= 0 && col < COLUMNS && pos >= 0)
    {
      tmp_link = g_slist_nth (palette->entries->colors, pos);
      if (tmp_link)
	{
	  palette->color = tmp_link->data;

	  /*  Update either foreground or background colors  */
	  r = palette->color->color[0];
	  g = palette->color->color[1];
	  b = palette->color->color[2];
	  if (active_color == FOREGROUND)
	    palette_set_foreground (r, g, b);
	  else if (active_color == BACKGROUND)
	    palette_set_background (r, g, b);

	  palette_update_current_entry (palette);
	}
    }
}

static void
palette_scroll_update (GtkAdjustment *adjustment,
		       gpointer       data)
{
  PaletteP palette;

  palette = (PaletteP) data;

  if (palette)
    {
      palette->scroll_offset = gtk_adjustment_get_value (adjustment);
      palette_draw_entries (palette);
      palette_draw_current_entry (palette);
    }
}

static void
palette_new_callback (GtkWidget *w,
		      gpointer   client_data)
{
  PaletteP palette;

  palette = client_data;
  if (palette && palette->entries)
    {
      if (active_color == FOREGROUND)
	palette->color =
	  palette_add_entry (palette->entries, "Untitled",
			     foreground[0], foreground[1], foreground[2]);
      else if (active_color == BACKGROUND)
	palette->color =
	  palette_add_entry (palette->entries, "Untitled",
			     background[0], background[1], background[2]);

      palette_calc_scrollbar (palette);
      palette_draw_entries (palette);
      palette_draw_current_entry (palette);
    }
}

static void
palette_delete_callback (GtkWidget *w,
			 gpointer   client_data)
{
  PaletteP palette;

  palette = client_data;
  if (palette)
    palette_delete_entry (palette);
}


static void
palette_refresh_callback (GtkWidget *w,
			  gpointer client_data)
{
  PaletteP palette;

  palette = client_data;
  if(palette)
    {
      palette_free_palettes ();
      palette_init_palettes(FALSE);
      palette_create_palette_menu (palette, default_palette_entries);
      palette_calc_scrollbar (palette);
      palette_draw_entries (palette);
      palette_draw_current_entry (palette);
    }
  else
    {
      palette_free_palettes ();
      palette_init_palettes(FALSE);
    }

}



static void
palette_edit_callback (GtkWidget *w,
		       gpointer   client_data)
{
  PaletteP palette;
  unsigned char *color;

  palette = client_data;
  if (palette && palette->entries && palette->color)
    {
      color = palette->color->color;

      if (!palette->color_select)
	{
	  palette->color_select = color_select_new (color[0], color[1], color[2],
						    palette_select_callback, NULL,
						    FALSE);
	  palette->color_select_active = 1;
	}
      else
	{
	  if (!palette->color_select_active)
	    {
	      color_select_show (palette->color_select);
	      palette->color_select_active = 1;
	    }

	  color_select_set_color (palette->color_select, color[0], color[1], color[2], 1);
	}
    }
}

static gboolean
palette_dialog_delete_callback (GtkWindow *w,
				gpointer   client_data)
{
  palette_close_callback (GTK_WIDGET (w), client_data);

  return TRUE;
}


static void
palette_close_callback (GtkWidget *w,
			gpointer   client_data)
{
  PaletteP palette;

  palette = client_data;
  if (palette)
    {
      if (palette->color_select_active)
	{
	  palette->color_select_active = 0;
	  color_select_hide (palette->color_select);
	}

      if (gtk_widget_get_visible (palette->shell))
	gtk_widget_set_visible (palette->shell, FALSE);
    }
}

static void
palette_new_entries_callback (GtkWidget *w,
			      gpointer   client_data)
{
  query_string_box ("New Palette", "Enter a name for new palette", NULL,
		    palette_add_entries_callback, NULL);
}

/*  The first directory of a search path, with "~" expanded, or NULL.  */
static char *
palette_first_path (const char *search_path)
{
  char **tokens;
  char *path = NULL;

  if (!search_path)
    return NULL;

  tokens = g_strsplit (search_path, G_SEARCHPATH_SEPARATOR_S, 2);

  if (tokens[0] && tokens[0][0])
    {
      if (tokens[0][0] == '~')
	path = g_build_filename (g_get_home_dir (), tokens[0] + 1, NULL);
      else
	path = g_strdup (tokens[0]);
    }

  g_strfreev (tokens);

  return path;
}

static void
palette_add_entries_callback (GtkWidget *w,
			      gpointer   client_data,
			      gpointer   call_data)
{
  char *palette_name;
  char *path;
  PaletteEntriesP entries;

  palette_name = (char *) call_data;
  if (palette && palette_name)
    {
      entries = g_malloc (sizeof (_PaletteEntries));
      entries->filename = NULL;

      path = palette_first_path (palette_path);
      if (path)
	{
	  entries->filename = g_build_filename (path, palette_name, NULL);
	  g_free (path);
	}

      entries->name = palette_name;  /*  don't need to copy because this memory is ours  */
      entries->colors = NULL;
      entries->n_colors = 0;
      entries->changed = 1;

      palette_entries_list = palette_entries_insert_list (palette_entries_list, entries);

      palette_create_palette_menu (palette, entries);
    }
  else
    g_free (palette_name);
}

/* static void
palette_merge_entries_callback (GtkWidget *w,
				gpointer   client_data)
{
} */

static void
palette_delete_entries_callback (GtkWidget *w,
				 gpointer   client_data)
{
  PaletteP palette;
  PaletteEntriesP entries;

  palette = client_data;
  if (palette && palette->entries)
    {
      /*  If a color selection dialog is up, hide it  */
      if (palette->color_select_active)
	{
	  palette->color_select_active = 0;
	  color_select_hide (palette->color_select);
	}

      entries = palette->entries;
      if (entries && entries->filename)
	palette_entries_delete (entries->filename);

      if (default_palette_entries == entries)
	default_palette_entries = NULL;
      palette->entries = NULL;

      palette_free_palettes ();   /*  free palettes, don't save any modified versions  */
      palette_init_palettes (FALSE);   /*  load in brand new palettes  */

      palette_create_palette_menu (palette, default_palette_entries);
    }
}

static void
palette_select_callback (int   r,
			 int   g,
			 int   b,
			 ColorSelectState state,
			 void *client_data)
{
  unsigned char * color;

  if (palette && palette->entries)
    {
      switch (state) {
      case COLOR_SELECT_UPDATE:
	break;
      case COLOR_SELECT_OK:
	if (palette->color)
	  {
	  color = palette->color->color;

	  color[0] = r;
	  color[1] = g;
	  color[2] = b;
	  palette->entries->changed = 1;

	  /*  Update either foreground or background colors  */
	  if (active_color == FOREGROUND)
	    palette_set_foreground (r, g, b);
	  else if (active_color == BACKGROUND)
	    palette_set_background (r, g, b);

	  palette_calc_scrollbar (palette);
	  palette_draw_entries (palette);
	  palette_draw_current_entry (palette);
	  }
	/* Fallthrough */
      case COLOR_SELECT_CANCEL:
        color_select_hide (palette->color_select);
	palette->color_select_active = 0;
      }
    }
}

/*  Copies width RGB pixels from buffer into row y of the surface.  */
static void
palette_surface_draw_row (cairo_surface_t *surface,
			  unsigned char   *buffer,
			  int              y,
			  int              width)
{
  unsigned char *data;
  guint32 *dest;
  int stride;
  int i;

  if (y < 0 || y >= cairo_image_surface_get_height (surface))
    return;

  data = cairo_image_surface_get_data (surface);
  stride = cairo_image_surface_get_stride (surface);
  dest = (guint32 *) (data + y * stride);

  for (i = 0; i < width; i++, buffer += 3)
    dest[i] = ((guint32) buffer[0] << 16) | ((guint32) buffer[1] << 8) | buffer[2];
}

static int
palette_draw_color_row (unsigned char  **colors,
			int              ncolors,
			int              y,
			unsigned char   *buffer,
			cairo_surface_t *surface)
{
  unsigned char *p;
  unsigned char bcolor;
  int width, height;
  int entry_width;
  int entry_height;
  int vsize;
  int vspacing;
  int i, j;

  bcolor = 0;

  width = PREVIEW_WIDTH;
  height = PREVIEW_HEIGHT;
  entry_width = (width - (SPACING * (COLUMNS + 1))) / COLUMNS;
  entry_height = (height - (SPACING * (ROWS + 1))) / ROWS;

  if ((y >= 0) && ((y + SPACING) < height))
    vspacing = SPACING;
  else if (y < 0)
    vspacing = SPACING + y;
  else
    vspacing = height - y;

  if (vspacing > 0)
    {
      if (y < 0)
	y += SPACING - vspacing;

      for (i = SPACING - vspacing; i < SPACING; i++, y++)
	{
	  p = buffer;
	  for (j = 0; j < width; j++)
	    {
	      *p++ = bcolor;
	      *p++ = bcolor;
	      *p++ = bcolor;
	    }

	  palette_surface_draw_row (surface, buffer, y, width);
	}

      if (y > SPACING)
	y += SPACING - vspacing;
    }
  else
    y += SPACING;

  vsize = (y >= 0) ? (entry_height) : (entry_height + y);

  if ((y >= 0) && ((y + entry_height) < height))
    vsize = entry_height;
  else if (y < 0)
    vsize = entry_height + y;
  else
    vsize = height - y;

  if (vsize > 0)
    {
      p = buffer;
      for (i = 0; i < ncolors; i++)
	{
	  for (j = 0; j < SPACING; j++)
	    {
	      *p++ = bcolor;
	      *p++ = bcolor;
	      *p++ = bcolor;
	    }

	  for (j = 0; j < entry_width; j++)
	    {
	      *p++ = colors[i][0];
	      *p++ = colors[i][1];
	      *p++ = colors[i][2];
	    }
	}

      for (i = 0; i < (COLUMNS - ncolors); i++)
	{
	  for (j = 0; j < (SPACING + entry_width); j++)
	    {
	      *p++ = 0;
	      *p++ = 0;
	      *p++ = 0;
	    }
	}

      for (j = 0; j < SPACING; j++)
	{
	  *p++ = bcolor;
	  *p++ = bcolor;
	  *p++ = bcolor;
	}

      if (y < 0)
	y += entry_height - vsize;
      for (i = 0; i < vsize; i++, y++)
	palette_surface_draw_row (surface, buffer, y, width);
      if (y > entry_height)
	y += entry_height - vsize;
    }
  else
    y += entry_height;

  return y;
}

static void
palette_draw_entries (PaletteP palette)
{
  PaletteEntryP entry;
  unsigned char *buffer;
  unsigned char *colors[COLUMNS];
  GSList *tmp_link;
  int width, height;
  int entry_height;
  int row_vsize;
  int index, y;

  if (palette && palette->entries && !palette->updating)
    {
      width = PREVIEW_WIDTH;
      height = PREVIEW_HEIGHT;
      entry_height = (height - (SPACING * (ROWS + 1))) / ROWS;

      cairo_surface_flush (palette->surface);

      buffer = g_malloc (width * 3);

      y = -palette->scroll_offset;
      row_vsize = SPACING + entry_height;
      tmp_link = palette->entries->colors;
      index = 0;

      while ((tmp_link) && (y < -row_vsize))
	{
	  tmp_link = tmp_link->next;

	  if (++index == COLUMNS)
	    {
	      index = 0;
	      y += row_vsize;
	    }
	}

      index = 0;
      while (tmp_link)
	{
	  entry = tmp_link->data;
	  tmp_link = tmp_link->next;

	  colors[index] = entry->color;
	  index++;

	  if (index == COLUMNS)
	    {
	      index = 0;
	      y = palette_draw_color_row (colors, COLUMNS, y, buffer, palette->surface);
	      if (y >= height)
		break;
	    }
	}

      while (y < height)
	{
	  y = palette_draw_color_row (colors, index, y, buffer, palette->surface);
	  index = 0;
	}

      cairo_surface_mark_dirty (palette->surface);
      gtk_widget_queue_draw (palette->color_area);

      g_free (buffer);
    }
}

/*  The current entry is outlined by the draw function; this only asks
 *  for a redraw.
 */
static void
palette_draw_current_entry (PaletteP palette)
{
  if (palette && palette->color_area)
    gtk_widget_queue_draw (palette->color_area);
}

static void
palette_update_current_entry (PaletteP palette)
{
  if (palette && palette->entries)
    {
      /*  Draw the current entry  */
      palette_draw_current_entry (palette);

      /*  Update the active color name  */
      if (palette->color)
	gtk_editable_set_text (GTK_EDITABLE (palette->color_name),
			       palette->color->name);
    }
}

static PaletteEntryP
palette_add_entry (PaletteEntriesP  entries,
		   char            *name,
		   int              r,
		   int              g,
		   int              b)
{
  PaletteEntryP entry;

  if (entries)
    {
      entry = g_malloc (sizeof (_PaletteEntry));

      entry->color[0] = r;
      entry->color[1] = g;
      entry->color[2] = b;
      if (name)
	entry->name = g_strdup (name);
      else
	entry->name = g_strdup ("Untitled");
      entry->position = entries->n_colors;

      entries->colors = g_slist_append (entries->colors, entry);
      entries->n_colors += 1;

      entries->changed = 1;

      return entry;
    }

  return NULL;
}

static void
palette_delete_entry (PaletteP palette)
{
  PaletteEntryP entry;
  GSList *tmp_link;
  int pos;

  if (palette && palette->entries && palette->color)
    {
      entry = palette->color;
      palette->entries->colors = g_slist_remove (palette->entries->colors, entry);
      palette->entries->n_colors--;
      palette->entries->changed = 1;

      pos = entry->position;
      palette_entry_free (entry);
      palette->color = NULL;

      tmp_link = g_slist_nth (palette->entries->colors, pos);

      if (tmp_link)
	{
	  palette->color = tmp_link->data;

	  while (tmp_link)
	    {
	      entry = tmp_link->data;
	      tmp_link = tmp_link->next;
	      entry->position = pos++;
	    }
	}
      else
	{
	  tmp_link = g_slist_nth (palette->entries->colors, pos - 1);
	  if (tmp_link)
	    palette->color = tmp_link->data;
	}

      if (palette->entries->n_colors == 0)
	palette->color = palette_add_entry (palette->entries, "Black", 0, 0, 0);

      palette_calc_scrollbar (palette);
      palette_draw_entries (palette);
      palette_draw_current_entry (palette);
    }
}

static void
palette_calc_scrollbar (PaletteP palette)
{
  int n_entries;
  int cur_entry_row;
  int nrows;
  int row_vsize;
  int vsize;
  int page_size;
  int new_offset;

  if (palette && palette->entries)
    {
      n_entries = palette->entries->n_colors;
      nrows = n_entries / COLUMNS;
      if (n_entries % COLUMNS)
	nrows += 1;
      row_vsize = SPACING + ((PREVIEW_HEIGHT - (SPACING * (ROWS + 1))) / ROWS);
      vsize = row_vsize * nrows;
      page_size = row_vsize * ROWS;

      if (palette->color)
	cur_entry_row = palette->color->position / COLUMNS;
      else
	cur_entry_row = 0;

      new_offset = cur_entry_row * row_vsize;

      /* scroll only if necessary */
      if (new_offset < palette->scroll_offset)
	{
	  palette->scroll_offset = new_offset;
	}
      else if (new_offset > palette->scroll_offset)
	{
	  /* only scroll the minimum amount to bring the current color into view */
	  if ((palette->scroll_offset + page_size - row_vsize) < new_offset)
	    palette->scroll_offset = new_offset - (page_size - row_vsize);
	}

      /* sanity check to make sure the scrollbar offset is valid */
      if (vsize > page_size)
	if (palette->scroll_offset > (vsize - page_size))
	  palette->scroll_offset = vsize - page_size;

      /*  configure () clamps the value; keep our offset as computed.  */
      new_offset = palette->scroll_offset;
      g_signal_handlers_block_by_func (palette->sbar_data,
				       palette_scroll_update, palette);
      gtk_adjustment_configure (palette->sbar_data,
				palette->scroll_offset,
				0, vsize,
				row_vsize, page_size,
				(page_size < vsize) ? page_size : vsize);
      g_signal_handlers_unblock_by_func (palette->sbar_data,
					 palette_scroll_update, palette);
      palette->scroll_offset = new_offset;
    }
}

/*  Procedural database entries  */

/****************************/
/*  PALETTE_GET_FOREGROUND  */


static Argument *
palette_refresh_invoker (Argument *args)
{
  /* FIXME: I've hardcoded success to be 1, because brushes_init() is a 
   *        void function right now.  It'd be nice if it returned a value at 
   *        some future date, so we could tell if things blew up when reparsing
   *        the list (for whatever reason). 
   *                       - Seth "Yes, this is a kludge" Burgess
   *                         <sjburges@ou.edu>
   *   -and shaemlessly stolen by Adrian Likins for use here...
   */
  
  int success = TRUE ;
  palette_free_palettes();
  palette_init_palettes(FALSE);

  return procedural_db_return_args (&palette_refresh_proc, success);
}


ProcRecord palette_refresh_proc =
{
  "gimp_palette_refresh",
  "Refreshes current palettes",
  "This procedure incorporates all palettes currently in the users palette path. ",
  "Adrian Likins <adrain@gimp.org>",
  "Adrian Likins",
  "1998",
  PDB_INTERNAL,

  /* input aarguments */
  0,
  NULL,

  /* Output arguments */
  0,
  NULL,
  
  /* Exec mehtos  */
  { { palette_refresh_invoker } },
};



static Argument *
palette_get_foreground_invoker (Argument *args)
{
  Argument *return_args;
  unsigned char r, g, b;
  unsigned char *col;

  palette_get_foreground (&r, &g, &b);
  col = (unsigned char *) g_malloc (3);
  col[RED_PIX] = r;
  col[GREEN_PIX] = g;
  col[BLUE_PIX] = b;

  return_args = procedural_db_return_args (&palette_get_foreground_proc, TRUE);
  return_args[1].value.pdb_pointer = col;

  return return_args;
}

/*  The procedure definition  */
ProcArg palette_get_foreground_args[] =
{
  { PDB_COLOR,
    "foreground",
    "the foreground color"
  }
};

ProcRecord palette_get_foreground_proc =
{
  "gimp_palette_get_foreground",
  "Get the current GIMP foreground color",
  "This procedure retrieves the current GIMP foreground color.  The foreground color is used in a variety of tools such as paint tools, blending, and bucket fill.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1995-1996",
  PDB_INTERNAL,

  /*  Input arguments  */
  0,
  NULL,

  /*  Output arguments  */
  1,
  palette_get_foreground_args,

  /*  Exec method  */
  { { palette_get_foreground_invoker } },
};


/****************************/
/*  PALETTE_GET_BACKGROUND  */

static Argument *
palette_get_background_invoker (Argument *args)
{
  Argument *return_args;
  unsigned char r, g, b;
  unsigned char *col;

  palette_get_background (&r, &g, &b);
  col = (unsigned char *) g_malloc (3);
  col[RED_PIX] = r;
  col[GREEN_PIX] = g;
  col[BLUE_PIX] = b;

  return_args = procedural_db_return_args (&palette_get_background_proc, TRUE);
  return_args[1].value.pdb_pointer = col;

  return return_args;
}

/*  The procedure definition  */
ProcArg palette_get_background_args[] =
{
  { PDB_COLOR,
    "background",
    "the background color"
  }
};

ProcRecord palette_get_background_proc =
{
  "gimp_palette_get_background",
  "Get the current GIMP background color",
  "This procedure retrieves the current GIMP background color.  The background color is used in a variety of tools such as blending, erasing (with non-apha images), and image filling.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1995-1996",
  PDB_INTERNAL,

  /*  Input arguments  */
  0,
  NULL,

  /*  Output arguments  */
  1,
  palette_get_background_args,

  /*  Exec method  */
  { { palette_get_background_invoker } },
};


/****************************/
/*  PALETTE_SET_FOREGROUND  */

static Argument *
palette_set_foreground_invoker (Argument *args)
{
  unsigned char *color;
  int success;

  success = TRUE;
  if (success)
    color = (unsigned char *) args[0].value.pdb_pointer;

  if (success)
    palette_set_foreground (color[RED_PIX], color[GREEN_PIX], color[BLUE_PIX]);

  return procedural_db_return_args (&palette_set_foreground_proc, success);
}

/*  The procedure definition  */
ProcArg palette_set_foreground_args[] =
{
  { PDB_COLOR,
    "foreground",
    "the foreground color"
  }
};

ProcRecord palette_set_foreground_proc =
{
  "gimp_palette_set_foreground",
  "Set the current GIMP foreground color",
  "This procedure sets the current GIMP foreground color.  After this is set, operations which use foreground such as paint tools, blending, and bucket fill will use the new value.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1995-1996",
  PDB_INTERNAL,

  /*  Input arguments  */
  1,
  palette_set_foreground_args,

  /*  Output arguments  */
  0,
  NULL,

  /*  Exec method  */
  { { palette_set_foreground_invoker } },
};


/****************************/
/*  PALETTE_SET_BACKGROUND  */

static Argument *
palette_set_background_invoker (Argument *args)
{
  unsigned char *color;
  int success;

  success = TRUE;
  if (success)
    color = (unsigned char *) args[0].value.pdb_pointer;

  if (success)
    palette_set_background (color[RED_PIX], color[GREEN_PIX], color[BLUE_PIX]);

  return procedural_db_return_args (&palette_set_background_proc, success);
}

/*  The procedure definition  */
ProcArg palette_set_background_args[] =
{
  { PDB_COLOR,
    "background",
    "the background color"
  }
};

ProcRecord palette_set_background_proc =
{
  "gimp_palette_set_background",
  "Set the current GIMP background color",
  "This procedure sets the current GIMP background color.  After this is set, operations which use background such as blending, filling images, clearing, and erasing (in non-alpha images) will use the new value.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1995-1996",
  PDB_INTERNAL,

  /*  Input arguments  */
  1,
  palette_set_background_args,

  /*  Output arguments  */
  0,
  NULL,

  /*  Exec method  */
  { { palette_set_background_invoker } },
};

/*******************************/
/*  PALETTE_SET_DEFAULT_COLORS */

static Argument *
palette_set_default_colors_invoker (Argument *args)
{
  int success;

  success = TRUE;
  if (success)
    palette_set_default_colors ();

  return procedural_db_return_args (&palette_set_background_proc, success);
}

/*  The procedure definition  */

ProcRecord palette_set_default_colors_proc =
{
  "gimp_palette_set_default_colors",
  "Set the current GIMP foreground and background colors to black and white",
  "This procedure sets the current GIMP foreground and background colors to their initial default values, black and white.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1995-1996",
  PDB_INTERNAL,

  /*  Input arguments  */
  0,
  NULL,

  /*  Output arguments  */
  0,
  NULL,

  /*  Exec method  */
  { { palette_set_default_colors_invoker } },
};

/************************/
/*  PALETTE_SWAP_COLORS */

static Argument *
palette_swap_colors_invoker (Argument *args)
{
  int success;

  success = TRUE;
  if (success)
    palette_swap_colors ();

  return procedural_db_return_args (&palette_swap_colors_proc, success);
}

/*  The procedure definition  */

ProcRecord palette_swap_colors_proc =
{
  "gimp_palette_swap_colors",
  "Swap the current GIMP foreground and background colors",
  "This procedure swaps the current GIMP foreground and background colors, so that the new foreground color becomes the old background color and vice versa.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1995-1996",
  PDB_INTERNAL,

  /*  Input arguments  */
  0,
  NULL,

  /*  Output arguments  */
  0,
  NULL,

  /*  Exec method  */
  { { palette_swap_colors_invoker } },
};
