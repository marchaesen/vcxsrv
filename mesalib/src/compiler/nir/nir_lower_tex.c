/*
 * Copyright © 2015 Broadcom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * This lowering pass supports (as configured via nir_lower_tex_options)
 * various texture related conversions:
 *   + texture projector lowering: converts the coordinate division for
 *     texture projection to be done in ALU instructions instead of
 *     asking the texture operation to do so.
 *   + lowering RECT: converts the un-normalized RECT texture coordinates
 *     to normalized coordinates with txs plus ALU instructions
 *   + saturate s/t/r coords: to emulate certain texture clamp/wrap modes,
 *     inserts instructions to clamp specified coordinates to [0.0, 1.0].
 *     Note that this automatically triggers texture projector lowering if
 *     needed, since clamping must happen after projector lowering.
 */

#include "nir.h"
#include "nir_builder.h"

static void
project_src(nir_builder *b, nir_tex_instr *tex)
{
   /* Find the projector in the srcs list, if present. */
   int proj_index = nir_tex_instr_src_index(tex, nir_tex_src_projector);
   if (proj_index < 0)
      return;

   b->cursor = nir_before_instr(&tex->instr);

   nir_ssa_def *inv_proj =
      nir_frcp(b, nir_ssa_for_src(b, tex->src[proj_index].src, 1));

   /* Walk through the sources projecting the arguments. */
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      switch (tex->src[i].src_type) {
      case nir_tex_src_coord:
      case nir_tex_src_comparitor:
         break;
      default:
         continue;
      }
      nir_ssa_def *unprojected =
         nir_ssa_for_src(b, tex->src[i].src, nir_tex_instr_src_size(tex, i));
      nir_ssa_def *projected = nir_fmul(b, unprojected, inv_proj);

      /* Array indices don't get projected, so make an new vector with the
       * coordinate's array index untouched.
       */
      if (tex->is_array && tex->src[i].src_type == nir_tex_src_coord) {
         switch (tex->coord_components) {
         case 4:
            projected = nir_vec4(b,
                                 nir_channel(b, projected, 0),
                                 nir_channel(b, projected, 1),
                                 nir_channel(b, projected, 2),
                                 nir_channel(b, unprojected, 3));
            break;
         case 3:
            projected = nir_vec3(b,
                                 nir_channel(b, projected, 0),
                                 nir_channel(b, projected, 1),
                                 nir_channel(b, unprojected, 2));
            break;
         case 2:
            projected = nir_vec2(b,
                                 nir_channel(b, projected, 0),
                                 nir_channel(b, unprojected, 1));
            break;
         default:
            unreachable("bad texture coord count for array");
            break;
         }
      }

      nir_instr_rewrite_src(&tex->instr,
                            &tex->src[i].src,
                            nir_src_for_ssa(projected));
   }

   nir_tex_instr_remove_src(tex, proj_index);
}

static bool
lower_offset(nir_builder *b, nir_tex_instr *tex)
{
   int offset_index = nir_tex_instr_src_index(tex, nir_tex_src_offset);
   if (offset_index < 0)
      return false;

   int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   assert(coord_index >= 0);

   assert(tex->src[offset_index].src.is_ssa);
   assert(tex->src[coord_index].src.is_ssa);
   nir_ssa_def *offset = tex->src[offset_index].src.ssa;
   nir_ssa_def *coord = tex->src[coord_index].src.ssa;

   b->cursor = nir_before_instr(&tex->instr);

   nir_ssa_def *offset_coord;
   if (nir_tex_instr_src_type(tex, coord_index) == nir_type_float) {
      assert(tex->sampler_dim == GLSL_SAMPLER_DIM_RECT);
      offset_coord = nir_fadd(b, coord, nir_i2f(b, offset));
   } else {
      offset_coord = nir_iadd(b, coord, offset);
   }

   if (tex->is_array) {
      /* The offset is not applied to the array index */
      if (tex->coord_components == 2) {
         offset_coord = nir_vec2(b, nir_channel(b, offset_coord, 0),
                                    nir_channel(b, coord, 1));
      } else if (tex->coord_components == 3) {
         offset_coord = nir_vec3(b, nir_channel(b, offset_coord, 0),
                                    nir_channel(b, offset_coord, 1),
                                    nir_channel(b, coord, 2));
      } else {
         unreachable("Invalid number of components");
      }
   }

   nir_instr_rewrite_src(&tex->instr, &tex->src[coord_index].src,
                         nir_src_for_ssa(offset_coord));

   nir_tex_instr_remove_src(tex, offset_index);

   return true;
}


