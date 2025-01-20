LLVMpipe
========

Introduction
------------

The Gallium LLVMpipe driver is a software rasterizer that uses LLVM to
do runtime code generation. Shaders, point/line/triangle rasterization
and vertex processing are implemented with LLVM IR which is translated
to x86, x86-64, or ppc64le machine code. Also, the driver is
multithreaded to take advantage of multiple CPU cores (up to 32 at this
time). It's the fastest software rasterizer for Mesa.

Requirements
------------

-  For x86 or amd64 processors, 64-bit mode is recommended. Support for
   SSE2 is strongly encouraged. Support for SSE3 and SSE4.1 will yield
   the most efficient code. The fewer features the CPU has the more
   likely it is that you will run into underperforming, buggy, or
   incomplete code.

   For ppc64le processors, use of the Altivec feature (the Vector
   Facility) is recommended if supported; use of the VSX feature (the
   Vector-Scalar Facility) is recommended if supported AND Mesa is built
   with LLVM version 4.0 or later.

   See ``/proc/cpuinfo`` to know what your CPU supports.

-  Unless otherwise stated, LLVM version 3.9 or later is required.

   For Linux, on a recent Debian based distribution do:

   .. code-block:: sh

      aptitude install llvm-dev

   If you want development snapshot builds of LLVM for Debian and
   derived distributions like Ubuntu, you can use the APT repository at
   `apt.llvm.org <https://apt.llvm.org/>`__, which are maintained by
   Debian's LLVM maintainer.

   For a RPM-based distribution do:

   .. code-block:: sh

      yum install llvm-devel

   If you want development snapshot builds of LLVM for Fedora, you can
   use the Copr repository at `fedora-llvm-team/llvm-snapshots <https://copr.fedorainfracloud.org/coprs/g/fedora-llvm-team/llvm-snapshots/>`__,
   which is maintained by Red Hat's LLVM team.

   For Windows you will need to build LLVM from source with MSVC or
   MINGW (either natively or through cross compilers) and CMake, and set
   the ``LLVM`` environment variable to the directory you installed it
   to. LLVM will be statically linked, so when building on MSVC it needs
   to be built with a matching CRT as Mesa, and you'll need to pass
   ``-DLLVM_USE_CRT_xxx=yyy`` as described below.


   +-----------------+----------------------------------------------------------------+
   | LLVM build-type | Mesa build-type                                                |
   |                 +--------------------------------+-------------------------------+
   |                 | debug,checked                  | release,profile               |
   +=================+================================+===============================+
   | Debug           | ``-DLLVM_USE_CRT_DEBUG=MTd``   | ``-DLLVM_USE_CRT_DEBUG=MT``   |
   +-----------------+--------------------------------+-------------------------------+
   | Release         | ``-DLLVM_USE_CRT_RELEASE=MTd`` | ``-DLLVM_USE_CRT_RELEASE=MT`` |
   +-----------------+--------------------------------+-------------------------------+

   You can build only the x86 target by passing
   ``-DLLVM_TARGETS_TO_BUILD=X86`` to CMake.

Building
--------

To build everything on Linux invoke meson as:

.. code-block:: sh

   mkdir build
   cd build
   meson -D glx=xlib -D gallium-drivers=swrast
   ninja

Building for Android
--------------------

To build for Android requires the additional step of building LLVM
for Android using the NDK. Before following the steps in
:doc:`Android's documentation <../android>` you must build a version
of LLVM that targets the NDK with all the required libraries for
llvmpipe, and then create a wrap file so that meson knows where to
find the LLVM libraries. It can be a bit tricky to get LLVM to build
properly using the Android NDK, so the script below can be
used as a reference to configure LLVM to build with the NDK for x86.
You need to set the ``ANDROID_NDK_ROOT``, ``ANDROID_SDK_VERSION`` and
``LLVML_INSTALL_PREFIX`` environment variables appropriately.

.. code-block:: sh

   #!/bin/bash

   set -e
   set -u

   # Early check for required env variables, relies on `set -u`
   : "$ANDROID_NDK_ROOT"
   : "$ANDROID_SDK_VERSION"
   : "$LLVM_INSTALL_PREFIX"

   cmake -GNinja -S llvm -B build/ \
      -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=x86_64 \
      -DANDROID_PLATFORM=android-${ANDROID_SDK_VERSION} \
      -DANDROID_NDK=${ANDROID_NDK_ROOT} \
      -DCMAKE_ANDROID_ARCH_ABI=x86_64 \
      -DCMAKE_ANDROID_NDK=${ANDROID_NDK_ROOT} \
      -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DCMAKE_SYSTEM_NAME=Android \
      -DCMAKE_SYSTEM_VERSION=${ANDROID_SDK_VERSION} \
      -DCMAKE_INSTALL_PREFIX=${LLVM_INSTALL_PREFIX} \
      -DCMAKE_CXX_FLAGS="-march=x86-64 --target=x86_64-linux-android${ANDROID_SDK_VERSION} -fno-rtti" \
      -DLLVM_HOST_TRIPLE=x86_64-linux-android${ANDROID_SDK_VERSION} \
      -DLLVM_TARGETS_TO_BUILD=X86 \
      -DLLVM_BUILD_LLVM_DYLIB=OFF \
      -DLLVM_BUILD_TESTS=OFF \
      -DLLVM_BUILD_EXAMPLES=OFF \
      -DLLVM_BUILD_DOCS=OFF \
      -DLLVM_BUILD_TOOLS=OFF \
      -DLLVM_ENABLE_RTTI=OFF \
      -DLLVM_BUILD_INSTRUMENTED_COVERAGE=OFF \
      -DLLVM_NATIVE_TOOL_DIR=${ANDROID_NDK_ROOT}toolchains/llvm/prebuilt/linux-x86_64/bin \
      -DLLVM_ENABLE_PIC=False

   ninja -C build/ install

