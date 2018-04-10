# Mesa 3-D graphics library
#
# Copyright (C) 2010-2011 Chia-I Wu <olvaffe@gmail.com>
# Copyright (C) 2010-2011 LunarG Inc.
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

# src/gallium/Android.mk

GALLIUM_TOP := $(call my-dir)
GALLIUM_COMMON_MK := $(GALLIUM_TOP)/Android.common.mk
GALLIUM_TARGET_DRIVERS :=

SUBDIRS := auxiliary
SUBDIRS += auxiliary/pipe-loader

#
# Gallium drivers and their respective winsys
#

SUBDIRS += winsys/sw/dri drivers/softpipe
SUBDIRS += winsys/freedreno/drm drivers/freedreno
SUBDIRS += winsys/i915/drm drivers/i915
SUBDIRS += winsys/nouveau/drm drivers/nouveau
SUBDIRS += winsys/pl111/drm drivers/pl111
SUBDIRS += winsys/radeon/drm drivers/r300
SUBDIRS += winsys/radeon/drm drivers/r600
SUBDIRS += winsys/radeon/drm winsys/amdgpu/drm drivers/radeonsi
SUBDIRS += winsys/vc4/drm drivers/vc4
SUBDIRS += winsys/virgl/drm winsys/virgl/vtest drivers/virgl
SUBDIRS += winsys/svga/drm drivers/svga
SUBDIRS += winsys/etnaviv/drm drivers/etnaviv drivers/renderonly
SUBDIRS += winsys/imx/drm
SUBDIRS += state_trackers/dri

# sort to eliminate any duplicates
INC_DIRS := $(call all-named-subdir-makefiles,$(sort $(SUBDIRS)))
# targets/dri must be included last
INC_DIRS += $(call all-named-subdir-makefiles,targets/dri)

include $(INC_DIRS)
