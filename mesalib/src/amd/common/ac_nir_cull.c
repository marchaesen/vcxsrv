/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 * Copyright 2021 Valve Corporation
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
 *
 */

#include "ac_nir.h"
#include "nir_builder.h"

/* This code is adapted from ac_llvm_cull.c, hence the copyright to AMD. */

typedef struct
{
   nir_ssa_def *w_reflection;
   nir_ssa_def *w_accepted;
   nir_ssa_def *all_w_positive;
   nir_ssa_def *any_w_negative;
} position_w_info;

static void
analyze_position_w(nir_builder *b, nir_ssa_def *pos[3][4], position_w_info *w_info)
{
   nir_ssa_def *all_w_negative = nir_imm_bool(b, true);

   w_info->w_reflection = nir_imm_bool(b, false);
   w_info->any_w_negative = nir_imm_bool(b, false);

   for (unsigned i = 0; i < 3; ++i) {
      nir_ssa_def *neg_w = nir_flt(b, pos[i][3], nir_imm_float(b, 0.0f));
      w_info->w_reflection = nir_ixor(b, neg_w, w_info->w_reflection);
      w_info->any_w_negative = nir_ior(b, neg_w, w_info->any_w_negative);
      all_w_negative = nir_iand(b, neg_w, all_w_negative);
   }

   w_info->all_w_positive = nir_inot(b, w_info->any_w_negative);
   w_info->w_accepted = nir_inot(b, all_w_negative);
}

static nir_ssa_def *
cull_face(nir_builder *b, nir_ssa_def *pos[3][4], const position_w_info *w_info)
{
   nir_ssa_def *det_t0 = nir_fsub(b, pos[2][0], pos[0][0]);
   nir_ssa_def *det_t1 = nir_fsub(b, pos[1][1], pos[0][1]);
   nir_ssa_def *det_t2 = nir_fsub(b, pos[0][0], pos[1][0]);
   nir_ssa_def *det_t3 = nir_fsub(b, pos[0][1], pos[2][1]);
   nir_ssa_def *det_p0 = nir_fmul(b, det_t0, det_t1);
   nir_ssa_def *det_p1 = nir_fmul(b, det_t2, det_t3);
   nir_ssa_def *det = nir_fsub(b, det_p0, det_p1);

   det = nir_bcsel(b, w_info->w_reflection, nir_fneg(b, det), det);

   nir_ssa_def *front_facing_cw = nir_flt(b, det, nir_imm_float(b, 0.0f));
   nir_ssa_def *front_facing_ccw = nir_flt(b, nir_imm_float(b, 0.0f), det);
   nir_ssa_def *ccw = nir_load_cull_ccw_amd(b);
   nir_ssa_def *front_facing = nir_bcsel(b, ccw, front_facing_ccw, front_facing_cw);
   nir_ssa_def *cull_front = nir_load_cull_front_face_enabled_amd(b);
   nir_ssa_def *cull_back = nir_load_cull_back_face_enabled_amd(b);

   nir_ssa_def *face_culled = nir_bcsel(b, front_facing, cull_front, cull_back);

   /* Don't reject NaN and +/-infinity, these are tricky.
    * Just trust fixed-function HW to handle these cases correctly.
    */
   face_culled = nir_iand(b, face_culled, nir_fisfinite(b, det));

   return nir_inot(b, face_culled);
}

static nir_ssa_def *
cull_bbox(nir_builder *b, nir_ssa_def *pos[3][4], nir_ssa_def *accepted, const position_w_info *w_info)
{
   nir_ssa_def *bbox_accepted = NULL;
   nir_ssa_def *try_cull_bbox = nir_iand(b, accepted, w_info->all_w_positive);

   nir_if *if_cull_bbox = nir_push_if(b, try_cull_bbox);
   {
      nir_ssa_def *bbox_min[3] = {0}, *bbox_max[3] = {0};

      for (unsigned chan = 0; chan < 2; ++chan) {
         bbox_min[chan] = nir_fmin(b, pos[0][chan], nir_fmin(b, pos[1][chan], pos[2][chan]));
         bbox_max[chan] = nir_fmax(b, pos[0][chan], nir_fmax(b, pos[1][chan], pos[2][chan]));
      }

      nir_ssa_def *vp_scale[2] = { nir_load_viewport_x_scale(b), nir_load_viewport_y_scale(b), };
      nir_ssa_def *vp_translate[2] = { nir_load_viewport_x_offset(b), nir_load_viewport_y_offset(b), };
      nir_ssa_def *prim_outside_view = nir_imm_false(b);

      /* Frustrum culling - eliminate triangles that are fully outside the view. */
      for (unsigned chan = 0; chan < 2; ++chan) {
         prim_outside_view = nir_ior(b, prim_outside_view, nir_flt(b, bbox_max[chan], nir_imm_float(b, -1.0f)));
         prim_outside_view = nir_ior(b, prim_outside_view, nir_flt(b, nir_imm_float(b, 1.0f), bbox_min[chan]));
      }

      nir_ssa_def *prim_is_small = NULL;
      nir_ssa_def *prim_is_small_else = nir_imm_false(b);

      /* Small primitive filter - eliminate triangles that are too small to affect a sample. */
      nir_if *if_cull_small_prims = nir_push_if(b, nir_load_cull_small_primitives_enabled_amd(b));
      {
         nir_ssa_def *small_prim_precision = nir_load_cull_small_prim_precision_amd(b);
         prim_is_small = nir_imm_false(b);

         for (unsigned chan = 0; chan < 2; ++chan) {
            /* Convert the position to screen-space coordinates. */
            nir_ssa_def *min = nir_ffma(b, bbox_min[chan], vp_scale[chan], vp_translate[chan]);
            nir_ssa_def *max = nir_ffma(b, bbox_max[chan], vp_scale[chan], vp_translate[chan]);

            /* Scale the bounding box according to precision. */
            min = nir_fsub(b, min, small_prim_precision);
            max = nir_fadd(b, max, small_prim_precision);

            /* Determine if the bbox intersects the sample point, by checking if the min and max round to the same int. */
            min = nir_fround_even(b, min);
            max = nir_fround_even(b, max);

            nir_ssa_def *rounded_to_eq = nir_feq(b, min, max);
            prim_is_small = nir_ior(b, prim_is_small, rounded_to_eq);
         }
      }
      nir_pop_if(b, if_cull_small_prims);

      prim_is_small = nir_if_phi(b, prim_is_small, prim_is_small_else);
      nir_ssa_def *prim_invisible = nir_ior(b, prim_outside_view, prim_is_small);

      bbox_accepted = nir_inot(b, prim_invisible);
   }
   nir_pop_if(b, if_cull_bbox);
   return nir_if_phi(b, bbox_accepted, accepted);
}

nir_ssa_def *
ac_nir_cull_triangle(nir_builder *b,
                     nir_ssa_def *initially_accepted,
                     nir_ssa_def *pos[3][4])
{
   position_w_info w_info = {0};
   analyze_position_w(b, pos, &w_info);

   nir_ssa_def *accepted = initially_accepted;
   accepted = nir_iand(b, accepted, w_info.w_accepted);
   accepted = nir_iand(b, accepted, cull_face(b, pos, &w_info));
   accepted = nir_iand(b, accepted, cull_bbox(b, pos, accepted, &w_info));

   return accepted;
}
