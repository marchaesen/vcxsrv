/*
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
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

#include "util/u_math.h"
#include "util/register_allocate.h"
#include "util/ralloc.h"
#include "util/bitset.h"

#include "ir3.h"
#include "ir3_compiler.h"
#include "ir3_ra.h"

static void
setup_conflicts(struct ir3_ra_reg_set *set)
{
	unsigned reg;

	reg = 0;
	for (unsigned i = 0; i < class_count; i++) {
		for (unsigned j = 0; j < CLASS_REGS(i); j++) {
			for (unsigned br = j; br < j + class_sizes[i]; br++) {
				ra_add_transitive_reg_conflict(set->regs, br, reg);
			}

			reg++;
		}
	}

	for (unsigned i = 0; i < half_class_count; i++) {
		for (unsigned j = 0; j < HALF_CLASS_REGS(i); j++) {
			for (unsigned br = j; br < j + half_class_sizes[i]; br++) {
				ra_add_transitive_reg_conflict(set->regs,
						br + set->first_half_reg, reg);
			}

			reg++;
		}
	}

	for (unsigned i = 0; i < high_class_count; i++) {
		for (unsigned j = 0; j < HIGH_CLASS_REGS(i); j++) {
			for (unsigned br = j; br < j + high_class_sizes[i]; br++) {
				ra_add_transitive_reg_conflict(set->regs,
						br + set->first_high_reg, reg);
			}

			reg++;
		}
	}

	/*
	 * Setup conflicts with registers over 0x3f for the special vreg
	 * that exists to use as interference for tex-prefetch:
	 */

	for (unsigned i = 0x40; i < CLASS_REGS(0); i++) {
		ra_add_transitive_reg_conflict(set->regs, i,
				set->prefetch_exclude_reg);
	}

	for (unsigned i = 0x40; i < HALF_CLASS_REGS(0); i++) {
		ra_add_transitive_reg_conflict(set->regs, i + set->first_half_reg,
				set->prefetch_exclude_reg);
	}
}

/* One-time setup of RA register-set, which describes all the possible
 * "virtual" registers and their interferences.  Ie. double register
 * occupies (and conflicts with) two single registers, and so forth.
 * Since registers do not need to be aligned to their class size, they
 * can conflict with other registers in the same class too.  Ie:
 *
 *    Single (base) |  Double
 *    --------------+---------------
 *       R0         |  D0
 *       R1         |  D0 D1
 *       R2         |     D1 D2
 *       R3         |        D2
 *           .. and so on..
 *
 * (NOTE the disassembler uses notation like r0.x/y/z/w but those are
 * really just four scalar registers.  Don't let that confuse you.)
 */
