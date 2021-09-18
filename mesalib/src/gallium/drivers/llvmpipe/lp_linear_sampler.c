/**************************************************************************
 *
 * Copyright 2010-2021 VMware, Inc.
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
 **************************************************************************/


#include "pipe/p_config.h"

#include "util/u_math.h"
#include "util/u_cpu_detect.h"
#include "util/u_pack_color.h"
#include "util/u_rect.h"
#include "util/u_sse.h"

#include "lp_jit.h"
#include "lp_debug.h"
#include "lp_state_fs.h"
#include "lp_linear_priv.h"

#if defined(PIPE_ARCH_SSE)

#define FIXED16_SHIFT  16
#define FIXED16_ONE    (1<<16)
#define FIXED16_HALF   (1<<15)

/*
 * Color tolerance.  Allow 1 bit of error in 8 bit unorm colors.
 */
#define FIXED16_TOL (FIXED16_ONE >> 7)

/*
 * Tolerance for texture coordinate derivatives when doing linear filtering.
 *
 * (Note that extra care needs to be taken when doing linear filtering as
 * coordinates may snap up to neighbour texels inside the tile).
 */
#define FIXED16_TOL_DERIV (FIXED16_TOL / TILE_SIZE)

static inline int
float_to_fixed16(float f)
{
   return f * (float)FIXED16_ONE;
}

static inline int
fixed16_frac(int x)
{
   return x & (FIXED16_ONE - 1);
}

static inline int
fixed16_approx(int x, int y, int tol)
{
   return y - tol <= x && x <= y + tol;
}


/*
 * Unstretched blit of a bgra texture.
 */
static const uint32_t *
fetch_bgra_memcpy(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   const struct lp_jit_texture *texture = samp->texture;
   const uint32_t *src_row =
      (const uint32_t *)((const uint8_t *)texture->base +
                         (samp->t >> FIXED16_SHIFT) * texture->row_stride[0]);
   const int s     = samp->s;
   const int width = samp->width;
   const uint32_t *row;

   src_row = &src_row[s >> FIXED16_SHIFT];

   if (((uintptr_t)src_row & 0xf) == 0) {
      /* The source texels are already aligned. Return them */
      row = src_row;
   } else {
      memcpy(samp->row, src_row, width * sizeof *row);
      row = samp->row;
   }

   samp->t += samp->dtdy;
   return row;
}


/*
 * Unstretched blit of a bgrx texture.
 */
static const uint32_t *
fetch_bgrx_memcpy(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   const struct lp_jit_texture *texture = samp->texture;
   const uint32_t *src_row =
      (const uint32_t *)((const uint8_t *)texture->base +
                         (samp->t >> FIXED16_SHIFT) * texture->row_stride[0]);
   const int s     = samp->s;
   const int width = samp->width;
   uint32_t *row   = samp->row;
   int i;

   src_row = &src_row[s >> FIXED16_SHIFT];

   for (i = 0; i < width; i++) {
      row[i] = src_row[i] | 0xff000000;
   }

   samp->t += samp->dtdy;
   return row;
}


/*
 * Perform nearest filtered lookup of a row of texels.  Texture lookup
 * is assumed to be axis aligned but with arbitrary scaling.
 *
 * Texture coordinate interpolation is performed in 16.16 fixed point,
 * not to be confused with the 1.15 format used by the interpolants.
 *
 * After 64 pixels (ie. in the next tile), the starting point will be
 * recalculated with floating point arithmetic.
 */
static const uint32_t *
fetch_bgra_axis_aligned(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   const struct lp_jit_texture *texture = samp->texture;
   const uint32_t *src_row =
      (const uint32_t *)((const uint8_t *)texture->base +
                         (samp->t >> FIXED16_SHIFT) * texture->row_stride[0]);
   const int dsdx  = samp->dsdx;
   const int width = samp->width;
   uint32_t *row   = samp->row;
   int s = samp->s;
   int i;

   for (i = 0; i < width; i++) {
      row[i] = src_row[s>>FIXED16_SHIFT];
      s += dsdx;
   }

   samp->t += samp->dtdy;
   return row;
}

