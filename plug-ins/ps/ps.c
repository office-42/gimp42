/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * PostScript file plugin
 * PostScript writing and GhostScript interfacing code
 * Copyright (C) 1997-98 Peter Kirchgessner
 * (email: peter@kirchgessner.net, WWW: http://www.kirchgessner.net)
 *
 * Added controls for TextAlphaBits and GraphicsAlphaBits
 *   George White <aa056@chebucto.ns.ca>
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
 *
 */

/* Event history:
 * V 0.90, PK, 28-Mar-97: Creation.
 * V 0.91, PK, 03-Apr-97: Clip everything outside BoundingBox.
 *             24-Apr-97: Multi page read support.
 * V 1.00, PK, 30-Apr-97: PDF support.
 * V 1.01, PK, 05-Oct-97: Parse rc-file.
 * V 1.02, GW, 09-Oct-97: Antialiasing support.
 *         PK, 11-Oct-97: No progress bars when running non-interactive.
 *                        New procedure file_ps_load_setargs to set
 *                        load-arguments non-interactively.
 *                        If GS_OPTIONS are not set, use at least "-dSAFER"
 * V 1.03, nn, 20-Dec-97: Initialize some variables
 * V 1.04, PK, 20-Dec-97: Add Encapsulated PostScript output and preview
 * V 1.05, PK, 21-Sep-98: Write b/w-images (indexed) using image-operator
 * V 1.06, PK, 22-Dec-98: Fix problem with writing color PS files.
 *                        Ghostview may hang when displaying the files.
 */
#define VERSIO                                               1.06
static char dversio[] =                                    "v1.06  22-Dec-98";
static char ident[] = "@(#) GIMP PostScript/PDF file-plugin v1.06  22-Dec-98";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define STR_LENGTH 64

/* Load info */
typedef struct
{
  guint resolution;     /* resolution (dpi) at which to run ghostscript */
  guint width, height;  /* desired size (ghostscript may ignore this) */
  gint use_bbox;        /* 0: use width/height, 1: try to use BoundingBox */
  char pages[STR_LENGTH];/* Pages to load (eg.: 1,3,5-7) */
  gint pnm_type;        /* 4: pbm, 5: pgm, 6: ppm, 7: automatic */
  gint textalpha;       /* antialiasing: 1,2, or 4 TextAlphaBits */
  gint graphicsalpha;   /* antialiasing: 1,2, or 4 GraphicsAlphaBits */
} PSLoadVals;

typedef struct
{
  gint  run;  /*  run  */
} PSLoadInterface;

static PSLoadVals plvals =
{
  100,                  /* 100 dpi */
  826, 1170,            /* default width/height (A4) */
  1,                    /* try to use BoundingBox */
  "1-99",               /* pages to load */
  6,                    /* use ppm (colour) */
  1,                    /* dont use text antialiasing */
  1                     /* dont use graphics antialiasing */
};

static PSLoadInterface plint =
{
  FALSE     /* run */
};


/* Save info  */
typedef struct
{
  gdouble width, height;      /* Size of image */
  gdouble x_offset, y_offset; /* Offset to image on page */
  gint unit_mm;               /* Unit of measure (0: inch, 1: mm) */
  gint keep_ratio;            /* Keep aspect ratio */
  gint rotate;                /* Rotation (0, 90, 180, 270) */
  gint eps;                   /* Encapsulated PostScript flag */
  gint preview;               /* Preview Flag */
  gint preview_size;          /* Preview size */
} PSSaveVals;

typedef struct
{
  gint  run;  /*  run  */
} PSSaveInterface;

static PSSaveVals psvals =
{
  287.0, 200.0,   /* Image size (A4) */
  5.0, 5.0,       /* Offset */
  1,              /* Unit is mm */
  1,              /* Keep edge ratio */
  90,             /* Rotate */
  0,              /* Encapsulated PostScript flag */
  0,              /* Preview flag */
  256             /* Preview size */
};

static PSSaveInterface psint =
{
  FALSE     /* run */
};


/* Declare some local functions.
 */
static void   query      (void);
static void   run        (char    *name,
                          int      nparams,
                          GParam  *param,
                          int     *nreturn_vals,
                          GParam **return_vals);

static gint32 load_image (char *filename);
static gint   save_image (char *filename,
                          gint32  image_ID,
                          gint32  drawable_ID);

static gint save_gray  (FILE *ofp,
                        gint32 image_ID,
                        gint32 drawable_ID);
static gint save_bw    (FILE *ofp,
                        gint32 image_ID,
                        gint32 drawable_ID);
static gint save_index (FILE *ofp,
                        gint32 image_ID,
                        gint32 drawable_ID);
static gint save_rgb   (FILE *ofp,
                        gint32 image_ID,
                        gint32 drawable_ID);

static gint32 create_new_image (char *filename, guint pagenum,
                guint width, guint height,
                GImageType type, gint32 *layer_ID, GDrawable **drawable,
                GPixelRgn *pixel_rgn);

static void   check_load_vals (void);
static void   check_save_vals (void);

static char  *ftoa (char *format, double r);

static gint   page_in_list (char *list, guint pagenum);

static gint   get_bbox (char *filename,
                        int *x0, int *y0, int *x1, int *y1);

static FILE  *ps_open (char *filename,
                       const PSLoadVals *loadopt,
                       int *llx, int* lly, int* urx, int* ury);

static void   ps_close (FILE *ifp);

static gint32 skip_ps (FILE *ifp);

static gint32 load_ps (char *filename,
                       guint pagenum,
                       FILE *ifp,
                       int llx, int lly, int urx, int ury);

static void save_ps_header (FILE *ofp,
                            char *filename);
static void save_ps_setup (FILE *ofp,
                           gint32 drawable_ID,
                           int width,
                           int height,
                           int bpp);
static void save_ps_trailer (FILE *ofp);
static void save_ps_preview (FILE *ofp,
                             gint32 drawable_ID);
static void dither_grey (unsigned char *grey,
                         unsigned char *bw,
                         int npix,
                         int linecount);


/* Dialog-handling */
typedef struct
{
  GtkWidget *dialog;
  GtkWidget *entry[4];
  int use_bbox;
  int dataformat[4];
  int textalphabits[3];
  int graphicsalphabits[3];
} LoadDialogVals;

static gint   load_dialog              (void);
static void   load_close_callback      (GtkWidget *widget,
                                        gpointer   data);
static void   load_ok_callback         (GtkWidget *widget,
                                        gpointer   data);
static void   load_toggle_update       (GtkWidget *widget,
                                        gpointer   data);

typedef struct
{
  GtkWidget *dialog;
  GtkWidget *entry[4];
  GtkWidget *psize_entry;
  int keep_ratio;
  int unit[2];
  int rot[4];
  int eps;
  int preview;
  int preview_size;
} SaveDialogVals;

static gint   save_dialog              (void);
static void   save_close_callback      (GtkWidget *widget,
                                        gpointer   data);
static void   save_ok_callback         (GtkWidget *widget,
                                        gpointer   data);
static void   save_toggle_update       (GtkWidget *widget,
                                        gpointer   data);
static void   save_mm_toggle_update    (GtkWidget *widget,
                                        gpointer   data);


GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};


/* The run mode */
static GRunModeType l_run_mode;


MAIN ()


static void
query (void)

{
  static GParamDef load_args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_STRING, "filename", "The name of the file to load" },
    { PARAM_STRING, "raw_filename", "The name of the file to load" }
  };
  static GParamDef load_return_vals[] =
  {
    { PARAM_IMAGE, "image", "Output image" },
  };
  static int nload_args = sizeof (load_args) / sizeof (load_args[0]);
  static int nload_return_vals = sizeof (load_return_vals)
                               / sizeof (load_return_vals[0]);

  static GParamDef set_load_args[] =
  {
    { PARAM_INT32, "resolution", "Resolution to interprete image (dpi)" },
    { PARAM_INT32, "width", "Desired width" },
    { PARAM_INT32, "height", "Desired height" },
    { PARAM_INT32, "check_bbox", "0: Use width/height, 1: Use BoundingBox" },
    { PARAM_STRING, "pages", "Pages to load (e.g.: 1,3,5-7)" },
    { PARAM_INT32, "coloring", "4: b/w, 5: grey, 6: colour image, 7: automatic" },
    { PARAM_INT32, "TextAlphaBits", "1, 2, or 4" },
    { PARAM_INT32, "GraphicsAlphaBits", "1, 2, or 4" }
  };
  static int nset_load_args = sizeof (set_load_args) / sizeof (set_load_args[0]);

  static GParamDef save_args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image" },
    { PARAM_DRAWABLE, "drawable", "Drawable to save" },
    { PARAM_STRING, "filename", "The name of the file to save the image in" },
    { PARAM_STRING, "raw_filename",
            "The name of the file to save the image in" },
    { PARAM_FLOAT, "width", "Width of the image in PostScript file" },
    { PARAM_FLOAT, "height", "Height of image in PostScript file" },
    { PARAM_FLOAT, "x_offset", "X-offset to image from lower left corner" },
    { PARAM_FLOAT, "y_offset", "Y-offset to image from lower left corner" },
    { PARAM_INT32, "unit", "Unit for width/height/offset. 0: inches, 1: millimeters" },
    { PARAM_INT32, "keep_ratio", "0: use width/height, 1: keep aspect ratio" },
    { PARAM_INT32, "rotation", "0, 90, 180, 270" },
    { PARAM_INT32, "eps_flag", "0: PostScript, 1: Encapsulated PostScript" },
    { PARAM_INT32, "preview", "0: no preview, >0: max. size of preview" }
  };
  static int nsave_args = sizeof (save_args) / sizeof (save_args[0]);

  gimp_install_procedure ("file_ps_load",
                          "load file of PostScript/PDF file format",
                          "load file of PostScript/PDF file format",
                          "Peter Kirchgessner <pkirchg@aol.com>",
                          "Peter Kirchgessner",
                          dversio,
                          "<Load>/PostScript",
                          NULL,
                          PROC_PLUG_IN,
                          nload_args, nload_return_vals,
                          load_args, load_return_vals);

  gimp_install_procedure ("file_ps_load_setargs",
                          "set additional parameters for procedure file_ps_load",
                          "set additional parameters for procedure file_ps_load",
                          "Peter Kirchgessner <pkirchg@aol.com>",
                          "Peter Kirchgessner",
                          dversio,
                          NULL,
                          NULL,
                          PROC_PLUG_IN,
                          nset_load_args, 0,
                          set_load_args, NULL);

  gimp_install_procedure ("file_ps_save",
                          "save file in PostScript file format",
                          "PostScript saving handles all image types except \
those with alpha channels.",
                          "Peter Kirchgessner <pkirchg@aol.com>",
                          "Peter Kirchgessner",
                          dversio,
                          "<Save>/PostScript",
                          "RGB*, GRAY*, INDEXED*",
                          PROC_PLUG_IN,
                          nsave_args, 0,
                          save_args, NULL);

  /* Register file plugin by plugin name and handable extensions */
  gimp_register_magic_load_handler ("file_ps_load", "ps,eps,pdf", "",
                                    "0,string,%!,0,string,%PDF");
  gimp_register_save_handler ("file_ps_save", "ps,eps", "");
}


