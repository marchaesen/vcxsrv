Knobs
=====

OpenSWR has a number of environment variables which control its
operation, in addition to the normal Mesa and gallium controls.

.. envvar:: KNOB_ENABLE_ASSERT_DIALOGS <bool> (true)

Use dialogs when asserts fire. Asserts are only enabled in debug builds

.. envvar:: KNOB_SINGLE_THREADED <bool> (false)

If enabled will perform all rendering on the API thread. This is useful mainly for debugging purposes.

.. envvar:: KNOB_DUMP_SHADER_IR <bool> (false)

Dumps shader LLVM IR at various stages of jit compilation.

.. envvar:: KNOB_USE_GENERIC_STORETILE <bool> (false)

Always use generic function for performing StoreTile. Will be slightly slower than using optimized (jitted) path

.. envvar:: KNOB_FAST_CLEAR <bool> (true)

Replace 3D primitive execute with a SWRClearRT operation and defer clear execution to first backend op on hottile, or hottile store

.. envvar:: KNOB_MAX_NUMA_NODES <uint32_t> (0)

Maximum # of NUMA-nodes per system used for worker threads   0 == ALL NUMA-nodes in the system   N == Use at most N NUMA-nodes for rendering

.. envvar:: KNOB_MAX_CORES_PER_NUMA_NODE <uint32_t> (0)

Maximum # of cores per NUMA-node used for worker threads.   0 == ALL non-API thread cores per NUMA-node   N == Use at most N cores per NUMA-node

.. envvar:: KNOB_MAX_THREADS_PER_CORE <uint32_t> (1)

Maximum # of (hyper)threads per physical core used for worker threads.   0 == ALL hyper-threads per core   N == Use at most N hyper-threads per physical core

.. envvar:: KNOB_MAX_WORKER_THREADS <uint32_t> (0)

Maximum worker threads to spawn.  IMPORTANT: If this is non-zero, no worker threads will be bound to specific HW threads.  They will all be "floating" SW threads. In this case, the above 3 KNOBS will be ignored.

.. envvar:: KNOB_BUCKETS_START_FRAME <uint32_t> (1200)

Frame from when to start saving buckets data.  NOTE: KNOB_ENABLE_RDTSC must be enabled in core/knobs.h for this to have an effect.

.. envvar:: KNOB_BUCKETS_END_FRAME <uint32_t> (1400)

Frame at which to stop saving buckets data.  NOTE: KNOB_ENABLE_RDTSC must be enabled in core/knobs.h for this to have an effect.

.. envvar:: KNOB_WORKER_SPIN_LOOP_COUNT <uint32_t> (5000)

Number of spin-loop iterations worker threads will perform before going to sleep when waiting for work

.. envvar:: KNOB_MAX_DRAWS_IN_FLIGHT <uint32_t> (160)

Maximum number of draws outstanding before API thread blocks.

.. envvar:: KNOB_MAX_PRIMS_PER_DRAW <uint32_t> (2040)

Maximum primitives in a single Draw(). Larger primitives are split into smaller Draw calls. Should be a multiple of (3 * vectorWidth).

.. envvar:: KNOB_MAX_TESS_PRIMS_PER_DRAW <uint32_t> (16)

Maximum primitives in a single Draw() with tessellation enabled. Larger primitives are split into smaller Draw calls. Should be a multiple of (vectorWidth).

.. envvar:: KNOB_MAX_FRAC_ODD_TESS_FACTOR <float> (63.0f)

(DEBUG) Maximum tessellation factor for fractional-odd partitioning.

.. envvar:: KNOB_MAX_FRAC_EVEN_TESS_FACTOR <float> (64.0f)

(DEBUG) Maximum tessellation factor for fractional-even partitioning.

.. envvar:: KNOB_MAX_INTEGER_TESS_FACTOR <uint32_t> (64)

(DEBUG) Maximum tessellation factor for integer partitioning.

.. envvar:: KNOB_BUCKETS_ENABLE_THREADVIZ <bool> (false)

Enable threadviz output.

.. envvar:: KNOB_TOSS_DRAW <bool> (false)

Disable per-draw/dispatch execution

.. envvar:: KNOB_TOSS_QUEUE_FE <bool> (false)

Stop per-draw execution at worker FE  NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h

.. envvar:: KNOB_TOSS_FETCH <bool> (false)

Stop per-draw execution at vertex fetch  NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h

.. envvar:: KNOB_TOSS_IA <bool> (false)

Stop per-draw execution at input assembler  NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h

.. envvar:: KNOB_TOSS_VS <bool> (false)

Stop per-draw execution at vertex shader  NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h

.. envvar:: KNOB_TOSS_SETUP_TRIS <bool> (false)

Stop per-draw execution at primitive setup  NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h

.. envvar:: KNOB_TOSS_BIN_TRIS <bool> (false)

Stop per-draw execution at primitive binning  NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h

.. envvar:: KNOB_TOSS_RS <bool> (false)

Stop per-draw execution at rasterizer  NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h

