/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_print.c
 *
 * \brief PCO printing functions.
 */

#include "pco.h"
#include "pco_builder.h"
#include "pco_common.h"
#include "pco_internal.h"
#include "util/bitscan.h"
#include "util/list.h"
#include "util/macros.h"
#include "util/u_hexdump.h"

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct _pco_print_state {
   FILE *fp; /** The print target file pointer. */
   pco_shader *shader; /** The shader being printed. */
   unsigned indent; /** The current printing indent. */
   bool is_grouped; /** Whether the shader uses igrps. */
   bool verbose; /** Whether to print additional info. */
} pco_print_state;

/* Forward declarations. */
static void _pco_print_cf_node(pco_print_state *state, pco_cf_node *cf_node);
static void pco_print_block_name(pco_print_state *state, pco_block *block);
static void
pco_print_func_sig(pco_print_state *state, pco_func *func, bool call);

enum color_esc {
   ESC_RESET = 0,
   ESC_BLACK,
   ESC_RED,
   ESC_GREEN,
   ESC_YELLOW,
   ESC_BLUE,
   ESC_PURPLE,
   ESC_CYAN,
   ESC_WHITE,
   _ESC_COUNT,
};

static
const char *color_esc[2][_ESC_COUNT] = {
   [0] = {
      [ESC_RESET] = "",
      [ESC_BLACK] = "",
      [ESC_RED] = "",
      [ESC_GREEN] = "",
      [ESC_YELLOW] = "",
      [ESC_BLUE] = "",
      [ESC_PURPLE] = "",
      [ESC_CYAN] = "",
      [ESC_WHITE] = "",
   },
   [1] = {
      [ESC_RESET] = "\033[0m",
      [ESC_BLACK] = "\033[0;30m",
      [ESC_RED] = "\033[0;31m",
      [ESC_GREEN] = "\033[0;32m",
      [ESC_YELLOW] = "\033[0;33m",
      [ESC_BLUE] = "\033[0;34m",
      [ESC_PURPLE] = "\033[0;35m",
      [ESC_CYAN] = "\033[0;36m",
      [ESC_WHITE] = "\033[0;37m",
   },
};

static inline void RESET(pco_print_state *state)
{
   fputs(color_esc[pco_color][ESC_RESET], state->fp);
}

static inline void BLACK(pco_print_state *state)
{
   fputs(color_esc[pco_color][ESC_BLACK], state->fp);
}

static inline void RED(pco_print_state *state)
{
   fputs(color_esc[pco_color][ESC_RED], state->fp);
}

static inline void GREEN(pco_print_state *state)
{
   fputs(color_esc[pco_color][ESC_GREEN], state->fp);
}

static inline void YELLOW(pco_print_state *state)
{
   fputs(color_esc[pco_color][ESC_YELLOW], state->fp);
}

static inline void BLUE(pco_print_state *state)
{
   fputs(color_esc[pco_color][ESC_BLUE], state->fp);
}

static inline void PURPLE(pco_print_state *state)
{
   fputs(color_esc[pco_color][ESC_PURPLE], state->fp);
}

static inline void CYAN(pco_print_state *state)
{
   fputs(color_esc[pco_color][ESC_CYAN], state->fp);
}

static inline void WHITE(pco_print_state *state)
{
   fputs(color_esc[pco_color][ESC_WHITE], state->fp);
}

inline static const char *true_false_str(bool b)
{
   return b ? "true" : "false";
}

static void
_pco_printf(pco_print_state *state, bool nl, const char *fmt, va_list args)
{
   if (nl)
      for (unsigned u = 0; u < state->indent; ++u)
         fputs("    ", state->fp);

   vfprintf(state->fp, fmt, args);
}

/**
 * \brief Formatted print.
 *
 * \param[in] state Print state.
 * \param[in] fmt Print format.
 */
PRINTFLIKE(2, 3)
static void pco_printf(pco_print_state *state, const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   _pco_printf(state, false, fmt, args);
   va_end(args);
}

/**
 * \brief Formatted print, with indentation.
 *
 * \param[in] state Print state.
 * \param[in] fmt Print format.
 */
PRINTFLIKE(2, 3)
static void pco_printfi(pco_print_state *state, const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   _pco_printf(state, true, fmt, args);
   va_end(args);
}

