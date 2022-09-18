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

#include "ac_nir.h"
#include "si_pipe.h"
#include "si_shader_internal.h"
#include "si_query.h"
#include "sid.h"
#include "util/u_memory.h"

LLVMValueRef si_is_es_thread(struct si_shader_context *ctx)
{
   /* Return true if the current thread should execute an ES thread. */
   return LLVMBuildICmp(ctx->ac.builder, LLVMIntULT, ac_get_thread_id(&ctx->ac),
                        si_unpack_param(ctx, ctx->args.merged_wave_info, 0, 8), "");
}

LLVMValueRef si_is_gs_thread(struct si_shader_context *ctx)
{
   /* Return true if the current thread should execute a GS thread. */
   return LLVMBuildICmp(ctx->ac.builder, LLVMIntULT, ac_get_thread_id(&ctx->ac),
                        si_unpack_param(ctx, ctx->args.merged_wave_info, 8, 8), "");
}

/* Pass GS inputs from ES to GS on GFX9. */
static void si_set_es_return_value_for_gs(struct si_shader_context *ctx)
{
   if (!ctx->shader->is_monolithic)
      ac_build_endif(&ctx->ac, ctx->merged_wrap_if_label);

   LLVMValueRef ret = ctx->return_value;

   ret = si_insert_input_ptr(ctx, ret, ctx->other_const_and_shader_buffers, 0);
   ret = si_insert_input_ptr(ctx, ret, ctx->other_samplers_and_images, 1);
   if (ctx->shader->key.ge.as_ngg)
      ret = si_insert_input_ptr(ctx, ret, ctx->args.gs_tg_info, 2);
   else
      ret = si_insert_input_ret(ctx, ret, ctx->args.gs2vs_offset, 2);
   ret = si_insert_input_ret(ctx, ret, ctx->args.merged_wave_info, 3);
   if (ctx->screen->info.gfx_level >= GFX11)
      ret = si_insert_input_ret(ctx, ret, ctx->args.gs_attr_offset, 5);
   else
      ret = si_insert_input_ret(ctx, ret, ctx->args.scratch_offset, 5);
   ret = si_insert_input_ptr(ctx, ret, ctx->internal_bindings, 8 + SI_SGPR_INTERNAL_BINDINGS);
   ret = si_insert_input_ptr(ctx, ret, ctx->bindless_samplers_and_images,
                             8 + SI_SGPR_BINDLESS_SAMPLERS_AND_IMAGES);
   if (ctx->screen->use_ngg) {
      ret = si_insert_input_ptr(ctx, ret, ctx->vs_state_bits, 8 + SI_SGPR_VS_STATE_BITS);
      ret = si_insert_input_ptr(ctx, ret, ctx->small_prim_cull_info, 8 + GFX9_SGPR_SMALL_PRIM_CULL_INFO);
      if (ctx->screen->info.gfx_level >= GFX11)
         ret = si_insert_input_ptr(ctx, ret, ctx->gs_attr_address, 8 + GFX9_SGPR_ATTRIBUTE_RING_ADDR);
   }

   unsigned vgpr = 8 + GFX9_GS_NUM_USER_SGPR;

   ret = si_insert_input_ret_float(ctx, ret, ctx->args.gs_vtx_offset[0], vgpr++);
   ret = si_insert_input_ret_float(ctx, ret, ctx->args.gs_vtx_offset[1], vgpr++);
   ret = si_insert_input_ret_float(ctx, ret, ctx->args.gs_prim_id, vgpr++);
   ret = si_insert_input_ret_float(ctx, ret, ctx->args.gs_invocation_id, vgpr++);
   ret = si_insert_input_ret_float(ctx, ret, ctx->args.gs_vtx_offset[2], vgpr++);
   ctx->return_value = ret;
}

void si_llvm_es_build_end(struct si_shader_context *ctx)
{
   if (ctx->screen->info.gfx_level >= GFX9)
      si_set_es_return_value_for_gs(ctx);
}

static LLVMValueRef si_get_gs_wave_id(struct si_shader_context *ctx)
{
   if (ctx->screen->info.gfx_level >= GFX9)
      return si_unpack_param(ctx, ctx->args.merged_wave_info, 16, 8);
   else
      return ac_get_arg(&ctx->ac, ctx->args.gs_wave_id);
}

