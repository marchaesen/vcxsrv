/****************************************************************

        Copyright 1992, 1993 by FUJITSU LIMITED
        Copyright 1993 by Fujitsu Open Systems Solutions, Inc.
	Copyright 1994 by Sony Corporation

Permission to use, copy, modify, distribute and sell this software
and its documentation for any purpose is hereby granted without fee,
provided that the above copyright notice appear in all copies and
that both that copyright notice and this permission notice appear
in supporting documentation, and that the name of FUJITSU LIMITED,
Fujitsu Open Systems Solutions, Inc. and Sony Corporation  not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.
FUJITSU LIMITED, Fujitsu Open Systems Solutions, Inc. and
Sony Corporation make no representations about the suitability of
this software for any purpose.  It is provided "as is" without
express or implied warranty.

FUJITSU LIMITED, FUJITSU OPEN SYSTEMS SOLUTIONS, INC. AND SONY
CORPORATION DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS,
IN NO EVENT SHALL FUJITSU OPEN SYSTEMS SOLUTIONS, INC., FUJITSU LIMITED
AND SONY CORPORATION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE
OR PERFORMANCE OF THIS SOFTWARE.

    Authors: Jeffrey Bloomfield		(jeffb@ossi.com)
	     Shigeru Yamada		(yamada@ossi.com)
             Yoshiyuki Segawa		(segawa@ossi.com)
    Modifier:Makoto Wakamatsu   Sony Corporation
				makoto@sm.sony.co.jp

*****************************************************************/

/*
 * A Japanese SJIS locale.
 * Supports: all locales with codeset SJIS.
 * How: Provides converters for SJIS.
 * Platforms: Only those defining X_LOCALE (only Lynx, Linux-libc5, OS/2).
 */

#ifdef X_LOCALE

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "Xlibint.h"
#include "XlcGeneric.h"

#include <ctype.h>
#ifdef WIN32
#define isascii __isascii
#endif

#define CS0	codesets[0]		/* Codeset 0 - 7-bit ASCII	*/
#define CS1	codesets[1]		/* Codeset 1 - Kanji		*/
#define CS2	codesets[2]		/* Codeset 2 - Half-Kana	*/
#define CS3	codesets[3]		/* Codeset 3 - User defined	*/

#define ascii		(codeset->cs_num == 0)
#define kanji		(codeset->cs_num == 1)
#define kana		(codeset->cs_num == 2)
#define userdef		(codeset->cs_num == 3)

#define	ASCII_CODESET	0
#define KANJI_CODESET	1
#define KANA_CODESET	2
#define USERDEF_CODESET	3
#define MAX_CODESETS	4

#define GR	0x80	/* begins right-side (non-ascii) region */
#define GL	0x7f    /* ends left-side (ascii) region        */

#define isleftside(c)	(((c) & GR) ? 0 : 1)
#define isrightside(c)	(!isleftside(c))

typedef unsigned char   Uchar;
typedef unsigned long	Ulong;
typedef unsigned int	Uint;

/* Acceptable range for 2nd byte of SJIS multibyte char */
#define VALID_MULTIBYTE(c) \
			((0x40<=((Uchar)c) && ((Uchar)c)<=0x7e) \
			|| (0x80<=((Uchar)c) && ((Uchar)c)<=0xfc))

#ifndef iskanji
#define iskanji(c)	((0x81<=((Uchar)c) && ((Uchar)c)<=0x9f) \
			|| (0xe0<=((Uchar)c) && ((Uchar)c)<=0xef))
#endif /* !iskanji */

#ifndef iskana
#define iskana(c)	(0xa1<=((Uchar)c) && ((Uchar)c)<=0xdf)
#endif /* !iskana */

#define	isuserdef(c)	(0xf0<=((Uchar)c) && ((Uchar)c)<=0xfc)

#define BIT8OFF(c)	((c) & GL)
#define BIT8ON(c)	((c) | GR)


static void jis_to_sjis (Uchar *p1, Uchar *p2);
static void sjis_to_jis (Uchar *p1, Uchar *p2);
static CodeSet wc_codeset (XLCd lcd, wchar_t wch);


/*
 * Notes:
 * 1.  16-bit widechar format is limited to 14 data bits.  Since the 2nd byte
 *     of SJIS multibyte chars are in the ranges of 0x40 - 7E and 0x80 - 0xFC,
 *     SJIS cannot map directly into 16 bit widechar format within the confines
 *     of a single codeset.  Therefore, for SJIS widechar conversion, SJIS Kanji
 *     is mapped into the JIS codeset.  (The algorithms used in jis_to_sjis()
 *     and sjis_to_jis() are from Ken Lunde (lunde@mv.us.adobe.com) and are in
 *     the public domain.)
 * 2.  Defining FORCE_INDIRECT_CONVERTER (see _XlcEucLoader())
 *     forces indirect (charset) conversions (e.g. wcstocs()<->cstombs()).
 * 3.  Using direct converters (e.g. mbstowcs()) decreases conversion
 *     times by 20-40% (depends on specific converter used).
 */


