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

/*  The text tool renders text with Pango and cairo.
 *
 *  Under X the text was drawn with a core font into a 1 bit pixmap
 *  (supersampled SUPERSAMPLE times when antialiasing), read back with
 *  gdk_image_get and turned into a mask.  Now the text is laid out
 *  with a PangoLayout and drawn into an A8 cairo image surface, with
 *  or without antialiasing; the surface's alpha values are the mask.
 *  The rest - cropping the mask, filling a layer with the foreground
 *  color through it, making it a floating selection or a new layer -
 *  is unchanged.
 *
 *  The dialog chooses the font with a GtkFontDialogButton (family and
 *  face); the size, its unit, the border and the antialiasing toggle
 *  are as before.  The X font browser's foundry/set width/spacing/
 *  registry/encoding menus have no counterpart any more.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pango/pangocairo.h>
#include "appenv.h"
#include "actionarea.h"
#include "buildmenu.h"
#include "drawable.h"
#include "edit_selection.h"
#include "errors.h"
#include "floating_sel.h"
#include "gimage_mask.h"
#include "gdisplay.h"
#include "general.h"
#include "global_edit.h"
#include "interface.h"
#include "palette.h"
#include "procedural_db.h"
#include "selection.h"
#include "text_tool.h"
#include "tools.h"
#include "undo.h"

#include "tile_manager_pvt.h"
#include "drawable_pvt.h"

#define PIXELS 0
#define POINTS 1

/*  The XLFD names the old code asked for had a resolution of 75 dpi,
 *  so a point size became size * 75 / 72 pixels.  Keep that.
 */
#define POINTS_TO_PIXELS(s)  ((s) * 75.0 / 72.0)

/*  The largest font size used to preview the font in the text entry  */
#define PREVIEW_MAX_SIZE 48

#define DEFAULT_FONT "Sans"

typedef struct _TextTool TextTool;
struct _TextTool
{
  GtkWidget *shell;
  GtkWidget *main_vbox;
  GtkWidget *font_button;
  GtkWidget *size_menu;
  GtkWidget *size_text;
  GtkWidget *border_text;
  GtkWidget *the_text;
  GtkWidget *antialias_toggle;
  int click_x;
  int click_y;
  int size_type;
  int antialias;
  void *gdisp_ptr;
};

static void       text_button_press       (Tool *, GimpButtonEvent *, gpointer);
static void       text_button_release     (Tool *, GimpButtonEvent *, gpointer);
static void       text_motion             (Tool *, GimpMotionEvent *, gpointer);
static void       text_cursor_update      (Tool *, GimpMotionEvent *, gpointer);
static void       text_control            (Tool *, int, gpointer);

static void       text_create_dialog      (TextTool *);
static void       text_ok_callback        (GtkWidget *, gpointer);
static void       text_cancel_callback    (GtkWidget *, gpointer);
static gboolean   text_delete_callback    (GtkWidget *, gpointer);
static void       text_pixels_callback    (GtkWidget *, gpointer);
static void       text_points_callback    (GtkWidget *, gpointer);
static void       text_antialias_update   (GtkWidget *, gpointer);
static void       text_font_changed       (GObject *, GParamSpec *, gpointer);
static void       text_size_changed       (GtkWidget *, gpointer);
static void       text_update_preview     (TextTool *);

static PangoFontDescription * text_get_font_desc  (TextTool *);
static PangoFontDescription * text_font_desc_from_xlfd (double, int, char *,
							char *, char *,
							char *, char *);
static void       text_font_desc_set_size (PangoFontDescription *, double, int);
static PangoLayout * text_create_layout   (PangoFontDescription *, char *, int);
static void       text_layout_bounds      (PangoLayout *, int *, int *, int *, int *);

static void       text_init_render        (TextTool *);
static int        text_get_extents        (PangoFontDescription *, char *,
					   int *, int *, int *, int *);
static Layer *    text_render             (GImage *, GimpDrawable *, int, int,
					   PangoFontDescription *, char *, int, int);

static Argument * text_tool_invoker                  (Argument *);
static Argument * text_tool_invoker_ext              (Argument *);
static Argument * text_tool_get_extents_invoker      (Argument *);
static Argument * text_tool_get_extents_invoker_ext  (Argument *);

static ActionAreaItem action_items[] =
{
  { "OK", text_ok_callback, NULL, NULL },
  { "Cancel", text_cancel_callback, NULL, NULL },
};

static MenuItem size_metric_items[] =
{
  { "Pixels", 0, 0, text_pixels_callback, NULL, NULL, NULL, 0 },
  { "Points", 0, 0, text_points_callback, NULL, NULL, NULL, 0 },
  { NULL, 0, 0, NULL, NULL, NULL, NULL, 0 }
};

static TextTool *the_text_tool = NULL;

static void *text_options = NULL;

Tool*
tools_new_text ()
{
  Tool * tool;

  if (! text_options)
    text_options = tools_register_no_options (TEXT, "Text Tool Options");

  tool = g_malloc (sizeof (Tool));
  if (!the_text_tool)
    {
      the_text_tool = g_malloc (sizeof (TextTool));
      the_text_tool->shell = NULL;
      the_text_tool->main_vbox = NULL;
      the_text_tool->font_button = NULL;
      the_text_tool->size_menu = NULL;
      the_text_tool->size_text = NULL;
      the_text_tool->border_text = NULL;
      the_text_tool->the_text = NULL;
      the_text_tool->antialias_toggle = NULL;
      the_text_tool->click_x = 0;
      the_text_tool->click_y = 0;
      the_text_tool->size_type = PIXELS;
      the_text_tool->antialias = 1;
      the_text_tool->gdisp_ptr = NULL;
    }

  tool->type = TEXT;
  tool->state = INACTIVE;
  tool->scroll_lock = 1;  /* Do not allow scrolling */
  tool->auto_snap_to = TRUE;
  tool->private = (void *) the_text_tool;
  tool->button_press_func = text_button_press;
  tool->button_release_func = text_button_release;
  tool->motion_func = text_motion;
  tool->arrow_keys_func = standard_arrow_keys_func;
  tool->cursor_update_func = text_cursor_update;
  tool->control_func = text_control;
  tool->preserve = TRUE;

  return tool;
}

void
tools_free_text (Tool *tool)
{
}

