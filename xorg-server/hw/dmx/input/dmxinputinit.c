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
 * This file provides generic input support.  Functions here set up
 * input and lead to the calling of low-level device drivers for
 * input. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#define DMX_WINDOW_DEBUG 0

#include "dmxinputinit.h"
#include "dmxextension.h"       /* For dmxInputCount */

#include "dmxdummy.h"
#include "dmxbackend.h"
#include "dmxconsole.h"
#include "dmxcommon.h"
#include "dmxevents.h"
#include "dmxmotion.h"
#include "dmxprop.h"
#include "config/dmxconfig.h"
#include "dmxcursor.h"

#include "lnx-keyboard.h"
#include "lnx-ms.h"
#include "lnx-ps2.h"
#include "usb-keyboard.h"
#include "usb-mouse.h"
#include "usb-other.h"
#include "usb-common.h"

#include "dmxsigio.h"
#include "dmxarg.h"

#include "inputstr.h"
#include "input.h"
#include "mipointer.h"
#include "windowstr.h"
#include "mi.h"
#include "xkbsrv.h"

#include <X11/extensions/XI.h>
#include <X11/extensions/XIproto.h>
#include "exevents.h"
#include "extinit.h"

DMXLocalInputInfoPtr dmxLocalCorePointer, dmxLocalCoreKeyboard;

static DMXLocalInputInfoRec DMXDummyMou = {
    "dummy-mou", DMX_LOCAL_MOUSE, DMX_LOCAL_TYPE_LOCAL, 1,
    NULL, NULL, NULL, NULL, NULL, dmxDummyMouGetInfo
};

static DMXLocalInputInfoRec DMXDummyKbd = {
    "dummy-kbd", DMX_LOCAL_KEYBOARD, DMX_LOCAL_TYPE_LOCAL, 1,
    NULL, NULL, NULL, NULL, NULL, dmxDummyKbdGetInfo
};

static DMXLocalInputInfoRec DMXBackendMou = {
    "backend-mou", DMX_LOCAL_MOUSE, DMX_LOCAL_TYPE_BACKEND, 2,
    dmxBackendCreatePrivate, dmxBackendDestroyPrivate,
    dmxBackendInit, NULL, dmxBackendLateReInit, dmxBackendMouGetInfo,
    dmxCommonMouOn, dmxCommonMouOff, dmxBackendUpdatePosition,
    NULL, NULL, NULL,
    dmxBackendCollectEvents, dmxBackendProcessInput, dmxBackendFunctions, NULL,
    dmxCommonMouCtrl
};

static DMXLocalInputInfoRec DMXBackendKbd = {
    "backend-kbd", DMX_LOCAL_KEYBOARD, DMX_LOCAL_TYPE_BACKEND,
    1,                          /* With backend-mou or console-mou */
    dmxCommonCopyPrivate, NULL,
    dmxBackendInit, NULL, NULL, dmxBackendKbdGetInfo,
    dmxCommonKbdOn, dmxCommonKbdOff, NULL,
    NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, dmxCommonKbdCtrl, dmxCommonKbdBell
};

static DMXLocalInputInfoRec DMXConsoleMou = {
    "console-mou", DMX_LOCAL_MOUSE, DMX_LOCAL_TYPE_CONSOLE, 2,
    dmxConsoleCreatePrivate, dmxConsoleDestroyPrivate,
    dmxConsoleInit, dmxConsoleReInit, NULL, dmxConsoleMouGetInfo,
    dmxCommonMouOn, dmxCommonMouOff, dmxConsoleUpdatePosition,
    NULL, NULL, NULL,
    dmxConsoleCollectEvents, NULL, dmxConsoleFunctions, dmxConsoleUpdateInfo,
    dmxCommonMouCtrl
};

static DMXLocalInputInfoRec DMXConsoleKbd = {
    "console-kbd", DMX_LOCAL_KEYBOARD, DMX_LOCAL_TYPE_CONSOLE,
    1,                          /* With backend-mou or console-mou */
    dmxCommonCopyPrivate, NULL,
    dmxConsoleInit, dmxConsoleReInit, NULL, dmxConsoleKbdGetInfo,
    dmxCommonKbdOn, dmxCommonKbdOff, NULL,
    NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, dmxCommonKbdCtrl, dmxCommonKbdBell
};

static DMXLocalInputInfoRec DMXLocalDevices[] = {
    /* Dummy drivers that can compile on any OS */
#ifdef __linux__
    /* Linux-specific drivers */
    {
     "kbd", DMX_LOCAL_KEYBOARD, DMX_LOCAL_TYPE_LOCAL, 1,
     kbdLinuxCreatePrivate, kbdLinuxDestroyPrivate,
     kbdLinuxInit, NULL, NULL, kbdLinuxGetInfo,
     kbdLinuxOn, kbdLinuxOff, NULL,
     kbdLinuxVTPreSwitch, kbdLinuxVTPostSwitch, kbdLinuxVTSwitch,
     kbdLinuxRead, NULL, NULL, NULL,
     NULL, kbdLinuxCtrl, kbdLinuxBell},
    {
     "ms", DMX_LOCAL_MOUSE, DMX_LOCAL_TYPE_LOCAL, 1,
     msLinuxCreatePrivate, msLinuxDestroyPrivate,
     msLinuxInit, NULL, NULL, msLinuxGetInfo,
     msLinuxOn, msLinuxOff, NULL,
     msLinuxVTPreSwitch, msLinuxVTPostSwitch, NULL,
     msLinuxRead},
    {
     "ps2", DMX_LOCAL_MOUSE, DMX_LOCAL_TYPE_LOCAL, 1,
     ps2LinuxCreatePrivate, ps2LinuxDestroyPrivate,
     ps2LinuxInit, NULL, NULL, ps2LinuxGetInfo,
     ps2LinuxOn, ps2LinuxOff, NULL,
     ps2LinuxVTPreSwitch, ps2LinuxVTPostSwitch, NULL,
     ps2LinuxRead},
#endif
#ifdef __linux__
    /* USB drivers, currently only for
       Linux, but relatively easy to port to
       other OSs */
    {
     "usb-kbd", DMX_LOCAL_KEYBOARD, DMX_LOCAL_TYPE_LOCAL, 1,
     usbCreatePrivate, usbDestroyPrivate,
     kbdUSBInit, NULL, NULL, kbdUSBGetInfo,
     kbdUSBOn, usbOff, NULL,
     NULL, NULL, NULL,
     kbdUSBRead, NULL, NULL, NULL,
     NULL, kbdUSBCtrl},
    {
     "usb-mou", DMX_LOCAL_MOUSE, DMX_LOCAL_TYPE_LOCAL, 1,
     usbCreatePrivate, usbDestroyPrivate,
     mouUSBInit, NULL, NULL, mouUSBGetInfo,
     mouUSBOn, usbOff, NULL,
     NULL, NULL, NULL,
     mouUSBRead},
    {
     "usb-oth", DMX_LOCAL_OTHER, DMX_LOCAL_TYPE_LOCAL, 1,
     usbCreatePrivate, usbDestroyPrivate,
     othUSBInit, NULL, NULL, othUSBGetInfo,
     othUSBOn, usbOff, NULL,
     NULL, NULL, NULL,
     othUSBRead},
#endif
    {
     "dummy-mou", DMX_LOCAL_MOUSE, DMX_LOCAL_TYPE_LOCAL, 1,
     NULL, NULL, NULL, NULL, NULL, dmxDummyMouGetInfo},
    {
     "dummy-kbd", DMX_LOCAL_KEYBOARD, DMX_LOCAL_TYPE_LOCAL, 1,
     NULL, NULL, NULL, NULL, NULL, dmxDummyKbdGetInfo},
    {NULL}                      /* Must be last */
};

