# Copyright © 2016 Advanced Micro Devices, Inc.
# SPDX-License-Identifier: MIT

project('glsl_tests', 'c')

dep_epoxy = dependency('epoxy')
dep_gbm = dependency('gbm')

executable(
  'amdgcn_glslc',
  files('amdgcn_glslc.c'),
  dependencies: [dep_epoxy, dep_gbm]
)
