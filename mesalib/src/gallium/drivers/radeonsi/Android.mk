# Mesa 3-D graphics library
#
# Copyright (C) 2010-2011 Chia-I Wu <olvaffe@gmail.com>
# Copyright (C) 2010-2011 LunarG Inc.
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

ifeq ($(MESA_ENABLE_LLVM),true)

LOCAL_PATH := $(call my-dir)

# get C_SOURCES and GENERATED_SOURCES
include $(LOCAL_PATH)/Makefile.sources

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(C_SOURCES)

LOCAL_CFLAGS += -DFORCE_BUILD_AMDGPU   # instructs LLVM to declare LLVMInitializeAMDGPU* functions

LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/src/amd/common \
	$(MESA_TOP)/src/amd/llvm \
	$(MESA_TOP)/src/compiler/nir \
	$(call generated-sources-dir-for,STATIC_LIBRARIES,libmesa_amd_common,,)/common \
	$(call generated-sources-dir-for,STATIC_LIBRARIES,libmesa_nir,,)/nir

LOCAL_STATIC_LIBRARIES := \
	libmesa_amd_common \
	libmesa_galliumvl

LOCAL_SHARED_LIBRARIES := libdrm_radeon
LOCAL_MODULE := libmesa_pipe_radeonsi

# We need to get NIR's generated headers.
LOCAL_GENERATED_SOURCES := $(MESA_GEN_NIR_H)

$(call mesa-build-with-llvm)

include $(GALLIUM_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

ifneq ($(HAVE_GALLIUM_RADEONSI),)
GALLIUM_TARGET_DRIVERS += radeonsi
$(eval GALLIUM_LIBS += \
	$(LOCAL_MODULE) \
	$(LOCAL_STATIC_LIBRARIES) \
	libmesa_winsys_radeon \
	libmesa_winsys_amdgpu)
$(eval GALLIUM_SHARED_LIBS += $(LOCAL_SHARED_LIBRARIES))
endif

endif # MESA_ENABLE_LLVM==true
