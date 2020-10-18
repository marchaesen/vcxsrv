/*
 * Copyright (C) 2018 Rob Clark <robclark@freedesktop.org>
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

#include <assert.h>
#include <inttypes.h>

#include "util/hash_table.h"
#include "util/slab.h"

#include "drm/freedreno_ringbuffer.h"
#include "msm_priv.h"

/* A "softpin" implementation of submit/ringbuffer, which lowers CPU overhead
 * by avoiding the additional tracking necessary to build cmds/relocs tables
 * (but still builds a bos table)
 */


#define INIT_SIZE 0x1000


struct msm_submit_sp {
	struct fd_submit base;

	DECLARE_ARRAY(struct drm_msm_gem_submit_bo, submit_bos);
	DECLARE_ARRAY(struct fd_bo *, bos);

	/* maps fd_bo to idx in bos table: */
	struct hash_table *bo_table;

	struct slab_child_pool ring_pool;

	struct fd_ringbuffer *primary;

	/* Allow for sub-allocation of stateobj ring buffers (ie. sharing
	 * the same underlying bo)..
	 *
	 * We also rely on previous stateobj having been fully constructed
	 * so we can reclaim extra space at it's end.
	 */
	struct fd_ringbuffer *suballoc_ring;
};
FD_DEFINE_CAST(fd_submit, msm_submit_sp);

/* for FD_RINGBUFFER_GROWABLE rb's, tracks the 'finalized' cmdstream buffers
 * and sizes.  Ie. a finalized buffer can have no more commands appended to
 * it.
 */
struct msm_cmd_sp {
	struct fd_bo *ring_bo;
	unsigned size;
};

struct msm_ringbuffer_sp {
	struct fd_ringbuffer base;

	/* for FD_RINGBUFFER_STREAMING rb's which are sub-allocated */
	unsigned offset;

	union {
		/* for _FD_RINGBUFFER_OBJECT case, the array of BOs referenced from
		 * this one
		 */
		struct {
			struct fd_pipe *pipe;
			DECLARE_ARRAY(struct fd_bo *, reloc_bos);
		};
		/* for other cases: */
		struct {
			struct fd_submit *submit;
			DECLARE_ARRAY(struct msm_cmd_sp, cmds);
		};
	} u;

	struct fd_bo *ring_bo;
};
FD_DEFINE_CAST(fd_ringbuffer, msm_ringbuffer_sp);

static void finalize_current_cmd(struct fd_ringbuffer *ring);
static struct fd_ringbuffer * msm_ringbuffer_sp_init(
		struct msm_ringbuffer_sp *msm_ring,
		uint32_t size, enum fd_ringbuffer_flags flags);

/* add (if needed) bo to submit and return index: */
static uint32_t
msm_submit_append_bo(struct msm_submit_sp *submit, struct fd_bo *bo)
{
	struct msm_bo *msm_bo = to_msm_bo(bo);
	uint32_t idx;

	/* NOTE: it is legal to use the same bo on different threads for
	 * different submits.  But it is not legal to use the same submit
	 * from given threads.
	 */
	idx = READ_ONCE(msm_bo->idx);

	if (unlikely((idx >= submit->nr_submit_bos) ||
			(submit->submit_bos[idx].handle != bo->handle))) {
		uint32_t hash = _mesa_hash_pointer(bo);
		struct hash_entry *entry;

		entry = _mesa_hash_table_search_pre_hashed(submit->bo_table, hash, bo);
		if (entry) {
			/* found */
			idx = (uint32_t)(uintptr_t)entry->data;
		} else {
			idx = APPEND(submit, submit_bos);
			idx = APPEND(submit, bos);

			submit->submit_bos[idx].flags = bo->flags;
			submit->submit_bos[idx].handle = bo->handle;
			submit->submit_bos[idx].presumed = 0;

			submit->bos[idx] = fd_bo_ref(bo);

			_mesa_hash_table_insert_pre_hashed(submit->bo_table, hash, bo,
					(void *)(uintptr_t)idx);
		}
		msm_bo->idx = idx;
	}

	return idx;
}

