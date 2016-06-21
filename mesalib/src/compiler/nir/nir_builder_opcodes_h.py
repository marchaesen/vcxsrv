#! /usr/bin/env python

template = """\
/* Copyright (C) 2015 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef _NIR_BUILDER_OPCODES_
#define _NIR_BUILDER_OPCODES_

<%
def src_decl_list(num_srcs):
   return ', '.join('nir_ssa_def *src' + str(i) for i in range(num_srcs))

def src_list(num_srcs):
   return ', '.join('src' + str(i) if i < num_srcs else 'NULL' for i in range(4))
%>

% for name, opcode in sorted(opcodes.iteritems()):
static inline nir_ssa_def *
nir_${name}(nir_builder *build, ${src_decl_list(opcode.num_inputs)})
{
   return nir_build_alu(build, nir_op_${name}, ${src_list(opcode.num_inputs)});
}
% endfor

#endif /* _NIR_BUILDER_OPCODES_ */"""

from nir_opcodes import opcodes
from mako.template import Template

print Template(template).render(opcodes=opcodes)
