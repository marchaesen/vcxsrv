/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@openedhand.com>
 *
 * Copyright © 2007 OpenedHand Ltd
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of OpenedHand Ltd not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. OpenedHand Ltd makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * OpenedHand Ltd DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OpenedHand Ltd BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * This file is heavily copied from hw/xfree86/dri/xf86dri.c
 *
 * Authors:
 *    Dodji Seketeli <dodji@openedhand.com>
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include <string.h>

#include <X11/X.h>
#include <X11/Xproto.h>
#define _XF86DRI_SERVER_
#include <X11/dri/xf86dri.h>
#include <X11/dri/xf86driproto.h>
#include <xcb/xcb.h>
#include <xcb/shape.h>
#include <xcb/xf86dri.h>
#include "misc.h"
#include "privates.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "colormapst.h"
#include "cursorstr.h"
#include "scrnintstr.h"
#include "windowstr.h"
#include "servermd.h"
#include "swaprep.h"
#include "ephyrdri.h"
#include "ephyrdriext.h"
#include "hostx.h"
#define _HAVE_XALLOC_DECLS
#include "ephyrlog.h"
#include "protocol-versions.h"

typedef struct {
    int foo;
} EphyrDRIWindowPrivRec;
typedef EphyrDRIWindowPrivRec *EphyrDRIWindowPrivPtr;

typedef struct {
    CreateWindowProcPtr CreateWindow;
    DestroyWindowProcPtr DestroyWindow;
    MoveWindowProcPtr MoveWindow;
    PositionWindowProcPtr PositionWindow;
    ClipNotifyProcPtr ClipNotify;
} EphyrDRIScreenPrivRec;
typedef EphyrDRIScreenPrivRec *EphyrDRIScreenPrivPtr;

static int DRIErrorBase;

static Bool ephyrDRIScreenInit(ScreenPtr a_screen);
static Bool ephyrDRICreateWindow(WindowPtr a_win);
static Bool ephyrDRIDestroyWindow(WindowPtr a_win);
static void ephyrDRIMoveWindow(WindowPtr a_win,
                               int a_x, int a_y,
                               WindowPtr a_siblings, VTKind a_kind);
static Bool ephyrDRIPositionWindow(WindowPtr a_win, int x, int y);
static void ephyrDRIClipNotify(WindowPtr a_win, int a_x, int a_y);

static Bool EphyrMirrorHostVisuals(ScreenPtr a_screen);
static Bool destroyHostPeerWindow(const WindowPtr a_win);
static Bool findWindowPairFromLocal(WindowPtr a_local,
                                    EphyrWindowPair ** a_pair);

static unsigned char DRIReqCode = 0;

static DevPrivateKeyRec ephyrDRIWindowKeyRec;

#define ephyrDRIWindowKey (&ephyrDRIWindowKeyRec)
static DevPrivateKeyRec ephyrDRIScreenKeyRec;

#define ephyrDRIScreenKey (&ephyrDRIScreenKeyRec)

#define GET_EPHYR_DRI_WINDOW_PRIV(win) ((EphyrDRIWindowPrivPtr) \
    dixLookupPrivate(&(win)->devPrivates, ephyrDRIWindowKey))
#define GET_EPHYR_DRI_SCREEN_PRIV(screen) ((EphyrDRIScreenPrivPtr) \
    dixLookupPrivate(&(screen)->devPrivates, ephyrDRIScreenKey))

static Bool
ephyrDRIScreenInit(ScreenPtr a_screen)
{
    Bool is_ok = FALSE;
    EphyrDRIScreenPrivPtr screen_priv = NULL;

    EPHYR_RETURN_VAL_IF_FAIL(a_screen, FALSE);

    screen_priv = GET_EPHYR_DRI_SCREEN_PRIV(a_screen);
    EPHYR_RETURN_VAL_IF_FAIL(screen_priv, FALSE);

    screen_priv->CreateWindow = a_screen->CreateWindow;
    screen_priv->DestroyWindow = a_screen->DestroyWindow;
    screen_priv->MoveWindow = a_screen->MoveWindow;
    screen_priv->PositionWindow = a_screen->PositionWindow;
    screen_priv->ClipNotify = a_screen->ClipNotify;

    a_screen->CreateWindow = ephyrDRICreateWindow;
    a_screen->DestroyWindow = ephyrDRIDestroyWindow;
    a_screen->MoveWindow = ephyrDRIMoveWindow;
    a_screen->PositionWindow = ephyrDRIPositionWindow;
    a_screen->ClipNotify = ephyrDRIClipNotify;

    is_ok = TRUE;

    return is_ok;
}

static Bool
ephyrDRICreateWindow(WindowPtr a_win)
{
    Bool is_ok = FALSE;
    ScreenPtr screen = NULL;
    EphyrDRIScreenPrivPtr screen_priv = NULL;

    EPHYR_RETURN_VAL_IF_FAIL(a_win, FALSE);
    screen = a_win->drawable.pScreen;
    EPHYR_RETURN_VAL_IF_FAIL(screen, FALSE);
    screen_priv = GET_EPHYR_DRI_SCREEN_PRIV(screen);
    EPHYR_RETURN_VAL_IF_FAIL(screen_priv && screen_priv->CreateWindow, FALSE);

    EPHYR_LOG("enter. win:%p\n", a_win);

    screen->CreateWindow = screen_priv->CreateWindow;
    is_ok = (*screen->CreateWindow) (a_win);
    screen->CreateWindow = ephyrDRICreateWindow;

    if (is_ok) {
        dixSetPrivate(&a_win->devPrivates, ephyrDRIWindowKey, NULL);
    }
    return is_ok;
}

