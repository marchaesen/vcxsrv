/*
 * Copyright 2002,2003 Red Hat Inc., Durham, North Carolina.
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
 * Interface to functions used by backend and console input devices.
 * \see dmxcommon.c \see dmxbackend.c \see dmxconsole.c */

#ifndef _DMXCOMMON_H_
#define _DMXCOMMON_H_

#define DMX_COMMON_OTHER                    \
    Display                 *display;       \
    Window                  window;         \
    DMXScreenInfo           *be;            \
    DMXLocalInputInfoPtr    dmxLocal;       \
    int                     initPointerX;   \
    int                     initPointerY;   \
    long                    eventMask;      \
    KeybdCtrl               kctrl;          \
    PtrCtrl                 mctrl;          \
    int                     kctrlset;       \
    int                     mctrlset;       \
    KeybdCtrl               savedKctrl;     \
    XModifierKeymap         *savedModMap;   \
    int                     stateSaved

#define DMX_COMMON_XKB                      \
    DMX_COMMON_OTHER;                       \
    XkbDescPtr              xkb;            \
    XkbIndicatorRec         savedIndicators

#define DMX_COMMON_PRIVATE                  \
    DMX_COMMON_XKB;                         \
    XDevice                 *xi

#define GETONLYPRIVFROMPRIVATE                                          \
    myPrivate            *priv     = private

#define GETPRIVFROMPRIVATE                                              \
    GETONLYPRIVFROMPRIVATE;                                             \
    DMXInputInfo         *dmxInput = &dmxInputs[priv->dmxLocal->inputIdx]

#define GETDMXLOCALFROMPDEVICE                                          \
    DevicePtr            pDev      = &pDevice->public;                  \
    DMXLocalInputInfoPtr dmxLocal  = pDev->devicePrivate

#define GETDMXINPUTFROMPRIV                                             \
    DMXInputInfo         *dmxInput = &dmxInputs[priv->dmxLocal->inputIdx]

#define GETDMXINPUTFROMPDEVICE                                          \
    GETDMXLOCALFROMPDEVICE;                                             \
    DMXInputInfo         *dmxInput = &dmxInputs[dmxLocal->inputIdx]

#define GETDMXLOCALFROMPDEV                                             \
    DMXLocalInputInfoPtr dmxLocal  = pDev->devicePrivate

#define GETDMXINPUTFROMPDEV                                             \
    GETDMXLOCALFROMPDEV;                                                \
    DMXInputInfo         *dmxInput = &dmxInputs[dmxLocal->inputIdx]

#define GETPRIVFROMPDEV                                                 \
    GETDMXLOCALFROMPDEV;                                                \
    myPrivate            *priv     = dmxLocal->private

#define DMX_KEYBOARD_EVENT_MASK                                         \
    (KeyPressMask | KeyReleaseMask | KeymapStateMask)

#define DMX_POINTER_EVENT_MASK                                          \
    (ButtonPressMask | ButtonReleaseMask | PointerMotionMask)

extern void dmxCommonKbdGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info);
extern void dmxCommonKbdGetMap(DevicePtr pDev,
                               KeySymsPtr pKeySyms, CARD8 *pModMap);
extern void dmxCommonKbdCtrl(DevicePtr pDev, KeybdCtrl * ctrl);
extern void dmxCommonKbdBell(DevicePtr pDev, int percent,
                             int volume, int pitch, int duration);
extern int dmxCommonKbdOn(DevicePtr pDev);
extern void dmxCommonKbdOff(DevicePtr pDev);
extern void dmxCommonMouGetMap(DevicePtr pDev,
                               unsigned char *map, int *nButtons);
extern void dmxCommonMouCtrl(DevicePtr pDev, PtrCtrl * ctrl);
extern int dmxCommonMouOn(DevicePtr pDev);
extern void dmxCommonMouOff(DevicePtr pDev);
extern int dmxFindPointerScreen(int x, int y);

extern int dmxCommonOthOn(DevicePtr pDev);
extern void dmxCommonOthOff(DevicePtr pDev);
extern void dmxCommonOthGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info);

                                /* helper functions */
extern pointer dmxCommonCopyPrivate(DeviceIntPtr pDevice);
extern void dmxCommonSaveState(pointer private);
extern void dmxCommonRestoreState(pointer private);
#endif