You will also need to create a wrap file, so that meson is able
to find the LLVM libraries built with the NDK. The process for this
is described in :doc:`meson documentation <../meson>`.

For example the following script will create the
``subprojects/llvm/meson.build`` wrap file, after setting ``LLVM_INSTALL_PREFIX``
to the path where LLVM was installed to.

The list of libraries passed in `dep_llvm` below should match what it was
produced by the LLVM build from above.

.. code-block:: sh

   #!/usr/bin/env bash

   set -exu

   # Early check for required env variables, relies on `set -u`
   : "$LLVM_INSTALL_PREFIX"

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

Afterwards you can continue following the instructors to build mesa
on :doc:`Android <../android>` and follow the steps to add the driver
directly to an Android OS image.

Using
-----

Environment variables
~~~~~~~~~~~~~~~~~~~~~

.. envvar:: LP_NATIVE_VECTOR_WIDTH

   We can use it to override vector bits. Because sometimes it turns
   out LLVMpipe can be fastest by using 128 bit vectors,
   yet use AVX instructions.

.. envvar:: GALLIUM_NOSSE

   Deprecated in favor of ``GALLIUM_OVERRIDE_CPU_CAPS``,
   use ``GALLIUM_OVERRIDE_CPU_CAPS=nosse`` instead.

.. envvar:: LP_FORCE_SSE2

   Deprecated in favor of ``GALLIUM_OVERRIDE_CPU_CAPS``
   use ``GALLIUM_OVERRIDE_CPU_CAPS=sse2`` instead.

Linux
~~~~~

On Linux, building will create a drop-in alternative for ``libGL.so``
into

::

   build/foo/gallium/targets/libgl-xlib/libGL.so

or

::

   lib/gallium/libGL.so

To use it set the ``LD_LIBRARY_PATH`` environment variable accordingly.

Windows
~~~~~~~

On Windows, building will create
``build/windows-x86-debug/gallium/targets/libgl-gdi/opengl32.dll`` which
is a drop-in alternative for system's ``opengl32.dll``, which will use
the Mesa ICD, ``build/windows-x86-debug/gallium/targets/wgl/libgallium_wgl.dll``.
To use it put both DLLs in the same directory as your application. It can also
be used by replacing the native ICD driver, but it's quite an advanced usage, so if
you need to ask, don't even try it.

There is however an easy way to replace the OpenGL software renderer
that comes with Microsoft Windows 7 (or later) with LLVMpipe (that is,
on systems without any OpenGL drivers):

-  copy
   ``build/windows-x86-debug/gallium/targets/wgl/libgallium_wgl.dll`` to
   ``C:\Windows\SysWOW64\mesadrv.dll``

-  load this registry settings:

   ::

      REGEDIT4

      ; https://technet.microsoft.com/en-us/library/cc749368.aspx
      ; https://www.msfn.org/board/topic/143241-portable-windows-7-build-from-winpe-30/page-5#entry942596
      [HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\MSOGL]
      "DLL"="mesadrv.dll"
      "DriverVersion"=dword:00000001
      "Flags"=dword:00000001
      "Version"=dword:00000002

-  Ditto for 64 bits drivers if you need them.

Profiling
---------

Linux perf integration
~~~~~~~~~~~~~~~~~~~~~~

On Linux, it is possible to have symbol resolution of JIT code with
`Linux perf <https://perfwiki.github.io/main/>`__:

::

   perf record -g /my/application
   perf report

When run inside Linux perf, LLVMpipe will create a
``/tmp/perf-XXXXX.map`` file with symbol address table. It also dumps
assembly code to ``/tmp/perf-XXXXX.map.asm``, which can be used by the
``bin/perf-annotate-jit.py`` script to produce disassembly of the
generated code annotated with the samples.

You can obtain a call graph via
`Gprof2Dot <https://github.com/jrfonseca/gprof2dot#linux-perf>`__.

FlameGraph support
~~~~~~~~~~~~~~~~~~~~~~

Outside Linux, it is possible to generate a
`FlameGraph <https://github.com/brendangregg/FlameGraph>`__
with resolved JIT symbols.

