/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 */

#ifndef DISASM_H_
#define DISASM_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "compiler/shader_enums.h"

/* bitmask of debug flags */
enum debug_t {
   PRINT_RAW = 0x1, /* dump raw hexdump */
   PRINT_VERBOSE = 0x2,
   PRINT_STATS = 0x4,
   EXPAND_REPEAT = 0x8,
};

struct shader_stats {
   /* instructions counts rpnN, and instlen does not */
   int instructions, instlen;
   int nops;
   int ss, sy;
   int constlen;
   int halfreg;
   int fullreg;
   uint16_t sstall;
   uint16_t mov_count;
   uint16_t cov_count;
   uint16_t last_baryf;
   uint16_t instrs_per_cat[8];
};

int disasm_a2xx(uint32_t *dwords, int sizedwords, int level,
                gl_shader_stage type);
int disasm_a3xx(uint32_t *dwords, int sizedwords, int level, FILE *out,
                unsigned gpu_id);
int disasm_a3xx_stat(uint32_t *dwords, int sizedwords, int level, FILE *out,
                     unsigned gpu_id, struct shader_stats *stats);
int try_disasm_a3xx(uint32_t *dwords, int sizedwords, int level, FILE *out,
                    unsigned gpu_id);

void disasm_a2xx_set_debug(enum debug_t debug);
void disasm_a3xx_set_debug(enum debug_t debug);

#endif /* DISASM_H_ */
