dnl Copyright © 2011-2014 Intel Corporation
dnl Copyright © 2011-2014 Emil Velikov <emil.l.velikov@gmail.com>
dnl Copyright © 2007-2010 Dan Nicholson
dnl Copyright © 2010-2014 Marek Olšák <maraeo@gmail.com>
dnl Copyright © 2010-2014 Christian König
dnl Copyright © 2012-2014 Tom Stellard <tstellar@gmail.com>
dnl Copyright © 2009-2012 Jakob Bornecrantz
dnl Copyright © 2009-2014 Jon TURNEY
dnl Copyright © 2011-2012 Benjamin Franzke
dnl Copyright © 2008-2014 David Airlie
dnl Copyright © 2009-2013 Brian Paul
dnl
dnl Permission is hereby granted, free of charge, to any person obtaining a
dnl copy of this software and associated documentation files (the "Software"),
dnl to deal in the Software without restriction, including without limitation
dnl the rights to use, copy, modify, merge, publish, distribute, sublicense,
dnl and/or sell copies of the Software, and to permit persons to whom the
dnl Software is furnished to do so, subject to the following conditions:
dnl
dnl The above copyright notice and this permission notice (including the next
dnl paragraph) shall be included in all copies or substantial portions of the
dnl Software.
dnl
dnl THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
dnl IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
dnl FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
dnl THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
dnl LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
dnl FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
dnl DEALINGS IN THE SOFTWARE.
dnl
dnl Process this file with autoconf to create configure.

AC_PREREQ([2.60])

dnl Tell the user about autoconf.html in the --help output
m4_divert_once([HELP_END], [
See docs/autoconf.html for more details on the options for Mesa.])

m4_define(MESA_VERSION, m4_normalize(m4_include(VERSION)))
AC_INIT([Mesa], [MESA_VERSION],
    [https://bugs.freedesktop.org/enter_bug.cgi?product=Mesa])
AC_CONFIG_AUX_DIR([bin])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE([foreign tar-ustar dist-xz subdir-objects])

dnl We only support native Windows builds (MinGW/MSVC) through SCons.
case "$host_os" in
mingw*)
    AC_MSG_ERROR([MinGW build not supported through autoconf/automake, use SCons instead])
    ;;
esac

# Support silent build rules, requires at least automake-1.11. Disable
# by either passing --disable-silent-rules to configure or passing V=1
# to make
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])],
    [AC_SUBST([AM_DEFAULT_VERBOSITY], [1])])

m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

dnl Set internal versions
OSMESA_VERSION=8
AC_SUBST([OSMESA_VERSION])
OPENCL_VERSION=1
AC_SUBST([OPENCL_VERSION])

# The idea is that libdrm is distributed as one cohesive package, even
# though it is composed of multiple libraries. However some drivers
# may have different version requirements than others. This list
# codifies which drivers need which version of libdrm. Any libdrm
# version dependencies in non-driver-specific code should be reflected
# in the first entry.
LIBDRM_REQUIRED=2.4.75
LIBDRM_RADEON_REQUIRED=2.4.71
LIBDRM_AMDGPU_REQUIRED=2.4.90
LIBDRM_INTEL_REQUIRED=2.4.75
LIBDRM_NVVIEUX_REQUIRED=2.4.66
LIBDRM_NOUVEAU_REQUIRED=2.4.66
LIBDRM_FREEDRENO_REQUIRED=2.4.89
LIBDRM_ETNAVIV_REQUIRED=2.4.82

dnl Versions for external dependencies
DRI2PROTO_REQUIRED=2.8
GLPROTO_REQUIRED=1.4.14
LIBOMXIL_BELLAGIO_REQUIRED=0.0
LIBVA_REQUIRED=0.38.0
VDPAU_REQUIRED=1.1
WAYLAND_REQUIRED=1.11
WAYLAND_PROTOCOLS_REQUIRED=1.8
XCB_REQUIRED=1.9.3
XCBDRI2_REQUIRED=1.8
XCBGLX_REQUIRED=1.8.1
XDAMAGE_REQUIRED=1.1
XSHMFENCE_REQUIRED=1.1
XVMC_REQUIRED=1.0.6
PYTHON_MAKO_REQUIRED=0.8.0
LIBSENSORS_REQUIRED=4.0.0
ZLIB_REQUIRED=1.2.3

dnl LLVM versions
LLVM_REQUIRED_GALLIUM=3.3.0
LLVM_REQUIRED_OPENCL=3.9.0
LLVM_REQUIRED_R600=3.9.0
LLVM_REQUIRED_RADEONSI=4.0.0
LLVM_REQUIRED_RADV=4.0.0
LLVM_REQUIRED_SWR=3.9.0

dnl Check for progs
AC_PROG_CPP
AC_PROG_CC
AC_PROG_CXX
dnl add this here, so the help for this environmnet variable is close to
dnl other CC/CXX flags related help
AC_ARG_VAR([CXX11_CXXFLAGS], [Compiler flag to enable C++11 support (only needed if not
                              enabled by default and different  from -std=c++11)])
AM_PROG_CC_C_O
AM_PROG_AS
AX_CHECK_GNU_MAKE
AC_CHECK_PROGS([PYTHON2], [python2.7 python2 python])
AC_PROG_SED
AC_PROG_MKDIR_P

AC_SYS_LARGEFILE


LT_PREREQ([2.2])
LT_INIT([disable-static])

AC_CHECK_PROG(RM, rm, [rm -f])

AX_PROG_BISON([],
              AS_IF([test ! -f "$srcdir/src/compiler/glsl/glcpp/glcpp-parse.c"],
                    [AC_MSG_ERROR([bison not found - unable to compile glcpp-parse.y])]))
AX_PROG_FLEX([],
             AS_IF([test ! -f "$srcdir/src/compiler/glsl/glcpp/glcpp-lex.c"],
                   [AC_MSG_ERROR([flex not found - unable to compile glcpp-lex.l])]))

AC_CHECK_PROG(INDENT, indent, indent, cat)
if test "x$INDENT" != "xcat"; then
    # Only GNU indent is supported
    INDENT_VERSION=`indent --version | grep GNU`
    if test $? -eq 0; then
       AC_SUBST(INDENT_FLAGS, '-i4 -nut -br -brs -npcs -ce -TGLubyte -TGLbyte -TBool')
    else
       INDENT="cat"
    fi
fi

AX_CHECK_PYTHON_MAKO_MODULE($PYTHON_MAKO_REQUIRED)

if test -z "$PYTHON2"; then
    if test ! -f "$srcdir/src/util/format_srgb.c"; then
        AC_MSG_ERROR([Python not found - unable to generate sources])
    fi
else
    if test "x$acv_mako_found" = xno; then
        if test ! -f "$srcdir/src/mesa/main/format_unpack.c"; then
            AC_MSG_ERROR([Python mako module v$PYTHON_MAKO_REQUIRED or higher not found])
        fi
    fi
fi

AC_PROG_INSTALL

dnl We need a POSIX shell for parts of the build. Assume we have one
dnl in most cases.
case "$host_os" in
solaris*)
    # Solaris /bin/sh is too old/non-POSIX compliant
    AC_PATH_PROGS(POSIX_SHELL, [ksh93 ksh sh])
    SHELL="$POSIX_SHELL"
    ;;
esac

dnl clang is mostly GCC-compatible, but its version is much lower,
dnl so we have to check for it.
AC_MSG_CHECKING([if compiling with clang])

AC_COMPILE_IFELSE(
[AC_LANG_PROGRAM([], [[
#ifndef __clang__
       not clang
#endif
]])],
[acv_mesa_CLANG=yes], [acv_mesa_CLANG=no])

AC_MSG_RESULT([$acv_mesa_CLANG])

dnl If we're using GCC, make sure that it is at least version 4.2.0.  Older
dnl versions are explictly not supported.
GEN_ASM_OFFSETS=no
USE_GNU99=no
if test "x$GCC" = xyes -a "x$acv_mesa_CLANG" = xno; then
    AC_MSG_CHECKING([whether gcc version is sufficient])
    major=0
    minor=0

    GCC_VERSION=`$CC -dumpversion`
    if test $? -eq 0; then
        GCC_VERSION_MAJOR=`echo $GCC_VERSION | cut -d. -f1`
        GCC_VERSION_MINOR=`echo $GCC_VERSION | cut -d. -f2`
    fi

    if test $GCC_VERSION_MAJOR -lt 4 -o $GCC_VERSION_MAJOR -eq 4 -a $GCC_VERSION_MINOR -lt 2 ; then
        AC_MSG_RESULT([no])
        AC_MSG_ERROR([If using GCC, version 4.2.0 or later is required.])
    else
        AC_MSG_RESULT([yes])
    fi

    if test $GCC_VERSION_MAJOR -lt 4 -o $GCC_VERSION_MAJOR -eq 4 -a $GCC_VERSION_MINOR -lt 6 ; then
        USE_GNU99=yes
    fi
    if test "x$cross_compiling" = xyes; then
        GEN_ASM_OFFSETS=yes
    fi
fi

dnl We don't support building Mesa with Sun C compiler
dnl https://bugs.freedesktop.org/show_bug.cgi?id=93189
AC_CHECK_DECL([__SUNPRO_C], [SUNCC=yes], [SUNCC=no])
if test "x$SUNCC" = xyes; then
    AC_MSG_ERROR([Building with Sun C compiler is not supported, use GCC instead.])
fi

dnl Check for compiler builtins
AX_GCC_BUILTIN([__builtin_bswap32])
AX_GCC_BUILTIN([__builtin_bswap64])
AX_GCC_BUILTIN([__builtin_clz])
AX_GCC_BUILTIN([__builtin_clzll])
AX_GCC_BUILTIN([__builtin_ctz])
AX_GCC_BUILTIN([__builtin_expect])
AX_GCC_BUILTIN([__builtin_ffs])
AX_GCC_BUILTIN([__builtin_ffsll])
AX_GCC_BUILTIN([__builtin_popcount])
AX_GCC_BUILTIN([__builtin_popcountll])
AX_GCC_BUILTIN([__builtin_unreachable])

AX_GCC_FUNC_ATTRIBUTE([const])
AX_GCC_FUNC_ATTRIBUTE([flatten])
AX_GCC_FUNC_ATTRIBUTE([format])
AX_GCC_FUNC_ATTRIBUTE([malloc])
AX_GCC_FUNC_ATTRIBUTE([packed])
AX_GCC_FUNC_ATTRIBUTE([pure])
AX_GCC_FUNC_ATTRIBUTE([returns_nonnull])
AX_GCC_FUNC_ATTRIBUTE([unused])
AX_GCC_FUNC_ATTRIBUTE([visibility])
AX_GCC_FUNC_ATTRIBUTE([warn_unused_result])
AX_GCC_FUNC_ATTRIBUTE([weak])
AX_GCC_FUNC_ATTRIBUTE([alias])
AX_GCC_FUNC_ATTRIBUTE([noreturn])

AM_CONDITIONAL([GEN_ASM_OFFSETS], test "x$GEN_ASM_OFFSETS" = xyes)

dnl Make sure the pkg-config macros are defined
m4_ifndef([PKG_PROG_PKG_CONFIG],
    [m4_fatal([Could not locate the pkg-config autoconf macros.
  These are usually located in /usr/share/aclocal/pkg.m4. If your macros
  are in a different location, try setting the environment variable
  ACLOCAL="aclocal -I/other/macro/dir" before running autoreconf.])])
PKG_PROG_PKG_CONFIG()

dnl LIB_DIR - library basename
LIB_DIR=`echo $libdir | $SED 's%.*/%%'`
AC_SUBST([LIB_DIR])

dnl Cache LDFLAGS and CPPFLAGS so we can add to them and restore later
_SAVE_LDFLAGS="$LDFLAGS"
_SAVE_CPPFLAGS="$CPPFLAGS"

dnl Compiler macros
DEFINES="-D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS"
AC_SUBST([DEFINES])
android=no
case "$host_os" in
*-android*)
    android=yes
    ;;
linux*|*-gnu*|gnu*|cygwin*)
    DEFINES="$DEFINES -D_GNU_SOURCE"
    ;;
solaris*)
    DEFINES="$DEFINES -DSVR4"
    ;;
esac

AM_CONDITIONAL(HAVE_ANDROID, test "x$android" = xyes)

dnl
dnl Check compiler flags
dnl
AX_CHECK_COMPILE_FLAG([-Wall],                                 [CFLAGS="$CFLAGS -Wall"])
AX_CHECK_COMPILE_FLAG([-Werror=implicit-function-declaration], [CFLAGS="$CFLAGS -Werror=implicit-function-declaration"])
AX_CHECK_COMPILE_FLAG([-Werror=missing-prototypes],            [CFLAGS="$CFLAGS -Werror=missing-prototypes"])
AX_CHECK_COMPILE_FLAG([-Wmissing-prototypes],                  [CFLAGS="$CFLAGS -Wmissing-prototypes"])
AX_CHECK_COMPILE_FLAG([-fno-math-errno],                       [CFLAGS="$CFLAGS -fno-math-errno"])
AX_CHECK_COMPILE_FLAG([-fno-trapping-math],                    [CFLAGS="$CFLAGS -fno-trapping-math"])
AX_CHECK_COMPILE_FLAG([-fvisibility=hidden],                   [VISIBILITY_CFLAGS="-fvisibility=hidden"])

dnl
dnl Check C++ compiler flags
dnl
AC_LANG_PUSH([C++])
AX_CHECK_COMPILE_FLAG([-Wall],                                 [CXXFLAGS="$CXXFLAGS -Wall"])
AX_CHECK_COMPILE_FLAG([-fno-math-errno],                       [CXXFLAGS="$CXXFLAGS -fno-math-errno"])
AX_CHECK_COMPILE_FLAG([-fno-trapping-math],                    [CXXFLAGS="$CXXFLAGS -fno-trapping-math"])
AX_CHECK_COMPILE_FLAG([-fvisibility=hidden],                   [VISIBILITY_CXXFLAGS="-fvisibility=hidden"])
AC_LANG_POP([C++])

# Flags to help ensure that certain portions of the code -- and only those
# portions -- can be built with MSVC:
# - src/util, src/gallium/auxiliary, rc/gallium/drivers/llvmpipe, and
# - non-Linux/Posix OpenGL portions needs to build on MSVC 2013 (which
#   supports most of C99)
# - the rest has no compiler compiler restrictions
AX_CHECK_COMPILE_FLAG([-Werror=pointer-arith],                 [MSVC2013_COMPAT_CFLAGS="$MSVC2013_COMPAT_CFLAGS -Werror=pointer-arith"])
AX_CHECK_COMPILE_FLAG([-Werror=vla],                           [MSVC2013_COMPAT_CFLAGS="$MSVC2013_COMPAT_CFLAGS -Werror=vla"])
AC_LANG_PUSH([C++])
AX_CHECK_COMPILE_FLAG([-Werror=pointer-arith],                 [MSVC2013_COMPAT_CXXFLAGS="$MSVC2013_COMPAT_CXXFLAGS -Werror=pointer-arith"])
AX_CHECK_COMPILE_FLAG([-Werror=vla],                           [MSVC2013_COMPAT_CXXFLAGS="$MSVC2013_COMPAT_CXXFLAGS -Werror=vla"])
AC_LANG_POP([C++])

AC_SUBST([MSVC2013_COMPAT_CFLAGS])
AC_SUBST([MSVC2013_COMPAT_CXXFLAGS])

if test "x$GCC" = xyes; then
    if test "x$USE_GNU99" = xyes; then
        CFLAGS="$CFLAGS -std=gnu99"
    else
        CFLAGS="$CFLAGS -std=c99"
    fi
fi

dnl
dnl Check whether C++11 is supported, if the environment variable
dnl CXX11_CXXFLAGS is set it takes precedence.
dnl

AC_LANG_PUSH([C++])

check_cxx11_available() {
    output_support=$1
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM([
             #if !(__cplusplus >= 201103L)
             #error
             #endif
             #include <tuple>
        ])
    ], [
         AC_MSG_RESULT(yes)
         cxx11_support=yes
    ], AC_MSG_RESULT(no))
    eval "$output_support=\$cxx11_support"
}

HAVE_CXX11=no
save_CXXFLAGS="$CXXFLAGS"

dnl If the user provides a flag to enable c++11, then we test only this
if test "x$CXX11_CXXFLAGS" != "x"; then
   CXXFLAGS="$CXXFLAGS $CXX11_CXXFLAGS"
   AC_MSG_CHECKING(whether c++11 is enabled by via $CXX11_CXXFLAGS)
   check_cxx11_available HAVE_CXX11
else
   dnl test whether c++11 is enabled by default
   AC_MSG_CHECKING(whether c++11 is enabled by default)
   check_cxx11_available HAVE_CXX11

   dnl C++11 not enabled by default, test whether -std=c++11 does the job
   if test "x$HAVE_CXX11" != "xyes"; then
       CXX11_CXXFLAGS=-std=c++11
       CXXFLAGS="$CXXFLAGS $CXX11_CXXFLAGS"
       AC_MSG_CHECKING(whether c++11 is enabled by via $CXX11_CXXFLAGS)
       check_cxx11_available HAVE_CXX11
   fi
fi

CXXFLAGS="$save_CXXFLAGS"
AM_CONDITIONAL(HAVE_STD_CXX11, test "x$HAVE_CXX11" = "xyes")
AC_SUBST(CXX11_CXXFLAGS)
AC_LANG_POP([C++])

dnl even if the compiler appears to support it, using visibility attributes isn't
dnl going to do anything useful currently on cygwin apart from emit lots of warnings
case "$host_os" in
cygwin*)
    VISIBILITY_CFLAGS=""
    VISIBILITY_CXXFLAGS=""
    ;;
