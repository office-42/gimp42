# Compresses and decompresses in-process with libbz2 when it is available;
# otherwise pipes through the bzip2 program with GSubprocess.
bz2_lib_dep = dependency('bzip2', required: false)
if not bz2_lib_dep.found()
  bz2_lib_dep = cc.find_library('bz2', has_headers: ['bzlib.h'], required: false)
endif

executable('bz2', 'bz2.c',
  dependencies: [plugin_deps, bz2_lib_dep],
  c_args: bz2_lib_dep.found() ? ['-DHAVE_LIBBZ2'] : [],
  install: true,
  install_dir: plugin_install_dir,
  win_subsystem: 'windows',
)
