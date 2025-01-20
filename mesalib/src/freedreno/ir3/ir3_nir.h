/*
 * Copyright © 2015 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef IR3_NIR_H_
#define IR3_NIR_H_

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/shader_enums.h"

#include "ir3_shader.h"

BEGINC;

bool ir3_nir_apply_trig_workarounds(nir_shader *shader);
bool ir3_nir_lower_imul(nir_shader *shader);
bool ir3_nir_lower_io_offsets(nir_shader *shader);
bool ir3_nir_lower_load_barycentric_at_sample(nir_shader *shader);
bool ir3_nir_lower_load_barycentric_at_offset(nir_shader *shader);
bool ir3_nir_lower_push_consts_to_preamble(nir_shader *nir,
                                           struct ir3_shader_variant *v);
bool ir3_nir_lower_driver_params_to_ubo(nir_shader *nir,
                                        struct ir3_shader_variant *v);
bool ir3_nir_move_varying_inputs(nir_shader *shader);
int ir3_nir_coord_offset(nir_def *ssa);
bool ir3_nir_lower_tex_prefetch(nir_shader *shader);
bool ir3_nir_lower_layer_id(nir_shader *shader);
bool ir3_nir_lower_frag_shading_rate(nir_shader *shader);
bool ir3_nir_lower_primitive_shading_rate(nir_shader *shader);

void ir3_nir_lower_to_explicit_output(nir_shader *shader,
                                      struct ir3_shader_variant *v,
                                      unsigned topology);
void ir3_nir_lower_to_explicit_input(nir_shader *shader,
                                     struct ir3_shader_variant *v);
void ir3_nir_lower_tess_ctrl(nir_shader *shader, struct ir3_shader_variant *v,
                             unsigned topology);
void ir3_nir_lower_tess_eval(nir_shader *shader, struct ir3_shader_variant *v,
                             unsigned topology);
void ir3_nir_lower_gs(nir_shader *shader);

bool ir3_supports_vectorized_nir_op(nir_op op);
uint8_t ir3_nir_vectorize_filter(const nir_instr *instr, const void *data);

/*
 * 64b related lowering:
 */
bool ir3_nir_lower_64b_intrinsics(nir_shader *shader);
bool ir3_nir_lower_64b_undef(nir_shader *shader);
bool ir3_nir_lower_64b_global(nir_shader *shader);
bool ir3_nir_lower_64b_regs(nir_shader *shader);

nir_mem_access_size_align ir3_mem_access_size_align(
   nir_intrinsic_op intrin, uint8_t bytes, uint8_t bit_size, uint32_t align,
   uint32_t align_offset, bool offset_is_const, enum gl_access_qualifier access,
   const void *cb_data);

bool ir3_nir_opt_branch_and_or_not(nir_shader *nir);
bool ir3_nir_opt_triops_bitwise(nir_shader *nir);
bool ir3_optimize_loop(struct ir3_compiler *compiler,
                       const struct ir3_shader_nir_options *options,
                       nir_shader *s);
void ir3_nir_lower_io_to_temporaries(nir_shader *s);
void ir3_finalize_nir(struct ir3_compiler *compiler,
                      const struct ir3_shader_nir_options *options,
                      nir_shader *s);
void ir3_nir_post_finalize(struct ir3_shader *shader);
void ir3_nir_lower_variant(struct ir3_shader_variant *so,
                           const struct ir3_shader_nir_options *options,
                           nir_shader *s);

void ir3_setup_const_state(nir_shader *nir, struct ir3_shader_variant *v,
                           struct ir3_const_state *const_state);
uint32_t ir3_const_state_get_free_space(const struct ir3_shader_variant *v,
                                        const struct ir3_const_state *const_state,
                                        uint32_t align_vec4);
void ir3_const_alloc(struct ir3_const_allocations *const_alloc,
                     enum ir3_const_alloc_type type, uint32_t size_vec4,
                     uint32_t align_vec4);
void ir3_const_reserve_space(struct ir3_const_allocations *const_alloc,
                             enum ir3_const_alloc_type type,
                             uint32_t size_vec4, uint32_t align_vec4);
void ir3_const_free_reserved_space(struct ir3_const_allocations *const_alloc,
                                   enum ir3_const_alloc_type type);