static Bool
ephyrDRIDestroyWindow(WindowPtr a_win)
{
    Bool is_ok = FALSE;
    ScreenPtr screen = NULL;
    EphyrDRIScreenPrivPtr screen_priv = NULL;

    EPHYR_RETURN_VAL_IF_FAIL(a_win, FALSE);
    screen = a_win->drawable.pScreen;
    EPHYR_RETURN_VAL_IF_FAIL(screen, FALSE);
    screen_priv = GET_EPHYR_DRI_SCREEN_PRIV(screen);
    EPHYR_RETURN_VAL_IF_FAIL(screen_priv && screen_priv->DestroyWindow, FALSE);

    screen->DestroyWindow = screen_priv->DestroyWindow;
    if (screen->DestroyWindow) {
        is_ok = (*screen->DestroyWindow) (a_win);
    }
    screen->DestroyWindow = ephyrDRIDestroyWindow;

    if (is_ok) {
        EphyrDRIWindowPrivPtr win_priv = GET_EPHYR_DRI_WINDOW_PRIV(a_win);

        if (win_priv) {
            destroyHostPeerWindow(a_win);
            free(win_priv);
            dixSetPrivate(&a_win->devPrivates, ephyrDRIWindowKey, NULL);
            EPHYR_LOG("destroyed the remote peer window\n");
        }
    }
    return is_ok;
}

static void
ephyrDRIMoveWindow(WindowPtr a_win,
                   int a_x, int a_y, WindowPtr a_siblings, VTKind a_kind)
{
    ScreenPtr screen = NULL;
    EphyrDRIScreenPrivPtr screen_priv = NULL;
    EphyrDRIWindowPrivPtr win_priv = NULL;
    EphyrWindowPair *pair = NULL;
    EphyrBox geo;
    int x = 0, y = 0;           /*coords relative to parent window */

    EPHYR_RETURN_IF_FAIL(a_win);

    EPHYR_LOG("enter\n");
    screen = a_win->drawable.pScreen;
    EPHYR_RETURN_IF_FAIL(screen);
    screen_priv = GET_EPHYR_DRI_SCREEN_PRIV(screen);
    EPHYR_RETURN_IF_FAIL(screen_priv && screen_priv->MoveWindow);

    screen->MoveWindow = screen_priv->MoveWindow;
    if (screen->MoveWindow) {
        (*screen->MoveWindow) (a_win, a_x, a_y, a_siblings, a_kind);
    }
    screen->MoveWindow = ephyrDRIMoveWindow;

    EPHYR_LOG("window: %p\n", a_win);
    if (!a_win->parent) {
        EPHYR_LOG("cannot move root window\n");
        return;
    }
    win_priv = GET_EPHYR_DRI_WINDOW_PRIV(a_win);
    if (!win_priv) {
        EPHYR_LOG("not a DRI peered window\n");
        return;
    }
    if (!findWindowPairFromLocal(a_win, &pair) || !pair) {
        EPHYR_LOG_ERROR("failed to get window pair\n");
        return;
    }
    /*compute position relative to parent window */
    x = a_win->drawable.x - a_win->parent->drawable.x;
    y = a_win->drawable.y - a_win->parent->drawable.y;
    /*set the geometry to pass to hostx_set_window_geometry */
    memset(&geo, 0, sizeof(geo));
    geo.x = x;
    geo.y = y;
    geo.width = a_win->drawable.width;
    geo.height = a_win->drawable.height;
    hostx_set_window_geometry(pair->remote, &geo);
}

static Bool
ephyrDRIPositionWindow(WindowPtr a_win, int a_x, int a_y)
{
    Bool is_ok = FALSE;
    ScreenPtr screen = NULL;
    EphyrDRIScreenPrivPtr screen_priv = NULL;
    EphyrDRIWindowPrivPtr win_priv = NULL;
    EphyrWindowPair *pair = NULL;
    EphyrBox geo;

    EPHYR_RETURN_VAL_IF_FAIL(a_win, FALSE);

    EPHYR_LOG("enter\n");
    screen = a_win->drawable.pScreen;
    EPHYR_RETURN_VAL_IF_FAIL(screen, FALSE);
    screen_priv = GET_EPHYR_DRI_SCREEN_PRIV(screen);
    EPHYR_RETURN_VAL_IF_FAIL(screen_priv && screen_priv->PositionWindow, FALSE);

    screen->PositionWindow = screen_priv->PositionWindow;
    if (screen->PositionWindow) {
        (*screen->PositionWindow) (a_win, a_x, a_y);
    }
    screen->PositionWindow = ephyrDRIPositionWindow;

    EPHYR_LOG("window: %p\n", a_win);
    win_priv = GET_EPHYR_DRI_WINDOW_PRIV(a_win);
    if (!win_priv) {
        EPHYR_LOG("not a DRI peered window\n");
        is_ok = TRUE;
        goto out;
    }
    if (!findWindowPairFromLocal(a_win, &pair) || !pair) {
        EPHYR_LOG_ERROR("failed to get window pair\n");
        goto out;
    }
    /*set the geometry to pass to hostx_set_window_geometry */
    memset(&geo, 0, sizeof(geo));
    geo.x = a_x;
    geo.y = a_y;
    geo.width = a_win->drawable.width;
    geo.height = a_win->drawable.height;
    hostx_set_window_geometry(pair->remote, &geo);
    is_ok = TRUE;

 out:
    EPHYR_LOG("leave. is_ok:%d\n", is_ok);
    /*do cleanup here */
    return is_ok;
}

