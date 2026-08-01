/*
 *Copyright (C) 1994-2000 The XFree86 Project, Inc. All Rights Reserved.
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
 *NONINFRINGEMENT. IN NO EVENT SHALL THE XFREE86 PROJECT BE LIABLE FOR
 *ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *Except as contained in this notice, the name of the XFree86 Project
 *shall not be used in advertising or otherwise to promote the sale, use
 *or other dealings in this Software without prior written authorization
 *from the XFree86 Project.
 *
 * Authors:	Dakshinamurthy Karra
 *		Suhaib M Siddiqi
 *		Peter Busch
 *		Harold L Hunt II
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"

#ifdef XKB
#ifndef XKB_IN_SERVER
#define XKB_IN_SERVER
#endif
#include <xkbsrv.h>
#endif

#include "inputstr.h"
#include "exevents.h"           /* for button/axes labels */
#include "xserver-properties.h"
#include "inpututils.h"

#include "winwindow.h"   /* for winGetWindowPriv, winPrivWinRec */

/* Prevent POINTER_ABSOLUTE events from generating XI_RawMotion.
 * Only POINTER_RELATIVE events should produce XI_RawMotion for
 * XInput2 relative-mode clients (SDL2, etc.). */
#ifndef POINTER_NORAW
#define POINTER_NORAW 0x20
#endif

/* Peek the internal button mapping */
static CARD8 const *g_winMouseButtonMap = NULL;

/* Master device grab callback hooks (for Core XGrabPointer) */
static void (*winOrigMasterActivateGrab)(DeviceIntPtr, GrabPtr, TimeStamp, Bool) = NULL;
static void (*winOrigMasterDeactivateGrab)(DeviceIntPtr) = NULL;

/* Slave device grab callback hooks (for XI2 XGrabDevice) */
static void (*winOrigSlaveActivateGrab)(DeviceIntPtr, GrabPtr, TimeStamp, Bool) = NULL;
static void (*winOrigSlaveDeactivateGrab)(DeviceIntPtr) = NULL;

/* HWND of the vcxsrv window that currently has the mouse. */
HWND g_hwndGrabWindow = NULL;

/* Timer ID for periodic ClipCursor re-apply */
static UINT_PTR g_grabTimerId = 0;

/*
 * Apply cursor constraint to foreground window
 */
static void
winApplyCursorConstraint(void)
{
    HWND hwnd;
    RECT rect;

    hwnd = g_hwndGrabWindow;
    if (!hwnd)
        hwnd = GetForegroundWindow();
    if (!hwnd)
        return;

    GetClientRect(hwnd, &rect);
    ClientToScreen(hwnd, (LPPOINT)&rect.left);
    ClientToScreen(hwnd, (LPPOINT)&rect.right);
    /* 1px padding: keep cursor away from sizing border */
    rect.left   += 1;
    rect.top    += 1;
    rect.right  -= 1;
    rect.bottom -= 1;
    ClipCursor(&rect);
}

/*
 * Release cursor constraint
 */
static void
winReleaseCursorConstraint(void)
{
    ClipCursor(NULL);
}

/*
 * Force-hide cursor during grab
 */
static void
winGrabHideCursor(void)
{
    while (ShowCursor(FALSE) >= 0)
        ;
}

/*
 * Force-show cursor after grab release
 */
static void
winGrabShowCursor(void)
{
    while (ShowCursor(TRUE) < 0)
        ;
}

/*
 * Master device ActivateGrab hook.
 * SDL2 X11 backend uses XGrabPointer (Core protocol), which operates
 * on the MASTER pointer device. This hook handles that path.
 */
