/* LIBGIMP - The GIMP Library                                                   
 * Copyright (C) 1995-1997 Peter Mattis and Spencer Kimball                
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.             
 *                                                                              
 * This library is distributed in the hope that it will be useful,              
 * but WITHOUT ANY WARRANTY; without even the implied warranty of               
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU            
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */                                                                             
#include "gimpenums.h"
#include "gimpprotocol.h"
#include "gimpwire.h"


static void _gp_quit_read                (GIOChannel *channel, WireMessage *msg);
static void _gp_quit_write               (GIOChannel *channel, WireMessage *msg);
static void _gp_quit_destroy             (WireMessage *msg);
static void _gp_config_read              (GIOChannel *channel, WireMessage *msg);
static void _gp_config_write             (GIOChannel *channel, WireMessage *msg);
static void _gp_config_destroy           (WireMessage *msg);
static void _gp_tile_req_read            (GIOChannel *channel, WireMessage *msg);
static void _gp_tile_req_write           (GIOChannel *channel, WireMessage *msg);
static void _gp_tile_req_destroy         (WireMessage *msg);
static void _gp_tile_ack_read            (GIOChannel *channel, WireMessage *msg);
static void _gp_tile_ack_write           (GIOChannel *channel, WireMessage *msg);
static void _gp_tile_ack_destroy         (WireMessage *msg);
static void _gp_tile_data_read           (GIOChannel *channel, WireMessage *msg);
static void _gp_tile_data_write          (GIOChannel *channel, WireMessage *msg);
static void _gp_tile_data_destroy        (WireMessage *msg);
static void _gp_proc_run_read            (GIOChannel *channel, WireMessage *msg);
static void _gp_proc_run_write           (GIOChannel *channel, WireMessage *msg);
static void _gp_proc_run_destroy         (WireMessage *msg);
static void _gp_proc_return_read         (GIOChannel *channel, WireMessage *msg);
static void _gp_proc_return_write        (GIOChannel *channel, WireMessage *msg);
static void _gp_proc_return_destroy      (WireMessage *msg);
static void _gp_temp_proc_run_read       (GIOChannel *channel, WireMessage *msg);
static void _gp_temp_proc_run_write      (GIOChannel *channel, WireMessage *msg);
static void _gp_temp_proc_run_destroy    (WireMessage *msg);
static void _gp_temp_proc_return_read    (GIOChannel *channel, WireMessage *msg);
static void _gp_temp_proc_return_write   (GIOChannel *channel, WireMessage *msg);
static void _gp_temp_proc_return_destroy (WireMessage *msg);
static void _gp_proc_install_read        (GIOChannel *channel, WireMessage *msg);
static void _gp_proc_install_write       (GIOChannel *channel, WireMessage *msg);
static void _gp_proc_install_destroy     (WireMessage *msg);
static void _gp_proc_uninstall_read      (GIOChannel *channel, WireMessage *msg);
static void _gp_proc_uninstall_write     (GIOChannel *channel, WireMessage *msg);
static void _gp_proc_uninstall_destroy   (WireMessage *msg);
static void _gp_extension_ack_read       (GIOChannel *channel, WireMessage *msg);
static void _gp_extension_ack_write      (GIOChannel *channel, WireMessage *msg);
static void _gp_extension_ack_destroy    (WireMessage *msg);
static int  _gp_params_read              (GIOChannel *channel, GPParam **params, guint *nparams);
static void _gp_params_write             (GIOChannel *channel, GPParam *params, int nparams);
       void _gp_params_destroy           (GPParam *params, int nparams);

/*  in gimpwire.c: marks the stream as broken  */

/*  Sanity limits for what the other side sends.  The pipe peer is another
 *  process (a plug-in for the GIMP, the GIMP for a plug-in) and whatever
 *  it claims must not make us allocate absurd amounts or overflow.
 */
#define GP_MAX_PARAMS       65536
#define GP_MAX_ARRAY_BYTES  (256 * 1024 * 1024)
#define GP_MAX_TILE_SIDE    1024
#define GP_MAX_TILE_BPP     4


