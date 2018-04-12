# Copyright © 2013 Intel Corporation
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

.PHONY: git_sha1.h
git_sha1.h: $(top_srcdir)/src/git_sha1.h.in
	@echo "updating $@"
	@$(PYTHON2) $(top_srcdir)/bin/git_sha1_gen.py --output $@

BUILT_SOURCES = git_sha1.h
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = git_sha1.h.in meson.build

SUBDIRS = . gtest util mapi/glapi/gen mapi

if HAVE_OPENGL
gldir = $(includedir)/GL
gl_HEADERS = \
  $(top_srcdir)/include/GL/gl.h \
  $(top_srcdir)/include/GL/glext.h \
  $(top_srcdir)/include/GL/glcorearb.h \
  $(top_srcdir)/include/GL/gl_mangle.h
endif

if HAVE_GLX
glxdir = $(includedir)/GL
glx_HEADERS = \
  $(top_srcdir)/include/GL/glx.h \
  $(top_srcdir)/include/GL/glxext.h \
  $(top_srcdir)/include/GL/glx_mangle.h
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = mesa/gl.pc
endif

if HAVE_COMMON_OSMESA
osmesadir = $(includedir)/GL
osmesa_HEADERS = $(top_srcdir)/include/GL/osmesa.h
endif

# include only conditionally ?
SUBDIRS += compiler

## Optionally required by EGL
if HAVE_PLATFORM_WAYLAND
SUBDIRS += egl/wayland/wayland-drm
endif

if HAVE_VULKAN_COMMON
SUBDIRS += vulkan
endif

EXTRA_DIST += vulkan/registry/vk.xml

if HAVE_AMD_DRIVERS
SUBDIRS += amd
endif

if HAVE_INTEL_DRIVERS
SUBDIRS += intel
endif

if HAVE_BROADCOM_DRIVERS
SUBDIRS += broadcom
endif

if NEED_OPENGL_COMMON
SUBDIRS += mesa
endif

SUBDIRS += loader

if HAVE_DRI_GLX
SUBDIRS += glx
endif

## Optionally required by EGL (aka PLATFORM_GBM)
if HAVE_GBM
SUBDIRS += gbm
endif

## Optionally required by EGL
if HAVE_PLATFORM_WAYLAND
SUBDIRS += egl/wayland/wayland-egl
endif

if HAVE_EGL
SUBDIRS += egl
endif

# Requires wayland-drm
if HAVE_RADEON_VULKAN
SUBDIRS += amd/vulkan
endif

if HAVE_GALLIUM
SUBDIRS += gallium
endif

EXTRA_DIST += \
	getopt hgl SConscript \
	$(top_srcdir)/include/GL/mesa_glinterop.h

AM_CFLAGS = $(VISIBILITY_CFLAGS)
AM_CXXFLAGS = $(VISIBILITY_CXXFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include/ \
	-I$(top_srcdir)/src/mapi/ \
	-I$(top_srcdir)/src/mesa/ \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	$(DEFINES)

noinst_LTLIBRARIES = libglsl_util.la

libglsl_util_la_SOURCES = \
	mesa/main/extensions_table.c \
	mesa/main/imports.c \
	mesa/program/prog_parameter.c \
	mesa/program/symbol_table.c \
	mesa/program/dummy_errors.c