static const uint32_t *
fetch_bgrx_axis_aligned(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   const struct lp_jit_texture *texture = samp->texture;
   const uint32_t *src_row =
      (const uint32_t *)((const uint8_t *)texture->base +
                         (samp->t >> FIXED16_SHIFT) * texture->row_stride[0]);
   const int dsdx  = samp->dsdx;
   const int width = samp->width;
   uint32_t *row   = samp->row;
   int s = samp->s;
   int i;

   for (i = 0; i < width; i++) {
      row[i] = src_row[s>>FIXED16_SHIFT] | 0xff000000;
      s += dsdx;
   }

   samp->t += samp->dtdy;
   return row;
}

/* Non-axis aligned, but no clamping or wrapping required
 */
static const uint32_t *
fetch_bgra(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   const struct lp_jit_texture *texture = samp->texture;
   const uint8_t *src = texture->base;
   const int stride = texture->row_stride[0];
   const int dsdx  = samp->dsdx;
   const int dtdx  = samp->dtdx;
   const int width = samp->width;
   uint32_t *row   = samp->row;
   int s = samp->s;
   int t = samp->t;
   int i;

   for (i = 0; i < width; i++) {
      const uint8_t *texel = (src +
                              (t>>FIXED16_SHIFT) * stride +
                              (s>>FIXED16_SHIFT) * 4);

      row[i] = *(const uint32_t *)texel;

      s += dsdx;
      t += dtdx;
   }

   samp->s += samp->dsdy;
   samp->t += samp->dtdy;
   return row;
}


static const uint32_t *
fetch_bgrx(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   const struct lp_jit_texture *texture = samp->texture;
   const uint8_t *src = texture->base;
   const int stride = texture->row_stride[0];
   const int dsdx  = samp->dsdx;
   const int dtdx  = samp->dtdx;
   const int width = samp->width;
   uint32_t *row   = samp->row;
   int s = samp->s;
   int t = samp->t;
   int i;

   for (i = 0; i < width; i++) {
      const uint8_t *texel = (src +
                              (t>>FIXED16_SHIFT) * stride +
                              (s>>FIXED16_SHIFT) * 4);

      row[i] = (*(const uint32_t *)texel) | 0xff000000;

      s += dsdx;
      t += dtdx;
   }

   samp->s += samp->dsdy;
   samp->t += samp->dtdy;
   return row;
}

/* Non-axis aligned, clamped.
 */
static const uint32_t *
fetch_bgra_clamp(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   const struct lp_jit_texture *texture = samp->texture;
   const uint8_t *src   = texture->base;
   const int stride     = texture->row_stride[0];
   const int tex_height = texture->height - 1;
   const int tex_width  = texture->width - 1;
   const int dsdx  = samp->dsdx;
   const int dtdx  = samp->dtdx;
   const int width = samp->width;
   uint32_t *row   = samp->row;
   int s = samp->s;
   int t = samp->t;
   int i;

   for (i = 0; i < width; i++) {
      int ct = CLAMP(t>>FIXED16_SHIFT, 0, tex_height);
      int cs = CLAMP(s>>FIXED16_SHIFT, 0, tex_width);

      const uint8_t *texel = (src +
                              ct * stride +
                              cs * 4);

      row[i] = *(const uint32_t *)texel;

      s += dsdx;
      t += dtdx;
   }

   samp->s += samp->dsdy;
   samp->t += samp->dtdy;
   return row;
}

static const uint32_t *
fetch_bgrx_clamp(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   const struct lp_jit_texture *texture = samp->texture;
   const uint8_t *src   = texture->base;
   const int stride     = texture->row_stride[0];
   const int tex_height = texture->height - 1;
   const int tex_width  = texture->width - 1;
   const int dsdx  = samp->dsdx;
   const int dtdx  = samp->dtdx;
   const int width = samp->width;
   uint32_t *row   = samp->row;
   int s = samp->s;
   int t = samp->t;
   int i;

   for (i = 0; i < width; i++) {
      int ct = CLAMP(t>>FIXED16_SHIFT, 0, tex_height);
      int cs = CLAMP(s>>FIXED16_SHIFT, 0, tex_width);

      const uint8_t *texel = (src +
                              ct * stride +
                              cs * 4);

      row[i] = (*(const uint32_t *)texel) | 0xff000000;

      s += dsdx;
      t += dtdx;
   }

   samp->s += samp->dsdy;
   samp->t += samp->dtdy;
   return row;
}

