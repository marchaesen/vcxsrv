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

#ifndef ROGUE_CONSTREGS_H
#define ROGUE_CONSTREGS_H

#include <stddef.h>
#include <stdint.h>

#include "util/macros.h"
#include "util/u_math.h"

#define ROGUE_NO_CONST_REG SIZE_MAX

PUBLIC
size_t rogue_constreg_lookup(uint32_t value);

/**
 * \brief Determines whether a given floating point value exists in a constant
 * register.
 *
 * \param[in] value The value required.
 * \return The index of the constant register containing the value, or
 * ROGUE_NO_CONST_REG if the value is not found.
 */
static inline size_t rogue_constreg_lookup_float(float value)
{
   return rogue_constreg_lookup(fui(value));
}

#endif /* ROGUE_CONSTREGS_H */