esac

AC_SUBST([VISIBILITY_CFLAGS])
AC_SUBST([VISIBILITY_CXXFLAGS])

dnl For some reason, the test for -Wno-foo always succeeds with gcc, even if the
dnl option is not supported. Hence, check for -Wfoo instead.
AX_CHECK_COMPILE_FLAG([-Woverride-init],                    [WNO_OVERRIDE_INIT="$WNO_OVERRIDE_INIT -Wno-override-init"])         # gcc
AX_CHECK_COMPILE_FLAG([-Winitializer-overrides],            [WNO_OVERRIDE_INIT="$WNO_OVERRIDE_INIT -Wno-initializer-overrides"]) # clang
AC_SUBST([WNO_OVERRIDE_INIT])

dnl
dnl Optional flags, check for compiler support
dnl
SSE41_CFLAGS="-msse4.1"
dnl Code compiled by GCC with -msse* assumes a 16 byte aligned
dnl stack, but on x86-32 such alignment is not guaranteed.
case "$target_cpu" in
i?86)
    SSE41_CFLAGS="$SSE41_CFLAGS -mstackrealign"
    ;;
esac
save_CFLAGS="$CFLAGS"
CFLAGS="$SSE41_CFLAGS $CFLAGS"
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#include <smmintrin.h>
int param;
int main () {
    __m128i a = _mm_set1_epi32 (param), b = _mm_set1_epi32 (param + 1), c;
    c = _mm_max_epu32(a, b);
    return _mm_cvtsi128_si32(c);
}]])], SSE41_SUPPORTED=1)
CFLAGS="$save_CFLAGS"
if test "x$SSE41_SUPPORTED" = x1; then
    DEFINES="$DEFINES -DUSE_SSE41"
fi
AM_CONDITIONAL([SSE41_SUPPORTED], [test x$SSE41_SUPPORTED = x1])
AC_SUBST([SSE41_CFLAGS], $SSE41_CFLAGS)

dnl Check for new-style atomic builtins
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
int main() {
    int n;
    return __atomic_load_n(&n, __ATOMIC_ACQUIRE);
}]])], GCC_ATOMIC_BUILTINS_SUPPORTED=1)
if test "x$GCC_ATOMIC_BUILTINS_SUPPORTED" = x1; then
    DEFINES="$DEFINES -DUSE_GCC_ATOMIC_BUILTINS"
    dnl On some platforms, new-style atomics need a helper library
    AC_MSG_CHECKING(whether -latomic is needed)
    AC_LINK_IFELSE([AC_LANG_SOURCE([[
    #include <stdint.h>
    uint64_t v;
    int main() {
        return (int)__atomic_load_n(&v, __ATOMIC_ACQUIRE);
    }]])], GCC_ATOMIC_BUILTINS_NEED_LIBATOMIC=no, GCC_ATOMIC_BUILTINS_NEED_LIBATOMIC=yes)
    AC_MSG_RESULT($GCC_ATOMIC_BUILTINS_NEED_LIBATOMIC)
    if test "x$GCC_ATOMIC_BUILTINS_NEED_LIBATOMIC" = xyes; then
        LIBATOMIC_LIBS="-latomic"
    fi
fi
AM_CONDITIONAL([GCC_ATOMIC_BUILTINS_SUPPORTED], [test x$GCC_ATOMIC_BUILTINS_SUPPORTED = x1])
AC_SUBST([LIBATOMIC_LIBS])

dnl Check if host supports 64-bit atomics
dnl note that lack of support usually results in link (not compile) error
AC_MSG_CHECKING(whether __sync_add_and_fetch_8 is supported)
AC_LINK_IFELSE([AC_LANG_SOURCE([[
#include <stdint.h>
uint64_t v;
int main() {
    return __sync_add_and_fetch(&v, (uint64_t)1);
}]])], GCC_64BIT_ATOMICS_SUPPORTED=yes, GCC_64BIT_ATOMICS_SUPPORTED=no)
if test "x$GCC_64BIT_ATOMICS_SUPPORTED" != xyes; then
    DEFINES="$DEFINES -DMISSING_64BIT_ATOMICS"
fi
AC_MSG_RESULT($GCC_64BIT_ATOMICS_SUPPORTED)

dnl Check for Endianness
AC_C_BIGENDIAN(
   little_endian=no,
   little_endian=yes,
   little_endian=no,
   little_endian=no
)

dnl Check for POWER8 Architecture
PWR8_CFLAGS="-mpower8-vector"
have_pwr8_intrinsics=no
AC_MSG_CHECKING(whether gcc supports -mpower8-vector)
save_CFLAGS=$CFLAGS
CFLAGS="$PWR8_CFLAGS $CFLAGS"
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#if defined(__GNUC__) && (__GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 8))
#error "Need GCC >= 4.8 for sane POWER8 support"
#endif
#include <altivec.h>
int main () {
    vector unsigned char r;
    vector unsigned int v = vec_splat_u32 (1);
    r = __builtin_vec_vgbbd ((vector unsigned char) v);
    return 0;
}]])], have_pwr8_intrinsics=yes)
CFLAGS=$save_CFLAGS

AC_ARG_ENABLE(pwr8,
   [AS_HELP_STRING([--disable-pwr8],
                   [disable POWER8-specific instructions])],
   [enable_pwr8=$enableval], [enable_pwr8=auto])

if test "x$enable_pwr8" = xno ; then
   have_pwr8_intrinsics=disabled
fi

if test $have_pwr8_intrinsics = yes && test $little_endian = yes ; then
   DEFINES="$DEFINES -D_ARCH_PWR8"
   CXXFLAGS="$CXXFLAGS $PWR8_CFLAGS"
   CFLAGS="$CFLAGS $PWR8_CFLAGS"
else
   PWR8_CFLAGS=
fi

AC_MSG_RESULT($have_pwr8_intrinsics)
if test "x$enable_pwr8" = xyes && test $have_pwr8_intrinsics = no ; then
   AC_MSG_ERROR([POWER8 compiler support not detected])
fi

if test $have_pwr8_intrinsics = yes && test $little_endian = no ; then
   AC_MSG_WARN([POWER8 optimization is enabled only on POWER8 Little-Endian])
fi

AC_SUBST([PWR8_CFLAGS], $PWR8_CFLAGS)

dnl Can't have static and shared libraries, default to static if user
dnl explicitly requested. If both disabled, set to static since shared
dnl was explicitly requested.
case "x$enable_static$enable_shared" in
xyesyes)
    AC_MSG_ERROR([Cannot enable both static and shared. Building using --enable-shared is strongly recommended])
    ;;
xnono)
    AC_MSG_ERROR([Cannot disable both static and shared. Building using --enable-shared is strongly recommended])
    ;;
esac

AM_CONDITIONAL(BUILD_SHARED, test "x$enable_shared" = xyes)

dnl
dnl other compiler options
dnl
AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug],
        [use debug compiler flags and macros @<:@default=disabled@:>@])],
    [enable_debug="$enableval"],
    [enable_debug=no]
)

AC_ARG_ENABLE([profile],
    [AS_HELP_STRING([--enable-profile],
        [enable profiling of code @<:@default=disabled@:>@])],
    [enable_profile="$enableval"],
    [enable_profile=no]
)

AC_ARG_ENABLE([sanitize],
    [AS_HELP_STRING([--enable-sanitize@<:@=address|undefined@:>@],
        [enable code sanitizer @<:@default=disabled@:>@])],
    [enable_sanitize="$enableval"],
    [enable_sanitize=no])

if test "x$enable_profile" = xyes; then
    DEFINES="$DEFINES -DPROFILE"
    if test "x$GCC" = xyes; then
        CFLAGS="$CFLAGS -fno-omit-frame-pointer"
    fi
    if test "x$GXX" = xyes; then
        CXXFLAGS="$CXXFLAGS -fno-omit-frame-pointer"
    fi
fi

if test "x$enable_debug" = xyes; then
    DEFINES="$DEFINES -DDEBUG"
    if test "x$enable_profile" = xyes; then
        AC_MSG_WARN([Debug and Profile are enabled at the same time])
    fi
    if test "x$GCC" = xyes; then
        if ! echo "$CFLAGS" | grep -q -e '-g'; then
            CFLAGS="$CFLAGS -g"
        fi
        if ! echo "$CFLAGS" | grep -q -e '-O'; then
            CFLAGS="$CFLAGS -O0"
        fi
    fi
    if test "x$GXX" = xyes; then
        if ! echo "$CXXFLAGS" | grep -q -e '-g'; then
            CXXFLAGS="$CXXFLAGS -g"
        fi
        if ! echo "$CXXFLAGS" | grep -q -e '-O'; then
            CXXFLAGS="$CXXFLAGS -O0"
        fi
    fi
else
   DEFINES="$DEFINES -DNDEBUG"
fi

if test "x$enable_sanitize" != xno; then
    if test "x$enable_profile" = xyes; then
        AC_MSG_WARN([Sanitize and Profile are enabled at the same time])
    fi

    CFLAGS="$CFLAGS -fsanitize=$enable_sanitize"
    CXXFLAGS="$CXXFLAGS -fsanitize=$enable_sanitize"
    LDFLAGS="$LDFLAGS -fsanitize=$enable_sanitize"

    AC_LINK_IFELSE(
        [AC_LANG_SOURCE([int main(){return 0;}])],
        [],
        [AC_MSG_FAILURE([sanitize flags '$enable_sanitize' not supported])])
fi

dnl
dnl Check if linker supports -Bsymbolic
dnl
save_LDFLAGS=$LDFLAGS
LDFLAGS="$LDFLAGS -Wl,-Bsymbolic"
AC_MSG_CHECKING([if ld supports -Bsymbolic])
AC_LINK_IFELSE(
    [AC_LANG_SOURCE([int main() { return 0;}])],
    [AC_MSG_RESULT([yes])
        BSYMBOLIC="-Wl,-Bsymbolic";],
    [AC_MSG_RESULT([no])
        BSYMBOLIC="";])
LDFLAGS=$save_LDFLAGS

AC_SUBST([BSYMBOLIC])

dnl
dnl Check if linker supports garbage collection
dnl
save_LDFLAGS=$LDFLAGS
LDFLAGS="$LDFLAGS -Wl,--gc-sections"
AC_MSG_CHECKING([whether ld supports --gc-sections])
AC_LINK_IFELSE(
    [AC_LANG_SOURCE([static char UnusedFunc() { return 5; } int main() { return 0;}])],
    [AC_MSG_RESULT([yes])
        GC_SECTIONS="-Wl,--gc-sections";],
    [AC_MSG_RESULT([no])
        GC_SECTIONS="";])
LDFLAGS=$save_LDFLAGS

AC_SUBST([GC_SECTIONS])

dnl
dnl OpenBSD does not have DT_NEEDED entries for libc by design
dnl so when these flags are passed to ld via libtool the checks will fail
dnl
case "$host_os" in
openbsd* | darwin* )
    LD_NO_UNDEFINED="" ;;
*)
    if test "x$enable_sanitize" = xno; then
        LD_NO_UNDEFINED="-Wl,--no-undefined"
    else
        LD_NO_UNDEFINED=""
    fi
    ;;
esac

AC_SUBST([LD_NO_UNDEFINED])

dnl
dnl Check if linker supports version scripts
dnl
AC_MSG_CHECKING([if the linker supports version-scripts])
save_LDFLAGS=$LDFLAGS
LDFLAGS="$LDFLAGS -Wl,--version-script=$srcdir/build-support/conftest.map"
AC_LINK_IFELSE(
    [AC_LANG_SOURCE([int main() { return 0;}])],
    [have_ld_version_script=yes;AC_MSG_RESULT(yes)],
    [have_ld_version_script=no; AC_MSG_RESULT(no)])
LDFLAGS=$save_LDFLAGS
AM_CONDITIONAL(HAVE_LD_VERSION_SCRIPT, test "$have_ld_version_script" = "yes")

dnl
dnl Check if linker supports dynamic list files
dnl
AC_MSG_CHECKING([if the linker supports --dynamic-list])
save_LDFLAGS=$LDFLAGS
LDFLAGS="$LDFLAGS -Wl,--dynamic-list=$srcdir/build-support/conftest.dyn"
AC_LINK_IFELSE(
    [AC_LANG_SOURCE([int main() { return 0;}])],
    [have_ld_dynamic_list=yes;AC_MSG_RESULT(yes)],
    [have_ld_dynamic_list=no; AC_MSG_RESULT(no)])
LDFLAGS=$save_LDFLAGS
AM_CONDITIONAL(HAVE_LD_DYNAMIC_LIST, test "$have_ld_dynamic_list" = "yes")

dnl
dnl OSX linker does not support build-id
dnl
case "$host_os" in
darwin*)
    LD_BUILD_ID=""
    ;;
*)
    LD_BUILD_ID="-Wl,--build-id=sha1"
    ;;
esac
AC_SUBST([LD_BUILD_ID])

dnl
dnl compatibility symlinks
dnl
case "$host_os" in
linux* )
    HAVE_COMPAT_SYMLINKS=yes ;;
* )
    HAVE_COMPAT_SYMLINKS=no ;;
esac

AM_CONDITIONAL(HAVE_COMPAT_SYMLINKS, test "x$HAVE_COMPAT_SYMLINKS" = xyes)

dnl
dnl library names
dnl
dnl Unfortunately we need to do a few things that libtool can't help us with,
dnl so we need some knowledge of shared library filenames:
dnl
dnl LIB_EXT is the extension used when creating symlinks for alternate
dnl filenames for a shared library which will be dynamically loaded
dnl
dnl IMP_LIB_EXT is the extension used when checking for the presence of a
dnl the file for a shared library we wish to link with
dnl
case "$host_os" in
darwin* )
    LIB_EXT='dylib'
    IMP_LIB_EXT=$LIB_EXT
    ;;
cygwin* )
    LIB_EXT='dll'
    IMP_LIB_EXT='dll.a'
    ;;
aix* )
    LIB_EXT='a'
    IMP_LIB_EXT=$LIB_EXT
    ;;
* )
    LIB_EXT='so'
    IMP_LIB_EXT=$LIB_EXT
    ;;
esac

AC_SUBST([LIB_EXT])

dnl
dnl potentially-infringing-but-nobody-knows-for-sure stuff
dnl
AC_ARG_ENABLE([texture-float],
    [AS_HELP_STRING([--enable-texture-float],
        [enable floating-point textures and renderbuffers @<:@default=disabled@:>@])],
    [enable_texture_float="$enableval"],
    [enable_texture_float=no]
)
if test "x$enable_texture_float" = xyes; then
    AC_MSG_WARN([Floating-point textures enabled.])
    AC_MSG_WARN([Please consult docs/patents.txt with your lawyer before building Mesa.])
    DEFINES="$DEFINES -DTEXTURE_FLOAT_ENABLED"
fi

dnl
dnl Arch/platform-specific settings
dnl
AC_ARG_ENABLE([asm],
    [AS_HELP_STRING([--disable-asm],
        [disable assembly usage @<:@default=enabled on supported platforms@:>@])],
    [enable_asm="$enableval"],
    [enable_asm=yes]
)
asm_arch=""
AC_MSG_CHECKING([whether to enable assembly])
test "x$enable_asm" = xno && AC_MSG_RESULT([no])
# disable if cross compiling on x86/x86_64 since we must run gen_matypes
if test "x$enable_asm" = xyes -a "x$cross_compiling" = xyes; then
    case "$host_cpu" in
    i?86 | x86_64 | amd64)
        if test "x$host_cpu" != "x$target_cpu"; then
            enable_asm=no
            AC_MSG_RESULT([no, cross compiling])
        fi
        ;;
    esac
fi
# check for supported arches
if test "x$enable_asm" = xyes; then
    case "$host_cpu" in
    i?86)
        case "$host_os" in
        linux* | *freebsd* | dragonfly* | *netbsd* | openbsd* | gnu*)
            asm_arch=x86
            ;;
        esac
        ;;
    x86_64|amd64)
        case "$host_os" in
        linux* | *freebsd* | dragonfly* | *netbsd* | openbsd*)
            asm_arch=x86_64
            ;;
        esac
        ;;
    sparc*)
        case "$host_os" in
        linux*)
            asm_arch=sparc
            ;;
        esac
        ;;
    powerpc64le)
        case "$host_os" in
        linux*)
            asm_arch=ppc64le
            ;;
        esac
        ;;
    aarch64)
        case "$host_os" in
        linux*)
            asm_arch=aarch64
            ;;
        esac
        ;;
    arm)
        case "$host_os" in
        linux*)
            asm_arch=arm
            ;;
        esac
        ;;
    esac

    case "$asm_arch" in
    x86)
        DEFINES="$DEFINES -DUSE_X86_ASM -DUSE_MMX_ASM -DUSE_3DNOW_ASM -DUSE_SSE_ASM"
        AC_MSG_RESULT([yes, x86])
        ;;
    x86_64|amd64)
        DEFINES="$DEFINES -DUSE_X86_64_ASM"
        AC_MSG_RESULT([yes, x86_64])
        ;;
    sparc)
        DEFINES="$DEFINES -DUSE_SPARC_ASM"
        AC_MSG_RESULT([yes, sparc])
        ;;
    ppc64le)
        DEFINES="$DEFINES -DUSE_PPC64LE_ASM"
        AC_MSG_RESULT([yes, ppc64le])
        ;;
    aarch64)
        DEFINES="$DEFINES -DUSE_AARCH64_ASM"
        AC_MSG_RESULT([yes, aarch64])
        ;;
    arm)
        DEFINES="$DEFINES -DUSE_ARM_ASM"
        AC_MSG_RESULT([yes, arm])
        ;;
    *)
        AC_MSG_RESULT([no, platform not supported])
        ;;
    esac
