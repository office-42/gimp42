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
#include "config.h"

#include <string.h>

#include "gimppreview.h"

struct _GimpPreview
{
  GtkWidget        parent_instance;

  GimpPreviewType  type;
  gint             width;
  gint             height;
  gboolean         expand;

  guchar          *buffer;      /*  width * bpp bytes per row          */
  cairo_surface_t *surface;     /*  the buffer converted for cairo     */
  gboolean         dirty;       /*  surface is behind the buffer       */
};

G_DEFINE_FINAL_TYPE (GimpPreview, gimp_preview, GTK_TYPE_WIDGET)

static void
gimp_preview_update_surface (GimpPreview *preview)
{
  guchar *dest;
  gint    stride;
  gint    bpp;
  gint    x, y;

  if (preview->width <= 0 || preview->height <= 0 || !preview->buffer)
    return;

  if (!preview->surface)
    preview->surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
						   preview->width,
						   preview->height);

  cairo_surface_flush (preview->surface);
  dest   = cairo_image_surface_get_data (preview->surface);
  stride = cairo_image_surface_get_stride (preview->surface);
  bpp    = gimp_preview_get_bpp (preview);

  for (y = 0; y < preview->height; y++)
    {
      const guchar *s = preview->buffer + y * preview->width * bpp;
      guint32      *d = (guint32 *) (dest + y * stride);

      if (bpp == 3)
	for (x = 0; x < preview->width; x++, s += 3)
	  d[x] = (0xffu << 24) | (s[0] << 16) | (s[1] << 8) | s[2];
      else
	for (x = 0; x < preview->width; x++, s++)
	  d[x] = (0xffu << 24) | (s[0] << 16) | (s[0] << 8) | s[0];
    }

  cairo_surface_mark_dirty (preview->surface);
  preview->dirty = FALSE;
}

static void
gimp_preview_snapshot (GtkWidget   *widget,
		       GtkSnapshot *snapshot)
{
  GimpPreview *preview = GIMP_PREVIEW (widget);
  cairo_t     *cr;
  gint         w, h;
  gint         ox, oy;

  if (preview->width <= 0 || preview->height <= 0)
    return;

  if (preview->dirty || !preview->surface)
    gimp_preview_update_surface (preview);

  if (!preview->surface)
    return;

  w = gtk_widget_get_width (widget);
  h = gtk_widget_get_height (widget);
  ox = MAX (0, (w - preview->width) / 2);
  oy = MAX (0, (h - preview->height) / 2);

  cr = gtk_snapshot_append_cairo (snapshot,
				  &GRAPHENE_RECT_INIT (0, 0, w, h));
  cairo_set_source_surface (cr, preview->surface, ox, oy);
  cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_NEAREST);
  cairo_rectangle (cr, ox, oy, preview->width, preview->height);
  cairo_fill (cr);
  cairo_destroy (cr);
}

static void
gimp_preview_measure (GtkWidget      *widget,
		      GtkOrientation  orientation,
		      int             for_size,
		      int            *minimum,
		      int            *natural,
		      int            *minimum_baseline,
		      int            *natural_baseline)
{
  GimpPreview *preview = GIMP_PREVIEW (widget);
  gint size;

  size = (orientation == GTK_ORIENTATION_HORIZONTAL) ?
    preview->width : preview->height;

  *minimum = preview->expand ? 0 : MAX (size, 0);
  *natural = MAX (size, 0);
}

static void
gimp_preview_finalize (GObject *object)
{
  GimpPreview *preview = GIMP_PREVIEW (object);

  g_free (preview->buffer);
  g_clear_pointer (&preview->surface, cairo_surface_destroy);

  G_OBJECT_CLASS (gimp_preview_parent_class)->finalize (object);
}

static void
gimp_preview_class_init (GimpPreviewClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gimp_preview_finalize;
  widget_class->snapshot = gimp_preview_snapshot;
  widget_class->measure  = gimp_preview_measure;
}

static void
gimp_preview_init (GimpPreview *preview)
{
  preview->type = GIMP_PREVIEW_COLOR;
}