/**
 * Fetch and stretch one row.
 */
static inline const uint32_t *
fetch_and_stretch_bgra_row(struct lp_linear_sampler *samp,
                           int y)
{
   const struct lp_jit_texture *texture = samp->texture;
   const uint32_t *data = (const uint32_t *)texture->base;
   const int stride = texture->row_stride[0] / sizeof(uint32_t);
   const uint32_t * restrict src_row;
   uint32_t * restrict dst_row;
   const int width = samp->width;

   /*
    * Search the stretched row cache first.
    */

   if (y == samp->stretched_row_y[0]) {
      samp->stretched_row_index = 1;
      return samp->stretched_row[0];
   }

   if (y == samp->stretched_row_y[1]) {
      samp->stretched_row_index = 0;
      return samp->stretched_row[1];
   }

   /*
    * Replace one entry.
    */

   src_row = data + y * stride;

   dst_row = samp->stretched_row[samp->stretched_row_index];

   if (fixed16_frac(samp->s) == 0 &&
       samp->dsdx == FIXED16_ONE) { // TODO: could be relaxed
      /*
       * 1:1 blit on the x direction.
       */

      unsigned i;

      src_row += samp->s >> FIXED16_SHIFT;

      if (((uintptr_t)src_row & 0xf) == 0) {
         /* The source texture is already aligned. Return it */
         return src_row;
      }

      /* Copy the source texture */
      for (i = 0; i < width; i += 4) {
         __m128i src = _mm_loadu_si128((const __m128i *)&src_row[i]);
         *(__m128i *)&dst_row[i] = src;
      }
   }
   else {
      util_sse2_stretch_row_8unorm((__m128i *)dst_row,
                                   align(width, 4),
                                   src_row, samp->s, samp->dsdx);
   }

   samp->stretched_row_y[samp->stretched_row_index] = y;
   samp->stretched_row_index ^= 1;

   return dst_row;
}

/* Maximise only as we fetch unscaled pixels linearly into a size-64
 * temporary.  For minimise, we will want to either have a bigger
 * temporary or fetch sparsely.
 */
static const uint32_t *
fetch_bgra_axis_aligned_linear(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   const int width = samp->width;
   const uint32_t * restrict src_row0;
   const uint32_t * restrict src_row1;
   uint32_t * restrict row = samp->row;
   int y = samp->t >> FIXED16_SHIFT;
   int w = (samp->t >> 8) & 0xff;
   int i;
   __m128i wt;

   samp->t += samp->dtdy;

   src_row0 = fetch_and_stretch_bgra_row(samp, y);

   if (w == 0) {
      return src_row0;
   }

   src_row1 = fetch_and_stretch_bgra_row(samp, y + 1);

   wt = _mm_set1_epi16(w);

   /* Combine the two rows using a constant weight.
    */
   for (i = 0; i < width; i += 4) {
      __m128i srca = _mm_load_si128((const __m128i *)&src_row0[i]);
      __m128i srcb = _mm_load_si128((const __m128i *)&src_row1[i]);

      *(__m128i *)&row[i] = util_sse2_lerp_epi8_fixed88(srca, srcb, &wt, &wt);
   }

   return row;
}

/* Non-axis-aligned version.  Don't try to take advantage of
 * maximize.
 */
