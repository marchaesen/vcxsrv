/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "agx_tilebuffer.h"
#include <assert.h>
#include "util/bitscan.h"
#include "util/format/u_format.h"
#include "agx_usc.h"
#include "layout.h"

/* Maximum number of bytes per tile on G13G. This may change in future versions
 * of the architecture.
 */
#define MAX_BYTES_PER_TILE (32768 - 1)

/* Maximum bytes per sample in the tilebuffer. Greater allocations require
 * spilling render targets to memory.
 */
#define MAX_BYTES_PER_SAMPLE (64)

/* Minimum tile size in pixels, architectural. */
#define MIN_TILE_SIZE_PX (16 * 16)

/* Select the largest tile size that fits */
static struct agx_tile_size
agx_select_tile_size(unsigned bytes_per_pixel)
{
   /* clang-format off */
   struct agx_tile_size sizes[] = {
      { 32, 32 },
      { 32, 16 },
      { 16, 16 }
   };
   /* clang-format on */

   for (unsigned i = 0; i < ARRAY_SIZE(sizes); ++i) {
      struct agx_tile_size size = sizes[i];

      if ((bytes_per_pixel * size.width * size.height) <= MAX_BYTES_PER_TILE)
         return size;
   }

   unreachable("No supported tile size meets the bytes per pixel requirement");
}

static unsigned
agx_shared_layout_from_tile_size(struct agx_tile_size t)
{
   if (t.width == 32 && t.height == 32)
      return AGX_SHARED_LAYOUT_32X32;
   else if (t.width == 32 && t.height == 16)
      return AGX_SHARED_LAYOUT_32X16;
   else if (t.width == 16 && t.height == 16)
      return AGX_SHARED_LAYOUT_16X16;
   else
      unreachable("Invalid tile size");
}

struct agx_tilebuffer_layout
agx_build_tilebuffer_layout(const enum pipe_format *formats, uint8_t nr_cbufs,
                            uint8_t nr_samples, bool layered)
{
   struct agx_tilebuffer_layout tib = {
      .nr_samples = nr_samples,
      .layered = layered,
   };

   uint32_t offset_B = 0;

   for (unsigned rt = 0; rt < nr_cbufs; ++rt) {
      tib.logical_format[rt] = formats[rt];

      /* If there are gaps in the layout, don't allocate holes. Obscure,
       * PIPE_FORMAT_NONE has a size of 1, not 0.
       */
      if (formats[rt] == PIPE_FORMAT_NONE)
         continue;

      /* Require natural alignment for tilebuffer allocations. This could be
       * optimized, but this shouldn't be a problem in practice.
       */
      enum pipe_format physical_fmt = agx_tilebuffer_physical_format(&tib, rt);
      unsigned align_B = util_format_get_blocksize(physical_fmt);
      assert(util_is_power_of_two_nonzero(align_B) &&
             util_is_power_of_two_nonzero(MAX_BYTES_PER_SAMPLE) &&
             align_B < MAX_BYTES_PER_SAMPLE &&
             "max bytes per sample divisible by alignment");

      offset_B = ALIGN_POT(offset_B, align_B);
      assert(offset_B <= MAX_BYTES_PER_SAMPLE && "loop invariant + above");

      /* Determine the size, if we were to allocate this render target to the
       * tilebuffer as desired.
       */
      unsigned nr = util_format_get_nr_components(physical_fmt) == 1
                       ? util_format_get_nr_components(formats[rt])
                       : 1;

      unsigned size_B = align_B * nr;
      unsigned new_offset_B = offset_B + size_B;

      /* If allocating this render target would exceed any tilebuffer limits, we
       * need to spill it to memory. We continue processing in case there are
       * smaller render targets after that would still fit. Otherwise, we
       * allocate it to the tilebuffer.
       *
       * TODO: Suboptimal, we might be able to reorder render targets to
       * avoid fragmentation causing spilling.
       */
      bool fits = (new_offset_B <= MAX_BYTES_PER_SAMPLE) &&
                  (ALIGN_POT(new_offset_B, 8) * MIN_TILE_SIZE_PX *
                   nr_samples) <= MAX_BYTES_PER_TILE;

      if (fits) {
         tib._offset_B[rt] = offset_B;
         offset_B = new_offset_B;
      } else {
         tib.spilled[rt] = true;
      }
   }

   assert(offset_B <= MAX_BYTES_PER_SAMPLE && "loop invariant");

   /* Multisampling needs a nonempty allocation.
    * XXX: Check this against hw
    */
   if (nr_samples > 1)
      offset_B = MAX2(offset_B, 1);

   tib.sample_size_B = ALIGN_POT(offset_B, 8);

   tib.tile_size = agx_select_tile_size(tib.sample_size_B * nr_samples);

   agx_tilebuffer_pack_usc(&tib);
   return tib;
}

/*
 * With attachmentless rendering in Vulkan, the sample count may not known until
 * draw-time. It's convenient to construct an agx_tilebuffer_layout anyway when
 * beginning rendering, updating the sample count later. This helper allows the
 * driver to set the sample count in a partial agx_tilebuffer_layout.
 *
 * When doing so, we need to rebuild entirely since e.g. tile size might change.
 */
void
agx_tilebuffer_set_samples(struct agx_tilebuffer_layout *tib,
                           unsigned nr_samples)
{
   assert(tib->nr_samples == 0 && "must not be initialized");

   *tib = agx_build_tilebuffer_layout(tib->logical_format,
                                      ARRAY_SIZE(tib->logical_format),
                                      nr_samples, tib->layered);
}

enum pipe_format
agx_tilebuffer_physical_format(struct agx_tilebuffer_layout *tib, unsigned rt)
{
   return ail_pixel_format[tib->logical_format[rt]].renderable;
}

bool
agx_tilebuffer_supports_mask(struct agx_tilebuffer_layout *tib, unsigned rt)
{
   /* We don't bother support masking with spilled render targets. This might be
    * optimized in the future but spilling is so rare anyway it's not worth it.
    */
   if (tib->spilled[rt])
      return false;

   enum pipe_format fmt = agx_tilebuffer_physical_format(tib, rt);
   return ail_isa_format_supports_mask((enum ail_isa_format)fmt);
}

uint32_t
agx_tilebuffer_total_size(struct agx_tilebuffer_layout *tib)
{
   return tib->sample_size_B * tib->nr_samples * tib->tile_size.width *
          tib->tile_size.height;
}

void
agx_tilebuffer_pack_usc(struct agx_tilebuffer_layout *tib)
{
   agx_pack(&tib->usc, USC_SHARED, cfg) {
      if (tib->nr_samples > 0) {
         cfg.uses_shared_memory = true;
         cfg.layout = agx_shared_layout_from_tile_size(tib->tile_size);
         cfg.sample_stride_in_8_bytes = tib->sample_size_B / 8;
         cfg.sample_count = tib->nr_samples;
         cfg.bytes_per_threadgroup = agx_tilebuffer_total_size(tib);
      } else {
         cfg.layout = AGX_SHARED_LAYOUT_VERTEX_COMPUTE;
         cfg.bytes_per_threadgroup = 65536;
      }
   }
}
