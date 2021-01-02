# Copyright (C) 2014-2018 Intel Corporation.   All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
import sys

# Python source
KNOBS = [

    ['ENABLE_ASSERT_DIALOGS', {
        'type'      : 'bool',
        'default'   : 'true',
        'desc'      : ['Use dialogs when asserts fire.',
                       'Asserts are only enabled in debug builds'],
        'category'  : 'debug',
    }],

    ['SINGLE_THREADED', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['If enabled will perform all rendering on the API thread.',
                       'This is useful mainly for debugging purposes.'],
        'category'  : 'debug',
    }],

    ['DUMP_SHADER_IR', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Dumps shader LLVM IR at various stages of jit compilation.'],
        'category'  : 'debug',
    }],

    ['USE_GENERIC_STORETILE', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Always use generic function for performing StoreTile.',
                       'Will be slightly slower than using optimized (jitted) path'],
        'category'  : 'debug_adv',
    }],

    ['FAST_CLEAR', {
        'type'      : 'bool',
        'default'   : 'true',
        'desc'      : ['Replace 3D primitive execute with a SWRClearRT operation and',
                       'defer clear execution to first backend op on hottile, or hottile store'],
        'category'  : 'perf_adv',
    }],

    ['MAX_NUMA_NODES', {
        'type'      : 'uint32_t',
        'default'   : '1' if sys.platform == 'win32' else '0',
        'desc'      : ['Maximum # of NUMA-nodes per system used for worker threads',
                       '  0 == ALL NUMA-nodes in the system',
                       '  N == Use at most N NUMA-nodes for rendering'],
        'category'  : 'perf',
    }],

    ['MAX_CORES_PER_NUMA_NODE', {
        'type'      : 'uint32_t',
        'default'   : '0',
        'desc'      : ['Maximum # of cores per NUMA-node used for worker threads.',
                       '  0 == ALL non-API thread cores per NUMA-node',
                       '  N == Use at most N cores per NUMA-node'],
        'category'  : 'perf',
    }],

    ['MAX_THREADS_PER_CORE', {
        'type'      : 'uint32_t',
        'default'   : '1',
        'desc'      : ['Maximum # of (hyper)threads per physical core used for worker threads.',
                       '  0 == ALL hyper-threads per core',
                       '  N == Use at most N hyper-threads per physical core'],
        'category'  : 'perf',
    }],

    ['MAX_WORKER_THREADS', {
        'type'      : 'uint32_t',
        'default'   : '0',
        'desc'      : ['Maximum worker threads to spawn.',
                       '',
                       'IMPORTANT: If this is non-zero, no worker threads will be bound to',
                       'specific HW threads.  They will all be "floating" SW threads.',
                       'In this case, the above 3 KNOBS will be ignored.'],
        'category'  : 'perf',
    }],

    ['BASE_NUMA_NODE', {
        'type'      : 'uint32_t',
        'default'   : '0',
        'desc'      : ['Starting NUMA node index to use when allocating compute resources.',
                       'Setting this to a non-zero value will reduce the maximum # of NUMA nodes used.'],
        'category'  : 'perf',
    }],

    ['BASE_CORE', {
        'type'      : 'uint32_t',
        'default'   : '0',
        'desc'      : ['Starting core index to use when allocating compute resources.',
                       'Setting this to a non-zero value will reduce the maximum # of cores used.'],
        'category'  : 'perf',
    }],

    ['BASE_THREAD', {
        'type'      : 'uint32_t',
        'default'   : '0',
        'desc'      : ['Starting thread index to use when allocating compute resources.',
                       'Setting this to a non-zero value will reduce the maximum # of threads used.'],
        'category'  : 'perf',
    }],

    ['BUCKETS_START_FRAME', {
        'type'      : 'uint32_t',
        'default'   : '1200',
        'desc'      : ['Frame from when to start saving buckets data.',
                       '',
                       'NOTE: KNOB_ENABLE_RDTSC must be enabled in core/knobs.h',
                       'for this to have an effect.'],
        'category'  : 'perf_adv',
    }],

    ['BUCKETS_END_FRAME', {
        'type'      : 'uint32_t',
        'default'   : '1400',
        'desc'      : ['Frame at which to stop saving buckets data.',
                       '',
                       'NOTE: KNOB_ENABLE_RDTSC must be enabled in core/knobs.h',
                       'for this to have an effect.'],
        'category'  : 'perf_adv',
    }],

    ['WORKER_SPIN_LOOP_COUNT', {
        'type'      : 'uint32_t',
        'default'   : '5000',
        'desc'      : ['Number of spin-loop iterations worker threads will perform',
                       'before going to sleep when waiting for work'],
        'category'  : 'perf_adv',
    }],

    ['MAX_DRAWS_IN_FLIGHT', {
        'type'      : 'uint32_t',
        'default'   : '256',
        'desc'      : ['Maximum number of draws outstanding before API thread blocks.',
                       'This value MUST be evenly divisible into 2^32'],
        'category'  : 'perf_adv',
    }],

    ['MAX_PRIMS_PER_DRAW', {
        'type'      : 'uint32_t',
        'default'   : '49152',
        'desc'      : ['Maximum primitives in a single Draw().',
                       'Larger primitives are split into smaller Draw calls.',
                       'Should be a multiple of (3 * vectorWidth).'],
        'category'  : 'perf_adv',
    }],

    ['MAX_TESS_PRIMS_PER_DRAW', {
        'type'      : 'uint32_t',
        'default'   : '16',
        'desc'      : ['Maximum primitives in a single Draw() with tessellation enabled.',
                       'Larger primitives are split into smaller Draw calls.',
                       'Should be a multiple of (vectorWidth).'],
        'category'  : 'perf_adv',
    }],


    ['DEBUG_OUTPUT_DIR', {
        'type'      : 'std::string',
        'default'   : r'%TEMP%\Rast\DebugOutput' if sys.platform == 'win32' else '/tmp/Rast/DebugOutput',
        'desc'      : ['Output directory for debug data.'],
        'category'  : 'debug',
    }],

    ['JIT_ENABLE_CACHE', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Enables caching of compiled shaders'],
        'category'  : 'debug_adv',
    }],

    ['JIT_OPTIMIZATION_LEVEL', {
        'type'      : 'int',
        'default'   : '-1',
        'desc'      : ['JIT compile optimization level:',],
        'category'  : 'debug',
        'control'   : 'dropdown',
        'choices' : [
            {
                'name'  : 'Automatic',
                'desc'  : 'Automatic based on other KNOB and build settings',
                'value' : -1,
            },
            {
                'name'  : 'Debug',
                'desc'  : 'No optimization: -O0',
                'value' : 0,
            },
            {
                'name'  : 'Less',
                'desc'  : 'Some optimization: -O1',
                'value' : 1,
            },
            {
                'name'  : 'Optimize',
                'desc'  : 'Default Clang / LLVM optimizations: -O2',
                'value' : 2,
            },
            {
                'name'  : 'Aggressive',
                'desc'  : 'Maximum optimization: -O3',
                'value' : 3,
            },
        ],
    }],

    ['JIT_CACHE_DIR', {
        'type'      : 'std::string',
        'default'   : r'%TEMP%\SWR\JitCache' if sys.platform == 'win32' else '${HOME}/.swr/jitcache',
        'desc'      : ['Cache directory for compiled shaders.'],
        'category'  : 'debug',
    }],

    ['TOSS_DRAW', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Disable per-draw/dispatch execution'],
        'category'  : 'perf',
    }],

    ['TOSS_QUEUE_FE', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Stop per-draw execution at worker FE',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
        'category'  : 'perf_adv',
    }],

    ['TOSS_FETCH', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Stop per-draw execution at vertex fetch',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
        'category'  : 'perf_adv',
    }],

    ['TOSS_IA', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Stop per-draw execution at input assembler',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
        'category'  : 'perf_adv',
    }],

    ['TOSS_VS', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Stop per-draw execution at vertex shader',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
        'category'  : 'perf_adv',
    }],

    ['TOSS_SETUP_TRIS', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Stop per-draw execution at primitive setup',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
        'category'  : 'perf_adv',
    }],

    ['TOSS_BIN_TRIS', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Stop per-draw execution at primitive binning',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
        'category'  : 'perf_adv',
    }],

    ['TOSS_RS', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Stop per-draw execution at rasterizer',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
        'category'  : 'perf_adv',
    }],

    ['DISABLE_SPLIT_DRAW', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Don\'t split large draws into smaller draws.,',
                       'MAX_PRIMS_PER_DRAW and MAX_TESS_PRIMS_PER_DRAW can be used to control split size.',
                       '',
                       'Useful to disable split draws for gathering archrast stats.'],
        'category'  : 'perf_adv',
    }],

    ['AR_ENABLE_PIPELINE_STATS', {
        'type'      : 'bool',
        'default'   : 'true',
        'desc'      : ['Enable pipeline stats when using Archrast'],
        'category'  : 'archrast',
    }],

    ['AR_ENABLE_SHADER_STATS', {
        'type'      : 'bool',
        'default'   : 'true',
        'desc'      : ['Enable shader stats when using Archrast'],
        'category'  : 'archrast',
    }],

    ['AR_ENABLE_SWTAG_DATA', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Enable SWTag data when using Archrast'],
        'category'  : 'archrast',
    }],

    ['AR_ENABLE_SWR_EVENTS', {
        'type'      : 'bool',
        'default'   : 'true',
        'desc'      : ['Enable internal SWR events when using Archrast'],
        'category'  : 'archrast',
    }],

    ['AR_ENABLE_PIPELINE_EVENTS', {
        'type'      : 'bool',
        'default'   : 'true',
        'desc'      : ['Enable pipeline events when using Archrast'],
        'category'  : 'archrast',
    }],

    ['AR_ENABLE_SHADER_EVENTS', {
        'type'      : 'bool',
        'default'   : 'true',
        'desc'      : ['Enable shader events when using Archrast'],
        'category'  : 'archrast',
    }],

    ['AR_ENABLE_SWTAG_EVENTS', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Enable SWTag events when using Archrast'],
        'category'  : 'archrast',
    }],

    ['AR_ENABLE_MEMORY_EVENTS', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Enable memory events when using Archrast'],
        'category'  : 'archrast',
    }],

    ['AR_MEM_SET_BYTE_GRANULARITY', {
        'type'      : 'uint32_t',
        'default'   : '64',
        'desc'      : ['Granularity and alignment of tracking of memory accesses',
                       'ONLY ACTIVE UNDER ArchRast.'],
        'category'  : 'archrast',
    }],


    ]
