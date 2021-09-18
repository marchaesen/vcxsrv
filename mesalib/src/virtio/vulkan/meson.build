# Copyright 2019 Google LLC
# SPDX-License-Identifier: MIT
#
# based in part on anv and radv which are:
# Copyright © 2017 Intel Corporation

vn_entrypoints = custom_target(
  'vn_entrypoints',
  input : [vk_entrypoints_gen, vk_api_xml],
  output : ['vn_entrypoints.h', 'vn_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@', '--prefix', 'vn',
  ],
)

virtio_icd = custom_target(
  'virtio_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : 'virtio_icd.@0@.json'.format(host_machine.cpu()),
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.2', '--xml', '@INPUT1@',
    '--lib-path', join_paths(get_option('prefix'), get_option('libdir'),
                             'libvulkan_virtio.so'),
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install : true,
)

libvn_files = files(
  'vn_buffer.c',
  'vn_command_buffer.c',
  'vn_common.c',
  'vn_cs.c',
  'vn_descriptor_set.c',
  'vn_device.c',
  'vn_device_memory.c',
  'vn_icd.c',
  'vn_image.c',
  'vn_instance.c',
  'vn_physical_device.c',
  'vn_pipeline.c',
  'vn_query_pool.c',
  'vn_queue.c',
  'vn_render_pass.c',
  'vn_ring.c',
  'vn_renderer_virtgpu.c',
  'vn_renderer_vtest.c',
)

vn_deps = [
  dep_libdrm,
  dep_thread,
  idep_mesautil,
  idep_vulkan_util,
  idep_xmlconfig,
]

vn_flags = [
  no_override_init_args,
]

vn_libs = []

if with_platform_wayland or with_platform_x11
  libvn_files += files('vn_wsi.c')
  vn_flags += '-DVN_USE_WSI_PLATFORM'
  vn_libs += libvulkan_wsi
endif

if with_platform_wayland
  libvn_files += files('vn_wsi_wayland.c')
  vn_deps += dep_wayland_client
  vn_flags += '-DVK_USE_PLATFORM_WAYLAND_KHR'
endif

if with_platform_x11
  libvn_files += files('vn_wsi_x11.c')
  vn_deps += dep_xcb_dri3
  vn_flags += [
    '-DVK_USE_PLATFORM_XCB_KHR',
    '-DVK_USE_PLATFORM_XLIB_KHR',
  ]
endif

if with_platform_android
  libvn_files += files('vn_android.c')
  vn_deps += dep_android
  vn_flags += '-DVK_USE_PLATFORM_ANDROID_KHR'
endif

libvulkan_virtio = shared_library(
  'vulkan_virtio',
  [libvn_files, vn_entrypoints, sha1_h],
  include_directories : [
    inc_include, inc_src, inc_vulkan_wsi, inc_virtio,
  ],
  link_with : vn_libs,
  dependencies : [vn_deps],
  c_args : [vn_flags],
  link_args : [ld_args_bsymbolic, ld_args_gc_sections],
  gnu_symbol_visibility : 'hidden',
  install : true,
)
