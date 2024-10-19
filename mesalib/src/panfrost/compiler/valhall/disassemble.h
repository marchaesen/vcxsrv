#ifndef __DISASM_H
#define __DISASM_H

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void va_disasm_instr(FILE *fp, uint64_t instr);
void disassemble_valhall(FILE *fp, const void *code, size_t size, bool verbose);

#endif
