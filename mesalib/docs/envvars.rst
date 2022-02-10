Environment Variables
=====================

Normally, no environment variables need to be set. Most of the
environment variables used by Mesa/Gallium are for debugging purposes,
but they can sometimes be useful for debugging end-user issues.

LibGL environment variables
---------------------------

:envvar:`LIBGL_DEBUG`
   If defined debug information will be printed to stderr. If set to
   ``verbose`` additional information will be printed.
:envvar:`LIBGL_DRIVERS_PATH`
   colon-separated list of paths to search for DRI drivers
:envvar:`LIBGL_ALWAYS_INDIRECT`
   if set to ``true``, forces an indirect rendering context/connection.
:envvar:`LIBGL_ALWAYS_SOFTWARE`
   if set to ``true``, always use software rendering
:envvar:`LIBGL_NO_DRAWARRAYS`
   if set to ``true``, do not use DrawArrays GLX protocol (for
   debugging)
:envvar:`LIBGL_SHOW_FPS`
   print framerate to stdout based on the number of ``glXSwapBuffers``
   calls per second.
:envvar:`LIBGL_DRI2_DISABLE`
   disable DRI2 if set to ``true``.
:envvar:`LIBGL_DRI3_DISABLE`
   disable DRI3 if set to ``true``.

Core Mesa environment variables
-------------------------------

:envvar:`MESA_NO_ASM`
   if set, disables all assembly language optimizations
:envvar:`MESA_NO_MMX`
   if set, disables Intel MMX optimizations
:envvar:`MESA_NO_3DNOW`
   if set, disables AMD 3DNow! optimizations
:envvar:`MESA_NO_SSE`
   if set, disables Intel SSE optimizations
:envvar:`MESA_NO_ERROR`
   if set to 1, error checking is disabled as per ``KHR_no_error``. This
   will result in undefined behavior for invalid use of the API, but
   can reduce CPU use for apps that are known to be error free.
:envvar:`MESA_DEBUG`
   if set, error messages are printed to stderr. For example, if the
   application generates a ``GL_INVALID_ENUM`` error, a corresponding
   error message indicating where the error occurred, and possibly why,
   will be printed to stderr. For release builds, :envvar:`MESA_DEBUG`
   defaults to off (no debug output). :envvar:`MESA_DEBUG` accepts the
   following comma-separated list of named flags, which adds extra
   behavior to just set :envvar:`MESA_DEBUG` to ``1``:

   ``silent``
      turn off debug messages. Only useful for debug builds.
   ``flush``
      flush after each drawing command
   ``incomplete_tex``
      extra debug messages when a texture is incomplete
   ``incomplete_fbo``
      extra debug messages when a FBO is incomplete
   ``context``
      create a debug context (see ``GLX_CONTEXT_DEBUG_BIT_ARB``) and
      print error and performance messages to stderr (or
      ``MESA_LOG_FILE``).

:envvar:`MESA_LOG_FILE`
   specifies a file name for logging all errors, warnings, etc., rather
   than stderr
:envvar:`MESA_EXTENSION_OVERRIDE`
   can be used to enable/disable extensions. A value such as
   ``GL_EXT_foo -GL_EXT_bar`` will enable the ``GL_EXT_foo`` extension
   and disable the ``GL_EXT_bar`` extension. Note that this will override
   extensions override configured using driconf.
:envvar:`MESA_EXTENSION_MAX_YEAR`
   The ``GL_EXTENSIONS`` string returned by Mesa is sorted by extension
   year. If this variable is set to year X, only extensions defined on
   or before year X will be reported. This is to work-around a bug in
   some games where the extension string is copied into a fixed-size
   buffer without truncating. If the extension string is too long, the
   buffer overrun can cause the game to crash. This is a work-around for
   that.
:envvar:`MESA_GL_VERSION_OVERRIDE`
   changes the value returned by ``glGetString(GL_VERSION)`` and
   possibly the GL API type.

   -  The format should be ``MAJOR.MINOR[FC|COMPAT]``
   -  ``FC`` is an optional suffix that indicates a forward compatible
      context. This is only valid for versions >= 3.0.
   -  ``COMPAT`` is an optional suffix that indicates a compatibility
      context or ``GL_ARB_compatibility`` support. This is only valid
      for versions >= 3.1.
   -  GL versions <= 3.0 are set to a compatibility (non-Core) profile
   -  GL versions = 3.1, depending on the driver, it may or may not have
      the ``ARB_compatibility`` extension enabled.
   -  GL versions >= 3.2 are set to a Core profile
   -  Examples:

      ``2.1``
         select a compatibility (non-Core) profile with GL version 2.1.
      ``3.0``
         select a compatibility (non-Core) profile with GL version 3.0.
      ``3.0FC``
         select a Core+Forward Compatible profile with GL version 3.0.
      ``3.1``
         select GL version 3.1 with ``GL_ARB_compatibility`` enabled per
         the driver default.
      ``3.1FC``
         select GL version 3.1 with forward compatibility and
         ``GL_ARB_compatibility`` disabled.
      ``3.1COMPAT``
         select GL version 3.1 with ``GL_ARB_compatibility`` enabled.
      ``X.Y``
         override GL version to X.Y without changing the profile.
      ``X.YFC``
         select a Core+Forward Compatible profile with GL version X.Y.
      ``X.YCOMPAT``
         select a Compatibility profile with GL version X.Y.

   -  Mesa may not really implement all the features of the given
      version. (for developers only)

