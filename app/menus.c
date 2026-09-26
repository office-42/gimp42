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
#include "appenv.h"
#include "colormaps.h"
#include "commands.h"
#include "fileops.h"
#include "gimprc.h"
#include "interface.h"
#include "menus.h"
#include "paint_funcs.h"
#include "procedural_db.h"
#include "scale.h"
#include "tools.h"
#include "gdisplay.h"

static MenuEntry toolbox_entries[] =
{
  { "/File/New", "<control>N", (MenuCallback) file_new_cmd_callback, 0 },
  { "/File/Open", "<control>O", (MenuCallback) file_open_cmd_callback, 0 },
  { "/File/Open Recent/", NULL, NULL, 0 },
  { "/File/About...", NULL, (MenuCallback) about_dialog_cmd_callback, 0 },
  { "/File/Preferences...", NULL, (MenuCallback) file_pref_cmd_callback, 0 },
  { "/File/Tip of the day", NULL, (MenuCallback) tips_dialog_cmd_callback, 0 },
  { "/File/---", NULL, NULL, 0, "<Separator>" },
  { "/File/Dialogs/Brushes...", "<control><shift>B", (MenuCallback) dialogs_brushes_cmd_callback, 0 },
  { "/File/Dialogs/Patterns...", "<control><shift>P", (MenuCallback) dialogs_patterns_cmd_callback, 0 },
  { "/File/Dialogs/Palette...", "<control>P", (MenuCallback) dialogs_palette_cmd_callback, 0 },
  { "/File/Dialogs/Gradient Editor...", "<control>G", (MenuCallback) dialogs_gradient_editor_cmd_callback, 0 },
  { "/File/Dialogs/Tool Options...", "<control><shift>T", (MenuCallback) dialogs_tools_options_cmd_callback, 0 },
  { "/File/---", NULL, NULL, 0, "<Separator>" },
  { "/File/Quit", "<control>Q", (MenuCallback) file_quit_cmd_callback, 0 },
};
static int n_toolbox_entries = sizeof (toolbox_entries) / sizeof (toolbox_entries[0]);
 
