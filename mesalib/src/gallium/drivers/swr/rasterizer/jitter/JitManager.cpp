/****************************************************************************
 * Copyright (C) 2014-2018 Intel Corporation.   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * @file JitManager.cpp
 *
 * @brief Implementation if the Jit Manager.
 *
 * Notes:
 *
 ******************************************************************************/
#include "jit_pch.hpp"

#include "JitManager.h"
#include "jit_api.h"
#include "fetch_jit.h"

#include "core/state.h"

#include "gen_state_llvm.h"

#include <sstream>
#if defined(_WIN32)
#include <psapi.h>
#include <cstring>

#define INTEL_OUTPUT_DIR "c:\\Intel"
#define SWR_OUTPUT_DIR INTEL_OUTPUT_DIR "\\SWR"
#define JITTER_OUTPUT_DIR SWR_OUTPUT_DIR "\\Jitter"
#endif // _WIN32

#if defined(__APPLE__) || defined(FORCE_LINUX) || defined(__linux__) || defined(__gnu_linux__)
#include <pwd.h>
#include <sys/stat.h>
#endif


using namespace llvm;
using namespace SwrJit;

//////////////////////////////////////////////////////////////////////////
/// @brief Contructor for JitManager.
/// @param simdWidth - SIMD width to be used in generated program.
JitManager::JitManager(uint32_t simdWidth, const char* arch, const char* core) :
    mContext(), mBuilder(mContext), mIsModuleFinalized(true), mJitNumber(0), mVWidth(simdWidth),
    mArch(arch)
{
    mpCurrentModule = nullptr;
    mpExec          = nullptr;

    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetDisassembler();


    // force JIT to use the same CPU arch as the rest of swr
    if (mArch.AVX512F())
    {
#if USE_SIMD16_SHADERS
        if (mArch.AVX512ER())
        {
            mHostCpuName = StringRef("knl");
        }
        else
        {
            mHostCpuName = StringRef("skylake-avx512");
        }
        mUsingAVX512 = true;
#else
        mHostCpuName = StringRef("core-avx2");
#endif
        if (mVWidth == 0)
        {
            mVWidth = 8;
        }
    }
    else if (mArch.AVX2())
    {
        mHostCpuName = StringRef("core-avx2");
        if (mVWidth == 0)
        {
            mVWidth = 8;
        }
    }
    else if (mArch.AVX())
    {
        if (mArch.F16C())
        {
            mHostCpuName = StringRef("core-avx-i");
        }
        else
        {
            mHostCpuName = StringRef("corei7-avx");
        }
        if (mVWidth == 0)
        {
            mVWidth = 8;
        }
    }
    else
    {
        SWR_INVALID("Jitting requires at least AVX ISA support");
    }


    mOptLevel = CodeGenOpt::Aggressive;

    if (KNOB_JIT_OPTIMIZATION_LEVEL >= CodeGenOpt::None &&
        KNOB_JIT_OPTIMIZATION_LEVEL <= CodeGenOpt::Aggressive)
    {
        mOptLevel = CodeGenOpt::Level(KNOB_JIT_OPTIMIZATION_LEVEL);
    }

    if (KNOB_JIT_ENABLE_CACHE)
    {
        mCache.Init(this, mHostCpuName, mOptLevel);
    }

    SetupNewModule();
    mIsModuleFinalized = true;

    // fetch function signature
#if USE_SIMD16_SHADERS
    // typedef void(__cdecl *PFN_FETCH_FUNC)(SWR_FETCH_CONTEXT& fetchInfo, simd16vertex& out);
#else
    // typedef void(__cdecl *PFN_FETCH_FUNC)(SWR_FETCH_CONTEXT& fetchInfo, simdvertex& out);
#endif
    std::vector<Type*> fsArgs;

    // llvm5 is picky and does not take a void * type
    fsArgs.push_back(PointerType::get(Gen_SWR_FETCH_CONTEXT(this), 0));

    fsArgs.push_back(Type::getInt8PtrTy(mContext));

    fsArgs.push_back(PointerType::get(Gen_SWR_FETCH_CONTEXT(this), 0));
#if USE_SIMD16_SHADERS
    fsArgs.push_back(PointerType::get(Gen_simd16vertex(this), 0));
#else
    fsArgs.push_back(PointerType::get(Gen_simdvertex(this), 0));
#endif

    mFetchShaderTy = FunctionType::get(Type::getVoidTy(mContext), fsArgs, false);

#if defined(_MSC_VER)
    // explicitly instantiate used symbols from potentially staticly linked libs
    sys::DynamicLibrary::AddSymbol("exp2f", &exp2f);
    sys::DynamicLibrary::AddSymbol("log2f", &log2f);
    sys::DynamicLibrary::AddSymbol("sinf", &sinf);
    sys::DynamicLibrary::AddSymbol("cosf", &cosf);
    sys::DynamicLibrary::AddSymbol("powf", &powf);
#endif

#if defined(_WIN32)
    if (KNOB_DUMP_SHADER_IR)
    {
        CreateDirectoryPath(INTEL_OUTPUT_DIR);
        CreateDirectoryPath(SWR_OUTPUT_DIR);
        CreateDirectoryPath(JITTER_OUTPUT_DIR);
    }
#endif
}

