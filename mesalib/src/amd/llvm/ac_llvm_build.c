/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/* based on pieces from si_pipe.c and radeon_llvm_emit.c */
#include "ac_llvm_build.h"

#include "ac_exp_param.h"
#include "ac_llvm_util.h"
#include "ac_shader_util.h"
#include "c11/threads.h"
#include "shader_enums.h"
#include "sid.h"
#include "util/bitscan.h"
#include "util/macros.h"
#include "util/u_atomic.h"
#include "util/u_math.h"
#include <llvm-c/Core.h>
#include <llvm/Config/llvm-config.h>

#include <assert.h>
#include <stdio.h>

#define AC_LLVM_INITIAL_CF_DEPTH 4

/* Data for if/else/endif and bgnloop/endloop control flow structures.
 */
struct ac_llvm_flow {
   /* Loop exit or next part of if/else/endif. */
   LLVMBasicBlockRef next_block;
   LLVMBasicBlockRef loop_entry_block;
};

/* Initialize module-independent parts of the context.
 *
 * The caller is responsible for initializing ctx::module and ctx::builder.
 */
void ac_llvm_context_init(struct ac_llvm_context *ctx, struct ac_llvm_compiler *compiler,
                          enum chip_class chip_class, enum radeon_family family,
                          const struct radeon_info *info,
                          enum ac_float_mode float_mode, unsigned wave_size,
                          unsigned ballot_mask_bits)
{
   ctx->context = LLVMContextCreate();

   ctx->chip_class = chip_class;
   ctx->family = family;
   ctx->info = info;
   ctx->wave_size = wave_size;
   ctx->ballot_mask_bits = ballot_mask_bits;
   ctx->float_mode = float_mode;
   ctx->module = ac_create_module(compiler->tm, ctx->context);
   ctx->builder = ac_create_builder(ctx->context, float_mode);

   ctx->voidt = LLVMVoidTypeInContext(ctx->context);
   ctx->i1 = LLVMInt1TypeInContext(ctx->context);
   ctx->i8 = LLVMInt8TypeInContext(ctx->context);
   ctx->i16 = LLVMIntTypeInContext(ctx->context, 16);
   ctx->i32 = LLVMIntTypeInContext(ctx->context, 32);
   ctx->i64 = LLVMIntTypeInContext(ctx->context, 64);
   ctx->i128 = LLVMIntTypeInContext(ctx->context, 128);
   ctx->intptr = ctx->i32;
   ctx->f16 = LLVMHalfTypeInContext(ctx->context);
   ctx->f32 = LLVMFloatTypeInContext(ctx->context);
   ctx->f64 = LLVMDoubleTypeInContext(ctx->context);
   ctx->v2i16 = LLVMVectorType(ctx->i16, 2);
   ctx->v4i16 = LLVMVectorType(ctx->i16, 4);
   ctx->v2f16 = LLVMVectorType(ctx->f16, 2);
   ctx->v4f16 = LLVMVectorType(ctx->f16, 4);
   ctx->v2i32 = LLVMVectorType(ctx->i32, 2);
   ctx->v3i32 = LLVMVectorType(ctx->i32, 3);
   ctx->v4i32 = LLVMVectorType(ctx->i32, 4);
   ctx->v2f32 = LLVMVectorType(ctx->f32, 2);
   ctx->v3f32 = LLVMVectorType(ctx->f32, 3);
   ctx->v4f32 = LLVMVectorType(ctx->f32, 4);
   ctx->v8i32 = LLVMVectorType(ctx->i32, 8);
   ctx->iN_wavemask = LLVMIntTypeInContext(ctx->context, ctx->wave_size);
   ctx->iN_ballotmask = LLVMIntTypeInContext(ctx->context, ballot_mask_bits);

   ctx->i8_0 = LLVMConstInt(ctx->i8, 0, false);
   ctx->i8_1 = LLVMConstInt(ctx->i8, 1, false);
   ctx->i16_0 = LLVMConstInt(ctx->i16, 0, false);
   ctx->i16_1 = LLVMConstInt(ctx->i16, 1, false);
   ctx->i32_0 = LLVMConstInt(ctx->i32, 0, false);
   ctx->i32_1 = LLVMConstInt(ctx->i32, 1, false);
   ctx->i64_0 = LLVMConstInt(ctx->i64, 0, false);
   ctx->i64_1 = LLVMConstInt(ctx->i64, 1, false);
   ctx->i128_0 = LLVMConstInt(ctx->i128, 0, false);
   ctx->i128_1 = LLVMConstInt(ctx->i128, 1, false);
   ctx->f16_0 = LLVMConstReal(ctx->f16, 0.0);
   ctx->f16_1 = LLVMConstReal(ctx->f16, 1.0);
   ctx->f32_0 = LLVMConstReal(ctx->f32, 0.0);
   ctx->f32_1 = LLVMConstReal(ctx->f32, 1.0);
   ctx->f64_0 = LLVMConstReal(ctx->f64, 0.0);
   ctx->f64_1 = LLVMConstReal(ctx->f64, 1.0);

   ctx->i1false = LLVMConstInt(ctx->i1, 0, false);
   ctx->i1true = LLVMConstInt(ctx->i1, 1, false);

   ctx->range_md_kind = LLVMGetMDKindIDInContext(ctx->context, "range", 5);

   ctx->invariant_load_md_kind = LLVMGetMDKindIDInContext(ctx->context, "invariant.load", 14);

   ctx->uniform_md_kind = LLVMGetMDKindIDInContext(ctx->context, "amdgpu.uniform", 14);

   ctx->empty_md = LLVMMDNodeInContext(ctx->context, NULL, 0);
   ctx->flow = calloc(1, sizeof(*ctx->flow));
}

void ac_llvm_context_dispose(struct ac_llvm_context *ctx)
{
   free(ctx->flow->stack);
   free(ctx->flow);
   ctx->flow = NULL;
}

int ac_get_llvm_num_components(LLVMValueRef value)
{
   LLVMTypeRef type = LLVMTypeOf(value);
   unsigned num_components =
      LLVMGetTypeKind(type) == LLVMVectorTypeKind ? LLVMGetVectorSize(type) : 1;
   return num_components;
}

LLVMValueRef ac_llvm_extract_elem(struct ac_llvm_context *ac, LLVMValueRef value, int index)
{
   if (LLVMGetTypeKind(LLVMTypeOf(value)) != LLVMVectorTypeKind) {
      assert(index == 0);
      return value;
   }

   return LLVMBuildExtractElement(ac->builder, value, LLVMConstInt(ac->i32, index, false), "");
}

int ac_get_elem_bits(struct ac_llvm_context *ctx, LLVMTypeRef type)
{
   if (LLVMGetTypeKind(type) == LLVMVectorTypeKind)
      type = LLVMGetElementType(type);

   if (LLVMGetTypeKind(type) == LLVMIntegerTypeKind)
      return LLVMGetIntTypeWidth(type);

   if (LLVMGetTypeKind(type) == LLVMPointerTypeKind) {
      if (LLVMGetPointerAddressSpace(type) == AC_ADDR_SPACE_LDS)
         return 32;
   }

   if (type == ctx->f16)
      return 16;
   if (type == ctx->f32)
      return 32;
   if (type == ctx->f64)
      return 64;

   unreachable("Unhandled type kind in get_elem_bits");
}

unsigned ac_get_type_size(LLVMTypeRef type)
{
   LLVMTypeKind kind = LLVMGetTypeKind(type);

   switch (kind) {
   case LLVMIntegerTypeKind:
      return LLVMGetIntTypeWidth(type) / 8;
   case LLVMHalfTypeKind:
      return 2;
   case LLVMFloatTypeKind:
      return 4;
   case LLVMDoubleTypeKind:
      return 8;
   case LLVMPointerTypeKind:
      if (LLVMGetPointerAddressSpace(type) == AC_ADDR_SPACE_CONST_32BIT)
         return 4;
      return 8;
   case LLVMVectorTypeKind:
      return LLVMGetVectorSize(type) * ac_get_type_size(LLVMGetElementType(type));
   case LLVMArrayTypeKind:
      return LLVMGetArrayLength(type) * ac_get_type_size(LLVMGetElementType(type));
   default:
      assert(0);
      return 0;
   }
}

static LLVMTypeRef to_integer_type_scalar(struct ac_llvm_context *ctx, LLVMTypeRef t)
{
   if (t == ctx->i1)
      return ctx->i1;
   else if (t == ctx->i8)
      return ctx->i8;
   else if (t == ctx->f16 || t == ctx->i16)
      return ctx->i16;
   else if (t == ctx->f32 || t == ctx->i32)
      return ctx->i32;
   else if (t == ctx->f64 || t == ctx->i64)
      return ctx->i64;
   else
      unreachable("Unhandled integer size");
}

LLVMTypeRef ac_to_integer_type(struct ac_llvm_context *ctx, LLVMTypeRef t)
{
   if (LLVMGetTypeKind(t) == LLVMVectorTypeKind) {
      LLVMTypeRef elem_type = LLVMGetElementType(t);
      return LLVMVectorType(to_integer_type_scalar(ctx, elem_type), LLVMGetVectorSize(t));
   }
   if (LLVMGetTypeKind(t) == LLVMPointerTypeKind) {
      switch (LLVMGetPointerAddressSpace(t)) {
      case AC_ADDR_SPACE_GLOBAL:
         return ctx->i64;
      case AC_ADDR_SPACE_CONST_32BIT:
      case AC_ADDR_SPACE_LDS:
         return ctx->i32;
      default:
         unreachable("unhandled address space");
      }
   }
   return to_integer_type_scalar(ctx, t);
}

LLVMValueRef ac_to_integer(struct ac_llvm_context *ctx, LLVMValueRef v)
{
   LLVMTypeRef type = LLVMTypeOf(v);
   if (LLVMGetTypeKind(type) == LLVMPointerTypeKind) {
      return LLVMBuildPtrToInt(ctx->builder, v, ac_to_integer_type(ctx, type), "");
   }
   return LLVMBuildBitCast(ctx->builder, v, ac_to_integer_type(ctx, type), "");
}

LLVMValueRef ac_to_integer_or_pointer(struct ac_llvm_context *ctx, LLVMValueRef v)
{
   LLVMTypeRef type = LLVMTypeOf(v);
   if (LLVMGetTypeKind(type) == LLVMPointerTypeKind)
      return v;
   return ac_to_integer(ctx, v);
}

static LLVMTypeRef to_float_type_scalar(struct ac_llvm_context *ctx, LLVMTypeRef t)
{
   if (t == ctx->i8)
      return ctx->i8;
   else if (t == ctx->i16 || t == ctx->f16)
      return ctx->f16;
   else if (t == ctx->i32 || t == ctx->f32)
      return ctx->f32;
   else if (t == ctx->i64 || t == ctx->f64)
      return ctx->f64;
   else
      unreachable("Unhandled float size");
}

LLVMTypeRef ac_to_float_type(struct ac_llvm_context *ctx, LLVMTypeRef t)
{
   if (LLVMGetTypeKind(t) == LLVMVectorTypeKind) {
      LLVMTypeRef elem_type = LLVMGetElementType(t);
      return LLVMVectorType(to_float_type_scalar(ctx, elem_type), LLVMGetVectorSize(t));
   }
   return to_float_type_scalar(ctx, t);
}

LLVMValueRef ac_to_float(struct ac_llvm_context *ctx, LLVMValueRef v)
{
   LLVMTypeRef type = LLVMTypeOf(v);
   return LLVMBuildBitCast(ctx->builder, v, ac_to_float_type(ctx, type), "");
}

LLVMValueRef ac_build_intrinsic(struct ac_llvm_context *ctx, const char *name,
                                LLVMTypeRef return_type, LLVMValueRef *params, unsigned param_count,
                                unsigned attrib_mask)
{
   LLVMValueRef function, call;
   bool set_callsite_attrs = !(attrib_mask & AC_FUNC_ATTR_LEGACY);

   function = LLVMGetNamedFunction(ctx->module, name);
   if (!function) {
      LLVMTypeRef param_types[32], function_type;
      unsigned i;

      assert(param_count <= 32);

      for (i = 0; i < param_count; ++i) {
         assert(params[i]);
         param_types[i] = LLVMTypeOf(params[i]);
      }
      function_type = LLVMFunctionType(return_type, param_types, param_count, 0);
      function = LLVMAddFunction(ctx->module, name, function_type);

      LLVMSetFunctionCallConv(function, LLVMCCallConv);
      LLVMSetLinkage(function, LLVMExternalLinkage);

      if (!set_callsite_attrs)
         ac_add_func_attributes(ctx->context, function, attrib_mask);
   }

   call = LLVMBuildCall(ctx->builder, function, params, param_count, "");
   if (set_callsite_attrs)
      ac_add_func_attributes(ctx->context, call, attrib_mask);
   return call;
}

/**
 * Given the i32 or vNi32 \p type, generate the textual name (e.g. for use with
 * intrinsic names).
 */
void ac_build_type_name_for_intr(LLVMTypeRef type, char *buf, unsigned bufsize)
{
   LLVMTypeRef elem_type = type;

   if (LLVMGetTypeKind(type) == LLVMStructTypeKind) {
      unsigned count = LLVMCountStructElementTypes(type);
      int ret = snprintf(buf, bufsize, "sl_");
      buf += ret;
      bufsize -= ret;

      LLVMTypeRef *elems = alloca(count * sizeof(LLVMTypeRef));
      LLVMGetStructElementTypes(type, elems);

      for (unsigned i = 0; i < count; i++) {
         ac_build_type_name_for_intr(elems[i], buf, bufsize);
         ret = strlen(buf);
         buf += ret;
         bufsize -= ret;
      }

      snprintf(buf, bufsize, "s");
      return;
   }

   assert(bufsize >= 8);
   if (LLVMGetTypeKind(type) == LLVMVectorTypeKind) {
      int ret = snprintf(buf, bufsize, "v%u", LLVMGetVectorSize(type));
      if (ret < 0) {
         char *type_name = LLVMPrintTypeToString(type);
         fprintf(stderr, "Error building type name for: %s\n", type_name);
         LLVMDisposeMessage(type_name);
         return;
      }
      elem_type = LLVMGetElementType(type);
      buf += ret;
      bufsize -= ret;
   }
   switch (LLVMGetTypeKind(elem_type)) {
   default:
      break;
   case LLVMIntegerTypeKind:
      snprintf(buf, bufsize, "i%d", LLVMGetIntTypeWidth(elem_type));
      break;
   case LLVMHalfTypeKind:
      snprintf(buf, bufsize, "f16");
      break;
   case LLVMFloatTypeKind:
      snprintf(buf, bufsize, "f32");
      break;
   case LLVMDoubleTypeKind:
      snprintf(buf, bufsize, "f64");
      break;
   }
}

/**
 * Helper function that builds an LLVM IR PHI node and immediately adds
 * incoming edges.
 */
LLVMValueRef ac_build_phi(struct ac_llvm_context *ctx, LLVMTypeRef type, unsigned count_incoming,
                          LLVMValueRef *values, LLVMBasicBlockRef *blocks)
{
   LLVMValueRef phi = LLVMBuildPhi(ctx->builder, type, "");
   LLVMAddIncoming(phi, values, blocks, count_incoming);
   return phi;
}

void ac_build_s_barrier(struct ac_llvm_context *ctx)
{
   ac_build_intrinsic(ctx, "llvm.amdgcn.s.barrier", ctx->voidt, NULL, 0, AC_FUNC_ATTR_CONVERGENT);
}

/* Prevent optimizations (at least of memory accesses) across the current
 * point in the program by emitting empty inline assembly that is marked as
 * having side effects.
 *
 * Optionally, a value can be passed through the inline assembly to prevent
 * LLVM from hoisting calls to ReadNone functions.
 */
void ac_build_optimization_barrier(struct ac_llvm_context *ctx, LLVMValueRef *pgpr, bool sgpr)
{
   static int counter = 0;

   LLVMBuilderRef builder = ctx->builder;
   char code[16];
   const char *constraint = sgpr ? "=s,0" : "=v,0";

   snprintf(code, sizeof(code), "; %d", (int)p_atomic_inc_return(&counter));

   if (!pgpr) {
      LLVMTypeRef ftype = LLVMFunctionType(ctx->voidt, NULL, 0, false);
      LLVMValueRef inlineasm = LLVMConstInlineAsm(ftype, code, "", true, false);
      LLVMBuildCall(builder, inlineasm, NULL, 0, "");
   } else if (LLVMTypeOf(*pgpr) == ctx->i32) {
      /* Simple version for i32 that allows the caller to set LLVM metadata on the call
       * instruction. */
      LLVMTypeRef ftype = LLVMFunctionType(ctx->i32, &ctx->i32, 1, false);
      LLVMValueRef inlineasm = LLVMConstInlineAsm(ftype, code, constraint, true, false);

      *pgpr = LLVMBuildCall(builder, inlineasm, pgpr, 1, "");
   } else if (LLVMTypeOf(*pgpr) == ctx->i16) {
      /* Simple version for i16 that allows the caller to set LLVM metadata on the call
       * instruction. */
      LLVMTypeRef ftype = LLVMFunctionType(ctx->i16, &ctx->i16, 1, false);
      LLVMValueRef inlineasm = LLVMConstInlineAsm(ftype, code, constraint, true, false);

      *pgpr = LLVMBuildCall(builder, inlineasm, pgpr, 1, "");
   } else if (LLVMGetTypeKind(LLVMTypeOf(*pgpr)) == LLVMPointerTypeKind) {
      LLVMTypeRef type = LLVMTypeOf(*pgpr);
      LLVMTypeRef ftype = LLVMFunctionType(type, &type, 1, false);
      LLVMValueRef inlineasm = LLVMConstInlineAsm(ftype, code, constraint, true, false);

      *pgpr = LLVMBuildCall(builder, inlineasm, pgpr, 1, "");
   } else {
      LLVMTypeRef ftype = LLVMFunctionType(ctx->i32, &ctx->i32, 1, false);
      LLVMValueRef inlineasm = LLVMConstInlineAsm(ftype, code, constraint, true, false);
      LLVMTypeRef type = LLVMTypeOf(*pgpr);
      unsigned bitsize = ac_get_elem_bits(ctx, type);
      LLVMValueRef vgpr = *pgpr;
      LLVMTypeRef vgpr_type;
      unsigned vgpr_size;
      LLVMValueRef vgpr0;

      if (bitsize < 32)
         vgpr = LLVMBuildZExt(ctx->builder, vgpr, ctx->i32, "");

      vgpr_type = LLVMTypeOf(vgpr);
      vgpr_size = ac_get_type_size(vgpr_type);

      assert(vgpr_size % 4 == 0);

      vgpr = LLVMBuildBitCast(builder, vgpr, LLVMVectorType(ctx->i32, vgpr_size / 4), "");
      vgpr0 = LLVMBuildExtractElement(builder, vgpr, ctx->i32_0, "");
      vgpr0 = LLVMBuildCall(builder, inlineasm, &vgpr0, 1, "");
      vgpr = LLVMBuildInsertElement(builder, vgpr, vgpr0, ctx->i32_0, "");
      vgpr = LLVMBuildBitCast(builder, vgpr, vgpr_type, "");

      if (bitsize < 32)
         vgpr = LLVMBuildTrunc(builder, vgpr, type, "");

      *pgpr = vgpr;
   }
}

LLVMValueRef ac_build_shader_clock(struct ac_llvm_context *ctx, nir_scope scope)
{
   const char *subgroup = "llvm.readcyclecounter";
   const char *name = scope == NIR_SCOPE_DEVICE ? "llvm.amdgcn.s.memrealtime" : subgroup;

   LLVMValueRef tmp = ac_build_intrinsic(ctx, name, ctx->i64, NULL, 0, 0);
   return LLVMBuildBitCast(ctx->builder, tmp, ctx->v2i32, "");
}

LLVMValueRef ac_build_ballot(struct ac_llvm_context *ctx, LLVMValueRef value)
{
   const char *name;

   if (LLVMTypeOf(value) == ctx->i1)
      value = LLVMBuildZExt(ctx->builder, value, ctx->i32, "");

   if (ctx->wave_size == 64)
      name = "llvm.amdgcn.icmp.i64.i32";
   else
      name = "llvm.amdgcn.icmp.i32.i32";

   LLVMValueRef args[3] = {value, ctx->i32_0, LLVMConstInt(ctx->i32, LLVMIntNE, 0)};

   /* We currently have no other way to prevent LLVM from lifting the icmp
    * calls to a dominating basic block.
    */
   ac_build_optimization_barrier(ctx, &args[0], false);

   args[0] = ac_to_integer(ctx, args[0]);

   return ac_build_intrinsic(
      ctx, name, ctx->iN_wavemask, args, 3,
      AC_FUNC_ATTR_NOUNWIND | AC_FUNC_ATTR_READNONE | AC_FUNC_ATTR_CONVERGENT);
}

LLVMValueRef ac_get_i1_sgpr_mask(struct ac_llvm_context *ctx, LLVMValueRef value)
{
   const char *name;

   if (ctx->wave_size == 64)
      name = "llvm.amdgcn.icmp.i64.i1";
   else
      name = "llvm.amdgcn.icmp.i32.i1";

   LLVMValueRef args[3] = {
      value,
      ctx->i1false,
      LLVMConstInt(ctx->i32, LLVMIntNE, 0),
   };

   return ac_build_intrinsic(
      ctx, name, ctx->iN_wavemask, args, 3,
      AC_FUNC_ATTR_NOUNWIND | AC_FUNC_ATTR_READNONE | AC_FUNC_ATTR_CONVERGENT);
}

LLVMValueRef ac_build_vote_all(struct ac_llvm_context *ctx, LLVMValueRef value)
{
   LLVMValueRef active_set = ac_build_ballot(ctx, ctx->i32_1);
   LLVMValueRef vote_set = ac_build_ballot(ctx, value);
   return LLVMBuildICmp(ctx->builder, LLVMIntEQ, vote_set, active_set, "");
}

LLVMValueRef ac_build_vote_any(struct ac_llvm_context *ctx, LLVMValueRef value)
{
   LLVMValueRef vote_set = ac_build_ballot(ctx, value);
   return LLVMBuildICmp(ctx->builder, LLVMIntNE, vote_set, LLVMConstInt(ctx->iN_wavemask, 0, 0),
                        "");
}

LLVMValueRef ac_build_vote_eq(struct ac_llvm_context *ctx, LLVMValueRef value)
{
   LLVMValueRef active_set = ac_build_ballot(ctx, ctx->i32_1);
   LLVMValueRef vote_set = ac_build_ballot(ctx, value);

   LLVMValueRef all = LLVMBuildICmp(ctx->builder, LLVMIntEQ, vote_set, active_set, "");
   LLVMValueRef none =
      LLVMBuildICmp(ctx->builder, LLVMIntEQ, vote_set, LLVMConstInt(ctx->iN_wavemask, 0, 0), "");
   return LLVMBuildOr(ctx->builder, all, none, "");
}

LLVMValueRef ac_build_varying_gather_values(struct ac_llvm_context *ctx, LLVMValueRef *values,
                                            unsigned value_count, unsigned component)
{
   LLVMValueRef vec = NULL;

   if (value_count == 1) {
      return values[component];
   } else if (!value_count)
      unreachable("value_count is 0");

   for (unsigned i = component; i < value_count + component; i++) {
      LLVMValueRef value = values[i];

      if (i == component)
         vec = LLVMGetUndef(LLVMVectorType(LLVMTypeOf(value), value_count));
      LLVMValueRef index = LLVMConstInt(ctx->i32, i - component, false);
      vec = LLVMBuildInsertElement(ctx->builder, vec, value, index, "");
   }
   return vec;
}

LLVMValueRef ac_build_gather_values_extended(struct ac_llvm_context *ctx, LLVMValueRef *values,
                                             unsigned value_count, unsigned value_stride, bool load,
                                             bool always_vector)
{
   LLVMBuilderRef builder = ctx->builder;
   LLVMValueRef vec = NULL;
   unsigned i;

   if (value_count == 1 && !always_vector) {
      if (load)
         return LLVMBuildLoad(builder, values[0], "");
      return values[0];
   } else if (!value_count)
      unreachable("value_count is 0");

   for (i = 0; i < value_count; i++) {
      LLVMValueRef value = values[i * value_stride];
      if (load)
         value = LLVMBuildLoad(builder, value, "");

      if (!i)
         vec = LLVMGetUndef(LLVMVectorType(LLVMTypeOf(value), value_count));
      LLVMValueRef index = LLVMConstInt(ctx->i32, i, false);
      vec = LLVMBuildInsertElement(builder, vec, value, index, "");
   }
   return vec;
}

LLVMValueRef ac_build_gather_values(struct ac_llvm_context *ctx, LLVMValueRef *values,
                                    unsigned value_count)
{
   return ac_build_gather_values_extended(ctx, values, value_count, 1, false, false);
}

LLVMValueRef ac_build_concat(struct ac_llvm_context *ctx, LLVMValueRef a, LLVMValueRef b)
{
   unsigned a_size = ac_get_llvm_num_components(a);
   unsigned b_size = ac_get_llvm_num_components(b);

   LLVMValueRef *elems = alloca((a_size + b_size) * sizeof(LLVMValueRef));
   for (unsigned i = 0; i < a_size; i++)
      elems[i] = ac_llvm_extract_elem(ctx, a, i);
   for (unsigned i = 0; i < b_size; i++)
      elems[a_size + i] = ac_llvm_extract_elem(ctx, b, i);

   return ac_build_gather_values(ctx, elems, a_size + b_size);
}

/* Expand a scalar or vector to <dst_channels x type> by filling the remaining
 * channels with undef. Extract at most src_channels components from the input.
 */
LLVMValueRef ac_build_expand(struct ac_llvm_context *ctx, LLVMValueRef value,
                             unsigned src_channels, unsigned dst_channels)
{
   LLVMTypeRef elemtype;
   LLVMValueRef *const chan = alloca(dst_channels * sizeof(LLVMValueRef));

   if (LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMVectorTypeKind) {
      unsigned vec_size = LLVMGetVectorSize(LLVMTypeOf(value));

      if (src_channels == dst_channels && vec_size == dst_channels)
         return value;

      src_channels = MIN2(src_channels, vec_size);

      for (unsigned i = 0; i < src_channels; i++)
         chan[i] = ac_llvm_extract_elem(ctx, value, i);

      elemtype = LLVMGetElementType(LLVMTypeOf(value));
   } else {
      if (src_channels) {
         assert(src_channels == 1);
         chan[0] = value;
      }
      elemtype = LLVMTypeOf(value);
   }

   for (unsigned i = src_channels; i < dst_channels; i++)
      chan[i] = LLVMGetUndef(elemtype);

   return ac_build_gather_values(ctx, chan, dst_channels);
}

