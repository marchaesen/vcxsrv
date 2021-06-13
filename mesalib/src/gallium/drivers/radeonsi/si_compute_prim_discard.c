/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
 *
 */

#include "ac_llvm_cull.h"
#include "si_build_pm4.h"
#include "si_pipe.h"
#include "si_shader_internal.h"
#include "sid.h"
#include "util/fast_idiv_by_const.h"
#include "util/u_prim.h"
#include "util/u_suballoc.h"
#include "util/u_upload_mgr.h"

/* Based on:
 * https://frostbite-wp-prd.s3.amazonaws.com/wp-content/uploads/2016/03/29204330/GDC_2016_Compute.pdf
 */

/* This file implements primitive culling using asynchronous compute.
 * It's written to be GL conformant.
 *
 * It takes a monolithic VS in LLVM IR returning gl_Position and invokes it
 * in a compute shader. The shader processes 1 primitive/thread by invoking
 * the VS for each vertex to get the positions, decomposes strips and fans
 * into triangles (if needed), eliminates primitive restart (if needed),
 * does (W<0) culling, face culling, view XY culling, zero-area and
 * small-primitive culling, and generates a new index buffer that doesn't
 * contain culled primitives.
 *
 * The index buffer is generated using the Ordered Count feature of GDS,
 * which is an atomic counter that is incremented in the wavefront launch
 * order, so that the original primitive order is preserved.
 *
 * Another GDS ordered counter is used to eliminate primitive restart indices.
 * If a restart index lands on an even thread ID, the compute shader has to flip
 * the primitive orientation of the whole following triangle strip. The primitive
 * orientation has to be correct after strip and fan decomposition for two-sided
 * shading to behave correctly. The decomposition also needs to be aware of
 * which vertex is the provoking vertex for flat shading to behave correctly.
 *
 * IB = a GPU command buffer
 *
 * Both the compute and gfx IBs run in parallel sort of like CE and DE.
 * The gfx IB has a CP barrier (REWIND packet) before a draw packet. REWIND
 * doesn't continue if its word isn't 0x80000000. Once compute shaders are
 * finished culling, the last wave will write the final primitive count from
 * GDS directly into the count word of the draw packet in the gfx IB, and
 * a CS_DONE event will signal the REWIND packet to continue. It's really
 * a direct draw with command buffer patching from the compute queue.
 *
 * The compute IB doesn't have to start when its corresponding gfx IB starts,
 * but can start sooner. The compute IB is signaled to start after the last
 * execution barrier in the *previous* gfx IB. This is handled as follows.
 * The kernel GPU scheduler starts the compute IB after the previous gfx IB has
 * started. The compute IB then waits (WAIT_REG_MEM) for a mid-IB fence that
 * represents the barrier in the previous gfx IB.
 *
 * Features:
 * - Triangle strips and fans are decomposed into an indexed triangle list.
 *   The decomposition differs based on the provoking vertex state.
 * - Instanced draws are converted into non-instanced draws for 16-bit indices.
 *   (InstanceID is stored in the high bits of VertexID and unpacked by VS)
 * - Primitive restart is fully supported with triangle strips, including
 *   correct primitive orientation across multiple waves. (restart indices
 *   reset primitive orientation)
 * - W<0 culling (W<0 is behind the viewer, sort of like near Z culling).
 * - Back face culling, incl. culling zero-area / degenerate primitives.
 * - View XY culling.
 * - View Z culling (disabled due to limited impact with perspective projection).
 * - Small primitive culling for all MSAA modes and all quant modes.
 *
 * The following are not implemented:
 * - ClipVertex/ClipDistance/CullDistance-based culling.
 * - Scissor culling.
 * - HiZ culling.
 *
 * Limitations (and unimplemented features that may be possible to implement):
 * - Only triangles, triangle strips, and triangle fans are supported.
 * - Primitive restart is only supported with triangle strips.
 * - Instancing and primitive restart can't be used together.
 * - Instancing is only supported with 16-bit indices and instance count <= 2^16.
 * - The instance divisor buffer is unavailable, so all divisors must be
 *   either 0 or 1.
 * - Multidraws where the vertex shader reads gl_DrawID are unsupported.
 * - No support for tessellation and geometry shaders.
 *   (patch elimination where tess factors are 0 would be possible to implement)
 * - The vertex shader must not contain memory stores.
 * - All VS resources must not have a write usage in the command buffer.
 * - Bindless textures and images must not occur in the vertex shader.
 *
 * User data SGPR layout:
 *   INDEX_BUFFERS: pointer to constants
 *     0..3: input index buffer - typed buffer view
 *     4..7: output index buffer - typed buffer view
 *     8..11: viewport state - scale.xy, translate.xy
 *   VERTEX_COUNTER: counter address or first primitive ID
 *     - If unordered memory counter: address of "count" in the draw packet
 *       and is incremented atomically by the shader.
 *     - If unordered GDS counter: address of "count" in GDS starting from 0,
 *       must be initialized to 0 before the dispatch.
 *     - If ordered GDS counter: the primitive ID that should reset the vertex
 *       counter to 0 in GDS
 *   LAST_WAVE_PRIM_ID: the primitive ID that should write the final vertex
 *       count to memory if using GDS ordered append
 *   VERTEX_COUNT_ADDR: where the last wave should write the vertex count if
 *       using GDS ordered append
 *   VS.VERTEX_BUFFERS:           same value as VS
 *   VS.CONST_AND_SHADER_BUFFERS: same value as VS
 *   VS.SAMPLERS_AND_IMAGES:      same value as VS
 *   VS.BASE_VERTEX:              same value as VS
 *   VS.START_INSTANCE:           same value as VS
 *   NUM_PRIMS_UDIV_MULTIPLIER: For fast 31-bit division by the number of primitives
 *       per instance for instancing.
 *   NUM_PRIMS_UDIV_TERMS:
 *     - Bits [0:4]: "post_shift" for fast 31-bit division for instancing.
 *     - Bits [5:31]: The number of primitives per instance for computing the remainder.
 *   PRIMITIVE_RESTART_INDEX
 *   SMALL_PRIM_CULLING_PRECISION: Scale the primitive bounding box by this number.
 *
 *
 * The code contains 3 codepaths:
 * - Unordered memory counter (for debugging, random primitive order, no primitive restart)
 * - Unordered GDS counter (for debugging, random primitive order, no primitive restart)
 * - Ordered GDS counter (it preserves the primitive order)
 *
 * How to test primitive restart (the most complicated part because it needs
 * to get the primitive orientation right):
 *   Set THREADGROUP_SIZE to 2 to exercise both intra-wave and inter-wave
 *   primitive orientation flips with small draw calls, which is what most tests use.
 *   You can also enable draw call splitting into draw calls with just 2 primitives.
 */

/* At least 256 is needed for the fastest wave launch rate from compute queues
 * due to hw constraints. Nothing in the code needs more than 1 wave/threadgroup. */
#define THREADGROUP_SIZE     256 /* high numbers limit available VGPRs */
#define THREADGROUPS_PER_CU  1   /* TGs to launch on 1 CU before going onto the next, max 8 */
#define MAX_WAVES_PER_SH     0   /* no limit */
#define INDEX_STORES_USE_SLC 1   /* don't cache indices if L2 is full */
/* Don't cull Z. We already do (W < 0) culling for primitives behind the viewer. */
#define CULL_Z 0
/* 0 = unordered memory counter, 1 = unordered GDS counter, 2 = ordered GDS counter */
#define VERTEX_COUNTER_GDS_MODE 2
#define GDS_SIZE_UNORDERED      (4 * 1024) /* only for the unordered GDS counter */

/* Grouping compute dispatches for small draw calls: How many primitives from multiple
 * draw calls to process by compute before signaling the gfx IB. This reduces the number
 * of EOP events + REWIND packets, because they decrease performance. */
#define PRIMS_PER_BATCH (512 * 1024)
/* Draw call splitting at the packet level. This allows signaling the gfx IB
 * for big draw calls sooner, but doesn't allow context flushes between packets.
 * Primitive restart is supported. Only implemented for ordered append. */
#define SPLIT_PRIMS_PACKET_LEVEL_VALUE PRIMS_PER_BATCH
/* If there is not enough ring buffer space for the current IB, split draw calls into
 * this number of primitives, so that we can flush the context and get free ring space. */
#define SPLIT_PRIMS_DRAW_LEVEL PRIMS_PER_BATCH

/* Derived values. */
#define WAVES_PER_TG DIV_ROUND_UP(THREADGROUP_SIZE, 64)
#define SPLIT_PRIMS_PACKET_LEVEL                                                                   \
   (VERTEX_COUNTER_GDS_MODE == 2 ? SPLIT_PRIMS_PACKET_LEVEL_VALUE                                  \
                                 : UINT_MAX & ~(THREADGROUP_SIZE - 1))

#define REWIND_SIGNAL_BIT 0x80000000
/* For emulating the rewind packet on CI. */
#define FORCE_REWIND_EMULATION 0

