/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "ac_nir_helpers.h"

#include "nir_builder.h"

typedef struct {
   enum amd_gfx_level gfx_level;
   bool use_llvm;
   bool after_lowering;
} mem_access_cb_data;

static bool
use_smem_for_load(nir_builder *b, nir_intrinsic_instr *intrin, void *cb_data_)
{
   const mem_access_cb_data *cb_data = (mem_access_cb_data *)cb_data_;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
   case nir_intrinsic_load_global_amd:
   case nir_intrinsic_load_constant:
      if (cb_data->use_llvm)
         return false;
      break;
   case nir_intrinsic_load_ubo:
      break;
   default:
      return false;
   }

   if (intrin->def.divergent || (cb_data->after_lowering && intrin->def.bit_size < 32))
      return false;

   enum gl_access_qualifier access = nir_intrinsic_access(intrin);
   bool glc = access & (ACCESS_VOLATILE | ACCESS_COHERENT);
   bool reorder = nir_intrinsic_can_reorder(intrin) || ((access & ACCESS_NON_WRITEABLE) && !(access & ACCESS_VOLATILE));
   if (!reorder || (glc && cb_data->gfx_level < GFX8))
      return false;

   nir_intrinsic_set_access(intrin, access | ACCESS_SMEM_AMD);
   return true;
}

static nir_mem_access_size_align
lower_mem_access_cb(nir_intrinsic_op intrin, uint8_t bytes, uint8_t bit_size, uint32_t align_mul, uint32_t align_offset,
                    bool offset_is_const, enum gl_access_qualifier access, const void *cb_data_)
{
   const mem_access_cb_data *cb_data = (mem_access_cb_data *)cb_data_;
   const bool is_load = nir_intrinsic_infos[intrin].has_dest;
   const bool is_smem = intrin == nir_intrinsic_load_push_constant || (access & ACCESS_SMEM_AMD);
   const uint32_t combined_align = nir_combined_align(align_mul, align_offset);

   /* Make 8-bit accesses 16-bit if possible */
   if (is_load && bit_size == 8 && combined_align >= 2 && bytes % 2 == 0)
      bit_size = 16;

   unsigned max_components = 4;
   if (cb_data->use_llvm && access & (ACCESS_COHERENT | ACCESS_VOLATILE) &&
       (intrin == nir_intrinsic_load_global || intrin == nir_intrinsic_store_global))
      max_components = 1;
   else if (is_smem)
      max_components = MIN2(512 / bit_size, 16);

   nir_mem_access_size_align res;
   res.num_components = MIN2(bytes / (bit_size / 8), max_components);
   res.bit_size = bit_size;
   res.align = MIN2(bit_size / 8, 4); /* 64-bit access only requires 4 byte alignment. */
   res.shift = nir_mem_access_shift_method_shift64;

   if (!is_load)
      return res;

   /* Lower 8/16-bit loads to 32-bit, unless it's a VMEM scalar load. */

   const bool support_subdword = res.num_components == 1 && !is_smem &&
                                 (!cb_data->use_llvm || intrin != nir_intrinsic_load_ubo);

   if (res.bit_size >= 32 || support_subdword)
      return res;

   const uint32_t max_pad = 4 - MIN2(combined_align, 4);

   /* Global loads don't have bounds checking, so increasing the size might not be safe. */
   if (intrin == nir_intrinsic_load_global || intrin == nir_intrinsic_load_global_constant) {
      if (align_mul < 4) {
         /* If we split the load, only lower it to 32-bit if this is a SMEM load. */
         const unsigned chunk_bytes = align(bytes, 4) - max_pad;
         if (!is_smem && chunk_bytes < bytes)
            return res;
      }

      res.num_components = DIV_ROUND_UP(bytes, 4);
   } else {
      res.num_components = DIV_ROUND_UP(bytes + max_pad, 4);
   }
   res.num_components = MIN2(res.num_components, max_components);
   res.bit_size = 32;
   res.align = 4;
   res.shift = is_smem ? res.shift : nir_mem_access_shift_method_bytealign_amd;

   return res;
}

bool
ac_nir_flag_smem_for_loads(nir_shader *shader, enum amd_gfx_level gfx_level, bool use_llvm, bool after_lowering)
{
   mem_access_cb_data cb_data = {
      .gfx_level = gfx_level,
      .use_llvm = use_llvm,
      .after_lowering = after_lowering,
   };
   return nir_shader_intrinsics_pass(shader, &use_smem_for_load, nir_metadata_all, &cb_data);
}

bool
ac_nir_lower_mem_access_bit_sizes(nir_shader *shader, enum amd_gfx_level gfx_level, bool use_llvm)
{
   mem_access_cb_data cb_data = {
      .gfx_level = gfx_level,
      .use_llvm = use_llvm,
   };
   nir_lower_mem_access_bit_sizes_options lower_mem_access_options = {
      .callback = &lower_mem_access_cb,
      .modes = nir_var_mem_ubo | nir_var_mem_push_const | nir_var_mem_ssbo |
               nir_var_mem_global | nir_var_mem_constant | nir_var_mem_shared |
               nir_var_shader_temp,
      .may_lower_unaligned_stores_to_atomics = false,
      .cb_data = &cb_data,
   };
   return nir_lower_mem_access_bit_sizes(shader, &lower_mem_access_options);
}