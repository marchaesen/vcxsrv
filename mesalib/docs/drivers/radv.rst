RADV
====

RADV is a Vulkan driver for AMD GCN/RDNA GPUs.

Introduction
------------

RADV is a userspace driver that implements the Vulkan API on most modern AMD GPUs.

Many Linux distributions include RADV in their default installation as part of their Mesa packages.
It is also the Vulkan driver on the Steam Deck, the handheld console developed by Valve.

Features
~~~~~~~~

The easiest way to track the feature set of RADV (and other Vulkan drivers in Mesa) is to
take a look at the
`Mesa matrix <https://mesamatrix.net/#Vulkan>`__.

Supported hardware
~~~~~~~~~~~~~~~~~~

All GCN and RDNA GPUs that are supported by the Linux kernel (and capable of graphics)
are also supported by RADV, starting from GCN 1.
We are always working on supporting the very latest GPUs too.

Vulkan API support:

* GFX6-7 (GCN 1-2): Vulkan 1.3
* GFX8 and newer (GCN 3-5 and RDNA): Vulkan 1.4

`The exact list of Vulkan conformant products can be seen here. <https://www.khronos.org/conformance/adopters/conformant-products>`__

Each GPU chip can contain various hardware blocks (also known as IP blocks), and each of those are separately versioned.
We usually refer to hardware generations by the main version number of the GFX (graphics) hardware block.

Each hardware generation has different chips which usually only have minor differences between each other,
such as number of compute units and different minor versions of some IP blocks.
We refer to each chip by the code name of its first release, and we don't differentiate between
refreshes of the same chip, because they are functionally exactly the same.

For more information about which GPU chip name corresponds to which GPU product,
`see the src/amd/common/amd_family.h file <https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/amd/common/amd_family.h>`__.

Note that for GFX6-7 (GCN 1-2) GPUs, the ``amdgpu`` kernel driver is currently not the default in Linux
(by default the old ``radeon`` KMD is used for these old GPUs, which is not supported by RADV),
so users need to manually enable ``amdgpu`` by adding the following to the kernel command line:
``radeon.si_support=0 radeon.cik_support=0 amdgpu.si_support=1 amdgpu.cik_support=1``

Basics
~~~~~~

The RADV source code is located in ``src/amd/vulkan``.

RADV is a userspace driver, compiled to a shared library file.
On Linux, typically ``libvulkan_radeon.so`` (equivalent to a ``.dll`` on Windows).

When you start a Vulkan application, the Vulkan loader (in userspace)
will find a set of Vulkan drivers (also known as Vulkan implementations),
all of which are technically shared libraries.
If you are running on a system with a supported AMD GPU and have RADV installed,
the loader will find ``libvulkan_radeon.so`` and load that.
The Vulkan application can then choose which available Vulkan implementation to use.

With RADV, when the application makes Vulkan API calls (aka. entry points),
the ``vk*`` functions will end up calling ``radv_*`` functions.
For example, ``vkCmdDrawMeshTasksEXT`` will actually call ``radv_CmdDrawMeshTasksEXT``.

Responsibilities of RADV vs. the kernel driver
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Due to the complexity of how modern GPUs work, the graphics stack is split
between kernel-mode drivers (KMD) and user-mode drivers (UMD).
All Graphics APIs such as Vulkan, OpenGL, etc. are implemented in userspace.

RADV is a UMD that currently works with the ``amdgpu`` KMD in the Linux kernel.
Interacting with the KMD is done by RADV's winsys code.

The KMD is responsible for:

* Talking to the GPU through a PCIe port and power management (such as choosing voltage, frequency, sleep modes etc.)
* Display functionality
* Video memory management (and GTT)
* Writing submitted commands to the GPU's ring buffers

The UMD (in our case, RADV) is responsible for:

* Recording commands in a binary format that the GPU can understand
* Programming GPU registers for correct functionality
* Compiling shaders to the GPU's native ISA (instruction set architecture)
  and uploading them to memory accessible by the GPU
* Submitting recorded commands to the kernel through a system call

To communicate with ``amdgpu``, RADV relies on the DRM userspace API (uAPI) of the Linux kernel,
which is a set of system calls.
RADV depends on ``libdrm`` for some functionality and for others it uses the system calls directly.

