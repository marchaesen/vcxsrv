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
#include "nir_format_convert.h"

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
      case nir_tex_src_comparator:
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

static nir_ssa_def *
get_texture_size(nir_builder *b, nir_tex_instr *tex)
{
   b->cursor = nir_before_instr(&tex->instr);

   nir_tex_instr *txs;

   unsigned num_srcs = 1; /* One for the LOD */
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      if (tex->src[i].src_type == nir_tex_src_texture_deref ||
          tex->src[i].src_type == nir_tex_src_sampler_deref ||
          tex->src[i].src_type == nir_tex_src_texture_offset ||
          tex->src[i].src_type == nir_tex_src_sampler_offset)
         num_srcs++;
   }

   txs = nir_tex_instr_create(b->shader, num_srcs);
   txs->op = nir_texop_txs;
   txs->sampler_dim = tex->sampler_dim;
   txs->is_array = tex->is_array;
   txs->is_shadow = tex->is_shadow;
   txs->is_new_style_shadow = tex->is_new_style_shadow;
   txs->texture_index = tex->texture_index;
   txs->sampler_index = tex->sampler_index;
   txs->dest_type = nir_type_int;

   unsigned idx = 0;
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      if (tex->src[i].src_type == nir_tex_src_texture_deref ||
          tex->src[i].src_type == nir_tex_src_sampler_deref ||
          tex->src[i].src_type == nir_tex_src_texture_offset ||
          tex->src[i].src_type == nir_tex_src_sampler_offset) {
         nir_src_copy(&txs->src[idx].src, &tex->src[i].src, txs);
         txs->src[idx].src_type = tex->src[i].src_type;
         idx++;
      }
   }
   /* Add in an LOD because some back-ends require it */
   txs->src[idx].src = nir_src_for_ssa(nir_imm_int(b, 0));
   txs->src[idx].src_type = nir_tex_src_lod;

   nir_ssa_dest_init(&txs->instr, &txs->dest,
                     nir_tex_instr_dest_size(txs), 32, NULL);
   nir_builder_instr_insert(b, &txs->instr);

   return nir_i2f32(b, &txs->dest.ssa);
}

static nir_ssa_def *
get_texture_lod(nir_builder *b, nir_tex_instr *tex)
{
   b->cursor = nir_before_instr(&tex->instr);

   nir_tex_instr *tql;

   unsigned num_srcs = 0;
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      if (tex->src[i].src_type == nir_tex_src_coord ||
          tex->src[i].src_type == nir_tex_src_texture_deref ||
          tex->src[i].src_type == nir_tex_src_sampler_deref ||
          tex->src[i].src_type == nir_tex_src_texture_offset ||
          tex->src[i].src_type == nir_tex_src_sampler_offset)
         num_srcs++;
   }

   tql = nir_tex_instr_create(b->shader, num_srcs);
   tql->op = nir_texop_lod;
   tql->coord_components = tex->coord_components;
   tql->sampler_dim = tex->sampler_dim;
   tql->is_array = tex->is_array;
   tql->is_shadow = tex->is_shadow;
   tql->is_new_style_shadow = tex->is_new_style_shadow;
   tql->texture_index = tex->texture_index;
   tql->sampler_index = tex->sampler_index;
   tql->dest_type = nir_type_float;

   unsigned idx = 0;
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      if (tex->src[i].src_type == nir_tex_src_coord ||
          tex->src[i].src_type == nir_tex_src_texture_deref ||
          tex->src[i].src_type == nir_tex_src_sampler_deref ||
          tex->src[i].src_type == nir_tex_src_texture_offset ||
          tex->src[i].src_type == nir_tex_src_sampler_offset) {
         nir_src_copy(&tql->src[idx].src, &tex->src[i].src, tql);
         tql->src[idx].src_type = tex->src[i].src_type;
         idx++;
      }
   }

   nir_ssa_dest_init(&tql->instr, &tql->dest, 2, 32, NULL);
   nir_builder_instr_insert(b, &tql->instr);

   /* The LOD is the y component of the result */
   return nir_channel(b, &tql->dest.ssa, 1);
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
      if (tex->sampler_dim == GLSL_SAMPLER_DIM_RECT) {
         offset_coord = nir_fadd(b, coord, nir_i2f32(b, offset));
      } else {
         nir_ssa_def *txs = get_texture_size(b, tex);
         nir_ssa_def *scale = nir_frcp(b, txs);

         offset_coord = nir_fadd(b, coord,
                                 nir_fmul(b,
                                          nir_i2f32(b, offset),
                                          scale));
      }
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