/* Extract components [start, start + channels) from a vector.
 */
LLVMValueRef ac_extract_components(struct ac_llvm_context *ctx, LLVMValueRef value, unsigned start,
                                   unsigned channels)
{
   LLVMValueRef *const chan = alloca(channels * sizeof(LLVMValueRef));

   for (unsigned i = 0; i < channels; i++)
      chan[i] = ac_llvm_extract_elem(ctx, value, i + start);

   return ac_build_gather_values(ctx, chan, channels);
}

/* Expand a scalar or vector to <4 x type> by filling the remaining channels
 * with undef. Extract at most num_channels components from the input.
 */
LLVMValueRef ac_build_expand_to_vec4(struct ac_llvm_context *ctx, LLVMValueRef value,
                                     unsigned num_channels)
{
   return ac_build_expand(ctx, value, num_channels, 4);
}

LLVMValueRef ac_build_round(struct ac_llvm_context *ctx, LLVMValueRef value)
{
   unsigned type_size = ac_get_type_size(LLVMTypeOf(value));
   const char *name;

   if (type_size == 2)
      name = "llvm.rint.f16";
   else if (type_size == 4)
      name = "llvm.rint.f32";
   else
      name = "llvm.rint.f64";

   return ac_build_intrinsic(ctx, name, LLVMTypeOf(value), &value, 1, AC_FUNC_ATTR_READNONE);
}

LLVMValueRef ac_build_fdiv(struct ac_llvm_context *ctx, LLVMValueRef num, LLVMValueRef den)
{
   unsigned type_size = ac_get_type_size(LLVMTypeOf(den));
   const char *name;

   /* For doubles, we need precise division to pass GLCTS. */
   if (ctx->float_mode == AC_FLOAT_MODE_DEFAULT_OPENGL && type_size == 8)
      return LLVMBuildFDiv(ctx->builder, num, den, "");

   if (type_size == 2)
      name = "llvm.amdgcn.rcp.f16";
   else if (type_size == 4)
      name = "llvm.amdgcn.rcp.f32";
   else
      name = "llvm.amdgcn.rcp.f64";

   LLVMValueRef rcp =
      ac_build_intrinsic(ctx, name, LLVMTypeOf(den), &den, 1, AC_FUNC_ATTR_READNONE);

   return LLVMBuildFMul(ctx->builder, num, rcp, "");
}

/* See fast_idiv_by_const.h. */
/* Set: increment = util_fast_udiv_info::increment ? multiplier : 0; */
LLVMValueRef ac_build_fast_udiv(struct ac_llvm_context *ctx, LLVMValueRef num,
                                LLVMValueRef multiplier, LLVMValueRef pre_shift,
                                LLVMValueRef post_shift, LLVMValueRef increment)
{
   LLVMBuilderRef builder = ctx->builder;

   num = LLVMBuildLShr(builder, num, pre_shift, "");
   num = LLVMBuildMul(builder, LLVMBuildZExt(builder, num, ctx->i64, ""),
                      LLVMBuildZExt(builder, multiplier, ctx->i64, ""), "");
   num = LLVMBuildAdd(builder, num, LLVMBuildZExt(builder, increment, ctx->i64, ""), "");
   num = LLVMBuildLShr(builder, num, LLVMConstInt(ctx->i64, 32, 0), "");
   num = LLVMBuildTrunc(builder, num, ctx->i32, "");
   return LLVMBuildLShr(builder, num, post_shift, "");
}

/* See fast_idiv_by_const.h. */
/* If num != UINT_MAX, this more efficient version can be used. */
/* Set: increment = util_fast_udiv_info::increment; */
LLVMValueRef ac_build_fast_udiv_nuw(struct ac_llvm_context *ctx, LLVMValueRef num,
                                    LLVMValueRef multiplier, LLVMValueRef pre_shift,
                                    LLVMValueRef post_shift, LLVMValueRef increment)
{
   LLVMBuilderRef builder = ctx->builder;

   num = LLVMBuildLShr(builder, num, pre_shift, "");
   num = LLVMBuildNUWAdd(builder, num, increment, "");
   num = LLVMBuildMul(builder, LLVMBuildZExt(builder, num, ctx->i64, ""),
                      LLVMBuildZExt(builder, multiplier, ctx->i64, ""), "");
   num = LLVMBuildLShr(builder, num, LLVMConstInt(ctx->i64, 32, 0), "");
   num = LLVMBuildTrunc(builder, num, ctx->i32, "");
   return LLVMBuildLShr(builder, num, post_shift, "");
}

/* See fast_idiv_by_const.h. */
/* Both operands must fit in 31 bits and the divisor must not be 1. */
LLVMValueRef ac_build_fast_udiv_u31_d_not_one(struct ac_llvm_context *ctx, LLVMValueRef num,
                                              LLVMValueRef multiplier, LLVMValueRef post_shift)
{
   LLVMBuilderRef builder = ctx->builder;

   num = LLVMBuildMul(builder, LLVMBuildZExt(builder, num, ctx->i64, ""),
                      LLVMBuildZExt(builder, multiplier, ctx->i64, ""), "");
   num = LLVMBuildLShr(builder, num, LLVMConstInt(ctx->i64, 32, 0), "");
   num = LLVMBuildTrunc(builder, num, ctx->i32, "");
   return LLVMBuildLShr(builder, num, post_shift, "");
}

/* Coordinates for cube map selection. sc, tc, and ma are as in Table 8.27
 * of the OpenGL 4.5 (Compatibility Profile) specification, except ma is
 * already multiplied by two. id is the cube face number.
 */
struct cube_selection_coords {
   LLVMValueRef stc[2];
   LLVMValueRef ma;
   LLVMValueRef id;
};

static void build_cube_intrinsic(struct ac_llvm_context *ctx, LLVMValueRef in[3],
                                 struct cube_selection_coords *out)
{
   LLVMTypeRef f32 = ctx->f32;

   out->stc[1] = ac_build_intrinsic(ctx, "llvm.amdgcn.cubetc", f32, in, 3, AC_FUNC_ATTR_READNONE);
   out->stc[0] = ac_build_intrinsic(ctx, "llvm.amdgcn.cubesc", f32, in, 3, AC_FUNC_ATTR_READNONE);
   out->ma = ac_build_intrinsic(ctx, "llvm.amdgcn.cubema", f32, in, 3, AC_FUNC_ATTR_READNONE);
   out->id = ac_build_intrinsic(ctx, "llvm.amdgcn.cubeid", f32, in, 3, AC_FUNC_ATTR_READNONE);
}

/**
 * Build a manual selection sequence for cube face sc/tc coordinates and
 * major axis vector (multiplied by 2 for consistency) for the given
 * vec3 \p coords, for the face implied by \p selcoords.
 *
 * For the major axis, we always adjust the sign to be in the direction of
 * selcoords.ma; i.e., a positive out_ma means that coords is pointed towards
 * the selcoords major axis.
 */
static void build_cube_select(struct ac_llvm_context *ctx,
                              const struct cube_selection_coords *selcoords,
                              const LLVMValueRef *coords, LLVMValueRef *out_st,
                              LLVMValueRef *out_ma)
{
   LLVMBuilderRef builder = ctx->builder;
   LLVMTypeRef f32 = LLVMTypeOf(coords[0]);
   LLVMValueRef is_ma_positive;
   LLVMValueRef sgn_ma;
   LLVMValueRef is_ma_z, is_not_ma_z;
   LLVMValueRef is_ma_y;
   LLVMValueRef is_ma_x;
   LLVMValueRef sgn;
   LLVMValueRef tmp;

   is_ma_positive = LLVMBuildFCmp(builder, LLVMRealUGE, selcoords->ma, LLVMConstReal(f32, 0.0), "");
   sgn_ma = LLVMBuildSelect(builder, is_ma_positive, LLVMConstReal(f32, 1.0),
                            LLVMConstReal(f32, -1.0), "");

   is_ma_z = LLVMBuildFCmp(builder, LLVMRealUGE, selcoords->id, LLVMConstReal(f32, 4.0), "");
   is_not_ma_z = LLVMBuildNot(builder, is_ma_z, "");
   is_ma_y = LLVMBuildAnd(
      builder, is_not_ma_z,
      LLVMBuildFCmp(builder, LLVMRealUGE, selcoords->id, LLVMConstReal(f32, 2.0), ""), "");
   is_ma_x = LLVMBuildAnd(builder, is_not_ma_z, LLVMBuildNot(builder, is_ma_y, ""), "");

   /* Select sc */
   tmp = LLVMBuildSelect(builder, is_ma_x, coords[2], coords[0], "");
   sgn = LLVMBuildSelect(
      builder, is_ma_y, LLVMConstReal(f32, 1.0),
      LLVMBuildSelect(builder, is_ma_z, sgn_ma, LLVMBuildFNeg(builder, sgn_ma, ""), ""), "");
   out_st[0] = LLVMBuildFMul(builder, tmp, sgn, "");

   /* Select tc */
   tmp = LLVMBuildSelect(builder, is_ma_y, coords[2], coords[1], "");
   sgn = LLVMBuildSelect(builder, is_ma_y, sgn_ma, LLVMConstReal(f32, -1.0), "");
   out_st[1] = LLVMBuildFMul(builder, tmp, sgn, "");

   /* Select ma */
   tmp = LLVMBuildSelect(builder, is_ma_z, coords[2],
                         LLVMBuildSelect(builder, is_ma_y, coords[1], coords[0], ""), "");
   tmp = ac_build_intrinsic(ctx, "llvm.fabs.f32", ctx->f32, &tmp, 1, AC_FUNC_ATTR_READNONE);
   *out_ma = LLVMBuildFMul(builder, tmp, LLVMConstReal(f32, 2.0), "");
}

void ac_prepare_cube_coords(struct ac_llvm_context *ctx, bool is_deriv, bool is_array, bool is_lod,
                            LLVMValueRef *coords_arg, LLVMValueRef *derivs_arg)
{

   LLVMBuilderRef builder = ctx->builder;
   struct cube_selection_coords selcoords;
   LLVMValueRef coords[3];
   LLVMValueRef invma;

   if (is_array && !is_lod) {
      LLVMValueRef tmp = ac_build_round(ctx, coords_arg[3]);

      /* Section 8.9 (Texture Functions) of the GLSL 4.50 spec says:
       *
       *    "For Array forms, the array layer used will be
       *
       *       max(0, min(d−1, floor(layer+0.5)))
       *
       *     where d is the depth of the texture array and layer
       *     comes from the component indicated in the tables below.
       *     Workaroudn for an issue where the layer is taken from a
       *     helper invocation which happens to fall on a different
       *     layer due to extrapolation."
       *
       * GFX8 and earlier attempt to implement this in hardware by
       * clamping the value of coords[2] = (8 * layer) + face.
       * Unfortunately, this means that the we end up with the wrong
       * face when clamping occurs.
       *
       * Clamp the layer earlier to work around the issue.
       */
      if (ctx->chip_class <= GFX8) {
         LLVMValueRef ge0;
         ge0 = LLVMBuildFCmp(builder, LLVMRealOGE, tmp, ctx->f32_0, "");
         tmp = LLVMBuildSelect(builder, ge0, tmp, ctx->f32_0, "");
      }

      coords_arg[3] = tmp;
   }

   build_cube_intrinsic(ctx, coords_arg, &selcoords);

   invma =
      ac_build_intrinsic(ctx, "llvm.fabs.f32", ctx->f32, &selcoords.ma, 1, AC_FUNC_ATTR_READNONE);
   invma = ac_build_fdiv(ctx, LLVMConstReal(ctx->f32, 1.0), invma);

   for (int i = 0; i < 2; ++i)
      coords[i] = LLVMBuildFMul(builder, selcoords.stc[i], invma, "");

   coords[2] = selcoords.id;

   if (is_deriv && derivs_arg) {
      LLVMValueRef derivs[4];
      int axis;

      /* Convert cube derivatives to 2D derivatives. */
      for (axis = 0; axis < 2; axis++) {
         LLVMValueRef deriv_st[2];
         LLVMValueRef deriv_ma;

         /* Transform the derivative alongside the texture
          * coordinate. Mathematically, the correct formula is
          * as follows. Assume we're projecting onto the +Z face
          * and denote by dx/dh the derivative of the (original)
          * X texture coordinate with respect to horizontal
          * window coordinates. The projection onto the +Z face
          * plane is:
          *
          *   f(x,z) = x/z
          *
          * Then df/dh = df/dx * dx/dh + df/dz * dz/dh
          *            = 1/z * dx/dh - x/z * 1/z * dz/dh.
          *
          * This motivatives the implementation below.
          *
          * Whether this actually gives the expected results for
          * apps that might feed in derivatives obtained via
          * finite differences is anyone's guess. The OpenGL spec
          * seems awfully quiet about how textureGrad for cube
          * maps should be handled.
          */
         build_cube_select(ctx, &selcoords, &derivs_arg[axis * 3], deriv_st, &deriv_ma);

         deriv_ma = LLVMBuildFMul(builder, deriv_ma, invma, "");

         for (int i = 0; i < 2; ++i)
            derivs[axis * 2 + i] =
               LLVMBuildFSub(builder, LLVMBuildFMul(builder, deriv_st[i], invma, ""),
                             LLVMBuildFMul(builder, deriv_ma, coords[i], ""), "");
      }

      memcpy(derivs_arg, derivs, sizeof(derivs));
   }

   /* Shift the texture coordinate. This must be applied after the
    * derivative calculation.
    */
   for (int i = 0; i < 2; ++i)
      coords[i] = LLVMBuildFAdd(builder, coords[i], LLVMConstReal(ctx->f32, 1.5), "");

   if (is_array) {
      /* for cube arrays coord.z = coord.w(array_index) * 8 + face */
      /* coords_arg.w component - array_index for cube arrays */
      coords[2] = ac_build_fmad(ctx, coords_arg[3], LLVMConstReal(ctx->f32, 8.0), coords[2]);
   }

   memcpy(coords_arg, coords, sizeof(coords));
}

LLVMValueRef ac_build_fs_interp(struct ac_llvm_context *ctx, LLVMValueRef llvm_chan,
                                LLVMValueRef attr_number, LLVMValueRef params, LLVMValueRef i,
                                LLVMValueRef j)
{
   LLVMValueRef args[5];
   LLVMValueRef p1;

   args[0] = i;
   args[1] = llvm_chan;
   args[2] = attr_number;
   args[3] = params;

   p1 = ac_build_intrinsic(ctx, "llvm.amdgcn.interp.p1", ctx->f32, args, 4, AC_FUNC_ATTR_READNONE);

   args[0] = p1;
   args[1] = j;
   args[2] = llvm_chan;
   args[3] = attr_number;
   args[4] = params;

   return ac_build_intrinsic(ctx, "llvm.amdgcn.interp.p2", ctx->f32, args, 5,
                             AC_FUNC_ATTR_READNONE);
}

LLVMValueRef ac_build_fs_interp_f16(struct ac_llvm_context *ctx, LLVMValueRef llvm_chan,
                                    LLVMValueRef attr_number, LLVMValueRef params, LLVMValueRef i,
                                    LLVMValueRef j, bool high_16bits)
{
   LLVMValueRef args[6];
   LLVMValueRef p1;

   args[0] = i;
   args[1] = llvm_chan;
   args[2] = attr_number;
   args[3] = high_16bits ? ctx->i1true : ctx->i1false;
   args[4] = params;

   p1 = ac_build_intrinsic(ctx, "llvm.amdgcn.interp.p1.f16", ctx->f32, args, 5,
                           AC_FUNC_ATTR_READNONE);

   args[0] = p1;
   args[1] = j;
   args[2] = llvm_chan;
   args[3] = attr_number;
   args[4] = high_16bits ? ctx->i1true : ctx->i1false;
   args[5] = params;

   return ac_build_intrinsic(ctx, "llvm.amdgcn.interp.p2.f16", ctx->f16, args, 6,
                             AC_FUNC_ATTR_READNONE);
}

LLVMValueRef ac_build_fs_interp_mov(struct ac_llvm_context *ctx, LLVMValueRef parameter,
                                    LLVMValueRef llvm_chan, LLVMValueRef attr_number,
                                    LLVMValueRef params)
{
   LLVMValueRef args[4];

   args[0] = parameter;
   args[1] = llvm_chan;
   args[2] = attr_number;
   args[3] = params;

   return ac_build_intrinsic(ctx, "llvm.amdgcn.interp.mov", ctx->f32, args, 4,
                             AC_FUNC_ATTR_READNONE);
}

LLVMValueRef ac_build_gep_ptr(struct ac_llvm_context *ctx, LLVMValueRef base_ptr,
                              LLVMValueRef index)
{
   return LLVMBuildGEP(ctx->builder, base_ptr, &index, 1, "");
}

LLVMValueRef ac_build_gep0(struct ac_llvm_context *ctx, LLVMValueRef base_ptr, LLVMValueRef index)
{
   LLVMValueRef indices[2] = {
      ctx->i32_0,
      index,
   };
   return LLVMBuildGEP(ctx->builder, base_ptr, indices, 2, "");
}

LLVMValueRef ac_build_pointer_add(struct ac_llvm_context *ctx, LLVMValueRef ptr, LLVMValueRef index)
{
   return LLVMBuildPointerCast(ctx->builder, LLVMBuildGEP(ctx->builder, ptr, &index, 1, ""),
                               LLVMTypeOf(ptr), "");
}

void ac_build_indexed_store(struct ac_llvm_context *ctx, LLVMValueRef base_ptr, LLVMValueRef index,
                            LLVMValueRef value)
{
   LLVMBuildStore(ctx->builder, value, ac_build_gep0(ctx, base_ptr, index));
}

/**
 * Build an LLVM bytecode indexed load using LLVMBuildGEP + LLVMBuildLoad.
 * It's equivalent to doing a load from &base_ptr[index].
 *
 * \param base_ptr  Where the array starts.
 * \param index     The element index into the array.
 * \param uniform   Whether the base_ptr and index can be assumed to be
 *                  dynamically uniform (i.e. load to an SGPR)
 * \param invariant Whether the load is invariant (no other opcodes affect it)
 * \param no_unsigned_wraparound
 *    For all possible re-associations and re-distributions of an expression
 *    "base_ptr + index * elemsize" into "addr + offset" (excluding GEPs
 *    without inbounds in base_ptr), this parameter is true if "addr + offset"
 *    does not result in an unsigned integer wraparound. This is used for
 *    optimal code generation of 32-bit pointer arithmetic.
 *
 *    For example, a 32-bit immediate offset that causes a 32-bit unsigned
 *    integer wraparound can't be an imm offset in s_load_dword, because
 *    the instruction performs "addr + offset" in 64 bits.
 *
 *    Expected usage for bindless textures by chaining GEPs:
 *      // possible unsigned wraparound, don't use InBounds:
 *      ptr1 = LLVMBuildGEP(base_ptr, index);
 *      image = load(ptr1); // becomes "s_load ptr1, 0"
 *
 *      ptr2 = LLVMBuildInBoundsGEP(ptr1, 32 / elemsize);
 *      sampler = load(ptr2); // becomes "s_load ptr1, 32" thanks to InBounds
 */
static LLVMValueRef ac_build_load_custom(struct ac_llvm_context *ctx, LLVMValueRef base_ptr,
                                         LLVMValueRef index, bool uniform, bool invariant,
                                         bool no_unsigned_wraparound)
{
   LLVMValueRef pointer, result;

   if (no_unsigned_wraparound &&
       LLVMGetPointerAddressSpace(LLVMTypeOf(base_ptr)) == AC_ADDR_SPACE_CONST_32BIT)
      pointer = LLVMBuildInBoundsGEP(ctx->builder, base_ptr, &index, 1, "");
   else
      pointer = LLVMBuildGEP(ctx->builder, base_ptr, &index, 1, "");

   if (uniform)
      LLVMSetMetadata(pointer, ctx->uniform_md_kind, ctx->empty_md);
   result = LLVMBuildLoad(ctx->builder, pointer, "");
   if (invariant)
      LLVMSetMetadata(result, ctx->invariant_load_md_kind, ctx->empty_md);
   LLVMSetAlignment(result, 4);
   return result;
}

LLVMValueRef ac_build_load(struct ac_llvm_context *ctx, LLVMValueRef base_ptr, LLVMValueRef index)
{
   return ac_build_load_custom(ctx, base_ptr, index, false, false, false);
}

LLVMValueRef ac_build_load_invariant(struct ac_llvm_context *ctx, LLVMValueRef base_ptr,
                                     LLVMValueRef index)
{
   return ac_build_load_custom(ctx, base_ptr, index, false, true, false);
}

/* This assumes that there is no unsigned integer wraparound during the address
 * computation, excluding all GEPs within base_ptr. */
LLVMValueRef ac_build_load_to_sgpr(struct ac_llvm_context *ctx, LLVMValueRef base_ptr,
                                   LLVMValueRef index)
{
   return ac_build_load_custom(ctx, base_ptr, index, true, true, true);
}

/* See ac_build_load_custom() documentation. */
LLVMValueRef ac_build_load_to_sgpr_uint_wraparound(struct ac_llvm_context *ctx,
                                                   LLVMValueRef base_ptr, LLVMValueRef index)
{
   return ac_build_load_custom(ctx, base_ptr, index, true, true, false);
}

static unsigned get_load_cache_policy(struct ac_llvm_context *ctx, unsigned cache_policy)
{
   return cache_policy | (ctx->chip_class >= GFX10 && cache_policy & ac_glc ? ac_dlc : 0);
}

static void ac_build_buffer_store_common(struct ac_llvm_context *ctx, LLVMValueRef rsrc,
                                         LLVMValueRef data, LLVMValueRef vindex,
                                         LLVMValueRef voffset, LLVMValueRef soffset,
                                         unsigned cache_policy, bool use_format, bool structurized)
{
   LLVMValueRef args[6];
   int idx = 0;
   args[idx++] = data;
   args[idx++] = LLVMBuildBitCast(ctx->builder, rsrc, ctx->v4i32, "");
   if (structurized)
      args[idx++] = vindex ? vindex : ctx->i32_0;
   args[idx++] = voffset ? voffset : ctx->i32_0;
   args[idx++] = soffset ? soffset : ctx->i32_0;
   args[idx++] = LLVMConstInt(ctx->i32, cache_policy, 0);
   const char *indexing_kind = structurized ? "struct" : "raw";
   char name[256], type_name[8];

   ac_build_type_name_for_intr(LLVMTypeOf(data), type_name, sizeof(type_name));

   if (use_format) {
      snprintf(name, sizeof(name), "llvm.amdgcn.%s.buffer.store.format.%s", indexing_kind,
               type_name);
   } else {
      snprintf(name, sizeof(name), "llvm.amdgcn.%s.buffer.store.%s", indexing_kind, type_name);
   }

   ac_build_intrinsic(ctx, name, ctx->voidt, args, idx, AC_FUNC_ATTR_INACCESSIBLE_MEM_ONLY);
}

void ac_build_buffer_store_format(struct ac_llvm_context *ctx, LLVMValueRef rsrc, LLVMValueRef data,
                                  LLVMValueRef vindex, LLVMValueRef voffset, unsigned cache_policy)
{
   ac_build_buffer_store_common(ctx, rsrc, data, vindex, voffset, NULL, cache_policy, true, true);
}

/* TBUFFER_STORE_FORMAT_{X,XY,XYZ,XYZW} <- the suffix is selected by num_channels=1..4.
 * The type of vdata must be one of i32 (num_channels=1), v2i32 (num_channels=2),
 * or v4i32 (num_channels=3,4).
 */
void ac_build_buffer_store_dword(struct ac_llvm_context *ctx, LLVMValueRef rsrc, LLVMValueRef vdata,
                                 unsigned num_channels, LLVMValueRef voffset, LLVMValueRef soffset,
                                 unsigned inst_offset, unsigned cache_policy)
{
   /* Split 3 channel stores. */
   if (num_channels == 3 && !ac_has_vec3_support(ctx->chip_class, false)) {
      LLVMValueRef v[3], v01;

      for (int i = 0; i < 3; i++) {
         v[i] = LLVMBuildExtractElement(ctx->builder, vdata, LLVMConstInt(ctx->i32, i, 0), "");
      }
      v01 = ac_build_gather_values(ctx, v, 2);

      ac_build_buffer_store_dword(ctx, rsrc, v01, 2, voffset, soffset, inst_offset, cache_policy);
      ac_build_buffer_store_dword(ctx, rsrc, v[2], 1, voffset, soffset, inst_offset + 8,
                                  cache_policy);
      return;
   }

   /* SWIZZLE_ENABLE requires that soffset isn't folded into voffset
    * (voffset is swizzled, but soffset isn't swizzled).
    * llvm.amdgcn.buffer.store doesn't have a separate soffset parameter.
    */
   if (!(cache_policy & ac_swizzled)) {
      LLVMValueRef offset = soffset;

      if (inst_offset)
         offset = LLVMBuildAdd(ctx->builder, offset, LLVMConstInt(ctx->i32, inst_offset, 0), "");

      ac_build_buffer_store_common(ctx, rsrc, ac_to_float(ctx, vdata), ctx->i32_0, voffset, offset,
                                   cache_policy, false, false);
      return;
   }

   static const unsigned dfmts[] = {V_008F0C_BUF_DATA_FORMAT_32, V_008F0C_BUF_DATA_FORMAT_32_32,
                                    V_008F0C_BUF_DATA_FORMAT_32_32_32,
                                    V_008F0C_BUF_DATA_FORMAT_32_32_32_32};
   unsigned dfmt = dfmts[num_channels - 1];
   unsigned nfmt = V_008F0C_BUF_NUM_FORMAT_UINT;
   LLVMValueRef immoffset = LLVMConstInt(ctx->i32, inst_offset, 0);

   ac_build_raw_tbuffer_store(ctx, rsrc, vdata, voffset, soffset, immoffset, num_channels, dfmt,
                              nfmt, cache_policy);
}

