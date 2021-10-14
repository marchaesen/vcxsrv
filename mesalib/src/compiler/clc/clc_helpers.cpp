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
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Bitcode/BitcodeReader.h>
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
#include <spirv-tools/optimizer.hpp>

#include "util/macros.h"
#include "glsl_types.h"

#include "spirv.h"

#ifdef USE_STATIC_OPENCL_C_H
#include "opencl-c.h.h"
#include "opencl-c-base.h.h"
#endif

#include "clc_helpers.h"

/* Use the highest version of SPIRV supported by SPIRV-Tools. */
constexpr spv_target_env spirv_target = SPV_ENV_UNIVERSAL_1_5;

constexpr SPIRV::VersionNumber invalid_spirv_trans_version = static_cast<SPIRV::VersionNumber>(0);

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
      ctx = spvContextCreate(spirv_target);
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
      uint32_t typeId, storageClass;
      const spv_parsed_operand_t *op;

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

      if (decoration == SpvDecorationSpecId) {
         uint32_t spec_id = ins->words[ins->operands[2].offset];
         for (auto &c : specConstants) {
            if (c.second.id == spec_id) {
               assert(c.first == id);
               return;
            }
         }
         specConstants.emplace_back(id, clc_parsed_spec_constant{ spec_id });
         return;
      }

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
      uint32_t id;

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

   void parseLiteralType(const spv_parsed_instruction_t *ins)
   {
      uint32_t typeId = ins->words[ins->operands[0].offset];
      auto& literalType = literalTypes[typeId];
      switch (ins->opcode) {
      case SpvOpTypeBool:
         literalType = CLC_SPEC_CONSTANT_BOOL;
         break;
      case SpvOpTypeFloat: {
         uint32_t sizeInBits = ins->words[ins->operands[1].offset];
         switch (sizeInBits) {
         case 32:
            literalType = CLC_SPEC_CONSTANT_FLOAT;
            break;
         case 64:
            literalType = CLC_SPEC_CONSTANT_DOUBLE;
            break;
         case 16:
            /* Can't be used for a spec constant */
            break;
         default:
            unreachable("Unexpected float bit size");
         }
         break;
      }
      case SpvOpTypeInt: {
         uint32_t sizeInBits = ins->words[ins->operands[1].offset];
         bool isSigned = ins->words[ins->operands[2].offset];
         if (isSigned) {
            switch (sizeInBits) {
            case 8:
               literalType = CLC_SPEC_CONSTANT_INT8;
               break;
            case 16:
               literalType = CLC_SPEC_CONSTANT_INT16;
               break;
            case 32:
               literalType = CLC_SPEC_CONSTANT_INT32;
               break;
            case 64:
               literalType = CLC_SPEC_CONSTANT_INT64;
               break;
            default:
               unreachable("Unexpected int bit size");
            }
         } else {
            switch (sizeInBits) {
            case 8:
               literalType = CLC_SPEC_CONSTANT_UINT8;
               break;
            case 16:
               literalType = CLC_SPEC_CONSTANT_UINT16;
               break;
            case 32:
               literalType = CLC_SPEC_CONSTANT_UINT32;
               break;
            case 64:
               literalType = CLC_SPEC_CONSTANT_UINT64;
               break;
            default:
               unreachable("Unexpected uint bit size");
            }
         }
         break;
      }
      default:
         unreachable("Unexpected type opcode");
      }
   }

   void parseSpecConstant(const spv_parsed_instruction_t *ins)
   {
      uint32_t id = ins->result_id;
      for (auto& c : specConstants) {
         if (c.first == id) {
            auto& data = c.second;
            switch (ins->opcode) {
            case SpvOpSpecConstant: {
               uint32_t typeId = ins->words[ins->operands[0].offset];

               // This better be an integer or float type
               auto typeIter = literalTypes.find(typeId);
               assert(typeIter != literalTypes.end());

               data.type = typeIter->second;
               break;
            }
            case SpvOpSpecConstantFalse:
            case SpvOpSpecConstantTrue:
               data.type = CLC_SPEC_CONSTANT_BOOL;
               break;
            default:
               unreachable("Composites and Ops are not directly specializable.");
            }
         }
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
      case SpvOpTypeBool:
      case SpvOpTypeInt:
      case SpvOpTypeFloat:
         parser->parseLiteralType(ins);
         break;
      case SpvOpSpecConstant:
      case SpvOpSpecConstantFalse:
      case SpvOpSpecConstantTrue:
         parser->parseSpecConstant(ins);
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

   bool parseBinary(const struct clc_binary &spvbin, const struct clc_logger *logger)
   {
      /* 3 passes should be enough to retrieve all kernel information:
       * 1st pass: all entry point name and number of args
       * 2nd pass: argument names and type names
       * 3rd pass: pointer type names
       */
      for (unsigned pass = 0; pass < 3; pass++) {
         spv_diagnostic diagnostic = NULL;
         auto result = spvBinaryParse(ctx, reinterpret_cast<void *>(this),
                                      static_cast<uint32_t*>(spvbin.data), spvbin.size / 4,
                                      NULL, parseInstruction, &diagnostic);

         if (result != SPV_SUCCESS) {
            if (diagnostic && logger)
               logger->error(logger->priv, diagnostic->error);
            return false;
         }

         if (parsingComplete())
            return true;
      }

      assert(0);
      return false;
   }

   std::vector<SPIRVKernelInfo> kernels;
   std::vector<std::pair<uint32_t, clc_parsed_spec_constant>> specConstants;
   std::map<uint32_t, enum clc_spec_constant_type> literalTypes;
   std::map<uint32_t, std::vector<uint32_t>> decorationGroups;
   SPIRVKernelInfo *curKernel;
   spv_context ctx;
};

bool
clc_spirv_get_kernels_info(const struct clc_binary *spvbin,
                           const struct clc_kernel_info **out_kernels,
                           unsigned *num_kernels,
                           const struct clc_parsed_spec_constant **out_spec_constants,
                           unsigned *num_spec_constants,
                           const struct clc_logger *logger)
{
   struct clc_kernel_info *kernels;
   struct clc_parsed_spec_constant *spec_constants = NULL;

   SPIRVKernelParser parser;

   if (!parser.parseBinary(*spvbin, logger))
      return false;

   *num_kernels = parser.kernels.size();
   *num_spec_constants = parser.specConstants.size();
   if (!*num_kernels)
      return false;

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

   if (*num_spec_constants) {
      spec_constants = reinterpret_cast<struct clc_parsed_spec_constant *>(calloc(*num_spec_constants,
                                                                                  sizeof(*spec_constants)));
      assert(spec_constants);

      for (unsigned i = 0; i < parser.specConstants.size(); ++i) {
         spec_constants[i] = parser.specConstants[i].second;
      }
   }

   *out_kernels = kernels;
   *out_spec_constants = spec_constants;

   return true;
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

static std::pair<std::unique_ptr<::llvm::Module>, std::unique_ptr<LLVMContext>>
clc_compile_to_llvm_module(const struct clc_compile_args *args,
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
      clc_error(logger, "%sCouldn't create Clang invocation.\n", log.c_str());
      return {};
   }

   if (diag.hasErrorOccurred()) {
      clc_error(logger, "%sErrors occurred during Clang invocation.\n",
                log.c_str());
      return {};
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

#ifdef USE_STATIC_OPENCL_C_H
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
         ::llvm::MemoryBuffer::getMemBuffer(llvm::StringRef(opencl_c_source, ARRAY_SIZE(opencl_c_source) - 1)).release());

      ::llvm::sys::path::remove_filename(system_header_path);
      ::llvm::sys::path::append(system_header_path, "opencl-c-base.h");
      c->getPreprocessorOpts().addRemappedFile(system_header_path.str(),
         ::llvm::MemoryBuffer::getMemBuffer(llvm::StringRef(opencl_c_base_source, ARRAY_SIZE(opencl_c_base_source) - 1)).release());
   }
#else
   c->getHeaderSearchOpts().UseBuiltinIncludes = true;
   c->getHeaderSearchOpts().UseStandardSystemIncludes = true;
   c->getHeaderSearchOpts().ResourceDir = CLANG_RESOURCE_DIR;

   // Add opencl-c generic search path
   c->getHeaderSearchOpts().AddPath(CLANG_RESOURCE_DIR,
                                    clang::frontend::Angled,
                                    false, false);
   // Add opencl include
   c->getPreprocessorOpts().Includes.push_back("opencl-c.h");
#endif

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
      clc_error(logger, "%sError executing LLVM compilation action.\n",
                log.c_str());
      return {};
   }

   return { act.takeModule(), std::move(llvm_ctx) };
}

