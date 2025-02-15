/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_PRECOMP_CACHE_H
#define PANVK_PRECOMP_CACHE_H

#include "panvk_device.h"
#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "genxml/gen_macros.h"
#include "util/simple_mtx.h"
#include "libpan_dgc.h"
#include "libpan_shaders.h"
#include "pan_shader.h"
#include "panvk_macros.h"
#include "panvk_mempool.h"
#include "panvk_shader.h"

struct panvk_device;

struct panvk_precomp_cache {
   simple_mtx_t lock;
   struct panvk_device *dev;

   /* Precompiled binary table */
   const uint32_t **programs;

   struct panvk_shader *precomp[LIBPAN_SHADERS_NUM_PROGRAMS];
};

struct panvk_precomp_cache *
   panvk_per_arch(precomp_cache_init)(struct panvk_device *dev);
void panvk_per_arch(precomp_cache_cleanup)(struct panvk_precomp_cache *cache);

struct panvk_shader *
   panvk_per_arch(precomp_cache_get)(struct panvk_precomp_cache *cache,
                                     unsigned program);

#endif