void
gp_init ()
{
  wire_register (GP_QUIT,
		 _gp_quit_read,
		 _gp_quit_write,
		 _gp_quit_destroy);
  wire_register (GP_CONFIG,
		 _gp_config_read,
		 _gp_config_write,
		 _gp_config_destroy);
  wire_register (GP_TILE_REQ,
		 _gp_tile_req_read,
		 _gp_tile_req_write,
		 _gp_tile_req_destroy);
  wire_register (GP_TILE_ACK,
		 _gp_tile_ack_read,
		 _gp_tile_ack_write,
		 _gp_tile_ack_destroy);
  wire_register (GP_TILE_DATA,
		 _gp_tile_data_read,
		 _gp_tile_data_write,
		 _gp_tile_data_destroy);
  wire_register (GP_PROC_RUN,
		 _gp_proc_run_read,
		 _gp_proc_run_write,
		 _gp_proc_run_destroy);
  wire_register (GP_PROC_RETURN,
		 _gp_proc_return_read,
		 _gp_proc_return_write,
		 _gp_proc_return_destroy);
  wire_register (GP_TEMP_PROC_RUN,
		 _gp_temp_proc_run_read,
		 _gp_temp_proc_run_write,
		 _gp_temp_proc_run_destroy);
  wire_register (GP_TEMP_PROC_RETURN,
		 _gp_temp_proc_return_read,
		 _gp_temp_proc_return_write,
		 _gp_temp_proc_return_destroy);
  wire_register (GP_PROC_INSTALL,
		 _gp_proc_install_read,
		 _gp_proc_install_write,
		 _gp_proc_install_destroy);
  wire_register (GP_PROC_UNINSTALL,
		 _gp_proc_uninstall_read,
		 _gp_proc_uninstall_write,
		 _gp_proc_uninstall_destroy);
  wire_register (GP_EXTENSION_ACK,
		 _gp_extension_ack_read,
		 _gp_extension_ack_write,
		 _gp_extension_ack_destroy);
}

int
gp_quit_write (GIOChannel *channel)
{
  WireMessage msg;

  msg.type = GP_QUIT;
  msg.data = NULL;

  if (!wire_write_msg (channel, &msg))
    return FALSE;
  if (!wire_flush (channel))
    return FALSE;
  return TRUE;
}

int
gp_config_write (GIOChannel *channel,
		 GPConfig *config)
{
  WireMessage msg;

  msg.type = GP_CONFIG;
  msg.data = config;

  if (!wire_write_msg (channel, &msg))
    return FALSE;
  if (!wire_flush (channel))
    return FALSE;
  return TRUE;
}

int
gp_tile_req_write (GIOChannel *channel,
		   GPTileReq *tile_req)
{
  WireMessage msg;

  msg.type = GP_TILE_REQ;
  msg.data = tile_req;

  if (!wire_write_msg (channel, &msg))
    return FALSE;
  if (!wire_flush (channel))
    return FALSE;
  return TRUE;
}

int
gp_tile_ack_write (GIOChannel *channel)
{
  WireMessage msg;

  msg.type = GP_TILE_ACK;
  msg.data = NULL;

  if (!wire_write_msg (channel, &msg))
    return FALSE;
  if (!wire_flush (channel))
    return FALSE;
  return TRUE;
}

int
gp_tile_data_write (GIOChannel *channel,
		    GPTileData *tile_data)
{
  WireMessage msg;

  msg.type = GP_TILE_DATA;
  msg.data = tile_data;

  if (!wire_write_msg (channel, &msg))
    return FALSE;
  if (!wire_flush (channel))
    return FALSE;
  return TRUE;
}

int
gp_proc_run_write (GIOChannel *channel,
		   GPProcRun *proc_run)
{
  WireMessage msg;

  msg.type = GP_PROC_RUN;
  msg.data = proc_run;

  if (!wire_write_msg (channel, &msg))
    return FALSE;
  if (!wire_flush (channel))
    return FALSE;
  return TRUE;
}

int
gp_proc_return_write (GIOChannel *channel,
		      GPProcReturn *proc_return)
{
  WireMessage msg;

  msg.type = GP_PROC_RETURN;
  msg.data = proc_return;

  if (!wire_write_msg (channel, &msg))
    return FALSE;
  if (!wire_flush (channel))
    return FALSE;
  return TRUE;
}

int
gp_temp_proc_run_write (GIOChannel *channel,
			GPProcRun *proc_run)
{
  WireMessage msg;

  msg.type = GP_TEMP_PROC_RUN;
  msg.data = proc_run;

  if (!wire_write_msg (channel, &msg))
    return FALSE;
  if (!wire_flush (channel))
    return FALSE;
  return TRUE;
}

int
gp_temp_proc_return_write (GIOChannel *channel,
			   GPProcReturn *proc_return)
{
  WireMessage msg;

  msg.type = GP_TEMP_PROC_RETURN;
  msg.data = proc_return;

  if (!wire_write_msg (channel, &msg))
    return FALSE;
  if (!wire_flush (channel))
    return FALSE;
  return TRUE;
}

int
gp_proc_install_write (GIOChannel *channel,
		       GPProcInstall *proc_install)
{
  WireMessage msg;

  msg.type = GP_PROC_INSTALL;
  msg.data = proc_install;

  if (!wire_write_msg (channel, &msg))
    return FALSE;
  if (!wire_flush (channel))
    return FALSE;
  return TRUE;
}

int
gp_proc_uninstall_write (GIOChannel *channel,
			 GPProcUninstall *proc_uninstall)
{
  WireMessage msg;

  msg.type = GP_PROC_UNINSTALL;
  msg.data = proc_uninstall;

  if (!wire_write_msg (channel, &msg))
    return FALSE;
  if (!wire_flush (channel))
    return FALSE;
  return TRUE;
}

