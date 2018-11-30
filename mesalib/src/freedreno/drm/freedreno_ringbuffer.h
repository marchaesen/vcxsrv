/*
 * Copyright (C) 2012-2018 Rob Clark <robclark@freedesktop.org>
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

#ifndef FREEDRENO_RINGBUFFER_H_
#define FREEDRENO_RINGBUFFER_H_

#include "util/u_debug.h"

#include "freedreno_drmif.h"

struct fd_submit;
struct fd_ringbuffer;

enum fd_ringbuffer_flags {

	/* Primary ringbuffer for a submit, ie. an IB1 level rb
	 * which kernel must setup RB->IB1 CP_INDIRECT_BRANCH
	 * packets.
	 */
	FD_RINGBUFFER_PRIMARY = 0x1,

	/* Hint that the stateobj will be used for streaming state
	 * that is used once or a few times and then discarded.
	 *
	 * For sub-allocation, non streaming stateobj's should be
	 * sub-allocated from a page size buffer, so one long lived
	 * state obj doesn't prevent other pages from being freed.
	 * (Ie. it would be no worse than allocating a page sized
	 * bo for each small non-streaming stateobj).
	 *
	 * But streaming stateobj's could be sub-allocated from a
	 * larger buffer to reduce the alloc/del overhead.
	 */
	FD_RINGBUFFER_STREAMING = 0x2,

	/* Indicates that "growable" cmdstream can be used,
	 * consisting of multiple physical cmdstream buffers
	 */
	FD_RINGBUFFER_GROWABLE = 0x4,

	/* Internal use only: */
	_FD_RINGBUFFER_OBJECT = 0x8,
};

/* A submit object manages/tracks all the state buildup for a "submit"
 * ioctl to the kernel.  Additionally, with the exception of long-lived
 * non-STREAMING stateobj rb's, rb's are allocated from the submit.
 */
struct fd_submit * fd_submit_new(struct fd_pipe *pipe);

/* NOTE: all ringbuffer's create from the submit should be unref'd
 * before destroying the submit.
 */
void fd_submit_del(struct fd_submit *submit);

/* Allocate a new rb from the submit. */
struct fd_ringbuffer * fd_submit_new_ringbuffer(struct fd_submit *submit,
		uint32_t size, enum fd_ringbuffer_flags flags);

/* in_fence_fd: -1 for no in-fence, else fence fd
 * out_fence_fd: NULL for no output-fence requested, else ptr to return out-fence
 */
int fd_submit_flush(struct fd_submit *submit,
		int in_fence_fd, int *out_fence_fd,
		uint32_t *out_fence);

struct fd_ringbuffer_funcs;

/* the ringbuffer object is not opaque so that OUT_RING() type stuff
 * can be inlined.  Note that users should not make assumptions about
 * the size of this struct.
 */
struct fd_ringbuffer {
	uint32_t *cur, *end, *start;
	const struct fd_ringbuffer_funcs *funcs;

// size or end coudl probably go away
	int size;
	int32_t refcnt;
	enum fd_ringbuffer_flags flags;
};

/* Allocate a new long-lived state object, not associated with
 * a submit:
 */
struct fd_ringbuffer * fd_ringbuffer_new_object(struct fd_pipe *pipe,
		uint32_t size);

struct fd_ringbuffer *fd_ringbuffer_ref(struct fd_ringbuffer *ring);
void fd_ringbuffer_del(struct fd_ringbuffer *ring);

void fd_ringbuffer_grow(struct fd_ringbuffer *ring, uint32_t ndwords);

static inline void fd_ringbuffer_emit(struct fd_ringbuffer *ring,
		uint32_t data)
{
	(*ring->cur++) = data;
}

struct fd_reloc {
	struct fd_bo *bo;
#define FD_RELOC_READ             0x0001
#define FD_RELOC_WRITE            0x0002
	uint32_t flags;
	uint32_t offset;
	uint32_t or;
	int32_t  shift;
	uint32_t orhi;      /* used for a5xx+ */
};

/* NOTE: relocs are 2 dwords on a5xx+ */

void fd_ringbuffer_reloc(struct fd_ringbuffer *ring, const struct fd_reloc *reloc);
uint32_t fd_ringbuffer_cmd_count(struct fd_ringbuffer *ring);
uint32_t fd_ringbuffer_emit_reloc_ring_full(struct fd_ringbuffer *ring,
		struct fd_ringbuffer *target, uint32_t cmd_idx);

static inline uint32_t
offset_bytes(void *end, void *start)
{
	return ((char *)end) - ((char *)start);
}

static inline uint32_t
fd_ringbuffer_size(struct fd_ringbuffer *ring)
{
	/* only really needed for stateobj ringbuffers, and won't really
	 * do what you expect for growable rb's.. so lets just restrict
	 * this to stateobj's for now:
	 */
	debug_assert(!(ring->flags & FD_RINGBUFFER_GROWABLE));
	return offset_bytes(ring->cur, ring->start);
}


#endif /* FREEDRENO_RINGBUFFER_H_ */