void si_initialize_prim_discard_tunables(struct si_screen *sscreen, bool is_aux_context,
                                         unsigned *prim_discard_vertex_count_threshold,
                                         unsigned *index_ring_size_per_ib)
{
   *prim_discard_vertex_count_threshold = UINT_MAX; /* disable */

   if (sscreen->info.chip_class == GFX6 || /* SI support is not implemented */
       !sscreen->info.has_gds_ordered_append || sscreen->debug_flags & DBG(NO_PD) || is_aux_context)
      return;

   /* TODO: enable this after the GDS kernel memory management is fixed */
   bool enable_on_pro_graphics_by_default = false;

   if (sscreen->debug_flags & DBG(ALWAYS_PD) || sscreen->debug_flags & DBG(PD) ||
       (enable_on_pro_graphics_by_default && sscreen->info.is_pro_graphics &&
        (sscreen->info.family == CHIP_BONAIRE || sscreen->info.family == CHIP_HAWAII ||
         sscreen->info.family == CHIP_TONGA || sscreen->info.family == CHIP_FIJI ||
         sscreen->info.family == CHIP_POLARIS10 || sscreen->info.family == CHIP_POLARIS11 ||
         sscreen->info.family == CHIP_VEGA10 || sscreen->info.family == CHIP_VEGA20))) {
      *prim_discard_vertex_count_threshold = 6000 * 3; /* 6K triangles */

      if (sscreen->debug_flags & DBG(ALWAYS_PD))
         *prim_discard_vertex_count_threshold = 0; /* always enable */

      const uint32_t MB = 1024 * 1024;
      const uint64_t GB = 1024 * 1024 * 1024;

      /* The total size is double this per context.
       * Greater numbers allow bigger gfx IBs.
       */
      if (sscreen->info.vram_size <= 2 * GB)
         *index_ring_size_per_ib = 64 * MB;
      else if (sscreen->info.vram_size <= 4 * GB)
         *index_ring_size_per_ib = 128 * MB;
      else
         *index_ring_size_per_ib = 256 * MB;
   }
}

/* Opcode can be "add" or "swap". */
static LLVMValueRef si_build_ds_ordered_op(struct si_shader_context *ctx, const char *opcode,
                                           LLVMValueRef m0, LLVMValueRef value,
                                           unsigned ordered_count_index, bool release, bool done)
{
   if (ctx->screen->info.chip_class >= GFX10)
      ordered_count_index |= 1 << 24; /* number of dwords == 1 */

   LLVMValueRef args[] = {
      LLVMBuildIntToPtr(ctx->ac.builder, m0, LLVMPointerType(ctx->ac.i32, AC_ADDR_SPACE_GDS), ""),
      value,
      LLVMConstInt(ctx->ac.i32, LLVMAtomicOrderingMonotonic, 0), /* ordering */
      ctx->ac.i32_0,                                             /* scope */
      ctx->ac.i1false,                                           /* volatile */
      LLVMConstInt(ctx->ac.i32, ordered_count_index, 0),
      LLVMConstInt(ctx->ac.i1, release, 0),
      LLVMConstInt(ctx->ac.i1, done, 0),
   };

   char intrinsic[64];
   snprintf(intrinsic, sizeof(intrinsic), "llvm.amdgcn.ds.ordered.%s", opcode);
   return ac_build_intrinsic(&ctx->ac, intrinsic, ctx->ac.i32, args, ARRAY_SIZE(args), 0);
}

static LLVMValueRef si_expand_32bit_pointer(struct si_shader_context *ctx, LLVMValueRef ptr)
{
   uint64_t hi = (uint64_t)ctx->screen->info.address32_hi << 32;
   ptr = LLVMBuildZExt(ctx->ac.builder, ptr, ctx->ac.i64, "");
   ptr = LLVMBuildOr(ctx->ac.builder, ptr, LLVMConstInt(ctx->ac.i64, hi, 0), "");
   return LLVMBuildIntToPtr(ctx->ac.builder, ptr,
                            LLVMPointerType(ctx->ac.i32, AC_ADDR_SPACE_GLOBAL), "");
}

struct si_thread0_section {
   struct si_shader_context *ctx;
   LLVMValueRef vgpr_result; /* a VGPR for the value on thread 0. */
   LLVMValueRef saved_exec;
};

/* Enter a section that only executes on thread 0. */
static void si_enter_thread0_section(struct si_shader_context *ctx,
                                     struct si_thread0_section *section, LLVMValueRef thread_id)
{
   section->ctx = ctx;
   section->vgpr_result = ac_build_alloca_undef(&ctx->ac, ctx->ac.i32, "result0");

   /* This IF has 4 instructions:
    *   v_and_b32_e32 v, 63, v         ; get the thread ID
    *   v_cmp_eq_u32_e32 vcc, 0, v     ; thread ID == 0
    *   s_and_saveexec_b64 s, vcc
    *   s_cbranch_execz BB0_4
    *
    * It could just be s_and_saveexec_b64 s, 1.
    */
   ac_build_ifcc(&ctx->ac, LLVMBuildICmp(ctx->ac.builder, LLVMIntEQ, thread_id, ctx->ac.i32_0, ""),
                 12601);
}

/* Exit a section that only executes on thread 0 and broadcast the result
 * to all threads. */
static void si_exit_thread0_section(struct si_thread0_section *section, LLVMValueRef *result)
{
   struct si_shader_context *ctx = section->ctx;

   LLVMBuildStore(ctx->ac.builder, *result, section->vgpr_result);

   ac_build_endif(&ctx->ac, 12601);

   /* Broadcast the result from thread 0 to all threads. */
   *result =
      ac_build_readlane(&ctx->ac, LLVMBuildLoad(ctx->ac.builder, section->vgpr_result, ""), NULL);
}

