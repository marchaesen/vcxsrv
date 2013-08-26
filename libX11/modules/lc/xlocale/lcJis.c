/*
 * Copyright 1992, 1993 by TOSHIBA Corp.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of TOSHIBA not be used in advertising
 * or publicity pertaining to distribution of the software without specific,
 * written prior permission. TOSHIBA make no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * TOSHIBA DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
 * ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
 * TOSHIBA BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
 * ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 *
 * Author: Katsuhisa Yano	TOSHIBA Corp.
 *			   	mopi@osa.ilab.toshiba.co.jp
 */

/*
 * A Japanese JIS locale.
 * Supports: all locales with codeset JIS7.
 * How: Provides converters for JIS.
 * Platforms: Only those defining X_LOCALE (only Lynx, Linux-libc5, OS/2).
 */

#ifdef X_LOCALE

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "Xlibint.h"
#include "XlcGeneric.h"
#include <stdio.h>

#if !defined(X_LOCALE)
#define STDCVT
#endif

typedef struct _StateRec {
    XLCd lcd;
    XlcCharSet charset;
    XlcCharSet GL_charset;
    XlcCharSet GR_charset;
} StateRec, *State;

static void
init_state(
    XlcConv conv)
{
    State state = (State) conv->state;
    XLCdGenericPart *gen = XLC_GENERIC_PART(state->lcd);
    CodeSet codeset;

    codeset = gen->initial_state_GL;
    if (codeset && codeset->charset_list)
	state->GL_charset = *codeset->charset_list;
    codeset = gen->initial_state_GR;
    if (codeset && codeset->charset_list)
	state->GR_charset = *codeset->charset_list;

    if (state->GL_charset == NULL)
	if ((codeset = *gen->codeset_list) != NULL)
	    state->GL_charset = *codeset->charset_list;
}

static int
compare(
    const char *src,
    const char *encoding,
    int length)
{
    const char *start = src;

    while (length-- > 0) {
	if (*src++ != *encoding++)
	    return 0;
	if (*encoding == '\0')
	    return src - start;
    }

    return 0;
}

static int
mbtocs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    State state = (State) conv->state;
    XLCd lcd = state->lcd;
    const char *src;
    char *dst;
    unsigned char *mb_parse_table;
    ParseInfo *parse_list, parse_info;
    XlcCharSet charset;
    int length, number, encoding_len = 0;
    int i;

    src = *((const char **) from);
    dst = *((char **) to);

    mb_parse_table = XLC_GENERIC(lcd, mb_parse_table);
    if (mb_parse_table != NULL) {
	number = mb_parse_table[(unsigned char) *src];
	if (number > 0) {
	    parse_list = XLC_GENERIC(lcd, mb_parse_list) + number - 1;
	    for ( ; (parse_info = *parse_list) != NULL; parse_list++) {
		encoding_len = compare(src, parse_info->encoding, *from_left);
		if (encoding_len > 0) {
		    switch (parse_info->type) {
			case E_SS:
			    src += encoding_len;
			    charset = *parse_info->codeset->charset_list;
			    goto found;
			case E_LSL:
			case E_LSR:
			    src += encoding_len;
			    charset = *parse_info->codeset->charset_list;
			    if (parse_info->type == E_LSL)
			    	state->GL_charset = charset;
			    else
			    	state->GR_charset = charset;
			    length = 0;
			    goto end;
			case E_GL:
			    charset = state->GL_charset;
			    goto found;
			case E_GR:
			    charset = state->GR_charset;
			    goto found;
			default:
			    break;
		    }
		}
	    }
	}
    }

    if ((*src & 0x80) && state->GR_charset)
	charset = state->GR_charset;
    else
	charset = state->GL_charset;

found:
    if (charset == NULL ||
	(num_args == 2 && (XlcCharSet) args[1] != charset))
	return -1;

    length = charset->char_size;
    if (length > *from_left - encoding_len)
	return -1;

    if (dst) {
	if (length > *to_left)
	    return -1;
	if (charset->side == XlcGL) {
	    for (i = 0; i < length; i++)
		*dst++ = *src++ & 0x7f;
	} else if (charset->side == XlcGR) {
	    for (i = 0; i < length; i++)
		*dst++ = *src++ | 0x80;
	} else {
	    for (i = 0; i < length; i++)
		*dst++ = *src++;
	}
	*to = (XPointer) dst;
	*to_left -= length;
    }
