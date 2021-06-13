/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "si_pipe.h"
#include "si_shader_internal.h"
#include "sid.h"

static LLVMValueRef get_rel_patch_id(struct si_shader_context *ctx)
{
   switch (ctx->stage) {
   case MESA_SHADER_TESS_CTRL:
      return si_unpack_param(ctx, ctx->args.tcs_rel_ids, 0, 8);

   case MESA_SHADER_TESS_EVAL:
      return ac_get_arg(&ctx->ac, ctx->args.tes_rel_patch_id);

   default:
      assert(0);
      return NULL;
   }
}

/* Tessellation shaders pass outputs to the next shader using LDS.
 *
 * LS outputs = TCS inputs
 * TCS outputs = TES inputs
 *
 * The LDS layout is:
 * - TCS inputs for patch 0
 * - TCS inputs for patch 1
 * - TCS inputs for patch 2		= get_tcs_in_current_patch_offset (if RelPatchID==2)
 * - ...
 * - TCS outputs for patch 0            = get_tcs_out_patch0_offset
 * - Per-patch TCS outputs for patch 0  = get_tcs_out_patch0_patch_data_offset
 * - TCS outputs for patch 1
 * - Per-patch TCS outputs for patch 1
 * - TCS outputs for patch 2            = get_tcs_out_current_patch_offset (if RelPatchID==2)
 * - Per-patch TCS outputs for patch 2  = get_tcs_out_current_patch_data_offset (if RelPatchID==2)
 * - ...
 *
 * All three shaders VS(LS), TCS, TES share the same LDS space.
 */

static LLVMValueRef get_tcs_in_patch_stride(struct si_shader_context *ctx)
{
   return si_unpack_param(ctx, ctx->vs_state_bits, 11, 13);
}

static unsigned get_tcs_out_vertex_dw_stride_constant(struct si_shader_context *ctx)
{
   assert(ctx->stage == MESA_SHADER_TESS_CTRL);

   if (ctx->shader->key.mono.u.ff_tcs_inputs_to_copy)
      return util_last_bit64(ctx->shader->key.mono.u.ff_tcs_inputs_to_copy) * 4;

   return util_last_bit64(ctx->shader->selector->outputs_written) * 4;
}

static LLVMValueRef get_tcs_out_vertex_dw_stride(struct si_shader_context *ctx)
{
   unsigned stride = get_tcs_out_vertex_dw_stride_constant(ctx);

   return LLVMConstInt(ctx->ac.i32, stride, 0);
}

static LLVMValueRef get_tcs_out_patch_stride(struct si_shader_context *ctx)
{
   if (ctx->shader->key.mono.u.ff_tcs_inputs_to_copy)
      return si_unpack_param(ctx, ctx->tcs_out_lds_layout, 0, 13);

   const struct si_shader_info *info = &ctx->shader->selector->info;
   unsigned tcs_out_vertices = info->base.tess.tcs_vertices_out;
   unsigned vertex_dw_stride = get_tcs_out_vertex_dw_stride_constant(ctx);
   unsigned num_patch_outputs = util_last_bit64(ctx->shader->selector->patch_outputs_written);
   unsigned patch_dw_stride = tcs_out_vertices * vertex_dw_stride + num_patch_outputs * 4;
   return LLVMConstInt(ctx->ac.i32, patch_dw_stride, 0);
}

static LLVMValueRef get_tcs_out_patch0_offset(struct si_shader_context *ctx)
{
   return LLVMBuildMul(ctx->ac.builder, si_unpack_param(ctx, ctx->tcs_out_lds_offsets, 0, 16),
                       LLVMConstInt(ctx->ac.i32, 4, 0), "");
}

static LLVMValueRef get_tcs_out_patch0_patch_data_offset(struct si_shader_context *ctx)
{
   return LLVMBuildMul(ctx->ac.builder, si_unpack_param(ctx, ctx->tcs_out_lds_offsets, 16, 16),
                       LLVMConstInt(ctx->ac.i32, 4, 0), "");
}

static LLVMValueRef get_tcs_in_current_patch_offset(struct si_shader_context *ctx)
{
   LLVMValueRef patch_stride = get_tcs_in_patch_stride(ctx);
   LLVMValueRef rel_patch_id = get_rel_patch_id(ctx);

   return LLVMBuildMul(ctx->ac.builder, patch_stride, rel_patch_id, "");
}

static LLVMValueRef get_tcs_out_current_patch_offset(struct si_shader_context *ctx)
{
   LLVMValueRef patch0_offset = get_tcs_out_patch0_offset(ctx);
   LLVMValueRef patch_stride = get_tcs_out_patch_stride(ctx);
   LLVMValueRef rel_patch_id = get_rel_patch_id(ctx);

   return ac_build_imad(&ctx->ac, patch_stride, rel_patch_id, patch0_offset);
}

static LLVMValueRef get_tcs_out_current_patch_data_offset(struct si_shader_context *ctx)
{
   LLVMValueRef patch0_patch_data_offset = get_tcs_out_patch0_patch_data_offset(ctx);
   LLVMValueRef patch_stride = get_tcs_out_patch_stride(ctx);
   LLVMValueRef rel_patch_id = get_rel_patch_id(ctx);

   return ac_build_imad(&ctx->ac, patch_stride, rel_patch_id, patch0_patch_data_offset);
}

static LLVMValueRef get_num_tcs_out_vertices(struct si_shader_context *ctx)
{
   unsigned tcs_out_vertices =
      ctx->shader->selector ? ctx->shader->selector->info.base.tess.tcs_vertices_out
                            : 0;

   /* If !tcs_out_vertices, it's either the fixed-func TCS or the TCS epilog. */
   if (ctx->stage == MESA_SHADER_TESS_CTRL && tcs_out_vertices)
      return LLVMConstInt(ctx->ac.i32, tcs_out_vertices, 0);

   return LLVMBuildAdd(ctx->ac.builder,
                       si_unpack_param(ctx, ctx->tcs_offchip_layout, 6, 5), ctx->ac.i32_1, "");
}

