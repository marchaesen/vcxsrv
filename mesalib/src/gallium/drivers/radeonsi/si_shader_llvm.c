/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#include "ac_nir.h"
#include "ac_nir_to_llvm.h"
#include "ac_rtld.h"
#include "si_pipe.h"
#include "si_shader_internal.h"
#include "sid.h"
#include "tgsi/tgsi_from_mesa.h"
#include "util/u_memory.h"

struct si_llvm_diagnostics {
   struct util_debug_callback *debug;
   unsigned retval;
};

static void si_diagnostic_handler(LLVMDiagnosticInfoRef di, void *context)
{
   struct si_llvm_diagnostics *diag = (struct si_llvm_diagnostics *)context;
   LLVMDiagnosticSeverity severity = LLVMGetDiagInfoSeverity(di);
   const char *severity_str = NULL;

   switch (severity) {
   case LLVMDSError:
      severity_str = "error";
      break;
   case LLVMDSWarning:
      severity_str = "warning";
      break;
   case LLVMDSRemark:
   case LLVMDSNote:
   default:
      return;
   }

   char *description = LLVMGetDiagInfoDescription(di);

   util_debug_message(diag->debug, SHADER_INFO, "LLVM diagnostic (%s): %s", severity_str,
                      description);

   if (severity == LLVMDSError) {
      diag->retval = 1;
      fprintf(stderr, "LLVM triggered Diagnostic Handler: %s\n", description);
   }

   LLVMDisposeMessage(description);
}

bool si_compile_llvm(struct si_screen *sscreen, struct si_shader_binary *binary,
                     struct ac_shader_config *conf, struct ac_llvm_compiler *compiler,
                     struct ac_llvm_context *ac, struct util_debug_callback *debug,
                     gl_shader_stage stage, const char *name, bool less_optimized)
{
   unsigned count = p_atomic_inc_return(&sscreen->num_compilations);

   if (si_can_dump_shader(sscreen, stage)) {
      fprintf(stderr, "radeonsi: Compiling shader %d\n", count);

      if (!(sscreen->debug_flags & (DBG(NO_IR) | DBG(PREOPT_IR)))) {
         fprintf(stderr, "%s LLVM IR:\n\n", name);
         ac_dump_module(ac->module);
         fprintf(stderr, "\n");
      }
   }

   if (sscreen->record_llvm_ir) {
      char *ir = LLVMPrintModuleToString(ac->module);
      binary->llvm_ir_string = strdup(ir);
      LLVMDisposeMessage(ir);
   }

   if (!si_replace_shader(count, binary)) {
      struct ac_compiler_passes *passes = compiler->passes;

      if (less_optimized && compiler->low_opt_passes)
         passes = compiler->low_opt_passes;

      struct si_llvm_diagnostics diag = {debug};
      LLVMContextSetDiagnosticHandler(ac->context, si_diagnostic_handler, &diag);

      if (!ac_compile_module_to_elf(passes, ac->module, (char **)&binary->elf_buffer,
                                    &binary->elf_size))
         diag.retval = 1;

      if (diag.retval != 0) {
         util_debug_message(debug, SHADER_INFO, "LLVM compilation failed");
         return false;
      }
   }

   struct ac_rtld_binary rtld;
   if (!ac_rtld_open(&rtld, (struct ac_rtld_open_info){
                               .info = &sscreen->info,
                               .shader_type = stage,
                               .wave_size = ac->wave_size,
                               .num_parts = 1,
                               .elf_ptrs = &binary->elf_buffer,
                               .elf_sizes = &binary->elf_size}))
      return false;

   bool ok = ac_rtld_read_config(&sscreen->info, &rtld, conf);
   ac_rtld_close(&rtld);
   return ok;
}

void si_llvm_context_init(struct si_shader_context *ctx, struct si_screen *sscreen,
                          struct ac_llvm_compiler *compiler, unsigned wave_size)
{
   memset(ctx, 0, sizeof(*ctx));
   ctx->screen = sscreen;
   ctx->compiler = compiler;

   ac_llvm_context_init(&ctx->ac, compiler, sscreen->info.gfx_level, sscreen->info.family,
                        sscreen->info.has_3d_cube_border_color_mipmap, AC_FLOAT_MODE_DEFAULT_OPENGL, wave_size, 64);
}

void si_llvm_create_func(struct si_shader_context *ctx, const char *name, LLVMTypeRef *return_types,
                         unsigned num_return_elems, unsigned max_workgroup_size)
{
   LLVMTypeRef ret_type;
   enum ac_llvm_calling_convention call_conv;

   if (num_return_elems)
      ret_type = LLVMStructTypeInContext(ctx->ac.context, return_types, num_return_elems, true);
   else
      ret_type = ctx->ac.voidt;

   gl_shader_stage real_stage = ctx->stage;

   /* LS is merged into HS (TCS), and ES is merged into GS. */
   if (ctx->screen->info.gfx_level >= GFX9 && ctx->stage <= MESA_SHADER_GEOMETRY) {
      if (ctx->shader->key.ge.as_ls)
         real_stage = MESA_SHADER_TESS_CTRL;
      else if (ctx->shader->key.ge.as_es || ctx->shader->key.ge.as_ngg)
         real_stage = MESA_SHADER_GEOMETRY;
   }

   switch (real_stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_EVAL:
      call_conv = AC_LLVM_AMDGPU_VS;
      break;
   case MESA_SHADER_TESS_CTRL:
      call_conv = AC_LLVM_AMDGPU_HS;
      break;
   case MESA_SHADER_GEOMETRY:
      call_conv = AC_LLVM_AMDGPU_GS;
      break;
   case MESA_SHADER_FRAGMENT:
      call_conv = AC_LLVM_AMDGPU_PS;
      break;
   case MESA_SHADER_COMPUTE:
      call_conv = AC_LLVM_AMDGPU_CS;
      break;
   default:
      unreachable("Unhandle shader type");
   }

   /* Setup the function */
   ctx->return_type = ret_type;
   ctx->main_fn = ac_build_main(&ctx->args, &ctx->ac, call_conv, name, ret_type, ctx->ac.module);
   ctx->return_value = LLVMGetUndef(ctx->return_type);

   if (ctx->screen->info.address32_hi) {
      ac_llvm_add_target_dep_function_attr(ctx->main_fn, "amdgpu-32bit-address-high-bits",
                                           ctx->screen->info.address32_hi);
   }

   if (ctx->stage <= MESA_SHADER_GEOMETRY && ctx->shader->key.ge.as_ngg &&
       si_shader_uses_streamout(ctx->shader))
      ac_llvm_add_target_dep_function_attr(ctx->main_fn, "amdgpu-gds-size", 256);

   ac_llvm_set_workgroup_size(ctx->main_fn, max_workgroup_size);
   ac_llvm_set_target_features(ctx->main_fn, &ctx->ac);
}

