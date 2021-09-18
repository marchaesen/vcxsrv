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
#include "ac_exp_param.h"

static LLVMValueRef unpack_sint16(struct si_shader_context *ctx, LLVMValueRef i32, unsigned index)
{
   assert(index <= 1);

   if (index == 1)
      return LLVMBuildAShr(ctx->ac.builder, i32, LLVMConstInt(ctx->ac.i32, 16, 0), "");

   return LLVMBuildSExt(ctx->ac.builder, LLVMBuildTrunc(ctx->ac.builder, i32, ctx->ac.i16, ""),
                        ctx->ac.i32, "");
}

static void load_input_vs(struct si_shader_context *ctx, unsigned input_index, LLVMValueRef out[4])
{
   const struct si_shader_info *info = &ctx->shader->selector->info;
   unsigned vs_blit_property = info->base.vs.blit_sgprs_amd;

   if (vs_blit_property) {
      LLVMValueRef vertex_id = ctx->abi.vertex_id;
      LLVMValueRef sel_x1 =
         LLVMBuildICmp(ctx->ac.builder, LLVMIntULE, vertex_id, ctx->ac.i32_1, "");
      /* Use LLVMIntNE, because we have 3 vertices and only
       * the middle one should use y2.
       */
      LLVMValueRef sel_y1 = LLVMBuildICmp(ctx->ac.builder, LLVMIntNE, vertex_id, ctx->ac.i32_1, "");

      unsigned param_vs_blit_inputs = ctx->vs_blit_inputs.arg_index;
      if (input_index == 0) {
         /* Position: */
         LLVMValueRef x1y1 = LLVMGetParam(ctx->main_fn, param_vs_blit_inputs);
         LLVMValueRef x2y2 = LLVMGetParam(ctx->main_fn, param_vs_blit_inputs + 1);

         LLVMValueRef x1 = unpack_sint16(ctx, x1y1, 0);
         LLVMValueRef y1 = unpack_sint16(ctx, x1y1, 1);
         LLVMValueRef x2 = unpack_sint16(ctx, x2y2, 0);
         LLVMValueRef y2 = unpack_sint16(ctx, x2y2, 1);

         LLVMValueRef x = LLVMBuildSelect(ctx->ac.builder, sel_x1, x1, x2, "");
         LLVMValueRef y = LLVMBuildSelect(ctx->ac.builder, sel_y1, y1, y2, "");

         out[0] = LLVMBuildSIToFP(ctx->ac.builder, x, ctx->ac.f32, "");
         out[1] = LLVMBuildSIToFP(ctx->ac.builder, y, ctx->ac.f32, "");
         out[2] = LLVMGetParam(ctx->main_fn, param_vs_blit_inputs + 2);
         out[3] = ctx->ac.f32_1;
         return;
      }

      /* Color or texture coordinates: */
      assert(input_index == 1);

      if (vs_blit_property == SI_VS_BLIT_SGPRS_POS_COLOR) {
         for (int i = 0; i < 4; i++) {
            out[i] = LLVMGetParam(ctx->main_fn, param_vs_blit_inputs + 3 + i);
         }
      } else {
         assert(vs_blit_property == SI_VS_BLIT_SGPRS_POS_TEXCOORD);
         LLVMValueRef x1 = LLVMGetParam(ctx->main_fn, param_vs_blit_inputs + 3);
         LLVMValueRef y1 = LLVMGetParam(ctx->main_fn, param_vs_blit_inputs + 4);
         LLVMValueRef x2 = LLVMGetParam(ctx->main_fn, param_vs_blit_inputs + 5);
         LLVMValueRef y2 = LLVMGetParam(ctx->main_fn, param_vs_blit_inputs + 6);

         out[0] = LLVMBuildSelect(ctx->ac.builder, sel_x1, x1, x2, "");
         out[1] = LLVMBuildSelect(ctx->ac.builder, sel_y1, y1, y2, "");
         out[2] = LLVMGetParam(ctx->main_fn, param_vs_blit_inputs + 7);
         out[3] = LLVMGetParam(ctx->main_fn, param_vs_blit_inputs + 8);
      }
      return;
   }

   /* Set can_speculate=false to help keep all loads grouped together
    * for better latency hiding. If it was true, LLVM could move the loads forward
    * and accidentally double memory latency by doing:
    *
    *    buffer_load_dword_xyzw
    *    s_waitcnt vmcnt(0)
    *    buffer_load_dword_xyzw
    *    s_waitcnt vmcnt(0)
    *
    * ... which is what we must prevent at all cost.
    */
   const bool can_speculate = false;
   unsigned bit_size = info->input[input_index].fp16_lo_hi_valid & 0x1 ? 16 : 32;
   LLVMTypeRef int_type = bit_size == 16 ? ctx->ac.i16 : ctx->ac.i32;
   LLVMTypeRef float_type = bit_size == 16 ? ctx->ac.f16 : ctx->ac.f32;
   unsigned num_vbos_in_user_sgprs = ctx->shader->selector->num_vbos_in_user_sgprs;
   union si_vs_fix_fetch fix_fetch;
   LLVMValueRef vb_desc;
   LLVMValueRef vertex_index;
   LLVMValueRef tmp;

   if (input_index < num_vbos_in_user_sgprs) {
      vb_desc = ac_get_arg(&ctx->ac, ctx->vb_descriptors[input_index]);
   } else {
      unsigned index = input_index - num_vbos_in_user_sgprs;
      vb_desc = ac_build_load_to_sgpr(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args.vertex_buffers),
                                      LLVMConstInt(ctx->ac.i32, index, 0));
   }

   vertex_index = LLVMGetParam(ctx->main_fn, ctx->vertex_index0.arg_index + input_index);

   /* Use the open-coded implementation for all loads of doubles and
    * of dword-sized data that needs fixups. We need to insert conversion
    * code anyway, and the amd/common code does it for us.
    */
   bool opencode = ctx->shader->key.mono.vs_fetch_opencode & (1 << input_index);
   fix_fetch.bits = ctx->shader->key.mono.vs_fix_fetch[input_index].bits;
   if (opencode || (fix_fetch.u.log_size == 3 && fix_fetch.u.format == AC_FETCH_FORMAT_FLOAT) ||
       (fix_fetch.u.log_size == 2)) {
      tmp = ac_build_opencoded_load_format(&ctx->ac, fix_fetch.u.log_size,
                                           fix_fetch.u.num_channels_m1 + 1, fix_fetch.u.format,
                                           fix_fetch.u.reverse, !opencode, vb_desc, vertex_index,
                                           ctx->ac.i32_0, ctx->ac.i32_0, 0, can_speculate);
      for (unsigned i = 0; i < 4; ++i)
         out[i] =
            LLVMBuildExtractElement(ctx->ac.builder, tmp, LLVMConstInt(ctx->ac.i32, i, false), "");

      if (bit_size == 16) {
         if (fix_fetch.u.format == AC_FETCH_FORMAT_UINT ||
             fix_fetch.u.format == AC_FETCH_FORMAT_SINT) {
            for (unsigned i = 0; i < 4; i++)
               out[i] = LLVMBuildTrunc(ctx->ac.builder, out[i], ctx->ac.i16, "");
         } else {
            for (unsigned i = 0; i < 4; i++) {
               out[i] = ac_to_float(&ctx->ac, out[i]);
               out[i] = LLVMBuildFPTrunc(ctx->ac.builder, out[i], ctx->ac.f16, "");
            }
         }
      }
      return;
   }

   unsigned required_channels = util_last_bit(info->input[input_index].usage_mask);
   if (required_channels == 0) {
      for (unsigned i = 0; i < 4; ++i)
         out[i] = LLVMGetUndef(ctx->ac.f32);
      return;
   }

   /* Do multiple loads for special formats. */
   LLVMValueRef fetches[4];
   unsigned num_fetches;
   unsigned fetch_stride;
   unsigned channels_per_fetch;

   if (fix_fetch.u.log_size <= 1 && fix_fetch.u.num_channels_m1 == 2) {
      num_fetches = MIN2(required_channels, 3);
      fetch_stride = 1 << fix_fetch.u.log_size;
      channels_per_fetch = 1;
   } else {
      num_fetches = 1;
      fetch_stride = 0;
      channels_per_fetch = required_channels;
   }

   for (unsigned i = 0; i < num_fetches; ++i) {
      LLVMValueRef voffset = LLVMConstInt(ctx->ac.i32, fetch_stride * i, 0);
      fetches[i] = ac_build_buffer_load_format(&ctx->ac, vb_desc, vertex_index, voffset,
                                               channels_per_fetch, 0, can_speculate,
                                               bit_size == 16, false);
   }

   if (num_fetches == 1 && channels_per_fetch > 1) {
      LLVMValueRef fetch = fetches[0];
      for (unsigned i = 0; i < channels_per_fetch; ++i) {
         tmp = LLVMConstInt(ctx->ac.i32, i, false);
         fetches[i] = LLVMBuildExtractElement(ctx->ac.builder, fetch, tmp, "");
      }
      num_fetches = channels_per_fetch;
      channels_per_fetch = 1;
   }

   for (unsigned i = num_fetches; i < 4; ++i)
      fetches[i] = LLVMGetUndef(float_type);

   if (fix_fetch.u.log_size <= 1 && fix_fetch.u.num_channels_m1 == 2 && required_channels == 4) {
      if (fix_fetch.u.format == AC_FETCH_FORMAT_UINT || fix_fetch.u.format == AC_FETCH_FORMAT_SINT)
         fetches[3] = LLVMConstInt(int_type, 1, 0);
      else
         fetches[3] = LLVMConstReal(float_type, 1);
   } else if (fix_fetch.u.log_size == 3 &&
              (fix_fetch.u.format == AC_FETCH_FORMAT_SNORM ||
               fix_fetch.u.format == AC_FETCH_FORMAT_SSCALED ||
               fix_fetch.u.format == AC_FETCH_FORMAT_SINT) &&
              required_channels == 4) {

      /* For 2_10_10_10, the hardware returns an unsigned value;
       * convert it to a signed one.
       */
      LLVMValueRef tmp = fetches[3];
      LLVMValueRef c30 = LLVMConstInt(int_type, 30, 0);

      /* First, recover the sign-extended signed integer value. */
      if (fix_fetch.u.format == AC_FETCH_FORMAT_SSCALED)
         tmp = LLVMBuildFPToUI(ctx->ac.builder, tmp, int_type, "");
      else
         tmp = ac_to_integer(&ctx->ac, tmp);

      /* For the integer-like cases, do a natural sign extension.
       *
       * For the SNORM case, the values are 0.0, 0.333, 0.666, 1.0
       * and happen to contain 0, 1, 2, 3 as the two LSBs of the
       * exponent.
       */
      tmp = LLVMBuildShl(
         ctx->ac.builder, tmp,
         fix_fetch.u.format == AC_FETCH_FORMAT_SNORM ? LLVMConstInt(int_type, 7, 0) : c30, "");
      tmp = LLVMBuildAShr(ctx->ac.builder, tmp, c30, "");

      /* Convert back to the right type. */
      if (fix_fetch.u.format == AC_FETCH_FORMAT_SNORM) {
         LLVMValueRef clamp;
         LLVMValueRef neg_one = LLVMConstReal(float_type, -1.0);
         tmp = LLVMBuildSIToFP(ctx->ac.builder, tmp, float_type, "");
         clamp = LLVMBuildFCmp(ctx->ac.builder, LLVMRealULT, tmp, neg_one, "");
         tmp = LLVMBuildSelect(ctx->ac.builder, clamp, neg_one, tmp, "");
      } else if (fix_fetch.u.format == AC_FETCH_FORMAT_SSCALED) {
         tmp = LLVMBuildSIToFP(ctx->ac.builder, tmp, float_type, "");
      }

      fetches[3] = tmp;
   }

   for (unsigned i = 0; i < 4; ++i)
      out[i] = ac_to_float(&ctx->ac, fetches[i]);
}

