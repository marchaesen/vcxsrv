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
#include "nir_to_dxil.h"
#include "util/u_math.h"

static void
cl_type_size_align(const struct glsl_type *type, unsigned *size,
                   unsigned *align)
{
   *size = glsl_get_cl_size(type);
   *align = glsl_get_cl_alignment(type);
}

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
      case 8: {
         unsigned dst_offs = i * comps_per32b;

         tmp = nir_unpack_bits(b, nir_channel(b, vec32, i), dst_bit_size);
         for (unsigned j = 0; j < comps_per32b && dst_offs + j < num_dst_comps; j++)
            dst_comps[dst_offs + j] = nir_channel(b, tmp, j);
         }

         break;
      }
   }
}

static nir_ssa_def *
load_comps_to_vec32(nir_builder *b, unsigned src_bit_size,
                    nir_ssa_def **src_comps, unsigned num_src_comps)
{
   unsigned num_vec32comps = DIV_ROUND_UP(num_src_comps * src_bit_size, 32);
   unsigned step = DIV_ROUND_UP(src_bit_size, 32);
   unsigned comps_per32b = 32 / src_bit_size;
   nir_ssa_def *vec32comps[4];

   for (unsigned i = 0; i < num_vec32comps; i += step) {
      switch (src_bit_size) {
      case 64:
         vec32comps[i] = nir_unpack_64_2x32_split_x(b, src_comps[i / 2]);
         vec32comps[i + 1] = nir_unpack_64_2x32_split_y(b, src_comps[i / 2]);
         break;
      case 32:
         vec32comps[i] = src_comps[i];
         break;
      case 16:
      case 8: {
         unsigned src_offs = i * comps_per32b;

         vec32comps[i] = nir_u2u32(b, src_comps[src_offs]);
         for (unsigned j = 1; j < comps_per32b && src_offs + j < num_src_comps; j++) {
            nir_ssa_def *tmp = nir_ishl(b, nir_u2u32(b, src_comps[src_offs + j]),
                                           nir_imm_int(b, j * src_bit_size));
            vec32comps[i] = nir_ior(b, vec32comps[i], tmp);
         }
         break;
      }
      }
   }

   return nir_vec(b, vec32comps, num_vec32comps);
}

static nir_ssa_def *
build_load_ptr_dxil(nir_builder *b, nir_deref_instr *deref, nir_ssa_def *idx)
{
   return nir_load_ptr_dxil(b, 1, 32, &deref->dest.ssa, idx);
}

static bool
lower_load_deref(nir_builder *b, nir_intrinsic_instr *intr)
{
   assert(intr->dest.is_ssa);

   b->cursor = nir_before_instr(&intr->instr);

   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   if (!nir_deref_mode_is(deref, nir_var_shader_temp))
      return false;
   nir_ssa_def *ptr = nir_u2u32(b, nir_build_deref_offset(b, deref, cl_type_size_align));
   nir_ssa_def *offset = nir_iand(b, ptr, nir_inot(b, nir_imm_int(b, 3)));

   assert(intr->dest.is_ssa);
   unsigned num_components = nir_dest_num_components(intr->dest);
   unsigned bit_size = nir_dest_bit_size(intr->dest);
   unsigned load_size = MAX2(32, bit_size);
   unsigned num_bits = num_components * bit_size;
   nir_ssa_def *comps[NIR_MAX_VEC_COMPONENTS];
   unsigned comp_idx = 0;

   nir_deref_path path;
   nir_deref_path_init(&path, deref, NULL);
   nir_ssa_def *base_idx = nir_ishr(b, offset, nir_imm_int(b, 2 /* log2(32 / 8) */));

   /* Split loads into 32-bit chunks */
   for (unsigned i = 0; i < num_bits; i += load_size) {
      unsigned subload_num_bits = MIN2(num_bits - i, load_size);
      nir_ssa_def *idx = nir_iadd(b, base_idx, nir_imm_int(b, i / 32));
      nir_ssa_def *vec32 = build_load_ptr_dxil(b, path.path[0], idx);

      if (load_size == 64) {
         idx = nir_iadd(b, idx, nir_imm_int(b, 1));
         vec32 = nir_vec2(b, vec32,
                             build_load_ptr_dxil(b, path.path[0], idx));
      }

      /* If we have 2 bytes or less to load we need to adjust the u32 value so
       * we can always extract the LSB.
       */
      if (subload_num_bits <= 16) {
         nir_ssa_def *shift = nir_imul(b, nir_iand(b, ptr, nir_imm_int(b, 3)),
                                          nir_imm_int(b, 8));
         vec32 = nir_ushr(b, vec32, shift);
      }

      /* And now comes the pack/unpack step to match the original type. */
      extract_comps_from_vec32(b, vec32, bit_size, &comps[comp_idx],
                               subload_num_bits / bit_size);
      comp_idx += subload_num_bits / bit_size;
   }

   nir_deref_path_finish(&path);
   assert(comp_idx == num_components);
   nir_ssa_def *result = nir_vec(b, comps, num_components);
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, result);
   nir_instr_remove(&intr->instr);
   return true;
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
      nir_ssa_def *vec32 =
         nir_load_ubo_dxil(b, 4, 32, buffer, nir_iadd(b, idx, nir_imm_int(b, i / (16 * 8))));

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

