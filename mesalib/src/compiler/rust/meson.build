# Copyright © 2024 Igalia S.L.
# SPDX-License-Identifier: MIT

_compiler_rs_sources = [
  'as_slice.rs',
  'bitset.rs',
  'cfg.rs',
  'memstream.rs',
  'nir_instr_printer.rs',
  'nir.rs',
  'smallvec.rs',
]

_compiler_binding_types = [
  'exec_list',
  'exec_node',
  'float_controls',
  'gc_ctx',
  'gl_access_qualifier',
  'gl_frag_result',
  'gl_interp_mode',
  'gl_shader_stage',
  'gl_subgroup_size',
  'gl_system_value',
  'gl_tess_spacing',
  'gl_varying_slot',
  'gl_vert_attrib',
  'glsl_type',
  'nir_.*',
  'mesa_scope',
  'mesa_prim',
  'pipe_shader_type',
  'shader_info',
  'tess_primitive_mode',
]

_compiler_bindgen_args = [
  '--raw-line', '#![allow(non_camel_case_types)]',
  '--raw-line', '#![allow(non_snake_case)]',
  '--raw-line', '#![allow(non_upper_case_globals)]',
  '--allowlist-var', 'nir_.*_infos',
  '--allowlist-var', 'rust_.*',
  '--allowlist-function', 'glsl_.*',
  '--allowlist-function', '_mesa_shader_stage_to_string',
  '--allowlist-function', 'nir_.*',
  '--allowlist-function', 'compiler_rs.*',
  '--allowlist-function', 'u_memstream.*',
  '--allowlist-type', 'u_memstream',
  '--no-prepend-enum-name',
]

foreach type : _compiler_binding_types
  _compiler_bindgen_args += ['--allowlist-type', type]
endforeach

_libcompiler_c_sources = files('rust_helpers.c')

_libcompiler_c = static_library(
  'compiler_c_helpers',
  [_libcompiler_c_sources],
  include_directories : [inc_include, inc_util],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
)

_idep_libcompiler_c = declare_dependency(
  include_directories: include_directories('.'),
  link_with : _libcompiler_c,
)

_compiler_bindings_rs = rust.bindgen(
  input : ['bindings.h'],
  output : 'bindings.rs',
  c_args : [
    pre_args,
  ],
  args : _compiler_bindgen_args,
  dependencies : [
    idep_nir_headers,
    idep_mesautil,
  ],
)

compiler_rs_bindgen_blocklist = []
foreach type : _compiler_binding_types
  compiler_rs_bindgen_blocklist += ['--blocklist-type', type]
endforeach

_compiler_rs_sources = structured_sources([
  # lib.rs has to go first
  'lib.rs',
  _compiler_bindings_rs,
  _compiler_rs_sources,
])

_libcompiler_rs = static_library(
  'compiler',
  _compiler_rs_sources,
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'rust',
  dependencies: [_idep_libcompiler_c],
)

# TODO: Linking Rust executables (such as unit tests) doesn't play nicely
# with the sanitizers because meson doesn't know to pass -fsanitize to the
# Rust linker.  See also https://github.com/mesonbuild/meson/issues/11741
if with_tests and get_option('b_sanitize') == 'none'
  rust.test(
    'compiler_test',
    _libcompiler_rs,
    suite : ['compiler', 'rs'],
    dependencies : [
      idep_mesautil.partial_dependency(link_args : true, links : true),
    ],
    # This is needed to ensure we link against glibc
    # See also https://gitlab.freedesktop.org/mesa/mesa/-/issues/11632
    rust_args: ['-C', 'default-linker-libraries'],
  )
endif

idep_compiler_rs = declare_dependency(
  link_with : _libcompiler_rs,
)

dep_syn = dependency('syn',
  version : '>= 2.0.15',
  fallback : ['syn', 'dep_syn'],
  required : true,
)

_libcompiler_proc_rs = static_library(
  'compiler_proc',
  'proc/lib.rs',
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_syn],
  rust_abi : 'rust',
  native : true,
)

idep_compiler_proc_rs = declare_dependency(
  link_with : _libcompiler_proc_rs,
)
