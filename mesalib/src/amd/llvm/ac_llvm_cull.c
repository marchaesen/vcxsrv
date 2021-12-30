/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */

#include "ac_llvm_cull.h"

#include <llvm-c/Core.h>

struct ac_position_w_info {
   /* If a primitive intersects the W=0 plane, it causes a reflection
    * of the determinant used for face culling. Every vertex behind
    * the W=0 plane negates the determinant, so having 2 vertices behind
    * the plane has no effect. This is i1 true if the determinant should be
    * negated.
    */
   LLVMValueRef w_reflection;

   /* If we simplify the "-w <= p <= w" view culling equation, we get
    * "-w <= w", which can't be satisfied when w is negative.
    * In perspective projection, a negative W means that the primitive
    * is behind the viewer, but the equation is independent of the type
    * of projection.
    *
    * w_accepted is false when all W are negative and therefore
    * the primitive is invisible.
    */
   LLVMValueRef w_accepted;

   /* The bounding box culling doesn't work and should be skipped when this is true. */
   LLVMValueRef any_w_negative;
};

static void ac_analyze_position_w(struct ac_llvm_context *ctx, LLVMValueRef pos[3][4],
                                  struct ac_position_w_info *w, unsigned num_vertices)
{
   LLVMBuilderRef builder = ctx->builder;
   LLVMValueRef all_w_negative = ctx->i1true;

   w->w_reflection = ctx->i1false;
   w->any_w_negative = ctx->i1false;

   for (unsigned i = 0; i < num_vertices; i++) {
      LLVMValueRef neg_w;

      neg_w = LLVMBuildFCmp(builder, LLVMRealOLT, pos[i][3], ctx->f32_0, "");
      /* If neg_w is true, negate w_reflection. */
      w->w_reflection = LLVMBuildXor(builder, w->w_reflection, neg_w, "");
      w->any_w_negative = LLVMBuildOr(builder, w->any_w_negative, neg_w, "");
      all_w_negative = LLVMBuildAnd(builder, all_w_negative, neg_w, "");
   }
   w->w_accepted = LLVMBuildNot(builder, all_w_negative, "");
}

/* Perform front/back face culling and return true if the primitive is accepted. */
static LLVMValueRef ac_cull_face(struct ac_llvm_context *ctx, LLVMValueRef pos[3][4],
                                 struct ac_position_w_info *w, bool cull_front, bool cull_back,
                                 bool cull_zero_area)
{
   LLVMBuilderRef builder = ctx->builder;

   if (cull_front && cull_back)
      return ctx->i1false;

   if (!cull_front && !cull_back && !cull_zero_area)
      return ctx->i1true;

   /* Front/back face culling. Also if the determinant == 0, the triangle
    * area is 0.
    */
   LLVMValueRef det_t0 = LLVMBuildFSub(builder, pos[2][0], pos[0][0], "");
   LLVMValueRef det_t1 = LLVMBuildFSub(builder, pos[1][1], pos[0][1], "");
   LLVMValueRef det_t2 = LLVMBuildFSub(builder, pos[0][0], pos[1][0], "");
   LLVMValueRef det_t3 = LLVMBuildFSub(builder, pos[0][1], pos[2][1], "");
   /* t0 * t1 - t2 * t3  =  t2 * -t3 + t0 * t1  =  fma(t2, -t3, t0 * t1) */
   LLVMValueRef det = ac_build_fmad(ctx, det_t2, LLVMBuildFNeg(builder, det_t3, ""),
                                    LLVMBuildFMul(builder, det_t0, det_t1, ""));

   /* Negative W negates the determinant. */
   det = LLVMBuildSelect(builder, w->w_reflection, LLVMBuildFNeg(builder, det, ""), det, "");

   LLVMValueRef accepted = NULL;
   if (cull_front) {
      LLVMRealPredicate cond = cull_zero_area ? LLVMRealOGT : LLVMRealOGE;
      accepted = LLVMBuildFCmp(builder, cond, det, ctx->f32_0, "");
   } else if (cull_back) {
      LLVMRealPredicate cond = cull_zero_area ? LLVMRealOLT : LLVMRealOLE;
      accepted = LLVMBuildFCmp(builder, cond, det, ctx->f32_0, "");
   } else if (cull_zero_area) {
      accepted = LLVMBuildFCmp(builder, LLVMRealONE, det, ctx->f32_0, "");
   }

   if (accepted) {
      /* Don't reject NaN and +/-infinity, these are tricky.
       * Just trust fixed-function HW to handle these cases correctly.
       */
      accepted = LLVMBuildOr(builder, accepted, ac_build_is_inf_or_nan(ctx, det), "");
   }

   return accepted;
}

static void rotate_45degrees(struct ac_llvm_context *ctx, LLVMValueRef v[2])
{
   /* sin(45) == cos(45) */
   LLVMValueRef sincos45 = LLVMConstReal(ctx->f32, 0.707106781);

   /* x2  =  x*cos45 - y*sin45  =  x*sincos45 - y*sincos45
    * y2  =  x*sin45 + y*cos45  =  x*sincos45 + y*sincos45
    */
   LLVMValueRef first = LLVMBuildFMul(ctx->builder, v[0], sincos45, "");

   /* Doing 2x ffma while duplicating the multiplication is 33% faster than fmul+fadd+fadd. */
   LLVMValueRef result[2] = {
      ac_build_fmad(ctx, LLVMBuildFNeg(ctx->builder, v[1], ""), sincos45, first),
      ac_build_fmad(ctx, v[1], sincos45, first),
   };

   memcpy(v, result, sizeof(result));
}

/* Perform view culling and small primitive elimination and return true
 * if the primitive is accepted and initially_accepted == true. */
static void cull_bbox(struct ac_llvm_context *ctx, LLVMValueRef pos[3][4],
                      LLVMValueRef initially_accepted, struct ac_position_w_info *w,
                      LLVMValueRef vp_scale[2], LLVMValueRef vp_translate[2],
                      LLVMValueRef small_prim_precision,
                      LLVMValueRef clip_half_line_width[2],
                      struct ac_cull_options *options,
                      ac_cull_accept_func accept_func, void *userdata)
{
   LLVMBuilderRef builder = ctx->builder;

   if (!options->cull_view_xy && !options->cull_view_near_z && !options->cull_view_far_z &&
       !options->cull_small_prims) {
      if (accept_func)
         accept_func(ctx, initially_accepted, userdata);
      return;
   }

   ac_build_ifcc(ctx, initially_accepted, 10000000);
   {
      LLVMValueRef bbox_min[3], bbox_max[3];
      LLVMValueRef accepted = ctx->i1true;

      /* Compute the primitive bounding box for easy culling. */
      for (unsigned chan = 0; chan < (options->cull_view_near_z ||
                                      options->cull_view_far_z ? 3 : 2); chan++) {
         assert(options->num_vertices >= 2);
         bbox_min[chan] = ac_build_fmin(ctx, pos[0][chan], pos[1][chan]);
         bbox_max[chan] = ac_build_fmax(ctx, pos[0][chan], pos[1][chan]);

         if (options->num_vertices == 3) {
            bbox_min[chan] = ac_build_fmin(ctx, bbox_min[chan], pos[2][chan]);
            bbox_max[chan] = ac_build_fmax(ctx, bbox_max[chan], pos[2][chan]);
         }

         if (clip_half_line_width[chan]) {
            bbox_min[chan] = LLVMBuildFSub(builder, bbox_min[chan], clip_half_line_width[chan], "");
            bbox_max[chan] = LLVMBuildFAdd(builder, bbox_max[chan], clip_half_line_width[chan], "");
         }
      }

      /* View culling. */
      if (options->cull_view_xy || options->cull_view_near_z || options->cull_view_far_z) {
         for (unsigned chan = 0; chan < 3; chan++) {
            LLVMValueRef visible;

            if ((options->cull_view_xy && chan <= 1) || (options->cull_view_near_z && chan == 2)) {
               float t = chan == 2 && options->use_halfz_clip_space ? 0 : -1;
               visible = LLVMBuildFCmp(builder, LLVMRealOGE, bbox_max[chan],
                                       LLVMConstReal(ctx->f32, t), "");
               accepted = LLVMBuildAnd(builder, accepted, visible, "");
            }

            if ((options->cull_view_xy && chan <= 1) || (options->cull_view_far_z && chan == 2)) {
               visible = LLVMBuildFCmp(builder, LLVMRealOLE, bbox_min[chan], ctx->f32_1, "");
               accepted = LLVMBuildAnd(builder, accepted, visible, "");
            }
         }
      }

      /* Small primitive culling - triangles. */
      if (options->cull_small_prims && options->num_vertices == 3) {
         /* Assuming a sample position at (0.5, 0.5), if we round
          * the bounding box min/max extents and the results of
          * the rounding are equal in either the X or Y direction,
          * the bounding box does not intersect the sample.
          *
          * See these GDC slides for pictures:
          * https://frostbite-wp-prd.s3.amazonaws.com/wp-content/uploads/2016/03/29204330/GDC_2016_Compute.pdf
          */
         LLVMValueRef min, max, not_equal[2], visible;

         for (unsigned chan = 0; chan < 2; chan++) {
            /* Convert the position to screen-space coordinates. */
            min = ac_build_fmad(ctx, bbox_min[chan], vp_scale[chan], vp_translate[chan]);
            max = ac_build_fmad(ctx, bbox_max[chan], vp_scale[chan], vp_translate[chan]);
            /* Scale the bounding box according to the precision of
             * the rasterizer and the number of MSAA samples. */
            min = LLVMBuildFSub(builder, min, small_prim_precision, "");
            max = LLVMBuildFAdd(builder, max, small_prim_precision, "");

            /* Determine if the bbox intersects the sample point.
             * It also works for MSAA, but vp_scale, vp_translate,
             * and small_prim_precision are computed differently.
             */
            min = ac_build_round(ctx, min);
            max = ac_build_round(ctx, max);
            not_equal[chan] = LLVMBuildFCmp(builder, LLVMRealONE, min, max, "");
         }
         visible = LLVMBuildAnd(builder, not_equal[0], not_equal[1], "");
         accepted = LLVMBuildAnd(builder, accepted, visible, "");
      }

      /* Small primitive culling - lines. */
      if (options->cull_small_prims && options->num_vertices == 2) {
         /* This only works with lines without perpendicular end caps (lines with perpendicular
          * end caps are rasterized as quads and thus can't be culled as small prims in 99% of
          * cases because line_width >= 1).
          *
          * This takes advantage of the diamont exit rule, which says that every pixel
          * has a diamond inside it touching the pixel boundary and only if a line exits
          * the diamond, that pixel is filled. If a line enters the diamond or stays
          * outside the diamond, the pixel isn't filled.
          *
          * This algorithm is a little simpler than that. The space outside all diamonds also
          * has the same diamond shape, which we'll call corner diamonds.
          *
          * The idea is to cull all lines that are entirely inside a diamond, including
          * corner diamonds. If a line is entirely inside a diamond, it can be culled because
          * it doesn't exit it. If a line is entirely inside a corner diamond, it can be culled
          * because it doesn't enter any diamond and thus can't exit any diamond.
          *
          * The viewport is rotated by 45 degress to turn diamonds into squares, and a bounding
          * box test is used to determine whether a line is entirely inside any square (diamond).
          *
          * The line width doesn't matter. Wide lines only duplicate filled pixels in either X or
          * Y direction from the filled pixels. MSAA also doesn't matter. MSAA should ideally use
          * perpendicular end caps that enable quad rasterization for lines. Thus, this should
          * always use non-MSAA viewport transformation and non-MSAA small prim precision.
          *
          * A good test is piglit/lineloop because it draws 10k subpixel lines in a circle.
          * It should contain no holes if this matches hw behavior.
          */
         LLVMValueRef v0[2], v1[2];

         /* Get vertex positions in pixels. */
         for (unsigned chan = 0; chan < 2; chan++) {
            v0[chan] = ac_build_fmad(ctx, pos[0][chan], vp_scale[chan], vp_translate[chan]);
            v1[chan] = ac_build_fmad(ctx, pos[1][chan], vp_scale[chan], vp_translate[chan]);
         }

         /* Rotate the viewport by 45 degress, so that diamonds become squares. */
         rotate_45degrees(ctx, v0);
         rotate_45degrees(ctx, v1);

         LLVMValueRef not_equal[2];

         for (unsigned chan = 0; chan < 2; chan++) {
            /* The width of each square is sqrt(0.5), so scale it to 1 because we want
             * round() to give us the position of the closest center of a square (diamond).
             */
            v0[chan] = LLVMBuildFMul(builder, v0[chan], LLVMConstReal(ctx->f32, 1.414213562), "");
            v1[chan] = LLVMBuildFMul(builder, v1[chan], LLVMConstReal(ctx->f32, 1.414213562), "");

            /* Compute the bounding box around both vertices. We do this because we must
             * enlarge the line area by the precision of the rasterizer.
             */
            LLVMValueRef min = ac_build_fmin(ctx, v0[chan], v1[chan]);
            LLVMValueRef max = ac_build_fmax(ctx, v0[chan], v1[chan]);

            /* Enlarge the bounding box by the precision of the rasterizer. */
            min = LLVMBuildFSub(builder, min, small_prim_precision, "");
            max = LLVMBuildFAdd(builder, max, small_prim_precision, "");

            /* Round the bounding box corners. If both rounded corners are equal,
             * the bounding box is entirely inside a square (diamond).
             */
            min = ac_build_round(ctx, min);
            max = ac_build_round(ctx, max);
            not_equal[chan] = LLVMBuildFCmp(builder, LLVMRealONE, min, max, "");
         }

         accepted = LLVMBuildAnd(builder, accepted,
                                 LLVMBuildOr(builder, not_equal[0], not_equal[1], ""), "");
      }

      /* Disregard the bounding box culling if any W is negative because the code
       * doesn't work with that.
       */
      accepted = LLVMBuildOr(builder, accepted, w->any_w_negative, "");

      if (accept_func)
         accept_func(ctx, accepted, userdata);
   }
   ac_build_endif(ctx, 10000000);
}

