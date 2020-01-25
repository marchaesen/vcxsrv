/**************************************************************************
 *
 * Copyright 2019 Sonny Jiang <sonnyj608@gmail.com>
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "u_bitcast.h"
#include "util/format/u_format.h"
#include "u_sampler.h"
#include "tgsi/tgsi_text.h"
#include "tgsi/tgsi_ureg.h"
#include "u_inlines.h"
#include "u_compute.h"

static void *blit_compute_shader(struct pipe_context *ctx)
{
   static const char text[] =
      "COMP\n"
      "PROPERTY CS_FIXED_BLOCK_WIDTH 64\n"
      "PROPERTY CS_FIXED_BLOCK_HEIGHT 1\n"
      "PROPERTY CS_FIXED_BLOCK_DEPTH 1\n"
      "DCL SV[0], THREAD_ID\n"
      "DCL SV[1], BLOCK_ID\n"
      "DCL IMAGE[0], 2D_ARRAY, PIPE_FORMAT_R32G32B32A32_FLOAT, WR\n"
      "DCL SAMP[0]\n"
      "DCL SVIEW[0], 2D_ARRAY, FLOAT\n"
      "DCL CONST[0][0..2]\n" // 0:xyzw 1:xyzw
      "DCL TEMP[0..4], LOCAL\n"
      "IMM[0] UINT32 {64, 1, 0, 0}\n"

      "UMAD TEMP[0].xyz, SV[1].xyzz, IMM[0].xyyy, SV[0].xyzz\n"
      "U2F TEMP[1].xyz, TEMP[0]\n"
      "MAD TEMP[2].xyz, TEMP[1], CONST[0][1], CONST[0][0]\n"
      "TEX_LZ TEMP[3], TEMP[2], SAMP[0], 2D_ARRAY\n"
      "UADD TEMP[4].xyz, TEMP[0], CONST[0][2]\n"
      "STORE IMAGE[0], TEMP[4], TEMP[3], 2D_ARRAY, PIPE_FORMAT_R32G32B32A32_FLOAT\n"
      "END\n";

   struct tgsi_token tokens[1024];
   struct pipe_compute_state state = {0};

   if (!tgsi_text_translate(text, tokens, ARRAY_SIZE(tokens))) {
      assert(false);
      return NULL;
   }

   state.ir_type = PIPE_SHADER_IR_TGSI;
   state.prog = tokens;

   return ctx->create_compute_state(ctx, &state);
}

void util_compute_blit(struct pipe_context *ctx, struct pipe_blit_info *blit_info,
                       void **compute_state)
{
   if (blit_info->src.box.width == 0 || blit_info->src.box.height == 0 ||
       blit_info->dst.box.width == 0 || blit_info->dst.box.height == 0)
     return;

   struct pipe_resource *src = blit_info->src.resource;
   struct pipe_resource *dst = blit_info->dst.resource;
   struct pipe_sampler_view src_templ = {0}, *src_view;
   void *sampler_state_p;
   unsigned width = blit_info->dst.box.width;
   unsigned height = blit_info->dst.box.height;
   float x_scale = blit_info->src.box.width / (float)blit_info->dst.box.width;
   float y_scale = blit_info->src.box.height / (float)blit_info->dst.box.height;
   float z_scale = blit_info->src.box.depth / (float)blit_info->dst.box.depth;

   unsigned data[] = {u_bitcast_f2u((blit_info->src.box.x + 0.5) / (float)src->width0),
                      u_bitcast_f2u((blit_info->src.box.y + 0.5) / (float)src->height0),
                      u_bitcast_f2u(blit_info->src.box.z),
                      u_bitcast_f2u(0),
                      u_bitcast_f2u(x_scale / src->width0),
                      u_bitcast_f2u(y_scale / src->height0),
                      u_bitcast_f2u(z_scale),
                      u_bitcast_f2u(0),
                      blit_info->dst.box.x,
                      blit_info->dst.box.y,
                      blit_info->dst.box.z,
                      0};

   struct pipe_constant_buffer cb = {0};
   cb.buffer_size = sizeof(data);
   cb.user_buffer = data;
   ctx->set_constant_buffer(ctx, PIPE_SHADER_COMPUTE, 0, &cb);

   struct pipe_image_view image = {0};
   image.resource = dst;
   image.shader_access = image.access = PIPE_IMAGE_ACCESS_WRITE;
   image.format = util_format_linear(blit_info->dst.format);
   image.u.tex.level = blit_info->dst.level;
   image.u.tex.first_layer = 0;
   image.u.tex.last_layer = (unsigned)(dst->array_size - 1);

   ctx->set_shader_images(ctx, PIPE_SHADER_COMPUTE, 0, 1, &image);

   struct pipe_sampler_state sampler_state={0};
   sampler_state.wrap_s = PIPE_TEX_WRAP_CLAMP_TO_EDGE;
   sampler_state.wrap_t = PIPE_TEX_WRAP_CLAMP_TO_EDGE;
   sampler_state.wrap_r = PIPE_TEX_WRAP_CLAMP_TO_EDGE;
   sampler_state.normalized_coords = 1;

   if (blit_info->filter == PIPE_TEX_FILTER_LINEAR) {
      sampler_state.min_img_filter = PIPE_TEX_FILTER_LINEAR;
      sampler_state.mag_img_filter = PIPE_TEX_FILTER_LINEAR;
   }

   sampler_state_p = ctx->create_sampler_state(ctx, &sampler_state);
   ctx->bind_sampler_states(ctx, PIPE_SHADER_COMPUTE, 0, 1, &sampler_state_p);

   /* Initialize the sampler view. */
   u_sampler_view_default_template(&src_templ, src, src->format);
   src_templ.format = util_format_linear(blit_info->src.format);
   src_view = ctx->create_sampler_view(ctx, src, &src_templ);
   ctx->set_sampler_views(ctx, PIPE_SHADER_COMPUTE, 0, 1, &src_view);

   if (!*compute_state)
     *compute_state = blit_compute_shader(ctx);
   ctx->bind_compute_state(ctx, *compute_state);

   struct pipe_grid_info grid_info = {0};
   grid_info.block[0] = 64;
   grid_info.last_block[0] = width % 64;
   grid_info.block[1] = 1;
   grid_info.block[2] = 1;
   grid_info.grid[0] = DIV_ROUND_UP(width, 64);
   grid_info.grid[1] = height;
   grid_info.grid[2] = 1;

   ctx->launch_grid(ctx, &grid_info);

   ctx->memory_barrier(ctx, PIPE_BARRIER_ALL);

   ctx->set_shader_images(ctx, PIPE_SHADER_COMPUTE, 0, 1, NULL);
   ctx->set_constant_buffer(ctx, PIPE_SHADER_COMPUTE, 0, NULL);
   ctx->set_sampler_views(ctx, PIPE_SHADER_COMPUTE, 0, 1, NULL);
   pipe_sampler_view_reference(&src_view, NULL);
   ctx->delete_sampler_state(ctx, sampler_state_p);
   ctx->bind_compute_state(ctx, NULL);
}