static void
winMasterActivateGrab(DeviceIntPtr dev, GrabPtr grab, TimeStamp time, Bool autoGrab)
{
    if (winOrigMasterActivateGrab)
        winOrigMasterActivateGrab(dev, grab, time, autoGrab);

    /* Only confine cursor position if grab specifies a confine window. */
    if (grab && grab->confineTo) {
        /* Map X11 grab window to Windows hwnd for precise constraint. */
        WindowPtr pGrabWin = grab->confineTo ? grab->confineTo : grab->window;
        winPrivWinPtr pWinPriv = winGetWindowPriv(pGrabWin);
        if (pWinPriv && pWinPriv->hWnd) {
            g_hwndGrabWindow = pWinPriv->hWnd;
        }
        winApplyCursorConstraint();
        /* Windows may silently cancel ClipCursor (Win key,
         * cursor skipping, etc.). This timer defends against it. */
        if (!g_grabTimerId && g_hwndGrabWindow) {
            g_grabTimerId = SetTimer(g_hwndGrabWindow, WIN_CLIPCURSOR_TIMER_ID, 10, NULL);
        }
        winGrabHideCursor();
    }
    /* confineTo=None: no Windows-side effect */
}

/*
 * Master device DeactivateGrab hook.
 */
static void
winMasterDeactivateGrab(DeviceIntPtr dev)
{
    /* dev->deviceGrab.grab points to the grab being released. */
    GrabPtr grab = dev->deviceGrab.grab;
    /* Confining grab: release Windows cursor state */
    if (grab && grab->confineTo) {
        winReleaseCursorConstraint();
        winGrabShowCursor();
        /* Stop periodic timer */
        if (g_grabTimerId) {
            KillTimer(g_hwndGrabWindow, g_grabTimerId);
            g_grabTimerId = 0;
        }
    }

    if (winOrigMasterDeactivateGrab)
        winOrigMasterDeactivateGrab(dev);
}

/*
 * Slave device ActivateGrab hook.
 * XI2 XGrabDevice may operate on slave devices directly.
 */
static void
winSlaveActivateGrab(DeviceIntPtr dev, GrabPtr grab, TimeStamp time, Bool autoGrab)
{
    if (winOrigSlaveActivateGrab)
        winOrigSlaveActivateGrab(dev, grab, time, autoGrab);

    /* Only confine cursor position if grab specifies a confine window. */
    if (grab && grab->confineTo) {
        /* Map X11 grab window to Windows hwnd for precise constraint. */
        WindowPtr pGrabWin = grab->confineTo ? grab->confineTo : grab->window;
        winPrivWinPtr pWinPriv = winGetWindowPriv(pGrabWin);
        if (pWinPriv && pWinPriv->hWnd) {
            g_hwndGrabWindow = pWinPriv->hWnd;
        }
        winApplyCursorConstraint();
        /* Windows may silently cancel ClipCursor (Win key,
         * cursor skipping, etc.). This timer defends against it. */
        if (!g_grabTimerId && g_hwndGrabWindow) {
            g_grabTimerId = SetTimer(g_hwndGrabWindow, WIN_CLIPCURSOR_TIMER_ID, 10, NULL);
        }
        winGrabHideCursor();
    }
    /* confineTo=None: no Windows-side effect */
}


/*
 * Slave device DeactivateGrab hook.
 */
static void
winSlaveDeactivateGrab(DeviceIntPtr dev)
{
    /* dev->deviceGrab.grab points to the grab being released. */
    GrabPtr grab = dev->deviceGrab.grab;
    /* Confining grab: release Windows cursor state */
    if (grab && grab->confineTo) {
        winReleaseCursorConstraint();
        winGrabShowCursor();
        /* Stop periodic timer */
        if (g_grabTimerId) {
            KillTimer(g_hwndGrabWindow, g_grabTimerId);
            g_grabTimerId = 0;
        }
    }

    if (winOrigSlaveDeactivateGrab)
        winOrigSlaveDeactivateGrab(dev);
}


/*
 * See Porting Layer Definition - p. 18
 * This is known as a DeviceProc
 */

