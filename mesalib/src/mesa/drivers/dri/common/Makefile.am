# Copyright © 2012 Intel Corporation
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

SUBDIRS = xmlpool

include Makefile.sources

EXTRA_DIST = drirc xmlpool.h SConscript

AM_CFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src/ \
	-I$(top_srcdir)/src/mapi \
	-I$(top_srcdir)/src/mesa/ \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	$(LIBDRM_CFLAGS) \
	$(DEFINES) \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	$(VISIBILITY_CFLAGS)

noinst_LTLIBRARIES = \
	libdricommon.la \
	libxmlconfig.la \
	libmegadriver_stub.la

libdricommon_la_SOURCES = $(DRI_COMMON_FILES)

libxmlconfig_la_SOURCES = $(XMLCONFIG_FILES)
libxmlconfig_la_CFLAGS = $(AM_CFLAGS) $(EXPAT_CFLAGS)
libxmlconfig_la_LIBADD = $(EXPAT_LIBS) -lm

libmegadriver_stub_la_SOURCES = $(megadriver_stub_FILES)

sysconf_DATA = drirc
