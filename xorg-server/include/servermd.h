/***********************************************************

Copyright 1987, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.

Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

#ifndef SERVERMD_H
#define SERVERMD_H 1

/*
 * Note: much of this is vestigial from mfb/cfb times.  This should
 * really be simplified even further.
 */

/*
 * Machine dependent values:
 * GLYPHPADBYTES should be chosen with consideration for the space-time
 * trade-off.  Padding to 0 bytes means that there is no wasted space
 * in the font bitmaps (both on disk and in memory), but that access of
 * the bitmaps will cause odd-address memory references.  Padding to
 * 2 bytes would ensure even address memory references and would
 * be suitable for a 68010-class machine, but at the expense of wasted
 * space in the font bitmaps.  Padding to 4 bytes would be good
 * for real 32 bit machines, etc.  Be sure that you tell the font
 * compiler what kind of padding you want because its defines are
 * kept separate from this.  See server/include/font.h for how
 * GLYPHPADBYTES is used.
 */

#ifdef __avr32__

#define IMAGE_BYTE_ORDER        MSBFirst
#define BITMAP_BIT_ORDER        MSBFirst
#define GLYPHPADBYTES           4

#endif                          /* __avr32__ */

#ifdef __arm32__

#define IMAGE_BYTE_ORDER        LSBFirst
#define BITMAP_BIT_ORDER        LSBFirst
#define GLYPHPADBYTES           4

#endif                          /* __arm32__ */

#if defined(__nds32__)

#define IMAGE_BYTE_ORDER	LSBFirst

#if defined(XF86MONOVGA) || defined(XF86VGA16) || defined(XF86MONO)
#define BITMAP_BIT_ORDER	MSBFirst
#else
#define BITMAP_BIT_ORDER	LSBFirst
#endif

#if defined(XF86MONOVGA) || defined(XF86VGA16)
#define BITMAP_SCANLINE_UNIT	8
#endif

#define GLYPHPADBYTES		4
#define GETLEFTBITS_ALIGNMENT	1
#define LARGE_INSTRUCTION_CACHE
#define AVOID_MEMORY_READ

#endif                          /* __nds32__ */

#if defined __hppa__

#define IMAGE_BYTE_ORDER	MSBFirst
#define BITMAP_BIT_ORDER	MSBFirst
#define GLYPHPADBYTES		4       /* to make fb work */
                                        /* byte boundries */
#endif                          /* hpux || __hppa__ */

#if defined(__powerpc__) || defined(__ppc__) || defined(__ppc64__)

#if defined(__LITTLE_ENDIAN__)
#define IMAGE_BYTE_ORDER      LSBFirst
#define BITMAP_BIT_ORDER      LSBFirst
#else
#define IMAGE_BYTE_ORDER      MSBFirst
#define BITMAP_BIT_ORDER      MSBFirst
#endif
#define GLYPHPADBYTES           4

#endif                          /* PowerPC */

#if defined(__sh__)

#if defined(__BIG_ENDIAN__)
#define IMAGE_BYTE_ORDER	MSBFirst
#define BITMAP_BIT_ORDER	MSBFirst
#define GLYPHPADBYTES		4
#else
#define IMAGE_BYTE_ORDER	LSBFirst
#define BITMAP_BIT_ORDER	LSBFirst
#define GLYPHPADBYTES		4
#endif

#endif                          /* SuperH */

#if defined(__m32r__)

#if defined(__BIG_ENDIAN__)
#define IMAGE_BYTE_ORDER      MSBFirst
#define BITMAP_BIT_ORDER      MSBFirst
#define GLYPHPADBYTES         4
#else
#define IMAGE_BYTE_ORDER      LSBFirst
#define BITMAP_BIT_ORDER      LSBFirst
#define GLYPHPADBYTES         4
#endif

#endif                          /* __m32r__ */

#if (defined(sun) && (defined(__sparc) || defined(sparc))) || \
    (defined(__uxp__) && (defined(sparc) || defined(mc68000))) || \
    defined(__sparc__) || defined(__mc68000__)

#if defined(__sparc) || defined(__sparc__)
#if !defined(sparc)
#define sparc 1
#endif
#endif

