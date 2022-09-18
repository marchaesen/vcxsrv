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
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rogue_util.h"
#include "util/macros.h"

/**
 * \file rogue_util.c
 *
 * \brief Contains compiler utility and helper functions.
 */

/**
 * \brief Splits and distributes value "source" across "dest_bytes" according to
 * the ranges specified (from MSB to LSB).
 *
 * \param[in] source The source value to be distributed.
 * \param[in] rangelist The rangelist describing how to distribute "source".
 * \param[in] dest_size The size of the destination in bytes.
 * \param[in] dest_bytes The destination byte array.
 * \return false if invalid inputs were provided, else true.
 */
bool rogue_distribute_value(uint64_t source,
                            const struct rogue_rangelist *rangelist,
                            size_t dest_size,
                            uint8_t dest_bytes[dest_size])
{
   size_t total_bits_left = 0U;

   /* Check that "value" is actually representable in "total_bits" bits. */
   total_bits_left = rogue_rangelist_bits(rangelist);
   assert(util_last_bit64(source) <= total_bits_left &&
          "Value cannot be represented.");

   /* Iterate over each range. */
   for (size_t u = 0U; u < rangelist->num_ranges; ++u) {
      struct rogue_bitrange *range = &rangelist->ranges[u];

      size_t dest_bit = range->start;
      size_t bits_left = range->num;
      size_t bytes_covered = rogue_bytes_spilled(range) + 1;
      size_t base_byte = rogue_byte_index(range, dest_size);

      /* Iterate over each byte covered by the current range. */
      for (size_t b = 0U; b < bytes_covered; ++b) {
         size_t max_bits = rogue_max_bits(dest_bit);
         size_t bits_to_place = MIN2(bits_left, max_bits);
         size_t dest_byte_bit = dest_bit % 8;
         size_t source_bit = total_bits_left - 1;

         /* Mask and shuffle the source value so that it'll fit into the
          * correct place in the destination byte:
          */

         /* Extract bits. */
         uint64_t value_masked =
            (source & BITMASK64_N(source_bit, bits_to_place));
         /* Shift all the way right. */
         value_masked >>= (1 + source_bit - bits_to_place);
         /* Shift left to the correct position. */
         value_masked <<= (1 + dest_byte_bit - bits_to_place);
         /* Place value into byte. */
         dest_bytes[base_byte + b] |= (value_masked & 0xff);

         dest_bit -= max_bits;
         bits_left -= bits_to_place;
         total_bits_left -= bits_to_place;
      }
   }

   return true;
}
