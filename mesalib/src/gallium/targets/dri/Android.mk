# Mesa 3-D graphics library
#
# Copyright (C) 2015 Chih-Wei Huang <cwhuang@linux.org.tw>
# Copyright (C) 2015 Android-x86 Open Source Project
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

include $(CLEAR_VARS)

LOCAL_MODULE := gallium_dri

LOCAL_MODULE_RELATIVE_PATH := $(MESA_DRI_MODULE_REL_PATH)
LOCAL_SRC_FILES := target.c

LOCAL_CFLAGS :=

# We need --undefined-version as some functions in dri.sym may be missing
# depending on which drivers are enabled or not. Otherwise, we get the error:
# "version script assignment of  to symbol FOO failed: symbol not defined"
LOCAL_LDFLAGS := \
	-Wl,--version-script=$(LOCAL_PATH)/dri.sym \
	-Wl,--undefined-version

LOCAL_SHARED_LIBRARIES := \
	libdl \
	libglapi \
	libz \
	liblog \
	libsync

# If Android version >=8 MESA should static link libexpat else should dynamic link
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 27; echo $$?), 0)
LOCAL_STATIC_LIBRARIES := \
	libexpat
else
LOCAL_SHARED_LIBRARIES += \
	libexpat
endif

LOCAL_STATIC_LIBRARIES += \
	libetnaviv_drm \
	libfreedreno_common \
	libfreedreno_drm \
	libfreedreno_ir2 \
	libfreedreno_ir3 \
	libfreedreno_perfcntrs \
	libmesa_gallium \
	libpanfrost_lib \
	libpanfrost_bifrost \
	libpanfrost_bifrost_disasm \
	libpanfrost_midgard \
	libpanfrost_midgard_disasm \
	libpanfrost_shared \
	libpanfrost_util \

ifeq ($(USE_LIBBACKTRACE),true)
	LOCAL_SHARED_LIBRARIES += libbacktrace
endif

$(foreach d, $(MESA_BUILD_GALLIUM), $(eval LOCAL_CFLAGS += $(patsubst HAVE_%,-D%,$(d))))

# sort GALLIUM_LIBS to remove any duplicates
LOCAL_WHOLE_STATIC_LIBRARIES := \
	$(sort $(GALLIUM_LIBS)) \
	libmesa_st_dri \
	libmesa_st_mesa \
	libmesa_glsl \
	libmesa_compiler \
	libmesa_nir \
	libmesa_dri_common \
	libmesa_megadriver_stub \
	libmesa_pipe_loader \
	libmesa_util \
	libmesa_loader

LOCAL_SHARED_LIBRARIES += libcutils

# sort GALLIUM_SHARED_LIBS to remove any duplicates
LOCAL_SHARED_LIBRARIES += $(sort $(GALLIUM_SHARED_LIBS))

ifneq ($(filter 5 6 7, $(MESA_ANDROID_MAJOR_VERSION)),)
LOCAL_POST_INSTALL_CMD := \
	$(foreach l, lib $(if $(filter true,$(TARGET_IS_64_BIT)),lib64), \
	  $(eval MESA_DRI_MODULE_PATH := $(TARGET_OUT_VENDOR)/$(l)/$(MESA_DRI_MODULE_REL_PATH)) \
	  mkdir -p $(MESA_DRI_MODULE_PATH); \
	  $(foreach d, $(GALLIUM_TARGET_DRIVERS), ln -sf gallium_dri.so $(MESA_DRI_MODULE_PATH)/$(d)_dri.so;) \
	)
else
LOCAL_MODULE_SYMLINKS := $(foreach d, $(GALLIUM_TARGET_DRIVERS), $(d)_dri.so)
endif

include $(GALLIUM_COMMON_MK)
include $(BUILD_SHARED_LIBRARY)
