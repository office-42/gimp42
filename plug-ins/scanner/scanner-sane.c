/* Scanner plug-in for gimp42 -- SANE
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

/* Scanning through SANE, in the spirit of xscanimage, which GIMP 1.0
 * used for this:
 *
 * - the devices SANE finds are listed in a drop-down, with a Refresh
 *   button; looking for them runs in a thread, since network backends
 *   can take a while;
 * - source, mode and resolution come first; every other option the
 *   backend offers gets a widget by its type and constraint under
 *   "Advanced options", in the backend's groups, with the options it
 *   marks as advanced behind "Show expert options";
 * - Preview scans the whole bed at a low resolution into the scan area
 *   view, where the area to scan is dragged out (or typed in);
 * - Scan reads the page in a thread, so that the dialog stays live and
 *   Cancel works, and hands it to scanner.c, which makes an image of it.
 *   With a document feeder as the source, Scan reads pages until the
 *   feeder is empty.
 *
 * Values are applied with sane_control_option as they are changed; the
 * backend may round them (SANE_INFO_INEXACT: the value is read back) or
 * change other options with them (SANE_INFO_RELOAD_OPTIONS: the widgets
 * are rebuilt).
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <sane/sane.h>
#include <sane/saneopts.h>

#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#include "scanner.h"

#define NO_SCANNERS_MESSAGE \
  "No scanners were found. Check that the scanner is connected and " \
  "switched on, and that SANE supports it."

/*  Bytes asked for per sane_read.  */
#define SCAN_CHUNK_SIZE   (64 * 1024)

/*  Arrays with at most this many values are edited as text.  */
#define MAX_ARRAY_EDIT    16

/*  Pixels the preview is scanned at across the bed, at least.  */
#define PREVIEW_PIXELS    320

/*  How close to an edge of the area the pointer grabs it.  */
#define GRAB_DISTANCE     6.0


/*  Well-known options: their indices, or -1.  */
typedef struct
{
  gint source;
  gint mode;
  gint resolution;
  gint preview;
  gint area[4];		/* tl-x, tl-y, br-x, br-y */
} ScannerOptions;


/*  Reading a scan.  */

typedef struct
{
  SANE_Handle  handle;

  /* The result: 8 bit gray (bpp 1) or RGB (bpp 3) rows.  */
  SANE_Status  status;
  gchar       *error;		/* our own message, or NULL */
  guchar      *data;
  gint         width;
  gint         height;
  gint         bpp;
  gsize        rows_allocated;

  /* Shared with the dialog while a thread reads.  */
  gint         progress;	/* 0..10000, or -1 when the length is unknown */
  gint         cancelled;

  /* Without a thread: report through gimp_progress_update.  */
  gboolean     gimp_progress;
} ScanJob;


/*  The dialog.  */

typedef enum
{
  KIND_BOOL,
  KIND_RANGE,		/* scale and spin button */
  KIND_SPIN,		/* spin button */
  KIND_WORD_LIST,
  KIND_STRING_LIST,
  KIND_STRING,
  KIND_ARRAY,		/* values as text */
  KIND_BUTTON,
  KIND_READ_ONLY
} OptionKind;

typedef struct
{
  gint           index;
  OptionKind     kind;
  GtkWidget     *parts[4];	/* its widgets in the grid */
  gint           n_parts;
  GtkWidget     *widget;
  GtkAdjustment *adjustment;
  GtkWidget     *group;		/* header of its group, or NULL */
  gboolean       expert;
  gboolean       advanced;	/* under "Advanced options" */
} OptionWidget;

typedef struct
{
  gchar *name;
  gchar *label;
} DeviceEntry;

typedef enum
{
  DRAG_NONE,
  DRAG_NEW,
  DRAG_MOVE,
  DRAG_RESIZE
} DragMode;

enum
{
  EDGE_LEFT   = 1 << 0,
  EDGE_TOP    = 1 << 1,
  EDGE_RIGHT  = 1 << 2,
  EDGE_BOTTOM = 1 << 3
};

typedef struct
{
  /* Devices.  */
  GPtrArray       *devices;		/* DeviceEntry */
  GThread         *devices_thread;
  SANE_Handle      handle;
  gchar           *device;		/* name of the open device */
  gboolean         vals_applied;
  ScannerOptions   opts;

  /* The scan bed and the area, in the geometry options' unit.  */
  gboolean         have_area;
  gdouble          bed[4];
  gdouble          area[4];

  /* The preview, and how the bed is placed in its widget.  */
  cairo_surface_t *preview;
  gdouble          preview_bed[4];
  gdouble          view_x;
  gdouble          view_y;
  gdouble          view_scale;
  DragMode         drag_mode;
  gint             drag_edges;
  gdouble          drag_start[2];
  gdouble          drag_area[4];

  /* Widgets.  */
  GtkWidget       *dialog;
  GtkWidget       *device_menu;
  GtkWidget       *refresh_button;
  GtkWidget       *message;
  GtkWidget       *content;
  GtkWidget       *common_grid;
  GtkWidget       *advanced_expander;
  GtkWidget       *advanced_grid;
  GtkWidget       *expert_check;
  GtkWidget       *info_label;
  GtkWidget       *area_box;
  GtkWidget       *preview_area;
  GtkWidget       *area_spins[4];
  GtkWidget       *area_units[4];
  GtkWidget       *preview_button;
  GtkWidget       *full_button;
  GtkWidget       *status_label;
  GtkWidget       *progress;
  GtkWidget       *cancel_button;
  GtkWidget       *scan_button;
  GPtrArray       *option_widgets;	/* OptionWidget */
  GPtrArray       *group_headers;	/* GtkWidget */
  gint             updating;
  guint            rebuild_idle;
  gboolean         rebuild_needed;

  /* Scanning.  */
  ScanJob         *job;
  GThread         *thread;
  gboolean         job_is_preview;
  gboolean         feeder;		/* reading a document feeder */
  gint             pages;		/* pages of this Scan */
  gdouble          saved_area[4];
  gdouble          saved_resolution;
  SANE_Bool        saved_preview;
  guint            pulse_timeout;
  gboolean         closing;
  gint32           last_image;
} ScannerDialog;

static ScannerDialog sd;


static void     scanner_build_options   (void);
static void     scanner_schedule_rebuild (void);
static void     scanner_update_info     (void);
static void     scanner_update_sensitivity (void);
static void     scanner_area_read       (void);
static void     scanner_start_job       (gboolean preview);
static gboolean scanner_scan_done       (gpointer data);


/*  SANE helpers  */

static gint
option_count (SANE_Handle handle)
{
  SANE_Int count = 0;

  if (sane_control_option (handle, 0, SANE_ACTION_GET_VALUE, &count,
			   NULL) != SANE_STATUS_GOOD)
    return 0;

  return count;
}

static const SANE_Option_Descriptor *
option_desc (SANE_Handle handle,
	     gint        index)
{
  if (! handle || index <= 0)
    return NULL;

  return sane_get_option_descriptor (handle, index);
}

static gboolean
option_is_active (const SANE_Option_Descriptor *desc)
{
  return desc && desc->type != SANE_TYPE_GROUP &&
	 SANE_OPTION_IS_ACTIVE (desc->cap);
}

static gboolean
option_is_settable (const SANE_Option_Descriptor *desc)
{
  return option_is_active (desc) && SANE_OPTION_IS_SETTABLE (desc->cap);
}

static gboolean
option_is_number (const SANE_Option_Descriptor *desc)
{
  return desc &&
	 (desc->type == SANE_TYPE_INT || desc->type == SANE_TYPE_FIXED) &&
	 desc->size == (SANE_Int) sizeof (SANE_Word);
}

static gint
option_find (SANE_Handle  handle,
	     const gchar *name)
{
  gint count = option_count (handle);
  gint i;

  for (i = 1; i < count; i++)
    {
      const SANE_Option_Descriptor *desc = option_desc (handle, i);

      if (desc && desc->name && strcmp (desc->name, name) == 0)
	return i;
    }

  return -1;
}

static void
options_find (SANE_Handle     handle,
	      ScannerOptions *opts)
{
  opts->source     = option_find (handle, SANE_NAME_SCAN_SOURCE);
  opts->mode       = option_find (handle, SANE_NAME_SCAN_MODE);
  opts->resolution = option_find (handle, SANE_NAME_SCAN_RESOLUTION);
  opts->preview    = option_find (handle, SANE_NAME_PREVIEW);
  opts->area[0]    = option_find (handle, SANE_NAME_SCAN_TL_X);
  opts->area[1]    = option_find (handle, SANE_NAME_SCAN_TL_Y);
  opts->area[2]    = option_find (handle, SANE_NAME_SCAN_BR_X);
  opts->area[3]    = option_find (handle, SANE_NAME_SCAN_BR_Y);
}

/*  Whether the four geometry options can be dragged out on a preview:
 *  settable numbers with a range.
 */
static gboolean
options_have_area (SANE_Handle           handle,
		   const ScannerOptions *opts)
{
  gint k;

  for (k = 0; k < 4; k++)
    {
      const SANE_Option_Descriptor *desc = option_desc (handle, opts->area[k]);

      if (! option_is_settable (desc) || ! option_is_number (desc) ||
	  desc->constraint_type != SANE_CONSTRAINT_RANGE)
	return FALSE;
    }

  return TRUE;
}

static gdouble
word_to_double (const SANE_Option_Descriptor *desc,
		SANE_Word                     word)
{
  if (desc->type == SANE_TYPE_FIXED)
    return SANE_UNFIX (word);

  return (gdouble) word;
}

static SANE_Word
double_to_word (const SANE_Option_Descriptor *desc,
		gdouble                       value)
{
  if (desc->type == SANE_TYPE_FIXED)
    {
      value = CLAMP (value, -32768.0, 32767.99998);
      return SANE_FIX (value);
    }

  value = CLAMP (value, (gdouble) G_MININT32, (gdouble) G_MAXINT32);
  return (SANE_Word) floor (value + 0.5);
}

/*  The option's current value in a buffer of its size, or NULL.  */
static gpointer
option_get (SANE_Handle handle,
	    gint        index)
{
  const SANE_Option_Descriptor *desc = option_desc (handle, index);
  gpointer                      value;

  if (! option_is_active (desc) || desc->type == SANE_TYPE_BUTTON)
    return NULL;

  /* One more byte, so that strings are always terminated.  */
  value = g_malloc0 (MAX (desc->size, (SANE_Int) sizeof (SANE_Word)) + 1);
  if (sane_control_option (handle, index, SANE_ACTION_GET_VALUE, value,
			   NULL) != SANE_STATUS_GOOD)
    {
      g_free (value);
      return NULL;
    }

  return value;
}

static gboolean
option_get_number (SANE_Handle  handle,
		   gint         index,
		   gdouble     *number)
{
  const SANE_Option_Descriptor *desc = option_desc (handle, index);
  SANE_Word                    *value;

  if (! option_is_number (desc) || ! (value = option_get (handle, index)))
    return FALSE;

  *number = word_to_double (desc, *value);
  g_free (value);

  return TRUE;
}

static SANE_Status
option_set_number (SANE_Handle  handle,
		   gint         index,
		   gdouble      number,
		   SANE_Int    *info)
{
  const SANE_Option_Descriptor *desc = option_desc (handle, index);
  SANE_Word                     word;

  /* Backends need not touch it when they fail.  */
  if (info)
    *info = 0;

  if (! option_is_settable (desc) || ! option_is_number (desc))
    return SANE_STATUS_UNSUPPORTED;

  word = double_to_word (desc, number);

  return sane_control_option (handle, index, SANE_ACTION_SET_VALUE, &word,
			      info);
}

static gchar *
option_get_string (SANE_Handle handle,
		   gint        index)
{
  const SANE_Option_Descriptor *desc = option_desc (handle, index);

  if (! desc || desc->type != SANE_TYPE_STRING)
    return NULL;

  return option_get (handle, index);
}

static SANE_Status
option_set_string (SANE_Handle  handle,
		   gint         index,
		   const gchar *string,
		   SANE_Int    *info)
{
  const SANE_Option_Descriptor *desc = option_desc (handle, index);
  gchar                        *value;
  SANE_Status                   status;

  if (info)
    *info = 0;

  if (! option_is_settable (desc) || desc->type != SANE_TYPE_STRING ||
      desc->size <= 0)
    return SANE_STATUS_UNSUPPORTED;

  value = g_malloc0 (desc->size + 1);
  g_strlcpy (value, string, desc->size);
  status = sane_control_option (handle, index, SANE_ACTION_SET_VALUE, value,
				info);
  g_free (value);

  return status;
}

/*  Sets the area, an axis at a time in an order that never puts the
 *  top-left corner past the bottom-right one on the way.
 */
static SANE_Int
options_set_area (SANE_Handle           handle,
		  const ScannerOptions *opts,
		  const gdouble         area[4])
{
  SANE_Int result = 0;
  gint     axis;

  for (axis = 0; axis < 2; axis++)
    {
      gint     tl = opts->area[axis];
      gint     br = opts->area[axis + 2];
      gdouble  current_br;
      SANE_Int info = 0;

      if (! option_get_number (handle, br, &current_br))
	current_br = area[axis + 2];

      if (area[axis] >= current_br)
	{
	  option_set_number (handle, br, area[axis + 2], &info);
	  result |= info;
	  option_set_number (handle, tl, area[axis], &info);
	  result |= info;
	}
      else
	{
	  option_set_number (handle, tl, area[axis], &info);
	  result |= info;
	  option_set_number (handle, br, area[axis + 2], &info);
	  result |= info;
	}
    }

  return result;
}

