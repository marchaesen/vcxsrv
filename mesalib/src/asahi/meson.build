# Copyright 2018 Rob Clark
# Copyright 2019 Collabora, Ltd.
# SPDX-License-Identifier: MIT

inc_asahi = include_directories([
   '.', 'layout', 'lib', 'genxml', 'compiler'
])

if with_gallium_asahi or with_asahi_vk
   subdir('genxml')
   subdir('libagx')
   subdir('layout')
   subdir('compiler')
   subdir('clc')
   subdir('lib')
endif

if with_asahi_vk
   subdir('vulkan')
endif

if with_tools.contains('drm-shim')
  subdir('drm-shim')
endif