void si_llvm_create_main_func(struct si_shader_context *ctx, bool ngg_cull_shader)
{
   struct si_shader *shader = ctx->shader;
   LLVMTypeRef returns[AC_MAX_ARGS];
   unsigned i;

   si_init_shader_args(ctx, ngg_cull_shader);

   for (i = 0; i < ctx->args.num_sgprs_returned; i++)
      returns[i] = ctx->ac.i32; /* SGPR */
   for (; i < ctx->args.return_count; i++)
      returns[i] = ctx->ac.f32; /* VGPR */

   si_llvm_create_func(ctx, ngg_cull_shader ? "ngg_cull_main" : "main", returns,
                       ctx->args.return_count, si_get_max_workgroup_size(shader));

   /* Reserve register locations for VGPR inputs the PS prolog may need. */
   if (ctx->stage == MESA_SHADER_FRAGMENT && !ctx->shader->is_monolithic) {
      ac_llvm_add_target_dep_function_attr(
         ctx->main_fn, "InitialPSInputAddr",
         S_0286D0_PERSP_SAMPLE_ENA(1) | S_0286D0_PERSP_CENTER_ENA(1) |
            S_0286D0_PERSP_CENTROID_ENA(1) | S_0286D0_LINEAR_SAMPLE_ENA(1) |
            S_0286D0_LINEAR_CENTER_ENA(1) | S_0286D0_LINEAR_CENTROID_ENA(1) |
            S_0286D0_FRONT_FACE_ENA(1) | S_0286D0_ANCILLARY_ENA(1) |
            S_0286D0_SAMPLE_COVERAGE_ENA(1) | S_0286D0_POS_FIXED_PT_ENA(1));
   }


   if (ctx->stage <= MESA_SHADER_GEOMETRY &&
       (shader->key.ge.as_ls || ctx->stage == MESA_SHADER_TESS_CTRL)) {
      if (USE_LDS_SYMBOLS) {
         /* The LSHS size is not known until draw time, so we append it
          * at the end of whatever LDS use there may be in the rest of
          * the shader (currently none, unless LLVM decides to do its
          * own LDS-based lowering).
          */
         ctx->ac.lds = LLVMAddGlobalInAddressSpace(ctx->ac.module, LLVMArrayType(ctx->ac.i32, 0),
                                                   "__lds_end", AC_ADDR_SPACE_LDS);
         LLVMSetAlignment(ctx->ac.lds, 256);
      } else {
         ac_declare_lds_as_pointer(&ctx->ac);
      }
   }

   /* Unlike radv, we override these arguments in the prolog, so to the
    * API shader they appear as normal arguments.
    */
   if (ctx->stage == MESA_SHADER_VERTEX) {
      ctx->abi.vertex_id = ac_get_arg(&ctx->ac, ctx->args.vertex_id);
      ctx->abi.instance_id = ac_get_arg(&ctx->ac, ctx->args.instance_id);
   } else if (ctx->stage == MESA_SHADER_FRAGMENT) {
      ctx->abi.persp_centroid = ac_get_arg(&ctx->ac, ctx->args.persp_centroid);
      ctx->abi.linear_centroid = ac_get_arg(&ctx->ac, ctx->args.linear_centroid);
   }
}

void si_llvm_optimize_module(struct si_shader_context *ctx)
{
   /* Dump LLVM IR before any optimization passes */
   if (ctx->screen->debug_flags & DBG(PREOPT_IR) && si_can_dump_shader(ctx->screen, ctx->stage))
      LLVMDumpModule(ctx->ac.module);

   /* Run the pass */
   LLVMRunPassManager(ctx->compiler->passmgr, ctx->ac.module);
   LLVMDisposeBuilder(ctx->ac.builder);
}

void si_llvm_dispose(struct si_shader_context *ctx)
{
   LLVMDisposeModule(ctx->ac.module);
   LLVMContextDispose(ctx->ac.context);
   ac_llvm_context_dispose(&ctx->ac);
}

/**
 * Load a dword from a constant buffer.
 */
LLVMValueRef si_buffer_load_const(struct si_shader_context *ctx, LLVMValueRef resource,
                                  LLVMValueRef offset)
{
   return ac_build_buffer_load(&ctx->ac, resource, 1, NULL, offset, NULL, ctx->ac.f32,
                               0, true, true);
}

void si_llvm_build_ret(struct si_shader_context *ctx, LLVMValueRef ret)
{
   if (LLVMGetTypeKind(LLVMTypeOf(ret)) == LLVMVoidTypeKind)
      LLVMBuildRetVoid(ctx->ac.builder);
   else
      LLVMBuildRet(ctx->ac.builder, ret);
}

LLVMValueRef si_insert_input_ret(struct si_shader_context *ctx, LLVMValueRef ret,
                                 struct ac_arg param, unsigned return_index)
{
   return LLVMBuildInsertValue(ctx->ac.builder, ret, ac_get_arg(&ctx->ac, param), return_index, "");
}

LLVMValueRef si_insert_input_ret_float(struct si_shader_context *ctx, LLVMValueRef ret,
                                       struct ac_arg param, unsigned return_index)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef p = ac_get_arg(&ctx->ac, param);

   return LLVMBuildInsertValue(builder, ret, ac_to_float(&ctx->ac, p), return_index, "");
}

LLVMValueRef si_insert_input_ptr(struct si_shader_context *ctx, LLVMValueRef ret,
                                 struct ac_arg param, unsigned return_index)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef ptr = ac_get_arg(&ctx->ac, param);
   ptr = LLVMBuildPtrToInt(builder, ptr, ctx->ac.i32, "");
   return LLVMBuildInsertValue(builder, ret, ptr, return_index, "");
}

LLVMValueRef si_prolog_get_internal_bindings(struct si_shader_context *ctx)
{
   LLVMValueRef ptr[2], list;
   bool merged_shader = si_is_merged_shader(ctx->shader);

   ptr[0] = LLVMGetParam(ctx->main_fn, (merged_shader ? 8 : 0) + SI_SGPR_INTERNAL_BINDINGS);
   list =
      LLVMBuildIntToPtr(ctx->ac.builder, ptr[0], ac_array_in_const32_addr_space(ctx->ac.v4i32), "");
   return list;
}

/* Ensure that the esgs ring is declared.
 *
 * We declare it with 64KB alignment as a hint that the
 * pointer value will always be 0.
 */
void si_llvm_declare_esgs_ring(struct si_shader_context *ctx)
{
   if (ctx->esgs_ring)
      return;

   assert(!LLVMGetNamedGlobal(ctx->ac.module, "esgs_ring"));

   ctx->esgs_ring = LLVMAddGlobalInAddressSpace(ctx->ac.module, LLVMArrayType(ctx->ac.i32, 0),
                                                "esgs_ring", AC_ADDR_SPACE_LDS);
   LLVMSetLinkage(ctx->esgs_ring, LLVMExternalLinkage);
   LLVMSetAlignment(ctx->esgs_ring, 64 * 1024);
}

static void si_init_exec_from_input(struct si_shader_context *ctx, struct ac_arg param,
                                    unsigned bitoffset)
{
   LLVMValueRef args[] = {
      ac_get_arg(&ctx->ac, param),
      LLVMConstInt(ctx->ac.i32, bitoffset, 0),
   };
   ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.init.exec.from.input", ctx->ac.voidt, args, 2,
                      AC_FUNC_ATTR_CONVERGENT);
}

/**
 * Get the value of a shader input parameter and extract a bitfield.
 */
static LLVMValueRef unpack_llvm_param(struct si_shader_context *ctx, LLVMValueRef value,
                                      unsigned rshift, unsigned bitwidth)
{
   if (LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMFloatTypeKind)
      value = ac_to_integer(&ctx->ac, value);

   if (rshift)
      value = LLVMBuildLShr(ctx->ac.builder, value, LLVMConstInt(ctx->ac.i32, rshift, 0), "");

   if (rshift + bitwidth < 32) {
      unsigned mask = (1 << bitwidth) - 1;
      value = LLVMBuildAnd(ctx->ac.builder, value, LLVMConstInt(ctx->ac.i32, mask, 0), "");
   }

   return value;
}

LLVMValueRef si_unpack_param(struct si_shader_context *ctx, struct ac_arg param, unsigned rshift,
                             unsigned bitwidth)
{
   LLVMValueRef value = ac_get_arg(&ctx->ac, param);

   return unpack_llvm_param(ctx, value, rshift, bitwidth);
}

LLVMValueRef si_get_primitive_id(struct si_shader_context *ctx, unsigned swizzle)
{
   if (swizzle > 0)
      return ctx->ac.i32_0;

   switch (ctx->stage) {
   case MESA_SHADER_VERTEX:
      return ac_get_arg(&ctx->ac, ctx->args.vs_prim_id);
   case MESA_SHADER_TESS_CTRL:
      return ac_get_arg(&ctx->ac, ctx->args.tcs_patch_id);
   case MESA_SHADER_TESS_EVAL:
      return ctx->abi.tes_patch_id_replaced ?
         ctx->abi.tes_patch_id_replaced :
         ac_get_arg(&ctx->ac, ctx->args.tes_patch_id);
   case MESA_SHADER_GEOMETRY:
      return ac_get_arg(&ctx->ac, ctx->args.gs_prim_id);
   default:
      assert(0);
      return ctx->ac.i32_0;
   }
}