struct ir3_ra_reg_set *
ir3_ra_alloc_reg_set(struct ir3_compiler *compiler, bool mergedregs)
{
	struct ir3_ra_reg_set *set = rzalloc(compiler, struct ir3_ra_reg_set);
	unsigned ra_reg_count, reg, base;

	/* calculate # of regs across all classes: */
	ra_reg_count = 0;
	for (unsigned i = 0; i < class_count; i++)
		ra_reg_count += CLASS_REGS(i);
	for (unsigned i = 0; i < half_class_count; i++)
		ra_reg_count += HALF_CLASS_REGS(i);
	for (unsigned i = 0; i < high_class_count; i++)
		ra_reg_count += HIGH_CLASS_REGS(i);

	ra_reg_count += 1;   /* for tex-prefetch excludes */

	/* allocate the reg-set.. */
	set->regs = ra_alloc_reg_set(set, ra_reg_count, true);
	set->ra_reg_to_gpr = ralloc_array(set, uint16_t, ra_reg_count);
	set->gpr_to_ra_reg = ralloc_array(set, uint16_t *, total_class_count);

	/* .. and classes */
	reg = 0;
	for (unsigned i = 0; i < class_count; i++) {
		set->classes[i] = ra_alloc_reg_class(set->regs);

		set->gpr_to_ra_reg[i] = ralloc_array(set, uint16_t, CLASS_REGS(i));

		for (unsigned j = 0; j < CLASS_REGS(i); j++) {
			ra_class_add_reg(set->regs, set->classes[i], reg);

			set->ra_reg_to_gpr[reg] = j;
			set->gpr_to_ra_reg[i][j] = reg;

			reg++;
		}
	}

	set->first_half_reg = reg;
	base = HALF_OFFSET;

	for (unsigned i = 0; i < half_class_count; i++) {
		set->half_classes[i] = ra_alloc_reg_class(set->regs);

		set->gpr_to_ra_reg[base + i] =
				ralloc_array(set, uint16_t, HALF_CLASS_REGS(i));

		for (unsigned j = 0; j < HALF_CLASS_REGS(i); j++) {
			ra_class_add_reg(set->regs, set->half_classes[i], reg);

			set->ra_reg_to_gpr[reg] = j;
			set->gpr_to_ra_reg[base + i][j] = reg;

			reg++;
		}
	}

	set->first_high_reg = reg;
	base = HIGH_OFFSET;

	for (unsigned i = 0; i < high_class_count; i++) {
		set->high_classes[i] = ra_alloc_reg_class(set->regs);

		set->gpr_to_ra_reg[base + i] =
				ralloc_array(set, uint16_t, HIGH_CLASS_REGS(i));

		for (unsigned j = 0; j < HIGH_CLASS_REGS(i); j++) {
			ra_class_add_reg(set->regs, set->high_classes[i], reg);

			set->ra_reg_to_gpr[reg] = j;
			set->gpr_to_ra_reg[base + i][j] = reg;

			reg++;
		}
	}

	/*
	 * Setup an additional class, with one vreg, to simply conflict
	 * with registers that are too high to encode tex-prefetch.  This
	 * vreg is only used to setup additional conflicts so that RA
	 * knows to allocate prefetch dst regs below the limit:
	 */
	set->prefetch_exclude_class = ra_alloc_reg_class(set->regs);
	ra_class_add_reg(set->regs, set->prefetch_exclude_class, reg);
	set->prefetch_exclude_reg = reg++;

	/*
	 * And finally setup conflicts.  Starting a6xx, half precision regs
	 * conflict w/ full precision regs (when using MERGEDREGS):
	 */
	if (mergedregs) {
		for (unsigned i = 0; i < CLASS_REGS(0) / 2; i++) {
			unsigned freg  = set->gpr_to_ra_reg[0][i];
			unsigned hreg0 = set->gpr_to_ra_reg[0 + HALF_OFFSET][(i * 2) + 0];
			unsigned hreg1 = set->gpr_to_ra_reg[0 + HALF_OFFSET][(i * 2) + 1];

			ra_add_transitive_reg_pair_conflict(set->regs, freg, hreg0, hreg1);
		}
	}

	setup_conflicts(set);

	ra_set_finalize(set->regs, NULL);

	return set;
}

int
ra_size_to_class(unsigned sz, bool half, bool high)
{
	if (high) {
		for (unsigned i = 0; i < high_class_count; i++)
			if (high_class_sizes[i] >= sz)
				return i + HIGH_OFFSET;
	} else if (half) {
		for (unsigned i = 0; i < half_class_count; i++)
			if (half_class_sizes[i] >= sz)
				return i + HALF_OFFSET;
	} else {
		for (unsigned i = 0; i < class_count; i++)
			if (class_sizes[i] >= sz)
				return i;
	}
	debug_assert(0);
	return -1;
}

int
ra_class_to_size(unsigned class, bool *half, bool *high)
{
	*half = *high = false;

	if (class >= HIGH_OFFSET) {
		*high = true;
		return high_class_sizes[class - HIGH_OFFSET];
	} else if (class >= HALF_OFFSET) {
		*half = true;
		return half_class_sizes[class - HALF_OFFSET];
	} else {
		return class_sizes[class];
	}
}
