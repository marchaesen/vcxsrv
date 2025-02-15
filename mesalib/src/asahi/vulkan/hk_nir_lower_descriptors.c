/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "pipe/p_defines.h"
#include "vulkan/vulkan_core.h"
#include "agx_nir_texture.h"
#include "hk_cmd_buffer.h"
#include "hk_descriptor_set.h"
#include "hk_descriptor_set_layout.h"
#include "hk_shader.h"

#include "nir.h"
#include "nir_builder.h"
#include "nir_builder_opcodes.h"
#include "nir_intrinsics.h"
#include "nir_intrinsics_indices.h"
#include "shader_enums.h"
#include "vk_pipeline.h"

struct lower_descriptors_ctx {
   const struct hk_descriptor_set_layout *set_layouts[HK_MAX_SETS];

   bool clamp_desc_array_bounds;
   nir_address_format ubo_addr_format;
   nir_address_format ssbo_addr_format;
};

static const struct hk_descriptor_set_binding_layout *
get_binding_layout(uint32_t set, uint32_t binding,
                   const struct lower_descriptors_ctx *ctx)
{
   assert(set < HK_MAX_SETS);
   assert(ctx->set_layouts[set] != NULL);

   const struct hk_descriptor_set_layout *set_layout = ctx->set_layouts[set];

   assert(binding < set_layout->binding_count);
   return &set_layout->binding[binding];
}

static nir_def *
load_speculatable(nir_builder *b, unsigned num_components, unsigned bit_size,
                  nir_def *addr, unsigned align)
{
   return nir_build_load_global_constant(b, num_components, bit_size, addr,
                                         .align_mul = align,
                                         .access = ACCESS_CAN_SPECULATE);
}

static nir_def *
load_root(nir_builder *b, unsigned num_components, unsigned bit_size,
          nir_def *offset, unsigned align)
{
   nir_def *root = nir_load_preamble(b, 1, 64, .base = HK_ROOT_UNIFORM);

   /* We've bound the address of the root descriptor, index in. */
   nir_def *addr = nir_iadd(b, root, nir_u2u64(b, offset));

   return load_speculatable(b, num_components, bit_size, addr, align);
}

static bool
lower_load_constant(nir_builder *b, nir_intrinsic_instr *load,
                    const struct lower_descriptors_ctx *ctx)
{
   assert(load->intrinsic == nir_intrinsic_load_constant);
   unreachable("todo: stick an address in the root descriptor or something");

   uint32_t base = nir_intrinsic_base(load);
   uint32_t range = nir_intrinsic_range(load);

   b->cursor = nir_before_instr(&load->instr);

   nir_def *offset = nir_iadd_imm(b, load->src[0].ssa, base);
   nir_def *data = nir_load_ubo(
      b, load->def.num_components, load->def.bit_size, nir_imm_int(b, 0),
      offset, .align_mul = nir_intrinsic_align_mul(load),
      .align_offset = nir_intrinsic_align_offset(load), .range_base = base,
      .range = range);

   nir_def_rewrite_uses(&load->def, data);

   return true;
}

static nir_def *
load_descriptor_set_addr(nir_builder *b, uint32_t set,
                         UNUSED const struct lower_descriptors_ctx *ctx)
{
   uint32_t set_addr_offset =
      hk_root_descriptor_offset(sets) + set * sizeof(uint64_t);

   return load_root(b, 1, 64, nir_imm_int(b, set_addr_offset), 8);
}

static nir_def *
load_dynamic_buffer_start(nir_builder *b, uint32_t set,
                          const struct lower_descriptors_ctx *ctx)
{
   int dynamic_buffer_start_imm = 0;
   for (uint32_t s = 0; s < set; s++) {
      if (ctx->set_layouts[s] == NULL) {
         dynamic_buffer_start_imm = -1;
         break;
      }

      dynamic_buffer_start_imm += ctx->set_layouts[s]->dynamic_buffer_count;
   }

   if (dynamic_buffer_start_imm >= 0) {
      return nir_imm_int(b, dynamic_buffer_start_imm);
   } else {
      uint32_t root_offset =
         hk_root_descriptor_offset(set_dynamic_buffer_start) + set;

      return nir_u2u32(b, load_root(b, 1, 8, nir_imm_int(b, root_offset), 1));
   }
}