static void si_llvm_declare_compute_memory(struct si_shader_context *ctx)
{
   struct si_shader_selector *sel = ctx->shader->selector;
   unsigned lds_size = sel->info.base.shared_size;

   LLVMTypeRef i8p = LLVMPointerType(ctx->ac.i8, AC_ADDR_SPACE_LDS);
   LLVMValueRef var;

   assert(!ctx->ac.lds);

   var = LLVMAddGlobalInAddressSpace(ctx->ac.module, LLVMArrayType(ctx->ac.i8, lds_size),
                                     "compute_lds", AC_ADDR_SPACE_LDS);
   LLVMSetAlignment(var, 64 * 1024);

   ctx->ac.lds = LLVMBuildBitCast(ctx->ac.builder, var, i8p, "");
}

/**
 * Given a list of shader part functions, build a wrapper function that
 * runs them in sequence to form a monolithic shader.
 */
void si_build_wrapper_function(struct si_shader_context *ctx, LLVMValueRef *parts,
                               unsigned num_parts, unsigned main_part,
                               unsigned next_shader_first_part, bool same_thread_count)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   /* PS epilog has one arg per color component; gfx9 merged shader
    * prologs need to forward 40 SGPRs.
    */
   LLVMValueRef initial[AC_MAX_ARGS], out[AC_MAX_ARGS];
   LLVMTypeRef function_type;
   unsigned num_first_params;
   unsigned num_out, initial_num_out;
   ASSERTED unsigned num_out_sgpr;         /* used in debug checks */
   ASSERTED unsigned initial_num_out_sgpr; /* used in debug checks */
   unsigned num_sgprs, num_vgprs;
   unsigned gprs;

   memset(&ctx->args, 0, sizeof(ctx->args));

   for (unsigned i = 0; i < num_parts; ++i) {
      ac_add_function_attr(ctx->ac.context, parts[i], -1, AC_FUNC_ATTR_ALWAYSINLINE);
      LLVMSetLinkage(parts[i], LLVMPrivateLinkage);
   }

   /* The parameters of the wrapper function correspond to those of the
    * first part in terms of SGPRs and VGPRs, but we use the types of the
    * main part to get the right types. This is relevant for the
    * dereferenceable attribute on descriptor table pointers.
    */
   num_sgprs = 0;
   num_vgprs = 0;

   function_type = LLVMGetElementType(LLVMTypeOf(parts[0]));
   num_first_params = LLVMCountParamTypes(function_type);

   for (unsigned i = 0; i < num_first_params; ++i) {
      LLVMValueRef param = LLVMGetParam(parts[0], i);

      if (ac_is_sgpr_param(param)) {
         assert(num_vgprs == 0);
         num_sgprs += ac_get_type_size(LLVMTypeOf(param)) / 4;
      } else {
         num_vgprs += ac_get_type_size(LLVMTypeOf(param)) / 4;
      }
   }

   gprs = 0;
   while (gprs < num_sgprs + num_vgprs) {
      LLVMValueRef param = LLVMGetParam(parts[main_part], ctx->args.arg_count);
      LLVMTypeRef type = LLVMTypeOf(param);
      unsigned size = ac_get_type_size(type) / 4;

      /* This is going to get casted anyways, so we don't have to
       * have the exact same type. But we do have to preserve the
       * pointer-ness so that LLVM knows about it.
       */
      enum ac_arg_type arg_type = AC_ARG_INT;
      if (LLVMGetTypeKind(type) == LLVMPointerTypeKind) {
         type = LLVMGetElementType(type);

         if (LLVMGetTypeKind(type) == LLVMVectorTypeKind) {
            if (LLVMGetVectorSize(type) == 4)
               arg_type = AC_ARG_CONST_DESC_PTR;
            else if (LLVMGetVectorSize(type) == 8)
               arg_type = AC_ARG_CONST_IMAGE_PTR;
            else
               assert(0);
         } else if (type == ctx->ac.f32) {
            arg_type = AC_ARG_CONST_FLOAT_PTR;
         } else {
            assert(0);
         }
      }

      ac_add_arg(&ctx->args, gprs < num_sgprs ? AC_ARG_SGPR : AC_ARG_VGPR, size, arg_type, NULL);

      assert(ac_is_sgpr_param(param) == (gprs < num_sgprs));
      assert(gprs + size <= num_sgprs + num_vgprs &&
             (gprs >= num_sgprs || gprs + size <= num_sgprs));

      gprs += size;
   }

   /* Prepare the return type. */
   unsigned num_returns = 0;
   LLVMTypeRef returns[AC_MAX_ARGS], last_func_type, return_type;

   last_func_type = LLVMGetElementType(LLVMTypeOf(parts[num_parts - 1]));
   return_type = LLVMGetReturnType(last_func_type);

   switch (LLVMGetTypeKind(return_type)) {
   case LLVMStructTypeKind:
      num_returns = LLVMCountStructElementTypes(return_type);
      assert(num_returns <= ARRAY_SIZE(returns));
      LLVMGetStructElementTypes(return_type, returns);
      break;
   case LLVMVoidTypeKind:
      break;
   default:
      unreachable("unexpected type");
   }

   si_llvm_create_func(ctx, "wrapper", returns, num_returns,
                       si_get_max_workgroup_size(ctx->shader));

   if (si_is_merged_shader(ctx->shader) && !same_thread_count)
      ac_init_exec_full_mask(&ctx->ac);

   /* Record the arguments of the function as if they were an output of
    * a previous part.
    */
   num_out = 0;
   num_out_sgpr = 0;

   for (unsigned i = 0; i < ctx->args.arg_count; ++i) {
      LLVMValueRef param = LLVMGetParam(ctx->main_fn, i);
      LLVMTypeRef param_type = LLVMTypeOf(param);
      LLVMTypeRef out_type = ctx->args.args[i].file == AC_ARG_SGPR ? ctx->ac.i32 : ctx->ac.f32;
      unsigned size = ac_get_type_size(param_type) / 4;

      if (size == 1) {
         if (LLVMGetTypeKind(param_type) == LLVMPointerTypeKind) {
            param = LLVMBuildPtrToInt(builder, param, ctx->ac.i32, "");
            param_type = ctx->ac.i32;
         }

         if (param_type != out_type)
            param = LLVMBuildBitCast(builder, param, out_type, "");
         out[num_out++] = param;
      } else {
         LLVMTypeRef vector_type = LLVMVectorType(out_type, size);

         if (LLVMGetTypeKind(param_type) == LLVMPointerTypeKind) {
            param = LLVMBuildPtrToInt(builder, param, ctx->ac.i64, "");
            param_type = ctx->ac.i64;
         }

         if (param_type != vector_type)
            param = LLVMBuildBitCast(builder, param, vector_type, "");

         for (unsigned j = 0; j < size; ++j)
            out[num_out++] =
               LLVMBuildExtractElement(builder, param, LLVMConstInt(ctx->ac.i32, j, 0), "");
      }

      if (ctx->args.args[i].file == AC_ARG_SGPR)
         num_out_sgpr = num_out;
   }

   memcpy(initial, out, sizeof(out));
   initial_num_out = num_out;
   initial_num_out_sgpr = num_out_sgpr;

   /* Now chain the parts. */
   LLVMValueRef ret = NULL;
   for (unsigned part = 0; part < num_parts; ++part) {
      LLVMValueRef in[AC_MAX_ARGS];
      LLVMTypeRef ret_type;
      unsigned out_idx = 0;
      unsigned num_params = LLVMCountParams(parts[part]);

      /* Merged shaders are executed conditionally depending
       * on the number of enabled threads passed in the input SGPRs. */
      if (si_is_multi_part_shader(ctx->shader) && part == 0) {
         if (same_thread_count) {
            struct ac_arg arg;
            arg.arg_index = 3;
            arg.used = true;

            si_init_exec_from_input(ctx, arg, 0);
         } else {
            LLVMValueRef ena, count = initial[3];

            count = LLVMBuildAnd(builder, count, LLVMConstInt(ctx->ac.i32, 0x7f, 0), "");
            ena = LLVMBuildICmp(builder, LLVMIntULT, ac_get_thread_id(&ctx->ac), count, "");
            ac_build_ifcc(&ctx->ac, ena, 6506);
         }
      }

      /* Derive arguments for the next part from outputs of the
       * previous one.
       */
      for (unsigned param_idx = 0; param_idx < num_params; ++param_idx) {
         LLVMValueRef param;
         LLVMTypeRef param_type;
         bool is_sgpr;
         unsigned param_size;
         LLVMValueRef arg = NULL;

         param = LLVMGetParam(parts[part], param_idx);
         param_type = LLVMTypeOf(param);
         param_size = ac_get_type_size(param_type) / 4;
         is_sgpr = ac_is_sgpr_param(param);

         if (is_sgpr) {
            ac_add_function_attr(ctx->ac.context, parts[part], param_idx + 1, AC_FUNC_ATTR_INREG);
         } else if (out_idx < num_out_sgpr) {
            /* Skip returned SGPRs the current part doesn't
             * declare on the input. */
            out_idx = num_out_sgpr;
         }

         assert(out_idx + param_size <= (is_sgpr ? num_out_sgpr : num_out));

         if (param_size == 1)
            arg = out[out_idx];
         else
            arg = ac_build_gather_values(&ctx->ac, &out[out_idx], param_size);

         if (LLVMTypeOf(arg) != param_type) {
            if (LLVMGetTypeKind(param_type) == LLVMPointerTypeKind) {
               if (LLVMGetPointerAddressSpace(param_type) == AC_ADDR_SPACE_CONST_32BIT) {
                  arg = LLVMBuildBitCast(builder, arg, ctx->ac.i32, "");
                  arg = LLVMBuildIntToPtr(builder, arg, param_type, "");
               } else {
                  arg = LLVMBuildBitCast(builder, arg, ctx->ac.i64, "");
                  arg = LLVMBuildIntToPtr(builder, arg, param_type, "");
               }
            } else {
               arg = LLVMBuildBitCast(builder, arg, param_type, "");
            }
         }

         in[param_idx] = arg;
         out_idx += param_size;
      }

      ret = ac_build_call(&ctx->ac, parts[part], in, num_params);

      if (!same_thread_count &&
          si_is_multi_part_shader(ctx->shader) && part + 1 == next_shader_first_part) {
         ac_build_endif(&ctx->ac, 6506);

         /* The second half of the merged shader should use
          * the inputs from the toplevel (wrapper) function,
          * not the return value from the last call.
          *
          * That's because the last call was executed condi-
          * tionally, so we can't consume it in the main
          * block.
          */
         memcpy(out, initial, sizeof(initial));
         num_out = initial_num_out;
         num_out_sgpr = initial_num_out_sgpr;

         /* Execute the second shader conditionally based on the number of
          * enabled threads there.
          */
         if (ctx->stage == MESA_SHADER_TESS_CTRL) {
            LLVMValueRef ena, count = initial[3];

            count = LLVMBuildLShr(builder, count, LLVMConstInt(ctx->ac.i32, 8, 0), "");
            count = LLVMBuildAnd(builder, count, LLVMConstInt(ctx->ac.i32, 0x7f, 0), "");
            ena = LLVMBuildICmp(builder, LLVMIntULT, ac_get_thread_id(&ctx->ac), count, "");
            ac_build_ifcc(&ctx->ac, ena, 6507);
         }
         continue;
      }

      /* Extract the returned GPRs. */
      ret_type = LLVMTypeOf(ret);
      num_out = 0;
      num_out_sgpr = 0;

      if (LLVMGetTypeKind(ret_type) != LLVMVoidTypeKind) {
         assert(LLVMGetTypeKind(ret_type) == LLVMStructTypeKind);

         unsigned ret_size = LLVMCountStructElementTypes(ret_type);

         for (unsigned i = 0; i < ret_size; ++i) {
            LLVMValueRef val = LLVMBuildExtractValue(builder, ret, i, "");

            assert(num_out < ARRAY_SIZE(out));
            out[num_out++] = val;

            if (LLVMTypeOf(val) == ctx->ac.i32) {
               assert(num_out_sgpr + 1 == num_out);
               num_out_sgpr = num_out;
            }
         }
      }
   }

   /* Close the conditional wrapping the second shader. */
   if (ctx->stage == MESA_SHADER_TESS_CTRL &&
       !same_thread_count && si_is_multi_part_shader(ctx->shader))
      ac_build_endif(&ctx->ac, 6507);

   if (LLVMGetTypeKind(LLVMTypeOf(ret)) == LLVMVoidTypeKind)
      LLVMBuildRetVoid(builder);
   else
      LLVMBuildRet(builder, ret);
}

