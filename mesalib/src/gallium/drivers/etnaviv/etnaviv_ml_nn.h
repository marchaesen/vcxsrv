/*
 * Copyright (c) 2023-2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "etnaviv_ml.h"
#include "etnaviv_context.h"

void
etna_ml_calc_addition_sizes(unsigned *input_width, unsigned *input_height, unsigned *input_channels,
                            unsigned *output_width, unsigned *output_height, unsigned *output_channels);

unsigned
etna_ml_calculate_tiling_v7(struct etna_context *ctx, const struct etna_operation *operation, unsigned *tile_width_out, unsigned *tile_height_out);

struct etna_bo *
etna_ml_create_coeffs_v7(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation, unsigned *cache_size);

unsigned
etna_ml_calculate_tiling_v8(struct etna_context *ctx, const struct etna_operation *operation, unsigned *tile_width_out, unsigned *tile_height_out);

struct etna_bo *
etna_ml_create_coeffs_v8(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation, unsigned *cache_size);

void
etna_ml_lower_convolution(struct etna_ml_subgraph *subgraph,
                          const struct pipe_ml_operation *poperation,
                          struct etna_operation *operation);

void
etna_ml_lower_add(struct etna_ml_subgraph *subgraph,
                  const struct pipe_ml_operation *poperation,
                  struct etna_operation *operation);

void
etna_ml_lower_fully_connected(struct etna_ml_subgraph *subgraph,
                              const struct pipe_ml_operation *poperation,
                              struct etna_operation *operation);

void
etna_ml_compile_operation_nn(struct etna_ml_subgraph *subgraph,
                             const struct etna_operation *operation,
                             struct etna_vip_instruction *instruction);

void
etna_ml_emit_operation_nn(struct etna_ml_subgraph *subgraph,
                          struct etna_vip_instruction *operation,
                          unsigned idx);
