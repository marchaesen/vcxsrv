# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

amd_json_files = [
  # Generated:
  '../registers/gfx6.json',
  '../registers/gfx7.json',
  '../registers/gfx8.json',
  '../registers/gfx81.json',
  '../registers/gfx9.json',
  '../registers/gfx940.json',
  '../registers/gfx10.json',
  '../registers/gfx103.json',
  '../registers/gfx11.json',
  '../registers/gfx115.json',
  '../registers/gfx12.json',

  # Manually written:
  '../registers/pkt3.json',
  '../registers/gfx10-rsrc.json',
  '../registers/gfx11-rsrc.json',
  '../registers/gfx12-rsrc.json',
  '../registers/registers-manually-defined.json',
]

sid_tables_h = custom_target(
  'sid_tables_h',
  input : ['sid_tables.py', 'sid.h'] + amd_json_files,
  output : 'sid_tables.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
)

amdgfxregs_h = custom_target(
  'amdgfxregs_h',
  input : ['../registers/makeregheader.py'] + amd_json_files,
  output : 'amdgfxregs.h',
  command : [prog_python, '@INPUT@', '--sort', 'address', '--guard', 'AMDGFXREGS_H'],
  capture : true,
)

gfx10_format_table_c = custom_target(
  'gfx10_format_table.c',
  input : files(
    'gfx10_format_table.py',
    '../../util/format/u_format.yaml', '../registers/gfx10-rsrc.json', '../registers/gfx11-rsrc.json'
  ),
  output : 'gfx10_format_table.c',
  command : [prog_python, '@INPUT@'],
  capture : true,
  depend_files : ['../registers/regdb.py']
)

amd_common_files = files(
  'ac_binary.c',
  'ac_binary.h',
  'ac_cmdbuf.c',
  'ac_cmdbuf.h',
  'ac_shader_args.c',
  'ac_shader_args.h',
  'ac_shader_util.c',
  'ac_shader_util.h',
  'ac_gather_context_rolls.c',
  'ac_gpu_info.c',
  'ac_gpu_info.h',
  'ac_surface.c',
  'ac_surface.h',
  'ac_debug.c',
  'ac_debug.h',
  'ac_descriptors.c',
  'ac_descriptors.h',
  'ac_formats.c',
  'ac_formats.h',
  'ac_linux_drm.h',
  'ac_shadowed_regs.c',
  'ac_shadowed_regs.h',
  'ac_spm.c',
  'ac_spm.h',
  'ac_sqtt.c',
  'ac_sqtt.h',
  'ac_rgp.c',
  'ac_rgp.h',
  'ac_msgpack.c',
  'ac_msgpack.h',
  'amd_family.c',
  'ac_parse_ib.c',
  'ac_perfcounter.c',
  'ac_perfcounter.h',
  'ac_pm4.c',
  'ac_pm4.h',
  'ac_vcn_av1_default.h',
  'ac_vcn_dec.c',
  'ac_vcn_enc.c',
  'nir/ac_nir.c',
  'nir/ac_nir.h',
  'nir/ac_nir_helpers.h',
  'nir/ac_nir_opt_outputs.c',
  'nir/ac_nir_cull.c',
  'nir/ac_nir_create_gs_copy_shader.c',
  'nir/ac_nir_lower_esgs_io_to_mem.c',
  'nir/ac_nir_lower_global_access.c',
  'nir/ac_nir_lower_image_opcodes_cdna.c',
  'nir/ac_nir_lower_intrinsics_to_args.c',
  'nir/ac_nir_lower_legacy_gs.c',
  'nir/ac_nir_lower_legacy_vs.c',
  'nir/ac_nir_lower_mem_access_bit_sizes.c',
  'nir/ac_nir_lower_resinfo.c',
  'nir/ac_nir_lower_taskmesh_io_to_mem.c',
  'nir/ac_nir_lower_tess_io_to_mem.c',
  'nir/ac_nir_lower_tex.c',
  'nir/ac_nir_lower_ngg.c',
  'nir/ac_nir_lower_ngg_gs.c',
  'nir/ac_nir_lower_ngg_mesh.c',
  'nir/ac_nir_lower_ps_early.c',
  'nir/ac_nir_lower_ps_late.c',
  'nir/ac_nir_lower_sin_cos.c',
  'nir/ac_nir_meta.h',
  'nir/ac_nir_meta_cs_blit.c',
  'nir/ac_nir_meta_cs_clear_copy_buffer.c',
  'nir/ac_nir_meta_ps_resolve.c',
  'nir/ac_nir_opt_pack_half.c',
  'nir/ac_nir_opt_shared_append.c',
  'nir/ac_nir_prerast_utils.c',
  'nir/ac_nir_surface.c',
  'nir/ac_nir_surface.h',
)

