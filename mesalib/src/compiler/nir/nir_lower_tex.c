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
#include "nir_builtin_builder.h"
#include "nir_format_convert.h"

static float bt601_csc_coeffs[9] = {
   1.16438356f,  1.16438356f, 1.16438356f,
   0.0f,        -0.39176229f, 2.01723214f,
   1.59602678f, -0.81296764f, 0.0f,
};
static float bt709_csc_coeffs[9] = {
   1.16438356f,  1.16438356f, 1.16438356f,
   0.0f       , -0.21324861f, 2.11240179f,
   1.79274107f, -0.53290933f, 0.0f,
};
static float bt2020_csc_coeffs[9] = {
   1.16438356f,  1.16438356f, 1.16438356f,
   0.0f       , -0.18732610f, 2.14177232f,
   1.67867411f, -0.65042432f, 0.0f,
};

static float bt601_csc_offsets[3] = {
   -0.874202218f, 0.531667823f, -1.085630789f
};
static float bt709_csc_offsets[3] = {
   -0.972945075f, 0.301482665f, -1.133402218f
};
static float bt2020_csc_offsets[3] = {
   -0.915687932f, 0.347458499f, -1.148145075f
};

static bool
project_src(nir_builder *b, nir_tex_instr *tex)
{
   /* Find the projector in the srcs list, if present. */
   int proj_index = nir_tex_instr_src_index(tex, nir_tex_src_projector);
   if (proj_index < 0)
      return false;

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
   return true;
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
         nir_ssa_def *txs = nir_i2f32(b, nir_get_texture_size(b, tex));
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
   /* Set the sampler_dim to 2D here so that get_texture_size picks up the
    * right dimensionality.
    */
   tex->sampler_dim = GLSL_SAMPLER_DIM_2D;

   nir_ssa_def *txs = nir_i2f32(b, nir_get_texture_size(b, tex));
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
}

static void
lower_implicit_lod(nir_builder *b, nir_tex_instr *tex)
{
   assert(tex->op == nir_texop_tex || tex->op == nir_texop_txb);
   assert(nir_tex_instr_src_index(tex, nir_tex_src_lod) < 0);
   assert(nir_tex_instr_src_index(tex, nir_tex_src_ddx) < 0);
   assert(nir_tex_instr_src_index(tex, nir_tex_src_ddy) < 0);

   b->cursor = nir_before_instr(&tex->instr);

   nir_ssa_def *lod = nir_get_texture_lod(b, tex);

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
sample_plane(nir_builder *b, nir_tex_instr *tex, int plane,
             const nir_lower_tex_options *options)
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

   nir_ssa_dest_init(&plane_tex->instr, &plane_tex->dest, 4,
         nir_dest_bit_size(tex->dest), NULL);

   nir_builder_instr_insert(b, &plane_tex->instr);

   /* If scaling_factor is set, return a scaled value. */
   if (options->scale_factors[tex->texture_index])
      return nir_fmul_imm(b, &plane_tex->dest.ssa,
                          options->scale_factors[tex->texture_index]);

   return &plane_tex->dest.ssa;
}

