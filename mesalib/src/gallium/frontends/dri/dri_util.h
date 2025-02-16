/*
 * Copyright 1998-1999 Precision Insight, Inc., Cedar Park, Texas.
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
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * \file dri_util.h
 * DRI utility functions definitions.
 *
 * \author Kevin E. Martin <kevin@precisioninsight.com>
 * \author Brian Paul <brian@precisioninsight.com>
 */

#ifndef _DRI_UTIL_H_
#define _DRI_UTIL_H_

#include <GL/gl.h>
#include "mesa_interface.h"
#include "kopper_interface.h"
#include "main/formats.h"
#include "main/glconfig.h"
#include "main/menums.h"
#include "util/xmlconfig.h"
#include "pipe/p_defines.h"
#include <stdbool.h>

struct pipe_screen;
struct dri_screen;
struct mesa_glinterop_device_info;
struct mesa_glinterop_export_in;
struct mesa_glinterop_export_out;
struct mesa_glinterop_flush_out;

#define __DRI_BACKEND_VTABLE "DRI_DriverVtable"

struct dri_config {
    struct gl_config modes;
};

enum dri_screen_type {
   DRI_SCREEN_DRI3,
   DRI_SCREEN_KOPPER,
   DRI_SCREEN_SWRAST,
   DRI_SCREEN_KMS_SWRAST,
};

/**
 * Description of the attributes used to create a config.
 *
 * This is passed as the context_config parameter to CreateContext. The idea
 * with this struct is that it can be extended without having to modify all of
 * the drivers. The first three members (major/minor_version and flags) are
 * always valid, but the remaining members are only valid if the corresponding
 * flag is set for the attribute. If the flag is not set then the default
 * value should be assumed. That way the driver can quickly check if any
 * attributes were set that it doesn't understand and report an error.
 */
struct __DriverContextConfig {
    /* These members are always valid */
    unsigned major_version;
    unsigned minor_version;
    uint32_t flags;

    /* Flags describing which of the remaining members are valid */
    uint32_t attribute_mask;

    /* Only valid if __DRIVER_CONTEXT_ATTRIB_RESET_STRATEGY is set */
    int reset_strategy;

    /* Only valid if __DRIVER_CONTEXT_PRIORITY is set */
    unsigned priority;

    /* Only valid if __DRIVER_CONTEXT_ATTRIB_RELEASE_BEHAVIOR is set */
    int release_behavior;

    /* Only valid if __DRIVER_CONTEXT_ATTRIB_NO_ERROR is set */
    int no_error;

    /* Only valid if __DRIVER_CONTEXT_ATTRIB_PROTECTED is set */
    int protected_context;
};

#define __DRIVER_CONTEXT_ATTRIB_RESET_STRATEGY   (1 << 0)
#define __DRIVER_CONTEXT_ATTRIB_PRIORITY         (1 << 1)
#define __DRIVER_CONTEXT_ATTRIB_RELEASE_BEHAVIOR (1 << 2)
#define __DRIVER_CONTEXT_ATTRIB_NO_ERROR         (1 << 3)
#define __DRIVER_CONTEXT_ATTRIB_PROTECTED        (1 << 4)

PUBLIC struct dri_screen *
driCreateNewScreen3(int scrn, int fd,
                    const __DRIextension **loader_extensions,
                    enum dri_screen_type type,
                    const struct dri_config ***driver_configs, bool driver_name_is_inferred,
                    bool has_multibuffer, void *data);
PUBLIC struct dri_context *
driCreateContextAttribs(struct dri_screen *psp, int api,
                        const struct dri_config *config,
                        struct dri_context *shared,
                        unsigned num_attribs,
                        const uint32_t *attribs,
                        unsigned *error,
                        void *data);

extern uint32_t
driImageFormatToSizedInternalGLFormat(uint32_t image_format);
PUBLIC unsigned int
driGetAPIMask(struct dri_screen *screen);
PUBLIC struct dri_drawable *
dri_create_drawable(struct dri_screen *psp, const struct dri_config *config,
                    bool isPixmap, void *loaderPrivate);
