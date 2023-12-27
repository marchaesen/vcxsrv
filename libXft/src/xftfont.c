/*
 * Copyright © 2000 Keith Packard
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

#include "xftint.h"

_X_EXPORT FcPattern *
XftFontMatch (Display		*dpy,
	      int		screen,
	      _Xconst FcPattern *pattern,
	      FcResult		*result)
{
    FcPattern	*new;
    FcPattern	*match;

    if (!XftInit (NULL))
	return NULL;

    new = FcPatternDuplicate (pattern);
    if (!new)
	return NULL;

    if (XftDebug () & XFT_DBG_OPENV)
    {
	printf ("XftFontMatch pattern ");
	FcPatternPrint (new);
    }
    FcConfigSubstitute (NULL, new, FcMatchPattern);
    if (XftDebug () & XFT_DBG_OPENV)
    {
	printf ("XftFontMatch after FcConfig substitutions ");
	FcPatternPrint (new);
    }
    XftDefaultSubstitute (dpy, screen, new);
    if (XftDebug () & XFT_DBG_OPENV)
    {
	printf ("XftFontMatch after X resource substitutions ");
	FcPatternPrint (new);
    }

    match = FcFontMatch (NULL, new, result);
    if (XftDebug () & XFT_DBG_OPENV)
    {
	printf ("XftFontMatch result ");
	FcPatternPrint (match);
    }
    FcPatternDestroy (new);
    return match;
}

_X_EXPORT XftFont *
XftFontOpen (Display *dpy, int screen, ...)
{
    va_list	    va;
    FcPattern	    *pat;
    FcPattern	    *match;
    FcResult	    result;
    XftFont	    *font;

    va_start (va, screen);
    pat = FcPatternVaBuild (NULL, va);
    va_end (va);
    if (!pat)
    {
	if (XftDebug () & XFT_DBG_OPEN)
	    printf ("XftFontOpen: Invalid pattern argument\n");
	return NULL;
    }
    match = XftFontMatch (dpy, screen, pat, &result);
    if (XftDebug () & XFT_DBG_OPEN)
    {
	printf ("Pattern ");
	FcPatternPrint (pat);
	if (match)
	{
	    printf ("Match ");
	    FcPatternPrint (match);
	}
	else
	    printf ("No Match\n");
    }
    FcPatternDestroy (pat);
    if (!match)
	return NULL;

    font = XftFontOpenPattern (dpy, match);
    if (!font)
    {
	if (XftDebug () & XFT_DBG_OPEN)
	    printf ("No Font\n");
	FcPatternDestroy (match);
    }

    return font;
}

_X_EXPORT XftFont *
XftFontOpenName (Display *dpy, int screen, const char *name)
{
    FcPattern	    *pat;
    FcPattern	    *match;
    FcResult	    result;
    XftFont	    *font;

    pat = FcNameParse ((const FcChar8 *) name);
    if (XftDebug () & XFT_DBG_OPEN)
    {
	printf ("XftFontOpenName \"%s\": ", name);
	if (pat)
	    FcPatternPrint (pat);
	else
	    printf ("Invalid name\n");
    }

    if (!pat)
	return NULL;
    match = XftFontMatch (dpy, screen, pat, &result);
    if (XftDebug () & XFT_DBG_OPEN)
    {
	if (match)
	{
	    printf ("Match ");
	    FcPatternPrint (match);
	}
	else
	    printf ("No Match\n");
    }
    FcPatternDestroy (pat);
    if (!match)
	return NULL;

    font = XftFontOpenPattern (dpy, match);
    if (!font)
    {
	if (XftDebug () & XFT_DBG_OPEN)
	    printf ("No Font\n");
	FcPatternDestroy (match);
    }

    return font;
}

_X_EXPORT XftFont *
XftFontOpenXlfd (Display *dpy, int screen, const char *xlfd)
{
    FcPattern	    *pat;
    FcPattern	    *match;
    FcResult	    result;
    XftFont	    *font;

    pat = XftXlfdParse (xlfd, FcFalse, FcFalse);
    if (XftDebug () & XFT_DBG_OPEN)
    {
	printf ("XftFontOpenXlfd \"%s\": ", xlfd);
	if (pat)
	    printf ("Invalid xlfd\n");
	else
	    FcPatternPrint (pat);
    }

    if (!pat)
	return NULL;
    match = XftFontMatch (dpy, screen, pat, &result);
    if (XftDebug () & XFT_DBG_OPEN)
    {
	if (match)
	{
	    printf ("Match ");
	    FcPatternPrint (match);
	}
	else
	    printf ("No Match\n");
    }
    FcPatternDestroy (pat);
    if (!match)
	return NULL;

    font = XftFontOpenPattern (dpy, match);
    if (!font)
    {
	if (XftDebug () & XFT_DBG_OPEN)
	    printf ("No Font\n");
	FcPatternDestroy (match);
    }

    return font;
}