static nir_ssa_def *
get_texture_size(nir_builder *b, nir_tex_instr *tex)
{
   b->cursor = nir_before_instr(&tex->instr);

   /* RECT textures should not be array: */
   assert(!tex->is_array);

   nir_tex_instr *txs;

   txs = nir_tex_instr_create(b->shader, 1);
   txs->op = nir_texop_txs;
   txs->sampler_dim = GLSL_SAMPLER_DIM_RECT;
   txs->texture_index = tex->texture_index;
   txs->dest_type = nir_type_int;

   /* only single src, the lod: */
   txs->src[0].src = nir_src_for_ssa(nir_imm_int(b, 0));
   txs->src[0].src_type = nir_tex_src_lod;

   nir_ssa_dest_init(&txs->instr, &txs->dest, 2, 32, NULL);
   nir_builder_instr_insert(b, &txs->instr);

   return nir_i2f(b, &txs->dest.ssa);
}

static void
lower_rect(nir_builder *b, nir_tex_instr *tex)
{
   nir_ssa_def *txs = get_texture_size(b, tex);
   nir_ssa_def *scale = nir_frcp(b, txs);

   /* Walk through the sources normalizing the requested arguments. */
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      if (tex->src[i].src_type != nir_tex_src_coord)
         continue;

      nir_ssa_def *coords =
         nir_ssa_for_src(b, tex->src[i].src, tex->coord_components);
      nir_instr_rewrite_src(&tex->instr,
                            &tex->src[i].src,
                            nir_src_for_ssa(nir_fmul(b, coords, scale)));
   }

   tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
}

static nir_ssa_def *
sample_plane(nir_builder *b, nir_tex_instr *tex, int plane)
{
   assert(tex->dest.is_ssa);
   assert(nir_tex_instr_dest_size(tex) == 4);
   assert(nir_alu_type_get_base_type(tex->dest_type) == nir_type_float);
   assert(tex->op == nir_texop_tex);
   assert(tex->coord_components == 2);

   nir_tex_instr *plane_tex = nir_tex_instr_create(b->shader, 2);
   nir_src_copy(&plane_tex->src[0].src, &tex->src[0].src, plane_tex);
   plane_tex->src[0].src_type = nir_tex_src_coord;
   plane_tex->src[1].src = nir_src_for_ssa(nir_imm_int(b, plane));
   plane_tex->src[1].src_type = nir_tex_src_plane;
   plane_tex->op = nir_texop_tex;
   plane_tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
   plane_tex->dest_type = nir_type_float;
   plane_tex->coord_components = 2;

   plane_tex->texture_index = tex->texture_index;
   plane_tex->texture = (nir_deref_var *)
      nir_copy_deref(plane_tex, &tex->texture->deref);
   plane_tex->sampler_index = tex->sampler_index;
   plane_tex->sampler = (nir_deref_var *)
      nir_copy_deref(plane_tex, &tex->sampler->deref);

   nir_ssa_dest_init(&plane_tex->instr, &plane_tex->dest, 4, 32, NULL);

   nir_builder_instr_insert(b, &plane_tex->instr);

   return &plane_tex->dest.ssa;
}

