# Copyright © 2018 Intel Corporation

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

project(
  'pixman',
  ['c'],
  version : '0.35.1',
  license : 'MIT',
  meson_version : '>= 0.47.2',
  default_options : ['buildtype=debugoptimized'],
)

config = configuration_data()
cc = meson.get_compiler('c')
null_dep = dependency('', required : false)

add_project_arguments(
  cc.get_supported_arguments([
    '-Wdeclaration-after-statement',
    '-fno-strict-aliasing',
    '-fvisibility=hidden',
  ]),
  language : ['c']
)

# GCC and Clang both ignore -Wno options that they don't recognize, so test for
# -W<opt>, then add -Wno-<opt> if it's ignored
foreach opt : ['unused-local-typedefs']
  if cc.has_argument('-W' + opt)
    add_project_arguments(['-Wno-' + opt], language : ['c'])
  endif
endforeach

use_loongson_mmi = get_option('loongson-mmi')
have_loongson_mmi = false
loongson_mmi_flags = ['-mach=loongson2f']
if not use_loongson_mmi.disabled()
  if host_machine.cpu_family() == 'mips64' and cc.compiles('''
      #ifndef __mips_loongson_vector_rev
      #error "Loongson Multimedia Instructions are only available on Loongson"
      #endif
      #if defined(__GNUC__) && (__GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 4))
      #error "Need GCC >= 4.4 for Loongson MMI compilation"
      #endif
      #include "pixman/loongson-mmintrin.h"
      int main () {
        union {
          __m64 v;
          char c[8];
        } a = { .c = {1, 2, 3, 4, 5, 6, 7, 8} };
        int b = 4;
        __m64 c = _mm_srli_pi16 (a.v, b);
        return 0;
      }''',
      args : loongson_mmi_flags,
      name : 'Loongson MMI Intrinsic Support')
    have_loongson_mmi = true
  endif
endif

if have_loongson_mmi
  config.set10('USE_LOONGSON_MMI', true)
elif use_loongson_mmi.enabled()
  error('Loongson MMI Support unavailable, but required')
endif

use_mmx = get_option('mmx')
have_mmx = false
mmx_flags = ['-mmmx', '-Winline']
if not use_mmx.disabled()
  if host_machine.cpu_family() == 'x86_64'
    have_mmx = true
  elif host_machine.cpu_family() == 'x86' and cc.compiles('''
      #include <mmintrin.h>
      #include <stdint.h>

      /* Check support for block expressions */
      #define _mm_shuffle_pi16(A, N)                    \
        ({                                              \
        __m64 ret;                                      \
                                                        \
        /* Some versions of clang will choke on K */    \
        asm ("pshufw %2, %1, %0\n\t"                    \
             : "=y" (ret)                               \
             : "y" (A), "K" ((const int8_t)N)           \
        );                                              \
                                                        \
        ret;                                            \
        })

      int main () {
          __m64 v = _mm_cvtsi32_si64 (1);
          __m64 w;

          w = _mm_shuffle_pi16(v, 5);

          /* Some versions of clang will choke on this */
          asm ("pmulhuw %1, %0\n\t"
               : "+y" (w)
               : "y" (v)
          );

          return _mm_cvtsi64_si32 (v);
      }''',
      args : mmx_flags,
      name : 'MMX Intrinsic Support')
    have_mmx = true
  endif
endif

if have_mmx
  config.set10('USE_X86_MMX', true)
elif use_mmx.enabled()
  error('MMX Support unavailable, but required')
endif

use_sse2 = get_option('sse2')
have_sse2 = false
sse2_flags = ['-msse2', '-Winline']
if not use_sse2.disabled()
  if host_machine.cpu_family() == 'x86'
    if cc.compiles('''
        #if defined(__GNUC__) && (__GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 2))
        #   if !defined(__amd64__) && !defined(__x86_64__)
        #      error "Need GCC >= 4.2 for SSE2 intrinsics on x86"
        #   endif
        #endif
        #include <mmintrin.h>
        #include <xmmintrin.h>
        #include <emmintrin.h>
        int param;
        int main () {
          __m128i a = _mm_set1_epi32 (param), b = _mm_set1_epi32 (param + 1), c;
          c = _mm_xor_si128 (a, b);
          return _mm_cvtsi128_si32(c);
        }''',
        args : sse2_flags,
        name : 'SSE2 Intrinsic Support')
      have_sse2 = true
    endif
  elif host_machine.cpu_family() == 'x86_64'
    have_sse2 = true
  endif
endif

if have_sse2
  config.set10('USE_SSE2', true)
elif use_sse2.enabled()
  error('sse2 Support unavailable, but required')
endif

use_ssse3 = get_option('ssse3')
have_ssse3 = false
ssse3_flags =['-mssse3', '-Winline']
if not use_ssse3.disabled()
  if host_machine.cpu_family().startswith('x86')
    if cc.compiles('''
        #include <mmintrin.h>
        #include <xmmintrin.h>
        #include <emmintrin.h>
        int param;
        int main () {
          __m128i a = _mm_set1_epi32 (param), b = _mm_set1_epi32 (param + 1), c;
          c = _mm_xor_si128 (a, b);
          return _mm_cvtsi128_si32(c);
        }''',
        args : ssse3_flags,
        name : 'SSSE3 Intrinsic Support')
      have_ssse3 = true
    endif
  endif
