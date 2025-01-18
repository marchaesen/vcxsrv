/*
 * Copyright (C) 2023 Collabora, Ltd.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Louis-Francis Ratté-Boulianne <lfrb@collabora.com>
 */

#include "pan_texture.h"

/* Arm Fixed-Rate Compression (AFRC) is a lossy compression scheme natively
 * implemented in Mali GPUs. AFRC images can only be rendered or textured
 * from. It is currently not possible to do image reads or writes to such
 * resources.
 *
 * AFRC divides the image into an array of fixed-size coding units which are
 * grouped into paging tiles. The size of the coding units (clump size)
 * depends on the image format and the pixel layout (whether it is optimized
 * for 2D locality and rotation, or for scan line order access). The last
 * parameter is the size of the compressed block that can be either 16, 24,
 * or 32 bytes.
 *
 * The compression rate can be calculated by dividing the compressed block
 * size by the uncompressed block size (clump size multiplied by the component
 * size and the number of components).
 */

struct pan_afrc_format_info
panfrost_afrc_get_format_info(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   struct pan_afrc_format_info info = {0};

   /* No AFRC(ZS). */
   if (desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS)
      return info;

   unsigned bpc = 0;
   for (unsigned c = 0; c < desc->nr_channels; c++) {
      if (bpc && bpc != desc->channel[c].size)
         return info;

      bpc = desc->channel[0].size;
   }

   info.bpc = bpc;

   if (desc->colorspace == UTIL_FORMAT_COLORSPACE_YUV) {
      if (desc->layout != UTIL_FORMAT_LAYOUT_SUBSAMPLED)
         info.ichange_fmt = PAN_AFRC_ICHANGE_FORMAT_YUV444;
      else if (util_format_is_subsampled_422(format))
         info.ichange_fmt = PAN_AFRC_ICHANGE_FORMAT_YUV422;
      else
         info.ichange_fmt = PAN_AFRC_ICHANGE_FORMAT_YUV420;
   } else {
      assert(desc->colorspace == UTIL_FORMAT_COLORSPACE_RGB ||
             desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB);
      info.ichange_fmt = PAN_AFRC_ICHANGE_FORMAT_RAW;
   }

   info.num_planes = util_format_get_num_planes(format);
   info.num_comps = util_format_get_nr_components(format);
   return info;
}

bool
panfrost_format_supports_afrc(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   int c = util_format_get_first_non_void_channel(desc->format);

   if (c == -1)
      return false;

   return desc->is_array && desc->channel[c].size == 8;
}

struct panfrost_afrc_block_size {
   unsigned size;          /* Block size in bytes */
   unsigned alignment;     /* Buffer alignment */
   uint64_t modifier_flag; /* Part of the modifier for CU size */
};

#define BLOCK_SIZE(block_size, buffer_alignment)                               \
   {                                                                           \
      .size = block_size, .alignment = buffer_alignment,                       \
      .modifier_flag = AFRC_FORMAT_MOD_CU_SIZE_##block_size,                   \
   }

/* clang-format off */
const struct panfrost_afrc_block_size panfrost_afrc_block_sizes[] = {
   BLOCK_SIZE(16, 1024),
   BLOCK_SIZE(24, 512),
   BLOCK_SIZE(32, 2048),
};
/* clang-format on */

/* Total number of components in a AFRC coding unit */
static unsigned
panfrost_afrc_clump_get_nr_components(enum pipe_format format, bool scan)
{
   const struct util_format_description *desc = util_format_description(format);
   struct pan_block_size clump_sz = panfrost_afrc_clump_size(format, scan);
   return clump_sz.width * clump_sz.height * desc->nr_channels;
}

unsigned
panfrost_afrc_query_rates(enum pipe_format format, unsigned max,
                          uint32_t *rates)
{
   if (!panfrost_format_supports_afrc(format))
      return 0;

   unsigned clump_comps = panfrost_afrc_clump_get_nr_components(format, false);
   unsigned nr_rates = 0;

   /**
    * From EGL_EXT_surface_compression:
    *
    * "For pixel formats with different number of bits per component, the
    * specified fixed-rate compression rate applies to the component with
    * the highest number of bits."
    *
    * We only support formats where all components have the same size for now.
    * Let's just use the first component size for calculation.
    */
   unsigned uncompressed_rate =
      util_format_get_component_bits(format, UTIL_FORMAT_COLORSPACE_RGB, 0);

   for (unsigned i = 0; i < ARRAY_SIZE(panfrost_afrc_block_sizes); ++i) {
      unsigned clump_sz = panfrost_afrc_block_sizes[i].size * 8;
      unsigned rate = clump_sz / clump_comps;

      if (rate >= uncompressed_rate)
         continue;

      if (nr_rates < max)
         rates[nr_rates] = rate;
      nr_rates++;

      if (max > 0 && nr_rates == max)
         break;
   }

   return nr_rates;
}

unsigned
panfrost_afrc_get_modifiers(enum pipe_format format, uint32_t rate,
                            unsigned max, uint64_t *modifiers)
{
   if (!panfrost_format_supports_afrc(format))
      return 0;

   /* For now, the number of components in a clump is always the same no
    * matter the layout for all supported formats */
   unsigned clump_comps = panfrost_afrc_clump_get_nr_components(format, false);
   unsigned count = 0;

   /* FIXME Choose a more sensitive default compression rate? */
   if (rate == PAN_AFRC_RATE_DEFAULT) {
      if (max > 0)
         modifiers[0] = DRM_FORMAT_MOD_ARM_AFRC(AFRC_FORMAT_MOD_CU_SIZE_24);

      if (max > 1)
         modifiers[1] = DRM_FORMAT_MOD_ARM_AFRC(AFRC_FORMAT_MOD_CU_SIZE_24 |
                                                AFRC_FORMAT_MOD_LAYOUT_SCAN);

      return 2;
   }

   for (unsigned i = 0; i < ARRAY_SIZE(panfrost_afrc_block_sizes); ++i) {
      unsigned clump_sz = panfrost_afrc_block_sizes[i].size * 8;
      if (rate == clump_sz / clump_comps) {
         for (unsigned scan = 0; scan < 2; ++scan) {
            if (count < max) {
               modifiers[count] = DRM_FORMAT_MOD_ARM_AFRC(
                  panfrost_afrc_block_sizes[i].modifier_flag |
                  (scan ? AFRC_FORMAT_MOD_LAYOUT_SCAN : 0));
            }
            count++;
         }
      }
   }

   return count;
}

uint32_t
panfrost_afrc_get_rate(enum pipe_format format, uint64_t modifier)
{
   if (!drm_is_afrc(modifier) || !panfrost_format_supports_afrc(format))
      return PAN_AFRC_RATE_NONE;

   bool scan = panfrost_afrc_is_scan(modifier);
   unsigned block_comps = panfrost_afrc_clump_get_nr_components(format, scan);
   uint32_t block_sz = panfrost_afrc_block_size_from_modifier(modifier) * 8;

   return block_sz / block_comps;
}
