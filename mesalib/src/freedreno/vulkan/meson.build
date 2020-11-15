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

libtu_files = files(
  'tu_clear_blit.c',
  'tu_cmd_buffer.c',
  'tu_cs.c',
  'tu_cs.h',
  'tu_device.c',
  'tu_descriptor_set.c',
  'tu_descriptor_set.h',
  'tu_formats.c',
  'tu_image.c',
  'tu_legacy.c',
  'tu_nir_lower_multiview.c',
  'tu_pass.c',
  'tu_pipeline.c',
  'tu_pipeline_cache.c',
  'tu_private.h',
  'tu_query.c',
  'tu_shader.c',
  'tu_util.c',
  'tu_util.h',
  'vk_format.h',
)

tu_deps = []
tu_flags = []
tu_link_with = []

tu_wsi = false

if with_platform_x11
  tu_deps += dep_xcb_dri3
  tu_flags += [
    '-DVK_USE_PLATFORM_XCB_KHR',
    '-DVK_USE_PLATFORM_XLIB_KHR',
  ]
  libtu_files += files('tu_wsi_x11.c')
  tu_wsi = true
endif

if with_platform_wayland
  tu_deps += dep_wayland_client
  tu_flags += '-DVK_USE_PLATFORM_WAYLAND_KHR'
  libtu_files += files('tu_wsi_wayland.c')
  tu_wsi = true
endif

if system_has_kms_drm and not with_platform_android
  tu_flags += '-DVK_USE_PLATFORM_DISPLAY_KHR'
  libtu_files += files('tu_wsi_display.c')
  tu_wsi = true
endif

if tu_wsi
  libtu_files += 'tu_wsi.c'
  tu_link_with += libvulkan_wsi
endif

if with_platform_android
  tu_flags += '-DVK_USE_PLATFORM_ANDROID_KHR'
  libtu_files += files('tu_android.c')
  tu_deps += [dep_android]
endif

if with_xlib_lease
  tu_deps += [dep_xcb_xrandr, dep_xlib_xrandr]
  tu_flags += '-DVK_USE_PLATFORM_XLIB_XRANDR_EXT'
endif

if with_freedreno_kgsl
  libtu_files += files('tu_kgsl.c')
else
  libtu_files += files('tu_drm.c')
  tu_deps += dep_libdrm
endif

libvulkan_freedreno = shared_library(
  'vulkan_freedreno',
  [libtu_files, tu_entrypoints, tu_extensions_c, freedreno_xml_header_files],
  include_directories : [
    inc_include,
    inc_src,
    inc_mapi,
    inc_mesa,
    inc_gallium,
    inc_gallium_aux,
    inc_compiler,
    inc_vulkan_wsi,
    inc_freedreno,
  ],
  link_with : [
    tu_link_with,
    libfreedreno_ir3,
    libfreedreno_layout,
  ],
  dependencies : [
    idep_libfreedreno_common,
    dep_dl,
    dep_elf,
    dep_m,
    dep_thread,
    dep_valgrind,
    idep_nir,
    tu_deps,
    idep_vulkan_util,
    idep_mesautil,
  ],
  c_args : [no_override_init_args, tu_flags],
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_bsymbolic, ld_args_gc_sections, ld_args_build_id],
  install : true,
)

if with_symbols_check
  test(
    'tu symbols check',
    symbols_check,
    args : [
      '--lib', libvulkan_freedreno,
      '--symbols-file', vulkan_icd_symbols,
      symbols_check_args,
    ],
    suite : ['freedreno'],
  )
endif

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
