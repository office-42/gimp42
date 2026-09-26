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

/*  The server listens on a TCP port with GIO sockets, which work the same
 *  on Unix and on Windows (winsock, set up by GIO).  Sockets are watched
 *  from a main context of the server's own, iterated by
 *  script_fu_server_listen () where the old code called select ().
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <glib/gstdio.h>
#include <gio/gio.h>

#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"
#include "gtk/gtk.h"
#include "siod.h"
#include "script-fu-server.h"

#define COMMAND_HEADER  3
#define RESPONSE_HEADER 4
#define MAGIC   'G'

#ifdef NO_DIFFTIME
#define difftime(a,b) (((double)(a)) - ((double)(b)))
#endif


/*  image information  */

/*  Header format for incoming commands...
 *    bytes: 1          2          3
 *           MAGIC      CMD_LEN_H  CMD_LEN_L
 */

/*  Header format for outgoing responses...
 *    bytes: 1          2          3          4
 *           MAGIC      ERROR?     RSP_LEN_H  RSP_LEN_L
 */

#define MAGIC_BYTE      0

#define CMD_LEN_H_BYTE  1
#define CMD_LEN_L_BYTE  2

#define ERROR           1
#define RSP_LEN_H_BYTE  2
#define RSP_LEN_L_BYTE  3

/*
 *  Local Structures
 */

typedef struct
{
  gchar   *command;
  GSocket *client;
  gint     request_no;
} SFCommand;

typedef struct
{
  GtkWidget *port_entry;
  GtkWidget *log_entry;

  gint       port;
  gchar     *logfile;

  gint       run;
} ServerInterface;

/*
 *  Local Functions
 */

static void     server_start       (gint       port,
				    gchar     *logfile);
static gint     execute_command    (SFCommand *cmd);
static gint     read_from_client   (GSocket   *client);
static GSocket *make_socket        (guint      port);
static void     server_log         (gchar     *format,
				    ...) G_GNUC_PRINTF (1, 2);
static void     server_quit        (void);

static gboolean server_accept      (GSocket      *socket,
				    GIOCondition  condition,
				    gpointer      data);
static gboolean server_client_input (GSocket      *socket,
				    GIOCondition  condition,
				    gpointer      data);
static gboolean server_timeout     (gpointer   data);

static gint     server_interface   (void);
static void     ok_callback        (GtkWidget *widget,
				    gpointer   data);
static void     cancel_callback    (GtkWidget *widget,
				    gpointer   data);

/*
 *  Global variables
 */
gint server_mode = FALSE;

/*
 *  Local variables
 */
static GSocket      *server_sock = NULL;
static GMainContext *server_context = NULL;
static GList        *command_queue = NULL;
static gint          queue_length = 0;
static gint          request_no = 0;
static FILE         *server_log_file = NULL;
static GHashTable   *clientname_ht = NULL;

static ServerInterface sint =
{
  NULL,  /*  port entry widget  */
  NULL,  /*  log entry widget  */

  10008, /*  default port number  */
  NULL,  /*  use stdout  */

  FALSE  /*  run  */
};

extern gint   script_fu_done;
extern char   siod_err_msg[];
extern LISP   repl_return_val;

/*
 *  Server interface functions
 */

void
script_fu_server_run (char     *name,
		      int       nparams,
		      GParam   *params,
		      int      *nreturn_vals,
		      GParam  **return_vals)
{
  static GParam values[1];
  GStatusType status = STATUS_SUCCESS;
  GRunModeType run_mode;

  run_mode = params[0].data.d_int32;

  switch (run_mode)
    {
    case RUN_INTERACTIVE:
      if (server_interface ())
	{
	  server_mode = TRUE;

	  /*  Start the server  */
	  server_start (sint.port, sint.logfile);
	}
      break;

    case RUN_NONINTERACTIVE:
      /*  Set server_mode to TRUE  */
      server_mode = TRUE;

      /*  Start the server  */
      server_start (params[1].data.d_int32, params[2].data.d_string);
      break;

    case RUN_WITH_LAST_VALS:
      status = STATUS_CALLING_ERROR;
      g_warning ("Script-Fu server does handle \"RUN_WITH_LAST_VALS\"");

    default:
      break;
    }

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
}