static LLVMValueRef si_load_vs_input(struct ac_shader_abi *abi, unsigned driver_location,
                                     unsigned component, unsigned num_components,
                                     unsigned vertex_index, LLVMTypeRef type)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   LLVMValueRef values[4];

   load_input_vs(ctx, driver_location, values);

   for (unsigned i = 0; i < 4; i++)
      values[i] = LLVMBuildBitCast(ctx->ac.builder, values[i], type, "");

   return ac_build_varying_gather_values(&ctx->ac, values, num_components, component);
}

void si_llvm_streamout_store_output(struct si_shader_context *ctx, LLVMValueRef const *so_buffers,
                                    LLVMValueRef const *so_write_offsets,
                                    struct pipe_stream_output *stream_out,
                                    struct si_shader_output_values *shader_out)
{
   unsigned buf_idx = stream_out->output_buffer;
   unsigned start = stream_out->start_component;
   unsigned num_comps = stream_out->num_components;
   LLVMValueRef out[4];

   assert(num_comps && num_comps <= 4);
   if (!num_comps || num_comps > 4)
      return;

   /* Load the output as int. */
   for (int j = 0; j < num_comps; j++) {
      assert(stream_out->stream == shader_out->vertex_stream[start + j]);

      out[j] = ac_to_integer(&ctx->ac, shader_out->values[start + j]);
   }

