Environment Variables
=====================

Normally, no environment variables need to be set. Most of the
environment variables used by Mesa/Gallium are for debugging purposes,
but they can sometimes be useful for debugging end-user issues.

LibGL environment variables
---------------------------

``LIBGL_DEBUG``
   If defined debug information will be printed to stderr. If set to
   ``verbose`` additional information will be printed.
``LIBGL_DRIVERS_PATH``
   colon-separated list of paths to search for DRI drivers
``LIBGL_ALWAYS_INDIRECT``
   if set to ``true``, forces an indirect rendering context/connection.
``LIBGL_ALWAYS_SOFTWARE``
   if set to ``true``, always use software rendering
``LIBGL_NO_DRAWARRAYS``
   if set to ``true``, do not use DrawArrays GLX protocol (for
   debugging)
``LIBGL_SHOW_FPS``
   print framerate to stdout based on the number of ``glXSwapBuffers``
   calls per second.
``LIBGL_DRI3_DISABLE``
   disable DRI3 if set to ``true``.

Core Mesa environment variables
-------------------------------

``MESA_NO_ASM``
   if set, disables all assembly language optimizations
``MESA_NO_MMX``
   if set, disables Intel MMX optimizations
``MESA_NO_3DNOW``
   if set, disables AMD 3DNow! optimizations
``MESA_NO_SSE``
   if set, disables Intel SSE optimizations
``MESA_NO_ERROR``
   if set to 1, error checking is disabled as per ``KHR_no_error``. This
   will result in undefined behaviour for invalid use of the api, but
   can reduce CPU use for apps that are known to be error free.
``MESA_DEBUG``
   if set, error messages are printed to stderr. For example, if the
   application generates a ``GL_INVALID_ENUM`` error, a corresponding
   error message indicating where the error occurred, and possibly why,
   will be printed to stderr. For release builds, ``MESA_DEBUG``
   defaults to off (no debug output). ``MESA_DEBUG`` accepts the
   following comma-separated list of named flags, which adds extra
   behaviour to just set ``MESA_DEBUG=1``:

   ``silent``
      turn off debug messages. Only useful for debug builds.
   ``flush``
      flush after each drawing command
   ``incomplete_tex``
      extra debug messages when a texture is incomplete
   ``incomplete_fbo``
      extra debug messages when a fbo is incomplete
   ``context``
      create a debug context (see ``GLX_CONTEXT_DEBUG_BIT_ARB``) and
      print error and performance messages to stderr (or
      ``MESA_LOG_FILE``).

``MESA_LOG_FILE``
   specifies a file name for logging all errors, warnings, etc., rather
   than stderr
``MESA_TEX_PROG``
   if set, implement conventional texture env modes with fragment
   programs (intended for developers only)
``MESA_TNL_PROG``
   if set, implement conventional vertex transformation operations with
   vertex programs (intended for developers only). Setting this variable
   automatically sets the ``MESA_TEX_PROG`` variable as well.
``MESA_EXTENSION_OVERRIDE``
   can be used to enable/disable extensions. A value such as
   ``GL_EXT_foo -GL_EXT_bar`` will enable the ``GL_EXT_foo`` extension
   and disable the ``GL_EXT_bar`` extension.
``MESA_EXTENSION_MAX_YEAR``
   The ``GL_EXTENSIONS`` string returned by Mesa is sorted by extension
   year. If this variable is set to year X, only extensions defined on
   or before year X will be reported. This is to work-around a bug in
   some games where the extension string is copied into a fixed-size
   buffer without truncating. If the extension string is too long, the
   buffer overrun can cause the game to crash. This is a work-around for
   that.
``MESA_GL_VERSION_OVERRIDE``
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

``MESA_GLES_VERSION_OVERRIDE``
   changes the value returned by ``glGetString(GL_VERSION)`` for OpenGL
   ES.

   -  The format should be ``MAJOR.MINOR``
   -  Examples: ``2.0``, ``3.0``, ``3.1``
   -  Mesa may not really implement all the features of the given
      version. (for developers only)

``MESA_GLSL_VERSION_OVERRIDE``
   changes the value returned by
   ``glGetString(GL_SHADING_LANGUAGE_VERSION)``. Valid values are
   integers, such as ``130``. Mesa will not really implement all the
   features of the given language version if it's higher than what's
   normally reported. (for developers only)
``MESA_GLSL_CACHE_DISABLE``
   if set to ``true``, disables the GLSL shader cache
``MESA_GLSL_CACHE_MAX_SIZE``
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

``MESA_GLSL_CACHE_DIR``
   if set, determines the directory to be used for the on-disk cache of
   compiled GLSL programs. If this variable is not set, then the cache
   will be stored in ``$XDG_CACHE_HOME/mesa_shader_cache`` (if that
   variable is set), or else within ``.cache/mesa_shader_cache`` within
   the user's home directory.
