/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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
 */

#include "nir/nir.h"
#include "radv_debug.h"
#include "radv_private.h"
#include "radv_shader.h"
#include "radv_shader_args.h"
#include "radv_shader_helper.h"

#include "ac_binary.h"
#include "ac_exp_param.h"
#include "ac_llvm_build.h"
#include "ac_nir_to_llvm.h"
#include "ac_shader_abi.h"
#include "ac_shader_util.h"
#include "sid.h"

struct radv_shader_context {
   struct ac_llvm_context ac;
   const struct nir_shader *shader;
   struct ac_shader_abi abi;
   const struct radv_shader_args *args;

   gl_shader_stage stage;

   unsigned max_workgroup_size;
   LLVMContextRef context;
   LLVMValueRef main_function;

   LLVMValueRef descriptor_sets[MAX_SETS];

   LLVMValueRef ring_offsets;

   LLVMValueRef vs_rel_patch_id;

   LLVMValueRef gs_wave_id;
   LLVMValueRef gs_vtx_offset[6];

   LLVMValueRef esgs_ring;
   LLVMValueRef gsvs_ring[4];
   LLVMValueRef hs_ring_tess_offchip;
   LLVMValueRef hs_ring_tess_factor;

   uint64_t output_mask;

   LLVMValueRef gs_next_vertex[4];
   LLVMValueRef gs_curprim_verts[4];
   LLVMValueRef gs_generated_prims[4];
   LLVMValueRef gs_ngg_emit;
   LLVMValueRef gs_ngg_scratch;

   LLVMValueRef vertexptr; /* GFX10 only */
};

struct radv_shader_output_values {
   LLVMValueRef values[4];
   unsigned slot_name;
   unsigned slot_index;
   unsigned usage_mask;
};

static inline struct radv_shader_context *
radv_shader_context_from_abi(struct ac_shader_abi *abi)
{
   return container_of(abi, struct radv_shader_context, abi);
}

static LLVMValueRef
create_llvm_function(struct ac_llvm_context *ctx, LLVMModuleRef module, LLVMBuilderRef builder,
                     const struct ac_shader_args *args, enum ac_llvm_calling_convention convention,
                     unsigned max_workgroup_size, const struct radv_nir_compiler_options *options)
{
   LLVMValueRef main_function = ac_build_main(args, ctx, convention, "main", ctx->voidt, module);

   if (options->address32_hi) {
      ac_llvm_add_target_dep_function_attr(main_function, "amdgpu-32bit-address-high-bits",
                                           options->address32_hi);
   }

   ac_llvm_set_workgroup_size(main_function, max_workgroup_size);
   ac_llvm_set_target_features(main_function, ctx);

   return main_function;
}

static void
load_descriptor_sets(struct radv_shader_context *ctx)
{
   uint32_t mask = ctx->args->shader_info->desc_set_used_mask;
   if (ctx->args->shader_info->need_indirect_descriptor_sets) {
      LLVMValueRef desc_sets = ac_get_arg(&ctx->ac, ctx->args->descriptor_sets[0]);
      while (mask) {
         int i = u_bit_scan(&mask);

         ctx->descriptor_sets[i] =
            ac_build_load_to_sgpr(&ctx->ac, desc_sets, LLVMConstInt(ctx->ac.i32, i, false));
         LLVMSetAlignment(ctx->descriptor_sets[i], 4);
      }
   } else {
      while (mask) {
         int i = u_bit_scan(&mask);

         ctx->descriptor_sets[i] = ac_get_arg(&ctx->ac, ctx->args->descriptor_sets[i]);
      }
   }
}

static enum ac_llvm_calling_convention
get_llvm_calling_convention(LLVMValueRef func, gl_shader_stage stage)
{
   switch (stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_EVAL:
      return AC_LLVM_AMDGPU_VS;
      break;
   case MESA_SHADER_GEOMETRY:
      return AC_LLVM_AMDGPU_GS;
      break;
   case MESA_SHADER_TESS_CTRL:
      return AC_LLVM_AMDGPU_HS;
      break;
   case MESA_SHADER_FRAGMENT:
      return AC_LLVM_AMDGPU_PS;
      break;
   case MESA_SHADER_COMPUTE:
      return AC_LLVM_AMDGPU_CS;
      break;
   default:
      unreachable("Unhandle shader type");
   }
}

/* Returns whether the stage is a stage that can be directly before the GS */
static bool
is_pre_gs_stage(gl_shader_stage stage)
{
   return stage == MESA_SHADER_VERTEX || stage == MESA_SHADER_TESS_EVAL;
}

static void
create_function(struct radv_shader_context *ctx, gl_shader_stage stage, bool has_previous_stage)
{
   if (ctx->ac.chip_class >= GFX10) {
      if (is_pre_gs_stage(stage) && ctx->args->options->key.vs_common_out.as_ngg) {
         /* On GFX10, VS is merged into GS for NGG. */
         stage = MESA_SHADER_GEOMETRY;
         has_previous_stage = true;
      }
   }

   ctx->main_function =
      create_llvm_function(&ctx->ac, ctx->ac.module, ctx->ac.builder, &ctx->args->ac,
                           get_llvm_calling_convention(ctx->main_function, stage),
                           ctx->max_workgroup_size, ctx->args->options);

   ctx->ring_offsets = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.implicit.buffer.ptr",
                                          LLVMPointerType(ctx->ac.i8, AC_ADDR_SPACE_CONST), NULL, 0,
                                          AC_FUNC_ATTR_READNONE);
   ctx->ring_offsets = LLVMBuildBitCast(ctx->ac.builder, ctx->ring_offsets,
                                        ac_array_in_const_addr_space(ctx->ac.v4i32), "");

   load_descriptor_sets(ctx);

   if (stage == MESA_SHADER_TESS_CTRL ||
       (stage == MESA_SHADER_VERTEX && ctx->args->options->key.vs_common_out.as_ls) ||
       /* GFX9 has the ESGS ring buffer in LDS. */
       (stage == MESA_SHADER_GEOMETRY && has_previous_stage)) {
      ac_declare_lds_as_pointer(&ctx->ac);
   }
}

static LLVMValueRef
radv_load_resource(struct ac_shader_abi *abi, LLVMValueRef index, unsigned desc_set,
                   unsigned binding)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
   LLVMValueRef desc_ptr = ctx->descriptor_sets[desc_set];
   struct radv_pipeline_layout *pipeline_layout = ctx->args->options->layout;
   struct radv_descriptor_set_layout *layout = pipeline_layout->set[desc_set].layout;
   unsigned base_offset = layout->binding[binding].offset;
   LLVMValueRef offset, stride;

   if (layout->binding[binding].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
       layout->binding[binding].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
      unsigned idx = pipeline_layout->set[desc_set].dynamic_offset_start +
                     layout->binding[binding].dynamic_offset_offset;
      desc_ptr = ac_get_arg(&ctx->ac, ctx->args->ac.push_constants);
      base_offset = pipeline_layout->push_constant_size + 16 * idx;
      stride = LLVMConstInt(ctx->ac.i32, 16, false);
   } else
      stride = LLVMConstInt(ctx->ac.i32, layout->binding[binding].size, false);

   offset = LLVMConstInt(ctx->ac.i32, base_offset, false);

   if (layout->binding[binding].type != VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT) {
      offset = ac_build_imad(&ctx->ac, index, stride, offset);
   }

   desc_ptr = LLVMBuildPtrToInt(ctx->ac.builder, desc_ptr, ctx->ac.i32, "");

   LLVMValueRef res[] = {desc_ptr, offset, ctx->ac.i32_0};
   return ac_build_gather_values(&ctx->ac, res, 3);
}

static uint32_t
radv_get_sample_pos_offset(uint32_t num_samples)
{
   uint32_t sample_pos_offset = 0;

   switch (num_samples) {
   case 2:
      sample_pos_offset = 1;
      break;
   case 4:
      sample_pos_offset = 3;
      break;
   case 8:
      sample_pos_offset = 7;
      break;
   default:
      break;
   }
   return sample_pos_offset;
}

static LLVMValueRef
load_sample_position(struct ac_shader_abi *abi, LLVMValueRef sample_id)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);

   LLVMValueRef result;
   LLVMValueRef index = LLVMConstInt(ctx->ac.i32, RING_PS_SAMPLE_POSITIONS, false);
   LLVMValueRef ptr = LLVMBuildGEP(ctx->ac.builder, ctx->ring_offsets, &index, 1, "");

   ptr = LLVMBuildBitCast(ctx->ac.builder, ptr, ac_array_in_const_addr_space(ctx->ac.v2f32), "");

   uint32_t sample_pos_offset = radv_get_sample_pos_offset(ctx->args->options->key.fs.num_samples);

   sample_id = LLVMBuildAdd(ctx->ac.builder, sample_id,
                            LLVMConstInt(ctx->ac.i32, sample_pos_offset, false), "");
   result = ac_build_load_invariant(&ctx->ac, ptr, sample_id);

   return result;
}

static LLVMValueRef
load_sample_mask_in(struct ac_shader_abi *abi)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
   uint8_t log2_ps_iter_samples;

   if (ctx->args->shader_info->ps.uses_sample_shading) {
      log2_ps_iter_samples = util_logbase2(ctx->args->options->key.fs.num_samples);
   } else {
      log2_ps_iter_samples = ctx->args->options->key.fs.log2_ps_iter_samples;
   }

   LLVMValueRef result, sample_id;
   if (log2_ps_iter_samples) {
      /* gl_SampleMaskIn[0] = (SampleCoverage & (1 << gl_SampleID)). */
      sample_id = ac_unpack_param(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.ancillary), 8, 4);
      sample_id = LLVMBuildShl(ctx->ac.builder, LLVMConstInt(ctx->ac.i32, 1, false), sample_id, "");
      result = LLVMBuildAnd(ctx->ac.builder, sample_id,
                            ac_get_arg(&ctx->ac, ctx->args->ac.sample_coverage), "");
   } else {
      result = ac_get_arg(&ctx->ac, ctx->args->ac.sample_coverage);
   }

   return result;
}

static void gfx10_ngg_gs_emit_vertex(struct radv_shader_context *ctx, unsigned stream,
                                     LLVMValueRef vertexidx, LLVMValueRef *addrs);

static void
visit_emit_vertex_with_counter(struct ac_shader_abi *abi, unsigned stream, LLVMValueRef vertexidx,
                               LLVMValueRef *addrs)
{
   unsigned offset = 0;
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);

   if (ctx->args->options->key.vs_common_out.as_ngg) {
      gfx10_ngg_gs_emit_vertex(ctx, stream, vertexidx, addrs);
      return;
   }

   for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
      unsigned output_usage_mask = ctx->args->shader_info->gs.output_usage_mask[i];
      uint8_t output_stream = ctx->args->shader_info->gs.output_streams[i];
      LLVMValueRef *out_ptr = &addrs[i * 4];
      int length = util_last_bit(output_usage_mask);

      if (!(ctx->output_mask & (1ull << i)) || output_stream != stream)
         continue;

      for (unsigned j = 0; j < length; j++) {
         if (!(output_usage_mask & (1 << j)))
            continue;

         LLVMValueRef out_val = LLVMBuildLoad(ctx->ac.builder, out_ptr[j], "");
         LLVMValueRef voffset =
            LLVMConstInt(ctx->ac.i32, offset * ctx->shader->info.gs.vertices_out, false);

         offset++;

         voffset = LLVMBuildAdd(ctx->ac.builder, voffset, vertexidx, "");
         voffset = LLVMBuildMul(ctx->ac.builder, voffset, LLVMConstInt(ctx->ac.i32, 4, false), "");

         out_val = ac_to_integer(&ctx->ac, out_val);
         out_val = LLVMBuildZExtOrBitCast(ctx->ac.builder, out_val, ctx->ac.i32, "");

         ac_build_buffer_store_dword(&ctx->ac, ctx->gsvs_ring[stream], out_val, 1, voffset,
                                     ac_get_arg(&ctx->ac, ctx->args->ac.gs2vs_offset), 0,
                                     ac_glc | ac_slc | ac_swizzled);
      }
   }

   ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_EMIT | AC_SENDMSG_GS | (stream << 8),
                    ctx->gs_wave_id);
}

static void
visit_end_primitive(struct ac_shader_abi *abi, unsigned stream)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);

   if (ctx->args->options->key.vs_common_out.as_ngg) {
      LLVMBuildStore(ctx->ac.builder, ctx->ac.i32_0, ctx->gs_curprim_verts[stream]);
      return;
   }

   ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_CUT | AC_SENDMSG_GS | (stream << 8),
                    ctx->gs_wave_id);
}

static LLVMValueRef
load_ring_tess_factors(struct ac_shader_abi *abi)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
   assert(ctx->stage == MESA_SHADER_TESS_CTRL);

   return ctx->hs_ring_tess_factor;
}

static LLVMValueRef
load_ring_tess_offchip(struct ac_shader_abi *abi)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
   assert(ctx->stage == MESA_SHADER_TESS_CTRL || ctx->stage == MESA_SHADER_TESS_EVAL);

   return ctx->hs_ring_tess_offchip;
}

static LLVMValueRef
load_ring_esgs(struct ac_shader_abi *abi)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
   assert(ctx->stage == MESA_SHADER_VERTEX || ctx->stage == MESA_SHADER_TESS_EVAL ||
          ctx->stage == MESA_SHADER_GEOMETRY);

   return ctx->esgs_ring;
}

static LLVMValueRef
radv_load_base_vertex(struct ac_shader_abi *abi, bool non_indexed_is_zero)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
   return ac_get_arg(&ctx->ac, ctx->args->ac.base_vertex);
}

static LLVMValueRef
get_desc_ptr(struct radv_shader_context *ctx, LLVMValueRef ptr, bool non_uniform)
{
   LLVMValueRef set_ptr = ac_llvm_extract_elem(&ctx->ac, ptr, 0);
   LLVMValueRef offset = ac_llvm_extract_elem(&ctx->ac, ptr, 1);
   ptr = LLVMBuildNUWAdd(ctx->ac.builder, set_ptr, offset, "");

   unsigned addr_space = AC_ADDR_SPACE_CONST_32BIT;
   if (non_uniform) {
      /* 32-bit seems to always use SMEM. addrspacecast from 32-bit -> 64-bit is broken. */
      LLVMValueRef dwords[] = {ptr,
                               LLVMConstInt(ctx->ac.i32, ctx->args->options->address32_hi, false)};
      ptr = ac_build_gather_values(&ctx->ac, dwords, 2);
      ptr = LLVMBuildBitCast(ctx->ac.builder, ptr, ctx->ac.i64, "");
      addr_space = AC_ADDR_SPACE_CONST;
   }
   return LLVMBuildIntToPtr(ctx->ac.builder, ptr, LLVMPointerType(ctx->ac.v4i32, addr_space), "");
}

static LLVMValueRef
radv_load_ssbo(struct ac_shader_abi *abi, LLVMValueRef buffer_ptr, bool write, bool non_uniform)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
   LLVMValueRef result;

   buffer_ptr = get_desc_ptr(ctx, buffer_ptr, non_uniform);
   if (!non_uniform)
      LLVMSetMetadata(buffer_ptr, ctx->ac.uniform_md_kind, ctx->ac.empty_md);

   result = LLVMBuildLoad(ctx->ac.builder, buffer_ptr, "");
   LLVMSetMetadata(result, ctx->ac.invariant_load_md_kind, ctx->ac.empty_md);
   LLVMSetAlignment(result, 4);

   return result;
}