static int
sjis_mbstowcs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{

    XLCd lcd = (XLCd)conv->state;

    int chr_len = 0;
    int shift_mult = 0;
    Uint chrcode = 0;

    Uchar ch, ch2;
    Uint wc_encode = 0;
    Uint wc_tmp = 0;

    Bool new_char;

    int firstbyte;
    int length = 0;
    int num_conv;
    int unconv_num = 0;

    const char *inbufptr = *from;
    wchar_t *outbufptr = (wchar_t *) *to;
    wchar_t *outbuf_base = outbufptr;

    CodeSet *codesets = XLC_GENERIC(lcd, codeset_list);
    int codeset_num = XLC_GENERIC(lcd, codeset_num);
    Ulong wc_shift = XLC_GENERIC(lcd, wc_shift_bits);

    if (*from_left > *to_left)
	*from_left = *to_left;

     for (new_char = True, firstbyte = True; *from_left > 0; (*from_left)--) {

	ch = *inbufptr++;

	if (firstbyte) {
	    if (ASCII_CODESET >= codeset_num) {
		unconv_num++;
		(*from_left)--;
		continue;
	    }
	    if (isascii(ch)) {
		length = CS0->length;
		*outbufptr++ = (wchar_t)ch;
		continue;
	    }
	    else if (iskanji(ch)) {
		if (KANJI_CODESET >= codeset_num) {
		    unconv_num++;
		    (*from_left)--;
		    continue;
		}
		firstbyte = False;
		length = CS1->length;
		if (*from_left < length || *to_left < length)
		    return -1;
		wc_encode = CS1->wc_encoding;
		ch2 = *inbufptr;
		sjis_to_jis(&ch, &ch2);
		chrcode = ch;
	    }
	    else if (iskana(ch)) {
		if (KANA_CODESET >= codeset_num) {
		    unconv_num++;
		    (*from_left)--;
		    continue;
		}
		length = CS2->length;
		wc_encode = CS2->wc_encoding;
		chrcode = BIT8OFF(ch);
	    }
	    else if (isuserdef(ch)) {
		if (USERDEF_CODESET >= codeset_num) {
		    unconv_num++;
		    (*from_left)--;
		    continue;
		}
		firstbyte = False;
		length = CS3->length;
		if (*from_left < length || *to_left < length)
		    return -1;
		wc_encode = CS3->wc_encoding;
		ch2 = *inbufptr;
		sjis_to_jis(&ch, &ch2);
		chrcode = ch;
	    }
	    else /* unknown */ {
		unconv_num++;
		(*from_left)--;
		continue;
	    }
	} else {	     		/* 2nd byte of multibyte char */
	    if (!VALID_MULTIBYTE((Uchar) *(inbufptr-1))) {
		unconv_num++;
		firstbyte = True;
	    }
	    chrcode = ch2;
	}

	if (new_char) {
	    chr_len = length;
	    shift_mult = length - 1;
	    new_char = False;
	}

	chrcode <<= (wc_shift * shift_mult);
	shift_mult--;
	wc_tmp |= chrcode;
	if (--chr_len == 0) {
	    wc_tmp |= wc_encode;
	    *outbufptr++ = wc_tmp;

	    firstbyte = True;
	    new_char = True;
	    wc_tmp = (Uint)0;
	}

    } /* end for */

    *to = (XPointer)outbufptr;

    if ((num_conv = outbufptr - outbuf_base) > 0)
	(*to_left) -= num_conv;

    return unconv_num;
}


#define byte1		(length == codeset->length - 1)
#define byte2		(byte1 == 0)

static int
sjis_wcstombs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    const wchar_t *inbufptr = (const wchar_t *) *from;
    XPointer outbufptr = *to;
    XPointer outbuf_base = outbufptr;
    wchar_t  wch;
    int length;
    Uchar tmp;
    Uchar t1, t2;
    int num_conv;
    int unconv_num = 0;

    XLCd lcd = (XLCd)conv->state;
    CodeSet codeset;
    Ulong wc_shift = XLC_GENERIC(lcd, wc_shift_bits);

    if (*from_left > *to_left)
	*from_left = *to_left;

    for (; *from_left > 0 ; (*from_left)-- ) {

	wch = *inbufptr++;

	if (!(codeset = wc_codeset(lcd, wch))) {
	    unconv_num++;
	    (*from_left)--;
	    continue;
	}

	length = codeset->length;
	wch ^= (wchar_t)codeset->wc_encoding;

	do {
	    length--;
	    tmp = wch>>(wchar_t)( (Ulong)length * wc_shift);

	    if (kana)
		tmp = BIT8ON(tmp);

	    else if (byte1 && (kanji || userdef)) {
		t1 = BIT8OFF(tmp);
		continue;
	    }

	    else if (byte2 && (kanji || userdef)) {
		t2 = BIT8OFF(tmp);
		jis_to_sjis(&t1, &t2);
		*outbufptr++ = (char)t1;
		tmp = t2;
	    }

	    *outbufptr++ = (char)tmp;
	} while (length);

    }	/* end for */

    *to = (XPointer)outbufptr;

    if ((num_conv = (int)(outbufptr - outbuf_base)) > 0)
	(*to_left) -= num_conv;

    return unconv_num;
}
#undef byte1
#undef byte2

/*
 * sjis<->jis conversion for widechar kanji (See Note at top of file)
 */
static void
sjis_to_jis(
    Uchar *p1,
    Uchar *p2)
{
  Uchar c1 = *p1;
  Uchar c2 = *p2;
  Uchar adjust = c2 < 0x9f;
  Uchar rowOffset = c1 < 0xa0 ? 0x70 : 0xb0;
  Uchar cellOffset = adjust ? (0x1f + (c2 > 0x7f)) : 0x7e;

  *p1 = ((c1 - rowOffset) << 1) - adjust;
  *p2 -= cellOffset;
}

static void
jis_to_sjis(
    Uchar *p1,
    Uchar *p2)
{
  Uchar c1 = *p1;
  Uchar c2 = *p2;
  Uchar rowOffset = c1 < 0x5f ? 0x70 : 0xb0;
  Uchar cellOffset = c1 % 2 ? 0x1f + (c2 > 0x5f) : 0x7e;

  *p1 = ((Uchar)(c1 + 1) >> 1) + rowOffset;
  *p2 = c2 + cellOffset;
}

static CodeSet
wc_codeset(
    XLCd lcd,
    wchar_t wch)
{
    CodeSet *codesets = XLC_GENERIC(lcd, codeset_list);
#if !defined(__sony_news)  ||  defined(SVR4)
    int end = XLC_GENERIC(lcd, codeset_num);
    Ulong widech = (Ulong)(wch & XLC_GENERIC(lcd, wc_encode_mask));

    for (; --end >= 0; codesets++)
	if ( widech == (*codesets)->wc_encoding )
	    return *codesets;

    return NULL;
#else
    if( iskanji(wch >> 8) )
	return( codesets[1] );
    if( iskana(wch & 0xff) )
	return( codesets[2] );
    return( codesets[0] );
#endif
}


