/*
   flame - cosmic recursive fractal flames
   Copyright (C) 1997  Scott Draves <spot@cs.cmu.edu>

   The GIMP -- an image manipulation program
   Copyright (C) 1995 Spencer Kimball and Peter Mattis

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#include "megawidget.h"

#include "libifs.h"
#include "rect.h"
#include "cmap.h"

#define variation_same   (-2)

/* Declare local functions. */
static void query(void);
static void run(char *name,
		int nparams,
		GParam * param,
		int *nreturn_vals,
		GParam ** return_vals);
static gint dialog(void);

static void doit(GDrawable * drawable);

static void set_flame_preview(void);
static void load_callback(GtkWidget * widget, gpointer data);
static void store_callback(GtkWidget * widget, gpointer data);
static void set_edit_preview(void);
static void menu_cb(GtkWidget * widget, gpointer data);
static void my_mw_update_cb(gpointer data);
static void init_mutants(void);

static char buffer[10000];
static GtkWidget *cmap_preview;
static GtkWidget *flame_preview;
static int preview_width, preview_height;
static GtkWidget *dlg;

/* the file dialog is asynchronous: at most one is open at a time, and
   the last file chosen is offered again next time */
static gboolean file_dlg_active = FALSE;
static gchar *last_filename = NULL;

static GtkWidget *edit_dlg = 0;

#define preview_size 150
#define edit_preview_size 85
#define nmutants 9

static control_point edit_cp;
static control_point mutants[nmutants];
static GtkWidget *edit_previews[nmutants];
static double pick_speed = 0.2;


GPlugInInfo PLUG_IN_INFO =
{
  NULL, /* init_proc */
  NULL, /* quit_proc */
  query, /* query_proc */
  run, /* run_proc */
};

static int run_flag = 0;

#define black_drawable (-2)
#define gradient_drawable (-3)
#define table_drawable (-4)



static struct {
  int randomize;  /* superseded */
  int variation;
  gint32 cmap_drawable;
  control_point cp;
} config;

static frame_spec f = {0.0, &config.cp, 1, 0.0};


MAIN()


