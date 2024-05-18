/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asahi/genxml/agx_pack.h"

/* Opaque structure representing a USC program being constructed */
struct agx_usc_builder {
   uint8_t *head;

#ifndef NDEBUG
   uint8_t *begin;
   size_t size;
#endif
};

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

static struct agx_usc_builder
agx_usc_builder(void *out, ASSERTED size_t size)
{
   return (struct agx_usc_builder){
      .head = out,

#ifndef NDEBUG
      .begin = out,
      .size = size,
#endif
   };
}

static bool
agx_usc_builder_validate(struct agx_usc_builder *b, size_t size)
{
#ifndef NDEBUG
   assert(((b->head - b->begin) + size) <= b->size);
#endif

   return true;
}

#define agx_usc_pack(b, struct_name, template)                                 \
   for (bool it =                                                              \
           agx_usc_builder_validate((b), AGX_USC_##struct_name##_LENGTH);      \
        it; it = false, (b)->head += AGX_USC_##struct_name##_LENGTH)           \
      agx_pack((b)->head, USC_##struct_name, template)

#define agx_usc_push_blob(b, blob, length)                                     \
   for (bool it = agx_usc_builder_validate((b), length); it;                   \
        it = false, (b)->head += length)                                       \
      memcpy((b)->head, blob, length);

#define agx_usc_push_packed(b, struct_name, packed)                            \
   agx_usc_push_blob(b, packed.opaque, AGX_USC_##struct_name##_LENGTH);

static void
agx_usc_uniform(struct agx_usc_builder *b, unsigned start_halfs,
                unsigned size_halfs, uint64_t buffer)
{
   assert((start_halfs + size_halfs) <= (1 << 9) && "uniform file overflow");
   assert(size_halfs <= 64 && "caller's responsibility to split");
   assert(size_halfs > 0 && "no empty uniforms");

   if (start_halfs & BITFIELD_BIT(8)) {
      agx_usc_pack(b, UNIFORM_HIGH, cfg) {
         cfg.start_halfs = start_halfs & BITFIELD_MASK(8);
         cfg.size_halfs = size_halfs;
         cfg.buffer = buffer;
      }
   } else {
      agx_usc_pack(b, UNIFORM, cfg) {
         cfg.start_halfs = start_halfs;
         cfg.size_halfs = size_halfs;
         cfg.buffer = buffer;
      }
   }
}

static void
agx_usc_shared_none(struct agx_usc_builder *b)
{
   agx_usc_pack(b, SHARED, cfg) {
      cfg.layout = AGX_SHARED_LAYOUT_VERTEX_COMPUTE;
      cfg.bytes_per_threadgroup = 65536;
   }
}