static nir_def *
load_descriptor(nir_builder *b, unsigned num_components, unsigned bit_size,
                uint32_t set, uint32_t binding, nir_def *index,
                unsigned offset_B, const struct lower_descriptors_ctx *ctx)
{
   const struct hk_descriptor_set_binding_layout *binding_layout =
      get_binding_layout(set, binding, ctx);

   if (ctx->clamp_desc_array_bounds)
      index =
         nir_umin(b, index, nir_imm_int(b, binding_layout->array_size - 1));

   switch (binding_layout->type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
      /* Get the index in the root descriptor table dynamic_buffers array. */
      nir_def *dynamic_buffer_start = load_dynamic_buffer_start(b, set, ctx);

      index = nir_iadd(b, index,
                       nir_iadd_imm(b, dynamic_buffer_start,
                                    binding_layout->dynamic_buffer_index));

      nir_def *root_desc_offset = nir_iadd_imm(
         b, nir_imul_imm(b, index, sizeof(struct hk_buffer_address)),
         hk_root_descriptor_offset(dynamic_buffers));

      assert(num_components == 4 && bit_size == 32);
      nir_def *desc = load_root(b, 4, 32, root_desc_offset, 16);

      /* We know a priori that the the .w compnent (offset) is zero */
      return nir_vector_insert_imm(b, desc, nir_imm_int(b, 0), 3);
   }

   case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: {
      nir_def *base_addr = nir_iadd_imm(
         b, load_descriptor_set_addr(b, set, ctx), binding_layout->offset);

      assert(binding_layout->stride == 1);
      const uint32_t binding_size = binding_layout->array_size;

      /* Convert it to nir_address_format_64bit_bounded_global */
      assert(num_components == 4 && bit_size == 32);
      return nir_vec4(b, nir_unpack_64_2x32_split_x(b, base_addr),
                      nir_unpack_64_2x32_split_y(b, base_addr),
                      nir_imm_int(b, binding_size), nir_imm_int(b, 0));
   }

   default: {
      assert(binding_layout->stride > 0);
      nir_def *desc_ubo_offset =
         nir_iadd_imm(b, nir_imul_imm(b, index, binding_layout->stride),
                      binding_layout->offset + offset_B);

      unsigned desc_align_mul = (1 << (ffs(binding_layout->stride) - 1));
      desc_align_mul = MIN2(desc_align_mul, 16);
      unsigned desc_align_offset = binding_layout->offset + offset_B;
      desc_align_offset %= desc_align_mul;

      nir_def *desc;
      nir_def *set_addr = load_descriptor_set_addr(b, set, ctx);
      desc = nir_load_global_constant_offset(
         b, num_components, bit_size, set_addr, desc_ubo_offset,
         .align_mul = desc_align_mul, .align_offset = desc_align_offset,
         .access = ACCESS_CAN_SPECULATE);

      if (binding_layout->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
          binding_layout->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
         /* We know a priori that the the .w compnent (offset) is zero */
         assert(num_components == 4 && bit_size == 32);
         desc = nir_vector_insert_imm(b, desc, nir_imm_int(b, 0), 3);
      }
      return desc;
   }
   }
}

static bool
is_idx_intrin(nir_intrinsic_instr *intrin)
{
   while (intrin->intrinsic == nir_intrinsic_vulkan_resource_reindex) {
      intrin = nir_src_as_intrinsic(intrin->src[0]);
      if (intrin == NULL)
         return false;
   }

   return intrin->intrinsic == nir_intrinsic_vulkan_resource_index;
}

static nir_def *
load_descriptor_for_idx_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                               const struct lower_descriptors_ctx *ctx)
{
   nir_def *index = nir_imm_int(b, 0);

   while (intrin->intrinsic == nir_intrinsic_vulkan_resource_reindex) {
      index = nir_iadd(b, index, intrin->src[1].ssa);
      intrin = nir_src_as_intrinsic(intrin->src[0]);
   }

   assert(intrin->intrinsic == nir_intrinsic_vulkan_resource_index);
   uint32_t set = nir_intrinsic_desc_set(intrin);
   uint32_t binding = nir_intrinsic_binding(intrin);
   index = nir_iadd(b, index, intrin->src[0].ssa);

   return load_descriptor(b, 4, 32, set, binding, index, 0, ctx);
}

