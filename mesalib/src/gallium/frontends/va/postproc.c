/**************************************************************************
 *
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include "util/u_handle_table.h"
#include "util/u_memory.h"
#include "util/u_compute.h"

#include "vl/vl_defines.h"
#include "vl/vl_video_buffer.h"
#include "vl/vl_deint_filter.h"

#include "va_private.h"

static const VARectangle *
vlVaRegionDefault(const VARectangle *region, vlVaSurface *surf,
		  VARectangle *def)
{
   if (region)
      return region;

   def->x = 0;
   def->y = 0;
   def->width = surf->templat.width;
   def->height = surf->templat.height;

   return def;
}

static VAStatus
vlVaPostProcCompositor(vlVaDriver *drv, vlVaContext *context,
                       const VARectangle *src_region,
                       const VARectangle *dst_region,
                       struct pipe_video_buffer *src,
                       struct pipe_video_buffer *dst,
                       enum vl_compositor_deinterlace deinterlace)
{
   struct pipe_surface **surfaces;
   struct u_rect src_rect;
   struct u_rect dst_rect;

   surfaces = dst->get_surfaces(dst);
   if (!surfaces || !surfaces[0])
      return VA_STATUS_ERROR_INVALID_SURFACE;

   src_rect.x0 = src_region->x;
   src_rect.y0 = src_region->y;
   src_rect.x1 = src_region->x + src_region->width;
   src_rect.y1 = src_region->y + src_region->height;

   dst_rect.x0 = dst_region->x;
   dst_rect.y0 = dst_region->y;
   dst_rect.x1 = dst_region->x + dst_region->width;
   dst_rect.y1 = dst_region->y + dst_region->height;

   vl_compositor_clear_layers(&drv->cstate);
   vl_compositor_set_buffer_layer(&drv->cstate, &drv->compositor, 0, src,
				  &src_rect, NULL, deinterlace);
   vl_compositor_set_layer_dst_area(&drv->cstate, 0, &dst_rect);
   vl_compositor_render(&drv->cstate, &drv->compositor, surfaces[0], NULL, false);

   drv->pipe->flush(drv->pipe, NULL, 0);
   return VA_STATUS_SUCCESS;
}

static void vlVaGetBox(struct pipe_video_buffer *buf, unsigned idx,
                       struct pipe_box *box, const VARectangle *region)
{
   unsigned plane = buf->interlaced ? idx / 2: idx;
   unsigned x, y, width, height;

   x = abs(region->x);
   y = abs(region->y);
   width = region->width;
   height = region->height;

   vl_video_buffer_adjust_size(&x, &y, plane,
                               pipe_format_to_chroma_format(buf->buffer_format),
                               buf->interlaced);
   vl_video_buffer_adjust_size(&width, &height, plane,
                               pipe_format_to_chroma_format(buf->buffer_format),
                               buf->interlaced);

   box->x = region->x < 0 ? -x : x;
   box->y = region->y < 0 ? -y : y;
   box->width = width;
   box->height = height;
}

static VAStatus vlVaPostProcBlit(vlVaDriver *drv, vlVaContext *context,
                                 const VARectangle *src_region,
                                 const VARectangle *dst_region,
                                 struct pipe_video_buffer *src,
                                 struct pipe_video_buffer *dst,
                                 enum vl_compositor_deinterlace deinterlace)
{
   struct pipe_surface **src_surfaces;
   struct pipe_surface **dst_surfaces;
   struct u_rect src_rect;
   struct u_rect dst_rect;
   bool scale = false;
   bool grab = false;
   unsigned i;

   if ((src->buffer_format == PIPE_FORMAT_B8G8R8A8_UNORM ||
        src->buffer_format == PIPE_FORMAT_B8G8R8X8_UNORM) &&
       !src->interlaced)
      grab = true;

   if ((src->width != dst->width || src->height != dst->height) &&
       (src->interlaced && dst->interlaced))
      scale = true;

   src_surfaces = src->get_surfaces(src);
   if (!src_surfaces || !src_surfaces[0])
      return VA_STATUS_ERROR_INVALID_SURFACE;

   if (scale || (src->interlaced != dst->interlaced && dst->interlaced)) {
      vlVaSurface *surf;

      surf = handle_table_get(drv->htab, context->target_id);
      surf->templat.interlaced = false;
      dst->destroy(dst);

      if (vlVaHandleSurfaceAllocate(drv, surf, &surf->templat) != VA_STATUS_SUCCESS)
         return VA_STATUS_ERROR_ALLOCATION_FAILED;

      dst = context->target = surf->buffer;
   }

   dst_surfaces = dst->get_surfaces(dst);
   if (!dst_surfaces || !dst_surfaces[0])
      return VA_STATUS_ERROR_INVALID_SURFACE;

   src_rect.x0 = src_region->x;
   src_rect.y0 = src_region->y;
   src_rect.x1 = src_region->x + src_region->width;
   src_rect.y1 = src_region->y + src_region->height;

   dst_rect.x0 = dst_region->x;
   dst_rect.y0 = dst_region->y;
   dst_rect.x1 = dst_region->x + dst_region->width;
   dst_rect.y1 = dst_region->y + dst_region->height;

   if (grab) {
      vl_compositor_convert_rgb_to_yuv(&drv->cstate, &drv->compositor, 0,
                                       ((struct vl_video_buffer *)src)->resources[0],
                                       dst, &src_rect, &dst_rect);

      return VA_STATUS_SUCCESS;
   }

   if (src->interlaced != dst->interlaced) {
      vl_compositor_yuv_deint_full(&drv->cstate, &drv->compositor,
                                   src, dst, &src_rect, &dst_rect,
                                   deinterlace);

      return VA_STATUS_SUCCESS;
   }

   for (i = 0; i < VL_MAX_SURFACES; ++i) {
      struct pipe_surface *from = src_surfaces[i];
      struct pipe_blit_info blit;

      if (src->interlaced) {
         /* Not 100% accurate, but close enough */
         switch (deinterlace) {
         case VL_COMPOSITOR_BOB_TOP:
            from = src_surfaces[i & ~1];
            break;
         case VL_COMPOSITOR_BOB_BOTTOM:
            from = src_surfaces[(i & ~1) + 1];
            break;
         default:
            break;
         }
      }

      if (!from || !dst_surfaces[i])
         continue;

      memset(&blit, 0, sizeof(blit));
      blit.src.resource = from->texture;
      blit.src.format = from->format;
      blit.src.level = 0;
      blit.src.box.z = from->u.tex.first_layer;
      blit.src.box.depth = 1;
      vlVaGetBox(src, i, &blit.src.box, src_region);

      blit.dst.resource = dst_surfaces[i]->texture;
      blit.dst.format = dst_surfaces[i]->format;
      blit.dst.level = 0;
      blit.dst.box.z = dst_surfaces[i]->u.tex.first_layer;
      blit.dst.box.depth = 1;
      vlVaGetBox(dst, i, &blit.dst.box, dst_region);

      blit.mask = PIPE_MASK_RGBA;
      blit.filter = PIPE_TEX_MIPFILTER_LINEAR;

      if (drv->pipe->screen->get_param(drv->pipe->screen,
                                       PIPE_CAP_PREFER_COMPUTE_FOR_MULTIMEDIA))
         util_compute_blit(drv->pipe, &blit, &context->blit_cs);
      else
         drv->pipe->blit(drv->pipe, &blit);
   }

   // TODO: figure out why this is necessary for DMA-buf sharing
   drv->pipe->flush(drv->pipe, NULL, 0);

   return VA_STATUS_SUCCESS;
}