/*  Applies the settings remembered from last time to the device they
 *  were made on.
 */
static void
options_apply_vals (SANE_Handle handle)
{
  ScannerOptions opts;

  /* The source and the mode can change what else there is.  */
  options_find (handle, &opts);
  if (scanner_vals.source[0])
    option_set_string (handle, opts.source, scanner_vals.source, NULL);

  options_find (handle, &opts);
  if (scanner_vals.mode[0])
    option_set_string (handle, opts.mode, scanner_vals.mode, NULL);

  options_find (handle, &opts);
  if (scanner_vals.resolution > 0.0)
    option_set_number (handle, opts.resolution, scanner_vals.resolution,
		       NULL);

  options_find (handle, &opts);
  if (scanner_vals.have_area && options_have_area (handle, &opts))
    options_set_area (handle, &opts, scanner_vals.area);
}

/*  Remembers the device and its common settings for next time.  */
static void
options_store_vals (SANE_Handle  handle,
		    const gchar *device)
{
  ScannerOptions opts;
  gchar         *string;
  gint           k;

  options_find (handle, &opts);

  g_strlcpy (scanner_vals.device, device, sizeof (scanner_vals.device));

  string = option_get_string (handle, opts.source);
  g_strlcpy (scanner_vals.source, string ? string : "",
	     sizeof (scanner_vals.source));
  g_free (string);

  string = option_get_string (handle, opts.mode);
  g_strlcpy (scanner_vals.mode, string ? string : "",
	     sizeof (scanner_vals.mode));
  g_free (string);

  if (! option_get_number (handle, opts.resolution, &scanner_vals.resolution))
    scanner_vals.resolution = -1.0;

  scanner_vals.have_area = options_have_area (handle, &opts);
  for (k = 0; k < 4 && scanner_vals.have_area; k++)
    if (! option_get_number (handle, opts.area[k], &scanner_vals.area[k]))
      scanner_vals.have_area = FALSE;
}

static const gchar *
unit_name (SANE_Unit unit)
{
  switch (unit)
    {
    case SANE_UNIT_PIXEL:       return "px";
    case SANE_UNIT_BIT:         return "bit";
    case SANE_UNIT_MM:          return "mm";
    case SANE_UNIT_DPI:         return "dpi";
    case SANE_UNIT_PERCENT:     return "%";
    case SANE_UNIT_MICROSECOND: return "\302\265s";
    default:                    return NULL;
    }
}

/*  Digits worth showing for a range's step.  */
static gint
range_digits (const SANE_Option_Descriptor *desc)
{
  gdouble quant;

  if (desc->type != SANE_TYPE_FIXED)
    return 0;

  if (desc->constraint_type != SANE_CONSTRAINT_RANGE ||
      desc->constraint.range->quant == 0)
    return 2;

  quant = SANE_UNFIX (desc->constraint.range->quant);
  if (quant >= 1.0)
    return 0;
  if (quant >= 0.1)
    return 1;
  if (quant >= 0.01)
    return 2;

  return 3;
}

static gdouble
range_step (const SANE_Option_Descriptor *desc)
{
  if (desc->constraint_type == SANE_CONSTRAINT_RANGE &&
      desc->constraint.range->quant != 0)
    return fabs (word_to_double (desc, desc->constraint.range->quant));

  return (desc->type == SANE_TYPE_FIXED) ? 0.1 : 1.0;
}

static gchar *
format_word (const SANE_Option_Descriptor *desc,
	     SANE_Word                     word)
{
  const gchar *unit = unit_name (desc->unit);
  gchar       *number;
  gchar       *text;

  if (desc->type == SANE_TYPE_FIXED)
    number = g_strdup_printf ("%g", SANE_UNFIX (word));
  else
    number = g_strdup_printf ("%d", (gint) word);

  if (! unit)
    return number;

  text = g_strdup_printf ("%s %s", number, unit);
  g_free (number);

  return text;
}


/*  Reading  */

static void
scan_job_free (ScanJob *job)
{
  g_free (job->error);
  g_free (job->data);
  g_free (job);
}

static gboolean
scan_job_ensure_rows (ScanJob *job,
		      gsize    rows)
{
  gsize   row_bytes = (gsize) job->width * job->bpp;
  gsize   new_rows;
  guchar *data;

  if (rows <= job->rows_allocated)
    return TRUE;

  /* Exactly what was announced, or doubling when the length is not
   * known in advance.
   */
  new_rows = MAX (rows, job->rows_allocated * 2);
  if (job->rows_allocated == 0 && rows == 1)
    new_rows = 256;

  if (new_rows > G_MAXSIZE / row_bytes)
    return FALSE;

  data = g_try_realloc (job->data, new_rows * row_bytes);
  if (! data)
    return FALSE;

  memset (data + job->rows_allocated * row_bytes, 0,
	  (new_rows - job->rows_allocated) * row_bytes);

  job->data           = data;
  job->rows_allocated = new_rows;

  return TRUE;
}

/*  Bytes a line of this frame needs, or 0 when it cannot be read.  */
static gsize
frame_line_bytes (const SANE_Parameters *params)
{
  gsize pixels   = params->pixels_per_line;
  gsize channels = (params->format == SANE_FRAME_RGB) ? 3 : 1;

  switch (params->depth)
    {
    case 1:  return channels * ((pixels + 7) / 8);
    case 8:  return channels * pixels;
    case 16: return channels * pixels * 2;
    default: return 0;
    }
}

static gboolean
scan_job_check_frame (ScanJob               *job,
		      const SANE_Parameters *params,
		      gint                   frame)
{
  gsize needed;

  if (params->format != SANE_FRAME_GRAY &&
      params->format != SANE_FRAME_RGB &&
      params->format != SANE_FRAME_RED &&
      params->format != SANE_FRAME_GREEN &&
      params->format != SANE_FRAME_BLUE)
    {
      job->error = g_strdup_printf ("The scanner sends its image in a format "
				    "that is not supported (%d).",
				    (gint) params->format);
      return FALSE;
    }

  if (params->depth != 1 && params->depth != 8 && params->depth != 16)
    {
      job->error = g_strdup_printf ("The scanner sends %d bits per sample, "
				    "which is not supported.",
				    (gint) params->depth);
      return FALSE;
    }

  needed = frame_line_bytes (params);
  if (params->pixels_per_line <= 0 || params->bytes_per_line <= 0 ||
      (gsize) params->bytes_per_line < needed)
    {
      job->error = g_strdup ("The scanner reports an image size that "
			     "makes no sense.");
      return FALSE;
    }

  if (frame > 0)
    {
      gboolean single = (params->format == SANE_FRAME_GRAY ||
			 params->format == SANE_FRAME_RGB);

      if (single || job->bpp != 3 || params->pixels_per_line != job->width)
	{
	  job->error = g_strdup ("The scanner sends frames that do not fit "
				 "together.");
	  return FALSE;
	}
    }

  return TRUE;
}

static inline guchar
sample16 (const guchar *line,
	  gsize         i)
{
  guint16 value;

  /* Host byte order, as the SANE standard has it.  */
  memcpy (&value, line + 2 * i, 2);

  return value >> 8;
}

static inline gboolean
bit_set (const guchar *line,
	 gsize         i)
{
  return (line[i >> 3] & (0x80 >> (i & 7))) != 0;
}

/*  Converts one line of the frame into 8 bit samples in row.  The SANE
 *  standard: samples of 16 bits are in the host's byte order; at depth
 *  1 the leftmost pixel is the most significant bit, a gray 1 is black
 *  (anything else is intensity, so 1 is full red, green or blue), and
 *  RGB is interleaved a byte, that is 8 pixels of one channel, at a
 *  time.
 */
static void
scan_convert_line (const SANE_Parameters *params,
		   const guchar          *line,
		   guchar                *row,
		   gint                   width)
{
  gsize x;
  gsize n;
  gint  channel;

  switch (params->format)
    {
    case SANE_FRAME_GRAY:
      if (params->depth == 1)
	{
	  for (x = 0; x < (gsize) width; x++)
	    row[x] = bit_set (line, x) ? 0 : 255;
	}
      else if (params->depth == 8)
	{
	  memcpy (row, line, width);
	}
      else
	{
	  for (x = 0; x < (gsize) width; x++)
	    row[x] = sample16 (line, x);
	}
      break;

    case SANE_FRAME_RGB:
      n = (gsize) width * 3;
      if (params->depth == 1)
	{
	  for (x = 0; x < (gsize) width; x++)
	    for (channel = 0; channel < 3; channel++)
	      {
		const guchar byte = line[(x >> 3) * 3 + channel];

		row[x * 3 + channel] = (byte & (0x80 >> (x & 7))) ? 255 : 0;
	      }
	}
      else if (params->depth == 8)
	{
	  memcpy (row, line, n);
	}
      else
	{
	  for (x = 0; x < n; x++)
	    row[x] = sample16 (line, x);
	}
      break;

    case SANE_FRAME_RED:
    case SANE_FRAME_GREEN:
    case SANE_FRAME_BLUE:
      channel = params->format - SANE_FRAME_RED;
      for (x = 0; x < (gsize) width; x++)
	{
	  guchar value;

	  if (params->depth == 1)
	    value = bit_set (line, x) ? 255 : 0;
	  else if (params->depth == 8)
	    value = line[x];
	  else
	    value = sample16 (line, x);

	  row[x * 3 + channel] = value;
	}
      break;

    default:
      break;
    }
}

static void
scan_job_progress (ScanJob *job,
		   gdouble  fraction)
{
  gint value;

  if (fraction < 0.0)
    value = -1;
  else
    value = (gint) (CLAMP (fraction, 0.0, 1.0) * 10000);

  if (job->gimp_progress)
    {
      /* Every percent is plenty.  */
      if (value >= 0 && value / 100 != job->progress / 100)
	gimp_progress_update (value / 10000.0);
      job->progress = value;
    }
  else
    {
      g_atomic_int_set (&job->progress, value);
    }
}

/*  Scans a page: sane_start, and sane_read until the last frame has
 *  been read.  Runs in a thread of its own when the dialog is up; it
 *  touches nothing but the job and the handle, which the dialog leaves
 *  alone meanwhile (apart from sane_cancel, which the SANE standard
 *  allows at any time).
 */
static void
scan_job_read (ScanJob *job)
{
  SANE_Parameters params;
  SANE_Status     status = SANE_STATUS_GOOD;
  guchar         *chunk;
  guchar         *line   = NULL;
  gint            frames = 1;
  gint            frame;

  chunk = g_malloc (SCAN_CHUNK_SIZE);
  scan_job_progress (job, 0.0);

  for (frame = 0; ; frame++)
    {
      gsize    line_bytes;
      gsize    frame_bytes;
      gsize    received;
      gboolean settled;
      gsize    fill = 0;
      gsize    y    = 0;

      if (g_atomic_int_get (&job->cancelled))
	{
	  status = SANE_STATUS_CANCELLED;
	  break;
	}

      status = sane_start (job->handle);
      if (status != SANE_STATUS_GOOD)
	break;

      status = sane_get_parameters (job->handle, &params);
      if (status != SANE_STATUS_GOOD)
	break;

      if (! scan_job_check_frame (job, &params, frame))
	{
	  status = SANE_STATUS_INVAL;
	  break;
	}

      if (frame == 0)
	{
	  job->width = params.pixels_per_line;
	  job->bpp   = (params.format == SANE_FRAME_GRAY) ? 1 : 3;
	  if (params.format != SANE_FRAME_GRAY &&
	      params.format != SANE_FRAME_RGB)
	    frames = 3;
	}

      line_bytes = params.bytes_per_line;
      line = g_realloc (line, line_bytes);

      if (params.lines > 0 && ! scan_job_ensure_rows (job, params.lines))
	{
	  status = SANE_STATUS_NO_MEM;
	  break;
	}

      frame_bytes = (params.lines > 0) ? (gsize) params.lines * line_bytes : 0;
      received    = 0;
      settled     = FALSE;

      for (;;)
	{
	  SANE_Int length = 0;
	  SANE_Int wanted = SCAN_CHUNK_SIZE;
	  gsize    pos    = 0;

	  /* Backends on sanei_thread (the test backend, for one) stop
	   * their reader thread, asynchronously, as soon as the last byte
	   * of the frame is read.  Caught on its way out, the thread can
	   * take the dynamic loader's lock with it, and sane_exit
	   * (dlclose) and even exit () then hang for good.  So when the
	   * length is known, give the thread, which has everything in the
	   * pipe by now, a moment to finish before reading the rest.
	   */
	  if (frame_bytes > 0 && received < frame_bytes)
	    {
	      wanted = MIN (SCAN_CHUNK_SIZE, frame_bytes - received);
	      if (! settled && frame_bytes - received <= SCAN_CHUNK_SIZE)
		{
		  g_usleep (G_USEC_PER_SEC / 20);
		  settled = TRUE;
		}
	    }

	  status = sane_read (job->handle, chunk, wanted, &length);
	  if (status != SANE_STATUS_GOOD)
	    break;

	  received += length;

	  if (g_atomic_int_get (&job->cancelled))
	    {
	      status = SANE_STATUS_CANCELLED;
	      break;
	    }

	  while (pos < (gsize) length)
	    {
	      gsize n = MIN (line_bytes - fill, (gsize) length - pos);

	      memcpy (line + fill, chunk + pos, n);
	      fill += n;
	      pos  += n;

	      if (fill == line_bytes)
		{
		  if (! scan_job_ensure_rows (job, y + 1))
		    {
		      status = SANE_STATUS_NO_MEM;
		      break;
		    }

		  scan_convert_line (&params, line,
				     job->data + y * job->width * job->bpp,
				     job->width);
		  y++;
		  fill = 0;
		}
	    }
	  if (status != SANE_STATUS_GOOD)
	    break;

	  if (params.lines > 0)
	    scan_job_progress (job, (frame % frames +
				     MIN (1.0, (gdouble) y / params.lines)) /
				    frames);
	  else
	    scan_job_progress (job, -1.0);
	}

      /* After sane_cancel some backends end the frame as if it were
       * complete; it is not.
       */
      if (g_atomic_int_get (&job->cancelled))
	status = SANE_STATUS_CANCELLED;

      if (status != SANE_STATUS_EOF)
	break;

      status = SANE_STATUS_GOOD;
      job->height = MAX (job->height, (gint) y);

      /* A backend that never says "last" would keep us here.  */
      if (params.last_frame || frame >= 15)
	break;
    }

  /* Ends the scan, as the standard asks after the last frame, and
   * stops it after an error.
   */
  sane_cancel (job->handle);

  g_free (line);
  g_free (chunk);

  if (status == SANE_STATUS_GOOD && job->height == 0)
    {
      job->error = g_strdup ("The scanner sent no image.");
      status = SANE_STATUS_INVAL;
    }
  else if (status == SANE_STATUS_NO_MEM && ! job->error)
    {
      job->error = g_strdup ("There is not enough memory for the scan.");
    }

  job->status = status;
}

