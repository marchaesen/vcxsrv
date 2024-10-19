# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

# Mesa-local imports in the Python files must be declared here for correct
# dependency tracking.

vulkan_lite_runtime_files = files(
  'rmv/vk_rmv_common.c',
  'rmv/vk_rmv_exporter.c',
  'vk_acceleration_structure.c',
  'vk_blend.c',
  'vk_buffer.c',
  'vk_buffer_view.c',
  'vk_cmd_copy.c',
  'vk_cmd_enqueue.c',
  'vk_command_buffer.c',
  'vk_command_pool.c',
  'vk_debug_report.c',
  'vk_debug_utils.c',
  'vk_deferred_operation.c',
  'vk_descriptor_set_layout.c',
  'vk_descriptors.c',
  'vk_descriptor_update_template.c',
  'vk_device.c',
  'vk_device_generated_commands.c',
  'vk_device_memory.c',
  'vk_fence.c',
  'vk_framebuffer.c',
  'vk_graphics_state.c',
  'vk_image.c',
  'vk_log.c',
  'vk_meta_object_list.c',
  'vk_object.c',
  'vk_physical_device.c',
  'vk_pipeline_layout.c',
  'vk_query_pool.c',
  'vk_queue.c',
  'vk_render_pass.c',
  'vk_sampler.c',
  'vk_semaphore.c',
  'vk_standard_sample_locations.c',
  'vk_sync.c',
  'vk_sync_binary.c',
  'vk_sync_dummy.c',
  'vk_sync_timeline.c',
  'vk_synchronization.c',
  'vk_video.c',
  'vk_ycbcr_conversion.c',
)

vulkan_lite_runtime_deps = [
  vulkan_wsi_deps,
  idep_mesautil,
  idep_nir_headers,
  idep_vulkan_util,
]

if dep_libdrm.found()
  vulkan_lite_runtime_files += files('vk_drm_syncobj.c')
  vulkan_lite_runtime_deps += dep_libdrm
endif

if with_platform_android
  vulkan_lite_runtime_files += files('vk_android.c')
  vulkan_lite_runtime_deps += [dep_android, idep_u_gralloc]
endif

vk_common_entrypoints = custom_target(
  'vk_common_entrypoints',
  input : [vk_entrypoints_gen, vk_api_xml],
  output : ['vk_common_entrypoints.h', 'vk_common_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@', '--prefix', 'vk_common',
    '--beta', with_vulkan_beta.to_string()
  ],
  depend_files : vk_entrypoints_gen_depend_files,
)

vk_cmd_queue = custom_target(
  'vk_cmd_queue',
  input : [vk_cmd_queue_gen, vk_api_xml],
  output : ['vk_cmd_queue.c', 'vk_cmd_queue.h'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-c', '@OUTPUT0@', '--out-h', '@OUTPUT1@',
    '--beta', with_vulkan_beta.to_string()
  ],
  depend_files : vk_cmd_queue_gen_depend_files,
)

vk_cmd_enqueue_entrypoints = custom_target(
  'vk_cmd_enqueue_entrypoints',
  input : [vk_entrypoints_gen, vk_api_xml],
  output : ['vk_cmd_enqueue_entrypoints.h', 'vk_cmd_enqueue_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@',
    '--prefix', 'vk_cmd_enqueue', '--prefix', 'vk_cmd_enqueue_unless_primary',
    '--beta', with_vulkan_beta.to_string()
  ],
  depend_files : vk_entrypoints_gen_depend_files,
)

vk_dispatch_trampolines = custom_target(
  'vk_dispatch_trampolines',
  input : [vk_dispatch_trampolines_gen, vk_api_xml],
  output : ['vk_dispatch_trampolines.c', 'vk_dispatch_trampolines.h'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-c', '@OUTPUT0@', '--out-h', '@OUTPUT1@',
    '--beta', with_vulkan_beta.to_string()
  ],
  depend_files : vk_dispatch_trampolines_gen_depend_files,
)

vk_physical_device_features = custom_target(
  'vk_physical_device_features',
  input : [vk_physical_device_features_gen, vk_api_xml],
  output : ['vk_physical_device_features.c', 'vk_physical_device_features.h'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-c', '@OUTPUT0@', '--out-h', '@OUTPUT1@',
    '--beta', with_vulkan_beta.to_string()
  ],
  depend_files : vk_physical_device_features_gen_depend_files,
)

vk_physical_device_properties = custom_target(
  'vk_physical_device_properties',
  input : [vk_physical_device_properties_gen, vk_api_xml],
  output : ['vk_physical_device_properties.c', 'vk_physical_device_properties.h'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-c', '@OUTPUT0@', '--out-h', '@OUTPUT1@',
    '--beta', with_vulkan_beta.to_string()
  ],
  depend_files : vk_physical_device_properties_gen_depend_files,
)

vk_physical_device_spirv_caps = custom_target(
  'vk_physical_device_spirv_caps',
  input : [vk_physical_device_spirv_caps_gen, vk_api_xml],
  output : 'vk_physical_device_spirv_caps.c',
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-c', '@OUTPUT0@', '--beta', with_vulkan_beta.to_string()
  ],
  depend_files : vk_physical_device_spirv_caps_gen_depend_files,
)

vk_synchronization_helpers = custom_target(
  'vk_synchronization_helpers',
  input : [vk_synchronization_helpers_gen, vk_api_xml],
  output : 'vk_synchronization_helpers.c',
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-c', '@OUTPUT0@',
    '--beta', with_vulkan_beta.to_string()
  ],
  depend_files : vk_synchronization_helpers_gen_depend_files,
)

