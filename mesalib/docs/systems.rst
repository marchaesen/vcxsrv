Platforms and Drivers
=====================

Mesa is primarily developed and used on Linux systems. But there's also
support for Windows, other flavors of Unix and other systems such as
Haiku. We're actively developing and maintaining several hardware and
software drivers.

The primary API is OpenGL but there's also support for OpenGL ES 1, ES2
and ES 3, OpenCL, VDPAU, XvMC and the EGL interface.

Hardware drivers include:

-  Intel GMA, HD Graphics, Iris. See `Intel's
   Website <https://01.org/linuxgraphics>`__
-  AMD Radeon series. See
   `RadeonFeature <https://www.x.org/wiki/RadeonFeature>`__
-  NVIDIA GPUs (Riva TNT and later). See `Nouveau
   Wiki <https://nouveau.freedesktop.org>`__
-  Qualcomm Adreno A2xx-A6xx. See `Freedreno
   Wiki <https://github.com/freedreno/freedreno/wiki>`__
-  Broadcom VideoCore 4, 5. See `This Week in
   V3D <https://anholt.github.io/twivc4/>`__
-  ARM Mali Utgard. See `Lima
   Wiki <https://gitlab.freedesktop.org/lima/web/wikis/home>`__
-  ARM Mali Midgard, Bifrost. See `Panfrost
   Site <https://panfrost.freedesktop.org/>`__
-  Vivante GCxxx. See `Etnaviv
   Wiki <https://github.com/laanwj/etna_viv/wiki>`__
-  NVIDIA Tegra (K1 and later).

Software drivers include:

-  :doc:`llvmpipe <drivers/llvmpipe>` - uses LLVM for x86 JIT code generation
   and is multi-threaded
-  softpipe - a reference Gallium driver
-  :doc:`svga <drivers/vmware-guest>` - driver for VMware virtual GPU
-  `swr <https://www.openswr.org/>`__ - x86-optimized software renderer
   for visualization workloads
-  `virgl <https://virgil3d.github.io/>`__ - research project for
   accelerated graphics for qemu guests
-  swrast - the legacy/original Mesa software rasterizer

Additional driver information:

-  `DRI hardware drivers <https://dri.freedesktop.org/>`__ for the X
   Window System
-  :doc:`Xlib / swrast driver <xlibdriver>` for the X Window System
   and Unix-like operating systems
-  `Microsoft Windows <README.WIN32>`__

Deprecated Systems and Drivers
------------------------------

In the past there were other drivers for older GPUs and operating
systems. These have been removed from the Mesa source tree and
distribution. If anyone's interested though, the code can be found in
the Git repo. The list includes:

-  3dfx/glide
-  Matrox
-  ATI R128
-  Savage
-  VIA Unichrome
-  SIS
-  3Dlabs gamma
-  DOS
-  fbdev
-  DEC/VMS
-  Mach64
-  Intel i810
