/* The GIMP -- an image manipulation program
 * Copyright (C) 1995, 1996, 1997 Spencer Kimball and Peter Mattis
 * Copyright (C) 1997 Josh MacDonald
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
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <glib/gstdio.h>

#include "appenv.h"
#include "actionarea.h"
#include "gdisplay.h"
#include "general.h"
#include "gimage.h"
#include "fileops.h"
#include "interface.h"
#include "menus.h"
#include "plug_in.h"
#include "procedural_db.h"
#include "gimprc.h"

static Argument* register_load_handler_invoker (Argument *args);
static Argument* register_magic_load_handler_invoker (Argument *args);
static Argument* register_save_handler_invoker (Argument *args);
static Argument* file_load_invoker             (Argument *args);
static Argument* file_save_invoker             (Argument *args);
static Argument* file_temp_name_invoker        (Argument *args);

static void file_convert_string (char *instr,
                                 char *outmem,
                                 int maxmem,
                                 int *nmem);

static int  file_check_single_magic (char *offset,
                                     char *type,
                                     char *value,
                                     int headsize,
                                     unsigned char *file_head,
                                     FILE *ifp);

static int  file_check_magic_list (GSList *magics_list,
                                   int headsize,
                                   unsigned char *head,
                                   FILE *ifp);

static PlugInProcDef* file_proc_find         (GSList *procs,
					 char   *filename);


/*  The folder the last file was opened from or saved to.  */
static GFile *last_folder = NULL;

/* Load by extension.
 */
static ProcArg file_load_args[] =
{
  { PDB_INT32, "run_mode", "Interactive, non-interactive." },
  { PDB_STRING, "filename", "The name of the file to load." },
  { PDB_STRING, "raw_filename", "The name entered." },
};

static ProcArg file_load_return_vals[] =
{
  { PDB_IMAGE, "image", "Output image." },
};

static ProcRecord file_load_proc =
{
  "gimp_file_load",
  "Loads a file by extension",
  "This procedure invokes the correct file load handler according to the file's extension and/or prefix.  The name of the file to load is typically a full pathname, and the name entered is what the user actually typed before prepending a directory path.  The reason for this is that if the user types http://www.xcf/~gimp he wants to fetch a URL, and the full pathname will not look like a URL.",
  "Josh MacDonald",
  "Josh MacDonald",
  "1997",
  PDB_INTERNAL,
  3,
  file_load_args,
  1,
  file_load_return_vals,
  { { file_load_invoker } },
};

/* Save by extension.
 */
static ProcArg file_save_args[] =
{
  { PDB_INT32, "run_mode", "Interactive, non-interactive" },
  { PDB_IMAGE, "image", "Input image" },
  { PDB_DRAWABLE, "drawable", "Drawable to save" },
  { PDB_STRING, "filename", "The name of the file to save the image in" },
  { PDB_STRING, "raw_filename", "The name of the file to save the image in" }
};

static ProcRecord file_save_proc =
{
  "gimp_file_save",
  "Saves a file by extension",
  "This procedure invokes the correct file save handler according to the file's extension and/or prefix.  The name of the file to save is typically a full pathname, and the name entered is what the user actually typed before prepending a directory path.  The reason for this is that if the user types http://www.xcf/~gimp he wants to fetch a URL, and the full pathname will not look like a URL.",
  "Josh MacDonald",
  "Josh MacDonald",
  "1997",
  PDB_INTERNAL,
  5,
  file_save_args,
  0,
  NULL,
  { { file_save_invoker } },
};

/* Temp name.
 */

static ProcArg file_temp_name_args[] =
{
  { PDB_STRING, "extension", "The extension the file will have." }
};

static ProcArg file_temp_name_values[] =
{
  { PDB_STRING, "name", "The temp name." }
};

static ProcRecord file_temp_name_proc =
{
  "gimp_temp_name",
  "Generates a unique filename.",
  "Generates a unique filename using the temp path supplied in the user's gimprc.",
  "Josh MacDonald",
  "Josh MacDonald",
  "1997",
  PDB_INTERNAL,
  1,
  file_temp_name_args,
  1,
  file_temp_name_values,
  { { file_temp_name_invoker } },
};

