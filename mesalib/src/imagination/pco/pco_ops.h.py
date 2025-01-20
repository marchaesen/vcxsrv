# Copyright © 2024 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

from mako.template import Template, exceptions
from pco_ops import *

template = """/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PCO_OPS_H
#define PCO_OPS_H

/**
 * \\file pco_ops.h
 *
 * \\brief PCO op definitions and functions.
 */

#include "util/macros.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#define _PCO_OP_MAX_DESTS ${max([op.num_dests for op in ops.values()])}U
#define _PCO_OP_MAX_SRCS ${max([op.num_srcs for op in ops.values()])}U
#define _PCO_OP_MAX_MODS ${max([len(op.op_mods) for op in ops.values()])}U

/** Ops. */
#define _PCO_OP_COUNT ${len(ops) + 1}U
enum pco_op {
   PCO_OP_NONE,
% for op in ops.values():
   ${op.cname.upper()},
% endfor
};

/** Op mods. */
#define _PCO_OP_MOD_COUNT ${len(op_mods) + 1}U
enum pco_op_mod {
   PCO_OP_MOD_NONE,
% for op_mod in op_mods.values():
   ${op_mod.cname},
% endfor
};

/** Ref mods. */
#define _PCO_REF_MOD_COUNT ${len(ref_mods) + 1}U
enum pco_ref_mod {
   PCO_REF_MOD_NONE,
% for ref_mod in ref_mods.values():
   ${ref_mod.cname},
% endfor
};
#endif /* PCO_OPS_H */"""

def main():
   try:
      print(Template(template).render(ops=ops, op_mods=op_mods, ref_mods=ref_mods))
   except:
       raise Exception(exceptions.text_error_template().render())

if __name__ == '__main__':
   main()
