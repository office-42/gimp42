# Needs the Berkeley-derived mpeg_lib (mpeg.h, libmpeg), which hardly
# any system packages any more; the plug-in is skipped without it.
mpeg_lib_dep = cc.find_library('mpeg', has_headers: ['mpeg.h'], required: false)

if mpeg_lib_dep.found()
  executable('mpeg', 'mpeg.c',
    dependencies: [libgimp_dep, mpeg_lib_dep],
    install: true,
    install_dir: plugin_install_dir,
    win_subsystem: 'windows',
  )
endif