static struct pipe_video_buffer *
vlVaApplyDeint(vlVaDriver *drv, vlVaContext *context,
               VAProcPipelineParameterBuffer *param,
               struct pipe_video_buffer *current,
               unsigned field)
{
   vlVaSurface *prevprev, *prev, *next;

   if (param->num_forward_references < 2 ||
       param->num_backward_references < 1)
      return current;

   prevprev = handle_table_get(drv->htab, param->forward_references[1]);
   prev = handle_table_get(drv->htab, param->forward_references[0]);
   next = handle_table_get(drv->htab, param->backward_references[0]);

   if (!prevprev || !prev || !next)
      return current;

   if (context->deint && (context->deint->video_width != current->width ||
       context->deint->video_height != current->height)) {
      vl_deint_filter_cleanup(context->deint);
      FREE(context->deint);
      context->deint = NULL;
   }

   if (!context->deint) {
      context->deint = MALLOC(sizeof(struct vl_deint_filter));
      if (!vl_deint_filter_init(context->deint, drv->pipe, current->width,
                                current->height, false, false)) {
         FREE(context->deint);
         context->deint = NULL;
         return current;
      }
   }

   if (!vl_deint_filter_check_buffers(context->deint, prevprev->buffer,
                                      prev->buffer, current, next->buffer))
      return current;

   vl_deint_filter_render(context->deint, prevprev->buffer, prev->buffer,
                          current, next->buffer, field);
   return context->deint->video_buffer;
}

