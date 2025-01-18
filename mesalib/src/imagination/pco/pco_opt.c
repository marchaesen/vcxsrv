/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_opt.c
 *
 * \brief PCO optimization passes.
 */

#include "pco.h"
#include "pco_builder.h"
#include "util/bitscan.h"
#include "util/bitset.h"
#include "util/macros.h"
#include "util/ralloc.h"
#include "util/u_dynarray.h"

#include <assert.h>
#include <stdbool.h>

/** Source use. */
struct pco_use {
   pco_instr *instr;
   pco_ref *psrc;
};

/** Shared optimization context. */
struct pco_opt_ctx {
   void *mem_ctx;
   struct util_dynarray mods;
};

/**
 * \brief Prepares modifiers and their users for propagation.
 *
 * Instructions with commutative sources may use a modifier in the source which
 * can't have said modifier applied to it, e.g. fadd can have {abs,neg,flr} set
 * in src0, but src1 only supports abs.
 *
 * \param[in,out] shader PCO shader.
 * \param[in,out] ctx Shared optimization context.
 * \return True if any instructions were modified.
 */
static inline bool prep_mods(pco_shader *shader, struct pco_opt_ctx *ctx)
{
   bool progress = false;

   util_dynarray_init(&ctx->mods, ctx->mem_ctx);

   /* TODO: support for more modifiers/ops. */
   /* TODO: support cases where > 1 modifier can be applied (e.g. .abs.neg),
    * and where modifiers might need to be applied on more than one source.
    */
   pco_foreach_func_in_shader (func, shader) {
      pco_foreach_instr_in_func_safe (mod, func) {
         if (mod->op != PCO_OP_NEG && mod->op != PCO_OP_ABS &&
             mod->op != PCO_OP_FLR)
            continue;

         pco_foreach_instr_in_func_from (instr, mod) {
            if (instr->op != PCO_OP_FADD && instr->op != PCO_OP_FMUL)
               continue;

            pco_ref *match_src = NULL;
            pco_ref *other_src = NULL;
            pco_foreach_instr_src (psrc, instr) {
               if (!pco_ref_is_ssa(*psrc) || psrc->val != mod->dest[0].val)
                  other_src = psrc;
               else
                  match_src = psrc;
            }

            /* Instruction doesn't use the mod. */
            if (!match_src)
               continue;

            /* Mod used in *both* sources; swapping would do nothing. */
            if (!other_src)
               continue;

            unsigned match_src_index = match_src - instr->src;
            unsigned other_src_index = other_src - instr->src;

            bool match_has_mod = false;
            bool other_has_mod = false;

            switch (mod->op) {
            case PCO_OP_NEG:
               match_has_mod = pco_instr_src_has_neg(instr, match_src_index);
               other_has_mod = pco_instr_src_has_neg(instr, other_src_index);
               break;

            case PCO_OP_ABS:
               match_has_mod = pco_instr_src_has_abs(instr, match_src_index);
               other_has_mod = pco_instr_src_has_abs(instr, other_src_index);
               break;

            case PCO_OP_FLR:
               match_has_mod = pco_instr_src_has_flr(instr, match_src_index);
               other_has_mod = pco_instr_src_has_flr(instr, other_src_index);
               break;

            default:
               unreachable();
            }

            /* Source can already have the mod set. */
            if (match_has_mod)
               continue;

            /* Other source can't have the mod set either. */
            if (!other_has_mod)
               continue;

            /* Swap the sources. */
            pco_ref tmp = *match_src;
            *match_src = *other_src;
            *other_src = tmp;

            progress = true;
         }

         /* Rewrite the mod op to a mov. */
         pco_ref src = mod->src[0];
         switch (mod->op) {
         case PCO_OP_NEG:
            src = pco_ref_neg(src);
            break;

         case PCO_OP_ABS:
            src = pco_ref_abs(src);
            break;

         case PCO_OP_FLR:
            src = pco_ref_flr(src);
            break;

         default:
            unreachable();
         }

         pco_builder b = pco_builder_create(func, pco_cursor_before_instr(mod));
         pco_instr *mov = pco_mov(&b, mod->dest[0], src);
         util_dynarray_append(&ctx->mods, pco_instr *, mov);
         pco_instr_delete(mod);

         progress = true;
      }
   }

   return progress;
}

