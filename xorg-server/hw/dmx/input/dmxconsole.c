/*
 * Copyright 2001-2003 Red Hat Inc., Durham, North Carolina.
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
 *   David H. Dawes <dawes@xfree86.org>
 *   Kevin E. Martin <kem@redhat.com>
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 *
 * This file implements the console input devices.
 */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#define DMX_CONSOLE_DEBUG 0
#define DMX_WINDOW_DEBUG  0

#include "dmxinputinit.h"
#include "dmxevents.h"
#include "dmxconsole.h"
#include "dmxcommon.h"
#include "dmxscrinit.h"
#include "dmxcb.h"
#include "dmxsync.h"

#include "inputstr.h"
#include "input.h"
#include "mipointer.h"
#include "windowstr.h"

#define CONSOLE_NUM 3
#define CONSOLE_DEN 4
#define DMX_CONSOLE_NAME "DMX Console"
#define DMX_RES_NAME     "Xdmx"
#define DMX_RES_CLASS    "XDmx"
#define CONSOLE_BG_COLOR "gray75"
#define CONSOLE_FG_COLOR "black"
#define CONSOLE_SCREEN_BG_COLOR "white"
#define CONSOLE_SCREEN_FG_COLOR "black"
#define CONSOLE_SCREEN_DET_COLOR "gray75"
#define CONSOLE_SCREEN_CUR_COLOR "red"

#if DMX_CONSOLE_DEBUG
#define DMXDBG0(f)               dmxLog(dmxDebug,f)
#define DMXDBG1(f,a)             dmxLog(dmxDebug,f,a)
#define DMXDBG2(f,a,b)           dmxLog(dmxDebug,f,a,b)
#define DMXDBG3(f,a,b,c)         dmxLog(dmxDebug,f,a,b,c)
#define DMXDBG4(f,a,b,c,d)       dmxLog(dmxDebug,f,a,b,c,d)
#define DMXDBG5(f,a,b,c,d,e)     dmxLog(dmxDebug,f,a,b,c,d,e)
#define DMXDBG6(f,a,b,c,d,e,g)   dmxLog(dmxDebug,f,a,b,c,d,e,g)
#define DMXDBG7(f,a,b,c,d,e,g,h) dmxLog(dmxDebug,f,a,b,c,d,e,g,h)
#else
#define DMXDBG0(f)
#define DMXDBG1(f,a)
#define DMXDBG2(f,a,b)
#define DMXDBG3(f,a,b,c)
#define DMXDBG4(f,a,b,c,d)
#define DMXDBG5(f,a,b,c,d,e)
#define DMXDBG6(f,a,b,c,d,e,g)
#define DMXDBG7(f,a,b,c,d,e,g,h)
#endif

/* Private area for consoles. */
typedef struct _myPrivate {
    DMX_COMMON_PRIVATE;
    int lastX;
    int lastY;
    int globalX;
    int globalY;
    int curX;
    int curY;
    int width;
    int height;
    int consWidth;
    int consHeight;
    double xScale;
    double yScale;
    XlibGC gc, gcDet, gcRev, gcCur;
    int grabbed, fine, captured;
    Cursor cursorNormal, cursorGrabbed, cursorEmpty;
    Pixmap pixmap;

    CloseScreenProcPtr CloseScreen;
    struct _myPrivate *next;    /* for closing multiple consoles */
    int initialized;
    DevicePtr mou, kbd;
} myPrivate;

static int
scalex(myPrivate * priv, int x)
{
    return (int) ((x * priv->xScale) + .5);
}

static int
scaley(myPrivate * priv, int y)
{
    return (int) ((y * priv->yScale) + .5);
}

static int
unscalex(myPrivate * priv, int x)
{
    return (int) ((x / priv->xScale) + .5);
}

static int
unscaley(myPrivate * priv, int y)
{
    return (int) ((y / priv->yScale) + .5);
}

/** Create the private area for \a pDevice. */
void *
dmxConsoleCreatePrivate(DeviceIntPtr pDevice)
{
    GETDMXLOCALFROMPDEVICE;
    myPrivate *priv = calloc(1, sizeof(*priv));

    priv->dmxLocal = dmxLocal;
    return priv;
}

/** If \a private is non-NULL, free its associated memory. */
void
dmxConsoleDestroyPrivate(void *private)
{
    free(private);
}

static void
dmxConsoleDrawFineCursor(myPrivate * priv, XRectangle * rect)
{
    int size = 6;
    int x, y;

    XDrawLine(priv->display, priv->pixmap, priv->gcCur,
              x = scalex(priv, priv->globalX) - size,
              scaley(priv, priv->globalY),
              scalex(priv, priv->globalX) + size, scaley(priv, priv->globalY));
    XDrawLine(priv->display, priv->pixmap, priv->gcCur,
              scalex(priv, priv->globalX),
              y = scaley(priv, priv->globalY) - size,
              scalex(priv, priv->globalX), scaley(priv, priv->globalY) + size);
    if (priv->grabbed) {
        XDrawLine(priv->display, priv->pixmap, priv->gcCur,
                  scalex(priv, priv->globalX) - (int) (size / 1.4),
                  scaley(priv, priv->globalY) - (int) (size / 1.4),
                  scalex(priv, priv->globalX) + (int) (size / 1.4),
                  scaley(priv, priv->globalY) + (int) (size / 1.4));
        XDrawLine(priv->display, priv->pixmap, priv->gcCur,
                  scalex(priv, priv->globalX) - (int) (size / 1.4),
                  scaley(priv, priv->globalY) + (int) (size / 1.4),
                  scalex(priv, priv->globalX) + (int) (size / 1.4),
                  scaley(priv, priv->globalY) - (int) (size / 1.4));
    }
    if (rect) {
        rect->x = x;
        rect->y = y;
        rect->width = 2 * size;
        rect->height = 2 * size;
    }
}

