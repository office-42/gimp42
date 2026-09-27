# Porting gimp42 from GTK 1.2 to GTK 4

gimp42 is GIMP 1.0.4 brought forward: GLib 2, GTK 4, cairo and Pango, built
with Meson, running natively on Windows as well as Linux and macOS.  This
file is the working guide for that port: how to build, what replaces what,
and the conventions every ported file follows.

## Building

Linux and macOS: install GTK 4 (>= 4.10), Meson and Ninja, then

    meson setup builddir
    meson compile -C builddir

Windows: MSYS2 with the MINGW64 (or UCRT64) toolchain.

    pacman -S mingw-w64-x86_64-{toolchain,meson,ninja,pkgconf,gtk4,libjpeg-turbo,libpng,libtiff}
    export PATH=/c/msys64/mingw64/bin:$PATH
    meson setup builddir
    meson compile -C builddir

A single target builds on its own, which is how to check one file while the
rest of the tree is still being ported:

    ninja -C builddir plug-ins/sparkle/sparkle.exe      # .exe only on Windows

## Layout

* `libgimp/` - the plug-in library (`libgimp`), the wire protocol shared
  with the application (`libgimpwire`), GTK 4 widgets shared by the
  application and the plug-in dialogs (`libgimpwidgets`: `gimpwidgets.h`,
  `gimppreview.h`) and the image/layer/channel menus (`libgimpui`).
* `app/` - the application.  Only the flat `app/*.c` files are built; the
  subdirectories under `app/` are unused copies of them.
* `plug-ins/<name>/` - one directory per plug-in, each with its own
  `meson.build`.  `plug-ins/meson.build` picks up every listed directory
  that has one.
* `libgimpbase/`, `libgimpwidgets/`, `plug-ins/common`, `plug-ins/Lighting`
  and `plug-ins/uri` are unused copies and are not built.

## Ground rules

* Port to real GTK 4 and GLib 2 API.  There is no GTK 1 compatibility
  layer.  The helpers in `libgimp/gimpwidgets.h` exist because GTK 1
  semantics (expand/fill/padding packing, tables, dialog action areas,
  option menus, nested main loops) come up in almost every dialog; use
  them where they fit, and plain GTK 4 everywhere else.
* No X11, no Xlib, no `gdkx.h`.  No `fork`, `exec*`, `pipe`, `select` on
  pipes, SysV shared memory or signals beyond ANSI C: use `g_spawn_*`,
  `GIOChannel`, `GSubprocess` and friends.  Paths go through
  `g_build_filename`, `g_get_home_dir`, `g_get_user_config_dir`,
  `g_get_tmp_dir`; never assume `/` as the only separator or `/tmp`.
* GCC 14 makes implicit declarations, int/pointer conversions and
  incompatible pointer types errors.  Fix them properly: add the prototype,
  the include or the cast the code actually needs.  Keep the file's
  existing style (GNU layout, tabs, old-style definitions are fine).
* Keep the behaviour.  A dialog should have the same controls, defaults and
  results as before; it only has to look like a GTK 4 dialog.
* Do not add unit tests.

## Plug-in conventions

A plug-in dialog in GTK 4:

```c
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"     /* gimpwidgets.h, gimppreview.h, gimpmenu.h */

static gint
foo_dialog (void)
{
  GtkWidget *dlg, *button, *frame, *table;

  gtk_init ();                                  /* was gtk_init (&argc, &argv) + gtk_rc_parse () */

  dlg = gimp_dialog_new ("Foo");                /* was gtk_dialog_new () + set_title */
  g_signal_connect (dlg, "destroy", G_CALLBACK (foo_close_callback), NULL);

  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (foo_ok_callback), dlg, TRUE);
  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked", G_CALLBACK (gtk_window_destroy), dlg);

  frame = gtk_frame_new ("Parameter Settings");
  gimp_container_set_border_width (frame, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), frame, TRUE, TRUE, 0);
  ...
  gtk_window_present (GTK_WINDOW (dlg));        /* was gtk_widget_show (dlg) */

  gimp_main_loop_run ();                        /* was gtk_main (); gdk_flush (); */
  return run_flag;
}

static void foo_close_callback (GtkWidget *w, gpointer d) { gimp_main_loop_quit (); }
static void foo_ok_callback (GtkWidget *w, gpointer d)
{
  run_flag = TRUE;
  gtk_window_destroy (GTK_WINDOW (d));          /* was gtk_widget_destroy */
}
```