   /* Pack the output. */
   LLVMValueRef vdata = NULL;

   switch (num_comps) {
   case 1: /* as i32 */
      vdata = out[0];
      break;
   case 2: /* as v2i32 */
   case 3: /* as v3i32 */
      if (ac_has_vec3_support(ctx->screen->info.chip_class, false)) {
         vdata = ac_build_gather_values(&ctx->ac, out, num_comps);
         break;
      }
      /* as v4i32 (aligned to 4) */
      out[3] = LLVMGetUndef(ctx->ac.i32);
      FALLTHROUGH;
   case 4: /* as v4i32 */
      vdata = ac_build_gather_values(&ctx->ac, out, util_next_power_of_two(num_comps));
      break;
   }

   ac_build_buffer_store_dword(&ctx->ac, so_buffers[buf_idx], vdata, num_comps,
                               so_write_offsets[buf_idx], ctx->ac.i32_0, stream_out->dst_offset * 4,
                               ac_glc | ac_slc);
}

/**
 * Write streamout data to buffers for vertex stream @p stream (different
 * vertex streams can occur for GS copy shaders).
 */
void si_llvm_emit_streamout(struct si_shader_context *ctx, struct si_shader_output_values *outputs,
                            unsigned noutput, unsigned stream)
{
   struct si_shader_selector *sel = ctx->shader->selector;
   struct pipe_stream_output_info *so = &sel->so;
   LLVMBuilderRef builder = ctx->ac.builder;
   int i;

   /* Get bits [22:16], i.e. (so_param >> 16) & 127; */
   LLVMValueRef so_vtx_count = si_unpack_param(ctx, ctx->args.streamout_config, 16, 7);

   LLVMValueRef tid = ac_get_thread_id(&ctx->ac);

   /* can_emit = tid < so_vtx_count; */
   LLVMValueRef can_emit = LLVMBuildICmp(builder, LLVMIntULT, tid, so_vtx_count, "");

   /* Emit the streamout code conditionally. This actually avoids
    * out-of-bounds buffer access. The hw tells us via the SGPR
    * (so_vtx_count) which threads are allowed to emit streamout data. */
   ac_build_ifcc(&ctx->ac, can_emit, 6501);
   {
      /* The buffer offset is computed as follows:
       *   ByteOffset = streamout_offset[buffer_id]*4 +
       *                (streamout_write_index + thread_id)*stride[buffer_id] +
       *                attrib_offset
       */

      LLVMValueRef so_write_index = ac_get_arg(&ctx->ac, ctx->args.streamout_write_index);

      /* Compute (streamout_write_index + thread_id). */
      so_write_index = LLVMBuildAdd(builder, so_write_index, tid, "");

      /* Load the descriptor and compute the write offset for each
       * enabled buffer. */
      LLVMValueRef so_write_offset[4] = {};
      LLVMValueRef so_buffers[4];
      LLVMValueRef buf_ptr = ac_get_arg(&ctx->ac, ctx->internal_bindings);

      for (i = 0; i < 4; i++) {
         if (!so->stride[i])
            continue;

         LLVMValueRef offset = LLVMConstInt(ctx->ac.i32, SI_VS_STREAMOUT_BUF0 + i, 0);

         so_buffers[i] = ac_build_load_to_sgpr(&ctx->ac, buf_ptr, offset);

         LLVMValueRef so_offset = ac_get_arg(&ctx->ac, ctx->args.streamout_offset[i]);
         so_offset = LLVMBuildMul(builder, so_offset, LLVMConstInt(ctx->ac.i32, 4, 0), "");

         so_write_offset[i] = ac_build_imad(
            &ctx->ac, so_write_index, LLVMConstInt(ctx->ac.i32, so->stride[i] * 4, 0), so_offset);
      }

      /* Write streamout data. */
      for (i = 0; i < so->num_outputs; i++) {
         unsigned reg = so->output[i].register_index;

         if (reg >= noutput)
            continue;

         if (stream != so->output[i].stream)
            continue;

         si_llvm_streamout_store_output(ctx, so_buffers, so_write_offset, &so->output[i],
                                        &outputs[reg]);
      }
   }
   ac_build_endif(&ctx->ac, 6501);
}

static void si_llvm_emit_clipvertex(struct si_shader_context *ctx, struct ac_export_args *pos,
                                    LLVMValueRef *out_elts)
{
   unsigned reg_index;
   unsigned chan;
   unsigned const_chan;
   LLVMValueRef base_elt;
   LLVMValueRef ptr = ac_get_arg(&ctx->ac, ctx->internal_bindings);
   LLVMValueRef constbuf_index = LLVMConstInt(ctx->ac.i32, SI_VS_CONST_CLIP_PLANES, 0);
   LLVMValueRef const_resource = ac_build_load_to_sgpr(&ctx->ac, ptr, constbuf_index);
   unsigned clipdist_mask = ctx->shader->selector->clipdist_mask &
                            ~ctx->shader->key.opt.kill_clip_distances;

   for (reg_index = 0; reg_index < 2; reg_index++) {
      struct ac_export_args *args = &pos[2 + reg_index];

      if (!(clipdist_mask & BITFIELD_RANGE(reg_index * 4, 4)))
         continue;

      args->out[0] = args->out[1] = args->out[2] = args->out[3] = LLVMGetUndef(ctx->ac.f32);

      /* Compute dot products of position and user clip plane vectors */
      for (chan = 0; chan < 4; chan++) {
         if (!(clipdist_mask & BITFIELD_BIT(reg_index * 4 + chan)))
            continue;

         for (const_chan = 0; const_chan < 4; const_chan++) {
            LLVMValueRef addr =
               LLVMConstInt(ctx->ac.i32, ((reg_index * 4 + chan) * 4 + const_chan) * 4, 0);
            base_elt = si_buffer_load_const(ctx, const_resource, addr);
            args->out[chan] =
               ac_build_fmad(&ctx->ac, base_elt, out_elts[const_chan],
                             const_chan == 0 ? ctx->ac.f32_0 : args->out[chan]);
         }
      }

      args->enabled_channels = 0xf;
      args->valid_mask = 0;
      args->done = 0;
      args->target = V_008DFC_SQ_EXP_POS + 2 + reg_index;
      args->compr = 0;
   }
}

