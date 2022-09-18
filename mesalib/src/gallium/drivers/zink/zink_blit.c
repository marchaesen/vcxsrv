#include "zink_clear.h"
#include "zink_context.h"
#include "zink_format.h"
#include "zink_kopper.h"
#include "zink_helpers.h"
#include "zink_query.h"
#include "zink_resource.h"
#include "zink_screen.h"

#include "util/u_blitter.h"
#include "util/u_rect.h"
#include "util/u_surface.h"
#include "util/format/u_format.h"

static void
apply_dst_clears(struct zink_context *ctx, const struct pipe_blit_info *info, bool discard_only)
{
   if (info->scissor_enable) {
      struct u_rect rect = { info->scissor.minx, info->scissor.maxx,
                             info->scissor.miny, info->scissor.maxy };
      zink_fb_clears_apply_or_discard(ctx, info->dst.resource, rect, discard_only);
   } else
      zink_fb_clears_apply_or_discard(ctx, info->dst.resource, zink_rect_from_box(&info->dst.box), discard_only);
}

static bool
blit_resolve(struct zink_context *ctx, const struct pipe_blit_info *info, bool *needs_present_readback)
{
   if (util_format_get_mask(info->dst.format) != info->mask ||
       util_format_get_mask(info->src.format) != info->mask ||
       util_format_is_depth_or_stencil(info->dst.format) ||
       info->scissor_enable ||
       info->alpha_blend)
      return false;

   if (info->src.box.width != info->dst.box.width ||
       info->src.box.height != info->dst.box.height ||
       info->src.box.depth != info->dst.box.depth)
      return false;

   if (info->render_condition_enable &&
       ctx->render_condition_active)
      return false;

   struct zink_resource *src = zink_resource(info->src.resource);
   struct zink_resource *dst = zink_resource(info->dst.resource);

   struct zink_screen *screen = zink_screen(ctx->base.screen);
   /* aliased/swizzled formats need u_blitter */
   if (src->format != zink_get_format(screen, info->src.format) ||
       dst->format != zink_get_format(screen, info->dst.format))
      return false;
   if (src->format != dst->format)
      return false;

   apply_dst_clears(ctx, info, false);
   zink_fb_clears_apply_region(ctx, info->src.resource, zink_rect_from_box(&info->src.box));

   if (src->obj->dt)
      *needs_present_readback = zink_kopper_acquire_readback(ctx, src);

   struct zink_batch *batch = &ctx->batch;
   zink_resource_setup_transfer_layouts(ctx, src, dst);
   VkCommandBuffer cmdbuf = *needs_present_readback ?
                            ctx->batch.state->cmdbuf :
                            zink_get_cmdbuf(ctx, src, dst);
   zink_batch_reference_resource_rw(batch, src, false);
   zink_batch_reference_resource_rw(batch, dst, true);

   VkImageResolve region = {0};

   region.srcSubresource.aspectMask = src->aspect;
   region.srcSubresource.mipLevel = info->src.level;
   region.srcOffset.x = info->src.box.x;
   region.srcOffset.y = info->src.box.y;

   if (src->base.b.array_size > 1) {
      region.srcOffset.z = 0;
      region.srcSubresource.baseArrayLayer = info->src.box.z;
      region.srcSubresource.layerCount = info->src.box.depth;
   } else {
      assert(info->src.box.depth == 1);
      region.srcOffset.z = info->src.box.z;
      region.srcSubresource.baseArrayLayer = 0;
      region.srcSubresource.layerCount = 1;
   }

   region.dstSubresource.aspectMask = dst->aspect;
   region.dstSubresource.mipLevel = info->dst.level;
   region.dstOffset.x = info->dst.box.x;
   region.dstOffset.y = info->dst.box.y;

   if (dst->base.b.array_size > 1) {
      region.dstOffset.z = 0;
      region.dstSubresource.baseArrayLayer = info->dst.box.z;
      region.dstSubresource.layerCount = info->dst.box.depth;
   } else {
      assert(info->dst.box.depth == 1);
      region.dstOffset.z = info->dst.box.z;
      region.dstSubresource.baseArrayLayer = 0;
      region.dstSubresource.layerCount = 1;
   }

   region.extent.width = info->dst.box.width;
   region.extent.height = info->dst.box.height;
   region.extent.depth = info->dst.box.depth;
   VKCTX(CmdResolveImage)(cmdbuf, src->obj->image, src->layout,
                     dst->obj->image, dst->layout,
                     1, &region);

   return true;
}

