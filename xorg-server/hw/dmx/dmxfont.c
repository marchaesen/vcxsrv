/*
 * Copyright 2001-2004 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   Kevin E. Martin <kem@redhat.com>
 *
 */

/** \file
 * This file provides support for fonts. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#define DMX_FONTPATH_DEBUG 0

#include "dmx.h"
#include "dmxsync.h"
#include "dmxfont.h"
#include "dmxlog.h"

#include <X11/fonts/fontstruct.h>
#include "dixfont.h"
#include "dixstruct.h"

static int (*dmxSaveProcVector[256]) (ClientPtr);
static int dmxFontLastError;

static int
dmxFontErrorHandler(Display * dpy, XErrorEvent * ev)
{
    dmxFontLastError = ev->error_code;

    return 0;
}

static char **
dmxGetFontPath(int *npaths)
{
    char **fp;
    unsigned char *c, *paths;
    char *newfp;
    int len, l, i;

    GetFontPath(serverClient, npaths, &len, &paths);

    newfp = malloc(*npaths + len);
    c = (unsigned char *) newfp;
    fp = xallocarray(*npaths, sizeof(*fp));

    memmove(newfp, paths + 1, *npaths + len - 1);
    l = *paths;
    for (i = 0; i < *npaths; i++) {
        fp[i] = (char *) c;
        c += l;
        l = *c;
        *c++ = '\0';
    }

#if DMX_FONTPATH_DEBUG
    for (i = 0; i < *npaths; i++)
        dmxLog(dmxDebug, "FontPath[%d] = %s\n", i, fp[i]);
#endif

    return fp;
}

static void
dmxFreeFontPath(char **fp)
{
    free(fp[0]);
    free(fp);
}

static Bool
dmxCheckFontPathElement(DMXScreenInfo * dmxScreen, char *fp)
{
    int (*oldErrorHandler) (Display *, XErrorEvent *);

    if (!dmxScreen->beDisplay)
        return TRUE;

    dmxFontLastError = 0;
    oldErrorHandler = XSetErrorHandler(dmxFontErrorHandler);
    XSetFontPath(dmxScreen->beDisplay, &fp, 1);
    dmxSync(dmxScreen, TRUE);   /* Must complete before removing handler */
    XSetErrorHandler(oldErrorHandler);

    return dmxFontLastError == 0;
}

static int
dmxSetFontPath(DMXScreenInfo * dmxScreen)
{
    int (*oldErrorHandler) (Display *, XErrorEvent *);
    char **fp;
    int result = Success;
    int npaths;

    if (!dmxScreen->beDisplay)
        return result;

    fp = dmxGetFontPath(&npaths);
    if (!fp)
        return BadAlloc;

    dmxFontLastError = 0;
    oldErrorHandler = XSetErrorHandler(dmxFontErrorHandler);
    XSetFontPath(dmxScreen->beDisplay, fp, npaths);
    dmxSync(dmxScreen, TRUE);   /* Must complete before removing handler */
    XSetErrorHandler(oldErrorHandler);

    if (dmxFontLastError) {
        result = dmxFontLastError;
        /* We could set *error here to the offending path, but it is
         * ignored, so we don't bother figuring out which path is bad.
         * If we do add this support in the future, we'll need to add
         * error to the function's argument list.
         */
    }

    dmxFreeFontPath(fp);

    return result;
}

static int
dmxCheckFontPath(DMXScreenInfo * dmxScreen, int *error)
{
    char **oldFontPath;
    int nOldPaths;
    int result = Success;

    if (!dmxScreen->beDisplay)
        return result;

    /* Save old font path */
    oldFontPath = XGetFontPath(dmxScreen->beDisplay, &nOldPaths);

    result = dmxSetFontPath(dmxScreen);

    /* Restore old font path */
    XSetFontPath(dmxScreen->beDisplay, oldFontPath, nOldPaths);
    XFreeFontPath(oldFontPath);
    dmxSync(dmxScreen, FALSE);

    return result;
}

