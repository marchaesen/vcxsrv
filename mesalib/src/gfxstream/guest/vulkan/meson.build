# Copyright 2022 Android Open Source Project
# SPDX-License-Identifier: MIT

subdir('stubs')

files_lib_vulkan_gfxstream = files(
  'gfxstream_vk_device.cpp',
  'gfxstream_vk_cmd.cpp',
  'gfxstream_vk_wsi.cpp'
)

gfxstream_vk_wsi_args = []
if with_platform_x11
   gfxstream_vk_wsi_args += '-DGFXSTREAM_VK_X11'
endif

if with_platform_wayland
   gfxstream_vk_wsi_args += '-DGFXSTREAM_VK_WAYLAND'
endif

lib_vulkan_gfxstream = shared_library(
   'vulkan_gfxstream',
   files_lib_vulkan_gfxstream + files_lib_vulkan_enc + gfxstream_vk_entrypoints +
   gfxstream_guest_vk_autogen + vulkan_gfxstream_h,
   cpp_args: gfxstream_guest_args + gfxstream_vk_wsi_args,
   include_directories: [inc_vulkan_headers, inc_guest_iostream,
                         inc_vulkan_enc, inc_platform_virtgpu,
                         inc_gfxstream_vk_stubs, inc_goldfish_address_space,
                         inc_include, inc_src,
                         inc_platform_virtgpu, inc_connection_manager],
   link_with: [libgfxstream_vk_stubs, libvulkan_wsi, libplatform_virtgpu,
               libconnection_manager],
   link_args: [vulkan_icd_link_args, ld_args_bsymbolic, ld_args_gc_sections],
   link_depends: vulkan_icd_link_depends,
   dependencies: [dependency('libdrm'), idep_vulkan_wsi_headers,
                  idep_vulkan_runtime_headers, idep_vulkan_runtime,
                  idep_vulkan_util_headers, idep_vulkan_wsi, dep_valgrind,
                  aemu_base_dep],
   install: true,
)

gfxstream_icd = custom_target(
  'gfxstream_vk_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : 'gfxstream_vk_icd.@0@.json'.format(host_machine.cpu()),
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.1', '--xml', '@INPUT1@',
    '--lib-path', join_paths(get_option('prefix'), get_option('libdir'),
                             'libvulkan_gfxstream.so'),
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install : true,
)

_dev_icdname = 'gfxstream_vk_devenv_icd.@0@.json'.format(host_machine.cpu())
_dev_icd = custom_target(
  'gfxstream_vk_devenv_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : _dev_icdname,
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.3', '--xml', '@INPUT1@',
    '--lib-path', meson.current_build_dir() / 'libvulkan_gfxstream.so',
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
)

devenv.append('VK_ICD_FILENAMES', _dev_icd.full_path())