static ScanJob *
scan_job_new (SANE_Handle handle)
{
  ScanJob *job = g_new0 (ScanJob, 1);

  job->handle   = handle;
  job->status   = SANE_STATUS_GOOD;
  job->progress = -1;

  return job;
}

/*  What went wrong, for the user.  */
static gchar *
scan_job_error (ScanJob *job)
{
  switch (job->status)
    {
    case SANE_STATUS_NO_DOCS:
      return g_strdup ("There are no documents in the document feeder.");
    case SANE_STATUS_JAMMED:
      return g_strdup ("The paper is jammed in the document feeder.");
    case SANE_STATUS_COVER_OPEN:
      return g_strdup ("The scanner's cover is open.");
    case SANE_STATUS_DEVICE_BUSY:
      return g_strdup ("The scanner is busy. Try again in a moment.");
    default:
      break;
    }

  if (job->error)
    return g_strdup (job->error);

  return g_strdup_printf ("Scanning failed: %s.",
			  sane_strstatus (job->status));
}


/*  Scanning without the dialog  */

gint32
scanner_sane_scan (gboolean progress)
{
  const SANE_Device **devices = NULL;
  SANE_Handle         handle;
  SANE_Status         status;
  SANE_Int            version;
  ScanJob            *job;
  const gchar        *name = NULL;
  gint32              image = -1;
  gint                i;

  status = sane_init (&version, NULL);
  if (status != SANE_STATUS_GOOD)
    {
      g_message ("Scanner: SANE could not be initialized: %s\n",
		 sane_strstatus (status));
      return -1;
    }

  if (sane_get_devices (&devices, SANE_FALSE) == SANE_STATUS_GOOD && devices)
    {
      for (i = 0; devices[i]; i++)
	if (strcmp (devices[i]->name, scanner_vals.device) == 0)
	  name = devices[i]->name;

      if (! name && devices[0])
	name = devices[0]->name;
    }

  if (! name)
    {
      g_message ("Scanner: " NO_SCANNERS_MESSAGE "\n");
      sane_exit ();
      return -1;
    }

  status = sane_open (name, &handle);
  if (status != SANE_STATUS_GOOD)
    {
      g_message ("Scanner: the scanner %s could not be opened: %s\n",
		 name, sane_strstatus (status));
      sane_exit ();
      return -1;
    }

  if (strcmp (name, scanner_vals.device) == 0)
    options_apply_vals (handle);

  if (progress)
    gimp_progress_init ("Scanning...");

  job = scan_job_new (handle);
  job->gimp_progress = progress;
  scan_job_read (job);

  if (job->status == SANE_STATUS_GOOD)
    {
      image = scanner_image_new (job->data, job->width, job->height,
				 job->bpp);
      options_store_vals (handle, name);
    }
  else
    {
      gchar *message = scan_job_error (job);

      g_message ("Scanner: %s\n", message);
      g_free (message);
    }

  scan_job_free (job);
  sane_close (handle);
  sane_exit ();

  return image;
}


/*  The dialog: messages and busy states  */

static void
scanner_set_status (const gchar *text,
		    gboolean     error)
{
  gtk_label_set_text (GTK_LABEL (sd.status_label), text ? text : "");
  if (error)
    gtk_widget_add_css_class (sd.status_label, "error");
  else
    gtk_widget_remove_css_class (sd.status_label, "error");
}

/*  The message shown in place of the options, or NULL for none.  */
static void
scanner_set_message (const gchar *text)
{
  gtk_label_set_text (GTK_LABEL (sd.message), text ? text : "");
  gtk_widget_set_visible (sd.message, text != NULL);
  gtk_widget_set_visible (sd.content, text == NULL && sd.handle != NULL);
}

static gboolean
scanner_pulse (gpointer data)
{
  gint value = sd.job ? g_atomic_int_get (&sd.job->progress) : -1;

  if (value < 0)
    gtk_progress_bar_pulse (GTK_PROGRESS_BAR (sd.progress));
  else
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (sd.progress),
				   value / 10000.0);

  return G_SOURCE_CONTINUE;
}

static void
scanner_progress_start (const gchar *text)
{
  gtk_progress_bar_set_text (GTK_PROGRESS_BAR (sd.progress), text);
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (sd.progress), 0.0);
  if (! sd.pulse_timeout)
    sd.pulse_timeout = g_timeout_add (100, scanner_pulse, NULL);
}

static void
scanner_progress_stop (void)
{
  if (sd.pulse_timeout)
    {
      g_source_remove (sd.pulse_timeout);
      sd.pulse_timeout = 0;
    }
  /* Not NULL, which would show "0 %"; the text keeps its line.  */
  gtk_progress_bar_set_text (GTK_PROGRESS_BAR (sd.progress), " ");
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (sd.progress), 0.0);
}

static gboolean
scanner_busy (void)
{
  return sd.job != NULL || sd.devices_thread != NULL;
}

static void
scanner_update_sensitivity (void)
{
  gboolean busy  = scanner_busy ();
  gboolean ready = sd.handle != NULL && ! busy;

  gtk_widget_set_sensitive (sd.device_menu,
			    ! busy && sd.devices && sd.devices->len > 0);
  gtk_widget_set_sensitive (sd.refresh_button, ! busy);
  gtk_widget_set_sensitive (sd.content, ready);
  gtk_widget_set_sensitive (sd.scan_button, ready);
  gtk_widget_set_sensitive (sd.cancel_button,
			    sd.job != NULL &&
			    ! g_atomic_int_get (&sd.job->cancelled));
}


/*  Option widgets  */

static OptionWidget *
option_widget_find (gint index)
{
  guint i;

  for (i = 0; sd.option_widgets && i < sd.option_widgets->len; i++)
    {
      OptionWidget *ow = g_ptr_array_index (sd.option_widgets, i);

      if (ow->index == index)
	return ow;
    }

  return NULL;
}

static gint
word_list_position (const SANE_Option_Descriptor *desc,
		    SANE_Word                     word)
{
  const SANE_Word *list = desc->constraint.word_list;
  gint             best = 0;
  gdouble          best_distance = G_MAXDOUBLE;
  gint             i;

  for (i = 1; i <= list[0]; i++)
    {
      gdouble distance = fabs ((gdouble) list[i] - (gdouble) word);

      if (distance < best_distance)
	{
	  best          = i - 1;
	  best_distance = distance;
	}
    }

  return best;
}

static gint
string_list_position (const SANE_Option_Descriptor *desc,
		      const gchar                  *string)
{
  gint i;

  for (i = 0; desc->constraint.string_list[i]; i++)
    if (strcmp (desc->constraint.string_list[i], string) == 0)
      return i;

  return -1;
}

static gchar *
array_to_text (const SANE_Option_Descriptor *desc,
	       const SANE_Word              *values)
{
  GString *text  = g_string_new (NULL);
  gint     count = desc->size / sizeof (SANE_Word);
  gint     i;

  for (i = 0; i < count; i++)
    {
      if (i > 0)
	g_string_append_c (text, ' ');
      if (desc->type == SANE_TYPE_FIXED)
	g_string_append_printf (text, "%g", SANE_UNFIX (values[i]));
      else
	g_string_append_printf (text, "%d", (gint) values[i]);
    }

  return g_string_free (text, FALSE);
}

/*  Shows the device's value of the option in its widget.  */
static void
option_widget_update (OptionWidget *ow)
{
  const SANE_Option_Descriptor *desc = option_desc (sd.handle, ow->index);
  gpointer                      value;
  gchar                        *text;
  gint                          position;

  if (! option_is_active (desc))
    return;

  value = option_get (sd.handle, ow->index);
  if (! value)
    return;

  sd.updating++;

  switch (ow->kind)
    {
    case KIND_BOOL:
      gtk_check_button_set_active (GTK_CHECK_BUTTON (ow->widget),
				   *(SANE_Bool *) value != SANE_FALSE);
      break;

    case KIND_RANGE:
    case KIND_SPIN:
      gtk_adjustment_set_value (ow->adjustment,
				word_to_double (desc, *(SANE_Word *) value));
      break;

    case KIND_WORD_LIST:
      gimp_option_menu_set_history (ow->widget,
				    word_list_position (desc,
							*(SANE_Word *) value));
      break;

    case KIND_STRING_LIST:
      position = string_list_position (desc, value);
      if (position >= 0)
	gimp_option_menu_set_history (ow->widget, position);
      break;

    case KIND_STRING:
      gtk_editable_set_text (GTK_EDITABLE (ow->widget), value);
      break;

    case KIND_ARRAY:
      text = array_to_text (desc, value);
      gtk_editable_set_text (GTK_EDITABLE (ow->widget), text);
      g_free (text);
      break;

    case KIND_BUTTON:
    case KIND_READ_ONLY:
      break;
    }

  sd.updating--;

  g_free (value);
}

static gboolean
option_is_area (gint index)
{
  gint k;

  for (k = 0; k < 4 && sd.have_area; k++)
    if (sd.opts.area[k] == index)
      return TRUE;

  return FALSE;
}

/*  Follows up a change of an option: the backend may have rounded the
 *  value, or changed other options along with it.
 */
static void
option_changed (gint        index,
		SANE_Status status,
		SANE_Int    info)
{
  OptionWidget *ow;

  if (status != SANE_STATUS_GOOD)
    {
      const SANE_Option_Descriptor *desc = option_desc (sd.handle, index);
      gchar *text;

      text = g_strdup_printf ("\"%s\" could not be set: %s.",
			      (desc && desc->title) ? desc->title : "?",
			      sane_strstatus (status));
      scanner_set_status (text, TRUE);
      g_free (text);

      /* Show what the device has.  */
      info |= SANE_INFO_INEXACT;
    }

  if (info & SANE_INFO_RELOAD_OPTIONS)
    {
      scanner_schedule_rebuild ();
    }
  else if (info & SANE_INFO_INEXACT)
    {
      ow = option_widget_find (index);
      if (ow)
	option_widget_update (ow);
      if (option_is_area (index))
	scanner_area_read ();
    }

  scanner_update_info ();
}

static void
option_set_value (gint     index,
		  gpointer value)
{
  SANE_Int    info = 0;
  SANE_Status status;

  status = sane_control_option (sd.handle, index, SANE_ACTION_SET_VALUE,
				value, &info);
  option_changed (index, status, info);
}

static void
option_bool_toggled (GtkCheckButton *button,
		     OptionWidget   *ow)
{
  SANE_Bool value;

  if (sd.updating)
    return;

  value = gtk_check_button_get_active (button) ? SANE_TRUE : SANE_FALSE;
  option_set_value (ow->index, &value);
}

static void
option_adjustment_changed (GtkAdjustment *adjustment,
			   OptionWidget  *ow)
{
  const SANE_Option_Descriptor *desc;
  SANE_Word                     word;

  if (sd.updating)
    return;

  desc = option_desc (sd.handle, ow->index);
  if (! desc)
    return;

  word = double_to_word (desc, gtk_adjustment_get_value (adjustment));
  option_set_value (ow->index, &word);
}

