#!/usr/bin/env python3
#
# Copyright © 2024 Igalia S.L.
# SPDX-License-Identifier: MIT

template = """/*
 * Copyright © 2024 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "util/macros.h"

#ifdef __cplusplus
extern "C" {
#endif

% for name, enum in enums.items():
enum PACKED ${prefix}_${name} {
% for k, v in enum.items():
   ${k} = ${v},
% endfor
};

% endfor

#ifdef __cplusplus
} /* extern C */
#endif
"""

import argparse
import sys, os
sys.path.append(os.path.dirname(os.path.abspath(__file__)) + "/../../compiler/isaspec")

from mako.template import Template
from isa import ISA

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--xml', required=True, type=str, action="store",
                        help='source isaspec xml file')
    parser.add_argument('--output', required=True, type=str, action="store",
                        help='output C header file')
    args = parser.parse_args()

    isa = ISA(args.xml)
    prefix = 'isa'
    enums = {}

    for name, enum in isa.enums.items():
        name = name.replace('#', '')
        e = {}

        for k, v in enum.values.items():
            v = v.get_name().replace('.', '').replace('[', '').replace(']', '').upper()
            item = prefix.upper() + '_' + name.upper() + '_' + v
            e[item] = k

        enums[name] = e

    opc = {}

    for instr in isa.instructions():
        pattern = instr.xml.findall('pattern')
        bit05 = int(pattern[0].text, 2)
        bit6 = int(pattern[1].text, 2)
        num = bit05 | (bit6 << 6)

        opc[prefix.upper() + '_OPC_' + instr.name.upper()] = num

    opc = dict(sorted(opc.items(), key=lambda item: int(item[1])))
    enums['opc'] = opc

    with open(args.output, "w", encoding="UTF-8") as fh:
        fh.write(Template(template).render(prefix=prefix, enums=enums))

if __name__ == '__main__':
    main()
