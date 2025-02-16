# Copyright 2017 Intel Corporation
# Copyright 2024 Collabora
# SPDX-License-Identifier: MIT


if get_option('precomp-compiler') == 'system'
  prog_panfrost_compile = find_program('panfrost_compile', native : true)
else
  prog_panfrost_compile = executable(
    'panfrost_compile',
    ['panfrost_compile.c'],
    link_with : [libpanfrost_bifrost],
    include_directories : [inc_include, inc_src],
    c_args : [c_msvc_compat_args, no_override_init_args],
    link_args : [ld_args_build_id],
    dependencies : [idep_vtn, idep_nir, idep_mesautil, libpanfrost_dep],
    # If we can run host binaries directly, just build panfrost_compile for the host.
    # Most commonly this happens when doing a cross compile from an x86_64 build
    # machine to an x86 host
    native : not meson.can_run_host_binaries(),
    install : get_option('install-precomp-compiler'),
  )
endif