static void
run (char    *name,
     int      nparams,
     GParam  *param,
     int     *nreturn_vals,
     GParam **return_vals)

{
  static GParam values[2];
  GRunModeType run_mode;
  GStatusType status = STATUS_SUCCESS;
  gint32 image_ID = -1;
  int k;

  l_run_mode = run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;
  values[0].type = PARAM_STATUS;
  values[0].data.d_status = STATUS_CALLING_ERROR;

  if (strcmp (name, "file_ps_load") == 0)
    {
      *nreturn_vals = 2;
      values[1].type = PARAM_IMAGE;
      values[1].data.d_image = -1;

      switch (run_mode)
      {
        case RUN_INTERACTIVE:
          /*  Possibly retrieve data  */
          gimp_get_data ("file_ps_load", &plvals);

          if (! load_dialog ())
	    {
	      values[0].data.d_status = STATUS_CANCEL;
	      return;
	    }
          break;

        case RUN_NONINTERACTIVE:
          /*  Make sure all the arguments are there!  */
          if (nparams != 3)
            status = STATUS_CALLING_ERROR;
          else    /* Get additional interpretation arguments */
            gimp_get_data ("file_ps_load", &plvals);
          break;

        case RUN_WITH_LAST_VALS:
          /* Possibly retrieve data */
          gimp_get_data ("file_ps_load", &plvals);
          break;

        default:
          break;
      }
      if (status == STATUS_SUCCESS)
      {
        check_load_vals ();
        image_ID = load_image (param[1].data.d_string);

        status = (image_ID != -1) ? STATUS_SUCCESS : STATUS_EXECUTION_ERROR;

        /*  Store plvals data  */
        if (status == STATUS_SUCCESS)
          gimp_set_data ("file_ps_load", &plvals, sizeof (PSLoadVals));
      }
      values[0].data.d_status = status;
      values[1].data.d_image = image_ID;
    }
  else if (strcmp (name, "file_ps_save") == 0)
    {
      switch (run_mode)
        {
        case RUN_INTERACTIVE:
          /*  Possibly retrieve data  */
          gimp_get_data ("file_ps_save", &psvals);

          /* About to save an EPS-file ? Switch on eps-flag in dialog */
          k = strlen (param[3].data.d_string);
          if ((k >= 4) && (strcmp (param[3].data.d_string+k-4, ".eps") == 0))
            psvals.eps = 1;

          /*  First acquire information with a dialog  */
          if (! save_dialog ())
	    {
	      values[0].data.d_status = STATUS_CANCEL;
	      return;
	    }
          break;

        case RUN_NONINTERACTIVE:
          /*  Make sure all the arguments are there!  */
          if (nparams != 14)
          {
            status = STATUS_CALLING_ERROR;
          }
          else
          {
            psvals.width = param[5].data.d_float;
            psvals.height = param[6].data.d_float;
            psvals.x_offset = param[7].data.d_float;
            psvals.y_offset = param[8].data.d_float;
            psvals.unit_mm = (param[9].data.d_int32 != 0);
            psvals.keep_ratio = (param[10].data.d_int32 != 0);
            psvals.rotate = param[11].data.d_int32;
            psvals.eps = param[12].data.d_int32;
            psvals.preview = (param[13].data.d_int32 != 0);
            psvals.preview_size = param[13].data.d_int32;
          }
          break;

        case RUN_WITH_LAST_VALS:
          /*  Possibly retrieve data  */
          gimp_get_data ("file_ps_save", &psvals);
          break;

        default:
          break;
        }

      if (status == STATUS_SUCCESS)
      {
        check_save_vals ();
        if (save_image (param[3].data.d_string, param[1].data.d_int32,
                        param[2].data.d_int32))
        {
          /*  Store psvals data  */
          gimp_set_data ("file_ps_save", &psvals, sizeof (PSSaveVals));
        }
        else
        {
          status = STATUS_EXECUTION_ERROR;
        }
      }
      values[0].data.d_status = status;
    }
  else if (strcmp (name, "file_ps_load_setargs") == 0)
    {
      /*  Make sure all the arguments are there!  */
      if (nparams != 8)
      {
        status = STATUS_CALLING_ERROR;
      }
      else
      {
        plvals.resolution = param[0].data.d_int32;
        plvals.width = param[1].data.d_int32;
        plvals.height = param[2].data.d_int32;
        plvals.use_bbox = param[3].data.d_int32;
        if (param[4].data.d_string != NULL)
          strncpy (plvals.pages,param[4].data.d_string,sizeof (plvals.pages));
        else
          plvals.pages[0] = '\0';
        plvals.pages[sizeof (plvals.pages) - 1] = '\0';
        plvals.pnm_type = param[5].data.d_int32;
        plvals.textalpha = param[6].data.d_int32;
        plvals.graphicsalpha = param[7].data.d_int32;
        check_load_vals ();
        gimp_set_data ("file_ps_load", &plvals, sizeof (PSLoadVals));
      }
      values[0].data.d_status = status;
    }
}


static gint32
load_image (char *filename)

{
 gint32 image_ID, *image_list, *nl;
 guint page_count;
 FILE *ifp;
 char *temp,*format;
 int  llx, lly, urx, ury;
 int  k, n_images, max_images, max_pagenum;

#ifdef PS_DEBUG
 printf ("load_image:\n resolution = %d\n", plvals.resolution);
 printf (" %dx%d pixels\n", plvals.width, plvals.height);
 printf (" BoundingBox: %d\n", plvals.use_bbox);
 printf (" Colouring: %d\n", plvals.pnm_type);
 printf (" TextAlphaBits: %d\n", plvals.textalpha);
 printf (" GraphicsAlphaBits: %d\n", plvals.graphicsalpha);
#endif

 /* Try to see if PostScript file is available */
 ifp = g_fopen (filename, "rb");
 if (ifp == NULL)
 {
   g_message ("can't open file for reading");
   return (-1);
 }
 fclose (ifp);

 if (l_run_mode != RUN_NONINTERACTIVE)
 {
   format = "Interpreting and Loading %s:";
   temp = g_malloc (strlen (format) + strlen (filename) + 5);
   sprintf (temp, format, filename);
   gimp_progress_init (temp);
   g_free (temp);
 }

 ifp = ps_open (filename, &plvals, &llx, &lly, &urx, &ury);
 if (!ifp)
 {
   g_message ("can't interprete file");
   return (-1);
 }

 image_list = (gint32 *)g_malloc (10 * sizeof (gint32));
 if (image_list == NULL)
 {
   g_message ("out of memory");
   return (-1);
 }
 n_images = 0;
 max_images = 10;

 max_pagenum = 9999;  /* Try to get the maximum pagenumber to read */
 if (!page_in_list (plvals.pages, max_pagenum)) /* Is there a limit in list ? */
 {
   max_pagenum = -1;
   for (temp = plvals.pages; *temp != '\0'; temp++)
   {
     if ((*temp < '0') || (*temp > '9')) continue; /* Search next digit */
     sscanf (temp, "%d", &k);
     if (k > max_pagenum) max_pagenum = k;
     while ((*temp >= '0') && (*temp <= '9')) temp++;
     temp--;
   }
   if (max_pagenum < 1) max_pagenum = 9999;
 }

 /* Load all images */
 for (page_count = 1; page_count <= max_pagenum; page_count++)
 {
   if (page_in_list (plvals.pages, page_count))
   {
     image_ID = load_ps (filename, page_count, ifp, llx, lly, urx, ury);
     if (image_ID == -1) break;
     if (n_images == max_images)
     {
       nl = (gint32 *)g_realloc (image_list, (max_images+10)*sizeof (gint32));
       if (nl == NULL) break;
       image_list = nl;
       max_images += 10;
     }
     image_list[n_images++] = image_ID;
   }
   else  /* Skip an image */
   {
     image_ID = skip_ps (ifp);
     if (image_ID == -1) break;
   }
 }

 ps_close (ifp);

 /* Display images in reverse order. The last will be displayed by GIMP itself*/
 if (l_run_mode != RUN_NONINTERACTIVE)
 {
   for (k = n_images-1; k >= 1; k--)
     gimp_display_new (image_list[k]);
 }

 image_ID = (n_images > 0) ? image_list[0] : -1;
 g_free (image_list);

 return (image_ID);
}


static gint
save_image (char *filename,
            gint32  image_ID,
            gint32  drawable_ID)

{
  FILE* ofp;
  GDrawableType drawable_type;
  gint retval;
  char *temp = ident; /* Just to satisfy lint/gcc */

  /* initialize */

  retval = 0;

  drawable_type = gimp_drawable_type (drawable_ID);

  /*  Make sure we're not saving an image with an alpha channel  */
  if (gimp_drawable_has_alpha (drawable_ID))
  {
    g_message ("PostScript save cannot handle images with alpha channels");
    return FALSE;
  }

  switch (drawable_type)
  {
    case INDEXED_IMAGE:
    case GRAY_IMAGE:
    case RGB_IMAGE:
      break;
    default:
      g_message ("cannot operate on unknown image types");
      return (FALSE);
      break;
  }

  /* Open the output file. */
  ofp = g_fopen (filename, "wb");
  if (!ofp)
  {
    g_message ("cant open file for writing");
    return (FALSE);
  }

  if (l_run_mode != RUN_NONINTERACTIVE)
  {
    temp = g_malloc (strlen (filename) + 11);
    sprintf (temp, "Saving %s:", filename);
    gimp_progress_init (temp);
    g_free (temp);
  }

  save_ps_header (ofp, filename);

  if (drawable_type == GRAY_IMAGE)
    retval = save_gray (ofp,image_ID, drawable_ID);
  else if (drawable_type == INDEXED_IMAGE)
    retval = save_index (ofp,image_ID, drawable_ID);
  else if (drawable_type == RGB_IMAGE)
    retval = save_rgb (ofp,image_ID, drawable_ID);

  save_ps_trailer (ofp);

  fclose (ofp);

  return (retval);
}


