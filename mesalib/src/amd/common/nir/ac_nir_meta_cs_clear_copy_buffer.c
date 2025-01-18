/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir_meta.h"
#include "ac_nir_helpers.h"
#include "nir_builder.h"
#include "util/helpers.h"

/* This is regular load_ssbo with special handling for sparse buffers. Normally, sparse buffer
 * loads return 0 for all components if a sparse load starts on a non-resident page, crosses
 * the page boundary, and ends on a resident page. For copy_buffer, we want it to return 0 only
 * for the portion of the load that's non-resident, and load values for the portion that's
 * resident. The workaround is to scalarize such loads and disallow vectorization.
 */
static nir_def *
load_ssbo_sparse(nir_builder *b, unsigned num_components, unsigned bit_size, nir_def *buf,
                 nir_def *offset, struct _nir_load_ssbo_indices params, bool sparse)
{
   if (sparse && num_components > 1) {
      nir_def *vec[NIR_MAX_VEC_COMPONENTS];

      /* Split the vector load into scalar loads. */
      for (unsigned i = 0; i < num_components; i++) {
         unsigned elem_offset = i * bit_size / 8;
         unsigned align_offset = (params.align_offset + elem_offset) % params.align_mul;

         vec[i] = nir_load_ssbo(b, 1, bit_size, buf,
                                nir_iadd_imm(b, offset, elem_offset),
                                .access = params.access | ACCESS_KEEP_SCALAR,
                                .align_mul = params.align_mul,
                                .align_offset = align_offset);
      }
      return nir_vec(b, vec, num_components);
   } else {
      return nir_load_ssbo(b, num_components, bit_size, buf, offset,
                           .access = params.access,
                           .align_mul = params.align_mul,
                           .align_offset = params.align_offset);
   }
}

/* Create a compute shader implementing clear_buffer or copy_buffer. */
nir_shader *
ac_create_clear_copy_buffer_cs(struct ac_cs_clear_copy_buffer_options *options,
                               union ac_cs_clear_copy_buffer_key *key)
{
   if (options->print_key) {
      fprintf(stderr, "Internal shader: dma\n");
      fprintf(stderr, "   key.is_clear = %u\n", key->is_clear);
      fprintf(stderr, "   key.dwords_per_thread = %u\n", key->dwords_per_thread);
      fprintf(stderr, "   key.clear_value_size_is_12 = %u\n", key->clear_value_size_is_12);
      fprintf(stderr, "   key.src_is_sparse = %u\n", key->src_is_sparse);
      fprintf(stderr, "   key.src_align_offset = %u\n", key->src_align_offset);
      fprintf(stderr, "   key.dst_align_offset = %u\n", key->dst_align_offset);
      fprintf(stderr, "   key.dst_last_thread_bytes = %u\n", key->dst_last_thread_bytes);
      fprintf(stderr, "   key.dst_single_thread_unaligned = %u\n", key->dst_single_thread_unaligned);
      fprintf(stderr, "\n");
   }

