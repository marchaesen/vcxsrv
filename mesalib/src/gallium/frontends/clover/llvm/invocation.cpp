//
// Copyright 2012-2016 Francisco Jerez
// Copyright 2012-2016 Advanced Micro Devices, Inc.
// Copyright 2014-2016 Jan Vesely
// Copyright 2014-2015 Serge Martin
// Copyright 2015 Zoltan Gilian
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//

#include <llvm/IR/DiagnosticPrinter.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm-c/Target.h>
#ifdef HAVE_CLOVER_SPIRV
#include <LLVMSPIRVLib/LLVMSPIRVLib.h>
#endif

#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Frontend/TextDiagnosticBuffer.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Basic/TargetInfo.h>

// We need to include internal headers last, because the internal headers
// include CL headers which have #define's like:
//
//#define cl_khr_gl_sharing 1
//#define cl_khr_icd 1
//
// Which will break the compilation of clang/Basic/OpenCLOptions.h

#include "core/error.hpp"
#include "llvm/codegen.hpp"
#include "llvm/compat.hpp"
#include "llvm/invocation.hpp"
#include "llvm/metadata.hpp"
#include "llvm/util.hpp"
#ifdef HAVE_CLOVER_SPIRV
#include "spirv/invocation.hpp"
#endif
#include "util/algorithm.hpp"


using namespace clover;
using namespace clover::llvm;

using ::llvm::Function;
using ::llvm::LLVMContext;
using ::llvm::Module;
using ::llvm::raw_string_ostream;

namespace {

    struct cl_version {
        std::string version_str; // CL Version
        unsigned version_number; // Numeric CL Version
    };

   static const unsigned ANY_VERSION = 999;
   const cl_version cl_versions[] = {
      { "1.0", 100},
      { "1.1", 110},
      { "1.2", 120},
      { "2.0", 200},
      { "2.1", 210},
      { "2.2", 220},
   };

    struct clc_version_lang_std {
        unsigned version_number; // CLC Version
        clang::LangStandard::Kind clc_lang_standard;
    };

    const clc_version_lang_std cl_version_lang_stds[] = {
       { 100, compat::lang_opencl10},
       { 110, clang::LangStandard::lang_opencl11},
       { 120, clang::LangStandard::lang_opencl12},
       { 200, clang::LangStandard::lang_opencl20},
    };

   void
   init_targets() {
      static bool targets_initialized = false;
      if (!targets_initialized) {
         LLVMInitializeAllTargets();
         LLVMInitializeAllTargetInfos();
         LLVMInitializeAllTargetMCs();
         LLVMInitializeAllAsmParsers();
         LLVMInitializeAllAsmPrinters();
         targets_initialized = true;
      }
   }

   void
   diagnostic_handler(const ::llvm::DiagnosticInfo &di, void *data) {
      if (di.getSeverity() == ::llvm::DS_Error) {
         raw_string_ostream os { *reinterpret_cast<std::string *>(data) };
         ::llvm::DiagnosticPrinterRawOStream printer { os };
         di.print(printer);
         throw build_error();
      }
   }

   std::unique_ptr<LLVMContext>
   create_context(std::string &r_log) {
      init_targets();
      std::unique_ptr<LLVMContext> ctx { new LLVMContext };
      compat::set_diagnostic_handler(*ctx, diagnostic_handler, &r_log);
      return ctx;
   }

   const struct clc_version_lang_std&
   get_cl_lang_standard(unsigned requested, unsigned max = ANY_VERSION) {
       for (const struct clc_version_lang_std &version : cl_version_lang_stds) {
           if (version.version_number == max ||
                   version.version_number == requested) {
               return version;
           }
       }
       throw build_error("Unknown/Unsupported language version");
   }

   const struct cl_version&
   get_cl_version(const std::string &version_str,
                  unsigned max = ANY_VERSION) {
      for (const struct cl_version &version : cl_versions) {
         if (version.version_number == max || version.version_str == version_str) {
            return version;
         }
      }
      throw build_error("Unknown/Unsupported language version");
   }