fi

AC_HEADER_MAJOR
AC_CHECK_HEADER([xlocale.h], [DEFINES="$DEFINES -DHAVE_XLOCALE_H"])
AC_CHECK_HEADER([sys/sysctl.h], [DEFINES="$DEFINES -DHAVE_SYS_SYSCTL_H"])
AC_CHECK_FUNC([strtof], [DEFINES="$DEFINES -DHAVE_STRTOF"])
AC_CHECK_FUNC([mkostemp], [DEFINES="$DEFINES -DHAVE_MKOSTEMP"])
AC_CHECK_FUNC([timespec_get], [DEFINES="$DEFINES -DHAVE_TIMESPEC_GET"])
AC_CHECK_FUNC([memfd_create], [DEFINES="$DEFINES -DHAVE_MEMFD_CREATE"])

AC_MSG_CHECKING([whether strtod has locale support])
AC_LINK_IFELSE([AC_LANG_SOURCE([[
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
       float f = strtof_l(s, end, loc);
       freelocale(loc);
       return 0;
    }]])],
  [DEFINES="$DEFINES -DHAVE_STRTOD_L"];
   AC_MSG_RESULT([yes]),
   AC_MSG_RESULT([no]))

dnl Check to see if dlopen is in default libraries (like Solaris, which
dnl has it in libc), or if libdl is needed to get it.
AC_CHECK_FUNC([dlopen], [],
    [AC_CHECK_LIB([dl], [dlopen],
       [DLOPEN_LIBS="-ldl"])])
AC_SUBST([DLOPEN_LIBS])

dnl Check if that library also has dladdr
save_LIBS="$LIBS"
LIBS="$LIBS $DLOPEN_LIBS"
AC_CHECK_FUNCS([dladdr])
LIBS="$save_LIBS"

AC_CHECK_FUNC([dl_iterate_phdr], [DEFINES="$DEFINES -DHAVE_DL_ITERATE_PHDR"])

case "$host_os" in
darwin*)
    ;;
*)
    AC_CHECK_FUNCS([clock_gettime], [CLOCK_LIB=],
                   [AC_CHECK_LIB([rt], [clock_gettime], [CLOCK_LIB=-lrt],
                                 [AC_MSG_ERROR([Could not find clock_gettime])])])
    AC_SUBST([CLOCK_LIB])
    ;;
esac

dnl See if posix_memalign is available
AC_CHECK_FUNC([posix_memalign], [DEFINES="$DEFINES -DHAVE_POSIX_MEMALIGN"])

dnl Check for zlib
PKG_CHECK_MODULES([ZLIB], [zlib >= $ZLIB_REQUIRED])
DEFINES="$DEFINES -DHAVE_ZLIB"

dnl Check for pthreads
AX_PTHREAD
if test "x$ax_pthread_ok" = xno; then
    AC_MSG_ERROR([Building mesa on this platform requires pthreads])
fi
dnl AX_PTHREADS leaves PTHREAD_LIBS empty for gcc and sets PTHREAD_CFLAGS
dnl to -pthread, which causes problems if we need -lpthread to appear in
dnl pkgconfig files.  Since Android doesn't have a pthread lib, this check
dnl is not valid for that platform.
if test "x$android" = xno; then
    test -z "$PTHREAD_LIBS" && PTHREAD_LIBS="-lpthread"
fi
dnl According to the manual when using pthreads, one should add -pthread to
dnl both compile and link-time arguments.
dnl In practise that should be sufficient for all platforms, since any
dnl platforms build with GCC and Clang support the flag.
PTHREAD_LIBS="$PTHREAD_LIBS -pthread"

dnl pthread-stubs is mandatory on BSD platforms, due to the nature of the
dnl project. Even then there's a notable issue as described in the project README
case "$host_os" in
linux* | cygwin* | darwin* | solaris* | *-gnu* | gnu*)
    pthread_stubs_possible="no"
    ;;
* )
    pthread_stubs_possible="yes"
    ;;
esac

if test "x$pthread_stubs_possible" = xyes; then
    PKG_CHECK_MODULES(PTHREADSTUBS, pthread-stubs >= 0.4)
fi

dnl Check for futex for fast inline simple_mtx_t.
AC_CHECK_HEADER([linux/futex.h], [DEFINES="$DEFINES -DHAVE_LINUX_FUTEX_H"])

dnl SELinux awareness.
AC_ARG_ENABLE([selinux],
    [AS_HELP_STRING([--enable-selinux],
        [Build SELinux-aware Mesa @<:@default=disabled@:>@])],
    [MESA_SELINUX="$enableval"],
    [MESA_SELINUX=no])
if test "x$enable_selinux" = "xyes"; then
    PKG_CHECK_MODULES([SELINUX], [libselinux], [],
        [AC_CHECK_HEADER([selinux/selinux.h],[],
                         [AC_MSG_ERROR([SELinux headers not found])])
         AC_CHECK_LIB([selinux],[is_selinux_enabled],[],
                      [AC_MSG_ERROR([SELinux library not found])])
         SELINUX_LIBS="-lselinux"])
    DEFINES="$DEFINES -DMESA_SELINUX"
fi
AC_SUBST([SELINUX_CFLAGS])
AC_SUBST([SELINUX_LIBS])

dnl
dnl LLVM
dnl
AC_ARG_ENABLE([llvm-shared-libs],
    [AS_HELP_STRING([--enable-llvm-shared-libs],
        [link with LLVM shared libraries @<:@default=enabled@:>@])],
    [enable_llvm_shared_libs="$enableval"],
    [enable_llvm_shared_libs=yes])

AC_ARG_WITH([llvm-prefix],
    [AS_HELP_STRING([--with-llvm-prefix],
        [Prefix for LLVM installations in non-standard locations])],
    [llvm_prefix="$withval"],
    [llvm_prefix=''])

PKG_CHECK_MODULES([LIBELF], [libelf], [have_libelf=yes], [have_libelf=no])
if test "x$have_libelf" = xno; then
   LIBELF_LIBS=''
   LIBELF_CFLAGS=''
   AC_CHECK_LIB([elf], [elf_memory], [have_libelf=yes;LIBELF_LIBS=-lelf], [have_libelf=no])
   AC_SUBST([LIBELF_LIBS])
   AC_SUBST([LIBELF_CFLAGS])
fi

if test -z "$LLVM_CONFIG"; then
    if test -n "$llvm_prefix"; then
        AC_PATH_TOOL([LLVM_CONFIG], [llvm-config], [no], ["$llvm_prefix/bin"])
    else
        AC_PATH_TOOL([LLVM_CONFIG], [llvm-config], [no])
    fi
fi

llvm_add_component() {
    new_llvm_component=$1
    driver_name=$2

    if $LLVM_CONFIG --components | grep -iqw $new_llvm_component ; then
        LLVM_COMPONENTS="${LLVM_COMPONENTS} ${new_llvm_component}"
    else
        AC_MSG_ERROR([LLVM component '$new_llvm_component' not enabled in your LLVM build. Required by $driver_name.])
    fi
}

llvm_add_optional_component() {
    new_llvm_component=$1
    driver_name=$2

    if $LLVM_CONFIG --components | grep -iqw $new_llvm_component ; then
        LLVM_COMPONENTS="${LLVM_COMPONENTS} ${new_llvm_component}"
    fi
}

llvm_add_default_components() {
    driver_name=$1

    # Required default components
    llvm_add_component "bitwriter" $driver_name
    llvm_add_component "engine" $driver_name
    llvm_add_component "mcdisassembler" $driver_name
    llvm_add_component "mcjit" $driver_name

    # Optional default components
    llvm_add_optional_component "inteljitevents" $driver_name
}

llvm_add_target() {
    new_llvm_target=$1
    driver_name=$2

    if $LLVM_CONFIG --targets-built | grep -iqw $new_llvm_target ; then
        llvm_add_component $new_llvm_target $driver_name
    else
        AC_MSG_ERROR([LLVM target '$new_llvm_target' not enabled in your LLVM build. Required by $driver_name.])
    fi
}

# Call this inside ` ` to get the return value.
# $1 is the llvm-config command with arguments.
strip_unwanted_llvm_flags() {
    echo " `$1` " | sed -E \
        -e 's/[[[:space:]]]+-m[[^[:space:]]]*//g' \
        -e 's/[[[:space:]]]+-DNDEBUG[[[:space:]]]/ /g' \
        -e 's/[[[:space:]]]+-D_GNU_SOURCE[[[:space:]]]/ /g' \
        -e 's/[[[:space:]]]+-pedantic[[[:space:]]]/ /g' \
        -e 's/[[[:space:]]]+-W[[^[:space:]]]*//g' \
        -e 's/[[[:space:]]]+-O[[^[:space:]]]*//g' \
        -e 's/[[[:space:]]]+-g[[^[:space:]]]*//g' \
        -e 's/-fno-rtti[[[:space:]]]/-Fno-rtti /g' \
        -e 's/[[[:space:]]]+-f[[^[:space:]]]*//g' \
        -e 's/-Fno-rtti[[[:space:]]]/-fno-rtti /g' \
        -e 's/^[[[:space:]]]//' \
        -e 's/[[[:space:]]]$//'
}

llvm_set_environment_variables() {
    if test "x$LLVM_CONFIG" != xno; then
        LLVM_VERSION=`$LLVM_CONFIG --version | egrep -o '^[[0-9.]]+'`
        LLVM_CPPFLAGS=`strip_unwanted_llvm_flags "$LLVM_CONFIG --cppflags"`
        LLVM_INCLUDEDIR=`$LLVM_CONFIG --includedir`
        LLVM_LIBDIR=`$LLVM_CONFIG --libdir`

        # We need to respect LLVM_CPPFLAGS when compiling LLVM headers.
        save_CFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS $LLVM_CPPFLAGS"

        AC_COMPUTE_INT([LLVM_VERSION_MAJOR], [LLVM_VERSION_MAJOR],
            [#include "${LLVM_INCLUDEDIR}/llvm/Config/llvm-config.h"])
        AC_COMPUTE_INT([LLVM_VERSION_MINOR], [LLVM_VERSION_MINOR],
            [#include "${LLVM_INCLUDEDIR}/llvm/Config/llvm-config.h"])
        AC_COMPUTE_INT([LLVM_VERSION_PATCH], [LLVM_VERSION_PATCH],
            [#include "${LLVM_INCLUDEDIR}/llvm/Config/llvm-config.h"])

        CFLAGS="$save_CFLAGS"

        # Only needed for LLVM < 3.6.0
        if test -z "$LLVM_VERSION_PATCH"; then
            LLVM_VERSION_PATCH=0
        fi

        LLVM_VERSION_INT="${LLVM_VERSION_MAJOR}0${LLVM_VERSION_MINOR}"

        FOUND_LLVM=yes
    else
        FOUND_LLVM=no
        LLVM_VERSION_INT=0
    fi
}

require_llvm() {
    if test "x$enable_llvm" != "xyes"; then
        AC_MSG_ERROR([--enable-llvm is required when building $1])
    fi
}

llvm_require_version() {
    require_llvm $2

    llvm_target_version_major=`echo $1 | cut -d. -f1 | egrep -o '^[[0-9]]+'`
    llvm_target_version_minor=`echo $1 | cut -d. -f2 | egrep -o '^[[0-9]]+'`
    llvm_target_version_patch=`echo $1 | cut -d. -f3 | egrep -o '^[[0-9]]+'`

    if test "$LLVM_VERSION_MAJOR" -gt "$llvm_target_version_major"; then
        # major > required major
        #  --> OK
        return
    fi

    if test "$LLVM_VERSION_MAJOR" -eq "$llvm_target_version_major"; then
        if test "$LLVM_VERSION_MINOR" -gt "$llvm_target_version_minor"; then
            # major = required major and
            # minor > required minor
            #  --> OK
            return
        else
            if test "$LLVM_VERSION_MINOR" -eq "$llvm_target_version_minor"; then
                if test "$LLVM_VERSION_PATCH" -ge "$llvm_target_version_patch"; then
                    # major = required major and
                    # minor = required minor and
                    # patch >= required patch
                    #  --> OK
                    return
                fi
            fi
        fi
    fi

    AC_MSG_ERROR([LLVM $1 or newer is required for $2])
}

radeon_llvm_check() {
    if test ${LLVM_VERSION_INT} -lt 307; then
        amdgpu_llvm_target_name='r600'
    else
        amdgpu_llvm_target_name='amdgpu'
    fi

    llvm_require_version $*

    llvm_add_target $amdgpu_llvm_target_name $2

    llvm_add_default_components $2
    llvm_add_component "bitreader" $2
    llvm_add_component "ipo" $2

    if test "x$have_libelf" != xyes; then
       AC_MSG_ERROR([$2 requires libelf when using llvm])
    fi
}

llvm_set_environment_variables

AC_SUBST([LLVM_CFLAGS])
AC_SUBST([LLVM_CXXFLAGS])
AC_SUBST([LLVM_LIBS])
AC_SUBST([LLVM_LDFLAGS])
AC_SUBST([LLVM_INCLUDEDIR])

dnl
dnl libunwind
dnl
PKG_CHECK_EXISTS(libunwind, [HAVE_LIBUNWIND=yes], [HAVE_LIBUNWIND=no])
AC_ARG_ENABLE([libunwind],
    [AS_HELP_STRING([--enable-libunwind],
            [Use libunwind for backtracing (default: auto)])],
        [LIBUNWIND="$enableval"],
        [LIBUNWIND="$HAVE_LIBUNWIND"])

if test "x$LIBUNWIND" = "xyes"; then
    PKG_CHECK_MODULES(LIBUNWIND, libunwind)
    AC_DEFINE(HAVE_LIBUNWIND, 1, [Have libunwind support])
fi


dnl Options for APIs
AC_ARG_ENABLE([opengl],
    [AS_HELP_STRING([--disable-opengl],
        [disable support for standard OpenGL API @<:@default=enabled@:>@])],
    [enable_opengl="$enableval"],
    [enable_opengl=yes])
AC_ARG_ENABLE([gles1],
    [AS_HELP_STRING([--disable-gles1],
        [disable support for OpenGL ES 1.x API @<:@default=enabled@:>@])],
    [enable_gles1="$enableval"],
    [enable_gles1=yes])
AC_ARG_ENABLE([gles2],
    [AS_HELP_STRING([--disable-gles2],
        [disable support for OpenGL ES 2.x API @<:@default=enabled@:>@])],
    [enable_gles2="$enableval"],
    [enable_gles2=yes])

AC_ARG_ENABLE([dri],
    [AS_HELP_STRING([--enable-dri],
        [enable DRI modules @<:@default=enabled@:>@])],
    [enable_dri="$enableval"],
    [enable_dri=yes])

AC_ARG_ENABLE([gallium-extra-hud],
    [AS_HELP_STRING([--enable-gallium-extra-hud],
        [enable HUD block/NIC I/O HUD stats support @<:@default=disabled@:>@])],
    [enable_gallium_extra_hud="$enableval"],
    [enable_gallium_extra_hud=no])
AM_CONDITIONAL(HAVE_GALLIUM_EXTRA_HUD, test "x$enable_gallium_extra_hud" = xyes)
if test "x$enable_gallium_extra_hud" = xyes ; then
    DEFINES="${DEFINES} -DHAVE_GALLIUM_EXTRA_HUD=1"
fi

#TODO: no pkgconfig .pc available for libsensors.
#PKG_CHECK_MODULES([LIBSENSORS], [libsensors >= $LIBSENSORS_REQUIRED], [enable_lmsensors=yes], [enable_lmsensors=no])
AC_ARG_ENABLE([lmsensors],
    [AS_HELP_STRING([--enable-lmsensors],
        [enable HUD lmsensor support @<:@default=disabled@:>@])],
    [enable_lmsensors="$enableval"],
    [enable_lmsensors=no])
AM_CONDITIONAL(HAVE_LIBSENSORS, test "x$enable_lmsensors" = xyes)
if test "x$enable_lmsensors" = xyes ; then
    DEFINES="${DEFINES} -DHAVE_LIBSENSORS=1"
    LIBSENSORS_LIBS="-lsensors"
else
    LIBSENSORS_LIBS=""
fi
AC_SUBST(LIBSENSORS_LIBS)

case "$host_os" in
linux*)
    dri3_default=yes
    ;;
*)
    dri3_default=no
    ;;
esac

if test "x$enable_dri" = xno; then
    dri3_default=no
fi

AC_ARG_ENABLE([dri3],
    [AS_HELP_STRING([--enable-dri3],
        [enable DRI3 @<:@default=auto@:>@])],
    [enable_dri3="$enableval"],
    [enable_dri3="$dri3_default"])
AC_ARG_ENABLE([glx],
    [AS_HELP_STRING([--enable-glx@<:@=dri|xlib|gallium-xlib@:>@],
        [enable the GLX library and choose an implementation @<:@default=auto@:>@])],
    [enable_glx="$enableval"],
    [enable_glx=yes])
AC_ARG_ENABLE([osmesa],
    [AS_HELP_STRING([--enable-osmesa],
        [enable OSMesa library @<:@default=disabled@:>@])],
    [enable_osmesa="$enableval"],
    [enable_osmesa=no])
AC_ARG_ENABLE([gallium-osmesa],
    [AS_HELP_STRING([--enable-gallium-osmesa],
        [enable Gallium implementation of the OSMesa library @<:@default=disabled@:>@])],
    [enable_gallium_osmesa="$enableval"],
    [enable_gallium_osmesa=no])
AC_ARG_ENABLE([egl],
    [AS_HELP_STRING([--disable-egl],
        [disable EGL library @<:@default=enabled@:>@])],
    [enable_egl="$enableval"],
    [enable_egl=yes])

AC_ARG_ENABLE([xa],
    [AS_HELP_STRING([--enable-xa],
        [enable build of the XA X Acceleration API @<:@default=disabled@:>@])],
    [enable_xa="$enableval"],
    [enable_xa=no])
AC_ARG_ENABLE([gbm],
   [AS_HELP_STRING([--enable-gbm],
         [enable gbm library @<:@default=yes except cygwin and macOS@:>@])],
   [enable_gbm="$enableval"],
   [case "$host_os" in
       cygwin* | darwin*)
          enable_gbm=no
          ;;
       *)
          enable_gbm=yes
          ;;
    esac])
AC_ARG_ENABLE([nine],
    [AS_HELP_STRING([--enable-nine],
        [enable build of the nine Direct3D9 API @<:@default=no@:>@])],
    [enable_nine="$enableval"],
    [enable_nine=no])

AC_ARG_ENABLE([xvmc],
   [AS_HELP_STRING([--enable-xvmc],
         [enable xvmc library @<:@default=auto@:>@])],
   [enable_xvmc="$enableval"],
   [enable_xvmc=auto])
AC_ARG_ENABLE([vdpau],
   [AS_HELP_STRING([--enable-vdpau],
         [enable vdpau library @<:@default=auto@:>@])],
   [enable_vdpau="$enableval"],
   [enable_vdpau=auto])
AC_ARG_ENABLE([omx],
   [AS_HELP_STRING([--enable-omx],
         [DEPRECATED: Use --enable-omx-bellagio instead @<:@default=auto@:>@])],
   [AC_MSG_ERROR([--enable-omx is deprecated. Use --enable-omx-bellagio instead.])],
   [])
AC_ARG_ENABLE([omx-bellagio],
   [AS_HELP_STRING([--enable-omx-bellagio],
         [enable OpenMAX Bellagio library @<:@default=disabled@:>@])],
   [enable_omx_bellagio="$enableval"],
   [enable_omx_bellagio=no])
AC_ARG_ENABLE([va],
   [AS_HELP_STRING([--enable-va],
         [enable va library @<:@default=auto@:>@])],
   [enable_va="$enableval"],
   [enable_va=auto])
AC_ARG_ENABLE([opencl],
   [AS_HELP_STRING([--enable-opencl],
         [enable OpenCL library @<:@default=disabled@:>@])],
   [enable_opencl="$enableval"],
   [enable_opencl=no])
AC_ARG_ENABLE([opencl_icd],
   [AS_HELP_STRING([--enable-opencl-icd],
          [Build an OpenCL ICD library to be loaded by an ICD implementation
           @<:@default=enabled@:>@])],
    [enable_opencl_icd="$enableval"],
    [enable_opencl_icd=yes])

AC_ARG_ENABLE([gallium-tests],
    [AS_HELP_STRING([--enable-gallium-tests],
        [Enable optional Gallium tests) @<:@default=disabled@:>@])],
    [enable_gallium_tests="$enableval"],
    [enable_gallium_tests=no])

# Option for Gallium drivers

# Keep this in sync with the --with-gallium-drivers help string default value
GALLIUM_DRIVERS_DEFAULT="r300,r600,svga,swrast"

AC_ARG_WITH([gallium-drivers],
    [AS_HELP_STRING([--with-gallium-drivers@<:@=DIRS...@:>@],
        [comma delimited Gallium drivers list, e.g.
        "i915,nouveau,r300,r600,radeonsi,freedreno,pl111,svga,swrast,swr,vc4,vc5,virgl,etnaviv,imx"
        @<:@default=r300,r600,svga,swrast@:>@])],
    [with_gallium_drivers="$withval"],
    [with_gallium_drivers="$GALLIUM_DRIVERS_DEFAULT"])

# Doing '--without-gallium-drivers' will set this variable to 'no'.  Clear it
# here so that the script doesn't choke on an unknown driver name later.
case "$with_gallium_drivers" in
    yes) with_gallium_drivers="$GALLIUM_DRIVERS_DEFAULT" ;;
    no) with_gallium_drivers='' ;;
