# Script-Fu: the SIOD Scheme interpreter as a GIMP extension, with its
# console, the script dialogs and the network server.  The console's
# "Browse..." button uses the procedure browser from plug-ins/dbbrowser,
# which is compiled in, as the old Makefile linked dbbrowser_utils.o.

script_fu_sources = [
  'interp_md5.c',
  'interp_regex.c',
  'interp_slib.c',
  'interp_sliba.c',
  'interp_trace.c',
  'script-fu.c',
  'script-fu-console.c',
  'script-fu-scripts.c',
  'script-fu-server.c',
  'regex.c',
  '..' / 'dbbrowser' / 'dbbrowser_utils.c',
]

executable('script-fu', script_fu_sources,
  dependencies: plugin_deps + [gio_dep],
  c_args: ['-DREGEX_MALLOC'],
  install: true,
  install_dir: plugin_install_dir,
  win_subsystem: 'windows',
)

subdir('scripts')