int
winMouseProc(DeviceIntPtr pDeviceInt, int iState)
{
    int lngMouseButtons, i;
    int lngWheelEvents = 4;
    CARD8 *map;
    DevicePtr pDevice = (DevicePtr) pDeviceInt;
    Atom btn_labels[9];
    Atom axes_labels[2];

    switch (iState) {
    case DEVICE_INIT:
        /* Get number of mouse buttons */
        lngMouseButtons = GetSystemMetrics(SM_CMOUSEBUTTONS);
        winDebug("%d mouse buttons found\n", lngMouseButtons);

        /* Mapping of windows events to X events:
         * LEFT:1 MIDDLE:2 RIGHT:3
         * SCROLL_UP:4 SCROLL_DOWN:5
         * TILT_LEFT:6 TILT_RIGHT:7
         * XBUTTON 1:8 XBUTTON 2:9 (most commonly 'back' and 'forward')
         * ...
         *
         * The current Windows API only defines 2 extra buttons, so we don't
         * expect more than 5 buttons to be reported, but more than that
         * should be handled correctly
         */

        /*
         * To map scroll wheel correctly we need at least the 3 normal buttons
         */
        if (lngMouseButtons < 3)
            lngMouseButtons = 3;

        /* allocate memory:
         * number of buttons + 4 x mouse wheel event + 1 extra (offset for map)
         */
        map = malloc(sizeof(CARD8) * (lngMouseButtons + lngWheelEvents + 1));

        /* initialize button map */
        map[0] = 0;
        for (i = 1; i <= lngMouseButtons + lngWheelEvents; i++)
            map[i] = i;

        btn_labels[0] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_LEFT);
        btn_labels[1] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_MIDDLE);
        btn_labels[2] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_RIGHT);
        btn_labels[3] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_WHEEL_UP);
        btn_labels[4] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_WHEEL_DOWN);
        btn_labels[5] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_HWHEEL_LEFT);
        btn_labels[6] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_HWHEEL_RIGHT);
        btn_labels[7] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_BACK);
        btn_labels[8] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_FORWARD);

        axes_labels[0] = XIGetKnownProperty(AXIS_LABEL_PROP_REL_X);
        axes_labels[1] = XIGetKnownProperty(AXIS_LABEL_PROP_REL_Y);

        InitPointerDeviceStruct(pDevice,
                                map,
                                lngMouseButtons + lngWheelEvents,
                                btn_labels,
                                (PtrCtrlProcPtr)NoopDDA,
                                GetMotionHistorySize(), 2, axes_labels);

        /* Hook master device (for Core XGrabPointer used by SDL2) */
        {
            DeviceIntPtr master = inputInfo.pointer;
            if (master && master != pDeviceInt) {
                winOrigMasterActivateGrab = master->deviceGrab.ActivateGrab;
                winOrigMasterDeactivateGrab = master->deviceGrab.DeactivateGrab;
                master->deviceGrab.ActivateGrab = winMasterActivateGrab;
                master->deviceGrab.DeactivateGrab = winMasterDeactivateGrab;
            }
        }

        /* Hook slave device too (for XI2 XGrabDevice) */
        winOrigSlaveActivateGrab = pDeviceInt->deviceGrab.ActivateGrab;
        winOrigSlaveDeactivateGrab = pDeviceInt->deviceGrab.DeactivateGrab;
        pDeviceInt->deviceGrab.ActivateGrab = winSlaveActivateGrab;
        pDeviceInt->deviceGrab.DeactivateGrab = winSlaveDeactivateGrab;

        /* XInput2 clients query the master pointer's valuator mode
         * (Virtual core pointer, device 2) to determine how to
         * interpret XI_RawMotion values. Mode must be Relative for
         * SDL2 relative mouse mode to work correctly.
         * The slave (Windows pointer, device 6) mode is set
         * automatically by InitPointerDeviceStruct, but the master
         * mode must be updated explicitly.
         */
        if (inputInfo.pointer && inputInfo.pointer->valuator &&
            inputInfo.pointer->valuator->numAxes >= 2) {
            inputInfo.pointer->valuator->axes[0].mode = Relative;
            inputInfo.pointer->valuator->axes[1].mode = Relative;
        }

        free(map);

        g_winMouseButtonMap = pDeviceInt->button->map;
        break;

    case DEVICE_ON:
        pDevice->on = TRUE;
        break;

    case DEVICE_CLOSE:
        g_winMouseButtonMap = NULL;

    case DEVICE_OFF:
        pDevice->on = FALSE;

        /* Force release cursor constraint on device shutdown */
        winReleaseCursorConstraint();
        winGrabShowCursor();
        if (g_grabTimerId) {
            KillTimer(g_hwndGrabWindow, g_grabTimerId);
            g_grabTimerId = 0;
        }
        break;
    }
    return Success;
}

