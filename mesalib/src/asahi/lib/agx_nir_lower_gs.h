/*
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "shaders/tessellator.h"
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

bool agx_nir_lower_sw_vs(struct nir_shader *s, unsigned index_size_B);

bool agx_nir_lower_vs_before_gs(struct nir_shader *vs,
                                const struct nir_shader *libagx);

bool agx_nir_lower_gs(struct nir_shader *gs, const struct nir_shader *libagx,
                      bool rasterizer_discard, struct nir_shader **gs_count,
                      struct nir_shader **gs_copy, struct nir_shader **pre_gs,
                      enum mesa_prim *out_mode, unsigned *out_count_words);

void agx_nir_prefix_sum_gs(struct nir_builder *b, const void *data);

void agx_nir_prefix_sum_tess(struct nir_builder *b, const void *data);

struct agx_gs_setup_indirect_key {
   enum mesa_prim prim;
};

void agx_nir_gs_setup_indirect(struct nir_builder *b, const void *key);

struct agx_unroll_restart_key {
   enum mesa_prim prim;
   unsigned index_size_B;
};

void agx_nir_unroll_restart(struct nir_builder *b, const void *key);

struct agx_tessellator_key {
   enum tess_primitive_mode prim                      : 8;
   enum libagx_tess_output_primitive output_primitive : 8;
   enum libagx_tess_partitioning partitioning         : 8;
   enum libagx_tess_mode mode                         : 8;
};
static_assert(sizeof(struct agx_tessellator_key) == 4, "padded");

struct agx_tess_setup_indirect_key {
   bool point_mode;
   bool with_counts;
   bool padding[2];
};
static_assert(sizeof(struct agx_tess_setup_indirect_key) == 4, "padded");

void agx_nir_tessellate(struct nir_builder *b, const void *key);

bool agx_nir_lower_tcs(struct nir_shader *tcs, const struct nir_shader *libagx);

bool agx_nir_lower_tes(struct nir_shader *tes, const struct nir_shader *libagx,
                       bool to_hw_vs);

uint64_t agx_tcs_per_vertex_outputs(const struct nir_shader *nir);

unsigned agx_tcs_output_stride(const struct nir_shader *nir);

void agx_nir_tess_setup_indirect(struct nir_builder *b, const void *data);

void agx_nir_increment_statistic(struct nir_builder *b, const void *data);

void agx_nir_increment_cs_invocations(struct nir_builder *b, const void *data);

struct agx_increment_ia_counters_key {
   /* Implies primitive restart */
   uint8_t index_size_B;
};
static_assert(sizeof(struct agx_increment_ia_counters_key) == 1, "padded");

void agx_nir_increment_ia_counters(struct nir_builder *b, const void *data);

struct agx_predicate_indirect_key {
   bool indexed;
};
static_assert(sizeof(struct agx_predicate_indirect_key) == 1, "padded");

void agx_nir_predicate_indirect(struct nir_builder *b, const void *data);

struct agx_decompress_key {
   uint8_t nr_samples;
};
static_assert(sizeof(struct agx_decompress_key) == 1, "padded");

void agx_nir_decompress(struct nir_builder *b, const void *data);