esac

if test "x$enable_opengl" = xno -a \
        "x$enable_gles1" = xno -a \
        "x$enable_gles2" = xno -a \
        "x$enable_nine" = xno -a \
        "x$enable_xa" = xno -a \
        "x$enable_xvmc" = xno -a \
        "x$enable_vdpau" = xno -a \
        "x$enable_omx_bellagio" = xno -a \
        "x$enable_va" = xno -a \
        "x$enable_opencl" = xno; then
    AC_MSG_ERROR([at least one API should be enabled])
fi

# Building OpenGL ES1 and/or ES2 without OpenGL is not supported on mesa 9.0.x
if test "x$enable_opengl" = xno -a \
        "x$enable_gles1" = xyes; then
    AC_MSG_ERROR([Building OpenGL ES1 without OpenGL is not supported])
fi

if test "x$enable_opengl" = xno -a \
        "x$enable_gles2" = xyes; then
    AC_MSG_ERROR([Building OpenGL ES2 without OpenGL is not supported])
fi

AM_CONDITIONAL(HAVE_OPENGL, test "x$enable_opengl" = xyes)
AM_CONDITIONAL(HAVE_OPENGL_ES1, test "x$enable_gles1" = xyes)
AM_CONDITIONAL(HAVE_OPENGL_ES2, test "x$enable_gles2" = xyes)
AM_CONDITIONAL(NEED_OPENGL_COMMON, test "x$enable_opengl" = xyes -o \
                                        "x$enable_gles1" = xyes -o \
                                        "x$enable_gles2" = xyes)
AM_CONDITIONAL(NEED_KHRPLATFORM, test "x$enable_egl" = xyes -o \
                                      "x$enable_gles1" = xyes -o \
                                      "x$enable_gles2" = xyes)

# Validate GLX options
if test "x$enable_glx" = xyes; then
    if test "x$enable_dri" = xyes; then
        enable_glx=dri
    elif test -n "$with_gallium_drivers"; then
        enable_glx=gallium-xlib
    else
        enable_glx=xlib
    fi
fi
case "x$enable_glx" in
xdri | xxlib | xgallium-xlib)
    # GLX requires OpenGL
    if test "x$enable_opengl" = xno; then
        AC_MSG_ERROR([GLX cannot be built without OpenGL])
    fi

    # Check individual dependencies
    case "x$enable_glx" in
    xdri)
        if test "x$enable_dri" = xno; then
            AC_MSG_ERROR([DRI-based GLX requires DRI to be enabled])
        fi
        ;;
    xxlib)
        if test "x$enable_dri" = xyes; then
            AC_MSG_ERROR([Xlib-based GLX cannot be built with DRI enabled])
        fi
        ;;
    xgallium-xlib )
        if test "x$enable_dri" = xyes; then
            AC_MSG_ERROR([Xlib-based (Gallium) GLX cannot be built with DRI enabled])
        fi
        if test -z "$with_gallium_drivers"; then
            AC_MSG_ERROR([Xlib-based (Gallium) GLX cannot be built without Gallium enabled])
        fi
        ;;
    esac
    ;;
xno)
    ;;
*)
    AC_MSG_ERROR([Illegal value for --enable-glx: $enable_glx])
    ;;
esac

AM_CONDITIONAL(HAVE_GLX, test "x$enable_glx" != xno)
AM_CONDITIONAL(HAVE_DRI_GLX, test "x$enable_glx" = xdri)
AM_CONDITIONAL(HAVE_XLIB_GLX, test "x$enable_glx" = xxlib)
AM_CONDITIONAL(HAVE_GALLIUM_XLIB_GLX, test "x$enable_glx" = xgallium-xlib)

DEFAULT_GL_LIB_NAME=GL

dnl
dnl Libglvnd configuration
dnl
AC_ARG_ENABLE([libglvnd],
    [AS_HELP_STRING([--enable-libglvnd],
        [Build GLX and EGL for libglvnd @<:@default=disabled@:>@])],
    [enable_libglvnd="$enableval"],
    [enable_libglvnd=no])
AM_CONDITIONAL(USE_LIBGLVND, test "x$enable_libglvnd" = xyes)

if test "x$enable_libglvnd" = xyes ; then
    case "x$enable_glx" in
    xxlib | xgallium-xlib )
        AC_MSG_ERROR([cannot build libgvnd when Xlib-GLX or Gallium-Xlib-GLX is enabled])
        ;;
    esac

    PKG_CHECK_MODULES([GLVND], libglvnd >= 0.2.0)
    LIBGLVND_DATADIR=`$PKG_CONFIG --variable=datadir libglvnd`
    AC_SUBST([LIBGLVND_DATADIR])

    DEFINES="${DEFINES} -DUSE_LIBGLVND=1"
    DEFAULT_GL_LIB_NAME=GLX_mesa

    if test "x$enable_glx" = xno -a "x$enable_egl" = xno; then
        AC_MSG_ERROR([cannot build libglvnd without GLX or EGL])
    fi
fi

AC_ARG_WITH([gl-lib-name],
  [AS_HELP_STRING([--with-gl-lib-name@<:@=NAME@:>@],
    [specify GL library name @<:@default=GL@:>@])],
  [GL_LIB=$withval],
  [GL_LIB="$DEFAULT_GL_LIB_NAME"])
AC_ARG_WITH([osmesa-lib-name],
  [AS_HELP_STRING([--with-osmesa-lib-name@<:@=NAME@:>@],
    [specify OSMesa library name @<:@default=OSMesa@:>@])],
  [OSMESA_LIB=$withval],
  [OSMESA_LIB=OSMesa])
AS_IF([test "x$GL_LIB" = xyes], [GL_LIB="$DEFAULT_GL_LIB_NAME"])
AS_IF([test "x$OSMESA_LIB" = xyes], [OSMESA_LIB=OSMesa])

dnl
dnl Mangled Mesa support
dnl
AC_ARG_ENABLE([mangling],
  [AS_HELP_STRING([--enable-mangling],
    [enable mangled symbols and library name @<:@default=disabled@:>@])],
  [enable_mangling="${enableval}"],
  [enable_mangling=no]
)
if test "x${enable_mangling}" = "xyes" ; then
  DEFINES="${DEFINES} -DUSE_MGL_NAMESPACE"
  GL_LIB="Mangled${GL_LIB}"
  OSMESA_LIB="Mangled${OSMESA_LIB}"
fi
AC_SUBST([GL_LIB])
AC_SUBST([OSMESA_LIB])

# Check for libdrm
PKG_CHECK_MODULES([LIBDRM], [libdrm >= $LIBDRM_REQUIRED],
                  [have_libdrm=yes], [have_libdrm=no])
if test "x$have_libdrm" = xyes; then
    DEFINES="$DEFINES -DHAVE_LIBDRM"
fi

require_libdrm() {
    if test "x$have_libdrm" != xyes; then
       AC_MSG_ERROR([$1 requires libdrm >= $LIBDRM_REQUIRED])
    fi
}


# Select which platform-dependent DRI code gets built
case "$host_os" in
darwin*)
    dri_platform='apple' ;;
cygwin*)
    dri_platform='windows' ;;
gnu*)
    dri_platform='none' ;;
*)
    dri_platform='drm' ;;
esac

if test "x$enable_dri" = xyes -a "x$dri_platform" = xdrm -a "x$have_libdrm" = xyes; then
    have_drisw_kms='yes'
fi

AM_CONDITIONAL(HAVE_DRICOMMON, test "x$enable_dri" = xyes )
AM_CONDITIONAL(HAVE_DRISW, test "x$enable_dri" = xyes )
AM_CONDITIONAL(HAVE_DRISW_KMS, test "x$have_drisw_kms" = xyes )
AM_CONDITIONAL(HAVE_DRI2, test "x$enable_dri" = xyes -a "x$dri_platform" = xdrm -a "x$have_libdrm" = xyes )
AM_CONDITIONAL(HAVE_DRI3, test "x$enable_dri3" = xyes -a "x$dri_platform" = xdrm -a "x$have_libdrm" = xyes )
AM_CONDITIONAL(HAVE_APPLEDRI, test "x$enable_dri" = xyes -a "x$dri_platform" = xapple )
AM_CONDITIONAL(HAVE_LMSENSORS, test "x$enable_lmsensors" = xyes )
AM_CONDITIONAL(HAVE_GALLIUM_EXTRA_HUD, test "x$enable_gallium_extra_hud" = xyes )
AM_CONDITIONAL(HAVE_WINDOWSDRI, test "x$enable_dri" = xyes -a "x$dri_platform" = xwindows )

AC_ARG_ENABLE([shared-glapi],
    [AS_HELP_STRING([--enable-shared-glapi],
        [Enable shared glapi for OpenGL @<:@default=enabled@:>@])],
    [enable_shared_glapi="$enableval"],
    [enable_shared_glapi=yes])

case "x$enable_opengl$enable_gles1$enable_gles2" in
x*yes*yes*)
    if test "x$enable_shared_glapi" = xno; then
        AC_MSG_ERROR([shared GLAPI required when building two or more of
                      the following APIs - opengl, gles1 gles2])
    fi
    ;;
esac

AM_CONDITIONAL(HAVE_SHARED_GLAPI, test "x$enable_shared_glapi" = xyes)

# Build the pipe-drivers as separate libraries/modules.
# Do not touch this unless you know what you are doing.
# XXX: Expose via configure option ?
enable_shared_pipe_drivers=no

dnl
dnl Driver specific build directories
dnl

if test "x$enable_gallium_osmesa" = xyes; then
    if ! echo "$with_gallium_drivers" | grep -q 'swrast'; then
        AC_MSG_ERROR([gallium_osmesa requires the gallium swrast driver])
    fi
    if test "x$enable_osmesa" = xyes; then
        AC_MSG_ERROR([Cannot enable both classic and Gallium OSMesa implementations])
    fi
fi

require_dri_shared_libs_and_glapi() {
    if test "x$enable_static" = xyes; then
        AC_MSG_ERROR([$1 cannot be build as static library])
    fi

    if test "x$enable_dri" != xyes; then
        # There is only a single backend which won't be build/used otherwise.
        # XXX: Revisit this as the egl/haiku is a thing.
        AC_MSG_ERROR([$1 requires --enable-dri])
    fi

    if test "x$enable_shared_glapi" != xyes; then
        AC_MSG_ERROR([$1 requires --enable-shared-glapi])
    fi
}

if test "x$enable_dri" = xyes; then
    require_dri_shared_libs_and_glapi "DRI"

    # not a hard requirement as swrast does not depend on it
    if test "x$have_libdrm" = xyes; then
        DRI_PC_REQ_PRIV="libdrm >= $LIBDRM_REQUIRED"
    fi
fi

AC_ARG_ENABLE([driglx-direct],
    [AS_HELP_STRING([--disable-driglx-direct],
        [disable direct rendering in GLX and EGL for DRI \
            @<:@default=enabled@:>@])],
    [driglx_direct="$enableval"],
    [driglx_direct="yes"])

dnl
dnl libGL configuration per driver
dnl
if test "x$enable_glx" != xno; then
    PKG_CHECK_MODULES([GLPROTO], [glproto >= $GLPROTO_REQUIRED])
fi
case "x$enable_glx" in
xxlib | xgallium-xlib)
    # Xlib-based GLX
    dri_modules="x11 xext xcb"
    PKG_CHECK_MODULES([XLIBGL], [$dri_modules])
    GL_PC_REQ_PRIV="$GL_PC_REQ_PRIV $dri_modules"
    X11_INCLUDES="$X11_INCLUDES $XLIBGL_CFLAGS"
    GL_LIB_DEPS="$XLIBGL_LIBS"
    GL_LIB_DEPS="$GL_LIB_DEPS $SELINUX_LIBS -lm $PTHREAD_LIBS $DLOPEN_LIBS"
    GL_PC_LIB_PRIV="$GL_PC_LIB_PRIV $SELINUX_LIBS -lm $PTHREAD_LIBS"
    ;;
xdri)
    # DRI-based GLX

    # find the DRI deps for libGL
    dri_modules="x11 xext xdamage >= $XDAMAGE_REQUIRED xfixes x11-xcb xcb xcb-glx >= $XCBGLX_REQUIRED"

    if test x"$driglx_direct" = xyes; then
        if test x"$dri_platform" = xdrm ; then
            DEFINES="$DEFINES -DGLX_USE_DRM"
            require_libdrm "Direct rendering"

            PKG_CHECK_MODULES([DRI2PROTO], [dri2proto >= $DRI2PROTO_REQUIRED])
            GL_PC_REQ_PRIV="$GL_PC_REQ_PRIV libdrm >= $LIBDRM_REQUIRED"

            if test x"$enable_dri" = xyes; then
               dri_modules="$dri_modules xcb-dri2 >= $XCBDRI2_REQUIRED"
            fi
        fi
        if test x"$dri_platform" = xapple ; then
            DEFINES="$DEFINES -DGLX_USE_APPLEGL"
        fi
        if test x"$dri_platform" = xwindows ; then
            DEFINES="$DEFINES -DGLX_USE_WINDOWSGL"
        fi
    fi

    # add xf86vidmode if available
    PKG_CHECK_MODULES([XF86VIDMODE], [xxf86vm], HAVE_XF86VIDMODE=yes, HAVE_XF86VIDMODE=no)
    if test "$HAVE_XF86VIDMODE" = yes ; then
        dri_modules="$dri_modules xxf86vm"
    fi

    PKG_CHECK_MODULES([DRIGL], [$dri_modules])
    GL_PC_REQ_PRIV="$GL_PC_REQ_PRIV $dri_modules"
    X11_INCLUDES="$X11_INCLUDES $DRIGL_CFLAGS"
    GL_LIB_DEPS="$DRIGL_LIBS"

    # need DRM libs, $PTHREAD_LIBS, etc.
    GL_LIB_DEPS="$GL_LIB_DEPS $LIBDRM_LIBS -lm $PTHREAD_LIBS $DLOPEN_LIBS"
    GL_PC_LIB_PRIV="-lm $PTHREAD_LIBS $DLOPEN_LIBS"
    ;;
