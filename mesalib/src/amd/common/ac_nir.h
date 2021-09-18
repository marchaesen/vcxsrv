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
#include "ac_shader_util.h"
#include "amd_family.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of nir_builder so we don't have to include nir_builder.h here */
struct nir_builder;
typedef struct nir_builder nir_builder;

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

bool
ac_nir_lower_indirect_derefs(nir_shader *shader,
                             enum chip_class chip_class);

typedef struct
{
   unsigned lds_bytes_if_culling_off;
   bool can_cull;
   bool passthrough;
   bool early_prim_export;
   uint64_t nggc_inputs_read_by_pos;
   uint64_t nggc_inputs_read_by_others;
} ac_nir_ngg_config;

ac_nir_ngg_config
ac_nir_lower_ngg_nogs(nir_shader *shader,
                      unsigned max_num_es_vertices,
                      unsigned num_vertices_per_primitive,
                      unsigned max_workgroup_size,
                      unsigned wave_size,
                      bool consider_culling,
                      bool consider_passthrough,
                      bool export_prim_id,
                      bool provoking_vtx_last,
                      uint32_t instance_rate_inputs);

void
ac_nir_lower_ngg_gs(nir_shader *shader,
                    unsigned wave_size,
                    unsigned max_workgroup_size,
                    unsigned esgs_ring_lds_bytes,
                    unsigned gs_out_vtx_bytes,
                    unsigned gs_total_out_vtx_bytes,
                    bool provoking_vtx_last);

nir_ssa_def *
ac_nir_cull_triangle(nir_builder *b,
                     nir_ssa_def *initially_accepted,
                     nir_ssa_def *pos[3][4]);

#ifdef __cplusplus
}
#endif

#endif /* AC_NIR_H */
