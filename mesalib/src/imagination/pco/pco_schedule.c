/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_schedule.c
 *
 * \brief PCO instruction scheduling pass.
 */

#include "pco.h"
#include "pco_builder.h"
#include "pco_internal.h"
#include "util/macros.h"

#include <stdbool.h>

/**
 * \brief Schedules instructions and inserts waits.
 *
 * \param[in,out] shader PCO shader.
 * \return True if the pass made progress.
 */
bool pco_schedule(pco_shader *shader)
{
   bool progress = false;
   pco_builder b;

   pco_foreach_func_in_shader (func, shader) {
      pco_foreach_instr_in_func_safe (instr, func) {
         pco_foreach_instr_src (psrc, instr) {
            if (!pco_ref_is_drc(*psrc))
               continue;

            b = pco_builder_create(func, pco_cursor_after_instr(instr));
            pco_wdf(&b, *psrc);

            progress = true;
            break;
         }
      }
   }

   return progress;
}
