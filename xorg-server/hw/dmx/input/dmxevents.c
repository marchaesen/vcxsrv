/*
 * Copyright 2002-2003 Red Hat Inc., Durham, North Carolina.
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
 * Provide support and helper functions for enqueing events received by
 * the low-level input drivers. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#define DMX_EVENTS_DEBUG 0

#include "dmxinputinit.h"
#include "dmxevents.h"
#include "dmxcb.h"
#include "dmxcommon.h"
#include "dmxcursor.h"
#include "dmxmotion.h"
#include "dmxsigio.h"
#include "dmxmap.h"

#include <X11/keysym.h>
#include "opaque.h"
#include "inputstr.h"
#include "inpututils.h"
#include "mipointer.h"
#include "mi.h"
#include "exglobals.h"

#include "xkbsrv.h"
#include "XIstubs.h"

static int dmxGlobalX, dmxGlobalY;      /* Global cursor position */
static int dmxGlobalInvalid;    /* Flag indicating dmxCoreMotion
                                 * should move the mouse anyway. */

#if DMX_EVENTS_DEBUG
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

static int
dmxApplyFunctions(DMXInputInfo * dmxInput, DMXFunctionType f)
{
    int i;
    int rc = 0;

    for (i = 0; i < dmxInput->numDevs; i += dmxInput->devs[i]->binding)
        if (dmxInput->devs[i]->functions)
            rc += dmxInput->devs[i]->functions(dmxInput->devs[i]->private, f);
    return rc;
}

static int
dmxCheckFunctionKeys(DMXLocalInputInfoPtr dmxLocal, int type, KeySym keySym)
{
    DMXInputInfo *dmxInput = &dmxInputs[dmxLocal->inputIdx];

#if 1                           /* hack to detect ctrl-alt-q, etc */
    static int ctrl = 0, alt = 0;

    /* keep track of ctrl/alt key status */
    if (type == KeyPress && keySym == 0xffe3) {
        ctrl = 1;
    }
    else if (type == KeyRelease && keySym == 0xffe3) {
        ctrl = 0;
    }
    else if (type == KeyPress && keySym == 0xffe9) {
        alt = 1;
    }
    else if (type == KeyRelease && keySym == 0xffe9) {
        alt = 0;
    }
    if (!ctrl || !alt)
        return 0;
#else
    unsigned short state = 0;

    if (dmxLocal->sendsCore)
        state = dmxLocalCoreKeyboard->pDevice->key->state;
    else if (dmxLocal->pDevice->key)
        state = dmxLocal->pDevice->key->state;

    DMXDBG3("dmxCheckFunctionKeys: keySym=0x%04x %s state=0x%04x\n",
            keySym, type == KeyPress ? "press" : "release", state);

    if ((state & (ControlMask | Mod1Mask)) != (ControlMask | Mod1Mask))
        return 0;
#endif

    switch (keySym) {
    case XK_g:
        if (type == KeyPress)
            dmxApplyFunctions(dmxInput, DMX_FUNCTION_GRAB);
        return 1;
    case XK_f:
        if (type == KeyPress)
            dmxApplyFunctions(dmxInput, DMX_FUNCTION_FINE);
        return 1;
    case XK_q:
        if (type == KeyPress && dmxLocal->sendsCore)
            if (dmxApplyFunctions(dmxInput, DMX_FUNCTION_TERMINATE)) {
                dmxLog(dmxInfo, "User request for termination\n");
                dispatchException |= DE_TERMINATE;
            }
        return 1;
    }

    return 0;
}

DMXScreenInfo *
dmxFindFirstScreen(int x, int y)
{
    int i;

    for (i = 0; i < dmxNumScreens; i++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[i];

        if (dmxOnScreen(x, y, dmxScreen))
            return dmxScreen;
    }
    return NULL;
}

/**
 * Enqueue a motion event.
 */
static void
enqueueMotion(DevicePtr pDev, int x, int y)
{
    GETDMXLOCALFROMPDEV;
    DeviceIntPtr p = dmxLocal->pDevice;
    int valuators[3];
    int detail = 0;             /* XXX should this be mask of pressed buttons? */
    ValuatorMask mask;

    valuators[0] = x;
    valuators[1] = y;

    valuator_mask_set_range(&mask, 0, 2, valuators);
    QueuePointerEvents(p, MotionNotify, detail,
                       POINTER_ABSOLUTE | POINTER_SCREEN, &mask);
    return;
}