int
gp_extension_ack_write (GIOChannel *channel)
{
  WireMessage msg;

  msg.type = GP_EXTENSION_ACK;
  msg.data = NULL;

  if (!wire_write_msg (channel, &msg))
    return FALSE;
  if (!wire_flush (channel))
    return FALSE;
  return TRUE;
}


static void
_gp_quit_read (GIOChannel *channel, WireMessage *msg)
{
}

static void
_gp_quit_write (GIOChannel *channel, WireMessage *msg)
{
}

static void
_gp_quit_destroy (WireMessage *msg)
{
}

static void
_gp_config_read (GIOChannel *channel, WireMessage *msg)
{
  GPConfig *config;

  config = g_new0 (GPConfig, 1);
  if (!wire_read_int32 (channel, &config->version, 1) ||
      !wire_read_int32 (channel, &config->tile_width, 1) ||
      !wire_read_int32 (channel, &config->tile_height, 1) ||
      !wire_read_int32 (channel, (guint32*) &config->shm_ID, 1) ||
      !wire_read_double (channel, &config->gamma, 1) ||
      !wire_read_int8 (channel, (guint8*) &config->install_cmap, 1) ||
      !wire_read_int8 (channel, (guint8*) config->color_cube, 3))
    {
      g_free (config);
      return;
    }

  msg->data = config;
}

static void
_gp_config_write (GIOChannel *channel, WireMessage *msg)
{
  GPConfig *config;

  config = msg->data;
  if (!wire_write_int32 (channel, &config->version, 1))
    return;
  if (!wire_write_int32 (channel, &config->tile_width, 1))
    return;
  if (!wire_write_int32 (channel, &config->tile_height, 1))
    return;
  if (!wire_write_int32 (channel, (guint32*) &config->shm_ID, 1))
    return;
  if (!wire_write_double (channel, &config->gamma, 1))
    return;
  if (!wire_write_int8 (channel, (guint8*) &config->install_cmap, 1))
    return;
  if (!wire_write_int8 (channel, (guint8*) config->color_cube, 3))
    return;
}

static void
_gp_config_destroy (WireMessage *msg)
{
  g_free (msg->data);
}

static void
_gp_tile_req_read (GIOChannel *channel, WireMessage *msg)
{
  GPTileReq *tile_req;

  tile_req = g_new0 (GPTileReq, 1);
  if (!wire_read_int32 (channel, (guint32*) &tile_req->drawable_ID, 1) ||
      !wire_read_int32 (channel, &tile_req->tile_num, 1) ||
      !wire_read_int32 (channel, &tile_req->shadow, 1))
    {
      g_free (tile_req);
      return;
    }

  msg->data = tile_req;
}

static void
_gp_tile_req_write (GIOChannel *channel, WireMessage *msg)
{
  GPTileReq *tile_req;

  tile_req = msg->data;
  if (!wire_write_int32 (channel, (guint32*) &tile_req->drawable_ID, 1))
    return;
  if (!wire_write_int32 (channel, &tile_req->tile_num, 1))
    return;
  if (!wire_write_int32 (channel, &tile_req->shadow, 1))
    return;
}

static void
_gp_tile_req_destroy (WireMessage *msg)
{
  g_free (msg->data);
}

static void
_gp_tile_ack_read (GIOChannel *channel, WireMessage *msg)
{
}

static void
_gp_tile_ack_write (GIOChannel *channel, WireMessage *msg)
{
}

static void
_gp_tile_ack_destroy (WireMessage *msg)
{
}

static void
_gp_tile_data_read (GIOChannel *channel, WireMessage *msg)
{
  GPTileData *tile_data;
  guint length;

  tile_data = g_new0 (GPTileData, 1);
  if (!wire_read_int32 (channel, (guint32*) &tile_data->drawable_ID, 1) ||
      !wire_read_int32 (channel, &tile_data->tile_num, 1) ||
      !wire_read_int32 (channel, &tile_data->shadow, 1) ||
      !wire_read_int32 (channel, &tile_data->bpp, 1) ||
      !wire_read_int32 (channel, &tile_data->width, 1) ||
      !wire_read_int32 (channel, &tile_data->height, 1) ||
      !wire_read_int32 (channel, &tile_data->use_shm, 1))
    goto fail;

  /*  keep width * height * bpp small and free of overflow; the receiver
   *  still has to check that it matches the tile it is meant for
   */
  if (tile_data->bpp > GP_MAX_TILE_BPP ||
      tile_data->width > GP_MAX_TILE_SIDE ||
      tile_data->height > GP_MAX_TILE_SIDE)
    {
      g_print ("gp_tile_data_read: bogus tile size %ux%ux%u\n",
	       tile_data->width, tile_data->height, tile_data->bpp);
      wire_set_error ();
      goto fail;
    }

  if (!tile_data->use_shm)
    {
      length = tile_data->width * tile_data->height * tile_data->bpp;
      if (length > 0)
	{
	  tile_data->data = g_new (guchar, length);
	  if (!wire_read_int8 (channel, (guint8*) tile_data->data, length))
	    goto fail;
	}
    }

  msg->data = tile_data;
  return;

 fail:
  g_free (tile_data->data);
  g_free (tile_data);
}