void
script_fu_server_listen (gint timeout)
{
  GSource *timeout_source = NULL;

  if (server_context == NULL)
    return;

  /*  Set the timeout  */
  if (timeout)
    {
      timeout_source = g_timeout_source_new (timeout);
      g_source_set_callback (timeout_source, server_timeout, NULL, NULL);
      g_source_attach (timeout_source, server_context);
    }

  /* Block until input arrives on one or more active sockets or timeout occurs. */
  g_main_context_iteration (server_context, TRUE);

  /* Service all the sockets with input pending. */
  while (g_main_context_iteration (server_context, FALSE))
    ;

  if (timeout_source)
    {
      g_source_destroy (timeout_source);
      g_source_unref (timeout_source);
    }
}

static gboolean
server_timeout (gpointer data)
{
  return G_SOURCE_CONTINUE;
}

static gboolean
server_accept (GSocket      *socket,
	       GIOCondition  condition,
	       gpointer      data)
{
  /* Connection request on original socket. */
  GSocket        *client;
  GSocketAddress *address;
  GSource        *source;
  gchar          *clientname = NULL;
  GError         *error = NULL;

  client = g_socket_accept (server_sock, NULL, &error);
  if (client == NULL)
    {
      g_printerr ("accept: %s\n", error->message);
      g_error_free (error);
      return G_SOURCE_CONTINUE;
    }

  g_socket_set_blocking (client, TRUE);

  /*  Associate the client address with the socket  */
  address = g_socket_get_remote_address (client, NULL);
  if (address && G_IS_INET_SOCKET_ADDRESS (address))
    clientname = g_inet_address_to_string
      (g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (address)));
  if (address)
    g_object_unref (address);

  g_hash_table_insert (clientname_ht, client, clientname);

  source = g_socket_create_source (client, G_IO_IN | G_IO_HUP | G_IO_ERR,
				   NULL);
  g_source_set_callback (source, (GSourceFunc) server_client_input,
			 NULL, NULL);
  g_source_attach (source, server_context);
  g_source_unref (source);

  return G_SOURCE_CONTINUE;
}

