/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#include "ac_llvm_cull.h"
#include "si_pipe.h"
#include "si_shader_internal.h"
#include "sid.h"
#include "util/u_memory.h"
#include "util/u_prim.h"

static LLVMValueRef get_wave_id_in_tg(struct si_shader_context *ctx)
{
   return si_unpack_param(ctx, ctx->args.merged_wave_info, 24, 4);
}

static LLVMValueRef get_tgsize(struct si_shader_context *ctx)
{
   return si_unpack_param(ctx, ctx->args.merged_wave_info, 28, 4);
}

LLVMValueRef gfx10_get_thread_id_in_tg(struct si_shader_context *ctx)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef tmp;
   tmp = LLVMBuildMul(builder, get_wave_id_in_tg(ctx),
                      LLVMConstInt(ctx->ac.i32, ctx->ac.wave_size, false), "");
   return LLVMBuildAdd(builder, tmp, ac_get_thread_id(&ctx->ac), "");
}

static LLVMValueRef ngg_get_vtx_cnt(struct si_shader_context *ctx)
{
   return si_unpack_param(ctx, ctx->args.gs_tg_info, 12, 9);
}

static LLVMValueRef ngg_get_prim_cnt(struct si_shader_context *ctx)
{
   return si_unpack_param(ctx, ctx->args.gs_tg_info, 22, 9);
}

static LLVMValueRef ngg_get_ordered_id(struct si_shader_context *ctx)
{
   return si_unpack_param(ctx, ctx->args.gs_tg_info, 0, 12);
}

static LLVMValueRef ngg_get_query_buf(struct si_shader_context *ctx)
{
   LLVMValueRef buf_ptr = ac_get_arg(&ctx->ac, ctx->internal_bindings);

   return ac_build_load_to_sgpr(&ctx->ac, buf_ptr,
                                LLVMConstInt(ctx->ac.i32, SI_GS_QUERY_BUF, false));
}

/**
 * Return the number of vertices as a constant in \p num_vertices,
 * and return a more precise value as LLVMValueRef from the function.
 */
static LLVMValueRef ngg_get_vertices_per_prim(struct si_shader_context *ctx, unsigned *num_vertices)
{
   const struct si_shader_info *info = &ctx->shader->selector->info;

   if (ctx->stage == MESA_SHADER_GEOMETRY) {
      *num_vertices = u_vertices_per_prim(info->base.gs.output_primitive);
      return LLVMConstInt(ctx->ac.i32, *num_vertices, false);
   } else if (ctx->stage == MESA_SHADER_VERTEX) {
      if (info->base.vs.blit_sgprs_amd) {
         /* Blits always use axis-aligned rectangles with 3 vertices. */
         *num_vertices = 3;
         return LLVMConstInt(ctx->ac.i32, 3, 0);
      } else if (ctx->shader->key.ge.opt.ngg_culling & SI_NGG_CULL_LINES) {
         *num_vertices = 2;
         return LLVMConstInt(ctx->ac.i32, 2, 0);
      } else {
         /* We always build up all three indices for the prim export
          * independent of the primitive type. The additional garbage
          * data shouldn't hurt. This is used by exports and streamout.
          */
         *num_vertices = 3;

         /* Extract OUTPRIM field. */
         LLVMValueRef num = si_unpack_param(ctx, ctx->vs_state_bits, 2, 2);
         return LLVMBuildAdd(ctx->ac.builder, num, ctx->ac.i32_1, "");
      }
   } else {
      assert(ctx->stage == MESA_SHADER_TESS_EVAL);

      if (info->base.tess.point_mode)
         *num_vertices = 1;
      else if (info->base.tess._primitive_mode == TESS_PRIMITIVE_ISOLINES)
         *num_vertices = 2;
      else
         *num_vertices = 3;

      return LLVMConstInt(ctx->ac.i32, *num_vertices, false);
   }
}

bool gfx10_ngg_export_prim_early(struct si_shader *shader)
{
   struct si_shader_selector *sel = shader->selector;

   assert(shader->key.ge.as_ngg && !shader->key.ge.as_es);

   return sel->info.stage != MESA_SHADER_GEOMETRY &&
          !gfx10_ngg_writes_user_edgeflags(shader);
}

void gfx10_ngg_build_sendmsg_gs_alloc_req(struct si_shader_context *ctx)
{
   /* Newer chips can use PRIMGEN_PASSTHRU_NO_MSG to skip gs_alloc_req for NGG passthrough. */
   if (gfx10_is_ngg_passthrough(ctx->shader) &&
       ctx->screen->info.family >= CHIP_DIMGREY_CAVEFISH)
      return;

   ac_build_sendmsg_gs_alloc_req(&ctx->ac, get_wave_id_in_tg(ctx), ngg_get_vtx_cnt(ctx),
                                 ngg_get_prim_cnt(ctx));
}

void gfx10_ngg_build_export_prim(struct si_shader_context *ctx, LLVMValueRef user_edgeflags[3],
                                 LLVMValueRef prim_passthrough)
{
   LLVMBuilderRef builder = ctx->ac.builder;

   if (gfx10_is_ngg_passthrough(ctx->shader) || ctx->shader->key.ge.opt.ngg_culling) {
      ac_build_ifcc(&ctx->ac, si_is_gs_thread(ctx), 6001);
      {
         struct ac_ngg_prim prim = {};

         if (prim_passthrough)
            prim.passthrough = prim_passthrough;
         else
            prim.passthrough = ac_get_arg(&ctx->ac, ctx->args.gs_vtx_offset[0]);

         /* This is only used with NGG culling, which returns the NGG
          * passthrough prim export encoding.
          */
         if (gfx10_ngg_writes_user_edgeflags(ctx->shader)) {
            unsigned all_bits_no_edgeflags = ~SI_NGG_PRIM_EDGE_FLAG_BITS;
            LLVMValueRef edgeflags = LLVMConstInt(ctx->ac.i32, all_bits_no_edgeflags, 0);

            unsigned num_vertices;
            ngg_get_vertices_per_prim(ctx, &num_vertices);

            for (unsigned i = 0; i < num_vertices; i++) {
               unsigned shift = 9 + i * 10;
               LLVMValueRef edge;

               edge = LLVMBuildLoad(builder, user_edgeflags[i], "");
               edge = LLVMBuildZExt(builder, edge, ctx->ac.i32, "");
               edge = LLVMBuildShl(builder, edge, LLVMConstInt(ctx->ac.i32, shift, 0), "");
               edgeflags = LLVMBuildOr(builder, edgeflags, edge, "");
            }
            prim.passthrough = LLVMBuildAnd(builder, prim.passthrough, edgeflags, "");
         }

         ac_build_export_prim(&ctx->ac, &prim);
      }
      ac_build_endif(&ctx->ac, 6001);
      return;
   }

   ac_build_ifcc(&ctx->ac, si_is_gs_thread(ctx), 6001);
   {
      struct ac_ngg_prim prim = {};

      ngg_get_vertices_per_prim(ctx, &prim.num_vertices);

      prim.isnull = ctx->ac.i1false;

      if (gfx10_edgeflags_have_effect(ctx->shader))
         prim.edgeflags = ac_pack_edgeflags_for_export(&ctx->ac, &ctx->args);
      else
         prim.edgeflags = ctx->ac.i32_0;

      for (unsigned i = 0; i < prim.num_vertices; ++i)
         prim.index[i] = si_unpack_param(ctx, ctx->args.gs_vtx_offset[i / 2], (i & 1) * 16, 16);

      if (gfx10_ngg_writes_user_edgeflags(ctx->shader)) {
         LLVMValueRef edgeflags = ctx->ac.i32_0;

         for (unsigned i = 0; i < prim.num_vertices; ++i) {
            LLVMValueRef edge;

            edge = LLVMBuildLoad(ctx->ac.builder, user_edgeflags[i], "");
            edge = LLVMBuildZExt(ctx->ac.builder, edge, ctx->ac.i32, "");
            edge = LLVMBuildShl(ctx->ac.builder, edge, LLVMConstInt(ctx->ac.i32, 9 + i*10, 0), "");
            edgeflags = LLVMBuildOr(ctx->ac.builder, edgeflags, edge, "");
         }
         prim.edgeflags = LLVMBuildAnd(ctx->ac.builder, prim.edgeflags, edgeflags, "");
      }

      ac_build_export_prim(&ctx->ac, &prim);
   }
   ac_build_endif(&ctx->ac, 6001);
}

static void build_streamout_vertex(struct si_shader_context *ctx, LLVMValueRef *so_buffer,
                                   LLVMValueRef *wg_offset_dw, unsigned stream,
                                   LLVMValueRef offset_vtx, LLVMValueRef vertexptr)
{
   struct si_shader_info *info = &ctx->shader->selector->info;
   struct pipe_stream_output_info *so = &ctx->shader->selector->so;
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef offset[4] = {};
   LLVMValueRef tmp;

   for (unsigned buffer = 0; buffer < 4; ++buffer) {
      if (!wg_offset_dw[buffer])
         continue;

      tmp = LLVMBuildMul(builder, offset_vtx, LLVMConstInt(ctx->ac.i32, so->stride[buffer], false),
                         "");
      tmp = LLVMBuildAdd(builder, wg_offset_dw[buffer], tmp, "");
      offset[buffer] = LLVMBuildShl(builder, tmp, LLVMConstInt(ctx->ac.i32, 2, false), "");
   }

   for (unsigned i = 0; i < so->num_outputs; ++i) {
      if (so->output[i].stream != stream)
         continue;

      unsigned reg = so->output[i].register_index;
      struct si_shader_output_values out;
      out.semantic = info->output_semantic[reg];

      for (unsigned comp = 0; comp < 4; comp++) {
         tmp = ac_build_gep0(&ctx->ac, vertexptr, LLVMConstInt(ctx->ac.i32, 4 * reg + comp, false));
         out.values[comp] = LLVMBuildLoad(builder, tmp, "");
         out.vertex_streams = info->output_streams[reg];
      }

      si_llvm_streamout_store_output(ctx, so_buffer, offset, &so->output[i], &out);
   }
}

struct ngg_streamout {
   LLVMValueRef num_vertices;

   /* per-thread data */
   LLVMValueRef prim_enable[4]; /* i1 per stream */
   LLVMValueRef vertices[3];    /* [N x i32] addrspace(LDS)* */

   /* Output */
   LLVMValueRef emit[4]; /* per-stream emitted primitives (only valid for used streams) */
};

/**
 * Build streamout logic.
 *
 * Implies a barrier.
 *
 * Writes number of emitted primitives to gs_ngg_scratch[4:8].
 *
 * Clobbers gs_ngg_scratch[8:].
 */