static LLVMValueRef si_llvm_load_intrinsic(struct ac_shader_abi *abi, nir_intrinsic_op op)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);

   switch (op) {
   case nir_intrinsic_load_first_vertex:
      return ac_get_arg(&ctx->ac, ctx->args.base_vertex);

   case nir_intrinsic_load_base_vertex: {
      /* For non-indexed draws, the base vertex set by the driver
       * (for direct draws) or the CP (for indirect draws) is the
       * first vertex ID, but GLSL expects 0 to be returned.
       */
      LLVMValueRef indexed = GET_FIELD(ctx, VS_STATE_INDEXED);
      indexed = LLVMBuildTrunc(ctx->ac.builder, indexed, ctx->ac.i1, "");
      return LLVMBuildSelect(ctx->ac.builder, indexed, ac_get_arg(&ctx->ac, ctx->args.base_vertex),
                             ctx->ac.i32_0, "");
   }

   case nir_intrinsic_load_workgroup_size: {
      assert(ctx->shader->selector->info.base.workgroup_size_variable &&
             ctx->shader->selector->info.uses_variable_block_size);
      LLVMValueRef chan[3] = {
         si_unpack_param(ctx, ctx->block_size, 0, 10),
         si_unpack_param(ctx, ctx->block_size, 10, 10),
         si_unpack_param(ctx, ctx->block_size, 20, 10),
      };
      return ac_build_gather_values(&ctx->ac, chan, 3);
   }

   case nir_intrinsic_load_tess_level_outer_default:
   case nir_intrinsic_load_tess_level_inner_default: {
      LLVMValueRef slot = LLVMConstInt(ctx->ac.i32, SI_HS_CONST_DEFAULT_TESS_LEVELS, 0);
      LLVMValueRef buf = ac_get_arg(&ctx->ac, ctx->internal_bindings);
      buf = ac_build_load_to_sgpr(&ctx->ac, buf, slot);
      int offset = op == nir_intrinsic_load_tess_level_inner_default ? 4 : 0;
      LLVMValueRef val[4];

      for (int i = 0; i < 4; i++)
         val[i] = si_buffer_load_const(ctx, buf, LLVMConstInt(ctx->ac.i32, (offset + i) * 4, 0));
      return ac_build_gather_values(&ctx->ac, val, 4);
   }

   case nir_intrinsic_load_patch_vertices_in:
      if (ctx->stage == MESA_SHADER_TESS_CTRL)
         return si_unpack_param(ctx, ctx->tcs_out_lds_layout, 13, 6);
      else if (ctx->stage == MESA_SHADER_TESS_EVAL)
         return si_get_num_tcs_out_vertices(ctx);
      else
         return NULL;

   case nir_intrinsic_load_sample_mask_in:
      return ac_to_integer(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args.sample_coverage));

   case nir_intrinsic_load_lshs_vertex_stride_amd:
      return LLVMBuildShl(ctx->ac.builder, si_get_tcs_in_vertex_dw_stride(ctx),
                          LLVMConstInt(ctx->ac.i32, 2, 0), "");

   case nir_intrinsic_load_tcs_num_patches_amd:
      return LLVMBuildAdd(ctx->ac.builder,
                          si_unpack_param(ctx, ctx->tcs_offchip_layout, 0, 6),
                          ctx->ac.i32_1, "");

   case nir_intrinsic_load_hs_out_patch_data_offset_amd:
      return si_unpack_param(ctx, ctx->tcs_offchip_layout, 11, 21);

   case nir_intrinsic_load_ring_tess_offchip_amd:
      return ctx->tess_offchip_ring;

   case nir_intrinsic_load_ring_tess_offchip_offset_amd:
      return ac_get_arg(&ctx->ac, ctx->args.tess_offchip_offset);

   case nir_intrinsic_load_tess_rel_patch_id_amd:
      return si_get_rel_patch_id(ctx);

   case nir_intrinsic_load_ring_esgs_amd:
      return ctx->esgs_ring;

   case nir_intrinsic_load_ring_es2gs_offset_amd:
      return ac_get_arg(&ctx->ac, ctx->args.es2gs_offset);

   case nir_intrinsic_load_clip_half_line_width_amd: {
      LLVMValueRef ptr =
         LLVMBuildPointerCast(ctx->ac.builder, ac_get_arg(&ctx->ac, ctx->small_prim_cull_info),
                              LLVMPointerType(ctx->ac.v2f32, AC_ADDR_SPACE_CONST_32BIT), "");
      return ac_build_load_to_sgpr(&ctx->ac, ptr, LLVMConstInt(ctx->ac.i32, 4, 0));
   }

   case nir_intrinsic_load_viewport_xy_scale_and_offset: {
      bool prim_is_lines = ctx->shader->key.ge.opt.ngg_culling & SI_NGG_CULL_LINES;
      LLVMValueRef ptr = ac_get_arg(&ctx->ac, ctx->small_prim_cull_info);
      LLVMValueRef terms =
         ac_build_load_to_sgpr(&ctx->ac, ptr, prim_is_lines ? ctx->ac.i32_1 : ctx->ac.i32_0);
      return LLVMBuildBitCast(ctx->ac.builder, terms, ctx->ac.v4f32, "");
   }

   case nir_intrinsic_load_cull_ccw_amd:
      /* radeonsi embed cw/ccw info into front/back face enabled */
      return ctx->ac.i1false;

   case nir_intrinsic_load_cull_any_enabled_amd:
      return ctx->shader->key.ge.opt.ngg_culling ? ctx->ac.i1true : ctx->ac.i1false;

   case nir_intrinsic_load_cull_back_face_enabled_amd:
      return ctx->shader->key.ge.opt.ngg_culling & SI_NGG_CULL_BACK_FACE ?
         ctx->ac.i1true : ctx->ac.i1false;

   case nir_intrinsic_load_cull_front_face_enabled_amd:
      return ctx->shader->key.ge.opt.ngg_culling & SI_NGG_CULL_FRONT_FACE ?
         ctx->ac.i1true : ctx->ac.i1false;

   case nir_intrinsic_load_cull_small_prim_precision_amd: {
      LLVMValueRef small_prim_precision =
         ctx->shader->key.ge.opt.ngg_culling & SI_NGG_CULL_LINES ?
         GET_FIELD(ctx, GS_STATE_SMALL_PRIM_PRECISION_NO_AA) :
         GET_FIELD(ctx, GS_STATE_SMALL_PRIM_PRECISION);

      /* Extract the small prim precision. */
      small_prim_precision =
         LLVMBuildOr(ctx->ac.builder, small_prim_precision,
                     LLVMConstInt(ctx->ac.i32, 0x70, 0), "");
      small_prim_precision =
         LLVMBuildShl(ctx->ac.builder, small_prim_precision,
                      LLVMConstInt(ctx->ac.i32, 23, 0), "");

      return LLVMBuildBitCast(ctx->ac.builder, small_prim_precision, ctx->ac.f32, "");
   }

   case nir_intrinsic_load_cull_small_primitives_enabled_amd:
      if (ctx->shader->key.ge.opt.ngg_culling & SI_NGG_CULL_LINES)
         return ctx->shader->key.ge.opt.ngg_culling & SI_NGG_CULL_SMALL_LINES_DIAMOND_EXIT ?
            ctx->ac.i1true : ctx->ac.i1false;
      else
         return ctx->ac.i1true;

   default:
      return NULL;
   }
}

