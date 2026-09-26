# The download goes through GIO (g_file_copy), with curl or wget run as a
# subprocess when GIO has no backend for the URI scheme (plain Windows
# and Linux without gvfs have none for http/ftp).
executable('url', 'url.c',
  dependencies: [libgimp_dep, gio_dep],
  install: true,
  install_dir: plugin_install_dir,
  win_subsystem: 'windows',
)
