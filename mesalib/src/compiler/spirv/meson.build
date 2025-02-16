# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

vtn_gather_types_c = custom_target(
  'vtn_gather_types.c',
  input : files('vtn_gather_types_c.py', 'spirv.core.grammar.json'),
  output : 'vtn_gather_types.c',
  command : [prog_python, '@INPUT0@', '@INPUT1@', '@OUTPUT@'],
)

spirv_info = custom_target(
  'spirv_info',
  input : files('spirv_info_gen.py', 'spirv.core.grammar.json'),
  output : ['spirv_info.h', 'spirv_info.c'],
  command : [prog_python, '@INPUT0@', '--json', '@INPUT1@',
             '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@'],
)
spirv_info_h = spirv_info[0]

vtn_generator_ids_h = custom_target(
  'vtn_generator_ids.h',
  input : files('vtn_generator_ids_h.py', 'spir-v.xml'),
  output : 'vtn_generator_ids.h',
  command : [prog_python, '@INPUT0@', '@INPUT1@', '@OUTPUT@'],
)

files_libvtn = files(
  'GLSL.ext.AMD.h',
  'GLSL.std.450.h',
  'OpenCL.std.h',
  'gl_spirv.c',
  'nir_spirv.h',
  'spirv.h',
  'spirv_to_nir.c',
  'vtn_alu.c',
  'vtn_amd.c',
  'vtn_cfg.c',
  'vtn_cmat.c',
  'vtn_debug.c',
  'vtn_glsl450.c',
  'vtn_opencl.c',
  'vtn_private.h',
  'vtn_structured_cfg.c',
  'vtn_subgroup.c',
  'vtn_variables.c',
)

libvtn = static_library(
  'vtn',
  [files_libvtn,
   spirv_info,
   vtn_gather_types_c,
   vtn_generator_ids_h,
  ],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa],
  c_args : [c_msvc_compat_args, no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_spirv_tools, idep_nir, dep_valgrind],
  build_by_default : false,
)

idep_vtn = declare_dependency(
  dependencies : [dep_spirv_tools, idep_nir, idep_mesautil],
  link_with : libvtn,
)

spirv2nir = executable(
  'spirv2nir',
  files('spirv2nir.c') + [
   vtn_generator_ids_h,
  ],
  dependencies : [dep_m, idep_vtn, idep_mesautil],
  include_directories : [inc_include, inc_src],
  c_args : [c_msvc_compat_args, no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : with_tools.contains('nir'),
  install : with_tools.contains('nir'),
)

if get_option('mesa-clc') != 'system' and (with_driver_using_cl or \
                                           get_option('install-mesa-clc'))
   prog_vtn_bindgen2 = executable(
     'vtn_bindgen2',
     ['vtn_bindgen2.c'],
     include_directories : [inc_include, inc_src],
     c_args : [c_msvc_compat_args, no_override_init_args],
     dependencies : [idep_vtn, idep_mesautil, idep_nir],
     # If we can run host binaries directly, just build vtn_bindgen for the host.
     # Most commonly this happens when doing a cross compile from an x86_64 build
     # machine to an x86 host
     native : not meson.can_run_host_binaries(),
     install : get_option('install-mesa-clc'),
   )
endif

if with_tests
  test(
    'spirv_tests',
    executable(
      'spirv_tests',
      files(
        'tests/helpers.h',
        'tests/avail_vis.cpp',
        'tests/volatile.cpp',
        'tests/control_flow_tests.cpp',
      ),
      c_args : [c_msvc_compat_args, no_override_init_args],
      gnu_symbol_visibility : 'hidden',
      include_directories : [inc_include, inc_src],
      dependencies : [idep_vtn, dep_thread, idep_gtest, idep_nir, idep_mesautil],
    ),
    suite : ['compiler', 'spirv'],
    protocol : 'gtest',
  )
endif
