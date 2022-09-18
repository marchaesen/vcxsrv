Panfrost
========

The Panfrost driver stack includes an OpenGL ES implementation for Arm Mali
GPUs based on the Midgard and Bifrost microarchitectures. It is **conformant**
on Mali-G52 and Mali-G57 but **non-conformant** on other GPUs. The following
hardware is currently supported:

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
Mali G57   Valhall (v9) 3.1          3.1
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

drm-shim
--------

Panfrost implements ``drm-shim``, stubbing out the Panfrost kernel interface.
Use cases for this functionality include:

- Future hardware bring up
- Running shader-db on non-Mali workstations
- Reproducing compiler (and some driver) bugs without Mali hardware

Although Mali hardware is usually paired with an Arm CPU, Panfrost is portable C
code and should work on any Linux machine. In particular, you can test the
compiler on shader-db on an Intel desktop.

To build Mesa with Panfrost drm-shim, configure meson with
``-Dgallium-drivers=panfrost`` and ``-Dtools=drm-shim``. See the above
building section for a full invocation. The drm-shim binary will be built to
``build/src/panfrost/drm-shim/libpanfrost_noop_drm_shim.so``.

To use, set the ``LD_PRELOAD`` environment variable to the drm-shim binary.  It
may also be necessary to set ``LIBGL_DRIVERS_PATH`` to the location where Mesa
was installed.

By default, drm-shim mocks a Mali-G52 system. To select a specific Mali GPU,
set the ``PAN_GPU_ID`` environment variable to the desired GPU ID:

=========  ============ =======
Product    Architecture GPU ID
=========  ============ =======
Mali-T720  Midgard (v4) 720
Mali-T860  Midgard (v5) 860
Mali-G72   Bifrost (v6) 6221
Mali-G52   Bifrost (v7) 7212
Mali-G57   Valhall (v9) 9093
=========  ============ =======

Additional GPU IDs are enumerated in the ``panfrost_model_list`` list in
``src/panfrost/lib/pan_props.c``.

As an example: assuming Mesa is installed to a local path ``~/lib`` and Mesa's
build directory is ``~/mesa/build``, a shader can be compiled for Mali-G52 as::

   ~/shader-db$ BIFROST_MESA_DEBUG=shaders LIBGL_DRIVERS_PATH=~/lib/dri/ LD_PRELOAD=~/mesa/build/src/panfrost/drm-shim/libpanfrost_noop_drm_shim.so PAN_GPU_ID=7212 ./run shaders/glmark/1-1.shader_test

The same shader can be compiled for Mali-T720 as::

   ~/shader-db$ MIDGARD_MESA_DEBUG=shaders LIBGL_DRIVERS_PATH=~/lib/dri/ LD_PRELOAD=~/mesa/build/src/panfrost/drm-shim/libpanfrost_noop_drm_shim.so PAN_GPU_ID=720 ./run shaders/glmark/1-1.shader_test

These examples set the compilers' ``shaders`` debug flags to dump the optimized
NIR, backend IR after instruction selection, backend IR after register
allocation and scheduling, and a disassembly of the final compiled binary.

As another example, this invocation runs a single dEQP test "on" Mali-G52,
pretty-printing GPU data structures and disassembling all shaders
(``PAN_MESA_DEBUG=trace``) as well as dumping raw GPU memory
(``PAN_MESA_DEBUG=dump``). The ``EGL_PLATFORM=surfaceless`` environment variable
and various flags to dEQP mimic the surfaceless environment that our
continuous integration (CI) uses. This eliminates window system dependencies,
although it requires a specially built CTS::

   ~/VK-GL-CTS/build/external/openglcts/modules$ PAN_MESA_DEBUG=trace,dump LIBGL_DRIVERS_PATH=~/lib/dri/ LD_PRELOAD=~/mesa/build/src/panfrost/drm-shim/libpanfrost_noop_drm_shim.so PAN_GPU_ID=7212 EGL_PLATFORM=surfaceless ./glcts --deqp-surface-type=pbuffer --deqp-gl-config-name=rgba8888d24s8ms0 --deqp-surface-width=256 --deqp-surface-height=256 -n dEQP-GLES31.functional.shaders.builtin_functions.common.abs.float_highp_compute

U-interleaved tiling
---------------------

Panfrost supports u-interleaved tiling. U-interleaved tiling is
indicated by the ``DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED`` modifier.

The tiling reorders whole pixels (blocks). It does not compress or modify the
pixels themselves, so it can be used for any image format. Internally, images
are divided into tiles. Tiles occur in source order, but pixels (blocks) within
each tile are reordered according to a space-filling curve.

For regular formats, 16x16 tiles are used. This harmonizes with the default tile
size for binning and CRCs (transaction elimination). It also means a single line
(16 pixels) at 4 bytes per pixel equals a single 64-byte cache line.

For formats that are already block compressed (S3TC, RGTC, etc), 4x4 tiles are
used, where entire blocks are reorder. Most of these formats compress 4x4
blocks, so this gives an effective 16x16 tiling. This justifies the tile size
intuitively, though it's not a rule: ASTC may uses larger blocks.

Within a tile, the X and Y bits are interleaved (like Morton order), but with a
twist: adjacent bit pairs are XORed. The reason to add XORs is not obvious.
Visually, addresses take the form::

   | y3 | (x3 ^ y3) | y2 | (y2 ^ x2) | y1 | (y1 ^ x1) | y0 | (y0 ^ x0) |

Reference routines to encode/decode u-interleaved images are available in
``src/panfrost/shared/test/test-tiling.cpp``, which documents the space-filling
curve. This reference implementation is used to unit test the optimized
implementation used in production. The optimized implementation is available in
``src/panfrost/shared/pan_tiling.c``.

Although these routines are part of Panfrost, they are also used by Lima, as Arm
introduced the format with Utgard. It is the only tiling supported on Utgard. On
Mali-T760 and newer, Arm Framebuffer Compression (AFBC) is more efficient and
should be used instead where possible. However, not all formats are
compressible, so u-interleaved tiling remains an important fallback on Panfrost.