static LLVMValueRef
radv_load_ubo(struct ac_shader_abi *abi, unsigned desc_set, unsigned binding, bool valid_binding,
              LLVMValueRef buffer_ptr)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
   LLVMValueRef result;

   if (valid_binding) {
      struct radv_pipeline_layout *pipeline_layout = ctx->args->options->layout;
      struct radv_descriptor_set_layout *layout = pipeline_layout->set[desc_set].layout;

      if (layout->binding[binding].type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT) {
         LLVMValueRef set_ptr = ac_llvm_extract_elem(&ctx->ac, buffer_ptr, 0);
         LLVMValueRef offset = ac_llvm_extract_elem(&ctx->ac, buffer_ptr, 1);
         buffer_ptr = LLVMBuildNUWAdd(ctx->ac.builder, set_ptr, offset, "");

         uint32_t desc_type =
            S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
            S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

         if (ctx->ac.chip_class >= GFX10) {
            desc_type |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                         S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW) | S_008F0C_RESOURCE_LEVEL(1);
         } else {
            desc_type |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                         S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
         }

         LLVMValueRef desc_components[4] = {
            LLVMBuildPtrToInt(ctx->ac.builder, buffer_ptr, ctx->ac.intptr, ""),
            LLVMConstInt(ctx->ac.i32, S_008F04_BASE_ADDRESS_HI(ctx->args->options->address32_hi),
                         false),
            LLVMConstInt(ctx->ac.i32, 0xffffffff, false),
            LLVMConstInt(ctx->ac.i32, desc_type, false),
         };

         return ac_build_gather_values(&ctx->ac, desc_components, 4);
      }
   }

   buffer_ptr = get_desc_ptr(ctx, buffer_ptr, false);
   LLVMSetMetadata(buffer_ptr, ctx->ac.uniform_md_kind, ctx->ac.empty_md);

   result = LLVMBuildLoad(ctx->ac.builder, buffer_ptr, "");
   LLVMSetMetadata(result, ctx->ac.invariant_load_md_kind, ctx->ac.empty_md);
   LLVMSetAlignment(result, 4);

   return result;
}

static LLVMValueRef
radv_get_sampler_desc(struct ac_shader_abi *abi, unsigned descriptor_set, unsigned base_index,
                      unsigned constant_index, LLVMValueRef index,
                      enum ac_descriptor_type desc_type, bool image, bool write, bool bindless)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
   LLVMValueRef list = ctx->descriptor_sets[descriptor_set];
   struct radv_descriptor_set_layout *layout =
      ctx->args->options->layout->set[descriptor_set].layout;
   struct radv_descriptor_set_binding_layout *binding = layout->binding + base_index;
   unsigned offset = binding->offset;
   unsigned stride = binding->size;
   unsigned type_size;
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMTypeRef type;

   assert(base_index < layout->binding_count);

   switch (desc_type) {
   case AC_DESC_IMAGE:
      type = ctx->ac.v8i32;
      type_size = 32;
      break;
   case AC_DESC_FMASK:
      type = ctx->ac.v8i32;
      offset += 32;
      type_size = 32;
      break;
   case AC_DESC_SAMPLER:
      type = ctx->ac.v4i32;
      if (binding->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
         offset += radv_combined_image_descriptor_sampler_offset(binding);
      }

      type_size = 16;
      break;
   case AC_DESC_BUFFER:
      type = ctx->ac.v4i32;
      type_size = 16;
      break;
   case AC_DESC_PLANE_0:
   case AC_DESC_PLANE_1:
   case AC_DESC_PLANE_2:
      type = ctx->ac.v8i32;
      type_size = 32;
      offset += 32 * (desc_type - AC_DESC_PLANE_0);
      break;
   default:
      unreachable("invalid desc_type\n");
   }

   offset += constant_index * stride;

   if (desc_type == AC_DESC_SAMPLER && binding->immutable_samplers_offset &&
       (!index || binding->immutable_samplers_equal)) {
      if (binding->immutable_samplers_equal)
         constant_index = 0;

      const uint32_t *samplers = radv_immutable_samplers(layout, binding);

      LLVMValueRef constants[] = {
         LLVMConstInt(ctx->ac.i32, samplers[constant_index * 4 + 0], 0),
         LLVMConstInt(ctx->ac.i32, samplers[constant_index * 4 + 1], 0),
         LLVMConstInt(ctx->ac.i32, samplers[constant_index * 4 + 2], 0),
         LLVMConstInt(ctx->ac.i32, samplers[constant_index * 4 + 3], 0),
      };
      return ac_build_gather_values(&ctx->ac, constants, 4);
   }

   assert(stride % type_size == 0);

   LLVMValueRef adjusted_index = index;
   if (!adjusted_index)
      adjusted_index = ctx->ac.i32_0;

   adjusted_index =
      LLVMBuildMul(builder, adjusted_index, LLVMConstInt(ctx->ac.i32, stride / type_size, 0), "");

   LLVMValueRef val_offset = LLVMConstInt(ctx->ac.i32, offset, 0);
   list = LLVMBuildGEP(builder, list, &val_offset, 1, "");
   list = LLVMBuildPointerCast(builder, list, ac_array_in_const32_addr_space(type), "");

   LLVMValueRef descriptor = ac_build_load_to_sgpr(&ctx->ac, list, adjusted_index);

   /* 3 plane formats always have same size and format for plane 1 & 2, so
    * use the tail from plane 1 so that we can store only the first 16 bytes
    * of the last plane. */
   if (desc_type == AC_DESC_PLANE_2) {
      LLVMValueRef descriptor2 =
         radv_get_sampler_desc(abi, descriptor_set, base_index, constant_index, index,
                               AC_DESC_PLANE_1, image, write, bindless);

      LLVMValueRef components[8];
      for (unsigned i = 0; i < 4; ++i)
         components[i] = ac_llvm_extract_elem(&ctx->ac, descriptor, i);

      for (unsigned i = 4; i < 8; ++i)
         components[i] = ac_llvm_extract_elem(&ctx->ac, descriptor2, i);
      descriptor = ac_build_gather_values(&ctx->ac, components, 8);
   } else if (desc_type == AC_DESC_IMAGE &&
              ctx->args->options->has_image_load_dcc_bug &&
              image && !write) {
      LLVMValueRef components[8];

      for (unsigned i = 0; i < 8; i++)
         components[i] = ac_llvm_extract_elem(&ctx->ac, descriptor, i);

      /* WRITE_COMPRESS_ENABLE must be 0 for all image loads to workaround a hardware bug. */
      components[6] = LLVMBuildAnd(ctx->ac.builder, components[6],
                                   LLVMConstInt(ctx->ac.i32, C_00A018_WRITE_COMPRESS_ENABLE, false), "");

      descriptor = ac_build_gather_values(&ctx->ac, components, 8);
   }

   return descriptor;
}

/* For 2_10_10_10 formats the alpha is handled as unsigned by pre-vega HW.
 * so we may need to fix it up. */
static LLVMValueRef
adjust_vertex_fetch_alpha(struct radv_shader_context *ctx, unsigned adjustment, LLVMValueRef alpha)
{
   if (adjustment == AC_FETCH_FORMAT_NONE)
      return alpha;

   LLVMValueRef c30 = LLVMConstInt(ctx->ac.i32, 30, 0);

   alpha = LLVMBuildBitCast(ctx->ac.builder, alpha, ctx->ac.f32, "");

   if (adjustment == AC_FETCH_FORMAT_SSCALED)
      alpha = LLVMBuildFPToUI(ctx->ac.builder, alpha, ctx->ac.i32, "");
   else
      alpha = ac_to_integer(&ctx->ac, alpha);

   /* For the integer-like cases, do a natural sign extension.
    *
    * For the SNORM case, the values are 0.0, 0.333, 0.666, 1.0
    * and happen to contain 0, 1, 2, 3 as the two LSBs of the
    * exponent.
    */
   alpha =
      LLVMBuildShl(ctx->ac.builder, alpha,
                   adjustment == AC_FETCH_FORMAT_SNORM ? LLVMConstInt(ctx->ac.i32, 7, 0) : c30, "");
   alpha = LLVMBuildAShr(ctx->ac.builder, alpha, c30, "");

   /* Convert back to the right type. */
   if (adjustment == AC_FETCH_FORMAT_SNORM) {
      LLVMValueRef clamp;
      LLVMValueRef neg_one = LLVMConstReal(ctx->ac.f32, -1.0);
      alpha = LLVMBuildSIToFP(ctx->ac.builder, alpha, ctx->ac.f32, "");
      clamp = LLVMBuildFCmp(ctx->ac.builder, LLVMRealULT, alpha, neg_one, "");
      alpha = LLVMBuildSelect(ctx->ac.builder, clamp, neg_one, alpha, "");
   } else if (adjustment == AC_FETCH_FORMAT_SSCALED) {
      alpha = LLVMBuildSIToFP(ctx->ac.builder, alpha, ctx->ac.f32, "");
   }

   return LLVMBuildBitCast(ctx->ac.builder, alpha, ctx->ac.i32, "");
}

static LLVMValueRef
radv_fixup_vertex_input_fetches(struct radv_shader_context *ctx, LLVMValueRef value,
                                unsigned num_channels, bool is_float)
{
   LLVMValueRef zero = is_float ? ctx->ac.f32_0 : ctx->ac.i32_0;
   LLVMValueRef one = is_float ? ctx->ac.f32_1 : ctx->ac.i32_1;
   LLVMValueRef chan[4];

   if (LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMVectorTypeKind) {
      unsigned vec_size = LLVMGetVectorSize(LLVMTypeOf(value));

      if (num_channels == 4 && num_channels == vec_size)
         return value;

      num_channels = MIN2(num_channels, vec_size);

      for (unsigned i = 0; i < num_channels; i++)
         chan[i] = ac_llvm_extract_elem(&ctx->ac, value, i);
   } else {
      assert(num_channels == 1);
      chan[0] = value;
   }

   for (unsigned i = num_channels; i < 4; i++) {
      chan[i] = i == 3 ? one : zero;
      chan[i] = ac_to_integer(&ctx->ac, chan[i]);
   }

   return ac_build_gather_values(&ctx->ac, chan, 4);
}

static void
load_vs_input(struct radv_shader_context *ctx, unsigned driver_location, LLVMTypeRef dest_type,
              LLVMValueRef out[4])
{
   LLVMValueRef t_list_ptr = ac_get_arg(&ctx->ac, ctx->args->ac.vertex_buffers);
   LLVMValueRef t_offset;
   LLVMValueRef t_list;
   LLVMValueRef input;
   LLVMValueRef buffer_index;
   unsigned attrib_index = driver_location - VERT_ATTRIB_GENERIC0;
   unsigned attrib_format = ctx->args->options->key.vs.vertex_attribute_formats[attrib_index];
   unsigned data_format = attrib_format & 0x0f;
   unsigned num_format = (attrib_format >> 4) & 0x07;
   bool is_float =
      num_format != V_008F0C_BUF_NUM_FORMAT_UINT && num_format != V_008F0C_BUF_NUM_FORMAT_SINT;
   uint8_t input_usage_mask =
      ctx->args->shader_info->vs.input_usage_mask[driver_location];
   unsigned num_input_channels = util_last_bit(input_usage_mask);

   if (ctx->args->options->key.vs.instance_rate_inputs & (1u << attrib_index)) {
      uint32_t divisor = ctx->args->options->key.vs.instance_rate_divisors[attrib_index];

      if (divisor) {
         buffer_index = ctx->abi.instance_id;

         if (divisor != 1) {
            buffer_index = LLVMBuildUDiv(ctx->ac.builder, buffer_index,
                                         LLVMConstInt(ctx->ac.i32, divisor, 0), "");
         }
      } else {
         buffer_index = ctx->ac.i32_0;
      }

      buffer_index = LLVMBuildAdd(
         ctx->ac.builder, ac_get_arg(&ctx->ac, ctx->args->ac.start_instance), buffer_index, "");
   } else {
      buffer_index = LLVMBuildAdd(ctx->ac.builder, ctx->abi.vertex_id,
                                  ac_get_arg(&ctx->ac, ctx->args->ac.base_vertex), "");
   }

   const struct ac_data_format_info *vtx_info = ac_get_data_format_info(data_format);

   /* Adjust the number of channels to load based on the vertex attribute format. */
   unsigned num_channels = MIN2(num_input_channels, vtx_info->num_channels);
   unsigned attrib_binding = ctx->args->options->key.vs.vertex_attribute_bindings[attrib_index];
   unsigned attrib_offset = ctx->args->options->key.vs.vertex_attribute_offsets[attrib_index];
   unsigned attrib_stride = ctx->args->options->key.vs.vertex_attribute_strides[attrib_index];
   unsigned alpha_adjust = ctx->args->options->key.vs.alpha_adjust[attrib_index];

   if (ctx->args->options->key.vs.post_shuffle & (1 << attrib_index)) {
      /* Always load, at least, 3 channels for formats that need to be shuffled because X<->Z. */
      num_channels = MAX2(num_channels, 3);
   }

   unsigned desc_index =
      ctx->args->shader_info->vs.use_per_attribute_vb_descs ? attrib_index : attrib_binding;
   desc_index = util_bitcount(ctx->args->shader_info->vs.vb_desc_usage_mask &
                              u_bit_consecutive(0, desc_index));
   t_offset = LLVMConstInt(ctx->ac.i32, desc_index, false);
   t_list = ac_build_load_to_sgpr(&ctx->ac, t_list_ptr, t_offset);

   /* Always split typed vertex buffer loads on GFX6 and GFX10+ to avoid any alignment issues that
    * triggers memory violations and eventually a GPU hang. This can happen if the stride (static or
    * dynamic) is unaligned and also if the VBO offset is aligned to a scalar (eg. stride is 8 and
    * VBO offset is 2 for R16G16B16A16_SNORM).
    */
   if (ctx->ac.chip_class == GFX6 || ctx->ac.chip_class >= GFX10) {
      unsigned chan_format = vtx_info->chan_format;
      LLVMValueRef values[4];

      assert(ctx->ac.chip_class == GFX6 || ctx->ac.chip_class >= GFX10);

      for (unsigned chan = 0; chan < num_channels; chan++) {
         unsigned chan_offset = attrib_offset + chan * vtx_info->chan_byte_size;
         LLVMValueRef chan_index = buffer_index;

         if (attrib_stride != 0 && chan_offset > attrib_stride) {
            LLVMValueRef buffer_offset =
               LLVMConstInt(ctx->ac.i32, chan_offset / attrib_stride, false);

            chan_index = LLVMBuildAdd(ctx->ac.builder, buffer_index, buffer_offset, "");

            chan_offset = chan_offset % attrib_stride;
         }

         values[chan] = ac_build_struct_tbuffer_load(
            &ctx->ac, t_list, chan_index, LLVMConstInt(ctx->ac.i32, chan_offset, false),
            ctx->ac.i32_0, ctx->ac.i32_0, 1, chan_format, num_format, 0, true);
      }

      input = ac_build_gather_values(&ctx->ac, values, num_channels);
   } else {
      if (attrib_stride != 0 && attrib_offset > attrib_stride) {
         LLVMValueRef buffer_offset =
            LLVMConstInt(ctx->ac.i32, attrib_offset / attrib_stride, false);

         buffer_index = LLVMBuildAdd(ctx->ac.builder, buffer_index, buffer_offset, "");

         attrib_offset = attrib_offset % attrib_stride;
      }

      input = ac_build_struct_tbuffer_load(
         &ctx->ac, t_list, buffer_index, LLVMConstInt(ctx->ac.i32, attrib_offset, false),
         ctx->ac.i32_0, ctx->ac.i32_0, num_channels, data_format, num_format, 0, true);
   }

   if (ctx->args->options->key.vs.post_shuffle & (1 << attrib_index)) {
      LLVMValueRef c[4];
      c[0] = ac_llvm_extract_elem(&ctx->ac, input, 2);
      c[1] = ac_llvm_extract_elem(&ctx->ac, input, 1);
      c[2] = ac_llvm_extract_elem(&ctx->ac, input, 0);
      c[3] = ac_llvm_extract_elem(&ctx->ac, input, 3);

      input = ac_build_gather_values(&ctx->ac, c, 4);
   }

   input = radv_fixup_vertex_input_fetches(ctx, input, num_channels, is_float);

   for (unsigned chan = 0; chan < 4; chan++) {
      LLVMValueRef llvm_chan = LLVMConstInt(ctx->ac.i32, chan, false);
      out[chan] = LLVMBuildExtractElement(ctx->ac.builder, input, llvm_chan, "");
      if (dest_type == ctx->ac.i16 && is_float) {
         out[chan] = LLVMBuildBitCast(ctx->ac.builder, out[chan], ctx->ac.f32, "");
         out[chan] = LLVMBuildFPTrunc(ctx->ac.builder, out[chan], ctx->ac.f16, "");
      }
   }

   out[3] = adjust_vertex_fetch_alpha(ctx, alpha_adjust, out[3]);

   for (unsigned chan = 0; chan < 4; chan++) {
      out[chan] = ac_to_integer(&ctx->ac, out[chan]);
      if (dest_type == ctx->ac.i16 && !is_float)
         out[chan] = LLVMBuildTrunc(ctx->ac.builder, out[chan], ctx->ac.i16, "");
   }
}