   clang::LangStandard::Kind
   get_lang_standard_from_version_str(const std::string &version_str,
                                      bool is_build_opt = false) {

       //Per CL 2.0 spec, section 5.8.4.5:
       //  If it's an option, use the value directly.
       //  If it's a device version, clamp to max 1.x version, a.k.a. 1.2
      const cl_version version =
         get_cl_version(version_str, is_build_opt ? ANY_VERSION : 120);

      const struct clc_version_lang_std standard =
         get_cl_lang_standard(version.version_number);

      return standard.clc_lang_standard;
   }

   clang::LangStandard::Kind
   get_language_version(const std::vector<std::string> &opts,
                        const std::string &device_version) {

      const std::string search = "-cl-std=CL";

      for (auto &opt: opts) {
         auto pos = opt.find(search);
         if (pos == 0){
            const auto ver = opt.substr(pos + search.size());
            const auto device_ver = get_cl_version(device_version);
            const auto requested = get_cl_version(ver);
            if (requested.version_number > device_ver.version_number) {
               throw build_error();
            }
            return get_lang_standard_from_version_str(ver, true);
         }
      }

      return get_lang_standard_from_version_str(device_version);
   }

   std::unique_ptr<clang::CompilerInstance>
   create_compiler_instance(const device &dev, const std::string& ir_target,
                            const std::vector<std::string> &opts,
                            std::string &r_log) {
      std::unique_ptr<clang::CompilerInstance> c { new clang::CompilerInstance };
      clang::TextDiagnosticBuffer *diag_buffer = new clang::TextDiagnosticBuffer;
      clang::DiagnosticsEngine diag { new clang::DiagnosticIDs,
            new clang::DiagnosticOptions, diag_buffer };

      // Parse the compiler options.  A file name should be present at the end
      // and must have the .cl extension in order for the CompilerInvocation
      // class to recognize it as an OpenCL source file.
      const std::vector<const char *> copts =
         map(std::mem_fn(&std::string::c_str), opts);

      const target &target = ir_target;
      const std::string &device_clc_version = dev.device_clc_version();

      if (!compat::create_compiler_invocation_from_args(
             c->getInvocation(), copts, diag))
         throw invalid_build_options_error();

      diag_buffer->FlushDiagnostics(diag);
      if (diag.hasErrorOccurred())
         throw invalid_build_options_error();

      c->getTargetOpts().CPU = target.cpu;
      c->getTargetOpts().Triple = target.triple;
      c->getLangOpts().NoBuiltin = true;

      // This is a workaround for a Clang bug which causes the number
      // of warnings and errors to be printed to stderr.
      // http://www.llvm.org/bugs/show_bug.cgi?id=19735
      c->getDiagnosticOpts().ShowCarets = false;

      c->getInvocation().setLangDefaults(c->getLangOpts(),
                                compat::ik_opencl, ::llvm::Triple(target.triple),
                                c->getPreprocessorOpts(),
                                get_language_version(opts, device_clc_version));

      c->createDiagnostics(new clang::TextDiagnosticPrinter(
                              *new raw_string_ostream(r_log),
                              &c->getDiagnosticOpts(), true));

      c->setTarget(clang::TargetInfo::CreateTargetInfo(
                           c->getDiagnostics(), c->getInvocation().TargetOpts));

      return c;
   }

