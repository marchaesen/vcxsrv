/*

Copyright 1989, 1998  The Open Group

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

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/Xfuncs.h>
#include "Xct.h"
#include <stdio.h>
#include "Xmuint.h"

#define UsedGraphic	0x0001
#define UsedDirection	0x0002

typedef struct _XctPriv {
    XctString		ptr;
    XctString		ptrend;
    unsigned		flags;
    XctHDirection	*dirstack;
    unsigned		dirsize;
    char		**encodings;
    unsigned		enc_count;
    XctString		itembuf;
    unsigned		buf_count;
} *XctPriv;

#define IsMore(priv) ((priv)->ptr != (priv)->ptrend)
#define AmountLeft(priv) ((priv)->ptrend - (priv)->ptr)

#include <stdlib.h>

#define HT	0x09
#define NL	0x0a
#define ESC	0x1b
#define CSI	0x9b

#define IsLegalC0(data, c) (((c) == HT) || ((c) == NL) || \
			    (((data)->version > XctVersion) && \
			     ((data)->flags & XctAcceptC0Extensions)))

#define IsLegalC1(priv, c) (((data)->version > XctVersion) && \
			    ((data)->flags & XctAcceptC1Extensions))

#define IsI2(c) (((c) >= 0x20) && ((c) <= 0x2f))
#define IsI3(c) (((c) >= 0x30) && ((c) <= 0x3f))
#define IsESCF(c) (((c) >= 0x30) && ((c) <= 0x7e))
#define IsCSIF(c) (((c) >= 0x40) && ((c) <= 0x7e))
#define IsC0(c) ((c) <= 0x1f)
#define IsGL(c) (((c) >= 0x20) && ((c) <= 0x7f))
#define IsC1(c) (((c) >= 0x80) && ((c) <= 0x9f))
#define IsGR(c) ((c) >= 0xa0)

#define HasC  1
#define HasGL 2
#define HasGR 4
#define ToGL  8

/*
 * Prototypes
 */
static void ComputeGLGR(XctData);
static int Handle94GR(XctData, int);
static int Handle96GR(XctData, int);
static int HandleExtended(XctData data, int);
static int HandleGL(XctData, int);
static int HandleMultiGL(XctData, int);
static int HandleMultiGR(XctData data, int);
static void ShiftGRToGL(XctData, int);

/*
 * Implementation
 */
static void
ComputeGLGR(register XctData data)
{
    /* XXX this will need more work if more sets are registered */
    if ((data->GL_set_size == 94) && (data->GL_char_size == 1) &&
	(data->GL[0] == '\102') &&
	(data->GR_set_size == 96) && (data->GR_char_size == 1))
	data->GLGR_encoding = data->GR_encoding;
    else if ((data->GL_set_size == 94) && (data->GL_char_size == 1) &&
	     (data->GL[0] == '\112') &&
	     (data->GR_set_size == 94) && (data->GR_char_size == 1))
	data->GLGR_encoding = data->GR_encoding;
    else
	data->GLGR_encoding = (char *)NULL;
}

static int
HandleGL(register XctData data, int c)
{
    switch (c) {
    case 0x42:
	data->GL = "\102";
	data->GL_encoding = "ISO8859-1";
	break;
    case 0x4a:
	data->GL = "\112";
	data->GL_encoding = "JISX0201.1976-0";
	break;
    default:
	return 0;
    }
    data->GL_set_size = 94;
    data->GL_char_size = 1;
    ComputeGLGR(data);
    return 1;
}

static int
HandleMultiGL(register XctData data, int c)
{
    switch (c) {
    case 0x41:
	data->GL = "\101";
	data->GL_encoding = "GB2312.1980-0";
	break;
    case 0x42:
	data->GL = "\102";
	data->GL_encoding = "JISX0208.1983-0";
	break;
    case 0x43:
	data->GL = "\103";
	data->GL_encoding = "KSC5601.1987-0";
	break;
    default:
	return 0;
    }
    data->GL_set_size = 94;
    data->GL_char_size = 2;
#ifdef notdef
    if (c < 0x60)
	data->GL_char_size = 2;
    else if (c < 0x70)
	data->GL_char_size = 3;
    else
	data->GL_char_size = 4;
#endif
    data->GLGR_encoding = (char *)NULL;
    return 1;
}