static void
ephyrDRIClipNotify(WindowPtr a_win, int a_x, int a_y)
{
    ScreenPtr screen = NULL;
    EphyrDRIScreenPrivPtr screen_priv = NULL;
    EphyrDRIWindowPrivPtr win_priv = NULL;
    EphyrWindowPair *pair = NULL;
    EphyrRect *rects = NULL;
    int i = 0;

    EPHYR_RETURN_IF_FAIL(a_win);

    EPHYR_LOG("enter\n");
    screen = a_win->drawable.pScreen;
    EPHYR_RETURN_IF_FAIL(screen);
    screen_priv = GET_EPHYR_DRI_SCREEN_PRIV(screen);
    EPHYR_RETURN_IF_FAIL(screen_priv && screen_priv->ClipNotify);

    screen->ClipNotify = screen_priv->ClipNotify;
    if (screen->ClipNotify) {
        (*screen->ClipNotify) (a_win, a_x, a_y);
    }
    screen->ClipNotify = ephyrDRIClipNotify;

    EPHYR_LOG("window: %p\n", a_win);
    win_priv = GET_EPHYR_DRI_WINDOW_PRIV(a_win);
    if (!win_priv) {
        EPHYR_LOG("not a DRI peered window\n");
        goto out;
    }
    if (!findWindowPairFromLocal(a_win, &pair) || !pair) {
        EPHYR_LOG_ERROR("failed to get window pair\n");
        goto out;
    }
    rects = calloc(RegionNumRects(&a_win->clipList), sizeof(EphyrRect));
    for (i = 0; i < RegionNumRects(&a_win->clipList); i++) {
        memmove(&rects[i],
                &RegionRects(&a_win->clipList)[i], sizeof(EphyrRect));
        rects[i].x1 -= a_win->drawable.x;
        rects[i].x2 -= a_win->drawable.x;
        rects[i].y1 -= a_win->drawable.y;
        rects[i].y2 -= a_win->drawable.y;
    }
    /*
     * push the clipping region of this window
     * to the peer window in the host
     */
    hostx_set_window_bounding_rectangles
        (pair->remote, rects, RegionNumRects(&a_win->clipList));

 out:
    free(rects);
    rects = NULL;

    EPHYR_LOG("leave.\n");
    /*do cleanup here */
}

/**
 * Duplicates a visual of a_screen
 * In screen a_screen, for depth a_depth, find a visual which
 * bitsPerRGBValue and colormap size equal
 * a_bits_per_rgb_values and a_colormap_entries.
 * The ID of that duplicated visual is set to a_new_id.
 * That duplicated visual is then added to the list of visuals
 * of the screen.
 */
static Bool
EphyrDuplicateVisual(unsigned int a_screen,
                     short a_depth,
                     short a_class,
                     short a_bits_per_rgb_values,
                     short a_colormap_entries,
                     unsigned int a_red_mask,
                     unsigned int a_green_mask,
                     unsigned int a_blue_mask, unsigned int a_new_id)
{
    Bool is_ok = FALSE, found_visual = FALSE, found_depth = FALSE;
    ScreenPtr screen = NULL;
    VisualRec new_visual, *new_visuals = NULL;
    int i = 0;

    EPHYR_LOG("enter\n");
    if (a_screen >= screenInfo.numScreens) {
        EPHYR_LOG_ERROR("bad screen number\n");
        goto out;
    }
    memset(&new_visual, 0, sizeof(VisualRec));

    /*get the screen pointed to by a_screen */
    screen = screenInfo.screens[a_screen];
    EPHYR_RETURN_VAL_IF_FAIL(screen, FALSE);

    /*
     * In that screen, first look for an existing visual that has the
     * same characteristics as those passed in parameter
     * to this function and copy it.
     */
    for (i = 0; i < screen->numVisuals; i++) {
        if (screen->visuals[i].bitsPerRGBValue == a_bits_per_rgb_values &&
            screen->visuals[i].ColormapEntries == a_colormap_entries) {
            /*copy the visual found */
            memcpy(&new_visual, &screen->visuals[i], sizeof(new_visual));
            new_visual.vid = a_new_id;
            new_visual.class = a_class;
            new_visual.redMask = a_red_mask;
            new_visual.greenMask = a_green_mask;
            new_visual.blueMask = a_blue_mask;
            found_visual = TRUE;
            EPHYR_LOG("found a visual that matches visual id: %d\n", a_new_id);
            break;
        }
    }
    if (!found_visual) {
        EPHYR_LOG("did not find any visual matching %d\n", a_new_id);
        goto out;
    }
    /*
     * be prepare to extend screen->visuals to add new_visual to it
     */
    new_visuals = calloc(screen->numVisuals + 1, sizeof(VisualRec));
    memmove(new_visuals,
            screen->visuals, screen->numVisuals * sizeof(VisualRec));
    memmove(&new_visuals[screen->numVisuals], &new_visual, sizeof(VisualRec));
    /*
     * Now, in that same screen, update the screen->allowedDepths member.
     * In that array, each element represents the visuals applicable to
     * a given depth. So we need to add an entry matching the new visual
     * that we are going to add to screen->visuals
     */
    for (i = 0; i < screen->numDepths; i++) {
        VisualID *vids = NULL;
        DepthPtr cur_depth = NULL;

        /*find the entry matching a_depth */
        if (screen->allowedDepths[i].depth != a_depth)
            continue;
        cur_depth = &screen->allowedDepths[i];
        /*
         * extend the list of visual IDs in that entry,
         * so to add a_new_id in there.
         */
        vids = reallocarray(cur_depth->vids,
                            cur_depth->numVids + 1, sizeof(VisualID));
        if (!vids) {
            EPHYR_LOG_ERROR("failed to realloc numids\n");
            goto out;
        }
        vids[cur_depth->numVids] = a_new_id;
        /*
         * Okay now commit our change.
         * Do really update screen->allowedDepths[i]
         */
        cur_depth->numVids++;
        cur_depth->vids = vids;
        found_depth = TRUE;
    }
    if (!found_depth) {
        EPHYR_LOG_ERROR("failed to update screen[%d]->allowedDepth\n",
                        a_screen);
        goto out;
    }
    /*
     * Commit our change to screen->visuals
     */
    free(screen->visuals);
    screen->visuals = new_visuals;
    screen->numVisuals++;
    new_visuals = NULL;

    is_ok = TRUE;
 out:
    free(new_visuals);
    new_visuals = NULL;

    EPHYR_LOG("leave\n");
    return is_ok;
}

/**
 * Duplicates the visuals of the host X server.
 * This is necessary to have visuals that have the same
 * ID as those of the host X. It is important to have that for
 * GLX.
 */
