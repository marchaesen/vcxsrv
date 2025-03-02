# SPDX-License-Identifier: MIT
#
# Copyright (c) 2025, Oracle and/or its affiliates.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

#
# This is the package version number, not the shared library
# version.  This version number will be substituted into Xft.h
# Please bump the minor library number at each release as well.
#
project(
  'libXft',
  'c',
  version: '2.3.8',
  license: 'HPND-sell-variant',
  license_files: 'COPYING',
  meson_version: '>= 1.1.0',
)

# Replacement for XORG_DEFAULT_OPTIONS
cc = meson.get_compiler('c')
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
endif

# Make builds reproducible instead of embedding build paths due to use
# of __FILE__ in the assert() macro
fs = import('fs')
source_prefix_path = fs.relative_to(
    meson.project_source_root(),
    meson.project_build_root()
)
macro_prefix_map = '-fmacro-prefix-map=' + source_prefix_path + '/src/='
if cc.has_argument(macro_prefix_map)
  add_project_arguments(macro_prefix_map, language: 'c')
endif

prog_sed = find_program('sed')

# Set library version for Xft.h from package version set above
xft_version = meson.project_version()
xft_vers_components = xft_version.split('.')
xft_vers_conf = configuration_data()
xft_vers_conf.set('XFT_MAJOR', xft_vers_components[0],
                  description: 'Major version of Xft')
xft_vers_conf.set('XFT_MINOR', xft_vers_components[1],
                  description: 'Minor version of Xft')
xft_vers_conf.set('XFT_REVISION', xft_vers_components[2],
                  description: 'Micro revision of Xft')
# Temporary solution to allow building with either autoconf or meson
# during the transition period - can be replaced by configure_file()
# and use of '#mesondefine' when configure.ac is removed.
xft_h = custom_target(
  input: 'include/X11/Xft/Xft.h.in',
  output: 'Xft.h',
  command: [
    prog_sed,
    '-e', f's/#undef XFT_MAJOR/#define XFT_MAJOR @0@/'.format(xft_vers_conf.get('XFT_MAJOR')),
    '-e', f's/#undef XFT_MINOR/#define XFT_MINOR @0@/'.format(xft_vers_conf.get('XFT_MINOR')),
    '-e', f's/#undef XFT_REVISION/#define XFT_REVISION @0@/'.format(xft_vers_conf.get('XFT_REVISION')),
    '@INPUT@',
  ],
  capture: true,
  install: true,
  install_dir: get_option('prefix') / get_option('includedir') / 'X11/Xft'
)


# Check for Xrender
dep_xrender    = dependency('xrender', required: true, version: '>= 0.8.2')
dep_libx11     = dependency('x11', required: true)
dep_xproto     = dependency('xproto', required: true, version: '>= 7.0.22')

# Check freetype configuration
dep_freetype   = dependency('freetype2', required: true, version: '>= 2.1.6')

# Check fontconfig configuration
dep_fontconfig = dependency('fontconfig', required: true, version: '>= 2.5.92')

libXft_sources = [
  'src/xftcolor.c',
  'src/xftcore.c',
  'src/xftdbg.c',
  'src/xftdpy.c',
  'src/xftdraw.c',
  'src/xftextent.c',
  'src/xftfont.c',
  'src/xftfreetype.c',
  'src/xftglyphs.c',
  'src/xftinit.c',
  'src/xftlist.c',
  'src/xftname.c',
  'src/xftrender.c',
  'src/xftstr.c',
  'src/xftswap.c',
  'src/xftxlfd.c'
]

lib = library(
  'Xft',
  libXft_sources,
  include_directories: 'include/X11/Xft',
  dependencies: [dep_fontconfig, dep_freetype, dep_xrender, dep_libx11,
                 dep_xproto],
  sources: xft_h,
  version: xft_version,
  install: true,
)

install_headers(
  'include/X11/Xft/XftCompat.h',
  subdir: 'X11/Xft',
)

