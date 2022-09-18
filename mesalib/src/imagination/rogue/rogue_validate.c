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

/**
 * \file rogue_validate.c
 *
 * \brief Contains rules and functions for validating Rogue data structures.
 */

#include <stdbool.h>

#include "rogue_operand.h"
#include "rogue_shader.h"
#include "rogue_util.h"
#include "rogue_validate.h"
#include "util/list.h"
#include "util/macros.h"

/**
 * \brief Register operand rules.
 */
#define REG_RULE(OPERAND, ACCESS, MAX, MODIFIERS) \
   [ROGUE_OPERAND_TYPE_REG_##OPERAND] = {         \
      .access = ROGUE_REG_ACCESS_##ACCESS,        \
      .max = MAX,                                 \
      .modifiers = ROGUE_REG_MOD_##MODIFIERS,     \
   }

/* TODO: Support register indexing > ROGUE_MAX_REG_TEMP. */
static const struct rogue_register_rule reg_rules[ROGUE_NUM_REG_TYPES] = {
   REG_RULE(TEMP, RW, MIN2(ROGUE_MAX_REG_INDEX, ROGUE_MAX_REG_TEMP), ALL),
   REG_RULE(COEFF, RW, MIN2(ROGUE_MAX_REG_INDEX, ROGUE_MAX_REG_COEFF), ALL),
   REG_RULE(CONST, RW, MIN2(ROGUE_MAX_REG_INDEX, ROGUE_MAX_REG_CONST), NONE),
   REG_RULE(SHARED, RW, MIN2(ROGUE_MAX_REG_INDEX, ROGUE_MAX_REG_SHARED), ALL),
   REG_RULE(PIXEL_OUT,
            RW,
            MIN2(ROGUE_MAX_REG_INDEX, ROGUE_MAX_REG_PIXEL_OUT),
            NONE),
   REG_RULE(VERTEX_IN,
            RW,
            MIN2(ROGUE_MAX_REG_INDEX, ROGUE_MAX_REG_VERTEX_IN),
            ALL),
   REG_RULE(INTERNAL,
            RW,
            MIN2(ROGUE_MAX_REG_INDEX, ROGUE_MAX_REG_INTERNAL),
            NONE),
};
#undef REG_RULE

/**
 * \brief Instruction rules.
 */