   assert(key->dwords_per_thread && key->dwords_per_thread <= 4);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options->nir_options,
                                                  "clear_copy_buffer_cs");
   b.shader->info.workgroup_size[0] = 64;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.num_ssbos = key->is_clear ? 1 : 2;
   b.shader->info.cs.user_data_components_amd = 0;

   if (key->is_clear) {
      b.shader->info.cs.user_data_components_amd +=
         key->clear_value_size_is_12 ? 3 : key->dwords_per_thread;
   }

   /* Add the last thread ID value. */
   unsigned last_thread_user_data_index = b.shader->info.cs.user_data_components_amd;
   if (key->dst_last_thread_bytes)
      b.shader->info.cs.user_data_components_amd++;

   unsigned start_thread_user_data_index = b.shader->info.cs.user_data_components_amd;
   if (key->has_start_thread)
      b.shader->info.cs.user_data_components_amd++;

   nir_def *thread_id = ac_get_global_ids(&b, 1, 32);

   /* If the clear/copy area is unaligned, we launched extra threads at the beginning to make it
    * aligned. Skip those threads here.
    */
   nir_if *if_positive = NULL;
   if (key->has_start_thread) {
      nir_def *start_thread =
         nir_channel(&b, nir_load_user_data_amd(&b), start_thread_user_data_index);
      thread_id = nir_isub(&b, thread_id, start_thread);
      if_positive = nir_push_if(&b, nir_ige_imm(&b, thread_id, 0));
   }

   /* Convert the global thread ID into bytes. */
   nir_def *offset = nir_imul_imm(&b, thread_id, 4 * key->dwords_per_thread);
   nir_def *value;

   if (key->is_clear) {
      value = nir_trim_vector(&b, nir_load_user_data_amd(&b), key->dwords_per_thread);

      /* We store 4 dwords per thread, but the clear value has 3 dwords. Swizzle it to 4 dwords.
       * Storing 4 dwords per thread is faster even when the ALU cost is worse.
       */
      if (key->clear_value_size_is_12 && key->dwords_per_thread == 4) {
         nir_def *dw_offset = nir_imul_imm(&b, thread_id, key->dwords_per_thread);
         nir_def *vec[3];

         /* Swizzle a 3-component clear value to get a 4-component clear value. Example:
          * 0 1 2 3 | 4 5 6 7 | 8 9 10 11  // dw_offset
          *              |
          *              V
          * 0 1 2 0 | 1 2 0 1 | 2 0 1 2    // clear value component indices
          */
         for (unsigned i = 0; i < 3; i++) {
            vec[i] = nir_vector_extract(&b, value,
                                        nir_umod_imm(&b, nir_iadd_imm(&b, dw_offset, i), 3));
         }
         value = nir_vec4(&b, vec[0], vec[1], vec[2], vec[0]);
      }
   } else {
      /* The hw doesn't support unaligned 32-bit loads, and only supports single-component
       * unaligned 1-byte and 2-byte loads. Luckily, we don't have to use single-component loads
       * because ac_nir_lower_subdword_load converts 1-byte and 2-byte vector loads with unaligned
       * offsets into aligned 32-bit loads by loading an extra dword and then bit-shifting all bits
       * to get the expected result. We only have to set bit_size to 8 or 16 and align_offset to
       * 1..3 to indicate that this is an unaligned load. align_offset is the amount of
       * unalignment.
       *
       * Since the buffer binding offsets are rounded down to the clear/copy size of the thread
       * (i.e. dst_align_offset is subtracted from dst_offset, and src_align_offset is subtracted
       * from src_offset), the stores expect the loaded value to be byte-shifted accordingly.
       * realign_offset is the amount of byte-shifting we have to do.
       */
      assert(util_is_power_of_two_nonzero(key->dwords_per_thread));
      int realign_offset = key->src_align_offset - key->dst_align_offset;
      unsigned alignment = (unsigned)realign_offset % 4 == 0 ? 4 :
                           (unsigned)realign_offset % 2 == 0 ? 2 : 1;
      unsigned bit_size = alignment * 8;
      unsigned num_comps = key->dwords_per_thread * 4 / alignment;
      nir_if *if_first_thread = NULL;
      nir_def *value0 = NULL;

      if (realign_offset < 0) {
         /* if src_align_offset is less than dst_align_offset, realign_offset is
          * negative, which causes the first thread to use a negative buffer offset, which goes
          * entirely out of bounds because the offset is treated as unsigned. Instead of that,
          * the first thread should load from offset 0 by not loading the bytes before
          * the beginning of the buffer.
          */
         if_first_thread = nir_push_if(&b, nir_ieq_imm(&b, thread_id, 0));
         {
            unsigned num_removed_comps = -realign_offset / alignment;
            unsigned num_inbounds_comps = num_comps - num_removed_comps;

            /* Only 8 and 16 component vectors are valid after 5 in NIR. */
            while (!nir_num_components_valid(num_inbounds_comps))
               num_inbounds_comps = util_next_power_of_two(num_inbounds_comps);

            value0 = load_ssbo_sparse(&b, num_inbounds_comps, bit_size, nir_imm_int(&b, 0), offset,
                                      (struct _nir_load_ssbo_indices){
                                         .access = ACCESS_RESTRICT,
                                         .align_mul = 4,
                                         .align_offset = 0
                                      }, key->src_is_sparse);

            /* Add the components that we didn't load as undef. */
            nir_def *comps[16];
            assert(num_comps <= ARRAY_SIZE(comps));
            for (unsigned i = 0; i < num_comps; i++) {
               if (i < num_removed_comps)
                  comps[i] = nir_undef(&b, 1, bit_size);
               else
                  comps[i] = nir_channel(&b, value0, i - num_removed_comps);
            }
            value0 = nir_vec(&b, comps, num_comps);
         }
         nir_push_else(&b, if_first_thread);
      }

      value = load_ssbo_sparse(&b, num_comps, bit_size, nir_imm_int(&b, 0),
                               nir_iadd_imm(&b, offset, realign_offset),
                               (struct _nir_load_ssbo_indices){
                                  .access = ACCESS_RESTRICT,
                                  .align_mul = 4,
                                  .align_offset = (unsigned)realign_offset % 4
                               }, key->src_is_sparse);


      if (if_first_thread) {
         nir_pop_if(&b, if_first_thread);
         value = nir_if_phi(&b, value0, value);
      }

      /* Bitcast the vector to 32 bits. */
      if (value->bit_size != 32)
         value = nir_extract_bits(&b, &value, 1, 0, key->dwords_per_thread, 32);
   }

   nir_def *dst_buf = nir_imm_int(&b, !key->is_clear);
   nir_if *if_first_thread = NULL, *if_last_thread = NULL;

   if (!key->dst_single_thread_unaligned) {
      /* dst_align_offset means how many bytes the first thread should skip because the offset of
       * the buffer binding is rounded down to the clear/copy size of thread, causing the bytes
       * before dst_align_offset to be writable. Above we used realign_offset to byte-shift
       * the value to compensate for the rounded-down offset, so that all stores are dword stores
       * regardless of the offset/size alignment except that the first thread shouldn't store
       * the first dst_align_offset bytes, and the last thread should only store the first
       * dst_last_thread_bytes. In both cases, there is a dword that must be only partially
       * written by splitting it into 8-bit and 16-bit stores.
       */
      if (key->dst_align_offset) {
          if_first_thread = nir_push_if(&b, nir_ieq_imm(&b, thread_id, 0));
          {
             unsigned local_offset = key->dst_align_offset;
             nir_def *first_dword = nir_channel(&b, value, local_offset / 4);

             if (local_offset % 2 == 1) {
                nir_store_ssbo(&b, nir_channel(&b, nir_unpack_32_4x8(&b, first_dword), local_offset % 4),
                               dst_buf, nir_iadd_imm_nuw(&b, offset, local_offset),
                               .access = ACCESS_RESTRICT);
                local_offset++;
             }

             if (local_offset % 4 == 2) {
                nir_store_ssbo(&b, nir_unpack_32_2x16_split_y(&b, first_dword), dst_buf,
                               nir_iadd_imm_nuw(&b, offset, local_offset),
                               .access = ACCESS_RESTRICT);
                local_offset += 2;
             }

             assert(local_offset % 4 == 0);
             unsigned num_dw_remaining = key->dwords_per_thread - local_offset / 4;

             if (num_dw_remaining) {
                nir_def *dwords =
                   nir_channels(&b, value, BITFIELD_RANGE(local_offset / 4, num_dw_remaining));

                nir_store_ssbo(&b, dwords, dst_buf, nir_iadd_imm_nuw(&b, offset, local_offset),
                               .access = ACCESS_RESTRICT);
             }
          }
          nir_push_else(&b, if_first_thread);
      }

      if (key->dst_last_thread_bytes) {
         nir_def *last_thread_id =
            nir_channel(&b, nir_load_user_data_amd(&b), last_thread_user_data_index);

         if_last_thread = nir_push_if(&b, nir_ieq(&b, thread_id, last_thread_id));
         {
            unsigned num_dwords = key->dst_last_thread_bytes / 4;
            bool write_short = (key->dst_last_thread_bytes - num_dwords * 4) / 2;
            bool write_byte = key->dst_last_thread_bytes % 2;
            nir_def *last_dword = nir_channel(&b, value, num_dwords);

            if (num_dwords) {
               nir_def *dwords = nir_channels(&b, value, BITFIELD_MASK(num_dwords));
               nir_store_ssbo(&b, dwords, dst_buf, offset, .access = ACCESS_RESTRICT);
            }

            if (write_short) {
               nir_store_ssbo(&b, nir_u2u16(&b, last_dword), dst_buf,
                              nir_iadd_imm_nuw(&b, offset, num_dwords * 4),
                              .access = ACCESS_RESTRICT);
            }

            if (write_byte) {
               nir_store_ssbo(&b, nir_channel(&b, nir_unpack_32_4x8(&b, last_dword), write_short * 2),
                              dst_buf, nir_iadd_imm_nuw(&b, offset, num_dwords * 4 + write_short * 2),
                              .access = ACCESS_RESTRICT);
            }
         }
         nir_push_else(&b, if_last_thread);
      }

      nir_store_ssbo(&b, value, dst_buf, offset, .access = ACCESS_RESTRICT);

      if (if_last_thread)
         nir_pop_if(&b, if_last_thread);
      if (if_first_thread)
         nir_pop_if(&b, if_first_thread);
   } else {
      /* This shader only executes a single thread (tiny copy or clear) and it's unaligned at both
       * the beginning and the end. Walk the individual dwords/words/bytes that should be written
       * to split the store accordingly.
       */
      for (unsigned local_offset = key->dst_align_offset;
           local_offset < key->dst_last_thread_bytes;) {
         unsigned remaining = key->dst_last_thread_bytes - local_offset;
         nir_def *src_dword = nir_channel(&b, value, local_offset / 4);

         if (local_offset % 2 == 1 || remaining == 1) {
            /* 1-byte store. */
            nir_def *src_dword4x8 = nir_unpack_32_4x8(&b, src_dword);
            nir_store_ssbo(&b, nir_channel(&b, src_dword4x8, local_offset % 4), dst_buf,
                           nir_iadd_imm_nuw(&b, offset, local_offset), .access = ACCESS_RESTRICT);
            local_offset++;
         } else if (local_offset % 4 == 2 || remaining == 2 || remaining == 3) {
            /* 2-byte store. */
            nir_def *src_dword2x16 = nir_unpack_32_2x16(&b, src_dword);
            nir_store_ssbo(&b, nir_channel(&b, src_dword2x16, (local_offset / 2) % 2), dst_buf,
                           nir_iadd_imm_nuw(&b, offset, local_offset), .access = ACCESS_RESTRICT);
            local_offset += 2;
         } else {
            /* 1-N dwords. */
            unsigned dw_size = remaining / 4;
            assert(dw_size);
            assert(local_offset % 4 == 0);

            nir_store_ssbo(&b, nir_channels(&b, value, BITFIELD_RANGE(local_offset / 4, dw_size)),
                           dst_buf, nir_iadd_imm_nuw(&b, offset, local_offset),
                           .access = ACCESS_RESTRICT);
            local_offset += dw_size * 4;
         }
      }
   }

   if (key->has_start_thread)
      nir_pop_if(&b, if_positive);

   return b.shader;
}

