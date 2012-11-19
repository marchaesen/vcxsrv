/******************************************************************

        Copyright 1992, 1993 by FUJITSU LIMITED
        Copyright 1993 by Fujitsu Open Systems Solutions, Inc.

Permission to use, copy, modify, distribute and sell this software
and its documentation for any purpose is hereby granted without fee,
provided that the above copyright notice appear in all copies and
that both that copyright notice and this permission notice appear
in supporting documentation, and that the name of FUJITSU LIMITED and
Fujitsu Open Systems Solutions, Inc. not be used in advertising or
publicity pertaining to distribution of the software without specific,
written prior permission.
FUJITSU LIMITED and Fujitsu Open Systems Solutions, Inc. makes no
representations about the suitability of this software for any purpose.
It is provided "as is" without express or implied warranty.

FUJITSU LIMITED AND FUJITSU OPEN SYSTEMS SOLUTIONS, INC. DISCLAIMS ALL
WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL FUJITSU OPEN SYSTEMS
SOLUTIONS, INC. AND FUJITSU LIMITED BE LIABLE FOR ANY SPECIAL, INDIRECT
OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
OF THIS SOFTWARE.

    Authors: Shigeru Yamada 		(yamada@ossi.com)
             Jeffrey Bloomfield		(jeffb@ossi.com)
             Yoshiyuki Segawa		(segawa@ossi.com)

*****************************************************************/

/*
 * An EUC locale.
 * Supports: all locales with codeset eucJP, eucKR, eucCN, eucTW.
 * How: Provides converters for euc*.
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

#define CS0     codesets[0]             /* Codeset 0 - 7-bit ASCII      */
#define CS1     codesets[1]             /* Codeset 1 - Kanji            */
#define CS2     codesets[2]             /* Codeset 2 - Half-Kana        */
#define CS3     codesets[3]             /* Codeset 3 - User defined     */

#define SS2	0x8e	/* Single-shift char: CS2 */
#define SS3	0x8f    /* Single-shift char: CS3 */

#define ASCII_CODESET   0
#define KANJI_CODESET   1
#define KANA_CODESET    2
#define USERDEF_CODESET 3
#define MAX_CODESETS

#define GR	0x80	/* begins right-side (non-ascii) region */
#define GL	0x7f    /* ends left-side (ascii) region        */

#define isleftside(c)	(((c) & GR) ? 0 : 1)
#define isrightside(c)	(!isleftside(c))

#define BIT8OFF(c)	((c) & GL)
#define BIT8ON(c)	((c) | GR)

typedef unsigned char   Uchar;
typedef unsigned long   Ulong;
typedef unsigned int	Uint;

static CodeSet GetCodeSetFromCharSet (XLCd lcd, XlcCharSet charset);
static CodeSet wc_codeset (XLCd lcd, wchar_t wch);

#define BADCHAR(min_ch, c)  (BIT8OFF(c) < (char)min_ch && BIT8OFF(c) != 0x0 && \
			     BIT8OFF(c) != '\t' && BIT8OFF(c) != '\n' && \
			     BIT8OFF(c) != 0x1b)

/*
 * Notes:
 * 1. Defining FORCE_INDIRECT_CONVERTER (see _XlcEucLoader())
 *    forces indirect (charset) conversions (e.g. wcstocs()<->cstombs()).
 * 2. Using direct converters (e.g. mbstowcs()) decreases conversion
 *    times by 20-40% (depends on specific converter used).
 */

