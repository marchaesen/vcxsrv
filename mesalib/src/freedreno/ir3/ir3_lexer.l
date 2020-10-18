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
#include <stdlib.h>
#include "ir3/ir3.h"
#include "ir3_parser.h"

#define YY_NO_INPUT
#define YY_NO_UNPUT
#define TOKEN(t) (ir3_yylval.tok = t)
extern YYSTYPE ir3_yylval;

static int parse_wrmask(const char *src)
{
	int i, num = 0;
	for (i = 0; i < 4; i++) {
		if ("xyzw"[i] == src[1]) {
			num |= (1 << i);
			src++;
		}
	}
	return num;
}

static int parse_reg(const char *str)
{
	int num = 0;
	if (str[0] == 'h') {
		str++;
		num++;
	}
	str++;
	num += strtol(str, (char **)&str, 10) << 3;
	switch (str[1]) {
	case 'x': num += 0; break;
	case 'y': num += 2; break;
	case 'z': num += 4; break;
	case 'w': num += 6; break;
	default: assert(0); break;
	}
	return num;
}
%}

%option noyywrap
%option prefix="ir3_yy"

%%
"\n"                              yylineno++;
[ \t]                             ; /* ignore whitespace */
";"[^\n]*"\n"                     yylineno++; /* ignore comments */
[0-9]+"."[0-9]+                   ir3_yylval.flt = strtod(yytext, NULL);       return T_FLOAT;
[0-9]*                            ir3_yylval.num = strtoul(yytext, NULL, 0);    return T_INT;
"0x"[0-9a-fA-F]*                  ir3_yylval.num = strtoul(yytext, NULL, 0);    return T_HEX;
"@localsize"                      return TOKEN(T_A_LOCALSIZE);
"@const"                          return TOKEN(T_A_CONST);
"@buf"                            return TOKEN(T_A_BUF);
"@invocationid"                   return TOKEN(T_A_INVOCATIONID);
"@wgid"                           return TOKEN(T_A_WGID);
"@numwg"                          return TOKEN(T_A_NUMWG);
"@in"                             return TOKEN(T_A_IN);
"@out"                            return TOKEN(T_A_OUT);
"@tex"                            return TOKEN(T_A_TEX);
"(sy)"                            return TOKEN(T_SY);
"(ss)"                            return TOKEN(T_SS);
"(absneg)"                        return TOKEN(T_ABSNEG);
"(neg)"                           return TOKEN(T_NEG);
"(abs)"                           return TOKEN(T_ABS);
"(r)"                             return TOKEN(T_R);
"(ul)"                            return TOKEN(T_UL);
"(even)"                          return TOKEN(T_EVEN);
"(pos_infinity)"                  return TOKEN(T_POS_INFINITY);
"(ei)"                            return TOKEN(T_EI);
"(jp)"                            return TOKEN(T_JP);
"(rpt"[0-7]")"                    ir3_yylval.num = strtol(yytext+4, NULL, 10); return T_RPT;
"(nop"[0-7]")"                    ir3_yylval.num = strtol(yytext+4, NULL, 10); return T_NOP;
"("[x]?[y]?[z]?[w]?")"            ir3_yylval.num = parse_wrmask(yytext); return T_WRMASK;

[h]?"r"[0-9]+"."[xyzw]            ir3_yylval.num = parse_reg(yytext); return T_REGISTER;
[h]?"c"[0-9]+"."[xyzw]            ir3_yylval.num = parse_reg(yytext); return T_CONSTANT;
"a0."[xyzw]                       ir3_yylval.num = parse_reg(yytext); return T_A0;
"p0."[xyzw]                       ir3_yylval.num = parse_reg(yytext); return T_P0;
"s#"[0-9]+                        ir3_yylval.num = strtol(yytext+2, NULL, 10); return T_SAMP;
"t#"[0-9]+                        ir3_yylval.num = strtol(yytext+2, NULL, 10); return T_TEX;

                                  /* category 0: */