static bool
try_lower_load_vulkan_descriptor(nir_builder *b, nir_intrinsic_instr *intrin,
                                 const struct lower_descriptors_ctx *ctx)
{
   ASSERTED const VkDescriptorType desc_type = nir_intrinsic_desc_type(intrin);
   b->cursor = nir_before_instr(&intrin->instr);

   nir_intrinsic_instr *idx_intrin = nir_src_as_intrinsic(intrin->src[0]);
   if (idx_intrin == NULL || !is_idx_intrin(idx_intrin)) {
      assert(desc_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
             desc_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
      return false;
   }

   nir_def *desc = load_descriptor_for_idx_intrin(b, idx_intrin, ctx);

   nir_def_rewrite_uses(&intrin->def, desc);

   return true;
}

static bool
_lower_sysval_to_root_table(nir_builder *b, nir_intrinsic_instr *intrin,
                            uint32_t root_table_offset)
{
   b->cursor = nir_instr_remove(&intrin->instr);
   assert((root_table_offset & 3) == 0 && "aligned");

   nir_def *val = load_root(b, intrin->def.num_components, intrin->def.bit_size,
                            nir_imm_int(b, root_table_offset), 4);

   nir_def_rewrite_uses(&intrin->def, val);

   return true;
}

#define lower_sysval_to_root_table(b, intrin, member)                          \
   _lower_sysval_to_root_table(b, intrin, hk_root_descriptor_offset(member))

static bool
lower_load_push_constant(nir_builder *b, nir_intrinsic_instr *load,
                         const struct lower_descriptors_ctx *ctx)
{
   const uint32_t push_region_offset = hk_root_descriptor_offset(push);
   const uint32_t base = nir_intrinsic_base(load);

   b->cursor = nir_before_instr(&load->instr);

   nir_def *offset =
      nir_iadd_imm(b, load->src[0].ssa, push_region_offset + base);

   nir_def *val = load_root(b, load->def.num_components, load->def.bit_size,
                            offset, load->def.bit_size / 8);

   nir_def_rewrite_uses(&load->def, val);

   return true;
}

static void
get_resource_deref_binding(nir_builder *b, nir_deref_instr *deref,
                           uint32_t *set, uint32_t *binding, nir_def **index)
{
   if (deref->deref_type == nir_deref_type_array) {
      *index = deref->arr.index.ssa;
      deref = nir_deref_instr_parent(deref);
   } else {
      *index = nir_imm_int(b, 0);
   }

   assert(deref->deref_type == nir_deref_type_var);
   nir_variable *var = deref->var;

   *set = var->data.descriptor_set;
   *binding = var->data.binding;
}

static nir_def *
load_resource_deref_desc(nir_builder *b, unsigned num_components,
                         unsigned bit_size, nir_deref_instr *deref,
                         unsigned offset_B,
                         const struct lower_descriptors_ctx *ctx)
{
   uint32_t set, binding;
   nir_def *index;
   get_resource_deref_binding(b, deref, &set, &binding, &index);
   return load_descriptor(b, num_components, bit_size, set, binding, index,
                          offset_B, ctx);
}

/*
 * Returns an AGX bindless handle to access an indexed image within the global
 * image heap.
 */
static nir_def *
image_heap_handle(nir_builder *b, nir_def *offset)
{
   return nir_vec2(b, nir_imm_int(b, HK_IMAGE_HEAP_UNIFORM), offset);
}

static bool
lower_image_intrin(nir_builder *b, nir_intrinsic_instr *intr,
                   const struct lower_descriptors_ctx *ctx)
{
   b->cursor = nir_before_instr(&intr->instr);
   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);

   /* Reads and queries use the texture descriptor; writes and atomics PBE. */
   unsigned offs;
   if (intr->intrinsic != nir_intrinsic_image_deref_load &&
       intr->intrinsic != nir_intrinsic_image_deref_size &&
       intr->intrinsic != nir_intrinsic_image_deref_samples) {

      offs = offsetof(struct hk_storage_image_descriptor, pbe_offset);
   } else {
      offs = offsetof(struct hk_storage_image_descriptor, tex_offset);
   }

   nir_def *offset = load_resource_deref_desc(b, 1, 32, deref, offs, ctx);
   nir_rewrite_image_intrinsic(intr, image_heap_handle(b, offset), true);

   return true;
}