static Bool
EphyrMirrorHostVisuals(ScreenPtr a_screen)
{
    Bool is_ok = FALSE;
    EphyrHostVisualInfo *visuals = NULL;
    int nb_visuals = 0, i = 0;

    EPHYR_LOG("enter\n");
    if (!hostx_get_visuals_info(&visuals, &nb_visuals)) {
        EPHYR_LOG_ERROR("failed to get host visuals\n");
        goto out;
    }
    for (i = 0; i < nb_visuals; i++) {
        if (!EphyrDuplicateVisual(a_screen->myNum,
                                  visuals[i].depth,
                                  visuals[i].class,
                                  visuals[i].bits_per_rgb,
                                  visuals[i].colormap_size,
                                  visuals[i].red_mask,
                                  visuals[i].green_mask,
                                  visuals[i].blue_mask, visuals[i].visualid)) {
            EPHYR_LOG_ERROR("failed to duplicate host visual %d\n",
                            (int) visuals[i].visualid);
        }
    }

    is_ok = TRUE;
 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

static int
ProcXF86DRIQueryVersion(register ClientPtr client)
{
    xXF86DRIQueryVersionReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .majorVersion = SERVER_XF86DRI_MAJOR_VERSION,
        .minorVersion = SERVER_XF86DRI_MINOR_VERSION,
        .patchVersion = SERVER_XF86DRI_PATCH_VERSION
    };

    REQUEST_SIZE_MATCH(xXF86DRIQueryVersionReq);

    EPHYR_LOG("enter\n");

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swaps(&rep.majorVersion);
        swaps(&rep.minorVersion);
        swapl(&rep.patchVersion);
    }
    WriteToClient(client, sizeof(xXF86DRIQueryVersionReply), &rep);
    EPHYR_LOG("leave\n");
    return Success;
}

static int
ProcXF86DRIQueryDirectRenderingCapable(register ClientPtr client)
{
    xXF86DRIQueryDirectRenderingCapableReply rep;
    Bool isCapable;

    REQUEST(xXF86DRIQueryDirectRenderingCapableReq);
    REQUEST_SIZE_MATCH(xXF86DRIQueryDirectRenderingCapableReq);

    EPHYR_LOG("enter\n");
    if (stuff->screen >= screenInfo.numScreens) {
        client->errorValue = stuff->screen;
        return BadValue;
    }

    if (!ephyrDRIQueryDirectRenderingCapable(stuff->screen, &isCapable)) {
        return BadValue;
    }

    if (!client->local || client->swapped)
        isCapable = 0;

    rep = (xXF86DRIQueryDirectRenderingCapableReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .isCapable = isCapable
    };

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
    }

    WriteToClient(client, sizeof(xXF86DRIQueryDirectRenderingCapableReply),
                  &rep);
    EPHYR_LOG("leave\n");

    return Success;
}

static int
ProcXF86DRIOpenConnection(register ClientPtr client)
{
    xXF86DRIOpenConnectionReply rep;
    drm_handle_t hSAREA;
    char *busIdString = NULL;
    CARD32 busIdStringLength = 0;

    REQUEST(xXF86DRIOpenConnectionReq);
    REQUEST_SIZE_MATCH(xXF86DRIOpenConnectionReq);

    EPHYR_LOG("enter\n");
    if (stuff->screen >= screenInfo.numScreens) {
        client->errorValue = stuff->screen;
        return BadValue;
    }

    if (!ephyrDRIOpenConnection(stuff->screen, &hSAREA, &busIdString)) {
        return BadValue;
    }

    if (busIdString)
        busIdStringLength = strlen(busIdString);

    rep = (xXF86DRIOpenConnectionReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = bytes_to_int32(SIZEOF(xXF86DRIOpenConnectionReply) -
                                 SIZEOF(xGenericReply) +
                                 pad_to_int32(busIdStringLength)),
        .hSAREALow = (CARD32) (hSAREA & 0xffffffff),
#if defined(LONG64) && !defined(__linux__)
        .hSAREAHigh = (CARD32) (hSAREA >> 32),
#else
        .hSAREAHigh = 0,
#endif
        .busIdStringLength = busIdStringLength
    };

    WriteToClient(client, sizeof(xXF86DRIOpenConnectionReply), &rep);
    if (busIdStringLength)
        WriteToClient(client, busIdStringLength, busIdString);
    free(busIdString);
    EPHYR_LOG("leave\n");
    return Success;
}

static int
ProcXF86DRIAuthConnection(register ClientPtr client)
{
    xXF86DRIAuthConnectionReply rep;

    REQUEST(xXF86DRIAuthConnectionReq);
    REQUEST_SIZE_MATCH(xXF86DRIAuthConnectionReq);

    EPHYR_LOG("enter\n");
    if (stuff->screen >= screenInfo.numScreens) {
        client->errorValue = stuff->screen;
        return BadValue;
    }

    rep = (xXF86DRIAuthConnectionReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .authenticated = 1
    };

    if (!ephyrDRIAuthConnection(stuff->screen, stuff->magic)) {
        ErrorF("Failed to authenticate %lu\n", (unsigned long) stuff->magic);
        rep.authenticated = 0;
    }
    WriteToClient(client, sizeof(xXF86DRIAuthConnectionReply), &rep);
    EPHYR_LOG("leave\n");
    return Success;
}

static int
ProcXF86DRICloseConnection(register ClientPtr client)
{
    REQUEST(xXF86DRICloseConnectionReq);
    REQUEST_SIZE_MATCH(xXF86DRICloseConnectionReq);
    EPHYR_LOG("enter\n");
    if (stuff->screen >= screenInfo.numScreens) {
        client->errorValue = stuff->screen;
        return BadValue;
    }

    /*
       DRICloseConnection( screenInfo.screens[stuff->screen]);
     */

    EPHYR_LOG("leave\n");
    return Success;
}

