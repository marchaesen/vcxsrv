# Copyright © 2016 Broadcom
# Copyright © 2016 Intel Corporation
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

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/src \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/freedreno/ \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/include \
	$(DEFINES)

AM_CFLAGS = \
	$(EXPAT_CFLAGS)

include Makefile.sources

lib_LTLIBRARIES =
check_LTLIBRARIES =
noinst_DATA =
noinst_HEADERS =
noinst_LTLIBRARIES =
noinst_PROGRAMS =
check_PROGRAMS =
TESTS =
BUILT_SOURCES =
CLEANFILES =
EXTRA_DIST = \
	drm/meson.build \
	ir3/meson.build

MKDIR_GEN = $(AM_V_at)$(MKDIR_P) $(@D)
PYTHON_GEN = $(AM_V_GEN)$(PYTHON) $(PYTHON_FLAGS)

CLEANFILES += $(BUILT_SOURCES)

noinst_LTLIBRARIES += libfreedreno_drm.la

libfreedreno_drm_la_SOURCES = $(drm_SOURCES)
libfreedreno_drm_la_CFLAGS = $(VALGRIND_CFLAGS) $(LIBDRM_CFLAGS)

noinst_LTLIBRARIES += libfreedreno_ir3.la

libfreedreno_ir3_la_SOURCES = $(ir3_SOURCES) $(ir3_GENERATED_FILES)
libfreedreno_ir3_la_CFLAGS = \
	-I$(top_srcdir)/src/freedreno/ir3 \
	-I$(top_builddir)/src/compiler/nir \
	-I$(top_srcdir)/src/compiler/nir

ir3/ir3_nir_trig.c: ir3/ir3_nir_trig.py $(top_srcdir)/src/compiler/nir/nir_algebraic.py
	$(MKDIR_GEN)
	$(AM_V_GEN) $(PYTHON) $(PYTHON_FLAGS) $(srcdir)/ir3/ir3_nir_trig.py -p $(top_srcdir)/src/compiler/nir > $@ || ($(RM) $@; false)