Set the environment variable ``JIT_SYMBOL_MAP_DIR`` to a directory path,
and run your LLVMpipe program. Follow the FlameGraph instructions:
capture traces using a supported tool (for example DTrace),
and fold the stacks using the associated script
(``stackcollapse.pl`` for DTrace stacks).

LLVMpipe will create a ``jit-symbols-XXXXX.map`` file containing the symbol
address table inside the chosen directory. It will also dump the JIT
disassemblies to ``jit-symbols-XXXXX.map.asm``. Run your folded traces and
both output files through the ``bin/flamegraph_map_lp_jit.py`` script to map
addresses to JIT symbols, and annotate the disassembly with the sample counts.

Unit testing
------------

Building will also create several unit tests in
``build/linux-???-debug/gallium/drivers/llvmpipe``:

-  ``lp_test_blend``: blending
-  ``lp_test_conv``: SIMD vector conversion
-  ``lp_test_format``: pixel unpacking/packing

Some of these tests can output results and benchmarks to a tab-separated
file for later analysis, e.g.:

::

   build/linux-x86_64-debug/gallium/drivers/llvmpipe/lp_test_blend -o blend.tsv

Development Notes
-----------------

-  When looking at this code for the first time, start in lp_state_fs.c,
   and then skim through the ``lp_bld_*`` functions called there, and
   the comments at the top of the ``lp_bld_*.c`` functions.
-  The driver-independent parts of the LLVM / Gallium code are found in
   ``src/gallium/auxiliary/gallivm/``. The filenames and function
   prefixes need to be renamed from ``lp_bld_`` to something else
   though.
-  We use LLVM-C bindings for now. They are not documented, but follow
   the C++ interfaces very closely, and appear to be complete enough for
   code generation. See `this stand-alone
   example <https://npcontemplation.blogspot.com/2008/06/secret-of-llvm-c-bindings.html>`__.
   See the ``llvm-c/Core.h`` file for reference.

.. _recommended_reading:

Recommended Reading
-------------------

-  Rasterization

   -  `Triangle Scan Conversion using 2D Homogeneous
      Coordinates <https://userpages.cs.umbc.edu/olano/papers/2dh-tri/>`__
   -  `Rasterization on
      Larrabee <https://www.drdobbs.com/parallel/rasterization-on-larrabee/217200602>`__
   -  `Rasterization using half-space
      functions <http://web.archive.org/web/20110820052005/http://www.devmaster.net/codespotlight/show.php?id=17>`__
   -  `Advanced
      Rasterization <http://web.archive.org/web/20140514220546/http://devmaster.net/posts/6145/advanced-rasterization>`__
   -  `Optimizing Software Occlusion
      Culling <https://fgiesen.wordpress.com/2013/02/17/optimizing-sw-occlusion-culling-index/>`__

-  Texture sampling

   -  `Perspective Texture
      Mapping <https://chrishecker.com/Miscellaneous_Technical_Articles#Perspective_Texture_Mapping>`__
   -  `Texturing As In
      Unreal <https://www.flipcode.com/archives/Texturing_As_In_Unreal.shtml>`__
   -  `Run-Time MIP-Map
      Filtering <http://web.archive.org/web/20220709145555/http://www.gamasutra.com/view/feature/3301/runtime_mipmap_filtering.php>`__
   -  `Will "brilinear" filtering
      persist? <https://alt.3dcenter.org/artikel/2003/10-26_a_english.php>`__
   -  `Trilinear
      filtering <http://ixbtlabs.com/articles2/gffx/nv40-rx800-3.html>`__
   -  `Texture tiling and
      swizzling <https://fgiesen.wordpress.com/2011/01/17/texture-tiling-and-swizzling/>`__

-  SIMD

   -  `Whole-Function
      Vectorization <https://compilers.cs.uni-saarland.de/projects/wfv/#pubs>`__

-  Optimization

   -  `Optimizing Pixomatic For Modern x86
      Processors <https://www.drdobbs.com/optimizing-pixomatic-for-modern-x86-proc/184405807>`__
   -  `Intel 64 and IA-32 Architectures Optimization Reference
      Manual <https://www.intel.com/content/www/us/en/content-details/779559/intel-64-and-ia-32-architectures-optimization-reference-manual.html>`__
   -  `Software optimization
      resources <https://www.agner.org/optimize/>`__
   -  `Intel Intrinsics
      Guide <https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html>`__

-  LLVM

   -  `LLVM Language Reference
      Manual <https://llvm.org/docs/LangRef.html>`__
   -  `The secret of LLVM C
      bindings <https://npcontemplation.blogspot.com/2008/06/secret-of-llvm-c-bindings.html>`__

-  General

   -  `A trip through the Graphics
      Pipeline <https://fgiesen.wordpress.com/2011/07/09/a-trip-through-the-graphics-pipeline-2011-index/>`__
   -  `WARP Architecture and
      Performance <https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp#warp-architecture-and-performance>`__
