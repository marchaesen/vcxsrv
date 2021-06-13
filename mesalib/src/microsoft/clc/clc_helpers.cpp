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

#include <sstream>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/IR/DiagnosticPrinter.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <LLVMSPIRVLib/LLVMSPIRVLib.h>

#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/TextDiagnosticBuffer.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Basic/TargetInfo.h>

#include <spirv-tools/libspirv.hpp>
#include <spirv-tools/linker.hpp>

#include "util/macros.h"
#include "glsl_types.h"
#include "nir.h"
#include "nir_types.h"

#include "clc_helpers.h"
#include "spirv.h"

#include "opencl-c.h.h"
#include "opencl-c-base.h.h"

using ::llvm::Function;
using ::llvm::LLVMContext;
using ::llvm::Module;
using ::llvm::raw_string_ostream;

static void
llvm_log_handler(const ::llvm::DiagnosticInfo &di, void *data) {
   raw_string_ostream os { *reinterpret_cast<std::string *>(data) };
   ::llvm::DiagnosticPrinterRawOStream printer { os };
   di.print(printer);
}

class SPIRVKernelArg {
public:
   SPIRVKernelArg(uint32_t id, uint32_t typeId) : id(id), typeId(typeId),
                                                  addrQualifier(CLC_KERNEL_ARG_ADDRESS_PRIVATE),
                                                  accessQualifier(0),
                                                  typeQualifier(0) { }
   ~SPIRVKernelArg() { }

   uint32_t id;
   uint32_t typeId;
   std::string name;
   std::string typeName;
   enum clc_kernel_arg_address_qualifier addrQualifier;
   unsigned accessQualifier;
   unsigned typeQualifier;
};

class SPIRVKernelInfo {
public:
   SPIRVKernelInfo(uint32_t fid, const char *nm) : funcId(fid), name(nm), vecHint(0) { }
   ~SPIRVKernelInfo() { }

   uint32_t funcId;
   std::string name;
   std::vector<SPIRVKernelArg> args;
   unsigned vecHint;
};

class SPIRVKernelParser {
public:
   SPIRVKernelParser() : curKernel(NULL)
   {
      ctx = spvContextCreate(SPV_ENV_UNIVERSAL_1_0);
   }

   ~SPIRVKernelParser()
   {
     spvContextDestroy(ctx);
   }

   void parseEntryPoint(const spv_parsed_instruction_t *ins)
   {
      assert(ins->num_operands >= 3);

      const spv_parsed_operand_t *op = &ins->operands[1];

      assert(op->type == SPV_OPERAND_TYPE_ID);

      uint32_t funcId = ins->words[op->offset];

      for (auto &iter : kernels) {
         if (funcId == iter.funcId)
            return;
      }

      op = &ins->operands[2];
      assert(op->type == SPV_OPERAND_TYPE_LITERAL_STRING);
      const char *name = reinterpret_cast<const char *>(ins->words + op->offset);

      kernels.push_back(SPIRVKernelInfo(funcId, name));
   }

   void parseFunction(const spv_parsed_instruction_t *ins)
   {
      assert(ins->num_operands == 4);

      const spv_parsed_operand_t *op = &ins->operands[1];

      assert(op->type == SPV_OPERAND_TYPE_RESULT_ID);

      uint32_t funcId = ins->words[op->offset];

      SPIRVKernelInfo *kernel = NULL;

      for (auto &kernel : kernels) {
         if (funcId == kernel.funcId && !kernel.args.size()) {
            curKernel = &kernel;
	    return;
         }
      }
   }

   void parseFunctionParam(const spv_parsed_instruction_t *ins)
   {
      const spv_parsed_operand_t *op;
      uint32_t id, typeId;

      if (!curKernel)
         return;

      assert(ins->num_operands == 2);
      op = &ins->operands[0];
      assert(op->type == SPV_OPERAND_TYPE_TYPE_ID);
      typeId = ins->words[op->offset];
      op = &ins->operands[1];
      assert(op->type == SPV_OPERAND_TYPE_RESULT_ID);
      id = ins->words[op->offset];
      curKernel->args.push_back(SPIRVKernelArg(id, typeId));
   }

