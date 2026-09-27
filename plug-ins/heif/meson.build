if heif_dep.found()
  executable('heif', 'heif.c',
    dependencies: [plugin_deps, heif_dep],
    install: true,
    install_dir: plugin_install_dir,
    win_subsystem: 'windows',
  )
endif