void si_build_prim_discard_compute_shader(struct si_shader_context *ctx)
{
   struct si_shader_key *key = &ctx->shader->key;
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef vs = ctx->main_fn;

   /* Always inline the VS function. */
   ac_add_function_attr(ctx->ac.context, vs, -1, AC_FUNC_ATTR_ALWAYSINLINE);
   LLVMSetLinkage(vs, LLVMPrivateLinkage);

   enum ac_arg_type const_desc_type;
   if (ctx->shader->selector->info.base.num_ubos == 1 &&
       ctx->shader->selector->info.base.num_ssbos == 0)
      const_desc_type = AC_ARG_CONST_FLOAT_PTR;
   else
      const_desc_type = AC_ARG_CONST_DESC_PTR;

   memset(&ctx->args, 0, sizeof(ctx->args));

   struct ac_arg param_index_buffers_and_constants, param_vertex_counter;
   struct ac_arg param_vb_desc, param_const_desc;
   struct ac_arg param_base_vertex, param_start_instance;
   struct ac_arg param_block_id, param_local_id, param_ordered_wave_id;
   struct ac_arg param_restart_index, param_smallprim_precision;
   struct ac_arg param_num_prims_udiv_multiplier, param_num_prims_udiv_terms;
   struct ac_arg param_sampler_desc, param_last_wave_prim_id, param_vertex_count_addr;

   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_CONST_DESC_PTR,
              &param_index_buffers_and_constants);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &param_vertex_counter);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &param_last_wave_prim_id);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &param_vertex_count_addr);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_CONST_DESC_PTR, &param_vb_desc);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, const_desc_type, &param_const_desc);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_CONST_IMAGE_PTR, &param_sampler_desc);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &param_base_vertex);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &param_start_instance);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &param_num_prims_udiv_multiplier);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &param_num_prims_udiv_terms);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &param_restart_index);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_FLOAT, &param_smallprim_precision);

   /* Block ID and thread ID inputs. */
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &param_block_id);
   if (VERTEX_COUNTER_GDS_MODE == 2)
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &param_ordered_wave_id);
   ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &param_local_id);

   /* Create the compute shader function. */
   gl_shader_stage old_stage = ctx->stage;
   ctx->stage = MESA_SHADER_COMPUTE;
   si_llvm_create_func(ctx, "prim_discard_cs", NULL, 0, THREADGROUP_SIZE);
   ctx->stage = old_stage;

   if (VERTEX_COUNTER_GDS_MODE == 2) {
      ac_llvm_add_target_dep_function_attr(ctx->main_fn, "amdgpu-gds-size", 256);
   } else if (VERTEX_COUNTER_GDS_MODE == 1) {
      ac_llvm_add_target_dep_function_attr(ctx->main_fn, "amdgpu-gds-size", GDS_SIZE_UNORDERED);
   }

   /* Assemble parameters for VS. */
   LLVMValueRef vs_params[16];
   unsigned num_vs_params = 0;
   unsigned param_vertex_id, param_instance_id;

   vs_params[num_vs_params++] = LLVMGetUndef(LLVMTypeOf(LLVMGetParam(vs, 0))); /* INTERNAL RESOURCES */
   vs_params[num_vs_params++] = LLVMGetUndef(LLVMTypeOf(LLVMGetParam(vs, 1))); /* BINDLESS */
   vs_params[num_vs_params++] = ac_get_arg(&ctx->ac, param_const_desc);
   vs_params[num_vs_params++] = ac_get_arg(&ctx->ac, param_sampler_desc);
   vs_params[num_vs_params++] =
      LLVMConstInt(ctx->ac.i32, S_VS_STATE_INDEXED(key->opt.cs_indexed), 0);
   vs_params[num_vs_params++] = ac_get_arg(&ctx->ac, param_base_vertex);
   vs_params[num_vs_params++] = ac_get_arg(&ctx->ac, param_start_instance);
   vs_params[num_vs_params++] = ctx->ac.i32_0; /* DrawID */
   vs_params[num_vs_params++] = ac_get_arg(&ctx->ac, param_vb_desc);

   vs_params[(param_vertex_id = num_vs_params++)] = NULL;   /* VertexID */
   vs_params[(param_instance_id = num_vs_params++)] = NULL; /* InstanceID */
   vs_params[num_vs_params++] = ctx->ac.i32_0;              /* unused (PrimID) */
   vs_params[num_vs_params++] = ctx->ac.i32_0;              /* unused */

   assert(num_vs_params <= ARRAY_SIZE(vs_params));
   assert(num_vs_params == LLVMCountParamTypes(LLVMGetElementType(LLVMTypeOf(vs))));

   /* Load descriptors. (load 8 dwords at once) */
   LLVMValueRef input_indexbuf, output_indexbuf, tmp, desc[8];

   LLVMValueRef index_buffers_and_constants =
      ac_get_arg(&ctx->ac, param_index_buffers_and_constants);
   tmp = LLVMBuildPointerCast(builder, index_buffers_and_constants,
                              ac_array_in_const32_addr_space(ctx->ac.v8i32), "");
   tmp = ac_build_load_to_sgpr(&ctx->ac, tmp, ctx->ac.i32_0);

   for (unsigned i = 0; i < 8; i++)
      desc[i] = ac_llvm_extract_elem(&ctx->ac, tmp, i);

   input_indexbuf = ac_build_gather_values(&ctx->ac, desc, 4);
   output_indexbuf = ac_build_gather_values(&ctx->ac, desc + 4, 4);

   /* Compute PrimID and InstanceID. */
   LLVMValueRef global_thread_id = ac_build_imad(&ctx->ac, ac_get_arg(&ctx->ac, param_block_id),
                                                 LLVMConstInt(ctx->ac.i32, THREADGROUP_SIZE, 0),
                                                 ac_get_arg(&ctx->ac, param_local_id));
   LLVMValueRef prim_id = global_thread_id; /* PrimID within an instance */
   LLVMValueRef instance_id = ctx->ac.i32_0;

   if (key->opt.cs_instancing) {
      LLVMValueRef num_prims_udiv_terms = ac_get_arg(&ctx->ac, param_num_prims_udiv_terms);
      LLVMValueRef num_prims_udiv_multiplier =
         ac_get_arg(&ctx->ac, param_num_prims_udiv_multiplier);
      /* Unpack num_prims_udiv_terms. */
      LLVMValueRef post_shift =
         LLVMBuildAnd(builder, num_prims_udiv_terms, LLVMConstInt(ctx->ac.i32, 0x1f, 0), "");
      LLVMValueRef prims_per_instance =
         LLVMBuildLShr(builder, num_prims_udiv_terms, LLVMConstInt(ctx->ac.i32, 5, 0), "");
      /* Divide the total prim_id by the number of prims per instance. */
      instance_id =
         ac_build_fast_udiv_u31_d_not_one(&ctx->ac, prim_id, num_prims_udiv_multiplier, post_shift);
      /* Compute the remainder. */
      prim_id = LLVMBuildSub(builder, prim_id,
                             LLVMBuildMul(builder, instance_id, prims_per_instance, ""), "");
   }

   /* Generate indices (like a non-indexed draw call). */
   LLVMValueRef index[4] = {NULL, NULL, NULL, LLVMGetUndef(ctx->ac.i32)};
   unsigned vertices_per_prim = 3;

   switch (key->opt.cs_prim_type) {
   case PIPE_PRIM_TRIANGLES:
      for (unsigned i = 0; i < 3; i++) {
         index[i] = ac_build_imad(&ctx->ac, prim_id, LLVMConstInt(ctx->ac.i32, 3, 0),
                                  LLVMConstInt(ctx->ac.i32, i, 0));
      }
      break;
   case PIPE_PRIM_TRIANGLE_STRIP:
      for (unsigned i = 0; i < 3; i++) {
         index[i] = LLVMBuildAdd(builder, prim_id, LLVMConstInt(ctx->ac.i32, i, 0), "");
      }
      break;
   case PIPE_PRIM_TRIANGLE_FAN:
      /* Vertex 1 is first and vertex 2 is last. This will go to the hw clipper
       * and rasterizer as a normal triangle, so we need to put the provoking
       * vertex into the correct index variable and preserve orientation at the same time.
       * gl_VertexID is preserved, because it's equal to the index.
       */
      if (key->opt.cs_provoking_vertex_first) {
         index[0] = LLVMBuildAdd(builder, prim_id, LLVMConstInt(ctx->ac.i32, 1, 0), "");
         index[1] = LLVMBuildAdd(builder, prim_id, LLVMConstInt(ctx->ac.i32, 2, 0), "");
         index[2] = ctx->ac.i32_0;
      } else {
         index[0] = ctx->ac.i32_0;
         index[1] = LLVMBuildAdd(builder, prim_id, LLVMConstInt(ctx->ac.i32, 1, 0), "");
         index[2] = LLVMBuildAdd(builder, prim_id, LLVMConstInt(ctx->ac.i32, 2, 0), "");
      }
      break;
   default:
      unreachable("unexpected primitive type");
   }

   /* Fetch indices. */
   if (key->opt.cs_indexed) {
      for (unsigned i = 0; i < 3; i++) {
         index[i] = ac_build_buffer_load_format(&ctx->ac, input_indexbuf, index[i], ctx->ac.i32_0,
                                                1, 0, true, false, false);
         index[i] = ac_to_integer(&ctx->ac, index[i]);
      }
   }

   LLVMValueRef ordered_wave_id = NULL;

   /* Extract the ordered wave ID. */
   if (VERTEX_COUNTER_GDS_MODE == 2) {
      ordered_wave_id = ac_get_arg(&ctx->ac, param_ordered_wave_id);
      ordered_wave_id =
         LLVMBuildLShr(builder, ordered_wave_id, LLVMConstInt(ctx->ac.i32, 6, 0), "");
      ordered_wave_id =
         LLVMBuildAnd(builder, ordered_wave_id, LLVMConstInt(ctx->ac.i32, 0xfff, 0), "");
   }
   LLVMValueRef thread_id = LLVMBuildAnd(builder, ac_get_arg(&ctx->ac, param_local_id),
                                         LLVMConstInt(ctx->ac.i32, 63, 0), "");

   /* Every other triangle in a strip has a reversed vertex order, so we
    * need to swap vertices of odd primitives to get the correct primitive
    * orientation when converting triangle strips to triangles. Primitive
    * restart complicates it, because a strip can start anywhere.
    */
   LLVMValueRef prim_restart_accepted = ctx->ac.i1true;
   LLVMValueRef vertex_counter = ac_get_arg(&ctx->ac, param_vertex_counter);

   if (key->opt.cs_prim_type == PIPE_PRIM_TRIANGLE_STRIP) {
      /* Without primitive restart, odd primitives have reversed orientation.
       * Only primitive restart can flip it with respect to the first vertex
       * of the draw call.
       */
      LLVMValueRef first_is_odd = ctx->ac.i1false;

      /* Handle primitive restart. */
      if (key->opt.cs_primitive_restart) {
         /* Get the GDS primitive restart continue flag and clear
          * the flag in vertex_counter. This flag is used when the draw
          * call was split and we need to load the primitive orientation
          * flag from GDS for the first wave too.
          */
         LLVMValueRef gds_prim_restart_continue =
            LLVMBuildLShr(builder, vertex_counter, LLVMConstInt(ctx->ac.i32, 31, 0), "");
         gds_prim_restart_continue =
            LLVMBuildTrunc(builder, gds_prim_restart_continue, ctx->ac.i1, "");
         vertex_counter =
            LLVMBuildAnd(builder, vertex_counter, LLVMConstInt(ctx->ac.i32, 0x7fffffff, 0), "");

         LLVMValueRef index0_is_reset;

         for (unsigned i = 0; i < 3; i++) {
            LLVMValueRef not_reset = LLVMBuildICmp(builder, LLVMIntNE, index[i],
                                                   ac_get_arg(&ctx->ac, param_restart_index), "");
            if (i == 0)
               index0_is_reset = LLVMBuildNot(builder, not_reset, "");
            prim_restart_accepted = LLVMBuildAnd(builder, prim_restart_accepted, not_reset, "");
         }

         /* If the previous waves flip the primitive orientation
          * of the current triangle strip, it will be stored in GDS.
          *
          * Sometimes the correct orientation is not needed, in which case
          * we don't need to execute this.
          */
         if (key->opt.cs_need_correct_orientation && VERTEX_COUNTER_GDS_MODE == 2) {
            /* If there are reset indices in this wave, get the thread index
             * where the most recent strip starts relative to each thread.
             */
            LLVMValueRef preceding_threads_mask =
               LLVMBuildSub(builder,
                            LLVMBuildShl(builder, ctx->ac.i64_1,
                                         LLVMBuildZExt(builder, thread_id, ctx->ac.i64, ""), ""),
                            ctx->ac.i64_1, "");

            LLVMValueRef reset_threadmask = ac_get_i1_sgpr_mask(&ctx->ac, index0_is_reset);
            LLVMValueRef preceding_reset_threadmask =
               LLVMBuildAnd(builder, reset_threadmask, preceding_threads_mask, "");
            LLVMValueRef strip_start = ac_build_umsb(&ctx->ac, preceding_reset_threadmask, NULL);
            strip_start = LLVMBuildAdd(builder, strip_start, ctx->ac.i32_1, "");

            /* This flips the orientation based on reset indices within this wave only. */
            first_is_odd = LLVMBuildTrunc(builder, strip_start, ctx->ac.i1, "");

            LLVMValueRef last_strip_start, prev_wave_state, ret, tmp;
            LLVMValueRef is_first_wave, current_wave_resets_index;

            /* Get the thread index where the last strip starts in this wave.
             *
             * If the last strip doesn't start in this wave, the thread index
             * will be 0.
             *
             * If the last strip starts in the next wave, the thread index will
             * be 64.
             */
            last_strip_start = ac_build_umsb(&ctx->ac, reset_threadmask, NULL);
            last_strip_start = LLVMBuildAdd(builder, last_strip_start, ctx->ac.i32_1, "");

            struct si_thread0_section section;
            si_enter_thread0_section(ctx, &section, thread_id);

            /* This must be done in the thread 0 section, because
             * we expect PrimID to be 0 for the whole first wave
             * in this expression.
             *
             * NOTE: This will need to be different if we wanna support
             * instancing with primitive restart.
             */
            is_first_wave = LLVMBuildICmp(builder, LLVMIntEQ, prim_id, ctx->ac.i32_0, "");
            is_first_wave = LLVMBuildAnd(builder, is_first_wave,
                                         LLVMBuildNot(builder, gds_prim_restart_continue, ""), "");
            current_wave_resets_index =
               LLVMBuildICmp(builder, LLVMIntNE, last_strip_start, ctx->ac.i32_0, "");

            ret = ac_build_alloca_undef(&ctx->ac, ctx->ac.i32, "prev_state");

            /* Save the last strip start primitive index in GDS and read
             * the value that previous waves stored.
             *
             * if (is_first_wave || current_wave_resets_strip)
             *    // Read the value that previous waves stored and store a new one.
             *    first_is_odd = ds.ordered.swap(last_strip_start);
             * else
             *    // Just read the value that previous waves stored.
             *    first_is_odd = ds.ordered.add(0);
             */
            ac_build_ifcc(
               &ctx->ac, LLVMBuildOr(builder, is_first_wave, current_wave_resets_index, ""), 12602);
            {
               /* The GDS address is always 0 with ordered append. */
               tmp = si_build_ds_ordered_op(ctx, "swap", ordered_wave_id, last_strip_start, 1, true,
                                            false);
               LLVMBuildStore(builder, tmp, ret);
            }
            ac_build_else(&ctx->ac, 12603);
            {
               /* Just read the value from GDS. */
               tmp = si_build_ds_ordered_op(ctx, "add", ordered_wave_id, ctx->ac.i32_0, 1, true,
                                            false);
               LLVMBuildStore(builder, tmp, ret);
            }
            ac_build_endif(&ctx->ac, 12602);

            prev_wave_state = LLVMBuildLoad(builder, ret, "");
            /* Ignore the return value if this is the first wave. */
            prev_wave_state =
               LLVMBuildSelect(builder, is_first_wave, ctx->ac.i32_0, prev_wave_state, "");
            si_exit_thread0_section(&section, &prev_wave_state);
            prev_wave_state = LLVMBuildTrunc(builder, prev_wave_state, ctx->ac.i1, "");

            /* If the strip start appears to be on thread 0 for the current primitive
             * (meaning the reset index is not present in this wave and might have
             * appeared in previous waves), use the value from GDS to determine
             * primitive orientation.
             *
             * If the strip start is in this wave for the current primitive, use
             * the value from the current wave to determine primitive orientation.
             */
            LLVMValueRef strip_start_is0 =
               LLVMBuildICmp(builder, LLVMIntEQ, strip_start, ctx->ac.i32_0, "");
            first_is_odd =
               LLVMBuildSelect(builder, strip_start_is0, prev_wave_state, first_is_odd, "");
         }
      }
      /* prim_is_odd = (first_is_odd + current_is_odd) % 2. */
      LLVMValueRef prim_is_odd = LLVMBuildXor(
         builder, first_is_odd, LLVMBuildTrunc(builder, thread_id, ctx->ac.i1, ""), "");

      /* Convert triangle strip indices to triangle indices. */
      ac_build_triangle_strip_indices_to_triangle(
         &ctx->ac, prim_is_odd, LLVMConstInt(ctx->ac.i1, key->opt.cs_provoking_vertex_first, 0),
         index);
   }

   /* Execute the vertex shader for each vertex to get vertex positions. */
   LLVMValueRef pos[3][4];
   for (unsigned i = 0; i < vertices_per_prim; i++) {
      vs_params[param_vertex_id] = index[i];
      vs_params[param_instance_id] = instance_id;

      LLVMValueRef ret = ac_build_call(&ctx->ac, vs, vs_params, num_vs_params);
      for (unsigned chan = 0; chan < 4; chan++)
         pos[i][chan] = LLVMBuildExtractValue(builder, ret, chan, "");
   }

   /* Divide XYZ by W. */
   for (unsigned i = 0; i < vertices_per_prim; i++) {
      for (unsigned chan = 0; chan < 3; chan++)
         pos[i][chan] = ac_build_fdiv(&ctx->ac, pos[i][chan], pos[i][3]);
   }

   /* Load the viewport state. */
   LLVMValueRef vp = ac_build_load_invariant(&ctx->ac, index_buffers_and_constants,
                                             LLVMConstInt(ctx->ac.i32, 2, 0));
   vp = LLVMBuildBitCast(builder, vp, ctx->ac.v4f32, "");
   LLVMValueRef vp_scale[2], vp_translate[2];
   vp_scale[0] = ac_llvm_extract_elem(&ctx->ac, vp, 0);
   vp_scale[1] = ac_llvm_extract_elem(&ctx->ac, vp, 1);
   vp_translate[0] = ac_llvm_extract_elem(&ctx->ac, vp, 2);
   vp_translate[1] = ac_llvm_extract_elem(&ctx->ac, vp, 3);

   /* Do culling. */
   struct ac_cull_options options = {};
   options.cull_front = key->opt.cs_cull_front;
   options.cull_back = key->opt.cs_cull_back;
   options.cull_view_xy = true;
   options.cull_view_near_z = CULL_Z && key->opt.cs_cull_z;
   options.cull_view_far_z = CULL_Z && key->opt.cs_cull_z;
   options.cull_small_prims = true;
   options.cull_zero_area = true;
   options.cull_w = true;
   options.use_halfz_clip_space = key->opt.cs_halfz_clip_space;

   LLVMValueRef accepted =
      ac_cull_triangle(&ctx->ac, pos, prim_restart_accepted, vp_scale, vp_translate,
                       ac_get_arg(&ctx->ac, param_smallprim_precision), &options);

   ac_build_optimization_barrier(&ctx->ac, &accepted);
   LLVMValueRef accepted_threadmask = ac_get_i1_sgpr_mask(&ctx->ac, accepted);

   /* Count the number of active threads by doing bitcount(accepted). */
   LLVMValueRef num_prims_accepted = ac_build_intrinsic(
      &ctx->ac, "llvm.ctpop.i64", ctx->ac.i64, &accepted_threadmask, 1, AC_FUNC_ATTR_READNONE);
   num_prims_accepted = LLVMBuildTrunc(builder, num_prims_accepted, ctx->ac.i32, "");

   LLVMValueRef start;

   /* Execute atomic_add on the vertex count. */
   struct si_thread0_section section;
   si_enter_thread0_section(ctx, &section, thread_id);
   {
      if (VERTEX_COUNTER_GDS_MODE == 0) {
         LLVMValueRef num_indices = LLVMBuildMul(
            builder, num_prims_accepted, LLVMConstInt(ctx->ac.i32, vertices_per_prim, 0), "");
         vertex_counter = si_expand_32bit_pointer(ctx, vertex_counter);
         start = LLVMBuildAtomicRMW(builder, LLVMAtomicRMWBinOpAdd, vertex_counter, num_indices,
                                    LLVMAtomicOrderingMonotonic, false);
      } else if (VERTEX_COUNTER_GDS_MODE == 1) {
         LLVMValueRef num_indices = LLVMBuildMul(
            builder, num_prims_accepted, LLVMConstInt(ctx->ac.i32, vertices_per_prim, 0), "");
         vertex_counter = LLVMBuildIntToPtr(builder, vertex_counter,
                                            LLVMPointerType(ctx->ac.i32, AC_ADDR_SPACE_GDS), "");
         start = LLVMBuildAtomicRMW(builder, LLVMAtomicRMWBinOpAdd, vertex_counter, num_indices,
                                    LLVMAtomicOrderingMonotonic, false);
      } else if (VERTEX_COUNTER_GDS_MODE == 2) {
         LLVMValueRef tmp_store = ac_build_alloca_undef(&ctx->ac, ctx->ac.i32, "");

         /* If the draw call was split into multiple subdraws, each using
          * a separate draw packet, we need to start counting from 0 for
          * the first compute wave of the subdraw.
          *
          * vertex_counter contains the primitive ID of the first thread
          * in the first wave.
          *
          * This is only correct with VERTEX_COUNTER_GDS_MODE == 2:
          */
         LLVMValueRef is_first_wave =
            LLVMBuildICmp(builder, LLVMIntEQ, global_thread_id, vertex_counter, "");

         /* Store the primitive count for ordered append, not vertex count.
          * The idea is to avoid GDS initialization via CP DMA. The shader
          * effectively stores the first count using "swap".
          *
          * if (first_wave) {
          *    ds.ordered.swap(num_prims_accepted); // store the first primitive count
          *    previous = 0;
          * } else {
          *    previous = ds.ordered.add(num_prims_accepted) // add the primitive count
          * }
          */
         ac_build_ifcc(&ctx->ac, is_first_wave, 12604);
         {
            /* The GDS address is always 0 with ordered append. */
            si_build_ds_ordered_op(ctx, "swap", ordered_wave_id, num_prims_accepted, 0, true, true);
            LLVMBuildStore(builder, ctx->ac.i32_0, tmp_store);
         }
         ac_build_else(&ctx->ac, 12605);
         {
            LLVMBuildStore(builder,
                           si_build_ds_ordered_op(ctx, "add", ordered_wave_id, num_prims_accepted,
                                                  0, true, true),
                           tmp_store);
         }
         ac_build_endif(&ctx->ac, 12604);

         start = LLVMBuildLoad(builder, tmp_store, "");
      }
   }
   si_exit_thread0_section(&section, &start);

   /* Write the final vertex count to memory. An EOS/EOP event could do this,
    * but those events are super slow and should be avoided if performance
    * is a concern. Thanks to GDS ordered append, we can emulate a CS_DONE
    * event like this.
    */
   if (VERTEX_COUNTER_GDS_MODE == 2) {
      ac_build_ifcc(&ctx->ac,
                    LLVMBuildICmp(builder, LLVMIntEQ, global_thread_id,
                                  ac_get_arg(&ctx->ac, param_last_wave_prim_id), ""),
                    12606);
      LLVMValueRef count = LLVMBuildAdd(builder, start, num_prims_accepted, "");
      count = LLVMBuildMul(builder, count, LLVMConstInt(ctx->ac.i32, vertices_per_prim, 0), "");

      /* GFX8 needs to disable caching, so that the CP can see the stored value.
       * MTYPE=3 bypasses TC L2.
       */
      if (ctx->screen->info.chip_class <= GFX8) {
         LLVMValueRef desc[] = {
            ac_get_arg(&ctx->ac, param_vertex_count_addr),
            LLVMConstInt(ctx->ac.i32, S_008F04_BASE_ADDRESS_HI(ctx->screen->info.address32_hi), 0),
            LLVMConstInt(ctx->ac.i32, 4, 0),
            LLVMConstInt(
               ctx->ac.i32,
               S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32) | S_008F0C_MTYPE(3 /* uncached */),
               0),
         };
         LLVMValueRef rsrc = ac_build_gather_values(&ctx->ac, desc, 4);
         ac_build_buffer_store_dword(&ctx->ac, rsrc, count, 1, ctx->ac.i32_0, ctx->ac.i32_0, 0,
                                     ac_glc | ac_slc);
      } else {
         LLVMBuildStore(
            builder, count,
            si_expand_32bit_pointer(ctx, ac_get_arg(&ctx->ac, param_vertex_count_addr)));
      }
      ac_build_endif(&ctx->ac, 12606);
   } else {
      /* For unordered modes that increment a vertex count instead of
       * primitive count, convert it into the primitive index.
       */
      start = LLVMBuildUDiv(builder, start, LLVMConstInt(ctx->ac.i32, vertices_per_prim, 0), "");
   }

   /* Now we need to store the indices of accepted primitives into
    * the output index buffer.
    */
   ac_build_ifcc(&ctx->ac, accepted, 16607);
   {
      /* Get the number of bits set before the index of this thread. */
      LLVMValueRef prim_index = ac_build_mbcnt(&ctx->ac, accepted_threadmask);

      /* We have lowered instancing. Pack the instance ID into vertex ID. */
      if (key->opt.cs_instancing) {
         instance_id = LLVMBuildShl(builder, instance_id, LLVMConstInt(ctx->ac.i32, 16, 0), "");

         for (unsigned i = 0; i < vertices_per_prim; i++)
            index[i] = LLVMBuildOr(builder, index[i], instance_id, "");
      }

      if (VERTEX_COUNTER_GDS_MODE == 2) {
         /* vertex_counter contains the first primitive ID
          * for this dispatch. If the draw call was split into
          * multiple subdraws, the first primitive ID is > 0
          * for subsequent subdraws. Each subdraw uses a different
          * portion of the output index buffer. Offset the store
          * vindex by the first primitive ID to get the correct
          * store address for the subdraw.
          */
         start = LLVMBuildAdd(builder, start, vertex_counter, "");
      }

      /* Write indices for accepted primitives. */
      LLVMValueRef vindex = LLVMBuildAdd(builder, start, prim_index, "");
      LLVMValueRef vdata = ac_build_gather_values(&ctx->ac, index, 3);

      if (!ac_has_vec3_support(ctx->ac.chip_class, true))
         vdata = ac_build_expand_to_vec4(&ctx->ac, vdata, 3);

      ac_build_buffer_store_format(&ctx->ac, output_indexbuf, vdata, vindex, ctx->ac.i32_0,
                                   ac_glc | (INDEX_STORES_USE_SLC ? ac_slc : 0));
   }
   ac_build_endif(&ctx->ac, 16607);

   LLVMBuildRetVoid(builder);
}