static int
dmxProcSetFontPath(ClientPtr client)
{
    unsigned char *ptr;
    unsigned long nbytes, total, n;
    long nfonts;
    int i, result;
    unsigned char *oldFontPath, *tmpFontPath;
    int nOldPaths;
    int lenOldPaths;

    REQUEST(xSetFontPathReq);

    REQUEST_AT_LEAST_SIZE(xSetFontPathReq);

    nbytes = (client->req_len << 2) - sizeof(xSetFontPathReq);
    total = nbytes;
    ptr = (unsigned char *) &stuff[1];
    nfonts = stuff->nFonts;

    while (--nfonts >= 0) {
        if ((total == 0) || (total < (n = (*ptr + 1))))
            return BadLength;
        total -= n;
        ptr += n;
    }
    if (total >= 4)
        return BadLength;

    GetFontPath(serverClient, &nOldPaths, &lenOldPaths, &tmpFontPath);
    oldFontPath = malloc(nOldPaths + lenOldPaths);
    memmove(oldFontPath, tmpFontPath, nOldPaths + lenOldPaths);

    result = SetFontPath(client, stuff->nFonts, (unsigned char *) &stuff[1]);
    if (!result) {
        int error = 0;

        for (i = 0; i < dmxNumScreens; i++)
            if ((result = dmxCheckFontPath(&dmxScreens[i], &error)))
                break;

        if (result) {
            /* Restore old fontpath in the DMX server */
            SetFontPath(client, nOldPaths, oldFontPath);
            client->errorValue = error;
        }
    }

    free(oldFontPath);
    return result;
}

/** Initialize font support.  In addition to the screen function call
 *  pointers, DMX also hooks in at the ProcVector[] level.  Here the old
 *  ProcVector function pointers are saved and the new ProcVector
 *  function pointers are initialized. */
void
dmxInitFonts(void)
{
    int i;

    for (i = 0; i < 256; i++)
        dmxSaveProcVector[i] = ProcVector[i];

    ProcVector[X_SetFontPath] = dmxProcSetFontPath;
}

/** Reset font support by restoring the original ProcVector function
 *  pointers. */
void
dmxResetFonts(void)
{
    int i;

    for (i = 0; i < 256; i++)
        ProcVector[i] = dmxSaveProcVector[i];
}

/** Load the font, \a pFont, on the back-end server associated with \a
 *  pScreen.  When a font is loaded, the font path on back-end server is
 *  first initialized to that specified on the command line with the
 *  -fontpath options, and then the font is loaded. */
