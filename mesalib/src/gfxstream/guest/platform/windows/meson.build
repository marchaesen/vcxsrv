# Copyright 2025 Mesa3D authors
# SPDX-License-Identifier: MIT

files_libplatform_virtgpu_windows = files(
  'WindowsVirtGpuDevice.cpp',
  'WindowsVirtGpuBlobMapping.cpp',
  'WindowsVirtGpuBlob.cpp',
  'WindowsSync.cpp',
)

libplatform_virtgpu_windows = static_library(
   'platform_virtgpu_windows',
   files_libplatform_virtgpu_windows,
   cpp_args: gfxstream_guest_args,
   include_directories: [inc_platform_virtgpu, inc_src, inc_include],
)