   void parseName(const spv_parsed_instruction_t *ins)
   {
      const spv_parsed_operand_t *op;
      const char *name;
      uint32_t id;

      assert(ins->num_operands == 2);

      op = &ins->operands[0];
      assert(op->type == SPV_OPERAND_TYPE_ID);
      id = ins->words[op->offset];
      op = &ins->operands[1];
      assert(op->type == SPV_OPERAND_TYPE_LITERAL_STRING);
      name = reinterpret_cast<const char *>(ins->words + op->offset);

      for (auto &kernel : kernels) {
         for (auto &arg : kernel.args) {
            if (arg.id == id && arg.name.empty()) {
              arg.name = name;
              break;
	    }
         }
      }
   }

   void parseTypePointer(const spv_parsed_instruction_t *ins)
   {
      enum clc_kernel_arg_address_qualifier addrQualifier;
      uint32_t typeId, targetTypeId, storageClass;
      const spv_parsed_operand_t *op;
      const char *typeName;

      assert(ins->num_operands == 3);

      op = &ins->operands[0];
      assert(op->type == SPV_OPERAND_TYPE_RESULT_ID);
      typeId = ins->words[op->offset];

      op = &ins->operands[1];
      assert(op->type == SPV_OPERAND_TYPE_STORAGE_CLASS);
      storageClass = ins->words[op->offset];
      switch (storageClass) {
      case SpvStorageClassCrossWorkgroup:
         addrQualifier = CLC_KERNEL_ARG_ADDRESS_GLOBAL;
         break;
      case SpvStorageClassWorkgroup:
         addrQualifier = CLC_KERNEL_ARG_ADDRESS_LOCAL;
         break;
      case SpvStorageClassUniformConstant:
         addrQualifier = CLC_KERNEL_ARG_ADDRESS_CONSTANT;
         break;
      default:
         addrQualifier = CLC_KERNEL_ARG_ADDRESS_PRIVATE;
         break;
      }

      for (auto &kernel : kernels) {
	 for (auto &arg : kernel.args) {
            if (arg.typeId == typeId)
               arg.addrQualifier = addrQualifier;
         }
      }
   }

   void parseOpString(const spv_parsed_instruction_t *ins)
   {
      const spv_parsed_operand_t *op;
      std::string str;

      assert(ins->num_operands == 2);

      op = &ins->operands[1];
      assert(op->type == SPV_OPERAND_TYPE_LITERAL_STRING);
      str = reinterpret_cast<const char *>(ins->words + op->offset);

      if (str.find("kernel_arg_type.") != 0)
         return;

      size_t start = sizeof("kernel_arg_type.") - 1;

      for (auto &kernel : kernels) {
         size_t pos;

	 pos = str.find(kernel.name, start);
         if (pos == std::string::npos ||
             pos != start || str[start + kernel.name.size()] != '.')
            continue;

	 pos = start + kernel.name.size();
         if (str[pos++] != '.')
            continue;

         for (auto &arg : kernel.args) {
            if (arg.name.empty())
               break;

            size_t typeEnd = str.find(',', pos);
	    if (typeEnd == std::string::npos)
               break;

            arg.typeName = str.substr(pos, typeEnd - pos);
            pos = typeEnd + 1;
         }
      }
   }

