# Copyright © 2024 Collabora
# SPDX-License-Identifier: MIT


libpan_shader_files = files(
  'query_pool.cl',
)

# We need to set -fmacro-prefix-map properly for reproducability.
fs = import('fs')
relative_dir = fs.relative_to(meson.global_source_root(), meson.global_build_root()) + '/'

idep_libpan_per_arch = {}

foreach ver : ['4', '5', '6', '7', '9', '10']
  libpan_spv = custom_target(
    'libpan_v' + ver + '.spv',
    input : libpan_shader_files,
    output : 'libpan_v' + ver + '.spv',
    command : [
        prog_mesa_clc, '-o', '@OUTPUT@',  '--depfile', '@DEPFILE@',
        libpan_shader_files, '--',
        '-DPAN_ARCH=@0@'.format(ver),
        '-I' + join_paths(meson.current_source_dir(), '.'),
        '-I' + join_paths(meson.current_source_dir(), '../../'),
        '-I' + join_paths(meson.current_source_dir(), '../lib/'),
        '-I' + join_paths(meson.current_build_dir(), '../lib/'),
        '-fmacro-prefix-map=@0@='.format(relative_dir),
        ],
    env: ['MESA_SHADER_CACHE_DISABLE=true'],
    depends : [pan_packers],
    depfile : 'libpan_shaders_v' + ver + '.h.d',
  )

  libpan_nir_cur_ver = custom_target(
    'libpan_v' + ver,
    input : libpan_spv,
    output : ['libpan_v' + ver + '.cpp', 'libpan_v' + ver + '.h'],
    command : [prog_vtn_bindgen2, libpan_spv, '@OUTPUT0@', '@OUTPUT1@'],
  )

  libpan_shaders_cur_ver = custom_target(
    'libpan_shaders_v' + ver,
    input : libpan_spv,
    output : ['libpan_shaders_v' + ver + '.h', 'libpan_shaders_v' + ver + '.c'],
    command : [prog_panfrost_compile, 'libpan_shaders', ver, '@INPUT0@', '@OUTPUT0@', '@OUTPUT1@'],
    env: ['MESA_SHADER_CACHE_DISABLE=true'],
  )

  idep_libpan_cur_ver = declare_dependency(
    sources: [libpan_shaders_cur_ver, libpan_nir_cur_ver],
    include_directories : include_directories('.'),
  )
  idep_libpan_per_arch += {ver: idep_libpan_cur_ver}
endforeach

