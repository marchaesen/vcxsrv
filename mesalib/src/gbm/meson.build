# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

inc_gbm = include_directories('.', 'main', 'backends/dri')

files_gbm = files(
  'main/backend.c',
  'main/backend.h',
  'main/gbm.c',
  'main/gbm.h',
  'main/gbmint.h',
)
args_gbm = [
  '-DDEFAULT_BACKENDS_PATH="@0@"'.format(gbm_backends_path),
]
incs_gbm = [include_directories('main'), inc_loader, inc_gallium]

if with_dri2
  subdir('backends/dri')
endif

libgbm_name = 'gbm'

if with_platform_android and get_option('platform-sdk-version') >= 30
  libgbm_name = 'gbm_mesa'
endif

libgbm = shared_library(
  libgbm_name,
  files_gbm,
  include_directories : incs_gbm,
  c_args : [args_gbm],
  link_args : [ld_args_gc_sections],
  link_with : [libloader],
  dependencies : [dep_libdrm, idep_xmlconfig],
  gnu_symbol_visibility : 'hidden',
  version : '1.0.0',
  install : true,
)

if with_tests
  abi_check = executable('gbm_abi_check', 'main/gbm_abi_check.c')
  test('gbm-abi-check', abi_check, suite : ['gbm'])
endif

install_headers('main/gbm.h')

pkg.generate(
  name : 'gbm',
  filebase : 'gbm',
  description : 'Mesa gbm library',
  version : meson.project_version(),
  libraries : libgbm,
  libraries_private : gbm_priv_libs,
  variables : ['gbmbackendspath=' + gbm_backends_path],
)

if with_symbols_check
  test(
    'gbm-symbols-check',
    symbols_check,
    args : [
      '--lib', libgbm,
      '--symbols-file', files('gbm-symbols.txt'),
      symbols_check_args,
    ],
    suite : ['gbm'],
  )
endif
