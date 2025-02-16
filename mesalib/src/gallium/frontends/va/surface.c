/**************************************************************************
 *
 * Copyright 2010 Thomas Balling Sørensen & Orasanu Lucian.
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "pipe/p_screen.h"
#include "pipe/p_video_codec.h"

#include "util/u_memory.h"
#include "util/u_handle_table.h"
#include "util/u_rect.h"
#include "util/u_sampler.h"
#include "util/u_surface.h"
#include "util/u_video.h"
#include "util/set.h"

#include "vl/vl_compositor.h"
#include "vl/vl_video_buffer.h"
#include "vl/vl_winsys.h"

#include "va_private.h"

#ifdef _WIN32
#include "frontend/winsys_handle.h"
#include <va/va_win32.h>
#else
#include "frontend/drm_driver.h"
#include <va/va_drmcommon.h>
#include "drm-uapi/drm_fourcc.h"
#endif

#ifndef VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_3
#define VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_3      0x08000000
#endif

VAStatus
vlVaCreateSurfaces(VADriverContextP ctx, int width, int height, int format,
                   int num_surfaces, VASurfaceID *surfaces)
{
   return vlVaCreateSurfaces2(ctx, format, width, height, surfaces, num_surfaces,
                              NULL, 0);
}

static void
vlVaRemoveDpbSurface(vlVaSurface *surf, VASurfaceID id)
{
   assert(surf->ctx->templat.entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE);

   switch (u_reduce_video_profile(surf->ctx->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      for (unsigned i = 0; i < surf->ctx->desc.h264enc.dpb_size; i++) {
         if (surf->ctx->desc.h264enc.dpb[i].id == id) {
            memset(&surf->ctx->desc.h264enc.dpb[i], 0, sizeof(surf->ctx->desc.h264enc.dpb[i]));
            break;
         }
      }
      break;
   case PIPE_VIDEO_FORMAT_HEVC:
      for (unsigned i = 0; i < surf->ctx->desc.h265enc.dpb_size; i++) {
         if (surf->ctx->desc.h265enc.dpb[i].id == id) {
            memset(&surf->ctx->desc.h265enc.dpb[i], 0, sizeof(surf->ctx->desc.h265enc.dpb[i]));
            break;
         }
      }
      break;
   case PIPE_VIDEO_FORMAT_AV1:
      for (unsigned i = 0; i < surf->ctx->desc.av1enc.dpb_size; i++) {
         if (surf->ctx->desc.av1enc.dpb[i].id == id) {
            memset(&surf->ctx->desc.av1enc.dpb[i], 0, sizeof(surf->ctx->desc.av1enc.dpb[i]));
            break;
         }
      }
      break;
   default:
      assert(false);
      break;
   }
}

VAStatus
vlVaDestroySurfaces(VADriverContextP ctx, VASurfaceID *surface_list, int num_surfaces)
{
   vlVaDriver *drv;
   int i;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);
   for (i = 0; i < num_surfaces; ++i) {
      vlVaSurface *surf = handle_table_get(drv->htab, surface_list[i]);
      if (!surf) {
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_INVALID_SURFACE;
      }
      if (surf->buffer)
         surf->buffer->destroy(surf->buffer);
      if (surf->pipe_fence)
         drv->pipe->screen->fence_reference(drv->pipe->screen, &surf->pipe_fence, NULL);
      if (surf->ctx) {
         assert(_mesa_set_search(surf->ctx->surfaces, surf));
         _mesa_set_remove_key(surf->ctx->surfaces, surf);
         if (surf->fence && surf->ctx->decoder && surf->ctx->decoder->destroy_fence)
            surf->ctx->decoder->destroy_fence(surf->ctx->decoder, surf->fence);
         if (surf->is_dpb)
            vlVaRemoveDpbSurface(surf, surface_list[i]);
      }
      if (drv->last_efc_surface) {
         vlVaSurface *efc_surf = drv->last_efc_surface;
         if (efc_surf == surf || efc_surf->efc_surface == surf) {
            efc_surf->efc_surface = NULL;
            drv->last_efc_surface = NULL;
            drv->efc_count = -1;
         }
      }
      if (surf->coded_buf)
         surf->coded_buf->coded_surf = NULL;
      util_dynarray_fini(&surf->subpics);
      FREE(surf);
      handle_table_remove(drv->htab, surface_list[i]);
   }
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}

static VAStatus
_vlVaSyncSurface(VADriverContextP ctx, VASurfaceID render_target, uint64_t timeout_ns)
{
   vlVaDriver *drv;
   vlVaContext *context;
   vlVaSurface *surf;
   struct pipe_fence_handle *fence;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   mtx_lock(&drv->mutex);
   surf = handle_table_get(drv->htab, render_target);
   if (!surf) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   if (surf->coded_buf) {
      context = surf->coded_buf->ctx;
      fence = surf->coded_buf->fence;
   } else {
      context = surf->ctx;
      fence = surf->fence;
   }

   if (surf->pipe_fence) {
      struct pipe_screen *pscreen = drv->pipe->screen;
      if (!pscreen->fence_finish(pscreen, NULL, surf->pipe_fence, timeout_ns)) {
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_TIMEDOUT;
      }
      pscreen->fence_reference(pscreen, &surf->pipe_fence, NULL);
   }

   /* No outstanding operation: nothing to do. */
   if (!surf->fence) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_SUCCESS;
   }

   if (!context || !context->decoder) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_CONTEXT;
   }

   mtx_lock(&context->mutex);
   mtx_unlock(&drv->mutex);
   int ret = context->decoder->fence_wait(context->decoder, fence, timeout_ns);
   mtx_unlock(&context->mutex);
   return ret ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_TIMEDOUT;
}

VAStatus
vlVaSyncSurface(VADriverContextP ctx, VASurfaceID render_target)
{
   return _vlVaSyncSurface(ctx, render_target, VA_TIMEOUT_INFINITE);
}

#if VA_CHECK_VERSION(1, 15, 0)
VAStatus
vlVaSyncSurface2(VADriverContextP ctx, VASurfaceID surface, uint64_t timeout_ns)
{
   return _vlVaSyncSurface(ctx, surface, timeout_ns);
}
#endif

