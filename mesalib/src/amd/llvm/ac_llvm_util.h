/*
 * Copyright 2016 Bas Nieuwenhuizen
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

#ifndef AC_LLVM_UTIL_H
#define AC_LLVM_UTIL_H

#include "amd_family.h"
#include <llvm-c/TargetMachine.h>
#include <llvm/Config/llvm-config.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ac_compiler_passes;
struct ac_llvm_context;

enum ac_func_attr
{
   AC_FUNC_ATTR_ALWAYSINLINE = (1 << 0),
   AC_FUNC_ATTR_INREG = (1 << 2),
   AC_FUNC_ATTR_NOALIAS = (1 << 3),
   AC_FUNC_ATTR_NOUNWIND = (1 << 4),
   AC_FUNC_ATTR_READNONE = (1 << 5),
   AC_FUNC_ATTR_READONLY = (1 << 6),
   AC_FUNC_ATTR_WRITEONLY = (1 << 7),
   AC_FUNC_ATTR_INACCESSIBLE_MEM_ONLY = (1 << 8),
   AC_FUNC_ATTR_CONVERGENT = (1 << 9),

   /* Legacy intrinsic that needs attributes on function declarations
    * and they must match the internal LLVM definition exactly, otherwise
    * intrinsic selection fails.
    */
   AC_FUNC_ATTR_LEGACY = (1u << 31),
};

enum ac_target_machine_options
{
   AC_TM_SUPPORTS_SPILL = (1 << 0),
   AC_TM_FORCE_ENABLE_XNACK = (1 << 1),
   AC_TM_FORCE_DISABLE_XNACK = (1 << 2),
   AC_TM_PROMOTE_ALLOCA_TO_SCRATCH = (1 << 3),
   AC_TM_CHECK_IR = (1 << 4),
   AC_TM_ENABLE_GLOBAL_ISEL = (1 << 5),
   AC_TM_CREATE_LOW_OPT = (1 << 6),
   AC_TM_WAVE32 = (1 << 7),
};

enum ac_float_mode
{
   AC_FLOAT_MODE_DEFAULT,
   AC_FLOAT_MODE_DEFAULT_OPENGL,
   AC_FLOAT_MODE_DENORM_FLUSH_TO_ZERO,
};

/* Per-thread persistent LLVM objects. */
struct ac_llvm_compiler {
   LLVMTargetLibraryInfoRef target_library_info;
   LLVMPassManagerRef passmgr;

   /* Default compiler. */
   LLVMTargetMachineRef tm;
   struct ac_compiler_passes *passes;

   /* Wave32 compiler for GFX10. */
   LLVMTargetMachineRef tm_wave32;
   struct ac_compiler_passes *passes_wave32;

   /* Optional compiler for faster compilation with fewer optimizations.
    * LLVM modules can be created with "tm" too. There is no difference.
    */
   LLVMTargetMachineRef low_opt_tm; /* uses -O1 instead of -O2 */
   struct ac_compiler_passes *low_opt_passes;
};

const char *ac_get_llvm_processor_name(enum radeon_family family);
void ac_add_attr_dereferenceable(LLVMValueRef val, uint64_t bytes);
void ac_add_attr_alignment(LLVMValueRef val, uint64_t bytes);
bool ac_is_sgpr_param(LLVMValueRef param);
void ac_add_function_attr(LLVMContextRef ctx, LLVMValueRef function, int attr_idx,
                          enum ac_func_attr attr);
void ac_add_func_attributes(LLVMContextRef ctx, LLVMValueRef function, unsigned attrib_mask);
void ac_dump_module(LLVMModuleRef module);

LLVMValueRef ac_llvm_get_called_value(LLVMValueRef call);
bool ac_llvm_is_function(LLVMValueRef v);
LLVMModuleRef ac_create_module(LLVMTargetMachineRef tm, LLVMContextRef ctx);

LLVMBuilderRef ac_create_builder(LLVMContextRef ctx, enum ac_float_mode float_mode);
void ac_enable_signed_zeros(struct ac_llvm_context *ctx);
void ac_disable_signed_zeros(struct ac_llvm_context *ctx);

void ac_llvm_add_target_dep_function_attr(LLVMValueRef F, const char *name, unsigned value);
void ac_llvm_set_workgroup_size(LLVMValueRef F, unsigned size);

static inline unsigned ac_get_load_intr_attribs(bool can_speculate)
{
   /* READNONE means writes can't affect it, while READONLY means that
    * writes can affect it. */
   return can_speculate ? AC_FUNC_ATTR_READNONE : AC_FUNC_ATTR_READONLY;
}

unsigned ac_count_scratch_private_memory(LLVMValueRef function);

LLVMTargetLibraryInfoRef ac_create_target_library_info(const char *triple);
void ac_dispose_target_library_info(LLVMTargetLibraryInfoRef library_info);
void ac_init_shared_llvm_once(void); /* Do not use directly, use ac_init_llvm_once */
void ac_init_llvm_once(void);

bool ac_init_llvm_compiler(struct ac_llvm_compiler *compiler, enum radeon_family family,
                           enum ac_target_machine_options tm_options);
void ac_destroy_llvm_compiler(struct ac_llvm_compiler *compiler);

struct ac_compiler_passes *ac_create_llvm_passes(LLVMTargetMachineRef tm);
void ac_destroy_llvm_passes(struct ac_compiler_passes *p);
bool ac_compile_module_to_elf(struct ac_compiler_passes *p, LLVMModuleRef module,
                              char **pelf_buffer, size_t *pelf_size);
void ac_llvm_add_barrier_noop_pass(LLVMPassManagerRef passmgr);
void ac_enable_global_isel(LLVMTargetMachineRef tm);

static inline bool ac_has_vec3_support(enum chip_class chip, bool use_format)
{
   if (chip == GFX6 && !use_format) {
      /* GFX6 only supports vec3 with load/store format. */
      return false;
   }

   return LLVM_VERSION_MAJOR >= 9;
}

#ifdef __cplusplus
}
#endif

#endif /* AC_LLVM_UTIL_H */
