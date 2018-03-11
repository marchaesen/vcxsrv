# Copyright © 2016 Red Hat
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

noinst_HEADERS = \
	$(top_srcdir)/include/vulkan/vk_platform.h \
	$(top_srcdir)/include/vulkan/vulkan.h

lib_LTLIBRARIES = libvulkan_radeon.la

# The gallium includes are for the util/u_math.h include from main/macros.h

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/src \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/vulkan/wsi \
	-I$(top_builddir)/src/vulkan/util \
	-I$(top_srcdir)/src/vulkan/util \
	-I$(top_srcdir)/src/amd \
	-I$(top_srcdir)/src/amd/common \
	-I$(top_builddir)/src/compiler \
	-I$(top_builddir)/src/compiler/nir \
	-I$(top_srcdir)/src/compiler \
	-I$(top_srcdir)/src/mapi \
	-I$(top_srcdir)/src/mesa \
	-I$(top_srcdir)/src/mesa/drivers/dri/common \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/include \
	$(AMDGPU_CFLAGS) \
	$(VALGRIND_CFLAGS) \
	$(DEFINES)

AM_CFLAGS = \
	$(VISIBILITY_CFLAGS) \
	$(PTHREAD_CFLAGS) \
	$(LLVM_CFLAGS)

VULKAN_SOURCES = \
	$(VULKAN_GENERATED_FILES) \
	$(VULKAN_FILES)

VULKAN_LIB_DEPS = \
	libvulkan_common.la \
	$(top_builddir)/src/vulkan/libvulkan_util.la \
	$(top_builddir)/src/vulkan/libvulkan_wsi.la \
	$(top_builddir)/src/amd/common/libamd_common.la \
	$(top_builddir)/src/amd/addrlib/libamdgpu_addrlib.la \
	$(top_builddir)/src/compiler/nir/libnir.la \
	$(top_builddir)/src/util/libmesautil.la \
	$(LLVM_LIBS) \
	$(LIBELF_LIBS) \
	$(PTHREAD_LIBS) \
	$(AMDGPU_LIBS) \
	$(LIBDRM_LIBS) \
	$(PTHREAD_LIBS) \
	$(DLOPEN_LIBS) \
	-lm

if HAVE_PLATFORM_X11
AM_CPPFLAGS += \
	$(XCB_DRI3_CFLAGS) \
	-DVK_USE_PLATFORM_XCB_KHR \
	-DVK_USE_PLATFORM_XLIB_KHR

VULKAN_SOURCES += $(VULKAN_WSI_X11_FILES)

VULKAN_LIB_DEPS += $(XCB_DRI3_LIBS)
endif


if HAVE_PLATFORM_WAYLAND
AM_CPPFLAGS += \
	$(WAYLAND_CLIENT_CFLAGS) \
	-DVK_USE_PLATFORM_WAYLAND_KHR

VULKAN_SOURCES += $(VULKAN_WSI_WAYLAND_FILES)

VULKAN_LIB_DEPS += \
	$(WAYLAND_CLIENT_LIBS)
endif

if HAVE_PLATFORM_ANDROID
AM_CPPFLAGS += $(ANDROID_CPPFLAGS)
AM_CFLAGS += $(ANDROID_CFLAGS)
VULKAN_LIB_DEPS += $(ANDROID_LIBS)
VULKAN_SOURCES += $(VULKAN_ANDROID_FILES)
endif

noinst_LTLIBRARIES = libvulkan_common.la
libvulkan_common_la_SOURCES = $(VULKAN_SOURCES)

nodist_EXTRA_libvulkan_radeon_la_SOURCES = dummy.cpp
libvulkan_radeon_la_SOURCES = $(VULKAN_GEM_FILES)

vulkan_api_xml = $(top_srcdir)/src/vulkan/registry/vk.xml
vk_android_native_buffer_xml = $(top_srcdir)/src/vulkan/registry/vk_android_native_buffer.xml

radv_entrypoints.c: radv_entrypoints_gen.py radv_extensions.py $(vulkan_api_xml)
	$(MKDIR_GEN)
	$(AM_V_GEN)$(PYTHON2) $(srcdir)/radv_entrypoints_gen.py \
		--xml $(vulkan_api_xml) \
		--xml $(vk_android_native_buffer_xml) \
		--outdir $(builddir)
radv_entrypoints.h: radv_entrypoints.c

radv_extensions.c: radv_extensions.py \
		 $(vulkan_api_xml)
	$(MKDIR_GEN)
	$(AM_V_GEN)$(PYTHON2) $(srcdir)/radv_extensions.py \
		--xml $(vulkan_api_xml) \
		--xml $(vk_android_native_buffer_xml) \
		--out-c radv_extensions.c \
		--out-h radv_extensions.h
radv_extensions.h: radv_extensions.c

vk_format_table.c: vk_format_table.py \
		   vk_format_parse.py \
                   vk_format_layout.csv
	$(PYTHON2) $(srcdir)/vk_format_table.py $(srcdir)/vk_format_layout.csv > $@

BUILT_SOURCES = $(VULKAN_GENERATED_FILES)
CLEANFILES = $(BUILT_SOURCES) dev_icd.json radeon_icd.@host_cpu@.json
EXTRA_DIST = \
	$(top_srcdir)/include/vulkan/vk_icd.h \
	radv_entrypoints_gen.py \
	radv_extensions.py \
	radv_icd.py \
	vk_format_layout.csv \
	vk_format_parse.py \
	vk_format_table.py \
	meson.build

libvulkan_radeon_la_LIBADD = $(VULKAN_LIB_DEPS)

libvulkan_radeon_la_LDFLAGS = \
	-shared \
	-module \
	-no-undefined \
	-avoid-version \
	$(BSYMBOLIC) \
	$(LLVM_LDFLAGS) \
	$(GC_SECTIONS) \
	$(LD_NO_UNDEFINED)


icdconfdir = @VULKAN_ICD_INSTALL_DIR@
icdconf_DATA = radeon_icd.@host_cpu@.json
# The following is used for development purposes, by setting VK_ICD_FILENAMES.
noinst_DATA = dev_icd.json

dev_icd.json : radv_extensions.py radv_icd.py
	$(AM_V_GEN)$(PYTHON2) $(srcdir)/radv_icd.py \
		--lib-path="${abs_top_builddir}/${LIB_DIR}" --out $@

radeon_icd.@host_cpu@.json : radv_extensions.py radv_icd.py
	$(AM_V_GEN)$(PYTHON2) $(srcdir)/radv_icd.py \
		--lib-path="${libdir}" --out $@

include $(top_srcdir)/install-lib-links.mk
