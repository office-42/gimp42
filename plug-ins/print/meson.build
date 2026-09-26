# The print plug-in writes PostScript, PCL or ESC/P2 itself.  It sends
# the job to a file, to a spooler command (lp/lpr, where the system has
# one) or, on every platform, to the system's printers through
# GtkPrintOperation ("System Printer" in the printer menu).

print_args = []
if not is_windows
  lpstat = find_program('lpstat', required: false)
  lpc    = find_program('lpc', required: false)
  if lpstat.found()
    print_args += ['-DLPSTAT_COMMAND="lpstat"', '-DLP_COMMAND="lp"']
  endif
  if lpc.found()
    print_args += ['-DLPC_COMMAND="lpc"', '-DLPR_COMMAND="lpr"']
  endif
endif

executable('print',
  'print.c', 'print-escp2.c', 'print-pcl.c', 'print-ps.c', 'print-util.c',
  'print-gtk.c',
  c_args: print_args,
  dependencies: [plugin_deps, gio_dep],
  install: true,
  install_dir: plugin_install_dir,
  win_subsystem: 'windows',
)
