Source Code Tree
================

This is a brief summary of Mesa's directory tree and what's contained in
each directory.

-  **docs** - Documentation
-  **include** - Public OpenGL header files
-  **src**

   -  **amd** - AMD-specific sources

      -  **addrlib** - common sources for creating images
      -  **common** - common code between RADV, radeonsi and ACO
      -  **compiler** - ACO shader compiler
      -  **llvm** - common code between RADV and radeonsi for compiling
         shaders using LLVM
      -  **registers** - register definitions
      -  **vulkan** - RADV Vulkan implementation for AMD Southern Island
         and newer

   -  **compiler** - Common utility sources for different compilers.

      -  **glsl** - the GLSL IR and compiler
      -  **nir** - the NIR IR and compiler
      -  **spirv** - the SPIR-V compiler

   -  **egl** - EGL library sources

      -  **drivers** - EGL drivers
      -  **main** - main EGL library implementation. This is where all
         the EGL API functions are implemented, like eglCreateContext().

   -  **freedreno** - Adreno-specific sources

      -  **fdl** - mipmap layout manager
      -  **vulkan** - Turnip is a Vulkan implementation for
         Qualcomm Adreno

   -  **gbm** - Generic Buffer Manager is a memory allocator for
      device buffers

   -  **intel** - Intel-specific sources

      -  **blorp** - BLit Or Resolve Pass is a blit and HiZ resolve framework
      -  **vulkan** - Anvil is a Vulkan implementation for Intel gen 7
         (Ivy Bridge) and newer

   -  **mapi** - Mesa APIs

      -  **glapi** - OpenGL API dispatch layer. This is where all the GL
         entrypoints like glClear, glBegin, etc. are generated, as well as
         the GL dispatch table. All GL function calls jump through the
         dispatch table to functions found in main/.

   -  **mesa** - Main Mesa sources

      -  **main** - The core Mesa code (mainly state management)
      -  **drivers** - Mesa drivers (not used with Gallium)

         -  **common** - code which may be shared by all drivers
         -  **dri** - Direct Rendering Infrastructure drivers

            -  **common** - code shared by all DRI drivers
            -  **i915** - driver for Intel i915/i945
            -  **i965** - driver for Intel i965
            -  **nouveau** - driver for nVidia nv04/nv10/nv20
            -  **radeon** - driver for ATI R100
            -  **r200** - driver for ATI R200
            -  **swrast** - software rasterizer driver that uses the
               swrast module

         -  **x11** - Xlib-based software driver
         -  **osmesa** - off-screen software driver

      -  **math** - vertex array translation and transformation code
         (not used with Gallium)
      -  **program** - Vertex/fragment shader and GLSL compiler code
      -  **sparc** - Assembly code/optimizations for SPARC systems (not
         used with Gallium)
      -  **state_tracker** - Translator from Mesa to Gallium. This is
         basically a Mesa device driver that speaks to Gallium. This
         directory may be moved to src/mesa/drivers/gallium at some
         point.
      -  **swrast** - Software rasterization module. For drawing points,
         lines, triangles, bitmaps, images, etc. in software. (not used
         with Gallium)
      -  **swrast_setup** - Software primitive setup. Does things like
         polygon culling, glPolygonMode, polygon offset, etc. (not used
         with Gallium)
      -  **tnl** - Software vertex Transformation 'n Lighting. (not used
         with Gallium)
      -  **tnl_dd** - TNL code for device drivers. (not used with
         Gallium)
      -  **vbo** - Vertex Buffer Object code. All drawing with
         glBegin/glEnd, glDrawArrays, display lists, etc. goes through
         this module. The results is a well-defined set of vertex arrays
         which are passed to the device driver (or tnl module) for
         rendering.
      -  **x86** - Assembly code/optimizations for 32-bit x86 systems
         (not used with Gallium)
      -  **x86-64** - Assembly code/optimizations for 64-bit x86 systems
         (not used with Gallium)

   -  **gallium** - Gallium3D source code

      -  **include** - Gallium3D header files which define the Gallium3D
         interfaces
      -  **drivers** - Gallium3D device drivers

         -  **etnaviv** - Driver for Vivante.
         -  **freedreno** - Driver for Qualcomm Adreno.
         -  **i915** - Driver for Intel i915/i945.
         -  **iris** - Driver for Intel gen 8 (Broadwell) and newer.
         -  **lima** - Driver for ARM Mali-400 (Utgard) series.
         -  **llvmpipe** - Software driver using LLVM for runtime code
            generation.
         -  **nouveau** - Driver for NVIDIA GPUs.
         -  **panfrost** - Driver for ARM Mali Txxx (Midgard) and
            Gxx (Bifrost) GPUs.
         -  **radeon** - Shared module for the r600 and radeonsi
            drivers.
         -  **r300** - Driver for ATI R300 - R500.
         -  **r600** - Driver for ATI/AMD R600 - Northern Island (Terascale).
         -  **radeonsi** - Driver for AMD Southern Island and newer (GCN, RDNA).
         -  **softpipe** - Software reference driver.
         -  **svga** - Driver for VMware's SVGA virtual GPU.
         -  **swr** - Software driver with massively parellel vertex processing.
         -  **tegra** - Driver for NVIDIA Tegra GPUs.
         -  **v3d** - Driver for Broadcom VideoCore 5 and newer.
         -  **vc4** - Driver for Broadcom VideoCore 4.
         -  **virgl** - Driver for Virtio virtual GPU of QEMU.
         -  **zink** - Driver that uses Vulkan for rendering.

      -  **auxiliary** - Gallium support code

         -  **cso_cache** - Constant State Objects Cache. Used to filter
            out redundant state changes between frontends and drivers.
         -  **draw** - Software vertex processing and primitive assembly
            module. This includes vertex program execution, clipping,
            culling and optional stages for drawing wide lines, stippled
            lines, polygon stippling, two-sided lighting, etc. Intended
            for use by drivers for hardware that does not have vertex
            shaders. Geometry shaders will also be implemented in this
            module.
         -  **gallivm** - LLVM module for Gallium. For LLVM-based
            compilation, optimization and code generation for TGSI
            shaders. Incomplete.
         -  **hud** - Heads-Up Display, an overlay showing GPU statistics
         -  **pipebuffer** - utility module for managing buffers
         -  **rbug** - Gallium remote debug utility
         -  **rtasm** - run-time assembly/machine code generation.
            Currently there's run-time code generation for x86/SSE,
            PowerPC and Cell SPU.
         -  **tessellator**- used by software drivers to implement
            tessellation shaders
         -  **tgsi** - TG Shader Infrastructure. Code for encoding,
            manipulating and interpreting GPU programs.
         -  **translate** - module for translating vertex data from one
            format to another.
         -  **util** - assorted utilities for arithmetic, hashing,
            surface creation, memory management, 2D blitting, simple
            rendering, etc.
         -  **vl** - utility code for video decode/encode
         -  XXX more

      -  **frontends** - These implement various libraries using the
         device drivers

         -  **clover** - OpenCL frontend
         -  **dri** - Meta frontend for DRI drivers, see mesa/state_tracker
         -  **glx** - Meta frontend for GLX
         -  **hgl** - Haiku OpenGL
         -  **nine** - D3D9 frontend, see targets/d3dadapter9
         -  **omx** - OpenMAX Bellagio frontend
         -  **osmesa** - Off-screen OpenGL rendering library
         -  **va** - VA-API frontend
         -  **vdpau** - VDPAU frontend
         -  **wgl** - Windows WGL frontend
         -  **xa** - XA frontend
         -  **xvmc** - XvMC frontend

      -  **winsys** - The device drivers are platform-independent, the
         winsys connects them to various platforms. There is usually one winsys
         per device family, and within the winsys directory there can be
         multiple flavors connecting to different platforms.

         -  **drm** - Direct Rendering Manager on Linux
         -  **gdi** - Windows
         -  **xlib** - indirect rendering on X Window System
         -  XXX more

   -  **targets** - These control how the Gallium code is compiled into
      different libraries. Each of these roughly corresponds to one frontend.

         -  **d3dadapter9** - d3dadapter9.so for Wine
         -  **dri** - libgallium_dri.so loaded by libGL.so
         -  **graw** - raw Gallium interface without a frontend
         -  XXX more

   -  **glx** - The GLX library code for building libGL.so using DRI
      drivers.
   -  **loader** - Used by libGL.so to find and load the appropriate DRI driver.
   -  **panfrost** - Panfrost-specific sources

         -  **bifrost** - shader compiler for the Bifrost generation GPUs
         -  **midgard** - shader compiler for the Midgard generation GPUs
         -  **pandecode** - command stream debugger

   -  **util** - Various utility codes
   -  **vulkan** - Common code for Vulkan drivers