void ir3_const_alloc_all_reserved_space(struct ir3_const_allocations *const_alloc);

uint32_t ir3_nir_scan_driver_consts(struct ir3_compiler *compiler,
                                    nir_shader *shader,
                                    struct ir3_const_image_dims *image_dims);
void ir3_alloc_driver_params(struct ir3_const_allocations *const_alloc,
                             uint32_t *num_driver_params,
                             struct ir3_compiler *compiler,
                             enum pipe_shader_type shader_type);
bool ir3_nir_lower_load_constant(nir_shader *nir, struct ir3_shader_variant *v);
void ir3_nir_analyze_ubo_ranges(nir_shader *nir, struct ir3_shader_variant *v);
bool ir3_nir_lower_ubo_loads(nir_shader *nir, struct ir3_shader_variant *v);
bool ir3_nir_lower_const_global_loads(nir_shader *nir, struct ir3_shader_variant *v);
bool ir3_nir_fixup_load_const_ir3(nir_shader *nir);
bool ir3_nir_opt_preamble(nir_shader *nir, struct ir3_shader_variant *v);
bool ir3_nir_opt_prefetch_descriptors(nir_shader *nir, struct ir3_shader_variant *v);
bool ir3_nir_lower_preamble(nir_shader *nir, struct ir3_shader_variant *v);

nir_def *ir3_nir_try_propagate_bit_shift(nir_builder *b,
                                             nir_def *offset,
                                             int32_t shift);

bool ir3_nir_lower_subgroups_filter(const nir_instr *instr, const void *data);
bool ir3_nir_lower_shuffle(nir_shader *nir, struct ir3_shader *shader);
bool ir3_nir_opt_subgroups(nir_shader *nir, struct ir3_shader_variant *v);

nir_def *ir3_get_shared_driver_ubo(nir_builder *b,
                                   const struct ir3_driver_ubo *ubo);
nir_def *ir3_get_driver_ubo(nir_builder *b, struct ir3_driver_ubo *ubo);
nir_def *ir3_get_driver_consts_ubo(nir_builder *b,
                                   struct ir3_shader_variant *v);
void ir3_update_driver_ubo(nir_shader *nir, const struct ir3_driver_ubo *ubo, const char *name);
nir_def *ir3_load_shared_driver_ubo(nir_builder *b, unsigned components,
                                    const struct ir3_driver_ubo *ubo,
                                    unsigned offset);
nir_def *ir3_load_driver_ubo(nir_builder *b, unsigned components,
                             struct ir3_driver_ubo *ubo,
                             unsigned offset);
nir_def *ir3_load_driver_ubo_indirect(nir_builder *b, unsigned components,
                                      struct ir3_driver_ubo *ubo,
                                      unsigned base, nir_def *offset,
                                      unsigned range);

bool ir3_def_is_rematerializable_for_preamble(nir_def *def,
                                              nir_def **preamble_defs);

nir_def *ir3_rematerialize_def_for_preamble(nir_builder *b, nir_def *def,
                                            struct set *instr_set,
                                            nir_def **preamble_defs);

struct driver_param_info {
   uint32_t offset;
};

bool ir3_get_driver_param_info(const nir_shader *shader,
                               nir_intrinsic_instr *intr,
                               struct driver_param_info *param_info);

static inline nir_intrinsic_instr *
ir3_bindless_resource(nir_src src)
{
   if (src.ssa->parent_instr->type != nir_instr_type_intrinsic)
      return NULL;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(src.ssa->parent_instr);
   if (intrin->intrinsic != nir_intrinsic_bindless_resource_ir3)
      return NULL;

   return intrin;
}

static inline bool
is_intrinsic_store(nir_intrinsic_op op)
{
   switch (op) {
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_view_output:
   case nir_intrinsic_store_scratch:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_store_global:
   case nir_intrinsic_store_global_ir3:
      return true;
   default:
      return false;
   }
}

static inline bool
is_intrinsic_load(nir_intrinsic_op op)
{
   switch (op) {
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_scratch:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_ir3:
   case nir_intrinsic_load_const_ir3:
      return true;
   default:
      return false;
   }
}

uint32_t ir3_nir_max_imm_offset(nir_intrinsic_instr *intrin, const void *data);

ENDC;

#endif /* IR3_NIR_H_ */