static void build_streamout(struct si_shader_context *ctx, struct ngg_streamout *nggso)
{
   struct si_shader_info *info = &ctx->shader->selector->info;
   struct pipe_stream_output_info *so = &ctx->shader->selector->so;
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef buf_ptr = ac_get_arg(&ctx->ac, ctx->internal_bindings);
   LLVMValueRef tid = gfx10_get_thread_id_in_tg(ctx);
   LLVMValueRef tmp, tmp2;
   LLVMValueRef i32_2 = LLVMConstInt(ctx->ac.i32, 2, false);
   LLVMValueRef i32_4 = LLVMConstInt(ctx->ac.i32, 4, false);
   LLVMValueRef i32_8 = LLVMConstInt(ctx->ac.i32, 8, false);
   LLVMValueRef so_buffer[4] = {};
   unsigned max_num_vertices = 1 + (nggso->vertices[1] ? 1 : 0) + (nggso->vertices[2] ? 1 : 0);
   LLVMValueRef prim_stride_dw[4] = {};
   LLVMValueRef prim_stride_dw_vgpr = LLVMGetUndef(ctx->ac.i32);
   int stream_for_buffer[4] = {-1, -1, -1, -1};
   unsigned bufmask_for_stream[4] = {};
   bool isgs = ctx->stage == MESA_SHADER_GEOMETRY;
   unsigned scratch_emit_base = isgs ? 4 : 0;
   LLVMValueRef scratch_emit_basev = isgs ? i32_4 : ctx->ac.i32_0;
   unsigned scratch_offset_base = isgs ? 8 : 4;
   LLVMValueRef scratch_offset_basev = isgs ? i32_8 : i32_4;

   ac_llvm_add_target_dep_function_attr(ctx->main_fn, "amdgpu-gds-size", 256);

   /* Determine the mapping of streamout buffers to vertex streams. */
   for (unsigned i = 0; i < so->num_outputs; ++i) {
      unsigned buf = so->output[i].output_buffer;
      unsigned stream = so->output[i].stream;
      assert(stream_for_buffer[buf] < 0 || stream_for_buffer[buf] == stream);
      stream_for_buffer[buf] = stream;
      bufmask_for_stream[stream] |= 1 << buf;
   }

   for (unsigned buffer = 0; buffer < 4; ++buffer) {
      if (stream_for_buffer[buffer] == -1)
         continue;

      assert(so->stride[buffer]);

      tmp = LLVMConstInt(ctx->ac.i32, so->stride[buffer], false);
      prim_stride_dw[buffer] = LLVMBuildMul(builder, tmp, nggso->num_vertices, "");
      prim_stride_dw_vgpr =
         ac_build_writelane(&ctx->ac, prim_stride_dw_vgpr, prim_stride_dw[buffer],
                            LLVMConstInt(ctx->ac.i32, buffer, false));

      so_buffer[buffer] = ac_build_load_to_sgpr(
         &ctx->ac, buf_ptr, LLVMConstInt(ctx->ac.i32, SI_VS_STREAMOUT_BUF0 + buffer, false));
   }

   tmp = LLVMBuildICmp(builder, LLVMIntEQ, get_wave_id_in_tg(ctx), ctx->ac.i32_0, "");
   ac_build_ifcc(&ctx->ac, tmp, 5200);
   {
      LLVMTypeRef gdsptr = LLVMPointerType(ctx->ac.i32, AC_ADDR_SPACE_GDS);
      LLVMValueRef gdsbase = LLVMBuildIntToPtr(builder, ctx->ac.i32_0, gdsptr, "");

      /* Advance the streamout offsets in GDS. */
      LLVMValueRef offsets_vgpr = ac_build_alloca_undef(&ctx->ac, ctx->ac.i32, "");
      LLVMValueRef generated_by_stream_vgpr = ac_build_alloca_undef(&ctx->ac, ctx->ac.i32, "");

      tmp = LLVMBuildICmp(builder, LLVMIntULT, ac_get_thread_id(&ctx->ac), i32_4, "");
      ac_build_ifcc(&ctx->ac, tmp, 5210);
      {
         if (isgs) {
            tmp = ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, tid);
            tmp = LLVMBuildLoad(builder, tmp, "");
         } else {
            tmp = ac_build_writelane(&ctx->ac, ctx->ac.i32_0, ngg_get_prim_cnt(ctx), ctx->ac.i32_0);
         }
         LLVMBuildStore(builder, tmp, generated_by_stream_vgpr);

         unsigned swizzle[4];
         int unused_stream = -1;
         for (unsigned stream = 0; stream < 4; ++stream) {
            if (!info->num_stream_output_components[stream]) {
               unused_stream = stream;
               break;
            }
         }
         for (unsigned buffer = 0; buffer < 4; ++buffer) {
            if (stream_for_buffer[buffer] >= 0) {
               swizzle[buffer] = stream_for_buffer[buffer];
            } else {
               assert(unused_stream >= 0);
               swizzle[buffer] = unused_stream;
            }
         }

         tmp = ac_build_quad_swizzle(&ctx->ac, tmp, swizzle[0], swizzle[1], swizzle[2], swizzle[3]);
         tmp = LLVMBuildMul(builder, tmp, prim_stride_dw_vgpr, "");

         LLVMValueRef args[] = {
            LLVMBuildIntToPtr(builder, ngg_get_ordered_id(ctx), gdsptr, ""),
            tmp,
            ctx->ac.i32_0,                             // ordering
            ctx->ac.i32_0,                             // scope
            ctx->ac.i1false,                           // isVolatile
            LLVMConstInt(ctx->ac.i32, 4 << 24, false), // OA index
            ctx->ac.i1true,                            // wave release
            ctx->ac.i1true,                            // wave done
         };
         tmp = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.ds.ordered.add", ctx->ac.i32, args,
                                  ARRAY_SIZE(args), 0);

         /* Keep offsets in a VGPR for quick retrieval via readlane by
          * the first wave for bounds checking, and also store in LDS
          * for retrieval by all waves later. */
         LLVMBuildStore(builder, tmp, offsets_vgpr);

         tmp2 = LLVMBuildAdd(builder, ac_get_thread_id(&ctx->ac), scratch_offset_basev, "");
         tmp2 = ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, tmp2);
         LLVMBuildStore(builder, tmp, tmp2);
      }
      ac_build_endif(&ctx->ac, 5210);

      /* Determine the max emit per buffer. This is done via the SALU, in part
       * because LLVM can't generate divide-by-multiply if we try to do this
       * via VALU with one lane per buffer.
       */
      LLVMValueRef max_emit[4] = {};
      for (unsigned buffer = 0; buffer < 4; ++buffer) {
         if (stream_for_buffer[buffer] == -1)
            continue;

         LLVMValueRef bufsize_dw = LLVMBuildLShr(
            builder, LLVMBuildExtractElement(builder, so_buffer[buffer], i32_2, ""), i32_2, "");

         tmp = LLVMBuildLoad(builder, offsets_vgpr, "");
         LLVMValueRef offset_dw =
            ac_build_readlane(&ctx->ac, tmp, LLVMConstInt(ctx->ac.i32, buffer, false));

         tmp = LLVMBuildSub(builder, bufsize_dw, offset_dw, "");
         tmp = LLVMBuildUDiv(builder, tmp, prim_stride_dw[buffer], "");

         tmp2 = LLVMBuildICmp(builder, LLVMIntULT, bufsize_dw, offset_dw, "");
         max_emit[buffer] = LLVMBuildSelect(builder, tmp2, ctx->ac.i32_0, tmp, "");
      }

      /* Determine the number of emitted primitives per stream and fixup the
       * GDS counter if necessary.
       *
       * This is complicated by the fact that a single stream can emit to
       * multiple buffers (but luckily not vice versa).
       */
      LLVMValueRef emit_vgpr = ctx->ac.i32_0;

      for (unsigned stream = 0; stream < 4; ++stream) {
         if (!info->num_stream_output_components[stream])
            continue;

         tmp = LLVMBuildLoad(builder, generated_by_stream_vgpr, "");
         LLVMValueRef generated =
            ac_build_readlane(&ctx->ac, tmp, LLVMConstInt(ctx->ac.i32, stream, false));

         LLVMValueRef emit = generated;
         for (unsigned buffer = 0; buffer < 4; ++buffer) {
            if (stream_for_buffer[buffer] == stream)
               emit = ac_build_umin(&ctx->ac, emit, max_emit[buffer]);
         }

         emit_vgpr =
            ac_build_writelane(&ctx->ac, emit_vgpr, emit, LLVMConstInt(ctx->ac.i32, stream, false));

         /* Fixup the offset using a plain GDS atomic if we overflowed. */
         tmp = LLVMBuildICmp(builder, LLVMIntULT, emit, generated, "");
         ac_build_ifcc(&ctx->ac, tmp, 5221); /* scalar branch */
         tmp = LLVMBuildLShr(builder, LLVMConstInt(ctx->ac.i32, bufmask_for_stream[stream], false),
                             ac_get_thread_id(&ctx->ac), "");
         tmp = LLVMBuildTrunc(builder, tmp, ctx->ac.i1, "");
         ac_build_ifcc(&ctx->ac, tmp, 5222);
         {
            tmp = LLVMBuildSub(builder, generated, emit, "");
            tmp = LLVMBuildMul(builder, tmp, prim_stride_dw_vgpr, "");
            tmp2 = LLVMBuildGEP(builder, gdsbase, &tid, 1, "");
            LLVMBuildAtomicRMW(builder, LLVMAtomicRMWBinOpSub, tmp2, tmp,
                               LLVMAtomicOrderingMonotonic, false);
         }
         ac_build_endif(&ctx->ac, 5222);
         ac_build_endif(&ctx->ac, 5221);
      }

      tmp = LLVMBuildICmp(builder, LLVMIntULT, ac_get_thread_id(&ctx->ac), i32_4, "");
      ac_build_ifcc(&ctx->ac, tmp, 5225);
      {
         tmp = LLVMBuildAdd(builder, ac_get_thread_id(&ctx->ac), scratch_emit_basev, "");
         tmp = ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, tmp);
         LLVMBuildStore(builder, emit_vgpr, tmp);
      }
      ac_build_endif(&ctx->ac, 5225);
   }
   ac_build_endif(&ctx->ac, 5200);

   /* Determine the workgroup-relative per-thread / primitive offset into
    * the streamout buffers */
   struct ac_wg_scan primemit_scan[4] = {};

   if (isgs) {
      for (unsigned stream = 0; stream < 4; ++stream) {
         if (!info->num_stream_output_components[stream])
            continue;

         primemit_scan[stream].enable_exclusive = true;
         primemit_scan[stream].op = nir_op_iadd;
         primemit_scan[stream].src = nggso->prim_enable[stream];
         primemit_scan[stream].scratch = ac_build_gep0(
            &ctx->ac, ctx->gs_ngg_scratch, LLVMConstInt(ctx->ac.i32, 12 + 8 * stream, false));
         primemit_scan[stream].waveidx = get_wave_id_in_tg(ctx);
         primemit_scan[stream].numwaves = get_tgsize(ctx);
         if (ctx->stage == MESA_SHADER_GEOMETRY) {
            /* ngg_subgroup_size is only the input size. GS can always generate up to 256 vertices. */
            primemit_scan[stream].maxwaves = DIV_ROUND_UP(256, ctx->ac.wave_size);
         } else {
            primemit_scan[stream].maxwaves = DIV_ROUND_UP(ctx->screen->ngg_subgroup_size,
                                                          ctx->ac.wave_size);
         }
         ac_build_wg_scan_top(&ctx->ac, &primemit_scan[stream]);
      }
   }

   ac_build_s_barrier(&ctx->ac);

   /* Fetch the per-buffer offsets and per-stream emit counts in all waves. */
   LLVMValueRef wgoffset_dw[4] = {};

   {
      LLVMValueRef scratch_vgpr;

      tmp = ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, ac_get_thread_id(&ctx->ac));
      scratch_vgpr = LLVMBuildLoad(builder, tmp, "");

      for (unsigned buffer = 0; buffer < 4; ++buffer) {
         if (stream_for_buffer[buffer] >= 0) {
            wgoffset_dw[buffer] =
               ac_build_readlane(&ctx->ac, scratch_vgpr,
                                 LLVMConstInt(ctx->ac.i32, scratch_offset_base + buffer, false));
         }
      }

      for (unsigned stream = 0; stream < 4; ++stream) {
         if (info->num_stream_output_components[stream]) {
            nggso->emit[stream] =
               ac_build_readlane(&ctx->ac, scratch_vgpr,
                                 LLVMConstInt(ctx->ac.i32, scratch_emit_base + stream, false));
         }
      }
   }

   /* Write out primitive data */
   for (unsigned stream = 0; stream < 4; ++stream) {
      if (!info->num_stream_output_components[stream])
         continue;

      if (isgs) {
         ac_build_wg_scan_bottom(&ctx->ac, &primemit_scan[stream]);
      } else {
         primemit_scan[stream].result_exclusive = tid;
      }

      tmp = LLVMBuildICmp(builder, LLVMIntULT, primemit_scan[stream].result_exclusive,
                          nggso->emit[stream], "");
      tmp = LLVMBuildAnd(builder, tmp, nggso->prim_enable[stream], "");
      ac_build_ifcc(&ctx->ac, tmp, 5240);
      {
         LLVMValueRef offset_vtx =
            LLVMBuildMul(builder, primemit_scan[stream].result_exclusive, nggso->num_vertices, "");

         for (unsigned i = 0; i < max_num_vertices; ++i) {
            tmp = LLVMBuildICmp(builder, LLVMIntULT, LLVMConstInt(ctx->ac.i32, i, false),
                                nggso->num_vertices, "");
            ac_build_ifcc(&ctx->ac, tmp, 5241);
            build_streamout_vertex(ctx, so_buffer, wgoffset_dw, stream, offset_vtx,
                                   nggso->vertices[i]);
            ac_build_endif(&ctx->ac, 5241);
            offset_vtx = LLVMBuildAdd(builder, offset_vtx, ctx->ac.i32_1, "");
         }
      }
      ac_build_endif(&ctx->ac, 5240);
   }
}

/* LDS layout of ES vertex data for NGG culling. */
enum
{
   /* Byte 0: Boolean ES thread accepted (unculled) flag.
    * Byte 1: New ES thread ID, loaded by GS to prepare the prim export value.
    * Byte 2: TES rel patch ID
    * Byte 3: 8-bit clip distance mask: 1 means the clip distance is negative.
    *         The mask from all vertices is AND'ed. If the result is non-zero,
    *         the primitive is culled.
    */
   lds_byte0_accept_flag = 0,
   lds_byte1_new_thread_id,
   lds_byte2_tes_rel_patch_id,
   lds_byte3_clipdist_neg_mask,

   lds_packed_data = 0, /* lds_byteN_... */
   lds_pos_cull_x_div_w,
   lds_pos_cull_y_div_w,
   lds_pos_cull_w,

   lds_pos_x = lds_packed_data + 1,
   lds_pos_y,
   lds_pos_z,
   lds_pos_w,
   /* If VS: */
   lds_vertex_id,
   lds_instance_id, /* optional */
   /* If TES: */
   lds_tes_u = lds_vertex_id,
   lds_tes_v = lds_instance_id,
   lds_tes_patch_id, /* optional */
};

static LLVMValueRef si_build_gep_i8_var(struct si_shader_context *ctx, LLVMValueRef ptr,
                                        LLVMValueRef index)
{
   LLVMTypeRef pi8 = LLVMPointerType(ctx->ac.i8, AC_ADDR_SPACE_LDS);

   return LLVMBuildGEP(ctx->ac.builder, LLVMBuildPointerCast(ctx->ac.builder, ptr, pi8, ""), &index,
                       1, "");
}

static LLVMValueRef si_build_gep_i8(struct si_shader_context *ctx, LLVMValueRef ptr,
                                    unsigned byte_index)
{
   assert(byte_index < 4);
   return si_build_gep_i8_var(ctx, ptr, LLVMConstInt(ctx->ac.i32, byte_index, 0));
}

