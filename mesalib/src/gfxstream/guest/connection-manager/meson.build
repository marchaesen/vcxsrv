# Copyright 2024 Android Open Source Project
# SPDX-License-Identifier: MIT

inc_connection_manager = include_directories('.')

files_libconnection_manager = files(
  'GfxStreamConnectionManager.cpp',
  'GfxStreamConnection.cpp',
  'QemuPipeStreamStub.cpp',
  'VirtioGpuPipeStream.cpp',
)

libconnection_manager = static_library(
   'connection_manager',
   files_libconnection_manager,
   cpp_args: gfxstream_guest_args,
   include_directories: [inc_connection_manager, inc_guest_iostream,
                         inc_guest_iostream, inc_goldfish_address_space,
                         inc_platform_virtgpu, inc_src],
   link_with: [lib_guest_iostream, lib_goldfish_address_space,
               libplatform_virtgpu],
   dependencies: drm_dep
)
