#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

void disassemble_midgard(FILE *fp, const void *code, size_t size, unsigned gpu_id,
                         bool verbose);
