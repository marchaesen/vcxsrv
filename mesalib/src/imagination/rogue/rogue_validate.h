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

#ifndef ROGUE_VALIDATE_H
#define ROGUE_VALIDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "rogue_instr.h"
#include "rogue_operand.h"
#include "rogue_shader.h"
#include "util/macros.h"

/**
 * \brief Register rule description.
 */
struct rogue_register_rule {
   enum rogue_register_access access;
   size_t max;
   enum rogue_register_modifier modifiers;
};

/**
 * \brief Instruction operand rule description.
 */
struct rogue_instr_operand_rule {
   uint64_t mask;
   ssize_t min;
   ssize_t max;
   ssize_t align;
};

/**
 * \brief Instruction rule description.
 */
struct rogue_instr_rule {
   uint64_t flags; /** A mask of #rogue_instr_flag values. */
   size_t num_operands;
   struct rogue_instr_operand_rule *operand_rules;
};

PUBLIC
bool rogue_validate_operand(const struct rogue_operand *operand);

PUBLIC
bool rogue_validate_instr(const struct rogue_instr *instr);

PUBLIC
bool rogue_validate_shader(const struct rogue_shader *shader);

#endif /* ROGUE_VALIDATE_H */