static void
option_menu_item_selected (GtkWidget *menu,
			   gpointer   data)
{
  OptionWidget                 *ow   = g_object_get_data (G_OBJECT (menu),
							  "scanner-option");
  const SANE_Option_Descriptor *desc;
  gint                          item = GPOINTER_TO_INT (data);

  if (sd.updating || ! ow)
    return;

  desc = option_desc (sd.handle, ow->index);
  if (! desc)
    return;

  if (desc->constraint_type == SANE_CONSTRAINT_WORD_LIST)
    {
      SANE_Word word;

      if (item < 0 || item >= desc->constraint.word_list[0])
	return;

      word = desc->constraint.word_list[item + 1];
      option_set_value (ow->index, &word);
    }
  else if (desc->constraint_type == SANE_CONSTRAINT_STRING_LIST)
    {
      SANE_Int    info = 0;
      SANE_Status status;
      gint        i;

      for (i = 0; i < item && desc->constraint.string_list[i]; i++)
	;
      if (! desc->constraint.string_list[i])
	return;

      status = option_set_string (sd.handle, ow->index,
				  desc->constraint.string_list[i], &info);
      option_changed (ow->index, status, info);
    }
}

/*  Entries apply their text when Enter is pressed or they lose focus.  */
static void
option_entry_apply (GtkWidget    *entry,
		    OptionWidget *ow)
{
  const SANE_Option_Descriptor *desc;
  const gchar                  *text;

  if (sd.updating || ! sd.handle || scanner_busy ())
    return;

  desc = option_desc (sd.handle, ow->index);
  if (! option_is_settable (desc))
    return;

  text = gtk_editable_get_text (GTK_EDITABLE (entry));

  if (ow->kind == KIND_STRING)
    {
      gchar *current = option_get_string (sd.handle, ow->index);

      if (! current || strcmp (current, text) != 0)
	{
	  SANE_Int    info = 0;
	  SANE_Status status;

	  status = option_set_string (sd.handle, ow->index, text, &info);
	  option_changed (ow->index, status, info);
	}
      g_free (current);
    }
  else if (ow->kind == KIND_ARRAY)
    {
      SANE_Word *values = option_get (sd.handle, ow->index);
      gchar    **words;
      gint       count  = desc->size / sizeof (SANE_Word);
      gint       i, j;

      if (! values)
	return;

      words = g_strsplit_set (text, " ,;\t", -1);
      for (i = 0, j = 0; words[i] && j < count; i++)
	{
	  gchar   *end;
	  gdouble  number;

	  if (! words[i][0])
	    continue;

	  number = g_ascii_strtod (words[i], &end);
	  if (end != words[i])
	    values[j] = (desc->type == SANE_TYPE_BOOL)
	      ? (number != 0.0) : double_to_word (desc, number);
	  j++;
	}
      g_strfreev (words);

      option_set_value (ow->index, values);
      g_free (values);

      /* Show what was taken.  */
      option_widget_update (ow);
    }
}

static void
option_entry_focus_leave (GtkEventControllerFocus *controller,
			  OptionWidget            *ow)
{
  GtkEventController *event_controller = GTK_EVENT_CONTROLLER (controller);

  option_entry_apply (gtk_event_controller_get_widget (event_controller), ow);
}

static void
option_button_clicked (GtkButton    *button,
		       OptionWidget *ow)
{
  if (! sd.updating)
    option_set_value (ow->index, NULL);
}

static void
option_auto_clicked (GtkButton    *button,
		     OptionWidget *ow)
{
  SANE_Int    info = 0;
  SANE_Status status;

  status = sane_control_option (sd.handle, ow->index, SANE_ACTION_SET_AUTO,
				NULL, &info);
  /* Show the value the backend picked.  */
  option_changed (ow->index, status, info | SANE_INFO_INEXACT);
}

static GtkWidget *
option_spin_new (GtkAdjustment *adjustment,
		 gint           digits)
{
  GtkWidget *spin;

  spin = gtk_spin_button_new (adjustment, 1.0, digits);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spin), TRUE);
  gtk_editable_set_width_chars (GTK_EDITABLE (spin), 7);

  return spin;
}

/*  The next free row of an options grid.  */
static gint
grid_next_row (GtkWidget *grid)
{
  gint row = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (grid),
						 "scanner-rows"));

  g_object_set_data (G_OBJECT (grid), "scanner-rows",
		     GINT_TO_POINTER (row + 1));

  return row;
}

static void
grid_clear (GtkWidget *grid)
{
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (grid)))
    gtk_grid_remove (GTK_GRID (grid), child);

  g_object_set_data (G_OBJECT (grid), "scanner-rows", GINT_TO_POINTER (0));
}

/*  Puts widget into the option's row of grid, from column for width
 *  columns.
 */
static void
option_widget_attach (OptionWidget *ow,
		      GtkWidget    *grid,
		      GtkWidget    *widget,
		      gint          row,
		      gint          column,
		      gint          width,
		      const gchar  *tooltip)
{
  if (tooltip && tooltip[0] && ! gtk_widget_get_tooltip_text (widget))
    gtk_widget_set_tooltip_text (widget, tooltip);

  gtk_widget_set_valign (widget, GTK_ALIGN_CENTER);
  gtk_grid_attach (GTK_GRID (grid), widget, column, row, width, 1);
  ow->parts[ow->n_parts++] = widget;
}

/*  Makes the widgets for option index in the next row of grid: a label,
 *  the value, its unit and an Auto button, as the option has them.
 *  title replaces the option's own title; compact asks for a spin
 *  button without a scale.  Returns NULL for options that get no
 *  widget.
 */
static OptionWidget *
option_widget_new (gint          index,
		   GtkWidget    *grid,
		   const gchar  *title,
		   gboolean      compact)
{
  const SANE_Option_Descriptor *desc = option_desc (sd.handle, index);
  OptionWidget                 *ow;
  GtkWidget                    *controls = NULL;
  const gchar                  *unit     = NULL;
  const gchar                  *tooltip;
  gpointer                      value    = NULL;
  gboolean                      settable;
  gboolean                      labelled = TRUE;
  gint                          count;
  gint                          row;
  gint                          i;

  if (! option_is_active (desc))
    return NULL;

  settable = SANE_OPTION_IS_SETTABLE (desc->cap);
  count    = (desc->size > 0) ? desc->size / (gint) sizeof (SANE_Word) : 0;

  if (desc->type == SANE_TYPE_BUTTON)
    {
      if (! settable)
	return NULL;
    }
  else if (desc->type != SANE_TYPE_BOOL && desc->type != SANE_TYPE_INT &&
	   desc->type != SANE_TYPE_FIXED && desc->type != SANE_TYPE_STRING)
    {
      return NULL;
    }
  else
    {
      value = option_get (sd.handle, index);
      if (! value)
	return NULL;
    }

  if (! title)
    title = (desc->title && desc->title[0]) ? desc->title : desc->name;
  tooltip = desc->desc;

  ow = g_new0 (OptionWidget, 1);
  ow->index  = index;
  ow->expert = (desc->cap & SANE_CAP_ADVANCED) != 0;

  if ((desc->type == SANE_TYPE_BOOL || desc->type == SANE_TYPE_INT ||
       desc->type == SANE_TYPE_FIXED) && count > 1)
    {
      /* Arrays: gamma tables and the like.  */
      gchar *text;

      if (count <= MAX_ARRAY_EDIT && settable)
	{
	  GtkEventController *focus;

	  ow->kind   = KIND_ARRAY;
	  ow->widget = gtk_entry_new ();
	  text = array_to_text (desc, value);
	  gtk_editable_set_text (GTK_EDITABLE (ow->widget), text);
	  g_free (text);
	  gtk_widget_set_tooltip_text (ow->widget,
				       "Values separated by spaces; "
				       "press Enter to apply them");
	  g_signal_connect (ow->widget, "activate",
			    G_CALLBACK (option_entry_apply), ow);
	  focus = gtk_event_controller_focus_new ();
	  g_signal_connect (focus, "leave",
			    G_CALLBACK (option_entry_focus_leave), ow);
	  gtk_widget_add_controller (ow->widget, focus);
	}
      else
	{
	  ow->kind   = KIND_READ_ONLY;
	  text = g_strdup_printf ("A table of %d values", count);
	  ow->widget = gtk_label_new (text);
	  gtk_label_set_xalign (GTK_LABEL (ow->widget), 0.0);
	  gtk_widget_add_css_class (ow->widget, "dim-label");
	  g_free (text);
	}
      controls = ow->widget;
      unit     = unit_name (desc->unit);
    }
  else switch (desc->type)
    {
    case SANE_TYPE_BOOL:
      {
	GtkWidget *label = gtk_label_new (title);

	gtk_label_set_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (label), 36);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);

	ow->kind   = KIND_BOOL;
	ow->widget = gtk_check_button_new ();
	gtk_check_button_set_child (GTK_CHECK_BUTTON (ow->widget), label);
	gtk_check_button_set_active (GTK_CHECK_BUTTON (ow->widget),
				     *(SANE_Bool *) value != SANE_FALSE);
	g_signal_connect (ow->widget, "toggled",
			  G_CALLBACK (option_bool_toggled), ow);
	controls = ow->widget;
	labelled = FALSE;
      }
      break;

    case SANE_TYPE_INT:
    case SANE_TYPE_FIXED:
      if (desc->constraint_type == SANE_CONSTRAINT_WORD_LIST)
	{
	  const SANE_Word *list = desc->constraint.word_list;

	  ow->kind   = KIND_WORD_LIST;
	  ow->widget = gimp_option_menu_new ();
	  g_object_set_data (G_OBJECT (ow->widget), "scanner-option", ow);
	  for (i = 1; i <= list[0]; i++)
	    {
	      gchar *text = format_word (desc, list[i]);

	      gimp_option_menu_append (ow->widget, text,
				       G_CALLBACK (option_menu_item_selected),
				       GINT_TO_POINTER (i - 1));
	      g_free (text);
	    }
	  gimp_option_menu_set_history (ow->widget,
					word_list_position (desc,
							    *(SANE_Word *) value));
	  controls = ow->widget;
	}
      else
	{
	  gdouble lower, upper, step;
	  gint    digits = range_digits (desc);

	  if (desc->constraint_type == SANE_CONSTRAINT_RANGE)
	    {
	      lower = word_to_double (desc, desc->constraint.range->min);
	      upper = word_to_double (desc, desc->constraint.range->max);
	    }
	  else if (desc->type == SANE_TYPE_FIXED)
	    {
	      lower = -32768.0;
	      upper = 32767.99;
	    }
	  else
	    {
	      lower = G_MININT32;
	      upper = G_MAXINT32;
	    }
	  step = range_step (desc);

	  ow->adjustment = gtk_adjustment_new (word_to_double (desc,
							       *(SANE_Word *) value),
					       lower, upper, step,
					       step * 10.0, 0.0);
	  ow->widget = option_spin_new (ow->adjustment, digits);
	  g_signal_connect (ow->adjustment, "value-changed",
			    G_CALLBACK (option_adjustment_changed), ow);

	  if (desc->constraint_type == SANE_CONSTRAINT_RANGE && ! compact)
	    {
	      GtkWidget *scale;

	      ow->kind = KIND_RANGE;
	      controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	      scale = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL,
				     ow->adjustment);
	      gtk_scale_set_draw_value (GTK_SCALE (scale), FALSE);
	      gtk_widget_set_hexpand (scale, TRUE);
	      gtk_widget_set_size_request (scale, 80, -1);
	      gtk_box_append (GTK_BOX (controls), scale);
	      gtk_box_append (GTK_BOX (controls), ow->widget);
	    }
	  else
	    {
	      ow->kind = KIND_SPIN;
	      controls = ow->widget;
	    }

	  unit = unit_name (desc->unit);
	}
      break;

    case SANE_TYPE_STRING:
      if (desc->constraint_type == SANE_CONSTRAINT_STRING_LIST)
	{
	  gint position;

	  ow->kind   = KIND_STRING_LIST;
	  ow->widget = gimp_option_menu_new ();
	  g_object_set_data (G_OBJECT (ow->widget), "scanner-option", ow);
	  for (i = 0; desc->constraint.string_list[i]; i++)
	    gimp_option_menu_append (ow->widget,
				     desc->constraint.string_list[i],
				     G_CALLBACK (option_menu_item_selected),
				     GINT_TO_POINTER (i));
	  position = string_list_position (desc, value);
	  if (position >= 0)
	    gimp_option_menu_set_history (ow->widget, position);
	}
      else
	{
	  GtkEventController *focus;

	  ow->kind   = KIND_STRING;
	  ow->widget = gtk_entry_new ();
	  if (desc->size > 1)
	    gtk_entry_set_max_length (GTK_ENTRY (ow->widget), desc->size - 1);
	  gtk_editable_set_text (GTK_EDITABLE (ow->widget), value);
	  g_signal_connect (ow->widget, "activate",
			    G_CALLBACK (option_entry_apply), ow);
	  focus = gtk_event_controller_focus_new ();
	  g_signal_connect (focus, "leave",
			    G_CALLBACK (option_entry_focus_leave), ow);
	  gtk_widget_add_controller (ow->widget, focus);
	}
      controls = ow->widget;
      break;

    case SANE_TYPE_BUTTON:
    default:
      ow->kind   = KIND_BUTTON;
      ow->widget = gtk_button_new_with_label (title);
      gtk_widget_set_halign (ow->widget, GTK_ALIGN_START);
      g_signal_connect (ow->widget, "clicked",
			G_CALLBACK (option_button_clicked), ow);
      controls = ow->widget;
      labelled = FALSE;
      break;
    }

  row = grid_next_row (grid);

  if (labelled)
    {
      GtkWidget *label = gtk_label_new (title);

      /* Long titles wrap, but not down to a word a line: the value
       * widgets would take all the room they are offered.
       */
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_label_set_wrap (GTK_LABEL (label), TRUE);
      gtk_label_set_width_chars (GTK_LABEL (label), 14);
      gtk_label_set_max_width_chars (GTK_LABEL (label), 20);
      option_widget_attach (ow, grid, label, row, 0, 1, tooltip);

      gtk_widget_set_hexpand (controls, TRUE);
      option_widget_attach (ow, grid, controls, row, 1, 1, tooltip);
    }
  else
    {
      /* Check buttons and buttons carry their own label.  */
      option_widget_attach (ow, grid, controls, row, 0, 2, tooltip);
    }

  if (unit)
    {
      GtkWidget *label = gtk_label_new (unit);

      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      option_widget_attach (ow, grid, label, row, 2, 1, tooltip);
    }

  if (! settable)
    gtk_widget_set_sensitive (controls, FALSE);

  if (settable && (desc->cap & SANE_CAP_AUTOMATIC) &&
      desc->type != SANE_TYPE_BUTTON)
    {
      GtkWidget *button = gtk_button_new_with_label ("Auto");

      g_signal_connect (button, "clicked",
			G_CALLBACK (option_auto_clicked), ow);
      option_widget_attach (ow, grid, button, row, 3, 1,
			    "Let the scanner choose the value");
    }

  g_ptr_array_add (sd.option_widgets, ow);
  g_free (value);

  return ow;
}