`plug-ins/sparkle/sparkle.c` is a complete worked example.

A load or save plug-in whose dialog the user cancels returns
`STATUS_CANCEL`, not `STATUS_SUCCESS` (which would mark the image saved)
or an error (which would show "Save failed").

Each plug-in directory gets a `meson.build`:

```meson
executable('foo', 'foo.c',
  dependencies: plugin_deps,            # libgimpui + libm; add jpeg_dep, png_dep, ...
  install: true,
  install_dir: plugin_install_dir,
  win_subsystem: 'windows',
)
```

A plug-in that needs an optional library wraps itself in
`if jpeg_dep.found() ... endif`.  One that cannot work on a platform (it
needs X11, sendmail, ...) either gets a portable replacement (GIO,
`g_app_info_launch_default_for_uri`, zlib instead of piping through
`gzip`, ...) or is left out on that platform with a comment saying why.
Shared helper libraries: `plug-ins/gpc` defines `gpc_dep`,
`plug-ins/megawidget` defines `megawidget_dep`, `plug-ins/libgck` defines
`gck_dep`; they are built before the plug-ins.

## What replaces what

### Objects and signals

| GTK 1.2 | GTK 4 / GLib 2 |
|---|---|
| `GtkObject *`, `GTK_OBJECT (x)` | `GObject *`, `G_OBJECT (x)` (or just `x`) |
| `gtk_signal_connect (o, "sig", (GtkSignalFunc) f, d)` | `g_signal_connect (o, "sig", G_CALLBACK (f), d)` |
| `gtk_signal_connect_object (o, "sig", f, obj)` | `g_signal_connect_swapped (o, "sig", G_CALLBACK (f), obj)` |
| `gtk_signal_connect_after` | `g_signal_connect_after` |
| `gtk_signal_disconnect*`, `gtk_signal_handler_block_by_data` | `g_signal_handlers_disconnect_by_func`, `g_signal_handlers_block_by_func` / `_by_data` via `g_signal_handlers_block_matched` |
| `gtk_signal_emit_by_name (o, "sig")` | `g_signal_emit_by_name (o, "sig")` |
| `gtk_object_set_user_data (o, d)` / `get_user_data` | `g_object_set_data (G_OBJECT (o), "user_data", d)` / `g_object_get_data (..., "user_data")` |
| `gtk_object_set_data` / `get_data` | `g_object_set_data` / `g_object_get_data` |
| `gtk_object_ref/unref/sink` | `g_object_ref/unref/ref_sink` |
| `GtkSignalFunc` | `GCallback` |
| `gtk_main ()` / `gtk_main_quit ()` | `gimp_main_loop_run ()` / `gimp_main_loop_quit ()` |
| `while (gtk_events_pending ()) gtk_main_iteration ();` | `gimp_process_events ()` |
| `gtk_timeout_add`, `gtk_idle_add`, `gtk_timeout_remove` | `g_timeout_add`, `g_idle_add`, `g_source_remove` (callbacks return `G_SOURCE_CONTINUE`/`_REMOVE`) |
| `gdk_input_add (fd, ...)` | `g_io_add_watch` on a `GIOChannel` |
| `gdk_flush ()`, `gdk_set_use_xshm`, `gtk_rc_parse`, `gtk_preview_set_*`, `gtk_widget_set_default_visual/colormap`, `gtk_widget_push/pop_visual/colormap` | delete |
| `g_malloc` of a struct + manual `gtk_type_new` classes | `G_DEFINE_TYPE` / `G_DECLARE_FINAL_TYPE` |

### Widgets and containers