static LLVMValueRef ac_build_buffer_load_common(struct ac_llvm_context *ctx, LLVMValueRef rsrc,
                                                LLVMValueRef vindex, LLVMValueRef voffset,
                                                LLVMValueRef soffset, unsigned num_channels,
                                                LLVMTypeRef channel_type, unsigned cache_policy,
                                                bool can_speculate, bool use_format,
                                                bool structurized)
{
   LLVMValueRef args[5];
   int idx = 0;
   args[idx++] = LLVMBuildBitCast(ctx->builder, rsrc, ctx->v4i32, "");
   if (structurized)
      args[idx++] = vindex ? vindex : ctx->i32_0;
   args[idx++] = voffset ? voffset : ctx->i32_0;
   args[idx++] = soffset ? soffset : ctx->i32_0;
   args[idx++] = LLVMConstInt(ctx->i32, get_load_cache_policy(ctx, cache_policy), 0);
   unsigned func =
      !ac_has_vec3_support(ctx->chip_class, use_format) && num_channels == 3 ? 4 : num_channels;
   const char *indexing_kind = structurized ? "struct" : "raw";
   char name[256], type_name[8];

   /* D16 is only supported on gfx8+ */
   assert(!use_format || (channel_type != ctx->f16 && channel_type != ctx->i16) ||
          ctx->chip_class >= GFX8);

   LLVMTypeRef type = func > 1 ? LLVMVectorType(channel_type, func) : channel_type;
   ac_build_type_name_for_intr(type, type_name, sizeof(type_name));

   if (use_format) {
      snprintf(name, sizeof(name), "llvm.amdgcn.%s.buffer.load.format.%s", indexing_kind,
               type_name);
   } else {
      snprintf(name, sizeof(name), "llvm.amdgcn.%s.buffer.load.%s", indexing_kind, type_name);
   }

   return ac_build_intrinsic(ctx, name, type, args, idx, ac_get_load_intr_attribs(can_speculate));
}

LLVMValueRef ac_build_buffer_load(struct ac_llvm_context *ctx, LLVMValueRef rsrc, int num_channels,
                                  LLVMValueRef vindex, LLVMValueRef voffset, LLVMValueRef soffset,
                                  unsigned inst_offset, LLVMTypeRef channel_type,
                                  unsigned cache_policy, bool can_speculate, bool allow_smem)
{
   LLVMValueRef offset = LLVMConstInt(ctx->i32, inst_offset, 0);
   if (voffset)
      offset = LLVMBuildAdd(ctx->builder, offset, voffset, "");
   if (soffset)
      offset = LLVMBuildAdd(ctx->builder, offset, soffset, "");

   if (allow_smem && !(cache_policy & ac_slc) &&
       (!(cache_policy & ac_glc) || ctx->chip_class >= GFX8)) {
      assert(vindex == NULL);

      LLVMValueRef result[8];

      for (int i = 0; i < num_channels; i++) {
         if (i) {
            offset = LLVMBuildAdd(ctx->builder, offset, LLVMConstInt(ctx->i32, 4, 0), "");
         }
         LLVMValueRef args[3] = {
            rsrc,
            offset,
            LLVMConstInt(ctx->i32, get_load_cache_policy(ctx, cache_policy), 0),
         };
         result[i] = ac_build_intrinsic(ctx, "llvm.amdgcn.s.buffer.load.f32", ctx->f32, args, 3,
                                        AC_FUNC_ATTR_READNONE);
      }
      if (num_channels == 1)
         return result[0];

      if (num_channels == 3 && !ac_has_vec3_support(ctx->chip_class, false))
         result[num_channels++] = LLVMGetUndef(ctx->f32);
      return ac_build_gather_values(ctx, result, num_channels);
   }

   return ac_build_buffer_load_common(ctx, rsrc, vindex, offset, ctx->i32_0, num_channels,
                                      channel_type, cache_policy, can_speculate, false, false);
}

LLVMValueRef ac_build_buffer_load_format(struct ac_llvm_context *ctx, LLVMValueRef rsrc,
                                         LLVMValueRef vindex, LLVMValueRef voffset,
                                         unsigned num_channels, unsigned cache_policy,
                                         bool can_speculate, bool d16, bool tfe)
{
   if (tfe) {
      assert(!d16);

      char code[256];
      /* The definition in the assembly and the one in the constraint string
       * differs because of an assembler bug.
       */
      snprintf(code, sizeof(code),
               "v_mov_b32 v0, 0\n"
               "v_mov_b32 v1, 0\n"
               "v_mov_b32 v2, 0\n"
               "v_mov_b32 v3, 0\n"
               "v_mov_b32 v4, 0\n"
               "buffer_load_format_xyzw v[0:3], $1, $2, 0, idxen offen %s %s tfe %s\n"
               "s_waitcnt vmcnt(0)",
               cache_policy & ac_glc ? "glc" : "",
               cache_policy & ac_slc ? "slc" : "",
               cache_policy & ac_dlc ? "dlc" : "");

      LLVMTypeRef param_types[] = {ctx->v2i32, ctx->v4i32};
      LLVMTypeRef calltype = LLVMFunctionType(LLVMVectorType(ctx->f32, 5), param_types, 2, false);
      LLVMValueRef inlineasm = LLVMConstInlineAsm(calltype, code, "=&{v[0:4]},v,s", false, false);

      LLVMValueRef addr_comp[2] = {vindex ? vindex : ctx->i32_0,
                                   voffset ? voffset : ctx->i32_0};

      LLVMValueRef args[] = {ac_build_gather_values(ctx, addr_comp, 2),
                             LLVMBuildBitCast(ctx->builder, rsrc, ctx->v4i32, "")};
      LLVMValueRef res = LLVMBuildCall(ctx->builder, inlineasm, args, 2, "");

      return ac_build_concat(ctx, ac_trim_vector(ctx, res, num_channels),
                             ac_llvm_extract_elem(ctx, res, 4));
   }

   return ac_build_buffer_load_common(ctx, rsrc, vindex, voffset, ctx->i32_0, num_channels,
                                      d16 ? ctx->f16 : ctx->f32, cache_policy, can_speculate, true,
                                      true);
}

static LLVMValueRef ac_build_tbuffer_load(struct ac_llvm_context *ctx, LLVMValueRef rsrc,
                                          LLVMValueRef vindex, LLVMValueRef voffset,
                                          LLVMValueRef soffset, LLVMValueRef immoffset,
                                          unsigned num_channels, unsigned dfmt, unsigned nfmt,
                                          unsigned cache_policy, bool can_speculate,
                                          bool structurized)
{
   voffset = LLVMBuildAdd(ctx->builder, voffset, immoffset, "");

   LLVMValueRef args[6];
   int idx = 0;
   args[idx++] = LLVMBuildBitCast(ctx->builder, rsrc, ctx->v4i32, "");
   if (structurized)
      args[idx++] = vindex ? vindex : ctx->i32_0;
   args[idx++] = voffset ? voffset : ctx->i32_0;
   args[idx++] = soffset ? soffset : ctx->i32_0;
   args[idx++] = LLVMConstInt(ctx->i32, ac_get_tbuffer_format(ctx->chip_class, dfmt, nfmt), 0);
   args[idx++] = LLVMConstInt(ctx->i32, get_load_cache_policy(ctx, cache_policy), 0);
   unsigned func =
      !ac_has_vec3_support(ctx->chip_class, true) && num_channels == 3 ? 4 : num_channels;
   const char *indexing_kind = structurized ? "struct" : "raw";
   char name[256], type_name[8];

   LLVMTypeRef type = func > 1 ? LLVMVectorType(ctx->i32, func) : ctx->i32;
   ac_build_type_name_for_intr(type, type_name, sizeof(type_name));

   snprintf(name, sizeof(name), "llvm.amdgcn.%s.tbuffer.load.%s", indexing_kind, type_name);

   return ac_build_intrinsic(ctx, name, type, args, idx, ac_get_load_intr_attribs(can_speculate));
}

LLVMValueRef ac_build_struct_tbuffer_load(struct ac_llvm_context *ctx, LLVMValueRef rsrc,
                                          LLVMValueRef vindex, LLVMValueRef voffset,
                                          LLVMValueRef soffset, LLVMValueRef immoffset,
                                          unsigned num_channels, unsigned dfmt, unsigned nfmt,
                                          unsigned cache_policy, bool can_speculate)
{
   return ac_build_tbuffer_load(ctx, rsrc, vindex, voffset, soffset, immoffset, num_channels, dfmt,
                                nfmt, cache_policy, can_speculate, true);
}

LLVMValueRef ac_build_tbuffer_load_short(struct ac_llvm_context *ctx, LLVMValueRef rsrc,
                                         LLVMValueRef voffset, LLVMValueRef soffset,
                                         LLVMValueRef immoffset, unsigned cache_policy)
{
   voffset = LLVMBuildAdd(ctx->builder, voffset, immoffset, "");

   return ac_build_buffer_load_common(ctx, rsrc, NULL, voffset, soffset, 1, ctx->i16,
                                      cache_policy, false, false, false);
}

LLVMValueRef ac_build_tbuffer_load_byte(struct ac_llvm_context *ctx, LLVMValueRef rsrc,
                                        LLVMValueRef voffset, LLVMValueRef soffset,
                                        LLVMValueRef immoffset, unsigned cache_policy)
{
   voffset = LLVMBuildAdd(ctx->builder, voffset, immoffset, "");

   return ac_build_buffer_load_common(ctx, rsrc, NULL, voffset, soffset, 1, ctx->i8, cache_policy,
                                      false, false, false);
}

/**
 * Convert an 11- or 10-bit unsigned floating point number to an f32.
 *
 * The input exponent is expected to be biased analogous to IEEE-754, i.e. by
 * 2^(exp_bits-1) - 1 (as defined in OpenGL and other graphics APIs).
 */
static LLVMValueRef ac_ufN_to_float(struct ac_llvm_context *ctx, LLVMValueRef src,
                                    unsigned exp_bits, unsigned mant_bits)
{
   assert(LLVMTypeOf(src) == ctx->i32);

   LLVMValueRef tmp;
   LLVMValueRef mantissa;
   mantissa =
      LLVMBuildAnd(ctx->builder, src, LLVMConstInt(ctx->i32, (1 << mant_bits) - 1, false), "");

   /* Converting normal numbers is just a shift + correcting the exponent bias */
   unsigned normal_shift = 23 - mant_bits;
   unsigned bias_shift = 127 - ((1 << (exp_bits - 1)) - 1);
   LLVMValueRef shifted, normal;

   shifted = LLVMBuildShl(ctx->builder, src, LLVMConstInt(ctx->i32, normal_shift, false), "");
   normal =
      LLVMBuildAdd(ctx->builder, shifted, LLVMConstInt(ctx->i32, bias_shift << 23, false), "");

   /* Converting nan/inf numbers is the same, but with a different exponent update */
   LLVMValueRef naninf;
   naninf = LLVMBuildOr(ctx->builder, normal, LLVMConstInt(ctx->i32, 0xff << 23, false), "");

   /* Converting denormals is the complex case: determine the leading zeros of the
    * mantissa to obtain the correct shift for the mantissa and exponent correction.
    */
   LLVMValueRef denormal;
   LLVMValueRef params[2] = {
      mantissa, ctx->i1true, /* result can be undef when arg is 0 */
   };
   LLVMValueRef ctlz =
      ac_build_intrinsic(ctx, "llvm.ctlz.i32", ctx->i32, params, 2, AC_FUNC_ATTR_READNONE);

   /* Shift such that the leading 1 ends up as the LSB of the exponent field. */
   tmp = LLVMBuildSub(ctx->builder, ctlz, LLVMConstInt(ctx->i32, 8, false), "");
   denormal = LLVMBuildShl(ctx->builder, mantissa, tmp, "");

   unsigned denormal_exp = bias_shift + (32 - mant_bits) - 1;
   tmp = LLVMBuildSub(ctx->builder, LLVMConstInt(ctx->i32, denormal_exp, false), ctlz, "");
   tmp = LLVMBuildShl(ctx->builder, tmp, LLVMConstInt(ctx->i32, 23, false), "");
   denormal = LLVMBuildAdd(ctx->builder, denormal, tmp, "");

   /* Select the final result. */
   LLVMValueRef result;

   tmp = LLVMBuildICmp(ctx->builder, LLVMIntUGE, src,
                       LLVMConstInt(ctx->i32, ((1ULL << exp_bits) - 1) << mant_bits, false), "");
   result = LLVMBuildSelect(ctx->builder, tmp, naninf, normal, "");

   tmp = LLVMBuildICmp(ctx->builder, LLVMIntUGE, src,
                       LLVMConstInt(ctx->i32, 1ULL << mant_bits, false), "");
   result = LLVMBuildSelect(ctx->builder, tmp, result, denormal, "");

   tmp = LLVMBuildICmp(ctx->builder, LLVMIntNE, src, ctx->i32_0, "");
   result = LLVMBuildSelect(ctx->builder, tmp, result, ctx->i32_0, "");

   return ac_to_float(ctx, result);
}

/**
 * Generate a fully general open coded buffer format fetch with all required
 * fixups suitable for vertex fetch, using non-format buffer loads.
 *
 * Some combinations of argument values have special interpretations:
 * - size = 8 bytes, format = fixed indicates PIPE_FORMAT_R11G11B10_FLOAT
 * - size = 8 bytes, format != {float,fixed} indicates a 2_10_10_10 data format
 *
 * \param log_size log(size of channel in bytes)
 * \param num_channels number of channels (1 to 4)
 * \param format AC_FETCH_FORMAT_xxx value
 * \param reverse whether XYZ channels are reversed
 * \param known_aligned whether the source is known to be aligned to hardware's
 *                      effective element size for loading the given format
 *                      (note: this means dword alignment for 8_8_8_8, 16_16, etc.)
 * \param rsrc buffer resource descriptor
 * \return the resulting vector of floats or integers bitcast to <4 x i32>
 */
LLVMValueRef ac_build_opencoded_load_format(struct ac_llvm_context *ctx, unsigned log_size,
                                            unsigned num_channels, unsigned format, bool reverse,
                                            bool known_aligned, LLVMValueRef rsrc,
                                            LLVMValueRef vindex, LLVMValueRef voffset,
                                            LLVMValueRef soffset, unsigned cache_policy,
                                            bool can_speculate)
{
   LLVMValueRef tmp;
   unsigned load_log_size = log_size;
   unsigned load_num_channels = num_channels;
   if (log_size == 3) {
      load_log_size = 2;
      if (format == AC_FETCH_FORMAT_FLOAT) {
         load_num_channels = 2 * num_channels;
      } else {
         load_num_channels = 1; /* 10_11_11 or 2_10_10_10 */
      }
   }

   int log_recombine = 0;
   if ((ctx->chip_class == GFX6 || ctx->chip_class >= GFX10) && !known_aligned) {
      /* Avoid alignment restrictions by loading one byte at a time. */
      load_num_channels <<= load_log_size;
      log_recombine = load_log_size;
      load_log_size = 0;
   } else if (load_num_channels == 2 || load_num_channels == 4) {
      log_recombine = -util_logbase2(load_num_channels);
      load_num_channels = 1;
      load_log_size += -log_recombine;
   }

   LLVMValueRef loads[32]; /* up to 32 bytes */
   for (unsigned i = 0; i < load_num_channels; ++i) {
      tmp =
         LLVMBuildAdd(ctx->builder, soffset, LLVMConstInt(ctx->i32, i << load_log_size, false), "");
      LLVMTypeRef channel_type =
         load_log_size == 0 ? ctx->i8 : load_log_size == 1 ? ctx->i16 : ctx->i32;
      unsigned num_channels = 1 << (MAX2(load_log_size, 2) - 2);
      loads[i] =
         ac_build_buffer_load_common(ctx, rsrc, vindex, voffset, tmp, num_channels, channel_type,
                                     cache_policy, can_speculate, false, true);
      if (load_log_size >= 2)
         loads[i] = ac_to_integer(ctx, loads[i]);
   }

   if (log_recombine > 0) {
      /* Recombine bytes if necessary (GFX6 only) */
      LLVMTypeRef dst_type = log_recombine == 2 ? ctx->i32 : ctx->i16;

      for (unsigned src = 0, dst = 0; src < load_num_channels; ++dst) {
         LLVMValueRef accum = NULL;
         for (unsigned i = 0; i < (1 << log_recombine); ++i, ++src) {
            tmp = LLVMBuildZExt(ctx->builder, loads[src], dst_type, "");
            if (i == 0) {
               accum = tmp;
            } else {
               tmp = LLVMBuildShl(ctx->builder, tmp, LLVMConstInt(dst_type, 8 * i, false), "");
               accum = LLVMBuildOr(ctx->builder, accum, tmp, "");
            }
         }
         loads[dst] = accum;
      }
   } else if (log_recombine < 0) {
      /* Split vectors of dwords */
      if (load_log_size > 2) {
         assert(load_num_channels == 1);
         LLVMValueRef loaded = loads[0];
         unsigned log_split = load_log_size - 2;
         log_recombine += log_split;
         load_num_channels = 1 << log_split;
         load_log_size = 2;
         for (unsigned i = 0; i < load_num_channels; ++i) {
            tmp = LLVMConstInt(ctx->i32, i, false);
            loads[i] = LLVMBuildExtractElement(ctx->builder, loaded, tmp, "");
         }
      }

      /* Further split dwords and shorts if required */
      if (log_recombine < 0) {
         for (unsigned src = load_num_channels, dst = load_num_channels << -log_recombine; src > 0;
              --src) {
            unsigned dst_bits = 1 << (3 + load_log_size + log_recombine);
            LLVMTypeRef dst_type = LLVMIntTypeInContext(ctx->context, dst_bits);
            LLVMValueRef loaded = loads[src - 1];
            LLVMTypeRef loaded_type = LLVMTypeOf(loaded);
            for (unsigned i = 1 << -log_recombine; i > 0; --i, --dst) {
               tmp = LLVMConstInt(loaded_type, dst_bits * (i - 1), false);
               tmp = LLVMBuildLShr(ctx->builder, loaded, tmp, "");
               loads[dst - 1] = LLVMBuildTrunc(ctx->builder, tmp, dst_type, "");
            }
         }
      }
   }

   if (log_size == 3) {
      if (format == AC_FETCH_FORMAT_FLOAT) {
         for (unsigned i = 0; i < num_channels; ++i) {
            tmp = ac_build_gather_values(ctx, &loads[2 * i], 2);
            loads[i] = LLVMBuildBitCast(ctx->builder, tmp, ctx->f64, "");
         }
      } else if (format == AC_FETCH_FORMAT_FIXED) {
         /* 10_11_11_FLOAT */
         LLVMValueRef data = loads[0];
         LLVMValueRef i32_2047 = LLVMConstInt(ctx->i32, 2047, false);
         LLVMValueRef r = LLVMBuildAnd(ctx->builder, data, i32_2047, "");
         tmp = LLVMBuildLShr(ctx->builder, data, LLVMConstInt(ctx->i32, 11, false), "");
         LLVMValueRef g = LLVMBuildAnd(ctx->builder, tmp, i32_2047, "");
         LLVMValueRef b = LLVMBuildLShr(ctx->builder, data, LLVMConstInt(ctx->i32, 22, false), "");

         loads[0] = ac_to_integer(ctx, ac_ufN_to_float(ctx, r, 5, 6));
         loads[1] = ac_to_integer(ctx, ac_ufN_to_float(ctx, g, 5, 6));
         loads[2] = ac_to_integer(ctx, ac_ufN_to_float(ctx, b, 5, 5));

         num_channels = 3;
         log_size = 2;
         format = AC_FETCH_FORMAT_FLOAT;
      } else {
         /* 2_10_10_10 data formats */
         LLVMValueRef data = loads[0];
         LLVMTypeRef i10 = LLVMIntTypeInContext(ctx->context, 10);
         LLVMTypeRef i2 = LLVMIntTypeInContext(ctx->context, 2);
         loads[0] = LLVMBuildTrunc(ctx->builder, data, i10, "");
         tmp = LLVMBuildLShr(ctx->builder, data, LLVMConstInt(ctx->i32, 10, false), "");
         loads[1] = LLVMBuildTrunc(ctx->builder, tmp, i10, "");
         tmp = LLVMBuildLShr(ctx->builder, data, LLVMConstInt(ctx->i32, 20, false), "");
         loads[2] = LLVMBuildTrunc(ctx->builder, tmp, i10, "");
         tmp = LLVMBuildLShr(ctx->builder, data, LLVMConstInt(ctx->i32, 30, false), "");
         loads[3] = LLVMBuildTrunc(ctx->builder, tmp, i2, "");

         num_channels = 4;
      }
   }

   if (format == AC_FETCH_FORMAT_FLOAT) {
      if (log_size != 2) {
         for (unsigned chan = 0; chan < num_channels; ++chan) {
            tmp = ac_to_float(ctx, loads[chan]);
            if (log_size == 3)
               tmp = LLVMBuildFPTrunc(ctx->builder, tmp, ctx->f32, "");
            else if (log_size == 1)
               tmp = LLVMBuildFPExt(ctx->builder, tmp, ctx->f32, "");
            loads[chan] = ac_to_integer(ctx, tmp);
         }
      }
   } else if (format == AC_FETCH_FORMAT_UINT) {
      if (log_size != 2) {
         for (unsigned chan = 0; chan < num_channels; ++chan)
            loads[chan] = LLVMBuildZExt(ctx->builder, loads[chan], ctx->i32, "");
      }
   } else if (format == AC_FETCH_FORMAT_SINT) {
      if (log_size != 2) {
         for (unsigned chan = 0; chan < num_channels; ++chan)
            loads[chan] = LLVMBuildSExt(ctx->builder, loads[chan], ctx->i32, "");
      }
   } else {
      bool unsign = format == AC_FETCH_FORMAT_UNORM || format == AC_FETCH_FORMAT_USCALED ||
                    format == AC_FETCH_FORMAT_UINT;

      for (unsigned chan = 0; chan < num_channels; ++chan) {
         if (unsign) {
            tmp = LLVMBuildUIToFP(ctx->builder, loads[chan], ctx->f32, "");
         } else {
            tmp = LLVMBuildSIToFP(ctx->builder, loads[chan], ctx->f32, "");
         }

         LLVMValueRef scale = NULL;
         if (format == AC_FETCH_FORMAT_FIXED) {
            assert(log_size == 2);
            scale = LLVMConstReal(ctx->f32, 1.0 / 0x10000);
         } else if (format == AC_FETCH_FORMAT_UNORM) {
            unsigned bits = LLVMGetIntTypeWidth(LLVMTypeOf(loads[chan]));
            scale = LLVMConstReal(ctx->f32, 1.0 / (((uint64_t)1 << bits) - 1));
         } else if (format == AC_FETCH_FORMAT_SNORM) {
            unsigned bits = LLVMGetIntTypeWidth(LLVMTypeOf(loads[chan]));
            scale = LLVMConstReal(ctx->f32, 1.0 / (((uint64_t)1 << (bits - 1)) - 1));
         }
         if (scale)
            tmp = LLVMBuildFMul(ctx->builder, tmp, scale, "");

         if (format == AC_FETCH_FORMAT_SNORM) {
            /* Clamp to [-1, 1] */
            LLVMValueRef neg_one = LLVMConstReal(ctx->f32, -1.0);
            LLVMValueRef clamp = LLVMBuildFCmp(ctx->builder, LLVMRealULT, tmp, neg_one, "");
            tmp = LLVMBuildSelect(ctx->builder, clamp, neg_one, tmp, "");
         }

         loads[chan] = ac_to_integer(ctx, tmp);
      }
   }

   while (num_channels < 4) {
      if (format == AC_FETCH_FORMAT_UINT || format == AC_FETCH_FORMAT_SINT) {
         loads[num_channels] = num_channels == 3 ? ctx->i32_1 : ctx->i32_0;
      } else {
         loads[num_channels] = ac_to_integer(ctx, num_channels == 3 ? ctx->f32_1 : ctx->f32_0);
      }
      num_channels++;
   }

   if (reverse) {
      tmp = loads[0];
      loads[0] = loads[2];
      loads[2] = tmp;
   }

   return ac_build_gather_values(ctx, loads, 4);
}

static void ac_build_tbuffer_store(struct ac_llvm_context *ctx, LLVMValueRef rsrc,
                                   LLVMValueRef vdata, LLVMValueRef vindex, LLVMValueRef voffset,
                                   LLVMValueRef soffset, LLVMValueRef immoffset,
                                   unsigned num_channels, unsigned dfmt, unsigned nfmt,
                                   unsigned cache_policy, bool structurized)
{
   voffset = LLVMBuildAdd(ctx->builder, voffset ? voffset : ctx->i32_0, immoffset, "");

   LLVMValueRef args[7];
   int idx = 0;
   args[idx++] = vdata;
   args[idx++] = LLVMBuildBitCast(ctx->builder, rsrc, ctx->v4i32, "");
   if (structurized)
      args[idx++] = vindex ? vindex : ctx->i32_0;
   args[idx++] = voffset ? voffset : ctx->i32_0;
   args[idx++] = soffset ? soffset : ctx->i32_0;
   args[idx++] = LLVMConstInt(ctx->i32, ac_get_tbuffer_format(ctx->chip_class, dfmt, nfmt), 0);
   args[idx++] = LLVMConstInt(ctx->i32, cache_policy, 0);
   unsigned func =
      !ac_has_vec3_support(ctx->chip_class, true) && num_channels == 3 ? 4 : num_channels;
   const char *indexing_kind = structurized ? "struct" : "raw";
   char name[256], type_name[8];

   LLVMTypeRef type = func > 1 ? LLVMVectorType(ctx->i32, func) : ctx->i32;
   ac_build_type_name_for_intr(type, type_name, sizeof(type_name));

   snprintf(name, sizeof(name), "llvm.amdgcn.%s.tbuffer.store.%s", indexing_kind, type_name);

   ac_build_intrinsic(ctx, name, ctx->voidt, args, idx, AC_FUNC_ATTR_INACCESSIBLE_MEM_ONLY);
}

void ac_build_struct_tbuffer_store(struct ac_llvm_context *ctx, LLVMValueRef rsrc,
                                   LLVMValueRef vdata, LLVMValueRef vindex, LLVMValueRef voffset,
                                   LLVMValueRef soffset, LLVMValueRef immoffset,
                                   unsigned num_channels, unsigned dfmt, unsigned nfmt,
                                   unsigned cache_policy)
{
   ac_build_tbuffer_store(ctx, rsrc, vdata, vindex, voffset, soffset, immoffset, num_channels, dfmt,
                          nfmt, cache_policy, true);
}

void ac_build_raw_tbuffer_store(struct ac_llvm_context *ctx, LLVMValueRef rsrc, LLVMValueRef vdata,
                                LLVMValueRef voffset, LLVMValueRef soffset, LLVMValueRef immoffset,
                                unsigned num_channels, unsigned dfmt, unsigned nfmt,
                                unsigned cache_policy)
{
   ac_build_tbuffer_store(ctx, rsrc, vdata, NULL, voffset, soffset, immoffset, num_channels, dfmt,
                          nfmt, cache_policy, false);
}

