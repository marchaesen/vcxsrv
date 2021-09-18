/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (C) 2019-2020 Collabora, Ltd.
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

static void
agx_print_sized(char prefix, unsigned value, enum agx_size size, FILE *fp)
{
   switch (size) {
   case AGX_SIZE_16:
      fprintf(fp, "%c%u%c", prefix, value >> 1, (value & 1) ? 'h' : 'l');
      return;
   case AGX_SIZE_32:
      assert((value & 1) == 0);
      fprintf(fp, "%c%u", prefix, value >> 1);
      return;
   case AGX_SIZE_64:
      assert((value & 1) == 0);
      fprintf(fp, "%c%u:%c%u", prefix, value >> 1,
            prefix, (value >> 1) + 1);
      return;
   }

   unreachable("Invalid size");
}

static void
agx_print_index(agx_index index, FILE *fp)
{
   switch (index.type) {
   case AGX_INDEX_NULL:
      fprintf(fp, "_");
      return;

   case AGX_INDEX_NORMAL:
      if (index.cache)
         fprintf(fp, "$");

      if (index.discard)
         fprintf(fp, "`");

      if (index.kill)
         fprintf(fp, "*");

      fprintf(fp, "%u", index.value);
      break;

   case AGX_INDEX_IMMEDIATE:
      fprintf(fp, "#%u", index.value);
      break;

   case AGX_INDEX_UNIFORM:
      agx_print_sized('u', index.value, index.size, fp);
      break;

   case AGX_INDEX_REGISTER:
      agx_print_sized('r', index.value, index.size, fp);
      break;

   default:
      unreachable("Invalid index type");
   }

   /* Print length suffixes if not implied */
   if (index.type == AGX_INDEX_NORMAL || index.type == AGX_INDEX_IMMEDIATE) {
      if (index.size == AGX_SIZE_16)
         fprintf(fp, "h");
      else if (index.size == AGX_SIZE_64)
         fprintf(fp, "d");
   }

   if (index.abs)
      fprintf(fp, ".abs");

   if (index.neg)
      fprintf(fp, ".neg");
}

void
agx_print_instr(agx_instr *I, FILE *fp)
{
   assert(I->op < AGX_NUM_OPCODES);
   struct agx_opcode_info info = agx_opcodes_info[I->op];

   fprintf(fp, "   %s", info.name);

   if (I->saturate)
      fprintf(fp, ".sat");

   if (I->last)
      fprintf(fp, ".last");

   fprintf(fp, " ");

   bool print_comma = false;

   for (unsigned d = 0; d < info.nr_dests; ++d) {
      if (print_comma)
         fprintf(fp, ", ");
      else
         print_comma = true;

      agx_print_index(I->dest[d], fp);
   }

   for (unsigned s = 0; s < info.nr_srcs; ++s) {
      if (print_comma)
         fprintf(fp, ", ");
      else
         print_comma = true;

      agx_print_index(I->src[s], fp);
   }

   if (I->mask) {
      fprintf(fp, ", ");

      for (unsigned i = 0; i < 4; ++i) {
         if (I->mask & (1 << i))
            fprintf(fp, "%c", "xyzw"[i]);
      }
   }

   /* TODO: Do better for enums, truth tables, etc */
   if (info.immediates) {
      if (print_comma)
         fprintf(fp, ", ");
      else
         print_comma = true;

      fprintf(fp, "#%X", I->imm);
   }

   if (info.immediates & AGX_IMMEDIATE_DIM) {
      if (print_comma)
         fprintf(fp, ", ");
      else
         print_comma = true;

      fprintf(fp, "dim %u", I->dim); // TODO enumify
   }

   if (info.immediates & AGX_IMMEDIATE_SCOREBOARD) {
      if (print_comma)
         fprintf(fp, ", ");
      else
         print_comma = true;

      fprintf(fp, "slot %u", I->scoreboard);
   }

   if (info.immediates & AGX_IMMEDIATE_NEST) {
      if (print_comma)
         fprintf(fp, ", ");
      else
         print_comma = true;

      fprintf(fp, "n=%u", I->nest);
   }

   if ((info.immediates & AGX_IMMEDIATE_INVERT_COND) && I->invert_cond) {
      if (print_comma)
         fprintf(fp, ", ");
      else
         print_comma = true;

      fprintf(fp, "inv");
   }

   fprintf(fp, "\n");
}

void
agx_print_block(agx_block *block, FILE *fp)
{
   fprintf(fp, "block%u {\n", block->name);

   agx_foreach_instr_in_block(block, ins)
      agx_print_instr(ins, fp);

   fprintf(fp, "}");

   if (block->successors[0]) {
      fprintf(fp, " -> ");

      agx_foreach_successor(block, succ)
         fprintf(fp, "block%u ", succ->name);
   }

   if (block->predecessors->entries) {
      fprintf(fp, " from");

      agx_foreach_predecessor(block, pred)
         fprintf(fp, " block%u", pred->name);
   }

   fprintf(fp, "\n\n");
}

void
agx_print_shader(agx_context *ctx, FILE *fp)
{
   agx_foreach_block(ctx, block)
      agx_print_block(block, fp);
}