static void
text_button_press (Tool            *tool,
		   GimpButtonEvent *bevent,
		   gpointer         gdisp_ptr)
{
  GDisplay *gdisp;
  Layer *layer;
  TextTool *text_tool;

  gdisp = gdisp_ptr;
  text_tool = tool->private;
  text_tool->gdisp_ptr = gdisp_ptr;

  tool->state = ACTIVE;
  tool->gdisp_ptr = gdisp_ptr;

  gdisplay_untransform_coords (gdisp, bevent->x, bevent->y,
			       &text_tool->click_x, &text_tool->click_y,
			       TRUE, 0);

  if ((layer = gimage_pick_correlate_layer (gdisp->gimage, text_tool->click_x, text_tool->click_y)))
    /*  If there is a floating selection, and this aint it, use the move tool  */
    if (layer_is_floating_sel (layer))
      {
	init_edit_selection (tool, gdisp_ptr, bevent, LayerTranslate);
	return;
      }

  if (!text_tool->shell)
    text_create_dialog (text_tool);

  switch (gimage_base_type (gdisp->gimage))
    {
    case RGB:
    case GRAY:
      if (!gtk_widget_get_visible (text_tool->antialias_toggle))
	{
	  gtk_widget_set_visible (text_tool->antialias_toggle, TRUE);
	  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (text_tool->antialias_toggle)))
	    text_tool->antialias = TRUE;
	  else
	    text_tool->antialias = FALSE;
	}
      break;
    case INDEXED:
      if (gtk_widget_get_visible (text_tool->antialias_toggle))
	{
	  gtk_widget_set_visible (text_tool->antialias_toggle, FALSE);
	  text_tool->antialias = FALSE;
	}
      break;
    }

  gtk_window_present (GTK_WINDOW (text_tool->shell));
}

static void
text_button_release (Tool            *tool,
		     GimpButtonEvent *bevent,
		     gpointer         gdisp_ptr)
{
  tool->state = INACTIVE;
}

static void
text_motion (Tool            *tool,
	     GimpMotionEvent *mevent,
	     gpointer         gdisp_ptr)
{
}

static void
text_cursor_update (Tool            *tool,
		    GimpMotionEvent *mevent,
		    gpointer         gdisp_ptr)
{
  GDisplay *gdisp;
  Layer *layer;
  int x, y;

  gdisp = (GDisplay *) gdisp_ptr;

  gdisplay_untransform_coords (gdisp, mevent->x, mevent->y, &x, &y, FALSE, FALSE);

  if ((layer = gimage_pick_correlate_layer (gdisp->gimage, x, y)))
    /*  if there is a floating selection, and this aint it...  */
    if (layer_is_floating_sel (layer))
      {
	gdisplay_install_tool_cursor (gdisp, GIMP_CURSOR_FLEUR);
	return;
      }

  gdisplay_install_tool_cursor (gdisp, GIMP_CURSOR_XTERM);
}

static void
text_control (Tool     *tool,
	      int       action,
	      gpointer  gdisp_ptr)
{
  switch (action)
    {
    case PAUSE :
      break;
    case RESUME :
      break;
    case HALT :
      if (the_text_tool->shell && gtk_widget_get_visible (the_text_tool->shell))
	gtk_widget_set_visible (the_text_tool->shell, FALSE);
      break;
    }
}

static void
text_create_dialog (TextTool *text_tool)
{
  GtkWidget *top_hbox;
  GtkWidget *right_vbox;
  GtkWidget *text_hbox;
  GtkWidget *font_label;
  GtkWidget *border_label;
  GtkWidget *border_hbox;
  GtkFontDialog *font_dialog;
  PangoFontDescription *desc;

  /* Create the shell and vertical & horizontal boxes */
  text_tool->shell = gimp_dialog_new ("Text Tool");
  gtk_window_set_resizable (GTK_WINDOW (text_tool->shell), TRUE);

  /* handle the wm close signal */
  g_signal_connect (text_tool->shell, "close-request",
		    G_CALLBACK (text_delete_callback),
		    text_tool);

  text_tool->main_vbox = gimp_vbox_new (FALSE, 2);
  gimp_container_set_border_width (text_tool->main_vbox, 2);
  gimp_box_pack_start (gimp_dialog_get_vbox (text_tool->shell),
		       text_tool->main_vbox, TRUE, TRUE, 0);
  top_hbox = gimp_hbox_new (FALSE, 4);
  gimp_box_pack_start (text_tool->main_vbox, top_hbox, TRUE, TRUE, 0);

  /* The font chooser: family and face; the size is set below */
  font_label = gtk_label_new ("Font");
  gtk_label_set_xalign (GTK_LABEL (font_label), 0.0);
  gtk_box_append (GTK_BOX (top_hbox), font_label);

  font_dialog = gtk_font_dialog_new ();
  gtk_font_dialog_set_title (font_dialog, "Text Tool Font");
  gtk_font_dialog_set_modal (font_dialog, TRUE);
  text_tool->font_button = gtk_font_dialog_button_new (font_dialog);
  gtk_font_dialog_button_set_level (GTK_FONT_DIALOG_BUTTON (text_tool->font_button),
				    GTK_FONT_LEVEL_FACE);
  gtk_font_dialog_button_set_use_font (GTK_FONT_DIALOG_BUTTON (text_tool->font_button),
				       TRUE);
  desc = pango_font_description_from_string (DEFAULT_FONT);
  gtk_font_dialog_button_set_font_desc (GTK_FONT_DIALOG_BUTTON (text_tool->font_button),
					desc);
  pango_font_description_free (desc);
  gimp_box_pack_start (top_hbox, text_tool->font_button, TRUE, TRUE, 0);
  g_signal_connect (text_tool->font_button, "notify::font-desc",
		    G_CALLBACK (text_font_changed),
		    text_tool);

  /* Create the box to hold options  */
  right_vbox = gimp_vbox_new (FALSE, 2);
  gimp_box_pack_start (top_hbox, right_vbox, FALSE, FALSE, 2);

  /* Create the text hbox, size text, and fonts size metric option menu */
  text_hbox = gimp_hbox_new (FALSE, 2);
  gtk_box_append (GTK_BOX (right_vbox), text_hbox);
  text_tool->size_text = gtk_entry_new ();
  gtk_editable_set_width_chars (GTK_EDITABLE (text_tool->size_text), 6);
  gtk_editable_set_text (GTK_EDITABLE (text_tool->size_text), "50");
  g_signal_connect (text_tool->size_text, "changed",
		    G_CALLBACK (text_size_changed),
		    text_tool);
  gimp_box_pack_start (text_hbox, text_tool->size_text, TRUE, TRUE, 0);

  /* Create the size menu */
  size_metric_items[0].user_data = text_tool;
  size_metric_items[1].user_data = text_tool;
  text_tool->size_menu = build_menu (size_metric_items, NULL);
  menu_item_set_active (&size_metric_items[text_tool->size_type]);
  gtk_box_append (GTK_BOX (text_hbox), text_tool->size_menu);

  /* create the antialiasing toggle button  */
  text_tool->antialias_toggle = gtk_check_button_new_with_label ("Antialiasing");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (text_tool->antialias_toggle),
			       text_tool->antialias);
  gtk_box_append (GTK_BOX (right_vbox), text_tool->antialias_toggle);
  g_signal_connect (text_tool->antialias_toggle, "toggled",
		    G_CALLBACK (text_antialias_update),
		    text_tool);

  /* Create the border text hbox, border text, and label  */
  border_hbox = gimp_hbox_new (FALSE, 2);
  gtk_box_append (GTK_BOX (right_vbox), border_hbox);
  border_label = gtk_label_new ("Border");
  gtk_label_set_xalign (GTK_LABEL (border_label), 0.0);
  gtk_box_append (GTK_BOX (border_hbox), border_label);
  text_tool->border_text = gtk_entry_new ();
  gtk_editable_set_width_chars (GTK_EDITABLE (text_tool->border_text), 6);
  gtk_editable_set_text (GTK_EDITABLE (text_tool->border_text), "0");
  gimp_box_pack_start (border_hbox, text_tool->border_text, TRUE, TRUE, 0);

  /* create the text entry widget */
  text_tool->the_text = gtk_entry_new ();
  gimp_box_pack_start (text_tool->main_vbox, text_tool->the_text, FALSE, FALSE, 2);

  /* Create the action area */
  action_items[0].user_data = text_tool;
  action_items[1].user_data = text_tool;
  build_action_area (text_tool->shell, action_items, 2, 0);

  /* Post initialization */
  text_update_preview (text_tool);

  /* Show the shell */
  gtk_window_present (GTK_WINDOW (text_tool->shell));
}

