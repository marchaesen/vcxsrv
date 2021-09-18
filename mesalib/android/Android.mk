# Mesa 3-D graphics library
#
# Copyright (C) 2021 GlobalLogic Ukraine
# Copyright (C) 2021 Roman Stratiienko (r.stratiienko@gmail.com)
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

ifneq ($(filter true, $(BOARD_MESA3D_USES_MESON_BUILD)),)

LOCAL_PATH := $(call my-dir)
MESA3D_TOP := $(dir $(LOCAL_PATH))

LIBDRM_VERSION = $(shell cat external/libdrm/meson.build | grep -o "\<version\>\s*:\s*'\w*\.\w*\.\w*'" | grep -o "\w*\.\w*\.\w*" | head -1)

MESA_VK_LIB_SUFFIX_amd := radeon
MESA_VK_LIB_SUFFIX_intel := intel
MESA_VK_LIB_SUFFIX_freedreno := freedreno
MESA_VK_LIB_SUFFIX_broadcom := broadcom
MESA_VK_LIB_SUFFIX_panfrost := panfrost
MESA_VK_LIB_SUFFIX_virtio-experimental := virtio
MESA_VK_LIB_SUFFIX_swrast := lvp

include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := libc libdl libdrm libm liblog libcutils libz libc++ libnativewindow libsync libhardware
LOCAL_STATIC_LIBRARIES := libexpat libarect libelf
LOCAL_HEADER_LIBRARIES := libnativebase_headers hwvulkan_headers libbacktrace_headers
MESON_GEN_PKGCONFIGS := backtrace cutils expat hardware libdrm:$(LIBDRM_VERSION) nativewindow sync zlib:1.2.11 libelf

ifneq ($(filter swr swrast,$(BOARD_MESA3D_GALLIUM_DRIVERS) $(BOARD_MESA3D_VULKAN_DRIVERS)),)
MESON_GEN_LLVM_STUB := true
endif

ifneq ($(filter zink,$(BOARD_MESA3D_GALLIUM_DRIVERS)),)
LOCAL_SHARED_LIBRARIES += libvulkan
MESON_GEN_PKGCONFIGS += vulkan
endif

ifneq ($(filter iris,$(BOARD_MESA3D_GALLIUM_DRIVERS)),)
LOCAL_SHARED_LIBRARIES += libdrm_intel
MESON_GEN_PKGCONFIGS += libdrm_intel:$(LIBDRM_VERSION)
endif

ifneq ($(filter radeonsi amd,$(BOARD_MESA3D_GALLIUM_DRIVERS) $(BOARD_MESA3D_VULKAN_DRIVERS)),)
MESON_GEN_LLVM_STUB := true
LOCAL_CFLAGS += -DFORCE_BUILD_AMDGPU   # instructs LLVM to declare LLVMInitializeAMDGPU* functions
LOCAL_SHARED_LIBRARIES += libdrm_amdgpu
MESON_GEN_PKGCONFIGS += libdrm_amdgpu:$(LIBDRM_VERSION)
endif

ifneq ($(filter radeonsi r300 r600,$(BOARD_MESA3D_GALLIUM_DRIVERS)),)
LOCAL_SHARED_LIBRARIES += libdrm_radeon
MESON_GEN_PKGCONFIGS += libdrm_radeon:$(LIBDRM_VERSION)
endif

ifneq ($(filter nouveau,$(BOARD_MESA3D_GALLIUM_DRIVERS)),)
LOCAL_SHARED_LIBRARIES += libdrm_nouveau
MESON_GEN_PKGCONFIGS += libdrm_nouveau:$(LIBDRM_VERSION)
endif

ifneq ($(MESON_GEN_LLVM_STUB),)
MESON_LLVM_VERSION := 12.0.0
# Required for swr gallium target
MESON_LLVM_IRBUILDER_PATH := external/llvm-project/llvm/include/llvm/IR/IRBuilder.h
LOCAL_SHARED_LIBRARIES += libLLVM12
endif

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 30; echo $$?), 0)
LOCAL_SHARED_LIBRARIES += \
    android.hardware.graphics.mapper@4.0 \
    libgralloctypes \
    libhidlbase \
    libutils