Command submission and PM4 packets
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Vulkan applications record a series of commands in command buffers and
later submit these command buffers to one of the queues in the GPU.

Command buffer recording in RADV is implemented by emitting packets in a buffer,
which is called a command stream (CS).

Command packets are more or less analogous to Vulkan API calls,
which means that each Vulkan command (such as draw or dispatch)
corresponds to one or more packets.
However, depending on how closely the hardware follows Vulkan spec
some commands will require a more complex implementation.
For example, a simple ``vkCmdDraw`` call will result in just one packet,
however emitting draw state (before the draw) may take dozens of packets.

For the graphics queue (GFX) and async compute queue (ACE), the PM4 packet
format is used; other queues such as SDMA and the various video queues
have their own format. Commands typically vary between different GPU generations.

When submitting to a queue, several CSs are submitted to the kernel at once.
The kernel terminology calls these indirect buffers (IB) because the kernel
typically uses the ``INDIRECT_BUFFER`` PM4 command to execute them.
Modern AMD GPUs have several queues, which more or less map to the Vulkan
queue types, though sometimes we need to submit to more than one HW queue
at the same time.

After command submission, the packets in the IBs will be executed
by the command processor (CP) of the queue that the buffer was submitted to.
The exact capabilities and supported commands of the CP depend on HW generation and queue type.

Each CP has a collection of registers that control how the CP behaves.
Registers in the CP are not to be confused with registers in shaders.
For example, the addresses of shader programs, some details of shader execution,
draw call information etc. have a corresponding register.
These registers can be typically set by PM4 packets.

Shader compilation
~~~~~~~~~~~~~~~~~~

One of the main responsibilities of RADV is compiling shaders for Vulkan applications.

RADV relies on the Mesa shader compiler stack.
Here is a rough overview of how it works:

* Vulkan applications pass shaders to RADV in the SPIR-V format
* RADV calls ``spirv_to_nir`` which translates the shader into the NIR intermediate representation
* We perform various optimizations and lowerings on the NIR shader
* The shader is linked with the other shaders it is compiled with (if any)
* Still using NIR, the shader is further lowered to be closer to the hardware
* Finally, we pass the lowered NIR shader to ACO, our compiler backend, which compiles it into GPU specific ISA

ACO is the default shader-compiler used in RADV.
`Read its documentation here. <https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/amd/compiler/README.md>`__

We still maintain an LLVM based compiler backend too,
which is these days solely used for testing and hardware bringup.
Users are recommended **NOT** to use the LLVM backend.

Shader execution
~~~~~~~~~~~~~~~~

Some commands (such as draws and dispatches) ask the CP to launch shaders.
Shader launch is handled by the firmware, based on registers that control shader programs.
Additionally, draw commands will also automatically use the appropriate fixed function
units in the hardware.

Shaders are executed by a so-called compute unit on the GPU, which is a SIMD machine.
A shader invocation is a single SIMD lane (AMD also calls it thread, not to be confused by a CPU HW thread),
and a subgroup is all 64 or 32 SIMD lanes together (also known as a wave).
Each wave is a separate running instance of a shader program,
but multiple waves can be grouped together into workgroups.

Registers in shaders (not to be confused with registers in the CP):

* VGPR - vector general purpose register: each SIMD lane has a different value for this register
* SGPR - scalar general purpose register: same value within a wave

For further reading, AMD has publised whitepapers and documentation for the GCN and RDNA GPU architectures.
`These can be found on their GPUOpen site. <https://gpuopen.com/amd-gpu-architecture-programming-documentation/>`__

Debugging
---------

For a list of environment variables to debug RADV, please see
:ref:`radv env-vars` for a list.

Instructions for debugging GPU hangs can be found :ref:`here <radv-debug-hang>`.

Hardware Documentation
----------------------

You can find a list of documentation for the various generations of
AMD hardware on the `X.Org wiki
<https://www.x.org/wiki/RadeonFeature/#documentation>`__.

Additional community-written documentation is also available in Mesa:

.. toctree::
   :glob:

   amd/hw/*