static gboolean
server_client_input (GSocket      *socket,
		     GIOCondition  condition,
		     gpointer      data)
{
  if (read_from_client (socket) < 0)
    {
      /*  Disassociate the client address with the socket  */
      g_socket_close (socket, NULL);
      g_hash_table_remove (clientname_ht, socket);

      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

static void
server_start (gint   port,
	      gchar *logfile)
{
  SFCommand *cmd;
  GSource   *source;
  GError    *error = NULL;

  /*  Set up the clientname hash table; it owns the client sockets  */
  clientname_ht = g_hash_table_new_full (g_direct_hash, NULL,
					 g_object_unref, g_free);

  /*  Setup up the server log file  */
  if (logfile && *logfile)
    server_log_file = g_fopen (logfile, "a");
  else
    server_log_file = NULL;
  if (server_log_file == NULL)
    server_log_file = stdout;

  /* Create the socket and set it up to accept connections. */
  server_sock = make_socket (port);
  g_socket_set_listen_backlog (server_sock, 5);
  if (! g_socket_listen (server_sock, &error))
    {
      g_printerr ("listen: %s\n", error->message);
      g_error_free (error);
      return;
    }

  server_log ("Script-fu initialized and listening...\n");

  /* Initialize the set of active sockets. */
  server_context = g_main_context_new ();

  source = g_socket_create_source (server_sock, G_IO_IN, NULL);
  g_source_set_callback (source, (GSourceFunc) server_accept, NULL, NULL);
  g_source_attach (source, server_context);
  g_source_unref (source);

  /*  Loop until the server is finished  */
  while (! script_fu_done)
    {
      script_fu_server_listen (0);

      while (command_queue)
	{
	  /*  Get the current command  */
	  cmd = (SFCommand *) command_queue->data;

	  /*  Process the command  */
	  execute_command (cmd);

	  /*  Remove the command from the list  */
	  command_queue = g_list_remove (command_queue, cmd);
	  queue_length--;

	  /*  Free the request  */
	  g_free (cmd->command);
	  g_object_unref (cmd->client);
	  g_free (cmd);
	}
    }

  server_quit ();

  /*  Close the server log file  */
  if (server_log_file != stdout)
    fclose (server_log_file);
}

/*  Writes all of buffer, or returns FALSE.  */
static gboolean
server_write (GSocket     *client,
	      const gchar *buffer,
	      gsize        length)
{
  GError *error = NULL;
  gssize  n;

  while (length > 0)
    {
      n = g_socket_send (client, buffer, length, NULL, &error);
      if (n < 0)
	{
	  /*  Write error  */
	  g_printerr ("write: %s\n", error->message);
	  g_error_free (error);
	  return FALSE;
	}

      buffer += n;
      length -= n;
    }

  return TRUE;
}

/*  Reads up to length bytes, waiting for them.  Returns the number read,
 *  which is less than length at end of file, or -1 on a read error.
 */
static gssize
server_read (GSocket *client,
	     gchar   *buffer,
	     gsize    length)
{
  GError *error = NULL;
  gsize   total = 0;
  gssize  n;

  while (total < length)
    {
      n = g_socket_receive (client, buffer + total, length - total,
			    NULL, &error);
      if (n < 0)
	{
	  /* Read error. */
	  g_printerr ("read: %s\n", error->message);
	  g_error_free (error);
	  return -1;
	}
      else if (n == 0)
	/* End-of-file. */
	break;

      total += n;
    }

  return total;
}

static gint
execute_command (SFCommand *cmd)
{
  guchar buffer[RESPONSE_HEADER];
  gchar *response;
  time_t clock1, clock2;
  gint response_len;
  gint error;

  /*  Get the client address from the address/socket table  */
  server_log ("Processing request #%d\n", cmd->request_no);
  time (&clock1);

  /*  run the command  */
  if (repl_c_string (cmd->command, 0, 0, 1) != 0)
    {
      error = TRUE;
      response_len = strlen (siod_err_msg);
      response = siod_err_msg;

      server_log ("%s\n", siod_err_msg);
    }
  else
    {
      error = FALSE;

      if (TYPEP (repl_return_val, tc_string))
	response = get_c_string (repl_return_val);
      else
	response = "Success";

      response_len = strlen (response);

      time (&clock2);
      server_log ("Request #%d processed in %f seconds, finishing on %s",
		  cmd->request_no, difftime (clock2, clock1), ctime (&clock2));
    }

  buffer[MAGIC_BYTE] = MAGIC;
  buffer[ERROR] = (error) ? 1 : 0;
  buffer[RSP_LEN_H_BYTE] = (guchar) (response_len >> 8);
  buffer[RSP_LEN_L_BYTE] = (guchar) (response_len & 0xFF);

  /*  Write the response to the client  */
  if (! server_write (cmd->client, (gchar *) buffer, RESPONSE_HEADER))
    return 0;

  if (! server_write (cmd->client, response, response_len))
    return 0;

  return 0;
}

static gint
read_from_client (GSocket *client)
{
  SFCommand *cmd;
  guchar buffer[COMMAND_HEADER];
  gchar *command;
  gchar *clientaddr;
  time_t clock;
  gint command_len;
  gssize nbytes;

  nbytes = server_read (client, (gchar *) buffer, COMMAND_HEADER);
  if (nbytes < 0)
    return 0;
  else if (nbytes < COMMAND_HEADER)
    /* End-of-file. */
    return -1;

  if (buffer[MAGIC_BYTE] != MAGIC)
    {
      server_log ("Error in script-fu command transmission.\n");
      return -1;
    }
  command_len = (buffer [CMD_LEN_H_BYTE] << 8) | buffer [CMD_LEN_L_BYTE];
  command = g_new (gchar, command_len + 1);

  nbytes = server_read (client, command, command_len);
  if (nbytes < command_len)
    {
      server_log ("Error reading command.  Read %d out of %d bytes.\n",
		  (gint) MAX (nbytes, 0), command_len);
      g_free (command);
      return -1;
    }

  command[command_len] = '\0';
  cmd = g_new (SFCommand, 1);
  cmd->client = g_object_ref (client);
  cmd->command = command;
  cmd->request_no = request_no ++;

  /*  Add the command to the queue  */
  command_queue = g_list_append (command_queue, cmd);
  queue_length ++;

  /*  Get the client address from the address/socket table  */
  clientaddr = g_hash_table_lookup (clientname_ht, cmd->client);
  time (&clock);
  server_log ("Received request #%d from IP address %s: %s on %s, [Request queue length: %d]",
	      cmd->request_no, clientaddr ? clientaddr : "(unknown)",
	      cmd->command, ctime (&clock), queue_length);

  return 0;
}

static GSocket *
make_socket (guint port)
{
  GSocket        *sock;
  GInetAddress   *any;
  GSocketAddress *name;
  GError         *error = NULL;

  /* Create the socket. */
  sock = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
		       G_SOCKET_PROTOCOL_TCP, &error);
  if (sock == NULL)
    {
      g_printerr ("socket: %s\n", error->message);
      gimp_quit ();
    }

  /* Give the socket a name. */
  any = g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);
  name = g_inet_socket_address_new (any, port);
  g_object_unref (any);

  if (! g_socket_bind (sock, name, TRUE, &error))
    {
      g_printerr ("bind: %s\n", error->message);
      gimp_quit ();
    }
  g_object_unref (name);

  return sock;
}