static int
sjis_mbtocs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    XLCd lcd = (XLCd)conv->state;
    XlcCharSet charset = NULL;
    int char_size = 0;
    int unconv_num = 0;
    const char *src = *from;
    char *dst = *to;
    CodeSet *codesets = XLC_GENERIC(lcd, codeset_list);
    int codeset_num = XLC_GENERIC(lcd, codeset_num);

    if (iskanji(*src)) {
	if (KANJI_CODESET >= codeset_num)
	    return -1;
	charset = *CS1->charset_list;
	char_size = charset->char_size;

	if (*from_left >= char_size && *to_left >= char_size) {
	    *dst++ = *src++;
	    *dst++ = *src++;
	    if (!VALID_MULTIBYTE((Uchar) *(src-1))) /* check 2nd byte */
		unconv_num++;
	    sjis_to_jis((Uchar *)(dst-2), (Uchar *)(dst-1));
	} else
	    return -1;
    }
    else if (isuserdef(*src)) {
	if (USERDEF_CODESET >= codeset_num)
	    return -1;
	charset = *CS3->charset_list;
	char_size = charset->char_size;

	if (*from_left >= char_size && *to_left >= char_size) {
	    *dst++ = *src++;
	    *dst++ = *src++;
	    if (!VALID_MULTIBYTE((Uchar) *(src-1))) /* check 2nd byte */
		unconv_num++;
	    sjis_to_jis((Uchar *)(dst-2), (Uchar *)(dst-1));
	} else
	    return -1;
    }
    else if (isascii(*src)) {
	if (ASCII_CODESET >= codeset_num)
	    return -1;
	charset = *CS0->charset_list;
	char_size = charset->char_size;

	if (*from_left >= char_size && *to_left >= char_size)
	    *dst++ = *src++;
	else
	    return -1;
    }
    else if (iskana(*src)) {
	if (KANA_CODESET >= codeset_num)
	    return  -1;
	charset = *CS2->charset_list;
	char_size = charset->char_size;

	if (*from_left >= char_size && *to_left >= char_size)
	    *dst++ = *src++;
	else
	   return -1;
    }
    else 	/* unknown */
	return -1;

    *from_left -= char_size;
    *to_left -= char_size;

    *to = (XPointer) dst;
    *from = (XPointer) src;

    if (num_args > 0)
	*((XlcCharSet *) args[0]) = charset;

    return unconv_num;
}


static int
sjis_mbstocs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    const char *tmp_from;
    char *tmp_to;
    int tmp_from_left, tmp_to_left;
    XlcCharSet charset, tmp_charset;
    XPointer tmp_args[1];
    int unconv_num = 0, ret;

/* Determine the charset of the segment and convert one character: */

    tmp_args[0] = (XPointer) &charset; /* charset from sjis_mbtocs() */
    while
      ((ret = sjis_mbtocs(conv, from, from_left, to, to_left, tmp_args, 1)) > 0)
	unconv_num += ret;
    if ( ret < 0 )
	return ret;

    tmp_from = *from;
    tmp_from_left = *from_left;
    tmp_to_left = *to_left;
    tmp_to = *to;

/* Convert remainder of the segment: */

    tmp_args[0] = (XPointer) &tmp_charset;
    while( (ret = sjis_mbtocs(conv, (XPointer *) &tmp_from, &tmp_from_left,
		     (XPointer *) &tmp_to, &tmp_to_left, tmp_args, 1)) >= 0 ) {

	if (ret > 0) {
	    unconv_num += ret;
	    continue;
	}

	if (tmp_charset != charset)  /* quit on end of segment */
	    break;

	*from = (XPointer) tmp_from;
	*from_left = tmp_from_left;
	*to = (XPointer) tmp_to;
	*to_left = tmp_to_left;
    }

    if (num_args > 0)
	*((XlcCharSet *) args[0]) = charset;

    return unconv_num;
}

static int
sjis_wcstocs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    XLCd lcd = (XLCd) conv->state;
    const wchar_t *wcptr = *((const wchar_t **)from);
    char *bufptr = *((char **) to);
    wchar_t wch;
    char *tmpptr;
    int length;
    CodeSet codeset;
    Ulong wc_encoding;
    int buf_len = *to_left;
    int wcstr_len = *from_left;

    if (!(codeset = wc_codeset(lcd, *wcptr)))
	return -1;

    if (wcstr_len < buf_len / codeset->length)
	buf_len = wcstr_len * codeset->length;

#if !defined(__sony_news)  ||  defined(SVR4)
    wc_encoding = codeset->wc_encoding;

    for ( ; wcstr_len > 0 && buf_len > 0; wcptr++, wcstr_len--) {
	wch = *wcptr;

	if ((wch & XLC_GENERIC(lcd, wc_encode_mask)) != wc_encoding)
	    break;

	length = codeset->length;

	buf_len -= length;
	bufptr += length;
	tmpptr = bufptr - 1;

	while (length--) {
	    *tmpptr-- = kana ? BIT8ON(wch) : BIT8OFF(wch);
	    wch >>= (wchar_t)XLC_GENERIC(lcd, wc_shift_bits);
	}
    }
#else
    length = codeset->length;
    for( ; wcstr_len > 0  &&  buf_len > 0; wcptr++, wcstr_len-- ) {
	wch = *wcptr;
	if( codeset != wc_codeset( lcd, wch ) )
	    break;

	buf_len -= length;
	if( length == 2 ) {
	    unsigned short	code;

	    code = sjis2jis( wch & 0xffff );
	    *bufptr++ = code >> 8;
	    *bufptr++ = code & 0xff;
	}
	else
	    *bufptr++ = wch & 0xff;
    }
