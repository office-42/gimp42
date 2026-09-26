/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * Copyright (C) 1997 Daniel Risacher 
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

/*
   *   GUMP - Gimp Useless Mail Plugin (or Gump Useless Mail Plugin if you prefer)
   *          version about .645 I would say... give or take a few decimal points
   *
   *
   *   by Adrian Likins <aklikins@eos.ncsu.edu>
   *      MIME encapsulation by Reagan Blundell <reagan@emails.net>
   *
   *
   *
   *   Based heavily on gz.c by Daniel Risacher
   *
   *     Lets you choose to send a image to the mail from the file save as dialog.
   *      images are piped to uuencode and then to mail...
   *
   *
   *   This works fine for .99.10. I havent actually tried it in combination with
   *   the gz plugin, but it works with all other file types. I will eventually get
   *   around to making sure it works with gz.
   *
   *  To use: 1) image->File->mail image
   *          2) when the mail dialog popups up, fill it out. Only to: and filename are required
   *             note: the filename needs to a type that the image can be saved as. otherwise
   *                   you will just send an empty message.
   *          3) click ok and it should be on its way
   *
   
   *
   * NOTE: You probabaly need sendmail installed. If your sendmail is in an odd spot
   *       you can change the #define below. If you use qmail or other MTA's, and this
   *       works after changing the MAILER, let me know how well or what changes were
   *       needed.
   *
   * NOTE: Uuencoding is now done in the plug-in itself; no uuencode program
   *       is needed.  The message is handed to sendmail with GSubprocess.  If
   *       MAILER does not exist, a "sendmail" in PATH is used; where there is
   *       none (Windows, usually) the plug-in says so and fails.
   *
   *
   * TODO: 1) the aforementioned abilty to specify the 
   *           uuencode filename                         *done*
   *       2) someway to do this without tmp files
   *              * wont happen anytime soon*
   *       3) MIME? *done*
   *       4) a pointlessly snazzier dialog
   *       5) make sure it works with gz     
   *               * works for .xcfgz but not .xcf.gz *
   *       6) add an option to choose if mail get 
   *          uuencode or not (or MIME'ed for that matter)
   *       7) realtime preview
   *       8) better entry for comments
   *       9) list of frequently used addreses
   *      10) openGL compliance
   *      11) better handling of filesave errors
   *
   *
   *  Version history
   *       .5  - 6/30/97 - inital relese
   *       .51 - 7/3/97  - fixed a few spelling errors and the like
   *       .65 - 7/4/97  - a fairly significant revision. changed it from a file
   *                       plugin to an image plugin.
   *                     - Changed some strcats into strcpy to be a bit more robust.
   *                     - added the abilty to specify the filename you want it sent as
   *                     - no more annoying hassles with the file saves as dialog
   *                     - plugin now registers itself as <image>/File/Mail image
   *       .7  - 9/12/97 - (RB) added support for MIME encapsulation
   *       .71 - 9/17/97 - (RB) included Base64 encoding functions from mpack
   *                       instead of using external program.
   *                     - General cleanup of the MIME handling code.
   *
   * As always: The utility of this plugin is left as an exercise for the reader
   *
 */
#ifndef MAILER
#define MAILER "/usr/lib/sendmail"
#endif

#define ENCAPSULATION_UUENCODE 0
#define ENCAPSULATION_MIME     1

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#ifdef G_OS_WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"


static void query (void);
static void run (char *name,
		 int nparams,
		 GParam * param,
		 int *nreturn_vals,
		 GParam ** return_vals);


static gint save_image (char *filename,
			gint32 image_ID,
			gint32 drawable_ID,
			gint32 run_mode);

