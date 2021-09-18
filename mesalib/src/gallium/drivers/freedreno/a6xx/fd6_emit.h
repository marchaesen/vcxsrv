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

#ifndef FD6_EMIT_H
#define FD6_EMIT_H

#include "pipe/p_context.h"

#include "fd6_context.h"
#include "fd6_format.h"
#include "fd6_program.h"
#include "freedreno_context.h"
#include "ir3_gallium.h"

struct fd_ringbuffer;

/* To collect all the state objects to emit in a single CP_SET_DRAW_STATE
 * packet, the emit tracks a collection of however many state_group's that
 * need to be emit'd.
 */
enum fd6_state_id {
   FD6_GROUP_PROG_CONFIG,
   FD6_GROUP_PROG,
   FD6_GROUP_PROG_BINNING,
   FD6_GROUP_PROG_INTERP,
   FD6_GROUP_PROG_FB_RAST,
   FD6_GROUP_LRZ,
   FD6_GROUP_LRZ_BINNING,
   FD6_GROUP_VTXSTATE,
   FD6_GROUP_VBO,
   FD6_GROUP_CONST,
   FD6_GROUP_VS_DRIVER_PARAMS,
   FD6_GROUP_PRIMITIVE_PARAMS,
   FD6_GROUP_VS_TEX,
   FD6_GROUP_HS_TEX,
   FD6_GROUP_DS_TEX,
   FD6_GROUP_GS_TEX,
   FD6_GROUP_FS_TEX,
   FD6_GROUP_RASTERIZER,
   FD6_GROUP_ZSA,
   FD6_GROUP_BLEND,
   FD6_GROUP_SCISSOR,
   FD6_GROUP_BLEND_COLOR,
   FD6_GROUP_SO,
   FD6_GROUP_IBO,
   FD6_GROUP_NON_GROUP, /* placeholder group for state emit in IB2, keep last */
};

#define ENABLE_ALL                                                             \
   (CP_SET_DRAW_STATE__0_BINNING | CP_SET_DRAW_STATE__0_GMEM |                 \
    CP_SET_DRAW_STATE__0_SYSMEM)
#define ENABLE_DRAW (CP_SET_DRAW_STATE__0_GMEM | CP_SET_DRAW_STATE__0_SYSMEM)

struct fd6_state_group {
   struct fd_ringbuffer *stateobj;
   enum fd6_state_id group_id;
   /* enable_mask controls which states the stateobj is evaluated in,
    * b0 is binning pass b1 and/or b2 is draw pass
    */
   uint32_t enable_mask;
};

/* grouped together emit-state for prog/vertex/state emit: */
struct fd6_emit {
   struct fd_context *ctx;
   const struct fd_vertex_state *vtx;
   const struct pipe_draw_info *info;
	unsigned drawid_offset;
   const struct pipe_draw_indirect_info *indirect;
	const struct pipe_draw_start_count_bias *draw;
   struct ir3_cache_key key;
   enum fd_dirty_3d_state dirty;
   uint32_t dirty_groups;

   uint32_t sprite_coord_enable; /* bitmask */
   bool sprite_coord_mode;
   bool rasterflat;
   bool primitive_restart;
   uint8_t patch_vertices;

   /* cached to avoid repeated lookups: */
   const struct fd6_program_state *prog;

   struct ir3_shader_variant *bs;
   struct ir3_shader_variant *vs;
   struct ir3_shader_variant *hs;
   struct ir3_shader_variant *ds;
   struct ir3_shader_variant *gs;
   struct ir3_shader_variant *fs;

   unsigned streamout_mask;

   struct fd6_state_group groups[32];
   unsigned num_groups;
};

static inline const struct fd6_program_state *
fd6_emit_get_prog(struct fd6_emit *emit)
{
   if (!emit->prog) {
      struct ir3_program_state *s = ir3_cache_lookup(
         emit->ctx->shader_cache, &emit->key, &emit->ctx->debug);
      emit->prog = fd6_program_state(s);
   }
   return emit->prog;
}

static inline void
fd6_emit_take_group(struct fd6_emit *emit, struct fd_ringbuffer *stateobj,
                    enum fd6_state_id group_id, unsigned enable_mask)
{
   debug_assert(emit->num_groups < ARRAY_SIZE(emit->groups));
   struct fd6_state_group *g = &emit->groups[emit->num_groups++];
   g->stateobj = stateobj;
   g->group_id = group_id;
   g->enable_mask = enable_mask;
}

static inline void
fd6_emit_add_group(struct fd6_emit *emit, struct fd_ringbuffer *stateobj,
                   enum fd6_state_id group_id, unsigned enable_mask)
{
   fd6_emit_take_group(emit, fd_ringbuffer_ref(stateobj), group_id,
                       enable_mask);
}

static inline unsigned
fd6_event_write(struct fd_batch *batch, struct fd_ringbuffer *ring,
                enum vgt_event_type evt, bool timestamp)
{
   unsigned seqno = 0;

   fd_reset_wfi(batch);

   OUT_PKT7(ring, CP_EVENT_WRITE, timestamp ? 4 : 1);
   OUT_RING(ring, CP_EVENT_WRITE_0_EVENT(evt));
   if (timestamp) {
      struct fd6_context *fd6_ctx = fd6_context(batch->ctx);
      seqno = ++fd6_ctx->seqno;
      OUT_RELOC(ring, control_ptr(fd6_ctx, seqno)); /* ADDR_LO/HI */
      OUT_RING(ring, seqno);
   }