static int
euc_mbstowcs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    XLCd lcd = (XLCd)conv->state;

    Uchar ch;
    int chr_len = 0;
    int sshift =  False;
    int shift_mult = 0;
    Uint chrcode;

    Uint wc_encode = 0;
    Uint wc_tmp = 0;

    int cs0flg = False;
    int cs1flg = False;
    int length = 0;
    int unconv_num = 0;

    Bool new_char;

    const char *inbufptr = *from;
    wchar_t *outbufptr = (wchar_t *) *to;

    CodeSet *codesets = XLC_GENERIC(lcd, codeset_list);
    int codeset_num = XLC_GENERIC(lcd, codeset_num);
    Ulong wc_shift = XLC_GENERIC(lcd, wc_shift_bits);

    for (new_char = True; *from_left > 0 && *to_left > 0;) {

	ch = *inbufptr++;

	if (isleftside(ch)) {				/* CS0 */
            if (ASCII_CODESET >= codeset_num) {
		unconv_num++;
		(*from_left)--;
		continue;
	    }
	    if( cs0flg == True) {
		new_char = True;
		cs0flg = False;
	    }
	    length = CS0->length;
	    *outbufptr++ = (wchar_t)ch;
	    (*from_left)--;
	    (*to_left)--;
	    continue;
	}
	else if (ch == SS2) {				/* CS2 */
	    if (KANA_CODESET >= codeset_num) {
		unconv_num++;
		(*from_left)--;
		continue;
	    }
	    if (sshift == True || cs1flg == True) {
		cs1flg = False;
		unconv_num++;
		continue;
	    }
	    length = CS2->length;
	    wc_encode = CS2->wc_encoding;
	    chrcode = 0;
	    sshift = True;
	    cs0flg = True;
	    (*from_left)--;
	    continue;
	}
	else if (ch == SS3) {				/* CS3 */
	    if (USERDEF_CODESET >= codeset_num) {
		unconv_num++;
		(*from_left)--;
		continue;
	    }
	    if (sshift == True || cs1flg == True) {
		cs1flg = False;
		unconv_num++;
		continue;
	    }
	    length = CS3->length;
	    if (*from_left < 1 )
		unconv_num++;
	    wc_encode = CS3->wc_encoding;
	    chrcode = 0;
	    sshift = True;
	    cs0flg = True;
	    (*from_left)--;
	    continue;

	} else {					/* CS1 */
	    if (KANJI_CODESET >= codeset_num) {
		unconv_num++;
		(*from_left)--;
		continue;
	    }
	    if (sshift == False) {
		length = CS1->length;
		if (*from_left < 1)
		    unconv_num++;
		wc_encode = CS1->wc_encoding;
	    }
	    chrcode = BIT8OFF(ch);
	    cs0flg = True;
	    cs1flg = True;
	    (*from_left)--;
	}

	if (new_char) {				/* begin new character */
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
	    (*to_left)--;

	    new_char = True;
	    sshift = False;
	    cs0flg = False;
	    cs1flg = False;
	    wc_tmp  = (Uint)0;
	}

    }	/* end for */

    *to = (XPointer)outbufptr;

    if (cs0flg == True || cs1flg == True)	/* error check on last char */
	unconv_num++;

    return unconv_num;
}


static int
euc_wcstombs(
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
    wchar_t  wch;
    int length;
    Uchar tmp;
    int unconv_num = 0;

    XLCd lcd = (XLCd)conv->state;
    CodeSet codeset;
    Ulong wc_shift = XLC_GENERIC(lcd, wc_shift_bits);

    for (; *from_left > 0 && *to_left > 0; (*from_left)-- ) {

	wch = *inbufptr++;

	if (!(codeset = wc_codeset(lcd, wch))) {
	    unconv_num++;
	    (*from_left)--;
	    continue;
	}

	length = codeset->length;
	wch ^= (wchar_t)codeset->wc_encoding;

	if (codeset->parse_info) {	/* put out SS2 or SS3 */
	    if (*to_left < length + 1) {
	        unconv_num++;
		break;
	    }
	    *outbufptr++ = *codeset->parse_info->encoding;
	    (*to_left)--;
        } else {
	    if (*to_left < length) {
		unconv_num++;
		break;
	    }
	}

	do {
	    length--;
	    tmp = (wch>>(wchar_t)(length * wc_shift));

	    if (codeset->side == XlcGR)
		tmp = BIT8ON(tmp);

	    *outbufptr++ = (Uchar)tmp;
	    (*to_left)--;
	} while (length);
    }

    *to = (XPointer)outbufptr;

    return unconv_num;
}