static bool
lower_load_ssbo(nir_builder *b, nir_intrinsic_instr *intr)
{
   assert(intr->dest.is_ssa);
   assert(intr->src[0].is_ssa);
   assert(intr->src[1].is_ssa);

   b->cursor = nir_before_instr(&intr->instr);

   nir_ssa_def *buffer = intr->src[0].ssa;
   nir_ssa_def *offset = nir_iand(b, intr->src[1].ssa, nir_imm_int(b, ~3));
   enum gl_access_qualifier access = nir_intrinsic_access(intr);
   unsigned bit_size = nir_dest_bit_size(intr->dest);
   unsigned num_components = nir_dest_num_components(intr->dest);
   unsigned num_bits = num_components * bit_size;

   nir_ssa_def *comps[NIR_MAX_VEC_COMPONENTS];
   unsigned comp_idx = 0;

   /* We need to split loads in 16byte chunks because that's the optimal
    * granularity of bufferLoad(). Minimum alignment is 4byte, which saves
    * from us from extra complexity to extract >= 32 bit components.
    */
   for (unsigned i = 0; i < num_bits; i += 4 * 32) {
      /* For each 16byte chunk (or smaller) we generate a 32bit ssbo vec
       * load.
       */
      unsigned subload_num_bits = MIN2(num_bits - i, 4 * 32);

      /* The number of components to store depends on the number of bytes. */
      nir_ssa_def *vec32 =
         nir_load_ssbo(b, DIV_ROUND_UP(subload_num_bits, 32), 32,
                       buffer, nir_iadd(b, offset, nir_imm_int(b, i / 8)),
                       .align_mul = 4,
                       .align_offset = 0,
                       .access = access);

      /* If we have 2 bytes or less to load we need to adjust the u32 value so
       * we can always extract the LSB.
       */
      if (subload_num_bits <= 16) {
         nir_ssa_def *shift = nir_imul(b, nir_iand(b, intr->src[1].ssa, nir_imm_int(b, 3)),
                                          nir_imm_int(b, 8));
         vec32 = nir_ushr(b, vec32, shift);
      }

      /* And now comes the pack/unpack step to match the original type. */
      extract_comps_from_vec32(b, vec32, bit_size, &comps[comp_idx],
                               subload_num_bits / bit_size);
      comp_idx += subload_num_bits / bit_size;
   }

   assert(comp_idx == num_components);
   nir_ssa_def *result = nir_vec(b, comps, num_components);
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, result);
   nir_instr_remove(&intr->instr);
   return true;
}

static bool
lower_store_ssbo(nir_builder *b, nir_intrinsic_instr *intr)
{
   b->cursor = nir_before_instr(&intr->instr);

   assert(intr->src[0].is_ssa);
   assert(intr->src[1].is_ssa);
   assert(intr->src[2].is_ssa);

   nir_ssa_def *val = intr->src[0].ssa;
   nir_ssa_def *buffer = intr->src[1].ssa;
   nir_ssa_def *offset = nir_iand(b, intr->src[2].ssa, nir_imm_int(b, ~3));

   unsigned bit_size = val->bit_size;
   unsigned num_components = val->num_components;
   unsigned num_bits = num_components * bit_size;

   nir_ssa_def *comps[NIR_MAX_VEC_COMPONENTS];
   unsigned comp_idx = 0;

   for (unsigned i = 0; i < num_components; i++)
      comps[i] = nir_channel(b, val, i);

   /* We split stores in 16byte chunks because that's the optimal granularity
    * of bufferStore(). Minimum alignment is 4byte, which saves from us from
    * extra complexity to store >= 32 bit components.
    */
   for (unsigned i = 0; i < num_bits; i += 4 * 32) {
      /* For each 16byte chunk (or smaller) we generate a 32bit ssbo vec
       * store.
       */
      unsigned substore_num_bits = MIN2(num_bits - i, 4 * 32);
      nir_ssa_def *local_offset = nir_iadd(b, offset, nir_imm_int(b, i / 8));
      nir_ssa_def *vec32 = load_comps_to_vec32(b, bit_size, &comps[comp_idx],
                                               substore_num_bits / bit_size);
      nir_intrinsic_instr *store;

      if (substore_num_bits < 32) {
         nir_ssa_def *mask = nir_imm_int(b, (1 << substore_num_bits) - 1);

        /* If we have 16 bits or less to store we need to place them
         * correctly in the u32 component. Anything greater than 16 bits
         * (including uchar3) is naturally aligned on 32bits.
         */
         if (substore_num_bits <= 16) {
            nir_ssa_def *pos = nir_iand(b, intr->src[2].ssa, nir_imm_int(b, 3));
            nir_ssa_def *shift = nir_imul_imm(b, pos, 8);

            vec32 = nir_ishl(b, vec32, shift);
            mask = nir_ishl(b, mask, shift);
         }

         store = nir_intrinsic_instr_create(b->shader,
                                            nir_intrinsic_store_ssbo_masked_dxil);
         store->src[0] = nir_src_for_ssa(vec32);
         store->src[1] = nir_src_for_ssa(nir_inot(b, mask));
         store->src[2] = nir_src_for_ssa(buffer);
         store->src[3] = nir_src_for_ssa(local_offset);
      } else {
         store = nir_intrinsic_instr_create(b->shader,
                                            nir_intrinsic_store_ssbo);
         store->src[0] = nir_src_for_ssa(vec32);
         store->src[1] = nir_src_for_ssa(buffer);
         store->src[2] = nir_src_for_ssa(local_offset);

         nir_intrinsic_set_align(store, 4, 0);
      }

      /* The number of components to store depends on the number of bits. */
      store->num_components = DIV_ROUND_UP(substore_num_bits, 32);
      nir_builder_instr_insert(b, &store->instr);
      comp_idx += substore_num_bits / bit_size;
   }

   nir_instr_remove(&intr->instr);
   return true;
}

static void
lower_load_vec32(nir_builder *b, nir_ssa_def *index, unsigned num_comps, nir_ssa_def **comps, nir_intrinsic_op op)
{
   for (unsigned i = 0; i < num_comps; i++) {
      nir_intrinsic_instr *load =
         nir_intrinsic_instr_create(b->shader, op);

      load->num_components = 1;
      load->src[0] = nir_src_for_ssa(nir_iadd(b, index, nir_imm_int(b, i)));
      nir_ssa_dest_init(&load->instr, &load->dest, 1, 32, NULL);
      nir_builder_instr_insert(b, &load->instr);
      comps[i] = &load->dest.ssa;
   }
}

