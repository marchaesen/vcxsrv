# Copyright © 2014 Intel Corporation
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
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

SUBDIRS = . \
	xmlpool \
	tests/hash_table \
	tests/string_buffer

if HAVE_STD_CXX11
SUBDIRS += tests/vma
endif

include Makefile.sources

noinst_LTLIBRARIES = \
	libmesautil.la \
	libxmlconfig.la

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-I$(top_srcdir)/include

libmesautil_la_CPPFLAGS = \
	$(DEFINES) \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/mapi \
	-I$(top_srcdir)/src/mesa \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	$(VISIBILITY_CFLAGS) \
	$(MSVC2013_COMPAT_CFLAGS) \
	$(ZLIB_CFLAGS)

libmesautil_la_SOURCES = \
	$(MESA_UTIL_FILES) \
	$(MESA_UTIL_GENERATED_FILES)

libmesautil_la_LIBADD = \
	$(PTHREAD_LIBS) \
	$(CLOCK_LIB) \
	$(ZLIB_LIBS) \
	$(LIBATOMIC_LIBS)

libxmlconfig_la_SOURCES = $(XMLCONFIG_FILES)
libxmlconfig_la_CFLAGS = \
	$(DEFINES) \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	$(VISIBILITY_CFLAGS) \
	$(EXPAT_CFLAGS)
libxmlconfig_la_LIBADD = $(EXPAT_LIBS) -lm

sysconf_DATA = drirc

u_atomic_test_LDADD = libmesautil.la
roundeven_test_LDADD = -lm
mesa_sha1_test_LDADD = libmesautil.la

check_PROGRAMS = u_atomic_test roundeven_test mesa-sha1_test
TESTS = $(check_PROGRAMS)

BUILT_SOURCES = $(MESA_UTIL_GENERATED_FILES)
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = \
	drirc \
	format_srgb.py \
	merge_driinfo.py \
	SConscript \
	xmlpool.h \
	sha1/README \
	meson.build

PYTHON_GEN = $(AM_V_GEN)$(PYTHON2) $(PYTHON_FLAGS)

format_srgb.c: format_srgb.py
	$(PYTHON_GEN) $(srcdir)/format_srgb.py > $@