void ac_build_tbuffer_store_short(struct ac_llvm_context *ctx, LLVMValueRef rsrc,
                                  LLVMValueRef vdata, LLVMValueRef voffset, LLVMValueRef soffset,
                                  unsigned cache_policy)
{
   vdata = LLVMBuildBitCast(ctx->builder, vdata, ctx->i16, "");

   ac_build_buffer_store_common(ctx, rsrc, vdata, NULL, voffset, soffset, cache_policy, false,
                                false);
}

void ac_build_tbuffer_store_byte(struct ac_llvm_context *ctx, LLVMValueRef rsrc, LLVMValueRef vdata,
                                 LLVMValueRef voffset, LLVMValueRef soffset, unsigned cache_policy)
{
   vdata = LLVMBuildBitCast(ctx->builder, vdata, ctx->i8, "");

   ac_build_buffer_store_common(ctx, rsrc, vdata, NULL, voffset, soffset, cache_policy, false,
                                false);
}

/**
 * Set range metadata on an instruction.  This can only be used on load and
 * call instructions.  If you know an instruction can only produce the values
 * 0, 1, 2, you would do set_range_metadata(value, 0, 3);
 * \p lo is the minimum value inclusive.
 * \p hi is the maximum value exclusive.
 */
void ac_set_range_metadata(struct ac_llvm_context *ctx, LLVMValueRef value, unsigned lo,
                           unsigned hi)
{
   LLVMValueRef range_md, md_args[2];
   LLVMTypeRef type = LLVMTypeOf(value);
   LLVMContextRef context = LLVMGetTypeContext(type);

   md_args[0] = LLVMConstInt(type, lo, false);
   md_args[1] = LLVMConstInt(type, hi, false);
   range_md = LLVMMDNodeInContext(context, md_args, 2);
   LLVMSetMetadata(value, ctx->range_md_kind, range_md);
}

LLVMValueRef ac_get_thread_id(struct ac_llvm_context *ctx)
{
   return ac_build_mbcnt(ctx, LLVMConstInt(ctx->iN_wavemask, ~0ull, 0));
}

/*
 * AMD GCN implements derivatives using the local data store (LDS)
 * All writes to the LDS happen in all executing threads at
 * the same time. TID is the Thread ID for the current
 * thread and is a value between 0 and 63, representing
 * the thread's position in the wavefront.
 *
 * For the pixel shader threads are grouped into quads of four pixels.
 * The TIDs of the pixels of a quad are:
 *
 *  +------+------+
 *  |4n + 0|4n + 1|
 *  +------+------+
 *  |4n + 2|4n + 3|
 *  +------+------+
 *
 * So, masking the TID with 0xfffffffc yields the TID of the top left pixel
 * of the quad, masking with 0xfffffffd yields the TID of the top pixel of
 * the current pixel's column, and masking with 0xfffffffe yields the TID
 * of the left pixel of the current pixel's row.
 *
 * Adding 1 yields the TID of the pixel to the right of the left pixel, and
 * adding 2 yields the TID of the pixel below the top pixel.
 */
LLVMValueRef ac_build_ddxy(struct ac_llvm_context *ctx, uint32_t mask, int idx, LLVMValueRef val)
{
   unsigned tl_lanes[4], trbl_lanes[4];
   char name[32], type[8];
   LLVMValueRef tl, trbl;
   LLVMTypeRef result_type;
   LLVMValueRef result;

   result_type = ac_to_float_type(ctx, LLVMTypeOf(val));

   if (result_type == ctx->f16)
      val = LLVMBuildZExt(ctx->builder, val, ctx->i32, "");
   else if (result_type == ctx->v2f16)
      val = LLVMBuildBitCast(ctx->builder, val, ctx->i32, "");

   for (unsigned i = 0; i < 4; ++i) {
      tl_lanes[i] = i & mask;
      trbl_lanes[i] = (i & mask) + idx;
   }

   tl = ac_build_quad_swizzle(ctx, val, tl_lanes[0], tl_lanes[1], tl_lanes[2], tl_lanes[3]);
   trbl =
      ac_build_quad_swizzle(ctx, val, trbl_lanes[0], trbl_lanes[1], trbl_lanes[2], trbl_lanes[3]);

   if (result_type == ctx->f16) {
      tl = LLVMBuildTrunc(ctx->builder, tl, ctx->i16, "");
      trbl = LLVMBuildTrunc(ctx->builder, trbl, ctx->i16, "");
   }

   tl = LLVMBuildBitCast(ctx->builder, tl, result_type, "");
   trbl = LLVMBuildBitCast(ctx->builder, trbl, result_type, "");
   result = LLVMBuildFSub(ctx->builder, trbl, tl, "");

   ac_build_type_name_for_intr(result_type, type, sizeof(type));
   snprintf(name, sizeof(name), "llvm.amdgcn.wqm.%s", type);

   return ac_build_intrinsic(ctx, name, result_type, &result, 1, 0);
}

void ac_build_sendmsg(struct ac_llvm_context *ctx, uint32_t msg, LLVMValueRef wave_id)
{
   LLVMValueRef args[2];
   args[0] = LLVMConstInt(ctx->i32, msg, false);
   args[1] = wave_id;
   ac_build_intrinsic(ctx, "llvm.amdgcn.s.sendmsg", ctx->voidt, args, 2, 0);
}

LLVMValueRef ac_build_imsb(struct ac_llvm_context *ctx, LLVMValueRef arg, LLVMTypeRef dst_type)
{
   LLVMValueRef msb =
      ac_build_intrinsic(ctx, "llvm.amdgcn.sffbh.i32", dst_type, &arg, 1, AC_FUNC_ATTR_READNONE);

   /* The HW returns the last bit index from MSB, but NIR/TGSI wants
    * the index from LSB. Invert it by doing "31 - msb". */
   msb = LLVMBuildSub(ctx->builder, LLVMConstInt(ctx->i32, 31, false), msb, "");

   LLVMValueRef all_ones = LLVMConstInt(ctx->i32, -1, true);
   LLVMValueRef cond =
      LLVMBuildOr(ctx->builder, LLVMBuildICmp(ctx->builder, LLVMIntEQ, arg, ctx->i32_0, ""),
                  LLVMBuildICmp(ctx->builder, LLVMIntEQ, arg, all_ones, ""), "");

   return LLVMBuildSelect(ctx->builder, cond, all_ones, msb, "");
}

LLVMValueRef ac_build_umsb(struct ac_llvm_context *ctx, LLVMValueRef arg, LLVMTypeRef dst_type)
{
   const char *intrin_name;
   LLVMTypeRef type;
   LLVMValueRef highest_bit;
   LLVMValueRef zero;
   unsigned bitsize;

   bitsize = ac_get_elem_bits(ctx, LLVMTypeOf(arg));
   switch (bitsize) {
   case 64:
      intrin_name = "llvm.ctlz.i64";
      type = ctx->i64;
      highest_bit = LLVMConstInt(ctx->i64, 63, false);
      zero = ctx->i64_0;
      break;
   case 32:
      intrin_name = "llvm.ctlz.i32";
      type = ctx->i32;
      highest_bit = LLVMConstInt(ctx->i32, 31, false);
      zero = ctx->i32_0;
      break;
   case 16:
      intrin_name = "llvm.ctlz.i16";
      type = ctx->i16;
      highest_bit = LLVMConstInt(ctx->i16, 15, false);
      zero = ctx->i16_0;
      break;
   case 8:
      intrin_name = "llvm.ctlz.i8";
      type = ctx->i8;
      highest_bit = LLVMConstInt(ctx->i8, 7, false);
      zero = ctx->i8_0;
      break;
   default:
      unreachable(!"invalid bitsize");
      break;
   }

   LLVMValueRef params[2] = {
      arg,
      ctx->i1true,
   };

   LLVMValueRef msb = ac_build_intrinsic(ctx, intrin_name, type, params, 2, AC_FUNC_ATTR_READNONE);

   /* The HW returns the last bit index from MSB, but TGSI/NIR wants
    * the index from LSB. Invert it by doing "31 - msb". */
   msb = LLVMBuildSub(ctx->builder, highest_bit, msb, "");

   if (bitsize == 64) {
      msb = LLVMBuildTrunc(ctx->builder, msb, ctx->i32, "");
   } else if (bitsize < 32) {
      msb = LLVMBuildSExt(ctx->builder, msb, ctx->i32, "");
   }

   /* check for zero */
   return LLVMBuildSelect(ctx->builder, LLVMBuildICmp(ctx->builder, LLVMIntEQ, arg, zero, ""),
                          LLVMConstInt(ctx->i32, -1, true), msb, "");
}

LLVMValueRef ac_build_fmin(struct ac_llvm_context *ctx, LLVMValueRef a, LLVMValueRef b)
{
   char name[64], type[64];

   ac_build_type_name_for_intr(LLVMTypeOf(a), type, sizeof(type));
   snprintf(name, sizeof(name), "llvm.minnum.%s", type);
   LLVMValueRef args[2] = {a, b};
   return ac_build_intrinsic(ctx, name, LLVMTypeOf(a), args, 2, AC_FUNC_ATTR_READNONE);
}

LLVMValueRef ac_build_fmax(struct ac_llvm_context *ctx, LLVMValueRef a, LLVMValueRef b)
{
   char name[64], type[64];

   ac_build_type_name_for_intr(LLVMTypeOf(a), type, sizeof(type));
   snprintf(name, sizeof(name), "llvm.maxnum.%s", type);
   LLVMValueRef args[2] = {a, b};
   return ac_build_intrinsic(ctx, name, LLVMTypeOf(a), args, 2, AC_FUNC_ATTR_READNONE);
}

LLVMValueRef ac_build_imin(struct ac_llvm_context *ctx, LLVMValueRef a, LLVMValueRef b)
{
   LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSLE, a, b, "");
   return LLVMBuildSelect(ctx->builder, cmp, a, b, "");
}

LLVMValueRef ac_build_imax(struct ac_llvm_context *ctx, LLVMValueRef a, LLVMValueRef b)
{
   LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSGT, a, b, "");
   return LLVMBuildSelect(ctx->builder, cmp, a, b, "");
}

LLVMValueRef ac_build_umin(struct ac_llvm_context *ctx, LLVMValueRef a, LLVMValueRef b)
{
   LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntULE, a, b, "");
   return LLVMBuildSelect(ctx->builder, cmp, a, b, "");
}

LLVMValueRef ac_build_umax(struct ac_llvm_context *ctx, LLVMValueRef a, LLVMValueRef b)
{
   LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntUGE, a, b, "");
   return LLVMBuildSelect(ctx->builder, cmp, a, b, "");
}

LLVMValueRef ac_build_clamp(struct ac_llvm_context *ctx, LLVMValueRef value)
{
   LLVMTypeRef t = LLVMTypeOf(value);
   return ac_build_fmin(ctx, ac_build_fmax(ctx, value, LLVMConstReal(t, 0.0)),
                        LLVMConstReal(t, 1.0));
}

void ac_build_export(struct ac_llvm_context *ctx, struct ac_export_args *a)
{
   LLVMValueRef args[9];

   args[0] = LLVMConstInt(ctx->i32, a->target, 0);
   args[1] = LLVMConstInt(ctx->i32, a->enabled_channels, 0);

   if (a->compr) {
      args[2] = LLVMBuildBitCast(ctx->builder, a->out[0], ctx->v2i16, "");
      args[3] = LLVMBuildBitCast(ctx->builder, a->out[1], ctx->v2i16, "");
      args[4] = LLVMConstInt(ctx->i1, a->done, 0);
      args[5] = LLVMConstInt(ctx->i1, a->valid_mask, 0);

      ac_build_intrinsic(ctx, "llvm.amdgcn.exp.compr.v2i16", ctx->voidt, args, 6, 0);
   } else {
      args[2] = a->out[0];
      args[3] = a->out[1];
      args[4] = a->out[2];
      args[5] = a->out[3];
      args[6] = LLVMConstInt(ctx->i1, a->done, 0);
      args[7] = LLVMConstInt(ctx->i1, a->valid_mask, 0);

      ac_build_intrinsic(ctx, "llvm.amdgcn.exp.f32", ctx->voidt, args, 8, 0);
   }
}

void ac_build_export_null(struct ac_llvm_context *ctx)
{
   struct ac_export_args args;

   args.enabled_channels = 0x0; /* enabled channels */
   args.valid_mask = 1;         /* whether the EXEC mask is valid */
   args.done = 1;               /* DONE bit */
   args.target = V_008DFC_SQ_EXP_NULL;
   args.compr = 0;                       /* COMPR flag (0 = 32-bit export) */
   args.out[0] = LLVMGetUndef(ctx->f32); /* R */
   args.out[1] = LLVMGetUndef(ctx->f32); /* G */
   args.out[2] = LLVMGetUndef(ctx->f32); /* B */
   args.out[3] = LLVMGetUndef(ctx->f32); /* A */

   ac_build_export(ctx, &args);
}

static unsigned ac_num_coords(enum ac_image_dim dim)
{
   switch (dim) {
   case ac_image_1d:
      return 1;
   case ac_image_2d:
   case ac_image_1darray:
      return 2;
   case ac_image_3d:
   case ac_image_cube:
   case ac_image_2darray:
   case ac_image_2dmsaa:
      return 3;
   case ac_image_2darraymsaa:
      return 4;
   default:
      unreachable("ac_num_coords: bad dim");
   }
}

static unsigned ac_num_derivs(enum ac_image_dim dim)
{
   switch (dim) {
   case ac_image_1d:
   case ac_image_1darray:
      return 2;
   case ac_image_2d:
   case ac_image_2darray:
   case ac_image_cube:
      return 4;
   case ac_image_3d:
      return 6;
   case ac_image_2dmsaa:
   case ac_image_2darraymsaa:
   default:
      unreachable("derivatives not supported");
   }
}

static const char *get_atomic_name(enum ac_atomic_op op)
{
   switch (op) {
   case ac_atomic_swap:
      return "swap";
   case ac_atomic_add:
      return "add";
   case ac_atomic_sub:
      return "sub";
   case ac_atomic_smin:
      return "smin";
   case ac_atomic_umin:
      return "umin";
   case ac_atomic_smax:
      return "smax";
   case ac_atomic_umax:
      return "umax";
   case ac_atomic_and:
      return "and";
   case ac_atomic_or:
      return "or";
   case ac_atomic_xor:
      return "xor";
   case ac_atomic_inc_wrap:
      return "inc";
   case ac_atomic_dec_wrap:
      return "dec";
   case ac_atomic_fmin:
      return "fmin";
   case ac_atomic_fmax:
      return "fmax";
   }
   unreachable("bad atomic op");
}

LLVMValueRef ac_build_image_opcode(struct ac_llvm_context *ctx, struct ac_image_args *a)
{
   const char *overload[3] = {"", "", ""};
   unsigned num_overloads = 0;
   LLVMValueRef args[18];
   unsigned num_args = 0;
   enum ac_image_dim dim = a->dim;

   assert(!a->lod || a->lod == ctx->i32_0 || a->lod == ctx->f32_0 || !a->level_zero);
   assert((a->opcode != ac_image_get_resinfo && a->opcode != ac_image_load_mip &&
           a->opcode != ac_image_store_mip) ||
          a->lod);
   assert(a->opcode == ac_image_sample || a->opcode == ac_image_gather4 ||
          (!a->compare && !a->offset));
   assert((a->opcode == ac_image_sample || a->opcode == ac_image_gather4 ||
           a->opcode == ac_image_get_lod) ||
          !a->bias);
   assert((a->bias ? 1 : 0) + (a->lod ? 1 : 0) + (a->level_zero ? 1 : 0) + (a->derivs[0] ? 1 : 0) <=
          1);
   assert((a->min_lod ? 1 : 0) + (a->lod ? 1 : 0) + (a->level_zero ? 1 : 0) <= 1);
   assert(!a->d16 || (ctx->chip_class >= GFX8 && a->opcode != ac_image_atomic &&
                      a->opcode != ac_image_atomic_cmpswap && a->opcode != ac_image_get_lod &&
                      a->opcode != ac_image_get_resinfo));
   assert(!a->a16 || ctx->chip_class >= GFX9);
   assert(a->g16 == a->a16 || ctx->chip_class >= GFX10);

   assert(!a->offset ||
          ac_get_elem_bits(ctx, LLVMTypeOf(a->offset)) == 32);
   assert(!a->bias ||
          ac_get_elem_bits(ctx, LLVMTypeOf(a->bias)) == 32);
   assert(!a->compare ||
          ac_get_elem_bits(ctx, LLVMTypeOf(a->compare)) == 32);
   assert(!a->derivs[0] ||
          ((!a->g16 || ac_get_elem_bits(ctx, LLVMTypeOf(a->derivs[0])) == 16) &&
           (a->g16 || ac_get_elem_bits(ctx, LLVMTypeOf(a->derivs[0])) == 32)));
   assert(!a->coords[0] ||
          ((!a->a16 || ac_get_elem_bits(ctx, LLVMTypeOf(a->coords[0])) == 16) &&
           (a->a16 || ac_get_elem_bits(ctx, LLVMTypeOf(a->coords[0])) == 32)));
   assert(!a->lod ||
          ((a->opcode != ac_image_get_resinfo || ac_get_elem_bits(ctx, LLVMTypeOf(a->lod))) &&
           (a->opcode == ac_image_get_resinfo ||
            ac_get_elem_bits(ctx, LLVMTypeOf(a->lod)) ==
            ac_get_elem_bits(ctx, LLVMTypeOf(a->coords[0])))));
   assert(!a->min_lod ||
          ac_get_elem_bits(ctx, LLVMTypeOf(a->min_lod)) ==
          ac_get_elem_bits(ctx, LLVMTypeOf(a->coords[0])));

   if (a->opcode == ac_image_get_lod) {
      switch (dim) {
      case ac_image_1darray:
         dim = ac_image_1d;
         break;
      case ac_image_2darray:
      case ac_image_cube:
         dim = ac_image_2d;
         break;
      default:
         break;
      }
   }

   bool sample = a->opcode == ac_image_sample || a->opcode == ac_image_gather4 ||
                 a->opcode == ac_image_get_lod;
   bool atomic = a->opcode == ac_image_atomic || a->opcode == ac_image_atomic_cmpswap;
   bool load = a->opcode == ac_image_sample || a->opcode == ac_image_gather4 ||
               a->opcode == ac_image_load || a->opcode == ac_image_load_mip;
   LLVMTypeRef coord_type = sample ? (a->a16 ? ctx->f16 : ctx->f32) : (a->a16 ? ctx->i16 : ctx->i32);
   uint8_t dmask = a->dmask;
   LLVMTypeRef data_type;
   char data_type_str[32];

   if (atomic) {
      data_type = LLVMTypeOf(a->data[0]);
   } else if (a->opcode == ac_image_store || a->opcode == ac_image_store_mip) {
      /* Image stores might have been shrinked using the format. */
      data_type = LLVMTypeOf(a->data[0]);
      dmask = (1 << ac_get_llvm_num_components(a->data[0])) - 1;
   } else {
      data_type = a->d16 ? ctx->v4f16 : ctx->v4f32;
   }

   if (a->tfe) {
      data_type = LLVMStructTypeInContext(
         ctx->context, (LLVMTypeRef[]){data_type, ctx->i32}, 2, false);
   }

   if (atomic || a->opcode == ac_image_store || a->opcode == ac_image_store_mip) {
      args[num_args++] = a->data[0];
      if (a->opcode == ac_image_atomic_cmpswap)
         args[num_args++] = a->data[1];
   }

   if (!atomic)
      args[num_args++] = LLVMConstInt(ctx->i32, dmask, false);

   if (a->offset)
      args[num_args++] = ac_to_integer(ctx, a->offset);
   if (a->bias) {
      args[num_args++] = ac_to_float(ctx, a->bias);
      overload[num_overloads++] = ".f32";
   }
   if (a->compare)
      args[num_args++] = ac_to_float(ctx, a->compare);
   if (a->derivs[0]) {
      unsigned count = ac_num_derivs(dim);
      for (unsigned i = 0; i < count; ++i)
         args[num_args++] = ac_to_float(ctx, a->derivs[i]);
      overload[num_overloads++] = a->g16 ? ".f16" : ".f32";
   }
   unsigned num_coords = a->opcode != ac_image_get_resinfo ? ac_num_coords(dim) : 0;
   for (unsigned i = 0; i < num_coords; ++i)
      args[num_args++] = LLVMBuildBitCast(ctx->builder, a->coords[i], coord_type, "");
   if (a->lod)
      args[num_args++] = LLVMBuildBitCast(ctx->builder, a->lod, coord_type, "");
   if (a->min_lod)
      args[num_args++] = LLVMBuildBitCast(ctx->builder, a->min_lod, coord_type, "");

   overload[num_overloads++] = sample ? (a->a16 ? ".f16" : ".f32") : (a->a16 ? ".i16" : ".i32");

   args[num_args++] = a->resource;
   if (sample) {
      args[num_args++] = a->sampler;
      args[num_args++] = LLVMConstInt(ctx->i1, a->unorm, false);
   }

   args[num_args++] = a->tfe ? ctx->i32_1 : ctx->i32_0; /* texfailctrl */
   args[num_args++] = LLVMConstInt(
      ctx->i32, load ? get_load_cache_policy(ctx, a->cache_policy) : a->cache_policy, false);

   const char *name;
   const char *atomic_subop = "";
   switch (a->opcode) {
   case ac_image_sample:
      name = "sample";
      break;
   case ac_image_gather4:
      name = "gather4";
      break;
   case ac_image_load:
      name = "load";
      break;
   case ac_image_load_mip:
      name = "load.mip";
      break;
   case ac_image_store:
      name = "store";
      break;
   case ac_image_store_mip:
      name = "store.mip";
      break;
   case ac_image_atomic:
      name = "atomic.";
      atomic_subop = get_atomic_name(a->atomic);
      break;
   case ac_image_atomic_cmpswap:
      name = "atomic.";
      atomic_subop = "cmpswap";
      break;
   case ac_image_get_lod:
      name = "getlod";
      break;
   case ac_image_get_resinfo:
      name = "getresinfo";
      break;
   default:
      unreachable("invalid image opcode");
   }

   const char *dimname;
   switch (dim) {
   case ac_image_1d:
      dimname = "1d";
      break;
   case ac_image_2d:
      dimname = "2d";
      break;
   case ac_image_3d:
      dimname = "3d";
      break;
   case ac_image_cube:
      dimname = "cube";
      break;
   case ac_image_1darray:
      dimname = "1darray";
      break;
   case ac_image_2darray:
      dimname = "2darray";
      break;
   case ac_image_2dmsaa:
      dimname = "2dmsaa";
      break;
   case ac_image_2darraymsaa:
      dimname = "2darraymsaa";
      break;
   default:
      unreachable("invalid dim");
   }

   ac_build_type_name_for_intr(data_type, data_type_str, sizeof(data_type_str));

   bool lod_suffix = a->lod && (a->opcode == ac_image_sample || a->opcode == ac_image_gather4);
   char intr_name[96];
   snprintf(intr_name, sizeof(intr_name),
            "llvm.amdgcn.image.%s%s" /* base name */
            "%s%s%s%s"               /* sample/gather modifiers */
            ".%s.%s%s%s%s",          /* dimension and type overloads */
            name, atomic_subop, a->compare ? ".c" : "",
            a->bias ? ".b" : lod_suffix ? ".l" : a->derivs[0] ? ".d" : a->level_zero ? ".lz" : "",
            a->min_lod ? ".cl" : "", a->offset ? ".o" : "", dimname,
            data_type_str, overload[0], overload[1], overload[2]);

   LLVMTypeRef retty;
   if (a->opcode == ac_image_store || a->opcode == ac_image_store_mip)
      retty = ctx->voidt;
   else
      retty = data_type;

   LLVMValueRef result = ac_build_intrinsic(ctx, intr_name, retty, args, num_args, a->attributes);
   if (a->tfe) {
      LLVMValueRef texel = LLVMBuildExtractValue(ctx->builder, result, 0, "");
      LLVMValueRef code = LLVMBuildExtractValue(ctx->builder, result, 1, "");
      result = ac_build_concat(ctx, texel, ac_to_float(ctx, code));
   }

   if (!sample && !atomic && retty != ctx->voidt)
      result = ac_to_integer(ctx, result);

   return result;
}

LLVMValueRef ac_build_image_get_sample_count(struct ac_llvm_context *ctx, LLVMValueRef rsrc)
{
   LLVMValueRef samples;

   /* Read the samples from the descriptor directly.
    * Hardware doesn't have any instruction for this.
    */
   samples = LLVMBuildExtractElement(ctx->builder, rsrc, LLVMConstInt(ctx->i32, 3, 0), "");
   samples = LLVMBuildLShr(ctx->builder, samples, LLVMConstInt(ctx->i32, 16, 0), "");
   samples = LLVMBuildAnd(ctx->builder, samples, LLVMConstInt(ctx->i32, 0xf, 0), "");
   samples = LLVMBuildShl(ctx->builder, ctx->i32_1, samples, "");
   return samples;
}

LLVMValueRef ac_build_cvt_pkrtz_f16(struct ac_llvm_context *ctx, LLVMValueRef args[2])
{
   return ac_build_intrinsic(ctx, "llvm.amdgcn.cvt.pkrtz", ctx->v2f16, args, 2,
                             AC_FUNC_ATTR_READNONE);
}

LLVMValueRef ac_build_cvt_pknorm_i16(struct ac_llvm_context *ctx, LLVMValueRef args[2])
{
   LLVMValueRef res = ac_build_intrinsic(ctx, "llvm.amdgcn.cvt.pknorm.i16", ctx->v2i16, args, 2,
                                         AC_FUNC_ATTR_READNONE);
   return LLVMBuildBitCast(ctx->builder, res, ctx->i32, "");
}

LLVMValueRef ac_build_cvt_pknorm_u16(struct ac_llvm_context *ctx, LLVMValueRef args[2])
{
   LLVMValueRef res = ac_build_intrinsic(ctx, "llvm.amdgcn.cvt.pknorm.u16", ctx->v2i16, args, 2,
                                         AC_FUNC_ATTR_READNONE);
   return LLVMBuildBitCast(ctx->builder, res, ctx->i32, "");
}

LLVMValueRef ac_build_cvt_pknorm_i16_f16(struct ac_llvm_context *ctx,
                                         LLVMValueRef args[2])
{
   LLVMTypeRef param_types[] = {ctx->f16, ctx->f16};
   LLVMTypeRef calltype = LLVMFunctionType(ctx->i32, param_types, 2, false);
   LLVMValueRef code = LLVMConstInlineAsm(calltype,
                                          "v_cvt_pknorm_i16_f16 $0, $1, $2", "=v,v,v",
                                          false, false);
   return LLVMBuildCall(ctx->builder, code, args, 2, "");
}

