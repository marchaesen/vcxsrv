/*
 * fontconfig/src/fcrange.c
 *
 * Copyright © 2002 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the author(s) not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  The authors make no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * THE AUTHOR(S) DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "fcint.h"


FcRange *
FcRangeCreateDouble (double begin, double end)
{
    FcRange *ret = malloc (sizeof (FcRange));

    if (ret)
    {
	ret->is_double = FcTrue;
	ret->is_inclusive = FcDoubleCmpEQ (begin, end);
	ret->u.d.begin = begin;
	ret->u.d.end = end;
    }

    return ret;
}

FcRange *
FcRangeCreateInteger (FcChar32 begin, FcChar32 end)
{
    FcRange *ret = malloc (sizeof (FcRange));

    if (ret)
    {
	ret->is_double = FcFalse;
	ret->is_inclusive = (begin == end);
	ret->u.i.begin = begin;
	ret->u.i.end = end;
    }

    return ret;
}

void
FcRangeDestroy (FcRange *range)
{
    free (range);
}

FcRange *
FcRangeCopy (const FcRange *range)
{
    FcRange *ret;

    if (range->is_double)
	ret = FcRangeCreateDouble (range->u.d.begin, range->u.d.end);
    else
	ret = FcRangeCreateInteger (range->u.i.begin, range->u.i.end);

    return ret;
}

FcRange
FcRangeCanonicalize (const FcRange *range)
{
    FcRange new;

    if (range->is_double)
	new = *range;
    else
    {
	new.is_double = FcTrue;
	new.is_inclusive = range->is_inclusive;
	new.u.d.begin = (double)range->u.i.begin;
	new.u.d.end = (double)range->u.i.end;
    }
    return new;
}

FcRange *
FcRangePromote (double v, FcValuePromotionBuffer *vbuf)
{
    typedef struct {
	FcRange	r;
    } FcRangePromotionBuffer;
    FcRangePromotionBuffer *buf = (FcRangePromotionBuffer *) vbuf;

    FC_ASSERT_STATIC (sizeof (FcRangePromotionBuffer) <= sizeof (FcValuePromotionBuffer));
    buf->r.is_double = FcTrue;
    buf->r.is_inclusive = FcTrue;
    buf->r.u.d.begin = v;
    buf->r.u.d.end = v;

    return &buf->r;
}

FcBool
FcRangeIsZero (const FcRange *r)
{
    FcRange c;

    if (!r)
	return FcFalse;
    c = FcRangeCanonicalize (r);

    return FcDoubleIsZero (c.u.d.begin) && FcDoubleIsZero (c.u.d.end);
}

FcBool
FcRangeIsInRange (const FcRange *a, const FcRange *b)
{
    FcRange ca, cb;
    FcBool f;

    if (!a || !b)
	return FcFalse;

    ca = FcRangeCanonicalize (a);
    cb = FcRangeCanonicalize (b);
    if (ca.is_inclusive & cb.is_inclusive)
	f = ca.u.d.end <= cb.u.d.end;
    else
	f = ca.u.d.end < cb.u.d.end;

    return FcDoubleCmpGE (ca.u.d.begin, cb.u.d.begin) && f;
}

FcBool
FcRangeCompare (FcOp op, const FcRange *a, const FcRange *b)
{
    FcRange ca, cb;

    switch ((int) op) {
    case FcOpEqual:
    case FcOpContains:
    case FcOpListing:
	return FcRangeIsInRange (a, b);
    case FcOpNotEqual:
    case FcOpNotContains:
	return !FcRangeIsInRange (a, b);
    case FcOpLess:
	ca = FcRangeCanonicalize (a);
	cb = FcRangeCanonicalize (b);
	return ca.u.d.begin < cb.u.d.begin;
    case FcOpLessEqual:
	ca = FcRangeCanonicalize (a);
	cb = FcRangeCanonicalize (b);
	return FcDoubleCmpLE (ca.u.d.begin, cb.u.d.begin);
    case FcOpMore:
	ca = FcRangeCanonicalize (a);
	cb = FcRangeCanonicalize (b);
	return ca.u.d.end > cb.u.d.end;
    case FcOpMoreEqual:
	ca = FcRangeCanonicalize (a);
	cb = FcRangeCanonicalize (b);
	return FcDoubleCmpGE (ca.u.d.end, cb.u.d.end);
    default:
	break;
    }
    return FcFalse;
}

FcChar32
FcRangeHash (const FcRange *r)
{
    FcRange c = FcRangeCanonicalize (r);
    int b = (int) (c.u.d.begin * 100);
    int e = (int) (c.u.d.end * 100);

    return b ^ (b << 1) ^ (e << 9);
}

FcBool
FcRangeSerializeAlloc (FcSerialize *serialize, const FcRange *r)
{
    if (!FcSerializeAlloc (serialize, r, sizeof (FcRange)))
	return FcFalse;
    return FcTrue;
}

FcRange *
FcRangeSerialize (FcSerialize *serialize, const FcRange *r)
{
    FcRange *r_serialize = FcSerializePtr (serialize, r);

    if (!r_serialize)
	return NULL;
    memcpy (r_serialize, r, sizeof (FcRange));

    return r_serialize;
}
