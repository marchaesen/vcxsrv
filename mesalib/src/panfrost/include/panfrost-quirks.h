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
 */

#ifndef __PANFROST_QUIRKS_H
#define __PANFROST_QUIRKS_H

/* Model-specific quirks requiring workarounds/etc. Quirks may be errata
 * requiring a workaround, or features. We're trying to be quirk-positive
 * here; quirky is the best! */

/* Whether the GPU lacks the capability for hierarchical tiling, without an
 * "Advanced Tiling Unit", instead requiring a single bin size for the entire
 * framebuffer be selected by the driver */

#define MIDGARD_NO_HIER_TILING (1 << 0)

/* Whether this GPU lacks native multiple render target support and accordingly
 * needs SFBDs instead, with complex lowering with ES3 */

#define MIDGARD_SFBD (1 << 1)

/* Whether fp16 is broken in the compiler. Hopefully this quirk will go away
 * over time */

#define MIDGARD_BROKEN_FP16 (1 << 2)

/* What it says on the tin */
#define IS_BIFROST (1 << 3)

/* What it says on the tin */
#define HAS_SWIZZLES (1 << 4)

/* bit 5 unused */

/* Whether this GPU lacks support for any typed stores in blend shader,
 * requiring packing instead */
#define MIDGARD_NO_TYPED_BLEND_STORES (1 << 6)

/* Whether this GPU lacks support for any typed loads, requiring packing */
#define MIDGARD_NO_TYPED_BLEND_LOADS (1 << 7)

/* Lack support for colour pack/unpack opcodes */
#define NO_BLEND_PACKS (1 << 8)

/* Has some missing formats for typed loads */
#define MIDGARD_MISSING_LOADS (1 << 9)

/* Lack support for AFBC */
#define MIDGARD_NO_AFBC (1 << 10)

/* Quirk collections common to particular uarchs */

#define MIDGARD_QUIRKS (MIDGARD_BROKEN_FP16 | HAS_SWIZZLES \
                | MIDGARD_NO_TYPED_BLEND_STORES \
                | MIDGARD_MISSING_LOADS)

/* TODO: AFBC on Bifrost */
#define BIFROST_QUIRKS (IS_BIFROST | NO_BLEND_PACKS | MIDGARD_NO_AFBC)

static inline unsigned
panfrost_get_quirks(unsigned gpu_id)
{
        switch (gpu_id) {
        case 0x600:
        case 0x620:
                return MIDGARD_QUIRKS | MIDGARD_SFBD
                        | MIDGARD_NO_TYPED_BLEND_LOADS
                        | NO_BLEND_PACKS | MIDGARD_NO_AFBC;

        case 0x720:
                return MIDGARD_QUIRKS | MIDGARD_SFBD | MIDGARD_NO_HIER_TILING
                        | MIDGARD_NO_AFBC;

        case 0x820:
        case 0x830:
                return MIDGARD_QUIRKS | MIDGARD_NO_HIER_TILING;

        case 0x750:
                /* Someone should investigate the broken loads? */
                return MIDGARD_QUIRKS | MIDGARD_NO_TYPED_BLEND_LOADS
                        | NO_BLEND_PACKS;

        case 0x860:
        case 0x880:
                return MIDGARD_QUIRKS;

        case 0x6000: /* G71 */
        case 0x6221: /* G72 */
                return BIFROST_QUIRKS | HAS_SWIZZLES;

        case 0x7093: /* G31 */
        case 0x7212: /* G52 */
                return BIFROST_QUIRKS;

        default:
                unreachable("Unknown Panfrost GPU ID");
        }
}

#endif
