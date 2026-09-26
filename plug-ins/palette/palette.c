/* palette.c GIMP plug-in to export palettes */

/* This code should really be a GIMP built-in IMHO, but with a feature
   freeze in place I think this plug-in is a big win for 1.0 */

/* All the work here so far is owned by Nick Lamb <njl195@ecs.soton.ac.uk>,
   and is distributed under the same license as the core GIMP components
   (ie GNU General Public License) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#define PLUG_IN_NAME "plug_in_export_palette"

/* Declare plug-in functions.  */

static void query (void);
static void run (char *name, int nparams, GParam *param, int *nreturn_vals,
                 GParam **return_vals);

GPlugInInfo PLUG_IN_INFO = { NULL, NULL, query, run, };

static int running= FALSE;
static gchar *filename; /* Filename of palette file */

MAIN ()

static void query () {
  static GParamDef args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image" },
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
    { PARAM_STRING, "filename", "Name of palette" }
  };
  static GParamDef *return_vals = NULL;

  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0;

  gimp_install_procedure (PLUG_IN_NAME,
                          "Makes a GIMP palette from an indexed image",
                          "Apply this to an INDEXED image to create a GIMP "
                          "palette with the same colours. This allows you "
                          "to later convert any image to that same "
                          "palette, by picking it explicitly in the RGB "
                          "to INDEXED conversion dialog.",
                          "Nick Lamb",
                          "Nick Lamb <njl195@ecs.soton.ac.uk>",
                          "February 1998",
                          "<Image>/Image/Save palette",
			  "INDEXED*",
                          PROC_PLUG_IN,
                          nargs, nreturn_vals, args, return_vals);
}

static GStatusType export_palette(gint32 image_id);
static void write_palette(FILE *fp, int count, guchar* rgb);
static int palette_dialog(void);

/* AFAIK This should be part of libgimp (sigh) */

static gchar* gimp_gimprc_query(gchar *key) {
  GParam *return_vals;
  int nreturn_vals;
  gchar *value;

  return_vals = gimp_run_procedure ("gimp_gimprc_query", &nreturn_vals,
                                    PARAM_STRING, key, PARAM_END);

  value= g_strdup(return_vals[1].data.d_string);
  gimp_destroy_params(return_vals, nreturn_vals);
  return value;
}

/* Plug-in implementation */

static void run (char *name, int nparams, GParam *param, int *nreturn_vals,
     GParam **return_vals) {
  static GParam values[1];
  GRunModeType run_mode;
  int length= 0;

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;
  values[0].type = PARAM_STATUS;
  values[0].data.d_status = STATUS_CALLING_ERROR;

  if (strcmp (name, PLUG_IN_NAME) == 0) {
    switch (run_mode) {
      case RUN_INTERACTIVE:
        gimp_get_data(PLUG_IN_NAME, &length);
        filename= g_malloc(length + 1);
        gimp_get_data(PLUG_IN_NAME ":filename", filename);
        filename[length]= '\0';
        if (palette_dialog()) {
          length= strlen(filename);
          gimp_set_data(PLUG_IN_NAME, &length, sizeof(length));
          gimp_set_data(PLUG_IN_NAME ":filename", filename, length);
        } else {
          values[0].data.d_status = STATUS_EXECUTION_ERROR;
          return;
        }
	break;

      case RUN_NONINTERACTIVE:
        if (nparams == 4) {
          filename= g_strdup(param[3].data.d_string);
        } else {
          values[0].data.d_status = STATUS_CALLING_ERROR;
          return;
        }
        break;

      case RUN_WITH_LAST_VALS:
        gimp_get_data(PLUG_IN_NAME, &length);
        filename= g_malloc(length + 1);
        gimp_get_data(PLUG_IN_NAME ":filename", filename);
        filename[length]= '\0';
        break;

      default:
        break;
    }

    values[0].data.d_status = export_palette(param[1].data.d_int32);
  } else {
    values[0].data.d_status = STATUS_EXECUTION_ERROR;
  }
}

static GStatusType export_palette(gint32 image_id) {
  FILE *fp;
  gint count;
  guchar *palette;

  if (gimp_image_base_type(image_id) != 2)
    return STATUS_EXECUTION_ERROR;

  if ((fp= g_fopen(filename, "w")) == NULL)
    return STATUS_EXECUTION_ERROR;

  fprintf(fp, "GIMP Palette\n# Added by the Export Palette plug-in\n");
  palette= gimp_image_get_cmap(image_id, &count);
  write_palette(fp, count, palette);
  fclose(fp);
  g_free(palette);
  return STATUS_SUCCESS;
}

static void write_palette(FILE *fp, int count, guchar* rgb) {

  while (count > 0) {
    fprintf(fp, "%d %d %d\tUnknown\n", rgb[0], rgb[1], rgb[2]);
    rgb+= 3; --count;
  }
}

static void palette_file_chosen(const gchar *name, gpointer data) {
    if (name) {
      g_free(filename);
      filename= g_strdup(name);
      running= TRUE;
    }
    gimp_main_loop_quit();
}

static int palette_dialog(void) {
  gchar *directory, *data;

  gtk_init ();

  data= gimp_gimprc_query("gimp_dir");
  directory= g_build_filename(data, "palettes", NULL);
  g_free(data);

  gimp_file_dialog_save(NULL, "Export GIMP Palette",
                        (filename[0] == 0) ? directory : filename,
                        palette_file_chosen, NULL);
  gimp_main_loop_run();

  g_free(directory);
  return running;
}