static const uint32_t *
fetch_bgra_linear(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   const struct lp_jit_texture *texture = samp->texture;
   const int stride     = texture->row_stride[0] / sizeof(uint32_t);
   const uint32_t *data  = (const uint32_t *)texture->base;
   const int dsdx  = samp->dsdx;
   const int dtdx  = samp->dtdx;
   const int width = samp->width;
   uint32_t *row   = samp->row;
   int s = samp->s;
   int t = samp->t;
   int i, j;

   for (i = 0; i < width; i += 4) {
      union m128i si0, si1, si2, si3, ws, wt;
      __m128i si02, si13;

      for (j = 0; j < 4; j++) {
         const uint32_t *src = data + (t >> 16) * stride + (s>>16);

         si0.ui[j] = src[0];
         si1.ui[j] = src[1];
         si2.ui[j] = src[stride + 0];
         si3.ui[j] = src[stride + 1];

         ws.ui[j] = (s>>8) & 0xff;
         wt.ui[j] = (t>>8) & 0xff;

         s += dsdx;
         t += dtdx;
      }

      ws.m = _mm_or_si128(ws.m, _mm_slli_epi32(ws.m, 16));
      ws.m = _mm_or_si128(ws.m, _mm_slli_epi32(ws.m, 8));

      wt.m = _mm_or_si128(wt.m, _mm_slli_epi32(wt.m, 16));
      wt.m = _mm_or_si128(wt.m, _mm_slli_epi32(wt.m, 8));

      si02 = util_sse2_lerp_epi8_fixed08(si0.m, si2.m, wt.m);
      si13 = util_sse2_lerp_epi8_fixed08(si1.m, si3.m, wt.m);

      *(__m128i *)&row[i] = util_sse2_lerp_epi8_fixed08(si02, si13, ws.m);
   }

   samp->s += samp->dsdy;
   samp->t += samp->dtdy;
   return row;
}


/* Clamped, non-axis-aligned version.  Don't try to take advantage of
 * maximize.
 */