static void
convert_yuv_to_rgb(nir_builder *b, nir_tex_instr *tex,
                   nir_ssa_def *y, nir_ssa_def *u, nir_ssa_def *v,
                   nir_ssa_def *a,
                   const nir_lower_tex_options *options)
{

   float *offset_vals;
   float *m_vals;
   assert((options->bt709_external & options->bt2020_external) == 0);
   if (options->bt709_external & (1 << tex->texture_index)) {
      m_vals = bt709_csc_coeffs;
      offset_vals = bt709_csc_offsets;
   } else if (options->bt2020_external & (1 << tex->texture_index)) {
      m_vals = bt2020_csc_coeffs;
      offset_vals = bt2020_csc_offsets;
   } else {
      m_vals = bt601_csc_coeffs;
      offset_vals = bt601_csc_offsets;
   }

   nir_const_value m[3][4] = {
      { { .f32 = m_vals[0] }, { .f32 =  m_vals[1] }, { .f32 = m_vals[2] }, { .f32 = 0.0f } },
      { { .f32 = m_vals[3] }, { .f32 =  m_vals[4] }, { .f32 = m_vals[5] }, { .f32 = 0.0f } },
      { { .f32 = m_vals[6] }, { .f32 =  m_vals[7] }, { .f32 = m_vals[8] }, { .f32 = 0.0f } },
   };
   unsigned bit_size = nir_dest_bit_size(tex->dest);

   nir_ssa_def *offset =
      nir_vec4(b,
               nir_imm_float(b, offset_vals[0]),
               nir_imm_float(b, offset_vals[1]),
               nir_imm_float(b, offset_vals[2]),
               a);

   offset = nir_f2fN(b, offset, bit_size);

   nir_ssa_def *m0 = nir_f2fN(b, nir_build_imm(b, 4, 32, m[0]), bit_size);
   nir_ssa_def *m1 = nir_f2fN(b, nir_build_imm(b, 4, 32, m[1]), bit_size);
   nir_ssa_def *m2 = nir_f2fN(b, nir_build_imm(b, 4, 32, m[2]), bit_size);

   nir_ssa_def *result =
      nir_ffma(b, y, m0, nir_ffma(b, u, m1, nir_ffma(b, v, m2, offset)));

   nir_ssa_def_rewrite_uses(&tex->dest.ssa, nir_src_for_ssa(result));
}

static void
lower_y_uv_external(nir_builder *b, nir_tex_instr *tex,
                    const nir_lower_tex_options *options)
{
   b->cursor = nir_after_instr(&tex->instr);

   nir_ssa_def *y = sample_plane(b, tex, 0, options);
   nir_ssa_def *uv = sample_plane(b, tex, 1, options);

   convert_yuv_to_rgb(b, tex,
                      nir_channel(b, y, 0),
                      nir_channel(b, uv, 0),
                      nir_channel(b, uv, 1),
                      nir_imm_float(b, 1.0f),
                      options);
}

static void
lower_y_u_v_external(nir_builder *b, nir_tex_instr *tex,
                     const nir_lower_tex_options *options)
{
   b->cursor = nir_after_instr(&tex->instr);

   nir_ssa_def *y = sample_plane(b, tex, 0, options);
   nir_ssa_def *u = sample_plane(b, tex, 1, options);
   nir_ssa_def *v = sample_plane(b, tex, 2, options);

   convert_yuv_to_rgb(b, tex,
                      nir_channel(b, y, 0),
                      nir_channel(b, u, 0),
                      nir_channel(b, v, 0),
                      nir_imm_float(b, 1.0f),
                      options);
}

static void
lower_yx_xuxv_external(nir_builder *b, nir_tex_instr *tex,
                       const nir_lower_tex_options *options)
{
   b->cursor = nir_after_instr(&tex->instr);

   nir_ssa_def *y = sample_plane(b, tex, 0, options);
   nir_ssa_def *xuxv = sample_plane(b, tex, 1, options);

   convert_yuv_to_rgb(b, tex,
                      nir_channel(b, y, 0),
                      nir_channel(b, xuxv, 1),
                      nir_channel(b, xuxv, 3),
                      nir_imm_float(b, 1.0f),
                      options);
}

static void
lower_xy_uxvx_external(nir_builder *b, nir_tex_instr *tex,
                       const nir_lower_tex_options *options)
{
  b->cursor = nir_after_instr(&tex->instr);

  nir_ssa_def *y = sample_plane(b, tex, 0, options);
  nir_ssa_def *uxvx = sample_plane(b, tex, 1, options);

  convert_yuv_to_rgb(b, tex,
                     nir_channel(b, y, 1),
                     nir_channel(b, uxvx, 0),
                     nir_channel(b, uxvx, 2),
                     nir_imm_float(b, 1.0f),
                     options);
}

static void
lower_ayuv_external(nir_builder *b, nir_tex_instr *tex,
                    const nir_lower_tex_options *options)
{
  b->cursor = nir_after_instr(&tex->instr);

  nir_ssa_def *ayuv = sample_plane(b, tex, 0, options);

  convert_yuv_to_rgb(b, tex,
                     nir_channel(b, ayuv, 2),
                     nir_channel(b, ayuv, 1),
                     nir_channel(b, ayuv, 0),
                     nir_channel(b, ayuv, 3),
                     options);
}

