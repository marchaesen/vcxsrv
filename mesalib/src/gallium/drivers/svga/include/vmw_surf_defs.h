/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**********************************************************
 *
 * Copyright (c) 2024 Broadcom.
 * The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
 *
 **********************************************************/
#ifndef VMW_SURF_DEFS_H
#define VMW_SURF_DEFS_H

#include "util/macros.h"
#include "svga3d_surfacedefs.h"
#include "svga3d_types.h"

static inline uint32
clamped_umul32(uint32 a, uint32 b)
{
   uint64_t tmp = (uint64_t)a * b;
   return (tmp > (uint64_t)((uint32)-1)) ? (uint32)-1 : tmp;
}

static inline uint32
clamped_uadd32(uint32 a, uint32 b)
{
   uint32 c = a + b;
   if (c < a || c < b) {
      return MAX_UINT32;
   }
   return c;
}

static inline const struct SVGA3dSurfaceDesc *
vmw_surf_get_desc(SVGA3dSurfaceFormat format)
{
   if (format < ARRAY_SIZE(g_SVGA3dSurfaceDescs))
      return &g_SVGA3dSurfaceDescs[format];

   return &g_SVGA3dSurfaceDescs[SVGA3D_FORMAT_INVALID];
}

static inline SVGA3dSize
vmw_surf_get_mip_size(SVGA3dSize base_level, uint32 mip_level)
{
   SVGA3dSize size;

   size.width = MAX2(base_level.width >> mip_level, 1);
   size.height = MAX2(base_level.height >> mip_level, 1);
   size.depth = MAX2(base_level.depth >> mip_level, 1);
   return size;
}

static inline void
vmw_surf_get_size_in_blocks(const struct SVGA3dSurfaceDesc *desc,
                            const SVGA3dSize *pixel_size,
                            SVGA3dSize *block_size)
{
   block_size->width = DIV_ROUND_UP(pixel_size->width, desc->blockSize.width);
   block_size->height =
      DIV_ROUND_UP(pixel_size->height, desc->blockSize.height);
   block_size->depth = DIV_ROUND_UP(pixel_size->depth, desc->blockSize.depth);
}

static inline bool
vmw_surf_is_planar_surface(const struct SVGA3dSurfaceDesc *desc)
{
   return (desc->blockDesc & SVGA3DBLOCKDESC_PLANAR_YUV) != 0;
}

static inline uint32
vmw_surf_calculate_pitch(const struct SVGA3dSurfaceDesc *desc,
                         const SVGA3dSize *size)
{
   uint32 pitch;
   SVGA3dSize blocks;

   vmw_surf_get_size_in_blocks(desc, size, &blocks);

   pitch = blocks.width * desc->pitchBytesPerBlock;

   return pitch;
}

static inline uint32
vmw_surf_get_image_buffer_size(const struct SVGA3dSurfaceDesc *desc,
                               const SVGA3dSize *size, uint32 pitch)
{
   SVGA3dSize image_blocks;
   uint32 slice_size, total_size;

   vmw_surf_get_size_in_blocks(desc, size, &image_blocks);

   if (vmw_surf_is_planar_surface(desc)) {
      total_size = clamped_umul32(image_blocks.width, image_blocks.height);
      total_size = clamped_umul32(total_size, image_blocks.depth);
      total_size = clamped_umul32(total_size, desc->bytesPerBlock);
      return total_size;
   }

   if (pitch == 0)
      pitch = vmw_surf_calculate_pitch(desc, size);

   slice_size = clamped_umul32(image_blocks.height, pitch);
   total_size = clamped_umul32(slice_size, image_blocks.depth);

   return total_size;
}

static inline uint32
vmw_surf_get_serialized_size(SVGA3dSurfaceFormat format,
                             SVGA3dSize base_level_size, uint32 num_mip_levels,
                             uint32 num_layers)
{
   const struct SVGA3dSurfaceDesc *desc = vmw_surf_get_desc(format);
   uint64_t total_size = 0;
   uint32 mip;

   for (mip = 0; mip < num_mip_levels; mip++) {
      SVGA3dSize size = vmw_surf_get_mip_size(base_level_size, mip);
      total_size += vmw_surf_get_image_buffer_size(desc, &size, 0);
   }

   total_size *= num_layers;

   return (total_size > (uint64_t)MAX_UINT32) ? MAX_UINT32 : (uint32)total_size;
}

/**
 * vmw_surf_get_serialized_size_extended - Returns the number of bytes
 * required for a surface with given parameters. Support for sample count.
 *
 */
static inline uint32
vmw_surf_get_serialized_size_extended(SVGA3dSurfaceFormat format,
                                      SVGA3dSize base_level_size,
                                      uint32 num_mip_levels, uint32 num_layers,
                                      uint32 num_samples)
{
   uint64_t total_size = vmw_surf_get_serialized_size(
      format, base_level_size, num_mip_levels, num_layers);

   total_size *= (num_samples > 1 ? num_samples : 1);

   return (total_size > (uint64_t)MAX_UINT32) ? MAX_UINT32 : (uint32)total_size;
}

static inline uint32
vmw_surf_get_image_offset(SVGA3dSurfaceFormat format, SVGA3dSize baseLevelSize,
                          uint32 numMipLevels, uint32 layer, uint32 mip)

{
   uint32 offset;
   uint32 mipChainBytes;
   uint32 mipChainBytesToLevel;
   uint32 i;
   const struct SVGA3dSurfaceDesc *desc;
   SVGA3dSize mipSize;
   uint32 bytes;

   desc = vmw_surf_get_desc(format);

   mipChainBytes = 0;
   mipChainBytesToLevel = 0;
   for (i = 0; i < numMipLevels; i++) {
      mipSize = vmw_surf_get_mip_size(baseLevelSize, i);
      bytes = vmw_surf_get_image_buffer_size(desc, &mipSize, 0);
      mipChainBytes += bytes;
      if (i < mip) {
         mipChainBytesToLevel += bytes;
      }
   }

   offset = mipChainBytes * layer + mipChainBytesToLevel;

   return offset;
}

/**
 * Compute the offset (in bytes) to a pixel in an image (or volume).
 * 'width' is the image width in pixels
 * 'height' is the image height in pixels
 */
static inline uint32
vmw_surf_get_pixel_offset(SVGA3dSurfaceFormat format, uint32 width,
                          uint32 height, uint32 x, uint32 y, uint32 z)
{
   const struct SVGA3dSurfaceDesc *desc = vmw_surf_get_desc(format);
   const uint32 bw = desc->blockSize.width, bh = desc->blockSize.height;
   const uint32 bd = desc->blockSize.depth;
   const uint32 rowstride = DIV_ROUND_UP(width, bw) * desc->bytesPerBlock;
   const uint32 imgstride = DIV_ROUND_UP(height, bh) * rowstride;
   const uint32 offset =
      (z / bd * imgstride + y / bh * rowstride + x / bw * desc->bytesPerBlock);
   return offset;
}

#endif /* VMW_SURF_DEFS_H */
