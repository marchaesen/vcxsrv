# Copyright (C) 2014 Emil Velikov <emil.l.velikov@gmail.com>
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

LOCAL_PATH := $(call my-dir)

# get C_SOURCES
include $(LOCAL_PATH)/Makefile.sources

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	$(C_SOURCES) \
	$(a2xx_SOURCES) \
	$(a3xx_SOURCES)	\
	$(a4xx_SOURCES) \
	$(a5xx_SOURCES) \
	$(a6xx_SOURCES) \
	$(ir3_SOURCES)

#LOCAL_CFLAGS := \
#	-Wno-packed-bitfield-compat

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/ir3 \
	$(MESA_TOP)/include \
	$(MESA_TOP)/src/freedreno/common \
	$(call generated-sources-dir-for,STATIC_LIBRARIES,libmesa_gallium,,)/util

LOCAL_GENERATED_SOURCES := $(MESA_GEN_NIR_H)

LOCAL_SHARED_LIBRARIES := libdrm libsync
LOCAL_STATIC_LIBRARIES := libmesa_glsl libmesa_nir libfreedreno_drm libfreedreno_ir3 libfreedreno_perfcntrs libfreedreno_registers
LOCAL_MODULE := libmesa_pipe_freedreno

LOCAL_MODULE_CLASS := STATIC_LIBRARIES

intermediates := $(call local-generated-sources-dir)

LOCAL_GENERATED_SOURCES += $(addprefix $(intermediates)/, $(GENERATED_SOURCES))

freedreno_tracepoints_deps := \
	$(MESA_TOP)/src/gallium/drivers/freedreno/freedreno_tracepoints.py \
	$(MESA_TOP)/src/gallium/auxiliary/util/u_trace.py

freedreno_tracepoints_c := $(intermediates)/freedreno_tracepoints.c
freedreno_tracepoints_h := $(intermediates)/freedreno_tracepoints.h

$(intermediates)/freedreno_tracepoints.c \
$(intermediates)/freedreno_tracepoints.h: $(freedreno_tracepoints_deps)
	@mkdir -p $(dir $@)
	$(hide) $(MESA_PYTHON3) $< -p $(MESA_TOP)/src/gallium/auxiliary/util -C $(freedreno_tracepoints_c) -H $(freedreno_tracepoints_h)

include $(GALLIUM_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

ifneq ($(HAVE_GALLIUM_FREEDRENO),)
GALLIUM_TARGET_DRIVERS += msm
$(eval GALLIUM_LIBS += $(LOCAL_MODULE) libmesa_winsys_freedreno)
$(eval GALLIUM_SHARED_LIBS += $(LOCAL_SHARED_LIBRARIES))
endif
