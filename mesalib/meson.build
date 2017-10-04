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

project('mesa', ['c', 'cpp'], version : '17.3.0-devel', license : 'MIT',
        default_options : ['c_std=c99', 'cpp_std=c++11'])

with_dri3 = true  # XXX: need a switch for this
with_vulkan_icd_dir = get_option('vulkan-icd-dir')
with_tests = get_option('build-tests')
with_valgrind = get_option('valgrind')

# TODO: there are more platforms required for non-vulkan drivers
with_platform_wayland = false
with_platform_x11 = false
_platforms = get_option('platforms')
if _platforms != ''
  _split = _platforms.split(',')
  with_platform_x11 = _split.contains('x11')
  with_platform_wayland = _split.contains('wayland')
endif

if with_vulkan_icd_dir == ''
  with_vulkan_icd_dir = join_paths(get_option('datadir'), 'vulkan/icd.d')
endif

with_intel_vk = false
with_amd_vk = false
_vulkan_drivers = get_option('vulkan-drivers')
if _vulkan_drivers != ''
  _split = _vulkan_drivers.split(',')
  with_intel_vk = _split.contains('intel')
  with_amd_vk = _split.contains('amd')
  if not (with_platform_x11 or with_platform_wayland)
    error('Vulkan requires at least one platform (x11, wayland)')
  endif
endif

prog_python2 = find_program('python2')
has_mako = run_command(prog_python2, '-c', 'import mako')
if has_mako.returncode() != 0
  error('Python (2.x) mako module required to build mesa.')
endif

cc = meson.get_compiler('c')
if cc.get_id() == 'gcc' and cc.version().version_compare('< 4.4.6')
  error('When using GCC, version 4.4.6 or later is required.')
endif

# Arguments for the preprocessor, put these in a separate array from the C and
# C++ (cpp in meson terminology) arguments since they need to be added to the
# default arguments for both C and C++.
pre_args = ['-D__STDC_CONSTANT_MACROS', '-D__STDC_FORMAT_MACROS',
            '-D__STDC_LIMIT_MACROS',
            '-DVERSION="@0@"'.format(meson.project_version())]

# Define DEBUG for debug and debugoptimized builds
if get_option('buildtype').startswith('debug')
  pre_args += '-DDEBUG'
endif

if get_option('shader-cache')
  pre_args += '-DENABLE_SHADER_CACHE'
elif with_amd_vk
  error('Radv requires shader cache support')
endif

# Check for GCC style builtins
foreach b : ['bswap32', 'bswap64', 'clz', 'clzll', 'ctz', 'expect', 'ffs',
             'ffsll', 'popcount', 'popcountll', 'unreachable']
  if cc.has_function(b)
    pre_args += '-DHAVE___BUILTIN_@0@'.format(b.to_upper())
  endif
endforeach

# check for GCC __attribute__
foreach a : ['const', 'flatten', 'malloc', 'pure', 'unused',
             'warn_unused_result', 'weak',]
  if cc.compiles('int foo(void) __attribute__((@0@));'.format(a),
                 name : '__attribute__((@0@))'.format(a))
    pre_args += '-DHAVE_FUNC_ATTRIBUTE_@0@'.format(a.to_upper())
  endif
endforeach
if cc.compiles('int foo(const char *p, ...) __attribute__((format(printf, 1, 2)));',
               name : '__attribute__((format(...)))')
  pre_args += '-DHAVE_FUNC_ATTRIBUTE_FORMAT'
endif
if cc.compiles('struct __attribute__((packed)) foo { int bar; };',
               name : '__attribute__((packed))')
  pre_args += '-DHAVE_FUNC_ATTRIBUTE_PACKED'
endif
if cc.compiles('int *foo(void) __attribute__((returns_nonnull));',
               name : '__attribute__((returns_nonnull))')
  pre_args += '-DHAVE_FUNC_ATTRIBUTE_NONNULL'
endif
if cc.compiles('''int foo_def(void) __attribute__((visibility("default")));
                  int foo_hid(void) __attribute__((visibility("hidden")));
                  int foo_int(void) __attribute__((visibility("internal")));
                  int foo_pro(void) __attribute__((visibility("protected")));''',
               name : '__attribute__((visibility(...)))')
  pre_args += '-DHAVE_FUNC_ATTRIBUTE_VISBILITY'
endif
if cc.compiles('int foo(void) { return 0; } int bar(void) __attribute__((alias("foo")));',
               name : '__attribute__((alias(...)))')
  pre_args += '-DHAVE_FUNC_ATTRIBUTE_ALIAS'
endif

# TODO: this is very incomplete
if host_machine.system() == 'linux'
  pre_args += '-D_GNU_SOURCE'
endif

