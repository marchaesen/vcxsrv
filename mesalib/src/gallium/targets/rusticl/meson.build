# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

opencl_version = '1'

rusticl_ld_args = []
rusticl_ld_depends = []

if with_ld_version_script
  rusticl_ld_args += ['-Wl,--version-script', join_paths(meson.current_source_dir(), 'rusticl.sym')]
  rusticl_ld_depends += files('rusticl.sym')
endif

librusticl = shared_library(
  'RusticlOpenCL',
  'rusticl_target.c',
  gnu_symbol_visibility : 'hidden',
  include_directories : [
    inc_include,
    inc_src,
    inc_gallium,
    inc_gallium_aux,
    inc_gallium_drivers,
    inc_gallium_winsys,
  ],
  link_whole : librusticl,
  link_with : [
    libgalliumvl_stub,
    libpipe_loader_static,
    libswdri,
    libswkmsdri,
    libwsw,
    libws_null,
  ],
  dependencies : [
    driver_asahi,
    driver_freedreno,
    driver_iris,
    driver_nouveau,
    driver_r600,
    driver_panfrost,
    driver_radeonsi,
    driver_swrast,
    driver_v3d,
    driver_zink,
    idep_nir,
    idep_vtn,
    idep_mesautil,
  ],
  link_args : [
    ld_args_build_id,
    ld_args_gc_sections,
    ld_args_bsymbolic,
    rusticl_ld_args,
  ],
  link_depends : rusticl_ld_depends,
  version : '@0@.0.0'.format(opencl_version),
  install : true,
)

_config = configuration_data()
_config.set('OPENCL_VERSION', opencl_version)
configure_file(
  configuration : _config,
  input : 'rusticl.icd.in',
  output : 'rusticl.icd',
  install : true,
  install_tag : 'runtime',
  install_dir : join_paths(get_option('sysconfdir'), 'OpenCL', 'vendors'),
)

# .so is hardcoded in the icd as well
devenv.prepend(
  'OCL_ICD_VENDORS',
  meson.current_build_dir() / ''
)