extern const __DRIimageDriverExtension driImageDriverExtension;
PUBLIC void driDestroyScreen(struct dri_screen *psp);
PUBLIC int
driGetConfigAttrib(const struct dri_config *config, unsigned int attrib, unsigned int *value);
PUBLIC int
driIndexConfigAttrib(const struct dri_config *config, int index, unsigned int *attrib, unsigned int *value);
PUBLIC void
driDestroyDrawable(struct dri_drawable *drawable);
PUBLIC void
driSwapBuffers(struct dri_drawable *drawable);
PUBLIC void
driSwapBuffersWithDamage(struct dri_drawable *drawable, int nrects, const int *rects);
PUBLIC struct dri_context *
driCreateNewContext(struct dri_screen *screen, const struct dri_config *config, struct dri_context *shared, void *data);
PUBLIC int
driCopyContext(struct dri_context *dest, struct dri_context *src, unsigned long mask);
PUBLIC void
driDestroyContext(struct dri_context *ctx);
PUBLIC int driBindContext(struct dri_context *ctx, struct dri_drawable *draw, struct dri_drawable *read);
PUBLIC int driUnbindContext(struct dri_context *ctx);


PUBLIC int64_t
kopperSwapBuffers(struct dri_drawable *dPriv, uint32_t flush_flags);
PUBLIC int64_t
kopperSwapBuffersWithDamage(struct dri_drawable *drawable, uint32_t flush_flags, int nrects, const int *rects);
PUBLIC struct dri_drawable *
kopperCreateNewDrawable(struct dri_screen *psp,
                        const struct dri_config *config,
                        void *data,
                        __DRIkopperDrawableInfo *info);
PUBLIC void
kopperSetSwapInterval(struct dri_drawable *drawable, int interval);
PUBLIC int
kopperQueryBufferAge(struct dri_drawable *drawable);

PUBLIC void
driswCopySubBuffer(struct dri_drawable *drawable, int x, int y, int w, int h);

PUBLIC int
kopperGetSyncValues(struct dri_drawable *drawable, int64_t target_msc, int64_t divisor,
                    int64_t remainder, int64_t *ust, int64_t *msc, int64_t *sbc);

PUBLIC void
dri_set_tex_buffer2(struct dri_context *ctx, GLint target,
                    GLint format, struct dri_drawable *drawable);

PUBLIC int
dri_query_renderer_string(struct dri_screen *_screen, int param,
                           const char **value);
PUBLIC int
dri_query_renderer_integer(struct dri_screen *_screen, int param,
                            unsigned int *value);

PUBLIC void
dri_flush_drawable(struct dri_drawable *dPriv);
PUBLIC void
dri_flush(struct dri_context *cPriv,
          struct dri_drawable *dPriv,
          unsigned flags,
          enum __DRI2throttleReason reason);
PUBLIC void
dri_invalidate_drawable(struct dri_drawable *drawable);

PUBLIC int
dri2GalliumConfigQueryb(struct dri_screen *sPriv, const char *var,
                        unsigned char *val);
PUBLIC int
dri2GalliumConfigQueryi(struct dri_screen *sPriv, const char *var, int *val);
PUBLIC int
dri2GalliumConfigQueryf(struct dri_screen *sPriv, const char *var, float *val);
PUBLIC int
dri2GalliumConfigQuerys(struct dri_screen *sPriv, const char *var, char **val);

PUBLIC int dri_get_initial_swap_interval(struct dri_screen *driScreen);
PUBLIC bool dri_valid_swap_interval(struct dri_screen *driScreen, int interval);

PUBLIC void
dri_throttle(struct dri_context *cPriv, struct dri_drawable *dPriv,
             enum __DRI2throttleReason reason);

PUBLIC int
dri_interop_query_device_info(struct dri_context *ctx,
                              struct mesa_glinterop_device_info *out);
PUBLIC int
dri_interop_export_object(struct dri_context *ctx,
                          struct mesa_glinterop_export_in *in,
                          struct mesa_glinterop_export_out *out);
PUBLIC int
dri_interop_flush_objects(struct dri_context *_ctx,
                           unsigned count, struct mesa_glinterop_export_in *objects,
                           struct mesa_glinterop_flush_out *out);

PUBLIC struct dri_image *
dri_create_image_from_renderbuffer(struct dri_context *dri_ctx,
                                   int renderbuffer, void *loaderPrivate,
                                   unsigned *error);

PUBLIC void
dri2_destroy_image(struct dri_image *img);

PUBLIC struct dri_image *
dri2_create_from_texture(struct dri_context *dri_ctx, int target, unsigned texture,
                         int depth, int level, unsigned *error,
                         void *loaderPrivate);

PUBLIC struct dri_image *
dri_create_image(struct dri_screen *screen,
                 int width, int height,
                 int format,
                 const uint64_t *modifiers,
                 const unsigned _count,
                 unsigned int use,
                 void *loaderPrivate);