:envvar:`MESA_GLES_VERSION_OVERRIDE`
   changes the value returned by ``glGetString(GL_VERSION)`` for OpenGL
   ES.

   -  The format should be ``MAJOR.MINOR``
   -  Examples: ``2.0``, ``3.0``, ``3.1``
   -  Mesa may not really implement all the features of the given
      version. (for developers only)

:envvar:`MESA_GLSL_VERSION_OVERRIDE`
   changes the value returned by
   ``glGetString(GL_SHADING_LANGUAGE_VERSION)``. Valid values are
   integers, such as ``130``. Mesa will not really implement all the
   features of the given language version if it's higher than what's
   normally reported. (for developers only)
:envvar:`MESA_GLSL_CACHE_DISABLE`
   if set to ``true``, disables the GLSL shader cache. If set to
   ``false``, enables the GLSL shader cache when it is disabled by
   default.
:envvar:`MESA_GLSL_CACHE_MAX_SIZE`
   if set, determines the maximum size of the on-disk cache of compiled
   GLSL programs. Should be set to a number optionally followed by
   ``K``, ``M``, or ``G`` to specify a size in kilobytes, megabytes, or
   gigabytes. By default, gigabytes will be assumed. And if unset, a
   maximum size of 1GB will be used.

   .. note::

      A separate cache might be created for each architecture that Mesa is
      installed for on your system. For example under the default settings
      you may end up with a 1GB cache for x86_64 and another 1GB cache for
      i386.

:envvar:`MESA_GLSL_CACHE_DIR`
   if set, determines the directory to be used for the on-disk cache of
   compiled GLSL programs. If this variable is not set, then the cache
   will be stored in ``$XDG_CACHE_HOME/mesa_shader_cache`` (if that
   variable is set), or else within ``.cache/mesa_shader_cache`` within
   the user's home directory.
:envvar:`MESA_GLSL`
   :ref:`shading language compiler options <envvars>`
:envvar:`MESA_NO_MINMAX_CACHE`
   when set, the minmax index cache is globally disabled.
:envvar:`MESA_SHADER_CAPTURE_PATH`
   see :ref:`Capturing Shaders <capture>`
:envvar:`MESA_SHADER_DUMP_PATH` and :envvar:`MESA_SHADER_READ_PATH`
   see :ref:`Experimenting with Shader
   Replacements <replacement>`
:envvar:`MESA_VK_VERSION_OVERRIDE`
   changes the Vulkan physical device version as returned in
   ``VkPhysicalDeviceProperties::apiVersion``.

   -  The format should be ``MAJOR.MINOR[.PATCH]``
   -  This will not let you force a version higher than the driver's
      instance version as advertised by ``vkEnumerateInstanceVersion``
   -  This can be very useful for debugging but some features may not be
      implemented correctly. (For developers only)
:envvar:`MESA_VK_WSI_PRESENT_MODE`
   overrides the WSI present mode clients specify in
   ``VkSwapchainCreateInfoKHR::presentMode``. Values can be ``fifo``,
   ``relaxed``, ``mailbox`` or ``immediate``.
:envvar:`MESA_VK_ABORT_ON_DEVICE_LOSS`
   causes the Vulkan driver to call abort() immediately after detecting a
   lost device.  This is extremely useful when testing as it prevents the
   test suite from continuing on with a lost device.
:envvar:`MESA_VK_ENABLE_SUBMIT_THREAD`
   for Vulkan drivers which support real timeline semaphores, this forces
   them to use a submit thread from the beginning, regardless of whether or
   not they ever see a wait-before-signal condition.
:envvar:`MESA_LOADER_DRIVER_OVERRIDE`
   chooses a different driver binary such as ``etnaviv`` or ``zink``.

NIR passes environment variables
--------------------------------

The following are only applicable for drivers that uses NIR, as they
modify the behavior for the common ``NIR_PASS`` and ``NIR_PASS_V`` macros,
that wrap calls to NIR lowering/optimizations.

