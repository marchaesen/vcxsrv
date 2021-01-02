/*
 * Copyright (c) 2011-2013 Luc Verhaegen <libv@skynet.be>
 * Copyright (c) 2018-2019 Lima Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "util/u_memory.h"
#include "util/u_upload_mgr.h"
#include "util/u_math.h"
#include "util/u_debug.h"
#include "util/u_transfer.h"

#include "lima_bo.h"
#include "lima_context.h"
#include "lima_screen.h"
#include "lima_texture.h"
#include "lima_resource.h"
#include "lima_job.h"
#include "lima_util.h"
#include "lima_format.h"

#include <drm-uapi/lima_drm.h>


#define lima_tex_list_size 64

static_assert(offsetof(lima_tex_desc, va) == 24, "lima_tex_desc->va offset isn't 24");


static void
lima_texture_desc_set_va(lima_tex_desc *desc,
                         int idx,
                         uint32_t va)
{
   unsigned va_bit_idx = VA_BIT_OFFSET + (VA_BIT_SIZE * idx);
   unsigned va_idx = va_bit_idx / 32;
   va_bit_idx %= 32;

   va >>= 6;

   desc->va[va_idx] |= va << va_bit_idx;
   if (va_bit_idx <= 6)
      return;
   desc->va[va_idx + 1] |= va >> (32 - va_bit_idx);
}

/*
 * Note: this function is used by both draw and flush code path,
 * make sure no lima_job_get() is called inside this.
 */
void
lima_texture_desc_set_res(struct lima_context *ctx, lima_tex_desc *desc,
                          struct pipe_resource *prsc,
                          unsigned first_level, unsigned last_level, unsigned first_layer)
{
   unsigned width, height, layout, i;
   struct lima_resource *lima_res = lima_resource(prsc);

   width = prsc->width0;
   height = prsc->height0;
   if (first_level != 0) {
      width = u_minify(width, first_level);
      height = u_minify(height, first_level);
   }

   desc->format = lima_format_get_texel(prsc->format);
   desc->swap_r_b = lima_format_get_texel_swap_rb(prsc->format);
   desc->width  = width;
   desc->height = height;
   desc->unknown_3_1 = 1;

   if (lima_res->tiled)
      layout = 3;
   else {
      /* for padded linear texture */
      if (lima_res->levels[first_level].width != width) {
         desc->stride = lima_res->levels[first_level].stride;
         desc->has_stride = 1;
      }
      layout = 0;
   }

   uint32_t base_va = lima_res->bo->va;

   /* attach first level */
   uint32_t first_va = base_va + lima_res->levels[first_level].offset + first_layer * lima_res->levels[first_level].layer_stride;
   desc->va_s.va_0 = first_va >> 6;
   desc->va_s.layout = layout;

   /* Attach remaining levels.
    * Each subsequent mipmap address is specified using the 26 msbs.
    * These addresses are then packed continuously in memory */
   for (i = 1; i <= (last_level - first_level); i++) {
      uint32_t address = base_va + lima_res->levels[first_level + i].offset;
      lima_texture_desc_set_va(desc, i, address);
   }
}

static void
lima_update_tex_desc(struct lima_context *ctx, struct lima_sampler_state *sampler,
                     struct lima_sampler_view *texture, void *pdesc,
                     unsigned desc_size)
{
   /* unit is 1/16 since lod_bias is in fixed format */
   int lod_bias_delta = 0;
   lima_tex_desc *desc = pdesc;
   unsigned first_level;
   unsigned last_level;
   unsigned first_layer;
   float max_lod;

   memset(desc, 0, desc_size);

   switch (texture->base.target) {
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_RECT:
      desc->texture_type = LIMA_TEXTURE_TYPE_2D;
      break;
   case PIPE_TEXTURE_CUBE:
      desc->texture_type = LIMA_TEXTURE_TYPE_CUBE;
      break;
   default:
      break;
   }

   if (!sampler->base.normalized_coords)
      desc->unnorm_coords = 1;

   first_level = texture->base.u.tex.first_level;
   last_level = texture->base.u.tex.last_level;
   first_layer = texture->base.u.tex.first_layer;
   if (last_level - first_level >= LIMA_MAX_MIP_LEVELS)
      last_level = first_level + LIMA_MAX_MIP_LEVELS - 1;

   desc->min_lod = lima_float_to_fixed8(sampler->base.min_lod);
   max_lod = MIN2(sampler->base.max_lod, sampler->base.min_lod +
                                         (last_level - first_level));
   desc->max_lod = lima_float_to_fixed8(max_lod);
   desc->lod_bias = lima_float_to_fixed8(sampler->base.lod_bias);

   switch (sampler->base.min_mip_filter) {
      case PIPE_TEX_MIPFILTER_LINEAR:
         desc->min_mipfilter_2 = 3;
         break;
      case PIPE_TEX_MIPFILTER_NEAREST:
         desc->min_mipfilter_2 = 0;
         break;
      case PIPE_TEX_MIPFILTER_NONE:
         desc->max_lod = desc->min_lod;
         break;
      default:
         break;
   }

   switch (sampler->base.mag_img_filter) {
   case PIPE_TEX_FILTER_LINEAR:
      desc->mag_img_filter_nearest = 0;
      break;
   case PIPE_TEX_FILTER_NEAREST:
   default:
      desc->mag_img_filter_nearest = 1;
      break;
   }

   switch (sampler->base.min_img_filter) {
      break;
   case PIPE_TEX_FILTER_LINEAR:
      desc->min_img_filter_nearest = 0;
      break;
   case PIPE_TEX_FILTER_NEAREST:
   default:
      lod_bias_delta = 8;
      desc->min_img_filter_nearest = 1;
      break;
   }

   /* Only clamp, clamp to edge, repeat and mirror repeat are supported */
   switch (sampler->base.wrap_s) {
   case PIPE_TEX_WRAP_CLAMP:
      desc->wrap_s_clamp = 1;
      break;
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
      desc->wrap_s_clamp_to_edge = 1;
      break;
   case PIPE_TEX_WRAP_MIRROR_REPEAT:
      desc->wrap_s_mirror_repeat = 1;
      break;
   case PIPE_TEX_WRAP_REPEAT:
   default:
      break;
   }

   /* Only clamp, clamp to edge, repeat and mirror repeat are supported */
   switch (sampler->base.wrap_t) {
   case PIPE_TEX_WRAP_CLAMP:
      desc->wrap_t_clamp = 1;
      break;
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
      desc->wrap_t_clamp_to_edge = 1;
      break;
   case PIPE_TEX_WRAP_MIRROR_REPEAT:
      desc->wrap_t_mirror_repeat = 1;
      break;
   case PIPE_TEX_WRAP_REPEAT:
   default:
      break;
   }

   if (desc->min_img_filter_nearest && desc->mag_img_filter_nearest &&
       desc->min_mipfilter_2 == 0 &&
       (desc->min_lod != desc->max_lod))
     lod_bias_delta = -1;

   desc->lod_bias += lod_bias_delta;

   lima_texture_desc_set_res(ctx, desc, texture->base.texture,
                             first_level, last_level, first_layer);
}