static const uint32_t *
fetch_bgra_clamp_linear(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   const struct lp_jit_texture *texture = samp->texture;
   const uint32_t *data  = (const uint32_t *)texture->base;
   const int stride     = texture->row_stride[0] / sizeof(uint32_t);
   const int tex_height = texture->height - 1;
   const int tex_width  = texture->width - 1;
   const int dsdx  = samp->dsdx;
   const int dtdx  = samp->dtdx;
   const int width = samp->width;
   uint32_t *row   = samp->row;
   int s = samp->s;
   int t = samp->t;
   int i, j;
   /* width, height, stride (in pixels) must be smaller than 32768 */
   __m128i dsdx4, dtdx4, s4, t4, stride4, w4, h4, zero, one;
   s4 = _mm_set1_epi32(s);
   t4 = _mm_set1_epi32(t);
   s4 = _mm_add_epi32(s4, _mm_set_epi32(3*dsdx, 2*dsdx, dsdx, 0));
   t4 =  _mm_add_epi32(t4, _mm_set_epi32(3*dtdx, 2*dtdx, dtdx, 0));
   dsdx4 = _mm_set1_epi32(4*dsdx);
   dtdx4 = _mm_set1_epi32(4*dtdx);
   stride4 = _mm_set1_epi32(stride);
   w4 = _mm_set1_epi32(tex_width);
   h4 = _mm_set1_epi32(tex_height);
   zero = _mm_setzero_si128();
   one = _mm_set1_epi32(1);

   for (i = 0; i < width; i += 4) {
      union m128i addr[4];
      __m128i ws, wt, wsl, wsh, wtl, wth;
      __m128i s4s, t4s, cs0, cs1, ct0, ct1, tmp, si[4];

      s4s = _mm_srli_epi32(s4, 16);
      t4s = _mm_srli_epi32(t4, 16);
      cs0 = _mm_min_epi16(_mm_max_epi16(s4s, zero), w4);
      cs1 = _mm_add_epi16(s4s, one);
      cs1 = _mm_min_epi16(_mm_max_epi16(cs1, zero), w4);
      ct0 = _mm_min_epi16(_mm_max_epi16(t4s, zero), h4);
      ct1 = _mm_add_epi16(t4s, one);
      ct1 = _mm_min_epi16(_mm_max_epi16(ct1, zero), h4);
      tmp = _mm_madd_epi16(ct0, stride4);
      addr[0].m = _mm_add_epi32(tmp, cs0);
      addr[1].m = _mm_add_epi32(tmp, cs1);
      tmp = _mm_madd_epi16(ct1, stride4);
      addr[2].m = _mm_add_epi32(tmp, cs0);
      addr[3].m = _mm_add_epi32(tmp, cs1);

      for (j = 0; j < 4; j++) {
         __m128i ld1, ld2, ld3;
         si[j] = _mm_cvtsi32_si128(data[addr[j].ui[0]]);
         ld1 = _mm_cvtsi32_si128(data[addr[j].ui[1]]);
         si[j] = _mm_unpacklo_epi32(si[j], ld1);
         ld2 = _mm_cvtsi32_si128(data[addr[j].ui[2]]);
         ld3 = _mm_cvtsi32_si128(data[addr[j].ui[3]]);
         ld2 = _mm_unpacklo_epi32(ld2, ld3);
         si[j] =  _mm_unpacklo_epi64(si[j], ld2);
      }

      ws = _mm_srli_epi32(s4, 8);
      ws = _mm_and_si128(ws, _mm_set1_epi32(0xFF));
      wt = _mm_srli_epi32(t4, 8);
      wt = _mm_and_si128(wt, _mm_set1_epi32(0xFF));

      s4 = _mm_add_epi32(s4, dsdx4);
      t4 = _mm_add_epi32(t4, dtdx4);

#if 0
/* scalar code for reference */
      for (j = 0; j < 4; j++) {
         int s0 = s >> FIXED16_SHIFT;
         int t0 = t >> FIXED16_SHIFT;
         int cs0 = CLAMP(s0    , 0, tex_width);
         int cs1 = CLAMP(s0 + 1, 0, tex_width);
         int ct0 = CLAMP(t0    , 0, tex_height);
         int ct1 = CLAMP(t0 + 1, 0, tex_height);

         si0.ui[j] = data[ct0 * stride + cs0];
         si1.ui[j] = data[ct0 * stride + cs1];
         si2.ui[j] = data[ct1 * stride + cs0];
         si3.ui[j] = data[ct1 * stride + cs1];

         ws.ui[j] = (s>>8) & 0xff;
         wt.ui[j] = (t>>8) & 0xff;

         s += dsdx;
         t += dtdx;
      }
#endif

      ws = _mm_or_si128(ws, _mm_slli_epi32(ws, 16));
      wsl = _mm_shuffle_epi32(ws, _MM_SHUFFLE(1,1,0,0));
      wsh = _mm_shuffle_epi32(ws, _MM_SHUFFLE(3,3,2,2));

      wt = _mm_or_si128(wt, _mm_slli_epi32(wt, 16));
      wtl = _mm_shuffle_epi32(wt, _MM_SHUFFLE(1,1,0,0));
      wth = _mm_shuffle_epi32(wt, _MM_SHUFFLE(3,3,2,2));

      *(__m128i *)&row[i] = util_sse2_lerp_2d_epi8_fixed88(si[0], si[2],
                                                           &si[1], &si[3],
                                                           &wtl, &wth,
                                                           &wsl, &wsh);
   }

   samp->s += samp->dsdy;
   samp->t += samp->dtdy;
   return row;
}

static const uint32_t *
fetch_bgrx_axis_aligned_linear(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   const __m128i mask = _mm_set1_epi32(0xff000000);
   uint32_t *dst_row = samp->row;
   const uint32_t *src_row;
   int width = samp->width;
   int i;

   src_row = fetch_bgra_axis_aligned_linear(&samp->base);

   for (i = 0; i < width; i += 4) {
      __m128i bgra = *(__m128i *)&src_row[i];
      __m128i bgrx = _mm_or_si128(bgra, mask);
      *(__m128i *)&dst_row[i] = bgrx;
   }

   return dst_row;
}


static const uint32_t *
fetch_bgrx_clamp_linear(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   const __m128i mask = _mm_set1_epi32(0xff000000);
   uint32_t *row   = samp->row;
   int width = samp->width;
   int i;

   fetch_bgra_clamp_linear(&samp->base);

   for (i = 0; i < width; i += 4) {
      __m128i bgra = *(__m128i *)&row[i];
      __m128i bgrx = _mm_or_si128(bgra, mask);
      *(__m128i *)&row[i] = bgrx;
   }

   return row;
}


