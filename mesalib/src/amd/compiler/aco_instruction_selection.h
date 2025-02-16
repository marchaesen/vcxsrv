/*
 * Copyright © 2018 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ACO_INSTRUCTION_SELECTION_H
#define ACO_INSTRUCTION_SELECTION_H

#include "aco_ir.h"

#include "nir.h"

#include <array>
#include <unordered_map>
#include <vector>

namespace aco {

enum aco_color_output_type {
   ACO_TYPE_ANY32,
   ACO_TYPE_FLOAT16,
   ACO_TYPE_INT16,
   ACO_TYPE_UINT16,
};

struct shader_io_state {
   uint8_t mask[VARYING_SLOT_MAX];
   Temp temps[VARYING_SLOT_MAX * 4u];

   shader_io_state()
   {
      memset(mask, 0, sizeof(mask));
      std::fill_n(temps, VARYING_SLOT_MAX * 4u, Temp(0, RegClass::v1));
   }
};

struct exec_info {
   /* Set to false when in_divergent_cf==false */
   bool potentially_empty_discard = false;

   /* Set to false when leaving the loop, or if parent_if.is_divergent==false and
    * parent_loop.has_divergent_continue==false. */
   bool potentially_empty_break = false;

   /* Set to false when leaving the loop, or if parent_if.is_divergent==false. */
   bool potentially_empty_continue = false;

   void combine(struct exec_info& other)
   {
      potentially_empty_discard |= other.potentially_empty_discard;
      potentially_empty_break |= other.potentially_empty_break;
      potentially_empty_continue |= other.potentially_empty_continue;
   }

   bool empty() const noexcept
   {
      return potentially_empty_discard || potentially_empty_break || potentially_empty_continue;
   }
};

struct cf_context {
   struct {
      unsigned header_idx;
      Block* exit;
      bool has_divergent_continue = false;
      bool has_divergent_break = false;
   } parent_loop;
   struct {
      bool is_divergent = false;
   } parent_if;

   bool has_branch;
   bool has_divergent_branch = false;
   bool had_divergent_discard = false;
   bool in_divergent_cf = false;
   struct exec_info exec;
};

struct if_context {
   Temp cond;

   cf_context cf_info_old;

   unsigned BB_if_idx;
   unsigned invert_idx;
   Block BB_invert;
   Block BB_endif;
};

struct isel_context {
   const struct aco_compiler_options* options;
   const struct ac_shader_args* args;
   Program* program;
   nir_shader* shader;
   uint32_t constant_data_offset;
   Block* block;
   uint32_t first_temp_id;
   std::unordered_map<unsigned, std::array<Temp, NIR_MAX_VEC_COMPONENTS>> allocated_vec;
   std::vector<Temp> unended_linear_vgprs;
   Stage stage;

   cf_context cf_info;
   bool skipping_empty_exec = false;
   if_context empty_exec_skip;

   /* NIR range analysis. */
   struct hash_table* range_ht;
   nir_unsigned_upper_bound_config ub_config;

   Temp arg_temps[AC_MAX_ARGS];
   Operand workgroup_id[3];
   Temp ttmp8;

   /* tessellation information */
   bool any_tcs_inputs_via_lds = false;
   bool tcs_in_out_eq = false;

   /* Fragment color output information */
   uint16_t output_color_types;

   /* I/O information */
   shader_io_state inputs;
   shader_io_state outputs;

   /* WQM information */
   uint32_t wqm_block_idx;
   uint32_t wqm_instruction_idx;

   BITSET_DECLARE(output_args, AC_MAX_ARGS);
};

inline Temp
get_arg(isel_context* ctx, struct ac_arg arg)
{
   assert(arg.used);
   return ctx->arg_temps[arg.arg_index];
}

void init_context(isel_context* ctx, nir_shader* shader);
void cleanup_context(isel_context* ctx);

isel_context setup_isel_context(Program* program, unsigned shader_count,
                                struct nir_shader* const* shaders, ac_shader_config* config,
                                const struct aco_compiler_options* options,
                                const struct aco_shader_info* info,
                                const struct ac_shader_args* args,
                                SWStage sw_stage = SWStage::None);

} // namespace aco

#endif /* ACO_INSTRUCTION_SELECTION_H */