endif

if have_ssse3
  config.set10('USE_SSSE3', true)
elif use_ssse3.enabled()
  error('ssse3 Support unavailable, but required')
endif

use_vmx = get_option('vmx')
have_vmx = false
vmx_flags = ['-maltivec', '-mabi=altivec']
if not use_vmx.disabled()
  if host_machine.cpu_family().startswith('ppc')
    if cc.compiles('''
        #include <altivec.h>
        int main () {
            vector unsigned int v = vec_splat_u32 (1);
            v = vec_sub (v, v);
            return 0;
        }''',
        args : vmx_flags,
        name : 'VMX/Altivec Intrinsic Support')
      have_vmx = true
    endif
  endif
endif

if have_vmx
  config.set10('USE_VMX', true)
elif use_vmx.enabled()
  error('vmx Support unavailable, but required')
endif

use_armv6_simd = get_option('arm-simd')
have_armv6_simd = false
if not use_armv6_simd.disabled()
  if host_machine.cpu_family() == 'arm'
    if cc.compiles('''
        .text
        .arch armv6
        .object_arch armv4
        .arm
        .altmacro
        #ifndef __ARM_EABI__
        #error EABI is required (to be sure that calling conventions are compatible)
        #endif
        pld [r0]
        uqadd8 r0, r0, r0
        ''',
        args : ['-x assembler-with-cpp'],
        name : 'ARMv6 SIMD Intrinsic Support')
      have_armv6_simd = true
    endif
  endif
endif

if have_armv6_simd
  config.set10('USE_ARM_SIMD', true)
elif use_armv6_simd.enabled()
  error('ARMv6 SIMD Support unavailable, but required')
endif

use_neon = get_option('neon')
have_neon = false
if not use_neon.disabled()
  if host_machine.cpu_family() == 'arm'
    if cc.compiles('''
        .text
        .fpu neon
        .arch armv7a
        .object_arch armv4
        .eabi_attribute 10, 0
        .arm
        .altmacro
        #ifndef __ARM_EABI__
        #error EABI is required (to be sure that calling conventions are compatible)
        #endif
        pld [r0]
        vmovn.u16 d0, q0
        ''',
        args : ['-x assembler-with-cpp'],
        name : 'NEON Intrinsic Support')
      have_neon = true
    endif
  endif
endif

if have_neon
  config.set10('USE_ARM_NEON', true)
elif use_neon.enabled()
  error('NEON Support unavailable, but required')
endif

use_iwmmxt = get_option('iwmmxt')
have_iwmmxt = false
iwmmxt_flags = ['-flax-vector-conversions', '-Winline']
if not use_iwmmxt.disabled()
  if get_option('iwmmxt2')
    iwmmxt_flags += '-march=iwmmxt2'
  else
    iwmmxt_flags += '-march=iwmmxt'
  endif

  if host_machine.cpu_family() == 'arm'
    if cc.compiles('''
        #ifndef __IWMMXT__
        #error "IWMMXT not enabled (with -march=iwmmxt)"
        #endif
        #if defined(__GNUC__) && (__GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 8))
        #error "Need GCC >= 4.8 for IWMMXT intrinsics"
        #endif
        #include <mmintrin.h>
        int main () {
          union {
            __m64 v;
            char c[8];
          } a = { .c = {1, 2, 3, 4, 5, 6, 7, 8} };
          int b = 4;
          __m64 c = _mm_srli_si64 (a.v, b);
        }
        ''',
        args : iwmmxt_flags,
        name : 'IWMMXT Intrinsic Support')
      have_iwmmxt = true
    endif
  endif
endif

if have_iwmmxt
  config.set10('USE_ARM_IWMMXT', true)
elif use_iwmmxt.enabled()
  error('NEON Support unavailable, but required')
endif

use_mips_dspr2 = get_option('mips-dspr2')
have_mips_dspr2 = false
mips_dspr2_flags = ['-mdspr2']
if not use_mips_dspr2.disabled()
  if host_machine.cpu_family() == 'mips32'
    if cc.compiles('''
        #if !(defined(__mips__) &&  __mips_isa_rev >= 2)
        #error MIPS DSPr2 is currently only available on MIPS32r2 platforms.
        #endif
        int
        main ()
        {
            int c = 0, a = 0, b = 0;
            __asm__ __volatile__ (
                "precr.qb.ph %[c], %[a], %[b]          \n\t"
                : [c] "=r" (c)
                : [a] "r" (a), [b] "r" (b)
            );
            return c;
        }''',
        args : mipds_dspr2_flags,
        name : 'DSPr2 Intrinsic Support')
      have_mips_dspr2 = true
    endif
  endif
endif

if have_mips_dspr2
  config.set10('USE_MIPS_DSPR2', true)