/* Check (and correct) the load values plvals */
static void
check_load_vals (void)

{
  if (plvals.resolution < 5) plvals.resolution = 5;
  else if (plvals.resolution > 1440) plvals.resolution = 1440;

  if (plvals.width < 2) plvals.width = 2;
  if (plvals.height < 2) plvals.height = 2;
  plvals.use_bbox = (plvals.use_bbox != 0);
  if (plvals.pages[0] == '\0')
    strcpy (plvals.pages, "1-99");
  if ((plvals.pnm_type < 4) || (plvals.pnm_type > 7))
    plvals.pnm_type = 6;
  if (   (plvals.textalpha != 1) && (plvals.textalpha != 2)
      && (plvals.textalpha != 4))
    plvals.textalpha = 1;
  if (   (plvals.graphicsalpha != 1) && (plvals.graphicsalpha != 2)
      && (plvals.graphicsalpha != 4))
    plvals.graphicsalpha = 1;
}


/* Check (and correct) the save values psvals */
static void
check_save_vals (void)

{int i;

 i = psvals.rotate;
 if ((i != 0) && (i != 90) && (i != 180) && (i != 270))
   psvals.rotate = 90;
 if (psvals.preview_size <= 0) psvals.preview = 0;
}


/* Convert float to ascii for use in labels (cuts off trailing blanks). */
/* The pointer returned is only valid up to the next call of the function. */
static char *ftoa (char *format,
                   double r)

{static char buffer[32];
 register int n;

 g_snprintf (buffer, sizeof (buffer), format, r);
 n = strlen (buffer)-1;
 while ((n >= 0) && (buffer[n] == ' '))
   buffer[n--] = '\0';
 return (buffer);
}


/* Check if a page is in a given list */
static gint
page_in_list (char *list,
              guint page_num)

{char tmplist[STR_LENGTH], *c0, *c1;
 int state, start_num, end_num;
#define READ_STARTNUM  0
#define READ_ENDNUM    1
#define CHK_LIST(a,b,c) {int low=(a),high=(b),swp; \
  if ((low>0) && (high>0)) { \
  if (low>high) {swp=low; low=high; high=swp;} \
  if ((low<=(c))&&(high>=(c))) return (1); } }

 if ((list == NULL) || (*list == '\0')) return (1);

 strncpy (tmplist, list, STR_LENGTH);
 tmplist[STR_LENGTH-1] = '\0';

 c0 = c1 = tmplist;
 while (*c1)    /* Remove all whitespace and break on unsupported characters */
 {
   if ((*c1 >= '0') && (*c1 <= '9'))
   {
     *(c0++) = *c1;
   }
   else if ((*c1 == '-') || (*c1 == ','))
   { /* Try to remove double occurances of these characters */
     if (c0 == tmplist)
     {
       *(c0++) = *c1;
     }
     else
     {
       if (*(c0-1) != *c1)
         *(c0++) = *c1;
     }
   }
   else break;
   c1++;
 }
 if (c0 == tmplist) return (1);
 *c0 = '\0';

 /* Now we have a comma separated list like 1-4-1,-3,1- */

 start_num = end_num = -1;
 state = READ_STARTNUM;
 for (c0 = tmplist; *c0 != '\0'; c0++)
 {
   switch (state)
   {
     case READ_STARTNUM:
       if (*c0 == ',')
       {
         if ((start_num > 0) && (start_num == (int)page_num)) return (-1);
         start_num = -1;
       }
       else if (*c0 == '-')
       {
         if (start_num < 0) start_num = 1;
         state = READ_ENDNUM;
       }
       else /* '0' - '9' */
       {
         if (start_num < 0) start_num = 0;
         start_num *= 10;
         start_num += *c0 - '0';
       }
       break;

     case READ_ENDNUM:
       if (*c0 == ',')
       {
         if (end_num < 0) end_num = 9999;
         CHK_LIST (start_num, end_num, (int)page_num);
         start_num = end_num = -1;
         state = READ_STARTNUM;
       }
       else if (*c0 == '-')
       {
         CHK_LIST (start_num, end_num, (int)page_num);
         start_num = end_num;
         end_num = -1;
       }
       else /* '0' - '9' */
       {
         if (end_num < 0) end_num = 0;
         end_num *= 10;
         end_num += *c0 - '0';
       }
       break;
   }
 }
 if (state == READ_STARTNUM)
 {
   if (start_num > 0)
    return (start_num == (int)page_num);
 }
 else
 {
   if (end_num < 0) end_num = 9999;
   CHK_LIST (start_num, end_num, (int)page_num);
 }
 return (0);
#undef CHK_LIST
}


/* Get the BoundingBox of a PostScript file. On success, 0 is returned. */
/* On failure, -1 is returned. */
static gint
get_bbox (char *filename,
          int *x0,
          int *y0,
          int *x1,
          int *y1)

{char line[1024], *src;
 FILE *ifp;
 int retval = -1;

 ifp = g_fopen (filename, "rb");
 if (ifp == NULL) return (-1);

 for (;;)
 {
   if (fgets (line, sizeof (line)-1, ifp) == NULL) break;
   if ((line[0] != '%') || (line[1] != '%')) continue;
   src = &(line[2]);
   while ((*src == ' ') || (*src == '\t')) src++;
   if (strncmp (src, "BoundingBox", 11) != 0) continue;
   src += 11;
   while ((*src == ' ') || (*src == '\t') || (*src == ':')) src++;
   if (strncmp (src, "(atend)", 7) == 0) continue;
   if (sscanf (src, "%d%d%d%d", x0, y0, x1, y1) == 4)
     retval = 0;
   break;
 }
 fclose (ifp);
 return (retval);
}


/* Name of the PNM-file Ghostscript renders into; removed by ps_close () */
static gchar *ps_pnmfile = NULL;


/* Find the Ghostscript interpreter. GS_PROG overrides the search. */
static gchar *
ps_find_ghostscript (void)

{static const char *names[] =
 {
#ifdef G_OS_WIN32
   "gswin64c", "gswin32c", "gs"
#else
   "gs", "gswin64c", "gswin32c"
#endif
 };
 const char *gs_prog;
 gchar *path;
 guint k;

 gs_prog = g_getenv ("GS_PROG");
 if (gs_prog != NULL)
 {
   path = g_find_program_in_path (gs_prog);
   return (path ? path : g_strdup (gs_prog));
 }

 for (k = 0; k < G_N_ELEMENTS (names); k++)
 {
   path = g_find_program_in_path (names[k]);
   if (path != NULL) return (path);
 }
 return (NULL);
}


/* Open the PostScript file. On failure, NULL is returned. */
/* The filepointer returned will give a PNM-file generated */
/* by the PostScript-interpreter. */
static FILE *
ps_open (char *filename,
         const PSLoadVals *loadopt,
         int *llx,
         int *lly,
         int *urx,
         int *ury)

{gchar *gs, *driver, *output, *resopt, *infile;
 const char *gs_opts;
 GPtrArray *argv;
 GSubprocess *proc;
 GError *error = NULL;
 FILE *ifp;
 int width, height, resolution;
 int x0, y0, x1, y1;
 int is_pdf, fd;
 char TextAlphaBits[64], GraphicsAlphaBits[64], geometry[32];

 resolution = loadopt->resolution;
 *llx = *lly = 0;
 width = loadopt->width;
 height = loadopt->height;
 *urx = width-1;
 *ury = height-1;

 /* Check if the file is a PDF. For PDF, we cant set geometry */
 is_pdf = 0;
 ifp = g_fopen (filename, "rb");
 if (ifp != NULL)
 {char hdr[4];

   is_pdf = (   (fread (hdr, 1, 4, ifp) == 4)
             && (strncmp (hdr, "%PDF", 4) == 0));
   fclose (ifp);
 }

 if ((!is_pdf) && (loadopt->use_bbox))    /* Try the BoundingBox ? */
 {
   if (   (get_bbox (filename, &x0, &y0, &x1, &y1) == 0)
       && (x0 >= 0) && (y0 >= 0) && (x1 > x0) && (y1 > y0))
   {
     *llx = (int)((x0/72.0) * resolution + 0.01);
     *lly = (int)((y0/72.0) * resolution + 0.01);
     *urx = (int)((x1/72.0) * resolution + 0.01);
     *ury = (int)((y1/72.0) * resolution + 0.01);
     width = *urx + 1;
     height = *ury + 1;
   }
 }
 if (loadopt->pnm_type == 4) driver = "pbmraw";
 else if (loadopt->pnm_type == 5) driver = "pgmraw";
 else if (loadopt->pnm_type == 7) driver = "pnmraw";
 else driver = "ppmraw";

 gs = ps_find_ghostscript ();
 if (gs == NULL)
 {
   g_message ("PostScript: can't find Ghostscript (gs, gswin64c or gswin32c)\n"
              "on the PATH. Install Ghostscript or set GS_PROG.");
   return (NULL);
 }

 /* A PostScript file is a program: without -dSAFER it can read, write */
 /* and delete files, and run commands through %pipe%.  -dSAFER is     */
 /* always given, whatever GS_OPTIONS (which Ghostscript reads itself) */
 /* holds.                                                              */
 gs_opts = "-dSAFER";

 /* Ghostscript renders into a temporary PNM-file that is read back */
 /* when it has finished. ps_close() removes it again.              */
 fd = g_file_open_tmp ("gimp-ps-XXXXXX.pnm", &ps_pnmfile, &error);
 if (fd == -1)
 {
   g_message ("PostScript: can't create temporary file: %s", error->message);
   g_error_free (error);
   g_free (gs);
   return (NULL);
 }
 g_close (fd, NULL);

 TextAlphaBits[0] = GraphicsAlphaBits[0] = geometry[0] = '\0';

 /* Antialiasing not available for PBM-device */
 if ((loadopt->pnm_type != 4) && (loadopt->textalpha != 1))
   g_snprintf (TextAlphaBits, sizeof (TextAlphaBits),
               "-dTextAlphaBits=%d", (int)loadopt->textalpha);

 if ((loadopt->pnm_type != 4) && (loadopt->graphicsalpha != 1))
   g_snprintf (GraphicsAlphaBits, sizeof (GraphicsAlphaBits),
               "-dGraphicsAlphaBits=%d", (int)loadopt->graphicsalpha);

 if (!is_pdf)    /* For PDF, we cant set geometry */
   g_snprintf (geometry, sizeof (geometry), "-g%dx%d", width, height);

 driver = g_strdup_printf ("-sDEVICE=%s", driver);
 resopt = g_strdup_printf ("-r%d", resolution);
 /* '%' in OutputFile is a page number format: double it */
 {gchar **parts = g_strsplit (ps_pnmfile, "%", -1);
  gchar *escaped = g_strjoinv ("%%", parts);

   output = g_strdup_printf ("-sOutputFile=%s", escaped);
   g_free (escaped);
   g_strfreev (parts);
 }
 /* A file name starting with '-' or '@' would be taken as an option or */
 /* a response file by Ghostscript.                                     */
 if ((filename[0] == '-') || (filename[0] == '@'))
   infile = g_build_filename (".", filename, NULL);
 else
   infile = g_strdup (filename);

 argv = g_ptr_array_new ();
 g_ptr_array_add (argv, gs);
 g_ptr_array_add (argv, driver);
 g_ptr_array_add (argv, resopt);
 if (geometry[0]) g_ptr_array_add (argv, geometry);
 if (TextAlphaBits[0]) g_ptr_array_add (argv, TextAlphaBits);
 if (GraphicsAlphaBits[0]) g_ptr_array_add (argv, GraphicsAlphaBits);
 g_ptr_array_add (argv, "-q");
 g_ptr_array_add (argv, "-dNOPAUSE");
 g_ptr_array_add (argv, "-dBATCH");
 g_ptr_array_add (argv, (gpointer) gs_opts);
 g_ptr_array_add (argv, output);
 g_ptr_array_add (argv, infile);
 g_ptr_array_add (argv, "-c");
 g_ptr_array_add (argv, "quit");
 g_ptr_array_add (argv, NULL);

#ifdef PS_DEBUG
 {gchar *cmd = g_strjoinv (" ", (gchar **) argv->pdata);
   printf ("Going to start ghostscript with:\n%s\n", cmd);
   g_free (cmd);
 }
#endif

 proc = g_subprocess_newv ((const gchar * const *) argv->pdata,
                           G_SUBPROCESS_FLAGS_STDIN_INHERIT
                           | G_SUBPROCESS_FLAGS_STDOUT_SILENCE
                           | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                           &error);
 g_ptr_array_free (argv, TRUE);
 g_free (gs);
 g_free (driver);
 g_free (resopt);
 g_free (output);
 g_free (infile);

 ifp = NULL;
 if (proc == NULL)
 {
   g_message ("PostScript: can't start Ghostscript: %s", error->message);
   g_clear_error (&error);
 }
 else
 {
   /* Ghostscript may fail on some pages but still produce the */
   /* others, so read whatever it wrote regardless of its exit */
   if (!g_subprocess_wait (proc, NULL, &error))
     g_clear_error (&error);
   g_object_unref (proc);

   ifp = g_fopen (ps_pnmfile, "rb");
 }

 if (ifp == NULL)
 {
   g_unlink (ps_pnmfile);
   g_free (ps_pnmfile);
   ps_pnmfile = NULL;
 }
 return (ifp);
}


