/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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

#include "agx_compiler.h"

/* Manages the uniform file. We can push certain fixed items (during initial
 * code generation), where we're gauranteed to have sufficient space. After
 * that, UBO ranges can be selectively pushed while there's space. */

/* Directly index an array sysval. Index must be in bounds. Index specified in
 * 16-bit units regardless of the underlying sysval's unit. */

agx_index
agx_indexed_sysval(agx_context *ctx, enum agx_push_type type,
      enum agx_size size, unsigned index, unsigned length)
{
   /* Check if we already pushed */
   for (unsigned i = 0; i < ctx->out->push_ranges; ++i) {
      struct agx_push push = ctx->out->push[i];

      if (push.type == type && !push.indirect) {
         assert(length == push.length);
         assert(index < push.length);
         return agx_uniform(push.base + index, size);
      }
   }

   /* Otherwise, push */
   assert(ctx->out->push_ranges < AGX_MAX_PUSH_RANGES);

   unsigned base = ctx->push_base;
   ctx->push_base += length;

   ctx->out->push[ctx->out->push_ranges++] = (struct agx_push) {
      .type = type,
      .base = base,
      .length = length,
      .indirect = false
   };

   return agx_uniform(base + index, size);
}
