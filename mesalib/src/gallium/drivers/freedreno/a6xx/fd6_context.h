/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_CONTEXT_H_
#define FD6_CONTEXT_H_

#include "util/u_upload_mgr.h"

#include "freedreno_context.h"
#include "freedreno_resource.h"

#include "ir3/ir3_shader.h"
#include "ir3/ir3_descriptor.h"

#include "a6xx.xml.h"

struct fd6_lrz_state {
   union {
      struct {
         bool enable : 1;
         bool write : 1;
         bool test : 1;
         bool z_bounds_enable : 1;
         enum fd_lrz_direction direction : 2;

         /* this comes from the fs program state, rather than zsa: */
         enum a6xx_ztest_mode z_mode : 2;
      };
      uint32_t val : 8;
   };
};

/**
 * Bindless descriptor set state for a single descriptor set.
 */
struct fd6_descriptor_set {
   /**
    * Pre-baked descriptor state, updated when image/SSBO is bound
    */
   uint32_t descriptor[IR3_BINDLESS_DESC_COUNT][FDL6_TEX_CONST_DWORDS];

   /**
    * The current seqn of the backed in resource, for detecting if the
    * resource has been rebound
    */
   uint16_t seqno[IR3_BINDLESS_DESC_COUNT];

   /**
    * Current GPU copy of the desciptor set
    */
   struct fd_bo *bo;
};

static inline void
fd6_descriptor_set_invalidate(struct fd6_descriptor_set *set)
{
   if (!set->bo)
      return;
   fd_bo_del(set->bo);
   set->bo = NULL;
}

struct fd6_context {
   struct fd_context base;

   /* Two buffers related to hw binning / visibility stream (VSC).
    * Compared to previous generations
    *   (1) we cannot specify individual buffers per VSC, instead
    *       just a pitch and base address
    *   (2) there is a second smaller buffer.. we also stash
    *       VSC_BIN_SIZE at end of 2nd buffer.
    */
   struct fd_bo *vsc_draw_strm, *vsc_prim_strm;

   unsigned vsc_draw_strm_pitch, vsc_prim_strm_pitch;

   /* The 'control' mem BO is used for various housekeeping
    * functions.  See 'struct fd6_control'
    */
   struct fd_bo *control_mem;
   uint32_t seqno;

   /* pre-baked stateobj for stream-out disable: */
   struct fd_ringbuffer *streamout_disable_stateobj;

   /* pre-baked stateobj for sample-locations disable: */
   struct fd_ringbuffer *sample_locations_disable_stateobj;

   /* pre-baked stateobj for preamble: */
   struct fd_ringbuffer *preamble, *restore;

   /* storage for ctx->last.key: */
   struct ir3_shader_key last_key;

   /* Is there current VS driver-param state set? */
   bool has_dp_state;

   /* cached stateobjs to avoid hashtable lookup when not dirty: */
   const struct fd6_program_state *prog;

   /* We expect to see a finite # of unique border-color entry values,
    * which are a function of the color value and (to a limited degree)
    * the border color format.  These unique border-color entry values
    * get populated into a global border-color buffer, and a hash-table
    * is used to map to the matching entry in the table.
    */
   struct hash_table *bcolor_cache;
   struct fd_bo *bcolor_mem;

   struct util_idalloc tex_ids;
   struct hash_table *tex_cache;
   bool tex_cache_needs_invalidate;

   /**
    * Descriptor sets for 3d shader stages
    */
   struct fd6_descriptor_set descriptor_sets[5] dt;

   /**
    * Descriptor set for compute shaders
    */
   struct fd6_descriptor_set cs_descriptor_set dt;

   struct {
      /* previous lrz state, which is a function of multiple gallium
       * stateobjs, but doesn't necessarily change as frequently:
       */
      struct fd6_lrz_state lrz;
   } last;
};

static inline struct fd6_context *
fd6_context(struct fd_context *ctx)
{
   return (struct fd6_context *)ctx;
}

template <chip CHIP>
struct pipe_context *fd6_context_create(struct pipe_screen *pscreen, void *priv,
                                        unsigned flags);

/* This struct defines the layout of the fd6_context::control buffer: */
struct fd6_control {
   uint32_t seqno; /* seqno for async CP_EVENT_WRITE, etc */
   uint32_t _pad0;
   volatile uint32_t vsc_overflow;
   uint32_t _pad1[5];

   /* scratch space for VPC_SO[i].FLUSH_BASE_LO/HI, start on 32 byte boundary. */
   struct {
      uint32_t offset;
      uint32_t pad[7];
   } flush_base[4];

   uint32_t vsc_state[32];
};

#define control_ptr(fd6_ctx, member)                                           \
   (fd6_ctx)->control_mem, offsetof(struct fd6_control, member), 0, 0

static inline void
emit_marker6(struct fd_ringbuffer *ring, int scratch_idx)
{
   extern int32_t marker_cnt;
   unsigned reg = REG_A6XX_CP_SCRATCH_REG(scratch_idx);
   if (__EMIT_MARKER) {
      OUT_WFI5(ring);
      OUT_PKT4(ring, reg, 1);
      OUT_RING(ring, p_atomic_inc_return(&marker_cnt));
   }
}

struct fd6_vertex_stateobj {
   struct fd_vertex_stateobj base;
   struct fd_ringbuffer *stateobj;
};

static inline struct fd6_vertex_stateobj *
fd6_vertex_stateobj(void *p)
{
   return (struct fd6_vertex_stateobj *)p;
}

#endif /* FD6_CONTEXT_H_ */