PUBLIC GLboolean
dri2_query_image(struct dri_image *image, int attrib, int *value);
PUBLIC struct dri_image *
dri2_dup_image(struct dri_image *image, void *loaderPrivate);
PUBLIC GLboolean
dri2_validate_usage(struct dri_image *image, unsigned int use);
PUBLIC struct dri_image *
dri2_from_names(struct dri_screen *screen, int width, int height, int fourcc,
                int *names, int num_names, int *strides, int *offsets,
                void *loaderPrivate);
PUBLIC struct dri_image *
dri2_from_planar(struct dri_image *image, int plane, void *loaderPrivate);
PUBLIC struct dri_image *
dri2_from_dma_bufs(struct dri_screen *screen,
                    int width, int height, int fourcc,
                    uint64_t modifier, int *fds, int num_fds,
                    int *strides, int *offsets,
                    enum __DRIYUVColorSpace yuv_color_space,
                    enum __DRISampleRange sample_range,
                    enum __DRIChromaSiting horizontal_siting,
                    enum __DRIChromaSiting vertical_siting,
                    uint32_t dri_flags,
                    unsigned *error,
                    void *loaderPrivate);
PUBLIC void
dri2_blit_image(struct dri_context *ctx, struct dri_image *dst, struct dri_image *src,
                int dstx0, int dsty0, int dstwidth, int dstheight,
                int srcx0, int srcy0, int srcwidth, int srcheight,
                int flush_flag);
PUBLIC int
dri2_get_capabilities(struct dri_screen *_screen);
PUBLIC void *
dri2_map_image(struct dri_context *ctx, struct dri_image *image,
               int x0, int y0, int width, int height,
               unsigned int flags, int *stride, void **data);
PUBLIC void
dri2_unmap_image(struct dri_context *ctx, struct dri_image *image, void *data);
PUBLIC bool
dri_query_dma_buf_formats(struct dri_screen *_screen, int max, int *formats,
                           int *count);
PUBLIC bool
dri_query_dma_buf_modifiers(struct dri_screen *_screen, int fourcc, int max,
                             uint64_t *modifiers, unsigned int *external_only,
                             int *count);
PUBLIC bool
dri2_query_dma_buf_format_modifier_attribs(struct dri_screen *_screen,
                                           uint32_t fourcc, uint64_t modifier,
                                           int attrib, uint64_t *value);
PUBLIC struct dri_image *
dri_create_image_with_modifiers(struct dri_screen *screen,
                                 uint32_t width, uint32_t height,
                                 uint32_t dri_format, uint32_t dri_usage,
                                 const uint64_t *modifiers,
                                 unsigned int modifiers_count,
                                 void *loaderPrivate);
PUBLIC int
dri_query_compatible_render_only_device_fd(int kms_only_fd);

PUBLIC int
driSWRastQueryBufferAge(struct dri_drawable *drawable);

PUBLIC void
dri2_set_in_fence_fd(struct dri_image *img, int fd);

PUBLIC bool
dri2_query_compression_rates(struct dri_screen *_screen, const struct dri_config *config, int max,
                             enum __DRIFixedRateCompression *rates, int *count);
PUBLIC bool
dri2_query_compression_modifiers(struct dri_screen *_screen, uint32_t fourcc,
                                 enum __DRIFixedRateCompression rate, int max,
                                 uint64_t *modifiers, int *count);

PUBLIC void
dri_set_damage_region(struct dri_drawable *drawable, unsigned int nrects, int *rects);

PUBLIC unsigned
dri_fence_get_caps(struct dri_screen *screen);
PUBLIC void *
dri_create_fence(struct dri_context *ctx);
PUBLIC void *
dri_create_fence_fd(struct dri_context *_ctx, int fd);
PUBLIC int
dri_get_fence_fd(struct dri_screen *driscreen, void *_fence);
PUBLIC void *
dri_get_fence_from_cl_event(struct dri_screen *driscreen, intptr_t cl_event);
PUBLIC void
dri_destroy_fence(struct dri_screen *driscreen, void *_fence);
PUBLIC GLboolean
dri_client_wait_sync(struct dri_context *_ctx, void *_fence, unsigned flags,
                      uint64_t timeout);
PUBLIC void
dri_server_wait_sync(struct dri_context *ctx, void *_fence, unsigned flags);

PUBLIC void
dri_set_blob_cache_funcs(struct dri_screen *screen, __DRIblobCacheSet set,
                         __DRIblobCacheGet get);

PUBLIC struct pipe_screen *
dri_get_pipe_screen(struct dri_screen *driScreen);
#endif /* _DRI_UTIL_H_ */
