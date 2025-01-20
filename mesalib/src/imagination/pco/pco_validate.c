/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_validate.c
 *
 * \brief PCO validation functions.
 */

#include "pco.h"
#include "pco_internal.h"
#include "util/bitset.h"
#include "util/macros.h"
#include "util/ralloc.h"

#include <inttypes.h>
#include <stdio.h>

enum ref_cursor {
   REF_CURSOR_NONE,
   REF_CURSOR_INSTR_DEST,
   REF_CURSOR_INSTR_SRC,
   REF_CURSOR_IGRP_SRC,
   REF_CURSOR_IGRP_ISS,
   REF_CURSOR_IGRP_DEST,
};

/** Validation state. */
struct val_state {
   const char *when; /** Description of the validation being done. */
   pco_shader *shader; /** The shader being validated. */
   pco_func *func; /** Current function being validated. */
   pco_cf_node *cf_node; /** Current cf node being validated. */
   pco_igrp *igrp; /** Current instruction group being validated. */
   enum pco_op_phase phase; /** Phase of the instruction being validated. */
   pco_instr *instr; /** Current instruction being validated. */
   pco_ref *ref; /** Current reference being validated. */
   enum ref_cursor ref_cursor; /** Current reference cursor. */
};

/**
 * \brief Asserts a condition, printing an error and aborting on failure.
 *
 * \param[in] state Validation state.
 * \param[in] cond Assertion condition.
 * \param[in] cond_str Assertion condition string.
 * \param[in] fmt Format string.
 */
static void pco_assert(struct val_state *state,
                       bool cond,
                       const char *cond_str,
                       const char *fmt,
                       ...)
{
   if (cond)
      return;

   printf("PCO validation failed with assertion \"%s\" - ", cond_str);

   va_list args;
   va_start(args, fmt);
   vprintf(fmt, args);
   va_end(args);

   printf(" - while validating");

   if (state->ref_cursor != REF_CURSOR_NONE) {
      switch (state->ref_cursor) {
      case REF_CURSOR_INSTR_DEST:
         printf(" instr dest #%" PRIuPTR, state->ref - state->instr->dest);
         break;

      case REF_CURSOR_INSTR_SRC:
         printf(" instr src #%" PRIuPTR, state->ref - state->instr->src);
         break;

      default:
         unreachable();
      }

      printf(" (");
      pco_print_ref(state->shader, *state->ref);
      printf(")");
   }

   if (state->cf_node) {
      printf(" ");
      pco_print_cf_node_name(state->shader, state->cf_node);
   }

   if (state->igrp) {
      printf(" igrp ");
      pco_print_igrp(state->shader, state->igrp);
   }

   if (state->instr) {
      printf(" instr ");
      pco_print_instr(state->shader, state->instr);
   }

   if (state->func) {
      printf(" ");
      pco_print_cf_node_name(state->shader, &state->func->cf_node);
   }

   printf(".\n");

   pco_print_shader_info(state->shader);

   abort();
}

#define PCO_ASSERT(state, cond, fmt, ...) \
   pco_assert(state, cond, #cond, fmt, ##__VA_ARGS__)

/**
 * \brief Validates SSA assignments and uses.
 *
 * \param[in,out] state Validation state.
 */
static void pco_validate_ssa(struct val_state *state)
{
   BITSET_WORD *ssa_writes;
   pco_foreach_func_in_shader (func, state->shader) {
      state->func = func;

      ssa_writes = rzalloc_array_size(NULL,
                                      sizeof(*ssa_writes),
                                      BITSET_WORDS(func->next_ssa));

      /* Ensure sources have been defined before they're used. */
      state->ref_cursor = REF_CURSOR_INSTR_SRC;
      pco_foreach_instr_in_func (instr, func) {
         state->cf_node = &instr->parent_block->cf_node;
         state->instr = instr;
         pco_foreach_instr_src_ssa (psrc, instr) {
            state->ref = psrc;
            PCO_ASSERT(state,
                       BITSET_TEST(ssa_writes, psrc->val),
                       "SSA source used before being defined");
         }

         /* Ensure destinations are only defined once. */
         state->ref_cursor = REF_CURSOR_INSTR_DEST;
         pco_foreach_instr_dest_ssa (pdest, instr) {
            state->ref = pdest;
            PCO_ASSERT(state,
                       !BITSET_TEST(ssa_writes, pdest->val),
                       "SSA destination defined to more than once");
            BITSET_SET(ssa_writes, pdest->val);
         }
      }

      ralloc_free(ssa_writes);

      state->func = NULL;
      state->ref = NULL;
   }
}

/**
 * \brief Validates a PCO shader.
 *
 * \param[in] shader PCO shader.
 * \param[in] when When the validation check is being run.
 */
void pco_validate_shader(UNUSED pco_shader *shader, UNUSED const char *when)
{
#ifndef NDEBUG
   if (PCO_DEBUG(VAL_SKIP))
      return;

   struct val_state state = {
      .when = when,
      .shader = shader,
      .phase = -1,
   };

   if (!shader->is_grouped)
      pco_validate_ssa(&state);

   puts("finishme: pco_validate_shader");
#endif /* NDEBUG */
}
