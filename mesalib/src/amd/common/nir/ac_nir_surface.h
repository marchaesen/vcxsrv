/*
 * Copyright © 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_NIR__SURFACE_H
#define AC_NIR__SURFACE_H

#include "ac_surface.h"
#include "amd_family.h"
#include "nir_defines.h"
#include "util/format/u_format.h"

#ifdef __cplusplus
extern "C" {
#endif

nir_def *ac_nir_dcc_addr_from_coord(nir_builder *b, const struct radeon_info *info,
                                    unsigned bpe, const struct gfx9_meta_equation *equation,
                                    nir_def *dcc_pitch, nir_def *dcc_height,
                                    nir_def *dcc_slice_size,
                                    nir_def *x, nir_def *y, nir_def *z,
                                    nir_def *sample, nir_def *pipe_xor);

nir_def *ac_nir_cmask_addr_from_coord(nir_builder *b, const struct radeon_info *info,
                                      const struct gfx9_meta_equation *equation,
                                      nir_def *cmask_pitch, nir_def *cmask_height,
                                      nir_def *cmask_slice_size,
                                      nir_def *x, nir_def *y, nir_def *z,
                                      nir_def *pipe_xor,
                                      nir_def **bit_position);

nir_def *ac_nir_htile_addr_from_coord(nir_builder *b, const struct radeon_info *info,
                                      const struct gfx9_meta_equation *equation,
                                      nir_def *htile_pitch,
                                      nir_def *htile_slice_size,
                                      nir_def *x, nir_def *y, nir_def *z,
                                      nir_def *pipe_xor);

#ifdef __cplusplus
}
#endif

#endif /* AC_NIR__SURFACE_H */
