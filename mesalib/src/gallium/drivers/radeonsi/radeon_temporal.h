/**************************************************************************
 *
 * Copyright 2021 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef _RADEON_TEMPORAL_H
#define _RADEON_TEMPORAL_H

#include "radeon_video.h"
#include "radeon_vcn_enc.h"

#define RENCODE_MAX_TEMPORAL_LAYER_PATTERN_SIZE                                     9

typedef struct rvcn_temporal_layer_pattern_entry_s
{
   unsigned    temporal_id;
   unsigned    reference_index_in_table;
   bool        reference_modification;
   unsigned    frame_num_offset;
   unsigned    poc_offset;
   bool        mark_as_reference;
} rvcn_temporal_layer_pattern_entry_t;

typedef struct rvcn_temporal_layer_pattern_table_s
{
   unsigned    pattern_size;
   rvcn_temporal_layer_pattern_entry_t  pattern_table[RENCODE_MAX_TEMPORAL_LAYER_PATTERN_SIZE];
} rvcn_temporal_layer_pattern_table_t;

static const rvcn_temporal_layer_pattern_table_t  rvcn_temporal_layer_pattern_tables[RENCODE_MAX_NUM_TEMPORAL_LAYERS] =
{
   /* 1 temporal layer */
   {
      2,      /* temporal layer pattern size */
      {
         {
            0,
            0,
            false,
            0,
            0,
            true,
         },
         {
            0,
            0,
            false,
            1,
            2,
            true,
         }
      }
   },
   /* 2 temporal layers */
   {
      3,      /* temporal layer pattern size */
      {
         {
            0,
            0,
            false,
            0,
            0,
            true,
         },
         {
            1,
            0,
            false,
            1,
            2,
            false,
         },
         {
            0,
            0,
            false,
            1,
            4,
            true,
         }
      }
   },
   /* 3 temporal layers */
   {
      5,      /* temporal layer pattern size */
      {
         {
            0,
            0,
            false,
            0,
            0,
            true,
         },
         {
            2,
            0,
            false,
            1,
            2,
            false,
         },
         {
            1,
            0,
            false,
            1,
            4,
            true,
         },
         {
            2,
            2,
            false,
            2,
            6,
            false,
         },
         {
            0,
            0,
            true,
            2,
            8,
            true,
         }
      }
   },
   /* 4 temporal layers */
   {
      9,      /* temporal layer pattern size */
      {
         {
            0,
            0,
            false,
            0,
            0,
            true,
         },
         {
            3,
            0,
            false,
            1,
            2,
            false,
         },
         {
            2,
            0,
            false,
            1,
            4,
            true,
         },
         {
            3,
            2,
            false,
            2,
            6,
            false,
         },
         {
            1,
            0,
            true,
            2,
            8,
            true,
         },
         {
            3,
            4,
            false,
            3,
            10,
            false,
         },
         {
            2,
            4,
            false,
            3,
            12,
            true,
         },
         {
            3,
            6,
            false,
            4,
            14,
            false,
         },
         {
            0,
            0,
            true,
            4,
            16,
            true,
         }
      }
   }
};

#endif // _RADEON_TEMPORAL_H