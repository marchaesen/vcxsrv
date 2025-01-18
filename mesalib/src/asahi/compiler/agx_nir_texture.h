/*
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* Texture backend flags */
#define AGX_TEXTURE_FLAG_NO_CLAMP (1 << 0)

/* Indicates that the sampler should be overriden to clamp to 0 instead of 1 */
#define AGX_TEXTURE_FLAG_CLAMP_TO_0 (1 << 1)

/* Texel buffers lowered to (at most) 16384x16384 2D textures */
#define AGX_TEXTURE_BUFFER_WIDTH      16384
#define AGX_TEXTURE_BUFFER_MAX_HEIGHT 16384
#define AGX_TEXTURE_BUFFER_MAX_SIZE                                            \
   (AGX_TEXTURE_BUFFER_WIDTH * AGX_TEXTURE_BUFFER_MAX_HEIGHT)
