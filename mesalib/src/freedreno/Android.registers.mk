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

# Android.mk for libfreedreno_registers.a

# ---------------------------------------
# Build libfreedreno_registers
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_MODULE := libfreedreno_registers

LOCAL_MODULE_CLASS := STATIC_LIBRARIES

intermediates := $(call local-generated-sources-dir)

# dummy.c source file is generated to meet the build system's rules.
LOCAL_GENERATED_SOURCES += $(intermediates)/dummy.c

$(intermediates)/dummy.c:
	@mkdir -p $(dir $@)
	@echo "Gen Dummy: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) touch $@

RNN_SRC_PATH := $(MESA_TOP)/src/freedreno/registers/

# This is the list of auto-generated files headers
LOCAL_GENERATED_SOURCES += $(addprefix $(intermediates)/registers/adreno/, \
	a2xx.xml.h a3xx.xml.h a4xx.xml.h a5xx.xml.h a6xx.xml.h a6xx-pack.xml.h adreno_common.xml.h adreno_pm4.xml.h adreno-pm4-pack.xml.h)

$(intermediates)/registers/adreno/a2xx.xml.h: $(LOCAL_PATH)/registers/adreno/a2xx.xml $(MESA_TOP)/src/freedreno/registers/gen_header.py
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_PYTHON3) $(MESA_TOP)/src/freedreno/registers/gen_header.py $(RNN_SRC_PATH) $< > $@

$(intermediates)/registers/adreno/a3xx.xml.h: $(LOCAL_PATH)/registers/adreno/a3xx.xml $(MESA_TOP)/src/freedreno/registers/gen_header.py
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_PYTHON3) $(MESA_TOP)/src/freedreno/registers/gen_header.py $(RNN_SRC_PATH) $< > $@

$(intermediates)/registers/adreno/a4xx.xml.h: $(LOCAL_PATH)/registers/adreno/a4xx.xml $(MESA_TOP)/src/freedreno/registers/gen_header.py
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_PYTHON3) $(MESA_TOP)/src/freedreno/registers/gen_header.py $(RNN_SRC_PATH) $< > $@

$(intermediates)/registers/adreno/a5xx.xml.h: $(LOCAL_PATH)/registers/adreno/a5xx.xml $(MESA_TOP)/src/freedreno/registers/gen_header.py
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_PYTHON3) $(MESA_TOP)/src/freedreno/registers/gen_header.py $(RNN_SRC_PATH) $< > $@

$(intermediates)/registers/adreno/a6xx.xml.h: $(LOCAL_PATH)/registers/adreno/a6xx.xml $(MESA_TOP)/src/freedreno/registers/gen_header.py
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_PYTHON3) $(MESA_TOP)/src/freedreno/registers/gen_header.py $(RNN_SRC_PATH) $< > $@

$(intermediates)/registers/adreno/a6xx-pack.xml.h: $(LOCAL_PATH)/registers/adreno/a6xx.xml $(MESA_TOP)/src/freedreno/registers/gen_header.py
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_PYTHON3) $(MESA_TOP)/src/freedreno/registers/gen_header.py $(RNN_SRC_PATH) $< --pack-structs > $@

$(intermediates)/registers/adreno/adreno_common.xml.h: $(LOCAL_PATH)/registers/adreno/adreno_common.xml $(MESA_TOP)/src/freedreno/registers/gen_header.py
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_PYTHON3) $(MESA_TOP)/src/freedreno/registers/gen_header.py $(RNN_SRC_PATH) $< > $@

$(intermediates)/registers/adreno/adreno_pm4.xml.h: $(LOCAL_PATH)/registers/adreno/adreno_pm4.xml $(MESA_TOP)/src/freedreno/registers/gen_header.py
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_PYTHON3) $(MESA_TOP)/src/freedreno/registers/gen_header.py $(RNN_SRC_PATH) $< > $@

$(intermediates)/registers/adreno/adreno-pm4-pack.xml.h: $(LOCAL_PATH)/registers/adreno/adreno_pm4.xml $(MESA_TOP)/src/freedreno/registers/gen_header.py
	@mkdir -p $(dir $@)
	@echo "Gen Header: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) $(MESA_PYTHON3) $(MESA_TOP)/src/freedreno/registers/gen_header.py $(RNN_SRC_PATH) $< --pack-structs > $@

LOCAL_EXPORT_C_INCLUDE_DIRS := \
	$(intermediates)/registers/adreno/

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
