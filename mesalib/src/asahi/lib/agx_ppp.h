/*
 * Copyright 2022 Alyssa Rosenzweig
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef AGX_PPP_H
#define AGX_PPP_H

#include "asahi/lib/agx_pack.h"

/* Opaque structure representing a PPP update */
struct agx_ppp_update {
   uint8_t *head;
   uint64_t gpu_base;
   size_t total_size;

#ifndef NDEBUG
   uint8_t *cpu_base;
#endif
};

static size_t
agx_ppp_update_size(struct AGX_PPP_HEADER *present)
{
   size_t size = AGX_PPP_HEADER_LENGTH;

#define PPP_CASE(x, y) if (present->x) size += AGX_ ## y ##_LENGTH;
   PPP_CASE(fragment_control, FRAGMENT_CONTROL);
   PPP_CASE(fragment_control_2, FRAGMENT_CONTROL_2);
   PPP_CASE(fragment_front_face, FRAGMENT_FACE);
   PPP_CASE(fragment_front_face_2, FRAGMENT_FACE_2);
   PPP_CASE(fragment_front_stencil, FRAGMENT_STENCIL);
   PPP_CASE(fragment_back_face, FRAGMENT_FACE);
   PPP_CASE(fragment_back_face_2, FRAGMENT_FACE_2);
   PPP_CASE(fragment_back_stencil, FRAGMENT_STENCIL);
   PPP_CASE(depth_bias_scissor, DEPTH_BIAS_SCISSOR);
   PPP_CASE(region_clip, REGION_CLIP);
   PPP_CASE(viewport, VIEWPORT);
   PPP_CASE(w_clamp, W_CLAMP);
   PPP_CASE(output_select, OUTPUT_SELECT);
   PPP_CASE(varying_word_0, VARYING_0);
   PPP_CASE(varying_word_1, VARYING_1);
   PPP_CASE(cull, CULL);
   PPP_CASE(cull_2, CULL_2);
   PPP_CASE(fragment_shader, FRAGMENT_SHADER);
   PPP_CASE(occlusion_query, FRAGMENT_OCCLUSION_QUERY);
   PPP_CASE(occlusion_query_2, FRAGMENT_OCCLUSION_QUERY_2);
   PPP_CASE(output_unknown, OUTPUT_UNKNOWN);
   PPP_CASE(output_size, OUTPUT_SIZE);
   PPP_CASE(varying_word_2, VARYING_2);
#undef PPP_CASE

   assert((size % 4) == 0 && "PPP updates are aligned");
   return size;
}

static inline bool
agx_ppp_validate(struct agx_ppp_update *ppp, size_t size)
{
#ifndef NDEBUG
   /* Assert that we don't overflow. Ideally we'd assert that types match too
    * but that's harder to do at the moment.
    */
   assert(((ppp->head - ppp->cpu_base) + size) <= ppp->total_size);
#endif

   return true;
}

#define agx_ppp_push(ppp, T, name) \
   for (bool it = agx_ppp_validate((ppp), AGX_##T##_LENGTH); it; it = false, \
        (ppp)->head += AGX_##T##_LENGTH) \
      agx_pack((ppp)->head, T, name)

#define agx_ppp_push_packed(ppp, src, T) do { \
      agx_ppp_validate((ppp), AGX_##T##_LENGTH); \
      memcpy((ppp)->head, src, AGX_##T##_LENGTH); \
      (ppp)->head += AGX_##T##_LENGTH; \
   } while(0) \

static inline struct agx_ppp_update
agx_new_ppp_update(struct agx_pool *pool, struct AGX_PPP_HEADER present)
{
   size_t size = agx_ppp_update_size(&present);
   struct agx_ptr T = agx_pool_alloc_aligned(pool, size, 64);

   struct agx_ppp_update ppp = {
      .gpu_base = T.gpu,
      .head = T.cpu,
      .total_size = size,
#ifndef NDEBUG
      .cpu_base = T.cpu,
#endif
   };

   agx_ppp_push(&ppp, PPP_HEADER, cfg) { cfg = present; }

   return ppp;
}

static inline void
agx_ppp_fini(uint8_t **out, struct agx_ppp_update *ppp)
{
   size_t size = ppp->total_size;
   assert((size % 4) == 0);
   size_t size_words = size / 4;

#ifndef NDEBUG
   assert(size == (ppp->head - ppp->cpu_base) && "mismatched ppp size");
#endif

   assert(ppp->gpu_base < (1ull << 40));
   assert(size_words < (1ull << 24));

   agx_pack(*out, PPP_STATE, cfg) {
      cfg.pointer_hi = (ppp->gpu_base >> 32);
      cfg.pointer_lo = (uint32_t) ppp->gpu_base;
      cfg.size_words = size_words;
   };

   *out += AGX_PPP_STATE_LENGTH;
}

#endif
