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
#include "ac_llvm_util.h"
#include "ac_llvm_build.h"
#include "util/bitscan.h"
#include <llvm-c/Core.h>
#include <llvm-c/Support.h>
#include "c11/threads.h"
#include "util/u_math.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void ac_init_llvm_target()
{
	LLVMInitializeAMDGPUTargetInfo();
	LLVMInitializeAMDGPUTarget();
	LLVMInitializeAMDGPUTargetMC();
	LLVMInitializeAMDGPUAsmPrinter();

	/* For inline assembly. */
	LLVMInitializeAMDGPUAsmParser();

	/* Workaround for bug in llvm 4.0 that causes image intrinsics
	 * to disappear.
	 * https://reviews.llvm.org/D26348
	 *
	 * "mesa" is the prefix for error messages.
	 */
	const char *argv[2] = { "mesa", "-simplifycfg-sink-common=false" };
	LLVMParseCommandLineOptions(2, argv, NULL);
}

static once_flag ac_init_llvm_target_once_flag = ONCE_FLAG_INIT;

LLVMTargetRef ac_get_llvm_target(const char *triple)
{
	LLVMTargetRef target = NULL;
	char *err_message = NULL;

	call_once(&ac_init_llvm_target_once_flag, ac_init_llvm_target);

	if (LLVMGetTargetFromTriple(triple, &target, &err_message)) {
		fprintf(stderr, "Cannot find target for triple %s ", triple);
		if (err_message) {
			fprintf(stderr, "%s\n", err_message);
		}
		LLVMDisposeMessage(err_message);
		return NULL;
	}
	return target;
}

const char *ac_get_llvm_processor_name(enum radeon_family family)
{
	switch (family) {
	case CHIP_TAHITI:
		return "tahiti";
	case CHIP_PITCAIRN:
		return "pitcairn";
	case CHIP_VERDE:
		return "verde";
	case CHIP_OLAND:
		return "oland";
	case CHIP_HAINAN:
		return "hainan";
	case CHIP_BONAIRE:
		return "bonaire";
	case CHIP_KABINI:
		return "kabini";
	case CHIP_KAVERI:
		return "kaveri";
	case CHIP_HAWAII:
		return "hawaii";
	case CHIP_MULLINS:
		return "mullins";
	case CHIP_TONGA:
		return "tonga";
	case CHIP_ICELAND:
		return "iceland";
	case CHIP_CARRIZO:
		return "carrizo";
	case CHIP_FIJI:
		return "fiji";
	case CHIP_STONEY:
		return "stoney";
	case CHIP_POLARIS10:
		return "polaris10";
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		return "polaris11";
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_RAVEN:
		return "gfx900";
	default:
		return "";
	}
}

LLVMTargetMachineRef ac_create_target_machine(enum radeon_family family, enum ac_target_machine_options tm_options)
{
	assert(family >= CHIP_TAHITI);
	char features[256];
	const char *triple = (tm_options & AC_TM_SUPPORTS_SPILL) ? "amdgcn-mesa-mesa3d" : "amdgcn--";
	LLVMTargetRef target = ac_get_llvm_target(triple);

	snprintf(features, sizeof(features),
		 "+DumpCode,+vgpr-spilling,-fp32-denormals,+fp64-denormals%s%s%s%s",
		 tm_options & AC_TM_SISCHED ? ",+si-scheduler" : "",
		 tm_options & AC_TM_FORCE_ENABLE_XNACK ? ",+xnack" : "",
		 tm_options & AC_TM_FORCE_DISABLE_XNACK ? ",-xnack" : "",
		 tm_options & AC_TM_PROMOTE_ALLOCA_TO_SCRATCH ? ",-promote-alloca" : "");
	
	LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
	                             target,
	                             triple,
	                             ac_get_llvm_processor_name(family),
				     features,
	                             LLVMCodeGenLevelDefault,
	                             LLVMRelocDefault,
	                             LLVMCodeModelDefault);

	return tm;
}

static const char *attr_to_str(enum ac_func_attr attr)
{
   switch (attr) {
   case AC_FUNC_ATTR_ALWAYSINLINE: return "alwaysinline";
   case AC_FUNC_ATTR_INREG: return "inreg";
   case AC_FUNC_ATTR_NOALIAS: return "noalias";
   case AC_FUNC_ATTR_NOUNWIND: return "nounwind";
   case AC_FUNC_ATTR_READNONE: return "readnone";
   case AC_FUNC_ATTR_READONLY: return "readonly";
   case AC_FUNC_ATTR_WRITEONLY: return "writeonly";
   case AC_FUNC_ATTR_INACCESSIBLE_MEM_ONLY: return "inaccessiblememonly";
   case AC_FUNC_ATTR_CONVERGENT: return "convergent";
   default:
	   fprintf(stderr, "Unhandled function attribute: %x\n", attr);
	   return 0;
   }
}

void
ac_add_function_attr(LLVMContextRef ctx, LLVMValueRef function,
                     int attr_idx, enum ac_func_attr attr)
{
   const char *attr_name = attr_to_str(attr);
   unsigned kind_id = LLVMGetEnumAttributeKindForName(attr_name,
                                                      strlen(attr_name));
   LLVMAttributeRef llvm_attr = LLVMCreateEnumAttribute(ctx, kind_id, 0);

   if (LLVMIsAFunction(function))
      LLVMAddAttributeAtIndex(function, attr_idx, llvm_attr);
   else
      LLVMAddCallSiteAttribute(function, attr_idx, llvm_attr);
}

void ac_add_func_attributes(LLVMContextRef ctx, LLVMValueRef function,
			    unsigned attrib_mask)
{
	attrib_mask |= AC_FUNC_ATTR_NOUNWIND;
	attrib_mask &= ~AC_FUNC_ATTR_LEGACY;

	while (attrib_mask) {
		enum ac_func_attr attr = 1u << u_bit_scan(&attrib_mask);
		ac_add_function_attr(ctx, function, -1, attr);
	}
}

void
ac_dump_module(LLVMModuleRef module)
{
	char *str = LLVMPrintModuleToString(module);
	fprintf(stderr, "%s", str);
	LLVMDisposeMessage(str);
}

void
ac_llvm_add_target_dep_function_attr(LLVMValueRef F,
				     const char *name, unsigned value)
{
	char str[16];

	snprintf(str, sizeof(str), "0x%x", value);
	LLVMAddTargetDependentFunctionAttr(F, name, str);
}

unsigned
ac_count_scratch_private_memory(LLVMValueRef function)
{
	unsigned private_mem_vgprs = 0;

	/* Process all LLVM instructions. */
	LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
	while (bb) {
		LLVMValueRef next = LLVMGetFirstInstruction(bb);

		while (next) {
			LLVMValueRef inst = next;
			next = LLVMGetNextInstruction(next);

			if (LLVMGetInstructionOpcode(inst) != LLVMAlloca)
				continue;

			LLVMTypeRef type = LLVMGetElementType(LLVMTypeOf(inst));
			/* No idea why LLVM aligns allocas to 4 elements. */
			unsigned alignment = LLVMGetAlignment(inst);
			unsigned dw_size = align(ac_get_type_size(type) / 4, alignment);
			private_mem_vgprs += dw_size;
		}
		bb = LLVMGetNextBasicBlock(bb);
	}

	return private_mem_vgprs;
}