static LLVMValueRef ngg_get_emulated_counters_buf(struct si_shader_context *ctx)
{
   LLVMValueRef buf_ptr = ac_get_arg(&ctx->ac, ctx->internal_bindings);

   return ac_build_load_to_sgpr(&ctx->ac, buf_ptr,
                                LLVMConstInt(ctx->ac.i32, SI_GS_QUERY_EMULATED_COUNTERS_BUF, false));
}

void si_llvm_gs_build_end(struct si_shader_context *ctx)
{
   struct si_shader_info UNUSED *info = &ctx->shader->selector->info;

   assert(info->num_outputs <= AC_LLVM_MAX_OUTPUTS);

   if (ctx->screen->info.gfx_level >= GFX10)
      ac_build_waitcnt(&ctx->ac, AC_WAIT_VSTORE);

   if (ctx->screen->use_ngg) {
      /* Implement PIPE_STAT_QUERY_GS_PRIMITIVES for non-ngg draws because we can't
       * use pipeline statistics (they would be correct but when screen->use_ngg, we
       * can't know when the query is started if the next draw(s) will use ngg or not).
       */
      LLVMValueRef tmp = GET_FIELD(ctx, GS_STATE_PIPELINE_STATS_EMU);
      tmp = LLVMBuildTrunc(ctx->ac.builder, tmp, ctx->ac.i1, "");
      ac_build_ifcc(&ctx->ac, tmp, 5229); /* if (GS_PIPELINE_STATS_EMU) */
      {
         LLVMValueRef prim = ctx->ac.i32_0;
         switch (ctx->shader->selector->info.base.gs.output_primitive) {
         case SHADER_PRIM_POINTS:
            prim = ctx->gs_emitted_vertices;
            break;
         case SHADER_PRIM_LINE_STRIP:
            prim = LLVMBuildSub(ctx->ac.builder, ctx->gs_emitted_vertices, ctx->ac.i32_1, "");
            prim = ac_build_imax(&ctx->ac, prim, ctx->ac.i32_0);
            break;
         case SHADER_PRIM_TRIANGLE_STRIP:
            prim = LLVMBuildSub(ctx->ac.builder, ctx->gs_emitted_vertices, LLVMConstInt(ctx->ac.i32, 2, 0), "");
            prim = ac_build_imax(&ctx->ac, prim, ctx->ac.i32_0);
            break;
         }

         LLVMValueRef args[] = {
            prim,
            ngg_get_emulated_counters_buf(ctx),
            LLVMConstInt(ctx->ac.i32,
                         si_query_pipestat_end_dw_offset(ctx->screen, PIPE_STAT_QUERY_GS_PRIMITIVES) * 4,
                         false),
            ctx->ac.i32_0,                            /* soffset */
            ctx->ac.i32_0,                            /* cachepolicy */
         };
         ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.raw.buffer.atomic.add.i32", ctx->ac.i32, args, 5, 0);

         args[0] = ctx->ac.i32_1;
         args[2] = LLVMConstInt(ctx->ac.i32,
                                si_query_pipestat_end_dw_offset(ctx->screen, PIPE_STAT_QUERY_GS_INVOCATIONS) * 4,
                                false);
         ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.raw.buffer.atomic.add.i32", ctx->ac.i32, args, 5, 0);
      }
      ac_build_endif(&ctx->ac, 5229);
   }

   ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_NOP | AC_SENDMSG_GS_DONE, si_get_gs_wave_id(ctx));

   if (ctx->screen->info.gfx_level >= GFX9)
      ac_build_endif(&ctx->ac, ctx->merged_wrap_if_label);
}