/* Initialize arguments for the shader export intrinsic */
static void si_llvm_init_vs_export_args(struct si_shader_context *ctx, const LLVMValueRef *values,
                                        unsigned target, struct ac_export_args *args)
{
   args->enabled_channels = 0xf; /* writemask - default is 0xf */
   args->valid_mask = 0;         /* Specify whether the EXEC mask represents the valid mask */
   args->done = 0;               /* Specify whether this is the last export */
   args->target = target;        /* Specify the target we are exporting */
   args->compr = false;

   memcpy(&args->out[0], values, sizeof(values[0]) * 4);
}

static void si_prepare_param_exports(struct si_shader_context *ctx,
                                     const struct si_shader_output_values *outputs, unsigned noutput,
                                     struct ac_export_args exports[32])
{
   struct si_shader *shader = ctx->shader;
   unsigned param_count = 0;

   memset(shader->info.vs_output_param_offset, AC_EXP_PARAM_DEFAULT_VAL_0000,
          sizeof(shader->info.vs_output_param_offset));

   for (unsigned i = 0; i < noutput; i++) {
      unsigned semantic = outputs[i].semantic;

      if (outputs[i].vertex_stream[0] != 0 && outputs[i].vertex_stream[1] != 0 &&
          outputs[i].vertex_stream[2] != 0 && outputs[i].vertex_stream[3] != 0)
         continue;

      switch (semantic) {
      case VARYING_SLOT_LAYER:
      case VARYING_SLOT_VIEWPORT:
      case VARYING_SLOT_CLIP_DIST0:
      case VARYING_SLOT_CLIP_DIST1:
      case VARYING_SLOT_COL0:
      case VARYING_SLOT_COL1:
      case VARYING_SLOT_BFC0:
      case VARYING_SLOT_BFC1:
      case VARYING_SLOT_PRIMITIVE_ID:
      case VARYING_SLOT_FOGC:
         break;
      default:
         if ((semantic >= VARYING_SLOT_TEX0 && semantic <= VARYING_SLOT_TEX7) ||
             semantic >= VARYING_SLOT_VAR0)
            break;
         else
            continue;
      }

      if ((semantic <= VARYING_SLOT_VAR31 || semantic >= VARYING_SLOT_VAR0_16BIT) &&
          shader->key.opt.kill_outputs &
             (1ull << si_shader_io_get_unique_index(semantic, true)))
         continue;

      si_llvm_init_vs_export_args(ctx, outputs[i].values, V_008DFC_SQ_EXP_PARAM + param_count,
                                  &exports[param_count]);

      assert(i < ARRAY_SIZE(shader->info.vs_output_param_offset));
      shader->info.vs_output_param_offset[i] = param_count++;
   }

   shader->info.nr_param_exports = param_count;
}

/**
 * Vertex color clamping.
 *
 * This uses a state constant loaded in a user data SGPR and
 * an IF statement is added that clamps all colors if the constant
 * is true.
 */
static void si_vertex_color_clamping(struct si_shader_context *ctx,
                                     struct si_shader_output_values *outputs, unsigned noutput)
{
   LLVMValueRef addr[SI_MAX_VS_OUTPUTS][4];
   bool has_colors = false;

   /* Store original colors to alloca variables. */
   for (unsigned i = 0; i < noutput; i++) {
      if (outputs[i].semantic != VARYING_SLOT_COL0 &&
          outputs[i].semantic != VARYING_SLOT_COL1 &&
          outputs[i].semantic != VARYING_SLOT_BFC0 &&
          outputs[i].semantic != VARYING_SLOT_BFC1)
         continue;

      for (unsigned j = 0; j < 4; j++)
         addr[i][j] = ac_build_alloca_init(&ctx->ac, outputs[i].values[j], "");

      has_colors = true;
   }

   if (!has_colors)
      return;

   /* The state is in the first bit of the user SGPR. */
   LLVMValueRef cond = ac_get_arg(&ctx->ac, ctx->vs_state_bits);
   cond = LLVMBuildTrunc(ctx->ac.builder, cond, ctx->ac.i1, "");

   ac_build_ifcc(&ctx->ac, cond, 6502);

   /* Store clamped colors to alloca variables within the conditional block. */
   for (unsigned i = 0; i < noutput; i++) {
      if (outputs[i].semantic != VARYING_SLOT_COL0 &&
          outputs[i].semantic != VARYING_SLOT_COL1 &&
          outputs[i].semantic != VARYING_SLOT_BFC0 &&
          outputs[i].semantic != VARYING_SLOT_BFC1)
         continue;

      for (unsigned j = 0; j < 4; j++) {
         LLVMBuildStore(ctx->ac.builder, ac_build_clamp(&ctx->ac, outputs[i].values[j]),
                        addr[i][j]);
      }
   }
   ac_build_endif(&ctx->ac, 6502);

   /* Load clamped colors */
   for (unsigned i = 0; i < noutput; i++) {
      if (outputs[i].semantic != VARYING_SLOT_COL0 &&
          outputs[i].semantic != VARYING_SLOT_COL1 &&
          outputs[i].semantic != VARYING_SLOT_BFC0 &&
          outputs[i].semantic != VARYING_SLOT_BFC1)
         continue;

      for (unsigned j = 0; j < 4; j++) {
         outputs[i].values[j] = LLVMBuildLoad(ctx->ac.builder, addr[i][j], "");
      }
   }
}

/* Generate export instructions for hardware VS shader stage or NGG GS stage
 * (position and parameter data only).
 */