static SPIRV::VersionNumber
spirv_version_to_llvm_spirv_translator_version(enum clc_spirv_version version)
{
   switch (version) {
   case CLC_SPIRV_VERSION_MAX: return SPIRV::VersionNumber::MaximumVersion;
   case CLC_SPIRV_VERSION_1_0: return SPIRV::VersionNumber::SPIRV_1_0;
   case CLC_SPIRV_VERSION_1_1: return SPIRV::VersionNumber::SPIRV_1_1;
   case CLC_SPIRV_VERSION_1_2: return SPIRV::VersionNumber::SPIRV_1_2;
   case CLC_SPIRV_VERSION_1_3: return SPIRV::VersionNumber::SPIRV_1_3;
#ifdef HAS_SPIRV_1_4
   case CLC_SPIRV_VERSION_1_4: return SPIRV::VersionNumber::SPIRV_1_4;
#endif
   default:      return invalid_spirv_trans_version;
   }
}

static int
llvm_mod_to_spirv(std::unique_ptr<::llvm::Module> mod,
                  std::unique_ptr<LLVMContext> context,
                  const struct clc_compile_args *args,
                  const struct clc_logger *logger,
                  struct clc_binary *out_spirv)
{
   std::string log;

   SPIRV::VersionNumber version =
      spirv_version_to_llvm_spirv_translator_version(args->spirv_version);
   if (version == invalid_spirv_trans_version) {
      clc_error(logger, "Invalid/unsupported SPIRV specified.\n");
      return -1;
   }