pkg = import('pkgconfig')
pkg.generate(
  name: 'Xft',
  description: 'X FreeType library',
  filebase: 'xft',
  libraries: '-L${libdir} -lXft',
  requires: ['xproto'],
  requires_private: ['xrender', 'fontconfig', 'freetype2'],  
  url: 'https://gitlab.freedesktop.org/xorg/lib/libxft/'
)

lib_man_suffix = get_option('lib_man_suffix')

custom_target(
  input: 'man/Xft.man',
  output: f'Xft.@lib_man_suffix@',
  command: [
    prog_sed,
    '-e', f's/__libmansuffix__/@lib_man_suffix@/g',
    '-e', 's/__vendorversion__/"libXft @0@" "X Version 11"/'.format(meson.project_version()),
    '@INPUT@',
  ],
  capture: true,
  install: true,
  install_dir: get_option('prefix') / get_option('mandir') / f'man@lib_man_suffix@',
)

shadow_man_pages = [
  'XftCharExists',
  'XftCharFontSpecRender',
  'XftCharIndex',
  'XftCharSpecRender',
  'XftColorAllocName',
  'XftColorAllocValue',
  'XftColorFree',
  'XftDefaultHasRender',
  'XftDefaultSet',
  'XftDefaultSubstitute',
  'XftDrawChange',
  'XftDrawCharFontSpec',
  'XftDrawCharSpec',
  'XftDrawColormap',
  'XftDrawCreate',
  'XftDrawCreateAlpha',
  'XftDrawCreateBitmap',
  'XftDrawDestroy',
  'XftDrawDisplay',
  'XftDrawDrawable',
  'XftDrawGlyphFontSpec',
  'XftDrawGlyphSpec',
  'XftDrawGlyphs',
  'XftDrawPicture',
  'XftDrawRect',
  'XftDrawSetClip',
  'XftDrawSetClipRectangles',
  'XftDrawSetSubwindowMode',
  'XftDrawSrcPicture',
  'XftDrawString16',
  'XftDrawString32',
  'XftDrawString8',
  'XftDrawStringUtf16',
  'XftDrawStringUtf8',
  'XftDrawVisual',
  'XftFontCheckGlyph',
  'XftFontClose',
  'XftFontCopy',
  'XftFontInfoCreate',
  'XftFontInfoDestroy',
  'XftFontInfoEqual',
  'XftFontInfoHash',
  'XftFontLoadGlyphs',
  'XftFontMatch',
  'XftFontOpen',
  'XftFontOpenInfo',
  'XftFontOpenName',
  'XftFontOpenPattern',
  'XftFontOpenXlfd',
  'XftFontUnloadGlyphs',
  'XftGetVersion',
  'XftGlyphExtents',
  'XftGlyphFontSpecRender',
  'XftGlyphRender',
  'XftGlyphSpecRender',
  'XftInit',
  'XftInitFtLibrary',
  'XftListFonts',
  'XftLockFace',
  'XftNameParse',
  'XftNameUnparse',
  'XftTextExtents16',
  'XftTextExtents32',
  'XftTextExtents8',
  'XftTextExtentsUtf16',
  'XftTextExtentsUtf8',
  'XftTextRender16',
  'XftTextRender16BE',
  'XftTextRender16LE',
  'XftTextRender32',
  'XftTextRender32BE',
  'XftTextRender32LE',
  'XftTextRender8',
  'XftTextRenderUtf16',
  'XftTextRenderUtf8',
  'XftUnlockFace',
  'XftXlfdParse'
]

foreach man: shadow_man_pages
  custom_target(
    output: f'@man@.@lib_man_suffix@',
    command: ['echo', f'.so man@lib_man_suffix@/Xft.@lib_man_suffix@'],
    capture: true,
    install: true,
    install_dir: get_option('prefix') / get_option('mandir') / f'man@lib_man_suffix@',
  )
endforeach