static void
_gp_tile_data_write (GIOChannel *channel, WireMessage *msg)
{
  GPTileData *tile_data;
  guint length;

  tile_data = msg->data;
  if (!wire_write_int32 (channel, (guint32*) &tile_data->drawable_ID, 1))
    return;
  if (!wire_write_int32 (channel, &tile_data->tile_num, 1))
    return;
  if (!wire_write_int32 (channel, &tile_data->shadow, 1))
    return;
  if (!wire_write_int32 (channel, &tile_data->bpp, 1))
    return;
  if (!wire_write_int32 (channel, &tile_data->width, 1))
    return;
  if (!wire_write_int32 (channel, &tile_data->height, 1))
    return;
  if (!wire_write_int32 (channel, &tile_data->use_shm, 1))
    return;

  if (!tile_data->use_shm)
    {
      length = tile_data->width * tile_data->height * tile_data->bpp;
      if (!wire_write_int8 (channel, (guint8*) tile_data->data, length))
	return;
    }
}

static void
_gp_tile_data_destroy (WireMessage *msg)
{
  GPTileData *tile_data;

  tile_data = msg->data;
  if (tile_data->data)
    g_free (tile_data->data);
  g_free (tile_data);
}

static void
_gp_proc_run_read (GIOChannel *channel, WireMessage *msg)
{
  GPProcRun *proc_run;

  proc_run = g_new0 (GPProcRun, 1);
  if (!wire_read_string (channel, &proc_run->name, 1))
    {
      g_free (proc_run);
      return;
    }
  if (!proc_run->name)
    {
      /*  a procedure without a name makes no sense  */
      wire_set_error ();
      g_free (proc_run);
      return;
    }
  if (!_gp_params_read (channel, &proc_run->params, (guint*) &proc_run->nparams))
    {
      g_free (proc_run->name);
      g_free (proc_run);
      return;
    }

  msg->data = proc_run;
}

static void
_gp_proc_run_write (GIOChannel *channel, WireMessage *msg)
{
  GPProcRun *proc_run;

  proc_run = msg->data;

  if (!wire_write_string (channel, &proc_run->name, 1))
    return;
  _gp_params_write (channel, proc_run->params, proc_run->nparams);
}

static void
_gp_proc_run_destroy (WireMessage *msg)
{
  GPProcRun *proc_run;

  proc_run = msg->data;
  _gp_params_destroy (proc_run->params, proc_run->nparams);
  g_free (proc_run->name);
  g_free (proc_run);
}

static void
_gp_proc_return_read (GIOChannel *channel, WireMessage *msg)
{
  GPProcReturn *proc_return;

  proc_return = g_new0 (GPProcReturn, 1);
  if (!wire_read_string (channel, &proc_return->name, 1))
    {
      g_free (proc_return);
      return;
    }
  if (!proc_return->name)
    {
      wire_set_error ();
      g_free (proc_return);
      return;
    }
  if (!_gp_params_read (channel, &proc_return->params, (guint*) &proc_return->nparams))
    {
      g_free (proc_return->name);
      g_free (proc_return);
      return;
    }

  msg->data = proc_return;
}

static void
_gp_proc_return_write (GIOChannel *channel, WireMessage *msg)
{
  GPProcReturn *proc_return;

  proc_return = msg->data;

  if (!wire_write_string (channel, &proc_return->name, 1))
    return;
  _gp_params_write (channel, proc_return->params, proc_return->nparams);
}

static void
_gp_proc_return_destroy (WireMessage *msg)
{
  GPProcReturn *proc_return;

  proc_return = msg->data;
  _gp_params_destroy (proc_return->params, proc_return->nparams);
  g_free (proc_return);
}

static void
_gp_temp_proc_run_read (GIOChannel *channel, WireMessage *msg)
{
  _gp_proc_run_read (channel, msg);
}

static void
_gp_temp_proc_run_write (GIOChannel *channel, WireMessage *msg)
{
  _gp_proc_run_write (channel, msg);
}

static void
_gp_temp_proc_run_destroy (WireMessage *msg)
{
  _gp_proc_run_destroy (msg);
}

static void
_gp_temp_proc_return_read (GIOChannel *channel, WireMessage *msg)
{
  _gp_proc_return_read (channel, msg);
}

static void
_gp_temp_proc_return_write (GIOChannel *channel, WireMessage *msg)
{
  _gp_proc_return_write (channel, msg);
}

