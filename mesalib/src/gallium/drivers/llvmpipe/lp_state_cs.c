/**************************************************************************
 *
 * Copyright 2019 Red Hat.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **************************************************************************/
#include "util/u_memory.h"
#include "util/simple_list.h"
#include "util/os_time.h"
#include "util/u_dump.h"
#include "util/u_string.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_parse.h"
#include "gallivm/lp_bld_const.h"
#include "gallivm/lp_bld_debug.h"
#include "gallivm/lp_bld_intr.h"
#include "gallivm/lp_bld_flow.h"
#include "gallivm/lp_bld_gather.h"
#include "gallivm/lp_bld_coro.h"
#include "gallivm/lp_bld_nir.h"
#include "lp_state_cs.h"
#include "lp_context.h"
#include "lp_debug.h"
#include "lp_state.h"
#include "lp_perf.h"
#include "lp_screen.h"
#include "lp_memory.h"
#include "lp_query.h"
#include "lp_cs_tpool.h"
#include "frontend/sw_winsys.h"
#include "nir/nir_to_tgsi_info.h"
#include "util/mesa-sha1.h"
#include "nir_serialize.h"

/** Fragment shader number (for debugging) */
static unsigned cs_no = 0;

struct lp_cs_job_info {
   unsigned grid_size[3];
   unsigned grid_base[3];
   unsigned block_size[3];
   unsigned req_local_mem;
   unsigned work_dim;
   struct lp_cs_exec *current;
};

static void
generate_compute(struct llvmpipe_context *lp,
                 struct lp_compute_shader *shader,
                 struct lp_compute_shader_variant *variant)
{
   struct gallivm_state *gallivm = variant->gallivm;
   const struct lp_compute_shader_variant_key *key = &variant->key;
   char func_name[64], func_name_coro[64];
   LLVMTypeRef arg_types[17];
   LLVMTypeRef func_type, coro_func_type;
   LLVMTypeRef int32_type = LLVMInt32TypeInContext(gallivm->context);
   LLVMValueRef context_ptr;
   LLVMValueRef x_size_arg, y_size_arg, z_size_arg;
   LLVMValueRef grid_x_arg, grid_y_arg, grid_z_arg;
   LLVMValueRef grid_size_x_arg, grid_size_y_arg, grid_size_z_arg;
   LLVMValueRef work_dim_arg, thread_data_ptr;
   LLVMBasicBlockRef block;
   LLVMBuilderRef builder;
   struct lp_build_sampler_soa *sampler;
   struct lp_build_image_soa *image;
   LLVMValueRef function, coro;
   struct lp_type cs_type;
   unsigned i;

   /*
    * This function has two parts
    * a) setup the coroutine execution environment loop.
    * b) build the compute shader llvm for use inside the coroutine.
    */
   assert(lp_native_vector_width / 32 >= 4);

   memset(&cs_type, 0, sizeof cs_type);
   cs_type.floating = TRUE;      /* floating point values */
   cs_type.sign = TRUE;          /* values are signed */
   cs_type.norm = FALSE;         /* values are not limited to [0,1] or [-1,1] */
   cs_type.width = 32;           /* 32-bit float */
   cs_type.length = MIN2(lp_native_vector_width / 32, 16); /* n*4 elements per vector */
   snprintf(func_name, sizeof(func_name), "cs_variant");

   snprintf(func_name_coro, sizeof(func_name), "cs_co_variant");

   arg_types[0] = variant->jit_cs_context_ptr_type;       /* context */
   arg_types[1] = int32_type;                          /* block_x_size */
   arg_types[2] = int32_type;                          /* block_y_size */
   arg_types[3] = int32_type;                          /* block_z_size */
   arg_types[4] = int32_type;                          /* grid_x */
   arg_types[5] = int32_type;                          /* grid_y */
   arg_types[6] = int32_type;                          /* grid_z */
   arg_types[7] = int32_type;                          /* grid_size_x */
   arg_types[8] = int32_type;                          /* grid_size_y */
   arg_types[9] = int32_type;                          /* grid_size_z */
   arg_types[10] = int32_type;                         /* work dim */
   arg_types[11] = variant->jit_cs_thread_data_ptr_type;  /* per thread data */
   arg_types[12] = int32_type;                         /* coro only - num X loops */
   arg_types[13] = int32_type;                         /* coro only - partials */
   arg_types[14] = int32_type;                         /* coro block_x_size */
   arg_types[15] = int32_type;                         /* coro block_y_size */
   arg_types[16] = int32_type;                         /* coro block_z_size */
   func_type = LLVMFunctionType(LLVMVoidTypeInContext(gallivm->context),
                                arg_types, ARRAY_SIZE(arg_types) - 5, 0);

   coro_func_type = LLVMFunctionType(LLVMPointerType(LLVMInt8TypeInContext(gallivm->context), 0),
                                     arg_types, ARRAY_SIZE(arg_types), 0);

   function = LLVMAddFunction(gallivm->module, func_name, func_type);
   LLVMSetFunctionCallConv(function, LLVMCCallConv);

   coro = LLVMAddFunction(gallivm->module, func_name_coro, coro_func_type);
   LLVMSetFunctionCallConv(coro, LLVMCCallConv);

   variant->function = function;

   for(i = 0; i < ARRAY_SIZE(arg_types); ++i) {
      if(LLVMGetTypeKind(arg_types[i]) == LLVMPointerTypeKind) {
         lp_add_function_attr(coro, i + 1, LP_FUNC_ATTR_NOALIAS);
         lp_add_function_attr(function, i + 1, LP_FUNC_ATTR_NOALIAS);
      }
   }

   lp_build_coro_declare_malloc_hooks(gallivm);

   if (variant->gallivm->cache->data_size)
      return;

   context_ptr  = LLVMGetParam(function, 0);
   x_size_arg = LLVMGetParam(function, 1);
   y_size_arg = LLVMGetParam(function, 2);
   z_size_arg = LLVMGetParam(function, 3);
   grid_x_arg = LLVMGetParam(function, 4);
   grid_y_arg = LLVMGetParam(function, 5);
   grid_z_arg = LLVMGetParam(function, 6);
   grid_size_x_arg = LLVMGetParam(function, 7);
   grid_size_y_arg = LLVMGetParam(function, 8);
   grid_size_z_arg = LLVMGetParam(function, 9);
   work_dim_arg = LLVMGetParam(function, 10);
   thread_data_ptr  = LLVMGetParam(function, 11);

   lp_build_name(context_ptr, "context");
   lp_build_name(x_size_arg, "x_size");
   lp_build_name(y_size_arg, "y_size");
   lp_build_name(z_size_arg, "z_size");
   lp_build_name(grid_x_arg, "grid_x");
   lp_build_name(grid_y_arg, "grid_y");
   lp_build_name(grid_z_arg, "grid_z");
   lp_build_name(grid_size_x_arg, "grid_size_x");
   lp_build_name(grid_size_y_arg, "grid_size_y");
   lp_build_name(grid_size_z_arg, "grid_size_z");
   lp_build_name(work_dim_arg, "work_dim");
   lp_build_name(thread_data_ptr, "thread_data");

   block = LLVMAppendBasicBlockInContext(gallivm->context, function, "entry");
   builder = gallivm->builder;
   assert(builder);
   LLVMPositionBuilderAtEnd(builder, block);
   sampler = lp_llvm_sampler_soa_create(key->samplers, key->nr_samplers);
   image = lp_llvm_image_soa_create(lp_cs_variant_key_images(key), key->nr_images);

