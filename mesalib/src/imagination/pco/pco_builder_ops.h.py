# Copyright © 2024 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

from mako.template import Template, exceptions
from pco_ops import *

template = """/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PCO_BUILDER_OPS_H
#define PCO_BUILDER_OPS_H

/**
 * \\file pco_builder_ops.h
 *
 * \\brief PCO op building functions.
 */

#include "pco_internal.h"
#include "pco_common.h"
#include "pco_ops.h"
#include "util/macros.h"

#include <assert.h>
#include <stdbool.h>

/** Ref mod info. */
static inline
bool pco_instr_dest_has_mod(const pco_instr *instr, unsigned dest, enum pco_ref_mod mod)
{
   const struct pco_op_info *info = &pco_op_info[instr->op];
   assert(dest < info->num_dests);
   assert(mod < _PCO_REF_MOD_COUNT);
   return (info->dest_mods[dest] & (1ULL << mod)) != 0;
}

static inline
bool pco_instr_src_has_mod(const pco_instr *instr, unsigned src, enum pco_ref_mod mod)
{
   const struct pco_op_info *info = &pco_op_info[instr->op];
   assert(src < info->num_srcs);
   assert(mod < _PCO_REF_MOD_COUNT);
   return (info->src_mods[src] & (1ULL << mod)) != 0;
}

% for ref_mod in ref_mods.values():
static inline
bool pco_instr_dest_has_${ref_mod.t.tname}(const pco_instr *instr, unsigned dest)
{
   return pco_instr_dest_has_mod(instr, dest, ${ref_mod.cname});
}

static inline
bool pco_instr_src_has_${ref_mod.t.tname}(const pco_instr *instr, unsigned src)
{
   return pco_instr_src_has_mod(instr, src, ${ref_mod.cname});
}

% endfor
/** Op mod getting/setting. */
static inline
bool pco_instr_has_mod(const pco_instr *instr, enum pco_op_mod mod)
{
   assert(mod < _PCO_OP_MOD_COUNT);
   return (pco_op_info[instr->op].mods & (1ULL << mod)) != 0;
}

static inline
void pco_instr_set_mod(pco_instr *instr, enum pco_op_mod mod, uint32_t val)
{
   assert(mod < _PCO_OP_MOD_COUNT);
   unsigned mod_index = pco_op_info[instr->op].mod_map[mod];
   assert(mod_index > 0);
   instr->mod[mod_index - 1] = val;
}

static inline
uint32_t pco_instr_get_mod(const pco_instr *instr, enum pco_op_mod mod)
{
   assert(mod < _PCO_OP_MOD_COUNT);
   unsigned mod_index = pco_op_info[instr->op].mod_map[mod];
   assert(mod_index > 0);
   return instr->mod[mod_index - 1];
}

static inline
bool pco_instr_mod_is_set(const pco_instr *instr, enum pco_op_mod mod)
{
   const struct pco_op_mod_info *info = &pco_op_mod_info[mod];
   return pco_instr_get_mod(instr, mod) != (info->nzdefault ? info->nzdefault : 0);
}

% for op_mod in op_mods.values():
static inline
bool pco_instr_has_${op_mod.t.tname}(const pco_instr *instr)
{
   return pco_instr_has_mod(instr, ${op_mod.cname});
}

static inline
void pco_instr_set_${op_mod.t.tname}(pco_instr *instr, ${op_mod.t.name} val)
{
   return pco_instr_set_mod(instr, ${op_mod.cname}, val);
}

static inline
${op_mod.t.name} pco_instr_get_${op_mod.t.tname}(const pco_instr *instr)
{
   return pco_instr_get_mod(instr, ${op_mod.cname});
}

% endfor
% for op in ops.values():
   % if bool(op.op_mods):
struct ${op.bname}_mods {
      % for op_mod in op.op_mods:
  ${op_mod.t.name} ${op_mod.t.tname};
      % endfor
};
   % endif
#define ${op.bname}(${op.builder_params[1]}${op.builder_params[2]}) _${op.bname}(${op.builder_params[1]}${op.builder_params[3]})
static
pco_instr *_${op.bname}(${op.builder_params[0]})
{
   pco_instr *instr = pco_instr_create(${op.builder_params[4]},
                                       ${op.cname.upper()},
                                       ${'num_dests' if op.num_dests == VARIABLE else op.num_dests},
                                       ${'num_srcs' if op.num_srcs == VARIABLE else op.num_srcs});

   % if op.has_target_cf_node:
   instr->target_cf_node = target_cf_node;
   % endif
   % if op.num_dests == VARIABLE:
   for (unsigned d = 0; d < num_dests; ++d)
      instr->dest[d] = dest[d];
   % else:
      % for d in range(op.num_dests):
   instr->dest[${d}] = dest${d};
      % endfor
   % endif
   % if op.num_srcs == VARIABLE:
   for (unsigned s = 0; s < num_srcs; ++s)
      instr->src[s] = src[s];
   % else:
      % for s in range(op.num_srcs):
   instr->src[${s}] = src${s};
      % endfor
   % endif

   % for op_mod in op.op_mods:
      % if op_mod.t.nzdefault is None:
   pco_instr_set_${op_mod.t.tname}(instr, mods.${op_mod.t.tname});
      % else:
   pco_instr_set_${op_mod.t.tname}(instr, !mods.${op_mod.t.tname} ? ${op_mod.t.nzdefault} : mods.${op_mod.t.tname});
      % endif
   % endfor

   % if op.op_type != 'hw_direct':
   pco_builder_insert_instr(b, instr);
   % endif
   return instr;
}

% endfor
#endif /* PCO_BUILDER_OPS_H */"""

def main():
   try:
      print(Template(template).render(ops=ops, op_mods=op_mods, ref_mods=ref_mods, VARIABLE=VARIABLE))
   except:
       raise Exception(exceptions.text_error_template().render())

if __name__ == '__main__':
   main()