#if defined(sun386) || defined(sun5)
#define IMAGE_BYTE_ORDER	LSBFirst        /* Values for the SUN only */
#define BITMAP_BIT_ORDER	LSBFirst
#else
#define IMAGE_BYTE_ORDER	MSBFirst        /* Values for the SUN only */
#define BITMAP_BIT_ORDER	MSBFirst
#endif

#define	GLYPHPADBYTES		4

#endif                          /* sun && !(i386 && SVR4) */

#if defined(ibm032) || defined (ibm)

#ifdef __i386__
#define IMAGE_BYTE_ORDER	LSBFirst        /* Value for PS/2 only */
#else
#define IMAGE_BYTE_ORDER	MSBFirst        /* Values for the RT only */
#endif
#define BITMAP_BIT_ORDER	MSBFirst
#define	GLYPHPADBYTES		1
/* ibm pcc doesn't understand pragmas. */

#ifdef __i386__
#define BITMAP_SCANLINE_UNIT	8
#endif

#endif                          /* ibm */

#if (defined(mips) || defined(__mips))

#if defined(MIPSEL) || defined(__MIPSEL__)
#define IMAGE_BYTE_ORDER	LSBFirst        /* Values for the PMAX only */
#define BITMAP_BIT_ORDER	LSBFirst
#define GLYPHPADBYTES		4
#else
#define IMAGE_BYTE_ORDER	MSBFirst        /* Values for the MIPS only */
#define BITMAP_BIT_ORDER	MSBFirst
#define GLYPHPADBYTES		4
#endif

#endif                          /* mips */

#if defined(__alpha) || defined(__alpha__)
#define IMAGE_BYTE_ORDER	LSBFirst        /* Values for the Alpha only */
#define BITMAP_BIT_ORDER       LSBFirst
#define GLYPHPADBYTES		4

#endif                          /* alpha */

#if defined (linux) && defined (__s390__)

#define IMAGE_BYTE_ORDER      	MSBFirst
#define BITMAP_BIT_ORDER      	MSBFirst
#define GLYPHPADBYTES         	4

#define BITMAP_SCANLINE_UNIT	8
#define FAST_UNALIGNED_READ

#endif                          /* linux/s390 */

#if defined (linux) && defined (__s390x__)

#define IMAGE_BYTE_ORDER       MSBFirst
#define BITMAP_BIT_ORDER       MSBFirst
#define GLYPHPADBYTES          4

#define BITMAP_SCANLINE_UNIT	8
#define FAST_UNALIGNED_READ

#endif                          /* linux/s390x */

#if defined(__ia64__) || defined(ia64)

#define IMAGE_BYTE_ORDER	LSBFirst
#define BITMAP_BIT_ORDER       LSBFirst
#define GLYPHPADBYTES		4

#endif                          /* ia64 */

#if defined(__amd64__) || defined(amd64) || defined(__amd64)
#define IMAGE_BYTE_ORDER	LSBFirst
#define BITMAP_BIT_ORDER       LSBFirst
#define GLYPHPADBYTES		4
/* ???? */
#endif                          /* AMD64 */

#if	defined(SVR4) && (defined(__i386__) || defined(__i386) ) ||	\
	defined(__alpha__) || defined(__alpha) || \
	defined(__i386__) || \
	defined(__s390x__) || defined(__s390__)

#ifndef IMAGE_BYTE_ORDER
#define IMAGE_BYTE_ORDER	LSBFirst
#endif

#ifndef BITMAP_BIT_ORDER
#define BITMAP_BIT_ORDER      LSBFirst
#endif

#ifndef GLYPHPADBYTES
#define GLYPHPADBYTES           4
#endif

#endif                          /* SVR4 / BSD / i386 */

#if defined (linux) && defined (__mc68000__)

#define IMAGE_BYTE_ORDER       MSBFirst
#define BITMAP_BIT_ORDER       MSBFirst
#define GLYPHPADBYTES          4

#endif                          /* linux/m68k */

/* linux on ARM */
#if defined(linux) && defined(__arm__)
#define IMAGE_BYTE_ORDER	LSBFirst
#define BITMAP_BIT_ORDER	LSBFirst
#define GLYPHPADBYTES		4
#endif

/* linux on IBM S/390 */
#if defined (linux) && defined (__s390__)
#define IMAGE_BYTE_ORDER	MSBFirst
#define BITMAP_BIT_ORDER	MSBFirst
#define GLYPHPADBYTES		4
#endif                          /* linux/s390 */