:envvar:`NIR_DEBUG`
   a comma-separated list of debug options to apply to NIR
   shaders. Use `NIR_DEBUG=help` to print a list of available options.
:envvar:`NIR_SKIP`
   a comma-separated list of optimization/lowering passes to skip.

Mesa Xlib driver environment variables
--------------------------------------

The following are only applicable to the Mesa Xlib software driver. See
the :doc:`Xlib software driver page <xlibdriver>` for details.

:envvar:`MESA_RGB_VISUAL`
   specifies the X visual and depth for RGB mode
:envvar:`MESA_BACK_BUFFER`
   specifies how to implement the back color buffer, either ``pixmap``
   or ``ximage``
:envvar:`MESA_XSYNC`
   enable synchronous X behavior (for debugging only)
:envvar:`MESA_GLX_FORCE_ALPHA`
   if set, forces RGB windows to have an alpha channel.
:envvar:`MESA_GLX_DEPTH_BITS`
   specifies default number of bits for depth buffer.
:envvar:`MESA_GLX_ALPHA_BITS`
   specifies default number of bits for alpha channel.

Intel driver environment variables
----------------------------------------------------

:envvar:`INTEL_BLACKHOLE_DEFAULT`
   if set to 1, true or yes, then the OpenGL implementation will
   default ``GL_BLACKHOLE_RENDER_INTEL`` to true, thus disabling any
   rendering.
:envvar:`INTEL_DEBUG`
   a comma-separated list of named flags, which do various things:

   ``ann``
      annotate IR in assembly dumps
   ``bat``
      emit batch information
   ``blit``
      emit messages about blit operations
   ``blorp``
      emit messages about the blorp operations (blits & clears)
   ``buf``
      emit messages about buffer objects
   ``bt``
      emit messages binding tables
   ``clip``
      emit messages about the clip unit (for old gens, includes the CLIP
      program)
   ``color``
      use color in output
   ``cs``
      dump shader assembly for compute shaders
   ``do32``
      generate compute shader SIMD32 programs even if workgroup size
      doesn't exceed the SIMD16 limit
   ``fall``
      emit messages about performance issues (same as ``perf``)
   ``fs``
      dump shader assembly for fragment shaders
   ``gs``
      dump shader assembly for geometry shaders
   ``hex``
      print instruction hex dump with the disassembly
   ``l3``
      emit messages about the new L3 state during transitions
   ``mesh``
      dump shader assembly for mesh shaders
   ``no8``
      don't generate SIMD8 fragment shader
   ``no16``
      suppress generation of 16-wide fragment shaders. useful for
      debugging broken shaders
   ``no32``
      suppress generation of 32-wide fragment shaders. useful for
      debugging broken shaders
   ``no-oaconfig``
      disable HW performance metric configuration, and anything
      related to i915-perf (useful when running on simulation)
   ``nocompact``
      disable instruction compaction
   ``nodualobj``
      suppress generation of dual-object geometry shader code
   ``nofc``
      disable fast clears
   ``norbc``
      disable single sampled render buffer compression
   ``optimizer``
      dump shader assembly to files at each optimization pass and
      iteration that make progress
   ``pc``
      emit messages about PIPE_CONTROL instruction usage
   ``perf``
      emit messages about performance issues
   ``perfmon``
      emit messages about ``AMD_performance_monitor``
   ``reemit``
      mark all state dirty on each draw call
   ``rt``
      dump shader assembly for ray tracing shaders
   ``sf``
      emit messages about the strips & fans unit (for old gens, includes
      the SF program)
   ``soft64``
      enable implementation of software 64bit floating point support
   ``spill_fs``
      force spilling of all registers in the scalar backend (useful to
      debug spilling code)
   ``spill_vec4``
      force spilling of all registers in the vec4 backend (useful to
      debug spilling code)
   ``submit``
      emit batchbuffer usage statistics
   ``sync``
      after sending each batch, emit a message and wait for that batch
      to finish rendering
   ``task``
      dump shader assembly for task shaders
   ``tcs``
      dump shader assembly for tessellation control shaders
   ``tcs8``
      force usage of 8-patches tessellation control shaders (only
      for gfx 9-11)
   ``tes``
      dump shader assembly for tessellation evaluation shaders
   ``tex``
      emit messages about textures.
   ``urb``
      emit messages about URB setup
   ``vs``
      dump shader assembly for vertex shaders
   ``wm``
      dump shader assembly for fragment shaders (same as ``fs``)

