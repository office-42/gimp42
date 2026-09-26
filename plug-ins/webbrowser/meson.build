executable('webbrowser', 'webbrowser.c',
  dependencies: plugin_deps,
  install: true,
  install_dir: plugin_install_dir,
  win_subsystem: 'windows',
)

# The bookmarks, as a Script-Fu script next to the others.
install_data('web-browser.scm',
  install_dir: get_option('prefix') / datadir / 'scripts')