LLVMValueRef ac_build_cvt_pknorm_u16_f16(struct ac_llvm_context *ctx,
                                         LLVMValueRef args[2])
{
   LLVMTypeRef param_types[] = {ctx->f16, ctx->f16};
   LLVMTypeRef calltype = LLVMFunctionType(ctx->i32, param_types, 2, false);
   LLVMValueRef code = LLVMConstInlineAsm(calltype,
                                          "v_cvt_pknorm_u16_f16 $0, $1, $2", "=v,v,v",
                                          false, false);
   return LLVMBuildCall(ctx->builder, code, args, 2, "");
}

/* The 8-bit and 10-bit clamping is for HW workarounds. */
LLVMValueRef ac_build_cvt_pk_i16(struct ac_llvm_context *ctx, LLVMValueRef args[2], unsigned bits,
                                 bool hi)
{
   assert(bits == 8 || bits == 10 || bits == 16);

   LLVMValueRef max_rgb = LLVMConstInt(ctx->i32, bits == 8 ? 127 : bits == 10 ? 511 : 32767, 0);
   LLVMValueRef min_rgb = LLVMConstInt(ctx->i32, bits == 8 ? -128 : bits == 10 ? -512 : -32768, 0);
   LLVMValueRef max_alpha = bits != 10 ? max_rgb : ctx->i32_1;
   LLVMValueRef min_alpha = bits != 10 ? min_rgb : LLVMConstInt(ctx->i32, -2, 0);

   /* Clamp. */
   if (bits != 16) {
      for (int i = 0; i < 2; i++) {
         bool alpha = hi && i == 1;
         args[i] = ac_build_imin(ctx, args[i], alpha ? max_alpha : max_rgb);
         args[i] = ac_build_imax(ctx, args[i], alpha ? min_alpha : min_rgb);
      }
   }

   LLVMValueRef res =
      ac_build_intrinsic(ctx, "llvm.amdgcn.cvt.pk.i16", ctx->v2i16, args, 2, AC_FUNC_ATTR_READNONE);
   return LLVMBuildBitCast(ctx->builder, res, ctx->i32, "");
}

/* The 8-bit and 10-bit clamping is for HW workarounds. */
LLVMValueRef ac_build_cvt_pk_u16(struct ac_llvm_context *ctx, LLVMValueRef args[2], unsigned bits,
                                 bool hi)
{
   assert(bits == 8 || bits == 10 || bits == 16);

   LLVMValueRef max_rgb = LLVMConstInt(ctx->i32, bits == 8 ? 255 : bits == 10 ? 1023 : 65535, 0);
   LLVMValueRef max_alpha = bits != 10 ? max_rgb : LLVMConstInt(ctx->i32, 3, 0);

   /* Clamp. */
   if (bits != 16) {
      for (int i = 0; i < 2; i++) {
         bool alpha = hi && i == 1;
         args[i] = ac_build_umin(ctx, args[i], alpha ? max_alpha : max_rgb);
      }
   }

   LLVMValueRef res =
      ac_build_intrinsic(ctx, "llvm.amdgcn.cvt.pk.u16", ctx->v2i16, args, 2, AC_FUNC_ATTR_READNONE);
   return LLVMBuildBitCast(ctx->builder, res, ctx->i32, "");
}

LLVMValueRef ac_build_wqm_vote(struct ac_llvm_context *ctx, LLVMValueRef i1)
{
   return ac_build_intrinsic(ctx, "llvm.amdgcn.wqm.vote", ctx->i1, &i1, 1, AC_FUNC_ATTR_READNONE);
}

void ac_build_kill_if_false(struct ac_llvm_context *ctx, LLVMValueRef i1)
{
   ac_build_intrinsic(ctx, "llvm.amdgcn.kill", ctx->voidt, &i1, 1, 0);
}

LLVMValueRef ac_build_bfe(struct ac_llvm_context *ctx, LLVMValueRef input, LLVMValueRef offset,
                          LLVMValueRef width, bool is_signed)
{
   LLVMValueRef args[] = {
      input,
      offset,
      width,
   };

   return ac_build_intrinsic(ctx, is_signed ? "llvm.amdgcn.sbfe.i32" : "llvm.amdgcn.ubfe.i32",
                             ctx->i32, args, 3, AC_FUNC_ATTR_READNONE);
}

LLVMValueRef ac_build_imad(struct ac_llvm_context *ctx, LLVMValueRef s0, LLVMValueRef s1,
                           LLVMValueRef s2)
{
   return LLVMBuildAdd(ctx->builder, LLVMBuildMul(ctx->builder, s0, s1, ""), s2, "");
}

LLVMValueRef ac_build_fmad(struct ac_llvm_context *ctx, LLVMValueRef s0, LLVMValueRef s1,
                           LLVMValueRef s2)
{
   /* FMA is better on GFX10, because it has FMA units instead of MUL-ADD units. */
   if (ctx->chip_class >= GFX10) {
      return ac_build_intrinsic(ctx, "llvm.fma.f32", ctx->f32, (LLVMValueRef[]){s0, s1, s2}, 3,
                                AC_FUNC_ATTR_READNONE);
   }

   return LLVMBuildFAdd(ctx->builder, LLVMBuildFMul(ctx->builder, s0, s1, ""), s2, "");
}

void ac_build_waitcnt(struct ac_llvm_context *ctx, unsigned wait_flags)
{
   if (!wait_flags)
      return;

   unsigned lgkmcnt = 63;
   unsigned vmcnt = ctx->chip_class >= GFX9 ? 63 : 15;
   unsigned vscnt = 63;

   if (wait_flags & AC_WAIT_LGKM)
      lgkmcnt = 0;
   if (wait_flags & AC_WAIT_VLOAD)
      vmcnt = 0;

   if (wait_flags & AC_WAIT_VSTORE) {
      if (ctx->chip_class >= GFX10)
         vscnt = 0;
      else
         vmcnt = 0;
   }

   /* There is no intrinsic for vscnt(0), so use a fence. */
   if ((wait_flags & AC_WAIT_LGKM && wait_flags & AC_WAIT_VLOAD && wait_flags & AC_WAIT_VSTORE) ||
       vscnt == 0) {
      LLVMBuildFence(ctx->builder, LLVMAtomicOrderingRelease, false, "");
      return;
   }

   unsigned simm16 = (lgkmcnt << 8) | (7 << 4) | /* expcnt */
                     (vmcnt & 0xf) | ((vmcnt >> 4) << 14);

   LLVMValueRef args[1] = {
      LLVMConstInt(ctx->i32, simm16, false),
   };
   ac_build_intrinsic(ctx, "llvm.amdgcn.s.waitcnt", ctx->voidt, args, 1, 0);
}

LLVMValueRef ac_build_fsat(struct ac_llvm_context *ctx, LLVMValueRef src,
                           LLVMTypeRef type)
{
   unsigned bitsize = ac_get_elem_bits(ctx, type);
   LLVMValueRef zero = LLVMConstReal(type, 0.0);
   LLVMValueRef one = LLVMConstReal(type, 1.0);
   LLVMValueRef result;

   if (bitsize == 64 || (bitsize == 16 && ctx->chip_class <= GFX8) || type == ctx->v2f16) {
      /* Use fmin/fmax for 64-bit fsat or 16-bit on GFX6-GFX8 because LLVM
       * doesn't expose an intrinsic.
       */
      result = ac_build_fmin(ctx, ac_build_fmax(ctx, src, zero), one);
   } else {
      LLVMTypeRef type;
      char *intr;

      if (bitsize == 16) {
         intr = "llvm.amdgcn.fmed3.f16";
         type = ctx->f16;
      } else {
         assert(bitsize == 32);
         intr = "llvm.amdgcn.fmed3.f32";
         type = ctx->f32;
      }

      LLVMValueRef params[] = {
         zero,
         one,
         src,
      };

      result = ac_build_intrinsic(ctx, intr, type, params, 3,
                                  AC_FUNC_ATTR_READNONE);
   }

   if (ctx->chip_class < GFX9 && bitsize == 32) {
      /* Only pre-GFX9 chips do not flush denorms. */
      result = ac_build_canonicalize(ctx, result, bitsize);
   }

   return result;
}

LLVMValueRef ac_build_fract(struct ac_llvm_context *ctx, LLVMValueRef src0, unsigned bitsize)
{
   LLVMTypeRef type;
   char *intr;

   if (bitsize == 16) {
      intr = "llvm.amdgcn.fract.f16";
      type = ctx->f16;
   } else if (bitsize == 32) {
      intr = "llvm.amdgcn.fract.f32";
      type = ctx->f32;
   } else {
      intr = "llvm.amdgcn.fract.f64";
      type = ctx->f64;
   }

   LLVMValueRef params[] = {
      src0,
   };
   return ac_build_intrinsic(ctx, intr, type, params, 1, AC_FUNC_ATTR_READNONE);
}

LLVMValueRef ac_const_uint_vec(struct ac_llvm_context *ctx, LLVMTypeRef type, uint64_t value)
{

   if (LLVMGetTypeKind(type) == LLVMVectorTypeKind) {
      LLVMValueRef scalar = LLVMConstInt(LLVMGetElementType(type), value, 0);
      unsigned vec_size = LLVMGetVectorSize(type);
      LLVMValueRef *scalars = alloca(vec_size * sizeof(LLVMValueRef));

      for (unsigned i = 0; i < vec_size; i++)
         scalars[i] = scalar;
      return LLVMConstVector(scalars, vec_size);
   }
   return LLVMConstInt(type, value, 0);
}

LLVMValueRef ac_build_isign(struct ac_llvm_context *ctx, LLVMValueRef src0)
{
   LLVMTypeRef type = LLVMTypeOf(src0);
   LLVMValueRef val;

   /* v_med3 is selected only when max is first. (LLVM bug?) */
   val = ac_build_imax(ctx, src0, ac_const_uint_vec(ctx, type, -1));
   return ac_build_imin(ctx, val, ac_const_uint_vec(ctx, type, 1));
}

static LLVMValueRef ac_eliminate_negative_zero(struct ac_llvm_context *ctx, LLVMValueRef val)
{
   ac_enable_signed_zeros(ctx);
   /* (val + 0) converts negative zero to positive zero. */
   val = LLVMBuildFAdd(ctx->builder, val, LLVMConstNull(LLVMTypeOf(val)), "");
   ac_disable_signed_zeros(ctx);
   return val;
}

LLVMValueRef ac_build_fsign(struct ac_llvm_context *ctx, LLVMValueRef src)
{
   LLVMTypeRef type = LLVMTypeOf(src);
   LLVMValueRef pos, neg, dw[2], val;
   unsigned bitsize = ac_get_elem_bits(ctx, type);

   /* The standard version leads to this:
    *   v_cmp_ngt_f32_e64 s[0:1], s4, 0                       ; D40B0000 00010004
    *   v_cndmask_b32_e64 v4, 1.0, s4, s[0:1]                 ; D5010004 000008F2
    *   v_cmp_le_f32_e32 vcc, 0, v4                           ; 7C060880
    *   v_cndmask_b32_e32 v4, -1.0, v4, vcc                   ; 020808F3
    *
    * The isign version:
    *   v_add_f32_e64 v4, s4, 0                               ; D5030004 00010004
    *   v_med3_i32 v4, v4, -1, 1                              ; D5580004 02058304
    *   v_cvt_f32_i32_e32 v4, v4                              ; 7E080B04
    *
    * (src0 + 0) converts negative zero to positive zero.
    * After that, int(fsign(x)) == isign(floatBitsToInt(x)).
    *
    * For FP64, use the standard version, which doesn't suffer from the huge DP rate
    * reduction. (FP64 comparisons are as fast as int64 comparisons)
    */
   if (bitsize == 16 || bitsize == 32) {
      val = ac_to_integer(ctx, ac_eliminate_negative_zero(ctx, src));
      val = ac_build_isign(ctx, val);
      return LLVMBuildSIToFP(ctx->builder, val, type, "");
   }

   assert(bitsize == 64);
   pos = LLVMBuildFCmp(ctx->builder, LLVMRealOGT, src, ctx->f64_0, "");
   neg = LLVMBuildFCmp(ctx->builder, LLVMRealOLT, src, ctx->f64_0, "");
   dw[0] = ctx->i32_0;
   dw[1] = LLVMBuildSelect(
      ctx->builder, pos, LLVMConstInt(ctx->i32, 0x3FF00000, 0),
      LLVMBuildSelect(ctx->builder, neg, LLVMConstInt(ctx->i32, 0xBFF00000, 0), ctx->i32_0, ""),
      "");
   return LLVMBuildBitCast(ctx->builder, ac_build_gather_values(ctx, dw, 2), ctx->f64, "");
}

LLVMValueRef ac_build_bit_count(struct ac_llvm_context *ctx, LLVMValueRef src0)
{
   LLVMValueRef result;
   unsigned bitsize;

   bitsize = ac_get_elem_bits(ctx, LLVMTypeOf(src0));

   switch (bitsize) {
   case 128:
      result = ac_build_intrinsic(ctx, "llvm.ctpop.i128", ctx->i128, (LLVMValueRef[]){src0}, 1,
                                  AC_FUNC_ATTR_READNONE);
      result = LLVMBuildTrunc(ctx->builder, result, ctx->i32, "");
      break;
   case 64:
      result = ac_build_intrinsic(ctx, "llvm.ctpop.i64", ctx->i64, (LLVMValueRef[]){src0}, 1,
                                  AC_FUNC_ATTR_READNONE);

      result = LLVMBuildTrunc(ctx->builder, result, ctx->i32, "");
      break;
   case 32:
      result = ac_build_intrinsic(ctx, "llvm.ctpop.i32", ctx->i32, (LLVMValueRef[]){src0}, 1,
                                  AC_FUNC_ATTR_READNONE);
      break;
   case 16:
      result = ac_build_intrinsic(ctx, "llvm.ctpop.i16", ctx->i16, (LLVMValueRef[]){src0}, 1,
                                  AC_FUNC_ATTR_READNONE);

      result = LLVMBuildZExt(ctx->builder, result, ctx->i32, "");
      break;
   case 8:
      result = ac_build_intrinsic(ctx, "llvm.ctpop.i8", ctx->i8, (LLVMValueRef[]){src0}, 1,
                                  AC_FUNC_ATTR_READNONE);

      result = LLVMBuildZExt(ctx->builder, result, ctx->i32, "");
      break;
   default:
      unreachable(!"invalid bitsize");
      break;
   }

   return result;
}

LLVMValueRef ac_build_bitfield_reverse(struct ac_llvm_context *ctx, LLVMValueRef src0)
{
   LLVMValueRef result;
   unsigned bitsize;

   bitsize = ac_get_elem_bits(ctx, LLVMTypeOf(src0));

   switch (bitsize) {
   case 64:
      result = ac_build_intrinsic(ctx, "llvm.bitreverse.i64", ctx->i64, (LLVMValueRef[]){src0}, 1,
                                  AC_FUNC_ATTR_READNONE);

      result = LLVMBuildTrunc(ctx->builder, result, ctx->i32, "");
      break;
   case 32:
      result = ac_build_intrinsic(ctx, "llvm.bitreverse.i32", ctx->i32, (LLVMValueRef[]){src0}, 1,
                                  AC_FUNC_ATTR_READNONE);
      break;
   case 16:
      result = ac_build_intrinsic(ctx, "llvm.bitreverse.i16", ctx->i16, (LLVMValueRef[]){src0}, 1,
                                  AC_FUNC_ATTR_READNONE);

      result = LLVMBuildZExt(ctx->builder, result, ctx->i32, "");
      break;
   case 8:
      result = ac_build_intrinsic(ctx, "llvm.bitreverse.i8", ctx->i8, (LLVMValueRef[]){src0}, 1,
                                  AC_FUNC_ATTR_READNONE);

      result = LLVMBuildZExt(ctx->builder, result, ctx->i32, "");
      break;
   default:
      unreachable(!"invalid bitsize");
      break;
   }

   return result;
}

#define AC_EXP_TARGET           0
#define AC_EXP_ENABLED_CHANNELS 1
#define AC_EXP_OUT0             2

enum ac_ir_type
{
   AC_IR_UNDEF,
   AC_IR_CONST,
   AC_IR_VALUE,
};

struct ac_vs_exp_chan {
   LLVMValueRef value;
   float const_float;
   enum ac_ir_type type;
};

struct ac_vs_exp_inst {
   unsigned offset;
   LLVMValueRef inst;
   struct ac_vs_exp_chan chan[4];
};

struct ac_vs_exports {
   unsigned num;
   struct ac_vs_exp_inst exp[VARYING_SLOT_MAX];
};

/* Return true if the PARAM export has been eliminated. */
static bool ac_eliminate_const_output(uint8_t *vs_output_param_offset, uint32_t num_outputs,
                                      struct ac_vs_exp_inst *exp)
{
   unsigned i, default_val; /* SPI_PS_INPUT_CNTL_i.DEFAULT_VAL */
   bool is_zero[4] = {0}, is_one[4] = {0};

   for (i = 0; i < 4; i++) {
      /* It's a constant expression. Undef outputs are eliminated too. */
      if (exp->chan[i].type == AC_IR_UNDEF) {
         is_zero[i] = true;
         is_one[i] = true;
      } else if (exp->chan[i].type == AC_IR_CONST) {
         if (exp->chan[i].const_float == 0)
            is_zero[i] = true;
         else if (exp->chan[i].const_float == 1)
            is_one[i] = true;
         else
            return false; /* other constant */
      } else
         return false;
   }

   /* Only certain combinations of 0 and 1 can be eliminated. */
   if (is_zero[0] && is_zero[1] && is_zero[2])
      default_val = is_zero[3] ? 0 : 1;
   else if (is_one[0] && is_one[1] && is_one[2])
      default_val = is_zero[3] ? 2 : 3;
   else
      return false;

   /* The PARAM export can be represented as DEFAULT_VAL. Kill it. */
   LLVMInstructionEraseFromParent(exp->inst);

   /* Change OFFSET to DEFAULT_VAL. */
   for (i = 0; i < num_outputs; i++) {
      if (vs_output_param_offset[i] == exp->offset) {
         vs_output_param_offset[i] = AC_EXP_PARAM_DEFAULT_VAL_0000 + default_val;
         break;
      }
   }
   return true;
}

static bool ac_eliminate_duplicated_output(struct ac_llvm_context *ctx,
                                           uint8_t *vs_output_param_offset, uint32_t num_outputs,
                                           struct ac_vs_exports *processed,
                                           struct ac_vs_exp_inst *exp)
{
   unsigned p, copy_back_channels = 0;

   /* See if the output is already in the list of processed outputs.
    * The LLVMValueRef comparison relies on SSA.
    */
   for (p = 0; p < processed->num; p++) {
      bool different = false;

      for (unsigned j = 0; j < 4; j++) {
         struct ac_vs_exp_chan *c1 = &processed->exp[p].chan[j];
         struct ac_vs_exp_chan *c2 = &exp->chan[j];

         /* Treat undef as a match. */
         if (c2->type == AC_IR_UNDEF)
            continue;

         /* If c1 is undef but c2 isn't, we can copy c2 to c1
          * and consider the instruction duplicated.
          */
         if (c1->type == AC_IR_UNDEF) {
            copy_back_channels |= 1 << j;
            continue;
         }

         /* Test whether the channels are not equal. */
         if (c1->type != c2->type ||
             (c1->type == AC_IR_CONST && c1->const_float != c2->const_float) ||
             (c1->type == AC_IR_VALUE && c1->value != c2->value)) {
            different = true;
            break;
         }
      }
      if (!different)
         break;

      copy_back_channels = 0;
   }
   if (p == processed->num)
      return false;

   /* If a match was found, but the matching export has undef where the new
    * one has a normal value, copy the normal value to the undef channel.
    */
   struct ac_vs_exp_inst *match = &processed->exp[p];

   /* Get current enabled channels mask. */
   LLVMValueRef arg = LLVMGetOperand(match->inst, AC_EXP_ENABLED_CHANNELS);
   unsigned enabled_channels = LLVMConstIntGetZExtValue(arg);

   while (copy_back_channels) {
      unsigned chan = u_bit_scan(&copy_back_channels);

      assert(match->chan[chan].type == AC_IR_UNDEF);
      LLVMSetOperand(match->inst, AC_EXP_OUT0 + chan, exp->chan[chan].value);
      match->chan[chan] = exp->chan[chan];

      /* Update number of enabled channels because the original mask
       * is not always 0xf.
       */
      enabled_channels |= (1 << chan);
      LLVMSetOperand(match->inst, AC_EXP_ENABLED_CHANNELS,
                     LLVMConstInt(ctx->i32, enabled_channels, 0));
   }

   /* The PARAM export is duplicated. Kill it. */
   LLVMInstructionEraseFromParent(exp->inst);

   /* Change OFFSET to the matching export. */
   for (unsigned i = 0; i < num_outputs; i++) {
      if (vs_output_param_offset[i] == exp->offset) {
         vs_output_param_offset[i] = match->offset;
         break;
      }
   }
   return true;
}

void ac_optimize_vs_outputs(struct ac_llvm_context *ctx, LLVMValueRef main_fn,
                            uint8_t *vs_output_param_offset, uint32_t num_outputs,
                            uint32_t skip_output_mask, uint8_t *num_param_exports)
{
   LLVMBasicBlockRef bb;
   bool removed_any = false;
   struct ac_vs_exports exports;

   exports.num = 0;

   /* Process all LLVM instructions. */
   bb = LLVMGetFirstBasicBlock(main_fn);
   while (bb) {
      LLVMValueRef inst = LLVMGetFirstInstruction(bb);

      while (inst) {
         LLVMValueRef cur = inst;
         inst = LLVMGetNextInstruction(inst);
         struct ac_vs_exp_inst exp;

         if (LLVMGetInstructionOpcode(cur) != LLVMCall)
            continue;

         LLVMValueRef callee = ac_llvm_get_called_value(cur);

         if (!ac_llvm_is_function(callee))
            continue;

         const char *name = LLVMGetValueName(callee);
         unsigned num_args = LLVMCountParams(callee);

         /* Check if this is an export instruction. */
         if ((num_args != 9 && num_args != 8) ||
             (strcmp(name, "llvm.SI.export") && strcmp(name, "llvm.amdgcn.exp.f32")))
            continue;

         LLVMValueRef arg = LLVMGetOperand(cur, AC_EXP_TARGET);
         unsigned target = LLVMConstIntGetZExtValue(arg);

         if (target < V_008DFC_SQ_EXP_PARAM)
            continue;

         target -= V_008DFC_SQ_EXP_PARAM;

         /* Parse the instruction. */
         memset(&exp, 0, sizeof(exp));
         exp.offset = target;
         exp.inst = cur;

         for (unsigned i = 0; i < 4; i++) {
            LLVMValueRef v = LLVMGetOperand(cur, AC_EXP_OUT0 + i);

            exp.chan[i].value = v;

            if (LLVMIsUndef(v)) {
               exp.chan[i].type = AC_IR_UNDEF;
            } else if (LLVMIsAConstantFP(v)) {
               LLVMBool loses_info;
               exp.chan[i].type = AC_IR_CONST;
               exp.chan[i].const_float = LLVMConstRealGetDouble(v, &loses_info);
            } else {
               exp.chan[i].type = AC_IR_VALUE;
            }
         }

         /* Eliminate constant and duplicated PARAM exports. */
         if (!((1u << target) & skip_output_mask) &&
             (ac_eliminate_const_output(vs_output_param_offset, num_outputs, &exp) ||
              ac_eliminate_duplicated_output(ctx, vs_output_param_offset, num_outputs, &exports,
                                             &exp))) {
            removed_any = true;
         } else {
            exports.exp[exports.num++] = exp;
         }
      }
      bb = LLVMGetNextBasicBlock(bb);
   }

   /* Remove holes in export memory due to removed PARAM exports.
    * This is done by renumbering all PARAM exports.
    */
   if (removed_any) {
      uint8_t old_offset[VARYING_SLOT_MAX];
      unsigned out, i;

      /* Make a copy of the offsets. We need the old version while
       * we are modifying some of them. */
      memcpy(old_offset, vs_output_param_offset, sizeof(old_offset));

      for (i = 0; i < exports.num; i++) {
         unsigned offset = exports.exp[i].offset;

         /* Update vs_output_param_offset. Multiple outputs can
          * have the same offset.
          */
         for (out = 0; out < num_outputs; out++) {
            if (old_offset[out] == offset)
               vs_output_param_offset[out] = i;
         }

         /* Change the PARAM offset in the instruction. */
         LLVMSetOperand(exports.exp[i].inst, AC_EXP_TARGET,
                        LLVMConstInt(ctx->i32, V_008DFC_SQ_EXP_PARAM + i, 0));
      }
      *num_param_exports = exports.num;
   }
}

void ac_init_exec_full_mask(struct ac_llvm_context *ctx)
{
   LLVMValueRef full_mask = LLVMConstInt(ctx->i64, ~0ull, 0);
   ac_build_intrinsic(ctx, "llvm.amdgcn.init.exec", ctx->voidt, &full_mask, 1,
                      AC_FUNC_ATTR_CONVERGENT);
}

void ac_declare_lds_as_pointer(struct ac_llvm_context *ctx)
{
   unsigned lds_size = ctx->chip_class >= GFX7 ? 65536 : 32768;
   ctx->lds = LLVMBuildIntToPtr(
      ctx->builder, ctx->i32_0,
      LLVMPointerType(LLVMArrayType(ctx->i32, lds_size / 4), AC_ADDR_SPACE_LDS), "lds");
}

LLVMValueRef ac_lds_load(struct ac_llvm_context *ctx, LLVMValueRef dw_addr)
{
   return LLVMBuildLoad(ctx->builder, ac_build_gep0(ctx, ctx->lds, dw_addr), "");
}

void ac_lds_store(struct ac_llvm_context *ctx, LLVMValueRef dw_addr, LLVMValueRef value)
{
   value = ac_to_integer(ctx, value);
   ac_build_indexed_store(ctx, ctx->lds, dw_addr, value);
}

