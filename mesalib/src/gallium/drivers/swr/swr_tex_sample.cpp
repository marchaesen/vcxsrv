/**************************************************************************
 *
 * Copyright 2009 VMware, Inc.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/**
 * Largely a copy of llvmpipe's lp_tex_sample.c
 */

/**
 * Texture sampling code generation
 *
 * This file is nothing more than ugly glue between three largely independent
 * entities:
 * - TGSI -> LLVM translation (i.e., lp_build_tgsi_soa)
 * - texture sampling code generation (i.e., lp_build_sample_soa)
 * - SWR driver
 *
 * All interesting code is in the functions mentioned above. There is really
 * nothing to see here.
 *
 * @author Jose Fonseca <jfonseca@vmware.com>
 */

#include "state.h"
#include "JitManager.h"
#include "gen_state_llvm.h"

#include "pipe/p_defines.h"
#include "pipe/p_shader_tokens.h"
#include "gallivm/lp_bld_debug.h"
#include "gallivm/lp_bld_const.h"
#include "gallivm/lp_bld_type.h"
#include "gallivm/lp_bld_sample.h"
#include "gallivm/lp_bld_tgsi.h"
#include "util/u_memory.h"

#include "swr_tex_sample.h"
#include "gen_surf_state_llvm.h"
#include "gen_swr_context_llvm.h"

using namespace SwrJit;

/**
 * This provides the bridge between the sampler state store in
 * lp_jit_context and lp_jit_texture and the sampler code
 * generator. It provides the texture layout information required by
 * the texture sampler code generator in terms of the state stored in
 * lp_jit_context and lp_jit_texture in runtime.
 */
struct swr_sampler_dynamic_state {
   struct lp_sampler_dynamic_state base;

   const struct swr_sampler_static_state *static_state;

   enum pipe_shader_type shader_type;
};


/**
 * This is the bridge between our sampler and the TGSI translator.
 */
struct swr_sampler_soa {
   struct lp_build_sampler_soa base;

   struct swr_sampler_dynamic_state dynamic_state;
};


/**
 * Fetch the specified member of the lp_jit_texture structure.
 * \param emit_load  if TRUE, emit the LLVM load instruction to actually
 *                   fetch the field's value.  Otherwise, just emit the
 *                   GEP code to address the field.
 *
 * @sa http://llvm.org/docs/GetElementPtr.html
 */
static LLVMValueRef
swr_texture_member(const struct lp_sampler_dynamic_state *base,
                   struct gallivm_state *gallivm,
                   LLVMValueRef context_ptr,
                   unsigned texture_unit,
                   unsigned member_index,
                   const char *member_name,
                   boolean emit_load)
{
   LLVMBuilderRef builder = gallivm->builder;
   LLVMValueRef indices[4];
   LLVMValueRef ptr;
   LLVMValueRef res;

   assert(texture_unit < PIPE_MAX_SHADER_SAMPLER_VIEWS);

   /* context[0] */
   indices[0] = lp_build_const_int32(gallivm, 0);
   /* context[0].textures */
   auto dynamic = (const struct swr_sampler_dynamic_state *)base;
   switch (dynamic->shader_type) {
   case PIPE_SHADER_FRAGMENT:
      indices[1] = lp_build_const_int32(gallivm, swr_draw_context_texturesFS);
      break;
   case PIPE_SHADER_VERTEX:
      indices[1] = lp_build_const_int32(gallivm, swr_draw_context_texturesVS);
      break;
   case PIPE_SHADER_GEOMETRY:
      indices[1] = lp_build_const_int32(gallivm, swr_draw_context_texturesGS);
      break;
   case PIPE_SHADER_TESS_CTRL:
      indices[1] = lp_build_const_int32(gallivm, swr_draw_context_texturesTCS);
      break;
   case PIPE_SHADER_TESS_EVAL:
      indices[1] = lp_build_const_int32(gallivm, swr_draw_context_texturesTES);
      break;
   default:
      assert(0 && "unsupported shader type");
      break;
   }
   /* context[0].textures[unit] */
   indices[2] = lp_build_const_int32(gallivm, texture_unit);
   /* context[0].textures[unit].member */
   indices[3] = lp_build_const_int32(gallivm, member_index);

   ptr = LLVMBuildGEP(builder, context_ptr, indices, ARRAY_SIZE(indices), "");

   if (emit_load)
      res = LLVMBuildLoad(builder, ptr, "");
   else
      res = ptr;

   lp_build_name(res, "context.texture%u.%s", texture_unit, member_name);

   return res;
}


