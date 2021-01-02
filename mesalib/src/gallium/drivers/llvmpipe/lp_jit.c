/**************************************************************************
 *
 * Copyright 2009 VMware, Inc.
 * All Rights Reserved.
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
 * @file
 * C - JIT interfaces
 *
 * @author Jose Fonseca <jfonseca@vmware.com>
 */

#include <llvm/Config/llvm-config.h>

#include "util/u_memory.h"
#include "gallivm/lp_bld_init.h"
#include "gallivm/lp_bld_debug.h"
#include "gallivm/lp_bld_format.h"
#include "lp_context.h"
#include "lp_jit.h"

static LLVMTypeRef
create_jit_texture_type(struct gallivm_state *gallivm)
{
   LLVMContextRef lc = gallivm->context;
   LLVMTypeRef texture_type;
   LLVMTypeRef elem_types[LP_JIT_TEXTURE_NUM_FIELDS];

   /* struct lp_jit_texture */
   elem_types[LP_JIT_TEXTURE_WIDTH]  =
   elem_types[LP_JIT_TEXTURE_HEIGHT] =
   elem_types[LP_JIT_TEXTURE_DEPTH] =
   elem_types[LP_JIT_TEXTURE_NUM_SAMPLES] =
   elem_types[LP_JIT_TEXTURE_SAMPLE_STRIDE] =
   elem_types[LP_JIT_TEXTURE_FIRST_LEVEL] =
   elem_types[LP_JIT_TEXTURE_LAST_LEVEL] = LLVMInt32TypeInContext(lc);
   elem_types[LP_JIT_TEXTURE_BASE] = LLVMPointerType(LLVMInt8TypeInContext(lc), 0);
   elem_types[LP_JIT_TEXTURE_ROW_STRIDE] =
   elem_types[LP_JIT_TEXTURE_IMG_STRIDE] =
   elem_types[LP_JIT_TEXTURE_MIP_OFFSETS] =
      LLVMArrayType(LLVMInt32TypeInContext(lc), LP_MAX_TEXTURE_LEVELS);

   texture_type = LLVMStructTypeInContext(lc, elem_types,
                                          ARRAY_SIZE(elem_types), 0);

   LP_CHECK_MEMBER_OFFSET(struct lp_jit_texture, width,
                          gallivm->target, texture_type,
                          LP_JIT_TEXTURE_WIDTH);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_texture, height,
                          gallivm->target, texture_type,
                          LP_JIT_TEXTURE_HEIGHT);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_texture, depth,
                          gallivm->target, texture_type,
                          LP_JIT_TEXTURE_DEPTH);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_texture, base,
                          gallivm->target, texture_type,
                          LP_JIT_TEXTURE_BASE);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_texture, row_stride,
                          gallivm->target, texture_type,
                          LP_JIT_TEXTURE_ROW_STRIDE);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_texture, img_stride,
                          gallivm->target, texture_type,
                          LP_JIT_TEXTURE_IMG_STRIDE);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_texture, first_level,
                          gallivm->target, texture_type,
                          LP_JIT_TEXTURE_FIRST_LEVEL);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_texture, last_level,
                          gallivm->target, texture_type,
                          LP_JIT_TEXTURE_LAST_LEVEL);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_texture, mip_offsets,
                          gallivm->target, texture_type,
                          LP_JIT_TEXTURE_MIP_OFFSETS);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_texture, num_samples,
                          gallivm->target, texture_type,
                          LP_JIT_TEXTURE_NUM_SAMPLES);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_texture, sample_stride,
                          gallivm->target, texture_type,
                          LP_JIT_TEXTURE_SAMPLE_STRIDE);
   LP_CHECK_STRUCT_SIZE(struct lp_jit_texture,
                        gallivm->target, texture_type);
   return texture_type;
}

