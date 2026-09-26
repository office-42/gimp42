# gz used to pipe files through an external gzip; it now uses zlib, and
# is left out when zlib is not available.
if zlib_dep.found()
  executable('gz', 'gz.c',
    dependencies: [libgimp_dep, zlib_dep],
    install: true,
    install_dir: plugin_install_dir,
    win_subsystem: 'windows',
  )
endif
