# SPDX-License-Identifier: MIT
# Copyright © 2023 Intel Corporation

project(
  'libXau',
  'c',
  version : '1.0.11',
  license : 'MIT',
  meson_version : '>= 0.60.0',
)

add_project_arguments(
  '-D_GNU_SOURCE',
  '-D__EXTENSIONS__',
  language : 'c'
)

cc = meson.get_compiler('c')

lib_args = []

foreach f : ['explicit_bzero', 'explicit_memset', 'pathconf']
  if cc.has_function(f)
    lib_args += '-DHAVE_@0@'.format(f.to_upper())
  endif
endforeach

if cc.has_header('unistd.h')
  lib_args += '-DHAVE_UNISTD_H'
endif

dep_xproto = dependency('xproto')

if get_option('xthreads')
  lib_args += '-DXTHREADS'
  # This define is not in libXau specific code, but is part of the xproto header
  # This may be only required by HP-UX.
  if cc.has_function('gethostbyname_r') or \
     cc.has_function('gethostbyname_r', dependencies : cc.find_library('nls'))
    lib_args += '-DXUSE_MTSAFE_API=1'
  endif
  if host_machine.system() == 'sunos'
    lib_args += ['-D_REENETRANT', '-D_POSIX_PTHREAD_SEMANTICS']
  endif
endif

lib = library(
  'Xau',
  [
    'AuDispose.c',
    'AuFileName.c',
    'AuGetAddr.c',
    'AuGetBest.c',
    'AuLock.c',
    'AuRead.c',
    'AuUnlock.c',
    'AuWrite.c',
  ],
  c_args : lib_args,
  include_directories : 'include',
  dependencies : dep_xproto,
  version : '6.0.0',
  install : true,
)

test(
  'autest',
  executable(
    'autest',
    'Autest.c',
    link_with : lib,
    include_directories : 'include',
    dependencies : dep_xproto,
  )
)

libxau = declare_dependency(
  link_with : lib,
  include_directories : 'include',
)

meson.override_dependency('xau', libxau)

install_headers(
  'include/X11/Xauth.h',
  subdir : 'X11',
)

pkg = import('pkgconfig')
pkg.generate(
  lib,
  description : 'X authorization file management library',
  filebase : 'xau',
  requires : 'xproto',
)

subdir('man')