/* Close the PNM-File of the PostScript interpreter */
static void
ps_close (FILE *ifp)

{
 /* Close the output file of ghostscript and remove it. */
 fclose (ifp);
 if (ps_pnmfile != NULL)
 {
   g_unlink (ps_pnmfile);
   g_free (ps_pnmfile);
   ps_pnmfile = NULL;
 }
}


/* Read the header of a raw PNM-file and return type (4-6) or -1 on failure */
static gint
read_pnmraw_type (FILE *ifp,
                  int *width,
                  int *height,
                  int *maxval)

{register int frst, scnd, thrd;
 gint pnmtype;
 char line[1024];

 /* GhostScript may write some informational messages infront of the header. */
 /* We are just looking at a Px\n in the input stream. */
 frst = getc (ifp);
 scnd = getc (ifp);
 thrd = getc (ifp);
 for (;;)
 {
   if (thrd == EOF) return (-1);
   if ((thrd == '\n') && (frst == 'P') && (scnd >= '1') && (scnd <= '6'))
     break;
   frst = scnd;
   scnd = thrd;
   thrd = getc (ifp);
 }
 pnmtype = scnd - '0';
                       /* We dont use the ASCII-versions */
 if ((pnmtype >= 1) && (pnmtype <= 3)) return (-1);

 /* Read width/height */
 for (;;)
 {
   if (fgets (line, sizeof (line)-1, ifp) == NULL) return (-1);
   if (line[0] != '#') break;
 }
 if (sscanf (line, "%d%d", width, height) != 2) return (-1);
 /* The size comes from the document (a PDF MediaBox, say): keep the */
 /* line buffers (width*3) and tile buffers free of overflow.        */
 if ((*width <= 0) || (*height <= 0) || (*width > 65535) || (*height > 65535))
   return (-1);
 *maxval = 255;

 if (pnmtype != 4)  /* Read maxval */
 {
   for (;;)
   {
     if (fgets (line, sizeof (line)-1, ifp) == NULL) return (-1);
     if (line[0] != '#') break;
   }
   if (sscanf (line, "%d", maxval) != 1) return (-1);
 }
 return (pnmtype);
}


/* Create an image. Sets layer_ID, drawable and rgn. Returns image_ID */
static gint32
create_new_image (char *filename,
                  guint pagenum,
                  guint width,
                  guint height,
                  GImageType type,
                  gint32 *layer_ID,
                  GDrawable **drawable,
                  GPixelRgn *pixel_rgn)

{gint32 image_ID;
 GDrawableType gdtype;
 char *tmp;

 if (type == GRAY) gdtype = GRAY_IMAGE;
 else if (type == INDEXED) gdtype = INDEXED_IMAGE;
 else gdtype = RGB_IMAGE;

 image_ID = gimp_image_new (width, height, type);
 if ((tmp = g_malloc (strlen (filename) + 32)) != NULL)
 {
   sprintf (tmp, "%s-pg%ld", filename, (long)pagenum);
   gimp_image_set_filename (image_ID, tmp);
   g_free (tmp);
 }
 else
   gimp_image_set_filename (image_ID, filename);

 *layer_ID = gimp_layer_new (image_ID, "Background", width, height,
                            gdtype, 100, NORMAL_MODE);
 gimp_image_add_layer (image_ID, *layer_ID, 0);

 *drawable = gimp_drawable_get (*layer_ID);
 gimp_pixel_rgn_init (pixel_rgn, *drawable, 0, 0, (*drawable)->width,
                      (*drawable)->height, TRUE, FALSE);

 return (image_ID);
}


/* Skip PNM image generated from PostScript file. */
/* Returns 0 on success, -1 on failure. */
static gint32
skip_ps (FILE *ifp)

{register int k, c;
 int i, pnmtype, width, height, maxval, bpl;


 pnmtype = read_pnmraw_type (ifp, &width, &height, &maxval);

 if (pnmtype == 4)    /* Portable bitmap */
   bpl = (width + 7)/8;
 else if (pnmtype == 5)
   bpl = width;
 else if (pnmtype == 6)
   bpl = width*3;
 else
   return (-1);

 for (i = 0; i < height; i++)
 {
   k = bpl;  c = EOF;
   while (k-- > 0) c = getc (ifp);
   if (c == EOF) return (-1);

   if ((l_run_mode != RUN_NONINTERACTIVE) && ((i % 20) == 0))
     gimp_progress_update ((double)(i+1) / (double)height);
 }
 return (0);
}


/* Load PNM image generated from PostScript file */
static gint32
load_ps (char *filename,
         guint pagenum,
         FILE *ifp,
         int llx,
         int lly,
         int urx,
         int ury)

{register unsigned char *dest;
 unsigned char *data, *bitline = NULL, *byteline = NULL, *byteptr, *temp;
 unsigned char bit2byte[256*8];
 int width, height, tile_height, scan_lines, total_scan_lines;
 int image_width, image_height;
 int skip_left, skip_bottom;
 int i, j, pnmtype, maxval, bpp, nread;
 GImageType imagetype;
 gint32 layer_ID, image_ID;
 GPixelRgn pixel_rgn;
 GDrawable *drawable;
 int err = 0, e;

 pnmtype = read_pnmraw_type (ifp, &width, &height, &maxval);

 if ((width == urx+1) && (height == ury+1))  /* gs respected BoundingBox ? */
 {
   skip_left = llx;    skip_bottom = lly;
   image_width = width - skip_left;
   image_height = height - skip_bottom;
 }
 else
 {
   skip_left = skip_bottom = 0;
   image_width = width;
   image_height = height;
 }
 if (pnmtype == 4)   /* Portable Bitmap */
 {
   imagetype = INDEXED;
   nread = (width+7)/8;
   bpp = 1;
   bitline = (unsigned char *)g_malloc (nread);
   if (bitline == NULL) return (-1);
   byteline = (unsigned char *)g_malloc (nread*8);
   if (byteline == NULL) { g_free (bitline); return (-1); }

   /* Get an array for mapping 8 bits in a byte to 8 bytes */
   temp = bit2byte;
   for (j = 0; j < 256; j++)
     for (i = 7; i >= 0; i--)
       *(temp++) = ((j & (1 << i)) != 0);
 }
 else if (pnmtype == 5)  /* Portable Greymap */
 {
   imagetype = GRAY;
   nread = width;
   bpp = 1;
   byteline = (unsigned char *)g_malloc (nread);
   if (byteline == NULL) return (-1);
 }
 else if (pnmtype == 6)  /* Portable Pixmap */
 {
   imagetype = RGB;
   nread = width * 3;
   bpp = 3;
   byteline = (unsigned char *)g_malloc (nread);
   if (byteline == NULL) return (-1);
 }
 else return (-1);

 image_ID = create_new_image (filename, pagenum,
                              image_width, image_height, imagetype,
                              &layer_ID, &drawable, &pixel_rgn);

 tile_height = gimp_tile_height ();
 data = g_malloc (tile_height * image_width * bpp);

 dest = data;
 total_scan_lines = scan_lines = 0;

 if (pnmtype == 4)   /* Read bitimage ? Must be mapped to indexed */
 {static unsigned char BWColorMap[2*3] = { 255, 255, 255, 0, 0, 0 };

   gimp_image_set_cmap (image_ID, BWColorMap, 2);

   for (i = 0; i < height; i++)
   {
     e = (fread (bitline, 1, nread, ifp) != nread);
     if (total_scan_lines >= image_height) continue;
     err |= e;
     if (err) break;

     j = width;      /* Map 1 byte of bitimage to 8 bytes of indexed image */
     temp = bitline;
     byteptr = byteline;
     while (j >= 8)
     {
       memcpy (byteptr, bit2byte + *(temp++)*8, 8);
       byteptr += 8;
       j -= 8;
     }
     if (j > 0)
       memcpy (byteptr, bit2byte + *temp*8, j);

     memcpy (dest, byteline+skip_left, image_width);
     dest += image_width;
     scan_lines++;
     total_scan_lines++;

     if ((l_run_mode != RUN_NONINTERACTIVE) && ((i % 20) == 0))
       gimp_progress_update ((double)(i+1) / (double)image_height);

     if ((scan_lines == tile_height) || ((i+1) == image_height))
     {
       gimp_pixel_rgn_set_rect (&pixel_rgn, data, 0, i-scan_lines+1,
                                image_width, scan_lines);
       scan_lines = 0;
       dest = data;
     }
     if (err) break;
   }
 }
 else   /* Read gray/rgb-image */
 {
   for (i = 0; i < height; i++)
   {
     e = (fread (byteline, bpp, width, ifp) != width);
     if (total_scan_lines >= image_height) continue;
     err |= e;
     if (err) break;

     memcpy (dest, byteline+skip_left*bpp, image_width*bpp);
     dest += image_width*bpp;
     scan_lines++;
     total_scan_lines++;

     if ((l_run_mode != RUN_NONINTERACTIVE) && ((i % 20) == 0))
       gimp_progress_update ((double)(i+1) / (double)image_height);

     if ((scan_lines == tile_height) || ((i+1) == image_height))
     {
       gimp_pixel_rgn_set_rect (&pixel_rgn, data, 0, i-scan_lines+1,
                                image_width, scan_lines);
       scan_lines = 0;
       dest = data;
     }
     if (err) break;
   }
 }

 g_free (data);
 if (byteline) g_free (byteline);
 if (bitline) g_free (bitline);

 if (err)
   g_message ("EOF encountered on reading");

 gimp_drawable_flush (drawable);

 return (err ? -1 : image_ID);
}


