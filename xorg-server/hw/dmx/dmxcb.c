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
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * This code queries and modifies the connection block. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxcb.h"
#include "dmxinput.h"
#include "dmxlog.h"

extern int connBlockScreenStart;

#ifdef PANORAMIX
#include "panoramiXsrv.h"
#endif

int dmxGlobalWidth, dmxGlobalHeight;

/** We may want the wall dimensions to be different from the bounding
 * box dimensions that Xinerama computes, so save those and update them
 * here.
 */
void
dmxSetWidthHeight(int width, int height)
{
    dmxGlobalWidth = width;
    dmxGlobalHeight = height;
}

/** Computes the global bounding box for DMX.  This may be larger than
 * the one computed by Xinerama because of the DMX configuration
 * file. */
void
dmxComputeWidthHeight(DMXRecomputeFlag flag)
{
    int i;
    DMXScreenInfo *dmxScreen;
    int w = 0;
    int h = 0;

    for (i = 0; i < dmxNumScreens; i++) {
        /* Don't use root* here because this is
         * the global bounding box. */
        dmxScreen = &dmxScreens[i];
        if (w < dmxScreen->scrnWidth + dmxScreen->rootXOrigin)
            w = dmxScreen->scrnWidth + dmxScreen->rootXOrigin;
        if (h < dmxScreen->scrnHeight + dmxScreen->rootYOrigin)
            h = dmxScreen->scrnHeight + dmxScreen->rootYOrigin;
    }
    if (!dmxGlobalWidth && !dmxGlobalHeight) {
        dmxLog(dmxInfo, "Using %dx%d as global bounding box\n", w, h);
    }
    else {
        switch (flag) {
        case DMX_NO_RECOMPUTE_BOUNDING_BOX:
            dmxLog(dmxInfo,
                   "Using old bounding box (%dx%d) instead of new (%dx%d)\n",
                   dmxGlobalWidth, dmxGlobalHeight, w, h);
            w = dmxGlobalWidth;
            h = dmxGlobalHeight;
            break;
        case DMX_RECOMPUTE_BOUNDING_BOX:
            dmxLog(dmxInfo,
                   "Using %dx%d as global bounding box, instead of %dx%d\n",
                   w, h, dmxGlobalWidth, dmxGlobalHeight);
            break;
        }
    }

    dmxGlobalWidth = w;
    dmxGlobalHeight = h;
}

/** A callback routine that hooks into Xinerama and provides a
 * convenient place to print summary log information during server
 * startup.  This routine does not modify any values. */
void
dmxConnectionBlockCallback(void)
{
    xWindowRoot *root = (xWindowRoot *) (ConnectionInfo + connBlockScreenStart);
    int offset = connBlockScreenStart + sizeof(xWindowRoot);
    int i;
    Bool *found = NULL;

    MAXSCREENSALLOC(found);
    if (!found)
        dmxLog(dmxFatal, "dmxConnectionBlockCallback: out of memory\n");

    dmxLog(dmxInfo, "===== Start of Summary =====\n");
#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        if (dmxGlobalWidth && dmxGlobalHeight
            && (dmxGlobalWidth != PanoramiXPixWidth
                || dmxGlobalHeight != PanoramiXPixHeight)) {
            dmxLog(dmxInfo,
                   "Changing Xinerama dimensions from %d %d to %d %d\n",
                   PanoramiXPixWidth, PanoramiXPixHeight,
                   dmxGlobalWidth, dmxGlobalHeight);
            PanoramiXPixWidth = root->pixWidth = dmxGlobalWidth;
            PanoramiXPixHeight = root->pixHeight = dmxGlobalHeight;
        }
        else {
            dmxGlobalWidth = PanoramiXPixWidth;
            dmxGlobalHeight = PanoramiXPixHeight;
        }
        dmxLog(dmxInfo, "%d screens configured with Xinerama (%d %d)\n",
               PanoramiXNumScreens, PanoramiXPixWidth, PanoramiXPixHeight);
        FOR_NSCREENS(i) found[i] = FALSE;
    }
    else {
#endif
        /* This never happens because we're
         * either called from a Xinerama
         * callback or during reconfiguration
         * (which only works with Xinerama on).
         * In any case, be reasonable. */
        dmxLog(dmxInfo, "%d screens configured (%d %d)\n",
               screenInfo.numScreens, root->pixWidth, root->pixHeight);
#ifdef PANORAMIX
    }
#endif

    for (i = 0; i < root->nDepths; i++) {
        xDepth *depth = (xDepth *) (ConnectionInfo + offset);
        int voffset = offset + sizeof(xDepth);
        xVisualType *visual = (xVisualType *) (ConnectionInfo + voffset);
        int j;

        dmxLog(dmxInfo, "%d visuals at depth %d:\n",
               depth->nVisuals, depth->depth);
        for (j = 0; j < depth->nVisuals; j++, visual++) {
            XVisualInfo vi;

            vi.visual = NULL;
            vi.visualid = visual->visualID;
            vi.screen = 0;
            vi.depth = depth->depth;
            vi.class = visual->class;
            vi.red_mask = visual->redMask;
            vi.green_mask = visual->greenMask;
            vi.blue_mask = visual->blueMask;
            vi.colormap_size = visual->colormapEntries;
            vi.bits_per_rgb = visual->bitsPerRGB;
            dmxLogVisual(NULL, &vi, 0);

#ifdef PANORAMIX
            if (!noPanoramiXExtension) {
                int k;

                FOR_NSCREENS(k) {
                    DMXScreenInfo *dmxScreen = &dmxScreens[k];

                    if (dmxScreen->beDisplay) {
                        XVisualInfo *pvi =
                            &dmxScreen->beVisuals[dmxScreen->beDefVisualIndex];
                        if (pvi->depth == depth->depth &&
                            pvi->class == visual->class)
                            found[k] = TRUE;
                    }
                    else {
                        /* Screen #k is detatched, so it always succeeds */
                        found[k] = TRUE;
                    }
                }
            }
#endif
        }
        offset = voffset + depth->nVisuals * sizeof(xVisualType);
    }

    dmxInputLogDevices();
    dmxLog(dmxInfo, "===== End of Summary =====\n");

#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        Bool fatal = FALSE;

        FOR_NSCREENS(i) {
            fatal |= !found[i];
            if (!found[i]) {
                dmxLog(dmxError,
                       "The default visual for screen #%d does not match "
                       "any of the\n", i);
                dmxLog(dmxError,
                       "consolidated visuals from Xinerama (listed above)\n");
            }
        }
        if (fatal)
            dmxLog(dmxFatal,
                   "dmxConnectionBlockCallback: invalid screen(s) found");
    }
#endif
    MAXSCREENSFREE(found);
}
