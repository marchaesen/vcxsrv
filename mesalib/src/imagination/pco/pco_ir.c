/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_ir.c
 *
 * \brief PCO IR-specific functions.
 */

#include "pco.h"
#include "pco_internal.h"
#include "util/u_debug.h"

#include <stdbool.h>
#include <stdio.h>

static inline bool pco_should_skip_pass(const char *pass)
{
   return comma_separated_list_contains(pco_skip_passes, pass);
}

#define PCO_PASS(progress, shader, pass, ...)                 \
   do {                                                       \
      if (pco_should_skip_pass(#pass)) {                      \
         fprintf(stdout, "Skipping pass '%s'\n", #pass);      \
         break;                                               \
      }                                                       \
                                                              \
      if (pass(shader, ##__VA_ARGS__)) {                      \
         UNUSED bool _;                                       \
         progress = true;                                     \
                                                              \
         if (PCO_DEBUG(REINDEX))                              \
            pco_index(shader, false);                         \
                                                              \
         pco_validate_shader(shader, "after " #pass);         \
                                                              \
         if (pco_should_print_shader_pass(shader))            \
            pco_print_shader(shader, stdout, "after " #pass); \
      }                                                       \
   } while (0)

/**
 * \brief Runs passes on a PCO shader.
 *
 * \param[in] ctx PCO compiler context.
 * \param[in,out] shader PCO shader.
 */
void pco_process_ir(pco_ctx *ctx, pco_shader *shader)
{
   pco_validate_shader(shader, "before passes");

   PCO_PASS(_, shader, pco_const_imms);
   PCO_PASS(_, shader, pco_opt);
   PCO_PASS(_, shader, pco_dce);
   /* TODO: schedule after RA instead as e.g. vecs may no longer be the first
    * time a drc result is used.
    */
   PCO_PASS(_, shader, pco_schedule);
   PCO_PASS(_, shader, pco_ra);
   PCO_PASS(_, shader, pco_end);
   PCO_PASS(_, shader, pco_group_instrs);

   pco_validate_shader(shader, "after passes");

   if (pco_should_print_shader(shader))
      pco_print_shader(shader, stdout, "after passes");
}