#if 11 /*BP*/
    void
DDXRingBell(int volume, int pitch, int duration)
{
    /* NO-OP */
}

/* taken from kdrive/src/kinput.c: */
static void
dmxKbdCtrl(DeviceIntPtr pDevice, KeybdCtrl * ctrl)
{
#if 0
    KdKeyboardInfo *ki;

    for (ki = kdKeyboards; ki; ki = ki->next) {
        if (ki->dixdev && ki->dixdev->id == pDevice->id)
            break;
    }

    if (!ki || !ki->dixdev || ki->dixdev->id != pDevice->id || !ki->driver)
        return;

    KdSetLeds(ki, ctrl->leds);
    ki->bellPitch = ctrl->bell_pitch;
    ki->bellDuration = ctrl->bell_duration;
#endif
}

/* taken from kdrive/src/kinput.c: */
static void
dmxBell(int volume, DeviceIntPtr pDev, void *arg, int something)
{
#if 0
    KeybdCtrl *ctrl = arg;
    KdKeyboardInfo *ki = NULL;

    for (ki = kdKeyboards; ki; ki = ki->next) {
        if (ki->dixdev && ki->dixdev->id == pDev->id)
            break;
    }

    if (!ki || !ki->dixdev || ki->dixdev->id != pDev->id || !ki->driver)
        return;

    KdRingBell(ki, volume, ctrl->bell_pitch, ctrl->bell_duration);
#endif
}

#endif /*BP*/
    static void
_dmxChangePointerControl(DMXLocalInputInfoPtr dmxLocal, PtrCtrl * ctrl)
{
    if (!dmxLocal)
        return;
    dmxLocal->mctrl = *ctrl;
    if (dmxLocal->mCtrl)
        dmxLocal->mCtrl(&dmxLocal->pDevice->public, ctrl);
}

/** Change the pointer control information for the \a pDevice.  If the
 * device sends core events, then also change the control information
 * for all of the pointer devices that send core events. */
void
dmxChangePointerControl(DeviceIntPtr pDevice, PtrCtrl * ctrl)
{
    GETDMXLOCALFROMPDEVICE;
    int i, j;

    if (dmxLocal->sendsCore) {  /* Do for all core devices */
        for (i = 0; i < dmxNumInputs; i++) {
            DMXInputInfo *dmxInput = &dmxInputs[i];

            if (dmxInput->detached)
                continue;
            for (j = 0; j < dmxInput->numDevs; j++)
                if (dmxInput->devs[j]->sendsCore)
                    _dmxChangePointerControl(dmxInput->devs[j], ctrl);
        }
    }
    else {                      /* Do for this device only */
        _dmxChangePointerControl(dmxLocal, ctrl);
    }
}

static void
_dmxKeyboardKbdCtrlProc(DMXLocalInputInfoPtr dmxLocal, KeybdCtrl * ctrl)
{
    dmxLocal->kctrl = *ctrl;
    if (dmxLocal->kCtrl) {
        dmxLocal->kCtrl(&dmxLocal->pDevice->public, ctrl);
        if (dmxLocal->pDevice->kbdfeed) {
            XkbEventCauseRec cause;

            XkbSetCauseUnknown(&cause);
            /* Generate XKB events, as necessary */
            XkbUpdateIndicators(dmxLocal->pDevice, XkbAllIndicatorsMask, False,
                                NULL, &cause);
        }
    }
}

/** Change the keyboard control information for the \a pDevice.  If the
 * device sends core events, then also change the control information
 * for all of the keyboard devices that send core events. */
void
dmxKeyboardKbdCtrlProc(DeviceIntPtr pDevice, KeybdCtrl * ctrl)
{
    GETDMXLOCALFROMPDEVICE;
    int i, j;

    if (dmxLocal->sendsCore) {  /* Do for all core devices */
        for (i = 0; i < dmxNumInputs; i++) {
            DMXInputInfo *dmxInput = &dmxInputs[i];

            if (dmxInput->detached)
                continue;
            for (j = 0; j < dmxInput->numDevs; j++)
                if (dmxInput->devs[j]->sendsCore)
                    _dmxKeyboardKbdCtrlProc(dmxInput->devs[j], ctrl);
        }
    }
    else {                      /* Do for this device only */
        _dmxKeyboardKbdCtrlProc(dmxLocal, ctrl);
    }
}

