# SPDX-License-Identifier: MIT
# Copyright © 2023 Intel Corporation

prog_sed = find_program('sed')

foreach man : ['Xau', 'XauDisposeAuth', 'XauFileName', 'XauGetAuthByAddr',
               'XauGetBestAuthByAddr', 'XauLockAuth', 'XauReadAuth', 'XauUnlockAuth',
               'XauWriteAuth']
  custom_target(
    f'@man@.man',
    input : f'@man@.man',
    output : f'@man@.3',
    command : [
      prog_sed,
      '-e', 's/__xorgversion__/"libXau @0@" "X Version 11"/'.format(meson.project_version()),
      '-e', 's/__libmansuffix__/3/',
      '@INPUT@',
    ],
    capture : true,
    install : true,
    install_dir : get_option('prefix') / get_option('mandir') / 'man3',
  )
endforeach
