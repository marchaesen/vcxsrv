# Mesa 3-D graphics library
#
# Copyright (C) 2021 Mauro Rossi issor.oruam@gmail.com
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

# Android.mk for libir3decode.a and libir3encode.a

# ---------------------------------------
# Build libir3decode
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	$(decode_SOURCES)

LOCAL_MODULE := libir3decode

LOCAL_MODULE_CLASS := STATIC_LIBRARIES

intermediates := $(call local-generated-sources-dir)

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/src/gallium/include \
	$(MESA_TOP)/src/gallium/auxiliary \
	$(MESA_TOP)/src/freedreno/isa \
	$(intermediates)/isa

LOCAL_GENERATED_SOURCES += $(addprefix $(intermediates)/isa/, ir3-isa.c)

ir3-isa_c_gen := \
	$(MESA_TOP)/src/freedreno/isa/decode.py

ir3-isa_c_deps := \
	$(MESA_TOP)/src/freedreno/isa/ir3.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-common.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat0.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat1.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat2.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat3.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat4.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat5.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat6.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat7.xml \
	$(MESA_TOP)/src/freedreno/isa/isa.py

$(intermediates)/isa/ir3-isa.c: $(ir3-isa_c_deps)
	@mkdir -p $(dir $@)
	$(hide) $(MESA_PYTHON3) $(ir3-isa_c_gen) $< $@

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------
# Build libir3encode
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	$(encode_SOURCES)

LOCAL_MODULE := libir3encode

LOCAL_MODULE_CLASS := STATIC_LIBRARIES

intermediates := $(call local-generated-sources-dir)

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/src/compiler/nir \
	$(MESA_TOP)/src/gallium/include \
	$(MESA_TOP)/src/gallium/auxiliary \
	$(MESA_TOP)/src/freedreno/isa \
	$(intermediates)/isa

# We need libmesa_nir to get NIR's generated include directories.
LOCAL_STATIC_LIBRARIES := \
	libmesa_nir

LOCAL_GENERATED_SOURCES := \
	$(MESA_GEN_NIR_H)

LOCAL_GENERATED_SOURCES += $(addprefix $(intermediates)/isa/, encode.h)

encode_h_gen := \
	$(MESA_TOP)/src/freedreno/isa/encode.py

encode_h_deps := \
	$(MESA_TOP)/src/freedreno/isa/ir3.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-common.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat0.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat1.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat2.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat3.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat4.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat5.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat6.xml \
	$(MESA_TOP)/src/freedreno/isa/ir3-cat7.xml \
	$(MESA_TOP)/src/freedreno/isa/isa.py

$(intermediates)/isa/encode.h: $(encode_h_deps)
	@mkdir -p $(dir $@)
	$(hide) $(MESA_PYTHON3) $(encode_h_gen) $< $@

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
