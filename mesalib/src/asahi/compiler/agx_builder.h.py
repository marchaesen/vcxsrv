template = """/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _AGX_BUILDER_
#define _AGX_BUILDER_

#include "agx_compiler.h"

static inline agx_instr *
agx_alloc_instr(agx_builder *b, enum agx_opcode op)
{
   agx_instr *I = rzalloc(b->shader, agx_instr);
   I->op = op;
   return I;
}

% for opcode in opcodes:
<%
   op = opcodes[opcode]
   dests = op.dests
   srcs = op.srcs
   imms = op.imms
   suffix = "_to" if dests > 0 else ""
%>

static inline agx_instr *
agx_${opcode}${suffix}(agx_builder *b

% for dest in range(dests):
   , agx_index dst${dest}
% endfor

% for src in range(srcs):
   , agx_index src${src}
% endfor

% for imm in imms:
   , ${imm.ctype} ${imm.name}
% endfor

) {
   agx_instr *I = agx_alloc_instr(b, AGX_OPCODE_${opcode.upper()});

% for dest in range(dests):
   I->dest[${dest}] = dst${dest};
% endfor

% for src in range(srcs):
   I->src[${src}] = src${src};
% endfor

% for imm in imms:
   I->${imm.name} = ${imm.name};
% endfor

   agx_builder_insert(&b->cursor, I);
   return I;
}

% if dests == 1:
static inline agx_index
agx_${opcode}(agx_builder *b

% if srcs == 0:
   , unsigned size
% endif

% for src in range(srcs):
   , agx_index src${src}
% endfor

% for imm in imms:
   , ${imm.ctype} ${imm.name}
% endfor

) {
<%
   args = ["tmp"]
   args += ["src" + str(i) for i in range(srcs)]
   args += [imm.name for imm in imms]
%>
% if srcs == 0:
   agx_index tmp = agx_temp(b->shader, agx_size_for_bits(size));
% else:
   agx_index tmp = agx_temp(b->shader, src0.size);
% endif
   agx_${opcode}_to(b, ${", ".join(args)});
   return tmp;
}
% endif

% endfor

/* Convenience methods */

enum agx_bitop_table {
   AGX_BITOP_NOT = 0x5,
   AGX_BITOP_XOR = 0x6,
   AGX_BITOP_AND = 0x8,
   AGX_BITOP_MOV = 0xA,
   AGX_BITOP_OR  = 0xE
};

#define UNOP_BITOP(name, table) \
   static inline agx_instr * \
   agx_## name ##_to(agx_builder *b, agx_index dst0, agx_index src0) \
   { \
      return agx_bitop_to(b, dst0, src0, agx_zero(), AGX_BITOP_ ## table); \
   }

#define BINOP_BITOP(name, table) \
   static inline agx_instr * \
   agx_## name ##_to(agx_builder *b, agx_index dst0, agx_index src0, agx_index src1) \
   { \
      return agx_bitop_to(b, dst0, src0, src1, AGX_BITOP_ ## table); \
   }

UNOP_BITOP(mov, MOV)
UNOP_BITOP(not, NOT)

BINOP_BITOP(and, AND)
BINOP_BITOP(xor, XOR)
BINOP_BITOP(or, OR)

#undef UNOP_BITOP
#undef BINOP_BITOP

static inline agx_instr *
agx_fmov_to(agx_builder *b, agx_index dst0, agx_index src0)
{
   return agx_fadd_to(b, dst0, src0, agx_negzero());
}

static inline agx_instr *
agx_push_exec(agx_builder *b, unsigned n)
{
   return agx_if_fcmp(b, agx_zero(), agx_zero(), n, AGX_FCOND_EQ, false);
}

static inline agx_instr *
agx_ushr_to(agx_builder *b, agx_index dst, agx_index s0, agx_index s1)
{
    return agx_bfeil_to(b, dst, agx_zero(), s0, s1, 0);
}

static inline agx_index
agx_ushr(agx_builder *b, agx_index s0, agx_index s1)
{
    agx_index tmp = agx_temp(b->shader, s0.size);
    agx_ushr_to(b, tmp, s0, s1);
    return tmp;
}

#endif
"""

from mako.template import Template
from agx_opcodes import opcodes

print(Template(template).render(opcodes=opcodes))
