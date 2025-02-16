# Copyright © 2017 Dylan Baker
# Copyright © 2018 Intel Corporation
# SPDX-License-Identifier: MIT

# TODO: support non-static targets
# Static targets are always enabled in autotools (unless you modify
# configure.ac)

gallium_dri_c_args = []
gallium_dri_ld_args = [cc.get_supported_link_arguments('-Wl,--default-symver')]
gallium_dri_link_depends = []
gallium_dri_drivers = []
gallium_dri_link_with = []
gallium_dri_link_whole = []

if with_gallium_va or with_gallium_vdpau
  gallium_dri_link_with += [libgalliumvlwinsys]
  if with_gallium_va
    gallium_dri_link_whole += [libva_st]
  endif
  if with_gallium_vdpau
    gallium_dri_link_whole += [libvdpau_st]
  endif
endif

dri_sym = configure_file(input : 'dri.sym.in', output : 'dri.sym', configuration : sym_config)

if with_ld_version_script
  gallium_dri_ld_args += ['-Wl,--version-script', join_paths(meson.current_build_dir(), 'dri.sym')]
  gallium_dri_link_depends += dri_sym
endif
if with_ld_dynamic_list
  gallium_dri_ld_args += ['-Wl,--dynamic-list', join_paths(meson.current_source_dir(), '../dri.dyn')]
  gallium_dri_link_depends += files('../dri.dyn')
endif

if get_option('unversion-libgallium') or with_platform_android
  libgallium_name = 'gallium_dri'
else
  libgallium_name = 'gallium-@0@'.format(meson.project_version())
endif

libgallium_dri = shared_library(
  libgallium_name,
  files('dri_target.c'),
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_util, inc_gallium_drivers,
    inc_gallium_winsys, include_directories('../../frontends/dri'),
  ],
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_build_id, ld_args_gc_sections, gallium_dri_ld_args],
  link_depends : gallium_dri_link_depends,
  link_with : [
    libmesa, libgalliumvl,
    libgallium, libglapi, libpipe_loader_static, libws_null, libwsw, libswdri,
    libswkmsdri, gallium_dri_link_with
  ],
  link_whole : [libdri, gallium_dri_link_whole],
  dependencies : [
    dep_libdrm, dep_llvm, dep_thread, idep_xmlconfig, idep_mesautil,
    driver_swrast, driver_r300, driver_r600, driver_radeonsi, driver_nouveau,
    driver_kmsro, driver_v3d, driver_vc4, driver_freedreno, driver_etnaviv,
    driver_tegra, driver_i915, driver_svga, driver_virgl,
    driver_panfrost, driver_iris, driver_lima, driver_zink, driver_d3d12,
    driver_asahi, driver_crocus
  ],
  install : true,
  name_suffix : libname_suffix,
)

shared_glapi_lib = libgallium_dri

if with_gallium_va or with_gallium_vdpau
  va_drivers = []
  vdpau_drivers = []
  foreach d : [[with_gallium_r600, 'r600'],
              [with_gallium_radeonsi, 'radeonsi'],
              [with_gallium_nouveau, 'nouveau'],
              [with_gallium_virgl, 'virtio_gpu'],
              [with_gallium_d3d12_video, 'd3d12']]
    if d[0]
      if with_gallium_va
        va_drivers += '@0@_drv_video.so'.format(d[1])
      endif
      if with_gallium_vdpau
        vdpau_drivers += 'libvdpau_@0@.so.@1@.@2@.0'.format(d[1], VDPAU_MAJOR, VDPAU_MINOR)
      endif
    endif
  endforeach

  if va_drivers.length() > 0
    meson.add_install_script(
      install_megadrivers,
      libgallium_dri.full_path(),
      va_drivers_path,
      va_drivers,
      '--megadriver-libdir', get_option('libdir'),
      install_tag : 'runtime',
    )
  endif

  if vdpau_drivers.length() > 0
    meson.add_install_script(
      install_megadrivers,
      libgallium_dri.full_path(),
      vdpau_drivers_path,
      vdpau_drivers,
      '--megadriver-libdir', get_option('libdir'),
      install_tag : 'runtime',
    )
  endif
endif
