Usage
=====

Requirements
^^^^^^^^^^^^

* An x86 processor with AVX or above
* LLVM version 3.9 or later
* C++14 capable compiler

Building
^^^^^^^^

To build with GNU automake, select building the swr driver at
configure time, for example: ::

  configure --with-gallium-drivers=swrast,swr

Using
^^^^^

On Linux, building with autotools will create a drop-in alternative
for libGL.so into::

  lib/gallium/libGL.so
  lib/gallium/libswrAVX.so
  lib/gallium/libswrAVX2.so

Alternatively, building with SCons will produce::

  build/linux-x86_64/gallium/targets/libgl-xlib/libGL.so
  build/linux-x86_64/gallium/drivers/swr/libswrAVX.so
  build/linux-x86_64/gallium/drivers/swr/libswrAVX2.so

To use it set the LD_LIBRARY_PATH environment variable accordingly.

**IMPORTANT:** Mesa will default to using llvmpipe or softpipe as the default software renderer.  To select the OpenSWR driver, set the GALLIUM_DRIVER environment variable appropriately: ::

  GALLIUM_DRIVER=swr

To verify OpenSWR is being used, check to see if a message like the following is printed when the application is started: ::

  SWR detected AVX2

