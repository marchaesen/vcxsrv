# Copyright 2024 Google LLC
# SPDX-License-Identifier: MIT

inc_aemu = include_directories('include')

files_libaemu = files(
  'ring_buffer.cpp',
  'Stream.cpp',
)

libaemu = static_library(
   'aemu',
   files_libaemu,
   include_directories: [inc_aemu],
)
