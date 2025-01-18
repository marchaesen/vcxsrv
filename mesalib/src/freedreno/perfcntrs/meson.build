# Copyright © 2018 Rob Clark
# SPDX-License-Identifier: MIT

libfreedreno_perfcntrs_files = files(
  'fd2_perfcntr.c',
  'fd5_perfcntr.c',
  'fd6_perfcntr.c',
  'fd7_perfcntr.c',
  'freedreno_dt.c',
  'freedreno_dt.h',
  'freedreno_perfcntr.c',
  'freedreno_perfcntr.h',
)

libfreedreno_perfcntrs = static_library(
  'freedreno_perfcntrs',
  [libfreedreno_perfcntrs_files, freedreno_xml_header_files],
  include_directories : [inc_freedreno, inc_include, inc_src],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  link_with : [libfreedreno_common],
  dependencies : idep_nir_headers,
  build_by_default : false,
)

dep_libconfig = dependency('libconfig', required : false)
dep_curses = dependency('curses', required : false)

if dep_libconfig.found() and dep_curses.found()
  fdperf = executable(
    'fdperf',
    ['fdperf.c', freedreno_xml_header_files],
    include_directories : [
      inc_freedreno,
      inc_include,
      inc_src,
    ],
    link_with : [
      libfreedreno_common,
      libfreedreno_drm,
      libfreedreno_perfcntrs,
    ],
    dependencies : [
      dep_libconfig,
      dep_libdrm,
      dep_curses,
      idep_mesautil,
    ],
    build_by_default : with_tools.contains('freedreno'),
    install : with_tools.contains('freedreno'),
  )
endif
