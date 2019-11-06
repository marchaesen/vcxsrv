# Copyright © 2018 Valve Corporation
# Copyright © 2019 Mauro Rossi issor.oruam@gmail.com

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

include $(LOCAL_PATH)/Makefile.sources

# ---------------------------------------
# Build libmesa_aco
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_aco

# filter-out compiler/aco_instruction_selection_setup.cpp because
# it's already included by compiler/aco_instruction_selection.cpp
LOCAL_SRC_FILES := \
	$(filter-out compiler/aco_instruction_selection_setup.cpp, $(ACO_FILES))

LOCAL_CFLAGS += -DFORCE_BUILD_AMDGPU   # instructs LLVM to declare LLVMInitializeAMDGPU* functions

LOCAL_CPPFLAGS += -Wall -std=c++14

# generate sources
LOCAL_MODULE_CLASS := STATIC_LIBRARIES
intermediates := $(call local-generated-sources-dir)
LOCAL_GENERATED_SOURCES += $(addprefix $(intermediates)/, $(ACO_GENERATED_FILES))

ACO_OPCODES_H_SCRIPT := $(MESA_TOP)/src/amd/compiler/aco_opcodes_h.py
ACO_OPCODES_CPP_SCRIPT := $(MESA_TOP)/src/amd/compiler/aco_opcodes_cpp.py
ACO_BUILDER_H_SCRIPT := $(MESA_TOP)/src/amd/compiler/aco_builder_h.py

ACO_DEPS := $(MESA_TOP)/src/amd/compiler/aco_opcodes.py

$(intermediates)/compiler/aco_opcodes.h: $(ACO_OPCODES_H_SCRIPT) $(ACO_DEPS)
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_PYTHON2) $(ACO_OPCODES_H_SCRIPT) > $@ || ($(RM) $@; false)

$(intermediates)/compiler/aco_opcodes.cpp: $(ACO_OPCODES_CPP_SCRIPT) $(ACO_DEPS)
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_PYTHON2) $(ACO_OPCODES_CPP_SCRIPT) > $@ || ($(RM) $@; false)

$(intermediates)/compiler/aco_builder.h: $(ACO_BUILDER_H_SCRIPT) $(ACO_DEPS)
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_PYTHON2) $(ACO_BUILDER_H_SCRIPT) > $@ || ($(RM) $@; false)

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/src/amd \
	$(MESA_TOP)/src/amd/common \
	$(MESA_TOP)/src/amd/compiler \
	$(MESA_TOP)/src/compiler/nir \
	$(MESA_TOP)/src/mapi \
	$(MESA_TOP)/src/mesa \
	$(intermediates)/compiler

LOCAL_EXPORT_C_INCLUDE_DIRS := \
	$(MESA_TOP)/src/amd/compiler \
	$(intermediates)/compiler

LOCAL_SHARED_LIBRARIES := \
	libdrm_amdgpu

LOCAL_STATIC_LIBRARIES := \
	libmesa_amd_common \
	libmesa_nir

$(call mesa-build-with-llvm)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
