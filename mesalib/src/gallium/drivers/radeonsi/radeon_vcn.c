/*
 * Copyright © 2022 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "radeon_vcn.h"

/* vcn unified queue (sq) ib header */
void rvcn_sq_header(struct radeon_cmdbuf *cs,
                    struct rvcn_sq_var *sq,
                    bool enc)
{
   /* vcn ib signature */
   radeon_emit(cs, RADEON_VCN_SIGNATURE_SIZE);
   radeon_emit(cs, RADEON_VCN_SIGNATURE);
   sq->signature_ib_checksum = &cs->current.buf[cs->current.cdw];
   radeon_emit(cs, 0);
   sq->signature_ib_total_size_in_dw = &cs->current.buf[cs->current.cdw];
   radeon_emit(cs, 0);

   /* vcn ib engine info */
   radeon_emit(cs, RADEON_VCN_ENGINE_INFO_SIZE);
   radeon_emit(cs, RADEON_VCN_ENGINE_INFO);
   radeon_emit(cs, enc ? RADEON_VCN_ENGINE_TYPE_ENCODE
                       : RADEON_VCN_ENGINE_TYPE_DECODE);
   sq->engine_ib_size_of_packages = &cs->current.buf[cs->current.cdw];
   radeon_emit(cs, 0);
}

void rvcn_sq_tail(struct radeon_cmdbuf *cs,
                  struct rvcn_sq_var *sq)
{
   uint32_t *end;
   uint32_t size_in_dw;
   uint32_t checksum = 0;

   if (sq->signature_ib_checksum == NULL || sq->signature_ib_total_size_in_dw == NULL ||
       sq->engine_ib_size_of_packages == NULL)
      return;

   end = &cs->current.buf[cs->current.cdw];
   size_in_dw = end - sq->signature_ib_total_size_in_dw - 1;
   *sq->signature_ib_total_size_in_dw = size_in_dw;
   *sq->engine_ib_size_of_packages = size_in_dw * sizeof(uint32_t);

   for (int i = 0; i < size_in_dw; i++)
      checksum += *(sq->signature_ib_checksum + 2 + i);

   *sq->signature_ib_checksum = checksum;
}