MESON_GEN_PKGCONFIGS += android.hardware.graphics.mapper:4.0
endif

ifeq ($(TARGET_IS_64_BIT),true)
LOCAL_MULTILIB := 64
else
LOCAL_MULTILIB := 32
endif
include $(LOCAL_PATH)/mesa3d_cross.mk

ifdef TARGET_2ND_ARCH
LOCAL_MULTILIB := 32
include $(LOCAL_PATH)/mesa3d_cross.mk
endif

#-------------------------------------------------------------------------------

define mesa3d-lib
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE := $1
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_RELATIVE_PATH := $3
ifdef TARGET_2ND_ARCH
LOCAL_SRC_FILES_$(TARGET_ARCH) := $(call relative_top_path,$(LOCAL_PATH))$($4)
LOCAL_SRC_FILES_$(TARGET_2ND_ARCH) := $(call relative_top_path,$(LOCAL_PATH))$(2ND_$4)
LOCAL_MULTILIB := both
else
LOCAL_SRC_FILES := $(call relative_top_path,$(LOCAL_PATH))$($4)
endif
LOCAL_CHECK_ELF_FILES := false
LOCAL_MODULE_SUFFIX := .so
LOCAL_MODULE_SYMLINKS := $1$2
include $(BUILD_PREBUILT)
include $(CLEAR_VARS)
endef

__MY_SHARED_LIBRARIES := $(LOCAL_SHARED_LIBRARIES)
include $(CLEAR_VARS)
LOCAL_SHARED_LIBRARIES := $(__MY_SHARED_LIBRARIES)

# Module 'libgallium_dri', produces '/vendor/lib{64}/dri/libgallium_dri.so'
# This module also trigger DRI symlinks creation process
$(eval $(call mesa3d-lib,libgallium_dri,.so.0,dri,MESA3D_GALLIUM_DRI_BIN))
# Module 'libglapi', produces '/vendor/lib{64}/libglapi.so'
$(eval $(call mesa3d-lib,libglapi,.so.0,,MESA3D_LIBGLAPI_BIN))

# Module 'libEGL_mesa', produces '/vendor/lib{64}/egl/libEGL_mesa.so'
$(eval $(call mesa3d-lib,libEGL_mesa,.so.1,egl,MESA3D_LIBEGL_BIN))
# Module 'libGLESv1_CM_mesa', produces '/vendor/lib{64}/egl/libGLESv1_CM_mesa.so'
$(eval $(call mesa3d-lib,libGLESv1_CM_mesa,.so.1,egl,MESA3D_LIBGLESV1_BIN))
# Module 'libGLESv2_mesa', produces '/vendor/lib{64}/egl/libGLESv2_mesa.so'
$(eval $(call mesa3d-lib,libGLESv2_mesa,.so.2,egl,MESA3D_LIBGLESV2_BIN))

# Modules 'vulkan.{driver_name}', produces '/vendor/lib{64}/hw/vulkan.{driver_name}.so' HAL
$(foreach driver,$(BOARD_MESA3D_VULKAN_DRIVERS), \
    $(eval $(call mesa3d-lib,vulkan.$(MESA_VK_LIB_SUFFIX_$(driver)),.so.0,hw,MESA3D_VULKAN_$(driver)_BIN)))

ifneq ($(filter true, $(BOARD_MESA3D_BUILD_LIBGBM)),)
LOCAL_EXPORT_C_INCLUDE_DIRS := $(MESA3D_TOP)/src/gbm/main

# Modules 'libgbm', produces '/vendor/lib{64}/libgbm.so'
$(eval $(call mesa3d-lib,libgbm,.so.1,,MESA3D_LIBGBM_BIN))
endif

#-------------------------------------------------------------------------------

endif