esac

# This is outside the case (above) so that it is invoked even for non-GLX
# builds.
AM_CONDITIONAL(HAVE_XF86VIDMODE, test "x$HAVE_XF86VIDMODE" = xyes)

GLESv1_CM_LIB_DEPS="$LIBDRM_LIBS -lm $PTHREAD_LIBS $DLOPEN_LIBS"
GLESv1_CM_PC_LIB_PRIV="-lm $PTHREAD_LIBS $DLOPEN_LIBS"
GLESv2_LIB_DEPS="$LIBDRM_LIBS -lm $PTHREAD_LIBS $DLOPEN_LIBS"
GLESv2_PC_LIB_PRIV="-lm $PTHREAD_LIBS $DLOPEN_LIBS"

AC_SUBST([X11_INCLUDES])
AC_SUBST([GL_LIB_DEPS])
AC_SUBST([GL_PC_REQ_PRIV])
AC_SUBST([GL_PC_LIB_PRIV])
AC_SUBST([GL_PC_CFLAGS])
AC_SUBST([DRI_PC_REQ_PRIV])
AC_SUBST([GLESv1_CM_LIB_DEPS])
AC_SUBST([GLESv1_CM_PC_LIB_PRIV])
AC_SUBST([GLESv2_LIB_DEPS])
AC_SUBST([GLESv2_PC_LIB_PRIV])

AC_SUBST([HAVE_XF86VIDMODE])

dnl
dnl More GLX setup
dnl
case "x$enable_glx" in
xxlib | xgallium-xlib)
    DEFINES="$DEFINES -DUSE_XSHM"
    ;;
xdri)
    DEFINES="$DEFINES -DGLX_INDIRECT_RENDERING"
    if test "x$driglx_direct" = xyes; then
        DEFINES="$DEFINES -DGLX_DIRECT_RENDERING"
    fi
    ;;
esac

dnl
dnl TLS detection
dnl

AC_ARG_ENABLE([glx-tls],
    [AS_HELP_STRING([--enable-glx-tls],
        [enable TLS support in GLX @<:@default=enabled@:>@])],
    [GLX_USE_TLS="$enableval"],
    [GLX_USE_TLS=yes])
AC_SUBST(GLX_TLS, ${GLX_USE_TLS})

if test "x$GLX_USE_TLS" = xyes; then
    DEFINES="$DEFINES -DGLX_USE_TLS"
fi

dnl Read-only text section on x86 hardened platforms
AC_ARG_ENABLE([glx-read-only-text],
    [AS_HELP_STRING([--enable-glx-read-only-text],
        [Disable writable .text section on x86 (decreases performance) @<:@default=disabled@:>@])],
    [enable_glx_read_only_text="$enableval"],
    [enable_glx_read_only_text=no])
if test "x$enable_glx_read_only_text" = xyes; then
    DEFINES="$DEFINES -DGLX_X86_READONLY_TEXT"
fi

dnl
dnl DEPRECATED: EGL Platforms configuration
dnl
AC_ARG_WITH([egl-platforms],
    [AS_HELP_STRING([--with-egl-platforms@<:@=DIRS...@:>@],
        [DEPRECATED: use --with-platforms instead@<:@default=auto@:>@])],
    [with_egl_platforms="$withval"],
    [with_egl_platforms=auto])

if test "x$with_egl_platforms" = xauto; then
    with_egl_platforms="x11,surfaceless"
    if test "x$enable_gbm" = xyes; then
        with_egl_platforms="$with_egl_platforms,drm"
    fi
else
    AC_MSG_WARN([--with-egl-platforms is deprecated. Use --with-platforms instead.])
fi

dnl
dnl Platforms configuration
dnl
AC_ARG_WITH([platforms],
    [AS_HELP_STRING([--with-platforms@<:@=DIRS...@:>@],
        [comma delimited native platforms libEGL/Vulkan/other supports, e.g.
        "x11,drm,wayland,surfaceless..." @<:@default=auto@:>@])],
    [with_platforms="$withval"],
    [with_platforms=auto])

# Reuse the autodetection rather than duplicating it.
if test "x$with_platforms" = xauto; then
    with_platforms=$with_egl_platforms
fi

PKG_CHECK_MODULES([WAYLAND_SCANNER], [wayland-scanner],
        WAYLAND_SCANNER=`$PKG_CONFIG --variable=wayland_scanner wayland-scanner`,
        WAYLAND_SCANNER='')
if test "x$WAYLAND_SCANNER" = x; then
    AC_PATH_PROG([WAYLAND_SCANNER], [wayland-scanner], [:])
fi

PKG_CHECK_EXISTS([wayland-protocols >= $WAYLAND_PROTOCOLS_REQUIRED], [have_wayland_protocols=yes], [have_wayland_protocols=no])
if test "x$have_wayland_protocols" = xyes; then
    ac_wayland_protocols_pkgdatadir=`$PKG_CONFIG --variable=pkgdatadir wayland-protocols`
fi
AC_SUBST(WAYLAND_PROTOCOLS_DATADIR, $ac_wayland_protocols_pkgdatadir)

# Do per platform setups and checks
platforms=`IFS=', '; echo $with_platforms`
for plat in $platforms; do
    case "$plat" in
    wayland)

        PKG_CHECK_MODULES([WAYLAND_CLIENT], [wayland-client >= $WAYLAND_REQUIRED])
        PKG_CHECK_MODULES([WAYLAND_SERVER], [wayland-server >= $WAYLAND_REQUIRED])

        if test "x$WAYLAND_SCANNER" = "x:"; then
                AC_MSG_ERROR([wayland-scanner is needed to compile the wayland platform])
        fi
        if test "x$have_wayland_protocols" = xno; then
                AC_MSG_ERROR([wayland-protocols >= $WAYLAND_PROTOCOLS_REQUIRED is needed to compile the wayland platform])
        fi
        DEFINES="$DEFINES -DHAVE_WAYLAND_PLATFORM -DWL_HIDE_DEPRECATED"
        ;;

    x11)
        PKG_CHECK_MODULES([XCB_DRI2], [x11-xcb xcb xcb-dri2 >= $XCBDRI2_REQUIRED xcb-xfixes])
        DEFINES="$DEFINES -DHAVE_X11_PLATFORM"
        ;;

    drm)
        test "x$enable_gbm" = "xno" &&
                AC_MSG_ERROR([EGL platform drm needs gbm])
        DEFINES="$DEFINES -DHAVE_DRM_PLATFORM"
        ;;

    surfaceless)
        DEFINES="$DEFINES -DHAVE_SURFACELESS_PLATFORM"
        ;;

    android)
        PKG_CHECK_MODULES([ANDROID], [cutils hardware sync])
        DEFINES="$DEFINES -DHAVE_ANDROID_PLATFORM"
        ;;

    *)
        AC_MSG_ERROR([platform '$plat' does not exist])
        ;;
    esac

    case "$plat" in
    wayland|drm|surfaceless)
        require_libdrm "Platform $plat"
        ;;
    esac
done

if test "x$enable_glx" != xno; then
    if ! echo "$platforms" | grep -q 'x11'; then
        AC_MSG_ERROR([Building GLX without the x11 platform is not supported])
    fi
fi

if test x"$enable_dri3" = xyes; then
    DEFINES="$DEFINES -DHAVE_DRI3"

    dri3_modules="x11-xcb xcb >= $XCB_REQUIRED xcb-dri3 xcb-xfixes xcb-present xcb-sync xshmfence >= $XSHMFENCE_REQUIRED"
    PKG_CHECK_MODULES([XCB_DRI3], [$dri3_modules])
fi

AM_CONDITIONAL(HAVE_PLATFORM_X11, echo "$platforms" | grep -q 'x11')
AM_CONDITIONAL(HAVE_PLATFORM_WAYLAND, echo "$platforms" | grep -q 'wayland')
AM_CONDITIONAL(HAVE_PLATFORM_DRM, echo "$platforms" | grep -q 'drm')
AM_CONDITIONAL(HAVE_PLATFORM_SURFACELESS, echo "$platforms" | grep -q 'surfaceless')
AM_CONDITIONAL(HAVE_PLATFORM_ANDROID, echo "$platforms" | grep -q 'android')

dnl
dnl More DRI setup
dnl
dnl Directory for DRI drivers
AC_ARG_WITH([dri-driverdir],
    [AS_HELP_STRING([--with-dri-driverdir=DIR],
        [directory for the DRI drivers @<:@${libdir}/dri@:>@])],
    [DRI_DRIVER_INSTALL_DIR="$withval"],
    [DRI_DRIVER_INSTALL_DIR='${libdir}/dri'])
AC_SUBST([DRI_DRIVER_INSTALL_DIR])
dnl Extra search path for DRI drivers
AC_ARG_WITH([dri-searchpath],
    [AS_HELP_STRING([--with-dri-searchpath=DIRS...],
        [semicolon delimited DRI driver search directories @<:@${libdir}/dri@:>@])],
    [DRI_DRIVER_SEARCH_DIR="$withval"],
    [DRI_DRIVER_SEARCH_DIR='${DRI_DRIVER_INSTALL_DIR}'])
AC_SUBST([DRI_DRIVER_SEARCH_DIR])
dnl Which drivers to build - default is chosen by platform
AC_ARG_WITH([dri-drivers],
    [AS_HELP_STRING([--with-dri-drivers@<:@=DIRS...@:>@],
        [comma delimited classic DRI drivers list, e.g.
        "i915,i965,nouveau,radeon,r200,swrast" @<:@default=auto@:>@])],
    [with_dri_drivers="$withval"],
    [with_dri_drivers=auto])

if test "x$with_dri_drivers" = xauto; then
    if test "x$enable_opengl" = xyes -a "x$enable_dri" = xyes; then
        with_dri_drivers="yes"
    else
        with_dri_drivers="no"
    fi
fi
if test "x$with_dri_drivers" = xno; then
    with_dri_drivers=''
fi

# Check for expat
PKG_CHECK_MODULES([EXPAT], [expat],,
    [PKG_CHECK_MODULES([EXPAT], [expat21])]
)

dnl If $with_dri_drivers is yes, drivers will be added through
dnl platform checks. Set DEFINES and LIB_DEPS
if test "x$enable_dri" = xyes; then
    # Platform specific settings and drivers to build
    case "$host_os" in
    linux*)
        case "$host_cpu" in
        powerpc* | sparc*)
            # Build only the drivers for cards that exist on PowerPC/sparc
            if test "x$with_dri_drivers" = "xyes"; then
                with_dri_drivers="r200 radeon swrast"
            fi
            ;;
        esac
        ;;
    cygwin*)
        if test "x$with_dri_drivers" = "xyes"; then
            with_dri_drivers="swrast"
        fi
        ;;
    darwin*)
        DEFINES="$DEFINES -DBUILDING_MESA"
        if test "x$with_dri_drivers" = "xyes"; then
            with_dri_drivers="swrast"
        fi
        ;;
    esac

    # default drivers
    if test "x$with_dri_drivers" = "xyes"; then
        with_dri_drivers="i915 i965 nouveau r200 radeon swrast"
    fi

    # put all the necessary libs together
    DRI_LIB_DEPS="$DRI_LIB_DEPS $SELINUX_LIBS $LIBDRM_LIBS $EXPAT_LIBS -lm $PTHREAD_LIBS $DLOPEN_LIBS"
fi

AC_SUBST([DRI_LIB_DEPS])

DRI_DIRS=''
dnl Duplicates in DRI_DIRS are removed by sorting it at the end of this block
if test -n "$with_dri_drivers"; then
    if test "x$enable_opengl" != xyes; then
        AC_MSG_ERROR([--with-dri-drivers requires OpenGL])
    fi

    dri_drivers=`IFS=', '; echo $with_dri_drivers`
    for driver in $dri_drivers; do
        DRI_DIRS="$DRI_DIRS $driver"
        case "x$driver" in
        xi915)
            require_libdrm "i915"
            HAVE_I915_DRI=yes
            PKG_CHECK_MODULES([I915], [libdrm >= $LIBDRM_INTEL_REQUIRED libdrm_intel >= $LIBDRM_INTEL_REQUIRED])
            ;;
        xi965)
            require_libdrm "i965"
            HAVE_I965_DRI=yes
            ;;
        xnouveau)
            require_libdrm "nouveau"
            HAVE_NOUVEAU_DRI=yes
            PKG_CHECK_MODULES([NVVIEUX], [libdrm >= $LIBDRM_NVVIEUX_REQUIRED libdrm_nouveau >= $LIBDRM_NVVIEUX_REQUIRED])
            ;;
        xradeon)
            require_libdrm "radeon"
            HAVE_RADEON_DRI=yes;
            PKG_CHECK_MODULES([RADEON], [libdrm >= $LIBDRM_RADEON_REQUIRED libdrm_radeon >= $LIBDRM_RADEON_REQUIRED])
            ;;
        xr200)
            require_libdrm "r200"
            HAVE_R200_DRI=yes
            PKG_CHECK_MODULES([RADEON], [libdrm >= $LIBDRM_RADEON_REQUIRED libdrm_radeon >= $LIBDRM_RADEON_REQUIRED])
            ;;
        xswrast)
            HAVE_SWRAST_DRI=yes
            ;;
        *)
            AC_MSG_ERROR([classic DRI driver '$driver' does not exist])
            ;;
        esac
    done
    DRI_DIRS=`echo $DRI_DIRS|tr " " "\n"|sort -u|tr "\n" " "`
fi


dnl
dnl Gallium LLVM
dnl Deprecated: kept for backwards compatibility
dnl
AC_ARG_ENABLE([gallium-llvm],
    [AS_HELP_STRING([--enable-gallium-llvm],
        [DEPRECATED: use --enable-llvm instead])],
    [enable_gallium_llvm="$enableval"],
    [enable_gallium_llvm=auto])

if test "x$enable_gallium_llvm" != xauto; then
   AC_MSG_WARN([The --enable-gallium-llvm option has been deprecated. Use --enable-llvm instead.])
   enable_llvm=$enable_gallium_llvm
fi

dnl
dnl LLVM
dnl
AC_ARG_ENABLE([llvm],
    [AS_HELP_STRING([--enable-llvm],
        [build with LLVM support @<:@default=enabled on x86/x86_64@:>@])],
    [enable_llvm="$enableval"],
    [enable_llvm=auto])

if test "x$enable_llvm" = xauto -a "x$FOUND_LLVM" = xyes; then
    if test "x$FOUND_LLVM" = xyes; then
        case "$host_cpu" in
        i*86|x86_64|amd64) enable_llvm=yes;;
        *) enable_llvm=no;;
        esac
    else
        enable_llvm=no
    fi
fi

if test "x$enable_llvm" = xyes -a "x$FOUND_LLVM" = xno; then
    AC_MSG_ERROR([--enable-llvm selected but llvm-config is not found])
fi

#
# Vulkan driver configuration
#

AC_ARG_WITH([vulkan-drivers],
    [AS_HELP_STRING([--with-vulkan-drivers@<:@=DIRS...@:>@],
        [comma delimited Vulkan drivers list, e.g.
        "intel,radeon"
        @<:@default=no@:>@])],
    [with_vulkan_drivers="$withval"],
    [with_vulkan_drivers="no"])

# Doing '--without-vulkan-drivers' will set this variable to 'no'.  Clear it
# here so that the script doesn't choke on an unknown driver name later.
case "x$with_vulkan_drivers" in
    xyes) with_vulkan_drivers="$VULKAN_DRIVERS_DEFAULT" ;;
    xno) with_vulkan_drivers='' ;;
esac

AC_ARG_WITH([vulkan-icddir],
    [AS_HELP_STRING([--with-vulkan-icddir=DIR],
        [directory for the Vulkan driver icd files @<:@${datarootdir}/vulkan/icd.d@:>@])],
    [VULKAN_ICD_INSTALL_DIR="$withval"],
    [VULKAN_ICD_INSTALL_DIR='${datarootdir}/vulkan/icd.d'])
AC_SUBST([VULKAN_ICD_INSTALL_DIR])

require_x11_dri3() {
    if echo "$platforms" | grep -q 'x11'; then
        if test "x$enable_dri3" != xyes; then
            AC_MSG_ERROR([$1 Vulkan driver requires DRI3 when built with X11])
        fi
    fi
}

if test -n "$with_vulkan_drivers"; then
    if test "x$ac_cv_func_dl_iterate_phdr" = xno; then
        AC_MSG_ERROR([Vulkan drivers require the dl_iterate_phdr function])
    fi

    VULKAN_DRIVERS=`IFS=', '; echo $with_vulkan_drivers`
    for driver in $VULKAN_DRIVERS; do
        case "x$driver" in
        xintel)
            require_libdrm "ANV"
            require_x11_dri3 "ANV"
            HAVE_INTEL_VULKAN=yes
            ;;
        xradeon)
            require_libdrm "radv"
            PKG_CHECK_MODULES([AMDGPU], [libdrm >= $LIBDRM_AMDGPU_REQUIRED libdrm_amdgpu >= $LIBDRM_AMDGPU_REQUIRED])
            radeon_llvm_check $LLVM_REQUIRED_RADV "radv"
            require_x11_dri3 "radv"
            HAVE_RADEON_VULKAN=yes
            ;;
        *)
            AC_MSG_ERROR([Vulkan driver '$driver' does not exist])
            ;;
        esac
    done
    VULKAN_DRIVERS=`echo $VULKAN_DRIVERS|tr " " "\n"|sort -u|tr "\n" " "`
fi


