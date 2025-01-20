/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_binary.c
 *
 * \brief PCO binary-specific functions.
 */

#include "pco.h"
#include "pco_internal.h"
#include "pco_isa.h"
#include "pco_map.h"
#include "util/u_dynarray.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/**
 * \brief Encodes instruction group alignment.
 *
 * \param[in,out] buf Binary buffer.
 * \param[in] igrp PCO instruction group.
 */
static inline unsigned pco_encode_align(struct util_dynarray *buf,
                                        pco_igrp *igrp)
{
   unsigned bytes_encoded = 0;

   if (igrp->enc.len.word_padding) {
      util_dynarray_append(buf, uint8_t, 0xff);
      bytes_encoded += 1;
   }

   if (igrp->enc.len.align_padding) {
      assert(!(igrp->enc.len.align_padding % 2));

      unsigned align_words = igrp->enc.len.align_padding / 2;
      util_dynarray_append(buf, uint8_t, 0xf0 | align_words);
      bytes_encoded += 1;

      for (unsigned u = 0; u < igrp->enc.len.align_padding - 1; ++u) {
         util_dynarray_append(buf, uint8_t, 0xff);
         bytes_encoded += 1;
      }
   }

   return bytes_encoded;
}

/**
 * \brief Encodes a PCO instruction group into binary.
 *
 * \param[in,out] buf Binary buffer.
 * \param[in] igrp PCO instruction group.
 * \return The number of bytes encoded.
 */
static unsigned pco_encode_igrp(struct util_dynarray *buf, pco_igrp *igrp)
{
   uint8_t *ptr;
   unsigned bytes_encoded = 0;

   /* Header. */
   ptr = util_dynarray_grow(buf, uint8_t, igrp->enc.len.hdr);
   bytes_encoded += pco_igrp_hdr_map_encode(ptr, igrp);

   /* Instructions. */
   for (enum pco_op_phase p = _PCO_OP_PHASE_COUNT; p-- > 0;) {
      if (!igrp->enc.len.instrs[p])
         continue;

      ptr = util_dynarray_grow(buf, uint8_t, igrp->enc.len.instrs[p]);
      bytes_encoded += pco_instr_map_encode(ptr, igrp, p);
   }

   /* I/O. */
   if (igrp->enc.len.lower_srcs) {
      ptr = util_dynarray_grow(buf, uint8_t, igrp->enc.len.lower_srcs);
      bytes_encoded += pco_srcs_map_encode(ptr, igrp, false);
   }

   if (igrp->enc.len.upper_srcs) {
      ptr = util_dynarray_grow(buf, uint8_t, igrp->enc.len.upper_srcs);
      bytes_encoded += pco_srcs_map_encode(ptr, igrp, true);
   }

   if (igrp->enc.len.iss) {
      ptr = util_dynarray_grow(buf, uint8_t, igrp->enc.len.iss);
      bytes_encoded += pco_iss_map_encode(ptr, igrp);
   }

   if (igrp->enc.len.dests) {
      ptr = util_dynarray_grow(buf, uint8_t, igrp->enc.len.dests);
      bytes_encoded += pco_dests_map_encode(ptr, igrp);
   }

   /* Word/alignment padding. */
   bytes_encoded += pco_encode_align(buf, igrp);

   assert(bytes_encoded == igrp->enc.len.total);

   return bytes_encoded;
}

/**
 * \brief Encodes a PCO shader into binary.
 *
 * \param[in] ctx PCO compiler context.
 * \param[in,out] shader PCO shader.
 */
void pco_encode_ir(pco_ctx *ctx, pco_shader *shader)
{
   assert(shader->is_grouped);

   util_dynarray_init(&shader->binary.buf, shader);

   unsigned bytes_encoded = 0;
   pco_foreach_func_in_shader (func, shader) {
      func->enc_offset = bytes_encoded;
      pco_foreach_block_in_func (block, func) {
         pco_foreach_igrp_in_block (igrp, block) {
            bytes_encoded += pco_encode_igrp(&shader->binary.buf, igrp);
         }
      }
   }

   if (pco_should_print_binary(shader))
      pco_print_binary(shader, stdout, "after encoding");
}

/**
 * \brief Finalizes a PCO shader binary.
 *
 * \param[in] ctx PCO compiler context.
 * \param[in,out] shader PCO shader.
 */
void pco_shader_finalize(pco_ctx *ctx, pco_shader *shader)
{
   puts("finishme: pco_shader_finalize");

   pco_func *entry = pco_entrypoint(shader);
   shader->data.common.entry_offset = entry->enc_offset;

   if (pco_should_print_binary(shader))
      pco_print_binary(shader, stdout, "after finalizing");
}

/**
 * \brief Returns the size in bytes of a PCO shader binary.
 *
 * \param[in] shader PCO shader.
 * \return The size in bytes of the PCO shader binary.
 */
unsigned pco_shader_binary_size(pco_shader *shader)
{
   return shader->binary.buf.size;
}

/**
 * \brief Returns the PCO shader binary data.
 *
 * \param[in] shader PCO shader.
 * \return The PCO shader binary data.
 */
const void *pco_shader_binary_data(pco_shader *shader)
{
   return shader->binary.buf.data;
}
