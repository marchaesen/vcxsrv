#
# Copyright (c) 2023, Oracle and/or its affiliates.
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
check_PROGRAMS = XpmCreate XpmMisc XpmRead XpmWrite rgb

TESTS=$(check_PROGRAMS)

AM_CFLAGS = $(CWARNFLAGS) $(XPM_CFLAGS) $(GLIB_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir)/include
LDADD= $(top_builddir)/src/libXpm.la $(GLIB_LIBS)

TESTS_ENVIRONMENT = $(MALLOC_DEBUG_ENV)
TESTS_ENVIRONMENT += G_TEST_SRCDIR="$(abs_srcdir)"
TESTS_ENVIRONMENT += G_TEST_BUILDDIR="$(abs_builddir)"

XpmCreate_SOURCES = XpmCreate.c TestAllFiles.h
XpmMisc_SOURCES = XpmMisc.c
XpmRead_SOURCES = XpmRead.c TestAllFiles.h
XpmWrite_SOURCES = XpmWrite.c CompareXpmImage.h TestAllFiles.h

rgb_SOURCES = rgb.c
rgb_CPPFLAGS = -I$(top_srcdir)/include/X11

LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
		  $(top_srcdir)/tap-driver.sh
LOG_COMPILER = $(srcdir)/tap-test

if COMPRESSED_PIXMAPS
$(check_PROGRAMS): pixmaps/.generated

pixmaps/.generated:
	-rm -rf pixmaps/good/generated pixmaps/invalid/generated pixmaps/.generated
	$(MKDIR_P) pixmaps/good/generated pixmaps/invalid/generated \
	    pixmaps/no-mem/generated
	cp $(srcdir)/pixmaps/good/*.xpm pixmaps/good/generated
	gzip -f pixmaps/good/generated/*.xpm
	cp $(srcdir)/pixmaps/good/*.xpm pixmaps/good/generated
	compress -f pixmaps/good/generated/*.xpm
	cp $(srcdir)/pixmaps/invalid/*.xpm pixmaps/invalid/generated
	gzip -f pixmaps/invalid/generated/*.xpm
	cp $(srcdir)/pixmaps/invalid/*.xpm pixmaps/invalid/generated
	compress -f pixmaps/invalid/generated/*.xpm
	cp $(srcdir)/pixmaps/no-mem/*.xpm pixmaps/no-mem/generated
	gzip -f pixmaps/no-mem/generated/*.xpm
	cp $(srcdir)/pixmaps/no-mem/*.xpm pixmaps/no-mem/generated
	compress -f pixmaps/no-mem/generated/*.xpm
	touch $@

clean-local:
	-rm -rf pixmaps/*/generated pixmaps/.generated
endif COMPRESSED_PIXMAPS

endif HAVE_GLIB
endif ENABLE_UNIT_TESTS

EXTRA_DIST = \
	pixmaps/good/BlueCurves.xpm \
	pixmaps/good/Dimple.xpm \
	pixmaps/good/Dolphins.xpm \
	pixmaps/good/Miniweave.xpm \
	pixmaps/good/Squares.xpm \
	pixmaps/good/Swirl.xpm \
	pixmaps/good/Utah-teapot.xpm \
	pixmaps/good/chromesphere.xpm \
	pixmaps/good/plaid-v1.xpm \
	pixmaps/good/plaid-v2.xpm \
	pixmaps/good/plaid-v3.xpm \
	pixmaps/good/plaid-lisp.xpm \
	pixmaps/good/xorg-bw.xpm \
	pixmaps/good/xorg.xpm \
	pixmaps/invalid/CVE-2016-10164-poc.xpm.gz.gz.gz \
	pixmaps/invalid/doom.xpm \
	pixmaps/invalid/doom2.xpm \
	pixmaps/invalid/invalid-type.xpm \
	pixmaps/invalid/no-contents.xpm \
	pixmaps/invalid/unending-comment-c.xpm \
	pixmaps/invalid/zero-width.xpm \
	pixmaps/invalid/zero-width-v1.xpm \
	pixmaps/no-mem/oversize.xpm \
	rgb.txt \
	tap-test