static LLVMValueRef get_tcs_in_vertex_dw_stride(struct si_shader_context *ctx)
{
   unsigned stride;

   switch (ctx->stage) {
   case MESA_SHADER_VERTEX:
      stride = ctx->shader->selector->lshs_vertex_stride / 4;
      return LLVMConstInt(ctx->ac.i32, stride, 0);

   case MESA_SHADER_TESS_CTRL:
      if (ctx->screen->info.chip_class >= GFX9 && ctx->shader->is_monolithic) {
         stride = ctx->shader->key.part.tcs.ls->lshs_vertex_stride / 4;
         return LLVMConstInt(ctx->ac.i32, stride, 0);
      }
      return si_unpack_param(ctx, ctx->vs_state_bits, 24, 8);

   default:
      assert(0);
      return NULL;
   }
}

static LLVMValueRef
get_dw_address_from_generic_indices(struct si_shader_context *ctx, LLVMValueRef vertex_dw_stride,
                                    LLVMValueRef base_addr, LLVMValueRef vertex_index,
                                    LLVMValueRef param_index, ubyte name)
{
   if (vertex_dw_stride) {
      base_addr = ac_build_imad(&ctx->ac, vertex_index, vertex_dw_stride, base_addr);
   }

   if (param_index) {
      base_addr = ac_build_imad(&ctx->ac, param_index, LLVMConstInt(ctx->ac.i32, 4, 0), base_addr);
   }

   int param = name >= VARYING_SLOT_PATCH0 ||
               name == VARYING_SLOT_TESS_LEVEL_INNER ||
               name == VARYING_SLOT_TESS_LEVEL_OUTER
                  ? si_shader_io_get_unique_index_patch(name)
                  : si_shader_io_get_unique_index(name, false);

   /* Add the base address of the element. */
   return LLVMBuildAdd(ctx->ac.builder, base_addr, LLVMConstInt(ctx->ac.i32, param * 4, 0), "");
}

/* The offchip buffer layout for TCS->TES is
 *
 * - attribute 0 of patch 0 vertex 0
 * - attribute 0 of patch 0 vertex 1
 * - attribute 0 of patch 0 vertex 2
 *   ...
 * - attribute 0 of patch 1 vertex 0
 * - attribute 0 of patch 1 vertex 1
 *   ...
 * - attribute 1 of patch 0 vertex 0
 * - attribute 1 of patch 0 vertex 1
 *   ...
 * - per patch attribute 0 of patch 0
 * - per patch attribute 0 of patch 1
 *   ...
 *
 * Note that every attribute has 4 components.
 */
static LLVMValueRef get_tcs_tes_buffer_address(struct si_shader_context *ctx,
                                               LLVMValueRef rel_patch_id, LLVMValueRef vertex_index,
                                               LLVMValueRef param_index)
{
   LLVMValueRef base_addr, vertices_per_patch, num_patches, total_vertices;
   LLVMValueRef param_stride, constant16;

   vertices_per_patch = get_num_tcs_out_vertices(ctx);
   num_patches = si_unpack_param(ctx, ctx->tcs_offchip_layout, 0, 6);
   num_patches = LLVMBuildAdd(ctx->ac.builder, num_patches, ctx->ac.i32_1, "");
   total_vertices = LLVMBuildMul(ctx->ac.builder, vertices_per_patch, num_patches, "");

   constant16 = LLVMConstInt(ctx->ac.i32, 16, 0);
   if (vertex_index) {
      base_addr = ac_build_imad(&ctx->ac, rel_patch_id, vertices_per_patch, vertex_index);
      param_stride = total_vertices;
   } else {
      base_addr = rel_patch_id;
      param_stride = num_patches;
   }

   base_addr = ac_build_imad(&ctx->ac, param_index, param_stride, base_addr);
   base_addr = LLVMBuildMul(ctx->ac.builder, base_addr, constant16, "");

   if (!vertex_index) {
      LLVMValueRef patch_data_offset = si_unpack_param(ctx, ctx->tcs_offchip_layout, 11, 21);

      base_addr = LLVMBuildAdd(ctx->ac.builder, base_addr, patch_data_offset, "");
   }
   return base_addr;
}

static LLVMValueRef get_tcs_tes_buffer_address_from_generic_indices(struct si_shader_context *ctx,
                                                                    LLVMValueRef vertex_index,
                                                                    LLVMValueRef param_index,
                                                                    ubyte name)
{
   unsigned param_index_base;

   param_index_base = name >= VARYING_SLOT_PATCH0 ||
                      name == VARYING_SLOT_TESS_LEVEL_INNER ||
                      name == VARYING_SLOT_TESS_LEVEL_OUTER
                         ? si_shader_io_get_unique_index_patch(name)
                         : si_shader_io_get_unique_index(name, false);

   if (param_index) {
      param_index = LLVMBuildAdd(ctx->ac.builder, param_index,
                                 LLVMConstInt(ctx->ac.i32, param_index_base, 0), "");
   } else {
      param_index = LLVMConstInt(ctx->ac.i32, param_index_base, 0);
   }

   return get_tcs_tes_buffer_address(ctx, get_rel_patch_id(ctx), vertex_index, param_index);
}

static LLVMValueRef buffer_load(struct si_shader_context *ctx, LLVMTypeRef type, unsigned swizzle,
                                LLVMValueRef buffer, LLVMValueRef offset, LLVMValueRef base,
                                bool can_speculate)
{
   LLVMValueRef value;
   LLVMTypeRef vec_type = LLVMVectorType(type, 4);

   if (swizzle == ~0) {
      value = ac_build_buffer_load(&ctx->ac, buffer, 4, NULL, base, offset, 0, type, ac_glc,
                                   can_speculate, false);

      return LLVMBuildBitCast(ctx->ac.builder, value, vec_type, "");
   }

   value = ac_build_buffer_load(&ctx->ac, buffer, 4, NULL, base, offset, 0, type, ac_glc,
                                can_speculate, false);

   value = LLVMBuildBitCast(ctx->ac.builder, value, vec_type, "");
   return LLVMBuildExtractElement(ctx->ac.builder, value, LLVMConstInt(ctx->ac.i32, swizzle, 0),
                                  "");
}

/**
 * Load from LSHS LDS storage.
 *
 * \param type		output value type
 * \param swizzle	offset (typically 0..3); it can be ~0, which loads a vec4
 * \param dw_addr	address in dwords
 */
