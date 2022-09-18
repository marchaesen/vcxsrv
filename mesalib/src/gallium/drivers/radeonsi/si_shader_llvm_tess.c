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

LLVMValueRef si_get_rel_patch_id(struct si_shader_context *ctx)
{
   switch (ctx->stage) {
   case MESA_SHADER_TESS_CTRL:
      return si_unpack_param(ctx, ctx->args.tcs_rel_ids, 0, 8);

   case MESA_SHADER_TESS_EVAL:
      return ctx->abi.tes_rel_patch_id_replaced ?
         ctx->abi.tes_rel_patch_id_replaced :
         ac_get_arg(&ctx->ac, ctx->args.tes_rel_patch_id);

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
 * - TCS inputs for patch 2             = get_tcs_in_current_patch_offset (if RelPatchID==2)
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

static unsigned get_tcs_out_vertex_dw_stride_constant(struct si_shader_context *ctx)
{
   assert(ctx->stage == MESA_SHADER_TESS_CTRL);

   return util_last_bit64(ctx->shader->selector->info.outputs_written) * 4;
}

static LLVMValueRef get_tcs_out_patch_stride(struct si_shader_context *ctx)
{
   const struct si_shader_info *info = &ctx->shader->selector->info;
   unsigned tcs_out_vertices = info->base.tess.tcs_vertices_out;
   unsigned vertex_dw_stride = get_tcs_out_vertex_dw_stride_constant(ctx);
   unsigned num_patch_outputs = util_last_bit64(ctx->shader->selector->info.patch_outputs_written);
   unsigned patch_dw_stride = tcs_out_vertices * vertex_dw_stride + num_patch_outputs * 4;
   return LLVMConstInt(ctx->ac.i32, patch_dw_stride, 0);
}

static LLVMValueRef get_tcs_out_patch0_patch_data_offset(struct si_shader_context *ctx)
{
   return si_unpack_param(ctx, ctx->tcs_out_lds_offsets, 16, 16);
}

static LLVMValueRef get_tcs_out_current_patch_data_offset(struct si_shader_context *ctx)
{
   LLVMValueRef patch0_patch_data_offset = get_tcs_out_patch0_patch_data_offset(ctx);
   LLVMValueRef patch_stride = get_tcs_out_patch_stride(ctx);
   LLVMValueRef rel_patch_id = si_get_rel_patch_id(ctx);

   return ac_build_imad(&ctx->ac, patch_stride, rel_patch_id, patch0_patch_data_offset);
}

LLVMValueRef si_get_num_tcs_out_vertices(struct si_shader_context *ctx)
{
   unsigned tcs_out_vertices =
      ctx->shader->selector ? ctx->shader->selector->info.base.tess.tcs_vertices_out
                            : 0;

   /* If !tcs_out_vertices, it's the TCS epilog. */
   if (ctx->stage == MESA_SHADER_TESS_CTRL && tcs_out_vertices)
      return LLVMConstInt(ctx->ac.i32, tcs_out_vertices, 0);

   return LLVMBuildAdd(ctx->ac.builder,
                       si_unpack_param(ctx, ctx->tcs_offchip_layout, 6, 5), ctx->ac.i32_1, "");
}

LLVMValueRef si_get_tcs_in_vertex_dw_stride(struct si_shader_context *ctx)
{
   unsigned stride;

   switch (ctx->stage) {
   case MESA_SHADER_VERTEX:
      stride = ctx->shader->selector->info.lshs_vertex_stride / 4;
      return LLVMConstInt(ctx->ac.i32, stride, 0);

   case MESA_SHADER_TESS_CTRL:
      if (ctx->screen->info.gfx_level >= GFX9 && ctx->shader->is_monolithic) {
         stride = ctx->shader->key.ge.part.tcs.ls->info.lshs_vertex_stride / 4;
         return LLVMConstInt(ctx->ac.i32, stride, 0);
      }
      return GET_FIELD(ctx, VS_STATE_LS_OUT_VERTEX_SIZE);

   default:
      assert(0);
      return NULL;
   }
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

   vertices_per_patch = si_get_num_tcs_out_vertices(ctx);
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

/**
 * Load from LSHS LDS storage.
 *
 * \param type     output value type
 * \param swizzle  offset (typically 0..3); it can be ~0, which loads a vec4
 * \param dw_addr  address in dwords
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
      unsigned tf_offset = ctx->screen->hs.tess_offchip_ring_size;
      addr = LLVMBuildAdd(builder, addr, LLVMConstInt(ctx->ac.i32, tf_offset, 0), "");
   }

   uint32_t rsrc3 = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                    S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

   if (ctx->screen->info.gfx_level >= GFX11)
      rsrc3 |= S_008F0C_FORMAT(V_008F0C_GFX11_FORMAT_32_FLOAT) |
               S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW);
   else if (ctx->screen->info.gfx_level >= GFX10)
      rsrc3 |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
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

void si_llvm_preload_tess_rings(struct si_shader_context *ctx)
{
   ctx->tess_offchip_ring = get_tess_ring_descriptor(
      ctx, ctx->stage == MESA_SHADER_TESS_CTRL ? TESS_OFFCHIP_RING_TCS : TESS_OFFCHIP_RING_TES);
}

static LLVMValueRef si_nir_load_tcs_varyings(struct ac_shader_abi *abi, LLVMTypeRef type,
                                             LLVMValueRef vertex_index, LLVMValueRef param_index,
                                             unsigned driver_location, unsigned component,
                                             unsigned num_components, bool load_input)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   struct si_shader_info *info = &ctx->shader->selector->info;

   assert(ctx->shader->key.ge.opt.same_patch_vertices && !param_index);

   ubyte semantic = info->input[driver_location].semantic;
   /* Load the TCS input from a VGPR. */
   unsigned func_param = ctx->args.tcs_rel_ids.arg_index + 1 +
      si_shader_io_get_unique_index(semantic, false) * 4;

   LLVMValueRef value[4];
   for (unsigned i = component; i < component + num_components; i++) {
      value[i] = LLVMGetParam(ctx->main_fn, func_param + i);
      value[i] = LLVMBuildBitCast(ctx->ac.builder, value[i], type, "");
   }

   return ac_build_varying_gather_values(&ctx->ac, value, num_components, component);
}

static void si_write_tess_factors(struct si_shader_context *ctx, union si_shader_part_key *key,
                                  LLVMValueRef rel_patch_id, LLVMValueRef invocation_id,
                                  LLVMValueRef tcs_out_current_patch_data_offset,
                                  LLVMValueRef invoc0_tf_outer[4], LLVMValueRef invoc0_tf_inner[2])
{
   struct si_shader *shader = ctx->shader;
   unsigned tess_inner_index, tess_outer_index;
   LLVMValueRef lds_base, lds_inner, lds_outer, byteoffset, buffer;
   LLVMValueRef out[6], vec0, vec1, tf_base, inner[4], outer[4];
   unsigned stride, outer_comps, inner_comps, i, offset;

   /* Add a barrier before loading tess factors from LDS. */
   if (!shader->key.ge.part.tcs.epilog.invoc0_tess_factors_are_def) {
      ac_build_waitcnt(&ctx->ac, AC_WAIT_LGKM);

      if (!key->tcs_epilog.noop_s_barrier)
         ac_build_s_barrier(&ctx->ac, ctx->stage);
   }

   /* Do this only for invocation 0, because the tess levels are per-patch,
    * not per-vertex.
    *
    * This can't jump, because invocation 0 executes this. It should
    * at least mask out the loads and stores for other invocations.
    */
   ac_build_ifcc(&ctx->ac,
                 LLVMBuildICmp(ctx->ac.builder, LLVMIntEQ, invocation_id, ctx->ac.i32_0, ""), 6503);

   /* Determine the layout of one tess factor element in the buffer. */
   switch (shader->key.ge.part.tcs.epilog.prim_mode) {
   case TESS_PRIMITIVE_ISOLINES:
      stride = 2; /* 2 dwords, 1 vec2 store */
      outer_comps = 2;
      inner_comps = 0;
      break;
   case TESS_PRIMITIVE_TRIANGLES:
      stride = 4; /* 4 dwords, 1 vec4 store */
      outer_comps = 3;
      inner_comps = 1;
      break;
   case TESS_PRIMITIVE_QUADS:
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

   if (shader->key.ge.part.tcs.epilog.invoc0_tess_factors_are_def) {
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

   if (shader->key.ge.part.tcs.epilog.prim_mode == TESS_PRIMITIVE_ISOLINES) {
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
   if (ctx->screen->info.gfx_level <= GFX8) {
      ac_build_ifcc(&ctx->ac,
                    LLVMBuildICmp(ctx->ac.builder, LLVMIntEQ, rel_patch_id, ctx->ac.i32_0, ""), 6504);
      ac_build_buffer_store_dword(&ctx->ac, buffer, LLVMConstInt(ctx->ac.i32, 0x80000000, 0),
                                  NULL, LLVMConstInt(ctx->ac.i32, offset, 0), tf_base, ac_glc);
      ac_build_endif(&ctx->ac, 6504);
      offset += 4;
   }

   /* Store the tessellation factors. */
   ac_build_buffer_store_dword(&ctx->ac, buffer, vec0, NULL,
                               LLVMBuildAdd(ctx->ac.builder, byteoffset,
                                            LLVMConstInt(ctx->ac.i32, offset, 0), ""),
                               tf_base, ac_glc);
   offset += 16;
   if (vec1)
      ac_build_buffer_store_dword(&ctx->ac, buffer, vec1, NULL,
                                  LLVMBuildAdd(ctx->ac.builder, byteoffset,
                                               LLVMConstInt(ctx->ac.i32, offset, 0), ""),
                                  tf_base, ac_glc);

   /* Store the tess factors into the offchip buffer if TES reads them. */
   if (shader->key.ge.part.tcs.epilog.tes_reads_tess_factors) {
      LLVMValueRef buf, base, inner_vec, outer_vec, tf_outer_offset;
      LLVMValueRef tf_inner_offset;
      unsigned param_outer, param_inner;

      buf = get_tess_ring_descriptor(ctx, TESS_OFFCHIP_RING_TCS);
      base = ac_get_arg(&ctx->ac, ctx->args.tess_offchip_offset);

      param_outer = si_shader_io_get_unique_index_patch(VARYING_SLOT_TESS_LEVEL_OUTER);
      tf_outer_offset = get_tcs_tes_buffer_address(ctx, rel_patch_id, NULL,
                                                   LLVMConstInt(ctx->ac.i32, param_outer, 0));

      outer_vec = ac_build_gather_values(&ctx->ac, outer, outer_comps);

      ac_build_buffer_store_dword(&ctx->ac, buf, outer_vec, NULL, tf_outer_offset,
                                  base, ac_glc);
      if (inner_comps) {
         param_inner = si_shader_io_get_unique_index_patch(VARYING_SLOT_TESS_LEVEL_INNER);
         tf_inner_offset = get_tcs_tes_buffer_address(ctx, rel_patch_id, NULL,
                                                      LLVMConstInt(ctx->ac.i32, param_inner, 0));

         inner_vec = ac_build_gather_values(&ctx->ac, inner, inner_comps);
         ac_build_buffer_store_dword(&ctx->ac, buf, inner_vec, NULL,
                                     tf_inner_offset, base, ac_glc);
      }
   }

   ac_build_endif(&ctx->ac, 6503);
}

/* This only writes the tessellation factor levels. */
void si_llvm_tcs_build_end(struct si_shader_context *ctx)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef rel_patch_id, invocation_id, tf_lds_offset;

   rel_patch_id = si_get_rel_patch_id(ctx);
   invocation_id = si_unpack_param(ctx, ctx->args.tcs_rel_ids, 8, 5);
   tf_lds_offset = get_tcs_out_current_patch_data_offset(ctx);

   if (ctx->screen->info.gfx_level >= GFX9 && !ctx->shader->is_monolithic) {
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

   if (ctx->screen->info.gfx_level >= GFX9) {
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

   struct si_shader_info *info = &ctx->shader->selector->info;
   if (info->tessfactors_are_def_in_all_invocs) {
      vgpr++; /* skip the tess factor LDS offset */

      /* get tess factor driver location */
      int outer_loc = -1;
      int inner_loc = -1;
      for (int i = 0; i < info->num_outputs; i++) {
         unsigned semantic = info->output_semantic[i];
         if (semantic == VARYING_SLOT_TESS_LEVEL_OUTER)
            outer_loc = i;
         else if (semantic == VARYING_SLOT_TESS_LEVEL_INNER)
            inner_loc = i;
      }

      for (unsigned i = 0; i < 6; i++) {
         int loc = i < 4 ? outer_loc : inner_loc;
         LLVMValueRef value = loc < 0 ? LLVMGetUndef(ctx->ac.f32) :
            LLVMBuildLoad2(builder, ctx->ac.f32, ctx->abi.outputs[loc * 4 + i % 4], "");
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
   if (ctx->screen->info.gfx_level <= GFX10_3)
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

void si_llvm_ls_build_end(struct si_shader_context *ctx)
{
   struct si_shader *shader = ctx->shader;
   struct si_shader_info *info = &shader->selector->info;
   LLVMValueRef *addrs = ctx->abi.outputs;
   unsigned ret_offset = 8 + GFX9_TCS_NUM_USER_SGPR + 2;

   if (shader->key.ge.opt.same_patch_vertices) {
      for (unsigned i = 0; i < info->num_outputs; i++) {
         unsigned semantic = info->output_semantic[i];
         int param = si_shader_io_get_unique_index(semantic, false);

         for (unsigned chan = 0; chan < 4; chan++) {
            if (!(info->output_usagemask[i] & (1 << chan)))
               continue;

            LLVMValueRef value = LLVMBuildLoad2(ctx->ac.builder, ctx->ac.f32, addrs[4 * i + chan], "");

            ctx->return_value = LLVMBuildInsertValue(ctx->ac.builder, ctx->return_value,
                                                     value, ret_offset + param * 4 + chan, "");
         }
      }
   }

   if (ctx->screen->info.gfx_level >= GFX9)
      si_set_ls_return_value_for_tcs(ctx);
}

/**
 * Compile the TCS epilog function. This writes tesselation factors to memory
 * based on the output primitive type of the tesselator (determined by TES).
 */
void si_llvm_build_tcs_epilog(struct si_shader_context *ctx, union si_shader_part_key *key)
{
   memset(&ctx->args, 0, sizeof(ctx->args));

   if (ctx->screen->info.gfx_level >= GFX9) {
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
   si_llvm_create_func(ctx, "tcs_epilog", NULL, 0, ctx->screen->info.gfx_level >= GFX7 ? 128 : 0);
   ac_declare_lds_as_pointer(&ctx->ac);

   LLVMValueRef invoc0_tess_factors[6];
   for (unsigned i = 0; i < 6; i++)
      invoc0_tess_factors[i] = ac_get_arg(&ctx->ac, tess_factors[i]);

   si_write_tess_factors(ctx, key, ac_get_arg(&ctx->ac, rel_patch_id),
                         ac_get_arg(&ctx->ac, invocation_id),
                         ac_get_arg(&ctx->ac, tcs_out_current_patch_data_offset),
                         invoc0_tess_factors, invoc0_tess_factors + 4);

   LLVMBuildRetVoid(ctx->ac.builder);
}

void si_llvm_init_tcs_callbacks(struct si_shader_context *ctx)
{
   ctx->abi.load_tess_varyings = si_nir_load_tcs_varyings;
}