#endif

    if (num_args > 0)
	*((XlcCharSet *) args[0]) = *codeset->charset_list;

    *from_left -= wcptr - (wchar_t *) *from;
    *from = (XPointer) wcptr;

    *to_left -= bufptr - *to;
    *to = bufptr;

    return 0;
}

static CodeSet
GetCodeSetFromCharSet(
    XLCd lcd,
    XlcCharSet charset)
{
    CodeSet *codeset = XLC_GENERIC(lcd, codeset_list);
    XlcCharSet *charset_list;
    int codeset_num, num_charsets;

    codeset_num = XLC_GENERIC(lcd, codeset_num);

    for ( ; codeset_num-- > 0; codeset++) {
	num_charsets = (*codeset)->num_charsets;
	charset_list = (*codeset)->charset_list;

	for ( ; num_charsets-- > 0; charset_list++)
	    if (*charset_list == charset)
		return *codeset;
    }

    return (CodeSet) NULL;
}


static int
sjis_cstombs(
    XlcConv conv,
    char **from,
    int *from_left,
    char **to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    XLCd lcd = (XLCd) conv->state;
    const char *csptr = *from;
    char *bufptr = *to;
    int csstr_len = *from_left;
    int buf_len = *to_left;
    int length;
    CodeSet codeset;
    int cvt_length = 0;

    if (num_args < 1)
	return -1;

    if (!(codeset = GetCodeSetFromCharSet(lcd, (XlcCharSet) args[0])))
	return -1;

    csstr_len /= codeset->length;
    buf_len /= codeset->length;
    if (csstr_len < buf_len)
	buf_len = csstr_len;

    cvt_length += buf_len * codeset->length;

    if (bufptr) {
	while (buf_len--) {
	    length = codeset->length;
	    while (length--)
		*bufptr++ = codeset->length == 1 && codeset->side == XlcGR ?
		  BIT8ON(*csptr++) : BIT8OFF(*csptr++);

	    if (codeset->length == 2)
		jis_to_sjis((Uchar *)(bufptr-2), (Uchar *)(bufptr-1));
	}
    }

    *from_left -= csptr - *from;
    *from = (XPointer) csptr;

    if (bufptr)
	*to += cvt_length;
    *to_left -= cvt_length;


    return 0;
}

static int
sjis_cstowcs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    XLCd lcd = (XLCd) conv->state;
    const char *csptr = (const char *) *from;
    wchar_t *bufptr = (wchar_t *) *to;
    wchar_t *toptr = (wchar_t *) *to;
    int csstr_len = *from_left;
    int buf_len = *to_left;
    wchar_t wch;
    int length;
    Ulong wc_shift_bits = (int)XLC_GENERIC(lcd, wc_shift_bits);
    CodeSet codeset;

    if (num_args < 1)
	return -1;

    if (!(codeset = GetCodeSetFromCharSet(lcd, (XlcCharSet) args[0])))
	return -1;

    csstr_len /= codeset->length;
    if (csstr_len < buf_len)
	buf_len = csstr_len;

    *to_left -= buf_len;

    if (bufptr) {

	toptr += buf_len;
	*to = (XPointer) toptr;

	while (buf_len--) {
	    wch = (wchar_t) BIT8OFF(*csptr);
	    csptr++;

	    length = codeset->length - 1;
	    while (length--) {
		wch = (wch << wc_shift_bits) | BIT8OFF(*csptr);
		csptr++;
	    }
	    *bufptr++ = wch | codeset->wc_encoding;
	}
    }

    *from_left -= csptr - *from;
    *from = (XPointer) csptr;

    return 0;
}


/*
 *    Stripped down Direct CT converters for SJIS
 *
 */

#define BADCHAR(min_ch, c)  (BIT8OFF(c) < (char)min_ch && BIT8OFF(c) != 0x0 && \
			     BIT8OFF(c) != '\t' && BIT8OFF(c) != '\n' && \
			     BIT8OFF(c) != 0x1b)

typedef struct _CTDataRec {
    int side;
    int length;
    char *name;
    Ulong wc_encoding;
    char *ct_encoding;
    int ct_encoding_len;
    int set_size;
    Uchar min_ch;
    Uchar ct_type;
} CTDataRec, *CTData;

typedef struct _StateRec {
    CTData GL_charset;
    CTData GR_charset;
    CTData charset;
} StateRec, *State;

#define CT_STD  0
#define CT_NSTD 1
#define CT_DIR  2
#define CT_EXT0 3
#define CT_EXT1 4
#define CT_EXT2 5
#define CT_VER  6

static CTDataRec ctdata[] =
{
    { XlcGL,      1, "ISO8859-1:GL",       0, "\033(B"   ,  3, 0, 0, CT_STD  },
    { XlcGR,      1, "ISO8859-1:GR",       0, "\033-A"   ,  3, 0, 0, CT_STD  },
    { XlcGL,      1, "JISX0201.1976-0:GL", 0, "\033(J"   ,  3, 0, 0, CT_STD  },
    { XlcGR,      1, "JISX0201.1976-0:GR", 0, "\033)I"   ,  3, 0, 0, CT_STD  },
    { XlcGL,      2, "JISX0208.1983-0:GL", 0, "\033$(B"  ,  4, 0, 0, CT_STD  },
    { XlcGR,      2, "JISX0208.1983-0:GR", 0, "\033$)B"  ,  4, 0, 0, CT_STD  },
    { XlcGL,      2, "JISX0212.1990-0:GL", 0, "\033$(D"  ,  4, 0, 0, CT_STD  },
    { XlcGR,      2, "JISX0212.1990-0:GR", 0, "\033$)D"  ,  4, 0, 0, CT_STD  },
    { XlcUnknown, 0, "Ignore-Ext-Status?", 0, "\033#"    ,  2, 0, 0, CT_VER  },
    { XlcUnknown, 0, "NonStd-?-OctetChar", 0, "\033%/0"  ,  4, 0, 0, CT_NSTD },
    { XlcUnknown, 1, "NonStd-1-OctetChar", 0, "\033%/1"  ,  4, 0, 0, CT_NSTD },
    { XlcUnknown, 2, "NonStd-2-OctetChar", 0, "\033%/2"  ,  4, 0, 0, CT_NSTD },
    { XlcUnknown, 3, "NonStd-3-OctetChar", 0, "\033%/3"  ,  4, 0, 0, CT_NSTD },
    { XlcUnknown, 4, "NonStd-4-OctetChar", 0, "\033%/4"  ,  4, 0, 0, CT_NSTD },
    { XlcUnknown, 0, "Extension-2"       , 0, "\033%/"   ,  3, 0, 0, CT_EXT2 },
    { XlcUnknown, 0, "Extension-0"       , 0, "\033"     ,  1, 0, 0, CT_EXT0 },
    { XlcUnknown, 0, "Begin-L-to-R-Text",  0, "\2331]"   ,  3, 0, 0, CT_DIR  },
    { XlcUnknown, 0, "Begin-R-to-L-Text",  0, "\2332]"   ,  3, 0, 0, CT_DIR  },
    { XlcUnknown, 0, "End-Of-String",      0, "\233]"    ,  2, 0, 0, CT_DIR  },
    { XlcUnknown, 0, "Extension-1"       , 0, "\233"     ,  1, 0, 0, CT_EXT1 },
};

