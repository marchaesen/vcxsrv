/**************************************************************************
 *
 * Copyright 2010-2021 VMware, Inc.
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

#include <limits.h>
#include "util/simple_list.h"

#include "pipe/p_defines.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_pointer.h"
#include "util/format/u_format.h"
#include "util/u_dump.h"
#include "util/u_string.h"
#include "util/os_time.h"
#include "pipe/p_shader_tokens.h"
#include "draw/draw_context.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_scan.h"
#include "tgsi/tgsi_parse.h"
#include "gallivm/lp_bld_type.h"
#include "gallivm/lp_bld_const.h"
#include "gallivm/lp_bld_conv.h"
#include "gallivm/lp_bld_init.h"
#include "gallivm/lp_bld_intr.h"
#include "gallivm/lp_bld_logic.h"
#include "gallivm/lp_bld_tgsi.h"
#include "gallivm/lp_bld_swizzle.h"
#include "gallivm/lp_bld_flow.h"
#include "gallivm/lp_bld_printf.h"
#include "gallivm/lp_bld_debug.h"

#include "lp_bld_alpha.h"
#include "lp_bld_blend.h"
#include "lp_bld_depth.h"
#include "lp_bld_interp.h"
#include "lp_context.h"
#include "lp_debug.h"
#include "lp_perf.h"
#include "lp_screen.h"
#include "lp_setup.h"
#include "lp_state.h"
#include "lp_tex_sample.h"
#include "lp_flush.h"
#include "lp_state_fs.h"


/**
 * Sampler.
 */
struct linear_sampler
{
   struct lp_build_sampler_aos base;

   LLVMValueRef texels_ptrs[LP_MAX_LINEAR_TEXTURES];

   LLVMValueRef counter;

   unsigned instance;
};


/**
 * Provide texels to the TGSI translation.
 *
 * We don't actually do any texture sampling here, but simply hand the
 * precomputed row of texels.
 */
static LLVMValueRef
emit_fetch_texel_linear(const struct lp_build_sampler_aos *base,
                        struct lp_build_context *bld,
                        unsigned target, /* TGSI_TEXTURE_* */
                        unsigned unit,
                        LLVMValueRef coords,
                        const struct lp_derivatives derivs,
                        enum lp_build_tex_modifier modifier)
{
   struct linear_sampler *sampler = (struct linear_sampler *)base;
   LLVMValueRef texels_ptr;
   LLVMValueRef texel;

   if (sampler->instance >= LP_MAX_LINEAR_TEXTURES) {
      assert(FALSE);
      return bld->undef;
   }

   /* Pointer to a row of texels */
   texels_ptr = sampler->texels_ptrs[sampler->instance];

   texel = lp_build_pointer_get(bld->gallivm->builder, texels_ptr,
                                sampler->counter);
   assert(LLVMTypeOf(texel) == bld->vec_type);

   /*
    * We have a struct lp_linear_sampler instance per TEX instruction,
    * _not_ per unit, as each TEX intruction will need separate storage for the
    * texels.
    */
   (void)unit;
   ++sampler->instance;

   return texel;
}

/**
 * Generates the main body of the fragment shader
 * Supports generating code for 4 pixel blocks and individual pixels
 */
