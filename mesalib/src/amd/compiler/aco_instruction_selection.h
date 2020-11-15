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
   has_vmem_loadstore = has_vmem_store | has_glc_vmem_load | has_nonglc_vmem_load,
   has_nonglc_vmem_loadstore = has_nonglc_vmem_load | has_nonglc_vmem_store,

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
   bool has_gfx10_wave64_bpermute = false;
   struct {
      bool has_branch;
      uint16_t loop_nest_depth = 0;
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
   unsigned tcs_tess_lvl_out_loc;
   unsigned tcs_tess_lvl_in_loc;
   uint64_t tcs_temp_only_inputs;
   uint32_t tcs_num_inputs;
   uint32_t tcs_num_outputs;
   uint32_t tcs_num_patch_outputs;
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
   int desc_set = -1;
   unsigned binding = 0;

   if (!def) {
      /* global resources are considered aliasing with all other buffers and
       * buffer images */
      // TODO: only merge flags of resources which can really alias.
   } else if (def->parent_instr->type == nir_instr_type_alu) {
      nir_alu_instr* mov_instr = nir_instr_as_alu(def->parent_instr);
      if (mov_instr->op == nir_op_mov && mov_instr->src[0].swizzle[0] == 0 &&
          mov_instr->src[0].src.ssa->parent_instr->type == nir_instr_type_intrinsic) {
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(mov_instr->src[0].src.ssa->parent_instr);
         if (intrin->intrinsic == nir_intrinsic_vulkan_resource_index) {
            desc_set = nir_intrinsic_desc_set(intrin);
            binding = nir_intrinsic_binding(intrin);
         }
      }
   } else if (def->parent_instr->type == nir_instr_type_deref) {
      nir_deref_instr *deref = nir_instr_as_deref(def->parent_instr);
      assert(deref->type->is_image());
      if (deref->type->sampler_dimensionality != GLSL_SAMPLER_DIM_BUF) {
         *flags = NULL;
         *count = 0;
         return;
      }

      nir_variable *var = nir_deref_instr_get_variable(deref);
      desc_set = var->data.descriptor_set;
      binding = var->data.binding;
   }

   if (desc_set < 0) {
      *flags = ctx->buffer_resource_flags.data();
      *count = ctx->buffer_resource_flags.size();
      return;
   }

   unsigned set_offset = ctx->resource_flag_offsets[desc_set];

   if (!(ctx->buffer_resource_flags[set_offset + binding] & buffer_is_restrict)) {
      /* Non-restrict buffers alias only with other non-restrict buffers.
       * We reserve flags[0] for these. */
      *flags = ctx->buffer_resource_flags.data();
      *count = 1;
      return;
   }

   *flags = ctx->buffer_resource_flags.data() + set_offset + binding;
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

inline bool can_subdword_ssbo_store_use_smem(nir_intrinsic_instr *intrin)
{
   unsigned wrmask = nir_intrinsic_write_mask(intrin);
   if (util_last_bit(wrmask) != util_bitcount(wrmask) ||
       util_bitcount(wrmask) * intrin->src[0].ssa->bit_size % 32 ||
       util_bitcount(wrmask) != intrin->src[0].ssa->num_components)
      return false;

   if (nir_intrinsic_align_mul(intrin) % 4 || nir_intrinsic_align_offset(intrin) % 4)
      return false;

   return true;
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
