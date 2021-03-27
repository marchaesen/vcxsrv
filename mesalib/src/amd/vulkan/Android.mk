# Copyright © 2018 Advanced Micro Devices, Inc.
# Copyright © 2018 Mauro Rossi issor.oruam@gmail.com

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

LOCAL_PATH := $(call my-dir)

# get VULKAN_FILES and VULKAN_GENERATED_FILES
include $(LOCAL_PATH)/Makefile.sources

# The gallium includes are for the util/u_math.h include from main/macros.h

RADV_COMMON_INCLUDES := \
	$(MESA_TOP)/include \
	$(MESA_TOP)/src/ \
	$(MESA_TOP)/src/amd/vulkan \
	$(MESA_TOP)/src/vulkan/wsi \
	$(MESA_TOP)/src/vulkan/util \
	$(MESA_TOP)/src/amd \
	$(MESA_TOP)/src/mapi \
	$(MESA_TOP)/src/mesa \
	$(MESA_TOP)/src/mesa/drivers/dri/common \
	$(MESA_TOP)/src/gallium/auxiliary \
	$(MESA_TOP)/src/gallium/include \
	frameworks/native/vulkan/include

RADV_SHARED_LIBRARIES := libdrm_amdgpu

ifeq ($(filter $(MESA_ANDROID_MAJOR_VERSION), 4 5 6 7),)
RADV_SHARED_LIBRARIES += libnativewindow
endif

#
# libmesa_radv_common
#

include $(CLEAR_VARS)
LOCAL_MODULE := libmesa_radv_common
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

intermediates := $(call local-generated-sources-dir)

LOCAL_SRC_FILES := \
	$(VULKAN_FILES)

LOCAL_CFLAGS += -DFORCE_BUILD_AMDGPU   # instructs LLVM to declare LLVMInitializeAMDGPU* functions
LOCAL_CFLAGS += -DVK_USE_PLATFORM_ANDROID_KHR

$(call mesa-build-with-llvm)

LOCAL_C_INCLUDES := $(RADV_COMMON_INCLUDES)

LOCAL_STATIC_LIBRARIES := \
	libmesa_aco \
	libmesa_amd_common \
	libmesa_nir \
	libmesa_util \
	libmesa_vulkan_util \
	libmesa_git_sha1

LOCAL_GENERATED_SOURCES += $(intermediates)/radv_entrypoints.c
LOCAL_GENERATED_SOURCES += $(intermediates)/radv_entrypoints.h

RADV_ENTRYPOINTS_SCRIPT := $(MESA_TOP)/src/vulkan/util/vk_entrypoints_gen.py

vulkan_api_xml = $(MESA_TOP)/src/vulkan/registry/vk.xml

$(intermediates)/radv_entrypoints.c: $(RADV_ENTRYPOINTS_SCRIPT) \
					$(vulkan_api_xml)
	@mkdir -p $(dir $@)
	$(MESA_PYTHON2) $(RADV_ENTRYPOINTS_SCRIPT) \
		--xml $(vulkan_api_xml) \
		--proto --weak \
		--out-c $@ \
		--out-h $(addsuffix .h,$(basename $@)) \
		--prefix radv --device-prefix sqtt

$(intermediates)/radv_entrypoints.h: $(intermediates)/radv_entrypoints.c

LOCAL_SHARED_LIBRARIES += $(RADV_SHARED_LIBRARIES)

LOCAL_EXPORT_C_INCLUDE_DIRS := \
	$(MESA_TOP)/src/amd/vulkan \
	$(intermediates)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

#
# libvulkan_radeon
#

include $(CLEAR_VARS)

LOCAL_MODULE := vulkan.radv
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_RELATIVE_PATH := hw

LOCAL_LDFLAGS += -Wl,--build-id=sha1

LOCAL_SRC_FILES := \
	$(VULKAN_ANDROID_FILES)

LOCAL_CFLAGS += -DFORCE_BUILD_AMDGPU   # instructs LLVM to declare LLVMInitializeAMDGPU* functions
LOCAL_CFLAGS += -DVK_USE_PLATFORM_ANDROID_KHR

$(call mesa-build-with-llvm)

LOCAL_C_INCLUDES := $(RADV_COMMON_INCLUDES)

LOCAL_WHOLE_STATIC_LIBRARIES := \
	libmesa_util \
	libmesa_nir \
	libmesa_glsl \
	libmesa_compiler \
	libmesa_amdgpu_addrlib \
	libmesa_amd_common \
	libmesa_radv_common \
	libmesa_vulkan_util \
	libmesa_aco

LOCAL_SHARED_LIBRARIES += $(RADV_SHARED_LIBRARIES) libz libsync liblog libcutils

# If Android version >=8 MESA should static link libexpat else should dynamic link
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 27; echo $$?), 0)
LOCAL_STATIC_LIBRARIES := \
	libexpat
else
LOCAL_SHARED_LIBRARIES += \
	libexpat
endif

include $(MESA_COMMON_MK)
include $(BUILD_SHARED_LIBRARY)