static void
msm_submit_suballoc_ring_bo(struct fd_submit *submit,
		struct msm_ringbuffer_sp *msm_ring, uint32_t size)
{
	struct msm_submit_sp *msm_submit = to_msm_submit_sp(submit);
	unsigned suballoc_offset = 0;
	struct fd_bo *suballoc_bo = NULL;

	if (msm_submit->suballoc_ring) {
		struct msm_ringbuffer_sp *suballoc_ring =
				to_msm_ringbuffer_sp(msm_submit->suballoc_ring);

		suballoc_bo = suballoc_ring->ring_bo;
		suballoc_offset = fd_ringbuffer_size(msm_submit->suballoc_ring) +
				suballoc_ring->offset;

		suballoc_offset = align(suballoc_offset, 0x10);

		if ((size + suballoc_offset) > suballoc_bo->size) {
			suballoc_bo = NULL;
		}
	}

	if (!suballoc_bo) {
		// TODO possibly larger size for streaming bo?
		msm_ring->ring_bo = fd_bo_new_ring(submit->pipe->dev, 0x8000);
		msm_ring->offset = 0;
	} else {
		msm_ring->ring_bo = fd_bo_ref(suballoc_bo);
		msm_ring->offset = suballoc_offset;
	}

	struct fd_ringbuffer *old_suballoc_ring = msm_submit->suballoc_ring;

	msm_submit->suballoc_ring = fd_ringbuffer_ref(&msm_ring->base);

	if (old_suballoc_ring)
		fd_ringbuffer_del(old_suballoc_ring);
}

static struct fd_ringbuffer *
msm_submit_sp_new_ringbuffer(struct fd_submit *submit, uint32_t size,
		enum fd_ringbuffer_flags flags)
{
	struct msm_submit_sp *msm_submit = to_msm_submit_sp(submit);
	struct msm_ringbuffer_sp *msm_ring;

	msm_ring = slab_alloc(&msm_submit->ring_pool);

	msm_ring->u.submit = submit;

	/* NOTE: needs to be before _suballoc_ring_bo() since it could
	 * increment the refcnt of the current ring
	 */
	msm_ring->base.refcnt = 1;

	if (flags & FD_RINGBUFFER_STREAMING) {
		msm_submit_suballoc_ring_bo(submit, msm_ring, size);
	} else {
		if (flags & FD_RINGBUFFER_GROWABLE)
			size = INIT_SIZE;

		msm_ring->offset = 0;
		msm_ring->ring_bo = fd_bo_new_ring(submit->pipe->dev, size);
	}

	if (!msm_ringbuffer_sp_init(msm_ring, size, flags))
		return NULL;

	if (flags & FD_RINGBUFFER_PRIMARY) {
		debug_assert(!msm_submit->primary);
		msm_submit->primary = fd_ringbuffer_ref(&msm_ring->base);
	}

	return &msm_ring->base;
}