void si_llvm_build_vs_exports(struct si_shader_context *ctx,
                              struct si_shader_output_values *outputs, unsigned noutput)
{
   struct si_shader *shader = ctx->shader;
   struct ac_export_args pos_args[4] = {};
   LLVMValueRef psize_value = NULL, edgeflag_value = NULL, layer_value = NULL,
                viewport_index_value = NULL;
   unsigned pos_idx, index;
   unsigned clipdist_mask = (shader->selector->clipdist_mask &
                             ~shader->key.opt.kill_clip_distances) |
                            shader->selector->culldist_mask;
   int i;

   si_vertex_color_clamping(ctx, outputs, noutput);

   struct ac_export_args param_exports[32];
   si_prepare_param_exports(ctx, outputs, noutput, param_exports);

   /* Build position exports. */
   for (i = 0; i < noutput; i++) {
      switch (outputs[i].semantic) {
      case VARYING_SLOT_POS:
         si_llvm_init_vs_export_args(ctx, outputs[i].values, V_008DFC_SQ_EXP_POS, &pos_args[0]);
         break;
      case VARYING_SLOT_PSIZ:
         psize_value = outputs[i].values[0];
         break;
      case VARYING_SLOT_LAYER:
         layer_value = outputs[i].values[0];
         break;
      case VARYING_SLOT_VIEWPORT:
         viewport_index_value = outputs[i].values[0];
         break;
      case VARYING_SLOT_EDGE:
         edgeflag_value = outputs[i].values[0];
         break;
      case VARYING_SLOT_CLIP_DIST0:
      case VARYING_SLOT_CLIP_DIST1:
         index = outputs[i].semantic - VARYING_SLOT_CLIP_DIST0;
         if (clipdist_mask & BITFIELD_RANGE(index * 4, 4)) {
            si_llvm_init_vs_export_args(ctx, outputs[i].values, V_008DFC_SQ_EXP_POS + 2 + index,
                                        &pos_args[2 + index]);
         }
         break;
      case VARYING_SLOT_CLIP_VERTEX:
         si_llvm_emit_clipvertex(ctx, pos_args, outputs[i].values);
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

   bool writes_psize = shader->selector->info.writes_psize && !shader->key.opt.kill_pointsize;
   bool pos_writes_edgeflag = shader->selector->info.writes_edgeflag && !shader->key.as_ngg;
   bool writes_vrs = ctx->screen->options.vrs2x2;

   /* Write the misc vector (point size, edgeflag, layer, viewport). */
   if (writes_psize || pos_writes_edgeflag || writes_vrs ||
       shader->selector->info.writes_viewport_index || shader->selector->info.writes_layer) {
      pos_args[1].enabled_channels = writes_psize |
                                     ((pos_writes_edgeflag | writes_vrs) << 1) |
                                     (shader->selector->info.writes_layer << 2);

      pos_args[1].valid_mask = 0; /* EXEC mask */
      pos_args[1].done = 0;       /* last export? */
      pos_args[1].target = V_008DFC_SQ_EXP_POS + 1;
      pos_args[1].compr = 0;              /* COMPR flag */
      pos_args[1].out[0] = ctx->ac.f32_0; /* X */
      pos_args[1].out[1] = ctx->ac.f32_0; /* Y */
      pos_args[1].out[2] = ctx->ac.f32_0; /* Z */
      pos_args[1].out[3] = ctx->ac.f32_0; /* W */

      if (writes_psize)
         pos_args[1].out[0] = psize_value;

      if (pos_writes_edgeflag) {
         /* The output is a float, but the hw expects an integer
          * with the first bit containing the edge flag. */
         edgeflag_value = LLVMBuildFPToUI(ctx->ac.builder, edgeflag_value, ctx->ac.i32, "");
         edgeflag_value = ac_build_umin(&ctx->ac, edgeflag_value, ctx->ac.i32_1);

         /* The LLVM intrinsic expects a float. */
         pos_args[1].out[1] = ac_to_float(&ctx->ac, edgeflag_value);
      }

      if (writes_vrs) {
         /* Bits [2:3] = VRS rate X
          * Bits [4:5] = VRS rate Y
          *
          * The range is [-2, 1]. Values:
          *   1: 2x coarser shading rate in that direction.
          *   0: normal shading rate
          *  -1: 2x finer shading rate (sample shading, not directional)
          *  -2: 4x finer shading rate (sample shading, not directional)
          *
          * Sample shading can't go above 8 samples, so both numbers can't be -2
          * at the same time.
          */
         LLVMValueRef rates = LLVMConstInt(ctx->ac.i32, (1 << 2) | (1 << 4), 0);

         /* If Pos.W != 1 (typical for non-GUI elements), use 2x2 coarse shading. */
         rates = LLVMBuildSelect(ctx->ac.builder,
                                 LLVMBuildFCmp(ctx->ac.builder, LLVMRealUNE,
                                               pos_args[0].out[3], ctx->ac.f32_1, ""),
                                 rates, ctx->ac.i32_0, "");

         LLVMValueRef v = ac_to_integer(&ctx->ac, pos_args[1].out[1]);
         v = LLVMBuildOr(ctx->ac.builder, v, rates, "");
         pos_args[1].out[1] = ac_to_float(&ctx->ac, v);
      }

      if (ctx->screen->info.chip_class >= GFX9) {
         /* GFX9 has the layer in out.z[10:0] and the viewport
          * index in out.z[19:16].
          */
         if (shader->selector->info.writes_layer)
            pos_args[1].out[2] = layer_value;

         if (shader->selector->info.writes_viewport_index) {
            LLVMValueRef v = viewport_index_value;

            v = ac_to_integer(&ctx->ac, v);
            v = LLVMBuildShl(ctx->ac.builder, v, LLVMConstInt(ctx->ac.i32, 16, 0), "");
            v = LLVMBuildOr(ctx->ac.builder, v, ac_to_integer(&ctx->ac, pos_args[1].out[2]), "");
            pos_args[1].out[2] = ac_to_float(&ctx->ac, v);
            pos_args[1].enabled_channels |= 1 << 2;
         }
      } else {
         if (shader->selector->info.writes_layer)
            pos_args[1].out[2] = layer_value;

         if (shader->selector->info.writes_viewport_index) {
            pos_args[1].out[3] = viewport_index_value;
            pos_args[1].enabled_channels |= 1 << 3;
         }
      }
   }

   for (i = 0; i < 4; i++)
      if (pos_args[i].out[0])
         shader->info.nr_pos_exports++;

   /* GFX10 (Navi1x) skip POS0 exports if EXEC=0 and DONE=0, causing a hang.
    * Setting valid_mask=1 prevents it and has no other effect.
    */
   if (ctx->screen->info.chip_class == GFX10)
      pos_args[0].valid_mask = 1;

   pos_idx = 0;
   for (i = 0; i < 4; i++) {
      if (!pos_args[i].out[0])
         continue;

      /* Specify the target we are exporting */
      pos_args[i].target = V_008DFC_SQ_EXP_POS + pos_idx++;

      if (pos_idx == shader->info.nr_pos_exports) {
         /* Specify that this is the last export */
         pos_args[i].done = 1;

         /* If a shader has no param exports, rasterization can start before
          * the shader finishes and thus memory stores might not finish before
          * the pixel shader starts.
          *
          * VLOAD is for atomics with return.
          */
         if (ctx->screen->info.chip_class >= GFX10 &&
             !shader->info.nr_param_exports &&
             shader->selector->info.base.writes_memory)
            ac_build_waitcnt(&ctx->ac, AC_WAIT_VLOAD | AC_WAIT_VSTORE);
      }

      ac_build_export(&ctx->ac, &pos_args[i]);
   }

   /* Build parameter exports. */
   for (unsigned i = 0; i < shader->info.nr_param_exports; i++)
      ac_build_export(&ctx->ac, &param_exports[i]);
}

void si_llvm_emit_vs_epilogue(struct ac_shader_abi *abi)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   struct si_shader_info *info = &ctx->shader->selector->info;
   struct si_shader_output_values *outputs = NULL;
   LLVMValueRef *addrs = abi->outputs;
   int i, j;

   assert(!ctx->shader->is_gs_copy_shader);
   assert(info->num_outputs <= AC_LLVM_MAX_OUTPUTS);

   outputs = MALLOC((info->num_outputs + 1) * sizeof(outputs[0]));

   for (i = 0; i < info->num_outputs; i++) {
      outputs[i].semantic = info->output_semantic[i];

      for (j = 0; j < 4; j++) {
         outputs[i].values[j] = LLVMBuildLoad(ctx->ac.builder, addrs[4 * i + j], "");
         outputs[i].vertex_stream[j] = (info->output_streams[i] >> (2 * j)) & 3;
      }
   }

   if (!ctx->screen->use_ngg_streamout && ctx->shader->selector->so.num_outputs)
      si_llvm_emit_streamout(ctx, outputs, i, 0);

   /* Export PrimitiveID. */
   if (ctx->shader->key.mono.u.vs_export_prim_id) {
      outputs[i].semantic = VARYING_SLOT_PRIMITIVE_ID;
      outputs[i].values[0] = ac_to_float(&ctx->ac, si_get_primitive_id(ctx, 0));
      for (j = 1; j < 4; j++)
         outputs[i].values[j] = LLVMConstReal(ctx->ac.f32, 0);

      memset(outputs[i].vertex_stream, 0, sizeof(outputs[i].vertex_stream));
      i++;
   }

   si_llvm_build_vs_exports(ctx, outputs, i);
   FREE(outputs);
}

/**
 * Build the vertex shader prolog function.
 *
 * The inputs are the same as VS (a lot of SGPRs and 4 VGPR system values).
 * All inputs are returned unmodified. The vertex load indices are
 * stored after them, which will be used by the API VS for fetching inputs.
 *
 * For example, the expected outputs for instance_divisors[] = {0, 1, 2} are:
 *   input_v0,
 *   input_v1,
 *   input_v2,
 *   input_v3,
 *   (VertexID + BaseVertex),
 *   (InstanceID + StartInstance),
 *   (InstanceID / 2 + StartInstance)
 */
void si_llvm_build_vs_prolog(struct si_shader_context *ctx, union si_shader_part_key *key)
{
   LLVMTypeRef *returns;
   LLVMValueRef ret, func;
   int num_returns, i;
   unsigned first_vs_vgpr = key->vs_prolog.num_merged_next_stage_vgprs;
   unsigned num_input_vgprs =
      key->vs_prolog.num_merged_next_stage_vgprs + 4;
   struct ac_arg input_sgpr_param[key->vs_prolog.num_input_sgprs];
   struct ac_arg input_vgpr_param[10];
   LLVMValueRef input_vgprs[10];
   unsigned num_all_input_regs = key->vs_prolog.num_input_sgprs + num_input_vgprs;
   unsigned user_sgpr_base = key->vs_prolog.num_merged_next_stage_vgprs ? 8 : 0;

   memset(&ctx->args, 0, sizeof(ctx->args));

   /* 4 preloaded VGPRs + vertex load indices as prolog outputs */
   returns = alloca((num_all_input_regs + key->vs_prolog.num_inputs) * sizeof(LLVMTypeRef));
   num_returns = 0;

   /* Declare input and output SGPRs. */
   for (i = 0; i < key->vs_prolog.num_input_sgprs; i++) {
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &input_sgpr_param[i]);
      returns[num_returns++] = ctx->ac.i32;
   }

