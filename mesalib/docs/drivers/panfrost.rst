Panfrost
========

The Panfrost driver stack includes an OpenGL ES implementation for Arm Mali
GPUs based on the Midgard and Bifrost microarchitectures. It is **conformant**
on Mali G52 but **non-conformant** on other GPUs. The following hardware is
currently supported:

=========  ============ ============ =======
Product    Architecture OpenGL ES    OpenGL
=========  ============ ============ =======
Mali T720  Midgard (v4) 2.0          2.1
Mali T760  Midgard (v5) 3.1          3.1
Mali T820  Midgard (v5) 3.1          3.1
Mali T830  Midgard (v5) 3.1          3.1
Mali T860  Midgard (v5) 3.1          3.1
Mali T880  Midgard (v5) 3.1          3.1
Mali G72   Bifrost (v6) 3.1          3.1
Mali G31   Bifrost (v7) 3.1          3.1
Mali G51   Bifrost (v7) 3.1          3.1
Mali G52   Bifrost (v7) 3.1          3.1
Mali G76   Bifrost (v7) 3.1          3.1
=========  ============ ============ =======

Other Midgard and Bifrost chips (T604, T628, G71) are not yet supported.

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

Build like ``meson . build/ -Ddri-drivers= -Dvulkan-drivers=
-Dgallium-drivers=panfrost -Dllvm=disabled`` for a build directory
``build``.

For general information on building Mesa, read :doc:`the install documentation
<../install>`.

Chat
----

Panfrost developers and users hang out on IRC at ``#panfrost`` on OFTC. Note
that registering and authenticating with `NickServ` is required to prevent
spam. `Join the chat. <https://webchat.oftc.net/?channels=#panfrost>`_
