# Copyright 2022 Android Open Source Project
# SPDX-License-Identifier: MIT

inc_goldfish_address_space = include_directories('include')

files_lib_goldfish_address_space = files(
  'AddressSpaceStream.cpp',
  'VirtioGpuAddressSpaceStream.cpp',
)

if host_machine.system() == 'android'
  files_lib_goldfish_address_space += files('GoldfishAddressSpaceStream.cpp')
  files_lib_goldfish_address_space += files('goldfish_address_space.cpp')
endif

lib_goldfish_address_space = static_library(
   'goldfish_address_space',
   files_lib_goldfish_address_space,
   cpp_args: gfxstream_guest_args,
   include_directories: [inc_goldfish_address_space,
                         inc_guest_iostream,
                         inc_platform_virtgpu,
                         inc_include,
                         inc_aemu,
                         inc_src],
   link_with: [libplatform_virtgpu],
   dependencies: dep_libdrm
)