static void
text_ok_callback (GtkWidget *w,
		  gpointer   client_data)
{
  TextTool * text_tool;

  text_tool = (TextTool *) client_data;

  if (gtk_widget_get_visible (text_tool->shell))
    gtk_widget_set_visible (text_tool->shell, FALSE);

  text_init_render (text_tool);
}

static gboolean
text_delete_callback (GtkWidget *w,
		      gpointer   client_data)
{
  text_cancel_callback (w, client_data);

  return TRUE;
}

static void
text_cancel_callback (GtkWidget *w,
		      gpointer   client_data)
{
  TextTool * text_tool;

  text_tool = (TextTool *) client_data;

  if (gtk_widget_get_visible (text_tool->shell))
    gtk_widget_set_visible (text_tool->shell, FALSE);
}

static void
text_font_changed (GObject    *object,
		   GParamSpec *pspec,
		   gpointer    data)
{
  text_update_preview ((TextTool *) data);
}

static void
text_size_changed (GtkWidget *w,
		   gpointer   data)
{
  text_update_preview ((TextTool *) data);
}

static void
text_pixels_callback (GtkWidget *w,
		      gpointer   client_data)
{
  TextTool *text_tool;

  text_tool = (TextTool *) client_data;
  text_tool->size_type = PIXELS;
  text_update_preview (text_tool);
}

static void
text_points_callback (GtkWidget *w,
		      gpointer   client_data)
{
  TextTool *text_tool;

  text_tool = (TextTool *) client_data;
  text_tool->size_type = POINTS;
  text_update_preview (text_tool);
}

static void
text_antialias_update (GtkWidget *w,
		       gpointer   data)
{
  TextTool *text_tool;

  text_tool = (TextTool *) data;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (w)))
    text_tool->antialias = TRUE;
  else
    text_tool->antialias = FALSE;
}

/*  Shows the text entry in the chosen font (as the X version did), at
 *  the chosen size up to PREVIEW_MAX_SIZE pixels.
 */
static void
text_update_preview (TextTool *text_tool)
{
  PangoFontDescription *desc;
  PangoAttrList *attrs;

  if (! text_tool->the_text)
    return;

  desc = text_get_font_desc (text_tool);
  if (! desc)
    return;

  if (pango_font_description_get_size (desc) > PREVIEW_MAX_SIZE * PANGO_SCALE)
    pango_font_description_set_absolute_size (desc, PREVIEW_MAX_SIZE * PANGO_SCALE);

  attrs = pango_attr_list_new ();
  pango_attr_list_insert (attrs, pango_attr_font_desc_new (desc));
  gtk_entry_set_attributes (GTK_ENTRY (text_tool->the_text), attrs);
  pango_attr_list_unref (attrs);

  pango_font_description_free (desc);
}

/*  The dialog's font: the chosen family and face at the size in the
 *  size entry.  NULL if the size is not a positive number.
 */
static PangoFontDescription *
text_get_font_desc (TextTool *text_tool)
{
  PangoFontDescription *chosen;
  PangoFontDescription *desc;
  double size;

  size = atof (gtk_editable_get_text (GTK_EDITABLE (text_tool->size_text)));
  if (size <= 0)
    return NULL;

  chosen = gtk_font_dialog_button_get_font_desc (GTK_FONT_DIALOG_BUTTON (text_tool->font_button));
  if (chosen)
    desc = pango_font_description_copy (chosen);
  else
    desc = pango_font_description_from_string (DEFAULT_FONT);

  text_font_desc_set_size (desc, size, text_tool->size_type);

  return desc;
}

static void
text_font_desc_set_size (PangoFontDescription *desc,
			 double                size,
			 int                   size_type)
{
  if (size_type == POINTS)
    size = POINTS_TO_PIXELS (size);

  pango_font_description_set_absolute_size (desc, size * PANGO_SCALE);
}

/*  Turns the XLFD-style font fields of the gimp_text PDB procedures
 *  into a Pango font description.  The mapping:
 *
 *  family     "*" or "" -> "Sans" ("Monospace" if spacing is "m" or
 *             "c").  The classic X families are given fallbacks that
 *             exist on every platform: "helvetica" -> "Helvetica,
 *             Arial, Sans", "times" -> "Times, Times New Roman,
 *             Serif", "courier" -> "Courier, Courier New, Monospace",
 *             "fixed" -> "Monospace", ...; any other family is used as
 *             it is, followed by "Sans".
 *  weight     "thin", "extralight"/"ultralight", "light", "book",
 *             "regular"/"normal"/"medium", "demibold"/"semibold",
 *             "bold", "extrabold"/"ultrabold", "black"/"heavy" -> the
 *             Pango weight of that name ("medium" is X's regular
 *             weight, so it maps to PANGO_WEIGHT_NORMAL); "*" or
 *             anything else -> normal.
 *  slant      "r" -> normal, "i" and "ri" -> italic, "o" and "ro" ->
 *             oblique; "*" -> normal.
 *  set_width  "normal", "condensed", "semicondensed", "narrow"
 *             (condensed), "expanded", "semiexpanded", ... -> the
 *             Pango stretch of that name; "*" -> normal.
 *  size       PIXELS: an absolute size of that many pixels.  POINTS:
 *             the old code asked for 75 dpi fonts, so size * 75 / 72
 *             pixels.
 *
 *  foundry, registry and encoding have no Pango counterpart and are
 *  ignored: Pango picks the face and renders Unicode text.
 *
 *  Returns NULL when size is not positive.
 */