/* Register magic load handler.
 */

static ProcArg register_magic_load_handler_args[] =
{
  { PDB_STRING,
    "procedure_name",
    "the name of the procedure to be used for loading" },
  { PDB_STRING,
    "extensions",
    "comma separated list of extensions this handler can load (ie. \"jpeg,jpg\")" },
  { PDB_STRING,
    "prefixes",
    "comma separated list of prefixes this handler can load (ie. \"http:,ftp:\")" },
  { PDB_STRING,
    "magics",
    "comma separated list of magic file information this handler can load (ie. \"0,string,GIF\")" },
};

static ProcRecord register_magic_load_handler_proc =
{
  "gimp_register_magic_load_handler",
  "Registers a file load handler procedure",
  "Registers a procedural database procedure to be called to load files of a \
particular file format using magic file information.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1995-1996",
  PDB_INTERNAL,
  4,
  register_magic_load_handler_args,
  0,
  NULL,
  { { register_magic_load_handler_invoker } },
};

/* Register load handler.
 */

static ProcArg register_load_handler_args[] =
{
  { PDB_STRING,
    "procedure_name",
    "the name of the procedure to be used for loading" },
  { PDB_STRING,
    "extensions",
    "comma separated list of extensions this handler can load (ie. \"jpeg,jpg\")" },
  { PDB_STRING,
    "prefixes",
    "comma separated list of prefixes this handler can load (ie. \"http:,ftp:\")" },
};

static ProcRecord register_load_handler_proc =
{
  "gimp_register_load_handler",
  "Registers a file load handler procedure",
  "Registers a procedural database procedure to be called to load files of a particular file format.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1995-1996",
  PDB_INTERNAL,
  3,
  register_load_handler_args,
  0,
  NULL,
  { { register_load_handler_invoker } },
};

/* Register save handler.
 */

static ProcArg register_save_handler_args[] =
{
  { PDB_STRING,
    "procedure_name",
    "the name of the procedure to be used for saving" },
  { PDB_STRING,
    "extensions",
    "comma separated list of extensions this handler can save (ie. \"jpeg,jpg\")" },
  { PDB_STRING,
    "prefixes",
    "comma separated list of prefixes this handler can save (ie. \"http:,ftp:\")" },
};

static ProcRecord register_save_handler_proc =
{
  "gimp_register_save_handler",
  "Registers a file save handler procedure",
  "Registers a procedural database procedure to be called to save files in a particular file format.",
  "Spencer Kimball & Peter Mattis",
  "Spencer Kimball & Peter Mattis",
  "1995-1996",
  PDB_INTERNAL,
  3,
  register_save_handler_args,
  0,
  NULL,
  { { register_save_handler_invoker } },
};

static GSList *load_procs = NULL;
static GSList *save_procs = NULL;

static PlugInProcDef *load_file_proc = NULL;
static PlugInProcDef *save_file_proc = NULL;

static int image_ID = 0;

void
file_ops_pre_init ()
{
  procedural_db_register (&register_magic_load_handler_proc);
  procedural_db_register (&register_load_handler_proc);
  procedural_db_register (&register_save_handler_proc);
  procedural_db_register (&file_load_proc);
  procedural_db_register (&file_save_proc);
  procedural_db_register (&file_temp_name_proc);
}

void
file_ops_post_init ()
{
  /*  The file types were menus in the file dialogs; the type is now
   *  always found from the file's name and contents.
   */
  load_procs = g_slist_reverse (load_procs);
  save_procs = g_slist_reverse (save_procs);
}

static Argument*
register_load_handler_invoker (Argument *args)
{
  Argument fargs[4];

  memcpy ((char *)fargs, (char *)args, 3*sizeof (args[0]));
  fargs[3].arg_type = PDB_STRING;
  fargs[3].value.pdb_pointer = NULL;

  return (register_magic_load_handler_invoker (fargs));
}

