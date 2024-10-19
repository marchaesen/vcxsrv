# Copyright 2022 Android Open Source Project
# SPDX-License-Identifier: MIT

inc_gfxstream_vk_stubs = include_directories('.')
files_libgfxstream_vk_stubs = files(
  'GfxStreamRenderControl.cpp',
)

libgfxstream_vk_stubs = static_library(
   'gfxstream_vk_stubs',
   files_libgfxstream_vk_stubs,
   cpp_args: gfxstream_guest_args,
   include_directories: [inc_connection_manager, inc_platform_virtgpu,
                         inc_guest_iostream],
)