static int
msm_submit_sp_flush(struct fd_submit *submit, int in_fence_fd,
		int *out_fence_fd, uint32_t *out_fence)
{
	struct msm_submit_sp *msm_submit = to_msm_submit_sp(submit);
	struct msm_pipe *msm_pipe = to_msm_pipe(submit->pipe);
	struct drm_msm_gem_submit req = {
			.flags = msm_pipe->pipe,
			.queueid = msm_pipe->queue_id,
	};
	int ret;

	debug_assert(msm_submit->primary);
	finalize_current_cmd(msm_submit->primary);

	struct msm_ringbuffer_sp *primary = to_msm_ringbuffer_sp(msm_submit->primary);
	struct drm_msm_gem_submit_cmd cmds[primary->u.nr_cmds];

	for (unsigned i = 0; i < primary->u.nr_cmds; i++) {
		cmds[i].type = MSM_SUBMIT_CMD_BUF;
		cmds[i].submit_idx = msm_submit_append_bo(msm_submit,
				primary->u.cmds[i].ring_bo);
		cmds[i].submit_offset = primary->offset;
		cmds[i].size = primary->u.cmds[i].size;
		cmds[i].pad = 0;
		cmds[i].nr_relocs = 0;
	}

	if (in_fence_fd != -1) {
		req.flags |= MSM_SUBMIT_FENCE_FD_IN | MSM_SUBMIT_NO_IMPLICIT;
		req.fence_fd = in_fence_fd;
	}

	if (out_fence_fd) {
		req.flags |= MSM_SUBMIT_FENCE_FD_OUT;
	}

	/* needs to be after get_cmd() as that could create bos/cmds table: */
	req.bos = VOID2U64(msm_submit->submit_bos),
	req.nr_bos = msm_submit->nr_submit_bos;
	req.cmds = VOID2U64(cmds),
	req.nr_cmds = primary->u.nr_cmds;

	DEBUG_MSG("nr_cmds=%u, nr_bos=%u", req.nr_cmds, req.nr_bos);

	ret = drmCommandWriteRead(submit->pipe->dev->fd, DRM_MSM_GEM_SUBMIT,
			&req, sizeof(req));
	if (ret) {
		ERROR_MSG("submit failed: %d (%s)", ret, strerror(errno));
		msm_dump_submit(&req);
	} else if (!ret) {
		if (out_fence)
			*out_fence = req.fence;

		if (out_fence_fd)
			*out_fence_fd = req.fence_fd;
	}

	return ret;
}

static void
msm_submit_sp_destroy(struct fd_submit *submit)
{
	struct msm_submit_sp *msm_submit = to_msm_submit_sp(submit);

	if (msm_submit->primary)
		fd_ringbuffer_del(msm_submit->primary);
	if (msm_submit->suballoc_ring)
		fd_ringbuffer_del(msm_submit->suballoc_ring);

	_mesa_hash_table_destroy(msm_submit->bo_table, NULL);

	// TODO it would be nice to have a way to debug_assert() if all
	// rb's haven't been free'd back to the slab, because that is
	// an indication that we are leaking bo's
	slab_destroy_child(&msm_submit->ring_pool);

	for (unsigned i = 0; i < msm_submit->nr_bos; i++)
		fd_bo_del(msm_submit->bos[i]);

	free(msm_submit->submit_bos);
	free(msm_submit->bos);
	free(msm_submit);
}

static const struct fd_submit_funcs submit_funcs = {
		.new_ringbuffer = msm_submit_sp_new_ringbuffer,
		.flush = msm_submit_sp_flush,
		.destroy = msm_submit_sp_destroy,
};

struct fd_submit *
msm_submit_sp_new(struct fd_pipe *pipe)
{
	struct msm_submit_sp *msm_submit = calloc(1, sizeof(*msm_submit));
	struct fd_submit *submit;

	msm_submit->bo_table = _mesa_hash_table_create(NULL,
			_mesa_hash_pointer, _mesa_key_pointer_equal);

	slab_create_child(&msm_submit->ring_pool, &to_msm_pipe(pipe)->ring_pool);

	submit = &msm_submit->base;
	submit->pipe = pipe;
	submit->funcs = &submit_funcs;

	return submit;
}

void
msm_pipe_sp_ringpool_init(struct msm_pipe *msm_pipe)
{
	// TODO tune size:
	slab_create_parent(&msm_pipe->ring_pool, sizeof(struct msm_ringbuffer_sp), 16);
}

void
msm_pipe_sp_ringpool_fini(struct msm_pipe *msm_pipe)
{
	if (msm_pipe->ring_pool.num_elements)
		slab_destroy_parent(&msm_pipe->ring_pool);
}

static void
finalize_current_cmd(struct fd_ringbuffer *ring)
{
	debug_assert(!(ring->flags & _FD_RINGBUFFER_OBJECT));

	struct msm_ringbuffer_sp *msm_ring = to_msm_ringbuffer_sp(ring);
	unsigned idx = APPEND(&msm_ring->u, cmds);

	msm_ring->u.cmds[idx].ring_bo = fd_bo_ref(msm_ring->ring_bo);
	msm_ring->u.cmds[idx].size = offset_bytes(ring->cur, ring->start);
}