static void
_gp_temp_proc_return_destroy (WireMessage *msg)
{
  _gp_proc_return_destroy (msg);
}

static void
_gp_proc_install_read (GIOChannel *channel, WireMessage *msg)
{
  GPProcInstall *proc_install;
  int i;

  proc_install = g_new0 (GPProcInstall, 1);
  msg->data = proc_install;

  if (!wire_read_string (channel, &proc_install->name, 1) ||
      !wire_read_string (channel, &proc_install->blurb, 1) ||
      !wire_read_string (channel, &proc_install->help, 1) ||
      !wire_read_string (channel, &proc_install->author, 1) ||
      !wire_read_string (channel, &proc_install->copyright, 1) ||
      !wire_read_string (channel, &proc_install->date, 1) ||
      !wire_read_string (channel, &proc_install->menu_path, 1) ||
      !wire_read_string (channel, &proc_install->image_types, 1))
    goto fail;

  if (!wire_read_int32 (channel, &proc_install->type, 1) ||
      !wire_read_int32 (channel, &proc_install->nparams, 1) ||
      !wire_read_int32 (channel, &proc_install->nreturn_vals, 1))
    {
      proc_install->nparams = 0;
      proc_install->nreturn_vals = 0;
      goto fail;
    }

  if (!proc_install->name ||
      proc_install->nparams > GP_MAX_PARAMS ||
      proc_install->nreturn_vals > GP_MAX_PARAMS)
    {
      g_print ("gp_proc_install_read: bogus procedure\n");
      proc_install->nparams = 0;
      proc_install->nreturn_vals = 0;
      wire_set_error ();
      goto fail;
    }

  proc_install->params = g_new0 (GPParamDef, proc_install->nparams);
  proc_install->return_vals = g_new0 (GPParamDef, proc_install->nreturn_vals);

  for (i = 0; i < proc_install->nparams; i++)
    {
      if (!wire_read_int32 (channel, (guint32*) &proc_install->params[i].type, 1) ||
	  !wire_read_string (channel, &proc_install->params[i].name, 1) ||
	  !wire_read_string (channel, &proc_install->params[i].description, 1))
	goto fail;
    }

  for (i = 0; i < proc_install->nreturn_vals; i++)
    {
      if (!wire_read_int32 (channel, (guint32*) &proc_install->return_vals[i].type, 1) ||
	  !wire_read_string (channel, &proc_install->return_vals[i].name, 1) ||
	  !wire_read_string (channel, &proc_install->return_vals[i].description, 1))
	goto fail;
    }

  return;

 fail:
  _gp_proc_install_destroy (msg);
  msg->data = NULL;
}

static void
_gp_proc_install_write (GIOChannel *channel, WireMessage *msg)
{
  GPProcInstall *proc_install;
  int i;

  proc_install = msg->data;

  if (!wire_write_string (channel, &proc_install->name, 1))
    return;
  if (!wire_write_string (channel, &proc_install->blurb, 1))
    return;
  if (!wire_write_string (channel, &proc_install->help, 1))
    return;
  if (!wire_write_string (channel, &proc_install->author, 1))
    return;
  if (!wire_write_string (channel, &proc_install->copyright, 1))
    return;
  if (!wire_write_string (channel, &proc_install->date, 1))
    return;
  if (!wire_write_string (channel, &proc_install->menu_path, 1))
    return;
  if (!wire_write_string (channel, &proc_install->image_types, 1))
    return;

  if (!wire_write_int32 (channel, &proc_install->type, 1))
    return;
  if (!wire_write_int32 (channel, &proc_install->nparams, 1))
    return;
  if (!wire_write_int32 (channel, &proc_install->nreturn_vals, 1))
    return;

  for (i = 0; i < proc_install->nparams; i++)
    {
      if (!wire_write_int32 (channel, (guint32*) &proc_install->params[i].type, 1))
	return;
      if (!wire_write_string (channel, &proc_install->params[i].name, 1))
	return;
      if (!wire_write_string (channel, &proc_install->params[i].description, 1))
	return;
    }

  for (i = 0; i < proc_install->nreturn_vals; i++)
    {
      if (!wire_write_int32 (channel, (guint32*) &proc_install->return_vals[i].type, 1))
	return;
      if (!wire_write_string (channel, &proc_install->return_vals[i].name, 1))
	return;
      if (!wire_write_string (channel, &proc_install->return_vals[i].description, 1))
	return;
    }
}

