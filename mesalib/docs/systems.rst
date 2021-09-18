Platforms and Drivers
=====================

Mesa is primarily developed and used on Linux systems. But there's also
support for Windows, other flavors of Unix and other systems such as
Haiku. We're actively developing and maintaining several hardware and
software drivers.

The primary API is OpenGL but there's also support for OpenGL ES, Vulkan,
EGL, OpenMAX, OpenCL, VDPAU, VA-API and XvMC.

Hardware drivers include:

-  Intel GMA, HD Graphics, Iris. See `Intel's
   Website <https://01.org/linuxgraphics>`__
-  AMD Radeon series. See
   `RadeonFeature <https://www.x.org/wiki/RadeonFeature>`__
-  NVIDIA GPUs (Riva TNT and later). See `Nouveau
   Wiki <https://nouveau.freedesktop.org>`__
-  Qualcomm Adreno A2xx-A6xx. See :doc:`Freedreno
   <drivers/freedreno>`
-  Broadcom VideoCore 4 and 5. See :doc:`VC4 <drivers/vc4>` and
   :doc:`V3D <drivers/v3d>`
-  ARM Mali Utgard. See :doc:`Lima <drivers/lima>`
-  ARM Mali Midgard, Bifrost. See :doc:`Panfrost <drivers/panfrost>`
-  Vivante GCxxx. See `Etnaviv
   Wiki <https://github.com/laanwj/etna_viv/wiki>`__
-  NVIDIA Tegra (K1 and later).

Layered driver include:

-  :doc:`D3D12 <drivers/d3d12>` - driver providing OpenGL on top of
   Microsoft's Direct3D 12 API.
-  :doc:`SVGA3D <drivers/svga3d>` - driver for VMware virtual GPU
-  `VirGL <https://virgil3d.github.io/>`__ - research project for
   accelerated graphics for qemu guests
-  :doc:`Zink <drivers/zink>` - driver providing OpenGL on top of
   Khoronos' Vulkan API.

Software drivers include:

-  :doc:`LLVMpipe <drivers/llvmpipe>` - uses LLVM for x86 JIT code generation
   and is multi-threaded
-  Softpipe - a reference Gallium driver
-  :doc:`OpenSWR <drivers/openswr>` - x86-optimized software renderer
   for visualization workloads

Additional driver information:

-  `DRI hardware drivers <https://dri.freedesktop.org/>`__ for the X
   Window System
-  :doc:`Xlib / swrast driver <xlibdriver>` for the X Window System
   and Unix-like operating systems

Deprecated Systems and Drivers
------------------------------

In the past there were other drivers for older GPUs and operating
systems. These have been removed from the Mesa source tree and
distribution. If anyone's interested though, the code can be found in
the Git repo. The list includes:

-  3dfx Glide
-  3DLABS Gamma
-  ATI Mach 64
-  ATI Rage 128
-  DEC OpenVMS
-  Intel i810
-  Linux fbdev
-  Matrox
-  MS-DOS
-  S3 Savage
-  Silicon Integrated Systems
-  swrast
-  VIA Unichrome