static void
msm_ringbuffer_sp_grow(struct fd_ringbuffer *ring, uint32_t size)
{
	struct msm_ringbuffer_sp *msm_ring = to_msm_ringbuffer_sp(ring);
	struct fd_pipe *pipe = msm_ring->u.submit->pipe;

	debug_assert(ring->flags & FD_RINGBUFFER_GROWABLE);

	finalize_current_cmd(ring);

	fd_bo_del(msm_ring->ring_bo);
	msm_ring->ring_bo = fd_bo_new_ring(pipe->dev, size);

	ring->start = fd_bo_map(msm_ring->ring_bo);
	ring->end = &(ring->start[size/4]);
	ring->cur = ring->start;
	ring->size = size;
}

static void
msm_ringbuffer_sp_emit_reloc(struct fd_ringbuffer *ring,
		const struct fd_reloc *reloc)
{
	struct msm_ringbuffer_sp *msm_ring = to_msm_ringbuffer_sp(ring);
	struct fd_pipe *pipe;

	if (ring->flags & _FD_RINGBUFFER_OBJECT) {
		/* Avoid emitting duplicate BO references into the list.  Ringbuffer
		 * objects are long-lived, so this saves ongoing work at draw time in
		 * exchange for a bit at context setup/first draw.  And the number of
		 * relocs per ringbuffer object is fairly small, so the O(n^2) doesn't
		 * hurt much.
		 */
		bool found = false;
		for (int i = 0; i < msm_ring->u.nr_reloc_bos; i++) {
			if (msm_ring->u.reloc_bos[i] == reloc->bo) {
				found = true;
				break;
			}
		}
		if (!found) {
			unsigned idx = APPEND(&msm_ring->u, reloc_bos);
			msm_ring->u.reloc_bos[idx] = fd_bo_ref(reloc->bo);
		}

		pipe = msm_ring->u.pipe;
	} else {
		struct msm_submit_sp *msm_submit =
				to_msm_submit_sp(msm_ring->u.submit);

		msm_submit_append_bo(msm_submit, reloc->bo);

		pipe = msm_ring->u.submit->pipe;
	}

	uint64_t iova = reloc->bo->iova + reloc->offset;
	int shift = reloc->shift;

	if (shift < 0)
		iova >>= -shift;
	else
		iova <<= shift;

	uint32_t dword = iova;

	(*ring->cur++) = dword | reloc->or;

	if (pipe->gpu_id >= 500) {
		dword = iova >> 32;
		(*ring->cur++) = dword | reloc->orhi;
	}
}

static uint32_t
msm_ringbuffer_sp_emit_reloc_ring(struct fd_ringbuffer *ring,
		struct fd_ringbuffer *target, uint32_t cmd_idx)
{
	struct msm_ringbuffer_sp *msm_target = to_msm_ringbuffer_sp(target);
	struct fd_bo *bo;
	uint32_t size;

	if ((target->flags & FD_RINGBUFFER_GROWABLE) &&
			(cmd_idx < msm_target->u.nr_cmds)) {
		bo   = msm_target->u.cmds[cmd_idx].ring_bo;
		size = msm_target->u.cmds[cmd_idx].size;
	} else {
		bo   = msm_target->ring_bo;
		size = offset_bytes(target->cur, target->start);
	}

	msm_ringbuffer_sp_emit_reloc(ring, &(struct fd_reloc){
		.bo     = bo,
		.offset = msm_target->offset,
	});

	if (!(target->flags & _FD_RINGBUFFER_OBJECT))
		return size;

	struct msm_ringbuffer_sp *msm_ring = to_msm_ringbuffer_sp(ring);

	if (ring->flags & _FD_RINGBUFFER_OBJECT) {
		for (unsigned i = 0; i < msm_target->u.nr_reloc_bos; i++) {
			unsigned idx = APPEND(&msm_ring->u, reloc_bos);

			msm_ring->u.reloc_bos[idx] =
				fd_bo_ref(msm_target->u.reloc_bos[i]);
		}
	} else {
		// TODO it would be nice to know whether we have already
		// seen this target before.  But hopefully we hit the
		// append_bo() fast path enough for this to not matter:
		struct msm_submit_sp *msm_submit = to_msm_submit_sp(msm_ring->u.submit);

		for (unsigned i = 0; i < msm_target->u.nr_reloc_bos; i++) {
			msm_submit_append_bo(msm_submit, msm_target->u.reloc_bos[i]);
		}
	}

