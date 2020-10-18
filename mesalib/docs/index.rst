.. include:: contents.rst

Introduction
============

The Mesa project began as an open-source implementation of the
`OpenGL <https://www.opengl.org/>`__ specification - a system for
rendering interactive 3D graphics.

Over the years the project has grown to implement more graphics APIs,
including `OpenGL ES <https://www.khronos.org/opengles/>`__ (versions 1,
2, 3), `OpenCL <https://www.khronos.org/opencl/>`__,
`OpenMAX <https://www.khronos.org/openmax/>`__,
`VDPAU <https://en.wikipedia.org/wiki/VDPAU>`__, `VA
API <https://en.wikipedia.org/wiki/Video_Acceleration_API>`__,
`XvMC <https://en.wikipedia.org/wiki/X-Video_Motion_Compensation>`__ and
`Vulkan <https://www.khronos.org/vulkan/>`__.

A variety of device drivers allows the Mesa libraries to be used in many
different environments ranging from software emulation to complete
hardware acceleration for modern GPUs.

Mesa ties into several other open-source projects: the `Direct Rendering
Infrastructure <https://dri.freedesktop.org/>`__ and
`X.org <https://x.org>`__ to provide OpenGL support on Linux, FreeBSD
and other operating systems.

Project History
---------------

The Mesa project was originally started by Brian Paul. Here's a short
history of the project.

August, 1993: I begin working on Mesa in my spare time. The project has
no name at that point. I was simply interested in writing a simple 3D
graphics library that used the then-new OpenGL API. I was partially
inspired by the *VOGL* library which emulated a subset of IRIS GL. I had
been programming with IRIS GL since 1991.

November 1994: I contact SGI to ask permission to distribute my
OpenGL-like graphics library on the internet. SGI was generally
receptive to the idea and after negotiations with SGI's legal
department, I get permission to release it.

February 1995: Mesa 1.0 is released on the internet. I expected that a
few people would be interested in it, but not thousands. I was soon
receiving patches, new features and thank-you notes on a daily basis.
That encouraged me to continue working on Mesa. The name Mesa just
popped into my head one day. SGI had asked me not to use the terms
*"Open"* or *"GL"* in the project name and I didn't want to make up a
new acronym. Later, I heard of the Mesa programming language and the
Mesa spreadsheet for NeXTStep.

In the early days, OpenGL wasn't available on too many systems. It even
took a while for SGI to support it across their product line. Mesa
filled a big hole during that time. For a lot of people, Mesa was their
first introduction to OpenGL. I think SGI recognized that Mesa actually
helped to promote the OpenGL API, so they didn't feel threatened by the
project.

1995-1996: I continue working on Mesa both during my spare time and
during my work hours at the Space Science and Engineering Center at the
University of Wisconsin in Madison. My supervisor, Bill Hibbard, lets me
do this because Mesa is now being using for the
`Vis5D <https://www.ssec.wisc.edu/%7Ebillh/vis.html>`__ project.

October 1996: Mesa 2.0 is released. It implements the OpenGL 1.1
specification.

March 1997: Mesa 2.2 is released. It supports the new 3dfx Voodoo
graphics card via the Glide library. It's the first really popular
hardware OpenGL implementation for Linux.

September 1998: Mesa 3.0 is released. It's the first publicly-available
implementation of the OpenGL 1.2 API.

March 1999: I attend my first OpenGL ARB meeting. I contribute to the
development of several official OpenGL extensions over the years.

September 1999: I'm hired by Precision Insight, Inc. Mesa is a key
component of 3D hardware acceleration in the new DRI project for
XFree86. Drivers for 3dfx, 3dLabs, Intel, Matrox and ATI hardware soon
follow.

October 2001: Mesa 4.0 is released. It implements the OpenGL 1.3
specification.

November 2001: I cofounded Tungsten Graphics, Inc. with Keith Whitwell,
Jens Owen, David Dawes and Frank LaMonica. Tungsten Graphics was
acquired by VMware in December 2008.