DEFINES="$DEFINES -DENABLE_SHADER_CACHE"
AM_CONDITIONAL(NEED_MEGADRIVER, test -n "$DRI_DIRS")
AM_CONDITIONAL(NEED_LIBMESA, test "x$enable_glx" = xxlib -o \
                                  "x$enable_osmesa" = xyes -o \
                                  -n "$DRI_DIRS")

dnl
dnl OSMesa configuration
dnl

dnl Configure the channel bits for OSMesa (libOSMesa, libOSMesa16, ...)
AC_ARG_WITH([osmesa-bits],
    [AS_HELP_STRING([--with-osmesa-bits=BITS],
        [OSMesa channel bits and library name: 8, 16, 32 @<:@default=8@:>@])],
    [osmesa_bits="$withval"],
    [osmesa_bits=8])
if test "x$osmesa_bits" != x8; then
    if test "x$enable_dri" = xyes -o "x$enable_glx" != xno; then
        AC_MSG_WARN([Ignoring OSMesa channel bits because of non-OSMesa driver])
        osmesa_bits=8
    fi
fi
case "x$osmesa_bits" in
x8)
    OSMESA_LIB="${OSMESA_LIB}"
    ;;
x16|x32)
    OSMESA_LIB="${OSMESA_LIB}$osmesa_bits"
    DEFINES="$DEFINES -DCHAN_BITS=$osmesa_bits -DDEFAULT_SOFTWARE_DEPTH_BITS=31"
    ;;
*)
    AC_MSG_ERROR([OSMesa bits '$osmesa_bits' is not a valid option])
    ;;
esac

if test "x$enable_osmesa" = xyes -o "x$enable_gallium_osmesa" = xyes; then
    # only link libraries with osmesa if shared
    if test "$enable_static" = no; then
        OSMESA_LIB_DEPS="-lm $PTHREAD_LIBS $SELINUX_LIBS $DLOPEN_LIBS"
    else
        OSMESA_LIB_DEPS=""
    fi
    OSMESA_PC_LIB_PRIV="-lm $PTHREAD_LIBS $SELINUX_LIBS $DLOPEN_LIBS"
fi

AC_SUBST([OSMESA_LIB_DEPS])
AC_SUBST([OSMESA_PC_REQ])
AC_SUBST([OSMESA_PC_LIB_PRIV])

dnl
dnl gbm configuration
dnl
if test "x$enable_gbm" = xyes; then
    require_dri_shared_libs_and_glapi "gbm"
fi
AM_CONDITIONAL(HAVE_GBM, test "x$enable_gbm" = xyes)
# FINISHME: GBM has a number of dependencies which we should add below
GBM_PC_REQ_PRIV=""
GBM_PC_LIB_PRIV="$DLOPEN_LIBS"
AC_SUBST([GBM_PC_REQ_PRIV])
AC_SUBST([GBM_PC_LIB_PRIV])

dnl
dnl EGL configuration
dnl

if test "x$enable_egl" = xyes; then
    EGL_LIB_DEPS="$DLOPEN_LIBS $SELINUX_LIBS $PTHREAD_LIBS"

    AC_CHECK_FUNC(mincore, [DEFINES="$DEFINES -DHAVE_MINCORE"])

    require_dri_shared_libs_and_glapi "egl"
fi
AM_CONDITIONAL(HAVE_EGL, test "x$enable_egl" = xyes)
AC_SUBST([EGL_LIB_DEPS])

gallium_st="mesa"

dnl
dnl XA configuration
dnl
if test "x$enable_xa" = xyes; then
    if test "x$with_gallium_drivers" = xswrast; then
       AC_MSG_ERROR([
          Building xa requires at least one non swrast gallium driver.
          If you are looking to use libxatracker.so with the VMware driver,
          make sure to include svga in the gallium drivers list, apart from
          enabling XA.
          Example: ./configure --enable-xa --with-gallium-drivers=svga...])
    fi
    gallium_st="$gallium_st xa"
fi
AM_CONDITIONAL(HAVE_ST_XA, test "x$enable_xa" = xyes)

if echo $platforms | grep -q "x11"; then
    have_xvmc_platform=yes
else
    have_xvmc_platform=no
fi

if echo $platforms | grep -q "x11"; then
    have_vdpau_platform=yes
else
    have_vdpau_platform=no
fi

if echo $platforms | grep -q "x11\|drm"; then
    have_omx_platform=yes
else
    have_omx_platform=no
fi

if echo $platforms | grep -q "x11\|drm\|wayland"; then
    have_va_platform=yes
else
    have_va_platform=no
fi

dnl
dnl Gallium G3DVL configuration
dnl
if test -n "$with_gallium_drivers" -a "x$with_gallium_drivers" != xswrast; then
    if test "x$enable_xvmc" = xauto -a "x$have_xvmc_platform" = xyes; then
        PKG_CHECK_EXISTS([xvmc >= $XVMC_REQUIRED], [enable_xvmc=yes], [enable_xvmc=no])
    fi

    if test "x$enable_vdpau" = xauto -a "x$have_vdpau_platform" = xyes; then
        PKG_CHECK_EXISTS([vdpau >= $VDPAU_REQUIRED], [enable_vdpau=yes], [enable_vdpau=no])
    fi

    if test "x$enable_omx_bellagio" = xauto -a "x$have_omx_platform" = xyes; then
        PKG_CHECK_EXISTS([libomxil-bellagio >= $LIBOMXIL_BELLAGIO_REQUIRED], [enable_omx_bellagio=yes], [enable_omx_bellagio=no])
    fi

    if test "x$enable_va" = xauto -a "x$have_va_platform" = xyes; then
        PKG_CHECK_EXISTS([libva >= $LIBVA_REQUIRED], [enable_va=yes], [enable_va=no])
    fi
fi

if test "x$enable_dri" = xyes -o \
        "x$enable_xvmc" = xyes -o \
        "x$enable_vdpau" = xyes -o \
        "x$enable_omx_bellagio" = xyes -o \
        "x$enable_va" = xyes; then
    need_gallium_vl=yes
fi
AM_CONDITIONAL(NEED_GALLIUM_VL, test "x$need_gallium_vl" = xyes)

if test "x$enable_xvmc" = xyes -o \
        "x$enable_vdpau" = xyes -o \
        "x$enable_omx_bellagio" = xyes -o \
        "x$enable_va" = xyes; then
    if echo $platforms | grep -q "x11"; then
        PKG_CHECK_MODULES([VL], [x11-xcb xcb xcb-dri2 >= $XCBDRI2_REQUIRED])
    fi
    need_gallium_vl_winsys=yes
fi
AM_CONDITIONAL(NEED_GALLIUM_VL_WINSYS, test "x$need_gallium_vl_winsys" = xyes)

if test "x$enable_xvmc" = xyes; then
    if test "x$have_xvmc_platform" != xyes; then
        AC_MSG_ERROR([XVMC requires the x11 platforms])
    fi
    PKG_CHECK_MODULES([XVMC], [xvmc >= $XVMC_REQUIRED])
    gallium_st="$gallium_st xvmc"
fi
AM_CONDITIONAL(HAVE_ST_XVMC, test "x$enable_xvmc" = xyes)

if test "x$enable_vdpau" = xyes; then
    if test "x$have_vdpau_platform" != xyes; then
        AC_MSG_ERROR([VDPAU requires the x11 platforms])
    fi
    PKG_CHECK_MODULES([VDPAU], [vdpau >= $VDPAU_REQUIRED])
    gallium_st="$gallium_st vdpau"
    DEFINES="$DEFINES -DHAVE_ST_VDPAU"
fi
AM_CONDITIONAL(HAVE_ST_VDPAU, test "x$enable_vdpau" = xyes)

if test "x$enable_omx_bellagio" = xyes; then
    if test "x$have_omx_platform" != xyes; then
        AC_MSG_ERROR([OMX requires at least one of the x11 or drm platforms])
    fi
    PKG_CHECK_MODULES([OMX_BELLAGIO], [libomxil-bellagio >= $LIBOMXIL_BELLAGIO_REQUIRED])
    gallium_st="$gallium_st omx_bellagio"
fi
AM_CONDITIONAL(HAVE_ST_OMX_BELLAGIO, test "x$enable_omx_bellagio" = xyes)

if test "x$enable_va" = xyes; then
    if test "x$have_va_platform" != xyes; then
        AC_MSG_ERROR([VA requires at least one of the x11 drm or wayland platforms])
    fi
    PKG_CHECK_MODULES([VA], [libva >= $LIBVA_REQUIRED])
    gallium_st="$gallium_st va"
fi
AM_CONDITIONAL(HAVE_ST_VA, test "x$enable_va" = xyes)

dnl
dnl Nine Direct3D9 configuration
dnl
if test "x$enable_nine" = xyes; then
    if ! echo "$with_gallium_drivers" | grep -q 'swrast'; then
        AC_MSG_ERROR([nine requires the gallium swrast driver])
    fi
    if test "x$with_gallium_drivers" = xswrast; then
        AC_MSG_ERROR([nine requires at least one non-swrast gallium driver])
    fi
    if test $GCC_VERSION_MAJOR -lt 4 -o $GCC_VERSION_MAJOR -eq 4 -a $GCC_VERSION_MINOR -lt 6; then
        AC_MSG_ERROR([gcc >= 4.6 is required to build nine])
    fi

    if test "x$enable_dri3" = xno; then
        AC_MSG_WARN([using nine together with wine requires DRI3 enabled system])
    fi

    gallium_st="$gallium_st nine"
fi
AM_CONDITIONAL(HAVE_ST_NINE, test "x$enable_nine" = xyes)

dnl
dnl OpenCL configuration
dnl

AC_ARG_WITH([clang-libdir],
   [AS_HELP_STRING([--with-clang-libdir],
         [Path to Clang libraries @<:@default=llvm-config --libdir@:>@])],
   [CLANG_LIBDIR="$withval"],
   [CLANG_LIBDIR=''])

PKG_CHECK_EXISTS([libclc], [have_libclc=yes], [have_libclc=no])

if test "x$enable_opencl" = xyes; then
    if test -z "$with_gallium_drivers"; then
        AC_MSG_ERROR([cannot enable OpenCL without Gallium])
    fi

    if test "x$HAVE_CXX11" != "xyes"; then
       AC_MSG_ERROR([clover requires c++11 support])
    fi

    if test "x$have_libclc" = xno; then
        AC_MSG_ERROR([pkg-config cannot find libclc.pc which is required to build clover.
                    Make sure the directory containing libclc.pc is specified in your
                    PKG_CONFIG_PATH environment variable.
                    By default libclc.pc is installed to /usr/local/share/pkgconfig/])
    else
        LIBCLC_INCLUDEDIR=`$PKG_CONFIG --variable=includedir libclc`
        LIBCLC_LIBEXECDIR=`$PKG_CONFIG --variable=libexecdir libclc`
        AC_SUBST([LIBCLC_INCLUDEDIR])
        AC_SUBST([LIBCLC_LIBEXECDIR])
    fi

    gallium_st="$gallium_st clover"

    if test "x$enable_opencl_icd" = xyes; then
        OPENCL_LIBNAME="MesaOpenCL"
    else
        OPENCL_LIBNAME="OpenCL"
    fi

    if test "x$have_libelf" != xyes; then
       AC_MSG_ERROR([Clover requires libelf])
    fi

    if test "x${ac_cv_cxx_compiler_gnu}" = xyes; then
        altivec_enabled=no
        AC_COMPILE_IFELSE([AC_LANG_SOURCE([
        #if !defined(__VEC__) || !defined(__ALTIVEC__)
        #error "AltiVec not enabled"
        #endif
        ])], altivec_enabled=yes)

        if test "$altivec_enabled" = yes; then
            CLOVER_STD_OVERRIDE="-std=gnu++11"
        fi
        AC_SUBST([CLOVER_STD_OVERRIDE])
    fi

    llvm_require_version $LLVM_REQUIRED_OPENCL "opencl"

    llvm_add_default_components "opencl"
    llvm_add_component "all-targets" "opencl"
    llvm_add_component "coverage" "opencl"
    llvm_add_component "linker" "opencl"
    llvm_add_component "instrumentation" "opencl"
    llvm_add_component "ipo" "opencl"
    llvm_add_component "irreader" "opencl"
    llvm_add_component "lto" "opencl"
    llvm_add_component "option" "opencl"
    llvm_add_component "objcarcopts" "opencl"
    llvm_add_component "profiledata" "opencl"
    llvm_add_optional_component "coroutines" "opencl"

    dnl Check for Clang internal headers
    if test -z "$CLANG_LIBDIR"; then
        CLANG_LIBDIR=${LLVM_LIBDIR}
    fi
    CLANG_RESOURCE_DIR=$CLANG_LIBDIR/clang/${LLVM_VERSION}
    AS_IF([test ! -f "$CLANG_RESOURCE_DIR/include/stddef.h"],
        [AC_MSG_ERROR([Could not find clang internal header stddef.h in $CLANG_RESOURCE_DIR Use --with-clang-libdir to specify the correct path to the clang libraries.])])
fi
AM_CONDITIONAL(HAVE_CLOVER, test "x$enable_opencl" = xyes)
AM_CONDITIONAL(HAVE_CLOVER_ICD, test "x$enable_opencl_icd" = xyes)
AC_SUBST([OPENCL_LIBNAME])
AC_SUBST([CLANG_RESOURCE_DIR])

dnl
dnl Gallium configuration
dnl
AM_CONDITIONAL(HAVE_GALLIUM, test -n "$with_gallium_drivers")

# libEGL wants to default to the first platform specified in
# ./configure.  parse that here.
if test "x$platforms" != "x"; then
    FIRST_PLATFORM_CAPS=`echo $platforms | sed 's| .*||' | tr '[[a-z]]' '[[A-Z]]'`
    EGL_NATIVE_PLATFORM="_EGL_PLATFORM_$FIRST_PLATFORM_CAPS"
else
    EGL_NATIVE_PLATFORM="_EGL_INVALID_PLATFORM"
fi

AC_SUBST([EGL_NATIVE_PLATFORM])
AC_SUBST([EGL_CFLAGS])

# If we don't have the X11 platform, set this define so we don't try to include
# the X11 headers.
if ! echo "$platforms" | grep -q 'x11'; then
    DEFINES="$DEFINES -DMESA_EGL_NO_X11_HEADERS"
    GL_PC_CFLAGS="$GL_PC_CFLAGS -DMESA_EGL_NO_X11_HEADERS"
fi

dnl Directory for XVMC libs
AC_ARG_WITH([xvmc-libdir],
    [AS_HELP_STRING([--with-xvmc-libdir=DIR],
        [directory for the XVMC libraries @<:@default=${libdir}@:>@])],
    [XVMC_LIB_INSTALL_DIR="$withval"],
    [XVMC_LIB_INSTALL_DIR='${libdir}'])
AC_SUBST([XVMC_LIB_INSTALL_DIR])

dnl
dnl Gallium Tests
dnl
AM_CONDITIONAL(HAVE_GALLIUM_TESTS, test "x$enable_gallium_tests" = xyes)

dnl Directory for VDPAU libs
AC_ARG_WITH([vdpau-libdir],
    [AS_HELP_STRING([--with-vdpau-libdir=DIR],
        [directory for the VDPAU libraries @<:@default=${libdir}/vdpau@:>@])],
    [VDPAU_LIB_INSTALL_DIR="$withval"],
    [VDPAU_LIB_INSTALL_DIR='${libdir}/vdpau'])
AC_SUBST([VDPAU_LIB_INSTALL_DIR])

dnl Directory for OMX_BELLAGIO libs

AC_ARG_WITH([omx-bellagio-libdir],
    [AS_HELP_STRING([--with-omx-bellagio-libdir=DIR],
        [directory for the OMX_BELLAGIO libraries])],
    [OMX_BELLAGIO_LIB_INSTALL_DIR="$withval"],
    [OMX_BELLAGIO_LIB_INSTALL_DIR=`$PKG_CONFIG --exists libomxil-bellagio && \
                                   $PKG_CONFIG --define-variable=libdir=\$libdir --variable=pluginsdir libomxil-bellagio`])
AC_SUBST([OMX_BELLAGIO_LIB_INSTALL_DIR])

dnl Directory for VA libs

AC_ARG_WITH([va-libdir],
    [AS_HELP_STRING([--with-va-libdir=DIR],
        [directory for the VA libraries @<:@${libdir}/dri@:>@])],
    [VA_LIB_INSTALL_DIR="$withval"],
    [VA_LIB_INSTALL_DIR="${libdir}/dri"])
AC_SUBST([VA_LIB_INSTALL_DIR])

AC_ARG_WITH([d3d-libdir],
    [AS_HELP_STRING([--with-d3d-libdir=DIR],
        [directory for the D3D modules @<:@${libdir}/d3d@:>@])],
    [D3D_DRIVER_INSTALL_DIR="$withval"],
    [D3D_DRIVER_INSTALL_DIR="${libdir}/d3d"])
AC_SUBST([D3D_DRIVER_INSTALL_DIR])

dnl Architectures to build SWR library for

AC_ARG_WITH([swr-archs],
    [AS_HELP_STRING([--with-swr-archs@<:@=DIRS...@:>@],
        [comma delimited swr architectures list, e.g.
        "avx,avx2,knl,skx" @<:@default="avx,avx2"@:>@])],
    [with_swr_archs="$withval"],
    [with_swr_archs="avx,avx2"])

dnl
dnl r300 doesn't strictly require LLVM, but for performance reasons we
dnl highly recommend LLVM usage. So require it at least on x86 and x86_64
dnl architectures.
dnl
r300_require_llvm() {
    case "$host" in *gnux32) return;; esac
    case "$host_cpu" in
    i*86|x86_64|amd64) require_llvm $1
        ;;
    esac
}

