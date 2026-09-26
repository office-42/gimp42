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

/* GimpPreview takes the place of GTK 1's GtkPreview: a widget holding an
 * RGB or grayscale buffer that is filled a row at a time and painted with
 * cairo.  Rows drawn with gimp_preview_draw_row () show up on the next
 * frame; there is no need to ask for a redraw.
 */

#ifndef __GIMP_PREVIEW_H__
#define __GIMP_PREVIEW_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef enum
{
  GIMP_PREVIEW_COLOR,
  GIMP_PREVIEW_GRAYSCALE
} GimpPreviewType;

#define GIMP_TYPE_PREVIEW (gimp_preview_get_type ())
G_DECLARE_FINAL_TYPE (GimpPreview, gimp_preview, GIMP, PREVIEW, GtkWidget)

GtkWidget *     gimp_preview_new          (GimpPreviewType  type);
GimpPreviewType gimp_preview_get_preview_type (GimpPreview *preview);

/*  Sets the size of the buffer, and the size the widget asks for.  */
void            gimp_preview_size         (GimpPreview     *preview,
					   gint             width,
					   gint             height);
gint            gimp_preview_get_width    (GimpPreview     *preview);
gint            gimp_preview_get_height   (GimpPreview     *preview);

/*  When expand is set the buffer is centred in whatever space the
 *  widget is given, rather than the widget asking for exactly its size.
 */
void            gimp_preview_set_expand   (GimpPreview     *preview,
					   gboolean         expand);

/*  Copies w pixels of RGB (3 bytes each) or gray (1 byte) data into
 *  row y, starting at column x.
 */
void            gimp_preview_draw_row     (GimpPreview     *preview,
					   const guchar    *data,
					   gint             x,
					   gint             y,
					   gint             w);

/*  Direct access to the buffer: rows of width * bpp bytes.  Call
 *  gimp_preview_changed () after writing to it.
 */
guchar *        gimp_preview_get_buffer   (GimpPreview     *preview);
gint            gimp_preview_get_rowstride(GimpPreview     *preview);
gint            gimp_preview_get_bpp      (GimpPreview     *preview);
void            gimp_preview_changed      (GimpPreview     *preview);

/*  Fills the whole buffer with one value per channel.  */
void            gimp_preview_fill         (GimpPreview     *preview,
					   guchar           r,
					   guchar           g,
					   guchar           b);

G_END_DECLS

#endif /* __GIMP_PREVIEW_H__ */
