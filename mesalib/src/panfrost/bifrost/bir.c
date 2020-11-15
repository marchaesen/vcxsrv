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

/* Does an instruction respect outmods and source mods? Depend
 * on the types involved */

bool
bi_has_outmod(bi_instruction *ins)
{
        bool classy = bi_class_props[ins->type] & BI_MODS;
        bool floaty = nir_alu_type_get_base_type(ins->dest_type) == nir_type_float;

        return classy && floaty;
}

/* Have to check source for e.g. compares */

bool
bi_has_source_mods(bi_instruction *ins)
{
        bool classy = bi_class_props[ins->type] & BI_MODS;
        bool floaty = nir_alu_type_get_base_type(ins->src_types[0]) == nir_type_float;

        return classy && floaty;
}

/* A source is swizzled if the op is swizzlable, in 8-bit or
 * 16-bit mode, and the swizzled op. TODO: multi args */

bool
bi_is_src_swizzled(bi_instruction *ins, unsigned s)
{
        bool classy = bi_class_props[ins->type] & BI_SWIZZLABLE;
        bool small = nir_alu_type_get_type_size(ins->dest_type) < 32;
        bool first = (s == 0); /* TODO: prop? */

        return classy && small && first;
}

bool
bi_has_arg(bi_instruction *ins, unsigned arg)
{
        if (!ins)
                return false;

        bi_foreach_src(ins, s) {
                if (ins->src[s] == arg)
                        return true;
        }

        return false;
}

uint16_t
bi_from_bytemask(uint16_t bytemask, unsigned bytes)
{
        unsigned value = 0;

        for (unsigned c = 0, d = 0; c < 16; c += bytes, ++d) {
                bool a = (bytemask & (1 << c)) != 0;

                for (unsigned q = c; q < bytes; ++q)
                        assert(((bytemask & (1 << q)) != 0) == a);

                value |= (a << d);
        }

        return value;
}

unsigned
bi_get_component_count(bi_instruction *ins, signed src)
{
        /* Discards and branches are oddball since they're not BI_VECTOR but no
         * destination. So special case.. */
        if (ins->type == BI_DISCARD || ins->type == BI_BRANCH)
                return 1;

        if (bi_class_props[ins->type] & BI_VECTOR) {
                assert(ins->vector_channels);
                return (src <= 0) ? ins->vector_channels : 1;
        } else {
                unsigned dest_bytes = nir_alu_type_get_type_size(ins->dest_type);
                unsigned src_bytes = nir_alu_type_get_type_size(ins->src_types[MAX2(src, 0)]);

                /* If there's either f32 on either end, it's only a single
                 * component, etc. */

                unsigned bytes = src < 0 ? dest_bytes : src_bytes;

                if (ins->type == BI_CONVERT)
                        bytes = MAX2(dest_bytes, src_bytes);
                
                if (ins->type == BI_ATEST || ins->type == BI_SELECT)
                        return 1;

                return MAX2(32 / bytes, 1);
        }
}

uint16_t
bi_bytemask_of_read_components(bi_instruction *ins, unsigned node)
{
        uint16_t mask = 0x0;

        bi_foreach_src(ins, s) {
                if (ins->src[s] != node) continue;
                unsigned component_count = bi_get_component_count(ins, s);
                nir_alu_type T = ins->src_types[s];
                unsigned size = nir_alu_type_get_type_size(T);
                unsigned bytes = size / 8;
                unsigned cmask = (1 << bytes) - 1;

                for (unsigned i = 0; i < component_count; ++i) {
                        unsigned c = ins->swizzle[s][i];
                        mask |= (cmask << (c * bytes));
                }
        }

        return mask;
}

uint64_t
bi_get_immediate(bi_instruction *ins, unsigned index)
{
        unsigned v = ins->src[index];
        assert(v & BIR_INDEX_CONSTANT);
        unsigned shift = v & ~BIR_INDEX_CONSTANT;
        uint64_t shifted = ins->constant.u64 >> shift;

        /* Mask off the accessed part */
        unsigned sz = nir_alu_type_get_type_size(ins->src_types[index]);

        if (sz == 64)
                return shifted;
        else
                return shifted & ((1ull << sz) - 1);
}

bool
bi_writes_component(bi_instruction *ins, unsigned comp)
{
        return comp < bi_get_component_count(ins, -1);
}

/* Determine effective writemask for RA/DCE, noting that we currently act
 * per-register hence aligning. TODO: when real write masks are handled in
 * packing (not for a while), update this routine, removing the align */

unsigned
bi_writemask(bi_instruction *ins)
{
        nir_alu_type T = ins->dest_type;
        unsigned size = nir_alu_type_get_type_size(T);
        unsigned bytes_per_comp = size / 8;
        unsigned components = bi_get_component_count(ins, -1);
        unsigned bytes = ALIGN_POT(bytes_per_comp * components, 4);
        unsigned mask = (1 << bytes) - 1;
        unsigned shift = ins->dest_offset * 4; /* 32-bit words */
        return (mask << shift);
}

/* Rewrites uses of an index. This is O(nc) to the program and number of
 * uses, so combine lowering is effectively O(n^2).  Better bookkeeping
 * would bring down to linear if that's an issue. */

void
bi_rewrite_uses(bi_context *ctx,
                unsigned old, unsigned oldc,
                unsigned new, unsigned newc)
{
        assert(newc >= oldc);

        bi_foreach_instr_global(ctx, ins) {
                bi_foreach_src(ins, s) {
                        if (ins->src[s] != old) continue;

                        for (unsigned i = 0; i < 16; ++i)
                                ins->swizzle[s][i] += (newc - oldc);

                        ins->src[s] = new;
                }
        }
}


