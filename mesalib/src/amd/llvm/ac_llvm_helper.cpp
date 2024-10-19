/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <llvm-c/Core.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/CodeGen/Passes.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/IPO/AlwaysInliner.h>
#include <llvm/Transforms/IPO/SCCP.h>
#include <llvm/Transforms/Scalar/EarlyCSE.h>
#include <llvm/Transforms/Scalar/LICM.h>
#include <llvm/Transforms/Scalar/SROA.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include "llvm/CodeGen/SelectionDAGNodes.h"

#include <cstring>

/* DO NOT REORDER THE HEADERS
 * The LLVM headers need to all be included before any Mesa header,
 * as they use the `restrict` keyword in ways that are incompatible
 * with our #define in include/c99_compat.h
 */

#include "ac_binary.h"
#include "ac_llvm_util.h"
#include "ac_llvm_build.h"
#include "util/macros.h"

using namespace llvm;

class RunAtExitForStaticDestructors : public SDNode
{
public:
   /* getSDVTList (protected) calls getValueTypeList (private), which contains static variables. */
   RunAtExitForStaticDestructors(): SDNode(0, 0, DebugLoc(), getSDVTList(MVT::Other))
   {
   }
};

void ac_llvm_run_atexit_for_destructors(void)
{
   /* LLVM >= 16 registers static variable destructors on the first compile, which gcc
    * implements by calling atexit there. Before that, u_queue registers its atexit
    * handler to kill all threads. Since exit() runs atexit handlers in the reverse order,
    * the LLVM destructors are called first while shader compiler threads may still be
    * running, which crashes in LLVM in SelectionDAG.cpp.
    *
    * The solution is to run the code that declares the LLVM static variables first,
    * so that atexit for LLVM is registered first and u_queue is registered after that,
    * which ensures that all u_queue threads are terminated before LLVM destructors are
    * called.
    *
    * This just executes the code that declares static variables.
    */
   RunAtExitForStaticDestructors();
}

bool ac_is_llvm_processor_supported(LLVMTargetMachineRef tm, const char *processor)
{
   TargetMachine *TM = reinterpret_cast<TargetMachine *>(tm);
   return TM->getMCSubtargetInfo()->isCPUStringValid(processor);
}

void ac_reset_llvm_all_options_occurrences()
{
   cl::ResetAllOptionOccurrences();
}

void ac_add_attr_dereferenceable(LLVMValueRef val, uint64_t bytes)
{
   Argument *A = unwrap<Argument>(val);
   A->addAttr(Attribute::getWithDereferenceableBytes(A->getContext(), bytes));
}

void ac_add_attr_alignment(LLVMValueRef val, uint64_t bytes)
{
   Argument *A = unwrap<Argument>(val);
   A->addAttr(Attribute::getWithAlignment(A->getContext(), Align(bytes)));
}

bool ac_is_sgpr_param(LLVMValueRef arg)
{
   Argument *A = unwrap<Argument>(arg);
   AttributeList AS = A->getParent()->getAttributes();
   unsigned ArgNo = A->getArgNo();
   return AS.hasParamAttr(ArgNo, Attribute::InReg);
}

LLVMModuleRef ac_create_module(LLVMTargetMachineRef tm, LLVMContextRef ctx)
{
   TargetMachine *TM = reinterpret_cast<TargetMachine *>(tm);
   LLVMModuleRef module = LLVMModuleCreateWithNameInContext("mesa-shader", ctx);

   unwrap(module)->setTargetTriple(TM->getTargetTriple().getTriple());
   unwrap(module)->setDataLayout(TM->createDataLayout());
   return module;
}

LLVMBuilderRef ac_create_builder(LLVMContextRef ctx, enum ac_float_mode float_mode)
{
   LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);

   FastMathFlags flags;

   switch (float_mode) {
   case AC_FLOAT_MODE_DEFAULT:
   case AC_FLOAT_MODE_DENORM_FLUSH_TO_ZERO:
      break;

   case AC_FLOAT_MODE_DEFAULT_OPENGL:
      /* Allow optimizations to treat the sign of a zero argument or
       * result as insignificant.
       */
      flags.setNoSignedZeros(); /* nsz */

      /* Allow optimizations to use the reciprocal of an argument
       * rather than perform division.
       */
      flags.setAllowReciprocal(); /* arcp */

      unwrap(builder)->setFastMathFlags(flags);
      break;
   }

   return builder;
}

void ac_enable_signed_zeros(struct ac_llvm_context *ctx)
{
   if (ctx->float_mode == AC_FLOAT_MODE_DEFAULT_OPENGL) {
      auto *b = unwrap(ctx->builder);
      FastMathFlags flags = b->getFastMathFlags();

      /* This disables the optimization of (x + 0), which is used
       * to convert negative zero to positive zero.
       */
      flags.setNoSignedZeros(false);
      b->setFastMathFlags(flags);
   }
}