static gint save_dialog (void);
static char *find_mailer (void);
static void uuencode (FILE *infile, const char *name, FILE *outfile);
static char *find_content_type (char *filename);
static void close_callback (GtkWidget * widget, gpointer data);
static void ok_callback (GtkWidget * widget, gpointer data);
static void encap_callback (GtkWidget * widget, gpointer data);
static void receipt_callback (GtkWidget * widget, gpointer data);
static void subject_callback (GtkWidget * widget, gpointer data);
static void comment_callback (GtkWidget * widget, gpointer data);
static void filename_callback (GtkWidget * widget, gpointer data);
static int valid_file (char *filename);
static void create_headers (FILE * mailpipe);
static char *find_extension (char *filename);
static int to64(FILE *infile, FILE *outfile);
static void output64chunk(int c1, int c2, int c3, int pads, FILE *outfile);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,				/* init_proc */
  NULL,				/* quit_proc */
  query,			/* query_proc */
  run,				/* run_proc */
};


typedef struct
  {
    char receipt[256];
    char subject[256];
    char comment[256];
    char filename[256];
    int  encapsulation;
  }
m_info;

static m_info mail_info = {
  /* I would a assume there is a better way to do this, but this works for now */
  "\0",
  "\0",
  "\0",
  "\0",
  ENCAPSULATION_MIME,  /* Change this to ENCAPSULATION_UUENCODE
			  if you prefer that as the default */
};

static int run_flag = 0;

MAIN ()

static void
query ()
{

  static GParamDef args[] =
  {
    {PARAM_INT32, "run_mode", "Interactive, non-interactive"},
    {PARAM_IMAGE, "image", "Input image"},
    {PARAM_DRAWABLE, "drawable", "Drawable to save"},
    {PARAM_STRING, "filename", "The name of the file to save the image in"},
    {PARAM_STRING, "receipt", "The email address to send to"},
    {PARAM_STRING, "subject", "The subject"},
    {PARAM_STRING, "comment", "The Comment"},
    {PARAM_INT32,  "encapsulation", "Uuencode, MIME"},
  };
  static int nargs = sizeof (args) / sizeof (args[0]);
  static GParamDef *return_vals = NULL;
  static int nreturn_vals = 0;

  gimp_install_procedure ("plug_in_mail_image",
			  "pipe files to uuencode then mail them",
			  "You need to have sendmail installed",
			  "Adrian Likins, Reagan Blundell",
			  "Adrian Likins, Reagan Blundell, Daniel Risacher, Spencer Kimball and Peter Mattis",
			  "1995-1997",
			  "<Image>/File/Mail image",
			  "RGB*, GRAY*, INDEXED*",
			  PROC_PLUG_IN,
			  nargs, nreturn_vals,
			  args, return_vals);

}


static void
run (char *name,
     int nparams,
     GParam * param,
     int *nreturn_vals,
     GParam ** return_vals)
{
  static GParam values[2];
  GRunModeType run_mode;
  gint32 drawable_ID;
  GStatusType status = STATUS_SUCCESS;
  gint32 image_ID;


  run_mode = param[0].data.d_int32;
  drawable_ID = param[2].data.d_drawable;
  image_ID = param[1].data.d_image;
  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = STATUS_CALLING_ERROR;

  if (strcmp (name, "plug_in_mail_image") == 0)
    {
      switch (run_mode)
	{
	case RUN_INTERACTIVE:
	  gimp_get_data ("plug_in_mail_image", &mail_info);
	  if (!save_dialog ())
	    return;
	  break;
	case RUN_NONINTERACTIVE:
	  /*  Make sure all the arguments are there!  */
	  if (nparams != 8)
	    status = STATUS_CALLING_ERROR;
	  if(status == STATUS_SUCCESS)
	    {
	      /* this hasnt been tested yet */
	      g_strlcpy (mail_info.filename, param[3].data.d_string, 256);
	      g_strlcpy (mail_info.receipt, param[4].data.d_string, 256);
	      g_strlcpy (mail_info.subject, param[5].data.d_string, 256);
	      g_strlcpy (mail_info.comment, param[6].data.d_string, 256);
	      mail_info.encapsulation = param[7].data.d_int32;
	    }
	  break;
	case RUN_WITH_LAST_VALS:
	  gimp_get_data ("plug_in_mail_image", &mail_info);
	  break;
	  
	default:
	  break;
	}

      *nreturn_vals = 1;
      if (save_image (mail_info.filename,
		      image_ID,
		      drawable_ID,
		      run_mode))
	{
	  values[0].data.d_status = STATUS_SUCCESS;
	}
      else
	values[0].data.d_status = STATUS_EXECUTION_ERROR;
    }
  else
    g_assert (FALSE);
}

