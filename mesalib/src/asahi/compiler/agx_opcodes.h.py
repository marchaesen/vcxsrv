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

#ifndef _AGX_OPCODES_
#define _AGX_OPCODES_

#include <stdbool.h>
#include <stdint.h>

/* Listing of opcodes */

enum agx_opcode {
% for op in opcodes:
   AGX_OPCODE_${op.upper()},
% endfor
   AGX_NUM_OPCODES
};

% for name in enums:
enum agx_${name} {
% for k in enums[name]:
   AGX_${name.upper()}_${enums[name][k].replace('.', '_').upper()} = ${k},
% endfor
};
% endfor

/* Runtime accessible info on each defined opcode */

<% assert(len(immediates) < 32); %>

enum agx_immediate {
% for i, imm in enumerate(immediates):
   AGX_IMMEDIATE_${imm.upper()} = (1 << ${i}),
% endfor
};

struct agx_encoding {
   uint64_t exact;
   unsigned length_short : 4;
   bool extensible : 1;
};

struct agx_opcode_info {
   const char *name;
   unsigned nr_srcs;
   unsigned nr_dests;
   enum agx_immediate immediates;
   struct agx_encoding encoding;
   struct agx_encoding encoding_16;
   bool is_float : 1;
   bool can_eliminate : 1;
};

extern const struct agx_opcode_info agx_opcodes_info[AGX_NUM_OPCODES];

#endif
"""

from mako.template import Template
from agx_opcodes import opcodes, immediates, enums

print(Template(template).render(opcodes=opcodes, immediates=immediates,
         enums=enums))
