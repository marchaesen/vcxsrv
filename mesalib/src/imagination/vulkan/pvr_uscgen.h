/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_USCGEN_H
#define PVR_USCGEN_H

/**
 * \file pvr_uscgen.h
 *
 * \brief USC shader generation header.
 */

#include "compiler/shader_enums.h"
#include "pco/pco.h"

/* NOP shader generation. */
void pvr_uscgen_nop(pco_ctx *ctx, gl_shader_stage stage, pco_binary **binary);

/* EOT shader generation. */
struct pvr_eot_props {
};

void pvr_uscgen_eot(pco_ctx *ctx,
                    struct pvr_eot_props *props,
                    pco_binary **binary);

/* Transfer queue shader generation. */
struct pvr_tq_props {
};

void pvr_uscgen_tq(pco_ctx *ctx,
                   struct pvr_tq_props *props,
                   pco_binary **binary);

#endif /* PVR_USCGEN_H */
