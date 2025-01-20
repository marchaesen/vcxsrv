/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "compiler/libcl/libcl.h"
#include "compiler/shader_enums.h"
#include "agx_pack.h"

#define agx_push(ptr, T, cfg)                                                  \
   for (unsigned _loop = 0; _loop < 1;                                         \
        ++_loop, ptr = (GLOBAL void *)(((uintptr_t)ptr) + AGX_##T##_LENGTH))   \
      agx_pack(ptr, T, cfg)

#define agx_push_packed(ptr, src, T)                                           \
   static_assert(sizeof(src) == AGX_##T##_LENGTH);                             \
   memcpy(ptr, &src, sizeof(src));                                             \
   ptr = (GLOBAL void *)(((uintptr_t)ptr) + sizeof(src));

static inline enum agx_index_size
agx_translate_index_size(uint8_t size_B)
{
   /* Index sizes are encoded logarithmically */
   static_assert(__builtin_ctz(1) == AGX_INDEX_SIZE_U8);
   static_assert(__builtin_ctz(2) == AGX_INDEX_SIZE_U16);
   static_assert(__builtin_ctz(4) == AGX_INDEX_SIZE_U32);

   assert((size_B == 1) || (size_B == 2) || (size_B == 4));
   return __builtin_ctz(size_B);
}

static inline unsigned
agx_indices_to_B(unsigned x, enum agx_index_size size)
{
   return x << size;
}

static inline uint8_t
agx_index_size_to_B(enum agx_index_size size)
{
   return agx_indices_to_B(1, size);
}

struct agx_workgroup {
   uint32_t x, y, z;
};

static inline struct agx_workgroup
agx_workgroup(uint32_t x, uint32_t y, uint32_t z)
{
   return (struct agx_workgroup){.x = x, .y = y, .z = z};
}

static inline unsigned
agx_workgroup_threads(struct agx_workgroup wg)
{
   return wg.x * wg.y * wg.z;
}

struct agx_grid {
   enum agx_cdm_mode mode;
   union {
      uint32_t count[3];
      uint64_t ptr;
   };
};

static struct agx_grid
agx_3d(uint32_t x, uint32_t y, uint32_t z)
{
   return (struct agx_grid){.mode = AGX_CDM_MODE_DIRECT, .count = {x, y, z}};
}

static struct agx_grid
agx_1d(uint32_t x)
{
   return agx_3d(x, 1, 1);
}

static struct agx_grid
agx_grid_indirect(uint64_t ptr)
{
   return (struct agx_grid){.mode = AGX_CDM_MODE_INDIRECT_GLOBAL, .ptr = ptr};
}

static struct agx_grid
agx_grid_indirect_local(uint64_t ptr)
{
   return (struct agx_grid){.mode = AGX_CDM_MODE_INDIRECT_LOCAL, .ptr = ptr};
}

static inline bool
agx_is_indirect(struct agx_grid grid)
{
   return grid.mode != AGX_CDM_MODE_DIRECT;
}

enum agx_barrier {
   /* No barrier/cache operations needed */
   AGX_BARRIER_NONE = 0,

   /* Catch-all for all defined barriers. Because we have not yet
    * reverse-engineered the finer details here, this is the only barrier we
    * have....
    */
   AGX_BARRIER_ALL = (1 << 0),
};

struct agx_draw {
   struct agx_grid b;
   uint64_t index_buffer;
   uint32_t index_buffer_range_B;
   uint32_t start;
   uint32_t index_bias;
   uint32_t start_instance;

   /* Primitive restart enabled. If true, implies indexed */
   bool restart;
   enum agx_index_size index_size;

   /* TODO: Optimize this boolean. We can't just check if index_buffer != 0
    * because that breaks with null index buffers.
    */
   bool indexed;
};

static inline struct agx_draw
agx_draw_indirect(uint64_t ptr)
{
   return (struct agx_draw){.b = agx_grid_indirect(ptr)};
}

static inline struct agx_draw
agx_draw_indexed(uint32_t index_count, uint32_t instance_count,
                 uint32_t first_index, uint32_t index_bias,
                 uint32_t first_instance, uint64_t buf, uint32_t range_B,
                 enum agx_index_size index_size, bool restart)
{
   return (struct agx_draw){
      .b = agx_3d(index_count, instance_count, 1),
      .index_buffer = buf,
      .index_buffer_range_B = range_B,
      .start = first_index,
      .index_bias = index_bias,
      .start_instance = first_instance,
      .index_size = index_size,
      .restart = restart,
      .indexed = true,
   };
}

static inline struct agx_draw
agx_draw_indexed_indirect(uint64_t ptr, uint64_t buf, uint32_t range_B,
                          enum agx_index_size index_size, bool restart)
{
   return (struct agx_draw){
      .b = agx_grid_indirect(ptr),
      .index_buffer = buf,
      .index_buffer_range_B = range_B,
      .index_size = index_size,
      .restart = restart,
      .indexed = true,
   };
}

static inline unsigned
agx_draw_index_range_B(struct agx_draw d)
{
   uint range_B = d.index_buffer_range_B;
   if (!agx_is_indirect(d.b))
      range_B -= agx_indices_to_B(d.start, d.index_size);

   return range_B;
}

static inline unsigned
agx_draw_index_range_el(struct agx_draw d)
{
   assert(d.indexed);
   return agx_draw_index_range_B(d) >> d.index_size;
}

static inline uint64_t
agx_draw_index_buffer(struct agx_draw d)
{
   assert(d.indexed);

   uint64_t ib = d.index_buffer;
   if (!agx_is_indirect(d.b))
      ib += agx_indices_to_B(d.start, d.index_size);

   return ib;
}

static bool
agx_direct_draw_overreads_indices(struct agx_draw d)
{
   uint32_t range_B = agx_indices_to_B(d.start + d.b.count[0], d.index_size);
   return range_B > d.index_buffer_range_B;
}

enum agx_chip {
   AGX_CHIP_G13G,
   AGX_CHIP_G13X,
   AGX_CHIP_G14G,
   AGX_CHIP_G14X,
};

static inline GLOBAL uint32_t *
agx_cdm_launch(GLOBAL uint32_t *out, enum agx_chip chip, struct agx_grid grid,
               struct agx_workgroup wg,
               struct agx_cdm_launch_word_0_packed launch, uint32_t usc)
{
#ifndef __OPENCL_VERSION__
   struct agx_cdm_launch_word_0_packed mode;
   agx_pack(&mode, CDM_LAUNCH_WORD_0, cfg) {
      cfg.mode = grid.mode;
   }

   agx_merge(launch, mode, CDM_LAUNCH_WORD_0);
#endif

   agx_push_packed(out, launch, CDM_LAUNCH_WORD_0);

   agx_push(out, CDM_LAUNCH_WORD_1, cfg) {
      cfg.pipeline = usc;
   }

   if (chip == AGX_CHIP_G14X) {
      agx_push(out, CDM_UNK_G14X, cfg)
         ;
   }

   if (agx_is_indirect(grid)) {
      agx_push(out, CDM_INDIRECT, cfg) {
         cfg.address_hi = grid.ptr >> 32;
         cfg.address_lo = grid.ptr;
      }
   } else {
      agx_push(out, CDM_GLOBAL_SIZE, cfg) {
         cfg.x = grid.count[0];
         cfg.y = grid.count[1];
         cfg.z = grid.count[2];
      }
   }

   if (grid.mode != AGX_CDM_MODE_INDIRECT_LOCAL) {
      agx_push(out, CDM_LOCAL_SIZE, cfg) {
         cfg.x = wg.x;
         cfg.y = wg.y;
         cfg.z = wg.z;
      }
   }

   return out;
}

static inline GLOBAL uint32_t *
agx_vdm_draw(GLOBAL uint32_t *out, enum agx_chip chip, struct agx_draw draw,
             enum agx_primitive topology)
{
   uint64_t ib = draw.indexed ? agx_draw_index_buffer(draw) : 0;

   agx_push(out, INDEX_LIST, cfg) {
      cfg.primitive = topology;

      if (agx_is_indirect(draw.b)) {
         cfg.indirect_buffer_present = true;
      } else {
         cfg.instance_count_present = true;
         cfg.index_count_present = true;
         cfg.start_present = true;
      }

      if (draw.indexed) {
         cfg.restart_enable = draw.restart;
         cfg.index_buffer_hi = ib >> 32;
         cfg.index_size = draw.index_size;

         cfg.index_buffer_present = true;
         cfg.index_buffer_size_present = true;
      }
   }

   if (draw.indexed) {
      agx_push(out, INDEX_LIST_BUFFER_LO, cfg) {
         cfg.buffer_lo = ib;
      }
   }

   if (agx_is_indirect(draw.b)) {
      agx_push(out, INDEX_LIST_INDIRECT_BUFFER, cfg) {
         cfg.address_hi = draw.b.ptr >> 32;
         cfg.address_lo = draw.b.ptr & BITFIELD_MASK(32);
      }
   } else {
      agx_push(out, INDEX_LIST_COUNT, cfg) {
         cfg.count = draw.b.count[0];
      }

      agx_push(out, INDEX_LIST_INSTANCES, cfg) {
         cfg.count = draw.b.count[1];
      }

      agx_push(out, INDEX_LIST_START, cfg) {
         cfg.start = draw.indexed ? draw.index_bias : draw.start;
      }
   }

   if (draw.indexed) {
      agx_push(out, INDEX_LIST_BUFFER_SIZE, cfg) {
         cfg.size = align(agx_draw_index_range_B(draw), 4);
      }
   }

   return out;
}

static inline uint32_t
agx_vdm_draw_size(enum agx_chip chip, struct agx_draw draw)
{
   uint32_t size = AGX_INDEX_LIST_LENGTH;

   if (agx_is_indirect(draw.b)) {
      size += AGX_INDEX_LIST_INDIRECT_BUFFER_LENGTH;
   } else {
      size += AGX_INDEX_LIST_COUNT_LENGTH;
      size += AGX_INDEX_LIST_INSTANCES_LENGTH;
      size += AGX_INDEX_LIST_START_LENGTH;
   }

   if (draw.indexed) {
      size += AGX_INDEX_LIST_BUFFER_LO_LENGTH;
      size += AGX_INDEX_LIST_BUFFER_SIZE_LENGTH;
   }

   return size;
}

static inline GLOBAL uint32_t *
agx_cdm_barrier(GLOBAL uint32_t *out, enum agx_chip chip)
{
   agx_push(out, CDM_BARRIER, cfg) {
      cfg.unk_5 = true;
      cfg.unk_6 = true;
      cfg.unk_8 = true;
      // cfg.unk_11 = true;
      // cfg.unk_20 = true;
      // cfg.unk_24 = true; if clustered?
      if (chip == AGX_CHIP_G13X) {
         cfg.unk_4 = true;
         // cfg.unk_26 = true;
      }

      /* With multiple launches in the same CDM stream, we can get cache
       * coherency (? or sync?) issues. We hit this with blits, which need - in
       * between dispatches - need the PBE cache to be flushed and the texture
       * cache to be invalidated. Until we know what bits mean what exactly,
       * let's just set these after every launch to be safe. We can revisit in
       * the future when we figure out what the bits mean.
       */
      cfg.unk_0 = true;
      cfg.unk_1 = true;
      cfg.unk_2 = true;
      cfg.usc_cache_inval = true;
      cfg.unk_4 = true;
      cfg.unk_5 = true;
      cfg.unk_6 = true;
      cfg.unk_7 = true;
      cfg.unk_8 = true;
      cfg.unk_9 = true;
      cfg.unk_10 = true;
      cfg.unk_11 = true;
      cfg.unk_12 = true;
      cfg.unk_13 = true;
      cfg.unk_14 = true;
      cfg.unk_15 = true;
      cfg.unk_16 = true;
      cfg.unk_17 = true;
      cfg.unk_18 = true;
      cfg.unk_19 = true;
   }

   return out;
}

static inline GLOBAL uint32_t *
agx_vdm_return(GLOBAL uint32_t *out)
{
   agx_push(out, VDM_BARRIER, cfg) {
      cfg.returns = true;
   }

   return out;
}

static inline GLOBAL uint32_t *
agx_cdm_return(GLOBAL uint32_t *out)
{
   agx_push(out, CDM_STREAM_RETURN, cfg)
      ;

   return out;
}

static inline GLOBAL uint32_t *
agx_cdm_terminate(GLOBAL uint32_t *out)
{
   agx_push(out, CDM_STREAM_TERMINATE, _)
      ;

   return out;
}

static inline GLOBAL uint32_t *
agx_vdm_terminate(GLOBAL uint32_t *out)
{
   agx_push(out, VDM_STREAM_TERMINATE, _)
      ;

   return out;
}

static inline GLOBAL uint32_t *
agx_cdm_jump(GLOBAL uint32_t *out, uint64_t target)
{
   agx_push(out, CDM_STREAM_LINK, cfg) {
      cfg.target_lo = target & BITFIELD_MASK(32);
      cfg.target_hi = target >> 32;
   }

   return out;
}

static inline GLOBAL uint32_t *
agx_vdm_jump(GLOBAL uint32_t *out, uint64_t target)
{
   agx_push(out, VDM_STREAM_LINK, cfg) {
      cfg.target_lo = target & BITFIELD_MASK(32);
      cfg.target_hi = target >> 32;
   }

   return out;
}

static inline GLOBAL uint32_t *
agx_cs_jump(GLOBAL uint32_t *out, uint64_t target, bool vdm)
{
   return vdm ? agx_vdm_jump(out, target) : agx_cdm_jump(out, target);
}

static inline GLOBAL uint32_t *
agx_cdm_call(GLOBAL uint32_t *out, uint64_t target)
{
   agx_push(out, CDM_STREAM_LINK, cfg) {
      cfg.target_lo = target & BITFIELD_MASK(32);
      cfg.target_hi = target >> 32;
      cfg.with_return = true;
   }

   return out;
}

static inline GLOBAL uint32_t *
agx_vdm_call(GLOBAL uint32_t *out, uint64_t target)
{
   agx_push(out, VDM_STREAM_LINK, cfg) {
      cfg.target_lo = target & BITFIELD_MASK(32);
      cfg.target_hi = target >> 32;
      cfg.with_return = true;
   }

   return out;
}

#define AGX_MAX_LINKED_USC_SIZE                                                \
   (AGX_USC_PRESHADER_LENGTH + AGX_USC_FRAGMENT_PROPERTIES_LENGTH +            \
    AGX_USC_REGISTERS_LENGTH + AGX_USC_SHADER_LENGTH + AGX_USC_SHARED_LENGTH + \
    AGX_USC_SAMPLER_LENGTH + (AGX_USC_UNIFORM_LENGTH * 9))

/*
 * This data structure contains everything needed to dispatch a compute shader
 * (and hopefully eventually graphics?).
 *
 * It is purely flat, no CPU pointers. That makes it suitable for sharing
 * between CPU and GPU. The intention is that it is packed on the CPU side and
 * then consumed on either host or device for dispatching work.
 */
struct agx_shader {
   struct agx_cdm_launch_word_0_packed launch;
   struct agx_workgroup workgroup;

   struct {
      uint32_t size;
      uint8_t data[AGX_MAX_LINKED_USC_SIZE];
   } usc;
};

/* Opaque structure representing a USC program being constructed */
struct agx_usc_builder {
   GLOBAL uint8_t *head;

#ifndef NDEBUG
   uint8_t *begin;
   size_t size;
#endif
} PACKED;

static struct agx_usc_builder
agx_usc_builder(GLOBAL void *out, ASSERTED size_t size)
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

static inline void
agx_usc_words_precomp(GLOBAL uint32_t *out, CONST struct agx_shader *s,
                      uint64_t data, unsigned data_size)
{
   /* Map the data directly as uniforms starting at u0 */
   struct agx_usc_builder b = agx_usc_builder(out, sizeof(s->usc.data));
   agx_usc_uniform(&b, 0, DIV_ROUND_UP(data_size, 2), data);
   agx_usc_push_blob(&b, s->usc.data, s->usc.size);
}

/* This prototype is sufficient for sizing the output */
static inline unsigned
libagx_draw_robust_index_vdm_size()
{
   struct agx_draw draw = agx_draw_indexed(0, 0, 0, 0, 0, 0, 0, 0, 0);
   return agx_vdm_draw_size(0, draw);
}

static inline unsigned
libagx_remap_adj_count(unsigned count, enum mesa_prim prim)
{
   if (prim == MESA_PRIM_TRIANGLE_STRIP_ADJACENCY) {
      /* Spec gives formula for # of primitives in a tri strip adj */
      unsigned c4 = count >= 4 ? count - 4 : 0;
      return 3 * (c4 / 2);
   } else if (prim == MESA_PRIM_LINE_STRIP_ADJACENCY) {
      return 2 * (count >= 3 ? count - 3 : 0);
   } else {
      /* Adjacency lists just drop half the vertices. */
      return count / 2;
   }
}
