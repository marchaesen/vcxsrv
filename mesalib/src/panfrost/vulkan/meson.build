# Copyright © 2021 Collabora Ltd.
#
# Derived from the freedreno driver which is:
# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

panvk_entrypoints = custom_target(
  'panvk_entrypoints.[ch]',
  input : [vk_entrypoints_gen, vk_api_xml],
  output : ['panvk_entrypoints.h', 'panvk_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@', '--prefix', 'panvk',
    '--device-prefix', 'panvk_v6', '--device-prefix', 'panvk_v7',
    '--device-prefix', 'panvk_v9', '--device-prefix', 'panvk_v10',
    '--beta', with_vulkan_beta.to_string()
  ],
  depend_files : vk_entrypoints_gen_depend_files,
)

panvk_tracepoints = custom_target(
  'panvk_tracepoints.[ch]',
  input: 'panvk_tracepoints.py',
  output: ['panvk_tracepoints.h',
           'panvk_tracepoints_perfetto.h',
           'panvk_tracepoints.c'],
  command: [
    prog_python, '@INPUT@',
    '--import-path', join_paths(dir_source_root, 'src/util/perf/'),
    '--utrace-hdr', '@OUTPUT0@',
    '--perfetto-hdr', '@OUTPUT1@',
    '--utrace-src', '@OUTPUT2@',
  ],
  depend_files: u_trace_py,
)

libpanvk_files = files(
  'panvk_buffer.c',
  'panvk_cmd_pool.c',
  'panvk_device_memory.c',
  'panvk_image.c',
  'panvk_instance.c',
  'panvk_mempool.c',
  'panvk_physical_device.c',
  'panvk_priv_bo.c',
  'panvk_utrace.c',
  'panvk_wsi.c',
)
libpanvk_files += [sha1_h]

panvk_deps = []
panvk_flags = []
panvk_per_arch_libs = []

bifrost_archs = [6, 7]
bifrost_inc_dir = ['bifrost']
bifrost_files = [
  'bifrost/panvk_vX_meta_desc_copy.c',
]

valhall_archs = [9, 10]
valhall_inc_dir = ['valhall']
valhall_files = []

jm_archs = [6, 7]
jm_inc_dir = ['jm']
jm_files = [
  'jm/panvk_vX_cmd_buffer.c',
  'jm/panvk_vX_cmd_dispatch.c',
  'jm/panvk_vX_cmd_draw.c',
  'jm/panvk_vX_cmd_event.c',
  'jm/panvk_vX_cmd_query.c',
  'jm/panvk_vX_cmd_precomp.c',
  'jm/panvk_vX_device.c',
  'jm/panvk_vX_event.c',
  'jm/panvk_vX_queue.c',
]

csf_archs = [10]
csf_inc_dir = ['csf']
csf_files = [
  'csf/panvk_vX_cmd_buffer.c',
  'csf/panvk_vX_cmd_dispatch.c',
  'csf/panvk_vX_cmd_draw.c',
  'csf/panvk_vX_cmd_event.c',
  'csf/panvk_vX_cmd_query.c',
  'csf/panvk_vX_cmd_precomp.c',
  'csf/panvk_vX_device.c',
  'csf/panvk_vX_event.c',
  'csf/panvk_vX_exception_handler.c',
  'csf/panvk_vX_queue.c',
  'csf/panvk_vX_utrace.c',
]

common_per_arch_files = [
  panvk_entrypoints[0],
  panvk_tracepoints[0],
  'panvk_vX_blend.c',
  'panvk_vX_buffer_view.c',
  'panvk_vX_cmd_fb_preload.c',
  'panvk_vX_cmd_desc_state.c',
  'panvk_vX_cmd_dispatch.c',
  'panvk_vX_cmd_draw.c',
  'panvk_vX_cmd_meta.c',
  'panvk_vX_cmd_push_constant.c',
  'panvk_vX_descriptor_set.c',
  'panvk_vX_descriptor_set_layout.c',
  'panvk_vX_device.c',
  'panvk_vX_precomp_cache.c',
  'panvk_vX_query_pool.c',
  'panvk_vX_image_view.c',
  'panvk_vX_nir_lower_descriptors.c',
  'panvk_vX_sampler.c',
  'panvk_vX_shader.c',
]

