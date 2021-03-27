/*
 * Copyright (C) 2020 Collabora Ltd.
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
 *
 * Authors (Collabora):
 *      Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "compiler.h"

bool
bi_has_arg(bi_instr *ins, bi_index arg)
{
        if (!ins)
                return false;

        bi_foreach_src(ins, s) {
                if (bi_is_equiv(ins->src[s], arg))
                        return true;
        }

        return false;
}

/* Precondition: valid 16-bit or 32-bit register format. Returns whether it is
 * 32-bit. Note auto reads to 32-bit registers even if the memory format is
 * 16-bit, so is considered as such here */

static bool
bi_is_regfmt_16(enum bi_register_format fmt)
{
        switch  (fmt) {
        case BI_REGISTER_FORMAT_F16:
        case BI_REGISTER_FORMAT_S16:
        case BI_REGISTER_FORMAT_U16:
                return true;
        case BI_REGISTER_FORMAT_F32:
        case BI_REGISTER_FORMAT_S32:
        case BI_REGISTER_FORMAT_U32:
        case BI_REGISTER_FORMAT_AUTO:
                return false;
        default:
                unreachable("Invalid register format");
        }
}

static unsigned
bi_count_staging_registers(bi_instr *ins)
{
        enum bi_sr_count count = bi_opcode_props[ins->op].sr_count;
        unsigned vecsize = ins->vecsize + 1; /* XXX: off-by-one */

        switch (count) {
        case BI_SR_COUNT_0 ... BI_SR_COUNT_4:
                return count;
        case BI_SR_COUNT_FORMAT:
                return bi_is_regfmt_16(ins->register_format) ?
                        DIV_ROUND_UP(vecsize, 2) : vecsize;
        case BI_SR_COUNT_VECSIZE:
                return vecsize;
        case BI_SR_COUNT_SR_COUNT:
                return ins->sr_count;
        }

        unreachable("Invalid sr_count");
}

unsigned
bi_count_read_registers(bi_instr *ins, unsigned s)
{
        if (s == 0 && bi_opcode_props[ins->op].sr_read)
                return bi_count_staging_registers(ins);
        else
                return 1;
}

unsigned
bi_writemask(bi_instr *ins, unsigned d)
{
        /* Assume we write a scalar */
        unsigned mask = 0xF;

        if (d == 0 && bi_opcode_props[ins->op].sr_write) {
                unsigned count = bi_count_staging_registers(ins);

                /* TODO: this special case is even more special, TEXC has a
                 * generic write mask stuffed in the desc... */
                if (ins->op == BI_OPCODE_TEXC)
                        count = 4;

                mask = (1 << (count * 4)) - 1;
        }

        unsigned shift = ins->dest[d].offset * 4; /* 32-bit words */
        return (mask << shift);
}

bi_clause *
bi_next_clause(bi_context *ctx, pan_block *block, bi_clause *clause)
{
        /* Try the first clause in this block if we're starting from scratch */
        if (!clause && !list_is_empty(&((bi_block *) block)->clauses))
                return list_first_entry(&((bi_block *) block)->clauses, bi_clause, link);

        /* Try the next clause in this block */
        if (clause && clause->link.next != &((bi_block *) block)->clauses)
                return list_first_entry(&(clause->link), bi_clause, link);

        /* Try the next block, or the one after that if it's empty, etc .*/
        pan_block *next_block = pan_next_block(block);

        bi_foreach_block_from(ctx, next_block, block) {
                bi_block *blk = (bi_block *) block;

                if (!list_is_empty(&blk->clauses))
                        return list_first_entry(&(blk->clauses), bi_clause, link);
        }

        return NULL;
}

/* Does an instruction have a side effect not captured by its register
 * destination? Applies to certain message-passing instructions, +DISCARD, and
 * branching only, used in dead code elimation. Branches are characterized by
 * `last` which applies to them and some atomics, +BARRIER, +BLEND which
 * implies no loss of generality */

bool
bi_side_effects(enum bi_opcode op)
{
        if (bi_opcode_props[op].last || op == BI_OPCODE_DISCARD_F32)
                return true;

        switch (bi_opcode_props[op].message) {
        case BIFROST_MESSAGE_NONE:
        case BIFROST_MESSAGE_VARYING:
        case BIFROST_MESSAGE_ATTRIBUTE:
        case BIFROST_MESSAGE_TEX:
        case BIFROST_MESSAGE_VARTEX:
        case BIFROST_MESSAGE_LOAD:
        case BIFROST_MESSAGE_64BIT:
                return false;

        case BIFROST_MESSAGE_STORE:
        case BIFROST_MESSAGE_ATOMIC:
        case BIFROST_MESSAGE_BARRIER:
        case BIFROST_MESSAGE_BLEND:
        case BIFROST_MESSAGE_Z_STENCIL:
        case BIFROST_MESSAGE_ATEST:
        case BIFROST_MESSAGE_JOB:
                return true;

        case BIFROST_MESSAGE_TILE:
                return (op != BI_OPCODE_LD_TILE);
        }

        unreachable("Invalid message type");
}
