
lvp_entrypoints = custom_target(
  'lvp_entrypoints.[ch]',
  input : ['lvp_entrypoints_gen.py', vk_api_xml],
  output : ['lvp_entrypoints.h', 'lvp_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--outdir',
    meson.current_build_dir()
  ],
  depend_files : files('lvp_extensions.py'),
)

lvp_extensions_c = custom_target(
  'lvp_extensions.c',
  input : ['lvp_extensions.py', vk_api_xml],
  output : ['lvp_extensions.c', 'lvp_extensions.h'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--out-c', '@OUTPUT0@',
    '--out-h', '@OUTPUT1@'
  ],
)

liblvp_files = files(
    'lvp_device.c',
    'lvp_cmd_buffer.c',
    'lvp_descriptor_set.c',
    'lvp_execute.c',
    'lvp_util.c',
    'lvp_image.c',
    'lvp_formats.c',
    'lvp_lower_vulkan_resource.c',
    'lvp_lower_vulkan_resource.h',
    'lvp_lower_input_attachments.c',
    'lvp_pass.c',
    'lvp_pipeline.c',
    'lvp_pipeline_cache.c',
    'lvp_query.c',
    'lvp_wsi.c')

lvp_deps = []
lvp_flags = []

if with_platform_x11
  lvp_deps += dep_xcb_dri3
  lvp_flags += [
    '-DVK_USE_PLATFORM_XCB_KHR',
    '-DVK_USE_PLATFORM_XLIB_KHR',
  ]
  liblvp_files += files('lvp_wsi_x11.c')
endif

if with_platform_wayland
  lvp_deps += dep_wayland_client
  lvp_flags += '-DVK_USE_PLATFORM_WAYLAND_KHR'
  liblvp_files += files('lvp_wsi_wayland.c')
endif

liblavapipe_st = static_library(
  'lavapipe_st',
  [liblvp_files, lvp_entrypoints, lvp_extensions_c ],
  link_with : [ libvulkan_wsi ],
  c_args : [ lvp_flags ],
  gnu_symbol_visibility : 'hidden',
  include_directories : [ inc_include, inc_src, inc_util, inc_gallium, inc_compiler, inc_gallium_aux, inc_vulkan_wsi ],
  dependencies : [ idep_nir, idep_mesautil, idep_vulkan_util ]
)