static gint
save_image (char *filename,
	    gint32 image_ID,
	    gint32 drawable_ID,
	    gint32 run_mode)
{

  GParam *params;
  gint retvals;
  char *ext;
  char *tmpname;
  char *mailer;
  char *msgname = NULL;
  char *msg;
  gsize msglen;
  int msgfd;
  gboolean ok;
  GError *error = NULL;
  GSubprocessLauncher *launcher;
  GSubprocess *proc;
  FILE *mailpipe;
  FILE *infile;

  if (NULL == (ext = find_extension (filename)))
    return 0;

  /* there has to be a sendmail to hand the message to */
  mailer = find_mailer ();
  if (mailer == NULL)
    {
      g_message ("mail: can't send mail, no sendmail program was found\n"
		 "(looked for " MAILER " and \"sendmail\" in PATH)\n");
      return 0;
    }

  /* get a temp name with the right extension and save into it. */
  params = gimp_run_procedure ("gimp_temp_name",
			       &retvals,
			       PARAM_STRING, ext + 1,
			       PARAM_END);
  tmpname = params[1].data.d_string;

  /* the message is put together in a second temp file, which then
   * becomes sendmail's standard input
   */
  msgfd = g_file_open_tmp ("gimp-mail-XXXXXX", &msgname, &error);
  if (msgfd < 0 || (mailpipe = fdopen (msgfd, "wb")) == NULL)
    {
      g_message ("mail: can't create a temporary file: %s\n",
		 error ? error->message : g_strerror (errno));
      g_clear_error (&error);
      if (msgfd >= 0)
	close (msgfd);
      g_free (msgname);
      g_free (mailer);
      return 0;
    }
  create_headers (mailpipe);

  params = gimp_run_procedure ("gimp_file_save",
			       &retvals,
			       PARAM_INT32, run_mode,
			       PARAM_IMAGE, image_ID,
			       PARAM_DRAWABLE, drawable_ID,
			       PARAM_STRING, tmpname,
			       PARAM_STRING, tmpname,
			       PARAM_END);

  /* need to figure a way to make sure the user is trying to save in an approriate format */
  /* but this can wait....                                                                */

  if (params[0].data.d_status == FALSE || !valid_file (tmpname))
    {
      g_unlink (tmpname);
      fclose (mailpipe);
      g_unlink (msgname);
      g_free (msgname);
      g_free (mailer);
      return 0;
    }

  infile = g_fopen (tmpname, "rb");
  if (infile == NULL)
    {
      g_message ("mail: can't open %s: %s\n", tmpname, g_strerror (errno));
      g_unlink (tmpname);
      fclose (mailpipe);
      g_unlink (msgname);
      g_free (msgname);
      g_free (mailer);
      return 0;
    }

  if( mail_info.encapsulation == ENCAPSULATION_UUENCODE ) {
      /* this used to run an external uuencode */
      uuencode (infile, filename, mailpipe);
  }
  else {  /* This must be MIME stuff. Base64 away... */
      to64(infile,mailpipe);
      /* close off mime */
      if( mail_info.encapsulation == ENCAPSULATION_MIME ) {
	  fprintf(mailpipe, "\n--GUMP-MIME-boundary--\n");
      }
  }
  fclose (infile);
  fclose (mailpipe);

  /* delete the tmpfile that was generated */
  g_unlink (tmpname);

  /* and hand the message to "sendmail user@location" */
  proc = NULL;
  ok = g_file_get_contents (msgname, &msg, &msglen, &error);
  if (ok)
    {
      GBytes *bytes = g_bytes_new_take (msg, msglen);

      launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDIN_PIPE);
      proc = g_subprocess_launcher_spawn (launcher, &error,
					  mailer, mail_info.receipt, NULL);
      ok = proc &&
	g_subprocess_communicate (proc, bytes, NULL, NULL, NULL, &error) &&
	g_subprocess_wait_check (proc, NULL, &error);
      g_bytes_unref (bytes);
      g_object_unref (launcher);
    }
  if (!ok)
    g_message ("mail: sending mail with %s failed: %s\n",
	       mailer, error->message);

  g_clear_error (&error);
  g_clear_object (&proc);
  g_unlink (msgname);
  g_free (msgname);
  g_free (mailer);

  return ok;
}