"nop"                             return TOKEN(T_OP_NOP);
"br"                              return TOKEN(T_OP_BR);
"jump"                            return TOKEN(T_OP_JUMP);
"call"                            return TOKEN(T_OP_CALL);
"ret"                             return TOKEN(T_OP_RET);
"kill"                            return TOKEN(T_OP_KILL);
"end"                             return TOKEN(T_OP_END);
"emit"                            return TOKEN(T_OP_EMIT);
"cut"                             return TOKEN(T_OP_CUT);
"chmask"                          return TOKEN(T_OP_CHMASK);
"chsh"                            return TOKEN(T_OP_CHSH);
"flow_rev"                        return TOKEN(T_OP_FLOW_REV);

                                  /* category 1: */
"mova"                            return TOKEN(T_OP_MOVA);
"mov"                             return TOKEN(T_OP_MOV);
"cov"                             return TOKEN(T_OP_COV);

("f16"|"f32"|"u16"|"u32"|"s16"|"s32"|"u8"|"s8"){2} ir3_yylval.str = yytext; return T_CAT1_TYPE_TYPE;

                                  /* category 2: */
"add.f"                           return TOKEN(T_OP_ADD_F);
"min.f"                           return TOKEN(T_OP_MIN_F);
"max.f"                           return TOKEN(T_OP_MAX_F);
"mul.f"                           return TOKEN(T_OP_MUL_F);
"sign.f"                          return TOKEN(T_OP_SIGN_F);
"cmps.f"                          return TOKEN(T_OP_CMPS_F);
"absneg.f"                        return TOKEN(T_OP_ABSNEG_F);
"cmpv.f"                          return TOKEN(T_OP_CMPV_F);
"floor.f"                         return TOKEN(T_OP_FLOOR_F);
"ceil.f"                          return TOKEN(T_OP_CEIL_F);
"rndne.f"                         return TOKEN(T_OP_RNDNE_F);
"rndaz.f"                         return TOKEN(T_OP_RNDAZ_F);
"trunc.f"                         return TOKEN(T_OP_TRUNC_F);
"add.u"                           return TOKEN(T_OP_ADD_U);
"add.s"                           return TOKEN(T_OP_ADD_S);
"sub.u"                           return TOKEN(T_OP_SUB_U);
"sub.s"                           return TOKEN(T_OP_SUB_S);
"cmps.u"                          return TOKEN(T_OP_CMPS_U);
"cmps.s"                          return TOKEN(T_OP_CMPS_S);
"min.u"                           return TOKEN(T_OP_MIN_U);
"min.s"                           return TOKEN(T_OP_MIN_S);
"max.u"                           return TOKEN(T_OP_MAX_U);
"max.s"                           return TOKEN(T_OP_MAX_S);
"absneg.s"                        return TOKEN(T_OP_ABSNEG_S);
"and.b"                           return TOKEN(T_OP_AND_B);
"or.b"                            return TOKEN(T_OP_OR_B);
"not.b"                           return TOKEN(T_OP_NOT_B);
"xor.b"                           return TOKEN(T_OP_XOR_B);
"cmpv.u"                          return TOKEN(T_OP_CMPV_U);
"cmpv.s"                          return TOKEN(T_OP_CMPV_S);
"mul.u24"                         return TOKEN(T_OP_MUL_U24);
"mul.s24"                         return TOKEN(T_OP_MUL_S24);
"mull.u"                          return TOKEN(T_OP_MULL_U);
"bfrev.b"                         return TOKEN(T_OP_BFREV_B);
"clz.s"                           return TOKEN(T_OP_CLZ_S);
"clz.b"                           return TOKEN(T_OP_CLZ_B);
"shl.b"                           return TOKEN(T_OP_SHL_B);
"shr.b"                           return TOKEN(T_OP_SHR_B);
"ashr.b"                          return TOKEN(T_OP_ASHR_B);
"bary.f"                          return TOKEN(T_OP_BARY_F);
"mgen.b"                          return TOKEN(T_OP_MGEN_B);
"getbit.b"                        return TOKEN(T_OP_GETBIT_B);
"setrm"                           return TOKEN(T_OP_SETRM);
"cbits.b"                         return TOKEN(T_OP_CBITS_B);
"shb"                             return TOKEN(T_OP_SHB);
"msad"                            return TOKEN(T_OP_MSAD);

                                  /* category 3: */