/**
 * \brief Returns a space if the string is not empty.
 *
 * \param[in] str String.
 * \return A space if the string is not empty, else an empty string.
 */
static inline const char *space_if_str(const char *str)
{
   return str[0] != '\0' ? " " : "";
}

/**
 * \brief Print PCO reference flags/modifiers.
 *
 * \param[in] state Print state.
 * \param[in] ref PCO reference.
 */
static void pco_print_ref_mods(pco_print_state *state, pco_ref ref)
{
   if (ref.oneminus)
      pco_printf(state, ".oneminus");
   if (ref.clamp)
      pco_printf(state, ".clamp");
   if (ref.flr)
      pco_printf(state, ".flr");
   if (ref.abs)
      pco_printf(state, ".abs");
   if (ref.neg)
      pco_printf(state, ".neg");

   u_foreach_bit (e, ref.elem) {
      pco_printf(state, ".e%u", e);
   }
}

/**
 * \brief Print PCO reference color.
 *
 * \param[in] state Print state.
 * \param[in] ref PCO reference.
 */
static void pco_print_ref_color(pco_print_state *state, pco_ref ref)
{
   switch (ref.type) {
   case PCO_REF_TYPE_NULL:
      return;

   case PCO_REF_TYPE_SSA:
   case PCO_REF_TYPE_REG:
   case PCO_REF_TYPE_IDX_REG:
      YELLOW(state);
      return;

   case PCO_REF_TYPE_IMM:
      BLUE(state);
      return;

   case PCO_REF_TYPE_IO:
   case PCO_REF_TYPE_PRED:
   case PCO_REF_TYPE_DRC:
      WHITE(state);
      return;

   default:
      break;
   }

   unreachable();
}

/**
 * \brief Print PCO reference.
 *
 * \param[in] state Print state.
 * \param[in] ref PCO reference.
 */
static void _pco_print_ref(pco_print_state *state, pco_ref ref)
{
   pco_print_ref_color(state, ref);
   pco_printf(state, "%s", pco_ref_type_str(ref.type));

   switch (ref.type) {
   case PCO_REF_TYPE_NULL:
      break;

   case PCO_REF_TYPE_SSA:
      pco_printf(state, "%u", ref.val);
      break;

   case PCO_REF_TYPE_REG:
      pco_printf(state, "%s%u", pco_reg_class_str(ref.reg_class), ref.val);
      break;

   case PCO_REF_TYPE_IDX_REG:
      _pco_print_ref(state, pco_ref_get_idx_pointee(ref));
      pco_print_ref_color(state, ref);
      pco_printf(state, "[idx%u", ref.idx_reg.num);
      break;

   case PCO_REF_TYPE_IMM:
      assert(pco_ref_is_scalar(ref));
      switch (ref.dtype) {
      case PCO_DTYPE_ANY:
         pco_printf(state, "0x%" PRIx64, pco_ref_get_imm(ref));
         break;

      case PCO_DTYPE_UNSIGNED:
         pco_printf(state, "%" PRIu64, pco_ref_get_imm(ref));
         break;

      case PCO_DTYPE_SIGNED:
         pco_printf(state, "%" PRId64, pco_ref_get_imm(ref));
         break;

      case PCO_DTYPE_FLOAT:
         pco_printf(state, "%f", uif(pco_ref_get_imm(ref)));
         break;

      default:
         unreachable();
      }
      pco_printf(state, "%s", pco_dtype_str(ref.dtype));
      break;

   case PCO_REF_TYPE_IO:
      assert(pco_ref_is_scalar(ref));
      pco_printf(state, "%s", pco_io_str(ref.val));
      break;

   case PCO_REF_TYPE_PRED:
      assert(pco_ref_is_scalar(ref));
      pco_printf(state, "%s", pco_pred_str(ref.val));
      break;

   case PCO_REF_TYPE_DRC:
      assert(pco_ref_is_scalar(ref));
      pco_printf(state, "%s", pco_drc_str(ref.val));
      break;

   default:
      unreachable();
   }

   unsigned chans = pco_ref_get_chans(ref);
   if (chans > 1 && !pco_ref_is_ssa(ref))
      pco_printf(state, "..%u", ref.val + chans - 1);

   if (ref.type == PCO_REF_TYPE_IDX_REG)
      pco_printf(state, "]");

   RESET(state);

   /* Modifiers. */
   pco_print_ref_mods(state, ref);
}

