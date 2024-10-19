# Copyright © 2019 Purism SPC
# SPDX-License-Identifier: MIT

inc_etnaviv = include_directories(['.'])

subdir('common')
subdir('hwdb')
subdir('drm')
subdir('isa')

if with_tools.contains('drm-shim')
  subdir('drm-shim')
endif
