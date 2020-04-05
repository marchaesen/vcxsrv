/*
 * Copyright © 2020 Google, Inc.
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
 */

#ifndef __MAIN_H__
#define __MAIN_H__

#include <err.h>
#include <stdint.h>
#include <stdio.h>

#include "drm/freedreno_drmif.h"
#include "drm/freedreno_ringbuffer.h"

#include "registers/adreno_pm4.xml.h"
#include "registers/adreno_common.xml.h"

#define MAX_BUFS 4

struct kernel {
	/* filled in by backend when shader is assembled: */
	uint32_t local_size[3];
	uint32_t num_bufs;
	uint32_t buf_sizes[MAX_BUFS]; /* size in dwords */

	/* filled in by frontend before launching grid: */
	struct fd_bo *bufs[MAX_BUFS];
};

struct perfcntr {
	const char *name;

	/* for backend to configure/read the counter, describes
	 * the selected counter:
	 */
	unsigned select_reg;
	unsigned counter_reg_lo;
	unsigned counter_reg_hi;
	/* and selected countable:
	 */
	unsigned selector;
};

/* per-generation entry-points: */
struct backend {
	struct kernel *(*assemble)(struct backend *b, FILE *in);
	void (*disassemble)(struct kernel *kernel, FILE *out);
	void (*emit_grid)(struct kernel *kernel, uint32_t grid[3],
			struct fd_submit *submit);

	/* performance-counter API: */
	void (*set_perfcntrs)(struct backend *b, const struct perfcntr *perfcntrs,
			unsigned num_perfcntrs);
	void (*read_perfcntrs)(struct backend *b, uint64_t *results);
};

#define define_cast(_from, _to)	\
static inline struct _to *		\
to_ ## _to(struct _from *f)		\
{ return (struct _to *)f; }

struct backend *a6xx_init(struct fd_device *dev, uint32_t gpu_id);

/*
 * cmdstream helpers:
 */

static inline void
BEGIN_RING(struct fd_ringbuffer *ring, uint32_t ndwords)
{
	if (ring->cur + ndwords > ring->end)
		fd_ringbuffer_grow(ring, ndwords);
}

static inline void
OUT_RING(struct fd_ringbuffer *ring, uint32_t data)
{
	fd_ringbuffer_emit(ring, data);
}

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

/*
 * NOTE: OUT_RELOC*() is 2 dwords (64b) on a5xx+
 */

static inline void
__out_reloc(struct fd_ringbuffer *ring, struct fd_bo *bo,
		uint32_t offset, uint64_t or, int32_t shift, uint32_t flags)
{
	debug_assert(offset < fd_bo_size(bo));
	fd_ringbuffer_reloc(ring, &(struct fd_reloc){
		.bo = bo,
		.flags = flags,
		.offset = offset,
		.or = or,
		.shift = shift,
		.orhi = or >> 32,
	});
}

static inline void
OUT_RELOC(struct fd_ringbuffer *ring, struct fd_bo *bo,
		uint32_t offset, uint64_t or, int32_t shift)
{
	__out_reloc(ring, bo, offset, or, shift, FD_RELOC_READ);
}

static inline void
OUT_RELOCW(struct fd_ringbuffer *ring, struct fd_bo *bo,
		uint32_t offset, uint64_t or, int32_t shift)
{
	__out_reloc(ring, bo, offset, or, shift, FD_RELOC_READ | FD_RELOC_WRITE);
}

static inline void
OUT_RELOCD(struct fd_ringbuffer *ring, struct fd_bo *bo,
		uint32_t offset, uint64_t or, int32_t shift)
{
	__out_reloc(ring, bo, offset, or, shift, FD_RELOC_READ | FD_RELOC_DUMP);
}

static inline void
OUT_RB(struct fd_ringbuffer *ring, struct fd_ringbuffer *target)
{
	fd_ringbuffer_emit_reloc_ring_full(ring, target, 0);
}

/* for conditionally setting boolean flag(s): */
#define COND(bool, val) ((bool) ? (val) : 0)

#endif /* __MAIN_H__ */