/*  Shows or hides the expert options, and the group headers with
 *  nothing left to show under them.
 */
static void
scanner_update_expert (void)
{
  GtkCheckButton *check   = GTK_CHECK_BUTTON (sd.expert_check);
  gboolean        expert  = gtk_check_button_get_active (check);
  gboolean        any     = FALSE;
  gboolean        experts = FALSE;
  guint           i;
  gint            k;

  for (i = 0; i < sd.group_headers->len; i++)
    gtk_widget_set_visible (g_ptr_array_index (sd.group_headers, i), FALSE);

  for (i = 0; i < sd.option_widgets->len; i++)
    {
      OptionWidget *ow = g_ptr_array_index (sd.option_widgets, i);
      gboolean      visible;

      if (! ow->advanced)
	continue;

      visible = expert || ! ow->expert;
      for (k = 0; k < ow->n_parts; k++)
	gtk_widget_set_visible (ow->parts[k], visible);
      if (visible && ow->group)
	gtk_widget_set_visible (ow->group, TRUE);

      any     = TRUE;
      experts = experts || ow->expert;
    }

  gtk_widget_set_visible (sd.advanced_expander, any);
  gtk_widget_set_visible (sd.expert_check, experts);
}

static void
scanner_expert_toggled (GtkCheckButton *button,
			gpointer        data)
{
  scanner_update_expert ();
}

static void
scanner_update_info (void)
{
  SANE_Parameters params;
  gchar          *text;
  const gchar    *kind;

  if (! sd.handle || scanner_busy () ||
      sane_get_parameters (sd.handle, &params) != SANE_STATUS_GOOD ||
      params.pixels_per_line <= 0)
    {
      gtk_label_set_text (GTK_LABEL (sd.info_label), "");
      return;
    }

  if (params.format == SANE_FRAME_GRAY)
    kind = (params.depth == 1) ? "black and white" : "gray";
  else
    kind = "color";

  if (params.lines > 0)
    {
      guint64  bytes = (guint64) params.pixels_per_line * params.lines *
		       ((params.format == SANE_FRAME_GRAY) ? 1 : 3);
      gchar   *size  = g_format_size (bytes);

      text = g_strdup_printf ("Image: %d \303\227 %d pixels, %s, %s",
			      params.pixels_per_line, params.lines, kind,
			      size);
      g_free (size);
    }
  else
    {
      text = g_strdup_printf ("Image: %d pixels wide, %s, length not "
			      "known before the scan",
			      params.pixels_per_line, kind);
    }

  gtk_label_set_text (GTK_LABEL (sd.info_label), text);
  g_free (text);
}


/*  The scan area  */

static void
scanner_preview_clear (void)
{
  if (sd.preview)
    {
      cairo_surface_destroy (sd.preview);
      sd.preview = NULL;
    }
  if (sd.preview_area)
    gtk_widget_queue_draw (sd.preview_area);
}

/*  Reads the area from the device into the spin buttons and the view.  */
static void
scanner_area_read (void)
{
  gint k;

  if (! sd.have_area)
    return;

  for (k = 0; k < 4; k++)
    option_get_number (sd.handle, sd.opts.area[k], &sd.area[k]);

  sd.updating++;
  for (k = 0; k < 4; k++)
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (sd.area_spins[k]),
			       sd.area[k]);
  sd.updating--;

  gtk_widget_queue_draw (sd.preview_area);
}

/*  Gives the device the area, and shows what it made of it.  */
static void
scanner_area_write (void)
{
  SANE_Int info;

  if (! sd.have_area || ! sd.handle)
    return;

  info = options_set_area (sd.handle, &sd.opts, sd.area);
  if (info & SANE_INFO_RELOAD_OPTIONS)
    scanner_schedule_rebuild ();

  scanner_area_read ();
  scanner_update_info ();
}

/*  Takes the bed and the ranges of the spin buttons from the geometry
 *  options.
 */
static void
scanner_area_setup (void)
{
  const SANE_Option_Descriptor *desc[4];
  const gchar                  *unit;
  gint                          k;

  sd.have_area = options_have_area (sd.handle, &sd.opts);
  gtk_widget_set_visible (sd.area_box, sd.have_area);

  if (! sd.have_area)
    {
      scanner_preview_clear ();
      return;
    }

  for (k = 0; k < 4; k++)
    desc[k] = option_desc (sd.handle, sd.opts.area[k]);

  sd.bed[0] = word_to_double (desc[0], desc[0]->constraint.range->min);
  sd.bed[1] = word_to_double (desc[1], desc[1]->constraint.range->min);
  sd.bed[2] = word_to_double (desc[2], desc[2]->constraint.range->max);
  sd.bed[3] = word_to_double (desc[3], desc[3]->constraint.range->max);

  if (sd.bed[2] <= sd.bed[0] || sd.bed[3] <= sd.bed[1])
    {
      sd.have_area = FALSE;
      gtk_widget_set_visible (sd.area_box, FALSE);
      return;
    }

  /* A preview of another bed (another source, say) is no use.  */
  if (sd.preview && memcmp (sd.bed, sd.preview_bed, sizeof (sd.bed)) != 0)
    scanner_preview_clear ();

  sd.updating++;
  for (k = 0; k < 4; k++)
    {
      GtkSpinButton *spin = GTK_SPIN_BUTTON (sd.area_spins[k]);
      gdouble        step = range_step (desc[k]);
      gint           digits;

      /* Millimetres get a decimal even when the backend steps by whole
       * ones, pixels never do.
       */
      digits = (desc[k]->type == SANE_TYPE_FIXED) ?
	MAX (1, range_digits (desc[k])) : 0;

      gtk_spin_button_set_digits (spin, digits);
      gtk_spin_button_set_range (spin,
				 word_to_double (desc[k],
						 desc[k]->constraint.range->min),
				 word_to_double (desc[k],
						 desc[k]->constraint.range->max));
      gtk_spin_button_set_increments (spin, step, step * 10.0);

      unit = unit_name (desc[k]->unit);
      gtk_label_set_text (GTK_LABEL (sd.area_units[k]), unit ? unit : "");
    }
  sd.updating--;

  scanner_area_read ();
}

static void
scanner_area_spin_changed (GtkSpinButton *spin,
			   gpointer       data)
{
  gint k = GPOINTER_TO_INT (data);

  if (sd.updating || ! sd.have_area)
    return;

  sd.area[k] = gtk_spin_button_get_value (spin);
  scanner_area_write ();
}

static void
scanner_full_area_clicked (GtkButton *button,
			   gpointer   data)
{
  memcpy (sd.area, sd.bed, sizeof (sd.area));
  scanner_area_write ();
}

/*  The area in widget coordinates, sorted.  */
static void
preview_area_rect (gdouble *x0,
		   gdouble *y0,
		   gdouble *x1,
		   gdouble *y1)
{
  gdouble ax0 = MIN (sd.area[0], sd.area[2]);
  gdouble ay0 = MIN (sd.area[1], sd.area[3]);
  gdouble ax1 = MAX (sd.area[0], sd.area[2]);
  gdouble ay1 = MAX (sd.area[1], sd.area[3]);

  *x0 = sd.view_x + (ax0 - sd.bed[0]) * sd.view_scale;
  *y0 = sd.view_y + (ay0 - sd.bed[1]) * sd.view_scale;
  *x1 = sd.view_x + (ax1 - sd.bed[0]) * sd.view_scale;
  *y1 = sd.view_y + (ay1 - sd.bed[1]) * sd.view_scale;
}

static void
preview_layout (gint width,
		gint height)
{
  gdouble bed_width  = sd.bed[2] - sd.bed[0];
  gdouble bed_height = sd.bed[3] - sd.bed[1];
  gdouble margin     = 8.0;

  sd.view_scale = MIN ((width - 2 * margin) / bed_width,
		       (height - 2 * margin) / bed_height);
  sd.view_scale = MAX (sd.view_scale, 1e-6);
  sd.view_x = floor ((width - bed_width * sd.view_scale) / 2.0);
  sd.view_y = floor ((height - bed_height * sd.view_scale) / 2.0);
}

static void
preview_draw (GtkDrawingArea *area,
	      cairo_t        *cr,
	      int             width,
	      int             height,
	      gpointer        data)
{
  gdouble bw, bh;
  gdouble x0, y0, x1, y1;
  gint    k;

  if (! sd.have_area)
    return;

  preview_layout (width, height);
  bw = (sd.bed[2] - sd.bed[0]) * sd.view_scale;
  bh = (sd.bed[3] - sd.bed[1]) * sd.view_scale;

  /* The bed, with the preview on it.  */
  if (sd.preview)
    {
      gint pw = cairo_image_surface_get_width (sd.preview);
      gint ph = cairo_image_surface_get_height (sd.preview);

      cairo_save (cr);
      cairo_rectangle (cr, sd.view_x, sd.view_y, bw, bh);
      cairo_clip (cr);
      cairo_translate (cr, sd.view_x, sd.view_y);
      cairo_scale (cr, bw / pw, bh / ph);
      cairo_set_source_surface (cr, sd.preview, 0, 0);
      cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_GOOD);
      cairo_paint (cr);
      cairo_restore (cr);
    }
  else
    {
      cairo_rectangle (cr, sd.view_x, sd.view_y, bw, bh);
      cairo_set_source_rgb (cr, 0.94, 0.94, 0.94);
      cairo_fill (cr);
    }

  cairo_rectangle (cr, sd.view_x - 0.5, sd.view_y - 0.5, bw + 1.0, bh + 1.0);
  cairo_set_source_rgb (cr, 0.55, 0.55, 0.55);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);

  /* Dim what is outside the area.  */
  preview_area_rect (&x0, &y0, &x1, &y1);
  cairo_rectangle (cr, sd.view_x, sd.view_y, bw, bh);
  cairo_rectangle (cr, x0, y0, x1 - x0, y1 - y0);
  cairo_set_fill_rule (cr, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.5);
  cairo_fill (cr);
  cairo_set_fill_rule (cr, CAIRO_FILL_RULE_WINDING);

  /* The outline, and handles at the corners.  */
  cairo_rectangle (cr, x0, y0, x1 - x0, y1 - y0);
  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_set_line_width (cr, 3.0);
  cairo_stroke (cr);
  cairo_rectangle (cr, x0, y0, x1 - x0, y1 - y0);
  cairo_set_source_rgb (cr, 0.11, 0.44, 0.85);
  cairo_set_line_width (cr, 1.5);
  cairo_stroke (cr);

  for (k = 0; k < 4; k++)
    {
      gdouble cx = (k & 1) ? x1 : x0;
      gdouble cy = (k & 2) ? y1 : y0;

      cairo_rectangle (cr, cx - 3.5, cy - 3.5, 7.0, 7.0);
      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
      cairo_fill_preserve (cr);
      cairo_set_source_rgb (cr, 0.11, 0.44, 0.85);
      cairo_set_line_width (cr, 1.0);
      cairo_stroke (cr);
    }

  /* Until there is a preview, a hint on top of it all.  */
  if (! sd.preview)
    {
      PangoLayout *layout;
      gint         lw, lh;
      gdouble      tx, ty;

      layout = gtk_widget_create_pango_layout (GTK_WIDGET (area),
					       "Click Preview to see\n"
					       "what is on the scanner");
      pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
      pango_layout_get_pixel_size (layout, &lw, &lh);
      tx = floor (sd.view_x + (bw - lw) / 2.0);
      ty = floor (sd.view_y + (bh - lh) / 2.0);

      cairo_rectangle (cr, tx - 8.0, ty - 5.0, lw + 16.0, lh + 10.0);
      cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.85);
      cairo_fill (cr);

      cairo_set_source_rgb (cr, 0.3, 0.3, 0.3);
      cairo_move_to (cr, tx, ty);
      pango_cairo_show_layout (cr, layout);
      g_object_unref (layout);
    }
}

