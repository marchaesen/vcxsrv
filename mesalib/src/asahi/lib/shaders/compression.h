/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include "agx_pack.h"
#include "libagx.h"

#pragma once

struct libagx_decompress_push {
   struct agx_texture_packed compressed;
   struct agx_pbe_packed uncompressed;
   GLOBAL(uint64_t) metadata;
   uint64_t tile_uncompressed;
   uint32_t metadata_layer_stride_tl;
   uint16_t metadata_width_tl;
   uint16_t metadata_height_tl;
};
AGX_STATIC_ASSERT(sizeof(struct libagx_decompress_push) == 72);