typedef struct
{
  const char *name;
  int         value;
} TextNameMap;

static const TextNameMap text_weights[] =
{
  { "thin",        PANGO_WEIGHT_THIN },
  { "extralight",  PANGO_WEIGHT_ULTRALIGHT },
  { "ultralight",  PANGO_WEIGHT_ULTRALIGHT },
  { "light",       PANGO_WEIGHT_LIGHT },
  { "book",        PANGO_WEIGHT_BOOK },
  { "regular",     PANGO_WEIGHT_NORMAL },
  { "normal",      PANGO_WEIGHT_NORMAL },
  { "medium",      PANGO_WEIGHT_NORMAL },
  { "demibold",    PANGO_WEIGHT_SEMIBOLD },
  { "semibold",    PANGO_WEIGHT_SEMIBOLD },
  { "demi",        PANGO_WEIGHT_SEMIBOLD },
  { "bold",        PANGO_WEIGHT_BOLD },
  { "extrabold",   PANGO_WEIGHT_ULTRABOLD },
  { "ultrabold",   PANGO_WEIGHT_ULTRABOLD },
  { "black",       PANGO_WEIGHT_HEAVY },
  { "heavy",       PANGO_WEIGHT_HEAVY },
  { NULL, 0 }
};

static const TextNameMap text_stretches[] =
{
  { "ultracondensed", PANGO_STRETCH_ULTRA_CONDENSED },
  { "extracondensed", PANGO_STRETCH_EXTRA_CONDENSED },
  { "condensed",      PANGO_STRETCH_CONDENSED },
  { "narrow",         PANGO_STRETCH_CONDENSED },
  { "semicondensed",  PANGO_STRETCH_SEMI_CONDENSED },
  { "normal",         PANGO_STRETCH_NORMAL },
  { "semiexpanded",   PANGO_STRETCH_SEMI_EXPANDED },
  { "expanded",       PANGO_STRETCH_EXPANDED },
  { "wide",           PANGO_STRETCH_EXPANDED },
  { "extraexpanded",  PANGO_STRETCH_EXTRA_EXPANDED },
  { "ultraexpanded",  PANGO_STRETCH_ULTRA_EXPANDED },
  { NULL, 0 }
};

static const char *text_family_fallbacks[][2] =
{
  { "helvetica",              "Helvetica, Arial, Sans" },
  { "arial",                  "Arial, Sans" },
  { "lucida",                 "Lucida, Lucida Sans, Sans" },
  { "lucidabright",           "Lucida Bright, Serif" },
  { "lucidatypewriter",       "Lucida Typewriter, Lucida Console, Monospace" },
  { "times",                  "Times, Times New Roman, Serif" },
  { "new century schoolbook", "New Century Schoolbook, Century Schoolbook, Serif" },
  { "charter",                "Charter, Serif" },
  { "utopia",                 "Utopia, Serif" },
  { "courier",                "Courier, Courier New, Monospace" },
  { "fixed",                  "Monospace" },
  { "clean",                  "Monospace" },
  { "terminal",               "Monospace" },
  { "symbol",                 "Symbol" },
  { NULL, NULL }
};

/*  Is field unset: NULL, empty, "*" or the "(nil)" of the old menus?  */
static int
text_field_is_any (const char *field)
{
  return (field == NULL || *field == '\0' ||
	  strcmp (field, "*") == 0 || strcmp (field, "(nil)") == 0);
}

/*  Looks field up in map, ignoring case, spaces and dashes.  */
static int
text_field_lookup (const TextNameMap *map,
		   const char        *field,
		   int                default_value)
{
  char *key;
  int i, j;

  if (text_field_is_any (field))
    return default_value;

  key = g_malloc (strlen (field) + 1);
  for (i = 0, j = 0; field[i]; i++)
    if (field[i] != ' ' && field[i] != '-' && field[i] != '_')
      key[j++] = g_ascii_tolower (field[i]);
  key[j] = '\0';

  for (i = 0; map[i].name; i++)
    if (strcmp (key, map[i].name) == 0)
      {
	g_free (key);
	return map[i].value;
      }

  g_free (key);
  return default_value;
}

static PangoFontDescription *
text_font_desc_from_xlfd (double  size,
			  int     size_type,
			  char   *family,
			  char   *weight,
			  char   *slant,
			  char   *set_width,
			  char   *spacing)
{
  PangoFontDescription *desc;
  char *families;
  int i;

  if (size <= 0)
    return NULL;

  desc = pango_font_description_new ();

  /*  family  */
  if (text_field_is_any (family))
    {
      if (! text_field_is_any (spacing) &&
	  (g_ascii_strcasecmp (spacing, "m") == 0 ||
	   g_ascii_strcasecmp (spacing, "c") == 0))
	families = g_strdup ("Monospace");
      else
	families = g_strdup (DEFAULT_FONT);
    }
  else
    {
      families = NULL;
      for (i = 0; text_family_fallbacks[i][0]; i++)
	if (g_ascii_strcasecmp (family, text_family_fallbacks[i][0]) == 0)
	  {
	    families = g_strdup (text_family_fallbacks[i][1]);
	    break;
	  }
      if (! families)
	families = g_strconcat (family, ", " DEFAULT_FONT, NULL);
    }
  pango_font_description_set_family (desc, families);
  g_free (families);

  /*  weight  */
  pango_font_description_set_weight (desc,
				     (PangoWeight) text_field_lookup (text_weights, weight,
								      PANGO_WEIGHT_NORMAL));

  /*  slant  */
  if (! text_field_is_any (slant) &&
      (g_ascii_strcasecmp (slant, "i") == 0 ||
       g_ascii_strcasecmp (slant, "ri") == 0))
    pango_font_description_set_style (desc, PANGO_STYLE_ITALIC);
  else if (! text_field_is_any (slant) &&
	   (g_ascii_strcasecmp (slant, "o") == 0 ||
	    g_ascii_strcasecmp (slant, "ro") == 0))
    pango_font_description_set_style (desc, PANGO_STYLE_OBLIQUE);
  else
    pango_font_description_set_style (desc, PANGO_STYLE_NORMAL);

  /*  set width  */
  pango_font_description_set_stretch (desc,
				      (PangoStretch) text_field_lookup (text_stretches, set_width,
									PANGO_STRETCH_NORMAL));

  /*  size  */
  text_font_desc_set_size (desc, size, size_type);

  return desc;
}