November 2002: Mesa 5.0 is released. It implements the OpenGL 1.4
specification.

January 2003: Mesa 6.0 is released. It implements the OpenGL 1.5
specification as well as the GL_ARB_vertex_program and
GL_ARB_fragment_program extensions.

June 2007: Mesa 7.0 is released, implementing the OpenGL 2.1
specification and OpenGL Shading Language.

2008: Keith Whitwell and other Tungsten Graphics employees develop
`Gallium <https://en.wikipedia.org/wiki/Gallium3D>`__ - a new GPU
abstraction layer. The latest Mesa drivers are based on Gallium and
other APIs such as OpenVG are implemented on top of Gallium.

February 2012: Mesa 8.0 is released, implementing the OpenGL 3.0
specification and version 1.30 of the OpenGL Shading Language.

July 2016: Mesa 12.0 is released, including OpenGL 4.3 support and
initial support for Vulkan for Intel GPUs. Plus, there's another Gallium
software driver ("swr") based on LLVM and developed by Intel.

Ongoing: Mesa is the OpenGL implementation for devices designed by
Intel, AMD, NVIDIA, Qualcomm, Broadcom, Vivante, plus the VMware and
VirGL virtual GPUs. There's also several software-based renderers:
swrast (the legacy Mesa rasterizer), softpipe (a Gallium reference
driver), llvmpipe (LLVM/JIT-based high-speed rasterizer) and swr
(another LLVM-based driver).

Work continues on the drivers and core Mesa to implement newer versions
of the OpenGL, OpenGL ES and Vulkan specifications.

Major Versions
--------------

This is a summary of the major versions of Mesa. Mesa's major version
number has been incremented whenever a new version of the OpenGL
specification is implemented.

Version 12.x features
~~~~~~~~~~~~~~~~~~~~~

Version 12.x of Mesa implements the OpenGL 4.3 API, but not all drivers
support OpenGL 4.3.

Initial support for Vulkan is also included.

Version 11.x features
~~~~~~~~~~~~~~~~~~~~~

Version 11.x of Mesa implements the OpenGL 4.1 API, but not all drivers
support OpenGL 4.1.

Version 10.x features
~~~~~~~~~~~~~~~~~~~~~

Version 10.x of Mesa implements the OpenGL 3.3 API, but not all drivers
support OpenGL 3.3.

Version 9.x features
~~~~~~~~~~~~~~~~~~~~

Version 9.x of Mesa implements the OpenGL 3.1 API. While the driver for
Intel Sandy Bridge and Ivy Bridge is the only driver to support OpenGL
3.1, many developers across the open-source community contributed
features required for OpenGL 3.1. The primary features added since the
Mesa 8.0 release are GL_ARB_texture_buffer_object and
GL_ARB_uniform_buffer_object.

Version 9.0 of Mesa also included the first release of the Clover state
tracker for OpenCL.

Version 8.x features
~~~~~~~~~~~~~~~~~~~~

Version 8.x of Mesa implements the OpenGL 3.0 API. The developers at
Intel deserve a lot of credit for implementing most of the OpenGL 3.0
features in core Mesa, the GLSL compiler as well as the i965 driver.

Version 7.x features
~~~~~~~~~~~~~~~~~~~~

Version 7.x of Mesa implements the OpenGL 2.1 API. The main feature of
OpenGL 2.x is the OpenGL Shading Language.

Version 6.x features
~~~~~~~~~~~~~~~~~~~~

Version 6.x of Mesa implements the OpenGL 1.5 API with the following
extensions incorporated as standard features:

-  GL_ARB_occlusion_query
-  GL_ARB_vertex_buffer_object
-  GL_EXT_shadow_funcs

Also note that several OpenGL tokens were renamed in OpenGL 1.5 for the
sake of consistency. The old tokens are still available.

