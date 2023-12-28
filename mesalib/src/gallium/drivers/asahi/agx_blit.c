/*
 * Copyright 2021 Alyssa Rosenzweig
 * Copyright 2020-2021 Collabora, Ltd.
 * Copyright 2014 Broadcom
 * SPDX-License-Identifier: MIT
 */

#include "asahi/compiler/agx_compile.h"
#include "compiler/nir/nir_builder.h"
#include "gallium/auxiliary/util/u_blitter.h"
#include "gallium/auxiliary/util/u_dump.h"
#include "agx_state.h"

void
agx_blitter_save(struct agx_context *ctx, struct blitter_context *blitter,
                 bool render_cond)
{
   util_blitter_save_vertex_buffer_slot(blitter, ctx->vertex_buffers);
   util_blitter_save_vertex_elements(blitter, ctx->attributes);
   util_blitter_save_vertex_shader(blitter,
                                   ctx->stage[PIPE_SHADER_VERTEX].shader);
   util_blitter_save_geometry_shader(blitter,
                                     ctx->stage[PIPE_SHADER_GEOMETRY].shader);
   util_blitter_save_rasterizer(blitter, ctx->rast);
   util_blitter_save_viewport(blitter, &ctx->viewport[0]);
   util_blitter_save_scissor(blitter, &ctx->scissor[0]);
   util_blitter_save_fragment_shader(blitter,
                                     ctx->stage[PIPE_SHADER_FRAGMENT].shader);
   util_blitter_save_blend(blitter, ctx->blend);
   util_blitter_save_depth_stencil_alpha(blitter, ctx->zs);
   util_blitter_save_stencil_ref(blitter, &ctx->stencil_ref);
   util_blitter_save_so_targets(blitter, ctx->streamout.num_targets,
                                ctx->streamout.targets);
   util_blitter_save_sample_mask(blitter, ctx->sample_mask, 0);

   util_blitter_save_framebuffer(blitter, &ctx->framebuffer);
   util_blitter_save_fragment_sampler_states(
      blitter, ctx->stage[PIPE_SHADER_FRAGMENT].sampler_count,
      (void **)(ctx->stage[PIPE_SHADER_FRAGMENT].samplers));
   util_blitter_save_fragment_sampler_views(
      blitter, ctx->stage[PIPE_SHADER_FRAGMENT].texture_count,
      (struct pipe_sampler_view **)ctx->stage[PIPE_SHADER_FRAGMENT].textures);
   util_blitter_save_fragment_constant_buffer_slot(
      blitter, ctx->stage[PIPE_SHADER_FRAGMENT].cb);

   if (!render_cond) {
      util_blitter_save_render_condition(blitter,
                                         (struct pipe_query *)ctx->cond_query,
                                         ctx->cond_cond, ctx->cond_mode);
   }
}

void
agx_blit(struct pipe_context *pipe, const struct pipe_blit_info *info)
{
   struct agx_context *ctx = agx_context(pipe);

   if (info->render_condition_enable && !agx_render_condition_check(ctx))
      return;

   if (!util_blitter_is_blit_supported(ctx->blitter, info)) {
      fprintf(stderr, "\n");
      util_dump_blit_info(stderr, info);
      fprintf(stderr, "\n\n");
      unreachable("Unsupported blit");
   }

   /* Handle self-blits */
   agx_flush_writer(ctx, agx_resource(info->dst.resource), "Blit");

   /* Legalize compression /before/ calling into u_blitter to avoid recursion.
    * u_blitter bans recursive usage.
    */
   agx_legalize_compression(ctx, agx_resource(info->dst.resource),
                            info->dst.format);

   agx_legalize_compression(ctx, agx_resource(info->src.resource),
                            info->src.format);

   agx_blitter_save(ctx, ctx->blitter, info->render_condition_enable);
   util_blitter_blit(ctx->blitter, info);
}