LLVMValueRef ac_find_lsb(struct ac_llvm_context *ctx, LLVMTypeRef dst_type, LLVMValueRef src0)
{
   unsigned src0_bitsize = ac_get_elem_bits(ctx, LLVMTypeOf(src0));
   const char *intrin_name;
   LLVMTypeRef type;
   LLVMValueRef zero;

   switch (src0_bitsize) {
   case 64:
      intrin_name = "llvm.cttz.i64";
      type = ctx->i64;
      zero = ctx->i64_0;
      break;
   case 32:
      intrin_name = "llvm.cttz.i32";
      type = ctx->i32;
      zero = ctx->i32_0;
      break;
   case 16:
      intrin_name = "llvm.cttz.i16";
      type = ctx->i16;
      zero = ctx->i16_0;
      break;
   case 8:
      intrin_name = "llvm.cttz.i8";
      type = ctx->i8;
      zero = ctx->i8_0;
      break;
   default:
      unreachable(!"invalid bitsize");
   }

   LLVMValueRef params[2] = {
      src0,

      /* The value of 1 means that ffs(x=0) = undef, so LLVM won't
       * add special code to check for x=0. The reason is that
       * the LLVM behavior for x=0 is different from what we
       * need here. However, LLVM also assumes that ffs(x) is
       * in [0, 31], but GLSL expects that ffs(0) = -1, so
       * a conditional assignment to handle 0 is still required.
       *
       * The hardware already implements the correct behavior.
       */
      ctx->i1true,
   };

   LLVMValueRef lsb = ac_build_intrinsic(ctx, intrin_name, type, params, 2, AC_FUNC_ATTR_READNONE);

   if (src0_bitsize == 64) {
      lsb = LLVMBuildTrunc(ctx->builder, lsb, ctx->i32, "");
   } else if (src0_bitsize < 32) {
      lsb = LLVMBuildSExt(ctx->builder, lsb, ctx->i32, "");
   }

   /* TODO: We need an intrinsic to skip this conditional. */
   /* Check for zero: */
   return LLVMBuildSelect(ctx->builder, LLVMBuildICmp(ctx->builder, LLVMIntEQ, src0, zero, ""),
                          LLVMConstInt(ctx->i32, -1, 0), lsb, "");
}

LLVMTypeRef ac_array_in_const_addr_space(LLVMTypeRef elem_type)
{
   return LLVMPointerType(elem_type, AC_ADDR_SPACE_CONST);
}

LLVMTypeRef ac_array_in_const32_addr_space(LLVMTypeRef elem_type)
{
   return LLVMPointerType(elem_type, AC_ADDR_SPACE_CONST_32BIT);
}

static struct ac_llvm_flow *get_current_flow(struct ac_llvm_context *ctx)
{
   if (ctx->flow->depth > 0)
      return &ctx->flow->stack[ctx->flow->depth - 1];
   return NULL;
}

static struct ac_llvm_flow *get_innermost_loop(struct ac_llvm_context *ctx)
{
   for (unsigned i = ctx->flow->depth; i > 0; --i) {
      if (ctx->flow->stack[i - 1].loop_entry_block)
         return &ctx->flow->stack[i - 1];
   }
   return NULL;
}

static struct ac_llvm_flow *push_flow(struct ac_llvm_context *ctx)
{
   struct ac_llvm_flow *flow;

   if (ctx->flow->depth >= ctx->flow->depth_max) {
      unsigned new_max = MAX2(ctx->flow->depth << 1, AC_LLVM_INITIAL_CF_DEPTH);

      ctx->flow->stack = realloc(ctx->flow->stack, new_max * sizeof(*ctx->flow->stack));
      ctx->flow->depth_max = new_max;
   }

   flow = &ctx->flow->stack[ctx->flow->depth];
   ctx->flow->depth++;

   flow->next_block = NULL;
   flow->loop_entry_block = NULL;
   return flow;
}

static void set_basicblock_name(LLVMBasicBlockRef bb, const char *base, int label_id)
{
   char buf[32];
   snprintf(buf, sizeof(buf), "%s%d", base, label_id);
   LLVMSetValueName(LLVMBasicBlockAsValue(bb), buf);
}

/* Append a basic block at the level of the parent flow.
 */
static LLVMBasicBlockRef append_basic_block(struct ac_llvm_context *ctx, const char *name)
{
   assert(ctx->flow->depth >= 1);

   if (ctx->flow->depth >= 2) {
      struct ac_llvm_flow *flow = &ctx->flow->stack[ctx->flow->depth - 2];

      return LLVMInsertBasicBlockInContext(ctx->context, flow->next_block, name);
   }

   LLVMValueRef main_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
   return LLVMAppendBasicBlockInContext(ctx->context, main_fn, name);
}

/* Emit a branch to the given default target for the current block if
 * applicable -- that is, if the current block does not already contain a
 * branch from a break or continue.
 */
static void emit_default_branch(LLVMBuilderRef builder, LLVMBasicBlockRef target)
{
   if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
      LLVMBuildBr(builder, target);
}

void ac_build_bgnloop(struct ac_llvm_context *ctx, int label_id)
{
   struct ac_llvm_flow *flow = push_flow(ctx);
   flow->loop_entry_block = append_basic_block(ctx, "LOOP");
   flow->next_block = append_basic_block(ctx, "ENDLOOP");
   set_basicblock_name(flow->loop_entry_block, "loop", label_id);
   LLVMBuildBr(ctx->builder, flow->loop_entry_block);
   LLVMPositionBuilderAtEnd(ctx->builder, flow->loop_entry_block);
}

void ac_build_break(struct ac_llvm_context *ctx)
{
   struct ac_llvm_flow *flow = get_innermost_loop(ctx);
   LLVMBuildBr(ctx->builder, flow->next_block);
}

void ac_build_continue(struct ac_llvm_context *ctx)
{
   struct ac_llvm_flow *flow = get_innermost_loop(ctx);
   LLVMBuildBr(ctx->builder, flow->loop_entry_block);
}

void ac_build_else(struct ac_llvm_context *ctx, int label_id)
{
   struct ac_llvm_flow *current_branch = get_current_flow(ctx);
   LLVMBasicBlockRef endif_block;

   assert(!current_branch->loop_entry_block);

   endif_block = append_basic_block(ctx, "ENDIF");
   emit_default_branch(ctx->builder, endif_block);

   LLVMPositionBuilderAtEnd(ctx->builder, current_branch->next_block);
   set_basicblock_name(current_branch->next_block, "else", label_id);

   current_branch->next_block = endif_block;
}

/* Invoked after a branch is exited. */
static void ac_branch_exited(struct ac_llvm_context *ctx)
{
   if (ctx->flow->depth == 0 && ctx->conditional_demote_seen) {
      /* The previous conditional branch contained demote. Kill threads
       * after all conditional blocks because amdgcn.wqm.vote doesn't
       * return usable values inside the blocks.
       *
       * This is an optional optimization that only kills whole inactive quads.
       */
      LLVMValueRef cond = LLVMBuildLoad(ctx->builder, ctx->postponed_kill, "");
      ac_build_kill_if_false(ctx, ac_build_wqm_vote(ctx, cond));
      ctx->conditional_demote_seen = false;
   }
}

void ac_build_endif(struct ac_llvm_context *ctx, int label_id)
{
   struct ac_llvm_flow *current_branch = get_current_flow(ctx);

   assert(!current_branch->loop_entry_block);

   emit_default_branch(ctx->builder, current_branch->next_block);
   LLVMPositionBuilderAtEnd(ctx->builder, current_branch->next_block);
   set_basicblock_name(current_branch->next_block, "endif", label_id);

   ctx->flow->depth--;
   ac_branch_exited(ctx);
}

void ac_build_endloop(struct ac_llvm_context *ctx, int label_id)
{
   struct ac_llvm_flow *current_loop = get_current_flow(ctx);

   assert(current_loop->loop_entry_block);

   emit_default_branch(ctx->builder, current_loop->loop_entry_block);

   LLVMPositionBuilderAtEnd(ctx->builder, current_loop->next_block);
   set_basicblock_name(current_loop->next_block, "endloop", label_id);
   ctx->flow->depth--;
   ac_branch_exited(ctx);
}

void ac_build_ifcc(struct ac_llvm_context *ctx, LLVMValueRef cond, int label_id)
{
   struct ac_llvm_flow *flow = push_flow(ctx);
   LLVMBasicBlockRef if_block;

   if_block = append_basic_block(ctx, "IF");
   flow->next_block = append_basic_block(ctx, "ELSE");
   set_basicblock_name(if_block, "if", label_id);
   LLVMBuildCondBr(ctx->builder, cond, if_block, flow->next_block);
   LLVMPositionBuilderAtEnd(ctx->builder, if_block);
}

LLVMValueRef ac_build_alloca_undef(struct ac_llvm_context *ac, LLVMTypeRef type, const char *name)
{
   LLVMBuilderRef builder = ac->builder;
   LLVMBasicBlockRef current_block = LLVMGetInsertBlock(builder);
   LLVMValueRef function = LLVMGetBasicBlockParent(current_block);
   LLVMBasicBlockRef first_block = LLVMGetEntryBasicBlock(function);
   LLVMValueRef first_instr = LLVMGetFirstInstruction(first_block);
   LLVMBuilderRef first_builder = LLVMCreateBuilderInContext(ac->context);
   LLVMValueRef res;

   if (first_instr) {
      LLVMPositionBuilderBefore(first_builder, first_instr);
   } else {
      LLVMPositionBuilderAtEnd(first_builder, first_block);
   }

   res = LLVMBuildAlloca(first_builder, type, name);
   LLVMDisposeBuilder(first_builder);
   return res;
}

LLVMValueRef ac_build_alloca(struct ac_llvm_context *ac, LLVMTypeRef type, const char *name)
{
   LLVMValueRef ptr = ac_build_alloca_undef(ac, type, name);
   LLVMBuildStore(ac->builder, LLVMConstNull(type), ptr);
   return ptr;
}

LLVMValueRef ac_build_alloca_init(struct ac_llvm_context *ac, LLVMValueRef val, const char *name)
{
   LLVMValueRef ptr = ac_build_alloca_undef(ac, LLVMTypeOf(val), name);
   LLVMBuildStore(ac->builder, val, ptr);
   return ptr;
}

LLVMValueRef ac_cast_ptr(struct ac_llvm_context *ctx, LLVMValueRef ptr, LLVMTypeRef type)
{
   int addr_space = LLVMGetPointerAddressSpace(LLVMTypeOf(ptr));
   return LLVMBuildBitCast(ctx->builder, ptr, LLVMPointerType(type, addr_space), "");
}

LLVMValueRef ac_trim_vector(struct ac_llvm_context *ctx, LLVMValueRef value, unsigned count)
{
   unsigned num_components = ac_get_llvm_num_components(value);
   if (count == num_components)
      return value;

   LLVMValueRef *const masks = alloca(MAX2(count, 2) * sizeof(LLVMValueRef));
   masks[0] = ctx->i32_0;
   masks[1] = ctx->i32_1;
   for (unsigned i = 2; i < count; i++)
      masks[i] = LLVMConstInt(ctx->i32, i, false);

   if (count == 1)
      return LLVMBuildExtractElement(ctx->builder, value, masks[0], "");

   LLVMValueRef swizzle = LLVMConstVector(masks, count);
   return LLVMBuildShuffleVector(ctx->builder, value, value, swizzle, "");
}

/* If param is i64 and bitwidth <= 32, the return value will be i32. */
LLVMValueRef ac_unpack_param(struct ac_llvm_context *ctx, LLVMValueRef param, unsigned rshift,
                             unsigned bitwidth)
{
   LLVMValueRef value = param;
   if (rshift)
      value = LLVMBuildLShr(ctx->builder, value, LLVMConstInt(LLVMTypeOf(param), rshift, false), "");

   if (rshift + bitwidth < 32) {
      uint64_t mask = (1ull << bitwidth) - 1;
      value = LLVMBuildAnd(ctx->builder, value, LLVMConstInt(LLVMTypeOf(param), mask, false), "");
   }

   if (bitwidth <= 32 && LLVMTypeOf(param) == ctx->i64)
      value = LLVMBuildTrunc(ctx->builder, value, ctx->i32, "");
   return value;
}

/* Adjust the sample index according to FMASK.
 *
 * For uncompressed MSAA surfaces, FMASK should return 0x76543210,
 * which is the identity mapping. Each nibble says which physical sample
 * should be fetched to get that sample.
 *
 * For example, 0x11111100 means there are only 2 samples stored and
 * the second sample covers 3/4 of the pixel. When reading samples 0
 * and 1, return physical sample 0 (determined by the first two 0s
 * in FMASK), otherwise return physical sample 1.
 *
 * The sample index should be adjusted as follows:
 *   addr[sample_index] = (fmask >> (addr[sample_index] * 4)) & 0xF;
 */
void ac_apply_fmask_to_sample(struct ac_llvm_context *ac, LLVMValueRef fmask, LLVMValueRef *addr,
                              bool is_array_tex)
{
   struct ac_image_args fmask_load = {0};
   fmask_load.opcode = ac_image_load;
   fmask_load.resource = fmask;
   fmask_load.dmask = 0xf;
   fmask_load.dim = is_array_tex ? ac_image_2darray : ac_image_2d;
   fmask_load.attributes = AC_FUNC_ATTR_READNONE;

   fmask_load.coords[0] = addr[0];
   fmask_load.coords[1] = addr[1];
   if (is_array_tex)
      fmask_load.coords[2] = addr[2];
   fmask_load.a16 = ac_get_elem_bits(ac, LLVMTypeOf(addr[0])) == 16;

   LLVMValueRef fmask_value = ac_build_image_opcode(ac, &fmask_load);
   fmask_value = LLVMBuildExtractElement(ac->builder, fmask_value, ac->i32_0, "");

   /* Apply the formula. */
   unsigned sample_chan = is_array_tex ? 3 : 2;
   LLVMValueRef final_sample;
   final_sample = LLVMBuildMul(ac->builder, addr[sample_chan],
                               LLVMConstInt(LLVMTypeOf(addr[0]), 4, 0), "");
   final_sample = LLVMBuildLShr(ac->builder, fmask_value,
                                LLVMBuildZExt(ac->builder, final_sample, ac->i32, ""), "");
   /* Mask the sample index by 0x7, because 0x8 means an unknown value
    * with EQAA, so those will map to 0. */
   final_sample = LLVMBuildAnd(ac->builder, final_sample, LLVMConstInt(ac->i32, 0x7, 0), "");
   if (fmask_load.a16)
      final_sample = LLVMBuildTrunc(ac->builder, final_sample, ac->i16, "");

   /* Don't rewrite the sample index if WORD1.DATA_FORMAT of the FMASK
    * resource descriptor is 0 (invalid).
    */
   LLVMValueRef tmp;
   tmp = LLVMBuildBitCast(ac->builder, fmask, ac->v8i32, "");
   tmp = LLVMBuildExtractElement(ac->builder, tmp, ac->i32_1, "");
   tmp = LLVMBuildICmp(ac->builder, LLVMIntNE, tmp, ac->i32_0, "");

   /* Replace the MSAA sample index. */
   addr[sample_chan] = LLVMBuildSelect(ac->builder, tmp, final_sample, addr[sample_chan], "");
}

static LLVMValueRef _ac_build_readlane(struct ac_llvm_context *ctx, LLVMValueRef src,
                                       LLVMValueRef lane, bool with_opt_barrier)
{
   LLVMTypeRef type = LLVMTypeOf(src);
   LLVMValueRef result;

   if (with_opt_barrier)
      ac_build_optimization_barrier(ctx, &src, false);

   src = LLVMBuildZExt(ctx->builder, src, ctx->i32, "");
   if (lane)
      lane = LLVMBuildZExt(ctx->builder, lane, ctx->i32, "");

   result =
      ac_build_intrinsic(ctx, lane == NULL ? "llvm.amdgcn.readfirstlane" : "llvm.amdgcn.readlane",
                         ctx->i32, (LLVMValueRef[]){src, lane}, lane == NULL ? 1 : 2,
                         AC_FUNC_ATTR_READNONE | AC_FUNC_ATTR_CONVERGENT);

   return LLVMBuildTrunc(ctx->builder, result, type, "");
}

static LLVMValueRef ac_build_readlane_common(struct ac_llvm_context *ctx, LLVMValueRef src,
                                             LLVMValueRef lane, bool with_opt_barrier)
{
   LLVMTypeRef src_type = LLVMTypeOf(src);
   src = ac_to_integer(ctx, src);
   unsigned bits = LLVMGetIntTypeWidth(LLVMTypeOf(src));
   LLVMValueRef ret;

   if (bits > 32) {
      assert(bits % 32 == 0);
      LLVMTypeRef vec_type = LLVMVectorType(ctx->i32, bits / 32);
      LLVMValueRef src_vector = LLVMBuildBitCast(ctx->builder, src, vec_type, "");
      ret = LLVMGetUndef(vec_type);
      for (unsigned i = 0; i < bits / 32; i++) {
         LLVMValueRef ret_comp;

         src = LLVMBuildExtractElement(ctx->builder, src_vector, LLVMConstInt(ctx->i32, i, 0), "");

         ret_comp = _ac_build_readlane(ctx, src, lane, with_opt_barrier);

         ret =
            LLVMBuildInsertElement(ctx->builder, ret, ret_comp, LLVMConstInt(ctx->i32, i, 0), "");
      }
   } else {
      ret = _ac_build_readlane(ctx, src, lane, with_opt_barrier);
   }

   if (LLVMGetTypeKind(src_type) == LLVMPointerTypeKind)
      return LLVMBuildIntToPtr(ctx->builder, ret, src_type, "");
   return LLVMBuildBitCast(ctx->builder, ret, src_type, "");
}

/**
 * Builds the "llvm.amdgcn.readlane" or "llvm.amdgcn.readfirstlane" intrinsic.
 *
 * The optimization barrier is not needed if the value is the same in all lanes
 * or if this is called in the outermost block.
 *
 * @param ctx
 * @param src
 * @param lane - id of the lane or NULL for the first active lane
 * @return value of the lane
 */
LLVMValueRef ac_build_readlane_no_opt_barrier(struct ac_llvm_context *ctx, LLVMValueRef src,
                                              LLVMValueRef lane)
{
   return ac_build_readlane_common(ctx, src, lane, false);
}

LLVMValueRef ac_build_readlane(struct ac_llvm_context *ctx, LLVMValueRef src, LLVMValueRef lane)
{
   return ac_build_readlane_common(ctx, src, lane, true);
}

LLVMValueRef ac_build_writelane(struct ac_llvm_context *ctx, LLVMValueRef src, LLVMValueRef value,
                                LLVMValueRef lane)
{
   return ac_build_intrinsic(ctx, "llvm.amdgcn.writelane", ctx->i32,
                             (LLVMValueRef[]){value, lane, src}, 3,
                             AC_FUNC_ATTR_READNONE | AC_FUNC_ATTR_CONVERGENT);
}

LLVMValueRef ac_build_mbcnt_add(struct ac_llvm_context *ctx, LLVMValueRef mask, LLVMValueRef add_src)
{
   if (ctx->wave_size == 32) {
      LLVMValueRef val = ac_build_intrinsic(ctx, "llvm.amdgcn.mbcnt.lo", ctx->i32,
                                (LLVMValueRef[]){mask, ctx->i32_0}, 2, AC_FUNC_ATTR_READNONE);
      ac_set_range_metadata(ctx, val, 0, ctx->wave_size);
      return val;
   }
   LLVMValueRef mask_vec = LLVMBuildBitCast(ctx->builder, mask, ctx->v2i32, "");
   LLVMValueRef mask_lo = LLVMBuildExtractElement(ctx->builder, mask_vec, ctx->i32_0, "");
   LLVMValueRef mask_hi = LLVMBuildExtractElement(ctx->builder, mask_vec, ctx->i32_1, "");
   LLVMValueRef val =
      ac_build_intrinsic(ctx, "llvm.amdgcn.mbcnt.lo", ctx->i32,
                         (LLVMValueRef[]){mask_lo, add_src}, 2, AC_FUNC_ATTR_READNONE);
   val = ac_build_intrinsic(ctx, "llvm.amdgcn.mbcnt.hi", ctx->i32, (LLVMValueRef[]){mask_hi, val},
                            2, AC_FUNC_ATTR_READNONE);
   ac_set_range_metadata(ctx, val, 0, ctx->wave_size);
   return val;
}

LLVMValueRef ac_build_mbcnt(struct ac_llvm_context *ctx, LLVMValueRef mask)
{
   return ac_build_mbcnt_add(ctx, mask, ctx->i32_0);
}

enum dpp_ctrl
{
   _dpp_quad_perm = 0x000,
   _dpp_row_sl = 0x100,
   _dpp_row_sr = 0x110,
   _dpp_row_rr = 0x120,
   dpp_wf_sl1 = 0x130,
   dpp_wf_rl1 = 0x134,
   dpp_wf_sr1 = 0x138,
   dpp_wf_rr1 = 0x13C,
   dpp_row_mirror = 0x140,
   dpp_row_half_mirror = 0x141,
   dpp_row_bcast15 = 0x142,
   dpp_row_bcast31 = 0x143
};

static inline enum dpp_ctrl dpp_quad_perm(unsigned lane0, unsigned lane1, unsigned lane2,
                                          unsigned lane3)
{
   assert(lane0 < 4 && lane1 < 4 && lane2 < 4 && lane3 < 4);
   return _dpp_quad_perm | lane0 | (lane1 << 2) | (lane2 << 4) | (lane3 << 6);
}

static inline enum dpp_ctrl dpp_row_sl(unsigned amount)
{
   assert(amount > 0 && amount < 16);
   return _dpp_row_sl | amount;
}

static inline enum dpp_ctrl dpp_row_sr(unsigned amount)
{
   assert(amount > 0 && amount < 16);
   return _dpp_row_sr | amount;
}

static LLVMValueRef _ac_build_dpp(struct ac_llvm_context *ctx, LLVMValueRef old, LLVMValueRef src,
                                  enum dpp_ctrl dpp_ctrl, unsigned row_mask, unsigned bank_mask,
                                  bool bound_ctrl)
{
   LLVMTypeRef type = LLVMTypeOf(src);
   LLVMValueRef res;

   old = LLVMBuildZExt(ctx->builder, old, ctx->i32, "");
   src = LLVMBuildZExt(ctx->builder, src, ctx->i32, "");

   res = ac_build_intrinsic(
      ctx, "llvm.amdgcn.update.dpp.i32", ctx->i32,
      (LLVMValueRef[]){old, src, LLVMConstInt(ctx->i32, dpp_ctrl, 0),
                       LLVMConstInt(ctx->i32, row_mask, 0), LLVMConstInt(ctx->i32, bank_mask, 0),
                       LLVMConstInt(ctx->i1, bound_ctrl, 0)},
      6, AC_FUNC_ATTR_READNONE | AC_FUNC_ATTR_CONVERGENT);

   return LLVMBuildTrunc(ctx->builder, res, type, "");
}

static LLVMValueRef ac_build_dpp(struct ac_llvm_context *ctx, LLVMValueRef old, LLVMValueRef src,
                                 enum dpp_ctrl dpp_ctrl, unsigned row_mask, unsigned bank_mask,
                                 bool bound_ctrl)
{
   LLVMTypeRef src_type = LLVMTypeOf(src);
   src = ac_to_integer(ctx, src);
   old = ac_to_integer(ctx, old);
   unsigned bits = LLVMGetIntTypeWidth(LLVMTypeOf(src));
   LLVMValueRef ret;
   if (bits > 32) {
      assert(bits % 32 == 0);
      LLVMTypeRef vec_type = LLVMVectorType(ctx->i32, bits / 32);
      LLVMValueRef src_vector = LLVMBuildBitCast(ctx->builder, src, vec_type, "");
      LLVMValueRef old_vector = LLVMBuildBitCast(ctx->builder, old, vec_type, "");
      ret = LLVMGetUndef(vec_type);
      for (unsigned i = 0; i < bits / 32; i++) {
         src = LLVMBuildExtractElement(ctx->builder, src_vector, LLVMConstInt(ctx->i32, i, 0), "");
         old = LLVMBuildExtractElement(ctx->builder, old_vector, LLVMConstInt(ctx->i32, i, 0), "");
         LLVMValueRef ret_comp =
            _ac_build_dpp(ctx, old, src, dpp_ctrl, row_mask, bank_mask, bound_ctrl);
         ret =
            LLVMBuildInsertElement(ctx->builder, ret, ret_comp, LLVMConstInt(ctx->i32, i, 0), "");
      }
   } else {
      ret = _ac_build_dpp(ctx, old, src, dpp_ctrl, row_mask, bank_mask, bound_ctrl);
   }
   return LLVMBuildBitCast(ctx->builder, ret, src_type, "");
}

static LLVMValueRef _ac_build_permlane16(struct ac_llvm_context *ctx, LLVMValueRef src,
                                         uint64_t sel, bool exchange_rows, bool bound_ctrl)
{
   LLVMTypeRef type = LLVMTypeOf(src);
   LLVMValueRef result;

   src = LLVMBuildZExt(ctx->builder, src, ctx->i32, "");

   LLVMValueRef args[6] = {
      src,
      src,
      LLVMConstInt(ctx->i32, sel, false),
      LLVMConstInt(ctx->i32, sel >> 32, false),
      ctx->i1true, /* fi */
      bound_ctrl ? ctx->i1true : ctx->i1false,
   };

   result =
      ac_build_intrinsic(ctx, exchange_rows ? "llvm.amdgcn.permlanex16" : "llvm.amdgcn.permlane16",
                         ctx->i32, args, 6, AC_FUNC_ATTR_READNONE | AC_FUNC_ATTR_CONVERGENT);

   return LLVMBuildTrunc(ctx->builder, result, type, "");
}

static LLVMValueRef ac_build_permlane16(struct ac_llvm_context *ctx, LLVMValueRef src, uint64_t sel,
                                        bool exchange_rows, bool bound_ctrl)
{
   LLVMTypeRef src_type = LLVMTypeOf(src);
   src = ac_to_integer(ctx, src);
   unsigned bits = LLVMGetIntTypeWidth(LLVMTypeOf(src));
   LLVMValueRef ret;
   if (bits > 32) {
      assert(bits % 32 == 0);
      LLVMTypeRef vec_type = LLVMVectorType(ctx->i32, bits / 32);
      LLVMValueRef src_vector = LLVMBuildBitCast(ctx->builder, src, vec_type, "");
      ret = LLVMGetUndef(vec_type);
      for (unsigned i = 0; i < bits / 32; i++) {
         src = LLVMBuildExtractElement(ctx->builder, src_vector, LLVMConstInt(ctx->i32, i, 0), "");
         LLVMValueRef ret_comp = _ac_build_permlane16(ctx, src, sel, exchange_rows, bound_ctrl);
         ret =
            LLVMBuildInsertElement(ctx->builder, ret, ret_comp, LLVMConstInt(ctx->i32, i, 0), "");
      }
   } else {
      ret = _ac_build_permlane16(ctx, src, sel, exchange_rows, bound_ctrl);
   }
   return LLVMBuildBitCast(ctx->builder, ret, src_type, "");
}