static void
convert_yuv_to_rgb(nir_builder *b, nir_tex_instr *tex,
                   nir_ssa_def *y, nir_ssa_def *u, nir_ssa_def *v)
{
   nir_const_value m[3] = {
      { .f32 = { 1.0f,  0.0f,         1.59602678f, 0.0f } },
      { .f32 = { 1.0f, -0.39176229f, -0.81296764f, 0.0f } },
      { .f32 = { 1.0f,  2.01723214f,  0.0f,        0.0f } }
   };

   nir_ssa_def *yuv =
      nir_vec4(b,
               nir_fmul(b, nir_imm_float(b, 1.16438356f),
                        nir_fadd(b, y, nir_imm_float(b, -0.0625f))),
               nir_channel(b, nir_fadd(b, u, nir_imm_float(b, -0.5f)), 0),
               nir_channel(b, nir_fadd(b, v, nir_imm_float(b, -0.5f)), 0),
               nir_imm_float(b, 0.0));

   nir_ssa_def *red = nir_fdot4(b, yuv, nir_build_imm(b, 4, 32, m[0]));
   nir_ssa_def *green = nir_fdot4(b, yuv, nir_build_imm(b, 4, 32, m[1]));
   nir_ssa_def *blue = nir_fdot4(b, yuv, nir_build_imm(b, 4, 32, m[2]));

   nir_ssa_def *result = nir_vec4(b, red, green, blue, nir_imm_float(b, 1.0f));

   nir_ssa_def_rewrite_uses(&tex->dest.ssa, nir_src_for_ssa(result));
}

static void
lower_y_uv_external(nir_builder *b, nir_tex_instr *tex)
{
   b->cursor = nir_after_instr(&tex->instr);

   nir_ssa_def *y = sample_plane(b, tex, 0);
   nir_ssa_def *uv = sample_plane(b, tex, 1);

   convert_yuv_to_rgb(b, tex,
                      nir_channel(b, y, 0),
                      nir_channel(b, uv, 0),
                      nir_channel(b, uv, 1));
}

static void
lower_y_u_v_external(nir_builder *b, nir_tex_instr *tex)
{
   b->cursor = nir_after_instr(&tex->instr);

   nir_ssa_def *y = sample_plane(b, tex, 0);
   nir_ssa_def *u = sample_plane(b, tex, 1);
   nir_ssa_def *v = sample_plane(b, tex, 2);

   convert_yuv_to_rgb(b, tex,
                      nir_channel(b, y, 0),
                      nir_channel(b, u, 0),
                      nir_channel(b, v, 0));
}

static void
lower_yx_xuxv_external(nir_builder *b, nir_tex_instr *tex)
{
   b->cursor = nir_after_instr(&tex->instr);

   nir_ssa_def *y = sample_plane(b, tex, 0);
   nir_ssa_def *xuxv = sample_plane(b, tex, 1);

   convert_yuv_to_rgb(b, tex,
                      nir_channel(b, y, 0),
                      nir_channel(b, xuxv, 1),
                      nir_channel(b, xuxv, 3));
}

static void
saturate_src(nir_builder *b, nir_tex_instr *tex, unsigned sat_mask)
{
   b->cursor = nir_before_instr(&tex->instr);

   /* Walk through the sources saturating the requested arguments. */
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      if (tex->src[i].src_type != nir_tex_src_coord)
         continue;

      nir_ssa_def *src =
         nir_ssa_for_src(b, tex->src[i].src, tex->coord_components);

      /* split src into components: */
      nir_ssa_def *comp[4];

      assume(tex->coord_components >= 1);

      for (unsigned j = 0; j < tex->coord_components; j++)
         comp[j] = nir_channel(b, src, j);

      /* clamp requested components, array index does not get clamped: */
      unsigned ncomp = tex->coord_components;
      if (tex->is_array)
         ncomp--;

      for (unsigned j = 0; j < ncomp; j++) {
         if ((1 << j) & sat_mask) {
            if (tex->sampler_dim == GLSL_SAMPLER_DIM_RECT) {
               /* non-normalized texture coords, so clamp to texture
                * size rather than [0.0, 1.0]
                */
               nir_ssa_def *txs = get_texture_size(b, tex);
               comp[j] = nir_fmax(b, comp[j], nir_imm_float(b, 0.0));
               comp[j] = nir_fmin(b, comp[j], nir_channel(b, txs, j));
            } else {
               comp[j] = nir_fsat(b, comp[j]);
            }
         }
      }

      /* and move the result back into a single vecN: */
      src = nir_vec(b, comp, tex->coord_components);

      nir_instr_rewrite_src(&tex->instr,
                            &tex->src[i].src,
                            nir_src_for_ssa(src));
   }
}

