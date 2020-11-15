"""llvm

Tool-specific initialization for LLVM

"""

#
# Copyright (c) 2009 VMware, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
# KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
# WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#

import os
import os.path
import re
import platform as host_platform
import sys
import distutils.version

import SCons.Errors
import SCons.Util


required_llvm_version = '3.9'


def generate(env):
    env['llvm'] = False

    try:
        llvm_dir = os.environ['LLVM']
    except KeyError:
        # Do nothing -- use the system headers/libs
        llvm_dir = None
    else:
        if not os.path.isdir(llvm_dir):
            raise SCons.Errors.InternalError("Specified LLVM directory not found")

        if env['debug']:
            llvm_subdir = 'Debug'
        else:
            llvm_subdir = 'Release'

        llvm_bin_dir = os.path.join(llvm_dir, llvm_subdir, 'bin')
        if not os.path.isdir(llvm_bin_dir):
            llvm_bin_dir = os.path.join(llvm_dir, 'bin')
            if not os.path.isdir(llvm_bin_dir):
                raise SCons.Errors.InternalError("LLVM binary directory not found")

        env.PrependENVPath('PATH', llvm_bin_dir)

    if env['platform'] == 'windows':
        # XXX: There is no llvm-config on Windows, so assume a standard layout
        if llvm_dir is None:
            print('scons: LLVM environment variable must be specified when building for windows')
            return

        # Try to determine the LLVM version from llvm/Config/config.h
        llvm_config = os.path.join(llvm_dir, 'include/llvm/Config/llvm-config.h')
        if not os.path.exists(llvm_config):
            print('scons: could not find %s' % llvm_config)
            return
        llvm_version_major_re = re.compile(r'^#define LLVM_VERSION_MAJOR ([0-9]+)')
        llvm_version_minor_re = re.compile(r'^#define LLVM_VERSION_MINOR ([0-9]+)')
        llvm_version = None
        llvm_version_major = None
        llvm_version_minor = None
        for line in open(llvm_config, 'rt'):
            mo = llvm_version_major_re.match(line)
            if mo:
                llvm_version_major = mo.group(1)
            mo = llvm_version_minor_re.match(line)
            if mo:
                llvm_version_minor = mo.group(1)
        if llvm_version_major is not None and llvm_version_minor is not None:
            llvm_version = distutils.version.LooseVersion('%s.%s' % (llvm_version_major, llvm_version_minor))

        if llvm_version is None:
            print('scons: could not determine the LLVM version from %s' % llvm_config)
            return
        if llvm_version < distutils.version.LooseVersion(required_llvm_version):
            print('scons: LLVM version %s found, but %s is required' % (llvm_version, required_llvm_version))
            return

        env.Prepend(CPPPATH = [os.path.join(llvm_dir, 'include')])
        env.Prepend(LIBPATH = [os.path.join(llvm_dir, 'lib')])

        # LLVM 5.0 and newer requires MinGW w/ pthreads due to use of std::thread and friends.
        if llvm_version >= distutils.version.LooseVersion('5.0') and env['crosscompile']:
            assert env['gcc']
            env.AppendUnique(CXXFLAGS = ['-posix'])

        # LIBS should match the output of `llvm-config --libs engine mcjit bitwriter x86asmprinter irreader` for LLVM<=7.0
        # and `llvm-config --libs engine coroutines` for LLVM>=8.0
        # LLVMAggressiveInstCombine library part of engine component since LLVM 6 is only needed by Mesa3D for LLVM>=8.
        # While not directly needed by Mesa3D, this library is needed by LLVMipo which is part of coroutines component.
        if llvm_version >= distutils.version.LooseVersion('11.0'):
            env.Prepend(LIBS = [
                'LLVMX86Disassembler', 'LLVMX86AsmParser',
                'LLVMX86CodeGen', 'LLVMSelectionDAG', 'LLVMAsmPrinter',
                'LLVMDebugInfoCodeView', 'LLVMCodeGen',
                'LLVMScalarOpts', 'LLVMInstCombine',
                'LLVMTransformUtils',
                'LLVMBitWriter', 'LLVMX86Desc',
                'LLVMMCDisassembler', 'LLVMX86Info',
                'LLVMMCJIT', 'LLVMExecutionEngine', 'LLVMTarget',
                'LLVMAnalysis', 'LLVMProfileData',
                'LLVMRuntimeDyld', 'LLVMObject', 'LLVMMCParser',
                'LLVMBitReader', 'LLVMMC', 'LLVMCore',
                'LLVMSupport',
                'LLVMIRReader', 'LLVMAsmParser',
                'LLVMDemangle', 'LLVMGlobalISel', 'LLVMDebugInfoMSF',
                'LLVMBinaryFormat',
                'LLVMRemarks', 'LLVMBitstreamReader', 'LLVMDebugInfoDWARF',
                'LLVMAggressiveInstCombine','LLVMLinker', 'LLVMVectorize',
                'LLVMInstrumentation', 'LLVMipo', 'LLVMCoroutines',
                'LLVMCFGuard', 'LLVMTextAPI',
                'LLVMFrontendOpenMP',
            ])
        elif llvm_version >= distutils.version.LooseVersion('10.0'):
            env.Prepend(LIBS = [
                'LLVMX86Disassembler', 'LLVMX86AsmParser',
                'LLVMX86CodeGen', 'LLVMSelectionDAG', 'LLVMAsmPrinter',
                'LLVMDebugInfoCodeView', 'LLVMCodeGen',
                'LLVMScalarOpts', 'LLVMInstCombine',
                'LLVMTransformUtils',
                'LLVMBitWriter', 'LLVMX86Desc',
                'LLVMMCDisassembler', 'LLVMX86Info',
                'LLVMX86Utils',
                'LLVMMCJIT', 'LLVMExecutionEngine', 'LLVMTarget',
                'LLVMAnalysis', 'LLVMProfileData',
                'LLVMRuntimeDyld', 'LLVMObject', 'LLVMMCParser',
                'LLVMBitReader', 'LLVMMC', 'LLVMCore',
                'LLVMSupport',
                'LLVMIRReader', 'LLVMAsmParser',
                'LLVMDemangle', 'LLVMGlobalISel', 'LLVMDebugInfoMSF',
                'LLVMBinaryFormat',
                'LLVMRemarks', 'LLVMBitstreamReader', 'LLVMDebugInfoDWARF',
                'LLVMAggressiveInstCombine','LLVMLinker', 'LLVMVectorize',
                'LLVMInstrumentation', 'LLVMipo', 'LLVMCoroutines',
                'LLVMCFGuard', 'LLVMTextAPI',
            ])
        elif llvm_version >= distutils.version.LooseVersion('9.0'):
            env.Prepend(LIBS = [
                'LLVMX86Disassembler', 'LLVMX86AsmParser',
                'LLVMX86CodeGen', 'LLVMSelectionDAG', 'LLVMAsmPrinter',
                'LLVMDebugInfoCodeView', 'LLVMCodeGen',
                'LLVMScalarOpts', 'LLVMInstCombine',
                'LLVMTransformUtils',
                'LLVMBitWriter', 'LLVMX86Desc',
                'LLVMMCDisassembler', 'LLVMX86Info',
                'LLVMX86Utils',
                'LLVMMCJIT', 'LLVMExecutionEngine', 'LLVMTarget',
                'LLVMAnalysis', 'LLVMProfileData',
                'LLVMRuntimeDyld', 'LLVMObject', 'LLVMMCParser',
                'LLVMBitReader', 'LLVMMC', 'LLVMCore',
                'LLVMSupport',
                'LLVMIRReader', 'LLVMAsmParser',
                'LLVMDemangle', 'LLVMGlobalISel', 'LLVMDebugInfoMSF',
                'LLVMBinaryFormat',
                'LLVMRemarks', 'LLVMBitstreamReader', 'LLVMDebugInfoDWARF',
                # Add these libraries to enable ompute shaders support.
                'LLVMAggressiveInstCombine','LLVMLinker', 'LLVMVectorize',
                'LLVMInstrumentation', 'LLVMipo', 'LLVMCoroutines',
            ])
        elif llvm_version >= distutils.version.LooseVersion('8.0'):
            env.Prepend(LIBS = [
                'LLVMX86Disassembler', 'LLVMX86AsmParser',
                'LLVMX86CodeGen', 'LLVMSelectionDAG', 'LLVMAsmPrinter',
                'LLVMDebugInfoCodeView', 'LLVMCodeGen',
                'LLVMScalarOpts', 'LLVMInstCombine',
                'LLVMTransformUtils',
                'LLVMBitWriter', 'LLVMX86Desc',
                'LLVMMCDisassembler', 'LLVMX86Info',
                'LLVMX86AsmPrinter', 'LLVMX86Utils',
                'LLVMMCJIT', 'LLVMExecutionEngine', 'LLVMTarget',
                'LLVMAnalysis', 'LLVMProfileData',
                'LLVMRuntimeDyld', 'LLVMObject', 'LLVMMCParser',
                'LLVMBitReader', 'LLVMMC', 'LLVMCore',
                'LLVMSupport',
                'LLVMIRReader', 'LLVMAsmParser',
                'LLVMDemangle', 'LLVMGlobalISel', 'LLVMDebugInfoMSF',
                'LLVMBinaryFormat',
                # Add these libraries to enable ompute shaders support.
                'LLVMAggressiveInstCombine', 'LLVMLinker', 'LLVMVectorize',
                'LLVMInstrumentation', 'LLVMipo', 'LLVMCoroutines',
            ])
        elif llvm_version >= distutils.version.LooseVersion('5.0'):
            env.Prepend(LIBS = [
                'LLVMX86Disassembler', 'LLVMX86AsmParser',
                'LLVMX86CodeGen', 'LLVMSelectionDAG', 'LLVMAsmPrinter',
                'LLVMDebugInfoCodeView', 'LLVMCodeGen',
                'LLVMScalarOpts', 'LLVMInstCombine',
                'LLVMTransformUtils',
                'LLVMBitWriter', 'LLVMX86Desc',
                'LLVMMCDisassembler', 'LLVMX86Info',
                'LLVMX86AsmPrinter', 'LLVMX86Utils',
                'LLVMMCJIT', 'LLVMExecutionEngine', 'LLVMTarget',
                'LLVMAnalysis', 'LLVMProfileData',
                'LLVMRuntimeDyld', 'LLVMObject', 'LLVMMCParser',
                'LLVMBitReader', 'LLVMMC', 'LLVMCore',
                'LLVMSupport',
                'LLVMIRReader', 'LLVMAsmParser',
                'LLVMDemangle', 'LLVMGlobalISel', 'LLVMDebugInfoMSF',
                'LLVMBinaryFormat',
            ])
        elif llvm_version >= distutils.version.LooseVersion('4.0'):
            env.Prepend(LIBS = [
                'LLVMX86Disassembler', 'LLVMX86AsmParser',
                'LLVMX86CodeGen', 'LLVMSelectionDAG', 'LLVMAsmPrinter',
                'LLVMDebugInfoCodeView', 'LLVMCodeGen',
                'LLVMScalarOpts', 'LLVMInstCombine',
                'LLVMTransformUtils',
                'LLVMBitWriter', 'LLVMX86Desc',
                'LLVMMCDisassembler', 'LLVMX86Info',
                'LLVMX86AsmPrinter', 'LLVMX86Utils',
                'LLVMMCJIT', 'LLVMExecutionEngine', 'LLVMTarget',
                'LLVMAnalysis', 'LLVMProfileData',
                'LLVMRuntimeDyld', 'LLVMObject', 'LLVMMCParser',
                'LLVMBitReader', 'LLVMMC', 'LLVMCore',
                'LLVMSupport',
                'LLVMIRReader', 'LLVMAsmParser',
                'LLVMDemangle', 'LLVMGlobalISel', 'LLVMDebugInfoMSF',
            ])
        else:
            env.Prepend(LIBS = [
                'LLVMX86Disassembler', 'LLVMX86AsmParser',
                'LLVMX86CodeGen', 'LLVMSelectionDAG', 'LLVMAsmPrinter',
                'LLVMDebugInfoCodeView', 'LLVMCodeGen',
                'LLVMScalarOpts', 'LLVMInstCombine',
                'LLVMInstrumentation', 'LLVMTransformUtils',
                'LLVMBitWriter', 'LLVMX86Desc',
                'LLVMMCDisassembler', 'LLVMX86Info',
                'LLVMX86AsmPrinter', 'LLVMX86Utils',
                'LLVMMCJIT', 'LLVMExecutionEngine', 'LLVMTarget',
                'LLVMAnalysis', 'LLVMProfileData',
                'LLVMRuntimeDyld', 'LLVMObject', 'LLVMMCParser',
                'LLVMBitReader', 'LLVMMC', 'LLVMCore',
                'LLVMSupport',
                'LLVMIRReader', 'LLVMASMParser'
            ])
        env.Append(LIBS = [
            'imagehlp',
            'psapi',
            'shell32',
            'advapi32',
            'ole32',
            'uuid',
        ])

        # Mingw-w64 zlib is required when building with LLVM support in MSYS2 environment
        if host_platform.system().lower().startswith('mingw'):
            env.Append(LIBS = [
                 'z',
            ])

        if env['msvc']:
            # Some of the LLVM C headers use the inline keyword without
            # defining it.
            env.Append(CPPDEFINES = [('inline', '__inline')])
            # Match some of the warning options from llvm/cmake/modules/HandleLLVMOptions.cmake
            env.AppendUnique(CXXFLAGS = [
                '/wd4355', # 'this' : used in base member initializer list
                '/wd4624', # 'derived class' : destructor could not be generated because a base class destructor is inaccessible
            ])
            if env['build'] in ('debug', 'checked'):
                # LLVM libraries are static, build with /MT, and they
                # automatically link agains LIBCMT. When we're doing a
                # debug build we'll be linking against LIBCMTD, so disable
                # that.
                env.Append(LINKFLAGS = ['/nodefaultlib:LIBCMT'])
    else:
        llvm_config = os.environ.get('LLVM_CONFIG', 'llvm-config')
        if not env.Detect(llvm_config):
            print('scons: %s script not found' % llvm_config)
            return

        llvm_version = env.backtick('%s --version' % llvm_config).rstrip()
        llvm_version = distutils.version.LooseVersion(llvm_version)

        if llvm_version < distutils.version.LooseVersion(required_llvm_version):
            print('scons: LLVM version %s found, but %s is required' % (llvm_version, required_llvm_version))
            return

        try:
            # Treat --cppflags specially to prevent NDEBUG from disabling
            # assertion failures in debug builds.
            cppflags = env.ParseFlags('!%s --cppflags' % llvm_config)
            try:
                cppflags['CPPDEFINES'].remove('NDEBUG')
            except ValueError:
                pass
            env.MergeFlags(cppflags)

            # Match llvm --fno-rtti flag
            cxxflags = env.backtick('%s --cxxflags' % llvm_config).split()
            if '-fno-rtti' in cxxflags:
                env.Append(CXXFLAGS = ['-fno-rtti'])

            if llvm_version < distutils.version.LooseVersion('9.0'):
               components = ['engine', 'mcjit', 'bitwriter', 'x86asmprinter', 'mcdisassembler', 'irreader']
            else:
               components = ['engine', 'mcjit', 'bitwriter', 'mcdisassembler', 'irreader']

            if llvm_version >= distutils.version.LooseVersion('8.0'):
                components.append('coroutines')

            env.ParseConfig('%s --libs ' % llvm_config + ' '.join(components))
            env.ParseConfig('%s --ldflags' % llvm_config)
            env.ParseConfig('%s --system-libs' % llvm_config)
            env.Append(CXXFLAGS = ['-std=c++14'])
        except OSError:
            print('scons: llvm-config version %s failed' % llvm_version)
            return

    assert llvm_version is not None
    env['llvm'] = True

    print('scons: Found LLVM version %s' % llvm_version)
    env['LLVM_VERSION'] = llvm_version

    # Define LLVM_AVAILABLE macro to guard code blocks, and MESA_LLVM_VERSION_STRING
    env.Prepend(CPPDEFINES = [('LLVM_AVAILABLE', 1)])
    env.Prepend(CPPDEFINES = [('MESA_LLVM_VERSION_STRING=\\"%s\\"' % llvm_version)])

def exists(env):
    return True

# vim:set ts=4 sw=4 et:
