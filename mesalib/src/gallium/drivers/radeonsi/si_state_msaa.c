/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "si_build_pm4.h"

/* For MSAA sample positions. */
#define FILL_SREG(s0x, s0y, s1x, s1y, s2x, s2y, s3x, s3y)                                          \
   ((((unsigned)(s0x)&0xf) << 0) | (((unsigned)(s0y)&0xf) << 4) | (((unsigned)(s1x)&0xf) << 8) |   \
    (((unsigned)(s1y)&0xf) << 12) | (((unsigned)(s2x)&0xf) << 16) |                                \
    (((unsigned)(s2y)&0xf) << 20) | (((unsigned)(s3x)&0xf) << 24) | (((unsigned)(s3y)&0xf) << 28))

/* For obtaining location coordinates from registers */
#define SEXT4(x)               ((int)((x) | ((x)&0x8 ? 0xfffffff0 : 0)))
#define GET_SFIELD(reg, index) SEXT4(((reg) >> ((index)*4)) & 0xf)
#define GET_SX(reg, index)     GET_SFIELD((reg)[(index) / 4], ((index) % 4) * 2)
#define GET_SY(reg, index)     GET_SFIELD((reg)[(index) / 4], ((index) % 4) * 2 + 1)

/* The following sample ordering is required by EQAA.
 *
 * Sample 0 is approx. in the top-left quadrant.
 * Sample 1 is approx. in the bottom-right quadrant.
 *
 * Sample 2 is approx. in the bottom-left quadrant.
 * Sample 3 is approx. in the top-right quadrant.
 * (sample I={2,3} adds more detail to the vicinity of sample I-2)
 *
 * Sample 4 is approx. in the same quadrant as sample 0. (top-left)
 * Sample 5 is approx. in the same quadrant as sample 1. (bottom-right)
 * Sample 6 is approx. in the same quadrant as sample 2. (bottom-left)
 * Sample 7 is approx. in the same quadrant as sample 3. (top-right)
 * (sample I={4,5,6,7} adds more detail to the vicinity of sample I-4)
 *
 * The next 8 samples add more detail to the vicinity of the previous samples.
 * (sample I (I >= 8) adds more detail to the vicinity of sample I-8)
 *
 * The ordering is specified such that:
 *   If we take the first 2 samples, we should get good 2x MSAA.
 *   If we add 2 more samples, we should get good 4x MSAA with the same sample locations.
 *   If we add 4 more samples, we should get good 8x MSAA with the same sample locations.
 *   If we add 8 more samples, we should get perfect 16x MSAA with the same sample locations.
 *
 * The ordering also allows finding samples in the same vicinity.
 *
 * Group N of 2 samples in the same vicinity in 16x MSAA: {N,N+8}
 * Group N of 2 samples in the same vicinity in 8x MSAA: {N,N+4}
 * Group N of 2 samples in the same vicinity in 4x MSAA: {N,N+2}
 *
 * Groups of 4 samples in the same vicinity in 16x MSAA:
 *   Top left:     {0,4,8,12}
 *   Bottom right: {1,5,9,13}
 *   Bottom left:  {2,6,10,14}
 *   Top right:    {3,7,11,15}
 *
 * Groups of 4 samples in the same vicinity in 8x MSAA:
 *   Left half:  {0,2,4,6}
 *   Right half: {1,3,5,7}
 *
 * Groups of 8 samples in the same vicinity in 16x MSAA:
 *   Left half:  {0,2,4,6,8,10,12,14}
 *   Right half: {1,3,5,7,9,11,13,15}
 */

/* Important note: We have to use the standard DX positions, because
 * the primitive discard compute shader relies on them.
 */

/* 1x MSAA */
static const uint32_t sample_locs_1x =
   FILL_SREG(0, 0, 0, 0, 0, 0, 0, 0); /* S1, S2, S3 fields are not used by 1x */
static const uint64_t centroid_priority_1x = 0x0000000000000000ull;

/* 2x MSAA (the positions are sorted for EQAA) */
static const uint32_t sample_locs_2x =
   FILL_SREG(-4, -4, 4, 4, 0, 0, 0, 0); /* S2 & S3 fields are not used by 2x MSAA */
static const uint64_t centroid_priority_2x = 0x1010101010101010ull;

/* 4x MSAA (the positions are sorted for EQAA) */
static const uint32_t sample_locs_4x = FILL_SREG(-2, -6, 2, 6, -6, 2, 6, -2);
static const uint64_t centroid_priority_4x = 0x3210321032103210ull;