static Argument*
register_magic_load_handler_invoker (Argument *args)
{
  Argument *return_args;
  ProcRecord *proc;
  PlugInProcDef* file_proc;
  int success;

  success = FALSE;

  proc = procedural_db_lookup ((char*) args[0].value.pdb_pointer);

  if (proc && ((proc->num_args < 3) ||
	       (proc->num_values < 1) ||
	       (proc->args[0].arg_type != PDB_INT32) ||
	       (proc->args[1].arg_type != PDB_STRING) ||
	       (proc->args[2].arg_type != PDB_STRING) ||
	       (proc->values[0].arg_type != PDB_IMAGE)))
    {
      g_message ("load handler \"%s\" does not take the standard load handler args",
		 (char*) args[0].value.pdb_pointer);
      goto done;
    }

  file_proc = plug_in_file_handler ((char*) args[0].value.pdb_pointer,
				    (char*) args[1].value.pdb_pointer,
				    (char*) args[2].value.pdb_pointer,
				    (char*) args[3].value.pdb_pointer);

  if (!file_proc)
    {
      g_message ("attempt to register non-existant load handler \"%s\"",
		 (char*) args[0].value.pdb_pointer);
      goto done;
    }

  load_procs = g_slist_prepend (load_procs, file_proc);

  success = TRUE;

done:

  return_args = procedural_db_return_args (&register_load_handler_proc, success);

  return return_args;
}

static Argument*
register_save_handler_invoker (Argument *args)
{
  Argument *return_args;
  ProcRecord *proc;
  PlugInProcDef* file_proc;
  int success;

  success = FALSE;

  proc = procedural_db_lookup ((char*) args[0].value.pdb_pointer);
  if (proc && ((proc->num_args < 5) ||
	       (proc->args[0].arg_type != PDB_INT32) ||
	       (proc->args[1].arg_type != PDB_IMAGE) ||
	       (proc->args[2].arg_type != PDB_DRAWABLE) ||
	       (proc->args[3].arg_type != PDB_STRING) ||
	       (proc->args[4].arg_type != PDB_STRING)))
    {
      g_message ("save handler \"%s\" does not take the standard save handler args",
		 (char*) args[0].value.pdb_pointer);
      goto done;
    }

  file_proc = plug_in_file_handler ((char*) args[0].value.pdb_pointer,
				    (char*) args[1].value.pdb_pointer,
				    (char*) args[2].value.pdb_pointer,
				    NULL);

  if (!file_proc)
    {
      g_message ("attempt to register non-existant save handler \"%s\"",
		 (char*) args[0].value.pdb_pointer);
      goto done;
    }

  save_procs = g_slist_prepend (save_procs, file_proc);

  success = TRUE;

done:
  return_args = procedural_db_return_args (&register_save_handler_proc, success);

  return return_args;
}

/*  A filter for every file type the plug-ins handle, and one for all
 *  of them together.
 */
static GListModel *
file_dialog_filters (GSList   *procs,
		     gboolean  for_save,
		     int       image_type)
{
  GListStore *filters;
  GtkFileFilter *all;
  GtkFileFilter *filter;
  GSList *list;

  filters = g_list_store_new (GTK_TYPE_FILE_FILTER);

  all = gtk_file_filter_new ();
  gtk_file_filter_set_name (all, for_save ? "All supported types"
					  : "All images");
  g_list_store_append (filters, all);

  for (list = procs; list; list = list->next)
    {
      PlugInProcDef *file_proc = list->data;
      GSList *ext;
      const char *name;

      if (!file_proc->extensions_list)
	continue;
      if (for_save && image_type &&
	  !(file_proc->image_types_val & image_type))
	continue;

      name = file_proc->menu_path ? prune_filename (file_proc->menu_path)
				  : file_proc->db_info.name;

      filter = gtk_file_filter_new ();
      gtk_file_filter_set_name (filter, name);

      for (ext = file_proc->extensions_list; ext; ext = ext->next)
	{
	  char *pattern = g_strdup_printf ("*.%s", (char *) ext->data);

	  gtk_file_filter_add_pattern (filter, pattern);
	  gtk_file_filter_add_pattern (all, pattern);
	  g_free (pattern);
	}

      g_list_store_append (filters, filter);
      g_object_unref (filter);
    }

  if (!for_save)
    {
      filter = gtk_file_filter_new ();
      gtk_file_filter_set_name (filter, "All files");
      gtk_file_filter_add_pattern (filter, "*");
      g_list_store_append (filters, filter);
      g_object_unref (filter);
    }

  g_object_unref (all);

  return G_LIST_MODEL (filters);
}