static void
lower_implicit_lod(nir_builder *b, nir_tex_instr *tex)
{
   assert(tex->op == nir_texop_tex || tex->op == nir_texop_txb);
   assert(nir_tex_instr_src_index(tex, nir_tex_src_lod) < 0);
   assert(nir_tex_instr_src_index(tex, nir_tex_src_ddx) < 0);
   assert(nir_tex_instr_src_index(tex, nir_tex_src_ddy) < 0);

   b->cursor = nir_before_instr(&tex->instr);

   nir_ssa_def *lod = get_texture_lod(b, tex);

   int bias_idx = nir_tex_instr_src_index(tex, nir_tex_src_bias);
   if (bias_idx >= 0) {
      /* If we have a bias, add it in */
      lod = nir_fadd(b, lod, nir_ssa_for_src(b, tex->src[bias_idx].src, 1));
      nir_tex_instr_remove_src(tex, bias_idx);
   }

   int min_lod_idx = nir_tex_instr_src_index(tex, nir_tex_src_min_lod);
   if (min_lod_idx >= 0) {
      /* If we have a minimum LOD, clamp LOD accordingly */
      lod = nir_fmax(b, lod, nir_ssa_for_src(b, tex->src[min_lod_idx].src, 1));
      nir_tex_instr_remove_src(tex, min_lod_idx);
   }

   nir_tex_instr_add_src(tex, nir_tex_src_lod, nir_src_for_ssa(lod));
   tex->op = nir_texop_txl;
}

static nir_ssa_def *
sample_plane(nir_builder *b, nir_tex_instr *tex, int plane)
{
   assert(tex->dest.is_ssa);
   assert(nir_tex_instr_dest_size(tex) == 4);
   assert(nir_alu_type_get_base_type(tex->dest_type) == nir_type_float);
   assert(tex->op == nir_texop_tex);
   assert(tex->coord_components == 2);

   nir_tex_instr *plane_tex =
      nir_tex_instr_create(b->shader, tex->num_srcs + 1);
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      nir_src_copy(&plane_tex->src[i].src, &tex->src[i].src, plane_tex);
      plane_tex->src[i].src_type = tex->src[i].src_type;
   }
   plane_tex->src[tex->num_srcs].src = nir_src_for_ssa(nir_imm_int(b, plane));
   plane_tex->src[tex->num_srcs].src_type = nir_tex_src_plane;
   plane_tex->op = nir_texop_tex;
   plane_tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
   plane_tex->dest_type = nir_type_float;
   plane_tex->coord_components = 2;

   plane_tex->texture_index = tex->texture_index;
   plane_tex->sampler_index = tex->sampler_index;

   nir_ssa_dest_init(&plane_tex->instr, &plane_tex->dest, 4, 32, NULL);

   nir_builder_instr_insert(b, &plane_tex->instr);

   return &plane_tex->dest.ssa;
}

