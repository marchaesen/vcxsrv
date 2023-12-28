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

_X_HIDDEN FcFontSet *
XftListFontsPatternObjects (Display	    *dpy _X_UNUSED,
			    int		    screen _X_UNUSED,
			    FcPattern	    *pattern,
			    FcObjectSet	    *os)
{
    return FcFontList (NULL, pattern, os);
}

_X_EXPORT FcFontSet *
XftListFonts (Display	*dpy,
	      int	screen,
	      ...)
{
    va_list	    va;
    FcFontSet	    *fs;
    FcObjectSet	    *os;
    FcPattern	    *pattern;
    const char	    *first;

    va_start (va, screen);

    FcPatternVapBuild (pattern, NULL, va);

    first = va_arg (va, const char *);
    FcObjectSetVapBuild (os, first, va);

    va_end (va);

    fs = XftListFontsPatternObjects (dpy, screen, pattern, os);
    FcPatternDestroy (pattern);
    FcObjectSetDestroy (os);
    return fs;
}