static VkQueryPipelineStatisticFlagBits
translate_pipeline_stat_bit(enum pipe_statistics_query_index pipe)
{
   switch (pipe) {
   case PIPE_STAT_QUERY_IA_VERTICES:
      return VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT;
   case PIPE_STAT_QUERY_IA_PRIMITIVES:
      return VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT;
   case PIPE_STAT_QUERY_VS_INVOCATIONS:
      return VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT;
   case PIPE_STAT_QUERY_GS_INVOCATIONS:
      return VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT;
   case PIPE_STAT_QUERY_GS_PRIMITIVES:
      return VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT;
   case PIPE_STAT_QUERY_C_INVOCATIONS:
      return VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT;
   case PIPE_STAT_QUERY_C_PRIMITIVES:
      return VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT;
   case PIPE_STAT_QUERY_PS_INVOCATIONS:
      return VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
   case PIPE_STAT_QUERY_HS_INVOCATIONS:
      return VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT;
   case PIPE_STAT_QUERY_DS_INVOCATIONS:
      return VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT;
   case PIPE_STAT_QUERY_CS_INVOCATIONS:
      return VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
   case PIPE_STAT_QUERY_TS_INVOCATIONS:
      return VK_QUERY_PIPELINE_STATISTIC_TASK_SHADER_INVOCATIONS_BIT_EXT;
   case PIPE_STAT_QUERY_MS_INVOCATIONS:
      return VK_QUERY_PIPELINE_STATISTIC_MESH_SHADER_INVOCATIONS_BIT_EXT;
   }

   unreachable("invalid statistic");
}