/* Emit one vertex from the geometry shader */
static void si_llvm_emit_vertex(struct ac_shader_abi *abi, unsigned stream, LLVMValueRef *addrs)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);

   if (ctx->shader->key.ge.as_ngg) {
      gfx10_ngg_gs_emit_vertex(ctx, stream, addrs);
      return;
   }

   struct si_shader_info *info = &ctx->shader->selector->info;
   struct si_shader *shader = ctx->shader;
   LLVMValueRef soffset = ac_get_arg(&ctx->ac, ctx->args.gs2vs_offset);
   LLVMValueRef gs_next_vertex;
   LLVMValueRef can_emit;
   unsigned chan, offset;
   int i;

   /* Write vertex attribute values to GSVS ring */
   gs_next_vertex = LLVMBuildLoad2(ctx->ac.builder, ctx->ac.i32, ctx->gs_next_vertex[stream], "");

   /* If this thread has already emitted the declared maximum number of
    * vertices, skip the write: excessive vertex emissions are not
    * supposed to have any effect.
    *
    * If the shader has no writes to memory, kill it instead. This skips
    * further memory loads and may allow LLVM to skip to the end
    * altogether.
    */
   can_emit =
      LLVMBuildICmp(ctx->ac.builder, LLVMIntULT, gs_next_vertex,
                    LLVMConstInt(ctx->ac.i32, shader->selector->info.base.gs.vertices_out, 0), "");

   bool use_kill = !info->base.writes_memory;
   if (use_kill) {
      ac_build_kill_if_false(&ctx->ac, can_emit);
   } else {
      ac_build_ifcc(&ctx->ac, can_emit, 6505);
   }

   offset = 0;
   for (i = 0; i < info->num_outputs; i++) {
      for (chan = 0; chan < 4; chan++) {
         if (!(info->output_usagemask[i] & (1 << chan)) ||
             ((info->output_streams[i] >> (2 * chan)) & 3) != stream)
            continue;

         LLVMValueRef out_val = LLVMBuildLoad2(ctx->ac.builder, ctx->ac.f32, addrs[4 * i + chan], "");
         LLVMValueRef voffset =
            LLVMConstInt(ctx->ac.i32, offset * shader->selector->info.base.gs.vertices_out, 0);
         offset++;

         voffset = LLVMBuildAdd(ctx->ac.builder, voffset, gs_next_vertex, "");
         voffset = LLVMBuildMul(ctx->ac.builder, voffset, LLVMConstInt(ctx->ac.i32, 4, 0), "");

         out_val = ac_to_integer(&ctx->ac, out_val);

         ac_build_buffer_store_dword(&ctx->ac, ctx->gsvs_ring[stream], out_val, NULL,
                                     voffset, soffset, ac_glc | ac_slc | ac_swizzled);
      }
   }

   gs_next_vertex = LLVMBuildAdd(ctx->ac.builder, gs_next_vertex, ctx->ac.i32_1, "");
   LLVMBuildStore(ctx->ac.builder, gs_next_vertex, ctx->gs_next_vertex[stream]);

   /* Signal vertex emission if vertex data was written. */
   if (offset) {
      ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_EMIT | AC_SENDMSG_GS | (stream << 8),
                       si_get_gs_wave_id(ctx));

      ctx->gs_emitted_vertices = LLVMBuildAdd(ctx->ac.builder, ctx->gs_emitted_vertices,
                                              ctx->ac.i32_1, "vert");
   }

   if (!use_kill)
      ac_build_endif(&ctx->ac, 6505);
}

/* Cut one primitive from the geometry shader */
static void si_llvm_emit_primitive(struct ac_shader_abi *abi, unsigned stream)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);

   if (ctx->shader->key.ge.as_ngg) {
      LLVMBuildStore(ctx->ac.builder, ctx->ac.i32_0, ctx->gs_curprim_verts[stream]);
      return;
   }

   /* Signal primitive cut */
   ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_CUT | AC_SENDMSG_GS | (stream << 8),
                    si_get_gs_wave_id(ctx));
}

