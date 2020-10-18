/*
 * Copyright © 2000 Keith Packard
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

FcBool
FcPatternAddFullname (FcPattern *pat)
{
    FcBool b = FcFalse;

    if (FcRefIsConst (&pat->ref))
	return FcFalse;
    if (FcPatternObjectGetBool (pat, FC_VARIABLE_OBJECT, 0, &b) != FcResultMatch || b == FcFalse)
    {
	FcChar8 *family, *style, *lang = NULL;
	int n = 0;
	size_t len, i;
	FcStrBuf sbuf;

	while (FcPatternObjectGetString (pat, FC_FAMILYLANG_OBJECT, n, &lang) == FcResultMatch)
	{
	    if (FcStrCmp (lang, (const FcChar8 *) "en") == 0)
		break;
	    n++;
	    lang = NULL;
	}
	if (!lang)
	    n = 0;
	if (FcPatternObjectGetString (pat, FC_FAMILY_OBJECT, n, &family) != FcResultMatch)
	    return FcFalse;
	len = strlen ((const char *) family);
	for (i = len; i > 0; i--)
	{
	    if (!isspace (family[i]))
		break;
	}
	family[i] = 0;
	lang = NULL;
	while (FcPatternObjectGetString (pat, FC_STYLELANG_OBJECT, n, &lang) == FcResultMatch)
	{
	    if (FcStrCmp (lang, (const FcChar8 *) "en") == 0)
		break;
	    n++;
	    lang = NULL;
	}
	if (!lang)
	    n = 0;
	if (FcPatternObjectGetString (pat, FC_STYLE_OBJECT, n, &style) != FcResultMatch)
	    return FcFalse;
	len = strlen ((const char *) style);
	for (i = 0; style[i] != 0 && isspace (style[i]); i++)
	    break;
	memcpy (style, &style[i], len - i);
	FcStrBufInit (&sbuf, NULL, 0);
	FcStrBufString (&sbuf, family);
	if (FcStrCmpIgnoreBlanksAndCase(style, (const FcChar8 *) "Regular") != 0)
	{
	    FcStrBufChar (&sbuf, ' ');
	    FcStrBufString (&sbuf, style);
	}
	if (!FcPatternObjectAddString (pat, FC_FULLNAME_OBJECT, FcStrBufDoneStatic (&sbuf)))
	{
	    FcStrBufDestroy (&sbuf);
	    return FcFalse;
	}
	FcStrBufDestroy (&sbuf);
	if (!FcPatternObjectAddString (pat, FC_FULLNAMELANG_OBJECT, (const FcChar8 *) "en"))
	    return FcFalse;
    }

    return FcTrue;
}
