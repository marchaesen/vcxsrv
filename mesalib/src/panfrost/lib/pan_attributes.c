/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 */

#include "util/u_math.h"
#include "midgard_pack.h"
#include "pan_encoder.h"

/* This file handles attribute descriptors. The
 * bulk of the complexity is from instancing. See mali_job for
 * notes on how this works. But basically, for small vertex
 * counts, we have a lookup table, and for large vertex counts,
 * we look at the high bits as a heuristic. This has to match
 * exactly how the hardware calculates this (which is why the
 * algorithm is so weird) or else instancing will break. */

/* Given an odd number (of the form 2k + 1), compute k */
#define ODD(odd) ((odd - 1) >> 1)

static unsigned
panfrost_small_padded_vertex_count(unsigned idx)
{
        if (idx < 10)
                return idx;
        else
                return (idx + 1) & ~1;
}

static unsigned
panfrost_large_padded_vertex_count(uint32_t vertex_count)
{
        /* First, we have to find the highest set one */
        unsigned highest = 32 - __builtin_clz(vertex_count);

        /* Using that, we mask out the highest 4-bits */
        unsigned n = highest - 4;
        unsigned nibble = (vertex_count >> n) & 0xF;

        /* Great, we have the nibble. Now we can just try possibilities. Note
         * that we don't care about the bottom most bit in most cases, and we
         * know the top bit must be 1 */

        unsigned middle_two = (nibble >> 1) & 0x3;

        switch (middle_two) {
        case 0b00:
                if (!(nibble & 1))
                        return (1 << n) * 9;
                else
                        return (1 << (n + 1)) * 5;
        case 0b01:
                return (1 << (n + 2)) * 3;
        case 0b10:
                return (1 << (n + 1)) * 7;
        case 0b11:
                return (1 << (n + 4));
        default:
                return 0; /* unreachable */
        }
}

unsigned
panfrost_padded_vertex_count(unsigned vertex_count)
{
        if (vertex_count < 20)
                return panfrost_small_padded_vertex_count(vertex_count);
        else
                return panfrost_large_padded_vertex_count(vertex_count);
}

/* The much, much more irritating case -- instancing is enabled. See
 * panfrost_job.h for notes on how this works */

unsigned
panfrost_compute_magic_divisor(unsigned hw_divisor, unsigned *o_shift, unsigned *extra_flags)
{
        /* We have a NPOT divisor. Here's the fun one (multipling by
         * the inverse and shifting) */

        /* floor(log2(d)) */
        unsigned shift = util_logbase2(hw_divisor);

        /* m = ceil(2^(32 + shift) / d) */
        uint64_t shift_hi = 32 + shift;
        uint64_t t = 1ll << shift_hi;
        double t_f = t;
        double hw_divisor_d = hw_divisor;
        double m_f = ceil(t_f / hw_divisor_d);
        unsigned m = m_f;

        /* Default case */
        uint32_t magic_divisor = m;

        /* e = 2^(shift + 32) % d */
        uint64_t e = t % hw_divisor;

        /* Apply round-down algorithm? e <= 2^shift?. XXX: The blob
         * seems to use a different condition */
        if (e <= (1ll << shift)) {
                magic_divisor = m - 1;
                *extra_flags = 1;
        }

        /* Top flag implicitly set */
        assert(magic_divisor & (1u << 31));
        magic_divisor &= ~(1u << 31);
        *o_shift = shift;

        return magic_divisor;
}

/* Records for gl_VertexID and gl_InstanceID use a slightly special encoding,
 * but the idea is the same */

void
panfrost_vertex_id(
        unsigned padded_count,
        struct mali_attribute_buffer_packed *attr,
        bool instanced)
{
        /* We factor the padded count as shift/odd and that's it */
        pan_pack(attr, ATTRIBUTE_BUFFER, cfg) {
                cfg.special = MALI_ATTRIBUTE_SPECIAL_VERTEX_ID;
                cfg.type = 0;

                if (instanced) {
                        cfg.divisor_r = __builtin_ctz(padded_count);
                        cfg.divisor_p = padded_count >> (cfg.divisor_r + 1);
                } else {
                        /* Match the blob... */
                        cfg.divisor_r = 0x1F;
                        cfg.divisor_p = 0x4;
                }
        }
}

void
panfrost_instance_id(
        unsigned padded_count,
        struct mali_attribute_buffer_packed *attr,
        bool instanced)
{
        pan_pack(attr, ATTRIBUTE_BUFFER, cfg) {
                cfg.special = MALI_ATTRIBUTE_SPECIAL_INSTANCE_ID;
                cfg.type = 0;

                /* POT records have just a shift directly with an off-by-one for
                 * unclear reasons. NPOT records have a magic divisor smushed into the
                 * stride field (which is unused for these special records) */

                if (!instanced || padded_count <= 1) {
                        /* Match the blob... */
                        cfg.stride = ((1u << 31) - 1);
                        cfg.divisor_r = 0x1F;
                        cfg.divisor_e = 0x1;
                } else if(util_is_power_of_two_or_zero(padded_count)) {
                        /* By above, padded_count > 1 => padded_count >= 2 so
                         * since we're a power of two, ctz(padded_count) =
                         * log2(padded_count) >= log2(2) = 1, so
                         * ctz(padded_count) - 1 >= 0, so this can't underflow
                         * */

                        cfg.divisor_r = __builtin_ctz(padded_count) - 1;
                } else {
                        cfg.stride = panfrost_compute_magic_divisor(padded_count,
                                        &cfg.divisor_r, &cfg.divisor_e);
                }
        }
}
 