static bool
lower_uvs_index(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   unsigned *vs_uniform_base = data;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_uvs_index_agx: {
      gl_varying_slot slot = nir_intrinsic_io_semantics(intrin).location;
      unsigned offset = hk_root_descriptor_offset(draw.uvs_index[slot]);
      b->cursor = nir_instr_remove(&intrin->instr);

      nir_def *val = load_root(b, 1, 8, nir_imm_int(b, offset), 1);
      nir_def_rewrite_uses(&intrin->def, nir_u2u16(b, val));
      return true;
   }

   case nir_intrinsic_load_shader_part_tests_zs_agx:
      return lower_sysval_to_root_table(b, intrin, draw.no_epilog_discard);

   case nir_intrinsic_load_api_sample_mask_agx:
      return lower_sysval_to_root_table(b, intrin, draw.api_sample_mask);

   case nir_intrinsic_load_sample_positions_agx:
      return lower_sysval_to_root_table(b, intrin, draw.ppp_multisamplectl);

   case nir_intrinsic_load_depth_never_agx:
      return lower_sysval_to_root_table(b, intrin, draw.force_never_in_shader);

   case nir_intrinsic_load_geometry_param_buffer_agx:
      return lower_sysval_to_root_table(b, intrin, draw.geometry_params);

   case nir_intrinsic_load_vs_output_buffer_agx:
      return lower_sysval_to_root_table(b, intrin, draw.vertex_output_buffer);

   case nir_intrinsic_load_vs_outputs_agx:
      return lower_sysval_to_root_table(b, intrin, draw.vertex_outputs);

   case nir_intrinsic_load_tess_param_buffer_agx:
      return lower_sysval_to_root_table(b, intrin, draw.tess_params);

   case nir_intrinsic_load_is_first_fan_agx: {
      unsigned offset = hk_root_descriptor_offset(draw.provoking);
      b->cursor = nir_instr_remove(&intrin->instr);
      nir_def *val = load_root(b, 1, 16, nir_imm_int(b, offset), 2);
      nir_def_rewrite_uses(&intrin->def, nir_ieq_imm(b, val, 1));
      return true;
   }

   case nir_intrinsic_load_provoking_last: {
      unsigned offset = hk_root_descriptor_offset(draw.provoking);
      b->cursor = nir_instr_remove(&intrin->instr);
      nir_def *val = load_root(b, 1, 16, nir_imm_int(b, offset), 2);
      nir_def_rewrite_uses(&intrin->def, nir_b2b32(b, nir_ieq_imm(b, val, 2)));
      return true;
   }

   case nir_intrinsic_load_base_vertex:
   case nir_intrinsic_load_first_vertex:
   case nir_intrinsic_load_base_instance:
   case nir_intrinsic_load_draw_id:
   case nir_intrinsic_load_input_assembly_buffer_agx: {
      b->cursor = nir_instr_remove(&intrin->instr);

      unsigned base = *vs_uniform_base;
      unsigned size = 32;

      if (intrin->intrinsic == nir_intrinsic_load_base_instance) {
         base += 2;
      } else if (intrin->intrinsic == nir_intrinsic_load_draw_id) {
         base += 4;
         size = 16;
      } else if (intrin->intrinsic ==
                 nir_intrinsic_load_input_assembly_buffer_agx) {
         base += 8;
         size = 64;
      }

      nir_def *val = nir_load_preamble(b, 1, size, .base = base);
      nir_def_rewrite_uses(&intrin->def,
                           nir_u2uN(b, val, intrin->def.bit_size));
      return true;
   }

   case nir_intrinsic_load_stat_query_address_agx: {
      b->cursor = nir_instr_remove(&intrin->instr);

      unsigned off1 = hk_root_descriptor_offset(draw.pipeline_stats);
      unsigned off2 = hk_root_descriptor_offset(draw.pipeline_stats_flags);

      nir_def *base = load_root(b, 1, 64, nir_imm_int(b, off1), 8);
      nir_def *flags = load_root(b, 1, 16, nir_imm_int(b, off2), 2);

      unsigned query = nir_intrinsic_base(intrin);
      VkQueryPipelineStatisticFlagBits bit = translate_pipeline_stat_bit(query);

      /* Prefix sum to find the compacted offset */
      nir_def *idx = nir_bit_count(b, nir_iand_imm(b, flags, bit - 1));
      nir_def *addr = nir_iadd(
         b, base, nir_imul_imm(b, nir_u2u64(b, idx), sizeof(uint64_t)));

      /* The above returns garbage if the query isn't actually enabled, handle
       * that case.
       *
       * TODO: Optimize case where we *know* the query is present?
       */
      nir_def *present = nir_ine_imm(b, nir_iand_imm(b, flags, bit), 0);

      /* Sometimes we insert a GS internally, it should not contribute to GS
       * statistics. This is not strictly needed for Vulkan but vkd3d-proton
       * tests it and we should avoid the surprising behaviour.
       */
      if (query == PIPE_STAT_QUERY_GS_INVOCATIONS ||
          query == PIPE_STAT_QUERY_GS_PRIMITIVES) {

         unsigned api_gs_offset = hk_root_descriptor_offset(draw.api_gs);
         nir_def *api_gs =
            load_root(b, 1, 16, nir_imm_int(b, api_gs_offset), 4);

         present = nir_iand(b, present, nir_ine_imm(b, api_gs, 0));
      }

      addr = nir_bcsel(b, present, addr, nir_imm_int64(b, 0));

      nir_def_rewrite_uses(&intrin->def, addr);
      return true;
   }

   default:
      return false;
   }
}

bool
hk_lower_uvs_index(nir_shader *s, unsigned vs_uniform_base)
{
   return nir_shader_intrinsics_pass(
      s, lower_uvs_index, nir_metadata_control_flow, &vs_uniform_base);
}

static bool
try_lower_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                 const struct lower_descriptors_ctx *ctx)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_constant:
      return lower_load_constant(b, intrin, ctx);

   case nir_intrinsic_load_vulkan_descriptor:
      return try_lower_load_vulkan_descriptor(b, intrin, ctx);

   case nir_intrinsic_load_workgroup_size:
      unreachable("Should have been lowered by nir_lower_cs_intrinsics()");

   case nir_intrinsic_load_base_workgroup_id:
      return lower_sysval_to_root_table(b, intrin, cs.base_group);

   case nir_intrinsic_load_push_constant:
      return lower_load_push_constant(b, intrin, ctx);

   case nir_intrinsic_load_view_index:
      return lower_sysval_to_root_table(b, intrin, draw.view_index);

   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_sparse_load:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_samples:
   case nir_intrinsic_image_deref_store_block_agx:
      return lower_image_intrin(b, intrin, ctx);

   case nir_intrinsic_load_num_workgroups: {
      b->cursor = nir_instr_remove(&intrin->instr);

      unsigned offset = hk_root_descriptor_offset(cs.group_count_addr);
      nir_def *ptr = load_root(b, 1, 64, nir_imm_int(b, offset), 4);
      nir_def *val = load_speculatable(b, 3, 32, ptr, 4);

      nir_def_rewrite_uses(&intrin->def, val);
      return true;
   }

   default:
      return false;
   }
}

