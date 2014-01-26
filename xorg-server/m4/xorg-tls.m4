dnl Copyright © 2011 Apple Inc.
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
dnl Authors: Jeremy Huddleston <jeremyhu@apple.com>

AC_DEFUN([XORG_TLS], [
    AC_REQUIRE([XORG_STRICT_OPTION])
    AC_MSG_CHECKING(for thread local storage (TLS) support)
    AC_CACHE_VAL(ac_cv_tls, [
        ac_cv_tls=none
        keywords="__thread __declspec(thread)"
        for kw in $keywords ; do
            AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[int $kw test;]], [])], ac_cv_tls=$kw ; break ;)
        done
    ])
    AC_MSG_RESULT($ac_cv_tls)

    if test "$ac_cv_tls" != "none"; then
        AC_MSG_CHECKING(for tls_model attribute support)
        AC_CACHE_VAL(ac_cv_tls_model, [
            save_CFLAGS="$CFLAGS"
            CFLAGS="$CFLAGS $STRICT_CFLAGS"
            AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[int $ac_cv_tls __attribute__((tls_model("initial-exec"))) test;]], [])],
                           ac_cv_tls_model=yes, ac_cv_tls_model=no)
            CFLAGS="$save_CFLAGS"
        ])
        AC_MSG_RESULT($ac_cv_tls_model)

        if test "x$ac_cv_tls_model" = "xyes" ; then
            xorg_tls=$ac_cv_tls' __attribute__((tls_model("initial-exec")))'
        else
            xorg_tls=$ac_cv_tls
        fi

        AC_DEFINE_UNQUOTED([TLS], $xorg_tls, [The compiler supported TLS storage class, prefering initial-exec if tls_model is supported])
    fi
])