static MenuEntry image_entries[] =
{
  { "/File/New", "<control>N", (MenuCallback) file_new_cmd_callback, 1 },
  { "/File/Open", "<control>O", (MenuCallback) file_open_cmd_callback, 0 },
  { "/File/Open Recent/", NULL, NULL, 0 },
  { "/File/Save", "<control>S", (MenuCallback) file_save_cmd_callback, 0 },
  { "/File/Save as", "<control><shift>S", (MenuCallback) file_save_as_cmd_callback, 0 },
  { "/File/Preferences...", NULL, (MenuCallback) file_pref_cmd_callback, 0 },
  { "/File/---", NULL, NULL, 0, "<Separator>" },
  
  
  { "/File/Close", "<control>W", (MenuCallback) file_close_cmd_callback, 0 },
  { "/File/Quit", "<control>Q", (MenuCallback) file_quit_cmd_callback, 0 },
  { "/File/---", NULL, NULL, 0, "<Separator>" },
  
  { "/Edit/Cut", "<control>X", (MenuCallback) edit_cut_cmd_callback, 0 },
  { "/Edit/Copy", "<control>C", (MenuCallback) edit_copy_cmd_callback, 0 },
  { "/Edit/Paste", "<control>V", (MenuCallback) edit_paste_cmd_callback, 0 },
  { "/Edit/Paste Into", NULL, (MenuCallback) edit_paste_into_cmd_callback, 0 },
  { "/Edit/Clear", "<control>K", (MenuCallback) edit_clear_cmd_callback, 0 },
  { "/Edit/Fill", "<control>.", (MenuCallback) edit_fill_cmd_callback, 0 },
  { "/Edit/Stroke", NULL, (MenuCallback) edit_stroke_cmd_callback, 0 },
  { "/Edit/Undo", "<control>Z", (MenuCallback) edit_undo_cmd_callback, 0 },
  { "/Edit/Redo", "<control>Y", (MenuCallback) edit_redo_cmd_callback, 0 },
  { "/Edit/---", NULL, NULL, 0, "<Separator>" },
  { "/Edit/Cut Named", "<control><shift>X", (MenuCallback) edit_named_cut_cmd_callback, 0 },
  { "/Edit/Copy Named", "<control><shift>C", (MenuCallback) edit_named_copy_cmd_callback, 0 },
  { "/Edit/Paste Named", "<control><shift>V", (MenuCallback) edit_named_paste_cmd_callback, 0 },
  { "/Edit/---", NULL, NULL, 0, "<Separator>" },
  
  { "/Select/Toggle", "<control>T", (MenuCallback) select_toggle_cmd_callback, 0 },
  { "/Select/Invert", "<control>I", (MenuCallback) select_invert_cmd_callback, 0 },
  { "/Select/All", "<control>A", (MenuCallback) select_all_cmd_callback, 0 },
  { "/Select/None", "<control><shift>A", (MenuCallback) select_none_cmd_callback, 0 },
  { "/Select/Float", "<control><shift>L", (MenuCallback) select_float_cmd_callback, 0 },
  { "/Select/Sharpen", "<control><shift>H", (MenuCallback) select_sharpen_cmd_callback, 0 },
  { "/Select/Border", "<control><shift>B", (MenuCallback) select_border_cmd_callback, 0 },
  { "/Select/Feather", "<control><shift>F", (MenuCallback) select_feather_cmd_callback, 0 },
  { "/Select/Grow", NULL, (MenuCallback) select_grow_cmd_callback, 0 },
  { "/Select/Shrink", NULL, (MenuCallback) select_shrink_cmd_callback, 0 },
  { "/Select/Save To Channel", NULL, (MenuCallback) select_save_cmd_callback, 0 },
  { "/Select/By Color...", NULL, (MenuCallback) select_by_color_cmd_callback, 0 },
  
  { "/View/Zoom In", "equal", (MenuCallback) view_zoomin_cmd_callback, 0 },
  { "/View/Zoom Out", "minus", (MenuCallback) view_zoomout_cmd_callback, 0 },
  { "/View/Zoom/16:1", NULL, (MenuCallback) view_zoom_16_1_callback, 0 },
  { "/View/Zoom/8:1", NULL, (MenuCallback) view_zoom_8_1_callback, 0 },
  { "/View/Zoom/4:1", NULL, (MenuCallback) view_zoom_4_1_callback, 0 },
  { "/View/Zoom/2:1", NULL, (MenuCallback) view_zoom_2_1_callback, 0 },
  { "/View/Zoom/1:1", "1", (MenuCallback) view_zoom_1_1_callback, 0 },
  { "/View/Zoom/1:2", NULL, (MenuCallback) view_zoom_1_2_callback, 0 },
  { "/View/Zoom/1:4", NULL, (MenuCallback) view_zoom_1_4_callback, 0 },
  { "/View/Zoom/1:8", NULL, (MenuCallback) view_zoom_1_8_callback, 0 },
  { "/View/Zoom/1:16", NULL, (MenuCallback) view_zoom_1_16_callback, 0 },
  { "/View/Window Info...", "<control><shift>I", (MenuCallback) view_window_info_cmd_callback, 0 },
  { "/View/Toggle Rulers", "<control><shift>R", (MenuCallback) view_toggle_rulers_cmd_callback, 0, "<ToggleItem>" },
  { "/View/Toggle Guides", "<control><shift>T", (MenuCallback) view_toggle_guides_cmd_callback, 0, "<ToggleItem>" },
  { "/View/Snap To Guides", NULL, (MenuCallback) view_snap_to_guides_cmd_callback, 0, "<ToggleItem>" },
  { "/View/---", NULL, NULL, 0, "<Separator>" },
  { "/View/New View", NULL, (MenuCallback) view_new_view_cmd_callback, 0 },
  { "/View/Shrink Wrap", "<control>E", (MenuCallback) view_shrink_wrap_cmd_callback, 0 },
  
  { "/Image/Colors/Equalize", NULL, (MenuCallback) image_equalize_cmd_callback, 0 },
  { "/Image/Colors/Invert", NULL, (MenuCallback) image_invert_cmd_callback, 0 },
  { "/Image/Colors/Posterize", NULL, (MenuCallback) image_posterize_cmd_callback, 0 },
  { "/Image/Colors/Threshold", NULL, (MenuCallback) image_threshold_cmd_callback, 0 },
  { "/Image/Colors/---", NULL, NULL, 0, "<Separator>" },
  { "/Image/Colors/Color Balance", NULL, (MenuCallback) image_color_balance_cmd_callback, 0 },
  { "/Image/Colors/Brightness-Contrast", NULL, (MenuCallback) image_brightness_contrast_cmd_callback, 0 },
  { "/Image/Colors/Hue-Saturation", NULL, (MenuCallback) image_hue_saturation_cmd_callback, 0 },
  { "/Image/Colors/Curves", NULL, (MenuCallback) image_curves_cmd_callback, 0 },
  { "/Image/Colors/Levels", NULL, (MenuCallback) image_levels_cmd_callback, 0 },
  { "/Image/Colors/---", NULL, NULL, 0, "<Separator>" },
  { "/Image/Colors/Desaturate", NULL, (MenuCallback) image_desaturate_cmd_callback, 0 },
  { "/Image/Channel Ops/Duplicate", "<control>D", (MenuCallback) channel_ops_duplicate_cmd_callback, 0 },
  { "/Image/Channel Ops/Offset", "<control><shift>O", (MenuCallback) channel_ops_offset_cmd_callback, 0 },
  { "/Image/Alpha/Add Alpha Channel", NULL, (MenuCallback) layers_add_alpha_channel_cmd_callback, 0 },
  
  { "/Image/---", NULL, NULL, 0, "<Separator>" },
  { "/Image/RGB", NULL, (MenuCallback) image_convert_rgb_cmd_callback, 0 },
  { "/Image/Grayscale", NULL, (MenuCallback) image_convert_grayscale_cmd_callback, 0 },
  { "/Image/Indexed", NULL, (MenuCallback) image_convert_indexed_cmd_callback, 0 },
  { "/Image/---", NULL, NULL, 0, "<Separator>" },
  { "/Image/Resize", NULL, (MenuCallback) image_resize_cmd_callback, 0 },
  { "/Image/Scale", NULL, (MenuCallback) image_scale_cmd_callback, 0 },
  { "/Image/---", NULL, NULL, 0, "<Separator>" },
  { "/Image/Histogram", NULL, (MenuCallback) image_histogram_cmd_callback, 0 },
  { "/Image/---", NULL, NULL, 0, "<Separator>" },
  
  { "/Layers/Layers & Channels...", "<control>L", (MenuCallback) dialogs_lc_cmd_callback, 0 },
  { "/Layers/Raise Layer", "<control>F", (MenuCallback) layers_raise_cmd_callback, 0 },
  { "/Layers/Lower Layer", "<control>B", (MenuCallback) layers_lower_cmd_callback, 0 },
  { "/Layers/Anchor Layer", "<control>H", (MenuCallback) layers_anchor_cmd_callback, 0 },
  { "/Layers/Merge Visible Layers", "<control>M", (MenuCallback) layers_merge_cmd_callback, 0 },
  { "/Layers/Flatten Image", NULL, (MenuCallback) layers_flatten_cmd_callback, 0 },
  { "/Layers/Alpha To Selection", NULL, (MenuCallback) layers_alpha_select_cmd_callback, 0 },
  { "/Layers/Mask To Selection", NULL, (MenuCallback) layers_mask_select_cmd_callback, 0 },
  { "/Layers/Add Alpha Channel", NULL, (MenuCallback) layers_add_alpha_channel_cmd_callback, 0 },
  
  { "/Tools/Rect Select", "R", (MenuCallback) tools_select_cmd_callback, RECT_SELECT },
  { "/Tools/Ellipse Select", "E", (MenuCallback) tools_select_cmd_callback, ELLIPSE_SELECT },
  { "/Tools/Free Select", "F", (MenuCallback) tools_select_cmd_callback, FREE_SELECT },
  { "/Tools/Fuzzy Select", "Z", (MenuCallback) tools_select_cmd_callback, FUZZY_SELECT },
  { "/Tools/Bezier Select", "B", (MenuCallback) tools_select_cmd_callback, BEZIER_SELECT },
  { "/Tools/Intelligent Scissors", "I", (MenuCallback) tools_select_cmd_callback, ISCISSORS },
  { "/Tools/Move", "M", (MenuCallback) tools_select_cmd_callback, MOVE },
  { "/Tools/Magnify", "<shift>M", (MenuCallback) tools_select_cmd_callback, MAGNIFY },
  { "/Tools/Crop", "<shift>C", (MenuCallback) tools_select_cmd_callback, CROP },
  { "/Tools/Transform", "<shift>T", (MenuCallback) tools_select_cmd_callback, ROTATE },
  { "/Tools/Flip", "<shift>F", (MenuCallback) tools_select_cmd_callback, FLIP_HORZ },
  { "/Tools/Text", "T", (MenuCallback) tools_select_cmd_callback, TEXT },
  { "/Tools/Color Picker", "O", (MenuCallback) tools_select_cmd_callback, COLOR_PICKER },
  { "/Tools/Bucket Fill", "<shift>B", (MenuCallback) tools_select_cmd_callback, BUCKET_FILL },
  { "/Tools/Blend", "L", (MenuCallback) tools_select_cmd_callback, BLEND },
  { "/Tools/Paintbrush", "P", (MenuCallback) tools_select_cmd_callback, PAINTBRUSH },
  { "/Tools/Pencil", "<shift>P", (MenuCallback) tools_select_cmd_callback, PENCIL },
  { "/Tools/Eraser", "<shift>E", (MenuCallback) tools_select_cmd_callback, ERASER },
  { "/Tools/Airbrush", "A", (MenuCallback) tools_select_cmd_callback, AIRBRUSH },
  { "/Tools/Clone", "C", (MenuCallback) tools_select_cmd_callback, CLONE },
  { "/Tools/Convolve", "V", (MenuCallback) tools_select_cmd_callback, CONVOLVE },
  { "/Tools/Default Colors", "D", (MenuCallback) tools_default_colors_cmd_callback, 0 },
  { "/Tools/Swap Colors", "X", (MenuCallback) tools_swap_colors_cmd_callback, 0 },  
  { "/Tools/---", NULL, NULL, 0, "<Separator>" },
  { "/Tools/Toolbox", NULL, (MenuCallback) toolbox_raise_callback, 0 },
  
  { "/Filters/", NULL, NULL, 0 },
  { "/Filters/Repeat last", "<alt>F", (MenuCallback) filters_repeat_cmd_callback, 0x0 },
  { "/Filters/Re-show last", "<alt><shift>F", (MenuCallback) filters_repeat_cmd_callback, 0x1 },
  { "/Filters/---", NULL, NULL, 0, "<Separator>" },
  
  { "/Script-Fu/", NULL, NULL, 0 },
  
  { "/Dialogs/Brushes...", "<control><shift>B", (MenuCallback) dialogs_brushes_cmd_callback, 0 },
  { "/Dialogs/Patterns...", "<control><shift>P", (MenuCallback) dialogs_patterns_cmd_callback, 0 },
  { "/Dialogs/Palette...", "<control>P", (MenuCallback) dialogs_palette_cmd_callback, 0 },
  { "/Dialogs/Gradient Editor...", "<control>G", (MenuCallback) dialogs_gradient_editor_cmd_callback, 0 },
  { "/Dialogs/Layers & Channels...", "<control>L", (MenuCallback) dialogs_lc_cmd_callback, 0 },
  { "/Dialogs/Indexed Palette...", NULL, (MenuCallback) dialogs_indexed_palette_cmd_callback, 0 },
  { "/Dialogs/Tool Options...", NULL, (MenuCallback) dialogs_tools_options_cmd_callback, 0 },
};
static int n_image_entries = sizeof (image_entries) / sizeof (image_entries[0]);


