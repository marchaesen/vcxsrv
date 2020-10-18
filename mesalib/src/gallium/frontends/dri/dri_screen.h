/**************************************************************************
 *
 * Copyright 2009, VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Author: Keith Whitwell <keithw@vmware.com>
 * Author: Jakob Bornecrantz <wallbraker@gmail.com>
 */

#ifndef DRI_SCREEN_H
#define DRI_SCREEN_H

#include "dri_util.h"

#include "pipe/p_compiler.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "frontend/api.h"
#include "frontend/opencl_interop.h"
#include "os/os_thread.h"
#include "postprocess/filters.h"

struct dri_context;
struct dri_drawable;
struct pipe_loader_device;

struct dri_screen
{
   /* st_api */
   struct st_manager base;
   struct st_api *st_api;

   /* on old libGL's invalidate doesn't get called as it should */
   boolean broken_invalidate;

   /* dri */
   __DRIscreen *sPriv;
   boolean throttle;

   struct st_config_options options;

   /* Which postprocessing filters are enabled. */
   unsigned pp_enabled[PP_FILTERS];

   /* drm */
   int fd;
   boolean can_share_buffer;

   struct pipe_loader_device *dev;

   /* gallium */
   boolean d_depth_bits_last;
   boolean sd_depth_bits_last;
   boolean auto_fake_front;
   boolean has_reset_status_query;
   enum pipe_texture_target target;

   boolean swrast_no_present;

   /* hooks filled in by dri2 & drisw */
   __DRIimage * (*lookup_egl_image)(struct dri_screen *ctx, void *handle);

   /* OpenCL interop */
   mtx_t opencl_func_mutex;
   opencl_dri_event_add_ref_t opencl_dri_event_add_ref;
   opencl_dri_event_release_t opencl_dri_event_release;
   opencl_dri_event_wait_t opencl_dri_event_wait;
   opencl_dri_event_get_fence_t opencl_dri_event_get_fence;
};

/** cast wrapper */
static inline struct dri_screen *
dri_screen(__DRIscreen * sPriv)
{
   return (struct dri_screen *)sPriv->driverPrivate;
}

struct __DRIimageRec {
   struct pipe_resource *texture;
   unsigned level;
   unsigned layer;
   uint32_t dri_format;
   uint32_t dri_fourcc;
   uint32_t dri_components;
   unsigned use;
   unsigned plane;

   void *loader_private;

   boolean imported_dmabuf;
   /**
    * Provided by EGL_EXT_image_dma_buf_import.
    */
   enum __DRIYUVColorSpace yuv_color_space;
   enum __DRISampleRange sample_range;
   enum __DRIChromaSiting horizontal_siting;
   enum __DRIChromaSiting vertical_siting;

};

static inline boolean
dri_with_format(__DRIscreen * sPriv)
{
   const __DRIdri2LoaderExtension *loader = sPriv->dri2.loader;

   return loader
       && (loader->base.version >= 3)
       && (loader->getBuffersWithFormat != NULL);
}

void
dri_fill_st_visual(struct st_visual *stvis,
                   const struct dri_screen *screen,
                   const struct gl_config *mode);

void
dri_init_options(struct dri_screen *screen);

const __DRIconfig **
dri_init_screen_helper(struct dri_screen *screen,
                       struct pipe_screen *pscreen);

void
dri_destroy_screen_helper(struct dri_screen * screen);

void
dri_destroy_screen(__DRIscreen * sPriv);

extern const struct __DriverAPIRec dri_kms_driver_api;

extern const struct __DriverAPIRec galliumdrm_driver_api;
extern const __DRIextension *galliumdrm_driver_extensions[];
extern const struct __DriverAPIRec galliumsw_driver_api;
extern const __DRIextension *galliumsw_driver_extensions[];
extern const __DRIconfigOptionsExtension gallium_config_options;

#endif

/* vim: set sw=3 ts=8 sts=3 expandtab: */