static LLVMValueRef
radv_load_vs_inputs(struct ac_shader_abi *abi, unsigned driver_location, unsigned component,
                    unsigned num_components, unsigned vertex_index, LLVMTypeRef type)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
   LLVMValueRef values[4];

   load_vs_input(ctx, driver_location, type, values);

   for (unsigned i = 0; i < 4; i++)
      values[i] = LLVMBuildBitCast(ctx->ac.builder, values[i], type, "");

   return ac_build_varying_gather_values(&ctx->ac, values, num_components, component);
}

static void
prepare_interp_optimize(struct radv_shader_context *ctx, struct nir_shader *nir)
{
   bool uses_center = false;
   bool uses_centroid = false;
   nir_foreach_shader_in_variable (variable, nir) {
      if (glsl_get_base_type(glsl_without_array(variable->type)) != GLSL_TYPE_FLOAT ||
          variable->data.sample)
         continue;

      if (variable->data.centroid)
         uses_centroid = true;
      else
         uses_center = true;
   }

   ctx->abi.persp_centroid = ac_get_arg(&ctx->ac, ctx->args->ac.persp_centroid);
   ctx->abi.linear_centroid = ac_get_arg(&ctx->ac, ctx->args->ac.linear_centroid);

   if (uses_center && uses_centroid) {
      LLVMValueRef sel =
         LLVMBuildICmp(ctx->ac.builder, LLVMIntSLT, ac_get_arg(&ctx->ac, ctx->args->ac.prim_mask),
                       ctx->ac.i32_0, "");
      ctx->abi.persp_centroid =
         LLVMBuildSelect(ctx->ac.builder, sel, ac_get_arg(&ctx->ac, ctx->args->ac.persp_center),
                         ctx->abi.persp_centroid, "");
      ctx->abi.linear_centroid =
         LLVMBuildSelect(ctx->ac.builder, sel, ac_get_arg(&ctx->ac, ctx->args->ac.linear_center),
                         ctx->abi.linear_centroid, "");
   }
}

static void
scan_shader_output_decl(struct radv_shader_context *ctx, struct nir_variable *variable,
                        struct nir_shader *shader, gl_shader_stage stage)
{
   int idx = variable->data.driver_location;
   unsigned attrib_count = glsl_count_attribute_slots(variable->type, false);
   uint64_t mask_attribs;

   if (variable->data.compact) {
      unsigned component_count = variable->data.location_frac + glsl_get_length(variable->type);
      attrib_count = (component_count + 3) / 4;
   }

   mask_attribs = ((1ull << attrib_count) - 1) << idx;

   ctx->output_mask |= mask_attribs;
}

/* Initialize arguments for the shader export intrinsic */
static void
si_llvm_init_export_args(struct radv_shader_context *ctx, LLVMValueRef *values,
                         unsigned enabled_channels, unsigned target, struct ac_export_args *args)
{
   /* Specify the channels that are enabled. */
   args->enabled_channels = enabled_channels;

   /* Specify whether the EXEC mask represents the valid mask */
   args->valid_mask = 0;

   /* Specify whether this is the last export */
   args->done = 0;

   /* Specify the target we are exporting */
   args->target = target;

   args->compr = false;
   args->out[0] = LLVMGetUndef(ctx->ac.f32);
   args->out[1] = LLVMGetUndef(ctx->ac.f32);
   args->out[2] = LLVMGetUndef(ctx->ac.f32);
   args->out[3] = LLVMGetUndef(ctx->ac.f32);

   if (!values)
      return;

   bool is_16bit = ac_get_type_size(LLVMTypeOf(values[0])) == 2;
   if (ctx->stage == MESA_SHADER_FRAGMENT) {
      unsigned index = target - V_008DFC_SQ_EXP_MRT;
      unsigned col_format = (ctx->args->options->key.fs.col_format >> (4 * index)) & 0xf;
      bool is_int8 = (ctx->args->options->key.fs.is_int8 >> index) & 1;
      bool is_int10 = (ctx->args->options->key.fs.is_int10 >> index) & 1;

      LLVMValueRef (*packf)(struct ac_llvm_context * ctx, LLVMValueRef args[2]) = NULL;
      LLVMValueRef (*packi)(struct ac_llvm_context * ctx, LLVMValueRef args[2], unsigned bits,
                            bool hi) = NULL;

      switch (col_format) {
      case V_028714_SPI_SHADER_ZERO:
         args->enabled_channels = 0; /* writemask */
         args->target = V_008DFC_SQ_EXP_NULL;
         break;

      case V_028714_SPI_SHADER_32_R:
         args->enabled_channels = 1;
         args->out[0] = values[0];
         break;

      case V_028714_SPI_SHADER_32_GR:
         args->enabled_channels = 0x3;
         args->out[0] = values[0];
         args->out[1] = values[1];
         break;

      case V_028714_SPI_SHADER_32_AR:
         if (ctx->ac.chip_class >= GFX10) {
            args->enabled_channels = 0x3;
            args->out[0] = values[0];
            args->out[1] = values[3];
         } else {
            args->enabled_channels = 0x9;
            args->out[0] = values[0];
            args->out[3] = values[3];
         }
         break;

      case V_028714_SPI_SHADER_FP16_ABGR:
         args->enabled_channels = 0xf;
         packf = ac_build_cvt_pkrtz_f16;
         if (is_16bit) {
            for (unsigned chan = 0; chan < 4; chan++)
               values[chan] = LLVMBuildFPExt(ctx->ac.builder, values[chan], ctx->ac.f32, "");
         }
         break;

      case V_028714_SPI_SHADER_UNORM16_ABGR:
         args->enabled_channels = 0xf;
         packf = ac_build_cvt_pknorm_u16;
         break;

      case V_028714_SPI_SHADER_SNORM16_ABGR:
         args->enabled_channels = 0xf;
         packf = ac_build_cvt_pknorm_i16;
         break;

      case V_028714_SPI_SHADER_UINT16_ABGR:
         args->enabled_channels = 0xf;
         packi = ac_build_cvt_pk_u16;
         if (is_16bit) {
            for (unsigned chan = 0; chan < 4; chan++)
               values[chan] = LLVMBuildZExt(ctx->ac.builder, ac_to_integer(&ctx->ac, values[chan]),
                                            ctx->ac.i32, "");
         }
         break;

      case V_028714_SPI_SHADER_SINT16_ABGR:
         args->enabled_channels = 0xf;
         packi = ac_build_cvt_pk_i16;
         if (is_16bit) {
            for (unsigned chan = 0; chan < 4; chan++)
               values[chan] = LLVMBuildSExt(ctx->ac.builder, ac_to_integer(&ctx->ac, values[chan]),
                                            ctx->ac.i32, "");
         }
         break;

      default:
      case V_028714_SPI_SHADER_32_ABGR:
         memcpy(&args->out[0], values, sizeof(values[0]) * 4);
         break;
      }

      /* Replace NaN by zero (only 32-bit) to fix game bugs if
       * requested.
       */
      if (ctx->args->options->enable_mrt_output_nan_fixup && !is_16bit &&
          (col_format == V_028714_SPI_SHADER_32_R || col_format == V_028714_SPI_SHADER_32_GR ||
           col_format == V_028714_SPI_SHADER_32_AR || col_format == V_028714_SPI_SHADER_32_ABGR ||
           col_format == V_028714_SPI_SHADER_FP16_ABGR)) {
         for (unsigned i = 0; i < 4; i++) {
            LLVMValueRef class_args[2] = {values[i],
                                          LLVMConstInt(ctx->ac.i32, S_NAN | Q_NAN, false)};
            LLVMValueRef isnan = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.class.f32", ctx->ac.i1,
                                                    class_args, 2, AC_FUNC_ATTR_READNONE);
            values[i] = LLVMBuildSelect(ctx->ac.builder, isnan, ctx->ac.f32_0, values[i], "");
         }
      }

      /* Pack f16 or norm_i16/u16. */
      if (packf) {
         for (unsigned chan = 0; chan < 2; chan++) {
            LLVMValueRef pack_args[2] = {values[2 * chan], values[2 * chan + 1]};
            LLVMValueRef packed;

            packed = packf(&ctx->ac, pack_args);
            args->out[chan] = ac_to_float(&ctx->ac, packed);
         }
         args->compr = 1; /* COMPR flag */
      }

      /* Pack i16/u16. */
      if (packi) {
         for (unsigned chan = 0; chan < 2; chan++) {
            LLVMValueRef pack_args[2] = {ac_to_integer(&ctx->ac, values[2 * chan]),
                                         ac_to_integer(&ctx->ac, values[2 * chan + 1])};
            LLVMValueRef packed;

            packed = packi(&ctx->ac, pack_args, is_int8 ? 8 : is_int10 ? 10 : 16, chan == 1);
            args->out[chan] = ac_to_float(&ctx->ac, packed);
         }
         args->compr = 1; /* COMPR flag */
      }
      return;
   }

   if (is_16bit) {
      for (unsigned chan = 0; chan < 4; chan++) {
         values[chan] = LLVMBuildBitCast(ctx->ac.builder, values[chan], ctx->ac.i16, "");
         args->out[chan] = LLVMBuildZExt(ctx->ac.builder, values[chan], ctx->ac.i32, "");
      }
   } else
      memcpy(&args->out[0], values, sizeof(values[0]) * 4);

   for (unsigned i = 0; i < 4; ++i)
      args->out[i] = ac_to_float(&ctx->ac, args->out[i]);
}

static void
radv_export_param(struct radv_shader_context *ctx, unsigned index, LLVMValueRef *values,
                  unsigned enabled_channels)
{
   struct ac_export_args args;

   si_llvm_init_export_args(ctx, values, enabled_channels, V_008DFC_SQ_EXP_PARAM + index, &args);
   ac_build_export(&ctx->ac, &args);
}

static LLVMValueRef
radv_load_output(struct radv_shader_context *ctx, unsigned index, unsigned chan)
{
   LLVMValueRef output = ctx->abi.outputs[ac_llvm_reg_index_soa(index, chan)];
   return LLVMBuildLoad(ctx->ac.builder, output, "");
}

static void
radv_emit_stream_output(struct radv_shader_context *ctx, LLVMValueRef const *so_buffers,
                        LLVMValueRef const *so_write_offsets,
                        const struct radv_stream_output *output,
                        struct radv_shader_output_values *shader_out)
{
   unsigned num_comps = util_bitcount(output->component_mask);
   unsigned buf = output->buffer;
   unsigned offset = output->offset;
   unsigned start;
   LLVMValueRef out[4];

   assert(num_comps && num_comps <= 4);
   if (!num_comps || num_comps > 4)
      return;

   /* Get the first component. */
   start = ffs(output->component_mask) - 1;

   /* Load the output as int. */
   for (int i = 0; i < num_comps; i++) {
      out[i] = ac_to_integer(&ctx->ac, shader_out->values[start + i]);
   }

   /* Pack the output. */
   LLVMValueRef vdata = NULL;

   switch (num_comps) {
   case 1: /* as i32 */
      vdata = out[0];
      break;
   case 2: /* as v2i32 */
   case 3: /* as v4i32 (aligned to 4) */
      out[3] = LLVMGetUndef(ctx->ac.i32);
      FALLTHROUGH;
   case 4: /* as v4i32 */
      vdata = ac_build_gather_values(&ctx->ac, out,
                                     !ac_has_vec3_support(ctx->ac.chip_class, false)
                                        ? util_next_power_of_two(num_comps)
                                        : num_comps);
      break;
   }

   ac_build_buffer_store_dword(&ctx->ac, so_buffers[buf], vdata, num_comps, so_write_offsets[buf],
                               ctx->ac.i32_0, offset, ac_glc | ac_slc);
}