static void
convert_yuv_to_rgb(nir_builder *b, nir_tex_instr *tex,
                   nir_ssa_def *y, nir_ssa_def *u, nir_ssa_def *v,
                   nir_ssa_def *a)
{
   nir_const_value m[3] = {
      { .f32 = { 1.0f,  0.0f,         1.59602678f, 0.0f } },
      { .f32 = { 1.0f, -0.39176229f, -0.81296764f, 0.0f } },
      { .f32 = { 1.0f,  2.01723214f,  0.0f,        0.0f } }
   };

   nir_ssa_def *yuv =
      nir_vec4(b,
               nir_fmul(b, nir_imm_float(b, 1.16438356f),
                        nir_fadd(b, y, nir_imm_float(b, -16.0f / 255.0f))),
               nir_channel(b, nir_fadd(b, u, nir_imm_float(b, -128.0f / 255.0f)), 0),
               nir_channel(b, nir_fadd(b, v, nir_imm_float(b, -128.0f / 255.0f)), 0),
               nir_imm_float(b, 0.0));

   nir_ssa_def *red = nir_fdot4(b, yuv, nir_build_imm(b, 4, 32, m[0]));
   nir_ssa_def *green = nir_fdot4(b, yuv, nir_build_imm(b, 4, 32, m[1]));
   nir_ssa_def *blue = nir_fdot4(b, yuv, nir_build_imm(b, 4, 32, m[2]));

   nir_ssa_def *result = nir_vec4(b, red, green, blue, a);

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
                      nir_channel(b, uv, 1),
                      nir_imm_float(b, 1.0f));
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
                      nir_channel(b, v, 0),
                      nir_imm_float(b, 1.0f));
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
                      nir_channel(b, xuxv, 3),
                      nir_imm_float(b, 1.0f));
}

static void
lower_xy_uxvx_external(nir_builder *b, nir_tex_instr *tex)
{
  b->cursor = nir_after_instr(&tex->instr);

  nir_ssa_def *y = sample_plane(b, tex, 0);
  nir_ssa_def *uxvx = sample_plane(b, tex, 1);

  convert_yuv_to_rgb(b, tex,
                     nir_channel(b, y, 1),
                     nir_channel(b, uxvx, 0),
                     nir_channel(b, uxvx, 2),
                     nir_imm_float(b, 1.0f));
}

static void
lower_ayuv_external(nir_builder *b, nir_tex_instr *tex)
{
  b->cursor = nir_after_instr(&tex->instr);

  nir_ssa_def *ayuv = sample_plane(b, tex, 0);

  convert_yuv_to_rgb(b, tex,
                     nir_channel(b, ayuv, 2),
                     nir_channel(b, ayuv, 1),
                     nir_channel(b, ayuv, 0),
                     nir_channel(b, ayuv, 3));
}

/*
 * Converts a nir_texop_txd instruction to nir_texop_txl with the given lod
 * computed from the gradients.
 */
static void
replace_gradient_with_lod(nir_builder *b, nir_ssa_def *lod, nir_tex_instr *tex)
{
   assert(tex->op == nir_texop_txd);

   nir_tex_instr_remove_src(tex, nir_tex_instr_src_index(tex, nir_tex_src_ddx));
   nir_tex_instr_remove_src(tex, nir_tex_instr_src_index(tex, nir_tex_src_ddy));

   int min_lod_idx = nir_tex_instr_src_index(tex, nir_tex_src_min_lod);
   if (min_lod_idx >= 0) {
      /* If we have a minimum LOD, clamp LOD accordingly */
      lod = nir_fmax(b, lod, nir_ssa_for_src(b, tex->src[min_lod_idx].src, 1));
      nir_tex_instr_remove_src(tex, min_lod_idx);
   }

   nir_tex_instr_add_src(tex, nir_tex_src_lod, nir_src_for_ssa(lod));
   tex->op = nir_texop_txl;
}