"mad.u16"                         return TOKEN(T_OP_MAD_U16);
"madsh.u16"                       return TOKEN(T_OP_MADSH_U16);
"mad.s16"                         return TOKEN(T_OP_MAD_S16);
"madsh.m16"                       return TOKEN(T_OP_MADSH_M16);
"mad.u24"                         return TOKEN(T_OP_MAD_U24);
"mad.s24"                         return TOKEN(T_OP_MAD_S24);
"mad.f16"                         return TOKEN(T_OP_MAD_F16);
"mad.f32"                         return TOKEN(T_OP_MAD_F32);
"sel.b16"                         return TOKEN(T_OP_SEL_B16);
"sel.b32"                         return TOKEN(T_OP_SEL_B32);
"sel.s16"                         return TOKEN(T_OP_SEL_S16);
"sel.s32"                         return TOKEN(T_OP_SEL_S32);
"sel.f16"                         return TOKEN(T_OP_SEL_F16);
"sel.f32"                         return TOKEN(T_OP_SEL_F32);
"sad.s16"                         return TOKEN(T_OP_SAD_S16);
"sad.s32"                         return TOKEN(T_OP_SAD_S32);

                                  /* category 4: */
"rcp"                             return TOKEN(T_OP_RCP);
"rsq"                             return TOKEN(T_OP_RSQ);
"log2"                            return TOKEN(T_OP_LOG2);
"exp2"                            return TOKEN(T_OP_EXP2);
"sin"                             return TOKEN(T_OP_SIN);
"cos"                             return TOKEN(T_OP_COS);
"sqrt"                            return TOKEN(T_OP_SQRT);
"hrsq"                            return TOKEN(T_OP_HRSQ);
"hlog2"                           return TOKEN(T_OP_HLOG2);
"hexp2"                           return TOKEN(T_OP_HEXP2);

                                  /* category 5: */
"isam"                            return TOKEN(T_OP_ISAM);
"isaml"                           return TOKEN(T_OP_ISAML);
"isamm"                           return TOKEN(T_OP_ISAMM);
"sam"                             return TOKEN(T_OP_SAM);
"samb"                            return TOKEN(T_OP_SAMB);
"saml"                            return TOKEN(T_OP_SAML);
"samgq"                           return TOKEN(T_OP_SAMGQ);
"getlod"                          return TOKEN(T_OP_GETLOD);
"conv"                            return TOKEN(T_OP_CONV);
"convm"                           return TOKEN(T_OP_CONVM);
"getsize"                         return TOKEN(T_OP_GETSIZE);
"getbuf"                          return TOKEN(T_OP_GETBUF);
"getpos"                          return TOKEN(T_OP_GETPOS);
"getinfo"                         return TOKEN(T_OP_GETINFO);
"dsx"                             return TOKEN(T_OP_DSX);
"dsy"                             return TOKEN(T_OP_DSY);
"gather4r"                        return TOKEN(T_OP_GATHER4R);
"gather4g"                        return TOKEN(T_OP_GATHER4G);
"gather4b"                        return TOKEN(T_OP_GATHER4B);
"gather4a"                        return TOKEN(T_OP_GATHER4A);
"samgp0"                          return TOKEN(T_OP_SAMGP0);
"samgp1"                          return TOKEN(T_OP_SAMGP1);
"samgp2"                          return TOKEN(T_OP_SAMGP2);
"samgp3"                          return TOKEN(T_OP_SAMGP3);
"dsxpp.1"                         return TOKEN(T_OP_DSXPP_1);
"dsypp.1"                         return TOKEN(T_OP_DSYPP_1);
"rgetpos"                         return TOKEN(T_OP_RGETPOS);
"rgetinfo"                        return TOKEN(T_OP_RGETINFO);

                                  /* category 6: */
