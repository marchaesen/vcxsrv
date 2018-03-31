#
# Copyright © 2012 Jon TURNEY
# Copyright (C) 2015 Intel Corporation
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

include Makefile.sources

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/mapi \
	-I$(top_srcdir)/src/mesa/ \
	-I$(top_builddir)/src/compiler/glsl\
	-I$(top_srcdir)/src/compiler/glsl\
	-I$(top_srcdir)/src/compiler/glsl/glcpp\
	-I$(top_builddir)/src/compiler/nir \
	-I$(top_srcdir)/src/compiler/nir \
	-I$(top_srcdir)/src/compiler/spirv \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gtest/include \
	$(VALGRIND_CFLAGS) \
	$(DEFINES)

AM_CFLAGS = \
	$(VISIBILITY_CFLAGS) \
	$(WNO_OVERRIDE_INIT) \
	$(MSVC2013_COMPAT_CFLAGS)

AM_CXXFLAGS = \
	$(VISIBILITY_CXXFLAGS) \
	$(MSVC2013_COMPAT_CXXFLAGS)

noinst_LTLIBRARIES = libcompiler.la

libcompiler_la_SOURCES = $(LIBCOMPILER_FILES)

noinst_PROGRAMS =
check_PROGRAMS =
TESTS =
BUILT_SOURCES =
CLEANFILES =
EXTRA_DIST = \
	SConscript \
	glsl/glcpp/meson.build \
	glsl/tests/meson.build \
	glsl/meson.build \
	nir/meson.build \
	meson.build

MKDIR_GEN = $(AM_V_at)$(MKDIR_P) $(@D)
PYTHON_GEN = $(AM_V_GEN)$(PYTHON2) $(PYTHON_FLAGS)

include Makefile.spirv.am

include Makefile.glsl.am

include Makefile.nir.am
