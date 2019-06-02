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

# Android.mk for libfreedreno_drm.a

# ---------------------------------------
# Build libfreedreno_drm
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	$(drm_SOURCES)

LOCAL_C_INCLUDES := \
	$(MESA_TOP)/src/gallium/include \
	$(MESA_TOP)/src/gallium/auxiliary

LOCAL_MODULE := libfreedreno_drm

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
