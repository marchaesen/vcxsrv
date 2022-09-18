/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef ROGUE_REGALLOC_H
#define ROGUE_REGALLOC_H

#include <stdbool.h>
#include <stddef.h>

#include "util/list.h"

/**
 * \brief Register classes used for allocation.
 */
enum rogue_reg_class {
   ROGUE_REG_CLASS_TEMP,
   ROGUE_REG_CLASS_VEC4,

   ROGUE_REG_CLASS_COUNT,
};

/**
 * \brief Register data for each class.
 */
struct rogue_reg_data {
   enum rogue_operand_type type;
   size_t count;
   size_t stride;

   size_t offset;
   struct ra_class *class;
   size_t num_used;
};

/**
 * \brief Register allocation context.
 */
struct rogue_ra {
   struct ra_regs *regs;

   struct rogue_reg_data reg_data[ROGUE_REG_CLASS_COUNT];
};

struct rogue_ra *rogue_ra_init(void *mem_ctx);
bool rogue_ra_alloc(struct list_head *instr_list,
                    struct rogue_ra *ra,
                    size_t *temps_used,
                    size_t *internals_used);

#endif /* ROGUE_REGALLOC_H */