/* Write out the PostScript file header */
static void save_ps_header (FILE *ofp,
                            char *filename)

{time_t cutime = time (NULL);

  fprintf (ofp, "%%!PS-Adobe-3.0%s\n", psvals.eps ? " EPSF-3.0" : "");
  fprintf (ofp, "%%%%Creator: GIMP PostScript file plugin V %4.2f \
by Peter Kirchgessner\n", VERSIO);
  fprintf (ofp, "%%%%Title: %s\n", filename);
  fprintf (ofp, "%%%%CreationDate: %s", ctime (&cutime));
  fprintf (ofp, "%%%%DocumentData: Clean7Bit\n");
  if (psvals.eps) fprintf (ofp, "%%%%LanguageLevel: 2\n");
  fprintf (ofp, "%%%%Pages: 1\n");
}


/* Write out transformation for image */
static void
save_ps_setup (FILE *ofp,
               gint32 drawable_ID,
               int width,
               int height,
               int bpp)

{double x_offset, y_offset, x_size, y_size;
 double x_scale, y_scale;
 double width_inch, height_inch;
 double f1, f2, dx, dy;
 int xtrans, ytrans;

  /* initialize */

  dx = 0.0;
  dy = 0.0;

  x_offset = psvals.x_offset;
  y_offset = psvals.y_offset;
  width_inch = fabs (psvals.width);
  height_inch = fabs (psvals.height);

  if (psvals.unit_mm)
  {
    x_offset /= 25.4; y_offset /= 25.4;
    width_inch /= 25.4; height_inch /= 25.4;
  }
  if (psvals.keep_ratio)   /* Proportions to keep ? */
  {                        /* Fit the image into the allowed size */
    f1 = width_inch / width;
    f2 = height_inch / height;
    if (f1 < f2)
      height_inch = width_inch * (double)(height)/(double)(width);
    else
      width_inch = fabs (height_inch) * (double)(width)/(double)(height);
  }
  if ((psvals.rotate == 0) || (psvals.rotate == 180))
  { x_size = width_inch; y_size = height_inch; }
  else
  { y_size = width_inch; x_size = height_inch; }

  fprintf (ofp, "%%%%BoundingBox: %d %d %d %d\n",(int)(x_offset*72.0),
           (int)(y_offset*72.0), (int)((x_offset+x_size)*72.0)+1,
           (int)((y_offset+y_size)*72.0)+1);
  fprintf (ofp, "%%%%EndComments\n");

  if (psvals.preview && (psvals.preview_size > 0))
  {
    save_ps_preview (ofp, drawable_ID);
  }

  fprintf (ofp, "%%%%BeginProlog\n");
  fprintf (ofp, "%% Use own dictionary to avoid conflicts\n");
  fprintf (ofp, "5 dict begin\n");
  fprintf (ofp, "%%%%EndProlog\n");
  fprintf (ofp, "%%%%Page: 1 1\n");
  fprintf (ofp, "%% Translate for offset\n");
  fprintf (ofp, "%f %f translate\n", x_offset*72.0, y_offset*72.0);

  /* Calculate translation to startpoint of first scanline */
  switch (psvals.rotate)
  {
    case   0: dx = 0.0; dy = y_size*72.0;
              break;
    case  90: dx = dy = 0.0;
              x_scale = 72.0 * width_inch;
              y_scale = -72.0 * height_inch;
              break;
    case 180: dx = x_size*72.0; dy = 0.0; break;
    case 270: dx = x_size*72.0; dy = y_size*72.0; break;
  }
  if ((dx != 0.0) || (dy != 0.0))
    fprintf (ofp, "%% Translate to begin of first scanline\n%f %f translate\n",
             dx, dy);
  if (psvals.rotate)
    fprintf (ofp, "%d rotate\n", (int)psvals.rotate);
  fprintf (ofp, "%f %f scale\n", 72.0*width_inch, -72.0*height_inch);

  /* Write the PostScript procedures to read the image */
  fprintf (ofp, "%% Variable to keep one line of raster data\n");
  if (bpp == 1)
    fprintf (ofp, "/scanline %d string def\n", (width+7)/8);
  else
    fprintf (ofp, "/scanline %d %d mul string def\n", width, bpp/8);

  fprintf (ofp, "%% Image geometry\n%d %d %d\n", width, height,
           (bpp == 1) ? 1 : 8);
  fprintf (ofp, "%% Transformation matrix\n");
  xtrans = ytrans = 0;
  if (psvals.width < 0.0) { width = -width; xtrans = -width; }
  if (psvals.height < 0.0) { height = -height; ytrans = -height; }
  fprintf (ofp, "[ %d 0 0 %d %d %d ]\n", width, height, xtrans, ytrans);
}


static void
save_ps_trailer (FILE *ofp)

{
  fprintf (ofp, "%%%%Trailer\n");
  fprintf (ofp, "end\n%%%%EOF\n");
}

/* Do a Floyd-Steinberg dithering on a greyscale scanline. */
/* linecount must keep the counter for the actual scanline (0, 1, 2, ...). */
/* If linecount is less than zero, all used memory is freed. */

static void
dither_grey (unsigned char *grey,
             unsigned char *bw,
             int npix,
             int linecount)

{register unsigned char *greyptr, *bwptr, mask;
 register int *fse;
 int x, greyval, fse_inline;
 static int *fs_error = NULL;
 static int do_init_arrays = 1;
 static int limit_array[1278];
 static int east_error[256],seast_error[256],south_error[256],swest_error[256];
 int *limit = &(limit_array[512]);

 if (linecount <= 0)
 {
   if (fs_error) g_free (fs_error-1);
   if (linecount < 0) return;
   fs_error = (int *)g_malloc ((npix+2)*sizeof (int));
   if (fs_error != NULL)
   {
     memset ((char *)fs_error, 0, (npix+2)*sizeof (int));
     fs_error++;
   }

   /* Initialize some arrays that speed up dithering */
   if (do_init_arrays)
   {
     do_init_arrays = 0;
     for (x = -511; x <= 766; x++)
       limit[x] = (x < 0) ? 0 : ((x > 255) ? 255 : x);
     for (greyval = 0; greyval < 256; greyval++)
     {
       east_error[greyval] = (greyval < 128) ? ((greyval * 79) >> 8)
                                             : (((greyval-255)*79) >> 8);
       seast_error[greyval] = (greyval < 128) ? ((greyval * 34) >> 8)
                                             : (((greyval-255)*34) >> 8);
       south_error[greyval] = (greyval < 128) ? ((greyval * 56) >> 8)
                                             : (((greyval-255)*56) >> 8);
       swest_error[greyval] = (greyval < 128) ? ((greyval * 12) >> 8)
                                             : (((greyval-255)*12) >> 8);
     }
   }
 }
 if (fs_error == NULL) return;

 memset (bw, 0, (npix+7)/8); /* Initialize with white */

 greyptr = grey;
 bwptr = bw;
 mask = 0x80;
 fse_inline = fs_error[0];
 for (x = 0, fse = fs_error; x < npix; x++, fse++)
 {
   greyval = limit[*(greyptr++) + fse_inline];  /* 0 <= greyval <= 255 */
   if (greyval < 128) *bwptr |= mask;  /* Set a black pixel */

   /* Error distribution */
   fse_inline = east_error[greyval] + fse[1];
   fse[1] = seast_error[greyval];
   fse[0] += south_error[greyval];
   fse[-1] += swest_error[greyval];

   mask >>= 1;   /* Get mask for next b/w-pixel */
   if (!mask)
   {
     mask = 0x80;
     bwptr++;
   }
 }
}

/* Write a device independant screen preview */
static void
save_ps_preview (FILE *ofp,
                 gint32 drawable_ID)