/**
 * \brief Print PCO reference specification.
 *
 * \param[in] state Print state.
 * \param[in] ref PCO reference.
 */
static void pco_print_ref_spec(pco_print_state *state, pco_ref ref)
{
   pco_printf(state,
              "(%s%ux%u)",
              pco_dtype_str(pco_ref_get_dtype(ref)),
              pco_ref_get_bits(ref),
              pco_ref_get_chans(ref));
}

/**
 * \brief Print PCO phi source.
 *
 * \param[in] state Print state.
 * \param[in] phi_src PCO phi source.
 */
static void pco_print_phi_src(pco_print_state *state, pco_phi_src *phi_src)
{
   pco_print_block_name(state, phi_src->pred);
   pco_printf(state, ": ");
   _pco_print_ref(state, phi_src->ref);
}

/**
 * \brief Print PCO instruction modifiers.
 *
 * \param[in] state Print state.
 * \param[in] op_info The instruction op info.
 * \param[in] instr PCO instruction.
 * \param[in] print_early Whether the mods are being printed before the
 *            instruction name.
 */
static void pco_print_instr_mods(pco_print_state *state,
                                 const struct pco_op_info *op_info,
                                 pco_instr *instr,
                                 bool print_early)
{
   u_foreach_bit64 (op_mod, op_info->mods) {
      const struct pco_op_mod_info *mod_info = &pco_op_mod_info[op_mod];
      if (mod_info->print_early != print_early)
         continue;

      uint32_t val = pco_instr_get_mod(instr, op_mod);

      switch (mod_info->type) {
      case PCO_MOD_TYPE_BOOL:
         if (val && strlen(mod_info->str)) {
            if (print_early)
               pco_printf(state, "%s ", mod_info->str);
            else
               pco_printf(state, ".%s", mod_info->str);
         }
         break;

      case PCO_MOD_TYPE_UINT:
         if ((!mod_info->nzdefault || val != mod_info->nzdefault) &&
             strlen(mod_info->str)) {
            if (print_early)
               pco_printf(state, "%s%u ", mod_info->str, val);
            else
               pco_printf(state, "%s%u", mod_info->str, val);
         }
         break;

      case PCO_MOD_TYPE_ENUM:
         if (mod_info->is_bitset) {
            u_foreach_bit (bit, val) {
               pco_printf(state, ".%s", mod_info->strs[1U << bit]);
            }
         } else {
            if (strlen(mod_info->strs[val])) {
               if (print_early)
                  pco_printf(state, "%s ", mod_info->strs[val]);
               else
                  pco_printf(state, ".%s", mod_info->strs[val]);
            }
         }
         break;

      default:
         unreachable();
      }
   }
}

/**
 * \brief Print PCO instruction.
 *
 * \param[in] state Print state.
 * \param[in] instr PCO instruction.
 */
