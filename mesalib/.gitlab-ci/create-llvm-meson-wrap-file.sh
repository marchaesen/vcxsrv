#!/usr/bin/env bash

set -exu

# Early check for required env variables, relies on `set -u`
: "$ANDROID_LLVM_ARTIFACT_NAME"

# if DEST_DIR is not set, assing an empty value, this prevents -u to fail
: "${DEST_DIR:=}"

# TODO, check if meson can do the download and wrap file generation for us.

LLVM_INSTALL_PREFIX="${DEST_DIR}/${ANDROID_LLVM_ARTIFACT_NAME}"

if [ ! -d "$LLVM_INSTALL_PREFIX" ]; then
  echo "Cannot find an LLVM build in $LLVM_INSTALL_PREFIX" 1>&2
  exit 1
fi

mkdir -p subprojects/llvm

cat << EOF > subprojects/llvm/meson.build
project('llvm', ['cpp'])

cpp = meson.get_compiler('cpp')

_deps = []
_search = join_paths('$LLVM_INSTALL_PREFIX', 'lib')

foreach d: ['libLLVMAggressiveInstCombine', 'libLLVMAnalysis', 'libLLVMAsmParser', 'libLLVMAsmPrinter', 'libLLVMBinaryFormat', 'libLLVMBitReader', 'libLLVMBitstreamReader', 'libLLVMBitWriter', 'libLLVMCFGuard', 'libLLVMCFIVerify', 'libLLVMCodeGen', 'libLLVMCodeGenTypes', 'libLLVMCore', 'libLLVMCoroutines', 'libLLVMCoverage', 'libLLVMDebugInfoBTF', 'libLLVMDebugInfoCodeView', 'libLLVMDebuginfod', 'libLLVMDebugInfoDWARF', 'libLLVMDebugInfoGSYM', 'libLLVMDebugInfoLogicalView', 'libLLVMDebugInfoMSF', 'libLLVMDebugInfoPDB', 'libLLVMDemangle', 'libLLVMDiff', 'libLLVMDlltoolDriver', 'libLLVMDWARFLinker', 'libLLVMDWARFLinkerClassic', 'libLLVMDWARFLinkerParallel', 'libLLVMDWP', 'libLLVMExecutionEngine', 'libLLVMExegesis', 'libLLVMExegesisX86', 'libLLVMExtensions', 'libLLVMFileCheck', 'libLLVMFrontendDriver', 'libLLVMFrontendHLSL', 'libLLVMFrontendOffloading', 'libLLVMFrontendOpenACC', 'libLLVMFrontendOpenMP', 'libLLVMFuzzerCLI', 'libLLVMFuzzMutate', 'libLLVMGlobalISel', 'libLLVMHipStdPar', 'libLLVMInstCombine', 'libLLVMInstrumentation', 'libLLVMInterfaceStub', 'libLLVMInterpreter', 'libLLVMipo', 'libLLVMIRPrinter', 'libLLVMIRReader', 'libLLVMJITLink', 'libLLVMLibDriver', 'libLLVMLineEditor', 'libLLVMLinker', 'libLLVMLTO', 'libLLVMMC', 'libLLVMMCA', 'libLLVMMCDisassembler', 'libLLVMMCJIT', 'libLLVMMCParser', 'libLLVMMIRParser', 'libLLVMObjCARCOpts', 'libLLVMObjCopy', 'libLLVMObject', 'libLLVMObjectYAML', 'libLLVMOption', 'libLLVMOrcDebugging', 'libLLVMOrcJIT', 'libLLVMOrcShared', 'libLLVMOrcTargetProcess', 'libLLVMPasses', 'libLLVMProfileData', 'libLLVMRemarks', 'libLLVMRuntimeDyld', 'libLLVMScalarOpts', 'libLLVMSelectionDAG', 'libLLVMSupport', 'libLLVMSymbolize', 'libLLVMTableGen', 'libLLVMTableGenCommon', 'libLLVMTarget', 'libLLVMTargetParser', 'libLLVMTextAPI', 'libLLVMTextAPIBinaryReader', 'libLLVMTransformUtils', 'libLLVMVectorize', 'libLLVMWindowsDriver', 'libLLVMWindowsManifest', 'libLLVMX86AsmParser', 'libLLVMX86CodeGen', 'libLLVMX86Desc', 'libLLVMX86Disassembler', 'libLLVMX86Info', 'libLLVMX86TargetMCA', 'libLLVMXRay']
  _deps += cpp.find_library(d, dirs : _search)
endforeach

dep_llvm = declare_dependency(
  include_directories : include_directories('$LLVM_INSTALL_PREFIX/include'),
  dependencies : _deps,
  version : '$(sed -n -e 's/^#define LLVM_VERSION_STRING "\([^"]*\)".*/\1/p' "${LLVM_INSTALL_PREFIX}/include/llvm/Config/llvm-config.h" )',
)

has_rtti = false
irbuilder_h = files('$LLVM_INSTALL_PREFIX/include/llvm/IR/IRBuilder.h')
EOF