static int
ProcXF86DRIGetClientDriverName(register ClientPtr client)
{
    xXF86DRIGetClientDriverNameReply rep =  {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .clientDriverNameLength = 0
    };
    char *clientDriverName;

    REQUEST(xXF86DRIGetClientDriverNameReq);
    REQUEST_SIZE_MATCH(xXF86DRIGetClientDriverNameReq);

    EPHYR_LOG("enter\n");
    if (stuff->screen >= screenInfo.numScreens) {
        client->errorValue = stuff->screen;
        return BadValue;
    }

    ephyrDRIGetClientDriverName(stuff->screen,
                                (int *) &rep.ddxDriverMajorVersion,
                                (int *) &rep.ddxDriverMinorVersion,
                                (int *) &rep.ddxDriverPatchVersion,
                                &clientDriverName);
    if (clientDriverName)
        rep.clientDriverNameLength = strlen(clientDriverName);
    rep.length = bytes_to_int32(SIZEOF(xXF86DRIGetClientDriverNameReply) -
                                SIZEOF(xGenericReply) +
                                pad_to_int32(rep.clientDriverNameLength));

    WriteToClient(client, sizeof(xXF86DRIGetClientDriverNameReply), &rep);
    if (rep.clientDriverNameLength)
        WriteToClient(client, rep.clientDriverNameLength, clientDriverName);
    EPHYR_LOG("leave\n");
    return Success;
}

static int
ProcXF86DRICreateContext(register ClientPtr client)
{
    xXF86DRICreateContextReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0
    };
    ScreenPtr pScreen;
    VisualPtr visual;
    int i = 0;

    REQUEST(xXF86DRICreateContextReq);
    REQUEST_SIZE_MATCH(xXF86DRICreateContextReq);

    EPHYR_LOG("enter\n");
    if (stuff->screen >= screenInfo.numScreens) {
        client->errorValue = stuff->screen;
        return BadValue;
    }

    pScreen = screenInfo.screens[stuff->screen];
    visual = pScreen->visuals;

    /* Find the requested X visual */
    for (i = 0; i < pScreen->numVisuals; i++, visual++)
        if (visual->vid == stuff->visual)
            break;
    if (i == pScreen->numVisuals) {
        /* No visual found */
        return BadValue;
    }

    if (!ephyrDRICreateContext(stuff->screen,
                               stuff->visual,
                               stuff->context,
                               (drm_context_t *) &rep.hHWContext)) {
        return BadValue;
    }

    WriteToClient(client, sizeof(xXF86DRICreateContextReply), &rep);
    EPHYR_LOG("leave\n");
    return Success;
}

static int
ProcXF86DRIDestroyContext(register ClientPtr client)
{
    REQUEST(xXF86DRIDestroyContextReq);
    REQUEST_SIZE_MATCH(xXF86DRIDestroyContextReq);
    EPHYR_LOG("enter\n");

    if (stuff->screen >= screenInfo.numScreens) {
        client->errorValue = stuff->screen;
        return BadValue;
    }

    if (!ephyrDRIDestroyContext(stuff->screen, stuff->context)) {
        return BadValue;
    }

    EPHYR_LOG("leave\n");
    return Success;
}

static Bool
getWindowVisual(const WindowPtr a_win, VisualPtr * a_visual)
{
    int i = 0, visual_id = 0;

    EPHYR_RETURN_VAL_IF_FAIL(a_win
                             && a_win->drawable.pScreen
                             && a_win->drawable.pScreen->visuals, FALSE);

    visual_id = wVisual(a_win);
    for (i = 0; i < a_win->drawable.pScreen->numVisuals; i++) {
        if (a_win->drawable.pScreen->visuals[i].vid == visual_id) {
            *a_visual = &a_win->drawable.pScreen->visuals[i];
            return TRUE;
        }
    }
    return FALSE;
}

#define NUM_WINDOW_PAIRS 256
static EphyrWindowPair window_pairs[NUM_WINDOW_PAIRS];

static Bool
appendWindowPairToList(WindowPtr a_local, int a_remote)
{
    int i = 0;

    EPHYR_RETURN_VAL_IF_FAIL(a_local, FALSE);

    EPHYR_LOG("(local,remote):(%p, %d)\n", a_local, a_remote);

    for (i = 0; i < NUM_WINDOW_PAIRS; i++) {
        if (window_pairs[i].local == NULL) {
            window_pairs[i].local = a_local;
            window_pairs[i].remote = a_remote;
            return TRUE;
        }
    }
    return FALSE;
}

static Bool
findWindowPairFromLocal(WindowPtr a_local, EphyrWindowPair ** a_pair)
{
    int i = 0;

    EPHYR_RETURN_VAL_IF_FAIL(a_pair && a_local, FALSE);

    for (i = 0; i < NUM_WINDOW_PAIRS; i++) {
        if (window_pairs[i].local == a_local) {
            *a_pair = &window_pairs[i];
            EPHYR_LOG("found (%p, %d)\n", (*a_pair)->local, (*a_pair)->remote);
            return TRUE;
        }
    }
    return FALSE;
}

Bool
findWindowPairFromRemote(int a_remote, EphyrWindowPair ** a_pair)
{
    int i = 0;

    EPHYR_RETURN_VAL_IF_FAIL(a_pair, FALSE);

    for (i = 0; i < NUM_WINDOW_PAIRS; i++) {
        if (window_pairs[i].remote == a_remote) {
            *a_pair = &window_pairs[i];
            EPHYR_LOG("found (%p, %d)\n", (*a_pair)->local, (*a_pair)->remote);
            return TRUE;
        }
    }
    return FALSE;
}