static int
Handle94GR(register XctData data, int c)
{
    switch (c) {
    case 0x49:
	data->GR = "\111";
	data->GR_encoding = "JISX0201.1976-0";
	break;
    default:
	return 0;
    }
    data->priv->flags &= ~ToGL;
    data->GR_set_size = 94;
    data->GR_char_size = 1;
    data->GLGR_encoding = (char *)NULL;
    return 1;
}

static int
Handle96GR(register XctData data, int c)
{
    switch (c) {
    case 0x41:
	data->GR = "\101";
	data->GR_encoding = "ISO8859-1";
	break;
    case 0x42:
	data->GR = "\102";
	data->GR_encoding = "ISO8859-2";
	break;
    case 0x43:
	data->GR = "\103";
	data->GR_encoding = "ISO8859-3";
	break;
    case 0x44:
	data->GR = "\104";
	data->GR_encoding = "ISO8859-4";
	break;
    case 0x46:
	data->GR = "\106";
	data->GR_encoding = "ISO8859-7";
	break;
    case 0x47:
	data->GR = "\107";
	data->GR_encoding = "ISO8859-6";
	break;
    case 0x48:
	data->GR = "\110";
	data->GR_encoding = "ISO8859-8";
	break;
    case 0x4c:
	data->GR = "\114";
	data->GR_encoding = "ISO8859-5";
	break;
    case 0x4d:
	data->GR = "\115";
	data->GR_encoding = "ISO8859-9";
	break;
    default:
	return 0;
    }
    data->priv->flags &= ~ToGL;
    data->GR_set_size = 96;
    data->GR_char_size = 1;
    ComputeGLGR(data);
    return 1;
}

static int
HandleMultiGR(register XctData data, int c)
{
    switch (c) {
    case 0x41:
	data->GR = "\101";
	if (data->flags & XctShiftMultiGRToGL)
	    data->GR_encoding = "GB2312.1980-0";
	else
	    data->GR_encoding = "GB2312.1980-1";
	break;
    case 0x42:
	data->GR = "\102";
	if (data->flags & XctShiftMultiGRToGL)
	    data->GR_encoding = "JISX0208.1983-0";
	else
	    data->GR_encoding = "JISX0208.1983-1";
	break;
    case 0x43:
	data->GR = "\103";
	if (data->flags & XctShiftMultiGRToGL)
	    data->GR_encoding = "KSC5601.1987-0";
	else
	    data->GR_encoding = "KSC5601.1987-1";
	break;
    default:
	return 0;
    }
    if (data->flags & XctShiftMultiGRToGL)
	data->priv->flags |= ToGL;
    else
	data->priv->flags &= ~ToGL;
    data->GR_set_size = 94;
    data->GR_char_size = 2;
#ifdef notdef
    if (c < 0x60)
	data->GR_char_size = 2;
    else if (c < 0x70)
	data->GR_char_size = 3;
    else
	data->GR_char_size = 4;
#endif
    data->GLGR_encoding = (char *)NULL;
    return 1;
}

static int
HandleExtended(register XctData data, int c)
{
    register XctPriv priv = data->priv;
    XctString enc = data->item + 6;
    register XctString ptr = enc;
    unsigned i, len;

    while (*ptr != 0x02) {
	if (!*ptr || (++ptr == priv->ptr))
	    return 0;
    }
    data->item = ptr + 1;
    data->item_length = priv->ptr - data->item;
    len = ptr - enc;
    for (i = 0;
	 (i < priv->enc_count) &&
	 strncmp(priv->encodings[i], (char *)enc, len);
	 i++)
	;
    if (i == priv->enc_count) {
	XctString cp;
	char **new_encodings;

	for (cp = enc; cp != ptr; cp++) {
	    if ((!IsGL(*cp) && !IsGR(*cp)) || (*cp == 0x2a) || (*cp == 0x3f))
		return 0;
	}
	ptr = malloc(len + 1);
	memcpy(ptr, enc, len);
	ptr[len] = 0x00;
	priv->enc_count++;
	new_encodings = reallocarray(priv->encodings,
				     priv->enc_count, sizeof(char *));
	if (new_encodings == NULL) {
	    priv->enc_count--;
	    free(ptr);
	    return 0;
	}
	priv->encodings = new_encodings;
	priv->encodings[i] = (char *)ptr;
    }
    data->encoding = priv->encodings[i];
    data->char_size = c - 0x30;
    return 1;
}