void
dmxCoreMotion(DevicePtr pDev, int x, int y, int delta, DMXBlockType block)
{
    DMXScreenInfo *dmxScreen;
    DMXInputInfo *dmxInput;
    ScreenPtr pScreen;
    int localX;
    int localY;
    int i;

    if (!dmxGlobalInvalid && dmxGlobalX == x && dmxGlobalY == y)
        return;

    DMXDBG5("dmxCoreMotion(%d,%d,%d) dmxGlobalX=%d dmxGlobalY=%d\n",
            x, y, delta, dmxGlobalX, dmxGlobalY);

    dmxGlobalInvalid = 0;
    dmxGlobalX = x;
    dmxGlobalY = y;

    if (dmxGlobalX < 0)
        dmxGlobalX = 0;
    if (dmxGlobalY < 0)
        dmxGlobalY = 0;
    if (dmxGlobalX >= dmxGlobalWidth)
        dmxGlobalX = dmxGlobalWidth + delta - 1;
    if (dmxGlobalY >= dmxGlobalHeight)
        dmxGlobalY = dmxGlobalHeight + delta - 1;

    if ((dmxScreen = dmxFindFirstScreen(dmxGlobalX, dmxGlobalY))) {
        localX = dmxGlobalX - dmxScreen->rootXOrigin;
        localY = dmxGlobalY - dmxScreen->rootYOrigin;
        if ((pScreen = miPointerGetScreen(inputInfo.pointer))
            && pScreen->myNum == dmxScreen->index) {
            /* Screen is old screen */
            if (block)
                OsBlockSIGIO();
            if (pDev)
                enqueueMotion(pDev, localX, localY);
            if (block)
                OsReleaseSIGIO();
        }
        else {
            /* Screen is new */
            DMXDBG4("   New screen: old=%d new=%d localX=%d localY=%d\n",
                    pScreen->myNum, dmxScreen->index, localX, localY);
            if (block)
                OsBlockSIGIO();
            mieqProcessInputEvents();
            miPointerSetScreen(inputInfo.pointer, dmxScreen->index,
                               localX, localY);
            if (pDev)
                enqueueMotion(pDev, localX, localY);
            if (block)
                OsReleaseSIGIO();
        }
#if 00
        miPointerGetPosition(inputInfo.pointer, &localX, &localY);

        if ((pScreen = miPointerGetScreen(inputInfo.pointer))) {
            dmxGlobalX = localX + dmxScreens[pScreen->myNum].rootXOrigin;
            dmxGlobalY = localY + dmxScreens[pScreen->myNum].rootYOrigin;
            ErrorF("Global is now %d, %d  %d, %d\n", dmxGlobalX, dmxGlobalY,
                   localX, localY);
            DMXDBG6("   Moved to dmxGlobalX=%d dmxGlobalY=%d"
                    " on screen index=%d/%d localX=%d localY=%d\n",
                    dmxGlobalX, dmxGlobalY,
                    dmxScreen ? dmxScreen->index : -1, pScreen->myNum,
                    localX, localY);
        }
#endif
    }
    /* Send updates down to all core input
     * drivers */
    for (i = 0, dmxInput = &dmxInputs[0]; i < dmxNumInputs; i++, dmxInput++) {
        int j;

        for (j = 0; j < dmxInput->numDevs; j += dmxInput->devs[j]->binding)
            if (!dmxInput->detached
                && dmxInput->devs[j]->sendsCore
                && dmxInput->devs[j]->update_position)
                dmxInput->devs[j]->update_position(dmxInput->devs[j]->private,
                                                   dmxGlobalX, dmxGlobalY);
    }
    if (!dmxScreen)
        ProcessInputEvents();
}