static void
radv_emit_streamout(struct radv_shader_context *ctx, unsigned stream)
{
   int i;

   /* Get bits [22:16], i.e. (so_param >> 16) & 127; */
   assert(ctx->args->ac.streamout_config.used);
   LLVMValueRef so_vtx_count = ac_build_bfe(
      &ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.streamout_config),
      LLVMConstInt(ctx->ac.i32, 16, false), LLVMConstInt(ctx->ac.i32, 7, false), false);

   LLVMValueRef tid = ac_get_thread_id(&ctx->ac);

   /* can_emit = tid < so_vtx_count; */
   LLVMValueRef can_emit = LLVMBuildICmp(ctx->ac.builder, LLVMIntULT, tid, so_vtx_count, "");

   /* Emit the streamout code conditionally. This actually avoids
    * out-of-bounds buffer access. The hw tells us via the SGPR
    * (so_vtx_count) which threads are allowed to emit streamout data.
    */
   ac_build_ifcc(&ctx->ac, can_emit, 6501);
   {
      /* The buffer offset is computed as follows:
       *   ByteOffset = streamout_offset[buffer_id]*4 +
       *                (streamout_write_index + thread_id)*stride[buffer_id] +
       *                attrib_offset
       */
      LLVMValueRef so_write_index = ac_get_arg(&ctx->ac, ctx->args->ac.streamout_write_index);

      /* Compute (streamout_write_index + thread_id). */
      so_write_index = LLVMBuildAdd(ctx->ac.builder, so_write_index, tid, "");

      /* Load the descriptor and compute the write offset for each
       * enabled buffer.
       */
      LLVMValueRef so_write_offset[4] = {0};
      LLVMValueRef so_buffers[4] = {0};
      LLVMValueRef buf_ptr = ac_get_arg(&ctx->ac, ctx->args->streamout_buffers);

      for (i = 0; i < 4; i++) {
         uint16_t stride = ctx->args->shader_info->so.strides[i];

         if (!stride)
            continue;

         LLVMValueRef offset = LLVMConstInt(ctx->ac.i32, i, false);

         so_buffers[i] = ac_build_load_to_sgpr(&ctx->ac, buf_ptr, offset);

         LLVMValueRef so_offset = ac_get_arg(&ctx->ac, ctx->args->ac.streamout_offset[i]);

         so_offset =
            LLVMBuildMul(ctx->ac.builder, so_offset, LLVMConstInt(ctx->ac.i32, 4, false), "");

         so_write_offset[i] = ac_build_imad(
            &ctx->ac, so_write_index, LLVMConstInt(ctx->ac.i32, stride * 4, false), so_offset);
      }

      /* Write streamout data. */
      for (i = 0; i < ctx->args->shader_info->so.num_outputs; i++) {
         struct radv_shader_output_values shader_out = {0};
         struct radv_stream_output *output = &ctx->args->shader_info->so.outputs[i];

         if (stream != output->stream)
            continue;

         for (int j = 0; j < 4; j++) {
            shader_out.values[j] = radv_load_output(ctx, output->location, j);
         }

         radv_emit_stream_output(ctx, so_buffers, so_write_offset, output, &shader_out);
      }
   }
   ac_build_endif(&ctx->ac, 6501);
}

static void
radv_build_param_exports(struct radv_shader_context *ctx, struct radv_shader_output_values *outputs,
                         unsigned noutput, struct radv_vs_output_info *outinfo,
                         bool export_clip_dists)
{
   unsigned param_count = 0;

   for (unsigned i = 0; i < noutput; i++) {
      unsigned slot_name = outputs[i].slot_name;
      unsigned usage_mask = outputs[i].usage_mask;

      if (slot_name != VARYING_SLOT_LAYER && slot_name != VARYING_SLOT_PRIMITIVE_ID &&
          slot_name != VARYING_SLOT_VIEWPORT && slot_name != VARYING_SLOT_CLIP_DIST0 &&
          slot_name != VARYING_SLOT_CLIP_DIST1 && slot_name < VARYING_SLOT_VAR0)
         continue;

      if ((slot_name == VARYING_SLOT_CLIP_DIST0 || slot_name == VARYING_SLOT_CLIP_DIST1) &&
          !export_clip_dists)
         continue;

      radv_export_param(ctx, param_count, outputs[i].values, usage_mask);

      assert(i < ARRAY_SIZE(outinfo->vs_output_param_offset));
      outinfo->vs_output_param_offset[slot_name] = param_count++;
   }

   outinfo->param_exports = param_count;
}

/* Generate export instructions for hardware VS shader stage or NGG GS stage
 * (position and parameter data only).
 */
static void
radv_llvm_export_vs(struct radv_shader_context *ctx, struct radv_shader_output_values *outputs,
                    unsigned noutput, struct radv_vs_output_info *outinfo, bool export_clip_dists)
{
   LLVMValueRef psize_value = NULL, layer_value = NULL, viewport_value = NULL;
   LLVMValueRef primitive_shading_rate = NULL;
   struct ac_export_args pos_args[4] = {0};
   unsigned pos_idx, index;
   int i;

   /* Build position exports */
   for (i = 0; i < noutput; i++) {
      switch (outputs[i].slot_name) {
      case VARYING_SLOT_POS:
         si_llvm_init_export_args(ctx, outputs[i].values, 0xf, V_008DFC_SQ_EXP_POS, &pos_args[0]);
         break;
      case VARYING_SLOT_PSIZ:
         psize_value = outputs[i].values[0];
         break;
      case VARYING_SLOT_LAYER:
         layer_value = outputs[i].values[0];
         break;
      case VARYING_SLOT_VIEWPORT:
         viewport_value = outputs[i].values[0];
         break;
      case VARYING_SLOT_PRIMITIVE_SHADING_RATE:
         primitive_shading_rate = outputs[i].values[0];
         break;
      case VARYING_SLOT_CLIP_DIST0:
      case VARYING_SLOT_CLIP_DIST1:
         index = 2 + outputs[i].slot_index;
         si_llvm_init_export_args(ctx, outputs[i].values, 0xf, V_008DFC_SQ_EXP_POS + index,
                                  &pos_args[index]);
         break;
      default:
         break;
      }
   }

   /* We need to add the position output manually if it's missing. */
   if (!pos_args[0].out[0]) {
      pos_args[0].enabled_channels = 0xf; /* writemask */
      pos_args[0].valid_mask = 0;         /* EXEC mask */
      pos_args[0].done = 0;               /* last export? */
      pos_args[0].target = V_008DFC_SQ_EXP_POS;
      pos_args[0].compr = 0;              /* COMPR flag */
      pos_args[0].out[0] = ctx->ac.f32_0; /* X */
      pos_args[0].out[1] = ctx->ac.f32_0; /* Y */
      pos_args[0].out[2] = ctx->ac.f32_0; /* Z */
      pos_args[0].out[3] = ctx->ac.f32_1; /* W */
   }

   bool writes_primitive_shading_rate = outinfo->writes_primitive_shading_rate ||
                                        ctx->args->options->force_vrs_rates;

   if (outinfo->writes_pointsize || outinfo->writes_layer || outinfo->writes_layer ||
       outinfo->writes_viewport_index || writes_primitive_shading_rate) {
      pos_args[1].enabled_channels = ((outinfo->writes_pointsize == true ? 1 : 0) |
                                      (writes_primitive_shading_rate == true ? 2 : 0) |
                                      (outinfo->writes_layer == true ? 4 : 0));
      pos_args[1].valid_mask = 0;
      pos_args[1].done = 0;
      pos_args[1].target = V_008DFC_SQ_EXP_POS + 1;
      pos_args[1].compr = 0;
      pos_args[1].out[0] = ctx->ac.f32_0; /* X */
      pos_args[1].out[1] = ctx->ac.f32_0; /* Y */
      pos_args[1].out[2] = ctx->ac.f32_0; /* Z */
      pos_args[1].out[3] = ctx->ac.f32_0; /* W */

      if (outinfo->writes_pointsize == true)
         pos_args[1].out[0] = psize_value;
      if (outinfo->writes_layer == true)
         pos_args[1].out[2] = layer_value;
      if (outinfo->writes_viewport_index == true) {
         if (ctx->args->options->chip_class >= GFX9) {
            /* GFX9 has the layer in out.z[10:0] and the viewport
             * index in out.z[19:16].
             */
            LLVMValueRef v = viewport_value;
            v = ac_to_integer(&ctx->ac, v);
            v = LLVMBuildShl(ctx->ac.builder, v, LLVMConstInt(ctx->ac.i32, 16, false), "");
            v = LLVMBuildOr(ctx->ac.builder, v, ac_to_integer(&ctx->ac, pos_args[1].out[2]), "");

            pos_args[1].out[2] = ac_to_float(&ctx->ac, v);
            pos_args[1].enabled_channels |= 1 << 2;
         } else {
            pos_args[1].out[3] = viewport_value;
            pos_args[1].enabled_channels |= 1 << 3;
         }
      }

      if (outinfo->writes_primitive_shading_rate) {
         pos_args[1].out[1] = primitive_shading_rate;
      } else if (ctx->args->options->force_vrs_rates) {
         /* Bits [2:3] = VRS rate X
          * Bits [4:5] = VRS rate Y
          *
          * The range is [-2, 1]. Values:
          *   1: 2x coarser shading rate in that direction.
          *   0: normal shading rate
          *  -1: 2x finer shading rate (sample shading, not directional)
          *  -2: 4x finer shading rate (sample shading, not directional)
          *
          * Sample shading can't go above 8 samples, so both numbers can't be -2 at the same time.
          */
         LLVMValueRef rates = LLVMConstInt(ctx->ac.i32, ctx->args->options->force_vrs_rates, false);
         LLVMValueRef cond;
         LLVMValueRef v;

         /* If Pos.W != 1 (typical for non-GUI elements), use 2x2 coarse shading. */
         cond = LLVMBuildFCmp(ctx->ac.builder, LLVMRealUNE, pos_args[0].out[3], ctx->ac.f32_1, "");
         v = LLVMBuildSelect(ctx->ac.builder, cond, rates, ctx->ac.i32_0, "");

         pos_args[1].out[1] = ac_to_float(&ctx->ac, v);
      }
   }

   for (i = 0; i < 4; i++) {
      if (pos_args[i].out[0])
         outinfo->pos_exports++;
   }

   /* GFX10 skip POS0 exports if EXEC=0 and DONE=0, causing a hang.
    * Setting valid_mask=1 prevents it and has no other effect.
    */
   if (ctx->ac.chip_class == GFX10)
      pos_args[0].valid_mask = 1;

   pos_idx = 0;
   for (i = 0; i < 4; i++) {
      if (!pos_args[i].out[0])
         continue;

      /* Specify the target we are exporting */
      pos_args[i].target = V_008DFC_SQ_EXP_POS + pos_idx++;

      if (pos_idx == outinfo->pos_exports)
         /* Specify that this is the last export */
         pos_args[i].done = 1;

      ac_build_export(&ctx->ac, &pos_args[i]);
   }

   /* Build parameter exports */
   radv_build_param_exports(ctx, outputs, noutput, outinfo, export_clip_dists);
}

static void
handle_vs_outputs_post(struct radv_shader_context *ctx, bool export_prim_id, bool export_clip_dists,
                       struct radv_vs_output_info *outinfo)
{
   struct radv_shader_output_values *outputs;
   unsigned noutput = 0;

   if (ctx->args->options->key.has_multiview_view_index) {
      LLVMValueRef *tmp_out = &ctx->abi.outputs[ac_llvm_reg_index_soa(VARYING_SLOT_LAYER, 0)];
      if (!*tmp_out) {
         for (unsigned i = 0; i < 4; ++i)
            ctx->abi.outputs[ac_llvm_reg_index_soa(VARYING_SLOT_LAYER, i)] =
               ac_build_alloca_undef(&ctx->ac, ctx->ac.f32, "");
      }

      LLVMValueRef view_index = ac_get_arg(&ctx->ac, ctx->args->ac.view_index);
      LLVMBuildStore(ctx->ac.builder, ac_to_float(&ctx->ac, view_index), *tmp_out);
      ctx->output_mask |= 1ull << VARYING_SLOT_LAYER;
   }

   memset(outinfo->vs_output_param_offset, AC_EXP_PARAM_UNDEFINED,
          sizeof(outinfo->vs_output_param_offset));
   outinfo->pos_exports = 0;

   if (ctx->args->shader_info->so.num_outputs && !ctx->args->is_gs_copy_shader) {
      /* The GS copy shader emission already emits streamout. */
      radv_emit_streamout(ctx, 0);
   }

   /* Allocate a temporary array for the output values. */
   unsigned num_outputs = util_bitcount64(ctx->output_mask) + export_prim_id;
   outputs = malloc(num_outputs * sizeof(outputs[0]));

   for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
      if (!(ctx->output_mask & (1ull << i)))
         continue;

      outputs[noutput].slot_name = i;
      outputs[noutput].slot_index = i == VARYING_SLOT_CLIP_DIST1;

      if (ctx->stage == MESA_SHADER_VERTEX && !ctx->args->is_gs_copy_shader) {
         outputs[noutput].usage_mask = ctx->args->shader_info->vs.output_usage_mask[i];
      } else if (ctx->stage == MESA_SHADER_TESS_EVAL) {
         outputs[noutput].usage_mask = ctx->args->shader_info->tes.output_usage_mask[i];
      } else {
         assert(ctx->args->is_gs_copy_shader);
         outputs[noutput].usage_mask = ctx->args->shader_info->gs.output_usage_mask[i];
      }

      for (unsigned j = 0; j < 4; j++) {
         outputs[noutput].values[j] = ac_to_float(&ctx->ac, radv_load_output(ctx, i, j));
      }

      noutput++;
   }

   /* Export PrimitiveID. */
   if (export_prim_id) {
      outputs[noutput].slot_name = VARYING_SLOT_PRIMITIVE_ID;
      outputs[noutput].slot_index = 0;
      outputs[noutput].usage_mask = 0x1;
      if (ctx->stage == MESA_SHADER_TESS_EVAL)
         outputs[noutput].values[0] = ac_get_arg(&ctx->ac, ctx->args->ac.tes_patch_id);
      else
         outputs[noutput].values[0] = ac_get_arg(&ctx->ac, ctx->args->ac.vs_prim_id);
      for (unsigned j = 1; j < 4; j++)
         outputs[noutput].values[j] = ctx->ac.f32_0;
      noutput++;
   }

   radv_llvm_export_vs(ctx, outputs, noutput, outinfo, export_clip_dists);

   free(outputs);
}

static LLVMValueRef
get_wave_id_in_tg(struct radv_shader_context *ctx)
{
   return ac_unpack_param(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.merged_wave_info), 24, 4);
}

static LLVMValueRef
get_tgsize(struct radv_shader_context *ctx)
{
   return ac_unpack_param(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.merged_wave_info), 28, 4);
}

static LLVMValueRef
get_thread_id_in_tg(struct radv_shader_context *ctx)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef tmp;
   tmp = LLVMBuildMul(builder, get_wave_id_in_tg(ctx),
                      LLVMConstInt(ctx->ac.i32, ctx->ac.wave_size, false), "");
   return LLVMBuildAdd(builder, tmp, ac_get_thread_id(&ctx->ac), "");
}

static LLVMValueRef
ngg_get_vtx_cnt(struct radv_shader_context *ctx)
{
   return ac_build_bfe(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.gs_tg_info),
                       LLVMConstInt(ctx->ac.i32, 12, false), LLVMConstInt(ctx->ac.i32, 9, false),
                       false);
}

static LLVMValueRef
ngg_get_prim_cnt(struct radv_shader_context *ctx)
{
   return ac_build_bfe(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.gs_tg_info),
                       LLVMConstInt(ctx->ac.i32, 22, false), LLVMConstInt(ctx->ac.i32, 9, false),
                       false);
}

static LLVMValueRef
ngg_gs_get_vertex_storage(struct radv_shader_context *ctx)
{
   unsigned num_outputs = util_bitcount64(ctx->output_mask);

   if (ctx->args->options->key.has_multiview_view_index)
      num_outputs++;

   LLVMTypeRef elements[2] = {
      LLVMArrayType(ctx->ac.i32, 4 * num_outputs),
      LLVMArrayType(ctx->ac.i8, 4),
   };
   LLVMTypeRef type = LLVMStructTypeInContext(ctx->ac.context, elements, 2, false);
   type = LLVMPointerType(LLVMArrayType(type, 0), AC_ADDR_SPACE_LDS);
   return LLVMBuildBitCast(ctx->ac.builder, ctx->gs_ngg_emit, type, "");
}

