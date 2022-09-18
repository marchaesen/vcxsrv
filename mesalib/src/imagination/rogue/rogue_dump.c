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

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "rogue_dump.h"
#include "rogue_shader.h"
#include "rogue_util.h"
#include "util/bitscan.h"

/**
 * \file rogue_dump.c
 *
 * \brief Contains functions to dump Rogue data structures into a textual
 * format.
 */

static const char *const rogue_operand_string[ROGUE_OPERAND_TYPE_COUNT] = {
   [ROGUE_OPERAND_TYPE_REG_TEMP] = "r",
   [ROGUE_OPERAND_TYPE_REG_COEFF] = "cf",
   [ROGUE_OPERAND_TYPE_REG_CONST] = "c",
   [ROGUE_OPERAND_TYPE_REG_SHARED] = "sh",
   [ROGUE_OPERAND_TYPE_REG_PIXEL_OUT] = "po",
   [ROGUE_OPERAND_TYPE_REG_VERTEX_IN] = "vi",
   [ROGUE_OPERAND_TYPE_REG_INTERNAL] = "i",
   [ROGUE_OPERAND_TYPE_IMMEDIATE] = "#",
   [ROGUE_OPERAND_TYPE_DRC] = "drc",
   [ROGUE_OPERAND_TYPE_VREG] = "V",
};

static const char *const rogue_opcode_string[ROGUE_OP_COUNT] = {
   [ROGUE_OP_NOP] = "nop",
   [ROGUE_OP_END_FRAG] = "end.frag",
   [ROGUE_OP_END_VERT] = "end.vert",
   [ROGUE_OP_WDF] = "wdf",
   [ROGUE_OP_PIX_ITER_W] = "pixiter.w",
   [ROGUE_OP_MAX] = "max",
   [ROGUE_OP_MIN] = "min",
   [ROGUE_OP_PACK_U8888] = "pack.u8888",
   [ROGUE_OP_MOV] = "mov",
   [ROGUE_OP_MOV_IMM] = "mov.imm",
   [ROGUE_OP_FMA] = "fma",
   [ROGUE_OP_MUL] = "mul",
   [ROGUE_OP_VTXOUT] = "vtxout",
};

static const char *const rogue_instr_flag_string[ROGUE_INSTR_FLAG_COUNT] = {
   [ROGUE_INSTR_FLAG_SAT] = "sat",
   [ROGUE_INSTR_FLAG_LP] = "lp",
   [ROGUE_INSTR_FLAG_OLCHK] = "olchk",
};

static const char rogue_vector_string[4] = {
   'x',
   'y',
   'z',
   'w',
};

/**
 * \brief Dumps an operand as text to a file pointer.
 *
 * \param[in] operand The operand.
 * \param[in] fp The file pointer.
 * \return true if successful, otherwise false.
 */
bool rogue_dump_operand(const struct rogue_operand *operand, FILE *fp)
{
   ASSERT_OPERAND_RANGE(operand->type);

   fprintf(fp, "%s", rogue_operand_string[operand->type]);

   if (operand->type == ROGUE_OPERAND_TYPE_IMMEDIATE)
      fprintf(fp, "%" PRIu64, operand->immediate.value);
   else if (operand->type == ROGUE_OPERAND_TYPE_DRC)
      fprintf(fp, "%zu", operand->drc.number);
   else if (rogue_check_bitset(rogue_onehot(operand->type), ROGUE_MASK_ANY_REG))
      fprintf(fp, "%zu", operand->reg.number);
   else if (operand->type == ROGUE_OPERAND_TYPE_VREG) {
      fprintf(fp, "%zu", operand->vreg.number);
      if (operand->vreg.is_vector)
         fprintf(fp, ".%c", rogue_vector_string[operand->vreg.component]);
   }

   return true;
}

/**
 * \brief Dumps an instruction as text to a file pointer.
 *
 * \param[in] instr The instruction.
 * \param[in] fp The file pointer.
 * \return true if successful, otherwise false.
 */
bool rogue_dump_instr(const struct rogue_instr *instr, FILE *fp)
{
   uint64_t flags = 0U;

   ASSERT_OPCODE_RANGE(instr->opcode);

   flags = instr->flags;

   fprintf(fp, "%s", rogue_opcode_string[instr->opcode]);

   /* Iterate over each flag bit and print its string form. */
   while (flags) {
      uint64_t flag = u_bit_scan64(&flags);
      ASSERT_INSTR_FLAG_RANGE(flag);
      fprintf(fp, ".%s", rogue_instr_flag_string[flag]);
   }

   if (instr->num_operands)
      fprintf(fp, " ");

   /* Dump each operand. */
   for (size_t u = 0U; u < instr->num_operands; ++u) {
      CHECKF(rogue_dump_operand(&instr->operands[u], fp),
             "Failed to dump operand.");
      if (u < (instr->num_operands - 1))
         fprintf(fp, ", ");
   }

   fprintf(fp, ";");

   return true;
}

/**
 * \brief Dumps a shader as text to a file pointer.
 *
 * \param[in] shader The shader.
 * \param[in] fp The file pointer.
 * \return true if successful, otherwise false.
 */
bool rogue_dump_shader(const struct rogue_shader *shader, FILE *fp)
{
   /* Dump the shader stage. */
   fprintf(fp, "# %s shader\n", _mesa_shader_stage_to_string(shader->stage));

   /* Dump each instruction. */
   foreach_instr (instr, &shader->instr_list) {
      CHECKF(rogue_dump_instr(instr, fp), "Failed to dump instruction.");
      fprintf(fp, "\n");
   }
   fprintf(fp, "\n");

   return true;
}
