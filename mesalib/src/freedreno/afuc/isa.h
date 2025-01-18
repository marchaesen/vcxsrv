/*
 * Copyright © 2020 Google, Inc.
 * Copyright © 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef _ISA_H_
#define _ISA_H_

#include <stdlib.h>

#include "compiler/isaspec/isaspec.h"
#include "afuc.h"

static inline struct afuc_instr *__instruction_create(afuc_opc opc)
{
   struct afuc_instr *instr = calloc(1, sizeof(struct afuc_instr));

   switch (opc) {
#define ALU(name) \
   case OPC_##name##I: \
      instr->opc = OPC_##name; \
      instr->has_immed = true; \
      break;
   ALU(ADD)
   ALU(ADDHI)
   ALU(SUB)
   ALU(SUBHI)
   ALU(AND)
   ALU(OR)
   ALU(XOR)
   ALU(NOT)
   ALU(SHL)
   ALU(USHR)
   ALU(ISHR)
   ALU(ROT)
   ALU(MUL8)
   ALU(MIN)
   ALU(MAX)
   ALU(CMP)
#undef ALU

   default:
      instr->opc = opc;
   }

   return instr;
}

#endif /* _ISA_H_ */
