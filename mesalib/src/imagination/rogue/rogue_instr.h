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

#ifndef ROGUE_INSTR_H
#define ROGUE_INSTR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rogue_operand.h"
#include "util/list.h"

/**
 * \brief Instruction opcodes.
 */
enum rogue_opcode {
   ROGUE_OP_NOP = 0, /** No-operation. */
   ROGUE_OP_END_FRAG, /** Fragment shader end. */
   ROGUE_OP_END_VERT, /** Vertex shader end. */
   ROGUE_OP_WDF, /** Write data fence. */

   ROGUE_OP_PIX_ITER_W, /** Pixel iteration with coefficients. */

   ROGUE_OP_MAX, /** Returns the largest out of two floats. */
   ROGUE_OP_MIN, /** Returns the smallest out of two floats. */

   ROGUE_OP_PACK_U8888, /** Scales the four input floats:
                         * [0.0f, 0.1f] -> [0, 255] and packs them
                         * into a 32-bit unsigned integer.
                         */

   ROGUE_OP_MOV, /** Register move instruction. */
   ROGUE_OP_MOV_IMM, /** Move immediate instruction. */

   ROGUE_OP_FMA, /** Fused-multiply-add (float). */
   ROGUE_OP_MUL, /** Multiply (float). */

   ROGUE_OP_VTXOUT, /** Writes the input register
                     * to the given vertex output index.
                     */

   ROGUE_OP_COUNT,
};

/**
 * \brief Instruction flags.
 */
enum rogue_instr_flag {
   ROGUE_INSTR_FLAG_SAT = 0, /** Saturate values to 0.0 ... 1.0. */
   ROGUE_INSTR_FLAG_LP, /** Low-precision modifier. */
   ROGUE_INSTR_FLAG_OLCHK, /** Overlap check (pixel write). */

   ROGUE_INSTR_FLAG_COUNT,
};

/**
 * \brief Instruction description.
 */
struct rogue_instr {
   enum rogue_opcode opcode;

   size_t num_operands;
   struct rogue_operand *operands;

   uint64_t flags; /** A mask of #rogue_instr_flag values. */

   struct list_head node; /** Linked list node. */
};

struct rogue_instr *rogue_instr_create(void *mem_ctx, enum rogue_opcode opcode);

bool rogue_instr_set_flag(struct rogue_instr *instr,
                          enum rogue_instr_flag flag);

bool rogue_instr_set_operand_imm(struct rogue_instr *instr,
                                 size_t index,
                                 uint64_t value);
bool rogue_instr_set_operand_drc(struct rogue_instr *instr,
                                 size_t index,
                                 size_t number);
bool rogue_instr_set_operand_reg(struct rogue_instr *instr,
                                 size_t index,
                                 enum rogue_operand_type type,
                                 size_t number);
bool rogue_instr_set_operand_vreg(struct rogue_instr *instr,
                                  size_t index,
                                  size_t number);
bool rogue_instr_set_operand_vreg_vec(struct rogue_instr *instr,
                                      size_t index,
                                      size_t component,
                                      size_t number);
#endif /* ROGUE_INSTR_H */
