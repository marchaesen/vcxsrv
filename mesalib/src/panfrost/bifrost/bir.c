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

uint16_t
bi_bytemask_of_read_components(bi_instr *ins, bi_index node)
{
        uint16_t mask = 0x0;
        bool reads_sr = bi_opcode_props[ins->op].sr_read;

        bi_foreach_src(ins, s) {
                if (!bi_is_equiv(ins->src[s], node)) continue;

                /* assume we read a scalar */
                unsigned rmask = 0xF;

                if (s == 0 && reads_sr) {
                        /* Override for a staging register */
                        unsigned count = bi_count_staging_registers(ins);
                        rmask = (1 << (count * 4)) - 1;
                }

                mask |= (rmask << (4 * node.offset));
        }

        return mask;
}

unsigned
bi_writemask(bi_instr *ins)
{
        /* Assume we write a scalar */
        unsigned mask = 0xF;

        if (bi_opcode_props[ins->op].sr_write) {
                unsigned count = bi_count_staging_registers(ins);

                /* TODO: this special case is even more special, TEXC has a
                 * generic write mask stuffed in the desc... */
                if (ins->op == BI_OPCODE_TEXC)
                        count = 4;

                mask = (1 << (count * 4)) - 1;
        }

        unsigned shift = ins->dest[0].offset * 4; /* 32-bit words */
        return (mask << shift);
}
