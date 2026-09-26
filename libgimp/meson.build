# SPDX-License-Identifier: GPL-2.0-or-later

gimpfeatures_conf = configuration_data()
gimpfeatures_conf.set('GIMP_MAJOR_VERSION', gimp_major)
gimpfeatures_conf.set('GIMP_MINOR_VERSION', gimp_minor)
gimpfeatures_conf.set('GIMP_MICRO_VERSION', gimp_micro)
gimpfeatures_conf.set('GIMP_VERSION', meson.project_version())
gimpfeatures_h = configure_file(input: 'gimpfeatures.h.in',
                                output: 'gimpfeatures.h',
                                configuration: gimpfeatures_conf)

# The wire protocol, shared by the application and the plug-in library.
libgimpwire = static_library('gimpwire',
  files('gimpprotocol.c', 'gimpwire.c'),
  dependencies: [glib_dep],
  include_directories: gimp_inc,
)

# What every plug-in links against.
libgimp = static_library('gimp',
  files(
    'gimp.c',
    'gimpchannel.c',
    'gimpdisplay.c',
    'gimpdrawable.c',
    'gimpgradient.c',
    'gimpimage.c',
    'gimplayer.c',
    'gimppalette.c',
    'gimppixelrgn.c',
    'gimptile.c',
  ),
  gimpfeatures_h,
  link_with: libgimpwire,
  dependencies: [glib_dep, m_dep],
  include_directories: gimp_inc,
)

libgimp_dep = declare_dependency(
  link_with: [libgimp, libgimpwire],
  sources: gimpfeatures_h,
  include_directories: gimp_inc,
  dependencies: [glib_dep, m_dep],
)

# GTK 4 widgets shared by the application and the plug-in dialogs.
libgimpwidgets = static_library('gimpwidgets',
  files('gimppreview.c', 'gimpwidgets.c'),
  dependencies: [gtk_dep],
  include_directories: gimp_inc,
)

libgimpwidgets_dep = declare_dependency(
  link_with: libgimpwidgets,
  include_directories: gimp_inc,
  dependencies: [gtk_dep],
)

# Menus of images, layers and channels for plug-in dialogs.
libgimpui = static_library('gimpui',
  files('gimpmenu.c'),
  gimpfeatures_h,
  dependencies: [libgimp_dep, libgimpwidgets_dep],
  include_directories: gimp_inc,
)

libgimpui_dep = declare_dependency(
  link_with: libgimpui,
  dependencies: [libgimp_dep, libgimpwidgets_dep],
)