   struct ac_arg merged_wave_info = input_sgpr_param[3];

   /* Preloaded VGPRs (outputs must be floats) */
   for (i = 0; i < num_input_vgprs; i++) {
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &input_vgpr_param[i]);
      returns[num_returns++] = ctx->ac.f32;
   }

   /* Vertex load indices. */
   for (i = 0; i < key->vs_prolog.num_inputs; i++)
      returns[num_returns++] = ctx->ac.f32;

   /* Create the function. */
   si_llvm_create_func(ctx, "vs_prolog", returns, num_returns, 0);
   func = ctx->main_fn;

   for (i = 0; i < num_input_vgprs; i++) {
      input_vgprs[i] = ac_get_arg(&ctx->ac, input_vgpr_param[i]);
   }

   if (key->vs_prolog.num_merged_next_stage_vgprs) {
      if (!key->vs_prolog.is_monolithic)
         ac_init_exec_full_mask(&ctx->ac);

      if (key->vs_prolog.as_ls && ctx->screen->info.has_ls_vgpr_init_bug) {
         /* If there are no HS threads, SPI loads the LS VGPRs
          * starting at VGPR 0. Shift them back to where they
          * belong.
          */
         LLVMValueRef has_hs_threads =
            LLVMBuildICmp(ctx->ac.builder, LLVMIntNE,
                          si_unpack_param(ctx, input_sgpr_param[3], 8, 8), ctx->ac.i32_0, "");

         for (i = 4; i > 0; --i) {
            input_vgprs[i + 1] = LLVMBuildSelect(ctx->ac.builder, has_hs_threads,
                                                 input_vgprs[i + 1], input_vgprs[i - 1], "");
         }
      }
   }

   /* The culling code stored the LDS addresses of the VGPRs into those VGPRs. Load them. */
   if (key->vs_prolog.load_vgprs_after_culling) {
      for (i = 5; i <= 8; i++) {
         bool is_tes_rel_patch_id = i == 7;
         input_vgprs[i] = LLVMBuildIntToPtr(ctx->ac.builder, input_vgprs[i],
                                            LLVMPointerType(is_tes_rel_patch_id ? ctx->ac.i8 : ctx->ac.i32,
                                                            AC_ADDR_SPACE_LDS), "");
         input_vgprs[i] = LLVMBuildLoad(ctx->ac.builder, input_vgprs[i], "");
         if (is_tes_rel_patch_id)
            input_vgprs[i] = LLVMBuildZExt(ctx->ac.builder, input_vgprs[i], ctx->ac.i32, "");
      }
   }

   if (key->vs_prolog.gs_fast_launch_tri_list || key->vs_prolog.gs_fast_launch_tri_strip) {
      LLVMValueRef wave_id, thread_id_in_tg;

      wave_id = si_unpack_param(ctx, input_sgpr_param[3], 24, 4);
      thread_id_in_tg =
         ac_build_imad(&ctx->ac, wave_id, LLVMConstInt(ctx->ac.i32, ctx->ac.wave_size, false),
                       ac_get_thread_id(&ctx->ac));

      /* The GS fast launch initializes all VGPRs to the value of
       * the first thread, so we have to add the thread ID.
       *
       * Only these are initialized by the hw:
       *   VGPR2: Base Primitive ID
       *   VGPR5: Base Vertex ID
       *   VGPR6: Instance ID
       */

      /* Put the vertex thread IDs into VGPRs as-is instead of packing them.
       * The NGG cull shader will read them from there.
       */
      if (key->vs_prolog.gs_fast_launch_tri_list) {
         input_vgprs[0] = ac_build_imad(&ctx->ac, thread_id_in_tg,       /* gs_vtx01_offset */
                                        LLVMConstInt(ctx->ac.i32, 3, 0), /* Vertex 0 */
                                        LLVMConstInt(ctx->ac.i32, 0, 0));
         input_vgprs[1] = ac_build_imad(&ctx->ac, thread_id_in_tg,       /* gs_vtx23_offset */
                                        LLVMConstInt(ctx->ac.i32, 3, 0), /* Vertex 1 */
                                        LLVMConstInt(ctx->ac.i32, 1, 0));
         input_vgprs[4] = ac_build_imad(&ctx->ac, thread_id_in_tg,       /* gs_vtx45_offset */
                                        LLVMConstInt(ctx->ac.i32, 3, 0), /* Vertex 2 */
                                        LLVMConstInt(ctx->ac.i32, 2, 0));
      } else {
         assert(key->vs_prolog.gs_fast_launch_tri_strip);
         LLVMBuilderRef builder = ctx->ac.builder;
         /* Triangle indices: */
         LLVMValueRef index[3] = {
            thread_id_in_tg,
            LLVMBuildAdd(builder, thread_id_in_tg, LLVMConstInt(ctx->ac.i32, 1, 0), ""),
            LLVMBuildAdd(builder, thread_id_in_tg, LLVMConstInt(ctx->ac.i32, 2, 0), ""),
         };
         LLVMValueRef is_odd = LLVMBuildTrunc(ctx->ac.builder, thread_id_in_tg, ctx->ac.i1, "");
         LLVMValueRef flatshade_first = LLVMBuildICmp(
            builder, LLVMIntEQ,
            si_unpack_param(ctx, input_sgpr_param[8 + SI_SGPR_VS_STATE_BITS], 4, 2),
            ctx->ac.i32_0, "");

         ac_build_triangle_strip_indices_to_triangle(&ctx->ac, is_odd, flatshade_first, index);
         input_vgprs[0] = index[0];
         input_vgprs[1] = index[1];
         input_vgprs[4] = index[2];
      }

      /* Triangles always have all edge flags set initially. */
      input_vgprs[3] = LLVMConstInt(ctx->ac.i32, 0x7 << 8, 0);

      input_vgprs[2] =
         LLVMBuildAdd(ctx->ac.builder, input_vgprs[2], thread_id_in_tg, ""); /* PrimID */
      input_vgprs[5] =
         LLVMBuildAdd(ctx->ac.builder, input_vgprs[5], thread_id_in_tg, ""); /* VertexID */
      input_vgprs[8] = input_vgprs[6];                                       /* InstanceID */

      if (key->vs_prolog.gs_fast_launch_index_size_packed) {
         LLVMTypeRef index_type = ctx->ac.voidt;

         switch (key->vs_prolog.gs_fast_launch_index_size_packed) {
         case 1:
            index_type = ctx->ac.i8;
            break;
         case 2:
            index_type = ctx->ac.i16;
            break;
         case 3:
            index_type = ctx->ac.i32;
            break;
         default:
            unreachable("invalid gs_fast_launch_index_size_packed");
         }

         LLVMValueRef sgprs[2] = {
            ac_get_arg(&ctx->ac, input_sgpr_param[0]),
            ac_get_arg(&ctx->ac, input_sgpr_param[1]),
         };
         LLVMValueRef indices = ac_build_gather_values(&ctx->ac, sgprs, 2);
         indices = LLVMBuildBitCast(ctx->ac.builder, indices, ctx->ac.i64, "");
         indices = LLVMBuildIntToPtr(ctx->ac.builder, indices,
                                     LLVMPointerType(index_type, AC_ADDR_SPACE_CONST), "");

         LLVMValueRef vertex_id = ac_build_alloca_init(&ctx->ac, input_vgprs[5], "");

         /* if (is ES thread...) */
         ac_build_ifcc(&ctx->ac,
                       LLVMBuildICmp(ctx->ac.builder, LLVMIntULT, ac_get_thread_id(&ctx->ac),
                                     si_unpack_param(ctx, merged_wave_info, 0, 8), ""), 0);
         /* VertexID = indexBufferLoad(VertexID); */
         LLVMValueRef index = LLVMBuildGEP(ctx->ac.builder, indices, &input_vgprs[5], 1, "");
         index = LLVMBuildLoad(ctx->ac.builder, index, "");
         index = LLVMBuildZExt(ctx->ac.builder, index, ctx->ac.i32, "");
         LLVMBuildStore(ctx->ac.builder, index, vertex_id);
         ac_build_endif(&ctx->ac, 0);

         input_vgprs[5] = LLVMBuildLoad(ctx->ac.builder, vertex_id, "");
      }
   }

   unsigned vertex_id_vgpr = first_vs_vgpr;
   unsigned instance_id_vgpr = ctx->screen->info.chip_class >= GFX10
                                  ? first_vs_vgpr + 3
                                  : first_vs_vgpr + (key->vs_prolog.as_ls ? 2 : 1);

   ctx->abi.vertex_id = input_vgprs[vertex_id_vgpr];
   ctx->abi.instance_id = input_vgprs[instance_id_vgpr];

   /* Copy inputs to outputs. This should be no-op, as the registers match,
    * but it will prevent the compiler from overwriting them unintentionally.
    */
   ret = ctx->return_value;
   for (i = 0; i < key->vs_prolog.num_input_sgprs; i++) {
      LLVMValueRef p = LLVMGetParam(func, i);
      ret = LLVMBuildInsertValue(ctx->ac.builder, ret, p, i, "");
   }
   for (i = 0; i < num_input_vgprs; i++) {
      LLVMValueRef p = input_vgprs[i];

      if (i == vertex_id_vgpr)
         p = ctx->abi.vertex_id;
      else if (i == instance_id_vgpr)
         p = ctx->abi.instance_id;

      p = ac_to_float(&ctx->ac, p);
      ret = LLVMBuildInsertValue(ctx->ac.builder, ret, p, key->vs_prolog.num_input_sgprs + i, "");
   }

   /* Compute vertex load indices from instance divisors. */
   LLVMValueRef instance_divisor_constbuf = NULL;

   if (key->vs_prolog.states.instance_divisor_is_fetched) {
      LLVMValueRef list = si_prolog_get_internal_bindings(ctx);
      LLVMValueRef buf_index = LLVMConstInt(ctx->ac.i32, SI_VS_CONST_INSTANCE_DIVISORS, 0);
      instance_divisor_constbuf = ac_build_load_to_sgpr(&ctx->ac, list, buf_index);
   }

   for (i = 0; i < key->vs_prolog.num_inputs; i++) {
      bool divisor_is_one = key->vs_prolog.states.instance_divisor_is_one & (1u << i);
      bool divisor_is_fetched = key->vs_prolog.states.instance_divisor_is_fetched & (1u << i);
      LLVMValueRef index = NULL;

      if (divisor_is_one) {
         index = ctx->abi.instance_id;
      } else if (divisor_is_fetched) {
         LLVMValueRef udiv_factors[4];

         for (unsigned j = 0; j < 4; j++) {
            udiv_factors[j] = si_buffer_load_const(ctx, instance_divisor_constbuf,
                                                   LLVMConstInt(ctx->ac.i32, i * 16 + j * 4, 0));
            udiv_factors[j] = ac_to_integer(&ctx->ac, udiv_factors[j]);
         }
         /* The faster NUW version doesn't work when InstanceID == UINT_MAX.
          * Such InstanceID might not be achievable in a reasonable time though.
          */
         index = ac_build_fast_udiv_nuw(&ctx->ac, ctx->abi.instance_id, udiv_factors[0],
                                        udiv_factors[1], udiv_factors[2], udiv_factors[3]);
      }

      if (divisor_is_one || divisor_is_fetched) {
         /* Add StartInstance. */
         index =
            LLVMBuildAdd(ctx->ac.builder, index,
                         LLVMGetParam(ctx->main_fn, user_sgpr_base + SI_SGPR_START_INSTANCE), "");
      } else {
         /* VertexID + BaseVertex */
         index = LLVMBuildAdd(ctx->ac.builder, ctx->abi.vertex_id,
                              LLVMGetParam(func, user_sgpr_base + SI_SGPR_BASE_VERTEX), "");
      }

      index = ac_to_float(&ctx->ac, index);
      ret = LLVMBuildInsertValue(ctx->ac.builder, ret, index, ctx->args.arg_count + i, "");
   }

   si_llvm_build_ret(ctx, ret);
}