static unsigned
lima_calc_tex_desc_size(struct lima_sampler_view *texture)
{
   unsigned size = offsetof(lima_tex_desc, va);
   unsigned va_bit_size;
   unsigned first_level = texture->base.u.tex.first_level;
   unsigned last_level = texture->base.u.tex.last_level;

   if (last_level - first_level >= LIMA_MAX_MIP_LEVELS)
      last_level = first_level + LIMA_MAX_MIP_LEVELS - 1;

   va_bit_size = VA_BIT_OFFSET + VA_BIT_SIZE * (last_level - first_level + 1);
   size += (va_bit_size + 7) >> 3;
   size = align(size, lima_min_tex_desc_size);

   return size;
}

void
lima_update_textures(struct lima_context *ctx)
{
   struct lima_job *job = lima_job_get(ctx);
   struct lima_texture_stateobj *lima_tex = &ctx->tex_stateobj;

   assert (lima_tex->num_samplers <= 16);

   /* Nothing to do - we have no samplers or textures */
   if (!lima_tex->num_samplers || !lima_tex->num_textures)
      return;

   /* we always need to add texture bo to job */
   for (int i = 0; i < lima_tex->num_samplers; i++) {
      struct lima_sampler_view *texture = lima_sampler_view(lima_tex->textures[i]);
      struct lima_resource *rsc = lima_resource(texture->base.texture);
      lima_flush_previous_job_writing_resource(ctx, texture->base.texture);
      lima_job_add_bo(job, LIMA_PIPE_PP, rsc->bo, LIMA_SUBMIT_BO_READ);
   }

   /* do not regenerate texture desc if no change */
   if (!(ctx->dirty & LIMA_CONTEXT_DIRTY_TEXTURES))
      return;

   unsigned size = lima_tex_list_size;
   for (int i = 0; i < lima_tex->num_samplers; i++) {
      struct lima_sampler_view *texture = lima_sampler_view(lima_tex->textures[i]);
      size += lima_calc_tex_desc_size(texture);
   }

   uint32_t *descs =
      lima_ctx_buff_alloc(ctx, lima_ctx_buff_pp_tex_desc, size);

   off_t offset = lima_tex_list_size;
   for (int i = 0; i < lima_tex->num_samplers; i++) {
      struct lima_sampler_state *sampler = lima_sampler_state(lima_tex->samplers[i]);
      struct lima_sampler_view *texture = lima_sampler_view(lima_tex->textures[i]);
      unsigned desc_size = lima_calc_tex_desc_size(texture);

      descs[i] = lima_ctx_buff_va(ctx, lima_ctx_buff_pp_tex_desc) + offset;
      lima_update_tex_desc(ctx, sampler, texture, (void *)descs + offset, desc_size);
      offset += desc_size;
   }

   lima_dump_command_stream_print(
      job->dump, descs, size, false, "add textures_desc at va %x\n",
      lima_ctx_buff_va(ctx, lima_ctx_buff_pp_tex_desc));

   lima_dump_texture_descriptor(
      job->dump, descs, size,
      lima_ctx_buff_va(ctx, lima_ctx_buff_pp_tex_desc) + lima_tex_list_size,
      lima_tex_list_size);
}
