# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

subdir('gen')

inc_glapi = include_directories('.')

static_glapi_files = []
static_glapi_args = []

if with_dri and ['apple', 'windows'].contains(with_dri_platform)
  static_glapi_files += [glapi_gentable_c, glapitable_h]
endif

bridge_glapi_files = static_glapi_files
bridge_glapi_args = static_glapi_args

bridge_glapi_files += files(
  '../entry.c',
  '../entry.h',
  '../entry_x86-64_tls.h',
  '../entry_x86_tls.h',
  '../entry_ppc64le_tls.h',
  '../mapi_tmp.h',
)
bridge_glapi_files += glapi_mapi_tmp_h
bridge_glapi_args += [
  '-DMAPI_MODE_BRIDGE',
  '-DMAPI_ABI_HEADER="@0@"'.format(glapi_mapi_tmp_h.full_path()),
  gcc_lto_quirk,
]
if with_platform_windows
  bridge_glapi_args += ['-D_GDI32_']
endif

static_glapi_args += '-DMAPI_MODE_UTIL'
if with_platform_windows
  static_glapi_args += ['-D_GDI32_', '-DKHRONOS_DLL_EXPORTS', '-D_GLAPI_DLL_EXPORTS']
endif
static_glapi_files += files(
  '../u_current.c',
  '../u_current.h',
  'glapi_dispatch.c',
  'glapi_entrypoint.c',
  'glapi_getproc.c',
  'glapi_nop.c',
  'glapi.c',
  'glapi.h',
  'glapi_priv.h',
)
static_glapi_files += [
  glapitable_h, glapi_mapi_tmp_h, glprocs_h, glapitemp_h,
]
if with_asm_arch == 'x86'
  static_glapi_files += glapi_x86_s
elif with_asm_arch == 'x86_64'
  static_glapi_files += glapi_x86_64_s
elif with_asm_arch == 'sparc'
  static_glapi_files += glapi_sparc_s
endif


libglapi_bridge = static_library(
  'glapi_bridge',
  bridge_glapi_files,
  include_directories : [inc_mesa, inc_include, inc_src, inc_mapi, inc_gallium],
  c_args : [c_msvc_compat_args, bridge_glapi_args],
  dependencies : [dep_thread, idep_mesautil],
  build_by_default : false,
)

if with_shared_glapi
  libglapi_static = libglapi_bridge
else
  libglapi_static = static_library(
    'glapi_static',
    static_glapi_files,
    include_directories : [inc_mesa, inc_include, inc_src, inc_mapi, inc_gallium],
    c_args : [c_msvc_compat_args, static_glapi_args],
    dependencies : [dep_thread, idep_mesautil],
    build_by_default : false,
  )
endif

# TODO: this test doesn't compile on windows with mingw or msvc due to
# undefined symbols from libglapi_static, but that should be fixable.
if with_any_opengl and not with_shared_glapi and with_tests and not with_platform_windows
  test(
    'glapi_static_check_table',
    executable(
      'glapi_static_check_table',
      ['tests/check_table.cpp', glapitable_h],
      include_directories : [inc_include, inc_src, inc_mesa, inc_mapi, inc_gallium],
      link_with : [libglapi_static],
      dependencies : [idep_gtest, dep_thread],
    ),
    suite : ['mapi'],
    protocol : 'gtest',
  )
endif
