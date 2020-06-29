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

#include <stdio.h>
#include "util/u_debug.h"
#include "util/u_dynarray.h"

#include "freedreno_drmif.h"
#include "adreno_common.xml.h"
#include "adreno_pm4.xml.h"

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

struct fd_ringbuffer;
struct fd_reloc;

struct fd_ringbuffer_funcs {
	void (*grow)(struct fd_ringbuffer *ring, uint32_t size);
	void (*emit_reloc)(struct fd_ringbuffer *ring,
			const struct fd_reloc *reloc);
	uint32_t (*emit_reloc_ring)(struct fd_ringbuffer *ring,
			struct fd_ringbuffer *target, uint32_t cmd_idx);
	uint32_t (*cmd_count)(struct fd_ringbuffer *ring);
	void (*destroy)(struct fd_ringbuffer *ring);
};

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

static inline void
fd_ringbuffer_del(struct fd_ringbuffer *ring)
{
	if (--ring->refcnt > 0)
		return;

	ring->funcs->destroy(ring);
}

static inline
struct fd_ringbuffer *
fd_ringbuffer_ref(struct fd_ringbuffer *ring)
{
	ring->refcnt++;
	return ring;
}

static inline void
fd_ringbuffer_grow(struct fd_ringbuffer *ring, uint32_t ndwords)
{
	assert(ring->funcs->grow);     /* unsupported on kgsl */

	/* there is an upper bound on IB size, which appears to be 0x100000 */
	if (ring->size < 0x100000)
		ring->size *= 2;

	ring->funcs->grow(ring, ring->size);
}

static inline void
fd_ringbuffer_emit(struct fd_ringbuffer *ring,
		uint32_t data)
{
	(*ring->cur++) = data;
}

struct fd_reloc {
	struct fd_bo *bo;
#define FD_RELOC_READ             0x0001
#define FD_RELOC_WRITE            0x0002
#define FD_RELOC_DUMP             0x0004
	uint32_t offset;
	uint32_t or;
	int32_t  shift;
	uint32_t orhi;      /* used for a5xx+ */
};

/* We always mark BOs for write, instead of tracking it across reloc
 * sources in userspace.  On the kernel side, this means we track a single
 * excl fence in the BO instead of a set of read fences, which is cheaper.
 * The downside is that a dmabuf-shared device won't be able to read in
 * parallel with a read-only access by freedreno, but most other drivers
 * have decided that that usecase isn't important enough to do this
 * tracking, as well.
 */
#define FD_RELOC_FLAGS_INIT (FD_RELOC_READ | FD_RELOC_WRITE)

/* NOTE: relocs are 2 dwords on a5xx+ */

static inline void
fd_ringbuffer_reloc(struct fd_ringbuffer *ring,
		const struct fd_reloc *reloc)
{
	ring->funcs->emit_reloc(ring, reloc);
}

static inline uint32_t
fd_ringbuffer_cmd_count(struct fd_ringbuffer *ring)
{
	if (!ring->funcs->cmd_count)
		return 1;
	return ring->funcs->cmd_count(ring);
}

static inline uint32_t
fd_ringbuffer_emit_reloc_ring_full(struct fd_ringbuffer *ring,
		struct fd_ringbuffer *target, uint32_t cmd_idx)
{
	return ring->funcs->emit_reloc_ring(ring, target, cmd_idx);
}

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

#define LOG_DWORDS 0

static inline void
OUT_RING(struct fd_ringbuffer *ring, uint32_t data)
{
	if (LOG_DWORDS) {
		fprintf(stderr, "ring[%p]: OUT_RING   %04x:  %08x", ring,
				(uint32_t)(ring->cur - ring->start), data);
	}
	fd_ringbuffer_emit(ring, data);
}

/*
 * NOTE: OUT_RELOC() is 2 dwords (64b) on a5xx+
 */
static inline void
OUT_RELOC(struct fd_ringbuffer *ring, struct fd_bo *bo,
		uint32_t offset, uint64_t or, int32_t shift)
{
	if (LOG_DWORDS) {
		fprintf(stderr, "ring[%p]: OUT_RELOC   %04x:  %p+%u << %d", ring,
				(uint32_t)(ring->cur - ring->start), bo, offset, shift);
	}
	debug_assert(offset < fd_bo_size(bo));
	fd_ringbuffer_reloc(ring, &(struct fd_reloc){
		.bo = bo,
		.offset = offset,
		.or = or,
		.shift = shift,
		.orhi = or >> 32,
	});
}

static inline void
OUT_RB(struct fd_ringbuffer *ring, struct fd_ringbuffer *target)
{
	fd_ringbuffer_emit_reloc_ring_full(ring, target, 0);
}

static inline void BEGIN_RING(struct fd_ringbuffer *ring, uint32_t ndwords)
{
	if (unlikely(ring->cur + ndwords > ring->end))
		fd_ringbuffer_grow(ring, ndwords);
}

static inline void
OUT_PKT0(struct fd_ringbuffer *ring, uint16_t regindx, uint16_t cnt)
{
	BEGIN_RING(ring, cnt+1);
	OUT_RING(ring, CP_TYPE0_PKT | ((cnt-1) << 16) | (regindx & 0x7FFF));
}

static inline void
OUT_PKT2(struct fd_ringbuffer *ring)
{
	BEGIN_RING(ring, 1);
	OUT_RING(ring, CP_TYPE2_PKT);
}

static inline void
OUT_PKT3(struct fd_ringbuffer *ring, uint8_t opcode, uint16_t cnt)
{
	BEGIN_RING(ring, cnt+1);
	OUT_RING(ring, CP_TYPE3_PKT | ((cnt-1) << 16) | ((opcode & 0xFF) << 8));
}

/*
 * Starting with a5xx, pkt4/pkt7 are used instead of pkt0/pkt3
 */

static inline unsigned
_odd_parity_bit(unsigned val)
{
	/* See: http://graphics.stanford.edu/~seander/bithacks.html#ParityParallel
	 * note that we want odd parity so 0x6996 is inverted.
	 */
	val ^= val >> 16;
	val ^= val >> 8;
	val ^= val >> 4;
	val &= 0xf;
	return (~0x6996 >> val) & 1;
}

static inline void
OUT_PKT4(struct fd_ringbuffer *ring, uint16_t regindx, uint16_t cnt)
{
	BEGIN_RING(ring, cnt+1);
	OUT_RING(ring, CP_TYPE4_PKT | cnt |
			(_odd_parity_bit(cnt) << 7) |
			((regindx & 0x3ffff) << 8) |
			((_odd_parity_bit(regindx) << 27)));
}

static inline void
OUT_PKT7(struct fd_ringbuffer *ring, uint8_t opcode, uint16_t cnt)
{
	BEGIN_RING(ring, cnt+1);
	OUT_RING(ring, CP_TYPE7_PKT | cnt |
			(_odd_parity_bit(cnt) << 15) |
			((opcode & 0x7f) << 16) |
			((_odd_parity_bit(opcode) << 23)));
}

static inline void
OUT_WFI(struct fd_ringbuffer *ring)
{
	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x00000000);
}

static inline void
OUT_WFI5(struct fd_ringbuffer *ring)
{
	OUT_PKT7(ring, CP_WAIT_FOR_IDLE, 0);
}

#endif /* FREEDRENO_RINGBUFFER_H_ */