static void
_dmxKeyboardBellProc(DMXLocalInputInfoPtr dmxLocal, int percent)
{
    if (dmxLocal->kBell)
        dmxLocal->kBell(&dmxLocal->pDevice->public,
                        percent,
                        dmxLocal->kctrl.bell,
                        dmxLocal->kctrl.bell_pitch,
                        dmxLocal->kctrl.bell_duration);
}

/** Sound the bell on the device.  If the device send core events, then
 * sound the bell on all of the devices that send core events. */
void
dmxKeyboardBellProc(int percent, DeviceIntPtr pDevice,
                    void *ctrl, int unknown)
{
    GETDMXLOCALFROMPDEVICE;
    int i, j;

    if (dmxLocal->sendsCore) {  /* Do for all core devices */
        for (i = 0; i < dmxNumInputs; i++) {
            DMXInputInfo *dmxInput = &dmxInputs[i];

            if (dmxInput->detached)
                continue;
            for (j = 0; j < dmxInput->numDevs; j++)
                if (dmxInput->devs[j]->sendsCore)
                    _dmxKeyboardBellProc(dmxInput->devs[j], percent);
        }
    }
    else {                      /* Do for this device only */
        _dmxKeyboardBellProc(dmxLocal, percent);
    }
}

static void
dmxKeyboardFreeNames(XkbComponentNamesPtr names)
{
    if (names->keycodes)
        XFree(names->keycodes);
    if (names->types)
        XFree(names->types);
    if (names->compat)
        XFree(names->compat);
    if (names->symbols)
        XFree(names->symbols);
    if (names->geometry)
        XFree(names->geometry);
}

static int
dmxKeyboardOn(DeviceIntPtr pDevice, DMXLocalInitInfo * info)
{
    GETDMXINPUTFROMPDEVICE;
    XkbRMLVOSet rmlvo;

    rmlvo.rules = dmxConfigGetXkbRules();
    rmlvo.model = dmxConfigGetXkbModel();
    rmlvo.layout = dmxConfigGetXkbLayout();
    rmlvo.variant = dmxConfigGetXkbVariant();
    rmlvo.options = dmxConfigGetXkbOptions();

    XkbSetRulesDflts(&rmlvo);
    if (!info->force && (dmxInput->keycodes
                         || dmxInput->symbols || dmxInput->geometry)) {
        if (info->freenames)
            dmxKeyboardFreeNames(&info->names);
        info->freenames = 0;
        info->names.keycodes = dmxInput->keycodes;
        info->names.types = NULL;
        info->names.compat = NULL;
        info->names.symbols = dmxInput->symbols;
        info->names.geometry = dmxInput->geometry;

        dmxLogInput(dmxInput, "XKEYBOARD: From command line: %s",
                    info->names.keycodes);
        if (info->names.symbols && *info->names.symbols)
            dmxLogInputCont(dmxInput, " %s", info->names.symbols);
        if (info->names.geometry && *info->names.geometry)
            dmxLogInputCont(dmxInput, " %s", info->names.geometry);
        dmxLogInputCont(dmxInput, "\n");
    }
    else if (info->names.keycodes) {
        dmxLogInput(dmxInput, "XKEYBOARD: From device: %s",
                    info->names.keycodes);
        if (info->names.symbols && *info->names.symbols)
            dmxLogInputCont(dmxInput, " %s", info->names.symbols);
        if (info->names.geometry && *info->names.geometry)
            dmxLogInputCont(dmxInput, " %s", info->names.geometry);
        dmxLogInputCont(dmxInput, "\n");
    }
    else {
        dmxLogInput(dmxInput, "XKEYBOARD: Defaults: %s %s %s %s %s\n",
                    dmxConfigGetXkbRules(),
                    dmxConfigGetXkbLayout(),
                    dmxConfigGetXkbModel(), dmxConfigGetXkbVariant()
                    ? dmxConfigGetXkbVariant() : "", dmxConfigGetXkbOptions()
                    ? dmxConfigGetXkbOptions() : "");
    }
    InitKeyboardDeviceStruct(pDevice, &rmlvo,
                             dmxKeyboardBellProc, dmxKeyboardKbdCtrlProc);

    if (info->freenames)
        dmxKeyboardFreeNames(&info->names);

    return Success;
}

