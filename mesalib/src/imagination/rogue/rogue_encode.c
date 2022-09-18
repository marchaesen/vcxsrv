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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hwdef/rogue_hw_defs.h"
#include "rogue_encode.h"
#include "rogue_encoders.h"
#include "rogue_operand.h"
#include "rogue_shader.h"
#include "rogue_util.h"
#include "util/bitscan.h"
#include "util/macros.h"

static size_t rogue_encode_reg_bank(const struct rogue_operand *operand)
{
   switch (operand->type) {
   case ROGUE_OPERAND_TYPE_REG_INTERNAL:
   case ROGUE_OPERAND_TYPE_REG_PIXEL_OUT:
   case ROGUE_OPERAND_TYPE_REG_CONST:
      return 0;
   case ROGUE_OPERAND_TYPE_REG_TEMP:
      return 1;
   case ROGUE_OPERAND_TYPE_REG_VERTEX_IN:
      return 2;
   case ROGUE_OPERAND_TYPE_REG_COEFF:
      return 3;
   case ROGUE_OPERAND_TYPE_REG_SHARED:
      return 4;
   default:
      break;
   }

   unreachable("Unimplemented register bank.");
}

/**
 * \brief Field mapping type.
 */
enum rogue_map_type {
   ROGUE_MAP_TYPE_INSTR_FLAG = 0,
   ROGUE_MAP_TYPE_OPERAND_FLAG,
   ROGUE_MAP_TYPE_OPERAND,

   ROGUE_MAP_TYPE_COUNT,
};

/**
 * \brief Field mapping rule description.
 */
struct rogue_field_mapping {
   /* Type of mapping being performed. */
   enum rogue_map_type type;

   /* Index of the source operand/flag being mapped. */
   size_t index;

   /* List of ranges to perform mapping. */
   struct rogue_rangelist rangelist;

   /* Function used to encode the input into the value to be mapped. */
   field_encoder_t encoder_fn;
};

/**
 * \brief Instruction encoding rule description.
 */
struct rogue_instr_encoding {
   /* Number of bytes making up the base mask. */
   size_t num_bytes;
   /* Base mask bytes. */
   uint8_t *bytes;

   /* Number of field mappings for this instruction. */
   size_t num_mappings;
   /* Field mappings. */
   struct rogue_field_mapping *mappings;
};

static const
struct rogue_instr_encoding instr_encodings[ROGUE_OP_COUNT] = {
	[ROGUE_OP_NOP] = {
		.num_bytes = 8,
		.bytes = (uint8_t []) { 0x04, 0x80, 0x6e, 0x00, 0xf2, 0xff, 0xff, 0xff },
	},

	[ROGUE_OP_END_FRAG] = {
		.num_bytes = 8,
		.bytes = (uint8_t []) { 0x04, 0x80, 0xee, 0x00, 0xf2, 0xff, 0xff, 0xff },
	},

	[ROGUE_OP_END_VERT] = {
		.num_bytes = 8,
		.bytes = (uint8_t []) { 0x44, 0xa0, 0x80, 0x05, 0x00, 0x00, 0x00, 0xff },
	},

