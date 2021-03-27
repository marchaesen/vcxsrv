/*
 * Copyright © 2021 Valve Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */


#ifndef AC_NIR_H
#define AC_NIR_H

#include "nir.h"
#include "ac_shader_args.h"
#include "amd_family.h"

#ifdef __cplusplus
extern "C" {
#endif

void
ac_nir_lower_ls_outputs_to_mem(nir_shader *ls,
                               bool tcs_in_out_eq,
                               uint64_t tcs_temp_only_inputs,
                               unsigned num_reserved_ls_outputs);

void
ac_nir_lower_hs_inputs_to_mem(nir_shader *shader,
                              bool tcs_in_out_eq,
                              unsigned num_reserved_tcs_inputs);

void
ac_nir_lower_hs_outputs_to_mem(nir_shader *shader,
                               enum chip_class chip_class,
                               bool tes_reads_tessfactors,
                               uint64_t tes_inputs_read,
                               uint64_t tes_patch_inputs_read,
                               unsigned num_reserved_tcs_inputs,
                               unsigned num_reserved_tcs_outputs,
                               unsigned num_reserved_tcs_patch_outputs,
                               bool emit_tess_factor_write);

void
ac_nir_lower_tes_inputs_to_mem(nir_shader *shader,
                               unsigned num_reserved_tcs_outputs,
                               unsigned num_reserved_tcs_patch_outputs);

enum ac_nir_tess_to_const_options {
    ac_nir_lower_patch_vtx_in = 1 << 0,
    ac_nir_lower_num_patches = 1 << 1,
};

void
ac_nir_lower_tess_to_const(nir_shader *shader,
                           unsigned patch_vtx_in,
                           unsigned tcs_num_patches,
                           unsigned options);

void
ac_nir_lower_es_outputs_to_mem(nir_shader *shader,
                               enum chip_class chip_class,
                               unsigned num_reserved_es_outputs);

void
ac_nir_lower_gs_inputs_to_mem(nir_shader *shader,
                              enum chip_class chip_class,
                              unsigned num_reserved_es_outputs);

#ifdef __cplusplus
}
#endif

#endif /* AC_NIR_H */