bool
ac_prepare_cs_clear_copy_buffer(const struct ac_cs_clear_copy_buffer_options *options,
                                const struct ac_cs_clear_copy_buffer_info *info,
                                struct ac_cs_clear_copy_buffer_dispatch *out)
{
   bool is_copy = info->clear_value_size == 0;

   memset(out, 0, sizeof(*out));

   /* Expand 1-byte and 2-byte clear values to a dword. */
   int clear_value_size = info->clear_value_size;
   const uint32_t *clear_value = info->clear_value;
   uint32_t tmp_clear_value;

   if (!is_copy) {
      if (util_lower_clearsize_to_dword(clear_value, &clear_value_size, &tmp_clear_value))
         clear_value = &tmp_clear_value;

      assert(clear_value_size % 4 == 0);
   }

   /* This doesn't fail very often because the only possible fallback is CP DMA, which doesn't
    * support the render condition.
    */
   if (options->fail_if_slow && !info->render_condition_enabled && options->info->has_cp_dma &&
       !options->info->cp_sdma_ge_use_system_memory_scope) {
      switch (options->info->gfx_level) {
      /* GFX6-8: CP DMA clears are so slow that we risk getting a GPU timeout. CP DMA copies
       * are also slow but less.
       */
      case GFX6:
         /* Optimal for Tahiti. */
         if (is_copy) {
            if (!info->dst_is_vram || !info->src_is_vram ||
                info->size <= (info->dst_offset % 4 ||
                               (info->dst_offset == 4 && info->src_offset % 4) ? 32 * 1024 : 16 * 1024))
               return false;
         } else {
            /* CP DMA only supports dword-aligned clears and small clear values. */
            if (clear_value_size <= 4 && info->dst_offset % 4 == 0 && info->size % 4 == 0 &&
                info->dst_is_vram && info->size <= 1024)
               return false;
         }
         break;

      case GFX7:
         /* Optimal for Hawaii. */
         if (is_copy && info->dst_is_vram && info->src_is_vram && info->size <= 512)
            return false;
         break;

      case GFX8:
         /* Optimal for Tonga. */
         break;

      case GFX9:
         /* Optimal for Vega10. */
         if (is_copy) {
            if (info->src_is_vram) {
               if (info->dst_is_vram) {
                  if (info->size < 4096)
                     return false;
               } else {
                  if (info->size < (info->dst_offset % 64 ? 8192 : 2048))
                     return false;
               }
            } else {
               /* GTT->VRAM and GTT->GTT. */
               return false;
            }
         } else {
            /* CP DMA only supports dword-aligned clears and small clear values. */
            if (clear_value_size <= 4 && info->dst_offset % 4 == 0 && info->size % 4 == 0 &&
                !info->dst_is_vram && (info->size < 2048 || info->size >= 8 << 20 /* 8 MB */))
               return false;
         }
         break;

      case GFX10:
      case GFX10_3:
         /* Optimal for Navi21, Navi10. */
         break;

      case GFX11:
      default:
         /* Optimal for Navi31. */
         if (is_copy && info->size < 1024 && info->dst_offset % 256 && info->dst_is_vram && info->src_is_vram)
            return false;
         break;

      case GFX12:
         unreachable("cp_sdma_ge_use_system_memory_scope should be true, so we should never get here");
      }
   }