static void
dmxConsoleDrawWindows(void *private)
{
    GETONLYPRIVFROMPRIVATE;
    Display *dpy = priv->display;
    int i;
    Region whole, used, avail;
    XRectangle rect;

    whole = XCreateRegion();
    used = XCreateRegion();
    avail = XCreateRegion();
    rect.x = 0;
    rect.y = 0;
    rect.width = priv->consWidth;
    rect.height = priv->consHeight;
    XUnionRectWithRegion(&rect, whole, whole);

    for (i = 0; i < dmxNumScreens; i++) {
        ScreenPtr pScreen = screenInfo.screens[i];
        WindowPtr pRoot = pScreen->root;
        WindowPtr pChild;

#if DMX_WINDOW_DEBUG
        dmxLog(dmxDebug, "%lu %p %p %p 2\n",
               pRoot->drawable.id,
               pRoot->parent, pRoot->firstChild, pRoot->lastChild);
#endif

        for (pChild = pRoot->firstChild; pChild; pChild = pChild->nextSib) {
            if (pChild->mapped && pChild->realized) {
#if DMX_WINDOW_DEBUG
                dmxLog(dmxDebug, "  %p %d,%d %dx%d %d %d  %d RECTS\n",
                       pChild,
                       pChild->drawable.x,
                       pChild->drawable.y,
                       pChild->drawable.width,
                       pChild->drawable.height,
                       pChild->visibility,
                       pChild->overrideRedirect,
                       RegionNumRects(&pChild->clipList));
#endif
                rect.x = scalex(priv, pChild->drawable.x + pScreen->x);
                rect.y = scaley(priv, pChild->drawable.y + pScreen->y);
                rect.width = scalex(priv, pChild->drawable.width);
                rect.height = scaley(priv, pChild->drawable.height);
                XDrawRectangle(dpy, priv->pixmap, priv->gc,
                               rect.x, rect.y, rect.width, rect.height);
                XUnionRectWithRegion(&rect, used, used);
                XSubtractRegion(whole, used, avail);
                XSetRegion(dpy, priv->gc, avail);
            }
        }
#ifdef PANORAMIX
        if (!noPanoramiXExtension)
            break;              /* Screen 0 valid with Xinerama */
#endif
    }
    XDestroyRegion(avail);
    XDestroyRegion(used);
    XDestroyRegion(whole);
    XSetClipMask(dpy, priv->gc, None);
}

static void
dmxConsoleDraw(myPrivate * priv, int updateCursor, int update)
{
    GETDMXINPUTFROMPRIV;
    Display *dpy = priv->display;
    int i;

    XFillRectangle(dpy, priv->pixmap, priv->gc, 0, 0,
                   priv->consWidth, priv->consHeight);

    for (i = 0; i < dmxNumScreens; i++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[i];

        XFillRectangle(dpy, priv->pixmap,
                       dmxScreen->beDisplay ? priv->gcRev : priv->gcDet,
                       scalex(priv, screenInfo.screens[i]->x),
                       scaley(priv, screenInfo.screens[i]->y),
                       scalex(priv, screenInfo.screens[i]->width),
                       scaley(priv, screenInfo.screens[i]->height));
    }
    for (i = 0; i < dmxNumScreens; i++) {
        XDrawRectangle(dpy, priv->pixmap, priv->gc,
                       scalex(priv, screenInfo.screens[i]->x),
                       scaley(priv, screenInfo.screens[i]->y),
                       scalex(priv, screenInfo.screens[i]->width),
                       scaley(priv, screenInfo.screens[i]->height));
    }
    if (dmxInput->windows)
        dmxConsoleDrawWindows(priv);
    if (priv->fine && updateCursor)
        dmxConsoleDrawFineCursor(priv, 0);
    if (update) {
        XCopyArea(priv->display, priv->pixmap, priv->window, priv->gc,
                  0, 0, priv->consWidth, priv->consHeight, 0, 0);
        XSync(priv->display, False);    /* Not a backend display */
    }
}