static void
lower_gradient_cube_map(nir_builder *b, nir_tex_instr *tex)
{
   assert(tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE);
   assert(tex->op == nir_texop_txd);
   assert(tex->dest.is_ssa);

   /* Use textureSize() to get the width and height of LOD 0 */
   nir_ssa_def *size = get_texture_size(b, tex);

   /* Cubemap texture lookups first generate a texture coordinate normalized
    * to [-1, 1] on the appropiate face. The appropiate face is determined
    * by which component has largest magnitude and its sign. The texture
    * coordinate is the quotient of the remaining texture coordinates against
    * that absolute value of the component of largest magnitude. This
    * division requires that the computing of the derivative of the texel
    * coordinate must use the quotient rule. The high level GLSL code is as
    * follows:
    *
    * Step 1: selection
    *
    * vec3 abs_p, Q, dQdx, dQdy;
    * abs_p = abs(ir->coordinate);
    * if (abs_p.x >= max(abs_p.y, abs_p.z)) {
    *    Q = ir->coordinate.yzx;
    *    dQdx = ir->lod_info.grad.dPdx.yzx;
    *    dQdy = ir->lod_info.grad.dPdy.yzx;
    * }
    * if (abs_p.y >= max(abs_p.x, abs_p.z)) {
    *    Q = ir->coordinate.xzy;
    *    dQdx = ir->lod_info.grad.dPdx.xzy;
    *    dQdy = ir->lod_info.grad.dPdy.xzy;
    * }
    * if (abs_p.z >= max(abs_p.x, abs_p.y)) {
    *    Q = ir->coordinate;
    *    dQdx = ir->lod_info.grad.dPdx;
    *    dQdy = ir->lod_info.grad.dPdy;
    * }
    *
    * Step 2: use quotient rule to compute derivative. The normalized to
    * [-1, 1] texel coordinate is given by Q.xy / (sign(Q.z) * Q.z). We are
    * only concerned with the magnitudes of the derivatives whose values are
    * not affected by the sign. We drop the sign from the computation.
    *
    * vec2 dx, dy;
    * float recip;
    *
    * recip = 1.0 / Q.z;
    * dx = recip * ( dQdx.xy - Q.xy * (dQdx.z * recip) );
    * dy = recip * ( dQdy.xy - Q.xy * (dQdy.z * recip) );
    *
    * Step 3: compute LOD. At this point we have the derivatives of the
    * texture coordinates normalized to [-1,1]. We take the LOD to be
    *  result = log2(max(sqrt(dot(dx, dx)), sqrt(dy, dy)) * 0.5 * L)
    *         = -1.0 + log2(max(sqrt(dot(dx, dx)), sqrt(dy, dy)) * L)
    *         = -1.0 + log2(sqrt(max(dot(dx, dx), dot(dy,dy))) * L)
    *         = -1.0 + log2(sqrt(L * L * max(dot(dx, dx), dot(dy,dy))))
    *         = -1.0 + 0.5 * log2(L * L * max(dot(dx, dx), dot(dy,dy)))
    * where L is the dimension of the cubemap. The code is:
    *
    * float M, result;
    * M = max(dot(dx, dx), dot(dy, dy));
    * L = textureSize(sampler, 0).x;
    * result = -1.0 + 0.5 * log2(L * L * M);
    */

   /* coordinate */
   nir_ssa_def *p =
      tex->src[nir_tex_instr_src_index(tex, nir_tex_src_coord)].src.ssa;

   /* unmodified dPdx, dPdy values */
   nir_ssa_def *dPdx =
      tex->src[nir_tex_instr_src_index(tex, nir_tex_src_ddx)].src.ssa;
   nir_ssa_def *dPdy =
      tex->src[nir_tex_instr_src_index(tex, nir_tex_src_ddy)].src.ssa;

   nir_ssa_def *abs_p = nir_fabs(b, p);
   nir_ssa_def *abs_p_x = nir_channel(b, abs_p, 0);
   nir_ssa_def *abs_p_y = nir_channel(b, abs_p, 1);
   nir_ssa_def *abs_p_z = nir_channel(b, abs_p, 2);

   /* 1. compute selector */
   nir_ssa_def *Q, *dQdx, *dQdy;

   nir_ssa_def *cond_z = nir_fge(b, abs_p_z, nir_fmax(b, abs_p_x, abs_p_y));
   nir_ssa_def *cond_y = nir_fge(b, abs_p_y, nir_fmax(b, abs_p_x, abs_p_z));

   unsigned yzx[3] = { 1, 2, 0 };
   unsigned xzy[3] = { 0, 2, 1 };

   Q = nir_bcsel(b, cond_z,
                 p,
                 nir_bcsel(b, cond_y,
                           nir_swizzle(b, p, xzy, 3, false),
                           nir_swizzle(b, p, yzx, 3, false)));

   dQdx = nir_bcsel(b, cond_z,
                    dPdx,
                    nir_bcsel(b, cond_y,
                              nir_swizzle(b, dPdx, xzy, 3, false),
                              nir_swizzle(b, dPdx, yzx, 3, false)));

   dQdy = nir_bcsel(b, cond_z,
                    dPdy,
                    nir_bcsel(b, cond_y,
                              nir_swizzle(b, dPdy, xzy, 3, false),
                              nir_swizzle(b, dPdy, yzx, 3, false)));

   /* 2. quotient rule */

   /* tmp = Q.xy * recip;
    * dx = recip * ( dQdx.xy - (tmp * dQdx.z) );
    * dy = recip * ( dQdy.xy - (tmp * dQdy.z) );
    */
   nir_ssa_def *rcp_Q_z = nir_frcp(b, nir_channel(b, Q, 2));

   nir_ssa_def *Q_xy = nir_channels(b, Q, 0x3);
   nir_ssa_def *tmp = nir_fmul(b, Q_xy, rcp_Q_z);

   nir_ssa_def *dQdx_xy = nir_channels(b, dQdx, 0x3);
   nir_ssa_def *dQdx_z = nir_channel(b, dQdx, 2);
   nir_ssa_def *dx =
      nir_fmul(b, rcp_Q_z, nir_fsub(b, dQdx_xy, nir_fmul(b, tmp, dQdx_z)));

   nir_ssa_def *dQdy_xy = nir_channels(b, dQdy, 0x3);
   nir_ssa_def *dQdy_z = nir_channel(b, dQdy, 2);
   nir_ssa_def *dy =
      nir_fmul(b, rcp_Q_z, nir_fsub(b, dQdy_xy, nir_fmul(b, tmp, dQdy_z)));

   /* M = max(dot(dx, dx), dot(dy, dy)); */
   nir_ssa_def *M = nir_fmax(b, nir_fdot(b, dx, dx), nir_fdot(b, dy, dy));

   /* size has textureSize() of LOD 0 */
   nir_ssa_def *L = nir_channel(b, size, 0);

   /* lod = -1.0 + 0.5 * log2(L * L * M); */
   nir_ssa_def *lod =
      nir_fadd(b,
               nir_imm_float(b, -1.0f),
               nir_fmul(b,
                        nir_imm_float(b, 0.5f),
                        nir_flog2(b, nir_fmul(b, L, nir_fmul(b, L, M)))));

   /* 3. Replace the gradient instruction with an equivalent lod instruction */
   replace_gradient_with_lod(b, lod, tex);
}

