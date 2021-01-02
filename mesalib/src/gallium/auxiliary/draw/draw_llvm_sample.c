/**************************************************************************
 *
 * Copyright 2010 VMware, Inc.
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
 * Texture sampling code generation
 * @author Jose Fonseca <jfonseca@vmware.com>
 */

#include "pipe/p_defines.h"
#include "pipe/p_shader_tokens.h"
#include "gallivm/lp_bld_const.h"
#include "gallivm/lp_bld_debug.h"
#include "gallivm/lp_bld_type.h"
#include "gallivm/lp_bld_sample.h"
#include "gallivm/lp_bld_tgsi.h"


#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/u_pointer.h"
#include "util/u_string.h"

#include "draw_llvm.h"


/**
 * This provides the bridge between the sampler state store in
 * lp_jit_context and lp_jit_texture and the sampler code
 * generator. It provides the texture layout information required by
 * the texture sampler code generator in terms of the state stored in
 * lp_jit_context and lp_jit_texture in runtime.
 */
struct draw_llvm_sampler_dynamic_state
{
   struct lp_sampler_dynamic_state base;

   const struct draw_sampler_static_state *static_state;
};


/**
 * This is the bridge between our sampler and the TGSI translator.
 */
struct draw_llvm_sampler_soa
{
   struct lp_build_sampler_soa base;

   struct draw_llvm_sampler_dynamic_state dynamic_state;

   unsigned nr_samplers;
};

struct draw_llvm_image_dynamic_state
{
   struct lp_sampler_dynamic_state base;

   const struct draw_image_static_state *static_state;
};

struct draw_llvm_image_soa
{
   struct lp_build_image_soa base;

   struct draw_llvm_image_dynamic_state dynamic_state;

