/*
 * Copyright © 1998 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "fb.h"
#include "miline.h"
#include "mizerarc.h"

#undef BRESSOLID
#undef BRESDASH
#undef DOTS
#undef ARC
#undef GLYPH
#undef BITS
#undef BITS2
#undef BITS4

#define BRESSOLID   fbBresSolid8
#define BRESDASH    fbBresDash8
#define DOTS	    fbDots8
#define ARC	    fbArc8
#define GLYPH	    fbGlyph8
#define POLYLINE    fbPolyline8
#define POLYSEGMENT fbPolySegment8
#define BITS	    BYTE
#define BITS2	    CARD16
#define BITS4	    CARD32

#include "fbbits.h"

#undef BRESSOLID
#undef BRESDASH
#undef DOTS
#undef ARC
#undef GLYPH
#undef POLYLINE
#undef POLYSEGMENT
#undef BITS
#undef BITS2
#undef BITS4

#define BRESSOLID   fbBresSolid16
#define BRESDASH    fbBresDash16
#define DOTS	    fbDots16
#define ARC	    fbArc16
#define GLYPH	    fbGlyph16
#define POLYLINE    fbPolyline16
#define POLYSEGMENT fbPolySegment16
#define BITS	    CARD16
#define BITS2	    CARD32

#include "fbbits.h"

#undef BRESSOLID
#undef BRESDASH
#undef DOTS
#undef ARC
#undef GLYPH
#undef POLYLINE
#undef POLYSEGMENT
#undef BITS
#undef BITS2

#define BRESSOLID   fbBresSolid24
#define BRESDASH    fbBresDash24
#define DOTS        fbDots24
#define ARC         fbArc24
#define POLYLINE    fbPolyline24
#define POLYSEGMENT fbPolySegment24

#define BITS        CARD32
#define BITSUNIT    BYTE
#define BITSMUL	    3

#define FbDoTypeStore(b,t,x,s)	WRITE(((t *) (b)), (x) >> (s))
#define FbDoTypeRRop(b,t,a,x,s) WRITE((t *) (b), FbDoRRop(READ((t *) (b)),\
							  (a) >> (s), \
							  (x) >> (s)))
#define FbDoTypeMaskRRop(b,t,a,x,m,s) WRITE((t *) (b), FbDoMaskRRop(READ((t *) (b)),\
								    (a) >> (s), \
								    (x) >> (s), \
								    (m) >> (s)))
#if BITMAP_BIT_ORDER == LSBFirst
#define BITSSTORE(b,x)	((unsigned long) (b) & 1 ? \
			 (FbDoTypeStore (b, CARD8, x, 0), \
			  FbDoTypeStore ((b) + 1, CARD16, x, 8)) : \
			 (FbDoTypeStore (b, CARD16, x, 0), \
			  FbDoTypeStore ((b) + 2, CARD8, x, 16)))
#define BITSRROP(b,a,x)	((unsigned long) (b) & 1 ? \
			 (FbDoTypeRRop(b,CARD8,a,x,0), \
			  FbDoTypeRRop((b)+1,CARD16,a,x,8)) : \
			 (FbDoTypeRRop(b,CARD16,a,x,0), \
			  FbDoTypeRRop((b)+2,CARD8,a,x,16)))
#else
#define BITSSTORE(b,x)  ((unsigned long) (b) & 1 ? \
			 (FbDoTypeStore (b, CARD8, x, 16), \
			  FbDoTypeStore ((b) + 1, CARD16, x, 0)) : \
			 (FbDoTypeStore (b, CARD16, x, 8), \
			  FbDoTypeStore ((b) + 2, CARD8, x, 0)))
#define BITSRROP(b,a,x)	((unsigned long) (b) & 1 ? \
			 (FbDoTypeRRop (b, CARD8, a, x, 16), \
			  FbDoTypeRRop ((b) + 1, CARD16, a, x, 0)) : \
			 (FbDoTypeRRop (b, CARD16, a, x, 8), \
			  FbDoTypeRRop ((b) + 2, CARD8, a, x, 0)))
#endif

#include "fbbits.h"

#undef BITSSTORE
#undef BITSRROP
#undef BITSMUL
#undef BITSUNIT
#undef BITS

#undef BRESSOLID
#undef BRESDASH
#undef DOTS
#undef ARC
#undef POLYLINE
#undef POLYSEGMENT

#define BRESSOLID   fbBresSolid32
#define BRESDASH    fbBresDash32
#define DOTS	    fbDots32
#define ARC	    fbArc32
#define GLYPH	    fbGlyph32
#define POLYLINE    fbPolyline32
#define POLYSEGMENT fbPolySegment32
#define BITS	    CARD32

#include "fbbits.h"

#undef BRESSOLID
#undef BRESDASH
#undef DOTS
#undef ARC
#undef GLYPH
#undef POLYLINE
#undef POLYSEGMENT
#undef BITS