static void
lower_gradient(nir_builder *b, nir_tex_instr *tex)
{
   /* Cubes are more complicated and have their own function */
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
      lower_gradient_cube_map(b, tex);
      return;
   }

   assert(tex->sampler_dim != GLSL_SAMPLER_DIM_CUBE);
   assert(tex->op == nir_texop_txd);
   assert(tex->dest.is_ssa);

   /* Use textureSize() to get the width and height of LOD 0 */
   unsigned component_mask;
   switch (tex->sampler_dim) {
   case GLSL_SAMPLER_DIM_3D:
      component_mask = 7;
      break;
   case GLSL_SAMPLER_DIM_1D:
      component_mask = 1;
      break;
   default:
      component_mask = 3;
      break;
   }

   nir_ssa_def *size =
      nir_channels(b, get_texture_size(b, tex), component_mask);

   /* Scale the gradients by width and height.  Effectively, the incoming
    * gradients are s'(x,y), t'(x,y), and r'(x,y) from equation 3.19 in the
    * GL 3.0 spec; we want u'(x,y), which is w_t * s'(x,y).
    */
   nir_ssa_def *ddx =
      tex->src[nir_tex_instr_src_index(tex, nir_tex_src_ddx)].src.ssa;
   nir_ssa_def *ddy =
      tex->src[nir_tex_instr_src_index(tex, nir_tex_src_ddy)].src.ssa;

   nir_ssa_def *dPdx = nir_fmul(b, ddx, size);
   nir_ssa_def *dPdy = nir_fmul(b, ddy, size);

   nir_ssa_def *rho;
   if (dPdx->num_components == 1) {
      rho = nir_fmax(b, nir_fabs(b, dPdx), nir_fabs(b, dPdy));
   } else {
      rho = nir_fmax(b,
                     nir_fsqrt(b, nir_fdot(b, dPdx, dPdx)),
                     nir_fsqrt(b, nir_fdot(b, dPdy, dPdy)));
   }

   /* lod = log2(rho).  We're ignoring GL state biases for now. */
   nir_ssa_def *lod = nir_flog2(b, rho);

   /* Replace the gradient instruction with an equivalent lod instruction */
   replace_gradient_with_lod(b, lod, tex);
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
swizzle_tg4_broadcom(nir_builder *b, nir_tex_instr *tex)
{
   assert(tex->dest.is_ssa);

   b->cursor = nir_after_instr(&tex->instr);

   assert(nir_tex_instr_dest_size(tex) == 4);
   unsigned swiz[4] = { 2, 3, 1, 0 };
   nir_ssa_def *swizzled = nir_swizzle(b, &tex->dest.ssa, swiz, 4, false);

   nir_ssa_def_rewrite_uses_after(&tex->dest.ssa, nir_src_for_ssa(swizzled),
                                  swizzled->parent_instr);
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
         /* We have no 0s or 1s, just emit a swizzling MOV */
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

   nir_ssa_def *rgb =
      nir_format_srgb_to_linear(b, nir_channels(b, &tex->dest.ssa, 0x7));

   /* alpha is untouched: */
   nir_ssa_def *result = nir_vec4(b,
                                  nir_channel(b, rgb, 0),
                                  nir_channel(b, rgb, 1),
                                  nir_channel(b, rgb, 2),
                                  nir_channel(b, &tex->dest.ssa, 3));

   nir_ssa_def_rewrite_uses_after(&tex->dest.ssa, nir_src_for_ssa(result),
                                  result->parent_instr);
}