static bool
lower_tex(nir_builder *b, nir_tex_instr *tex,
          const struct lower_descriptors_ctx *ctx)
{
   b->cursor = nir_before_instr(&tex->instr);

   nir_def *texture = nir_steal_tex_src(tex, nir_tex_src_texture_deref);
   nir_def *sampler = nir_steal_tex_src(tex, nir_tex_src_sampler_deref);
   if (!texture) {
      assert(!sampler);
      return false;
   }

   nir_def *plane_ssa = nir_steal_tex_src(tex, nir_tex_src_plane);
   const uint32_t plane =
      plane_ssa ? nir_src_as_uint(nir_src_for_ssa(plane_ssa)) : 0;
   const uint64_t plane_offset_B =
      plane * sizeof(struct hk_sampled_image_descriptor);

   /* LOD bias is passed in the descriptor set, rather than embedded into
    * the sampler descriptor. There's no spot in the hardware descriptor,
    * plus this saves on precious sampler heap spots.
    */
   if (tex->op == nir_texop_lod_bias_agx) {
      unsigned offs =
         offsetof(struct hk_sampled_image_descriptor, lod_bias_fp16);

      nir_def *bias = load_resource_deref_desc(
         b, 1, 16, nir_src_as_deref(nir_src_for_ssa(sampler)),
         plane_offset_B + offs, ctx);

      nir_def_replace(&tex->def, bias);
      return true;
   }

   if (tex->op == nir_texop_image_min_lod_agx) {
      assert(tex->dest_type == nir_type_float16 ||
             tex->dest_type == nir_type_uint16);

      unsigned offs =
         tex->dest_type == nir_type_float16
            ? offsetof(struct hk_sampled_image_descriptor, min_lod_fp16)
            : offsetof(struct hk_sampled_image_descriptor, min_lod_uint16);

      nir_def *min = load_resource_deref_desc(
         b, 1, 16, nir_src_as_deref(nir_src_for_ssa(texture)),
         plane_offset_B + offs, ctx);

      nir_def_replace(&tex->def, min);
      return true;
   }

   if (tex->op == nir_texop_has_custom_border_color_agx) {
      unsigned offs = offsetof(struct hk_sampled_image_descriptor,
                               clamp_0_sampler_index_or_negative);

      nir_def *res = load_resource_deref_desc(
         b, 1, 16, nir_src_as_deref(nir_src_for_ssa(sampler)),
         plane_offset_B + offs, ctx);

      nir_def_replace(&tex->def, nir_ige_imm(b, res, 0));
      return true;
   }

   if (tex->op == nir_texop_custom_border_color_agx) {
      unsigned offs = offsetof(struct hk_sampled_image_descriptor, border);

      nir_def *border = load_resource_deref_desc(
         b, 4, 32, nir_src_as_deref(nir_src_for_ssa(sampler)),
         plane_offset_B + offs, ctx);

      nir_alu_type T = nir_alu_type_get_base_type(tex->dest_type);
      border = nir_convert_to_bit_size(b, border, T, tex->def.bit_size);

      nir_def_replace(&tex->def, border);
      return true;
   }

   {
      unsigned offs =
         offsetof(struct hk_sampled_image_descriptor, image_offset);

      nir_def *offset = load_resource_deref_desc(
         b, 1, 32, nir_src_as_deref(nir_src_for_ssa(texture)),
         plane_offset_B + offs, ctx);

      nir_def *handle = image_heap_handle(b, offset);
      nir_tex_instr_add_src(tex, nir_tex_src_texture_handle, handle);
   }

   if (sampler != NULL) {
      unsigned offs =
         offsetof(struct hk_sampled_image_descriptor, sampler_index);

      if (tex->backend_flags & AGX_TEXTURE_FLAG_CLAMP_TO_0) {
         offs = offsetof(struct hk_sampled_image_descriptor,
                         clamp_0_sampler_index_or_negative);
      }

      nir_def *index = load_resource_deref_desc(
         b, 1, 16, nir_src_as_deref(nir_src_for_ssa(sampler)),
         plane_offset_B + offs, ctx);

      nir_tex_instr_add_src(tex, nir_tex_src_sampler_handle, index);
   }

   return true;
}

