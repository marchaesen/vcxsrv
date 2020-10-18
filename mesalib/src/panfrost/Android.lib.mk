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

include $(CLEAR_VARS)

LOCAL_MODULE := libpanfrost_lib
LOCAL_MODULE_CLASS := STATIC_LIBRARIES
intermediates := $(call local-generated-sources-dir)

LOCAL_SRC_FILES := \
	$(lib_FILES)

LOCAL_GENERATED_SOURCES := \
	$(intermediates)/panfrost/lib/midgard_pack.h

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/src/gallium/auxiliary/ \
	$(MESA_TOP)/src/gallium/include/ \
	$(MESA_TOP)/src/panfrost/lib/ \
	$(MESA_TOP)/src/panfrost/include/ \
	$(intermediates)/panfrost/lib/

LOCAL_STATIC_LIBRARIES := \
	libmesa_nir

midgard_pack_gen := $(LOCAL_PATH)/lib/gen_pack.py
midgard_pack_deps := $(LOCAL_PATH)/lib/midgard.xml

$(intermediates)/panfrost/lib/midgard_pack.h: $(midgard_pack_deps)
	@mkdir -p $(dir $@)
	$(hide) $(MESA_PYTHON3) $(midgard_pack_gen) $< > $@

LOCAL_EXPORT_C_INCLUDE_DIRS := \
	$(MESA_TOP)/src/panfrost/lib/ \
	$(intermediates)/panfrost/lib/ \
	$(intermediates)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
