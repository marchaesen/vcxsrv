# Copyright © 2024 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

from mako.template import Template, exceptions
from pco_map import *

template = """/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PCO_MAP_H
#define PCO_MAP_H

/**
 * \\file pco_map.h
 *
 * \\brief PCO mapping definitions.
 */

#include "pco_builder.h"
#include "pco_common.h"
#include "pco_internal.h"
#include "pco_isa.h"
#include "pco_ops.h"
#include "util/macros.h"

#include <stdbool.h>

/* Enum mappings. */
% for enum_map in enum_maps.values():
static inline
${enum_map.type_to} ${enum_map.name}(${enum_map.type_from} val)
{
   switch (val) {
   % for elem_from, elem_to in enum_map.mappings:
   case ${elem_from}:
      return ${elem_to};

   % endfor
   default: break;
   }

   unreachable();
}

% endfor
static inline
enum pco_regbank pco_map_reg_bank(pco_ref ref)
{
   enum pco_regbank regbank = pco_map_reg_class_to_regbank(pco_ref_get_reg_class(ref));
   return pco_ref_is_idx_reg(ref) ? regbank + ref.idx_reg.num : regbank;
}

static inline
unsigned pco_map_reg_bank_bits(pco_ref ref)
{
   return util_last_bit(pco_map_reg_bank(ref));
}

static inline
enum pco_idxbank pco_map_idx_bank(pco_ref ref)
{
   assert(pco_ref_is_idx_reg(ref));
   return pco_map_reg_class_to_idxbank(ref.reg_class);
}

static inline unsigned pco_map_reg_index(pco_ref ref)
{
   if (pco_ref_is_reg(ref)) {
      switch (ref.reg_class) {
      case PCO_REG_CLASS_TEMP:
      case PCO_REG_CLASS_VTXIN:
      case PCO_REG_CLASS_COEFF:
      case PCO_REG_CLASS_SHARED:
      case PCO_REG_CLASS_SPEC:
      case PCO_REG_CLASS_CONST:
         return ref.val;

      case PCO_REG_CLASS_INTERN:
         return ref.val + PCO_SR_INTL0;

      case PCO_REG_CLASS_PIXOUT:
         if (ref.val >= 4)
            return ref.val + PCO_SR_PIXOUT4 - 4;
         else
            return ref.val + PCO_SR_PIXOUT0;

      case PCO_REG_CLASS_GLOBAL:
         return ref.val + PCO_SR_GLOBAL0;

      case PCO_REG_CLASS_SLOT:
         return (7 - ref.val) + PCO_SR_SLOT7;

      default:
         break;
      }
   } else if (pco_ref_is_idx_reg(ref)) {
      return pco_map_idx_bank(ref) | (ref.idx_reg.offset << 3);
   }

   unreachable();
}

static inline unsigned pco_map_reg_index_bits(pco_ref ref)
{
   if (pco_ref_is_reg(ref))
      return util_last_bit(pco_map_reg_index(ref));
   else if (pco_ref_is_idx_reg(ref))
      return 11;

   unreachable();
}

static inline
enum pco_igrp_hdr_variant pco_igrp_hdr_variant(pco_igrp *igrp)
{
   if (igrp->hdr.alutype == PCO_ALUTYPE_BITWISE)
      return PCO_IGRP_HDR_BITWISE;
   else if (igrp->hdr.alutype == PCO_ALUTYPE_CONTROL)
      return PCO_IGRP_HDR_CONTROL;
   else
      assert(igrp->hdr.alutype == PCO_ALUTYPE_MAIN);

   if (!igrp->hdr.end && !igrp->hdr.atom && !igrp->hdr.rpt && !igrp->hdr.cc)
      return PCO_IGRP_HDR_MAIN_BRIEF;

   return PCO_IGRP_HDR_MAIN;
}

static inline
enum pco_src_variant pco_igrp_src_variant(const pco_igrp *igrp,
                                          bool is_upper)
{
   unsigned offset = is_upper ? ROGUE_ALU_INPUT_GROUP_SIZE : 0;

   pco_ref sA = igrp->srcs.s[0 + offset];
   pco_ref sB = igrp->srcs.s[1 + offset];
   pco_ref sC = igrp->srcs.s[2 + offset];
   pco_ref mux = is_upper ? pco_ref_null() : igrp->iss.is[0];

   bool sA_set = !pco_ref_is_null(sA);
   bool sB_set = !pco_ref_is_null(sB);
   bool sC_set = !pco_ref_is_null(sC);
   bool mux_set = !pco_ref_is_null(mux);

   int sbA_bits = sA_set ? pco_map_reg_bank_bits(sA) : -1;
   int sA_bits = sA_set ? pco_map_reg_index_bits(sA) : -1;

   int sbB_bits = sB_set ? pco_map_reg_bank_bits(sB) : -1;
   int sB_bits = sB_set ? pco_map_reg_index_bits(sB) : -1;

   int sbC_bits = sC_set ? pco_map_reg_bank_bits(sC) : -1;
   int sC_bits = sC_set ? pco_map_reg_index_bits(sC) : -1;

   int mux_bits = mux_set ? util_last_bit(pco_map_io_to_is0_sel(pco_ref_get_io(mux))) : -1;

   bool is_ctrl = igrp->hdr.alutype == PCO_ALUTYPE_CONTROL;
   bool no_srcs_set = !sA_set && !sB_set && !sC_set && !mux_set;
   if (is_ctrl && no_srcs_set)
      return PCO_SRC_NONE;

% for variant, spec in [(bs.name.upper(), bs.data) for bs in I_SRC.bit_structs.values()]:
   ${'else ' if not loop.first else ''}if (${'!' if not spec.is_upper else ''}is_upper &&
            sbA_bits ${'==' if spec.sbA_bits == -1 else '<='} ${spec.sbA_bits} && sA_bits ${'==' if spec.sA_bits == -1 else '<='} ${spec.sA_bits} &&
            sbB_bits ${'==' if spec.sbB_bits == -1 else '<='} ${spec.sbB_bits} && sB_bits ${'==' if spec.sB_bits == -1 else '<='} ${spec.sB_bits} &&
            sbC_bits ${'==' if spec.sbC_bits == -1 else '<='} ${spec.sbC_bits} && sC_bits ${'==' if spec.sC_bits == -1 else '<='} ${spec.sC_bits} &&
            mux_bits ${'==' if spec.mux_bits == -1 else '<='} ${spec.mux_bits}) {
      return ${variant};
   }
% endfor

   unreachable();
}

static inline
enum pco_iss_variant pco_igrp_iss_variant(const pco_igrp *igrp)
{
   if (igrp->hdr.alutype == PCO_ALUTYPE_MAIN)
      return PCO_ISS_ISS;

   return PCO_ISS_NONE;
}

static inline
enum pco_dst_variant pco_igrp_dest_variant(pco_igrp *igrp)
{
   pco_ref w0 = igrp->dests.w[0];
   pco_ref w1 = igrp->dests.w[1];

   bool w0_set = !pco_ref_is_null(w0);
   bool w1_set = !pco_ref_is_null(w1);

   bool no_dsts = !w0_set && !w1_set;
   bool one_dest = w0_set != w1_set;
   bool dual_dsts = w0_set && w1_set;

   if (no_dsts)
      return PCO_DST_NONE;

   int db0_bits = w0_set ? pco_map_reg_bank_bits(w0) : -1;
   int d0_bits = w0_set ? pco_map_reg_index_bits(w0) : -1;

   int db1_bits = w1_set ? pco_map_reg_bank_bits(w1) : -1;
   int d1_bits = w1_set ? pco_map_reg_index_bits(w1) : -1;

   int dbN_bits = w0_set ? db0_bits : db1_bits;
   int dN_bits = w0_set ? d0_bits : d1_bits;

   if (one_dest) {
      db0_bits = dbN_bits;
      d0_bits = dN_bits;

      db1_bits = -1;
      d1_bits = -1;
   }

% for variant, spec in [(bs.name.upper(), bs.data) for bs in I_DST.bit_structs.values()]:
   ${'else ' if not loop.first else ''}if (${'!' if not spec.dual_dsts else ''}dual_dsts &&
        db0_bits ${'==' if spec.db0_bits == -1 else '<='} ${spec.db0_bits} && d0_bits ${'==' if spec.d0_bits == -1 else '<='} ${spec.d0_bits} &&
        db1_bits ${'==' if spec.db1_bits == -1 else '<='} ${spec.db1_bits} && d1_bits ${'==' if spec.d1_bits == -1 else '<='} ${spec.d1_bits}) {
      return ${variant};
   }
% endfor

   unreachable();
}

/* Instruction group mappings. */
% for op_map in op_maps.values():
static inline
void ${op_map.name}_map_igrp(pco_igrp *igrp, pco_instr *instr)
{
   % for mapping_group in op_map.igrp_mappings:
      % for mapping in mapping_group:
   ${mapping.format('igrp', 'instr')}
      % endfor

   % endfor
   igrp->variant.hdr = pco_igrp_hdr_variant(igrp);
   igrp->variant.lower_src = pco_igrp_src_variant(igrp, false);
   igrp->variant.upper_src = pco_igrp_src_variant(igrp, true);
   igrp->variant.iss = pco_igrp_iss_variant(igrp);
   igrp->variant.dest = pco_igrp_dest_variant(igrp);
}

% endfor
static inline
void pco_map_igrp(pco_igrp *igrp, pco_instr *instr)
{
   switch (instr->op) {
% for op_map in op_maps.values():
   case ${op_map.cop_name}:
      return ${op_map.name}_map_igrp(igrp, instr);

% endfor
   default:
      break;
   }

   const struct pco_op_info *info = &pco_op_info[instr->op];
   printf("Instruction group mapping not defined for %s op '%s'.\\n",
          info->type == PCO_OP_TYPE_PSEUDO ? "pseudo" : "hardware",
          info->str);

   unreachable();
}

static inline unsigned pco_igrp_hdr_map_encode(uint8_t *bin, pco_igrp *igrp)
{
   switch (igrp->variant.hdr) {
   case PCO_IGRP_HDR_MAIN_BRIEF:
      return pco_igrp_hdr_main_brief_encode(bin,
                                            .da = igrp->hdr.da,
                                            .length = igrp->hdr.length,
                                            .oporg = igrp->hdr.oporg,
                                            .olchk = igrp->hdr.olchk,
                                            .w1p = igrp->hdr.w1p,
                                            .w0p = igrp->hdr.w0p,
                                            .cc = igrp->hdr.cc);

   case PCO_IGRP_HDR_MAIN:
      return pco_igrp_hdr_main_encode(bin,
                                      .da = igrp->hdr.da,
                                      .length = igrp->hdr.length,
                                      .oporg = igrp->hdr.oporg,
                                      .olchk = igrp->hdr.olchk,
                                      .w1p = igrp->hdr.w1p,
                                      .w0p = igrp->hdr.w0p,
                                      .cc = igrp->hdr.cc,
                                      .end = igrp->hdr.end,
                                      .atom = igrp->hdr.atom,
                                      .rpt = igrp->hdr.rpt);

   case PCO_IGRP_HDR_BITWISE:
      return pco_igrp_hdr_bitwise_encode(bin,
                                         .da = igrp->hdr.da,
                                         .length = igrp->hdr.length,
                                         .opcnt = igrp->hdr.opcnt,
                                         .olchk = igrp->hdr.olchk,
                                         .w1p = igrp->hdr.w1p,
                                         .w0p = igrp->hdr.w0p,
                                         .cc = igrp->hdr.cc,
                                         .end = igrp->hdr.end,
                                         .atom = igrp->hdr.atom,
                                         .rpt = igrp->hdr.rpt);

   case PCO_IGRP_HDR_CONTROL:
      return pco_igrp_hdr_control_encode(bin,
                                         .da = igrp->hdr.da,
                                         .length = igrp->hdr.length,
                                         .olchk = igrp->hdr.olchk,
                                         .w1p = igrp->hdr.w1p,
                                         .w0p = igrp->hdr.w0p,
                                         .cc = igrp->hdr.cc,
                                         .miscctl = igrp->hdr.miscctl,
                                         .ctrlop = igrp->hdr.ctrlop);

   default:
      break;
   }

   unreachable();
}


% for op_map in encode_maps.values():
static inline
   % if len(op_map.encode_variants) > 1:
unsigned ${op_map.name}_map_encode(uint8_t *bin, pco_instr *instr, unsigned variant)
{
   switch (variant) {
   % for variant, mapping in op_map.encode_variants:
   case ${variant}:
      return ${mapping.format('bin', 'instr', 'variant')};

   % endfor
   default:
      break;
   }

   unreachable();
}
   % else:
unsigned ${op_map.name}_map_encode(uint8_t *bin, pco_instr *instr)
{
   return ${op_map.encode_variants[0][1].format('bin', 'instr', 'variant')};
}
   % endif

% endfor
static inline
unsigned pco_instr_map_encode(uint8_t *bin, pco_igrp *igrp, enum pco_op_phase phase)
{
   pco_instr *instr = igrp->instrs[phase];
   switch (instr->op) {
% for op_map in encode_maps.values():
   case ${op_map.cop_name}:
   % if len(op_map.encode_variants) > 1:
      return ${op_map.name}_map_encode(bin, instr, pco_igrp_variant(igrp, phase));
   % else:
      return ${op_map.name}_map_encode(bin, instr);
   % endif

% endfor
   default:
      break;
   }

   unreachable();
}

static inline
unsigned pco_srcs_map_encode(uint8_t *bin, pco_igrp *igrp, bool is_upper)
{
   unsigned offset = is_upper ? ROGUE_ALU_INPUT_GROUP_SIZE : 0;

   pco_ref _sA = igrp->srcs.s[0 + offset];
   pco_ref _sB = igrp->srcs.s[1 + offset];
   pco_ref _sC = igrp->srcs.s[2 + offset];
   pco_ref _mux = is_upper ? pco_ref_null() : igrp->iss.is[0];

   bool sA_set = !pco_ref_is_null(_sA);
   bool sB_set = !pco_ref_is_null(_sB);
   bool sC_set = !pco_ref_is_null(_sC);
   bool mux_set = !pco_ref_is_null(_mux);

   unsigned sbA = sA_set ? pco_map_reg_bank(_sA) : 0;
   unsigned sA = sA_set ? pco_map_reg_index(_sA) : 0;

   unsigned sbB = sB_set ? pco_map_reg_bank(_sB) : 0;
   unsigned sB = sB_set ? pco_map_reg_index(_sB) : 0;

   unsigned sbC = sC_set ? pco_map_reg_bank(_sC) : 0;
   unsigned sC = sC_set ? pco_map_reg_index(_sC) : 0;

   unsigned mux = mux_set ? pco_map_io_to_is0_sel(pco_ref_get_io(_mux)) : 0;

   enum pco_src_variant variant = is_upper ? igrp->variant.upper_src : igrp->variant.lower_src;
   switch (variant) {
% for variant, encode_func, spec, offset in [(bs.name.upper(), f'{bs.name}_encode', bs.data, 3 if bs.data.is_upper else 0) for bs in I_SRC.bit_structs.values()]:
   case ${variant}:
      return ${encode_func}(bin,
   % if spec.sbA_bits != -1:
         .s${offset + 0} = sA,
         .sb${offset + 0} = sbA,
   % endif
   % if spec.sbB_bits != -1:
         .s${offset + 1} = sB,
         .sb${offset + 1} = sbB,
   % endif
   % if spec.sbC_bits != -1:
         .s${offset + 2} = sC,
         .sb${offset + 2} = sbC,
   % endif
   % if spec.mux_bits != -1:
         .is0 = mux,
   % endif
      );

% endfor
   default:
      break;
   }

   unreachable();
}

static inline
unsigned pco_iss_map_encode(uint8_t *bin, pco_igrp *igrp)
{
   bool is5_set = !pco_ref_is_null(igrp->iss.is[5]);
   bool is4_set = !pco_ref_is_null(igrp->iss.is[4]);
   bool is3_set = !pco_ref_is_null(igrp->iss.is[3]);
   bool is2_set = !pco_ref_is_null(igrp->iss.is[2]);
   bool is1_set = !pco_ref_is_null(igrp->iss.is[1]);

   unsigned is5 = is5_set ? pco_map_io_to_is5_sel(pco_ref_get_io(igrp->iss.is[5])) : 0;
   unsigned is4 = is4_set ? pco_map_io_to_is4_sel(pco_ref_get_io(igrp->iss.is[4])) : 0;
   unsigned is3 = is3_set ? pco_map_io_to_is3_sel(pco_ref_get_io(igrp->iss.is[3])) : 0;
   unsigned is2 = is2_set ? pco_map_io_to_is2_sel(pco_ref_get_io(igrp->iss.is[2])) : 0;
   unsigned is1 = is1_set ? pco_map_io_to_is1_sel(pco_ref_get_io(igrp->iss.is[1])) : 0;

   assert(igrp->variant.iss == PCO_ISS_ISS);
   return pco_iss_iss_encode(bin, .is5 = is5, .is4 = is4, .is3 = is3, .is2 = is2, .is1 = is1);
}

static inline
unsigned pco_dests_map_encode(uint8_t *bin, pco_igrp *igrp)
{
   pco_ref w0 = igrp->dests.w[0];
   pco_ref w1 = igrp->dests.w[1];

   bool w0_set = !pco_ref_is_null(w0);
   bool w1_set = !pco_ref_is_null(w1);

   bool one_dest = w0_set != w1_set;

   int db0 = w0_set ? pco_map_reg_bank(w0) : 0;
   int d0 = w0_set ? pco_map_reg_index(w0) : 0;

   int db1 = w1_set ? pco_map_reg_bank(w1) : 0;
   int d1 = w1_set ? pco_map_reg_index(w1) : 0;

   int dbN = w0_set ? db0 : db1;
   int dN = w0_set ? d0 : d1;

   if (one_dest) {
      db0 = dbN;
      d0 = dN;

      db1 = 0;
      d1 = 0;
   }

   switch (igrp->variant.dest) {
% for variant, encode_func, spec in [(bs.name.upper(), f'{bs.name}_encode', bs.data) for bs in I_DST.bit_structs.values()]:
   case ${variant}:
      return ${encode_func}(bin,
   % if spec.db0_bits != -1:
      % if spec.dual_dsts:
         .d0 = d0,
         .db0 = db0,
      % else:
         .dN = d0,
         .dbN = db0,
      % endif
   % endif
   % if spec.db1_bits != -1:
         .d1 = d1,
         .db1 = db1,
   % endif
      );

% endfor
   default:
      break;
   }

   unreachable();
}
#endif /* PCO_MAP_H */"""

def main():
   try:
      print(Template(template).render(enum_maps=enum_maps, op_maps=op_maps, encode_maps=encode_maps, I_SRC=I_SRC, I_DST=I_DST))
   except:
       raise Exception(exceptions.text_error_template().render())

if __name__ == '__main__':
   main()
