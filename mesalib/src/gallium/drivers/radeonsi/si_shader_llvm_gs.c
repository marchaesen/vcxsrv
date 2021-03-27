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

static LLVMValueRef si_llvm_load_input_gs(struct ac_shader_abi *abi, unsigned input_index,
                                          unsigned vtx_offset_param, LLVMTypeRef type,
                                          unsigned swizzle)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   struct si_shader *shader = ctx->shader;
   LLVMValueRef vtx_offset, soffset;
   struct si_shader_info *info = &shader->selector->info;
   unsigned param;
   LLVMValueRef value;

   param = si_shader_io_get_unique_index(info->input_semantic[input_index], false);

   /* GFX9 has the ESGS ring in LDS. */
   if (ctx->screen->info.chip_class >= GFX9) {
      unsigned index = vtx_offset_param;

      switch (index / 2) {
      case 0:
         vtx_offset = si_unpack_param(ctx, ctx->gs_vtx01_offset, index % 2 ? 16 : 0, 16);
         break;
      case 1:
         vtx_offset = si_unpack_param(ctx, ctx->gs_vtx23_offset, index % 2 ? 16 : 0, 16);
         break;
      case 2:
         vtx_offset = si_unpack_param(ctx, ctx->gs_vtx45_offset, index % 2 ? 16 : 0, 16);
         break;
      default:
         assert(0);
         return NULL;
      }

      unsigned offset = param * 4 + swizzle;
      vtx_offset =
         LLVMBuildAdd(ctx->ac.builder, vtx_offset, LLVMConstInt(ctx->ac.i32, offset, false), "");

      LLVMValueRef ptr = ac_build_gep0(&ctx->ac, ctx->esgs_ring, vtx_offset);
      LLVMValueRef value = LLVMBuildLoad(ctx->ac.builder, ptr, "");
      return LLVMBuildBitCast(ctx->ac.builder, value, type, "");
   }

   /* GFX6: input load from the ESGS ring in memory. */
   /* Get the vertex offset parameter on GFX6. */
   LLVMValueRef gs_vtx_offset = ac_get_arg(&ctx->ac, ctx->args.gs_vtx_offset[vtx_offset_param]);

   vtx_offset = LLVMBuildMul(ctx->ac.builder, gs_vtx_offset, LLVMConstInt(ctx->ac.i32, 4, 0), "");

   soffset = LLVMConstInt(ctx->ac.i32, (param * 4 + swizzle) * 256, 0);

   value = ac_build_buffer_load(&ctx->ac, ctx->esgs_ring, 1, ctx->ac.i32_0, vtx_offset, soffset, 0,
                                ctx->ac.f32, ac_glc, true, false);
   return LLVMBuildBitCast(ctx->ac.builder, value, type, "");
}

static LLVMValueRef si_nir_load_input_gs(struct ac_shader_abi *abi,
                                         unsigned driver_location, unsigned component,
                                         unsigned num_components, unsigned vertex_index,
                                         LLVMTypeRef type)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);

   LLVMValueRef value[4];
   for (unsigned i = component; i < component + num_components; i++) {
      value[i] = si_llvm_load_input_gs(&ctx->abi, driver_location,
                                       vertex_index, type, i);
   }

   return ac_build_varying_gather_values(&ctx->ac, value, num_components, component);
}

/* Pass GS inputs from ES to GS on GFX9. */
static void si_set_es_return_value_for_gs(struct si_shader_context *ctx)
{
   if (!ctx->shader->is_monolithic)
      ac_build_endif(&ctx->ac, ctx->merged_wrap_if_label);

   LLVMValueRef ret = ctx->return_value;

   ret = si_insert_input_ptr(ctx, ret, ctx->other_const_and_shader_buffers, 0);
   ret = si_insert_input_ptr(ctx, ret, ctx->other_samplers_and_images, 1);
   if (ctx->shader->key.as_ngg)
      ret = si_insert_input_ptr(ctx, ret, ctx->args.gs_tg_info, 2);
   else
      ret = si_insert_input_ret(ctx, ret, ctx->args.gs2vs_offset, 2);
   ret = si_insert_input_ret(ctx, ret, ctx->args.merged_wave_info, 3);
   ret = si_insert_input_ret(ctx, ret, ctx->args.scratch_offset, 5);

   ret = si_insert_input_ptr(ctx, ret, ctx->internal_bindings, 8 + SI_SGPR_INTERNAL_BINDINGS);
   ret = si_insert_input_ptr(ctx, ret, ctx->bindless_samplers_and_images,
                             8 + SI_SGPR_BINDLESS_SAMPLERS_AND_IMAGES);
   if (ctx->screen->use_ngg) {
      ret = si_insert_input_ptr(ctx, ret, ctx->vs_state_bits, 8 + SI_SGPR_VS_STATE_BITS);
   }

   unsigned vgpr = 8 + SI_NUM_VS_STATE_RESOURCE_SGPRS;

   ret = si_insert_input_ret_float(ctx, ret, ctx->gs_vtx01_offset, vgpr++);
   ret = si_insert_input_ret_float(ctx, ret, ctx->gs_vtx23_offset, vgpr++);
   ret = si_insert_input_ret_float(ctx, ret, ctx->args.gs_prim_id, vgpr++);
   ret = si_insert_input_ret_float(ctx, ret, ctx->args.gs_invocation_id, vgpr++);
   ret = si_insert_input_ret_float(ctx, ret, ctx->gs_vtx45_offset, vgpr++);
   ctx->return_value = ret;
}

