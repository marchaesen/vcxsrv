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
#include "radv_llvm_helper.h"
#include "radv_private.h"
#include "radv_shader.h"
#include "radv_shader_args.h"

#include "ac_binary.h"
#include "ac_nir.h"
#include "ac_llvm_build.h"
#include "ac_nir_to_llvm.h"
#include "ac_shader_abi.h"
#include "ac_shader_util.h"
#include "sid.h"

struct radv_shader_context {
   struct ac_llvm_context ac;
   const struct nir_shader *shader;
   struct ac_shader_abi abi;
   const struct radv_nir_compiler_options *options;
   const struct radv_shader_info *shader_info;
   const struct radv_shader_args *args;

   gl_shader_stage stage;

   unsigned max_workgroup_size;
   LLVMContextRef context;
   LLVMValueRef main_function;

   LLVMValueRef descriptor_sets[MAX_SETS];

   LLVMValueRef ring_offsets;

   LLVMValueRef vs_rel_patch_id;

   LLVMValueRef gs_wave_id;

   LLVMValueRef esgs_ring;
   LLVMValueRef gsvs_ring[4];
   LLVMValueRef hs_ring_tess_offchip;
   LLVMValueRef hs_ring_tess_factor;

   uint64_t output_mask;
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
   const struct radv_userdata_locations *user_sgprs_locs = &ctx->shader_info->user_sgprs_locs;
   uint32_t mask = ctx->shader_info->desc_set_used_mask;

   if (user_sgprs_locs->shader_data[AC_UD_INDIRECT_DESCRIPTOR_SETS].sgpr_idx != -1) {
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
   if (ctx->ac.gfx_level >= GFX10) {
      if (is_pre_gs_stage(stage) && ctx->shader_info->is_ngg) {
         /* On GFX10+, VS and TES are merged into GS for NGG. */
         stage = MESA_SHADER_GEOMETRY;
         has_previous_stage = true;
      }
   }

   ctx->main_function =
      create_llvm_function(&ctx->ac, ctx->ac.module, ctx->ac.builder, &ctx->args->ac,
                           get_llvm_calling_convention(ctx->main_function, stage),
                           ctx->max_workgroup_size, ctx->options);

   ctx->ring_offsets = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.implicit.buffer.ptr",
                                          LLVMPointerType(ctx->ac.i8, AC_ADDR_SPACE_CONST), NULL, 0,
                                          AC_FUNC_ATTR_READNONE);
   ctx->ring_offsets = LLVMBuildBitCast(ctx->ac.builder, ctx->ring_offsets,
                                        ac_array_in_const_addr_space(ctx->ac.v4i32), "");

   load_descriptor_sets(ctx);

   if (stage == MESA_SHADER_TESS_CTRL ||
       (stage == MESA_SHADER_VERTEX && ctx->shader_info->vs.as_ls) ||
       ctx->shader_info->is_ngg ||
       /* GFX9 has the ESGS ring buffer in LDS. */
       (stage == MESA_SHADER_GEOMETRY && has_previous_stage)) {
      ac_declare_lds_as_pointer(&ctx->ac);
   }
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

   uint32_t sample_pos_offset = radv_get_sample_pos_offset(ctx->options->key.ps.num_samples);

   sample_id = LLVMBuildAdd(ctx->ac.builder, sample_id,
                            LLVMConstInt(ctx->ac.i32, sample_pos_offset, false), "");
   result = ac_build_load_invariant(&ctx->ac, ptr, sample_id);

   return result;
}

static void
visit_emit_vertex_with_counter(struct ac_shader_abi *abi, unsigned stream, LLVMValueRef vertexidx,
                               LLVMValueRef *addrs)
{
   unsigned offset = 0;
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);

   for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
      unsigned output_usage_mask = ctx->shader_info->gs.output_usage_mask[i];
      uint8_t output_stream = ctx->shader_info->gs.output_streams[i];
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

         ac_build_buffer_store_dword(&ctx->ac, ctx->gsvs_ring[stream], out_val, NULL, voffset,
                                     ac_get_arg(&ctx->ac, ctx->args->ac.gs2vs_offset),
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
   ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_CUT | AC_SENDMSG_GS | (stream << 8),
                    ctx->gs_wave_id);
}