/*  Which edges of the area are at x, y, and whether it is inside.  */
static gint
preview_hit (gdouble   x,
	     gdouble   y,
	     gboolean *inside)
{
  gdouble x0, y0, x1, y1;
  gint    edges = 0;

  preview_area_rect (&x0, &y0, &x1, &y1);

  if (y >= y0 - GRAB_DISTANCE && y <= y1 + GRAB_DISTANCE)
    {
      if (fabs (x - x0) <= GRAB_DISTANCE)
	edges |= EDGE_LEFT;
      else if (fabs (x - x1) <= GRAB_DISTANCE)
	edges |= EDGE_RIGHT;
    }
  if (x >= x0 - GRAB_DISTANCE && x <= x1 + GRAB_DISTANCE)
    {
      if (fabs (y - y0) <= GRAB_DISTANCE)
	edges |= EDGE_TOP;
      else if (fabs (y - y1) <= GRAB_DISTANCE)
	edges |= EDGE_BOTTOM;
    }

  if (inside)
    *inside = (x > x0 && x < x1 && y > y0 && y < y1);

  return edges;
}

static void
preview_to_bed (gdouble  x,
		gdouble  y,
		gboolean clamp,
		gdouble *bx,
		gdouble *by)
{
  *bx = sd.bed[0] + (x - sd.view_x) / sd.view_scale;
  *by = sd.bed[1] + (y - sd.view_y) / sd.view_scale;

  if (clamp)
    {
      *bx = CLAMP (*bx, sd.bed[0], sd.bed[2]);
      *by = CLAMP (*by, sd.bed[1], sd.bed[3]);
    }
}

static void
preview_motion (GtkEventControllerMotion *controller,
		gdouble                   x,
		gdouble                   y,
		gpointer                  data)
{
  const gchar *cursor = "crosshair";
  gboolean     inside;
  gint         edges;

  if (sd.drag_mode != DRAG_NONE || ! sd.have_area)
    return;

  edges = preview_hit (x, y, &inside);
  switch (edges)
    {
    case EDGE_LEFT | EDGE_TOP:     cursor = "nw-resize"; break;
    case EDGE_RIGHT | EDGE_TOP:    cursor = "ne-resize"; break;
    case EDGE_LEFT | EDGE_BOTTOM:  cursor = "sw-resize"; break;
    case EDGE_RIGHT | EDGE_BOTTOM: cursor = "se-resize"; break;
    case EDGE_LEFT:
    case EDGE_RIGHT:               cursor = "ew-resize"; break;
    case EDGE_TOP:
    case EDGE_BOTTOM:              cursor = "ns-resize"; break;
    default:
      if (inside)
	cursor = "move";
      break;
    }

  gtk_widget_set_cursor_from_name (sd.preview_area, cursor);
}

static void
preview_drag_begin (GtkGestureDrag *gesture,
		    gdouble         x,
		    gdouble         y,
		    gpointer        data)
{
  gboolean inside;

  if (! sd.have_area || ! sd.handle || scanner_busy ())
    {
      gtk_gesture_set_state (GTK_GESTURE (gesture),
			     GTK_EVENT_SEQUENCE_DENIED);
      return;
    }

  memcpy (sd.drag_area, sd.area, sizeof (sd.area));
  preview_to_bed (x, y, TRUE, &sd.drag_start[0], &sd.drag_start[1]);

  sd.drag_edges = preview_hit (x, y, &inside);
  if (sd.drag_edges)
    sd.drag_mode = DRAG_RESIZE;
  else if (inside)
    sd.drag_mode = DRAG_MOVE;
  else
    sd.drag_mode = DRAG_NEW;
}

static void
preview_drag_update (GtkGestureDrag *gesture,
		     gdouble         offset_x,
		     gdouble         offset_y,
		     gpointer        data)
{
  gdouble start_x, start_y;
  gdouble bx, by;
  gdouble dx, dy, w, h;
  gint    k;

  if (sd.drag_mode == DRAG_NONE)
    return;

  gtk_gesture_drag_get_start_point (gesture, &start_x, &start_y);

  switch (sd.drag_mode)
    {
    case DRAG_NEW:
      preview_to_bed (start_x + offset_x, start_y + offset_y, TRUE, &bx, &by);
      sd.area[0] = MIN (sd.drag_start[0], bx);
      sd.area[1] = MIN (sd.drag_start[1], by);
      sd.area[2] = MAX (sd.drag_start[0], bx);
      sd.area[3] = MAX (sd.drag_start[1], by);
      break;

    case DRAG_MOVE:
      dx = offset_x / sd.view_scale;
      dy = offset_y / sd.view_scale;
      w  = sd.drag_area[2] - sd.drag_area[0];
      h  = sd.drag_area[3] - sd.drag_area[1];
      sd.area[0] = CLAMP (sd.drag_area[0] + dx, sd.bed[0], sd.bed[2] - w);
      sd.area[1] = CLAMP (sd.drag_area[1] + dy, sd.bed[1], sd.bed[3] - h);
      sd.area[2] = sd.area[0] + w;
      sd.area[3] = sd.area[1] + h;
      break;

    case DRAG_RESIZE:
      preview_to_bed (start_x + offset_x, start_y + offset_y, TRUE, &bx, &by);
      memcpy (sd.area, sd.drag_area, sizeof (sd.area));
      if (sd.drag_edges & EDGE_LEFT)
	sd.area[0] = MIN (bx, sd.area[2]);
      if (sd.drag_edges & EDGE_RIGHT)
	sd.area[2] = MAX (bx, sd.area[0]);
      if (sd.drag_edges & EDGE_TOP)
	sd.area[1] = MIN (by, sd.area[3]);
      if (sd.drag_edges & EDGE_BOTTOM)
	sd.area[3] = MAX (by, sd.area[1]);
      break;

    default:
      break;
    }

  /* The numbers follow the pointer; the device gets the area when the
   * button is released.
   */
  sd.updating++;
  for (k = 0; k < 4; k++)
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (sd.area_spins[k]),
			       sd.area[k]);
  sd.updating--;

  gtk_widget_queue_draw (sd.preview_area);
}

static void
preview_drag_end (GtkGestureDrag *gesture,
		  gdouble         offset_x,
		  gdouble         offset_y,
		  gpointer        data)
{
  if (sd.drag_mode == DRAG_NONE)
    return;

  /* A click without a drag leaves the area as it was.  */
  if (sd.drag_mode == DRAG_NEW &&
      (fabs (offset_x) < 3.0 || fabs (offset_y) < 3.0))
    memcpy (sd.area, sd.drag_area, sizeof (sd.area));

  sd.drag_mode = DRAG_NONE;
  scanner_area_write ();
}

/*  Shows data, a scan of the whole bed, as the preview.  */
static void
scanner_preview_set (const guchar *data,
		     gint          width,
		     gint          height,
		     gint          bpp)
{
  cairo_surface_t *surface;
  guchar          *pixels;
  gint             stride;
  gint             x, y;

  surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24, width, height);
  if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (surface);
      return;
    }

  cairo_surface_flush (surface);
  pixels = cairo_image_surface_get_data (surface);
  stride = cairo_image_surface_get_stride (surface);

  for (y = 0; y < height; y++)
    {
      const guchar *src  = data + (gsize) y * width * bpp;
      guint32      *dest = (guint32 *) (pixels + (gsize) y * stride);

      for (x = 0; x < width; x++, src += bpp)
	{
	  guint32 r = src[0];
	  guint32 g = (bpp == 3) ? src[1] : src[0];
	  guint32 b = (bpp == 3) ? src[2] : src[0];

	  dest[x] = (r << 16) | (g << 8) | b;
	}
    }
  cairo_surface_mark_dirty (surface);

  scanner_preview_clear ();
  sd.preview = surface;
  memcpy (sd.preview_bed, sd.bed, sizeof (sd.bed));
  gtk_widget_queue_draw (sd.preview_area);
}


/*  Building the option widgets  */

static void
scanner_clear_options (void)
{
  /* Entries losing the focus as they go must not apply their text.  */
  sd.updating++;
  grid_clear (sd.common_grid);
  grid_clear (sd.advanced_grid);
  g_ptr_array_set_size (sd.option_widgets, 0);
  g_ptr_array_set_size (sd.group_headers, 0);
  sd.updating--;
}

static void
scanner_build_options (void)
{
  GtkWidget *group = NULL;
  gint       count;
  gint       i, k;

  sd.rebuild_needed = FALSE;
  scanner_clear_options ();

  if (! sd.handle)
    return;

  /* The widgets take the device's values as they are made.  */
  sd.updating++;

  options_find (sd.handle, &sd.opts);
  scanner_area_setup ();

  /* The common options.  */
  option_widget_new (sd.opts.source, sd.common_grid, "Source:", FALSE);
  option_widget_new (sd.opts.mode, sd.common_grid, "Mode:", FALSE);
  option_widget_new (sd.opts.resolution, sd.common_grid, "Resolution:",
		     TRUE);
  gtk_widget_set_visible (sd.common_grid,
			  gtk_widget_get_first_child (sd.common_grid) != NULL);

  /* Everything else, in the backend's groups.  */
  count = option_count (sd.handle);
  for (i = 1; i < count; i++)
    {
      const SANE_Option_Descriptor *desc = option_desc (sd.handle, i);
      OptionWidget                 *ow;
      gboolean                      handled;

      if (! desc)
	continue;

      if (desc->type == SANE_TYPE_GROUP)
	{
	  gchar *markup;

	  group = gtk_label_new (NULL);
	  markup = g_markup_printf_escaped ("<b>%s</b>",
					    desc->title ? desc->title : "");
	  gtk_label_set_markup (GTK_LABEL (group), markup);
	  g_free (markup);
	  gtk_label_set_xalign (GTK_LABEL (group), 0.0);
	  gtk_widget_set_margin_top (group, 6);
	  gtk_grid_attach (GTK_GRID (sd.advanced_grid), group,
			   0, grid_next_row (sd.advanced_grid), 4, 1);
	  g_ptr_array_add (sd.group_headers, group);
	  continue;
	}

      handled = (i == sd.opts.source || i == sd.opts.mode ||
		 i == sd.opts.resolution || i == sd.opts.preview);
      for (k = 0; k < 4 && sd.have_area; k++)
	if (i == sd.opts.area[k])
	  handled = TRUE;
      if (handled)
	continue;

      ow = option_widget_new (i, sd.advanced_grid, NULL, FALSE);
      if (ow)
	{
	  ow->group    = group;
	  ow->advanced = TRUE;
	}
    }

  sd.updating--;

  scanner_update_expert ();
  scanner_update_info ();
  scanner_update_sensitivity ();
}

static gboolean
scanner_rebuild_idle (gpointer data)
{
  sd.rebuild_idle = 0;

  /* Not while a thread uses the device; the scan's end rebuilds.  */
  if (scanner_busy ())
    sd.rebuild_needed = TRUE;
  else
    scanner_build_options ();

  return G_SOURCE_REMOVE;
}

/*  Rebuilds the widgets once the handler that asked for it has returned
 *  (it may belong to a widget about to go).
 */
static void
scanner_schedule_rebuild (void)
{
  if (! sd.rebuild_idle)
    sd.rebuild_idle = g_idle_add (scanner_rebuild_idle, NULL);
}


/*  Devices  */

static void
device_entry_free (gpointer data)
{
  DeviceEntry *entry = data;

  g_free (entry->name);
  g_free (entry->label);
  g_free (entry);
}

static void
scanner_close_device (void)
{
  if (sd.handle)
    {
      options_store_vals (sd.handle, sd.device);
      scanner_clear_options ();
      sane_close (sd.handle);
      sd.handle = NULL;
    }

  g_free (sd.device);
  sd.device = NULL;
  sd.have_area = FALSE;
  scanner_preview_clear ();
}

static void
scanner_open_device (const gchar *name)
{
  SANE_Handle handle;
  SANE_Status status;

  scanner_close_device ();
  scanner_set_status (NULL, FALSE);

  status = sane_open (name, &handle);
  if (status != SANE_STATUS_GOOD)
    {
      gchar *text;

      text = g_strdup_printf ("The scanner %s could not be opened: %s.",
			      name, sane_strstatus (status));
      scanner_set_message (text);
      g_free (text);
      scanner_update_sensitivity ();
      return;
    }

  sd.handle = handle;
  sd.device = g_strdup (name);

  /* The settings of last time, once, on the device they were made on.  */
  if (! sd.vals_applied && strcmp (name, scanner_vals.device) == 0)
    options_apply_vals (handle);
  sd.vals_applied = TRUE;

  scanner_set_message (NULL);
  scanner_build_options ();
}

static void
scanner_device_selected (GtkWidget *menu,
			 gpointer   data)
{
  gint index = GPOINTER_TO_INT (data);

  if (! sd.devices || index < 0 || (guint) index >= sd.devices->len)
    return;

  scanner_open_device (((DeviceEntry *) g_ptr_array_index (sd.devices,
							   index))->name);
}