static void
dmxConsoleClearCursor(myPrivate * priv, int x, int y, XRectangle * rect)
{
    int cw = 14, ch = 14;       /* Clear width and height */

    rect->x = scalex(priv, x) - cw / 2;
    rect->y = scaley(priv, y) - ch / 2;
    rect->width = cw;
    rect->height = ch;
    XSetClipRectangles(priv->display, priv->gc, 0, 0, rect, 1, Unsorted);
    XSetClipRectangles(priv->display, priv->gcDet, 0, 0, rect, 1, Unsorted);
    XSetClipRectangles(priv->display, priv->gcRev, 0, 0, rect, 1, Unsorted);
    dmxConsoleDraw(priv, 0, 0);
    XSetClipMask(priv->display, priv->gc, None);
    XSetClipMask(priv->display, priv->gcDet, None);
    XSetClipMask(priv->display, priv->gcRev, None);
}

static void
dmxConsoleUpdateFineCursor(myPrivate * priv)
{
    int leave = 0;
    XRectangle rects[2];

    dmxConsoleClearCursor(priv, priv->globalX, priv->globalY, &rects[0]);
    if (priv->dmxLocal->sendsCore) {
        dmxGetGlobalPosition(&priv->globalX, &priv->globalY);
    }
    else {
        priv->globalX = priv->dmxLocal->lastX;
        priv->globalY = priv->dmxLocal->lastY;
    }

    priv->lastX = scalex(priv, priv->width / 2);
    priv->lastY = scaley(priv, priv->height / 2);

    /* Compute new warp position, which may be
       outside the window */
    if (priv->globalX < 1 || priv->globalX >= priv->width) {
        if (priv->globalX < 1)
            priv->lastX = 0;
        else
            priv->lastX = scalex(priv, priv->width);
        priv->lastY = scaley(priv, priv->globalY);
        ++leave;
    }
    if (priv->globalY < 1 || priv->globalY >= priv->height) {
        if (priv->globalY < 1)
            priv->lastY = 0;
        else
            priv->lastY = scaley(priv, priv->height);
        priv->lastX = scalex(priv, priv->globalX);
        ++leave;
    }

    /* Draw pseudo cursor in window */
    dmxConsoleDrawFineCursor(priv, &rects[1]);

    XSetClipRectangles(priv->display, priv->gc, 0, 0, rects, 2, Unsorted);
    XCopyArea(priv->display, priv->pixmap, priv->window, priv->gc,
              0, 0, priv->consWidth, priv->consHeight, 0, 0);
    XSetClipMask(priv->display, priv->gc, None);

    DMXDBG2("dmxConsoleUpdateFineCursor: WARP %d %d\n",
            priv->lastX, priv->lastY);
    XWarpPointer(priv->display, priv->window, priv->window,
                 0, 0, 0, 0, priv->lastX, priv->lastY);
    XSync(priv->display, False);        /* Not a backend display */

    if (leave) {
        XEvent X;

        while (XCheckMaskEvent(priv->display, PointerMotionMask, &X)) {
            if (X.type == MotionNotify) {
                if (X.xmotion.x != priv->lastX || X.xmotion.y != priv->lastY) {
                    DMXDBG4("Ignoring motion to %d %d after leave frm %d %d\n",
                            X.xmotion.x, X.xmotion.y, priv->lastX, priv->lastY);
                }
            }
            else {
                dmxLog(dmxInfo, "Ignoring event (%d): %s ****************\n",
                       X.type, dmxEventName(X.type));
            }
        }
    }
    DMXDBG6("dmxConsoleUpdateFineCursor: Warp %d %d on %d %d [%d %d]\n",
            priv->lastX, priv->lastY,
            scalex(priv, priv->width),
            scaley(priv, priv->height), priv->globalX, priv->globalY);
}

/** Whenever the window layout (size, position, stacking order) might be
 * changed, this routine is called with the \a pWindow that changed and
 * the \a type of change.  This routine is called in a conservative
 * fashion: the actual layout of the windows of the screen might not
 * have had any human-visible changes. */
void
dmxConsoleUpdateInfo(void *private, DMXUpdateType type, WindowPtr pWindow)
{
    GETONLYPRIVFROMPRIVATE;
    dmxConsoleDraw(priv, 1, 1);
}

static void
dmxConsoleMoveAbsolute(myPrivate * priv, int x, int y,
                       DevicePtr pDev, dmxMotionProcPtr motion,
                       DMXBlockType block)
{
    int tmpX, tmpY, v[2];

    tmpX = unscalex(priv, x);
    tmpY = unscalex(priv, y);
    DMXDBG6("dmxConsoleMoveAbsolute(,%d,%d) %d %d =? %d %d\n",
            x, y, tmpX, tmpY, priv->curX, priv->curY);
    if (tmpX == priv->curX && tmpY == priv->curY)
        return;
    v[0] = unscalex(priv, x);
    v[1] = unscaley(priv, y);
    motion(pDev, v, 0, 2, DMX_ABSOLUTE_CONFINED, block);
    /* dmxConsoleUpdatePosition gets called here by dmxCoreMotion */
}

static void
dmxConsoleMoveRelative(myPrivate * priv, int x, int y,
                       DevicePtr pDev, dmxMotionProcPtr motion,
                       DMXBlockType block)
{
    int v[2];

    /* Ignore the event generated from * warping back to middle */
    if (x == priv->lastX && y == priv->lastY)
        return;
    v[0] = priv->lastX - x;
    v[1] = priv->lastY - y;
    motion(pDev, v, 0, 2, DMX_RELATIVE, block);
    /* dmxConsoleUpdatePosition gets called here by dmxCoreMotion */
}

