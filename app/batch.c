#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "appenv.h"
#include "app_procs.h"
#include "batch.h"
#include "procedural_db.h"


static void batch_run_cmd  (char              *cmd);
static gboolean batch_read (GIOChannel        *channel,
			    GIOCondition       condition,
			    gpointer           data);


static ProcRecord *eval_proc;


void
batch_init ()
{
  extern char **batch_cmds;

  int read_from_stdin;
  int i;

  eval_proc = procedural_db_lookup ("extension_script_fu_eval");
  if (eval_proc &&
      (eval_proc->num_args < 2 ||
       eval_proc->args[0].arg_type != PDB_INT32 ||
       eval_proc->args[1].arg_type != PDB_STRING))
    {
      /*  whatever installed itself under that name is not script-fu  */
      eval_proc = NULL;
    }
  if (!eval_proc)
    {
      g_message ("script-fu not available: batch mode disabled\n");
      return;
    }

  read_from_stdin = FALSE;
  for (i = 0; batch_cmds[i]; i++)
    {
      if (strcmp (batch_cmds[i], "-") == 0)
	{
	  if (!read_from_stdin)
	    {
	      g_print ("reading batch commands from stdin\n");
	      {
		GIOChannel *channel;

#ifdef G_OS_WIN32
		channel = g_io_channel_win32_new_fd (0);
#else
		channel = g_io_channel_unix_new (0);
#endif
		g_io_channel_set_encoding (channel, NULL, NULL);
		g_io_add_watch (channel, G_IO_IN | G_IO_HUP, batch_read, NULL);
	      }
	      read_from_stdin = TRUE;
	    }
	}
      else
	{
	  batch_run_cmd (batch_cmds[i]);
	}
    }
}


static void
batch_run_cmd (char *cmd)
{
  Argument *args;
  Argument *vals;
  int i;

  if (g_strcasecmp (cmd, "(gimp-quit 0)") == 0)
    {
      app_exit (0);
      exit (0);
    }

  args = g_new0 (Argument, eval_proc->num_args);
  for (i = 0; i < eval_proc->num_args; i++)
    args[i].arg_type = eval_proc->args[i].arg_type;

  args[0].value.pdb_int = 1;
  args[1].value.pdb_pointer = cmd;

  vals = procedural_db_execute ("extension_script_fu_eval", args);
  if (!vals)
    {
      g_print ("batch command: experienced an execution error.\n");
      g_free (args);
      return;
    }
  switch (vals[0].value.pdb_int)
    {
    case PDB_EXECUTION_ERROR:
      g_print ("batch command: experienced an execution error.\n");
      break;
    case PDB_CALLING_ERROR:
      g_print ("batch command: experienced a calling error.\n");
      break;
    case PDB_SUCCESS:
      g_print ("batch command: executed successfully.\n");
      break;
    default:
      break;
    }
  
  procedural_db_destroy_args (vals, eval_proc->num_values);
  g_free(args);

  return;
}


static gboolean
batch_read (GIOChannel   *channel,
	    GIOCondition  condition,
	    gpointer      data)
{
  static GString *string;
  char buf[32];
  gsize nread = 0;
  gsize i;

  if (condition & (G_IO_IN | G_IO_HUP))
    {
      GIOStatus status;

      do {
	status = g_io_channel_read_chars (channel, buf, sizeof (buf),
					  &nread, NULL);
      } while (status == G_IO_STATUS_AGAIN);

      if (!string)
	string = g_string_new ("");

      /*  one command per line; leading white space is skipped  */
      for (i = 0; i < nread; i++)
	{
	  if ((buf[i] == '\n') || (buf[i] == '\r'))
	    {
	      if (string->len > 0)
		{
		  batch_run_cmd (string->str);
		  g_string_truncate (string, 0);
		}
	    }
	  else if (buf[i] == '\0' ||
		   (string->len == 0 && isspace ((guchar) buf[i])))
	    {
	      /*  skip  */
	    }
	  else
	    {
	      g_string_append_c (string, buf[i]);
	    }
	}

      if (nread == 0 || status == G_IO_STATUS_EOF ||
	  status == G_IO_STATUS_ERROR)
	{
	  /*  run what is left of an unterminated last line, then stop  */
	  if (string->len > 0)
	    {
	      batch_run_cmd (string->str);
	      g_string_truncate (string, 0);
	    }
	  app_exit (FALSE);
	  return G_SOURCE_REMOVE;
	}
    }

  return G_SOURCE_CONTINUE;
}
