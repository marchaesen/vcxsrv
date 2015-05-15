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
 * Colormap support. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxlog.h"
#include "dmxsync.h"
#include "dmxcmap.h"
#include "dmxvisual.h"

#include "micmap.h"

static Bool
dmxAllocateColormapPrivates(ColormapPtr pColormap)
{
    dmxColormapPrivPtr pCmapPriv;

    pCmapPriv = (dmxColormapPrivPtr) malloc(sizeof(*pCmapPriv));
    if (!pCmapPriv)
        return FALSE;
    pCmapPriv->cmap = (Colormap) 0;

    DMX_SET_COLORMAP_PRIV(pColormap, pCmapPriv);

    return TRUE;
}

/** Create \a pColormap on the back-end server. */
Bool
dmxBECreateColormap(ColormapPtr pColormap)
{
    ScreenPtr pScreen = pColormap->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxColormapPrivPtr pCmapPriv = DMX_GET_COLORMAP_PRIV(pColormap);
    VisualPtr pVisual = pColormap->pVisual;
    Visual *visual = dmxLookupVisual(pScreen, pVisual);

    if (visual) {
        pCmapPriv->cmap = XCreateColormap(dmxScreen->beDisplay,
                                          dmxScreen->scrnWin,
                                          visual,
                                          (pVisual->class & DynamicClass ?
                                           AllocAll : AllocNone));
        return pCmapPriv->cmap != 0;
    }
    else {
        dmxLog(dmxWarning, "dmxBECreateColormap: No visual found\n");
        return 0;
    }
}

/** Create colormap on back-end server associated with \a pColormap's
 *  screen. */
Bool
dmxCreateColormap(ColormapPtr pColormap)
{
    ScreenPtr pScreen = pColormap->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    Bool ret = TRUE;

    if (!dmxAllocateColormapPrivates(pColormap))
        return FALSE;

    if (dmxScreen->beDisplay) {
        if (!dmxBECreateColormap(pColormap))
            return FALSE;
    }

    DMX_UNWRAP(CreateColormap, dmxScreen, pScreen);
    if (pScreen->CreateColormap)
        ret = pScreen->CreateColormap(pColormap);
    DMX_WRAP(CreateColormap, dmxCreateColormap, dmxScreen, pScreen);

    return ret;
}

/** Destroy \a pColormap on the back-end server. */
Bool
dmxBEFreeColormap(ColormapPtr pColormap)
{
    ScreenPtr pScreen = pColormap->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxColormapPrivPtr pCmapPriv = DMX_GET_COLORMAP_PRIV(pColormap);

    if (pCmapPriv->cmap) {
        XFreeColormap(dmxScreen->beDisplay, pCmapPriv->cmap);
        pCmapPriv->cmap = (Colormap) 0;
        return TRUE;
    }

    return FALSE;
}

/** Destroy colormap on back-end server associated with \a pColormap's
 *  screen. */
void
dmxDestroyColormap(ColormapPtr pColormap)
{
    ScreenPtr pScreen = pColormap->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxColormapPrivPtr pCmapPriv = DMX_GET_COLORMAP_PRIV(pColormap);

    if (dmxScreen->beDisplay)
        dmxBEFreeColormap(pColormap);
    free(pCmapPriv);
    DMX_SET_COLORMAP_PRIV(pColormap, NULL);

    DMX_UNWRAP(DestroyColormap, dmxScreen, pScreen);
    if (pScreen->DestroyColormap)
        pScreen->DestroyColormap(pColormap);
    DMX_WRAP(DestroyColormap, dmxDestroyColormap, dmxScreen, pScreen);
}

/** Install colormap on back-end server associated with \a pColormap's
 *  screen. */
void
dmxInstallColormap(ColormapPtr pColormap)
{
    ScreenPtr pScreen = pColormap->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxColormapPrivPtr pCmapPriv = DMX_GET_COLORMAP_PRIV(pColormap);

    DMX_UNWRAP(InstallColormap, dmxScreen, pScreen);
    if (pScreen->InstallColormap)
        pScreen->InstallColormap(pColormap);
    DMX_WRAP(InstallColormap, dmxInstallColormap, dmxScreen, pScreen);

    if (dmxScreen->beDisplay) {
        XInstallColormap(dmxScreen->beDisplay, pCmapPriv->cmap);
        dmxSync(dmxScreen, FALSE);
    }
}

/** Store colors in \a pColormap on back-end server associated with \a
 *  pColormap's screen. */
void
dmxStoreColors(ColormapPtr pColormap, int ndef, xColorItem * pdef)
{
    ScreenPtr pScreen = pColormap->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxColormapPrivPtr pCmapPriv = DMX_GET_COLORMAP_PRIV(pColormap);

    if (dmxScreen->beDisplay && (pColormap->pVisual->class & DynamicClass)) {
        XColor *color = xallocarray(ndef, sizeof(*color));
        int i;

        if (color) {
            for (i = 0; i < ndef; i++) {
                color[i].pixel = pdef[i].pixel;
                color[i].red = pdef[i].red;
                color[i].blue = pdef[i].blue;
                color[i].green = pdef[i].green;
                color[i].flags = pdef[i].flags;
                color[i].pad = pdef[i].pad;
            }
            XStoreColors(dmxScreen->beDisplay, pCmapPriv->cmap, color, ndef);
            free(color);
        }
        else {                  /* xalloc failed, so fallback */
            XColor c;

            for (i = 0; i < ndef; i++) {
                c.pixel = pdef[i].pixel;
                c.red = pdef[i].red;
                c.blue = pdef[i].blue;
                c.green = pdef[i].green;
                c.flags = pdef[i].flags;
                c.pad = pdef[i].pad;
                XStoreColor(dmxScreen->beDisplay, pCmapPriv->cmap, &c);
            }
        }
        dmxSync(dmxScreen, FALSE);
    }

    DMX_UNWRAP(StoreColors, dmxScreen, pScreen);
    if (pScreen->StoreColors)
        pScreen->StoreColors(pColormap, ndef, pdef);
    DMX_WRAP(StoreColors, dmxStoreColors, dmxScreen, pScreen);
}

/** Create the DMX server's default colormap. */
Bool
dmxCreateDefColormap(ScreenPtr pScreen)
{
    return miCreateDefColormap(pScreen);
}
