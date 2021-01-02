Introduction
============

The Mesa project began as an open-source implementation of the
`OpenGL`_ specification - a system for rendering interactive 3D graphics.

Over the years the project has grown to implement more graphics APIs,
including `OpenGL ES`_, `OpenCL`_, `OpenMAX`_, `VDPAU`_, `VA API`_,
`XvMC`_, `Vulkan`_ and `EGL`_.

A variety of device drivers allows the Mesa libraries to be used in many
different environments ranging from software emulation to complete
hardware acceleration for modern GPUs.

Mesa ties into several other open-source projects: the `Direct Rendering
Infrastructure`_ and `X.org`_ to provide OpenGL support on Linux, FreeBSD
and other operating systems.

.. _OpenGL: https://www.opengl.org/
.. _OpenGL ES: https://www.khronos.org/opengles/
.. _OpenCL: https://www.khronos.org/opencl/
.. _OpenMAX: https://www.khronos.org/openmax/
.. _VDPAU: https://en.wikipedia.org/wiki/VDPAU
.. _VA API: https://en.wikipedia.org/wiki/Video_Acceleration_API
.. _XvMC: https://en.wikipedia.org/wiki/X-Video_Motion_Compensation
.. _Vulkan: https://www.khronos.org/vulkan/
.. _EGL: https://www.khronos.org/egl/
.. _Direct Rendering Infrastructure: https://dri.freedesktop.org/
.. _X.org: https://x.org

.. toctree::
   :maxdepth: 1
   :caption: Documentation
   :hidden:

   self
   history
   developers
   systems
   license
   faq
   relnotes
   thanks

.. toctree::
   :maxdepth: 2
   :caption: Download and Install
   :hidden:

   download
   install
   precompiled

.. toctree::
   :maxdepth: 1
   :caption: Need help?
   :hidden:

   lists
   bugs

.. toctree::
   :maxdepth: 1
   :caption: User Topics
   :hidden:

   shading
   egl
   opengles
   envvars
   osmesa
   debugging
   perf
   extensions
   application-issues
   viewperf
   xlibdriver

.. toctree::
   :maxdepth: 1
   :caption: Drivers
   :hidden:

   drivers/d3d12
   drivers/freedreno
   drivers/llvmpipe
   drivers/openswr
   drivers/panfrost
   drivers/v3d
   drivers/vc4
   drivers/vmware-guest
   drivers/zink

.. toctree::
   :maxdepth: 1
   :caption: Developer Topics
   :hidden:

   repository
   sourcetree
   utilities
   helpwanted
   devinfo
   codingstyle
   submittingpatches
   releasing
   release-calendar
   sourcedocs
   dispatch
   gallium/index
   android
   Linux Kernel Drivers <https://www.kernel.org/doc/html/latest/gpu/>

.. toctree::
   :maxdepth: 1
   :caption: Testing
   :hidden:

   conform
   ci/index

.. toctree::
   :maxdepth: 1
   :caption: Links
   :hidden:

   OpenGL Website <https://www.opengl.org>
   DRI Website <https://dri.freedesktop.org>
   Developer Blogs <https://planet.freedesktop.org>

.. toctree::
   :maxdepth: 1
   :caption: Hosted by:
   :hidden:

   freedesktop.org <https://www.freedesktop.org>