static LLVMValueRef si_llvm_load_user_clip_plane(struct ac_shader_abi *abi, unsigned ucp_id)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   LLVMValueRef ptr = ac_get_arg(&ctx->ac, ctx->internal_bindings);
   LLVMValueRef constbuf_index = LLVMConstInt(ctx->ac.i32, SI_VS_CONST_CLIP_PLANES, 0);
   LLVMValueRef const_resource = ac_build_load_to_sgpr(&ctx->ac, ptr, constbuf_index);
   LLVMValueRef addr = LLVMConstInt(ctx->ac.i32, ucp_id * 16, 0);
   return ac_build_buffer_load(&ctx->ac, const_resource, 4, NULL, addr, NULL,
                               ctx->ac.f32, 0, true, true);
}

bool si_llvm_translate_nir(struct si_shader_context *ctx, struct si_shader *shader,
                           struct nir_shader *nir, bool free_nir, bool ngg_cull_shader)
{
   struct si_shader_selector *sel = shader->selector;
   const struct si_shader_info *info = &sel->info;

   ctx->shader = shader;
   ctx->stage = sel->stage;

   ctx->num_const_buffers = info->base.num_ubos;
   ctx->num_shader_buffers = info->base.num_ssbos;

   ctx->num_samplers = BITSET_LAST_BIT(info->base.textures_used);
   ctx->num_images = info->base.num_images;

   ctx->abi.intrinsic_load = si_llvm_load_intrinsic;
   ctx->abi.load_user_clip_plane = si_llvm_load_user_clip_plane;

   si_llvm_init_resource_callbacks(ctx);
   si_llvm_create_main_func(ctx, ngg_cull_shader);

   if (ctx->stage <= MESA_SHADER_GEOMETRY &&
       (ctx->shader->key.ge.as_es || ctx->stage == MESA_SHADER_GEOMETRY))
      si_preload_esgs_ring(ctx);

