# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

# TODO: support non-static targets
# Static targets are always enabled in autotools (unless you modify
# configure.ac)

gallium_xlib_ld_args = []
gallium_xlib_link_with = []
gallium_xlib_link_depends = []

if with_ld_version_script
  gallium_xlib_ld_args += ['-Wl,--version-script', join_paths(meson.current_source_dir(), 'libgl-xlib.sym')]
  gallium_xlib_link_depends += files('libgl-xlib.sym')
endif
if with_shared_glapi
  gallium_xlib_link_with += libglapi
endif

libgl = shared_library(
  'GL',
  files('xlib.c'),
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_gallium_winsys, inc_gallium_drivers,
    include_directories('../../frontends/glx/xlib'),
  ],
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_bsymbolic, ld_args_gc_sections, gallium_xlib_ld_args],
  link_depends : gallium_xlib_link_depends,
  link_whole : [libxlib, libglapi_static],
  link_with : [
    libgalliumvl_stub, libws_xlib,
    libgallium, libmesa, gallium_xlib_link_with,
  ],
  dependencies : [dep_x11, idep_mesautil, dep_thread, dep_clock, dep_unwind, driver_swrast, driver_virgl, driver_asahi],
  install : true,
  version : '1.5.0',
  darwin_versions: '4.0.0',
)
