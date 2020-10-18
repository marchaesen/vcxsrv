/*
 * Copyright (C) 2019 Connor Abbott <cwabbott0@gmail.com>
 * Copyright (C) 2019 Lyude Paul <thatslyude@gmail.com>
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
 * Copyright (C) 2020 Collabora Ltd.
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

#ifndef __BI_PRINT_H
#define __BI_PRINT_H

#include <stdio.h>
#include "bifrost.h"
#include "compiler.h"

const char * bi_message_type_name(enum bifrost_message_type T);
const char * bi_output_mod_name(enum bifrost_outmod mod);
const char * bi_minmax_mode_name(enum bifrost_minmax_mode mod);
const char * bi_round_mode_name(enum bifrost_roundmode mod);
const char * bi_interp_mode_name(enum bifrost_interp_mode mode);
const char * bi_class_name(enum bi_class cl);
const char * bi_cond_name(enum bi_cond cond);
const char * bi_special_op_name(enum bi_special_op op);
const char * bi_table_op_name(enum bi_table_op op);
const char * bi_reduce_op_name(enum bi_reduce_op op);
const char * bi_frexp_op_name(enum bi_frexp_op op);

void bi_print_instruction(bi_instruction *ins, FILE *fp);
void bi_print_slots(bi_registers *regs, FILE *fp);
void bi_print_bundle(bi_bundle *bundle, FILE *fp);
void bi_print_clause(bi_clause *clause, FILE *fp);
void bi_print_block(bi_block *block, FILE *fp);
void bi_print_shader(bi_context *ctx, FILE *fp);

#endif