static LLVMValueRef lshs_lds_load(struct si_shader_context *ctx, LLVMTypeRef type, unsigned swizzle,
                                  LLVMValueRef dw_addr)
{
   LLVMValueRef value;

   if (swizzle == ~0) {
      LLVMValueRef values[4];

      for (unsigned chan = 0; chan < 4; chan++)
         values[chan] = lshs_lds_load(ctx, type, chan, dw_addr);

      return ac_build_gather_values(&ctx->ac, values, 4);
   }

   dw_addr = LLVMBuildAdd(ctx->ac.builder, dw_addr, LLVMConstInt(ctx->ac.i32, swizzle, 0), "");
   value = ac_lds_load(&ctx->ac, dw_addr);
   return LLVMBuildBitCast(ctx->ac.builder, value, type, "");
}

/**
 * Store to LSHS LDS storage.
 *
 * \param swizzle	offset (typically 0..3)
 * \param dw_addr	address in dwords
 * \param value		value to store
 */
static void lshs_lds_store(struct si_shader_context *ctx, unsigned dw_offset_imm,
                           LLVMValueRef dw_addr, LLVMValueRef value)
{
   dw_addr =
      LLVMBuildAdd(ctx->ac.builder, dw_addr, LLVMConstInt(ctx->ac.i32, dw_offset_imm, 0), "");

   ac_lds_store(&ctx->ac, dw_addr, value);
}

enum si_tess_ring
{
   TCS_FACTOR_RING,
   TESS_OFFCHIP_RING_TCS,
   TESS_OFFCHIP_RING_TES,
};

static LLVMValueRef get_tess_ring_descriptor(struct si_shader_context *ctx, enum si_tess_ring ring)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef addr = ac_get_arg(
      &ctx->ac, ring == TESS_OFFCHIP_RING_TES ? ctx->tes_offchip_addr : ctx->tcs_out_lds_layout);

   /* TCS only receives high 13 bits of the address. */
   if (ring == TESS_OFFCHIP_RING_TCS || ring == TCS_FACTOR_RING) {
      addr = LLVMBuildAnd(builder, addr, LLVMConstInt(ctx->ac.i32, 0xfff80000, 0), "");
   }

   if (ring == TCS_FACTOR_RING) {
      unsigned tf_offset = ctx->screen->tess_offchip_ring_size;
      addr = LLVMBuildAdd(builder, addr, LLVMConstInt(ctx->ac.i32, tf_offset, 0), "");
   }

   uint32_t rsrc3 = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                    S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

   if (ctx->screen->info.chip_class >= GFX10)
      rsrc3 |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
               S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW) | S_008F0C_RESOURCE_LEVEL(1);
   else
      rsrc3 |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
               S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);

   LLVMValueRef desc[4];
   desc[0] = addr;
   desc[1] = LLVMConstInt(ctx->ac.i32, S_008F04_BASE_ADDRESS_HI(ctx->screen->info.address32_hi), 0);
   desc[2] = LLVMConstInt(ctx->ac.i32, 0xffffffff, 0);
   desc[3] = LLVMConstInt(ctx->ac.i32, rsrc3, false);

   return ac_build_gather_values(&ctx->ac, desc, 4);
}

void si_llvm_preload_tes_rings(struct si_shader_context *ctx)
{
   ctx->tess_offchip_ring = get_tess_ring_descriptor(ctx, TESS_OFFCHIP_RING_TES);
}

static LLVMValueRef si_nir_load_tcs_varyings(struct ac_shader_abi *abi, LLVMTypeRef type,
                                             LLVMValueRef vertex_index, LLVMValueRef param_index,
                                             unsigned driver_location, unsigned component,
                                             unsigned num_components, bool load_input,
                                             bool vertex_index_is_invoc_id)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   struct si_shader_info *info = &ctx->shader->selector->info;
   LLVMValueRef dw_addr, stride;
   ubyte semantic;

   if (load_input) {
      semantic = info->input_semantic[driver_location];
   } else {
      semantic = info->output_semantic[driver_location];
   }

   /* Load the TCS input from a VGPR if possible. */
   if (ctx->shader->key.opt.same_patch_vertices &&
       load_input && vertex_index_is_invoc_id && !param_index) {
      unsigned func_param = ctx->args.tcs_rel_ids.arg_index + 1 +
                            si_shader_io_get_unique_index(semantic, false) * 4;
      LLVMValueRef value[4];

      for (unsigned i = component; i < component + num_components; i++) {
         value[i] = LLVMGetParam(ctx->main_fn, func_param + i);
         value[i] = LLVMBuildBitCast(ctx->ac.builder, value[i], type, "");
      }

      return ac_build_varying_gather_values(&ctx->ac, value, num_components, component);
   }

   bool is_patch = vertex_index == NULL;
   assert((semantic >= VARYING_SLOT_PATCH0 ||
           semantic == VARYING_SLOT_TESS_LEVEL_INNER ||
           semantic == VARYING_SLOT_TESS_LEVEL_OUTER) == is_patch);

   if (load_input) {
      stride = get_tcs_in_vertex_dw_stride(ctx);
      dw_addr = get_tcs_in_current_patch_offset(ctx);
   } else {
      if (is_patch) {
         stride = NULL;
         dw_addr = get_tcs_out_current_patch_data_offset(ctx);
      } else {
         stride = get_tcs_out_vertex_dw_stride(ctx);
         dw_addr = get_tcs_out_current_patch_offset(ctx);
      }
   }

   dw_addr = get_dw_address_from_generic_indices(ctx, stride, dw_addr, vertex_index, param_index,
                                                 semantic);

   LLVMValueRef value[4];
   for (unsigned i = component; i < component + num_components; i++)
      value[i] = lshs_lds_load(ctx, type, i, dw_addr);

   return ac_build_varying_gather_values(&ctx->ac, value, num_components, component);
}