   struct lp_build_loop_state loop_state[4];
   LLVMValueRef num_x_loop;
   LLVMValueRef vec_length = lp_build_const_int32(gallivm, cs_type.length);
   num_x_loop = LLVMBuildAdd(gallivm->builder, x_size_arg, vec_length, "");
   num_x_loop = LLVMBuildSub(gallivm->builder, num_x_loop, lp_build_const_int32(gallivm, 1), "");
   num_x_loop = LLVMBuildUDiv(gallivm->builder, num_x_loop, vec_length, "");
   LLVMValueRef partials = LLVMBuildURem(gallivm->builder, x_size_arg, vec_length, "");

   LLVMValueRef coro_num_hdls = LLVMBuildMul(gallivm->builder, num_x_loop, y_size_arg, "");
   coro_num_hdls = LLVMBuildMul(gallivm->builder, coro_num_hdls, z_size_arg, "");

   LLVMTypeRef hdl_ptr_type = LLVMPointerType(LLVMInt8TypeInContext(gallivm->context), 0);
   LLVMValueRef coro_hdls = LLVMBuildArrayAlloca(gallivm->builder, hdl_ptr_type, coro_num_hdls, "coro_hdls");

   unsigned end_coroutine = INT_MAX;

   /*
    * This is the main coroutine execution loop. It iterates over the dimensions
    * and calls the coroutine main entrypoint on the first pass, but in subsequent
    * passes it checks if the coroutine has completed and resumes it if not.
    */
   /* take x_width - round up to type.length width */
   lp_build_loop_begin(&loop_state[3], gallivm,
                       lp_build_const_int32(gallivm, 0)); /* coroutine reentry loop */
   lp_build_loop_begin(&loop_state[2], gallivm,
                       lp_build_const_int32(gallivm, 0)); /* z loop */
   lp_build_loop_begin(&loop_state[1], gallivm,
                       lp_build_const_int32(gallivm, 0)); /* y loop */
   lp_build_loop_begin(&loop_state[0], gallivm,
                       lp_build_const_int32(gallivm, 0)); /* x loop */
   {
      LLVMValueRef args[17];
      args[0] = context_ptr;
      args[1] = loop_state[0].counter;
      args[2] = loop_state[1].counter;
      args[3] = loop_state[2].counter;
      args[4] = grid_x_arg;
      args[5] = grid_y_arg;
      args[6] = grid_z_arg;
      args[7] = grid_size_x_arg;
      args[8] = grid_size_y_arg;
      args[9] = grid_size_z_arg;
      args[10] = work_dim_arg;
      args[11] = thread_data_ptr;
      args[12] = num_x_loop;
      args[13] = partials;
      args[14] = x_size_arg;
      args[15] = y_size_arg;
      args[16] = z_size_arg;

      /* idx = (z * (size_x * size_y) + y * size_x + x */
      LLVMValueRef coro_hdl_idx = LLVMBuildMul(gallivm->builder, loop_state[2].counter,
                                               LLVMBuildMul(gallivm->builder, num_x_loop, y_size_arg, ""), "");
      coro_hdl_idx = LLVMBuildAdd(gallivm->builder, coro_hdl_idx,
                                  LLVMBuildMul(gallivm->builder, loop_state[1].counter,
                                               num_x_loop, ""), "");
      coro_hdl_idx = LLVMBuildAdd(gallivm->builder, coro_hdl_idx,
                                  loop_state[0].counter, "");

      LLVMValueRef coro_entry = LLVMBuildGEP(gallivm->builder, coro_hdls, &coro_hdl_idx, 1, "");

      LLVMValueRef coro_hdl = LLVMBuildLoad(gallivm->builder, coro_entry, "coro_hdl");

      struct lp_build_if_state ifstate;
      LLVMValueRef cmp = LLVMBuildICmp(gallivm->builder, LLVMIntEQ, loop_state[3].counter,
                                       lp_build_const_int32(gallivm, 0), "");
      /* first time here - call the coroutine function entry point */
      lp_build_if(&ifstate, gallivm, cmp);
      LLVMValueRef coro_ret = LLVMBuildCall(gallivm->builder, coro, args, 17, "");
      LLVMBuildStore(gallivm->builder, coro_ret, coro_entry);
      lp_build_else(&ifstate);
      /* subsequent calls for this invocation - check if done. */
      LLVMValueRef coro_done = lp_build_coro_done(gallivm, coro_hdl);
      struct lp_build_if_state ifstate2;
      lp_build_if(&ifstate2, gallivm, coro_done);
      /* if done destroy and force loop exit */
      lp_build_coro_destroy(gallivm, coro_hdl);
      lp_build_loop_force_set_counter(&loop_state[3], lp_build_const_int32(gallivm, end_coroutine - 1));
      lp_build_else(&ifstate2);
      /* otherwise resume the coroutine */
      lp_build_coro_resume(gallivm, coro_hdl);
      lp_build_endif(&ifstate2);
      lp_build_endif(&ifstate);
      lp_build_loop_force_reload_counter(&loop_state[3]);
   }
   lp_build_loop_end_cond(&loop_state[0],
                          num_x_loop,
                          NULL,  LLVMIntUGE);
   lp_build_loop_end_cond(&loop_state[1],
                          y_size_arg,
                          NULL,  LLVMIntUGE);
   lp_build_loop_end_cond(&loop_state[2],
                          z_size_arg,
                          NULL,  LLVMIntUGE);
   lp_build_loop_end_cond(&loop_state[3],
                          lp_build_const_int32(gallivm, end_coroutine),
                          NULL, LLVMIntEQ);
   LLVMBuildRetVoid(builder);