void si_preload_esgs_ring(struct si_shader_context *ctx)
{
   LLVMBuilderRef builder = ctx->ac.builder;

   if (ctx->screen->info.gfx_level <= GFX8) {
      LLVMValueRef offset = LLVMConstInt(ctx->ac.i32, SI_RING_ESGS, 0);
      LLVMValueRef buf_ptr = ac_get_arg(&ctx->ac, ctx->internal_bindings);

      ctx->esgs_ring = ac_build_load_to_sgpr(&ctx->ac, buf_ptr, offset);

      if (ctx->stage != MESA_SHADER_GEOMETRY) {
         LLVMValueRef desc1 = LLVMBuildExtractElement(builder, ctx->esgs_ring, ctx->ac.i32_1, "");
         LLVMValueRef desc3 = LLVMBuildExtractElement(builder, ctx->esgs_ring,
                                                      LLVMConstInt(ctx->ac.i32, 3, 0), "");
         desc1 = LLVMBuildOr(builder, desc1, LLVMConstInt(ctx->ac.i32,
                                                          S_008F04_SWIZZLE_ENABLE_GFX6(1), 0), "");
         desc3 = LLVMBuildOr(builder, desc3, LLVMConstInt(ctx->ac.i32,
                                                          S_008F0C_ELEMENT_SIZE(1) |
                                                          S_008F0C_INDEX_STRIDE(3) |
                                                          S_008F0C_ADD_TID_ENABLE(1), 0), "");

         /* If MUBUF && ADD_TID_ENABLE, DATA_FORMAT means STRIDE[14:17] on gfx8-9, so set 0. */
         if (ctx->screen->info.gfx_level == GFX8) {
            desc3 = LLVMBuildAnd(builder, desc3,
                                 LLVMConstInt(ctx->ac.i32, C_008F0C_DATA_FORMAT, 0), "");
         }

         ctx->esgs_ring = LLVMBuildInsertElement(builder, ctx->esgs_ring, desc1, ctx->ac.i32_1, "");
         ctx->esgs_ring = LLVMBuildInsertElement(builder, ctx->esgs_ring, desc3,
                                                 LLVMConstInt(ctx->ac.i32, 3, 0), "");
      }
   } else {
      if (USE_LDS_SYMBOLS) {
         /* Declare the ESGS ring as an explicit LDS symbol. */
         si_llvm_declare_esgs_ring(ctx);
         ctx->ac.lds = ctx->esgs_ring;
      } else {
         ac_declare_lds_as_pointer(&ctx->ac);
         ctx->esgs_ring = ctx->ac.lds;
      }
   }
}

void si_preload_gs_rings(struct si_shader_context *ctx)
{
   if (ctx->ac.gfx_level >= GFX11)
      return;

   const struct si_shader_selector *sel = ctx->shader->selector;
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef offset = LLVMConstInt(ctx->ac.i32, SI_RING_GSVS, 0);
   LLVMValueRef buf_ptr = ac_get_arg(&ctx->ac, ctx->internal_bindings);
   LLVMValueRef base_ring = ac_build_load_to_sgpr(&ctx->ac, buf_ptr, offset);

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

   for (unsigned stream = 0; stream < 4; ++stream) {
      unsigned num_components;
      unsigned stride;
      unsigned num_records;
      LLVMValueRef ring, tmp;

      num_components = sel->info.num_stream_output_components[stream];
      if (!num_components)
         continue;

      stride = 4 * num_components * sel->info.base.gs.vertices_out;

      /* Limit on the stride field for <= GFX7. */
      assert(stride < (1 << 14));

      num_records = ctx->ac.wave_size;

      ring = LLVMBuildBitCast(builder, base_ring, v2i64, "");
      tmp = LLVMBuildExtractElement(builder, ring, ctx->ac.i32_0, "");
      tmp = LLVMBuildAdd(builder, tmp, LLVMConstInt(ctx->ac.i64, stream_offset, 0), "");
      stream_offset += stride * ctx->ac.wave_size;

      ring = LLVMBuildInsertElement(builder, ring, tmp, ctx->ac.i32_0, "");
      ring = LLVMBuildBitCast(builder, ring, ctx->ac.v4i32, "");
      tmp = LLVMBuildExtractElement(builder, ring, ctx->ac.i32_1, "");
      tmp = LLVMBuildOr(
         builder, tmp,
         LLVMConstInt(ctx->ac.i32, S_008F04_STRIDE(stride) | S_008F04_SWIZZLE_ENABLE_GFX6(1), 0), "");
      ring = LLVMBuildInsertElement(builder, ring, tmp, ctx->ac.i32_1, "");
      ring = LLVMBuildInsertElement(builder, ring, LLVMConstInt(ctx->ac.i32, num_records, 0),
                                    LLVMConstInt(ctx->ac.i32, 2, 0), "");

      uint32_t rsrc3 =
         S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
         S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
         S_008F0C_INDEX_STRIDE(1) | /* index_stride = 16 (elements) */
         S_008F0C_ADD_TID_ENABLE(1);

      if (ctx->ac.gfx_level >= GFX10) {
         rsrc3 |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                  S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED) | S_008F0C_RESOURCE_LEVEL(1);
      } else {
         /* If MUBUF && ADD_TID_ENABLE, DATA_FORMAT means STRIDE[14:17] on gfx8-9, so set 0. */
         unsigned data_format = ctx->ac.gfx_level == GFX8 || ctx->ac.gfx_level == GFX9 ?
                                   0 : V_008F0C_BUF_DATA_FORMAT_32;

         rsrc3 |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                  S_008F0C_DATA_FORMAT(data_format) |
                  S_008F0C_ELEMENT_SIZE(1); /* element_size = 4 (bytes) */
      }

      ring = LLVMBuildInsertElement(builder, ring, LLVMConstInt(ctx->ac.i32, rsrc3, false),
                                    LLVMConstInt(ctx->ac.i32, 3, 0), "");

      ctx->gsvs_ring[stream] = ring;
   }
}