/*  A layout of text in desc, set up for rendering with or without
 *  antialiasing.  Lines are separated by '\n', as before.
 */
static PangoLayout *
text_create_layout (PangoFontDescription *desc,
		    char                 *text,
		    int                   antialias)
{
  PangoFontMap *fontmap;
  PangoContext *context;
  PangoLayout *layout;
  cairo_font_options_t *options;

  fontmap = pango_cairo_font_map_get_default ();
  context = pango_font_map_create_context (fontmap);

  options = cairo_font_options_create ();
  cairo_font_options_set_antialias (options,
				    antialias ? CAIRO_ANTIALIAS_GRAY
					      : CAIRO_ANTIALIAS_NONE);
  cairo_font_options_set_hint_metrics (options, CAIRO_HINT_METRICS_OFF);
  pango_cairo_context_set_font_options (context, options);
  cairo_font_options_destroy (options);

  layout = pango_layout_new (context);
  g_object_unref (context);

  pango_layout_set_font_description (layout, desc);
  pango_layout_set_text (layout, text ? text : "", -1);

  return layout;
}

/*  The box around both the ink and the logical extents of layout, in
 *  pixels, relative to the layout's origin (the top left corner of
 *  the first line).
 */
static void
text_layout_bounds (PangoLayout *layout,
		    int         *x,
		    int         *y,
		    int         *width,
		    int         *height)
{
  PangoRectangle ink, logical;
  int x1, y1, x2, y2;

  pango_layout_get_pixel_extents (layout, &ink, &logical);

  x1 = MIN (logical.x, 0);
  y1 = MIN (logical.y, 0);
  x2 = logical.x + logical.width;
  y2 = logical.y + logical.height;

  if (ink.width > 0 && ink.height > 0)
    {
      x1 = MIN (x1, ink.x);
      y1 = MIN (y1, ink.y);
      x2 = MAX (x2, ink.x + ink.width);
      y2 = MAX (y2, ink.y + ink.height);
    }

  *x = x1;
  *y = y1;
  *width = x2 - x1;
  *height = y2 - y1;
}

static void
text_init_render (TextTool *text_tool)
{
  GDisplay *gdisp;
  PangoFontDescription *desc;
  char *text;
  int border;

  desc = text_get_font_desc (text_tool);
  if (! desc)
    {
      g_message ("The font size must be a positive number.");
      return;
    }

  /* get the text */
  text = g_strdup (gtk_editable_get_text (GTK_EDITABLE (text_tool->the_text)));

  border = atoi (gtk_editable_get_text (GTK_EDITABLE (text_tool->border_text)));

  gdisp = (GDisplay *) text_tool->gdisp_ptr;

  text_render (gdisp->gimage, gimage_active_drawable (gdisp->gimage),
	       text_tool->click_x, text_tool->click_y,
	       desc, text, border, text_tool->antialias);

  gdisplays_flush ();

  g_free (text);
  pango_font_description_free (desc);
}

static Layer *
text_render (GImage *gimage,
	     GimpDrawable *drawable,
	     int     text_x,
	     int     text_y,
	     PangoFontDescription *desc,
	     char   *text,
	     int     border,
	     int     antialias)
{
  PangoLayout *layout;
  cairo_surface_t *surface;
  cairo_t *cr;
  Layer *layer;
  TileManager *mask, *newmask;
  PixelRegion textPR, maskPR;
  int layer_type;
  unsigned char color[MAX_CHANNELS];
  unsigned char *surface_data;
  unsigned char *s, *d;
  int stride;
  int crop;
  int text_width, text_height;
  int origin_x, origin_y;
  int row;
  void * pr;

  /*  determine the layer type  */
  if (drawable)
    layer_type = drawable_type_with_alpha (drawable);
  else
    layer_type = gimage_base_type_with_alpha (gimage);

  /* Dont crop the text if border is negative */
  crop = (border >= 0);
  if (!crop) border = 0;

  /* lay the text out and determine its bounding box */
  layout = text_create_layout (desc, text, antialias);
  text_layout_bounds (layout, &origin_x, &origin_y, &text_width, &text_height);

  if (text_width <= 0 || text_height <= 0 || ! text || ! *text)
    {
      g_object_unref (layout);
      return NULL;
    }

  /* render the text into an alpha-only surface: its values are the mask */
  surface = cairo_image_surface_create (CAIRO_FORMAT_A8, text_width, text_height);
  if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS)
    {
      g_message ("text_render: could not allocate image");
      cairo_surface_destroy (surface);
      g_object_unref (layout);
      return NULL;
    }

  cr = cairo_create (surface);
  cairo_set_antialias (cr, antialias ? CAIRO_ANTIALIAS_GRAY : CAIRO_ANTIALIAS_NONE);
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
  cairo_move_to (cr, -origin_x, -origin_y);
  pango_cairo_show_layout (cr, layout);
  cairo_destroy (cr);
  cairo_surface_flush (surface);

  g_object_unref (layout);

  surface_data = cairo_image_surface_get_data (surface);
  stride = cairo_image_surface_get_stride (surface);

  /* copy the surface into the mask */
  mask = tile_manager_new (text_width, text_height, 1);
  pixel_region_init (&maskPR, mask, 0, 0, text_width, text_height, TRUE);

  for (pr = pixel_regions_register (1, &maskPR); pr != NULL; pr = pixel_regions_process (pr))
    {
      s = surface_data + maskPR.y * stride + maskPR.x;
      d = maskPR.data;

      for (row = 0; row < maskPR.h; row++)
	{
	  memcpy (d, s, maskPR.w);
	  s += stride;
	  d += maskPR.rowstride;
	}
    }

  cairo_surface_destroy (surface);

  /*  Crop the mask buffer  */
  newmask = crop ? crop_buffer (mask, border) : mask;
  if (newmask != mask)
    tile_manager_destroy (mask);

  if (newmask &&
      (layer = layer_new (gimage->ID, newmask->levels[0].width,
			 newmask->levels[0].height, layer_type,
			 "Text Layer", OPAQUE_OPACITY, NORMAL_MODE)))
    {
      /*  color the layer buffer  */
      gimage_get_foreground (gimage, drawable, color);
      color[GIMP_DRAWABLE(layer)->bytes - 1] = OPAQUE_OPACITY;
      pixel_region_init (&textPR, GIMP_DRAWABLE(layer)->tiles, 0, 0, GIMP_DRAWABLE(layer)->width, GIMP_DRAWABLE(layer)->height, TRUE);
      color_region (&textPR, color);

      /*  apply the text mask  */
      pixel_region_init (&textPR, GIMP_DRAWABLE(layer)->tiles, 0, 0, GIMP_DRAWABLE(layer)->width, GIMP_DRAWABLE(layer)->height, TRUE);
      pixel_region_init (&maskPR, newmask, 0, 0, GIMP_DRAWABLE(layer)->width, GIMP_DRAWABLE(layer)->height, FALSE);
      apply_mask_to_region (&textPR, &maskPR, OPAQUE_OPACITY);

      /*  Start a group undo  */
      undo_push_group_start (gimage, EDIT_PASTE_UNDO);

      /*  Set the layer offsets: the text box's top left corner is at
       *  (text_x, text_y); ink may reach a little outside of it
       */
      GIMP_DRAWABLE(layer)->offset_x = text_x + (crop ? 0 : origin_x);
      GIMP_DRAWABLE(layer)->offset_y = text_y + (crop ? 0 : origin_y);

      /*  If there is a selection mask clear it--
       *  this might not always be desired, but in general,
       *  it seems like the correct behavior.
       */
      if (! gimage_mask_is_empty (gimage))
	channel_clear (gimage_get_mask (gimage));

      /*  If the drawable id is invalid, create a new layer  */
      if (drawable == NULL)
	gimage_add_layer (gimage, layer, -1);
      /*  Otherwise, instantiate the text as the new floating selection */
      else
	floating_sel_attach (layer, drawable);

      /*  end the group undo  */
      undo_push_group_end (gimage);

      tile_manager_destroy (newmask);
    }
  else
    {
      if (newmask)
	{
	  g_message ("text_render: could not allocate image");
          tile_manager_destroy (newmask);
	}
      layer = NULL;
    }

  return layer;
}


