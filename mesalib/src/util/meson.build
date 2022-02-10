# Copyright © 2017 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

inc_util = include_directories('.')

subdir('format')

files_mesa_util = files(
  'anon_file.h',
  'anon_file.c',
  'bigmath.h',
  'bitscan.c',
  'bitscan.h',
  'bitset.h',
  'blob.c',
  'blob.h',
  'build_id.c',
  'build_id.h',
  'cnd_monotonic.h',
  'compiler.h',
  'compress.c',
  'compress.h',
  'crc32.c',
  'crc32.h',
  'dag.c',
  'debug.c',
  'debug.h',
  'disk_cache.c',
  'disk_cache.h',
  'disk_cache_os.c',
  'disk_cache_os.h',
  'double.c',
  'double.h',
  'enum_operators.h',
  'fast_idiv_by_const.c',
  'fast_idiv_by_const.h',
  'format_r11g11b10f.h',
  'format_rgb9e5.h',
  'format_srgb.h',
  'fossilize_db.c',
  'fossilize_db.h',
  'futex.h',
  'half_float.c',
  'half_float.h',
  'hash_table.c',
  'hash_table.h',
  'u_idalloc.c',
  'u_idalloc.h',
  'list.h',
  'log.c',
  'macros.h',
  'memstream.c',
  'memstream.h',
  'mesa-sha1.c',
  'mesa-sha1.h',
  'os_time.c',
  'os_time.h',
  'os_file.c',
  'os_memory_fd.c',
  'os_misc.c',
  'os_misc.h',
  'os_socket.c',
  'os_socket.h',
  'ptralloc.h',
  'perf/u_trace.h',
  'perf/u_trace.c',
  'perf/u_trace_priv.h',
  'u_process.c',
  'u_process.h',
  'u_qsort.cpp',
  'u_qsort.h',
  'rwlock.h',
  'sha1/sha1.c',
  'sha1/sha1.h',
  'ralloc.c',
  'ralloc.h',
  'rand_xor.c',
  'rand_xor.h',
  'rb_tree.c',
  'rb_tree.h',
  'register_allocate.c',
  'register_allocate.h',
  'rgtc.c',
  'rgtc.h',
  'rounding.h',
  'set.c',
  'set.h',
  'simple_list.h',
  'simple_mtx.h',
  'slab.c',
  'slab.h',
  'softfloat.c',
  'softfloat.h',
  'sparse_array.c',
  'sparse_array.h',
  'string_buffer.c',
  'string_buffer.h',
  'strndup.h',
  'strtod.c',
  'strtod.h',
  'texcompress_rgtc_tmp.h',
  'timespec.h',
  'u_atomic.c',
  'u_atomic.h',
  'u_debug_describe.c',
  'u_debug_describe.h',
  'u_debug_refcnt.c',
  'u_debug_refcnt.h',
  'u_dynarray.h',
  'u_endian.h',
  'u_fifo.h',
  'u_hash_table.c',
  'u_hash_table.h',
  'u_queue.c',
  'u_queue.h',
  'u_string.h',
  'u_thread.h',
  'u_vector.c',
  'u_vector.h',
  'u_math.c',
  'u_math.h',
  'u_memset.h',
  'u_mm.c',
  'u_mm.h',
  'u_debug.c',
  'u_debug.h',
  'u_debug_memory.c',
  'u_cpu_detect.c',
  'u_cpu_detect.h',
  'u_printf.cpp',
  'u_printf.h',
  'vl_vlc.h',
  'vl_rbsp.h',
  'vma.c',
  'vma.h',
  'xxhash.h',
  'indices/u_indices.h',
  'indices/u_indices_priv.h',
  'indices/u_primconvert.c',
  'indices/u_primconvert.h',
)

files_drirc = files('00-mesa-defaults.conf')

install_data(files_drirc, install_dir : join_paths(get_option('datadir'), 'drirc.d'))

if with_tests
  prog_xmllint = find_program('xmllint', required : false, native : true)
  if prog_xmllint.found()
    test(
      'drirc xml validation',
      prog_xmllint,
      args : ['--noout', '--valid', files_drirc],
      suite : ['util'],
    )
  endif
endif

files_xmlconfig = files(
  'xmlconfig.c',
  'xmlconfig.h',
)

files_xmlconfig += custom_target(
  'driconf_static.h',
  input: ['driconf_static.py', '00-mesa-defaults.conf'],
  output: 'driconf_static.h',
  command: [
    prog_python, '@INPUT0@', '@INPUT1@', '@OUTPUT@'
  ],
)

format_srgb = custom_target(
  'format_srgb',
  input : ['format_srgb.py'],
  output : 'format_srgb.c',
  command : [prog_python, '@INPUT0@'],
  capture : true,
)

deps_for_libmesa_util = [
  dep_zlib,
  dep_clock,
  dep_thread,
  dep_atomic,
  dep_m,
  dep_valgrind,
  dep_zstd,
  dep_dl,
  dep_unwind,
]

if with_platform_android
  deps_for_libmesa_util += dep_android
  files_debug_stack = files('u_debug_stack_android.cpp')
 else
  files_debug_stack = files(
    'u_debug_stack.c',
    'u_debug_symbol.c',
    'u_debug_symbol.h',
  )
endif

if with_platform_haiku
  deps_for_libmesa_util += dep_network
endif

if with_perfetto
  files_mesa_util += files(
    'u_perfetto.cc',
    'u_perfetto.h',
  )
  deps_for_libmesa_util += dep_perfetto