static const uint32_t *
fetch_bgrx_linear(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   const __m128i mask = _mm_set1_epi32(0xff000000);
   uint32_t *row   = samp->row;
   int width = samp->width;
   int i;

   fetch_bgra_linear(&samp->base);

   for (i = 0; i < width; i += 4) {
      __m128i bgra = *(__m128i *)&row[i];
      __m128i bgrx = _mm_or_si128(bgra, mask);
      *(__m128i *)&row[i] = bgrx;
   }

   return row;
}


static boolean
sampler_is_nearest(const struct lp_linear_sampler *samp,
                   const struct lp_sampler_static_state *sampler_state,
                   boolean minify)
{
   unsigned img_filter;

   if (minify)
      img_filter = sampler_state->sampler_state.min_img_filter;
   else
      img_filter = sampler_state->sampler_state.mag_img_filter;

   /* Is it obviously nearest?
    */
   if (img_filter == PIPE_TEX_FILTER_NEAREST)
      return TRUE;

   /* Otherwise look for linear samplers which devolve to nearest.
    */

   /* Needs to be axis aligned.
    */
   if (!samp->axis_aligned)
      return FALSE;

   if (0) {
      /* For maximizing shaders, revert to nearest
       */
      if (samp->dsdx < -FIXED16_HALF && samp->dsdx < FIXED16_HALF &&
          samp->dtdy < -FIXED16_HALF && samp->dtdy < FIXED16_HALF)
         return TRUE;

      /* For severely minimising shaders, revert to nearest:
       */
      if ((samp->dsdx < 2 * FIXED16_ONE || samp->dsdx > 2 * FIXED16_ONE) &&
          (samp->dtdy < 2 * FIXED16_ONE || samp->dtdy > 2 * FIXED16_ONE))
         return TRUE;
   }

   /*
    * Must be near a pixel center:
    */
   if (!fixed16_approx(fixed16_frac(samp->s), FIXED16_HALF, FIXED16_TOL) ||
       !fixed16_approx(fixed16_frac(samp->t), FIXED16_HALF, FIXED16_TOL))
      return FALSE;

   /*
    * Must make a full step between pixels:
    */
   if (!fixed16_approx(samp->dsdx, FIXED16_ONE, FIXED16_TOL_DERIV) ||
       !fixed16_approx(samp->dtdy, FIXED16_ONE, FIXED16_TOL_DERIV))
      return FALSE;

   /* Treat it as nearest!
    */
   return TRUE;
}

/* XXX: Lots of static-state parameters being passed in here but very
 * little info is extracted from each one.  Consolidate it all down to
 * something succinct in the prepare phase?
 */