   void applyDecoration(uint32_t id, const spv_parsed_instruction_t *ins)
   {
      auto iter = decorationGroups.find(id);
      if (iter != decorationGroups.end()) {
         for (uint32_t entry : iter->second)
            applyDecoration(entry, ins);
         return;
      }

      const spv_parsed_operand_t *op;
      uint32_t decoration;

      assert(ins->num_operands >= 2);

      op = &ins->operands[1];
      assert(op->type == SPV_OPERAND_TYPE_DECORATION);
      decoration = ins->words[op->offset];

      for (auto &kernel : kernels) {
         for (auto &arg : kernel.args) {
            if (arg.id == id) {
               switch (decoration) {
               case SpvDecorationVolatile:
                  arg.typeQualifier |= CLC_KERNEL_ARG_TYPE_VOLATILE;
                  break;
               case SpvDecorationConstant:
                  arg.typeQualifier |= CLC_KERNEL_ARG_TYPE_CONST;
                  break;
               case SpvDecorationRestrict:
                  arg.typeQualifier |= CLC_KERNEL_ARG_TYPE_RESTRICT;
                  break;
               case SpvDecorationFuncParamAttr:
                  op = &ins->operands[2];
                  assert(op->type == SPV_OPERAND_TYPE_FUNCTION_PARAMETER_ATTRIBUTE);
                  switch (ins->words[op->offset]) {
                  case SpvFunctionParameterAttributeNoAlias:
                     arg.typeQualifier |= CLC_KERNEL_ARG_TYPE_RESTRICT;
                     break;
                  case SpvFunctionParameterAttributeNoWrite:
                     arg.typeQualifier |= CLC_KERNEL_ARG_TYPE_CONST;
                     break;
                  }
                  break;
               }
            }

         }
      }
   }

   void parseOpDecorate(const spv_parsed_instruction_t *ins)
   {
      const spv_parsed_operand_t *op;
      uint32_t id, decoration;

      assert(ins->num_operands >= 2);

      op = &ins->operands[0];
      assert(op->type == SPV_OPERAND_TYPE_ID);
      id = ins->words[op->offset];

      applyDecoration(id, ins);
   }

   void parseOpGroupDecorate(const spv_parsed_instruction_t *ins)
   {
      assert(ins->num_operands >= 2);

      const spv_parsed_operand_t *op = &ins->operands[0];
      assert(op->type == SPV_OPERAND_TYPE_ID);
      uint32_t groupId = ins->words[op->offset];

      auto lowerBound = decorationGroups.lower_bound(groupId);
      if (lowerBound != decorationGroups.end() &&
          lowerBound->first == groupId)
         // Group already filled out
         return;

      auto iter = decorationGroups.emplace_hint(lowerBound, groupId, std::vector<uint32_t>{});
      auto& vec = iter->second;
      vec.reserve(ins->num_operands - 1);
      for (uint32_t i = 1; i < ins->num_operands; ++i) {
         op = &ins->operands[i];
         assert(op->type == SPV_OPERAND_TYPE_ID);
         vec.push_back(ins->words[op->offset]);
      }
   }

   void parseOpTypeImage(const spv_parsed_instruction_t *ins)
   {
      const spv_parsed_operand_t *op;
      uint32_t typeId;
      unsigned accessQualifier = CLC_KERNEL_ARG_ACCESS_READ;

      op = &ins->operands[0];
      assert(op->type == SPV_OPERAND_TYPE_RESULT_ID);
      typeId = ins->words[op->offset];

      if (ins->num_operands >= 9) {
         op = &ins->operands[8];
         assert(op->type == SPV_OPERAND_TYPE_ACCESS_QUALIFIER);
         switch (ins->words[op->offset]) {
         case SpvAccessQualifierReadOnly:
            accessQualifier = CLC_KERNEL_ARG_ACCESS_READ;
            break;
         case SpvAccessQualifierWriteOnly:
            accessQualifier = CLC_KERNEL_ARG_ACCESS_WRITE;
            break;
         case SpvAccessQualifierReadWrite:
            accessQualifier = CLC_KERNEL_ARG_ACCESS_WRITE |
               CLC_KERNEL_ARG_ACCESS_READ;
            break;
         }
      }

      for (auto &kernel : kernels) {
	 for (auto &arg : kernel.args) {
            if (arg.typeId == typeId) {
               arg.accessQualifier = accessQualifier;
               arg.addrQualifier = CLC_KERNEL_ARG_ADDRESS_GLOBAL;
            }
         }
      }
   }

