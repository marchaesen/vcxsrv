/*
 * Copyright © 2020 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef __IR3_ASSEMBLER_H__
#define __IR3_ASSEMBLER_H__

#include <stdint.h>
#include <stdio.h>

#define MAX_BUFS 4

#ifdef __cplusplus
extern "C" {
#endif

struct ir3_kernel_info {
   uint32_t num_bufs;
   uint32_t buf_sizes[MAX_BUFS]; /* size in dwords */
   uint32_t buf_addr_regs[MAX_BUFS];
   uint32_t *buf_init_data[MAX_BUFS];
   uint32_t buf_init_data_sizes[MAX_BUFS];

   uint64_t shader_print_buffer_iova;

   /* driver-param / replaced uniforms: */
   unsigned numwg;
   unsigned wgid;
   unsigned early_preamble;
};

struct ir3_shader;
struct ir3_compiler;

struct ir3_shader *ir3_parse_asm(struct ir3_compiler *c,
                                 struct ir3_kernel_info *info, FILE *in);

#ifdef __cplusplus
}
#endif

#endif /* __IR3_ASSEMBLER_H__ */