	[ROGUE_OP_WDF] = {
		.num_bytes = 8,
		.bytes = (uint8_t []) { 0x04, 0x80, 0x6a, 0xff, 0xf2, 0xff, 0xff, 0xff },
		.num_mappings = 1,
		.mappings = (struct rogue_field_mapping []) {
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 0,
				.rangelist = {
					.num_ranges = 1,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 47, .num = 1, },
					},
				},
				.encoder_fn = &rogue_encoder_drc,
			},
		},
	},

	[ROGUE_OP_PIX_ITER_W] = {
		.num_bytes = 16,
		.bytes = (uint8_t []) { 0x48, 0x20, 0xb0, 0x01, 0x80, 0x40, 0x80, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xff, 0xf1, 0xff },
		.num_mappings = 6,
		.mappings = (struct rogue_field_mapping []) {
			/* Instruction flag mappings. */
			{
				.type = ROGUE_MAP_TYPE_INSTR_FLAG,
				.index = ROGUE_INSTR_FLAG_SAT,
				.rangelist = {
					.num_ranges = 1,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 100, .num = 1, },
					},
				},
				.encoder_fn = NULL,
			},
			/* Operand mappings. */
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 0,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 43, .num = 2, }, /* SB3(2..1) */
						{ .start = 54, .num = 1, }, /* SB3(0) */
						{ .start = 34, .num = 3, }, /* S3(10..8) */
						{ .start = 41, .num = 2, }, /* S3(7..6) */
						{ .start = 53, .num = 6, }, /* S3(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_11,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 1,
				.rangelist = {
					.num_ranges = 1,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 59, .num = 1, },
					},
				},
				.encoder_fn = &rogue_encoder_drc,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 2,
				.rangelist = {
					.num_ranges = 6,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 59, .num = 1, }, /* SB0(2) */
						{ .start = 76, .num = 1, }, /* SB0(1) */
						{ .start = 94, .num = 1, }, /* SB0(0) */
						{ .start = 57, .num = 1, }, /* S0(7) */
						{ .start = 74, .num = 1, }, /* S0(6) */
						{ .start = 93, .num = 6, }, /* S0(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_8,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 3,
				.rangelist = {
					.num_ranges = 4,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 63, .num = 1, }, /* SB2(2) */
						{ .start = 71, .num = 2, }, /* SB2(1..0) */
						{ .start = 62, .num = 2, }, /* S2(7..6) */
						{ .start = 69, .num = 6, }, /* S2(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_8,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 4,
				.rangelist = {
					.num_ranges = 1,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 99, .num = 4, },
					},
				},
				.encoder_fn = &rogue_encoder_ls_1_16,
			},
		},
	},

	[ROGUE_OP_MAX] = {
		.num_bytes = 16,
		.bytes = (uint8_t []) { 0x68, 0x42, 0xd0, 0x3c, 0xfa, 0x10, 0x87, 0x80, 0xc0, 0x80, 0x10, 0x00, 0x32, 0x80, 0x00, 0xff },
		.num_mappings = 3,
		.mappings = (struct rogue_field_mapping []) {
			/* Operand mappings. */
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 0,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 11, .num = 2, }, /* DBn(2..1) */
						{ .start = 22, .num = 1, }, /* DBn(0) */
						{ .start = 14, .num = 3, }, /* Dn(10..8) */
						{ .start = 9, .num = 2, }, /* Dn(7..6) */
						{ .start = 21, .num = 6, }, /* Dn(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_11,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 1,
				.rangelist = {
					.num_ranges = 7,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 43, .num = 1, }, /* SB0(2) */
						{ .start = 52, .num = 1, }, /* SB0(1) */
						{ .start = 70, .num = 1, }, /* SB0(0) */
						{ .start = 47, .num = 3, }, /* S0(10..8) */
						{ .start = 41, .num = 1, }, /* S0(7) */
						{ .start = 50, .num = 1, }, /* S0(6) */
						{ .start = 69, .num = 6, }, /* S0(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_11,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 2,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 51, .num = 1, }, /* SB1(1) */
						{ .start = 61, .num = 1, }, /* SB1(0) */
						{ .start = 40, .num = 1, }, /* S1(7) */
						{ .start = 49, .num = 2, }, /* S1(6..5) */
						{ .start = 60, .num = 5, }, /* S1(4..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_2_8,
			},
		},
	},

	[ROGUE_OP_MIN] = {
		.num_bytes = 16,
		.bytes = (uint8_t []) { 0x68, 0x42, 0xd0, 0x3c, 0xf0, 0x11, 0x87, 0x80, 0xc0, 0x80, 0x10, 0x00, 0x32, 0x80, 0x00, 0xff },
		.num_mappings = 3,
		.mappings = (struct rogue_field_mapping []) {
			/* Operand mappings. */
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 0,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 11, .num = 2, }, /* DBn(2..1) */
						{ .start = 22, .num = 1, }, /* DBn(0) */
						{ .start = 14, .num = 3, }, /* Dn(10..8) */
						{ .start = 9, .num = 2, }, /* Dn(7..6) */
						{ .start = 21, .num = 6, }, /* Dn(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_11,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 1,
				.rangelist = {
					.num_ranges = 7,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 43, .num = 1, }, /* SB0(2) */
						{ .start = 52, .num = 1, }, /* SB0(1) */
						{ .start = 70, .num = 1, }, /* SB0(0) */
						{ .start = 47, .num = 3, }, /* S0(10..8) */
						{ .start = 41, .num = 1, }, /* S0(7) */
						{ .start = 50, .num = 1, }, /* S0(6) */
						{ .start = 69, .num = 6, }, /* S0(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_11,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 2,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 51, .num = 1, }, /* SB1(1) */
						{ .start = 61, .num = 1, }, /* SB1(0) */
						{ .start = 40, .num = 1, }, /* S1(7) */
						{ .start = 49, .num = 2, }, /* S1(6..5) */
						{ .start = 60, .num = 5, }, /* S1(4..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_2_8,
			},
		},
	},

	[ROGUE_OP_PACK_U8888] = {
		.num_bytes = 16,
		.bytes = (uint8_t []) { 0x58, 0x92, 0x06, 0x9c, 0x20, 0x80, 0x00, 0x00, 0x00, 0x2c, 0x80, 0x00, 0xf2, 0xff, 0xff, 0xff },
		.num_mappings = 2,
		.mappings = (struct rogue_field_mapping []) {
			/* Operand mappings. */
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 0,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 35, .num = 2, }, /* DBn(2..1) */
						{ .start = 46, .num = 1, }, /* DBn(0) */
						{ .start = 38, .num = 3, }, /* Dn(10..8) */
						{ .start = 33, .num = 2, }, /* Dn(7..6) */
						{ .start = 45, .num = 6, }, /* Dn(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_11,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 1,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 75, .num = 2, }, /* SB0(2..1) */
						{ .start = 86, .num = 1, }, /* SB0(0) */
						{ .start = 66, .num = 3, }, /* S0(10..8) */
						{ .start = 73, .num = 2, }, /* S0(7..6) */
						{ .start = 85, .num = 6, }, /* S0(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_11,
			},
		},
	},

	[ROGUE_OP_MOV] = {
		.num_bytes = 16,
		.bytes = (uint8_t []) { 0x48, 0x42, 0xd0, 0x3f, 0x87, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0xf2, 0xff, 0xff, 0xff },
		.num_mappings = 3,
		.mappings = (struct rogue_field_mapping []) {
			/* Instruction flag mappings. */
			{
				.type = ROGUE_MAP_TYPE_INSTR_FLAG,
				.index = ROGUE_INSTR_FLAG_OLCHK,
				.rangelist = {
					.num_ranges = 1,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 115, .num = 1, },
					},
				},
				.encoder_fn = NULL,
			},
			/* Operand mappings. */
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 0,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 35, .num = 2, }, /* DBn(2..1) */
						{ .start = 46, .num = 1, }, /* DBn(0) */
						{ .start = 38, .num = 3, }, /* Dn(10..8) */
						{ .start = 33, .num = 2, }, /* Dn(7..6) */
						{ .start = 45, .num = 6, }, /* Dn(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_11,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 1,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 75, .num = 2, }, /* SB0(2..1) */
						{ .start = 86, .num = 1, }, /* SB0(0) */
						{ .start = 66, .num = 3, }, /* S0(10..8) */
						{ .start = 73, .num = 2, }, /* S0(7..6) */
						{ .start = 85, .num = 6, }, /* S0(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_11,
			},
		},
	},

	[ROGUE_OP_MOV_IMM] = {
		.num_bytes = 16,
		.bytes = (uint8_t []) { 0x88, 0x92, 0x40, 0x91, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0xf2, 0xff, 0xff, 0xff },
		.num_mappings = 2,
		.mappings = (struct rogue_field_mapping []) {
			/* Operand mappings. */
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 0,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 35, .num = 2, }, /* DBn(2..1) */
						{ .start = 46, .num = 1, }, /* DBn(0) */
						{ .start = 38, .num = 3, }, /* Dn(10..8) */
						{ .start = 33, .num = 2, }, /* Dn(7..6) */
						{ .start = 45, .num = 6, }, /* Dn(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_11,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 1,
				.rangelist = {
					.num_ranges = 4,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 71, .num = 8, }, /* imm(31:24) */
						{ .start = 79, .num = 8, }, /* imm(23:16) */
						{ .start = 87, .num = 8, }, /* imm(15:8) */
						{ .start = 95, .num = 8, }, /* imm(7:0) */
					},
				},
				.encoder_fn = &rogue_encoder_imm,
			},
		},
	},

	[ROGUE_OP_FMA] = {
		.num_bytes = 16,
		.bytes = (uint8_t []) { 0x28, 0x02, 0xd0, 0x00, 0x80, 0x40, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0xff, 0xf1, 0xff },
		.num_mappings = 6,
		.mappings = (struct rogue_field_mapping []) {
			/* Instruction flag mappings. */
			{
				.type = ROGUE_MAP_TYPE_INSTR_FLAG,
				.index = ROGUE_INSTR_FLAG_SAT,
				.rangelist = {
					.num_ranges = 1,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 104, .num = 1, },
					},
				},
				.encoder_fn = NULL,
			},
			{
				.type = ROGUE_MAP_TYPE_INSTR_FLAG,
				.index = ROGUE_INSTR_FLAG_LP,
				.rangelist = {
					.num_ranges = 1,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 100, .num = 1, },
					},
				},
				.encoder_fn = NULL,
			},
			/* Operand mappings. */
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 0,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 27, .num = 2, }, /* DBn(2..1) */
						{ .start = 38, .num = 1, }, /* DBn(0) */
						{ .start = 30, .num = 3, }, /* Dn(10..8) */
						{ .start = 25, .num = 2, }, /* Dn(7..6) */
						{ .start = 37, .num = 6, }, /* Dn(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_11,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 1,
				.rangelist = {
					.num_ranges = 6,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 59, .num = 1, }, /* SB0(2) */
						{ .start = 76, .num = 1, }, /* SB0(1) */
						{ .start = 94, .num = 1, }, /* SB0(0) */
						{ .start = 57, .num = 1, }, /* S0(7) */
						{ .start = 74, .num = 1, }, /* S0(6) */
						{ .start = 93, .num = 6, }, /* S0(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_8,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 2,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 75, .num = 1, }, /* SB1(1) */
						{ .start = 85, .num = 1, }, /* SB1(0) */
						{ .start = 56, .num = 1, }, /* S1(7) */
						{ .start = 73, .num = 2, }, /* S1(6..5) */
						{ .start = 84, .num = 5, }, /* S1(4..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_2_8,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 3,
				.rangelist = {
					.num_ranges = 4,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 63, .num = 1, }, /* SB2(2) */
						{ .start = 71, .num = 2, }, /* SB2(1..0) */
						{ .start = 62, .num = 2, }, /* S2(7..6) */
						{ .start = 69, .num = 6, }, /* S2(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_8,
			},
		},
	},

	[ROGUE_OP_MUL] = {
		.num_bytes = 16,
		.bytes = (uint8_t []) { 0x28, 0x02, 0x40, 0x80, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0xff, 0xf2, 0xff, 0xff, 0xff },
		.num_mappings = 5,
		.mappings = (struct rogue_field_mapping []) {
			/* Instruction flag mappings. */
			{
				.type = ROGUE_MAP_TYPE_INSTR_FLAG,
				.index = ROGUE_INSTR_FLAG_SAT,
				.rangelist = {
					.num_ranges = 1,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 108, .num = 1, },
					},
				},
				.encoder_fn = NULL,
			},
			{
				.type = ROGUE_MAP_TYPE_INSTR_FLAG,
				.index = ROGUE_INSTR_FLAG_LP,
				.rangelist = {
					.num_ranges = 1,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 109, .num = 1, },
					},
				},
				.encoder_fn = NULL,
			},
			/* Operand mappings. */
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 0,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 43, .num = 2, }, /* DBn(2..1) */
						{ .start = 54, .num = 1, }, /* DBn(0) */
						{ .start = 46, .num = 3, }, /* Dn(10..8) */
						{ .start = 41, .num = 2, }, /* Dn(7..6) */
						{ .start = 53, .num = 6, }, /* Dn(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_11,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 1,
				.rangelist = {
					.num_ranges = 7,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 75, .num = 1, }, /* SB0(2) */
						{ .start = 84, .num = 1, }, /* SB0(1) */
						{ .start = 102, .num = 1, }, /* SB0(0) */
						{ .start = 79, .num = 3, }, /* S0(10..8) */
						{ .start = 73, .num = 1, }, /* S0(7) */
						{ .start = 82, .num = 1, }, /* S0(6) */
						{ .start = 101, .num = 6, }, /* S0(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_11,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 2,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 83, .num = 1, }, /* SB1(1) */
						{ .start = 93, .num = 1, }, /* SB1(0) */
						{ .start = 72, .num = 1, }, /* S1(7) */
						{ .start = 81, .num = 2, }, /* S1(6..5) */
						{ .start = 92, .num = 5, }, /* S1(4..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_2_8,
			},
		},
	},

	[ROGUE_OP_VTXOUT] = {
		.num_bytes = 16,
		.bytes = (uint8_t []) { 0x48, 0x20, 0x08, 0x00, 0x80, 0x00, 0x00, 0x00, 0x30, 0xff, 0xf3, 0xff, 0xff, 0xff, 0xff, 0xff },
		.num_mappings = 2,
		.mappings = (struct rogue_field_mapping []) {
			/* Operand mappings. */
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 0,
				.rangelist = {
					.num_ranges = 1,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 103, .num = 8, }, /* Immediate address. */
					},
				},
				.encoder_fn = &rogue_encoder_imm,
			},
			{
				.type = ROGUE_MAP_TYPE_OPERAND,
				.index = 1,
				.rangelist = {
					.num_ranges = 5,
					.ranges = (struct rogue_bitrange []) {
						{ .start = 83, .num = 2, }, /* SB0(2..1) */
						{ .start = 94, .num = 1, }, /* SB0(0) */
						{ .start = 74, .num = 3, }, /* S0(10..8) */
						{ .start = 81, .num = 2, }, /* S0(7..6) */
						{ .start = 93, .num = 6, }, /* S0(5..0) */
					},
				},
				.encoder_fn = &rogue_encoder_reg_3_11,
			},
		},
	},
};