#define DMX_MAX_AXES 32         /* Max axes reported by this routine */
static void
dmxExtMotion(DMXLocalInputInfoPtr dmxLocal,
             int *v, int firstAxis, int axesCount,
             DMXMotionType type, DMXBlockType block)
{
    DeviceIntPtr pDevice = dmxLocal->pDevice;
    xEvent xE[2 * DMX_MAX_AXES / 6];
    deviceKeyButtonPointer *xev = (deviceKeyButtonPointer *) xE;
    deviceValuator *xv = (deviceValuator *) xev + 1;
    int thisX = 0;
    int thisY = 0;
    int count;
    ValuatorMask mask;

    memset(xE, 0, sizeof(xE));

    if (axesCount > DMX_MAX_AXES)
        axesCount = DMX_MAX_AXES;

    if ((valuator_get_mode(pDevice, 0) == Relative) && axesCount == 2) {
        /* The dmx console is a relative mode
         * device that sometimes reports
         * absolute motion.  It only has two
         * axes. */
        if (type == DMX_RELATIVE) {
            thisX = -v[0];
            thisY = -v[1];
            dmxLocal->lastX += thisX;
            dmxLocal->lastY += thisY;
            if (dmxLocal->update_position)
                dmxLocal->update_position(dmxLocal->private,
                                          dmxLocal->lastX, dmxLocal->lastY);
        }
        else {                  /* Convert to relative */
            if (dmxLocal->lastX || dmxLocal->lastY) {
                thisX = v[0] - dmxLocal->lastX;
                thisY = v[1] - dmxLocal->lastY;
            }
            dmxLocal->lastX = v[0];
            dmxLocal->lastY = v[1];
        }
        v[0] = thisX;
        v[1] = thisY;
    }

    if (axesCount <= 6) {
        /* Optimize for the common case when
         * only 1 or 2 axes change. */
        xev->time = GetTimeInMillis();
        xev->type = DeviceMotionNotify;
        xev->detail = 0;
        xev->deviceid = pDevice->id | MORE_EVENTS;

        xv->type = DeviceValuator;
        xv->deviceid = pDevice->id;
        xv->num_valuators = axesCount;
        xv->first_valuator = firstAxis;
        switch (xv->num_valuators) {
        case 6:
            xv->valuator5 = v[5];
        case 5:
            xv->valuator4 = v[4];
        case 4:
            xv->valuator3 = v[3];
        case 3:
            xv->valuator2 = v[2];
        case 2:
            xv->valuator1 = v[1];
        case 1:
            xv->valuator0 = v[0];
        }
        count = 2;
    }
    else {
        int i;

        for (i = 0, count = 0; i < axesCount; i += 6) {
            xev->time = GetTimeInMillis();
            xev->type = DeviceMotionNotify;
            xev->detail = 0;
            xev->deviceid = pDevice->id | MORE_EVENTS;
            xev += 2;

            xv->type = DeviceValuator;
            xv->deviceid = pDevice->id;
            xv->num_valuators = (i + 6 >= axesCount ? axesCount - i : 6);
            xv->first_valuator = firstAxis + i;
            switch (xv->num_valuators) {
            case 6:
                xv->valuator5 = v[i + 5];
            case 5:
                xv->valuator4 = v[i + 4];
            case 4:
                xv->valuator3 = v[i + 3];
            case 3:
                xv->valuator2 = v[i + 2];
            case 2:
                xv->valuator1 = v[i + 1];
            case 1:
                xv->valuator0 = v[i + 0];
            }
            xv += 2;
            count += 2;
        }
    }

    if (block)
        OsBlockSIGIO();
    valuator_mask_set_range(&mask, firstAxis, axesCount, v);
    QueuePointerEvents(pDevice, MotionNotify, 0, POINTER_ABSOLUTE, &mask);

    if (block)
        OsReleaseSIGIO();
}

