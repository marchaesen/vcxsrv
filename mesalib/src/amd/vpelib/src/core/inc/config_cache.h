/* Copyright 2024 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */
#pragma once

#include <stdint.h>
#include <string.h>
#include "config_writer.h"

/** To use this config caching helper, there are pre-requisites:
 * The object that passes to the hw programming layer must have the following members in its
 * structure
 * 1. struct config_cache config_cache;
 * 2. bool   dirty;
 *
 * e.g.
 * struct transfer_function {
 *     bool   dirty;
 *     struct config_cache config_cache;
 * };
 *
 * The upper layer has to indicate this object is dirty or not for the hw programming layer to
 * determine i.  re-use the config cache? ii. cache the new settings?
 *
 * Before using the CONFIG_CACHE(), make sure the function has these local variables visible in the
 * same code block:
 * 1. struct config_writer *config_writer
 *    - usually been declared with PROGRAM_ENTRY()
 * 2. a debug option that want to disable caching or not
 * 3. an input object that has the config_cache member
 * 4. the hw programming function that would generate command buffer content
 * 5. the input/output context that has configs vector which stores the generated configs
 *
 * Inside this CONFIG_CACHE macro it will clear the dirty bit after consuming the settings
 *
 * Make sure to free up this cache object when the parent object is destroyed using
 * CONFIG_CACHE_FREE()
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

struct vpe_priv;
struct vpe_vector;

/* a common config cache structure to be included in the object that is for program hardware API
 * layer
 */
struct config_cache {
    uint8_t *p_buffer;
    uint64_t size;
    bool     cached;
};

/* A macro that helps cache the config packet, it won't cache if it is in bypass mode
 * as bypass mode is not heavy lifting programming.
 *
 * /param   obj_cache           an object that has the config cache member
 * /param   ctx                 an input/output context that contains the configs vector
 * /param   disable_cache       a flag that controls a caching is needed
 * /param   is_bypass           if it is in bypass, it doesn't cache the bypass config
 * /param   program_func_call   the program call that generate config packet content
 * /param   inst                index to address the config_cache array
 */
#define CONFIG_CACHE(obj_cache, ctx, disable_cache, is_bypass, program_func_call, inst)            \
    {                                                                                              \
        bool use_cache = false;                                                                    \
                                                                                                   \
        if ((obj_cache) && !disable_cache && (obj_cache)->config_cache[inst].p_buffer &&           \
            (obj_cache)->config_cache[inst].cached && !((obj_cache)->dirty[inst]) && !is_bypass) { \
            /* make sure it opens a new config packet */                                           \
            config_writer_force_new_with_type(config_writer, CONFIG_TYPE_DIRECT);                  \
                                                                                                   \
            /* reuse the cache */                                                                  \
            if (config_writer->buf->size >= (obj_cache)->config_cache[inst].size) {                \
                memcpy((void *)(uintptr_t)config_writer->base_cpu_va,                              \
                    (obj_cache)->config_cache[inst].p_buffer,                                      \
                    (size_t)(obj_cache)->config_cache[inst].size);                                 \
                config_writer->buf->cpu_va =                                                       \
                    config_writer->base_cpu_va + (obj_cache)->config_cache[inst].size;             \
                config_writer->buf->gpu_va =                                                       \
                    config_writer->base_gpu_va + (obj_cache)->config_cache[inst].size;             \
                config_writer->buf->size -=                                                        \
                    ((obj_cache)->config_cache[inst].size - sizeof(uint32_t));                     \
                use_cache = true;                                                                  \
            }                                                                                      \
        }                                                                                          \
                                                                                                   \
        if (!use_cache) {                                                                          \
            uint64_t start, end;                                                                   \
            uint16_t num_config = (uint16_t)(ctx)->configs[inst]->num_elements;                    \
                                                                                                   \
            if (!is_bypass) {                                                                      \
                /* make sure it opens a new config packet so we can cache a complete new config */ \
                /* for bypass we don't do caching, so no need to open a new desc */                \
                config_writer_force_new_with_type(config_writer, CONFIG_TYPE_DIRECT);              \
            }                                                                                      \
                                                                                                   \
            start = config_writer->base_cpu_va;                                                    \
            program_func_call;                                                                     \
            end = config_writer->buf->cpu_va;                                                      \
                                                                                                   \
            if (!disable_cache && !is_bypass) {                                                    \
                /* only cache when it is not crossing config packets */                            \
                if (num_config == (ctx)->configs[inst]->num_elements) {                            \
                    if ((obj_cache)->dirty[inst]) {                                                \
                        uint64_t size = end - start;                                               \
                                                                                                   \
                        if ((obj_cache)->config_cache[inst].size < size) {                         \
                            if ((obj_cache)->config_cache[inst].p_buffer)                          \
                                vpe_free((obj_cache)->config_cache[inst].p_buffer);                \
                                                                                                   \
                            (obj_cache)->config_cache[inst].p_buffer = vpe_zalloc((size_t)size);   \
                            if ((obj_cache)->config_cache[inst].p_buffer) {                        \
                                memcpy((obj_cache)->config_cache[inst].p_buffer,                   \
                                    (void *)(uintptr_t)start, (size_t)size);                       \
                                (obj_cache)->config_cache[inst].size   = size;                     \
                                (obj_cache)->config_cache[inst].cached = true;                     \
                            } else {                                                               \
                                (obj_cache)->config_cache[inst].size = 0;                          \
                            }                                                                      \
                        }                                                                          \
                    }                                                                              \
                }                                                                                  \
            }                                                                                      \
        }                                                                                          \
        if ((obj_cache))                                                                           \
            (obj_cache)->dirty[inst] = false;                                                      \
    }

/* the following macro requires a local variable vpr_priv to be present */
#define CONFIG_CACHE_FREE(cache)                                                                   \
    {                                                                                              \
        if (cache.p_buffer)                                                                        \
            vpe_free(cache.p_buffer);                                                              \
    }

#ifdef __cplusplus
}
#endif