/* Return false if the shader isn't ready. */
static bool si_shader_select_prim_discard_cs(struct si_context *sctx,
                                             const struct pipe_draw_info *info,
                                             bool primitive_restart)
{
   struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
   struct si_shader_key key;

   /* Primitive restart needs ordered counters. */
   assert(!primitive_restart || VERTEX_COUNTER_GDS_MODE == 2);
   assert(!primitive_restart || info->instance_count == 1);

   memset(&key, 0, sizeof(key));
   si_shader_selector_key_vs(sctx, sctx->shader.vs.cso, &key, &key.part.vs.prolog);
   assert(!key.part.vs.prolog.instance_divisor_is_fetched);

   key.part.vs.prolog.unpack_instance_id_from_vertex_id = 0;
   key.opt.vs_as_prim_discard_cs = 1;
   key.opt.cs_prim_type = info->mode;
   key.opt.cs_indexed = info->index_size != 0;
   key.opt.cs_instancing = info->instance_count > 1;
   key.opt.cs_primitive_restart = primitive_restart;
   key.opt.cs_provoking_vertex_first = rs->provoking_vertex_first;

   /* Primitive restart with triangle strips needs to preserve primitive
    * orientation for cases where front and back primitive orientation matters.
    */
   if (primitive_restart) {
      struct si_shader_selector *ps = sctx->shader.ps.cso;

      key.opt.cs_need_correct_orientation = rs->cull_front != rs->cull_back ||
                                            ps->info.uses_frontface ||
                                            (rs->two_side && ps->info.colors_read);
   }

   if (rs->rasterizer_discard) {
      /* Just for performance testing and analysis of trivial bottlenecks.
       * This should result in a very short compute shader. */
      key.opt.cs_cull_front = 1;
      key.opt.cs_cull_back = 1;
   } else {
      key.opt.cs_cull_front = sctx->viewport0_y_inverted ? rs->cull_back : rs->cull_front;
      key.opt.cs_cull_back = sctx->viewport0_y_inverted ? rs->cull_front : rs->cull_back;
   }

   if (!rs->depth_clamp_any && CULL_Z) {
      key.opt.cs_cull_z = 1;
      key.opt.cs_halfz_clip_space = rs->clip_halfz;
   }

   sctx->cs_prim_discard_state.cso = sctx->shader.vs.cso;
   sctx->cs_prim_discard_state.current = NULL;

   if (!sctx->compiler.passes)
      si_init_compiler(sctx->screen, &sctx->compiler);

   struct si_compiler_ctx_state compiler_state;
   compiler_state.compiler = &sctx->compiler;
   compiler_state.debug = sctx->debug;
   compiler_state.is_debug_context = sctx->is_debug;

   return si_shader_select_with_key(sctx->screen, &sctx->cs_prim_discard_state, &compiler_state,
                                    &key, -1, true) == 0 &&
          /* Disallow compute shaders using the scratch buffer. */
          sctx->cs_prim_discard_state.current->config.scratch_bytes_per_wave == 0;
}