/* The sendmail to use: MAILER if it is there, otherwise a "sendmail"
 * found in PATH (on Windows there usually is none, and mailing is
 * then not available).
 */
static char *
find_mailer (void)
{
  if (g_file_test (MAILER, G_FILE_TEST_IS_EXECUTABLE))
    return g_strdup (MAILER);

  return g_find_program_in_path ("sendmail");
}

/* What "uuencode infile name" used to write */
static void
uuencode (FILE       *infile,
	  const char *name,
	  FILE       *outfile)
{
  unsigned char in[45];
  size_t n, i;

#define UUENC(c) ((c) ? ((c) & 077) + ' ' : '`')

  fprintf (outfile, "begin 644 %s\n", name);

  while ((n = fread (in, 1, sizeof (in), infile)) > 0)
    {
      /* pad the last line to a multiple of 3 */
      for (i = n; i % 3; i++)
	in[i] = 0;

      putc (UUENC (n), outfile);
      for (i = 0; i < n; i += 3)
	{
	  int c1 = in[i], c2 = in[i + 1], c3 = in[i + 2];

	  putc (UUENC (c1 >> 2), outfile);
	  putc (UUENC (((c1 << 4) & 060) | ((c2 >> 4) & 017)), outfile);
	  putc (UUENC (((c2 << 2) & 074) | ((c3 >> 6) & 03)), outfile);
	  putc (UUENC (c3 & 077), outfile);
	}
      putc ('\n', outfile);
    }

  putc (UUENC (0), outfile);
  fprintf (outfile, "\nend\n");

#undef UUENC
}


