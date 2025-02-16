/*
 * Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef LIBPAN_DGC_H
#define LIBPAN_DGC_H
#include "libpan.h"

enum panlib_barrier {
   PANLIB_BARRIER_NONE = 0,
   PANLIB_BARRIER_JM_BARRIER = (1 << 0),
   PANLIB_BARRIER_JM_SUPPRESS_PREFETCH = (1 << 1),
};

struct panlib_precomp_grid {
   uint32_t count[3];
};

static struct panlib_precomp_grid
panlib_3d(uint32_t x, uint32_t y, uint32_t z)
{
   return (struct panlib_precomp_grid){.count = {x, y, z}};
}

static struct panlib_precomp_grid
panlib_1d(uint32_t x)
{
   return panlib_3d(x, 1, 1);
}

#endif