static LLVMValueRef
llvm_fragment_body(struct lp_build_context *bld,
                   struct lp_fragment_shader *shader,
                   struct lp_fragment_shader_variant *variant,
                   struct linear_sampler* sampler,
                   LLVMValueRef *inputs_ptrs,
                   LLVMValueRef consts_ptr,
                   LLVMValueRef blend_color,
                   LLVMValueRef alpha_ref,
                   struct lp_type fs_type,
                   LLVMValueRef dst)
{
   const unsigned char bgra_swizzles[4] = {2, 1, 0, 3};

   LLVMValueRef inputs[PIPE_MAX_SHADER_INPUTS];
   LLVMValueRef outputs[PIPE_MAX_SHADER_OUTPUTS];

   LLVMBuilderRef builder = bld->gallivm->builder;
   struct gallivm_state *gallivm = bld->gallivm;

   LLVMValueRef src1 = lp_build_zero(gallivm, fs_type);

   LLVMValueRef result = NULL;
   unsigned i;

   sampler->instance = 0;


   /*
    * Advance inputs
    */

   for (i = 0; i < shader->info.base.num_inputs; ++i) {
      LLVMValueRef inputs_ptr;
      LLVMValueRef input;

      inputs_ptr = inputs_ptrs[i];

      input = lp_build_pointer_get(builder, inputs_ptr, sampler->counter);
      assert(LLVMTypeOf(input) == bld->vec_type);

      inputs[i] = input;
   }

   for ( ; i < PIPE_MAX_SHADER_INPUTS; ++i) {
      inputs[i] = bld->undef;
   }


   /*
    * Translate the TGSI
    */

   for (i = 0; i < PIPE_MAX_SHADER_OUTPUTS; ++i) {
      outputs[i] = bld->undef;
   }

   lp_build_tgsi_aos(gallivm, shader->base.tokens, fs_type,
                     bgra_swizzles,
                     consts_ptr, inputs, outputs,
                     &sampler->base,
                     &shader->info.base);

   /*
    * Blend output color
    */

   for (i = 0; i < shader->info.base.num_outputs; ++i) {
      LLVMValueRef mask = NULL;
      LLVMValueRef output;
      unsigned cbuf;

      if (!outputs[i])
         continue;

      output = LLVMBuildLoad(builder, outputs[i], "");
      lp_build_name(output, "output%u", i);

      cbuf = shader->info.base.output_semantic_index[i];
      lp_build_name(output, "cbuf%u", cbuf);

      if (shader->info.base.output_semantic_name[i] != TGSI_SEMANTIC_COLOR || cbuf != 0)
         continue;

      /* Perform alpha test if necessary */
      if (variant->key.alpha.enabled) {
         LLVMTypeRef vec_type = lp_build_vec_type(gallivm, fs_type);
         LLVMValueRef broadcast_alpha = lp_build_broadcast(gallivm, vec_type, alpha_ref);

         mask = lp_build_cmp(bld, variant->key.alpha.func, output, broadcast_alpha);
         /* XXX is 4 correct? */
         mask = lp_build_swizzle_scalar_aos(bld, mask, bgra_swizzles[3], 4);

         lp_build_name(mask, "alpha_test_mask");
      }

      result = lp_build_blend_aos(gallivm,
                                  &variant->key.blend,
                                  variant->key.cbuf_format[i],
                                  fs_type,
                                  cbuf,   /* rt */
                                  output, /* src */
                                  NULL,   /* src_alpha */
                                  src1,   /* src1 */
                                  NULL,   /* src1_alpha */
                                  dst,
                                  mask,
                                  blend_color,  /* const_ */
                                  NULL,         /* const_alpha */
                                  bgra_swizzles,
                                  4);
   }

   return result;
}

/**
 * Generate a function that executes the fragment shader in a linear fashion.
 */
void
llvmpipe_fs_variant_linear_llvm(struct llvmpipe_context *lp,
                                struct lp_fragment_shader *shader,
                                struct lp_fragment_shader_variant *variant)
{
   struct gallivm_state *gallivm = variant->gallivm;
   char func_name[256];
   struct lp_type fs_type;
   LLVMTypeRef ret_type;
   LLVMTypeRef arg_types[4];
   LLVMTypeRef func_type;
   LLVMValueRef context_ptr;
   LLVMValueRef x;
   LLVMValueRef y;
   LLVMValueRef width;
   LLVMValueRef excess;
   LLVMValueRef function;
   LLVMBasicBlockRef block;
   LLVMBuilderRef builder;
   struct lp_build_context bld;
   struct linear_sampler sampler;
   struct lp_build_if_state ifstate;
   struct lp_build_for_loop_state loop;

   LLVMValueRef consts_ptr;
   LLVMValueRef interpolators_ptr;
   LLVMValueRef samplers_ptr;
   LLVMValueRef color0_ptr;
   LLVMValueRef blend_color;
   LLVMValueRef alpha_ref;

   LLVMValueRef inputs_ptrs[LP_MAX_LINEAR_INPUTS];

   LLVMTypeRef int8t = LLVMInt8TypeInContext(gallivm->context);
   LLVMTypeRef int32t = LLVMInt32TypeInContext(gallivm->context);
   LLVMTypeRef pint8t = LLVMPointerType(int8t, 0);
   LLVMTypeRef pixelt = LLVMVectorType(int32t, 4);

   unsigned attrib;
   unsigned i;

   memset(&fs_type, 0, sizeof fs_type);
   fs_type.floating = FALSE;
   fs_type.sign = FALSE;
   fs_type.norm = TRUE;
   fs_type.width = 8;
   fs_type.length = 16;

   if (LP_DEBUG & DEBUG_TGSI) {
      tgsi_dump(shader->base.tokens, 0);
   }

   /*
    * Generate the function prototype. Any change here must be reflected in
    * lp_jit.h's lp_jit_frag_func function pointer type, and vice-versa.
    */

   snprintf(func_name, sizeof(func_name), "fs%u_variant%u_linear",
            shader->no, variant->no);

   ret_type = pint8t;
   arg_types[0] = variant->jit_linear_context_ptr_type; /* context */
   arg_types[1] = int32t;                              /* x */
   arg_types[2] = int32t;                              /* y */
   arg_types[3] = int32t;                              /* width */

   func_type = LLVMFunctionType(ret_type, arg_types, ARRAY_SIZE(arg_types), 0);