static LLVMTypeRef
create_jit_sampler_type(struct gallivm_state *gallivm)
{
   LLVMContextRef lc = gallivm->context;
   LLVMTypeRef sampler_type;
   LLVMTypeRef elem_types[LP_JIT_SAMPLER_NUM_FIELDS];
   elem_types[LP_JIT_SAMPLER_MIN_LOD] =
   elem_types[LP_JIT_SAMPLER_MAX_LOD] =
   elem_types[LP_JIT_SAMPLER_LOD_BIAS] = LLVMFloatTypeInContext(lc);
   elem_types[LP_JIT_SAMPLER_BORDER_COLOR] =
      LLVMArrayType(LLVMFloatTypeInContext(lc), 4);

   sampler_type = LLVMStructTypeInContext(lc, elem_types,
                                          ARRAY_SIZE(elem_types), 0);

   LP_CHECK_MEMBER_OFFSET(struct lp_jit_sampler, min_lod,
                          gallivm->target, sampler_type,
                          LP_JIT_SAMPLER_MIN_LOD);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_sampler, max_lod,
                          gallivm->target, sampler_type,
                          LP_JIT_SAMPLER_MAX_LOD);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_sampler, lod_bias,
                          gallivm->target, sampler_type,
                          LP_JIT_SAMPLER_LOD_BIAS);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_sampler, border_color,
                          gallivm->target, sampler_type,
                          LP_JIT_SAMPLER_BORDER_COLOR);
   LP_CHECK_STRUCT_SIZE(struct lp_jit_sampler,
                        gallivm->target, sampler_type);
   return sampler_type;
}

static LLVMTypeRef
create_jit_image_type(struct gallivm_state *gallivm)
{
   LLVMContextRef lc = gallivm->context;
   LLVMTypeRef image_type;
   LLVMTypeRef elem_types[LP_JIT_IMAGE_NUM_FIELDS];
   elem_types[LP_JIT_IMAGE_WIDTH] =
   elem_types[LP_JIT_IMAGE_HEIGHT] =
   elem_types[LP_JIT_IMAGE_DEPTH] = LLVMInt32TypeInContext(lc);
   elem_types[LP_JIT_IMAGE_BASE] = LLVMPointerType(LLVMInt8TypeInContext(lc), 0);
   elem_types[LP_JIT_IMAGE_ROW_STRIDE] =
   elem_types[LP_JIT_IMAGE_IMG_STRIDE] =
   elem_types[LP_JIT_IMAGE_NUM_SAMPLES] =
   elem_types[LP_JIT_IMAGE_SAMPLE_STRIDE] = LLVMInt32TypeInContext(lc);

   image_type = LLVMStructTypeInContext(lc, elem_types,
                                        ARRAY_SIZE(elem_types), 0);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_image, width,
                          gallivm->target, image_type,
                          LP_JIT_IMAGE_WIDTH);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_image, height,
                          gallivm->target, image_type,
                          LP_JIT_IMAGE_HEIGHT);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_image, depth,
                          gallivm->target, image_type,
                          LP_JIT_IMAGE_DEPTH);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_image, base,
                          gallivm->target, image_type,
                          LP_JIT_IMAGE_BASE);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_image, row_stride,
                          gallivm->target, image_type,
                          LP_JIT_IMAGE_ROW_STRIDE);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_image, img_stride,
                          gallivm->target, image_type,
                          LP_JIT_IMAGE_IMG_STRIDE);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_image, num_samples,
                          gallivm->target, image_type,
                          LP_JIT_IMAGE_NUM_SAMPLES);
   LP_CHECK_MEMBER_OFFSET(struct lp_jit_image, sample_stride,
                          gallivm->target, image_type,
                          LP_JIT_IMAGE_SAMPLE_STRIDE);
   return image_type;
}

