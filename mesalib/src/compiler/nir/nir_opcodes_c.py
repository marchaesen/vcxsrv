#! /usr/bin/env python
#
# Copyright (C) 2014 Connor Abbott
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#
# Authors:
#    Connor Abbott (cwabbott0@gmail.com)

from nir_opcodes import opcodes
from mako.template import Template

template = Template("""
#include "nir.h"

const nir_op_info nir_op_infos[nir_num_opcodes] = {
% for name, opcode in sorted(opcodes.iteritems()):
{
   .name = "${name}",
   .num_inputs = ${opcode.num_inputs},
   .output_size = ${opcode.output_size},
   .output_type = ${"nir_type_" + opcode.output_type},
   .input_sizes = {
      ${ ", ".join(str(size) for size in opcode.input_sizes) }
   },
   .input_types = {
      ${ ", ".join("nir_type_" + type for type in opcode.input_types) }
   },
   .algebraic_properties =
      ${ "0" if opcode.algebraic_properties == "" else " | ".join(
            "NIR_OP_IS_" + prop.upper() for prop in
               opcode.algebraic_properties.strip().split(" ")) }
},
% endfor
};
""")

print template.render(opcodes=opcodes)