	return size;
}

static uint32_t
msm_ringbuffer_sp_cmd_count(struct fd_ringbuffer *ring)
{
	if (ring->flags & FD_RINGBUFFER_GROWABLE)
		return to_msm_ringbuffer_sp(ring)->u.nr_cmds + 1;
	return 1;
}

static void
msm_ringbuffer_sp_destroy(struct fd_ringbuffer *ring)
{
	struct msm_ringbuffer_sp *msm_ring = to_msm_ringbuffer_sp(ring);

	fd_bo_del(msm_ring->ring_bo);

	if (ring->flags & _FD_RINGBUFFER_OBJECT) {
		for (unsigned i = 0; i < msm_ring->u.nr_reloc_bos; i++) {
			fd_bo_del(msm_ring->u.reloc_bos[i]);
		}
		free(msm_ring->u.reloc_bos);

		free(msm_ring);
	} else {
		struct fd_submit *submit = msm_ring->u.submit;

		for (unsigned i = 0; i < msm_ring->u.nr_cmds; i++) {
			fd_bo_del(msm_ring->u.cmds[i].ring_bo);
		}
		free(msm_ring->u.cmds);

		slab_free(&to_msm_submit_sp(submit)->ring_pool, msm_ring);
	}
}

static const struct fd_ringbuffer_funcs ring_funcs = {
		.grow = msm_ringbuffer_sp_grow,
		.emit_reloc = msm_ringbuffer_sp_emit_reloc,
		.emit_reloc_ring = msm_ringbuffer_sp_emit_reloc_ring,
		.cmd_count = msm_ringbuffer_sp_cmd_count,
		.destroy = msm_ringbuffer_sp_destroy,
};

static inline struct fd_ringbuffer *
msm_ringbuffer_sp_init(struct msm_ringbuffer_sp *msm_ring, uint32_t size,
		enum fd_ringbuffer_flags flags)
{
	struct fd_ringbuffer *ring = &msm_ring->base;

	/* We don't do any translation from internal FD_RELOC flags to MSM flags. */
	STATIC_ASSERT(FD_RELOC_READ == MSM_SUBMIT_BO_READ);
	STATIC_ASSERT(FD_RELOC_WRITE == MSM_SUBMIT_BO_WRITE);
	STATIC_ASSERT(FD_RELOC_DUMP == MSM_SUBMIT_BO_DUMP);

	debug_assert(msm_ring->ring_bo);

	uint8_t *base = fd_bo_map(msm_ring->ring_bo);
	ring->start = (void *)(base + msm_ring->offset);
	ring->end = &(ring->start[size/4]);
	ring->cur = ring->start;

	ring->size = size;
	ring->flags = flags;

	ring->funcs = &ring_funcs;

	// TODO initializing these could probably be conditional on flags
	// since unneed for FD_RINGBUFFER_STAGING case..
	msm_ring->u.cmds = NULL;
	msm_ring->u.nr_cmds = msm_ring->u.max_cmds = 0;

	msm_ring->u.reloc_bos = NULL;
	msm_ring->u.nr_reloc_bos = msm_ring->u.max_reloc_bos = 0;

	return ring;
}

struct fd_ringbuffer *
msm_ringbuffer_sp_new_object(struct fd_pipe *pipe, uint32_t size)
{
	struct msm_ringbuffer_sp *msm_ring = malloc(sizeof(*msm_ring));

	msm_ring->u.pipe = pipe;
	msm_ring->offset = 0;
	msm_ring->ring_bo = fd_bo_new_ring(pipe->dev, size);
	msm_ring->base.refcnt = 1;

	return msm_ringbuffer_sp_init(msm_ring, size, _FD_RINGBUFFER_OBJECT);
}