   /* This is stage (b) - generate the compute shader code inside the coroutine. */
   LLVMValueRef block_x_size_arg, block_y_size_arg, block_z_size_arg;
   context_ptr  = LLVMGetParam(coro, 0);
   x_size_arg = LLVMGetParam(coro, 1);
   y_size_arg = LLVMGetParam(coro, 2);
   z_size_arg = LLVMGetParam(coro, 3);
   grid_x_arg = LLVMGetParam(coro, 4);
   grid_y_arg = LLVMGetParam(coro, 5);
   grid_z_arg = LLVMGetParam(coro, 6);
   grid_size_x_arg = LLVMGetParam(coro, 7);
   grid_size_y_arg = LLVMGetParam(coro, 8);
   grid_size_z_arg = LLVMGetParam(coro, 9);
   work_dim_arg = LLVMGetParam(coro, 10);
   thread_data_ptr  = LLVMGetParam(coro, 11);
   num_x_loop = LLVMGetParam(coro, 12);
   partials = LLVMGetParam(coro, 13);
   block_x_size_arg = LLVMGetParam(coro, 14);
   block_y_size_arg = LLVMGetParam(coro, 15);
   block_z_size_arg = LLVMGetParam(coro, 16);
   block = LLVMAppendBasicBlockInContext(gallivm->context, coro, "entry");
   LLVMPositionBuilderAtEnd(builder, block);
   {
      LLVMValueRef consts_ptr, num_consts_ptr;
      LLVMValueRef ssbo_ptr, num_ssbo_ptr;
      LLVMValueRef shared_ptr;
      LLVMValueRef kernel_args_ptr;
      struct lp_build_mask_context mask;
      struct lp_bld_tgsi_system_values system_values;

      memset(&system_values, 0, sizeof(system_values));
      consts_ptr = lp_jit_cs_context_constants(gallivm, context_ptr);
      num_consts_ptr = lp_jit_cs_context_num_constants(gallivm, context_ptr);
      ssbo_ptr = lp_jit_cs_context_ssbos(gallivm, context_ptr);
      num_ssbo_ptr = lp_jit_cs_context_num_ssbos(gallivm, context_ptr);
      kernel_args_ptr = lp_jit_cs_context_kernel_args(gallivm, context_ptr);

      shared_ptr = lp_jit_cs_thread_data_shared(gallivm, thread_data_ptr);

      /* these are coroutine entrypoint necessities */
      LLVMValueRef coro_id = lp_build_coro_id(gallivm);
      LLVMValueRef coro_hdl = lp_build_coro_begin_alloc_mem(gallivm, coro_id);

      LLVMValueRef has_partials = LLVMBuildICmp(gallivm->builder, LLVMIntNE, partials, lp_build_const_int32(gallivm, 0), "");
      LLVMValueRef tid_vals[3];
      LLVMValueRef tids_x[LP_MAX_VECTOR_LENGTH], tids_y[LP_MAX_VECTOR_LENGTH], tids_z[LP_MAX_VECTOR_LENGTH];
      LLVMValueRef base_val = LLVMBuildMul(gallivm->builder, x_size_arg, vec_length, "");
      for (i = 0; i < cs_type.length; i++) {
         tids_x[i] = LLVMBuildAdd(gallivm->builder, base_val, lp_build_const_int32(gallivm, i), "");
         tids_y[i] = y_size_arg;
         tids_z[i] = z_size_arg;
      }
      tid_vals[0] = lp_build_gather_values(gallivm, tids_x, cs_type.length);
      tid_vals[1] = lp_build_gather_values(gallivm, tids_y, cs_type.length);
      tid_vals[2] = lp_build_gather_values(gallivm, tids_z, cs_type.length);
      system_values.thread_id = LLVMGetUndef(LLVMArrayType(LLVMVectorType(int32_type, cs_type.length), 3));
      for (i = 0; i < 3; i++)
         system_values.thread_id = LLVMBuildInsertValue(builder, system_values.thread_id, tid_vals[i], i, "");

      LLVMValueRef gtids[3] = { grid_x_arg, grid_y_arg, grid_z_arg };
      system_values.block_id = LLVMGetUndef(LLVMVectorType(int32_type, 3));
      for (i = 0; i < 3; i++)
         system_values.block_id = LLVMBuildInsertElement(builder, system_values.block_id, gtids[i], lp_build_const_int32(gallivm, i), "");

      LLVMValueRef gstids[3] = { grid_size_x_arg, grid_size_y_arg, grid_size_z_arg };
      system_values.grid_size = LLVMGetUndef(LLVMVectorType(int32_type, 3));
      for (i = 0; i < 3; i++)
         system_values.grid_size = LLVMBuildInsertElement(builder, system_values.grid_size, gstids[i], lp_build_const_int32(gallivm, i), "");

      system_values.work_dim = work_dim_arg;

      LLVMValueRef bsize[3] = { block_x_size_arg, block_y_size_arg, block_z_size_arg };
      system_values.block_size = LLVMGetUndef(LLVMVectorType(int32_type, 3));
      for (i = 0; i < 3; i++)
         system_values.block_size = LLVMBuildInsertElement(builder, system_values.block_size, bsize[i], lp_build_const_int32(gallivm, i), "");

      LLVMValueRef last_x_loop = LLVMBuildICmp(gallivm->builder, LLVMIntEQ, x_size_arg, LLVMBuildSub(gallivm->builder, num_x_loop, lp_build_const_int32(gallivm, 1), ""), "");
      LLVMValueRef use_partial_mask = LLVMBuildAnd(gallivm->builder, last_x_loop, has_partials, "");
      struct lp_build_if_state if_state;
      LLVMValueRef mask_val = lp_build_alloca(gallivm, LLVMVectorType(int32_type, cs_type.length), "mask");
      LLVMValueRef full_mask_val = lp_build_const_int_vec(gallivm, cs_type, ~0);
      LLVMBuildStore(gallivm->builder, full_mask_val, mask_val);

      lp_build_if(&if_state, gallivm, use_partial_mask);
      struct lp_build_loop_state mask_loop_state;
      lp_build_loop_begin(&mask_loop_state, gallivm, partials);
      LLVMValueRef tmask_val = LLVMBuildLoad(gallivm->builder, mask_val, "");
      tmask_val = LLVMBuildInsertElement(gallivm->builder, tmask_val, lp_build_const_int32(gallivm, 0), mask_loop_state.counter, "");
      LLVMBuildStore(gallivm->builder, tmask_val, mask_val);
      lp_build_loop_end_cond(&mask_loop_state, vec_length, NULL, LLVMIntUGE);
      lp_build_endif(&if_state);

      mask_val = LLVMBuildLoad(gallivm->builder, mask_val, "");
      lp_build_mask_begin(&mask, gallivm, cs_type, mask_val);

      struct lp_build_coro_suspend_info coro_info;

      LLVMBasicBlockRef sus_block = LLVMAppendBasicBlockInContext(gallivm->context, coro, "suspend");
      LLVMBasicBlockRef clean_block = LLVMAppendBasicBlockInContext(gallivm->context, coro, "cleanup");

      coro_info.suspend = sus_block;
      coro_info.cleanup = clean_block;

      struct lp_build_tgsi_params params;
      memset(&params, 0, sizeof(params));

      params.type = cs_type;
      params.mask = &mask;
      params.consts_ptr = consts_ptr;
      params.const_sizes_ptr = num_consts_ptr;
      params.system_values = &system_values;
      params.context_ptr = context_ptr;
      params.sampler = sampler;
      params.info = &shader->info.base;
      params.ssbo_ptr = ssbo_ptr;
      params.ssbo_sizes_ptr = num_ssbo_ptr;
      params.image = image;
      params.shared_ptr = shared_ptr;
      params.coro = &coro_info;
      params.kernel_args = kernel_args_ptr;

      if (shader->base.type == PIPE_SHADER_IR_TGSI)
         lp_build_tgsi_soa(gallivm, shader->base.tokens, &params, NULL);
      else
         lp_build_nir_soa(gallivm, shader->base.ir.nir, &params,
                          NULL);

      mask_val = lp_build_mask_end(&mask);

      lp_build_coro_suspend_switch(gallivm, &coro_info, NULL, true);
      LLVMPositionBuilderAtEnd(builder, clean_block);

      lp_build_coro_free_mem(gallivm, coro_id, coro_hdl);

      LLVMBuildBr(builder, sus_block);
      LLVMPositionBuilderAtEnd(builder, sus_block);

      lp_build_coro_end(gallivm, coro_hdl);
      LLVMBuildRet(builder, coro_hdl);
   }

   sampler->destroy(sampler);
   image->destroy(image);

   gallivm_verify_function(gallivm, coro);
   gallivm_verify_function(gallivm, function);
}

static void *
llvmpipe_create_compute_state(struct pipe_context *pipe,
                                     const struct pipe_compute_state *templ)
{
   struct lp_compute_shader *shader;
   int nr_samplers, nr_sampler_views;

   shader = CALLOC_STRUCT(lp_compute_shader);
   if (!shader)
      return NULL;

   shader->no = cs_no++;

   shader->base.type = templ->ir_type;
   shader->req_local_mem = templ->req_local_mem;
   if (templ->ir_type == PIPE_SHADER_IR_NIR_SERIALIZED) {
      struct blob_reader reader;
      const struct pipe_binary_program_header *hdr = templ->prog;

      blob_reader_init(&reader, hdr->blob, hdr->num_bytes);
      shader->base.ir.nir = nir_deserialize(NULL, pipe->screen->get_compiler_options(pipe->screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE), &reader);
      shader->base.type = PIPE_SHADER_IR_NIR;

      pipe->screen->finalize_nir(pipe->screen, shader->base.ir.nir, false);
      shader->req_local_mem += ((struct nir_shader *)shader->base.ir.nir)->info.cs.shared_size;
   } else if (templ->ir_type == PIPE_SHADER_IR_NIR)
      shader->base.ir.nir = (struct nir_shader *)templ->prog;

   if (shader->base.type == PIPE_SHADER_IR_TGSI) {
      /* get/save the summary info for this shader */
      lp_build_tgsi_info(templ->prog, &shader->info);

      /* we need to keep a local copy of the tokens */
      shader->base.tokens = tgsi_dup_tokens(templ->prog);
   } else {
      nir_tgsi_scan_shader(shader->base.ir.nir, &shader->info.base, false);
   }

   make_empty_list(&shader->variants);

   nr_samplers = shader->info.base.file_max[TGSI_FILE_SAMPLER] + 1;
   nr_sampler_views = shader->info.base.file_max[TGSI_FILE_SAMPLER_VIEW] + 1;
   int nr_images = shader->info.base.file_max[TGSI_FILE_IMAGE] + 1;
   shader->variant_key_size = lp_cs_variant_key_size(MAX2(nr_samplers, nr_sampler_views), nr_images);

   return shader;
}