/** This routine gets called from #dmxCoreMotion for each motion.  This
 * allows the console's notion of the cursor postion to change when
 * another input device actually caused the change. */
void
dmxConsoleUpdatePosition(void *private, int x, int y)
{
    GETONLYPRIVFROMPRIVATE;
    int tmpX, tmpY;
    Display *dpy = priv->display;
    static unsigned long dmxGeneration = 0;

    tmpX = scalex(priv, x);
    tmpY = scaley(priv, y);
    DMXDBG6("dmxConsoleUpdatePosition(,%d,%d) new=%d,%d dims=%d,%d\n",
            x, y, tmpX, tmpY, priv->consWidth, priv->consHeight);

    if (priv->fine)
        dmxConsoleUpdateFineCursor(priv);
    if (tmpX != priv->curX || tmpY != priv->curY) {
        if (tmpX < 0)
            tmpX = 0;
        if (tmpY < 0)
            tmpY = 0;
        if (tmpX >= priv->consWidth)
            tmpX = priv->consWidth - 1;
        if (tmpY >= priv->consHeight)
            tmpY = priv->consHeight - 1;
        priv->curX = tmpX;
        priv->curY = tmpY;
        if (!priv->fine) {
            DMXDBG2("   WARP B %d %d\n", priv->curX, priv->curY);
            XWarpPointer(dpy, priv->window,
                         priv->window, 0, 0, 0, 0, tmpX, tmpY);
            XSync(dpy, False);  /* Not a backend display */
        }
    }

    if (dmxGeneration != serverGeneration) {
        dmxGeneration = serverGeneration;
        dmxConsoleDraw(priv, 1, 1);
    }
}

/** Collect all pending events from the console's display.  Plase these
 * events on the server event queue using the \a motion and \a enqueue
 * routines.  The \a checkspecial routine is used to check for special
 * keys that need handling.  \a block tells if signals should be blocked
 * when updating the event queue. */
void
dmxConsoleCollectEvents(DevicePtr pDev,
                        dmxMotionProcPtr motion,
                        dmxEnqueueProcPtr enqueue,
                        dmxCheckSpecialProcPtr checkspecial, DMXBlockType block)
{
    GETPRIVFROMPDEV;
    GETDMXINPUTFROMPRIV;
    Display *dpy = priv->display;
    Window win = priv->window;
    int width = priv->width;
    int height = priv->height;
    XEvent X, N;
    XSetWindowAttributes attribs;
    static int rInitialized = 0;
    static Region r;
    XRectangle rect;
    static int raising = 0, raiseX, raiseY;     /* FIXME */

    while (XPending(dpy)) {
        XNextEvent(dpy, &X);
        switch (X.type) {
        case VisibilityNotify:
            break;
        case Expose:
            DMXDBG5("dmxConsoleCollectEvents: Expose #%d %d %d %d %d\n",
                    X.xexpose.count,
                    X.xexpose.x, X.xexpose.y,
                    X.xexpose.width, X.xexpose.height);
            if (!rInitialized++)
                r = XCreateRegion();
            rect.x = X.xexpose.x;
            rect.y = X.xexpose.y;
            rect.width = X.xexpose.width;
            rect.height = X.xexpose.height;
            XUnionRectWithRegion(&rect, r, r);
            if (X.xexpose.count == 0) {
                XSetRegion(dpy, priv->gc, r);
                XSetRegion(dpy, priv->gcDet, r);
                XSetRegion(dpy, priv->gcRev, r);
                dmxConsoleDraw(priv, 1, 1);
                XSetClipMask(dpy, priv->gc, None);
                XSetClipMask(dpy, priv->gcDet, None);
                XSetClipMask(dpy, priv->gcRev, None);
                XDestroyRegion(r);
                rInitialized = 0;
            }
            break;
        case ResizeRequest:
            DMXDBG2("dmxConsoleCollectEvents: Resize %d %d\n",
                    X.xresizerequest.width, X.xresizerequest.height);
            priv->consWidth = X.xresizerequest.width;
            priv->consHeight = X.xresizerequest.height;
            priv->xScale = (double) priv->consWidth / width;
            priv->yScale = (double) priv->consHeight / height;
            attribs.override_redirect = True;
            XChangeWindowAttributes(dpy, win, CWOverrideRedirect, &attribs);
            XResizeWindow(dpy, win, priv->consWidth, priv->consHeight);
            XFreePixmap(dpy, priv->pixmap);
            priv->pixmap = XCreatePixmap(dpy,
                                         RootWindow(dpy, DefaultScreen(dpy)),
                                         priv->consWidth,
                                         priv->consHeight,
                                         DefaultDepth(dpy, DefaultScreen(dpy)));
            dmxConsoleDraw(priv, 1, 1);
            attribs.override_redirect = False;
            XChangeWindowAttributes(dpy, win, CWOverrideRedirect, &attribs);
            break;
        case LeaveNotify:
            DMXDBG4("dmxConsoleCollectEvents: Leave @ %d,%d; r=%d f=%d\n",
                    X.xcrossing.x, X.xcrossing.y, raising, priv->fine);
            if (!priv->captured)
                dmxCommonRestoreState(priv);
            else {
                dmxConsoleUncapture(dmxInput);
                dmxCommonRestoreState(priv);
            }
            break;
        case EnterNotify:
            DMXDBG6("dmxConsoleCollectEvents: Enter %d,%d r=%d f=%d (%d,%d)\n",
                    X.xcrossing.x, X.xcrossing.y, raising, priv->fine,
                    priv->curX, priv->curY);
            dmxCommonSaveState(priv);
            if (raising) {
                raising = 0;
                dmxConsoleMoveAbsolute(priv, raiseX, raiseY,
                                       priv->mou, motion, block);
            }
            else {
                if (priv->fine) {
                    /* The raise will generate an event near the center,
                     * which is not where the cursor should be.  So we
                     * save the real position, do the raise, and move
                     * the cursor here again after the raise generates
                     * the event. */
                    raising = 1;
                    raiseX = X.xcrossing.x;
                    raiseY = X.xcrossing.y;
                    XRaiseWindow(dpy, priv->window);
                }
                XSync(dpy, False);      /* Not a backend display */
                if (!X.xcrossing.x && !X.xcrossing.y)
                    dmxConsoleMoveAbsolute(priv, priv->curX, priv->curY,
                                           priv->mou, motion, block);
            }
            break;
        case MotionNotify:
            if (priv->curX == X.xmotion.x && priv->curY == X.xmotion.y)
                continue;
            if (XPending(dpy)) {        /* do motion compression */
                XPeekEvent(dpy, &N);
                if (N.type == MotionNotify)
                    continue;
            }
            DMXDBG2("dmxConsoleCollectEvents: Motion %d %d\n",
                    X.xmotion.x, X.xmotion.y);
            if (raising) {
                raising = 0;
                dmxConsoleMoveAbsolute(priv, raiseX, raiseY,
                                       priv->mou, motion, block);
            }
            else {
                if (priv->fine)
                    dmxConsoleMoveRelative(priv, X.xmotion.x, X.xmotion.y,
                                           priv->mou, motion, block);
                else
                    dmxConsoleMoveAbsolute(priv, X.xmotion.x, X.xmotion.y,
                                           priv->mou, motion, block);
            }
            break;
        case KeyPress:
        case KeyRelease:
            enqueue(priv->kbd, X.type, X.xkey.keycode, 0, NULL, block);
            break;
        default:
            /* Pass the whole event here, because
             * this may be an extension event. */
            enqueue(priv->mou, X.type, X.xbutton.button, 0, &X, block);
            break;
        }
    }
}