/**
 * Return a pointer to the LDS storage reserved for the N'th vertex, where N
 * is in emit order; that is:
 * - during the epilogue, N is the threadidx (relative to the entire threadgroup)
 * - during vertex emit, i.e. while the API GS shader invocation is running,
 *   N = threadidx * gs_max_out_vertices + emitidx
 *
 * Goals of the LDS memory layout:
 * 1. Eliminate bank conflicts on write for geometry shaders that have all emits
 *    in uniform control flow
 * 2. Eliminate bank conflicts on read for export if, additionally, there is no
 *    culling
 * 3. Agnostic to the number of waves (since we don't know it before compiling)
 * 4. Allow coalescing of LDS instructions (ds_write_b128 etc.)
 * 5. Avoid wasting memory.
 *
 * We use an AoS layout due to point 4 (this also helps point 3). In an AoS
 * layout, elimination of bank conflicts requires that each vertex occupy an
 * odd number of dwords. We use the additional dword to store the output stream
 * index as well as a flag to indicate whether this vertex ends a primitive
 * for rasterization.
 *
 * Swizzling is required to satisfy points 1 and 2 simultaneously.
 *
 * Vertices are stored in export order (gsthread * gs_max_out_vertices + emitidx).
 * Indices are swizzled in groups of 32, which ensures point 1 without
 * disturbing point 2.
 *
 * \return an LDS pointer to type {[N x i32], [4 x i8]}
 */
static LLVMValueRef
ngg_gs_vertex_ptr(struct radv_shader_context *ctx, LLVMValueRef vertexidx)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef storage = ngg_gs_get_vertex_storage(ctx);

   /* gs_max_out_vertices = 2^(write_stride_2exp) * some odd number */
   unsigned write_stride_2exp = ffs(MAX2(ctx->shader->info.gs.vertices_out, 1)) - 1;
   if (write_stride_2exp) {
      LLVMValueRef row = LLVMBuildLShr(builder, vertexidx, LLVMConstInt(ctx->ac.i32, 5, false), "");
      LLVMValueRef swizzle = LLVMBuildAnd(
         builder, row, LLVMConstInt(ctx->ac.i32, (1u << write_stride_2exp) - 1, false), "");
      vertexidx = LLVMBuildXor(builder, vertexidx, swizzle, "");
   }

   return ac_build_gep0(&ctx->ac, storage, vertexidx);
}

static LLVMValueRef
ngg_gs_emit_vertex_ptr(struct radv_shader_context *ctx, LLVMValueRef gsthread, LLVMValueRef emitidx)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef tmp;

   tmp = LLVMConstInt(ctx->ac.i32, ctx->shader->info.gs.vertices_out, false);
   tmp = LLVMBuildMul(builder, tmp, gsthread, "");
   const LLVMValueRef vertexidx = LLVMBuildAdd(builder, tmp, emitidx, "");
   return ngg_gs_vertex_ptr(ctx, vertexidx);
}

static LLVMValueRef
ngg_gs_get_emit_output_ptr(struct radv_shader_context *ctx, LLVMValueRef vertexptr,
                           unsigned out_idx)
{
   LLVMValueRef gep_idx[3] = {
      ctx->ac.i32_0, /* implied C-style array */
      ctx->ac.i32_0, /* first struct entry */
      LLVMConstInt(ctx->ac.i32, out_idx, false),
   };
   return LLVMBuildGEP(ctx->ac.builder, vertexptr, gep_idx, 3, "");
}

static LLVMValueRef
ngg_gs_get_emit_primflag_ptr(struct radv_shader_context *ctx, LLVMValueRef vertexptr,
                             unsigned stream)
{
   LLVMValueRef gep_idx[3] = {
      ctx->ac.i32_0, /* implied C-style array */
      ctx->ac.i32_1, /* second struct entry */
      LLVMConstInt(ctx->ac.i32, stream, false),
   };
   return LLVMBuildGEP(ctx->ac.builder, vertexptr, gep_idx, 3, "");
}

static void
handle_ngg_outputs_post_2(struct radv_shader_context *ctx)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef tmp;

   assert((ctx->stage == MESA_SHADER_VERTEX || ctx->stage == MESA_SHADER_TESS_EVAL) &&
          !ctx->args->is_gs_copy_shader);

   LLVMValueRef prims_in_wave =
      ac_unpack_param(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.merged_wave_info), 8, 8);
   LLVMValueRef vtx_in_wave =
      ac_unpack_param(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.merged_wave_info), 0, 8);
   LLVMValueRef is_gs_thread =
      LLVMBuildICmp(builder, LLVMIntULT, ac_get_thread_id(&ctx->ac), prims_in_wave, "");
   LLVMValueRef is_es_thread =
      LLVMBuildICmp(builder, LLVMIntULT, ac_get_thread_id(&ctx->ac), vtx_in_wave, "");
   LLVMValueRef vtxindex[] = {
      ac_unpack_param(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.gs_vtx_offset[0]), 0, 16),
      ac_unpack_param(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.gs_vtx_offset[0]), 16, 16),
      ac_unpack_param(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.gs_vtx_offset[1]), 0, 16),
   };

   /* Determine the number of vertices per primitive. */
   unsigned num_vertices;

   if (ctx->stage == MESA_SHADER_VERTEX) {
      num_vertices = 3; /* TODO: optimize for points & lines */
   } else {
      assert(ctx->stage == MESA_SHADER_TESS_EVAL);

      if (ctx->shader->info.tess.point_mode)
         num_vertices = 1;
      else if (ctx->shader->info.tess.primitive_mode == GL_ISOLINES)
         num_vertices = 2;
      else
         num_vertices = 3;
   }

   /* Copy Primitive IDs from GS threads to the LDS address corresponding
    * to the ES thread of the provoking vertex.
    */
   if (ctx->stage == MESA_SHADER_VERTEX && ctx->args->options->key.vs_common_out.export_prim_id) {
      ac_build_ifcc(&ctx->ac, is_gs_thread, 5400);

      LLVMValueRef provoking_vtx_in_prim = LLVMConstInt(ctx->ac.i32, 0, false);

      /* For provoking vertex last mode, use num_vtx_in_prim - 1. */
      if (ctx->args->options->key.vs.provoking_vtx_last)
         provoking_vtx_in_prim = LLVMConstInt(ctx->ac.i32, ctx->args->options->key.vs.outprim, false);

      /* provoking_vtx_index = vtxindex[provoking_vtx_in_prim]; */
      LLVMValueRef indices = ac_build_gather_values(&ctx->ac, vtxindex, 3);
      LLVMValueRef provoking_vtx_index =
         LLVMBuildExtractElement(builder, indices, provoking_vtx_in_prim, "");

      LLVMBuildStore(builder, ac_get_arg(&ctx->ac, ctx->args->ac.gs_prim_id),
                     ac_build_gep0(&ctx->ac, ctx->esgs_ring, provoking_vtx_index));
      ac_build_endif(&ctx->ac, 5400);
   }

   /* TODO: primitive culling */

   ac_build_sendmsg_gs_alloc_req(&ctx->ac, get_wave_id_in_tg(ctx), ngg_get_vtx_cnt(ctx),
                                 ngg_get_prim_cnt(ctx));

   /* TODO: streamout queries */
   /* Export primitive data to the index buffer.
    *
    * For the first version, we will always build up all three indices
    * independent of the primitive type. The additional garbage data
    * shouldn't hurt.
    *
    * TODO: culling depends on the primitive type, so can have some
    * interaction here.
    */
   ac_build_ifcc(&ctx->ac, is_gs_thread, 6001);
   {
      struct ac_ngg_prim prim = {0};

      if (ctx->args->options->key.vs_common_out.as_ngg_passthrough) {
         prim.passthrough = ac_get_arg(&ctx->ac, ctx->args->ac.gs_vtx_offset[0]);
      } else {
         prim.num_vertices = num_vertices;
         prim.isnull = ctx->ac.i1false;
         prim.edgeflags = ac_pack_edgeflags_for_export(&ctx->ac, &ctx->args->ac);
         memcpy(prim.index, vtxindex, sizeof(vtxindex[0]) * 3);
      }

      ac_build_export_prim(&ctx->ac, &prim);
   }
   ac_build_endif(&ctx->ac, 6001);

   /* Export per-vertex data (positions and parameters). */
   ac_build_ifcc(&ctx->ac, is_es_thread, 6002);
   {
      struct radv_vs_output_info *outinfo = ctx->stage == MESA_SHADER_TESS_EVAL
                                               ? &ctx->args->shader_info->tes.outinfo
                                               : &ctx->args->shader_info->vs.outinfo;

      /* Exporting the primitive ID is handled below. */
      /* TODO: use the new VS export path */
      handle_vs_outputs_post(ctx, false, ctx->args->options->key.vs_common_out.export_clip_dists,
                             outinfo);

      if (ctx->args->options->key.vs_common_out.export_prim_id) {
         unsigned param_count = outinfo->param_exports;
         LLVMValueRef values[4];

         if (ctx->stage == MESA_SHADER_VERTEX) {
            /* Wait for GS stores to finish. */
            ac_build_s_barrier(&ctx->ac);

            tmp = ac_build_gep0(&ctx->ac, ctx->esgs_ring, get_thread_id_in_tg(ctx));
            values[0] = LLVMBuildLoad(builder, tmp, "");
         } else {
            assert(ctx->stage == MESA_SHADER_TESS_EVAL);
            values[0] = ac_get_arg(&ctx->ac, ctx->args->ac.tes_patch_id);
         }

         values[0] = ac_to_float(&ctx->ac, values[0]);
         for (unsigned j = 1; j < 4; j++)
            values[j] = ctx->ac.f32_0;

         radv_export_param(ctx, param_count, values, 0x1);

         outinfo->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID] = param_count++;
         outinfo->param_exports = param_count;
      }
   }
   ac_build_endif(&ctx->ac, 6002);
}

static void
gfx10_ngg_gs_emit_prologue(struct radv_shader_context *ctx)
{
   /* Zero out the part of LDS scratch that is used to accumulate the
    * per-stream generated primitive count.
    */
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef scratchptr = ctx->gs_ngg_scratch;
   LLVMValueRef tid = get_thread_id_in_tg(ctx);
   LLVMBasicBlockRef merge_block;
   LLVMValueRef cond;

   LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->ac.builder));
   LLVMBasicBlockRef then_block = LLVMAppendBasicBlockInContext(ctx->ac.context, fn, "");
   merge_block = LLVMAppendBasicBlockInContext(ctx->ac.context, fn, "");

   cond = LLVMBuildICmp(builder, LLVMIntULT, tid, LLVMConstInt(ctx->ac.i32, 4, false), "");
   LLVMBuildCondBr(ctx->ac.builder, cond, then_block, merge_block);
   LLVMPositionBuilderAtEnd(ctx->ac.builder, then_block);

   LLVMValueRef ptr = ac_build_gep0(&ctx->ac, scratchptr, tid);
   LLVMBuildStore(builder, ctx->ac.i32_0, ptr);

   LLVMBuildBr(ctx->ac.builder, merge_block);
   LLVMPositionBuilderAtEnd(ctx->ac.builder, merge_block);

   ac_build_s_barrier(&ctx->ac);
}

static void
gfx10_ngg_gs_emit_epilogue_1(struct radv_shader_context *ctx)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef i8_0 = LLVMConstInt(ctx->ac.i8, 0, false);
   LLVMValueRef tmp;

   /* Zero out remaining (non-emitted) primitive flags.
    *
    * Note: Alternatively, we could pass the relevant gs_next_vertex to
    *       the emit threads via LDS. This is likely worse in the expected
    *       typical case where each GS thread emits the full set of
    *       vertices.
    */
   for (unsigned stream = 0; stream < 4; ++stream) {
      unsigned num_components;

      num_components = ctx->args->shader_info->gs.num_stream_output_components[stream];
      if (!num_components)
         continue;

      const LLVMValueRef gsthread = get_thread_id_in_tg(ctx);

      ac_build_bgnloop(&ctx->ac, 5100);

      const LLVMValueRef vertexidx = LLVMBuildLoad(builder, ctx->gs_next_vertex[stream], "");
      tmp = LLVMBuildICmp(builder, LLVMIntUGE, vertexidx,
                          LLVMConstInt(ctx->ac.i32, ctx->shader->info.gs.vertices_out, false), "");
      ac_build_ifcc(&ctx->ac, tmp, 5101);
      ac_build_break(&ctx->ac);
      ac_build_endif(&ctx->ac, 5101);

      tmp = LLVMBuildAdd(builder, vertexidx, ctx->ac.i32_1, "");
      LLVMBuildStore(builder, tmp, ctx->gs_next_vertex[stream]);

      tmp = ngg_gs_emit_vertex_ptr(ctx, gsthread, vertexidx);
      LLVMBuildStore(builder, i8_0, ngg_gs_get_emit_primflag_ptr(ctx, tmp, stream));

      ac_build_endloop(&ctx->ac, 5100);
   }

   /* Accumulate generated primitives counts across the entire threadgroup. */
   for (unsigned stream = 0; stream < 4; ++stream) {
      unsigned num_components;

      num_components = ctx->args->shader_info->gs.num_stream_output_components[stream];
      if (!num_components)
         continue;

      LLVMValueRef numprims = LLVMBuildLoad(builder, ctx->gs_generated_prims[stream], "");
      numprims = ac_build_reduce(&ctx->ac, numprims, nir_op_iadd, ctx->ac.wave_size);

      tmp = LLVMBuildICmp(builder, LLVMIntEQ, ac_get_thread_id(&ctx->ac), ctx->ac.i32_0, "");
      ac_build_ifcc(&ctx->ac, tmp, 5105);
      {
         LLVMBuildAtomicRMW(
            builder, LLVMAtomicRMWBinOpAdd,
            ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, LLVMConstInt(ctx->ac.i32, stream, false)),
            numprims, LLVMAtomicOrderingMonotonic, false);
      }
      ac_build_endif(&ctx->ac, 5105);
   }
}