dnl
dnl DRM is needed by X, Wayland, and offscreen rendering.
dnl Surfaceless is an alternative for the last one.
dnl
require_basic_egl() {
    case "$with_platforms" in
        *drm*|*surfaceless*|*android*)
            ;;
        *)
            AC_MSG_ERROR([$1 requires one of these:
                  1) --with-platforms=drm (X, Wayland, offscreen rendering based on DRM)
                  2) --with-platforms=surfaceless (offscreen only)
                  3) --with-platforms=android (Android only)
                  Recommended options: drm,x11])
            ;;
    esac
}

swr_require_cxx_feature_flags() {
    feature_name="$1"
    preprocessor_test="$2"
    option_list="$3"
    output_var="$4"

    AC_MSG_CHECKING([whether $CXX supports $feature_name])
    AC_LANG_PUSH([C++])
    save_CXXFLAGS="$CXXFLAGS"
    save_IFS="$IFS"
    IFS=","
    found=0
    for opts in $option_list
    do
        unset IFS
        CXXFLAGS="$opts $save_CXXFLAGS"
        AC_COMPILE_IFELSE(
            [AC_LANG_PROGRAM(
                [   #if !($preprocessor_test)
                    #error
                    #endif
                ])],
            [found=1; break],
            [])
        IFS=","
    done
    IFS="$save_IFS"
    CXXFLAGS="$save_CXXFLAGS"
    AC_LANG_POP([C++])
    if test $found -eq 1; then
        AC_MSG_RESULT([$opts])
        eval "$output_var=\$opts"
        return 0
    fi
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([swr requires $feature_name support])
    return 1
}

dnl Duplicates in GALLIUM_DRIVERS_DIRS are removed by sorting it after this block
if test -n "$with_gallium_drivers"; then
    gallium_drivers=`IFS=', '; echo $with_gallium_drivers`
    for driver in $gallium_drivers; do
        case "x$driver" in
        xsvga)
            HAVE_GALLIUM_SVGA=yes
            require_libdrm "svga"
            ;;
        xi915)
            HAVE_GALLIUM_I915=yes
            PKG_CHECK_MODULES([I915], [libdrm >= $LIBDRM_INTEL_REQUIRED libdrm_intel >= $LIBDRM_INTEL_REQUIRED])
            require_libdrm "Gallium i915"
            ;;
        xr300)
            HAVE_GALLIUM_R300=yes
            PKG_CHECK_MODULES([RADEON], [libdrm >= $LIBDRM_RADEON_REQUIRED libdrm_radeon >= $LIBDRM_RADEON_REQUIRED])
            require_libdrm "r300"
            r300_require_llvm "r300"
            ;;
        xr600)
            HAVE_GALLIUM_R600=yes
            PKG_CHECK_MODULES([RADEON], [libdrm >= $LIBDRM_RADEON_REQUIRED libdrm_radeon >= $LIBDRM_RADEON_REQUIRED])
            require_libdrm "r600"
            if test "x$enable_llvm" = xyes; then
                radeon_llvm_check $LLVM_REQUIRED_R600 "r600"

                llvm_add_component "asmparser" "r600"
                llvm_add_component "bitreader" "r600"
            fi
            ;;
        xradeonsi)
            HAVE_GALLIUM_RADEONSI=yes
            PKG_CHECK_MODULES([RADEON], [libdrm >= $LIBDRM_RADEON_REQUIRED libdrm_radeon >= $LIBDRM_RADEON_REQUIRED])
            PKG_CHECK_MODULES([AMDGPU], [libdrm >= $LIBDRM_AMDGPU_REQUIRED libdrm_amdgpu >= $LIBDRM_AMDGPU_REQUIRED])
            require_libdrm "radeonsi"
            radeon_llvm_check $LLVM_REQUIRED_RADEONSI "radeonsi"
            if test "x$enable_egl" = xyes; then
                require_basic_egl "radeonsi"
            fi
            ;;
        xnouveau)
            HAVE_GALLIUM_NOUVEAU=yes
            PKG_CHECK_MODULES([NOUVEAU], [libdrm >= $LIBDRM_NOUVEAU_REQUIRED libdrm_nouveau >= $LIBDRM_NOUVEAU_REQUIRED])
            require_libdrm "nouveau"
            ;;
        xfreedreno)
            HAVE_GALLIUM_FREEDRENO=yes
            PKG_CHECK_MODULES([FREEDRENO], [libdrm >= $LIBDRM_FREEDRENO_REQUIRED libdrm_freedreno >= $LIBDRM_FREEDRENO_REQUIRED])
            require_libdrm "freedreno"
            ;;
        xetnaviv)
            HAVE_GALLIUM_ETNAVIV=yes
            PKG_CHECK_MODULES([ETNAVIV], [libdrm >= $LIBDRM_ETNAVIV_REQUIRED libdrm_etnaviv >= $LIBDRM_ETNAVIV_REQUIRED])
            require_libdrm "etnaviv"
            ;;
       ximx)
            HAVE_GALLIUM_IMX=yes
            ;;
        xswrast)
            HAVE_GALLIUM_SOFTPIPE=yes
            if test "x$enable_llvm" = xyes; then
                HAVE_GALLIUM_LLVMPIPE=yes
            fi
            ;;
        xswr)
            llvm_require_version $LLVM_REQUIRED_SWR "swr"

            if test "x$HAVE_CXX11" != "xyes"; then
                AC_MSG_ERROR([swr requires c++11 support])
            fi

            swr_require_cxx_feature_flags "AVX" "defined(__AVX__)" \
                ",-target-cpu=sandybridge,-mavx,-march=core-avx,-tp=sandybridge" \
                SWR_AVX_CXXFLAGS
            AC_SUBST([SWR_AVX_CXXFLAGS])

            swr_archs=`IFS=', '; echo $with_swr_archs`
            for arch in $swr_archs; do
                case "x$arch" in
                xavx)
                    HAVE_SWR_AVX=yes
                    ;;
                xavx2)
                    swr_require_cxx_feature_flags "AVX2" "defined(__AVX2__)" \
                        ",-target-cpu=haswell,-mavx2 -mfma -mbmi2 -mf16c,-march=core-avx2,-tp=haswell" \
                        SWR_AVX2_CXXFLAGS
                    AC_SUBST([SWR_AVX2_CXXFLAGS])
                    HAVE_SWR_AVX2=yes
                    ;;
                xknl)
                    swr_require_cxx_feature_flags "KNL" "defined(__AVX512F__) && defined(__AVX512ER__)" \
                        ",-target-cpu=mic-knl,-march=knl,-xMIC-AVX512" \
                        SWR_KNL_CXXFLAGS
                    AC_SUBST([SWR_KNL_CXXFLAGS])
                    HAVE_SWR_KNL=yes
                    ;;
                xskx)
                    swr_require_cxx_feature_flags "SKX" "defined(__AVX512F__) && defined(__AVX512BW__)" \
                        ",-target-cpu=x86-skylake,-march=skylake-avx512,-xCORE-AVX512" \
                        SWR_SKX_CXXFLAGS
                    AC_SUBST([SWR_SKX_CXXFLAGS])
                    HAVE_SWR_SKX=yes
                    ;;
                *)
                    AC_MSG_ERROR([unknown SWR build architecture '$arch'])
                    ;;
                esac
            done

            if test "x$HAVE_SWR_AVX" != xyes -a \
                    "x$HAVE_SWR_AVX2" != xyes -a \
                    "x$HAVE_SWR_KNL" != xyes -a \
                    "x$HAVE_SWR_SKX" != xyes; then
               AC_MSG_ERROR([swr enabled but no swr architectures selected])
            fi

            # test if more than one swr arch configured
            if test `echo $swr_archs | wc -w` -eq 1; then
                HAVE_SWR_BUILTIN=yes
            fi

            HAVE_GALLIUM_SWR=yes
            ;;
        xvc4)
            HAVE_GALLIUM_VC4=yes
            require_libdrm "vc4"

            PKG_CHECK_MODULES([SIMPENROSE], [simpenrose],
                              [USE_VC4_SIMULATOR=yes;
                               DEFINES="$DEFINES -DUSE_VC4_SIMULATOR"],
                              [USE_VC4_SIMULATOR=no])
            ;;
        xvc5)
            HAVE_GALLIUM_VC5=yes

            PKG_CHECK_MODULES([VC5_SIMULATOR], [v3dv3],
                              [USE_VC5_SIMULATOR=yes;
                               DEFINES="$DEFINES -DUSE_VC5_SIMULATOR"],
                              [AC_MSG_ERROR([vc5 requires the simulator])])
            ;;
        xpl111)
            HAVE_GALLIUM_PL111=yes
            ;;
        xvirgl)
            HAVE_GALLIUM_VIRGL=yes
            require_libdrm "virgl"
            if test "x$enable_egl" = xyes; then
                require_basic_egl "virgl"
            fi
            ;;
        *)
            AC_MSG_ERROR([Unknown Gallium driver: $driver])
            ;;
        esac
    done
fi

# XXX: Keep in sync with LLVM_REQUIRED_SWR
AM_CONDITIONAL(SWR_INVALID_LLVM_VERSION, test "x$LLVM_VERSION" != x3.9.0 -a \
                                              "x$LLVM_VERSION" != x3.9.1)

if test "x$enable_llvm" = "xyes" -a "$with_gallium_drivers"; then
    llvm_require_version $LLVM_REQUIRED_GALLIUM "gallium"
    llvm_add_default_components "gallium"
fi

AM_CONDITIONAL(HAVE_SWR_AVX, test "x$HAVE_SWR_AVX" = xyes)
AM_CONDITIONAL(HAVE_SWR_AVX2, test "x$HAVE_SWR_AVX2" = xyes)
AM_CONDITIONAL(HAVE_SWR_KNL, test "x$HAVE_SWR_KNL" = xyes)
AM_CONDITIONAL(HAVE_SWR_SKX, test "x$HAVE_SWR_SKX" = xyes)
AM_CONDITIONAL(HAVE_SWR_BUILTIN, test "x$HAVE_SWR_BUILTIN" = xyes)

dnl We need to validate some needed dependencies for renderonly drivers.

if test "x$HAVE_GALLIUM_ETNAVIV" != xyes -a "x$HAVE_GALLIUM_IMX" = xyes  ; then
    AC_MSG_ERROR([Building with imx requires etnaviv])
fi

if test "x$HAVE_GALLIUM_VC4" != xyes -a "x$HAVE_GALLIUM_PL111" = xyes  ; then
    AC_MSG_ERROR([Building with pl111 requires vc4])
fi