static LLVMValueRef
radv_load_base_vertex(struct ac_shader_abi *abi, bool non_indexed_is_zero)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
   return ac_get_arg(&ctx->ac, ctx->args->ac.base_vertex);
}

static LLVMValueRef
radv_load_rsrc(struct radv_shader_context *ctx, LLVMValueRef ptr, LLVMTypeRef type)
{
   if (ptr && LLVMTypeOf(ptr) == ctx->ac.i32) {
      LLVMValueRef result;

      LLVMTypeRef ptr_type = LLVMPointerType(type, AC_ADDR_SPACE_CONST_32BIT);
      ptr = LLVMBuildIntToPtr(ctx->ac.builder, ptr, ptr_type, "");
      LLVMSetMetadata(ptr, ctx->ac.uniform_md_kind, ctx->ac.empty_md);

      result = LLVMBuildLoad(ctx->ac.builder, ptr, "");
      LLVMSetMetadata(result, ctx->ac.invariant_load_md_kind, ctx->ac.empty_md);

      return result;
   }

   return ptr;
}

static LLVMValueRef
radv_load_ubo(struct ac_shader_abi *abi, LLVMValueRef buffer_ptr)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
   return radv_load_rsrc(ctx, buffer_ptr, ctx->ac.v4i32);
}

static LLVMValueRef
radv_load_ssbo(struct ac_shader_abi *abi, LLVMValueRef buffer_ptr, bool write, bool non_uniform)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
   return radv_load_rsrc(ctx, buffer_ptr, ctx->ac.v4i32);
}

static LLVMValueRef
radv_get_sampler_desc(struct ac_shader_abi *abi, unsigned descriptor_set, unsigned base_index,
                      unsigned constant_index, LLVMValueRef index,
                      enum ac_descriptor_type desc_type, bool image, bool write, bool bindless)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);

   if (image && desc_type == AC_DESC_FMASK)
      return NULL;

   /* 3 plane formats always have same size and format for plane 1 & 2, so
    * use the tail from plane 1 so that we can store only the first 16 bytes
    * of the last plane. */
   if (desc_type == AC_DESC_PLANE_2 && index && LLVMTypeOf(index) == ctx->ac.i32) {
      LLVMValueRef plane1_addr =
         LLVMBuildSub(ctx->ac.builder, index, LLVMConstInt(ctx->ac.i32, 32, false), "");
      LLVMValueRef descriptor1 = radv_load_rsrc(ctx, plane1_addr, ctx->ac.v8i32);
      LLVMValueRef descriptor2 = radv_load_rsrc(ctx, index, ctx->ac.v4i32);

      LLVMValueRef components[8];
      for (unsigned i = 0; i < 4; ++i)
         components[i] = ac_llvm_extract_elem(&ctx->ac, descriptor2, i);

      for (unsigned i = 4; i < 8; ++i)
         components[i] = ac_llvm_extract_elem(&ctx->ac, descriptor1, i);
      return ac_build_gather_values(&ctx->ac, components, 8);
   }

   bool v4 = desc_type == AC_DESC_BUFFER || desc_type == AC_DESC_SAMPLER;
   return radv_load_rsrc(ctx, index, v4 ? ctx->ac.v4i32 : ctx->ac.v8i32);
}

