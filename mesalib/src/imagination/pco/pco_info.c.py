# Copyright © 2024 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

from mako.template import Template, exceptions
from pco_ops import *

template = """/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \\file pco_info.c
 *
 * \\brief PCO info structures.
 */

#include "pco_common.h"
#include "pco_internal.h"
#include "pco_isa.h"
#include "pco_ops.h"

const struct pco_op_info pco_op_info[_PCO_OP_COUNT] = {
% for op in ops.values():
   [${op.cname.upper()}] = {
      .str = "${op.name}",
      .num_dests = ${op.num_dests},
      .num_srcs = ${op.num_srcs},
      .mods = ${op.cop_mods},
      .mod_map = {
% for mod, index in op.op_mod_map.items():
         [${mod}] = ${index},
% endfor
      },
      .dest_mods = {
% for index, cdest_mods in op.cdest_mods.items():
         [${index}] = ${cdest_mods},
% endfor
      },
      .src_mods = {
% for index, csrc_mods in op.csrc_mods.items():
         [${index}] = ${csrc_mods},
% endfor
      },
      .type = PCO_OP_TYPE_${op.op_type.upper()},
      .has_target_cf_node = ${str(op.has_target_cf_node).lower()},
   },
% endfor
};

const struct pco_op_mod_info pco_op_mod_info[_PCO_OP_MOD_COUNT] = {
% for name, op_mod in op_mods.items():
   [${op_mod.cname}] = {
      .print_early = ${str(op_mod.t.print_early).lower()},
      .type = ${op_mod.ctype},
   % if op_mod.t.base_type == BaseType.enum:
      .is_bitset = ${str(op_mod.t.enum.is_bitset).lower()},
      .strs = (const char * []){
      % for elem in op_mod.t.enum.elems.values():
         [${elem.cname}] = "${elem.string}",
      % endfor
      },
   % else:
      .str = "${name}",
   % endif
   % if op_mod.t.nzdefault is not None:
      .nzdefault = ${op_mod.t.nzdefault},
   % endif
   },
% endfor
};

const struct pco_ref_mod_info pco_ref_mod_info[_PCO_REF_MOD_COUNT] = {
% for name, ref_mod in ref_mods.items():
   [${ref_mod.cname}] = {
      .type = ${ref_mod.ctype},
   % if ref_mod.t.base_type == BaseType.enum:
      .is_bitset = ${str(ref_mod.t.enum.is_bitset).lower()},
      .strs = (const char * []){
      % for elem in ref_mod.t.enum.elems.values():
         [${elem.cname}] = "${elem.string}",
      % endfor
      },
   % else:
      .str = "${name}",
   % endif
   },
% endfor
};"""

def main():
   try:
      print(Template(template).render(BaseType=BaseType, ops=ops, op_mods=op_mods, ref_mods=ref_mods))
   except:
       raise Exception(exceptions.text_error_template().render())

if __name__ == '__main__':
   main()