{register unsigned char *bwptr, *greyptr;
 GDrawableType drawable_type;
 GDrawable *drawable;
 GPixelRgn src_rgn;
 int width, height, x, y, nbsl, out_count;
 int nchar_pl = 72, src_y;
 double f1, f2;
 unsigned char *grey, *bw, *src_row, *src_ptr;
 unsigned char *cmap;
 gint ncols, cind;

 if (psvals.preview_size <= 0) return;

 drawable = gimp_drawable_get (drawable_ID);
 drawable_type = gimp_drawable_type (drawable_ID);

 /* Calculate size of preview */
 if (   (drawable->width <= psvals.preview_size)
     && (drawable->height <= psvals.preview_size))
 {
   width = drawable->width;
   height = drawable->height;
 }
 else
 {
   f1 = (double)psvals.preview_size / (double)drawable->width;
   f2 = (double)psvals.preview_size / (double)drawable->height;
   if (f1 < f2)
   {
     width = psvals.preview_size;
     height = drawable->height * f1;
     if (height <= 0) height = 1;
   }
   else
   {
     height = psvals.preview_size;
     width = drawable->width * f1;
     if (width <= 0) width = 1;
   }
 }

 nbsl = (width+7)/8;  /* Number of bytes per scanline in bitmap */

 grey = (unsigned char *)g_malloc (width);
 if (grey == NULL) return;
 bw = (unsigned char *)g_malloc (nbsl);
 if (bw == NULL) return;
 src_row = (unsigned char *)g_malloc (drawable->width * drawable->bpp);
 if (src_row == NULL) return;

 fprintf (ofp, "%%%%BeginPreview: %d %d 1 %d\n", width, height,
          ((nbsl*2+nchar_pl-1)/nchar_pl)*height);

 gimp_pixel_rgn_init (&src_rgn, drawable, 0, 0, drawable->width,
                      drawable->height, FALSE, FALSE);

 cmap = NULL;     /* Check if we need a colour table */
 if (gimp_drawable_type (drawable_ID) == INDEXED_IMAGE)
   cmap = (unsigned char *)
            gimp_image_get_cmap (gimp_drawable_image_id (drawable_ID), &ncols);

 for (y = 0; y < height; y++)
 {
   /* Get a scanline from the input image and scale it to the desired width */
   src_y = (y * drawable->height) / height;
   gimp_pixel_rgn_get_row (&src_rgn, src_row, 0, src_y, drawable->width);

   greyptr = grey;
   if (drawable->bpp == 3)   /* RGB-image */
   {
     for (x = 0; x < width; x++)
     {                       /* Convert to grey */
       src_ptr = src_row + ((x * drawable->width) / width) * 3;
       *(greyptr++) = (3*src_ptr[0] + 6*src_ptr[1] + src_ptr[2]) / 10;
     }
   }
   else if (cmap)    /* Indexed image */
   {
     for (x = 0; x < width; x++)
     {
       src_ptr = src_row + ((x * drawable->width) / width);
       cind = *src_ptr;   /* Get colour index and convert to grey */
       src_ptr = (cind >= ncols) ? cmap : (cmap + 3*cind);
       *(greyptr++) = (3*src_ptr[0] + 6*src_ptr[1] + src_ptr[2]) / 10;
     }
   }
   else             /* Grey image */
   {
     for (x = 0; x < width; x++)
       *(greyptr++) = *(src_row + ((x * drawable->width) / width));
   }

   /* Now we have a greyscale line for the desired width. */
   /* Dither it to b/w */
   dither_grey (grey, bw, width, y);

   /* Write out the b/w line */
   out_count = 0;
   bwptr = bw;
   for (x = 0; x < nbsl; x++)
   {
     if (out_count == 0) fprintf (ofp, "%% ");
     fprintf (ofp, "%02x", *(bwptr++));
     out_count += 2;
     if (out_count >= nchar_pl)
     {
       fprintf (ofp, "\n");
       out_count = 0;
     }
   }
   if (out_count != 0)
     fprintf (ofp, "\n");

   if ((l_run_mode != RUN_NONINTERACTIVE) && ((y % 20) == 0))
     gimp_progress_update ((double)(y) / (double)height);
 }

 fprintf (ofp, "%%%%EndPreview\n");

 dither_grey (grey, bw, width, -1);
 g_free (src_row);
 g_free (bw);
 g_free (grey);

 gimp_drawable_detach (drawable);
}

static gint
save_gray  (FILE *ofp,
            gint32 image_ID,
            gint32 drawable_ID)

{ int height, width, i, j;
  int tile_height;
  unsigned char *data, *src;
  GPixelRgn pixel_rgn;
  GDrawable *drawable;
  GDrawableType drawable_type;
  static char *hex = "0123456789abcdef";

  drawable = gimp_drawable_get (drawable_ID);
  drawable_type = gimp_drawable_type (drawable_ID);
  width = drawable->width;
  height = drawable->height;
  tile_height = gimp_tile_height ();
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height, FALSE, FALSE);

  /* allocate a buffer for retrieving information from the pixel region  */
  src = data = (unsigned char *)g_malloc (tile_height * width * drawable->bpp);

  /* Set up transformation in PostScript */
  save_ps_setup (ofp, drawable_ID, width, height, 1*8);

  /* Write read image procedure */
  fprintf (ofp, "{ currentfile scanline readhexstring pop }\n");
  fprintf (ofp, "image\n");

#define GET_GRAY_TILE(begin) \
  {int scan_lines; \
    scan_lines = (i+tile_height-1 < height) ? tile_height : (height-i); \
    gimp_pixel_rgn_get_rect (&pixel_rgn, begin, 0, i, width, scan_lines); \
    src = begin; }

  for (i = 0; i < height; i++)
  {
    if ((i % tile_height) == 0) GET_GRAY_TILE (data); /* Get more data */
    for (j = 0; j < width; j++)
    {
      putc (hex[(*src) >> 4], ofp);
      putc (hex[(*(src++)) & 0x0f], ofp);
      if (((j+1) % 39) == 0) putc ('\n', ofp);
    }
    putc ('\n', ofp);
    if ((l_run_mode != RUN_NONINTERACTIVE) && ((i % 20) == 0))
      gimp_progress_update ((double) i / (double) height);
  }
  fprintf (ofp, "showpage\n");
  g_free (data);

  gimp_drawable_detach (drawable);

  if (ferror (ofp))
  {
    g_message ("write error occured");
    return (FALSE);
  }
  return (TRUE);
#undef GET_GRAY_TILE
}


static gint
save_bw (FILE *ofp,
         gint32 image_ID,
         gint32 drawable_ID)

{ int height, width, i, j;
  int ncols, nbsl, nwrite;
  int tile_height;
  unsigned char *cmap, *ct;
  unsigned char *data, *src;
  unsigned char *scanline, *dst, mask;
  unsigned char *hex_scanline;
  GPixelRgn pixel_rgn;
  GDrawable *drawable;
  GDrawableType drawable_type;
  static char *hex = "0123456789abcdef";

  cmap = gimp_image_get_cmap (image_ID, &ncols);

  drawable = gimp_drawable_get (drawable_ID);
  drawable_type = gimp_drawable_type (drawable_ID);
  width = drawable->width;
  height = drawable->height;
  tile_height = gimp_tile_height ();
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height, FALSE, FALSE);

  /* allocate a buffer for retrieving information from the pixel region  */
  src = data = (unsigned char *)g_malloc (tile_height * width * drawable->bpp);
  nbsl = (width+7)/8;
  scanline = (char *)g_malloc (nbsl + 1);
  hex_scanline = (char *)g_malloc ((nbsl + 1)*2);

  /* Set up transformation in PostScript */
  save_ps_setup (ofp, drawable_ID, width, height, 1);

  /* Write read image procedure */
  fprintf (ofp, "{ currentfile scanline readhexstring pop }\n");
  fprintf (ofp, "image\n");

#define GET_BW_TILE(begin) \
  {int scan_lines; \
    scan_lines = (i+tile_height-1 < height) ? tile_height : (height-i); \
    gimp_pixel_rgn_get_rect (&pixel_rgn, begin, 0, i, width, scan_lines); \
    src = begin; }

  for (i = 0; i < height; i++)
  {
    if ((i % tile_height) == 0) GET_BW_TILE (data); /* Get more data */
    dst = scanline;
    memset (dst, 0, nbsl);
    mask = 0x80;
    /* Build a bitmap for a scanline */
    for (j = 0; j < width; j++)
    {
      ct = cmap + *(src++)*3;
      if (ct[0] || ct[1] || ct[2])
        *dst |= mask;
      if (mask == 0x01) { mask = 0x80; dst++; } else mask >>= 1;
    }
    /* Convert to hexstring */
    for (j = 0; j < nbsl; j++)
    {
      hex_scanline[j*2] = (unsigned char)hex[scanline[j] >> 4];
      hex_scanline[j*2+1] = (unsigned char)hex[scanline[j] & 0x0f];
    }
    /* Write out hexstring */
    j = nbsl * 2;
    dst = hex_scanline;
    while (j > 0)
    {
      nwrite = (j > 78) ? 78 : j;
      fwrite (dst, nwrite, 1, ofp);
      putc ('\n', ofp);
      j -= nwrite;
      dst += nwrite;
    }
    if ((l_run_mode != RUN_NONINTERACTIVE) && ((i % 20) == 0))
      gimp_progress_update ((double) i / (double) height);
  }
  fprintf (ofp, "showpage\n");

  g_free (hex_scanline);
  g_free (scanline);
  g_free (data);

  gimp_drawable_detach (drawable);

  if (ferror (ofp))
  {
    g_message ("write error occured");
    return (FALSE);
  }
  return (TRUE);
#undef GET_BW_TILE
}


static gint
save_index (FILE *ofp,
            gint32 image_ID,
            gint32 drawable_ID)

{ int height, width, i, j;
  int ncols, bw;
  int tile_height;
  unsigned char *cmap;
  unsigned char *data, *src;
  char coltab[256*6], *ct;
  GPixelRgn pixel_rgn;
  GDrawable *drawable;
  GDrawableType drawable_type;
  static char *hex = "0123456789abcdef";
  static char *background = "000000";

  cmap = gimp_image_get_cmap (image_ID, &ncols);

  ct = coltab;
  bw = 1;
  for (j = 0; j < 256; j++)
  {
    if (j >= ncols)
    {
      memcpy (ct, background, 6);
      ct += 6;
    }
    else
    {
      bw &=    ((cmap[0] == 0) && (cmap[1] == 0) && (cmap[2] == 0))
            || ((cmap[0] == 255) && (cmap[1] == 255) && (cmap[2] == 255));
      *(ct++) = (unsigned char)hex[(*cmap) >> 4];
      *(ct++) = (unsigned char)hex[(*(cmap++)) & 0x0f];
      *(ct++) = (unsigned char)hex[(*cmap) >> 4];
      *(ct++) = (unsigned char)hex[(*(cmap++)) & 0x0f];
      *(ct++) = (unsigned char)hex[(*cmap) >> 4];
      *(ct++) = (unsigned char)hex[(*(cmap++)) & 0x0f];
    }
  }
  if (bw) return (save_bw (ofp, image_ID, drawable_ID));

  drawable = gimp_drawable_get (drawable_ID);
  drawable_type = gimp_drawable_type (drawable_ID);
  width = drawable->width;
  height = drawable->height;
  tile_height = gimp_tile_height ();
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height, FALSE, FALSE);

  /* allocate a buffer for retrieving information from the pixel region  */
  src = data = (unsigned char *)g_malloc (tile_height * width * drawable->bpp);

  /* Set up transformation in PostScript */
  save_ps_setup (ofp, drawable_ID, width, height, 3*8);

  /* Write read image procedure */
  fprintf (ofp, "{ currentfile scanline readhexstring pop } false 3\n");
  fprintf (ofp, "colorimage\n");