static void query(void)
{
  static GParamDef args[] =
  {
    {PARAM_INT32, "run_mode", "Interactive, non-interactive"},
    {PARAM_IMAGE, "image", "Input image (unused)"},
    {PARAM_DRAWABLE, "drawable", "Input drawable"},
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof(args) / sizeof(args[0]);
  static int nreturn_vals = 0;

  gimp_install_procedure("plug_in_flame",
			 "cosmic recursive fractal flames",
			 "use Smooth Palette to make colormaps",
			 "Scott Draves",
			 "Scott Draves",
			 "1997",
			 "<Image>/Filters/Render/Flame",
			 "RGB*",
			 PROC_PLUG_IN,
			 nargs, nreturn_vals,
			 args, return_vals);
}

static void maybe_init_cp(void) {
  if (0 == config.cp.spatial_oversample) {
    config.randomize = 0;
    config.variation = variation_same;
    config.cmap_drawable = gradient_drawable;
    random_control_point(&config.cp, variation_random);
    config.cp.center[0] = 0.0;
    config.cp.center[1] = 0.0;
    config.cp.pixels_per_unit = 100;
    config.cp.spatial_oversample = 2;
    config.cp.gamma = 2.0;
    config.cp.contrast = 1.0;
    config.cp.brightness = 1.0;
    config.cp.spatial_filter_radius = 0.75;
    config.cp.sample_density = 5.0;
    config.cp.zoom = 0.0;
    config.cp.nbatches = 1;
    config.cp.white_level = 200;
    config.cp.cmap_index = 72;
    /* cheating */
    config.cp.width = 256;
    config.cp.height = 256;
  }
}

static void run(char *name, int n_params, GParam * param, int *nreturn_vals,
		GParam ** return_vals)
{
  static GParam values[1];
  GDrawable *drawable = NULL;
  GRunModeType run_mode;
  GStatusType status = STATUS_SUCCESS;

  *nreturn_vals = 1;
  *return_vals = values;

  g_random_set_seed((guint32) time(NULL));

  run_mode = param[0].data.d_int32;

  if (run_mode == RUN_NONINTERACTIVE) {
    status = STATUS_CALLING_ERROR;
  } else {
    gimp_get_data("plug_in_flame", &config);
    /* XXX i tried using the init routine, but it didn't work. */
    mw_update_cb = my_mw_update_cb;
    maybe_init_cp();

    drawable = gimp_drawable_get(param[2].data.d_drawable);
    config.cp.width = drawable->width;
    config.cp.height = drawable->height;

    if (run_mode == RUN_INTERACTIVE) {
      if (!dialog()) {
	status = STATUS_EXECUTION_ERROR;
      }
    }
  }


  if (status == STATUS_SUCCESS) {

    if (gimp_drawable_color(drawable->id)) {
      gimp_progress_init("Drawing Flame...");
      gimp_tile_cache_ntiles(2 * (drawable->width / gimp_tile_width() + 1));

      doit(drawable);

      if (run_mode != RUN_NONINTERACTIVE)
	gimp_displays_flush();
      gimp_set_data("plug_in_flame", &config, sizeof(config));
    } else {
      status = STATUS_EXECUTION_ERROR;
    }
    gimp_drawable_detach(drawable);
  }


  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
}

static void
drawable_to_cmap(control_point *cp) {
  int i, j;
  GPixelRgn pr;
  GDrawable *d;
  guchar *p;

  if (table_drawable >= config.cmap_drawable) {
    i = table_drawable - config.cmap_drawable;
    get_cmap(i, cp->cmap, 256);
  } else if (black_drawable == config.cmap_drawable) {
    for (i = 0; i < 256; i++)
      for (j = 0; j < 3; j++)
	cp->cmap[i][j] = 0.0;
  } else if (gradient_drawable == config.cmap_drawable) {
    gdouble *g = gimp_gradients_sample_uniform(256);
    if (g == NULL)
      return;
    for (i = 0; i < 256; i++)
      for (j = 0; j < 3; j++)
	cp->cmap[i][j] = g[i*4 + j];
    g_free(g);
  } else {
    d = gimp_drawable_get(config.cmap_drawable);
    p = (guchar *) malloc(d->bpp);
    gimp_pixel_rgn_init(&pr, d, 0, 0,
			d->width, d->height, FALSE, FALSE);
    for (i = 0; i < 256; i++) {
      gimp_pixel_rgn_get_pixel(&pr, p, i % d->width,
			       (i / d->width) % d->height);
      for (j = 0; j < 3; j++)
	cp->cmap[i][j] =
	  (d->bpp >= 3) ? (p[j] / 255.0) : (p[0]/255.0);
    }
    free(p);
  }
}

static void doit(GDrawable * drawable)
{
  gint width, height;
  guchar *tmp;
  gint bytes;

  width = drawable->width;
  height = drawable->height;
  bytes = drawable->bpp;

  if (3 != bytes && 4 != bytes) {
    g_message("flame: only works with three or four channels, not %d.",
	      bytes);
    return;
  }

  tmp = (guchar *) malloc(width * height * 4);
  if (tmp == NULL) {
    g_message("flame: cannot malloc %d bytes.", width * height * bytes);
    return;
  }

  /* render */
  config.cp.width = width;
  config.cp.height = height;
  if (config.randomize)
    random_control_point(&config.cp, config.variation);
  drawable_to_cmap(&config.cp);
  render_rectangle(&f, tmp, width, field_both, 4,
		   gimp_progress_update);

  /* update destination */
  if (4 == bytes) {
    GPixelRgn pr;
    gimp_pixel_rgn_init(&pr, drawable, 0, 0, width, height,
			TRUE, TRUE);
    gimp_pixel_rgn_set_rect(&pr, tmp, 0, 0, width, height);
  } else if (3 == bytes) {
    int i, j;
    GPixelRgn src_pr, dst_pr;
    guchar *sl = (guchar *) malloc(3 * width);
    if (sl == NULL) {
      g_message("flame: cannot malloc %d bytes.", width * 3);
      free(tmp);
      return;
    }
    gimp_pixel_rgn_init(&src_pr, drawable,
			0, 0, width, height, FALSE, FALSE);
    gimp_pixel_rgn_init(&dst_pr, drawable,
			0, 0, width, height, TRUE, TRUE);
    for (i = 0; i < height; i++) {
      guchar *rr = tmp + 4 * i * width;
      guchar *sld = sl;
      gimp_pixel_rgn_get_rect(&src_pr, sl, 0, i, width, 1);
      for (j = 0; j < width; j++) {
	int k, alpha = rr[3];
	for (k = 0; k < 3; k++) {
	  int t = (rr[k] + ((sld[k] * (256-alpha)) >> 8));
	  if (t > 255) t = 255;
	  sld[k] = t;
	}
	rr += 4;
	sld += 3;
      }
      gimp_pixel_rgn_set_rect(&dst_pr, sl, 0, i, width, 1);
    }
    free(sl);
  } else
    g_message("flame: oops");
  free(tmp);
  gimp_drawable_flush(drawable);
  gimp_drawable_merge_shadow(drawable->id, TRUE);
  gimp_drawable_update(drawable->id, 0, 0, width, height);
}


static void close_callback(GtkWidget * widget, gpointer data)
{
  /* the edit dialog lives as long as the main one (it used to be
     destroyed by gtk_quit_add_destroy) */
  if (edit_dlg)
    gtk_window_destroy(GTK_WINDOW(edit_dlg));
  flame_preview = NULL;
  cmap_preview = NULL;
  gimp_main_loop_quit();
}

static void ok_callback(GtkWidget * widget, gpointer data)
{
  run_flag = 1;
  gtk_window_destroy(GTK_WINDOW(data));
}

static void load_flame(const gchar *filename) {
  FILE *fp = g_fopen(filename, "r");
  int i, c;
  char *ss;

  if (NULL == fp) {
    g_message("%s: %s", filename, g_strerror(errno));
    return;
  }
  i = 0;
  ss = buffer;
  do {
    c = getc(fp);
    if (EOF == c)
      break;
    ss[i++] = c;
  } while (';' != c && i < (int) sizeof(buffer) - 1);
  ss[i] = 0;
  parse_control_point(&ss, &config.cp);
  fclose(fp);
  /* i want to update the existing dialogue, but it's
     too painful */
  gimp_set_data("plug_in_flame", &config, sizeof(config));
  set_flame_preview();
  set_edit_preview();
}

static void store_flame(const gchar *filename) {
  FILE *fp = g_fopen(filename, "w");
  if (NULL == fp) {
    g_message("%s: %s", filename, g_strerror(errno));
    return;
  }
  print_control_point(fp, &config.cp, 0);
  fclose(fp);
}

/* data is 1 for load, 0 for store */
static void file_dialog_callback(const gchar *filename, gpointer data) {
  file_dlg_active = FALSE;

  if (NULL == filename)
    return;

  g_free(last_filename);
  last_filename = g_strdup(filename);

  if (GPOINTER_TO_INT(data))
    load_flame(filename);
  else
    store_flame(filename);
}

static void randomize_callback(GtkWidget * widget, gpointer data) {
  random_control_point(&edit_cp, config.variation);
  init_mutants();
  set_edit_preview();
}

static gboolean edit_close_request(GtkWindow * window, gpointer data) {
  gtk_widget_set_visible(edit_dlg, FALSE);
  return TRUE;
}

static void edit_destroy_callback(GtkWidget * widget, gpointer data) {
  int i;

  edit_dlg = 0;
  for (i = 0; i < nmutants; i++)
    edit_previews[i] = NULL;
}

static void edit_ok_callback(GtkWidget * widget, gpointer data) {
  gtk_widget_set_visible(edit_dlg, FALSE);
  config.cp = edit_cp;
  set_flame_preview();
}

static void edit_cancel_callback(GtkWidget * widget, gpointer data) {
  gtk_widget_set_visible(edit_dlg, FALSE);
}

static void init_mutants(void) {
  int i;
  for (i = 0; i < nmutants; i++) {
    mutants[i] = edit_cp;
    random_control_point(mutants + i, config.variation);
    if (variation_same == config.variation)
      copy_variation(mutants + i, &edit_cp);
  }
}

static void my_mw_update_cb(gpointer data) {
  double *fd = (double *) data;
  if (fd == &pick_speed)
    set_edit_preview();
  else if (&config.cp.brightness == fd ||
	   &config.cp.contrast == fd ||
	   &config.cp.gamma == fd ||
	   &config.cp.zoom == fd ||
	   &config.cp.center[0] == fd ||
	   &config.cp.center[1] == fd)
    set_flame_preview();
}

static void set_edit_preview(void) {
  int y, i, j;
  guchar *b;
  control_point pcp;
  static frame_spec pf = {0.0, 0, 1, 0.0};
  int nbytes = edit_preview_size * edit_preview_size * 3;

  if (NULL == edit_previews[0]) return;

  b = malloc(nbytes);
  maybe_init_cp();
  drawable_to_cmap(&edit_cp);
  for (i = 0; i < 3; i++)
    for (j = 0; j < 3; j++) {
      int mut = i*3 + j;
      pf.cps = &pcp;
      if (1 == i && 1 == j) {
	pcp = edit_cp;
      } else {
	control_point ends[2];
	ends[0] = edit_cp;
	ends[1] = mutants[mut];
	ends[0].time = 0.0;
	ends[1].time = 1.0;
	interpolate(ends, 2, pick_speed, &pcp);
      }
      pcp.pixels_per_unit =
	(pcp.pixels_per_unit * edit_preview_size) / pcp.width;
      pcp.width = edit_preview_size;
      pcp.height = edit_preview_size;

      pcp.sample_density = 1;
      pcp.spatial_oversample = 1;
      pcp.spatial_filter_radius = 0.5;

      drawable_to_cmap(&pcp);

      render_rectangle(&pf, b, edit_preview_size, field_both, 3, NULL);

      for (y = 0; y < edit_preview_size; y++)
	gimp_preview_draw_row(GIMP_PREVIEW (edit_previews[mut]),
			      b + y * edit_preview_size * 3,
			      0, y, edit_preview_size);
    }
  free(b);
}

static void preview_clicked(GtkWidget * widget, gpointer data) {
  int mut = GPOINTER_TO_INT(data);
  if (mut == 4) {
    control_point t = edit_cp;
    init_mutants();
    edit_cp = t;
  } else {
    control_point ends[2];
    ends[0] = edit_cp;
    ends[1] = mutants[mut];
    ends[0].time = 0.0;
    ends[1].time = 1.0;
    interpolate(ends, 2, pick_speed, &edit_cp);
  }
  set_edit_preview();
}


static void
edit_callback(GtkWidget * widget, gpointer data) {
  edit_cp = config.cp;
  if (0 == edit_dlg) {
    GtkWidget *table;
    GtkWidget *button;
    GtkWidget *box, *frame, *vbox;
    int i, j;

    edit_dlg = gimp_dialog_new("Edit Flame");
    gtk_window_set_transient_for(GTK_WINDOW(edit_dlg), GTK_WINDOW(dlg));
    g_signal_connect(edit_dlg, "destroy",
		     G_CALLBACK (edit_destroy_callback), NULL);
    g_signal_connect(edit_dlg, "close-request",
		     G_CALLBACK (edit_close_request), NULL);

    gimp_dialog_add_button(edit_dlg, "Ok",
			   G_CALLBACK (edit_ok_callback), NULL, FALSE);
    gimp_dialog_add_button(edit_dlg, "Cancel",
			   G_CALLBACK (edit_cancel_callback), NULL, TRUE);

    frame = gtk_frame_new("Directions");
    gimp_container_set_border_width(frame, 10);
    gimp_box_pack_start(gimp_dialog_get_vbox(edit_dlg),
			frame, TRUE, TRUE, 0);

    table = gimp_table_new(3, 3, FALSE);
    gtk_frame_set_child(GTK_FRAME(frame), table);

    for (i = 0; i < 3; i++)
      for (j = 0; j < 3; j++) {
	int mut = i*3 + j;
	edit_previews[mut] = gimp_preview_new (GIMP_PREVIEW_COLOR);
	gimp_preview_size (GIMP_PREVIEW (edit_previews[mut]),
			   edit_preview_size, edit_preview_size);
	button = gtk_button_new();
	gtk_button_set_child (GTK_BUTTON(button), edit_previews[mut]);
	g_signal_connect(button, "clicked",
			 G_CALLBACK (preview_clicked),
			 GINT_TO_POINTER (mut));
	gimp_table_attach (table, button, i, i+1, j, j+1,
			   GIMP_EXPAND, GIMP_EXPAND, 0, 0);
      }

    frame = gtk_frame_new("Controls");
    gimp_container_set_border_width(frame, 10);
    gimp_box_pack_start(gimp_dialog_get_vbox(edit_dlg),
			frame, TRUE, TRUE, 0);

    vbox = gimp_vbox_new (FALSE, 5);
    gtk_frame_set_child (GTK_FRAME (frame), vbox);

    table = gimp_table_new(2, 2, FALSE);
    gimp_box_pack_start(vbox, table, TRUE, FALSE, 10);

    mw_fscale_entry_new(table, "Speed", 0.05, 0.5, 0.01, 0.1,
			0.0, 0, 1, 1, 2, &pick_speed);

    box = gimp_hbox_new (TRUE, 5);
    gimp_box_pack_start(vbox, box, TRUE, FALSE, 10);

    button = gtk_button_new_with_label("Randomize");
    g_signal_connect(button, "clicked",
		     G_CALLBACK (randomize_callback), NULL);
    gimp_box_pack_start(box, button, TRUE, FALSE, 0);

    {
      static struct {
	char *name;
	int value;
      } menu_items[] = {
	{"Same", variation_same },
	{"Random", variation_random },
	{"Linear", 0},
	{"Sinusoidal", 1},
	{"Spherical", 2},
	{"Swirl", 3},
	{"Horseshoe", 4},
	{"Polar", 5},
	{"Bent", 6},
	{ NULL, 0},
      };
      GtkWidget *hbox;
      GtkWidget *option_menu;
      GtkWidget *w;
      int i;

      hbox = gimp_hbox_new (FALSE, 5);
      gimp_box_pack_start(box, hbox, TRUE, FALSE, 10);

      w = gtk_label_new("Variation:");
      gtk_label_set_xalign(GTK_LABEL(w), 0.0);
      gtk_label_set_yalign(GTK_LABEL(w), 0.5);
      gimp_box_pack_start(hbox, w, FALSE, FALSE, 0);

      option_menu = gimp_option_menu_new ();
      gimp_box_pack_start(hbox, option_menu, FALSE, FALSE, 0);
      i = 0;
      while (menu_items[i].name) {
	gimp_option_menu_append (option_menu, menu_items[i].name,
				 G_CALLBACK (menu_cb),
				 GINT_TO_POINTER (menu_items[i].value));
	i++;
      }
      gimp_option_menu_set_history (option_menu, config.variation + 2);
    }
    init_mutants();
  }
  set_edit_preview();

  if (!gtk_widget_get_visible(edit_dlg))
    gtk_window_present(GTK_WINDOW(edit_dlg));
}

static void load_callback(GtkWidget * widget, gpointer data) {
  if (file_dlg_active)
    return;
  file_dlg_active = TRUE;
  gimp_file_dialog_open(GTK_WINDOW(dlg), "Load Flame", last_filename,
			file_dialog_callback, GINT_TO_POINTER(1));
}

static void store_callback(GtkWidget * widget, gpointer data) {
  if (file_dlg_active)
    return;
  file_dlg_active = TRUE;
  gimp_file_dialog_save(GTK_WINDOW(dlg), "Store Flame", last_filename,
			file_dialog_callback, GINT_TO_POINTER(0));
}

static void menu_cb(GtkWidget * widget, gpointer data) {
  config.variation = GPOINTER_TO_INT(data);
  if (variation_same != config.variation)
    random_control_point(&edit_cp, config.variation);
  init_mutants();
  set_edit_preview();
}

static void set_flame_preview(void) {
  int y;
  guchar *b;
  control_point pcp;
  static frame_spec pf = {0.0, 0, 1, 0.0};

  if (NULL == flame_preview)
    return;

  b = malloc(preview_width * preview_height * 3);

  maybe_init_cp();
  drawable_to_cmap(&config.cp);

  pf.cps = &pcp;
  pcp = config.cp;
  pcp.pixels_per_unit =
    (pcp.pixels_per_unit * preview_width) / pcp.width;
  pcp.width = preview_width;
  pcp.height = preview_height;
  pcp.sample_density = 1;
  pcp.spatial_oversample = 1;
  pcp.spatial_filter_radius = 0.1;
  render_rectangle(&pf, b, preview_width, field_both, 3, NULL);

  for (y = 0; y < preview_height; y++)
    gimp_preview_draw_row(GIMP_PREVIEW (flame_preview),
			  b+y*preview_width*3, 0, y, preview_width);
  free(b);
}

static void set_cmap_preview(void) {
  int i, x, y;
  guchar b[96];

  if (NULL == cmap_preview)
    return;

  drawable_to_cmap(&config.cp);

  for (y = 0; y < 32; y+=4) {
    for (x = 0; x < 32; x++) {
      int j;
      i = x + (y/4)*32;
      for (j = 0; j < 3; j++)
	b[x*3+j] = config.cp.cmap[i][j]*255.0;
    }
    gimp_preview_draw_row (GIMP_PREVIEW (cmap_preview), b, 0, y, 32);
    gimp_preview_draw_row (GIMP_PREVIEW (cmap_preview), b, 0, y+1, 32);
    gimp_preview_draw_row (GIMP_PREVIEW (cmap_preview), b, 0, y+2, 32);
    gimp_preview_draw_row (GIMP_PREVIEW (cmap_preview), b, 0, y+3, 32);
  }
}

/* one callback for every colormap entry: the built-in tables, the
   custom gradient and the drawables all carry their id as data */
static void gradient_cb(GtkWidget * widget, gpointer data) {
  config.cmap_drawable = GPOINTER_TO_INT(data);
  set_cmap_preview();
  set_flame_preview();
  /*  set_edit_preview(); */
}

static void cmap_menu_ignore(gint32 id, gpointer data) {
  /* gimp_drawable_menu_new () reports its initial choice; the choice
     is made in make_cmap_menu () instead */
}


static gint
cmap_constrain (gint32 image_id, gint32 drawable_id, gpointer data) {

  return ! gimp_drawable_indexed (drawable_id);
}

/* The colormap menu: "Custom Gradient", the built-in tables and then
   every non-indexed drawable.  The drawable entries are taken from
   gimp_drawable_menu_new (), whose option menu can only be appended
   to, so the entries that used to be prepended go in first here.  */
static GtkWidget *
make_cmap_menu(void) {
  static char *names[] =
  {"sunny harvest", "rose", "calcoast09",
   "klee insula-dulcamara",
   "ernst anti-pope", "gris josette"};
  static int good[] = {10, 20, 68, 79, 70, 75};
  int i, n = (sizeof good) / (sizeof *good);
  gint32 save_drawable = config.cmap_drawable;
  GtkWidget *option_menu = gimp_option_menu_new ();
  GtkWidget *drawables;
  int index = 0, history = -1;

#if 0
  gimp_option_menu_append(option_menu, "Black",
			  G_CALLBACK (gradient_cb),
			  GINT_TO_POINTER (black_drawable));
  if (black_drawable == save_drawable)
    history = index;
  index++;
#endif

  gimp_option_menu_append(option_menu, "Custom Gradient",
			  G_CALLBACK (gradient_cb),
			  GINT_TO_POINTER (gradient_drawable));
  if (gradient_drawable == save_drawable)
    history = index;
  index++;

  for (i = n - 1; i >= 0; i--) {
    int d = table_drawable - good[i];
    gimp_option_menu_append(option_menu, names[i],
			    G_CALLBACK (gradient_cb),
			    GINT_TO_POINTER (d));
    if (d == save_drawable)
      history = index;
    index++;
  }

  drawables = gimp_drawable_menu_new(cmap_constrain, cmap_menu_ignore,
				     0, save_drawable);
  g_object_ref_sink(drawables);
  /* an insensitive menu holds only a "none" placeholder */
  if (gtk_widget_get_sensitive(drawables)) {
    GListModel *model = gtk_drop_down_get_model(GTK_DROP_DOWN(drawables));
    guint k, nitems = g_list_model_get_n_items(model);

    for (k = 0; k < nitems; k++) {
      gint32 id =
	GPOINTER_TO_INT(gimp_option_menu_get_item_data(drawables, k));
      gimp_option_menu_append(option_menu,
			      gtk_string_list_get_string(GTK_STRING_LIST(model),
							 k),
			      G_CALLBACK (gradient_cb),
			      GINT_TO_POINTER (id));
      if (id == save_drawable)
	history = index;
      index++;
    }
  }
  g_object_unref(drawables);

  /* the saved drawable may be gone by now: fall back to the first
     entry, and make the colormap match what the menu shows */
  if (history < 0) {
    history = 0;
    config.cmap_drawable = gradient_drawable;
  }
  gimp_option_menu_set_history(option_menu, history);

  return option_menu;
}


static gint dialog(void) {
  GtkWidget *button;
  GtkWidget *table;
  GtkWidget *box;
  GtkWidget *w;
  GtkWidget *frame;
  int row;

  gtk_init();

  dlg = gimp_dialog_new("Flame");
  g_signal_connect(dlg, "destroy",
		   G_CALLBACK (close_callback), NULL);

  gimp_dialog_add_button(dlg, "Ok", G_CALLBACK (ok_callback), dlg, TRUE);

  button = gimp_dialog_add_button(dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped(button, "clicked",
			   G_CALLBACK (gtk_window_destroy), dlg);

  frame = gtk_frame_new("Rendering");
  gimp_container_set_border_width(frame, 10);
  gimp_box_pack_start(gimp_dialog_get_vbox(dlg), frame, TRUE, TRUE, 0);

  box = gimp_vbox_new (FALSE, 5);
  gtk_frame_set_child(GTK_FRAME(frame), box);

  table = gimp_table_new(7, 2, FALSE);
  gimp_box_pack_start(box, table, FALSE, FALSE, 0);

  gimp_container_set_border_width(table, 10);

  gtk_grid_set_row_spacing(GTK_GRID(table), 10);
  gtk_grid_set_column_spacing(GTK_GRID(table), 10);

  row = 1;

  /* this zoom - gamma should redraw flame preview */
  mw_fscale_entry_new(table, "Brightness", 0, 5, 1, 1, 0,
                      0, 1, row, row+1, &config.cp.brightness); row++;
  mw_fscale_entry_new(table, "Contrast", 0, 5, 1, 1, 0,
                      0, 1, row, row+1, &config.cp.contrast); row++;
  mw_fscale_entry_new(table, "Gamma", 1, 5, 1, 1, 0,
                      0, 1, row, row+1, &config.cp.gamma); row++;
  mw_fscale_entry_new(table, "Sample Density", 0.1, 20, 1, 5, 0,
                      0, 1, row, row+1, &config.cp.sample_density); row++;
  mw_iscale_entry_new(table, "Spatial Oversample", 1, 4, 1, 1, 0,
                      0, 1, row, row+1, &config.cp.spatial_oversample); row++;

  mw_fscale_entry_new(table, "Spatial Filter Radius", 0, 4, 1, 1, 0,
                      0, 1, row, row+1, &config.cp.spatial_filter_radius); row++;

  {
    GtkWidget *hbox;
    GtkWidget *option_menu;

    hbox = gimp_hbox_new (FALSE, 5);
    gimp_box_pack_start(box, hbox, FALSE, FALSE, 10);

    w = gtk_label_new("Colormap:");
    gtk_label_set_xalign(GTK_LABEL(w), 0.0);
    gtk_label_set_yalign(GTK_LABEL(w), 0.5);
    gimp_box_pack_start(hbox, w, TRUE, TRUE, 10);

    option_menu = make_cmap_menu();
    gimp_box_pack_start(hbox, option_menu, TRUE, TRUE, 10);

    cmap_preview = gimp_preview_new (GIMP_PREVIEW_COLOR);
    gimp_preview_size (GIMP_PREVIEW (cmap_preview), 32, 32);

    /* GtkPreview centred its image in the space it was given */
    gimp_box_pack_start(hbox, cmap_preview, TRUE, FALSE, 10);
    set_cmap_preview();
  }

  frame = gtk_frame_new("Camera");
  gimp_container_set_border_width(frame, 10);
  gimp_box_pack_start(gimp_dialog_get_vbox(dlg), frame, TRUE, TRUE, 0);

  table = gimp_table_new(4, 2, FALSE);
  gimp_container_set_border_width(table, 10);
  gtk_frame_set_child(GTK_FRAME(frame), table);

  gtk_grid_set_row_spacing(GTK_GRID(table), 10);
  gtk_grid_set_column_spacing(GTK_GRID(table), 10);

  row = 1;

  mw_fscale_entry_new(table, "Zoom", -4, 4, 1, 1, 0,
                      0, 1, row, row+1, &config.cp.zoom); row++;
  mw_fscale_entry_new(table, "X", -2, 2, 0.5, 0.5, 0,
                      0, 1, row, row+1, &config.cp.center[0]); row++;
  mw_fscale_entry_new(table, "Y", -2, 2, 0.5, 0.5, 0,
                      0, 1, row, row+1, &config.cp.center[1]); row++;


  flame_preview = gimp_preview_new (GIMP_PREVIEW_COLOR);
  {
    double aspect = config.cp.width / (double) config.cp.height;
    if (aspect > 1.0) {
      preview_width = preview_size;
      preview_height = preview_size/aspect;
    } else {
      preview_width = preview_size*aspect;
      preview_height = preview_size;
    }
    if (preview_width < 1)
      preview_width = 1;
    if (preview_height < 1)
      preview_height = 1;
  }
  gimp_preview_size (GIMP_PREVIEW (flame_preview), preview_width, preview_height);
  gtk_widget_set_halign (flame_preview, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (flame_preview, GTK_ALIGN_CENTER);

  box = gimp_hbox_new (FALSE, 5);
  gimp_box_pack_start(gimp_dialog_get_vbox(dlg), box, FALSE, FALSE, 0);

  frame = gtk_frame_new("Preview");
  gimp_container_set_border_width(frame, 10);
  gtk_frame_set_child(GTK_FRAME(frame), flame_preview);
  gimp_box_pack_start(box, frame, TRUE, FALSE, 0);

  set_flame_preview();

  {
    GtkWidget *vbox = gimp_vbox_new (TRUE, 5);
    gimp_box_pack_start(box, vbox, TRUE, FALSE, 0);

    button = gtk_button_new_with_label("Shape Edit");
    g_signal_connect(button, "clicked",
		     G_CALLBACK (edit_callback), NULL);
    gimp_box_pack_start(vbox, button, TRUE, FALSE, 10);

    button = gtk_button_new_with_label("Load");
    g_signal_connect(button, "clicked",
		     G_CALLBACK (load_callback), NULL);
    gimp_box_pack_start(vbox, button, TRUE, FALSE, 10);

    button = gtk_button_new_with_label("Store");
    g_signal_connect(button, "clicked",
		     G_CALLBACK (store_callback), NULL);
    gimp_box_pack_start(vbox, button, TRUE, FALSE, 10);
  }

  gtk_window_present(GTK_WINDOW(dlg));
  gimp_main_loop_run();

  return run_flag;
}