:envvar:`INTEL_MEASURE`
   Collects GPU timestamps over common intervals, and generates a CSV report
   to show how long rendering took.  The overhead of collection is limited to
   the flushing that is required at the interval boundaries for accurate
   timestamps. By default, timing data is sent to ``stderr``.  To direct output
   to a file:

   ``INTEL_MEASURE=file=/tmp/measure.csv {workload}``

   To begin capturing timestamps at a particular frame:

   ``INTEL_MEASURE=file=/tmp/measure.csv,start=15 {workload}``

   To capture only 23 frames:

   ``INTEL_MEASURE=count=23 {workload}``

   To capture frames 15-37, stopping before frame 38:

   ``INTEL_MEASURE=start=15,count=23 {workload}``

   Designate an asynchronous control file with:

   ``INTEL_MEASURE=control=path/to/control.fifo {workload}``

   As the workload runs, enable capture for 5 frames with:

   ``$ echo 5 > path/to/control.fifo``

   Enable unbounded capture:

   ``$ echo -1 > path/to/control.fifo``

   and disable with:

   ``$ echo 0 > path/to/control.fifo``

   Select the boundaries of each snapshot with:

   ``INTEL_MEASURE=draw``
      Collects timings for every render (DEFAULT)

   ``INTEL_MEASURE=rt``
      Collects timings when the render target changes

   ``INTEL_MEASURE=batch``
      Collects timings when batches are submitted

   ``INTEL_MEASURE=frame``
      Collects timings at frame boundaries

   With ``INTEL_MEASURE=interval=5``, the duration of 5 events will be
   combined into a single record in the output.  When possible, a single
   start and end event will be submitted to the GPU to minimize
   stalling.  Combined events will not span batches, except in
   the case of ``INTEL_MEASURE=frame``.
:envvar:`INTEL_NO_HW`
   if set to 1, true or yes, prevents batches from being submitted to the
   hardware. This is useful for debugging hangs, etc.
:envvar:`INTEL_PRECISE_TRIG`
   if set to 1, true or yes, then the driver prefers accuracy over
   performance in trig functions.
:envvar:`INTEL_SHADER_ASM_READ_PATH`
   if set, determines the directory to be used for overriding shader
   assembly. The binaries with custom assembly should be placed in
   this folder and have a name formatted as ``sha1_of_assembly.bin``.
   The sha1 of a shader assembly is printed when assembly is dumped via
   corresponding :envvar:`INTEL_DEBUG` flag (e.g. ``vs`` for vertex shader).
   A binary could be generated from a dumped assembly by ``i965_asm``.
   For :envvar:`INTEL_SHADER_ASM_READ_PATH` to work it is necessary to enable
   dumping of corresponding shader stages via :envvar:`INTEL_DEBUG`.
   It is advised to use ``nocompact`` flag of :envvar:`INTEL_DEBUG` when
   dumping and overriding shader assemblies.
   The success of assembly override would be signified by "Successfully
   overrode shader with sha1 <sha1>" in stderr replacing the original
   assembly.


DRI environment variables
-------------------------

:envvar:`DRI_NO_MSAA`
   disable MSAA for GLX/EGL MSAA visuals


EGL environment variables
-------------------------

Mesa EGL supports different sets of environment variables. See the
:doc:`Mesa EGL <egl>` page for the details.

Gallium environment variables
-----------------------------

:envvar:`GALLIUM_HUD`
   draws various information on the screen, like framerate, CPU load,
   driver statistics, performance counters, etc. Set
   :envvar:`GALLIUM_HUD` to ``help`` and run e.g. ``glxgears`` for more info.
:envvar:`GALLIUM_HUD_PERIOD`
   sets the HUD update rate in seconds (float). Use zero to update every
   frame. The default period is 1/2 second.
:envvar:`GALLIUM_HUD_VISIBLE`
   control default visibility, defaults to true.
:envvar:`GALLIUM_HUD_TOGGLE_SIGNAL`
   toggle visibility via user specified signal. Especially useful to
   toggle HUD at specific points of application and disable for
   unencumbered viewing the rest of the time. For example, set
   :envvar:`GALLIUM_HUD_VISIBLE` to ``false`` and
   :envvar:`GALLIUM_HUD_TOGGLE_SIGNAL` to ``10`` (``SIGUSR1``). Use
   ``kill -10 <pid>`` to toggle the HUD as desired.
:envvar:`GALLIUM_HUD_SCALE`
   Scale HUD by an integer factor, for high DPI displays. Default is 1.
:envvar:`GALLIUM_HUD_DUMP_DIR`
   specifies a directory for writing the displayed HUD values into
   files.
:envvar:`GALLIUM_DRIVER`
   useful in combination with :envvar:`LIBGL_ALWAYS_SOFTWARE`=`true` for
   choosing one of the software renderers ``softpipe`` or ``llvmpipe``.
:envvar:`GALLIUM_LOG_FILE`
   specifies a file for logging all errors, warnings, etc. rather than
   stderr.