void si_llvm_emit_es_epilogue(struct ac_shader_abi *abi, unsigned max_outputs, LLVMValueRef *addrs)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   struct si_shader *es = ctx->shader;
   struct si_shader_info *info = &es->selector->info;
   LLVMValueRef lds_base = NULL;
   unsigned chan;
   int i;

   if (ctx->screen->info.chip_class >= GFX9 && info->num_outputs) {
      unsigned itemsize_dw = es->selector->esgs_itemsize / 4;
      LLVMValueRef vertex_idx = ac_get_thread_id(&ctx->ac);
      LLVMValueRef wave_idx = si_unpack_param(ctx, ctx->args.merged_wave_info, 24, 4);
      vertex_idx =
         LLVMBuildOr(ctx->ac.builder, vertex_idx,
                     LLVMBuildMul(ctx->ac.builder, wave_idx,
                                  LLVMConstInt(ctx->ac.i32, ctx->ac.wave_size, false), ""),
                     "");
      lds_base =
         LLVMBuildMul(ctx->ac.builder, vertex_idx, LLVMConstInt(ctx->ac.i32, itemsize_dw, 0), "");
   }

   for (i = 0; i < info->num_outputs; i++) {
      int param;

      if (info->output_semantic[i] == VARYING_SLOT_VIEWPORT ||
          info->output_semantic[i] == VARYING_SLOT_LAYER)
         continue;

      param = si_shader_io_get_unique_index(info->output_semantic[i], false);

      for (chan = 0; chan < 4; chan++) {
         if (!(info->output_usagemask[i] & (1 << chan)))
            continue;

         LLVMValueRef out_val = LLVMBuildLoad(ctx->ac.builder, addrs[4 * i + chan], "");
         out_val = ac_to_integer(&ctx->ac, out_val);

         /* GFX9 has the ESGS ring in LDS. */
         if (ctx->screen->info.chip_class >= GFX9) {
            LLVMValueRef idx = LLVMConstInt(ctx->ac.i32, param * 4 + chan, false);
            idx = LLVMBuildAdd(ctx->ac.builder, lds_base, idx, "");
            ac_build_indexed_store(&ctx->ac, ctx->esgs_ring, idx, out_val);
            continue;
         }

         ac_build_buffer_store_dword(&ctx->ac, ctx->esgs_ring, out_val, 1, NULL,
                                     ac_get_arg(&ctx->ac, ctx->args.es2gs_offset),
                                     (4 * param + chan) * 4, ac_glc | ac_slc | ac_swizzled);
      }
   }

   if (ctx->screen->info.chip_class >= GFX9)
      si_set_es_return_value_for_gs(ctx);
}

static LLVMValueRef si_get_gs_wave_id(struct si_shader_context *ctx)
{
   if (ctx->screen->info.chip_class >= GFX9)
      return si_unpack_param(ctx, ctx->args.merged_wave_info, 16, 8);
   else
      return ac_get_arg(&ctx->ac, ctx->args.gs_wave_id);
}

static void emit_gs_epilogue(struct si_shader_context *ctx)
{
   if (ctx->shader->key.as_ngg) {
      gfx10_ngg_gs_emit_epilogue(ctx);
      return;
   }

   if (ctx->screen->info.chip_class >= GFX10)
      LLVMBuildFence(ctx->ac.builder, LLVMAtomicOrderingRelease, false, "");

   ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_NOP | AC_SENDMSG_GS_DONE, si_get_gs_wave_id(ctx));

   if (ctx->screen->info.chip_class >= GFX9)
      ac_build_endif(&ctx->ac, ctx->merged_wrap_if_label);
}