/* TODO: Common up register classes to prevent long lines. */
static const struct rogue_instr_rule instr_rules[ROGUE_OP_COUNT] = {
	[ROGUE_OP_NOP] = { .flags = 0, .num_operands = 0, .operand_rules = NULL, },
	[ROGUE_OP_END_FRAG] = { .flags = 0, .num_operands = 0, .operand_rules = NULL, },
	[ROGUE_OP_END_VERT] = { .flags = 0, .num_operands = 0, .operand_rules = NULL, },
	[ROGUE_OP_WDF] = { .flags = 0,
		.num_operands = 1, .operand_rules = (struct rogue_instr_operand_rule[]){
			[0] = { .mask = ROH(ROGUE_OPERAND_TYPE_DRC), .min = -1, .max = -1, .align = -1, },
		},
	},
	[ROGUE_OP_PIX_ITER_W] = { .flags = ROH(ROGUE_INSTR_FLAG_SAT),
		.num_operands = 5, .operand_rules = (struct rogue_instr_operand_rule[]){
			[0] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
			[1] = { .mask = ROH(ROGUE_OPERAND_TYPE_DRC), .min = -1, .max = -1, .align = -1, },
			[2] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_COEFF), .min = -1, .max = -1, .align = ROGUE_COEFF_ALIGN, },
			[3] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_COEFF), .min = -1, .max = -1, .align = ROGUE_COEFF_ALIGN, },
			[4] = { .mask = ROH(ROGUE_OPERAND_TYPE_IMMEDIATE), .min = 1, .max = 16, .align = -1, },
		},
	},
	[ROGUE_OP_MAX] = { .flags = 0,
		.num_operands = 3, .operand_rules = (struct rogue_instr_operand_rule[]){
			[0] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
			[1] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
			[2] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_CONST) | ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
		},
	},
	[ROGUE_OP_MIN] = { .flags = 0,
		.num_operands = 3, .operand_rules = (struct rogue_instr_operand_rule[]){
			[0] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP) | ROH(ROGUE_OPERAND_TYPE_REG_INTERNAL), .min = -1, .max = -1, .align = -1, },
			[1] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
			[2] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_CONST) | ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
		},
	},
	/* TODO: Add representation for 4 sequential registers. */
	[ROGUE_OP_PACK_U8888] = { .flags = 0,
		.num_operands = 2, .operand_rules = (struct rogue_instr_operand_rule[]){
			[0] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
			[1] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_INTERNAL), .min = -1, .max = -1, .align = -1, },
		},
	},
	[ROGUE_OP_MOV] = { .flags = ROH(ROGUE_INSTR_FLAG_OLCHK),
		.num_operands = 2, .operand_rules = (struct rogue_instr_operand_rule[]){
			[0] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP) | ROH(ROGUE_OPERAND_TYPE_REG_INTERNAL) | ROH(ROGUE_OPERAND_TYPE_REG_PIXEL_OUT), .min = -1, .max = -1, .align = -1, },
			[1] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_CONST) | ROH(ROGUE_OPERAND_TYPE_REG_TEMP) | ROH(ROGUE_OPERAND_TYPE_REG_SHARED) | ROH(ROGUE_OPERAND_TYPE_REG_VERTEX_IN), .min = -1, .max = -1, .align = -1, },
		},
	},
	[ROGUE_OP_MOV_IMM] = { .flags = 0,
		.num_operands = 2, .operand_rules = (struct rogue_instr_operand_rule[]){
			[0] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
			[1] = { .mask = ROH(ROGUE_OPERAND_TYPE_IMMEDIATE), .min = 0, .max = UINT32_MAX, .align = -1, },
		},
	},
	[ROGUE_OP_FMA] = { .flags = ROH(ROGUE_INSTR_FLAG_SAT) | ROH(ROGUE_INSTR_FLAG_LP),
		.num_operands = 4, .operand_rules = (struct rogue_instr_operand_rule[]){
			[0] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
			[1] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
			[2] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
			[3] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
		},
	},
	[ROGUE_OP_MUL] = { .flags = ROH(ROGUE_INSTR_FLAG_SAT) | ROH(ROGUE_INSTR_FLAG_LP),
		.num_operands = 3, .operand_rules = (struct rogue_instr_operand_rule[]){
			[0] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
			[1] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
			[2] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
		},
	},
	[ROGUE_OP_VTXOUT] = { .flags = 0,
		.num_operands = 2, .operand_rules = (struct rogue_instr_operand_rule[]){
			[0] = { .mask = ROH(ROGUE_OPERAND_TYPE_IMMEDIATE), .min = 0, .max = ROGUE_MAX_VERTEX_OUTPUTS, .align = -1, },
			[1] = { .mask = ROH(ROGUE_OPERAND_TYPE_REG_TEMP), .min = -1, .max = -1, .align = -1, },
		},
	},
};

/**
 * \brief Validates an operand.
 *
 * \param[in] operand The operand.
 * \return true if valid, otherwise false.
 */
bool rogue_validate_operand(const struct rogue_operand *operand)
{
   ASSERT_OPERAND_RANGE(operand->type);

   switch (operand->type) {
   case ROGUE_OPERAND_TYPE_IMMEDIATE:
      return true;

   case ROGUE_OPERAND_TYPE_DRC:
      CHECKF(operand->drc.number < ROGUE_NUM_DRCS,
             "Invalid DRC number '%zu'.",
             operand->drc.number);
      return true;

   case ROGUE_OPERAND_TYPE_REG_TEMP:
   case ROGUE_OPERAND_TYPE_REG_COEFF:
   case ROGUE_OPERAND_TYPE_REG_CONST:
   case ROGUE_OPERAND_TYPE_REG_SHARED:
   case ROGUE_OPERAND_TYPE_REG_PIXEL_OUT:
   case ROGUE_OPERAND_TYPE_REG_VERTEX_IN:
   case ROGUE_OPERAND_TYPE_REG_INTERNAL:
      CHECKF(operand->reg.number < reg_rules[operand->type].max,
             "Register number '%zu' out of range.",
             operand->reg.number);
      return true;

   default:
      break;
   }

   return false;
}