static Bool
createHostPeerWindow(const WindowPtr a_win, int *a_peer_win)
{
    Bool is_ok = FALSE;
    VisualPtr visual = NULL;
    EphyrBox geo;

    EPHYR_RETURN_VAL_IF_FAIL(a_win && a_peer_win, FALSE);
    EPHYR_RETURN_VAL_IF_FAIL(a_win->drawable.pScreen, FALSE);

    EPHYR_LOG("enter. a_win '%p'\n", a_win);
    if (!getWindowVisual(a_win, &visual)) {
        EPHYR_LOG_ERROR("failed to get window visual\n");
        goto out;
    }
    if (!visual) {
        EPHYR_LOG_ERROR("failed to create visual\n");
        goto out;
    }
    memset(&geo, 0, sizeof(geo));
    geo.x = a_win->drawable.x;
    geo.y = a_win->drawable.y;
    geo.width = a_win->drawable.width;
    geo.height = a_win->drawable.height;
    if (!hostx_create_window(a_win->drawable.pScreen->myNum,
                             &geo, visual->vid, a_peer_win)) {
        EPHYR_LOG_ERROR("failed to create host peer window\n");
        goto out;
    }
    if (!appendWindowPairToList(a_win, *a_peer_win)) {
        EPHYR_LOG_ERROR("failed to append window to pair list\n");
        goto out;
    }
    is_ok = TRUE;
 out:
    EPHYR_LOG("leave:remote win%d\n", *a_peer_win);
    return is_ok;
}

static Bool
destroyHostPeerWindow(const WindowPtr a_win)
{
    Bool is_ok = FALSE;
    EphyrWindowPair *pair = NULL;

    EPHYR_RETURN_VAL_IF_FAIL(a_win, FALSE);

    EPHYR_LOG("enter\n");

    if (!findWindowPairFromLocal(a_win, &pair) || !pair) {
        EPHYR_LOG_ERROR("failed to find peer to local window\n");
        goto out;
    }
    hostx_destroy_window(pair->remote);
    is_ok = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

static int
ProcXF86DRICreateDrawable(ClientPtr client)
{
    xXF86DRICreateDrawableReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0
    };
    DrawablePtr drawable = NULL;
    WindowPtr window = NULL;
    EphyrWindowPair *pair = NULL;
    EphyrDRIWindowPrivPtr win_priv = NULL;
    int rc = 0, remote_win = 0;

    REQUEST(xXF86DRICreateDrawableReq);
    REQUEST_SIZE_MATCH(xXF86DRICreateDrawableReq);

    EPHYR_LOG("enter\n");
    if (stuff->screen >= screenInfo.numScreens) {
        client->errorValue = stuff->screen;
        return BadValue;
    }

    rc = dixLookupDrawable(&drawable, stuff->drawable, client, 0,
                           DixReadAccess);
    if (rc != Success)
        return rc;
    if (drawable->type != DRAWABLE_WINDOW) {
        EPHYR_LOG_ERROR("non drawable windows are not yet supported\n");
        return BadImplementation;
    }
    EPHYR_LOG("lookedup drawable %p\n", drawable);
    window = (WindowPtr) drawable;
    if (findWindowPairFromLocal(window, &pair) && pair) {
        remote_win = pair->remote;
        EPHYR_LOG("found window '%p' paire with remote '%d'\n",
                  window, remote_win);
    }
    else if (!createHostPeerWindow(window, &remote_win)) {
        EPHYR_LOG_ERROR("failed to create host peer window\n");
        return BadAlloc;
    }

    if (!ephyrDRICreateDrawable(stuff->screen,
                                remote_win,
                                (drm_drawable_t *) &rep.hHWDrawable)) {
        EPHYR_LOG_ERROR("failed to create dri drawable\n");
        return BadValue;
    }

    win_priv = GET_EPHYR_DRI_WINDOW_PRIV(window);
    if (!win_priv) {
        win_priv = calloc(1, sizeof(EphyrDRIWindowPrivRec));
        if (!win_priv) {
            EPHYR_LOG_ERROR("failed to allocate window private\n");
            return BadAlloc;
        }
        dixSetPrivate(&window->devPrivates, ephyrDRIWindowKey, win_priv);
        EPHYR_LOG("paired window '%p' with remote '%d'\n", window, remote_win);
    }

    WriteToClient(client, sizeof(xXF86DRICreateDrawableReply), &rep);
    EPHYR_LOG("leave\n");
    return Success;
}

static int
ProcXF86DRIDestroyDrawable(register ClientPtr client)
{
    DrawablePtr drawable = NULL;
    WindowPtr window = NULL;
    EphyrWindowPair *pair = NULL;
    int rc = 0;

    REQUEST(xXF86DRIDestroyDrawableReq);
    REQUEST_SIZE_MATCH(xXF86DRIDestroyDrawableReq);

    EPHYR_LOG("enter\n");
    if (stuff->screen >= screenInfo.numScreens) {
        client->errorValue = stuff->screen;
        return BadValue;
    }

    rc = dixLookupDrawable(&drawable,
                           stuff->drawable, client, 0, DixReadAccess);
    if (rc != Success)
        return rc;
    if (drawable->type != DRAWABLE_WINDOW) {
        EPHYR_LOG_ERROR("non drawable windows are not yet supported\n");
        return BadImplementation;
    }
    window = (WindowPtr) drawable;
    if (!findWindowPairFromLocal(window, &pair) && pair) {
        EPHYR_LOG_ERROR("failed to find pair window\n");
        return BadImplementation;
    }
    if (!ephyrDRIDestroyDrawable(stuff->screen,
                                 pair->remote /*drawable in host x */ )) {
        EPHYR_LOG_ERROR("failed to destroy dri drawable\n");
        return BadImplementation;
    }
    pair->local = NULL;
    pair->remote = 0;

    EPHYR_LOG("leave\n");
    return Success;
}