static void
_gp_proc_install_destroy (WireMessage *msg)
{
  GPProcInstall *proc_install;
  int i;

  proc_install = msg->data;

  g_free (proc_install->name);
  g_free (proc_install->blurb);
  g_free (proc_install->help);
  g_free (proc_install->author);
  g_free (proc_install->copyright);
  g_free (proc_install->date);
  g_free (proc_install->menu_path);
  g_free (proc_install->image_types);

  for (i = 0; i < proc_install->nparams; i++)
    {
      g_free (proc_install->params[i].name);
      g_free (proc_install->params[i].description);
    }

  for (i = 0; i < proc_install->nreturn_vals; i++)
    {
      g_free (proc_install->return_vals[i].name);
      g_free (proc_install->return_vals[i].description);
    }

  g_free (proc_install->params);
  g_free (proc_install->return_vals);
  g_free (proc_install);
}

static void
_gp_proc_uninstall_read (GIOChannel *channel, WireMessage *msg)
{
  GPProcUninstall *proc_uninstall;

  proc_uninstall = g_new0 (GPProcUninstall, 1);

  if (!wire_read_string (channel, &proc_uninstall->name, 1))
    {
      g_free (proc_uninstall);
      return;
    }
  if (!proc_uninstall->name)
    {
      wire_set_error ();
      g_free (proc_uninstall);
      return;
    }

  msg->data = proc_uninstall;
}

static void
_gp_proc_uninstall_write (GIOChannel *channel, WireMessage *msg)
{
  GPProcUninstall *proc_uninstall;

  proc_uninstall = msg->data;

  if (!wire_write_string (channel, &proc_uninstall->name, 1))
    return;
}

static void
_gp_proc_uninstall_destroy (WireMessage *msg)
{
  GPProcUninstall *proc_uninstall;

  proc_uninstall = msg->data;

  g_free (proc_uninstall->name);
  g_free (proc_uninstall);
}

/*  The length of the array in param i: by convention the INT32 just before
 *  it.  Returns -1 if there is no such count or it is out of range.
 */
static gint32
_gp_params_array_length (GPParam *params,
			 int      i,
			 gsize    element_size)
{
  gint32 count;

  if (i < 1 || params[i-1].type != PARAM_INT32)
    return -1;

  count = params[i-1].data.d_int32;
  if (count < 0 || (gsize) count > GP_MAX_ARRAY_BYTES / element_size)
    return -1;

  return count;
}