VAStatus
vlVaQuerySurfaceStatus(VADriverContextP ctx, VASurfaceID render_target, VASurfaceStatus *status)
{
   VAStatus ret = _vlVaSyncSurface(ctx, render_target, 0);

   if (ret == VA_STATUS_SUCCESS)
      *status = VASurfaceReady;
   else if (ret == VA_STATUS_ERROR_TIMEDOUT)
      *status = VASurfaceRendering;
   else
      return ret;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaQuerySurfaceError(VADriverContextP ctx, VASurfaceID render_target, VAStatus error_status, void **error_info)
{
   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static void
upload_sampler(struct pipe_context *pipe, struct pipe_sampler_view *dst,
               const struct pipe_box *dst_box, const void *src, unsigned src_stride,
               unsigned src_x, unsigned src_y)
{
   struct pipe_transfer *transfer;
   void *map;

   map = pipe->texture_map(pipe, dst->texture, 0, PIPE_MAP_WRITE,
                            dst_box, &transfer);
   if (!map)
      return;

   util_copy_rect(map, dst->texture->format, transfer->stride, 0, 0,
                  dst_box->width, dst_box->height,
                  src, src_stride, src_x, src_y);

   pipe->texture_unmap(pipe, transfer);
}

static VAStatus
vlVaPutSubpictures(vlVaSurface *surf, vlVaDriver *drv,
                   struct pipe_surface *surf_draw, struct u_rect *dirty_area,
                   struct u_rect *src_rect, struct u_rect *dst_rect)
{
   vlVaSubpicture *sub;
   int i;

   for (i = 0; i < surf->subpics.size/sizeof(vlVaSubpicture *); i++) {
      struct pipe_blend_state blend;
      void *blend_state = NULL;
      vlVaBuffer *buf;
      struct pipe_box box;
      struct u_rect *s, *d, sr, dr, c;
      int sw, sh, dw, dh;

      sub = ((vlVaSubpicture **)surf->subpics.data)[i];
      if (!sub)
         continue;

      buf = handle_table_get(drv->htab, sub->image->buf);
      if (!buf)
         return VA_STATUS_ERROR_INVALID_IMAGE;

      box.x = 0;
      box.y = 0;
      box.z = 0;
      box.width = sub->dst_rect.x1 - sub->dst_rect.x0;
      box.height = sub->dst_rect.y1 - sub->dst_rect.y0;
      box.depth = 1;

      s = &sub->src_rect;
      d = &sub->dst_rect;
      sw = s->x1 - s->x0;
      sh = s->y1 - s->y0;
      dw = d->x1 - d->x0;
      dh = d->y1 - d->y0;
      c.x0 = MAX2(d->x0, s->x0);
      c.y0 = MAX2(d->y0, s->y0);
      c.x1 = MIN2(d->x0 + dw, src_rect->x1);
      c.y1 = MIN2(d->y0 + dh, src_rect->y1);
      sr.x0 = s->x0 + (c.x0 - d->x0)*(sw/(float)dw);
      sr.y0 = s->y0 + (c.y0 - d->y0)*(sh/(float)dh);
      sr.x1 = s->x0 + (c.x1 - d->x0)*(sw/(float)dw);
      sr.y1 = s->y0 + (c.y1 - d->y0)*(sh/(float)dh);

      s = src_rect;
      d = dst_rect;
      sw = s->x1 - s->x0;
      sh = s->y1 - s->y0;
      dw = d->x1 - d->x0;
      dh = d->y1 - d->y0;
      dr.x0 = d->x0 + c.x0*(dw/(float)sw);
      dr.y0 = d->y0 + c.y0*(dh/(float)sh);
      dr.x1 = d->x0 + c.x1*(dw/(float)sw);
      dr.y1 = d->y0 + c.y1*(dh/(float)sh);

      vl_compositor_clear_layers(&drv->cstate);
      if (drv->pipe->create_blend_state) {
         memset(&blend, 0, sizeof(blend));
         blend.independent_blend_enable = 0;
         blend.rt[0].blend_enable = 1;
         blend.rt[0].rgb_src_factor = PIPE_BLENDFACTOR_SRC_ALPHA;
         blend.rt[0].rgb_dst_factor = PIPE_BLENDFACTOR_INV_SRC_ALPHA;
         blend.rt[0].alpha_src_factor = PIPE_BLENDFACTOR_ZERO;
         blend.rt[0].alpha_dst_factor = PIPE_BLENDFACTOR_ZERO;
         blend.rt[0].rgb_func = PIPE_BLEND_ADD;
         blend.rt[0].alpha_func = PIPE_BLEND_ADD;
         blend.rt[0].colormask = PIPE_MASK_RGBA;
         blend.logicop_enable = 0;
         blend.logicop_func = PIPE_LOGICOP_CLEAR;
         blend.dither = 0;
         blend_state = drv->pipe->create_blend_state(drv->pipe, &blend);
         vl_compositor_set_layer_blend(&drv->cstate, 0, blend_state, false);
      }
      upload_sampler(drv->pipe, sub->sampler, &box, buf->data,
                     sub->image->pitches[0], 0, 0);
      vl_compositor_set_rgba_layer(&drv->cstate, &drv->compositor, 0, sub->sampler,
                                   &sr, NULL, NULL);
      vl_compositor_set_layer_dst_area(&drv->cstate, 0, &dr);
      vl_compositor_render(&drv->cstate, &drv->compositor, surf_draw, dirty_area, false);
      if (blend_state)
         drv->pipe->delete_blend_state(drv->pipe, blend_state);
   }

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaPutSurface(VADriverContextP ctx, VASurfaceID surface_id, void* draw, short srcx, short srcy,
               unsigned short srcw, unsigned short srch, short destx, short desty,
               unsigned short destw, unsigned short desth, VARectangle *cliprects,
               unsigned int number_cliprects,  unsigned int flags)
{
   vlVaDriver *drv;
   vlVaSurface *surf;
   struct pipe_screen *screen;
   struct pipe_resource *tex;
   struct pipe_surface surf_templ, *surf_draw;
   struct vl_screen *vscreen;
   struct u_rect src_rect, *dirty_area;
   struct u_rect dst_rect = {destx, destx + destw, desty, desty + desth};
   enum pipe_format format;
   VAStatus status;
   enum VL_CSC_COLOR_STANDARD color_standard;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);
   surf = handle_table_get(drv->htab, surface_id);
   vlVaGetSurfaceBuffer(drv, surf);
   if (!surf || !surf->buffer) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   screen = drv->pipe->screen;
   vscreen = drv->vscreen;

   tex = vscreen->texture_from_drawable(vscreen, draw);
   if (!tex) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_DISPLAY;
   }

   dirty_area = vscreen->get_dirty_area(vscreen);

   memset(&surf_templ, 0, sizeof(surf_templ));
   surf_templ.format = tex->format;
   surf_draw = drv->pipe->create_surface(drv->pipe, tex, &surf_templ);
   if (!surf_draw) {
      pipe_resource_reference(&tex, NULL);
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_DISPLAY;
   }

   src_rect.x0 = srcx;
   src_rect.y0 = srcy;
   src_rect.x1 = srcw + srcx;
   src_rect.y1 = srch + srcy;

   format = surf->buffer->buffer_format;

   if (flags & VA_SRC_BT601)
      color_standard = VL_CSC_COLOR_STANDARD_BT_601;
   else if (flags & VA_SRC_SMPTE_240)
      color_standard = VL_CSC_COLOR_STANDARD_SMPTE_240M;
   else
      color_standard = VL_CSC_COLOR_STANDARD_BT_709;

   vl_csc_get_matrix(color_standard, NULL, true, &drv->csc);
   vl_compositor_set_csc_matrix(&drv->cstate, (const vl_csc_matrix *)&drv->csc, 1.0f, 0.0f);

   vl_compositor_clear_layers(&drv->cstate);

   if (!util_format_is_yuv(format)) {
      struct pipe_sampler_view **views;

      views = surf->buffer->get_sampler_view_planes(surf->buffer);
      vl_compositor_set_rgba_layer(&drv->cstate, &drv->compositor, 0, views[0], &src_rect, NULL, NULL);
   } else
      vl_compositor_set_buffer_layer(&drv->cstate, &drv->compositor, 0, surf->buffer, &src_rect, NULL, VL_COMPOSITOR_WEAVE);

   vl_compositor_set_layer_dst_area(&drv->cstate, 0, &dst_rect);
   vl_compositor_render(&drv->cstate, &drv->compositor, surf_draw, dirty_area, true);

   status = vlVaPutSubpictures(surf, drv, surf_draw, dirty_area, &src_rect, &dst_rect);
   if (status) {
      mtx_unlock(&drv->mutex);
      return status;
   }

   if (drv->pipe->flush_resource)
      drv->pipe->flush_resource(drv->pipe, tex);

   /* flush before calling flush_frontbuffer so that rendering is flushed
    * to back buffer so the texture can be copied in flush_frontbuffer
    */
   vlVaSurfaceFlush(drv, surf);

   screen->flush_frontbuffer(screen, drv->pipe, tex, 0, 0,
                             vscreen->get_private(vscreen), 0, NULL);


   pipe_resource_reference(&tex, NULL);
   pipe_surface_reference(&surf_draw, NULL);
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaLockSurface(VADriverContextP ctx, VASurfaceID surface, unsigned int *fourcc,
                unsigned int *luma_stride, unsigned int *chroma_u_stride, unsigned int *chroma_v_stride,
                unsigned int *luma_offset, unsigned int *chroma_u_offset, unsigned int *chroma_v_offset,
                unsigned int *buffer_name, void **buffer)
{
   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus
vlVaUnlockSurface(VADriverContextP ctx, VASurfaceID surface)
{
   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static void
vlVaAddSurfaceFormat(struct pipe_screen *screen, vlVaConfig *config,
                     enum pipe_format format, VASurfaceAttrib *attrib, int *i)
{
   if (!screen->is_video_format_supported(screen, format, config->profile, config->entrypoint))
      return;

   attrib[*i].type = VASurfaceAttribPixelFormat;
   attrib[*i].value.type = VAGenericValueTypeInteger;
   attrib[*i].flags = VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE;
   attrib[*i].value.value.i = PipeFormatToVaFourcc(format);
   (*i)++;
}

VAStatus
vlVaQuerySurfaceAttributes(VADriverContextP ctx, VAConfigID config_id,
                           VASurfaceAttrib *attrib_list, unsigned int *num_attribs)
{
   vlVaDriver *drv;
   vlVaConfig *config;
   VASurfaceAttrib *attribs;
   struct pipe_screen *pscreen;
   int i;

   if (config_id == VA_INVALID_ID)
      return VA_STATUS_ERROR_INVALID_CONFIG;

   if (!attrib_list && !num_attribs)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   if (!attrib_list) {
      *num_attribs = VL_VA_MAX_IMAGE_FORMATS + VASurfaceAttribCount;
      return VA_STATUS_SUCCESS;
   }

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);

   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   mtx_lock(&drv->mutex);
   config = handle_table_get(drv->htab, config_id);
   mtx_unlock(&drv->mutex);

   if (!config)
      return VA_STATUS_ERROR_INVALID_CONFIG;

   pscreen = VL_VA_PSCREEN(ctx);

   if (!pscreen)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   attribs = CALLOC(VL_VA_MAX_IMAGE_FORMATS + VASurfaceAttribCount,
                    sizeof(VASurfaceAttrib));

   if (!attribs)
      return VA_STATUS_ERROR_ALLOCATION_FAILED;

   i = 0;

   if (config->rt_format & VA_RT_FORMAT_YUV420) {
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_NV12, attribs, &i);
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_YV12, attribs, &i);
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_IYUV, attribs, &i);
   }

   if (config->rt_format & VA_RT_FORMAT_YUV420_10) {
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_P010, attribs, &i);
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_P016, attribs, &i);
   }

   if (config->rt_format & VA_RT_FORMAT_YUV420_12)
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_P012, attribs, &i);

   if (config->rt_format & VA_RT_FORMAT_YUV400)
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_Y8_400_UNORM, attribs, &i);

   if (config->rt_format & VA_RT_FORMAT_YUV422) {
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_UYVY, attribs, &i);
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_YUYV, attribs, &i);
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_Y8_U8_V8_440_UNORM, attribs, &i);
   }

   if (config->rt_format & VA_RT_FORMAT_YUV444)
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_Y8_U8_V8_444_UNORM, attribs, &i);

   if (config->rt_format & VA_RT_FORMAT_RGBP)
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_R8_G8_B8_UNORM, attribs, &i);

   if (config->rt_format & VA_RT_FORMAT_RGB32) {
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_R8G8B8A8_UNORM, attribs, &i);
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_B8G8R8A8_UNORM, attribs, &i);
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_R8G8B8X8_UNORM, attribs, &i);
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_B8G8R8X8_UNORM, attribs, &i);
   }

   if (config->rt_format & VA_RT_FORMAT_RGB32_10) {
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_R10G10B10A2_UNORM, attribs, &i);
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_B10G10R10A2_UNORM, attribs, &i);
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_R10G10B10X2_UNORM, attribs, &i);
      vlVaAddSurfaceFormat(pscreen, config, PIPE_FORMAT_B10G10R10X2_UNORM, attribs, &i);
   }

   attribs[i].type = VASurfaceAttribMemoryType;
   attribs[i].value.type = VAGenericValueTypeInteger;
   attribs[i].flags = VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE;
   attribs[i].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_VA |
