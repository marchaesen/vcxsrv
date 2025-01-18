# Copyright © 2017-2018 Intel Corporation
# SPDX-License-Identifier: MIT

# TODO: support non-static targets
# Static targets are always enabled in autotools (unless you modify
# configure.ac)

vdpau_link_args = []
vdpau_link_depends = []
vdpau_drivers = []

vdpau_sym = configure_file(input : 'vdpau.sym.in', output : 'vdpau.sym', configuration : sym_config)

if with_ld_version_script
  vdpau_link_args += ['-Wl,--version-script', join_paths(meson.current_build_dir(), 'vdpau.sym')]
  vdpau_link_depends += vdpau_sym
endif
if with_ld_dynamic_list
  vdpau_link_args += ['-Wl,--dynamic-list', join_paths(meson.current_source_dir(), '../dri.dyn')]
  vdpau_link_depends += files('../dri.dyn')
endif

link_with_libvdpau_gallium = [
  libgalliumvlwinsys, libgalliumvl, libgallium,
  libpipe_loader_static, libws_null, libwsw, libswdri, libswkmsdri,
]

if with_glx == 'xlib'
  link_with_libvdpau_gallium += [libws_xlib]
endif

libvdpau_gallium = shared_library(
  'vdpau_gallium',
  'vdpau_target.c',
  gnu_symbol_visibility : 'hidden',
  link_args : [vdpau_link_args, ld_args_gc_sections],
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_util, inc_gallium_winsys, inc_gallium_drivers,
  ],
  link_whole : [libvdpau_st],
  link_with : link_with_libvdpau_gallium,
  dependencies : [
    idep_mesautil,
    driver_r600, driver_radeonsi, driver_nouveau, driver_d3d12, driver_virgl,
  ],
  link_depends : vdpau_link_depends,
  soversion : '@0@.@1@.0'.format(VDPAU_MAJOR, VDPAU_MINOR),
  install : true,
  install_dir : vdpau_drivers_path,
  name_suffix : 'so',
)
foreach d : [[with_gallium_r600, 'r600'],
             [with_gallium_radeonsi, 'radeonsi'],
             [with_gallium_nouveau, 'nouveau'],
             [with_gallium_virgl, 'virtio_gpu'],
             [with_gallium_d3d12_video, 'd3d12']]
  if d[0]
    vdpau_drivers += 'libvdpau_@0@.so.@1@.@2@.0'.format(d[1], VDPAU_MAJOR, VDPAU_MINOR)
  endif
endforeach

if vdpau_drivers.length() > 0
  meson.add_install_script(
    install_megadrivers_py.full_path(),
    libvdpau_gallium.full_path(),
    vdpau_drivers_path,
    vdpau_drivers,
    install_tag : 'runtime',
  )
endif