static void
lower_xyuv_external(nir_builder *b, nir_tex_instr *tex,
                    const nir_lower_tex_options *options)
{
  b->cursor = nir_after_instr(&tex->instr);

  nir_ssa_def *xyuv = sample_plane(b, tex, 0, options);

  convert_yuv_to_rgb(b, tex,
                     nir_channel(b, xyuv, 2),
                     nir_channel(b, xyuv, 1),
                     nir_channel(b, xyuv, 0),
                     nir_imm_float(b, 1.0f),
                     options);
}

static void
lower_yuv_external(nir_builder *b, nir_tex_instr *tex,
                   const nir_lower_tex_options *options)
{
  b->cursor = nir_after_instr(&tex->instr);

  nir_ssa_def *yuv = sample_plane(b, tex, 0, options);

  convert_yuv_to_rgb(b, tex,
                     nir_channel(b, yuv, 0),
                     nir_channel(b, yuv, 1),
                     nir_channel(b, yuv, 2),
                     nir_imm_float(b, 1.0f),
                     options);
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
   nir_ssa_def *size = nir_i2f32(b, nir_get_texture_size(b, tex));

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
                           nir_swizzle(b, p, xzy, 3),
                           nir_swizzle(b, p, yzx, 3)));

   dQdx = nir_bcsel(b, cond_z,
                    dPdx,
                    nir_bcsel(b, cond_y,
                              nir_swizzle(b, dPdx, xzy, 3),
                              nir_swizzle(b, dPdx, yzx, 3)));

   dQdy = nir_bcsel(b, cond_z,
                    dPdy,
                    nir_bcsel(b, cond_y,
                              nir_swizzle(b, dPdy, xzy, 3),
                              nir_swizzle(b, dPdy, yzx, 3)));

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
      nir_channels(b, nir_i2f32(b, nir_get_texture_size(b, tex)),
                      component_mask);

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
               nir_ssa_def *txs = nir_i2f32(b, nir_get_texture_size(b, tex));
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
   nir_const_value v[4];

   memset(&v, 0, sizeof(v));

   if (swizzle_val == 4) {
      v[0].u32 = v[1].u32 = v[2].u32 = v[3].u32 = 0;
   } else {
      assert(swizzle_val == 5);
      if (type == nir_type_float)
         v[0].f32 = v[1].f32 = v[2].f32 = v[3].f32 = 1.0;
      else
         v[0].u32 = v[1].u32 = v[2].u32 = v[3].u32 = 1;
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
   nir_ssa_def *swizzled = nir_swizzle(b, &tex->dest.ssa, swiz, 4);

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
         swizzled = nir_swizzle(b, &tex->dest.ssa, swiz, 4);
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
         switch (nir_tex_instr_dest_size(tex)) {
         case 1:
            assert(tex->is_shadow && tex->is_new_style_shadow);
            color = nir_unpack_half_2x16_split_x(b, nir_channel(b, color, 0));
            break;
         case 2: {
            nir_ssa_def *rg = nir_channel(b, color, 0);
            color = nir_vec2(b,
                             nir_unpack_half_2x16_split_x(b, rg),
                             nir_unpack_half_2x16_split_y(b, rg));
            break;
         }
         case 4: {
            nir_ssa_def *rg = nir_channel(b, color, 0);
            nir_ssa_def *ba = nir_channel(b, color, 1);
            color = nir_vec4(b,
                             nir_unpack_half_2x16_split_x(b, rg),
                             nir_unpack_half_2x16_split_y(b, rg),
                             nir_unpack_half_2x16_split_x(b, ba),
                             nir_unpack_half_2x16_split_y(b, ba));
            break;
         }
         default:
            unreachable("wrong dest_size");
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
sampler_index_lt(nir_tex_instr *tex, unsigned max)
{
   assert(nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref) == -1);

   unsigned sampler_index = tex->sampler_index;

   int sampler_offset_idx =
      nir_tex_instr_src_index(tex, nir_tex_src_sampler_offset);
   if (sampler_offset_idx >= 0) {
      if (!nir_src_is_const(tex->src[sampler_offset_idx].src))
         return false;

      sampler_index += nir_src_as_uint(tex->src[sampler_offset_idx].src);
   }

   return sampler_index < max;
}

static bool
lower_tg4_offsets(nir_builder *b, nir_tex_instr *tex)
{
   assert(tex->op == nir_texop_tg4);
   assert(nir_tex_instr_has_explicit_tg4_offsets(tex));
   assert(nir_tex_instr_src_index(tex, nir_tex_src_offset) == -1);

   b->cursor = nir_after_instr(&tex->instr);

   nir_ssa_def *dest[4];
   for (unsigned i = 0; i < 4; ++i) {
      nir_tex_instr *tex_copy = nir_tex_instr_create(b->shader, tex->num_srcs + 1);
      tex_copy->op = tex->op;
      tex_copy->coord_components = tex->coord_components;
      tex_copy->sampler_dim = tex->sampler_dim;
      tex_copy->is_array = tex->is_array;
      tex_copy->is_shadow = tex->is_shadow;
      tex_copy->is_new_style_shadow = tex->is_new_style_shadow;
      tex_copy->component = tex->component;
      tex_copy->dest_type = tex->dest_type;

      for (unsigned j = 0; j < tex->num_srcs; ++j) {
         nir_src_copy(&tex_copy->src[j].src, &tex->src[j].src, tex_copy);
         tex_copy->src[j].src_type = tex->src[j].src_type;
      }

      nir_tex_src src;
      src.src = nir_src_for_ssa(nir_imm_ivec2(b, tex->tg4_offsets[i][0],
                                                 tex->tg4_offsets[i][1]));
      src.src_type = nir_tex_src_offset;
      tex_copy->src[tex_copy->num_srcs - 1] = src;

      nir_ssa_dest_init(&tex_copy->instr, &tex_copy->dest,
                        nir_tex_instr_dest_size(tex), 32, NULL);

      nir_builder_instr_insert(b, &tex_copy->instr);

      dest[i] = nir_channel(b, &tex_copy->dest.ssa, 3);
   }

   nir_ssa_def *res = nir_vec4(b, dest[0], dest[1], dest[2], dest[3]);
   nir_ssa_def_rewrite_uses(&tex->dest.ssa, nir_src_for_ssa(res));
   nir_instr_remove(&tex->instr);

   return true;
}

static bool
nir_lower_txs_lod(nir_builder *b, nir_tex_instr *tex)
{
   int lod_idx = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   if (lod_idx < 0 ||
       (nir_src_is_const(tex->src[lod_idx].src) &&
        nir_src_as_int(tex->src[lod_idx].src) == 0))
      return false;

   unsigned dest_size = nir_tex_instr_dest_size(tex);

   b->cursor = nir_before_instr(&tex->instr);
   nir_ssa_def *lod = nir_ssa_for_src(b, tex->src[lod_idx].src, 1);

   /* Replace the non-0-LOD in the initial TXS operation by a 0-LOD. */
   nir_instr_rewrite_src(&tex->instr, &tex->src[lod_idx].src,
                         nir_src_for_ssa(nir_imm_int(b, 0)));

   /* TXS(LOD) = max(TXS(0) >> LOD, 1) */
   b->cursor = nir_after_instr(&tex->instr);
   nir_ssa_def *minified = nir_imax(b, nir_ushr(b, &tex->dest.ssa, lod),
                                    nir_imm_int(b, 1));

   /* Make sure the component encoding the array size (if any) is not
    * minified.
    */
   if (tex->is_array) {
      nir_ssa_def *comp[3];

      assert(dest_size <= ARRAY_SIZE(comp));
      for (unsigned i = 0; i < dest_size - 1; i++)
         comp[i] = nir_channel(b, minified, i);

      comp[dest_size - 1] = nir_channel(b, &tex->dest.ssa, dest_size - 1);
      minified = nir_vec(b, comp, dest_size);
   }

   nir_ssa_def_rewrite_uses_after(&tex->dest.ssa, nir_src_for_ssa(minified),
                                  minified->parent_instr);
   return true;
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
         progress |= project_src(b, tex);
      }

      if ((tex->op == nir_texop_txf && options->lower_txf_offset) ||
          (sat_mask && nir_tex_instr_src_index(tex, nir_tex_src_coord) >= 0) ||
          (tex->sampler_dim == GLSL_SAMPLER_DIM_RECT &&
           options->lower_rect_offset)) {
         progress = lower_offset(b, tex) || progress;
      }

      if ((tex->sampler_dim == GLSL_SAMPLER_DIM_RECT) && options->lower_rect &&
          tex->op != nir_texop_txf && !nir_tex_instr_is_query(tex)) {
         lower_rect(b, tex);
         progress = true;
      }

      if ((1 << tex->texture_index) & options->lower_y_uv_external) {
         lower_y_uv_external(b, tex, options);
         progress = true;
      }

      if ((1 << tex->texture_index) & options->lower_y_u_v_external) {
         lower_y_u_v_external(b, tex, options);
         progress = true;
      }

      if ((1 << tex->texture_index) & options->lower_yx_xuxv_external) {
         lower_yx_xuxv_external(b, tex, options);
         progress = true;
      }

      if ((1 << tex->texture_index) & options->lower_xy_uxvx_external) {
         lower_xy_uxvx_external(b, tex, options);
         progress = true;
      }

      if ((1 << tex->texture_index) & options->lower_ayuv_external) {
         lower_ayuv_external(b, tex, options);
         progress = true;
      }

      if ((1 << tex->texture_index) & options->lower_xyuv_external) {
         lower_xyuv_external(b, tex, options);
         progress = true;
      }

      if ((1 << tex->texture_index) & options->lower_yuv_external) {
         lower_yuv_external(b, tex, options);
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
          tex->op != nir_texop_query_levels &&
          tex->op != nir_texop_texture_samples) {
         lower_tex_packing(b, tex, options);
         progress = true;
      }

      if (tex->op == nir_texop_txd &&
          (options->lower_txd ||
           (options->lower_txd_shadow && tex->is_shadow) ||
           (options->lower_txd_shadow_clamp && tex->is_shadow && has_min_lod) ||
           (options->lower_txd_offset_clamp && has_offset && has_min_lod) ||
           (options->lower_txd_clamp_bindless_sampler && has_min_lod &&
            nir_tex_instr_src_index(tex, nir_tex_src_sampler_handle) != -1) ||
           (options->lower_txd_clamp_if_sampler_index_not_lt_16 &&
            has_min_lod && !sampler_index_lt(tex, 16)) ||
           (options->lower_txd_cube_map &&
            tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE) ||
           (options->lower_txd_3d &&
            tex->sampler_dim == GLSL_SAMPLER_DIM_3D))) {
         lower_gradient(b, tex);
         progress = true;
         continue;
      }

      bool shader_supports_implicit_lod =
         b->shader->info.stage == MESA_SHADER_FRAGMENT ||
         (b->shader->info.stage == MESA_SHADER_COMPUTE &&
          b->shader->info.cs.derivative_group != DERIVATIVE_GROUP_NONE);

      /* TXF, TXS and TXL require a LOD but not everything we implement using those
       * three opcodes provides one.  Provide a default LOD of 0.
       */
      if ((nir_tex_instr_src_index(tex, nir_tex_src_lod) == -1) &&
          (tex->op == nir_texop_txf || tex->op == nir_texop_txs ||
           tex->op == nir_texop_txl || tex->op == nir_texop_query_levels ||
           (tex->op == nir_texop_tex && !shader_supports_implicit_lod))) {
         b->cursor = nir_before_instr(&tex->instr);
         nir_tex_instr_add_src(tex, nir_tex_src_lod, nir_src_for_ssa(nir_imm_int(b, 0)));
         if (tex->op == nir_texop_tex && options->lower_tex_without_implicit_lod)
            tex->op = nir_texop_txl;
         progress = true;
         continue;
      }

      if (options->lower_txs_lod && tex->op == nir_texop_txs) {
         progress |= nir_lower_txs_lod(b, tex);
         continue;
      }

      /* has to happen after all the other lowerings as the original tg4 gets
       * replaced by 4 tg4 instructions.
       */
      if (tex->op == nir_texop_tg4 &&
          nir_tex_instr_has_explicit_tg4_offsets(tex) &&
          options->lower_tg4_offsets) {
         progress |= lower_tg4_offsets(b, tex);
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