# Check for generic C arguments
c_args = []
foreach a : ['-Wall', '-Werror=implicit-function-declaration',
             '-Werror=missing-prototypes', '-fno-math-errno',
             '-fno-trapping-math', '-Qunused-arguments']
  if cc.has_argument(a)
    c_args += a
  endif
endforeach
c_vis_args = []
if cc.has_argument('-fvisibility=hidden')
  c_vis_args += '-fvisibility=hidden'
endif

# Check for generic C++ arguments
cpp = meson.get_compiler('cpp')
cpp_args = []
foreach a : ['-Wall', '-fno-math-errno', '-fno-trapping-math',
             '-Qunused-arguments', '-Wno-non-virtual-dtor']
  if cpp.has_argument(a)
    cpp_args += a
  endif
endforeach
cpp_vis_args = []
if cpp.has_argument('-fvisibility=hidden')
  cpp_vis_args += '-fvisibility=hidden'
endif

# Check for C and C++ arguments for MSVC2013 compatibility. These are only used
# in parts of the mesa code base that need to compile with old versions of
# MSVC, mainly common code
c_msvc_compat_args = []
cpp_msvc_compat_args = []
foreach a : ['-Werror=pointer-arith', '-Werror=vla']
  if cc.has_argument(a)
    c_msvc_compat_args += a
  endif
  if cpp.has_argument(a)
    cpp_msvc_compat_args += a
  endif
endforeach

no_override_init_args = []
foreach a : ['-Wno-override-init', '-Wno-initializer-overrides']
  if cc.has_argument(a)
    no_override_init_args += a
  endif
endforeach

# TODO: SSE41 (which is only required for core mesa)

# Check for GCC style atomics
if cc.compiles('int main() { int n; return __atomic_load_n(&n, __ATOMIC_ACQUIRE); }',
               name : 'GCC atomic builtins')
  pre_args += '-DUSE_GCC_ATOMIC_BUILTINS'
endif
if not cc.links('''#include <stdint.h>
                   uint64_t v;
                   int main() {
                     return __sync_add_and_fetch(&v, (uint64_t)1);
                   }''',
                name : 'GCC 64bit atomics')
  pre_args += '-DMISSING_64_BIT_ATOMICS'
endif

# TODO: endian
# TODO: powr8
# TODO: shared/static? Is this even worth doing?

# I don't think that I need to set any of the debug stuff, I think meson
# handles that for us

# TODO: ldflags

# TODO: texture-float (gallium/mesa only)

# TODO: cross-compiling. I don't think this is relavent to meson

# TODO: assembly support. mesa and vc4

# Check for standard headers and functions
if cc.has_header_symbol('sys/sysmacros.h', 'major')
  pre_args += '-DMAJOR_IN_SYSMACROS'
elif cc.has_header_symbol('sys/mkdev.h', 'major')
  pre_args += '-DMAJOR_IN_MKDEV'
endif

foreach h : ['xlocale.h', 'sys/sysctl.h']
  if cc.has_header(h)
    pre_args += '-DHAVE_@0@'.format(h.to_upper().underscorify())
  endif
endforeach

foreach f : ['strtof', 'mkostemp', 'posix_memalign']
  if cc.has_function(f)
    pre_args += '-DHAVE_@0@'.format(f.to_upper())
  endif
endforeach

# strtod locale support
if cc.links('''
    #define _GNU_SOURCE
    #include <stdlib.h>
    #include <locale.h>
    #ifdef HAVE_XLOCALE_H
    #include <xlocale.h>
    #endif
    int main() {
      locale_t loc = newlocale(LC_CTYPE_MASK, "C", NULL);
      const char *s = "1.0";
      char *end;
      double d = strtod_l(s, end, loc);
      float f = strtod_l(s, end, loc);
      freelocale(loc);
      return 0;
    }''',
    extra_args : pre_args,
    name : 'strtod has locale support')
  pre_args += '-DHAVE_STRTOD_L'
endif

# Check for some linker flags
ld_args_bsymbolic = []
if cc.links('int main() { return 0; }', args : '-Wl,-Bsymbolic')
  ld_args_bsymbolic += '-Wl,-Bsymbolic'
endif
ld_args_gc_sections = []
if cc.links('static char unused() { return 5; } int main() { return 0; }',
            args : '-Wl,--gc-sections')
  ld_args_gc_sections += '-Wl,--gc-sections'
endif

# check for dl support
if cc.has_function('dlopen')
  dep_dl = []
else
  dep_dl = cc.find_library('dl')
endif

if not cc.has_function('dladdr', dependencies : dep_dl)
  error('dl library doesn\'t have dladdr')
endif

if cc.has_function('dl_iterate_phdr')
  pre_args += '-DHAVE_DL_ITERATE_PHDR'
else
  # TODO: this is required for vulkan
endif

# Determine whether or not the rt library is needed for time functions
if cc.has_function('clock_gettime')
  dep_clock = []
else
  dep_clock = cc.find_library('rt')