/* Generate code for the hardware VS shader stage to go with a geometry shader */
struct si_shader *si_generate_gs_copy_shader(struct si_screen *sscreen,
                                             struct ac_llvm_compiler *compiler,
                                             struct si_shader_selector *gs_selector,
                                             const struct pipe_stream_output_info *so,
                                             struct util_debug_callback *debug)
{
   struct si_shader_context ctx;
   struct si_shader *shader;
   LLVMBuilderRef builder;
   struct si_shader_output_values outputs[SI_MAX_VS_OUTPUTS];
   struct si_shader_info *gsinfo = &gs_selector->info;
   int i;

   shader = CALLOC_STRUCT(si_shader);
   if (!shader)
      return NULL;

   /* We can leave the fence as permanently signaled because the GS copy
    * shader only becomes visible globally after it has been compiled. */
   util_queue_fence_init(&shader->ready);

   shader->selector = gs_selector;
   shader->is_gs_copy_shader = true;
   shader->wave_size = si_determine_wave_size(sscreen, shader);

   STATIC_ASSERT(sizeof(shader->info.vs_output_param_offset[0]) == 1);
   memset(shader->info.vs_output_param_offset, AC_EXP_PARAM_DEFAULT_VAL_0000,
          sizeof(shader->info.vs_output_param_offset));

   for (unsigned i = 0; i < gsinfo->num_outputs; i++) {
      unsigned semantic = gsinfo->output_semantic[i];

      /* Skip if no channel writes to stream 0. */
      if (!nir_slot_is_varying(semantic) ||
          (gsinfo->output_streams[i] & 0x03 &&
           gsinfo->output_streams[i] & 0x0c &&
           gsinfo->output_streams[i] & 0x30 &&
           gsinfo->output_streams[i] & 0xc0))
         continue;

      shader->info.vs_output_param_offset[semantic] = shader->info.nr_param_exports++;
      shader->info.vs_output_param_mask |= BITFIELD64_BIT(i);
   }

   si_llvm_context_init(&ctx, sscreen, compiler, shader->wave_size);
   ctx.shader = shader;
   ctx.stage = MESA_SHADER_VERTEX;
   ctx.so = *so;

   builder = ctx.ac.builder;

   /* Build the main function. */
   si_llvm_create_main_func(&ctx, false);

   LLVMValueRef buf_ptr = ac_get_arg(&ctx.ac, ctx.internal_bindings);
   ctx.gsvs_ring[0] =
      ac_build_load_to_sgpr(&ctx.ac, buf_ptr, LLVMConstInt(ctx.ac.i32, SI_RING_GSVS, 0));

   LLVMValueRef voffset =
      LLVMBuildMul(ctx.ac.builder, ctx.abi.vertex_id, LLVMConstInt(ctx.ac.i32, 4, 0), "");