static void
file_dialog_set_sensitive (int sensitive)
{
  menus_set_sensitive ("<Toolbox>/File/Open", sensitive);
  menus_set_sensitive ("<Image>/File/Open", sensitive);
  menus_set_sensitive ("<Image>/File/Save", sensitive);
  menus_set_sensitive ("<Image>/File/Save as", sensitive);
}

static void
file_remember_folder (GFile *file)
{
  GFile *parent = g_file_get_parent (file);

  if (parent)
    {
      g_clear_object (&last_folder);
      last_folder = parent;
    }
}

static GtkWindow *
file_dialog_parent (void)
{
  GDisplay *gdisplay = gdisplay_active ();

  if (gdisplay && gdisplay->shell)
    return GTK_WINDOW (gdisplay->shell);

  return NULL;
}

static void
file_open_done (GObject      *source,
		GAsyncResult *result,
		gpointer      data)
{
  GFile *file;
  char *filename;
  char *raw_filename;

  file_dialog_set_sensitive (TRUE);

  file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source), result, NULL);
  if (!file)
    return;

  file_remember_folder (file);

  filename = g_file_get_path (file);
  g_object_unref (file);
  if (!filename)
    return;

  raw_filename = g_path_get_basename (filename);

  if (!file_open (filename, raw_filename))
    {
      char *message = g_strdup_printf ("Open failed: %s", raw_filename);

      message_box (message, NULL, NULL);
      g_free (message);
    }

  g_free (raw_filename);
  g_free (filename);
}

void
file_open_callback (GtkWidget *w,
		    gpointer   client_data)
{
  GtkFileDialog *dialog;
  GListModel *filters;

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, "Load Image");
  filters = file_dialog_filters (load_procs, FALSE, 0);
  gtk_file_dialog_set_filters (dialog, filters);
  g_object_unref (filters);
  if (last_folder)
    gtk_file_dialog_set_initial_folder (dialog, last_folder);

  load_file_proc = NULL;
  file_dialog_set_sensitive (FALSE);

  gtk_file_dialog_open (dialog, file_dialog_parent (), NULL,
			file_open_done, NULL);
  g_object_unref (dialog);
}

void
file_save_callback (GtkWidget *w,
		    gpointer   client_data)
{
  GDisplay *gdisplay;

  gdisplay = gdisplay_active ();
  if (!gdisplay)
    return;

  /*  Only save if the gimage has been modified  */
  if (gdisplay->gimage->dirty != 0)
    {
      if (gdisplay->gimage->has_filename == FALSE)
	{
	  popup_shell = gdisplay->shell;
	  file_save_as_callback (w, client_data);
	}
      else
	file_save (gdisplay->gimage->ID, gimage_filename (gdisplay->gimage),
		   prune_filename (gimage_filename(gdisplay->gimage)));
    }
}

static void
file_save_done (GObject      *source,
		GAsyncResult *result,
		gpointer      data)
{
  int save_image_ID = GPOINTER_TO_INT (data);
  GFile *file;
  char *filename;
  char *raw_filename;

  file_dialog_set_sensitive (TRUE);

  file = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (source), result, NULL);
  if (!file)
    return;

  file_remember_folder (file);

  filename = g_file_get_path (file);
  g_object_unref (file);
  if (!filename)
    return;

  raw_filename = g_path_get_basename (filename);

  /*  The dialog has already asked whether to overwrite.  */
  if (gimage_get_ID (save_image_ID) == NULL ||
      !file_save (save_image_ID, filename, raw_filename))
    {
      char *message;

      if (!save_file_proc && !file_proc_find (save_procs, raw_filename))
	message = g_strdup_printf ("Save failed: %s\n"
				   "The file type is chosen by the extension; "
				   "use one like .xcf, .png or .jpg.",
				   raw_filename);
      else
	message = g_strdup_printf ("Save failed: %s", raw_filename);

      message_box (message, NULL, NULL);
      g_free (message);
    }

  g_free (raw_filename);
  g_free (filename);
}