static void
dmxCloseConsole(myPrivate * priv)
{
    GETDMXINPUTFROMPRIV;
    dmxCommonRestoreState(priv);
    if (priv->display) {
        XFreeGC(priv->display, priv->gc);
        XFreeGC(priv->display, priv->gcDet);
        XFreeGC(priv->display, priv->gcRev);
        XFreeGC(priv->display, priv->gcCur);
        if (!dmxInput->console)
            XCloseDisplay(priv->display);
    }
    priv->display = NULL;
}

static Bool
dmxCloseConsoleScreen(ScreenPtr pScreen)
{
    myPrivate *priv, *last;

    for (last = priv = (myPrivate *) dixLookupPrivate(&pScreen->devPrivates,
                                                      dmxScreenPrivateKey);
         priv; priv = priv->next)
        dmxCloseConsole(last = priv);

    DMX_UNWRAP(CloseScreen, last, pScreen);
    return pScreen->CloseScreen(pScreen);
}

static Cursor
dmxConsoleCreateEmptyCursor(myPrivate * priv)
{
    char noCursorData[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    Pixmap pixmap;
    Cursor cursor;
    XColor color, tmpColor;
    Display *dpy = priv->display;

    /* Create empty cursor for window */
    pixmap = XCreateBitmapFromData(priv->display, priv->window,
                                   noCursorData, 8, 8);
    if (!XAllocNamedColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)),
                          "black", &color, &tmpColor))
        dmxLog(dmxFatal, "Cannot allocate color for cursor\n");
    cursor = XCreatePixmapCursor(dpy, pixmap, pixmap, &color, &color, 0, 0);
    XFreePixmap(dpy, pixmap);
    return cursor;
}

