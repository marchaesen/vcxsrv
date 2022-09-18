/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef PVR_END_OF_TILE_H
#define PVR_END_OF_TILE_H

#include <stdint.h>

/* clang-format off */
static const uint8_t pvr_end_of_tile_program[] = {
   0xa9, 0xf2, 0x40, 0x00,
   0x47, 0x91, 0x00, 0x50,
   0x04, 0x00, 0x80, 0x40,
   0x00, 0x00, 0x80, 0x80,
   0x24, 0xff, 0xa9, 0xf2,
   0x40, 0x00, 0x47, 0x91,
   0x20, 0x20, 0x08, 0x00,
   0x80, 0x40, 0x00, 0x00,
   0x80, 0x80, 0x25, 0xff,
   0x45, 0xa0, 0x80, 0xc2,
   0xa4, 0x40, 0x00, 0x25,
   0x00, 0x00
};
/* clang-format on */

#endif /* PVR_END_OF_TILE_H */