``MESA_GLSL``
   :ref:`shading language compiler options <envvars>`
``MESA_NO_MINMAX_CACHE``
   when set, the minmax index cache is globally disabled.
``MESA_SHADER_CAPTURE_PATH``
   see :ref:`Capturing Shaders <capture>`
``MESA_SHADER_DUMP_PATH`` and ``MESA_SHADER_READ_PATH``
   see :ref:`Experimenting with Shader
   Replacements <replacement>`
``MESA_VK_VERSION_OVERRIDE``
   changes the Vulkan physical device version as returned in
   ``VkPhysicalDeviceProperties::apiVersion``.

   -  The format should be ``MAJOR.MINOR[.PATCH]``
   -  This will not let you force a version higher than the driver's
      instance version as advertised by ``vkEnumerateInstanceVersion``
   -  This can be very useful for debugging but some features may not be
      implemented correctly. (For developers only)

NIR passes enviroment variables
-------------------------------

The following are only applicable for drivers that uses NIR, as they
modify the behaviour for the common NIR_PASS and NIR_PASS_V macros, that
wrap calls to NIR lowering/optimizations.

``NIR_PRINT``
   If defined, the resulting NIR shader will be printed out at each
   succesful NIR lowering/optimization call.
``NIR_TEST_CLONE``
   If defined, cloning a NIR shader would be tested at each succesful
   NIR lowering/optimization call.
``NIR_TEST_SERIALIZE``
   If defined, serialize and deserialize a NIR shader would be tested at
   each succesful NIR lowering/optimization call.

Mesa Xlib driver environment variables
--------------------------------------

The following are only applicable to the Mesa Xlib software driver. See
the :doc:`Xlib software driver page <xlibdriver>` for details.

``MESA_RGB_VISUAL``
   specifies the X visual and depth for RGB mode
``MESA_CI_VISUAL``
   specifies the X visual and depth for CI mode
``MESA_BACK_BUFFER``
   specifies how to implement the back color buffer, either ``pixmap``
   or ``ximage``
``MESA_GAMMA``
   gamma correction coefficients for red, green, blue channels
``MESA_XSYNC``
   enable synchronous X behavior (for debugging only)
``MESA_GLX_FORCE_CI``
   if set, force GLX to treat 8bpp visuals as CI visuals
``MESA_GLX_FORCE_ALPHA``
   if set, forces RGB windows to have an alpha channel.
``MESA_GLX_DEPTH_BITS``
   specifies default number of bits for depth buffer.
``MESA_GLX_ALPHA_BITS``
   specifies default number of bits for alpha channel.

i945/i965 driver environment variables (non-Gallium)
----------------------------------------------------

``INTEL_NO_HW``
   if set to 1, prevents batches from being submitted to the hardware.
   This is useful for debugging hangs, etc.
``INTEL_DEBUG``
   a comma-separated list of named flags, which do various things:

   ``ann``
      annotate IR in assembly dumps
   ``aub``
      dump batches into an AUB trace for use with simulation tools
   ``bat``
      emit batch information
   ``blit``
      emit messages about blit operations
   ``blorp``
      emit messages about the blorp operations (blits & clears)
   ``buf``
      emit messages about buffer objects
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
   ``dri``
      emit messages about the DRI interface
   ``fbo``
      emit messages about framebuffers
   ``fs``
      dump shader assembly for fragment shaders
   ``gs``
      dump shader assembly for geometry shaders
   ``hex``
      print instruction hex dump with the disassembly
   ``l3``
      emit messages about the new L3 state during transitions
   ``miptree``
      emit messages about miptrees
   ``no8``
      don't generate SIMD8 fragment shader
   ``no16``
      suppress generation of 16-wide fragment shaders. useful for
      debugging broken shaders
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
   ``perf``
      emit messages about performance issues
   ``perfmon``
      emit messages about ``AMD_performance_monitor``
   ``pix``
      emit messages about pixel operations
   ``prim``
      emit messages about drawing primitives
   ``reemit``
      mark all state dirty on each draw call
   ``sf``
      emit messages about the strips & fans unit (for old gens, includes
      the SF program)
   ``shader_time``
      record how much GPU time is spent in each shader
   ``spill_fs``
      force spilling of all registers in the scalar backend (useful to
      debug spilling code)
   ``spill_vec4``
      force spilling of all registers in the vec4 backend (useful to
      debug spilling code)
   ``state``
      emit messages about state flag tracking
   ``submit``
      emit batchbuffer usage statistics
   ``sync``
      after sending each batch, emit a message and wait for that batch
      to finish rendering
   ``tcs``
      dump shader assembly for tessellation control shaders
   ``tes``
      dump shader assembly for tessellation evaluation shaders
   ``tex``
      emit messages about textures.
   ``urb``
      emit messages about URB setup
   ``vert``
      emit messages about vertex assembly
   ``vs``
      dump shader assembly for vertex shaders