static int
_gp_params_read (GIOChannel *channel, GPParam **params, guint *nparams)
{
  guint32 n;
  gint32 count;
  int i;

  *params = NULL;
  *nparams = 0;

  if (!wire_read_int32 (channel, &n, 1))
    return FALSE;

  if (n == 0)
    return TRUE;

  if (n > GP_MAX_PARAMS)
    {
      g_print ("gp_params_read: too many params (%u)\n", n);
      wire_set_error ();
      return FALSE;
    }

  *params = g_new0 (GPParam, n);

  for (i = 0; i < n; i++)
    {
      /*  so that a failure below frees what has been read so far  */
      *nparams = i + 1;

      if (!wire_read_int32 (channel, (guint32*) &(*params)[i].type, 1))
	goto fail;

      if ((guint32) (*params)[i].type >= PARAM_END)
	{
	  g_print ("gp_params_read: unknown param type %u\n",
		   (guint32) (*params)[i].type);
	  /*  don't let _gp_params_destroy look at it  */
	  (*params)[i].type = PARAM_END;
	  wire_set_error ();
	  goto fail;
	}

      switch ((*params)[i].type)
	{
	case PARAM_INT32ARRAY:
	case PARAM_INT16ARRAY:
	case PARAM_INT8ARRAY:
	case PARAM_FLOATARRAY:
	case PARAM_STRINGARRAY:
	  count = _gp_params_array_length (*params, i, sizeof (gdouble));
	  if (count < 0)
	    {
	      g_print ("gp_params_read: array param %d without a valid length\n", i);
	      /*  its (NULL) data must not be freed with a bogus count  */
	      (*params)[i].type = PARAM_END;
	      wire_set_error ();
	      goto fail;
	    }
	  break;
	default:
	  count = 0;
	  break;
	}

      switch ((*params)[i].type)
	{
	case PARAM_INT32:
	  if (!wire_read_int32 (channel, (guint32*) &(*params)[i].data.d_int32, 1))
	    goto fail;
	  break;
	case PARAM_INT16:
	  if (!wire_read_int16 (channel, (guint16*) &(*params)[i].data.d_int16, 1))
	    goto fail;
	  break;
	case PARAM_INT8:
	  if (!wire_read_int8 (channel, (guint8*) &(*params)[i].data.d_int8, 1))
	    goto fail;
	  break;
        case PARAM_FLOAT:
	  if (!wire_read_double (channel, &(*params)[i].data.d_float, 1))
	    goto fail;
          break;
        case PARAM_STRING:
	  if (!wire_read_string (channel, &(*params)[i].data.d_string, 1))
	    goto fail;
          break;
        case PARAM_INT32ARRAY:
	  (*params)[i].data.d_int32array = g_try_new0 (gint32, count);
	  if (count > 0 && !(*params)[i].data.d_int32array)
	    goto oom;
	  if (!wire_read_int32 (channel, (guint32*) (*params)[i].data.d_int32array,
				count))
	    goto fail;
          break;
        case PARAM_INT16ARRAY:
	  (*params)[i].data.d_int16array = g_try_new0 (gint16, count);
	  if (count > 0 && !(*params)[i].data.d_int16array)
	    goto oom;
	  if (!wire_read_int16 (channel, (guint16*) (*params)[i].data.d_int16array,
				count))
	    goto fail;
          break;
        case PARAM_INT8ARRAY:
	  (*params)[i].data.d_int8array = g_try_new0 (gint8, count);
	  if (count > 0 && !(*params)[i].data.d_int8array)
	    goto oom;
	  if (!wire_read_int8 (channel, (guint8*) (*params)[i].data.d_int8array,
			       count))
	    goto fail;
          break;
        case PARAM_FLOATARRAY:
	  (*params)[i].data.d_floatarray = g_try_new0 (gdouble, count);
	  if (count > 0 && !(*params)[i].data.d_floatarray)
	    goto oom;
	  if (!wire_read_double (channel, (*params)[i].data.d_floatarray,
				 count))
	    goto fail;
          break;
        case PARAM_STRINGARRAY:
	  (*params)[i].data.d_stringarray = g_try_new0 (gchar*, count);
	  if (count > 0 && !(*params)[i].data.d_stringarray)
	    goto oom;
	  if (!wire_read_string (channel, (*params)[i].data.d_stringarray,
				 count))
	    goto fail;
          break;
        case PARAM_COLOR:
	  if (!wire_read_int8 (channel, (guint8*) &(*params)[i].data.d_color.red, 1))
	    goto fail;
	  if (!wire_read_int8 (channel, (guint8*) &(*params)[i].data.d_color.green, 1))
	    goto fail;
	  if (!wire_read_int8 (channel, (guint8*) &(*params)[i].data.d_color.blue, 1))
	    goto fail;
          break;
        case PARAM_REGION:
          break;
        case PARAM_DISPLAY:
	  if (!wire_read_int32 (channel, (guint32*) &(*params)[i].data.d_display, 1))
	    goto fail;
          break;
        case PARAM_IMAGE:
	  if (!wire_read_int32 (channel, (guint32*) &(*params)[i].data.d_image, 1))
	    goto fail;
          break;
        case PARAM_LAYER:
	  if (!wire_read_int32 (channel, (guint32*) &(*params)[i].data.d_layer, 1))
	    goto fail;
          break;
        case PARAM_CHANNEL:
	  if (!wire_read_int32 (channel, (guint32*) &(*params)[i].data.d_channel, 1))
	    goto fail;
          break;
        case PARAM_DRAWABLE:
	  if (!wire_read_int32 (channel, (guint32*) &(*params)[i].data.d_drawable, 1))
	    goto fail;
          break;
        case PARAM_SELECTION:
	  if (!wire_read_int32 (channel, (guint32*) &(*params)[i].data.d_selection, 1))
	    goto fail;
          break;
        case PARAM_BOUNDARY:
	  if (!wire_read_int32 (channel, (guint32*) &(*params)[i].data.d_boundary, 1))
	    goto fail;
          break;
        case PARAM_PATH:
	  if (!wire_read_int32 (channel, (guint32*) &(*params)[i].data.d_path, 1))
	    goto fail;
          break;
        case PARAM_STATUS:
	  if (!wire_read_int32 (channel, (guint32*) &(*params)[i].data.d_status, 1))
	    goto fail;
          break;
	case PARAM_END:
	  break;
	}
    }

  return TRUE;

 oom:
  /*  the array pointer is NULL: nothing for _gp_params_destroy to free  */
  (*params)[i].type = PARAM_END;
  wire_set_error ();

 fail:
  _gp_params_destroy (*params, *nparams);
  *params = NULL;
  *nparams = 0;
  return FALSE;
}