static bool
lower_32b_offset_load(nir_builder *b, nir_intrinsic_instr *intr)
{
   assert(intr->dest.is_ssa);
   unsigned bit_size = nir_dest_bit_size(intr->dest);
   unsigned num_components = nir_dest_num_components(intr->dest);
   unsigned num_bits = num_components * bit_size;

   b->cursor = nir_before_instr(&intr->instr);
   nir_intrinsic_op op = intr->intrinsic;

   assert(intr->src[0].is_ssa);
   nir_ssa_def *offset = intr->src[0].ssa;
   if (op == nir_intrinsic_load_shared) {
      offset = nir_iadd(b, offset, nir_imm_int(b, nir_intrinsic_base(intr)));
      op = nir_intrinsic_load_shared_dxil;
   } else {
      offset = nir_u2u32(b, offset);
      op = nir_intrinsic_load_scratch_dxil;
   }
   nir_ssa_def *index = nir_ushr(b, offset, nir_imm_int(b, 2));
   nir_ssa_def *comps[NIR_MAX_VEC_COMPONENTS];
   nir_ssa_def *comps_32bit[NIR_MAX_VEC_COMPONENTS * 2];

   /* We need to split loads in 32-bit accesses because the buffer
    * is an i32 array and DXIL does not support type casts.
    */
   unsigned num_32bit_comps = DIV_ROUND_UP(num_bits, 32);
   lower_load_vec32(b, index, num_32bit_comps, comps_32bit, op);
   unsigned num_comps_per_pass = MIN2(num_32bit_comps, 4);

   for (unsigned i = 0; i < num_32bit_comps; i += num_comps_per_pass) {
      unsigned num_vec32_comps = MIN2(num_32bit_comps - i, 4);
      unsigned num_dest_comps = num_vec32_comps * 32 / bit_size;
      nir_ssa_def *vec32 = nir_vec(b, &comps_32bit[i], num_vec32_comps);

      /* If we have 16 bits or less to load we need to adjust the u32 value so
       * we can always extract the LSB.
       */
      if (num_bits <= 16) {
         nir_ssa_def *shift =
            nir_imul(b, nir_iand(b, offset, nir_imm_int(b, 3)),
                        nir_imm_int(b, 8));
         vec32 = nir_ushr(b, vec32, shift);
      }

      /* And now comes the pack/unpack step to match the original type. */
      unsigned dest_index = i * 32 / bit_size;
      extract_comps_from_vec32(b, vec32, bit_size, &comps[dest_index], num_dest_comps);
   }

   nir_ssa_def *result = nir_vec(b, comps, num_components);
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, result);
   nir_instr_remove(&intr->instr);

   return true;
}

static void
lower_store_vec32(nir_builder *b, nir_ssa_def *index, nir_ssa_def *vec32, nir_intrinsic_op op)
{

   for (unsigned i = 0; i < vec32->num_components; i++) {
      nir_intrinsic_instr *store =
         nir_intrinsic_instr_create(b->shader, op);

      store->src[0] = nir_src_for_ssa(nir_channel(b, vec32, i));
      store->src[1] = nir_src_for_ssa(nir_iadd(b, index, nir_imm_int(b, i)));
      store->num_components = 1;
      nir_builder_instr_insert(b, &store->instr);
   }
}

static void
lower_masked_store_vec32(nir_builder *b, nir_ssa_def *offset, nir_ssa_def *index,
                         nir_ssa_def *vec32, unsigned num_bits, nir_intrinsic_op op)
{
   nir_ssa_def *mask = nir_imm_int(b, (1 << num_bits) - 1);

   /* If we have 16 bits or less to store we need to place them correctly in
    * the u32 component. Anything greater than 16 bits (including uchar3) is
    * naturally aligned on 32bits.
    */
   if (num_bits <= 16) {
      nir_ssa_def *shift =
         nir_imul_imm(b, nir_iand(b, offset, nir_imm_int(b, 3)), 8);

      vec32 = nir_ishl(b, vec32, shift);
      mask = nir_ishl(b, mask, shift);
   }

   if (op == nir_intrinsic_store_shared_dxil) {
      /* Use the dedicated masked intrinsic */
      nir_store_shared_masked_dxil(b, vec32, nir_inot(b, mask), index);
   } else {
      /* For scratch, since we don't need atomics, just generate the read-modify-write in NIR */
      nir_ssa_def *load = nir_load_scratch_dxil(b, 1, 32, index);

      nir_ssa_def *new_val = nir_ior(b, vec32,
                                     nir_iand(b,
                                              nir_inot(b, mask),
                                              load));

      lower_store_vec32(b, index, new_val, op);
   }
}

static bool
lower_32b_offset_store(nir_builder *b, nir_intrinsic_instr *intr)
{
   assert(intr->src[0].is_ssa);
   unsigned num_components = nir_src_num_components(intr->src[0]);
   unsigned bit_size = nir_src_bit_size(intr->src[0]);
   unsigned num_bits = num_components * bit_size;

   b->cursor = nir_before_instr(&intr->instr);
   nir_intrinsic_op op = intr->intrinsic;

   nir_ssa_def *offset = intr->src[1].ssa;
   if (op == nir_intrinsic_store_shared) {
      offset = nir_iadd(b, offset, nir_imm_int(b, nir_intrinsic_base(intr)));
      op = nir_intrinsic_store_shared_dxil;
   } else {
      offset = nir_u2u32(b, offset);
      op = nir_intrinsic_store_scratch_dxil;
   }
   nir_ssa_def *comps[NIR_MAX_VEC_COMPONENTS];

   unsigned comp_idx = 0;
   for (unsigned i = 0; i < num_components; i++)
      comps[i] = nir_channel(b, intr->src[0].ssa, i);

   for (unsigned i = 0; i < num_bits; i += 4 * 32) {
      /* For each 4byte chunk (or smaller) we generate a 32bit scalar store.
       */
      unsigned substore_num_bits = MIN2(num_bits - i, 4 * 32);
      nir_ssa_def *local_offset = nir_iadd(b, offset, nir_imm_int(b, i / 8));
      nir_ssa_def *vec32 = load_comps_to_vec32(b, bit_size, &comps[comp_idx],
                                               substore_num_bits / bit_size);
      nir_ssa_def *index = nir_ushr(b, local_offset, nir_imm_int(b, 2));

      /* For anything less than 32bits we need to use the masked version of the
       * intrinsic to preserve data living in the same 32bit slot.
       */
      if (num_bits < 32) {
         lower_masked_store_vec32(b, local_offset, index, vec32, num_bits, op);
      } else {
         lower_store_vec32(b, index, vec32, op);
      }

      comp_idx += substore_num_bits / bit_size;
   }

   nir_instr_remove(&intr->instr);

   return true;
}

static void
ubo_to_temp_patch_deref_mode(nir_deref_instr *deref)
{
   deref->modes = nir_var_shader_temp;
   nir_foreach_use(use_src, &deref->dest.ssa) {
      if (use_src->parent_instr->type != nir_instr_type_deref)
	 continue;

      nir_deref_instr *parent = nir_instr_as_deref(use_src->parent_instr);
      ubo_to_temp_patch_deref_mode(parent);
   }
}