/*  A submenu: its model holds sections, and new items go into the last
 *  one; a separator starts a new section.
 */
typedef struct _MenuNode MenuNode;

struct _MenuNode
{
  char     *path;
  GMenu    *menu;
  GMenu    *section;
  gboolean  sensitive;
};

typedef struct _MenuItemInfo MenuItemInfo;

struct _MenuItemInfo
{
  char          *path;
  char          *action_name;     /*  within the "gimp" action group  */
  GSimpleAction *action;
  GMenu         *section;         /*  the section the item was put in  */
  GtkShortcut   *shortcut;
  GListStore    *shortcuts;       /*  the store the shortcut is in     */
  MenuCallback   callback;
  gpointer       callback_data;
  guint          callback_action;
  gboolean       toggle;
  gboolean       sensitive;
};

static void          menus_init          (void);
static MenuNode *    menus_get_node      (const char   *path);
static void          menus_add_entry     (const char   *factory,
					  MenuEntry    *entry);
static void          menus_update_sensitivity (MenuItemInfo *item);
static char *        menus_convert_accel (const char   *accel);

static GHashTable         *menu_nodes = NULL;    /*  path -> MenuNode      */
static GHashTable         *menu_items = NULL;    /*  path -> MenuItemInfo  */
static GHashTable         *menu_shortcuts = NULL; /*  factory -> GListStore */
static GSimpleActionGroup *menu_actions = NULL;
static guint               menu_action_count = 0;