static int
dmxTranslateAndEnqueueExtEvent(DMXLocalInputInfoPtr dmxLocal,
                               XEvent * e, DMXBlockType block)
{
    int type;
    int event = -1;
    XDeviceKeyEvent *ke = (XDeviceKeyEvent *) e;
    XDeviceMotionEvent *me = (XDeviceMotionEvent *) e;
    DeviceIntPtr pDevice = dmxLocal->pDevice;
    int valuators[MAX_VALUATORS];
    ValuatorMask mask;

    if (!e)
        return -1;              /* No extended event passed, cannot handle */

    if ((XID) dmxLocal->deviceId != ke->deviceid) {
        /* Search for the correct dmxLocal,
         * since backend and console events are
         * picked up for the first device on
         * that X server. */
        int i;
        DMXInputInfo *dmxInput = &dmxInputs[dmxLocal->inputIdx];

        for (i = 0; i < dmxInput->numDevs; i++) {
            dmxLocal = dmxInput->devs[i];
            if ((XID) dmxLocal->deviceId == ke->deviceid)
                break;
        }
    }

    if ((XID) dmxLocal->deviceId != ke->deviceid
        || (type = dmxMapLookup(dmxLocal, e->type)) < 0)
        return -1;              /* No mapping, so this event is unhandled */

    switch (type) {
    case XI_DeviceValuator:
        event = DeviceValuator;
        break;
    case XI_DeviceKeyPress:
        event = KeyPress;
        break;
    case XI_DeviceKeyRelease:
        event = KeyRelease;
        break;
    case XI_DeviceButtonPress:
        event = ButtonPress;
        break;
    case XI_DeviceButtonRelease:
        event = ButtonRelease;
        break;
    case XI_DeviceMotionNotify:
        event = MotionNotify;
        break;
    case XI_DeviceFocusIn:
        event = DeviceFocusIn;
        break;
    case XI_DeviceFocusOut:
        event = DeviceFocusOut;
        break;
    case XI_ProximityIn:
        event = ProximityIn;
        break;
    case XI_ProximityOut:
        event = ProximityOut;
        break;
    case XI_DeviceStateNotify:
        event = DeviceStateNotify;
        break;
    case XI_DeviceMappingNotify:
        event = DeviceMappingNotify;
        break;
    case XI_ChangeDeviceNotify:
        event = ChangeDeviceNotify;
        break;
    case XI_DeviceKeystateNotify:
        event = DeviceStateNotify;
        break;
    case XI_DeviceButtonstateNotify:
        event = DeviceStateNotify;
        break;
    }

#define EXTRACT_VALUATORS(ke, valuators) \
        valuators[0] = ke->axis_data[0]; \
        valuators[1] = ke->axis_data[1]; \
        valuators[2] = ke->axis_data[2]; \
        valuators[3] = ke->axis_data[3]; \
        valuators[4] = ke->axis_data[4]; \
        valuators[5] = ke->axis_data[5]; \

    switch (type) {
    case XI_DeviceKeyPress:
    case XI_DeviceKeyRelease:
        if (block)
            OsBlockSIGIO();
        QueueKeyboardEvents(pDevice, event, ke->keycode);
        if (block)
            OsReleaseSIGIO();
        break;
    case XI_DeviceButtonPress:
    case XI_DeviceButtonRelease:
        EXTRACT_VALUATORS(ke, valuators);
        valuator_mask_set_range(&mask, ke->first_axis, ke->axes_count,
                                valuators);
        if (block)
            OsBlockSIGIO();
        QueuePointerEvents(pDevice, event, ke->keycode,
                           POINTER_ABSOLUTE, &mask);
        if (block)
            OsReleaseSIGIO();
        break;
    case XI_ProximityIn:
    case XI_ProximityOut:
        EXTRACT_VALUATORS(ke, valuators);
        valuator_mask_set_range(&mask, ke->first_axis, ke->axes_count,
                                valuators);
        if (block)
            OsBlockSIGIO();
        QueueProximityEvents(pDevice, event, &mask);
        if (block)
            OsReleaseSIGIO();
        break;

        break;

    case XI_DeviceMotionNotify:
        dmxExtMotion(dmxLocal, me->axis_data, me->first_axis, me->axes_count,
                     DMX_ABSOLUTE, block);
        break;
    case XI_DeviceFocusIn:
    case XI_DeviceFocusOut:
    case XI_DeviceStateNotify:
    case XI_DeviceMappingNotify:
    case XI_ChangeDeviceNotify:
    case XI_DeviceKeystateNotify:
    case XI_DeviceButtonstateNotify:
        /* These are ignored, since DMX will
         * generate its own events of these
         * types, as necessary.

         * Perhaps ChangeDeviceNotify should
         * generate an error, because it is
         * unexpected? */
        break;
    case XI_DeviceValuator:
    default:
        dmxLog(dmxWarning,
               "XInput extension event (remote=%d -> zero-based=%d)"
               " not supported yet\n", e->type, type);
        return -1;
    }
    return 0;
}

static int
dmxGetButtonMapping(DMXLocalInputInfoPtr dmxLocal, int button)
{
    ButtonClassPtr b = dmxLocal->pDevice->button;

    if (button > b->numButtons) {       /* This shouldn't happen. */
        dmxLog(dmxWarning, "Button %d pressed, but only %d buttons?!?\n",
               button, b->numButtons);
        return button;
    }
    return b->map[button];
}

/** Return DMX's notion of the pointer position in the global coordinate
 * space. */
void
dmxGetGlobalPosition(int *x, int *y)
{
    *x = dmxGlobalX;
    *y = dmxGlobalY;
}

/** Invalidate the global position for #dmxCoreMotion. */
void
dmxInvalidateGlobalPosition(void)
{
    dmxGlobalInvalid = 1;
}

