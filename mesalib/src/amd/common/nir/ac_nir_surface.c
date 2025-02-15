/*
 * Copyright © 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir_surface.h"
#include "ac_gpu_info.h"
#include "nir_builder.h"
#include "sid.h"

static nir_def *gfx10_nir_meta_addr_from_coord(nir_builder *b, const struct radeon_info *info,
                                               const struct gfx9_meta_equation *equation,
                                               int blkSizeBias, unsigned blkStart,
                                               nir_def *meta_pitch, nir_def *meta_slice_size,
                                               nir_def *x, nir_def *y, nir_def *z,
                                               nir_def *pipe_xor,
                                               nir_def **bit_position)
{
   nir_def *zero = nir_imm_int(b, 0);
   nir_def *one = nir_imm_int(b, 1);

   assert(info->gfx_level >= GFX10);

   unsigned meta_block_width_log2 = util_logbase2(equation->meta_block_width);
   unsigned meta_block_height_log2 = util_logbase2(equation->meta_block_height);
   unsigned blkSizeLog2 = meta_block_width_log2 + meta_block_height_log2 + blkSizeBias;

   nir_def *coord[] = {x, y, z, 0};
   nir_def *address = zero;

   for (unsigned i = blkStart; i < blkSizeLog2 + 1; i++) {
      nir_def *v = zero;

      for (unsigned c = 0; c < 4; c++) {
         unsigned index = i * 4 + c - (blkStart * 4);
         if (equation->u.gfx10_bits[index]) {
            unsigned mask = equation->u.gfx10_bits[index];
            nir_def *bits = coord[c];

            while (mask)
               v = nir_ixor(b, v, nir_iand(b, nir_ushr_imm(b, bits, u_bit_scan(&mask)), one));
         }
      }

      address = nir_ior(b, address, nir_ishl_imm(b, v, i));
   }

   unsigned blkMask = (1 << blkSizeLog2) - 1;
   unsigned pipeMask = (1 << G_0098F8_NUM_PIPES(info->gb_addr_config)) - 1;
   unsigned m_pipeInterleaveLog2 = 8 + G_0098F8_PIPE_INTERLEAVE_SIZE_GFX9(info->gb_addr_config);
   nir_def *xb = nir_ushr_imm(b, x, meta_block_width_log2);
   nir_def *yb = nir_ushr_imm(b, y, meta_block_height_log2);
   nir_def *pb = nir_ushr_imm(b, meta_pitch, meta_block_width_log2);
   nir_def *blkIndex = nir_iadd(b, nir_imul(b, yb, pb), xb);
   nir_def *pipeXor = nir_iand_imm(b, nir_ishl_imm(b, nir_iand_imm(b, pipe_xor, pipeMask),
                                                       m_pipeInterleaveLog2), blkMask);

   if (bit_position)
      *bit_position = nir_ishl_imm(b, nir_iand_imm(b, address, 1), 2);

   return nir_iadd(b, nir_iadd(b, nir_imul(b, meta_slice_size, z),
                               nir_imul(b, blkIndex, nir_ishl_imm(b, one, blkSizeLog2))),
                   nir_ixor(b, nir_ushr(b, address, one), pipeXor));
}

static nir_def *gfx9_nir_meta_addr_from_coord(nir_builder *b, const struct radeon_info *info,
                                              const struct gfx9_meta_equation *equation,
                                              nir_def *meta_pitch, nir_def *meta_height,
                                              nir_def *x, nir_def *y, nir_def *z,
                                              nir_def *sample, nir_def *pipe_xor,
                                              nir_def **bit_position)
{
   nir_def *zero = nir_imm_int(b, 0);
   nir_def *one = nir_imm_int(b, 1);

   assert(info->gfx_level >= GFX9);

   unsigned meta_block_width_log2 = util_logbase2(equation->meta_block_width);
   unsigned meta_block_height_log2 = util_logbase2(equation->meta_block_height);
   unsigned meta_block_depth_log2 = util_logbase2(equation->meta_block_depth);

   unsigned m_pipeInterleaveLog2 = 8 + G_0098F8_PIPE_INTERLEAVE_SIZE_GFX9(info->gb_addr_config);
   unsigned numPipeBits = equation->u.gfx9.num_pipe_bits;
   nir_def *pitchInBlock = nir_ushr_imm(b, meta_pitch, meta_block_width_log2);
   nir_def *sliceSizeInBlock = nir_imul(b, nir_ushr_imm(b, meta_height, meta_block_height_log2),
                                            pitchInBlock);

   nir_def *xb = nir_ushr_imm(b, x, meta_block_width_log2);
   nir_def *yb = nir_ushr_imm(b, y, meta_block_height_log2);
   nir_def *zb = nir_ushr_imm(b, z, meta_block_depth_log2);

   nir_def *blockIndex = nir_iadd(b, nir_iadd(b, nir_imul(b, zb, sliceSizeInBlock),
                                                  nir_imul(b, yb, pitchInBlock)), xb);
   nir_def *coords[] = {x, y, z, sample, blockIndex};

   nir_def *address = zero;
   unsigned num_bits = equation->u.gfx9.num_bits;
   assert(num_bits <= 32);

   /* Compute the address up until the last bit that doesn't use the block index. */
   for (unsigned i = 0; i < num_bits - 1; i++) {
      nir_def *xor = zero;

      for (unsigned c = 0; c < 5; c++) {
         if (equation->u.gfx9.bit[i].coord[c].dim >= 5)
            continue;

         assert(equation->u.gfx9.bit[i].coord[c].ord < 32);
         nir_def *ison =
            nir_iand(b, nir_ushr_imm(b, coords[equation->u.gfx9.bit[i].coord[c].dim],
                                     equation->u.gfx9.bit[i].coord[c].ord), one);

         xor = nir_ixor(b, xor, ison);
      }
      address = nir_ior(b, address, nir_ishl_imm(b, xor, i));
   }

   /* Fill the remaining bits with the block index. */
   unsigned last = num_bits - 1;
   address = nir_ior(b, address,
                     nir_ishl_imm(b, nir_ushr_imm(b, blockIndex,
                                              equation->u.gfx9.bit[last].coord[0].ord),
                                  last));

   if (bit_position)
      *bit_position = nir_ishl_imm(b, nir_iand_imm(b, address, 1), 2);

   nir_def *pipeXor = nir_iand_imm(b, pipe_xor, (1 << numPipeBits) - 1);
   return nir_ixor(b, nir_ushr(b, address, one),
                   nir_ishl_imm(b, pipeXor, m_pipeInterleaveLog2));
}