static void
llvmpipe_bind_compute_state(struct pipe_context *pipe,
                            void *cs)
{
   struct llvmpipe_context *llvmpipe = llvmpipe_context(pipe);

   if (llvmpipe->cs == cs)
      return;

   llvmpipe->cs = (struct lp_compute_shader *)cs;
   llvmpipe->cs_dirty |= LP_CSNEW_CS;
}

/**
 * Remove shader variant from two lists: the shader's variant list
 * and the context's variant list.
 */
static void
llvmpipe_remove_cs_shader_variant(struct llvmpipe_context *lp,
                                  struct lp_compute_shader_variant *variant)
{
   if ((LP_DEBUG & DEBUG_CS) || (gallivm_debug & GALLIVM_DEBUG_IR)) {
      debug_printf("llvmpipe: del cs #%u var %u v created %u v cached %u "
                   "v total cached %u inst %u total inst %u\n",
                   variant->shader->no, variant->no,
                   variant->shader->variants_created,
                   variant->shader->variants_cached,
                   lp->nr_cs_variants, variant->nr_instrs, lp->nr_cs_instrs);
   }

   gallivm_destroy(variant->gallivm);

   /* remove from shader's list */
   remove_from_list(&variant->list_item_local);
   variant->shader->variants_cached--;

   /* remove from context's list */
   remove_from_list(&variant->list_item_global);
   lp->nr_cs_variants--;
   lp->nr_cs_instrs -= variant->nr_instrs;

   FREE(variant);
}

static void
llvmpipe_delete_compute_state(struct pipe_context *pipe,
                              void *cs)
{
   struct llvmpipe_context *llvmpipe = llvmpipe_context(pipe);
   struct lp_compute_shader *shader = cs;
   struct lp_cs_variant_list_item *li;

   if (llvmpipe->cs == cs)
      llvmpipe->cs = NULL;
   for (unsigned i = 0; i < shader->max_global_buffers; i++)
      pipe_resource_reference(&shader->global_buffers[i], NULL);
   FREE(shader->global_buffers);

   /* Delete all the variants */
   li = first_elem(&shader->variants);
   while(!at_end(&shader->variants, li)) {
      struct lp_cs_variant_list_item *next = next_elem(li);
      llvmpipe_remove_cs_shader_variant(llvmpipe, li->base);
      li = next;
   }
   if (shader->base.ir.nir)
      ralloc_free(shader->base.ir.nir);
   tgsi_free_tokens(shader->base.tokens);
   FREE(shader);
}

static struct lp_compute_shader_variant_key *
make_variant_key(struct llvmpipe_context *lp,
                 struct lp_compute_shader *shader,
                 char *store)
{
   int i;
   struct lp_compute_shader_variant_key *key;
   key = (struct lp_compute_shader_variant_key *)store;
   memset(key, 0, offsetof(struct lp_compute_shader_variant_key, samplers[1]));

   /* This value will be the same for all the variants of a given shader:
    */
   key->nr_samplers = shader->info.base.file_max[TGSI_FILE_SAMPLER] + 1;

   struct lp_sampler_static_state *cs_sampler;

   cs_sampler = key->samplers;
   for(i = 0; i < key->nr_samplers; ++i) {
      if(shader->info.base.file_mask[TGSI_FILE_SAMPLER] & (1 << i)) {
         lp_sampler_static_sampler_state(&cs_sampler[i].sampler_state,
                                         lp->samplers[PIPE_SHADER_COMPUTE][i]);
      }
   }

   /*
    * XXX If TGSI_FILE_SAMPLER_VIEW exists assume all texture opcodes
    * are dx10-style? Can't really have mixed opcodes, at least not
    * if we want to skip the holes here (without rescanning tgsi).
    */
   if (shader->info.base.file_max[TGSI_FILE_SAMPLER_VIEW] != -1) {
      key->nr_sampler_views = shader->info.base.file_max[TGSI_FILE_SAMPLER_VIEW] + 1;
      for(i = 0; i < key->nr_sampler_views; ++i) {
         /*
          * Note sview may exceed what's representable by file_mask.
          * This will still work, the only downside is that not actually
          * used views may be included in the shader key.
          */
         if(shader->info.base.file_mask[TGSI_FILE_SAMPLER_VIEW] & (1u << (i & 31))) {
            lp_sampler_static_texture_state(&cs_sampler[i].texture_state,
                                            lp->sampler_views[PIPE_SHADER_COMPUTE][i]);
         }
      }
   }
   else {
      key->nr_sampler_views = key->nr_samplers;
      for(i = 0; i < key->nr_sampler_views; ++i) {
         if(shader->info.base.file_mask[TGSI_FILE_SAMPLER] & (1 << i)) {
            lp_sampler_static_texture_state(&cs_sampler[i].texture_state,
                                            lp->sampler_views[PIPE_SHADER_COMPUTE][i]);
         }
      }
   }

   struct lp_image_static_state *lp_image;
   lp_image = lp_cs_variant_key_images(key);
   key->nr_images = shader->info.base.file_max[TGSI_FILE_IMAGE] + 1;
   for (i = 0; i < key->nr_images; ++i) {
      if (shader->info.base.file_mask[TGSI_FILE_IMAGE] & (1 << i)) {
         lp_sampler_static_texture_state_image(&lp_image[i].image_state,
                                               &lp->images[PIPE_SHADER_COMPUTE][i]);
      }
   }
   return key;
}

static void
dump_cs_variant_key(const struct lp_compute_shader_variant_key *key)
{
   int i;
   debug_printf("cs variant %p:\n", (void *) key);

   for (i = 0; i < key->nr_samplers; ++i) {
      const struct lp_static_sampler_state *sampler = &key->samplers[i].sampler_state;
      debug_printf("sampler[%u] = \n", i);
      debug_printf("  .wrap = %s %s %s\n",
                   util_str_tex_wrap(sampler->wrap_s, TRUE),
                   util_str_tex_wrap(sampler->wrap_t, TRUE),
                   util_str_tex_wrap(sampler->wrap_r, TRUE));
      debug_printf("  .min_img_filter = %s\n",
                   util_str_tex_filter(sampler->min_img_filter, TRUE));
      debug_printf("  .min_mip_filter = %s\n",
                   util_str_tex_mipfilter(sampler->min_mip_filter, TRUE));
      debug_printf("  .mag_img_filter = %s\n",
                   util_str_tex_filter(sampler->mag_img_filter, TRUE));
      if (sampler->compare_mode != PIPE_TEX_COMPARE_NONE)
         debug_printf("  .compare_func = %s\n", util_str_func(sampler->compare_func, TRUE));
      debug_printf("  .normalized_coords = %u\n", sampler->normalized_coords);
      debug_printf("  .min_max_lod_equal = %u\n", sampler->min_max_lod_equal);
      debug_printf("  .lod_bias_non_zero = %u\n", sampler->lod_bias_non_zero);
      debug_printf("  .apply_min_lod = %u\n", sampler->apply_min_lod);
      debug_printf("  .apply_max_lod = %u\n", sampler->apply_max_lod);
   }
   for (i = 0; i < key->nr_sampler_views; ++i) {
      const struct lp_static_texture_state *texture = &key->samplers[i].texture_state;
      debug_printf("texture[%u] = \n", i);
      debug_printf("  .format = %s\n",
                   util_format_name(texture->format));
      debug_printf("  .target = %s\n",
                   util_str_tex_target(texture->target, TRUE));
      debug_printf("  .level_zero_only = %u\n",
                   texture->level_zero_only);
      debug_printf("  .pot = %u %u %u\n",
                   texture->pot_width,
                   texture->pot_height,
                   texture->pot_depth);
   }
   struct lp_image_static_state *images = lp_cs_variant_key_images(key);
   for (i = 0; i < key->nr_images; ++i) {
      const struct lp_static_texture_state *image = &images[i].image_state;
      debug_printf("image[%u] = \n", i);
      debug_printf("  .format = %s\n",
                   util_format_name(image->format));
      debug_printf("  .target = %s\n",
                   util_str_tex_target(image->target, TRUE));
      debug_printf("  .level_zero_only = %u\n",
                   image->level_zero_only);
      debug_printf("  .pot = %u %u %u\n",
                   image->pot_width,
                   image->pot_height,
                   image->pot_depth);
   }
}

