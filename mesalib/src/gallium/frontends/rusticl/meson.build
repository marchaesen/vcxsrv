# Copyright © 

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

rust = import('unstable-rust')

libmesa_rust_util_files = files(
  'util/lib.rs',
  'util/assert.rs',
  'util/properties.rs',
  'util/ptr.rs',
  'util/string.rs',
)

libmesa_rust_files = files(
  'mesa/lib.rs',
  'mesa/compiler.rs',
  'mesa/compiler/clc.rs',
  'mesa/compiler/clc/spirv.rs',
  'mesa/compiler/nir.rs',
  'mesa/pipe.rs',
  'mesa/pipe/context.rs',
  'mesa/pipe/device.rs',
  'mesa/pipe/fence.rs',
  'mesa/pipe/screen.rs',
  'mesa/pipe/transfer.rs',
)

rusticl_files = files(
  'lib.rs',
  'api.rs',
  'api/context.rs',
  'api/device.rs',
  'api/event.rs',
  'api/icd.rs',
  'api/kernel.rs',
  'api/memory.rs',
  'api/platform.rs',
  'api/program.rs',
  'api/queue.rs',
  'api/types.rs',
  'api/util.rs',
  'core.rs',
  'core/context.rs',
  'core/device.rs',
  'core/format.rs',
  'core/kernel.rs',
  'core/memory.rs',
  'core/program.rs',
  'core/queue.rs',
  'core/util.rs',
  'core/version.rs',
)

rusticl_args = [
  # we error on all clippy warnings unless they are disabled
  '-Dclippy::all',
  # we want to add asserts in control flow
  '-Aclippy::assertions_on_constants',
  # warns on Arc<_> as keys
  '-Aclippy::mutable_key_type',
  '-Aclippy::not_unsafe_ptr_arg_deref',
  # dunno, kind of looks nicier being explicit
  '-Aclippy::redundant_field_names',
  '-Aclippy::too_many_arguments',
  '-Aclippy::type_complexity',
]

rusticl_gen_args = [
  # can't do anything about it anyway
  '-Aclippy::all',
]

rusticl_bindgen_args = [
  '--no-convert-floats',
  '--use-array-pointers-in-arguments',
  '--size_t-is-usize',
  '--default-enum-style', 'rust',
  '--with-derive-partialeq',
  '--with-derive-eq',
  '--with-derive-partialord',
  '--with-derive-ord',
  '--with-derive-hash',
  '--with-derive-default',
  '--anon-fields-prefix', 'anon_',
]

rusticl_bindgen_c_args = [
  '-fno-builtin-malloc',
]

cl_c_args = [
  '-DCL_USE_DEPRECATED_OPENCL_1_0_APIS',
  '-DCL_USE_DEPRECATED_OPENCL_1_1_APIS',
  '-DCL_USE_DEPRECATED_OPENCL_1_2_APIS',
  '-DCL_USE_DEPRECATED_OPENCL_2_0_APIS',
  '-DCL_USE_DEPRECATED_OPENCL_2_1_APIS',
  '-DCL_USE_DEPRECATED_OPENCL_2_2_APIS',
  '-DCL_TARGET_OPENCL_VERSION=300',
]

rusticl_opencl_bindings_rs = rust.bindgen(
  input : [
    'rusticl_opencl_bindings.h',
    opencl_headers,
  ],
  output : 'rusticl_opencl_bindings.rs',
  include_directories : [
    inc_include,
  ],
  c_args : [
    rusticl_bindgen_c_args,
    cl_c_args,
  ],
  args : [
    rusticl_bindgen_args,
    '--disable-header-comment',
    '--ignore-functions',
    # needed because bindgen adds *mut void fields...
    '--raw-line', 'unsafe impl std::marker::Sync for _cl_icd_dispatch {}',
    '--whitelist-type', 'cl_.*',
    '--whitelist-var', 'CL_.*',
    # some info types need to be strongly typed so we can implement various get_infos
    '--new-type-alias-deref', 'cl_(mem|image|pipe)_info',
    '--new-type-alias-deref', 'cl_kernel_(arg|work_group)_info',
    '--new-type-alias-deref', 'cl_(event|profiling)_info',
  ],
)

rusticl_opencl_gen = static_library(
  'rusticl_opencl_gen',
  rusticl_opencl_bindings_rs,
  gnu_symbol_visibility : 'hidden',
  rust_crate_type : 'rlib',
  rust_args : [
    rusticl_gen_args,
    '-Anon_snake_case',
    '-Anon_camel_case_types',
    '-Anon_upper_case_globals',
  ],
)

