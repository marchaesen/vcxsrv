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

#ifndef ROGUE_OPERAND_H
#define ROGUE_OPERAND_H

#include <stddef.h>
#include <stdint.h>

#include "rogue_util.h"
#include "util/macros.h"

/* Register-related defines. */

/* Total max number of registers per class
 * (instances > ROGUE_MAX_REG_INDEX addressable via indexing only).
 */
#define ROGUE_MAX_REG_TEMP 248
#define ROGUE_MAX_REG_COEFF 4096
#define ROGUE_MAX_REG_CONST 240
#define ROGUE_MAX_REG_SHARED 4096
#define ROGUE_MAX_REG_PIXEL_OUT 8
#define ROGUE_MAX_REG_VERTEX_IN 248
#define ROGUE_MAX_REG_INTERNAL 8

/* Maximum register index via offset encoding. */
#define ROGUE_MAX_REG_INDEX 256

/* Pixel-out register offset. */
#define ROGUE_PIXEL_OUT_REG_OFFSET 32

/* Internal register offset. */
#define ROGUE_INTERNAL_REG_OFFSET 36

/* Coefficient registers are typically used in groups of 4. */
#define ROGUE_COEFF_ALIGN 4

/* Defines for other operand types. */

/* Available dependent read counters. */
#define ROGUE_NUM_DRCS 2

/* Maximum number of vertex outputs. */
#define ROGUE_MAX_VERTEX_OUTPUTS 256

/* All components of an emulated vec4 register group. */
#define ROGUE_COMPONENT_ALL (~0)

/**
 * \brief Operand types.
 */
enum rogue_operand_type {
   /* Register operands. */
   ROGUE_OPERAND_TYPE_REG_TEMP = 0, /** Temporary register. */
   ROGUE_OPERAND_TYPE_REG_COEFF, /** Coefficient register. */
   ROGUE_OPERAND_TYPE_REG_CONST, /** Constant register. */
   ROGUE_OPERAND_TYPE_REG_SHARED, /** Shared register. */
   ROGUE_OPERAND_TYPE_REG_PIXEL_OUT, /** Pixel output register. */
   ROGUE_OPERAND_TYPE_REG_VERTEX_IN, /** Vertex input register. */
   ROGUE_OPERAND_TYPE_REG_INTERNAL, /** Internal register. */

   ROGUE_OPERAND_TYPE_REG_MAX = ROGUE_OPERAND_TYPE_REG_INTERNAL,

   ROGUE_OPERAND_TYPE_IMMEDIATE, /** Immediate value. */

   ROGUE_OPERAND_TYPE_DRC, /** Dependent read counter. */

   ROGUE_OPERAND_TYPE_VREG, /** Virtual register (pre-regalloc). */

   ROGUE_OPERAND_TYPE_COUNT,
};

/* clang-format off */

#define ROGUE_NUM_REG_TYPES (ROGUE_OPERAND_TYPE_REG_MAX + 1)

/**
 * \brief A bitmask for any register operand type.
 */
#define ROGUE_MASK_ANY_REG                 \
   ROH(ROGUE_OPERAND_TYPE_REG_TEMP) |      \
   ROH(ROGUE_OPERAND_TYPE_REG_COEFF) |     \
   ROH(ROGUE_OPERAND_TYPE_REG_CONST) |     \
   ROH(ROGUE_OPERAND_TYPE_REG_PIXEL_OUT) | \
   ROH(ROGUE_OPERAND_TYPE_REG_VERTEX_IN) | \
   ROH(ROGUE_OPERAND_TYPE_REG_SHARED) |    \
   ROH(ROGUE_OPERAND_TYPE_REG_INTERNAL)

/* clang-format on */

/**
 * \brief Operand description.
 */
struct rogue_operand {
   enum rogue_operand_type type;

   union {
      struct {
         uint64_t value;
      } immediate;

      struct {
         size_t number;
      } drc;

      struct {
         size_t number;
      } reg;

      struct {
         size_t number;
         bool is_vector;
         size_t component;
      } vreg;
   };
};

/**
 * \brief Register access flags.
 */
enum rogue_register_access {
   ROGUE_REG_ACCESS_READ = BITFIELD_BIT(0U), /** Read-only. */
   ROGUE_REG_ACCESS_WRITE = BITFIELD_BIT(1U), /* Write-only. */
   ROGUE_REG_ACCESS_RW = ROGUE_REG_ACCESS_READ |
                         ROGUE_REG_ACCESS_WRITE, /** Read/write. */
};

/**
 * \brief Register modifier flags.
 */
enum rogue_register_modifier {
   ROGUE_REG_MOD_NONE = 0U,
   ROGUE_REG_MOD_IDX = BITFIELD_BIT(0U), /** Index modifier. */
   ROGUE_REG_MOD_DIM = BITFIELD_BIT(1U), /** Dimension modifier. */
   ROGUE_REG_MOD_ALL = ROGUE_REG_MOD_IDX | ROGUE_REG_MOD_DIM,
};

#endif /* ROGUE_OPERAND_H */