void JitManager::CreateExecEngine(std::unique_ptr<Module> pModule)
{
    TargetOptions tOpts;
    tOpts.AllowFPOpFusion = FPOpFusion::Fast;
    tOpts.NoInfsFPMath    = false;
    tOpts.NoNaNsFPMath    = false;
    tOpts.UnsafeFPMath = false;

    // tOpts.PrintMachineCode    = true;

    mpExec = EngineBuilder(std::move(pModule))
                 .setTargetOptions(tOpts)
                 .setOptLevel(mOptLevel)
                 .setMCPU(mHostCpuName)
                 .create();

    if (KNOB_JIT_ENABLE_CACHE)
    {
        mpExec->setObjectCache(&mCache);
    }

#if LLVM_USE_INTEL_JITEVENTS
    JITEventListener* vTune = JITEventListener::createIntelJITEventListener();
    mpExec->RegisterJITEventListener(vTune);
#endif

    mvExecEngines.push_back(mpExec);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Create new LLVM module.
void JitManager::SetupNewModule()
{
    SWR_ASSERT(mIsModuleFinalized == true && "Current module is not finalized!");

    std::unique_ptr<Module> newModule(new Module("", mContext));
    mpCurrentModule = newModule.get();
    mpCurrentModule->setTargetTriple(sys::getProcessTriple());
    CreateExecEngine(std::move(newModule));
    mIsModuleFinalized = false;
}


DIType*
JitManager::CreateDebugStructType(StructType*                                          pType,
                                  const std::string&                                   name,
                                  DIFile*                                              pFile,
                                  uint32_t                                             lineNum,
                                  const std::vector<std::pair<std::string, uint32_t>>& members)
{
    DIBuilder                 builder(*mpCurrentModule);
    SmallVector<Metadata*, 8> ElemTypes;
    DataLayout                DL        = DataLayout(mpCurrentModule);
    uint32_t                  size      = DL.getTypeAllocSizeInBits(pType);
    uint32_t                  alignment = DL.getABITypeAlignment(pType);
    DINode::DIFlags           flags     = DINode::DIFlags::FlagPublic;

    DICompositeType* pDIStructTy = builder.createStructType(pFile,
                                                            name,
                                                            pFile,
                                                            lineNum,
                                                            size,
                                                            alignment,
                                                            flags,
                                                            nullptr,
                                                            builder.getOrCreateArray(ElemTypes));

    // Register mapping now to break loops (in case struct contains itself or pointers to itself)
    mDebugStructMap[pType] = pDIStructTy;

    uint32_t idx = 0;
    for (auto& elem : pType->elements())
    {
        std::string name       = members[idx].first;
        uint32_t    lineNum    = members[idx].second;
        size                   = DL.getTypeAllocSizeInBits(elem);
        alignment              = DL.getABITypeAlignment(elem);
        uint32_t      offset   = DL.getStructLayout(pType)->getElementOffsetInBits(idx);
        llvm::DIType* pDebugTy = GetDebugType(elem);
        ElemTypes.push_back(builder.createMemberType(
            pDIStructTy, name, pFile, lineNum, size, alignment, offset, flags, pDebugTy));

        idx++;
    }

    pDIStructTy->replaceElements(builder.getOrCreateArray(ElemTypes));
    return pDIStructTy;
}

DIType* JitManager::GetDebugArrayType(Type* pTy)
{
    DIBuilder  builder(*mpCurrentModule);
    DataLayout DL        = DataLayout(mpCurrentModule);
    ArrayType* pArrayTy  = cast<ArrayType>(pTy);
    uint32_t   size      = DL.getTypeAllocSizeInBits(pArrayTy);
    uint32_t   alignment = DL.getABITypeAlignment(pArrayTy);

    SmallVector<Metadata*, 8> Elems;
    Elems.push_back(builder.getOrCreateSubrange(0, pArrayTy->getNumElements()));
    return builder.createArrayType(
        size, alignment, GetDebugType(pArrayTy->getElementType()), builder.getOrCreateArray(Elems));
}

// Create a DIType from llvm Type
DIType* JitManager::GetDebugType(Type* pTy)
{
    DIBuilder    builder(*mpCurrentModule);
    Type::TypeID id = pTy->getTypeID();

    switch (id)
    {
    case Type::VoidTyID:
        return builder.createUnspecifiedType("void");
        break;
    case Type::HalfTyID:
        return builder.createBasicType("float16", 16, dwarf::DW_ATE_float);
        break;
    case Type::FloatTyID:
        return builder.createBasicType("float", 32, dwarf::DW_ATE_float);
        break;
    case Type::DoubleTyID:
        return builder.createBasicType("double", 64, dwarf::DW_ATE_float);
        break;
    case Type::IntegerTyID:
        return GetDebugIntegerType(pTy);
        break;
    case Type::StructTyID:
        return GetDebugStructType(pTy);
        break;
    case Type::ArrayTyID:
        return GetDebugArrayType(pTy);
        break;
    case Type::PointerTyID:
        return builder.createPointerType(GetDebugType(pTy->getPointerElementType()), 64, 64);
        break;
#if LLVM_VERSION_MAJOR >= 11
    case Type::FixedVectorTyID:
#else
    case Type::VectorTyID:
#endif
        return GetDebugVectorType(pTy);
        break;
    case Type::FunctionTyID:
        return GetDebugFunctionType(pTy);
        break;
    default:
        SWR_ASSERT(false, "Unimplemented llvm type");
    }
    return nullptr;
}

// Create a DISubroutineType from an llvm FunctionType
DIType* JitManager::GetDebugFunctionType(Type* pTy)
{
    SmallVector<Metadata*, 8> ElemTypes;
    FunctionType*             pFuncTy = cast<FunctionType>(pTy);
    DIBuilder                 builder(*mpCurrentModule);

    // Add result type
    ElemTypes.push_back(GetDebugType(pFuncTy->getReturnType()));

    // Add arguments
    for (auto& param : pFuncTy->params())
    {
        ElemTypes.push_back(GetDebugType(param));
    }

    return builder.createSubroutineType(builder.getOrCreateTypeArray(ElemTypes));
}

DIType* JitManager::GetDebugIntegerType(Type* pTy)
{
    DIBuilder    builder(*mpCurrentModule);
    IntegerType* pIntTy = cast<IntegerType>(pTy);
    switch (pIntTy->getBitWidth())
    {
    case 1:
        return builder.createBasicType("int1", 1, dwarf::DW_ATE_unsigned);
        break;
    case 8:
        return builder.createBasicType("int8", 8, dwarf::DW_ATE_signed);
        break;
    case 16:
        return builder.createBasicType("int16", 16, dwarf::DW_ATE_signed);
        break;
    case 32:
        return builder.createBasicType("int", 32, dwarf::DW_ATE_signed);
        break;
    case 64:
        return builder.createBasicType("int64", 64, dwarf::DW_ATE_signed);
        break;
    case 128:
        return builder.createBasicType("int128", 128, dwarf::DW_ATE_signed);
        break;
    default:
        SWR_ASSERT(false, "Unimplemented integer bit width");
    }
    return nullptr;
}

DIType* JitManager::GetDebugVectorType(Type* pTy)
{
    DIBuilder                 builder(*mpCurrentModule);
#if LLVM_VERSION_MAJOR >= 12
    FixedVectorType*          pVecTy    = cast<FixedVectorType>(pTy);
#elif LLVM_VERSION_MAJOR >= 11
    VectorType*               pVecTy    = cast<VectorType>(pTy);
#else
    auto                      pVecTy    = pTy;
#endif
    DataLayout                DL        = DataLayout(mpCurrentModule);
    uint32_t                  size      = DL.getTypeAllocSizeInBits(pVecTy);
    uint32_t                  alignment = DL.getABITypeAlignment(pVecTy);
    SmallVector<Metadata*, 1> Elems;

#if LLVM_VERSION_MAJOR >= 11
    Elems.push_back(builder.getOrCreateSubrange(0, pVecTy->getNumElements()));
#else
    Elems.push_back(builder.getOrCreateSubrange(0, pVecTy->getVectorNumElements()));
#endif

    return builder.createVectorType(size,
                                    alignment,
#if LLVM_VERSION_MAJOR >= 11
                                    GetDebugType(pVecTy->getElementType()),
#else
                                    GetDebugType(pVecTy->getVectorElementType()),
#endif
                                    builder.getOrCreateArray(Elems));
}

//////////////////////////////////////////////////////////////////////////
/// @brief Dump function x86 assembly to file.
/// @note This should only be called after the module has been jitted to x86 and the
///       module will not be further accessed.
void JitManager::DumpAsm(Function* pFunction, const char* fileName)
{
    if (KNOB_DUMP_SHADER_IR)
    {
#if defined(_WIN32)
        DWORD pid = GetCurrentProcessId();
        char  procname[MAX_PATH];
        GetModuleFileNameA(NULL, procname, MAX_PATH);
        const char*       pBaseName = strrchr(procname, '\\');
        std::stringstream outDir;
        outDir << JITTER_OUTPUT_DIR << pBaseName << "_" << pid << std::ends;
        CreateDirectoryPath(outDir.str().c_str());
#endif

        std::error_code EC;
        Module*         pModule  = pFunction->getParent();
        const char*     funcName = pFunction->getName().data();
        char            fName[256];
#if defined(_WIN32)
        sprintf(fName, "%s\\%s.%s.asm", outDir.str().c_str(), funcName, fileName);
#else
        sprintf(fName, "%s.%s.asm", funcName, fileName);
#endif

        raw_fd_ostream filestream(fName, EC, llvm::sys::fs::F_None);

        legacy::PassManager* pMPasses         = new legacy::PassManager();
        auto*                pTarget          = mpExec->getTargetMachine();
        pTarget->Options.MCOptions.AsmVerbose = true;
#if LLVM_VERSION_MAJOR >= 10
        pTarget->addPassesToEmitFile(
            *pMPasses, filestream, nullptr, CGFT_AssemblyFile);
#elif LLVM_VERSION_MAJOR >= 7
        pTarget->addPassesToEmitFile(
            *pMPasses, filestream, nullptr, TargetMachine::CGFT_AssemblyFile);
#else
        pTarget->addPassesToEmitFile(*pMPasses, filestream, TargetMachine::CGFT_AssemblyFile);
#endif
        pMPasses->run(*pModule);
        delete pMPasses;
        pTarget->Options.MCOptions.AsmVerbose = false;
    }
}

std::string JitManager::GetOutputDir()
{
#if defined(_WIN32)
    DWORD pid = GetCurrentProcessId();
    char  procname[MAX_PATH];
    GetModuleFileNameA(NULL, procname, MAX_PATH);
    const char*       pBaseName = strrchr(procname, '\\');
    std::stringstream outDir;
    outDir << JITTER_OUTPUT_DIR << pBaseName << "_" << pid;
    CreateDirectoryPath(outDir.str().c_str());
    return outDir.str();
#endif
    return "";
}

//////////////////////////////////////////////////////////////////////////
/// @brief Dump function to file.
void JitManager::DumpToFile(Module*                         M,
                            const char*                     fileName,
                            llvm::AssemblyAnnotationWriter* annotater)
{
    if (KNOB_DUMP_SHADER_IR)
    {
        std::string outDir = GetOutputDir();

        std::error_code EC;
        const char*     funcName = M->getName().data();
        char            fName[256];
#if defined(_WIN32)
        sprintf(fName, "%s\\%s.%s.ll", outDir.c_str(), funcName, fileName);
#else
        sprintf(fName, "%s.%s.ll", funcName, fileName);
#endif
        raw_fd_ostream fd(fName, EC, llvm::sys::fs::F_None);
        M->print(fd, annotater);
        fd.flush();
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Dump function to file.
void JitManager::DumpToFile(Function* f, const char* fileName)
{
    if (KNOB_DUMP_SHADER_IR)
    {
        std::string outDir = GetOutputDir();

        std::error_code EC;
        const char*     funcName = f->getName().data();
        char            fName[256];
#if defined(_WIN32)
        sprintf(fName, "%s\\%s.%s.ll", outDir.c_str(), funcName, fileName);
#else
        sprintf(fName, "%s.%s.ll", funcName, fileName);
#endif
        raw_fd_ostream fd(fName, EC, llvm::sys::fs::F_None);
        f->print(fd, nullptr);

#if defined(_WIN32)
        sprintf(fName, "%s\\cfg.%s.%s.dot", outDir.c_str(), funcName, fileName);
#else
        sprintf(fName, "cfg.%s.%s.dot", funcName, fileName);
#endif
        fd.flush();

        raw_fd_ostream fd_cfg(fName, EC, llvm::sys::fs::F_Text);
        WriteGraph(fd_cfg, (const Function*)f);

        fd_cfg.flush();
    }
}

extern "C" {
bool g_DllActive = true;

//////////////////////////////////////////////////////////////////////////
/// @brief Create JIT context.
/// @param simdWidth - SIMD width to be used in generated program.
HANDLE JITCALL JitCreateContext(uint32_t targetSimdWidth, const char* arch, const char* core)
{
    return new JitManager(targetSimdWidth, arch, core);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Destroy JIT context.
void JITCALL JitDestroyContext(HANDLE hJitContext)
{
    if (g_DllActive)
    {
        delete reinterpret_cast<JitManager*>(hJitContext);
    }
}
}

//////////////////////////////////////////////////////////////////////////
/// JitCache
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
/// JitCacheFileHeader
//////////////////////////////////////////////////////////////////////////
struct JitCacheFileHeader
{
    void Init(uint32_t           llCRC,
              uint32_t           objCRC,
              const std::string& moduleID,
              const std::string& cpu,
              uint32_t           optLevel,
              uint64_t           objSize)
    {
        m_objSize = objSize;
        m_llCRC   = llCRC;
        m_objCRC  = objCRC;
        strncpy(m_ModuleID, moduleID.c_str(), JC_STR_MAX_LEN - 1);
        m_ModuleID[JC_STR_MAX_LEN - 1] = 0;
        strncpy(m_Cpu, cpu.c_str(), JC_STR_MAX_LEN - 1);
        m_Cpu[JC_STR_MAX_LEN - 1] = 0;
        m_optLevel                = optLevel;
    }


    bool
    IsValid(uint32_t llCRC, const std::string& moduleID, const std::string& cpu, uint32_t optLevel)
    {
        if ((m_MagicNumber != JC_MAGIC_NUMBER) || (m_llCRC != llCRC) ||
            (m_platformKey != JC_PLATFORM_KEY) || (m_optLevel != optLevel))
        {
            return false;
        }

        m_ModuleID[JC_STR_MAX_LEN - 1] = 0;
        if (strncmp(moduleID.c_str(), m_ModuleID, JC_STR_MAX_LEN - 1))
        {
            return false;
        }

        m_Cpu[JC_STR_MAX_LEN - 1] = 0;
        if (strncmp(cpu.c_str(), m_Cpu, JC_STR_MAX_LEN - 1))
        {
            return false;
        }

        return true;
    }

    uint64_t GetObjectSize() const { return m_objSize; }
    uint64_t GetObjectCRC() const { return m_objCRC; }

private:
    static const uint64_t JC_MAGIC_NUMBER = 0xfedcba9876543210ULL + 7;
    static const size_t   JC_STR_MAX_LEN  = 32;
    static const uint32_t JC_PLATFORM_KEY = (LLVM_VERSION_MAJOR << 24) |
                                            (LLVM_VERSION_MINOR << 16) | (LLVM_VERSION_PATCH << 8) |
                                            ((sizeof(void*) > sizeof(uint32_t)) ? 1 : 0);

    uint64_t m_MagicNumber              = JC_MAGIC_NUMBER;
    uint64_t m_objSize                  = 0;
    uint32_t m_llCRC                    = 0;
    uint32_t m_platformKey              = JC_PLATFORM_KEY;
    uint32_t m_objCRC                   = 0;
    uint32_t m_optLevel                 = 0;
    char     m_ModuleID[JC_STR_MAX_LEN] = {};
    char     m_Cpu[JC_STR_MAX_LEN]      = {};
};

static inline uint32_t ComputeModuleCRC(const llvm::Module* M)
{
    std::string        bitcodeBuffer;
    raw_string_ostream bitcodeStream(bitcodeBuffer);

#if LLVM_VERSION_MAJOR >= 7
    llvm::WriteBitcodeToFile(*M, bitcodeStream);
#else
    llvm::WriteBitcodeToFile(M, bitcodeStream);
#endif
    // M->print(bitcodeStream, nullptr, false);

    bitcodeStream.flush();

    return ComputeCRC(0, bitcodeBuffer.data(), bitcodeBuffer.size());
}

/// constructor
JitCache::JitCache()
{
#if defined(__APPLE__) || defined(FORCE_LINUX) || defined(__linux__) || defined(__gnu_linux__)
    if (strncmp(KNOB_JIT_CACHE_DIR.c_str(), "~/", 2) == 0)
    {
        char* homedir;
        if (!(homedir = getenv("HOME")))
        {
            homedir = getpwuid(getuid())->pw_dir;
        }
        mCacheDir = homedir;
        mCacheDir += (KNOB_JIT_CACHE_DIR.c_str() + 1);
    }
    else
#endif
    {
        mCacheDir = KNOB_JIT_CACHE_DIR;
    }

    // Create cache dir at startup to allow jitter to write debug.ll files
    // to that directory.
    if (!llvm::sys::fs::exists(mCacheDir.str()) &&
        llvm::sys::fs::create_directories(mCacheDir.str()))
    {
        SWR_INVALID("Unable to create directory: %s", mCacheDir.c_str());
    }

}

int ExecUnhookedProcess(const std::string& CmdLine, std::string* pStdOut, std::string* pStdErr)
{

    return ExecCmd(CmdLine, nullptr, pStdOut, pStdErr);
}

/// Calculate actual directory where module will be cached.
/// This is always a subdirectory of mCacheDir.  Full absolute
/// path name will be stored in mCurrentModuleCacheDir
void JitCache::CalcModuleCacheDir()
{
    mModuleCacheDir.clear();

    llvm::SmallString<MAX_PATH> moduleDir = mCacheDir;

    // Create 4 levels of directory hierarchy based on CRC, 256 entries each
    uint8_t* pCRC = (uint8_t*)&mCurrentModuleCRC;
    for (uint32_t i = 0; i < 4; ++i)
    {
        llvm::sys::path::append(moduleDir, std::to_string((int)pCRC[i]));
    }

    mModuleCacheDir = moduleDir;
}

/// notifyObjectCompiled - Provides a pointer to compiled code for Module M.
void JitCache::notifyObjectCompiled(const llvm::Module* M, llvm::MemoryBufferRef Obj)
{
    const std::string& moduleID = M->getModuleIdentifier();
    if (!moduleID.length())
    {
        return;
    }

    if (!mModuleCacheDir.size())
    {
        SWR_INVALID("Unset module cache directory");
        return;
    }

    if (!llvm::sys::fs::exists(mModuleCacheDir.str()) &&
        llvm::sys::fs::create_directories(mModuleCacheDir.str()))
    {
        SWR_INVALID("Unable to create directory: %s", mModuleCacheDir.c_str());
        return;
    }

    JitCacheFileHeader header;

    llvm::SmallString<MAX_PATH> filePath = mModuleCacheDir;
    llvm::sys::path::append(filePath, moduleID);

    llvm::SmallString<MAX_PATH> objPath = filePath;
    objPath += JIT_OBJ_EXT;

    {
        std::error_code      err;
        llvm::raw_fd_ostream fileObj(objPath.c_str(), err, llvm::sys::fs::F_None);
        fileObj << Obj.getBuffer();
        fileObj.flush();
    }


    {
        std::error_code      err;
        llvm::raw_fd_ostream fileObj(filePath.c_str(), err, llvm::sys::fs::F_None);

        uint32_t objcrc = ComputeCRC(0, Obj.getBufferStart(), Obj.getBufferSize());

        header.Init(mCurrentModuleCRC, objcrc, moduleID, mCpu, mOptLevel, Obj.getBufferSize());

        fileObj.write((const char*)&header, sizeof(header));
        fileObj.flush();
    }
}

/// Returns a pointer to a newly allocated MemoryBuffer that contains the
/// object which corresponds with Module M, or 0 if an object is not
/// available.
std::unique_ptr<llvm::MemoryBuffer> JitCache::getObject(const llvm::Module* M)
{
    const std::string& moduleID = M->getModuleIdentifier();
    mCurrentModuleCRC           = ComputeModuleCRC(M);

    if (!moduleID.length())
    {
        return nullptr;
    }

    CalcModuleCacheDir();

    if (!llvm::sys::fs::exists(mModuleCacheDir))
    {
        return nullptr;
    }

    llvm::SmallString<MAX_PATH> filePath = mModuleCacheDir;
    llvm::sys::path::append(filePath, moduleID);

    llvm::SmallString<MAX_PATH> objFilePath = filePath;
    objFilePath += JIT_OBJ_EXT;

    FILE* fpObjIn = nullptr;
    FILE* fpIn    = fopen(filePath.c_str(), "rb");
    if (!fpIn)
    {
        return nullptr;
    }

    std::unique_ptr<llvm::MemoryBuffer> pBuf = nullptr;
    do
    {
        JitCacheFileHeader header;
        if (!fread(&header, sizeof(header), 1, fpIn))
        {
            break;
        }

        if (!header.IsValid(mCurrentModuleCRC, moduleID, mCpu, mOptLevel))
        {
            break;
        }

        fpObjIn = fopen(objFilePath.c_str(), "rb");
        if (!fpObjIn)
        {
            break;
        }

#if LLVM_VERSION_MAJOR < 6
        pBuf = llvm::MemoryBuffer::getNewUninitMemBuffer(size_t(header.GetObjectSize()));
#else
        pBuf = llvm::WritableMemoryBuffer::getNewUninitMemBuffer(size_t(header.GetObjectSize()));
#endif
        if (!fread(const_cast<char*>(pBuf->getBufferStart()), header.GetObjectSize(), 1, fpObjIn))
        {
            pBuf = nullptr;
            break;
        }

        if (header.GetObjectCRC() != ComputeCRC(0, pBuf->getBufferStart(), pBuf->getBufferSize()))
        {
            SWR_TRACE("Invalid object cache file, ignoring: %s", filePath.c_str());
            pBuf = nullptr;
            break;
        }

    } while (0);

    fclose(fpIn);

    if (fpObjIn)
    {
        fclose(fpObjIn);
    }


    return pBuf;
}

void InterleaveAssemblyAnnotater::emitInstructionAnnot(const llvm::Instruction*     pInst,
                                                       llvm::formatted_raw_ostream& OS)
{
    auto dbgLoc = pInst->getDebugLoc();
    if (dbgLoc)
    {
        unsigned int line = dbgLoc.getLine();
        if (line != mCurrentLineNo)
        {
            if (line > 0 && line <= mAssembly.size())
            {
                // HACK: here we assume that OS is a formatted_raw_ostream(ods())
                // and modify the color accordingly. We can't do the color
                // modification on OS because formatted_raw_ostream strips
                // the color information. The only way to fix this behavior
                // is to patch LLVM.
                OS << "\n; " << line << ": " << mAssembly[line - 1] << "\n";
            }
            mCurrentLineNo = line;
        }
    }
}
