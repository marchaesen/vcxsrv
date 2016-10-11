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

# BOARD_GPU_DRIVERS should be defined.  The valid values are
#
#   classic drivers: i915 i965
#   gallium drivers: swrast freedreno i915g ilo nouveau r300g r600g radeonsi vc4 virgl vmwgfx
#
# The main target is libGLES_mesa.  For each classic driver enabled, a DRI
# module will also be built.  DRI modules will be loaded by libGLES_mesa.

MESA_TOP := $(call my-dir)

MESA_ANDROID_MAJOR_VERSION := $(word 1, $(subst ., , $(PLATFORM_VERSION)))
MESA_ANDROID_MINOR_VERSION := $(word 2, $(subst ., , $(PLATFORM_VERSION)))
MESA_ANDROID_VERSION := $(MESA_ANDROID_MAJOR_VERSION).$(MESA_ANDROID_MINOR_VERSION)
ifeq ($(filter 1 2 3 4,$(MESA_ANDROID_MAJOR_VERSION)),)
MESA_LOLLIPOP_BUILD := true
else
define local-generated-sources-dir
$(call local-intermediates-dir)
endef
endif

MESA_DRI_MODULE_REL_PATH := dri
MESA_DRI_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/$(MESA_DRI_MODULE_REL_PATH)
MESA_DRI_MODULE_UNSTRIPPED_PATH := $(TARGET_OUT_SHARED_LIBRARIES_UNSTRIPPED)/$(MESA_DRI_MODULE_REL_PATH)

MESA_COMMON_MK := $(MESA_TOP)/Android.common.mk
MESA_PYTHON2 := python

classic_drivers := i915 i965
gallium_drivers := swrast freedreno i915g ilo nouveau r300g r600g radeonsi vmwgfx vc4 virgl

MESA_GPU_DRIVERS := $(strip $(BOARD_GPU_DRIVERS))

# warn about invalid drivers
invalid_drivers := $(filter-out \
	$(classic_drivers) $(gallium_drivers), $(MESA_GPU_DRIVERS))
ifneq ($(invalid_drivers),)
$(warning invalid GPU drivers: $(invalid_drivers))
# tidy up
MESA_GPU_DRIVERS := $(filter-out $(invalid_drivers), $(MESA_GPU_DRIVERS))
endif

# host and target must be the same arch to generate matypes.h
ifeq ($(TARGET_ARCH),$(HOST_ARCH))
MESA_ENABLE_ASM := true
else
MESA_ENABLE_ASM := false
endif

ifneq ($(filter $(classic_drivers), $(MESA_GPU_DRIVERS)),)
MESA_BUILD_CLASSIC := true
else
MESA_BUILD_CLASSIC := false
endif

ifneq ($(filter $(gallium_drivers), $(MESA_GPU_DRIVERS)),)
MESA_BUILD_GALLIUM := true
else
MESA_BUILD_GALLIUM := false
endif

MESA_ENABLE_LLVM := $(if $(filter radeonsi,$(MESA_GPU_DRIVERS)),true,false)

# add subdirectories
ifneq ($(strip $(MESA_GPU_DRIVERS)),)

SUBDIRS := \
	src/gbm \
	src/loader \
	src/mapi \
	src/compiler \
	src/mesa \
	src/util \
	src/egl \
	src/amd \
	src/intel \
	src/mesa/drivers/dri

INC_DIRS := $(call all-named-subdir-makefiles,$(SUBDIRS))

ifeq ($(strip $(MESA_BUILD_GALLIUM)),true)
INC_DIRS += $(call all-named-subdir-makefiles,src/gallium)
endif

include $(INC_DIRS)

endif