static void _pco_print_instr(pco_print_state *state, pco_instr *instr)
{
   const struct pco_op_info *info = &pco_op_info[instr->op];

   if (!state->is_grouped)
      pco_printfi(state, "%04u: ", instr->index);

   /* Early mods. */
   pco_print_instr_mods(state, info, instr, true);

   if (info->type == PCO_OP_TYPE_PSEUDO)
      RED(state);
   else
      GREEN(state);
   pco_printf(state, "%s", info->str);
   RESET(state);

   /* "Late" mods. */
   pco_print_instr_mods(state, info, instr, false);

   bool printed = false;

   /* Destinations. */
   for (unsigned d = 0; d < instr->num_dests; ++d) {
      if (printed)
         pco_printf(state, ",");
      pco_printf(state, " ");
      _pco_print_ref(state, instr->dest[d]);
      printed = true;
   }

   /* Special parameters. */
   if (info->has_target_cf_node) {
      if (printed)
         pco_printf(state, ",");
      pco_printf(state, " ");

      switch (instr->target_cf_node->type) {
      case PCO_CF_NODE_TYPE_BLOCK: {
         pco_block *target_block = pco_cf_node_as_block(instr->target_cf_node);
         pco_printf(state, " ");
         pco_print_block_name(state, target_block);
         break;
      }

      case PCO_CF_NODE_TYPE_FUNC: {
         pco_func *target_func = pco_cf_node_as_func(instr->target_cf_node);
         pco_printf(state, " ");
         pco_print_func_sig(state, target_func, true);
         break;
      }

      default:
         unreachable();
      }
      printed = true;
   } else if (!list_is_empty(&instr->phi_srcs)) {
      pco_foreach_phi_src_in_instr (phi_src, instr) {
         if (printed)
            pco_printf(state, ",");
         pco_printf(state, " ");
         pco_print_phi_src(state, phi_src);
         printed = true;
      }
   }

   /* Sources. */
   for (unsigned s = 0; s < instr->num_srcs; ++s) {
      if (printed)
         pco_printf(state, ",");
      pco_printf(state, " ");
      _pco_print_ref(state, instr->src[s]);
      printed = true;
   }
   pco_printf(state, ";");

   /* Spec for destinations. */
   if (state->verbose && !state->is_grouped && instr->num_dests) {
      pco_printf(state, " /*");

      printed = false;
      for (unsigned d = 0; d < instr->num_dests; ++d) {
         if (printed)
            pco_printf(state, ",");
         pco_printf(state, " ");

         _pco_print_ref(state, instr->dest[d]);
         pco_printf(state, ":");
         pco_print_ref_spec(state, instr->dest[d]);

         printed = true;
      }

      pco_printf(state, " */");
   }

   if (state->verbose && instr->comment)
      pco_printf(state, " /* %s */", instr->comment);
}

/**
 * \brief Print the name of a phase.
 *
 * \param[in] state Print state.
 * \param[in] alutype ALU type.
 * \param[in] phase Phase.
 */
static void pco_print_phase(pco_print_state *state,
                            enum pco_alutype alutype,
                            enum pco_op_phase phase)
{
   switch (alutype) {
   case PCO_ALUTYPE_MAIN:
      pco_printf(state, "%s", pco_op_phase_str(phase));
      return;

   case PCO_ALUTYPE_BITWISE:
      pco_printf(state, "p%c", '0' + phase);
      return;

   case PCO_ALUTYPE_CONTROL:
      pco_printf(state, "ctrl");
      return;

   default:
      break;
   }
   unreachable();
}

/**
 * \brief Print phases present in a PCO instruction group.
 *
 * \param[in] state Print state.
 * \param[in] igrp PCO instruction group.
 */
static void pco_print_igrp_phases(pco_print_state *state, pco_igrp *igrp)
{
   bool printed = false;
   for (enum pco_op_phase phase = 0; phase < _PCO_OP_PHASE_COUNT; ++phase) {
      if (!igrp->instrs[phase])
         continue;

      if (printed)
         pco_printf(state, ",");

      pco_print_phase(state, igrp->hdr.alutype, phase);

      printed = true;
   }
}

/**
 * \brief Print the sources in a PCO instruction group.
 *
 * \param[in] state Print state.
 * \param[in] igrp PCO instruction group.
 * \param[in] upper Whether to print the upper sources.
 */
static void
pco_print_igrp_srcs(pco_print_state *state, pco_igrp *igrp, bool upper)
{
   unsigned offset = upper ? ROGUE_ALU_INPUT_GROUP_SIZE : 0;
   bool printed = false;
   for (unsigned u = 0; u < ROGUE_ALU_INPUT_GROUP_SIZE; ++u) {
      const pco_ref *src = &igrp->srcs.s[u + offset];
      if (pco_ref_is_null(*src))
         continue;

      if (printed)
         pco_printf(state, ", ");

      pco_printf(state, "s%u = ", u + offset);
      _pco_print_ref(state, *src);
      printed = true;
   }
}

/**
 * \brief Print the internal source selector in a PCO instruction group.
 *
 * \param[in] state Print state.
 * \param[in] igrp PCO instruction group.
 */
static void pco_print_igrp_iss(pco_print_state *state, pco_igrp *igrp)
{
   bool printed = false;
   for (unsigned u = 0; u < ROGUE_MAX_ALU_INTERNAL_SOURCES; ++u) {
      const pco_ref *iss = &igrp->iss.is[u];
      if (pco_ref_is_null(*iss))
         continue;

      if (printed)
         pco_printf(state, ", ");

      pco_printf(state, "is%u = ", u);
      _pco_print_ref(state, *iss);
      printed = true;
   }
}