static void
lp_debug_cs_variant(const struct lp_compute_shader_variant *variant)
{
   debug_printf("llvmpipe: Compute shader #%u variant #%u:\n",
                variant->shader->no, variant->no);
   if (variant->shader->base.type == PIPE_SHADER_IR_TGSI)
      tgsi_dump(variant->shader->base.tokens, 0);
   else
      nir_print_shader(variant->shader->base.ir.nir, stderr);
   dump_cs_variant_key(&variant->key);
   debug_printf("\n");
}

static void
lp_cs_get_ir_cache_key(struct lp_compute_shader_variant *variant,
                       unsigned char ir_sha1_cache_key[20])
{
   struct blob blob = { 0 };
   unsigned ir_size;
   void *ir_binary;

   blob_init(&blob);
   nir_serialize(&blob, variant->shader->base.ir.nir, true);
   ir_binary = blob.data;
   ir_size = blob.size;

   struct mesa_sha1 ctx;
   _mesa_sha1_init(&ctx);
   _mesa_sha1_update(&ctx, &variant->key, variant->shader->variant_key_size);
   _mesa_sha1_update(&ctx, ir_binary, ir_size);
   _mesa_sha1_final(&ctx, ir_sha1_cache_key);

   blob_finish(&blob);
}

static struct lp_compute_shader_variant *
generate_variant(struct llvmpipe_context *lp,
                 struct lp_compute_shader *shader,
                 const struct lp_compute_shader_variant_key *key)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(lp->pipe.screen);
   struct lp_compute_shader_variant *variant;
   char module_name[64];
   unsigned char ir_sha1_cache_key[20];
   struct lp_cached_code cached = { 0 };
   bool needs_caching = false;
   variant = MALLOC(sizeof *variant + shader->variant_key_size - sizeof variant->key);
   if (!variant)
      return NULL;

   memset(variant, 0, sizeof(*variant));
   snprintf(module_name, sizeof(module_name), "cs%u_variant%u",
            shader->no, shader->variants_created);

   variant->shader = shader;
   memcpy(&variant->key, key, shader->variant_key_size);

   if (shader->base.ir.nir) {
      lp_cs_get_ir_cache_key(variant, ir_sha1_cache_key);

      lp_disk_cache_find_shader(screen, &cached, ir_sha1_cache_key);
      if (!cached.data_size)
         needs_caching = true;
   }
   variant->gallivm = gallivm_create(module_name, lp->context, &cached);
   if (!variant->gallivm) {
      FREE(variant);
      return NULL;
   }

   variant->list_item_global.base = variant;
   variant->list_item_local.base = variant;
   variant->no = shader->variants_created++;



   if ((LP_DEBUG & DEBUG_CS) || (gallivm_debug & GALLIVM_DEBUG_IR)) {
      lp_debug_cs_variant(variant);
   }

   lp_jit_init_cs_types(variant);

   generate_compute(lp, shader, variant);

   gallivm_compile_module(variant->gallivm);

   lp_build_coro_add_malloc_hooks(variant->gallivm);
   variant->nr_instrs += lp_build_count_ir_module(variant->gallivm->module);

   variant->jit_function = (lp_jit_cs_func)gallivm_jit_function(variant->gallivm, variant->function);

   if (needs_caching) {
      lp_disk_cache_insert_shader(screen, &cached, ir_sha1_cache_key);
   }
   gallivm_free_ir(variant->gallivm);
   return variant;
}

static void
lp_cs_ctx_set_cs_variant( struct lp_cs_context *csctx,
                          struct lp_compute_shader_variant *variant)
{
   csctx->cs.current.variant = variant;
}

static void
llvmpipe_update_cs(struct llvmpipe_context *lp)
{
   struct lp_compute_shader *shader = lp->cs;

   struct lp_compute_shader_variant_key *key;
   struct lp_compute_shader_variant *variant = NULL;
   struct lp_cs_variant_list_item *li;
   char store[LP_CS_MAX_VARIANT_KEY_SIZE];

   key = make_variant_key(lp, shader, store);

   /* Search the variants for one which matches the key */
   li = first_elem(&shader->variants);
   while(!at_end(&shader->variants, li)) {
      if(memcmp(&li->base->key, key, shader->variant_key_size) == 0) {
         variant = li->base;
         break;
      }
      li = next_elem(li);
   }

   if (variant) {
      /* Move this variant to the head of the list to implement LRU
       * deletion of shader's when we have too many.
       */
      move_to_head(&lp->cs_variants_list, &variant->list_item_global);
   }
   else {
      /* variant not found, create it now */
      int64_t t0, t1, dt;
      unsigned i;
      unsigned variants_to_cull;

      if (LP_DEBUG & DEBUG_CS) {
         debug_printf("%u variants,\t%u instrs,\t%u instrs/variant\n",
                      lp->nr_cs_variants,
                      lp->nr_cs_instrs,
                      lp->nr_cs_variants ? lp->nr_cs_instrs / lp->nr_cs_variants : 0);
      }

      /* First, check if we've exceeded the max number of shader variants.
       * If so, free 6.25% of them (the least recently used ones).
       */
      variants_to_cull = lp->nr_cs_variants >= LP_MAX_SHADER_VARIANTS ? LP_MAX_SHADER_VARIANTS / 16 : 0;

      if (variants_to_cull ||
          lp->nr_cs_instrs >= LP_MAX_SHADER_INSTRUCTIONS) {
         if (gallivm_debug & GALLIVM_DEBUG_PERF) {
            debug_printf("Evicting CS: %u cs variants,\t%u total variants,"
                         "\t%u instrs,\t%u instrs/variant\n",
                         shader->variants_cached,
                         lp->nr_cs_variants, lp->nr_cs_instrs,
                         lp->nr_cs_instrs / lp->nr_cs_variants);
         }

         /*
          * We need to re-check lp->nr_cs_variants because an arbitrarily large
          * number of shader variants (potentially all of them) could be
          * pending for destruction on flush.
          */

         for (i = 0; i < variants_to_cull || lp->nr_cs_instrs >= LP_MAX_SHADER_INSTRUCTIONS; i++) {
            struct lp_cs_variant_list_item *item;
            if (is_empty_list(&lp->cs_variants_list)) {
               break;
            }
            item = last_elem(&lp->cs_variants_list);
            assert(item);
            assert(item->base);
            llvmpipe_remove_cs_shader_variant(lp, item->base);
         }
      }
      /*
       * Generate the new variant.
       */
      t0 = os_time_get();
      variant = generate_variant(lp, shader, key);
      t1 = os_time_get();
      dt = t1 - t0;
      LP_COUNT_ADD(llvm_compile_time, dt);
      LP_COUNT_ADD(nr_llvm_compiles, 2);  /* emit vs. omit in/out test */

      /* Put the new variant into the list */
      if (variant) {
         insert_at_head(&shader->variants, &variant->list_item_local);
         insert_at_head(&lp->cs_variants_list, &variant->list_item_global);
         lp->nr_cs_variants++;
         lp->nr_cs_instrs += variant->nr_instrs;
         shader->variants_cached++;
      }
   }
   /* Bind this variant */
   lp_cs_ctx_set_cs_variant(lp->csctx, variant);
}

