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

project(
  'libfontenc',
  'c',
  version: '1.1.8',
  license: 'MIT',
  license_files: 'COPYING',
  meson_version: '>= 1.1.0',
)

cc = meson.get_compiler('c')
conf = configuration_data()

# Replaces AC_USE_SYSTEM_EXTENSIONS
if host_machine.system() == 'sunos'
    system_extensions = '__EXTENSIONS__'
elif host_machine.system() == 'netbsd'
    system_extensions = '_OPENBSD_SOURCE'
else
    system_extensions = '_GNU_SOURCE'
endif
conf.set(system_extensions, 1,
         description: 'Enable non-standardized system API extensions')

# Replacement for XORG_DEFAULT_OPTIONS
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
endif

# Replaces XORG_FONTROOTDIR from xorg/font/util/fontutil.m4
fontrootdir = get_option('with-fontrootdir')
if fontrootdir == ''
    fontutil_dep = dependency('fontutil', required: false)
    if fontutil_dep.found() and fontutil_dep.type_name() == 'pkgconfig'
        fontrootdir = fontutil_dep.get_variable(pkgconfig: 'fontrootdir')
    endif
    if fontrootdir == ''
        fontrootdir = get_option('prefix') / get_option('datadir') / '/fonts/X11'
    endif
endif

# Replaces XORG_FONTSUBDIR([ENCODINGSDIR],[encodingsdir],[encodings])
encodingsdir = get_option('with-encodingsdir')
if encodingsdir == ''
    encodingsdir = fontrootdir / 'encodings'
endif

dep_zlib = dependency('zlib', required: true)
dep_xproto = dependency('xproto', required: true, version: '>=1.0.0')

libfontenc_sources = [
  'src/encparse.c',
  'src/fontenc.c'
]

# Replaces AC_REPLACE_FUNCS([reallocarray])
has_reallocarray = cc.has_function(
  'reallocarray',
  args: '-D' + system_extensions,
  prefix: '#include <stdlib.h>'
)
if has_reallocarray
  conf.set('HAVE_REALLOCARRAY', 1)
else
  libfontenc_sources += 'src/reallocarray.c'
endif

configure_file(configuration: conf, output: 'config.h')
add_project_arguments('-DHAVE_CONFIG_H', language: 'c')

lib = library(
  'fontenc',
  libfontenc_sources,
  c_args: [
    '-DFONT_ENCODINGS_DIRECTORY="' + encodingsdir + '/encodings.dir"',
  ],
  include_directories: 'include',
  dependencies: [dep_xproto, dep_zlib],
  version: '1.0.0',
  install: true,
)

install_headers(
  'include/X11/fonts/fontenc.h',
  subdir: 'X11/fonts',
)

pkg = import('pkgconfig')
pkg.generate(
  lib,
  description: 'The fontenc Library',
  filebase: 'fontenc',
  url: 'https://gitlab.freedesktop.org/xorg/lib/libfontenc/'
)