static void
server_log (gchar *format, ...)
{
  va_list args;
  char *buf;

  va_start (args, format);
  buf = g_strdup_vprintf (format, args);
  va_end (args);

  fputs (buf, server_log_file);
  if (server_log_file != stdout)
    fflush (server_log_file);

  g_free (buf);
}

static void
server_quit (void)
{
  GHashTableIter iter;
  gpointer       client;

  g_hash_table_iter_init (&iter, clientname_ht);
  while (g_hash_table_iter_next (&iter, &client, NULL))
    g_socket_shutdown (G_SOCKET (client), TRUE, TRUE, NULL);

  g_socket_shutdown (server_sock, TRUE, TRUE, NULL);
  g_socket_close (server_sock, NULL);
}

static gint
server_interface ()
{
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *label;
  GtkWidget *table;

  gtk_init ();

  dlg = gimp_dialog_new ("Script-Fu Server Options");
  g_signal_connect (dlg, "destroy",
		    G_CALLBACK (cancel_callback),
		    NULL);
  gimp_container_set_border_width (gimp_dialog_get_action_area (dlg), 2);

  /*  Action area  */
  gimp_dialog_add_button (dlg, "OK", G_CALLBACK (ok_callback), dlg, TRUE);

  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			    G_CALLBACK (gtk_window_destroy),
			    dlg);

  /*  The table to hold port & logfile entries  */
  table = gimp_table_new (2, 2, FALSE);
  gimp_container_set_border_width (table, 4);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), table, TRUE, TRUE, 0);

  /*  The server port  */
  label = gtk_label_new ("Server Port: ");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 0, 1,
		     GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 0, 1);

  sint.port_entry = gtk_entry_new ();
  gimp_table_attach (table, sint.port_entry, 1, 2, 0, 1,
		     GIMP_EXPAND | GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 1, 1);
  gtk_editable_set_text (GTK_EDITABLE (sint.port_entry), "10008");

  /*  The server logfile  */
  label = gtk_label_new ("Server Logfile: ");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gimp_table_attach (table, label, 0, 1, 1, 2,
		     GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 0, 1);

  sint.log_entry = gtk_entry_new ();
  gimp_table_attach (table, sint.log_entry, 1, 2, 1, 2,
		     GIMP_EXPAND | GIMP_SHRINK | GIMP_FILL, GIMP_SHRINK, 1, 1);

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return sint.run;
}

static void
ok_callback (GtkWidget *widget,
	     gpointer   data)
{
  sint.port = atoi (gtk_editable_get_text (GTK_EDITABLE (sint.port_entry)));
  sint.logfile = g_strdup (gtk_editable_get_text (GTK_EDITABLE (sint.log_entry)));
  sint.run = TRUE;

  gtk_window_destroy (GTK_WINDOW (data));
}

static void
cancel_callback (GtkWidget *widget,
		 gpointer   data)
{
  gimp_main_loop_quit ();
}
