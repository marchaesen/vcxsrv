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
 */

/** \file
 * These routines support taking input from devices on the backend
 * (output) displays.  \see dmxcommon.c. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#define DMX_BACKEND_DEBUG 0

#include "dmxinputinit.h"
#include "dmxbackend.h"
#include "dmxcommon.h"
#include "dmxconsole.h"
#include "dmxcursor.h"
#include "dmxprop.h"
#include "dmxsync.h"
#include "dmxcb.h"              /* For dmxGlobalWidth and dmxGlobalHeight */
#include "dmxevents.h"          /* For dmxGetGlobalPosition */
#include "ChkNotMaskEv.h"

#include "inputstr.h"
#include "input.h"
#include <X11/keysym.h>
#include "mipointer.h"
#include "scrnintstr.h"
#include "windowstr.h"

/* Private area for backend devices. */
typedef struct _myPrivate {
    DMX_COMMON_PRIVATE;
    int myScreen;
    DMXScreenInfo *grabbedScreen;

    int lastX, lastY;
    int centerX, centerY;
    int relative;
    int newscreen;
    int initialized;
    DevicePtr mou, kbd;
    int entered;
    int offX, offY;
} myPrivate;

#if DMX_BACKEND_DEBUG
#define DMXDBG0(f)                   dmxLog(dmxDebug,f)
#define DMXDBG1(f,a)                 dmxLog(dmxDebug,f,a)
#define DMXDBG2(f,a,b)               dmxLog(dmxDebug,f,a,b)
#define DMXDBG3(f,a,b,c)             dmxLog(dmxDebug,f,a,b,c)
#define DMXDBG4(f,a,b,c,d)           dmxLog(dmxDebug,f,a,b,c,d)
#define DMXDBG5(f,a,b,c,d,e)         dmxLog(dmxDebug,f,a,b,c,d,e)
#define DMXDBG6(f,a,b,c,d,e,g)       dmxLog(dmxDebug,f,a,b,c,d,e,g)
#define DMXDBG7(f,a,b,c,d,e,g,h)     dmxLog(dmxDebug,f,a,b,c,d,e,g,h)
#define DMXDBG8(f,a,b,c,d,e,g,h,i)   dmxLog(dmxDebug,f,a,b,c,d,e,g,h,i)
#define DMXDBG9(f,a,b,c,d,e,g,h,i,j) dmxLog(dmxDebug,f,a,b,c,d,e,g,h,i,j)
#else
#define DMXDBG0(f)
#define DMXDBG1(f,a)
#define DMXDBG2(f,a,b)
#define DMXDBG3(f,a,b,c)
#define DMXDBG4(f,a,b,c,d)
#define DMXDBG5(f,a,b,c,d,e)
#define DMXDBG6(f,a,b,c,d,e,g)
#define DMXDBG7(f,a,b,c,d,e,g,h)
#define DMXDBG8(f,a,b,c,d,e,g,h,i)
#define DMXDBG9(f,a,b,c,d,e,g,h,i,j)
#endif

/** Create and return a private data structure. */
void *
dmxBackendCreatePrivate(DeviceIntPtr pDevice)
{
    GETDMXLOCALFROMPDEVICE;
    myPrivate *priv = calloc(1, sizeof(*priv));

    priv->dmxLocal = dmxLocal;
    return priv;
}

/** Destroy the private data structure.  No checking is performed to
 * verify that the structure was actually created by
 * #dmxBackendCreatePrivate. */
void
dmxBackendDestroyPrivate(void *private)
{
    free(private);
}

static void *
dmxBackendTestScreen(DMXScreenInfo * dmxScreen, void *closure)
{
    long target = (long) closure;

    if (dmxScreen->index == target)
        return dmxScreen;
    return NULL;
}

/* Return non-zero if screen and priv->myScreen are on the same physical
 * backend display (1 if they are the same screen, 2 if they are
 * different screens).  Since this is a common operation, the results
 * are cached.  The cache is invalidated if \a priv is NULL (this should
 * be done with each server generation and reconfiguration). */
static int
dmxBackendSameDisplay(myPrivate * priv, long screen)
{
    static myPrivate *oldpriv = NULL;
    static int oldscreen = -1;
    static int retcode = 0;

    if (priv == oldpriv && screen == oldscreen)
        return retcode;
    if (!priv) {                /* Invalidate cache */
        oldpriv = NULL;
        oldscreen = -1;
        retcode = 0;
        return 0;
    }

    if (screen == priv->myScreen)
        retcode = 1;
    else if (screen < 0 || screen >= dmxNumScreens)
        retcode = 0;
    else if (dmxPropertyIterate(priv->be,
                                dmxBackendTestScreen, (void *) screen))
        retcode = 2;
    else
        retcode = 0;

    oldpriv = priv;
    oldscreen = screen;
    return retcode;
}

