# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

files_vulkan_wsi = files('wsi_common.c')
links_vulkan_wsi = []
platform_deps = []

if dep_libdrm.found()
  files_vulkan_wsi += files('wsi_common_drm.c')
endif

if with_platform_x11
  files_vulkan_wsi += files('wsi_common_x11.c')
endif

if with_platform_wayland
  files_vulkan_wsi += files('wsi_common_wayland.c')
  files_vulkan_wsi += wp_files['fifo-v1']
  files_vulkan_wsi += wp_files['commit-timing-v1']
  files_vulkan_wsi += wp_files['linux-dmabuf-unstable-v1']
  files_vulkan_wsi += wp_files['presentation-time']
  files_vulkan_wsi += wp_files['tearing-control-v1']
  links_vulkan_wsi += libloader_wayland_helper
  files_vulkan_wsi += wp_files['linux-drm-syncobj-v1']
  files_vulkan_wsi += wp_files['color-management-v1']
endif

if with_platform_windows
  files_vulkan_wsi += files('wsi_common_win32.cpp')
  platform_deps += dep_dxheaders
else
  files_vulkan_wsi += files('wsi_common_headless.c')
endif

if with_platform_macos
  files_vulkan_wsi += files('wsi_common_metal.c', 'wsi_common_metal_layer.m')
endif

if system_has_kms_drm and not with_platform_android
  files_vulkan_wsi += files('wsi_common_display.c')
endif

wsi_entrypoints = custom_target(
  'wsi_entrypoints',
  input : [vk_entrypoints_gen, vk_api_xml],
  output : ['wsi_common_entrypoints.h', 'wsi_common_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@', '--prefix', 'wsi',
    '--beta', with_vulkan_beta.to_string()
  ],
  depend_files : vk_entrypoints_gen_depend_files,
)

libvulkan_wsi = static_library(
  'vulkan_wsi',
  [files_vulkan_wsi, wsi_entrypoints],
  include_directories : [inc_include, inc_src],
  dependencies : [
    vulkan_wsi_deps, dep_libdrm, dep_libudev, idep_vulkan_util_headers,
    idep_vulkan_runtime_headers, idep_xmlconfig, idep_mesautil, platform_deps,
    idep_blake3
  ],
  link_with: links_vulkan_wsi,
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

idep_vulkan_wsi_headers = declare_dependency(
  sources : wsi_entrypoints[0],
  dependencies : idep_vulkan_wsi_defines,
  include_directories : include_directories('.')
)

# This is likely a bug in the Meson VS backend, as MSVC with ninja works fine.
# See this discussion here:
# https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/10506
if get_option('backend').startswith('vs')
  idep_vulkan_wsi = declare_dependency(
    link_with : libvulkan_wsi,
    dependencies : idep_vulkan_wsi_headers
  )
else
  idep_vulkan_wsi = declare_dependency(
    # Instruct users of this library to link with --whole-archive.  Otherwise,
    # our weak function overloads may not resolve properly.
    link_whole : libvulkan_wsi,
    dependencies : [
      idep_vulkan_wsi_headers, dep_libudev
    ]
  )
endif