/**
 * Helper macro to instantiate the functions that generate the code to
 * fetch the members of lp_jit_texture to fulfill the sampler code
 * generator requests.
 *
 * This complexity is the price we have to pay to keep the texture
 * sampler code generator a reusable module without dependencies to
 * swr internals.
 */
#define SWR_TEXTURE_MEMBER(_name, _emit_load)                                \
   static LLVMValueRef swr_texture_##_name(                                  \
      const struct lp_sampler_dynamic_state *base,                           \
      struct gallivm_state *gallivm,                                         \
      LLVMValueRef context_ptr,                                              \
      unsigned texture_unit,                                                 \
      LLVMValueRef texture_unit_offset)                                      \
   {                                                                         \
      return swr_texture_member(base,                                        \
                                gallivm,                                     \
                                context_ptr,                                 \
                                texture_unit,                                \
                                swr_jit_texture_##_name,                     \
                                #_name,                                      \
                                _emit_load);                                 \
   }


SWR_TEXTURE_MEMBER(width, TRUE)
SWR_TEXTURE_MEMBER(height, TRUE)
SWR_TEXTURE_MEMBER(depth, TRUE)
SWR_TEXTURE_MEMBER(first_level, TRUE)
SWR_TEXTURE_MEMBER(last_level, TRUE)
SWR_TEXTURE_MEMBER(base_ptr, TRUE)
SWR_TEXTURE_MEMBER(num_samples, TRUE)
SWR_TEXTURE_MEMBER(sample_stride, TRUE)
SWR_TEXTURE_MEMBER(row_stride, FALSE)
SWR_TEXTURE_MEMBER(img_stride, FALSE)
SWR_TEXTURE_MEMBER(mip_offsets, FALSE)


/**
 * Fetch the specified member of the lp_jit_sampler structure.
 * \param emit_load  if TRUE, emit the LLVM load instruction to actually
 *                   fetch the field's value.  Otherwise, just emit the
 *                   GEP code to address the field.
 *
 * @sa http://llvm.org/docs/GetElementPtr.html
 */
static LLVMValueRef
swr_sampler_member(const struct lp_sampler_dynamic_state *base,
                   struct gallivm_state *gallivm,
                   LLVMValueRef context_ptr,
                   unsigned sampler_unit,
                   unsigned member_index,
                   const char *member_name,
                   boolean emit_load)
{
   LLVMBuilderRef builder = gallivm->builder;
   LLVMValueRef indices[4];
   LLVMValueRef ptr;
   LLVMValueRef res;

   assert(sampler_unit < PIPE_MAX_SAMPLERS);

   /* context[0] */
   indices[0] = lp_build_const_int32(gallivm, 0);
   /* context[0].samplers */
   auto dynamic = (const struct swr_sampler_dynamic_state *)base;
   switch (dynamic->shader_type) {
   case PIPE_SHADER_FRAGMENT:
      indices[1] = lp_build_const_int32(gallivm, swr_draw_context_samplersFS);
      break;
   case PIPE_SHADER_VERTEX:
      indices[1] = lp_build_const_int32(gallivm, swr_draw_context_samplersVS);
      break;
   case PIPE_SHADER_GEOMETRY:
      indices[1] = lp_build_const_int32(gallivm, swr_draw_context_samplersGS);
      break;
   case PIPE_SHADER_TESS_CTRL:
      indices[1] = lp_build_const_int32(gallivm, swr_draw_context_samplersTCS);
      break;
   case PIPE_SHADER_TESS_EVAL:
      indices[1] = lp_build_const_int32(gallivm, swr_draw_context_samplersTES);
      break;
   default:
      assert(0 && "unsupported shader type");
      break;
   }
   /* context[0].samplers[unit] */
   indices[2] = lp_build_const_int32(gallivm, sampler_unit);
   /* context[0].samplers[unit].member */
   indices[3] = lp_build_const_int32(gallivm, member_index);

   ptr = LLVMBuildGEP(builder, context_ptr, indices, ARRAY_SIZE(indices), "");

   if (emit_load)
      res = LLVMBuildLoad(builder, ptr, "");
   else
      res = ptr;

   lp_build_name(res, "context.sampler%u.%s", sampler_unit, member_name);

   return res;
}