static void *
dmxBackendTestEvents(DMXScreenInfo * dmxScreen, void *closure)
{
    XEvent *X = (XEvent *) closure;

    if (XCheckNotMaskEvent(dmxScreen->beDisplay, ExposureMask, X))
        return dmxScreen;
    return NULL;
}

static void *
dmxBackendTestMotionEvent(DMXScreenInfo * dmxScreen, void *closure)
{
    XEvent *X = (XEvent *) closure;

    if (XCheckTypedEvent(dmxScreen->beDisplay, MotionNotify, X))
        return dmxScreen;
    return NULL;
}

static DMXScreenInfo *
dmxBackendGetEvent(myPrivate * priv, XEvent * X)
{
    DMXScreenInfo *dmxScreen;

    if ((dmxScreen = dmxPropertyIterate(priv->be, dmxBackendTestEvents, X)))
        return dmxScreen;
    return NULL;
}

static DMXScreenInfo *
dmxBackendPendingMotionEvent(myPrivate * priv, int save)
{
    DMXScreenInfo *dmxScreen;
    XEvent N;

    if ((dmxScreen = dmxPropertyIterate(priv->be,
                                        dmxBackendTestMotionEvent, &N))) {
        if (save)
            XPutBackEvent(dmxScreen->beDisplay, &N);
        return dmxScreen;
    }
    return NULL;
}

static void *
dmxBackendTestWindow(DMXScreenInfo * dmxScreen, void *closure)
{
    Window win = (Window) (long) closure;

    if (dmxScreen->scrnWin == win)
        return dmxScreen;
    return NULL;
}

static DMXScreenInfo *
dmxBackendFindWindow(myPrivate * priv, Window win)
{
    return dmxPropertyIterate(priv->be, dmxBackendTestWindow,
                              (void *) (long) win);
}

/* If the cursor is over a set of overlapping screens and one of those
 * screens takes backend input, then we want that particular screen to
 * be current, not one of the other ones. */
static int
dmxBackendFindOverlapping(myPrivate * priv, int screen, int x, int y)
{
    DMXScreenInfo *start = &dmxScreens[screen];
    DMXScreenInfo *pt;

    if (!start->over)
        return screen;

    for (pt = start->over; /* condition at end of loop */ ; pt = pt->over) {
        if (pt->index == priv->myScreen
            && dmxOnScreen(x, y, &dmxScreens[pt->index]))
            return pt->index;
        if (pt == start)
            break;
    }
    return screen;
}

/* Return non-zero if \a x and \a y are off \a screen. */
static int
dmxBackendOffscreen(int screen, int x, int y)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[screen];

    return (!dmxOnScreen(x, y, dmxScreen));
}

/** This routine is called from #dmxCoreMotion for each motion
 * event. \a x and \a y are global coordinants. */