static void si_llvm_emit_gs_epilogue(struct ac_shader_abi *abi, unsigned max_outputs,
                                     LLVMValueRef *addrs)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   struct si_shader_info UNUSED *info = &ctx->shader->selector->info;

   assert(info->num_outputs <= max_outputs);

   emit_gs_epilogue(ctx);
}

/* Emit one vertex from the geometry shader */
static void si_llvm_emit_vertex(struct ac_shader_abi *abi, unsigned stream, LLVMValueRef *addrs)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);

   if (ctx->shader->key.as_ngg) {
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
   gs_next_vertex = LLVMBuildLoad(ctx->ac.builder, ctx->gs_next_vertex[stream], "");

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

         LLVMValueRef out_val = LLVMBuildLoad(ctx->ac.builder, addrs[4 * i + chan], "");
         LLVMValueRef voffset =
            LLVMConstInt(ctx->ac.i32, offset * shader->selector->info.base.gs.vertices_out, 0);
         offset++;

         voffset = LLVMBuildAdd(ctx->ac.builder, voffset, gs_next_vertex, "");
         voffset = LLVMBuildMul(ctx->ac.builder, voffset, LLVMConstInt(ctx->ac.i32, 4, 0), "");

         out_val = ac_to_integer(&ctx->ac, out_val);

         ac_build_buffer_store_dword(&ctx->ac, ctx->gsvs_ring[stream], out_val, 1, voffset, soffset,
                                     0, ac_glc | ac_slc | ac_swizzled);
      }
   }

   gs_next_vertex = LLVMBuildAdd(ctx->ac.builder, gs_next_vertex, ctx->ac.i32_1, "");
   LLVMBuildStore(ctx->ac.builder, gs_next_vertex, ctx->gs_next_vertex[stream]);

   /* Signal vertex emission if vertex data was written. */
   if (offset) {
      ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_EMIT | AC_SENDMSG_GS | (stream << 8),
                       si_get_gs_wave_id(ctx));
   }

   if (!use_kill)
      ac_build_endif(&ctx->ac, 6505);
}

/* Cut one primitive from the geometry shader */
static void si_llvm_emit_primitive(struct ac_shader_abi *abi, unsigned stream)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);

   if (ctx->shader->key.as_ngg) {
      LLVMBuildStore(ctx->ac.builder, ctx->ac.i32_0, ctx->gs_curprim_verts[stream]);
      return;
   }

   /* Signal primitive cut */
   ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_CUT | AC_SENDMSG_GS | (stream << 8),
                    si_get_gs_wave_id(ctx));
}

void si_preload_esgs_ring(struct si_shader_context *ctx)
{
   if (ctx->screen->info.chip_class <= GFX8) {
      unsigned ring = ctx->stage == MESA_SHADER_GEOMETRY ? SI_GS_RING_ESGS : SI_ES_RING_ESGS;
      LLVMValueRef offset = LLVMConstInt(ctx->ac.i32, ring, 0);
      LLVMValueRef buf_ptr = ac_get_arg(&ctx->ac, ctx->internal_bindings);

      ctx->esgs_ring = ac_build_load_to_sgpr(&ctx->ac, buf_ptr, offset);
   } else {
      if (USE_LDS_SYMBOLS && LLVM_VERSION_MAJOR >= 9) {
         /* Declare the ESGS ring as an explicit LDS symbol. */
         si_llvm_declare_esgs_ring(ctx);
      } else {
         ac_declare_lds_as_pointer(&ctx->ac);
         ctx->esgs_ring = ctx->ac.lds;
      }
   }
}