end:
    *from = (XPointer) src;
    *from_left -= encoding_len + length;
    state->charset = charset;
    if (num_args == 1)
	*((XlcCharSet *) args[0]) = charset;

    return 0;
}

static int
mbstocs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    XlcCharSet charset = NULL;
    XPointer tmp_args[2], save_from = *from;
    int ret, unconv_num = 0, tmp_num = 1;

    tmp_args[0] = (XPointer) &charset;

    while (*from_left > 0 && *to_left > 0) {
	ret = mbtocs(conv, from, from_left, to, to_left, tmp_args, tmp_num);
	if (ret < 0)
	    break;
	unconv_num += ret;
	if (tmp_num == 1 && charset) {
	    tmp_args[1] = (XPointer) charset;
	    tmp_num = 2;
	}
    }

    if (save_from == *from)
	return -1;

    if (num_args > 0)
	*((XlcCharSet *) args[0]) = charset;

    return unconv_num;
}

static CodeSet
wc_parse_codeset(
    XLCd lcd,
    const wchar_t *wcstr)
{
    CodeSet *codeset;
    unsigned long wc_encoding;
    int num;

    wc_encoding = *wcstr & XLC_GENERIC(lcd, wc_encode_mask);
    num = XLC_GENERIC(lcd, codeset_num);
    codeset = XLC_GENERIC(lcd, codeset_list);
    while (num-- > 0) {
	if (wc_encoding == (*codeset)->wc_encoding)
	    return *codeset;
	codeset++;
    }

    return NULL;
}

static int
wcstocs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    State state = (State) conv->state;
    XLCd lcd = state->lcd;
    const wchar_t *wcptr;
    char *bufptr;
    wchar_t wch;
    char *tmpptr;
    int length;
    CodeSet codeset;
    unsigned long wc_encoding;
    int wcstr_len, buf_len;

    if (from == NULL || *from == NULL)
	return 0;

    wcptr = *((const wchar_t **) from);
    bufptr = *((char **) to);
    wcstr_len = *from_left;
    buf_len = *to_left;

    codeset = wc_parse_codeset(lcd, wcptr);
    if (codeset == NULL)
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
	if ((*codeset->charset_list)->side == XlcGL) {
	    while (length--) {
		*tmpptr-- = (unsigned char) (wch & 0x7f);
		wch >>= (wchar_t)XLC_GENERIC(lcd, wc_shift_bits);
	    }
	} else if ((*codeset->charset_list)->side == XlcGR) {
	    while (length--) {
		*tmpptr-- = (unsigned char) (wch | 0x80);
		wch >>= (wchar_t)XLC_GENERIC(lcd, wc_shift_bits);
	    }
	} else {
	    while (length--) {
		*tmpptr-- = (unsigned char) wch;
		wch >>= (wchar_t)XLC_GENERIC(lcd, wc_shift_bits);
	    }
	}
    }

    if (num_args > 0)
	*((XlcCharSet *) args[0]) = *codeset->charset_list;

    *from_left -= wcptr - *((wchar_t **) from);
    *from = (XPointer) wcptr;

    *to_left -= bufptr - *((char **) to);
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
cstombs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    State state = (State) conv->state;
    const char *csptr;
    char *bufptr;
    int csstr_len;
    int buf_len;
    int num, encoding_len = 0;
    CodeSet codeset;
    XlcCharSet charset;
    EncodingType type;
    int cvt_length;

    csptr = *((const char **) from);
    bufptr = *((char **) to);
    csstr_len = *from_left;
    buf_len = *to_left;

    if (num_args < 1)
	return -1;

    charset = (XlcCharSet) args[0];

    codeset = GetCodeSetFromCharSet(state->lcd, charset);
    if (codeset == NULL)
	return -1;

    cvt_length = 0;
    if (codeset->parse_info) {
	switch (type = codeset->parse_info->type) {
	    case E_SS:
		encoding_len = strlen(codeset->parse_info->encoding);
		break;
	    case E_LSL:
	    case E_LSR:
		if (type == E_LSL) {
		    if (charset == state->GL_charset)
			break;
		} else {
		    if (charset == state->GR_charset)
			break;
		}
		encoding_len = strlen(codeset->parse_info->encoding);
		if (encoding_len > buf_len)
		    return -1;
		cvt_length += encoding_len;
		if (bufptr) {
		    strcpy(bufptr, codeset->parse_info->encoding);
		    bufptr += encoding_len;
		}
		buf_len -= encoding_len;
		encoding_len = 0;
		if (type == E_LSL)
		    state->GL_charset = charset;
		else
		    state->GR_charset = charset;
		break;
	    default:
		break;
	}
    }

    csstr_len /= codeset->length;
    buf_len /= codeset->length + encoding_len;
    if (csstr_len < buf_len)
	buf_len = csstr_len;

    cvt_length += buf_len * (encoding_len + codeset->length);
    if (bufptr) {
	while (buf_len--) {
	    if (encoding_len) {
		strcpy(bufptr, codeset->parse_info->encoding);
		bufptr += encoding_len;
	    }
	    num = codeset->length;
	    if (codeset->side == XlcGL) {
		while (num--)
		    *bufptr++ = *csptr++ & 0x7f;
	    } else if (codeset->side == XlcGR) {
		while (num--)
		    *bufptr++ = *csptr++ | 0x80;
	    } else {
		while (num--)
		    *bufptr++ = *csptr++;
	    }
    	}
    }

    *from_left -= csptr - *((char **) from);
    *from = (XPointer) csptr;

    if (bufptr)
	*to = (XPointer) bufptr;
    *to_left -= cvt_length;

    return 0;
}