void
file_save_as_callback (GtkWidget *w,
		       gpointer   client_data)
{
  GtkFileDialog *dialog;
  GListModel *filters;
  GDisplay *gdisplay;
  int image_type = 0;

  gdisplay = gdisplay_active ();
  if (!gdisplay)
    return;

  image_ID = gdisplay->gimage->ID;

  switch (gdisplay->gimage->base_type)
    {
    case RGB:
      image_type = RGB_IMAGE;
      break;
    case GRAY:
      image_type = GRAY_IMAGE;
      break;
    case INDEXED:
      image_type = INDEXED_IMAGE;
      break;
    }

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, "Save Image");
  filters = file_dialog_filters (save_procs, TRUE, image_type);
  gtk_file_dialog_set_filters (dialog, filters);
  g_object_unref (filters);

  if (gdisplay->gimage->has_filename)
    {
      GFile *current = g_file_new_for_path (gimage_filename (gdisplay->gimage));

      gtk_file_dialog_set_initial_file (dialog, current);
      g_object_unref (current);
    }
  else
    {
      if (last_folder)
	gtk_file_dialog_set_initial_folder (dialog, last_folder);
      gtk_file_dialog_set_initial_name (dialog, "Untitled.xcf");
    }

  save_file_proc = NULL;
  file_dialog_set_sensitive (FALSE);

  gtk_file_dialog_save (dialog, GTK_WINDOW (gdisplay->shell), NULL,
			file_save_done, GINT_TO_POINTER (image_ID));
  g_object_unref (dialog);
}

void
file_load_by_extension_callback (GtkWidget *w,
				 gpointer   client_data)
{
  load_file_proc = NULL;
}

void
file_save_by_extension_callback (GtkWidget *w,
				 gpointer   client_data)
{
  save_file_proc = NULL;
}

int
file_open (char *filename, char* raw_filename)
{
  PlugInProcDef *file_proc;
  ProcRecord *proc;
  Argument *args;
  Argument *return_vals;
  GImage *gimage;
  int gimage_ID;
  int return_val;
  int i;

  file_proc = load_file_proc;
  if (!file_proc)
    file_proc = file_proc_find (load_procs, filename);

  if (!file_proc)
    {
      /* WARNING */
      return FALSE;
    }

  proc = &file_proc->db_info;

  args = g_new (Argument, proc->num_args);
  memset (args, 0, (sizeof (Argument) * proc->num_args));

  for (i = 0; i < proc->num_args; i++)
    args[i].arg_type = proc->args[i].arg_type;

  args[0].value.pdb_int = 0;
  args[1].value.pdb_pointer = filename;
  args[2].value.pdb_pointer = raw_filename;

  return_vals = procedural_db_execute (proc->name, args);
  return_val = (return_vals[0].value.pdb_int == PDB_SUCCESS);
  gimage_ID = return_vals[1].value.pdb_int;

  procedural_db_destroy_args (return_vals, proc->num_values);
  g_free (args);

  if ((gimage = gimage_get_ID (gimage_ID)) != NULL)
    {
      /*  enable & clear all undo steps  */
      gimage_enable_undo (gimage);

      /*  set the image to clean  */
      gimage_clean_all (gimage);

      /*  display the image */
      gdisplay_new (gimage, 0x0101);
    }

  return return_val;
}

