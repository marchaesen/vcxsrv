/*
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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

#ifndef FREEDRENO_CONTEXT_H_
#define FREEDRENO_CONTEXT_H_

#include "pipe/p_context.h"
#include "indices/u_primconvert.h"
#include "util/u_blitter.h"
#include "util/libsync.h"
#include "util/list.h"
#include "util/slab.h"
#include "util/u_string.h"
#include "util/u_trace.h"

#include "freedreno_screen.h"
#include "freedreno_gmem.h"
#include "freedreno_util.h"

#define BORDER_COLOR_UPLOAD_SIZE (2 * PIPE_MAX_SAMPLERS * BORDERCOLOR_SIZE)

struct fd_vertex_stateobj;
struct fd_batch;

struct fd_texture_stateobj {
	struct pipe_sampler_view *textures[PIPE_MAX_SAMPLERS];
	unsigned num_textures;
	unsigned valid_textures;
	struct pipe_sampler_state *samplers[PIPE_MAX_SAMPLERS];
	unsigned num_samplers;
	unsigned valid_samplers;
	/* number of samples per sampler, 2 bits per sampler: */
	uint32_t samples;
};

struct fd_program_stateobj {
	void *vs, *hs, *ds, *gs, *fs;
};

struct fd_constbuf_stateobj {
	struct pipe_constant_buffer cb[PIPE_MAX_CONSTANT_BUFFERS];
	uint32_t enabled_mask;
};

struct fd_shaderbuf_stateobj {
	struct pipe_shader_buffer sb[PIPE_MAX_SHADER_BUFFERS];
	uint32_t enabled_mask;
	uint32_t writable_mask;
};

struct fd_shaderimg_stateobj {
	struct pipe_image_view si[PIPE_MAX_SHADER_IMAGES];
	uint32_t enabled_mask;
};

struct fd_vertexbuf_stateobj {
	struct pipe_vertex_buffer vb[PIPE_MAX_ATTRIBS];
	unsigned count;
	uint32_t enabled_mask;
};

struct fd_vertex_stateobj {
	struct pipe_vertex_element pipe[PIPE_MAX_ATTRIBS];
	unsigned num_elements;
};

struct fd_streamout_stateobj {
	struct pipe_stream_output_target *targets[PIPE_MAX_SO_BUFFERS];
	/* Bitmask of stream that should be reset. */
	unsigned reset;

	unsigned num_targets;
	/* Track offset from vtxcnt for streamout data.  This counter
	 * is just incremented by # of vertices on each draw until
	 * reset or new streamout buffer bound.
	 *
	 * When we eventually have GS, the CPU won't actually know the
	 * number of vertices per draw, so I think we'll have to do
	 * something more clever.
	 */
	unsigned offsets[PIPE_MAX_SO_BUFFERS];
};

#define MAX_GLOBAL_BUFFERS 16
struct fd_global_bindings_stateobj {
	struct pipe_resource *buf[MAX_GLOBAL_BUFFERS];
	uint32_t enabled_mask;
};

/* group together the vertex and vertexbuf state.. for ease of passing
 * around, and because various internal operations (gmem<->mem, etc)
 * need their own vertex state:
 */
struct fd_vertex_state {
	struct fd_vertex_stateobj *vtx;
	struct fd_vertexbuf_stateobj vertexbuf;
};

/* global 3d pipeline dirty state: */
enum fd_dirty_3d_state {
	FD_DIRTY_BLEND       = BIT(0),
	FD_DIRTY_RASTERIZER  = BIT(1),
	FD_DIRTY_ZSA         = BIT(2),
	FD_DIRTY_BLEND_COLOR = BIT(3),
	FD_DIRTY_STENCIL_REF = BIT(4),
	FD_DIRTY_SAMPLE_MASK = BIT(5),
	FD_DIRTY_FRAMEBUFFER = BIT(6),
	FD_DIRTY_STIPPLE     = BIT(7),
	FD_DIRTY_VIEWPORT    = BIT(8),
	FD_DIRTY_VTXSTATE    = BIT(9),
	FD_DIRTY_VTXBUF      = BIT(10),
	FD_DIRTY_MIN_SAMPLES = BIT(11),
	FD_DIRTY_SCISSOR     = BIT(12),
	FD_DIRTY_STREAMOUT   = BIT(13),
	FD_DIRTY_UCP         = BIT(14),
	FD_DIRTY_BLEND_DUAL  = BIT(15),