static bool si_initialize_prim_discard_cmdbuf(struct si_context *sctx)
{
   if (sctx->index_ring)
      return true;

   if (!sctx->prim_discard_compute_cs.priv) {
      struct radeon_winsys *ws = sctx->ws;
      unsigned gds_size =
         VERTEX_COUNTER_GDS_MODE == 1 ? GDS_SIZE_UNORDERED : VERTEX_COUNTER_GDS_MODE == 2 ? 8 : 0;
      unsigned num_oa_counters = VERTEX_COUNTER_GDS_MODE == 2 ? 2 : 0;

      if (gds_size) {
         sctx->gds = ws->buffer_create(ws, gds_size, 4, RADEON_DOMAIN_GDS,
                                       RADEON_FLAG_DRIVER_INTERNAL);
         if (!sctx->gds)
            return false;

         ws->cs_add_buffer(&sctx->gfx_cs, sctx->gds, RADEON_USAGE_READWRITE, 0, 0);
      }
      if (num_oa_counters) {
         assert(gds_size);
         sctx->gds_oa = ws->buffer_create(ws, num_oa_counters, 1, RADEON_DOMAIN_OA,
                                          RADEON_FLAG_DRIVER_INTERNAL);
         if (!sctx->gds_oa)
            return false;

         ws->cs_add_buffer(&sctx->gfx_cs, sctx->gds_oa, RADEON_USAGE_READWRITE, 0, 0);
      }

      if (!ws->cs_add_parallel_compute_ib(&sctx->prim_discard_compute_cs,
                                          &sctx->gfx_cs, num_oa_counters > 0))
         return false;
   }

   if (!sctx->index_ring) {
      sctx->index_ring = si_aligned_buffer_create(
         sctx->b.screen, SI_RESOURCE_FLAG_UNMAPPABLE | SI_RESOURCE_FLAG_DRIVER_INTERNAL,
         PIPE_USAGE_DEFAULT,
         sctx->index_ring_size_per_ib * 2, sctx->screen->info.pte_fragment_size);
      if (!sctx->index_ring)
         return false;
   }
   return true;
}

static bool si_check_ring_space(struct si_context *sctx, unsigned out_indexbuf_size)
{
   return sctx->index_ring_offset +
             align(out_indexbuf_size, sctx->screen->info.tcc_cache_line_size) <=
          sctx->index_ring_size_per_ib;
}