static int
ProcXF86DRIGetDrawableInfo(register ClientPtr client)
{
    xXF86DRIGetDrawableInfoReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0
    };
    DrawablePtr drawable;
    WindowPtr window = NULL;
    EphyrWindowPair *pair = NULL;
    int X = 0, Y = 0, W = 0, H = 0, backX = 0, backY = 0, rc = 0, i = 0;
    drm_clip_rect_t *clipRects = NULL;
    drm_clip_rect_t *backClipRects = NULL;

    REQUEST(xXF86DRIGetDrawableInfoReq);
    REQUEST_SIZE_MATCH(xXF86DRIGetDrawableInfoReq);

    EPHYR_LOG("enter\n");
    if (stuff->screen >= screenInfo.numScreens) {
        client->errorValue = stuff->screen;
        return BadValue;
    }

    rc = dixLookupDrawable(&drawable, stuff->drawable, client, 0,
                           DixReadAccess);
    if (rc != Success || !drawable) {
        EPHYR_LOG_ERROR("could not get drawable\n");
        return rc;
    }

    if (drawable->type != DRAWABLE_WINDOW) {
        EPHYR_LOG_ERROR("non windows type drawables are not yes supported\n");
        return BadImplementation;
    }
    window = (WindowPtr) drawable;
    memset(&pair, 0, sizeof(pair));
    if (!findWindowPairFromLocal(window, &pair) || !pair) {
        EPHYR_LOG_ERROR("failed to find remote peer drawable\n");
        return BadMatch;
    }
    EPHYR_LOG("clip list of xephyr gl drawable:\n");
    for (i = 0; i < RegionNumRects(&window->clipList); i++) {
        EPHYR_LOG("x1:%d, y1:%d, x2:%d, y2:%d\n",
                  RegionRects(&window->clipList)[i].x1,
                  RegionRects(&window->clipList)[i].y1,
                  RegionRects(&window->clipList)[i].x2,
                  RegionRects(&window->clipList)[i].y2);
    }

    if (!ephyrDRIGetDrawableInfo(stuff->screen,
                                 pair->remote /*the drawable in hostx */ ,
                                 (unsigned int *) &rep.drawableTableIndex,
                                 (unsigned int *) &rep.drawableTableStamp,
                                 (int *) &X,
                                 (int *) &Y,
                                 (int *) &W,
                                 (int *) &H,
                                 (int *) &rep.numClipRects,
                                 &clipRects,
                                 &backX,
                                 &backY,
                                 (int *) &rep.numBackClipRects,
                                 &backClipRects)) {
        return BadValue;
    }
    EPHYR_LOG("num clip rects:%d, num back clip rects:%d\n",
              (int) rep.numClipRects, (int) rep.numBackClipRects);

    rep.drawableX = X;
    rep.drawableY = Y;
    rep.drawableWidth = W;
    rep.drawableHeight = H;
    rep.length = (SIZEOF(xXF86DRIGetDrawableInfoReply) - SIZEOF(xGenericReply));

    rep.backX = backX;
    rep.backY = backY;

    if (rep.numClipRects) {
        if (clipRects) {
            ScreenPtr pScreen = screenInfo.screens[stuff->screen];

            EPHYR_LOG("clip list of host gl drawable:\n");
            for (i = 0; i < rep.numClipRects; i++) {
                clipRects[i].x1 = max(clipRects[i].x1, 0);
                clipRects[i].y1 = max(clipRects[i].y1, 0);
                clipRects[i].x2 = min(clipRects[i].x2,
                                      pScreen->width + clipRects[i].x1);
                clipRects[i].y2 = min(clipRects[i].y2,
                                      pScreen->width + clipRects[i].y1);

                EPHYR_LOG("x1:%d, y1:%d, x2:%d, y2:%d\n",
                          clipRects[i].x1, clipRects[i].y1,
                          clipRects[i].x2, clipRects[i].y2);
            }
        }
        else {
            rep.numClipRects = 0;
        }
    }
    else {
        EPHYR_LOG("got zero host gl drawable clipping rects\n");
    }
    rep.length += sizeof(drm_clip_rect_t) * rep.numClipRects;
    backClipRects = clipRects;
    rep.numBackClipRects = rep.numClipRects;
    if (rep.numBackClipRects)
        rep.length += sizeof(drm_clip_rect_t) * rep.numBackClipRects;
    EPHYR_LOG("num host clip rects:%d\n", (int) rep.numClipRects);
    EPHYR_LOG("num host back clip rects:%d\n", (int) rep.numBackClipRects);

    rep.length = bytes_to_int32(rep.length);

    WriteToClient(client, sizeof(xXF86DRIGetDrawableInfoReply), &rep);

    if (rep.numClipRects) {
        WriteToClient(client,
                      sizeof(drm_clip_rect_t) * rep.numClipRects,
                      clipRects);
    }

    if (rep.numBackClipRects) {
        WriteToClient(client,
                      sizeof(drm_clip_rect_t) * rep.numBackClipRects,
                      backClipRects);
    }
    free(clipRects);
    clipRects = NULL;

    EPHYR_LOG("leave\n");

    return Success;
}

static int
ProcXF86DRIGetDeviceInfo(register ClientPtr client)
{
    xXF86DRIGetDeviceInfoReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0
    };
    drm_handle_t hFrameBuffer;
    void *pDevPrivate;

    REQUEST(xXF86DRIGetDeviceInfoReq);
    REQUEST_SIZE_MATCH(xXF86DRIGetDeviceInfoReq);

    EPHYR_LOG("enter\n");
    if (stuff->screen >= screenInfo.numScreens) {
        client->errorValue = stuff->screen;
        return BadValue;
    }

    if (!ephyrDRIGetDeviceInfo(stuff->screen,
                               &hFrameBuffer,
                               (int *) &rep.framebufferOrigin,
                               (int *) &rep.framebufferSize,
                               (int *) &rep.framebufferStride,
                               (int *) &rep.devPrivateSize, &pDevPrivate)) {
        return BadValue;
    }

    rep.hFrameBufferLow = (CARD32) (hFrameBuffer & 0xffffffff);
#if defined(LONG64) && !defined(__linux__)
    rep.hFrameBufferHigh = (CARD32) (hFrameBuffer >> 32);
#else
    rep.hFrameBufferHigh = 0;
#endif

    if (rep.devPrivateSize) {
        rep.length = bytes_to_int32(SIZEOF(xXF86DRIGetDeviceInfoReply) -
                                    SIZEOF(xGenericReply) +
                                    pad_to_int32(rep.devPrivateSize));
    }

    WriteToClient(client, sizeof(xXF86DRIGetDeviceInfoReply), &rep);
    if (rep.length) {
        WriteToClient(client, rep.devPrivateSize, pDevPrivate);
    }
    EPHYR_LOG("leave\n");
    return Success;
}