static bool
try_lower_descriptors_instr(nir_builder *b, nir_instr *instr, void *_data)
{
   const struct lower_descriptors_ctx *ctx = _data;

   switch (instr->type) {
   case nir_instr_type_tex:
      return lower_tex(b, nir_instr_as_tex(instr), ctx);
   case nir_instr_type_intrinsic:
      return try_lower_intrin(b, nir_instr_as_intrinsic(instr), ctx);
   default:
      return false;
   }
}

static bool
lower_ssbo_resource_index(nir_builder *b, nir_intrinsic_instr *intrin,
                          const struct lower_descriptors_ctx *ctx)
{
   const VkDescriptorType desc_type = nir_intrinsic_desc_type(intrin);
   if (desc_type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
       desc_type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
      return false;

   b->cursor = nir_instr_remove(&intrin->instr);

   uint32_t set = nir_intrinsic_desc_set(intrin);
   uint32_t binding = nir_intrinsic_binding(intrin);
   nir_def *index = intrin->src[0].ssa;

   const struct hk_descriptor_set_binding_layout *binding_layout =
      get_binding_layout(set, binding, ctx);

   nir_def *binding_addr;
   uint8_t binding_stride;
   switch (binding_layout->type) {
   case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
      nir_def *set_addr = load_descriptor_set_addr(b, set, ctx);
      binding_addr = nir_iadd_imm(b, set_addr, binding_layout->offset);
      binding_stride = binding_layout->stride;
      break;
   }

   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
      const uint32_t root_desc_addr_offset =
         hk_root_descriptor_offset(root_desc_addr);

      nir_def *root_desc_addr =
         load_root(b, 1, 64, nir_imm_int(b, root_desc_addr_offset), 8);

      nir_def *dynamic_buffer_start =
         nir_iadd_imm(b, load_dynamic_buffer_start(b, set, ctx),
                      binding_layout->dynamic_buffer_index);

      nir_def *dynamic_binding_offset =
         nir_iadd_imm(b,
                      nir_imul_imm(b, dynamic_buffer_start,
                                   sizeof(struct hk_buffer_address)),
                      hk_root_descriptor_offset(dynamic_buffers));

      binding_addr =
         nir_iadd(b, root_desc_addr, nir_u2u64(b, dynamic_binding_offset));
      binding_stride = sizeof(struct hk_buffer_address);
      break;
   }

   default:
      unreachable("Not an SSBO descriptor");
   }

   /* Tuck the stride in the top 8 bits of the binding address */
   binding_addr = nir_ior_imm(b, binding_addr, (uint64_t)binding_stride << 56);

   const uint32_t binding_size = binding_layout->array_size * binding_stride;
   nir_def *offset_in_binding = nir_imul_imm(b, index, binding_stride);

   nir_def *addr = nir_vec4(b, nir_unpack_64_2x32_split_x(b, binding_addr),
                            nir_unpack_64_2x32_split_y(b, binding_addr),
                            nir_imm_int(b, binding_size), offset_in_binding);

   nir_def_rewrite_uses(&intrin->def, addr);

   return true;
}