detect_old_buggy_llvm() {
    dnl llvm-config may not give the right answer when llvm is a built as a
    dnl single shared library, so we must work the library name out for
    dnl ourselves.
    dnl (See https://llvm.org/bugs/show_bug.cgi?id=6823)
    dnl We can't use $LLVM_VERSION because it has 'svn' stripped out,
    LLVM_SO_NAME=LLVM-`$LLVM_CONFIG --version`
    AS_IF([test -f "$LLVM_LIBDIR/lib$LLVM_SO_NAME.$IMP_LIB_EXT"], [llvm_have_one_so=yes])

    if test "x$llvm_have_one_so" = xyes; then
        dnl LLVM was built using auto*, so there is only one shared object.
        LLVM_LIBS="-l$LLVM_SO_NAME"
    else
        dnl If LLVM was built with CMake, there will be one shared object per
        dnl component.
        AS_IF([test ! -f "$LLVM_LIBDIR/libLLVMTarget.$IMP_LIB_EXT"],
               [AC_MSG_ERROR([Could not find llvm shared libraries:
                Please make sure you have built llvm with the --enable-shared option
                and that your llvm libraries are installed in $LLVM_LIBDIR
                If you have installed your llvm libraries to a different directory you
                can use the --with-llvm-prefix= configure flag to specify this directory.
                NOTE: Mesa is attempting to use llvm shared libraries by default.
                If you do not want to build with llvm shared libraries and instead want to
                use llvm static libraries then add --disable-llvm-shared-libs to your configure
                invocation and rebuild.])])

        dnl We don't need to update LLVM_LIBS in this case because the LLVM
        dnl install uses a shared object for each component and we have
        dnl already added all of these objects to LLVM_LIBS.
    fi
}

dnl
dnl Set defines and buildtime variables only when using LLVM.
dnl
if test "x$enable_llvm" = xyes; then
    DEFINES="${DEFINES} -DHAVE_LLVM=0x0$LLVM_VERSION_INT -DMESA_LLVM_VERSION_PATCH=$LLVM_VERSION_PATCH"

    LLVM_LDFLAGS=`$LLVM_CONFIG --ldflags`
    LLVM_CFLAGS=$LLVM_CPPFLAGS   # CPPFLAGS seem to be sufficient
    LLVM_CXXFLAGS=`strip_unwanted_llvm_flags "$LLVM_CONFIG --cxxflags"`

    dnl Set LLVM_LIBS - This is done after the driver configuration so
    dnl that drivers can add additional components to LLVM_COMPONENTS.
    dnl Previously, gallium drivers were updating LLVM_LIBS directly
    dnl by calling llvm-config --libs ${DRIVER_LLVM_COMPONENTS}, but
    dnl this was causing the same libraries to be appear multiple times
    dnl in LLVM_LIBS.

    if test $LLVM_VERSION_MAJOR -ge 4 -o $LLVM_VERSION_MAJOR -eq 3 -a $LLVM_VERSION_MINOR -ge 9; then
        if test "x$enable_llvm_shared_libs" = xyes; then
            LLVM_LIBS="`$LLVM_CONFIG --link-shared --libs ${LLVM_COMPONENTS}`"
        else
            dnl Invoking llvm-config with both -libs and --system-libs produces the
            dnl two separate lines - each for the set of libraries.
		dnl Call the program twice, effectively folding them into a single line.
            LLVM_LIBS="`$LLVM_CONFIG --link-static --libs ${LLVM_COMPONENTS}`"
            dnl We need to link to llvm system libs when using static libs
            LLVM_LIBS="$LLVM_LIBS `$LLVM_CONFIG --link-static --system-libs`"
        fi
    else
        LLVM_LIBS="`$LLVM_CONFIG --libs ${LLVM_COMPONENTS}`"
        if test "x$enable_llvm_shared_libs" = xyes; then
            detect_old_buggy_llvm
        else
            AC_MSG_WARN([Building mesa with statically linked LLVM may cause compilation issues])
            dnl We need to link to llvm system libs when using static libs
            dnl However, only llvm 3.5+ provides --system-libs
            if test $LLVM_VERSION_MAJOR -ge 4 -o $LLVM_VERSION_MAJOR -eq 3 -a $LLVM_VERSION_MINOR -ge 5; then
                LLVM_LIBS="$LLVM_LIBS `$LLVM_CONFIG --system-libs`"
            fi
        fi
    fi

    dnl The gallium-xlib GLX and gallium OSMesa targets directly embed the
    dnl swr/llvmpipe driver into the final binary.  Adding LLVM_LIBS results in 
    dnl the LLVM library propagated in the Libs.private of the respective .pc
    dnl file which ensures complete dependency information when statically
    dnl linking.
    if test "x$enable_glx" == xgallium-xlib; then
        GL_PC_LIB_PRIV="$GL_PC_LIB_PRIV $LLVM_LIBS"
    fi
    if test "x$enable_gallium_osmesa" = xyes; then
        OSMESA_PC_LIB_PRIV="$OSMESA_PC_LIB_PRIV $LLVM_LIBS"
    fi
fi

AM_CONDITIONAL(HAVE_GALLIUM_SVGA, test "x$HAVE_GALLIUM_SVGA" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_I915, test "x$HAVE_GALLIUM_I915" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_PL111, test "x$HAVE_GALLIUM_PL111" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_R300, test "x$HAVE_GALLIUM_R300" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_R600, test "x$HAVE_GALLIUM_R600" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_RADEONSI, test "x$HAVE_GALLIUM_RADEONSI" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_RADEON_COMMON, test "x$HAVE_GALLIUM_RADEONSI" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_NOUVEAU, test "x$HAVE_GALLIUM_NOUVEAU" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_FREEDRENO, test "x$HAVE_GALLIUM_FREEDRENO" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_ETNAVIV, test "x$HAVE_GALLIUM_ETNAVIV" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_IMX, test "x$HAVE_GALLIUM_IMX" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_SOFTPIPE, test "x$HAVE_GALLIUM_SOFTPIPE" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_LLVMPIPE, test "x$HAVE_GALLIUM_LLVMPIPE" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_SWR, test "x$HAVE_GALLIUM_SWR" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_SWRAST, test "x$HAVE_GALLIUM_SOFTPIPE" = xyes -o \
                                         "x$HAVE_GALLIUM_LLVMPIPE" = xyes -o \
                                         "x$HAVE_GALLIUM_SWR" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_VC4, test "x$HAVE_GALLIUM_VC4" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_VC5, test "x$HAVE_GALLIUM_VC5" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_VIRGL, test "x$HAVE_GALLIUM_VIRGL" = xyes)

AM_CONDITIONAL(HAVE_GALLIUM_STATIC_TARGETS, test "x$enable_shared_pipe_drivers" = xno)

if test "x$enable_dri" = xyes; then
    GALLIUM_PIPE_LOADER_DEFINES="$GALLIUM_PIPE_LOADER_DEFINES -DHAVE_PIPE_LOADER_DRI"
fi

if test "x$have_drisw_kms" = xyes; then
    GALLIUM_PIPE_LOADER_DEFINES="$GALLIUM_PIPE_LOADER_DEFINES -DHAVE_PIPE_LOADER_KMS"
fi
AC_SUBST([GALLIUM_PIPE_LOADER_DEFINES])

AM_CONDITIONAL(HAVE_I915_DRI, test x$HAVE_I915_DRI = xyes)
AM_CONDITIONAL(HAVE_I965_DRI, test x$HAVE_I965_DRI = xyes)
AM_CONDITIONAL(HAVE_NOUVEAU_DRI, test x$HAVE_NOUVEAU_DRI = xyes)
AM_CONDITIONAL(HAVE_R200_DRI, test x$HAVE_R200_DRI = xyes)
AM_CONDITIONAL(HAVE_RADEON_DRI, test x$HAVE_RADEON_DRI = xyes)
AM_CONDITIONAL(HAVE_SWRAST_DRI, test x$HAVE_SWRAST_DRI = xyes)

AM_CONDITIONAL(HAVE_RADEON_VULKAN, test "x$HAVE_RADEON_VULKAN" = xyes)
AM_CONDITIONAL(HAVE_INTEL_VULKAN, test "x$HAVE_INTEL_VULKAN" = xyes)

AM_CONDITIONAL(HAVE_AMD_DRIVERS, test "x$HAVE_GALLIUM_RADEONSI" = xyes -o \
                                      "x$HAVE_RADEON_VULKAN" = xyes)

AM_CONDITIONAL(HAVE_BROADCOM_DRIVERS, test "x$HAVE_GALLIUM_VC4" = xyes -o \
                                      "x$HAVE_GALLIUM_VC5" = xyes)

AM_CONDITIONAL(HAVE_INTEL_DRIVERS, test "x$HAVE_INTEL_VULKAN" = xyes -o \
                                        "x$HAVE_I965_DRI" = xyes)

AM_CONDITIONAL(NEED_RADEON_DRM_WINSYS, test "x$HAVE_GALLIUM_R300" = xyes -o \
                                            "x$HAVE_GALLIUM_R600" = xyes -o \
                                            "x$HAVE_GALLIUM_RADEONSI" = xyes)
AM_CONDITIONAL(NEED_WINSYS_XLIB, test "x$enable_glx" = xgallium-xlib)
AM_CONDITIONAL(HAVE_GALLIUM_COMPUTE, test x$enable_opencl = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_LLVM, test "x$enable_llvm" = xyes)
AM_CONDITIONAL(USE_VC4_SIMULATOR, test x$USE_VC4_SIMULATOR = xyes)
AM_CONDITIONAL(USE_VC5_SIMULATOR, test x$USE_VC5_SIMULATOR = xyes)

AM_CONDITIONAL(HAVE_LIBDRM, test "x$have_libdrm" = xyes)
AM_CONDITIONAL(HAVE_OSMESA, test "x$enable_osmesa" = xyes)
AM_CONDITIONAL(HAVE_GALLIUM_OSMESA, test "x$enable_gallium_osmesa" = xyes)
AM_CONDITIONAL(HAVE_COMMON_OSMESA, test "x$enable_osmesa" = xyes -o \
                                        "x$enable_gallium_osmesa" = xyes)

AM_CONDITIONAL(HAVE_X86_ASM, test "x$asm_arch" = xx86 -o "x$asm_arch" = xx86_64)
AM_CONDITIONAL(HAVE_X86_64_ASM, test "x$asm_arch" = xx86_64)
AM_CONDITIONAL(HAVE_SPARC_ASM, test "x$asm_arch" = xsparc)
AM_CONDITIONAL(HAVE_PPC64LE_ASM, test "x$asm_arch" = xppc64le)
AM_CONDITIONAL(HAVE_AARCH64_ASM, test "x$asm_arch" = xaarch64)
AM_CONDITIONAL(HAVE_ARM_ASM, test "x$asm_arch" = xarm)

AC_SUBST([NINE_MAJOR], 1)
AC_SUBST([NINE_MINOR], 0)
AC_SUBST([NINE_PATCH], 0)
AC_SUBST([NINE_VERSION], "$NINE_MAJOR.$NINE_MINOR.$NINE_PATCH")

AC_SUBST([VDPAU_MAJOR], 1)
AC_SUBST([VDPAU_MINOR], 0)

if test "x$enable_va" = xyes; then
    VA_MAJOR=`$PKG_CONFIG --modversion libva | $SED -n 's/\([[^\.]]*\)\..*$/\1/p'`
    VA_MINOR=`$PKG_CONFIG --modversion libva | $SED -n 's/.*\.\(.*\)\..*$/\1/p'`
fi
AC_SUBST([VA_MAJOR], $VA_MAJOR)
AC_SUBST([VA_MINOR], $VA_MINOR)

AM_CONDITIONAL(HAVE_VULKAN_COMMON, test "x$VULKAN_DRIVERS" != "x")

AC_SUBST([XVMC_MAJOR], 1)
AC_SUBST([XVMC_MINOR], 0)

AC_SUBST([XA_MAJOR], 2)
AC_SUBST([XA_MINOR], 3)
AC_SUBST([XA_PATCH], 0)
AC_SUBST([XA_VERSION], "$XA_MAJOR.$XA_MINOR.$XA_PATCH")

AC_ARG_ENABLE(valgrind,
              [AS_HELP_STRING([--enable-valgrind],
                             [Build mesa with valgrind support (default: auto)])],
                             [VALGRIND=$enableval], [VALGRIND=auto])
if test "x$VALGRIND" != xno; then
    PKG_CHECK_MODULES(VALGRIND, [valgrind], [have_valgrind=yes], [have_valgrind=no])
fi
AC_MSG_CHECKING([whether to enable Valgrind support])
if test "x$VALGRIND" = xauto; then
    VALGRIND="$have_valgrind"
fi

if test "x$VALGRIND" = "xyes"; then
    if ! test "x$have_valgrind" = xyes; then
        AC_MSG_ERROR([Valgrind support required but not present])
    fi
    AC_DEFINE([HAVE_VALGRIND], 1, [Use valgrind intrinsics to suppress false warnings])
fi

AC_MSG_RESULT([$VALGRIND])

dnl Restore LDFLAGS and CPPFLAGS
LDFLAGS="$_SAVE_LDFLAGS"
CPPFLAGS="$_SAVE_CPPFLAGS"

dnl Suppress clang's warnings about unused CFLAGS and CXXFLAGS
if test "x$acv_mesa_CLANG" = xyes; then
    CFLAGS="$CFLAGS -Qunused-arguments"
    CXXFLAGS="$CXXFLAGS -Qunused-arguments"
fi

dnl Add user CFLAGS and CXXFLAGS
CFLAGS="$CFLAGS $USER_CFLAGS"
CXXFLAGS="$CXXFLAGS $USER_CXXFLAGS"

dnl Substitute the config
AC_CONFIG_FILES([Makefile
                 src/Makefile
                 src/amd/Makefile
                 src/amd/vulkan/Makefile
                 src/broadcom/Makefile
                 src/compiler/Makefile
                 src/egl/Makefile
                 src/egl/main/egl.pc
                 src/egl/wayland/wayland-drm/Makefile
                 src/egl/wayland/wayland-egl/Makefile
                 src/egl/wayland/wayland-egl/wayland-egl.pc
                 src/gallium/Makefile
                 src/gallium/auxiliary/Makefile
                 src/gallium/auxiliary/pipe-loader/Makefile
                 src/gallium/drivers/freedreno/Makefile
                 src/gallium/drivers/ddebug/Makefile
                 src/gallium/drivers/i915/Makefile
                 src/gallium/drivers/llvmpipe/Makefile
                 src/gallium/drivers/noop/Makefile
                 src/gallium/drivers/nouveau/Makefile
                 src/gallium/drivers/pl111/Makefile
                 src/gallium/drivers/r300/Makefile
                 src/gallium/drivers/r600/Makefile
                 src/gallium/drivers/radeon/Makefile
                 src/gallium/drivers/radeonsi/Makefile
                 src/gallium/drivers/rbug/Makefile
                 src/gallium/drivers/softpipe/Makefile
                 src/gallium/drivers/svga/Makefile
                 src/gallium/drivers/swr/Makefile
                 src/gallium/drivers/trace/Makefile
                 src/gallium/drivers/etnaviv/Makefile
                 src/gallium/drivers/imx/Makefile
                 src/gallium/drivers/vc4/Makefile
                 src/gallium/drivers/vc5/Makefile
                 src/gallium/drivers/virgl/Makefile
                 src/gallium/state_trackers/clover/Makefile
                 src/gallium/state_trackers/dri/Makefile
                 src/gallium/state_trackers/glx/xlib/Makefile
                 src/gallium/state_trackers/nine/Makefile
                 src/gallium/state_trackers/omx_bellagio/Makefile
                 src/gallium/state_trackers/osmesa/Makefile
                 src/gallium/state_trackers/va/Makefile
                 src/gallium/state_trackers/vdpau/Makefile
                 src/gallium/state_trackers/xa/Makefile
                 src/gallium/state_trackers/xa/xa_tracker.h
                 src/gallium/state_trackers/xvmc/Makefile
                 src/gallium/targets/d3dadapter9/Makefile
                 src/gallium/targets/d3dadapter9/d3d.pc
                 src/gallium/targets/dri/Makefile
                 src/gallium/targets/libgl-xlib/Makefile
                 src/gallium/targets/omx-bellagio/Makefile
                 src/gallium/targets/opencl/Makefile
                 src/gallium/targets/opencl/mesa.icd
                 src/gallium/targets/osmesa/Makefile
                 src/gallium/targets/osmesa/osmesa.pc
                 src/gallium/targets/pipe-loader/Makefile
                 src/gallium/targets/va/Makefile
                 src/gallium/targets/vdpau/Makefile
                 src/gallium/targets/xa/Makefile
                 src/gallium/targets/xa/xatracker.pc
                 src/gallium/targets/xvmc/Makefile
                 src/gallium/tests/trivial/Makefile
                 src/gallium/tests/unit/Makefile
                 src/gallium/winsys/etnaviv/drm/Makefile
                 src/gallium/winsys/imx/drm/Makefile
                 src/gallium/winsys/freedreno/drm/Makefile
                 src/gallium/winsys/i915/drm/Makefile
                 src/gallium/winsys/nouveau/drm/Makefile
                 src/gallium/winsys/pl111/drm/Makefile
                 src/gallium/winsys/radeon/drm/Makefile
                 src/gallium/winsys/amdgpu/drm/Makefile
                 src/gallium/winsys/svga/drm/Makefile
                 src/gallium/winsys/sw/dri/Makefile
                 src/gallium/winsys/sw/kms-dri/Makefile
                 src/gallium/winsys/sw/null/Makefile
                 src/gallium/winsys/sw/wrapper/Makefile
                 src/gallium/winsys/sw/xlib/Makefile
                 src/gallium/winsys/vc4/drm/Makefile
                 src/gallium/winsys/vc5/drm/Makefile
                 src/gallium/winsys/virgl/drm/Makefile
                 src/gallium/winsys/virgl/vtest/Makefile
                 src/gbm/Makefile
                 src/gbm/main/gbm.pc
                 src/glx/Makefile
                 src/glx/apple/Makefile
                 src/glx/tests/Makefile
                 src/glx/windows/Makefile
                 src/glx/windows/windowsdriproto.pc
                 src/gtest/Makefile
                 src/intel/Makefile
                 src/loader/Makefile
                 src/mapi/Makefile
                 src/mapi/es1api/glesv1_cm.pc
                 src/mapi/es2api/glesv2.pc
                 src/mapi/glapi/gen/Makefile
                 src/mesa/Makefile
                 src/mesa/gl.pc
                 src/mesa/drivers/dri/dri.pc
                 src/mesa/drivers/dri/common/Makefile
                 src/mesa/drivers/dri/i915/Makefile
                 src/mesa/drivers/dri/i965/Makefile
                 src/mesa/drivers/dri/Makefile
                 src/mesa/drivers/dri/nouveau/Makefile
                 src/mesa/drivers/dri/r200/Makefile
                 src/mesa/drivers/dri/radeon/Makefile
                 src/mesa/drivers/dri/swrast/Makefile
                 src/mesa/drivers/osmesa/Makefile
                 src/mesa/drivers/osmesa/osmesa.pc
                 src/mesa/drivers/x11/Makefile
                 src/mesa/main/tests/Makefile
                 src/mesa/state_tracker/tests/Makefile
                 src/util/Makefile
                 src/util/tests/hash_table/Makefile
                 src/util/tests/string_buffer/Makefile
                 src/util/xmlpool/Makefile
                 src/vulkan/Makefile])

AC_OUTPUT

# Fix up dependencies in *.Plo files, where we changed the extension of a
# source file
$SED -i -e 's/brw_blorp.cpp/brw_blorp.c/' src/mesa/drivers/dri/i965/.deps/brw_blorp.Plo

rm -f src/compiler/spirv/spirv_info.lo
echo "# dummy" > src/compiler/spirv/.deps/spirv_info.Plo

dnl
dnl Output some configuration info for the user
dnl
echo ""
echo "        prefix:          $prefix"
echo "        exec_prefix:     $exec_prefix"
echo "        libdir:          $libdir"
echo "        includedir:      $includedir"

dnl API info
echo ""
echo "        OpenGL:          $enable_opengl (ES1: $enable_gles1 ES2: $enable_gles2)"

dnl Driver info
echo ""
case "x$enable_osmesa$enable_gallium_osmesa" in
xnoyes)
        echo "        OSMesa:          lib$OSMESA_LIB (Gallium)"
        ;;
xyesno)
        echo "        OSMesa:          lib$OSMESA_LIB"
        ;;
xnono)
        echo "        OSMesa:          no"
        ;;
esac

echo ""
if test "x$enable_dri" != xno; then
        echo "        DRI platform:    $dri_platform"
        if test -z "$DRI_DIRS"; then
            echo "        DRI drivers:     no"
        else
            echo "        DRI drivers:     $DRI_DIRS"
        fi
        echo "        DRI driver dir:  $DRI_DRIVER_INSTALL_DIR"
fi

case "x$enable_glx" in
xdri)
    echo "        GLX:             DRI-based"
    ;;
xxlib)
    echo "        GLX:             Xlib-based"
    ;;
xgallium-xlib)
    echo "        GLX:             Xlib-based (Gallium)"
    ;;
*)
    echo "        GLX:             $enable_glx"
    ;;
esac

dnl EGL
echo ""
echo "        EGL:             $enable_egl"
if test "$enable_egl" = yes; then

    egl_drivers=""
    if test "x$enable_dri" != "xno"; then
        egl_drivers="$egl_drivers builtin:egl_dri2"
    fi
    if test "x$enable_dri3" != "xno"; then
        egl_drivers="$egl_drivers builtin:egl_dri3"
    fi

    echo "        EGL drivers:    $egl_drivers"
fi
if test "x$enable_gbm" = xyes; then
    echo "        GBM:             yes"
else
    echo "        GBM:             no"
fi

    echo "        EGL/Vulkan/VL platforms:   $platforms"

# Vulkan
echo ""
if test "x$VULKAN_DRIVERS" != x; then
    echo "        Vulkan drivers:  $VULKAN_DRIVERS"
    echo "        Vulkan ICD dir:  $VULKAN_ICD_INSTALL_DIR"
else
    echo "        Vulkan drivers:  no"
fi

echo ""
if test "x$enable_llvm" = xyes; then
    echo "        llvm:            yes"
    echo "        llvm-config:     $LLVM_CONFIG"
    echo "        llvm-version:    $LLVM_VERSION"
else
    echo "        llvm:            no"
fi

echo ""
if test -n "$with_gallium_drivers"; then
    echo "        Gallium drivers: $gallium_drivers"
    echo "        Gallium st:      $gallium_st"
else
    echo "        Gallium:         no"
fi

echo ""
if test "x$enable_gallium_extra_hud" != xyes; then
    echo "        HUD extra stats: no"
else
    echo "        HUD extra stats: yes"
fi

if test "x$enable_lmsensors" != xyes; then
    echo "        HUD lmsensors:   no"
else
    echo "        HUD lmsensors:   yes"
fi

echo ""
if test "x$HAVE_GALLIUM_SWR" != x; then
    if test "x$HAVE_SWR_BUILTIN" = xyes; then
        echo "        SWR archs:       $swr_archs (builtin)"
    else
        echo "        SWR archs:       $swr_archs"
    fi
fi

dnl Libraries
echo ""
echo "        Shared libs:     $enable_shared"
echo "        Static libs:     $enable_static"
echo "        Shared-glapi:    $enable_shared_glapi"

dnl Compiler options
# cleanup the CFLAGS/CXXFLAGS/LDFLAGS/DEFINES vars
cflags=`echo $CFLAGS | \
    $SED 's/^ *//;s/  */ /;s/ *$//'`
cxxflags=`echo $CXXFLAGS | \
    $SED 's/^ *//;s/  */ /;s/ *$//'`
ldflags=`echo $LDFLAGS | $SED 's/^ *//;s/  */ /;s/ *$//'`
defines=`echo $DEFINES | $SED 's/^ *//;s/  */ /;s/ *$//'`
echo ""
echo "        CFLAGS:          $cflags"
echo "        CXXFLAGS:        $cxxflags"
echo "        CXX11_CXXFLAGS:  $CXX11_CXXFLAGS"
echo "        LDFLAGS:         $ldflags"
echo "        Macros:          $defines"
echo ""
if test "x$enable_llvm" = xyes; then
    echo "        LLVM_CFLAGS:     $LLVM_CFLAGS"
    echo "        LLVM_CXXFLAGS:   $LLVM_CXXFLAGS"
    echo "        LLVM_CPPFLAGS:   $LLVM_CPPFLAGS"
    echo "        LLVM_LDFLAGS:    $LLVM_LDFLAGS"
    echo ""
fi
echo "        PYTHON2:         $PYTHON2"

echo ""
echo "        Run '${MAKE-make}' to build Mesa"
echo ""