#define GET_INDEX_TILE(begin) \
  {int scan_lines; \
    scan_lines = (i+tile_height-1 < height) ? tile_height : (height-i); \
    gimp_pixel_rgn_get_rect (&pixel_rgn, begin, 0, i, width, scan_lines); \
    src = begin; }

  for (i = 0; i < height; i++)
  {
    if ((i % tile_height) == 0) GET_INDEX_TILE (data); /* Get more data */
    for (j = 0; j < width; j++)
    {
      fwrite (coltab+(*(src++))*6, 6, 1, ofp);
      if (((j+1) % 13) == 0) putc ('\n', ofp);
    }
    putc ('\n', ofp);
    if ((l_run_mode != RUN_NONINTERACTIVE) && ((i % 20) == 0))
      gimp_progress_update ((double) i / (double) height);
  }
  fprintf (ofp, "showpage\n");

  g_free (data);

  gimp_drawable_detach (drawable);

  if (ferror (ofp))
  {
    g_message ("write error occured");
    return (FALSE);
  }
  return (TRUE);
#undef GET_INDEX_TILE
}


static gint
save_rgb (FILE *ofp,
          gint32 image_ID,
          gint32 drawable_ID)

{
  int height, width, tile_height;
  int i, j;
  unsigned char *data, *src;
  GPixelRgn pixel_rgn;
  GDrawable *drawable;
  GDrawableType drawable_type;
  static char *hex = "0123456789abcdef";

  drawable = gimp_drawable_get (drawable_ID);
  drawable_type = gimp_drawable_type (drawable_ID);
  width = drawable->width;
  height = drawable->height;
  tile_height = gimp_tile_height ();
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height, FALSE, FALSE);

  /* allocate a buffer for retrieving information from the pixel region  */
  src = data = (unsigned char *)g_malloc (tile_height * width * drawable->bpp);

  /* Set up transformation in PostScript */
  save_ps_setup (ofp, drawable_ID, width, height, 3*8);

  /* Write read image procedure */
  fprintf (ofp, "{ currentfile scanline readhexstring pop } false 3\n");
  fprintf (ofp, "colorimage\n");

#define GET_RGB_TILE(begin) \
  {int scan_lines; \
    scan_lines = (i+tile_height-1 < height) ? tile_height : (height-i); \
    gimp_pixel_rgn_get_rect (&pixel_rgn, begin, 0, i, width, scan_lines); \
    src = begin; }

  for (i = 0; i < height; i++)
  {
    if ((i % tile_height) == 0) GET_RGB_TILE (data); /* Get more data */
    for (j = 0; j < width; j++)
    {
      putc (hex[(*src) >> 4], ofp);        /* Red */
      putc (hex[(*(src++)) & 0x0f], ofp);
      putc (hex[(*src) >> 4], ofp);        /* Green */
      putc (hex[(*(src++)) & 0x0f], ofp);
      putc (hex[(*src) >> 4], ofp);        /* Blue */
      putc (hex[(*(src++)) & 0x0f], ofp);
      if (((j+1) % 13) == 0) putc ('\n', ofp);
    }
    putc ('\n', ofp);
    if ((l_run_mode != RUN_NONINTERACTIVE) && ((i % 20) == 0))
      gimp_progress_update ((double) i / (double) height);
  }
  fprintf (ofp, "showpage\n");
  g_free (data);

  gimp_drawable_detach (drawable);

  if (ferror (ofp))
  {
    g_message ("write error occured");
    return (FALSE);
  }
  return (TRUE);
#undef GET_RGB_TILE
}


/*  Dialog helpers  */

/* A frame holding a vbox, packed into box */
static GtkWidget *
ps_frame_vbox (GtkWidget *box,
               const char *title,
               int border,
               gboolean expand)

{
  GtkWidget *frame;
  GtkWidget *vbox;

  frame = gtk_frame_new (title);
  gimp_container_set_border_width (frame, border);
  gimp_box_pack_start (box, frame, expand, TRUE, 0);
  vbox = gimp_vbox_new (FALSE, 5);
  gimp_container_set_border_width (vbox, 5);
  gtk_frame_set_child (GTK_FRAME (frame), vbox);

  return vbox;
}


/* A left aligned label in the first column of table */
static void
ps_table_label (GtkWidget *table,
                const char *text,
                int row)

{
  GtkWidget *label;

  label = gtk_label_new (text);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, row, row+1,
                     GIMP_FILL, GIMP_FILL, 0, 0);
}


/* An entry in the second column of table */
static GtkWidget *
ps_table_entry (GtkWidget *table,
                const char *text,
                int width,
                int row)

{
  GtkWidget *entry;

  entry = gtk_entry_new ();
  gtk_widget_set_size_request (entry, width, -1);
  gtk_editable_set_text (GTK_EDITABLE (entry), text);
  gimp_table_attach (table, entry, 1, 2, row, row+1,
                     GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 0, 0);
  return entry;
}


/* A check or radio button bound to an int flag */
static GtkWidget *
ps_toggle (GtkWidget *box,
           gboolean radio,
           GtkWidget *group,
           const char *text,
           gboolean expand,
           int *value,
           GCallback callback,
           gpointer data)

{
  GtkWidget *toggle;

  if (radio)
    toggle = gimp_radio_button_new (group, text);
  else
    toggle = gtk_check_button_new_with_label (text);
  gimp_box_pack_start (box, toggle, expand, expand, 0);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (toggle), *value);
  g_signal_connect (toggle, "toggled", callback, data);

  return toggle;
}


/*  Load interface functions  */

static gint
load_dialog (void)

{
  LoadDialogVals *vals;
  GtkWidget *button;
  GtkWidget *toggle;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *group;
  char buffer[STR_LENGTH];
  static char *label_text[] = { "Resolution:", "Width:", "Height:", "Pages:" };
  static char *radio_text[] = { "b/w", "gray", "colour", "automatic" };
  static char *alias_text[] = { "none", "weak", "strong" };
  int j, n_prop, alias, *alpha_bits;

  gtk_init ();

  vals = g_malloc (sizeof (*vals));

  vals->dialog = gimp_dialog_new ("Load PostScript");
  g_signal_connect (vals->dialog, "destroy",
                    G_CALLBACK (load_close_callback),
                    NULL);

  /*  Action area  */
  gimp_dialog_add_button (vals->dialog, "OK",
                          G_CALLBACK (load_ok_callback), vals, TRUE);
  button = gimp_dialog_add_button (vals->dialog, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
                            G_CALLBACK (gtk_window_destroy), vals->dialog);

  hbox = gimp_hbox_new (FALSE, 0);
  gimp_box_pack_start (gimp_dialog_get_vbox (vals->dialog), hbox,
                       TRUE, TRUE, 0);

  /* Rendering */
  vbox = ps_frame_vbox (hbox, "Rendering", 10, TRUE);

  /* Resolution/Width/Height/Pages labels */
  n_prop = sizeof (label_text)/sizeof (label_text[0]);
  table = gimp_table_new (n_prop, 2, FALSE);
  gtk_grid_set_row_spacing (GTK_GRID (table), 5);
  gtk_grid_set_column_spacing (GTK_GRID (table), 5);
  gimp_box_pack_start (vbox, table, TRUE, TRUE, 0);

  for (j = 0; j < n_prop; j++)
    ps_table_label (table, label_text[j], j);

  /* Resolution/Width/Height/Pages Entries */
  for (j = 0; j < n_prop; j++)
  {
    if      (j == 0) g_snprintf (buffer, sizeof (buffer), "%d", (int)plvals.resolution);
    else if (j == 1) g_snprintf (buffer, sizeof (buffer), "%d", (int)plvals.width);
    else if (j == 2) g_snprintf (buffer, sizeof (buffer), "%d", (int)plvals.height);
    else             g_strlcpy (buffer, plvals.pages, sizeof (buffer));
    vals->entry[j] = ps_table_entry (table, buffer, 80, j);
  }

  vals->use_bbox = (plvals.use_bbox != 0);
  ps_toggle (vbox, FALSE, NULL, "try BoundingBox", TRUE,
             &(vals->use_bbox), G_CALLBACK (load_toggle_update),
             &(vals->use_bbox));

  /* Colouring */
  vbox = ps_frame_vbox (hbox, "Colouring", 10, TRUE);

  group = NULL;
  for (j = 0; j < 4; j++)
  {
    vals->dataformat[j] = (plvals.pnm_type == j+4);
    toggle = ps_toggle (vbox, TRUE, group, radio_text[j], FALSE,
                        &(vals->dataformat[j]),
                        G_CALLBACK (load_toggle_update),
                        &(vals->dataformat[j]));
    if (group == NULL) group = toggle;
  }

  hbox = gimp_hbox_new (FALSE, 0);
  gimp_box_pack_start (gimp_dialog_get_vbox (vals->dialog), hbox,
                       TRUE, TRUE, 0);

  for (alias = 0; alias < 2; alias++)
  {
    alpha_bits = alias ? &(vals->graphicsalphabits[0])
                       : &(vals->textalphabits[0]);
    vbox = ps_frame_vbox (hbox,
                          alias ? "Graphic antialiasing":"Text antialiasing",
                          10, TRUE);

    group = NULL;
    for (j = 0; j < 3; j++)
    {
      alpha_bits[j] = alias ? (plvals.graphicsalpha == (1 << j))
                            : (plvals.textalpha == (1 << j));
      toggle = ps_toggle (vbox, TRUE, group, alias_text[j], FALSE,
                          alpha_bits+j, G_CALLBACK (load_toggle_update),
                          alpha_bits+j);
      if (group == NULL) group = toggle;
    }
  }

  gtk_window_present (GTK_WINDOW (vals->dialog));

  gimp_main_loop_run ();

  g_free (vals);

  return plint.run;
}


