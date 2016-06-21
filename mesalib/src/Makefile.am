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

.PHONY: git_sha1.h.tmp
git_sha1.h.tmp:
	@# Don't assume that $(top_srcdir)/.git is a directory. It may be
	@# a gitlink file if $(top_srcdir) is a submodule checkout or a linked
	@# worktree.
	@# If we are building from a release tarball copy the bundled header.
	@if test -e $(top_srcdir)/.git; then \
		if which git > /dev/null; then \
		    git --git-dir=$(top_srcdir)/.git log -n 1 --oneline | \
			sed 's/^\([^ ]*\) .*/#define MESA_GIT_SHA1 "git-\1"/' \
			> git_sha1.h.tmp ; \
		fi \
	else \
		cp $(srcdir)/git_sha1.h git_sha1.h.tmp ;\
		chmod u+w git_sha1.h.tmp; \
	fi

git_sha1.h: git_sha1.h.tmp
	@echo "updating git_sha1.h"
	@if ! cmp -s git_sha1.h.tmp git_sha1.h; then \
		mv git_sha1.h.tmp git_sha1.h ;\
	else \
		rm git_sha1.h.tmp ;\
	fi

BUILT_SOURCES = git_sha1.h

# We want to keep the srcdir file since we need it on rebuild from tarball.
# At the same time `make distclean' gets angry at us if we don't cleanup the
# builddir one.
distclean-local:
	( test $(top_srcdir) != $(top_builddir) && rm $(builddir)/git_sha1.h ) || true

SUBDIRS = . gtest util mapi/glapi/gen mapi

# include only conditionally ?
SUBDIRS += compiler

if HAVE_INTEL_DRIVERS
SUBDIRS += intel
endif

if NEED_OPENGL_COMMON
SUBDIRS += mesa
endif

SUBDIRS += loader

if HAVE_DRI_GLX
SUBDIRS += glx
endif

## Optionally required by GBM and EGL
if HAVE_PLATFORM_WAYLAND
SUBDIRS += egl/wayland/wayland-drm
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

## Requires the i965 compiler (part of mesa) and wayland-drm
if HAVE_INTEL_VULKAN
SUBDIRS += intel/vulkan
endif

if HAVE_GALLIUM
SUBDIRS += gallium
endif

EXTRA_DIST = \
	getopt hgl SConscript git_sha1.h

AM_CFLAGS = $(VISIBILITY_CFLAGS)
AM_CXXFLAGS = $(VISIBILITY_CXXFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include/ \
	-I$(top_srcdir)/src/mapi/ \
	-I$(top_srcdir)/src/mesa/ \
	$(DEFINES)

noinst_LTLIBRARIES = libglsl_util.la

libglsl_util_la_SOURCES = \
	mesa/main/imports.c \
	mesa/program/prog_hash_table.c \
	mesa/program/symbol_table.c \
	mesa/program/dummy_errors.c
