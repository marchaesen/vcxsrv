/*
 * Copyright © 2018 Valve Corporation
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

#include <array>
#include <unordered_map>
#include "aco_ir.h"
#include "vulkan/radv_shader_args.h"

namespace aco {

struct shader_io_state {
   uint8_t mask[VARYING_SLOT_MAX];
   Temp temps[VARYING_SLOT_MAX * 4u];

   shader_io_state() {
      memset(mask, 0, sizeof(mask));
      std::fill_n(temps, VARYING_SLOT_MAX * 4u, Temp(0, RegClass::v1));
   }
};

enum resource_flags {
   has_glc_vmem_load = 0x1,
   has_nonglc_vmem_load = 0x2,
   has_glc_vmem_store = 0x4,
   has_nonglc_vmem_store = 0x8,

   has_vmem_store = has_glc_vmem_store | has_nonglc_vmem_store,

   buffer_is_restrict = 0x10,
};

struct isel_context {
   const struct radv_nir_compiler_options *options;
   struct radv_shader_args *args;
   Program *program;
   nir_shader *shader;
   uint32_t constant_data_offset;
   Block *block;
   uint32_t first_temp_id;
   std::unordered_map<unsigned, std::array<Temp,NIR_MAX_VEC_COMPONENTS>> allocated_vec;
   Stage stage;
   struct {
      bool has_branch;
      struct {
         unsigned header_idx;
         Block* exit;
         bool has_divergent_continue = false;
         bool has_divergent_branch = false;
      } parent_loop;
      struct {
         bool is_divergent = false;
      } parent_if;
      bool exec_potentially_empty_discard = false; /* set to false when loop_nest_depth==0 && parent_if.is_divergent==false */
      uint16_t exec_potentially_empty_break_depth = UINT16_MAX;
      /* Set to false when loop_nest_depth==exec_potentially_empty_break_depth
       * and parent_if.is_divergent==false. Called _break but it's also used for
       * loop continues. */
      bool exec_potentially_empty_break = false;
      std::unique_ptr<unsigned[]> nir_to_aco; /* NIR block index to ACO block index */
   } cf_info;

   /* NIR range analysis. */
   struct hash_table *range_ht;
   nir_unsigned_upper_bound_config ub_config;

   uint32_t resource_flag_offsets[MAX_SETS];
   std::vector<uint8_t> buffer_resource_flags;

   Temp arg_temps[AC_MAX_ARGS];

   /* FS inputs */
   Temp persp_centroid, linear_centroid;

   /* GS inputs */
   bool ngg_nogs_early_prim_export = false;
   bool ngg_gs_early_alloc = false;
   bool ngg_gs_known_vtxcnt[4] = {false, false, false, false};
   Temp gs_wave_id;
   unsigned ngg_gs_emit_addr = 0;
   unsigned ngg_gs_emit_vtx_bytes = 0;
   unsigned ngg_gs_scratch_addr = 0;
   unsigned ngg_gs_primflags_offset = 0;
   int ngg_gs_const_vtxcnt[4];
   int ngg_gs_const_prmcnt[4];

   /* VS output information */
   bool export_clip_dists;
   unsigned num_clip_distances;
   unsigned num_cull_distances;

   /* tessellation information */
   uint64_t tcs_temp_only_inputs;
   uint32_t tcs_num_patches;
   bool tcs_in_out_eq = false;

   /* I/O information */
   shader_io_state inputs;
   shader_io_state outputs;
};

inline Temp get_arg(isel_context *ctx, struct ac_arg arg)
{
   assert(arg.used);
   return ctx->arg_temps[arg.arg_index];
}

inline void get_buffer_resource_flags(isel_context *ctx, nir_ssa_def *def, unsigned access,
                                      uint8_t **flags, uint32_t *count)
{
   nir_binding binding = {0};
   /* global resources (def=NULL) are considered aliasing with all other buffers and
    * buffer images */
   // TODO: only merge flags of resources which can really alias.
   if (def)
      binding = nir_chase_binding(nir_src_for_ssa(def));

   if (binding.var) {
      const glsl_type *type = binding.var->type->without_array();
      assert(type->is_image());
      if (type->sampler_dimensionality != GLSL_SAMPLER_DIM_BUF) {
         *flags = NULL;
         *count = 0;
         return;
      }
   }

   if (!binding.success) {
      *flags = ctx->buffer_resource_flags.data();
      *count = ctx->buffer_resource_flags.size();
      return;
   }

   unsigned set_offset = ctx->resource_flag_offsets[binding.desc_set];

   if (!(ctx->buffer_resource_flags[set_offset + binding.binding] & buffer_is_restrict)) {
      /* Non-restrict buffers alias only with other non-restrict buffers.
       * We reserve flags[0] for these. */
      *flags = ctx->buffer_resource_flags.data();
      *count = 1;
      return;
   }

   *flags = ctx->buffer_resource_flags.data() + set_offset + binding.binding;
   *count = 1;
}

inline uint8_t get_all_buffer_resource_flags(isel_context *ctx, nir_ssa_def *def, unsigned access)
{
   uint8_t *flags;
   uint32_t count;
   get_buffer_resource_flags(ctx, def, access, &flags, &count);

   uint8_t res = 0;
   for (unsigned i = 0; i < count; i++)
      res |= flags[i];
   return res;
}

void init_context(isel_context *ctx, nir_shader *shader);
void cleanup_context(isel_context *ctx);

isel_context
setup_isel_context(Program* program,
                   unsigned shader_count,
                   struct nir_shader *const *shaders,
                   ac_shader_config* config,
                   struct radv_shader_args *args,
                   bool is_gs_copy_shader);

}