	/* These are a bit redundent with fd_dirty_shader_state, and possibly
	 * should be removed.  (But OTOH kinda convenient in some places)
	 */
	FD_DIRTY_PROG        = BIT(16),
	FD_DIRTY_CONST       = BIT(17),
	FD_DIRTY_TEX         = BIT(18),
	FD_DIRTY_IMAGE       = BIT(19),
	FD_DIRTY_SSBO        = BIT(20),

	/* only used by a2xx.. possibly can be removed.. */
	FD_DIRTY_TEXSTATE    = BIT(21),

	/* fine grained state changes, for cases where state is not orthogonal
	 * from hw perspective:
	 */
	FD_DIRTY_RASTERIZER_DISCARD = BIT(24),
};

/* per shader-stage dirty state: */
enum fd_dirty_shader_state {
	FD_DIRTY_SHADER_PROG  = BIT(0),
	FD_DIRTY_SHADER_CONST = BIT(1),
	FD_DIRTY_SHADER_TEX   = BIT(2),
	FD_DIRTY_SHADER_SSBO  = BIT(3),
	FD_DIRTY_SHADER_IMAGE = BIT(4),
};

/* Bitmask of stages in rendering that a particular query is active.
 * Queries will be automatically started/stopped (generating additional
 * fd_hw_sample_period's) on entrance/exit from stages that are
 * applicable to the query.
 *
 * NOTE: set the stage to NULL at end of IB to ensure no query is still
 * active.  Things aren't going to work out the way you want if a query
 * is active across IB's (or between tile IB and draw IB)
 */
enum fd_render_stage {
	FD_STAGE_NULL     = 0x00,
	FD_STAGE_DRAW     = 0x01,
	FD_STAGE_CLEAR    = 0x02,
	/* used for driver internal draws (ie. util_blitter_blit()): */
	FD_STAGE_BLIT     = 0x04,
	FD_STAGE_ALL      = 0xff,
};

#define MAX_HW_SAMPLE_PROVIDERS 7
struct fd_hw_sample_provider;
struct fd_hw_sample;

struct fd_context {
	struct pipe_context base;

	struct list_head node;   /* node in screen->context_list */

	/* We currently need to serialize emitting GMEM batches, because of
	 * VSC state access in the context.
	 *
	 * In practice this lock should not be contended, since pipe_context
	 * use should be single threaded.  But it is needed to protect the
	 * case, with batch reordering where a ctxB batch triggers flushing
	 * a ctxA batch
	 */
	simple_mtx_t gmem_lock;

	struct fd_device *dev;
	struct fd_screen *screen;
	struct fd_pipe *pipe;

	struct blitter_context *blitter;
	void *clear_rs_state[2];
	struct primconvert_context *primconvert;

	/* slab for pipe_transfer allocations: */
	struct slab_child_pool transfer_pool;

	/**
	 * query related state:
	 */
	/*@{*/
	/* slabs for fd_hw_sample and fd_hw_sample_period allocations: */
	struct slab_mempool sample_pool;
	struct slab_mempool sample_period_pool;

	/* sample-providers for hw queries: */
	const struct fd_hw_sample_provider *hw_sample_providers[MAX_HW_SAMPLE_PROVIDERS];

	/* list of active queries: */
	struct list_head hw_active_queries;

	/* sample-providers for accumulating hw queries: */
	const struct fd_acc_sample_provider *acc_sample_providers[MAX_HW_SAMPLE_PROVIDERS];

	/* list of active accumulating queries: */
	struct list_head acc_active_queries;
	/*@}*/

	/* Whether we need to walk the acc_active_queries next fd_set_stage() to
	 * update active queries (even if stage doesn't change).
	 */
	bool update_active_queries;