static void
_gp_params_write (GIOChannel *channel, GPParam *params, int nparams)
{
  int i;

  if (!wire_write_int32 (channel, (guint32*) &nparams, 1))
    return;

  for (i = 0; i < nparams; i++)
    {
      if (!wire_write_int32 (channel, (guint32*) &params[i].type, 1))
	return;

      switch (params[i].type)
	{
	case PARAM_INT32:
	  if (!wire_write_int32 (channel, (guint32*) &params[i].data.d_int32, 1))
	    return;
	  break;
	case PARAM_INT16:
	  if (!wire_write_int16 (channel, (guint16*) &params[i].data.d_int16, 1))
	    return;
	  break;
	case PARAM_INT8:
	  if (!wire_write_int8 (channel, (guint8*) &params[i].data.d_int8, 1))
	    return;
	  break;
        case PARAM_FLOAT:
	  if (!wire_write_double (channel, &params[i].data.d_float, 1))
	    return;
          break;
        case PARAM_STRING:
	  if (!wire_write_string (channel, &params[i].data.d_string, 1))
	    return;
          break;
        case PARAM_INT32ARRAY:
	  if (!wire_write_int32 (channel, (guint32*) params[i].data.d_int32array,
				 params[i-1].data.d_int32))
	    return;
          break;
        case PARAM_INT16ARRAY:
	  if (!wire_write_int16 (channel, (guint16*) params[i].data.d_int16array,
				 params[i-1].data.d_int32))
	    return;
          break;
        case PARAM_INT8ARRAY:
	  if (!wire_write_int8 (channel, (guint8*) params[i].data.d_int8array,
				params[i-1].data.d_int32))
	    return;
          break;
        case PARAM_FLOATARRAY:
	  if (!wire_write_double (channel, params[i].data.d_floatarray,
				  params[i-1].data.d_int32))
	    return;
          break;
        case PARAM_STRINGARRAY:
	  if (!wire_write_string (channel, params[i].data.d_stringarray,
				  params[i-1].data.d_int32))
	    return;
          break;
        case PARAM_COLOR:
	  if (!wire_write_int8 (channel, (guint8*) &params[i].data.d_color.red, 1))
	    return;
	  if (!wire_write_int8 (channel, (guint8*) &params[i].data.d_color.green, 1))
	    return;
	  if (!wire_write_int8 (channel, (guint8*) &params[i].data.d_color.blue, 1))
	    return;
          break;
        case PARAM_REGION:
          break;
        case PARAM_DISPLAY:
	  if (!wire_write_int32 (channel, (guint32*) &params[i].data.d_display, 1))
	    return;
          break;
        case PARAM_IMAGE:
	  if (!wire_write_int32 (channel, (guint32*) &params[i].data.d_image, 1))
	    return;
          break;
        case PARAM_LAYER:
	  if (!wire_write_int32 (channel, (guint32*) &params[i].data.d_layer, 1))
	    return;
          break;
        case PARAM_CHANNEL:
	  if (!wire_write_int32 (channel, (guint32*) &params[i].data.d_channel, 1))
	    return;
          break;
        case PARAM_DRAWABLE:
	  if (!wire_write_int32 (channel, (guint32*) &params[i].data.d_drawable, 1))
	    return;
          break;
        case PARAM_SELECTION:
	  if (!wire_write_int32 (channel, (guint32*) &params[i].data.d_selection, 1))
	    return;
          break;
        case PARAM_BOUNDARY:
	  if (!wire_write_int32 (channel, (guint32*) &params[i].data.d_boundary, 1))
	    return;
          break;
        case PARAM_PATH:
	  if (!wire_write_int32 (channel, (guint32*) &params[i].data.d_path, 1))
	    return;
          break;
        case PARAM_STATUS:
	  if (!wire_write_int32 (channel, (guint32*) &params[i].data.d_status, 1))
	    return;
          break;
	case PARAM_END:
	  break;
	}
    }
}

static void
_gp_extension_ack_read (GIOChannel *channel, WireMessage *msg)
{
}

static void
_gp_extension_ack_write (GIOChannel *channel, WireMessage *msg)
{
}

static void
_gp_extension_ack_destroy (WireMessage *msg)
{
}

void
_gp_params_destroy (GPParam *params, int nparams)
{
  int count;
  int i, j;

  for (i = 0; i < nparams; i++)
    {
      switch (params[i].type)
	{
	case PARAM_INT32:
	case PARAM_INT16:
	case PARAM_INT8:
	case PARAM_FLOAT:
	case PARAM_COLOR:
	case PARAM_REGION:
	case PARAM_DISPLAY:
	case PARAM_IMAGE:
	case PARAM_LAYER:
	case PARAM_CHANNEL:
	case PARAM_DRAWABLE:
	case PARAM_SELECTION:
	case PARAM_BOUNDARY:
	case PARAM_PATH:
	case PARAM_STATUS:
	  break;

	case PARAM_STRING:
	  g_free (params[i].data.d_string);
	  break;
	case PARAM_INT32ARRAY:
	  g_free (params[i].data.d_int32array);
	  break;
	case PARAM_INT16ARRAY:
	  g_free (params[i].data.d_int16array);
	  break;
	case PARAM_INT8ARRAY:
	  g_free (params[i].data.d_int8array);
	  break;
	case PARAM_FLOATARRAY:
	  g_free (params[i].data.d_floatarray);
	  break;
	case PARAM_STRINGARRAY:
	  if ((i > 0) && (params[i-1].type == PARAM_INT32))
	    {
	      count = params[i-1].data.d_int32;
	      for (j = 0; j < count; j++)
		g_free (params[i].data.d_stringarray[j]);
	      g_free (params[i].data.d_stringarray);
	    }
	  break;
	case PARAM_END:
	  break;
	}
    }
  g_free (params);
}