/**
 * Return i1 true if the primitive is accepted (not culled).
 *
 * \param pos                   Vertex positions 3x vec4
 * \param initially_accepted    AND'ed with the result. Some computations can be
 *                              skipped if this is false.
 * \param vp_scale              Viewport scale XY.
 *                              For MSAA, multiply them by the number of samples.
 * \param vp_translate          Viewport translation XY.
 *                              For MSAA, multiply them by the number of samples.
 * \param small_prim_precision  Precision of small primitive culling. This should
 *                              be the same as or greater than the precision of
 *                              the rasterizer. Set to num_samples / 2^subpixel_bits.
 *                              subpixel_bits are defined by the quantization mode.
 * \param options               See ac_cull_options.
 * \param accept_func           Callback invoked in the inner-most branch where the primitive is accepted.
 */
void ac_cull_primitive(struct ac_llvm_context *ctx, LLVMValueRef pos[3][4],
                       LLVMValueRef initially_accepted, LLVMValueRef vp_scale[2],
                       LLVMValueRef vp_translate[2], LLVMValueRef small_prim_precision,
                       LLVMValueRef clip_half_line_width[2], struct ac_cull_options *options,
                       ac_cull_accept_func accept_func, void *userdata)
{
   struct ac_position_w_info w;
   ac_analyze_position_w(ctx, pos, &w, options->num_vertices);

   /* W culling. */
   LLVMValueRef accepted = options->cull_w ? w.w_accepted : ctx->i1true;
   accepted = LLVMBuildAnd(ctx->builder, accepted, initially_accepted, "");

   /* Face culling. */
   accepted = LLVMBuildAnd(
      ctx->builder, accepted,
      ac_cull_face(ctx, pos, &w, options->cull_front, options->cull_back, options->cull_zero_area),
      "");

   /* View culling and small primitive elimination. */
   cull_bbox(ctx, pos, accepted, &w, vp_scale, vp_translate, small_prim_precision,
             clip_half_line_width, options, accept_func, userdata);
}