   std::unique_ptr<Module>
   compile(LLVMContext &ctx, clang::CompilerInstance &c,
           const std::string &name, const std::string &source,
           const header_map &headers, const device &dev,
           const std::string &opts, bool use_libclc, std::string &r_log) {
      c.getFrontendOpts().ProgramAction = clang::frontend::EmitLLVMOnly;
      c.getHeaderSearchOpts().UseBuiltinIncludes = true;
      c.getHeaderSearchOpts().UseStandardSystemIncludes = true;
      c.getHeaderSearchOpts().ResourceDir = CLANG_RESOURCE_DIR;

      if (use_libclc) {
         // Add libclc generic search path
         c.getHeaderSearchOpts().AddPath(LIBCLC_INCLUDEDIR,
                                         clang::frontend::Angled,
                                         false, false);

         // Add libclc include
         c.getPreprocessorOpts().Includes.push_back("clc/clc.h");
      } else {
         // Add opencl-c generic search path
         c.getHeaderSearchOpts().AddPath(CLANG_RESOURCE_DIR,
                                         clang::frontend::Angled,
                                         false, false);

         // Add opencl include
         c.getPreprocessorOpts().Includes.push_back("opencl-c.h");
      }

      // Add definition for the OpenCL version
      c.getPreprocessorOpts().addMacroDef("__OPENCL_VERSION__=" +
              std::to_string(get_cl_version(
                                  dev.device_version()).version_number));

      // clc.h requires that this macro be defined:
      c.getPreprocessorOpts().addMacroDef("cl_clang_storage_class_specifiers");
      c.getPreprocessorOpts().addRemappedFile(
              name, ::llvm::MemoryBuffer::getMemBuffer(source).release());

      if (headers.size()) {
         const std::string tmp_header_path = "/tmp/clover/";

         c.getHeaderSearchOpts().AddPath(tmp_header_path,
                                         clang::frontend::Angled,
                                         false, false);

         for (const auto &header : headers)
            c.getPreprocessorOpts().addRemappedFile(
               tmp_header_path + header.first,
               ::llvm::MemoryBuffer::getMemBuffer(header.second).release());
      }

      // Tell clang to link this file before performing any
      // optimizations.  This is required so that we can replace calls
      // to the OpenCL C barrier() builtin with calls to target
      // intrinsics that have the noduplicate attribute.  This
      // attribute will prevent Clang from creating illegal uses of
      // barrier() (e.g. Moving barrier() inside a conditional that is
      // no executed by all threads) during its optimizaton passes.
      if (use_libclc)
         compat::add_link_bitcode_file(c.getCodeGenOpts(),
                                       LIBCLC_LIBEXECDIR + dev.ir_target() + ".bc");

      // Compile the code
      clang::EmitLLVMOnlyAction act(&ctx);
      if (!c.ExecuteAction(act))
         throw build_error();

      return act.takeModule();
   }
}

module
clover::llvm::compile_program(const std::string &source,
                              const header_map &headers,
                              const device &dev,
                              const std::string &opts,
                              std::string &r_log) {
   if (has_flag(debug::clc))
      debug::log(".cl", "// Options: " + opts + '\n' + source);

   auto ctx = create_context(r_log);
   auto c = create_compiler_instance(dev, dev.ir_target(),
                                     tokenize(opts + " input.cl"), r_log);
   auto mod = compile(*ctx, *c, "input.cl", source, headers, dev, opts, true,
                      r_log);

   if (has_flag(debug::llvm))
      debug::log(".ll", print_module_bitcode(*mod));

   return build_module_library(*mod, module::section::text_intermediate);
}

namespace {
   void
   optimize(Module &mod, unsigned optimization_level,
            bool internalize_symbols) {
      ::llvm::legacy::PassManager pm;

      // By default, the function internalizer pass will look for a function
      // called "main" and then mark all other functions as internal.  Marking
      // functions as internal enables the optimizer to perform optimizations
      // like function inlining and global dead-code elimination.
      //
      // When there is no "main" function in a module, the internalize pass will
      // treat the module like a library, and it won't internalize any functions.
      // Since there is no "main" function in our kernels, we need to tell
      // the internalizer pass that this module is not a library by passing a
      // list of kernel functions to the internalizer.  The internalizer will
      // treat the functions in the list as "main" functions and internalize
      // all of the other functions.
      if (internalize_symbols) {
         std::vector<std::string> names =
            map(std::mem_fn(&Function::getName), get_kernels(mod));
         pm.add(::llvm::createInternalizePass(
                      [=](const ::llvm::GlobalValue &gv) {
                         return std::find(names.begin(), names.end(),
                                          gv.getName()) != names.end();
                      }));
      }

      ::llvm::PassManagerBuilder pmb;
      pmb.OptLevel = optimization_level;
      pmb.LibraryInfo = new ::llvm::TargetLibraryInfoImpl(
         ::llvm::Triple(mod.getTargetTriple()));
      pmb.populateModulePassManager(pm);
      pm.run(mod);
   }

