# Mesa 3-D graphics library
#
# Copyright (C)
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

# Android.mk for libfreedreno_ir3.a

# ---------------------------------------
# Build libfreedreno_ir3
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	$(ir3_SOURCES)

LOCAL_MODULE := libfreedreno_ir3

LOCAL_MODULE_CLASS := STATIC_LIBRARIES

intermediates := $(call local-generated-sources-dir)

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/src/compiler/nir \
	$(MESA_TOP)/src/gallium/include \
	$(MESA_TOP)/src/gallium/auxiliary \
	$(MESA_TOP)/prebuilt-intermediates/nir \
	$(MESA_TOP)/src/freedreno/common \
	$(MESA_TOP)/src/freedreno/ir3 \
	$(intermediates)/ir3

# We need libmesa_nir to get NIR's generated include directories.
LOCAL_STATIC_LIBRARIES := \
	libmesa_nir

LOCAL_GENERATED_SOURCES := \
	$(MESA_GEN_GLSL_H) \
	$(MESA_GEN_NIR_H)

LOCAL_GENERATED_SOURCES += $(addprefix $(intermediates)/, \
	$(ir3_GENERATED_FILES))

ir3_lexer_deps := \
	$(MESA_TOP)/src/freedreno/ir3/ir3_lexer.l

ir3_nir_imul_deps := \
	$(MESA_TOP)/src/freedreno/ir3/ir3_nir_imul.py \
	$(MESA_TOP)/src/compiler/nir/nir_algebraic.py

ir3_nir_trig_deps := \
	$(MESA_TOP)/src/freedreno/ir3/ir3_nir_trig.py \
	$(MESA_TOP)/src/compiler/nir/nir_algebraic.py

ir3_parser_deps := \
	$(MESA_TOP)/src/freedreno/ir3/ir3_parser.y

$(intermediates)/ir3/ir3_lexer.c: $(ir3_lexer_deps)
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_LEX) -o $@ $<

$(intermediates)/ir3/ir3_nir_imul.c: $(ir3_nir_imul_deps)
	@mkdir -p $(dir $@)
	$(hide) $(MESA_PYTHON2) $< -p $(MESA_TOP)/src/compiler/nir > $@

$(intermediates)/ir3/ir3_nir_trig.c: $(ir3_nir_trig_deps)
	@mkdir -p $(dir $@)
	$(hide) $(MESA_PYTHON2) $< -p $(MESA_TOP)/src/compiler/nir > $@

$(intermediates)/ir3/ir3_parser.c: $(ir3_parser_deps)
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(BISON) $< --name-prefix=ir3_yy --output=$@

$(intermediates)/ir3/ir3_parser.h: $(ir3_parser_deps)
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(BISON) $< --name-prefix=ir3_yy --defines=$@

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