static LLVMValueRef
radv_fixup_vertex_input_fetches(struct radv_shader_context *ctx, LLVMValueRef value,
                                unsigned num_channels, bool is_float, bool is_64bit)
{
   LLVMValueRef zero = is_64bit ? ctx->ac.i64_0 : (is_float ? ctx->ac.f32_0 : ctx->ac.i32_0);
   LLVMValueRef one = is_64bit ? ctx->ac.i64_0 : (is_float ? ctx->ac.f32_1 : ctx->ac.i32_1);
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
   enum pipe_format attrib_format = ctx->options->key.vs.vertex_attribute_formats[attrib_index];
   const struct util_format_description *desc = util_format_description(attrib_format);
   bool is_float = !desc->channel[0].pure_integer;
   uint8_t input_usage_mask =
      ctx->shader_info->vs.input_usage_mask[driver_location];
   unsigned num_input_channels = util_last_bit(input_usage_mask);

   if (ctx->options->key.vs.instance_rate_inputs & (1u << attrib_index)) {
      uint32_t divisor = ctx->options->key.vs.instance_rate_divisors[attrib_index];

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

   const struct ac_vtx_format_info *vtx_info =
      ac_get_vtx_format_info(GFX8, CHIP_POLARIS10, attrib_format);

   /* Adjust the number of channels to load based on the vertex attribute format. */
   unsigned num_channels = MIN2(num_input_channels, vtx_info->num_channels);
   unsigned attrib_binding = ctx->options->key.vs.vertex_attribute_bindings[attrib_index];
   unsigned attrib_offset = ctx->options->key.vs.vertex_attribute_offsets[attrib_index];
   unsigned attrib_stride = ctx->options->key.vs.vertex_attribute_strides[attrib_index];

   unsigned data_format = vtx_info->hw_format[num_channels - 1] & 0xf;
   unsigned num_format = vtx_info->hw_format[0] >> 4;

   unsigned desc_index =
      ctx->shader_info->vs.use_per_attribute_vb_descs ? attrib_index : attrib_binding;
   desc_index = util_bitcount(ctx->shader_info->vs.vb_desc_usage_mask &
                              u_bit_consecutive(0, desc_index));
   t_offset = LLVMConstInt(ctx->ac.i32, desc_index, false);
   t_list = ac_build_load_to_sgpr(&ctx->ac, t_list_ptr, t_offset);

   /* Always split typed vertex buffer loads on GFX6 and GFX10+ to avoid any alignment issues that
    * triggers memory violations and eventually a GPU hang. This can happen if the stride (static or
    * dynamic) is unaligned and also if the VBO offset is aligned to a scalar (eg. stride is 8 and
    * VBO offset is 2 for R16G16B16A16_SNORM).
    */
   unsigned chan_dwords = vtx_info->chan_byte_size == 8 ? 2 : 1;
   if (((ctx->ac.gfx_level == GFX6 || ctx->ac.gfx_level >= GFX10) && vtx_info->chan_byte_size) ||
       !(vtx_info->has_hw_format & BITFIELD_BIT(vtx_info->num_channels - 1)) ||
       vtx_info->element_size > 16) {
      unsigned chan_format = vtx_info->hw_format[0] & 0xf;
      LLVMValueRef values[4];

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
            ctx->ac.i32_0, chan_dwords, chan_format, num_format, 0, true);
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
         ctx->ac.i32_0, num_channels * chan_dwords, data_format, num_format, 0, true);
   }

   if (vtx_info->chan_byte_size == 8)
      input =
         LLVMBuildBitCast(ctx->ac.builder, input, LLVMVectorType(ctx->ac.i64, num_channels), "");

   input = radv_fixup_vertex_input_fetches(ctx, input, num_channels, is_float,
                                           vtx_info->chan_byte_size == 8);

   for (unsigned chan = 0; chan < 4; chan++) {
      LLVMValueRef llvm_chan = LLVMConstInt(ctx->ac.i32, chan, false);
      out[chan] = LLVMBuildExtractElement(ctx->ac.builder, input, llvm_chan, "");
      if (dest_type == ctx->ac.i16 && is_float) {
         out[chan] = LLVMBuildBitCast(ctx->ac.builder, out[chan], ctx->ac.f32, "");
         out[chan] = LLVMBuildFPTrunc(ctx->ac.builder, out[chan], ctx->ac.f16, "");
      }
   }

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
                         unsigned enabled_channels, unsigned target, unsigned index,
                         struct ac_export_args *args)
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
      unsigned col_format = (ctx->options->key.ps.col_format >> (4 * index)) & 0xf;
      bool is_int8 = (ctx->options->key.ps.is_int8 >> index) & 1;
      bool is_int10 = (ctx->options->key.ps.is_int10 >> index) & 1;
      bool enable_mrt_output_nan_fixup = (ctx->options->key.ps.enable_mrt_output_nan_fixup >> index) & 1;

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
         if (ctx->ac.gfx_level >= GFX10) {
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

      /* Replace NaN by zero (for 32-bit float formats) to fix game bugs if requested. */
      if (enable_mrt_output_nan_fixup && !is_16bit) {
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

   si_llvm_init_export_args(ctx, values, enabled_channels, V_008DFC_SQ_EXP_PARAM + index, 0, &args);
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
   case 3: /* as v3i32 */
   case 4: /* as v4i32 */
      vdata = ac_build_gather_values(&ctx->ac, out, num_comps);
      break;
   }

   LLVMValueRef voffset = LLVMBuildAdd(ctx->ac.builder, so_write_offsets[buf],
                                       LLVMConstInt(ctx->ac.i32, offset, 0), "");
   ac_build_buffer_store_dword(&ctx->ac, so_buffers[buf], vdata, NULL, voffset, ctx->ac.i32_0,
                               ac_glc | ac_slc);
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
         uint16_t stride = ctx->shader_info->so.strides[i];

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
      for (i = 0; i < ctx->shader_info->so.num_outputs; i++) {
         struct radv_shader_output_values shader_out = {0};
         const struct radv_stream_output *output = &ctx->shader_info->so.outputs[i];

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
                         unsigned noutput, const struct radv_vs_output_info *outinfo,
                         bool export_clip_dists)
{
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

      radv_export_param(ctx, outinfo->vs_output_param_offset[slot_name], outputs[i].values,
                        usage_mask);
   }
}