| GTK 1.2 | GTK 4 |
|---|---|
| `gtk_widget_show (w)` | delete (widgets are visible); toplevels: `gtk_window_present (GTK_WINDOW (w))` |
| `gtk_widget_hide (w)` / show again | `gtk_widget_set_visible (w, FALSE/TRUE)` |
| `gtk_widget_destroy (w)` | toplevel: `gtk_window_destroy`; child: `gimp_widget_destroy (w)` |
| `gtk_widget_set_usize (w, x, y)` | `gtk_widget_set_size_request (w, x, y)` (0 or -1 means "unset": use -1) |
| `gtk_widget_draw (w, NULL)`, `gtk_widget_queue_draw` | `gtk_widget_queue_draw (w)` |
| `w->allocation.width/height` | `gtk_widget_get_width/height (w)` |
| `w->window`, `gtk_widget_realize`, `gtk_widget_set_events` | none: draw in the draw function, use event controllers |
| `GTK_WIDGET_VISIBLE (w)`, `GTK_WIDGET_SENSITIVE` | `gtk_widget_get_visible`, `gtk_widget_is_sensitive` |
| `GTK_WIDGET_SET_FLAGS (w, GTK_CAN_DEFAULT)` | delete |
| `gtk_widget_grab_default (w)` | `gtk_window_set_default_widget (win, w)` |
| `gtk_window_position (win, GTK_WIN_POS_MOUSE)` | delete |
| `gtk_window_set_policy (...)` | `gtk_window_set_resizable` |
| `gtk_container_add (c, w)` | the specific call (`gtk_window_set_child`, `gtk_frame_set_child`, `gtk_button_set_child`, `gtk_scrolled_window_set_child`, `gtk_box_append`) or `gimp_container_add (c, w)` |
| `gtk_container_border_width (c, n)` | `gimp_container_set_border_width (c, n)` |
| `gtk_hbox_new (h, s)` / `gtk_vbox_new` | `gimp_hbox_new (h, s)` / `gimp_vbox_new` or `gtk_box_new (orientation, s)` |
| `gtk_box_pack_start (b, w, expand, fill, pad)` | `gimp_box_pack_start (b, w, expand, fill, pad)`; `gtk_box_append` when all are FALSE/0 |
| `gtk_box_pack_end` | `gimp_box_pack_end` |
| `gtk_table_new (r, c, h)` | `gimp_table_new (r, c, h)` (a GtkGrid) |
| `gtk_table_attach (t, w, l, r, t, b, xo, yo, xp, yp)` | `gimp_table_attach (t, w, ..., GIMP_EXPAND/GIMP_FILL/GIMP_SHRINK ...)` |
| `gtk_table_attach_defaults` | `gimp_table_attach_defaults` |
| `gtk_table_set_row_spacings / col_spacings` | `gtk_grid_set_row_spacing / column_spacing` |
| `gtk_misc_set_alignment (GTK_MISC (w), x, y)` | labels: `gtk_label_set_xalign/yalign`; others: `gimp_misc_set_alignment` |
| `gtk_alignment_new` | `halign`/`valign` on the child |
| `gtk_frame_set_shadow_type` | delete |
| `gtk_aspect_frame_new (label, x, y, ratio, obey)` | `gtk_aspect_frame_new (x, y, ratio, obey)`; label separately if needed |
| `gtk_event_box_new` | not needed: attach controllers to the child |
| `gtk_hseparator_new` / `vseparator` | `gtk_separator_new (GTK_ORIENTATION_HORIZONTAL/VERTICAL)` |
| `gtk_hscrollbar_new (adj)` / `vscrollbar` | `gtk_scrollbar_new (orientation, adj)` |
| `gtk_scrolled_window_new (NULL, NULL)` | `gtk_scrolled_window_new ()` |
| `gtk_scrolled_window_add_with_viewport` | `gtk_scrolled_window_set_child` |
| `gtk_tooltips_*` | `gtk_widget_set_tooltip_text` |
| `gtk_notebook_append_page` | same |
| `gtk_pixmap_new (pixmap, mask)` / XPM data | `gdk_pixbuf_new_from_xpm_data` then `gtk_picture_new_for_paintable (GDK_PAINTABLE (gdk_texture_new_for_pixbuf (pb)))` |
| `gtk_arrow_new (GTK_ARROW_DOWN, ...)` | `gtk_image_new_from_icon_name ("pan-down-symbolic")` |
| `gtk_text_*` | `GtkTextView` + `GtkTextBuffer` |
| `GtkList`, `GtkListItem`, `GtkCList` | `GtkListBox` with a row per item (or `GtkColumnView` for real tables) |
| `gtk_progress_bar_update (pb, f)` | `gtk_progress_bar_set_fraction (pb, f)` |