   switch (ctx->stage) {
   case MESA_SHADER_VERTEX:
      si_llvm_init_vs_callbacks(ctx, ngg_cull_shader);

      /* preload instance_divisor_constbuf to be used for input load after culling */
      if (ctx->shader->key.ge.opt.ngg_culling &&
          ctx->shader->key.ge.part.vs.prolog.instance_divisor_is_fetched) {
         LLVMValueRef buf = ac_get_arg(&ctx->ac, ctx->internal_bindings);
         ctx->instance_divisor_constbuf =
            ac_build_load_to_sgpr(
               &ctx->ac, buf, LLVMConstInt(ctx->ac.i32, SI_VS_CONST_INSTANCE_DIVISORS, 0));
      }
      break;

   case MESA_SHADER_TESS_CTRL:
      si_llvm_init_tcs_callbacks(ctx);
      si_llvm_preload_tess_rings(ctx);
      break;

   case MESA_SHADER_TESS_EVAL:
      si_llvm_preload_tess_rings(ctx);
      break;

   case MESA_SHADER_GEOMETRY:
      si_llvm_init_gs_callbacks(ctx);

      if (!ctx->shader->key.ge.as_ngg)
         si_preload_gs_rings(ctx);

      for (unsigned i = 0; i < 4; i++)
         ctx->gs_next_vertex[i] = ac_build_alloca(&ctx->ac, ctx->ac.i32, "");

      if (shader->key.ge.as_ngg) {
         for (unsigned i = 0; i < 4; ++i) {
            ctx->gs_curprim_verts[i] = ac_build_alloca(&ctx->ac, ctx->ac.i32, "");
            ctx->gs_generated_prims[i] = ac_build_alloca(&ctx->ac, ctx->ac.i32, "");
         }

         assert(!ctx->gs_ngg_scratch);
         LLVMTypeRef ai32 = LLVMArrayType(ctx->ac.i32, gfx10_ngg_get_scratch_dw_size(shader));
         ctx->gs_ngg_scratch =
            LLVMAddGlobalInAddressSpace(ctx->ac.module, ai32, "ngg_scratch", AC_ADDR_SPACE_LDS);
         LLVMSetInitializer(ctx->gs_ngg_scratch, LLVMGetUndef(ai32));
         LLVMSetAlignment(ctx->gs_ngg_scratch, 4);

         ctx->gs_ngg_emit = LLVMAddGlobalInAddressSpace(
            ctx->ac.module, LLVMArrayType(ctx->ac.i32, 0), "ngg_emit", AC_ADDR_SPACE_LDS);
         LLVMSetLinkage(ctx->gs_ngg_emit, LLVMExternalLinkage);
         LLVMSetAlignment(ctx->gs_ngg_emit, 4);
      } else {
         ctx->gs_emitted_vertices = LLVMConstInt(ctx->ac.i32, 0, false);
      }
      break;

   case MESA_SHADER_FRAGMENT: {
      si_llvm_init_ps_callbacks(ctx);

      unsigned colors_read = ctx->shader->selector->info.colors_read;
      LLVMValueRef main_fn = ctx->main_fn;

      LLVMValueRef undef = LLVMGetUndef(ctx->ac.f32);

      unsigned offset = SI_PARAM_POS_FIXED_PT + 1;

      if (colors_read & 0x0f) {
         unsigned mask = colors_read & 0x0f;
         LLVMValueRef values[4];
         values[0] = mask & 0x1 ? LLVMGetParam(main_fn, offset++) : undef;
         values[1] = mask & 0x2 ? LLVMGetParam(main_fn, offset++) : undef;
         values[2] = mask & 0x4 ? LLVMGetParam(main_fn, offset++) : undef;
         values[3] = mask & 0x8 ? LLVMGetParam(main_fn, offset++) : undef;
         ctx->abi.color0 = ac_to_integer(&ctx->ac, ac_build_gather_values(&ctx->ac, values, 4));
      }
      if (colors_read & 0xf0) {
         unsigned mask = (colors_read & 0xf0) >> 4;
         LLVMValueRef values[4];
         values[0] = mask & 0x1 ? LLVMGetParam(main_fn, offset++) : undef;
         values[1] = mask & 0x2 ? LLVMGetParam(main_fn, offset++) : undef;
         values[2] = mask & 0x4 ? LLVMGetParam(main_fn, offset++) : undef;
         values[3] = mask & 0x8 ? LLVMGetParam(main_fn, offset++) : undef;
         ctx->abi.color1 = ac_to_integer(&ctx->ac, ac_build_gather_values(&ctx->ac, values, 4));
      }

      ctx->abi.num_interp = si_get_ps_num_interp(shader);
      ctx->abi.interp_at_sample_force_center =
         ctx->shader->key.ps.mono.interpolate_at_sample_force_center;

      ctx->abi.kill_ps_if_inf_interp =
         ctx->screen->options.no_infinite_interp &&
         (ctx->shader->selector->info.uses_persp_center ||
          ctx->shader->selector->info.uses_persp_centroid ||
          ctx->shader->selector->info.uses_persp_sample);
      break;
   }

   case MESA_SHADER_COMPUTE:
      if (nir->info.cs.user_data_components_amd) {
         ctx->abi.user_data = ac_get_arg(&ctx->ac, ctx->cs_user_data);
         ctx->abi.user_data = ac_build_expand_to_vec4(&ctx->ac, ctx->abi.user_data,
                                                      nir->info.cs.user_data_components_amd);
      }

      if (ctx->shader->selector->info.base.shared_size)
         si_llvm_declare_compute_memory(ctx);
      break;

   default:
      break;
   }

   if ((ctx->stage == MESA_SHADER_VERTEX || ctx->stage == MESA_SHADER_TESS_EVAL) &&
       shader->key.ge.as_ngg && !shader->key.ge.as_es) {
      /* Unconditionally declare scratch space base for streamout and
       * vertex compaction. Whether space is actually allocated is
       * determined during linking / PM4 creation.
       */
      si_llvm_declare_esgs_ring(ctx);

      /* This is really only needed when streamout and / or vertex
       * compaction is enabled.
       */
      if (!ctx->gs_ngg_scratch && (ctx->so.num_outputs || shader->key.ge.opt.ngg_culling)) {
         LLVMTypeRef asi32 = LLVMArrayType(ctx->ac.i32, gfx10_ngg_get_scratch_dw_size(shader));
         ctx->gs_ngg_scratch =
            LLVMAddGlobalInAddressSpace(ctx->ac.module, asi32, "ngg_scratch", AC_ADDR_SPACE_LDS);
         LLVMSetInitializer(ctx->gs_ngg_scratch, LLVMGetUndef(asi32));
         LLVMSetAlignment(ctx->gs_ngg_scratch, 4);
      }
   }

   /* For merged shaders (VS-TCS, VS-GS, TES-GS): */
   if (ctx->screen->info.gfx_level >= GFX9 && si_is_merged_shader(shader)) {
      /* TES is special because it has only 1 shader part if NGG shader culling is disabled,
       * and therefore it doesn't use the wrapper function.
       */
      bool no_wrapper_func = ctx->stage == MESA_SHADER_TESS_EVAL && !shader->key.ge.as_es &&
                             !shader->key.ge.opt.ngg_culling;

      /* Set EXEC = ~0 before the first shader. If the prolog is present, EXEC is set there
       * instead. For monolithic shaders, the wrapper function does this.
       */
      if ((!shader->is_monolithic || no_wrapper_func) &&
          (ctx->stage == MESA_SHADER_TESS_EVAL ||
           (ctx->stage == MESA_SHADER_VERTEX &&
            !si_vs_needs_prolog(sel, &shader->key.ge.part.vs.prolog, &shader->key, ngg_cull_shader,
                                false))))
         ac_init_exec_full_mask(&ctx->ac);

      /* NGG VS and NGG TES: Send gs_alloc_req and the prim export at the beginning to decrease
       * register usage.
       */
      if ((ctx->stage == MESA_SHADER_VERTEX || ctx->stage == MESA_SHADER_TESS_EVAL) &&
          shader->key.ge.as_ngg && !shader->key.ge.as_es && !shader->key.ge.opt.ngg_culling) {
         /* GFX10 requires a barrier before gs_alloc_req due to a hw bug. */
         if (ctx->screen->info.gfx_level == GFX10)
            ac_build_s_barrier(&ctx->ac, ctx->stage);

         gfx10_ngg_build_sendmsg_gs_alloc_req(ctx);

         /* Build the primitive export at the beginning
          * of the shader if possible.
          */
         if (gfx10_ngg_export_prim_early(shader))
            gfx10_ngg_build_export_prim(ctx, NULL, NULL);
      }

      /* NGG GS: Initialize LDS and insert s_barrier, which must not be inside the if statement. */
      if (ctx->stage == MESA_SHADER_GEOMETRY && shader->key.ge.as_ngg)
         gfx10_ngg_gs_emit_begin(ctx);

      LLVMValueRef thread_enabled = NULL;

      if (ctx->stage == MESA_SHADER_GEOMETRY ||
          (ctx->stage == MESA_SHADER_TESS_CTRL && !shader->is_monolithic)) {
         /* Wrap both shaders in an if statement according to the number of enabled threads
          * there. For monolithic TCS, the if statement is inserted by the wrapper function,
          * not here.
          */
         thread_enabled = si_is_gs_thread(ctx); /* 2nd shader: thread enabled bool */
      } else if (((shader->key.ge.as_ls || shader->key.ge.as_es) && !shader->is_monolithic) ||
                 (shader->key.ge.as_ngg && !shader->key.ge.as_es)) {
         /* This is NGG VS or NGG TES or VS before GS or TES before GS or VS before TCS.
          * For monolithic LS (VS before TCS) and ES (VS before GS and TES before GS),
          * the if statement is inserted by the wrapper function.
          */
         thread_enabled = si_is_es_thread(ctx); /* 1st shader: thread enabled bool */
      }

      if (thread_enabled) {
         ctx->merged_wrap_if_entry_block = LLVMGetInsertBlock(ctx->ac.builder);
         ctx->merged_wrap_if_label = 11500;
         ac_build_ifcc(&ctx->ac, thread_enabled, ctx->merged_wrap_if_label);
      }

      /* Execute a barrier before the second shader in
       * a merged shader.
       *
       * Execute the barrier inside the conditional block,
       * so that empty waves can jump directly to s_endpgm,
       * which will also signal the barrier.
       *
       * This is possible in gfx9, because an empty wave for the second shader does not insert
       * any ending. With NGG, empty waves may still be required to export data (e.g. GS output
       * vertices), so we cannot let them exit early.
       *
       * If the shader is TCS and the TCS epilog is present
       * and contains a barrier, it will wait there and then
       * reach s_endpgm.
       */
      if (ctx->stage == MESA_SHADER_TESS_CTRL) {
         /* We need the barrier only if TCS inputs are read from LDS. */
         if (!shader->key.ge.opt.same_patch_vertices ||
             shader->selector->info.base.inputs_read &
             ~shader->selector->info.tcs_vgpr_only_inputs) {
            ac_build_waitcnt(&ctx->ac, AC_WAIT_LGKM);

            /* If both input and output patches are wholly in one wave, we don't need a barrier.
             * That's true when both VS and TCS have the same number of patch vertices and
             * the wave size is a multiple of the number of patch vertices.
             */
            if (!shader->key.ge.opt.same_patch_vertices ||
                ctx->ac.wave_size % sel->info.base.tess.tcs_vertices_out != 0)
               ac_build_s_barrier(&ctx->ac, ctx->stage);
         }
      } else if (ctx->stage == MESA_SHADER_GEOMETRY && !shader->key.ge.as_ngg) {
         /* gfx10_ngg_gs_emit_begin inserts the barrier for NGG. */
         ac_build_waitcnt(&ctx->ac, AC_WAIT_LGKM);
         ac_build_s_barrier(&ctx->ac, ctx->stage);
      }
   }

