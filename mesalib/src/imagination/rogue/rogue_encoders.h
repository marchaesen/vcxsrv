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

#ifndef ROGUE_ENCODERS_H
#define ROGUE_ENCODERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util/macros.h"

/* Returns false if input was invalid. */
typedef bool (*field_encoder_t)(uint64_t *value, size_t inputs, ...);

bool rogue_encoder_pass(uint64_t *value, size_t inputs, ...);
bool rogue_encoder_drc(uint64_t *value, size_t inputs, ...);
bool rogue_encoder_imm(uint64_t *value, size_t inputs, ...);
bool rogue_encoder_ls_1_16(uint64_t *value, size_t inputs, ...);

/**
 * \brief Macro to declare the rogue_encoder_reg variants.
 */
#define ROGUE_ENCODER_REG_VARIANT(bank_bits, num_bits)              \
   bool rogue_encoder_reg_##bank_bits##_##num_bits(uint64_t *value, \
                                                   size_t inputs,   \
                                                   ...);
ROGUE_ENCODER_REG_VARIANT(2, 8)
ROGUE_ENCODER_REG_VARIANT(3, 8)
ROGUE_ENCODER_REG_VARIANT(3, 11)
#undef ROGUE_ENCODER_REG_VARIANT

#endif /* ROGUE_ENCODERS_H */
