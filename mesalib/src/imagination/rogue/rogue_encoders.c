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

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rogue_encoders.h"
#include "rogue_util.h"
#include "util/bitscan.h"

/**
 * \brief Passes the input value through unchanged.
 *
 * \param[in] value Pointer to the destination value.
 * \param[in] inputs Number of inputs provided.
 * \param[in] ... Input value(s).
 * \return true if encoding was successful.
 */
bool rogue_encoder_pass(uint64_t *value, size_t inputs, ...)
{
   va_list args;

   assert(inputs == 1);

   va_start(args, inputs);
   *value = va_arg(args, uint64_t);
   va_end(args);

   return true;
}

/**
 * \brief Encoder for DRC values.
 *
 * \sa #rogue_encoder_pass()
 *
 * \param[in] value Pointer to the destination value.
 * \param[in] inputs Number of inputs provided.
 * \param[in] ... Input value(s).
 * \return true if encoding was successful.
 */
bool rogue_encoder_drc(uint64_t *value, size_t inputs, ...)
   __attribute__((alias("rogue_encoder_pass")));

/**
 * \brief Encoder for immediate values.
 *
 * \sa #rogue_encoder_pass()
 *
 * \param[in] value Pointer to the destination value.
 * \param[in] inputs Number of inputs provided.
 * \param[in] ... Input value(s).
 * \return true if encoding was successful.
 */
bool rogue_encoder_imm(uint64_t *value, size_t inputs, ...)
   __attribute__((alias("rogue_encoder_pass")));

/**
 * \brief Encodes input ranges {1..15 -> 1-15} and {16 -> 0}.
 *
 * The input should be in the range 1-16; the function represents 1-15 normally
 * and represents 16 by 0.
 *
 * \param[in] value Pointer to the destination value.
 * \param[in] inputs Number of inputs provided.
 * \param[in] ... Input value(s).
 * \return true if encoding was successful.
 */
bool rogue_encoder_ls_1_16(uint64_t *value, size_t inputs, ...)
{
   va_list args;
   uint64_t input;

   assert(inputs == 1);

   va_start(args, inputs);
   input = va_arg(args, uint64_t);
   va_end(args);

   /* Validate the input range. */
   if (!input || input > 16) {
      *value = UINT64_MAX;
      return false;
   }

   *value = input % 16;

   return true;
}

/**
 * \brief Encodes registers according to the number of bits needed to specify
 * the bank number and register number.
 *
 * \param[in] value Pointer to the destination value.
 * \param[in] bank_bits The number of bits used to represent the register bank.
 * \param[in] bank the register bank
 * \param[in] num_bits The number of bits used to represent the register number.
 * \param[in] num The register number.
 * \return true if encoding was successful.
 */
static bool rogue_encoder_reg(uint64_t *value,
                              size_t bank_bits,
                              size_t bank,
                              size_t num_bits,
                              size_t num)
{
   /* Verify "num" fits in "num_bits" and "bank" fits in "bank_bits". */
   assert(util_last_bit64(num) <= num_bits);
   assert(util_last_bit64(bank) <= bank_bits);

   *value = num;
   *value |= (bank << num_bits);

   return true;
}

/**
 * \brief Macro to define the rogue_encoder_reg variants.
 */
#define ROGUE_ENCODER_REG_VARIANT(bank_bits, num_bits)                 \
   bool rogue_encoder_reg_##bank_bits##_##num_bits(uint64_t *value,    \
                                                   size_t inputs,      \
                                                   ...)                \
   {                                                                   \
      va_list args;                                                    \
      size_t bank;                                                     \
      size_t num;                                                      \
      assert(inputs == 2);                                             \
      va_start(args, inputs);                                          \
      bank = va_arg(args, size_t);                                     \
      num = va_arg(args, size_t);                                      \
      va_end(args);                                                    \
      return rogue_encoder_reg(value, bank_bits, bank, num_bits, num); \
   }

ROGUE_ENCODER_REG_VARIANT(2, 8)
ROGUE_ENCODER_REG_VARIANT(3, 8)
ROGUE_ENCODER_REG_VARIANT(3, 11)

#undef ROGUE_ENCODER_REG_VARIANT