static void
ubo_to_temp_update_entry(nir_deref_instr *deref, struct hash_entry *he)
{
   assert(nir_deref_mode_is(deref, nir_var_mem_constant));
   assert(deref->dest.is_ssa);
   assert(he->data);

   nir_foreach_use(use_src, &deref->dest.ssa) {
      if (use_src->parent_instr->type == nir_instr_type_deref) {
         ubo_to_temp_update_entry(nir_instr_as_deref(use_src->parent_instr), he);
      } else if (use_src->parent_instr->type == nir_instr_type_intrinsic) {
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(use_src->parent_instr);
         if (intr->intrinsic != nir_intrinsic_load_deref)
            he->data = NULL;
      } else {
         he->data = NULL;
      }

      if (!he->data)
         break;
   }
}

bool
dxil_nir_lower_ubo_to_temp(nir_shader *nir)
{
   struct hash_table *ubo_to_temp = _mesa_pointer_hash_table_create(NULL);
   bool progress = false;

   /* First pass: collect all UBO accesses that could be turned into
    * shader temp accesses.
    */
   foreach_list_typed(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         continue;
      assert(func->impl);

      nir_foreach_block(block, func->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_deref)
               continue;

            nir_deref_instr *deref = nir_instr_as_deref(instr);
            if (!nir_deref_mode_is(deref, nir_var_mem_constant) ||
                deref->deref_type != nir_deref_type_var)
                  continue;

            struct hash_entry *he =
               _mesa_hash_table_search(ubo_to_temp, deref->var);

            if (!he)
               he = _mesa_hash_table_insert(ubo_to_temp, deref->var, deref->var);

            if (!he->data)
               continue;

            ubo_to_temp_update_entry(deref, he);
         }
      }
   }

   hash_table_foreach(ubo_to_temp, he) {
      nir_variable *var = he->data;

      if (!var)
         continue;

      /* Change the variable mode. */
      var->data.mode = nir_var_shader_temp;

      /* Make sure the variable has a name.
       * DXIL variables must have names.
       */
      if (!var->name)
         var->name = ralloc_asprintf(nir, "global_%d", exec_list_length(&nir->variables));

      progress = true;
   }
   _mesa_hash_table_destroy(ubo_to_temp, NULL);

   /* Second pass: patch all derefs that were accessing the converted UBOs
    * variables.
    */
   foreach_list_typed(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         continue;
      assert(func->impl);

      nir_foreach_block(block, func->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_deref)
               continue;

            nir_deref_instr *deref = nir_instr_as_deref(instr);
            if (nir_deref_mode_is(deref, nir_var_mem_constant) &&
                deref->deref_type == nir_deref_type_var &&
                deref->var->data.mode == nir_var_shader_temp)
               ubo_to_temp_patch_deref_mode(deref);
         }
      }
   }

   return progress;
}

static bool
lower_load_ubo(nir_builder *b, nir_intrinsic_instr *intr)
{
   assert(intr->dest.is_ssa);
   assert(intr->src[0].is_ssa);
   assert(intr->src[1].is_ssa);

   b->cursor = nir_before_instr(&intr->instr);

   nir_ssa_def *result =
      build_load_ubo_dxil(b, intr->src[0].ssa, intr->src[1].ssa,
                             nir_dest_num_components(intr->dest),
                             nir_dest_bit_size(intr->dest));

   nir_ssa_def_rewrite_uses(&intr->dest.ssa, result);
   nir_instr_remove(&intr->instr);
   return true;
}

bool
dxil_nir_lower_loads_stores_to_dxil(nir_shader *nir)
{
   bool progress = false;

   foreach_list_typed(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         continue;
      assert(func->impl);

      nir_builder b;
      nir_builder_init(&b, func->impl);

      nir_foreach_block(block, func->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

            switch (intr->intrinsic) {
            case nir_intrinsic_load_deref:
               progress |= lower_load_deref(&b, intr);
               break;
            case nir_intrinsic_load_shared:
            case nir_intrinsic_load_scratch:
               progress |= lower_32b_offset_load(&b, intr);
               break;
            case nir_intrinsic_load_ssbo:
               progress |= lower_load_ssbo(&b, intr);
               break;
            case nir_intrinsic_load_ubo:
               progress |= lower_load_ubo(&b, intr);
               break;
            case nir_intrinsic_store_shared:
            case nir_intrinsic_store_scratch:
               progress |= lower_32b_offset_store(&b, intr);
               break;
            case nir_intrinsic_store_ssbo:
               progress |= lower_store_ssbo(&b, intr);
               break;
            default:
               break;
            }
         }
      }
   }

   return progress;
}

static bool
lower_shared_atomic(nir_builder *b, nir_intrinsic_instr *intr,
                    nir_intrinsic_op dxil_op)
{
   b->cursor = nir_before_instr(&intr->instr);

   assert(intr->src[0].is_ssa);
   nir_ssa_def *offset =
      nir_iadd(b, intr->src[0].ssa, nir_imm_int(b, nir_intrinsic_base(intr)));
   nir_ssa_def *index = nir_ushr(b, offset, nir_imm_int(b, 2));

   nir_intrinsic_instr *atomic = nir_intrinsic_instr_create(b->shader, dxil_op);
   atomic->src[0] = nir_src_for_ssa(index);
   assert(intr->src[1].is_ssa);
   atomic->src[1] = nir_src_for_ssa(intr->src[1].ssa);
   if (dxil_op == nir_intrinsic_shared_atomic_comp_swap_dxil) {
      assert(intr->src[2].is_ssa);
      atomic->src[2] = nir_src_for_ssa(intr->src[2].ssa);
   }
   atomic->num_components = 0;
   nir_ssa_dest_init(&atomic->instr, &atomic->dest, 1, 32, NULL);

   nir_builder_instr_insert(b, &atomic->instr);
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, &atomic->dest.ssa);
   nir_instr_remove(&intr->instr);
   return true;
}