static int
text_get_extents (PangoFontDescription *desc,
		  char *text,
		  int  *width,
		  int  *height,
		  int  *ascent,
		  int  *descent)
{
  PangoLayout *layout;
  PangoContext *context;
  PangoFontMetrics *metrics;
  int x, y;

  if (! text || ! *text)
    return FALSE;

  layout = text_create_layout (desc, text, TRUE);

  /* determine the bounding box of the text */
  text_layout_bounds (layout, &x, &y, width, height);

  /* and the font's ascent and descent */
  context = pango_layout_get_context (layout);
  metrics = pango_context_get_metrics (context, desc, NULL);
  *ascent = PANGO_PIXELS_CEIL (pango_font_metrics_get_ascent (metrics));
  *descent = PANGO_PIXELS_CEIL (pango_font_metrics_get_descent (metrics));
  pango_font_metrics_unref (metrics);

  g_object_unref (layout);

  if (*width <= 0)
    return FALSE;
  else
    return TRUE;
}

/****************************************/
/*  The text_tool procedure definition  */
ProcArg text_tool_args[] =
{
  { PDB_IMAGE,
    "image",
    "The image"
  },
  { PDB_DRAWABLE,
    "drawable",
    "The affected drawable: (-1 for a new text layer)"
  },
  { PDB_FLOAT,
    "x",
    "the x coordinate for the left side of text bounding box"
  },
  { PDB_FLOAT,
    "y",
    "the y coordinate for the top of text bounding box"
  },
  { PDB_STRING,
    "text",
    "the text to generate"
  },
  { PDB_INT32,
    "border",
    "the size of the border: border >= 0"
  },
  { PDB_INT32,
    "antialias",
    "generate antialiased text"
  },
  { PDB_FLOAT,
    "size",
    "the size of text in either pixels or points"
  },
  { PDB_INT32,
    "size_type",
    "the units of the specified size: { PIXELS (0), POINTS (1) }"
  },
  { PDB_STRING,
    "foundry",
    "the font foundry, \"*\" for any"
  },
  { PDB_STRING,
    "family",
    "the font family, \"*\" for any"
  },
  { PDB_STRING,
    "weight",
    "the font weight, \"*\" for any"
  },
  { PDB_STRING,
    "slant",
    "the font slant, \"*\" for any"
  },
  { PDB_STRING,
    "set_width",
    "the font set-width parameter, \"*\" for any"
  },
  { PDB_STRING,
    "spacing",
    "the font spacing, \"*\" for any"
  }
};

ProcArg text_tool_out_args[] =
{
  { PDB_LAYER,
    "text_layer",
    "the new text layer"
  }
};

ProcRecord text_tool_proc =
{
  "gimp_text",
  "Add text at the specified location as a floating selection or a new layer.",
  "This tool requires font information in the form of seven parameters: {size, foundry, family, weight, slant, set_width, spacing}.  The font size can either be specified in units of pixels or points, and the appropriate metric is specified using the size_type "
  "argument.  The x and y parameters together control the placement of the new text by specifying the upper left corner of the text bounding box.  If the antialias parameter is non-zero, the generated text will blend more smoothly with underlying layers.  "
  "This option requires more time and memory to compute than non-antialiased text; the resulting floating selection or layer, however, will require the same amount of memory with or without antialiasing.  If the specified drawable parameter is valid, the "
  "text will be created as a floating selection attached to the drawable.  If the drawable parameter is not valid (-1), the text will appear as a new layer.  Finally, a border can be specified around the final rendered text.  The border is measured in pixels.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1995-1996",
  PDB_INTERNAL,

  /*  Input arguments  */
  15,
  text_tool_args,

  /*  Output arguments  */
  1,
  text_tool_out_args,

  /*  Exec method  */
  { { text_tool_invoker } },
};