static void
ShiftGRToGL(register XctData data, int hasCdata)
{
    register XctPriv priv = data->priv;
    register int i;

    if (data->item_length > priv->buf_count) {
	priv->buf_count = data->item_length;
	if (priv->itembuf)
	    priv->itembuf = realloc(priv->itembuf, priv->buf_count);
	else
	    priv->itembuf = malloc(priv->buf_count);
    }
    memcpy(priv->itembuf, data->item, data->item_length);
    data->item = priv->itembuf;
    if (hasCdata) {
	for (i = data->item_length; --i >= 0; ) {
	    if (IsGR(data->item[i]))
		data->item[i] &= 0x7f;
	}
    } else {
	for (i = data->item_length; --i >= 0; )
	    data->item[i] &= 0x7f;
    }
}

/* Create an XctData structure for parsing a Compound Text string. */
XctData
XctCreate(_Xconst unsigned char *string, int length, XctFlags flags)
{
    register XctData data;
    register XctPriv priv;

    data = malloc(sizeof(struct _XctRec) + sizeof(struct _XctPriv));
    if (!data)
	return data;
    data->priv = priv = (XctPriv)(data + 1);
    data->total_string = (XctString)string;
    data->total_length = length;
    data->flags = flags;
    priv->dirstack = (XctHDirection *)NULL;
    priv->dirsize = 0;
    priv->encodings = (char **)NULL;
    priv->enc_count = 0;
    priv->itembuf = (XctString)NULL;
    priv->buf_count = 0;
    XctReset(data);
    return data;
}

/* Reset the XctData structure to re-parse the string from the beginning. */
void
XctReset(register XctData data)
{
    register XctPriv priv = data->priv;

    priv->ptr = data->total_string;
    priv->ptrend = data->total_string + data->total_length;
    data->item = (XctString)NULL;
    data->item_length = 0;
    data->encoding = (char *)NULL;
    data->char_size = 1;
    data->horizontal = XctUnspecified;
    data->horz_depth = 0;
    priv->flags = 0;
    data->GL_set_size = data->GR_set_size = 0; /* XXX */
    (void)HandleGL(data, (unsigned char)0x42);
    (void)Handle96GR(data, (unsigned char)0x41);
    data->version = 1;
    data->can_ignore_exts = 0;
    /* parse version, if present */
    if ((data->total_length >= 4) &&
	(priv->ptr[0] == ESC) && (priv->ptr[1] == 0x23) &&
	IsI2(priv->ptr[2]) &&
	((priv->ptr[3] == 0x30) || (priv->ptr[3] == 0x31))) {
	data->version = priv->ptr[2] - 0x1f;
	if (priv->ptr[3] == 0x30)
	    data->can_ignore_exts = 1;
	priv->ptr += 4;
    }
}

/* Parse the next "item" from the Compound Text string.  The return value
 * indicates what kind of item is returned.  The item itself, and the current
 * contextual state, are reported as components of the XctData structure.
 */