endif

u_trace_py = files('perf/u_trace.py')

u_indices_gen_c = custom_target(
  'u_indices_gen.c',
  input : 'indices/u_indices_gen.py',
  output : 'u_indices_gen.c',
  command : [prog_python, '@INPUT@'],
  capture : true,
)

u_unfilled_gen_c = custom_target(
  'u_unfilled_gen.c',
  input : 'indices/u_unfilled_gen.py',
  output : 'u_unfilled_gen.c',
  command : [prog_python, '@INPUT@'],
  capture : true,
)

_libmesa_util = static_library(
  'mesa_util',
  [files_mesa_util, files_debug_stack, format_srgb, u_indices_gen_c, u_unfilled_gen_c],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
  dependencies : deps_for_libmesa_util,
  link_with: libmesa_format,
  c_args : [c_msvc_compat_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false
)

idep_mesautil = declare_dependency(
  link_with : _libmesa_util,
  include_directories : [inc_util, inc_gallium],
  dependencies : [dep_zlib, dep_clock, dep_thread, dep_atomic, dep_m, dep_valgrind],
)

xmlconfig_deps = []
if not (with_platform_android or with_platform_windows)
  xmlconfig_deps += dep_expat
endif
xmlconfig_deps += dep_regex

_libxmlconfig = static_library(
  'xmlconfig',
  files_xmlconfig,
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
  dependencies : [idep_mesautil, dep_m, xmlconfig_deps],
  c_args : [
    c_msvc_compat_args,
    '-DSYSCONFDIR="@0@"'.format(
      join_paths(get_option('prefix'), get_option('sysconfdir'))
    ),
    '-DDATADIR="@0@"'.format(
      join_paths(get_option('prefix'), get_option('datadir'))
    ),
  ],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

idep_xmlconfig = declare_dependency(
  dependencies : xmlconfig_deps,
  link_with : _libxmlconfig,
)

files_xxd = files('xxd.py')

if with_tests
  # DRI_CONF macros use designated initializers (required for union
  # initializaiton), so we need c++2a since gtest forces us to use c++
  if host_machine.system() != 'windows' and cpp.has_argument('-std=c++2a')
    test('xmlconfig',
      executable('xmlconfig_test',
        files('tests/xmlconfig.cpp'),
        include_directories : [inc_include, inc_src],
        dependencies : [idep_mesautil, idep_xmlconfig, idep_gtest],
        override_options : ['cpp_std=c++2a'],
        cpp_args: ['-Wno-write-strings']
      ),
      suite : ['util'],
      env: ['HOME=' + join_paths(meson.current_source_dir(),
                                 'tests', 'drirc_home'),
            'DRIRC_CONFIGDIR=' + join_paths(meson.current_source_dir(),
                                            'tests', 'drirc_configdir')],
      protocol : gtest_test_protocol,
    )
  endif

  files_util_tests = files(
    'tests/bitset_test.cpp',
    'tests/blob_test.cpp',
    'tests/dag_test.cpp',
    'tests/fast_idiv_by_const_test.cpp',
    'tests/fast_urem_by_const_test.cpp',
    'tests/int_min_max.cpp',
    'tests/rb_tree_test.cpp',
    'tests/register_allocate_test.cpp',
    'tests/roundeven_test.cpp',
    'tests/set_test.cpp',
    'tests/sparse_array_test.cpp',
    'tests/u_atomic_test.cpp',
    'tests/u_debug_stack_test.cpp',
    'tests/u_qsort_test.cpp',
    'tests/vector_test.cpp',
  )

  if not (host_machine.system() == 'windows' and cc.get_id() == 'gcc')
    # FIXME: These tests fail with mingw, but not with msvc.
    files_util_tests += files(
      'tests/string_buffer_test.cpp',
    )
  endif

  if cc.has_header('sys/time.h')  # MinGW has this, but Vanilla windows doesn't
    files_util_tests += files(
      'tests/timespec_test.cpp'
    )
  endif

  # FIXME: this test crashes on windows
  if host_machine.system() != 'windows'
    files_util_tests += files(
      'tests/mesa-sha1_test.cpp',
    )
  endif

  if with_shader_cache
    files_util_tests += files(
      'tests/cache_test.cpp',
    )
  endif

  test(
    'util_tests',
    executable(
      'util_tests',
      files_util_tests,
      include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
      dependencies : [idep_mesautil, idep_gtest],
    ),
    suite : ['util'],
    protocol : gtest_test_protocol,
    timeout : 180,
  )

  process_test_exe = executable(
    'process_test',
    files('tests/process_test.c'),
    include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
    dependencies : idep_mesautil,
    c_args : [c_msvc_compat_args],
  )
  if (host_machine.system() == 'windows' and build_machine.system() != 'windows')
    # This conversion is only required on mingw crosscompilers, otherwise we hit at least one of these issues
    # https://gitlab.freedesktop.org/mesa/mesa/-/issues/2690
    # https://gitlab.freedesktop.org/mesa/mesa/-/issues/2788
    prog_winepath = find_program('winepath')
    process_test_exe_full_path = run_command(
      prog_winepath, '-w', process_test_exe.full_path()
    ).stdout().strip()
  else
    process_test_exe_full_path = process_test_exe.full_path()
  endif

  test(
    'process',
    process_test_exe,
    suite : ['util'],
    env: ['BUILD_FULL_PATH='+process_test_exe_full_path]
  )

  subdir('tests/hash_table')
  subdir('tests/vma')
  subdir('tests/format')
endif