static unsigned ngg_nogs_vertex_size(struct si_shader *shader)
{
   unsigned lds_vertex_size = 0;

   /* The edgeflag is always stored in the last element that's also
    * used for padding to reduce LDS bank conflicts. */
   if (shader->selector->so.num_outputs)
      lds_vertex_size = 4 * shader->selector->info.num_outputs + 1;
   if (gfx10_ngg_writes_user_edgeflags(shader))
      lds_vertex_size = MAX2(lds_vertex_size, 1);

   /* LDS size for passing data from GS to ES.
    * GS stores Primitive IDs into LDS at the address corresponding
    * to the ES thread of the provoking vertex. All ES threads
    * load and export PrimitiveID for their thread.
    */
   if (shader->selector->info.stage == MESA_SHADER_VERTEX && shader->key.ge.mono.u.vs_export_prim_id)
      lds_vertex_size = MAX2(lds_vertex_size, 1);

   if (shader->key.ge.opt.ngg_culling) {
      if (shader->selector->info.stage == MESA_SHADER_VERTEX) {
         STATIC_ASSERT(lds_instance_id + 1 == 7);
         lds_vertex_size = MAX2(lds_vertex_size, 7);
      } else {
         assert(shader->selector->info.stage == MESA_SHADER_TESS_EVAL);

         if (shader->selector->info.uses_primid || shader->key.ge.mono.u.vs_export_prim_id) {
            STATIC_ASSERT(lds_tes_patch_id + 2 == 9); /* +1 for LDS padding */
            lds_vertex_size = MAX2(lds_vertex_size, 9);
         } else {
            STATIC_ASSERT(lds_tes_v + 1 == 7);
            lds_vertex_size = MAX2(lds_vertex_size, 7);
         }
      }
   }

   return lds_vertex_size;
}

/**
 * Returns an `[N x i32] addrspace(LDS)*` pointing at contiguous LDS storage
 * for the vertex outputs.
 */
static LLVMValueRef ngg_nogs_vertex_ptr(struct si_shader_context *ctx, LLVMValueRef vtxid)
{
   /* The extra dword is used to avoid LDS bank conflicts. */
   unsigned vertex_size = ngg_nogs_vertex_size(ctx->shader);
   LLVMTypeRef ai32 = LLVMArrayType(ctx->ac.i32, vertex_size);
   LLVMTypeRef pai32 = LLVMPointerType(ai32, AC_ADDR_SPACE_LDS);
   LLVMValueRef tmp = LLVMBuildBitCast(ctx->ac.builder, ctx->esgs_ring, pai32, "");
   return LLVMBuildGEP(ctx->ac.builder, tmp, &vtxid, 1, "");
}

static LLVMValueRef si_insert_input_v4i32(struct si_shader_context *ctx, LLVMValueRef ret,
                                          struct ac_arg param, unsigned return_index)
{
   LLVMValueRef v = ac_get_arg(&ctx->ac, param);

   for (unsigned i = 0; i < 4; i++) {
      ret = LLVMBuildInsertValue(ctx->ac.builder, ret, ac_llvm_extract_elem(&ctx->ac, v, i),
                                 return_index + i, "");
   }
   return ret;
}

static void load_vertex_counts(struct si_shader_context *ctx, LLVMValueRef lds,
                               unsigned max_waves, LLVMValueRef tid,
                               LLVMValueRef *total_count,
                               LLVMValueRef *prefix_sum)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef i8vec4_lane = ac_build_alloca_undef(&ctx->ac, ctx->ac.i32, "");
   unsigned num_i8vec4 = DIV_ROUND_UP(max_waves, 4);

   /* If all threads loaded the vertex counts, it would cause many LDS bank conflicts
    * and the performance could decrease up to WaveSize times (32x or 64x).
    *
    * Therefore, only load the i-th tuple of vertex counts in the i-th thread. Other threads will
    * get them through readlane. 4 8-bit vertex counts are loaded per thread.
    */
   ac_build_ifcc(&ctx->ac, LLVMBuildICmp(builder, LLVMIntULT, tid,
                                         LLVMConstInt(ctx->ac.i32, num_i8vec4, 0), ""), 17771);
   LLVMBuildStore(builder, LLVMBuildLoad(builder, ac_build_gep0(&ctx->ac, lds, tid), ""), i8vec4_lane);
   ac_build_endif(&ctx->ac, 17771);

   /* Compute the number of ES waves. */
   LLVMValueRef num_waves = get_tgsize(ctx);

   /* Compute a byte mask where each byte is either 0 or 0xff depending on whether the wave
    * exists. We need the mask to clear uninitialized bytes in LDS and to compute the prefix sum.
    *
    * 8 waves: valid_mask = ~0ull >> (64 - num_waves * 8)
    * 4 waves: valid_mask = ~0 >> (32 - num_waves * 8)
    */
   LLVMValueRef num_waves8 = LLVMBuildShl(builder, num_waves, LLVMConstInt(ctx->ac.i32, 3, 0), "");
   LLVMValueRef valid_mask;

   if (max_waves > 4) {
      LLVMValueRef num_waves8_rev = LLVMBuildSub(builder, LLVMConstInt(ctx->ac.i32, 64, 0),
                                                 num_waves8, "");
      valid_mask = LLVMBuildLShr(builder, LLVMConstInt(ctx->ac.i64, ~0ull, 0),
                                 LLVMBuildZExt(builder, num_waves8_rev, ctx->ac.i64, ""), "");
   } else {
      LLVMValueRef num_waves8_rev = LLVMBuildSub(builder, LLVMConstInt(ctx->ac.i32, 32, 0),
                                                 num_waves8, "");
      valid_mask = LLVMBuildLShr(builder, LLVMConstInt(ctx->ac.i32, ~0, 0), num_waves8_rev, "");
   }

   /* Compute a byte mask where bytes below wave_id are 0xff, else they are 0.
    *
    * prefix_mask = ~(~0 << (wave_id * 8))
    */
   LLVMTypeRef type = max_waves > 4 ? ctx->ac.i64 : ctx->ac.i32;
   LLVMValueRef wave_id8 = LLVMBuildShl(builder, get_wave_id_in_tg(ctx),
                                        LLVMConstInt(ctx->ac.i32, 3, 0), "");
   LLVMValueRef prefix_mask =
      LLVMBuildNot(builder, LLVMBuildShl(builder, LLVMConstInt(type, ~0ull, 0),
                                         LLVMBuildZExt(builder, wave_id8, type, ""), ""), "");

   /* Compute the total vertex count and the vertex count of previous waves (prefix). */
   *total_count = ctx->ac.i32_0;
   *prefix_sum = ctx->ac.i32_0;

   for (unsigned i = 0; i < num_i8vec4; i++) {
      LLVMValueRef i8vec4;

      i8vec4 = ac_build_readlane_no_opt_barrier(&ctx->ac, LLVMBuildLoad(builder, i8vec4_lane, ""),
                                                LLVMConstInt(ctx->ac.i32, i, 0));
      /* Inactive waves have uninitialized vertex counts. Set them to 0 using this. */
      i8vec4 = LLVMBuildAnd(builder, i8vec4,
                            ac_unpack_param(&ctx->ac, valid_mask, 32 * i, 32), "");
      /* Compute the sum of all i8vec4 components and add it to the result. */
      *total_count = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.sad.u8", ctx->ac.i32,
                                        (LLVMValueRef[]){i8vec4, ctx->ac.i32_0, *total_count},
                                        3, AC_FUNC_ATTR_READNONE);
      ac_set_range_metadata(&ctx->ac, *total_count, 0, 64*4 + 1); /* the result is at most 64*4 */

      /* Compute the sum of the vertex counts of all previous waves. */
      i8vec4 = LLVMBuildAnd(builder, i8vec4,
                                ac_unpack_param(&ctx->ac, prefix_mask, 32 * i, 32), "");
      *prefix_sum = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.sad.u8", ctx->ac.i32,
                                       (LLVMValueRef[]){i8vec4, ctx->ac.i32_0, *prefix_sum},
                                       3, AC_FUNC_ATTR_READNONE);
      ac_set_range_metadata(&ctx->ac, *prefix_sum, 0, 64*4 + 1); /* the result is at most 64*4 */
   }
   *total_count = ac_build_readlane_no_opt_barrier(&ctx->ac, *total_count, NULL);
}

/**
 * Given a total thread count, update total and per-wave thread counts in input SGPRs
 * and return the per-wave thread count.
 *
 * \param new_num_threads    Total thread count on the input, per-wave thread count on the output.
 * \param tg_info            tg_info SGPR value
 * \param tg_info_num_bits   the bit size of thread count field in tg_info
 * \param tg_info_shift      the bit offset of the thread count field in tg_info
 * \param wave_info          merged_wave_info SGPR value
 * \param wave_info_num_bits the bit size of thread count field in merged_wave_info
 * \param wave_info_shift    the bit offset of the thread count field in merged_wave_info
 */
static void update_thread_counts(struct si_shader_context *ctx, LLVMValueRef *new_num_threads,
                                 LLVMValueRef *tg_info, unsigned tg_info_num_bits,
                                 unsigned tg_info_shift, LLVMValueRef *wave_info,
                                 unsigned wave_info_num_bits, unsigned wave_info_shift)
{
   LLVMBuilderRef builder = ctx->ac.builder;

   /* Update the total thread count. */
   unsigned tg_info_mask = ~(u_bit_consecutive(0, tg_info_num_bits) << tg_info_shift);
   *tg_info = LLVMBuildAnd(builder, *tg_info, LLVMConstInt(ctx->ac.i32, tg_info_mask, 0), "");
   *tg_info = LLVMBuildOr(
      builder, *tg_info,
      LLVMBuildShl(builder, *new_num_threads, LLVMConstInt(ctx->ac.i32, tg_info_shift, 0), ""), "");

   /* Update the per-wave thread count. */
   LLVMValueRef prev_threads = LLVMBuildMul(builder, get_wave_id_in_tg(ctx),
                                            LLVMConstInt(ctx->ac.i32, ctx->ac.wave_size, 0), "");
   *new_num_threads = LLVMBuildSub(builder, *new_num_threads, prev_threads, "");
   *new_num_threads = ac_build_imax(&ctx->ac, *new_num_threads, ctx->ac.i32_0);
   *new_num_threads =
      ac_build_imin(&ctx->ac, *new_num_threads, LLVMConstInt(ctx->ac.i32, ctx->ac.wave_size, 0));
   unsigned wave_info_mask = ~(u_bit_consecutive(0, wave_info_num_bits) << wave_info_shift);
   *wave_info = LLVMBuildAnd(builder, *wave_info, LLVMConstInt(ctx->ac.i32, wave_info_mask, 0), "");
   *wave_info = LLVMBuildOr(
      builder, *wave_info,
      LLVMBuildShl(builder, *new_num_threads, LLVMConstInt(ctx->ac.i32, wave_info_shift, 0), ""),
      "");
}

static void gfx10_build_primitive_accepted(struct ac_llvm_context *ac, LLVMValueRef accepted,
                                           void *userdata)
{
   struct si_shader_context *ctx = container_of(ac, struct si_shader_context, ac);
   LLVMValueRef *params = (LLVMValueRef *)userdata;
   LLVMValueRef gs_accepted = params[0];
   LLVMValueRef *gs_vtxptr = (LLVMValueRef *)params[1];

   unsigned num_vertices;
   ngg_get_vertices_per_prim(ctx, &num_vertices);

   ac_build_ifcc(&ctx->ac, accepted, 0);
   LLVMBuildStore(ctx->ac.builder, ctx->ac.i32_1, gs_accepted);

   if (gs_vtxptr) {
      for (unsigned vtx = 0; vtx < num_vertices; vtx++) {
         LLVMBuildStore(ctx->ac.builder, ctx->ac.i8_1,
                        si_build_gep_i8(ctx, gs_vtxptr[vtx], lds_byte0_accept_flag));
      }
   }
   ac_build_endif(&ctx->ac, 0);
}

static void add_clipdist_bit(struct si_shader_context *ctx, LLVMValueRef distance, unsigned i,
                             LLVMValueRef *packed_data)
{
   LLVMValueRef neg = LLVMBuildFCmp(ctx->ac.builder, LLVMRealOLT, distance, ctx->ac.f32_0, "");
   neg = LLVMBuildZExt(ctx->ac.builder, neg, ctx->ac.i32, "");
   /* Put the negative distance flag into lds_byte3_clipdist_neg_mask. */
   neg = LLVMBuildShl(ctx->ac.builder, neg, LLVMConstInt(ctx->ac.i32, 24 + i, 0), "");
   *packed_data = LLVMBuildOr(ctx->ac.builder, *packed_data, neg, "");
}

static bool add_clipdist_bits_for_clipvertex(struct si_shader_context *ctx,
                                             unsigned clipdist_enable,
                                             LLVMValueRef clipvertex[4],
                                             LLVMValueRef *packed_data)
{
   struct ac_export_args clipdist[2];
   bool added = false;

   si_llvm_clipvertex_to_clipdist(ctx, clipdist, clipvertex);

   for (unsigned j = 0; j < 8; j++) {
      if (!(clipdist_enable & BITFIELD_BIT(j)))
         continue;

      LLVMValueRef distance = clipdist[j / 4].out[j % 4];
      add_clipdist_bit(ctx, distance, j, packed_data);
      added = true;
   }
   return added;
}

