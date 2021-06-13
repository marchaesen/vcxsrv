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

#include "agx_opcodes.h"

<%
def make_encoding(encoding):
   if encoding is None:
      return "{ 0 }"

   return "{{ {}, {}, {} }}".format(hex(encoding.exact), encoding.length_short, int(encoding.extensible))
%>

const struct agx_opcode_info agx_opcodes_info[AGX_NUM_OPCODES] = {
% for opcode in opcodes:
<%
   op = opcodes[opcode]
   imms = ["AGX_IMMEDIATE_" + imm.name.upper() for imm in op.imms]
   if len(imms) == 0:
      imms = ["0"]
%>
   [AGX_OPCODE_${opcode.upper()}] = {
      "${opcode}", ${op.srcs}, ${op.dests}, ${" | ".join(imms)},
      ${make_encoding(op.encoding_32)},
      ${make_encoding(op.encoding_16)},
      ${int(op.is_float)},
      ${int(op.can_eliminate)},
   },
% endfor
};
"""

from mako.template import Template
from agx_opcodes import opcodes

print(Template(template).render(opcodes=opcodes))