/* Note on above table:  sjis_ctstombs() and sjis_ctstowcs() parser depends on
 * certain table entries occuring in decreasing string length--
 *   1.  CT_EXT2 and CT_EXT0 entries must occur after CT_NSTD entries.
 *   2.  CT_DIR and CT_EXT1 entries must occur after CT_DIR entries.
 */

static CTData ctdptr[sizeof(ctdata) / sizeof(CTDataRec)];
static CTData ctd_endp = ctdata + ((sizeof(ctdata) / sizeof(CTDataRec))) - 1;

#define Ascii   0
#define Kanji   1
#define Kana    2
#define Userdef 3

/*
 * initCTptr(): Set ctptr[] to point at ctdata[], indexed by codeset_num.
 */
static void
initCTptr(
    XLCd lcd)
{
    int num_codesets = XLC_GENERIC(lcd, codeset_num);
    int num_charsets;
    int i, j;
    CodeSet *codesets = XLC_GENERIC(lcd, codeset_list);
    CodeSet codeset;
    XlcCharSet charset;
    CTData ctdp = ctdata;

    ctdptr[Ascii] = &ctdata[0];         /* failsafe */

    for (i = 0; i < num_codesets; i++) {

	codeset = codesets[i];
	num_charsets = codeset->num_charsets;

	for (j = 0; j < num_charsets; j++) {

	    charset = codeset->charset_list[j];

	    for (ctdp = ctdata; ctdp <= ctd_endp; ctdp++)

		if (! strcmp(ctdp->name, charset->name)) {

		    ctdptr[codeset->cs_num] = ctdp;

		    ctdptr[codeset->cs_num]->wc_encoding = codeset->wc_encoding;

		    ctdptr[codeset->cs_num]->set_size =
		      charset->set_size;

		    ctdptr[codeset->cs_num]->min_ch =
		      charset->set_size == 94 &&
		      (ctdptr[codeset->cs_num]->length > 1 ||
		      ctdptr[codeset->cs_num]->side == XlcGR) ? 0x21 : 0x20;

		    break;
		}
	}
    }
}


static int
sjis_mbstocts(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    int ct_len = *to_left;
    int cs_num;
    int clen;
    int unconv_num = 0;
    int num_conv;
    const char *inbufptr = *from;
    char *ctptr = *to;
    XPointer ct_base = ctptr;

    StateRec ct_state;
    CTData charset = NULL;
    XLCd lcd = (XLCd) conv->state;
    int codeset_num = XLC_GENERIC(lcd, codeset_num);

/* Initial State: */

    ct_state.GL_charset = ctdptr[Ascii];
    ct_state.GR_charset = NULL;

    if (*from_left > *to_left)
        *from_left = *to_left;

    for (;*from_left > 0; (*from_left) -= charset->length) {

	if (iskanji(*inbufptr)) {
	    if (KANJI_CODESET >= codeset_num) {
		unconv_num++;
		(*from_left)--;
		continue;
	    }
	    cs_num = Kanji;
	    charset = ctdptr[Kanji];
	    if (!VALID_MULTIBYTE((Uchar) *(inbufptr+1)))
		unconv_num++;
	}
	else if (isuserdef(*inbufptr)) {
	    if (USERDEF_CODESET >= codeset_num) {
		unconv_num++;
		(*from_left)--;
		continue;
	    }
	    cs_num = Userdef;
	    charset = ctdptr[Userdef];
	    if (!VALID_MULTIBYTE((Uchar) *(inbufptr+1)))
		unconv_num++;
	}
	else if (isascii(*inbufptr)) {
	    if (ASCII_CODESET >= codeset_num) {
		unconv_num++;
		(*from_left)--;
		continue;
	    }
	    cs_num = Ascii;
	    charset = ctdptr[Ascii];
	}
	else if (iskana(*inbufptr)) {
	    if (KANA_CODESET >= codeset_num) {
		unconv_num++;
		(*from_left)--;
		continue;
	    }
	    cs_num = Kana;
	    charset = ctdptr[Kana];
	}
	else { 		 /* unknown */
	    unconv_num++;
	    (*from_left)--;
	    continue;
	}

	if ( (charset->side == XlcGR && charset != ct_state.GR_charset) ||
	     (charset->side == XlcGL && charset != ct_state.GL_charset) ) {

	    ct_len -= ctdptr[cs_num]->ct_encoding_len;
	    if (ct_len < 0) {
		unconv_num++;
		break;
	    }

	    if (ctptr) {
		strcpy(ctptr, ctdptr[cs_num]->ct_encoding);
		ctptr += ctdptr[cs_num]->ct_encoding_len;
	    }
	}

	clen = charset->length;
	do {
	    *ctptr++ = *inbufptr++;
	} while (--clen);

	if (charset->length >= 2) {
	    sjis_to_jis((Uchar *)(ctptr-2), (Uchar *)(ctptr-1));
	    if (BADCHAR(charset->min_ch, *(ctptr-2)) ||
		  BADCHAR(charset->min_ch, *(ctptr-1))) {
		unconv_num++;
		continue;
	    }
	}
	else
	    if (BADCHAR(charset->min_ch, *(ctptr-1))) {
		unconv_num++;
		continue;
	    }

	if (charset->side == XlcGR)
	    ct_state.GR_charset = charset;
	else if (charset->side == XlcGL)
	    ct_state.GL_charset = charset;

	if (charset->side == XlcGR) {
	  clen = charset->length;
	  do {
	    (*(Uchar *)(ctptr-clen)) = BIT8ON(*(Uchar *)(ctptr-clen));
	  } while (--clen);
	}
    }

    *to = (XPointer)ctptr;

    if ((num_conv = (int)(ctptr - ct_base)) > 0)
	(*to_left) -= num_conv;

    return unconv_num;

}