void ac_disable_signed_zeros(struct ac_llvm_context *ctx)
{
   if (ctx->float_mode == AC_FLOAT_MODE_DEFAULT_OPENGL) {
      auto *b = unwrap(ctx->builder);
      FastMathFlags flags = b->getFastMathFlags();

      flags.setNoSignedZeros();
      b->setFastMathFlags(flags);
   }
}

/* Implementation of raw_pwrite_stream that works on malloc()ed memory for
 * better compatibility with C code. */
struct raw_memory_ostream : public raw_pwrite_stream {
   char *buffer;
   size_t written;
   size_t bufsize;

   raw_memory_ostream()
   {
      buffer = NULL;
      written = 0;
      bufsize = 0;
      SetUnbuffered();
   }

   ~raw_memory_ostream()
   {
      free(buffer);
   }

   void clear()
   {
      written = 0;
   }

   void take(char *&out_buffer, size_t &out_size)
   {
      out_buffer = buffer;
      out_size = written;
      buffer = NULL;
      written = 0;
      bufsize = 0;
   }

   void flush() = delete;

   void write_impl(const char *ptr, size_t size) override
   {
      if (unlikely(written + size < written))
         abort();
      if (written + size > bufsize) {
         bufsize = MAX3(1024, written + size, bufsize / 3 * 4);
         buffer = (char *)realloc(buffer, bufsize);
         if (!buffer) {
            fprintf(stderr, "amd: out of memory allocating ELF buffer\n");
            abort();
         }
      }
      memcpy(buffer + written, ptr, size);
      written += size;
   }

   void pwrite_impl(const char *ptr, size_t size, uint64_t offset) override
   {
      assert(offset == (size_t)offset && offset + size >= offset && offset + size <= written);
      memcpy(buffer + offset, ptr, size);
   }

   uint64_t current_pos() const override
   {
      return written;
   }
};

/* The middle-end optimization passes are run using
 * the LLVM's new pass manager infrastructure.
 */
struct ac_midend_optimizer
{
   TargetMachine *target_machine;
   PassBuilder pass_builder;
   TargetLibraryInfoImpl target_library_info;

   /* Should be declared in this order only,
    * so that they are destroyed in the correct order
    * due to inter-analysis-manager references.
    */
   LoopAnalysisManager loop_am;
   FunctionAnalysisManager function_am;
   CGSCCAnalysisManager cgscc_am;
   ModuleAnalysisManager module_am;

   /* Pass Managers */
   LoopPassManager loop_pm;
   FunctionPassManager function_pm;
   ModulePassManager module_pm;

   ac_midend_optimizer(TargetMachine *arg_target_machine, bool arg_check_ir)
      : target_machine(arg_target_machine),
        pass_builder(target_machine, PipelineTuningOptions(), {}),
        target_library_info(Triple(target_machine->getTargetTriple()))
   {
      /* Build the pipeline and optimize.
       * Any custom analyses should be registered
       * before LLVM's default analysis sets.
       */
      function_am.registerPass(
         [&] { return TargetLibraryAnalysis(target_library_info); }
      );

      pass_builder.registerModuleAnalyses(module_am);
      pass_builder.registerCGSCCAnalyses(cgscc_am);
      pass_builder.registerFunctionAnalyses(function_am);
      pass_builder.registerLoopAnalyses(loop_am);
      pass_builder.crossRegisterProxies(loop_am, function_am, cgscc_am, module_am);

      if (arg_check_ir)
         module_pm.addPass(VerifierPass());

      /* Adding inliner pass to the module pass manager directly
       * ensures that the pass is run on all functions first, which makes sure
       * that the following passes are only run on the remaining non-inline
       * function, so it removes useless work done on dead inline functions.
       */
      module_pm.addPass(AlwaysInlinerPass());

      /* The following set of passes run on an individual function/loop first
       * before proceeding to the next.
       */
#if LLVM_VERSION_MAJOR >= 16
      function_pm.addPass(SROAPass(SROAOptions::ModifyCFG));
#else
      // Old version of the code
      function_pm.addPass(SROAPass());
#endif

      loop_pm.addPass(LICMPass(LICMOptions()));
      function_pm.addPass(createFunctionToLoopPassAdaptor(std::move(loop_pm), true));
      function_pm.addPass(SimplifyCFGPass());
      function_pm.addPass(EarlyCSEPass(true));

      module_pm.addPass(createModuleToFunctionPassAdaptor(std::move(function_pm)));
   }

   void run(Module &module)
   {
      module_pm.run(module, module_am);

      /* After a run(), the results in the analyses managers
       * aren't useful to optimize a subsequent LLVM module.
       * If used, it can lead to unexpected crashes.
       * Hence, the results in the analyses managers
       * need to be invalidated and cleared before
       * running optimizations on a new LLVM module.
       */
      module_am.invalidate(module, PreservedAnalyses::none());
      module_am.clear();
      cgscc_am.clear();
      function_am.clear();
      loop_am.clear();
   }
};

/* The backend passes for optimizations, instruction selection,
 * and code generation in the LLVM compiler still requires the
 * legacy::PassManager. The use of the legacy PM will be
 * deprecated when the new PM can handle backend passes.
 */