/**
 * \brief Validates an instruction.
 *
 * \param[in] instr The instruction.
 * \return true if valid, otherwise false.
 */
bool rogue_validate_instr(const struct rogue_instr *instr)
{
   const struct rogue_instr_rule *rule;

   ASSERT_OPCODE_RANGE(instr->opcode);

   rule = &instr_rules[instr->opcode];

   /* Validate flags. */
   CHECKF(rogue_check_bitset(instr->flags, rule->flags),
          "Invalid instruction flags specified.");

   /* Validate number of operands. */
   CHECKF(instr->num_operands == rule->num_operands,
          "Invalid number of operands specified.");

   CHECK(!rule->num_operands || instr->operands);
   for (size_t u = 0U; u < instr->num_operands; ++u) {
      /* Validate operand types. */
      CHECKF(rogue_check_bitset(rogue_onehot(instr->operands[u].type),
                                rule->operand_rules[u].mask),
             "Invalid type for operand %zu.",
             u);

      /* Validate immediate ranges. */
      if (rogue_check_bitset(rogue_onehot(instr->operands[u].type),
                             ROH(ROGUE_OPERAND_TYPE_IMMEDIATE)) &&
          rule->operand_rules[u].min != -1 &&
          rule->operand_rules[u].max != -1) {
         CHECKF(
            instr->operands[u].immediate.value >= rule->operand_rules[u].min &&
               instr->operands[u].immediate.value <= rule->operand_rules[u].max,
            "Immediate value out of range for operand %zu.",
            u);
      }

      /* Validate register alignment. */
      if (rogue_check_bitset(rogue_onehot(instr->operands[u].type),
                             ROGUE_MASK_ANY_REG) &&
          rule->operand_rules[u].align != -1) {
         CHECKF(!(instr->operands[u].reg.number % rule->operand_rules[u].align),
                "Invalid register alignment in operand %zu.",
                u);
      }

      /* Validate each operand. */
      CHECKF(rogue_validate_operand(&instr->operands[u]),
             "Failed to validate operand.");
   }

   return true;
}

/**
 * \brief Validates a shader.
 *
 * \param[in] shader The shader.
 * \return true if valid, otherwise false.
 */
bool rogue_validate_shader(const struct rogue_shader *shader)
{
   CHECK(!list_is_empty(&shader->instr_list));
   ASSERT_SHADER_STAGE_RANGE(shader->stage);

   /* Shader stage-specific validation. */
   switch (shader->stage) {
   case MESA_SHADER_VERTEX:
      /* Make sure there is (only) one end vertex shader instruction. */
      CHECKF(rogue_shader_instr_count_type(shader, ROGUE_OP_END_VERT) == 1,
             "Shader must contain a single end.vert instruction.");

      /* Make sure the end vertex shader instruction is the last one. */
      CHECKF(instr_last_entry(&shader->instr_list)->opcode == ROGUE_OP_END_VERT,
             "end.vert not last instruction.");
      break;

   case MESA_SHADER_FRAGMENT:
      /* Make sure there is (only) one end fragment shader instruction. */
      CHECKF(rogue_shader_instr_count_type(shader, ROGUE_OP_END_FRAG) == 1,
             "Shader must contain a single end.frag instruction.");

      /* Make sure the end fragment shader instruction is the last one. */
      CHECKF(instr_last_entry(&shader->instr_list)->opcode == ROGUE_OP_END_FRAG,
             "end.frag not last instruction.");
      break;

   default:
      return false;
   }

   /* Validate each instruction. */
   foreach_instr (instr, &shader->instr_list)
      CHECKF(rogue_validate_instr(instr), "Failed to validate instruction.");

   return true;
}
