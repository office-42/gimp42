# Small dialog-building helpers shared by several plug-ins (deinterlace,
# emboss, hot, nlfilt, sinus, waves).
libmegawidget = static_library('megawidget', 'megawidget.c',
  dependencies: plugin_deps,
)

megawidget_dep = declare_dependency(
  link_with: libmegawidget,
  dependencies: plugin_deps,
)