static void
gfx10_ngg_gs_emit_epilogue_2(struct radv_shader_context *ctx)
{
   const unsigned verts_per_prim =
      si_conv_gl_prim_to_vertices(ctx->shader->info.gs.output_primitive);
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef tmp, tmp2;

   ac_build_s_barrier(&ctx->ac);

   const LLVMValueRef tid = get_thread_id_in_tg(ctx);
   LLVMValueRef num_emit_threads = ngg_get_prim_cnt(ctx);

   /* Write shader query data. */
   tmp = ac_get_arg(&ctx->ac, ctx->args->ngg_gs_state);
   tmp = LLVMBuildTrunc(builder, tmp, ctx->ac.i1, "");
   ac_build_ifcc(&ctx->ac, tmp, 5109);
   tmp = LLVMBuildICmp(builder, LLVMIntULT, tid, LLVMConstInt(ctx->ac.i32, 4, false), "");
   ac_build_ifcc(&ctx->ac, tmp, 5110);
   {
      tmp = LLVMBuildLoad(builder, ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, tid), "");

      ac_llvm_add_target_dep_function_attr(ctx->main_function, "amdgpu-gds-size", 256);

      LLVMTypeRef gdsptr = LLVMPointerType(ctx->ac.i32, AC_ADDR_SPACE_GDS);
      LLVMValueRef gdsbase = LLVMBuildIntToPtr(builder, ctx->ac.i32_0, gdsptr, "");

      const char *sync_scope = "workgroup-one-as";

      /* Use a plain GDS atomic to accumulate the number of generated
       * primitives.
       */
      ac_build_atomic_rmw(&ctx->ac, LLVMAtomicRMWBinOpAdd, gdsbase, tmp, sync_scope);
   }
   ac_build_endif(&ctx->ac, 5110);
   ac_build_endif(&ctx->ac, 5109);

   /* TODO: culling */

   /* Determine vertex liveness. */
   LLVMValueRef vertliveptr = ac_build_alloca(&ctx->ac, ctx->ac.i1, "vertexlive");

   tmp = LLVMBuildICmp(builder, LLVMIntULT, tid, num_emit_threads, "");
   ac_build_ifcc(&ctx->ac, tmp, 5120);
   {
      for (unsigned i = 0; i < verts_per_prim; ++i) {
         const LLVMValueRef primidx =
            LLVMBuildAdd(builder, tid, LLVMConstInt(ctx->ac.i32, i, false), "");

         if (i > 0) {
            tmp = LLVMBuildICmp(builder, LLVMIntULT, primidx, num_emit_threads, "");
            ac_build_ifcc(&ctx->ac, tmp, 5121 + i);
         }

         /* Load primitive liveness */
         tmp = ngg_gs_vertex_ptr(ctx, primidx);
         tmp = LLVMBuildLoad(builder, ngg_gs_get_emit_primflag_ptr(ctx, tmp, 0), "");
         const LLVMValueRef primlive = LLVMBuildTrunc(builder, tmp, ctx->ac.i1, "");

         tmp = LLVMBuildLoad(builder, vertliveptr, "");
         tmp = LLVMBuildOr(builder, tmp, primlive, ""), LLVMBuildStore(builder, tmp, vertliveptr);

         if (i > 0)
            ac_build_endif(&ctx->ac, 5121 + i);
      }
   }
   ac_build_endif(&ctx->ac, 5120);

   /* Inclusive scan addition across the current wave. */
   LLVMValueRef vertlive = LLVMBuildLoad(builder, vertliveptr, "");
   struct ac_wg_scan vertlive_scan = {0};
   vertlive_scan.op = nir_op_iadd;
   vertlive_scan.enable_reduce = true;
   vertlive_scan.enable_exclusive = true;
   vertlive_scan.src = vertlive;
   vertlive_scan.scratch = ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, ctx->ac.i32_0);
   vertlive_scan.waveidx = get_wave_id_in_tg(ctx);
   vertlive_scan.numwaves = get_tgsize(ctx);
   vertlive_scan.maxwaves = 8;

   ac_build_wg_scan(&ctx->ac, &vertlive_scan);

   /* Skip all exports (including index exports) when possible. At least on
    * early gfx10 revisions this is also to avoid hangs.
    */
   LLVMValueRef have_exports =
      LLVMBuildICmp(builder, LLVMIntNE, vertlive_scan.result_reduce, ctx->ac.i32_0, "");
   num_emit_threads = LLVMBuildSelect(builder, have_exports, num_emit_threads, ctx->ac.i32_0, "");

   /* Allocate export space. Send this message as early as possible, to
    * hide the latency of the SQ <-> SPI roundtrip.
    *
    * Note: We could consider compacting primitives for export as well.
    *       PA processes 1 non-null prim / clock, but it fetches 4 DW of
    *       prim data per clock and skips null primitives at no additional
    *       cost. So compacting primitives can only be beneficial when
    *       there are 4 or more contiguous null primitives in the export
    *       (in the common case of single-dword prim exports).
    */
   ac_build_sendmsg_gs_alloc_req(&ctx->ac, get_wave_id_in_tg(ctx), vertlive_scan.result_reduce,
                                 num_emit_threads);

   /* Setup the reverse vertex compaction permutation. We re-use stream 1
    * of the primitive liveness flags, relying on the fact that each
    * threadgroup can have at most 256 threads. */
   ac_build_ifcc(&ctx->ac, vertlive, 5130);
   {
      tmp = ngg_gs_vertex_ptr(ctx, vertlive_scan.result_exclusive);
      tmp2 = LLVMBuildTrunc(builder, tid, ctx->ac.i8, "");
      LLVMBuildStore(builder, tmp2, ngg_gs_get_emit_primflag_ptr(ctx, tmp, 1));
   }
   ac_build_endif(&ctx->ac, 5130);

   ac_build_s_barrier(&ctx->ac);

   /* Export primitive data */
   tmp = LLVMBuildICmp(builder, LLVMIntULT, tid, num_emit_threads, "");
   ac_build_ifcc(&ctx->ac, tmp, 5140);
   {
      LLVMValueRef flags;
      struct ac_ngg_prim prim = {0};
      prim.num_vertices = verts_per_prim;

      tmp = ngg_gs_vertex_ptr(ctx, tid);
      flags = LLVMBuildLoad(builder, ngg_gs_get_emit_primflag_ptr(ctx, tmp, 0), "");
      prim.isnull = LLVMBuildNot(builder, LLVMBuildTrunc(builder, flags, ctx->ac.i1, ""), "");
      prim.edgeflags = ctx->ac.i32_0;

      for (unsigned i = 0; i < verts_per_prim; ++i) {
         prim.index[i] = LLVMBuildSub(builder, vertlive_scan.result_exclusive,
                                      LLVMConstInt(ctx->ac.i32, verts_per_prim - i - 1, false), "");
      }

      /* Geometry shaders output triangle strips, but NGG expects triangles. */
      if (verts_per_prim == 3) {
         LLVMValueRef is_odd = LLVMBuildLShr(builder, flags, ctx->ac.i8_1, "");
         is_odd = LLVMBuildTrunc(builder, is_odd, ctx->ac.i1, "");

         LLVMValueRef flatshade_first =
            LLVMConstInt(ctx->ac.i1, !ctx->args->options->key.vs.provoking_vtx_last, false);

         ac_build_triangle_strip_indices_to_triangle(&ctx->ac, is_odd, flatshade_first, prim.index);
      }

      ac_build_export_prim(&ctx->ac, &prim);
   }
   ac_build_endif(&ctx->ac, 5140);

   /* Export position and parameter data */
   tmp = LLVMBuildICmp(builder, LLVMIntULT, tid, vertlive_scan.result_reduce, "");
   ac_build_ifcc(&ctx->ac, tmp, 5145);
   {
      struct radv_vs_output_info *outinfo = &ctx->args->shader_info->vs.outinfo;
      bool export_view_index = ctx->args->options->key.has_multiview_view_index;
      struct radv_shader_output_values *outputs;
      unsigned noutput = 0;

      /* Allocate a temporary array for the output values. */
      unsigned num_outputs = util_bitcount64(ctx->output_mask) + export_view_index;
      outputs = calloc(num_outputs, sizeof(outputs[0]));

      memset(outinfo->vs_output_param_offset, AC_EXP_PARAM_UNDEFINED,
             sizeof(outinfo->vs_output_param_offset));
      outinfo->pos_exports = 0;

      tmp = ngg_gs_vertex_ptr(ctx, tid);
      tmp = LLVMBuildLoad(builder, ngg_gs_get_emit_primflag_ptr(ctx, tmp, 1), "");
      tmp = LLVMBuildZExt(builder, tmp, ctx->ac.i32, "");
      const LLVMValueRef vertexptr = ngg_gs_vertex_ptr(ctx, tmp);

      unsigned out_idx = 0;
      for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
         unsigned output_usage_mask = ctx->args->shader_info->gs.output_usage_mask[i];
         int length = util_last_bit(output_usage_mask);

         if (!(ctx->output_mask & (1ull << i)))
            continue;

         outputs[noutput].slot_name = i;
         outputs[noutput].slot_index = i == VARYING_SLOT_CLIP_DIST1;
         outputs[noutput].usage_mask = output_usage_mask;

         for (unsigned j = 0; j < length; j++, out_idx++) {
            if (!(output_usage_mask & (1 << j)))
               continue;

            tmp = ngg_gs_get_emit_output_ptr(ctx, vertexptr, out_idx);
            tmp = LLVMBuildLoad(builder, tmp, "");

            LLVMTypeRef type = LLVMGetAllocatedType(ctx->abi.outputs[ac_llvm_reg_index_soa(i, j)]);
            if (ac_get_type_size(type) == 2) {
               tmp = ac_to_integer(&ctx->ac, tmp);
               tmp = LLVMBuildTrunc(ctx->ac.builder, tmp, ctx->ac.i16, "");
            }

            outputs[noutput].values[j] = ac_to_float(&ctx->ac, tmp);
         }

         for (unsigned j = length; j < 4; j++)
            outputs[noutput].values[j] = LLVMGetUndef(ctx->ac.f32);

         noutput++;
      }

      /* Export ViewIndex. */
      if (export_view_index) {
         outputs[noutput].slot_name = VARYING_SLOT_LAYER;
         outputs[noutput].slot_index = 0;
         outputs[noutput].usage_mask = 0x1;
         outputs[noutput].values[0] =
            ac_to_float(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.view_index));
         for (unsigned j = 1; j < 4; j++)
            outputs[noutput].values[j] = ctx->ac.f32_0;
         noutput++;
      }

      radv_llvm_export_vs(ctx, outputs, noutput, outinfo,
                          ctx->args->options->key.vs_common_out.export_clip_dists);
      FREE(outputs);
   }
   ac_build_endif(&ctx->ac, 5145);
}

static void
gfx10_ngg_gs_emit_vertex(struct radv_shader_context *ctx, unsigned stream, LLVMValueRef vertexidx,
                         LLVMValueRef *addrs)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef tmp;

   const LLVMValueRef vertexptr = ngg_gs_emit_vertex_ptr(ctx, get_thread_id_in_tg(ctx), vertexidx);
   unsigned out_idx = 0;
   for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
      unsigned output_usage_mask = ctx->args->shader_info->gs.output_usage_mask[i];
      uint8_t output_stream = ctx->args->shader_info->gs.output_streams[i];
      LLVMValueRef *out_ptr = &addrs[i * 4];
      int length = util_last_bit(output_usage_mask);

      if (!(ctx->output_mask & (1ull << i)) || output_stream != stream)
         continue;

      for (unsigned j = 0; j < length; j++, out_idx++) {
         if (!(output_usage_mask & (1 << j)))
            continue;

         LLVMValueRef out_val = LLVMBuildLoad(ctx->ac.builder, out_ptr[j], "");
         out_val = ac_to_integer(&ctx->ac, out_val);
         out_val = LLVMBuildZExtOrBitCast(ctx->ac.builder, out_val, ctx->ac.i32, "");

         LLVMBuildStore(builder, out_val, ngg_gs_get_emit_output_ptr(ctx, vertexptr, out_idx));
      }
   }
   assert(out_idx * 4 <= ctx->args->shader_info->gs.gsvs_vertex_size);

   /* Store the current number of emitted vertices to zero out remaining
    * primitive flags in case the geometry shader doesn't emit the maximum
    * number of vertices.
    */
   tmp = LLVMBuildAdd(builder, vertexidx, ctx->ac.i32_1, "");
   LLVMBuildStore(builder, tmp, ctx->gs_next_vertex[stream]);

   /* Determine and store whether this vertex completed a primitive. */
   const LLVMValueRef curverts = LLVMBuildLoad(builder, ctx->gs_curprim_verts[stream], "");

   tmp = LLVMConstInt(
      ctx->ac.i32, si_conv_gl_prim_to_vertices(ctx->shader->info.gs.output_primitive) - 1, false);
   const LLVMValueRef iscompleteprim = LLVMBuildICmp(builder, LLVMIntUGE, curverts, tmp, "");

   /* Since the geometry shader emits triangle strips, we need to
    * track which primitive is odd and swap vertex indices to get
    * the correct vertex order.
    */
   LLVMValueRef is_odd = ctx->ac.i1false;
   if (stream == 0 && si_conv_gl_prim_to_vertices(ctx->shader->info.gs.output_primitive) == 3) {
      tmp = LLVMBuildAnd(builder, curverts, ctx->ac.i32_1, "");
      is_odd = LLVMBuildICmp(builder, LLVMIntEQ, tmp, ctx->ac.i32_1, "");
   }

   tmp = LLVMBuildAdd(builder, curverts, ctx->ac.i32_1, "");
   LLVMBuildStore(builder, tmp, ctx->gs_curprim_verts[stream]);

   /* The per-vertex primitive flag encoding:
    *   bit 0: whether this vertex finishes a primitive
    *   bit 1: whether the primitive is odd (if we are emitting triangle strips)
    */
   tmp = LLVMBuildZExt(builder, iscompleteprim, ctx->ac.i8, "");
   tmp = LLVMBuildOr(
      builder, tmp,
      LLVMBuildShl(builder, LLVMBuildZExt(builder, is_odd, ctx->ac.i8, ""), ctx->ac.i8_1, ""), "");
   LLVMBuildStore(builder, tmp, ngg_gs_get_emit_primflag_ptr(ctx, vertexptr, stream));

   tmp = LLVMBuildLoad(builder, ctx->gs_generated_prims[stream], "");
   tmp = LLVMBuildAdd(builder, tmp, LLVMBuildZExt(builder, iscompleteprim, ctx->ac.i32, ""), "");
   LLVMBuildStore(builder, tmp, ctx->gs_generated_prims[stream]);
}

static bool
si_export_mrt_color(struct radv_shader_context *ctx, LLVMValueRef *color, unsigned index,
                    struct ac_export_args *args)
{
   /* Export */
   si_llvm_init_export_args(ctx, color, 0xf, V_008DFC_SQ_EXP_MRT + index, args);
   if (!args->enabled_channels)
      return false; /* unnecessary NULL export */

   return true;
}

static void
radv_export_mrt_z(struct radv_shader_context *ctx, LLVMValueRef depth, LLVMValueRef stencil,
                  LLVMValueRef samplemask)
{
   struct ac_export_args args;

   ac_export_mrt_z(&ctx->ac, depth, stencil, samplemask, &args);

   ac_build_export(&ctx->ac, &args);
}

static void
handle_fs_outputs_post(struct radv_shader_context *ctx)
{
   unsigned index = 0;
   LLVMValueRef depth = NULL, stencil = NULL, samplemask = NULL;
   struct ac_export_args color_args[8];

   for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
      LLVMValueRef values[4];

      if (!(ctx->output_mask & (1ull << i)))
         continue;

      if (i < FRAG_RESULT_DATA0)
         continue;

      for (unsigned j = 0; j < 4; j++)
         values[j] = ac_to_float(&ctx->ac, radv_load_output(ctx, i, j));

      bool ret = si_export_mrt_color(ctx, values, i - FRAG_RESULT_DATA0, &color_args[index]);
      if (ret)
         index++;
   }

   /* Process depth, stencil, samplemask. */
   if (ctx->args->shader_info->ps.writes_z) {
      depth = ac_to_float(&ctx->ac, radv_load_output(ctx, FRAG_RESULT_DEPTH, 0));
   }
   if (ctx->args->shader_info->ps.writes_stencil) {
      stencil = ac_to_float(&ctx->ac, radv_load_output(ctx, FRAG_RESULT_STENCIL, 0));
   }
   if (ctx->args->shader_info->ps.writes_sample_mask) {
      samplemask = ac_to_float(&ctx->ac, radv_load_output(ctx, FRAG_RESULT_SAMPLE_MASK, 0));
   }

   /* Set the DONE bit on last non-null color export only if Z isn't
    * exported.
    */
   if (index > 0 && !ctx->args->shader_info->ps.writes_z &&
       !ctx->args->shader_info->ps.writes_stencil &&
       !ctx->args->shader_info->ps.writes_sample_mask) {
      unsigned last = index - 1;

      color_args[last].valid_mask = 1; /* whether the EXEC mask is valid */
      color_args[last].done = 1;       /* DONE bit */
   }

   /* Export PS outputs. */
   for (unsigned i = 0; i < index; i++)
      ac_build_export(&ctx->ac, &color_args[i]);

   if (depth || stencil || samplemask)
      radv_export_mrt_z(ctx, depth, stencil, samplemask);
   else if (!index)
      ac_build_export_null(&ctx->ac);
}