/* Generate export instructions for hardware VS shader stage or NGG GS stage
 * (position and parameter data only).
 */
static void
radv_llvm_export_vs(struct radv_shader_context *ctx, struct radv_shader_output_values *outputs,
                    unsigned noutput, const struct radv_vs_output_info *outinfo,
                    bool export_clip_dists)
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
         si_llvm_init_export_args(ctx, outputs[i].values, 0xf, V_008DFC_SQ_EXP_POS, 0, &pos_args[0]);
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
         si_llvm_init_export_args(ctx, outputs[i].values, 0xf, V_008DFC_SQ_EXP_POS + index, 0,
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

   if (outinfo->writes_pointsize || outinfo->writes_layer || outinfo->writes_layer ||
       outinfo->writes_viewport_index || outinfo->writes_primitive_shading_rate) {
      pos_args[1].enabled_channels = ((outinfo->writes_pointsize == true ? 1 : 0) |
                                      (outinfo->writes_primitive_shading_rate == true ? 2 : 0) |
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
         if (ctx->options->gfx_level >= GFX9) {
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
      }
   }

   /* GFX10 skip POS0 exports if EXEC=0 and DONE=0, causing a hang.
    * Setting valid_mask=1 prevents it and has no other effect.
    */
   if (ctx->ac.gfx_level == GFX10)
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
handle_vs_outputs_post(struct radv_shader_context *ctx)
{
   const struct radv_vs_output_info *outinfo = &ctx->shader_info->outinfo;
   const bool export_clip_dists = outinfo->export_clip_dists;
   struct radv_shader_output_values *outputs;
   unsigned noutput = 0;

   if (ctx->shader_info->so.num_outputs && !ctx->args->is_gs_copy_shader && ctx->stage != MESA_SHADER_GEOMETRY) {
      /* The GS copy shader emission already emits streamout. */
      radv_emit_streamout(ctx, 0);
   }

   /* Allocate a temporary array for the output values. */
   unsigned num_outputs = util_bitcount64(ctx->output_mask);
   outputs = malloc(num_outputs * sizeof(outputs[0]));

   for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
      if (!(ctx->output_mask & (1ull << i)))
         continue;

      outputs[noutput].slot_name = i;
      outputs[noutput].slot_index = i == VARYING_SLOT_CLIP_DIST1;

      if (ctx->stage == MESA_SHADER_VERTEX && !ctx->args->is_gs_copy_shader) {
         outputs[noutput].usage_mask = ctx->shader_info->vs.output_usage_mask[i];
      } else if (ctx->stage == MESA_SHADER_TESS_EVAL) {
         outputs[noutput].usage_mask = ctx->shader_info->tes.output_usage_mask[i];
      } else if (ctx->args->is_gs_copy_shader|| ctx->stage == MESA_SHADER_GEOMETRY) {
         outputs[noutput].usage_mask = ctx->shader_info->gs.output_usage_mask[i];
      }

      for (unsigned j = 0; j < 4; j++) {
         outputs[noutput].values[j] = ac_to_float(&ctx->ac, radv_load_output(ctx, i, j));
      }

      noutput++;
   }

   radv_llvm_export_vs(ctx, outputs, noutput, outinfo, export_clip_dists);

   free(outputs);
}

static bool
si_export_mrt_color(struct radv_shader_context *ctx, LLVMValueRef *color, unsigned target,
                    unsigned index, struct ac_export_args *args)
{
   /* Export */
   si_llvm_init_export_args(ctx, color, 0xf, V_008DFC_SQ_EXP_MRT + target, index, args);
   if (!args->enabled_channels)
      return false; /* unnecessary NULL export */

   return true;
}

static void
radv_export_mrt_z(struct radv_shader_context *ctx, LLVMValueRef depth, LLVMValueRef stencil,
                  LLVMValueRef samplemask)
{
   struct ac_export_args args;

   ac_export_mrt_z(&ctx->ac, depth, stencil, samplemask, NULL, true, &args);

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

      bool ret = si_export_mrt_color(ctx, values, index, i - FRAG_RESULT_DATA0, &color_args[index]);
      if (ret)
         index++;
   }

   /* Process depth, stencil, samplemask. */
   if (ctx->shader_info->ps.writes_z) {
      depth = ac_to_float(&ctx->ac, radv_load_output(ctx, FRAG_RESULT_DEPTH, 0));
   }
   if (ctx->shader_info->ps.writes_stencil) {
      stencil = ac_to_float(&ctx->ac, radv_load_output(ctx, FRAG_RESULT_STENCIL, 0));
   }
   if (ctx->shader_info->ps.writes_sample_mask) {
      samplemask = ac_to_float(&ctx->ac, radv_load_output(ctx, FRAG_RESULT_SAMPLE_MASK, 0));
   }

   /* Set the DONE bit on last non-null color export only if Z isn't
    * exported.
    */
   if (index > 0 && !ctx->shader_info->ps.writes_z &&
       !ctx->shader_info->ps.writes_stencil &&
       !ctx->shader_info->ps.writes_sample_mask) {
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
      ac_build_export_null(&ctx->ac, true);
}

static void
emit_gs_epilogue(struct radv_shader_context *ctx)
{
   if (ctx->ac.gfx_level >= GFX10)
      ac_build_waitcnt(&ctx->ac, AC_WAIT_VSTORE);

   ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_NOP | AC_SENDMSG_GS_DONE, ctx->gs_wave_id);
}