/**
 * Lowers texture instructions from giving a vec4 result to a vec2 of f16,
 * i16, or u16, or a single unorm4x8 value.
 *
 * Note that we don't change the destination num_components, because
 * nir_tex_instr_dest_size() will still return 4.  The driver is just expected
 * to not store the other channels, given that nothing at the NIR level will
 * read them.
 */
static void
lower_tex_packing(nir_builder *b, nir_tex_instr *tex,
                  const nir_lower_tex_options *options)
{
   nir_ssa_def *color = &tex->dest.ssa;

   b->cursor = nir_after_instr(&tex->instr);

   switch (options->lower_tex_packing[tex->sampler_index]) {
   case nir_lower_tex_packing_none:
      return;

   case nir_lower_tex_packing_16: {
      static const unsigned bits[4] = {16, 16, 16, 16};

      switch (nir_alu_type_get_base_type(tex->dest_type)) {
      case nir_type_float:
         if (tex->is_shadow && tex->is_new_style_shadow) {
            color = nir_unpack_half_2x16_split_x(b, nir_channel(b, color, 0));
         } else {
            nir_ssa_def *rg = nir_channel(b, color, 0);
            nir_ssa_def *ba = nir_channel(b, color, 1);
            color = nir_vec4(b,
                             nir_unpack_half_2x16_split_x(b, rg),
                             nir_unpack_half_2x16_split_y(b, rg),
                             nir_unpack_half_2x16_split_x(b, ba),
                             nir_unpack_half_2x16_split_y(b, ba));
         }
         break;

      case nir_type_int:
         color = nir_format_unpack_sint(b, color, bits, 4);
         break;

      case nir_type_uint:
         color = nir_format_unpack_uint(b, color, bits, 4);
         break;

      default:
         unreachable("unknown base type");
      }
      break;
   }

   case nir_lower_tex_packing_8:
      assert(nir_alu_type_get_base_type(tex->dest_type) == nir_type_float);
      color = nir_unpack_unorm_4x8(b, nir_channel(b, color, 0));
      break;
   }

   nir_ssa_def_rewrite_uses_after(&tex->dest.ssa, nir_src_for_ssa(color),
                                  color->parent_instr);
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
          (sat_mask && nir_tex_instr_src_index(tex, nir_tex_src_coord) >= 0) ||
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

      if ((1 << tex->texture_index) & options->lower_xy_uxvx_external) {
         lower_xy_uxvx_external(b, tex);
         progress = true;
      }

      if ((1 << tex->texture_index) & options->lower_ayuv_external) {
         lower_ayuv_external(b, tex);
         progress = true;
      }

      if (sat_mask) {
         saturate_src(b, tex, sat_mask);
         progress = true;
      }

      if (tex->op == nir_texop_tg4 && options->lower_tg4_broadcom_swizzle) {
         swizzle_tg4_broadcom(b, tex);
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

      const bool has_min_lod =
         nir_tex_instr_src_index(tex, nir_tex_src_min_lod) >= 0;
      const bool has_offset =
         nir_tex_instr_src_index(tex, nir_tex_src_offset) >= 0;

      if (tex->op == nir_texop_txb && tex->is_shadow && has_min_lod &&
          options->lower_txb_shadow_clamp) {
         lower_implicit_lod(b, tex);
         progress = true;
      }

      if (options->lower_tex_packing[tex->sampler_index] !=
          nir_lower_tex_packing_none &&
          tex->op != nir_texop_txs &&
          tex->op != nir_texop_query_levels) {
         lower_tex_packing(b, tex, options);
         progress = true;
      }

      if (tex->op == nir_texop_txd &&
          (options->lower_txd ||
           (options->lower_txd_shadow && tex->is_shadow) ||
           (options->lower_txd_shadow_clamp && tex->is_shadow && has_min_lod) ||
           (options->lower_txd_offset_clamp && has_offset && has_min_lod) ||
           (options->lower_txd_cube_map &&
            tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE) ||
           (options->lower_txd_3d &&
            tex->sampler_dim == GLSL_SAMPLER_DIM_3D))) {
         lower_gradient(b, tex);
         progress = true;
         continue;
      }

      /* TXF, TXS and TXL require a LOD but not everything we implement using those
       * three opcodes provides one.  Provide a default LOD of 0.
       */
      if ((nir_tex_instr_src_index(tex, nir_tex_src_lod) == -1) &&
          (tex->op == nir_texop_txf || tex->op == nir_texop_txs ||
           tex->op == nir_texop_txl || tex->op == nir_texop_query_levels ||
           (tex->op == nir_texop_tex &&
            b->shader->info.stage != MESA_SHADER_FRAGMENT))) {
         b->cursor = nir_before_instr(&tex->instr);
         nir_tex_instr_add_src(tex, nir_tex_src_lod, nir_src_for_ssa(nir_imm_int(b, 0)));
         progress = true;
         continue;
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