/** Enqueue a motion event for \a pDev.  The \a v vector has length \a
 * axesCount, and contains values for each of the axes, starting at \a
 * firstAxes.
 *
 * The \a type of the motion may be \a DMX_RELATIVE, \a DMX_ABSOLUTE, or
 * \a DMX_ABSOLUTE_CONFINED (in the latter case, the pointer will not be
 * allowed to move outside the global boundaires).
 *
 * If \a block is set to \a DMX_BLOCK, then the SIGIO handler will be
 * blocked around calls to \a enqueueMotion(). */
void
dmxMotion(DevicePtr pDev, int *v, int firstAxes, int axesCount,
          DMXMotionType type, DMXBlockType block)
{
    GETDMXLOCALFROMPDEV;

    if (!dmxLocal->sendsCore) {
        dmxExtMotion(dmxLocal, v, firstAxes, axesCount, type, block);
        return;
    }
    if (axesCount == 2) {
        switch (type) {
        case DMX_RELATIVE:
            dmxCoreMotion(pDev, dmxGlobalX - v[0], dmxGlobalY - v[1], 0, block);
            break;
        case DMX_ABSOLUTE:
            dmxCoreMotion(pDev, v[0], v[1], 0, block);
            break;
        case DMX_ABSOLUTE_CONFINED:
            dmxCoreMotion(pDev, v[0], v[1], -1, block);
            break;
        }
    }
}

static KeySym
dmxKeyCodeToKeySym(DMXLocalInputInfoPtr dmxLocal, KeyCode keyCode)
{
    KeySym keysym = NoSymbol;
    int effectiveGroup;
    XkbSrvInfoPtr xkbi;

    if (!dmxLocal || !dmxLocal->pDevice || !dmxLocal->pDevice->key)
        goto out;

    xkbi = dmxLocal->pDevice->key->xkbInfo;
    effectiveGroup = XkbGetEffectiveGroup(xkbi, &xkbi->state, keyCode);

    if (effectiveGroup == -1)
        goto out;

    keysym = XkbKeySym(xkbi->desc, keyCode, effectiveGroup);
    DMXDBG2("dmxKeyCodeToKeySym: Translated keyCode=%d to keySym=0x%04x\n",
            keyCode, keysym);

 out:
    return keysym;
}

static KeyCode
dmxKeySymToKeyCode(DMXLocalInputInfoPtr dmxLocal, KeySym keySym, int tryFirst)
{
    /* FIXME: this is quite ineffective, converting to a core map first and
     * then extracting the info from there. It'd be better to run the actual
     * xkb map */
    XkbSrvInfoPtr xkbi = dmxLocal->pDevice->key->xkbInfo;
    KeySymsPtr pKeySyms = XkbGetCoreMap(dmxLocal->pDevice);
    int i;

    /* Optimize for similar maps */
    if (XkbKeycodeInRange(xkbi->desc, tryFirst)
        && pKeySyms->map[(tryFirst - xkbi->desc->min_key_code)
                         * pKeySyms->mapWidth] == keySym)
        return tryFirst;

    for (i = pKeySyms->minKeyCode; i <= pKeySyms->maxKeyCode; i++) {
        if (pKeySyms->map[(i - pKeySyms->minKeyCode)
                          * pKeySyms->mapWidth] == keySym) {
            DMXDBG3("dmxKeySymToKeyCode: Translated keySym=0x%04x to"
                    " keyCode=%d (reverses to core keySym=0x%04x)\n",
                    keySym, i, dmxKeyCodeToKeySym(dmxLocalCoreKeyboard, i));
            return i;
        }
    }
    return 0;
}

static int
dmxFixup(DevicePtr pDev, int detail, KeySym keySym)
{
    GETDMXLOCALFROMPDEV;
    int keyCode;

    if (!dmxLocal->pDevice->key) {
        dmxLog(dmxWarning, "dmxFixup: not a keyboard device (%s)\n",
               dmxLocal->pDevice->name);
        return NoSymbol;
    }
    if (!keySym)
        keySym = dmxKeyCodeToKeySym(dmxLocal, detail);
    if (keySym == NoSymbol)
        return detail;
    keyCode = dmxKeySymToKeyCode(dmxLocalCoreKeyboard, keySym, detail);

    return keyCode ? keyCode : detail;
}

/** Enqueue an event from the \a pDev device with the
 * specified \a type and \a detail.  If the event is a KeyPress or
 * KeyRelease event, then the \a keySym is also specified.
 *
 * FIXME: make the code do what the comment says, or remove this comment.
 * If \a block is set to \a DMX_BLOCK, then the SIGIO handler will be
 * blocked around calls to dmxeqEnqueue(). */

