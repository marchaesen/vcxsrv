/*
 * Copyright (C) 2022 Alyssa Rosenzweig
 * Copyright (C) 2021 Collabora, Ltd.
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
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTAagxLITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIAagxLITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "agx_compiler.h"

/* Validatation doesn't make sense in release builds */
#ifndef NDEBUG

#define agx_validate_assert(stmt) if (!(stmt)) { return false; }

/*
 * If a block contains phi nodes, they must come at the start of the block. If a
 * block contains control flow, it must come after a p_logical_end marker.
 * Therefore the form of a valid block is:
 *
 *       Phi nodes
 *       General instructions
 *       Logical end
 *       Control flow instructions
 *
 * Validate that this form is satisfied.
 *
 * XXX: This only applies before we delete the logical end instructions, maybe
 * that should be deferred though?
 */
enum agx_block_state {
   AGX_BLOCK_STATE_PHI = 0,
   AGX_BLOCK_STATE_BODY = 1,
   AGX_BLOCK_STATE_CF = 2
};

static bool
agx_validate_block_form(agx_block *block)
{
   enum agx_block_state state = AGX_BLOCK_STATE_PHI;

   agx_foreach_instr_in_block(block, I) {
      switch (I->op) {
      case AGX_OPCODE_PHI:
         agx_validate_assert(state == AGX_BLOCK_STATE_PHI);
         break;

      default:
         agx_validate_assert(state != AGX_BLOCK_STATE_CF);
         state = AGX_BLOCK_STATE_BODY;
         break;

      case AGX_OPCODE_P_LOGICAL_END:
         agx_validate_assert(state != AGX_BLOCK_STATE_CF);
         state = AGX_BLOCK_STATE_CF;
         break;

      case AGX_OPCODE_JMP_EXEC_ANY:
      case AGX_OPCODE_JMP_EXEC_NONE:
      case AGX_OPCODE_POP_EXEC:
      case AGX_OPCODE_IF_ICMP:
      case AGX_OPCODE_ELSE_ICMP:
      case AGX_OPCODE_WHILE_ICMP:
      case AGX_OPCODE_IF_FCMP:
      case AGX_OPCODE_ELSE_FCMP:
      case AGX_OPCODE_WHILE_FCMP:
         agx_validate_assert(state == AGX_BLOCK_STATE_CF);
         break;
      }
   }

   return true;
}

void
agx_validate(agx_context *ctx, const char *after)
{
   bool fail = false;

   if (agx_debug & AGX_DBG_NOVALIDATE)
      return;

   agx_foreach_block(ctx, block) {
      if (!agx_validate_block_form(block)) {
         fprintf(stderr, "Invalid block form after %s\n", after);
         agx_print_block(block, stdout);
         fail = true;
      }
   }

   /* TODO: Validate more invariants */

   if (fail) {
      agx_print_shader(ctx, stderr);
      exit(1);
   }
}

#endif /* NDEBUG */