   /* Fetch the vertex stream ID.*/
   LLVMValueRef stream_id;

   if (!sscreen->use_ngg_streamout && ctx.so.num_outputs)
      stream_id = si_unpack_param(&ctx, ctx.args.streamout_config, 24, 2);
   else
      stream_id = ctx.ac.i32_0;

   /* Fill in output information. */
   for (i = 0; i < gsinfo->num_outputs; ++i) {
      outputs[i].semantic = gsinfo->output_semantic[i];
      outputs[i].vertex_streams = gsinfo->output_streams[i];
   }

   LLVMBasicBlockRef end_bb;
   LLVMValueRef switch_inst;

   end_bb = LLVMAppendBasicBlockInContext(ctx.ac.context, ctx.main_fn, "end");
   switch_inst = LLVMBuildSwitch(builder, stream_id, end_bb, 4);

   for (int stream = 0; stream < 4; stream++) {
      LLVMBasicBlockRef bb;
      unsigned offset;

      if (!gsinfo->num_stream_output_components[stream])
         continue;

      if (stream > 0 && !ctx.so.num_outputs)
         continue;

      bb = LLVMInsertBasicBlockInContext(ctx.ac.context, end_bb, "out");
      LLVMAddCase(switch_inst, LLVMConstInt(ctx.ac.i32, stream, 0), bb);
      LLVMPositionBuilderAtEnd(builder, bb);

      /* Fetch vertex data from GSVS ring */
      offset = 0;
      for (i = 0; i < gsinfo->num_outputs; ++i) {
         for (unsigned chan = 0; chan < 4; chan++) {
            if (!(gsinfo->output_usagemask[i] & (1 << chan)) ||
                ((outputs[i].vertex_streams >> (chan * 2)) & 0x3) != stream) {
               outputs[i].values[chan] = LLVMGetUndef(ctx.ac.f32);
               continue;
            }

            LLVMValueRef soffset =
               LLVMConstInt(ctx.ac.i32, offset * gs_selector->info.base.gs.vertices_out * 16 * 4, 0);
            offset++;

            outputs[i].values[chan] =
               ac_build_buffer_load(&ctx.ac, ctx.gsvs_ring[0], 1, ctx.ac.i32_0, voffset, soffset,
                                    ctx.ac.f32, ac_glc | ac_slc, true, false);
         }
      }

      /* Streamout and exports. */
      if (!sscreen->use_ngg_streamout && ctx.so.num_outputs) {
         si_llvm_emit_streamout(&ctx, outputs, gsinfo->num_outputs, stream);
      }

      if (stream == 0)
         si_llvm_build_vs_exports(&ctx, NULL, outputs, gsinfo->num_outputs);

      LLVMBuildBr(builder, end_bb);
   }

   LLVMPositionBuilderAtEnd(builder, end_bb);

   LLVMBuildRetVoid(ctx.ac.builder);

   ctx.stage = MESA_SHADER_GEOMETRY; /* override for shader dumping */
   si_llvm_optimize_module(&ctx);

   bool ok = false;
   if (si_compile_llvm(sscreen, &ctx.shader->binary, &ctx.shader->config, ctx.compiler, &ctx.ac,
                       debug, MESA_SHADER_GEOMETRY, "GS Copy Shader", false)) {
      assert(!ctx.shader->config.scratch_bytes_per_wave);
      if (!ctx.shader->config.scratch_bytes_per_wave)
         ok = si_shader_binary_upload(sscreen, ctx.shader, 0);

      if (si_can_dump_shader(sscreen, MESA_SHADER_GEOMETRY))
         fprintf(stderr, "GS Copy Shader:\n");
      si_shader_dump(sscreen, ctx.shader, debug, stderr, true);
   }

   si_llvm_dispose(&ctx);

   if (!ok) {
      FREE(shader);
      shader = NULL;
   } else {
      si_fix_resource_usage(sscreen, shader);
   }
   return shader;
}

void si_llvm_init_gs_callbacks(struct si_shader_context *ctx)
{
   ctx->abi.emit_vertex = si_llvm_emit_vertex;
   ctx->abi.emit_primitive = si_llvm_emit_primitive;
}