void
dmxEnqueue(DevicePtr pDev, int type, int detail, KeySym keySym,
           XEvent * e, DMXBlockType block)
{
    GETDMXINPUTFROMPDEV;
    DeviceIntPtr p = dmxLocal->pDevice;
    int valuators[3];
    ValuatorMask mask;

    DMXDBG2("dmxEnqueue: Enqueuing type=%d detail=0x%0x\n", type, detail);

    switch (type) {
    case KeyPress:
    case KeyRelease:
        if (!keySym)
            keySym = dmxKeyCodeToKeySym(dmxLocal, detail);
        if (dmxCheckFunctionKeys(dmxLocal, type, keySym))
            return;
        if (dmxLocal->sendsCore && dmxLocal != dmxLocalCoreKeyboard)
            detail = dmxFixup(pDev, detail, keySym);

        /*ErrorF("KEY %d  sym %d\n", detail, (int) keySym); */
        QueueKeyboardEvents(p, type, detail);
        return;

    case ButtonPress:
    case ButtonRelease:
        detail = dmxGetButtonMapping(dmxLocal, detail);
        valuator_mask_zero(&mask);
        QueuePointerEvents(p, type, detail, 0, &mask);
        return;

    case MotionNotify:
        valuators[0] = e->xmotion.x;
        valuators[1] = e->xmotion.y;
        valuators[2] = e->xmotion.state;        /* FIXME: WTF?? */
        valuator_mask_set_range(&mask, 0, 3, valuators);
        QueuePointerEvents(p, type, detail,
                           POINTER_ABSOLUTE | POINTER_SCREEN, &mask);
        return;

    case EnterNotify:
    case LeaveNotify:
    case KeymapNotify:
    case MappingNotify:        /* This is sent because we change the
                                 * modifier map on the backend/console
                                 * input device so that we have complete
                                 * control of the input device LEDs. */
        return;
    default:
        if (type == ProximityIn || type == ProximityOut) {
            if (dmxLocal->sendsCore)
                return;         /* Not a core event */
            break;
        }
        if (type >= LASTEvent) {
            if (dmxTranslateAndEnqueueExtEvent(dmxLocal, e, block))
                dmxLogInput(dmxInput, "Unhandled extension event: %d\n", type);
        }
        else {
            dmxLogInput(dmxInput, "Unhandled event: %d (%s)\n",
                        type, dmxEventName(type));
        }
        return;
    }

}

/** A pointer to this routine is passed to low-level input drivers so
 * that all special keychecking is unified to this file.  This function
 * returns 0 if no special keys have been pressed.  If the user has
 * requested termination of the DMX server, -1 is returned.  If the user
 * has requested a switch to a VT, then the (1-based) number of that VT
 * is returned. */
int
dmxCheckSpecialKeys(DevicePtr pDev, KeySym keySym)
{
    GETDMXINPUTFROMPDEV;
    int vt = 0;
    unsigned short state = 0;

    if (dmxLocal->sendsCore)
        state =
            XkbStateFieldFromRec(&dmxLocalCoreKeyboard->pDevice->key->xkbInfo->
                                 state);
    else if (dmxLocal->pDevice->key)
        state = XkbStateFieldFromRec(&dmxLocal->pDevice->key->xkbInfo->state);

    if (!dmxLocal->sendsCore)
        return 0;               /* Only for core devices */

    DMXDBG2("dmxCheckSpecialKeys: keySym=0x%04x state=0x%04x\n", keySym, state);

    if ((state & (ControlMask | Mod1Mask)) != (ControlMask | Mod1Mask))
        return 0;

    switch (keySym) {
    case XK_F1:
    case XK_F2:
    case XK_F3:
    case XK_F4:
    case XK_F5:
    case XK_F6:
    case XK_F7:
    case XK_F8:
    case XK_F9:
    case XK_F10:
        vt = keySym - XK_F1 + 1;
        break;

    case XK_F11:
    case XK_F12:
        vt = keySym - XK_F11 + 11;
        break;

    case XK_q:                 /* To avoid confusion  */
    case XK_BackSpace:
    case XK_Delete:
    case XK_KP_Delete:
        dmxLog(dmxInfo, "User request for termination\n");
        dispatchException |= DE_TERMINATE;
        return -1;              /* Terminate */
    }

    if (vt) {
        dmxLog(dmxInfo, "Request to switch to VT %d\n", vt);
        dmxInput->vt_switch_pending = vt;
        return vt;
    }

    return 0;                   /* Do nothing */
}