/**
 * Called during state validation when LP_CSNEW_SAMPLER_VIEW is set.
 */
static void
lp_csctx_set_sampler_views(struct lp_cs_context *csctx,
                           unsigned num,
                           struct pipe_sampler_view **views)
{
   unsigned i, max_tex_num;

   LP_DBG(DEBUG_SETUP, "%s\n", __FUNCTION__);

   assert(num <= PIPE_MAX_SHADER_SAMPLER_VIEWS);

   max_tex_num = MAX2(num, csctx->cs.current_tex_num);

   for (i = 0; i < max_tex_num; i++) {
      struct pipe_sampler_view *view = i < num ? views[i] : NULL;

      if (view) {
         struct pipe_resource *res = view->texture;
         struct llvmpipe_resource *lp_tex = llvmpipe_resource(res);
         struct lp_jit_texture *jit_tex;
         jit_tex = &csctx->cs.current.jit_context.textures[i];

         /* We're referencing the texture's internal data, so save a
          * reference to it.
          */
         pipe_resource_reference(&csctx->cs.current_tex[i], res);

         if (!lp_tex->dt) {
            /* regular texture - csctx array of mipmap level offsets */
            int j;
            unsigned first_level = 0;
            unsigned last_level = 0;

            if (llvmpipe_resource_is_texture(res)) {
               first_level = view->u.tex.first_level;
               last_level = view->u.tex.last_level;
               assert(first_level <= last_level);
               assert(last_level <= res->last_level);
               jit_tex->base = lp_tex->tex_data;
            }
            else {
              jit_tex->base = lp_tex->data;
            }
            if (LP_PERF & PERF_TEX_MEM) {
               /* use dummy tile memory */
               jit_tex->base = lp_dummy_tile;
               jit_tex->width = TILE_SIZE/8;
               jit_tex->height = TILE_SIZE/8;
               jit_tex->depth = 1;
               jit_tex->first_level = 0;
               jit_tex->last_level = 0;
               jit_tex->mip_offsets[0] = 0;
               jit_tex->row_stride[0] = 0;
               jit_tex->img_stride[0] = 0;
               jit_tex->num_samples = 0;
               jit_tex->sample_stride = 0;
            }
            else {
               jit_tex->width = res->width0;
               jit_tex->height = res->height0;
               jit_tex->depth = res->depth0;
               jit_tex->first_level = first_level;
               jit_tex->last_level = last_level;
               jit_tex->num_samples = res->nr_samples;
               jit_tex->sample_stride = 0;

               if (llvmpipe_resource_is_texture(res)) {
                  for (j = first_level; j <= last_level; j++) {
                     jit_tex->mip_offsets[j] = lp_tex->mip_offsets[j];
                     jit_tex->row_stride[j] = lp_tex->row_stride[j];
                     jit_tex->img_stride[j] = lp_tex->img_stride[j];
                  }
                  jit_tex->sample_stride = lp_tex->sample_stride;

                  if (res->target == PIPE_TEXTURE_1D_ARRAY ||
                      res->target == PIPE_TEXTURE_2D_ARRAY ||
                      res->target == PIPE_TEXTURE_CUBE ||
                      res->target == PIPE_TEXTURE_CUBE_ARRAY) {
                     /*
                      * For array textures, we don't have first_layer, instead
                      * adjust last_layer (stored as depth) plus the mip level offsets
                      * (as we have mip-first layout can't just adjust base ptr).
                      * XXX For mip levels, could do something similar.
                      */
                     jit_tex->depth = view->u.tex.last_layer - view->u.tex.first_layer + 1;
                     for (j = first_level; j <= last_level; j++) {
                        jit_tex->mip_offsets[j] += view->u.tex.first_layer *
                                                   lp_tex->img_stride[j];
                     }
                     if (view->target == PIPE_TEXTURE_CUBE ||
                         view->target == PIPE_TEXTURE_CUBE_ARRAY) {
                        assert(jit_tex->depth % 6 == 0);
                     }
                     assert(view->u.tex.first_layer <= view->u.tex.last_layer);
                     assert(view->u.tex.last_layer < res->array_size);
                  }
               }
               else {
                  /*
                   * For buffers, we don't have "offset", instead adjust
                   * the size (stored as width) plus the base pointer.
                   */
                  unsigned view_blocksize = util_format_get_blocksize(view->format);
                  /* probably don't really need to fill that out */
                  jit_tex->mip_offsets[0] = 0;
                  jit_tex->row_stride[0] = 0;
                  jit_tex->img_stride[0] = 0;

                  /* everything specified in number of elements here. */
                  jit_tex->width = view->u.buf.size / view_blocksize;
                  jit_tex->base = (uint8_t *)jit_tex->base + view->u.buf.offset;
                  /* XXX Unsure if we need to sanitize parameters? */
                  assert(view->u.buf.offset + view->u.buf.size <= res->width0);
               }
            }
         }
         else {
            /* display target texture/surface */
            /*
             * XXX: Where should this be unmapped?
             */
            struct llvmpipe_screen *screen = llvmpipe_screen(res->screen);
            struct sw_winsys *winsys = screen->winsys;
            jit_tex->base = winsys->displaytarget_map(winsys, lp_tex->dt,
                                                         PIPE_MAP_READ);
            jit_tex->row_stride[0] = lp_tex->row_stride[0];
            jit_tex->img_stride[0] = lp_tex->img_stride[0];
            jit_tex->mip_offsets[0] = 0;
            jit_tex->width = res->width0;
            jit_tex->height = res->height0;
            jit_tex->depth = res->depth0;
            jit_tex->first_level = jit_tex->last_level = 0;
            jit_tex->num_samples = res->nr_samples;
            jit_tex->sample_stride = 0;
            assert(jit_tex->base);
         }
      }
      else {
         pipe_resource_reference(&csctx->cs.current_tex[i], NULL);
      }
   }
   csctx->cs.current_tex_num = num;
}


/**
 * Called during state validation when LP_NEW_SAMPLER is set.
 */
static void
lp_csctx_set_sampler_state(struct lp_cs_context *csctx,
                           unsigned num,
                           struct pipe_sampler_state **samplers)
{
   unsigned i;

   LP_DBG(DEBUG_SETUP, "%s\n", __FUNCTION__);

   assert(num <= PIPE_MAX_SAMPLERS);

   for (i = 0; i < PIPE_MAX_SAMPLERS; i++) {
      const struct pipe_sampler_state *sampler = i < num ? samplers[i] : NULL;

      if (sampler) {
         struct lp_jit_sampler *jit_sam;
         jit_sam = &csctx->cs.current.jit_context.samplers[i];

         jit_sam->min_lod = sampler->min_lod;
         jit_sam->max_lod = sampler->max_lod;
         jit_sam->lod_bias = sampler->lod_bias;
         COPY_4V(jit_sam->border_color, sampler->border_color.f);
      }
   }
}

static void
lp_csctx_set_cs_constants(struct lp_cs_context *csctx,
                          unsigned num,
                          struct pipe_constant_buffer *buffers)
{
   unsigned i;

   LP_DBG(DEBUG_SETUP, "%s %p\n", __FUNCTION__, (void *) buffers);

   assert(num <= ARRAY_SIZE(csctx->constants));

   for (i = 0; i < num; ++i) {
      util_copy_constant_buffer(&csctx->constants[i].current, &buffers[i], false);
   }
   for (; i < ARRAY_SIZE(csctx->constants); i++) {
      util_copy_constant_buffer(&csctx->constants[i].current, NULL, false);
   }
}