void
dmxBackendUpdatePosition(void *private, int x, int y)
{
    GETPRIVFROMPRIVATE;
    int screen = miPointerGetScreen(inputInfo.pointer)->myNum;
    DMXScreenInfo *dmxScreen = &dmxScreens[priv->myScreen];
    int oldRelative = priv->relative;
    int topscreen = dmxBackendFindOverlapping(priv, screen, x, y);
    int same = dmxBackendSameDisplay(priv, topscreen);
    int offscreen = dmxBackendOffscreen(priv->myScreen, x, y);
    int offthis = dmxBackendOffscreen(screen, x, y);

    DMXDBG9("dmxBackendUpdatePosition(%d,%d) my=%d mi=%d rel=%d"
            " topscreen=%d same=%d offscreen=%d offthis=%d\n",
            x, y, priv->myScreen, screen, priv->relative,
            topscreen, same, offscreen, offthis);

    if (offscreen) {
        /* If the cursor is off the input screen, it should be moving
         * relative unless it is visible on a screen of the same display
         * (i.e., one that shares the mouse). */
        if (same == 2 && !offthis) {
            if (priv->relative) {
                DMXDBG0("   Off screen, but not absolute\n");
                priv->relative = 0;
            }
        }
        else {
            if (!priv->relative) {
                DMXDBG0("   Off screen, but not relative\n");
                priv->relative = 1;
            }
        }
    }
    else {
        if (topscreen != screen) {
            DMXDBG2("   Using screen %d instead of %d (from mi)\n",
                    topscreen, screen);
        }
        if (same) {
            if (priv->relative) {
                DMXDBG0("   On screen, but not absolute\n");
                priv->relative = 0;
            }
        }
        else {
            if (!priv->relative) {
                DMXDBG0("   Not on screen, but not relative\n");
                priv->relative = 1;
            }
        }
    }

    if (oldRelative != priv->relative) {
        DMXDBG2("   Do switch, relative=%d same=%d\n", priv->relative, same);
        /* Discard all pre-switch events */
        dmxSync(dmxScreen, TRUE);
        while (dmxBackendPendingMotionEvent(priv, FALSE));

        if (dmxInput->console && offscreen) {
            /* Our special case is a console window and a backend window
             * share a display.  In this case, the cursor is either on
             * the backend window (taking absolute input), or not (in
             * which case the cursor needs to be in the console
             * window). */
            if (priv->grabbedScreen) {
                DMXDBG2("   *** force ungrab on %s, display=%p\n",
                        priv->grabbedScreen->name,
                        priv->grabbedScreen->beDisplay);
                XUngrabPointer(priv->grabbedScreen->beDisplay, CurrentTime);
                dmxSync(priv->grabbedScreen, TRUE);
                priv->grabbedScreen = NULL;
            }
            DMXDBG0("   Capturing console\n");
            dmxConsoleCapture(dmxInput);
        }
        else {
            priv->newscreen = 1;
            if (priv->relative && !dmxInput->console) {
                DMXDBG5("   Hide cursor; warp from %d,%d to %d,%d on %d\n",
                        priv->lastX, priv->lastY, priv->centerX, priv->centerY,
                        priv->myScreen);
                dmxConsoleUncapture(dmxInput);
                dmxHideCursor(dmxScreen);
                priv->lastX = priv->centerX;
                priv->lastY = priv->centerY;
                XWarpPointer(priv->display, None, priv->window,
                             0, 0, 0, 0, priv->lastX, priv->lastY);
                dmxSync(dmxScreen, TRUE);
            }
            else {
                DMXDBG0("   Check cursor\n");
                dmxCheckCursor();
            }
        }
    }
}

/** Get events from the X queue on the backend servers and put the
 * events into the DMX event queue. */
