# Mesa 3-D graphics library
#
# Copyright (C) 2015 Intel Corporation
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

include $(LOCAL_PATH)/Android.glsl.mk
include $(LOCAL_PATH)/Android.nir.mk

# ---------------------------------------
# Build libmesa_compiler
# ---------------------------------------

include $(CLEAR_VARS)

include $(LOCAL_PATH)/Makefile.sources
LOCAL_SRC_FILES := $(LIBCOMPILER_FILES)

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/src/mapi \
	$(MESA_TOP)/src/mesa \
	$(MESA_TOP)/src/gallium/include \
	$(MESA_TOP)/src/gallium/auxiliary \
	$(dir $(MESA_GEN_GLSL_H))

LOCAL_GENERATED_SOURCES += \
	$(MESA_GEN_GLSL_H)

LOCAL_MODULE := libmesa_compiler

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)