/**
 * \brief Lowers modifiers to hardware instructions.
 *
 * \param[in,out] shader PCO shader.
 * \param[in,out] ctx Shared optimization context.
 * \return True if any instructions were modified.
 */
static inline bool lower_mods(pco_shader *shader, struct pco_opt_ctx *ctx)
{
   bool progress = false;

   util_dynarray_foreach (&ctx->mods, pco_instr *, pmod) {
      pco_instr *mod = *pmod;

      pco_builder b =
         pco_builder_create(mod->parent_func, pco_cursor_before_instr(mod));

      if (mod->src[0].flr)
         pco_fadd(&b, mod->dest[0], mod->src[0], pco_zero);
      else
         pco_mbyp0(&b, mod->dest[0], mod->src[0]);

      pco_instr_delete(mod);

      progress = true;
   }

   return progress;
}

/**
 * \brief Checks if an instruction can be back-propagated.
 *
 * \param[in] to Instruction being back-propagated to.
 * \param[in] from Instruction being back-propagated from.
 * \return True if from can be back-propagated.
 */
static inline bool can_back_prop_instr(const pco_instr *to,
                                       const pco_instr *from)
{
   const struct pco_op_info *info = &pco_op_info[from->op];

   /* Ensure any op mods set in from can also be set in to. */
   u_foreach_bit64 (mod, info->mods) {
      if (pco_instr_has_mod(from, mod) && pco_instr_mod_is_set(from, mod) &&
          !pco_instr_has_mod(to, mod))
         return false;
   }

   return true;
}

/**
 * \brief Transfers any op mods that have been set.
 *
 * \param[in] to Instruction receiving the op mods.
 * \param[in] from Instruction providing the op mods.
 */
static inline void xfer_set_op_mods(pco_instr *to, const pco_instr *from)
{
   const struct pco_op_info *info = &pco_op_info[from->op];

   /* Transfer set op mods. */
   u_foreach_bit64 (mod, info->mods) {
      if (pco_instr_has_mod(from, mod) && pco_instr_mod_is_set(from, mod)) {
         assert(pco_instr_has_mod(to, mod));
         pco_instr_set_mod(to, mod, pco_instr_get_mod(from, mod));
      }
   }
}

/**
 * \brief Tries to back-propagate an instruction.
 *
 * \param[in] uses Global list of uses.
 * \param[in] instr Instruction to try and back-propagate.
 * \return True if back-propagation was successful.
 */
static inline bool try_back_prop_instr(struct pco_use *uses, pco_instr *instr)
{
   pco_ref *pdest_to = &instr->dest[0];
   if (instr->num_dests != 1 || !pco_ref_is_ssa(*pdest_to))
      return false;

   struct pco_use *use = &uses[pdest_to->val];
   if (!use->instr)
      return false;

   /* TODO: allow propagating instructions which can have their dest/op
    * modifiers set to perform the same operations as use source modifiers.
    *
    * Make sure to check in can_back_prop_instr when implementing this.
    * We're fine for now since mov has no settable dest mods.
    */
   if (use->instr->op != PCO_OP_MOV || pco_ref_has_mods_set(*use->psrc))
      return false;

   if (!can_back_prop_instr(instr, use->instr))
      return false;

   pco_ref *pdest_from = &use->instr->dest[0];

   assert(pco_ref_get_bits(*pdest_from) == pco_ref_get_bits(*pdest_to));
   assert(pco_ref_get_chans(*pdest_from) == pco_ref_get_chans(*pdest_to));
   assert(!pco_ref_has_mods_set(*pdest_from) &&
          !pco_ref_has_mods_set(*pdest_to));

   /* Propagate the destination and the set op mods. */
   /* TODO: types? */
   *pdest_to = *pdest_from;
   xfer_set_op_mods(instr, use->instr);
   pco_instr_delete(use->instr);

   return true;
}

/**
 * \brief Instruction back-propagation pass.
 *
 * \param[in,out] shader PCO shader.
 * \return True if any back-propagations were performed.
 */