static inline unsigned ds_pattern_bitmode(unsigned and_mask, unsigned or_mask, unsigned xor_mask)
{
   assert(and_mask < 32 && or_mask < 32 && xor_mask < 32);
   return and_mask | (or_mask << 5) | (xor_mask << 10);
}

static LLVMValueRef _ac_build_ds_swizzle(struct ac_llvm_context *ctx, LLVMValueRef src,
                                         unsigned mask)
{
   LLVMTypeRef src_type = LLVMTypeOf(src);
   LLVMValueRef ret;

   src = LLVMBuildZExt(ctx->builder, src, ctx->i32, "");

   ret = ac_build_intrinsic(ctx, "llvm.amdgcn.ds.swizzle", ctx->i32,
                            (LLVMValueRef[]){src, LLVMConstInt(ctx->i32, mask, 0)}, 2,
                            AC_FUNC_ATTR_READNONE | AC_FUNC_ATTR_CONVERGENT);

   return LLVMBuildTrunc(ctx->builder, ret, src_type, "");
}

LLVMValueRef ac_build_ds_swizzle(struct ac_llvm_context *ctx, LLVMValueRef src, unsigned mask)
{
   LLVMTypeRef src_type = LLVMTypeOf(src);
   src = ac_to_integer(ctx, src);
   unsigned bits = LLVMGetIntTypeWidth(LLVMTypeOf(src));
   LLVMValueRef ret;
   if (bits > 32) {
      assert(bits % 32 == 0);
      LLVMTypeRef vec_type = LLVMVectorType(ctx->i32, bits / 32);
      LLVMValueRef src_vector = LLVMBuildBitCast(ctx->builder, src, vec_type, "");
      ret = LLVMGetUndef(vec_type);
      for (unsigned i = 0; i < bits / 32; i++) {
         src = LLVMBuildExtractElement(ctx->builder, src_vector, LLVMConstInt(ctx->i32, i, 0), "");
         LLVMValueRef ret_comp = _ac_build_ds_swizzle(ctx, src, mask);
         ret =
            LLVMBuildInsertElement(ctx->builder, ret, ret_comp, LLVMConstInt(ctx->i32, i, 0), "");
      }
   } else {
      ret = _ac_build_ds_swizzle(ctx, src, mask);
   }
   return LLVMBuildBitCast(ctx->builder, ret, src_type, "");
}

static LLVMValueRef ac_build_wwm(struct ac_llvm_context *ctx, LLVMValueRef src)
{
   LLVMTypeRef src_type = LLVMTypeOf(src);
   unsigned bitsize = ac_get_elem_bits(ctx, src_type);
   char name[32], type[8];
   LLVMValueRef ret;

   src = ac_to_integer(ctx, src);

   if (bitsize < 32)
      src = LLVMBuildZExt(ctx->builder, src, ctx->i32, "");

   ac_build_type_name_for_intr(LLVMTypeOf(src), type, sizeof(type));
   snprintf(name, sizeof(name), "llvm.amdgcn.wwm.%s", type);
   ret = ac_build_intrinsic(ctx, name, LLVMTypeOf(src), (LLVMValueRef[]){src}, 1,
                            AC_FUNC_ATTR_READNONE);

   if (bitsize < 32)
      ret = LLVMBuildTrunc(ctx->builder, ret, ac_to_integer_type(ctx, src_type), "");

   return LLVMBuildBitCast(ctx->builder, ret, src_type, "");
}

static LLVMValueRef ac_build_set_inactive(struct ac_llvm_context *ctx, LLVMValueRef src,
                                          LLVMValueRef inactive)
{
   char name[33], type[8];
   LLVMTypeRef src_type = LLVMTypeOf(src);
   unsigned bitsize = ac_get_elem_bits(ctx, src_type);
   src = ac_to_integer(ctx, src);
   inactive = ac_to_integer(ctx, inactive);

   if (bitsize < 32) {
      src = LLVMBuildZExt(ctx->builder, src, ctx->i32, "");
      inactive = LLVMBuildZExt(ctx->builder, inactive, ctx->i32, "");
   }

   ac_build_type_name_for_intr(LLVMTypeOf(src), type, sizeof(type));
   snprintf(name, sizeof(name), "llvm.amdgcn.set.inactive.%s", type);
   LLVMValueRef ret =
      ac_build_intrinsic(ctx, name, LLVMTypeOf(src), (LLVMValueRef[]){src, inactive}, 2,
                         AC_FUNC_ATTR_READNONE | AC_FUNC_ATTR_CONVERGENT);
   if (bitsize < 32)
      ret = LLVMBuildTrunc(ctx->builder, ret, src_type, "");

   return ret;
}

static LLVMValueRef get_reduction_identity(struct ac_llvm_context *ctx, nir_op op,
                                           unsigned type_size)
{

   if (type_size == 0) {
      switch (op) {
      case nir_op_ior:
      case nir_op_ixor:
         return LLVMConstInt(ctx->i1, 0, 0);
      case nir_op_iand:
         return LLVMConstInt(ctx->i1, 1, 0);
      default:
         unreachable("bad reduction intrinsic");
      }
   } else if (type_size == 1) {
      switch (op) {
      case nir_op_iadd:
         return ctx->i8_0;
      case nir_op_imul:
         return ctx->i8_1;
      case nir_op_imin:
         return LLVMConstInt(ctx->i8, INT8_MAX, 0);
      case nir_op_umin:
         return LLVMConstInt(ctx->i8, UINT8_MAX, 0);
      case nir_op_imax:
         return LLVMConstInt(ctx->i8, INT8_MIN, 0);
      case nir_op_umax:
         return ctx->i8_0;
      case nir_op_iand:
         return LLVMConstInt(ctx->i8, -1, 0);
      case nir_op_ior:
         return ctx->i8_0;
      case nir_op_ixor:
         return ctx->i8_0;
      default:
         unreachable("bad reduction intrinsic");
      }
   } else if (type_size == 2) {
      switch (op) {
      case nir_op_iadd:
         return ctx->i16_0;
      case nir_op_fadd:
         return ctx->f16_0;
      case nir_op_imul:
         return ctx->i16_1;
      case nir_op_fmul:
         return ctx->f16_1;
      case nir_op_imin:
         return LLVMConstInt(ctx->i16, INT16_MAX, 0);
      case nir_op_umin:
         return LLVMConstInt(ctx->i16, UINT16_MAX, 0);
      case nir_op_fmin:
         return LLVMConstReal(ctx->f16, INFINITY);
      case nir_op_imax:
         return LLVMConstInt(ctx->i16, INT16_MIN, 0);
      case nir_op_umax:
         return ctx->i16_0;
      case nir_op_fmax:
         return LLVMConstReal(ctx->f16, -INFINITY);
      case nir_op_iand:
         return LLVMConstInt(ctx->i16, -1, 0);
      case nir_op_ior:
         return ctx->i16_0;
      case nir_op_ixor:
         return ctx->i16_0;
      default:
         unreachable("bad reduction intrinsic");
      }
   } else if (type_size == 4) {
      switch (op) {
      case nir_op_iadd:
         return ctx->i32_0;
      case nir_op_fadd:
         return ctx->f32_0;
      case nir_op_imul:
         return ctx->i32_1;
      case nir_op_fmul:
         return ctx->f32_1;
      case nir_op_imin:
         return LLVMConstInt(ctx->i32, INT32_MAX, 0);
      case nir_op_umin:
         return LLVMConstInt(ctx->i32, UINT32_MAX, 0);
      case nir_op_fmin:
         return LLVMConstReal(ctx->f32, INFINITY);
      case nir_op_imax:
         return LLVMConstInt(ctx->i32, INT32_MIN, 0);
      case nir_op_umax:
         return ctx->i32_0;
      case nir_op_fmax:
         return LLVMConstReal(ctx->f32, -INFINITY);
      case nir_op_iand:
         return LLVMConstInt(ctx->i32, -1, 0);
      case nir_op_ior:
         return ctx->i32_0;
      case nir_op_ixor:
         return ctx->i32_0;
      default:
         unreachable("bad reduction intrinsic");
      }
   } else { /* type_size == 64bit */
      switch (op) {
      case nir_op_iadd:
         return ctx->i64_0;
      case nir_op_fadd:
         return ctx->f64_0;
      case nir_op_imul:
         return ctx->i64_1;
      case nir_op_fmul:
         return ctx->f64_1;
      case nir_op_imin:
         return LLVMConstInt(ctx->i64, INT64_MAX, 0);
      case nir_op_umin:
         return LLVMConstInt(ctx->i64, UINT64_MAX, 0);
      case nir_op_fmin:
         return LLVMConstReal(ctx->f64, INFINITY);
      case nir_op_imax:
         return LLVMConstInt(ctx->i64, INT64_MIN, 0);
      case nir_op_umax:
         return ctx->i64_0;
      case nir_op_fmax:
         return LLVMConstReal(ctx->f64, -INFINITY);
      case nir_op_iand:
         return LLVMConstInt(ctx->i64, -1, 0);
      case nir_op_ior:
         return ctx->i64_0;
      case nir_op_ixor:
         return ctx->i64_0;
      default:
         unreachable("bad reduction intrinsic");
      }
   }
}

static LLVMValueRef ac_build_alu_op(struct ac_llvm_context *ctx, LLVMValueRef lhs, LLVMValueRef rhs,
                                    nir_op op)
{
   bool _64bit = ac_get_type_size(LLVMTypeOf(lhs)) == 8;
   bool _32bit = ac_get_type_size(LLVMTypeOf(lhs)) == 4;
   switch (op) {
   case nir_op_iadd:
      return LLVMBuildAdd(ctx->builder, lhs, rhs, "");
   case nir_op_fadd:
      return LLVMBuildFAdd(ctx->builder, lhs, rhs, "");
   case nir_op_imul:
      return LLVMBuildMul(ctx->builder, lhs, rhs, "");
   case nir_op_fmul:
      return LLVMBuildFMul(ctx->builder, lhs, rhs, "");
   case nir_op_imin:
      return LLVMBuildSelect(ctx->builder, LLVMBuildICmp(ctx->builder, LLVMIntSLT, lhs, rhs, ""),
                             lhs, rhs, "");
   case nir_op_umin:
      return LLVMBuildSelect(ctx->builder, LLVMBuildICmp(ctx->builder, LLVMIntULT, lhs, rhs, ""),
                             lhs, rhs, "");
   case nir_op_fmin:
      return ac_build_intrinsic(
         ctx, _64bit ? "llvm.minnum.f64" : _32bit ? "llvm.minnum.f32" : "llvm.minnum.f16",
         _64bit ? ctx->f64 : _32bit ? ctx->f32 : ctx->f16, (LLVMValueRef[]){lhs, rhs}, 2,
         AC_FUNC_ATTR_READNONE);
   case nir_op_imax:
      return LLVMBuildSelect(ctx->builder, LLVMBuildICmp(ctx->builder, LLVMIntSGT, lhs, rhs, ""),
                             lhs, rhs, "");
   case nir_op_umax:
      return LLVMBuildSelect(ctx->builder, LLVMBuildICmp(ctx->builder, LLVMIntUGT, lhs, rhs, ""),
                             lhs, rhs, "");
   case nir_op_fmax:
      return ac_build_intrinsic(
         ctx, _64bit ? "llvm.maxnum.f64" : _32bit ? "llvm.maxnum.f32" : "llvm.maxnum.f16",
         _64bit ? ctx->f64 : _32bit ? ctx->f32 : ctx->f16, (LLVMValueRef[]){lhs, rhs}, 2,
         AC_FUNC_ATTR_READNONE);
   case nir_op_iand:
      return LLVMBuildAnd(ctx->builder, lhs, rhs, "");
   case nir_op_ior:
      return LLVMBuildOr(ctx->builder, lhs, rhs, "");
   case nir_op_ixor:
      return LLVMBuildXor(ctx->builder, lhs, rhs, "");
   default:
      unreachable("bad reduction intrinsic");
   }
}

/**
 * \param src The value to shift.
 * \param identity The value to use the first lane.
 * \param maxprefix specifies that the result only needs to be correct for a
 *     prefix of this many threads
 * \return src, shifted 1 lane up, and identity shifted into lane 0.
 */
static LLVMValueRef ac_wavefront_shift_right_1(struct ac_llvm_context *ctx, LLVMValueRef src,
                                               LLVMValueRef identity, unsigned maxprefix)
{
   if (ctx->chip_class >= GFX10) {
      /* wavefront shift_right by 1 on GFX10 (emulate dpp_wf_sr1) */
      LLVMValueRef active, tmp1, tmp2;
      LLVMValueRef tid = ac_get_thread_id(ctx);

      tmp1 = ac_build_dpp(ctx, identity, src, dpp_row_sr(1), 0xf, 0xf, false);

      tmp2 = ac_build_permlane16(ctx, src, (uint64_t)~0, true, false);

      if (maxprefix > 32) {
         active =
            LLVMBuildICmp(ctx->builder, LLVMIntEQ, tid, LLVMConstInt(ctx->i32, 32, false), "");

         tmp2 = LLVMBuildSelect(ctx->builder, active,
                                ac_build_readlane(ctx, src, LLVMConstInt(ctx->i32, 31, false)),
                                tmp2, "");

         active = LLVMBuildOr(
            ctx->builder, active,
            LLVMBuildICmp(ctx->builder, LLVMIntEQ,
                          LLVMBuildAnd(ctx->builder, tid, LLVMConstInt(ctx->i32, 0x1f, false), ""),
                          LLVMConstInt(ctx->i32, 0x10, false), ""),
            "");
         return LLVMBuildSelect(ctx->builder, active, tmp2, tmp1, "");
      } else if (maxprefix > 16) {
         active =
            LLVMBuildICmp(ctx->builder, LLVMIntEQ, tid, LLVMConstInt(ctx->i32, 16, false), "");

         return LLVMBuildSelect(ctx->builder, active, tmp2, tmp1, "");
      }
   } else if (ctx->chip_class >= GFX8) {
      return ac_build_dpp(ctx, identity, src, dpp_wf_sr1, 0xf, 0xf, false);
   }

   /* wavefront shift_right by 1 on SI/CI */
   LLVMValueRef active, tmp1, tmp2;
   LLVMValueRef tid = ac_get_thread_id(ctx);
   tmp1 = ac_build_ds_swizzle(ctx, src, (1 << 15) | dpp_quad_perm(0, 0, 1, 2));
   tmp2 = ac_build_ds_swizzle(ctx, src, ds_pattern_bitmode(0x18, 0x03, 0x00));
   active = LLVMBuildICmp(ctx->builder, LLVMIntEQ,
                          LLVMBuildAnd(ctx->builder, tid, LLVMConstInt(ctx->i32, 0x7, 0), ""),
                          LLVMConstInt(ctx->i32, 0x4, 0), "");
   tmp1 = LLVMBuildSelect(ctx->builder, active, tmp2, tmp1, "");
   tmp2 = ac_build_ds_swizzle(ctx, src, ds_pattern_bitmode(0x10, 0x07, 0x00));
   active = LLVMBuildICmp(ctx->builder, LLVMIntEQ,
                          LLVMBuildAnd(ctx->builder, tid, LLVMConstInt(ctx->i32, 0xf, 0), ""),
                          LLVMConstInt(ctx->i32, 0x8, 0), "");
   tmp1 = LLVMBuildSelect(ctx->builder, active, tmp2, tmp1, "");
   tmp2 = ac_build_ds_swizzle(ctx, src, ds_pattern_bitmode(0x00, 0x0f, 0x00));
   active = LLVMBuildICmp(ctx->builder, LLVMIntEQ,
                          LLVMBuildAnd(ctx->builder, tid, LLVMConstInt(ctx->i32, 0x1f, 0), ""),
                          LLVMConstInt(ctx->i32, 0x10, 0), "");
   tmp1 = LLVMBuildSelect(ctx->builder, active, tmp2, tmp1, "");
   tmp2 = ac_build_readlane(ctx, src, LLVMConstInt(ctx->i32, 31, 0));
   active = LLVMBuildICmp(ctx->builder, LLVMIntEQ, tid, LLVMConstInt(ctx->i32, 32, 0), "");
   tmp1 = LLVMBuildSelect(ctx->builder, active, tmp2, tmp1, "");
   active = LLVMBuildICmp(ctx->builder, LLVMIntEQ, tid, LLVMConstInt(ctx->i32, 0, 0), "");
   return LLVMBuildSelect(ctx->builder, active, identity, tmp1, "");
}

/**
 * \param maxprefix specifies that the result only needs to be correct for a
 *     prefix of this many threads
 */
static LLVMValueRef ac_build_scan(struct ac_llvm_context *ctx, nir_op op, LLVMValueRef src,
                                  LLVMValueRef identity, unsigned maxprefix, bool inclusive)
{
   LLVMValueRef result, tmp;

   if (!inclusive)
      src = ac_wavefront_shift_right_1(ctx, src, identity, maxprefix);

   result = src;

   if (ctx->chip_class <= GFX7) {
      assert(maxprefix == 64);
      LLVMValueRef tid = ac_get_thread_id(ctx);
      LLVMValueRef active;
      tmp = ac_build_ds_swizzle(ctx, src, ds_pattern_bitmode(0x1e, 0x00, 0x00));
      active = LLVMBuildICmp(ctx->builder, LLVMIntNE,
                             LLVMBuildAnd(ctx->builder, tid, ctx->i32_1, ""), ctx->i32_0, "");
      tmp = LLVMBuildSelect(ctx->builder, active, tmp, identity, "");
      result = ac_build_alu_op(ctx, result, tmp, op);
      tmp = ac_build_ds_swizzle(ctx, result, ds_pattern_bitmode(0x1c, 0x01, 0x00));
      active = LLVMBuildICmp(ctx->builder, LLVMIntNE,
                             LLVMBuildAnd(ctx->builder, tid, LLVMConstInt(ctx->i32, 2, 0), ""),
                             ctx->i32_0, "");
      tmp = LLVMBuildSelect(ctx->builder, active, tmp, identity, "");
      result = ac_build_alu_op(ctx, result, tmp, op);
      tmp = ac_build_ds_swizzle(ctx, result, ds_pattern_bitmode(0x18, 0x03, 0x00));
      active = LLVMBuildICmp(ctx->builder, LLVMIntNE,
                             LLVMBuildAnd(ctx->builder, tid, LLVMConstInt(ctx->i32, 4, 0), ""),
                             ctx->i32_0, "");
      tmp = LLVMBuildSelect(ctx->builder, active, tmp, identity, "");
      result = ac_build_alu_op(ctx, result, tmp, op);
      tmp = ac_build_ds_swizzle(ctx, result, ds_pattern_bitmode(0x10, 0x07, 0x00));
      active = LLVMBuildICmp(ctx->builder, LLVMIntNE,
                             LLVMBuildAnd(ctx->builder, tid, LLVMConstInt(ctx->i32, 8, 0), ""),
                             ctx->i32_0, "");
      tmp = LLVMBuildSelect(ctx->builder, active, tmp, identity, "");
      result = ac_build_alu_op(ctx, result, tmp, op);
      tmp = ac_build_ds_swizzle(ctx, result, ds_pattern_bitmode(0x00, 0x0f, 0x00));
      active = LLVMBuildICmp(ctx->builder, LLVMIntNE,
                             LLVMBuildAnd(ctx->builder, tid, LLVMConstInt(ctx->i32, 16, 0), ""),
                             ctx->i32_0, "");
      tmp = LLVMBuildSelect(ctx->builder, active, tmp, identity, "");
      result = ac_build_alu_op(ctx, result, tmp, op);
      tmp = ac_build_readlane(ctx, result, LLVMConstInt(ctx->i32, 31, 0));
      active = LLVMBuildICmp(ctx->builder, LLVMIntNE,
                             LLVMBuildAnd(ctx->builder, tid, LLVMConstInt(ctx->i32, 32, 0), ""),
                             ctx->i32_0, "");
      tmp = LLVMBuildSelect(ctx->builder, active, tmp, identity, "");
      result = ac_build_alu_op(ctx, result, tmp, op);
      return result;
   }

   if (maxprefix <= 1)
      return result;
   tmp = ac_build_dpp(ctx, identity, src, dpp_row_sr(1), 0xf, 0xf, false);
   result = ac_build_alu_op(ctx, result, tmp, op);
   if (maxprefix <= 2)
      return result;
   tmp = ac_build_dpp(ctx, identity, src, dpp_row_sr(2), 0xf, 0xf, false);
   result = ac_build_alu_op(ctx, result, tmp, op);
   if (maxprefix <= 3)
      return result;
   tmp = ac_build_dpp(ctx, identity, src, dpp_row_sr(3), 0xf, 0xf, false);
   result = ac_build_alu_op(ctx, result, tmp, op);
   if (maxprefix <= 4)
      return result;
   tmp = ac_build_dpp(ctx, identity, result, dpp_row_sr(4), 0xf, 0xe, false);
   result = ac_build_alu_op(ctx, result, tmp, op);
   if (maxprefix <= 8)
      return result;
   tmp = ac_build_dpp(ctx, identity, result, dpp_row_sr(8), 0xf, 0xc, false);
   result = ac_build_alu_op(ctx, result, tmp, op);
   if (maxprefix <= 16)
      return result;

   if (ctx->chip_class >= GFX10) {
      LLVMValueRef tid = ac_get_thread_id(ctx);
      LLVMValueRef active;

      tmp = ac_build_permlane16(ctx, result, ~(uint64_t)0, true, false);

      active = LLVMBuildICmp(ctx->builder, LLVMIntNE,
                             LLVMBuildAnd(ctx->builder, tid, LLVMConstInt(ctx->i32, 16, false), ""),
                             ctx->i32_0, "");

      tmp = LLVMBuildSelect(ctx->builder, active, tmp, identity, "");

      result = ac_build_alu_op(ctx, result, tmp, op);

      if (maxprefix <= 32)
         return result;

      tmp = ac_build_readlane(ctx, result, LLVMConstInt(ctx->i32, 31, false));

      active = LLVMBuildICmp(ctx->builder, LLVMIntUGE, tid, LLVMConstInt(ctx->i32, 32, false), "");

      tmp = LLVMBuildSelect(ctx->builder, active, tmp, identity, "");

      result = ac_build_alu_op(ctx, result, tmp, op);
      return result;
   }

   tmp = ac_build_dpp(ctx, identity, result, dpp_row_bcast15, 0xa, 0xf, false);
   result = ac_build_alu_op(ctx, result, tmp, op);
   if (maxprefix <= 32)
      return result;
   tmp = ac_build_dpp(ctx, identity, result, dpp_row_bcast31, 0xc, 0xf, false);
   result = ac_build_alu_op(ctx, result, tmp, op);
   return result;
}

LLVMValueRef ac_build_inclusive_scan(struct ac_llvm_context *ctx, LLVMValueRef src, nir_op op)
{
   LLVMValueRef result;

   if (LLVMTypeOf(src) == ctx->i1 && op == nir_op_iadd) {
      LLVMBuilderRef builder = ctx->builder;
      src = LLVMBuildZExt(builder, src, ctx->i32, "");
      result = ac_build_ballot(ctx, src);
      result = ac_build_mbcnt(ctx, result);
      result = LLVMBuildAdd(builder, result, src, "");
      return result;
   }

   ac_build_optimization_barrier(ctx, &src, false);

   LLVMValueRef identity = get_reduction_identity(ctx, op, ac_get_type_size(LLVMTypeOf(src)));
   result = LLVMBuildBitCast(ctx->builder, ac_build_set_inactive(ctx, src, identity),
                             LLVMTypeOf(identity), "");
   result = ac_build_scan(ctx, op, result, identity, ctx->wave_size, true);

   return ac_build_wwm(ctx, result);
}

LLVMValueRef ac_build_exclusive_scan(struct ac_llvm_context *ctx, LLVMValueRef src, nir_op op)
{
   LLVMValueRef result;

   if (LLVMTypeOf(src) == ctx->i1 && op == nir_op_iadd) {
      LLVMBuilderRef builder = ctx->builder;
      src = LLVMBuildZExt(builder, src, ctx->i32, "");
      result = ac_build_ballot(ctx, src);
      result = ac_build_mbcnt(ctx, result);
      return result;
   }

   ac_build_optimization_barrier(ctx, &src, false);

   LLVMValueRef identity = get_reduction_identity(ctx, op, ac_get_type_size(LLVMTypeOf(src)));
   result = LLVMBuildBitCast(ctx->builder, ac_build_set_inactive(ctx, src, identity),
                             LLVMTypeOf(identity), "");
   result = ac_build_scan(ctx, op, result, identity, ctx->wave_size, false);

   return ac_build_wwm(ctx, result);
}

LLVMValueRef ac_build_reduce(struct ac_llvm_context *ctx, LLVMValueRef src, nir_op op,
                             unsigned cluster_size)
{
   if (cluster_size == 1)
      return src;
   ac_build_optimization_barrier(ctx, &src, false);
   LLVMValueRef result, swap;
   LLVMValueRef identity = get_reduction_identity(ctx, op, ac_get_type_size(LLVMTypeOf(src)));
   result = LLVMBuildBitCast(ctx->builder, ac_build_set_inactive(ctx, src, identity),
                             LLVMTypeOf(identity), "");
   swap = ac_build_quad_swizzle(ctx, result, 1, 0, 3, 2);
   result = ac_build_alu_op(ctx, result, swap, op);
   if (cluster_size == 2)
      return ac_build_wwm(ctx, result);

   swap = ac_build_quad_swizzle(ctx, result, 2, 3, 0, 1);
   result = ac_build_alu_op(ctx, result, swap, op);
   if (cluster_size == 4)
      return ac_build_wwm(ctx, result);

   if (ctx->chip_class >= GFX8)
      swap = ac_build_dpp(ctx, identity, result, dpp_row_half_mirror, 0xf, 0xf, false);
   else
      swap = ac_build_ds_swizzle(ctx, result, ds_pattern_bitmode(0x1f, 0, 0x04));
   result = ac_build_alu_op(ctx, result, swap, op);
   if (cluster_size == 8)
      return ac_build_wwm(ctx, result);

   if (ctx->chip_class >= GFX8)
      swap = ac_build_dpp(ctx, identity, result, dpp_row_mirror, 0xf, 0xf, false);
   else
      swap = ac_build_ds_swizzle(ctx, result, ds_pattern_bitmode(0x1f, 0, 0x08));
   result = ac_build_alu_op(ctx, result, swap, op);
   if (cluster_size == 16)
      return ac_build_wwm(ctx, result);

   if (ctx->chip_class >= GFX10)
      swap = ac_build_permlane16(ctx, result, 0, true, false);
   else if (ctx->chip_class >= GFX8 && cluster_size != 32)
      swap = ac_build_dpp(ctx, identity, result, dpp_row_bcast15, 0xa, 0xf, false);
   else
      swap = ac_build_ds_swizzle(ctx, result, ds_pattern_bitmode(0x1f, 0, 0x10));
   result = ac_build_alu_op(ctx, result, swap, op);
   if (cluster_size == 32)
      return ac_build_wwm(ctx, result);

   if (ctx->chip_class >= GFX8) {
      if (ctx->wave_size == 64) {
         if (ctx->chip_class >= GFX10)
            swap = ac_build_readlane(ctx, result, LLVMConstInt(ctx->i32, 31, false));
         else
            swap = ac_build_dpp(ctx, identity, result, dpp_row_bcast31, 0xc, 0xf, false);
         result = ac_build_alu_op(ctx, result, swap, op);
         result = ac_build_readlane(ctx, result, LLVMConstInt(ctx->i32, 63, 0));
      }

      return ac_build_wwm(ctx, result);
   } else {
      swap = ac_build_readlane(ctx, result, ctx->i32_0);
      result = ac_build_readlane(ctx, result, LLVMConstInt(ctx->i32, 32, 0));
      result = ac_build_alu_op(ctx, result, swap, op);
      return ac_build_wwm(ctx, result);
   }
}