static void
dmxConsoleComputeWidthHeight(myPrivate * priv,
                             int *width, int *height,
                             double *xScale, double *yScale,
                             int *consWidth, int *consHeight)
{
    int screen;
    Display *dpy = priv->display;

    *width = 0;
    *height = 0;
    *xScale = 1.0;
    *yScale = 1.0;

    screen = DefaultScreen(dpy);
    *consWidth = DisplayWidth(dpy, screen) * CONSOLE_NUM / CONSOLE_DEN;
    *consHeight = DisplayHeight(dpy, screen) * CONSOLE_NUM / CONSOLE_DEN;

    if (*consWidth < 1)
        *consWidth = 1;
    if (*consHeight < 1)
        *consHeight = 1;

#if 1
    /* Always keep the console size similar
     * to the global bounding box. */
    *width = dmxGlobalWidth;
    *height = dmxGlobalHeight;
#else
    /* Make the console window as big as
     * possible by computing the visible
     * bounding box. */
    for (i = 0; i < dmxNumScreens; i++) {
        if (screenInfo.screens[i]->x + screenInfo.screens[i]->width > *width)
            *width = screenInfo.screens[i]->x + screenInfo.screens[i]->width;

        if (screenInfo.screens[i]->y + screenInfo.screens[i]->height > *height)
            *height = screenInfo.screens[i]->y + screenInfo.screens[i]->height;
    }
#endif

    if ((double) *consWidth / *width < (double) *consHeight / *height)
        *xScale = *yScale = (double) *consWidth / *width;
    else
        *xScale = *yScale = (double) *consHeight / *height;

    *consWidth = scalex(priv, *width);
    *consHeight = scaley(priv, *height);
    if (*consWidth < 1)
        *consWidth = 1;
    if (*consHeight < 1)
        *consHeight = 1;
}

/** Re-initialized the console device described by \a pDev (after a
 * reconfig). */
void
dmxConsoleReInit(DevicePtr pDev)
{
    GETPRIVFROMPDEV;
    Display *dpy;

    if (!priv || !priv->initialized)
        return;
    dpy = priv->display;

    dmxConsoleComputeWidthHeight(priv,
                                 &priv->width, &priv->height,
                                 &priv->xScale, &priv->yScale,
                                 &priv->consWidth, &priv->consHeight);
    XResizeWindow(dpy, priv->window, priv->consWidth, priv->consHeight);
    XFreePixmap(dpy, priv->pixmap);
    priv->pixmap = XCreatePixmap(dpy,
                                 RootWindow(dpy, DefaultScreen(dpy)),
                                 priv->consWidth,
                                 priv->consHeight,
                                 DefaultDepth(dpy, DefaultScreen(dpy)));
    dmxConsoleDraw(priv, 1, 1);
}