   function = LLVMAddFunction(gallivm->module, func_name, func_type);
   LLVMSetFunctionCallConv(function, LLVMCCallConv);

   variant->linear_function = function;

   /* XXX: need to propagate noalias down into color param now we are
    * passing a pointer-to-pointer?
    */
   for (i = 0; i < ARRAY_SIZE(arg_types); ++i) {
      if (LLVMGetTypeKind(arg_types[i]) == LLVMPointerTypeKind) {
         lp_add_function_attr(function, i + 1, LP_FUNC_ATTR_NOALIAS);
      }
   }

   context_ptr  = LLVMGetParam(function, 0);
   x            = LLVMGetParam(function, 1);
   y            = LLVMGetParam(function, 2);
   width        = LLVMGetParam(function, 3);

   lp_build_name(context_ptr, "context");
   lp_build_name(x, "x");
   lp_build_name(y, "y");
   lp_build_name(width, "width");

   /*
    * Function body
    */

   block = LLVMAppendBasicBlockInContext(gallivm->context, function, "entry");
   builder = gallivm->builder;

   LLVMPositionBuilderAtEnd(builder, block);

   lp_build_context_init(&bld, gallivm, fs_type);

   /*
    * Get context data
    */

   consts_ptr = lp_jit_linear_context_constants(gallivm, context_ptr);
   interpolators_ptr = lp_jit_linear_context_inputs(gallivm, context_ptr);
   samplers_ptr = lp_jit_linear_context_tex(gallivm, context_ptr);

   color0_ptr = lp_jit_linear_context_color0(gallivm, context_ptr);
   color0_ptr = LLVMBuildLoad(builder, color0_ptr, "");
   color0_ptr = LLVMBuildBitCast(builder, color0_ptr, LLVMPointerType(bld.vec_type, 0), "");

   blend_color = lp_jit_linear_context_blend_color(gallivm, context_ptr);
   blend_color = LLVMBuildLoad(builder, blend_color, "");
   blend_color = lp_build_broadcast(gallivm, LLVMVectorType(int32t, 4), blend_color);
   blend_color = LLVMBuildBitCast(builder, blend_color, LLVMVectorType(int8t, 16), "");

   alpha_ref = lp_jit_linear_context_alpha_ref(gallivm, context_ptr);
   alpha_ref = LLVMBuildLoad(builder, alpha_ref, "");

   /*
    * Invoke the input interpolators
    */

   for (attrib = 0; attrib < shader->info.base.num_inputs; ++attrib) {
      LLVMValueRef index;
      LLVMValueRef elem;
      LLVMValueRef fetch_ptr;
      LLVMValueRef inputs_ptr;

      assert(attrib < LP_MAX_LINEAR_INPUTS);
      if (attrib >= LP_MAX_LINEAR_INPUTS) {
         break;
      }

      index = LLVMConstInt(int32t, attrib, 0);

      elem = lp_build_array_get(bld.gallivm, interpolators_ptr, index);
      assert(LLVMGetTypeKind(LLVMTypeOf(elem)) == LLVMPointerTypeKind);

      fetch_ptr = lp_build_pointer_get(builder, elem,
                                       LLVMConstInt(int32t, 0, 0));
      assert(LLVMGetTypeKind(LLVMTypeOf(fetch_ptr)) == LLVMPointerTypeKind);

      /* Pointer to a row of interpolated inputs */
      elem = LLVMBuildBitCast(builder, elem, pint8t, "");
      inputs_ptr = LLVMBuildCall(builder, fetch_ptr, &elem, 1, "");
      assert(LLVMGetTypeKind(LLVMTypeOf(inputs_ptr)) == LLVMPointerTypeKind);

      /* Mark the function read-only so that LLVM can optimize it away */
      lp_add_function_attr(inputs_ptr, -1, LP_FUNC_ATTR_READONLY);
      lp_add_function_attr(inputs_ptr, -1, LP_FUNC_ATTR_NOUNWIND);

      lp_build_name(inputs_ptr, "input%u_ptr", attrib);

      inputs_ptrs[attrib] = inputs_ptr;
   }

   /*
    * Invoke and hook up the texture samplers.
    */

   memset(&sampler, 0, sizeof sampler);
   sampler.base.emit_fetch_texel = &emit_fetch_texel_linear;