:envvar:`GALLIUM_PIPE_SEARCH_DIR`
   specifies an alternate search directory for pipe-loader which overrides
   the compile-time path based on the install location.
:envvar:`GALLIUM_PRINT_OPTIONS`
   if non-zero, print all the Gallium environment variables which are
   used, and their current values.
:envvar:`GALLIUM_DUMP_CPU`
   if non-zero, print information about the CPU on start-up
:envvar:`TGSI_PRINT_SANITY`
   if set, do extra sanity checking on TGSI shaders and print any errors
   to stderr.
:envvar:`DRAW_FSE`
   Enable fetch-shade-emit middle-end even though its not correct (e.g.
   for softpipe)
:envvar:`DRAW_NO_FSE`
   Disable fetch-shade-emit middle-end even when it is correct
:envvar:`DRAW_USE_LLVM`
   if set to zero, the draw module will not use LLVM to execute shaders,
   vertex fetch, etc.
:envvar:`ST_DEBUG`
   controls debug output from the Mesa/Gallium state tracker. Setting to
   ``tgsi``, for example, will print all the TGSI shaders. See
   :file:`src/mesa/state_tracker/st_debug.c` for other options.

Clover environment variables
----------------------------

:envvar:`CLOVER_EXTRA_BUILD_OPTIONS`
   allows specifying additional compiler and linker options. Specified
   options are appended after the options set by the OpenCL program in
   ``clBuildProgram``.
:envvar:`CLOVER_EXTRA_COMPILE_OPTIONS`
   allows specifying additional compiler options. Specified options are
   appended after the options set by the OpenCL program in
   ``clCompileProgram``.
:envvar:`CLOVER_EXTRA_LINK_OPTIONS`
   allows specifying additional linker options. Specified options are
   appended after the options set by the OpenCL program in
   ``clLinkProgram``.

Softpipe driver environment variables
-------------------------------------

:envvar:`SOFTPIPE_DEBUG`
   a comma-separated list of named flags, which do various things:

   ``vs``
      Dump vertex shader assembly to stderr
   ``fs``
      Dump fragment shader assembly to stderr
   ``gs``
      Dump geometry shader assembly to stderr
   ``cs``
      Dump compute shader assembly to stderr
   ``no_rast``
      rasterization is disabled. For profiling purposes.
   ``use_llvm``
      the softpipe driver will try to use LLVM JIT for vertex
      shading processing.
   ``use_tgsi``
      if set, the softpipe driver will ask to directly consume TGSI, instead
      of NIR.

LLVMpipe driver environment variables
-------------------------------------

:envvar:`LP_NO_RAST`
   if set LLVMpipe will no-op rasterization
:envvar:`LP_DEBUG`
   a comma-separated list of debug options is accepted. See the source
   code for details.
:envvar:`LP_PERF`
   a comma-separated list of options to selectively no-op various parts
   of the driver. See the source code for details.
:envvar:`LP_NUM_THREADS`
   an integer indicating how many threads to use for rendering. Zero
   turns off threading completely. The default value is the number of
   CPU cores present.

VMware SVGA driver environment variables
----------------------------------------

:envvar`SVGA_FORCE_SWTNL`
   force use of software vertex transformation
:envvar`SVGA_NO_SWTNL`
   don't allow software vertex transformation fallbacks (will often
   result in incorrect rendering).
:envvar`SVGA_DEBUG`
   for dumping shaders, constant buffers, etc. See the code for details.
:envvar`SVGA_EXTRA_LOGGING`
   if set, enables extra logging to the ``vmware.log`` file, such as the
   OpenGL program's name and command line arguments.
:envvar`SVGA_NO_LOGGING`
   if set, disables logging to the ``vmware.log`` file. This is useful
   when using Valgrind because it otherwise crashes when initializing
   the host log feature.

See the driver code for other, lesser-used variables.

WGL environment variables
-------------------------

:envvar:`WGL_SWAP_INTERVAL`
   to set a swap interval, equivalent to calling
   ``wglSwapIntervalEXT()`` in an application. If this environment
   variable is set, application calls to ``wglSwapIntervalEXT()`` will
   have no effect.

VA-API environment variables
----------------------------

:envvar:`VAAPI_MPEG4_ENABLED`
   enable MPEG4 for VA-API, disabled by default.

VC4 driver environment variables
--------------------------------

