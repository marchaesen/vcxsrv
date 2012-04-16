/*
 *Copyright (C) 2001-2004 Harold L Hunt II All Rights Reserved.
 *
 *Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 *"Software"), to deal in the Software without restriction, including
 *without limitation the rights to use, copy, modify, merge, publish,
 *distribute, sublicense, and/or sell copies of the Software, and to
 *permit persons to whom the Software is furnished to do so, subject to
 *the following conditions:
 *
 *The above copyright notice and this permission notice shall be
 *included in all copies or substantial portions of the Software.
 *
 *THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *NONINFRINGEMENT. IN NO EVENT SHALL HAROLD L HUNT II BE LIABLE FOR
 *ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *Except as contained in this notice, the name of Harold L Hunt II
 *shall not be used in advertising or otherwise to promote the sale, use
 *or other dealings in this Software without prior written authorization
 *from Harold L Hunt II.
 *
 * Authors:	Harold L Hunt II
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"

#ifdef XWIN_NATIVEGDI
/* See Porting Layer Definition - p. 32 */
/* See mfb/mfbfont.c - mfbRealizeFont() - which is empty :) */
Bool
winRealizeFontNativeGDI(ScreenPtr pScreen, FontPtr pFont)
{
    BOOL fResult = TRUE;

    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winRealizeFont (%p, %p)\n", pScreen, pFont);
#endif

    WIN_UNWRAP(RealizeFont);
    if (pScreen->RealizeFont)
        fResult = (*pScreen->RealizeFont) (pScreen, pFont);
    WIN_WRAP(RealizeFont, winRealizeFontNativeGDI);

    return fResult;
}

/* See Porting Layer Definition - p. 32 */
/* See mfb/mfbfont.c - mfbUnrealizeFont() - which is empty :) */
Bool
winUnrealizeFontNativeGDI(ScreenPtr pScreen, FontPtr pFont)
{
    BOOL fResult = TRUE;

    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winUnrealizeFont (%p, %p)\n", pScreen, pFont);
#endif

    WIN_UNWRAP(UnrealizeFont);
    if (pScreen->UnrealizeFont)
        fResult = (*pScreen->UnrealizeFont) (pScreen, pFont);
    WIN_WRAP(UnrealizeFont, winUnrealizeFontNativeGDI);

    return fResult;
#if CYGDEBUG
    winDebug("winUnrealizeFont()\n");
#endif
    return TRUE;
}
#endif
