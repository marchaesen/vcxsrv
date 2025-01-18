/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_group_instrs.c
 *
 * \brief PCO instruction grouping pass.
 */

#include "hwdef/rogue_hw_defs.h"
#include "pco.h"
#include "pco_builder.h"
#include "pco_map.h"
#include "util/macros.h"

#include <stdbool.h>

/**
 * \brief Calculates the decode-assist value for an instruction group.
 *
 * \param[in] igrp PCO instruction group.
 * \return The decode-assist value.
 */
static inline unsigned calc_da(pco_igrp *igrp)
{
   unsigned da = igrp->enc.len.hdr;
   bool no_srcs_dests = !igrp->enc.len.lower_srcs &&
                        !igrp->enc.len.upper_srcs && !igrp->enc.len.dests;

   switch (igrp->hdr.alutype) {
   case PCO_ALUTYPE_MAIN:
   case PCO_ALUTYPE_BITWISE: {
      for (enum pco_op_phase p = _PCO_OP_PHASE_COUNT; p-- > 0;) {
         if (igrp->hdr.alutype == PCO_ALUTYPE_BITWISE || p > PCO_OP_PHASE_1)
            da += igrp->enc.len.instrs[p];
      }
      break;
   }

   case PCO_ALUTYPE_CONTROL:
      if (no_srcs_dests)
         return 0;

      da += igrp->enc.len.instrs[PCO_OP_PHASE_CTRL];
      break;

   default:
      unreachable();
   }

   return da;
}

/**
 * \brief Calculates the lengths for an instruction group.
 *
 * \param[in,out] igrp PCO instruction group.
 * \param[in,out] offset_bytes The cumulative shader offset (in bytes).
 */
static inline void calc_lengths(pco_igrp *igrp, unsigned *offset_bytes)
{
   unsigned total_length = 0;

   igrp->enc.len.hdr = pco_igrp_hdr_bytes(igrp->variant.hdr);
   total_length += igrp->enc.len.hdr;

   igrp->enc.len.lower_srcs = pco_src_bytes(igrp->variant.lower_src);
   total_length += igrp->enc.len.lower_srcs;

   igrp->enc.len.upper_srcs = pco_src_bytes(igrp->variant.upper_src);
   total_length += igrp->enc.len.upper_srcs;

   igrp->enc.len.iss = pco_iss_bytes(igrp->variant.iss);
   total_length += igrp->enc.len.iss;

   igrp->enc.len.dests = pco_dst_bytes(igrp->variant.dest);
   total_length += igrp->enc.len.dests;

   for (enum pco_op_phase phase = 0; phase < _PCO_OP_PHASE_COUNT; ++phase) {
      switch (igrp->hdr.alutype) {
      case PCO_ALUTYPE_MAIN:
         if (phase == PCO_OP_PHASE_BACKEND) {
            igrp->enc.len.instrs[phase] =
               pco_backend_bytes(igrp->variant.instr[phase].backend);
         } else {
            igrp->enc.len.instrs[phase] =
               pco_main_bytes(igrp->variant.instr[phase].main);
         }
         break;

      case PCO_ALUTYPE_BITWISE:
         igrp->enc.len.instrs[phase] =
            pco_bitwise_bytes(igrp->variant.instr[phase].bitwise);
         break;

      case PCO_ALUTYPE_CONTROL:
         igrp->enc.len.instrs[phase] =
            pco_ctrl_bytes(igrp->variant.instr[phase].ctrl);
         break;

      default:
         unreachable();
      }

      total_length += igrp->enc.len.instrs[phase];
   }

   igrp->enc.len.word_padding = total_length % 2;
   total_length += igrp->enc.len.word_padding;

   igrp->enc.len.total = total_length;

   /* Set igrp header length and decode-assist. */
   igrp->hdr.length = igrp->enc.len.total / 2;
   igrp->hdr.da = calc_da(igrp);

   /* Set offset and update running offset byte count. */
   igrp->enc.offset = *offset_bytes;
   *offset_bytes += igrp->enc.len.total;
}

/**
 * \brief Calculates the alignment padding to be applied to
 *        the last instruction group in the shader.
 *
 * \param[in,out] last_igrp The last instruction group.
 * \param[in,out] offset_bytes The cumulative shader offset (in bytes).
 */
static inline void calc_align_padding(pco_igrp *last_igrp,
                                      unsigned *offset_bytes)
{
   /* We should never end up with a completely empty shader. */
   assert(last_igrp);

   unsigned total_align = last_igrp->enc.len.total % ROGUE_ICACHE_ALIGN;
   unsigned offset_align = last_igrp->enc.offset % ROGUE_ICACHE_ALIGN;

   if (total_align) {
      unsigned padding = ROGUE_ICACHE_ALIGN - total_align;
      *offset_bytes += padding;

      /* Pad the size of the last igrp. */
      last_igrp->enc.len.align_padding += padding;
      last_igrp->enc.len.total += padding;

      /* Update the last igrp header length. */
      last_igrp->hdr.length = last_igrp->enc.len.total / 2;
   }

   if (offset_align) {
      unsigned padding = ROGUE_ICACHE_ALIGN - offset_align;
      *offset_bytes += padding;

      /* Pad the size of the penultimate igrp. */
      pco_igrp *penultimate_igrp =
         list_entry(last_igrp->link.prev, pco_igrp, link);

      penultimate_igrp->enc.len.align_padding += padding;
      penultimate_igrp->enc.len.total += padding;

      /* Update the penultimate igrp header length. */
      penultimate_igrp->hdr.length = penultimate_igrp->enc.len.total / 2;

      /* Update the offset of the last igrp. */
      last_igrp->enc.offset += padding;
   }
}

/**
 * \brief Converts a PCO instruction to an instruction group.
 *
 * \param[in] b PCO builder.
 * \param[in] instr PCO instruction.
 * \param[out] igrp PCO instruction group.
 * \param[in,out] offset_bytes The cumulative shader offset (in bytes).
 */
static void pco_instr_to_igrp(pco_builder *b,
                              pco_instr *instr,
                              pco_igrp *igrp,
                              unsigned *offset_bytes)
{
   pco_map_igrp(igrp, instr);
   calc_lengths(igrp, offset_bytes);
   pco_builder_insert_igrp(b, igrp);
}

/**
 * \brief Groups PCO instructions into instruction groups.
 *
 * \param[in,out] shader PCO shader.
 * \return True if the pass made progress.
 */
bool pco_group_instrs(pco_shader *shader)
{
   pco_builder b;
   pco_igrp *igrp = NULL;
   unsigned offset_bytes = 0;

   assert(!shader->is_grouped);

   pco_foreach_func_in_shader (func, shader) {
      /* TODO: double check that *start* alignment is satisfied by
       * calc_align_padding when having multiple functions?
       */
      pco_foreach_block_in_func (block, func) {
         b = pco_builder_create(func, pco_cursor_before_block(block));
         pco_foreach_instr_in_block_safe (instr, block) {
            igrp = pco_igrp_create(func);
            pco_instr_to_igrp(&b, instr, igrp, &offset_bytes);
         }
      }

      /* Ensure the final instruction group has a total size and offset
       * that are a multiple of the icache alignment.
       */
      calc_align_padding(igrp, &offset_bytes);
   }

   shader->is_grouped = true;
   return true;
}
