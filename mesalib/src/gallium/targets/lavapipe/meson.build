

libvulkan_lvp = shared_library(
  'vulkan_lvp',
  [ 'lavapipe_target.c' ],
  vs_module_defs : vulkan_api_def,
  include_directories : [ inc_src, inc_util, inc_include, inc_gallium, inc_gallium_aux, inc_gallium_winsys, inc_gallium_drivers ],
  link_whole : [ liblavapipe_st ],
  link_with : [libpipe_loader_static, libgallium, libwsw, libswdri, libws_null, libswkmsdri ],
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_bsymbolic, ld_args_gc_sections],
  # On Apple systems, "weak" is actually "weak_import", which is slightly different.
  # The linker still expects a definition for weak symbols at build time, just not at runtime.
  # We have to forcibly ignore undefined symbols, which unfortunately applies to all symbols.
  override_options : ['b_lundef=@0@'.format(host_machine.system() == 'darwin' ? 'false' : get_option('b_lundef').to_string())],
  dependencies : [driver_llvmpipe, idep_mesautil],
  name_prefix : host_machine.system() == 'windows' ? '' : [],
  install : true,
)

if host_machine.system() == 'windows'
  icd_lib_path = import('fs').relative_to(get_option('bindir'), with_vulkan_icd_dir)
  icd_file_name = 'vulkan_lvp.dll'
else
  icd_lib_path = join_paths(get_option('prefix'), get_option('libdir'))
  icd_file_name = 'libvulkan_lvp.@0@'.format(host_machine.system() == 'darwin' ? 'dylib' : 'so')
endif

icd_command = [
  prog_python, '@INPUT0@',
  '--api-version', '1.3', '--xml', '@INPUT1@',
  '--lib-path', join_paths(icd_lib_path, icd_file_name),
  '--out', '@OUTPUT@',
]
if host_machine.system() == 'windows'
  icd_command += '--use-backslash'
endif

lvp_icd = custom_target(
  'lvp_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : 'lvp_icd.@0@.json'.format(host_machine.cpu()),
  command : icd_command,
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install_tag : 'runtime',
  install : true,
)

_dev_icdname = 'lvp_devenv_icd.@0@.json'.format(host_machine.cpu())
_dev_icd = custom_target(
  'lvp_devenv_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : _dev_icdname,
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.1', '--xml', '@INPUT1@',
    '--lib-path', meson.current_build_dir() / icd_file_name,
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
)

devenv.append('VK_DRIVER_FILES', _dev_icd.full_path())
# Deprecated: replaced by VK_DRIVER_FILES above
devenv.append('VK_ICD_FILENAMES', _dev_icd.full_path())