/** Initialized the console device described by \a pDev. */
void
dmxConsoleInit(DevicePtr pDev)
{
    GETPRIVFROMPDEV;
    DMXInputInfo *dmxInput = &dmxInputs[dmxLocal->inputIdx];
    int screen;
    unsigned long mask;
    XSetWindowAttributes attribs;
    Display *dpy;
    Window win;
    XGCValues gcvals;
    XColor color;
    XClassHint class_hints;
    unsigned long tmp;

    if (dmxLocal->type == DMX_LOCAL_MOUSE)
        priv->mou = pDev;
    if (dmxLocal->type == DMX_LOCAL_KEYBOARD)
        priv->kbd = pDev;
    if (priv->initialized++)
        return;                 /* Only do once for mouse/keyboard pair */

    if (!(dpy = priv->display = XOpenDisplay(dmxInput->name)))
        dmxLog(dmxFatal,
               "dmxOpenConsole: cannot open console display %s\n",
               dmxInput->name);

    /* Set up defaults */
    dmxConsoleComputeWidthHeight(priv,
                                 &priv->width, &priv->height,
                                 &priv->xScale, &priv->yScale,
                                 &priv->consWidth, &priv->consHeight);

    /* Private initialization using computed values or constants. */
    screen = DefaultScreen(dpy);
    priv->initPointerX = scalex(priv, priv->width / 2);
    priv->initPointerY = scaley(priv, priv->height / 2);
    priv->eventMask = (ButtonPressMask
                       | ButtonReleaseMask
                       | PointerMotionMask
                       | EnterWindowMask
                       | LeaveWindowMask
                       | KeyPressMask
                       | KeyReleaseMask | ExposureMask | ResizeRedirectMask);

    mask = CWBackPixel | CWEventMask | CWColormap | CWOverrideRedirect;
    attribs.colormap = DefaultColormap(dpy, screen);
    if (XParseColor(dpy, attribs.colormap, CONSOLE_BG_COLOR, &color)
        && XAllocColor(dpy, attribs.colormap, &color)) {
        attribs.background_pixel = color.pixel;
    }
    else
        attribs.background_pixel = WhitePixel(dpy, screen);

    attribs.event_mask = priv->eventMask;
    attribs.override_redirect = False;

    win = priv->window = XCreateWindow(dpy,
                                       RootWindow(dpy, screen),
                                       0, 0, priv->consWidth, priv->consHeight,
                                       0,
                                       DefaultDepth(dpy, screen),
                                       InputOutput,
                                       DefaultVisual(dpy, screen),
                                       mask, &attribs);
    priv->pixmap = XCreatePixmap(dpy, RootWindow(dpy, screen),
                                 priv->consWidth, priv->consHeight,
                                 DefaultDepth(dpy, screen));

    /* Set up properties */
    XStoreName(dpy, win, DMX_CONSOLE_NAME);
    class_hints.res_name = (char *) DMX_RES_NAME;
    class_hints.res_class = (char *) DMX_RES_CLASS;
    XSetClassHint(dpy, win, &class_hints);

    /* Map the window */
    XMapWindow(dpy, win);

    /* Create cursors */
    priv->cursorNormal = XCreateFontCursor(dpy, XC_circle);
    priv->cursorGrabbed = XCreateFontCursor(dpy, XC_spider);
    priv->cursorEmpty = dmxConsoleCreateEmptyCursor(priv);
    XDefineCursor(dpy, priv->window, priv->cursorNormal);

    /* Create GC */
    mask = (GCFunction | GCPlaneMask | GCClipMask | GCForeground |
            GCBackground | GCLineWidth | GCLineStyle | GCCapStyle |
            GCFillStyle | GCGraphicsExposures);
    gcvals.function = GXcopy;
    gcvals.plane_mask = AllPlanes;
    gcvals.clip_mask = None;
    if (XParseColor(dpy, attribs.colormap, CONSOLE_SCREEN_FG_COLOR, &color)
        && XAllocColor(dpy, attribs.colormap, &color)) {
        gcvals.foreground = color.pixel;
    }
    else
        gcvals.foreground = BlackPixel(dpy, screen);
    if (XParseColor(dpy, attribs.colormap, CONSOLE_SCREEN_BG_COLOR, &color)
        && XAllocColor(dpy, attribs.colormap, &color)) {
        gcvals.background = color.pixel;
    }
    else
        gcvals.background = WhitePixel(dpy, screen);
    gcvals.line_width = 0;
    gcvals.line_style = LineSolid;
    gcvals.cap_style = CapNotLast;
    gcvals.fill_style = FillSolid;
    gcvals.graphics_exposures = False;

    priv->gc = XCreateGC(dpy, win, mask, &gcvals);

    tmp = gcvals.foreground;
    if (XParseColor(dpy, attribs.colormap, CONSOLE_SCREEN_DET_COLOR, &color)
        && XAllocColor(dpy, attribs.colormap, &color)) {
        gcvals.foreground = color.pixel;
    }
    else
        gcvals.foreground = BlackPixel(dpy, screen);
    priv->gcDet = XCreateGC(dpy, win, mask, &gcvals);
    gcvals.foreground = tmp;

    tmp = gcvals.background;
    gcvals.background = gcvals.foreground;
    gcvals.foreground = tmp;
    priv->gcRev = XCreateGC(dpy, win, mask, &gcvals);

    gcvals.background = gcvals.foreground;
    if (XParseColor(dpy, attribs.colormap, CONSOLE_SCREEN_CUR_COLOR, &color)
        && XAllocColor(dpy, attribs.colormap, &color)) {
        gcvals.foreground = color.pixel;
    }
    else
        gcvals.foreground = BlackPixel(dpy, screen);
    priv->gcCur = XCreateGC(dpy, win, mask, &gcvals);

    dmxConsoleDraw(priv, 1, 1);

    if (dixLookupPrivate(&screenInfo.screens[0]->devPrivates,
                         dmxScreenPrivateKey))
        priv->next = dixLookupPrivate(&screenInfo.screens[0]->devPrivates,
                                      dmxScreenPrivateKey);
    else
        DMX_WRAP(CloseScreen, dmxCloseConsoleScreen,
                 priv, screenInfo.screens[0]);
    dixSetPrivate(&screenInfo.screens[0]->devPrivates, dmxScreenPrivateKey,
                  priv);
}

/** Fill in the \a info structure for the specified \a pDev.  Only used
 * for pointers. */
void
dmxConsoleMouGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info)
{
    GETPRIVFROMPDEV;

    info->buttonClass = 1;
    dmxCommonMouGetMap(pDev, info->map, &info->numButtons);
    info->valuatorClass = 1;
    info->numRelAxes = 2;
    info->minval[0] = 0;
    info->minval[1] = 0;
    /* max possible console window size: */
    info->maxval[0] = DisplayWidth(priv->display, DefaultScreen(priv->display));
    info->maxval[1] =
        DisplayHeight(priv->display, DefaultScreen(priv->display));
    info->res[0] = 1;
    info->minres[0] = 0;
    info->maxres[0] = 1;
    info->ptrFeedbackClass = 1;
}

/** Fill in the \a info structure for the specified \a pDev.  Only used
 * for keyboard. */
void
dmxConsoleKbdGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info)
{
    dmxCommonKbdGetInfo(pDev, info);
    info->keyboard = 1;
    info->keyClass = 1;
    dmxCommonKbdGetMap(pDev, &info->keySyms, info->modMap);
    info->freemap = 1;
    info->focusClass = 1;
    info->kbdFeedbackClass = 1;
}