   ctx->abi.clamp_shadow_reference = true;
   ctx->abi.robust_buffer_access = true;
   ctx->abi.convert_undef_to_zero = true;
   ctx->abi.load_grid_size_from_user_sgpr = true;
   ctx->abi.clamp_div_by_zero = ctx->screen->options.clamp_div_by_zero ||
                                info->options & SI_PROFILE_CLAMP_DIV_BY_ZERO;
   ctx->abi.use_waterfall_for_divergent_tex_samplers = true;

   for (unsigned i = 0; i < info->num_outputs; i++) {
      LLVMTypeRef type = ctx->ac.f32;

      /* Only FS uses unpacked f16. Other stages pack 16-bit outputs into low and high bits of f32. */
      if (nir->info.stage == MESA_SHADER_FRAGMENT &&
          nir_alu_type_get_type_size(ctx->shader->selector->info.output_type[i]) == 16)
         type = ctx->ac.f16;

      for (unsigned j = 0; j < 4; j++) {
         ctx->abi.outputs[i * 4 + j] = ac_build_alloca_undef(&ctx->ac, type, "");
         ctx->abi.is_16bit[i * 4 + j] = type == ctx->ac.f16;
      }
   }

   if (!ac_nir_translate(&ctx->ac, &ctx->abi, &ctx->args, nir))
      return false;

   switch (sel->stage) {
   case MESA_SHADER_VERTEX:
      if (shader->key.ge.as_ls)
         si_llvm_ls_build_end(ctx);
      else if (shader->key.ge.as_es)
         si_llvm_es_build_end(ctx);
      else if (ngg_cull_shader)
         gfx10_ngg_culling_build_end(ctx);
      else if (shader->key.ge.as_ngg)
         gfx10_ngg_build_end(ctx);
      else
         si_llvm_vs_build_end(ctx);
      break;

   case MESA_SHADER_TESS_CTRL:
      si_llvm_tcs_build_end(ctx);
      break;

   case MESA_SHADER_TESS_EVAL:
      if (ctx->shader->key.ge.as_es)
         si_llvm_es_build_end(ctx);
      else if (ngg_cull_shader)
         gfx10_ngg_culling_build_end(ctx);
      else if (ctx->shader->key.ge.as_ngg)
         gfx10_ngg_build_end(ctx);
      else
         si_llvm_vs_build_end(ctx);
      break;

   case MESA_SHADER_GEOMETRY:
      if (ctx->shader->key.ge.as_ngg)
         gfx10_ngg_gs_build_end(ctx);
      else
         si_llvm_gs_build_end(ctx);
      break;

   case MESA_SHADER_FRAGMENT:
      si_llvm_ps_build_end(ctx);
      break;

   default:
      break;
   }

   si_llvm_build_ret(ctx, ctx->return_value);

   if (free_nir)
      ralloc_free(nir);
   return true;
}

static bool si_should_optimize_less(struct ac_llvm_compiler *compiler,
                                    struct si_shader_selector *sel)
{
   if (!compiler->low_opt_passes)
      return false;

   /* Assume a slow CPU. */
   assert(!sel->screen->info.has_dedicated_vram && sel->screen->info.gfx_level <= GFX8);

   /* For a crazy dEQP test containing 2597 memory opcodes, mostly
    * buffer stores. */
   return sel->stage == MESA_SHADER_COMPUTE && sel->info.num_memory_stores > 1000;
}