static nir_ssa_def *
get_zero_or_one(nir_builder *b, nir_alu_type type, uint8_t swizzle_val)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));

   if (swizzle_val == 4) {
      v.u32[0] = v.u32[1] = v.u32[2] = v.u32[3] = 0;
   } else {
      assert(swizzle_val == 5);
      if (type == nir_type_float)
         v.f32[0] = v.f32[1] = v.f32[2] = v.f32[3] = 1.0;
      else
         v.u32[0] = v.u32[1] = v.u32[2] = v.u32[3] = 1;
   }

   return nir_build_imm(b, 4, 32, v);
}

static void
swizzle_result(nir_builder *b, nir_tex_instr *tex, const uint8_t swizzle[4])
{
   assert(tex->dest.is_ssa);

   b->cursor = nir_after_instr(&tex->instr);

   nir_ssa_def *swizzled;
   if (tex->op == nir_texop_tg4) {
      if (swizzle[tex->component] < 4) {
         /* This one's easy */
         tex->component = swizzle[tex->component];
         return;
      } else {
         swizzled = get_zero_or_one(b, tex->dest_type, swizzle[tex->component]);
      }
   } else {
      assert(nir_tex_instr_dest_size(tex) == 4);
      if (swizzle[0] < 4 && swizzle[1] < 4 &&
          swizzle[2] < 4 && swizzle[3] < 4) {
         unsigned swiz[4] = { swizzle[0], swizzle[1], swizzle[2], swizzle[3] };
         /* We have no 0's or 1's, just emit a swizzling MOV */
         swizzled = nir_swizzle(b, &tex->dest.ssa, swiz, 4, false);
      } else {
         nir_ssa_def *srcs[4];
         for (unsigned i = 0; i < 4; i++) {
            if (swizzle[i] < 4) {
               srcs[i] = nir_channel(b, &tex->dest.ssa, swizzle[i]);
            } else {
               srcs[i] = get_zero_or_one(b, tex->dest_type, swizzle[i]);
            }
         }
         swizzled = nir_vec(b, srcs, 4);
      }
   }

   nir_ssa_def_rewrite_uses_after(&tex->dest.ssa, nir_src_for_ssa(swizzled),
                                  swizzled->parent_instr);
}

static void
linearize_srgb_result(nir_builder *b, nir_tex_instr *tex)
{
   assert(tex->dest.is_ssa);
   assert(nir_tex_instr_dest_size(tex) == 4);
   assert(nir_alu_type_get_base_type(tex->dest_type) == nir_type_float);

   b->cursor = nir_after_instr(&tex->instr);

   static const unsigned swiz[4] = {0, 1, 2, 0};
   nir_ssa_def *comp = nir_swizzle(b, &tex->dest.ssa, swiz, 3, true);

   /* Formula is:
    *    (comp <= 0.04045) ?
    *          (comp / 12.92) :
    *          pow((comp + 0.055) / 1.055, 2.4)
    */
   nir_ssa_def *low  = nir_fmul(b, comp, nir_imm_float(b, 1.0 / 12.92));
   nir_ssa_def *high = nir_fpow(b,
                                nir_fmul(b,
                                         nir_fadd(b,
                                                  comp,
                                                  nir_imm_float(b, 0.055)),
                                         nir_imm_float(b, 1.0 / 1.055)),
                                nir_imm_float(b, 2.4));
   nir_ssa_def *cond = nir_fge(b, nir_imm_float(b, 0.04045), comp);
   nir_ssa_def *rgb  = nir_bcsel(b, cond, low, high);

   /* alpha is untouched: */
   nir_ssa_def *result = nir_vec4(b,
                                  nir_channel(b, rgb, 0),
                                  nir_channel(b, rgb, 1),
                                  nir_channel(b, rgb, 2),
                                  nir_channel(b, &tex->dest.ssa, 3));

   nir_ssa_def_rewrite_uses_after(&tex->dest.ssa, nir_src_for_ssa(result),
                                  result->parent_instr);
}