Bool
dmxBELoadFont(ScreenPtr pScreen, FontPtr pFont)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxFontPrivPtr pFontPriv = FontGetPrivate(pFont, dmxFontPrivateIndex);
    const char *name;
    char **oldFontPath = NULL;
    int nOldPaths;
    Atom name_atom, value_atom;
    int i;

    /* Make sure we have a font private struct to work with */
    if (!pFontPriv)
        return FALSE;

    /* Don't load a font over top of itself */
    if (pFontPriv->font[pScreen->myNum]) {
        return TRUE;            /* Already loaded font */
    }

    /* Save old font path */
    oldFontPath = XGetFontPath(dmxScreen->beDisplay, &nOldPaths);

    /* Set the font path for the font about to be loaded on the back-end */
    if (dmxSetFontPath(dmxScreen)) {
        char **fp;
        int npaths;
        Bool *goodfps;

        /* This could fail only when first starting the X server and
         * loading the default font.  If it fails here, then the default
         * font path is invalid, no default font path will be set, the
         * DMX server will fail to load the default font, and it will
         * exit with an error unless we remove the offending font paths
         * with the -ignorebadfontpaths command line option.
         */

        fp = dmxGetFontPath(&npaths);
        if (!fp) {
            dmxLog(dmxError, "No default font path set.\n");
            dmxLog(dmxError,
                   "Please see the Xdmx man page for information on how to\n");
            dmxLog(dmxError,
                   "initialize the DMX server's default font path.\n");
            XFreeFontPath(oldFontPath);
            return FALSE;
        }

        if (!dmxFontPath)
            dmxLog(dmxWarning, "No default font path is set.\n");

        goodfps = xallocarray(npaths, sizeof(*goodfps));

        dmxLog(dmxError,
               "The DMX server failed to set the following font paths on "
               "screen #%d:\n", pScreen->myNum);

        for (i = 0; i < npaths; i++)
            if (!(goodfps[i] = dmxCheckFontPathElement(dmxScreen, fp[i])))
                dmxLog(dmxError, "    %s\n", fp[i]);

        if (dmxIgnoreBadFontPaths) {
            char *newfp;
            int newnpaths = 0;
            int len = 0;
            int j = 0;

            dmxLog(dmxError,
                   "These font paths will not be used because the "
                   "\"-ignorebadfontpaths\"\n");
            dmxLog(dmxError, "option is set.\n");

            for (i = 0; i < npaths; i++)
                if (goodfps[i]) {
                    len += strlen(fp[i]) + 1;
                    newnpaths++;
                }

            if (!newnpaths) {
                /* No valid font paths were found */
                dmxLog(dmxError,
                       "After removing the font paths above, no valid font "
                       "paths were\n");
                dmxLog(dmxError,
                       "available.  Please check that the font paths set on "
                       "the command\n");
                dmxLog(dmxError,
                       "line or in the configuration file via the "
                       "\"-fontpath\" option\n");
                dmxLog(dmxError,
                       "are valid on all back-end servers.  See the Xdmx man "
                       "page for\n");
                dmxLog(dmxError, "more information on font paths.\n");
                dmxFreeFontPath(fp);
                XFreeFontPath(oldFontPath);
                free(goodfps);
                return FALSE;
            }

            newfp = xallocarray(len, sizeof(*newfp));
            for (i = 0; i < npaths; i++) {
                if (goodfps[i]) {
                    int n = strlen(fp[i]);

                    newfp[j++] = n;
                    strncpy(&newfp[j], fp[i], n);
                    j += n;
                }
            }

            if (SetFontPath(serverClient, newnpaths, (unsigned char *) newfp)) {
                /* Note that this should never happen since all of the
                 * FPEs were previously valid. */
                dmxLog(dmxError, "Cannot reset the default font path.\n");
            }
        }
        else if (dmxFontPath) {
            dmxLog(dmxError,
                   "Please remove these font paths from the command line "
                   "or\n");
            dmxLog(dmxError,
                   "configuration file, or set the \"-ignorebadfontpaths\" "
                   "option to\n");
            dmxLog(dmxError,
                   "ignore them.  For more information on these options, see "
                   "the\n");
            dmxLog(dmxError, "Xdmx man page.\n");
        }
        else {
            dmxLog(dmxError,
                   "Please specify the font paths that are available on all "
                   "back-end\n");
            dmxLog(dmxError,
                   "servers with the \"-fontpath\" option, or use the "
                   "\"-ignorebadfontpaths\"\n");
            dmxLog(dmxError,
                   "to ignore bad defaults.  For more information on "
                   "these and other\n");
            dmxLog(dmxError,
                   "font-path-related options, see the Xdmx man page.\n");
        }

        free(goodfps);
        if (!dmxIgnoreBadFontPaths ||
            (dmxIgnoreBadFontPaths && dmxSetFontPath(dmxScreen))) {
            /* We still have errors so return with error */
            dmxFreeFontPath(fp);
            XFreeFontPath(oldFontPath);
            return FALSE;
        }
    }

    /* Find requested font on back-end server */
    name_atom = MakeAtom("FONT", 4, TRUE);
    value_atom = 0L;

    for (i = 0; i < pFont->info.nprops; i++) {
        if ((Atom) pFont->info.props[i].name == name_atom) {
            value_atom = pFont->info.props[i].value;
            break;
        }
    }
    if (!value_atom)
        return FALSE;

    name = NameForAtom(value_atom);
    if (!name)
        return FALSE;

    pFontPriv->font[pScreen->myNum] =
        XLoadQueryFont(dmxScreen->beDisplay, name);

    /* Restore old font path */
    XSetFontPath(dmxScreen->beDisplay, oldFontPath, nOldPaths);
    XFreeFontPath(oldFontPath);
    dmxSync(dmxScreen, FALSE);

    if (!pFontPriv->font[pScreen->myNum])
        return FALSE;

    return TRUE;
}

/** Realize the font, \a pFont, on the back-end server associated with
 *  \a pScreen. */
