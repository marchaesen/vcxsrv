# Copyright 2022 Android Open Source Project
# SPDX-License-Identifier: MIT

inc_goldfish_address_space = include_directories('include')

files_lib_goldfish_address_space = files(
  'AddressSpaceStream.cpp',
  'GoldfishAddressSpaceStream.cpp',
  'VirtioGpuAddressSpaceStream.cpp',
  'goldfish_address_space.cpp',
)

lib_goldfish_address_space = static_library(
   'goldfish_address_space',
   files_lib_goldfish_address_space,
   cpp_args: gfxstream_guest_args,
   include_directories: [inc_goldfish_address_space,
                         inc_guest_iostream,
                         inc_platform_virtgpu,
                         inc_include,
                         inc_src],
   link_with: [libplatform_virtgpu],
   dependencies: drm_dep
)