static void
load_close_callback (GtkWidget *widget,
                     gpointer   data)

{
  gimp_main_loop_quit ();
}


static const char *
ps_entry_text (GtkWidget *entry)

{
  return gtk_editable_get_text (GTK_EDITABLE (entry));
}


static void
load_ok_callback (GtkWidget *widget,
                  gpointer   data)

{LoadDialogVals *vals = (LoadDialogVals *)data;

  /* Read resolution */
  plvals.resolution = atoi (ps_entry_text (vals->entry[0]));

  /* Read width */
  plvals.width = atoi (ps_entry_text (vals->entry[1]));

  /* Read height */
  plvals.height = atoi (ps_entry_text (vals->entry[2]));

  /* Read Pages */
  g_strlcpy (plvals.pages, ps_entry_text (vals->entry[3]),
             sizeof (plvals.pages));

  /* Read try BoundingBox */
  plvals.use_bbox = (vals->use_bbox != 0);

  /* Read colouring */
  if (vals->dataformat[0] == 1) plvals.pnm_type = 4;
  else if (vals->dataformat[1] == 1) plvals.pnm_type = 5;
  else if (vals->dataformat[3] == 1) plvals.pnm_type = 7;
  else plvals.pnm_type = 6;

  /* Read TextAlphaBits */
  if (vals->textalphabits[0] == 1) plvals.textalpha = 1;
  else if (vals->textalphabits[1] == 1) plvals.textalpha = 2;
  else if (vals->textalphabits[2] == 1) plvals.textalpha = 4;
  else plvals.textalpha = 1;

  /* Read GraphicsAlphaBits */
  if (vals->graphicsalphabits[0] == 1) plvals.graphicsalpha = 1;
  else if (vals->graphicsalphabits[1] == 1) plvals.graphicsalpha = 2;
  else if (vals->graphicsalphabits[2] == 1) plvals.graphicsalpha = 4;
  else  plvals.graphicsalpha = 1;

  plint.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (vals->dialog));
}


static void
load_toggle_update (GtkWidget *widget,
                    gpointer   data)

{
  int *toggle_val;

  toggle_val = (int *) data;

  *toggle_val = (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)) != 0);
}


/*  Save interface functions  */

static gint
save_dialog (void)

{
  SaveDialogVals *vals;
  GtkWidget *button;
  GtkWidget *toggle;
  GtkWidget *hbox, *vbox, *uvbox;
  GtkWidget *main_vbox[2];
  GtkWidget *table;
  GtkWidget *group;
  static char *label_text[] = { "Width:", "Height:", "X-offset:", "Y-offset:" };
  static char *radio_text[] = { "0", "90", "180", "270" };
  static char *unit_text[] = { "Inch", "Millimeter" };
  char tmp[80];
  int j, idata;
  double rdata;

  gtk_init ();

  vals = g_malloc (sizeof (*vals));

  vals->dialog = gimp_dialog_new ("Save PostScript");
  g_signal_connect (vals->dialog, "destroy",
                    G_CALLBACK (save_close_callback),
                    NULL);

  /*  Action area  */
  gimp_dialog_add_button (vals->dialog, "OK",
                          G_CALLBACK (save_ok_callback), vals, TRUE);
  button = gimp_dialog_add_button (vals->dialog, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
                            G_CALLBACK (gtk_window_destroy), vals->dialog);

  /* Main hbox */
  hbox = gimp_hbox_new (FALSE, 0);
  gimp_box_pack_start (gimp_dialog_get_vbox (vals->dialog), hbox,
                       FALSE, TRUE, 0);

  for (j = 0; j < sizeof (main_vbox) / sizeof (main_vbox[0]); j++)
  {
    main_vbox[j] = gimp_vbox_new (FALSE, 0);
    gimp_box_pack_start (hbox, main_vbox[j], TRUE, TRUE, 0);
  }

  /* Image Size */
  vbox = ps_frame_vbox (main_vbox[0], "Image Size", 5, FALSE);

  /* Width/Height/X-/Y-offset labels */
  table = gimp_table_new (4, 2, FALSE);
  gtk_grid_set_row_spacing (GTK_GRID (table), 5);
  gtk_grid_set_column_spacing (GTK_GRID (table), 5);
  gimp_box_pack_start (vbox, table, TRUE, TRUE, 0);

  for (j = 0; j < 4; j++)
    ps_table_label (table, label_text[j], j);

  /* Width/Height/X-off/Y-off Entries */
  for (j = 0; j < 4; j++)
  {
    if      (j == 0) rdata = psvals.width;
    else if (j == 1) rdata = psvals.height;
    else if (j == 2) rdata = psvals.x_offset;
    else             rdata = psvals.y_offset;
    vals->entry[j] = ps_table_entry (table, ftoa ("%-8.2f", rdata), 50, j);
  }

  vals->keep_ratio = (psvals.keep_ratio != 0);
  ps_toggle (vbox, FALSE, NULL, "keep aspect ratio", TRUE,
             &(vals->keep_ratio), G_CALLBACK (save_toggle_update),
             &(vals->keep_ratio));

  /* Unit */
  uvbox = ps_frame_vbox (vbox, "Unit", 5, FALSE);

  group = NULL;
  for (j = 0; j < 2; j++)
  {
    vals->unit[j] = (psvals.unit_mm == j);
    toggle = ps_toggle (uvbox, TRUE, group, unit_text[j], FALSE, &(vals->unit[j]),
                        (j == 0) ? G_CALLBACK (save_toggle_update)
                                 : G_CALLBACK (save_mm_toggle_update),
                        (j == 0) ? (gpointer)(&(vals->unit[j]))
                                 : (gpointer)vals);
    if (group == NULL) group = toggle;
  }

  /* Rotation */
  vbox = ps_frame_vbox (main_vbox[1], "Rotation", 5, TRUE);

  group = NULL;
  for (j = 0; j < 4; j++)
  {
    vals->rot[j] = (psvals.rotate == j*90);
    toggle = ps_toggle (vbox, TRUE, group, radio_text[j], FALSE, &(vals->rot[j]),
                        G_CALLBACK (save_toggle_update), &(vals->rot[j]));
    if (group == NULL) group = toggle;
  }

  /* Format */
  vbox = ps_frame_vbox (main_vbox[1], "Output", 5, TRUE);

  vals->eps = (psvals.eps != 0);
  ps_toggle (vbox, FALSE, NULL, "Encapsulated PostScript", TRUE,
             &(vals->eps), G_CALLBACK (save_toggle_update), &(vals->eps));

  vals->preview = psvals.preview;
  ps_toggle (vbox, FALSE, NULL, "Preview", TRUE,
             &(vals->preview), G_CALLBACK (save_toggle_update),
             &(vals->preview));

  /* Preview size label/entry */
  table = gimp_table_new (1, 2, FALSE);
  gtk_grid_set_row_spacing (GTK_GRID (table), 5);
  gtk_grid_set_column_spacing (GTK_GRID (table), 5);
  gimp_box_pack_start (vbox, table, TRUE, TRUE, 0);

  ps_table_label (table, "Preview size", 0);

  idata = psvals.preview_size;
  if (idata < 0) idata = 0;
  g_snprintf (tmp, sizeof (tmp), "%d", idata);
  vals->psize_entry = ps_table_entry (table, tmp, 50, 0);

  gtk_window_present (GTK_WINDOW (vals->dialog));

  gimp_main_loop_run ();

  g_free (vals);

  return psint.run;
}


static void
save_close_callback (GtkWidget *widget,
                     gpointer   data)

{
  gimp_main_loop_quit ();
}


static void
save_ok_callback (GtkWidget *widget,
                  gpointer   data)

{SaveDialogVals *vals = (SaveDialogVals *)data;
 double r;
 int k, ival;

  /* Read width */
  k = sscanf (ps_entry_text (vals->entry[0]), "%lf", &r);
  if (k == 1) psvals.width = r;

  /* Read height */
  k = sscanf (ps_entry_text (vals->entry[1]), "%lf", &r);
  if (k == 1) psvals.height = r;

  /* Read x-offset */
  k = sscanf (ps_entry_text (vals->entry[2]), "%lf", &r);
  if (k == 1) psvals.x_offset = r;

  /* Read y-offset */
  k = sscanf (ps_entry_text (vals->entry[3]), "%lf", &r);
  if (k == 1) psvals.y_offset = r;

  /* Read keep aspect ratio */
  psvals.keep_ratio = (vals->keep_ratio != 0);

  /* Read unit */
  if (vals->unit[0] == 1) psvals.unit_mm = 0;
  else psvals.unit_mm = 1;

  /* Read rotation */
  if (vals->rot[1] == 1) psvals.rotate = 90;
  else if (vals->rot[2] == 1) psvals.rotate = 180;
  else if (vals->rot[3] == 1) psvals.rotate = 270;
  else psvals.rotate = 0;

  /* Read EPS flag */
  psvals.eps = (vals->eps != 0);

  /* Read Preview flag */
  psvals.preview = (vals->preview != 0);

  /* Read preview size */
  k = sscanf (ps_entry_text (vals->psize_entry), "%d", &ival);
  if (k == 1) psvals.preview_size = ival;

  psint.run = TRUE;
  gtk_window_destroy (GTK_WINDOW (vals->dialog));
}


static void
save_toggle_update (GtkWidget *widget,
                    gpointer   data)

{
  int *toggle_val;

  toggle_val = (int *) data;

  *toggle_val = (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)) != 0);
}


static void
save_mm_toggle_update (GtkWidget *widget,
                        gpointer   data)

{ double factor = 0.0, r;
  SaveDialogVals *vals = (SaveDialogVals *)data;
  int newval, oldval = vals->unit[1];
  int mm_to_inch, inch_to_mm, j, k;

  newval = vals->unit[1] =
    (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)) != 0);
  mm_to_inch = (oldval == 1) && (newval == 0);
  inch_to_mm = (oldval == 0) && (newval == 1);
  if (mm_to_inch) factor = 1.0 / 25.4;
  else if (inch_to_mm) factor = 25.4;
  if (factor != 0.0)
  {
    for (j = 0; j < 4; j++)
    {
      k = sscanf (ps_entry_text (vals->entry[j]), "%lf", &r);
      if (k == 1)
        gtk_editable_set_text (GTK_EDITABLE (vals->entry[j]),
                               ftoa ("%-8.2f", r*factor));
    }
  }
}