	/* Current state of pctx->set_active_query_state() (i.e. "should drawing
	 * be counted against non-perfcounter queries")
	 */
	bool active_queries;

	/* table with PIPE_PRIM_MAX entries mapping PIPE_PRIM_x to
	 * DI_PT_x value to use for draw initiator.  There are some
	 * slight differences between generation:
	 */
	const uint8_t *primtypes;
	uint32_t primtype_mask;

	/* shaders used by clear, and gmem->mem blits: */
	struct fd_program_stateobj solid_prog; // TODO move to screen?
	struct fd_program_stateobj solid_layered_prog;

	/* shaders used by mem->gmem blits: */
	struct fd_program_stateobj blit_prog[MAX_RENDER_TARGETS]; // TODO move to screen?
	struct fd_program_stateobj blit_z, blit_zs;

	/* Stats/counters:
	 */
	struct {
		uint64_t prims_emitted;
		uint64_t prims_generated;
		uint64_t draw_calls;
		uint64_t batch_total, batch_sysmem, batch_gmem, batch_nondraw, batch_restore;
		uint64_t staging_uploads, shadow_uploads;
		uint64_t vs_regs, hs_regs, ds_regs, gs_regs, fs_regs;
	} stats;

	/* Current batch.. the rule here is that you can deref ctx->batch
	 * in codepaths from pipe_context entrypoints.  But not in code-
	 * paths from fd_batch_flush() (basically, the stuff that gets
	 * called from GMEM code), since in those code-paths the batch
	 * you care about is not necessarily the same as ctx->batch.
	 */
	struct fd_batch *batch;

	/* NULL if there has been rendering since last flush.  Otherwise
	 * keeps a reference to the last fence so we can re-use it rather
	 * than having to flush no-op batch.
	 */
	struct pipe_fence_handle *last_fence;

	/* Fence fd we are told to wait on via ->fence_server_sync() (or -1
	 * if none).  The in-fence is transferred over to the batch on the
	 * next draw/blit/grid.
	 *
	 * The reason for this extra complexity is that apps will typically
	 * do eglWaitSyncKHR()/etc at the beginning of the frame, before the
	 * first draw.  But mesa/st doesn't flush down framebuffer state
	 * change until we hit a draw, so at ->fence_server_sync() time, we
	 * don't yet have the correct batch.  If we created a batch at that
	 * point, it would be the wrong one, and we'd have to flush it pre-
	 * maturely, causing us to stall early in the frame where we could
	 * be building up cmdstream.
	 */
	int in_fence_fd;

	/* track last known reset status globally and per-context to
	 * determine if more resets occurred since then.  If global reset
	 * count increases, it means some other context crashed.  If
	 * per-context reset count increases, it means we crashed the
	 * gpu.
	 */
	uint32_t context_reset_count, global_reset_count;

	/* Context sequence #, used for batch-cache key: */
	uint16_t seqno;

	/* Are we in process of shadowing a resource? Used to detect recursion
	 * in transfer_map, and skip unneeded synchronization.
	 */
	bool in_shadow : 1;

	/* Ie. in blit situation where we no longer care about previous framebuffer
	 * contents.  Main point is to eliminate blits from fd_try_shadow_resource().
	 * For example, in case of texture upload + gen-mipmaps.
	 */
	bool in_discard_blit : 1;

	/* points to either scissor or disabled_scissor depending on rast state: */
	struct pipe_scissor_state *current_scissor;

	struct pipe_scissor_state scissor;

	/* we don't have a disable/enable bit for scissor, so instead we keep
	 * a disabled-scissor state which matches the entire bound framebuffer
	 * and use that when scissor is not enabled.
	 */
	struct pipe_scissor_state disabled_scissor;

	/* Per vsc pipe bo's (a2xx-a5xx): */
	struct fd_bo *vsc_pipe_bo[32];

	/* which state objects need to be re-emit'd: */
	enum fd_dirty_3d_state dirty;

	/* per shader-stage dirty status: */
	enum fd_dirty_shader_state dirty_shader[PIPE_SHADER_TYPES];

