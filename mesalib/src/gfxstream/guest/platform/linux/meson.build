# Copyright 2022 Google
# SPDX-License-Identifier: MIT

files_libplatform_virtgpu_linux = files(
  'LinuxVirtGpuDevice.cpp',
  'LinuxVirtGpuBlobMapping.cpp',
  'LinuxVirtGpuBlob.cpp',
  'LinuxSync.cpp',
)

libplatform_virtgpu_linux = static_library(
   'platform_virtgpu_linux',
   files_libplatform_virtgpu_linux,
   cpp_args: gfxstream_guest_args,
   include_directories: [inc_platform_virtgpu, inc_src, inc_include],
   dependencies: drm_dep,
)
