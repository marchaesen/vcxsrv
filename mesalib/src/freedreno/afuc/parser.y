/*
 * Copyright (c) 2013 Rob Clark <robclark@freedesktop.org>
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

%{
#define YYDEBUG 0

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "asm.h"


int yyget_lineno(void);

#ifdef YYDEBUG
int yydebug;
#endif

extern int yylex(void);
typedef void *YY_BUFFER_STATE;
extern YY_BUFFER_STATE yy_scan_string(const char *);
extern void yy_delete_buffer(YY_BUFFER_STATE);

int yyparse(void);

void yyerror(const char *error);
void yyerror(const char *error)
{
	fprintf(stderr, "error at line %d: %s\n", yyget_lineno(), error);
}

static struct asm_instruction *instr;   /* current instruction */

static void
new_instr(int tok)
{
	instr = next_instr(tok);
}

static void
dst(int num)
{
	instr->dst = num;
}

static void
src1(int num)
{
	instr->src1 = num;
}

static void
src2(int num)
{
	instr->src2 = num;
}

static void
immed(int num)
{
	instr->immed = num;
	instr->has_immed = true;
}

static void
shift(int num)
{
	instr->shift = num;
	instr->has_shift = true;
}

static void
bit(int num)
{
	instr->bit = num;
	instr->has_bit = true;
}

static void
literal(uint32_t num)
{
	instr->literal = num;
	instr->is_literal = true;
}

static void
label(const char *str)
{
	instr->label = str;
}

%}

%union {
	int tok;
	uint32_t num;
	const char *str;
}

%token <num> T_INT
%token <num> T_HEX
%token <num> T_CONTROL_REG
%token <str> T_LABEL_DECL
%token <str> T_LABEL_REF
%token <num> T_LITERAL
%token <num> T_BIT
%token <num> T_REGISTER

%token <tok> T_OP_NOP
%token <tok> T_OP_ADD
%token <tok> T_OP_ADDHI
%token <tok> T_OP_SUB
%token <tok> T_OP_SUBHI
%token <tok> T_OP_AND
%token <tok> T_OP_OR
%token <tok> T_OP_XOR
%token <tok> T_OP_NOT
%token <tok> T_OP_SHL
%token <tok> T_OP_USHR
%token <tok> T_OP_ISHR
%token <tok> T_OP_ROT
%token <tok> T_OP_MUL8
%token <tok> T_OP_MIN
%token <tok> T_OP_MAX
%token <tok> T_OP_CMP
%token <tok> T_OP_MSB
%token <tok> T_OP_MOV
%token <tok> T_OP_CWRITE
%token <tok> T_OP_CREAD
%token <tok> T_OP_STORE
%token <tok> T_OP_LOAD
%token <tok> T_OP_BRNE
%token <tok> T_OP_BREQ
%token <tok> T_OP_RET
%token <tok> T_OP_IRET
%token <tok> T_OP_CALL
%token <tok> T_OP_JUMP
%token <tok> T_OP_WAITIN
%token <tok> T_OP_PREEMPTLEAVE
%token <tok> T_OP_SETSECURE
%token <tok> T_LSHIFT
%token <tok> T_REP
%token <num> T_XMOV

%type <num> reg
%type <num> immediate

%error-verbose

%start instrs

%%

instrs:            instr_or_label instrs
|                  instr_or_label

instr_or_label:    instr_r
|                  T_REP instr_r    { instr->rep = true; }
|                  branch_instr
|                  other_instr
|                  T_LABEL_DECL   { decl_label($1); }

/* instructions that can optionally have (rep) flag: */
instr_r:           alu_instr
|                  T_XMOV alu_instr { instr->xmov = $1; }
|                  config_instr

/* need to special case:
 * - not (single src, possibly an immediate)
 * - msb (single src, must be reg)
 * - mov (single src, plus possibly a shift)
 * from the other ALU instructions:
 */

alu_msb_instr:     T_OP_MSB reg ',' reg        { new_instr($1); dst($2); src2($4); }

alu_not_instr:     T_OP_NOT reg ',' reg        { new_instr($1); dst($2); src2($4); }
|                  T_OP_NOT reg ',' immediate  { new_instr($1); dst($2); immed($4); }

alu_mov_instr:     T_OP_MOV reg ',' reg        { new_instr($1); dst($2); src1($4); }
|                  T_OP_MOV reg ',' immediate T_LSHIFT immediate {
                       new_instr($1); dst($2); immed($4); shift($6);
}
|                  T_OP_MOV reg ',' immediate  { new_instr($1); dst($2); immed($4); }
|                  T_OP_MOV reg ',' T_LABEL_REF T_LSHIFT immediate {
                       new_instr($1); dst($2); label($4); shift($6);
}
|                  T_OP_MOV reg ',' T_LABEL_REF { new_instr($1); dst($2); label($4); }

alu_2src_op:       T_OP_ADD       { new_instr($1); }
|                  T_OP_ADDHI     { new_instr($1); }
|                  T_OP_SUB       { new_instr($1); }
|                  T_OP_SUBHI     { new_instr($1); }
|                  T_OP_AND       { new_instr($1); }
|                  T_OP_OR        { new_instr($1); }
|                  T_OP_XOR       { new_instr($1); }
|                  T_OP_SHL       { new_instr($1); }
|                  T_OP_USHR      { new_instr($1); }
|                  T_OP_ISHR      { new_instr($1); }
|                  T_OP_ROT       { new_instr($1); }
|                  T_OP_MUL8      { new_instr($1); }
|                  T_OP_MIN       { new_instr($1); }
|                  T_OP_MAX       { new_instr($1); }
|                  T_OP_CMP       { new_instr($1); }

alu_2src_instr:    alu_2src_op reg ',' reg ',' reg { dst($2); src1($4); src2($6); }
|                  alu_2src_op reg ',' reg ',' immediate { dst($2); src1($4); immed($6); }

alu_instr:         alu_2src_instr
|                  alu_msb_instr
|                  alu_not_instr
|                  alu_mov_instr

config_op:         T_OP_CWRITE    { new_instr($1); }
|                  T_OP_CREAD     { new_instr($1); }
|                  T_OP_LOAD      { new_instr($1); }
|                  T_OP_STORE     { new_instr($1); }

config_instr:      config_op reg ',' '[' reg '+' immediate ']' ',' immediate {
                       src1($2); src2($5); immed($7); bit($10);
}

branch_op:         T_OP_BRNE      { new_instr($1); }
|                  T_OP_BREQ      { new_instr($1); }

branch_instr:      branch_op reg ',' T_BIT ',' T_LABEL_REF     { src1($2); bit($4); label($6); }
|                  branch_op reg ',' immediate ',' T_LABEL_REF { src1($2); immed($4); label($6); }

other_instr:       T_OP_CALL T_LABEL_REF { new_instr($1); label($2); }
|                  T_OP_PREEMPTLEAVE T_LABEL_REF { new_instr($1); label($2); }
|                  T_OP_SETSECURE reg ',' T_LABEL_REF { new_instr($1); src1($2); label($4); }
|                  T_OP_RET              { new_instr($1); }
|                  T_OP_IRET             { new_instr($1); }
|                  T_OP_JUMP T_LABEL_REF { new_instr($1); label($2); }
|                  T_OP_WAITIN           { new_instr($1); }
|                  T_OP_NOP              { new_instr($1); }
|                  T_LITERAL             { new_instr($1); literal($1); }

reg:               T_REGISTER

immediate:         T_HEX
|                  T_INT
|                  T_CONTROL_REG
|                  T_CONTROL_REG '+' immediate { $$ = $1 + $3; }