:envvar:`VC4_DEBUG`
   a comma-separated list of named flags, which do various things:

   ``cl``
      dump command list during creation
   ``qpu``
      dump generated QPU instructions
   ``qir``
      dump QPU IR during program compile
   ``nir``
      dump NIR during program compile
   ``tgsi``
      dump TGSI during program compile
   ``shaderdb``
      dump program compile information for shader-db analysis
   ``perf``
      print during performance-related events
   ``norast``
      skip actual hardware execution of commands
   ``always_flush``
      flush after each draw call
   ``always_sync``
      wait for finish after each flush
   ``dump``
      write a GPU command stream trace file (VC4 simulator only)

RADV driver environment variables
---------------------------------

:envvar:`RADV_DEBUG`
   a comma-separated list of named flags, which do various things:

   ``llvm``
      enable LLVM compiler backend
   ``allbos``
      force all allocated buffers to be referenced in submissions
   ``checkir``
      validate the LLVM IR before LLVM compiles the shader
   ``forcecompress``
      Enables DCC,FMASK,CMASK,HTILE in situations where the driver supports it
      but normally does not deem it beneficial.
   ``hang``
      enable GPU hangs detection and dump a report to
      $HOME/radv_dumps_<pid>_<time> if a GPU hang is detected
   ``img``
      Print image info
   ``info``
      show GPU-related information
   ``invariantgeom``
      Mark geometry-affecting outputs as invariant. This works around a common
      class of application bugs appearing as flickering.
   ``metashaders``
      dump internal meta shaders
   ``noatocdithering``
      disable dithering for alpha to coverage
   ``nobinning``
      disable primitive binning
   ``nocache``
      disable shaders cache
   ``nocompute``
      disable compute queue
   ``nodcc``
      disable Delta Color Compression (DCC) on images
   ``nodisplaydcc``
      disable Delta Color Compression (DCC) on displayable images
   ``nodynamicbounds``
      do not check OOB access for dynamic descriptors
   ``nofastclears``
      disable fast color/depthstencil clears
   ``nohiz``
      disable HIZ for depthstencil images
   ``noibs``
      disable directly recording command buffers in GPU-visible memory
   ``nomemorycache``
      disable memory shaders cache
   ``nongg``
      disable NGG for GFX10+
   ``nonggc``
      disable NGG culling on GPUs where it's enabled by default (GFX10.3+ only).
   ``nooutoforder``
      disable out-of-order rasterization
   ``notccompatcmask``
      disable TC-compat CMASK for MSAA surfaces
   ``noumr``
      disable UMR dumps during GPU hang detection (only with
      :envvar:`RADV_DEBUG`=``hang``)
   ``novrsflatshading``
      disable VRS for flat shading (only on GFX10.3+)
   ``preoptir``
      dump LLVM IR before any optimizations
   ``prologs``
      dump vertex shader prologs
   ``shaders``
      dump shaders
   ``shaderstats``
      dump shader statistics
   ``spirv``
      dump SPIR-V
   ``splitfma``
      split application-provided fused multiply-add in geometry stages
   ``startup``
      display info at startup
   ``syncshaders``
      synchronize shaders after all draws/dispatches
   ``vmfaults``
      check for VM memory faults via dmesg
   ``zerovram``
      initialize all memory allocated in VRAM as zero

:envvar:`RADV_FORCE_FAMILY`
   create a null device to compile shaders without a AMD GPU (e.g. VEGA10)

:envvar:`RADV_FORCE_VRS`
   allow to force per-pipeline vertex VRS rates on GFX10.3+. This is only
   forced for pipelines that don't explicitely use VRS or flat shading.
   The supported values are 2x2, 1x2 and 2x1. Only for testing purposes.

:envvar:`RADV_PERFTEST`
   a comma-separated list of named flags, which do various things:

   ``bolist``
      enable the global BO list
   ``cswave32``
      enable wave32 for compute shaders (GFX10+)
   ``dccmsaa``
      enable DCC for MSAA images
   ``force_emulate_rt``
      forces ray-tracing to be emulated in software,
      even if there is hardware support.
   ``gewave32``
      enable wave32 for vertex/tess/geometry shaders (GFX10+)
   ``localbos``
      enable local BOs
   ``nosam``
      disable optimizations that get enabled when all VRAM is CPU visible.
   ``nv_ms``
      enable unofficial experimental support for NV_mesh_shader.
   ``pswave32``
      enable wave32 for pixel shaders (GFX10+)
   ``nggc``
      enable NGG culling on GPUs where it's not enabled by default (GFX10.1 only).
   ``rt``
      enable rt extensions whose implementation is still experimental.
   ``sam``
      enable optimizations to move more driver internal objects to VRAM.
   ``rtwave64``
      enable wave64 for ray tracing shaders (GFX10+)

:envvar:`RADV_TEX_ANISO`
   force anisotropy filter (up to 16)