static bool
blit_native(struct zink_context *ctx, const struct pipe_blit_info *info, bool *needs_present_readback)
{
   if (util_format_get_mask(info->dst.format) != info->mask ||
       util_format_get_mask(info->src.format) != info->mask ||
       info->scissor_enable ||
       info->alpha_blend)
      return false;

   if (info->render_condition_enable &&
       ctx->render_condition_active)
      return false;

   if (util_format_is_depth_or_stencil(info->dst.format) &&
       info->dst.format != info->src.format)
      return false;

   /* vkCmdBlitImage must not be used for multisampled source or destination images. */
   if (info->src.resource->nr_samples > 1 || info->dst.resource->nr_samples > 1)
      return false;

   struct zink_resource *src = zink_resource(info->src.resource);
   struct zink_resource *dst = zink_resource(info->dst.resource);

   struct zink_screen *screen = zink_screen(ctx->base.screen);
   if (src->format != zink_get_format(screen, info->src.format) ||
       dst->format != zink_get_format(screen, info->dst.format))
      return false;
   if (zink_format_is_emulated_alpha(info->src.format))
      return false;

   if (!(src->obj->vkfeats & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
       !(dst->obj->vkfeats & VK_FORMAT_FEATURE_BLIT_DST_BIT))
      return false;

   if ((util_format_is_pure_sint(info->src.format) !=
        util_format_is_pure_sint(info->dst.format)) ||
       (util_format_is_pure_uint(info->src.format) !=
        util_format_is_pure_uint(info->dst.format)))
      return false;

   if (info->filter == PIPE_TEX_FILTER_LINEAR &&
       !(src->obj->vkfeats & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
      return false;

   apply_dst_clears(ctx, info, false);
   zink_fb_clears_apply_region(ctx, info->src.resource, zink_rect_from_box(&info->src.box));

   if (src->obj->dt)
      *needs_present_readback = zink_kopper_acquire_readback(ctx, src);

   struct zink_batch *batch = &ctx->batch;
   zink_resource_setup_transfer_layouts(ctx, src, dst);
   VkCommandBuffer cmdbuf = *needs_present_readback ?
                            ctx->batch.state->cmdbuf :
                            zink_get_cmdbuf(ctx, src, dst);
   zink_batch_reference_resource_rw(batch, src, false);
   zink_batch_reference_resource_rw(batch, dst, true);

   VkImageBlit region = {0};
   region.srcSubresource.aspectMask = src->aspect;
   region.srcSubresource.mipLevel = info->src.level;
   region.srcOffsets[0].x = info->src.box.x;
   region.srcOffsets[0].y = info->src.box.y;
   region.srcOffsets[1].x = info->src.box.x + info->src.box.width;
   region.srcOffsets[1].y = info->src.box.y + info->src.box.height;

   enum pipe_texture_target src_target = src->base.b.target;
   if (src->need_2D)
      src_target = src_target == PIPE_TEXTURE_1D ? PIPE_TEXTURE_2D : PIPE_TEXTURE_2D_ARRAY;
   switch (src_target) {
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_1D_ARRAY:
      /* these use layer */
      region.srcSubresource.baseArrayLayer = info->src.box.z;
      region.srcSubresource.layerCount = info->src.box.depth;
      region.srcOffsets[0].z = 0;
      region.srcOffsets[1].z = 1;
      break;
   case PIPE_TEXTURE_3D:
      /* this uses depth */
      region.srcSubresource.baseArrayLayer = 0;
      region.srcSubresource.layerCount = 1;
      region.srcOffsets[0].z = info->src.box.z;
      region.srcOffsets[1].z = info->src.box.z + info->src.box.depth;
      break;
   default:
      /* these must only copy one layer */
      region.srcSubresource.baseArrayLayer = 0;
      region.srcSubresource.layerCount = 1;
      region.srcOffsets[0].z = 0;
      region.srcOffsets[1].z = 1;
   }

   region.dstSubresource.aspectMask = dst->aspect;
   region.dstSubresource.mipLevel = info->dst.level;
   region.dstOffsets[0].x = info->dst.box.x;
   region.dstOffsets[0].y = info->dst.box.y;
   region.dstOffsets[1].x = info->dst.box.x + info->dst.box.width;
   region.dstOffsets[1].y = info->dst.box.y + info->dst.box.height;
   assert(region.dstOffsets[0].x != region.dstOffsets[1].x);
   assert(region.dstOffsets[0].y != region.dstOffsets[1].y);

   enum pipe_texture_target dst_target = dst->base.b.target;
   if (dst->need_2D)
      dst_target = dst_target == PIPE_TEXTURE_1D ? PIPE_TEXTURE_2D : PIPE_TEXTURE_2D_ARRAY;
   switch (dst_target) {
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_1D_ARRAY:
      /* these use layer */
      region.dstSubresource.baseArrayLayer = info->dst.box.z;
      region.dstSubresource.layerCount = info->dst.box.depth;
      region.dstOffsets[0].z = 0;
      region.dstOffsets[1].z = 1;
      break;
   case PIPE_TEXTURE_3D:
      /* this uses depth */
      region.dstSubresource.baseArrayLayer = 0;
      region.dstSubresource.layerCount = 1;
      region.dstOffsets[0].z = info->dst.box.z;
      region.dstOffsets[1].z = info->dst.box.z + info->dst.box.depth;
      break;
   default:
      /* these must only copy one layer */
      region.dstSubresource.baseArrayLayer = 0;
      region.dstSubresource.layerCount = 1;
      region.dstOffsets[0].z = 0;
      region.dstOffsets[1].z = 1;
   }
   assert(region.dstOffsets[0].z != region.dstOffsets[1].z);

   VKCTX(CmdBlitImage)(cmdbuf, src->obj->image, src->layout,
                  dst->obj->image, dst->layout,
                  1, &region,
                  zink_filter(info->filter));

   return true;
}

static bool
try_copy_region(struct pipe_context *pctx, const struct pipe_blit_info *info)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_resource *src = zink_resource(info->src.resource);
   struct zink_resource *dst = zink_resource(info->dst.resource);
   /* if we're copying between resources with matching aspects then we can probably just copy_region */
   if (src->aspect != dst->aspect)
      return false;
   struct pipe_blit_info new_info = *info;

   if (src->aspect & VK_IMAGE_ASPECT_STENCIL_BIT &&
       new_info.render_condition_enable &&
       !ctx->render_condition_active)
      new_info.render_condition_enable = false;

   return util_try_blit_via_copy_region(pctx, &new_info, ctx->render_condition_active);
}

void
zink_blit(struct pipe_context *pctx,
          const struct pipe_blit_info *info)
{
   struct zink_context *ctx = zink_context(pctx);
   const struct util_format_description *src_desc = util_format_description(info->src.format);
   const struct util_format_description *dst_desc = util_format_description(info->dst.format);

   if (info->render_condition_enable &&
       unlikely(!zink_screen(pctx->screen)->info.have_EXT_conditional_rendering && !zink_check_conditional_render(ctx)))
      return;

   struct zink_resource *src = zink_resource(info->src.resource);
   struct zink_resource *dst = zink_resource(info->dst.resource);
   bool needs_present_readback = false;
   if (zink_is_swapchain(dst)) {
      if (!zink_kopper_acquire(ctx, dst, UINT64_MAX))
         return;
   }

   if (src_desc == dst_desc ||
       src_desc->nr_channels != 4 || src_desc->layout != UTIL_FORMAT_LAYOUT_PLAIN ||
       (src_desc->nr_channels == 4 && src_desc->channel[3].type != UTIL_FORMAT_TYPE_VOID)) {
      /* we can't blit RGBX -> RGBA formats directly since they're emulated
       * so we have to use sampler views
       */
      if (info->src.resource->nr_samples > 1 &&
          info->dst.resource->nr_samples <= 1) {
         if (blit_resolve(ctx, info, &needs_present_readback))
            goto end;
      } else {
         if (try_copy_region(pctx, info))
            goto end;
         if (blit_native(ctx, info, &needs_present_readback))
            goto end;
      }
   }



   bool stencil_blit = false;
   if (!util_blitter_is_blit_supported(ctx->blitter, info)) {
      if (util_format_is_depth_or_stencil(info->src.resource->format)) {
         struct pipe_blit_info depth_blit = *info;
         depth_blit.mask = PIPE_MASK_Z;
         stencil_blit = util_blitter_is_blit_supported(ctx->blitter, &depth_blit);
         if (stencil_blit) {
            zink_blit_begin(ctx, ZINK_BLIT_SAVE_FB | ZINK_BLIT_SAVE_FS | ZINK_BLIT_SAVE_TEXTURES);
            util_blitter_blit(ctx->blitter, &depth_blit);
         }
      }
      if (!stencil_blit) {
         mesa_loge("ZINK: blit unsupported %s -> %s",
                 util_format_short_name(info->src.resource->format),
                 util_format_short_name(info->dst.resource->format));
         goto end;
      }
   }

   if (src->obj->dt) {
      zink_fb_clears_apply_region(ctx, info->src.resource, zink_rect_from_box(&info->src.box));
      needs_present_readback = zink_kopper_acquire_readback(ctx, src);
   }

   /* this is discard_only because we're about to start a renderpass that will
    * flush all pending clears anyway
    */
   apply_dst_clears(ctx, info, true);

   /* this will draw a full-resource quad, so ignore existing data */
   if (util_blit_covers_whole_resource(info))
      pctx->invalidate_resource(pctx, info->dst.resource);
   zink_blit_begin(ctx, ZINK_BLIT_SAVE_FB | ZINK_BLIT_SAVE_FS | ZINK_BLIT_SAVE_TEXTURES);

   if (stencil_blit) {
      struct pipe_surface *dst_view, dst_templ;
      util_blitter_default_dst_texture(&dst_templ, info->dst.resource, info->dst.level, info->dst.box.z);
      dst_view = pctx->create_surface(pctx, info->dst.resource, &dst_templ);

      util_blitter_clear_depth_stencil(ctx->blitter, dst_view, PIPE_CLEAR_STENCIL,
                                       0, 0, info->dst.box.x, info->dst.box.y,
                                       info->dst.box.width, info->dst.box.height);
      zink_blit_begin(ctx, ZINK_BLIT_SAVE_FB | ZINK_BLIT_SAVE_FS | ZINK_BLIT_SAVE_TEXTURES);
      util_blitter_stencil_fallback(ctx->blitter,
                                    info->dst.resource,
                                    info->dst.level,
                                    &info->dst.box,
                                    info->src.resource,
                                    info->src.level,
                                    &info->src.box,
                                    info->scissor_enable ? &info->scissor : NULL);

      pipe_surface_release(pctx, &dst_view);
   } else {
      util_blitter_blit(ctx->blitter, info);
   }
end:
   if (needs_present_readback)
      zink_kopper_present_readback(ctx, src);
}

/* similar to radeonsi */
void
zink_blit_begin(struct zink_context *ctx, enum zink_blit_flags flags)
{
   util_blitter_save_vertex_elements(ctx->blitter, ctx->element_state);
   util_blitter_save_viewport(ctx->blitter, ctx->vp_state.viewport_states);

   util_blitter_save_vertex_buffer_slot(ctx->blitter, ctx->vertex_buffers);
   util_blitter_save_vertex_shader(ctx->blitter, ctx->gfx_stages[MESA_SHADER_VERTEX]);
   util_blitter_save_tessctrl_shader(ctx->blitter, ctx->gfx_stages[MESA_SHADER_TESS_CTRL]);
   util_blitter_save_tesseval_shader(ctx->blitter, ctx->gfx_stages[MESA_SHADER_TESS_EVAL]);
   util_blitter_save_geometry_shader(ctx->blitter, ctx->gfx_stages[MESA_SHADER_GEOMETRY]);
   util_blitter_save_rasterizer(ctx->blitter, ctx->rast_state);
   util_blitter_save_so_targets(ctx->blitter, ctx->num_so_targets, ctx->so_targets);

   if (flags & ZINK_BLIT_SAVE_FS) {
      util_blitter_save_fragment_constant_buffer_slot(ctx->blitter, ctx->ubos[MESA_SHADER_FRAGMENT]);
      util_blitter_save_blend(ctx->blitter, ctx->gfx_pipeline_state.blend_state);
      util_blitter_save_depth_stencil_alpha(ctx->blitter, ctx->dsa_state);
      util_blitter_save_stencil_ref(ctx->blitter, &ctx->stencil_ref);
      util_blitter_save_sample_mask(ctx->blitter, ctx->gfx_pipeline_state.sample_mask, ctx->gfx_pipeline_state.min_samples + 1);
      util_blitter_save_scissor(ctx->blitter, ctx->vp_state.scissor_states);
      /* also util_blitter_save_window_rectangles when we have that? */

      util_blitter_save_fragment_shader(ctx->blitter, ctx->gfx_stages[MESA_SHADER_FRAGMENT]);
   }

   if (flags & ZINK_BLIT_SAVE_FB)
      util_blitter_save_framebuffer(ctx->blitter, &ctx->fb_state);


   if (flags & ZINK_BLIT_SAVE_TEXTURES) {
      util_blitter_save_fragment_sampler_states(ctx->blitter,
                                                ctx->di.num_samplers[MESA_SHADER_FRAGMENT],
                                                (void**)ctx->sampler_states[MESA_SHADER_FRAGMENT]);
      util_blitter_save_fragment_sampler_views(ctx->blitter,
                                               ctx->di.num_sampler_views[MESA_SHADER_FRAGMENT],
                                               ctx->sampler_views[MESA_SHADER_FRAGMENT]);
   }

   if (flags & ZINK_BLIT_NO_COND_RENDER && ctx->render_condition_active)
      zink_stop_conditional_render(ctx);
}

bool
zink_blit_region_fills(struct u_rect region, unsigned width, unsigned height)
{
   struct u_rect intersect = {0, width, 0, height};
   struct u_rect r = {
      MIN2(region.x0, region.x1),
      MAX2(region.x0, region.x1),
      MIN2(region.y0, region.y1),
      MAX2(region.y0, region.y1),
   };

   if (!u_rect_test_intersection(&r, &intersect))
      /* is this even a thing? */
      return false;

   u_rect_find_intersection(&r, &intersect);
   if (intersect.x0 != 0 || intersect.y0 != 0 ||
       intersect.x1 != width || intersect.y1 != height)
      return false;

   return true;
}

bool
zink_blit_region_covers(struct u_rect region, struct u_rect covers)
{
   struct u_rect r = {
      MIN2(region.x0, region.x1),
      MAX2(region.x0, region.x1),
      MIN2(region.y0, region.y1),
      MAX2(region.y0, region.y1),
   };
   struct u_rect c = {
      MIN2(covers.x0, covers.x1),
      MAX2(covers.x0, covers.x1),
      MIN2(covers.y0, covers.y1),
      MAX2(covers.y0, covers.y1),
   };
   struct u_rect intersect;
   if (!u_rect_test_intersection(&r, &c))
      return false;

    u_rect_union(&intersect, &r, &c);
    return intersect.x0 == c.x0 && intersect.y0 == c.y0 &&
           intersect.x1 == c.x1 && intersect.y1 == c.y1;
}