/********************************************/
/*  The text_tool_ext procedure definition  */
ProcArg text_tool_args_ext[] =
{
  { PDB_IMAGE,
    "image",
    "The image"
  },
  { PDB_DRAWABLE,
    "drawable",
    "The affected drawable: (-1 for a new text layer)"
  },
  { PDB_FLOAT,
    "x",
    "the x coordinate for the left side of text bounding box"
  },
  { PDB_FLOAT,
    "y",
    "the y coordinate for the top of text bounding box"
  },
  { PDB_STRING,
    "text",
    "the text to generate"
  },
  { PDB_INT32,
    "border",
    "the size of the border: border >= 0"
  },
  { PDB_INT32,
    "antialias",
    "generate antialiased text"
  },
  { PDB_FLOAT,
    "size",
    "the size of text in either pixels or points"
  },
  { PDB_INT32,
    "size_type",
    "the units of the specified size: { PIXELS (0), POINTS (1) }"
  },
  { PDB_STRING,
    "foundry",
    "the font foundry, \"*\" for any"
  },
  { PDB_STRING,
    "family",
    "the font family, \"*\" for any"
  },
  { PDB_STRING,
    "weight",
    "the font weight, \"*\" for any"
  },
  { PDB_STRING,
    "slant",
    "the font slant, \"*\" for any"
  },
  { PDB_STRING,
    "set_width",
    "the font set-width parameter, \"*\" for any"
  },
  { PDB_STRING,
    "spacing",
    "the font spacing, \"*\" for any"
  },
  { PDB_STRING,
    "registry",
    "the font registry, \"*\" for any"
  },
  { PDB_STRING,
    "encoding",
    "the font encoding, \"*\" for any"
  }
};

ProcArg text_tool_out_args_ext[] =
{
  { PDB_LAYER,
    "text_layer",
    "the new text layer"
  }
};

ProcRecord text_tool_proc_ext =
{
  "gimp_text_ext",
  "Add text at the specified location as a floating selection or a new layer.",
  "This tool requires font information in the form of nine parameters: {size, foundry, family, weight, slant, set_width, spacing, registry, encoding}.  The font size can either be specified in units of pixels or points, and the appropriate metric is specified using the size_type "
  "argument.  The x and y parameters together control the placement of the new text by specifying the upper left corner of the text bounding box.  If the antialias parameter is non-zero, the generated text will blend more smoothly with underlying layers.  "
  "This option requires more time and memory to compute than non-antialiased text; the resulting floating selection or layer, however, will require the same amount of memory with or without antialiasing.  If the specified drawable parameter is valid, the "
  "text will be created as a floating selection attached to the drawable.  If the drawable parameter is not valid (-1), the text will appear as a new layer.  Finally, a border can be specified around the final rendered text.  The border is measured in pixels.",
  "Martin Edlman",
  "Spencer Kimball & Peter Mattis",
  "1998",
  PDB_INTERNAL,
    
  /*  Input arguments  */
  17,
  text_tool_args_ext,

  /*  Output arguments  */
  1,
  text_tool_out_args_ext,

  /*  Exec method  */
  { { text_tool_invoker_ext } },
};


/**********************/
/*  TEXT_GET_EXTENTS  */

ProcArg text_tool_get_extents_args[] =
{
  { PDB_STRING,
    "text",
    "the text to generate"
  },
  { PDB_FLOAT,
    "size",
    "the size of text in either pixels or points"
  },
  { PDB_INT32,
    "size_type",
    "the units of the specified size: { PIXELS (0), POINTS (1) }"
  },
  { PDB_STRING,
    "foundry",
    "the font foundry, \"*\" for any"
  },
  { PDB_STRING,
    "family",
    "the font family, \"*\" for any"
  },
  { PDB_STRING,
    "weight",
    "the font weight, \"*\" for any"
  },
  { PDB_STRING,
    "slant",
    "the font slant, \"*\" for any"
  },
  { PDB_STRING,
    "set_width",
    "the font set-width parameter, \"*\" for any"
  },
  { PDB_STRING,
    "spacing",
    "the font spacing, \"*\" for any"
  }
};

ProcArg text_tool_get_extents_out_args[] =
{
  { PDB_INT32,
    "width",
    "the width of the specified text"
  },
  { PDB_INT32,
    "height",
    "the height of the specified text"
  },
  { PDB_INT32,
    "ascent",
    "the ascent of the specified font"
  },
  { PDB_INT32,
    "descent",
    "the descent of the specified font"
  }
};

ProcRecord text_tool_get_extents_proc =
{
  "gimp_text_get_extents",
  "Get extents of the bounding box for the specified text",
  "This tool returns the width and height of a bounding box for the specified text string with the specified font information.  Ascent and descent for the specified font are returned as well.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1995-1996",
  PDB_INTERNAL,

  /*  Input arguments  */
  9,
  text_tool_get_extents_args,

  /*  Output arguments  */
  4,
  text_tool_get_extents_out_args,

  /*  Exec method  */
  { { text_tool_get_extents_invoker } },
};

/**************************/
/*  TEXT_GET_EXTENTS_EXT  */
ProcArg text_tool_get_extents_args_ext[] =
{
  { PDB_STRING,
    "text",
    "the text to generate"
  },
  { PDB_FLOAT,
    "size",
    "the size of text in either pixels or points"
  },
  { PDB_INT32,
    "size_type",
    "the units of the specified size: { PIXELS (0), POINTS (1) }"
  },
  { PDB_STRING,
    "foundry",
    "the font foundry, \"*\" for any"
  },
  { PDB_STRING,
    "family",
    "the font family, \"*\" for any"
  },
  { PDB_STRING,
    "weight",
    "the font weight, \"*\" for any"
  },
  { PDB_STRING,
    "slant",
    "the font slant, \"*\" for any"
  },
  { PDB_STRING,
    "set_width",
    "the font set-width parameter, \"*\" for any"
  },
  { PDB_STRING,
    "spacing",
    "the font spacing, \"*\" for any"
  },
  { PDB_STRING,
    "registry",
    "the font registry, \"*\" for any"
  },
  { PDB_STRING,
    "encoding",
    "the font encoding, \"*\" for any"
  }
};

ProcArg text_tool_get_extents_out_args_ext[] =
{
  { PDB_INT32,
    "width",
    "the width of the specified text"
  },
  { PDB_INT32,
    "height",
    "the height of the specified text"
  },
  { PDB_INT32,
    "ascent",
    "the ascent of the specified font"
  },
  { PDB_INT32,
    "descent",
    "the descent of the specified font"
  }
};

ProcRecord text_tool_get_extents_proc_ext =
{
  "gimp_text_get_extents_ext",
  "Get extents of the bounding box for the specified text",
  "This tool returns the width and height of a bounding box for the specified text string with the specified font information.  Ascent and descent for the specified font are returned as well.",
  "Martin Edlman",
  "Spencer Kimball & Peter Mattis",
  "1998",
  PDB_INTERNAL,

  /*  Input arguments  */
  11,
  text_tool_get_extents_args_ext,

  /*  Output arguments  */
  4,
  text_tool_get_extents_out_args_ext,

  /*  Exec method  */
  { { text_tool_get_extents_invoker_ext } },
};


