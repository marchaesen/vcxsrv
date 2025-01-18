/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include "agx_pack.h"

#pragma once

struct libagx_decompress_images {
   struct agx_texture_packed compressed;
   struct agx_pbe_packed uncompressed;
};
static_assert(sizeof(struct libagx_decompress_images) == 48);