static void
handle_shader_outputs_post(struct ac_shader_abi *abi)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);

   switch (ctx->stage) {
   case MESA_SHADER_VERTEX:
      if (ctx->shader_info->vs.as_ls)
         break; /* Lowered in NIR */
      else if (ctx->shader_info->vs.as_es)
         break; /* Lowered in NIR */
      else if (ctx->shader_info->is_ngg)
         break; /* Lowered in NIR */
      else
         handle_vs_outputs_post(ctx);
      break;
   case MESA_SHADER_FRAGMENT:
      handle_fs_outputs_post(ctx);
      break;
   case MESA_SHADER_GEOMETRY:
      if (ctx->shader_info->is_ngg)
         break; /* Lowered in NIR */
      else
         emit_gs_epilogue(ctx);
      break;
   case MESA_SHADER_TESS_CTRL:
      break; /* Lowered in NIR */
   case MESA_SHADER_TESS_EVAL:
      if (ctx->shader_info->tes.as_es)
         break; /* Lowered in NIR */
      else if (ctx->shader_info->is_ngg)
         break; /* Lowered in NIR */
      else
         handle_vs_outputs_post(ctx);
      break;
   default:
      break;
   }
}

static void
ac_llvm_finalize_module(struct radv_shader_context *ctx, LLVMPassManagerRef passmgr)
{
   LLVMRunPassManager(passmgr, ctx->ac.module);
   LLVMDisposeBuilder(ctx->ac.builder);

   ac_llvm_context_dispose(&ctx->ac);
}

static void
radv_llvm_visit_export_vertex(struct ac_shader_abi *abi)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);

   handle_vs_outputs_post(ctx);
}

