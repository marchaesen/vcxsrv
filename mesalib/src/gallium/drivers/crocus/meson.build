# Copyright © 2017-2019 Intel Corporation
# SPDX-License-Identifier: MIT

files_libcrocus = files(
  'gen4_blorp_exec.h',
  'driinfo_crocus.h',
  'crocus_batch.c',
  'crocus_batch.h',
  'crocus_blit.c',
  'crocus_bufmgr.c',
  'crocus_bufmgr.h',
  'crocus_clear.c',
  'crocus_context.c',
  'crocus_context.h',
  'crocus_draw.c',
  'crocus_fence.c',
  'crocus_fence.h',
  'crocus_fine_fence.c',
  'crocus_fine_fence.h',
  'crocus_formats.c',
  'crocus_genx_macros.h',
  'crocus_genx_protos.h',
  'crocus_monitor.c',
  'crocus_perf.c',
  'crocus_perf.h',
  'crocus_performance_query.c',
  'crocus_pipe.h',
  'crocus_pipe_control.c',
  'crocus_program.c',
  'crocus_program_cache.c',
  'crocus_resolve.c',
  'crocus_resource.c',
  'crocus_resource.h',
  'crocus_screen.c',
  'crocus_screen.h',
  'crocus_disk_cache.c',
)

crocus_per_hw_ver_libs = []
foreach v : ['40', '45', '50', '60', '70', '75', '80']
  crocus_per_hw_ver_libs += static_library(
    'crocus_per_hw_ver@0@'.format(v),
    ['crocus_blorp.c', 'crocus_query.c', 'crocus_state.c', 'crocus_blt.c', gen_xml_pack],
    include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_intel],
    c_args : [
      no_override_init_args, sse2_args,
      '-DGFX_VERx10=@0@'.format(v),
    ],
    gnu_symbol_visibility : 'hidden',
    dependencies : [dep_libdrm, dep_valgrind, idep_mesautil, idep_genxml, idep_nir_headers, idep_intel_dev, idep_intel_blorp_elk],
  )
endforeach

libcrocus = static_library(
  'crocus',
  [files_libcrocus, gen_xml_pack],
  include_directories : [
    inc_src, inc_include, inc_gallium, inc_gallium_aux, inc_intel,
    inc_gallium_drivers,
    # these should not be necessary, but main/macros.h...
    inc_mesa, inc_mapi
  ],
  c_args : [sse2_args],
  cpp_args : [sse2_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    dep_libdrm, dep_valgrind, idep_genxml, idep_mesautil,
    idep_libintel_common, idep_nir_headers,
    idep_intel_dev, idep_intel_blorp_elk, idep_intel_decoder_elk,
    idep_intel_compiler_elk,
  ],
  link_with : [
    crocus_per_hw_ver_libs, libisl,
    libintel_perf
  ],
)

driver_crocus = declare_dependency(
  compile_args : '-DGALLIUM_CROCUS',
  link_with : [libcrocus, libcrocuswinsys],
)
