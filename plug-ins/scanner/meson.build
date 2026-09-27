# File > Acquire > Scanner.  GIMP 1.0 scanned through the external
# xscanimage; this plug-in does it itself.  On Windows it shows the
# system's scanner dialog through WIA 1.0 (COM: ole32, oleaut32, uuid),
# which is always there.  Elsewhere it has its own dialog on SANE, and is
# left out when SANE is not found.
if is_windows
  executable('scanner', 'scanner.c', 'scanner-wia.c',
    dependencies: plugin_deps + [
      cc.find_library('ole32'),
      cc.find_library('oleaut32'),
      cc.find_library('uuid'),
    ],
    install: true,
    install_dir: plugin_install_dir,
    win_subsystem: 'windows',
  )
elif sane_dep.found()
  executable('scanner', 'scanner.c', 'scanner-sane.c',
    dependencies: plugin_deps + [sane_dep],
    install: true,
    install_dir: plugin_install_dir,
    win_subsystem: 'windows',
  )
endif
