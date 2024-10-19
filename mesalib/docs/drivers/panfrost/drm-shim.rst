
drm-shim
========

Panfrost implements ``drm-shim``, stubbing out the Panfrost kernel interface.
Use cases for this functionality include:

- Future hardware bring up
- Running shader-db on non-Mali workstations
- Reproducing compiler (and some driver) bugs without Mali hardware

Although Mali hardware is usually paired with an Arm CPU, Panfrost is portable C
code and should work on any Linux machine. In particular, you can test the
compiler on shader-db on an Intel desktop.

To build Mesa with Panfrost drm-shim, configure Meson with
``-Dgallium-drivers=panfrost`` and ``-Dtools=drm-shim``. See the above
building section for a full invocation. The drm-shim binary will be built to
``build/src/panfrost/drm-shim/libpanfrost_noop_drm_shim.so``.

To use, set the ``LD_PRELOAD`` environment variable to the drm-shim binary.

By default, drm-shim mocks a Mali-G52 system. To select a specific Mali GPU,
set the ``PAN_GPU_ID`` environment variable to the desired GPU ID:

=========  ============= =======
Product    Architecture  GPU ID
=========  ============= =======
Mali-T720  Midgard (v4)  720
Mali-T860  Midgard (v5)  860
Mali-G72   Bifrost (v6)  6221
Mali-G52   Bifrost (v7)  7212
Mali-G57   Valhall (v9)  9093
Mali-G610  Valhall (v10) a867
=========  ============= =======

Additional GPU IDs are enumerated in the ``panfrost_model_list`` list in
``src/panfrost/lib/pan_props.c``.

As an example: assuming Mesa is installed to a local path ``~/lib`` and Mesa's
build directory is ``~/mesa/build``, a shader can be compiled for Mali-G52 as:

.. code-block:: sh

   ~/shader-db$ BIFROST_MESA_DEBUG=shaders \
   LD_PRELOAD=~/mesa/build/src/panfrost/drm-shim/libpanfrost_noop_drm_shim.so \
   PAN_GPU_ID=7212 \
   ./run shaders/glmark/1-1.shader_test

The same shader can be compiled for Mali-T720 as:

.. code-block:: sh

   ~/shader-db$ MIDGARD_MESA_DEBUG=shaders \
   LD_PRELOAD=~/mesa/build/src/panfrost/drm-shim/libpanfrost_noop_drm_shim.so \
   PAN_GPU_ID=720 \
   ./run shaders/glmark/1-1.shader_test

These examples set the compilers' ``shaders`` debug flags to dump the optimized
NIR, backend IR after instruction selection, backend IR after register
allocation and scheduling, and a disassembly of the final compiled binary.

As another example, this invocation runs a single dEQP test "on" Mali-G52,
pretty-printing GPU data structures and disassembling all shaders
(``PAN_MESA_DEBUG=trace``) as well as dumping raw GPU memory
(``PAN_MESA_DEBUG=dump``). The ``EGL_PLATFORM=surfaceless`` environment variable
and various flags to dEQP mimic the surfaceless environment that our
continuous integration (CI) uses. This eliminates window system dependencies,
although it requires a specially built CTS:

.. code-block:: sh

   ~/VK-GL-CTS/build/external/openglcts/modules$ PAN_MESA_DEBUG=trace,dump \
   LD_PRELOAD=~/mesa/build/src/panfrost/drm-shim/libpanfrost_noop_drm_shim.so \
   PAN_GPU_ID=7212 EGL_PLATFORM=surfaceless \
   ./glcts --deqp-surface-type=pbuffer \
   --deqp-gl-config-name=rgba8888d24s8ms0 --deqp-surface-width=256 \
   --deqp-surface-height=256 -n \
   dEQP-GLES31.functional.shaders.builtin_functions.common.abs.float_highp_compute