static LLVMValueRef si_nir_load_input_tes(struct ac_shader_abi *abi, LLVMTypeRef type,
                                          LLVMValueRef vertex_index, LLVMValueRef param_index,
                                          unsigned driver_location, unsigned component,
                                          unsigned num_components,
                                          bool load_input, bool vertex_index_is_invoc_id)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   struct si_shader_info *info = &ctx->shader->selector->info;
   LLVMValueRef base, addr;

   ubyte semantic = info->input_semantic[driver_location];

   assert((semantic >= VARYING_SLOT_PATCH0 ||
           semantic == VARYING_SLOT_TESS_LEVEL_INNER ||
           semantic == VARYING_SLOT_TESS_LEVEL_OUTER) == (vertex_index == NULL));

   base = ac_get_arg(&ctx->ac, ctx->args.tess_offchip_offset);

   addr =
      get_tcs_tes_buffer_address_from_generic_indices(ctx, vertex_index, param_index, semantic);

   /* TODO: This will generate rather ordinary llvm code, although it
    * should be easy for the optimizer to fix up. In future we might want
    * to refactor buffer_load().
    */
   LLVMValueRef value[4];
   for (unsigned i = component; i < component + num_components; i++)
      value[i] = buffer_load(ctx, type, i, ctx->tess_offchip_ring, base, addr, true);

   return ac_build_varying_gather_values(&ctx->ac, value, num_components, component);
}

static void si_nir_store_output_tcs(struct ac_shader_abi *abi,
                                    LLVMValueRef vertex_index, LLVMValueRef param_index,
                                    LLVMValueRef src, unsigned writemask,
                                    unsigned component, unsigned location, unsigned driver_location)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   struct si_shader_info *info = &ctx->shader->selector->info;
   LLVMValueRef dw_addr, stride;
   LLVMValueRef buffer, base, addr;
   LLVMValueRef values[8];
   bool is_tess_factor = false, is_tess_inner = false;

   ubyte semantic = info->output_semantic[driver_location];

   const bool is_const = !param_index;
   const bool is_patch = vertex_index == NULL;

   /* Invalid SPIR-V can cause this. */
   if ((semantic >= VARYING_SLOT_PATCH0 || semantic == VARYING_SLOT_TESS_LEVEL_INNER ||
        semantic == VARYING_SLOT_TESS_LEVEL_OUTER) != is_patch)
      return;

   if (!is_patch) {
      stride = get_tcs_out_vertex_dw_stride(ctx);
      dw_addr = get_tcs_out_current_patch_offset(ctx);
      dw_addr = get_dw_address_from_generic_indices(ctx, stride, dw_addr, vertex_index, param_index,
                                                    semantic);
   } else {
      dw_addr = get_tcs_out_current_patch_data_offset(ctx);
      dw_addr = get_dw_address_from_generic_indices(ctx, NULL, dw_addr, vertex_index, param_index,
                                                    semantic);

      if (is_const) {
         int semantic = info->output_semantic[driver_location];

         /* Always write tess factors into LDS for the TCS epilog. */
         if (semantic == VARYING_SLOT_TESS_LEVEL_INNER ||
             semantic == VARYING_SLOT_TESS_LEVEL_OUTER) {
            is_tess_factor = true;
            is_tess_inner = semantic == VARYING_SLOT_TESS_LEVEL_INNER;
         }
      }
   }

   buffer = get_tess_ring_descriptor(ctx, TESS_OFFCHIP_RING_TCS);

   base = ac_get_arg(&ctx->ac, ctx->args.tess_offchip_offset);

   addr =
      get_tcs_tes_buffer_address_from_generic_indices(ctx, vertex_index, param_index, semantic);

   for (unsigned chan = component; chan < 4; chan++) {
      if (!(writemask & (1 << chan)))
         continue;
      LLVMValueRef value = ac_llvm_extract_elem(&ctx->ac, src, chan - component);

      /* Skip LDS stores if there is no LDS read of this output. */
      if (info->output_readmask[driver_location] & (1 << chan) ||
          /* The epilog reads LDS if invocation 0 doesn't define tess factors. */
          (is_tess_factor &&
           !ctx->shader->selector->info.tessfactors_are_def_in_all_invocs))
         lshs_lds_store(ctx, chan, dw_addr, value);

      value = ac_to_integer(&ctx->ac, value);
      values[chan] = value;

      if (writemask != 0xF && !is_tess_factor) {
         ac_build_buffer_store_dword(&ctx->ac, buffer, value, 1, addr, base,
                                     4 * chan, ac_glc);
      }

      /* Write tess factors into VGPRs for the epilog. */
      if (is_tess_factor && ctx->shader->selector->info.tessfactors_are_def_in_all_invocs) {
         if (!is_tess_inner) {
            LLVMBuildStore(ctx->ac.builder, value, /* outer */
                           ctx->invoc0_tess_factors[chan]);
         } else if (chan < 2) {
            LLVMBuildStore(ctx->ac.builder, value, /* inner */
                           ctx->invoc0_tess_factors[4 + chan]);
         }
      }
   }

   if (writemask == 0xF && !is_tess_factor) {
      LLVMValueRef value = ac_build_gather_values(&ctx->ac, values, 4);
      ac_build_buffer_store_dword(&ctx->ac, buffer, value, 4, addr, base, 0, ac_glc);
   }
}

static LLVMValueRef si_load_tess_coord(struct ac_shader_abi *abi)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   LLVMValueRef coord[4] = {ac_get_arg(&ctx->ac, ctx->args.tes_u),
                            ac_get_arg(&ctx->ac, ctx->args.tes_v),
                            ctx->ac.f32_0, ctx->ac.f32_0};

   /* For triangles, the vector should be (u, v, 1-u-v). */
   if (ctx->shader->selector->info.base.tess.primitive_mode == GL_TRIANGLES) {
      coord[2] = LLVMBuildFSub(ctx->ac.builder, ctx->ac.f32_1,
                               LLVMBuildFAdd(ctx->ac.builder, coord[0], coord[1], ""), "");
   }
   return ac_build_gather_values(&ctx->ac, coord, 4);
}

static LLVMValueRef load_tess_level(struct si_shader_context *ctx, unsigned semantic)
{
   LLVMValueRef base, addr;

   int param = si_shader_io_get_unique_index_patch(semantic);

   base = ac_get_arg(&ctx->ac, ctx->args.tess_offchip_offset);
   addr = get_tcs_tes_buffer_address(ctx, get_rel_patch_id(ctx), NULL,
                                     LLVMConstInt(ctx->ac.i32, param, 0));

   return buffer_load(ctx, ctx->ac.f32, ~0, ctx->tess_offchip_ring, base, addr, true);
}