static void cull_primitive(struct si_shader_context *ctx,
                           LLVMValueRef pos[3][4], LLVMValueRef clipdist_accepted,
                           LLVMValueRef out_prim_accepted, LLVMValueRef gs_vtxptr_accept[3])
{
   struct si_shader *shader = ctx->shader;
   LLVMBuilderRef builder = ctx->ac.builder;

   LLVMValueRef vp_scale[2] = {}, vp_translate[2] = {}, small_prim_precision = NULL;
   LLVMValueRef clip_half_line_width[2] = {};

   /* Load the viewport state for small prim culling. */
   bool prim_is_lines = shader->key.ge.opt.ngg_culling & SI_NGG_CULL_LINES;
   LLVMValueRef ptr = ac_get_arg(&ctx->ac, ctx->small_prim_cull_info);
   /* Lines will always use the non-AA viewport transformation. */
   LLVMValueRef vp = ac_build_load_to_sgpr(&ctx->ac, ptr,
                                           prim_is_lines ? ctx->ac.i32_1 : ctx->ac.i32_0);
   vp = LLVMBuildBitCast(builder, vp, ctx->ac.v4f32, "");
   vp_scale[0] = ac_llvm_extract_elem(&ctx->ac, vp, 0);
   vp_scale[1] = ac_llvm_extract_elem(&ctx->ac, vp, 1);
   vp_translate[0] = ac_llvm_extract_elem(&ctx->ac, vp, 2);
   vp_translate[1] = ac_llvm_extract_elem(&ctx->ac, vp, 3);

   /* Execute culling code. */
   struct ac_cull_options options = {};
   options.cull_view_xy = true;
   options.cull_w = true;

   if (prim_is_lines) {
      LLVMValueRef terms = ac_build_load_to_sgpr(&ctx->ac, ptr, LLVMConstInt(ctx->ac.i32, 2, 0));
      terms = LLVMBuildBitCast(builder, terms, ctx->ac.v4f32, "");
      clip_half_line_width[0] = ac_llvm_extract_elem(&ctx->ac, terms, 0);
      clip_half_line_width[1] = ac_llvm_extract_elem(&ctx->ac, terms, 1);
      small_prim_precision = ac_llvm_extract_elem(&ctx->ac, terms, 2);

      options.num_vertices = 2;
      options.cull_small_prims = shader->key.ge.opt.ngg_culling & SI_NGG_CULL_SMALL_LINES_DIAMOND_EXIT;

      assert(!(shader->key.ge.opt.ngg_culling & SI_NGG_CULL_BACK_FACE));
      assert(!(shader->key.ge.opt.ngg_culling & SI_NGG_CULL_FRONT_FACE));
   } else {
      /* Get the small prim filter precision. */
      small_prim_precision = si_unpack_param(ctx, ctx->vs_state_bits, 7, 4);
      small_prim_precision =
         LLVMBuildOr(builder, small_prim_precision, LLVMConstInt(ctx->ac.i32, 0x70, 0), "");
      small_prim_precision =
         LLVMBuildShl(builder, small_prim_precision, LLVMConstInt(ctx->ac.i32, 23, 0), "");
      small_prim_precision = LLVMBuildBitCast(builder, small_prim_precision, ctx->ac.f32, "");

      options.num_vertices = 3;
      options.cull_front = shader->key.ge.opt.ngg_culling & SI_NGG_CULL_FRONT_FACE;
      options.cull_back = shader->key.ge.opt.ngg_culling & SI_NGG_CULL_BACK_FACE;
      options.cull_small_prims = true; /* this would only be false with conservative rasterization */
      options.cull_zero_area = options.cull_front || options.cull_back;
   }

   /* Tell ES threads whether their vertex survived. */
   LLVMValueRef params[] = {
      out_prim_accepted,
      (void*)gs_vtxptr_accept,
   };
   ac_cull_primitive(&ctx->ac, pos, clipdist_accepted, vp_scale, vp_translate,
                     small_prim_precision, clip_half_line_width,
                     &options, gfx10_build_primitive_accepted, params);
}

/**
 * Cull primitives for NGG VS or TES, then compact vertices, which happens
 * before the VS or TES main function. Return values for the main function.
 * Also return the position, which is passed to the shader as an input,
 * so that we don't compute it twice.
 */
