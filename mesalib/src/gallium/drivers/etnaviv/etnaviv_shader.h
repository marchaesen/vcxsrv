/*
 * Copyright (c) 2012-2015 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Wladimir J. van der Laan <laanwj@gmail.com>
 */

#ifndef H_ETNAVIV_SHADER
#define H_ETNAVIV_SHADER

#include "pipe/p_state.h"
#include "util/disk_cache.h"

struct etna_context;
struct etna_shader_variant;
struct nir_shader;

struct etna_shader_key
{
   union {
      struct {
         /*
          * Combined Vertex/Fragment shader parameters:
          */

         /* do we need to swap rb in frag color? */
         unsigned frag_rb_swap : 1;
         /* do we need to invert front facing value? */
         unsigned front_ccw : 1;
      };
      uint32_t global;
   };
};

static inline bool
etna_shader_key_equal(struct etna_shader_key *a, struct etna_shader_key *b)
{
   STATIC_ASSERT(sizeof(struct etna_shader_key) <= sizeof(a->global));

   return a->global == b->global;
}

struct etna_shader {
   /* shader id (for debug): */
   uint32_t id;
   uint32_t variant_count;

   struct tgsi_token *tokens;
   struct nir_shader *nir;
   const struct etna_specs *specs;
   struct etna_compiler *compiler;

   struct etna_shader_variant *variants;

   cache_key cache_key;     /* shader disk-cache key */
};

bool
etna_shader_link(struct etna_context *ctx);

bool
etna_shader_update_vertex(struct etna_context *ctx);

struct etna_shader_variant *
etna_shader_variant(struct etna_shader *shader, struct etna_shader_key key,
                   struct pipe_debug_callback *debug);

void
etna_shader_init(struct pipe_context *pctx);

#endif