/* Handle the mouse wheel */
int
winMouseWheel(int *iTotalDeltaZ, int iDeltaZ, int iButtonUp, int iButtonDown)
{
    int button;

    /* Do we have any previous delta stored? */
    if ((*iTotalDeltaZ > 0 && iDeltaZ > 0)
        || (*iTotalDeltaZ < 0 && iDeltaZ < 0)) {
        /* Previous delta and of same sign as current delta */
        iDeltaZ += *iTotalDeltaZ;
        *iTotalDeltaZ = 0;
    }
    else {
        /*
         * Previous delta of different sign, or zero.
         * We will set it to zero for either case,
         * as blindly setting takes just as much time
         * as checking, then setting if necessary :)
         */
        *iTotalDeltaZ = 0;
    }

    /*
     * Only process this message if the wheel has moved further than
     * WHEEL_DELTA
     */
    if (iDeltaZ >= WHEEL_DELTA || (-1 * iDeltaZ) >= WHEEL_DELTA) {
        *iTotalDeltaZ = 0;

        /* Figure out how many whole deltas of the wheel we have */
        iDeltaZ /= WHEEL_DELTA;
    }
    else {
        /*
         * Wheel has not moved past WHEEL_DELTA threshold;
         * we will store the wheel delta until the threshold
         * has been reached.
         */
        *iTotalDeltaZ = iDeltaZ;
        return 0;
    }

    /* Set the button to indicate up or down wheel delta */
    if (iDeltaZ > 0) {
        button = iButtonUp;
    }
    else {
        button = iButtonDown;
    }

    /*
     * Flip iDeltaZ to positive, if negative,
     * because always need to generate a *positive* number of
     * button clicks for the Z axis.
     */
    if (iDeltaZ < 0) {
        iDeltaZ *= -1;
    }

    /* Generate X input messages for each wheel delta we have seen */
    while (iDeltaZ--) {
        /* Push the wheel button */
        winMouseButtonsSendEvent(ButtonPress, button);

        /* Release the wheel button */
        winMouseButtonsSendEvent(ButtonRelease, button);
    }

    return 0;
}

/*
 * Enqueue a mouse button event
 */

void
winMouseButtonsSendEvent(int iEventType, int iButton)
{
    ValuatorMask mask;

    if (g_winMouseButtonMap)
        iButton = g_winMouseButtonMap[iButton];

    valuator_mask_zero(&mask);
    QueuePointerEvents(g_pwinPointer, iEventType, iButton,
                       POINTER_RELATIVE, &mask);

  winDebug("winMouseButtonsSendEvent: iEventType: %d, iButton: %d\n",
           iEventType, iButton);
}

/*
 * Decide what to do with a Windows mouse message
 */

