#include "zink_context.h"
#include "zink_helpers.h"
#include "zink_resource.h"
#include "zink_screen.h"

#include "util/u_blitter.h"
#include "util/u_surface.h"
#include "util/format/u_format.h"

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

   struct zink_batch *batch = zink_batch_no_rp(ctx);

   zink_batch_reference_resource_rw(batch, src, false);
   zink_batch_reference_resource_rw(batch, dst, true);

   zink_resource_setup_transfer_layouts(batch, src, dst);

   VkImageResolve region = {};

   region.srcSubresource.aspectMask = src->aspect;
   region.srcSubresource.mipLevel = info->src.level;
   region.srcOffset.x = info->src.box.x;
   region.srcOffset.y = info->src.box.y;

   if (src->base.array_size > 1) {
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

   if (dst->base.array_size > 1) {
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
   vkCmdResolveImage(batch->cmdbuf, src->image, src->layout,
                     dst->image, dst->layout,
                     1, &region);

   return true;
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

   struct zink_batch *batch = zink_batch_no_rp(ctx);
   zink_batch_reference_resource_rw(batch, src, false);
   zink_batch_reference_resource_rw(batch, dst, true);

   zink_resource_setup_transfer_layouts(batch, src, dst);

   VkImageBlit region = {};
   region.srcSubresource.aspectMask = src->aspect;
   region.srcSubresource.mipLevel = info->src.level;
   region.srcOffsets[0].x = info->src.box.x;
   region.srcOffsets[0].y = info->src.box.y;
   region.srcOffsets[1].x = info->src.box.x + info->src.box.width;
   region.srcOffsets[1].y = info->src.box.y + info->src.box.height;

   if (src->base.array_size > 1) {
      region.srcOffsets[0].z = 0;
      region.srcOffsets[1].z = 1;
      region.srcSubresource.baseArrayLayer = info->src.box.z;
      region.srcSubresource.layerCount = info->src.box.depth;
   } else {
      region.srcOffsets[0].z = info->src.box.z;
      region.srcOffsets[1].z = info->src.box.z + info->src.box.depth;
      region.srcSubresource.baseArrayLayer = 0;
      region.srcSubresource.layerCount = 1;
   }

   region.dstSubresource.aspectMask = dst->aspect;
   region.dstSubresource.mipLevel = info->dst.level;
   region.dstOffsets[0].x = info->dst.box.x;
   region.dstOffsets[0].y = info->dst.box.y;
   region.dstOffsets[1].x = info->dst.box.x + info->dst.box.width;
   region.dstOffsets[1].y = info->dst.box.y + info->dst.box.height;

   if (dst->base.array_size > 1) {
      region.dstOffsets[0].z = 0;
      region.dstOffsets[1].z = 1;
      region.dstSubresource.baseArrayLayer = info->dst.box.z;
      region.dstSubresource.layerCount = info->dst.box.depth;
   } else {
      region.dstOffsets[0].z = info->dst.box.z;
      region.dstOffsets[1].z = info->dst.box.z + info->dst.box.depth;
      region.dstSubresource.baseArrayLayer = 0;
      region.dstSubresource.layerCount = 1;
   }

   vkCmdBlitImage(batch->cmdbuf, src->image, src->layout,
                  dst->image, dst->layout,
                  1, &region,
                  zink_filter(info->filter));

   return true;
}

void
zink_blit(struct pipe_context *pctx,
          const struct pipe_blit_info *info)
{
   struct zink_context *ctx = zink_context(pctx);
   if (info->src.resource->nr_samples > 1 &&
       info->dst.resource->nr_samples <= 1) {
      if (blit_resolve(ctx, info))
         return;
   } else {
      if (blit_native(ctx, info))
         return;
   }

   struct zink_resource *src = zink_resource(info->src.resource);
   struct zink_resource *dst = zink_resource(info->dst.resource);
   /* if we're copying between resources with matching aspects then we can probably just copy_region */
   if (src->aspect == dst->aspect && util_try_blit_via_copy_region(pctx, info))
      return;

   if (!util_blitter_is_blit_supported(ctx->blitter, info)) {
      debug_printf("blit unsupported %s -> %s\n",
              util_format_short_name(info->src.resource->format),
              util_format_short_name(info->dst.resource->format));
      return;
   }

   util_blitter_save_blend(ctx->blitter, ctx->gfx_pipeline_state.blend_state);
   util_blitter_save_depth_stencil_alpha(ctx->blitter, ctx->dsa_state);
   util_blitter_save_vertex_elements(ctx->blitter, ctx->element_state);
   util_blitter_save_stencil_ref(ctx->blitter, &ctx->stencil_ref);
   util_blitter_save_rasterizer(ctx->blitter, ctx->rast_state);
   util_blitter_save_fragment_shader(ctx->blitter, ctx->gfx_stages[PIPE_SHADER_FRAGMENT]);
   util_blitter_save_vertex_shader(ctx->blitter, ctx->gfx_stages[PIPE_SHADER_VERTEX]);
   util_blitter_save_tessctrl_shader(ctx->blitter, ctx->gfx_stages[PIPE_SHADER_TESS_CTRL]);
   util_blitter_save_tesseval_shader(ctx->blitter, ctx->gfx_stages[PIPE_SHADER_TESS_EVAL]);
   util_blitter_save_geometry_shader(ctx->blitter, ctx->gfx_stages[PIPE_SHADER_GEOMETRY]);
   util_blitter_save_framebuffer(ctx->blitter, &ctx->fb_state);
   util_blitter_save_viewport(ctx->blitter, ctx->viewport_states);
   util_blitter_save_scissor(ctx->blitter, ctx->scissor_states);
   util_blitter_save_fragment_sampler_states(ctx->blitter,
                                             ctx->num_samplers[PIPE_SHADER_FRAGMENT],
                                             ctx->sampler_states[PIPE_SHADER_FRAGMENT]);
   util_blitter_save_fragment_sampler_views(ctx->blitter,
                                            ctx->num_image_views[PIPE_SHADER_FRAGMENT],
                                            ctx->image_views[PIPE_SHADER_FRAGMENT]);
   util_blitter_save_fragment_constant_buffer_slot(ctx->blitter, ctx->ubos[PIPE_SHADER_FRAGMENT]);
   util_blitter_save_vertex_buffer_slot(ctx->blitter, ctx->buffers);
   util_blitter_save_sample_mask(ctx->blitter, ctx->gfx_pipeline_state.sample_mask);
   util_blitter_save_so_targets(ctx->blitter, ctx->num_so_targets, ctx->so_targets);

   util_blitter_blit(ctx->blitter, info);
}