static void
emit_gs_epilogue(struct radv_shader_context *ctx)
{
   if (ctx->args->options->key.vs_common_out.as_ngg) {
      gfx10_ngg_gs_emit_epilogue_1(ctx);
      return;
   }

   if (ctx->ac.chip_class >= GFX10)
      LLVMBuildFence(ctx->ac.builder, LLVMAtomicOrderingRelease, false, "");

   ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_NOP | AC_SENDMSG_GS_DONE, ctx->gs_wave_id);
}

static void
handle_shader_outputs_post(struct ac_shader_abi *abi)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);

   switch (ctx->stage) {
   case MESA_SHADER_VERTEX:
      if (ctx->args->options->key.vs_common_out.as_ls)
         break; /* Lowered in NIR */
      else if (ctx->args->options->key.vs_common_out.as_es)
         break; /* Lowered in NIR */
      else if (ctx->args->options->key.vs_common_out.as_ngg)
         break;
      else
         handle_vs_outputs_post(ctx, ctx->args->options->key.vs_common_out.export_prim_id,
                                ctx->args->options->key.vs_common_out.export_clip_dists,
                                &ctx->args->shader_info->vs.outinfo);
      break;
   case MESA_SHADER_FRAGMENT:
      handle_fs_outputs_post(ctx);
      break;
   case MESA_SHADER_GEOMETRY:
      emit_gs_epilogue(ctx);
      break;
   case MESA_SHADER_TESS_CTRL:
      break; /* Lowered in NIR */
   case MESA_SHADER_TESS_EVAL:
      if (ctx->args->options->key.vs_common_out.as_es)
         break; /* Lowered in NIR */
      else if (ctx->args->options->key.vs_common_out.as_ngg)
         break;
      else
         handle_vs_outputs_post(ctx, ctx->args->options->key.vs_common_out.export_prim_id,
                                ctx->args->options->key.vs_common_out.export_clip_dists,
                                &ctx->args->shader_info->tes.outinfo);
      break;
   default:
      break;
   }
}

static void
ac_llvm_finalize_module(struct radv_shader_context *ctx, LLVMPassManagerRef passmgr,
                        const struct radv_nir_compiler_options *options)
{
   LLVMRunPassManager(passmgr, ctx->ac.module);
   LLVMDisposeBuilder(ctx->ac.builder);

   ac_llvm_context_dispose(&ctx->ac);
}

static void
ac_nir_eliminate_const_vs_outputs(struct radv_shader_context *ctx)
{
   struct radv_vs_output_info *outinfo;

   switch (ctx->stage) {
   case MESA_SHADER_FRAGMENT:
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_GEOMETRY:
      return;
   case MESA_SHADER_VERTEX:
      if (ctx->args->options->key.vs_common_out.as_ls ||
          ctx->args->options->key.vs_common_out.as_es)
         return;
      outinfo = &ctx->args->shader_info->vs.outinfo;
      break;
   case MESA_SHADER_TESS_EVAL:
      if (ctx->args->options->key.vs_common_out.as_es)
         return;
      outinfo = &ctx->args->shader_info->tes.outinfo;
      break;
   default:
      unreachable("Unhandled shader type");
   }

   ac_optimize_vs_outputs(&ctx->ac, ctx->main_function, outinfo->vs_output_param_offset,
                          VARYING_SLOT_MAX, 0, &outinfo->param_exports);
}

static void
ac_setup_rings(struct radv_shader_context *ctx)
{
   if (ctx->args->options->chip_class <= GFX8 &&
       (ctx->stage == MESA_SHADER_GEOMETRY || ctx->args->options->key.vs_common_out.as_es)) {
      unsigned ring = ctx->stage == MESA_SHADER_GEOMETRY ? RING_ESGS_GS : RING_ESGS_VS;
      LLVMValueRef offset = LLVMConstInt(ctx->ac.i32, ring, false);

      ctx->esgs_ring = ac_build_load_to_sgpr(&ctx->ac, ctx->ring_offsets, offset);
   }

   if (ctx->args->is_gs_copy_shader) {
      ctx->gsvs_ring[0] = ac_build_load_to_sgpr(&ctx->ac, ctx->ring_offsets,
                                                LLVMConstInt(ctx->ac.i32, RING_GSVS_VS, false));
   }

   if (ctx->stage == MESA_SHADER_GEOMETRY) {
      /* The conceptual layout of the GSVS ring is
       *   v0c0 .. vLv0 v0c1 .. vLc1 ..
       * but the real memory layout is swizzled across
       * threads:
       *   t0v0c0 .. t15v0c0 t0v1c0 .. t15v1c0 ... t15vLcL
       *   t16v0c0 ..
       * Override the buffer descriptor accordingly.
       */
      LLVMTypeRef v2i64 = LLVMVectorType(ctx->ac.i64, 2);
      uint64_t stream_offset = 0;
      unsigned num_records = ctx->ac.wave_size;
      LLVMValueRef base_ring;

      base_ring = ac_build_load_to_sgpr(&ctx->ac, ctx->ring_offsets,
                                        LLVMConstInt(ctx->ac.i32, RING_GSVS_GS, false));

      for (unsigned stream = 0; stream < 4; stream++) {
         unsigned num_components, stride;
         LLVMValueRef ring, tmp;

         num_components = ctx->args->shader_info->gs.num_stream_output_components[stream];

         if (!num_components)
            continue;

         stride = 4 * num_components * ctx->shader->info.gs.vertices_out;

         /* Limit on the stride field for <= GFX7. */
         assert(stride < (1 << 14));

         ring = LLVMBuildBitCast(ctx->ac.builder, base_ring, v2i64, "");
         tmp = LLVMBuildExtractElement(ctx->ac.builder, ring, ctx->ac.i32_0, "");
         tmp = LLVMBuildAdd(ctx->ac.builder, tmp, LLVMConstInt(ctx->ac.i64, stream_offset, 0), "");
         ring = LLVMBuildInsertElement(ctx->ac.builder, ring, tmp, ctx->ac.i32_0, "");

         stream_offset += stride * ctx->ac.wave_size;

         ring = LLVMBuildBitCast(ctx->ac.builder, ring, ctx->ac.v4i32, "");

         tmp = LLVMBuildExtractElement(ctx->ac.builder, ring, ctx->ac.i32_1, "");
         tmp = LLVMBuildOr(ctx->ac.builder, tmp,
                           LLVMConstInt(ctx->ac.i32, S_008F04_STRIDE(stride), false), "");
         ring = LLVMBuildInsertElement(ctx->ac.builder, ring, tmp, ctx->ac.i32_1, "");

         ring = LLVMBuildInsertElement(ctx->ac.builder, ring,
                                       LLVMConstInt(ctx->ac.i32, num_records, false),
                                       LLVMConstInt(ctx->ac.i32, 2, false), "");

         ctx->gsvs_ring[stream] = ring;
      }
   }

   if (ctx->stage == MESA_SHADER_TESS_CTRL || ctx->stage == MESA_SHADER_TESS_EVAL) {
      ctx->hs_ring_tess_offchip = ac_build_load_to_sgpr(
         &ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->ac.i32, RING_HS_TESS_OFFCHIP, false));
      ctx->hs_ring_tess_factor = ac_build_load_to_sgpr(
         &ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->ac.i32, RING_HS_TESS_FACTOR, false));
   }
}

/* Fixup the HW not emitting the TCS regs if there are no HS threads. */
static void
ac_nir_fixup_ls_hs_input_vgprs(struct radv_shader_context *ctx)
{
   LLVMValueRef count =
      ac_unpack_param(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.merged_wave_info), 8, 8);
   LLVMValueRef hs_empty = LLVMBuildICmp(ctx->ac.builder, LLVMIntEQ, count, ctx->ac.i32_0, "");
   ctx->abi.instance_id =
      LLVMBuildSelect(ctx->ac.builder, hs_empty, ac_get_arg(&ctx->ac, ctx->args->ac.vertex_id),
                      ctx->abi.instance_id, "");
   ctx->vs_rel_patch_id =
      LLVMBuildSelect(ctx->ac.builder, hs_empty, ac_get_arg(&ctx->ac, ctx->args->ac.tcs_rel_ids),
                      ctx->vs_rel_patch_id, "");
   ctx->abi.vertex_id =
      LLVMBuildSelect(ctx->ac.builder, hs_empty, ac_get_arg(&ctx->ac, ctx->args->ac.tcs_patch_id),
                      ctx->abi.vertex_id, "");
}

static void
prepare_gs_input_vgprs(struct radv_shader_context *ctx, bool merged)
{
   if (merged) {
      for (int i = 5; i >= 0; --i) {
         ctx->gs_vtx_offset[i] = ac_unpack_param(
            &ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.gs_vtx_offset[i / 2]), (i & 1) * 16, 16);
      }

      ctx->gs_wave_id =
         ac_unpack_param(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.merged_wave_info), 16, 8);
   } else {
      for (int i = 0; i < 6; i++)
         ctx->gs_vtx_offset[i] = ac_get_arg(&ctx->ac, ctx->args->ac.gs_vtx_offset[i]);
      ctx->gs_wave_id = ac_get_arg(&ctx->ac, ctx->args->ac.gs_wave_id);
   }
}

/* Ensure that the esgs ring is declared.
 *
 * We declare it with 64KB alignment as a hint that the
 * pointer value will always be 0.
 */
static void
declare_esgs_ring(struct radv_shader_context *ctx)
{
   if (ctx->esgs_ring)
      return;

   assert(!LLVMGetNamedGlobal(ctx->ac.module, "esgs_ring"));

   ctx->esgs_ring = LLVMAddGlobalInAddressSpace(ctx->ac.module, LLVMArrayType(ctx->ac.i32, 0),
                                                "esgs_ring", AC_ADDR_SPACE_LDS);
   LLVMSetLinkage(ctx->esgs_ring, LLVMExternalLinkage);
   LLVMSetAlignment(ctx->esgs_ring, 64 * 1024);
}