boolean
lp_linear_init_sampler(struct lp_linear_sampler *samp,
                       const struct lp_tgsi_texture_info *info,
                       const struct lp_sampler_static_state *sampler_state,
                       const struct lp_jit_texture *texture,
                       int x0, int y0, int width, int height,
                       const float (*a0)[4],
                       const float (*dadx)[4],
                       const float (*dady)[4])
{
   const struct lp_tgsi_channel_info *schan = &info->coord[0];
   const struct lp_tgsi_channel_info *tchan = &info->coord[1];

   float w0   =   a0[0][3];

   float s0   =   a0[schan->u.index+1][schan->swizzle];
   float dsdx = dadx[schan->u.index+1][schan->swizzle];
   float dsdy = dady[schan->u.index+1][schan->swizzle];

   float t0   =   a0[tchan->u.index+1][tchan->swizzle];
   float dtdx = dadx[tchan->u.index+1][tchan->swizzle];
   float dtdy = dady[tchan->u.index+1][tchan->swizzle];

   int mins, mint, maxs, maxt;
   float oow = 1.0f / w0;
   float width_oow = texture->width * oow;
   float height_oow = texture->height * oow;
   float fdsdx = dsdx * width_oow;
   float fdsdy = dsdy * width_oow;
   float fdtdx = dtdx * height_oow;
   float fdtdy = dtdy * height_oow;
   int fetch_width;
   int fetch_height;
   boolean minify;
   boolean need_wrap;
   boolean is_nearest;

   samp->texture = texture;
   samp->width = width;

   samp->s = float_to_fixed16(fdsdx * x0 +
                              fdsdy * y0 +
                              s0 * width_oow);

   samp->t = float_to_fixed16(fdtdx * x0 +
                              fdtdy * y0 +
                              t0 * height_oow);

   samp->dsdx = float_to_fixed16(fdsdx);
   samp->dsdy = float_to_fixed16(fdsdy);
   samp->dtdx = float_to_fixed16(fdtdx);
   samp->dtdy = float_to_fixed16(fdtdy);


   samp->axis_aligned = (samp->dsdy == 0 &&
                         samp->dtdx == 0); // TODO: could be relaxed

   {
      int dsdx = samp->dsdx >= 0 ? samp->dsdx : -samp->dsdx;
      int dsdy = samp->dsdy >= 0 ? samp->dsdy : -samp->dsdy;
      int dtdx = samp->dtdx >= 0 ? samp->dtdx : -samp->dtdx;
      int dtdy = samp->dtdy >= 0 ? samp->dtdy : -samp->dtdy;
      int rho = MAX4(dsdx, dsdy, dtdx, dtdy);

      minify = (rho > FIXED16_ONE);
   }

   is_nearest = sampler_is_nearest(samp, sampler_state, minify);

   if (!is_nearest) {
      samp->s -= FIXED16_HALF;
      samp->t -= FIXED16_HALF;
   }

   /* Check for clamping.  This rarely happens as we're rejecting interpolants
    * which fall outside the 0..1 range.
    */

   if (is_nearest) {
      /* Nearest fetch routines don't employ SSE and always operate one pixel
       * at a time.
       */
      fetch_width = width - 1;
   }
   else {
      /* Linear fetch routines employ SSE, and always fetch groups of four
       * texels.
       */
      fetch_width = align(width, 4) - 1;
   }
   fetch_height = height - 1;

   if (samp->axis_aligned) {
      int s0 = samp->s;
      int s1 = samp->s + fetch_width  * samp->dsdx;
      int t0 = samp->t;
      int t1 = samp->t + fetch_height * samp->dtdy;

      mins = MIN2(s0, s1);
      mint = MIN2(t0, t1);
      maxs = MAX2(s0, s1);
      maxt = MAX2(t0, t1);
   }
   else {
      int s0 = samp->s;
      int s1 = samp->s + fetch_width  * samp->dsdx;
      int s2 = samp->s + fetch_height * samp->dsdy;
      int s3 = samp->s + fetch_width  * samp->dsdx + fetch_height * samp->dsdy;
      int t0 = samp->t;
      int t1 = samp->t + fetch_width  * samp->dtdx;
      int t2 = samp->t + fetch_height * samp->dtdy;
      int t3 = samp->t + fetch_width  * samp->dtdx + fetch_height * samp->dtdy;

      mins = MIN4(s0, s1, s2, s3);
      mint = MIN4(t0, t1, t2, t3);
      maxs = MAX4(s0, s1, s2, s3);
      maxt = MAX4(t0, t1, t2, t3);
   }

   if (is_nearest) {
      need_wrap = (mins < 0 ||
                   mint < 0 ||
                   maxs >= (texture->width  << FIXED16_SHIFT) ||
                   maxt >= (texture->height << FIXED16_SHIFT));
   } else {
      need_wrap = (mins < 0 ||
                   mint < 0 ||
                   maxs + FIXED16_ONE >= (texture->width  << FIXED16_SHIFT) ||
                   maxt + FIXED16_ONE >= (texture->height << FIXED16_SHIFT));
   }

   if (0 && need_wrap) {
      debug_printf("%u x %u %s\n",
                   texture->width, texture->height,
                   is_nearest ? "nearest" : "linear");
      debug_printf("mins = %f\n", mins*1.0f/FIXED16_ONE);
      debug_printf("mint = %f\n", mint*1.0f/FIXED16_ONE);
      debug_printf("maxs = %f\n", maxs*1.0f/FIXED16_ONE);
      debug_printf("maxt = %f\n", maxt*1.0f/FIXED16_ONE);
      debug_printf("\n");
   }

   /* We accept any mode below, but we only implement clamping.
    */
   if (need_wrap &&
       (sampler_state->sampler_state.wrap_s != PIPE_TEX_WRAP_CLAMP_TO_EDGE ||
        sampler_state->sampler_state.wrap_t != PIPE_TEX_WRAP_CLAMP_TO_EDGE)) {
       return FALSE;
   }

   if (is_nearest) {
      switch (sampler_state->texture_state.format) {
      case PIPE_FORMAT_B8G8R8A8_UNORM:
         if (need_wrap)
            samp->base.fetch = fetch_bgra_clamp;
         else if (!samp->axis_aligned)
            samp->base.fetch = fetch_bgra;
         else if (samp->dsdx != FIXED16_ONE) // TODO: could be relaxed
            samp->base.fetch = fetch_bgra_axis_aligned;
         else
            samp->base.fetch = fetch_bgra_memcpy;

         return TRUE;

      case PIPE_FORMAT_B8G8R8X8_UNORM:
         if (need_wrap)
            samp->base.fetch = fetch_bgrx_clamp;
         else if (!samp->axis_aligned)
            samp->base.fetch = fetch_bgrx;
         else if (samp->dsdx != FIXED16_ONE) // TODO: could be relaxed
            samp->base.fetch = fetch_bgrx_axis_aligned;
         else
            samp->base.fetch = fetch_bgrx_memcpy;

         return TRUE;

      default:
         break;
      }

      FAIL("unknown format for nearest");
   }
   else {
      samp->stretched_row_y[0] = -1;
      samp->stretched_row_y[1] = -1;
      samp->stretched_row_index = 0;

      switch (sampler_state->texture_state.format) {
      case PIPE_FORMAT_B8G8R8A8_UNORM:
         if (need_wrap)
            samp->base.fetch = fetch_bgra_clamp_linear;
         else if (!samp->axis_aligned)
            samp->base.fetch = fetch_bgra_linear;
         else
            samp->base.fetch = fetch_bgra_axis_aligned_linear;

         return TRUE;

      case PIPE_FORMAT_B8G8R8X8_UNORM:
         if (need_wrap)
            samp->base.fetch = fetch_bgrx_clamp_linear;
         else if (!samp->axis_aligned)
            samp->base.fetch = fetch_bgrx_linear;
         else
            samp->base.fetch = fetch_bgrx_axis_aligned_linear;
         return TRUE;

      default:
         break;
      }

      FAIL("unknown format");
   }
}


