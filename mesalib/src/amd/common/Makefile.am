# Copyright © 2016 Bas Nieuwenhuizen
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
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

include Makefile.sources

# TODO cleanup these
AM_CPPFLAGS = \
	$(VALGRIND_CFLAGS) \
	$(DEFINES) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/src \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src/compiler \
	-I$(top_builddir)/src/compiler/nir \
	-I$(top_srcdir)/src/compiler \
	-I$(top_srcdir)/src/mapi \
	-I$(top_srcdir)/src/mesa \
	-I$(top_srcdir)/src/mesa/drivers/dri/common \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/include

AM_CFLAGS = $(VISIBILITY_CFLAGS) \
	$(PTHREAD_CFLAGS) \
	$(LLVM_CFLAGS) \
	$(LIBELF_CFLAGS)

AM_CXXFLAGS = \
	$(VISIBILITY_CXXFLAGS) \
	$(LLVM_CXXFLAGS)

noinst_LTLIBRARIES = libamd_common.la

libamd_common_la_SOURCES = $(AMD_COMPILER_SOURCES)