int
file_save (int   image_ID,
	   char *filename,
	   char *raw_filename)
{
  PlugInProcDef *file_proc;
  ProcRecord *proc;
  Argument *args;
  Argument *return_vals;
  int return_val;
  GImage *gimage;
  int i;

  if ((gimage = gimage_get_ID (image_ID)) == NULL)
    return FALSE;
  if (gimage_active_drawable (gimage) == NULL)
    return FALSE;

  file_proc = save_file_proc;
  if (!file_proc)
    file_proc = file_proc_find (save_procs, raw_filename);

  if (!file_proc)
    return FALSE;

  proc = &file_proc->db_info;

  args = g_new (Argument, proc->num_args);
  memset (args, 0, (sizeof (Argument) * proc->num_args));

  for (i = 0; i < proc->num_args; i++)
    args[i].arg_type = proc->args[i].arg_type;

  args[0].value.pdb_int = 0;
  args[1].value.pdb_int = image_ID;
  args[2].value.pdb_int = drawable_ID (gimage_active_drawable (gimage));
  args[3].value.pdb_pointer = filename;
  args[4].value.pdb_pointer = raw_filename;

  return_vals = procedural_db_execute (proc->name, args);
  return_val = (return_vals[0].value.pdb_int == PDB_SUCCESS);

  if (return_val)
    {
      /*  set this image to clean  */
      gimage_clean_all (gimage);

      /*  set the image title  */
      gimage_set_filename (gimage, filename);
    }

  g_free (return_vals);
  g_free (args);

  return return_val;
}


static PlugInProcDef*
file_proc_find (GSList *procs,
		char   *filename)
{
  PlugInProcDef *file_proc, *size_matched_proc;
  GSList *all_procs = procs;
  GSList *extensions;
  GSList *prefixes;
  char *extension;
  char *p1, *p2;
  FILE *ifp = NULL;
  int head_size = -2, size_match_count = 0;
  int match_val;
  unsigned char head[256];

  size_matched_proc = NULL;

  extension = strrchr (filename, '.');
  if (extension)
    extension += 1;

  /* At first look for magics */
  while (procs)
    {
      file_proc = procs->data;
      procs = procs->next;

      if (file_proc->magics_list)
        {
          if (head_size == -2)
            {
              head_size = 0;
              if ((ifp = fopen (filename, "rb")) != NULL)
                head_size = fread ((char *)head, 1, sizeof (head), ifp);
            }
          if (head_size >= 4)
            {
              match_val = file_check_magic_list (file_proc->magics_list,
                                                 head_size, head, ifp);
              if (match_val == 2)  /* size match ? */
                { /* Use it only if no other magic matches */
                  size_match_count++;
                  size_matched_proc = file_proc;
                }
              else if (match_val)
                {
                  fclose (ifp);
                  return (file_proc);
                }
            }
        }
    }
  if (ifp) fclose (ifp);
  if (size_match_count == 1) return (size_matched_proc);

  procs = all_procs;
  while (procs)
    {
      file_proc = procs->data;
      procs = procs->next;

      for (prefixes = file_proc->prefixes_list; prefixes; prefixes = prefixes->next)
	{
	  p1 = filename;
	  p2 = (char*) prefixes->data;

	  if (strncmp (filename, prefixes->data, strlen (prefixes->data)) == 0)
	    return file_proc;
	}
     }

  procs = all_procs;
  while (procs)
    {
      file_proc = procs->data;
      procs = procs->next;

      for (extensions = file_proc->extensions_list; extension && extensions; extensions = extensions->next)
	{
	  p1 = extension;
	  p2 = (char*) extensions->data;

	  while (*p1 && *p2)
	    {
	      if (tolower (*p1) != tolower (*p2))
		break;
	      p1 += 1;
	      p2 += 1;
	    }
	  if (!(*p1) && !(*p2))
	    return file_proc;
	}
    }

  return NULL;
}