static void
ac_setup_rings(struct radv_shader_context *ctx)
{
   if (ctx->options->gfx_level <= GFX8 &&
       (ctx->stage == MESA_SHADER_GEOMETRY ||
        (ctx->stage == MESA_SHADER_VERTEX && ctx->shader_info->vs.as_es) ||
        (ctx->stage == MESA_SHADER_TESS_EVAL && ctx->shader_info->tes.as_es))) {
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

         num_components = ctx->shader_info->gs.num_stream_output_components[stream];

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
      ctx->gs_wave_id =
         ac_unpack_param(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args->ac.merged_wave_info), 16, 8);
   } else {
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

static LLVMValueRef radv_intrinsic_load(struct ac_shader_abi *abi, nir_intrinsic_op op)
{
   struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);

   switch (op) {
   case nir_intrinsic_load_base_vertex:
   case nir_intrinsic_load_first_vertex:
      return radv_load_base_vertex(abi, op == nir_intrinsic_load_base_vertex);
   case nir_intrinsic_load_ring_tess_factors_amd:
      return ctx->hs_ring_tess_factor;
   case nir_intrinsic_load_ring_tess_offchip_amd:
      return ctx->hs_ring_tess_offchip;
   case nir_intrinsic_load_ring_esgs_amd:
      return ctx->esgs_ring;
   default:
      return NULL;
   }
}