static int
dmxDeviceOnOff(DeviceIntPtr pDevice, int what)
{
    GETDMXINPUTFROMPDEVICE;
    int fd;
    DMXLocalInitInfo info;
    int i;
    Atom btn_labels[MAX_BUTTONS] = { 0 };       /* FIXME */
    Atom axis_labels[MAX_VALUATORS] = { 0 };    /* FIXME */

    if (dmxInput->detached)
        return Success;

    memset(&info, 0, sizeof(info));
    switch (what) {
    case DEVICE_INIT:
        if (dmxLocal->init)
            dmxLocal->init(pDev);
        if (dmxLocal->get_info)
            dmxLocal->get_info(pDev, &info);
        if (info.keyboard) {    /* XKEYBOARD makes this a special case */
            dmxKeyboardOn(pDevice, &info);
            break;
        }
        if (info.keyClass) {
            XkbRMLVOSet rmlvo;

            rmlvo.rules = dmxConfigGetXkbRules();
            rmlvo.model = dmxConfigGetXkbModel();
            rmlvo.layout = dmxConfigGetXkbLayout();
            rmlvo.variant = dmxConfigGetXkbVariant();
            rmlvo.options = dmxConfigGetXkbOptions();

            InitKeyboardDeviceStruct(pDevice, &rmlvo, dmxBell, dmxKbdCtrl);
        }
        if (info.buttonClass) {
            InitButtonClassDeviceStruct(pDevice, info.numButtons,
                                        btn_labels, info.map);
        }
        if (info.valuatorClass) {
            if (info.numRelAxes && dmxLocal->sendsCore) {
                InitValuatorClassDeviceStruct(pDevice, info.numRelAxes,
                                              axis_labels,
                                              GetMaximumEventsNum(), Relative);
                for (i = 0; i < info.numRelAxes; i++)
                    InitValuatorAxisStruct(pDevice, i, axis_labels[i],
                                           info.minval[i], info.maxval[i],
                                           info.res[i],
                                           info.minres[i], info.maxres[i],
                                           Relative);
            }
            else if (info.numRelAxes) {
                InitValuatorClassDeviceStruct(pDevice, info.numRelAxes,
                                              axis_labels,
                                              dmxPointerGetMotionBufferSize(),
                                              Relative);
                for (i = 0; i < info.numRelAxes; i++)
                    InitValuatorAxisStruct(pDevice, i, axis_labels[i],
                                           info.minval[i],
                                           info.maxval[i], info.res[i],
                                           info.minres[i], info.maxres[i],
                                           Relative);
            }
            else if (info.numAbsAxes) {
                InitValuatorClassDeviceStruct(pDevice, info.numAbsAxes,
                                              axis_labels,
                                              dmxPointerGetMotionBufferSize(),
                                              Absolute);
                for (i = 0; i < info.numAbsAxes; i++)
                    InitValuatorAxisStruct(pDevice, i,
                                           axis_labels[i],
                                           info.minval[i], info.maxval[i],
                                           info.res[i], info.minres[i],
                                           info.maxres[i], Absolute);
            }
        }
        if (info.focusClass)
            InitFocusClassDeviceStruct(pDevice);
        if (info.proximityClass)
            InitProximityClassDeviceStruct(pDevice);
        if (info.ptrFeedbackClass)
            InitPtrFeedbackClassDeviceStruct(pDevice, dmxChangePointerControl);
        if (info.intFeedbackClass || info.strFeedbackClass)
            dmxLog(dmxWarning,
                   "Integer and string feedback not supported for %s\n",
                   pDevice->name);
        if (!info.keyboard && (info.ledFeedbackClass || info.belFeedbackClass))
            dmxLog(dmxWarning,
                   "Led and bel feedback not supported for non-keyboard %s\n",
                   pDevice->name);
        break;
    case DEVICE_ON:
        if (!pDev->on) {
            if (dmxLocal->on && (fd = dmxLocal->on(pDev)) >= 0)
                dmxSigioRegister(dmxInput, fd);
            pDev->on = TRUE;
        }
        break;
    case DEVICE_OFF:
    case DEVICE_CLOSE:
        /* This can get called twice consecutively: once for a
         * detached screen (DEVICE_OFF), and then again at server
         * generation time (DEVICE_CLOSE). */
        if (pDev->on) {
            dmxSigioUnregister(dmxInput);
            if (dmxLocal->off)
                dmxLocal->off(pDev);
            pDev->on = FALSE;
        }
        break;
    }
    if (info.keySyms.map && info.freemap) {
        XFree(info.keySyms.map);
        info.keySyms.map = NULL;
    }
    if (info.xkb)
        XkbFreeKeyboard(info.xkb, 0, True);
    return Success;
}

static void
dmxProcessInputEvents(DMXInputInfo * dmxInput)
{
    int i;

    mieqProcessInputEvents();
#if 00 /*BP*/
        miPointerUpdate();
#endif
    if (dmxInput->detached)
        return;
    for (i = 0; i < dmxInput->numDevs; i += dmxInput->devs[i]->binding)
        if (dmxInput->devs[i]->process_input) {
            dmxInput->devs[i]->process_input(dmxInput->devs[i]->private);
        }

#if 11 /*BP*/
        mieqProcessInputEvents();
#endif
}

static void
dmxUpdateWindowInformation(DMXInputInfo * dmxInput,
                           DMXUpdateType type, WindowPtr pWindow)
{
    int i;

#ifdef PANORAMIX
    if (!noPanoramiXExtension && pWindow &&
        pWindow->parent != screenInfo.screens[0]->root)
        return;
#endif
#if DMX_WINDOW_DEBUG
    {
        const char *name = "Unknown";

        switch (type) {
        case DMX_UPDATE_REALIZE:
            name = "Realize";
            break;
        case DMX_UPDATE_UNREALIZE:
            name = "Unrealize";
            break;
        case DMX_UPDATE_RESTACK:
            name = "Restack";
            break;
        case DMX_UPDATE_COPY:
            name = "Copy";
            break;
        case DMX_UPDATE_RESIZE:
            name = "Resize";
            break;
        case DMX_UPDATE_REPARENT:
            name = "Repaint";
            break;
        }
        dmxLog(dmxDebug, "Window %p changed: %s\n", pWindow, name);
    }
#endif

    if (dmxInput->detached)
        return;
    for (i = 0; i < dmxInput->numDevs; i += dmxInput->devs[i]->binding)
        if (dmxInput->devs[i]->update_info)
            dmxInput->devs[i]->update_info(dmxInput->devs[i]->private,
                                           type, pWindow);
}

static void
dmxCollectAll(DMXInputInfo * dmxInput)
{
    int i;

    if (dmxInput->detached)
        return;
    for (i = 0; i < dmxInput->numDevs; i += dmxInput->devs[i]->binding)
        if (dmxInput->devs[i]->collect_events)
            dmxInput->devs[i]->collect_events(&dmxInput->devs[i]->pDevice->
                                              public, dmxMotion, dmxEnqueue,
                                              dmxCheckSpecialKeys, DMX_BLOCK);
}

static void
dmxBlockHandler(void *blockData, OSTimePtr pTimeout, void *pReadMask)
{
    DMXInputInfo *dmxInput = &dmxInputs[(uintptr_t) blockData];
    static unsigned long generation = 0;

    if (generation != serverGeneration) {
        generation = serverGeneration;
        dmxCollectAll(dmxInput);
    }
}

static void
dmxSwitchReturn(void *p)
{
    DMXInputInfo *dmxInput = p;
    int i;

    dmxLog(dmxInfo, "Returning from VT %d\n", dmxInput->vt_switched);

    if (!dmxInput->vt_switched)
        dmxLog(dmxFatal, "dmxSwitchReturn called, but not switched\n");
    dmxSigioEnableInput();
    for (i = 0; i < dmxInput->numDevs; i++)
        if (dmxInput->devs[i]->vt_post_switch)
            dmxInput->devs[i]->vt_post_switch(dmxInput->devs[i]->private);
    dmxInput->vt_switched = 0;
}

