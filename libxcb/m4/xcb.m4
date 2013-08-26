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

dnl Configure script for doxygen
dnl Vincent Torri 2006-05-11
dnl
dnl XCB_CHECK_DOXYGEN([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for the doxygen program, and define BUILD_DOCS and DOXYGEN.
dnl
AC_DEFUN([XCB_CHECK_DOXYGEN],
[
DOXYGEN="doxygen"

dnl
dnl Disable the build of the documentation
dnl
AC_ARG_ENABLE(
   [build_docs],
   AC_HELP_STRING(
      [--disable-build-docs],
      [Disable the build of the documentation]),
   [if test x"$enableval" != x"yes" ; then
       enable_build_docs="no"
    else
       enable_build_docs="yes"
    fi],
   [enable_build_docs="yes"])

if test "$enable_build_docs" = "no" ; then
    BUILD_DOCS=no
else
dnl
dnl Get the prefix where doxygen is installed.
dnl
AC_ARG_WITH(
   [doxygen],
   AC_HELP_STRING(
      [--with-doxygen=FILE],
      [doxygen program to use (eg /usr/bin/doxygen)]),
   dnl
   dnl Check the given doxygen program.
   dnl
   [DOXYGEN=${withval}
    AC_CHECK_PROG(
       [BUILD_DOCS],
       [${DOXYGEN}],
       [yes],
       [no])
    if test $BUILD_DOCS = no; then
       echo "WARNING:"
       echo "The doxygen program you specified:"
       echo "$DOXYGEN"
       echo "was not found.  Please check the path and make sure "
       echo "the program exists and is executable."
       AC_MSG_WARN(
          [Warning: no doxygen detected. Documentation will not be built])
    fi],
   [AC_CHECK_PROG(
       [BUILD_DOCS],
       [${DOXYGEN}],
       [yes],
       [no])
    if test ${BUILD_DOCS} = no; then
       echo "WARNING:"
       echo "The doxygen program was not found in your execute"
       echo "You may have doxygen installed somewhere not covered by your path."
       echo ""
       echo "If this is the case make sure you have the packages installed, AND"
       echo "that the doxygen program is in your execute path (see your"
       echo "shell manual page on setting the \$PATH environment variable), OR"
       echo "alternatively, specify the program to use with --with-doxygen."
       AC_MSG_WARN(
          [Warning: no doxygen detected. Documentation will not be built])
    fi])
    AC_PATH_PROG(DOT, dot, no)
    if test "$DOT" = "no"; then
        AC_MSG_WARN([Warning: no dot detected. Documentation will not be built])
	BUILD_DOCS="no"
    fi
fi
AC_MSG_CHECKING([whether documentation is built])
AC_MSG_RESULT([${BUILD_DOCS}])

dnl
dnl Substitution
dnl
AC_SUBST([DOXYGEN])

AM_CONDITIONAL(BUILD_DOCS, test "x$BUILD_DOCS" = "xyes")

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