static int initialize = TRUE;


void
menus_create (MenuEntry *entries,
	      int        nmenu_entries)
{
  int i;

  if (initialize)
    menus_init ();

  for (i = 0; i < nmenu_entries; i++)
    menus_add_entry (NULL, &entries[i]);
}

void
menus_set_sensitive (char *path,
		     int   sensitive)
{
  MenuItemInfo *item;
  MenuNode *node;

  if (initialize)
    menus_init ();

  item = g_hash_table_lookup (menu_items, path);
  if (item)
    {
      item->sensitive = sensitive ? TRUE : FALSE;
      menus_update_sensitivity (item);
      return;
    }

  node = g_hash_table_lookup (menu_nodes, path);
  if (node)
    {
      GHashTableIter iter;
      gpointer key, value;
      size_t len = strlen (path);

      node->sensitive = sensitive ? TRUE : FALSE;

      /*  Everything below a submenu follows it.  */
      g_hash_table_iter_init (&iter, menu_items);
      while (g_hash_table_iter_next (&iter, &key, &value))
	if (strncmp (key, path, len) == 0 && ((char *) key)[len] == '/')
	  menus_update_sensitivity (value);
      return;
    }

  g_debug ("Unable to set sensitivity for menu which doesn't exist: %s", path);
}

void
menus_set_state (char *path,
		 int   state)
{
  MenuItemInfo *item;

  if (initialize)
    menus_init ();

  item = g_hash_table_lookup (menu_items, path);

  if (item && item->toggle)
    g_simple_action_set_state (item->action,
			       g_variant_new_boolean (state ? TRUE : FALSE));
  else
    g_debug ("Unable to set state for menu which doesn't exist: %s", path);
}

