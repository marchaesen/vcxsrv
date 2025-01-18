/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PCO_H
#define PCO_H

/**
 * \file pco.h
 *
 * \brief Main compiler interface header.
 */

#include "compiler/nir/nir.h"

/* Defines. */
#define PCO_REG_UNUSED (~0U)

/* Driver-specific forward-declarations. */
struct pvr_device_info;

/* Compiler-specific forward-declarations. */
typedef struct _pco_shader pco_shader;
typedef struct _pco_ctx pco_ctx;
typedef struct _pco_data pco_data;

pco_ctx *pco_ctx_create(const struct pvr_device_info *dev_info, void *mem_ctx);
const struct spirv_to_nir_options *pco_spirv_options(pco_ctx *ctx);
const nir_shader_compiler_options *pco_nir_options(pco_ctx *ctx);

void pco_preprocess_nir(pco_ctx *ctx, nir_shader *nir);
void pco_link_nir(pco_ctx *ctx, nir_shader *producer, nir_shader *consumer);
void pco_rev_link_nir(pco_ctx *ctx, nir_shader *producer, nir_shader *consumer);
void pco_lower_nir(pco_ctx *ctx, nir_shader *nir, pco_data *data);
void pco_postprocess_nir(pco_ctx *ctx, nir_shader *nir, pco_data *data);

pco_shader *
pco_trans_nir(pco_ctx *ctx, nir_shader *nir, pco_data *data, void *mem_ctx);
void pco_process_ir(pco_ctx *ctx, pco_shader *shader);

void pco_encode_ir(pco_ctx *ctx, pco_shader *shader);
void pco_shader_finalize(pco_ctx *ctx, pco_shader *shader);

pco_data *pco_shader_data(pco_shader *shader);

unsigned pco_shader_binary_size(pco_shader *shader);
const void *pco_shader_binary_data(pco_shader *shader);

void pco_validate_shader(pco_shader *shader, const char *when);

void pco_print_shader(pco_shader *shader, FILE *fp, const char *when);
void pco_print_binary(pco_shader *shader, FILE *fp, const char *when);
#endif /* PCO_H */