foreach arch : [6, 7, 10]
  per_arch_files = common_per_arch_files
  inc_panvk_per_arch = []

  if arch in bifrost_archs
    inc_panvk_per_arch += bifrost_inc_dir
    per_arch_files += bifrost_files
  elif arch in valhall_archs
    inc_panvk_per_arch += valhall_inc_dir
    per_arch_files += valhall_files
  endif

  if arch in jm_archs
    inc_panvk_per_arch += jm_inc_dir
    per_arch_files += jm_files
  elif arch in csf_archs
    inc_panvk_per_arch += csf_inc_dir
    per_arch_files += csf_files
  endif

  panvk_per_arch_libs += static_library(
    'panvk_v@0@'.format(arch),
    per_arch_files,
    include_directories : [
      inc_include,
      inc_src,
      inc_panfrost,
      inc_panvk_per_arch,
    ],
    dependencies : [
      idep_nir_headers,
      idep_pan_packers,
      idep_vulkan_util_headers,
      idep_vulkan_runtime_headers,
      idep_vulkan_wsi_headers,
      idep_mesautil,
      dep_libdrm,
      dep_valgrind,
      idep_libpan_per_arch[arch.to_string()],
    ],
    c_args : [no_override_init_args, panvk_flags, '-DPAN_ARCH=@0@'.format(arch)],
  )
endforeach

if with_perfetto
  panvk_deps += dep_perfetto
  libpanvk_files += ['panvk_utrace_perfetto.cc']
endif

if with_platform_wayland
  panvk_deps += dep_wayland_client
  libpanvk_files += [wayland_drm_client_protocol_h, wayland_drm_protocol_c]
endif

if with_platform_android
  panvk_deps += [dep_android]
  panvk_flags += '-DVK_USE_PLATFORM_ANDROID_KHR'
  libpanvk_files += files('panvk_android.c')
endif

libvulkan_panfrost = shared_library(
  'vulkan_panfrost',
  [libpanvk_files, panvk_entrypoints, panvk_tracepoints],
  include_directories : [
    inc_include,
    inc_src,
    inc_panfrost,
  ],
  link_whole : [panvk_per_arch_libs],
  link_with : [
    libpanfrost_shared,
    libpanfrost_midgard,
    libpanfrost_bifrost,
    libpanfrost_decode,
    libpanfrost_lib,
    libpanfrost_util,
  ],
  dependencies : [
    dep_dl,
    dep_elf,
    dep_libdrm,
    dep_m,
    dep_thread,
    dep_valgrind,
    idep_nir,
    idep_pan_packers,
    panvk_deps,
    idep_vulkan_util,
    idep_vulkan_runtime,
    idep_vulkan_wsi,
    idep_mesautil,
  ],
  c_args : [no_override_init_args, panvk_flags],
  link_args : [ld_args_bsymbolic, ld_args_gc_sections, ld_args_build_id],
  install : true,
)

panfrost_icd = custom_target(
  'panfrost_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : 'panfrost_icd.@0@.json'.format(host_machine.cpu()),
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.4', '--xml', '@INPUT1@',
    '--lib-path', join_paths(get_option('prefix'), get_option('libdir'),
                             'libvulkan_panfrost.so'),
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install_tag : 'runtime',
  install : true,
)

_dev_icdname = 'panfrost_devenv_icd.@0@.json'.format(host_machine.cpu())
_dev_icd = custom_target(
  'panfrost_devenv_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : _dev_icdname,
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.4', '--xml', '@INPUT1@',
    '--lib-path', meson.current_build_dir() / 'libvulkan_panfrost.so',
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
)

devenv.append('VK_DRIVER_FILES', _dev_icd.full_path())
# Deprecated: replaced by VK_DRIVER_FILES above
devenv.append('VK_ICD_FILENAMES', _dev_icd.full_path())