	void *compute;
	struct pipe_blend_state *blend;
	struct pipe_rasterizer_state *rasterizer;
	struct pipe_depth_stencil_alpha_state *zsa;

	struct fd_texture_stateobj tex[PIPE_SHADER_TYPES];

	struct fd_program_stateobj prog;

	struct fd_vertex_state vtx;

	struct pipe_blend_color blend_color;
	struct pipe_stencil_ref stencil_ref;
	unsigned sample_mask;
	unsigned min_samples;
	/* local context fb state, for when ctx->batch is null: */
	struct pipe_framebuffer_state framebuffer;
	struct pipe_poly_stipple stipple;
	struct pipe_viewport_state viewport;
	struct pipe_scissor_state viewport_scissor;
	struct fd_constbuf_stateobj constbuf[PIPE_SHADER_TYPES];
	struct fd_shaderbuf_stateobj shaderbuf[PIPE_SHADER_TYPES];
	struct fd_shaderimg_stateobj shaderimg[PIPE_SHADER_TYPES];
	struct fd_streamout_stateobj streamout;
	struct fd_global_bindings_stateobj global_bindings;
	struct pipe_clip_state ucp;

	struct pipe_query *cond_query;
	bool cond_cond; /* inverted rendering condition */
	uint cond_mode;

	/* Private memory is a memory space where each fiber gets its own piece of
	 * memory, in addition to registers. It is backed by a buffer which needs
	 * to be large enough to hold the contents of every possible wavefront in
	 * every core of the GPU. Because it allocates space via the internal
	 * wavefront ID which is shared between all currently executing shaders,
	 * the same buffer can be reused by all shaders, as long as all shaders
	 * sharing the same buffer use the exact same configuration. There are two
	 * inputs to the configuration, the amount of per-fiber space and whether
	 * to use the newer per-wave or older per-fiber layout. We only ever
	 * increase the size, and shaders with a smaller size requirement simply
	 * use the larger existing buffer, so that we only need to keep track of
	 * one buffer and its size, but we still need to keep track of per-fiber
	 * and per-wave buffers separately so that we never use the same buffer
	 * for different layouts. pvtmem[0] is for per-fiber, and pvtmem[1] is for
	 * per-wave.
	 */
	struct {
		struct fd_bo *bo;
		uint32_t per_fiber_size;
	} pvtmem[2];

	struct pipe_debug_callback debug;

	struct u_trace_context trace_context;

	/* Called on rebind_resource() for any per-gen cleanup required: */
	void (*rebind_resource)(struct fd_context *ctx, struct fd_resource *rsc);

	/* GMEM/tile handling fxns: */
	void (*emit_tile_init)(struct fd_batch *batch);
	void (*emit_tile_prep)(struct fd_batch *batch, const struct fd_tile *tile);
	void (*emit_tile_mem2gmem)(struct fd_batch *batch, const struct fd_tile *tile);
	void (*emit_tile_renderprep)(struct fd_batch *batch, const struct fd_tile *tile);
	void (*emit_tile)(struct fd_batch *batch, const struct fd_tile *tile);
	void (*emit_tile_gmem2mem)(struct fd_batch *batch, const struct fd_tile *tile);
	void (*emit_tile_fini)(struct fd_batch *batch);   /* optional */

	/* optional, for GMEM bypass: */
	void (*emit_sysmem_prep)(struct fd_batch *batch);
	void (*emit_sysmem_fini)(struct fd_batch *batch);

	/* draw: */
	bool (*draw_vbo)(struct fd_context *ctx, const struct pipe_draw_info *info,
                         const struct pipe_draw_indirect_info *indirect,
                         const struct pipe_draw_start_count *draw,
			unsigned index_offset);
	bool (*clear)(struct fd_context *ctx, unsigned buffers,
			const union pipe_color_union *color, double depth, unsigned stencil);

	/* compute: */
	void (*launch_grid)(struct fd_context *ctx, const struct pipe_grid_info *info);

	/* query: */
	struct fd_query * (*create_query)(struct fd_context *ctx, unsigned query_type, unsigned index);
	void (*query_prepare)(struct fd_batch *batch, uint32_t num_tiles);
	void (*query_prepare_tile)(struct fd_batch *batch, uint32_t n,
			struct fd_ringbuffer *ring);
	void (*query_set_stage)(struct fd_batch *batch, enum fd_render_stage stage);

