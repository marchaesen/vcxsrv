/*

Copyright (c) 1989  X Consortium

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE X CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of the X Consortium shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from the X Consortium.

*/

/*
 * xcalc.h - symbolic constants for xcalc
 *
 * Author:  Donna Converse, MIT X Consortium
 */

#ifndef _XCALC_H_
#define _XCALC_H_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>

#include <stdio.h>
#include <X11/Xos.h>
#include <math.h>
#include <signal.h>
#if !defined(IEEE) && defined(SVR4)
#include <siginfo.h>
#endif
#include <setjmp.h>
#include <errno.h>
#include <X11/Xlocale.h>

#define kRECIP 0	/* reciprocal */
#define kSQR   1	/* square */
#define kSQRT  2	/* square root */
#define kCLR   3	/* clear */
#define kOFF   4	/* clear and quit */
#define kINV   5	/* inverse */
#define kSIN   6	/* sine */
#define kCOS   7	/* cosine */
#define kTAN   8	/* tangent */
#define kDRG   9	/* degree radian grad */
#define kE     10	/* the natural number e */
#define kEE    11	/* scientific notation */
#define kLOG   12	/* logarithm */
#define kLN    13	/* natural logarithm */
#define kPOW   14	/* power */
#define kPI    15	/* pi */
#define kFACT  16	/* factorial */
#define kLPAR  17	/* left paren */
#define kRPAR  18	/* right paren */
#define kDIV   19	/* division */
#define kSTO   20	/* store */
#define kSEVEN 21	/* 7 */
#define kEIGHT 22	/* 8 */
#define kNINE  23	/* 9 */
#define kMUL   24	/* multiplication */
#define kRCL   25	/* recall */
#define kFOUR  26	/* 4 */
#define kFIVE  27	/* 5 */
#define kSIX   28	/* 6 */
#define kSUB   29	/* subtraction */
#define kSUM   30	/* summation */
#define kONE   31	/* 1 */
#define kTWO   32	/* 2 */
#define kTHREE 33	/* 3 */
#define kADD   34	/* addition */
#define kEXC   35	/* exchange display and memory */
#define kZERO  36	/* 0 */
#define kDEC   37	/* decimal point */
#define kNEG   38	/* negation */
#define kEQU   39	/* equals */
#define kENTR  40	/* enter */
#define kXXY   41	/* exchange X and Y registers */
#define kEXP   42	/* exponent */
#define k10X   43	/* 10 raised to a power */
#define kROLL  44	/* roll stack */
#define kNOP   45	/* no operation */
#define kBKSP  46	/* backspace */

#define XCalc_MEMORY	0	/* memory indicator */
#define XCalc_INVERSE   1	/* inverse function indicator */
#define XCalc_DEGREE	2	/* degree indicator */
#define XCalc_RADIAN	3	/* radian indicator */
#define XCalc_GRADAM	4	/* grad indicator */
#define XCalc_PAREN	5	/* parenthesis indicator */

/* actions.c */
extern XtActionsRec Actions[];
extern int ActionsCount;

/* math.c */
extern void fperr(int sig) _X_NORETURN;
extern void illerr(int sig) _X_NORETURN;
extern void fail_op(void);
extern int pre_op(int keynum);
extern void post_op(void);

extern void numeric(int keynum);
extern void bkspf(void);
extern void decf(void);
extern void eef(void);
extern void clearf(void);
extern void negf(void);
extern void twoop(int keynum);
extern void twof(int keynum);
extern void entrf(void);
extern void equf(void);
extern void lparf(void);
extern void rollf(void);
extern void rparf(void);
extern void drgf(void);
extern void invf(void);
extern void memf(int keynum);
extern void oneop(int keynum);
extern void offf(void);
extern void ResetCalc(void);

#ifndef IEEE
extern jmp_buf env;
#endif

/* xcalc.c */
extern void do_select(Time time);
extern void draw(char *string);
extern void Quit(void) _X_NORETURN;
extern void ringbell(void);
extern void setflag(int indicator, Boolean on);

extern int rpn;
#define LCD_STR_LEN	32
extern char dispstr[LCD_STR_LEN];
extern Atom wm_delete_window;

#endif