#define byte1		(length == codeset->length - 1)
#define byte2		(byte1 == 0)

static int
sjis_wcstocts(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    int ct_len = *to_left;
    const wchar_t *inbufptr = (const wchar_t *) *from;
    char *ctptr = *to;
    XPointer ct_base = ctptr;
    wchar_t  wch;
    int length;
    Uchar tmp;
    Uchar t1 = 0;
    int num_conv;

    StateRec ct_state;
    XLCd lcd = (XLCd)conv->state;
    CTData charset;
    CodeSet codeset;
    int unconv_num = 0;
    Ulong wc_shift = XLC_GENERIC(lcd, wc_shift_bits);

/* Initial State: */
    ct_state.GL_charset = ctdptr[0]; /* Codeset 0 */
    ct_state.GR_charset = NULL;

    if (*from_left > *to_left)
	*from_left = *to_left;

    for (; *from_left > 0 ; (*from_left)-- ) {

	wch = *inbufptr++;

        if (!(codeset = wc_codeset(lcd, wch))) {
            unconv_num++;
            (*from_left)--;
            continue;
        }

	charset = ctdptr[codeset->cs_num];

	length = codeset->length;
	wch ^= (wchar_t)codeset->wc_encoding;

	if ( (charset->side == XlcGR && charset != ct_state.GR_charset) ||
	     (charset->side == XlcGL && charset != ct_state.GL_charset) ) {

	    ct_len -= ctdptr[codeset->cs_num]->ct_encoding_len;
	    if (ct_len < 0) {
		unconv_num++;
		break;
	    }

	    if (ctptr) {
		strcpy(ctptr, ctdptr[codeset->cs_num]->ct_encoding);
		ctptr += ctdptr[codeset->cs_num]->ct_encoding_len;
	    }

	}

	if (charset->side == XlcGR)
	    ct_state.GR_charset = charset;
	else if (charset->side == XlcGL)
	    ct_state.GL_charset = charset;

	do {
	    length--;
	    tmp = wch>>(wchar_t)( (Ulong)length * wc_shift);

	    if (kana) {
		if (BADCHAR(charset->min_ch, (char)tmp)) {
		    unconv_num++;
		    break;
		}
		*ctptr++ = (char)BIT8ON(tmp);
	    }

	    else if (byte1 && (kanji || userdef)) {
		t1 = tmp;
	    }

	    else if (byte2 && (kanji || userdef)) {
		if (BADCHAR(charset->min_ch, (char)t1) ||
		  BADCHAR(charset->min_ch, (char)tmp)) {
		    unconv_num++;
		    break;
		}

		*ctptr++ = (char)BIT8OFF(t1);
		*ctptr++ = (char)BIT8OFF(tmp);
	    }

	    else {
		if (BADCHAR(charset->min_ch, (char)tmp)) {
		    unconv_num++;
		    break;
		}
		*ctptr++ = (char)tmp;
	    }
	} while (length);

    }	/* end for */

    *to = (XPointer)ctptr;

    if ((num_conv = (int)(ctptr - ct_base)) > 0)
	(*to_left) -= num_conv;

    return unconv_num;
}
#undef byte1
#undef byte2

#define SKIP_I(str)	while (*(str) >= 0x20 && *(str) <=  0x2f) (str)++;
#define SKIP_P(str)	while (*(str) >= 0x30 && *(str) <=  0x3f) (str)++;