/**
 * \brief Print the dests in a PCO instruction group.
 *
 * \param[in] state Print state.
 * \param[in] igrp PCO instruction group.
 */
static void pco_print_igrp_dests(pco_print_state *state, pco_igrp *igrp)
{
   bool printed = false;
   for (unsigned u = 0; u < ROGUE_MAX_ALU_OUTPUTS; ++u) {
      const pco_ref *dest = &igrp->dests.w[u];
      if (pco_ref_is_null(*dest))
         continue;

      if (printed)
         pco_printf(state, ", ");

      pco_printf(state, "w%u = ", u);
      _pco_print_ref(state, *dest);
      printed = true;
   }
}

/**
 * \brief Print PCO instruction group.
 *
 * \param[in] state Print state.
 * \param[in] igrp PCO instruction group.
 */
static void _pco_print_igrp(pco_print_state *state, pco_igrp *igrp)
{
   bool printed = false;

   pco_printfi(state,
               "%04u:%s%s { ",
               igrp->index,
               space_if_str(pco_cc_str(igrp->hdr.cc)),
               pco_cc_str(igrp->hdr.cc));

   if (state->verbose) {
      unsigned padding_size =
         igrp->enc.len.word_padding + igrp->enc.len.align_padding;
      unsigned unpadded_size = igrp->enc.len.total - padding_size;

      pco_printf(state, "/* @ 0x%08x [", igrp->enc.offset);
      pco_print_igrp_phases(state, igrp);
      pco_printf(state,
                 "] len: %u, pad: %u, total: %u, da: %u",
                 unpadded_size,
                 padding_size,
                 igrp->enc.len.total,
                 igrp->hdr.da);

      if (igrp->hdr.w0p)
         pco_printf(state, ", w0p");

      if (igrp->hdr.w1p)
         pco_printf(state, ", w1p");

      pco_printf(state, " */\n");
      ++state->indent;

      pco_printfi(state,
                  "type %s /* hdr bytes: %u */\n",
                  pco_alutype_str(igrp->hdr.alutype),
                  igrp->enc.len.hdr);
   }

   if (igrp->hdr.alutype != PCO_ALUTYPE_CONTROL && igrp->hdr.rpt > 1) {
      if (state->verbose)
         pco_printfi(state, "repeat %u\n", igrp->hdr.rpt);
      else
         pco_printf(state, "repeat %u ", igrp->hdr.rpt);

      printed = true;
   }

   if (igrp->enc.len.lower_srcs) {
      if (state->verbose)
         pco_printfi(state, "%s", "");

      if (!pco_igrp_srcs_unset(igrp, false)) {
         if (!state->verbose && printed)
            pco_printf(state, ", ");

         pco_print_igrp_srcs(state, igrp, false);

         if (state->verbose)
            pco_printf(state, " ");
      }

      if (state->verbose)
         pco_printf(state,
                    "/* lo src bytes: %u */\n",
                    igrp->enc.len.lower_srcs);

      printed = true;
   }

   if (igrp->enc.len.upper_srcs) {
      if (state->verbose)
         pco_printfi(state, "%s", "");

      if (!pco_igrp_srcs_unset(igrp, true)) {
         if (!state->verbose && printed)
            pco_printf(state, ", ");

         pco_print_igrp_srcs(state, igrp, true);

         if (state->verbose)
            pco_printf(state, " ");
      }

      if (state->verbose)
         pco_printf(state,
                    "/* up src bytes: %u */\n",
                    igrp->enc.len.upper_srcs);

      printed = true;
   }

   if (igrp->enc.len.iss) {
      if (state->verbose)
         pco_printfi(state, "%s", "");

      if (!pco_igrp_iss_unset(igrp)) {
         if (!state->verbose && printed)
            pco_printf(state, ", ");

         pco_print_igrp_iss(state, igrp);

         if (state->verbose)
            pco_printf(state, " ");
      }

      if (state->verbose)
         pco_printf(state, "/* iss bytes: %u */\n", igrp->enc.len.iss);

      printed = true;
   }

   for (enum pco_op_phase phase = 0; phase < _PCO_OP_PHASE_COUNT; ++phase) {
      if (!igrp->instrs[phase])
         continue;

      if (state->verbose)
         pco_printfi(state, "%s", "");
      else if (printed)
         pco_printf(state, " ");

      pco_print_phase(state, igrp->hdr.alutype, phase);
      pco_printf(state, ": ");
      _pco_print_instr(state, igrp->instrs[phase]);

      if (state->verbose) {
         pco_printf(state, " /* ");
         pco_print_phase(state, igrp->hdr.alutype, phase);
         pco_printf(state, " bytes: %u */\n", igrp->enc.len.instrs[phase]);
      }

      printed = true;
   }

   if (igrp->enc.len.dests) {
      if (state->verbose)
         pco_printfi(state, "%s", "");

      if (!pco_igrp_dests_unset(igrp)) {
         if (!state->verbose && printed)
            pco_printf(state, " ");

         pco_print_igrp_dests(state, igrp);

         if (state->verbose)
            pco_printf(state, " ");
      }

      if (state->verbose)
         pco_printf(state, "/* dest bytes: %u */\n", igrp->enc.len.dests);

      printed = true;
   }

   if (state->verbose)
      --state->indent;
   else
      pco_printf(state, " ");

   if (state->verbose)
      pco_printfi(state, "}");
   else
      pco_printf(state, "}");

   if (igrp->hdr.olchk)
      pco_printf(state, ".olchk");

   if (igrp->hdr.alutype != PCO_ALUTYPE_CONTROL) {
      if (igrp->hdr.atom)
         pco_printf(state, ".atom");

      if (igrp->hdr.end)
         pco_printf(state, ".end");
   }

   if (state->verbose && igrp->comment)
      pco_printf(state, " /* %s */", igrp->comment);

   pco_printf(state, "\n");
}