static void
lp_jit_create_types(struct lp_fragment_shader_variant *lp)
{
   struct gallivm_state *gallivm = lp->gallivm;
   LLVMContextRef lc = gallivm->context;
   LLVMTypeRef viewport_type, texture_type, sampler_type, image_type;

   /* struct lp_jit_viewport */
   {
      LLVMTypeRef elem_types[LP_JIT_VIEWPORT_NUM_FIELDS];

      elem_types[LP_JIT_VIEWPORT_MIN_DEPTH] =
      elem_types[LP_JIT_VIEWPORT_MAX_DEPTH] = LLVMFloatTypeInContext(lc);

      viewport_type = LLVMStructTypeInContext(lc, elem_types,
                                              ARRAY_SIZE(elem_types), 0);

      LP_CHECK_MEMBER_OFFSET(struct lp_jit_viewport, min_depth,
                             gallivm->target, viewport_type,
                             LP_JIT_VIEWPORT_MIN_DEPTH);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_viewport, max_depth,
                             gallivm->target, viewport_type,
                             LP_JIT_VIEWPORT_MAX_DEPTH);
      LP_CHECK_STRUCT_SIZE(struct lp_jit_viewport,
                           gallivm->target, viewport_type);
   }

   texture_type = create_jit_texture_type(gallivm);
   sampler_type = create_jit_sampler_type(gallivm);
   image_type = create_jit_image_type(gallivm);

   /* struct lp_jit_context */
   {
      LLVMTypeRef elem_types[LP_JIT_CTX_COUNT];
      LLVMTypeRef context_type;

      elem_types[LP_JIT_CTX_CONSTANTS] =
         LLVMArrayType(LLVMPointerType(LLVMFloatTypeInContext(lc), 0), LP_MAX_TGSI_CONST_BUFFERS);
      elem_types[LP_JIT_CTX_NUM_CONSTANTS] =
            LLVMArrayType(LLVMInt32TypeInContext(lc), LP_MAX_TGSI_CONST_BUFFERS);
      elem_types[LP_JIT_CTX_TEXTURES] = LLVMArrayType(texture_type,
                                                      PIPE_MAX_SHADER_SAMPLER_VIEWS);
      elem_types[LP_JIT_CTX_SAMPLERS] = LLVMArrayType(sampler_type,
                                                      PIPE_MAX_SAMPLERS);
      elem_types[LP_JIT_CTX_IMAGES] = LLVMArrayType(image_type,
                                                    PIPE_MAX_SHADER_IMAGES);
      elem_types[LP_JIT_CTX_ALPHA_REF] = LLVMFloatTypeInContext(lc);
      elem_types[LP_JIT_CTX_SAMPLE_MASK] =
      elem_types[LP_JIT_CTX_STENCIL_REF_FRONT] =
      elem_types[LP_JIT_CTX_STENCIL_REF_BACK] = LLVMInt32TypeInContext(lc);
      elem_types[LP_JIT_CTX_U8_BLEND_COLOR] = LLVMPointerType(LLVMInt8TypeInContext(lc), 0);
      elem_types[LP_JIT_CTX_F_BLEND_COLOR] = LLVMPointerType(LLVMFloatTypeInContext(lc), 0);
      elem_types[LP_JIT_CTX_VIEWPORTS] = LLVMPointerType(viewport_type, 0);
      elem_types[LP_JIT_CTX_SSBOS] =
         LLVMArrayType(LLVMPointerType(LLVMInt32TypeInContext(lc), 0), LP_MAX_TGSI_SHADER_BUFFERS);
      elem_types[LP_JIT_CTX_NUM_SSBOS] =
            LLVMArrayType(LLVMInt32TypeInContext(lc), LP_MAX_TGSI_SHADER_BUFFERS);
      context_type = LLVMStructTypeInContext(lc, elem_types,
                                             ARRAY_SIZE(elem_types), 0);

      LP_CHECK_MEMBER_OFFSET(struct lp_jit_context, constants,
                             gallivm->target, context_type,
                             LP_JIT_CTX_CONSTANTS);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_context, num_constants,
                             gallivm->target, context_type,
                             LP_JIT_CTX_NUM_CONSTANTS);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_context, textures,
                             gallivm->target, context_type,
                             LP_JIT_CTX_TEXTURES);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_context, samplers,
                             gallivm->target, context_type,
                             LP_JIT_CTX_SAMPLERS);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_context, images,
                             gallivm->target, context_type,
                             LP_JIT_CTX_IMAGES);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_context, alpha_ref_value,
                             gallivm->target, context_type,
                             LP_JIT_CTX_ALPHA_REF);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_context, stencil_ref_front,
                             gallivm->target, context_type,
                             LP_JIT_CTX_STENCIL_REF_FRONT);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_context, stencil_ref_back,
                             gallivm->target, context_type,
                             LP_JIT_CTX_STENCIL_REF_BACK);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_context, u8_blend_color,
                             gallivm->target, context_type,
                             LP_JIT_CTX_U8_BLEND_COLOR);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_context, f_blend_color,
                             gallivm->target, context_type,
                             LP_JIT_CTX_F_BLEND_COLOR);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_context, viewports,
                             gallivm->target, context_type,
                             LP_JIT_CTX_VIEWPORTS);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_context, ssbos,
                             gallivm->target, context_type,
                             LP_JIT_CTX_SSBOS);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_context, num_ssbos,
                             gallivm->target, context_type,
                             LP_JIT_CTX_NUM_SSBOS);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_context, sample_mask,
                             gallivm->target, context_type,
                             LP_JIT_CTX_SAMPLE_MASK);
      LP_CHECK_STRUCT_SIZE(struct lp_jit_context,
                           gallivm->target, context_type);

      lp->jit_context_ptr_type = LLVMPointerType(context_type, 0);
   }

   /* struct lp_jit_thread_data */
   {
      LLVMTypeRef elem_types[LP_JIT_THREAD_DATA_COUNT];
      LLVMTypeRef thread_data_type;

      elem_types[LP_JIT_THREAD_DATA_CACHE] =
            LLVMPointerType(lp_build_format_cache_type(gallivm), 0);
      elem_types[LP_JIT_THREAD_DATA_COUNTER] = LLVMInt64TypeInContext(lc);
      elem_types[LP_JIT_THREAD_DATA_INVOCATIONS] = LLVMInt64TypeInContext(lc);
      elem_types[LP_JIT_THREAD_DATA_RASTER_STATE_VIEWPORT_INDEX] =
            LLVMInt32TypeInContext(lc);

      thread_data_type = LLVMStructTypeInContext(lc, elem_types,
                                                 ARRAY_SIZE(elem_types), 0);

      lp->jit_thread_data_ptr_type = LLVMPointerType(thread_data_type, 0);
   }

   if (gallivm_debug & GALLIVM_DEBUG_IR) {
      char *str = LLVMPrintModuleToString(gallivm->module);
      fprintf(stderr, "%s", str);
      LLVMDisposeMessage(str);
   }
}