/**
 * \brief Applies a boolean flag encoding onto an instruction mask.
 *
 * \param[in] set Whether to set/unset the flag.
 * \param[in] mapping The field mapping to apply.
 * \param[in] instr_size The size of the instruction mask in bytes.
 * \param[in] instr_bytes The instruction mask.
 * \return true if encoding was successful.
 */
static bool rogue_encode_flag(bool set,
                              const struct rogue_field_mapping *mapping,
                              size_t instr_size,
                              uint8_t instr_bytes[instr_size])
{
   return rogue_distribute_value((uint64_t)set,
                                 &mapping->rangelist,
                                 instr_size,
                                 instr_bytes);
}

/**
 * \brief Applies an operand encoding onto an instruction mask.
 *
 * \param[in] operand The operand to apply.
 * \param[in] mapping The field mapping to apply.
 * \param[in] instr_size The size of the instruction mask in bytes.
 * \param[in] instr_bytes The instruction mask.
 * \return true if encoding was successful.
 */
static bool rogue_encode_operand(const struct rogue_operand *operand,
                                 const struct rogue_field_mapping *mapping,
                                 size_t instr_size,
                                 uint8_t instr_bytes[instr_size])
{
   uint64_t value = 0U;

   switch (operand->type) {
   case ROGUE_OPERAND_TYPE_REG_PIXEL_OUT:
      CHECKF(
         mapping->encoder_fn(&value,
                             2,
                             rogue_encode_reg_bank(operand),
                             operand->reg.number + ROGUE_PIXEL_OUT_REG_OFFSET),
         "Failed to encode pixel output register operand.");
      break;
   case ROGUE_OPERAND_TYPE_REG_INTERNAL:
      CHECKF(
         mapping->encoder_fn(&value,
                             2,
                             rogue_encode_reg_bank(operand),
                             operand->reg.number + ROGUE_INTERNAL_REG_OFFSET),
         "Failed to encode internal register operand.");
      break;
   case ROGUE_OPERAND_TYPE_REG_TEMP:
   case ROGUE_OPERAND_TYPE_REG_COEFF:
   case ROGUE_OPERAND_TYPE_REG_CONST:
   case ROGUE_OPERAND_TYPE_REG_SHARED:
   case ROGUE_OPERAND_TYPE_REG_VERTEX_IN:
      CHECKF(mapping->encoder_fn(&value,
                                 2,
                                 rogue_encode_reg_bank(operand),
                                 operand->reg.number),
             "Failed to encode register operand.");
      break;

   case ROGUE_OPERAND_TYPE_IMMEDIATE:
      CHECKF(mapping->encoder_fn(&value, 1, operand->immediate.value),
             "Failed to encode immediate operand.");
      break;

   case ROGUE_OPERAND_TYPE_DRC:
      CHECKF(mapping->encoder_fn(&value, 1, (uint64_t)operand->drc.number),
             "Failed to encode DRC operand.");
      break;

   default:
      return false;
   }

   CHECKF(rogue_distribute_value(value,
                                 &mapping->rangelist,
                                 instr_size,
                                 instr_bytes),
          "Failed to distribute value.");

   return true;
}

