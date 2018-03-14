# Copyright © 2017 Intel Corporation

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

radv_entrypoints = custom_target(
  'radv_entrypoints.[ch]',
  input : ['radv_entrypoints_gen.py', vk_api_xml],
  output : ['radv_entrypoints.h', 'radv_entrypoints.c'],
  command : [
    prog_python2, '@INPUT0@', '--xml', '@INPUT1@', '--outdir',
    meson.current_build_dir()
  ],
  depend_files : files('radv_extensions.py'),
)

radv_extensions_c = custom_target(
  'radv_extensions.c',
  input : ['radv_extensions.py', vk_api_xml, vk_android_native_buffer_xml],
  output : ['radv_extensions.c', 'radv_extensions.h'],
  command : [
    prog_python2, '@INPUT0@', '--xml', '@INPUT1@', '--xml', '@INPUT2@', '--out-c', '@OUTPUT0@',
    '--out-h', '@OUTPUT1@'
  ],
)

vk_format_table_c = custom_target(
  'vk_format_table.c',
  input : ['vk_format_table.py', 'vk_format_layout.csv'],
  output : 'vk_format_table.c',
  command : [prog_python2, '@INPUT@'],
  depend_files : files('vk_format_parse.py'),
  capture : true,
)

libradv_files = files(
  'winsys/amdgpu/radv_amdgpu_bo.c',
  'winsys/amdgpu/radv_amdgpu_bo.h',
  'winsys/amdgpu/radv_amdgpu_cs.c',
  'winsys/amdgpu/radv_amdgpu_cs.h',
  'winsys/amdgpu/radv_amdgpu_surface.c',
  'winsys/amdgpu/radv_amdgpu_surface.h',
  'winsys/amdgpu/radv_amdgpu_winsys.c',
  'winsys/amdgpu/radv_amdgpu_winsys.h',
  'winsys/amdgpu/radv_amdgpu_winsys_public.h',
  'radv_cmd_buffer.c',
  'radv_cs.h',
  'radv_debug.c',
  'radv_debug.h',
  'radv_device.c',
  'radv_descriptor_set.c',
  'radv_descriptor_set.h',
  'radv_formats.c',
  'radv_image.c',
  'radv_meta.c',
  'radv_meta.h',
  'radv_meta_blit.c',
  'radv_meta_blit2d.c',
  'radv_meta_buffer.c',
  'radv_meta_bufimage.c',
  'radv_meta_clear.c',
  'radv_meta_copy.c',
  'radv_meta_decompress.c',
  'radv_meta_fast_clear.c',
  'radv_meta_resolve.c',
  'radv_meta_resolve_cs.c',
  'radv_meta_resolve_fs.c',
  'radv_nir_to_llvm.c',
  'radv_pass.c',
  'radv_pipeline.c',
  'radv_pipeline_cache.c',
  'radv_private.h',
  'radv_radeon_winsys.h',
  'radv_shader.c',
  'radv_shader.h',
  'radv_shader_info.c',
  'radv_query.c',
  'radv_util.c',
  'radv_util.h',
  'radv_wsi.c',
  'si_cmd_buffer.c',
  'vk_format.h',
)

radv_deps = []
radv_flags = []

if with_platform_x11
  radv_deps += dep_xcb_dri3
  radv_flags += [
    '-DVK_USE_PLATFORM_XCB_KHR',
    '-DVK_USE_PLATFORM_XLIB_KHR',
  ]
  libradv_files += files('radv_wsi_x11.c')
endif

if with_platform_wayland
  radv_deps += dep_wayland_client
  radv_flags += '-DVK_USE_PLATFORM_WAYLAND_KHR'
  libradv_files += files('radv_wsi_wayland.c')
endif

libvulkan_radeon = shared_library(
  'vulkan_radeon',
  [libradv_files, radv_entrypoints, radv_extensions_c, vk_format_table_c],
  include_directories : [
    inc_common, inc_amd, inc_amd_common, inc_compiler, inc_vulkan_util,
    inc_vulkan_wsi,
  ],
  link_with : [
    libamd_common, libamdgpu_addrlib, libvulkan_util, libvulkan_wsi,
    libmesa_util,
  ],
  dependencies : [
    dep_llvm, dep_libdrm_amdgpu, dep_thread, dep_elf, dep_dl, dep_m,
    dep_valgrind,
    idep_nir,
  ],
  c_args : [c_vis_args, no_override_init_args, radv_flags],
  link_args : [ld_args_bsymbolic, ld_args_gc_sections],
  install : true,
)

radeon_icd = custom_target(
  'radeon_icd',
  input : 'radv_icd.py',
  output : 'radeon_icd.@0@.json'.format(host_machine.cpu()),
  command : [
    prog_python2, '@INPUT@',
    '--lib-path', join_paths(get_option('prefix'), get_option('libdir')),
    '--out', '@OUTPUT@',
  ],
  depend_files : files('radv_extensions.py'),
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install : true,
)

radv_dev_icd = custom_target(
  'radv_dev_icd',
  input : 'radv_icd.py',
  output : 'dev_icd.json',
  command : [
    prog_python2, '@INPUT@', '--lib-path', meson.current_build_dir(),
    '--out', '@OUTPUT@'
  ],
  depend_files : files('radv_extensions.py'),
  build_by_default : true,
  install : false,
)