static void file_convert_string (char *instr,
                                 char *outmem,
                                 int maxmem,
                                 int *nmem)
{               /* Convert a string in C-notation to array of char */
  unsigned char *uin = (unsigned char *)instr;
  unsigned char *uout = (unsigned char *)outmem;
  unsigned char tmp[5], *tmpptr;
  int k;

  while ((*uin != '\0') && ((((char *)uout) - outmem) < maxmem))
    {
      if (*uin != '\\')   /* Not an escaped character ? */
        {
          *(uout++) = *(uin++);
          continue;
        }
      if (*(++uin) == '\0')
        {
          *(uout++) = '\\';
          break;
        }
      switch (*uin)
        {
          case '0':  case '1':  case '2':  case '3': /* octal */
            for (tmpptr = tmp; (tmpptr-tmp) <= 3;)
              {
                *(tmpptr++) = *(uin++);
                if (   (*uin == '\0') || (!isdigit (*uin))
                    || (*uin == '8') || (*uin == '9'))
                  break;
              }
            *tmpptr = '\0';
            sscanf ((char *)tmp, "%o", &k);
            *(uout++) = k;
            break;

          case 'a': *(uout++) = '\a'; uin++; break;
          case 'b': *(uout++) = '\b'; uin++; break;
          case 't': *(uout++) = '\t'; uin++; break;
          case 'n': *(uout++) = '\n'; uin++; break;
          case 'v': *(uout++) = '\v'; uin++; break;
          case 'f': *(uout++) = '\f'; uin++; break;
          case 'r': *(uout++) = '\r'; uin++; break;

          default : *(uout++) = *(uin++); break;
        }
    }
  *nmem = ((char *)uout) - outmem;
}

static int
file_check_single_magic (char *offset,
                         char *type,
                         char *value,
                         int headsize,
                         unsigned char *file_head,
                         FILE *ifp)

{ /* Return values are 0: no match, 1: magic match, 2: size match */
  long offs;
  unsigned long num_testval, num_operatorval;
  unsigned long fileval;
  int numbytes, k, c = 0, found = 0;
  char *num_operator_ptr, num_operator, num_test;
  unsigned char mem_testval[256];

  /* Check offset */
  if (sscanf (offset, "%ld", &offs) != 1) return (0);
  if (offs < 0) return (0);

  /* Check type of test */
  num_operator_ptr = NULL;
  num_operator = '\0';
  num_test = '=';
  if (strncmp (type, "byte", 4) == 0)
    {
      numbytes = 1;
      num_operator_ptr = type+4;
    }
  else if (strncmp (type, "short", 5) == 0)
    {
      numbytes = 2;
      num_operator_ptr = type+5;
    }
  else if (strncmp (type, "long", 4) == 0)
    {
      numbytes = 4;
      num_operator_ptr = type+4;
    }
  else if (strncmp (type, "size", 4) == 0)
    {
      numbytes = 5;
    }
  else if (strcmp (type, "string") == 0)
    {
      numbytes = 0;
    }
  else return (0);

  /* Check numerical operator value if present */
  if (num_operator_ptr && (*num_operator_ptr == '&'))
    {
      if (isdigit (num_operator_ptr[1]))
        {
          if (num_operator_ptr[1] != '0')      /* decimal */
            sscanf (num_operator_ptr+1, "%ld", &num_operatorval);
          else if (num_operator_ptr[2] == 'x') /* hexadecimal */
            sscanf (num_operator_ptr+3, "%lx", &num_operatorval);
          else                                 /* octal */
            sscanf (num_operator_ptr+2, "%lo", &num_operatorval);
          num_operator = *num_operator_ptr;
        }
    }

  if (numbytes > 0)   /* Numerical test ? */
    {
      /* Check test value */
      if ((value[0] == '=') || (value[0] == '>') || (value[0] == '<'))
      {
        num_test = value[0];
        value++;
      }
      if (!isdigit (value[0])) return (0);

      if (value[0] != '0')      /* decimal */
        num_testval = strtol(value, NULL, 10);
      else if (value[1] == 'x') /* hexadecimal */
        num_testval = strtol(value+2, NULL, 16);
      else                      /* octal */
        num_testval = strtol(value+1, NULL, 8);

      fileval = 0;
      if (numbytes == 5)    /* Check for file size ? */
        {struct stat buf;

          if (fstat (fileno (ifp), &buf) < 0) return (0);
          fileval = buf.st_size;
        }
      else if (offs + numbytes <= headsize)  /* We have it in memory ? */
        {
          for (k = 0; k < numbytes; k++)
          fileval = (fileval << 8) | (long)file_head[offs+k];
        }
      else   /* Read it from file */
        {
          if (fseek (ifp, offs, SEEK_SET) < 0) return (0);
          for (k = 0; k < numbytes; k++)
            fileval = (fileval << 8) | (c = getc (ifp));
          if (c == EOF) return (0);
        }
      if (num_operator == '&')
        fileval &= num_operatorval;

      if (num_test == '<')
        found = (fileval < num_testval);
      else if (num_test == '>')
        found = (fileval > num_testval);
      else
        found = (fileval == num_testval);

      if (found && (numbytes == 5)) found = 2;
    }
  else if (numbytes == 0) /* String test */
    {
      file_convert_string ((char *)value, (char *)mem_testval,
                           sizeof (mem_testval), &numbytes);
      if (numbytes <= 0) return (0);

      if (offs + numbytes <= headsize)  /* We have it in memory ? */
        {
          found = (memcmp (mem_testval, file_head+offs, numbytes) == 0);
        }
      else   /* Read it from file */
        {
          if (fseek (ifp, offs, SEEK_SET) < 0) return (0);
          found = 1;
          for (k = 0; found && (k < numbytes); k++)
            {
              c = getc (ifp);
              found = (c != EOF) && (c == (int)mem_testval[k]);
            }
        }
    }

  return (found);
}