   void parseExecutionMode(const spv_parsed_instruction_t *ins)
   {
      uint32_t executionMode = ins->words[ins->operands[1].offset];
      if (executionMode != SpvExecutionModeVecTypeHint)
         return;

      uint32_t funcId = ins->words[ins->operands[0].offset];
      uint32_t vecHint = ins->words[ins->operands[2].offset];
      for (auto& kernel : kernels) {
         if (kernel.funcId == funcId)
            kernel.vecHint = vecHint;
      }
   }

   static spv_result_t
   parseInstruction(void *data, const spv_parsed_instruction_t *ins)
   {
      SPIRVKernelParser *parser = reinterpret_cast<SPIRVKernelParser *>(data);

      switch (ins->opcode) {
      case SpvOpName:
         parser->parseName(ins);
         break;
      case SpvOpEntryPoint:
         parser->parseEntryPoint(ins);
         break;
      case SpvOpFunction:
         parser->parseFunction(ins);
         break;
      case SpvOpFunctionParameter:
         parser->parseFunctionParam(ins);
         break;
      case SpvOpFunctionEnd:
      case SpvOpLabel:
         parser->curKernel = NULL;
         break;
      case SpvOpTypePointer:
         parser->parseTypePointer(ins);
         break;
      case SpvOpTypeImage:
         parser->parseOpTypeImage(ins);
         break;
      case SpvOpString:
         parser->parseOpString(ins);
         break;
      case SpvOpDecorate:
         parser->parseOpDecorate(ins);
         break;
      case SpvOpGroupDecorate:
         parser->parseOpGroupDecorate(ins);
         break;
      case SpvOpExecutionMode:
         parser->parseExecutionMode(ins);
         break;
      default:
         break;
      }

      return SPV_SUCCESS;
   }

   bool parsingComplete()
   {
      for (auto &kernel : kernels) {
         if (kernel.name.empty())
            return false;

         for (auto &arg : kernel.args) {
            if (arg.name.empty() || arg.typeName.empty())
               return false;
         }
      }

      return true;
   }

   void parseBinary(const struct spirv_binary &spvbin)
   {
      /* 3 passes should be enough to retrieve all kernel information:
       * 1st pass: all entry point name and number of args
       * 2nd pass: argument names and type names
       * 3rd pass: pointer type names
       */
      for (unsigned pass = 0; pass < 3; pass++) {
         spvBinaryParse(ctx, reinterpret_cast<void *>(this),
                        spvbin.data, spvbin.size / 4,
                        NULL, parseInstruction, NULL);

         if (parsingComplete())
            return;
      }

      assert(0);
   }

   std::vector<SPIRVKernelInfo> kernels;
   std::map<uint32_t, std::vector<uint32_t>> decorationGroups;
   SPIRVKernelInfo *curKernel;
   spv_context ctx;
};

const struct clc_kernel_info *
clc_spirv_get_kernels_info(const struct spirv_binary *spvbin,
                           unsigned *num_kernels)
{
   struct clc_kernel_info *kernels;

   SPIRVKernelParser parser;

   parser.parseBinary(*spvbin);
   *num_kernels = parser.kernels.size();
   if (!*num_kernels)
      return NULL;

   kernels = reinterpret_cast<struct clc_kernel_info *>(calloc(*num_kernels,
                                                               sizeof(*kernels)));
   assert(kernels);
   for (unsigned i = 0; i < parser.kernels.size(); i++) {
      kernels[i].name = strdup(parser.kernels[i].name.c_str());
      kernels[i].num_args = parser.kernels[i].args.size();
      kernels[i].vec_hint_size = parser.kernels[i].vecHint >> 16;
      kernels[i].vec_hint_type = (enum clc_vec_hint_type)(parser.kernels[i].vecHint & 0xFFFF);
      if (!kernels[i].num_args)
         continue;

      struct clc_kernel_arg *args;

      args = reinterpret_cast<struct clc_kernel_arg *>(calloc(kernels[i].num_args,
                                                       sizeof(*kernels->args)));
      kernels[i].args = args;
      assert(args);
      for (unsigned j = 0; j < kernels[i].num_args; j++) {
         if (!parser.kernels[i].args[j].name.empty())
            args[j].name = strdup(parser.kernels[i].args[j].name.c_str());
         args[j].type_name = strdup(parser.kernels[i].args[j].typeName.c_str());
         args[j].address_qualifier = parser.kernels[i].args[j].addrQualifier;
         args[j].type_qualifier = parser.kernels[i].args[j].typeQualifier;
         args[j].access_qualifier = parser.kernels[i].args[j].accessQualifier;
      }
   }

   return kernels;
}