endif

# TODO: some of these may be conditional
dep_zlib = dependency('zlib', version : '>= 1.2.3')
dep_thread = dependency('threads')
pre_args += '-DHAVE_PTHREAD'
dep_elf = dependency('libelf', required : false)
if not dep_elf.found()
  dep_elf = cc.find_library('elf')
endif
dep_expat = dependency('expat')
# this only exists on linux so either this is linux and it will be found, or
# its not linux and and wont
dep_m = cc.find_library('m', required : false)

# TODO: conditionalize libdrm requirement
dep_libdrm = dependency('libdrm', version : '>= 2.4.75')
pre_args += '-DHAVE_LIBDRM'
dep_libdrm_amdgpu = []
if with_amd_vk
  dep_libdrm_amdgpu = dependency('libdrm_amdgpu', version : '>= 2.4.82')
endif

llvm_modules = ['bitwriter', 'engine', 'mcdisassembler', 'mcjit']
if with_amd_vk
  llvm_modules += ['amdgpu', 'bitreader', 'ipo']
endif
dep_llvm = dependency(
  'llvm', version : '>= 3.9.0', required : false, modules : llvm_modules,
)
if not dep_llvm.found()
  if with_amd_vk
    error('Radv requires llvm.')
  endif
else
  _llvm_version = dep_llvm.version().split('.')
  # Development versions of LLVM have an 'svn' suffix, we don't want that for
  # our version checks.
  _llvm_patch = _llvm_version[2]
  if _llvm_patch.endswith('svn')
    _llvm_patch = _llvm_patch.split('s')[0]
  endif
  pre_args += [
    '-DHAVE_LLVM=0x0@0@@1@@2@'.format(_llvm_version[0], _llvm_version[1], _llvm_patch),
    '-DMESA_LLVM_VERSION_PATCH=@0@'.format(_llvm_patch),
  ]
endif

# TODO: make this conditional
dep_valgrind = dependency('valgrind', required : false)
if dep_valgrind.found() and with_valgrind
  pre_args += '-DHAVE_VALGRIND'
endif

# pthread stubs. Lets not and say we didn't

# TODO: selinux

# TODO: llvm-prefix and llvm-shared-libs

# TODO: unwind (llvm [radeon, gallivm] and gallium)

# TODO: flags for opengl, gles, dri

# TODO: gallium-hud

# TODO: glx provider

# TODO: osmesa provider

# TODO: flags for xa, egl, gbm, nin, xvmc, vdpau, omx, va, opencl,
#       gallium-tests,

# TODO: gallium drivers

# TODO: libglvnd

# TODO: symbol mangling

# TODO: dri handling

# TODO: shared-glapi

# TODO: libgl requirements

# TODO: GLX configuration

# TODO: egl configuration

if with_platform_wayland
  prog_wl_scanner = find_program('wayland-scanner')
  dep_wl_protocols = dependency('wayland-protocols', version : '>= 1.8')
  dep_wayland_client = dependency('wayland-client', version : '>=1.11')
  dep_wayland_server = dependency('wayland-server', version : '>=1.11')
else
  prog_wl_scanner = []
  dep_wl_protocols = []
  dep_wayland_client = []
  dep_wayland_server = []
endif

dep_xcb_dri2 = []
dep_xcb_dri3 = []
if with_platform_x11
  dep_xcb_dri2 = [
    dependency('x11-xcb'),
    dependency('xcb'),
    dependency('xcb-dri2', version : '>= 1.8'),
    dependency('xcb-xfixes'),
  ]
  if with_dri3
    dep_xcb_dri3 = [
      dep_xcb_dri2,
      dependency('xcb-dri3'),
      dependency('xcb-present'),
      dependency('xcb-sync'),
      dependency('xshmfence', version : '>= 1.1'),
    ]
  else
    # TODO: dri3 is required for vulkan
  endif
endif

# TODO: platforms for !vulkan

# TODO: dri paths

# TODO: dri drivers

# TODO: osmesa

# TODO: egl

# TODO: xa

# TODO: vallium G3DVL

# TODO: nine

# TODO: clover

# TODO: egl sans x11

# TODO: xvmc

# TODO: gallium tests

# TODO: various libdirs

# TODO: swr

# TODO: gallium driver dirs

# FIXME: this is a workaround for #2326
prog_touch = find_program('touch')
dummy_cpp = custom_target(
  'dummy_cpp',
  output : 'dummy.cpp',
  command : [prog_touch, '@OUTPUT@'],
)

foreach a : pre_args
  add_project_arguments(a, language : ['c', 'cpp'])
endforeach
foreach a : c_args
  add_project_arguments(a, language : ['c'])
endforeach
foreach a : cpp_args
  add_project_arguments(a, language : ['cpp'])
endforeach

inc_include = include_directories('include')

subdir('include')
subdir('src')