bool
dxil_nir_lower_atomics_to_dxil(nir_shader *nir)
{
   bool progress = false;

   foreach_list_typed(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         continue;
      assert(func->impl);

      nir_builder b;
      nir_builder_init(&b, func->impl);

      nir_foreach_block(block, func->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

            switch (intr->intrinsic) {

#define ATOMIC(op)                                                            \
  case nir_intrinsic_shared_atomic_##op:                                     \
     progress |= lower_shared_atomic(&b, intr,                                \
                                     nir_intrinsic_shared_atomic_##op##_dxil); \
     break

            ATOMIC(add);
            ATOMIC(imin);
            ATOMIC(umin);
            ATOMIC(imax);
            ATOMIC(umax);
            ATOMIC(and);
            ATOMIC(or);
            ATOMIC(xor);
            ATOMIC(exchange);
            ATOMIC(comp_swap);

#undef ATOMIC
            default:
               break;
            }
         }
      }
   }

   return progress;
}

static bool
lower_deref_ssbo(nir_builder *b, nir_deref_instr *deref)
{
   assert(nir_deref_mode_is(deref, nir_var_mem_ssbo));
   assert(deref->deref_type == nir_deref_type_var ||
          deref->deref_type == nir_deref_type_cast);
   nir_variable *var = deref->var;

   b->cursor = nir_before_instr(&deref->instr);

   if (deref->deref_type == nir_deref_type_var) {
      /* We turn all deref_var into deref_cast and build a pointer value based on
       * the var binding which encodes the UAV id.
       */
      nir_ssa_def *ptr = nir_imm_int64(b, (uint64_t)var->data.binding << 32);
      nir_deref_instr *deref_cast =
         nir_build_deref_cast(b, ptr, nir_var_mem_ssbo, deref->type,
                              glsl_get_explicit_stride(var->type));
      nir_ssa_def_rewrite_uses(&deref->dest.ssa,
                               &deref_cast->dest.ssa);
      nir_instr_remove(&deref->instr);

      deref = deref_cast;
      return true;
   }
   return false;
}

bool
dxil_nir_lower_deref_ssbo(nir_shader *nir)
{
   bool progress = false;

   foreach_list_typed(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         continue;
      assert(func->impl);

      nir_builder b;
      nir_builder_init(&b, func->impl);

      nir_foreach_block(block, func->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_deref)
               continue;

            nir_deref_instr *deref = nir_instr_as_deref(instr);

            if (!nir_deref_mode_is(deref, nir_var_mem_ssbo) ||
                (deref->deref_type != nir_deref_type_var &&
                 deref->deref_type != nir_deref_type_cast))
               continue;

            progress |= lower_deref_ssbo(&b, deref);
         }
      }
   }

   return progress;
}

static bool
lower_alu_deref_srcs(nir_builder *b, nir_alu_instr *alu)
{
   const nir_op_info *info = &nir_op_infos[alu->op];
   bool progress = false;

   b->cursor = nir_before_instr(&alu->instr);

   for (unsigned i = 0; i < info->num_inputs; i++) {
      nir_deref_instr *deref = nir_src_as_deref(alu->src[i].src);

      if (!deref)
         continue;

      nir_deref_path path;
      nir_deref_path_init(&path, deref, NULL);
      nir_deref_instr *root_deref = path.path[0];
      nir_deref_path_finish(&path);

      if (root_deref->deref_type != nir_deref_type_cast)
         continue;

      nir_ssa_def *ptr =
         nir_iadd(b, root_deref->parent.ssa,
                     nir_build_deref_offset(b, deref, cl_type_size_align));
      nir_instr_rewrite_src(&alu->instr, &alu->src[i].src, nir_src_for_ssa(ptr));
      progress = true;
   }

   return progress;
}

bool
dxil_nir_opt_alu_deref_srcs(nir_shader *nir)
{
   bool progress = false;

   foreach_list_typed(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         continue;
      assert(func->impl);

      bool progress = false;
      nir_builder b;
      nir_builder_init(&b, func->impl);

      nir_foreach_block(block, func->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_alu)
               continue;

            nir_alu_instr *alu = nir_instr_as_alu(instr);
            progress |= lower_alu_deref_srcs(&b, alu);
         }
      }
   }

   return progress;
}

static nir_ssa_def *
memcpy_load_deref_elem(nir_builder *b, nir_deref_instr *parent,
                       nir_ssa_def *index)
{
   nir_deref_instr *deref;

   index = nir_i2i(b, index, nir_dest_bit_size(parent->dest));
   assert(parent->deref_type == nir_deref_type_cast);
   deref = nir_build_deref_ptr_as_array(b, parent, index);

   return nir_load_deref(b, deref);
}

static void
memcpy_store_deref_elem(nir_builder *b, nir_deref_instr *parent,
                        nir_ssa_def *index, nir_ssa_def *value)
{
   nir_deref_instr *deref;

   index = nir_i2i(b, index, nir_dest_bit_size(parent->dest));
   assert(parent->deref_type == nir_deref_type_cast);
   deref = nir_build_deref_ptr_as_array(b, parent, index);
   nir_store_deref(b, deref, value, 1);
}

static bool
lower_memcpy_deref(nir_builder *b, nir_intrinsic_instr *intr)
{
   nir_deref_instr *dst_deref = nir_src_as_deref(intr->src[0]);
   nir_deref_instr *src_deref = nir_src_as_deref(intr->src[1]);
   assert(intr->src[2].is_ssa);
   nir_ssa_def *num_bytes = intr->src[2].ssa;

   assert(dst_deref && src_deref);

   b->cursor = nir_after_instr(&intr->instr);

   dst_deref = nir_build_deref_cast(b, &dst_deref->dest.ssa, dst_deref->modes,
                                       glsl_uint8_t_type(), 1);
   src_deref = nir_build_deref_cast(b, &src_deref->dest.ssa, src_deref->modes,
                                       glsl_uint8_t_type(), 1);

   /*
    * We want to avoid 64b instructions, so let's assume we'll always be
    * passed a value that fits in a 32b type and truncate the 64b value.
    */
   num_bytes = nir_u2u32(b, num_bytes);

   nir_variable *loop_index_var =
     nir_local_variable_create(b->impl, glsl_uint_type(), "loop_index");
   nir_deref_instr *loop_index_deref = nir_build_deref_var(b, loop_index_var);
   nir_store_deref(b, loop_index_deref, nir_imm_int(b, 0), 1);

   nir_loop *loop = nir_push_loop(b);
   nir_ssa_def *loop_index = nir_load_deref(b, loop_index_deref);
   nir_ssa_def *cmp = nir_ige(b, loop_index, num_bytes);
   nir_if *loop_check = nir_push_if(b, cmp);
   nir_jump(b, nir_jump_break);
   nir_pop_if(b, loop_check);
   nir_ssa_def *val = memcpy_load_deref_elem(b, src_deref, loop_index);
   memcpy_store_deref_elem(b, dst_deref, loop_index, val);
   nir_store_deref(b, loop_index_deref, nir_iadd_imm(b, loop_index, 1), 1);
   nir_pop_loop(b, loop);
   nir_instr_remove(&intr->instr);
   return true;
}

