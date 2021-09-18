/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
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
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_CONTEXT_H_
#define FD6_CONTEXT_H_

#include "util/u_upload_mgr.h"

#include "freedreno_context.h"
#include "freedreno_resource.h"

#include "ir3/ir3_shader.h"

#include "a6xx.xml.h"

struct fd6_lrz_state {
   bool enable : 1;
   bool write : 1;
   bool test : 1;
   enum fd_lrz_direction direction : 2;

   /* this comes from the fs program state, rather than zsa: */
   enum a6xx_ztest_mode z_mode : 2;
};

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

   struct u_upload_mgr *border_color_uploader;
   struct pipe_resource *border_color_buf;

   /* pre-backed stateobj for stream-out disable: */
   struct fd_ringbuffer *streamout_disable_stateobj;

   /* storage for ctx->last.key: */
   struct ir3_shader_key last_key;

   /* Is there current VS driver-param state set? */
   bool has_dp_state;

   /* number of active samples-passed queries: */
   int samples_passed_queries;

   /* cached stateobjs to avoid hashtable lookup when not dirty: */
   const struct fd6_program_state *prog;

   uint16_t tex_seqno;
   struct hash_table *tex_cache;

   struct {
      /* previous binning/draw lrz state, which is a function of multiple
       * gallium stateobjs, but doesn't necessarily change as frequently:
       */
      struct fd6_lrz_state lrz[2];
   } last;
};

static inline struct fd6_context *
fd6_context(struct fd_context *ctx)
{
   return (struct fd6_context *)ctx;
}

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