/**
 * \brief Print PCO block name.
 *
 * \param[in] state Print state.
 * \param[in] block PCO block.
 */
static void pco_print_block_name(pco_print_state *state, pco_block *block)
{
   pco_printf(state, "B%u", block->index);
}

/**
 * \brief Print PCO block.
 *
 * \param[in] state Print state.
 * \param[in] block PCO block.
 */
static void pco_print_block(pco_print_state *state, pco_block *block)
{
   pco_printfi(state, "block ");
   pco_print_block_name(state, block);
   pco_printfi(state, ":\n");
   ++state->indent;

   if (state->is_grouped) {
      pco_foreach_igrp_in_block (igrp, block) {
         _pco_print_igrp(state, igrp);
      }
   } else {
      pco_foreach_instr_in_block (instr, block) {
         _pco_print_instr(state, instr);
         pco_printf(state, "\n");
      }
   }

   --state->indent;
}

/**
 * \brief Print PCO if name.
 *
 * \param[in] state Print state.
 * \param[in] pif PCO if.
 */
static void pco_print_if_name(pco_print_state *state, pco_if *pif)
{
   pco_printf(state, "I%u", pif->index);
}

/**
 * \brief Print PCO if.
 *
 * \param[in] state Print state.
 * \param[in] pif PCO if.
 */
static void pco_print_if(pco_print_state *state, pco_if *pif)
{
   pco_printfi(state, "if ");
   pco_print_if_name(state, pif);
   pco_printfi(state, " (");
   _pco_print_ref(state, pif->cond);
   pco_printf(state, ") {\n");
   ++state->indent;

   pco_foreach_cf_node_in_if_then (cf_node, pif) {
      _pco_print_cf_node(state, cf_node);
   }

   --state->indent;
   if (list_is_empty(&pif->else_body)) {
      pco_printf(state, "}\n");
      return;
   }

   pco_printf(state, "} else {\n");
   ++state->indent;

   pco_foreach_cf_node_in_if_else (cf_node, pif) {
      _pco_print_cf_node(state, cf_node);
   }

   --state->indent;
   pco_printf(state, "}\n");
}

/**
 * \brief Print PCO loop name.
 *
 * \param[in] state Print state.
 * \param[in] loop PCO loop.
 */
