/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_NIR_H
#define PVR_NIR_H

#include "nir/nir.h"
#include "pco/pco.h"
#include "pvr_private.h"

void pvr_lower_nir(pco_ctx *ctx,
                   struct pvr_pipeline_layout *layout,
                   nir_shader *nir);

#endif /* PVR_NIR_H */