static int
euc_mbtocs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    XLCd lcd = (XLCd)conv->state;
    XlcCharSet charset;
    CodeSet *codesets = XLC_GENERIC(lcd, codeset_list);
    int codeset_num = XLC_GENERIC(lcd, codeset_num);
    int length;
    int unconv_num = 0;
    int min_ch = 0;
    const char *src = *from;
    char *dst = *to;

    if (isleftside(*src)) { 			/* 7-bit (CS0) */
	if (ASCII_CODESET >= codeset_num)
	    return -1;
	charset = *CS0->charset_list;
    }
    else if ((Uchar)*src == SS2) {		/* half-kana (CS2) */
	if (KANA_CODESET >= codeset_num)
	    return -1;
	charset = *CS2->charset_list;
	src++;
	(*from_left)--;
    }
    else if ((Uchar)*src == SS3) {		/* user-def */
	if (USERDEF_CODESET >= codeset_num)
	    return -1;
	charset = *CS3->charset_list;
	src++;
	(*from_left)--;
    }
    else  { 					/* Kanji (CS1) */
	if (KANJI_CODESET >= codeset_num)
	    return -1;
	charset = *CS1->charset_list;
    }

    if(*from_left < charset->char_size || *to_left < charset->char_size)
	return -1;

    min_ch = 0x20;
    if (charset->set_size == 94)
	if (charset->char_size > 1 || charset->side == XlcGR)
	    min_ch = 0x21;

    length = charset->char_size;
    do {
	if(BADCHAR(min_ch, *src)) {
	    unconv_num++;
	    src++;
	    break;
	}
	switch (charset->side) {
	    case XlcGL:
		*dst++ = BIT8OFF(*src++);
		break;
	    case XlcGR:
		*dst++ = BIT8ON(*src++);
		break;
	    default:
		*dst++ = *src++;
		break;
	    }
    } while (--length);

    *to = dst;
    *from = (XPointer) src;
    *from_left -= charset->char_size;
    *to_left -= charset->char_size - length;

    if (num_args > 0)
	*((XlcCharSet *) args[0]) = charset;

    return unconv_num;
}


