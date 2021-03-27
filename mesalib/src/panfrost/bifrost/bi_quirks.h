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

static inline unsigned
bifrost_get_quirks(unsigned product_id)
{
        switch (product_id >> 8) {
        case 0x60:
                return BIFROST_NO_PRELOAD | BIFROST_NO_FP32_TRANSCENDENTALS;
        case 0x62:
                return BIFROST_NO_PRELOAD;
        case 0x70:
        case 0x72:
                return 0;
        default:
                unreachable("Unknown Bifrost GPU ID");
        }
}

#endif