#ifdef _WIN32
         VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE |
         VA_SURFACE_ATTRIB_MEM_TYPE_D3D12_RESOURCE;
#else
         VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME |
#if VA_CHECK_VERSION(1, 21, 0)
         VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_3 |
#endif
         VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2;
#endif
   i++;

   attribs[i].type = VASurfaceAttribExternalBufferDescriptor;
   attribs[i].value.type = VAGenericValueTypePointer;
   attribs[i].flags = VA_SURFACE_ATTRIB_SETTABLE;
   attribs[i].value.value.p = NULL; /* ignore */
   i++;

#ifdef HAVE_VA_SURFACE_ATTRIB_DRM_FORMAT_MODIFIERS
   if (drv->pipe->create_video_buffer_with_modifiers) {
      attribs[i].type = VASurfaceAttribDRMFormatModifiers;
      attribs[i].value.type = VAGenericValueTypePointer;
      attribs[i].flags = VA_SURFACE_ATTRIB_SETTABLE;
      attribs[i].value.value.p = NULL; /* ignore */
      i++;
   }
#endif

   /* If VPP supported entry, use the max dimensions cap values, if not fallback to this below */
   if (config->entrypoint != PIPE_VIDEO_ENTRYPOINT_PROCESSING ||
       pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                PIPE_VIDEO_CAP_SUPPORTED))
   {
      unsigned min_width, min_height;
      min_width = pscreen->get_video_param(pscreen,
                                  config->profile, config->entrypoint,
                                  PIPE_VIDEO_CAP_MIN_WIDTH);
      min_height = pscreen->get_video_param(pscreen,
                                  config->profile, config->entrypoint,
                                  PIPE_VIDEO_CAP_MIN_HEIGHT);

      if (min_width > 0 && min_height > 0) {
         attribs[i].type = VASurfaceAttribMinWidth;
         attribs[i].value.type = VAGenericValueTypeInteger;
         attribs[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
         attribs[i].value.value.i = min_width;
         i++;

         attribs[i].type = VASurfaceAttribMinHeight;
         attribs[i].value.type = VAGenericValueTypeInteger;
         attribs[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
         attribs[i].value.value.i = min_height;
         i++;
      }

      attribs[i].type = VASurfaceAttribMaxWidth;
      attribs[i].value.type = VAGenericValueTypeInteger;
      attribs[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
      attribs[i].value.value.i =
         pscreen->get_video_param(pscreen,
                                  config->profile, config->entrypoint,
                                  PIPE_VIDEO_CAP_MAX_WIDTH);
      i++;

      attribs[i].type = VASurfaceAttribMaxHeight;
      attribs[i].value.type = VAGenericValueTypeInteger;
      attribs[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
      attribs[i].value.value.i =
         pscreen->get_video_param(pscreen,
                                  config->profile, config->entrypoint,
                                  PIPE_VIDEO_CAP_MAX_HEIGHT);
      i++;
#if VA_CHECK_VERSION(1, 21, 0)
      int surface_alignment =
         pscreen->get_video_param(pscreen,
                                  config->profile, config->entrypoint,
                                  PIPE_VIDEO_CAP_ENC_SURFACE_ALIGNMENT);
      if (surface_alignment > 0) {
         attribs[i].type = VASurfaceAttribAlignmentSize;
         attribs[i].value.type = VAGenericValueTypeInteger;
         attribs[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
         attribs[i].value.value.i = surface_alignment;
         i++;
      }
#endif
   } else {
      attribs[i].type = VASurfaceAttribMaxWidth;
      attribs[i].value.type = VAGenericValueTypeInteger;
      attribs[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
      attribs[i].value.value.i = vl_video_buffer_max_size(pscreen);
      i++;

      attribs[i].type = VASurfaceAttribMaxHeight;
      attribs[i].value.type = VAGenericValueTypeInteger;
      attribs[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
      attribs[i].value.value.i = vl_video_buffer_max_size(pscreen);
      i++;
   }

   if (i > *num_attribs) {
      *num_attribs = i;
      FREE(attribs);
      return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
   }

   *num_attribs = i;
   memcpy(attrib_list, attribs, i * sizeof(VASurfaceAttrib));
   FREE(attribs);

   return VA_STATUS_SUCCESS;
}

#ifndef _WIN32
static VAStatus
surface_from_external_memory(VADriverContextP ctx, vlVaSurface *surface,
                             VASurfaceAttribExternalBuffers *memory_attribute,
                             unsigned index, struct pipe_video_buffer *templat)
{
   vlVaDriver *drv;
   struct pipe_screen *pscreen;
   struct pipe_resource res_templ;
   struct winsys_handle whandle;
   struct pipe_resource *resources[VL_NUM_COMPONENTS];
   enum pipe_format resource_formats[VL_NUM_COMPONENTS];
   VAStatus result;
   int i;

   pscreen = VL_VA_PSCREEN(ctx);
   drv = VL_VA_DRIVER(ctx);

   if (!memory_attribute || !memory_attribute->buffers ||
       index > memory_attribute->num_buffers)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   if (surface->templat.width != memory_attribute->width ||
       surface->templat.height != memory_attribute->height ||
       memory_attribute->num_planes < 1)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   if (memory_attribute->num_planes > VL_NUM_COMPONENTS)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   vl_get_video_buffer_formats(pscreen, templat->buffer_format, resource_formats);

   memset(&res_templ, 0, sizeof(res_templ));
   res_templ.target = PIPE_TEXTURE_2D;
   res_templ.last_level = 0;
   res_templ.depth0 = 1;
   res_templ.array_size = 1;
   res_templ.bind = PIPE_BIND_SAMPLER_VIEW;
   res_templ.usage = PIPE_USAGE_DEFAULT;

   memset(&whandle, 0, sizeof(struct winsys_handle));
   whandle.type = WINSYS_HANDLE_TYPE_FD;
   whandle.handle = memory_attribute->buffers[index];
   whandle.modifier = DRM_FORMAT_MOD_INVALID;
   whandle.format = templat->buffer_format;

   // Create a resource for each plane.
   memset(resources, 0, sizeof resources);
   for (i = 0; i < memory_attribute->num_planes; i++) {
      unsigned num_planes = util_format_get_num_planes(templat->buffer_format);

      res_templ.format = resource_formats[i];
      if (res_templ.format == PIPE_FORMAT_NONE) {
         if (i < num_planes) {
            result = VA_STATUS_ERROR_INVALID_PARAMETER;
            goto fail;
         } else {
            continue;
         }
      }

      res_templ.width0 = util_format_get_plane_width(templat->buffer_format, i,
                                                     memory_attribute->width);
      res_templ.height0 = util_format_get_plane_height(templat->buffer_format, i,
                                                       memory_attribute->height);

      whandle.stride = memory_attribute->pitches[i];
      whandle.offset = memory_attribute->offsets[i];
      resources[i] = pscreen->resource_from_handle(pscreen, &res_templ, &whandle,
                                                   PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
      if (!resources[i]) {
         result = VA_STATUS_ERROR_ALLOCATION_FAILED;
         goto fail;
      }
   }

   surface->buffer = vl_video_buffer_create_ex2(drv->pipe, templat, resources);
   if (!surface->buffer) {
      result = VA_STATUS_ERROR_ALLOCATION_FAILED;
      goto fail;
   }
   return VA_STATUS_SUCCESS;

fail:
   for (i = 0; i < VL_NUM_COMPONENTS; i++)
      pipe_resource_reference(&resources[i], NULL);
   return result;
}

static VAStatus
surface_from_prime(VADriverContextP ctx, vlVaSurface *surface,
                   VADRMPRIMESurfaceDescriptor *desc, int mem_type,
                   struct pipe_video_buffer *templat)
{
   vlVaDriver *drv;
   struct pipe_screen *pscreen;
   struct pipe_resource res_templ;
   struct winsys_handle whandle;
   struct pipe_resource *resources[VL_NUM_COMPONENTS];
   enum pipe_format resource_formats[VL_NUM_COMPONENTS];
   unsigned num_format_planes, expected_planes, input_planes, plane;
   VAStatus result;

   num_format_planes = util_format_get_num_planes(templat->buffer_format);
   pscreen = VL_VA_PSCREEN(ctx);
   drv = VL_VA_DRIVER(ctx);

   if (!desc || desc->num_layers >= 4 ||desc->num_objects == 0)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   if (surface->templat.width != desc->width ||
       surface->templat.height != desc->height ||
       desc->num_layers < 1)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   if (desc->num_layers > VL_NUM_COMPONENTS)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   input_planes = 0;
   for (unsigned i = 0; i < desc->num_layers; ++i) {
      if (desc->layers[i].num_planes == 0 || desc->layers[i].num_planes > 4)
         return VA_STATUS_ERROR_INVALID_PARAMETER;

      for (unsigned j = 0; j < desc->layers[i].num_planes; ++j)
         if (desc->layers[i].object_index[j] >= desc->num_objects)
            return VA_STATUS_ERROR_INVALID_PARAMETER;

      input_planes += desc->layers[i].num_planes;
   }

   expected_planes = num_format_planes;
   if (desc->objects[0].drm_format_modifier != DRM_FORMAT_MOD_INVALID &&
       pscreen->is_dmabuf_modifier_supported &&
       pscreen->is_dmabuf_modifier_supported(pscreen, desc->objects[0].drm_format_modifier,
                                            templat->buffer_format, NULL) &&
       pscreen->get_dmabuf_modifier_planes)
      expected_planes = pscreen->get_dmabuf_modifier_planes(pscreen, desc->objects[0].drm_format_modifier,
                                                           templat->buffer_format);

   if (input_planes != expected_planes)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   vl_get_video_buffer_formats(pscreen, templat->buffer_format, resource_formats);

   memset(&res_templ, 0, sizeof(res_templ));
   res_templ.target = PIPE_TEXTURE_2D;
   res_templ.last_level = 0;
   res_templ.depth0 = 1;
   res_templ.array_size = 1;
   res_templ.bind = PIPE_BIND_SAMPLER_VIEW;
   res_templ.usage = PIPE_USAGE_DEFAULT;
   res_templ.format = templat->buffer_format;

   memset(&whandle, 0, sizeof(struct winsys_handle));
   whandle.type = WINSYS_HANDLE_TYPE_FD;
   whandle.format = templat->buffer_format;
   whandle.modifier = desc->objects[0].drm_format_modifier;

   // Create a resource for each plane.
   memset(resources, 0, sizeof resources);

   /* This does a backwards walk to set the next pointers. It interleaves so
    * that the main planes always come first and then the first compression metadata
    * plane of each main plane etc. */
   plane = input_planes - 1;
   for (int layer_plane = 3; layer_plane >= 0; --layer_plane) {
      for (int layer = desc->num_layers - 1; layer >= 0; --layer) {
         if (layer_plane >= desc->layers[layer].num_planes)
            continue;

         if (plane < num_format_planes)
            res_templ.format = resource_formats[plane];

         res_templ.width0 = util_format_get_plane_width(templat->buffer_format, plane,
                                                        desc->width);
         res_templ.height0 = util_format_get_plane_height(templat->buffer_format, plane,
                                                          desc->height);
         whandle.stride = desc->layers[layer].pitch[layer_plane];
         whandle.offset = desc->layers[layer].offset[layer_plane];
         whandle.handle = desc->objects[desc->layers[layer].object_index[layer_plane]].fd;
         whandle.plane = plane;

         resources[plane] = pscreen->resource_from_handle(pscreen, &res_templ, &whandle,
                                                          PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
         if (!resources[plane]) {
            result = VA_STATUS_ERROR_ALLOCATION_FAILED;
            goto fail;
         }

         /* After the resource gets created the resource now owns the next reference. */
         res_templ.next = NULL;

         if (plane)
            pipe_resource_reference(&res_templ.next, resources[plane]);
         --plane;
      }
   }

   surface->buffer = vl_video_buffer_create_ex2(drv->pipe, templat, resources);
   if (!surface->buffer) {
      result = VA_STATUS_ERROR_ALLOCATION_FAILED;
      goto fail;
   }
   return VA_STATUS_SUCCESS;

fail:
   pipe_resource_reference(&res_templ.next, NULL);
   for (int i = 0; i < VL_NUM_COMPONENTS; i++)
      pipe_resource_reference(&resources[i], NULL);
   return result;
}
#else
static VAStatus
surface_from_external_win32_memory(VADriverContextP ctx, vlVaSurface *surface,
                             int memory_type, void *res_handle,
                             struct pipe_video_buffer *templat)
{
   vlVaDriver *drv;
   struct pipe_screen *pscreen;
   struct winsys_handle whandle;
   VAStatus result;

   pscreen = VL_VA_PSCREEN(ctx);
   drv = VL_VA_DRIVER(ctx);

   templat->buffer_format = surface->templat.buffer_format;
   templat->width = surface->templat.width;
   templat->height = surface->templat.height;

   memset(&whandle, 0, sizeof(whandle));
   whandle.format = surface->templat.buffer_format;
   if (memory_type == VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE) {
      whandle.type = WINSYS_HANDLE_TYPE_FD;
      whandle.handle = res_handle;
   } else if (memory_type == VA_SURFACE_ATTRIB_MEM_TYPE_D3D12_RESOURCE) {
      whandle.type = WINSYS_HANDLE_TYPE_D3D12_RES;
      whandle.com_obj = res_handle;
   } else {
      return VA_STATUS_ERROR_INVALID_PARAMETER;
   }

   surface->buffer = drv->pipe->video_buffer_from_handle(drv->pipe, templat, &whandle, PIPE_USAGE_DEFAULT);
   if (!surface->buffer) {
      result = VA_STATUS_ERROR_ALLOCATION_FAILED;
      goto fail;
   }
   return VA_STATUS_SUCCESS;

fail:
   return result;
}

#endif

VAStatus
vlVaHandleSurfaceAllocate(vlVaDriver *drv, vlVaSurface *surface,
                          struct pipe_video_buffer *templat,
                          const uint64_t *modifiers,
                          unsigned int modifiers_count)
{
   struct pipe_surface **surfaces;
   unsigned i;

   if (modifiers_count > 0) {
      if (!drv->pipe->create_video_buffer_with_modifiers)
         return VA_STATUS_ERROR_ATTR_NOT_SUPPORTED;
      surface->buffer =
         drv->pipe->create_video_buffer_with_modifiers(drv->pipe, templat,
                                                       modifiers,
                                                       modifiers_count);
   } else {
      surface->buffer = drv->pipe->create_video_buffer(drv->pipe, templat);
   }
   if (!surface->buffer)
      return VA_STATUS_ERROR_ALLOCATION_FAILED;

   if (drv->pipe->screen->get_video_param(drv->pipe->screen,
                                          PIPE_VIDEO_PROFILE_UNKNOWN,
                                          PIPE_VIDEO_ENTRYPOINT_UNKNOWN,
                                          PIPE_VIDEO_CAP_SKIP_CLEAR_SURFACE))
      return VA_STATUS_SUCCESS;

   surfaces = surface->buffer->get_surfaces(surface->buffer);
   if (surfaces) {
      for (i = 0; i < VL_MAX_SURFACES; ++i) {
         union pipe_color_union c;
         memset(&c, 0, sizeof(c));

         if (!surfaces[i])
            continue;

         if (i > !!surface->buffer->interlaced)
            c.f[0] = c.f[1] = c.f[2] = c.f[3] = 0.5f;

         drv->pipe->clear_render_target(drv->pipe, surfaces[i], &c, 0, 0,
                  surfaces[i]->width, surfaces[i]->height,
                  false);
      }
      vlVaSurfaceFlush(drv, surface);
   }

   return VA_STATUS_SUCCESS;
}

struct pipe_video_buffer *
vlVaGetSurfaceBuffer(vlVaDriver *drv, vlVaSurface *surface)
{
   if (!surface)
      return NULL;
   if (surface->buffer)
      return surface->buffer;
   vlVaHandleSurfaceAllocate(drv, surface, &surface->templat, NULL, 0);
   return surface->buffer;
}

void
vlVaSurfaceFlush(vlVaDriver *drv, vlVaSurface *surf)
{
   drv->pipe->flush(drv->pipe, &surf->pipe_fence,
                    drv->has_external_handles ? 0 : PIPE_FLUSH_ASYNC);
}

static void
vlVaSwitchToProtectedContext(vlVaDriver *drv)
{
   if (drv->pipe2)
      return;

   /* For now the context only needs to have graphics */
   struct pipe_context *ctx = pipe_create_multimedia_context(drv->pipe->screen, false);
   if (!ctx)
      return;

   drv->pipe2 = drv->pipe;
   drv->pipe = ctx;

   if (drv->cstate.pipe) {
      vl_compositor_cleanup_state(&drv->cstate);
      vl_compositor_cleanup(&drv->compositor);
      vl_compositor_init(&drv->compositor, drv->pipe, false);
      vl_compositor_init_state(&drv->cstate, drv->pipe);
   }
}

static int
rt_format_to_fourcc(uint32_t format)
{
   switch (format) {
   case VA_RT_FORMAT_YUV420:
      return VA_FOURCC_NV12;
   case VA_RT_FORMAT_YUV420_10:
      return VA_FOURCC_P010;
   case VA_RT_FORMAT_YUV420_12:
      return VA_FOURCC_P012;
   case VA_RT_FORMAT_YUV422:
      return VA_FOURCC_YUY2;
   case VA_RT_FORMAT_YUV444:
      return VA_FOURCC_444P;
   case VA_RT_FORMAT_YUV400:
      return VA_FOURCC_Y800;
   case VA_RT_FORMAT_RGBP:
      return VA_FOURCC_RGBP;
   case VA_RT_FORMAT_RGB32:
      return VA_FOURCC_BGRA;
   case VA_RT_FORMAT_RGB32_10:
      return VA_FOURCC_X2R10G10B10;
   default:
      return 0;
   }
}

VAStatus
vlVaCreateSurfaces2(VADriverContextP ctx, unsigned int format,
                    unsigned int width, unsigned int height,
                    VASurfaceID *surfaces, unsigned int num_surfaces,
                    VASurfaceAttrib *attrib_list, unsigned int num_attribs)
{
   vlVaDriver *drv;
   VASurfaceAttribExternalBuffers *memory_attribute;
#ifdef _WIN32
   void **win32_handles;
#else
   VADRMPRIMESurfaceDescriptor *prime_desc = NULL;
#ifdef HAVE_VA_SURFACE_ATTRIB_DRM_FORMAT_MODIFIERS
   const VADRMFormatModifierList *modifier_list;
#endif
#endif
   struct pipe_video_buffer templat = {0};
   struct pipe_screen *pscreen;
   int i;
   int memory_type;
   int expected_fourcc;
   VAStatus vaStatus;
   vlVaSurface *surf;
   bool protected;
   const uint64_t *modifiers;
   unsigned int modifiers_count;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!(width && height))
      return VA_STATUS_ERROR_INVALID_IMAGE_FORMAT;

   drv = VL_VA_DRIVER(ctx);

   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   pscreen = VL_VA_PSCREEN(ctx);

   if (!pscreen)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   /* Default. */
   memory_attribute = NULL;
   memory_type = VA_SURFACE_ATTRIB_MEM_TYPE_VA;
   expected_fourcc = 0;
   modifiers = NULL;
   modifiers_count = 0;

   protected = format & VA_RT_FORMAT_PROTECTED;
   format &= ~VA_RT_FORMAT_PROTECTED;

   if (protected)
      vlVaSwitchToProtectedContext(drv);

   expected_fourcc = rt_format_to_fourcc(format);
   if (!expected_fourcc)
      return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;

   for (i = 0; i < num_attribs && attrib_list; i++) {
      if (!(attrib_list[i].flags & VA_SURFACE_ATTRIB_SETTABLE))
         continue;

      switch (attrib_list[i].type) {
      case VASurfaceAttribPixelFormat:
         if (attrib_list[i].value.type != VAGenericValueTypeInteger)
            return VA_STATUS_ERROR_INVALID_PARAMETER;
         expected_fourcc = attrib_list[i].value.value.i;
         break;
      case VASurfaceAttribMemoryType:
         if (attrib_list[i].value.type != VAGenericValueTypeInteger)
            return VA_STATUS_ERROR_INVALID_PARAMETER;

         switch (attrib_list[i].value.value.i) {
         case VA_SURFACE_ATTRIB_MEM_TYPE_VA:

#ifdef _WIN32
         case VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE:
         case VA_SURFACE_ATTRIB_MEM_TYPE_D3D12_RESOURCE:
#else
         case VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME:
         case VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2:
         case VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_3:
#endif
            memory_type = attrib_list[i].value.value.i;
            break;
         default:
            return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;
         }
         break;
      case VASurfaceAttribExternalBufferDescriptor:
         if (attrib_list[i].value.type != VAGenericValueTypePointer)
            return VA_STATUS_ERROR_INVALID_PARAMETER;
#ifndef _WIN32
         if (memory_type == VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2 ||
             memory_type == VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_3)
            prime_desc = (VADRMPRIMESurfaceDescriptor *)attrib_list[i].value.value.p;
#else
         else if (memory_type == VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE ||
                  memory_type == VA_SURFACE_ATTRIB_MEM_TYPE_D3D12_RESOURCE)
            win32_handles = (void**) attrib_list[i].value.value.p;
#endif
         else
            memory_attribute = (VASurfaceAttribExternalBuffers *)attrib_list[i].value.value.p;
         break;
#ifndef _WIN32
#ifdef HAVE_VA_SURFACE_ATTRIB_DRM_FORMAT_MODIFIERS
      case VASurfaceAttribDRMFormatModifiers:
         if (attrib_list[i].value.type != VAGenericValueTypePointer)
            return VA_STATUS_ERROR_INVALID_PARAMETER;
         modifier_list = attrib_list[i].value.value.p;
         if (modifier_list != NULL) {
            modifiers = modifier_list->modifiers;
            modifiers_count = modifier_list->num_modifiers;
         }
         break;
#endif
#endif
      case VASurfaceAttribUsageHint:
         if (attrib_list[i].value.type != VAGenericValueTypeInteger)
            return VA_STATUS_ERROR_INVALID_PARAMETER;
         break;
      default:
         return VA_STATUS_ERROR_ATTR_NOT_SUPPORTED;
      }
   }

   switch (memory_type) {
   case VA_SURFACE_ATTRIB_MEM_TYPE_VA:
      break;
#ifdef _WIN32
         case VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE:
         case VA_SURFACE_ATTRIB_MEM_TYPE_D3D12_RESOURCE:
         if (!win32_handles)
            return VA_STATUS_ERROR_INVALID_PARAMETER;
         break;
#else
   case VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME:
      if (!memory_attribute)
         return VA_STATUS_ERROR_INVALID_PARAMETER;
      if (modifiers)
         return VA_STATUS_ERROR_INVALID_PARAMETER;

      expected_fourcc = memory_attribute->pixel_format;
      break;
   case VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2:
   case VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_3:
      /* If we don't have surface descriptor, use it as a hint
       * that application will export the surface later. */
      if (!prime_desc) {
         templat.bind |= PIPE_BIND_SHARED;
         memory_type = VA_SURFACE_ATTRIB_MEM_TYPE_VA;
      } else {
         expected_fourcc = prime_desc->fourcc;
      }
      break;
#endif
   default:
      assert(0);
   }

   if (!modifiers)
      templat.interlaced =
         !pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                   PIPE_VIDEO_ENTRYPOINT_BITSTREAM,
                                   PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE);

#ifndef _WIN32
   if (expected_fourcc != VA_FOURCC_NV12 || memory_attribute || prime_desc)
#else
   if (expected_fourcc != VA_FOURCC_NV12 || memory_attribute)
#endif
     templat.interlaced = false;

   templat.buffer_format = VaFourccToPipeFormat(expected_fourcc);
   templat.width = width;
   templat.height = height;
   if (protected)
      templat.bind |= PIPE_BIND_PROTECTED;

   memset(surfaces, VA_INVALID_ID, num_surfaces * sizeof(VASurfaceID));

   mtx_lock(&drv->mutex);
   for (i = 0; i < num_surfaces; i++) {
      surf = CALLOC(1, sizeof(vlVaSurface));
      if (!surf) {
         vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
         goto no_res;
      }

      surf->templat = templat;

      switch (memory_type) {
      case VA_SURFACE_ATTRIB_MEM_TYPE_VA:
         /* The application will clear the TILING flag when the surface is
          * intended to be exported as dmabuf. Adding shared flag because not
          * null memory_attribute means VASurfaceAttribExternalBuffers is used.
          */
         if (memory_attribute &&
             !(memory_attribute->flags & VA_SURFACE_EXTBUF_DESC_ENABLE_TILING))
            surf->templat.bind = PIPE_BIND_LINEAR | PIPE_BIND_SHARED;

         if (modifiers) {
            vaStatus = vlVaHandleSurfaceAllocate(drv, surf, &surf->templat, modifiers,
                                                 modifiers_count);
            if (vaStatus != VA_STATUS_SUCCESS)
               goto free_surf;
         } /* Delayed allocation from vlVaGetSurfaceBuffer otherwise */
         break;

#ifdef _WIN32
      case VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE:
      case VA_SURFACE_ATTRIB_MEM_TYPE_D3D12_RESOURCE:
         vaStatus = surface_from_external_win32_memory(ctx, surf, memory_type, win32_handles[i], &templat);
         if (vaStatus != VA_STATUS_SUCCESS)
            goto free_surf;
         break;
#else
      case VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME:
         vaStatus = surface_from_external_memory(ctx, surf, memory_attribute, i, &templat);
         if (vaStatus != VA_STATUS_SUCCESS)
            goto free_surf;
         break;

      case VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2:
      case VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_3:
         vaStatus = surface_from_prime(ctx, surf, prime_desc, memory_type, &templat);
         if (vaStatus != VA_STATUS_SUCCESS)
            goto free_surf;
         break;
#endif
      default:
         assert(0);
      }

      util_dynarray_init(&surf->subpics, NULL);
      surfaces[i] = handle_table_add(drv->htab, surf);
      if (!surfaces[i]) {
         vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
         goto destroy_surf;
      }
   }

   if (memory_type != VA_SURFACE_ATTRIB_MEM_TYPE_VA)
      drv->has_external_handles = true;
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;

destroy_surf:
   if (surf->buffer)
      surf->buffer->destroy(surf->buffer);

free_surf:
   FREE(surf);

no_res:
   mtx_unlock(&drv->mutex);
   if (i)
      vlVaDestroySurfaces(ctx, surfaces, i);

   return vaStatus;
}

VAStatus
vlVaQueryVideoProcFilters(VADriverContextP ctx, VAContextID context,
                          VAProcFilterType *filters, unsigned int *num_filters)
{
   unsigned int num = 0;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!num_filters || !filters)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   filters[num++] = VAProcFilterDeinterlacing;

   *num_filters = num;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaQueryVideoProcFilterCaps(VADriverContextP ctx, VAContextID context,
                             VAProcFilterType type, void *filter_caps,
                             unsigned int *num_filter_caps)
{
   unsigned int i;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!filter_caps || !num_filter_caps)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   i = 0;

   switch (type) {
   case VAProcFilterNone:
      break;
   case VAProcFilterDeinterlacing: {
      VAProcFilterCapDeinterlacing *deint = filter_caps;

      if (*num_filter_caps < 3) {
         *num_filter_caps = 3;
         return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
      }

      deint[i++].type = VAProcDeinterlacingBob;
      deint[i++].type = VAProcDeinterlacingWeave;
      deint[i++].type = VAProcDeinterlacingMotionAdaptive;
      break;
   }

   case VAProcFilterNoiseReduction:
   case VAProcFilterSharpening:
   case VAProcFilterColorBalance:
   case VAProcFilterSkinToneEnhancement:
      return VA_STATUS_ERROR_UNIMPLEMENTED;
   default:
      assert(0);
   }

   *num_filter_caps = i;

   return VA_STATUS_SUCCESS;
}

static VAProcColorStandardType vpp_input_color_standards[] = {
   VAProcColorStandardBT601,
   VAProcColorStandardBT709
};

static VAProcColorStandardType vpp_output_color_standards[] = {
   VAProcColorStandardBT601,
   VAProcColorStandardBT709
};

static VAProcColorStandardType vpp_input_color_standards_extends[] = {
   VAProcColorStandardBT601,
   VAProcColorStandardBT709,
   VAProcColorStandardBT2020,
   VAProcColorStandardExplicit
};

static VAProcColorStandardType vpp_output_color_standards_extends[] = {
   VAProcColorStandardBT601,
   VAProcColorStandardBT709,
   VAProcColorStandardBT2020,
   VAProcColorStandardExplicit
};

VAStatus
vlVaQueryVideoProcPipelineCaps(VADriverContextP ctx, VAContextID context,
                               VABufferID *filters, unsigned int num_filters,
                               VAProcPipelineCaps *pipeline_cap)
{
   unsigned int i = 0;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!pipeline_cap)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   if (num_filters && !filters)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   pipeline_cap->pipeline_flags = 0;
   pipeline_cap->filter_flags = 0;
   pipeline_cap->num_forward_references = 0;
   pipeline_cap->num_backward_references = 0;
   pipeline_cap->rotation_flags = VA_ROTATION_NONE;
   pipeline_cap->mirror_flags = VA_MIRROR_NONE;

   struct pipe_screen *pscreen = VL_VA_PSCREEN(ctx);
   bool media_only = !pscreen->caps.graphics && !pscreen->caps.compute;
   uint32_t pipe_orientation_flags = pscreen->get_video_param(pscreen,
                                                              PIPE_VIDEO_PROFILE_UNKNOWN,
                                                              PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                              PIPE_VIDEO_CAP_VPP_ORIENTATION_MODES);

   if (!media_only || pipe_orientation_flags & PIPE_VIDEO_VPP_ROTATION_90)
      pipeline_cap->rotation_flags |= (1 << VA_ROTATION_90);
   if (!media_only || pipe_orientation_flags & PIPE_VIDEO_VPP_ROTATION_180)
      pipeline_cap->rotation_flags |= (1 << VA_ROTATION_180);
   if (!media_only || pipe_orientation_flags & PIPE_VIDEO_VPP_ROTATION_270)
      pipeline_cap->rotation_flags |= (1 << VA_ROTATION_270);

   if (!media_only || pipe_orientation_flags & PIPE_VIDEO_VPP_FLIP_HORIZONTAL)
      pipeline_cap->mirror_flags |= VA_MIRROR_HORIZONTAL;
   if (!media_only || pipe_orientation_flags & PIPE_VIDEO_VPP_FLIP_VERTICAL)
      pipeline_cap->mirror_flags |= VA_MIRROR_VERTICAL;

   if (pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN, PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                PIPE_VIDEO_CAP_VPP_SUPPORT_HDR_INPUT)) {
      pipeline_cap->num_input_color_standards = ARRAY_SIZE(vpp_input_color_standards_extends);
      pipeline_cap->input_color_standards = vpp_input_color_standards_extends;
   } else {
      pipeline_cap->num_input_color_standards = ARRAY_SIZE(vpp_input_color_standards);
      pipeline_cap->input_color_standards = vpp_input_color_standards;
   }
   if (pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN, PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                PIPE_VIDEO_CAP_VPP_SUPPORT_HDR_OUTPUT)) {
      pipeline_cap->num_output_color_standards = ARRAY_SIZE(vpp_output_color_standards_extends);
      pipeline_cap->output_color_standards = vpp_output_color_standards_extends;
   } else {
      pipeline_cap->num_output_color_standards = ARRAY_SIZE(vpp_output_color_standards);
      pipeline_cap->output_color_standards = vpp_output_color_standards;
   }

   pipeline_cap->max_input_width = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                            PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                            PIPE_VIDEO_CAP_VPP_MAX_INPUT_WIDTH);

   pipeline_cap->max_input_height = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                             PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                             PIPE_VIDEO_CAP_VPP_MAX_INPUT_HEIGHT);

   pipeline_cap->min_input_width = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                            PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                            PIPE_VIDEO_CAP_VPP_MIN_INPUT_WIDTH);

   pipeline_cap->min_input_height = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                             PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                             PIPE_VIDEO_CAP_VPP_MIN_INPUT_HEIGHT);

   pipeline_cap->max_output_width = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                             PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                             PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_WIDTH);

   pipeline_cap->max_output_height = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                              PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                              PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_HEIGHT);

   pipeline_cap->min_output_width = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                             PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                             PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_WIDTH);

   pipeline_cap->min_output_height = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                              PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                              PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_HEIGHT);

   uint32_t pipe_blend_modes = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                        PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                        PIPE_VIDEO_CAP_VPP_BLEND_MODES);

   pipeline_cap->blend_flags = 0;
   if (pipe_blend_modes & PIPE_VIDEO_VPP_BLEND_MODE_GLOBAL_ALPHA)
      pipeline_cap->blend_flags |= VA_BLEND_GLOBAL_ALPHA;

   vlVaDriver *drv = VL_VA_DRIVER(ctx);

   mtx_lock(&drv->mutex);
   for (i = 0; i < num_filters; i++) {
      vlVaBuffer *buf = handle_table_get(drv->htab, filters[i]);
      VAProcFilterParameterBufferBase *filter;

      if (!buf || buf->type != VAProcFilterParameterBufferType) {
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_INVALID_BUFFER;
      }

      filter = buf->data;
      switch (filter->type) {
      case VAProcFilterDeinterlacing: {
         VAProcFilterParameterBufferDeinterlacing *deint = buf->data;
         if (deint->algorithm == VAProcDeinterlacingMotionAdaptive) {
            pipeline_cap->num_forward_references = 2;
            pipeline_cap->num_backward_references = 1;
         }
         break;
      }
      default:
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_UNIMPLEMENTED;
      }
   }
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}

