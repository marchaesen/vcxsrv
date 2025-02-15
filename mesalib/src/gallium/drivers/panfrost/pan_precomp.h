/*
 * Copyright © 2024 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */
#ifndef PAN_PRECOMP_H
#define PAN_PRECOMP_H

#include "pan_screen.h"
#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "genxml/gen_macros.h"
#include "util/simple_mtx.h"
#include "libpan_dgc.h"
#include "libpan_shaders.h"
#include "pan_job.h"
#include "pan_pool.h"

struct panfrost_precomp_shader {
   struct pan_shader_info info;
   struct pan_compute_dim local_size;
   uint64_t code_ptr;
   uint64_t state_ptr;
};

struct panfrost_screen;

struct panfrost_precomp_cache {
   simple_mtx_t lock;

   /* Shader binary pool. */
   struct pan_pool *bin_pool;

   /* Shader desc pool for any descriptor that can be re-used across
    * indirect dispatch calls. Job descriptors are allocated from the batch pool.
    */
   struct pan_pool *desc_pool;

   /* Precompiled binary table */
   const uint32_t **programs;

   struct panfrost_precomp_shader *precomp[LIBPAN_SHADERS_NUM_PROGRAMS];
};

struct panfrost_precomp_cache *
   GENX(panfrost_precomp_cache_init)(struct panfrost_screen *screen);
void GENX(panfrost_precomp_cache_cleanup)(struct panfrost_precomp_cache *cache);

void GENX(panfrost_launch_precomp)(struct panfrost_batch *batch,
                                   struct panlib_precomp_grid grid,
                                   enum panlib_barrier barrier,
                                   enum libpan_shaders_program idx, void *data,
                                   size_t data_size);

#define MESA_DISPATCH_PRECOMP GENX(panfrost_launch_precomp)

#endif