static int
ProcXF86DRIDispatch(register ClientPtr client)
{
    REQUEST(xReq);
    EPHYR_LOG("enter\n");

    switch (stuff->data) {
    case X_XF86DRIQueryVersion:{
        EPHYR_LOG("leave\n");
        return ProcXF86DRIQueryVersion(client);
    }
    case X_XF86DRIQueryDirectRenderingCapable:{
        EPHYR_LOG("leave\n");
        return ProcXF86DRIQueryDirectRenderingCapable(client);
    }
    }

    if (!client->local)
        return DRIErrorBase + XF86DRIClientNotLocal;

    switch (stuff->data) {
    case X_XF86DRIOpenConnection:{
        EPHYR_LOG("leave\n");
        return ProcXF86DRIOpenConnection(client);
    }
    case X_XF86DRICloseConnection:{
        EPHYR_LOG("leave\n");
        return ProcXF86DRICloseConnection(client);
    }
    case X_XF86DRIGetClientDriverName:{
        EPHYR_LOG("leave\n");
        return ProcXF86DRIGetClientDriverName(client);
    }
    case X_XF86DRICreateContext:{
        EPHYR_LOG("leave\n");
        return ProcXF86DRICreateContext(client);
    }
    case X_XF86DRIDestroyContext:{
        EPHYR_LOG("leave\n");
        return ProcXF86DRIDestroyContext(client);
    }
    case X_XF86DRICreateDrawable:{
        EPHYR_LOG("leave\n");
        return ProcXF86DRICreateDrawable(client);
    }
    case X_XF86DRIDestroyDrawable:{
        EPHYR_LOG("leave\n");
        return ProcXF86DRIDestroyDrawable(client);
    }
    case X_XF86DRIGetDrawableInfo:{
        EPHYR_LOG("leave\n");
        return ProcXF86DRIGetDrawableInfo(client);
    }
    case X_XF86DRIGetDeviceInfo:{
        EPHYR_LOG("leave\n");
        return ProcXF86DRIGetDeviceInfo(client);
    }
    case X_XF86DRIAuthConnection:{
        EPHYR_LOG("leave\n");
        return ProcXF86DRIAuthConnection(client);
    }
        /* {Open,Close}FullScreen are deprecated now */
    default:{
        EPHYR_LOG("leave\n");
        return BadRequest;
    }
    }
}

static int
SProcXF86DRIQueryVersion(register ClientPtr client)
{
    REQUEST(xXF86DRIQueryVersionReq);
    swaps(&stuff->length);
    return ProcXF86DRIQueryVersion(client);
}

static int
SProcXF86DRIQueryDirectRenderingCapable(register ClientPtr client)
{
    REQUEST(xXF86DRIQueryDirectRenderingCapableReq);
    swaps(&stuff->length);
    swapl(&stuff->screen);
    return ProcXF86DRIQueryDirectRenderingCapable(client);
}

static int
SProcXF86DRIDispatch(register ClientPtr client)
{
    REQUEST(xReq);

    EPHYR_LOG("enter\n");
    /*
     * Only local clients are allowed DRI access, but remote clients still need
     * these requests to find out cleanly.
     */
    switch (stuff->data) {
    case X_XF86DRIQueryVersion:{
        EPHYR_LOG("leave\n");
        return SProcXF86DRIQueryVersion(client);
    }
    case X_XF86DRIQueryDirectRenderingCapable:{
        EPHYR_LOG("leave\n");
        return SProcXF86DRIQueryDirectRenderingCapable(client);
    }
    default:{
        EPHYR_LOG("leave\n");
        return DRIErrorBase + XF86DRIClientNotLocal;
    }
    }
}

Bool
ephyrDRIExtensionInit(ScreenPtr a_screen)
{
    Bool is_ok = FALSE;
    ExtensionEntry *extEntry = NULL;
    EphyrDRIScreenPrivPtr screen_priv = NULL;

    EPHYR_LOG("enter\n");
    if (!hostx_has_extension(&xcb_xf86dri_id)) {
        EPHYR_LOG("host does not have DRI extension\n");
        goto out;
    }
    EPHYR_LOG("host X does have DRI extension\n");
    if (!hostx_has_extension(&xcb_shape_id)) {
        EPHYR_LOG("host does not have XShape extension\n");
        goto out;
    }
    EPHYR_LOG("host X does have XShape extension\n");

#ifdef XF86DRI_EVENTS
    EventType = CreateNewResourceType(XF86DRIFreeEvents, "DRIEvents");
    if (!EventType) {
        EPHYR_LOG_ERROR("failed to register DRI event resource type\n");
        goto out;
    }
#endif

    if ((extEntry = AddExtension(XF86DRINAME,
                                 XF86DRINumberEvents,
                                 XF86DRINumberErrors,
                                 ProcXF86DRIDispatch,
                                 SProcXF86DRIDispatch,
                                 NULL, StandardMinorOpcode))) {
        DRIReqCode = (unsigned char) extEntry->base;
        DRIErrorBase = extEntry->errorBase;
    }
    else {
        EPHYR_LOG_ERROR("failed to register DRI extension\n");
        goto out;
    }
    if (!dixRegisterPrivateKey(&ephyrDRIScreenKeyRec, PRIVATE_SCREEN, 0))
        goto out;
    if (!dixRegisterPrivateKey(&ephyrDRIWindowKeyRec, PRIVATE_WINDOW, 0))
        goto out;
    screen_priv = calloc(1, sizeof(EphyrDRIScreenPrivRec));
    if (!screen_priv) {
        EPHYR_LOG_ERROR("failed to allocate screen_priv\n");
        goto out;
    }
    dixSetPrivate(&a_screen->devPrivates, ephyrDRIScreenKey, screen_priv);

    if (!ephyrDRIScreenInit(a_screen)) {
        EPHYR_LOG_ERROR("ephyrDRIScreenInit() failed\n");
        goto out;
    }
    EphyrMirrorHostVisuals(a_screen);
    is_ok = TRUE;
 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}