vk_format_info = custom_target(
  'vk_format_info',
  input : ['vk_format_info_gen.py', vk_api_xml],
  output : ['vk_format_info.c', 'vk_format_info.h'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-c', '@OUTPUT0@', '--out-h', '@OUTPUT1@'
  ],
)

vulkan_lite_runtime_files += [
  vk_cmd_enqueue_entrypoints,
  vk_cmd_queue,
  vk_common_entrypoints,
  vk_dispatch_trampolines,
  vk_format_info,
  vk_physical_device_features,
  vk_physical_device_properties,
  vk_physical_device_spirv_caps,
  vk_synchronization_helpers,
]

# as a runtime library dep to ensure header gen order
vulkan_lite_runtime_header_gen_deps = declare_dependency(
  sources : [
    spirv_info_h,
    vk_cmd_enqueue_entrypoints[0],
    vk_cmd_queue[1],
    vk_common_entrypoints[0],
    vk_dispatch_trampolines[1],
    vk_format_info[1],
    vk_physical_device_features[1],
    vk_physical_device_properties[1],
  ],
)

vulkan_lite_runtime_deps += vulkan_lite_runtime_header_gen_deps

libvulkan_lite_runtime = static_library(
  'vulkan_lite_runtime',
  vulkan_lite_runtime_files,
  include_directories : [inc_include, inc_src],
  dependencies : vulkan_lite_runtime_deps,
  c_args : c_msvc_compat_args,
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

libvulkan_lite_instance = static_library(
  'vulkan_lite_instance',
  ['vk_instance.c'],
  include_directories : [inc_include, inc_src],
  dependencies : vulkan_lite_runtime_deps,
  c_args : ['-DVK_LITE_RUNTIME_INSTANCE=1', c_msvc_compat_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

# The sources part is to ensure those generated headers used externally are
# indeed generated before being compiled with, as long as either one of below
# is included as a dependency:
# - idep_vulkan_lite_runtime_headers
# - idep_vulkan_lite_runtime
# - idep_vulkan_runtime_headers
# - idep_vulkan_runtime
idep_vulkan_lite_runtime_headers = declare_dependency(
  sources : [
    spirv_info_h,
    vk_cmd_enqueue_entrypoints[0],
    vk_cmd_queue[1],
    vk_common_entrypoints[0],
    vk_physical_device_features[1],
    vk_physical_device_properties[1],
  ],
  include_directories : include_directories('.'),
)

# This is likely a bug in the Meson VS backend, as MSVC with ninja works fine.
# See this discussion here:
# https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/10506
if get_option('backend').startswith('vs')
  idep_vulkan_lite_runtime = declare_dependency(
    link_with : [libvulkan_lite_runtime, libvulkan_lite_instance],
    dependencies : idep_vulkan_lite_runtime_headers
  )
else
  idep_vulkan_lite_runtime = declare_dependency(
    # Instruct users of this library to link with --whole-archive.  Otherwise,
    # our weak function overloads may not resolve properly.
    link_whole : [libvulkan_lite_runtime, libvulkan_lite_instance],
    dependencies : idep_vulkan_lite_runtime_headers
  )
endif

vulkan_runtime_files = files(
  'vk_meta.c',
  'vk_meta_blit_resolve.c',
  'vk_meta_clear.c',
  'vk_meta_copy_fill_update.c',
  'vk_meta_draw_rects.c',
  'vk_nir.c',
  'vk_nir_convert_ycbcr.c',
  'vk_pipeline.c',
  'vk_pipeline_cache.c',
  'vk_shader.c',
  'vk_shader_module.c',
  'vk_texcompress_etc2.c',
)

vulkan_runtime_deps = [
  vulkan_lite_runtime_deps,
  idep_nir,
  idep_vtn,
]

if prog_glslang.found()
  vulkan_runtime_files += files('vk_texcompress_astc.c')
  vulkan_runtime_files += custom_target(
    'astc_spv.h',
    input : astc_decoder_glsl_file,
    output : 'astc_spv.h',
    command : [
      prog_glslang, '-V', '-S', 'comp', '-x', '-o', '@OUTPUT@', '@INPUT@',
      glslang_quiet, glslang_depfile,
    ],
    depfile : 'astc_spv.h.d',
  )
endif

libvulkan_runtime = static_library(
  'vulkan_runtime',
  [vulkan_runtime_files],
  include_directories : [inc_include, inc_src],
  dependencies : vulkan_runtime_deps,
  c_args : c_msvc_compat_args,
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

libvulkan_instance = static_library(
  'vulkan_instance',
  ['vk_instance.c'],
  include_directories : [inc_include, inc_src],
  dependencies : vulkan_runtime_deps,
  c_args : ['-DVK_LITE_RUNTIME_INSTANCE=0', c_msvc_compat_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

if get_option('backend').startswith('vs')
  idep_vulkan_runtime_body = declare_dependency(
    link_with : [libvulkan_lite_runtime, libvulkan_runtime, libvulkan_instance],
  )
else
  idep_vulkan_runtime_body = declare_dependency(
    link_whole : [libvulkan_lite_runtime, libvulkan_runtime, libvulkan_instance],
  )
endif

idep_vulkan_runtime_headers = idep_vulkan_lite_runtime_headers

idep_vulkan_runtime = declare_dependency(
  dependencies : [
    idep_vulkan_runtime_headers,
    idep_vulkan_runtime_body,
  ]
)
