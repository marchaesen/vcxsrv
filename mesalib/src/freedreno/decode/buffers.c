/*
 * Copyright (c) 2012 Rob Clark <robdclark@gmail.com>
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

/*
 * Helper lib to track gpu buffers contents/address, and map between gpu and
 * host address while decoding cmdstream/crashdumps
 */

#include <assert.h>
#include <stdlib.h>

#include "buffers.h"

struct buffer {
	void *hostptr;
	unsigned int len;
	uint64_t gpuaddr;

	/* for 'once' mode, for buffers containing cmdstream keep track per offset
	 * into buffer of which modes it has already been dumped;
	 */
	struct {
		unsigned offset;
		unsigned dumped_mask;
	} offsets[64];
	unsigned noffsets;
};

static struct buffer buffers[512];
static int nbuffers;

static int
buffer_contains_gpuaddr(struct buffer *buf, uint64_t gpuaddr, uint32_t len)
{
	return (buf->gpuaddr <= gpuaddr) && (gpuaddr < (buf->gpuaddr + buf->len));
}

static int
buffer_contains_hostptr(struct buffer *buf, void *hostptr)
{
	return (buf->hostptr <= hostptr) && (hostptr < (buf->hostptr + buf->len));
}


uint64_t
gpuaddr(void *hostptr)
{
	int i;
	for (i = 0; i < nbuffers; i++)
		if (buffer_contains_hostptr(&buffers[i], hostptr))
			return buffers[i].gpuaddr + (hostptr - buffers[i].hostptr);
	return 0;
}

uint64_t
gpubaseaddr(uint64_t gpuaddr)
{
	int i;
	if (!gpuaddr)
		return 0;
	for (i = 0; i < nbuffers; i++)
		if (buffer_contains_gpuaddr(&buffers[i], gpuaddr, 0))
			return buffers[i].gpuaddr;
	return 0;
}

void *
hostptr(uint64_t gpuaddr)
{
	int i;
	if (!gpuaddr)
		return 0;
	for (i = 0; i < nbuffers; i++)
		if (buffer_contains_gpuaddr(&buffers[i], gpuaddr, 0))
			return buffers[i].hostptr + (gpuaddr - buffers[i].gpuaddr);
	return 0;
}

unsigned
hostlen(uint64_t gpuaddr)
{
	int i;
	if (!gpuaddr)
		return 0;
	for (i = 0; i < nbuffers; i++)
		if (buffer_contains_gpuaddr(&buffers[i], gpuaddr, 0))
			return buffers[i].len + buffers[i].gpuaddr - gpuaddr;
	return 0;
}

bool
has_dumped(uint64_t gpuaddr, unsigned enable_mask)
{
	if (!gpuaddr)
		return false;

	for (int i = 0; i < nbuffers; i++) {
		if (buffer_contains_gpuaddr(&buffers[i], gpuaddr, 0)) {
			struct buffer *b = &buffers[i];
			assert(gpuaddr >= b->gpuaddr);
			unsigned offset = gpuaddr - b->gpuaddr;

			unsigned n = 0;
			while (n < b->noffsets) {
				if (offset == b->offsets[n].offset)
					break;
				n++;
			}

			/* if needed, allocate a new offset entry: */
			if (n == b->noffsets) {
				b->noffsets++;
				assert(b->noffsets < ARRAY_SIZE(b->offsets));
				b->offsets[n].dumped_mask = 0;
				b->offsets[n].offset = offset;
			}

			if ((b->offsets[n].dumped_mask & enable_mask) == enable_mask)
				return true;

			b->offsets[n].dumped_mask |= enable_mask;

			return false;
		}
	}

	return false;
}

void
reset_buffers(void)
{
	for (int i = 0; i < nbuffers; i++) {
		free(buffers[i].hostptr);
		buffers[i].hostptr = NULL;
		buffers[i].len = 0;
		buffers[i].noffsets = 0;
	}
	nbuffers = 0;
}

/**
 * Record buffer contents, takes ownership of hostptr (freed in
 * reset_buffers())
 */
void
add_buffer(uint64_t gpuaddr, unsigned int len, void *hostptr)
{
	int i;

	for (i = 0; i < nbuffers; i++) {
		if (buffers[i].gpuaddr == gpuaddr)
			break;
	}

	if (i == nbuffers) {
		/* some traces, like test-perf, with some blob versions,
		 * seem to generate an unreasonable # of gpu buffers (a
		 * leak?), so just ignore them.
		 */
		if (nbuffers >= ARRAY_SIZE(buffers)) {
			free(hostptr);
			return;
		}
		nbuffers++;
	}

	buffers[i].hostptr = hostptr;
	buffers[i].len     = len;
	buffers[i].gpuaddr = gpuaddr;
}
