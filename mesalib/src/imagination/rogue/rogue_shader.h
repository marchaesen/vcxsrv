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

#ifndef ROGUE_SHADER_H
#define ROGUE_SHADER_H

#include <stdbool.h>
#include <stddef.h>

#include "compiler/shader_enums.h"
#include "rogue_instr.h"
#include "rogue_operand.h"
#include "rogue_util.h"
#include "util/list.h"
#include "util/macros.h"

struct rogue_build_ctx;
struct rogue_ra;

/**
 * \brief Shader description.
 */
struct rogue_shader {
   gl_shader_stage stage; /** Shader stage. */

   struct list_head instr_list; /** Instructions linked list. */

   struct rogue_build_ctx *ctx;
   struct rogue_ra *ra;

   bool drc_used[ROGUE_NUM_DRCS];
};

/* Shader instruction list iterators and helpers. */
#define foreach_instr(__instr, __list) \
   list_for_each_entry (struct rogue_instr, __instr, __list, node)
#define foreach_instr_rev(__instr, __list) \
   list_for_each_entry_rev (struct rogue_instr, __instr, __list, node)
#define foreach_instr_safe(__instr, __list) \
   list_for_each_entry_safe (struct rogue_instr, __instr, __list, node)

#define instr_first_entry(__list) \
   list_first_entry(__list, struct rogue_instr, node)
#define instr_last_entry(__list) \
   list_last_entry(__list, struct rogue_instr, node)

size_t rogue_shader_instr_count_type(const struct rogue_shader *shader,
                                     enum rogue_opcode opcode);

PUBLIC
struct rogue_shader *rogue_shader_create(struct rogue_build_ctx *ctx,
                                         gl_shader_stage stage);

PUBLIC
struct rogue_instr *rogue_shader_insert(struct rogue_shader *shader,
                                        enum rogue_opcode opcode);

size_t rogue_acquire_drc(struct rogue_shader *shader);
void rogue_release_drc(struct rogue_shader *shader, size_t drc);

#endif /* ROGUE_SHADER_H */