void
dmxBackendCollectEvents(DevicePtr pDev,
                        dmxMotionProcPtr motion,
                        dmxEnqueueProcPtr enqueue,
                        dmxCheckSpecialProcPtr checkspecial, DMXBlockType block)
{
    GETPRIVFROMPDEV;
    GETDMXINPUTFROMPRIV;
    XEvent X;
    DMXScreenInfo *dmxScreen;
    int left = 0;
    int entered = priv->entered;
    int ignoreLeave = 0;
    int v[2];
    int retcode;

    while ((dmxScreen = dmxBackendGetEvent(priv, &X))) {
        switch (X.type) {
        case EnterNotify:
            dmxCommonSaveState(priv);
            if (entered++)
                continue;
            priv->entered = 1;
            ignoreLeave = 1;
            DMXDBG5("dmxBackendCollectEvents: Enter %lu %d,%d; GRAB %s %p\n",
                    X.xcrossing.root, X.xcrossing.x, X.xcrossing.y,
                    dmxScreen->name, dmxScreen->beDisplay);
            XRaiseWindow(dmxScreen->beDisplay, dmxScreen->scrnWin);
            priv->grabbedScreen = dmxScreen;
            if ((retcode = XGrabPointer(dmxScreen->beDisplay,
                                        dmxScreen->scrnWin,
                                        True, 0, GrabModeAsync,
                                        GrabModeAsync, None, None,
                                        CurrentTime))) {
                dmxLog(dmxError,
                       "XGrabPointer failed during backend enter (%d)\n",
                       retcode);
            }
            break;
        case LeaveNotify:
            if (ignoreLeave) {
                ignoreLeave = 0;
                continue;
            }
            dmxCommonRestoreState(priv);
            if (left++)
                continue;
            DMXDBG7("dmxBackendCollectEvents: Leave %lu %d,%d %d %d %s %s\n",
                    X.xcrossing.root, X.xcrossing.x, X.xcrossing.y,
                    X.xcrossing.detail, X.xcrossing.focus,
                    priv->grabbedScreen ? "UNGRAB" : "", dmxScreen->name);
            if (priv->grabbedScreen) {
                XUngrabPointer(priv->grabbedScreen->beDisplay, CurrentTime);
                dmxSync(priv->grabbedScreen, TRUE);
                priv->grabbedScreen = NULL;
            }
            break;
        case MotionNotify:
            DMXDBG8("dmxBackendCollectEvents: MotionNotify %d/%d"
                    " newscreen=%d: %d %d (e=%d; last=%d,%d)\n",
                    dmxScreen->index, priv->myScreen,
                    priv->newscreen,
                    X.xmotion.x, X.xmotion.y,
                    entered, priv->lastX, priv->lastY);
            if (dmxBackendPendingMotionEvent(priv, TRUE))
                continue;
            if (!(dmxScreen = dmxBackendFindWindow(priv, X.xmotion.window)))
                dmxLog(dmxFatal,
                       "   Event on non-existant window %lu\n",
                       X.xmotion.window);
            if (!priv->relative || dmxInput->console) {
                int newX = X.xmotion.x - dmxScreen->rootX;
                int newY = X.xmotion.y - dmxScreen->rootY;

                if (!priv->newscreen) {
                    int width = dmxScreen->rootWidth;
                    int height = dmxScreen->rootHeight;

                    if (!newX)
                        newX = -1;
                    if (newX == width - 1)
                        newX = width;
                    if (!newY)
                        newY = -1;
                    if (newY == height - 1)
                        newY = height;
                }
                priv->newscreen = 0;
                v[0] = dmxScreen->rootXOrigin + newX;
                v[1] = dmxScreen->rootYOrigin + newY;
                DMXDBG8("   Absolute move: %d,%d (r=%dx%d+%d+%d s=%dx%d)\n",
                        v[0], v[1],
                        priv->be->rootWidth, priv->be->rootHeight,
                        priv->be->rootX, priv->be->rootY,
                        priv->be->scrnWidth, priv->be->scrnHeight);
                motion(priv->mou, v, 0, 2, DMX_ABSOLUTE, block);
                priv->entered = 0;
            }
            else {
                int newX = priv->lastX - X.xmotion.x;
                int newY = priv->lastY - X.xmotion.y;

                priv->lastX = X.xmotion.x;
                priv->lastY = X.xmotion.y;
                v[0] = newX;
                v[1] = newY;
                DMXDBG2("   Relative move: %d, %d\n", v[0], v[1]);
                motion(priv->mou, v, 0, 2, DMX_RELATIVE, block);
            }
            if (entered && priv->relative) {
                DMXDBG4("   **** Relative %d %d instead of absolute %d %d\n",
                        v[0], v[1],
                        (dmxScreen->rootXOrigin + X.xmotion.x
                         - dmxScreen->rootX),
                        (dmxScreen->rootYOrigin + X.xmotion.y
                         - dmxScreen->rootY));
            }
            break;

        case KeyPress:
        case KeyRelease:
            enqueue(priv->kbd, X.type, X.xkey.keycode, 0, NULL, block);
            break;
        case ButtonPress:
        case ButtonRelease:
            /* fall-through */
        default:
            /* Pass the whole event here, because
             * this may be an extension event. */
            enqueue(priv->mou, X.type, X.xbutton.button, 0, &X, block);
            break;
        }
    }
}

/** Called after input events are processed from the DMX queue.  No
 * event processing actually takes place here, but this is a convenient
 * place to update the pointer. */
void
dmxBackendProcessInput(void *private)
{
    GETPRIVFROMPRIVATE;

    DMXDBG6("dmxBackendProcessInput: myScreen=%d relative=%d"
            " last=%d,%d center=%d,%d\n",
            priv->myScreen, priv->relative,
            priv->lastX, priv->lastY, priv->centerX, priv->centerY);

    if (priv->relative
        && !dmxInput->console
        && (priv->lastX != priv->centerX || priv->lastY != priv->centerY)) {
        DMXDBG4("   warping pointer from last=%d,%d to center=%d,%d\n",
                priv->lastX, priv->lastY, priv->centerX, priv->centerY);
        priv->lastX = priv->centerX;
        priv->lastY = priv->centerY;
        XWarpPointer(priv->display, None, priv->window,
                     0, 0, 0, 0, priv->lastX, priv->lastY);
        dmxSync(&dmxScreens[priv->myScreen], TRUE);
    }
}