void
lp_jit_screen_cleanup(struct llvmpipe_screen *screen)
{
   /* nothing */
}


boolean
lp_jit_screen_init(struct llvmpipe_screen *screen)
{
   return lp_build_init();
}


void
lp_jit_init_types(struct lp_fragment_shader_variant *lp)
{
   if (!lp->jit_context_ptr_type)
      lp_jit_create_types(lp);
}

static void
lp_jit_create_cs_types(struct lp_compute_shader_variant *lp)
{
   struct gallivm_state *gallivm = lp->gallivm;
   LLVMContextRef lc = gallivm->context;
   LLVMTypeRef texture_type, sampler_type, image_type;

   texture_type = create_jit_texture_type(gallivm);
   sampler_type = create_jit_sampler_type(gallivm);
   image_type = create_jit_image_type(gallivm);

   /* struct lp_jit_cs_thread_data */
   {
      LLVMTypeRef elem_types[LP_JIT_CS_THREAD_DATA_COUNT];
      LLVMTypeRef thread_data_type;

      elem_types[LP_JIT_CS_THREAD_DATA_CACHE] =
            LLVMPointerType(lp_build_format_cache_type(gallivm), 0);

      elem_types[LP_JIT_CS_THREAD_DATA_SHARED] = LLVMPointerType(LLVMInt32TypeInContext(lc), 0);
      thread_data_type = LLVMStructTypeInContext(lc, elem_types,
                                                 ARRAY_SIZE(elem_types), 0);

      lp->jit_cs_thread_data_ptr_type = LLVMPointerType(thread_data_type, 0);
   }

   /* struct lp_jit_cs_context */
   {
      LLVMTypeRef elem_types[LP_JIT_CS_CTX_COUNT];
      LLVMTypeRef cs_context_type;

      elem_types[LP_JIT_CS_CTX_CONSTANTS] =
         LLVMArrayType(LLVMPointerType(LLVMFloatTypeInContext(lc), 0), LP_MAX_TGSI_CONST_BUFFERS);
      elem_types[LP_JIT_CS_CTX_NUM_CONSTANTS] =
            LLVMArrayType(LLVMInt32TypeInContext(lc), LP_MAX_TGSI_CONST_BUFFERS);
      elem_types[LP_JIT_CS_CTX_TEXTURES] = LLVMArrayType(texture_type,
                                                      PIPE_MAX_SHADER_SAMPLER_VIEWS);
      elem_types[LP_JIT_CS_CTX_SAMPLERS] = LLVMArrayType(sampler_type,
                                                      PIPE_MAX_SAMPLERS);
      elem_types[LP_JIT_CS_CTX_IMAGES] = LLVMArrayType(image_type,
                                                       PIPE_MAX_SHADER_IMAGES);
      elem_types[LP_JIT_CS_CTX_SSBOS] =
         LLVMArrayType(LLVMPointerType(LLVMInt32TypeInContext(lc), 0), LP_MAX_TGSI_SHADER_BUFFERS);
      elem_types[LP_JIT_CS_CTX_NUM_SSBOS] =
            LLVMArrayType(LLVMInt32TypeInContext(lc), LP_MAX_TGSI_SHADER_BUFFERS);

      elem_types[LP_JIT_CS_CTX_SHARED_SIZE] = LLVMInt32TypeInContext(lc);

      elem_types[LP_JIT_CS_CTX_KERNEL_ARGS] = LLVMPointerType(LLVMInt8TypeInContext(lc), 0);

      cs_context_type = LLVMStructTypeInContext(lc, elem_types,
                                             ARRAY_SIZE(elem_types), 0);

      LP_CHECK_MEMBER_OFFSET(struct lp_jit_cs_context, constants,
                             gallivm->target, cs_context_type,
                             LP_JIT_CS_CTX_CONSTANTS);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_cs_context, num_constants,
                             gallivm->target, cs_context_type,
                             LP_JIT_CS_CTX_NUM_CONSTANTS);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_cs_context, textures,
                             gallivm->target, cs_context_type,
                             LP_JIT_CS_CTX_TEXTURES);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_cs_context, samplers,
                             gallivm->target, cs_context_type,
                             LP_JIT_CS_CTX_SAMPLERS);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_cs_context, images,
                             gallivm->target, cs_context_type,
                             LP_JIT_CS_CTX_IMAGES);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_cs_context, ssbos,
                             gallivm->target, cs_context_type,
                             LP_JIT_CS_CTX_SSBOS);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_cs_context, num_ssbos,
                             gallivm->target, cs_context_type,
                             LP_JIT_CS_CTX_NUM_SSBOS);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_cs_context, shared_size,
                             gallivm->target, cs_context_type,
                             LP_JIT_CS_CTX_SHARED_SIZE);
      LP_CHECK_MEMBER_OFFSET(struct lp_jit_cs_context, kernel_args,
                             gallivm->target, cs_context_type,
                             LP_JIT_CS_CTX_KERNEL_ARGS);
      LP_CHECK_STRUCT_SIZE(struct lp_jit_cs_context,
                           gallivm->target, cs_context_type);

      lp->jit_cs_context_ptr_type = LLVMPointerType(cs_context_type, 0);
   }

   if (gallivm_debug & GALLIVM_DEBUG_IR) {
      char *str = LLVMPrintModuleToString(gallivm->module);
      fprintf(stderr, "%s", str);
      LLVMDisposeMessage(str);
   }
}

void
lp_jit_init_cs_types(struct lp_compute_shader_variant *lp)
{
   if (!lp->jit_cs_context_ptr_type)
      lp_jit_create_cs_types(lp);
}
