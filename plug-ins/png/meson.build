if png_dep.found()
  executable('png', 'png.c',
    dependencies: [plugin_deps, png_dep],
    install: true,
    install_dir: plugin_install_dir,
    win_subsystem: 'windows',
  )
endif
