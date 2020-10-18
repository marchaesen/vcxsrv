# Copyright © 2019 Collabora Ltd.
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

# build libpanfrost_midgard_disasm
include $(CLEAR_VARS)

LOCAL_MODULE := libpanfrost_midgard_disasm

LOCAL_SRC_FILES := \
	$(midgard_disasm_FILES)

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/include \
	$(MESA_TOP)/src/compiler/nir/ \
	$(MESA_TOP)/src/gallium/auxiliary/ \
	$(MESA_TOP)/src/gallium/include/ \
	$(MESA_TOP)/src/mapi/ \
	$(MESA_TOP)/src/mesa/ \
	$(MESA_TOP)/src/panfrost/include/ \
	$(MESA_TOP)/src/panfrost/midgard/

LOCAL_EXPORT_C_INCLUDE_DIRS := \
	$(MESA_TOP)/src/panfrost/midgard/ \

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

# build libpanfrost_midgard
include $(CLEAR_VARS)

LOCAL_MODULE := libpanfrost_midgard
LOCAL_MODULE_CLASS := STATIC_LIBRARIES
intermediates := $(call local-generated-sources-dir)

LOCAL_SRC_FILES := \
	$(midgard_FILES)

LOCAL_GENERATED_SOURCES := \
	$(MESA_GEN_GLSL_H) \
	$(intermediates)/midgard_nir_algebraic.c

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/include \
	$(MESA_TOP)/src/compiler/nir/ \
	$(MESA_TOP)/src/gallium/auxiliary/ \
	$(MESA_TOP)/src/gallium/include/ \
	$(MESA_TOP)/src/mapi/ \
	$(MESA_TOP)/src/mesa/ \
	$(MESA_TOP)/src/panfrost/include/ \
	$(MESA_TOP)/src/panfrost/midgard/

LOCAL_STATIC_LIBRARIES := \
	libmesa_glsl \
	libmesa_nir \
	libmesa_st_mesa \
	libpanfrost_util \
	libpanfrost_midgard_disasm

midgard_nir_algebraic_gen := $(LOCAL_PATH)/midgard/midgard_nir_algebraic.py
midgard_nir_algebraic_deps := \
	$(MESA_TOP)/src/compiler/nir/

$(intermediates)/midgard_nir_algebraic.c: $(midgard_nir_algebraic_deps)
	@mkdir -p $(dir $@)
	$(hide) $(MESA_PYTHON3) $(midgard_nir_algebraic_gen) -p $< > $@

LOCAL_EXPORT_C_INCLUDE_DIRS := \
	$(MESA_TOP)/src/panfrost/midgard/ \

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
