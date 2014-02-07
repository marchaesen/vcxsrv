dnl Detection and configuration of the visibility feature of gcc
dnl Vincent Torri 2006-02-11
dnl
dnl XCB_CHECK_VISIBILITY([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Check the visibility feature of gcc
dnl
AC_DEFUN([XCB_CHECK_VISIBILITY],
[
AC_MSG_CHECKING([whether ${CC} supports symbol visibility])

save_CFLAGS=${CFLAGS}
CFLAGS="$CFLAGS -fvisibility=hidden -fvisibility-inlines-hidden"
AC_COMPILE_IFELSE(
   [AC_LANG_PROGRAM(
      [[
#pragma GCC visibility push(hidden)
extern void f(int);
#pragma GCC visibility pop
      ]],
      [[]]
    )],
   [AC_DEFINE(
       GCC_HAS_VISIBILITY,
       [],
       [Defined if GCC supports the visibility feature])
    m4_if([$1], [], [:], [$1])
    AC_MSG_RESULT(yes)],
   [m4_if([$2], [], [:], [$2])
    AC_MSG_RESULT(no)])

CFLAGS=${save_CFLAGS}
])

dnl Detection and configuration of the visibility feature of gcc
dnl Vincent Torri 2006-02-11
dnl
dnl XCB_EXTENSION(name, default)
dnl set the X extension
dnl
AC_DEFUN([XCB_EXTENSION],
[
pushdef([UP], translit([$1], [-a-z], [_A-Z]))dnl
pushdef([DOWN], translit([$1], [A-Z], [a-z]))dnl

AC_ARG_ENABLE(DOWN,
    [AS_HELP_STRING([--enable-[]DOWN], [Build XCB $1 Extension (default: $2)])],
    [BUILD_[]UP=$enableval],
    [BUILD_[]UP=$2])

AM_CONDITIONAL(BUILD_[]UP, [test "x$BUILD_[]UP" = "xyes"])
])

dnl End of acinclude.m4
