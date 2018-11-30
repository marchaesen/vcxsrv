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

#include <assert.h>

#include "freedreno_drmif.h"
#include "freedreno_ringbuffer.h"
#include "freedreno_priv.h"

struct fd_submit *
fd_submit_new(struct fd_pipe *pipe)
{
	return pipe->funcs->submit_new(pipe);
}

void
fd_submit_del(struct fd_submit *submit)
{
	return submit->funcs->destroy(submit);
}

int
fd_submit_flush(struct fd_submit *submit, int in_fence_fd, int *out_fence_fd,
		uint32_t *out_fence)
{
	return submit->funcs->flush(submit, in_fence_fd, out_fence_fd, out_fence);
}

struct fd_ringbuffer *
fd_submit_new_ringbuffer(struct fd_submit *submit, uint32_t size,
		enum fd_ringbuffer_flags flags)
{
	debug_assert(!(flags & _FD_RINGBUFFER_OBJECT));
	if (flags & FD_RINGBUFFER_STREAMING) {
		debug_assert(!(flags & FD_RINGBUFFER_GROWABLE));
		debug_assert(!(flags & FD_RINGBUFFER_PRIMARY));
	}
	return submit->funcs->new_ringbuffer(submit, size, flags);
}

struct fd_ringbuffer *
fd_ringbuffer_new_object(struct fd_pipe *pipe, uint32_t size)
{
	return pipe->funcs->ringbuffer_new_object(pipe, size);
}

void fd_ringbuffer_del(struct fd_ringbuffer *ring)
{
	if (!atomic_dec_and_test(&ring->refcnt))
		return;

	ring->funcs->destroy(ring);
}

struct fd_ringbuffer *
fd_ringbuffer_ref(struct fd_ringbuffer *ring)
{
	p_atomic_inc(&ring->refcnt);
	return ring;
}

void fd_ringbuffer_grow(struct fd_ringbuffer *ring, uint32_t ndwords)
{
	assert(ring->funcs->grow);     /* unsupported on kgsl */

	/* there is an upper bound on IB size, which appears to be 0x100000 */
	if (ring->size < 0x100000)
		ring->size *= 2;

	ring->funcs->grow(ring, ring->size);
}

void fd_ringbuffer_reloc(struct fd_ringbuffer *ring,
				     const struct fd_reloc *reloc)
{
	ring->funcs->emit_reloc(ring, reloc);
}

uint32_t fd_ringbuffer_cmd_count(struct fd_ringbuffer *ring)
{
	if (!ring->funcs->cmd_count)
		return 1;
	return ring->funcs->cmd_count(ring);
}

uint32_t
fd_ringbuffer_emit_reloc_ring_full(struct fd_ringbuffer *ring,
		struct fd_ringbuffer *target, uint32_t cmd_idx)
{
	return ring->funcs->emit_reloc_ring(ring, target, cmd_idx);
}
