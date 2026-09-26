# gpc: GTK Plug-in Convenience library, used by blur and randomize.

libgpc = static_library('gpc', 'gpc.c',
  dependencies: plugin_deps,
)

gpc_dep = declare_dependency(
  link_with: libgpc,
  include_directories: include_directories('.'),
  dependencies: plugin_deps,
)