   const char *const *extensions = NULL;
   if (args)
      extensions = args->allowed_spirv_extensions;
   if (!extensions) {
      /* The SPIR-V parser doesn't handle all extensions */
      static const char *default_extensions[] = {
         "SPV_EXT_shader_atomic_float_add",
         "SPV_EXT_shader_atomic_float_min_max",
         "SPV_KHR_float_controls",
         NULL,
      };
      extensions = default_extensions;
   }

   SPIRV::TranslatorOpts::ExtensionsStatusMap ext_map;
   for (int i = 0; extensions[i]; i++) {
#define EXT(X) \
      if (strcmp(#X, extensions[i]) == 0) \
         ext_map.insert(std::make_pair(SPIRV::ExtensionID::X, true));
#include "LLVMSPIRVLib/LLVMSPIRVExtensions.inc"
#undef EXT
   }
   SPIRV::TranslatorOpts spirv_opts = SPIRV::TranslatorOpts(version, ext_map);

#if LLVM_VERSION_MAJOR >= 13
   /* This was the default in 12.0 and older, but currently we'll fail to parse without this */
   spirv_opts.setPreserveOCLKernelArgTypeMetadataThroughString(true);
#endif

   std::ostringstream spv_stream;
   if (!::llvm::writeSpirv(mod.get(), spirv_opts, spv_stream, log)) {
      clc_error(logger, "%sTranslation from LLVM IR to SPIR-V failed.\n",
                log.c_str());
      return -1;
   }

   const std::string spv_out = spv_stream.str();
   out_spirv->size = spv_out.size();
   out_spirv->data = malloc(out_spirv->size);
   memcpy(out_spirv->data, spv_out.data(), out_spirv->size);

   return 0;
}

int
clc_c_to_spir(const struct clc_compile_args *args,
              const struct clc_logger *logger,
              struct clc_binary *out_spir)
{
   auto pair = clc_compile_to_llvm_module(args, logger);
   if (!pair.first)
      return -1;

   ::llvm::SmallVector<char, 0> buffer;
   ::llvm::BitcodeWriter writer(buffer);
   writer.writeModule(*pair.first);

   out_spir->size = buffer.size_in_bytes();
   out_spir->data = malloc(out_spir->size);
   memcpy(out_spir->data, buffer.data(), out_spir->size);

   return 0;
}

int
clc_c_to_spirv(const struct clc_compile_args *args,
               const struct clc_logger *logger,
               struct clc_binary *out_spirv)
{
   auto pair = clc_compile_to_llvm_module(args, logger);
   if (!pair.first)
      return -1;
   return llvm_mod_to_spirv(std::move(pair.first), std::move(pair.second), args, logger, out_spirv);
}

int
clc_spir_to_spirv(const struct clc_binary *in_spir,
                  const struct clc_logger *logger,
                  struct clc_binary *out_spirv)
{
   LLVMInitializeAllTargets();
   LLVMInitializeAllTargetInfos();
   LLVMInitializeAllTargetMCs();
   LLVMInitializeAllAsmPrinters();

   std::unique_ptr<LLVMContext> llvm_ctx{ new LLVMContext };
   ::llvm::StringRef spir_ref(static_cast<const char*>(in_spir->data), in_spir->size);
   auto mod = ::llvm::parseBitcodeFile(::llvm::MemoryBufferRef(spir_ref, "<spir>"), *llvm_ctx);
   if (!mod)
      return -1;

   return llvm_mod_to_spirv(std::move(mod.get()), std::move(llvm_ctx), NULL, logger, out_spirv);
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
         clc_error(logger, "(file=%s,line=%ld,column=%ld,index=%ld): %s\n",
                   src, pos.line, pos.column, pos.index, msg);
         break;

      case SPV_MSG_WARNING:
         clc_warning(logger, "(file=%s,line=%ld,column=%ld,index=%ld): %s\n",
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
                        const struct clc_logger *logger,
                        struct clc_binary *out_spirv)
{
   std::vector<std::vector<uint32_t>> binaries;

   for (unsigned i = 0; i < args->num_in_objs; i++) {
      const uint32_t *data = static_cast<const uint32_t *>(args->in_objs[i]->data);
      std::vector<uint32_t> bin(data, data + (args->in_objs[i]->size / 4));
      binaries.push_back(bin);
   }

   SPIRVMessageConsumer msgconsumer(logger);
   spvtools::Context context(spirv_target);
   context.SetMessageConsumer(msgconsumer);
   spvtools::LinkerOptions options;
   options.SetAllowPartialLinkage(args->create_library);
   options.SetCreateLibrary(args->create_library);
   std::vector<uint32_t> linkingResult;
   spv_result_t status = spvtools::Link(context, binaries, &linkingResult, options);
   if (status != SPV_SUCCESS) {
      return -1;
   }

   out_spirv->size = linkingResult.size() * 4;
   out_spirv->data = static_cast<uint32_t *>(malloc(out_spirv->size));
   memcpy(out_spirv->data, linkingResult.data(), out_spirv->size);

   return 0;
}

int
clc_spirv_specialize(const struct clc_binary *in_spirv,
                     const struct clc_parsed_spirv *parsed_data,
                     const struct clc_spirv_specialization_consts *consts,
                     struct clc_binary *out_spirv)
{
   std::unordered_map<uint32_t, std::vector<uint32_t>> spec_const_map;
   for (unsigned i = 0; i < consts->num_specializations; ++i) {
      unsigned id = consts->specializations[i].id;
      auto parsed_spec_const = std::find_if(parsed_data->spec_constants,
         parsed_data->spec_constants + parsed_data->num_spec_constants,
         [id](const clc_parsed_spec_constant &c) { return c.id == id; });
      assert(parsed_spec_const != parsed_data->spec_constants + parsed_data->num_spec_constants);

      std::vector<uint32_t> words;
      switch (parsed_spec_const->type) {
      case CLC_SPEC_CONSTANT_BOOL:
         words.push_back(consts->specializations[i].value.b);
         break;
      case CLC_SPEC_CONSTANT_INT32:
      case CLC_SPEC_CONSTANT_UINT32:
      case CLC_SPEC_CONSTANT_FLOAT:
         words.push_back(consts->specializations[i].value.u32);
         break;
      case CLC_SPEC_CONSTANT_INT16:
         words.push_back((uint32_t)(int32_t)consts->specializations[i].value.i16);
         break;
      case CLC_SPEC_CONSTANT_INT8:
         words.push_back((uint32_t)(int32_t)consts->specializations[i].value.i8);
         break;
      case CLC_SPEC_CONSTANT_UINT16:
         words.push_back((uint32_t)consts->specializations[i].value.u16);
         break;
      case CLC_SPEC_CONSTANT_UINT8:
         words.push_back((uint32_t)consts->specializations[i].value.u8);
         break;
      case CLC_SPEC_CONSTANT_DOUBLE:
      case CLC_SPEC_CONSTANT_INT64:
      case CLC_SPEC_CONSTANT_UINT64:
         words.resize(2);
         memcpy(words.data(), &consts->specializations[i].value.u64, 8);
         break;
      case CLC_SPEC_CONSTANT_UNKNOWN:
         assert(0);
         break;
      }

      ASSERTED auto ret = spec_const_map.emplace(id, std::move(words));
      assert(ret.second);
   }

   spvtools::Optimizer opt(spirv_target);
   opt.RegisterPass(spvtools::CreateSetSpecConstantDefaultValuePass(std::move(spec_const_map)));

   std::vector<uint32_t> result;
   if (!opt.Run(static_cast<const uint32_t*>(in_spirv->data), in_spirv->size / 4, &result))
      return false;

   out_spirv->size = result.size() * 4;
   out_spirv->data = malloc(out_spirv->size);
   memcpy(out_spirv->data, result.data(), out_spirv->size);
   return true;
}

void
clc_dump_spirv(const struct clc_binary *spvbin, FILE *f)
{
   spvtools::SpirvTools tools(spirv_target);
   const uint32_t *data = static_cast<const uint32_t *>(spvbin->data);
   std::vector<uint32_t> bin(data, data + (spvbin->size / 4));
   std::string out;
   tools.Disassemble(bin, &out,
                     SPV_BINARY_TO_TEXT_OPTION_INDENT |
                     SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES);
   fwrite(out.c_str(), out.size(), 1, f);
}

void
clc_free_spir_binary(struct clc_binary *spir)
{
   free(spir->data);
}

void
clc_free_spirv_binary(struct clc_binary *spvbin)
{
   free(spvbin->data);
}