static int
sjis_ctstombs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    const char *inbufptr =  *from;
    XPointer outbufptr =  *to;
    const char *inbuf_base;
    XPointer outbuf_base = outbufptr;
    int clen, length;
    int unconv_num = 0;
    int num_conv;
    unsigned int ct_seglen = 0;
    Uchar ct_type;
    CTData ctdp = ctdata;	/* default */
    CTData GL_ctdp = ctdp;	/* GL ctdp save */
    CTData GR_ctdp = ctdp;	/* GR ctdp save */

    if (*from_left > *to_left)
	*from_left = *to_left;

    for (length = ctdata[Ascii].length; *from_left > 0 ; (*from_left) -= length)
    {
	ct_type = CT_STD;
	/* change GL/GR charset */
	if(ctdp->side == XlcGR && isleftside(*inbufptr)){
	    /* select GL side */
	    ctdp = GL_ctdp;
	    length = ctdp->length;
	    ct_type = ctdp->ct_type;
	}else if(ctdp->side == XlcGL && isrightside(*inbufptr)){
	    /* select GR side */
	    ctdp = GR_ctdp;
	    length = ctdp->length;
	    ct_type = ctdp->ct_type;
	}
	if (*inbufptr == '\033' || *inbufptr == (char)'\233') {

	    for (ctdp = ctdata; ctdp <= ctd_endp ; ctdp++) {

		if(!strncmp(inbufptr, ctdp->ct_encoding, ctdp->ct_encoding_len))
		{
		    inbufptr += ctdp->ct_encoding_len;
		    (*from_left) -= ctdp->ct_encoding_len;
		    if( ctdp->length ) {
			length = ctdp->length;
			if( *from_left < length ) {
			    *to = (XPointer)outbufptr;
			    *to_left -= outbufptr - outbuf_base;
			    return( unconv_num + *from_left );
			}
		    }
		    ct_type = ctdp->ct_type;
		    if(ctdp->side == XlcGL){
			GL_ctdp = ctdp; /* save GL ctdp */
		    }else{
			GR_ctdp = ctdp; /* save GR ctdp */
		    }
		    break;
		}
	    }
	    if (ctdp > ctd_endp)  	/* failed to match CT sequence */
		unconv_num++;
	}

/* The following code insures that non-standard encodings, direction, extension,
 * and version strings are ignored; subject to change in future.
 */
	switch (ct_type) {
	  case CT_STD:
	    break;
	  case CT_EXT2:
	    inbufptr++;
	    (*from_left)--;
	  case CT_NSTD:
	    ct_seglen = (BIT8OFF(*inbufptr) << 7) + BIT8OFF(*(inbufptr+1)) + 2;
	    inbufptr += ct_seglen;
	    (*from_left) -= ct_seglen;
	    continue;
	  case CT_EXT0:
	    inbuf_base = inbufptr;
	    SKIP_I(inbufptr);
	    inbufptr++;
	    ct_seglen = (unsigned)(inbufptr - inbuf_base);
	    (*from_left) -= ct_seglen;
	    continue;
	  case CT_EXT1:
	    inbuf_base = inbufptr;
	    SKIP_P(inbufptr);
	    SKIP_I(inbufptr);
	    inbufptr++;
	    ct_seglen = (unsigned)(inbufptr - inbuf_base);
	    (*from_left) -= ct_seglen;
	    continue;
	  case CT_DIR:
	    continue;
	  case CT_VER:
	    inbufptr += 2;
	    (*from_left) -= 2;
	    continue;
	}

	if (ctdp->side == XlcGL || isrightside (*inbufptr)) {
	    clen = length;
	} else {
	    clen = 1;
	    *from_left += length - clen;
	}
	do {
	    Uchar mask = (length == 2) ? GL : -1;
	    *outbufptr++ = *inbufptr++ & mask;
	} while (--clen);

	if (length >= 2)
	    jis_to_sjis((Uchar *)(outbufptr-2), (Uchar *)(outbufptr-1));
    }

    *to = (XPointer)outbufptr;

    if ((num_conv = (int)(outbufptr - outbuf_base)) > 0)
	(*to_left) -= num_conv;

    return unconv_num;
}


static int
sjis_ctstowcs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    XLCd lcd = (XLCd)conv->state;
    Ulong wc_shift_bits = XLC_GENERIC(lcd, wc_shift_bits);
    const char *inbufptr = *from;
    const char *inbuf_base;
    wchar_t *outbufptr = (wchar_t *) *to;
    wchar_t *outbuf_base = outbufptr;
    int clen, length;
    int num_conv;
    int unconv_num = 0;
    unsigned int ct_seglen = 0;
    Uchar ct_type = 0;
    int shift_mult;
    wchar_t wc_tmp;
    wchar_t wch;
    Ulong wc_encoding;
    CTData ctdp = ctdata;
    CTData GL_ctdp = ctdp;	/* GL ctdp save */
    CTData GR_ctdp = ctdp;	/* GR ctdp save */

    if (*from_left > *to_left)
	*from_left = *to_left;

    for (length = ctdata[Ascii].length; *from_left > 0; (*from_left) -= length )
    {
	ct_type = CT_STD;
	/* change GL/GR charset */
	if(ctdp->side == XlcGR && isleftside(*inbufptr)){
	    /* select GL side */
	    ctdp = GL_ctdp;
	    length = ctdp->length;
	    ct_type = ctdp->ct_type;
	}else if(ctdp->side == XlcGL && isrightside(*inbufptr)){
	    /* select GR side */
	    ctdp = GR_ctdp;
	    length = ctdp->length;
	    ct_type = ctdp->ct_type;
	}
	if (*inbufptr == '\033' || *inbufptr == (char)'\233') {
	    for (ctdp = ctdata; ctdp <= ctd_endp ; ctdp++) {

		if(!strncmp(inbufptr, ctdp->ct_encoding, ctdp->ct_encoding_len))
		{
		    inbufptr += ctdp->ct_encoding_len;
		    (*from_left) -= ctdp->ct_encoding_len;
		    if( ctdp->length ) {
			length = ctdp->length;
			if( *from_left < length ) {
			    *to = (XPointer)outbufptr;
			    *to_left -= outbufptr - outbuf_base;
			    return( unconv_num + *from_left );
			}
		    }
		    ct_type = ctdp->ct_type;
		    if(ctdp->side == XlcGL){
			GL_ctdp = ctdp; /* save GL ctdp */
		    }else{
			GR_ctdp = ctdp; /* save GR ctdp */
		    }
		    break;
		}
	    }
	    if (ctdp > ctd_endp)    	/* failed to match CT sequence */
		unconv_num++;
	}

/* The following block of code insures that non-standard encodings, direction,
 * extension, and version strings are ignored; subject to change in future.
 */
	switch (ct_type) {
	  case CT_STD:
	    break;
	  case CT_EXT2:
	    inbufptr++;
	    (*from_left)--;
	  case CT_NSTD:
	    ct_seglen = (BIT8OFF(*inbufptr) << 7) + BIT8OFF(*(inbufptr+1)) + 2;
	    inbufptr += ct_seglen;
	    (*from_left) -= ct_seglen;
	    continue;
	  case CT_EXT0:
	    inbuf_base = inbufptr;
	    SKIP_I(inbufptr);
	    inbufptr++;
	    ct_seglen = (unsigned)(inbufptr - inbuf_base);
	    (*from_left) -= ct_seglen;
	    continue;
	  case CT_EXT1:
	    inbuf_base = inbufptr;
	    SKIP_P(inbufptr);
	    SKIP_I(inbufptr);
	    inbufptr++;
	    ct_seglen = (unsigned)(inbufptr - inbuf_base);
	    (*from_left) -= ct_seglen;
	    continue;
	  case CT_DIR:
	    continue;
	  case CT_VER:
	    inbufptr += 2;
	    (*from_left) -= 2;
	    continue;
	}
#if !defined(__sony_news)  ||  defined(SVR4)
	if (ctdp->side == XlcGL || isrightside (*inbufptr)) {
	    clen = length;
	    wc_encoding = ctdp->wc_encoding;
	} else {
	    clen = 1;
	    *from_left += length - clen;
	    wc_encoding = ctdptr[Ascii]->wc_encoding;
	}
	shift_mult = clen - 1;
	wch = (wchar_t)0;

	do {
	    wc_tmp = BIT8OFF(*inbufptr++) << (wc_shift_bits * shift_mult);
	    wch |= wc_tmp;
	    shift_mult--;
	} while (--clen);
	*outbufptr++ = wch | wc_encoding;
#else
	if( length == 1 )
	    *outbufptr++ = (unsigned char)*inbufptr++;
	else if( length == 2 ) {
	    unsigned short	code;
	    code = (*inbufptr << 8) | *(inbufptr+1);
	    *outbufptr++ = jis2sjis( code );
	    inbufptr += 2;
	}
#endif
    }
    *to = (XPointer)outbufptr;

    if ((num_conv = (int)(outbufptr - outbuf_base)) > 0)
	(*to_left) -= num_conv ;

    return unconv_num;

}
#undef BADCHAR