#define SWR_SAMPLER_MEMBER(_name, _emit_load)                                \
   static LLVMValueRef swr_sampler_##_name(                                  \
      const struct lp_sampler_dynamic_state *base,                           \
      struct gallivm_state *gallivm,                                         \
      LLVMValueRef context_ptr,                                              \
      unsigned sampler_unit)                                                 \
   {                                                                         \
      return swr_sampler_member(base,                                        \
                                gallivm,                                     \
                                context_ptr,                                 \
                                sampler_unit,                                \
                                swr_jit_sampler_##_name,                     \
                                #_name,                                      \
                                _emit_load);                                 \
   }


SWR_SAMPLER_MEMBER(min_lod, TRUE)
SWR_SAMPLER_MEMBER(max_lod, TRUE)
SWR_SAMPLER_MEMBER(lod_bias, TRUE)
SWR_SAMPLER_MEMBER(border_color, FALSE)


static void
swr_sampler_soa_destroy(struct lp_build_sampler_soa *sampler)
{
   FREE(sampler);
}


/**
 * Fetch filtered values from texture.
 * The 'texel' parameter returns four vectors corresponding to R, G, B, A.
 */
static void
swr_sampler_soa_emit_fetch_texel(const struct lp_build_sampler_soa *base,
                                 struct gallivm_state *gallivm,
                                 const struct lp_sampler_params *params)
{
   struct swr_sampler_soa *sampler = (struct swr_sampler_soa *)base;
   unsigned texture_index = params->texture_index;
   unsigned sampler_index = params->sampler_index;

   assert(sampler_index < PIPE_MAX_SAMPLERS);
   assert(texture_index < PIPE_MAX_SHADER_SAMPLER_VIEWS);

#if 0
      lp_build_sample_nop(gallivm, params->type, params->coords, params->texel);
#else
   lp_build_sample_soa(
      &sampler->dynamic_state.static_state[texture_index].texture_state,
      &sampler->dynamic_state.static_state[sampler_index].sampler_state,
      &sampler->dynamic_state.base,
      gallivm,
      params);
#endif
}

/**
 * Fetch the texture size.
 */
static void
swr_sampler_soa_emit_size_query(const struct lp_build_sampler_soa *base,
                                struct gallivm_state *gallivm,
                                const struct lp_sampler_size_query_params *params)
{
   struct swr_sampler_soa *sampler = (struct swr_sampler_soa *)base;

   assert(params->texture_unit < PIPE_MAX_SHADER_SAMPLER_VIEWS);

   lp_build_size_query_soa(
      gallivm,
      &sampler->dynamic_state.static_state[params->texture_unit].texture_state,
      &sampler->dynamic_state.base,
      params);
}


struct lp_build_sampler_soa *
swr_sampler_soa_create(const struct swr_sampler_static_state *static_state,
                       enum pipe_shader_type shader_type)
{
   struct swr_sampler_soa *sampler;

   sampler = CALLOC_STRUCT(swr_sampler_soa);
   if (!sampler)
      return NULL;

   sampler->base.destroy = swr_sampler_soa_destroy;
   sampler->base.emit_tex_sample = swr_sampler_soa_emit_fetch_texel;
   sampler->base.emit_size_query = swr_sampler_soa_emit_size_query;
   sampler->dynamic_state.base.width = swr_texture_width;
   sampler->dynamic_state.base.height = swr_texture_height;
   sampler->dynamic_state.base.depth = swr_texture_depth;
   sampler->dynamic_state.base.first_level = swr_texture_first_level;
   sampler->dynamic_state.base.last_level = swr_texture_last_level;
   sampler->dynamic_state.base.base_ptr = swr_texture_base_ptr;
   sampler->dynamic_state.base.row_stride = swr_texture_row_stride;
   sampler->dynamic_state.base.img_stride = swr_texture_img_stride;
   sampler->dynamic_state.base.mip_offsets = swr_texture_mip_offsets;
   sampler->dynamic_state.base.num_samples = swr_texture_num_samples;
   sampler->dynamic_state.base.sample_stride = swr_texture_sample_stride;
   sampler->dynamic_state.base.min_lod = swr_sampler_min_lod;
   sampler->dynamic_state.base.max_lod = swr_sampler_max_lod;
   sampler->dynamic_state.base.lod_bias = swr_sampler_lod_bias;
   sampler->dynamic_state.base.border_color = swr_sampler_border_color;

   sampler->dynamic_state.static_state = static_state;

   sampler->dynamic_state.shader_type = shader_type;

   return &sampler->base;
}