enum si_prim_discard_outcome
si_prepare_prim_discard_or_split_draw(struct si_context *sctx, const struct pipe_draw_info *info,
                                      const struct pipe_draw_start_count *draws,
                                      unsigned num_draws, bool primitive_restart,
                                      unsigned total_count)
{
   /* If the compute shader compilation isn't finished, this returns false. */
   if (!si_shader_select_prim_discard_cs(sctx, info, primitive_restart))
      return SI_PRIM_DISCARD_DISABLED;

   if (!si_initialize_prim_discard_cmdbuf(sctx))
      return SI_PRIM_DISCARD_DISABLED;

   struct radeon_cmdbuf *gfx_cs = &sctx->gfx_cs;
   unsigned prim = info->mode;
   unsigned count = total_count;
   unsigned instance_count = info->instance_count;
   unsigned num_prims_per_instance = u_decomposed_prims_for_vertices(prim, count);
   unsigned num_prims = num_prims_per_instance * instance_count;
   unsigned out_indexbuf_size = num_prims * 12;
   bool ring_full = !si_check_ring_space(sctx, out_indexbuf_size);
   const unsigned split_prims_draw_level = SPLIT_PRIMS_DRAW_LEVEL;

   /* Split draws at the draw call level if the ring is full. This makes
    * better use of the ring space.
    */
   if (ring_full && num_prims > split_prims_draw_level &&
       instance_count == 1 && /* TODO: support splitting instanced draws */
       (1 << prim) & ((1 << PIPE_PRIM_TRIANGLES) | (1 << PIPE_PRIM_TRIANGLE_STRIP))) {
      unsigned vert_count_per_subdraw = 0;

      if (prim == PIPE_PRIM_TRIANGLES)
         vert_count_per_subdraw = split_prims_draw_level * 3;
      else if (prim == PIPE_PRIM_TRIANGLE_STRIP)
         vert_count_per_subdraw = split_prims_draw_level;
      else
         unreachable("shouldn't get here");

      /* Split multi draws first. */
      if (num_draws > 1) {
         unsigned count = 0;
         unsigned first_draw = 0;
         unsigned num_draws_split = 0;

         for (unsigned i = 0; i < num_draws; i++) {
            if (count && count + draws[i].count > vert_count_per_subdraw) {
               /* Submit previous draws.  */
               sctx->b.draw_vbo(&sctx->b, info, NULL, draws + first_draw, num_draws_split);
               count = 0;
               first_draw = i;
               num_draws_split = 0;
            }

            if (draws[i].count > vert_count_per_subdraw) {
               /* Submit just 1 draw. It will be split. */
               sctx->b.draw_vbo(&sctx->b, info, NULL, draws + i, 1);
               assert(count == 0);
               assert(first_draw == i);
               assert(num_draws_split == 0);
               first_draw = i + 1;
               continue;
            }

            count += draws[i].count;
            num_draws_split++;
         }
         return SI_PRIM_DISCARD_MULTI_DRAW_SPLIT;
      }

      /* Split single draws if splitting multi draws isn't enough. */
      struct pipe_draw_info split_draw = *info;
      struct pipe_draw_start_count split_draw_range = draws[0];
      unsigned base_start = split_draw_range.start;

      split_draw.primitive_restart = primitive_restart;

      if (prim == PIPE_PRIM_TRIANGLES) {
         assert(vert_count_per_subdraw < count);

         for (unsigned start = 0; start < count; start += vert_count_per_subdraw) {
            split_draw_range.start = base_start + start;
            split_draw_range.count = MIN2(count - start, vert_count_per_subdraw);

            sctx->b.draw_vbo(&sctx->b, &split_draw, NULL, &split_draw_range, 1);
         }
      } else if (prim == PIPE_PRIM_TRIANGLE_STRIP) {
         /* No primitive pair can be split, because strips reverse orientation
          * for odd primitives. */
         STATIC_ASSERT(split_prims_draw_level % 2 == 0);

         for (unsigned start = 0; start < count - 2; start += vert_count_per_subdraw) {
            split_draw_range.start = base_start + start;
            split_draw_range.count = MIN2(count - start, vert_count_per_subdraw + 2);

            sctx->b.draw_vbo(&sctx->b, &split_draw, NULL, &split_draw_range, 1);

            if (start == 0 && primitive_restart &&
                sctx->cs_prim_discard_state.current->key.opt.cs_need_correct_orientation)
               sctx->preserve_prim_restart_gds_at_flush = true;
         }
         sctx->preserve_prim_restart_gds_at_flush = false;
      }

      return SI_PRIM_DISCARD_DRAW_SPLIT;
   }

   /* Just quit if the draw call doesn't fit into the ring and can't be split. */
   if (out_indexbuf_size > sctx->index_ring_size_per_ib) {
      if (SI_PRIM_DISCARD_DEBUG)
         puts("PD failed: draw call too big, can't be split");
      return SI_PRIM_DISCARD_DISABLED;
   }

   unsigned num_subdraws = DIV_ROUND_UP(num_prims, SPLIT_PRIMS_PACKET_LEVEL) * num_draws;
   unsigned need_compute_dw = 11 /* shader */ + 34 /* first draw */ +
                              24 * (num_subdraws - 1) + /* subdraws */
                              30;                       /* leave some space at the end */
   unsigned need_gfx_dw = si_get_minimum_num_gfx_cs_dwords(sctx, 0);

   if (sctx->chip_class <= GFX7 || FORCE_REWIND_EMULATION)
      need_gfx_dw += 9; /* NOP(2) + WAIT_REG_MEM(7), then chain */
   else
      need_gfx_dw += num_subdraws * 8; /* use REWIND(2) + DRAW(6) */

   if (ring_full ||
       (VERTEX_COUNTER_GDS_MODE == 1 && sctx->compute_gds_offset + 8 > GDS_SIZE_UNORDERED) ||
       !sctx->ws->cs_check_space(gfx_cs, need_gfx_dw, false)) {
      /* If the current IB is empty but the size is too small, add a NOP
       * packet to force a flush and get a bigger IB.
       */
      if (!radeon_emitted(gfx_cs, sctx->initial_gfx_cs_size) &&
          gfx_cs->current.cdw + need_gfx_dw > gfx_cs->current.max_dw) {
         radeon_begin(gfx_cs);
         radeon_emit(gfx_cs, PKT3(PKT3_NOP, 0, 0));
         radeon_emit(gfx_cs, 0);
         radeon_end();
      }

      si_flush_gfx_cs(sctx, RADEON_FLUSH_ASYNC_START_NEXT_GFX_IB_NOW, NULL);
   }

   /* The compute IB is always chained, but we need to call cs_check_space to add more space. */
   struct radeon_cmdbuf *cs = &sctx->prim_discard_compute_cs;
   ASSERTED bool compute_has_space = sctx->ws->cs_check_space(cs, need_compute_dw, false);
   assert(compute_has_space);
   assert(si_check_ring_space(sctx, out_indexbuf_size));
   return SI_PRIM_DISCARD_ENABLED;
}

void si_compute_signal_gfx(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->prim_discard_compute_cs;
   unsigned writeback_L2_flags = 0;

   /* The writeback L2 flags vary with each chip generation. */
   /* CI needs to flush vertex indices to memory. */
   if (sctx->chip_class <= GFX7)
      writeback_L2_flags = EVENT_TC_WB_ACTION_ENA;
   else if (sctx->chip_class == GFX8 && VERTEX_COUNTER_GDS_MODE == 0)
      writeback_L2_flags = EVENT_TC_WB_ACTION_ENA | EVENT_TC_NC_ACTION_ENA;

   if (!sctx->compute_num_prims_in_batch)
      return;

   assert(sctx->compute_rewind_va);

   /* After the queued dispatches are done and vertex counts are written to
    * the gfx IB, signal the gfx IB to continue. CP doesn't wait for
    * the dispatches to finish, it only adds the CS_DONE event into the event
    * queue.
    */
   si_cp_release_mem(sctx, cs, V_028A90_CS_DONE, writeback_L2_flags,
                     sctx->chip_class <= GFX8 ? EOP_DST_SEL_MEM : EOP_DST_SEL_TC_L2,
                     writeback_L2_flags ? EOP_INT_SEL_SEND_DATA_AFTER_WR_CONFIRM : EOP_INT_SEL_NONE,
                     EOP_DATA_SEL_VALUE_32BIT, NULL,
                     sctx->compute_rewind_va | ((uint64_t)sctx->screen->info.address32_hi << 32),
                     REWIND_SIGNAL_BIT, /* signaling value for the REWIND packet */
                     SI_NOT_QUERY);

   sctx->compute_rewind_va = 0;
   sctx->compute_num_prims_in_batch = 0;
}

