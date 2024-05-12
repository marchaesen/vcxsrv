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

    values = {}
    for x in operands["enumerants"]:
        name = x["enumerant"]
        val = x["value"]
        if val not in values:
            values[val] = [name]
        else:
            values[val].append(name)

    return (kind, list(values.values()), operands["category"])

def collect_opcodes(spirv):
    seen = set()
    values = []
    for x in spirv["instructions"]:
        # Handle aliases by choosing the first one in the grammar.
        # E.g. OpDecorateString and OpDecorateStringGOOGLE share same opcode.
        if x["opcode"] in seen:
            continue
        opcode = x["opcode"]
        name = x["opname"]
        assert name.startswith("Op")
        values.append([name[2:]])
        seen.add(opcode)

    return ("Op", values, None)

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('--out-c', required=True, help='Output C file.')
    p.add_argument('--out-h', required=True, help='Output H file.')
    p.add_argument('--json', required=True, help='SPIR-V JSON file.')
    return p.parse_args()

TEMPLATE_H  = Template("""\
/* DO NOT EDIT - This file is generated automatically by spirv_info_c.py script */

""" + COPYRIGHT + """\

#ifndef _SPIRV_INFO_H_
#define _SPIRV_INFO_H_

#include <stdbool.h>

#include "compiler/spirv/spirv.h"

% for kind,values,category in info:
% if kind == "Capability":
struct spirv_capabilities {
    % for names in values:
    % if len(names) == 1:
   bool ${names[0]};
    % else:
   union {
    % for name in names:
      bool ${name};
    % endfor
   };
    % endif
    % endfor
};
% endif
% endfor

bool spirv_capabilities_get(const struct spirv_capabilities *caps,
                            SpvCapability cap);
void spirv_capabilities_set(struct spirv_capabilities *caps,
                            SpvCapability cap, bool enabled);

% for kind,values,category in info:
% if category == "BitEnum":
const char *spirv_${kind.lower()}_to_string(Spv${kind}Mask v);
% else:
const char *spirv_${kind.lower()}_to_string(Spv${kind} v);
% endif
% endfor

#endif /* SPIRV_INFO_H */
""")

TEMPLATE_C  = Template("""\
/* DO NOT EDIT - This file is generated automatically by spirv_info_c.py script */

""" + COPYRIGHT + """\
#include "spirv_info.h"

#include "util/macros.h"

% for kind,values,category in info:
% if kind == "Capability":
bool
spirv_capabilities_get(const struct spirv_capabilities *caps,
                       SpvCapability cap)
{
   switch (cap) {
    % for names in values:
   case SpvCapability${names[0]}: return caps->${names[0]};
    % endfor
   default:
      return false;
   }
}

void
spirv_capabilities_set(struct spirv_capabilities *caps,
                       SpvCapability cap, bool enabled)
{
   switch (cap) {
    % for names in values:
   case SpvCapability${names[0]}: caps->${names[0]} = enabled; break;
    % endfor
   default:
      unreachable("Unknown capability");
   }
}
% endif
% endfor

% for kind,values,category in info:

% if category == "BitEnum":
const char *
spirv_${kind.lower()}_to_string(Spv${kind}Mask v)
{
   switch (v) {
    % for names in values:
    %if names[0] != "None":
   case Spv${kind}${names[0]}Mask: return "Spv${kind}${names[0]}";
    % else:
   case Spv${kind}MaskNone: return "Spv${kind}${names[0]}";
    % endif
    % endfor
   }

   return "unknown";
}
% else:
const char *
spirv_${kind.lower()}_to_string(Spv${kind} v)
{
   switch (v) {
    % for names in values:
   case Spv${kind}${names[0]}: return "Spv${kind}${names[0]}";
    % endfor
   case Spv${kind}Max: break; /* silence warnings about unhandled enums. */
   }

   return "unknown";
}
% endif
% endfor
""")

if __name__ == "__main__":
    pargs = parse_args()

    spirv_info = json.JSONDecoder().decode(open(pargs.json, "r").read())

    info = [
        collect_data(spirv_info, "AddressingModel"),
        collect_data(spirv_info, "BuiltIn"),
        collect_data(spirv_info, "Capability"),
        collect_data(spirv_info, "Decoration"),
        collect_data(spirv_info, "Dim"),
        collect_data(spirv_info, "ExecutionMode"),
        collect_data(spirv_info, "ExecutionModel"),
        collect_data(spirv_info, "ImageFormat"),
        collect_data(spirv_info, "MemoryModel"),
        collect_data(spirv_info, "StorageClass"),
        collect_data(spirv_info, "ImageOperands"),
        collect_data(spirv_info, "FPRoundingMode"),
        collect_opcodes(spirv_info),
    ]

    with open(pargs.out_h, 'w', encoding='utf-8') as f:
        f.write(TEMPLATE_H.render(info=info))
    with open(pargs.out_c, 'w', encoding='utf-8') as f:
        f.write(TEMPLATE_C.render(info=info))