   unsigned nr_images;
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
draw_llvm_texture_member(const struct lp_sampler_dynamic_state *base,
                         struct gallivm_state *gallivm,
                         LLVMValueRef context_ptr,
                         unsigned texture_unit,
                         LLVMValueRef texture_unit_offset,
                         unsigned member_index,
                         const char *member_name,
                         boolean emit_load)
{
   LLVMBuilderRef builder = gallivm->builder;
   LLVMValueRef indices[4];
   LLVMValueRef ptr;
   LLVMValueRef res;

   debug_assert(texture_unit < PIPE_MAX_SHADER_SAMPLER_VIEWS);

   /* context[0] */
   indices[0] = lp_build_const_int32(gallivm, 0);
   /* context[0].textures */
   indices[1] = lp_build_const_int32(gallivm, DRAW_JIT_CTX_TEXTURES);
   /* context[0].textures[unit] */
   indices[2] = lp_build_const_int32(gallivm, texture_unit);
   if (texture_unit_offset) {
      indices[2] = LLVMBuildAdd(gallivm->builder, indices[2], texture_unit_offset, "");
      LLVMValueRef cond = LLVMBuildICmp(gallivm->builder, LLVMIntULT, indices[2], lp_build_const_int32(gallivm, PIPE_MAX_SHADER_SAMPLER_VIEWS), "");
      indices[2] = LLVMBuildSelect(gallivm->builder, cond, indices[2], lp_build_const_int32(gallivm, texture_unit), "");
   }
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
 * Fetch the specified member of the lp_jit_sampler structure.
 * \param emit_load  if TRUE, emit the LLVM load instruction to actually
 *                   fetch the field's value.  Otherwise, just emit the
 *                   GEP code to address the field.
 *
 * @sa http://llvm.org/docs/GetElementPtr.html
 */
static LLVMValueRef
draw_llvm_sampler_member(const struct lp_sampler_dynamic_state *base,
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

   debug_assert(sampler_unit < PIPE_MAX_SAMPLERS);

   /* context[0] */
   indices[0] = lp_build_const_int32(gallivm, 0);
   /* context[0].samplers */
   indices[1] = lp_build_const_int32(gallivm, DRAW_JIT_CTX_SAMPLERS);
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

/**
 * Fetch the specified member of the lp_jit_texture structure.
 * \param emit_load  if TRUE, emit the LLVM load instruction to actually
 *                   fetch the field's value.  Otherwise, just emit the
 *                   GEP code to address the field.
 *
 * @sa http://llvm.org/docs/GetElementPtr.html
 */
static LLVMValueRef
draw_llvm_image_member(const struct lp_sampler_dynamic_state *base,
                       struct gallivm_state *gallivm,
                       LLVMValueRef context_ptr,
                       unsigned image_unit,
                       LLVMValueRef image_unit_offset,
                       unsigned member_index,
                       const char *member_name,
                       boolean emit_load)
{
   LLVMBuilderRef builder = gallivm->builder;
   LLVMValueRef indices[4];
   LLVMValueRef ptr;
   LLVMValueRef res;

   debug_assert(image_unit < PIPE_MAX_SHADER_IMAGES);

   /* context[0] */
   indices[0] = lp_build_const_int32(gallivm, 0);
   /* context[0].textures */
   indices[1] = lp_build_const_int32(gallivm, DRAW_JIT_CTX_IMAGES);
   /* context[0].textures[unit] */
   indices[2] = lp_build_const_int32(gallivm, image_unit);
   if (image_unit_offset) {
      indices[2] = LLVMBuildAdd(gallivm->builder, indices[2], image_unit_offset, "");
      LLVMValueRef cond = LLVMBuildICmp(gallivm->builder, LLVMIntULT, indices[2], lp_build_const_int32(gallivm, PIPE_MAX_SHADER_IMAGES), "");
      indices[2] = LLVMBuildSelect(gallivm->builder, cond, indices[2], lp_build_const_int32(gallivm, image_unit), "");
   }
   /* context[0].textures[unit].member */
   indices[3] = lp_build_const_int32(gallivm, member_index);

   ptr = LLVMBuildGEP(builder, context_ptr, indices, ARRAY_SIZE(indices), "");

   if (emit_load)
      res = LLVMBuildLoad(builder, ptr, "");
   else
      res = ptr;

   lp_build_name(res, "context.image%u.%s", image_unit, member_name);

   return res;
}

/**
 * Helper macro to instantiate the functions that generate the code to
 * fetch the members of lp_jit_texture to fulfill the sampler code
 * generator requests.
 *
 * This complexity is the price we have to pay to keep the texture
 * sampler code generator a reusable module without dependencies to
 * llvmpipe internals.
 */
#define DRAW_LLVM_TEXTURE_MEMBER(_name, _index, _emit_load)  \
   static LLVMValueRef \
   draw_llvm_texture_##_name( const struct lp_sampler_dynamic_state *base, \
                              struct gallivm_state *gallivm,               \
                              LLVMValueRef context_ptr,                    \
                              unsigned texture_unit,                       \
                              LLVMValueRef texture_unit_offset)            \
   { \
      return draw_llvm_texture_member(base, gallivm, context_ptr, \
                                      texture_unit, texture_unit_offset, \
                                      _index, #_name, _emit_load );     \
   }


DRAW_LLVM_TEXTURE_MEMBER(width,      DRAW_JIT_TEXTURE_WIDTH, TRUE)
DRAW_LLVM_TEXTURE_MEMBER(height,     DRAW_JIT_TEXTURE_HEIGHT, TRUE)
DRAW_LLVM_TEXTURE_MEMBER(depth,      DRAW_JIT_TEXTURE_DEPTH, TRUE)
DRAW_LLVM_TEXTURE_MEMBER(first_level,DRAW_JIT_TEXTURE_FIRST_LEVEL, TRUE)
DRAW_LLVM_TEXTURE_MEMBER(last_level, DRAW_JIT_TEXTURE_LAST_LEVEL, TRUE)
DRAW_LLVM_TEXTURE_MEMBER(base_ptr,   DRAW_JIT_TEXTURE_BASE, TRUE)
DRAW_LLVM_TEXTURE_MEMBER(row_stride, DRAW_JIT_TEXTURE_ROW_STRIDE, FALSE)
DRAW_LLVM_TEXTURE_MEMBER(img_stride, DRAW_JIT_TEXTURE_IMG_STRIDE, FALSE)
DRAW_LLVM_TEXTURE_MEMBER(mip_offsets, DRAW_JIT_TEXTURE_MIP_OFFSETS, FALSE)
DRAW_LLVM_TEXTURE_MEMBER(num_samples, DRAW_JIT_TEXTURE_NUM_SAMPLES, TRUE)
DRAW_LLVM_TEXTURE_MEMBER(sample_stride, DRAW_JIT_TEXTURE_SAMPLE_STRIDE, TRUE)

#define DRAW_LLVM_SAMPLER_MEMBER(_name, _index, _emit_load)  \
   static LLVMValueRef \
   draw_llvm_sampler_##_name( const struct lp_sampler_dynamic_state *base, \
                              struct gallivm_state *gallivm,               \
                              LLVMValueRef context_ptr,                    \
                              unsigned sampler_unit)                       \
   { \
      return draw_llvm_sampler_member(base, gallivm, context_ptr, \
                                      sampler_unit, _index, #_name, _emit_load ); \
   }


DRAW_LLVM_SAMPLER_MEMBER(min_lod,    DRAW_JIT_SAMPLER_MIN_LOD, TRUE)
DRAW_LLVM_SAMPLER_MEMBER(max_lod,    DRAW_JIT_SAMPLER_MAX_LOD, TRUE)
DRAW_LLVM_SAMPLER_MEMBER(lod_bias,   DRAW_JIT_SAMPLER_LOD_BIAS, TRUE)
DRAW_LLVM_SAMPLER_MEMBER(border_color, DRAW_JIT_SAMPLER_BORDER_COLOR, FALSE)

#define DRAW_LLVM_IMAGE_MEMBER(_name, _index, _emit_load)  \
   static LLVMValueRef \
   draw_llvm_image_##_name( const struct lp_sampler_dynamic_state *base, \
                            struct gallivm_state *gallivm,               \
                            LLVMValueRef context_ptr,                    \
                            unsigned image_unit, LLVMValueRef image_unit_offset) \
   { \
      return draw_llvm_image_member(base, gallivm, context_ptr, \
                                    image_unit, image_unit_offset, \
                                    _index, #_name, _emit_load );  \
   }


DRAW_LLVM_IMAGE_MEMBER(width,      DRAW_JIT_IMAGE_WIDTH, TRUE)
DRAW_LLVM_IMAGE_MEMBER(height,     DRAW_JIT_IMAGE_HEIGHT, TRUE)
DRAW_LLVM_IMAGE_MEMBER(depth,      DRAW_JIT_IMAGE_DEPTH, TRUE)
DRAW_LLVM_IMAGE_MEMBER(base_ptr,   DRAW_JIT_IMAGE_BASE, TRUE)
DRAW_LLVM_IMAGE_MEMBER(row_stride, DRAW_JIT_IMAGE_ROW_STRIDE, TRUE)
DRAW_LLVM_IMAGE_MEMBER(img_stride, DRAW_JIT_IMAGE_IMG_STRIDE, TRUE)
DRAW_LLVM_IMAGE_MEMBER(num_samples, DRAW_JIT_IMAGE_NUM_SAMPLES, TRUE)
DRAW_LLVM_IMAGE_MEMBER(sample_stride, DRAW_JIT_IMAGE_SAMPLE_STRIDE, TRUE)

static void
draw_llvm_sampler_soa_destroy(struct lp_build_sampler_soa *sampler)
{
   FREE(sampler);
}


/**
 * Fetch filtered values from texture.
 * The 'texel' parameter returns four vectors corresponding to R, G, B, A.
 */
static void
draw_llvm_sampler_soa_emit_fetch_texel(const struct lp_build_sampler_soa *base,
                                       struct gallivm_state *gallivm,
                                       const struct lp_sampler_params *params)
{
   struct draw_llvm_sampler_soa *sampler = (struct draw_llvm_sampler_soa *)base;
   unsigned texture_index = params->texture_index;
   unsigned sampler_index = params->sampler_index;

   assert(texture_index < PIPE_MAX_SHADER_SAMPLER_VIEWS);
   assert(sampler_index < PIPE_MAX_SAMPLERS);

   if (params->texture_index_offset) {
      struct lp_build_sample_array_switch switch_info;
      memset(&switch_info, 0, sizeof(switch_info));
      LLVMValueRef unit = LLVMBuildAdd(gallivm->builder, params->texture_index_offset,
                                       lp_build_const_int32(gallivm, texture_index), "");
      lp_build_sample_array_init_soa(&switch_info, gallivm, params, unit,
                                     0, sampler->nr_samplers);

      for (unsigned i = 0; i < sampler->nr_samplers; i++) {
         lp_build_sample_array_case_soa(&switch_info, i,
                                        &sampler->dynamic_state.static_state[i].texture_state,
                                        &sampler->dynamic_state.static_state[i].sampler_state,
                                        &sampler->dynamic_state.base);
      }
      lp_build_sample_array_fini_soa(&switch_info);
   } else {
      lp_build_sample_soa(&sampler->dynamic_state.static_state[texture_index].texture_state,
                          &sampler->dynamic_state.static_state[sampler_index].sampler_state,
                          &sampler->dynamic_state.base,
                          gallivm, params);
   }
}


/**
 * Fetch the texture size.
 */
static void
draw_llvm_sampler_soa_emit_size_query(const struct lp_build_sampler_soa *base,
                                      struct gallivm_state *gallivm,
                                      const struct lp_sampler_size_query_params *params)
{
   struct draw_llvm_sampler_soa *sampler = (struct draw_llvm_sampler_soa *)base;

   assert(params->texture_unit < PIPE_MAX_SHADER_SAMPLER_VIEWS);

   lp_build_size_query_soa(gallivm,
                           &sampler->dynamic_state.static_state[params->texture_unit].texture_state,
                           &sampler->dynamic_state.base,
                           params);
}

struct lp_build_sampler_soa *
draw_llvm_sampler_soa_create(const struct draw_sampler_static_state *static_state,
                             unsigned nr_samplers)
{
   struct draw_llvm_sampler_soa *sampler;

   sampler = CALLOC_STRUCT(draw_llvm_sampler_soa);
   if (!sampler)
      return NULL;

   sampler->base.destroy = draw_llvm_sampler_soa_destroy;
   sampler->base.emit_tex_sample = draw_llvm_sampler_soa_emit_fetch_texel;
   sampler->base.emit_size_query = draw_llvm_sampler_soa_emit_size_query;
   sampler->dynamic_state.base.width = draw_llvm_texture_width;
   sampler->dynamic_state.base.height = draw_llvm_texture_height;
   sampler->dynamic_state.base.depth = draw_llvm_texture_depth;
   sampler->dynamic_state.base.first_level = draw_llvm_texture_first_level;
   sampler->dynamic_state.base.last_level = draw_llvm_texture_last_level;
   sampler->dynamic_state.base.row_stride = draw_llvm_texture_row_stride;
   sampler->dynamic_state.base.img_stride = draw_llvm_texture_img_stride;
   sampler->dynamic_state.base.base_ptr = draw_llvm_texture_base_ptr;
   sampler->dynamic_state.base.mip_offsets = draw_llvm_texture_mip_offsets;
   sampler->dynamic_state.base.num_samples = draw_llvm_texture_num_samples;
   sampler->dynamic_state.base.sample_stride = draw_llvm_texture_sample_stride;
   sampler->dynamic_state.base.min_lod = draw_llvm_sampler_min_lod;
   sampler->dynamic_state.base.max_lod = draw_llvm_sampler_max_lod;
   sampler->dynamic_state.base.lod_bias = draw_llvm_sampler_lod_bias;
   sampler->dynamic_state.base.border_color = draw_llvm_sampler_border_color;
   sampler->dynamic_state.static_state = static_state;

   sampler->nr_samplers = nr_samplers;
   return &sampler->base;
}

static void
draw_llvm_image_soa_emit_op(const struct lp_build_image_soa *base,
                            struct gallivm_state *gallivm,
                            const struct lp_img_params *params)
{
   struct draw_llvm_image_soa *image = (struct draw_llvm_image_soa *)base;
   unsigned image_index = params->image_index;
   assert(image_index < PIPE_MAX_SHADER_IMAGES);

   if (params->image_index_offset) {
      struct lp_build_img_op_array_switch switch_info;
      memset(&switch_info, 0, sizeof(switch_info));
      LLVMValueRef unit = LLVMBuildAdd(gallivm->builder, params->image_index_offset,
                                       lp_build_const_int32(gallivm, image_index), "");
      lp_build_image_op_switch_soa(&switch_info, gallivm, params,
                                   unit, 0, image->nr_images);

      for (unsigned i = 0; i < image->nr_images; i++) {
         lp_build_image_op_array_case(&switch_info, i,
                                      &image->dynamic_state.static_state[i].image_state,
                                      &image->dynamic_state.base);
      }
      lp_build_image_op_array_fini_soa(&switch_info);
      return;
   }
   lp_build_img_op_soa(&image->dynamic_state.static_state[image_index].image_state,
                       &image->dynamic_state.base,
                       gallivm, params, params->outdata);
}
/**
 * Fetch the texture size.
 */
static void
draw_llvm_image_soa_emit_size_query(const struct lp_build_image_soa *base,
                                    struct gallivm_state *gallivm,
                                    const struct lp_sampler_size_query_params *params)
{
   struct draw_llvm_image_soa *image = (struct draw_llvm_image_soa *)base;

   assert(params->texture_unit < PIPE_MAX_SHADER_IMAGES);

   lp_build_size_query_soa(gallivm,
                           &image->dynamic_state.static_state[params->texture_unit].image_state,
                           &image->dynamic_state.base,
                           params);
}
static void
draw_llvm_image_soa_destroy(struct lp_build_image_soa *image)
{
   FREE(image);
}

struct lp_build_image_soa *
draw_llvm_image_soa_create(const struct draw_image_static_state *static_state,
                           unsigned nr_images)
{
   struct draw_llvm_image_soa *image;

   image = CALLOC_STRUCT(draw_llvm_image_soa);
   if (!image)
      return NULL;

   image->base.destroy = draw_llvm_image_soa_destroy;
   image->base.emit_op = draw_llvm_image_soa_emit_op;
   image->base.emit_size_query = draw_llvm_image_soa_emit_size_query;

   image->dynamic_state.base.width = draw_llvm_image_width;
   image->dynamic_state.base.height = draw_llvm_image_height;

   image->dynamic_state.base.depth = draw_llvm_image_depth;
   image->dynamic_state.base.base_ptr = draw_llvm_image_base_ptr;
   image->dynamic_state.base.row_stride = draw_llvm_image_row_stride;
   image->dynamic_state.base.img_stride = draw_llvm_image_img_stride;
   image->dynamic_state.base.num_samples = draw_llvm_image_num_samples;
   image->dynamic_state.base.sample_stride = draw_llvm_image_sample_stride;

   image->dynamic_state.static_state = static_state;

   image->nr_images = nr_images;
   return &image->base;
}