int
menus_get_state (char *path)
{
  MenuItemInfo *item;
  GVariant *state;
  int active;

  if (initialize)
    menus_init ();

  item = g_hash_table_lookup (menu_items, path);
  if (!item || !item->toggle)
    return FALSE;

  state = g_action_get_state (G_ACTION (item->action));
  active = g_variant_get_boolean (state);
  g_variant_unref (state);

  return active;
}

void
menus_destroy (char *path)
{
  MenuItemInfo *item;
  char *detailed;
  int i, n;

  if (initialize)
    menus_init ();

  item = g_hash_table_lookup (menu_items, path);
  if (!item)
    return;

  detailed = g_strconcat ("gimp.", item->action_name, NULL);
  n = g_menu_model_get_n_items (G_MENU_MODEL (item->section));
  for (i = 0; i < n; i++)
    {
      char *action = NULL;

      if (g_menu_model_get_item_attribute (G_MENU_MODEL (item->section), i,
					   G_MENU_ATTRIBUTE_ACTION, "s",
					   &action) &&
	  strcmp (action, detailed) == 0)
	{
	  g_free (action);
	  g_menu_remove (item->section, i);
	  break;
	}
      g_free (action);
    }
  g_free (detailed);

  if (item->shortcut)
    {
      guint position;

      if (g_list_store_find (item->shortcuts, item->shortcut, &position))
	g_list_store_remove (item->shortcuts, position);
    }

  g_action_map_remove_action (G_ACTION_MAP (menu_actions), item->action_name);
  g_hash_table_remove (menu_items, path);
}