if not with_platform_windows
  amd_common_files += files(
    'ac_linux_drm.c',
  )
endif

link_with = []
c_args = ['-DADDR_FASTCALL=']
if with_amdgpu_virtio
  c_args += ['-DHAVE_AMDGPU_VIRTIO', '-DENABLE_DRM_AMDGPU']
  amd_common_files += files(
    'virtio/amdgpu_virtio.c',
    'virtio/amdgpu_virtio_bo.c',
    'virtio/amdgpu_virtio_device.c',
    'virtio/amdgpu_virtio_private.h',
    'virtio/amdgpu_virtio_proto.h')
  link_with += libvdrm
endif

if dep_elf.found()
  amd_common_files += files(
    'ac_rtld.c',
    'ac_rtld.h',
    'ac_rgp_elf_object_pack.c',
  )
endif

libamd_common = static_library(
  'amd_common',
  [amd_common_files, sid_tables_h, amdgfxregs_h, gfx10_format_table_c],
  include_directories : [
    inc_include, inc_src, inc_amd, inc_virtio_gpu,
  ],
  dependencies : [dep_llvm.partial_dependency(compile_args: true, includes: true)] + [
    dep_thread, dep_elf, dep_libdrm_amdgpu, dep_valgrind,
    idep_mesautil, idep_nir_headers, idep_nir
  ],
  link_with: [ link_with ],
  gnu_symbol_visibility : 'hidden',
  c_args : c_args
)

idep_amdgfxregs_h = declare_dependency(sources : [amdgfxregs_h])

executable(
  'ac_ib_parser',
  ['ac_ib_parser.c'],
  link_with: [libamd_common],
  include_directories : [
    inc_amd, inc_include, inc_src,
  ],
  dependencies : [idep_amdgfxregs_h, idep_mesautil, idep_nir_headers],
)

if with_tests and not with_platform_windows
  test(
    'ac_surface_modifier_test',
    executable(
      'ac_surface_modifier_test',
      ['ac_surface_modifier_test.c'],
      link_with: [libamd_common, libamdgpu_addrlib],
      include_directories : [
        inc_amd, inc_include, inc_src,
      ],
      c_args : cpp_args_addrlib,
      dependencies: [idep_amdgfxregs_h, dep_libdrm_amdgpu, idep_mesautil],
    ),
    suite: ['amd']
  )

  # Limit this to only a few architectures for the Gitlab CI.
  if ['x86', 'x86_64', 'aarch64'].contains(host_machine.cpu_family())
    test(
      'ac_surface_meta_address_test',
      executable(
        'ac_surface_meta_address_test',
        ['ac_surface_meta_address_test.c'],
        link_with: [libamd_common, libamdgpu_addrlib],
        include_directories : [
          inc_amd, inc_include, inc_src,
        ],
        c_args : cpp_args_addrlib,
        dependencies: [idep_amdgfxregs_h, dep_libdrm_amdgpu, idep_mesautil, dep_openmp],
      ),
      suite: ['amd']
    )
  endif
endif
