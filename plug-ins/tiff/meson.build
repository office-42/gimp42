if tiff_dep.found()
  executable('tiff', 'tiff.c',
    dependencies: plugin_deps + [tiff_dep],
    install: true,
    install_dir: plugin_install_dir,
    win_subsystem: 'windows',
  )
endif