void gfx10_emit_ngg_culling_epilogue(struct ac_shader_abi *abi)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   struct si_shader *shader = ctx->shader;
   struct si_shader_selector *sel = shader->selector;
   struct si_shader_info *info = &sel->info;
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef *addrs = abi->outputs;
   unsigned max_waves = DIV_ROUND_UP(ctx->screen->ngg_subgroup_size, ctx->ac.wave_size);

   assert(shader->key.ge.opt.ngg_culling);
   assert(shader->key.ge.as_ngg);
   assert(sel->info.stage == MESA_SHADER_VERTEX ||
          (sel->info.stage == MESA_SHADER_TESS_EVAL && !shader->key.ge.as_es));

   LLVMValueRef es_vtxptr = ngg_nogs_vertex_ptr(ctx, gfx10_get_thread_id_in_tg(ctx));
   LLVMValueRef packed_data = ctx->ac.i32_0;
   LLVMValueRef position[4] = {};
   unsigned pos_index = 0;
   unsigned clip_plane_enable = SI_NGG_CULL_GET_CLIP_PLANE_ENABLE(shader->key.ge.opt.ngg_culling);
   unsigned clipdist_enable = (sel->clipdist_mask & clip_plane_enable) | sel->culldist_mask;
   bool has_clipdist_mask = false;

   for (unsigned i = 0; i < info->num_outputs; i++) {
      LLVMValueRef clipvertex[4];
      unsigned base;

      switch (info->output_semantic[i]) {
      case VARYING_SLOT_POS:
         /* If we are going to cull everything (rasterizer_discard), discard
          * the position. This is useful for analyzing maximum theoretical
          * performance without VS input loads.
          */
         if (shader->key.ge.opt.ngg_culling & SI_NGG_CULL_FRONT_FACE &&
             shader->key.ge.opt.ngg_culling & SI_NGG_CULL_BACK_FACE) {
            for (unsigned j = 0; j < 4; j++)
               LLVMBuildStore(builder, LLVMGetUndef(ctx->ac.f32), addrs[4 * i + j]);
            break;
         }

         pos_index = i;
         for (unsigned j = 0; j < 4; j++) {
            position[j] = LLVMBuildLoad(ctx->ac.builder, addrs[4 * i + j], "");
         }

         /* Store Position.W into LDS. */
         LLVMBuildStore(
            builder, ac_to_integer(&ctx->ac, position[3]),
            ac_build_gep0(&ctx->ac, es_vtxptr, LLVMConstInt(ctx->ac.i32, lds_pos_cull_w, 0)));

         /* Store Position.XY / W into LDS. */
         for (unsigned chan = 0; chan < 2; chan++) {
            LLVMValueRef val = ac_build_fdiv(&ctx->ac, position[chan], position[3]);
            LLVMBuildStore(
               builder, ac_to_integer(&ctx->ac, val),
               ac_build_gep0(&ctx->ac, es_vtxptr, LLVMConstInt(ctx->ac.i32, lds_pos_cull_x_div_w + chan, 0)));
         }
         break;

      case VARYING_SLOT_CLIP_DIST0:
      case VARYING_SLOT_CLIP_DIST1:
         base = info->output_semantic[i] == VARYING_SLOT_CLIP_DIST1 ? 4 : 0;

         for (unsigned j = 0; j < 4; j++) {
            unsigned index = base + j;

            if (!(clipdist_enable & BITFIELD_BIT(index)))
               continue;

            LLVMValueRef distance = LLVMBuildLoad(ctx->ac.builder, addrs[4 * i + j], "");
            add_clipdist_bit(ctx, distance, index, &packed_data);
            has_clipdist_mask = true;
         }
         break;

      case VARYING_SLOT_CLIP_VERTEX:
         for (unsigned j = 0; j < 4; j++)
            clipvertex[j] = LLVMBuildLoad(ctx->ac.builder, addrs[4 * i + j], "");

         if (add_clipdist_bits_for_clipvertex(ctx, clipdist_enable, clipvertex, &packed_data))
            has_clipdist_mask = true;
         break;
      }
   }

   if (clip_plane_enable && !sel->clipdist_mask) {
      /* When clip planes are enabled and there are no clip distance outputs,
       * we should use user clip planes and cull against the position.
       */
      assert(!has_clipdist_mask);
      if (add_clipdist_bits_for_clipvertex(ctx, clipdist_enable, position, &packed_data))
         has_clipdist_mask = true;
   }

   /* Initialize the packed data. */
   LLVMBuildStore(
      builder, packed_data,
      ac_build_gep0(&ctx->ac, es_vtxptr, LLVMConstInt(ctx->ac.i32, lds_packed_data, 0)));
   ac_build_endif(&ctx->ac, ctx->merged_wrap_if_label);
   ac_build_s_barrier(&ctx->ac);

   LLVMValueRef tid = ac_get_thread_id(&ctx->ac);

   unsigned num_vertices;
   ngg_get_vertices_per_prim(ctx, &num_vertices);

   /* The hardware requires that there are no holes between unculled vertices,
    * which means we have to pack ES threads, i.e. reduce the ES thread count
    * and move ES input VGPRs to lower threads. The upside is that varyings
    * are only fetched and computed for unculled vertices.
    *
    * Vertex compaction:
    *
    * Part 1: Store the surviving vertex count for each wave in LDS.
    *   - The GS culling code notifies ES threads which vertices were accepted.
    *   - Barrier
    *   - ES threads will compute the vertex count and store it in LDS.
    * - Barrier
    * - Each wave loads the vertex counts from LDS.
    *
    * Part 2: Compact ES threads:
    * - Compute the prefix sum for each surviving vertex. This is the new thread ID
    *   of the vertex.
    * - Write input VGPRs and vertex positions for each surviving vertex into the LDS
    *   address of the new thread ID.
    * - Now kill all waves that have inactive threads.
    * - Barrier
    * - Update vertex indices and null flag in the GS input VGPRs.
    *
    * Part 3: Update inputs GPRs
    * - For all waves, update per-wave thread counts in input SGPRs.
    * - In ES threads, update the ES input VGPRs (VertexID, InstanceID, TES inputs).
    */

   LLVMValueRef vtxindex[3];
   for (unsigned i = 0; i < num_vertices; ++i)
      vtxindex[i] = si_unpack_param(ctx, ctx->args.gs_vtx_offset[i / 2], (i & 1) * 16, 16);

   LLVMValueRef gs_vtxptr[3];
   for (unsigned i = 0; i < num_vertices; i++)
      gs_vtxptr[i] = ngg_nogs_vertex_ptr(ctx, vtxindex[i]);

   es_vtxptr = ngg_nogs_vertex_ptr(ctx, gfx10_get_thread_id_in_tg(ctx));

   /* Adding these optimization barriers improves the generated code as follows. Crazy right?
    *
    * - s_mov_b32 s4, 0xffff
    * - v_lshrrev_b32_e32 v10, 16, v0
    * - v_and_b32_e32 v12, s4, v0
    * - v_and_b32_e32 v11, s4, v1
    *   s_bfe_u32 s4, s3, 0x80008
    * - s_mov_b64 s[8:9], 0
    * - v_mul_u32_u24_e32 v0, 28, v10
    * - v_mul_u32_u24_e32 v9, 28, v12
    * - v_mul_u32_u24_e32 v1, 28, v11
    * + v_mov_b32_e32 v11, 28
    *   v_cmp_gt_u32_e32 vcc, s4, v2
    * + s_mov_b64 s[8:9], 0
    *   s_waitcnt lgkmcnt(0)
    *   s_barrier
    * + v_mul_u32_u24_sdwa v10, v0, v11 dst_sel:DWORD dst_unused:UNUSED_PAD src0_sel:WORD_0 src1_sel:DWORD
    * + v_mul_u32_u24_sdwa v23, v0, v11 dst_sel:DWORD dst_unused:UNUSED_PAD src0_sel:WORD_1 src1_sel:DWORD
    * + v_mul_u32_u24_sdwa v0, v1, v11 dst_sel:DWORD dst_unused:UNUSED_PAD src0_sel:WORD_0 src1_sel:DWORD
    *   s_and_saveexec_b64 s[44:45], vcc
    *   s_cbranch_execz BB2_8
    * - v_mul_u32_u24_e32 v16, 28, v12
    * - v_mul_u32_u24_e32 v17, 28, v11
    * - v_mul_u32_u24_e32 v18, 28, v10
    */
   for (unsigned i = 0; i < num_vertices; i++)
      ac_build_optimization_barrier(&ctx->ac, &gs_vtxptr[i], false);

   LLVMValueRef gs_accepted = ac_build_alloca(&ctx->ac, ctx->ac.i32, "");

   /* Do culling in GS threads. */
   ac_build_ifcc(&ctx->ac, si_is_gs_thread(ctx), 16002);
   {
      /* Load positions. */
      LLVMValueRef pos[3][4] = {};
      LLVMValueRef clipdist_neg_mask = NULL;

      for (unsigned vtx = 0; vtx < num_vertices; vtx++) {
         for (unsigned chan = 0; chan < 4; chan++) {
            unsigned index;
            if (chan == 0 || chan == 1)
               index = lds_pos_cull_x_div_w + chan;
            else if (chan == 3)
               index = lds_pos_cull_w;
            else
               continue;

            LLVMValueRef addr =
               ac_build_gep0(&ctx->ac, gs_vtxptr[vtx], LLVMConstInt(ctx->ac.i32, index, 0));
            pos[vtx][chan] = LLVMBuildLoad(builder, addr, "");
            pos[vtx][chan] = ac_to_float(&ctx->ac, pos[vtx][chan]);
         }

         if (has_clipdist_mask) {
            /* Load and AND clip distance masks. Each bit means whether that clip distance is
             * negative. If all masks are AND'ed and the result is 0, the primitive isn't culled
             * by clip distances.
             */
            LLVMValueRef addr = si_build_gep_i8(ctx, gs_vtxptr[vtx], lds_byte3_clipdist_neg_mask);
            LLVMValueRef mask = LLVMBuildLoad(builder, addr, "");
            if (!clipdist_neg_mask)
               clipdist_neg_mask = mask;
            else
               clipdist_neg_mask = LLVMBuildAnd(builder, clipdist_neg_mask, mask, "");
         }
      }

      LLVMValueRef clipdist_accepted =
         has_clipdist_mask ? LLVMBuildICmp(builder, LLVMIntEQ, clipdist_neg_mask, ctx->ac.i8_0, "")
                           : ctx->ac.i1true;

      cull_primitive(ctx, pos, clipdist_accepted, gs_accepted, gs_vtxptr);
   }
   ac_build_endif(&ctx->ac, 16002);
   ac_build_s_barrier(&ctx->ac);

   gs_accepted = LLVMBuildLoad(builder, gs_accepted, "");

   LLVMValueRef vertex_accepted = ac_build_alloca(&ctx->ac, ctx->ac.i1, "");
   LLVMValueRef vertex_mask = ac_build_alloca(&ctx->ac, ctx->ac.iN_wavemask, "");

   /* Convert the per-vertex accept flag to a vertex thread mask, store it in registers. */
   ac_build_ifcc(&ctx->ac, si_is_es_thread(ctx), 16007);
   {
      LLVMValueRef accepted =
         LLVMBuildLoad(builder, si_build_gep_i8(ctx, es_vtxptr, lds_byte0_accept_flag), "");
      accepted = LLVMBuildICmp(builder, LLVMIntNE, accepted, ctx->ac.i8_0, "");
      LLVMValueRef mask = ac_get_i1_sgpr_mask(&ctx->ac, accepted);

      LLVMBuildStore(builder, accepted, vertex_accepted);
      LLVMBuildStore(builder, mask, vertex_mask);
   }
   ac_build_endif(&ctx->ac, 16007);

   /* Store the per-wave vertex count to LDS. Non-ES waves store 0. */
   vertex_mask = LLVMBuildLoad(builder, vertex_mask, "");
   ac_build_ifcc(&ctx->ac, LLVMBuildICmp(builder, LLVMIntEQ, tid, ctx->ac.i32_0, ""), 16008);
   {
      LLVMValueRef vertex_count = ac_build_bit_count(&ctx->ac, vertex_mask);
      LLVMBuildStore(builder, LLVMBuildTrunc(builder, vertex_count, ctx->ac.i8, ""),
                     si_build_gep_i8_var(ctx, ctx->gs_ngg_scratch, get_wave_id_in_tg(ctx)));
   }
   ac_build_endif(&ctx->ac, 16008);

   ac_build_s_barrier(&ctx->ac);

   /* Load the vertex masks and compute the new ES thread count. */
   LLVMValueRef new_num_es_threads, prefix_sum, kill_wave;
   load_vertex_counts(ctx, ctx->gs_ngg_scratch, max_waves, tid, &new_num_es_threads,
                      &prefix_sum);

   bool uses_instance_id = ctx->stage == MESA_SHADER_VERTEX &&
                           (sel->info.uses_instanceid ||
                            shader->key.ge.part.vs.prolog.instance_divisor_is_one ||
                            shader->key.ge.part.vs.prolog.instance_divisor_is_fetched);
   bool uses_tes_prim_id = ctx->stage == MESA_SHADER_TESS_EVAL &&
                           (sel->info.uses_primid || shader->key.ge.mono.u.vs_export_prim_id);

   /* ES threads compute their prefix sum, which is the new ES thread ID.
    * Then they write the vertex position and input VGPRs into the LDS address
    * of the new thread ID. It will be used to load input VGPRs by compacted
    * threads.
    */
   vertex_accepted = LLVMBuildLoad(builder, vertex_accepted, "");
   ac_build_ifcc(&ctx->ac, vertex_accepted, 16009);
   {
      /* Add the number of bits set in vertex_mask up to the current thread ID - 1
       * to get the prefix sum.
       */
      prefix_sum = LLVMBuildAdd(builder, prefix_sum, ac_build_mbcnt(&ctx->ac, vertex_mask), "");

      LLVMValueRef new_id = prefix_sum;
      LLVMValueRef new_vtx = ngg_nogs_vertex_ptr(ctx, new_id);

      LLVMBuildStore(builder, LLVMBuildTrunc(builder, new_id, ctx->ac.i8, ""),
                     si_build_gep_i8(ctx, es_vtxptr, lds_byte1_new_thread_id));

      /* Store Position.XYZW into LDS. */
      for (unsigned chan = 0; chan < 4; chan++) {
         LLVMBuildStore(
            builder, ac_to_integer(&ctx->ac, LLVMBuildLoad(builder, addrs[4 * pos_index + chan], "")),
            ac_build_gep0(&ctx->ac, new_vtx, LLVMConstInt(ctx->ac.i32, lds_pos_x + chan, 0)));
      }

      /* Store VertexID and InstanceID into LDS. ES threads will have to load them
       * from LDS after vertex compaction and use them instead of their own
       * system values.
       */
      if (ctx->stage == MESA_SHADER_VERTEX) {
         LLVMBuildStore(
            builder, ctx->abi.vertex_id,
            ac_build_gep0(&ctx->ac, new_vtx, LLVMConstInt(ctx->ac.i32, lds_vertex_id, 0)));
         if (uses_instance_id) {
            LLVMBuildStore(
               builder, ctx->abi.instance_id,
               ac_build_gep0(&ctx->ac, new_vtx, LLVMConstInt(ctx->ac.i32, lds_instance_id, 0)));
         }
      } else {
         assert(ctx->stage == MESA_SHADER_TESS_EVAL);
         LLVMBuildStore(builder, ac_to_integer(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args.tes_u)),
                        ac_build_gep0(&ctx->ac, new_vtx, LLVMConstInt(ctx->ac.i32, lds_tes_u, 0)));
         LLVMBuildStore(builder, ac_to_integer(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args.tes_v)),
                        ac_build_gep0(&ctx->ac, new_vtx, LLVMConstInt(ctx->ac.i32, lds_tes_v, 0)));
         LLVMBuildStore(builder, LLVMBuildTrunc(builder, ac_get_arg(&ctx->ac, ctx->args.tes_rel_patch_id), ctx->ac.i8, ""),
                        si_build_gep_i8(ctx, new_vtx, lds_byte2_tes_rel_patch_id));
         if (uses_tes_prim_id) {
            LLVMBuildStore(
               builder, ac_get_arg(&ctx->ac, ctx->args.tes_patch_id),
               ac_build_gep0(&ctx->ac, new_vtx, LLVMConstInt(ctx->ac.i32, lds_tes_patch_id, 0)));
         }
      }
   }
   ac_build_endif(&ctx->ac, 16009);

   /* If all vertices are culled, set the primitive count to 0, so that all waves are culled here. */
   LLVMValueRef num_primitives = ngg_get_prim_cnt(ctx);
   num_primitives = LLVMBuildSelect(builder,
                                    LLVMBuildICmp(builder, LLVMIntEQ, new_num_es_threads,
                                                  ctx->ac.i32_0, ""),
                                    ctx->ac.i32_0, num_primitives, "");
   /* Kill waves that have inactive threads. */
   kill_wave = LLVMBuildICmp(builder, LLVMIntULE,
                             ac_build_imax(&ctx->ac, new_num_es_threads, num_primitives),
                             LLVMBuildMul(builder, get_wave_id_in_tg(ctx),
                                          LLVMConstInt(ctx->ac.i32, ctx->ac.wave_size, 0), ""),
                             "");
   ac_build_ifcc(&ctx->ac, kill_wave, 19202);
   {
      /* If we are killing wave 0, send that there are no primitives
       * in this threadgroup.
       */
      ac_build_sendmsg_gs_alloc_req(&ctx->ac, get_wave_id_in_tg(ctx), ctx->ac.i32_0, ctx->ac.i32_0);
      ac_build_s_endpgm(&ctx->ac);
   }
   ac_build_endif(&ctx->ac, 19202);
   ac_build_s_barrier(&ctx->ac);

   /* Send the final vertex and primitive counts. */
   ac_build_sendmsg_gs_alloc_req(&ctx->ac, get_wave_id_in_tg(ctx), new_num_es_threads,
                                 ngg_get_prim_cnt(ctx));

   /* Update thread counts in SGPRs. */
   LLVMValueRef new_gs_tg_info = ac_get_arg(&ctx->ac, ctx->args.gs_tg_info);
   LLVMValueRef new_merged_wave_info = ac_get_arg(&ctx->ac, ctx->args.merged_wave_info);

   /* This also converts the thread count from the total count to the per-wave count. */
   update_thread_counts(ctx, &new_num_es_threads, &new_gs_tg_info, 9, 12, &new_merged_wave_info, 8,
                        0);

   /* Update vertex indices in VGPR0 (same format as NGG passthrough).
    *
    * Set the null flag at the beginning (culled), and then
    * overwrite it for accepted primitives.
    */
   LLVMValueRef new_vgpr0 =
      ac_build_alloca_init(&ctx->ac, LLVMConstInt(ctx->ac.i32, 1u << 31, 0), "");

   /* Get vertex indices after vertex compaction. */
   ac_build_ifcc(&ctx->ac, LLVMBuildTrunc(builder, gs_accepted, ctx->ac.i1, ""), 16011);
   {
      struct ac_ngg_prim prim = {};
      prim.num_vertices = num_vertices;
      prim.isnull = ctx->ac.i1false;

      if (gfx10_edgeflags_have_effect(shader))
         prim.edgeflags = ac_pack_edgeflags_for_export(&ctx->ac, &ctx->args);
      else
         prim.edgeflags = ctx->ac.i32_0;

      for (unsigned vtx = 0; vtx < num_vertices; vtx++) {
         prim.index[vtx] = LLVMBuildLoad(
            builder, si_build_gep_i8(ctx, gs_vtxptr[vtx], lds_byte1_new_thread_id), "");
         prim.index[vtx] = LLVMBuildZExt(builder, prim.index[vtx], ctx->ac.i32, "");
      }

      /* Set the new GS input VGPR. */
      LLVMBuildStore(builder, ac_pack_prim_export(&ctx->ac, &prim), new_vgpr0);
   }
   ac_build_endif(&ctx->ac, 16011);

   if (gfx10_ngg_export_prim_early(shader))
      gfx10_ngg_build_export_prim(ctx, NULL, LLVMBuildLoad(builder, new_vgpr0, ""));

   /* Prepare LDS addresses of the new ES input VGPRs. */
   LLVMValueRef input_vgpr_addresses[4] = {
      ac_build_gep0(&ctx->ac, es_vtxptr, LLVMConstInt(ctx->ac.i32, lds_vertex_id, 0)),
      ac_build_gep0(&ctx->ac, es_vtxptr, LLVMConstInt(ctx->ac.i32, lds_instance_id, 0)),
   };
   if (ctx->stage == MESA_SHADER_TESS_EVAL) {
      input_vgpr_addresses[2] = si_build_gep_i8(ctx, es_vtxptr, lds_byte2_tes_rel_patch_id);
      if (uses_tes_prim_id) {
         input_vgpr_addresses[3] = ac_build_gep0(&ctx->ac, es_vtxptr,
                                                 LLVMConstInt(ctx->ac.i32, lds_tes_patch_id, 0));
      }
   }

   /* Return values for the main function. */
   LLVMValueRef ret = ctx->return_value;
   LLVMValueRef val;

   ret = LLVMBuildInsertValue(ctx->ac.builder, ret, new_gs_tg_info, 2, "");
   ret = LLVMBuildInsertValue(ctx->ac.builder, ret, new_merged_wave_info, 3, "");
   if (ctx->stage == MESA_SHADER_TESS_EVAL)
      ret = si_insert_input_ret(ctx, ret, ctx->args.tess_offchip_offset, 4);

   ret = si_insert_input_ptr(ctx, ret, ctx->internal_bindings, 8 + SI_SGPR_INTERNAL_BINDINGS);
   ret = si_insert_input_ptr(ctx, ret, ctx->bindless_samplers_and_images,
                             8 + SI_SGPR_BINDLESS_SAMPLERS_AND_IMAGES);
   ret = si_insert_input_ptr(ctx, ret, ctx->const_and_shader_buffers,
                             8 + SI_SGPR_CONST_AND_SHADER_BUFFERS);
   ret = si_insert_input_ptr(ctx, ret, ctx->samplers_and_images, 8 + SI_SGPR_SAMPLERS_AND_IMAGES);
   ret = si_insert_input_ptr(ctx, ret, ctx->vs_state_bits, 8 + SI_SGPR_VS_STATE_BITS);

   if (ctx->stage == MESA_SHADER_VERTEX) {
      ret = si_insert_input_ptr(ctx, ret, ctx->args.base_vertex, 8 + SI_SGPR_BASE_VERTEX);
      ret = si_insert_input_ptr(ctx, ret, ctx->args.draw_id, 8 + SI_SGPR_DRAWID);
      ret = si_insert_input_ptr(ctx, ret, ctx->args.start_instance, 8 + SI_SGPR_START_INSTANCE);
      ret = si_insert_input_ptr(ctx, ret, ctx->args.vertex_buffers, 8 + GFX9_GS_NUM_USER_SGPR);

      for (unsigned i = 0; i < shader->selector->num_vbos_in_user_sgprs; i++) {
         ret = si_insert_input_v4i32(ctx, ret, ctx->vb_descriptors[i],
                                     8 + SI_SGPR_VS_VB_DESCRIPTOR_FIRST + i * 4);
      }
   } else {
      assert(ctx->stage == MESA_SHADER_TESS_EVAL);
      ret = si_insert_input_ptr(ctx, ret, ctx->tcs_offchip_layout, 8 + SI_SGPR_TES_OFFCHIP_LAYOUT);
      ret = si_insert_input_ptr(ctx, ret, ctx->tes_offchip_addr, 8 + SI_SGPR_TES_OFFCHIP_ADDR);
   }

   unsigned vgpr;
   if (ctx->stage == MESA_SHADER_VERTEX) {
      if (shader->selector->num_vbos_in_user_sgprs) {
         vgpr = 8 + SI_SGPR_VS_VB_DESCRIPTOR_FIRST + shader->selector->num_vbos_in_user_sgprs * 4;
      } else {
         vgpr = 8 + GFX9_GS_NUM_USER_SGPR + 1;
      }
   } else {
      vgpr = 8 + GFX9_GS_NUM_USER_SGPR;
   }

   val = LLVMBuildLoad(builder, new_vgpr0, "");
   ret = LLVMBuildInsertValue(builder, ret, ac_to_float(&ctx->ac, val), vgpr++, "");
   vgpr++; /* gs_vtx_offset[1] = offsets of vertices 2-3  */

   ret = si_insert_input_ret_float(ctx, ret, ctx->args.gs_prim_id, vgpr++);
   ret = si_insert_input_ret_float(ctx, ret, ctx->args.gs_invocation_id, vgpr++);
   vgpr++; /* gs_vtx_offset[2] = offsets of vertices 4-5 */

   /* Set the input VPGRs to the corresponding LDS addresses where the VGPR values are
    * stored. The VS prolog will load them.
    */
   if (ctx->stage == MESA_SHADER_VERTEX) {
      val = LLVMBuildPtrToInt(builder, input_vgpr_addresses[0], ctx->ac.i32, "");
      ret = LLVMBuildInsertValue(builder, ret, ac_to_float(&ctx->ac, val), vgpr++,
                                 ""); /* VGPR5 - VertexID */
      vgpr += 2;
      if (uses_instance_id) {
         val = LLVMBuildPtrToInt(builder, input_vgpr_addresses[1], ctx->ac.i32, "");
         ret = LLVMBuildInsertValue(builder, ret, ac_to_float(&ctx->ac, val), vgpr++,
                                    ""); /* VGPR8 - InstanceID */
      } else {
         vgpr++;
      }
   } else {
      assert(ctx->stage == MESA_SHADER_TESS_EVAL);
      unsigned num_vgprs = uses_tes_prim_id ? 4 : 3;
      for (unsigned i = 0; i < num_vgprs; i++) {
         val = LLVMBuildPtrToInt(builder, input_vgpr_addresses[i], ctx->ac.i32, "");
         ret = LLVMBuildInsertValue(builder, ret, ac_to_float(&ctx->ac, val), vgpr++, "");
      }
      if (num_vgprs == 3)
         vgpr++;
   }

   /* These two also use LDS. */
   if (gfx10_ngg_writes_user_edgeflags(shader) ||
       (ctx->stage == MESA_SHADER_VERTEX && shader->key.ge.mono.u.vs_export_prim_id))
      ac_build_s_barrier(&ctx->ac);

   ctx->return_value = ret;
}

