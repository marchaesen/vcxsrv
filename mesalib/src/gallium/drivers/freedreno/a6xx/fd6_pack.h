/*
 * Copyright © 2019 Google, Inc.
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

#ifndef FD6_PACK_H
#define FD6_PACK_H

#include "a6xx.xml.h"

struct fd_reg_pair {
	uint32_t reg;
	uint64_t value;
	struct fd_bo *bo;
	bool is_address;
	bool bo_write;
	uint32_t bo_offset;
	uint32_t bo_shift;
};

#define __bo_type struct fd_bo *

#include "a6xx-pack.xml.h"
#include "adreno-pm4-pack.xml.h"

#define __assert_eq(a, b)													\
	do {																\
		if ((a) != (b)) {												\
			fprintf(stderr, "assert failed: " #a " (0x%x) != " #b " (0x%x)\n", a, b); \
			assert((a) == (b));											\
		}																\
	} while (0)

#define __ONE_REG(i, ...)											\
	do {															\
		const struct fd_reg_pair regs[] = { __VA_ARGS__ };			\
		/* NOTE: allow regs[0].reg==0, this happens in OUT_PKT() */	\
		if (i < ARRAY_SIZE(regs) && (i == 0 || regs[i].reg > 0)) {	\
			__assert_eq(regs[0].reg + i, regs[i].reg);				\
			if (regs[i].bo) {										\
				struct fd_reloc reloc = {							\
					.bo = regs[i].bo,								\
					.offset = regs[i].bo_offset,					\
					.or = regs[i].value,							\
					.shift = regs[i].bo_shift,						\
					.orhi = regs[i].value >> 32						\
				};													\
				ring->cur = p;										\
				p += 2;												\
				fd_ringbuffer_reloc(ring, &reloc);					\
			} else {												\
				*p++ = regs[i].value;								\
				if (regs[i].is_address)								\
					*p++ = regs[i].value >> 32;						\
			}														\
		}															\
	} while (0)

#define OUT_REG(ring, ...)									\
	do {													\
		const struct fd_reg_pair regs[] = { __VA_ARGS__ };	\
		unsigned count = ARRAY_SIZE(regs);					\
															\
		STATIC_ASSERT(count > 0);							\
		STATIC_ASSERT(count <= 16);							\
															\
		BEGIN_RING(ring, count + 1);						\
		uint32_t *p = ring->cur;							\
		*p++ = CP_TYPE4_PKT | count |						\
			(_odd_parity_bit(count) << 7) |					\
			((regs[0].reg & 0x3ffff) << 8) |				\
			((_odd_parity_bit(regs[0].reg) << 27));			\
															\
		__ONE_REG( 0, __VA_ARGS__);							\
		__ONE_REG( 1, __VA_ARGS__);							\
		__ONE_REG( 2, __VA_ARGS__);							\
		__ONE_REG( 3, __VA_ARGS__);							\
		__ONE_REG( 4, __VA_ARGS__);							\
		__ONE_REG( 5, __VA_ARGS__);							\
		__ONE_REG( 6, __VA_ARGS__);							\
		__ONE_REG( 7, __VA_ARGS__);							\
		__ONE_REG( 8, __VA_ARGS__);							\
		__ONE_REG( 9, __VA_ARGS__);							\
		__ONE_REG(10, __VA_ARGS__);							\
		__ONE_REG(11, __VA_ARGS__);							\
		__ONE_REG(12, __VA_ARGS__);							\
		__ONE_REG(13, __VA_ARGS__);							\
		__ONE_REG(14, __VA_ARGS__);							\
		__ONE_REG(15, __VA_ARGS__);							\
		ring->cur = p;										\
	} while (0)

#define OUT_PKT(ring, opcode, ...)							\
	do {													\
		const struct fd_reg_pair regs[] = { __VA_ARGS__ };	\
		unsigned count = ARRAY_SIZE(regs);					\
															\
		STATIC_ASSERT(count <= 16);							\
															\
		BEGIN_RING(ring, count + 1);						\
		uint32_t *p = ring->cur;							\
		*p++ = CP_TYPE7_PKT | count |						\
			(_odd_parity_bit(count) << 15) |				\
			((opcode & 0x7f) << 16) |						\
			((_odd_parity_bit(opcode) << 23));				\
															\
		__ONE_REG( 0, __VA_ARGS__);							\
		__ONE_REG( 1, __VA_ARGS__);							\
		__ONE_REG( 2, __VA_ARGS__);							\
		__ONE_REG( 3, __VA_ARGS__);							\
		__ONE_REG( 4, __VA_ARGS__);							\
		__ONE_REG( 5, __VA_ARGS__);							\
		__ONE_REG( 6, __VA_ARGS__);							\
		__ONE_REG( 7, __VA_ARGS__);							\
		__ONE_REG( 8, __VA_ARGS__);							\
		__ONE_REG( 9, __VA_ARGS__);							\
		__ONE_REG(10, __VA_ARGS__);							\
		__ONE_REG(11, __VA_ARGS__);							\
		__ONE_REG(12, __VA_ARGS__);							\
		__ONE_REG(13, __VA_ARGS__);							\
		__ONE_REG(14, __VA_ARGS__);							\
		__ONE_REG(15, __VA_ARGS__);							\
		ring->cur = p;										\
	} while (0)

/* similar to OUT_PKT() but appends specified # of dwords
 * copied for buf to the end of the packet (ie. for use-
 * cases like CP_LOAD_STATE)
 */
#define OUT_PKTBUF(ring, opcode, dwords, sizedwords, ...)	\
	do {													\
		const struct fd_reg_pair regs[] = { __VA_ARGS__ };	\
		unsigned count = ARRAY_SIZE(regs);					\
															\
		STATIC_ASSERT(count <= 16);							\
		count += sizedwords;								\
															\
		BEGIN_RING(ring, count + 1);						\
		uint32_t *p = ring->cur;							\
		*p++ = CP_TYPE7_PKT | count |						\
			(_odd_parity_bit(count) << 15) |				\
			((opcode & 0x7f) << 16) |						\
			((_odd_parity_bit(opcode) << 23));				\
															\
		__ONE_REG( 0, __VA_ARGS__);							\
		__ONE_REG( 1, __VA_ARGS__);							\
		__ONE_REG( 2, __VA_ARGS__);							\
		__ONE_REG( 3, __VA_ARGS__);							\
		__ONE_REG( 4, __VA_ARGS__);							\
		__ONE_REG( 5, __VA_ARGS__);							\
		__ONE_REG( 6, __VA_ARGS__);							\
		__ONE_REG( 7, __VA_ARGS__);							\
		__ONE_REG( 8, __VA_ARGS__);							\
		__ONE_REG( 9, __VA_ARGS__);							\
		__ONE_REG(10, __VA_ARGS__);							\
		__ONE_REG(11, __VA_ARGS__);							\
		__ONE_REG(12, __VA_ARGS__);							\
		__ONE_REG(13, __VA_ARGS__);							\
		__ONE_REG(14, __VA_ARGS__);							\
		__ONE_REG(15, __VA_ARGS__);							\
		memcpy(p, dwords, 4 * sizedwords);					\
		p += sizedwords;									\
		ring->cur = p;										\
	} while (0)

#endif /* FD6_PACK_H */
