/*
 * Copyright © 2020 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef __IR3_ASM_H__
#define __IR3_ASM_H__

#include "main.h"

#include "ir3/ir3_parser.h"
#include "ir3/ir3_shader.h"

struct ir3_kernel {
   struct kernel base;
   struct ir3_kernel_info info;
   struct backend *backend;
   struct ir3_shader_variant *v;
   void *bin;
};
define_cast(kernel, ir3_kernel);

struct ir3_kernel *ir3_asm_assemble(struct ir3_compiler *c, FILE *in);
void ir3_asm_disassemble(struct ir3_kernel *k, FILE *out);

#endif /* __IR3_ASM_H__ */
