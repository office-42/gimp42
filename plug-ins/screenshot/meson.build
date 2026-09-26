# The X11 xwd frontend is gone.  On Windows the grab is done with GDI
# (user32/gdi32, linked by default); elsewhere it goes through the
# desktop portal (org.freedesktop.portal.Screenshot) over D-Bus with GIO,
# which works on Wayland and X11 desktops.  Where no portal is running
# (macOS, bare X servers) the plug-in reports that at run time.
executable('screenshot', 'screenshot.c',
  dependencies: plugin_deps + [gio_dep],
  install: true,
  install_dir: plugin_install_dir,
  win_subsystem: 'windows',
)