static int
cstowcs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    State state = (State) conv->state;
    XLCd lcd = state->lcd;
    const char *csptr;
    wchar_t *bufptr;
    int csstr_len;
    int buf_len;
    wchar_t wch;
    unsigned long code_mask, wc_encoding;
    int num, length, wc_shift_bits;
    CodeSet codeset;

    csptr = *((const char **) from);
    bufptr = *((wchar_t **) to);
    csstr_len = *from_left;
    buf_len = *to_left;

    if (num_args < 1)
	return -1;

    codeset = GetCodeSetFromCharSet(lcd, (XlcCharSet) args[0]);
    if (codeset == NULL)
	return -1;

    length = codeset->length;
    csstr_len /= length;
    if (csstr_len < buf_len)
	buf_len = csstr_len;

    code_mask = ~XLC_GENERIC(lcd, wc_encode_mask);
    wc_encoding = codeset->wc_encoding;
    wc_shift_bits = XLC_GENERIC(lcd, wc_shift_bits);

    *to_left -= buf_len;

    if (bufptr) {
	while (buf_len--) {
	    wch = (wchar_t) (*csptr++ & 0x7f);
	    num = length - 1;
	    while (num--)
		wch = (wch << wc_shift_bits) | (*csptr++ & 0x7f);

	    *bufptr++ = (wch & code_mask) | wc_encoding;
	}
    }

    *from_left -= csptr - *((char **) from);
    *from = (XPointer) csptr;

    if (bufptr)
	*to = (XPointer) bufptr;

    return 0;
}


static void
close_converter(
    XlcConv conv)
{
    if (conv->state) {
	Xfree(conv->state);
    }

    Xfree(conv);
}

static XlcConv
create_conv(
    XLCd lcd,
    XlcConvMethods methods)
{
    XlcConv conv;
    State state;

    conv = Xmalloc(sizeof(XlcConvRec));
    if (conv == NULL)
	return (XlcConv) NULL;

    conv->methods = Xmalloc(sizeof(XlcConvMethodsRec));
    if (conv->methods == NULL)
	goto err;
    *conv->methods = *methods;
    if (XLC_PUBLIC(lcd, is_state_depend))
	conv->methods->reset = init_state;

    conv->state = Xcalloc(1, sizeof(StateRec));
    if (conv->state == NULL)
	goto err;

    state = (State) conv->state;
    state->lcd = lcd;
    init_state(conv);

    return conv;

err:
    close_converter(conv);

    return (XlcConv) NULL;
}