bool
dxil_nir_lower_memcpy_deref(nir_shader *nir)
{
   bool progress = false;

   foreach_list_typed(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         continue;
      assert(func->impl);

      nir_builder b;
      nir_builder_init(&b, func->impl);

      nir_foreach_block(block, func->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

            if (intr->intrinsic == nir_intrinsic_memcpy_deref)
               progress |= lower_memcpy_deref(&b, intr);
         }
      }
   }

   return progress;
}

static void
cast_phi(nir_builder *b, nir_phi_instr *phi, unsigned new_bit_size)
{
   nir_phi_instr *lowered = nir_phi_instr_create(b->shader);
   int num_components = 0;
   int old_bit_size = phi->dest.ssa.bit_size;

   nir_op upcast_op = nir_type_conversion_op(nir_type_uint | old_bit_size,
                                             nir_type_uint | new_bit_size,
                                             nir_rounding_mode_undef);
   nir_op downcast_op = nir_type_conversion_op(nir_type_uint | new_bit_size,
                                               nir_type_uint | old_bit_size,
                                               nir_rounding_mode_undef);

   nir_foreach_phi_src(src, phi) {
      assert(num_components == 0 || num_components == src->src.ssa->num_components);
      num_components = src->src.ssa->num_components;

      b->cursor = nir_after_instr_and_phis(src->src.ssa->parent_instr);

      nir_ssa_def *cast = nir_build_alu(b, upcast_op, src->src.ssa, NULL, NULL, NULL);
      nir_phi_instr_add_src(lowered, src->pred, nir_src_for_ssa(cast));
   }

   nir_ssa_dest_init(&lowered->instr, &lowered->dest,
                     num_components, new_bit_size, NULL);

   b->cursor = nir_before_instr(&phi->instr);
   nir_builder_instr_insert(b, &lowered->instr);

   b->cursor = nir_after_phis(nir_cursor_current_block(b->cursor));
   nir_ssa_def *result = nir_build_alu(b, downcast_op, &lowered->dest.ssa, NULL, NULL, NULL);

   nir_ssa_def_rewrite_uses(&phi->dest.ssa, result);
   nir_instr_remove(&phi->instr);
}

static bool
upcast_phi_impl(nir_function_impl *impl, unsigned min_bit_size)
{
   nir_builder b;
   nir_builder_init(&b, impl);
   bool progress = false;

   nir_foreach_block_reverse(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_phi)
            continue;

         nir_phi_instr *phi = nir_instr_as_phi(instr);
         assert(phi->dest.is_ssa);

         if (phi->dest.ssa.bit_size == 1 ||
             phi->dest.ssa.bit_size >= min_bit_size)
            continue;

         cast_phi(&b, phi, min_bit_size);
         progress = true;
      }
   }

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);
   } else {
      nir_metadata_preserve(impl, nir_metadata_all);
   }

   return progress;
}

bool
dxil_nir_lower_upcast_phis(nir_shader *shader, unsigned min_bit_size)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= upcast_phi_impl(function->impl, min_bit_size);
   }

   return progress;
}

struct dxil_nir_split_clip_cull_distance_params {
   nir_variable *new_var;
   nir_shader *shader;
};

/* In GLSL and SPIR-V, clip and cull distance are arrays of floats (with a limit of 8).
 * In DXIL, clip and cull distances are up to 2 float4s combined.
 * Coming from GLSL, we can request this 2 float4 format, but coming from SPIR-V,
 * we can't, and have to accept a "compact" array of scalar floats.
 *
 * To help emitting a valid input signature for this case, split the variables so that they
 * match what we need to put in the signature (e.g. { float clip[4]; float clip1; float cull[3]; })
 */
static bool
dxil_nir_split_clip_cull_distance_instr(nir_builder *b,
                                        nir_instr *instr,
                                        void *cb_data)
{
   struct dxil_nir_split_clip_cull_distance_params *params = cb_data;
   nir_variable *new_var = params->new_var;

   if (instr->type != nir_instr_type_deref)
      return false;

   nir_deref_instr *deref = nir_instr_as_deref(instr);
   nir_variable *var = nir_deref_instr_get_variable(deref);
   if (!var ||
       var->data.location < VARYING_SLOT_CLIP_DIST0 ||
       var->data.location > VARYING_SLOT_CULL_DIST1 ||
       !var->data.compact)
      return false;

   /* The location should only be inside clip distance, because clip
    * and cull should've been merged by nir_lower_clip_cull_distance_arrays()
    */
   assert(var->data.location == VARYING_SLOT_CLIP_DIST0 ||
          var->data.location == VARYING_SLOT_CLIP_DIST1);

   /* The deref chain to the clip/cull variables should be simple, just the
    * var and an array with a constant index, otherwise more lowering/optimization
    * might be needed before this pass, e.g. copy prop, lower_io_to_temporaries,
    * split_var_copies, and/or lower_var_copies
    */
   assert(deref->deref_type == nir_deref_type_var ||
          deref->deref_type == nir_deref_type_array);

   b->cursor = nir_before_instr(instr);
   if (!new_var) {
      /* Update lengths for new and old vars */
      int old_length = glsl_array_size(var->type);
      int new_length = (old_length + var->data.location_frac) - 4;
      old_length -= new_length;

      /* The existing variable fits in the float4 */
      if (new_length <= 0)
         return false;

      new_var = nir_variable_clone(var, params->shader);
      nir_shader_add_variable(params->shader, new_var);
      assert(glsl_get_base_type(glsl_get_array_element(var->type)) == GLSL_TYPE_FLOAT);
      var->type = glsl_array_type(glsl_float_type(), old_length, 0);
      new_var->type = glsl_array_type(glsl_float_type(), new_length, 0);
      new_var->data.location++;
      new_var->data.location_frac = 0;
      params->new_var = new_var;
   }

   /* Update the type for derefs of the old var */
   if (deref->deref_type == nir_deref_type_var) {
      deref->type = var->type;
      return false;
   }

   nir_const_value *index = nir_src_as_const_value(deref->arr.index);
   assert(index);

   /* Treat this array as a vector starting at the component index in location_frac,
    * so if location_frac is 1 and index is 0, then it's accessing the 'y' component
    * of the vector. If index + location_frac is >= 4, there's no component there,
    * so we need to add a new variable and adjust the index.
    */
   unsigned total_index = index->u32 + var->data.location_frac;
   if (total_index < 4)
      return false;

   nir_deref_instr *new_var_deref = nir_build_deref_var(b, new_var);
   nir_deref_instr *new_array_deref = nir_build_deref_array(b, new_var_deref, nir_imm_int(b, total_index % 4));
   nir_ssa_def_rewrite_uses(&deref->dest.ssa, &new_array_deref->dest.ssa);
   return true;
}

