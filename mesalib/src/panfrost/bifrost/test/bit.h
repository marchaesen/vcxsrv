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

#ifndef __BIFROST_TEST_H
#define __BIFROST_TEST_H

#include "panfrost/lib/midgard_pack.h"
#include "panfrost/lib/pan_device.h"
#include "panfrost/lib/pan_bo.h"
#include "bifrost_compile.h"
#include "bifrost/compiler.h"

struct panfrost_device *
bit_initialize(void *memctx);

bool bit_sanity_check(struct panfrost_device *dev);

enum bit_debug {
        BIT_DEBUG_NONE = 0,
        BIT_DEBUG_FAIL,
        BIT_DEBUG_ALL
};

bool
bit_vertex(struct panfrost_device *dev, panfrost_program *prog,
                uint32_t *iubo, size_t sz_ubo,
                uint32_t *iattr, size_t sz_attr,
                uint32_t *expected, size_t sz_expected, enum bit_debug debug);

/* BIT interpreter */

struct bit_state {
        /* Work registers */
        uint32_t r[64];

        /* Passthrough within the bundle */
        uint32_t T;

        /* Passthrough from last bundle */
        uint32_t T0;
        uint32_t T1;
};

void
bit_step(struct bit_state *s, bi_instruction *ins, bool FMA);

void bit_packing(struct panfrost_device *dev, enum bit_debug debug);

#endif


