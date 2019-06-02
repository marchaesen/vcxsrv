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

tu_entrypoints = custom_target(
  'tu_entrypoints.[ch]',
  input : ['tu_entrypoints_gen.py', vk_api_xml],
  output : ['tu_entrypoints.h', 'tu_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--outdir',
    meson.current_build_dir()
  ],
  depend_files : files('tu_extensions.py'),
)

tu_extensions_c = custom_target(
  'tu_extensions.c',
  input : ['tu_extensions.py', vk_api_xml],
  output : ['tu_extensions.c', 'tu_extensions.h'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--out-c', '@OUTPUT0@',
    '--out-h', '@OUTPUT1@'
  ],
)

tu_format_table_c = custom_target(
  'tu_format_table.c',
  input : ['vk_format_table.py', 'vk_format_layout.csv'],
  output : 'vk_format_table.c',
  command : [prog_python, '@INPUT@'],
  depend_files : files('vk_format_parse.py'),
  capture : true,
)

libtu_files = files(
  'tu_cmd_buffer.c',
  'tu_cs.c',
  'tu_device.c',
  'tu_descriptor_set.c',
  'tu_descriptor_set.h',
  'tu_drm.c',
  'tu_fence.c',
  'tu_formats.c',
  'tu_image.c',
  'tu_meta_blit.c',
  'tu_meta_buffer.c',
  'tu_meta_clear.c',
  'tu_meta_copy.c',
  'tu_meta_resolve.c',
  'tu_pass.c',
  'tu_pipeline.c',
  'tu_pipeline_cache.c',
  'tu_private.h',
  'tu_query.c',
  'tu_shader.c',
  'tu_util.c',
  'tu_util.h',
  'tu_wsi.c',
  'vk_format.h',
)

tu_deps = []
tu_flags = []

if with_platform_wayland
  tu_deps += dep_wayland_client
  tu_flags += '-DVK_USE_PLATFORM_WAYLAND_KHR'
  libtu_files += files('tu_wsi_wayland.c')
endif

libvulkan_freedreno = shared_library(
  'vulkan_freedreno',
  [libtu_files, tu_entrypoints, tu_extensions_c, tu_format_table_c],
  include_directories : [
    inc_common,
    inc_compiler,
    inc_vulkan_wsi,
    inc_freedreno,
  ],
  link_with : [
    libvulkan_wsi,
    libmesa_util,
    libfreedreno_drm, # required by ir3_shader_get_variant, which we don't use
    libfreedreno_ir3,
  ],
  dependencies : [
    dep_dl,
    dep_elf,
    dep_libdrm,
    dep_llvm,
    dep_m,
    dep_thread,
    dep_valgrind,
    idep_nir,
    tu_deps,
    idep_vulkan_util,
  ],
  c_args : [c_vis_args, no_override_init_args, tu_flags],
  link_args : [ld_args_bsymbolic, ld_args_gc_sections],
  install : true,
)

freedreno_icd = custom_target(
  'freedreno_icd',
  input : 'tu_icd.py',
  output : 'freedreno_icd.@0@.json'.format(host_machine.cpu()),
  command : [
    prog_python, '@INPUT@',
    '--lib-path', join_paths(get_option('prefix'), get_option('libdir')),
    '--out', '@OUTPUT@',
  ],
  depend_files : files('tu_extensions.py'),
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install : true,
)

tu_dev_icd = custom_target(
  'tu_dev_icd',
  input : 'tu_icd.py',
  output : 'dev_icd.json',
  command : [
    prog_python, '@INPUT@', '--lib-path', meson.current_build_dir(),
    '--out', '@OUTPUT@'
  ],
  depend_files : files('tu_extensions.py'),
  build_by_default : true,
  install : false,
)
