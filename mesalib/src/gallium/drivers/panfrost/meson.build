# Copyright © 2017 Intel Corporation
# Copyright © 2018 Alyssa Rosenzweig

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

files_panfrost = files(
  'pan_helpers.c',
  'pan_public.h',
  'pan_screen.c',
  'pan_screen.h',
  'pan_resource.c',
  'pan_resource.h',
  'pan_context.c',
  'pan_blit.c',
  'pan_job.c',
  'pan_assemble.c',
  'pan_compute.c',
  'pan_mempool.c',
  'pan_mempool.h',
)

panfrost_includes = [
  inc_mapi,
  inc_mesa,
  inc_gallium,
  inc_gallium_aux,
  inc_include,
  inc_src,
  inc_panfrost,
]

compile_args_panfrost = [
  '-DGALLIUM_PANFROST',
  '-Wno-pointer-arith'
]

panfrost_versions = ['4', '5', '6', '7']
libpanfrost_versions = []

foreach ver : panfrost_versions
  libpanfrost_versions += static_library(
    'panfrost-v' + ver,
    ['pan_cmdstream.c', pan_packers],
    include_directories : panfrost_includes,
    c_args : ['-DPAN_ARCH=' + ver],
    gnu_symbol_visibility : 'hidden',
    dependencies : [idep_pan_packers, idep_nir, dep_libdrm],
)
endforeach

libpanfrost = static_library(
  'panfrost',
  files_panfrost,
  dependencies: [
    dep_thread,
    dep_libdrm,
    idep_mesautil,
    idep_nir,
    idep_pan_packers
  ],
  include_directories : panfrost_includes,
  c_args : [c_msvc_compat_args, compile_args_panfrost],
  gnu_symbol_visibility : 'hidden',
  link_with: [libpanfrost_versions],
)

driver_panfrost = declare_dependency(
  compile_args : compile_args_panfrost,
  link_with : [libpanfrost, libpanfrostwinsys, libpanfrost_shared, libpanfrost_midgard, libpanfrost_bifrost, libpanfrost_decode, libpanfrost_lib],
)