static LLVMModuleRef
ac_translate_nir_to_llvm(struct ac_llvm_compiler *ac_llvm,
                         const struct radv_nir_compiler_options *options,
                         const struct radv_shader_info *info,
                         struct nir_shader *const *shaders, int shader_count,
                         const struct radv_shader_args *args)
{
   struct radv_shader_context ctx = {0};
   ctx.args = args;
   ctx.options = options;
   ctx.shader_info = info;

   enum ac_float_mode float_mode = AC_FLOAT_MODE_DEFAULT;

   if (shaders[0]->info.float_controls_execution_mode & FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP32) {
      float_mode = AC_FLOAT_MODE_DENORM_FLUSH_TO_ZERO;
   }

   ac_llvm_context_init(&ctx.ac, ac_llvm, options->gfx_level, options->family,
                        options->has_3d_cube_border_color_mipmap,
                        float_mode, info->wave_size, info->ballot_bit_size);
   ctx.context = ctx.ac.context;

   ctx.max_workgroup_size = info->workgroup_size;

   create_function(&ctx, shaders[shader_count - 1]->info.stage, shader_count >= 2);

   ctx.abi.intrinsic_load = radv_intrinsic_load;
   ctx.abi.load_ubo = radv_load_ubo;
   ctx.abi.load_ssbo = radv_load_ssbo;
   ctx.abi.load_sampler_desc = radv_get_sampler_desc;
   ctx.abi.clamp_shadow_reference = false;
   ctx.abi.robust_buffer_access = options->robust_buffer_access;
   ctx.abi.load_grid_size_from_user_sgpr = args->load_grid_size_from_user_sgpr;

   bool is_ngg = is_pre_gs_stage(shaders[0]->info.stage) && info->is_ngg;
   if (shader_count >= 2 || is_ngg)
      ac_init_exec_full_mask(&ctx.ac);

   if (args->ac.vertex_id.used)
      ctx.abi.vertex_id = ac_get_arg(&ctx.ac, args->ac.vertex_id);
   if (args->ac.vs_rel_patch_id.used)
      ctx.vs_rel_patch_id = ac_get_arg(&ctx.ac, args->ac.vs_rel_patch_id);
   if (args->ac.instance_id.used)
      ctx.abi.instance_id = ac_get_arg(&ctx.ac, args->ac.instance_id);

   if (options->has_ls_vgpr_init_bug &&
       shaders[shader_count - 1]->info.stage == MESA_SHADER_TESS_CTRL)
      ac_nir_fixup_ls_hs_input_vgprs(&ctx);

   if (is_ngg) {
      ctx.abi.export_vertex = radv_llvm_visit_export_vertex;

      if (!info->is_ngg_passthrough)
         declare_esgs_ring(&ctx);

      if (ctx.stage == MESA_SHADER_GEOMETRY) {
         /* Scratch space used by NGG GS for repacking vertices at the end. */
         LLVMTypeRef ai32 = LLVMArrayType(ctx.ac.i32, 8);
         LLVMValueRef gs_ngg_scratch =
            LLVMAddGlobalInAddressSpace(ctx.ac.module, ai32, "ngg_scratch", AC_ADDR_SPACE_LDS);
         LLVMSetInitializer(gs_ngg_scratch, LLVMGetUndef(ai32));
         LLVMSetLinkage(gs_ngg_scratch, LLVMExternalLinkage);
         LLVMSetAlignment(gs_ngg_scratch, 4);

         /* Vertex emit space used by NGG GS for storing all vertex attributes. */
         LLVMValueRef gs_ngg_emit =
            LLVMAddGlobalInAddressSpace(ctx.ac.module, LLVMArrayType(ctx.ac.i32, 0), "ngg_emit", AC_ADDR_SPACE_LDS);
         LLVMSetInitializer(gs_ngg_emit, LLVMGetUndef(ai32));
         LLVMSetLinkage(gs_ngg_emit, LLVMExternalLinkage);
         LLVMSetAlignment(gs_ngg_emit, 4);
      }

      /* GFX10 hang workaround - there needs to be an s_barrier before gs_alloc_req always */
      if (ctx.ac.gfx_level == GFX10 && shader_count == 1)
         ac_build_s_barrier(&ctx.ac, shaders[0]->info.stage);
   }

   for (int shader_idx = 0; shader_idx < shader_count; ++shader_idx) {
      ctx.stage = shaders[shader_idx]->info.stage;
      ctx.shader = shaders[shader_idx];
      ctx.output_mask = 0;

      if (shaders[shader_idx]->info.stage == MESA_SHADER_GEOMETRY && !ctx.shader_info->is_ngg) {
         ctx.abi.emit_vertex_with_counter = visit_emit_vertex_with_counter;
         ctx.abi.emit_primitive = visit_end_primitive;
      } else if (shaders[shader_idx]->info.stage == MESA_SHADER_TESS_EVAL) {
      } else if (shaders[shader_idx]->info.stage == MESA_SHADER_VERTEX) {
         ctx.abi.load_inputs = radv_load_vs_inputs;
      } else if (shaders[shader_idx]->info.stage == MESA_SHADER_FRAGMENT) {
         ctx.abi.load_sample_position = load_sample_position;
      }

      if (shader_idx && !(shaders[shader_idx]->info.stage == MESA_SHADER_GEOMETRY && info->is_ngg)) {
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
         ac_build_waitcnt(&ctx.ac, AC_WAIT_LGKM);
         ac_build_s_barrier(&ctx.ac, shaders[shader_idx]->info.stage);
      }

      nir_foreach_shader_out_variable(variable, shaders[shader_idx]) scan_shader_output_decl(
         &ctx, variable, shaders[shader_idx], shaders[shader_idx]->info.stage);

      ac_setup_rings(&ctx);

      bool check_merged_wave_info = shader_count >= 2 && !(is_ngg && shader_idx == 1);
      LLVMBasicBlockRef merge_block = NULL;

      if (check_merged_wave_info) {
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
      else if (shaders[shader_idx]->info.stage == MESA_SHADER_GEOMETRY && !info->is_ngg)
         prepare_gs_input_vgprs(&ctx, shader_count >= 2);

      if (!ac_nir_translate(&ctx.ac, &ctx.abi, &args->ac, shaders[shader_idx])) {
         abort();
      }

      if (!gl_shader_stage_is_compute(shaders[shader_idx]->info.stage))
         handle_shader_outputs_post(&ctx.abi);

      if (check_merged_wave_info) {
         LLVMBuildBr(ctx.ac.builder, merge_block);
         LLVMPositionBuilderAtEnd(ctx.ac.builder, merge_block);
      }
   }

   LLVMBuildRetVoid(ctx.ac.builder);

   if (options->dump_preoptir) {
      fprintf(stderr, "%s LLVM IR:\n\n",
              radv_get_shader_name(info, shaders[shader_count - 1]->info.stage));
      ac_dump_module(ctx.ac.module);
      fprintf(stderr, "\n");
   }

   ac_llvm_finalize_module(&ctx, ac_llvm->passmgr);

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
radv_compile_nir_shader(struct ac_llvm_compiler *ac_llvm,
                        const struct radv_nir_compiler_options *options,
                        const struct radv_shader_info *info,
                        struct radv_shader_binary **rbinary,
                        const struct radv_shader_args *args, struct nir_shader *const *nir,
                        int nir_count)
{

   LLVMModuleRef llvm_module;

   llvm_module = ac_translate_nir_to_llvm(ac_llvm, options, info, nir, nir_count, args);

   ac_compile_llvm_module(ac_llvm, llvm_module, rbinary, nir[nir_count - 1]->info.stage,
                          radv_get_shader_name(info, nir[nir_count - 1]->info.stage),
                          options);
}

static void
ac_gs_copy_shader_emit(struct radv_shader_context *ctx)
{
   LLVMValueRef vtx_offset =
      LLVMBuildMul(ctx->ac.builder, ac_get_arg(&ctx->ac, ctx->args->ac.vertex_id),
                   LLVMConstInt(ctx->ac.i32, 4, false), "");
   LLVMValueRef stream_id;

   /* Fetch the vertex stream ID. */
   if (ctx->shader_info->so.num_outputs) {
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
      unsigned num_components = ctx->shader_info->gs.num_stream_output_components[stream];
      LLVMBasicBlockRef bb;
      unsigned offset;

      if (stream > 0 && !num_components)
         continue;

      if (stream > 0 && !ctx->shader_info->so.num_outputs)
         continue;

      bb = LLVMInsertBasicBlockInContext(ctx->ac.context, end_bb, "out");
      LLVMAddCase(switch_inst, LLVMConstInt(ctx->ac.i32, stream, 0), bb);
      LLVMPositionBuilderAtEnd(ctx->ac.builder, bb);

      offset = 0;
      for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
         unsigned output_usage_mask = ctx->shader_info->gs.output_usage_mask[i];
         unsigned output_stream = ctx->shader_info->gs.output_streams[i];
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
                                         soffset, ctx->ac.f32, ac_glc | ac_slc, true, false);

            LLVMTypeRef type = LLVMGetAllocatedType(ctx->abi.outputs[ac_llvm_reg_index_soa(i, j)]);
            if (ac_get_type_size(type) == 2) {
               value = LLVMBuildBitCast(ctx->ac.builder, value, ctx->ac.i32, "");
               value = LLVMBuildTrunc(ctx->ac.builder, value, ctx->ac.i16, "");
            }

            LLVMBuildStore(ctx->ac.builder, ac_to_float(&ctx->ac, value),
                           ctx->abi.outputs[ac_llvm_reg_index_soa(i, j)]);
         }
      }

      if (ctx->shader_info->so.num_outputs)
         radv_emit_streamout(ctx, stream);

      if (stream == 0) {
         handle_vs_outputs_post(ctx);
      }

      LLVMBuildBr(ctx->ac.builder, end_bb);
   }

   LLVMPositionBuilderAtEnd(ctx->ac.builder, end_bb);
}

