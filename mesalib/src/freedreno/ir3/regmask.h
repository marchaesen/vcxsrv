/*
 * Copyright (c) 2013 Rob Clark <robdclark@gmail.com>
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

#ifndef REGMASK_H_
#define REGMASK_H_

#include <string.h>
#include "util/bitset.h"

#define MAX_REG 256

typedef BITSET_DECLARE(regmaskstate_t, 2 * MAX_REG);

typedef struct {
	bool mergedregs;
	regmaskstate_t mask;
} regmask_t;

static inline bool
__regmask_get(regmask_t *regmask, bool half, unsigned n)
{
	if (regmask->mergedregs) {
		/* a6xx+ case, with merged register file, we track things in terms
		 * of half-precision registers, with a full precisions register
		 * using two half-precision slots:
		 */
		if (half) {
			return BITSET_TEST(regmask->mask, n);
		} else {
			n *= 2;
			return BITSET_TEST(regmask->mask, n) ||
				BITSET_TEST(regmask->mask, n+1);
		}
	} else {
		/* pre a6xx case, with separate register file for half and full
		 * precision:
		 */
		if (half)
			n += MAX_REG;
		return BITSET_TEST(regmask->mask, n);
	}
}

static inline void
__regmask_set(regmask_t *regmask, bool half, unsigned n)
{
	if (regmask->mergedregs) {
		/* a6xx+ case, with merged register file, we track things in terms
		 * of half-precision registers, with a full precisions register
		 * using two half-precision slots:
		 */
		if (half) {
			BITSET_SET(regmask->mask, n);
		} else {
			n *= 2;
			BITSET_SET(regmask->mask, n);
			BITSET_SET(regmask->mask, n+1);
		}
	} else {
		/* pre a6xx case, with separate register file for half and full
		 * precision:
		 */
		if (half)
			n += MAX_REG;
		BITSET_SET(regmask->mask, n);
	}
}

static inline void
__regmask_clear(regmask_t *regmask, bool half, unsigned n)
{
	if (regmask->mergedregs) {
		/* a6xx+ case, with merged register file, we track things in terms
		 * of half-precision registers, with a full precisions register
		 * using two half-precision slots:
		 */
		if (half) {
			BITSET_CLEAR(regmask->mask, n);
		} else {
			n *= 2;
			BITSET_CLEAR(regmask->mask, n);
			BITSET_CLEAR(regmask->mask, n+1);
		}
	} else {
		/* pre a6xx case, with separate register file for half and full
		 * precision:
		 */
		if (half)
			n += MAX_REG;
		BITSET_CLEAR(regmask->mask, n);
	}
}

static inline void
regmask_init(regmask_t *regmask, bool mergedregs)
{
	memset(&regmask->mask, 0, sizeof(regmask->mask));
	regmask->mergedregs = mergedregs;
}

static inline void
regmask_or(regmask_t *dst, regmask_t *a, regmask_t *b)
{
	assert(dst->mergedregs == a->mergedregs);
	assert(dst->mergedregs == b->mergedregs);

	for (unsigned i = 0; i < ARRAY_SIZE(dst->mask); i++)
		dst->mask[i] = a->mask[i] | b->mask[i];
}

#endif /* REGMASK_H_ */