static inline bool back_prop(pco_shader *shader)
{
   bool progress = false;
   struct pco_use *uses;
   BITSET_WORD *multi_uses;

   pco_foreach_func_in_shader_rev (func, shader) {
      uses = rzalloc_array_size(NULL, sizeof(*uses), func->next_ssa);
      multi_uses = rzalloc_array_size(uses,
                                      sizeof(*multi_uses),
                                      BITSET_WORDS(func->next_ssa));

      pco_foreach_instr_in_func_safe_rev (instr, func) {
         pco_foreach_instr_src_ssa (psrc, instr) {
            if (BITSET_TEST(multi_uses, psrc->val) || uses[psrc->val].instr) {
               BITSET_SET(multi_uses, psrc->val);
               uses[psrc->val].instr = NULL;
               continue;
            }

            uses[psrc->val] = (struct pco_use){
               .instr = instr,
               .psrc = psrc,
            };
         }

         progress |= try_back_prop_instr(uses, instr);
      }

      ralloc_free(uses);
   }

   return progress;
}

/**
 * \brief Checks if a source can be forward-propagated.
 *
 * \param[in] to_instr Instruction being forward-propagated to.
 * \param[in] to Source being forward-propagated to.
 * \param[in] from Source being forward-propagated from.
 * \return True if from can be forward-propagated.
 */
static inline bool can_fwd_prop_src(const pco_instr *to_instr,
                                    const pco_ref *to,
                                    const pco_ref *from)
{
   /* Check sizes. */
   if (pco_ref_get_bits(*from) != pco_ref_get_bits(*to))
      return false;

   if (pco_ref_get_chans(*from) != pco_ref_get_chans(*to))
      return false;

   /* See if the modifiers can be propagated. */
   unsigned to_src_index = to - to_instr->src;
   if (pco_ref_has_mods_set(*from)) {
      if (from->oneminus && !pco_instr_src_has_oneminus(to_instr, to_src_index))
         return false;
      if (from->clamp && !pco_instr_src_has_clamp(to_instr, to_src_index))
         return false;
      if (from->flr && !pco_instr_src_has_flr(to_instr, to_src_index))
         return false;
      if (from->abs && !pco_instr_src_has_abs(to_instr, to_src_index))
         return false;
      if (from->neg && !pco_instr_src_has_neg(to_instr, to_src_index))
         return false;
      if (from->elem && !pco_instr_src_has_elem(to_instr, to_src_index))
         return false;
   }

   /* TODO: Also need to consider whether the source can be represented in the
    * propagated instruction.
    *  Or, a legalize pass to insert movs; probably better since
    * feature/arch-agnostic.
    */

   return true;
}

/**
 * \brief Tries to forward-propagate an instruction.
 *
 * \param[in] writes Global list of writes.
 * \param[in] instr Instruction to try and forward-propagate.
 * \return True if forward-propagation was successful.
 */
static inline bool try_fwd_prop_instr(pco_instr **writes, pco_instr *instr)
{
   bool progress = false;

   pco_foreach_instr_src_ssa (psrc, instr) {
      pco_instr *parent_instr = writes[psrc->val];

      if (!parent_instr || parent_instr->op != PCO_OP_MOV)
         continue;

      if (!can_fwd_prop_src(instr, psrc, &parent_instr->src[0]))
         continue;

      /* Propagate the source. */
      pco_ref repl = parent_instr->src[0];
      if (psrc->flr)
         repl = pco_ref_flr(repl);
      else if (psrc->abs)
         repl = pco_ref_abs(repl);

      repl.neg ^= psrc->neg;

      /* TODO: types? */
      *psrc = repl;

      progress = true;
   }

   return progress;
}

/**
 * \brief Instruction forward-propagation pass.
 *
 * \param[in,out] shader PCO shader.
 * \return True if any forward-propagations were performed.
 */
static inline bool fwd_prop(pco_shader *shader)
{
   bool progress = false;
   pco_instr **writes;

   pco_foreach_func_in_shader (func, shader) {
      writes = rzalloc_array_size(NULL, sizeof(*writes), func->next_ssa);

      pco_foreach_instr_in_func (instr, func) {
         pco_foreach_instr_dest_ssa (pdest, instr) {
            writes[pdest->val] = instr;
         }

         progress |= try_fwd_prop_instr(writes, instr);
      }

      ralloc_free(writes);
   }

   return progress;
}

/**
 * \brief Tries to propagate a comp instruction referencing hw registers.
 *
 * \param[in] src Source to match for replacement.
 * \param[in] repl Replacement hw register reference.
 * \param[in] from Instruction to try and propagate from.
 * \return True if propagation was successful.
 */
