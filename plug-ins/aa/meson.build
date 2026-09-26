# The ASCII art saver needs AAlib.  AAlib has no pkg-config file on most
# systems and is not packaged for Windows (MSYS2) at all, so look for it
# both ways and leave the plug-in out when it is missing.
aalib_dep = dependency('aalib', required: false)
if not aalib_dep.found()
  aalib_dep = cc.find_library('aa', has_headers: ['aalib.h'], required: false)
endif

if aalib_dep.found()
  executable('aa', 'aa.c',
    dependencies: plugin_deps + [aalib_dep],
    install: true,
    install_dir: plugin_install_dir,
    win_subsystem: 'windows',
  )
endif
