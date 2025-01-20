/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asahi/genxml/agx_pack.h"
#include "agx_compile.h"
#include "libagx_dgc.h"
#include "shader_enums.h"

static inline unsigned
agx_usc_size(unsigned num_reg_bindings)
{
   STATIC_ASSERT(AGX_USC_UNIFORM_HIGH_LENGTH == AGX_USC_UNIFORM_LENGTH);
   STATIC_ASSERT(AGX_USC_TEXTURE_LENGTH == AGX_USC_UNIFORM_LENGTH);
   STATIC_ASSERT(AGX_USC_SAMPLER_LENGTH == AGX_USC_UNIFORM_LENGTH);

   size_t size = AGX_USC_UNIFORM_LENGTH * num_reg_bindings;

   size += AGX_USC_SHARED_LENGTH;
   size += AGX_USC_SHADER_LENGTH;
   size += AGX_USC_REGISTERS_LENGTH;
   size += MAX2(AGX_USC_NO_PRESHADER_LENGTH, AGX_USC_PRESHADER_LENGTH);
   size += AGX_USC_FRAGMENT_PROPERTIES_LENGTH;

   return size;
}

static void
agx_usc_shared_none(struct agx_usc_builder *b)
{
   agx_usc_pack(b, SHARED, cfg) {
      cfg.layout = AGX_SHARED_LAYOUT_VERTEX_COMPUTE;
      cfg.bytes_per_threadgroup = 65536;
   }
}

static inline void
agx_usc_shared(struct agx_usc_builder *b, uint16_t local_size,
               uint16_t imageblock_stride, unsigned variable_shared_mem)
{
   if (imageblock_stride) {
      assert(local_size == 0 && "we don't handle this interaction");
      assert(variable_shared_mem == 0 && "we don't handle this interaction");

      agx_usc_pack(b, SHARED, cfg) {
         cfg.layout = AGX_SHARED_LAYOUT_32X32;
         cfg.uses_shared_memory = true;
         cfg.sample_count = 1;
         cfg.sample_stride_in_8_bytes = DIV_ROUND_UP(imageblock_stride, 8);
         cfg.bytes_per_threadgroup = cfg.sample_stride_in_8_bytes * 8 * 32 * 32;
      }
   } else if (local_size || variable_shared_mem) {
      unsigned size = local_size + variable_shared_mem;

      agx_usc_pack(b, SHARED, cfg) {
         cfg.layout = AGX_SHARED_LAYOUT_VERTEX_COMPUTE;
         cfg.bytes_per_threadgroup = size > 0 ? size : 65536;
         cfg.uses_shared_memory = size > 0;
      }
   } else {
      agx_usc_shared_none(b);
   }
}

static inline void
agx_usc_immediates(struct agx_usc_builder *b, const struct agx_rodata *ro,
                   uint64_t base_addr)
{
   for (unsigned range = 0; range < DIV_ROUND_UP(ro->size_16, 64); ++range) {
      unsigned offset = 64 * range;
      assert(offset < ro->size_16);

      agx_usc_uniform(b, ro->base_uniform + offset,
                      MIN2(64, ro->size_16 - offset),
                      base_addr + ro->offset + (offset * 2));
   }
}

static void
agx_usc_shared_non_fragment(struct agx_usc_builder *b,
                            const struct agx_shader_info *info,
                            unsigned variable_shared_mem)
{
   if (info->stage != PIPE_SHADER_FRAGMENT) {
      agx_usc_shared(b, info->local_size, info->imageblock_stride,
                     variable_shared_mem);
   }
}
