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

static inline unsigned
panfrost_get_quirks(unsigned gpu_id)
{
        switch (gpu_id) {
        case 0x600:
        case 0x620:
                return MIDGARD_SFBD;

        case 0x720:
                return MIDGARD_SFBD | MIDGARD_NO_HIER_TILING;

        case 0x820:
        case 0x830:
                return MIDGARD_NO_HIER_TILING;

        case 0x750:
        case 0x860:
        case 0x880:
                return 0;

        default:
                unreachable("Invalid Midgard GPU ID");
        }
}

#endif
