/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_index.c
 *
 * \brief PCO indexing pass.
 */

#include "pco.h"
#include "pco_internal.h"
#include "util/hash_table.h"
#include "util/macros.h"
#include "util/ralloc.h"

#include <assert.h>
#include <stdbool.h>

/**
 * \brief Indexes all shader child structures.
 *
 * \param[in,out] shader PCO shader.
 * \param[in] skip_ssa Whether to skip SSA indexing.
 * \return True if the pass made progress.
 */
bool pco_index(pco_shader *shader, bool skip_ssa)
{
   /* TODO */
   if (shader->is_grouped)
      return false;

   shader->next_func = 0;
   pco_foreach_func_in_shader (func, shader) {
      unsigned *ssa_idx_map = NULL;
      if (!skip_ssa) {
         ssa_idx_map =
            rzalloc_array_size(NULL, sizeof(*ssa_idx_map), func->next_ssa);
      }

      func->index = shader->next_func++;
      func->next_instr = 0;
      func->next_block = 0;

      struct hash_table_u64 *vec_infos = NULL;
      if (!skip_ssa) {
         func->next_ssa = 0;
         vec_infos = _mesa_hash_table_u64_create(func);
      }

      pco_foreach_block_in_func (block, func) {
         block->index = func->next_block++;
         pco_foreach_instr_in_block (instr, block) {
            instr->index = func->next_instr++;
            if (!skip_ssa) {
               pco_foreach_instr_dest_ssa (pdest, instr) {
                  ssa_idx_map[pdest->val] = func->next_ssa++;
                  if (instr->op == PCO_OP_VEC) {
                     pco_vec_info *vec_info =
                        _mesa_hash_table_u64_search(func->vec_infos,
                                                    pdest->val);

                     ralloc_steal(vec_infos, vec_info);

                     _mesa_hash_table_u64_insert(vec_infos,
                                                 ssa_idx_map[pdest->val],
                                                 vec_info);
                  }
                  pdest->val = ssa_idx_map[pdest->val];
               }
            }
         }
      }

      if (!skip_ssa) {
         pco_foreach_instr_in_func (instr, func) {
            pco_foreach_instr_src_ssa (psrc, instr) {
               psrc->val = ssa_idx_map[psrc->val];
            }
         }
      }

      /* TODO: */
      /* pco_foreach_if_in_func */
      /* pco_foreach_loop_in_func */

      if (!skip_ssa) {
         _mesa_hash_table_u64_destroy(func->vec_infos);
         func->vec_infos = vec_infos;
         ralloc_free(ssa_idx_map);
      }
   }

   return true;
}