static bool
lower_ssbo_resource_reindex(nir_builder *b, nir_intrinsic_instr *intrin,
                            const struct lower_descriptors_ctx *ctx)
{
   const VkDescriptorType desc_type = nir_intrinsic_desc_type(intrin);
   if (desc_type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
       desc_type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
      return false;

   b->cursor = nir_instr_remove(&intrin->instr);

   nir_def *addr = intrin->src[0].ssa;
   nir_def *index = intrin->src[1].ssa;

   nir_def *addr_high32 = nir_channel(b, addr, 1);
   nir_def *stride = nir_ushr_imm(b, addr_high32, 24);
   nir_def *offset = nir_imul(b, index, stride);

   addr = nir_build_addr_iadd(b, addr, ctx->ssbo_addr_format, nir_var_mem_ssbo,
                              offset);
   nir_def_rewrite_uses(&intrin->def, addr);

   return true;
}

static bool
lower_load_ssbo_descriptor(nir_builder *b, nir_intrinsic_instr *intrin,
                           const struct lower_descriptors_ctx *ctx)
{
   const VkDescriptorType desc_type = nir_intrinsic_desc_type(intrin);
   if (desc_type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
       desc_type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
      return false;

   b->cursor = nir_instr_remove(&intrin->instr);

   nir_def *addr = intrin->src[0].ssa;

   nir_def *desc;
   switch (ctx->ssbo_addr_format) {
   case nir_address_format_64bit_global_32bit_offset: {
      nir_def *base = nir_pack_64_2x32(b, nir_trim_vector(b, addr, 2));
      nir_def *offset = nir_channel(b, addr, 3);
      /* Mask off the binding stride */
      base = nir_iand_imm(b, base, BITFIELD64_MASK(56));
      desc = nir_load_global_constant_offset(b, 4, 32, base, offset,
                                             .align_mul = 16, .align_offset = 0,
                                             .access = ACCESS_CAN_SPECULATE);
      break;
   }

   case nir_address_format_64bit_bounded_global: {
      nir_def *base = nir_pack_64_2x32(b, nir_trim_vector(b, addr, 2));
      nir_def *size = nir_channel(b, addr, 2);
      nir_def *offset = nir_channel(b, addr, 3);
      /* Mask off the binding stride */
      base = nir_iand_imm(b, base, BITFIELD64_MASK(56));
      desc = nir_load_global_constant_bounded(
         b, 4, 32, base, offset, size, .align_mul = 16, .align_offset = 0,
         .access = ACCESS_CAN_SPECULATE);
      break;
   }

   default:
      unreachable("Unknown address mode");
   }

   nir_def_rewrite_uses(&intrin->def, desc);

   return true;
}

static bool
lower_ssbo_descriptor(nir_builder *b, nir_intrinsic_instr *intr, void *_data)
{
   const struct lower_descriptors_ctx *ctx = _data;

   switch (intr->intrinsic) {
   case nir_intrinsic_vulkan_resource_index:
      return lower_ssbo_resource_index(b, intr, ctx);
   case nir_intrinsic_vulkan_resource_reindex:
      return lower_ssbo_resource_reindex(b, intr, ctx);
   case nir_intrinsic_load_vulkan_descriptor:
      return lower_load_ssbo_descriptor(b, intr, ctx);
   default:
      return false;
   }
}

bool
hk_nir_lower_descriptors(nir_shader *nir,
                         const struct vk_pipeline_robustness_state *rs,
                         uint32_t set_layout_count,
                         struct vk_descriptor_set_layout *const *set_layouts)
{
   struct lower_descriptors_ctx ctx = {
      .clamp_desc_array_bounds =
         rs->storage_buffers !=
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT ||

         rs->uniform_buffers !=
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT ||

         rs->images != VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED_EXT,

      .ssbo_addr_format = hk_buffer_addr_format(rs->storage_buffers),
      .ubo_addr_format = hk_buffer_addr_format(rs->uniform_buffers),
   };

   assert(set_layout_count <= HK_MAX_SETS);
   for (uint32_t s = 0; s < set_layout_count; s++) {
      if (set_layouts[s] != NULL)
         ctx.set_layouts[s] = vk_to_hk_descriptor_set_layout(set_layouts[s]);
   }

   /* First lower everything but complex SSBOs, then lower complex SSBOs.
    *
    * TODO: See if we can unify this, not sure if the fast path matters on
    * Apple. This is inherited from NVK.
    */
   bool pass_lower_descriptors = nir_shader_instructions_pass(
      nir, try_lower_descriptors_instr, nir_metadata_control_flow, &ctx);

   bool pass_lower_ssbo = nir_shader_intrinsics_pass(
      nir, lower_ssbo_descriptor, nir_metadata_control_flow, &ctx);

   return pass_lower_descriptors || pass_lower_ssbo;
}