   std::unique_ptr<Module>
   link(LLVMContext &ctx, const clang::CompilerInstance &c,
        const std::vector<module> &modules, std::string &r_log) {
      std::unique_ptr<Module> mod { new Module("link", ctx) };
      std::unique_ptr< ::llvm::Linker> linker { new ::llvm::Linker(*mod) };

      for (auto &m : modules) {
         if (linker->linkInModule(parse_module_library(m, ctx, r_log)))
            throw build_error();
      }

      return mod;
   }
}

module
clover::llvm::link_program(const std::vector<module> &modules,
                           const device &dev, const std::string &opts,
                           std::string &r_log) {
   std::vector<std::string> options = tokenize(opts + " input.cl");
   const bool create_library = count("-create-library", options);
   erase_if(equals("-create-library"), options);

   auto ctx = create_context(r_log);
   auto c = create_compiler_instance(dev, dev.ir_target(), options, r_log);
   auto mod = link(*ctx, *c, modules, r_log);

   optimize(*mod, c->getCodeGenOpts().OptimizationLevel, !create_library);

   static std::atomic_uint seq(0);
   const std::string id = "." + mod->getModuleIdentifier() + "-" +
      std::to_string(seq++);

   if (has_flag(debug::llvm))
      debug::log(id + ".ll", print_module_bitcode(*mod));

   if (create_library) {
      return build_module_library(*mod, module::section::text_library);

   } else if (dev.ir_format() == PIPE_SHADER_IR_NATIVE) {
      if (has_flag(debug::native))
         debug::log(id +  ".asm", print_module_native(*mod, dev.ir_target()));

      return build_module_native(*mod, dev.ir_target(), *c, r_log);

   } else {
      unreachable("Unsupported IR.");
   }
}

#ifdef HAVE_CLOVER_SPIRV
module
clover::llvm::compile_to_spirv(const std::string &source,
                               const header_map &headers,
                               const device &dev,
                               const std::string &opts,
                               std::string &r_log) {
   if (has_flag(debug::clc))
      debug::log(".cl", "// Options: " + opts + '\n' + source);

   auto ctx = create_context(r_log);
   const std::string target = dev.address_bits() == 32u ?
      "-spir-unknown-unknown" :
      "-spir64-unknown-unknown";
   auto c = create_compiler_instance(dev, target,
                                     tokenize(opts + " input.cl"), r_log);
   auto mod = compile(*ctx, *c, "input.cl", source, headers, dev, opts, false,
                      r_log);

   if (has_flag(debug::llvm))
      debug::log(".ll", print_module_bitcode(*mod));

   std::string error_msg;
   if (!::llvm::regularizeLlvmForSpirv(mod.get(), error_msg)) {
      r_log += "Failed to regularize LLVM IR for SPIR-V: " + error_msg + ".\n";
      throw error(CL_INVALID_VALUE);
   }

   std::ostringstream os;
   if (!::llvm::writeSpirv(mod.get(), os, error_msg)) {
      r_log += "Translation from LLVM IR to SPIR-V failed: " + error_msg + ".\n";
      throw error(CL_INVALID_VALUE);
   }

   const std::string osContent = os.str();
   std::vector<char> binary(osContent.begin(), osContent.end());
   if (binary.empty()) {
      r_log += "Failed to retrieve SPIR-V binary.\n";
      throw error(CL_INVALID_VALUE);
   }

   if (has_flag(debug::spirv))
      debug::log(".spvasm", spirv::print_module(binary, dev.device_version()));

   return spirv::compile_program(binary, dev, r_log);
}
#endif