### Dialogs, buttons, entries, ranges

| GTK 1.2 | GTK 4 |
|---|---|
| `gtk_dialog_new ()`, `GTK_DIALOG (d)->vbox`, `->action_area` | `gimp_dialog_new (title)`, `gimp_dialog_get_vbox (d)`, `gimp_dialog_get_action_area (d)` |
| OK/Cancel buttons packed into the action area | `gimp_dialog_add_button (d, label, callback, data, is_default)` |
| `"delete_event"` returning TRUE to keep the window | `"close-request"` returning TRUE |
| `gtk_file_selection_*` | `gimp_file_dialog_open/save` (asynchronous: continue in the callback) |
| `gtk_color_selection_dialog_*` | `gimp_color_dialog_run` (asynchronous) |
| `gtk_radio_button_new_with_label (group, l)`, `gtk_radio_button_group` | `gimp_radio_button_new (previous_button_or_NULL, l)` |
| `gtk_check_button_new_with_label` | same |
| `GTK_TOGGLE_BUTTON (w)->active`, `gtk_toggle_button_set_state` | check/radio buttons: `gtk_check_button_get_active/set_active`; real toggle buttons: `gtk_toggle_button_get_active/set_active` |
| `gtk_entry_set_text / get_text` | `gtk_editable_set_text / get_text (GTK_EDITABLE (e))` |
| `gtk_entry_new_with_max_length (n)` | `gtk_entry_new ()` + `gtk_entry_set_max_length` |
| `gtk_adjustment_new` returns `GtkObject *` | returns `GtkAdjustment *` |
| `GTK_ADJUSTMENT (a)->value`, `->lower`, `->upper` | `gtk_adjustment_get_value / get_lower / get_upper` (and `set_*`) |
| `gtk_hscale_new (adj)` + `set_value_pos` + `set_digits` | `gimp_hscale_new (adj, digits)` or `gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, adj)` |
| `gtk_range_set_update_policy` | delete |
| `gtk_spin_button_new (adj, climb, digits)` | same |
| `gtk_option_menu_new` + `gtk_menu` of items with "activate" callbacks + `set_menu` + `set_history` | `gimp_option_menu_new ()` + `gimp_option_menu_append (om, label, G_CALLBACK (cb), data)` + `gimp_option_menu_set_history`; callbacks get the option menu and the item data |
| `gimp_image_menu_new` / `layer` / `channel` / `drawable` in plug-ins | now return a ready option menu: pack it directly, no `gtk_option_menu_set_menu` |
| popup `gtk_menu_popup` | `GtkPopoverMenu` on a `GMenu` with `GAction`s, or a `GtkPopover` holding buttons |

### Drawing

GTK 4 has no GdkWindow to draw on, no GdkGC, GdkPixmap, GdkImage, GdkFont
and no XOR.  Everything is drawn from a draw function with cairo:

| GTK 1.2 | GTK 4 |
|---|---|
| `gtk_preview_new (GTK_PREVIEW_COLOR/GRAYSCALE)` | `gimp_preview_new (GIMP_PREVIEW_COLOR/GRAYSCALE)` |
| `gtk_preview_size (GTK_PREVIEW (p), w, h)` | `gimp_preview_size (GIMP_PREVIEW (p), w, h)` |
| `gtk_preview_draw_row (GTK_PREVIEW (p), buf, x, y, w)` | `gimp_preview_draw_row (GIMP_PREVIEW (p), buf, x, y, w)`; the widget redraws itself |
| `GTK_PREVIEW (p)->buffer`, `buffer_width/height` | `gimp_preview_get_buffer`, `gimp_preview_get_width/height`, then `gimp_preview_changed` |
| `gtk_drawing_area_new` + `gtk_drawing_area_size` + `"expose_event"` | `gtk_drawing_area_new` + `gtk_drawing_area_set_content_width/height` + `gtk_drawing_area_set_draw_func` |
| `"configure_event"` | `"resize"` on the drawing area |
| `"button_press_event"` / `"button_release_event"` | `GtkGestureClick` (`gtk_gesture_single_set_button (g, 0)` for all buttons) or `GtkGestureDrag` |
| `"motion_notify_event"` | `GtkEventControllerMotion` (or the drag gesture's `drag-update`) |
| `"key_press_event"` | `GtkEventControllerKey` |
| `"enter_notify_event"` / `"leave_notify_event"` | `GtkEventControllerMotion` `enter` / `leave` |
| `GdkEventButton *ev`: `ev->x`, `ev->button`, `ev->state` | gesture callback arguments, `gtk_gesture_single_get_current_button`, `gtk_event_controller_get_current_event_state` |
| `GDK_SHIFT_MASK`, `GDK_CONTROL_MASK`, `GDK_MOD1_MASK` | `GDK_SHIFT_MASK`, `GDK_CONTROL_MASK`, `GDK_ALT_MASK` |
| `GDK_BUTTON1_MASK` in a motion state | `GDK_BUTTON1_MASK` |
| `gdk_draw_line/rectangle/arc/polygon/points/segments` | `cairo_move_to/line_to`, `cairo_rectangle`, `cairo_arc`, then `cairo_stroke/fill` |
| `GdkGC` foreground / line width / dashes | cairo source colour, `cairo_set_line_width`, `cairo_set_dash` |
| `GDK_INVERT`, `GDK_XOR` rubber-banding | keep the state, `gtk_widget_queue_draw`, draw it in the draw function (`CAIRO_OPERATOR_DIFFERENCE` with white looks like XOR) |
| `GdkPixmap` back buffer | a `cairo_surface_t` (`cairo_image_surface_create`), painted in the draw function |
| `gdk_draw_rgb_image` / `gdk_draw_image` | fill a cairo image surface (`CAIRO_FORMAT_RGB24`, 0x00RRGGBB per pixel) and paint it |
| `gdk_font_load`, `gdk_draw_string`, `gdk_string_width` | Pango: `pango_cairo_create_layout`, `pango_layout_set_text`, `pango_cairo_show_layout`, `pango_layout_get_pixel_size` |
| `gdk_color_alloc`, `GdkColor.pixel` | `GdkRGBA` or plain doubles for `cairo_set_source_rgb` |
| `gdk_cursor_new (GDK_CROSSHAIR)` + `gdk_window_set_cursor` | `gtk_widget_set_cursor_from_name (w, "crosshair")` |
| `gdk_pointer_grab` / `ungrab` | not needed: gestures keep receiving events while a button is held |
| `gdk_window_get_pointer` | track the position from the motion controller |

## Plug-in protocol on Windows

The application and a plug-in talk over two pipes wrapped in `GIOChannel`s
(`libgimp/gimpwire.c`).  On Windows the application passes the pipe ends
as inherited HANDLEs, which `gimp_main ()` turns back into C runtime
descriptors with `_open_osfhandle`.  Tiles always travel over the pipe;
there is no shared memory.

## The application's own GTK 4 layer

These replace X-era machinery inside `app/`; ported app code uses them.

* **Display** (`gdisplay.c`, `disp_callbacks.c`, `interface.c`): each
  image window's canvas is a `GtkDrawingArea`.  `gdisplay_display_area`
  renders the image into `gdisp->backing` (a cairo image surface the size
  of the canvas) and queues a redraw; the canvas' draw function paints
  the backing surface, then guides, the marching ants
  (`selection_draw`) and every visible tool `DrawCore` on that canvas.
  Nothing draws to the canvas directly any more.
* **Tool events** (`tools.h`): tools receive `GimpButtonEvent`,
  `GimpMotionEvent` and `GimpKeyEvent` - plain structs with the fields
  GTK 1's events had (`x`, `y`, `state`, `button`, `time`, `pressure`,
  `keyval`), in canvas coordinates.  Modifier masks are GDK 4's
  (`GDK_SHIFT_MASK`, `GDK_CONTROL_MASK`, `GDK_ALT_MASK` for Mod1,
  `GDK_BUTTON1_MASK`); keysyms are `GDK_KEY_*`.