   unsigned dwords_per_thread = info->dwords_per_thread;

   /* Determine optimal dwords_per_thread for performance. */
   if (!info->dwords_per_thread) {
      /* This is a good initial value to start with. */
      dwords_per_thread = info->size <= 64 * 1024 ? 2 : 4;

      /* Clearing 4 dwords per thread with a 3-dword clear value is faster with big sizes. */
      if (!is_copy && clear_value_size == 12)
         dwords_per_thread = info->size <= 4096 ? 3 : 4;

      switch (options->info->gfx_level) {
      case GFX6:
         /* Optimal for Tahiti. */
         if (is_copy) {
            if (info->dst_is_vram && info->src_is_vram)
               dwords_per_thread = 2;
         } else {
            if (info->dst_is_vram && clear_value_size != 12)
               dwords_per_thread = info->size <= 128 * 1024 || info->size >= 4 << 20 /* 4MB */ ? 2 : 4;

            if (clear_value_size == 12)
               dwords_per_thread = info->size <= (info->dst_is_vram ? 256 : 128) * 1024 ? 3 : 4;
         }
         break;

      case GFX7:
         /* Optimal for Hawaii. */
         if (is_copy) {
            if (info->dst_is_vram && info->src_is_vram && info->dst_offset % 4 == 0 &&
                info->size >= 8 << 20 /* 8MB */)
               dwords_per_thread = 2;
         } else {
            if (info->dst_is_vram && clear_value_size != 12)
               dwords_per_thread = info->size <= 32 * 1024 ? 2 : 4;

            if (clear_value_size == 12)
               dwords_per_thread = info->size <= 256 * 1024 ? 3 : 4;
         }
         break;

      case GFX8:
         /* Optimal for Tonga. */
         if (is_copy) {
            dwords_per_thread = 2;
         } else {
            if (clear_value_size == 12 && info->size < (2 << 20) /* 2MB */)
               dwords_per_thread = 3;
         }
         break;

      case GFX9:
         /* Optimal for Vega10. */
         if (is_copy && info->src_is_vram && info->dst_is_vram && info->size >= 8 << 20 /* 8 MB */)
            dwords_per_thread = 2;

         if (!info->dst_is_vram)
            dwords_per_thread = 2;
         break;

      case GFX10:
      case GFX10_3:
      case GFX11:
      case GFX12:
         /* Optimal for Gfx12xx, Navi31, Navi21, Navi10. */
         break;

      default:
         break;
      }
   }

