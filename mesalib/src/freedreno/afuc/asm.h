/*
 * Copyright © 2017 Rob Clark <robdclark@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef _ASM_H_
#define _ASM_H_

#include <stdbool.h>
#include <stdint.h>
#include "afuc.h"

extern int gpuver;

struct asm_label {
   unsigned offset;
   const char *label;
};

struct afuc_instr *next_instr(afuc_opc opc);
void decl_label(const char *str);
void decl_jumptbl(void);
void align_instr(unsigned alignment);
void next_section(void);
void parse_version(struct afuc_instr *instr);

static inline uint32_t
parse_reg(const char *str)
{
   char *retstr;
   long int ret;

   if (!strcmp(str, "$rem"))
      return REG_REM;
   else if (!strcmp(str, "$memdata"))
      return REG_MEMDATA;
   else if (!strcmp(str, "$addr"))
      return REG_ADDR;
   else if (!strcmp(str, "$regdata"))
      return REG_REGDATA;
   else if (!strcmp(str, "$usraddr"))
      return REG_USRADDR;
   else if (!strcmp(str, "$data"))
      return 0x1f;
   else if (!strcmp(str, "$sp"))
      return REG_SP;
   else if (!strcmp(str, "$lr"))
      return REG_LR;

   ret = strtol(str + 1, &retstr, 16);

   if (*retstr != '\0') {
      printf("invalid register: %s\n", str);
      exit(2);
   }

   return ret;
}

static inline uint32_t
parse_literal(const char *str)
{
   char *retstr;
   long int ret;

   ret = strtol(str + 1, &retstr, 16);

   if (*retstr != ']') {
      printf("invalid literal: %s\n", str);
      exit(2);
   }

   return ret;
}

static inline uint32_t
parse_bit(const char *str)
{
   return strtol(str + 1, NULL, 10);
}

unsigned parse_control_reg(const char *name);
unsigned parse_sqe_reg(const char *name);

void yyset_in(FILE *_in_str);

#endif /* _ASM_H_ */
