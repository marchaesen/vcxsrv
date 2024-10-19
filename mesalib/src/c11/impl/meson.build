# Copyright © 2022 Yonggang Luo
# SPDX-License-Identifier: MIT

inc_util_c11 = [include_directories('../..')]

files_mesa_util_c11 = files(
  'time.c',
)

if with_c11_threads
  # no need implement c11 threads
elif host_machine.system() == 'windows'
  files_mesa_util_c11 += 'threads_win32.c'
  files_mesa_util_c11 += 'threads_win32_tls_callback.cpp'
else
  files_mesa_util_c11 += 'threads_posix.c'
endif

_libmesa_util_c11 = static_library(
  'mesa_util_c11',
  [files_mesa_util_c11],
  include_directories : inc_util_c11,
  c_args : [c_msvc_compat_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false
)

idep_mesautilc11 = declare_dependency(
  link_with : _libmesa_util_c11,
  include_directories : inc_util_c11,
  dependencies : [dep_clock, dep_thread, dep_atomic, dep_m, dep_valgrind],
)