void
menus_quit ()
{
  if (!initialize)
    {
      g_hash_table_destroy (menu_items);
      g_hash_table_destroy (menu_nodes);
      g_hash_table_destroy (menu_shortcuts);
      g_object_unref (menu_actions);

      menu_items = NULL;
      menu_nodes = NULL;
      menu_shortcuts = NULL;
      menu_actions = NULL;
      initialize = TRUE;
    }
}

GMenuModel *
menus_get_toolbox_model (void)
{
  if (initialize)
    menus_init ();

  return G_MENU_MODEL (menus_get_node ("<Toolbox>")->menu);
}

GMenuModel *
menus_get_image_model (void)
{
  if (initialize)
    menus_init ();

  return G_MENU_MODEL (menus_get_node ("<Image>")->menu);
}

void
menus_install (GtkWidget  *window,
	       const char *factory)
{
  GListStore *store;
  GtkEventController *controller;

  if (initialize)
    menus_init ();

  gtk_widget_insert_action_group (window, "gimp", G_ACTION_GROUP (menu_actions));

  store = g_hash_table_lookup (menu_shortcuts, factory);
  if (!store)
    {
      store = g_list_store_new (GTK_TYPE_SHORTCUT);
      g_hash_table_insert (menu_shortcuts, g_strdup (factory), store);
    }

  controller = gtk_shortcut_controller_new_for_model (G_LIST_MODEL (store));
  gtk_widget_add_controller (window, controller);
}


static void
menu_node_free (gpointer data)
{
  MenuNode *node = data;

  g_free (node->path);
  g_object_unref (node->menu);
  g_free (node);
}

static void
menu_item_free (gpointer data)
{
  MenuItemInfo *item = data;

  g_free (item->path);
  g_free (item->action_name);
  g_object_unref (item->action);
  if (item->shortcut)
    g_object_unref (item->shortcut);
  g_free (item);
}

static void
menus_init ()
{
  int i;

  if (!initialize)
    return;

  initialize = FALSE;

  menu_nodes = g_hash_table_new_full (g_str_hash, g_str_equal,
				      NULL, menu_node_free);
  menu_items = g_hash_table_new_full (g_str_hash, g_str_equal,
				      NULL, menu_item_free);
  menu_shortcuts = g_hash_table_new_full (g_str_hash, g_str_equal,
					  g_free, g_object_unref);
  menu_actions = g_simple_action_group_new ();

  for (i = 0; i < n_toolbox_entries; i++)
    menus_add_entry ("<Toolbox>", &toolbox_entries[i]);
  for (i = 0; i < n_image_entries; i++)
    menus_add_entry ("<Image>", &image_entries[i]);
}