* **Tool feedback** (`draw_core.h`): `draw_core_start (core, canvas,
  tool)` now takes the canvas widget (`gdisp->canvas`).  start/stop/
  pause/resume only toggle visibility and queue a redraw; the display
  calls `draw_func` from its draw function with `core->cr` set.  Inside
  `draw_func` use `draw_core_line`, `draw_core_rectangle`,
  `draw_core_arc`, `draw_core_segments`, `draw_core_lines`,
  `draw_core_polygon`, `draw_core_points` (GDK argument conventions,
  inverting look).  A draw function must only draw - never change tool
  state.  Where a tool used to draw once to erase and once to redraw,
  change the state and call `draw_core_queue_draw (core)` (or pause/
  resume).  `GdkSegment`/`GdkPoint` are `GimpSegment`/`GimpPoint`
  (`gimpsegment.h`).
* **Cursors** (`cursorutil.h`): `GimpCursorType` values
  `GIMP_CURSOR_TCROSS`, `GIMP_CURSOR_FLEUR`, ...; `change_win_cursor
  (widget, type)`.
* **Selecting tools**: `tools_select_widget (TOOL)` (interface.h), in
  place of activating the toolbox button.
* **Tool options** (`tools.h`): `tools_register_options (TOOL, vbox)`
  adds the vbox as a page of the options panel in the toolbox; its first
  label (the "... Options" title) becomes the panel's heading.  There is
  no Tool Options dialog any more; `tools_options_show ()` brings the
  toolbox forward.
* **Menus** (`menus.h`): path-based as before (`menus_set_sensitive`,
  `menus_set_state`, and `menus_get_state (path)` to read a toggle).
  Callbacks get `(NULL, callback_data, callback_action)`: a toggle
  callback must read its state with `menus_get_state`, not from the
  widget.  `gdisplay_active ()` is the display the command is for.
  An entry of type `"<Placeholder>"` keeps a hidden place for an item
  a plug-in registers later under the same path (plug-in items
  otherwise go at the end of their menu).
* **Option menus in dialogs** (`buildmenu.h`): `build_menu (items, NULL)`
  returns a ready option menu (GtkDropDown); pack it directly.
  `menu_item_set_sensitive (&items[i], s)`, `menu_item_set_active
  (&items[i])`.  Item callbacks are called with the option menu and the
  item's user_data.
* **Icons**: `create_pixmap_widget (data, w, h)` / `create_pixmap_texture`
  for the letter-coded icons in `pixmaps.h`; real XPM data goes through
  `gdk_pixbuf_new_from_xpm_data` + `gdk_texture_new_for_pixbuf`.
  `ops_button_box_new (parent, buttons)` (no tooltips argument).
* **Dialog helpers** (`interface.h`, `actionarea.h`): `message_box (text,
  callback, data)` with a `MessageBoxCallback`; `query_string_box`.
* **Colors**: there are no pixel values, visuals or colormaps
  (`colormaps.h` is empty).  Draw with RGB through cairo.
* **64-bit Windows**: `long` is 32 bits.  Never cast pointers to `long`
  or `int`; use `GPOINTER_TO_INT`/`GINT_TO_POINTER` or `intptr_t`.
  Watch `(guint) pointer` hash functions and `(long) data` callbacks.

Building one app file while others still fail: compile only its object,
e.g. `ninja -C builddir app/gimp42.exe.p/levels.c.obj`.