static gboolean
scanner_devices_found (gpointer data)
{
  GPtrArray   *devices = data;
  const gchar *wanted;
  gint         selected = 0;
  guint        i;

  g_thread_join (sd.devices_thread);
  sd.devices_thread = NULL;
  scanner_progress_stop ();

  if (sd.closing)
    {
      g_ptr_array_unref (devices);
      gtk_window_destroy (GTK_WINDOW (sd.dialog));
      return G_SOURCE_REMOVE;
    }

  if (sd.devices)
    g_ptr_array_unref (sd.devices);
  sd.devices = devices;

  /* Stay with the open device, or go back to last time's.  */
  wanted = sd.device ? sd.device : scanner_vals.device;

  gimp_option_menu_clear (sd.device_menu);
  for (i = 0; i < devices->len; i++)
    {
      DeviceEntry *entry = g_ptr_array_index (devices, i);

      gimp_option_menu_append (sd.device_menu, entry->label,
			       G_CALLBACK (scanner_device_selected),
			       GINT_TO_POINTER (i));
      if (strcmp (entry->name, wanted) == 0)
	selected = i;
    }

  if (devices->len == 0)
    {
      scanner_close_device ();
      gimp_option_menu_append (sd.device_menu, "No scanner", NULL, NULL);
      scanner_set_message (NO_SCANNERS_MESSAGE);
    }
  else
    {
      DeviceEntry *entry = g_ptr_array_index (devices, selected);

      gimp_option_menu_set_history (sd.device_menu, selected);

      if (! sd.device || strcmp (sd.device, entry->name) != 0)
	scanner_open_device (entry->name);
      else
	scanner_set_message (NULL);
    }

  scanner_update_sensitivity ();
  scanner_update_info ();

  return G_SOURCE_REMOVE;
}

/*  Asks SANE for the devices; network backends may take a while, so
 *  this runs in a thread while the dialog shows that it is looking.
 */
static gpointer
scanner_devices_thread (gpointer data)
{
  const SANE_Device **list = NULL;
  GPtrArray          *devices;
  gint                i;

  devices = g_ptr_array_new_with_free_func (device_entry_free);

  if (sane_get_devices (&list, SANE_FALSE) == SANE_STATUS_GOOD && list)
    {
      for (i = 0; list[i]; i++)
	{
	  DeviceEntry *entry = g_new0 (DeviceEntry, 1);
	  const gchar *vendor = list[i]->vendor ? list[i]->vendor : "";
	  const gchar *model  = list[i]->model ? list[i]->model : "";

	  entry->name  = g_strdup (list[i]->name);
	  entry->label = g_strdup_printf ("%s%s%s (%s)", vendor,
					  (vendor[0] && model[0]) ? " " : "",
					  model, list[i]->name);
	  g_ptr_array_add (devices, entry);
	}
    }

  g_idle_add (scanner_devices_found, devices);

  return NULL;
}

static void
scanner_refresh (void)
{
  if (scanner_busy ())
    return;

  scanner_set_status (NULL, FALSE);
  if (! sd.handle)
    scanner_set_message ("Looking for scanners...");
  scanner_progress_start ("Looking for scanners...");

  sd.devices_thread = g_thread_new ("scanner-devices",
				    scanner_devices_thread, NULL);
  scanner_update_sensitivity ();
}

static void
scanner_refresh_clicked (GtkButton *button,
			 gpointer   data)
{
  scanner_refresh ();
}


/*  Scanning  */

static gboolean
scanner_source_is_feeder (void)
{
  gchar   *source = option_get_string (sd.handle, sd.opts.source);
  gboolean feeder = FALSE;

  if (source)
    {
      gchar *lower = g_ascii_strdown (source, -1);

      feeder = (strstr (lower, "adf") != NULL ||
		strstr (lower, "feeder") != NULL ||
		strstr (lower, "duplex") != NULL);
      g_free (lower);
      g_free (source);
    }

  return feeder;
}

/*  The resolution for a preview about as wide as the view.  */
static gboolean
scanner_preview_resolution (gdouble *resolution)
{
  const SANE_Option_Descriptor *desc;
  const SANE_Option_Descriptor *area_desc;
  gdouble                       wanted;
  gdouble                       pixels;
  gint                          i;

  desc = option_desc (sd.handle, sd.opts.resolution);
  if (! option_is_settable (desc) || ! option_is_number (desc))
    return FALSE;

  pixels = MAX (PREVIEW_PIXELS, gtk_widget_get_width (sd.preview_area)) *
	   gtk_widget_get_scale_factor (sd.preview_area);

  area_desc = option_desc (sd.handle, sd.opts.area[0]);
  if (sd.have_area && area_desc->unit == SANE_UNIT_MM)
    wanted = pixels / ((sd.bed[2] - sd.bed[0]) / 25.4);
  else
    wanted = 0.0;	/* the lowest there is */

  if (desc->constraint_type == SANE_CONSTRAINT_WORD_LIST)
    {
      const SANE_Word *list  = desc->constraint.word_list;
      gdouble          best  = -1.0;
      gdouble          most  = -1.0;

      /* The lowest at least as high as wanted, or the highest.  */
      for (i = 1; i <= list[0]; i++)
	{
	  gdouble value = word_to_double (desc, list[i]);

	  if (value >= wanted && (best < 0.0 || value < best))
	    best = value;
	  most = MAX (most, value);
	}
      if (best < 0.0)
	best = most;
      if (best <= 0.0)
	return FALSE;

      *resolution = best;
    }
  else if (desc->constraint_type == SANE_CONSTRAINT_RANGE)
    {
      gdouble min   = word_to_double (desc, desc->constraint.range->min);
      gdouble max   = word_to_double (desc, desc->constraint.range->max);
      gdouble quant = word_to_double (desc, desc->constraint.range->quant);

      quant = fabs (quant);

      wanted = CLAMP (wanted, min, max);
      if (quant > 0.0)
	wanted = MIN (max, min + ceil ((wanted - min) / quant) * quant);

      *resolution = wanted;
    }
  else
    {
      if (wanted <= 0.0)
	return FALSE;

      *resolution = ceil (wanted);
    }

  return TRUE;
}

/*  Sets the device up for a preview: the whole bed at a low resolution,
 *  remembering what the user had.
 */
static void
scanner_preview_setup (void)
{
  const SANE_Option_Descriptor *desc;
  gdouble                       resolution;
  SANE_Int                      info = 0;
  SANE_Int                      one  = 0;

  memcpy (sd.saved_area, sd.area, sizeof (sd.area));
  if (! option_get_number (sd.handle, sd.opts.resolution,
			   &sd.saved_resolution))
    sd.saved_resolution = -1.0;

  sd.saved_preview = SANE_FALSE;
  desc = option_desc (sd.handle, sd.opts.preview);
  if (option_is_settable (desc) && desc->type == SANE_TYPE_BOOL &&
      desc->size == (SANE_Int) sizeof (SANE_Word))
    {
      SANE_Bool *value = option_get (sd.handle, sd.opts.preview);
      SANE_Bool  on    = SANE_TRUE;

      if (value)
	{
	  sd.saved_preview = *value;
	  g_free (value);
	}
      sane_control_option (sd.handle, sd.opts.preview, SANE_ACTION_SET_VALUE,
			   &on, &one);
      info |= one;
    }

  if (sd.have_area)
    info |= options_set_area (sd.handle, &sd.opts, sd.bed);

  if (sd.saved_resolution > 0.0 && scanner_preview_resolution (&resolution))
    {
      one = 0;
      option_set_number (sd.handle, sd.opts.resolution, resolution, &one);
      info |= one;
    }

  if (info & SANE_INFO_RELOAD_OPTIONS)
    sd.rebuild_needed = TRUE;
}

static void
scanner_preview_restore (void)
{
  const SANE_Option_Descriptor *desc;
  SANE_Int                      info = 0;
  SANE_Int                      one  = 0;

  desc = option_desc (sd.handle, sd.opts.preview);
  if (option_is_settable (desc) && desc->type == SANE_TYPE_BOOL &&
      desc->size == (SANE_Int) sizeof (SANE_Word))
    {
      sane_control_option (sd.handle, sd.opts.preview, SANE_ACTION_SET_VALUE,
			   &sd.saved_preview, &one);
      info |= one;
    }

  if (sd.saved_resolution > 0.0)
    {
      one = 0;
      option_set_number (sd.handle, sd.opts.resolution, sd.saved_resolution,
			 &one);
      info |= one;
    }

  if (sd.have_area)
    info |= options_set_area (sd.handle, &sd.opts, sd.saved_area);

  if (info & SANE_INFO_RELOAD_OPTIONS)
    sd.rebuild_needed = TRUE;
}

static gpointer
scanner_scan_thread (gpointer data)
{
  ScanJob *job = data;

  scan_job_read (job);
  g_idle_add (scanner_scan_done, NULL);

  return NULL;
}

/*  A page has been scanned (or not): make an image of it, or show the
 *  preview, and go on with the next page from a document feeder.
 */
static gboolean
scanner_scan_done (gpointer data)
{
  ScanJob  *job     = sd.job;
  gboolean  preview = sd.job_is_preview;
  gboolean  again   = FALSE;
  gchar    *text;

  g_thread_join (sd.thread);
  sd.thread = NULL;
  sd.job    = NULL;

  if (preview)
    {
      scanner_preview_restore ();

      if (job->status == SANE_STATUS_GOOD)
	{
	  scanner_preview_set (job->data, job->width, job->height, job->bpp);
	  scanner_set_status (NULL, FALSE);
	}
      else if (job->status == SANE_STATUS_CANCELLED)
	{
	  scanner_set_status ("The preview was cancelled.", FALSE);
	}
      else
	{
	  text = scan_job_error (job);
	  scanner_set_status (text, TRUE);
	  g_free (text);
	}
    }
  else if (job->status == SANE_STATUS_GOOD)
    {
      gint32 image = scanner_image_new (job->data, job->width, job->height,
					job->bpp);

      if (image != -1)
	{
	  gchar *name = gimp_image_get_filename (image);

	  scanner_image_display (image);
	  sd.last_image = image;
	  sd.pages++;

	  /* Remember what this was scanned with, and the page count.  */
	  options_store_vals (sd.handle, sd.device);
	  scanner_vals_save ();

	  text = g_strdup_printf ("%s: %d \303\227 %d pixels.",
				  name ? name : "Scanned", job->width,
				  job->height);
	  scanner_set_status (text, FALSE);
	  g_free (text);
	  g_free (name);

	  again = sd.feeder && ! sd.closing;
	}
      else
	{
	  scanner_set_status ("The scanned image could not be made.", TRUE);
	}
    }
  else if (job->status == SANE_STATUS_NO_DOCS && sd.feeder && sd.pages > 0)
    {
      text = g_strdup_printf ("%d %s scanned; the document feeder is empty.",
			      sd.pages, (sd.pages == 1) ? "page" : "pages");
      scanner_set_status (text, FALSE);
      g_free (text);
    }
  else if (job->status == SANE_STATUS_CANCELLED)
    {
      scanner_set_status ("The scan was cancelled.", FALSE);
    }
  else
    {
      text = scan_job_error (job);
      scanner_set_status (text, TRUE);
      g_free (text);
    }

  scan_job_free (job);

  if (sd.closing)
    {
      scanner_progress_stop ();
      gtk_window_destroy (GTK_WINDOW (sd.dialog));
      return G_SOURCE_REMOVE;
    }

  if (again)
    {
      scanner_start_job (FALSE);
      return G_SOURCE_REMOVE;
    }

  scanner_progress_stop ();

  if (sd.rebuild_needed)
    scanner_build_options ();
  else
    {
      guint i;

      /* The preview's settings are back: show them.  */
      for (i = 0; i < sd.option_widgets->len; i++)
	option_widget_update (g_ptr_array_index (sd.option_widgets, i));
      scanner_area_read ();
    }

  scanner_update_sensitivity ();
  scanner_update_info ();

  return G_SOURCE_REMOVE;
}

static void
scanner_start_job (gboolean preview)
{
  gchar *text;

  if (! sd.handle || scanner_busy ())
    return;

  if (preview)
    {
      scanner_preview_setup ();
      text = g_strdup ("Preview...");
    }
  else if (sd.feeder)
    {
      text = g_strdup_printf ("Scanning page %d...", sd.pages + 1);
    }
  else
    {
      text = g_strdup ("Scanning...");
    }

  sd.job            = scan_job_new (sd.handle);
  sd.job_is_preview = preview;

  scanner_progress_start (text);
  g_free (text);
  scanner_update_sensitivity ();
  gtk_label_set_text (GTK_LABEL (sd.info_label), "");

  sd.thread = g_thread_new ("scanner", scanner_scan_thread, sd.job);
}

static void
scanner_scan_clicked (GtkButton *button,
		      gpointer   data)
{
  scanner_set_status (NULL, FALSE);
  sd.pages  = 0;
  sd.feeder = scanner_source_is_feeder ();
  scanner_start_job (FALSE);
}

static void
scanner_preview_clicked (GtkButton *button,
			 gpointer   data)
{
  scanner_set_status (NULL, FALSE);
  sd.pages  = 0;
  sd.feeder = FALSE;
  scanner_start_job (TRUE);
}

static void
scanner_cancel (void)
{
  if (! sd.job || g_atomic_int_get (&sd.job->cancelled))
    return;

  g_atomic_int_set (&sd.job->cancelled, 1);
  /* The SANE standard allows this while another thread reads; the read
   * then returns SANE_STATUS_CANCELLED.
   */
  sane_cancel (sd.handle);

  gtk_progress_bar_set_text (GTK_PROGRESS_BAR (sd.progress), "Cancelling...");
  scanner_update_sensitivity ();
}