/** Handle special console-only keys. */
int
dmxConsoleFunctions(void *private, DMXFunctionType function)
{
    GETONLYPRIVFROMPRIVATE;
    XRectangle rect;
    Display *dpy = priv->display;

    switch (function) {
    case DMX_FUNCTION_FINE:
        if (priv->fine) {
            priv->fine = 0;
            dmxConsoleClearCursor(priv, priv->globalX, priv->globalY, &rect);
            XSetClipRectangles(dpy, priv->gc, 0, 0, &rect, 1, Unsorted);
            XCopyArea(dpy, priv->pixmap, priv->window, priv->gc,
                      0, 0, priv->consWidth, priv->consHeight, 0, 0);
            XSetClipMask(dpy, priv->gc, None);

            XDefineCursor(dpy, priv->window,
                          priv->grabbed
                          ? priv->cursorGrabbed : priv->cursorNormal);
            XWarpPointer(dpy, priv->window, priv->window,
                         0, 0, 0, 0,
                         scalex(priv, priv->globalX),
                         scaley(priv, priv->globalY));
            XSync(dpy, False);  /* Not a backend display */
        }
        else {
            priv->fine = 1;
            XRaiseWindow(dpy, priv->window);
            XDefineCursor(dpy, priv->window, priv->cursorEmpty);
            dmxConsoleUpdateFineCursor(priv);
        }
        return 1;
    case DMX_FUNCTION_GRAB:
        if (priv->grabbed) {
            XUngrabKeyboard(dpy, CurrentTime);
            XUngrabPointer(dpy, CurrentTime);
            XDefineCursor(dpy, priv->window,
                          priv->fine ? priv->cursorEmpty : priv->cursorNormal);
        }
        else {
            if (XGrabPointer(dpy, priv->window, True,
                             0, GrabModeAsync, GrabModeAsync, priv->window,
                             None, CurrentTime)) {
                dmxLog(dmxError, "XGrabPointer failed\n");
                return 0;
            }
            if (XGrabKeyboard(dpy, priv->window, True,
                              GrabModeAsync, GrabModeAsync, CurrentTime)) {
                dmxLog(dmxError, "XGrabKeyboard failed\n");
                XUngrabPointer(dpy, CurrentTime);
                return 0;
            }
            XDefineCursor(dpy, priv->window,
                          priv->fine ? priv->cursorEmpty : priv->cursorGrabbed);
        }
        priv->grabbed = !priv->grabbed;
        if (priv->fine)
            dmxConsoleUpdateFineCursor(priv);
        return 1;
    case DMX_FUNCTION_TERMINATE:
        return 1;
    default:
        return 0;
    }
}

static void
dmxDump(void)
{
    int i, j;
    DMXInputInfo *dmxInput;
    XEvent X;

    for (i = 0, dmxInput = &dmxInputs[0]; i < dmxNumInputs; i++, dmxInput++) {
        for (j = 0; j < dmxInput->numDevs; j++) {
            DMXLocalInputInfoPtr dmxLocal = dmxInput->devs[j];
            myPrivate *priv = dmxLocal->private;

            while (priv
                   && priv->display
                   && XCheckTypedEvent(priv->display, MotionNotify, &X)) {
                DMXDBG4("dmxDump: %s/%d threw event away %d %s\n",
                        dmxInput->name, j, X.type, dmxEventName(X.type));
            }
        }
    }
}

/** This routine is used to warp the pointer into the console window
 * from anywhere on the screen.  It is used when backend and console
 * input are both being taken from the same X display. */
void
dmxConsoleCapture(DMXInputInfo * dmxInput)
{
    int i;
    XEvent X;

    DMXDBG0("dmxConsoleCapture\n");
    dmxSync(NULL, TRUE);
    for (i = 0; i < dmxInput->numDevs; i++) {
        DMXLocalInputInfoPtr dmxLocal = dmxInput->devs[i];
        myPrivate *priv = dmxLocal->private;

        if (dmxLocal->extType != DMX_LOCAL_TYPE_CONSOLE)
            continue;
        if (dmxLocal->type != DMX_LOCAL_MOUSE)
            continue;
        if (priv->captured)
            continue;
        priv->captured = 2;     /* Ungrab only after proximal events. */
        XRaiseWindow(priv->display, priv->window);
        XSync(priv->display, False);    /* Not a backend display */
        while (XCheckTypedEvent(priv->display, MotionNotify, &X)) {
            DMXDBG3("   Ignoring motion to %d %d after capture on %s\n",
                    X.xmotion.x, X.xmotion.y, dmxInput->name);
        }
        XWarpPointer(priv->display, None,
                     priv->window, 0, 0, 0, 0, priv->curX, priv->curY);
        XSync(priv->display, False);    /* Not a backend display */
        dmxDump();
        if (priv->fine)
            dmxConsoleUpdateFineCursor(priv);
    }
}

/** Undo the capture that was done by #dmxConsoleCapture. */
void
dmxConsoleUncapture(DMXInputInfo * dmxInput)
{
    int i;

    DMXDBG0("dmxConsoleUncapture\n");
    dmxSync(NULL, TRUE);
    for (i = 0; i < dmxInput->numDevs; i++) {
        DMXLocalInputInfoPtr dmxLocal = dmxInput->devs[i];
        myPrivate *priv = dmxLocal->private;

        if (dmxLocal->extType != DMX_LOCAL_TYPE_CONSOLE)
            continue;
        if (dmxLocal->type != DMX_LOCAL_MOUSE)
            continue;
        if (!priv->captured)
            continue;
        priv->captured = 0;
        XSync(priv->display, False);    /* Not a backend display */
    }
}
