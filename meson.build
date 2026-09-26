# gimp42 - a modernization of the GIMP 1.0
# SPDX-License-Identifier: GPL-2.0-or-later

project('gimp42', 'c',
  version: '1.0.4',
  license: 'GPL-2.0-or-later',
  meson_version: '>= 1.0',
  default_options: [
    'c_std=gnu11,c11',
    'warning_level=1',
    'buildtype=debugoptimized',
  ],
)

cc = meson.get_compiler('c')

version_parts = meson.project_version().split('.')
gimp_major = version_parts[0]
gimp_minor = version_parts[1]
gimp_micro = version_parts[2]

# The 1.0 sources predate ANSI prototypes in places and lean on implicit
# conversions GCC 14 now treats as errors.  Those are fixed as files are
# ported; meanwhile they stay warnings.
legacy_args = cc.get_supported_arguments([
  '-Wno-unused-parameter',
  '-Wno-sign-compare',
  '-Wno-missing-field-initializers',
  '-Wno-deprecated-declarations',
  '-fno-strict-aliasing',
])
add_project_arguments(legacy_args, language: 'c')
add_project_arguments('-DHAVE_CONFIG_H', language: 'c')

glib_dep    = dependency('glib-2.0',    version: '>= 2.76')
gobject_dep = dependency('gobject-2.0', version: '>= 2.76')
gio_dep     = dependency('gio-2.0',     version: '>= 2.76')
gtk_dep     = dependency('gtk4',        version: '>= 4.10')
cairo_dep   = dependency('cairo',       version: '>= 1.16')
pango_dep   = dependency('pango',       version: '>= 1.50')
pixbuf_dep  = dependency('gdk-pixbuf-2.0', version: '>= 2.42')
m_dep       = cc.find_library('m', required: false)

# Formats the file plug-ins read and write through outside libraries.  A
# build without one simply has no plug-in for that format.
jpeg_dep = dependency('libjpeg',  required: get_option('jpeg'))
png_dep  = dependency('libpng',   required: get_option('png'))
tiff_dep = dependency('libtiff-4', required: get_option('tiff'))
zlib_dep = dependency('zlib',     required: false)

is_windows = host_machine.system() == 'windows'

prefix     = get_option('prefix')
datadir    = get_option('datadir') / 'gimp42' / (gimp_major + '.' + gimp_minor)
plugindir  = get_option('libdir') / 'gimp42' / (gimp_major + '.' + gimp_minor)
sysconfdir = get_option('sysconfdir') / 'gimp42' / (gimp_major + '.' + gimp_minor)

conf = configuration_data()
conf.set_quoted('GIMP_VERSION', meson.project_version())
conf.set('GIMP_MAJOR_VERSION', '(' + gimp_major + ')')
conf.set('GIMP_MINOR_VERSION', '(' + gimp_minor + ')')
conf.set('GIMP_MICRO_VERSION', '(' + gimp_micro + ')')
# Installed locations, relative to the prefix, so that a Windows or macOS
# bundle can be moved anywhere: at run time they are resolved against the
# folder the executable was started from (see gimp42_installed_path).
conf.set_quoted('GIMP42_DATADIR_REL', datadir)
conf.set_quoted('GIMP42_PLUGINDIR_REL', plugindir)
conf.set_quoted('GIMP42_SYSCONFDIR_REL', sysconfdir)
conf.set_quoted('GIMP42_BINDIR_REL', get_option('bindir'))
conf.set_quoted('GIMP42_PREFIX', prefix)
# The per-user folder, below the home directory on Unix and below
# %APPDATA% on Windows.
conf.set_quoted('GIMPDIR', is_windows ? 'gimp42' : '.gimp42')
conf.set_quoted('GIMP_BUILD_SRCDIR', meson.project_source_root())
conf.set_quoted('GIMP_BUILD_DIR', meson.project_build_root())
conf.set('STDC_HEADERS', 1)
conf.set('HAVE_STRING_H', 1)
conf.set('HAVE_VPRINTF', 1)
conf.set('HAVE_PUTENV', 1)
conf.set('RETSIGTYPE', 'void')
conf.set('RAND_FUNC', 'rand')
conf.set('SRAND_FUNC', 'srand')
foreach h : ['unistd.h', 'sys/time.h', 'sys/wait.h', 'sys/select.h',
             'dirent.h', 'alloca.h', 'wchar.h', 'wctype.h', 'sys/param.h']
  if cc.has_header(h)
    conf.set('HAVE_' + h.underscorify().to_upper(), 1)
  endif
endforeach
if cc.has_function('strsep')
  conf.set('HAVE_STRSEP', 1)
endif
configure_file(output: 'config.h', configuration: conf)

gimp_inc = include_directories('.')

subdir('libgimp')
subdir('app')
subdir('plug-ins')
subdir('data')

summary({
    'prefix'  : prefix,
    'GTK'     : gtk_dep.version(),
    'GLib'    : glib_dep.version(),
    'JPEG'    : jpeg_dep.found(),
    'PNG'     : png_dep.found(),
    'TIFF'    : tiff_dep.found(),
  }, section: 'gimp42 ' + meson.project_version())