``INTEL_SCALAR_VS`` (or ``TCS``, ``TES``, ``GS``)
   force scalar/vec4 mode for a shader stage (Gen8-9 only)
``INTEL_PRECISE_TRIG``
   if set to 1, true or yes, then the driver prefers accuracy over
   performance in trig functions.

Radeon driver environment variables (radeon, r200, and r300g)
-------------------------------------------------------------

``RADEON_NO_TCL``
   if set, disable hardware-accelerated Transform/Clip/Lighting.

EGL environment variables
-------------------------

Mesa EGL supports different sets of environment variables. See the
:doc:`Mesa EGL <egl>` page for the details.

Gallium environment variables
-----------------------------

``GALLIUM_HUD``
   draws various information on the screen, like framerate, cpu load,
   driver statistics, performance counters, etc. Set
   ``GALLIUM_HUD=help`` and run e.g. ``glxgears`` for more info.
``GALLIUM_HUD_PERIOD``
   sets the hud update rate in seconds (float). Use zero to update every
   frame. The default period is 1/2 second.
``GALLIUM_HUD_VISIBLE``
   control default visibility, defaults to true.
``GALLIUM_HUD_TOGGLE_SIGNAL``
   toggle visibility via user specified signal. Especially useful to
   toggle hud at specific points of application and disable for
   unencumbered viewing the rest of the time. For example, set
   ``GALLIUM_HUD_VISIBLE`` to ``false`` and
   ``GALLIUM_HUD_TOGGLE_SIGNAL`` to ``10`` (``SIGUSR1``). Use
   ``kill -10 <pid>`` to toggle the hud as desired.
``GALLIUM_HUD_SCALE``
   Scale hud by an integer factor, for high DPI displays. Default is 1.
``GALLIUM_HUD_DUMP_DIR``
   specifies a directory for writing the displayed hud values into
   files.
``GALLIUM_DRIVER``
   useful in combination with ``LIBGL_ALWAYS_SOFTWARE=true`` for
   choosing one of the software renderers ``softpipe``, ``llvmpipe`` or
   ``swr``.
``GALLIUM_LOG_FILE``
   specifies a file for logging all errors, warnings, etc. rather than
   stderr.
``GALLIUM_PRINT_OPTIONS``
   if non-zero, print all the Gallium environment variables which are
   used, and their current values.
``GALLIUM_DUMP_CPU``
   if non-zero, print information about the CPU on start-up
``TGSI_PRINT_SANITY``
   if set, do extra sanity checking on TGSI shaders and print any errors
   to stderr.
``DRAW_FSE``
   ???
``DRAW_NO_FSE``
   ???
``DRAW_USE_LLVM``
   if set to zero, the draw module will not use LLVM to execute shaders,
   vertex fetch, etc.
``ST_DEBUG``
   controls debug output from the Mesa/Gallium state tracker. Setting to
   ``tgsi``, for example, will print all the TGSI shaders. See
   ``src/mesa/state_tracker/st_debug.c`` for other options.

Clover environment variables
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``CLOVER_EXTRA_BUILD_OPTIONS``
   allows specifying additional compiler and linker options. Specified
   options are appended after the options set by the OpenCL program in
   ``clBuildProgram``.
``CLOVER_EXTRA_COMPILE_OPTIONS``
   allows specifying additional compiler options. Specified options are
   appended after the options set by the OpenCL program in
   ``clCompileProgram``.
``CLOVER_EXTRA_LINK_OPTIONS``
   allows specifying additional linker options. Specified options are
   appended after the options set by the OpenCL program in
   ``clLinkProgram``.

Softpipe driver environment variables
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``SOFTPIPE_DUMP_FS``
   if set, the softpipe driver will print fragment shaders to stderr
``SOFTPIPE_DUMP_GS``
   if set, the softpipe driver will print geometry shaders to stderr
``SOFTPIPE_NO_RAST``
   if set, rasterization is no-op'd. For profiling purposes.
``SOFTPIPE_USE_LLVM``
   if set, the softpipe driver will try to use LLVM JIT for vertex
   shading processing.

LLVMpipe driver environment variables
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``LP_NO_RAST``
   if set LLVMpipe will no-op rasterization
``LP_DEBUG``
   a comma-separated list of debug options is accepted. See the source
   code for details.
``LP_PERF``
   a comma-separated list of options to selectively no-op various parts
   of the driver. See the source code for details.
``LP_NUM_THREADS``
   an integer indicating how many threads to use for rendering. Zero
   turns off threading completely. The default value is the number of
   CPU cores present.

VMware SVGA driver environment variables
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``SVGA_FORCE_SWTNL``
   force use of software vertex transformation
