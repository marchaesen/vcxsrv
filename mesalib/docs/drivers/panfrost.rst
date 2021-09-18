Panfrost
========

The Panfrost driver stack includes a **non-conformant** OpenGL ES
implementation for Arm Mali GPUs based on the Midgard and Bifrost
microarchitectures. The following GPUs are currently supported:

=========  ============ ============ =======
Product    Architecture OpenGL ES    OpenGL
=========  ============ ============ =======
Mali T720  Midgard (v4) 2.0          2.1
Mali T760  Midgard (v5) 3.1          3.1
Mali T820  Midgard (v5) 3.1          3.1
Mali T860  Midgard (v5) 3.1          3.1
Mali G72   Bifrost (v6) 3.1          3.1
Mali G31   Bifrost (v7) 3.1          3.1
Mali G52   Bifrost (v7) 3.1          3.1
=========  ============ ============ =======

Other Midgard and Bifrost chips (T604, T620, T830, T880, G71, G51, G76) may
work but may be buggy. End users are advised against using Panfrost on
unsupported hardware. Developers interested in porting will need to allowlist
the hardware (``src/gallium/drivers/panfrost/pan_screen.c``).

Older Mali chips based on the Utgard architecture (Mali 400, Mali 450) are
supported in the Lima driver, not Panfrost. Lima is also available in Mesa.

Other graphics APIs (Vulkan, OpenCL) are not supported at this time.

Building
--------

Panfrost's OpenGL support is a Gallium driver. Since Mali GPUs are 3D-only and
do not include a display controller, Mesa uses kmsro to support display
controllers paired with Mali GPUs. If your board with a Panfrost supported GPU
has a display controller with mainline Linux support not supported by kmsro,
it's easy to add support, see the commit ``cff7de4bb597e9`` as an example.

LLVM is *not* required by Panfrost's compilers. LLVM support in Mesa can
safely be disabled for most OpenGL ES users with Panfrost.

Build with meson like ``meson . build/ -Ddri-drivers= -Dvulkan-drivers=
-Dgallium-drivers=panfrost -Dllvm=disabled`` for a build directory
``build``.

Building for Android via the legacy ``Android.mk`` system is not officially
supported but reportedly works. Your mileage may vary.

For general information on building Mesa, read :doc:`the install documentation
<../install>`.

Chat
----

Panfrost developers and users hang out on IRC at ``#panfrost`` on OFTC.
