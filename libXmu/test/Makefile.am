#
# Copyright (c) 2022, 2023, Oracle and/or its affiliates.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

if ENABLE_UNIT_TESTS
if HAVE_GLIB

AM_CPPFLAGS = -D_CONST_X_STRING -I${top_srcdir}/include
AM_CFLAGS = $(CWARNFLAGS) $(XMU_CFLAGS) $(GLIB_CFLAGS)

XMU_TEST_LIBS  = ${top_builddir}/src/libXmu.la $(XMU_LIBS) $(GLIB_LIBS)
XMUU_TEST_LIBS = ${top_builddir}/src/libXmuu.la $(XMUU_LIBS) $(GLIB_LIBS)

check_PROGRAMS = \
        CursorName \
        EditResStream \
        Lower \
        ReadBitmapData \
        reallocarray \
        StrToBS \
        StrToGrav \
        StrToJust \
        StrToLong \
        StrToOrnt \
        StrToShap
TESTS = $(check_PROGRAMS)

TESTS_ENVIRONMENT = $(MALLOC_DEBUG_ENV)
TESTS_ENVIRONMENT += G_TEST_SRCDIR="$(abs_srcdir)"
TESTS_ENVIRONMENT += G_TEST_BUILDDIR="$(abs_builddir)"

LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
                  $(top_srcdir)/tap-driver.sh
LOG_COMPILER = $(srcdir)/tap-test

CursorName_SOURCES = CursorName.c
CursorName_LDADD = $(XMUU_TEST_LIBS)

EditResStream_SOURCES = EditResStream.c
EditResStream_LDADD = $(XMU_TEST_LIBS)

Lower_SOURCES = Lower.c
Lower_LDADD = $(XMUU_TEST_LIBS)

ReadBitmapData_SOURCES = ReadBitmapData.c
ReadBitmapData_LDADD = $(XMU_TEST_LIBS)

reallocarray_SOURCES = reallocarray.c
reallocarray_LDADD = $(GLIB_LIBS)

StrToBS_SOURCES = StrToBS.c
StrToBS_LDADD = $(XMU_TEST_LIBS)

StrToGrav_SOURCES = StrToGrav.c
StrToGrav_LDADD = $(XMU_TEST_LIBS)

StrToJust_SOURCES = StrToJust.c
StrToJust_LDADD = $(XMU_TEST_LIBS)

StrToLong_SOURCES = StrToLong.c
StrToLong_LDADD = $(XMU_TEST_LIBS)

StrToOrnt_SOURCES = StrToOrnt.c
StrToOrnt_LDADD = $(XMU_TEST_LIBS)

StrToShap_SOURCES = StrToShap.c
StrToShap_LDADD = $(XMU_TEST_LIBS)

endif HAVE_GLIB
endif ENABLE_UNIT_TESTS

EXTRA_DIST = \
        bitmaps/plaid \
        bitmaps/star \
        bitmaps/xlogo64 \
        tap-test