:envvar:`RADV_THREAD_TRACE`
   enable frame based SQTT/RGP captures (eg. `export RADV_THREAD_TRACE=100`
   will capture the frame #100)

:envvar:`RADV_THREAD_TRACE_BUFFER_SIZE`
   set the SQTT/RGP buffer size in bytes (default value is 32MiB, the buffer is
   automatically resized if too small)

:envvar:`RADV_THREAD_TRACE_CACHE_COUNTERS`
   enable/disable SQTT/RGP cache counters on GFX10+ (disabled by default)

:envvar:`RADV_THREAD_TRACE_INSTRUCTION_TIMING`
   enable/disable SQTT/RGP instruction timing (enabled by default)

:envvar:`RADV_THREAD_TRACE_TRIGGER`
   enable trigger file based SQTT/RGP captures (eg.
   `export RADV_THREAD_TRACE_TRIGGER=/tmp/radv_sqtt_trigger` and then
   `touch /tmp/radv_sqtt_trigger` to capture a frame)

:envvar:`ACO_DEBUG`
   a comma-separated list of named flags, which do various things:

   ``validateir``
      validate the ACO IR at various points of compilation (enabled by
      default for debug/debugoptimized builds)
   ``validatera``
      validate register assignment of ACO IR and catches many RA bugs
   ``perfwarn``
      abort on some suboptimal code generation
   ``force-waitcnt``
      force emitting waitcnt states if there is something to wait for
   ``novn``
      disable value numbering
   ``noopt``
      disable various optimizations
   ``noscheduling``
      disable instructions scheduling
   ``perfinfo``
      print information used to calculate some pipeline statistics
   ``liveinfo``
      print liveness and register demand information before scheduling

radeonsi driver environment variables
-------------------------------------

:envvar:`AMD_DEBUG`
   a comma-separated list of named flags, which do various things:

   ``nodcc``
      Disable DCC.
   ``nodccclear``
      Disable DCC fast clear.
   ``nodccmsaa``
      Disable DCC for MSAA
   ``nodpbb``
      Disable DPBB.
   ``nodfsm``
      Disable DFSM.
   ``notiling``
      Disable tiling
   ``nofmask``
      Disable MSAA compression
   ``nohyperz``
      Disable Hyper-Z
   ``no2d``
      Disable 2D tiling
   ``info``
      Print driver information
   ``tex``
      Print texture info
   ``compute``
      Print compute info
   ``vm``
      Print virtual addresses when creating resources
   ``vs``
      Print vertex shaders
   ``ps``
      Print pixel shaders
   ``gs``
      Print geometry shaders
   ``tcs``
      Print tessellation control shaders
   ``tes``
      Print tessellation evaluation shaders
   ``cs``
      Print compute shaders
   ``noir``
      Don't print the LLVM IR
   ``nonir``
      Don't print NIR when printing shaders
   ``noasm``
      Don't print disassembled shaders
   ``preoptir``
      Print the LLVM IR before initial optimizations
   ``gisel``
      Enable LLVM global instruction selector.
   ``w32ge``
      Use Wave32 for vertex, tessellation, and geometry shaders.
   ``w32ps``
      Use Wave32 for pixel shaders.
   ``w32cs``
      Use Wave32 for computes shaders.
   ``w64ge``
      Use Wave64 for vertex, tessellation, and geometry shaders.
   ``w64ps``
      Use Wave64 for pixel shaders.
   ``w64cs``
      Use Wave64 for computes shaders.
   ``checkir``
      Enable additional sanity checks on shader IR
   ``mono``
      Use old-style monolithic shaders compiled on demand
   ``nooptvariant``
      Disable compiling optimized shader variants.
   ``nowc``
      Disable GTT write combining
   ``check_vm``
      Check VM faults and dump debug info.
   ``reserve_vmid``
      Force VMID reservation per context.
   ``nogfx``
      Disable graphics. Only multimedia compute paths can be used.
   ``nongg``
      Disable NGG and use the legacy pipeline.
   ``nggc``
      Always use NGG culling even when it can hurt.
   ``nonggc``
      Disable NGG culling.
   ``switch_on_eop``
      Program WD/IA to switch on end-of-packet.
   ``nooutoforder``
      Disable out-of-order rasterization
   ``dpbb``
      Enable DPBB.
   ``dfsm``
      Enable DFSM.

r600 driver environment variables
---------------------------------

:envvar:`R600_DEBUG`
   a comma-separated list of named flags, which do various things:

   ``nocpdma``
      Disable CP DMA
   ``nosb``
      Disable sb backend for graphics shaders
   ``sbcl``
      Enable sb backend for compute shaders
   ``sbdry``
      Don't use optimized bytecode (just print the dumps)
   ``sbstat``
      Print optimization statistics for shaders
   ``sbdump``
      Print IR dumps after some optimization passes
   ``sbnofallback``
      Abort on errors instead of fallback
   ``sbdisasm``
      Use sb disassembler for shader dumps
   ``sbsafemath``
      Disable unsafe math optimizations
   ``nirsb``
      Enable NIR with SB optimizer
   ``tex``
      Print texture info
   ``nir``
      Enable experimental NIR shaders
   ``compute``
      Print compute info
   ``vm``
      Print virtual addresses when creating resources
   ``info``
      Print driver information
   ``fs``
      Print fetch shaders
   ``vs``
      Print vertex shaders
   ``gs``
      Print geometry shaders
   ``ps``
      Print pixel shaders
   ``cs``
      Print compute shaders
   ``tcs``
      Print tessellation control shaders
   ``tes``
      Print tessellation evaluation shaders
   ``noir``
      Don't print the LLVM IR
   ``notgsi``
      Don't print the TGSI
   ``noasm``
      Don't print disassembled shaders
   ``preoptir``
      Print the LLVM IR before initial optimizations
   ``checkir``
      Enable additional sanity checks on shader IR
   ``nooptvariant``
      Disable compiling optimized shader variants.
   ``testdma``
      Invoke SDMA tests and exit.
   ``testvmfaultcp``
      Invoke a CP VM fault test and exit.
   ``testvmfaultsdma``
      Invoke a SDMA VM fault test and exit.
   ``testvmfaultshader``
      Invoke a shader VM fault test and exit.
   ``nodma``
      Disable asynchronous DMA
   ``nohyperz``
      Disable Hyper-Z
   ``noinvalrange``
      Disable handling of INVALIDATE_RANGE map flags
   ``no2d``
      Disable 2D tiling
   ``notiling``
      Disable tiling
   ``switch_on_eop``
      Program WD/IA to switch on end-of-packet.
   ``forcedma``
      Use asynchronous DMA for all operations when possible.
   ``precompile``
      Compile one shader variant at shader creation.
   ``nowc``
      Disable GTT write combining
   ``check_vm``
      Check VM faults and dump debug info.
   ``unsafemath``
      Enable unsafe math shader optimizations

:envvar:`R600_DEBUG_COMPUTE`
   if set to ``true``, various compute-related debug information will
   be printed to stderr. Defaults to ``false``.
:envvar:`R600_DUMP_SHADERS`
   if set to ``true``, NIR shaders will be printed to stderr. Defaults
   to ``false``.
:envvar:`R600_HYPERZ`
   If set to ``false``, disables HyperZ optimizations. Defaults to ``true``.
:envvar:`R600_NIR_DEBUG`
   a comma-separated list of named flags, which do various things:

   ``instr``
      Log all consumed nir instructions
   ``ir``
      Log created R600 IR
   ``cc``
      Log R600 IR to assembly code creation
   ``noerr``
      Don't log shader conversion errors
   ``si``
      Log shader info (non-zero values)
   ``reg``
      Log register allocation and lookup
   ``io``
      Log shader in and output
   ``ass``
      Log IR to assembly conversion
   ``flow``
      Log control flow instructions
   ``merge``
      Log register merge operations
   ``nomerge``
      Skip register merge step
   ``tex``
      Log texture ops
   ``trans``
      Log generic translation messages

r300 driver environment variables
---------------------------------

:envvar:`RADEON_DEBUG`
   a comma-separated list of named flags, which do various things:

   ``info``
      Print hardware info (printed by default on debug builds
   ``fp``
      Log fragment program compilation
   ``vp``
      Log vertex program compilation
   ``draw``
      Log draw calls
   ``swtcl``
      Log SWTCL-specific info
   ``rsblock``
      Log rasterizer registers
   ``psc``
      Log vertex stream registers
   ``tex``
      Log basic info about textures
   ``texalloc``
      Log texture mipmap tree info
   ``rs``
      Log rasterizer
   ``fb``
      Log framebuffer
   ``cbzb``
      Log fast color clear info
   ``hyperz``
      Log HyperZ info
   ``scissor``
      Log scissor info
   ``msaa``
      Log MSAA resources
   ``anisohq``
      Use high quality anisotropic filtering
   ``notiling``
      Disable tiling
   ``noimmd``
      Disable immediate mode
   ``noopt``
      Disable shader optimizations
   ``nocbzb``
      Disable fast color clear
   ``nozmask``
      Disable zbuffer compression
   ``nohiz``
      Disable hierarchical zbuffer
   ``nocmask``
      Disable AA compression and fast AA clear
   ``use_tgsi``
      Request TGSI shaders from the state tracker
   ``notcl``
      Disable hardware accelerated Transform/Clip/Lighting

Other Gallium drivers have their own environment variables. These may
change frequently so the source code should be consulted for details.