static gint
save_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *entry;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *button1;
  GtkWidget *button2;
  GtkWidget *group;




  gtk_init ();

  dlg = gimp_dialog_new ("Send to mail");
  g_signal_connect (dlg, "destroy",
		      G_CALLBACK (close_callback), NULL);
  /* action area   */
  /* Okay buton */
  button = gtk_button_new_with_label ("OK");
  g_signal_connect (button, "clicked",
		      G_CALLBACK (ok_callback),
		      dlg);
  gimp_box_pack_start (gimp_dialog_get_action_area (dlg), button, TRUE, TRUE, 0);
  gtk_window_set_default_widget (GTK_WINDOW (dlg), button);


  /* cancel button */
  button = gtk_button_new_with_label ("Cancel");
  gimp_box_pack_start (gimp_dialog_get_action_area (dlg), button, TRUE, TRUE, 0);
  g_signal_connect_swapped (button, "clicked",
			     G_CALLBACK (gtk_window_destroy),
			     dlg);

  /* table */
  table = gimp_table_new (5, 3, FALSE);
  gimp_container_set_border_width (table, 10);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), table, TRUE, TRUE, 0);

  gtk_grid_set_row_spacing (GTK_GRID (table), 10);
  gtk_grid_set_column_spacing (GTK_GRID (table), 10);

  /*  To:  Label */
  label = gtk_label_new ("To:");
  gimp_table_attach (table, label,
		    0, 1, 0, 1,
		    GIMP_EXPAND | GIMP_FILL,
		    GIMP_EXPAND | GIMP_FILL,
		    0, 0);

  /* to: dialog */
  entry = gtk_entry_new ();
  gimp_table_attach (table, entry,
		    1, 3, 0, 1, 
		    GIMP_EXPAND | GIMP_FILL,
		    GIMP_EXPAND | GIMP_FILL,
		    0, 0);
  gtk_widget_set_size_request (entry, 200, -1);
  gtk_editable_set_text (GTK_EDITABLE (entry), mail_info.receipt);
  g_signal_connect (entry, "changed",
		      G_CALLBACK (receipt_callback), &mail_info.receipt);

  /*  subject Label */
  label = gtk_label_new ("Subject:");
  gimp_table_attach (table, label,
		    0, 1, 1, 2,
		    GIMP_EXPAND | GIMP_FILL,
		    GIMP_EXPAND | GIMP_FILL,
		    0, 0);

  /* Subject entry */
  entry = gtk_entry_new ();
  gimp_table_attach (table, entry,
		    1, 3, 1, 2,
		    GIMP_EXPAND | GIMP_FILL,
		    GIMP_EXPAND | GIMP_FILL,
		    0, 0);
  gtk_widget_set_size_request (entry, 200, -1);
  gtk_editable_set_text (GTK_EDITABLE (entry), mail_info.subject);
  g_signal_connect (entry, "changed",
		      G_CALLBACK (subject_callback), &mail_info.subject);


  /* Comment label  */
  label = gtk_label_new ("Comment:");
  gimp_table_attach (table, label, 
		    0, 1, 2, 3, 
		    GIMP_EXPAND | GIMP_FILL,
		    GIMP_EXPAND | GIMP_FILL,
		    0, 0);

  /* Comment dialog */
  entry = gtk_entry_new ();
  gimp_table_attach (table, entry, 
		    1, 3, 2, 3,
		    GIMP_EXPAND | GIMP_FILL,
		    GIMP_EXPAND | GIMP_FILL,
		    0, 0);
  gtk_widget_set_size_request (entry, 200, -1);
  gtk_editable_set_text (GTK_EDITABLE (entry), mail_info.comment);
  g_signal_connect (entry, "changed",
		      G_CALLBACK (comment_callback), &mail_info.comment);

  /* filename label  */
  label = gtk_label_new ("Filename:");
  gimp_table_attach (table, label, 
		    0, 1, 3, 4, 
		    GIMP_EXPAND | GIMP_FILL,
		    GIMP_EXPAND | GIMP_FILL,
		    0, 0);

  /* Filename dialog */
  entry = gtk_entry_new ();
  gimp_table_attach (table, entry, 
		    1, 3, 3, 4,
		    GIMP_EXPAND | GIMP_FILL,
		    GIMP_EXPAND | GIMP_FILL,
		    0, 0);
  gtk_widget_set_size_request (entry, 200, -1);
  gtk_editable_set_text (GTK_EDITABLE (entry), mail_info.filename);
  g_signal_connect (entry, "changed",
		      G_CALLBACK (filename_callback), &mail_info.filename);

  /* Encapsulation label */
  label = gtk_label_new ("Encapsulation:");
  gimp_table_attach (table, label ,
		    0, 1, 4, 5,
		    GIMP_EXPAND | GIMP_FILL,
		    GIMP_EXPAND | GIMP_FILL,
		    0, 0);

  /* Encapsulation radiobutton */
  button1 = gimp_radio_button_new (NULL, "Uuencode");
  group = button1;
  button2 = gimp_radio_button_new (group, "MIME" );
  if( mail_info.encapsulation == ENCAPSULATION_UUENCODE ) {
      gtk_check_button_set_active (GTK_CHECK_BUTTON (button1),TRUE);
  } else {
      gtk_check_button_set_active (GTK_CHECK_BUTTON (button2),TRUE);
  }
  g_signal_connect (button1, "toggled",
		      G_CALLBACK (encap_callback),
		      (gpointer) "uuencode" );
  g_signal_connect (button2, "toggled",
		      G_CALLBACK (encap_callback),
		      (gpointer) "mime" );

  gimp_table_attach (table, button1,
		    1, 2, 4, 5,
		    GIMP_EXPAND | GIMP_FILL, 
		    GIMP_EXPAND | GIMP_FILL,
		    0, 0 );

  gimp_table_attach (table, button2,
		    2, 3, 4, 5,
		    GIMP_EXPAND | GIMP_FILL,
		    GIMP_EXPAND | GIMP_FILL,
		    0, 0 );


  gtk_window_present (GTK_WINDOW (dlg));
  gimp_main_loop_run ();
  return run_flag;

}

static int 
valid_file (char *filename)
{
  int stat_res;
  GStatBuf buf;

  stat_res = g_stat (filename, &buf);

  if ((0 == stat_res) && (buf.st_size > 0))
    return 1;
  else
    return 0;
}