static const uint32_t *
fetch_noop(struct lp_linear_elem *elem)
{
   struct lp_linear_sampler *samp = (struct lp_linear_sampler *)elem;
   return samp->row;
}


void
lp_linear_init_noop_sampler(struct lp_linear_sampler *samp)
{
   samp->base.fetch = fetch_noop;
}

/* Check the variant for linear path compatibility.
 */
boolean
lp_linear_check_sampler(const struct lp_sampler_static_state *sampler,
                        const struct lp_tgsi_texture_info *tex)
{
   if (tex->modifier != LP_BLD_TEX_MODIFIER_NONE)
      return FALSE;

   if (tex->target != TGSI_TEXTURE_2D)
      return FALSE;

   if (tex->coord[0].file != TGSI_FILE_INPUT ||
       tex->coord[1].file != TGSI_FILE_INPUT)
      return FALSE;

   /* These are the only sampling modes we support at the moment.
    *
    * Actually we'll accept any mode as we're failing on any
    * interpolant which exceeds 0..1.  Clamping is applied only to
    * avoid invalid reads.
    */
   if (!is_nearest_sampler(sampler) &&
       !is_linear_sampler(sampler))
      return FALSE;

   /* These are the only texture formats we support at the moment
    */
   if (sampler->texture_state.format != PIPE_FORMAT_B8G8R8A8_UNORM &&
       sampler->texture_state.format != PIPE_FORMAT_B8G8R8X8_UNORM)
      return FALSE;

   return TRUE;
}

#else
boolean
lp_linear_check_sampler(const struct lp_sampler_static_state *sampler,
                        const struct lp_tgsi_texture_info *tex)
{
   return FALSE;
}
#endif