Bool
dmxRealizeFont(ScreenPtr pScreen, FontPtr pFont)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxFontPrivPtr pFontPriv;

    if (!(pFontPriv = FontGetPrivate(pFont, dmxFontPrivateIndex))) {
        FontSetPrivate(pFont, dmxFontPrivateIndex, NULL);
        pFontPriv = malloc(sizeof(dmxFontPrivRec));
        if (!pFontPriv)
            return FALSE;
        pFontPriv->font = NULL;
        MAXSCREENSALLOC(pFontPriv->font);
        if (!pFontPriv->font) {
            free(pFontPriv);
            return FALSE;
        }
        pFontPriv->refcnt = 0;
    }

    FontSetPrivate(pFont, dmxFontPrivateIndex, (void *) pFontPriv);

    if (dmxScreen->beDisplay) {
        if (!dmxBELoadFont(pScreen, pFont))
            return FALSE;

        pFontPriv->refcnt++;
    }
    else {
        pFontPriv->font[pScreen->myNum] = NULL;
    }

    return TRUE;
}

/** Free \a pFont on the back-end associated with \a pScreen. */
Bool
dmxBEFreeFont(ScreenPtr pScreen, FontPtr pFont)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxFontPrivPtr pFontPriv = FontGetPrivate(pFont, dmxFontPrivateIndex);

    if (pFontPriv && pFontPriv->font[pScreen->myNum]) {
        XFreeFont(dmxScreen->beDisplay, pFontPriv->font[pScreen->myNum]);
        pFontPriv->font[pScreen->myNum] = NULL;
        return TRUE;
    }

    return FALSE;
}

/** Unrealize the font, \a pFont, on the back-end server associated with
 *  \a pScreen. */
Bool
dmxUnrealizeFont(ScreenPtr pScreen, FontPtr pFont)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxFontPrivPtr pFontPriv;

    if ((pFontPriv = FontGetPrivate(pFont, dmxFontPrivateIndex))) {
        /* In case the font failed to load properly */
        if (!pFontPriv->refcnt) {
            MAXSCREENSFREE(pFontPriv->font);
            free(pFontPriv);
            FontSetPrivate(pFont, dmxFontPrivateIndex, NULL);
        }
        else if (pFontPriv->font[pScreen->myNum]) {
            if (dmxScreen->beDisplay)
                dmxBEFreeFont(pScreen, pFont);

            /* The code below is non-obvious, so here's an explanation...
             *
             * When creating the default GC, the server opens up the
             * default font once for each screen, which in turn calls
             * the RealizeFont function pointer once for each screen.
             * During this process both dix's font refcnt and DMX's font
             * refcnt are incremented once for each screen.
             *
             * Later, when shutting down the X server, dix shuts down
             * each screen in reverse order.  During this shutdown
             * procedure, each screen's default GC is freed and then
             * that screen is closed by calling the CloseScreen function
             * pointer.  screenInfo.numScreens is then decremented after
             * closing each screen.  This procedure means that the dix's
             * font refcnt for the font used by the default GC's is
             * decremented once for each screen # greater than 0.
             * However, since dix's refcnt for the default font is not
             * yet 0 for each screen greater than 0, no call to the
             * UnrealizeFont function pointer is made for those screens.
             * Then, when screen 0 is being closed, dix's font refcnt
             * for the default GC's font is finally 0 and the font is
             * unrealized.  However, since screenInfo.numScreens has
             * been decremented already down to 1, only one call to
             * UnrealizeFont is made (for screen 0).  Thus, even though
             * RealizeFont was called once for each screen,
             * UnrealizeFont is only called for screen 0.
             *
             * This is a bug in dix.
             *
             * To avoid the memory leak of pFontPriv for each server
             * generation, we can also free pFontPriv if the refcnt is
             * not yet 0 but the # of screens is 1 -- i.e., the case
             * described in the dix bug above.  This is only a temporary
             * workaround until the bug in dix is solved.
             *
             * The other problem is that the font structure allocated by
             * XLoadQueryFont() above is not freed for screens > 0.
             * This problem cannot be worked around here since the back-
             * end displays for screens > 0 have already been closed by
             * the time this code is called from dix.
             *
             * When the bug in dix described above is fixed, then we can
             * remove the "|| screenInfo.numScreens == 1" code below and
             * the memory leaks will be eliminated.
             */
            if (--pFontPriv->refcnt == 0
#if 1
                /* Remove this code when the dix bug is fixed */
                || screenInfo.numScreens == 1
#endif
                ) {
                MAXSCREENSFREE(pFontPriv->font);
                free(pFontPriv);
                FontSetPrivate(pFont, dmxFontPrivateIndex, NULL);
            }
        }
    }

    return TRUE;
}
