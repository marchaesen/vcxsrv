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
 * actions.c - externally available procedures for xcalc
 *
 * Author:  Donna Converse, MIT X Consortium
 */

#include <X11/Intrinsic.h>
#include <setjmp.h>
#include "xcalc.h"

#ifndef IEEE
#define XCALC_PRE_OP(keynum) { if (pre_op(keynum)) return; \
		       if (setjmp (env)) {fail_op(); return;}}
#else
#define XCALC_PRE_OP(keynum) if (pre_op(keynum)) return;
#endif

static void add(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void back(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void bell(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void clearit(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void cosine(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void decimal(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void degree(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void digit(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void divide(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void e(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void enter(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void epower(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void equal(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void exchange(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void factorial(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void inverse(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void leftParen(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void logarithm(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void multiply(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void naturalLog(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void negate(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void nop(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void off(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void pi(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void power(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void quit(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void recall(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void reciprocal(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void rightParen(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void roll(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void scientific(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void selection(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void sine(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void square(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void squareRoot(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void store(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void subtract(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void sum(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void tangent(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void tenpower(Widget w, XEvent *ev, String *vector, Cardinal *count);
static void XexchangeY(Widget w, XEvent *ev, String *vector, Cardinal *count);

/*
 * 	calculator action table
 */

XtActionsRec	Actions[] = {
{"add",		add},		/* addition */
{"back",	back},		/* HP-specific backspace */
{"bell",	bell},		/* ring bell */
{"clear",	clearit},	/* TI-specific clear calculator state */
{"cosine",	cosine},	/* trigonometric function cosine */
{"decimal",	decimal},	/* decimal point */
{"degree",	degree},	/* degree, radian, grad switch */
{"digit",	digit},		/* numeric key */
{"divide",	divide},	/* division */
{"e",		e},		/* the natural number e */
{"enter",	enter},		/* HP-specific enter */
{"epower",	epower},	/* e raised to a power */
{"equal",	equal},		/* TI-specific = */
{"exchange",	exchange},	/* TI-specific exchange memory and display */
{"factorial",	factorial},	/* factorial function */
{"inverse", 	inverse},	/* inverse */
{"leftParen",	leftParen},	/* TI-specific left parenthesis */
{"logarithm",	logarithm},	/* logarithm base 10 */
{"multiply",	multiply},	/* multiplication */
{"naturalLog",	naturalLog},	/* natural logarithm base e */
{"negate",	negate},	/* change sign */
{"nop",		nop},		/* no operation, rings bell */
{"off",		off},		/* clear state */
{"pi",		pi},		/* the number pi */
{"power",	power},		/* raise to an arbitrary power */
{"quit",	quit},		/* quit */
{"recall",	recall},	/* memory recall */
{"reciprocal",  reciprocal},	/* reciprocal function */
{"rightParen",	rightParen},	/* TI-specific left parenthesis */
{"roll",	roll},		/* HP-specific roll stack */
{"scientific",	scientific},	/* scientfic notation (EE) */
{"selection",	selection},	/* copy selection */
{"sine",	sine},		/* trigonometric function sine */
{"square",	square},	/* square */
{"squareRoot",	squareRoot},	/* square root */
{"store",	store},		/* memory store */
{"subtract", 	subtract},	/* subtraction */
{"sum",		sum},		/* memory summation */
{"tangent",	tangent},	/* trigonometric function tangent */
{"tenpower",	tenpower},	/* 10 raised to to an arbitrary power */
{"XexchangeY",	XexchangeY}	/* HP-specific exchange X and Y registers */
};

int ActionsCount = XtNumber(Actions);

/*ARGSUSED*/
static void add(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kADD);
    rpn ? twof(kADD) : twoop(kADD);
    post_op();
}

/*ARGSUSED*/
static void back(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kBKSP);
    bkspf();
    post_op();
}

/*ARGSUSED*/
static void bell(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    ringbell();
}

/*ARGSUSED*/
static void clearit(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kCLR);
    clearf();
    post_op();
}

/*ARGSUSED*/
static void cosine(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kCOS);
    oneop(kCOS);
    post_op();
}

/*ARGSUSED*/
static void decimal(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kDEC);
    decf();
    post_op();
}

/*ARGSUSED*/
static void degree(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kDRG);
    drgf();
    post_op();
}

/*ARGSUSED*/
static void digit(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    switch (vector[0][0])
    {
      case '1':	XCALC_PRE_OP(kONE); numeric(kONE); break;
      case '2': XCALC_PRE_OP(kTWO); numeric(kTWO); break;
      case '3': XCALC_PRE_OP(kTHREE); numeric(kTHREE); break;
      case '4': XCALC_PRE_OP(kFOUR); numeric(kFOUR); break;
      case '5': XCALC_PRE_OP(kFIVE); numeric(kFIVE); break;
      case '6': XCALC_PRE_OP(kSIX); numeric(kSIX); break;
      case '7': XCALC_PRE_OP(kSEVEN); numeric(kSEVEN); break;
      case '8': XCALC_PRE_OP(kEIGHT); numeric(kEIGHT); break;
      case '9': XCALC_PRE_OP(kNINE); numeric(kNINE); break;
      case '0': XCALC_PRE_OP(kZERO); numeric(kZERO); break;
    }
    post_op();
}

/*ARGSUSED*/
static void divide(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kDIV);
    rpn  ? twof(kDIV) : twoop(kDIV);
    post_op();
}

/*ARGSUSED*/
static void e(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kE);
    oneop(kE);
    post_op();
}

/*ARGSUSED*/
static void enter(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kENTR);
    entrf();
    post_op();
}

/*ARGSUSED*/
static void epower(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kEXP);
    oneop(kEXP);
    post_op();
}

/*ARGSUSED*/
static void equal(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kEQU);
    equf();
    post_op();
}

/*ARGSUSED*/
static void exchange(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kEXC);
    oneop(kEXC);
    post_op();
}

/*ARGSUSED*/
static void factorial(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kFACT);
    oneop(kFACT);
    post_op();
}

/*ARGSUSED*/
static void inverse(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kINV);
    invf();
    post_op();
}

/*ARGSUSED*/
static void leftParen(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kLPAR);
    lparf();
    post_op();
}

/*ARGSUSED*/
static void logarithm(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kLOG);
    oneop(kLOG);
    post_op();
}

/*ARGSUSED*/
static void multiply(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kMUL);
    rpn ? twof(kMUL) : twoop(kMUL);
    post_op();
}

/*ARGSUSED*/
static void naturalLog(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kLN);
    oneop(kLN);
    post_op();
}

/*ARGSUSED*/
static void negate(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kNEG);
    negf();
    post_op();
}

/*ARGSUSED*/
static void nop(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    ringbell();
}

/*ARGSUSED*/
static void off(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kOFF);
    offf();
    post_op();
}

/*ARGSUSED*/
static void pi(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kPI);
    oneop(kPI);
    post_op();
}

/*ARGSUSED*/
static void power(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kPOW);
    rpn ? twof(kPOW) : twoop(kPOW);
    post_op();
}

/*ARGSUSED*/
static void quit(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    if (ev->type == ClientMessage && ev->xclient.data.l[0] != wm_delete_window)
	ringbell();
    else
	Quit();
}

/*ARGSUSED*/
static void recall(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kRCL);
    rpn ? memf(kRCL) : oneop(kRCL);
    post_op();
}

/*ARGSUSED*/
static void reciprocal(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kRECIP);
    oneop(kRECIP);
    post_op();
}

/*ARGSUSED*/
static void rightParen(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kRPAR);
    rparf();
    post_op();
}

/*ARGSUSED*/
static void roll(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kROLL);
    rollf();
    post_op();
}

/*ARGSUSED*/
static void scientific(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kEE);
    eef();
    post_op();
}

/*ARGSUSED*/
static void selection(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    do_select(((XButtonReleasedEvent *)ev)->time);
}

/*ARGSUSED*/
static void sine(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kSIN);
    oneop(kSIN);
    post_op();
}

/*ARGSUSED*/
static void square(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kSQR);
    oneop(kSQR);
    post_op();
}

/*ARGSUSED*/
static void squareRoot(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kSQRT);
    oneop(kSQRT);
    post_op();
}

/*ARGSUSED*/
static void store(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kSTO);
    rpn ? memf(kSTO) : oneop(kSTO);
    post_op();
}

/*ARGSUSED*/
static void subtract(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kSUB);
    rpn ? twof(kSUB) : twoop(kSUB);
    post_op();
}

/*ARGSUSED*/
static void sum(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kSUM);
    rpn ? memf(kSUM) : oneop(kSUM);
    post_op();
}

/*ARGSUSED*/
static void tangent(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kTAN);
    oneop(kTAN);
    post_op();
}

/*ARGSUSED*/
static void tenpower(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(k10X);
    oneop(k10X);
    post_op();
}

/*ARGSUSED*/
static void XexchangeY(Widget w, XEvent *ev, String *vector, Cardinal *count)
{
    XCALC_PRE_OP(kXXY);
    twof(kXXY);
    post_op();
}