struct ac_backend_optimizer
{
   raw_memory_ostream ostream; /* ELF shader binary stream */
   legacy::PassManager backend_pass_manager; /* for codegen only */

   ac_backend_optimizer(TargetMachine *arg_target_machine)
   {
      /* add backend passes */
      if (arg_target_machine->addPassesToEmitFile(backend_pass_manager, ostream, nullptr,
#if LLVM_VERSION_MAJOR >= 18
                                             CodeGenFileType::ObjectFile)) {
#else
                                             CGFT_ObjectFile)) {
#endif
         fprintf(stderr, "amd: TargetMachine can't emit a file of this type!\n");
      }
   }

   void run(Module &module, char *&out_buffer, size_t &out_size)
   {
      backend_pass_manager.run(module);
      ostream.take(out_buffer, out_size);
   }
};

ac_midend_optimizer *ac_create_midend_optimizer(LLVMTargetMachineRef tm,
                                                bool check_ir)
{
   TargetMachine *TM = reinterpret_cast<TargetMachine *>(tm);
   return new ac_midend_optimizer(TM, check_ir);
}

void ac_destroy_midend_optimiser(ac_midend_optimizer *meo)
{
   delete meo;
}

bool ac_llvm_optimize_module(ac_midend_optimizer *meo, LLVMModuleRef module)
{
   if (!meo)
      return false;

   /* Runs all the middle-end optimizations, no code generation */
   meo->run(*unwrap(module));
   return true;
}

ac_backend_optimizer *ac_create_backend_optimizer(LLVMTargetMachineRef tm)
{
   TargetMachine *TM = reinterpret_cast<TargetMachine *>(tm);
   return new ac_backend_optimizer(TM);
}

void ac_destroy_backend_optimizer(ac_backend_optimizer *beo)
{
   delete beo;
}

bool ac_compile_module_to_elf(ac_backend_optimizer *beo, LLVMModuleRef module,
                              char **pelf_buffer, size_t *pelf_size)
{
   if (!beo)
      return false;

   /* Runs all backend optimizations and code generation */
   beo->run(*unwrap(module), *pelf_buffer, *pelf_size);
   return true;
}

LLVMValueRef ac_build_atomic_rmw(struct ac_llvm_context *ctx, LLVMAtomicRMWBinOp op,
                                 LLVMValueRef ptr, LLVMValueRef val, const char *sync_scope)
{
   AtomicRMWInst::BinOp binop;
   switch (op) {
   case LLVMAtomicRMWBinOpXchg:
      binop = AtomicRMWInst::Xchg;
      break;
   case LLVMAtomicRMWBinOpAdd:
      binop = AtomicRMWInst::Add;
      break;
   case LLVMAtomicRMWBinOpSub:
      binop = AtomicRMWInst::Sub;
      break;
   case LLVMAtomicRMWBinOpAnd:
      binop = AtomicRMWInst::And;
      break;
   case LLVMAtomicRMWBinOpNand:
      binop = AtomicRMWInst::Nand;
      break;
   case LLVMAtomicRMWBinOpOr:
      binop = AtomicRMWInst::Or;
      break;
   case LLVMAtomicRMWBinOpXor:
      binop = AtomicRMWInst::Xor;
      break;
   case LLVMAtomicRMWBinOpMax:
      binop = AtomicRMWInst::Max;
      break;
   case LLVMAtomicRMWBinOpMin:
      binop = AtomicRMWInst::Min;
      break;
   case LLVMAtomicRMWBinOpUMax:
      binop = AtomicRMWInst::UMax;
      break;
   case LLVMAtomicRMWBinOpUMin:
      binop = AtomicRMWInst::UMin;
      break;
   case LLVMAtomicRMWBinOpFAdd:
      binop = AtomicRMWInst::FAdd;
      break;
   default:
      unreachable("invalid LLVMAtomicRMWBinOp");
      break;
   }
   unsigned SSID = unwrap(ctx->context)->getOrInsertSyncScopeID(sync_scope);
   return wrap(unwrap(ctx->builder)
                        ->CreateAtomicRMW(binop, unwrap(ptr), unwrap(val),
                                          MaybeAlign(0),
                                          AtomicOrdering::SequentiallyConsistent, SSID));
}

LLVMValueRef ac_build_atomic_cmp_xchg(struct ac_llvm_context *ctx, LLVMValueRef ptr,
                                      LLVMValueRef cmp, LLVMValueRef val, const char *sync_scope)
{
   unsigned SSID = unwrap(ctx->context)->getOrInsertSyncScopeID(sync_scope);
   return wrap(unwrap(ctx->builder)
                        ->CreateAtomicCmpXchg(unwrap(ptr), unwrap(cmp),
                                              unwrap(val),
                                              MaybeAlign(0),
                                              AtomicOrdering::SequentiallyConsistent,
                                              AtomicOrdering::SequentiallyConsistent, SSID));
}