int
winMouseButtonsHandle(ScreenPtr pScreen,
                      int iEventType, int iButton, WPARAM wParam)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;

    /* Send button events right away if emulate 3 buttons is off */
    if (pScreenInfo->iE3BTimeout == WIN_E3B_OFF) {
        /* Emulate 3 buttons is off, send the button event */
        winMouseButtonsSendEvent(iEventType, iButton);
        return 0;
    }

    /* Emulate 3 buttons is on, let the fun begin */
    if (iEventType == ButtonPress
        && pScreenPriv->iE3BCachedPress == 0
        && !pScreenPriv->fE3BFakeButton2Sent) {
        /*
         * Button was pressed, no press is cached,
         * and there is no fake button 2 release pending.
         */

        /* Store button press type */
        pScreenPriv->iE3BCachedPress = iButton;

        /*
         * Set a timer to send this button press if the other button
         * is not pressed within the timeout time.
         */
        SetTimer(pScreenPriv->hwndScreen,
                 WIN_E3B_TIMER_ID, pScreenInfo->iE3BTimeout, NULL);
    }
    else if (iEventType == ButtonPress
             && pScreenPriv->iE3BCachedPress != 0
             && pScreenPriv->iE3BCachedPress != iButton
             && !pScreenPriv->fE3BFakeButton2Sent) {
        /*
         * Button press is cached, other button was pressed,
         * and there is no fake button 2 release pending.
         */

        /* Mouse button was cached and other button was pressed */
        KillTimer(pScreenPriv->hwndScreen, WIN_E3B_TIMER_ID);
        pScreenPriv->iE3BCachedPress = 0;

        /* Send fake middle button */
        winMouseButtonsSendEvent(ButtonPress, Button2);

        /* Indicate that a fake middle button event was sent */
        pScreenPriv->fE3BFakeButton2Sent = TRUE;
    }
    else if (iEventType == ButtonRelease
             && pScreenPriv->iE3BCachedPress == iButton) {
        /*
         * Cached button was released before timer ran out,
         * and before the other mouse button was pressed.
         */
        KillTimer(pScreenPriv->hwndScreen, WIN_E3B_TIMER_ID);
        pScreenPriv->iE3BCachedPress = 0;

        /* Send cached press, then send release */
        winMouseButtonsSendEvent(ButtonPress, iButton);
        winMouseButtonsSendEvent(ButtonRelease, iButton);
    }
    else if (iEventType == ButtonRelease
             && pScreenPriv->fE3BFakeButton2Sent && !(wParam & MK_LBUTTON)
             && !(wParam & MK_RBUTTON)) {
        /*
         * Fake button 2 was sent and both mouse buttons have now been released
         */
        pScreenPriv->fE3BFakeButton2Sent = FALSE;

        /* Send middle mouse button release */
        winMouseButtonsSendEvent(ButtonRelease, Button2);
    }
    else if (iEventType == ButtonRelease
             && pScreenPriv->iE3BCachedPress == 0
             && !pScreenPriv->fE3BFakeButton2Sent) {
        /*
         * Button was release, no button is cached,
         * and there is no fake button 2 release is pending.
         */
        winMouseButtonsSendEvent(ButtonRelease, iButton);
    }

    return 0;
}

/**
 * Enqueue a motion event.
 *
 */
void
winEnqueueMotion(int x, int y)
{
    int valuators[2];
    ValuatorMask mask;

    valuator_mask_zero(&mask);

    valuators[0] = x;
    valuators[1] = y;

    valuator_mask_set_range(&mask, 0, 2, valuators);
    QueuePointerEvents(g_pwinPointer, MotionNotify, 0,
                       POINTER_ABSOLUTE | POINTER_SCREEN | POINTER_NORAW, &mask);
                       /* ADD POINTER_NORAW */

}

/*
 * Enqueue a raw motion event with relative coordinates.
 *
 * WM_INPUT delivers hardware-level relative displacement (lLastX/Y)
 * via GetRawInputData. We inject it into the X server's
 * POINTER_RELATIVE pipeline so XInput2 generates XI_RawMotion
 * events for clients in relative mouse mode.
 */
void
winEnqueueRawMotion(int dx, int dy)
{
    int valuators[2];
    ValuatorMask mask;

    valuator_mask_zero(&mask);

    valuators[0] = dx;
    valuators[1] = dy;

    valuator_mask_set_range(&mask, 0, 2, valuators);
    QueuePointerEvents(g_pwinPointer, MotionNotify, 0,
                       POINTER_RELATIVE, &mask);
}
