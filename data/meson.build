# SPDX-License-Identifier: GPL-2.0-or-later
#
# Brushes, gradients, palettes and patterns, the system gimprc, the
# splash and logo images and the tips, all in the data folder the program
# finds next to itself (see gimp_data_directory in app/gimprc.c).

data_install_dir = get_option('prefix') / datadir

foreach d : ['brushes', 'gradients', 'palettes', 'patterns']
  install_subdir(d,
    install_dir: data_install_dir,
    exclude_files: ['Makefile.am', '.cvsignore'],
  )
endforeach

install_data('images/gimp_logo.ppm', 'images/gimp_splash.ppm',
  install_dir: data_install_dir)

gimprc_conf = configuration_data()
gimprc_conf.set('gimpdir', is_windows ? '%APPDATA%\gimp42' : '~/.gimp42')
gimprc_conf.set('gimpdatadir', data_install_dir)
gimprc_conf.set('prefix', get_option('prefix'))

configure_file(input: '../gimprc.in', output: 'gimprc',
  configuration: gimprc_conf,
  install: true, install_dir: data_install_dir)
configure_file(input: '../gimprc_user.in', output: 'gimprc_user',
  configuration: gimprc_conf,
  install: true, install_dir: data_install_dir)

install_data('../gimp_tips.txt', '../gtkrc',
  install_dir: data_install_dir)

# The application icon, in the hicolor theme GTK looks in for window icons.
install_subdir('icons/hicolor',
  install_dir: get_option('prefix') / get_option('datadir') / 'icons')
