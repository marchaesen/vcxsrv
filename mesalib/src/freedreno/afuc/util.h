/*
 * Copyright © 2021 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdbool.h>

/*
 * AFUC disasm / asm helpers
 */

unsigned afuc_control_reg(const char *name);
char * afuc_control_reg_name(unsigned id);

unsigned afuc_sqe_reg(const char *name);
char * afuc_sqe_reg_name(unsigned id);

unsigned afuc_pipe_reg(const char *name);
char * afuc_pipe_reg_name(unsigned id);
bool afuc_pipe_reg_is_void(unsigned id);

unsigned afuc_gpu_reg(const char *name);
char * afuc_gpu_reg_name(unsigned id);

unsigned afuc_gpr_reg(const char *name);

int afuc_pm4_id(const char *name);
const char * afuc_pm_id_name(unsigned id);

enum afuc_color {
   AFUC_ERR,
   AFUC_LBL,
};

void afuc_printc(enum afuc_color c, const char *fmt, ...);

enum afuc_fwid {
   AFUC_A730 = 0x730,
   AFUC_A740 = 0x740,
   AFUC_A750 = 0x520,

   AFUC_A630 = 0x6ee,
   AFUC_A650 = 0x6dc,
   AFUC_A660 = 0x6dd,

   AFUC_A530 = 0x5ff,
};

static inline enum afuc_fwid
afuc_get_fwid(uint32_t first_dword)
{
   /* The firmware ID is in bits 12-24 of the first dword */
   return (first_dword >> 12) & 0xfff;
}

int afuc_util_init(enum afuc_fwid fw_id, int *gpuver, bool colors);

#endif /* _UTIL_H_ */
