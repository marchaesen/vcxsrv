/*
 * Copyright © Microsoft Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "dxil_nir.h"

#include "nir_builder.h"
#include "nir_deref.h"
#include "util/u_math.h"

static void
extract_comps_from_vec32(nir_builder *b, nir_ssa_def *vec32,
                         unsigned dst_bit_size,
                         nir_ssa_def **dst_comps,
                         unsigned num_dst_comps)
{
   unsigned step = DIV_ROUND_UP(dst_bit_size, 32);
   unsigned comps_per32b = 32 / dst_bit_size;
   nir_ssa_def *tmp;

   for (unsigned i = 0; i < vec32->num_components; i += step) {
      switch (dst_bit_size) {
      case 64:
         tmp = nir_pack_64_2x32_split(b, nir_channel(b, vec32, i),
                                         nir_channel(b, vec32, i + 1));
         dst_comps[i / 2] = tmp;
         break;
      case 32:
         dst_comps[i] = nir_channel(b, vec32, i);
         break;
      case 16:
      case 8:
         unsigned dst_offs = i * comps_per32b;

         tmp = nir_unpack_bits(b, nir_channel(b, vec32, i), dst_bit_size);
         for (unsigned j = 0; j < comps_per32b && dst_offs + j < num_dst_comps; j++)
            dst_comps[dst_offs + j] = nir_channel(b, tmp, j);

         break;
      }
   }
}

static nir_ssa_def *
ubo_load_select_32b_comps(nir_builder *b, nir_ssa_def *vec32,
                          nir_ssa_def *offset, unsigned num_bytes)
{
   assert(num_bytes == 16 || num_bytes == 12 || num_bytes == 8 ||
          num_bytes == 4 || num_bytes == 3 || num_bytes == 2 ||
          num_bytes == 1);
   assert(vec32->num_components == 4);

   /* 16 and 12 byte types are always aligned on 16 bytes. */
   if (num_bytes > 8)
      return vec32;

   nir_ssa_def *comps[4];
   nir_ssa_def *cond;

   for (unsigned i = 0; i < 4; i++)
      comps[i] = nir_channel(b, vec32, i);

   /* If we have 8bytes or less to load, select which half the vec4 should
    * be used.
    */
   cond = nir_ine(b, nir_iand(b, offset, nir_imm_int(b, 0x8)),
                                 nir_imm_int(b, 0));

   comps[0] = nir_bcsel(b, cond, comps[2], comps[0]);
   comps[1] = nir_bcsel(b, cond, comps[3], comps[1]);

   /* Thanks to the CL alignment constraints, if we want 8 bytes we're done. */
   if (num_bytes == 8)
      return nir_vec(b, comps, 2);

   /* 4 bytes or less needed, select which of the 32bit component should be
    * used and return it. The sub-32bit split is handled in
    * extract_comps_from_vec32().
    */
   cond = nir_ine(b, nir_iand(b, offset, nir_imm_int(b, 0x4)),
                                 nir_imm_int(b, 0));
   return nir_bcsel(b, cond, comps[1], comps[0]);
}

nir_ssa_def *
build_load_ubo_dxil(nir_builder *b, nir_ssa_def *buffer,
                    nir_ssa_def *offset, unsigned num_components,
                    unsigned bit_size)
{
   nir_ssa_def *idx = nir_ushr(b, offset, nir_imm_int(b, 4));
   nir_ssa_def *comps[NIR_MAX_VEC_COMPONENTS];
   unsigned num_bits = num_components * bit_size;
   unsigned comp_idx = 0;

   /* We need to split loads in 16byte chunks because that's the
    * granularity of cBufferLoadLegacy().
    */
   for (unsigned i = 0; i < num_bits; i += (16 * 8)) {
      /* For each 16byte chunk (or smaller) we generate a 32bit ubo vec
       * load.
       */
      unsigned subload_num_bits = MIN2(num_bits - i, 16 * 8);
      nir_intrinsic_instr *load =
         nir_intrinsic_instr_create(b->shader,
                                    nir_intrinsic_load_ubo_dxil);

      load->num_components = 4;
      load->src[0] = nir_src_for_ssa(buffer);
      load->src[1] = nir_src_for_ssa(nir_iadd(b, idx, nir_imm_int(b, i / (16 * 8))));
      nir_ssa_dest_init(&load->instr, &load->dest, load->num_components,
                        32, NULL);
      nir_builder_instr_insert(b, &load->instr);

      nir_ssa_def *vec32 = &load->dest.ssa;

      /* First re-arrange the vec32 to account for intra 16-byte offset. */
      vec32 = ubo_load_select_32b_comps(b, vec32, offset, subload_num_bits / 8);

      /* If we have 2 bytes or less to load we need to adjust the u32 value so
       * we can always extract the LSB.
       */
      if (subload_num_bits <= 16) {
         nir_ssa_def *shift = nir_imul(b, nir_iand(b, offset,
                                                      nir_imm_int(b, 3)),
                                          nir_imm_int(b, 8));
         vec32 = nir_ushr(b, vec32, shift);
      }

      /* And now comes the pack/unpack step to match the original type. */
      extract_comps_from_vec32(b, vec32, bit_size, &comps[comp_idx],
                               subload_num_bits / bit_size);
      comp_idx += subload_num_bits / bit_size;
   }

   assert(comp_idx == num_components);
   return nir_vec(b, comps, num_components);
}