rusticl_mesa_bindings_inline_wrapper = static_library(
  'mesa_bindings_inline_wrapper',
  [
    'rusticl_mesa_inline_bindings_wrapper.c',
    'rusticl_mesa_inline_bindings_wrapper.h',
    'rusticl_nir.c',
    'rusticl_nir.h',
  ],
  gnu_symbol_visibility : 'hidden',
  include_directories : [
    inc_gallium,
    inc_gallium_aux,
    inc_include,
    inc_nir,
    inc_src,
  ],
  c_args : [
    pre_args,
    cl_c_args,
  ],
  dependencies: [
    idep_nir_headers,
  ],
)

rusticl_mesa_bindings_rs = rust.bindgen(
  input : 'rusticl_mesa_bindings.h',
  output : 'rusticl_mesa_bindings.rs',
  include_directories : [
    inc_compiler,
    inc_gallium,
    inc_gallium_aux,
    inc_include,
    inc_nir,
    inc_src,
  ],
  c_args : [
    rusticl_bindgen_c_args,
    pre_args,
  ],
  args : [
    rusticl_bindgen_args,
    '--whitelist-function', 'blob_.*',
    '--whitelist-function', 'clc_.*',
    '--whitelist-function', 'disk_cache_.*',
    '--whitelist-function', 'free',
    '--whitelist-function', 'glsl_.*',
    '--whitelist-function', 'malloc',
    '--whitelist-function', 'mesa_.*',
    '--whitelist-function', 'nir_.*',
    '--whitelist-function', 'pipe_.*',
    '--whitelist-function', 'rusticl_.*',
    '--whitelist-function', 'rz?alloc_.*',
    '--whitelist-function', 'spirv_.*',
    '--whitelist-function', 'u_.*',
    '--whitelist-function', 'util_format_.*',
    '--whitelist-type', 'pipe_endian',
    '--whitelist-type', 'clc_kernel_arg_access_qualifier',
    '--bitfield-enum', 'clc_kernel_arg_access_qualifier',
    '--whitelist-type', 'clc_kernel_arg_type_qualifier',
    '--bitfield-enum', 'clc_kernel_arg_type_qualifier',
    '--bitfield-enum', 'nir_opt_if_options',
    '--bitfield-enum', 'nir_variable_mode',
    '--whitelist-type', 'float_controls',
    '--whitelist-var', 'PIPE_.*',
    '--bitfield-enum', 'pipe_map_flags',
    '--allowlist-var', 'stderr',
    '--allowlist-var', 'stdout',
    '--bitfield-enum', 'nir_lower_int64_options',
    '--allowlist-type', 'pipe_tex_wrap',
    '--constified-enum-module', 'pipe_tex_wrap',
    '--allowlist-type', 'pipe_tex_filter',
    '--constified-enum-module', 'pipe_tex_filter',
    '--allowlist-type', 'gl_access_qualifier',
    '--bitfield-enum', 'gl_access_qualifier',
    '--allowlist-type', 'cl_sampler_.*_mode',
    '--constified-enum-module', 'cl_sampler_.*_mode',
  ],
)

idep_rusticl_gen = declare_dependency(
  sources: [
    rusticl_opencl_bindings_rs,
  ],
)

libmesa_rust_gen = static_library(
  'mesa_rust_gen',
  rusticl_mesa_bindings_rs,
  gnu_symbol_visibility : 'hidden',
  link_with: [
    libgallium,
  ],
  dependencies: [
    idep_clc,
  ],
  rust_crate_type : 'rlib',
  rust_args : [
    rusticl_gen_args,
    '-Anon_snake_case',
    '-Anon_camel_case_types',
    '-Anon_upper_case_globals',
  ],
)

libmesa_rust_util = static_library(
  'mesa_rust_util',
  [libmesa_rust_util_files],
  gnu_symbol_visibility : 'hidden',
  rust_crate_type : 'rlib',
  rust_args : [
    rusticl_args,
  ],
)

libmesa_rust = static_library(
  'mesa_rust',
  [libmesa_rust_files],
  gnu_symbol_visibility : 'hidden',
  rust_crate_type : 'rlib',
  rust_args : [
    rusticl_args,
  ],
  link_with : [
    libmesa_rust_gen,
    libmesa_rust_util,
    rusticl_mesa_bindings_inline_wrapper,
  ]
)

librusticl = static_library(
  'rusticl',
  [rusticl_files],
  gnu_symbol_visibility : 'hidden',
  rust_crate_type : 'staticlib',
  rust_args : [
    rusticl_args,
  ],
  link_with : [
    libmesa_rust,
    libmesa_rust_gen,
    libmesa_rust_util,
    rusticl_opencl_gen,
  ],
  dependencies : [
    idep_rusticl_gen,
  ],
)