static bool
nir_lower_tex_block(nir_block *block, nir_builder *b,
                    const nir_lower_tex_options *options)
{
   bool progress = false;

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_tex)
         continue;

      nir_tex_instr *tex = nir_instr_as_tex(instr);
      bool lower_txp = !!(options->lower_txp & (1 << tex->sampler_dim));

      /* mask of src coords to saturate (clamp): */
      unsigned sat_mask = 0;

      if ((1 << tex->sampler_index) & options->saturate_r)
         sat_mask |= (1 << 2);    /* .z */
      if ((1 << tex->sampler_index) & options->saturate_t)
         sat_mask |= (1 << 1);    /* .y */
      if ((1 << tex->sampler_index) & options->saturate_s)
         sat_mask |= (1 << 0);    /* .x */

      /* If we are clamping any coords, we must lower projector first
       * as clamping happens *after* projection:
       */
      if (lower_txp || sat_mask) {
         project_src(b, tex);
         progress = true;
      }

      if ((tex->op == nir_texop_txf && options->lower_txf_offset) ||
          (tex->sampler_dim == GLSL_SAMPLER_DIM_RECT &&
           options->lower_rect_offset)) {
         progress = lower_offset(b, tex) || progress;
      }

      if ((tex->sampler_dim == GLSL_SAMPLER_DIM_RECT) && options->lower_rect) {
         lower_rect(b, tex);
         progress = true;
      }

      if ((1 << tex->texture_index) & options->lower_y_uv_external) {
         lower_y_uv_external(b, tex);
         progress = true;
      }

      if ((1 << tex->texture_index) & options->lower_y_u_v_external) {
         lower_y_u_v_external(b, tex);
         progress = true;
      }

      if ((1 << tex->texture_index) & options->lower_yx_xuxv_external) {
         lower_yx_xuxv_external(b, tex);
         progress = true;
      }


      if (sat_mask) {
         saturate_src(b, tex, sat_mask);
         progress = true;
      }

      if (((1 << tex->texture_index) & options->swizzle_result) &&
          !nir_tex_instr_is_query(tex) &&
          !(tex->is_shadow && tex->is_new_style_shadow)) {
         swizzle_result(b, tex, options->swizzles[tex->texture_index]);
         progress = true;
      }

      /* should be after swizzle so we know which channels are rgb: */
      if (((1 << tex->texture_index) & options->lower_srgb) &&
          !nir_tex_instr_is_query(tex) && !tex->is_shadow) {
         linearize_srgb_result(b, tex);
         progress = true;
      }
   }

   return progress;
}

static bool
nir_lower_tex_impl(nir_function_impl *impl,
                   const nir_lower_tex_options *options)
{
   bool progress = false;
   nir_builder builder;
   nir_builder_init(&builder, impl);

   nir_foreach_block(block, impl) {
      progress |= nir_lower_tex_block(block, &builder, options);
   }

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
   return progress;
}

bool
nir_lower_tex(nir_shader *shader, const nir_lower_tex_options *options)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= nir_lower_tex_impl(function->impl, options);
   }

   return progress;
}