VAStatus
vlVaHandleVAProcPipelineParameterBufferType(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   enum vl_compositor_deinterlace deinterlace = VL_COMPOSITOR_WEAVE;
   VARectangle def_src_region, def_dst_region;
   const VARectangle *src_region, *dst_region;
   VAProcPipelineParameterBuffer *param;
   struct pipe_video_buffer *src;
   vlVaSurface *src_surface, *dst_surface;
   unsigned i;

   if (!drv || !context)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!buf || !buf->data)
      return VA_STATUS_ERROR_INVALID_BUFFER;

   if (!context->target)
      return VA_STATUS_ERROR_INVALID_SURFACE;

   param = buf->data;

   src_surface = handle_table_get(drv->htab, param->surface);
   dst_surface = handle_table_get(drv->htab, context->target_id);

   if (!src_surface || !src_surface->buffer)
      return VA_STATUS_ERROR_INVALID_SURFACE;

   src = src_surface->buffer;

   for (i = 0; i < param->num_filters; i++) {
      vlVaBuffer *buf = handle_table_get(drv->htab, param->filters[i]);
      VAProcFilterParameterBufferBase *filter;

      if (!buf || buf->type != VAProcFilterParameterBufferType)
         return VA_STATUS_ERROR_INVALID_BUFFER;

      filter = buf->data;
      switch (filter->type) {
      case VAProcFilterDeinterlacing: {
         VAProcFilterParameterBufferDeinterlacing *deint = buf->data;
         switch (deint->algorithm) {
         case VAProcDeinterlacingBob:
            if (deint->flags & VA_DEINTERLACING_BOTTOM_FIELD)
               deinterlace = VL_COMPOSITOR_BOB_BOTTOM;
            else
               deinterlace = VL_COMPOSITOR_BOB_TOP;
            break;

         case VAProcDeinterlacingWeave:
            deinterlace = VL_COMPOSITOR_WEAVE;
            break;

         case VAProcDeinterlacingMotionAdaptive:
            src = vlVaApplyDeint(drv, context, param, src,
				 !!(deint->flags & VA_DEINTERLACING_BOTTOM_FIELD));
            break;

         default:
            return VA_STATUS_ERROR_UNIMPLEMENTED;
         }

         break;
      }

      default:
         return VA_STATUS_ERROR_UNIMPLEMENTED;
      }
   }

   src_region = vlVaRegionDefault(param->surface_region, src_surface, &def_src_region);
   dst_region = vlVaRegionDefault(param->output_region, dst_surface, &def_dst_region);

   if (context->target->buffer_format != PIPE_FORMAT_NV12 &&
       context->target->buffer_format != PIPE_FORMAT_P010 &&
       context->target->buffer_format != PIPE_FORMAT_P016)
      return vlVaPostProcCompositor(drv, context, src_region, dst_region,
                                    src, context->target, deinterlace);
   else
      return vlVaPostProcBlit(drv, context, src_region, dst_region,
                              src, context->target, deinterlace);
}