/* Dispatch a primitive discard compute shader. */
void si_dispatch_prim_discard_cs_and_draw(struct si_context *sctx,
                                          const struct pipe_draw_info *info,
                                          unsigned count, unsigned index_size,
                                          unsigned base_vertex, uint64_t input_indexbuf_va,
                                          unsigned input_indexbuf_num_elements)
{
   struct radeon_cmdbuf *gfx_cs = &sctx->gfx_cs;
   struct radeon_cmdbuf *cs = &sctx->prim_discard_compute_cs;
   unsigned num_prims_per_instance = u_decomposed_prims_for_vertices(info->mode, count);
   if (!num_prims_per_instance)
      return;

   unsigned num_prims = num_prims_per_instance * info->instance_count;
   unsigned vertices_per_prim, output_indexbuf_format, gfx10_output_indexbuf_format;

   switch (info->mode) {
   case PIPE_PRIM_TRIANGLES:
   case PIPE_PRIM_TRIANGLE_STRIP:
   case PIPE_PRIM_TRIANGLE_FAN:
      vertices_per_prim = 3;
      output_indexbuf_format = V_008F0C_BUF_DATA_FORMAT_32_32_32;
      gfx10_output_indexbuf_format = V_008F0C_IMG_FORMAT_32_32_32_UINT;
      break;
   default:
      unreachable("unsupported primitive type");
      return;
   }

   unsigned out_indexbuf_offset;
   uint64_t output_indexbuf_size = num_prims * vertices_per_prim * 4;
   bool first_dispatch = !sctx->prim_discard_compute_ib_initialized;

   /* Initialize the compute IB if it's empty. */
   if (!sctx->prim_discard_compute_ib_initialized) {
      /* 1) State initialization. */
      sctx->compute_gds_offset = 0;
      sctx->compute_ib_last_shader = NULL;

      if (sctx->last_ib_barrier_fence) {
         assert(!sctx->last_ib_barrier_buf);
         sctx->ws->cs_add_fence_dependency(gfx_cs, sctx->last_ib_barrier_fence,
                                           RADEON_DEPENDENCY_PARALLEL_COMPUTE_ONLY);
      }

      /* 2) IB initialization. */

      /* This needs to be done at the beginning of IBs due to possible
       * TTM buffer moves in the kernel.
       */
      if (sctx->chip_class >= GFX10) {
         radeon_begin(cs);
         radeon_emit(cs, PKT3(PKT3_ACQUIRE_MEM, 6, 0));
         radeon_emit(cs, 0);          /* CP_COHER_CNTL */
         radeon_emit(cs, 0xffffffff); /* CP_COHER_SIZE */
         radeon_emit(cs, 0xffffff);   /* CP_COHER_SIZE_HI */
         radeon_emit(cs, 0);          /* CP_COHER_BASE */
         radeon_emit(cs, 0);          /* CP_COHER_BASE_HI */
         radeon_emit(cs, 0x0000000A); /* POLL_INTERVAL */
         radeon_emit(cs,              /* GCR_CNTL */
                     S_586_GLI_INV(V_586_GLI_ALL) | S_586_GLK_INV(1) | S_586_GLV_INV(1) |
                        S_586_GL1_INV(1) | S_586_GL2_INV(1) | S_586_GL2_WB(1) | S_586_GLM_INV(1) |
                        S_586_GLM_WB(1) | S_586_SEQ(V_586_SEQ_FORWARD));
         radeon_end();
      } else {
         si_emit_surface_sync(sctx, cs,
                              S_0085F0_TC_ACTION_ENA(1) | S_0085F0_TCL1_ACTION_ENA(1) |
                                 S_0301F0_TC_WB_ACTION_ENA(sctx->chip_class >= GFX8) |
                                 S_0085F0_SH_ICACHE_ACTION_ENA(1) |
                                 S_0085F0_SH_KCACHE_ACTION_ENA(1));
      }

      /* Restore the GDS prim restart counter if needed. */
      if (sctx->preserve_prim_restart_gds_at_flush) {
         si_cp_copy_data(sctx, cs, COPY_DATA_GDS, NULL, 4, COPY_DATA_SRC_MEM,
                         sctx->wait_mem_scratch, 4);
      }

      si_emit_initial_compute_regs(sctx, cs);

      radeon_begin(cs);
      radeon_set_sh_reg(
         cs, R_00B860_COMPUTE_TMPRING_SIZE,
         S_00B860_WAVES(sctx->scratch_waves) | S_00B860_WAVESIZE(0)); /* no scratch */

      /* Only 1D grids are launched. */
      radeon_set_sh_reg_seq(cs, R_00B820_COMPUTE_NUM_THREAD_Y, 2);
      radeon_emit(cs, S_00B820_NUM_THREAD_FULL(1) | S_00B820_NUM_THREAD_PARTIAL(1));
      radeon_emit(cs, S_00B824_NUM_THREAD_FULL(1) | S_00B824_NUM_THREAD_PARTIAL(1));

      radeon_set_sh_reg_seq(cs, R_00B814_COMPUTE_START_Y, 2);
      radeon_emit(cs, 0);
      radeon_emit(cs, 0);

      /* Disable ordered alloc for OA resources. */
      for (unsigned i = 0; i < 2; i++) {
         radeon_set_uconfig_reg_seq(cs, R_031074_GDS_OA_CNTL, 3, false);
         radeon_emit(cs, S_031074_INDEX(i));
         radeon_emit(cs, 0);
         radeon_emit(cs, S_03107C_ENABLE(0));
      }
      radeon_end();

      if (sctx->last_ib_barrier_buf) {
         assert(!sctx->last_ib_barrier_fence);
         radeon_add_to_buffer_list(sctx, gfx_cs, sctx->last_ib_barrier_buf, RADEON_USAGE_READ,
                                   RADEON_PRIO_FENCE);
         si_cp_wait_mem(sctx, cs,
                        sctx->last_ib_barrier_buf->gpu_address + sctx->last_ib_barrier_buf_offset,
                        1, 1, WAIT_REG_MEM_EQUAL);
      }

      sctx->prim_discard_compute_ib_initialized = true;
   }

   /* Allocate the output index buffer. */
   output_indexbuf_size = align(output_indexbuf_size, sctx->screen->info.tcc_cache_line_size);
   assert(sctx->index_ring_offset + output_indexbuf_size <= sctx->index_ring_size_per_ib);
   out_indexbuf_offset = sctx->index_ring_base + sctx->index_ring_offset;
   sctx->index_ring_offset += output_indexbuf_size;

   radeon_add_to_buffer_list(sctx, gfx_cs, sctx->index_ring, RADEON_USAGE_READWRITE,
                             RADEON_PRIO_SHADER_RW_BUFFER);
   uint64_t out_indexbuf_va = sctx->index_ring->gpu_address + out_indexbuf_offset;

   /* Prepare index buffer descriptors. */
   struct si_resource *indexbuf_desc = NULL;
   unsigned indexbuf_desc_offset;
   unsigned desc_size = 12 * 4;
   uint32_t *desc;

   u_upload_alloc(sctx->b.const_uploader, 0, desc_size, si_optimal_tcc_alignment(sctx, desc_size),
                  &indexbuf_desc_offset, (struct pipe_resource **)&indexbuf_desc, (void **)&desc);
   radeon_add_to_buffer_list(sctx, gfx_cs, indexbuf_desc, RADEON_USAGE_READ,
                             RADEON_PRIO_DESCRIPTORS);

   /* Input index buffer. */
   desc[0] = input_indexbuf_va;
   desc[1] = S_008F04_BASE_ADDRESS_HI(input_indexbuf_va >> 32) | S_008F04_STRIDE(index_size);
   desc[2] = input_indexbuf_num_elements * (sctx->chip_class == GFX8 ? index_size : 1);

   if (sctx->chip_class >= GFX10) {
      desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
                S_008F0C_FORMAT(index_size == 1 ? V_008F0C_IMG_FORMAT_8_UINT
                                                : index_size == 2 ? V_008F0C_IMG_FORMAT_16_UINT
                                                                  : V_008F0C_IMG_FORMAT_32_UINT) |
                S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_STRUCTURED_WITH_OFFSET) |
                S_008F0C_RESOURCE_LEVEL(1);
   } else {
      desc[3] =
         S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_UINT) |
         S_008F0C_DATA_FORMAT(index_size == 1 ? V_008F0C_BUF_DATA_FORMAT_8
                                              : index_size == 2 ? V_008F0C_BUF_DATA_FORMAT_16
                                                                : V_008F0C_BUF_DATA_FORMAT_32);
   }

   /* Output index buffer. */
   desc[4] = out_indexbuf_va;
   desc[5] =
      S_008F04_BASE_ADDRESS_HI(out_indexbuf_va >> 32) | S_008F04_STRIDE(vertices_per_prim * 4);
   desc[6] = num_prims * (sctx->chip_class == GFX8 ? vertices_per_prim * 4 : 1);

   if (sctx->chip_class >= GFX10) {
      desc[7] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_0) |
                S_008F0C_FORMAT(gfx10_output_indexbuf_format) |
                S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_STRUCTURED_WITH_OFFSET) |
                S_008F0C_RESOURCE_LEVEL(1);
   } else {
      desc[7] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_0) |
                S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_UINT) |
                S_008F0C_DATA_FORMAT(output_indexbuf_format);
   }

   /* Viewport state. */
   struct si_small_prim_cull_info cull_info;
   si_get_small_prim_cull_info(sctx, &cull_info);

   desc[8] = fui(cull_info.scale[0]);
   desc[9] = fui(cull_info.scale[1]);
   desc[10] = fui(cull_info.translate[0]);
   desc[11] = fui(cull_info.translate[1]);

   /* Set user data SGPRs. */
   /* This can't be greater than 14 if we want the fastest launch rate. */
   unsigned user_sgprs = 13;

   uint64_t index_buffers_va = indexbuf_desc->gpu_address + indexbuf_desc_offset;
   unsigned vs_const_desc = si_const_and_shader_buffer_descriptors_idx(PIPE_SHADER_VERTEX);
   unsigned vs_sampler_desc = si_sampler_and_image_descriptors_idx(PIPE_SHADER_VERTEX);
   uint64_t vs_const_desc_va = sctx->descriptors[vs_const_desc].gpu_address;
   uint64_t vs_sampler_desc_va = sctx->descriptors[vs_sampler_desc].gpu_address;
   uint64_t vb_desc_va = sctx->vb_descriptors_buffer
                            ? sctx->vb_descriptors_buffer->gpu_address + sctx->vb_descriptors_offset
                            : 0;
   unsigned gds_offset, gds_size;
   struct si_fast_udiv_info32 num_prims_udiv = {};

   if (info->instance_count > 1)
      num_prims_udiv = si_compute_fast_udiv_info32(num_prims_per_instance, 31);

   /* Limitations on how these two are packed in the user SGPR. */
   assert(num_prims_udiv.post_shift < 32);
   assert(num_prims_per_instance < 1 << 27);

   si_resource_reference(&indexbuf_desc, NULL);

   bool primitive_restart = sctx->cs_prim_discard_state.current->key.opt.cs_primitive_restart;

   if (VERTEX_COUNTER_GDS_MODE == 1) {
      gds_offset = sctx->compute_gds_offset;
      gds_size = primitive_restart ? 8 : 4;
      sctx->compute_gds_offset += gds_size;

      /* Reset the counters in GDS for the first dispatch using WRITE_DATA.
       * The remainder of the GDS will be cleared after the dispatch packet
       * in parallel with compute shaders.
       */
      if (first_dispatch) {
         radeon_begin(cs);
         radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 2 + gds_size / 4, 0));
         radeon_emit(cs, S_370_DST_SEL(V_370_GDS) | S_370_WR_CONFIRM(1));
         radeon_emit(cs, gds_offset);
         radeon_emit(cs, 0);
         radeon_emit(cs, 0); /* value to write */
         if (gds_size == 8)
            radeon_emit(cs, 0);
         radeon_end();
      }
   }

   /* Set shader registers. */
   struct si_shader *shader = sctx->cs_prim_discard_state.current;

   if (shader != sctx->compute_ib_last_shader) {
      radeon_add_to_buffer_list(sctx, gfx_cs, shader->bo, RADEON_USAGE_READ,
                                RADEON_PRIO_SHADER_BINARY);
      uint64_t shader_va = shader->bo->gpu_address;

      assert(shader->config.scratch_bytes_per_wave == 0);
      assert(shader->config.num_vgprs * WAVES_PER_TG <= 256 * 4);

      radeon_begin(cs);
      radeon_set_sh_reg_seq(cs, R_00B830_COMPUTE_PGM_LO, 2);
      radeon_emit(cs, shader_va >> 8);
      radeon_emit(cs, S_00B834_DATA(shader_va >> 40));

      radeon_set_sh_reg_seq(cs, R_00B848_COMPUTE_PGM_RSRC1, 2);
      radeon_emit(
         cs, S_00B848_VGPRS((shader->config.num_vgprs - 1) / 4) |
                S_00B848_SGPRS(sctx->chip_class <= GFX9 ? (shader->config.num_sgprs - 1) / 8 : 0) |
                S_00B848_FLOAT_MODE(shader->config.float_mode) | S_00B848_DX10_CLAMP(1) |
                S_00B848_MEM_ORDERED(sctx->chip_class >= GFX10) |
                S_00B848_WGP_MODE(sctx->chip_class >= GFX10));
      radeon_emit(cs, S_00B84C_SCRATCH_EN(0 /* no scratch */) | S_00B84C_USER_SGPR(user_sgprs) |
                         S_00B84C_TGID_X_EN(1 /* only blockID.x is used */) |
                         S_00B84C_TG_SIZE_EN(VERTEX_COUNTER_GDS_MODE == 2 /* need the wave ID */) |
                         S_00B84C_TIDIG_COMP_CNT(0 /* only threadID.x is used */) |
                         S_00B84C_LDS_SIZE(shader->config.lds_size));

      radeon_set_sh_reg(cs, R_00B854_COMPUTE_RESOURCE_LIMITS,
                        ac_get_compute_resource_limits(&sctx->screen->info, WAVES_PER_TG,
                                                       MAX_WAVES_PER_SH, THREADGROUPS_PER_CU));
      radeon_end();
      sctx->compute_ib_last_shader = shader;
   }

   STATIC_ASSERT(SPLIT_PRIMS_PACKET_LEVEL % THREADGROUP_SIZE == 0);

   /* Big draw calls are split into smaller dispatches and draw packets. */
   for (unsigned start_prim = 0; start_prim < num_prims; start_prim += SPLIT_PRIMS_PACKET_LEVEL) {
      unsigned num_subdraw_prims;

      if (start_prim + SPLIT_PRIMS_PACKET_LEVEL < num_prims)
         num_subdraw_prims = SPLIT_PRIMS_PACKET_LEVEL;
      else
         num_subdraw_prims = num_prims - start_prim;

      /* Small dispatches are executed back to back until a specific primitive
       * count is reached. Then, a CS_DONE is inserted to signal the gfx IB
       * to start drawing the batch. This batching adds latency to the gfx IB,
       * but CS_DONE and REWIND are too slow.
       */
      if (sctx->compute_num_prims_in_batch + num_subdraw_prims > PRIMS_PER_BATCH)
         si_compute_signal_gfx(sctx);

      if (sctx->compute_num_prims_in_batch == 0) {
         assert((gfx_cs->gpu_address >> 32) == sctx->screen->info.address32_hi);
         sctx->compute_rewind_va = gfx_cs->gpu_address + (gfx_cs->current.cdw + 1) * 4;

         if (sctx->chip_class <= GFX7 || FORCE_REWIND_EMULATION) {
            radeon_begin(gfx_cs);
            radeon_emit(gfx_cs, PKT3(PKT3_NOP, 0, 0));
            radeon_emit(gfx_cs, 0);
            radeon_end();

            si_cp_wait_mem(
               sctx, gfx_cs,
               sctx->compute_rewind_va | (uint64_t)sctx->screen->info.address32_hi << 32,
               REWIND_SIGNAL_BIT, REWIND_SIGNAL_BIT, WAIT_REG_MEM_EQUAL | WAIT_REG_MEM_PFP);

            /* Use INDIRECT_BUFFER to chain to a different buffer
             * to discard the CP prefetch cache.
             */
            sctx->ws->cs_check_space(gfx_cs, 0, true);
         } else {
            radeon_begin(gfx_cs);
            radeon_emit(gfx_cs, PKT3(PKT3_REWIND, 0, 0));
            radeon_emit(gfx_cs, 0);
            radeon_end();
         }
      }

      sctx->compute_num_prims_in_batch += num_subdraw_prims;

      uint32_t count_va = gfx_cs->gpu_address + (gfx_cs->current.cdw + 4) * 4;
      uint64_t index_va = out_indexbuf_va + start_prim * 12;

      /* Emit the draw packet into the gfx IB. */
      radeon_begin(gfx_cs);
      radeon_emit(gfx_cs, PKT3(PKT3_DRAW_INDEX_2, 4, 0));
      radeon_emit(gfx_cs, num_prims * vertices_per_prim);
      radeon_emit(gfx_cs, index_va);
      radeon_emit(gfx_cs, index_va >> 32);
      radeon_emit(gfx_cs, 0);
      radeon_emit(gfx_cs, V_0287F0_DI_SRC_SEL_DMA);
      radeon_end();

      radeon_begin_again(cs);

      /* Continue with the compute IB. */
      if (start_prim == 0) {
         uint32_t gds_prim_restart_continue_bit = 0;

         if (sctx->preserve_prim_restart_gds_at_flush) {
            assert(primitive_restart && info->mode == PIPE_PRIM_TRIANGLE_STRIP);
            assert(start_prim < 1 << 31);
            gds_prim_restart_continue_bit = 1 << 31;
         }

         radeon_set_sh_reg_seq(cs, R_00B900_COMPUTE_USER_DATA_0, user_sgprs);
         radeon_emit(cs, index_buffers_va);
         radeon_emit(cs, VERTEX_COUNTER_GDS_MODE == 0
                            ? count_va
                            : VERTEX_COUNTER_GDS_MODE == 1
                                 ? gds_offset
                                 : start_prim | gds_prim_restart_continue_bit);
         radeon_emit(cs, start_prim + num_subdraw_prims - 1);
         radeon_emit(cs, count_va);
         radeon_emit(cs, vb_desc_va);
         radeon_emit(cs, vs_const_desc_va);
         radeon_emit(cs, vs_sampler_desc_va);
         radeon_emit(cs, base_vertex);
         radeon_emit(cs, info->start_instance);
         radeon_emit(cs, num_prims_udiv.multiplier);
         radeon_emit(cs, num_prims_udiv.post_shift | (num_prims_per_instance << 5));
         radeon_emit(cs, info->restart_index);
         /* small-prim culling precision (same as rasterizer precision = QUANT_MODE) */
         radeon_emit(cs, fui(cull_info.small_prim_precision));
      } else {
         assert(VERTEX_COUNTER_GDS_MODE == 2);
         /* Only update the SGPRs that changed. */
         radeon_set_sh_reg_seq(cs, R_00B904_COMPUTE_USER_DATA_1, 3);
         radeon_emit(cs, start_prim);
         radeon_emit(cs, start_prim + num_subdraw_prims - 1);
         radeon_emit(cs, count_va);
      }

      /* Set grid dimensions. */
      unsigned start_block = start_prim / THREADGROUP_SIZE;
      unsigned num_full_blocks = num_subdraw_prims / THREADGROUP_SIZE;
      unsigned partial_block_size = num_subdraw_prims % THREADGROUP_SIZE;

      radeon_set_sh_reg(cs, R_00B810_COMPUTE_START_X, start_block);
      radeon_set_sh_reg(cs, R_00B81C_COMPUTE_NUM_THREAD_X,
                        S_00B81C_NUM_THREAD_FULL(THREADGROUP_SIZE) |
                           S_00B81C_NUM_THREAD_PARTIAL(partial_block_size));

      radeon_emit(cs, PKT3(PKT3_DISPATCH_DIRECT, 3, 0) | PKT3_SHADER_TYPE_S(1));
      radeon_emit(cs, start_block + num_full_blocks + !!partial_block_size);
      radeon_emit(cs, 1);
      radeon_emit(cs, 1);
      radeon_emit(cs, S_00B800_COMPUTE_SHADER_EN(1) | S_00B800_PARTIAL_TG_EN(!!partial_block_size) |
                         S_00B800_ORDERED_APPEND_ENBL(VERTEX_COUNTER_GDS_MODE == 2) |
                         S_00B800_ORDER_MODE(0 /* launch in order */));
      radeon_end();

      /* This is only for unordered append. Ordered append writes this from
       * the shader.
       *
       * Note that EOP and EOS events are super slow, so emulating the event
       * in a shader is an important optimization.
       */
      if (VERTEX_COUNTER_GDS_MODE == 1) {
         si_cp_release_mem(sctx, cs, V_028A90_CS_DONE, 0,
                           sctx->chip_class <= GFX8 ? EOP_DST_SEL_MEM : EOP_DST_SEL_TC_L2,
                           EOP_INT_SEL_NONE, EOP_DATA_SEL_GDS, NULL,
                           count_va | ((uint64_t)sctx->screen->info.address32_hi << 32),
                           EOP_DATA_GDS(gds_offset / 4, 1), SI_NOT_QUERY);

         /* Now that compute shaders are running, clear the remainder of GDS. */
         if (first_dispatch) {
            unsigned offset = gds_offset + gds_size;
            si_cp_dma_clear_buffer(
               sctx, cs, NULL, offset, GDS_SIZE_UNORDERED - offset, 0,
               SI_CPDMA_SKIP_CHECK_CS_SPACE | SI_CPDMA_SKIP_GFX_SYNC | SI_CPDMA_SKIP_SYNC_BEFORE,
               SI_COHERENCY_NONE, L2_BYPASS);
         }
      }
      first_dispatch = false;

      assert(cs->current.cdw <= cs->current.max_dw);
      assert(gfx_cs->current.cdw <= gfx_cs->current.max_dw);
   }
}