void
clc_free_kernels_info(const struct clc_kernel_info *kernels,
                      unsigned num_kernels)
{
   if (!kernels)
      return;

   for (unsigned i = 0; i < num_kernels; i++) {
      if (kernels[i].args) {
         for (unsigned j = 0; j < kernels[i].num_args; j++) {
            free((void *)kernels[i].args[j].name);
            free((void *)kernels[i].args[j].type_name);
         }
      }
      free((void *)kernels[i].name);
   }

   free((void *)kernels);
}

int
clc_to_spirv(const struct clc_compile_args *args,
             struct spirv_binary *spvbin,
             const struct clc_logger *logger)
{
   LLVMInitializeAllTargets();
   LLVMInitializeAllTargetInfos();
   LLVMInitializeAllTargetMCs();
   LLVMInitializeAllAsmPrinters();

   std::string log;
   std::unique_ptr<LLVMContext> llvm_ctx { new LLVMContext };
   llvm_ctx->setDiagnosticHandlerCallBack(llvm_log_handler, &log);

   std::unique_ptr<clang::CompilerInstance> c { new clang::CompilerInstance };
   clang::DiagnosticsEngine diag { new clang::DiagnosticIDs,
         new clang::DiagnosticOptions,
         new clang::TextDiagnosticPrinter(*new raw_string_ostream(log),
                                          &c->getDiagnosticOpts(), true)};

   std::vector<const char *> clang_opts = {
      args->source.name,
      "-triple", "spir64-unknown-unknown",
      // By default, clang prefers to use modules to pull in the default headers,
      // which doesn't work with our technique of embedding the headers in our binary
      "-finclude-default-header",
      // Add a default CL compiler version. Clang will pick the last one specified
      // on the command line, so the app can override this one.
      "-cl-std=cl1.2",
      // The LLVM-SPIRV-Translator doesn't support memset with variable size
      "-fno-builtin-memset",
      // LLVM's optimizations can produce code that the translator can't translate
      "-O0",
      // Ensure inline functions are actually emitted
      "-fgnu89-inline"
   };
   // We assume there's appropriate defines for __OPENCL_VERSION__ and __IMAGE_SUPPORT__
   // being provided by the caller here.
   clang_opts.insert(clang_opts.end(), args->args, args->args + args->num_args);

   if (!clang::CompilerInvocation::CreateFromArgs(c->getInvocation(),
#if LLVM_VERSION_MAJOR >= 10
                                                  clang_opts,
#else
                                                  clang_opts.data(),
                                                  clang_opts.data() + clang_opts.size(),
#endif
                                                  diag)) {
      log += "Couldn't create Clang invocation.\n";
      clc_error(logger, log.c_str());
      return -1;
   }

   if (diag.hasErrorOccurred()) {
      log += "Errors occurred during Clang invocation.\n";
      clc_error(logger, log.c_str());
      return -1;
   }

   // This is a workaround for a Clang bug which causes the number
   // of warnings and errors to be printed to stderr.
   // http://www.llvm.org/bugs/show_bug.cgi?id=19735
   c->getDiagnosticOpts().ShowCarets = false;

   c->createDiagnostics(new clang::TextDiagnosticPrinter(
                           *new raw_string_ostream(log),
                           &c->getDiagnosticOpts(), true));

   c->setTarget(clang::TargetInfo::CreateTargetInfo(
                   c->getDiagnostics(), c->getInvocation().TargetOpts));

   c->getFrontendOpts().ProgramAction = clang::frontend::EmitLLVMOnly;
   c->getHeaderSearchOpts().UseBuiltinIncludes = false;
   c->getHeaderSearchOpts().UseStandardSystemIncludes = false;

   // Add opencl-c generic search path
   {
      ::llvm::SmallString<128> system_header_path;
      ::llvm::sys::path::system_temp_directory(true, system_header_path);
      ::llvm::sys::path::append(system_header_path, "openclon12");
      c->getHeaderSearchOpts().AddPath(system_header_path.str(),
                                       clang::frontend::Angled,
                                       false, false);

      ::llvm::sys::path::append(system_header_path, "opencl-c.h");
      c->getPreprocessorOpts().addRemappedFile(system_header_path.str(),
         ::llvm::MemoryBuffer::getMemBuffer(llvm::StringRef(opencl_c_source, _countof(opencl_c_source) - 1)).release());

      ::llvm::sys::path::remove_filename(system_header_path);
      ::llvm::sys::path::append(system_header_path, "opencl-c-base.h");
      c->getPreprocessorOpts().addRemappedFile(system_header_path.str(),
         ::llvm::MemoryBuffer::getMemBuffer(llvm::StringRef(opencl_c_base_source, _countof(opencl_c_base_source) - 1)).release());
   }

   if (args->num_headers) {
      ::llvm::SmallString<128> tmp_header_path;
      ::llvm::sys::path::system_temp_directory(true, tmp_header_path);
      ::llvm::sys::path::append(tmp_header_path, "openclon12");

      c->getHeaderSearchOpts().AddPath(tmp_header_path.str(),
                                       clang::frontend::Quoted,
                                       false, false);

      for (size_t i = 0; i < args->num_headers; i++) {
         auto path_copy = tmp_header_path;
         ::llvm::sys::path::append(path_copy, ::llvm::sys::path::convert_to_slash(args->headers[i].name));
         c->getPreprocessorOpts().addRemappedFile(path_copy.str(),
            ::llvm::MemoryBuffer::getMemBufferCopy(args->headers[i].value).release());
      }
   }

   c->getPreprocessorOpts().addRemappedFile(
           args->source.name,
           ::llvm::MemoryBuffer::getMemBufferCopy(std::string(args->source.value)).release());

   // Compile the code
   clang::EmitLLVMOnlyAction act(llvm_ctx.get());
   if (!c->ExecuteAction(act)) {
      log += "Error executing LLVM compilation action.\n";
      clc_error(logger, log.c_str());
      return -1;
   }

   auto mod = act.takeModule();
   std::ostringstream spv_stream;
   if (!::llvm::writeSpirv(mod.get(), spv_stream, log)) {
      log += "Translation from LLVM IR to SPIR-V failed.\n";
      clc_error(logger, log.c_str());
      return -1;
   }

   const std::string spv_out = spv_stream.str();
   spvbin->size = spv_out.size();
   spvbin->data = static_cast<uint32_t *>(malloc(spvbin->size));
   memcpy(spvbin->data, spv_out.data(), spvbin->size);

   return 0;
}

