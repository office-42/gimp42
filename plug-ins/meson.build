# SPDX-License-Identifier: GPL-2.0-or-later
#
# Every plug-in is its own program, installed in the plug-in folder the
# application searches at startup.  Each directory below has its own
# meson.build; a directory without one has not been ported yet.

fs = import('fs')

plugin_install_dir = get_option('prefix') / plugindir / 'plug-ins'

# Plug-in dialogs link these; plug-ins without a dialog only need
# libgimp_dep.
plugin_deps = [libgimpui_dep, m_dep]

# Libraries some plug-ins share; they come first so the plug-ins after
# them can use them.
plugin_helper_dirs = ['gpc', 'megawidget', 'libgck']

plugin_dirs = [
  'AlienMap', 'CEL', 'CML_explorer', 'MapObject', 'aa', 'align_layers',
  'animationplay', 'animoptimize', 'apply_lens', 'autocrop',
  'autostretch_hsv', 'blinds', 'blur', 'bmp', 'bumpmap', 'bz2',
  'c_astretch', 'checkerboard', 'colorify', 'compose', 'convmatrix',
  'cubism', 'dbbrowser', 'decompose', 'deinterlace', 'depthmerge',
  'despeckle', 'destripe', 'diffraction', 'displace', 'edge', 'emboss',
  'engrave', 'exchange', 'faxg3', 'film', 'fits', 'flame', 'flarefx',
  'fractaltrace', 'gauss_iir', 'gauss_rle', 'gbr', 'gee', 'gfig', 'gfli',
  'gicon', 'gif', 'glasstile', 'gqbist', 'gradmap', 'grid', 'gtm', 'gz',
  'header', 'heif', 'hot', 'hrz', 'ico', 'ifscompose', 'illusion',
  'iwarp', 'jpeg', 'jxl', 'laplace', 'mail', 'max_rgb', 'maze', 'mblur',
  'mosaic', 'mpeg', 'nlfilt', 'noisify', 'normalize', 'nova', 'oilify',
  'pagecurl', 'palette', 'papertile', 'pat', 'pcx', 'pix', 'pixbuf',
  'pixelize', 'plasma', 'png', 'pnm', 'polar', 'print', 'ps', 'psd',
  'qoi', 'randomize', 'ripple', 'rotate', 'rotators', 'scanner',
  'scatter_hsv', 'screenshot', 'script-fu', 'semiflatten', 'sgi',
  'sharpen', 'shift', 'sinus', 'smooth_palette', 'snoise', 'snp',
  'sobel', 'sparkle', 'spread', 'struc', 'sunras', 'tga',
  'threshold_alpha', 'tiff', 'tile', 'tileit', 'tiler', 'url', 'video',
  'vinvert', 'vpropagate', 'waves', 'webbrowser', 'webp', 'whirlpinch',
  'xd', 'xpm', 'xwd', 'zealouscrop',
]

foreach d : plugin_helper_dirs + plugin_dirs
  if fs.is_file(d / 'meson.build')
    subdir(d)
  endif
endforeach