elif use_mips_dspr2.enabled()
  error('MIPS DSPr2 Support unavailable, but required')
endif

use_gnu_asm = get_option('gnu-inline-asm')
if not use_gnu_asm.disabled()
  if cc.compiles('''
      int main () {
        /* Most modern architectures have a NOP instruction, so this is a fairly generic test. */
        asm volatile ( "\tnop\n" : : : "cc", "memory" );
        return 0;
      }
      ''',
      name : 'GNU Inline ASM support.')
    config.set10('USE_GCC_INLINE_ASM', true)
  elif use_gnu_asm.enabled()
    error('GNU inline assembly support missing but required.')
  endif
endif

if get_option('timers')
  config.set('PIXMAN_TIMERS', 1)
endif
if get_option('gnuplot')
  config.set('PIXMAN_GNUPLOT', 1)
endif

use_openmp = get_option('openmp')
dep_openmp = null_dep
if not use_openmp.disabled()
  dep_openmp = dependency('openmp', required : get_option('openmp'))
  if dep_openmp.found()
    config.set10('USE_OPENMP', true)
  endif
endif

dep_gtk = dependency('gtk+-2.0', version : '>= 2.16', required : get_option('gtk'))
dep_glib = dependency('glib-2.0', required : get_option('gtk'))
dep_pixman = dependency('pixman-1', required : get_option('gtk'),
                        version : '>= ' + meson.project_version())
dep_png = dependency('libpng', required : get_option('libpng'))
if dep_png.found()
  config.set('HAVE_LIBPNG', 1)
endif
dep_m = cc.find_library('m', required : false)
dep_threads = dependency('threads')
if dep_threads.found()
  config.set('HAVE_PTHREADS', 1)
endif

funcs = ['sigaction', 'alarm', 'mprotect', 'getpagesize', 'mmap']
# mingw claimes to have posix_memalign, but it doesn't
if host_machine.system() != 'windows'
  funcs += 'posix_memalign'
endif

foreach f : funcs
  if cc.has_function(f)
    config.set('HAVE_@0@'.format(f.to_upper()), 1)
  endif
endforeach

if cc.has_function('gettimeofday')
  config.set('HAVE_GETTIMEOFDAY', 1)
endif

# This is only used in one test, that defines _GNU_SOURCE
if cc.has_function('feenableexcept',
                   prefix : '#define _GNU_SOURCE\n#include <fenv.h>',
                   dependencies : dep_m)
  config.set('HAVE_FEENABLEEXCEPT', 1)
endif

if cc.has_header_symbol('fenv.h', 'FE_DIVBYZERO')
  config.set('HAVE_FEDIVBYZERO', 1)
endif

foreach h : ['sys/mman.h', 'fenv.h', 'unistd.h']
  if cc.check_header(h)
    config.set('HAVE_@0@'.format(h.underscorify().to_upper()), 1)
  endif
endforeach

if (host_machine.system() == 'windows' and
    cc.compiles('int __declspec(thread) foo;', name : 'TLS via __declspec(thread)'))
  config.set('TLS', '__declspec(thread)')
elif cc.compiles('int __thread foo;', name : 'TLS via __thread')
  config.set('TLS', '__thread')
endif

if cc.links('''
    static int x = 1;
    static void __attribute__((constructor)) constructor_function () { x = 0; }
    int main (void) { return x; }
    ''',
    name : '__attribute__((constructor))')
  config.set('TOOLCHAIN_SUPPORTS_ATTRIBUTE_CONSTRUCTOR', 1)
endif

if cc.links(
    ' __float128 a = 1.0Q, b = 2.0Q; int main (void) { return a + b; }',
    name : 'Has float128 support')
  config.set('HAVE_FLOAT128', 1)
endif

if cc.has_function('clz')
  config.set('HAVE_BUILTIN_CLZ', 1)
endif

if cc.links('''
    unsigned int __attribute__ ((vector_size(16))) e, a, b;
    int main (void) { e = a - ((b << 27) + (b >> (32 - 27))) + 1; return e[0]; }
    ''',
    name : 'Support for GCC vector extensions')
  config.set('HAVE_GCC_VECTOR_EXTENSIONS', 1)
endif

if host_machine.endian() == 'big'
  config.set('WORDS_BIGENDIAN', 1)
endif

# Required to make pixman-private.h
config.set('PACKAGE', 'foo')

version_conf = configuration_data()
split = meson.project_version().split('.')
version_conf.set('PIXMAN_VERSION_MAJOR', split[0])
version_conf.set('PIXMAN_VERSION_MINOR', split[1])
version_conf.set('PIXMAN_VERSION_MICRO', split[2])

add_project_arguments('-DHAVE_CONFIG_H', language : ['c'])

subdir('pixman')
subdir('test')
subdir('demos')

pkg = import('pkgconfig')
pkg.generate(
  name : 'Pixman',
  filebase : 'pixman-1',
  description : 'The pixman library (version 1)',
  libraries : libpixman,
  subdirs: 'pixman-1',
  version : meson.project_version(),
)
