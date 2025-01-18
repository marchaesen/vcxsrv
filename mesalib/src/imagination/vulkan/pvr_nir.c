/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pvr_nir.h"
#include "util/macros.h"

#include <stdio.h>

/**
 * \brief Performs Vulkan-specific lowering on a NIR shader.
 *
 * \param[in] ctx PCO compiler context.
 * \param[in] layout Graphics/compute pipeline layout.
 * \param[in,out] nir NIR shader.
 */
void pvr_lower_nir(pco_ctx *ctx,
                   struct pvr_pipeline_layout *layout,
                   nir_shader *nir)
{
   puts("finishme: pvr_lower_nir");
}