XctResult
XctNextItem(register XctData data)
{
    register XctPriv priv = data->priv;
    unsigned char c;
    int len, bits;

#define NEXT data->item_length++; priv->ptr++

    while (IsMore(priv)) {
	data->item = priv->ptr;
	data->item_length = 0;
	c = *priv->ptr;
	if (c == ESC) {
	    NEXT;
	    while (IsMore(priv) && IsI2(*priv->ptr)) {
		NEXT;
	    }
	    if (!IsMore(priv))
		return XctError;
	    c = *priv->ptr;
	    NEXT;
	    if (!IsESCF(c))
		return XctError;
	    switch (data->item[1]) {
	    case 0x24:
		if (data->item_length > 3) {
		    if (data->item[2] == 0x28) {
			if (HandleMultiGL(data, c))
			    continue;
		    } else if (data->item[2] == 0x29) {
			if (HandleMultiGR(data, c))
			    continue;
		    }
		}
		break;
	    case 0x25:
		if ((data->item_length == 4) && (data->item[2] == 0x2f) &&
		    (c <= 0x3f)) {
		    if ((AmountLeft(priv) < 2) ||
			(priv->ptr[0] < 0x80) || (priv->ptr[1] < 0x80))
			return XctError;
		    len = *priv->ptr - 0x80;
		    NEXT;
		    len = (len << 7) + (*priv->ptr - 0x80);
		    NEXT;
		    if (AmountLeft(priv) < len)
			return XctError;
		    data->item_length += len;
		    priv->ptr += len;
		    if (c <= 0x34) {
			if (!HandleExtended(data, c) ||
			    ((data->horz_depth == 0) &&
			     (priv->flags & UsedDirection)))
			    return XctError;
			priv->flags |= UsedGraphic;
			return XctExtendedSegment;
		    }
		}
		break;
	    case 0x28:
		if (HandleGL(data, c))
		    continue;
		break;
	    case 0x29:
		if (Handle94GR(data, c))
		    continue;
		break;
	    case 0x2d:
		if (Handle96GR(data, c))
		    continue;
		break;
	    }
	} else if (c == CSI) {
	    NEXT;
	    while (IsMore(priv) && IsI3(*priv->ptr)) {
		NEXT;
	    }
	    while (IsMore(priv) && IsI2(*priv->ptr)) {
		NEXT;
	    }
	    if (!IsMore(priv))
		return XctError;
	    c = *priv->ptr;
	    NEXT;
	    if (!IsCSIF(c))
		return XctError;
	    if (c == 0x5d) {
		if ((data->item_length == 3) &&
		    ((data->item[1] == 0x31) || (data->item[1] == 0x32))) {
		    data->horz_depth++;
		    if (priv->dirsize < data->horz_depth) {
			XctHDirection *new_dirstack;
			priv->dirsize += 10;
			new_dirstack = reallocarray(priv->dirstack,
						    priv->dirsize,
						    sizeof(XctHDirection));
			if (new_dirstack == NULL) {
			    priv->dirsize -= 10;
			    return XctError;
			}
			priv->dirstack = new_dirstack;
		    }
		    priv->dirstack[data->horz_depth - 1] = data->horizontal;
		    if (data->item[1] == 0x31)
			data->horizontal = XctLeftToRight;
		    else
			data->horizontal = XctRightToLeft;
		    if ((priv->flags & UsedGraphic) &&
			!(priv->flags & UsedDirection))
			return XctError;
		    priv->flags |= UsedDirection;
		    if (data->flags & XctHideDirection)
			continue;
		    return XctHorizontal;
		} else if (data->item_length == 2) {
		    if (!data->horz_depth)
			return XctError;
		    data->horz_depth--;
		    data->horizontal = priv->dirstack[data->horz_depth];
		    if (data->flags & XctHideDirection)
			continue;
		    return XctHorizontal;
		}
	    }
	} else if (data->flags & XctSingleSetSegments) {
	    NEXT;
	    if IsC0(c) {
		data->encoding = (char *)NULL;
		data->char_size = 1;
		if (IsLegalC0(data, c))
		    return XctC0Segment;
	    } else if (IsGL(c)) {
		data->encoding = data->GL_encoding;
		data->char_size = data->GL_char_size;
		while (IsMore(priv) && IsGL(*priv->ptr)) {
		    NEXT;
		}
		if (((data->char_size > 1) &&
		     (data->item_length % data->char_size)) ||
		    ((data->horz_depth == 0) &&
		     (priv->flags & UsedDirection)))
		    return XctError;
		priv->flags |= UsedGraphic;
		return XctGLSegment;
	    } else if (IsC1(c)) {
		data->encoding = (char *)NULL;
		data->char_size = 1;
		if (IsLegalC1(data, c))
		    return XctC1Segment;
	    } else {
		data->encoding = data->GR_encoding;
		data->char_size = data->GR_char_size;
		while (IsMore(priv) && IsGR(*priv->ptr)) {
		    NEXT;
		}
		if (((data->char_size > 1) &&
		     (data->item_length % data->char_size)) ||
		    ((data->horz_depth == 0) &&
		     (priv->flags & UsedDirection)))
		    return XctError;
		priv->flags |= UsedGraphic;
		if (!(priv->flags & ToGL))
		    return XctGRSegment;
		ShiftGRToGL(data, 0);
		return XctGLSegment;
	    }
	} else {
	    bits = 0;
	    while (1) {
		if (IsC0(c) || IsC1(c)) {
		    if ((c == ESC) || (c == CSI))
			break;
		    if (IsC0(c) ? !IsLegalC0(data, c) : !IsLegalC1(data, c))
			break;
		    bits |= HasC;
		    NEXT;
		} else {
		    len = data->item_length;
		    if (IsGL(c)) {
			if ((data->flags & XctShiftMultiGRToGL) &&
			    (bits & HasGR))
			    break;
			NEXT;
			bits |= HasGL;
			while (IsMore(priv) && IsGL(*priv->ptr)) {
			    NEXT;
			}
			if ((data->GL_char_size > 1) &&
			    ((data->item_length - len) % data->GL_char_size))
			    return XctError;
		    } else {
			if ((data->flags & XctShiftMultiGRToGL) &&
			    (bits & HasGL))
			    break;
			NEXT;
			bits |= HasGR;
			while (IsMore(priv) && IsGR(*priv->ptr)) {
			    NEXT;
			}
			if ((data->GR_char_size > 1) &&
			    ((data->item_length - len) % data->GR_char_size))
			    return XctError;
		    }
		}
		if (!IsMore(priv))
		    break;
		c = *priv->ptr;
	    }
	    if (data->item_length) {
		if (bits & (HasGL|HasGR)) {
		    priv->flags |= UsedGraphic;
		    if ((data->horz_depth == 0) &&
			(priv->flags & UsedDirection))
			return XctError;
		    if ((data->flags & XctShiftMultiGRToGL) && (bits & HasGR))
			ShiftGRToGL(data, bits & HasC);
		}
		if ((bits == (HasGL|HasGR)) ||
		    (data->GLGR_encoding && !(bits & HasC))) {
		    data->encoding = data->GLGR_encoding;
		    if (data->GL_char_size == data->GR_char_size)
			data->char_size = data->GL_char_size;
		    else
			data->char_size = 0;
		} else if (bits == HasGL) {
		    data->encoding = data->GL_encoding;
		    data->char_size = data->GL_char_size;
		} else if (bits == HasGR) {
		    data->encoding = data->GR_encoding;
		    data->char_size = data->GR_char_size;
		} else {
		    data->encoding = (char *)NULL;
		    data->char_size = 1;
		    if ((bits & HasGL) &&
			(data->GL_char_size != data->char_size))
			data->char_size = 0;
		    if ((bits & HasGR) &&
			(data->GR_char_size != data->char_size))
			data->char_size = 0;
		}
		return XctSegment;
	    }
	    NEXT;
	}
	if (data->version <= XctVersion)
	    return XctError;
	if (data->flags & XctProvideExtensions)
	    return XctExtension;
	if (!data->can_ignore_exts)
	    return XctError;
    }
    return XctEndOfText;
}

/* Free all data associated with an XctDataStructure. */
void
XctFree(register XctData data)
{
    unsigned i;
    register XctPriv priv = data->priv;

    if (priv->dirstack)
	free(priv->dirstack);
    if (data->flags & XctFreeString)
	free(data->total_string);
    for (i = 0; i < priv->enc_count; i++)
	free(priv->encodings[i]);
    if (priv->encodings)
	free(priv->encodings);
    if (priv->itembuf)
	free(priv->itembuf);
    free(data);
}
