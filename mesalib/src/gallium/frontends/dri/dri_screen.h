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

#include "util/compiler.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "frontend/api.h"
#include "frontend/opencl_interop.h"
#include "util/u_thread.h"
#include "postprocess/filters.h"
#include "kopper_interface.h"

struct dri_context;
struct dri_drawable;
struct pipe_loader_device;

struct dri_screen
{
   /* st_api */
   struct pipe_frontend_screen base;

   /* dri */
   /* Current screen's number */
   int myNum;

   void *loaderPrivate;

   int max_gl_core_version;
   int max_gl_compat_version;
   int max_gl_es1_version;
   int max_gl_es2_version;

   enum dri_screen_type type;

   const __DRIswrastLoaderExtension *swrast_loader;
   const __DRIkopperLoaderExtension *kopper_loader;

   struct {
       /* Flag to indicate that this is a DRI2 screen.  Many of the above
        * fields will not be valid or initializaed in that case. */
       const __DRIdri2LoaderExtension *loader;
       const __DRIimageLookupExtension *image;
       const __DRIuseInvalidateExtension *useInvalidate;
       const __DRIbackgroundCallableExtension *backgroundCallable;
   } dri2;

   struct {
       const __DRIimageLoaderExtension *loader;
   } image;

   struct {
      const __DRImutableRenderBufferLoaderExtension *loader;
   } mutableRenderBuffer;

   driOptionCache optionInfo;
   driOptionCache optionCache;

   unsigned int api_mask;

   bool throttle;
   bool dmabuf_import;

   struct st_config_options options;

   /* Which postprocessing filters are enabled. */
   unsigned pp_enabled[PP_FILTERS];

   /* drm */
   int fd;
   bool can_share_buffer;

   struct pipe_loader_device *dev;

   /* gallium */
   bool auto_fake_front;
   bool has_reset_status_query;
   bool has_protected_context;
   enum pipe_texture_target target;

   bool swrast_no_present;

   /* DRI exts that vary based on gallium pipe_screen caps. */
   __DRIimageExtension image_extension;
   __DRI2bufferDamageExtension buffer_damage_extension;

   /* DRI exts on this screen. Populated at init time based on device caps. */
   const __DRIextension *screen_extensions[14];

   /* OpenCL interop */
   mtx_t opencl_func_mutex;
   opencl_dri_event_add_ref_t opencl_dri_event_add_ref;
   opencl_dri_event_release_t opencl_dri_event_release;
   opencl_dri_event_wait_t opencl_dri_event_wait;
   opencl_dri_event_get_fence_t opencl_dri_event_get_fence;

   /* kopper */
   bool has_dmabuf;
   bool is_sw;
};

static inline const __DRIkopperLoaderExtension *
dri_screen_get_kopper(struct dri_screen *screen)
{
   return screen->kopper_loader;
}

struct dri_image {
   struct pipe_resource *texture;
   unsigned level;
   unsigned layer;
   uint32_t dri_format;
   uint32_t dri_fourcc;
   uint32_t dri_components;
   /* Provided by eglCreateImageKHR if creating from a
    * texture or a renderbuffer. 0 otherwise.
    */
   uint32_t internal_format;
   unsigned use;
   unsigned plane;

   int in_fence_fd;

   void *loader_private;

   bool imported_dmabuf;
   /**
    * Provided by EGL_EXT_image_dma_buf_import.
    */
   enum __DRIYUVColorSpace yuv_color_space;
   enum __DRISampleRange sample_range;
   enum __DRIChromaSiting horizontal_siting;
   enum __DRIChromaSiting vertical_siting;

   struct dri_screen *screen;
};

static inline bool
dri_with_format(struct dri_screen *screen)
{
   const __DRIdri2LoaderExtension *loader = screen->dri2.loader;

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

const struct dri_config **
dri_init_screen(struct dri_screen *screen,
                struct pipe_screen *pscreen,
                bool has_multibuffer);

void
dri_release_screen(struct dri_screen * screen);

void
dri_destroy_screen(struct dri_screen *screen);

struct pipe_screen *
dri2_init_screen(struct dri_screen *screen, bool driver_name_is_inferred);
struct pipe_screen *
dri_swrast_kms_init_screen(struct dri_screen *screen, bool driver_name_is_inferred);
struct pipe_screen *
kopper_init_screen(struct dri_screen *screen, bool driver_name_is_inferred);
struct pipe_screen *
drisw_init_screen(struct dri_screen *screen, bool driver_name_is_inferred);

extern const struct __DriverAPIRec dri_swrast_kms_driver_api;
extern const __DRIextension *dri_swrast_kms_driver_extensions[];
extern const struct __DriverAPIRec galliumdrm_driver_api;
extern const __DRIextension *galliumdrm_driver_extensions[];
extern const struct __DriverAPIRec galliumsw_driver_api;
extern const __DRIextension *galliumsw_driver_extensions[];
extern const struct __DriverAPIRec galliumvk_driver_api;
extern const __DRIextension *galliumvk_driver_extensions[];
extern const __DRIconfigOptionsExtension gallium_config_options;

#endif

/* vim: set sw=3 ts=8 sts=3 expandtab: */