static void
lp_csctx_set_cs_ssbos(struct lp_cs_context *csctx,
                       unsigned num,
                       struct pipe_shader_buffer *buffers)
{
   int i;
   LP_DBG(DEBUG_SETUP, "%s %p\n", __FUNCTION__, (void *)buffers);

   assert (num <= ARRAY_SIZE(csctx->ssbos));

   for (i = 0; i < num; ++i) {
      util_copy_shader_buffer(&csctx->ssbos[i].current, &buffers[i]);
   }
   for (; i < ARRAY_SIZE(csctx->ssbos); i++) {
      util_copy_shader_buffer(&csctx->ssbos[i].current, NULL);
   }
}

static void
lp_csctx_set_cs_images(struct lp_cs_context *csctx,
                       unsigned num,
                       struct pipe_image_view *images)
{
   unsigned i;

   LP_DBG(DEBUG_SETUP, "%s %p\n", __FUNCTION__, (void *) images);

   assert(num <= ARRAY_SIZE(csctx->images));

   for (i = 0; i < num; ++i) {
      struct pipe_image_view *image = &images[i];
      util_copy_image_view(&csctx->images[i].current, &images[i]);

      struct pipe_resource *res = image->resource;
      struct llvmpipe_resource *lp_res = llvmpipe_resource(res);
      struct lp_jit_image *jit_image;

      jit_image = &csctx->cs.current.jit_context.images[i];
      if (!lp_res)
         continue;
      if (!lp_res->dt) {
         /* regular texture - csctx array of mipmap level offsets */
         if (llvmpipe_resource_is_texture(res)) {
            jit_image->base = lp_res->tex_data;
         } else
            jit_image->base = lp_res->data;

         jit_image->width = res->width0;
         jit_image->height = res->height0;
         jit_image->depth = res->depth0;
         jit_image->num_samples = res->nr_samples;

         if (llvmpipe_resource_is_texture(res)) {
            uint32_t mip_offset = lp_res->mip_offsets[image->u.tex.level];

            jit_image->width = u_minify(jit_image->width, image->u.tex.level);
            jit_image->height = u_minify(jit_image->height, image->u.tex.level);

            if (res->target == PIPE_TEXTURE_1D_ARRAY ||
                res->target == PIPE_TEXTURE_2D_ARRAY ||
                res->target == PIPE_TEXTURE_3D ||
                res->target == PIPE_TEXTURE_CUBE ||
                res->target == PIPE_TEXTURE_CUBE_ARRAY) {
               /*
                * For array textures, we don't have first_layer, instead
                * adjust last_layer (stored as depth) plus the mip level offsets
                * (as we have mip-first layout can't just adjust base ptr).
                * XXX For mip levels, could do something similar.
                */
               jit_image->depth = image->u.tex.last_layer - image->u.tex.first_layer + 1;
               mip_offset += image->u.tex.first_layer * lp_res->img_stride[image->u.tex.level];
            } else
               jit_image->depth = u_minify(jit_image->depth, image->u.tex.level);

            jit_image->row_stride = lp_res->row_stride[image->u.tex.level];
            jit_image->img_stride = lp_res->img_stride[image->u.tex.level];
            jit_image->sample_stride = lp_res->sample_stride;
            jit_image->base = (uint8_t *)jit_image->base + mip_offset;
         } else {
            unsigned view_blocksize = util_format_get_blocksize(image->format);
            jit_image->width = image->u.buf.size / view_blocksize;
            jit_image->base = (uint8_t *)jit_image->base + image->u.buf.offset;
         }
      }
   }
   for (; i < ARRAY_SIZE(csctx->images); i++) {
      util_copy_image_view(&csctx->images[i].current, NULL);
   }
}

static void
update_csctx_consts(struct llvmpipe_context *llvmpipe)
{
   struct lp_cs_context *csctx = llvmpipe->csctx;
   int i;

   for (i = 0; i < ARRAY_SIZE(csctx->constants); ++i) {
      struct pipe_resource *buffer = csctx->constants[i].current.buffer;
      const ubyte *current_data = NULL;
      unsigned current_size = csctx->constants[i].current.buffer_size;
      if (buffer) {
         /* resource buffer */
         current_data = (ubyte *) llvmpipe_resource_data(buffer);
      }
      else if (csctx->constants[i].current.user_buffer) {
         /* user-space buffer */
         current_data = (ubyte *) csctx->constants[i].current.user_buffer;
      }

      if (current_data && current_size >= sizeof(float)) {
         current_data += csctx->constants[i].current.buffer_offset;
         csctx->cs.current.jit_context.constants[i] = (const float *)current_data;
         csctx->cs.current.jit_context.num_constants[i] =
            DIV_ROUND_UP(csctx->constants[i].current.buffer_size,
                         lp_get_constant_buffer_stride(llvmpipe->pipe.screen));
      } else {
         static const float fake_const_buf[4];
         csctx->cs.current.jit_context.constants[i] = fake_const_buf;
         csctx->cs.current.jit_context.num_constants[i] = 0;
      }
   }
}

static void
update_csctx_ssbo(struct llvmpipe_context *llvmpipe)
{
   struct lp_cs_context *csctx = llvmpipe->csctx;
   int i;
   for (i = 0; i < ARRAY_SIZE(csctx->ssbos); ++i) {
      struct pipe_resource *buffer = csctx->ssbos[i].current.buffer;
      const ubyte *current_data = NULL;

      if (!buffer)
         continue;
      /* resource buffer */
      current_data = (ubyte *) llvmpipe_resource_data(buffer);
      if (current_data) {
         current_data += csctx->ssbos[i].current.buffer_offset;

         csctx->cs.current.jit_context.ssbos[i] = (const uint32_t *)current_data;
         csctx->cs.current.jit_context.num_ssbos[i] = csctx->ssbos[i].current.buffer_size;
      } else {
         csctx->cs.current.jit_context.ssbos[i] = NULL;
         csctx->cs.current.jit_context.num_ssbos[i] = 0;
      }
   }
}

static void
llvmpipe_cs_update_derived(struct llvmpipe_context *llvmpipe, void *input)
{
   if (llvmpipe->cs_dirty & LP_CSNEW_CONSTANTS) {
      lp_csctx_set_cs_constants(llvmpipe->csctx,
                                ARRAY_SIZE(llvmpipe->constants[PIPE_SHADER_COMPUTE]),
                                llvmpipe->constants[PIPE_SHADER_COMPUTE]);
      update_csctx_consts(llvmpipe);
   }

   if (llvmpipe->cs_dirty & LP_CSNEW_SSBOS) {
      lp_csctx_set_cs_ssbos(llvmpipe->csctx,
                            ARRAY_SIZE(llvmpipe->ssbos[PIPE_SHADER_COMPUTE]),
                            llvmpipe->ssbos[PIPE_SHADER_COMPUTE]);
      update_csctx_ssbo(llvmpipe);
   }

   if (llvmpipe->cs_dirty & LP_CSNEW_SAMPLER_VIEW)
      lp_csctx_set_sampler_views(llvmpipe->csctx,
                                 llvmpipe->num_sampler_views[PIPE_SHADER_COMPUTE],
                                 llvmpipe->sampler_views[PIPE_SHADER_COMPUTE]);

   if (llvmpipe->cs_dirty & LP_CSNEW_SAMPLER)
      lp_csctx_set_sampler_state(llvmpipe->csctx,
                                 llvmpipe->num_samplers[PIPE_SHADER_COMPUTE],
                                 llvmpipe->samplers[PIPE_SHADER_COMPUTE]);

   if (llvmpipe->cs_dirty & LP_CSNEW_IMAGES)
      lp_csctx_set_cs_images(llvmpipe->csctx,
                              ARRAY_SIZE(llvmpipe->images[PIPE_SHADER_COMPUTE]),
                              llvmpipe->images[PIPE_SHADER_COMPUTE]);

   if (input) {
      struct lp_cs_context *csctx = llvmpipe->csctx;
      csctx->input = input;
      csctx->cs.current.jit_context.kernel_args = input;
   }

   if (llvmpipe->cs_dirty & (LP_CSNEW_CS |
                             LP_CSNEW_IMAGES |
                             LP_CSNEW_SAMPLER_VIEW |
                             LP_CSNEW_SAMPLER))
      llvmpipe_update_cs(llvmpipe);


   llvmpipe->cs_dirty = 0;
}