bool
dxil_nir_split_clip_cull_distance(nir_shader *shader)
{
   struct dxil_nir_split_clip_cull_distance_params params = {
      .new_var = NULL,
      .shader = shader,
   };
   nir_shader_instructions_pass(shader,
                                dxil_nir_split_clip_cull_distance_instr,
                                nir_metadata_block_index |
                                nir_metadata_dominance |
                                nir_metadata_loop_analysis,
                                &params);
   return params.new_var != NULL;
}

static bool
dxil_nir_lower_double_math_instr(nir_builder *b,
                                 nir_instr *instr,
                                 UNUSED void *cb_data)
{
   if (instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu = nir_instr_as_alu(instr);

   /* TODO: See if we can apply this explicitly to packs/unpacks that are then
    * used as a double. As-is, if we had an app explicitly do a 64bit integer op,
    * then try to bitcast to double (not expressible in HLSL, but it is in other
    * source languages), this would unpack the integer and repack as a double, when
    * we probably want to just send the bitcast through to the backend.
    */

   b->cursor = nir_before_instr(&alu->instr);

   bool progress = false;
   for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; ++i) {
      if (nir_alu_type_get_base_type(nir_op_infos[alu->op].input_types[i]) == nir_type_float &&
          alu->src[i].src.ssa->bit_size == 64) {
         nir_ssa_def *packed_double = nir_channel(b, alu->src[i].src.ssa, alu->src[i].swizzle[0]);
         nir_ssa_def *unpacked_double = nir_unpack_64_2x32(b, packed_double);
         nir_ssa_def *repacked_double = nir_pack_double_2x32_dxil(b, unpacked_double);
         nir_instr_rewrite_src_ssa(instr, &alu->src[i].src, repacked_double);
         memset(alu->src[i].swizzle, 0, ARRAY_SIZE(alu->src[i].swizzle));
         progress = true;
      }
   }

   if (nir_alu_type_get_base_type(nir_op_infos[alu->op].output_type) == nir_type_float &&
       alu->dest.dest.ssa.bit_size == 64) {
      b->cursor = nir_after_instr(&alu->instr);
      nir_ssa_def *packed_double = &alu->dest.dest.ssa;
      nir_ssa_def *unpacked_double = nir_unpack_double_2x32_dxil(b, packed_double);
      nir_ssa_def *repacked_double = nir_pack_64_2x32(b, unpacked_double);
      nir_ssa_def_rewrite_uses_after(packed_double, repacked_double, unpacked_double->parent_instr);
      progress = true;
   }

   return progress;
}

bool
dxil_nir_lower_double_math(nir_shader *shader)
{
   return nir_shader_instructions_pass(shader,
                                       dxil_nir_lower_double_math_instr,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance |
                                       nir_metadata_loop_analysis,
                                       NULL);
}

typedef struct {
   gl_system_value *values;
   uint32_t count;
} zero_system_values_state;

static bool
lower_system_value_to_zero_filter(const nir_instr* instr, const void* cb_state)
{
   if (instr->type != nir_instr_type_intrinsic) {
      return false;
   }

   nir_intrinsic_instr* intrin = nir_instr_as_intrinsic(instr);

   /* All the intrinsics we care about are loads */
   if (!nir_intrinsic_infos[intrin->intrinsic].has_dest)
      return false;

   assert(intrin->dest.is_ssa);

   zero_system_values_state* state = (zero_system_values_state*)cb_state;
   for (uint32_t i = 0; i < state->count; ++i) {
      gl_system_value value = state->values[i];
      nir_intrinsic_op value_op = nir_intrinsic_from_system_value(value);

      if (intrin->intrinsic == value_op) {
         return true;
      } else if (intrin->intrinsic == nir_intrinsic_load_deref) {
         nir_deref_instr* deref = nir_src_as_deref(intrin->src[0]);
         if (!nir_deref_mode_is(deref, nir_var_system_value))
            return false;

         nir_variable* var = deref->var;
         if (var->data.location == value) {
            return true;
         }
      }
   }

   return false;
}

static nir_ssa_def*
lower_system_value_to_zero_instr(nir_builder* b, nir_instr* instr, void* _state)
{
   return nir_imm_int(b, 0);
}

bool
dxil_nir_lower_system_values_to_zero(nir_shader* shader,
                                     gl_system_value* system_values,
                                     uint32_t count)
{
   zero_system_values_state state = { system_values, count };
   return nir_shader_lower_instructions(shader,
      lower_system_value_to_zero_filter,
      lower_system_value_to_zero_instr,
      &state);
}

static const struct glsl_type *
get_bare_samplers_for_type(const struct glsl_type *type)
{
   if (glsl_type_is_sampler(type)) {
      if (glsl_sampler_type_is_shadow(type))
         return glsl_bare_shadow_sampler_type();
      else
         return glsl_bare_sampler_type();
   } else if (glsl_type_is_array(type)) {
      return glsl_array_type(
         get_bare_samplers_for_type(glsl_get_array_element(type)),
         glsl_get_length(type),
         0 /*explicit size*/);
   }
   assert(!"Unexpected type");
   return NULL;
}