static LLVMValueRef load_tess_level_default(struct si_shader_context *ctx, unsigned sysval)
{
   LLVMValueRef buf, slot, val[4];
   int i, offset;

   slot = LLVMConstInt(ctx->ac.i32, SI_HS_CONST_DEFAULT_TESS_LEVELS, 0);
   buf = ac_get_arg(&ctx->ac, ctx->internal_bindings);
   buf = ac_build_load_to_sgpr(&ctx->ac, buf, slot);
   offset = sysval == SYSTEM_VALUE_TESS_LEVEL_INNER_DEFAULT ? 4 : 0;

   for (i = 0; i < 4; i++)
      val[i] = si_buffer_load_const(ctx, buf, LLVMConstInt(ctx->ac.i32, (offset + i) * 4, 0));
   return ac_build_gather_values(&ctx->ac, val, 4);
}

static LLVMValueRef si_load_tess_level(struct ac_shader_abi *abi, unsigned varying_id,
                                       bool load_default_state)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   unsigned semantic;

   if (load_default_state) {
      switch (varying_id) {
      case VARYING_SLOT_TESS_LEVEL_INNER:
         semantic = SYSTEM_VALUE_TESS_LEVEL_INNER_DEFAULT;
         break;
      case VARYING_SLOT_TESS_LEVEL_OUTER:
         semantic = SYSTEM_VALUE_TESS_LEVEL_OUTER_DEFAULT;
         break;
      default:
         unreachable("unknown tess level");
      }
      return load_tess_level_default(ctx, semantic);
   }

   switch (varying_id) {
   case VARYING_SLOT_TESS_LEVEL_INNER:
      semantic = VARYING_SLOT_TESS_LEVEL_INNER;
      break;
   case VARYING_SLOT_TESS_LEVEL_OUTER:
      semantic = VARYING_SLOT_TESS_LEVEL_OUTER;
      break;
   default:
      unreachable("unknown tess level");
   }

   return load_tess_level(ctx, semantic);
}

static LLVMValueRef si_load_patch_vertices_in(struct ac_shader_abi *abi)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   if (ctx->stage == MESA_SHADER_TESS_CTRL)
      return si_unpack_param(ctx, ctx->tcs_out_lds_layout, 13, 6);
   else if (ctx->stage == MESA_SHADER_TESS_EVAL)
      return get_num_tcs_out_vertices(ctx);
   else
      unreachable("invalid shader stage for VERTICESIN");
}

/**
 * Forward all outputs from the vertex shader to the TES. This is only used
 * for the fixed function TCS.
 */
static void si_copy_tcs_inputs(struct si_shader_context *ctx)
{
   LLVMValueRef invocation_id, buffer, buffer_offset;
   LLVMValueRef lds_vertex_stride, lds_base;
   uint64_t inputs;

   invocation_id = si_unpack_param(ctx, ctx->args.tcs_rel_ids, 8, 5);
   buffer = get_tess_ring_descriptor(ctx, TESS_OFFCHIP_RING_TCS);
   buffer_offset = ac_get_arg(&ctx->ac, ctx->args.tess_offchip_offset);

   lds_vertex_stride = get_tcs_in_vertex_dw_stride(ctx);
   lds_base = get_tcs_in_current_patch_offset(ctx);
   lds_base = ac_build_imad(&ctx->ac, invocation_id, lds_vertex_stride, lds_base);

   inputs = ctx->shader->key.mono.u.ff_tcs_inputs_to_copy;
   while (inputs) {
      unsigned i = u_bit_scan64(&inputs);

      LLVMValueRef lds_ptr =
         LLVMBuildAdd(ctx->ac.builder, lds_base, LLVMConstInt(ctx->ac.i32, 4 * i, 0), "");

      LLVMValueRef buffer_addr = get_tcs_tes_buffer_address(
         ctx, get_rel_patch_id(ctx), invocation_id, LLVMConstInt(ctx->ac.i32, i, 0));

      LLVMValueRef value = lshs_lds_load(ctx, ctx->ac.i32, ~0, lds_ptr);

      ac_build_buffer_store_dword(&ctx->ac, buffer, value, 4, buffer_addr, buffer_offset, 0,
                                  ac_glc);
   }
}