bool si_llvm_compile_shader(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                            struct si_shader *shader, const struct pipe_stream_output_info *so,
                            struct util_debug_callback *debug, struct nir_shader *nir,
                            bool free_nir)
{
   struct si_shader_selector *sel = shader->selector;
   struct si_shader_context ctx;

   si_llvm_context_init(&ctx, sscreen, compiler, shader->wave_size);
   ctx.so = *so;

   LLVMValueRef ngg_cull_main_fn = NULL;
   if (sel->stage <= MESA_SHADER_TESS_EVAL && shader->key.ge.opt.ngg_culling) {
      if (!si_llvm_translate_nir(&ctx, shader, nir, false, true)) {
         si_llvm_dispose(&ctx);
         return false;
      }
      ngg_cull_main_fn = ctx.main_fn;
      ctx.main_fn = NULL;
   }

   if (!si_llvm_translate_nir(&ctx, shader, nir, free_nir, false)) {
      si_llvm_dispose(&ctx);
      return false;
   }

   if (shader->is_monolithic && sel->stage == MESA_SHADER_VERTEX) {
      LLVMValueRef parts[4];
      unsigned num_parts = 0;
      bool first_is_prolog = false;
      LLVMValueRef main_fn = ctx.main_fn;

      if (ngg_cull_main_fn) {
         if (si_vs_needs_prolog(sel, &shader->key.ge.part.vs.prolog, &shader->key, true, false)) {
            union si_shader_part_key prolog_key;
            si_get_vs_prolog_key(&sel->info, shader->info.num_input_sgprs, true,
                                 &shader->key.ge.part.vs.prolog, shader, &prolog_key);
            prolog_key.vs_prolog.is_monolithic = true;
            si_llvm_build_vs_prolog(&ctx, &prolog_key);
            parts[num_parts++] = ctx.main_fn;
            first_is_prolog = true;
         }
         parts[num_parts++] = ngg_cull_main_fn;
      }

      if (si_vs_needs_prolog(sel, &shader->key.ge.part.vs.prolog, &shader->key, false, false)) {
         union si_shader_part_key prolog_key;
         si_get_vs_prolog_key(&sel->info, shader->info.num_input_sgprs, false,
                              &shader->key.ge.part.vs.prolog, shader, &prolog_key);
         prolog_key.vs_prolog.is_monolithic = true;
         si_llvm_build_vs_prolog(&ctx, &prolog_key);
         parts[num_parts++] = ctx.main_fn;
         if (num_parts == 1)
            first_is_prolog = true;
      }
      parts[num_parts++] = main_fn;

      si_build_wrapper_function(&ctx, parts, num_parts, first_is_prolog ? 1 : 0, 0, false);
   } else if (shader->is_monolithic && sel->stage == MESA_SHADER_TESS_EVAL && ngg_cull_main_fn) {
      LLVMValueRef parts[3], prolog, main_fn = ctx.main_fn;

      /* We reuse the VS prolog code for TES just to load the input VGPRs from LDS. */
      union si_shader_part_key prolog_key;
      memset(&prolog_key, 0, sizeof(prolog_key));
      prolog_key.vs_prolog.num_input_sgprs = shader->info.num_input_sgprs;
      prolog_key.vs_prolog.num_merged_next_stage_vgprs = 5;
      prolog_key.vs_prolog.as_ngg = 1;
      prolog_key.vs_prolog.load_vgprs_after_culling = 1;
      prolog_key.vs_prolog.is_monolithic = true;
      si_llvm_build_vs_prolog(&ctx, &prolog_key);
      prolog = ctx.main_fn;

      parts[0] = ngg_cull_main_fn;
      parts[1] = prolog;
      parts[2] = main_fn;

      si_build_wrapper_function(&ctx, parts, 3, 0, 0, false);
   } else if (shader->is_monolithic && sel->stage == MESA_SHADER_TESS_CTRL) {
      if (sscreen->info.gfx_level >= GFX9) {
         struct si_shader_selector *ls = shader->key.ge.part.tcs.ls;
         LLVMValueRef parts[4];
         bool vs_needs_prolog =
            si_vs_needs_prolog(ls, &shader->key.ge.part.tcs.ls_prolog, &shader->key, false, false);

         /* TCS main part */
         parts[2] = ctx.main_fn;

         /* TCS epilog */
         union si_shader_part_key tcs_epilog_key;
         si_get_tcs_epilog_key(shader, &tcs_epilog_key);
         si_llvm_build_tcs_epilog(&ctx, &tcs_epilog_key);
         parts[3] = ctx.main_fn;

         struct si_shader shader_ls = {};
         shader_ls.selector = ls;
         shader_ls.key.ge.part.vs.prolog = shader->key.ge.part.tcs.ls_prolog;
         shader_ls.key.ge.as_ls = 1;
         shader_ls.key.ge.mono = shader->key.ge.mono;
         shader_ls.key.ge.opt = shader->key.ge.opt;
         shader_ls.key.ge.opt.inline_uniforms = false; /* only TCS can inline uniforms */
         shader_ls.is_monolithic = true;

         nir = si_get_nir_shader(&shader_ls, &free_nir, sel->info.tcs_vgpr_only_inputs);
         si_update_shader_binary_info(shader, nir);

         if (!si_llvm_translate_nir(&ctx, &shader_ls, nir, free_nir, false)) {
            si_llvm_dispose(&ctx);
            return false;
         }
         shader->info.uses_instanceid |= ls->info.uses_instanceid;
         parts[1] = ctx.main_fn;

         /* LS prolog */
         if (vs_needs_prolog) {
            union si_shader_part_key vs_prolog_key;
            si_get_vs_prolog_key(&ls->info, shader_ls.info.num_input_sgprs, false,
                                 &shader->key.ge.part.tcs.ls_prolog, shader, &vs_prolog_key);
            vs_prolog_key.vs_prolog.is_monolithic = true;
            si_llvm_build_vs_prolog(&ctx, &vs_prolog_key);
            parts[0] = ctx.main_fn;
         }

         /* Reset the shader context. */
         ctx.shader = shader;
         ctx.stage = MESA_SHADER_TESS_CTRL;

         si_build_wrapper_function(&ctx, parts + !vs_needs_prolog, 4 - !vs_needs_prolog,
                                   vs_needs_prolog, vs_needs_prolog ? 2 : 1,
                                   shader->key.ge.opt.same_patch_vertices);
      } else {
         LLVMValueRef parts[2];
         union si_shader_part_key epilog_key;

         parts[0] = ctx.main_fn;

         memset(&epilog_key, 0, sizeof(epilog_key));
         epilog_key.tcs_epilog.states = shader->key.ge.part.tcs.epilog;
         si_llvm_build_tcs_epilog(&ctx, &epilog_key);
         parts[1] = ctx.main_fn;

         si_build_wrapper_function(&ctx, parts, 2, 0, 0, false);
      }
   } else if (shader->is_monolithic && sel->stage == MESA_SHADER_GEOMETRY) {
      if (ctx.screen->info.gfx_level >= GFX9) {
         struct si_shader_selector *es = shader->key.ge.part.gs.es;
         LLVMValueRef es_prolog = NULL;
         LLVMValueRef es_main = NULL;
         LLVMValueRef gs_main = ctx.main_fn;

         /* ES main part */
         struct si_shader shader_es = {};
         shader_es.selector = es;
         shader_es.key.ge.part.vs.prolog = shader->key.ge.part.gs.vs_prolog;
         shader_es.key.ge.as_es = 1;
         shader_es.key.ge.as_ngg = shader->key.ge.as_ngg;
         shader_es.key.ge.mono = shader->key.ge.mono;
         shader_es.key.ge.opt = shader->key.ge.opt;
         shader_es.key.ge.opt.inline_uniforms = false; /* only GS can inline uniforms */
         /* kill_outputs was computed based on GS outputs so we can't use it to kill VS outputs */
         shader_es.key.ge.opt.kill_outputs = 0;
         shader_es.is_monolithic = true;

         nir = si_get_nir_shader(&shader_es, &free_nir, 0);
         si_update_shader_binary_info(shader, nir);

         if (!si_llvm_translate_nir(&ctx, &shader_es, nir, free_nir, false)) {
            si_llvm_dispose(&ctx);
            return false;
         }
         shader->info.uses_instanceid |= es->info.uses_instanceid;
         es_main = ctx.main_fn;

         /* ES prolog */
         if (es->stage == MESA_SHADER_VERTEX &&
             si_vs_needs_prolog(es, &shader->key.ge.part.gs.vs_prolog, &shader->key, false, true)) {
            union si_shader_part_key vs_prolog_key;
            si_get_vs_prolog_key(&es->info, shader_es.info.num_input_sgprs, false,
                                 &shader->key.ge.part.gs.vs_prolog, shader, &vs_prolog_key);
            vs_prolog_key.vs_prolog.is_monolithic = true;
            si_llvm_build_vs_prolog(&ctx, &vs_prolog_key);
            es_prolog = ctx.main_fn;
         }

         /* Reset the shader context. */
         ctx.shader = shader;
         ctx.stage = MESA_SHADER_GEOMETRY;

         /* Prepare the array of shader parts. */
         LLVMValueRef parts[4];
         unsigned num_parts = 0, main_part;

         if (es_prolog)
            parts[num_parts++] = es_prolog;

         parts[main_part = num_parts++] = es_main;
         parts[num_parts++] = gs_main;

         si_build_wrapper_function(&ctx, parts, num_parts, main_part, main_part + 1, false);
      } else {
         /* Nothing to do for gfx6-8. The shader has only 1 part and it's ctx.main_fn. */
      }
   } else if (shader->is_monolithic && sel->stage == MESA_SHADER_FRAGMENT) {
      si_llvm_build_monolithic_ps(&ctx, shader);
   }

   si_llvm_optimize_module(&ctx);

   /* Make sure the input is a pointer and not integer followed by inttoptr. */
   assert(LLVMGetTypeKind(LLVMTypeOf(LLVMGetParam(ctx.main_fn, 0))) == LLVMPointerTypeKind);

   /* Compile to bytecode. */
   if (!si_compile_llvm(sscreen, &shader->binary, &shader->config, compiler, &ctx.ac, debug,
                        sel->stage, si_get_shader_name(shader),
                        si_should_optimize_less(compiler, shader->selector))) {
      si_llvm_dispose(&ctx);
      fprintf(stderr, "LLVM failed to compile shader\n");
      return false;
   }

   si_llvm_dispose(&ctx);
   return true;
}
