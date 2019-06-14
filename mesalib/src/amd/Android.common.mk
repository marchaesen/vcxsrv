# Copyright Â© 2016 Red Hat.
# Copyright Â© 2016 Mauro Rossi <issor.oruam@gmail.com>
#
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

# ---------------------------------------
# Build libmesa_amd_common
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_amd_common

LOCAL_SRC_FILES := \
	$(AMD_COMMON_FILES) \
	$(AMD_COMPILER_FILES) \
	$(AMD_DEBUG_FILES) \
	$(AMD_NIR_FILES)

LOCAL_CFLAGS += -DFORCE_BUILD_AMDGPU   # instructs LLVM to declare LLVMInitializeAMDGPU* functions

# generate sources
LOCAL_MODULE_CLASS := STATIC_LIBRARIES
intermediates := $(call local-generated-sources-dir)
LOCAL_GENERATED_SOURCES := $(addprefix $(intermediates)/, $(AMD_GENERATED_FILES))

$(LOCAL_GENERATED_SOURCES): PRIVATE_PYTHON := $(MESA_PYTHON2)
$(LOCAL_GENERATED_SOURCES): PRIVATE_CUSTOM_TOOL = $(PRIVATE_PYTHON) $^ > $@

$(intermediates)/common/sid_tables.h: $(LOCAL_PATH)/common/sid_tables.py $(LOCAL_PATH)/common/sid.h $(LOCAL_PATH)/registers/amdgfxregs.json $(LOCAL_PATH)/registers/pkt3.json
	$(transform-generated-source)

$(intermediates)/common/amdgfxregs.h: $(LOCAL_PATH)/registers/makeregheader.py $(LOCAL_PATH)/registers/amdgfxregs.json $(LOCAL_PATH)/registers/pkt3.json
	$(transform-generated-source) --sort address --guard AMDGFXREGS_H

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/include \
	$(MESA_TOP)/src \
	$(MESA_TOP)/src/amd/common \
	$(MESA_TOP)/src/compiler \
	$(call generated-sources-dir-for,STATIC_LIBRARIES,libmesa_nir,,)/nir \
	$(MESA_TOP)/src/gallium/include \
	$(MESA_TOP)/src/gallium/auxiliary \
	$(MESA_TOP)/src/mesa \
	$(intermediates)/common

LOCAL_EXPORT_C_INCLUDE_DIRS := \
	$(LOCAL_PATH)/common

LOCAL_SHARED_LIBRARIES := \
	libdrm_amdgpu

LOCAL_STATIC_LIBRARIES := \
	libmesa_nir

LOCAL_WHOLE_STATIC_LIBRARIES := \
	libelf

$(call mesa-build-with-llvm)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
