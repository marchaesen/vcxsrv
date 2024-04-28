:orphan:

.. _radv-debug-hang:

Debugging GPU hangs with RADV
=============================

UMR (optional)
--------------

UMR is needed for dumping a lot of useful information. Clone, build and install
`UMR <https://gitlab.freedesktop.org/tomstdenis/umr>`__. Do not forget to run
``chmod +s $(which umr)`` so RADV can actually access UMR.

UMR needs to access some kernel debug interfaces:

.. code-block:: sh

   chmod 777 /sys/kernel/debug
   chmod -R 777 /sys/kernel/debug/dri

Secure boot has to be disabled as well.

Generating and analyzing hang reports
-------------------------------------

With UMR installed, you can now set ``RADV_DEBUG=hang`` which makes RADV insert
trace markers and synchronization and check for hangs. The hang report will be
saved to ``~/radv_dumps_<pid>_<time>``. Inside the directory of the hang report,
there are a couple of files:

* ``*.spv``: SPIR-V binaries of the pipeline that was bound when the hang occured.
* ``app_info.log``: ``VkApplicationInfo`` fields.
* ``bo_history.log``: A list of every GPU memory allocation and deallocation.
  If the GPU hang was caused by a page fault, you can use
  `radv_check_va.py <https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/amd/vulkan/radv_check_va.py>`__
  to figure out if address is invalid or used after the memory was deallocated.
* ``bo_ranges.log``: Address ranges that were valid at the time of submission.
* ``dmesg.log``: Output of ``dmesg``, if available.
* ``gpu_info.log``: Fields of ``radeon_info``.
* ``pipeline.log``: IR of the shaders that were bound during the hang as well as
  programm counters of waves executing said shaders and bound descriptors.
* ``registers.log``: Various GPU state registers.
* ``trace.log``: An annotated list of the command stream that caused the hang.
  the commands that hung come after
  ``!!!!! This is the last trace point that was reached by the CP !!!!!``.
* ``umr_ring.log``: Similar to ``trace.log``.
* ``umr_waves.log``: A list of waves that were active at the time of the hang,
  including register values.
* ``vm_fault.log``: The page fault address if a page fault occured.

Debugging Steam games
---------------------

Steam games require a bit more work so RADV can access UMR: In your Steam library,
make sure **Tools** is checked and search for **Steam Linux Runtime**.
Under **Properties** -> **Installed Files**, click **Browse**, open
``_v2-entry-point`` and add

.. code-block:: sh

   shift 2
   exec "$@"

at the top of the file. Hang debugging can be enabled by selecting the faulting
game and adding ``RADV_DEBUG=hang %command%`` under **Properties** -> **General**
-> **LAUNCH OPTIONS**.

Debugging hangs without RADV_DEBUG=hang
---------------------------------------

In some situations, ``RADV_DEBUG=hang`` wouldn't be able to generate a GPU hang
report, like for synchronization issues (because it enables
``RADV_DEBUG=syncshaders`` behind the scene). An alternative solution is to
disable GPU recovery by adding ``amdgpu.gpu_recovery=0`` to your kernel command
line options. And then invoke UMR manually with
``umr --by-pci <pci_id> -O bits,halt_waves -go 0 -wa <ring> -go 1 2>&1`` for
dumping the waves and ``umr --by-pci <pci_id> -RS <ring> 2>&1`` for dumping the
rings once the GPU hang occured.
