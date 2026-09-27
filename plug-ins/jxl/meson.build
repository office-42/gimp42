if jxl_dep.found() and jxl_threads_dep.found()
  executable('jxl', 'jxl.c',
    dependencies: [plugin_deps, jxl_dep, jxl_threads_dep],
    install: true,
    install_dir: plugin_install_dir,
    win_subsystem: 'windows',
  )
endif