/**
 * Emit the epilogue of an API VS or TES shader compiled as ESGS shader.
 */
void gfx10_emit_ngg_epilogue(struct ac_shader_abi *abi)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   struct si_shader_selector *sel = ctx->shader->selector;
   struct si_shader_info *info = &sel->info;
   struct si_shader_output_values outputs[PIPE_MAX_SHADER_OUTPUTS];
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef *addrs = abi->outputs;
   LLVMValueRef tmp, tmp2;

   assert(!ctx->shader->is_gs_copy_shader);
   assert(info->num_outputs <= AC_LLVM_MAX_OUTPUTS);

   LLVMValueRef vertex_ptr = NULL;

   if (sel->so.num_outputs || gfx10_ngg_writes_user_edgeflags(ctx->shader))
      vertex_ptr = ngg_nogs_vertex_ptr(ctx, gfx10_get_thread_id_in_tg(ctx));

   for (unsigned i = 0; i < info->num_outputs; i++) {
      outputs[i].semantic = info->output_semantic[i];

      for (unsigned j = 0; j < 4; j++) {
         outputs[i].vertex_streams = info->output_streams[i];

         /* TODO: we may store more outputs than streamout needs,
          * but streamout performance isn't that important.
          */
         if (sel->so.num_outputs) {
            tmp = ac_build_gep0(&ctx->ac, vertex_ptr, LLVMConstInt(ctx->ac.i32, 4 * i + j, false));
            tmp2 = LLVMBuildLoad(builder, addrs[4 * i + j], "");
            tmp2 = ac_to_integer(&ctx->ac, tmp2);
            LLVMBuildStore(builder, tmp2, tmp);
         }
      }

      /* Store the edgeflag at the end (if streamout is enabled) */
      if (info->output_semantic[i] == VARYING_SLOT_EDGE && gfx10_ngg_writes_user_edgeflags(ctx->shader)) {
         LLVMValueRef edgeflag = LLVMBuildLoad(builder, addrs[4 * i], "");
         /* The output is a float, but the hw expects a 1-bit integer. */
         edgeflag = LLVMBuildFPToUI(ctx->ac.builder, edgeflag, ctx->ac.i32, "");
         edgeflag = ac_build_umin(&ctx->ac, edgeflag, ctx->ac.i32_1);

         tmp = LLVMConstInt(ctx->ac.i32, ngg_nogs_vertex_size(ctx->shader) - 1, 0);
         tmp = ac_build_gep0(&ctx->ac, vertex_ptr, tmp);
         LLVMBuildStore(builder, edgeflag, tmp);
      }
   }

   bool unterminated_es_if_block =
      !sel->so.num_outputs && !gfx10_ngg_writes_user_edgeflags(ctx->shader) &&
      !ctx->screen->use_ngg_streamout && /* no query buffer */
      (ctx->stage != MESA_SHADER_VERTEX || !ctx->shader->key.ge.mono.u.vs_export_prim_id);

   if (!unterminated_es_if_block)
      ac_build_endif(&ctx->ac, ctx->merged_wrap_if_label);

   LLVMValueRef is_gs_thread = si_is_gs_thread(ctx);
   LLVMValueRef is_es_thread = si_is_es_thread(ctx);
   LLVMValueRef vtxindex[3];

   if (ctx->shader->key.ge.opt.ngg_culling || gfx10_is_ngg_passthrough(ctx->shader)) {
      for (unsigned i = 0; i < 3; ++i)
         vtxindex[i] = si_unpack_param(ctx, ctx->args.gs_vtx_offset[0], 10 * i, 9);
   } else {
      for (unsigned i = 0; i < 3; ++i)
         vtxindex[i] = si_unpack_param(ctx, ctx->args.gs_vtx_offset[i / 2], (i & 1) * 16, 16);
   }

   /* Determine the number of vertices per primitive. */
   unsigned num_vertices;
   LLVMValueRef num_vertices_val = ngg_get_vertices_per_prim(ctx, &num_vertices);

   /* Streamout */
   LLVMValueRef emitted_prims = NULL;

   if (sel->so.num_outputs) {
      assert(!unterminated_es_if_block);

      struct ngg_streamout nggso = {};
      nggso.num_vertices = num_vertices_val;
      nggso.prim_enable[0] = is_gs_thread;

      for (unsigned i = 0; i < num_vertices; ++i)
         nggso.vertices[i] = ngg_nogs_vertex_ptr(ctx, vtxindex[i]);

      build_streamout(ctx, &nggso);
      emitted_prims = nggso.emit[0];
   }

   LLVMValueRef user_edgeflags[3] = {};

   if (gfx10_ngg_writes_user_edgeflags(ctx->shader)) {
      assert(!unterminated_es_if_block);

      /* Streamout already inserted the barrier, so don't insert it again. */
      if (!sel->so.num_outputs)
         ac_build_s_barrier(&ctx->ac);

      ac_build_ifcc(&ctx->ac, is_gs_thread, 5400);
      /* Load edge flags from ES threads and store them into VGPRs in GS threads. */
      for (unsigned i = 0; i < num_vertices; i++) {
         tmp = ngg_nogs_vertex_ptr(ctx, vtxindex[i]);
         tmp2 = LLVMConstInt(ctx->ac.i32, ngg_nogs_vertex_size(ctx->shader) - 1, 0);
         tmp = ac_build_gep0(&ctx->ac, tmp, tmp2);
         tmp = LLVMBuildLoad(builder, tmp, "");
         tmp = LLVMBuildTrunc(builder, tmp, ctx->ac.i1, "");

         user_edgeflags[i] = ac_build_alloca_init(&ctx->ac, tmp, "");
      }
      ac_build_endif(&ctx->ac, 5400);
   }

   /* Copy Primitive IDs from GS threads to the LDS address corresponding
    * to the ES thread of the provoking vertex.
    */
   if (ctx->stage == MESA_SHADER_VERTEX && ctx->shader->key.ge.mono.u.vs_export_prim_id) {
      assert(!unterminated_es_if_block);

      /* Streamout and edge flags use LDS. Make it idle, so that we can reuse it. */
      if (sel->so.num_outputs || gfx10_ngg_writes_user_edgeflags(ctx->shader))
         ac_build_s_barrier(&ctx->ac);

      ac_build_ifcc(&ctx->ac, is_gs_thread, 5400);
      /* Extract the PROVOKING_VTX_INDEX field. */
      LLVMValueRef provoking_vtx_in_prim = si_unpack_param(ctx, ctx->vs_state_bits, 4, 2);

      /* provoking_vtx_index = vtxindex[provoking_vtx_in_prim]; */
      LLVMValueRef indices = ac_build_gather_values(&ctx->ac, vtxindex, 3);
      LLVMValueRef provoking_vtx_index =
         LLVMBuildExtractElement(builder, indices, provoking_vtx_in_prim, "");
      LLVMValueRef vertex_ptr = ngg_nogs_vertex_ptr(ctx, provoking_vtx_index);

      LLVMBuildStore(builder, ac_get_arg(&ctx->ac, ctx->args.gs_prim_id),
                     ac_build_gep0(&ctx->ac, vertex_ptr, ctx->ac.i32_0));
      ac_build_endif(&ctx->ac, 5400);
   }

   /* Update query buffer */
   if (ctx->screen->use_ngg_streamout && !info->base.vs.blit_sgprs_amd) {
      assert(!unterminated_es_if_block);

      tmp = si_unpack_param(ctx, ctx->vs_state_bits, 6, 1);
      tmp = LLVMBuildTrunc(builder, tmp, ctx->ac.i1, "");
      ac_build_ifcc(&ctx->ac, tmp, 5029); /* if (STREAMOUT_QUERY_ENABLED) */
      tmp = LLVMBuildICmp(builder, LLVMIntEQ, get_wave_id_in_tg(ctx), ctx->ac.i32_0, "");
      ac_build_ifcc(&ctx->ac, tmp, 5030);
      tmp = LLVMBuildICmp(builder, LLVMIntULE, ac_get_thread_id(&ctx->ac),
                          sel->so.num_outputs ? ctx->ac.i32_1 : ctx->ac.i32_0, "");
      ac_build_ifcc(&ctx->ac, tmp, 5031);
      {
         LLVMValueRef args[] = {
            ngg_get_prim_cnt(ctx),
            ngg_get_query_buf(ctx),
            LLVMConstInt(ctx->ac.i32, 16, false), /* offset of stream[0].generated_primitives */
            ctx->ac.i32_0,                        /* soffset */
            ctx->ac.i32_0,                        /* cachepolicy */
         };

         if (sel->so.num_outputs) {
            args[0] = ac_build_writelane(&ctx->ac, args[0], emitted_prims, ctx->ac.i32_1);
            args[2] = ac_build_writelane(&ctx->ac, args[2], LLVMConstInt(ctx->ac.i32, 24, false),
                                         ctx->ac.i32_1);
         }

         /* TODO: should this be 64-bit atomics? */
         ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.raw.buffer.atomic.add.i32", ctx->ac.i32, args, 5,
                            0);
      }
      ac_build_endif(&ctx->ac, 5031);
      ac_build_endif(&ctx->ac, 5030);
      ac_build_endif(&ctx->ac, 5029);
   }

   /* Build the primitive export. */
   if (!gfx10_ngg_export_prim_early(ctx->shader)) {
      assert(!unterminated_es_if_block);
      gfx10_ngg_build_export_prim(ctx, user_edgeflags, NULL);
   }

   /* Export per-vertex data (positions and parameters). */
   if (!unterminated_es_if_block)
      ac_build_ifcc(&ctx->ac, is_es_thread, 6002);
   {
      unsigned i;

      /* Unconditionally (re-)load the values for proper SSA form. */
      for (i = 0; i < info->num_outputs; i++) {
         /* If the NGG cull shader part computed the position, don't
          * use the position from the current shader part. Instead,
          * load it from LDS.
          */
         if (info->output_semantic[i] == VARYING_SLOT_POS &&
             ctx->shader->key.ge.opt.ngg_culling) {
            vertex_ptr = ngg_nogs_vertex_ptr(ctx, gfx10_get_thread_id_in_tg(ctx));

            for (unsigned j = 0; j < 4; j++) {
               tmp = LLVMConstInt(ctx->ac.i32, lds_pos_x + j, 0);
               tmp = ac_build_gep0(&ctx->ac, vertex_ptr, tmp);
               tmp = LLVMBuildLoad(builder, tmp, "");
               outputs[i].values[j] = ac_to_float(&ctx->ac, tmp);
            }
         } else {
            for (unsigned j = 0; j < 4; j++) {
               outputs[i].values[j] = LLVMBuildLoad(builder, addrs[4 * i + j], "");
            }
         }
      }

      if (ctx->shader->key.ge.mono.u.vs_export_prim_id) {
         outputs[i].semantic = VARYING_SLOT_PRIMITIVE_ID;
         outputs[i].vertex_streams = 0;

         if (ctx->stage == MESA_SHADER_VERTEX) {
            /* Wait for GS stores to finish. */
            ac_build_s_barrier(&ctx->ac);

            tmp = ngg_nogs_vertex_ptr(ctx, gfx10_get_thread_id_in_tg(ctx));
            tmp = ac_build_gep0(&ctx->ac, tmp, ctx->ac.i32_0);
            outputs[i].values[0] = LLVMBuildLoad(builder, tmp, "");
         } else {
            assert(ctx->stage == MESA_SHADER_TESS_EVAL);
            outputs[i].values[0] = si_get_primitive_id(ctx, 0);
         }

         outputs[i].values[0] = ac_to_float(&ctx->ac, outputs[i].values[0]);
         for (unsigned j = 1; j < 4; j++)
            outputs[i].values[j] = LLVMGetUndef(ctx->ac.f32);
         i++;
      }

      si_llvm_build_vs_exports(ctx, outputs, i);
   }
   ac_build_endif(&ctx->ac, 6002);
}