static bool
redirect_sampler_derefs(struct nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);
   if (!nir_tex_instr_need_sampler(tex))
      return false;

   int sampler_idx = nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);
   if (sampler_idx == -1) {
      /* No derefs, must be using indices */
      struct hash_entry *hash_entry = _mesa_hash_table_u64_search(data, tex->sampler_index);

      /* Already have a bare sampler here */
      if (hash_entry)
         return false;

      nir_variable *typed_sampler = NULL;
      nir_foreach_variable_with_modes(var, b->shader, nir_var_uniform) {
         if (var->data.binding <= tex->sampler_index &&
             var->data.binding + glsl_type_get_sampler_count(var->type) > tex->sampler_index) {
            /* Already have a bare sampler for this binding, add it to the table */
            if (glsl_get_sampler_result_type(glsl_without_array(var->type)) == GLSL_TYPE_VOID) {
               _mesa_hash_table_u64_insert(data, tex->sampler_index, var);
               return false;
            }

            typed_sampler = var;
         }
      }

      /* Clone the typed sampler to a bare sampler and we're done */
      assert(typed_sampler);
      nir_variable *bare_sampler = nir_variable_clone(typed_sampler, b->shader);
      bare_sampler->type = get_bare_samplers_for_type(typed_sampler->type);
      nir_shader_add_variable(b->shader, bare_sampler);
      _mesa_hash_table_u64_insert(data, tex->sampler_index, bare_sampler);
      return true;
   }

   /* Using derefs, means we have to rewrite the deref chain in addition to cloning */
   nir_deref_instr *final_deref = nir_src_as_deref(tex->src[sampler_idx].src);
   nir_deref_path path;
   nir_deref_path_init(&path, final_deref, NULL);

   nir_deref_instr *old_tail = path.path[0];
   assert(old_tail->deref_type == nir_deref_type_var);
   nir_variable *old_var = old_tail->var;
   if (glsl_get_sampler_result_type(glsl_without_array(old_var->type)) == GLSL_TYPE_VOID) {
      nir_deref_path_finish(&path);
      return false;
   }

   struct hash_entry *hash_entry = _mesa_hash_table_u64_search(data, old_var->data.binding);
   nir_variable *new_var;
   if (hash_entry) {
      new_var = hash_entry->data;
   } else {
      new_var = nir_variable_clone(old_var, b->shader);
      new_var->type = get_bare_samplers_for_type(old_var->type);
      nir_shader_add_variable(b->shader, new_var);
      _mesa_hash_table_u64_insert(data, old_var->data.binding, new_var);
   }

   b->cursor = nir_after_instr(&old_tail->instr);
   nir_deref_instr *new_tail = nir_build_deref_var(b, new_var);

   for (unsigned i = 1; path.path[i]; ++i) {
      b->cursor = nir_after_instr(&path.path[i]->instr);
      new_tail = nir_build_deref_follower(b, new_tail, path.path[i]);
   }

   nir_deref_path_finish(&path);
   nir_instr_rewrite_src_ssa(&tex->instr, &tex->src[sampler_idx].src, &new_tail->dest.ssa);

   return true;
}

bool
dxil_nir_create_bare_samplers(nir_shader *nir)
{
   struct hash_table_u64 *sampler_to_bare = _mesa_hash_table_u64_create(NULL);

   bool progress = nir_shader_instructions_pass(nir, redirect_sampler_derefs,
      nir_metadata_block_index | nir_metadata_dominance | nir_metadata_loop_analysis, sampler_to_bare);

   _mesa_hash_table_u64_destroy(sampler_to_bare);
   return progress;
}


/* Comparison function to sort io values so that first come normal varyings,
 * then system values, and then system generated values.
 */
static int
variable_location_cmp(const nir_variable* a, const nir_variable* b)
{
   // Sort by driver_location, location, then index
   return a->data.driver_location != b->data.driver_location ?
            a->data.driver_location - b->data.driver_location : 
            a->data.location !=  b->data.location ?
               a->data.location - b->data.location :
               a->data.index - b->data.index;
}

/* Order varyings according to driver location */
uint64_t
dxil_sort_by_driver_location(nir_shader* s, nir_variable_mode modes)
{
   nir_sort_variables_with_modes(s, variable_location_cmp, modes);

   uint64_t result = 0;
   nir_foreach_variable_with_modes(var, s, modes) {
      result |= 1ull << var->data.location;
   }
   return result;
}

/* Sort PS outputs so that color outputs come first */
void
dxil_sort_ps_outputs(nir_shader* s)
{
   nir_foreach_variable_with_modes_safe(var, s, nir_var_shader_out) {
      /* We use the driver_location here to avoid introducing a new
       * struct or member variable here. The true, updated driver location
       * will be written below, after sorting */
      switch (var->data.location) {
      case FRAG_RESULT_DEPTH:
         var->data.driver_location = 1;
         break;
      case FRAG_RESULT_STENCIL:
         var->data.driver_location = 2;
         break;
      case FRAG_RESULT_SAMPLE_MASK:
         var->data.driver_location = 3;
         break;
      default:
         var->data.driver_location = 0;
      }
   }

   nir_sort_variables_with_modes(s, variable_location_cmp,
                                 nir_var_shader_out);

   unsigned driver_loc = 0;
   nir_foreach_variable_with_modes(var, s, nir_var_shader_out) {
      var->data.driver_location = driver_loc++;
   }
}

/* Order between stage values so that normal varyings come first,
 * then sysvalues and then system generated values.
 */
uint64_t
dxil_reassign_driver_locations(nir_shader* s, nir_variable_mode modes,
   uint64_t other_stage_mask)
{
   nir_foreach_variable_with_modes_safe(var, s, modes) {
      /* We use the driver_location here to avoid introducing a new
       * struct or member variable here. The true, updated driver location
       * will be written below, after sorting */
      var->data.driver_location = nir_var_to_dxil_sysvalue_type(var, other_stage_mask);
   }

   nir_sort_variables_with_modes(s, variable_location_cmp, modes);

   uint64_t result = 0;
   unsigned driver_loc = 0;
   nir_foreach_variable_with_modes(var, s, modes) {
      result |= 1ull << var->data.location;
      var->data.driver_location = driver_loc++;
   }
   return result;
}
