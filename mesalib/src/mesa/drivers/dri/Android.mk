#
# Copyright (C) 2011 Intel Corporation
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
#

LOCAL_PATH := $(call my-dir)

# Import mesa_dri_common_INCLUDES.
include $(LOCAL_PATH)/common/Makefile.sources

#-----------------------------------------------
# Variables common to all DRI drivers

MESA_DRI_CFLAGS := \
	-DHAVE_ANDROID_PLATFORM

MESA_DRI_C_INCLUDES := \
	$(addprefix $(MESA_TOP)/, $(mesa_dri_common_INCLUDES)) \
	$(MESA_TOP)/src/gallium/include \
	$(MESA_TOP)/src/gallium/auxiliary \
	external/expat/lib

MESA_DRI_WHOLE_STATIC_LIBRARIES := \
	libmesa_glsl \
	libmesa_compiler \
	libmesa_nir \
	libmesa_megadriver_stub \
	libmesa_dri_common \
	libmesa_dricore \
	libmesa_util

MESA_DRI_SHARED_LIBRARIES := \
	libcutils \
	libdl \
	libglapi \
	liblog \
	libsync \
	libz

# If Android version >=8 MESA should static link libexpat else should dynamic link
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 27; echo $$?), 0)
MESA_DRI_WHOLE_STATIC_LIBRARIES += \
	libexpat
else
MESA_DRI_SHARED_LIBRARIES += \
	libexpat
endif

#-----------------------------------------------
# Build drivers and libmesa_dri_common

SUBDIRS := common i915 i965
include $(foreach d, $(SUBDIRS), $(LOCAL_PATH)/$(d)/Android.mk)
