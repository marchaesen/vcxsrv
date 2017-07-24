COPYRIGHT = """\
/*
 * Copyright (C) 2017 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
"""

import argparse
import json
from sys import stdout
from mako.template import Template

def collect_data(spirv, kind):
    for x in spirv["operand_kinds"]:
        if x["kind"] == kind:
            operands = x
            break

    # There are some duplicate values in some of the tables (thanks guys!), so
    # filter them out.
    last_value = -1
    values = []
    for x in operands["enumerants"]:
        if x["value"] != last_value:
            last_value = x["value"]
            values.append(x["enumerant"])

    return (kind, values)

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("json")
    p.add_argument("out")
    return p.parse_args()

TEMPLATE  = Template(COPYRIGHT + """\
#include "spirv_info.h"
% for kind,values in info:

const char *
spirv_${kind.lower()}_to_string(Spv${kind} v)
{
   switch (v) {
    % for name in values:
   case Spv${kind}${name}: return "Spv${kind}${name}";
    % endfor
   case Spv${kind}Max: break; /* silence warnings about unhandled enums. */
   }

   return "unknown";
}
% endfor
""")

if __name__ == "__main__":
    pargs = parse_args()

    spirv_info = json.JSONDecoder().decode(open(pargs.json, "r").read())

    capabilities = collect_data(spirv_info, "Capability")
    decorations = collect_data(spirv_info, "Decoration")

    with open(pargs.out, 'w') as f:
        f.write(TEMPLATE.render(info=[capabilities, decorations]))