/*  The node for a submenu path, created with its parents as needed.  */
static MenuNode *
menus_get_node (const char *path)
{
  MenuNode *node;
  const char *slash;

  node = g_hash_table_lookup (menu_nodes, path);
  if (node)
    return node;

  node = g_new0 (MenuNode, 1);
  node->path = g_strdup (path);
  node->menu = g_menu_new ();
  node->section = g_menu_new ();
  node->sensitive = TRUE;
  g_menu_append_section (node->menu, NULL, G_MENU_MODEL (node->section));
  g_object_unref (node->section);

  slash = strrchr (path, '/');
  if (slash)
    {
      char *parent_path = g_strndup (path, slash - path);
      char *label = g_strdup (slash + 1);
      MenuNode *parent = menus_get_node (parent_path);
      GString *escaped = g_string_new (NULL);
      const char *p;

      /*  GMenu labels use '_' for mnemonics.  */
      for (p = label; *p; p++)
	{
	  if (*p == '_')
	    g_string_append_c (escaped, '_');
	  g_string_append_c (escaped, *p);
	}

      /*  GtkPopoverMenu names a submenu's page after its label, so two
       *  submenus called "Render" (one under Filters, one under
       *  Script-Fu) would clash.  Repeats get invisible zero-width
       *  spaces added to tell them apart.
       */
      {
	static GHashTable *labels = NULL;
	char *root = g_strndup (path, strcspn (path, "/"));
	char *key = g_strconcat (root, "|", escaped->str, NULL);
	int count;

	if (!labels)
	  labels = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	count = GPOINTER_TO_INT (g_hash_table_lookup (labels, key));
	g_hash_table_replace (labels, key, GINT_TO_POINTER (count + 1));
	while (count-- > 0)
	  g_string_append (escaped, "\xe2\x80\x8b");
	g_free (root);
      }

      g_menu_append_submenu (parent->section, escaped->str,
			     G_MENU_MODEL (node->menu));

      g_string_free (escaped, TRUE);
      g_free (label);
      g_free (parent_path);
    }

  g_hash_table_insert (menu_nodes, node->path, node);

  return node;
}

static void
menu_action_activate (GSimpleAction *action,
		      GVariant      *parameter,
		      gpointer       data)
{
  MenuItemInfo *item = data;

  if (item->toggle)
    {
      GVariant *state = g_action_get_state (G_ACTION (action));

      g_simple_action_set_state (action,
				 g_variant_new_boolean (!g_variant_get_boolean (state)));
      g_variant_unref (state);
    }

  if (item->callback)
    (* item->callback) (NULL, item->callback_data, item->callback_action);
}