static int file_check_magic_list (GSList *magics_list,
                                  int headsize,
                                  unsigned char *head,
                                  FILE *ifp)

{ /* Return values are 0: no match, 1: magic match, 2: size match */
  char *offset, *type, *value;
  int and = 0;
  int found = 0, match_val;

  while (magics_list)
    {
      if ((offset = (char *)magics_list->data) == NULL) break;
      if ((magics_list = magics_list->next) == NULL) break;
      if ((type = (char *)magics_list->data) == NULL) break;
      if ((magics_list = magics_list->next) == NULL) break;
      if ((value = (char *)magics_list->data) == NULL) break;
      magics_list = magics_list->next;

      match_val = file_check_single_magic (offset, type, value,
                                           headsize, head, ifp);
      if (and)
          found = found && match_val;
      else
          found = match_val;

      and = (strchr (offset, '&') != NULL);
      if ((!and) && found) return (match_val);
    }
  return (0);
}

static Argument*
file_load_invoker (Argument *args)
{
  PlugInProcDef *file_proc;
  ProcRecord *proc;

  file_proc = file_proc_find (load_procs, args[2].value.pdb_pointer);
  if (!file_proc)
    return procedural_db_return_args (&file_load_proc, FALSE);

  proc = &file_proc->db_info;

  return procedural_db_execute (proc->name, args);
}

static Argument*
file_save_invoker (Argument *args)
{
  Argument *new_args;
  Argument *return_vals;
  PlugInProcDef *file_proc;
  ProcRecord *proc;
  int i;

  file_proc = file_proc_find (save_procs, args[4].value.pdb_pointer);
  if (!file_proc)
    return procedural_db_return_args (&file_save_proc, FALSE);

  proc = &file_proc->db_info;

  new_args = g_new (Argument, proc->num_args);
  memset (new_args, 0, (sizeof (Argument) * proc->num_args));

  for (i = 0; i < proc->num_args; i++)
    new_args[i].arg_type = proc->args[i].arg_type;

  memcpy(new_args, args, (sizeof (Argument) * 5));

  return_vals = procedural_db_execute (proc->name, new_args);
  g_free (new_args);

  return return_vals;
}

static Argument*
file_temp_name_invoker (Argument *args)
{
  static gint id = 0;
  Argument *return_args;
  const char *dir;
  char *name;

  /*  temp-path from gimprc, if it exists, otherwise the system's  */
  dir = temp_path;
  if (!dir || !g_file_test (dir, G_FILE_TEST_IS_DIR))
    {
      if (dir && g_mkdir_with_parents (dir, 0755) == 0)
	;
      else
	dir = g_get_tmp_dir ();
    }

  name = g_strdup_printf ("gimp_temp.%lu%d.%s",
			  (unsigned long) g_get_real_time () % 100000,
			  id++, (char*)args[0].value.pdb_pointer);

  return_args = procedural_db_return_args (&file_temp_name_proc, TRUE);

  return_args[1].value.pdb_pointer = g_build_filename (dir, name, NULL);

  g_free (name);

  return return_args;
}
