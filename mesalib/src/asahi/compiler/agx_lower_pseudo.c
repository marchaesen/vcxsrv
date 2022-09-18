/*
 * Copyright (C) 2022 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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
#include "agx_builder.h"

/* Lower pseudo instructions created during optimization. */

static void
agx_lower_to_unary_bitop(agx_instr *I, enum agx_bitop_table table)
{
   I->op = AGX_OPCODE_BITOP;
   I->truth_table = table;

   /* Allocate extra source */
   I->src = reralloc_array_size(I, I->src, sizeof(agx_index), I->nr_srcs++);
   I->src[1] = agx_zero();
}

static void
agx_lower_to_binary_bitop(agx_instr *I, enum agx_bitop_table table)
{
   I->op = AGX_OPCODE_BITOP;
   I->truth_table = table;
}

void
agx_lower_pseudo(agx_context *ctx)
{
   agx_foreach_instr_global(ctx, I) {
      switch (I->op) {

      /* Various instructions are implemented as bitwise truth tables */
      case AGX_OPCODE_MOV: agx_lower_to_unary_bitop(I, AGX_BITOP_MOV); break;
      case AGX_OPCODE_NOT: agx_lower_to_unary_bitop(I, AGX_BITOP_NOT); break;
      case AGX_OPCODE_AND: agx_lower_to_binary_bitop(I, AGX_BITOP_AND); break;
      case AGX_OPCODE_XOR: agx_lower_to_binary_bitop(I, AGX_BITOP_XOR); break;
      case AGX_OPCODE_OR:  agx_lower_to_binary_bitop(I, AGX_BITOP_OR); break;

      default:
         break;
      }
   }
}
