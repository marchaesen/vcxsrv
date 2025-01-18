# Copyright © 2022 Collabora Ltd. and Red Hat Inc.
# SPDX-License-Identifier: MIT
hk_files = files(
  'hk_buffer.c',
  'hk_buffer.h',
  'hk_buffer_view.c',
  'hk_buffer_view.h',
  'hk_cmd_buffer.c',
  'hk_cmd_buffer.h',
  'hk_cmd_clear.c',
  'hk_cmd_dispatch.c',
  'hk_cmd_draw.c',
  'hk_cmd_meta.c',
  'hk_cmd_pool.c',
  'hk_cmd_pool.h',
  'hk_descriptor_set.h',
  'hk_descriptor_set.c',
  'hk_descriptor_set_layout.c',
  'hk_descriptor_set_layout.h',
  'hk_descriptor_table.c',
  'hk_descriptor_table.h',
  'hk_device.c',
  'hk_device.h',
  'hk_device_memory.c',
  'hk_device_memory.h',
  'hk_event.c',
  'hk_event.h',
  'hk_format.c',
  'hk_image.c',
  'hk_image.h',
  'hk_image_view.c',
  'hk_image_view.h',
  'hk_instance.c',
  'hk_instance.h',
  'hk_nir_lower_descriptors.c',
  'hk_nir_passthrough_gs.c',
  'hk_physical_device.c',
  'hk_physical_device.h',
  'hk_private.h',
  'hk_query_pool.c',
  'hk_query_pool.h',
  'hk_queue.c',
  'hk_queue.h',
  'hk_sampler.c',
  'hk_sampler.h',
  'hk_shader.c',
  'hk_shader.h',
  'hk_wsi.c',
  'hk_wsi.h'
)

hk_entrypoints = custom_target(
  'hk_entrypoints',
  input : [vk_entrypoints_gen, vk_api_xml],
  output : ['hk_entrypoints.h', 'hk_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@', '--prefix', 'hk',
    '--beta', with_vulkan_beta.to_string(),
  ],
  depend_files : vk_entrypoints_gen_depend_files,
)

hk_deps = [
  dep_libdrm,
  idep_nir,
  idep_vulkan_runtime,
  idep_vulkan_util,
  idep_vulkan_wsi,
  idep_vulkan_wsi_headers,
  idep_agx_pack,
]

libhk = static_library(
  'hk',
  [
    hk_entrypoints,
    hk_files,
    libagx_shaders,
    sha1_h,
  ],
  include_directories : [
    inc_gallium,
    inc_gallium_aux,
    inc_include,
    inc_src,
    inc_asahi,
    inc_virtio_gpu,
    inc_virtio_vdrm,
  ],
  link_with : [libasahi_lib, libasahi_layout, libasahi_compiler],
  c_args : ['-Wno-c2x-extensions'],
  dependencies : [hk_deps],
  gnu_symbol_visibility : 'hidden',
)

libvulkan_asahi = shared_library(
  'vulkan_asahi',
  link_whole : [libhk],
  link_args: [ld_args_build_id],
  gnu_symbol_visibility : 'hidden',
  install : true,
)

icd_lib_path = join_paths(get_option('prefix'), get_option('libdir'))
icd_file_name = 'libvulkan_asahi.so'
if with_platform_windows
  icd_lib_path = import('fs').relative_to(get_option('bindir'), with_vulkan_icd_dir)
  icd_file_name = 'vulkan_asahi.dll'
endif

asahi_icd = custom_target(
  'asahi_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : 'asahi_icd.@0@.json'.format(host_machine.cpu()),
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.3', '--xml', '@INPUT1@',
    '--lib-path', join_paths(icd_lib_path, icd_file_name),
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install_tag : 'runtime',
  install : true,
)

_dev_icdname = 'asahi_devenv_icd.@0@.json'.format(host_machine.cpu())
custom_target(
  'asahi_devenv_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : _dev_icdname,
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.3', '--xml', '@INPUT1@',
    '--lib-path', meson.current_build_dir() / icd_file_name,
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
)

devenv.append('VK_DRIVER_FILES', meson.current_build_dir() / _dev_icdname)
# Deprecated: replaced by VK_DRIVER_FILES above
devenv.append('VK_ICD_FILENAMES', meson.current_build_dir() / _dev_icdname)