static LLVMModuleRef
ac_translate_nir_to_llvm(struct ac_llvm_compiler *ac_llvm, struct nir_shader *const *shaders,
                         int shader_count, const struct radv_shader_args *args)
{
   struct radv_shader_context ctx = {0};
   ctx.args = args;

   enum ac_float_mode float_mode = AC_FLOAT_MODE_DEFAULT;

   if (args->shader_info->float_controls_mode & FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP32) {
      float_mode = AC_FLOAT_MODE_DENORM_FLUSH_TO_ZERO;
   }

   ac_llvm_context_init(&ctx.ac, ac_llvm, args->options->chip_class, args->options->family,
                        args->options->info, float_mode, args->shader_info->wave_size,
                        args->shader_info->ballot_bit_size);
   ctx.context = ctx.ac.context;

   ctx.max_workgroup_size = args->shader_info->workgroup_size;

   if (ctx.ac.chip_class >= GFX10) {
      if (is_pre_gs_stage(shaders[0]->info.stage) && args->options->key.vs_common_out.as_ngg) {
         ctx.max_workgroup_size = 128;
      }
   }

   create_function(&ctx, shaders[shader_count - 1]->info.stage, shader_count >= 2);

   ctx.abi.emit_outputs = handle_shader_outputs_post;
   ctx.abi.emit_vertex_with_counter = visit_emit_vertex_with_counter;
   ctx.abi.load_ubo = radv_load_ubo;
   ctx.abi.load_ssbo = radv_load_ssbo;
   ctx.abi.load_sampler_desc = radv_get_sampler_desc;
   ctx.abi.load_resource = radv_load_resource;
   ctx.abi.load_ring_tess_factors = load_ring_tess_factors;
   ctx.abi.load_ring_tess_offchip = load_ring_tess_offchip;
   ctx.abi.load_ring_esgs = load_ring_esgs;
   ctx.abi.clamp_shadow_reference = false;
   ctx.abi.adjust_frag_coord_z = args->options->adjust_frag_coord_z;
   ctx.abi.robust_buffer_access = args->options->robust_buffer_access;

   bool is_ngg = is_pre_gs_stage(shaders[0]->info.stage) && args->options->key.vs_common_out.as_ngg;
   if (shader_count >= 2 || is_ngg)
      ac_init_exec_full_mask(&ctx.ac);

   if (args->ac.vertex_id.used)
      ctx.abi.vertex_id = ac_get_arg(&ctx.ac, args->ac.vertex_id);
   if (args->ac.vs_rel_patch_id.used)
      ctx.vs_rel_patch_id = ac_get_arg(&ctx.ac, args->ac.vs_rel_patch_id);
   if (args->ac.instance_id.used)
      ctx.abi.instance_id = ac_get_arg(&ctx.ac, args->ac.instance_id);

   if (args->options->has_ls_vgpr_init_bug &&
       shaders[shader_count - 1]->info.stage == MESA_SHADER_TESS_CTRL)
      ac_nir_fixup_ls_hs_input_vgprs(&ctx);

   if (is_ngg) {
      /* Declare scratch space base for streamout and vertex
       * compaction. Whether space is actually allocated is
       * determined during linking / PM4 creation.
       *
       * Add an extra dword per vertex to ensure an odd stride, which
       * avoids bank conflicts for SoA accesses.
       */
      if (!args->options->key.vs_common_out.as_ngg_passthrough)
         declare_esgs_ring(&ctx);

      /* GFX10 hang workaround - there needs to be an s_barrier before gs_alloc_req always */
      if (ctx.ac.chip_class == GFX10 && shader_count == 1)
         ac_build_s_barrier(&ctx.ac);
   }

   for (int shader_idx = 0; shader_idx < shader_count; ++shader_idx) {
      ctx.stage = shaders[shader_idx]->info.stage;
      ctx.shader = shaders[shader_idx];
      ctx.output_mask = 0;

      if (shaders[shader_idx]->info.stage == MESA_SHADER_GEOMETRY) {
         for (int i = 0; i < 4; i++) {
            ctx.gs_next_vertex[i] = ac_build_alloca(&ctx.ac, ctx.ac.i32, "");
         }
         if (args->options->key.vs_common_out.as_ngg) {
            for (unsigned i = 0; i < 4; ++i) {
               ctx.gs_curprim_verts[i] = ac_build_alloca(&ctx.ac, ctx.ac.i32, "");
               ctx.gs_generated_prims[i] = ac_build_alloca(&ctx.ac, ctx.ac.i32, "");
            }

            LLVMTypeRef ai32 = LLVMArrayType(ctx.ac.i32, 8);
            ctx.gs_ngg_scratch =
               LLVMAddGlobalInAddressSpace(ctx.ac.module, ai32, "ngg_scratch", AC_ADDR_SPACE_LDS);
            LLVMSetInitializer(ctx.gs_ngg_scratch, LLVMGetUndef(ai32));
            LLVMSetAlignment(ctx.gs_ngg_scratch, 4);

            ctx.gs_ngg_emit = LLVMAddGlobalInAddressSpace(
               ctx.ac.module, LLVMArrayType(ctx.ac.i32, 0), "ngg_emit", AC_ADDR_SPACE_LDS);
            LLVMSetLinkage(ctx.gs_ngg_emit, LLVMExternalLinkage);
            LLVMSetAlignment(ctx.gs_ngg_emit, 4);
         }

         ctx.abi.emit_primitive = visit_end_primitive;
      } else if (shaders[shader_idx]->info.stage == MESA_SHADER_TESS_EVAL) {
      } else if (shaders[shader_idx]->info.stage == MESA_SHADER_VERTEX) {
         ctx.abi.load_base_vertex = radv_load_base_vertex;
         ctx.abi.load_inputs = radv_load_vs_inputs;
      } else if (shaders[shader_idx]->info.stage == MESA_SHADER_FRAGMENT) {
         ctx.abi.load_sample_position = load_sample_position;
         ctx.abi.load_sample_mask_in = load_sample_mask_in;
      }

      if (shaders[shader_idx]->info.stage == MESA_SHADER_VERTEX &&
          args->options->key.vs_common_out.as_ngg &&
          args->options->key.vs_common_out.export_prim_id) {
         declare_esgs_ring(&ctx);
      }

      bool nested_barrier = false;

      if (shader_idx) {
         if (shaders[shader_idx]->info.stage == MESA_SHADER_GEOMETRY &&
             args->options->key.vs_common_out.as_ngg) {
            gfx10_ngg_gs_emit_prologue(&ctx);
            nested_barrier = false;
         } else {
            nested_barrier = true;
         }
      }

      if (nested_barrier) {
         /* Execute a barrier before the second shader in
          * a merged shader.
          *
          * Execute the barrier inside the conditional block,
          * so that empty waves can jump directly to s_endpgm,
          * which will also signal the barrier.
          *
          * This is possible in gfx9, because an empty wave
          * for the second shader does not participate in
          * the epilogue. With NGG, empty waves may still
          * be required to export data (e.g. GS output vertices),
          * so we cannot let them exit early.
          *
          * If the shader is TCS and the TCS epilog is present
          * and contains a barrier, it will wait there and then
          * reach s_endpgm.
          */
         ac_emit_barrier(&ctx.ac, ctx.stage);
      }

      nir_foreach_shader_out_variable(variable, shaders[shader_idx]) scan_shader_output_decl(
         &ctx, variable, shaders[shader_idx], shaders[shader_idx]->info.stage);

      ac_setup_rings(&ctx);

      LLVMBasicBlockRef merge_block = NULL;
      if (shader_count >= 2 || is_ngg) {
         LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx.ac.builder));
         LLVMBasicBlockRef then_block = LLVMAppendBasicBlockInContext(ctx.ac.context, fn, "");
         merge_block = LLVMAppendBasicBlockInContext(ctx.ac.context, fn, "");

         LLVMValueRef count = ac_unpack_param(
            &ctx.ac, ac_get_arg(&ctx.ac, args->ac.merged_wave_info), 8 * shader_idx, 8);
         LLVMValueRef thread_id = ac_get_thread_id(&ctx.ac);
         LLVMValueRef cond = LLVMBuildICmp(ctx.ac.builder, LLVMIntULT, thread_id, count, "");
         LLVMBuildCondBr(ctx.ac.builder, cond, then_block, merge_block);

         LLVMPositionBuilderAtEnd(ctx.ac.builder, then_block);
      }

      if (shaders[shader_idx]->info.stage == MESA_SHADER_FRAGMENT)
         prepare_interp_optimize(&ctx, shaders[shader_idx]);
      else if (shaders[shader_idx]->info.stage == MESA_SHADER_GEOMETRY)
         prepare_gs_input_vgprs(&ctx, shader_count >= 2);

      ac_nir_translate(&ctx.ac, &ctx.abi, &args->ac, shaders[shader_idx]);

      if (shader_count >= 2 || is_ngg) {
         LLVMBuildBr(ctx.ac.builder, merge_block);
         LLVMPositionBuilderAtEnd(ctx.ac.builder, merge_block);
      }

      /* This needs to be outside the if wrapping the shader body, as sometimes
       * the HW generates waves with 0 es/vs threads. */
      if (is_pre_gs_stage(shaders[shader_idx]->info.stage) &&
          args->options->key.vs_common_out.as_ngg && shader_idx == shader_count - 1) {
         handle_ngg_outputs_post_2(&ctx);
      } else if (shaders[shader_idx]->info.stage == MESA_SHADER_GEOMETRY &&
                 args->options->key.vs_common_out.as_ngg) {
         gfx10_ngg_gs_emit_epilogue_2(&ctx);
      }
   }

   LLVMBuildRetVoid(ctx.ac.builder);

   if (args->options->dump_preoptir) {
      fprintf(stderr, "%s LLVM IR:\n\n",
              radv_get_shader_name(args->shader_info, shaders[shader_count - 1]->info.stage));
      ac_dump_module(ctx.ac.module);
      fprintf(stderr, "\n");
   }

   ac_llvm_finalize_module(&ctx, ac_llvm->passmgr, args->options);

   if (shader_count == 1)
      ac_nir_eliminate_const_vs_outputs(&ctx);

   if (args->options->dump_shader) {
      args->shader_info->private_mem_vgprs = ac_count_scratch_private_memory(ctx.main_function);
   }

   return ctx.ac.module;
}

static void
ac_diagnostic_handler(LLVMDiagnosticInfoRef di, void *context)
{
   unsigned *retval = (unsigned *)context;
   LLVMDiagnosticSeverity severity = LLVMGetDiagInfoSeverity(di);
   char *description = LLVMGetDiagInfoDescription(di);

   if (severity == LLVMDSError) {
      *retval = 1;
      fprintf(stderr, "LLVM triggered Diagnostic Handler: %s\n", description);
   }

   LLVMDisposeMessage(description);
}

static unsigned
radv_llvm_compile(LLVMModuleRef M, char **pelf_buffer, size_t *pelf_size,
                  struct ac_llvm_compiler *ac_llvm)
{
   unsigned retval = 0;
   LLVMContextRef llvm_ctx;

   /* Setup Diagnostic Handler*/
   llvm_ctx = LLVMGetModuleContext(M);

   LLVMContextSetDiagnosticHandler(llvm_ctx, ac_diagnostic_handler, &retval);

   /* Compile IR*/
   if (!radv_compile_to_elf(ac_llvm, M, pelf_buffer, pelf_size))
      retval = 1;
   return retval;
}

static void
ac_compile_llvm_module(struct ac_llvm_compiler *ac_llvm, LLVMModuleRef llvm_module,
                       struct radv_shader_binary **rbinary, gl_shader_stage stage, const char *name,
                       const struct radv_nir_compiler_options *options)
{
   char *elf_buffer = NULL;
   size_t elf_size = 0;
   char *llvm_ir_string = NULL;

   if (options->dump_shader) {
      fprintf(stderr, "%s LLVM IR:\n\n", name);
      ac_dump_module(llvm_module);
      fprintf(stderr, "\n");
   }

   if (options->record_ir) {
      char *llvm_ir = LLVMPrintModuleToString(llvm_module);
      llvm_ir_string = strdup(llvm_ir);
      LLVMDisposeMessage(llvm_ir);
   }

   int v = radv_llvm_compile(llvm_module, &elf_buffer, &elf_size, ac_llvm);
   if (v) {
      fprintf(stderr, "compile failed\n");
   }

   LLVMContextRef ctx = LLVMGetModuleContext(llvm_module);
   LLVMDisposeModule(llvm_module);
   LLVMContextDispose(ctx);

   size_t llvm_ir_size = llvm_ir_string ? strlen(llvm_ir_string) : 0;
   size_t alloc_size = sizeof(struct radv_shader_binary_rtld) + elf_size + llvm_ir_size + 1;
   struct radv_shader_binary_rtld *rbin = calloc(1, alloc_size);
   memcpy(rbin->data, elf_buffer, elf_size);
   if (llvm_ir_string)
      memcpy(rbin->data + elf_size, llvm_ir_string, llvm_ir_size + 1);

   rbin->base.type = RADV_BINARY_TYPE_RTLD;
   rbin->base.stage = stage;
   rbin->base.total_size = alloc_size;
   rbin->elf_size = elf_size;
   rbin->llvm_ir_size = llvm_ir_size;
   *rbinary = &rbin->base;

   free(llvm_ir_string);
   free(elf_buffer);
}

static void
radv_compile_nir_shader(struct ac_llvm_compiler *ac_llvm, struct radv_shader_binary **rbinary,
                        const struct radv_shader_args *args, struct nir_shader *const *nir,
                        int nir_count)
{

   LLVMModuleRef llvm_module;

   llvm_module = ac_translate_nir_to_llvm(ac_llvm, nir, nir_count, args);

   ac_compile_llvm_module(ac_llvm, llvm_module, rbinary, nir[nir_count - 1]->info.stage,
                          radv_get_shader_name(args->shader_info, nir[nir_count - 1]->info.stage),
                          args->options);

   /* Determine the ES type (VS or TES) for the GS on GFX9. */
   if (args->options->chip_class >= GFX9) {
      if (nir_count == 2 && nir[1]->info.stage == MESA_SHADER_GEOMETRY) {
         args->shader_info->gs.es_type = nir[0]->info.stage;
      }
   }
}

static void
ac_gs_copy_shader_emit(struct radv_shader_context *ctx)
{
   LLVMValueRef vtx_offset =
      LLVMBuildMul(ctx->ac.builder, ac_get_arg(&ctx->ac, ctx->args->ac.vertex_id),
                   LLVMConstInt(ctx->ac.i32, 4, false), "");
   LLVMValueRef stream_id;

   /* Fetch the vertex stream ID. */
   if (ctx->args->shader_info->so.num_outputs) {
      stream_id =
         ac_unpack_param(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.streamout_config), 24, 2);
   } else {
      stream_id = ctx->ac.i32_0;
   }

   LLVMBasicBlockRef end_bb;
   LLVMValueRef switch_inst;

   end_bb = LLVMAppendBasicBlockInContext(ctx->ac.context, ctx->main_function, "end");
   switch_inst = LLVMBuildSwitch(ctx->ac.builder, stream_id, end_bb, 4);

   for (unsigned stream = 0; stream < 4; stream++) {
      unsigned num_components = ctx->args->shader_info->gs.num_stream_output_components[stream];
      LLVMBasicBlockRef bb;
      unsigned offset;

      if (stream > 0 && !num_components)
         continue;

      if (stream > 0 && !ctx->args->shader_info->so.num_outputs)
         continue;

      bb = LLVMInsertBasicBlockInContext(ctx->ac.context, end_bb, "out");
      LLVMAddCase(switch_inst, LLVMConstInt(ctx->ac.i32, stream, 0), bb);
      LLVMPositionBuilderAtEnd(ctx->ac.builder, bb);

      offset = 0;
      for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
         unsigned output_usage_mask = ctx->args->shader_info->gs.output_usage_mask[i];
         unsigned output_stream = ctx->args->shader_info->gs.output_streams[i];
         int length = util_last_bit(output_usage_mask);

         if (!(ctx->output_mask & (1ull << i)) || output_stream != stream)
            continue;

         for (unsigned j = 0; j < length; j++) {
            LLVMValueRef value, soffset;

            if (!(output_usage_mask & (1 << j)))
               continue;

            soffset = LLVMConstInt(ctx->ac.i32, offset * ctx->shader->info.gs.vertices_out * 16 * 4,
                                   false);

            offset++;

            value = ac_build_buffer_load(&ctx->ac, ctx->gsvs_ring[0], 1, ctx->ac.i32_0, vtx_offset,
                                         soffset, 0, ctx->ac.f32, ac_glc | ac_slc, true, false);

            LLVMTypeRef type = LLVMGetAllocatedType(ctx->abi.outputs[ac_llvm_reg_index_soa(i, j)]);
            if (ac_get_type_size(type) == 2) {
               value = LLVMBuildBitCast(ctx->ac.builder, value, ctx->ac.i32, "");
               value = LLVMBuildTrunc(ctx->ac.builder, value, ctx->ac.i16, "");
            }

            LLVMBuildStore(ctx->ac.builder, ac_to_float(&ctx->ac, value),
                           ctx->abi.outputs[ac_llvm_reg_index_soa(i, j)]);
         }
      }

      if (ctx->args->shader_info->so.num_outputs)
         radv_emit_streamout(ctx, stream);

      if (stream == 0) {
         handle_vs_outputs_post(ctx, false, true, &ctx->args->shader_info->vs.outinfo);
      }

      LLVMBuildBr(ctx->ac.builder, end_bb);
   }

   LLVMPositionBuilderAtEnd(ctx->ac.builder, end_bb);
}

static void
radv_compile_gs_copy_shader(struct ac_llvm_compiler *ac_llvm, struct nir_shader *geom_shader,
                            struct radv_shader_binary **rbinary,
                            const struct radv_shader_args *args)
{
   struct radv_shader_context ctx = {0};
   ctx.args = args;

   assert(args->is_gs_copy_shader);

   ac_llvm_context_init(&ctx.ac, ac_llvm, args->options->chip_class, args->options->family,
                        args->options->info, AC_FLOAT_MODE_DEFAULT, 64, 64);
   ctx.context = ctx.ac.context;

   ctx.stage = MESA_SHADER_VERTEX;
   ctx.shader = geom_shader;

   create_function(&ctx, MESA_SHADER_VERTEX, false);

   ac_setup_rings(&ctx);

   nir_foreach_shader_out_variable(variable, geom_shader)
   {
      scan_shader_output_decl(&ctx, variable, geom_shader, MESA_SHADER_VERTEX);
      ac_handle_shader_output_decl(&ctx.ac, &ctx.abi, geom_shader, variable, MESA_SHADER_VERTEX);
   }

   ac_gs_copy_shader_emit(&ctx);

   LLVMBuildRetVoid(ctx.ac.builder);

   ac_llvm_finalize_module(&ctx, ac_llvm->passmgr, args->options);

   ac_compile_llvm_module(ac_llvm, ctx.ac.module, rbinary, MESA_SHADER_VERTEX, "GS Copy Shader",
                          args->options);
   (*rbinary)->is_gs_copy_shader = true;
}

void
llvm_compile_shader(struct radv_device *device, unsigned shader_count,
                    struct nir_shader *const *shaders, struct radv_shader_binary **binary,
                    struct radv_shader_args *args)
{
   enum ac_target_machine_options tm_options = 0;
   struct ac_llvm_compiler ac_llvm;

   tm_options |= AC_TM_SUPPORTS_SPILL;
   if (args->options->check_ir)
      tm_options |= AC_TM_CHECK_IR;

   radv_init_llvm_compiler(&ac_llvm, args->options->family, tm_options,
                           args->shader_info->wave_size);

   if (args->is_gs_copy_shader) {
      radv_compile_gs_copy_shader(&ac_llvm, *shaders, binary, args);
   } else {
      radv_compile_nir_shader(&ac_llvm, binary, args, shaders, shader_count);
   }
}