#ifndef _WIN32
static uint32_t pipe_format_to_drm_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R8_UNORM:
      return DRM_FORMAT_R8;
   case PIPE_FORMAT_R8G8_UNORM:
      return DRM_FORMAT_GR88;
   case PIPE_FORMAT_R16_UNORM:
      return DRM_FORMAT_R16;
   case PIPE_FORMAT_R16G16_UNORM:
      return DRM_FORMAT_GR1616;
   case PIPE_FORMAT_B8G8R8A8_UNORM:
      return DRM_FORMAT_ARGB8888;
   case PIPE_FORMAT_R8G8B8A8_UNORM:
      return DRM_FORMAT_ABGR8888;
   case PIPE_FORMAT_B8G8R8X8_UNORM:
      return DRM_FORMAT_XRGB8888;
   case PIPE_FORMAT_R8G8B8X8_UNORM:
      return DRM_FORMAT_XBGR8888;
   case PIPE_FORMAT_B10G10R10A2_UNORM:
      return DRM_FORMAT_ARGB2101010;
   case PIPE_FORMAT_R10G10B10A2_UNORM:
      return DRM_FORMAT_ABGR2101010;
   case PIPE_FORMAT_B10G10R10X2_UNORM:
      return DRM_FORMAT_XRGB2101010;
   case PIPE_FORMAT_R10G10B10X2_UNORM:
      return DRM_FORMAT_XBGR2101010;
   case PIPE_FORMAT_NV12:
      return DRM_FORMAT_NV12;
   case PIPE_FORMAT_P010:
      return DRM_FORMAT_P010;
   case PIPE_FORMAT_P012:
      return DRM_FORMAT_P012;
   case PIPE_FORMAT_YUYV:
   case PIPE_FORMAT_R8G8_R8B8_UNORM:
      return DRM_FORMAT_YUYV;
   default:
      return DRM_FORMAT_INVALID;
   }
}
#endif

