#include "zink_context.h"
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
blit_resolve(struct zink_context *ctx, const struct pipe_blit_info *info)
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
   if (src->format != zink_get_format(screen, info->src.format) ||
       dst->format != zink_get_format(screen, info->dst.format))
      return false;
   if (info->dst.resource->target == PIPE_BUFFER)
      util_range_add(info->dst.resource, &dst->valid_buffer_range,
                     info->dst.box.x, info->dst.box.x + info->dst.box.width);

   apply_dst_clears(ctx, info, false);
   zink_fb_clears_apply_region(ctx, info->src.resource, zink_rect_from_box(&info->src.box));

   struct zink_batch *batch = &ctx->batch;
   zink_batch_no_rp(ctx);
   zink_batch_reference_resource_rw(batch, src, false);
   zink_batch_reference_resource_rw(batch, dst, true);

   zink_resource_setup_transfer_layouts(ctx, src, dst);

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
   VKCTX(CmdResolveImage)(batch->state->cmdbuf, src->obj->image, src->layout,
                     dst->obj->image, dst->layout,
                     1, &region);

   return true;
}

static VkFormatFeatureFlags
get_resource_features(struct zink_screen *screen, struct zink_resource *res)
{
   VkFormatProperties props = screen->format_props[res->base.b.format];
   return res->optimal_tiling ? props.optimalTilingFeatures :
                                props.linearTilingFeatures;
}