   for (attrib = 0; attrib < shader->info.num_texs; ++attrib) {
      LLVMValueRef index;
      LLVMValueRef elem;
      LLVMValueRef fetch_ptr;
      LLVMValueRef texels_ptr;

      assert(attrib < LP_MAX_LINEAR_TEXTURES);
      if (attrib >= LP_MAX_LINEAR_TEXTURES) {
         break;
      }

      index = LLVMConstInt(int32t, attrib, 0);

      elem = lp_build_array_get(bld.gallivm, samplers_ptr, index);
      assert(LLVMGetTypeKind(LLVMTypeOf(elem)) == LLVMPointerTypeKind);

      fetch_ptr = lp_build_pointer_get(builder, elem, LLVMConstInt(int32t, 0, 0));
      assert(LLVMGetTypeKind(LLVMTypeOf(fetch_ptr)) == LLVMPointerTypeKind);

      /* Pointer to a row of texels */
      elem = LLVMBuildBitCast(builder, elem, pint8t, "");
      texels_ptr = LLVMBuildCall(builder, fetch_ptr, &elem, 1, "");
      assert(LLVMGetTypeKind(LLVMTypeOf(texels_ptr)) == LLVMPointerTypeKind);

      /* Mark the function read-only so that LLVM can optimize it away */
      lp_add_function_attr(texels_ptr, -1, LP_FUNC_ATTR_READONLY);
      lp_add_function_attr(texels_ptr, -1, LP_FUNC_ATTR_NOUNWIND);

      lp_build_name(texels_ptr, "tex%u_ptr", attrib);

      sampler.texels_ptrs[attrib] = texels_ptr;
   }

   excess = LLVMBuildAnd(builder, width, LLVMConstInt(int32t, 3, 0), "");
   width = LLVMBuildLShr(builder, width, LLVMConstInt(int32t, 2, 0), "");

   /* Loop over blocks of 4 pixels */
   lp_build_for_loop_begin(&loop, gallivm, LLVMConstInt(int32t, 0, 0), LLVMIntULT, width, LLVMConstInt(int32t, 1, 0));
   {
      LLVMValueRef value;
      sampler.counter = loop.counter;

      /* Read 4 pixels */
      value = lp_build_pointer_get_unaligned(builder, color0_ptr, loop.counter, 4);

      /* Perform fragment shader body */
      value = llvm_fragment_body(&bld, shader, variant, &sampler, inputs_ptrs, consts_ptr, blend_color, alpha_ref, fs_type, value);

      /* Write 4 pixels */
      lp_build_pointer_set_unaligned(builder, color0_ptr, loop.counter, value, 4);
   }
   lp_build_for_loop_end(&loop);

   /* Compute the edge pixels (width % 4) */
   lp_build_if(&ifstate, gallivm, LLVMBuildICmp(builder, LLVMIntNE, excess, LLVMConstInt(int32t, 0, 0), ""));
   {
      struct lp_build_loop_state loop_read, loop_write;
      LLVMValueRef buf, elem, result, pixel_ptr;
      LLVMValueRef buf_ptr = lp_build_alloca(gallivm, pixelt, "");

      sampler.counter = width;

      /* Get the i32* pixel pointer from the <i16x8>* element pointer */
      pixel_ptr = LLVMBuildGEP(gallivm->builder, color0_ptr, &width, 1, "");
      pixel_ptr = LLVMBuildBitCast(gallivm->builder, pixel_ptr, LLVMPointerType(int32t, 0), "");

      /* Copy individual pixels from memory to local buffer */
      lp_build_loop_begin(&loop_read, gallivm, LLVMConstInt(int32t, 0, 0));
      {
         elem = lp_build_pointer_get(gallivm->builder, pixel_ptr, loop_read.counter);

         buf = LLVMBuildLoad(gallivm->builder, buf_ptr, "");
         buf = LLVMBuildInsertElement(builder, buf, elem, loop_read.counter, "");
         LLVMBuildStore(builder, buf, buf_ptr);
      }
      lp_build_loop_end_cond(&loop_read, excess, LLVMConstInt(int32t, 1, 0), LLVMIntUGE);

      /* Perform fragment shader body */
      buf = LLVMBuildLoad(gallivm->builder, buf_ptr, "");
      buf = LLVMBuildBitCast(builder, buf, bld.vec_type, "");

      result = llvm_fragment_body(&bld, shader, variant, &sampler, inputs_ptrs, consts_ptr, blend_color, alpha_ref, fs_type, buf);
      result = LLVMBuildBitCast(builder, result, pixelt, "");

      /* Write individual pixels from local buffer to the memory */
      lp_build_loop_begin(&loop_write, gallivm, LLVMConstInt(int32t, 0, 0));
      {
         elem = LLVMBuildExtractElement(builder, result, loop_write.counter, "");

         lp_build_pointer_set(gallivm->builder, pixel_ptr, loop_write.counter, elem);
      }
      lp_build_loop_end_cond(&loop_write, excess, LLVMConstInt(int32t, 1, 0), LLVMIntUGE);
   }
   lp_build_endif(&ifstate);


   color0_ptr = LLVMBuildBitCast(builder, color0_ptr, pint8t, "");

   LLVMBuildRet(builder, color0_ptr);

   gallivm_verify_function(gallivm, function);
}