static void
dmxWakeupHandler(void *blockData, int result, void *pReadMask)
{
    DMXInputInfo *dmxInput = &dmxInputs[(uintptr_t) blockData];
    int i;

    if (dmxInput->vt_switch_pending) {
        dmxLog(dmxInfo, "Switching to VT %d\n", dmxInput->vt_switch_pending);
        for (i = 0; i < dmxInput->numDevs; i++)
            if (dmxInput->devs[i]->vt_pre_switch)
                dmxInput->devs[i]->vt_pre_switch(dmxInput->devs[i]->private);
        dmxInput->vt_switched = dmxInput->vt_switch_pending;
        dmxInput->vt_switch_pending = 0;
        for (i = 0; i < dmxInput->numDevs; i++) {
            if (dmxInput->devs[i]->vt_switch) {
                dmxSigioDisableInput();
                if (!dmxInput->devs[i]->vt_switch(dmxInput->devs[i]->private,
                                                  dmxInput->vt_switched,
                                                  dmxSwitchReturn, dmxInput))
                    dmxSwitchReturn(dmxInput);
                break;          /* Only call one vt_switch routine */
            }
        }
    }
    dmxCollectAll(dmxInput);
}

static char *
dmxMakeUniqueDeviceName(DMXLocalInputInfoPtr dmxLocal)
{
    static int k = 0;
    static int m = 0;
    static int o = 0;
    static unsigned long dmxGeneration = 0;

#define LEN  32
    char *buf = malloc(LEN);

    if (dmxGeneration != serverGeneration) {
        k = m = o = 0;
        dmxGeneration = serverGeneration;
    }

    switch (dmxLocal->type) {
    case DMX_LOCAL_KEYBOARD:
        snprintf(buf, LEN, "Keyboard%d", k++);
        break;
    case DMX_LOCAL_MOUSE:
        snprintf(buf, LEN, "Mouse%d", m++);
        break;
    default:
        snprintf(buf, LEN, "Other%d", o++);
        break;
    }

    return buf;
}

static DeviceIntPtr
dmxAddDevice(DMXLocalInputInfoPtr dmxLocal)
{
    DeviceIntPtr pDevice;
    Atom atom;
    const char *name = NULL;
    char *devname;
    DMXInputInfo *dmxInput;

    if (!dmxLocal)
        return NULL;
    dmxInput = &dmxInputs[dmxLocal->inputIdx];

    if (dmxLocal->sendsCore) {
        if (dmxLocal->type == DMX_LOCAL_KEYBOARD && !dmxLocalCoreKeyboard) {
            dmxLocal->isCore = 1;
            dmxLocalCoreKeyboard = dmxLocal;
            name = "keyboard";
        }
        if (dmxLocal->type == DMX_LOCAL_MOUSE && !dmxLocalCorePointer) {
            dmxLocal->isCore = 1;
            dmxLocalCorePointer = dmxLocal;
            name = "pointer";
        }
    }

    if (!name) {
        name = "extension";
    }

    if (!name)
        dmxLog(dmxFatal, "Cannot add device %s\n", dmxLocal->name);

    pDevice = AddInputDevice(serverClient, dmxDeviceOnOff, TRUE);
    if (!pDevice) {
        dmxLog(dmxError, "Too many devices -- cannot add device %s\n",
               dmxLocal->name);
        return NULL;
    }
    pDevice->public.devicePrivate = dmxLocal;
    dmxLocal->pDevice = pDevice;

    devname = dmxMakeUniqueDeviceName(dmxLocal);
    atom = MakeAtom((char *) devname, strlen(devname), TRUE);
    pDevice->type = atom;
    pDevice->name = devname;

    if (dmxLocal->isCore && dmxLocal->type == DMX_LOCAL_MOUSE) {
#if 00   /*BP*/
            miRegisterPointerDevice(screenInfo.screens[0], pDevice);
#else
        /* Nothing? dmxDeviceOnOff() should get called to init, right? */
#endif
    }

    if (dmxLocal->create_private)
        dmxLocal->private = dmxLocal->create_private(pDevice);

    dmxLogInput(dmxInput, "Added %s as %s device called %s%s\n",
                dmxLocal->name, name, devname,
                dmxLocal->isCore
                ? " [core]"
                : (dmxLocal->sendsCore ? " [sends core events]" : ""));

    return pDevice;
}

static DMXLocalInputInfoPtr
dmxLookupLocal(const char *name)
{
    DMXLocalInputInfoPtr pt;

    for (pt = &DMXLocalDevices[0]; pt->name; ++pt)
        if (!strcmp(pt->name, name))
            return pt;          /* search for device name */
    return NULL;
}

/** Copy the local input information from \a s into a new \a devs slot
 * in \a dmxInput. */
DMXLocalInputInfoPtr
dmxInputCopyLocal(DMXInputInfo * dmxInput, DMXLocalInputInfoPtr s)
{
    DMXLocalInputInfoPtr dmxLocal = malloc(sizeof(*dmxLocal));

    if (!dmxLocal)
        dmxLog(dmxFatal, "DMXLocalInputInfoPtr: out of memory\n");

    memcpy(dmxLocal, s, sizeof(*dmxLocal));
    dmxLocal->inputIdx = dmxInput->inputIdx;
    dmxLocal->sendsCore = dmxInput->core;
    dmxLocal->savedSendsCore = dmxInput->core;
    dmxLocal->deviceId = -1;

    ++dmxInput->numDevs;
    dmxInput->devs = reallocarray(dmxInput->devs,
                                  dmxInput->numDevs, sizeof(*dmxInput->devs));
    dmxInput->devs[dmxInput->numDevs - 1] = dmxLocal;

    return dmxLocal;
}