static int
euc_mbstocs(
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

/* Determine the charset of the segment and convert one characater: */

    tmp_args[0] = (XPointer) &charset; /* charset from euc_mbtocs() */
    while
      ((ret = euc_mbtocs(conv, from, from_left, to, to_left, tmp_args, 1)) > 0)
	unconv_num += ret;
    if ( ret < 0 )
	return ret;

    tmp_from = *from;
    tmp_from_left = *from_left;
    tmp_to_left = *to_left;
    tmp_to = *to;

/* Convert remainder of the segment: */

    tmp_args[0] = (XPointer) &tmp_charset;
    while( (ret = euc_mbtocs(conv, (XPointer *) &tmp_from, &tmp_from_left,
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
euc_wcstocs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    XLCd lcd = (XLCd)conv->state;
    const wchar_t *wcptr = (const wchar_t *) *from;
    char *bufptr = (char *) *to;
    wchar_t wch;
    char *tmpptr;
    int length;
    CodeSet codeset;
    Ulong wc_encoding;
    int wcstr_len = *from_left, buf_len = *to_left;

    if (!(codeset = wc_codeset(lcd, *wcptr)))
	return -1;

    wc_encoding = codeset->wc_encoding;

    if (wcstr_len < buf_len / codeset->length)
	buf_len = wcstr_len * codeset->length;

    for ( ; wcstr_len > 0 && buf_len > 0; wcptr++, wcstr_len--) {
	wch = *wcptr;

	if ((wch & XLC_GENERIC(lcd, wc_encode_mask)) != wc_encoding)
	    break;

	length = codeset->length;

	buf_len -= length;
	bufptr += length;
	tmpptr = bufptr - 1;

	while (length--) {
	    *tmpptr-- = codeset->length == 1 && codeset->side == XlcGR ?
	      BIT8ON(wch) : BIT8OFF(wch);
	    wch >>= (wchar_t)XLC_GENERIC(lcd, wc_shift_bits);
	}
    }

    if (num_args > 0)
	*((XlcCharSet *) args[0]) = *codeset->charset_list;

    *from_left -= wcptr - (wchar_t *) *from;
    *from = (XPointer) wcptr;

    *to_left -= bufptr - *to;
    *to = bufptr;

    return 0;
}


static int
euc_cstombs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    XLCd lcd = (XLCd)conv->state;
    const char *csptr = *from;
    char *bufptr = *to;
    int csstr_len = *from_left;
    int buf_len = *to_left;
    int length;
    CodeSet codeset;
    int cvt_length;

    if (num_args < 1)
	return -1;

    if (!(codeset = GetCodeSetFromCharSet(lcd, (XlcCharSet) args[0])))
	return -1;

    cvt_length = 0;
    csstr_len /= codeset->length;
    buf_len /= codeset->length;

    if (codeset->parse_info)
	csstr_len *= 2;

    if (csstr_len < buf_len)
	buf_len = csstr_len;

    cvt_length += buf_len * codeset->length;

    if (bufptr) {
	while (buf_len--) {
	    if (codeset->parse_info)	/* put out SS2 or SS3 */
		*bufptr++ = *codeset->parse_info->encoding;

	    length = codeset->length;
	    while (length--)
		*bufptr++ = codeset->side == XlcGR ?
		  BIT8ON(*csptr++) : BIT8OFF(*csptr++);
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
euc_cstowcs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    XLCd lcd = (XLCd)conv->state;
    const char *csptr = *from;
    wchar_t *bufptr = (wchar_t *) *to;
    wchar_t *toptr = (wchar_t *) *to;
    int csstr_len = *from_left;
    int buf_len = *to_left;
    wchar_t wch;
    int length;
    Ulong wc_shift_bits = XLC_GENERIC(lcd, wc_shift_bits);
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


static CodeSet
wc_codeset(
    XLCd lcd,
    wchar_t wch)
{
    CodeSet *codesets = XLC_GENERIC(lcd, codeset_list);
    int end = XLC_GENERIC(lcd, codeset_num);
    Ulong widech = (Ulong)(wch & XLC_GENERIC(lcd, wc_encode_mask));

    for (; --end >= 0; codesets++)
	if ( widech == (*codesets)->wc_encoding )
	    return *codesets;

    return NULL;
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


/*
 *    Stripped down Direct CT converters for EUC
 *
 */

typedef struct _CTDataRec {
    int side;
    int length;
    char *name;
    Ulong wc_encoding;
    char sshift;
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
  { XlcGL,      1, "ISO8859-1:GL",       0, 0, "\033(B"   ,  3, 0, 0, CT_STD  },
  { XlcGR,      1, "ISO8859-1:GR",       0, 0, "\033-A"   ,  3, 0, 0, CT_STD  },
  { XlcGL,      1, "JISX0201.1976-0:GL", 0, 0, "\033(J"   ,  3, 0, 0, CT_STD  },
  { XlcGR,      1, "JISX0201.1976-0:GR", 0, 0, "\033)I"   ,  3, 0, 0, CT_STD  },
  { XlcGL,      2, "JISX0208.1983-0:GL", 0, 0, "\033$(B"  ,  4, 0, 0, CT_STD  },
  { XlcGR,      2, "JISX0208.1983-0:GR", 0, 0, "\033$)B"  ,  4, 0, 0, CT_STD  },
  { XlcGL,      2, "JISX0212.1990-0:GL", 0, 0, "\033$(D"  ,  4, 0, 0, CT_STD  },
  { XlcGR,      2, "JISX0212.1990-0:GR", 0, 0, "\033$)D"  ,  4, 0, 0, CT_STD  },
  { XlcUnknown, 0, "Ignore-Ext-Status?", 0, 0, "\033#"    ,  2, 0, 0, CT_VER  },
  { XlcUnknown, 0, "NonStd-?-OctetChar", 0, 0, "\033%/0"  ,  4, 0, 0, CT_NSTD },
  { XlcUnknown, 1, "NonStd-1-OctetChar", 0, 0, "\033%/1"  ,  4, 0, 0, CT_NSTD },
  { XlcUnknown, 2, "NonStd-2-OctetChar", 0, 0, "\033%/2"  ,  4, 0, 0, CT_NSTD },
  { XlcUnknown, 3, "NonStd-3-OctetChar", 0, 0, "\033%/3"  ,  4, 0, 0, CT_NSTD },
  { XlcUnknown, 4, "NonStd-4-OctetChar", 0, 0, "\033%/4"  ,  4, 0, 0, CT_NSTD },
  { XlcUnknown, 0, "Extension-2"       , 0, 0, "\033%/"   ,  3, 0, 0, CT_EXT2 },
  { XlcUnknown, 0, "Extension-0"       , 0, 0, "\033"     ,  1, 0, 0, CT_EXT0 },
  { XlcUnknown, 0, "Begin-L-to-R-Text",  0, 0, "\2331]"   ,  3, 0, 0, CT_DIR  },
  { XlcUnknown, 0, "Begin-R-to-L-Text",  0, 0, "\2332]"   ,  3, 0, 0, CT_DIR  },
  { XlcUnknown, 0, "End-Of-String",      0, 0, "\233]"    ,  2, 0, 0, CT_DIR  },
  { XlcUnknown, 0, "Extension-1"       , 0, 0, "\233"     ,  1, 0, 0, CT_EXT1 },
};

/* Note on above table:  euc_ctstombs() and euc_ctstowcs() parser depends on
 * certain table entries occuring in decreasing string length--
 *   1.  CT_EXT2 and CT_EXT0 entries must occur after CT_NSTD entries.
 *   2.  CT_DIR and CT_EXT1 entries must occur after CT_DIR entries.
 */

static CTData ctd_endp = ctdata + ((sizeof(ctdata) / sizeof(CTDataRec))) - 1;
static CTData ctdptr[sizeof(ctdata) / sizeof(CTDataRec)];

#define Ascii   0
#define Kanji   1
#define Kana    2
#define Userdef 3

/*
 * initCTptr(): Set ctdptr[] to point at ctdata[], indexed by codeset_num.
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

    ctdptr[Ascii] = &ctdata[0];		/* failsafe */

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

		    if (codeset->parse_info) {
			ctdptr[codeset->cs_num]->sshift =
			  *codeset->parse_info->encoding;
		    }

		    break;
		}
	}
    }
}


#define SKIP_I(str)     while (*(str) >= 0x20 && *(str) <=  0x2f) (str)++;
#define SKIP_P(str)     while (*(str) >= 0x30 && *(str) <=  0x3f) (str)++;

static int
euc_ctstowcs(
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
    Bool save_outbuf = True;
    /* If outbufptr is NULL, doen't save output, but just counts
       a length to hold the output */
    if (outbufptr == NULL) save_outbuf = False;

    for (length = ctdata[Ascii].length; *from_left > 0; (*from_left) -= length)
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
		    if (ctdp->length) {
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
	    if (ctdp > ctd_endp) 	/* failed to match CT sequence */
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
	    *(from_left) -= ct_seglen;
	    continue;
	case CT_EXT1:
	    inbuf_base = inbufptr;
	    SKIP_P(inbufptr);
	    SKIP_I(inbufptr);
	    inbufptr++;
	    ct_seglen = (unsigned)(inbufptr - inbuf_base);
	    *(from_left) -= ct_seglen;
	    continue;
	case CT_DIR:
	    continue;
	case CT_VER:
	    inbufptr += 2;
	    *(from_left) -= 2;
	    continue;
	}

	wc_encoding = (ctdp == ctdptr[Kana] && isleftside(*inbufptr)) ?
	    ctdptr[Ascii]->wc_encoding: ctdp->wc_encoding;

	shift_mult = length - 1;
	wch = (wchar_t)0;
	clen = length;

	do {
	    wc_tmp = BIT8OFF(*inbufptr++) << (wc_shift_bits * shift_mult);
	    wch |= wc_tmp;
	    shift_mult--;
	} while (--clen);

	if (save_outbuf == True)
	    *outbufptr++ = wch | wc_encoding;
	if (--*to_left == 0 && *from_left != length) {
	    *to = (XPointer)outbufptr;
	    unconv_num = *from_left;
	    return unconv_num;
	}
    }

    *to = (XPointer)outbufptr;

    return unconv_num;

}


#define byte1			(length == codeset->length - 1)
#define byte2			(byte1 == 0)
#define kanji			(codeset->cs_num == 1)
#define kana			(codeset->cs_num == 2)
#define userdef			(codeset->cs_num == 3)

static int
euc_wcstocts(
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
    int unconv_num = 0;
    Uchar tmp;
    Uchar t1 = 0;
    int num_conv;

    StateRec ct_state;
    XLCd lcd = (XLCd)conv->state;
    CTData charset;
    CodeSet codeset;
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

	if (charset->side == XlcGR) {
	    ct_state.GR_charset = charset;
	    ct_state.GL_charset = NULL;
	} else if (charset->side == XlcGL) {
	    ct_state.GL_charset = charset;
	    ct_state.GR_charset = NULL;
	}

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

	    else if (byte1 && (kanji || userdef))
		t1 = tmp;

	    else if (byte2 && (kanji || userdef)) {
		if (BADCHAR(charset->min_ch, (char)t1) ||
		  BADCHAR(charset->min_ch, (char)tmp)) {
		    unconv_num++;
		    break;
		}
		if (charset->side == XlcGR) {
		    *ctptr++ = (char)BIT8ON(t1);
		    *ctptr++ = (char)BIT8ON(tmp);
		} else {
		    *ctptr++ = (char)BIT8OFF(t1);
		    *ctptr++ = (char)BIT8OFF(tmp);
		}
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
#undef kana
#undef kanji
#undef userdef


#define byte1	(ctdp->length == clen)
#define kana    (ctdp == ctdptr[Kana] && isrightside(*inbufptr))
/* #define kanji   (ctdp == ctdptr[Kanji]) */
#define kanji   (strstr(ctdp->name, "JISX0208"))
#define userdef (ctdp == ctdptr[Userdef])

static int
euc_ctstombs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    char *inbufptr = *from;
    XPointer outbufptr = *to;
    const char *inbuf_base;
    XPointer outbuf_base = outbufptr;
    int clen, length;
    int unconv_num = 0;
    unsigned int ct_seglen = 0;
    Uchar ct_type = 0;
    CTData ctdp = &ctdata[0];	/* default */
    CTData GL_ctdp = ctdp;	/* GL ctdp save */
    CTData GR_ctdp = ctdp;	/* GR ctdp save */
    Bool save_outbuf = True;
    /* If outbufptr is NULL, doen't save output, but just counts
       a length to hold the output */
    if (outbufptr == NULL) save_outbuf = False;

    for (length = ctdata[Ascii].length; *from_left > 0; (*from_left) -= length)
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
		    if (ctdp->length) {
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
	    if (ctdp > ctd_endp) 	/* failed to match CT sequence */
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
	    *(from_left) -= ct_seglen;
	    continue;
	case CT_EXT1:
	    inbuf_base = inbufptr;
	    SKIP_P(inbufptr);
	    SKIP_I(inbufptr);
	    inbufptr++;
	    ct_seglen = (unsigned)(inbufptr - inbuf_base);
	    *(from_left) -= ct_seglen;
	    continue;
	case CT_DIR:
	    continue;
	case CT_VER:
	    inbufptr += 2;
	    *(from_left) -= 2;
	    continue;
	}

	clen = length;
	do {

	    if (byte1) {
		if (kanji) {
		    /* FIXME: assignment of read-only location */
		    *inbufptr = BIT8ON(*inbufptr);
		    *(inbufptr+1) = BIT8ON(*(inbufptr+1));
		}
		else if (kana || userdef) {
		    if (save_outbuf == True) {
			*outbufptr++ = ctdp->sshift;
		    }
		    (*to_left)--;
		}
	    }
	    if (save_outbuf == True) {
		*outbufptr++ = *inbufptr;
	    }
	    (*to_left)--;
	    inbufptr++;

	    if (*to_left == 0 && *from_left != length) {
		*to = (XPointer)outbufptr;
		unconv_num = *from_left;
		return unconv_num;
	    }
	} while (--clen);
    }

    *to = outbufptr;

    return unconv_num;

}
#undef byte1
#undef kana
#undef kanji
#undef userdef


static int
euc_mbstocts(
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
    int clen, length = 0;
    int unconv_num = 0;
    int num_conv;
    const char *inbufptr = *from;
    char *ctptr = *to;
    XPointer ct_base = ctptr;

    StateRec ct_state;
    CTData charset;
    XLCd lcd = (XLCd) conv->state;
    int codeset_num = XLC_GENERIC(lcd, codeset_num);

/* Initial State: */
    ct_state.GL_charset = NULL;
    ct_state.GR_charset = NULL;

    if (*from_left > *to_left)
        *from_left = *to_left;

    for (;*from_left > 0; (*from_left) -= length) {

	if (isleftside(*inbufptr)) {		/* 7-bit (CS0) */
	    if (ASCII_CODESET >= codeset_num) {
	      	unconv_num++;
		(*from_left)--;
		continue;
	    }
	    cs_num = Ascii;
	    charset = ctdptr[Ascii];
	}
	else if ((Uchar)*inbufptr == SS2) {	/* Kana */
	    if (KANA_CODESET >= codeset_num) {
	      	unconv_num++;
		(*from_left)--;
		continue;
	    }
	    cs_num = Kana;
	    charset = ctdptr[Kana];
	    inbufptr++;
	    (*from_left)--;
	}
	else if ((Uchar)*inbufptr == SS3) {	/* Userdef */
	    if (USERDEF_CODESET >= codeset_num) {
	      	unconv_num++;
		(*from_left)--;
		continue;
	    }
	    cs_num = Userdef;
	    charset = ctdptr[Userdef];
	    inbufptr++;
	    (*from_left)--;
	}
	else {
	    if (KANJI_CODESET >= codeset_num) {
	      	unconv_num++;
		(*from_left)--;
		continue;
	    }
	    cs_num = Kanji;
	    charset = ctdptr[Kanji];
	}

	length = charset->length;

	if (BADCHAR(charset->min_ch, *inbufptr))
            continue;

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

	if (charset->side == XlcGR) {
	    ct_state.GR_charset = charset;
	    ct_state.GL_charset = NULL;
	} else if (charset->side == XlcGL) {
	    ct_state.GL_charset = charset;
	    ct_state.GR_charset = NULL;
	}

	clen = length;

	do {
	    *ctptr++ = charset == ct_state.GR_charset ?
		BIT8ON(*inbufptr++) : BIT8OFF(*inbufptr++);
	} while (--clen);
    }

    *to = (XPointer)ctptr;

    if ((num_conv = (int)(ctptr - ct_base)) > 0)
	(*to_left) -= num_conv;
    return unconv_num;

}


static void
close_converter(
    XlcConv conv)
{
	Xfree((char *) conv);
}

enum { MBSTOCS, WCSTOCS, MBTOCS, CSTOMBS, CSTOWCS, MBSTOWCS, WCSTOMBS,
       CTSTOWCS, CTSTOMBS, WCSTOCTS, MBSTOCTS };

static XlcConvMethodsRec conv_methods[] = {
    {close_converter, euc_mbstocs,  NULL },
    {close_converter, euc_wcstocs,  NULL },
    {close_converter, euc_mbtocs,   NULL },
    {close_converter, euc_cstombs,  NULL },
    {close_converter, euc_cstowcs,  NULL },
    {close_converter, euc_mbstowcs, NULL },
    {close_converter, euc_wcstombs, NULL },
    {close_converter, euc_ctstowcs, NULL },
    {close_converter, euc_ctstombs, NULL },
    {close_converter, euc_wcstocts, NULL },
    {close_converter, euc_mbstocts, NULL },
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
open_ctstowcs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &conv_methods[CTSTOWCS]);
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

XLCd
_XlcEucLoader(
    const char *name)
{
    XLCd lcd;

    lcd = _XlcCreateLC(name, _XlcGenericMethods);
    if (lcd == NULL)
	return lcd;

    if (!XLC_PUBLIC_PART(lcd)->codeset ||
	(_XlcNCompareISOLatin1(XLC_PUBLIC_PART(lcd)->codeset, "euc", 3))) {
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