static XlcConvMethodsRec mbstocs_methods = {
    close_converter,
    mbstocs,
    NULL
};

static XlcConv
open_mbstocs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &mbstocs_methods);
}

static XlcConvMethodsRec wcstocs_methods = {
    close_converter,
    wcstocs,
    NULL
};

static XlcConv
open_wcstocs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &wcstocs_methods);
}

static XlcConvMethodsRec mbtocs_methods = {
    close_converter,
    mbtocs,
    NULL
};

static XlcConv
open_mbtocs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &mbtocs_methods);
}

static XlcConvMethodsRec cstombs_methods = {
    close_converter,
    cstombs,
    NULL
};

static XlcConv
open_cstombs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &cstombs_methods);
}

static XlcConvMethodsRec cstowcs_methods = {
    close_converter,
    cstowcs,
    NULL
};

static XlcConv
open_cstowcs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &cstowcs_methods);
}

#ifdef STDCVT
static int
stdc_mbstowcs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    const char *src = *((const char **) from);
    wchar_t *dst = *((wchar_t **) to);
    int src_left = *from_left;
    int dst_left = *to_left;
    int length;

    while (src_left > 0 && dst_left > 0) {
	length = mbtowc(dst, src, src_left);
	if (length < 0)
	    break;

	src += length;
	src_left -= length;
	if (dst)
	    dst++;
	dst_left--;

	if (length == 0) {
	    src++;
	    src_left--;
	    break;
	}
    }

    if (*from_left == src_left)
	return -1;

    *from = (XPointer) src;
    if (dst)
	*to = (XPointer) dst;
    *from_left = src_left;
    *to_left = dst_left;

    return 0;
}

static int
stdc_wcstombs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    const wchar_t *src = *((const wchar_t **) from);
    char *dst = *((char **) to);
    int src_left = *from_left;
    int dst_left = *to_left;
    int length;

    while (src_left > 0 && dst_left > 0) {
	length = wctomb(dst, *src);		/* XXX */
	if (length < 0 || dst_left < length)
	    break;

	src++;
	src_left--;
	dst += length;
	dst_left -= length;

	if (length == 0) {
	    dst++;
	    dst_left--;
	    break;
	}
    }

    if (*from_left == src_left)
	return -1;

    *from = (XPointer) src;
    *to = (XPointer) dst;
    *from_left = src_left;
    *to_left = dst_left;

    return 0;
}

static int
stdc_wcstocs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    const wchar_t *src = *((const wchar_t **) from);
    wchar_t wch;
    XlcCharSet charset = NULL;
    XPointer tmp_args[2], tmp_from, save_from = *from;
    char tmp[32];
    int length, ret, src_left = *from_left;
    int unconv_num = 0, tmp_num = 1;

    tmp_args[0] = (XPointer) &charset;

    while (src_left > 0 && *to_left > 0) {
	if (wch = *src) {
	    length = wctomb(tmp, wch);
	} else {
	    length = 1;
	    *tmp = '\0';
	}

	if (length < 0)
	    break;

	tmp_from = (XPointer) tmp;
	ret = mbtocs(conv, &tmp_from, &length, to, to_left, tmp_args, tmp_num);
	if (ret < 0)
	    break;
	unconv_num += ret;
	if (tmp_num == 1 && charset) {
	    tmp_args[1] = (XPointer) charset;
	    tmp_num = 2;
	}

	src++;
	src_left--;
    }

    if (save_from == (XPointer) src)
	return -1;

    *from = (XPointer) src;
    *from_left = src_left;

    if (num_args > 0)
	*((XlcCharSet *) args[0]) = charset;

    return unconv_num;
}

#define DefineLocalBuf		char local_buf[BUFSIZ]
#define AllocLocalBuf(length)	(length > BUFSIZ ? (char*) Xmalloc(length) : local_buf)
#define FreeLocalBuf(ptr)	if (ptr != local_buf) Xfree(ptr)