static void
dmxPopulateLocal(DMXInputInfo * dmxInput, dmxArg a)
{
    int i;
    int help = 0;
    DMXLocalInputInfoRec *pt;

    for (i = 1; i < dmxArgC(a); i++) {
        const char *name = dmxArgV(a, i);

        if ((pt = dmxLookupLocal(name))) {
            dmxInputCopyLocal(dmxInput, pt);
        }
        else {
            if (strlen(name))
                dmxLog(dmxWarning, "Could not find a driver called %s\n", name);
            ++help;
        }
    }
    if (help) {
        dmxLog(dmxInfo, "Available local device drivers:\n");
        for (pt = &DMXLocalDevices[0]; pt->name; ++pt) {
            const char *type;

            switch (pt->type) {
            case DMX_LOCAL_KEYBOARD:
                type = "keyboard";
                break;
            case DMX_LOCAL_MOUSE:
                type = "pointer";
                break;
            default:
                type = "unknown";
                break;
            }
            dmxLog(dmxInfo, "   %s (%s)\n", pt->name, type);
        }
        dmxLog(dmxFatal, "Must have valid local device driver\n");
    }
}

int
dmxInputExtensionErrorHandler(Display * dsp, _Xconst char *name,
                              _Xconst char *reason)
{
    return 0;
}

static void
dmxInputScanForExtensions(DMXInputInfo * dmxInput, int doXI)
{
    XExtensionVersion *ext;
    XDeviceInfo *devices;
    Display *dsp;
    int num;
    int i, j;
    XextErrorHandler handler;

    if (!(dsp = XOpenDisplay(dmxInput->name)))
        return;

    /* Print out information about the XInput Extension. */
    handler = XSetExtensionErrorHandler(dmxInputExtensionErrorHandler);
    ext = XGetExtensionVersion(dsp, INAME);
    XSetExtensionErrorHandler(handler);

    if (!ext || ext == (XExtensionVersion *) NoSuchExtension) {
        dmxLogInput(dmxInput, "%s is not available\n", INAME);
    }
    else {
        dmxLogInput(dmxInput, "Locating devices on %s (%s version %d.%d)\n",
                    dmxInput->name, INAME,
                    ext->major_version, ext->minor_version);
        devices = XListInputDevices(dsp, &num);

        XFree(ext);
        ext = NULL;

        /* Print a list of all devices */
        for (i = 0; i < num; i++) {
            const char *use = "Unknown";

            switch (devices[i].use) {
            case IsXPointer:
                use = "XPointer";
                break;
            case IsXKeyboard:
                use = "XKeyboard";
                break;
            case IsXExtensionDevice:
                use = "XExtensionDevice";
                break;
            case IsXExtensionPointer:
                use = "XExtensionPointer";
                break;
            case IsXExtensionKeyboard:
                use = "XExtensionKeyboard";
                break;
            }
            dmxLogInput(dmxInput, "  %2d %-10.10s %-16.16s\n",
                        (int) devices[i].id,
                        devices[i].name ? devices[i].name : "", use);
        }

        /* Search for extensions */
        for (i = 0; i < num; i++) {
            switch (devices[i].use) {
            case IsXKeyboard:
                for (j = 0; j < dmxInput->numDevs; j++) {
                    DMXLocalInputInfoPtr dmxL = dmxInput->devs[j];

                    if (dmxL->type == DMX_LOCAL_KEYBOARD && dmxL->deviceId < 0) {
                        dmxL->deviceId = devices[i].id;
                        dmxL->deviceName = (devices[i].name
                                            ? strdup(devices[i].name)
                                            : NULL);
                    }
                }
                break;
            case IsXPointer:
                for (j = 0; j < dmxInput->numDevs; j++) {
                    DMXLocalInputInfoPtr dmxL = dmxInput->devs[j];

                    if (dmxL->type == DMX_LOCAL_MOUSE && dmxL->deviceId < 0) {
                        dmxL->deviceId = devices[i].id;
                        dmxL->deviceName = (devices[i].name
                                            ? xstrdup(devices[i].name)
                                            : NULL);
                    }
                }
                break;
            }
        }
        XFreeDeviceList(devices);
    }
    XCloseDisplay(dsp);
}

/** Re-initialize all the devices described in \a dmxInput.  Called from
    #dmxAdjustCursorBoundaries before the cursor is redisplayed. */
void
dmxInputReInit(DMXInputInfo * dmxInput)
{
    int i;

    for (i = 0; i < dmxInput->numDevs; i++) {
        DMXLocalInputInfoPtr dmxLocal = dmxInput->devs[i];

        if (dmxLocal->reinit)
            dmxLocal->reinit(&dmxLocal->pDevice->public);
    }
}

/** Re-initialize all the devices described in \a dmxInput.  Called from
    #dmxAdjustCursorBoundaries after the cursor is redisplayed. */
void
dmxInputLateReInit(DMXInputInfo * dmxInput)
{
    int i;

    for (i = 0; i < dmxInput->numDevs; i++) {
        DMXLocalInputInfoPtr dmxLocal = dmxInput->devs[i];

        if (dmxLocal->latereinit)
            dmxLocal->latereinit(&dmxLocal->pDevice->public);
    }
}

