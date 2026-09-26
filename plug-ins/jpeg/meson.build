# jpeg-load.c, jpeg-save.c and the headers are unused copies of jpeg.c;
# the plug-in is built from jpeg.c alone, as in Makefile.am.
if jpeg_dep.found()
  executable('jpeg', 'jpeg.c',
    dependencies: plugin_deps + [jpeg_dep],
    install: true,
    install_dir: plugin_install_dir,
    win_subsystem: 'windows',
  )
endif
