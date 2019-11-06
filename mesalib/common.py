#######################################################################
# Common SCons code

import os
import os.path
import re
import subprocess
import sys
import platform as _platform

import SCons.Script.SConscript


#######################################################################
# Defaults

host_platform = _platform.system().lower()
if host_platform.startswith('cygwin'):
    host_platform = 'cygwin'
# MSYS2 default platform selection.
if host_platform.startswith('mingw'):
    host_platform = 'windows'

# Search sys.argv[] for a "platform=foo" argument since we don't have
# an 'env' variable at this point.
if 'platform' in SCons.Script.ARGUMENTS:
    target_platform = SCons.Script.ARGUMENTS['platform']
else:
    target_platform = host_platform

_machine_map = {
    'x86': 'x86',
    'i386': 'x86',
    'i486': 'x86',
    'i586': 'x86',
    'i686': 'x86',
    'BePC': 'x86',
    'Intel': 'x86',
    'ppc': 'ppc',
    'BeBox': 'ppc',
    'BeMac': 'ppc',
    'AMD64': 'x86_64',
    'x86_64': 'x86_64',
    'sparc': 'sparc',
    'sun4u': 'sparc',
}


# find host_machine value
if 'PROCESSOR_ARCHITECTURE' in os.environ:
    host_machine = os.environ['PROCESSOR_ARCHITECTURE']
else:
    host_machine = _platform.machine()
host_machine = _machine_map.get(host_machine, 'generic')
# MSYS2 default machine selection.
if _platform.system().lower().startswith('mingw') and 'MSYSTEM' in os.environ:
    if os.environ['MSYSTEM'] == 'MINGW32':
        host_machine = 'x86'
    if os.environ['MSYSTEM'] == 'MINGW64':
        host_machine = 'x86_64'

default_machine = host_machine
default_toolchain = 'default'
# MSYS2 default toolchain selection.
if _platform.system().lower().startswith('mingw'):
    default_toolchain = 'mingw'

if target_platform == 'windows' and host_platform != 'windows':
    default_machine = 'x86'
    default_toolchain = 'crossmingw'


# find default_llvm value
if 'LLVM' in os.environ or 'LLVM_CONFIG' in os.environ:
    default_llvm = 'yes'
else:
    default_llvm = 'no'
    try:
        if target_platform != 'windows' and \
           subprocess.call(['llvm-config', '--version'],
                           stdout=subprocess.PIPE) == 0:
            default_llvm = 'yes'
    except:
        pass


#######################################################################
# Common options

def AddOptions(opts):
    try:
        from SCons.Variables.BoolVariable import BoolVariable as BoolOption
    except ImportError:
        from SCons.Options.BoolOption import BoolOption
    try:
        from SCons.Variables.EnumVariable import EnumVariable as EnumOption
    except ImportError:
        from SCons.Options.EnumOption import EnumOption
    opts.Add(EnumOption('build', 'build type', 'debug',
                        allowed_values=('debug', 'checked', 'profile',
                                        'release')))
    opts.Add(BoolOption('verbose', 'verbose output', 'no'))
    opts.Add(EnumOption('machine', 'use machine-specific assembly code',
                        default_machine,
                        allowed_values=('generic', 'ppc', 'x86', 'x86_64')))
    opts.Add(EnumOption('platform', 'target platform', host_platform,
                        allowed_values=('cygwin', 'darwin', 'freebsd', 'haiku',
                                        'linux', 'sunos', 'windows')))
    opts.Add(BoolOption('embedded', 'embedded build', 'no'))
    opts.Add(BoolOption('analyze',
                        'enable static code analysis where available', 'no'))
    opts.Add(BoolOption('asan', 'enable Address Sanitizer', 'no'))
    opts.Add('toolchain', 'compiler toolchain', default_toolchain)
    opts.Add(BoolOption('llvm', 'use LLVM', default_llvm))
    opts.Add(BoolOption('force_scons', 'Force enable scons on deprecated platforms', 'false'))
    opts.Add(BoolOption('openmp', 'EXPERIMENTAL: compile with openmp (swrast)',
                        'no'))
    opts.Add(BoolOption('debug', 'DEPRECATED: debug build', 'yes'))
    opts.Add(BoolOption('profile', 'DEPRECATED: profile build', 'no'))
    opts.Add(BoolOption('quiet', 'DEPRECATED: profile build', 'yes'))
    opts.Add(BoolOption('swr', 'Build OpenSWR', 'no'))
    if host_platform == 'windows':
        opts.Add('MSVC_VERSION', 'Microsoft Visual C/C++ version')
        opts.Add('MSVC_USE_SCRIPT', 'Microsoft Visual C/C++ vcvarsall script', True)