   /* dwords_per_thread must be at least the size of the clear value. */
   if (!is_copy)
      dwords_per_thread = MAX2(dwords_per_thread, clear_value_size / 4);

   /* Validate dwords_per_thread. */
   if (dwords_per_thread > 4) {
      assert(!"dwords_per_thread must be <= 4");
      return false; /* invalid value */
   }

   if (clear_value_size > dwords_per_thread * 4) {
      assert(!"clear_value_size must be <= dwords_per_thread");
      return false; /* invalid value */
   }

   if (clear_value_size == 12 && info->dst_offset % 4) {
      assert(!"if clear_value_size == 12, dst_offset must be aligned to 4");
      return false; /* invalid value */
   }

   unsigned dst_align_offset = info->dst_offset % (dwords_per_thread * 4);
   unsigned dst_offset_bound = info->dst_offset - dst_align_offset;
   unsigned src_align_offset = is_copy ? info->src_offset % 4 : 0;
   unsigned num_user_data_terms = 0;

   /* Set the clear value in user data SGPRs. */
   if (!is_copy) {
      assert(clear_value_size >= 4 && clear_value_size <= 16 &&
             (clear_value_size == 12 || util_is_power_of_two_or_zero(clear_value_size)));

      /* Since the clear value may start on an unaligned offset and we just pass user SGPRs
       * to dword stores as-is, we need to byte-shift the clear value to that offset and
       * replicate it because 1 invocation stores up to 4 dwords from user SGPRs regardless of
       * the clear value size.
       */
      num_user_data_terms = clear_value_size == 12 ? 3 : dwords_per_thread;
      unsigned user_data_size = num_user_data_terms * 4;

      memcpy(out->user_data,
             (uint8_t*)clear_value + clear_value_size - dst_align_offset % clear_value_size,
             dst_align_offset % clear_value_size);
      unsigned offset = dst_align_offset % clear_value_size;

      while (offset + clear_value_size <= user_data_size) {
         memcpy((uint8_t*)out->user_data + offset, clear_value, clear_value_size);
         offset += clear_value_size;
      }

      if (offset < user_data_size)
         memcpy((uint8_t*)out->user_data + offset, clear_value, user_data_size - offset);
   }