/**
 * "Top half" of a scan that reduces per-wave values across an entire
 * workgroup.
 *
 * The source value must be present in the highest lane of the wave, and the
 * highest lane must be live.
 */
void ac_build_wg_wavescan_top(struct ac_llvm_context *ctx, struct ac_wg_scan *ws)
{
   if (ws->maxwaves <= 1)
      return;

   const LLVMValueRef last_lane = LLVMConstInt(ctx->i32, ctx->wave_size - 1, false);
   LLVMBuilderRef builder = ctx->builder;
   LLVMValueRef tid = ac_get_thread_id(ctx);
   LLVMValueRef tmp;

   tmp = LLVMBuildICmp(builder, LLVMIntEQ, tid, last_lane, "");
   ac_build_ifcc(ctx, tmp, 1000);
   LLVMBuildStore(builder, ws->src, LLVMBuildGEP(builder, ws->scratch, &ws->waveidx, 1, ""));
   ac_build_endif(ctx, 1000);
}

/**
 * "Bottom half" of a scan that reduces per-wave values across an entire
 * workgroup.
 *
 * The caller must place a barrier between the top and bottom halves.
 */
void ac_build_wg_wavescan_bottom(struct ac_llvm_context *ctx, struct ac_wg_scan *ws)
{
   const LLVMTypeRef type = LLVMTypeOf(ws->src);
   const LLVMValueRef identity = get_reduction_identity(ctx, ws->op, ac_get_type_size(type));

   if (ws->maxwaves <= 1) {
      ws->result_reduce = ws->src;
      ws->result_inclusive = ws->src;
      ws->result_exclusive = identity;
      return;
   }
   assert(ws->maxwaves <= 32);

   LLVMBuilderRef builder = ctx->builder;
   LLVMValueRef tid = ac_get_thread_id(ctx);
   LLVMBasicBlockRef bbs[2];
   LLVMValueRef phivalues_scan[2];
   LLVMValueRef tmp, tmp2;

   bbs[0] = LLVMGetInsertBlock(builder);
   phivalues_scan[0] = LLVMGetUndef(type);

   if (ws->enable_reduce)
      tmp = LLVMBuildICmp(builder, LLVMIntULT, tid, ws->numwaves, "");
   else if (ws->enable_inclusive)
      tmp = LLVMBuildICmp(builder, LLVMIntULE, tid, ws->waveidx, "");
   else
      tmp = LLVMBuildICmp(builder, LLVMIntULT, tid, ws->waveidx, "");
   ac_build_ifcc(ctx, tmp, 1001);
   {
      tmp = LLVMBuildLoad(builder, LLVMBuildGEP(builder, ws->scratch, &tid, 1, ""), "");

      ac_build_optimization_barrier(ctx, &tmp, false);

      bbs[1] = LLVMGetInsertBlock(builder);
      phivalues_scan[1] = ac_build_scan(ctx, ws->op, tmp, identity, ws->maxwaves, true);
   }
   ac_build_endif(ctx, 1001);

   const LLVMValueRef scan = ac_build_phi(ctx, type, 2, phivalues_scan, bbs);

   if (ws->enable_reduce) {
      tmp = LLVMBuildSub(builder, ws->numwaves, ctx->i32_1, "");
      ws->result_reduce = ac_build_readlane(ctx, scan, tmp);
   }
   if (ws->enable_inclusive)
      ws->result_inclusive = ac_build_readlane(ctx, scan, ws->waveidx);
   if (ws->enable_exclusive) {
      tmp = LLVMBuildSub(builder, ws->waveidx, ctx->i32_1, "");
      tmp = ac_build_readlane(ctx, scan, tmp);
      tmp2 = LLVMBuildICmp(builder, LLVMIntEQ, ws->waveidx, ctx->i32_0, "");
      ws->result_exclusive = LLVMBuildSelect(builder, tmp2, identity, tmp, "");
   }
}

/**
 * Inclusive scan of a per-wave value across an entire workgroup.
 *
 * This implies an s_barrier instruction.
 *
 * Unlike ac_build_inclusive_scan, the caller \em must ensure that all threads
 * of the workgroup are live. (This requirement cannot easily be relaxed in a
 * useful manner because of the barrier in the algorithm.)
 */
void ac_build_wg_wavescan(struct ac_llvm_context *ctx, struct ac_wg_scan *ws)
{
   ac_build_wg_wavescan_top(ctx, ws);
   ac_build_s_barrier(ctx);
   ac_build_wg_wavescan_bottom(ctx, ws);
}

/**
 * "Top half" of a scan that reduces per-thread values across an entire
 * workgroup.
 *
 * All lanes must be active when this code runs.
 */
void ac_build_wg_scan_top(struct ac_llvm_context *ctx, struct ac_wg_scan *ws)
{
   if (ws->enable_exclusive) {
      ws->extra = ac_build_exclusive_scan(ctx, ws->src, ws->op);
      if (LLVMTypeOf(ws->src) == ctx->i1 && ws->op == nir_op_iadd)
         ws->src = LLVMBuildZExt(ctx->builder, ws->src, ctx->i32, "");
      ws->src = ac_build_alu_op(ctx, ws->extra, ws->src, ws->op);
   } else {
      ws->src = ac_build_inclusive_scan(ctx, ws->src, ws->op);
   }

   bool enable_inclusive = ws->enable_inclusive;
   bool enable_exclusive = ws->enable_exclusive;
   ws->enable_inclusive = false;
   ws->enable_exclusive = ws->enable_exclusive || enable_inclusive;
   ac_build_wg_wavescan_top(ctx, ws);
   ws->enable_inclusive = enable_inclusive;
   ws->enable_exclusive = enable_exclusive;
}

/**
 * "Bottom half" of a scan that reduces per-thread values across an entire
 * workgroup.
 *
 * The caller must place a barrier between the top and bottom halves.
 */
void ac_build_wg_scan_bottom(struct ac_llvm_context *ctx, struct ac_wg_scan *ws)
{
   bool enable_inclusive = ws->enable_inclusive;
   bool enable_exclusive = ws->enable_exclusive;
   ws->enable_inclusive = false;
   ws->enable_exclusive = ws->enable_exclusive || enable_inclusive;
   ac_build_wg_wavescan_bottom(ctx, ws);
   ws->enable_inclusive = enable_inclusive;
   ws->enable_exclusive = enable_exclusive;

   /* ws->result_reduce is already the correct value */
   if (ws->enable_inclusive)
      ws->result_inclusive = ac_build_alu_op(ctx, ws->result_inclusive, ws->src, ws->op);
   if (ws->enable_exclusive)
      ws->result_exclusive = ac_build_alu_op(ctx, ws->result_exclusive, ws->extra, ws->op);
}

/**
 * A scan that reduces per-thread values across an entire workgroup.
 *
 * The caller must ensure that all lanes are active when this code runs
 * (WWM is insufficient!), because there is an implied barrier.
 */
void ac_build_wg_scan(struct ac_llvm_context *ctx, struct ac_wg_scan *ws)
{
   ac_build_wg_scan_top(ctx, ws);
   ac_build_s_barrier(ctx);
   ac_build_wg_scan_bottom(ctx, ws);
}

LLVMValueRef ac_build_quad_swizzle(struct ac_llvm_context *ctx, LLVMValueRef src, unsigned lane0,
                                   unsigned lane1, unsigned lane2, unsigned lane3)
{
   unsigned mask = dpp_quad_perm(lane0, lane1, lane2, lane3);
   if (ctx->chip_class >= GFX8) {
      return ac_build_dpp(ctx, src, src, mask, 0xf, 0xf, false);
   } else {
      return ac_build_ds_swizzle(ctx, src, (1 << 15) | mask);
   }
}

LLVMValueRef ac_build_shuffle(struct ac_llvm_context *ctx, LLVMValueRef src, LLVMValueRef index)
{
   LLVMTypeRef type = LLVMTypeOf(src);
   LLVMValueRef result;

   index = LLVMBuildMul(ctx->builder, index, LLVMConstInt(ctx->i32, 4, 0), "");
   src = LLVMBuildZExt(ctx->builder, src, ctx->i32, "");

   result =
      ac_build_intrinsic(ctx, "llvm.amdgcn.ds.bpermute", ctx->i32, (LLVMValueRef[]){index, src}, 2,
                         AC_FUNC_ATTR_READNONE | AC_FUNC_ATTR_CONVERGENT);
   return LLVMBuildTrunc(ctx->builder, result, type, "");
}

LLVMValueRef ac_build_frexp_exp(struct ac_llvm_context *ctx, LLVMValueRef src0, unsigned bitsize)
{
   LLVMTypeRef type;
   char *intr;

   if (bitsize == 16) {
      intr = "llvm.amdgcn.frexp.exp.i16.f16";
      type = ctx->i16;
   } else if (bitsize == 32) {
      intr = "llvm.amdgcn.frexp.exp.i32.f32";
      type = ctx->i32;
   } else {
      intr = "llvm.amdgcn.frexp.exp.i32.f64";
      type = ctx->i32;
   }

   LLVMValueRef params[] = {
      src0,
   };
   return ac_build_intrinsic(ctx, intr, type, params, 1, AC_FUNC_ATTR_READNONE);
}
LLVMValueRef ac_build_frexp_mant(struct ac_llvm_context *ctx, LLVMValueRef src0, unsigned bitsize)
{
   LLVMTypeRef type;
   char *intr;

   if (bitsize == 16) {
      intr = "llvm.amdgcn.frexp.mant.f16";
      type = ctx->f16;
   } else if (bitsize == 32) {
      intr = "llvm.amdgcn.frexp.mant.f32";
      type = ctx->f32;
   } else {
      intr = "llvm.amdgcn.frexp.mant.f64";
      type = ctx->f64;
   }

   LLVMValueRef params[] = {
      src0,
   };
   return ac_build_intrinsic(ctx, intr, type, params, 1, AC_FUNC_ATTR_READNONE);
}

LLVMValueRef ac_build_canonicalize(struct ac_llvm_context *ctx, LLVMValueRef src0, unsigned bitsize)
{
   LLVMTypeRef type;
   char *intr;

   if (bitsize == 16) {
      intr = "llvm.canonicalize.f16";
      type = ctx->f16;
   } else if (bitsize == 32) {
      intr = "llvm.canonicalize.f32";
      type = ctx->f32;
   } else {
      intr = "llvm.canonicalize.f64";
      type = ctx->f64;
   }

   LLVMValueRef params[] = {
      src0,
   };
   return ac_build_intrinsic(ctx, intr, type, params, 1, AC_FUNC_ATTR_READNONE);
}

/*
 * this takes an I,J coordinate pair,
 * and works out the X and Y derivatives.
 * it returns DDX(I), DDX(J), DDY(I), DDY(J).
 */
LLVMValueRef ac_build_ddxy_interp(struct ac_llvm_context *ctx, LLVMValueRef interp_ij)
{
   LLVMValueRef result[4], a;
   unsigned i;

   for (i = 0; i < 2; i++) {
      a = LLVMBuildExtractElement(ctx->builder, interp_ij, LLVMConstInt(ctx->i32, i, false), "");
      result[i] = ac_build_ddxy(ctx, AC_TID_MASK_TOP_LEFT, 1, a);
      result[2 + i] = ac_build_ddxy(ctx, AC_TID_MASK_TOP_LEFT, 2, a);
   }
   return ac_build_gather_values(ctx, result, 4);
}

LLVMValueRef ac_build_load_helper_invocation(struct ac_llvm_context *ctx)
{
   LLVMValueRef result;

   if (LLVM_VERSION_MAJOR >= 13) {
      result = ac_build_intrinsic(ctx, "llvm.amdgcn.live.mask", ctx->i1, NULL, 0,
                                  AC_FUNC_ATTR_READONLY | AC_FUNC_ATTR_INACCESSIBLE_MEM_ONLY);
   } else {
      result = ac_build_intrinsic(ctx, "llvm.amdgcn.ps.live", ctx->i1, NULL, 0,
                                  AC_FUNC_ATTR_READNONE);
   }
   return LLVMBuildNot(ctx->builder, result, "");
}

LLVMValueRef ac_build_is_helper_invocation(struct ac_llvm_context *ctx)
{
   if (!ctx->postponed_kill)
      return ac_build_load_helper_invocation(ctx);

   /* postponed_kill should be NULL on LLVM 13+ */
   assert(LLVM_VERSION_MAJOR < 13);

   /* !(exact && postponed) */
   LLVMValueRef exact =
      ac_build_intrinsic(ctx, "llvm.amdgcn.ps.live", ctx->i1, NULL, 0, AC_FUNC_ATTR_READNONE);

   LLVMValueRef postponed = LLVMBuildLoad(ctx->builder, ctx->postponed_kill, "");
   return LLVMBuildNot(ctx->builder, LLVMBuildAnd(ctx->builder, exact, postponed, ""), "");
}

LLVMValueRef ac_build_call(struct ac_llvm_context *ctx, LLVMValueRef func, LLVMValueRef *args,
                           unsigned num_args)
{
   LLVMValueRef ret = LLVMBuildCall(ctx->builder, func, args, num_args, "");
   LLVMSetInstructionCallConv(ret, LLVMGetFunctionCallConv(func));
   return ret;
}

void ac_export_mrt_z(struct ac_llvm_context *ctx, LLVMValueRef depth, LLVMValueRef stencil,
                     LLVMValueRef samplemask, struct ac_export_args *args)
{
   unsigned mask = 0;
   unsigned format = ac_get_spi_shader_z_format(depth != NULL, stencil != NULL, samplemask != NULL);

   assert(depth || stencil || samplemask);

   memset(args, 0, sizeof(*args));

   args->valid_mask = 1; /* whether the EXEC mask is valid */
   args->done = 1;       /* DONE bit */

   /* Specify the target we are exporting */
   args->target = V_008DFC_SQ_EXP_MRTZ;

   args->compr = 0;                       /* COMP flag */
   args->out[0] = LLVMGetUndef(ctx->f32); /* R, depth */
   args->out[1] = LLVMGetUndef(ctx->f32); /* G, stencil test val[0:7], stencil op val[8:15] */
   args->out[2] = LLVMGetUndef(ctx->f32); /* B, sample mask */
   args->out[3] = LLVMGetUndef(ctx->f32); /* A, alpha to mask */

   if (format == V_028710_SPI_SHADER_UINT16_ABGR) {
      assert(!depth);
      args->compr = 1; /* COMPR flag */

      if (stencil) {
         /* Stencil should be in X[23:16]. */
         stencil = ac_to_integer(ctx, stencil);
         stencil = LLVMBuildShl(ctx->builder, stencil, LLVMConstInt(ctx->i32, 16, 0), "");
         args->out[0] = ac_to_float(ctx, stencil);
         mask |= 0x3;
      }
      if (samplemask) {
         /* SampleMask should be in Y[15:0]. */
         args->out[1] = samplemask;
         mask |= 0xc;
      }
   } else {
      if (depth) {
         args->out[0] = depth;
         mask |= 0x1;
      }
      if (stencil) {
         args->out[1] = stencil;
         mask |= 0x2;
      }
      if (samplemask) {
         args->out[2] = samplemask;
         mask |= 0x4;
      }
   }

   /* GFX6 (except OLAND and HAINAN) has a bug that it only looks
    * at the X writemask component. */
   if (ctx->chip_class == GFX6 && ctx->family != CHIP_OLAND && ctx->family != CHIP_HAINAN)
      mask |= 0x1;

   /* Specify which components to enable */
   args->enabled_channels = mask;
}

/* Send GS Alloc Req message from the first wave of the group to SPI.
 * Message payload is:
 * - bits 0..10: vertices in group
 * - bits 12..22: primitives in group
 */
void ac_build_sendmsg_gs_alloc_req(struct ac_llvm_context *ctx, LLVMValueRef wave_id,
                                   LLVMValueRef vtx_cnt, LLVMValueRef prim_cnt)
{
   LLVMBuilderRef builder = ctx->builder;
   LLVMValueRef tmp;
   bool export_dummy_prim = false;

   /* HW workaround for a GPU hang with 100% culling.
    * We always have to export at least 1 primitive.
    * Export a degenerate triangle using vertex 0 for all 3 vertices.
    */
   if (prim_cnt == ctx->i32_0 && ctx->chip_class == GFX10) {
      assert(vtx_cnt == ctx->i32_0);
      prim_cnt = ctx->i32_1;
      vtx_cnt = ctx->i32_1;
      export_dummy_prim = true;
   }

   ac_build_ifcc(ctx, LLVMBuildICmp(builder, LLVMIntEQ, wave_id, ctx->i32_0, ""), 5020);

   tmp = LLVMBuildShl(builder, prim_cnt, LLVMConstInt(ctx->i32, 12, false), "");
   tmp = LLVMBuildOr(builder, tmp, vtx_cnt, "");
   ac_build_sendmsg(ctx, AC_SENDMSG_GS_ALLOC_REQ, tmp);

   if (export_dummy_prim) {
      struct ac_ngg_prim prim = {0};
      /* The vertex indices are 0,0,0. */
      prim.passthrough = ctx->i32_0;

      struct ac_export_args pos = {0};
      /* The hw culls primitives with NaN. */
      pos.out[0] = pos.out[1] = pos.out[2] = pos.out[3] = LLVMConstReal(ctx->f32, NAN);
      pos.target = V_008DFC_SQ_EXP_POS;
      pos.enabled_channels = 0xf;
      pos.done = true;

      ac_build_ifcc(ctx, LLVMBuildICmp(builder, LLVMIntEQ, ac_get_thread_id(ctx), ctx->i32_0, ""),
                    5021);
      ac_build_export_prim(ctx, &prim);
      ac_build_export(ctx, &pos);
      ac_build_endif(ctx, 5021);
   }

   ac_build_endif(ctx, 5020);
}


LLVMValueRef ac_pack_edgeflags_for_export(struct ac_llvm_context *ctx,
                                          const struct ac_shader_args *args)
{
   /* Use the following trick to extract the edge flags:
    *   extracted = v_and_b32 gs_invocation_id, 0x700 ; get edge flags at bits 8, 9, 10
    *   shifted = v_mul_u32_u24 extracted, 0x80402u   ; shift the bits: 8->9, 9->19, 10->29
    *   result = v_and_b32 shifted, 0x20080200        ; remove garbage
    */
   LLVMValueRef tmp = LLVMBuildAnd(ctx->builder,
                                   ac_get_arg(ctx, args->gs_invocation_id),
                                   LLVMConstInt(ctx->i32, 0x700, 0), "");
   tmp = LLVMBuildMul(ctx->builder, tmp, LLVMConstInt(ctx->i32, 0x80402u, 0), "");
   return LLVMBuildAnd(ctx->builder, tmp, LLVMConstInt(ctx->i32, 0x20080200, 0), "");
}

LLVMValueRef ac_pack_prim_export(struct ac_llvm_context *ctx, const struct ac_ngg_prim *prim)
{
   /* The prim export format is:
    *  - bits 0..8: index 0
    *  - bit 9: edge flag 0
    *  - bits 10..18: index 1
    *  - bit 19: edge flag 1
    *  - bits 20..28: index 2
    *  - bit 29: edge flag 2
    *  - bit 31: null primitive (skip)
    */
   LLVMBuilderRef builder = ctx->builder;
   LLVMValueRef tmp = LLVMBuildZExt(builder, prim->isnull, ctx->i32, "");
   LLVMValueRef result = LLVMBuildShl(builder, tmp, LLVMConstInt(ctx->i32, 31, false), "");
   result = LLVMBuildOr(ctx->builder, result, prim->edgeflags, "");

   for (unsigned i = 0; i < prim->num_vertices; ++i) {
      tmp = LLVMBuildShl(builder, prim->index[i], LLVMConstInt(ctx->i32, 10 * i, false), "");
      result = LLVMBuildOr(builder, result, tmp, "");
   }
   return result;
}

void ac_build_export_prim(struct ac_llvm_context *ctx, const struct ac_ngg_prim *prim)
{
   struct ac_export_args args;

   if (prim->passthrough) {
      args.out[0] = prim->passthrough;
   } else {
      args.out[0] = ac_pack_prim_export(ctx, prim);
   }

   args.out[0] = LLVMBuildBitCast(ctx->builder, args.out[0], ctx->f32, "");
   args.out[1] = LLVMGetUndef(ctx->f32);
   args.out[2] = LLVMGetUndef(ctx->f32);
   args.out[3] = LLVMGetUndef(ctx->f32);

   args.target = V_008DFC_SQ_EXP_PRIM;
   args.enabled_channels = 1;
   args.done = true;
   args.valid_mask = false;
   args.compr = false;

   ac_build_export(ctx, &args);
}

static LLVMTypeRef arg_llvm_type(enum ac_arg_type type, unsigned size, struct ac_llvm_context *ctx)
{
   if (type == AC_ARG_FLOAT) {
      return size == 1 ? ctx->f32 : LLVMVectorType(ctx->f32, size);
   } else if (type == AC_ARG_INT) {
      return size == 1 ? ctx->i32 : LLVMVectorType(ctx->i32, size);
   } else {
      LLVMTypeRef ptr_type;
      switch (type) {
      case AC_ARG_CONST_PTR:
         ptr_type = ctx->i8;
         break;
      case AC_ARG_CONST_FLOAT_PTR:
         ptr_type = ctx->f32;
         break;
      case AC_ARG_CONST_PTR_PTR:
         ptr_type = ac_array_in_const32_addr_space(ctx->i8);
         break;
      case AC_ARG_CONST_DESC_PTR:
         ptr_type = ctx->v4i32;
         break;
      case AC_ARG_CONST_IMAGE_PTR:
         ptr_type = ctx->v8i32;
         break;
      default:
         unreachable("unknown arg type");
      }
      if (size == 1) {
         return ac_array_in_const32_addr_space(ptr_type);
      } else {
         assert(size == 2);
         return ac_array_in_const_addr_space(ptr_type);
      }
   }
}

LLVMValueRef ac_build_main(const struct ac_shader_args *args, struct ac_llvm_context *ctx,
                           enum ac_llvm_calling_convention convention, const char *name,
                           LLVMTypeRef ret_type, LLVMModuleRef module)
{
   LLVMTypeRef arg_types[AC_MAX_ARGS];

   for (unsigned i = 0; i < args->arg_count; i++) {
      arg_types[i] = arg_llvm_type(args->args[i].type, args->args[i].size, ctx);
   }

   LLVMTypeRef main_function_type = LLVMFunctionType(ret_type, arg_types, args->arg_count, 0);

   LLVMValueRef main_function = LLVMAddFunction(module, name, main_function_type);
   LLVMBasicBlockRef main_function_body =
      LLVMAppendBasicBlockInContext(ctx->context, main_function, "main_body");
   LLVMPositionBuilderAtEnd(ctx->builder, main_function_body);

   LLVMSetFunctionCallConv(main_function, convention);
   for (unsigned i = 0; i < args->arg_count; ++i) {
      LLVMValueRef P = LLVMGetParam(main_function, i);

      if (args->args[i].file != AC_ARG_SGPR)
         continue;

      ac_add_function_attr(ctx->context, main_function, i + 1, AC_FUNC_ATTR_INREG);

      if (LLVMGetTypeKind(LLVMTypeOf(P)) == LLVMPointerTypeKind) {
         ac_add_function_attr(ctx->context, main_function, i + 1, AC_FUNC_ATTR_NOALIAS);
         ac_add_attr_dereferenceable(P, UINT64_MAX);
         ac_add_attr_alignment(P, 4);
      }
   }

   ctx->main_function = main_function;

   /* Enable denormals for FP16 and FP64: */
   LLVMAddTargetDependentFunctionAttr(main_function, "denormal-fp-math", "ieee,ieee");
   /* Disable denormals for FP32: */
   LLVMAddTargetDependentFunctionAttr(main_function, "denormal-fp-math-f32",
                                      "preserve-sign,preserve-sign");
   return main_function;
}

void ac_build_s_endpgm(struct ac_llvm_context *ctx)
{
   LLVMTypeRef calltype = LLVMFunctionType(ctx->voidt, NULL, 0, false);
   LLVMValueRef code = LLVMConstInlineAsm(calltype, "s_endpgm", "", true, false);
   LLVMBuildCall(ctx->builder, code, NULL, 0, "");
}

/**
 * Convert triangle strip indices to triangle indices. This is used to decompose
 * triangle strips into triangles.
 */
void ac_build_triangle_strip_indices_to_triangle(struct ac_llvm_context *ctx, LLVMValueRef is_odd,
                                                 LLVMValueRef flatshade_first,
                                                 LLVMValueRef index[3])
{
   LLVMBuilderRef builder = ctx->builder;
   LLVMValueRef out[3];

   /* We need to change the vertex order for odd triangles to get correct
    * front/back facing by swapping 2 vertex indices, but we also have to
    * keep the provoking vertex in the same place.
    *
    * If the first vertex is provoking, swap index 1 and 2.
    * If the last vertex is provoking, swap index 0 and 1.
    */
   out[0] = LLVMBuildSelect(builder, flatshade_first, index[0],
                            LLVMBuildSelect(builder, is_odd, index[1], index[0], ""), "");
   out[1] = LLVMBuildSelect(builder, flatshade_first,
                            LLVMBuildSelect(builder, is_odd, index[2], index[1], ""),
                            LLVMBuildSelect(builder, is_odd, index[0], index[1], ""), "");
   out[2] = LLVMBuildSelect(builder, flatshade_first,
                            LLVMBuildSelect(builder, is_odd, index[1], index[2], ""), index[2], "");
   memcpy(index, out, sizeof(out));
}