static bool
blit_native(struct zink_context *ctx, const struct pipe_blit_info *info)
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

   if (!(get_resource_features(screen, src) & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
       !(get_resource_features(screen, dst) & VK_FORMAT_FEATURE_BLIT_DST_BIT))
      return false;

   if ((util_format_is_pure_sint(info->src.format) !=
        util_format_is_pure_sint(info->dst.format)) ||
       (util_format_is_pure_uint(info->src.format) !=
        util_format_is_pure_uint(info->dst.format)))
      return false;

   if (info->filter == PIPE_TEX_FILTER_LINEAR &&
       !(get_resource_features(screen, src) &
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
      return false;

   apply_dst_clears(ctx, info, false);
   zink_fb_clears_apply_region(ctx, info->src.resource, zink_rect_from_box(&info->src.box));

   struct zink_batch *batch = &ctx->batch;
   zink_batch_no_rp(ctx);
   zink_batch_reference_resource_rw(batch, src, false);
   zink_batch_reference_resource_rw(batch, dst, true);

   zink_resource_setup_transfer_layouts(ctx, src, dst);
   if (info->dst.resource->target == PIPE_BUFFER)
      util_range_add(info->dst.resource, &dst->valid_buffer_range,
                     info->dst.box.x, info->dst.box.x + info->dst.box.width);
   VkImageBlit region = {0};
   region.srcSubresource.aspectMask = src->aspect;
   region.srcSubresource.mipLevel = info->src.level;
   region.srcOffsets[0].x = info->src.box.x;
   region.srcOffsets[0].y = info->src.box.y;
   region.srcOffsets[1].x = info->src.box.x + info->src.box.width;
   region.srcOffsets[1].y = info->src.box.y + info->src.box.height;

   enum pipe_texture_target src_target = src->base.b.target;
   if (src->need_2D_zs)
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
   if (dst->need_2D_zs)
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

   VKCTX(CmdBlitImage)(batch->state->cmdbuf, src->obj->image, src->layout,
                  dst->obj->image, dst->layout,
                  1, &region,
                  zink_filter(info->filter));

   return true;
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

   if (src_desc == dst_desc ||
       src_desc->nr_channels != 4 || src_desc->layout != UTIL_FORMAT_LAYOUT_PLAIN ||
       (src_desc->nr_channels == 4 && src_desc->channel[3].type != UTIL_FORMAT_TYPE_VOID)) {
      /* we can't blit RGBX -> RGBA formats directly since they're emulated
       * so we have to use sampler views
       */
      if (info->src.resource->nr_samples > 1 &&
          info->dst.resource->nr_samples <= 1) {
         if (blit_resolve(ctx, info))
            return;
      } else {
         if (blit_native(ctx, info))
            return;
      }
   }

   struct zink_resource *src = zink_resource(info->src.resource);
   struct zink_resource *dst = zink_resource(info->dst.resource);
   /* if we're copying between resources with matching aspects then we can probably just copy_region */
   if (src->aspect == dst->aspect) {
      struct pipe_blit_info new_info = *info;

      if (src->aspect & VK_IMAGE_ASPECT_STENCIL_BIT &&
          new_info.render_condition_enable &&
          !ctx->render_condition_active)
         new_info.render_condition_enable = false;

      if (util_try_blit_via_copy_region(pctx, &new_info, ctx->render_condition_active))
         return;
   }

   if (!util_blitter_is_blit_supported(ctx->blitter, info)) {
      debug_printf("blit unsupported %s -> %s\n",
              util_format_short_name(info->src.resource->format),
              util_format_short_name(info->dst.resource->format));
      return;
   }

   /* this is discard_only because we're about to start a renderpass that will
    * flush all pending clears anyway
    */
   apply_dst_clears(ctx, info, true);

   if (info->dst.resource->target == PIPE_BUFFER)
      util_range_add(info->dst.resource, &dst->valid_buffer_range,
                     info->dst.box.x, info->dst.box.x + info->dst.box.width);
   zink_blit_begin(ctx, ZINK_BLIT_SAVE_FB | ZINK_BLIT_SAVE_FS | ZINK_BLIT_SAVE_TEXTURES);

   util_blitter_blit(ctx->blitter, info);
}

/* similar to radeonsi */
void
zink_blit_begin(struct zink_context *ctx, enum zink_blit_flags flags)
{
   util_blitter_save_vertex_elements(ctx->blitter, ctx->element_state);
   util_blitter_save_viewport(ctx->blitter, ctx->vp_state.viewport_states);

   util_blitter_save_vertex_buffer_slot(ctx->blitter, ctx->vertex_buffers);
   util_blitter_save_vertex_shader(ctx->blitter, ctx->gfx_stages[PIPE_SHADER_VERTEX]);
   util_blitter_save_tessctrl_shader(ctx->blitter, ctx->gfx_stages[PIPE_SHADER_TESS_CTRL]);
   util_blitter_save_tesseval_shader(ctx->blitter, ctx->gfx_stages[PIPE_SHADER_TESS_EVAL]);
   util_blitter_save_geometry_shader(ctx->blitter, ctx->gfx_stages[PIPE_SHADER_GEOMETRY]);
   util_blitter_save_rasterizer(ctx->blitter, ctx->rast_state);
   util_blitter_save_so_targets(ctx->blitter, ctx->num_so_targets, ctx->so_targets);

   if (flags & ZINK_BLIT_SAVE_FS) {
      util_blitter_save_fragment_constant_buffer_slot(ctx->blitter, ctx->ubos[PIPE_SHADER_FRAGMENT]);
      util_blitter_save_blend(ctx->blitter, ctx->gfx_pipeline_state.blend_state);
      util_blitter_save_depth_stencil_alpha(ctx->blitter, ctx->dsa_state);
      util_blitter_save_stencil_ref(ctx->blitter, &ctx->stencil_ref);
      util_blitter_save_sample_mask(ctx->blitter, ctx->gfx_pipeline_state.sample_mask, 0);
      util_blitter_save_scissor(ctx->blitter, ctx->vp_state.scissor_states);
      /* also util_blitter_save_window_rectangles when we have that? */

      util_blitter_save_fragment_shader(ctx->blitter, ctx->gfx_stages[PIPE_SHADER_FRAGMENT]);
   }

   if (flags & ZINK_BLIT_SAVE_FB)
      util_blitter_save_framebuffer(ctx->blitter, &ctx->fb_state);


   if (flags & ZINK_BLIT_SAVE_TEXTURES) {
      util_blitter_save_fragment_sampler_states(ctx->blitter,
                                                ctx->di.num_samplers[PIPE_SHADER_FRAGMENT],
                                                (void**)ctx->sampler_states[PIPE_SHADER_FRAGMENT]);
      util_blitter_save_fragment_sampler_views(ctx->blitter,
                                               ctx->di.num_sampler_views[PIPE_SHADER_FRAGMENT],
                                               ctx->sampler_views[PIPE_SHADER_FRAGMENT]);
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