#ifdef __aarch64__

#ifdef __AARCH64EL__
#define IMAGE_BYTE_ORDER        LSBFirst
#define BITMAP_BIT_ORDER        LSBFirst
#endif
#ifdef __AARCH64EB__
#define IMAGE_BYTE_ORDER        MSBFirst
#define BITMAP_BIT_ORDER        MSBFirst
#endif
#define GLYPHPADBYTES           4

#endif                          /* __aarch64__ */

#if defined(__arc__)

#if defined(__BIG_ENDIAN__)
#define IMAGE_BYTE_ORDER	MSBFirst
#define BITMAP_BIT_ORDER	MSBFirst
#else
#define IMAGE_BYTE_ORDER	LSBFirst
#define BITMAP_BIT_ORDER	LSBFirst
#endif
#define GLYPHPADBYTES		4

#endif                          /* ARC */

#ifdef __xtensa__

#ifdef __XTENSA_EL__
#define IMAGE_BYTE_ORDER        LSBFirst
#define BITMAP_BIT_ORDER        LSBFirst
#endif
#ifdef __XTENSA_EB__
#define IMAGE_BYTE_ORDER        MSBFirst
#define BITMAP_BIT_ORDER        MSBFirst
#endif
#define GLYPHPADBYTES           4

#endif                          /* __xtensa__ */

/* size of buffer to use with GetImage, measured in bytes. There's obviously
 * a trade-off between the amount of heap used and the number of times the
 * ddx routine has to be called.
 */
#ifndef IMAGE_BUFSIZE
#define IMAGE_BUFSIZE		(64*1024)
#endif

/* pad scanline to a longword */
#ifndef BITMAP_SCANLINE_UNIT
#define BITMAP_SCANLINE_UNIT	32
#endif

#ifndef BITMAP_SCANLINE_PAD
#define BITMAP_SCANLINE_PAD  32
#define LOG2_BITMAP_PAD		5
#define LOG2_BYTES_PER_SCANLINE_PAD	2
#endif

#include <X11/Xfuncproto.h>
/* 
 *   This returns the number of padding units, for depth d and width w.
 * For bitmaps this can be calculated with the macros above.
 * Other depths require either grovelling over the formats field of the
 * screenInfo or hardwired constants.
 */

typedef struct _PaddingInfo {
    int padRoundUp;             /* pixels per pad unit - 1 */
    int padPixelsLog2;          /* log 2 (pixels per pad unit) */
    int padBytesLog2;           /* log 2 (bytes per pad unit) */
    int notPower2;              /* bitsPerPixel not a power of 2 */
    int bytesPerPixel;          /* only set when notPower2 is TRUE */
    int bitsPerPixel;           /* bits per pixel */
} PaddingInfo;
extern _X_EXPORT PaddingInfo PixmapWidthPaddingInfo[];

/* The only portable way to get the bpp from the depth is to look it up */
#define BitsPerPixel(d) (PixmapWidthPaddingInfo[d].bitsPerPixel)

#define PixmapWidthInPadUnits(w, d) \
    (PixmapWidthPaddingInfo[d].notPower2 ? \
    (((int)(w) * PixmapWidthPaddingInfo[d].bytesPerPixel +  \
	         PixmapWidthPaddingInfo[d].bytesPerPixel) >> \
	PixmapWidthPaddingInfo[d].padBytesLog2) : \
    ((int)((w) + PixmapWidthPaddingInfo[d].padRoundUp) >> \
	PixmapWidthPaddingInfo[d].padPixelsLog2))

/*
 *	Return the number of bytes to which a scanline of the given
 * depth and width will be padded.
 */
#define PixmapBytePad(w, d) \
    (PixmapWidthInPadUnits(w, d) << PixmapWidthPaddingInfo[d].padBytesLog2)

#define BitmapBytePad(w) \
    (((int)((w) + BITMAP_SCANLINE_PAD - 1) >> LOG2_BITMAP_PAD) << LOG2_BYTES_PER_SCANLINE_PAD)

#define PixmapWidthInPadUnitsProto(w, d) PixmapWidthInPadUnits(w, d)
#define PixmapBytePadProto(w, d) PixmapBytePad(w, d)
#define BitmapBytePadProto(w) BitmapBytePad(w)

#endif                          /* SERVERMD_H */