static void
cs_exec_fn(void *init_data, int iter_idx, struct lp_cs_local_mem *lmem)
{
   struct lp_cs_job_info *job_info = init_data;
   struct lp_jit_cs_thread_data thread_data;

   memset(&thread_data, 0, sizeof(thread_data));

   if (lmem->local_size < job_info->req_local_mem) {
      lmem->local_mem_ptr = REALLOC(lmem->local_mem_ptr, lmem->local_size,
                                    job_info->req_local_mem);
      lmem->local_size = job_info->req_local_mem;
   }
   thread_data.shared = lmem->local_mem_ptr;

   unsigned grid_z = iter_idx / (job_info->grid_size[0] * job_info->grid_size[1]);
   unsigned grid_y = (iter_idx - (grid_z * (job_info->grid_size[0] * job_info->grid_size[1]))) / job_info->grid_size[0];
   unsigned grid_x = (iter_idx - (grid_z * (job_info->grid_size[0] * job_info->grid_size[1])) - (grid_y * job_info->grid_size[0]));

   grid_z += job_info->grid_base[2];
   grid_y += job_info->grid_base[1];
   grid_x += job_info->grid_base[0];
   struct lp_compute_shader_variant *variant = job_info->current->variant;
   variant->jit_function(&job_info->current->jit_context,
                         job_info->block_size[0], job_info->block_size[1], job_info->block_size[2],
                         grid_x, grid_y, grid_z,
                         job_info->grid_size[0], job_info->grid_size[1], job_info->grid_size[2], job_info->work_dim,
                         &thread_data);
}

static void
fill_grid_size(struct pipe_context *pipe,
               const struct pipe_grid_info *info,
               uint32_t grid_size[3])
{
   struct pipe_transfer *transfer;
   uint32_t *params;
   if (!info->indirect) {
      grid_size[0] = info->grid[0];
      grid_size[1] = info->grid[1];
      grid_size[2] = info->grid[2];
      return;
   }
   params = pipe_buffer_map_range(pipe, info->indirect,
                                  info->indirect_offset,
                                  3 * sizeof(uint32_t),
                                  PIPE_MAP_READ,
                                  &transfer);

   if (!transfer)
      return;

   grid_size[0] = params[0];
   grid_size[1] = params[1];
   grid_size[2] = params[2];
   pipe_buffer_unmap(pipe, transfer);
}

static void llvmpipe_launch_grid(struct pipe_context *pipe,
                                 const struct pipe_grid_info *info)
{
   struct llvmpipe_context *llvmpipe = llvmpipe_context(pipe);
   struct llvmpipe_screen *screen = llvmpipe_screen(pipe->screen);
   struct lp_cs_job_info job_info;

   if (!llvmpipe_check_render_cond(llvmpipe))
      return;

   memset(&job_info, 0, sizeof(job_info));

   llvmpipe_cs_update_derived(llvmpipe, info->input);

   fill_grid_size(pipe, info, job_info.grid_size);

   job_info.grid_base[0] = info->grid_base[0];
   job_info.grid_base[1] = info->grid_base[1];
   job_info.grid_base[2] = info->grid_base[2];
   job_info.block_size[0] = info->block[0];
   job_info.block_size[1] = info->block[1];
   job_info.block_size[2] = info->block[2];
   job_info.work_dim = info->work_dim;
   job_info.req_local_mem = llvmpipe->cs->req_local_mem;
   job_info.current = &llvmpipe->csctx->cs.current;

   int num_tasks = job_info.grid_size[2] * job_info.grid_size[1] * job_info.grid_size[0];
   if (num_tasks) {
      struct lp_cs_tpool_task *task;
      mtx_lock(&screen->cs_mutex);
      task = lp_cs_tpool_queue_task(screen->cs_tpool, cs_exec_fn, &job_info, num_tasks);

      lp_cs_tpool_wait_for_task(screen->cs_tpool, &task);
      mtx_unlock(&screen->cs_mutex);
   }
   llvmpipe->pipeline_statistics.cs_invocations += num_tasks * info->block[0] * info->block[1] * info->block[2];
}

static void
llvmpipe_set_compute_resources(struct pipe_context *pipe,
                               unsigned start, unsigned count,
                               struct pipe_surface **resources)
{


}

static void
llvmpipe_set_global_binding(struct pipe_context *pipe,
                            unsigned first, unsigned count,
                            struct pipe_resource **resources,
                            uint32_t **handles)
{
   struct llvmpipe_context *llvmpipe = llvmpipe_context(pipe);
   struct lp_compute_shader *cs = llvmpipe->cs;
   unsigned i;

   if (first + count > cs->max_global_buffers) {
      unsigned old_max = cs->max_global_buffers;
      cs->max_global_buffers = first + count;
      cs->global_buffers = realloc(cs->global_buffers,
                                   cs->max_global_buffers * sizeof(cs->global_buffers[0]));
      if (!cs->global_buffers) {
         return;
      }

      memset(&cs->global_buffers[old_max], 0, (cs->max_global_buffers - old_max) * sizeof(cs->global_buffers[0]));
   }

   if (!resources) {
      for (i = 0; i < count; i++)
         pipe_resource_reference(&cs->global_buffers[first + i], NULL);
      return;
   }

   for (i = 0; i < count; i++) {
      uintptr_t va;
      uint32_t offset;
      pipe_resource_reference(&cs->global_buffers[first + i], resources[i]);
      struct llvmpipe_resource *lp_res = llvmpipe_resource(resources[i]);
      offset = *handles[i];
      va = (uintptr_t)((char *)lp_res->data + offset);
      memcpy(handles[i], &va, sizeof(va));
   }
}

void
llvmpipe_init_compute_funcs(struct llvmpipe_context *llvmpipe)
{
   llvmpipe->pipe.create_compute_state = llvmpipe_create_compute_state;
   llvmpipe->pipe.bind_compute_state = llvmpipe_bind_compute_state;
   llvmpipe->pipe.delete_compute_state = llvmpipe_delete_compute_state;
   llvmpipe->pipe.set_compute_resources = llvmpipe_set_compute_resources;
   llvmpipe->pipe.set_global_binding = llvmpipe_set_global_binding;
   llvmpipe->pipe.launch_grid = llvmpipe_launch_grid;
}

void
lp_csctx_destroy(struct lp_cs_context *csctx)
{
   unsigned i;
   for (i = 0; i < ARRAY_SIZE(csctx->cs.current_tex); i++) {
      pipe_resource_reference(&csctx->cs.current_tex[i], NULL);
   }
   for (i = 0; i < ARRAY_SIZE(csctx->constants); i++) {
      pipe_resource_reference(&csctx->constants[i].current.buffer, NULL);
   }
   for (i = 0; i < ARRAY_SIZE(csctx->ssbos); i++) {
      pipe_resource_reference(&csctx->ssbos[i].current.buffer, NULL);
   }
   FREE(csctx);
}

struct lp_cs_context *lp_csctx_create(struct pipe_context *pipe)
{
   struct lp_cs_context *csctx;

   csctx = CALLOC_STRUCT(lp_cs_context);
   if (!csctx)
      return NULL;

   csctx->pipe = pipe;
   return csctx;
}