static void
dmxBackendComputeCenter(myPrivate * priv)
{
    int centerX;
    int centerY;

    centerX = priv->be->rootWidth / 2 + priv->be->rootX;
    centerY = priv->be->rootHeight / 2 + priv->be->rootY;

    if (centerX > priv->be->rootWidth)
        centerX = priv->be->rootWidth - 1;
    if (centerY > priv->be->rootHeight)
        centerY = priv->be->rootHeight - 1;
    if (centerX < 1)
        centerX = 1;
    if (centerY < 1)
        centerY = 1;

    priv->centerX = centerX;
    priv->centerY = centerY;
}

static DMXScreenInfo *
dmxBackendInitPrivate(DevicePtr pDev)
{
    GETPRIVFROMPDEV;
    DMXInputInfo *dmxInput = &dmxInputs[dmxLocal->inputIdx];
    DMXScreenInfo *dmxScreen;
    int i;

    /* Fill in myPrivate */
    for (i = 0, dmxScreen = &dmxScreens[0]; i < dmxNumScreens; i++, dmxScreen++) {
        if (dmxPropertySameDisplay(dmxScreen, dmxInput->name)) {
            priv->display = dmxScreen->beDisplay;
            priv->window = dmxScreen->scrnWin;
            priv->be = dmxScreen;
            break;
        }
    }

    if (i >= dmxNumScreens)
        dmxLog(dmxFatal,
               "%s is not an existing backend display - cannot initialize\n",
               dmxInput->name);

    return dmxScreen;
}

/** Re-initialized the backend device described by \a pDev (after a
 * reconfig). */
void
dmxBackendLateReInit(DevicePtr pDev)
{
    GETPRIVFROMPDEV;
    int x, y;

    dmxBackendSameDisplay(NULL, 0);     /* Invalidate cache */
    dmxBackendInitPrivate(pDev);
    dmxBackendComputeCenter(priv);
    dmxGetGlobalPosition(&x, &y);
    dmxInvalidateGlobalPosition();      /* To force event processing */
    dmxBackendUpdatePosition(priv, x, y);
}

/** Initialized the backend device described by \a pDev. */
void
dmxBackendInit(DevicePtr pDev)
{
    GETPRIVFROMPDEV;
    DMXScreenInfo *dmxScreen;

    dmxBackendSameDisplay(NULL, 0);     /* Invalidate cache */

    if (dmxLocal->type == DMX_LOCAL_MOUSE)
        priv->mou = pDev;
    if (dmxLocal->type == DMX_LOCAL_KEYBOARD)
        priv->kbd = pDev;
    if (priv->initialized++)
        return;                 /* Only do once for mouse/keyboard pair */

    dmxScreen = dmxBackendInitPrivate(pDev);

    /* Finish initialization using computed values or constants. */
    dmxBackendComputeCenter(priv);
    priv->eventMask = (EnterWindowMask | LeaveWindowMask);
    priv->myScreen = dmxScreen->index;
    priv->lastX = priv->centerX;
    priv->lastY = priv->centerY;
    priv->relative = 0;
    priv->newscreen = 0;
}

/** Get information about the backend pointer (for initialization). */
void
dmxBackendMouGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info)
{
    const DMXScreenInfo *dmxScreen = dmxBackendInitPrivate(pDev);

    info->buttonClass = 1;
    dmxCommonMouGetMap(pDev, info->map, &info->numButtons);
    info->valuatorClass = 1;
    info->numRelAxes = 2;
    info->minval[0] = 0;
    info->minval[1] = 0;
    info->maxval[0] = dmxScreen->beWidth;
    info->maxval[1] = dmxScreen->beHeight;
    info->res[0] = 1;
    info->minres[0] = 0;
    info->maxres[0] = 1;
    info->ptrFeedbackClass = 1;
}

/** Get information about the backend keyboard (for initialization). */
void
dmxBackendKbdGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info)
{
    dmxCommonKbdGetInfo(pDev, info);
    info->keyboard = 1;
    info->keyClass = 1;
    dmxCommonKbdGetMap(pDev, &info->keySyms, info->modMap);
    info->freemap = 1;
    info->focusClass = 1;
    info->kbdFeedbackClass = 1;
}

/** Process #DMXFunctionType functions.  The only function handled here
 * is to acknowledge a pending server shutdown. */
int
dmxBackendFunctions(void *private, DMXFunctionType function)
{
    switch (function) {
    case DMX_FUNCTION_TERMINATE:
        return 1;
    default:
        return 0;
    }
}