/** Initialize all of the devices described in \a dmxInput. */
void
dmxInputInit(DMXInputInfo * dmxInput)
{
    dmxArg a;
    const char *name;
    int i;
    int doXI = 1;               /* Include by default */
    int forceConsole = 0;
    int doWindows = 1;          /* On by default */
    int hasXkb = 0;

    a = dmxArgParse(dmxInput->name);

    for (i = 1; i < dmxArgC(a); i++) {
        switch (hasXkb) {
        case 1:
            dmxInput->keycodes = xstrdup(dmxArgV(a, i));
            ++hasXkb;
            break;
        case 2:
            dmxInput->symbols = xstrdup(dmxArgV(a, i));
            ++hasXkb;
            break;
        case 3:
            dmxInput->geometry = xstrdup(dmxArgV(a, i));
            hasXkb = 0;
            break;
        case 0:
            if (!strcmp(dmxArgV(a, i), "noxi"))
                doXI = 0;
            else if (!strcmp(dmxArgV(a, i), "xi"))
                doXI = 1;
            else if (!strcmp(dmxArgV(a, i), "console"))
                forceConsole = 1;
            else if (!strcmp(dmxArgV(a, i), "noconsole"))
                forceConsole = 0;
            else if (!strcmp(dmxArgV(a, i), "windows"))
                doWindows = 1;
            else if (!strcmp(dmxArgV(a, i), "nowindows"))
                doWindows = 0;
            else if (!strcmp(dmxArgV(a, i), "xkb"))
                hasXkb = 1;
            else {
                dmxLog(dmxFatal, "Unknown input argument: %s\n", dmxArgV(a, i));
            }
        }
    }

    name = dmxArgV(a, 0);

    if (!strcmp(name, "local")) {
        dmxPopulateLocal(dmxInput, a);
    }
    else if (!strcmp(name, "dummy")) {
        dmxInputCopyLocal(dmxInput, &DMXDummyMou);
        dmxInputCopyLocal(dmxInput, &DMXDummyKbd);
        dmxLogInput(dmxInput, "Using dummy input\n");
    }
    else {
        int found;

        for (found = 0, i = 0; i < dmxNumScreens; i++) {
            if (dmxPropertySameDisplay(&dmxScreens[i], name)) {
                if (dmxScreens[i].shared)
                    dmxLog(dmxFatal,
                           "Cannot take input from shared backend (%s)\n",
                           name);
                if (!dmxInput->core) {
                    dmxLog(dmxWarning,
                           "Cannot use core devices on a backend (%s)"
                           " as XInput devices\n", name);
                }
                else {
                    char *pt;

                    for (pt = (char *) dmxInput->name; pt && *pt; pt++)
                        if (*pt == ',')
                            *pt = '\0';
                    dmxInputCopyLocal(dmxInput, &DMXBackendMou);
                    dmxInputCopyLocal(dmxInput, &DMXBackendKbd);
                    dmxInput->scrnIdx = i;
                    dmxLogInput(dmxInput,
                                "Using backend input from %s\n", name);
                }
                ++found;
                break;
            }
        }
        if (!found || forceConsole) {
            char *pt;

            if (found)
                dmxInput->console = TRUE;
            for (pt = (char *) dmxInput->name; pt && *pt; pt++)
                if (*pt == ',')
                    *pt = '\0';
            dmxInputCopyLocal(dmxInput, &DMXConsoleMou);
            dmxInputCopyLocal(dmxInput, &DMXConsoleKbd);
            if (doWindows) {
                dmxInput->windows = TRUE;
                dmxInput->updateWindowInfo = dmxUpdateWindowInformation;
            }
            dmxLogInput(dmxInput,
                        "Using console input from %s (%s windows)\n",
                        name, doWindows ? "with" : "without");
        }
    }

    dmxArgFree(a);

    /* Locate extensions we may be interested in */
    dmxInputScanForExtensions(dmxInput, doXI);

    for (i = 0; i < dmxInput->numDevs; i++) {
        DMXLocalInputInfoPtr dmxLocal = dmxInput->devs[i];

        dmxLocal->pDevice = dmxAddDevice(dmxLocal);
    }

    dmxInput->processInputEvents = dmxProcessInputEvents;
    dmxInput->detached = False;

    RegisterBlockAndWakeupHandlers(dmxBlockHandler, dmxWakeupHandler,
                                   (void *) (uintptr_t) dmxInput->inputIdx);
}

static void
dmxInputFreeLocal(DMXLocalInputInfoRec * local)
{
    if (!local)
        return;
    if (local->isCore && local->type == DMX_LOCAL_MOUSE)
        dmxLocalCorePointer = NULL;
    if (local->isCore && local->type == DMX_LOCAL_KEYBOARD)
        dmxLocalCoreKeyboard = NULL;
    if (local->destroy_private)
        local->destroy_private(local->private);
    free(local->history);
    free(local->valuators);
    free((void *) local->deviceName);
    local->private = NULL;
    local->history = NULL;
    local->deviceName = NULL;
    free(local);
}

/** Free all of the memory associated with \a dmxInput */
void
dmxInputFree(DMXInputInfo * dmxInput)
{
    int i;

    if (!dmxInput)
        return;

    free(dmxInput->keycodes);
    free(dmxInput->symbols);
    free(dmxInput->geometry);

    for (i = 0; i < dmxInput->numDevs; i++) {
        dmxInputFreeLocal(dmxInput->devs[i]);
        dmxInput->devs[i] = NULL;
    }
    free(dmxInput->devs);
    dmxInput->devs = NULL;
    dmxInput->numDevs = 0;
    if (dmxInput->freename)
        free((void *) dmxInput->name);
    dmxInput->name = NULL;
}

/** Log information about all of the known devices using #dmxLog(). */
void
dmxInputLogDevices(void)
{
    int i, j;

    dmxLog(dmxInfo, "%d devices:\n", dmxGetInputCount());
    dmxLog(dmxInfo, "  Id  Name                 Classes\n");
    for (j = 0; j < dmxNumInputs; j++) {
        DMXInputInfo *dmxInput = &dmxInputs[j];
        const char *pt = strchr(dmxInput->name, ',');
        int len = (pt ? (size_t) (pt - dmxInput->name)
                   : strlen(dmxInput->name));

        for (i = 0; i < dmxInput->numDevs; i++) {
            DeviceIntPtr pDevice = dmxInput->devs[i]->pDevice;

            if (pDevice) {
                dmxLog(dmxInfo, "  %2d%c %-20.20s",
                       pDevice->id,
                       dmxInput->detached ? 'D' : ' ', pDevice->name);
                if (pDevice->key)
                    dmxLogCont(dmxInfo, " key");
                if (pDevice->valuator)
                    dmxLogCont(dmxInfo, " val");
                if (pDevice->button)
                    dmxLogCont(dmxInfo, " btn");
                if (pDevice->focus)
                    dmxLogCont(dmxInfo, " foc");
                if (pDevice->kbdfeed)
                    dmxLogCont(dmxInfo, " fb/kbd");
                if (pDevice->ptrfeed)
                    dmxLogCont(dmxInfo, " fb/ptr");
                if (pDevice->intfeed)
                    dmxLogCont(dmxInfo, " fb/int");
                if (pDevice->stringfeed)
                    dmxLogCont(dmxInfo, " fb/str");
                if (pDevice->bell)
                    dmxLogCont(dmxInfo, " fb/bel");
                if (pDevice->leds)
                    dmxLogCont(dmxInfo, " fb/led");
                if (!pDevice->key && !pDevice->valuator && !pDevice->button
                    && !pDevice->focus && !pDevice->kbdfeed
                    && !pDevice->ptrfeed && !pDevice->intfeed
                    && !pDevice->stringfeed && !pDevice->bell && !pDevice->leds)
                    dmxLogCont(dmxInfo, " (none)");

                dmxLogCont(dmxInfo, "\t[i%d/%*.*s",
                           dmxInput->inputIdx, len, len, dmxInput->name);
                if (dmxInput->devs[i]->deviceId >= 0)
                    dmxLogCont(dmxInfo, "/id%d", (int) dmxInput->devs[i]->deviceId);
                if (dmxInput->devs[i]->deviceName)
                    dmxLogCont(dmxInfo, "=%s", dmxInput->devs[i]->deviceName);
                dmxLogCont(dmxInfo, "] %s\n",
                           dmxInput->devs[i]->isCore
                           ? "core"
                           : (dmxInput->devs[i]->sendsCore
                              ? "extension (sends core events)" : "extension"));
            }
        }
    }
}