/**
 * \brief Applies operand and flag encodings to the base instruction bytes, then
 * writes the result to file pointer "fp".
 *
 * \param[in] instr The instruction to be encoded.
 * \param[in] fp The file pointer.
 * \return true if encoding was successful.
 */
bool rogue_encode_instr(const struct rogue_instr *instr, FILE *fp)
{
   const struct rogue_instr_encoding *instr_encoding;
   size_t instr_size;
   uint8_t instr_bytes[ROGUE_MAX_INSTR_BYTES];

   ASSERT_OPCODE_RANGE(instr->opcode);

   instr_encoding = &instr_encodings[instr->opcode];

   /* Set up base instruction bytes. */
   instr_size = instr_encoding->num_bytes;
   assert(instr_size <= ARRAY_SIZE(instr_bytes));
   memcpy(instr_bytes, instr_encoding->bytes, instr_size);

   /* Encode the operands and flags. */
   for (size_t u = 0U; u < instr_encoding->num_mappings; ++u) {
      const struct rogue_field_mapping *mapping = &instr_encoding->mappings[u];

      switch (mapping->type) {
      case ROGUE_MAP_TYPE_INSTR_FLAG: {
         uint64_t flag = rogue_onehot(mapping->index);
         CHECKF(rogue_encode_flag(!!(instr->flags & flag),
                                  mapping,
                                  instr_size,
                                  instr_bytes),
                "Failed to encode instruction flag.");
         break;
      }

      case ROGUE_MAP_TYPE_OPERAND_FLAG:
         return false;

      case ROGUE_MAP_TYPE_OPERAND: {
         size_t operand_index = mapping->index;
         CHECKF(rogue_encode_operand(&instr->operands[operand_index],
                                     mapping,
                                     instr_size,
                                     instr_bytes),
                "Failed to encode instruction operand.");
         break;
      }

      default:
         return false;
      }
   }

   CHECKF(fwrite(instr_bytes, 1, instr_size, fp) == instr_size,
          "Failed to write encoded instruction bytes.");
   fflush(fp);

   return true;
}

/**
 * \brief Encodes each instruction in "shader", writing the output to "fp".
 *
 * \param[in] shader The shader to be encoded.
 * \param[in] fp The file pointer.
 * \return true if encoding was successful.
 */
bool rogue_encode_shader(const struct rogue_shader *shader, FILE *fp)
{
   long bytes_written;

   /* Encode each instruction. */
   foreach_instr (instr, &shader->instr_list)
      CHECKF(rogue_encode_instr(instr, fp), "Failed to encode instruction.");

   /* Pad end of shader if required. */
   bytes_written = ftell(fp);
   if (bytes_written <= 0)
      return false;

   /* FIXME: Figure out the define for alignment of 16. */
   for (size_t u = 0; u < (bytes_written % 16); ++u)
      fputc(0xff, fp);

   return true;
}