   out->shader_key.key = 0;

   out->shader_key.is_clear = !is_copy;
   assert(dwords_per_thread && dwords_per_thread <= 4);
   out->shader_key.dwords_per_thread = dwords_per_thread;
   out->shader_key.clear_value_size_is_12 = !is_copy && clear_value_size == 12;
   out->shader_key.src_is_sparse = info->src_is_sparse;
   out->shader_key.src_align_offset = src_align_offset;
   out->shader_key.dst_align_offset = dst_align_offset;

   if ((dst_align_offset + info->size) % 4)
      out->shader_key.dst_last_thread_bytes = (dst_align_offset + info->size) % (dwords_per_thread * 4);

   unsigned num_threads = DIV_ROUND_UP(dst_align_offset + info->size, dwords_per_thread * 4);
   out->shader_key.dst_single_thread_unaligned = num_threads == 1 && dst_align_offset &&
                                                 out->shader_key.dst_last_thread_bytes;

   /* start_thread offsets threads to make sure all non-zero waves start clearing/copying from
    * the beginning a 256B block and clear/copy whole 256B blocks. Clearing/copying a 256B block
    * partially for each wave is inefficient, which happens when dst_offset isn't aligned to 256.
    * Clearing/copying whole 256B blocks per wave isn't possible if dwords_per_thread isn't 2^n.
    */
   unsigned start_thread =
      dst_offset_bound % 256 && util_is_power_of_two_nonzero(dwords_per_thread) ?
            DIV_ROUND_UP(256 - dst_offset_bound % 256, dwords_per_thread * 4) : 0;
   out->shader_key.has_start_thread = start_thread != 0;

   /* Set the value of the last thread ID, so that the shader knows which thread is the last one. */
   if (out->shader_key.dst_last_thread_bytes)
      out->user_data[num_user_data_terms++] = num_threads - 1;
   if (out->shader_key.has_start_thread)
      out->user_data[num_user_data_terms++] = start_thread;

   /* We need to bind whole dwords because of how we compute voffset. The bytes that shouldn't
    * be written are not written by the shader.
    */
   out->ssbo[is_copy].offset = dst_offset_bound;
   out->ssbo[is_copy].size = align(dst_align_offset + info->size, 4);

   if (is_copy) {
      /* Since unaligned copies use 32-bit loads, any dword that's partially covered by the copy
       * range must be fully covered, so that the 32-bit loads succeed.
       */
      out->ssbo[0].offset = info->src_offset - src_align_offset;
      out->ssbo[0].size = align(src_align_offset + info->size, 4);
      assert(out->ssbo[0].offset % 4 == 0 && out->ssbo[0].size % 4 == 0);
   }

   out->num_ssbos = is_copy ? 2 : 1;
   out->workgroup_size = 64;
   out->num_threads = start_thread + num_threads;
   return true;
}