	/* blitter: */
	bool (*blit)(struct fd_context *ctx, const struct pipe_blit_info *info);
	void (*clear_ubwc)(struct fd_batch *batch, struct fd_resource *rsc);

	/* handling for barriers: */
	void (*framebuffer_barrier)(struct fd_context *ctx);

	/* logger: */
	void (*record_timestamp)(struct fd_ringbuffer *ring, struct fd_bo *bo, unsigned offset);
	uint64_t (*ts_to_ns)(uint64_t ts);

	/*
	 * Common pre-cooked VBO state (used for a3xx and later):
	 */

	/* for clear/gmem->mem vertices, and mem->gmem */
	struct pipe_resource *solid_vbuf;

	/* for mem->gmem tex coords: */
	struct pipe_resource *blit_texcoord_vbuf;

	/* vertex state for solid_vbuf:
	 *    - solid_vbuf / 12 / R32G32B32_FLOAT
	 */
	struct fd_vertex_state solid_vbuf_state;

	/* vertex state for blit_prog:
	 *    - blit_texcoord_vbuf / 8 / R32G32_FLOAT
	 *    - solid_vbuf / 12 / R32G32B32_FLOAT
	 */
	struct fd_vertex_state blit_vbuf_state;

	/*
	 * Info about state of previous draw, for state that comes from
	 * pipe_draw_info (ie. not part of a CSO).  This allows us to
	 * skip some register emit when the state doesn't change from
	 * draw-to-draw
	 */
	struct {
		bool dirty;               /* last draw state unknown */
		bool primitive_restart;
		uint32_t index_start;
		uint32_t instance_start;
		uint32_t restart_index;
		uint32_t streamout_mask;
	} last;
};

static inline struct fd_context *
fd_context(struct pipe_context *pctx)
{
	return (struct fd_context *)pctx;
}

/* mark all state dirty: */
static inline void
fd_context_all_dirty(struct fd_context *ctx)
{
	ctx->last.dirty = true;
	ctx->dirty = ~0;
	for (unsigned i = 0; i < PIPE_SHADER_TYPES; i++)
		ctx->dirty_shader[i] = ~0;
}

static inline void
fd_context_all_clean(struct fd_context *ctx)
{
	ctx->last.dirty = false;
	ctx->dirty = 0;
	for (unsigned i = 0; i < PIPE_SHADER_TYPES; i++) {
		/* don't mark compute state as clean, since it is not emitted
		 * during normal draw call.  The places that call _all_dirty(),
		 * it is safe to mark compute state dirty as well, but the
		 * inverse is not true.
		 */
		if (i == PIPE_SHADER_COMPUTE)
			continue;
		ctx->dirty_shader[i] = 0;
	}
}

static inline struct pipe_scissor_state *
fd_context_get_scissor(struct fd_context *ctx)
{
	return ctx->current_scissor;
}

static inline bool
fd_supported_prim(struct fd_context *ctx, unsigned prim)
{
	return (1 << prim) & ctx->primtype_mask;
}

void fd_context_switch_from(struct fd_context *ctx);
void fd_context_switch_to(struct fd_context *ctx, struct fd_batch *batch);
struct fd_batch * fd_context_batch(struct fd_context *ctx);
struct fd_batch * fd_context_batch_locked(struct fd_context *ctx);

void fd_context_setup_common_vbos(struct fd_context *ctx);
void fd_context_cleanup_common_vbos(struct fd_context *ctx);
void fd_emit_string(struct fd_ringbuffer *ring, const char *string, int len);
void fd_emit_string5(struct fd_ringbuffer *ring, const char *string, int len);

struct pipe_context * fd_context_init(struct fd_context *ctx,
		struct pipe_screen *pscreen, const uint8_t *primtypes,
		void *priv, unsigned flags);

void fd_context_destroy(struct pipe_context *pctx);

#endif /* FREEDRENO_CONTEXT_H_ */