static inline bool try_prop_hw_comp(pco_ref src, pco_ref repl, pco_instr *from)
{
   bool progress = false;

   pco_foreach_instr_in_func_from (instr, from) {
      pco_foreach_instr_src_ssa (psrc, instr) {
         if (psrc->val != src.val)
            continue;

         /* Propagate the source. */
         pco_ref _repl = repl;
         if (psrc->flr)
            _repl = pco_ref_flr(_repl);
         else if (psrc->abs)
            _repl = pco_ref_abs(_repl);

         _repl.neg ^= psrc->neg;

         /* TODO: types? */
         *psrc = _repl;

         progress = true;
      }
   }

   return progress;
}

/**
 * \brief Pass to propagate comp instructions referencing hw registers.
 *
 * \param[in,out] shader PCO shader.
 * \return True if any hw reg propagations were performed.
 */
static inline bool prop_hw_comps(pco_shader *shader)
{
   bool progress = false;
   pco_foreach_func_in_shader (func, shader) {
      pco_foreach_instr_in_func_safe (instr, func) {
         if (instr->op != PCO_OP_COMP)
            continue;

         pco_ref vec_src = instr->src[0];
         if (pco_ref_is_ssa(vec_src))
            continue;

         pco_ref dest = instr->dest[0];
         assert(pco_ref_is_ssa(dest));

         unsigned offset = pco_ref_get_imm(instr->src[1]);

         /* Construct a replacement scalar reference. */
         pco_ref repl = vec_src;
         repl = pco_ref_chans(repl, 1);
         repl = pco_ref_offset(repl, offset);

         progress |= try_prop_hw_comp(dest, repl, instr);

         pco_instr_delete(instr);
      }
   }

   return progress;
}

/**
 * \brief Performs shader optimizations.
 *
 * \param[in,out] shader PCO shader.
 * \return True if the pass made progress.
 */
bool pco_opt(pco_shader *shader)
{
   bool progress = false;
   struct pco_opt_ctx ctx = { .mem_ctx = ralloc_context(NULL) };

   progress |= prep_mods(shader, &ctx);
   progress |= back_prop(shader);
   progress |= fwd_prop(shader);
   /* TODO: Track whether there are any comp instructions referencing hw
    * registers resulting from the previous passes, and only run prop_hw_comps
    * if this is the case.
    */
   progress |= prop_hw_comps(shader);

   progress |= lower_mods(shader, &ctx);

   ralloc_free(ctx.mem_ctx);
   return progress;
}

/**
 * \brief Checks whether an instruction has side-effects.
 *
 * \param[in] instr Instruction to check.
 * \return True if the instruction has side-effects.
 */
static inline bool instr_has_side_effects(pco_instr *instr)
{
   /* Atomic instructions. */
   if (pco_instr_has_atom(instr) && pco_instr_get_atom(instr))
      return true;

   /* TODO:
    * - gradient
    * - conditional
    * - sample writes (+ set the destination pointer to point to the write data)
    * - others
    */
   return false;
}

/**
 * \brief Performs DCE.
 *
 * \param[in,out] shader PCO shader.
 * \return True if the pass made progress.
 */
bool pco_dce(pco_shader *shader)
{
   bool progress = false;
   BITSET_WORD *ssa_used;

   pco_foreach_func_in_shader (func, shader) {
      ssa_used = rzalloc_array_size(NULL,
                                    sizeof(*ssa_used),
                                    BITSET_WORDS(func->next_ssa));

      /* Collect used SSA sources. */
      pco_foreach_instr_in_func (instr, func) {
         pco_foreach_instr_src_ssa (psrc, instr) {
            BITSET_SET(ssa_used, psrc->val);
         }
      }

      /* Remove instructions with unused SSA destinations (if they also have no
       * side-effects).
       */
      pco_foreach_instr_in_func_safe (instr, func) {
         bool has_ssa_dests = false;
         bool dests_used = false;

         pco_foreach_instr_dest_ssa (pdest, instr) {
            has_ssa_dests = true;
            dests_used |= BITSET_TEST(ssa_used, pdest->val);
         }

         if (has_ssa_dests && !dests_used && !instr_has_side_effects(instr)) {
            pco_instr_delete(instr);
            progress = true;
         }
      }

      ralloc_free(ssa_used);
   }

   return progress;
}