static void
radv_compile_gs_copy_shader(struct ac_llvm_compiler *ac_llvm,
                            const struct radv_nir_compiler_options *options,
                            const struct radv_shader_info *info,
                            struct nir_shader *geom_shader,
                            struct radv_shader_binary **rbinary,
                            const struct radv_shader_args *args)
{
   struct radv_shader_context ctx = {0};
   ctx.args = args;
   ctx.options = options;
   ctx.shader_info = info;

   assert(args->is_gs_copy_shader);

   ac_llvm_context_init(&ctx.ac, ac_llvm, options->gfx_level, options->family,
                        options->has_3d_cube_border_color_mipmap,
                        AC_FLOAT_MODE_DEFAULT, 64, 64);
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

   ac_llvm_finalize_module(&ctx, ac_llvm->passmgr);

   ac_compile_llvm_module(ac_llvm, ctx.ac.module, rbinary, MESA_SHADER_VERTEX, "GS Copy Shader",
                          options);
   (*rbinary)->is_gs_copy_shader = true;
}

void
llvm_compile_shader(const struct radv_nir_compiler_options *options,
                    const struct radv_shader_info *info, unsigned shader_count,
                    struct nir_shader *const *shaders, struct radv_shader_binary **binary,
                    const struct radv_shader_args *args)
{
   enum ac_target_machine_options tm_options = 0;
   struct ac_llvm_compiler ac_llvm;

   tm_options |= AC_TM_SUPPORTS_SPILL;
   if (options->check_ir)
      tm_options |= AC_TM_CHECK_IR;

   radv_init_llvm_compiler(&ac_llvm, options->family, tm_options, info->wave_size);

   if (args->is_gs_copy_shader) {
      radv_compile_gs_copy_shader(&ac_llvm, options, info, *shaders, binary, args);
   } else {
      radv_compile_nir_shader(&ac_llvm, options, info, binary, args, shaders, shader_count);
   }
}