::

   New Token                   Old Token
   ------------------------------------------------------------
   GL_FOG_COORD_SRC            GL_FOG_COORDINATE_SOURCE
   GL_FOG_COORD                GL_FOG_COORDINATE
   GL_CURRENT_FOG_COORD        GL_CURRENT_FOG_COORDINATE
   GL_FOG_COORD_ARRAY_TYPE     GL_FOG_COORDINATE_ARRAY_TYPE
   GL_FOG_COORD_ARRAY_STRIDE   GL_FOG_COORDINATE_ARRAY_STRIDE
   GL_FOG_COORD_ARRAY_POINTER  GL_FOG_COORDINATE_ARRAY_POINTER
   GL_FOG_COORD_ARRAY          GL_FOG_COORDINATE_ARRAY
   GL_SRC0_RGB                 GL_SOURCE0_RGB
   GL_SRC1_RGB                 GL_SOURCE1_RGB
   GL_SRC2_RGB                 GL_SOURCE2_RGB
   GL_SRC0_ALPHA               GL_SOURCE0_ALPHA
   GL_SRC1_ALPHA               GL_SOURCE1_ALPHA
   GL_SRC2_ALPHA               GL_SOURCE2_ALPHA

See the `OpenGL
specification <https://www.opengl.org/documentation/spec.html>`__ for
more details.

Version 5.x features
~~~~~~~~~~~~~~~~~~~~

Version 5.x of Mesa implements the OpenGL 1.4 API with the following
extensions incorporated as standard features:

-  GL_ARB_depth_texture
-  GL_ARB_shadow
-  GL_ARB_texture_env_crossbar
-  GL_ARB_texture_mirror_repeat
-  GL_ARB_window_pos
-  GL_EXT_blend_color
-  GL_EXT_blend_func_separate
-  GL_EXT_blend_logic_op
-  GL_EXT_blend_minmax
-  GL_EXT_blend_subtract
-  GL_EXT_fog_coord
-  GL_EXT_multi_draw_arrays
-  GL_EXT_point_parameters
-  GL_EXT_secondary_color
-  GL_EXT_stencil_wrap
-  GL_EXT_texture_lod_bias (plus, a per-texture LOD bias parameter)
-  GL_SGIS_generate_mipmap

Version 4.x features
~~~~~~~~~~~~~~~~~~~~

Version 4.x of Mesa implements the OpenGL 1.3 API with the following
extensions incorporated as standard features:

-  GL_ARB_multisample
-  GL_ARB_multitexture
-  GL_ARB_texture_border_clamp
-  GL_ARB_texture_compression
-  GL_ARB_texture_cube_map
-  GL_ARB_texture_env_add
-  GL_ARB_texture_env_combine
-  GL_ARB_texture_env_dot3
-  GL_ARB_transpose_matrix

Version 3.x features
~~~~~~~~~~~~~~~~~~~~

Version 3.x of Mesa implements the OpenGL 1.2 API with the following
features:

-  BGR, BGRA and packed pixel formats
-  New texture border clamp mode
-  glDrawRangeElements()
-  standard 3-D texturing
-  advanced MIPMAP control
-  separate specular color interpolation

Version 2.x features
~~~~~~~~~~~~~~~~~~~~

Version 2.x of Mesa implements the OpenGL 1.1 API with the following
features.

-  Texture mapping:

   -  glAreTexturesResident
   -  glBindTexture
   -  glCopyTexImage1D
   -  glCopyTexImage2D
   -  glCopyTexSubImage1D
   -  glCopyTexSubImage2D
   -  glDeleteTextures
   -  glGenTextures
   -  glIsTexture
   -  glPrioritizeTextures
   -  glTexSubImage1D
   -  glTexSubImage2D

-  Vertex Arrays:

   -  glArrayElement
   -  glColorPointer
   -  glDrawElements
   -  glEdgeFlagPointer
   -  glIndexPointer
   -  glInterleavedArrays
   -  glNormalPointer
   -  glTexCoordPointer
   -  glVertexPointer

-  Client state management:

   -  glDisableClientState
   -  glEnableClientState
   -  glPopClientAttrib
   -  glPushClientAttrib

-  Misc:

   -  glGetPointer
   -  glIndexub
   -  glIndexubv
   -  glPolygonOffset