static void si_write_tess_factors(struct si_shader_context *ctx, LLVMValueRef rel_patch_id,
                                  LLVMValueRef invocation_id,
                                  LLVMValueRef tcs_out_current_patch_data_offset,
                                  LLVMValueRef invoc0_tf_outer[4], LLVMValueRef invoc0_tf_inner[2])
{
   struct si_shader *shader = ctx->shader;
   unsigned tess_inner_index, tess_outer_index;
   LLVMValueRef lds_base, lds_inner, lds_outer, byteoffset, buffer;
   LLVMValueRef out[6], vec0, vec1, tf_base, inner[4], outer[4];
   unsigned stride, outer_comps, inner_comps, i, offset;

   /* Add a barrier before loading tess factors from LDS. */
   if (!shader->key.part.tcs.epilog.invoc0_tess_factors_are_def)
      si_llvm_emit_barrier(ctx);

   /* Do this only for invocation 0, because the tess levels are per-patch,
    * not per-vertex.
    *
    * This can't jump, because invocation 0 executes this. It should
    * at least mask out the loads and stores for other invocations.
    */
   ac_build_ifcc(&ctx->ac,
                 LLVMBuildICmp(ctx->ac.builder, LLVMIntEQ, invocation_id, ctx->ac.i32_0, ""), 6503);

   /* Determine the layout of one tess factor element in the buffer. */
   switch (shader->key.part.tcs.epilog.prim_mode) {
   case GL_LINES:
      stride = 2; /* 2 dwords, 1 vec2 store */
      outer_comps = 2;
      inner_comps = 0;
      break;
   case GL_TRIANGLES:
      stride = 4; /* 4 dwords, 1 vec4 store */
      outer_comps = 3;
      inner_comps = 1;
      break;
   case GL_QUADS:
      stride = 6; /* 6 dwords, 2 stores (vec4 + vec2) */
      outer_comps = 4;
      inner_comps = 2;
      break;
   default:
      assert(0);
      return;
   }

   for (i = 0; i < 4; i++) {
      inner[i] = LLVMGetUndef(ctx->ac.i32);
      outer[i] = LLVMGetUndef(ctx->ac.i32);
   }

   if (shader->key.part.tcs.epilog.invoc0_tess_factors_are_def) {
      /* Tess factors are in VGPRs. */
      for (i = 0; i < outer_comps; i++)
         outer[i] = out[i] = invoc0_tf_outer[i];
      for (i = 0; i < inner_comps; i++)
         inner[i] = out[outer_comps + i] = invoc0_tf_inner[i];
   } else {
      /* Load tess_inner and tess_outer from LDS.
       * Any invocation can write them, so we can't get them from a temporary.
       */
      tess_inner_index = si_shader_io_get_unique_index_patch(VARYING_SLOT_TESS_LEVEL_INNER);
      tess_outer_index = si_shader_io_get_unique_index_patch(VARYING_SLOT_TESS_LEVEL_OUTER);

      lds_base = tcs_out_current_patch_data_offset;
      lds_inner = LLVMBuildAdd(ctx->ac.builder, lds_base,
                               LLVMConstInt(ctx->ac.i32, tess_inner_index * 4, 0), "");
      lds_outer = LLVMBuildAdd(ctx->ac.builder, lds_base,
                               LLVMConstInt(ctx->ac.i32, tess_outer_index * 4, 0), "");

      for (i = 0; i < outer_comps; i++) {
         outer[i] = out[i] = lshs_lds_load(ctx, ctx->ac.i32, i, lds_outer);
      }
      for (i = 0; i < inner_comps; i++) {
         inner[i] = out[outer_comps + i] = lshs_lds_load(ctx, ctx->ac.i32, i, lds_inner);
      }
   }

   if (shader->key.part.tcs.epilog.prim_mode == GL_LINES) {
      /* For isolines, the hardware expects tess factors in the
       * reverse order from what NIR specifies.
       */
      LLVMValueRef tmp = out[0];
      out[0] = out[1];
      out[1] = tmp;
   }

   /* Convert the outputs to vectors for stores. */
   vec0 = ac_build_gather_values(&ctx->ac, out, MIN2(stride, 4));
   vec1 = NULL;

   if (stride > 4)
      vec1 = ac_build_gather_values(&ctx->ac, out + 4, stride - 4);

   /* Get the buffer. */
   buffer = get_tess_ring_descriptor(ctx, TCS_FACTOR_RING);

   /* Get the offset. */
   tf_base = ac_get_arg(&ctx->ac, ctx->args.tcs_factor_offset);
   byteoffset =
      LLVMBuildMul(ctx->ac.builder, rel_patch_id, LLVMConstInt(ctx->ac.i32, 4 * stride, 0), "");
   offset = 0;

   /* Store the dynamic HS control word. */
   if (ctx->screen->info.chip_class <= GFX8) {
      ac_build_ifcc(&ctx->ac,
                    LLVMBuildICmp(ctx->ac.builder, LLVMIntEQ, rel_patch_id, ctx->ac.i32_0, ""), 6504);
      ac_build_buffer_store_dword(&ctx->ac, buffer, LLVMConstInt(ctx->ac.i32, 0x80000000, 0), 1,
                                  ctx->ac.i32_0, tf_base, offset, ac_glc);
      ac_build_endif(&ctx->ac, 6504);
      offset += 4;
   }

   /* Store the tessellation factors. */
   ac_build_buffer_store_dword(&ctx->ac, buffer, vec0, MIN2(stride, 4), byteoffset, tf_base, offset,
                               ac_glc);
   offset += 16;
   if (vec1)
      ac_build_buffer_store_dword(&ctx->ac, buffer, vec1, stride - 4, byteoffset, tf_base, offset,
                                  ac_glc);

   /* Store the tess factors into the offchip buffer if TES reads them. */
   if (shader->key.part.tcs.epilog.tes_reads_tess_factors) {
      LLVMValueRef buf, base, inner_vec, outer_vec, tf_outer_offset;
      LLVMValueRef tf_inner_offset;
      unsigned param_outer, param_inner;

      buf = get_tess_ring_descriptor(ctx, TESS_OFFCHIP_RING_TCS);
      base = ac_get_arg(&ctx->ac, ctx->args.tess_offchip_offset);

      param_outer = si_shader_io_get_unique_index_patch(VARYING_SLOT_TESS_LEVEL_OUTER);
      tf_outer_offset = get_tcs_tes_buffer_address(ctx, rel_patch_id, NULL,
                                                   LLVMConstInt(ctx->ac.i32, param_outer, 0));

      unsigned outer_vec_size = ac_has_vec3_support(ctx->screen->info.chip_class, false)
                                   ? outer_comps
                                   : util_next_power_of_two(outer_comps);
      outer_vec = ac_build_gather_values(&ctx->ac, outer, outer_vec_size);

      ac_build_buffer_store_dword(&ctx->ac, buf, outer_vec, outer_comps, tf_outer_offset, base, 0,
                                  ac_glc);
      if (inner_comps) {
         param_inner = si_shader_io_get_unique_index_patch(VARYING_SLOT_TESS_LEVEL_INNER);
         tf_inner_offset = get_tcs_tes_buffer_address(ctx, rel_patch_id, NULL,
                                                      LLVMConstInt(ctx->ac.i32, param_inner, 0));

         inner_vec =
            inner_comps == 1 ? inner[0] : ac_build_gather_values(&ctx->ac, inner, inner_comps);
         ac_build_buffer_store_dword(&ctx->ac, buf, inner_vec, inner_comps, tf_inner_offset, base,
                                     0, ac_glc);
      }
   }

   ac_build_endif(&ctx->ac, 6503);
}

