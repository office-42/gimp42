if webp_dep.found() and webpdemux_dep.found() and webpmux_dep.found()
  executable('webp', 'webp.c',
    dependencies: [plugin_deps, webp_dep, webpdemux_dep, webpmux_dep],
    install: true,
    install_dir: plugin_install_dir,
    win_subsystem: 'windows',
  )
endif