static char *
find_content_type (char *filename)
{
    /* This function returns a MIME Content-type: value based on the
       filename it is given.  */
    char *type_mappings[20] = {"gif" , "image/gif",
			       "jpg" , "image/jpeg",
			       "jpeg", "image/jpeg",
			       "tif" , "image/tiff",
			       "tiff", "image/tiff",
			       "png" , "image/png",
			       "g3"  , "image/g3fax",
			       "ps", "application/postscript",
			       "eps", "application/postscript",
			       NULL, NULL
    };

    char *ext;
    char *mimetype = malloc(100);
    int i=0;
    ext = find_extension(filename);
    if(!ext) {
	strcpy( mimetype, "application/octet-stream");
	return mimetype;
    }
    
    while( type_mappings[i] ) {
	if( strcmp( ext+1, type_mappings[i] ) == 0 ) {
	    strcpy(mimetype,type_mappings[i+1]);
	    return mimetype;
	}
	i += 2;
    }
    strcpy(mimetype,"image/x-");
    strncat(mimetype,ext+1,91);
    mimetype[99]='\0';
    return mimetype;

}

static char *
find_extension (char *filename)
{
  char *filename_copy;
  char *ext;

  /* this whole routine needs to be redone so it works for xccfgz and .gz files */
  /* not real sure where to start......                                         */
  /* right now saving for .xcfgz works but not .xcf.gz                          */
  /* this is all pretty close to straight from gz. It needs to be changed to    */
  /* work better for this plugin                                                */
  /* ie, FIXME */

  /* we never free this copy - aren't we evil! */
  filename_copy = malloc (strlen (filename) + 1);
  strcpy (filename_copy, filename);


  /* find the extension, boy! */
  ext = strrchr (filename_copy, '.');

  while (1)
    {
      if (!ext || ext[1] == 0 || strchr (ext, '/'))
	{
	  g_message ("mail: some sort of error with the file extension or lack thereof \n");
	  
	  return NULL;
	}
      if (0 != strcmp(ext,".gz"))
	{ 
	  return ext;
	}
      else
	{
	  /* we found somehting, loop back, and look again */
	  *ext = 0;
	  ext = strrchr (filename_copy, '.');
	}
    }
  return ext;
}


static void
close_callback (GtkWidget * widget, gpointer data)
{
  gimp_main_loop_quit ();
}

static void
ok_callback (GtkWidget * widget, gpointer data)
{
  run_flag = 1;
  gtk_window_destroy (GTK_WINDOW (data));
}

static void
encap_callback (GtkWidget * widget, gpointer data)
{
    /* Ignore the toggle-off signal, we are only interested in
       what is being set */
    if( ! gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)) ) {
	return;
    }
    if(strcmp(data,"uuencode")==0)
	mail_info.encapsulation = ENCAPSULATION_UUENCODE;
    if(strcmp(data,"mime")==0)
	mail_info.encapsulation = ENCAPSULATION_MIME;
}

static void
receipt_callback (GtkWidget * widget, gpointer data)
{
  g_strlcpy (mail_info.receipt, gtk_editable_get_text (GTK_EDITABLE (widget)), 256);
}

static void
subject_callback (GtkWidget * widget, gpointer data)
{
  g_strlcpy (mail_info.subject, gtk_editable_get_text (GTK_EDITABLE (widget)), 256);
}


static void
comment_callback (GtkWidget * widget, gpointer data)
{
  g_strlcpy (mail_info.comment, gtk_editable_get_text (GTK_EDITABLE (widget)), 256);
}


static void
filename_callback (GtkWidget * widget, gpointer data)
{
  g_strlcpy (mail_info.filename, gtk_editable_get_text (GTK_EDITABLE (widget)), 256);
}