/* 8x MSAA (the positions are sorted for EQAA) */
static const uint32_t sample_locs_8x[] = {
   FILL_SREG(-3, -5, 5, 1, -1, 3, 7, -7),
   FILL_SREG(-7, -1, 3, 7, -5, 5, 1, -3),
   /* The following are unused by hardware, but we emit them to IBs
    * instead of multiple SET_CONTEXT_REG packets. */
   0,
   0,
};
static const uint64_t centroid_priority_8x = 0x3546012735460127ull;

/* 16x MSAA (the positions are sorted for EQAA) */
static const uint32_t sample_locs_16x[] = {
   FILL_SREG(-5, -2, 5, 3, -2, 6, 3, -5),
   FILL_SREG(-4, -6, 1, 1, -6, 4, 7, -4),
   FILL_SREG(-1, -3, 6, 7, -3, 2, 0, -7),
   FILL_SREG(-7, -8, 2, 5, -8, 0, 4, -1),
};
static const uint64_t centroid_priority_16x = 0xc97e64b231d0fa85ull;

static void si_get_sample_position(struct pipe_context *ctx, unsigned sample_count,
                                   unsigned sample_index, float *out_value)
{
   const uint32_t *sample_locs;

   switch (sample_count) {
   case 1:
   default:
      sample_locs = &sample_locs_1x;
      break;
   case 2:
      sample_locs = &sample_locs_2x;
      break;
   case 4:
      sample_locs = &sample_locs_4x;
      break;
   case 8:
      sample_locs = sample_locs_8x;
      break;
   case 16:
      sample_locs = sample_locs_16x;
      break;
   }

   out_value[0] = (GET_SX(sample_locs, sample_index) + 8) / 16.0f;
   out_value[1] = (GET_SY(sample_locs, sample_index) + 8) / 16.0f;
}

static void si_emit_max_4_sample_locs(struct radeon_cmdbuf *cs, uint64_t centroid_priority,
                                      uint32_t sample_locs)
{
   radeon_set_context_reg_seq(cs, R_028BD4_PA_SC_CENTROID_PRIORITY_0, 2);
   radeon_emit(cs, centroid_priority);
   radeon_emit(cs, centroid_priority >> 32);
   radeon_set_context_reg(cs, R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, sample_locs);
   radeon_set_context_reg(cs, R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, sample_locs);
   radeon_set_context_reg(cs, R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, sample_locs);
   radeon_set_context_reg(cs, R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, sample_locs);
}

static void si_emit_max_16_sample_locs(struct radeon_cmdbuf *cs, uint64_t centroid_priority,
                                       const uint32_t *sample_locs, unsigned num_samples)
{
   radeon_set_context_reg_seq(cs, R_028BD4_PA_SC_CENTROID_PRIORITY_0, 2);
   radeon_emit(cs, centroid_priority);
   radeon_emit(cs, centroid_priority >> 32);
   radeon_set_context_reg_seq(cs, R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0,
                              num_samples == 8 ? 14 : 16);
   radeon_emit_array(cs, sample_locs, 4);
   radeon_emit_array(cs, sample_locs, 4);
   radeon_emit_array(cs, sample_locs, 4);
   radeon_emit_array(cs, sample_locs, num_samples == 8 ? 2 : 4);
}

void si_emit_sample_locations(struct radeon_cmdbuf *cs, int nr_samples)
{
   switch (nr_samples) {
   default:
   case 1:
      si_emit_max_4_sample_locs(cs, centroid_priority_1x, sample_locs_1x);
      break;
   case 2:
      si_emit_max_4_sample_locs(cs, centroid_priority_2x, sample_locs_2x);
      break;
   case 4:
      si_emit_max_4_sample_locs(cs, centroid_priority_4x, sample_locs_4x);
      break;
   case 8:
      si_emit_max_16_sample_locs(cs, centroid_priority_8x, sample_locs_8x, 8);
      break;
   case 16:
      si_emit_max_16_sample_locs(cs, centroid_priority_16x, sample_locs_16x, 16);
      break;
   }
}

void si_init_msaa_functions(struct si_context *sctx)
{
   int i;

   sctx->b.get_sample_position = si_get_sample_position;

   si_get_sample_position(&sctx->b, 1, 0, sctx->sample_positions.x1[0]);

   for (i = 0; i < 2; i++)
      si_get_sample_position(&sctx->b, 2, i, sctx->sample_positions.x2[i]);
   for (i = 0; i < 4; i++)
      si_get_sample_position(&sctx->b, 4, i, sctx->sample_positions.x4[i]);
   for (i = 0; i < 8; i++)
      si_get_sample_position(&sctx->b, 8, i, sctx->sample_positions.x8[i]);
   for (i = 0; i < 16; i++)
      si_get_sample_position(&sctx->b, 16, i, sctx->sample_positions.x16[i]);
}
