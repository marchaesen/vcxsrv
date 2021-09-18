# Copyright © 2019 Raspberry Pi
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

v3dv_entrypoints = custom_target(
  'v3dv_entrypoints',
  input : [vk_entrypoints_gen, vk_api_xml],
  output : ['v3dv_entrypoints.h', 'v3dv_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@', '--prefix', 'v3dv',
    '--device-prefix', 'ver42',
  ],
  depend_files : vk_entrypoints_gen_depend_files,
)

libv3dv_files = files(
  'v3dv_bo.c',
  'v3dv_cl.c',
  'v3dv_cmd_buffer.c',
  'v3dv_debug.c',
  'v3dv_debug.h',
  'v3dv_descriptor_set.c',
  'v3dv_device.c',
  'v3dv_formats.c',
  'v3dv_image.c',
  'v3dv_limits.h',
  'v3dv_meta_clear.c',
  'v3dv_meta_copy.c',
  'v3dv_pass.c',
  'v3dv_pipeline.c',
  'v3dv_pipeline_cache.c',
  'v3dv_private.h',
  'v3dv_query.c',
  'v3dv_queue.c',
  'v3dv_uniforms.c',
  'v3dv_util.c',
  'v3dv_wsi.c',
)

files_per_version = files(
  'v3dvx_cmd_buffer.c',
  'v3dvx_descriptor_set.c',
  'v3dvx_device.c',
  'v3dvx_formats.c',
  'v3dvx_image.c',
  'v3dvx_pipeline.c',
  'v3dvx_meta_common.c',
  'v3dvx_pipeline.c',
  'v3dvx_queue.c',
)

# The vulkan driver only supports version >= 42, which is the version present in
# Rpi4. We need to explicitly set it as we are reusing pieces from the GL v3d
# driver.
v3d_versions = ['42']

v3dv_flags = []

dep_v3dv3 = dependency('v3dv3', required : false)
if dep_v3dv3.found()
  v3dv_flags += '-DUSE_V3D_SIMULATOR'
endif

v3dv_deps = [
  dep_dl,
  dep_libdrm,
  dep_valgrind,
  dep_v3dv3,
  idep_nir,
  idep_nir_headers,
  idep_vulkan_util,
]

if with_platform_x11
  v3dv_deps += dep_xcb_dri3
  v3dv_flags += [
    '-DVK_USE_PLATFORM_XCB_KHR',
    '-DVK_USE_PLATFORM_XLIB_KHR',
  ]
  libv3dv_files += files('v3dv_wsi_x11.c')
endif

if with_platform_wayland
  v3dv_deps += [dep_wayland_client, dep_wl_protocols]
  v3dv_flags += '-DVK_USE_PLATFORM_WAYLAND_KHR'
  libv3dv_files += files('v3dv_wsi_wayland.c')
  libv3dv_files += [wayland_drm_client_protocol_h, wayland_drm_protocol_c]
endif

if system_has_kms_drm and not with_platform_android
 v3dv_flags += '-DVK_USE_PLATFORM_DISPLAY_KHR'
 libv3dv_files += files('v3dv_wsi_display.c')
endif

per_version_libs = []
foreach ver : v3d_versions
  per_version_libs += static_library(
    'v3dv-v' + ver,
    [files_per_version, v3d_xml_pack, v3dv_entrypoints[0]],
    include_directories : [
      inc_src, inc_include, inc_gallium, inc_gallium_aux, inc_broadcom,
      inc_compiler, inc_util, inc_vulkan_wsi,
    ],
    c_args : [v3dv_flags, '-DV3D_VERSION=' + ver],
    gnu_symbol_visibility : 'hidden',
    dependencies : [v3dv_deps],
)
endforeach

libvulkan_broadcom = shared_library(
  'vulkan_broadcom',
  [libv3dv_files, v3dv_entrypoints, sha1_h],
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_broadcom, inc_compiler, inc_util, inc_vulkan_wsi,
  ],
  link_with : [
    libbroadcom_cle,
    libbroadcom_v3d,
    libvulkan_wsi,
    per_version_libs,
  ],
  dependencies : v3dv_deps,
  c_args : v3dv_flags,
  link_args : ['-Wl,--build-id=sha1', ld_args_bsymbolic, ld_args_gc_sections],
  gnu_symbol_visibility : 'hidden',
  install : true,
)

if with_symbols_check
  test(
    'v3dv symbols check',
    symbols_check,
    args : [
      '--lib', libvulkan_broadcom,
      '--symbols-file', vulkan_icd_symbols,
      symbols_check_args,
    ],
    suite : ['broadcom'],
  )
endif

broadcom_icd = custom_target(
  'broadcom_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : 'broadcom_icd.@0@.json'.format(host_machine.cpu()),
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.0', '--xml', '@INPUT1@',
    '--lib-path', join_paths(get_option('prefix'), get_option('libdir'),
    		  	     'libvulkan_broadcom.so'),
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install : true,
)
