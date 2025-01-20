/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "ac_nir_helpers.h"

#include "nir_builder.h"

static bool
is_sin_cos(const nir_instr *instr, UNUSED const void *_)
{
   return instr->type == nir_instr_type_alu && (nir_instr_as_alu(instr)->op == nir_op_fsin ||
                                                nir_instr_as_alu(instr)->op == nir_op_fcos);
}

static nir_def *
lower_sin_cos(struct nir_builder *b, nir_instr *instr, UNUSED void *_)
{
   nir_alu_instr *sincos = nir_instr_as_alu(instr);
   nir_def *src = nir_fmul_imm(b, nir_ssa_for_alu_src(b, sincos, 0), 0.15915493667125702);
   return sincos->op == nir_op_fsin ? nir_fsin_amd(b, src) : nir_fcos_amd(b, src);
}

bool
ac_nir_lower_sin_cos(nir_shader *shader)
{
   return nir_shader_lower_instructions(shader, is_sin_cos, lower_sin_cos, NULL);
}