static void
create_headers (FILE * mailpipe)
{
  /* create all the mail header stuff. Feel free to add your own */
  /* It is advisable to leave the X-Mailer header though, as     */
  /* there is a possibilty of a Gimp mail scanner/reader in the  */
  /* future. It will probabaly need that header.                 */

  fprintf (mailpipe, "To: %s \n", mail_info.receipt);
  fprintf (mailpipe, "Subject: %s \n", mail_info.subject);
  fprintf (mailpipe, "X-Mailer: GIMP Useless Mail Program v.65\n");
  fprintf (mailpipe, "X-GUMP-Author: Adrian Likins\n");

  if(mail_info.encapsulation == ENCAPSULATION_MIME ){
      fprintf (mailpipe, "MIME-Version: 1.0\n");
      fprintf (mailpipe, "Content-type: multipart/mixed; boundary=GUMP-MIME-boundary\n");
  }
  fprintf (mailpipe, "\n\n");
  if(mail_info.encapsulation == ENCAPSULATION_MIME ) {
      fprintf (mailpipe, "--GUMP-MIME-boundary\n");
      fprintf (mailpipe, "Content-type: text/plain; charset=US-ASCII\n\n");
  }
  fputs (mail_info.comment, mailpipe);
  fprintf (mailpipe, "\n\n");
  if(mail_info.encapsulation == ENCAPSULATION_MIME ) {
      char *content;
      content=find_content_type(mail_info.filename);
      fprintf (mailpipe, "--GUMP-MIME-boundary\n");
      fprintf (mailpipe, "Content-type: %s\n",content);
      fprintf (mailpipe, "Content-transfer-encoding: base64\n");
      fprintf (mailpipe, "Content-disposition: attachment; filename=\"%s\"\n",mail_info.filename);
      fprintf (mailpipe, "Content-description: %s\n\n",mail_info.filename);
      free(content);
  }
}

/*
 * The following code taken from codes.c in the mpack-1.5 distribution
 * by Carnegie Mellon University. 
 * 
 *
 * (C) Copyright 1993,1994 by Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of Carnegie
 * Mellon University not be used in advertising or publicity
 * pertaining to distribution of the software without specific,
 * written prior permission.  Carnegie Mellon University makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */
/*
Copyright (c) 1991 Bell Communications Research, Inc. (Bellcore)

Permission to use, copy, modify, and distribute this material 
for any purpose and without fee is hereby granted, provided 
that the above copyright notice and this permission notice 
appear in all copies, and that the name of Bellcore not be 
used in advertising or publicity pertaining to this 
material without the specific, prior written permission 
of an authorized representative of Bellcore.  BELLCORE 
MAKES NO REPRESENTATIONS ABOUT THE ACCURACY OR SUITABILITY 
OF THIS MATERIAL FOR ANY PURPOSE.  IT IS PROVIDED "AS IS", 
WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES.  */


static char basis_64[] =
   "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int to64(FILE *infile, FILE *outfile)
{
    int c1, c2, c3, ct=0, written=0;

    while ((c1 = getc(infile)) != EOF) {
        c2 = getc(infile);
        if (c2 == EOF) {
            output64chunk(c1, 0, 0, 2, outfile);
        } else {
            c3 = getc(infile);
            if (c3 == EOF) {
                output64chunk(c1, c2, 0, 1, outfile);
            } else {
                output64chunk(c1, c2, c3, 0, outfile);
            }
        }
        ct += 4;
        if (ct > 71) {
            putc('\n', outfile);
	    written += 73;
            ct = 0;
        }
    }
    if (ct) {
	putc('\n', outfile);
	ct++;
    }
    return written + ct;
}

static void
output64chunk(int c1, int c2, int c3, int pads, FILE *outfile)
{
    putc(basis_64[c1>>2], outfile);
    putc(basis_64[((c1 & 0x3)<< 4) | ((c2 & 0xF0) >> 4)], outfile);
    if (pads == 2) {
        putc('=', outfile);
        putc('=', outfile);
    } else if (pads) {
        putc(basis_64[((c2 & 0xF) << 2) | ((c3 & 0xC0) >>6)], outfile);
        putc('=', outfile);
    } else {
        putc(basis_64[((c2 & 0xF) << 2) | ((c3 & 0xC0) >>6)], outfile);
        putc(basis_64[c3 & 0x3F], outfile);
    }
}