static void
menus_add_entry (const char *factory,
		 MenuEntry  *entry)
{
  MenuItemInfo *item;
  MenuNode *node;
  GMenuItem *menu_item;
  GString *label;
  char *path;
  char *parent_path;
  const char *name;
  const char *p;
  char *detailed;

  path = factory ? g_strconcat (factory, entry->path, NULL)
		 : g_strdup (entry->path);

  /*  "<Image>/Filters/" only makes sure the submenu is there.  */
  if (path[strlen (path) - 1] == '/')
    {
      path[strlen (path) - 1] = '\0';
      menus_get_node (path);
      g_free (path);
      return;
    }

  name = strrchr (path, '/');
  if (!name)
    {
      g_free (path);
      return;
    }

  parent_path = g_strndup (path, name - path);
  name++;
  node = menus_get_node (parent_path);
  g_free (parent_path);

  if (entry->item_type && strcmp (entry->item_type, "<Separator>") == 0)
    {
      node->section = g_menu_new ();
      g_menu_append_section (node->menu, NULL, G_MENU_MODEL (node->section));
      g_object_unref (node->section);
      g_free (path);
      return;
    }

  /*  Registering the same path again updates what it does.  */
  item = g_hash_table_lookup (menu_items, path);
  if (item)
    {
      item->callback        = entry->callback;
      item->callback_data   = entry->callback_data;
      item->callback_action = entry->callback_action;
      g_free (path);
      return;
    }

  item = g_new0 (MenuItemInfo, 1);
  item->path            = path;
  item->action_name     = g_strdup_printf ("m%u", menu_action_count++);
  item->callback        = entry->callback;
  item->callback_data   = entry->callback_data;
  item->callback_action = entry->callback_action;
  item->toggle          = (entry->item_type &&
			   strcmp (entry->item_type, "<ToggleItem>") == 0);
  item->sensitive       = TRUE;
  item->section         = node->section;

  if (item->toggle)
    item->action = g_simple_action_new_stateful (item->action_name, NULL,
						 g_variant_new_boolean (FALSE));
  else
    item->action = g_simple_action_new (item->action_name, NULL);

  g_signal_connect (item->action, "activate",
		    G_CALLBACK (menu_action_activate), item);
  g_action_map_add_action (G_ACTION_MAP (menu_actions),
			   G_ACTION (item->action));

  label = g_string_new (NULL);
  for (p = name; *p; p++)
    {
      if (*p == '_')
	g_string_append_c (label, '_');
      g_string_append_c (label, *p);
    }

  detailed = g_strconcat ("gimp.", item->action_name, NULL);
  menu_item = g_menu_item_new (label->str, detailed);
  g_string_free (label, TRUE);

  if (entry->accelerator && *entry->accelerator)
    {
      char *accel = menus_convert_accel (entry->accelerator);
      GtkShortcutTrigger *trigger = gtk_shortcut_trigger_parse_string (accel);

      if (trigger)
	{
	  char *factory_name = g_strndup (path, strchr (path, '/') - path);
	  GListStore *store = g_hash_table_lookup (menu_shortcuts, factory_name);

	  if (!store)
	    {
	      store = g_list_store_new (GTK_TYPE_SHORTCUT);
	      g_hash_table_insert (menu_shortcuts, g_strdup (factory_name), store);
	    }

	  item->shortcut = gtk_shortcut_new (trigger,
					     gtk_named_action_new (detailed));
	  item->shortcuts = store;
	  g_list_store_append (store, item->shortcut);

	  g_menu_item_set_attribute (menu_item, "accel", "s", accel);
	  g_free (factory_name);
	}

      g_free (accel);
    }

  g_menu_append_item (node->section, menu_item);
  g_object_unref (menu_item);
  g_free (detailed);

  g_hash_table_insert (menu_items, item->path, item);
}

static void
menus_update_sensitivity (MenuItemInfo *item)
{
  gboolean sensitive = item->sensitive;
  char *path = g_strdup (item->path);
  char *slash;

  /*  An item is only usable when every submenu above it is.  */
  while (sensitive && (slash = strrchr (path, '/')))
    {
      MenuNode *node;

      *slash = '\0';
      node = g_hash_table_lookup (menu_nodes, path);
      if (node && !node->sensitive)
	sensitive = FALSE;
    }
  g_free (path);

  g_simple_action_set_enabled (item->action, sensitive);
}

/*  GTK 1 accelerators ("<control>N", "<alt><shift>F", "equal") in the
 *  form GTK 4 parses ("<Control>n", "<Alt><Shift>f", "equal").
 */
static char *
menus_convert_accel (const char *accel)
{
  GString *result = g_string_new (NULL);
  const char *p = accel;

  while (*p == '<')
    {
      const char *end = strchr (p, '>');
      char *mod;

      if (!end)
	break;

      mod = g_ascii_strdown (p + 1, end - p - 1);
      if (strcmp (mod, "control") == 0 || strcmp (mod, "ctrl") == 0)
	g_string_append (result, "<Control>");
      else if (strcmp (mod, "shift") == 0)
	g_string_append (result, "<Shift>");
      else if (strcmp (mod, "alt") == 0 || strcmp (mod, "mod1") == 0)
	g_string_append (result, "<Alt>");
      else
	g_string_append_printf (result, "<%s>", mod);
      g_free (mod);

      p = end + 1;
    }

  if (p[0] && !p[1])
    {
      guint keyval = gdk_unicode_to_keyval (g_ascii_tolower (p[0]));
      const char *name = gdk_keyval_name (keyval);

      g_string_append (result, name ? name : p);
    }
  else
    g_string_append (result, p);

  return g_string_free (result, FALSE);
}