``SVGA_NO_SWTNL``
   don't allow software vertex transformation fallbacks (will often
   result in incorrect rendering).
``SVGA_DEBUG``
   for dumping shaders, constant buffers, etc. See the code for details.
``SVGA_EXTRA_LOGGING``
   if set, enables extra logging to the ``vmware.log`` file, such as the
   OpenGL program's name and command line arguments.
``SVGA_NO_LOGGING``
   if set, disables logging to the ``vmware.log`` file. This is useful
   when using Valgrind because it otherwise crashes when initializing
   the host log feature.

See the driver code for other, lesser-used variables.

WGL environment variables
~~~~~~~~~~~~~~~~~~~~~~~~~

``WGL_SWAP_INTERVAL``
   to set a swap interval, equivalent to calling
   ``wglSwapIntervalEXT()`` in an application. If this environment
   variable is set, application calls to ``wglSwapIntervalEXT()`` will
   have no effect.

VA-API environment variables
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``VAAPI_MPEG4_ENABLED``
   enable MPEG4 for VA-API, disabled by default.

VC4 driver environment variables
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``VC4_DEBUG``
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
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``RADV_DEBUG``
   a comma-separated list of named flags, which do various things:

   ``llvm``
      enable LLVM compiler backend
   ``allbos``
      force all allocated buffers to be referenced in submissions
   ``allentrypoints``
      enable all device/instance entrypoints
   ``checkir``
      validate the LLVM IR before LLVM compiles the shader
   ``errors``
      display more info about errors
   ``info``
      show GPU-related information
   ``metashaders``
      dump internal meta shaders
   ``nobinning``
      disable primitive binning
   ``nocache``
      disable shaders cache
   ``nocompute``
      disable compute queue
   ``nodcc``
      disable Delta Color Compression (DCC) on images
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
   ``nooutoforder``
      disable out-of-order rasterization
   ``nothreadllvm``
      disable LLVM threaded compilation
   ``preoptir``
      dump LLVM IR before any optimizations
   ``shaders``
      dump shaders
   ``shaderstats``
      dump shader statistics
   ``spirv``
      dump SPIR-V
   ``startup``
      display info at startup
   ``syncshaders``
      synchronize shaders after all draws/dispatches
   ``vmfaults``
      check for VM memory faults via dmesg
   ``zerovram``
      initialize all memory allocated in VRAM as zero

``RADV_FORCE_FAMILY``
   create a null device to compile shaders without a AMD GPU (eg.
   gfx900)
``RADV_PERFTEST``
   a comma-separated list of named flags, which do various things:

   ``bolist``
      enable the global BO list
   ``cswave32``
      enable wave32 for compute shaders (GFX10+)
   ``dccmsaa``
      enable DCC for MSAA images
   ``dfsm``
      enable dfsm
   ``gewave32``
      enable wave32 for vertex/tess/geometry shaders (GFX10+)
   ``localbos``
      enable local BOs
   ``pswave32``
      enable wave32 for pixel shaders (GFX10+)
   ``tccompatcmask``
      enable TC-compat cmask for MSAA images

``RADV_SECURE_COMPILE_THREADS``
   maximum number of secure compile threads (up to 32)
``RADV_TEX_ANISO``
   force anisotropy filter (up to 16)
``RADV_TRACE_FILE``
   generate cmdbuffer tracefiles when a GPU hang is detected
``ACO_DEBUG``
   a comma-separated list of named flags, which do various things:

   ``validateir``
      validate the ACO IR at various points of compilation (enabled by
      default for debug/debugoptimized builds)
   ``validatera``
      validate register assignment of ACO IR and catches many RA bugs
   ``perfwarn``
      abort on some suboptimal code generation

radeonsi driver environment variables
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``AMD_DEBUG``
   a comma-separated list of named flags, which do various things:
``nodma``
   Disable SDMA
``nodmaclear``
   Disable SDMA clears
``nodmacopyimage``
   Disable SDMA image copies
``zerovram``
   Clear VRAM allocations.
``nodcc``
   Disable DCC.
``nodccclear``
   Disable DCC fast clear.
``nodccfb``
   Disable separate DCC on the main framebuffer
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
``norbplus``
   Disable RB+.
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
``forcedma``
   Use SDMA for all operations when possible.
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
``alwayspd``
   Always enable the primitive discard compute shader.
``pd``
   Enable the primitive discard compute shader for large draw calls.
``nopd``
   Disable the primitive discard compute shader.
``switch_on_eop``
   Program WD/IA to switch on end-of-packet.
``nooutoforder``
   Disable out-of-order rasterization
``dpbb``
   Enable DPBB.
``dfsm``
   Enable DFSM.

Other Gallium drivers have their own environment variables. These may
change frequently so the source code should be consulted for details.
