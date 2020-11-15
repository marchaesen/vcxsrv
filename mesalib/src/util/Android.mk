# Mesa 3-D graphics library
#
# Copyright (C) 2014 Tomasz Figa <tomasz.figa@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/Makefile.sources

# ---------------------------------------
# Build libmesa_util
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	$(MESA_UTIL_FILES) \
	$(XMLCONFIG_FILES)

LOCAL_MODULE := libmesa_util

LOCAL_MODULE_CLASS := STATIC_LIBRARIES

intermediates := $(call local-generated-sources-dir)

LOCAL_C_INCLUDES := \
	external/zlib \
	$(MESA_TOP)/src/mesa \
	$(MESA_TOP)/src/mapi \
	$(MESA_TOP)/src/gallium/include \
	$(MESA_TOP)/src/gallium/auxiliary \
	$(MESA_TOP)/src/util/format \
	$(intermediates)/format

# If Android version >=8 MESA should static link libexpat else should dynamic link
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 27; echo $$?), 0)
LOCAL_STATIC_LIBRARIES := \
	libexpat
else
LOCAL_SHARED_LIBRARIES := \
	libexpat
endif

LOCAL_SHARED_LIBRARIES += liblog libsync

# Generated sources

LOCAL_EXPORT_C_INCLUDE_DIRS := $(intermediates)

UTIL_GENERATED_SOURCES := $(addprefix $(intermediates)/,$(MESA_UTIL_GENERATED_FILES))
LOCAL_GENERATED_SOURCES := $(UTIL_GENERATED_SOURCES)

format_srgb_gen := $(LOCAL_PATH)/format_srgb.py

$(intermediates)/format_srgb.c: $(format_srgb_gen)
	@mkdir -p $(dir $@)
	$(hide) $(MESA_PYTHON2) $(format_srgb_gen) $< > $@

u_format_gen := $(LOCAL_PATH)/format/u_format_table.py
u_format_deps := $(LOCAL_PATH)/format/u_format.csv \
	$(LOCAL_PATH)/format/u_format_pack.py \
	$(LOCAL_PATH)/format/u_format_parse.py

$(intermediates)/format/u_format_pack.h: $(u_format_deps)
	@mkdir -p $(dir $@)
	$(hide) $(MESA_PYTHON2) $(u_format_gen) --header $< > $@

$(intermediates)/format/u_format_table.c: $(u_format_deps)
	@mkdir -p $(dir $@)
	$(hide) $(MESA_PYTHON2) $(u_format_gen) $< > $@

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