static Argument *
text_tool_invoker (Argument *args)
{
  int i;
  Argument argv[17];

  for (i=0; i<15; i++)
    argv[i] = args[i];
  argv[15].arg_type = PDB_STRING;
  argv[15].value.pdb_pointer = (gpointer)"*";
  argv[16].arg_type = PDB_STRING;
  argv[16].value.pdb_pointer = (gpointer)"*";
  return text_tool_invoker_ext (argv);
}

static Argument *
text_tool_invoker_ext (Argument *args)
{
  int success = TRUE;
  GImage *gimage;
  Layer *text_layer;
  GimpDrawable *drawable;
  double x, y;
  char *text;
  int border;
  int antialias;
  double size;
  int size_type;
  char *family;
  char *weight;
  char *slant;
  char *set_width;
  char *spacing;
  int int_value;
  double fp_value;
  PangoFontDescription *desc;
  Argument *return_args;

  gimage      = NULL;
  text_layer  = NULL;
  drawable    = NULL;
  x           = 0;
  y           = 0;
  text        = NULL;
  border      = FALSE;
  antialias   = FALSE;
  size        = 0;
  size_type   = PIXELS;
  family      = NULL;
  weight      = NULL;
  slant       = NULL;
  set_width   = NULL;
  spacing     = NULL;
  desc        = NULL;

  /*  the gimage  */
  if (success)
    {
      int_value = args[0].value.pdb_int;
      if (! (gimage = gimage_get_ID (int_value)))
	success = FALSE;
    }
  /*  the drawable  */
  if (success)
    {
      int_value = args[1].value.pdb_int;
      drawable = drawable_get_ID (int_value);
      if (drawable && gimage != drawable_gimage (drawable))
	success = FALSE;
    }
  /*  x, y coordinates  */
  if (success)
    {
      x = args[2].value.pdb_float;
      y = args[3].value.pdb_float;
    }
  /*  text  */
  if (success)
    text = (char *) args[4].value.pdb_pointer;
  /*  border  */
  if (success)
    {
      int_value = args[5].value.pdb_int;
      if (int_value >= -1)
	border = int_value;
      else
	success = FALSE;
    }
  /*  antialias  */
  if (success)
    {
      int_value = args[6].value.pdb_int;
      antialias = (int_value) ? TRUE : FALSE;
    }
  /*  size  */
  if (success)
    {
      fp_value = args[7].value.pdb_float;
      if (fp_value > 0)
	size = fp_value;
      else
	success = FALSE;
    }
  /*  size type  */
  if (success)
    {
      int_value = args[8].value.pdb_int;
      switch (int_value)
	{
	case 0: size_type = PIXELS; break;
	case 1: size_type = POINTS; break;
	default: success = FALSE;
	}
    }
  /*  family, weight, slant, set_width, spacing (foundry, registry and
   *  encoding - args 9, 15 and 16 - do not matter to Pango)
   */
  if (success)
    {
      family = (char *) args[10].value.pdb_pointer;
      weight = (char *) args[11].value.pdb_pointer;
      slant = (char *) args[12].value.pdb_pointer;
      set_width = (char *) args[13].value.pdb_pointer;
      spacing = (char *) args[14].value.pdb_pointer;
    }

  /*  get a font description for the font  */
  if (success)
    success = ((desc = text_font_desc_from_xlfd (size, size_type, family, weight,
						 slant, set_width, spacing)) != NULL);

  /*  call the text render procedure  */
  if (success)
    success = ((text_layer = text_render (gimage, drawable, x, y, desc,
					  text, border, antialias)) != NULL);

  if (desc)
    pango_font_description_free (desc);

  return_args = procedural_db_return_args (&text_tool_proc, success);

  if (success)
    return_args[1].value.pdb_int = drawable_ID (GIMP_DRAWABLE(text_layer));

  return return_args;
}


static Argument *
text_tool_get_extents_invoker (Argument *args)
{
  int i;
  Argument argv[11];

  for (i=0; i<9; i++)
    argv[i] = args[i];
  argv[9].arg_type = PDB_STRING;
  argv[9].value.pdb_pointer = (gpointer)"*";
  argv[10].arg_type = PDB_STRING;
  argv[10].value.pdb_pointer = (gpointer)"*";
  return text_tool_get_extents_invoker_ext (argv);
}

static Argument *
text_tool_get_extents_invoker_ext (Argument *args)
{
  int success = TRUE;
  char *text;
  double size;
  int size_type;
  char *family;
  char *weight;
  char *slant;
  char *set_width;
  char *spacing;
  int width, height;
  int ascent, descent;
  int int_value;
  double fp_value;
  PangoFontDescription *desc;
  Argument *return_args;

  text = NULL;
  size = 0.0;
  size_type = PIXELS;
  family = weight = slant = set_width = spacing = NULL;
  width = height = ascent = descent = 0;
  desc = NULL;

  /*  text  */
  if (success)
    text = (char *) args[0].value.pdb_pointer;
  /*  size  */
  if (success)
    {
      fp_value = args[1].value.pdb_float;
      if (fp_value > 0)
	size = fp_value;
      else
	success = FALSE;
    }
  /*  size type  */
  if (success)
    {
      int_value = args[2].value.pdb_int;
      switch (int_value)
	{
	case 0: size_type = PIXELS; break;
	case 1: size_type = POINTS; break;
	default: success = FALSE;
	}
    }
  /*  family, weight, slant, set_width, spacing (foundry, registry and
   *  encoding - args 3, 9 and 10 - do not matter to Pango)
   */
  if (success)
    {
      family = (char *) args[4].value.pdb_pointer;
      weight = (char *) args[5].value.pdb_pointer;
      slant = (char *) args[6].value.pdb_pointer;
      set_width = (char *) args[7].value.pdb_pointer;
      spacing = (char *) args[8].value.pdb_pointer;
    }

  /*  get a font description for the font  */
  if (success)
    success = ((desc = text_font_desc_from_xlfd (size, size_type, family, weight,
						 slant, set_width, spacing)) != NULL);

  /*  measure the text  */
  if (success)
    success = text_get_extents (desc, text, &width, &height, &ascent, &descent);

  if (desc)
    pango_font_description_free (desc);

  return_args = procedural_db_return_args (&text_tool_get_extents_proc, success);

  if (success)
    {
      return_args[1].value.pdb_int = width;
      return_args[2].value.pdb_int = height;
      return_args[3].value.pdb_int = ascent;
      return_args[4].value.pdb_int = descent;
    }

  return return_args;
}