static void pco_print_loop_name(pco_print_state *state, pco_loop *loop)
{
   pco_printf(state, "L%u", loop->index);
}

/**
 * \brief Print PCO loop.
 *
 * \param[in] state Print state.
 * \param[in] loop PCO loop.
 */
static void pco_print_loop(pco_print_state *state, pco_loop *loop)
{
   pco_printfi(state, "loop ");
   pco_print_loop_name(state, loop);
   pco_printfi(state, " {\n");
   ++state->indent;

   pco_foreach_cf_node_in_loop (cf_node, loop) {
      _pco_print_cf_node(state, cf_node);
   }

   --state->indent;
   pco_printf(state, "}\n");
}

/**
 * \brief Print PCO function signature.
 *
 * \param[in] state Print state.
 * \param[in] func PCO function.
 * \param[in] call Whether the signature is for a function call/reference.
 */
static void
pco_print_func_sig(pco_print_state *state, pco_func *func, bool call)
{
   if (!call) {
      switch (func->type) {
      case PCO_FUNC_TYPE_CALLABLE:
         break;

      case PCO_FUNC_TYPE_PREAMBLE:
         pco_printf(state, " PREAMBLE");
         break;

      case PCO_FUNC_TYPE_ENTRYPOINT:
         pco_printf(state, " ENTRY");
         break;

      case PCO_FUNC_TYPE_PHASE_CHANGE:
         pco_printf(state, " PHASE CHANGE");
         break;

      default:
         unreachable();
      }
   }

   if (func->name)
      pco_printf(state, " %s", func->name);
   else
      pco_printf(state, " _%u", func->index);

   pco_printf(state, "(");

   if (!call) {
      /* TODO: Function parameter support. */
      assert(func->num_params == 0 && func->params == NULL);
      if (!func->num_params)
         pco_printf(state, "void");
   }

   pco_printf(state, ")");
}

/**
 * \brief Print PCO function.
 *
 * \param[in] state Print state.
 * \param[in] func PCO function.
 */
static void pco_print_func(pco_print_state *state, pco_func *func)
{
   pco_printfi(state, "func");
   pco_print_func_sig(state, func, false);
   if (state->is_grouped)
      pco_printf(state, " /* temps: %u */", func->temps);
   pco_printf(state, "\n");

   pco_printfi(state, "{\n");

   pco_foreach_cf_node_in_func (cf_node, func) {
      _pco_print_cf_node(state, cf_node);
   }

   pco_printfi(state, "}\n");
}

/**
 * \brief Print PCO control flow node.
 *
 * \param[in] state Print state.
 * \param[in] cf_node PCO control flow node.
 */
static void _pco_print_cf_node(pco_print_state *state, pco_cf_node *cf_node)
{
   switch (cf_node->type) {
   case PCO_CF_NODE_TYPE_BLOCK:
      return pco_print_block(state, pco_cf_node_as_block(cf_node));

   case PCO_CF_NODE_TYPE_IF:
      return pco_print_if(state, pco_cf_node_as_if(cf_node));

   case PCO_CF_NODE_TYPE_LOOP:
      return pco_print_loop(state, pco_cf_node_as_loop(cf_node));

   case PCO_CF_NODE_TYPE_FUNC:
      return pco_print_func(state, pco_cf_node_as_func(cf_node));

   default:
      break;
   }

   unreachable();
}

/**
 * \brief Print PCO shader info.
 *
 * \param[in] state Print state.
 * \param[in] shader PCO shader.
 */
static void _pco_print_shader_info(pco_print_state *state, pco_shader *shader)
{
   if (shader->name)
      pco_printfi(state, "name: \"%s\"\n", shader->name);
   pco_printfi(state, "stage: %s\n", gl_shader_stage_name(shader->stage));
   pco_printfi(state, "internal: %s\n", true_false_str(shader->is_internal));
   /* TODO: more info/stats, e.g. temps/other regs used, etc.? */
}

/**
 * \brief Print PCO shader.
 *
 * \param[in] shader PCO shader.
 * \param[in] fp Print target file pointer.
 * \param[in] when When the printing is being performed.
 */