static const char *
spv_result_to_str(spv_result_t res)
{
   switch (res) {
   case SPV_SUCCESS: return "success";
   case SPV_UNSUPPORTED: return "unsupported";
   case SPV_END_OF_STREAM: return "end of stream";
   case SPV_WARNING: return "warning";
   case SPV_FAILED_MATCH: return "failed match";
   case SPV_REQUESTED_TERMINATION: return "requested termination";
   case SPV_ERROR_INTERNAL: return "internal error";
   case SPV_ERROR_OUT_OF_MEMORY: return "out of memory";
   case SPV_ERROR_INVALID_POINTER: return "invalid pointer";
   case SPV_ERROR_INVALID_BINARY: return "invalid binary";
   case SPV_ERROR_INVALID_TEXT: return "invalid text";
   case SPV_ERROR_INVALID_TABLE: return "invalid table";
   case SPV_ERROR_INVALID_VALUE: return "invalid value";
   case SPV_ERROR_INVALID_DIAGNOSTIC: return "invalid diagnostic";
   case SPV_ERROR_INVALID_LOOKUP: return "invalid lookup";
   case SPV_ERROR_INVALID_ID: return "invalid id";
   case SPV_ERROR_INVALID_CFG: return "invalid config";
   case SPV_ERROR_INVALID_LAYOUT: return "invalid layout";
   case SPV_ERROR_INVALID_CAPABILITY: return "invalid capability";
   case SPV_ERROR_INVALID_DATA: return "invalid data";
   case SPV_ERROR_MISSING_EXTENSION: return "missing extension";
   case SPV_ERROR_WRONG_VERSION: return "wrong version";
   default: return "unknown error";
   }
}