nir_def *ac_nir_dcc_addr_from_coord(nir_builder *b, const struct radeon_info *info,
                                    unsigned bpe, const struct gfx9_meta_equation *equation,
                                    nir_def *dcc_pitch, nir_def *dcc_height,
                                    nir_def *dcc_slice_size,
                                    nir_def *x, nir_def *y, nir_def *z,
                                    nir_def *sample, nir_def *pipe_xor)
{
   if (info->gfx_level >= GFX10) {
      unsigned bpp_log2 = util_logbase2(bpe);

      return gfx10_nir_meta_addr_from_coord(b, info, equation, bpp_log2 - 8, 1,
                                            dcc_pitch, dcc_slice_size,
                                            x, y, z, pipe_xor, NULL);
   } else {
      return gfx9_nir_meta_addr_from_coord(b, info, equation, dcc_pitch,
                                           dcc_height, x, y, z,
                                           sample, pipe_xor, NULL);
   }
}

nir_def *ac_nir_cmask_addr_from_coord(nir_builder *b, const struct radeon_info *info,
                                      const struct gfx9_meta_equation *equation,
                                      nir_def *cmask_pitch, nir_def *cmask_height,
                                      nir_def *cmask_slice_size,
                                      nir_def *x, nir_def *y, nir_def *z,
                                      nir_def *pipe_xor,
                                      nir_def **bit_position)
{
   nir_def *zero = nir_imm_int(b, 0);

   if (info->gfx_level >= GFX10) {
      return gfx10_nir_meta_addr_from_coord(b, info, equation, -7, 1,
                                            cmask_pitch, cmask_slice_size,
                                            x, y, z, pipe_xor, bit_position);
   } else {
      return gfx9_nir_meta_addr_from_coord(b, info, equation, cmask_pitch,
                                           cmask_height, x, y, z, zero,
                                           pipe_xor, bit_position);
   }
}

nir_def *ac_nir_htile_addr_from_coord(nir_builder *b, const struct radeon_info *info,
                                      const struct gfx9_meta_equation *equation,
                                      nir_def *htile_pitch,
                                      nir_def *htile_slice_size,
                                      nir_def *x, nir_def *y, nir_def *z,
                                      nir_def *pipe_xor)
{
   return gfx10_nir_meta_addr_from_coord(b, info, equation, -4, 2,
                                            htile_pitch, htile_slice_size,
                                            x, y, z, pipe_xor, NULL);
}