"ldg"                             return TOKEN(T_OP_LDG);
"ldl"                             return TOKEN(T_OP_LDL);
"ldp"                             return TOKEN(T_OP_LDP);
"stg"                             return TOKEN(T_OP_STG);
"stl"                             return TOKEN(T_OP_STL);
"stp"                             return TOKEN(T_OP_STP);
"ldib"                            return TOKEN(T_OP_LDIB);
"g2l"                             return TOKEN(T_OP_G2L);
"l2g"                             return TOKEN(T_OP_L2G);
"prefetch"                        return TOKEN(T_OP_PREFETCH);
"ldlw"                            return TOKEN(T_OP_LDLW);
"stlw"                            return TOKEN(T_OP_STLW);
"resfmt"                          return TOKEN(T_OP_RESFMT);
"resinf"                          return TOKEN(T_OP_RESINF);
"atomic.add"                      return TOKEN(T_OP_ATOMIC_ADD);
"atomic.sub"                      return TOKEN(T_OP_ATOMIC_SUB);
"atomic.xchg"                     return TOKEN(T_OP_ATOMIC_XCHG);
"atomic.inc"                      return TOKEN(T_OP_ATOMIC_INC);
"atomic.dec"                      return TOKEN(T_OP_ATOMIC_DEC);
"atomic.cmpxchg"                  return TOKEN(T_OP_ATOMIC_CMPXCHG);
"atomic.min"                      return TOKEN(T_OP_ATOMIC_MIN);
"atomic.max"                      return TOKEN(T_OP_ATOMIC_MAX);
"atomic.and"                      return TOKEN(T_OP_ATOMIC_AND);
"atomic.or"                       return TOKEN(T_OP_ATOMIC_OR);
"atomic.xor"                      return TOKEN(T_OP_ATOMIC_XOR);
"ldgb"                            return TOKEN(T_OP_LDGB);
"stgb"                            return TOKEN(T_OP_STGB);
"stib"                            return TOKEN(T_OP_STIB);
"ldc"                             return TOKEN(T_OP_LDC);
"ldlv"                            return TOKEN(T_OP_LDLV);

"f16"                             return TOKEN(T_TYPE_F16);
"f32"                             return TOKEN(T_TYPE_F32);
"u16"                             return TOKEN(T_TYPE_U16);
"u32"                             return TOKEN(T_TYPE_U32);
"s16"                             return TOKEN(T_TYPE_S16);
"s32"                             return TOKEN(T_TYPE_S32);
"u8"                              return TOKEN(T_TYPE_U8);
"s8"                              return TOKEN(T_TYPE_S8);

"untyped"                         return TOKEN(T_UNTYPED);
"typed"                           return TOKEN(T_TYPED);

"1d"                              return TOKEN(T_1D);
"2d"                              return TOKEN(T_2D);
"3d"                              return TOKEN(T_3D);
"4d"                              return TOKEN(T_4D);

"lt"                              return TOKEN(T_LT);
"le"                              return TOKEN(T_LE);
"gt"                              return TOKEN(T_GT);
"ge"                              return TOKEN(T_GE);
"eq"                              return TOKEN(T_EQ);
"ne"                              return TOKEN(T_NE);

"a"                               return 'a';
"o"                               return 'o';
"p"                               return 'p';
"s2en"                            return TOKEN(T_S2EN);
"s"                               return 's';
"base"[0-9]+                      ir3_yylval.num = strtol(yytext+4, NULL, 10); return T_BASE;

"="                               return '=';
"("                               return '(';
")"                               return ')';
"["                               return '[';
"]"                               return ']';
","                               return ',';
"."                               return '.';
"-"                               return '-';
"+"                               return '+';
"|"                               return '|';
"c"                               return 'c';
"r"                               return 'r';
"hc"                              return TOKEN(T_HC);
"hr"                              return TOKEN(T_HR);
"g"                               return 'g';
"l"                               return 'l';
"<"                               return '<';
">"                               return '>';
"!"                               return '!';
"#"                               return '#';

"nan"                             return TOKEN(T_NAN);
"inf"                             return TOKEN(T_INF);

[a-zA-Z_][a-zA-Z_0-9]*            ir3_yylval.str = yytext;     return T_IDENTIFIER;
.                                 fprintf(stderr, "error at line %d: Unknown token: %s\n", ir3_yyget_lineno(), yytext); yyterminate();
%%