   return seqno;
}

static inline void
fd6_cache_inv(struct fd_batch *batch, struct fd_ringbuffer *ring)
{
   fd6_event_write(batch, ring, PC_CCU_INVALIDATE_COLOR, false);
   fd6_event_write(batch, ring, PC_CCU_INVALIDATE_DEPTH, false);
   fd6_event_write(batch, ring, CACHE_INVALIDATE, false);
}

static inline void
fd6_cache_flush(struct fd_batch *batch, struct fd_ringbuffer *ring)
{
   struct fd6_context *fd6_ctx = fd6_context(batch->ctx);
   unsigned seqno;

   seqno = fd6_event_write(batch, ring, RB_DONE_TS, true);

   OUT_PKT7(ring, CP_WAIT_REG_MEM, 6);
   OUT_RING(ring, CP_WAIT_REG_MEM_0_FUNCTION(WRITE_EQ) |
                     CP_WAIT_REG_MEM_0_POLL_MEMORY);
   OUT_RELOC(ring, control_ptr(fd6_ctx, seqno));
   OUT_RING(ring, CP_WAIT_REG_MEM_3_REF(seqno));
   OUT_RING(ring, CP_WAIT_REG_MEM_4_MASK(~0));
   OUT_RING(ring, CP_WAIT_REG_MEM_5_DELAY_LOOP_CYCLES(16));

   seqno = fd6_event_write(batch, ring, CACHE_FLUSH_TS, true);

   OUT_PKT7(ring, CP_WAIT_MEM_GTE, 4);
   OUT_RING(ring, CP_WAIT_MEM_GTE_0_RESERVED(0));
   OUT_RELOC(ring, control_ptr(fd6_ctx, seqno));
   OUT_RING(ring, CP_WAIT_MEM_GTE_3_REF(seqno));
}

static inline void
fd6_emit_blit(struct fd_batch *batch, struct fd_ringbuffer *ring)
{
   emit_marker6(ring, 7);
   fd6_event_write(batch, ring, BLIT, false);
   emit_marker6(ring, 7);
}

static inline void
fd6_emit_lrz_flush(struct fd_ringbuffer *ring)
{
   OUT_PKT7(ring, CP_EVENT_WRITE, 1);
   OUT_RING(ring, LRZ_FLUSH);
}

static inline bool
fd6_geom_stage(gl_shader_stage type)
{
   switch (type) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_TESS_EVAL:
   case MESA_SHADER_GEOMETRY:
      return true;
   case MESA_SHADER_FRAGMENT:
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      return false;
   default:
      unreachable("bad shader type");
   }
}

static inline uint32_t
fd6_stage2opcode(gl_shader_stage type)
{
   return fd6_geom_stage(type) ? CP_LOAD_STATE6_GEOM : CP_LOAD_STATE6_FRAG;
}

static inline enum a6xx_state_block
fd6_stage2shadersb(gl_shader_stage type)
{
   switch (type) {
   case MESA_SHADER_VERTEX:
      return SB6_VS_SHADER;
   case MESA_SHADER_TESS_CTRL:
      return SB6_HS_SHADER;
   case MESA_SHADER_TESS_EVAL:
      return SB6_DS_SHADER;
   case MESA_SHADER_GEOMETRY:
      return SB6_GS_SHADER;
   case MESA_SHADER_FRAGMENT:
      return SB6_FS_SHADER;
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      return SB6_CS_SHADER;
   default:
      unreachable("bad shader type");
      return ~0;
   }
}

static inline enum a6xx_tess_spacing
fd6_gl2spacing(enum gl_tess_spacing spacing)
{
   switch (spacing) {
   case TESS_SPACING_EQUAL:
      return TESS_EQUAL;
   case TESS_SPACING_FRACTIONAL_ODD:
      return TESS_FRACTIONAL_ODD;
   case TESS_SPACING_FRACTIONAL_EVEN:
      return TESS_FRACTIONAL_EVEN;
   case TESS_SPACING_UNSPECIFIED:
   default:
      unreachable("spacing must be specified");
   }
}

bool fd6_emit_textures(struct fd_context *ctx, struct fd_ringbuffer *ring,
                       enum pipe_shader_type type,
                       struct fd_texture_stateobj *tex, unsigned bcolor_offset,
                       const struct ir3_shader_variant *v) assert_dt;

void fd6_emit_state(struct fd_ringbuffer *ring,
                    struct fd6_emit *emit) assert_dt;

void fd6_emit_cs_state(struct fd_context *ctx, struct fd_ringbuffer *ring,
                       struct ir3_shader_variant *cp) assert_dt;

void fd6_emit_restore(struct fd_batch *batch, struct fd_ringbuffer *ring);

void fd6_emit_init_screen(struct pipe_screen *pscreen);
void fd6_emit_init(struct pipe_context *pctx);

static inline void
fd6_emit_ib(struct fd_ringbuffer *ring, struct fd_ringbuffer *target)
{
   emit_marker6(ring, 6);
   __OUT_IB5(ring, target);
   emit_marker6(ring, 6);
}

#define WRITE(reg, val)                                                        \
   do {                                                                        \
      OUT_PKT4(ring, reg, 1);                                                  \
      OUT_RING(ring, val);                                                     \
   } while (0)

#endif /* FD6_EMIT_H */
