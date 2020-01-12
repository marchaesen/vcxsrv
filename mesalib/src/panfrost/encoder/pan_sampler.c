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

#include "pan_encoder.h"

/* Sampler comparison functions are flipped in OpenGL from the hardware, so we
 * need to be able to flip accordingly */

enum mali_func
panfrost_flip_compare_func(enum mali_func f)
{
        switch (f) {
        case MALI_FUNC_LESS:
                return MALI_FUNC_GREATER;
        case MALI_FUNC_GREATER:
                return MALI_FUNC_LESS;
        case MALI_FUNC_LEQUAL:
                return MALI_FUNC_GEQUAL;
        case MALI_FUNC_GEQUAL:
                return MALI_FUNC_LEQUAL;
        default:
                return f;
        }
}