static LLVMValueRef ngg_gs_get_vertex_storage(struct si_shader_context *ctx)
{
   const struct si_shader_selector *sel = ctx->shader->selector;
   const struct si_shader_info *info = &sel->info;

   LLVMTypeRef elements[2] = {
      LLVMArrayType(ctx->ac.i32, 4 * info->num_outputs),
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
 *   N = threadidx * gs.vertices_out + emitidx
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
 * Vertices are stored in export order (gsthread * gs.vertices_out + emitidx).
 * Indices are swizzled in groups of 32, which ensures point 1 without
 * disturbing point 2.
 *
 * \return an LDS pointer to type {[N x i32], [4 x i8]}
 */
static LLVMValueRef ngg_gs_vertex_ptr(struct si_shader_context *ctx, LLVMValueRef vertexidx)
{
   struct si_shader_selector *sel = ctx->shader->selector;
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef storage = ngg_gs_get_vertex_storage(ctx);

   /* gs.vertices_out = 2^(write_stride_2exp) * some odd number */
   unsigned write_stride_2exp = ffs(sel->info.base.gs.vertices_out) - 1;
   if (write_stride_2exp) {
      LLVMValueRef row = LLVMBuildLShr(builder, vertexidx, LLVMConstInt(ctx->ac.i32, 5, false), "");
      LLVMValueRef swizzle = LLVMBuildAnd(
         builder, row, LLVMConstInt(ctx->ac.i32, (1u << write_stride_2exp) - 1, false), "");
      vertexidx = LLVMBuildXor(builder, vertexidx, swizzle, "");
   }

   return ac_build_gep0(&ctx->ac, storage, vertexidx);
}

static LLVMValueRef ngg_gs_emit_vertex_ptr(struct si_shader_context *ctx, LLVMValueRef gsthread,
                                           LLVMValueRef emitidx)
{
   struct si_shader_selector *sel = ctx->shader->selector;
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef tmp;

   tmp = LLVMConstInt(ctx->ac.i32, sel->info.base.gs.vertices_out, false);
   tmp = LLVMBuildMul(builder, tmp, gsthread, "");
   const LLVMValueRef vertexidx = LLVMBuildAdd(builder, tmp, emitidx, "");
   return ngg_gs_vertex_ptr(ctx, vertexidx);
}

static LLVMValueRef ngg_gs_get_emit_output_ptr(struct si_shader_context *ctx,
                                               LLVMValueRef vertexptr, unsigned out_idx)
{
   LLVMValueRef gep_idx[3] = {
      ctx->ac.i32_0, /* implied C-style array */
      ctx->ac.i32_0, /* first struct entry */
      LLVMConstInt(ctx->ac.i32, out_idx, false),
   };
   return LLVMBuildGEP(ctx->ac.builder, vertexptr, gep_idx, 3, "");
}

static LLVMValueRef ngg_gs_get_emit_primflag_ptr(struct si_shader_context *ctx,
                                                 LLVMValueRef vertexptr, unsigned stream)
{
   LLVMValueRef gep_idx[3] = {
      ctx->ac.i32_0, /* implied C-style array */
      ctx->ac.i32_1, /* second struct entry */
      LLVMConstInt(ctx->ac.i32, stream, false),
   };
   return LLVMBuildGEP(ctx->ac.builder, vertexptr, gep_idx, 3, "");
}

void gfx10_ngg_gs_emit_vertex(struct si_shader_context *ctx, unsigned stream, LLVMValueRef *addrs)
{
   const struct si_shader_selector *sel = ctx->shader->selector;
   const struct si_shader_info *info = &sel->info;
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef tmp;
   const LLVMValueRef vertexidx = LLVMBuildLoad(builder, ctx->gs_next_vertex[stream], "");

   /* If this thread has already emitted the declared maximum number of
    * vertices, skip the write: excessive vertex emissions are not
    * supposed to have any effect.
    */
   const LLVMValueRef can_emit =
      LLVMBuildICmp(builder, LLVMIntULT, vertexidx,
                    LLVMConstInt(ctx->ac.i32, sel->info.base.gs.vertices_out, false), "");

   tmp = LLVMBuildAdd(builder, vertexidx, ctx->ac.i32_1, "");
   tmp = LLVMBuildSelect(builder, can_emit, tmp, vertexidx, "");
   LLVMBuildStore(builder, tmp, ctx->gs_next_vertex[stream]);

   ac_build_ifcc(&ctx->ac, can_emit, 9001);

   const LLVMValueRef vertexptr = ngg_gs_emit_vertex_ptr(ctx, gfx10_get_thread_id_in_tg(ctx), vertexidx);
   unsigned out_idx = 0;
   for (unsigned i = 0; i < info->num_outputs; i++) {
      for (unsigned chan = 0; chan < 4; chan++, out_idx++) {
         if (!(info->output_usagemask[i] & (1 << chan)) ||
             ((info->output_streams[i] >> (2 * chan)) & 3) != stream)
            continue;

         LLVMValueRef out_val = LLVMBuildLoad(builder, addrs[4 * i + chan], "");
         out_val = ac_to_integer(&ctx->ac, out_val);
         LLVMBuildStore(builder, out_val, ngg_gs_get_emit_output_ptr(ctx, vertexptr, out_idx));
      }
   }
   assert(out_idx * 4 == sel->gsvs_vertex_size);

   /* Determine and store whether this vertex completed a primitive. */
   const LLVMValueRef curverts = LLVMBuildLoad(builder, ctx->gs_curprim_verts[stream], "");

   tmp = LLVMConstInt(ctx->ac.i32, u_vertices_per_prim(sel->info.base.gs.output_primitive) - 1, false);
   const LLVMValueRef iscompleteprim = LLVMBuildICmp(builder, LLVMIntUGE, curverts, tmp, "");

   /* Since the geometry shader emits triangle strips, we need to
    * track which primitive is odd and swap vertex indices to get
    * the correct vertex order.
    */
   LLVMValueRef is_odd = ctx->ac.i1false;
   if (stream == 0 && u_vertices_per_prim(sel->info.base.gs.output_primitive) == 3) {
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

   ac_build_endif(&ctx->ac, 9001);
}

void gfx10_ngg_gs_emit_prologue(struct si_shader_context *ctx)
{
   /* Zero out the part of LDS scratch that is used to accumulate the
    * per-stream generated primitive count.
    */
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef scratchptr = ctx->gs_ngg_scratch;
   LLVMValueRef tid = gfx10_get_thread_id_in_tg(ctx);
   LLVMValueRef tmp;

   tmp = LLVMBuildICmp(builder, LLVMIntULT, tid, LLVMConstInt(ctx->ac.i32, 4, false), "");
   ac_build_ifcc(&ctx->ac, tmp, 5090);
   {
      LLVMValueRef ptr = ac_build_gep0(&ctx->ac, scratchptr, tid);
      LLVMBuildStore(builder, ctx->ac.i32_0, ptr);
   }
   ac_build_endif(&ctx->ac, 5090);

   ac_build_s_barrier(&ctx->ac);
}

void gfx10_ngg_gs_emit_epilogue(struct si_shader_context *ctx)
{
   const struct si_shader_selector *sel = ctx->shader->selector;
   const struct si_shader_info *info = &sel->info;
   const unsigned verts_per_prim = u_vertices_per_prim(sel->info.base.gs.output_primitive);
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef i8_0 = LLVMConstInt(ctx->ac.i8, 0, false);
   LLVMValueRef tmp, tmp2;

   /* Zero out remaining (non-emitted) primitive flags.
    *
    * Note: Alternatively, we could pass the relevant gs_next_vertex to
    *       the emit threads via LDS. This is likely worse in the expected
    *       typical case where each GS thread emits the full set of
    *       vertices.
    */
   for (unsigned stream = 0; stream < 4; ++stream) {
      if (!info->num_stream_output_components[stream])
         continue;

      const LLVMValueRef gsthread = gfx10_get_thread_id_in_tg(ctx);

      ac_build_bgnloop(&ctx->ac, 5100);

      const LLVMValueRef vertexidx = LLVMBuildLoad(builder, ctx->gs_next_vertex[stream], "");
      tmp = LLVMBuildICmp(builder, LLVMIntUGE, vertexidx,
                          LLVMConstInt(ctx->ac.i32, sel->info.base.gs.vertices_out, false), "");
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
      if (!info->num_stream_output_components[stream])
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

   ac_build_endif(&ctx->ac, ctx->merged_wrap_if_label);

   ac_build_s_barrier(&ctx->ac);

   const LLVMValueRef tid = gfx10_get_thread_id_in_tg(ctx);
   LLVMValueRef num_emit_threads = ngg_get_prim_cnt(ctx);

   /* Streamout */
   if (sel->so.num_outputs) {
      struct ngg_streamout nggso = {};

      nggso.num_vertices = LLVMConstInt(ctx->ac.i32, verts_per_prim, false);

      LLVMValueRef vertexptr = ngg_gs_vertex_ptr(ctx, tid);
      for (unsigned stream = 0; stream < 4; ++stream) {
         if (!info->num_stream_output_components[stream])
            continue;

         tmp = LLVMBuildLoad(builder, ngg_gs_get_emit_primflag_ptr(ctx, vertexptr, stream), "");
         tmp = LLVMBuildTrunc(builder, tmp, ctx->ac.i1, "");
         tmp2 = LLVMBuildICmp(builder, LLVMIntULT, tid, num_emit_threads, "");
         nggso.prim_enable[stream] = LLVMBuildAnd(builder, tmp, tmp2, "");
      }

      for (unsigned i = 0; i < verts_per_prim; ++i) {
         tmp = LLVMBuildSub(builder, tid, LLVMConstInt(ctx->ac.i32, verts_per_prim - i - 1, false),
                            "");
         tmp = ngg_gs_vertex_ptr(ctx, tmp);
         nggso.vertices[i] = ac_build_gep0(&ctx->ac, tmp, ctx->ac.i32_0);
      }

      build_streamout(ctx, &nggso);
   }

   /* Write shader query data. */
   if (ctx->screen->use_ngg_streamout) {
      tmp = si_unpack_param(ctx, ctx->vs_state_bits, 6, 1);
      tmp = LLVMBuildTrunc(builder, tmp, ctx->ac.i1, "");
      ac_build_ifcc(&ctx->ac, tmp, 5109); /* if (STREAMOUT_QUERY_ENABLED) */
      unsigned num_query_comps = sel->so.num_outputs ? 8 : 4;
      tmp = LLVMBuildICmp(builder, LLVMIntULT, tid,
                          LLVMConstInt(ctx->ac.i32, num_query_comps, false), "");
      ac_build_ifcc(&ctx->ac, tmp, 5110);
      {
         LLVMValueRef offset;
         tmp = tid;
         if (sel->so.num_outputs)
            tmp = LLVMBuildAnd(builder, tmp, LLVMConstInt(ctx->ac.i32, 3, false), "");
         offset = LLVMBuildNUWMul(builder, tmp, LLVMConstInt(ctx->ac.i32, 32, false), "");
         if (sel->so.num_outputs) {
            tmp = LLVMBuildLShr(builder, tid, LLVMConstInt(ctx->ac.i32, 2, false), "");
            tmp = LLVMBuildNUWMul(builder, tmp, LLVMConstInt(ctx->ac.i32, 8, false), "");
            offset = LLVMBuildAdd(builder, offset, tmp, "");
         }

         tmp = LLVMBuildLoad(builder, ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, tid), "");
         LLVMValueRef args[] = {
            tmp,           ngg_get_query_buf(ctx),
            offset,        LLVMConstInt(ctx->ac.i32, 16, false), /* soffset */
            ctx->ac.i32_0,                                       /* cachepolicy */
         };
         ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.raw.buffer.atomic.add.i32", ctx->ac.i32, args, 5,
                            0);
      }
      ac_build_endif(&ctx->ac, 5110);
      ac_build_endif(&ctx->ac, 5109);
   }

   /* Cull primitives. */
   if (ctx->shader->key.ge.opt.ngg_culling) {
      assert(info->num_stream_output_components[0]);

      LLVMValueRef gs_vtxptr = ngg_gs_vertex_ptr(ctx, tid);
      LLVMValueRef live = LLVMBuildLoad(builder, ngg_gs_get_emit_primflag_ptr(ctx, gs_vtxptr, 0), "");
      live = LLVMBuildTrunc(builder, live, ctx->ac.i1, "");
      LLVMValueRef is_emit = LLVMBuildICmp(builder, LLVMIntULT, tid, num_emit_threads, "");
      LLVMValueRef prim_enable = LLVMBuildAnd(builder, live, is_emit, "");

      /* Wait for streamout to finish before we kill primitives. */
      if (sel->so.num_outputs)
         ac_build_s_barrier(&ctx->ac);

      ac_build_ifcc(&ctx->ac, prim_enable, 0);
      {
         LLVMValueRef vtxptr[3] = {};
         LLVMValueRef pos[3][4] = {};

         for (unsigned i = 0; i < verts_per_prim; i++) {
            tmp = LLVMBuildSub(builder, tid, LLVMConstInt(ctx->ac.i32, verts_per_prim - i - 1, false), "");
            vtxptr[i] = ac_build_gep0(&ctx->ac, ngg_gs_vertex_ptr(ctx, tmp), ctx->ac.i32_0);
         }

         for (unsigned i = 0; i < info->num_outputs; i++) {
            /* If the stream index is non-zero for all channels, skip the output. */
            if (info->output_streams[i] & 0x3 &&
                (info->output_streams[i] >> 2) & 0x3 &&
                (info->output_streams[i] >> 4) & 0x3 &&
                (info->output_streams[i] >> 6) & 0x3)
               continue;

            switch (info->output_semantic[i]) {
            case VARYING_SLOT_POS:
               /* Load the positions from LDS. */
               for (unsigned vert = 0; vert < verts_per_prim; vert++) {
                  for (unsigned comp = 0; comp < 4; comp++) {
                     /* Z is not needed. */
                     if (comp == 2)
                        continue;

                     tmp = ac_build_gep0(&ctx->ac, vtxptr[vert],
                                         LLVMConstInt(ctx->ac.i32, 4 * i + comp, false));
                     pos[vert][comp] = LLVMBuildLoad(builder, tmp, "");
                     pos[vert][comp] = ac_to_float(&ctx->ac, pos[vert][comp]);
                  }
               }

               /* Divide XY by W. */
               for (unsigned vert = 0; vert < verts_per_prim; vert++) {
                  for (unsigned comp = 0; comp < 2; comp++)
                     pos[vert][comp] = ac_build_fdiv(&ctx->ac, pos[vert][comp], pos[vert][3]);
               }
               break;
            }
         }

         LLVMValueRef clipdist_accepted = ctx->ac.i1true; /* TODO */
         LLVMValueRef accepted = ac_build_alloca(&ctx->ac, ctx->ac.i32, "");

         cull_primitive(ctx, pos, clipdist_accepted, accepted, NULL);

         accepted = LLVMBuildLoad(builder, accepted, "");
         LLVMValueRef rejected = LLVMBuildNot(builder, LLVMBuildTrunc(builder, accepted, ctx->ac.i1, ""), "");

         ac_build_ifcc(&ctx->ac, rejected, 0);
         LLVMBuildStore(builder, ctx->ac.i8_0, ngg_gs_get_emit_primflag_ptr(ctx, gs_vtxptr, 0));
         ac_build_endif(&ctx->ac, 0);
      }
      ac_build_endif(&ctx->ac, 0);
      ac_build_s_barrier(&ctx->ac);
   }

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
   struct ac_wg_scan vertlive_scan = {};
   vertlive_scan.op = nir_op_iadd;
   vertlive_scan.enable_reduce = true;
   vertlive_scan.enable_exclusive = true;
   vertlive_scan.src = vertlive;
   vertlive_scan.scratch = ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, ctx->ac.i32_0);
   vertlive_scan.waveidx = get_wave_id_in_tg(ctx);
   vertlive_scan.numwaves = get_tgsize(ctx);
   vertlive_scan.maxwaves = DIV_ROUND_UP(256, ctx->ac.wave_size);

   ac_build_wg_scan(&ctx->ac, &vertlive_scan);

   /* Skip all exports (including index exports) when possible. */
   LLVMValueRef have_exports =
      LLVMBuildICmp(builder, LLVMIntNE, vertlive_scan.result_reduce, ctx->ac.i32_0, "");
   num_emit_threads = LLVMBuildSelect(builder, have_exports, num_emit_threads, ctx->ac.i32_0, "");

   /* Allocate export space. Send this message as early as possible, to
    * hide the latency of the SQ <-> SPI roundtrip.
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
      struct ac_ngg_prim prim = {};
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
         LLVMValueRef flatshade_first = LLVMBuildICmp(
            builder, LLVMIntEQ, si_unpack_param(ctx, ctx->vs_state_bits, 4, 2), ctx->ac.i32_0, "");

         ac_build_triangle_strip_indices_to_triangle(&ctx->ac, is_odd, flatshade_first, prim.index);
      }

      ac_build_export_prim(&ctx->ac, &prim);
   }
   ac_build_endif(&ctx->ac, 5140);

   /* Export position and parameter data */
   tmp = LLVMBuildICmp(builder, LLVMIntULT, tid, vertlive_scan.result_reduce, "");
   ac_build_ifcc(&ctx->ac, tmp, 5145);
   {
      struct si_shader_output_values outputs[PIPE_MAX_SHADER_OUTPUTS];

      tmp = ngg_gs_vertex_ptr(ctx, tid);
      tmp = LLVMBuildLoad(builder, ngg_gs_get_emit_primflag_ptr(ctx, tmp, 1), "");
      tmp = LLVMBuildZExt(builder, tmp, ctx->ac.i32, "");
      const LLVMValueRef vertexptr = ngg_gs_vertex_ptr(ctx, tmp);

      unsigned out_idx = 0;
      for (unsigned i = 0; i < info->num_outputs; i++) {
         outputs[i].semantic = info->output_semantic[i];

         for (unsigned j = 0; j < 4; j++, out_idx++) {
            tmp = ngg_gs_get_emit_output_ptr(ctx, vertexptr, out_idx);
            tmp = LLVMBuildLoad(builder, tmp, "");
            outputs[i].values[j] = ac_to_float(&ctx->ac, tmp);
            outputs[i].vertex_streams = info->output_streams[i];
         }
      }

      si_llvm_build_vs_exports(ctx, outputs, info->num_outputs);
   }
   ac_build_endif(&ctx->ac, 5145);
}

static void clamp_gsprims_to_esverts(unsigned *max_gsprims, unsigned max_esverts,
                                     unsigned min_verts_per_prim, bool use_adjacency)
{
   unsigned max_reuse = max_esverts - min_verts_per_prim;
   if (use_adjacency)
      max_reuse /= 2;
   *max_gsprims = MIN2(*max_gsprims, 1 + max_reuse);
}

unsigned gfx10_ngg_get_scratch_dw_size(struct si_shader *shader)
{
   const struct si_shader_selector *sel = shader->selector;

   if (sel->info.stage == MESA_SHADER_GEOMETRY && sel->so.num_outputs)
      return 44;

   return 8;
}

/**
 * Determine subgroup information like maximum number of vertices and prims.
 *
 * This happens before the shader is uploaded, since LDS relocations during
 * upload depend on the subgroup size.
 */
bool gfx10_ngg_calculate_subgroup_info(struct si_shader *shader)
{
   const struct si_shader_selector *gs_sel = shader->selector;
   const struct si_shader_selector *es_sel =
      shader->previous_stage_sel ? shader->previous_stage_sel : gs_sel;
   const gl_shader_stage gs_stage = gs_sel->info.stage;
   const unsigned gs_num_invocations = MAX2(gs_sel->info.base.gs.invocations, 1);
   const unsigned input_prim = si_get_input_prim(gs_sel, &shader->key);
   const bool use_adjacency =
      input_prim >= PIPE_PRIM_LINES_ADJACENCY && input_prim <= PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY;
   const unsigned max_verts_per_prim = u_vertices_per_prim(input_prim);
   const unsigned min_verts_per_prim = gs_stage == MESA_SHADER_GEOMETRY ? max_verts_per_prim : 1;

   /* All these are in dwords: */
   /* GE can only use 8K dwords (32KB) of LDS per workgroup.
    */
   const unsigned max_lds_size = 8 * 1024 - gfx10_ngg_get_scratch_dw_size(shader);
   const unsigned target_lds_size = max_lds_size;
   unsigned esvert_lds_size = 0;
   unsigned gsprim_lds_size = 0;

   /* All these are per subgroup: */
   const unsigned min_esverts =
      gs_sel->screen->info.chip_class >= GFX10_3 ? 29 : (24 - 1 + max_verts_per_prim);
   bool max_vert_out_per_gs_instance = false;
   unsigned max_gsprims_base = gs_sel->screen->ngg_subgroup_size; /* default prim group size clamp */
   unsigned max_esverts_base = gs_sel->screen->ngg_subgroup_size;

   if (gs_stage == MESA_SHADER_GEOMETRY) {
      bool force_multi_cycling = false;
      unsigned max_out_verts_per_gsprim = gs_sel->info.base.gs.vertices_out * gs_num_invocations;

retry_select_mode:
      if (max_out_verts_per_gsprim <= 256 && !force_multi_cycling) {
         if (max_out_verts_per_gsprim) {
            max_gsprims_base = MIN2(max_gsprims_base, 256 / max_out_verts_per_gsprim);
         }
      } else {
         /* Use special multi-cycling mode in which each GS
          * instance gets its own subgroup. Does not work with
          * tessellation. */
         max_vert_out_per_gs_instance = true;
         max_gsprims_base = 1;
         max_out_verts_per_gsprim = gs_sel->info.base.gs.vertices_out;
      }

      esvert_lds_size = es_sel->esgs_itemsize / 4;
      gsprim_lds_size = (gs_sel->gsvs_vertex_size / 4 + 1) * max_out_verts_per_gsprim;

      if (gsprim_lds_size > target_lds_size && !force_multi_cycling) {
         if (gs_sel->tess_turns_off_ngg || es_sel->info.stage != MESA_SHADER_TESS_EVAL) {
            force_multi_cycling = true;
            goto retry_select_mode;
         }
      }
   } else {
      /* VS and TES. */
      /* LDS size for passing data from ES to GS. */
      esvert_lds_size = ngg_nogs_vertex_size(shader);
   }

   unsigned max_gsprims = max_gsprims_base;
   unsigned max_esverts = max_esverts_base;

   if (esvert_lds_size)
      max_esverts = MIN2(max_esverts, target_lds_size / esvert_lds_size);
   if (gsprim_lds_size)
      max_gsprims = MIN2(max_gsprims, target_lds_size / gsprim_lds_size);

   max_esverts = MIN2(max_esverts, max_gsprims * max_verts_per_prim);
   clamp_gsprims_to_esverts(&max_gsprims, max_esverts, min_verts_per_prim, use_adjacency);
   assert(max_esverts >= max_verts_per_prim && max_gsprims >= 1);

   if (esvert_lds_size || gsprim_lds_size) {
      /* Now that we have a rough proportionality between esverts
       * and gsprims based on the primitive type, scale both of them
       * down simultaneously based on required LDS space.
       *
       * We could be smarter about this if we knew how much vertex
       * reuse to expect.
       */
      unsigned lds_total = max_esverts * esvert_lds_size + max_gsprims * gsprim_lds_size;
      if (lds_total > target_lds_size) {
         max_esverts = max_esverts * target_lds_size / lds_total;
         max_gsprims = max_gsprims * target_lds_size / lds_total;

         max_esverts = MIN2(max_esverts, max_gsprims * max_verts_per_prim);
         clamp_gsprims_to_esverts(&max_gsprims, max_esverts, min_verts_per_prim, use_adjacency);
         assert(max_esverts >= max_verts_per_prim && max_gsprims >= 1);
      }
   }

   /* Round up towards full wave sizes for better ALU utilization. */
   if (!max_vert_out_per_gs_instance) {
      unsigned orig_max_esverts;
      unsigned orig_max_gsprims;
      do {
         orig_max_esverts = max_esverts;
         orig_max_gsprims = max_gsprims;

         max_esverts = align(max_esverts, shader->wave_size);
         max_esverts = MIN2(max_esverts, max_esverts_base);
         if (esvert_lds_size)
            max_esverts =
               MIN2(max_esverts, (max_lds_size - max_gsprims * gsprim_lds_size) / esvert_lds_size);
         max_esverts = MIN2(max_esverts, max_gsprims * max_verts_per_prim);

         /* Hardware restriction: minimum value of max_esverts */
         max_esverts = MAX2(max_esverts, min_esverts);

         max_gsprims = align(max_gsprims, shader->wave_size);
         max_gsprims = MIN2(max_gsprims, max_gsprims_base);
         if (gsprim_lds_size) {
            /* Don't count unusable vertices to the LDS size. Those are vertices above
             * the maximum number of vertices that can occur in the workgroup,
             * which is e.g. max_gsprims * 3 for triangles.
             */
            unsigned usable_esverts = MIN2(max_esverts, max_gsprims * max_verts_per_prim);
            max_gsprims =
               MIN2(max_gsprims, (max_lds_size - usable_esverts * esvert_lds_size) / gsprim_lds_size);
         }
         clamp_gsprims_to_esverts(&max_gsprims, max_esverts, min_verts_per_prim, use_adjacency);
         assert(max_esverts >= max_verts_per_prim && max_gsprims >= 1);
      } while (orig_max_esverts != max_esverts || orig_max_gsprims != max_gsprims);

      /* Verify the restriction. */
      assert(max_esverts >= min_esverts);
   } else {
      max_esverts = MAX2(max_esverts, min_esverts);
   }

   unsigned max_out_vertices =
      max_vert_out_per_gs_instance
         ? gs_sel->info.base.gs.vertices_out
         : gs_stage == MESA_SHADER_GEOMETRY
              ? max_gsprims * gs_num_invocations * gs_sel->info.base.gs.vertices_out
              : max_esverts;
   assert(max_out_vertices <= 256);

   unsigned prim_amp_factor = 1;
   if (gs_stage == MESA_SHADER_GEOMETRY) {
      /* Number of output primitives per GS input primitive after
       * GS instancing. */
      prim_amp_factor = gs_sel->info.base.gs.vertices_out;
   }

   shader->ngg.hw_max_esverts = max_esverts;
   shader->ngg.max_gsprims = max_gsprims;
   shader->ngg.max_out_verts = max_out_vertices;
   shader->ngg.prim_amp_factor = prim_amp_factor;
   shader->ngg.max_vert_out_per_gs_instance = max_vert_out_per_gs_instance;

   /* Don't count unusable vertices. */
   shader->gs_info.esgs_ring_size = MIN2(max_esverts, max_gsprims * max_verts_per_prim) *
                                    esvert_lds_size;
   shader->ngg.ngg_emit_size = max_gsprims * gsprim_lds_size;

   assert(shader->ngg.hw_max_esverts >= min_esverts); /* HW limitation */

   /* If asserts are disabled, we use the same conditions to return false */
   return max_esverts >= max_verts_per_prim && max_gsprims >= 1 &&
          max_out_vertices <= 256 &&
          shader->ngg.hw_max_esverts >= min_esverts;
}
