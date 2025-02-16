/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl.h"
#include "compiler/nir/nir_defines.h"
#include "compiler/shader_enums.h"
#include "agx_pack.h"
#include "compression.h"
#include "libagx_intrinsics.h"

/*
 * Decompress in place. The metadata is updated, so other processes can read the
 * image with a compressed texture descriptor.
 *
 * Each workgroup processes one 16x16 tile, avoiding races. We use 32x1
 * workgroups, matching the warp size, meaning each work-item must process
 * (16*16)/(32*1) = 8 sampels. Matching the warp size eliminates cross-warp
 * barriers. It also minimizes launched threads, accelerating the early exit.
 */

/* Our compiler represents a bindless handle as a uint2 of a uniform base and an
 * offset in bytes. Since the descriptors are all in the u0_u1 push, the former
 * is hardcoded and the latter is an offsetof.
 */
#define HANDLE(field)                                                          \
   (uint2)(0, offsetof(struct libagx_decompress_images, field))

/*
 * The metadata buffer is fully twiddled, so interleave the X/Y coordinate bits.
 * While dimensions are padded to powers-of-two, they are not padded to a
 * square. If the width is more than 2x the height or vice versa, the additional
 * bits are linear. So we interleave as much as possible, and then add what's
 * remaining. Finally, layers are strided linear and added at the end.
 */
static uint
index_metadata(uint3 c, uint width, uint height, uint layer_stride)
{
   uint major_coord = width > height ? c.x : c.y;
   uint minor_dim = min(width, height);

   uint intl_bits = util_logbase2_ceil(minor_dim);
   uint intl_mask = (1 << intl_bits) - 1;
   uint2 intl_coords = c.xy & intl_mask;

   return nir_interleave_agx(intl_coords.x, intl_coords.y) +
          ((major_coord & ~intl_mask) << intl_bits) + (layer_stride * c.z);
}

/*
 * For multisampled images, a 2x2 or 1x2 group of samples form a single pixel.
 * The following two helpers convert a coordinate in samples into a coordinate
 * in pixels and a sample ID, respectively. They each assume that samples > 1.
 */
static int4
decompose_px(int4 c, uint samples)
{
   if (samples == 4)
      c.xy >>= 1;
   else
      c.y >>= 1;

   return c;
}

static uint
sample_id(int4 c, uint samples)
{
   if (samples == 4)
      return (c.x & 1) | ((c.y & 1) << 1);
   else
      return c.y & 1;
}

KERNEL(32)
libagx_decompress(constant struct libagx_decompress_images *images,
                  global uint64_t *metadata, uint64_t tile_uncompressed,
                  uint32_t metadata_layer_stride_tl, uint16_t metadata_width_tl,
                  uint16_t metadata_height_tl,
                  uint log2_samples__3 /* 1x, 2x, 4x */)
{
   uint samples = 1 << log2_samples__3;

   /* Index into the metadata buffer */
   uint index_tl = index_metadata(cl_group_id, metadata_width_tl,
                                  metadata_height_tl, metadata_layer_stride_tl);

   /* If the tile is already uncompressed, there's nothing to do. */
   if (metadata[index_tl] == tile_uncompressed)
      return;

   /* Tiles are 16x16 */
   uint2 coord_sa = (cl_group_id.xy * 16);
   uint layer = cl_group_id.z;

   /* Since we use a 32x1 workgroup, each work-item handles half of a row. */
   uint offs_y_sa = cl_local_id.x >> 1;
   uint offs_x_sa = (cl_local_id.x & 1) ? 8 : 0;

   int2 img_coord_sa_2d = convert_int2(coord_sa) + (int2)(offs_x_sa, offs_y_sa);
   int4 img_coord_sa = (int4)(img_coord_sa_2d.x, img_coord_sa_2d.y, layer, 0);

   /* Read our half-row into registers. */
   uint4 texels[8];
   for (uint i = 0; i < 8; ++i) {
      int4 c_sa = img_coord_sa + (int4)(i, 0, 0, 0);
      if (samples == 1) {
         texels[i] = nir_bindless_image_load(
            HANDLE(compressed), c_sa, 0, 0, GLSL_SAMPLER_DIM_2D, true, 0,
            ACCESS_IN_BOUNDS_AGX, nir_type_uint32);
      } else {
         int4 dec_px = decompose_px(c_sa, samples);
         texels[i] = nir_bindless_image_load(
            HANDLE(compressed), dec_px, sample_id(c_sa, samples), 0,
            GLSL_SAMPLER_DIM_MS, true, 0, ACCESS_IN_BOUNDS_AGX,
            nir_type_uint32);
      }
   }

   sub_group_barrier(CLK_LOCAL_MEM_FENCE);

   /* Now that the whole tile is read, we write without racing. */
   for (uint i = 0; i < 8; ++i) {
      int4 c_sa = img_coord_sa + (int4)(i, 0, 0, 0);
      if (samples == 1) {
         nir_bindless_image_store(HANDLE(uncompressed), c_sa, 0, texels[i], 0,
                                  GLSL_SAMPLER_DIM_2D, true, 0,
                                  ACCESS_NON_READABLE, nir_type_uint32);
      } else {
         int4 dec_px = decompose_px(c_sa, samples);

         nir_bindless_image_store(HANDLE(uncompressed), dec_px,
                                  sample_id(c_sa, samples), texels[i], 0,
                                  GLSL_SAMPLER_DIM_MS, true, 0,
                                  ACCESS_NON_READABLE, nir_type_uint32);
      }
   }

   /* We've replaced the body buffer. Mark the tile as uncompressed. */
   if (cl_local_id.x == 0) {
      metadata[index_tl] = tile_uncompressed;
   }
}