void si_preload_gs_rings(struct si_shader_context *ctx)
{
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
         LLVMConstInt(ctx->ac.i32, S_008F04_STRIDE(stride) | S_008F04_SWIZZLE_ENABLE(1), 0), "");
      ring = LLVMBuildInsertElement(builder, ring, tmp, ctx->ac.i32_1, "");
      ring = LLVMBuildInsertElement(builder, ring, LLVMConstInt(ctx->ac.i32, num_records, 0),
                                    LLVMConstInt(ctx->ac.i32, 2, 0), "");

      uint32_t rsrc3 =
         S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
         S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
         S_008F0C_INDEX_STRIDE(1) | /* index_stride = 16 (elements) */
         S_008F0C_ADD_TID_ENABLE(1);

      if (ctx->ac.chip_class >= GFX10) {
         rsrc3 |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
                  S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED) | S_008F0C_RESOURCE_LEVEL(1);
      } else {
         rsrc3 |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                  S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32) |
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
                                             struct pipe_debug_callback *debug)
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

   si_llvm_context_init(&ctx, sscreen, compiler,
                        si_get_wave_size(sscreen, MESA_SHADER_VERTEX,
                                         false, false, false, false));
   ctx.shader = shader;
   ctx.stage = MESA_SHADER_VERTEX;

   builder = ctx.ac.builder;

   si_llvm_create_main_func(&ctx, false);

   LLVMValueRef buf_ptr = ac_get_arg(&ctx.ac, ctx.internal_bindings);
   ctx.gsvs_ring[0] =
      ac_build_load_to_sgpr(&ctx.ac, buf_ptr, LLVMConstInt(ctx.ac.i32, SI_RING_GSVS, 0));

   LLVMValueRef voffset =
      LLVMBuildMul(ctx.ac.builder, ctx.abi.vertex_id, LLVMConstInt(ctx.ac.i32, 4, 0), "");

   /* Fetch the vertex stream ID.*/
   LLVMValueRef stream_id;

   if (!sscreen->use_ngg_streamout && gs_selector->so.num_outputs)
      stream_id = si_unpack_param(&ctx, ctx.args.streamout_config, 24, 2);
   else
      stream_id = ctx.ac.i32_0;

   /* Fill in output information. */
   for (i = 0; i < gsinfo->num_outputs; ++i) {
      outputs[i].semantic = gsinfo->output_semantic[i];

      for (int chan = 0; chan < 4; chan++) {
         outputs[i].vertex_stream[chan] = (gsinfo->output_streams[i] >> (2 * chan)) & 3;
      }
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

      if (stream > 0 && !gs_selector->so.num_outputs)
         continue;

      bb = LLVMInsertBasicBlockInContext(ctx.ac.context, end_bb, "out");
      LLVMAddCase(switch_inst, LLVMConstInt(ctx.ac.i32, stream, 0), bb);
      LLVMPositionBuilderAtEnd(builder, bb);

      /* Fetch vertex data from GSVS ring */
      offset = 0;
      for (i = 0; i < gsinfo->num_outputs; ++i) {
         for (unsigned chan = 0; chan < 4; chan++) {
            if (!(gsinfo->output_usagemask[i] & (1 << chan)) ||
                outputs[i].vertex_stream[chan] != stream) {
               outputs[i].values[chan] = LLVMGetUndef(ctx.ac.f32);
               continue;
            }

            LLVMValueRef soffset =
               LLVMConstInt(ctx.ac.i32, offset * gs_selector->info.base.gs.vertices_out * 16 * 4, 0);
            offset++;

            outputs[i].values[chan] =
               ac_build_buffer_load(&ctx.ac, ctx.gsvs_ring[0], 1, ctx.ac.i32_0, voffset, soffset, 0,
                                    ctx.ac.f32, ac_glc | ac_slc, true, false);
         }
      }

      /* Streamout and exports. */
      if (!sscreen->use_ngg_streamout && gs_selector->so.num_outputs) {
         si_llvm_emit_streamout(&ctx, outputs, gsinfo->num_outputs, stream);
      }

      if (stream == 0)
         si_llvm_build_vs_exports(&ctx, outputs, gsinfo->num_outputs);

      LLVMBuildBr(builder, end_bb);
   }

   LLVMPositionBuilderAtEnd(builder, end_bb);

   LLVMBuildRetVoid(ctx.ac.builder);

   ctx.stage = MESA_SHADER_GEOMETRY; /* override for shader dumping */
   si_llvm_optimize_module(&ctx);

   bool ok = false;
   if (si_compile_llvm(sscreen, &ctx.shader->binary, &ctx.shader->config, ctx.compiler, &ctx.ac,
                       debug, MESA_SHADER_GEOMETRY, "GS Copy Shader", false)) {
      if (si_can_dump_shader(sscreen, MESA_SHADER_GEOMETRY))
         fprintf(stderr, "GS Copy Shader:\n");
      si_shader_dump(sscreen, ctx.shader, debug, stderr, true);

      if (!ctx.shader->config.scratch_bytes_per_wave)
         ok = si_shader_binary_upload(sscreen, ctx.shader, 0);
      else
         ok = true;
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

/**
 * Build the GS prolog function. Rotate the input vertices for triangle strips
 * with adjacency.
 */
void si_llvm_build_gs_prolog(struct si_shader_context *ctx, union si_shader_part_key *key)
{
   unsigned num_sgprs, num_vgprs;
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMTypeRef returns[AC_MAX_ARGS];
   LLVMValueRef func, ret;

   memset(&ctx->args, 0, sizeof(ctx->args));

   if (ctx->screen->info.chip_class >= GFX9) {
      /* Other user SGPRs are not needed by GS. */
      num_sgprs = 8 + SI_NUM_VS_STATE_RESOURCE_SGPRS;
      num_vgprs = 5; /* ES inputs are not needed by GS */
   } else {
      num_sgprs = GFX6_GS_NUM_USER_SGPR + 2;
      num_vgprs = 8;
   }

   for (unsigned i = 0; i < num_sgprs; ++i) {
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      returns[i] = ctx->ac.i32;
   }

   for (unsigned i = 0; i < num_vgprs; ++i) {
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, NULL);
      returns[num_sgprs + i] = ctx->ac.f32;
   }

   /* Create the function. */
   si_llvm_create_func(ctx, "gs_prolog", returns, num_sgprs + num_vgprs, 0);
   func = ctx->main_fn;

   /* Copy inputs to outputs. This should be no-op, as the registers match,
    * but it will prevent the compiler from overwriting them unintentionally.
    */
   ret = ctx->return_value;
   for (unsigned i = 0; i < num_sgprs; i++) {
      LLVMValueRef p = LLVMGetParam(func, i);
      ret = LLVMBuildInsertValue(builder, ret, p, i, "");
   }
   for (unsigned i = 0; i < num_vgprs; i++) {
      LLVMValueRef p = LLVMGetParam(func, num_sgprs + i);
      p = ac_to_float(&ctx->ac, p);
      ret = LLVMBuildInsertValue(builder, ret, p, num_sgprs + i, "");
   }

   if (key->gs_prolog.states.tri_strip_adj_fix) {
      /* Remap the input vertices for every other primitive. */
      const struct ac_arg gfx6_vtx_params[6] = {
         {.used = true, .arg_index = num_sgprs},     {.used = true, .arg_index = num_sgprs + 1},
         {.used = true, .arg_index = num_sgprs + 3}, {.used = true, .arg_index = num_sgprs + 4},
         {.used = true, .arg_index = num_sgprs + 5}, {.used = true, .arg_index = num_sgprs + 6},
      };
      const struct ac_arg gfx9_vtx_params[3] = {
         {.used = true, .arg_index = num_sgprs},
         {.used = true, .arg_index = num_sgprs + 1},
         {.used = true, .arg_index = num_sgprs + 4},
      };
      LLVMValueRef vtx_in[6], vtx_out[6];
      LLVMValueRef prim_id, rotate;

      if (ctx->screen->info.chip_class >= GFX9) {
         for (unsigned i = 0; i < 3; i++) {
            vtx_in[i * 2] = si_unpack_param(ctx, gfx9_vtx_params[i], 0, 16);
            vtx_in[i * 2 + 1] = si_unpack_param(ctx, gfx9_vtx_params[i], 16, 16);
         }
      } else {
         for (unsigned i = 0; i < 6; i++)
            vtx_in[i] = ac_get_arg(&ctx->ac, gfx6_vtx_params[i]);
      }

      prim_id = LLVMGetParam(func, num_sgprs + 2);
      rotate = LLVMBuildTrunc(builder, prim_id, ctx->ac.i1, "");

      for (unsigned i = 0; i < 6; ++i) {
         LLVMValueRef base, rotated;
         base = vtx_in[i];
         rotated = vtx_in[(i + 4) % 6];
         vtx_out[i] = LLVMBuildSelect(builder, rotate, rotated, base, "");
      }

      if (ctx->screen->info.chip_class >= GFX9) {
         for (unsigned i = 0; i < 3; i++) {
            LLVMValueRef hi, out;

            hi = LLVMBuildShl(builder, vtx_out[i * 2 + 1], LLVMConstInt(ctx->ac.i32, 16, 0), "");
            out = LLVMBuildOr(builder, vtx_out[i * 2], hi, "");
            out = ac_to_float(&ctx->ac, out);
            ret = LLVMBuildInsertValue(builder, ret, out, gfx9_vtx_params[i].arg_index, "");
         }
      } else {
         for (unsigned i = 0; i < 6; i++) {
            LLVMValueRef out;

            out = ac_to_float(&ctx->ac, vtx_out[i]);
            ret = LLVMBuildInsertValue(builder, ret, out, gfx6_vtx_params[i].arg_index, "");
         }
      }
   }

   LLVMBuildRet(builder, ret);
}

void si_llvm_init_gs_callbacks(struct si_shader_context *ctx)
{
   ctx->abi.load_inputs = si_nir_load_input_gs;
   ctx->abi.emit_vertex = si_llvm_emit_vertex;
   ctx->abi.emit_primitive = si_llvm_emit_primitive;
   ctx->abi.emit_outputs = si_llvm_emit_gs_epilogue;
}
