/*
 * Copyright © 2022 Advanced Micro Devices, Inc.
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

#include "nir.h"
#include "nir_builder.h"
#include "nir_builtin_builder.h"

typedef struct {
   bool set_barycentrics;
   nir_intrinsic_instr *found_baryc;
} lower_point_smooth_state;

static nir_intrinsic_instr *
find_any_used_barycentrics(nir_function_impl *impl)
{
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_intrinsic) {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic == nir_intrinsic_load_barycentric_pixel ||
                intr->intrinsic == nir_intrinsic_load_barycentric_centroid ||
                intr->intrinsic == nir_intrinsic_load_barycentric_sample)
               return intr;
         }
      }
   }

   return NULL;
}

/**
 * This NIR lowers pass for point smoothing by modifying the alpha value of
 * fragment outputs using the distance from the center of the point.
 * Anti-aliased points get rounded with respect to their radius.
 */

static bool
lower_point_smooth(nir_builder *b, nir_intrinsic_instr *intr, void *state)
{
   lower_point_smooth_state *s = (lower_point_smooth_state *)state;

   if (intr->intrinsic != nir_intrinsic_store_output &&
       intr->intrinsic != nir_intrinsic_store_deref)
      return false;

   int out_src_idx;
   if (intr->intrinsic == nir_intrinsic_store_output) {
      int location = nir_intrinsic_io_semantics(intr).location;
      if ((location != FRAG_RESULT_COLOR && location < FRAG_RESULT_DATA0) ||
          nir_intrinsic_src_type(intr) != nir_type_float32)
         return false;
      out_src_idx = 0;
   } else {
      nir_variable *var = nir_intrinsic_get_var(intr, 0);
      if ((var->data.location != FRAG_RESULT_COLOR &&
           var->data.location < FRAG_RESULT_DATA0) ||
          glsl_get_base_type(var->type) != GLSL_TYPE_FLOAT)
         return false;
      out_src_idx = 1;
   }

   assert(intr->num_components == 4);

   b->cursor = nir_before_instr(&intr->instr);

   /* Determine the barycentric coordinates. */
   nir_def *baryc;

   if (s->set_barycentrics) {
      baryc = nir_load_barycentric_pixel(b, 32,
                                         .interp_mode = INTERP_MODE_SMOOTH);

      /* Since point interpolation mostly doesn't care about which barycentrics
       * are used, use any that are used by the shader. This is an optimization
       * for hw that is faster if only one set of barycentrics is used.
       */
      if (s->found_baryc) {
         nir_intrinsic_instr *baryc_intr =
            nir_instr_as_intrinsic(baryc->parent_instr);

         /* Overwrite the intrinsic we just created. */
         baryc_intr->intrinsic = s->found_baryc->intrinsic;
         nir_intrinsic_set_interp_mode(baryc_intr,
                                       nir_intrinsic_interp_mode(s->found_baryc));
      }
   } else {
      baryc = nir_undef(b, 2, 32);
   }

   nir_def *coord = nir_load_point_coord_maybe_flipped(b, baryc);

   /* point_size = 1.0 / dFdx(gl_PointCoord.x); */
   nir_def *point_size = nir_frcp(b, nir_ddx(b, nir_channel(b, coord, 0)));

   /* radius = point_size * 0.5 */
   nir_def *radius = nir_fmul_imm(b, point_size, 0.5);

   /**
    * Compute the distance of point from centre
    * distance = √ (x - 0.5)^2 + (y - 0.5)^2
    */
   nir_def *distance = nir_fast_distance(b, coord,
                                         nir_imm_vec2(b, 0.5, 0.5));
   distance = nir_fmul(b, distance, point_size);

   /* alpha = min(max(radius - distance, 0.0), 1.0) */
   nir_def *coverage = nir_fsat(b, nir_fsub(b, radius, distance));

   /* Discard fragments that are not covered by the point */
   nir_discard_if(b, nir_feq_imm(b, coverage, 0.0f));

   /* Write out the fragment color*vec4(1, 1, 1, coverage)*/
   nir_def *one = nir_imm_float(b, 1.0f);
   nir_def *new_val = nir_fmul(b, nir_vec4(b, one, one, one, coverage),
                               intr->src[out_src_idx].ssa);
   nir_src_rewrite(&intr->src[out_src_idx], new_val);

   return true;
}

bool
nir_lower_point_smooth(nir_shader *shader, bool set_barycentrics)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   lower_point_smooth_state state = {
      .set_barycentrics = set_barycentrics,
      .found_baryc = set_barycentrics ? find_any_used_barycentrics(impl) : NULL,
   };

   return nir_shader_intrinsics_pass(shader, lower_point_smooth,
                                     nir_metadata_loop_analysis |
                                        nir_metadata_block_index |
                                        nir_metadata_dominance,
                                     &state);
}
