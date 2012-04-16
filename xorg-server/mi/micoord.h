/*
 * Copyright (C) 2000 The XFree86 Project, Inc.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the XFree86 Project shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from the
 * XFree86 Project.
 *
 */

#ifndef _MICOORD_H_
#define _MICOORD_H_ 1

#include "servermd.h"

/* Macros which handle a coordinate in a single register */

/*
 * Most compilers will convert divisions by 65536 into shifts, if signed
 * shifts exist.  If your machine does arithmetic shifts and your compiler
 * can't get it right, add to this line.
 */

/*
 * mips compiler - what a joke - it CSEs the 65536 constant into a reg
 * forcing as to use div instead of shift.  Let's be explicit.
 */

#if defined(mips) || \
    defined(sparc) || defined(__sparc64__) || \
    defined(__alpha) || defined(__alpha__) || \
    defined(__i386__) || defined(__i386) || defined(__ia64__) || \
    defined(__s390x__) || defined(__s390__) || \
    defined(__amd64__) || defined(amd64) || defined(__amd64)
#define GetHighWord(x) (((int) (x)) >> 16)
#else
#define GetHighWord(x) (((int) (x)) / 65536)
#endif

#if IMAGE_BYTE_ORDER == MSBFirst
#define intToCoord(i,x,y)   (((x) = GetHighWord(i)), ((y) = (int) ((short) (i))))
#define coordToInt(x,y)	(((x) << 16) | ((y) & 0xffff))
#define intToX(i)	(GetHighWord(i))
#define intToY(i)	((int) ((short) i))
#else
#define intToCoord(i,x,y)   (((x) = (int) ((short) (i))), ((y) = GetHighWord(i)))
#define coordToInt(x,y)	(((y) << 16) | ((x) & 0xffff))
#define intToX(i)	((int) ((short) (i)))
#define intToY(i)	(GetHighWord(i))
#endif

#endif                          /* _MICOORD_H_ */