void pco_print_shader(pco_shader *shader, FILE *fp, const char *when)
{
   pco_print_state state = {
      .fp = fp,
      .shader = shader,
      .indent = 0,
      .is_grouped = shader->is_grouped,
      .verbose = PCO_DEBUG_PRINT(VERBOSE),
   };

   if (when)
      fprintf(fp, "shader ir %s:\n", when);
   else
      fputs("shader ir:\n", fp);

   _pco_print_shader_info(&state, shader);

   pco_foreach_func_in_shader (func, shader) {
      pco_print_func(&state, func);
   }
}

/**
 * \brief Print PCO shader binary.
 *
 * \param[in] shader PCO shader.
 * \param[in] fp Print target file pointer.
 * \param[in] when When the printing is being performed.
 */
void pco_print_binary(pco_shader *shader, FILE *fp, const char *when)
{
   pco_print_state state = {
      .fp = fp,
      .shader = shader,
      .indent = 0,
      .is_grouped = shader->is_grouped,
      .verbose = PCO_DEBUG_PRINT(VERBOSE),
   };

   if (when)
      fprintf(fp, "shader binary %s:\n", when);
   else
      fputs("shader binary:", fp);

   _pco_print_shader_info(&state, shader);

   return u_hexdump(fp,
                    pco_shader_binary_data(shader),
                    pco_shader_binary_size(shader),
                    false);
}

/**
 * \brief Print PCO reference (wrapper).
 *
 * \param[in] shader PCO shader.
 * \param[in] ref PCO reference.
 */
void pco_print_ref(pco_shader *shader, pco_ref ref)
{
   pco_print_state state = {
      .fp = stdout,
      .shader = shader,
      .indent = 0,
      .is_grouped = shader->is_grouped,
      .verbose = false,
   };
   return _pco_print_ref(&state, ref);
}

/**
 * \brief Print PCO instruction (wrapper).
 *
 * \param[in] shader PCO shader.
 * \param[in] instr PCO instruction.
 */
void pco_print_instr(pco_shader *shader, pco_instr *instr)
{
   pco_print_state state = {
      .fp = stdout,
      .shader = shader,
      .indent = 0,
      .is_grouped = shader->is_grouped,
      .verbose = false,
   };
   return _pco_print_instr(&state, instr);
}

/**
 * \brief Print PCO instruction group (wrapper).
 *
 * \param[in] shader PCO shader.
 * \param[in] igrp PCO instruction group.
 */
void pco_print_igrp(pco_shader *shader, pco_igrp *igrp)
{
   pco_print_state state = {
      .fp = stdout,
      .shader = shader,
      .indent = 0,
      .is_grouped = shader->is_grouped,
      .verbose = false,
   };
   return _pco_print_igrp(&state, igrp);
}

/**
 * \brief Print PCO control flow node name (wrapper).
 *
 * \param[in] shader PCO shader.
 * \param[in] cf_node PCO control flow node.
 */
void pco_print_cf_node_name(pco_shader *shader, pco_cf_node *cf_node)
{
   pco_print_state state = {
      .fp = stdout,
      .shader = shader,
      .indent = 0,
      .is_grouped = shader->is_grouped,
      .verbose = false,
   };

   switch (cf_node->type) {
   case PCO_CF_NODE_TYPE_BLOCK:
      pco_printf(&state, "block ");
      return pco_print_block_name(&state, pco_cf_node_as_block(cf_node));

   case PCO_CF_NODE_TYPE_IF:
      pco_printf(&state, "if ");
      return pco_print_if_name(&state, pco_cf_node_as_if(cf_node));

   case PCO_CF_NODE_TYPE_LOOP:
      pco_printf(&state, "loop ");
      return pco_print_loop_name(&state, pco_cf_node_as_loop(cf_node));

   case PCO_CF_NODE_TYPE_FUNC:
      pco_printf(&state, "func");
      return pco_print_func_sig(&state, pco_cf_node_as_func(cf_node), true);

   default:
      break;
   }

   unreachable();
}

/**
 * \brief Print PCO shader info (wrapper).
 *
 * \param[in] shader PCO shader.
 */
void pco_print_shader_info(pco_shader *shader)
{
   pco_print_state state = {
      .fp = stdout,
      .shader = shader,
      .indent = 0,
      .is_grouped = shader->is_grouped,
      .verbose = false,
   };
   return _pco_print_shader_info(&state, shader);
}
