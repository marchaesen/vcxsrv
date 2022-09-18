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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rogue_instr.h"
#include "rogue_operand.h"
#include "rogue_util.h"
#include "util/ralloc.h"

/**
 * \file rogue_instr.c
 *
 * \brief Contains functions to manipulate Rogue instructions.
 */

/* clang-format off */

static const size_t instr_operand_count[ROGUE_OP_COUNT] = {
   [ROGUE_OP_NOP] = 0,
   [ROGUE_OP_END_FRAG] = 0,
   [ROGUE_OP_END_VERT] = 0,
   [ROGUE_OP_WDF] = 1,
   [ROGUE_OP_PIX_ITER_W] = 5,
   [ROGUE_OP_MAX] = 3,
   [ROGUE_OP_MIN] = 3,
   [ROGUE_OP_PACK_U8888] = 2,
   [ROGUE_OP_MOV] = 2,
   [ROGUE_OP_MOV_IMM] = 2,
   [ROGUE_OP_FMA] = 4,
   [ROGUE_OP_MUL] = 3,
   [ROGUE_OP_VTXOUT] = 2,
};

/* clang-format on */

/**
 * \brief Returns the number of operands an instruction takes.
 *
 * \param[in] opcode The instruction opcode.
 * \return The number of operands.
 */
static inline size_t rogue_instr_num_operands(enum rogue_opcode opcode)
{
   ASSERT_OPCODE_RANGE(opcode);

   return instr_operand_count[opcode];
}

/**
 * \brief Allocates and sets up a Rogue instruction.
 *
 * \param[in] mem_ctx The memory context for the instruction.
 * \param[in] opcode The instruction opcode.
 * \return A rogue_instr* if successful, or NULL if unsuccessful.
 */
struct rogue_instr *rogue_instr_create(void *mem_ctx, enum rogue_opcode opcode)
{
   struct rogue_instr *instr;

   ASSERT_OPCODE_RANGE(opcode);

   instr = rzalloc_size(mem_ctx, sizeof(*instr));
   if (!instr)
      return NULL;

   instr->opcode = opcode;
   instr->num_operands = rogue_instr_num_operands(opcode);

   /* Allocate space for operand array. */
   if (instr->num_operands) {
      instr->operands = rzalloc_array_size(instr,
                                           sizeof(*instr->operands),
                                           instr->num_operands);
      if (!instr->operands) {
         ralloc_free(instr);
         return NULL;
      }
   }

   return instr;
}

/**
 * \brief Sets a Rogue instruction flag.
 *
 * \param[in] instr The instruction.
 * \param[in] flag The flag to set.
 * \return true if valid, otherwise false.
 */
bool rogue_instr_set_flag(struct rogue_instr *instr, enum rogue_instr_flag flag)
{
   instr->flags = ROH(flag);

   return true;
}

/**
 * \brief Sets a Rogue instruction operand to an immediate value.
 *
 * \param[in] instr The instruction.
 * \param[in] index The operand index.
 * \param[in] value The value to set.
 * \return true if valid, otherwise false.
 */
bool rogue_instr_set_operand_imm(struct rogue_instr *instr,
                                 size_t index,
                                 uint64_t value)
{
   ASSERT_INSTR_OPERAND_INDEX(instr, index);

   instr->operands[index].type = ROGUE_OPERAND_TYPE_IMMEDIATE;
   instr->operands[index].immediate.value = value;

   return true;
}

/**
 * \brief Sets a Rogue instruction operand to a DRC number.
 *
 * \param[in] instr The instruction.
 * \param[in] index The operand index.
 * \param[in] number The DRC number to set.
 * \return true if valid, otherwise false.
 */
bool rogue_instr_set_operand_drc(struct rogue_instr *instr,
                                 size_t index,
                                 size_t number)
{
   ASSERT_INSTR_OPERAND_INDEX(instr, index);

   instr->operands[index].type = ROGUE_OPERAND_TYPE_DRC;
   instr->operands[index].drc.number = number;

   return true;
}

/**
 * \brief Sets a Rogue instruction operand to a register.
 *
 * \param[in] instr The instruction.
 * \param[in] index The operand index.
 * \param[in] type The register type to set.
 * \param[in] number The register number to set.
 * \return true if valid, otherwise false.
 */
bool rogue_instr_set_operand_reg(struct rogue_instr *instr,
                                 size_t index,
                                 enum rogue_operand_type type,
                                 size_t number)
{
   ASSERT_INSTR_OPERAND_INDEX(instr, index);
   ASSERT_OPERAND_REG(type);

   instr->operands[index].type = type;
   instr->operands[index].reg.number = number;

   return true;
}

/**
 * \brief Sets a Rogue instruction operand to a virtual register.
 *
 * \param[in] instr The instruction.
 * \param[in] index The operand index.
 * \param[in] number The register number to set.
 * \return true if valid, otherwise false.
 */
bool rogue_instr_set_operand_vreg(struct rogue_instr *instr,
                                  size_t index,
                                  size_t number)
{
   ASSERT_INSTR_OPERAND_INDEX(instr, index);

   instr->operands[index].type = ROGUE_OPERAND_TYPE_VREG;
   instr->operands[index].vreg.number = number;
   instr->operands[index].vreg.is_vector = false;

   return true;
}

/**
 * \brief Sets a Rogue instruction operand to a virtual register
 * that is a vector type.
 *
 * \param[in] instr The instruction.
 * \param[in] index The operand index.
 * \param[in] component The vector component.
 * \param[in] number The register number to set.
 * \return true if valid, otherwise false.
 */
bool rogue_instr_set_operand_vreg_vec(struct rogue_instr *instr,
                                      size_t index,
                                      size_t component,
                                      size_t number)
{
   ASSERT_INSTR_OPERAND_INDEX(instr, index);

   instr->operands[index].type = ROGUE_OPERAND_TYPE_VREG;
   instr->operands[index].vreg.number = number;
   instr->operands[index].vreg.is_vector = true;
   instr->operands[index].vreg.component = component;

   return true;
}
