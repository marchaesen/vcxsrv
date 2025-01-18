/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_end.c
 *
 * \brief PCO shader ending pass.
 */

#include "pco.h"
#include "pco_builder.h"
#include "pco_internal.h"
#include "util/macros.h"

#include <stdbool.h>

/**
 * \brief Processes end of shader instruction(s).
 *
 * \param[in,out] shader PCO shader.
 * \return True if the pass made progress.
 */
bool pco_end(pco_shader *shader)
{
   /* TODO: Support for multiple end points. */
   pco_func *entry = pco_entrypoint(shader);
   pco_block *last_block = pco_func_last_block(entry);
   pco_instr *last_instr = pco_last_instr(last_block);

   pco_builder b =
      pco_builder_create(entry, pco_cursor_after_block(last_block));

   if (shader->stage == MESA_SHADER_VERTEX) {
      if (last_instr->op == PCO_OP_UVSW_WRITE &&
          pco_instr_default_exec(last_instr) &&
          pco_instr_get_rpt(last_instr) == 1) {
         pco_instr *new_last_instr =
            pco_uvsw_write_emit_endtask(&b,
                                        last_instr->src[0],
                                        last_instr->src[1]);
         pco_instr_delete(last_instr);
         last_instr = new_last_instr;
      } else {
         last_instr = pco_uvsw_emit_endtask(&b);
      }
   }

   if (last_instr && pco_instr_has_end(last_instr)) {
      pco_instr_set_end(last_instr, true);
      return true;
   }

   pco_nop_end(&b);

   return true;
}