static void
scanner_cancel_clicked (GtkButton *button,
			gpointer   data)
{
  scanner_cancel ();
}


/*  Closing  */

/*  Closes the dialog, or, while a thread is busy, hides it and closes
 *  it when the thread is done.
 */
static gboolean
scanner_close_request (GtkWindow *window,
		       gpointer   data)
{
  if (! scanner_busy ())
    return FALSE;

  sd.closing = TRUE;
  scanner_cancel ();
  gtk_widget_set_visible (sd.dialog, FALSE);

  return TRUE;
}

static void
scanner_close_clicked (GtkButton *button,
		       gpointer   data)
{
  if (! scanner_close_request (GTK_WINDOW (sd.dialog), NULL))
    gtk_window_destroy (GTK_WINDOW (sd.dialog));
}

static void
scanner_destroy (GtkWidget *widget,
		 gpointer   data)
{
  gimp_main_loop_quit ();
}


/*  The dialog  */

static GtkWidget *
heading_new (const gchar *text)
{
  GtkWidget *label = gtk_label_new (NULL);
  gchar     *markup;

  markup = g_markup_printf_escaped ("<b>%s</b>", text);
  gtk_label_set_markup (GTK_LABEL (label), markup);
  g_free (markup);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);

  return label;
}

static void
scanner_dialog_new (void)
{
  static const gchar *area_labels[4] = { "_Left:", "_Top:", "R_ight:",
					 "_Bottom:" };
  GtkWidget          *main_box;
  GtkWidget          *hbox;
  GtkWidget          *vbox;
  GtkWidget          *label;
  GtkWidget          *frame;
  GtkWidget          *grid;
  GtkWidget          *scrolled;
  GtkGesture         *drag;
  GtkEventController *motion;
  gint                k;

  sd.dialog = gimp_dialog_new ("Scanner");
  g_signal_connect (sd.dialog, "close-request",
		    G_CALLBACK (scanner_close_request), NULL);
  g_signal_connect (sd.dialog, "destroy",
		    G_CALLBACK (scanner_destroy), NULL);

  sd.scan_button = gimp_dialog_add_button (sd.dialog, "_Scan",
					   G_CALLBACK (scanner_scan_clicked),
					   NULL, TRUE);
  gtk_widget_set_tooltip_text (sd.scan_button,
			       "Scan the area into a new image; the dialog "
			       "stays open for more pages");
  gimp_dialog_add_button (sd.dialog, "_Close",
			  G_CALLBACK (scanner_close_clicked), NULL, FALSE);

  main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gimp_container_set_border_width (main_box, 12);
  gimp_box_pack_start (gimp_dialog_get_vbox (sd.dialog), main_box,
		       TRUE, TRUE, 0);

  /* The device.  */
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_append (GTK_BOX (main_box), hbox);

  label = gtk_label_new_with_mnemonic ("Scanne_r:");
  gtk_box_append (GTK_BOX (hbox), label);

  sd.device_menu = gimp_option_menu_new ();
  gtk_widget_set_hexpand (sd.device_menu, TRUE);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), sd.device_menu);
  gtk_box_append (GTK_BOX (hbox), sd.device_menu);

  sd.refresh_button = gtk_button_new_with_mnemonic ("Re_fresh");
  gtk_widget_set_tooltip_text (sd.refresh_button,
			       "Look for scanners again");
  g_signal_connect (sd.refresh_button, "clicked",
		    G_CALLBACK (scanner_refresh_clicked), NULL);
  gtk_box_append (GTK_BOX (hbox), sd.refresh_button);

  /* Shown when there is nothing to scan with.  */
  sd.message = gtk_label_new (NULL);
  gtk_label_set_wrap (GTK_LABEL (sd.message), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (sd.message), 60);
  gtk_label_set_xalign (GTK_LABEL (sd.message), 0.0);
  gtk_widget_set_margin_top (sd.message, 12);
  gtk_widget_set_margin_bottom (sd.message, 12);
  gtk_widget_set_vexpand (sd.message, TRUE);
  gtk_widget_set_valign (sd.message, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (main_box), sd.message);

  /* Settings on the left, the area on the right.  */
  sd.content = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_set_vexpand (sd.content, TRUE);
  gtk_widget_set_visible (sd.content, FALSE);
  gtk_box_append (GTK_BOX (main_box), sd.content);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_size_request (vbox, 360, -1);
  gtk_box_append (GTK_BOX (sd.content), vbox);

  gtk_box_append (GTK_BOX (vbox), heading_new ("Settings"));

  sd.common_grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (sd.common_grid), 6);
  gtk_grid_set_column_spacing (GTK_GRID (sd.common_grid), 6);
  gtk_widget_set_margin_start (sd.common_grid, 12);
  gtk_box_append (GTK_BOX (vbox), sd.common_grid);

  sd.info_label = gtk_label_new (NULL);
  gtk_label_set_xalign (GTK_LABEL (sd.info_label), 0.0);
  gtk_label_set_wrap (GTK_LABEL (sd.info_label), TRUE);
  gtk_widget_add_css_class (sd.info_label, "dim-label");
  gtk_widget_set_margin_start (sd.info_label, 12);
  gtk_box_append (GTK_BOX (vbox), sd.info_label);

  sd.advanced_expander = gtk_expander_new_with_mnemonic ("A_dvanced options");
  gtk_widget_set_margin_top (sd.advanced_expander, 6);
  gtk_widget_set_vexpand (sd.advanced_expander, TRUE);
  gtk_box_append (GTK_BOX (vbox), sd.advanced_expander);

  frame = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_top (frame, 6);
  gtk_expander_set_child (GTK_EXPANDER (sd.advanced_expander), frame);

  sd.expert_check =
    gtk_check_button_new_with_mnemonic ("Show _expert options");
  gtk_widget_set_tooltip_text (sd.expert_check,
			       "Also show the options the scanner's driver "
			       "marks as advanced");
  g_signal_connect (sd.expert_check, "toggled",
		    G_CALLBACK (scanner_expert_toggled), NULL);
  gtk_box_append (GTK_BOX (frame), sd.expert_check);

  scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
				  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (scrolled),
						   TRUE);
  gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (scrolled),
						    TRUE);
  gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (scrolled),
					      320);
  gtk_widget_set_vexpand (scrolled, TRUE);
  gtk_box_append (GTK_BOX (frame), scrolled);

  sd.advanced_grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (sd.advanced_grid), 6);
  gtk_grid_set_column_spacing (GTK_GRID (sd.advanced_grid), 6);
  gtk_widget_set_margin_start (sd.advanced_grid, 12);
  gtk_widget_set_margin_end (sd.advanced_grid, 12);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled),
				 sd.advanced_grid);

  /* The scan area.  */
  sd.area_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_hexpand (sd.area_box, TRUE);
  gtk_box_append (GTK_BOX (sd.content), sd.area_box);

  gtk_box_append (GTK_BOX (sd.area_box), heading_new ("Scan area"));

  frame = gtk_frame_new (NULL);
  gtk_widget_set_vexpand (frame, TRUE);
  gtk_box_append (GTK_BOX (sd.area_box), frame);

  sd.preview_area = gtk_drawing_area_new ();
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (sd.preview_area),
				      300);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (sd.preview_area),
				       380);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (sd.preview_area),
				  preview_draw, NULL, NULL);
  gtk_widget_set_hexpand (sd.preview_area, TRUE);
  gtk_widget_set_vexpand (sd.preview_area, TRUE);
  gtk_widget_set_cursor_from_name (sd.preview_area, "crosshair");
  gtk_widget_set_tooltip_text (sd.preview_area,
			       "Drag out the area to scan; drag its edges or "
			       "the area itself to change it");
  gtk_frame_set_child (GTK_FRAME (frame), sd.preview_area);

  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), GDK_BUTTON_PRIMARY);
  g_signal_connect (drag, "drag-begin",
		    G_CALLBACK (preview_drag_begin), NULL);
  g_signal_connect (drag, "drag-update",
		    G_CALLBACK (preview_drag_update), NULL);
  g_signal_connect (drag, "drag-end",
		    G_CALLBACK (preview_drag_end), NULL);
  gtk_widget_add_controller (sd.preview_area, GTK_EVENT_CONTROLLER (drag));

  motion = gtk_event_controller_motion_new ();
  g_signal_connect (motion, "motion", G_CALLBACK (preview_motion), NULL);
  gtk_widget_add_controller (sd.preview_area, motion);

  grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
  gtk_widget_set_halign (grid, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (sd.area_box), grid);

  for (k = 0; k < 4; k++)
    {
      gint column = (k % 2) * 3;
      gint row    = k / 2;

      label = gtk_label_new_with_mnemonic (area_labels[k]);
      gtk_label_set_xalign (GTK_LABEL (label), 1.0);
      if (column > 0)
	gtk_widget_set_margin_start (label, 12);
      gtk_grid_attach (GTK_GRID (grid), label, column, row, 1, 1);

      sd.area_spins[k] = gtk_spin_button_new_with_range (0.0, 1.0, 1.0);
      gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (sd.area_spins[k]), TRUE);
      gtk_editable_set_width_chars (GTK_EDITABLE (sd.area_spins[k]), 6);
      gtk_label_set_mnemonic_widget (GTK_LABEL (label), sd.area_spins[k]);
      g_signal_connect (sd.area_spins[k], "value-changed",
			G_CALLBACK (scanner_area_spin_changed),
			GINT_TO_POINTER (k));
      gtk_grid_attach (GTK_GRID (grid), sd.area_spins[k], column + 1, row,
		       1, 1);

      sd.area_units[k] = gtk_label_new (NULL);
      gtk_label_set_xalign (GTK_LABEL (sd.area_units[k]), 0.0);
      gtk_grid_attach (GTK_GRID (grid), sd.area_units[k], column + 2, row,
		       1, 1);
    }

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_halign (hbox, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (sd.area_box), hbox);

  sd.preview_button = gtk_button_new_with_mnemonic ("_Preview");
  gtk_widget_set_tooltip_text (sd.preview_button,
			       "Scan the whole bed quickly at a low "
			       "resolution, to choose the area on it");
  g_signal_connect (sd.preview_button, "clicked",
		    G_CALLBACK (scanner_preview_clicked), NULL);
  gtk_box_append (GTK_BOX (hbox), sd.preview_button);

  sd.full_button = gtk_button_new_with_mnemonic ("Select _All");
  gtk_widget_set_tooltip_text (sd.full_button, "Scan the whole bed");
  g_signal_connect (sd.full_button, "clicked",
		    G_CALLBACK (scanner_full_area_clicked), NULL);
  gtk_box_append (GTK_BOX (hbox), sd.full_button);

  /* Results, and progress.  */
  sd.status_label = gtk_label_new (NULL);
  gtk_label_set_xalign (GTK_LABEL (sd.status_label), 0.0);
  gtk_label_set_wrap (GTK_LABEL (sd.status_label), TRUE);
  gtk_box_append (GTK_BOX (main_box), sd.status_label);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_append (GTK_BOX (main_box), hbox);

  sd.progress = gtk_progress_bar_new ();
  gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (sd.progress), TRUE);
  gtk_progress_bar_set_text (GTK_PROGRESS_BAR (sd.progress), " ");
  gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (sd.progress), 0.05);
  gtk_widget_set_hexpand (sd.progress, TRUE);
  gtk_widget_set_valign (sd.progress, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (hbox), sd.progress);

  sd.cancel_button = gtk_button_new_with_mnemonic ("Ca_ncel");
  gtk_widget_set_tooltip_text (sd.cancel_button, "Stop the scan");
  g_signal_connect (sd.cancel_button, "clicked",
		    G_CALLBACK (scanner_cancel_clicked), NULL);
  gtk_box_append (GTK_BOX (hbox), sd.cancel_button);

  sd.option_widgets = g_ptr_array_new_with_free_func (g_free);
  sd.group_headers  = g_ptr_array_new ();

  scanner_update_sensitivity ();
}

gint32
scanner_sane_dialog (void)
{
  SANE_Int    version;
  SANE_Status status;

  status = sane_init (&version, NULL);
  if (status != SANE_STATUS_GOOD)
    {
      g_message ("Scanner: SANE could not be initialized: %s\n",
		 sane_strstatus (status));
      return -1;
    }

  gtk_init ();

  memset (&sd, 0, sizeof (sd));
  sd.last_image = -1;

  scanner_dialog_new ();
  gtk_window_present (GTK_WINDOW (sd.dialog));

  scanner_refresh ();

  gimp_main_loop_run ();

  /* The widgets are gone now; only the device is left.  */
  if (sd.rebuild_idle)
    g_source_remove (sd.rebuild_idle);
  if (sd.pulse_timeout)
    g_source_remove (sd.pulse_timeout);

  if (sd.handle)
    {
      /* Remember the settings as the dialog leaves them.  */
      options_store_vals (sd.handle, sd.device);
      sane_close (sd.handle);
    }
  sane_exit ();

  g_free (sd.device);
  if (sd.preview)
    cairo_surface_destroy (sd.preview);
  if (sd.devices)
    g_ptr_array_unref (sd.devices);
  g_ptr_array_unref (sd.option_widgets);
  g_ptr_array_unref (sd.group_headers);

  return sd.last_image;
}
