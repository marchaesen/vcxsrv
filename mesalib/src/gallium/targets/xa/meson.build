# Copyright © 2017-2018 Intel Corporation
# SPDX-License-Identifier: MIT

# TODO: support non-static targets
# Static targets are always enabled in autotools (unless you modify
# configure.ac)

xa_link_args = []
xa_link_depends = []

if with_ld_version_script
  xa_link_args += ['-Wl,--version-script', join_paths(meson.current_source_dir(), 'xa.sym')]
  xa_link_depends += files('xa.sym')
endif

_xa_version = '.'.join(xa_version)

libxatracker = shared_library(
  'xatracker',
  'xa_target.c',
  gnu_symbol_visibility : 'hidden',
  link_args : [xa_link_args, ld_args_gc_sections],
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_util, inc_gallium_winsys, inc_gallium_drivers,
  ],
  link_whole : [libxa_st],
  link_with : [
    libgalliumvl_stub, libgallium, libpipe_loader_static,
    libws_null, libwsw, libswdri, libswkmsdri,
  ],
  link_depends : xa_link_depends,
  dependencies : [
    idep_mesautil,
    driver_nouveau, driver_i915, driver_svga, driver_freedreno,
  ],
  version : _xa_version,
  install : true,
)

pkg.generate(
  name : 'xatracker',
  description : 'Xorg gallium3D acceleration library',
  version : _xa_version,
  libraries : libxatracker,
)
