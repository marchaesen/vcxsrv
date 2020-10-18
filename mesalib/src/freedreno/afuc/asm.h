/*
 * Copyright (c) 2017 Rob Clark <robdclark@gmail.com>
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

#ifndef _ASM_H_
#define _ASM_H_

#include <stdint.h>
#include <stdbool.h>
#include "afuc.h"

extern int gpuver;

/**
 * Intermediate representation for an instruction, before final encoding.
 * This mostly exists because we need to resolve label offset's in a 2nd
 * pass, but also so that parser.y doesn't really need to care so much
 * about the different encodings for 2src regs vs 1src+immed, or mnemonics
 */
struct asm_instruction {
	int tok;
	int dst;
	int src1;
	int src2;
	int immed;
	int shift;
	int bit;
	int xmov;
	uint32_t literal;
	const char *label;

	bool has_immed : 1;
	bool has_shift : 1;
	bool has_bit   : 1;
	bool is_literal : 1;
	bool rep        : 1;
};

struct asm_label {
	unsigned offset;
	const char *label;
};

struct asm_instruction *next_instr(int tok);
void decl_label(const char *str);


static inline uint32_t
parse_reg(const char *str)
{
	char *retstr;
	long int ret;

	if (!strcmp(str, "$rem"))
		return 0x1c;
	else if (!strcmp(str, "$addr"))
		return 0x1d;
	else if (!strcmp(str, "$addr2"))
		return 0x1e;
	else if (!strcmp(str, "$data"))
		return 0x1f;

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

/* string trailing ':' off label: */
static inline const char *
parse_label_decl(const char *str)
{
	char *s = strdup(str);
	s[strlen(s) - 1] = '\0';
	return s;
}

void yyset_in (FILE *  _in_str );


#endif /* _ASM_H_ */
