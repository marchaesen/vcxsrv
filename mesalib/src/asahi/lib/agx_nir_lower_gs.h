/*
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "nir.h"
#include "shader_enums.h"

enum mesa_prim;

struct agx_lower_output_to_var_state {
   struct nir_variable *outputs[NUM_TOTAL_VARYING_SLOTS];
};

bool agx_lower_output_to_var(struct nir_builder *b, struct nir_instr *instr,
                             void *data);

struct nir_def *agx_load_per_vertex_input(struct nir_builder *b,
                                          nir_intrinsic_instr *intr,
                                          struct nir_def *vertex);

nir_def *agx_nir_load_vertex_id(struct nir_builder *b, nir_def *id,
                                unsigned index_size_B);

bool agx_nir_lower_sw_vs(struct nir_shader *s, unsigned index_size_B);

bool agx_nir_lower_vs_before_gs(struct nir_shader *vs);

bool agx_nir_lower_gs(struct nir_shader *gs, bool rasterizer_discard,
                      struct nir_shader **gs_count, struct nir_shader **gs_copy,
                      struct nir_shader **pre_gs, enum mesa_prim *out_mode,
                      unsigned *out_count_words);

bool agx_nir_lower_tcs(struct nir_shader *tcs);

bool agx_nir_lower_tes(struct nir_shader *tes, bool to_hw_vs);

uint64_t agx_tcs_per_vertex_outputs(const struct nir_shader *nir);

unsigned agx_tcs_output_stride(const struct nir_shader *nir);