class SPIRVMessageConsumer {
public:
   SPIRVMessageConsumer(const struct clc_logger *logger): logger(logger) {}

   void operator()(spv_message_level_t level, const char *src,
                   const spv_position_t &pos, const char *msg)
   {
      switch(level) {
      case SPV_MSG_FATAL:
      case SPV_MSG_INTERNAL_ERROR:
      case SPV_MSG_ERROR:
         clc_error(logger, "(file=%s,line=%ld,column=%ld,index=%ld): %s",
                   src, pos.line, pos.column, pos.index, msg);
         break;

      case SPV_MSG_WARNING:
         clc_warning(logger, "(file=%s,line=%ld,column=%ld,index=%ld): %s",
                     src, pos.line, pos.column, pos.index, msg);
         break;

      default:
         break;
      }
   }

private:
   const struct clc_logger *logger;
};

int
clc_link_spirv_binaries(const struct clc_linker_args *args,
                        struct spirv_binary *dst_bin,
                        const struct clc_logger *logger)
{
   std::vector<std::vector<uint32_t>> binaries;

   for (unsigned i = 0; i < args->num_in_objs; i++) {
      std::vector<uint32_t> bin(args->in_objs[i]->spvbin.data,
                                args->in_objs[i]->spvbin.data +
                                   (args->in_objs[i]->spvbin.size / 4));
      binaries.push_back(bin);
   }

   SPIRVMessageConsumer msgconsumer(logger);
   spvtools::Context context(SPV_ENV_UNIVERSAL_1_0);
   context.SetMessageConsumer(msgconsumer);
   spvtools::LinkerOptions options;
   options.SetAllowPartialLinkage(args->create_library);
   options.SetCreateLibrary(args->create_library);
   std::vector<uint32_t> linkingResult;
   spv_result_t status = spvtools::Link(context, binaries, &linkingResult, options);
   if (status != SPV_SUCCESS) {
      return -1;
   }

   dst_bin->size = linkingResult.size() * 4;
   dst_bin->data = static_cast<uint32_t *>(malloc(dst_bin->size));
   memcpy(dst_bin->data, linkingResult.data(), dst_bin->size);

   return 0;
}

void
clc_dump_spirv(const struct spirv_binary *spvbin, FILE *f)
{
   spvtools::SpirvTools tools(SPV_ENV_UNIVERSAL_1_0);
   std::vector<uint32_t> bin(spvbin->data, spvbin->data + (spvbin->size / 4));
   std::string out;
   tools.Disassemble(bin, &out,
                     SPV_BINARY_TO_TEXT_OPTION_INDENT |
                     SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES);
   fwrite(out.c_str(), out.size(), 1, f);
}

void
clc_free_spirv_binary(struct spirv_binary *spvbin)
{
   free(spvbin->data);
}