GtkWidget *
gimp_preview_new (GimpPreviewType type)
{
  GimpPreview *preview = g_object_new (GIMP_TYPE_PREVIEW, NULL);

  preview->type = type;

  return GTK_WIDGET (preview);
}

GimpPreviewType
gimp_preview_get_preview_type (GimpPreview *preview)
{
  g_return_val_if_fail (GIMP_IS_PREVIEW (preview), GIMP_PREVIEW_COLOR);

  return preview->type;
}

void
gimp_preview_size (GimpPreview *preview,
		   gint         width,
		   gint         height)
{
  g_return_if_fail (GIMP_IS_PREVIEW (preview));

  if (width == preview->width && height == preview->height && preview->buffer)
    return;

  preview->width  = MAX (width, 0);
  preview->height = MAX (height, 0);

  g_free (preview->buffer);
  preview->buffer = g_malloc0 ((gsize) MAX (preview->width, 1) *
			       MAX (preview->height, 1) *
			       gimp_preview_get_bpp (preview));
  g_clear_pointer (&preview->surface, cairo_surface_destroy);
  preview->dirty = TRUE;

  gtk_widget_queue_resize (GTK_WIDGET (preview));
}

gint
gimp_preview_get_width (GimpPreview *preview)
{
  g_return_val_if_fail (GIMP_IS_PREVIEW (preview), 0);

  return preview->width;
}

gint
gimp_preview_get_height (GimpPreview *preview)
{
  g_return_val_if_fail (GIMP_IS_PREVIEW (preview), 0);

  return preview->height;
}

void
gimp_preview_set_expand (GimpPreview *preview,
			 gboolean     expand)
{
  g_return_if_fail (GIMP_IS_PREVIEW (preview));

  preview->expand = expand;
  gtk_widget_queue_resize (GTK_WIDGET (preview));
}

void
gimp_preview_draw_row (GimpPreview  *preview,
		       const guchar *data,
		       gint          x,
		       gint          y,
		       gint          w)
{
  gint bpp;

  g_return_if_fail (GIMP_IS_PREVIEW (preview));

  if (!preview->buffer || y < 0 || y >= preview->height || x >= preview->width)
    return;

  bpp = gimp_preview_get_bpp (preview);

  if (x < 0)
    {
      data -= x * bpp;
      w += x;
      x = 0;
    }
  if (x + w > preview->width)
    w = preview->width - x;
  if (w <= 0)
    return;

  memcpy (preview->buffer + (y * preview->width + x) * bpp, data, w * bpp);

  gimp_preview_changed (preview);
}

guchar *
gimp_preview_get_buffer (GimpPreview *preview)
{
  g_return_val_if_fail (GIMP_IS_PREVIEW (preview), NULL);

  return preview->buffer;
}

gint
gimp_preview_get_rowstride (GimpPreview *preview)
{
  g_return_val_if_fail (GIMP_IS_PREVIEW (preview), 0);

  return preview->width * gimp_preview_get_bpp (preview);
}

gint
gimp_preview_get_bpp (GimpPreview *preview)
{
  return (preview->type == GIMP_PREVIEW_GRAYSCALE) ? 1 : 3;
}

void
gimp_preview_changed (GimpPreview *preview)
{
  g_return_if_fail (GIMP_IS_PREVIEW (preview));

  if (!preview->dirty)
    {
      preview->dirty = TRUE;
      gtk_widget_queue_draw (GTK_WIDGET (preview));
    }
}

void
gimp_preview_fill (GimpPreview *preview,
		   guchar       r,
		   guchar       g,
		   guchar       b)
{
  gint i, n;

  g_return_if_fail (GIMP_IS_PREVIEW (preview));

  if (!preview->buffer)
    return;

  n = preview->width * preview->height;

  if (gimp_preview_get_bpp (preview) == 1)
    memset (preview->buffer, r, n);
  else
    for (i = 0; i < n; i++)
      {
	preview->buffer[i * 3 + 0] = r;
	preview->buffer[i * 3 + 1] = g;
	preview->buffer[i * 3 + 2] = b;
      }

  gimp_preview_changed (preview);
}
