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
	$(C_SOURCES)

LOCAL_MODULE := libmesa_pipe_kmsro

include $(GALLIUM_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)

ifneq ($(HAVE_GALLIUM_KMSRO),)
GALLIUM_TARGET_DRIVERS += armada-drm
GALLIUM_TARGET_DRIVERS += exynos
GALLIUM_TARGET_DRIVERS += hx8357d
GALLIUM_TARGET_DRIVERS += ili9225
GALLIUM_TARGET_DRIVERS += ili9341
GALLIUM_TARGET_DRIVERS += imx-drm
GALLIUM_TARGET_DRIVERS += imx-dcss
GALLIUM_TARGET_DRIVERS += ingenic-drm
GALLIUM_TARGET_DRIVERS += mcde
GALLIUM_TARGET_DRIVERS += mediatek
GALLIUM_TARGET_DRIVERS += meson
GALLIUM_TARGET_DRIVERS += mi0283qt
GALLIUM_TARGET_DRIVERS += mxsfb-drm
GALLIUM_TARGET_DRIVERS += pl111
GALLIUM_TARGET_DRIVERS += repaper
GALLIUM_TARGET_DRIVERS += rockchip
GALLIUM_TARGET_DRIVERS += st7586
GALLIUM_TARGET_DRIVERS += st7735r
GALLIUM_TARGET_DRIVERS += stm
GALLIUM_TARGET_DRIVERS += sun4i-drm
$(eval GALLIUM_LIBS += $(LOCAL_MODULE) libmesa_winsys_kmsro)
endif
