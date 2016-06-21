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

ifeq ($(LOCAL_IS_HOST_MODULE),true)
LOCAL_CFLAGS += -D_GNU_SOURCE
endif

LOCAL_C_INCLUDES += \
	$(MESA_TOP)/src \
	$(MESA_TOP)/include

MESA_VERSION := $(shell cat $(MESA_TOP)/VERSION)
# define ANDROID_VERSION (e.g., 4.0.x => 0x0400)
LOCAL_CFLAGS += \
	-Wno-unused-parameter \
	-Wno-date-time \
	-Wno-pointer-arith \
	-Wno-missing-field-initializers \
	-Wno-initializer-overrides \
	-Wno-mismatched-tags \
	-DPACKAGE_VERSION=\"$(MESA_VERSION)\" \
	-DPACKAGE_BUGREPORT=\"https://bugs.freedesktop.org/enter_bug.cgi?product=Mesa\" \
	-DANDROID_VERSION=0x0$(MESA_ANDROID_MAJOR_VERSION)0$(MESA_ANDROID_MINOR_VERSION)

LOCAL_CFLAGS += \
	-D__STDC_LIMIT_MACROS \
	-DHAVE___BUILTIN_EXPECT \
	-DHAVE___BUILTIN_FFS \
	-DHAVE___BUILTIN_FFSLL \
	-DHAVE_FUNC_ATTRIBUTE_FLATTEN \
	-DHAVE_FUNC_ATTRIBUTE_UNUSED \
	-DHAVE_FUNC_ATTRIBUTE_FORMAT \
	-DHAVE_FUNC_ATTRIBUTE_PACKED \
	-DHAVE___BUILTIN_CTZ \
	-DHAVE___BUILTIN_POPCOUNT \
	-DHAVE___BUILTIN_POPCOUNTLL \
	-DHAVE___BUILTIN_CLZ \
	-DHAVE___BUILTIN_CLZLL \
	-DHAVE___BUILTIN_UNREACHABLE \
	-DHAVE_PTHREAD=1 \
	-DHAVE_DLOPEN \
	-fvisibility=hidden \
	-Wno-sign-compare

# mesa requires at least c99 compiler
LOCAL_CONLYFLAGS += \
	-std=c99

ifeq ($(strip $(MESA_ENABLE_ASM)),true)
ifeq ($(TARGET_ARCH),x86)
LOCAL_CFLAGS += \
	-DUSE_X86_ASM \

endif
endif

ifeq ($(MESA_ENABLE_LLVM),true)
LOCAL_CFLAGS += \
	-DHAVE_LLVM=0x0305 -DMESA_LLVM_VERSION_PATCH=2 \
	-D__STDC_CONSTANT_MACROS \
	-D__STDC_FORMAT_MACROS \
	-D__STDC_LIMIT_MACROS
endif

# add libdrm if there are hardware drivers
ifneq ($(filter-out swrast,$(MESA_GPU_DRIVERS)),)
LOCAL_CFLAGS += -DHAVE_LIBDRM
LOCAL_SHARED_LIBRARIES += libdrm
endif

LOCAL_CPPFLAGS += \
	$(if $(filter true,$(MESA_LOLLIPOP_BUILD)),-D_USING_LIBCXX) \
	-Wno-error=non-virtual-dtor \
	-Wno-non-virtual-dtor

ifeq ($(MESA_LOLLIPOP_BUILD),true)
  LOCAL_CFLAGS_32 += -DDEFAULT_DRIVER_DIR=\"/system/lib/$(MESA_DRI_MODULE_REL_PATH)\"
  LOCAL_CFLAGS_64 += -DDEFAULT_DRIVER_DIR=\"/system/lib64/$(MESA_DRI_MODULE_REL_PATH)\"
else
  LOCAL_CFLAGS += -DDEFAULT_DRIVER_DIR=\"/system/lib/$(MESA_DRI_MODULE_REL_PATH)\"
endif

# uncomment to keep the debug symbols
#LOCAL_STRIP_MODULE := false

ifeq ($(strip $(LOCAL_MODULE_TAGS)),)
LOCAL_MODULE_TAGS := optional
endif

# Quiet down the build system and remove any .h files from the sources
LOCAL_SRC_FILES := $(patsubst %.h, , $(LOCAL_SRC_FILES))