/* This only writes the tessellation factor levels. */
static void si_llvm_emit_tcs_epilogue(struct ac_shader_abi *abi, unsigned max_outputs,
                                      LLVMValueRef *addrs)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef rel_patch_id, invocation_id, tf_lds_offset;

   si_copy_tcs_inputs(ctx);

   rel_patch_id = get_rel_patch_id(ctx);
   invocation_id = si_unpack_param(ctx, ctx->args.tcs_rel_ids, 8, 5);
   tf_lds_offset = get_tcs_out_current_patch_data_offset(ctx);

   if (ctx->screen->info.chip_class >= GFX9 && !ctx->shader->is_monolithic) {
      LLVMBasicBlockRef blocks[2] = {LLVMGetInsertBlock(builder), ctx->merged_wrap_if_entry_block};
      LLVMValueRef values[2];

      ac_build_endif(&ctx->ac, ctx->merged_wrap_if_label);

      values[0] = rel_patch_id;
      values[1] = LLVMGetUndef(ctx->ac.i32);
      rel_patch_id = ac_build_phi(&ctx->ac, ctx->ac.i32, 2, values, blocks);

      values[0] = tf_lds_offset;
      values[1] = LLVMGetUndef(ctx->ac.i32);
      tf_lds_offset = ac_build_phi(&ctx->ac, ctx->ac.i32, 2, values, blocks);

      values[0] = invocation_id;
      values[1] = ctx->ac.i32_1; /* cause the epilog to skip threads */
      invocation_id = ac_build_phi(&ctx->ac, ctx->ac.i32, 2, values, blocks);
   }

   /* Return epilog parameters from this function. */
   LLVMValueRef ret = ctx->return_value;
   unsigned vgpr;

   if (ctx->screen->info.chip_class >= GFX9) {
      ret =
         si_insert_input_ret(ctx, ret, ctx->tcs_offchip_layout, 8 + GFX9_SGPR_TCS_OFFCHIP_LAYOUT);
      ret = si_insert_input_ret(ctx, ret, ctx->tcs_out_lds_layout, 8 + GFX9_SGPR_TCS_OUT_LAYOUT);
      /* Tess offchip and tess factor offsets are at the beginning. */
      ret = si_insert_input_ret(ctx, ret, ctx->args.tess_offchip_offset, 2);
      ret = si_insert_input_ret(ctx, ret, ctx->args.tcs_factor_offset, 4);
      vgpr = 8 + GFX9_SGPR_TCS_OUT_LAYOUT + 1;
   } else {
      ret = si_insert_input_ret(ctx, ret, ctx->tcs_offchip_layout, GFX6_SGPR_TCS_OFFCHIP_LAYOUT);
      ret = si_insert_input_ret(ctx, ret, ctx->tcs_out_lds_layout, GFX6_SGPR_TCS_OUT_LAYOUT);
      /* Tess offchip and tess factor offsets are after user SGPRs. */
      ret = si_insert_input_ret(ctx, ret, ctx->args.tess_offchip_offset, GFX6_TCS_NUM_USER_SGPR);
      ret = si_insert_input_ret(ctx, ret, ctx->args.tcs_factor_offset, GFX6_TCS_NUM_USER_SGPR + 1);
      vgpr = GFX6_TCS_NUM_USER_SGPR + 2;
   }

   /* VGPRs */
   rel_patch_id = ac_to_float(&ctx->ac, rel_patch_id);
   invocation_id = ac_to_float(&ctx->ac, invocation_id);
   tf_lds_offset = ac_to_float(&ctx->ac, tf_lds_offset);

   /* Leave a hole corresponding to the two input VGPRs. This ensures that
    * the invocation_id output does not alias the tcs_rel_ids input,
    * which saves a V_MOV on gfx9.
    */
   vgpr += 2;

   ret = LLVMBuildInsertValue(builder, ret, rel_patch_id, vgpr++, "");
   ret = LLVMBuildInsertValue(builder, ret, invocation_id, vgpr++, "");

   if (ctx->shader->selector->info.tessfactors_are_def_in_all_invocs) {
      vgpr++; /* skip the tess factor LDS offset */
      for (unsigned i = 0; i < 6; i++) {
         LLVMValueRef value = LLVMBuildLoad(builder, ctx->invoc0_tess_factors[i], "");
         value = ac_to_float(&ctx->ac, value);
         ret = LLVMBuildInsertValue(builder, ret, value, vgpr++, "");
      }
   } else {
      ret = LLVMBuildInsertValue(builder, ret, tf_lds_offset, vgpr++, "");
   }
   ctx->return_value = ret;
}

/* Pass TCS inputs from LS to TCS on GFX9. */
static void si_set_ls_return_value_for_tcs(struct si_shader_context *ctx)
{
   if (!ctx->shader->is_monolithic)
      ac_build_endif(&ctx->ac, ctx->merged_wrap_if_label);

   LLVMValueRef ret = ctx->return_value;

   ret = si_insert_input_ptr(ctx, ret, ctx->other_const_and_shader_buffers, 0);
   ret = si_insert_input_ptr(ctx, ret, ctx->other_samplers_and_images, 1);
   ret = si_insert_input_ret(ctx, ret, ctx->args.tess_offchip_offset, 2);
   ret = si_insert_input_ret(ctx, ret, ctx->args.merged_wave_info, 3);
   ret = si_insert_input_ret(ctx, ret, ctx->args.tcs_factor_offset, 4);
   ret = si_insert_input_ret(ctx, ret, ctx->args.scratch_offset, 5);

   ret = si_insert_input_ptr(ctx, ret, ctx->internal_bindings, 8 + SI_SGPR_INTERNAL_BINDINGS);
   ret = si_insert_input_ptr(ctx, ret, ctx->bindless_samplers_and_images,
                             8 + SI_SGPR_BINDLESS_SAMPLERS_AND_IMAGES);

   ret = si_insert_input_ret(ctx, ret, ctx->vs_state_bits, 8 + SI_SGPR_VS_STATE_BITS);

   ret = si_insert_input_ret(ctx, ret, ctx->tcs_offchip_layout, 8 + GFX9_SGPR_TCS_OFFCHIP_LAYOUT);
   ret = si_insert_input_ret(ctx, ret, ctx->tcs_out_lds_offsets, 8 + GFX9_SGPR_TCS_OUT_OFFSETS);
   ret = si_insert_input_ret(ctx, ret, ctx->tcs_out_lds_layout, 8 + GFX9_SGPR_TCS_OUT_LAYOUT);

   unsigned vgpr = 8 + GFX9_TCS_NUM_USER_SGPR;
   ret = LLVMBuildInsertValue(ctx->ac.builder, ret,
                              ac_to_float(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args.tcs_patch_id)),
                              vgpr++, "");
   ret = LLVMBuildInsertValue(ctx->ac.builder, ret,
                              ac_to_float(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args.tcs_rel_ids)),
                              vgpr++, "");
   ctx->return_value = ret;
}