static LLVMValueRef get_base_vertex(struct ac_shader_abi *abi, bool non_indexed_is_zero)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);

   /* This doesn't happen with GL: */
   if (!non_indexed_is_zero)
      return ac_get_arg(&ctx->ac, ctx->args.base_vertex);

   /* For non-indexed draws, the base vertex set by the driver
    * (for direct draws) or the CP (for indirect draws) is the
    * first vertex ID, but GLSL expects 0 to be returned.
    */
   LLVMValueRef indexed = si_unpack_param(ctx, ctx->vs_state_bits, 1, 1);
   indexed = LLVMBuildTrunc(ctx->ac.builder, indexed, ctx->ac.i1, "");

   return LLVMBuildSelect(ctx->ac.builder, indexed, ac_get_arg(&ctx->ac, ctx->args.base_vertex),
                          ctx->ac.i32_0, "");
}

void si_llvm_init_vs_callbacks(struct si_shader_context *ctx, bool ngg_cull_shader)
{
   struct si_shader *shader = ctx->shader;

   if (shader->key.as_ls)
      ctx->abi.emit_outputs = si_llvm_emit_ls_epilogue;
   else if (shader->key.as_es)
      ctx->abi.emit_outputs = si_llvm_emit_es_epilogue;
   else if (ngg_cull_shader)
      ctx->abi.emit_outputs = gfx10_emit_ngg_culling_epilogue;
   else if (shader->key.as_ngg)
      ctx->abi.emit_outputs = gfx10_emit_ngg_epilogue;
   else
      ctx->abi.emit_outputs = si_llvm_emit_vs_epilogue;

   ctx->abi.load_base_vertex = get_base_vertex;
   ctx->abi.load_inputs = si_load_vs_input;
}