/** Detach an input */
int
dmxInputDetach(DMXInputInfo * dmxInput)
{
    int i;

    if (dmxInput->detached)
        return BadAccess;

    for (i = 0; i < dmxInput->numDevs; i++) {
        DMXLocalInputInfoPtr dmxLocal = dmxInput->devs[i];

        dmxLogInput(dmxInput, "Detaching device id %d: %s%s\n",
                    dmxLocal->pDevice->id,
                    dmxLocal->pDevice->name,
                    dmxLocal->isCore
                    ? " [core]"
                    : (dmxLocal->sendsCore ? " [sends core events]" : ""));
        DisableDevice(dmxLocal->pDevice, TRUE);
    }
    dmxInput->detached = True;
    dmxInputLogDevices();
    return 0;
}

/** Search for input associated with \a dmxScreen, and detach. */
void
dmxInputDetachAll(DMXScreenInfo * dmxScreen)
{
    int i;

    for (i = 0; i < dmxNumInputs; i++) {
        DMXInputInfo *dmxInput = &dmxInputs[i];

        if (dmxInput->scrnIdx == dmxScreen->index)
            dmxInputDetach(dmxInput);
    }
}

/** Search for input associated with \a deviceId, and detach. */
int
dmxInputDetachId(int id)
{
    DMXInputInfo *dmxInput = dmxInputLocateId(id);

    if (!dmxInput)
        return BadValue;

    return dmxInputDetach(dmxInput);
}

DMXInputInfo *
dmxInputLocateId(int id)
{
    int i, j;

    for (i = 0; i < dmxNumInputs; i++) {
        DMXInputInfo *dmxInput = &dmxInputs[i];

        for (j = 0; j < dmxInput->numDevs; j++) {
            DMXLocalInputInfoPtr dmxLocal = dmxInput->devs[j];

            if (dmxLocal->pDevice->id == id)
                return dmxInput;
        }
    }
    return NULL;
}

static int
dmxInputAttachNew(DMXInputInfo * dmxInput, int *id)
{
    dmxInputInit(dmxInput);
    InitAndStartDevices();
    if (id && dmxInput->devs)
        *id = dmxInput->devs[0]->pDevice->id;
    dmxInputLogDevices();
    return 0;
}

static int
dmxInputAttachOld(DMXInputInfo * dmxInput, int *id)
{
    int i;

    dmxInput->detached = False;
    for (i = 0; i < dmxInput->numDevs; i++) {
        DMXLocalInputInfoPtr dmxLocal = dmxInput->devs[i];

        if (id)
            *id = dmxLocal->pDevice->id;
        dmxLogInput(dmxInput,
                    "Attaching device id %d: %s%s\n",
                    dmxLocal->pDevice->id,
                    dmxLocal->pDevice->name,
                    dmxLocal->isCore
                    ? " [core]"
                    : (dmxLocal->sendsCore ? " [sends core events]" : ""));
        EnableDevice(dmxLocal->pDevice, TRUE);
    }
    dmxInputLogDevices();
    return 0;
}

int
dmxInputAttachConsole(const char *name, int isCore, int *id)
{
    DMXInputInfo *dmxInput;
    int i;

    for (i = 0; i < dmxNumInputs; i++) {
        dmxInput = &dmxInputs[i];
        if (dmxInput->scrnIdx == -1
            && dmxInput->detached && !strcmp(dmxInput->name, name)) {
            /* Found match */
            dmxLogInput(dmxInput, "Reattaching detached console input\n");
            return dmxInputAttachOld(dmxInput, id);
        }
    }

    /* No match found */
    dmxInput = dmxConfigAddInput(xstrdup(name), isCore);
    dmxInput->freename = TRUE;
    dmxLogInput(dmxInput, "Attaching new console input\n");
    return dmxInputAttachNew(dmxInput, id);
}

int
dmxInputAttachBackend(int physicalScreen, int isCore, int *id)
{
    DMXInputInfo *dmxInput;
    DMXScreenInfo *dmxScreen;
    int i;

    if (physicalScreen < 0 || physicalScreen >= dmxNumScreens)
        return BadValue;
    for (i = 0; i < dmxNumInputs; i++) {
        dmxInput = &dmxInputs[i];
        if (dmxInput->scrnIdx != -1 && dmxInput->scrnIdx == physicalScreen) {
            /* Found match */
            if (!dmxInput->detached)
                return BadAccess;       /* Already attached */
            dmxScreen = &dmxScreens[physicalScreen];
            if (!dmxScreen->beDisplay)
                return BadAccess;       /* Screen detached */
            dmxLogInput(dmxInput, "Reattaching detached backend input\n");
            return dmxInputAttachOld(dmxInput, id);
        }
    }
    /* No match found */
    dmxScreen = &dmxScreens[physicalScreen];
    if (!dmxScreen->beDisplay)
        return BadAccess;       /* Screen detached */
    dmxInput = dmxConfigAddInput(dmxScreen->name, isCore);
    dmxLogInput(dmxInput, "Attaching new backend input\n");
    return dmxInputAttachNew(dmxInput, id);
}
