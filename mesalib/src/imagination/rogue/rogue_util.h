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

#ifndef ROGUE_UTIL_H
#define ROGUE_UTIL_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util/bitscan.h"
#include "util/log.h"
#include "util/macros.h"

/* Input validation helpers. */

/**
 * \brief Returns false if "expr" is not asserted.
 *
 * \param[in] expr The expression to check.
 */
#define CHECK(expr)    \
   do {                \
      if (!(expr))     \
         return false; \
   } while (0)

/**
 * \brief Returns false if "expr" is not asserted,
 * and logs the provided error message.
 *
 * \param[in] expr The expression to check.
 * \param[in] fmt The error message to print.
 * \param[in] ... The printf-style varable arguments.
 */
#define CHECKF(expr, fmt, ...)                                  \
   do {                                                         \
      if (!(expr)) {                                            \
         mesa_log(MESA_LOG_ERROR, "ROGUE", fmt, ##__VA_ARGS__); \
         return false;                                          \
      }                                                         \
   } while (0)

/**
 * \brief Asserts if "opcode" is invalid.
 *
 * \param[in] opcode The opcode to check.
 */
#define ASSERT_OPCODE_RANGE(opcode) assert((opcode) < ROGUE_OP_COUNT)

/**
 * \brief Asserts if "operand" is invalid.
 *
 * \param[in] operand The operand to check.
 */
#define ASSERT_OPERAND_RANGE(operand) \
   assert((operand) < ROGUE_OPERAND_TYPE_COUNT)

/**
 * \brief Asserts if "operand" is not a register.
 *
 * \param[in] operand The operand to check.
 */
#define ASSERT_OPERAND_REG(operand) \
   assert((operand) <= ROGUE_OPERAND_TYPE_REG_MAX)

/**
 * \brief Asserts if "flag" is invalid.
 *
 * \param[in] flag The flag to check.
 */
#define ASSERT_INSTR_FLAG_RANGE(flag) assert((flag) < ROGUE_INSTR_FLAG_COUNT)

/**
 * \brief Asserts if operand index "index" is out of range.
 *
 * \param[in] instr The target instruction.
 * \param[in] index The operand index to check.
 */
#define ASSERT_INSTR_OPERAND_INDEX(instr, index) \
   assert((index) < (instr)->num_operands)

/**
 * \brief Asserts if "stage" is invalid.
 *
 * \param[in] stage The stage to check.
 */
#define ASSERT_SHADER_STAGE_RANGE(stage) assert((stage) < MESA_SHADER_STAGES)

/**
 * \brief Creates a "n"-bit mask starting from bit "b".
 *
 * \param[in] b The starting bit.
 * \param[in] n The number of bits in the mask.
 */
#define BITMASK64_N(b, n) (((~0ULL) << (64 - (n))) >> (63 - (b)))

/**
 * \brief Compile-time rogue_onehot.
 *
 * \sa #rogue_onehot()
 */
#define ROH(OFFSET) BITFIELD64_BIT(OFFSET)

/* TODO: Consider integrating the following into src/util/{macros,bitscan}.h */

/**
 * \brief Converts a one-hot encoding to an offset encoding.
 *
 * E.g. 0b10000 -> 4
 *
 * \param[in] onehot The one-hot encoding.
 * \return The offset encoding.
 */
static inline uint64_t rogue_offset(uint64_t onehot)
{
   assert(util_bitcount64(onehot) == 1);
   return ffsll(onehot) - 1;
}

/**
 * \brief Converts an offset encoding to a one-hot encoding.
 *
 * E.g. 0 -> 0b1
 *
 * \param[in] offset The offset encoding.
 * \return The one-hot encoding.
 */
static inline uint64_t rogue_onehot(uint64_t offset)
{
   assert(offset < 64ULL);
   return (1ULL << offset);
}

/**
 * \brief Checks whether an input bitfield contains only a valid bitset.
 *
 * E.g. rogue_check_bitset(0b00001100, 0b00001111) -> true
 *      rogue_check_bitset(0b00001100, 0b00000111) -> false
 *
 * \param[in] input The input bitfield.
 * \param[in] valid_bits The valid bitset.
 * \return true if "input" contains only "valid_bits", false otherwise.
 */
static inline bool rogue_check_bitset(uint64_t input, uint64_t valid_bits)
{
   input &= ~valid_bits;
   return !input;
}

/**
 * \brief Describes a downward range of bits within an arbitrarily-sized
 * sequence.
 *
 * E.g. for start = 7 and num = 3:
 *
 * 76543210
 * abcdefgh
 *
 * the bit range would be: abc.
 */
struct rogue_bitrange {
   size_t start;
   size_t num;
};

/**
 * \brief Describes a collection of bit-ranges within an arbitrarily-sized
 * sequence that are meaningful together.
 *
 * E.g. an 8-bit value that is encoded within a larger value:
 *     8-bit value: abcdefgh
 *     Parent value: 010ab0cdef0010gh
 *
 */
struct rogue_rangelist {
   size_t num_ranges;
   struct rogue_bitrange *ranges;
};

/**
 * \brief Counts the total number of bits described in a rangelist.
 *
 * \param[in] rangelist The input rangelist.
 * \return The total number of bits.
 */
static inline size_t
rogue_rangelist_bits(const struct rogue_rangelist *rangelist)
{
   size_t total_bits = 0U;

   for (size_t u = 0U; u < rangelist->num_ranges; ++u)
      total_bits += rangelist->ranges[u].num;

   return total_bits;
}

/**
 * \brief Returns the byte offset of the bitrange moving left from the LSB.
 *
 * \param[in] bitrange The input bit-range.
 * \return The byte offset.
 */
static inline size_t rogue_byte_num(const struct rogue_bitrange *bitrange)
{
   /* Make sure there are enough bits. */
   assert(bitrange->num <= (bitrange->start + 1));

   return bitrange->start / 8;
}

/**
 * \brief Returns the array-indexable byte offset of a bit-range if the sequence
 * it represents were to be stored in an byte-array containing "num_bytes"
 * bytes.
 *
 * E.g. uint8_t array[2] is a sequence of 16 bits:
 *     bit(0) is located in array[1].
 *     bit(15) is located in array[0].
 *
 * For uint8_t array[4]:
 *     bit(0) is located in array[3].
 *     bit(15) is located in array[2].
 *
 * \param[in] bitrange The input bit-range.
 * \param[in] num_bytes The number of bytes that are used to contain the
 * bit-range. \return The byte offset.
 */
static inline size_t rogue_byte_index(const struct rogue_bitrange *bitrange,
                                      size_t num_bytes)
{
   /* Make sure there are enough bits. */
   assert(bitrange->num <= (bitrange->start + 1));

   return num_bytes - rogue_byte_num(bitrange) - 1;
}

/**
 * \brief Returns the bit offset of a bit-range if the sequence it represents is
 * being accessed in a byte-wise manner.
 *
 * E.g. bit 17 has a bit offset of 1.
 *
 * \param[in] bitrange The input bit-range.
 * \return The bit offset.
 */
static inline size_t rogue_bit_offset(const struct rogue_bitrange *bitrange)
{
   /* Make sure there are enough bits. */
   assert(bitrange->num <= (bitrange->start + 1));

   return bitrange->start % 8;
}

/**
 * \brief Returns the number of additional bytes that the bit-range spills into
 * (excluding its "starting" byte).
 *
 * \param[in] bitrange The input bit-range.
 * \return The number of bytes spilled.
 */
static inline size_t rogue_bytes_spilled(const struct rogue_bitrange *bitrange)
{
   /* Make sure there are enough bits. */
   assert(bitrange->num <= (bitrange->start + 1));

   return ((bitrange->num - 1) / 8) +
          ((bitrange->num % 8) > (rogue_bit_offset(bitrange) + 1));
}

/**
 * \brief For a given bit offset, returns the maximum number of bits (including
 * itself) that are accessible before spilling into the following byte.
 *
 * E.g. When trying to insert an 8-bit value offset of 13, a maximum of 6 bits
 * can be placed; the last 2 bits will need to go into the next byte.
 *
 *     8-bit value: abcdefgh
 *
 *     array[0]  array[1]
 *     15      8 7      0
 *      iiiiiiii jjjjjjjj
 *        ^
 *        abcdef gh
 *
 * \param[in] The bit offset.
 * \return The maximum number of accessible bits.
 */
static inline size_t rogue_max_bits(size_t offset)
{
   return (offset % 8) + 1;
}

bool rogue_distribute_value(uint64_t source,
                            const struct rogue_rangelist *rangelist,
                            size_t dest_size,
                            uint8_t dest_bytes[dest_size]);

#endif /* ROGUE_UTIL_H */
