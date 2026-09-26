# libgck - the General Convenience Kit, used by MapObject.  Headers are
# included as <gck/gck.h>, so this directory is on the include path.
#
# gck/gcklistbox.c is left out: it wraps GTK 1's GtkList, which GTK 4
# does not have, and no plug-in uses it.

libgck = static_library('gck',
  'gck/gckcolor.c',
  'gck/gckmath.c',
  'gck/gckui.c',
  'gck/gckvector.c',
  include_directories: include_directories('.', 'gck'),
  dependencies: plugin_deps,
)

gck_dep = declare_dependency(
  link_with: libgck,
  include_directories: include_directories('.'),
  dependencies: plugin_deps,
)
