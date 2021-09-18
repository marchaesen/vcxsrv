/*
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

#ifndef __BI_QUIRKS_H
#define __BI_QUIRKS_H

/* Model-specific quirks requiring compiler workarounds/etc. Quirks
 * may be errata requiring a workaround, or features. We're trying to be
 * quirk-positive here; quirky is the best! */

/* Whether this GPU lacks support for the preload mechanism. New GPUs can have
 * varyings and textures preloaded into the fragment shader to amortize the I/O
 * cost; early Bifrost models lacked this feature. */

#define BIFROST_NO_PRELOAD (1 << 0)

/* Whether this GPU lacks support for fp32 transcendentals, requiring backend
 * lowering to low-precision lookup tables and polynomial approximation */

#define BIFROST_NO_FP32_TRANSCENDENTALS (1 << 1)

/* Whether this GPU lacks support for the full form of the CLPER instruction.
 * These GPUs use a simple encoding of CLPER that does not support
 * inactive_result, subgroup_size, or lane_op. Using those features requires
 * lowering to additional ALU instructions. The encoding forces inactive_result
 * = zero, subgroup_size = subgroup4, and lane_op = none. */

#define BIFROST_LIMITED_CLPER (1 << 2)

static inline unsigned
bifrost_get_quirks(unsigned product_id)
{
        switch (product_id >> 8) {
        case 0x60:
                return BIFROST_NO_PRELOAD | BIFROST_NO_FP32_TRANSCENDENTALS |
                       BIFROST_LIMITED_CLPER;
        case 0x62:
                return BIFROST_NO_PRELOAD | BIFROST_LIMITED_CLPER;
        case 0x70: /* G31 */
                return BIFROST_LIMITED_CLPER;
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
                return 0;
        case 0x90:
        case 0x91:
        case 0x92:
        case 0x93:
        case 0x94:
        case 0x95:
                return BIFROST_NO_PRELOAD;
        default:
                unreachable("Unknown Bifrost/Valhall GPU ID");
        }
}

/* How many lanes per architectural warp (subgroup)? Used to lower divergent
 * indirects. */

static inline unsigned
bifrost_lanes_per_warp(unsigned product_id)
{
        switch (product_id >> 12) {
        case 6: return 4;
        case 7: return 8;
        case 9: return 16;
        default: unreachable("Invalid Bifrost/Valhall GPU major");
        }
}

#endif