static void
close_converter(
    XlcConv conv)
{
	Xfree((char *) conv);
}


static XlcConv
create_conv(
    XLCd lcd,
    XlcConvMethods methods)
{
    XlcConv conv;

    conv = (XlcConv) Xmalloc(sizeof(XlcConvRec));
    if (conv == NULL)
	return (XlcConv) NULL;

    conv->methods = methods;
    conv->state = (XPointer) lcd;
    return conv;
}


enum { MBSTOCS, WCSTOCS, MBTOCS, CSTOMBS, CSTOWCS, MBSTOWCS, WCSTOMBS,
       WCSTOCTS, MBSTOCTS, CTSTOMBS, CTSTOWCS };

static XlcConvMethodsRec conv_methods[] = {
    {close_converter, sjis_mbstocs,  NULL },
    {close_converter, sjis_wcstocs,  NULL },
    {close_converter, sjis_mbtocs,   NULL },
    {close_converter, sjis_cstombs,  NULL },
    {close_converter, sjis_cstowcs,  NULL },
    {close_converter, sjis_mbstowcs, NULL },
    {close_converter, sjis_wcstombs, NULL },
    {close_converter, sjis_wcstocts, NULL },
    {close_converter, sjis_mbstocts, NULL },
    {close_converter, sjis_ctstombs, NULL },
    {close_converter, sjis_ctstowcs, NULL },
};


static XlcConv
open_mbstocs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &conv_methods[MBSTOCS]);
}

static XlcConv
open_wcstocs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &conv_methods[WCSTOCS]);
}

static XlcConv
open_mbtocs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &conv_methods[MBTOCS]);
}

static XlcConv
open_cstombs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &conv_methods[CSTOMBS]);
}

static XlcConv
open_cstowcs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &conv_methods[CSTOWCS]);
}

static XlcConv
open_mbstowcs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &conv_methods[MBSTOWCS]);
}

static XlcConv
open_wcstombs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &conv_methods[WCSTOMBS]);
}

static XlcConv
open_wcstocts(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &conv_methods[WCSTOCTS]);
}

static XlcConv
open_mbstocts(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &conv_methods[MBSTOCTS]);
}

static XlcConv
open_ctstombs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &conv_methods[CTSTOMBS]);
}

static XlcConv
open_ctstowcs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &conv_methods[CTSTOWCS]);
}

XLCd
_XlcSjisLoader(
    const char *name)
{
    XLCd lcd;

    lcd = _XlcCreateLC(name, _XlcGenericMethods);
    if (lcd == NULL)
	return lcd;

    if (!XLC_PUBLIC_PART(lcd)->codeset ||
	(_XlcCompareISOLatin1(XLC_PUBLIC_PART(lcd)->codeset, "SJIS"))) {
	_XlcDestroyLC(lcd);
	return (XLCd) NULL;
    }

    initCTptr(lcd);

    _XlcSetConverter(lcd, XlcNMultiByte, lcd, XlcNCharSet, open_mbstocs);
    _XlcSetConverter(lcd, XlcNWideChar, lcd, XlcNCharSet, open_wcstocs);
    _XlcSetConverter(lcd, XlcNCharSet, lcd, XlcNMultiByte, open_cstombs);
    _XlcSetConverter(lcd, XlcNCharSet, lcd, XlcNWideChar, open_cstowcs);
    _XlcSetConverter(lcd, XlcNMultiByte, lcd, XlcNChar, open_mbtocs);

#ifndef FORCE_INDIRECT_CONVERTER
    _XlcSetConverter(lcd, XlcNCompoundText, lcd, XlcNMultiByte, open_ctstombs);
    _XlcSetConverter(lcd, XlcNCompoundText, lcd, XlcNWideChar, open_ctstowcs);
    _XlcSetConverter(lcd, XlcNMultiByte, lcd, XlcNCompoundText, open_mbstocts);
    _XlcSetConverter(lcd, XlcNMultiByte, lcd, XlcNWideChar, open_mbstowcs);
    _XlcSetConverter(lcd, XlcNWideChar, lcd, XlcNCompoundText, open_wcstocts);
    _XlcSetConverter(lcd, XlcNWideChar, lcd, XlcNMultiByte, open_wcstombs);
#endif

    _XlcAddUtf8Converters(lcd);

    return lcd;
}

#else
typedef int dummy;
#endif /* X_LOCALE */