void si_llvm_emit_ls_epilogue(struct ac_shader_abi *abi, unsigned max_outputs, LLVMValueRef *addrs)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   struct si_shader *shader = ctx->shader;
   struct si_shader_info *info = &shader->selector->info;
   unsigned i, chan;
   LLVMValueRef vertex_id = ac_get_arg(&ctx->ac, ctx->args.vs_rel_patch_id);
   LLVMValueRef vertex_dw_stride = get_tcs_in_vertex_dw_stride(ctx);
   LLVMValueRef base_dw_addr = LLVMBuildMul(ctx->ac.builder, vertex_id, vertex_dw_stride, "");
   unsigned ret_offset = 8 + GFX9_TCS_NUM_USER_SGPR + 2;

   /* Write outputs to LDS. The next shader (TCS aka HS) will read
    * its inputs from it. */
   for (i = 0; i < info->num_outputs; i++) {
      unsigned semantic = info->output_semantic[i];

      /* The ARB_shader_viewport_layer_array spec contains the
       * following issue:
       *
       *    2) What happens if gl_ViewportIndex or gl_Layer is
       *    written in the vertex shader and a geometry shader is
       *    present?
       *
       *    RESOLVED: The value written by the last vertex processing
       *    stage is used. If the last vertex processing stage
       *    (vertex, tessellation evaluation or geometry) does not
       *    statically assign to gl_ViewportIndex or gl_Layer, index
       *    or layer zero is assumed.
       *
       * So writes to those outputs in VS-as-LS are simply ignored.
       */
      if (semantic == VARYING_SLOT_LAYER || semantic == VARYING_SLOT_VIEWPORT)
         continue;

      int param = si_shader_io_get_unique_index(semantic, false);
      LLVMValueRef dw_addr =
         LLVMBuildAdd(ctx->ac.builder, base_dw_addr, LLVMConstInt(ctx->ac.i32, param * 4, 0), "");

      for (chan = 0; chan < 4; chan++) {
         if (!(info->output_usagemask[i] & (1 << chan)))
            continue;

         LLVMValueRef value = LLVMBuildLoad(ctx->ac.builder, addrs[4 * i + chan], "");

         if (!shader->key.opt.same_patch_vertices ||
             !(ctx->next_shader_sel->tcs_vgpr_only_inputs & (1ull << semantic)))
            lshs_lds_store(ctx, chan, dw_addr, value);

         if (shader->key.opt.same_patch_vertices) {
            ctx->return_value = LLVMBuildInsertValue(ctx->ac.builder, ctx->return_value,
                                                     value, ret_offset + param * 4 + chan, "");
         }
      }
   }

   if (ctx->screen->info.chip_class >= GFX9)
      si_set_ls_return_value_for_tcs(ctx);
}

/**
 * Compile the TCS epilog function. This writes tesselation factors to memory
 * based on the output primitive type of the tesselator (determined by TES).
 */
void si_llvm_build_tcs_epilog(struct si_shader_context *ctx, union si_shader_part_key *key)
{
   memset(&ctx->args, 0, sizeof(ctx->args));

   if (ctx->screen->info.chip_class >= GFX9) {
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.tess_offchip_offset);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); /* wave info */
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.tcs_factor_offset);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->tcs_offchip_layout);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->tcs_out_lds_layout);
   } else {
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->tcs_offchip_layout);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->tcs_out_lds_layout);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.tess_offchip_offset);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.tcs_factor_offset);
   }

   ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* VGPR gap */
   ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* VGPR gap */
   struct ac_arg rel_patch_id; /* patch index within the wave (REL_PATCH_ID) */
   ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &rel_patch_id);
   struct ac_arg invocation_id; /* invocation ID within the patch */
   ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &invocation_id);
   struct ac_arg
      tcs_out_current_patch_data_offset; /* LDS offset where tess factors should be loaded from */
   ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &tcs_out_current_patch_data_offset);

   struct ac_arg tess_factors[6];
   for (unsigned i = 0; i < 6; i++)
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &tess_factors[i]);

   /* Create the function. */
   si_llvm_create_func(ctx, "tcs_epilog", NULL, 0, ctx->screen->info.chip_class >= GFX7 ? 128 : 0);
   ac_declare_lds_as_pointer(&ctx->ac);

   LLVMValueRef invoc0_tess_factors[6];
   for (unsigned i = 0; i < 6; i++)
      invoc0_tess_factors[i] = ac_get_arg(&ctx->ac, tess_factors[i]);

   si_write_tess_factors(ctx, ac_get_arg(&ctx->ac, rel_patch_id),
                         ac_get_arg(&ctx->ac, invocation_id),
                         ac_get_arg(&ctx->ac, tcs_out_current_patch_data_offset),
                         invoc0_tess_factors, invoc0_tess_factors + 4);

   LLVMBuildRetVoid(ctx->ac.builder);
}

void si_llvm_init_tcs_callbacks(struct si_shader_context *ctx)
{
   ctx->abi.load_tess_varyings = si_nir_load_tcs_varyings;
   ctx->abi.load_tess_level = si_load_tess_level;
   ctx->abi.store_tcs_outputs = si_nir_store_output_tcs;
   ctx->abi.emit_outputs = si_llvm_emit_tcs_epilogue;
   ctx->abi.load_patch_vertices_in = si_load_patch_vertices_in;
}

void si_llvm_init_tes_callbacks(struct si_shader_context *ctx, bool ngg_cull_shader)
{
   ctx->abi.load_tess_varyings = si_nir_load_input_tes;
   ctx->abi.load_tess_coord = si_load_tess_coord;
   ctx->abi.load_tess_level = si_load_tess_level;
   ctx->abi.load_patch_vertices_in = si_load_patch_vertices_in;

   if (ctx->shader->key.as_es)
      ctx->abi.emit_outputs = si_llvm_emit_es_epilogue;
   else if (ngg_cull_shader)
      ctx->abi.emit_outputs = gfx10_emit_ngg_culling_epilogue;
   else if (ctx->shader->key.as_ngg)
      ctx->abi.emit_outputs = gfx10_emit_ngg_epilogue;
   else
      ctx->abi.emit_outputs = si_llvm_emit_vs_epilogue;
}
