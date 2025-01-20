# Copyright 2022 Google LLC
# SPDX-License-Identifier: MIT

inc_platform_virtgpu = include_directories('include')

subdir('kumquat')

libplatform_virtgpu = [libplatform_virtgpu_kumquat]

if system_has_kms_drm
  subdir('linux')
  libplatform_virtgpu += libplatform_virtgpu_linux
elif host_machine.system() == 'windows'
  subdir('windows')
  libplatform_virtgpu += libplatform_virtgpu_windows
endif

files_libplatform_virtgpu = files(
  'VirtGpu.cpp',
)

libplatform_virtgpu = static_library(
   'platform_virtgpu',
   files_libplatform_virtgpu,
   cpp_args: gfxstream_guest_args,
   include_directories: [inc_platform_virtgpu, inc_src],
   link_with: libplatform_virtgpu
)
