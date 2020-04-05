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

/* Technically we should check the source type, not the dest
 * type, but the type converting opcodes (i2f, f2i) don't
 * actually support mods so it doesn't matter. */

bool
bi_has_source_mods(bi_instruction *ins)
{
        return bi_has_outmod(ins);
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
bi_get_component_count(bi_instruction *ins, unsigned src)
{
        if (bi_class_props[ins->type] & BI_VECTOR) {
                return (src == 0) ? 4 : 1;
        } else {
                /* Stores imply VECTOR */
                assert(ins->dest_type);
                unsigned bytes = nir_alu_type_get_type_size(ins->dest_type);
                return 32 / bytes;
        }
}

unsigned
bi_load32_components(bi_instruction *ins)
{
        unsigned mask = bi_from_bytemask(ins->writemask, 4);
        unsigned count = util_bitcount(mask);
        assert(mask == ((1 << count) - 1));
        assert(count >= 1 && count <= 4);
        return count;
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
        assert(index & BIR_INDEX_CONSTANT);
        unsigned shift = index & ~BIR_INDEX_CONSTANT;
        return ins->constant.u64 >> shift;
}

bool
bi_writes_component(bi_instruction *ins, unsigned comp)
{
        /* TODO: Do we want something less coarse? */
        if (bi_class_props[ins->type] & BI_VECTOR)
                return true;

        nir_alu_type T = ins->dest_type;
        unsigned size = nir_alu_type_get_type_size(T);
        return ins->writemask & (0xF << (comp * (size / 8)));
}
