/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pvr_uscgen.c
 *
 * \brief USC shader generation.
 */

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "pco/pco.h"
#include "pvr_uscgen.h"
#include "util/macros.h"

/**
 * Common function to build a NIR shader and export the binary.
 *
 * \param ctx PCO context.
 * \param nir NIR shader.
 * \param binary Output shader binary.
 */
static void build_shader(pco_ctx *ctx, nir_shader *nir, pco_binary **binary)
{
   pco_preprocess_nir(ctx, nir);
   pco_lower_nir(ctx, nir);
   pco_postprocess_nir(ctx, nir);

   pco_shader *shader = pco_trans_nir(ctx, nir);
   pco_process_ir(ctx, shader);

   pco_binary *bin = pco_encode_ir(ctx, shader);
   ralloc_free(shader);

   pco_binary_finalize(ctx, bin);
   *binary = bin;
}

/**
 * Generate a nop (empty) shader.
 *
 * \param ctx PCO context.
 * \param stage Shader stage.
 * \param binary Output shader binary.
 */
void pvr_uscgen_nop(pco_ctx *ctx, gl_shader_stage stage, pco_binary **binary)
{
   unreachable("finishme: pvr_uscgen_nop");
}

/**
 * Generate an end-of-tile shader.
 *
 * \param ctx PCO context.
 * \param props End of tile shader properties.
 * \param binary Output shader binary.
 */
void pvr_uscgen_eot(pco_ctx *ctx,
                    struct pvr_eot_props *props,
                    pco_binary **binary)
{
   unreachable("finishme: pvr_uscgen_eot");
}

/**
 * Generate a transfer queue shader.
 *
 * \param ctx PCO context.
 * \param props Transfer queue shader properties.
 * \param binary Output shader binary.
 */
void pvr_uscgen_tq(pco_ctx *ctx,
                   struct pvr_tq_props *props,
                   pco_binary **binary)
{
   unreachable("finishme: pvr_uscgen_tq");
}
