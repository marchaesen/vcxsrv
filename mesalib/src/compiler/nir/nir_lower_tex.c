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

typedef struct {
   nir_builder b;
   const nir_lower_tex_options *options;
   bool progress;
} lower_tex_state;

static void
project_src(nir_builder *b, nir_tex_instr *tex)
{
   /* Find the projector in the srcs list, if present. */
   unsigned proj_index;
   for (proj_index = 0; proj_index < tex->num_srcs; proj_index++) {
      if (tex->src[proj_index].src_type == nir_tex_src_projector)
         break;
   }
   if (proj_index == tex->num_srcs)
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

   /* Now move the later tex sources down the array so that the projector
    * disappears.
    */
   nir_instr_rewrite_src(&tex->instr, &tex->src[proj_index].src,
                         NIR_SRC_INIT);
   for (unsigned i = proj_index + 1; i < tex->num_srcs; i++) {
      tex->src[i-1].src_type = tex->src[i].src_type;
      nir_instr_move_src(&tex->instr, &tex->src[i-1].src, &tex->src[i].src);
   }
   tex->num_srcs--;
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

   nir_ssa_dest_init(&txs->instr, &txs->dest, 2, NULL);
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
      v.u[0] = v.u[1] = v.u[2] = v.u[3] = 0;
   } else {
      assert(swizzle_val == 5);
      if (type == nir_type_float)
         v.f[0] = v.f[1] = v.f[2] = v.f[3] = 1.0;
      else
         v.u[0] = v.u[1] = v.u[2] = v.u[3] = 1;
   }

   return nir_build_imm(b, 4, v);
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

static bool
nir_lower_tex_block(nir_block *block, void *void_state)
{
   lower_tex_state *state = void_state;
   nir_builder *b = &state->b;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_tex)
         continue;

      nir_tex_instr *tex = nir_instr_as_tex(instr);
      bool lower_txp = !!(state->options->lower_txp & (1 << tex->sampler_dim));

      /* mask of src coords to saturate (clamp): */
      unsigned sat_mask = 0;

      if ((1 << tex->sampler_index) & state->options->saturate_r)
         sat_mask |= (1 << 2);    /* .z */
      if ((1 << tex->sampler_index) & state->options->saturate_t)
         sat_mask |= (1 << 1);    /* .y */
      if ((1 << tex->sampler_index) & state->options->saturate_s)
         sat_mask |= (1 << 0);    /* .x */

      /* If we are clamping any coords, we must lower projector first
       * as clamping happens *after* projection:
       */
      if (lower_txp || sat_mask) {
         project_src(b, tex);
         state->progress = true;
      }

      if ((tex->sampler_dim == GLSL_SAMPLER_DIM_RECT) &&
          state->options->lower_rect) {
         lower_rect(b, tex);
         state->progress = true;
      }

      if (sat_mask) {
         saturate_src(b, tex, sat_mask);
         state->progress = true;
      }

      if (((1 << tex->texture_index) & state->options->swizzle_result) &&
          !nir_tex_instr_is_query(tex) &&
          !(tex->is_shadow && tex->is_new_style_shadow)) {
         swizzle_result(b, tex, state->options->swizzles[tex->texture_index]);
         state->progress = true;
      }
   }

   return true;
}

static void
nir_lower_tex_impl(nir_function_impl *impl, lower_tex_state *state)
{
   nir_builder_init(&state->b, impl);

   nir_foreach_block(impl, nir_lower_tex_block, state);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

bool
nir_lower_tex(nir_shader *shader, const nir_lower_tex_options *options)
{
   lower_tex_state state;
   state.options = options;
   state.progress = false;

   nir_foreach_function(shader, function) {
      if (function->impl)
         nir_lower_tex_impl(function->impl, &state);
   }

   return state.progress;
}