#if VA_CHECK_VERSION(1, 1, 0)
VAStatus
vlVaExportSurfaceHandle(VADriverContextP ctx,
                        VASurfaceID surface_id,
                        uint32_t mem_type,
                        uint32_t flags,
                        void *descriptor)
{
   vlVaDriver *drv;
   vlVaSurface *surf;
   struct pipe_surface **surfaces;
   struct pipe_screen *screen;
   VAStatus ret;
   unsigned int usage;

#ifdef _WIN32
   if ((mem_type != VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE)
      && (mem_type != VA_SURFACE_ATTRIB_MEM_TYPE_D3D12_RESOURCE))
      return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;

   if ((flags & VA_EXPORT_SURFACE_COMPOSED_LAYERS) == 0)
      return VA_STATUS_ERROR_INVALID_SURFACE;
#else
   int i, p;
   if (mem_type != VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2 &&
       mem_type != VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_3)
      return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;
#endif

   drv    = VL_VA_DRIVER(ctx);
   screen = VL_VA_PSCREEN(ctx);
   mtx_lock(&drv->mutex);

   surf = handle_table_get(drv->htab, surface_id);
   vlVaGetSurfaceBuffer(drv, surf);
   if (!surf || !surf->buffer) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   if (surf->buffer->interlaced) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   surfaces = surf->buffer->get_surfaces(surf->buffer);

   usage = 0;
   if (flags & VA_EXPORT_SURFACE_WRITE_ONLY)
      usage |= PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE;

#ifdef _WIN32
   struct winsys_handle whandle;
   memset(&whandle, 0, sizeof(struct winsys_handle));
   struct pipe_resource *resource = surfaces[0]->texture;

   if (mem_type == VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE)
      whandle.type = WINSYS_HANDLE_TYPE_FD;
   else if (mem_type == VA_SURFACE_ATTRIB_MEM_TYPE_D3D12_RESOURCE)
      whandle.type = WINSYS_HANDLE_TYPE_D3D12_RES;

   if (!screen->resource_get_handle(screen, drv->pipe, resource,
                                    &whandle, usage)) {
      ret = VA_STATUS_ERROR_INVALID_SURFACE;
      goto fail;
   }

   if (mem_type == VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE)
      *(HANDLE**)descriptor = whandle.handle;
   else if (mem_type == VA_SURFACE_ATTRIB_MEM_TYPE_D3D12_RESOURCE)
      *(void**) descriptor = whandle.com_obj;

#else
   VADRMPRIMESurfaceDescriptor *desc = descriptor;
   desc->fourcc = PipeFormatToVaFourcc(surf->buffer->buffer_format);
   desc->width = surf->templat.width;
   desc->height = surf->templat.height;
   desc->num_objects = 0;

   bool supports_contiguous_planes = screen->resource_get_info && surf->buffer->contiguous_planes;

   for (p = 0; p < ARRAY_SIZE(desc->objects); p++) {
      struct winsys_handle whandle;
      struct pipe_resource *resource;
      uint32_t drm_format;

      if (!surfaces[p])
         break;

      resource = surfaces[p]->texture;

      drm_format = pipe_format_to_drm_format(resource->format);
      if (drm_format == DRM_FORMAT_INVALID) {
         ret = VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;
         goto fail;
      }

      /* If the driver stores all planes contiguously in memory, only one
       * handle needs to be exported. resource_get_info is used to obtain
       * pitch and offset for each layer. */
      if (!desc->num_objects || !supports_contiguous_planes) {
         memset(&whandle, 0, sizeof(whandle));
         whandle.type = WINSYS_HANDLE_TYPE_FD;

         if (!screen->resource_get_handle(screen, drv->pipe, resource,
                                          &whandle, usage)) {
            ret = VA_STATUS_ERROR_INVALID_SURFACE;
            goto fail;
         }

         desc->objects[desc->num_objects].fd = (int) whandle.handle;
         /* As per VADRMPRIMESurfaceDescriptor documentation, size must be the
         * "Total size of this object (may include regions which are not part
         * of the surface)."" */
         desc->objects[desc->num_objects].size = (uint32_t) whandle.size;
         desc->objects[desc->num_objects].drm_format_modifier = whandle.modifier;

         desc->num_objects++;
      }

      if (flags & VA_EXPORT_SURFACE_COMPOSED_LAYERS) {
         desc->layers[0].object_index[p] = desc->num_objects - 1;

         if (supports_contiguous_planes) {
            screen->resource_get_info(screen, resource, &desc->layers[0].pitch[p], &desc->layers[0].offset[p]);
         } else {
            desc->layers[0].pitch[p] = whandle.stride;
            desc->layers[0].offset[p] = whandle.offset;
         }
      } else {
         desc->layers[p].drm_format      = drm_format;
         desc->layers[p].num_planes      = 1;
         desc->layers[p].object_index[0] = desc->num_objects - 1;

         if (supports_contiguous_planes) {
            screen->resource_get_info(screen, resource, &desc->layers[p].pitch[0], &desc->layers[p].offset[0]);
         } else {
            desc->layers[p].pitch[0] = whandle.stride;
            desc->layers[p].offset[0] = whandle.offset;
         }
      }
   }

   if (flags & VA_EXPORT_SURFACE_COMPOSED_LAYERS) {
      uint32_t drm_format = pipe_format_to_drm_format(surf->buffer->buffer_format);
      if (drm_format == DRM_FORMAT_INVALID) {
         ret = VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;
         goto fail;
      }

      desc->num_layers = 1;
      desc->layers[0].drm_format = drm_format;
      desc->layers[0].num_planes = p;
   } else {
      desc->num_layers = p;
   }

#if VA_CHECK_VERSION(1, 21, 0)
   if (mem_type == VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_3) {
      VADRMPRIME3SurfaceDescriptor *desc3 = descriptor;
      memset(desc3->reserved, 0, sizeof(desc3->reserved));
      desc3->flags = 0;
      if (surf->templat.bind & PIPE_BIND_PROTECTED)
         desc3->flags |= VA_SURFACE_EXTBUF_DESC_PROTECTED;
   }
#endif

#endif

   drv->has_external_handles = true;
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;

fail:
#ifndef _WIN32
   for (i = 0; i < desc->num_objects; i++)
      close(desc->objects[i].fd);
#else
   if(whandle.handle)
      CloseHandle(whandle.handle);
#endif

   mtx_unlock(&drv->mutex);

   return ret;
}
#endif