static int
stdc_cstowcs(
    XlcConv conv,
    XPointer *from,
    int *from_left,
    XPointer *to,
    int *to_left,
    XPointer *args,
    int num_args)
{
    XLCd lcd = ((State) conv->state)->lcd;
    DefineLocalBuf;
    XPointer buf, save_buf;
    int length, left, ret;

    left = length = *to_left * XLC_PUBLIC(lcd, mb_cur_max);
    buf = save_buf = (XPointer) AllocLocalBuf(length);
    if (buf == NULL)
	return -1;

    ret = cstombs(conv, from, from_left, &buf, &left, args, num_args);
    if (ret < 0)
	goto err;

    buf = save_buf;
    length -= left;
    if (stdc_mbstowcs(conv, &buf, &length, to, to_left, args, num_args) < 0)
	ret = -1;

err:
    FreeLocalBuf(save_buf);

    return ret;
}

static XlcConvMethodsRec stdc_mbstowcs_methods = {
    close_converter,
    stdc_mbstowcs,
    NULL
};

static XlcConv
open_stdc_mbstowcs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &stdc_mbstowcs_methods);
}

static XlcConvMethodsRec stdc_wcstombs_methods = {
    close_converter,
    stdc_wcstombs,
    NULL
};

static XlcConv
open_stdc_wcstombs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &stdc_wcstombs_methods);
}

static XlcConvMethodsRec stdc_wcstocs_methods = {
    close_converter,
    stdc_wcstocs,
    NULL
};

static XlcConv
open_stdc_wcstocs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &stdc_wcstocs_methods);
}

static XlcConvMethodsRec stdc_cstowcs_methods = {
    close_converter,
    stdc_cstowcs,
    NULL
};

static XlcConv
open_stdc_cstowcs(
    XLCd from_lcd,
    const char *from_type,
    XLCd to_lcd,
    const char *to_type)
{
    return create_conv(from_lcd, &stdc_cstowcs_methods);
}
#endif /* STDCVT */

XLCd
_XlcJisLoader(
    const char *name)
{
    XLCd lcd;
#ifdef STDCVT
    XLCdGenericPart *gen;
#endif

    lcd = _XlcCreateLC(name, _XlcGenericMethods);
    if (lcd == NULL)
	return lcd;

    if (!XLC_PUBLIC_PART(lcd)->codeset ||
	(_XlcCompareISOLatin1(XLC_PUBLIC_PART(lcd)->codeset, "JIS7"))) {
        _XlcDestroyLC(lcd);
        return (XLCd) NULL;
    }

    _XlcSetConverter(lcd, XlcNMultiByte, lcd, XlcNChar, open_mbtocs);
    _XlcSetConverter(lcd, XlcNMultiByte, lcd, XlcNCharSet, open_mbstocs);
    _XlcSetConverter(lcd, XlcNCharSet, lcd, XlcNMultiByte, open_cstombs);

#ifdef STDCVT
    gen = XLC_GENERIC_PART(lcd);

    if (gen->use_stdc_env == True) {
	_XlcSetConverter(lcd,XlcNMultiByte,lcd,XlcNWideChar,open_stdc_mbstowcs);
	_XlcSetConverter(lcd,XlcNWideChar,lcd,XlcNMultiByte,open_stdc_wcstombs);
    }
    if (gen->force_convert_to_mb == True) {
	_XlcSetConverter(lcd, XlcNWideChar, lcd, XlcNCharSet,open_stdc_wcstocs);
	_XlcSetConverter(lcd, XlcNCharSet, lcd, XlcNWideChar,open_stdc_cstowcs);
    } else {
#endif
    _XlcSetConverter(lcd, XlcNWideChar, lcd, XlcNCharSet, open_wcstocs);
    _XlcSetConverter(lcd, XlcNCharSet, lcd, XlcNWideChar, open_cstowcs);
#ifdef STDCVT
    }
#endif

    _XlcAddUtf8Converters(lcd);

    return lcd;
}

#else
typedef int dummy;
#endif /* X_LOCALE */
