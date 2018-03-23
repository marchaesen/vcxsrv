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
LOCAL_CFLAGS += \
	-Wno-error \
	-Wno-unused-parameter \
	-Wno-pointer-arith \
	-Wno-missing-field-initializers \
	-Wno-initializer-overrides \
	-Wno-mismatched-tags \
	-DVERSION=\"$(MESA_VERSION)\" \
	-DPACKAGE_VERSION=\"$(MESA_VERSION)\" \
	-DPACKAGE_BUGREPORT=\"https://bugs.freedesktop.org/enter_bug.cgi?product=Mesa\"

# XXX: The following __STDC_*_MACROS defines should not be needed.
# It's likely due to a bug elsewhere, but let's temporarily add them
# here to fix the radeonsi build.
LOCAL_CFLAGS += \
	-DANDROID_API_LEVEL=$(PLATFORM_SDK_VERSION) \
	-DENABLE_SHADER_CACHE \
	-D__STDC_CONSTANT_MACROS \
	-D__STDC_LIMIT_MACROS \
	-DHAVE___BUILTIN_EXPECT \
	-DHAVE___BUILTIN_FFS \
	-DHAVE___BUILTIN_FFSLL \
	-DHAVE_FUNC_ATTRIBUTE_FLATTEN \
	-DHAVE_FUNC_ATTRIBUTE_UNUSED \
	-DHAVE_FUNC_ATTRIBUTE_FORMAT \
	-DHAVE_FUNC_ATTRIBUTE_PACKED \
	-DHAVE_FUNC_ATTRIBUTE_ALIAS \
	-DHAVE_FUNC_ATTRIBUTE_NORETURN \
	-DHAVE_FUNC_ATTRIBUTE_RETURNS_NONNULL \
	-DHAVE_FUNC_ATTRIBUTE_WARN_UNUSED_RESULT \
	-DHAVE___BUILTIN_CTZ \
	-DHAVE___BUILTIN_POPCOUNT \
	-DHAVE___BUILTIN_POPCOUNTLL \
	-DHAVE___BUILTIN_CLZ \
	-DHAVE___BUILTIN_CLZLL \
	-DHAVE___BUILTIN_UNREACHABLE \
	-DHAVE_PTHREAD=1 \
	-DHAVE_DLADDR \
	-DHAVE_DL_ITERATE_PHDR \
	-DHAVE_LINUX_FUTEX_H \
	-DHAVE_ENDIAN_H \
	-DHAVE_ZLIB \
	-DMAJOR_IN_SYSMACROS \
	-fvisibility=hidden \
	-Wno-sign-compare

LOCAL_CPPFLAGS += \
	-D__STDC_CONSTANT_MACROS \
	-D__STDC_FORMAT_MACROS \
	-D__STDC_LIMIT_MACROS \
	-Wno-error=non-virtual-dtor \
	-Wno-non-virtual-dtor

# mesa requires at least c99 compiler
LOCAL_CONLYFLAGS += \
	-std=c99

ifeq ($(strip $(MESA_ENABLE_ASM)),true)
ifeq ($(TARGET_ARCH),x86)
LOCAL_CFLAGS += \
	-DUSE_X86_ASM

endif
endif
ifeq ($(ARCH_ARM_HAVE_NEON),true)
LOCAL_CFLAGS_arm += -DUSE_ARM_ASM
endif
LOCAL_CFLAGS_arm64 += -DUSE_AARCH64_ASM

ifneq ($(LOCAL_IS_HOST_MODULE),true)
LOCAL_CFLAGS += -DHAVE_LIBDRM
LOCAL_SHARED_LIBRARIES += libdrm
endif

LOCAL_CFLAGS_32 += -DDEFAULT_DRIVER_DIR=\"/vendor/lib/$(MESA_DRI_MODULE_REL_PATH)\"
LOCAL_CFLAGS_64 += -DDEFAULT_DRIVER_DIR=\"/vendor/lib64/$(MESA_DRI_MODULE_REL_PATH)\"
LOCAL_PROPRIETARY_MODULE := true

# uncomment to keep the debug symbols
#LOCAL_STRIP_MODULE := false

ifeq ($(strip $(LOCAL_MODULE_TAGS)),)
LOCAL_MODULE_TAGS := optional
endif

# Quiet down the build system and remove any .h files from the sources
LOCAL_SRC_FILES := $(patsubst %.h, , $(LOCAL_SRC_FILES))
