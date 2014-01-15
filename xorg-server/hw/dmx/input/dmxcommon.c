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
 *
 * This file implements common routines used by the backend and console
 * input devices.
 */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#define DMX_STATE_DEBUG 0

#include "dmxinputinit.h"
#include "dmxcommon.h"
#include "dmxconsole.h"
#include "dmxprop.h"
#include "dmxsync.h"
#include "dmxmap.h"

#include "inputstr.h"
#include "input.h"
#include <X11/keysym.h>
#include "mipointer.h"
#include "scrnintstr.h"

#include <unistd.h>             /* For usleep() */

#if DMX_STATE_DEBUG
#define DMXDBG0(f)               dmxLog(dmxDebug,f)
#else
#define DMXDBG0(f)
#endif

/** Each device has a private area that is visible only from inside the
 * driver code. */
typedef struct _myPrivate {
    DMX_COMMON_PRIVATE;
} myPrivate;

static void
dmxCommonKbdSetAR(Display * display, unsigned char *old, unsigned char *new)
{
    XKeyboardControl kc;
    XKeyboardState ks;
    unsigned long mask = KBKey | KBAutoRepeatMode;
    int i, j;
    int minKeycode, maxKeycode;

    if (!old) {
        XGetKeyboardControl(display, &ks);
        old = (unsigned char *) ks.auto_repeats;
    }

    XDisplayKeycodes(display, &minKeycode, &maxKeycode);
    for (i = 1; i < 32; i++) {
        if (!old || old[i] != new[i]) {
            for (j = 0; j < 8; j++) {
                if ((new[i] & (1 << j)) != (old[i] & (1 << j))) {
                    kc.key = i * 8 + j;
                    kc.auto_repeat_mode = ((new[i] & (1 << j))
                                           ? AutoRepeatModeOn
                                           : AutoRepeatModeOff);
                    if (kc.key >= minKeycode && kc.key <= maxKeycode)
                        XChangeKeyboardControl(display, mask, &kc);
                }
            }
        }
    }
}

static void
dmxCommonKbdSetLeds(Display * display, unsigned long new)
{
    int i;
    XKeyboardControl kc;

    for (i = 0; i < 32; i++) {
        kc.led = i + 1;
        kc.led_mode = (new & (1 << i)) ? LedModeOn : LedModeOff;
        XChangeKeyboardControl(display, KBLed | KBLedMode, &kc);
    }
}

static void
dmxCommonKbdSetCtrl(Display * display, KeybdCtrl * old, KeybdCtrl * new)
{
    XKeyboardControl kc;
    unsigned long mask = KBKeyClickPercent | KBAutoRepeatMode;

    if (!old || old->click != new->click || old->autoRepeat != new->autoRepeat) {

        kc.key_click_percent = new->click;
        kc.auto_repeat_mode = new->autoRepeat;

        XChangeKeyboardControl(display, mask, &kc);
    }

    dmxCommonKbdSetLeds(display, new->leds);
    dmxCommonKbdSetAR(display, old ? old->autoRepeats : NULL, new->autoRepeats);
}

static void
dmxCommonMouSetCtrl(Display * display, PtrCtrl * old, PtrCtrl * new)
{
    Bool do_accel, do_threshold;

    if (!old
        || old->num != new->num
        || old->den != new->den || old->threshold != new->threshold) {
        do_accel = (new->num > 0 && new->den > 0);
        do_threshold = (new->threshold > 0);
        if (do_accel || do_threshold) {
            XChangePointerControl(display, do_accel, do_threshold,
                                  new->num, new->den, new->threshold);
        }
    }
}

/** Update the keyboard control. */
void
dmxCommonKbdCtrl(DevicePtr pDev, KeybdCtrl * ctrl)
{
    GETPRIVFROMPDEV;

    if (!priv->stateSaved && priv->be)
        dmxCommonSaveState(priv);
    if (!priv->display || !priv->stateSaved)
        return;
    dmxCommonKbdSetCtrl(priv->display,
                        priv->kctrlset ? &priv->kctrl : NULL, ctrl);
    priv->kctrl = *ctrl;
    priv->kctrlset = 1;
}

/** Update the mouse control. */
void
dmxCommonMouCtrl(DevicePtr pDev, PtrCtrl * ctrl)
{
    GETPRIVFROMPDEV;

    /* Don't set the acceleration for the
     * console, because that should be
     * controlled by the X server that the
     * console is running on.  Otherwise,
     * the acceleration for the console
     * window would be unexpected for the
     * scale of the window. */
    if (priv->be) {
        dmxCommonMouSetCtrl(priv->display,
                            priv->mctrlset ? &priv->mctrl : NULL, ctrl);
        priv->mctrl = *ctrl;
        priv->mctrlset = 1;
    }
}

/** Sound they keyboard bell. */
void
dmxCommonKbdBell(DevicePtr pDev, int percent,
                 int volume, int pitch, int duration)
{
    GETPRIVFROMPDEV;
    XKeyboardControl kc;
    XKeyboardState ks;
    unsigned long mask = KBBellPercent | KBBellPitch | KBBellDuration;

    if (!priv->be)
        XGetKeyboardControl(priv->display, &ks);
    kc.bell_percent = volume;
    kc.bell_pitch = pitch;
    kc.bell_duration = duration;
    XChangeKeyboardControl(priv->display, mask, &kc);
    XBell(priv->display, percent);
    if (!priv->be) {
        kc.bell_percent = ks.bell_percent;
        kc.bell_pitch = ks.bell_pitch;
        kc.bell_duration = ks.bell_duration;
        XChangeKeyboardControl(priv->display, mask, &kc);
    }
}

/** Get the keyboard mapping. */
void
dmxCommonKbdGetMap(DevicePtr pDev, KeySymsPtr pKeySyms, CARD8 *pModMap)
{
    GETPRIVFROMPDEV;
    int min_keycode;
    int max_keycode;
    int map_width;
    KeySym *keyboard_mapping;
    XModifierKeymap *modifier_mapping;
    int i, j;

    /* Compute pKeySyms.  Cast
     * XGetKeyboardMapping because of
     * compiler warning on 64-bit machines.
     * We assume pointers to 32-bit and
     * 64-bit ints are the same. */
    XDisplayKeycodes(priv->display, &min_keycode, &max_keycode);
    keyboard_mapping = (KeySym *) XGetKeyboardMapping(priv->display,
                                                      min_keycode,
                                                      max_keycode
                                                      - min_keycode + 1,
                                                      &map_width);
    pKeySyms->minKeyCode = min_keycode;
    pKeySyms->maxKeyCode = max_keycode;
    pKeySyms->mapWidth = map_width;
    pKeySyms->map = keyboard_mapping;

    /* Compute pModMap  */
    modifier_mapping = XGetModifierMapping(priv->display);
    for (i = 0; i < MAP_LENGTH; i++)
        pModMap[i] = 0;
    for (j = 0; j < 8; j++) {
        int max_keypermod = modifier_mapping->max_keypermod;

        for (i = 0; i < max_keypermod; i++) {
            CARD8 keycode =
                modifier_mapping->modifiermap[j * max_keypermod + i];
            if (keycode)
                pModMap[keycode] |= 1 << j;
        }
    }
    XFreeModifiermap(modifier_mapping);
}

/** Fill in the XKEYBOARD parts of the \a info structure for the
 * specified \a pDev. */
void
dmxCommonKbdGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info)
{
    GETPRIVFROMPDEV;
    GETDMXINPUTFROMPRIV;
    char *pt;

    dmxCommonSaveState(priv);
    if (priv->xkb) {
#define NAME(x) \
 priv->xkb->names->x ? XGetAtomName(priv->display,priv->xkb->names->x) : NULL
        info->names.keycodes = NAME(keycodes);
        info->names.types = NAME(types);
        info->names.compat = NAME(compat);
        info->names.symbols = NAME(symbols);
        info->names.geometry = NAME(geometry);
        info->freenames = 1;
#undef NAME
        dmxLogInput(dmxInput,
                    "XKEYBOARD: keycodes = %s\n", info->names.keycodes);
        dmxLogInput(dmxInput,
                    "XKEYBOARD: symbols  = %s\n", info->names.symbols);
        dmxLogInput(dmxInput,
                    "XKEYBOARD: geometry = %s\n", info->names.geometry);
        if ((pt = strchr(info->names.keycodes, '+')))
            *pt = '\0';
    }
    dmxCommonRestoreState(priv);
}

/** Turn \a pDev on (i.e., take input from \a pDev). */
int
dmxCommonKbdOn(DevicePtr pDev)
{
    GETPRIVFROMPDEV;
    if (priv->be)
        dmxCommonSaveState(priv);
    priv->eventMask |= DMX_KEYBOARD_EVENT_MASK;
    XSelectInput(priv->display, priv->window, priv->eventMask);
    if (priv->be)
        XSetInputFocus(priv->display, priv->window, RevertToPointerRoot,
                       CurrentTime);
    return -1;
}

/** Turn \a pDev off. */
void
dmxCommonKbdOff(DevicePtr pDev)
{
    GETPRIVFROMPDEV;
    priv->eventMask &= ~DMX_KEYBOARD_EVENT_MASK;
    XSelectInput(priv->display, priv->window, priv->eventMask);
    dmxCommonRestoreState(priv);
}

/** Turn \a pDev on (i.e., take input from \a pDev). */
int
dmxCommonOthOn(DevicePtr pDev)
{
    GETPRIVFROMPDEV;
    GETDMXINPUTFROMPRIV;
    XEventClass event_list[DMX_MAX_XINPUT_EVENT_TYPES];
    int event_type[DMX_MAX_XINPUT_EVENT_TYPES];
    int count = 0;

#define ADD(type)                                                            \
    if (count < DMX_MAX_XINPUT_EVENT_TYPES) {                                \
        type(priv->xi, event_type[count], event_list[count]);                \
        if (event_type[count]) {                                             \
            dmxMapInsert(dmxLocal, event_type[count], XI_##type);            \
            ++count;                                                         \
        }                                                                    \
    } else {                                                                 \
        dmxLog(dmxWarning, "More than %d event types for %s\n",              \
               DMX_MAX_XINPUT_EVENT_TYPES, dmxInput->name);                  \
    }

    if (!(priv->xi = XOpenDevice(priv->display, dmxLocal->deviceId))) {
        dmxLog(dmxWarning, "Cannot open %s device (id=%d) on %s\n",
               dmxLocal->deviceName ? dmxLocal->deviceName : "(unknown)",
               (int) dmxLocal->deviceId, dmxInput->name);
        return -1;
    }
    ADD(DeviceKeyPress);
    ADD(DeviceKeyRelease);
    ADD(DeviceButtonPress);
    ADD(DeviceButtonRelease);
    ADD(DeviceMotionNotify);
    ADD(DeviceFocusIn);
    ADD(DeviceFocusOut);
    ADD(ProximityIn);
    ADD(ProximityOut);
    ADD(DeviceStateNotify);
    ADD(DeviceMappingNotify);
    ADD(ChangeDeviceNotify);
    XSelectExtensionEvent(priv->display, priv->window, event_list, count);

    return -1;
}

/** Turn \a pDev off. */
void
dmxCommonOthOff(DevicePtr pDev)
{
    GETPRIVFROMPDEV;

    if (priv->xi)
        XCloseDevice(priv->display, priv->xi);
    priv->xi = NULL;
}

/** Fill the \a info structure with information needed to initialize \a
 * pDev. */
void
dmxCommonOthGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info)
{
    GETPRIVFROMPDEV;
    GETDMXINPUTFROMPRIV;
    XExtensionVersion *ext;
    XDeviceInfo *devices;
    Display *display = priv->display;
    int num;
    int i, j, k;
    XextErrorHandler handler;

    if (!display && !(display = XOpenDisplay(dmxInput->name)))
        return;

    /* Print out information about the XInput Extension. */
    handler = XSetExtensionErrorHandler(dmxInputExtensionErrorHandler);
    ext = XGetExtensionVersion(display, INAME);
    XSetExtensionErrorHandler(handler);

    if (ext && ext != (XExtensionVersion *) NoSuchExtension) {
        XFree(ext);
        devices = XListInputDevices(display, &num);
        for (i = 0; i < num; i++) {
            if (devices[i].id == (XID) dmxLocal->deviceId) {
                XAnyClassPtr any;
                XKeyInfoPtr ki;
                XButtonInfoPtr bi;
                XValuatorInfoPtr vi;

                for (j = 0, any = devices[i].inputclassinfo;
                     j < devices[i].num_classes;
                     any = (XAnyClassPtr) ((char *) any + any->length), j++) {
                    switch (any->class) {
                    case KeyClass:
                        ki = (XKeyInfoPtr) any;
                        info->keyboard = 1;
                        info->keyClass = 1;
                        info->keySyms.minKeyCode = ki->min_keycode;
                        info->keySyms.maxKeyCode = ki->max_keycode;
                        info->kbdFeedbackClass = 1;
                        break;
                    case ButtonClass:
                        bi = (XButtonInfoPtr) any;
                        info->buttonClass = 1;
                        info->numButtons = bi->num_buttons;
                        info->ptrFeedbackClass = 1;
                        break;
                    case ValuatorClass:
                        /* This assume all axes are either
                         * Absolute or Relative. */
                        vi = (XValuatorInfoPtr) any;
                        info->valuatorClass = 1;
                        if (vi->mode == Absolute)
                            info->numAbsAxes = vi->num_axes;
                        else
                            info->numRelAxes = vi->num_axes;
                        for (k = 0; k < vi->num_axes; k++) {
                            info->res[k] = vi->axes[k].resolution;
                            info->minres[k] = vi->axes[k].resolution;
                            info->maxres[k] = vi->axes[k].resolution;
                            info->minval[k] = vi->axes[k].min_value;
                            info->maxval[k] = vi->axes[k].max_value;
                        }
                        break;
                    case FeedbackClass:
                        /* Only keyboard and pointer feedback
                         * are handled at this time. */
                        break;
                    case ProximityClass:
                        info->proximityClass = 1;
                        break;
                    case FocusClass:
                        info->focusClass = 1;
                        break;
                    case OtherClass:
                        break;
                    }
                }
            }
        }
        XFreeDeviceList(devices);
    }
    if (display != priv->display)
        XCloseDisplay(display);
}

/** Obtain the mouse button mapping. */
void
dmxCommonMouGetMap(DevicePtr pDev, unsigned char *map, int *nButtons)
{
    GETPRIVFROMPDEV;
    int i;

    *nButtons = XGetPointerMapping(priv->display, map, DMX_MAX_BUTTONS);
    for (i = 0; i <= *nButtons; i++)
        map[i] = i;
}

static void *
dmxCommonXSelect(DMXScreenInfo * dmxScreen, void *closure)
{
    myPrivate *priv = closure;

    XSelectInput(dmxScreen->beDisplay, dmxScreen->scrnWin, priv->eventMask);
    return NULL;
}

static void *
dmxCommonAddEnabledDevice(DMXScreenInfo * dmxScreen, void *closure)
{
    AddEnabledDevice(XConnectionNumber(dmxScreen->beDisplay));
    return NULL;
}

static void *
dmxCommonRemoveEnabledDevice(DMXScreenInfo * dmxScreen, void *closure)
{
    RemoveEnabledDevice(XConnectionNumber(dmxScreen->beDisplay));
    return NULL;
}

/** Turn \a pDev on (i.e., take input from \a pDev). */
int
dmxCommonMouOn(DevicePtr pDev)
{
    GETPRIVFROMPDEV;
    GETDMXINPUTFROMPRIV;

    priv->eventMask |= DMX_POINTER_EVENT_MASK;
    if (!priv->be) {
        XSelectInput(priv->display, priv->window, priv->eventMask);
        AddEnabledDevice(XConnectionNumber(priv->display));
    }
    else {
        dmxPropertyIterate(priv->be, dmxCommonXSelect, priv);
        dmxPropertyIterate(priv->be, dmxCommonAddEnabledDevice, dmxInput);
    }

    return -1;
}

/** Turn \a pDev off. */
void
dmxCommonMouOff(DevicePtr pDev)
{
    GETPRIVFROMPDEV;
    GETDMXINPUTFROMPRIV;

    priv->eventMask &= ~DMX_POINTER_EVENT_MASK;
    if (!priv->be) {
        RemoveEnabledDevice(XConnectionNumber(priv->display));
        XSelectInput(priv->display, priv->window, priv->eventMask);
    }
    else {
        dmxPropertyIterate(priv->be, dmxCommonRemoveEnabledDevice, dmxInput);
        dmxPropertyIterate(priv->be, dmxCommonXSelect, priv);
    }
}

/** Given the global coordinates \a x and \a y, determine the screen
 * with the lowest number on which those coordinates lie.  If they are
 * not on any screen, return -1.  The number returned is an index into
 * \a dmxScreenInfo and is between -1 and \a dmxNumScreens - 1,
 * inclusive. */
int
dmxFindPointerScreen(int x, int y)
{
    int i;

    for (i = 0; i < dmxNumScreens; i++) {
        ScreenPtr pScreen = screenInfo.screens[i];

        if (x >= pScreen->x && x < pScreen->x + pScreen->width &&
            y >= pScreen->y && y < pScreen->y + pScreen->height)
            return i;
    }
    return -1;
}

/** Returns a pointer to the private area for the device that comes just
 * prior to \a pDevice in the current \a dmxInput device list.  This is
 * used as the private area for the current device in some situations
 * (e.g., when a keyboard and mouse form a pair that should share the
 * same private area).  If the requested private area cannot be located,
 * then NULL is returned. */
void *
dmxCommonCopyPrivate(DeviceIntPtr pDevice)
{
    GETDMXLOCALFROMPDEVICE;
    DMXInputInfo *dmxInput = &dmxInputs[dmxLocal->inputIdx];
    int i;

    for (i = 0; i < dmxInput->numDevs; i++)
        if (dmxInput->devs[i] == dmxLocal && i)
            return dmxInput->devs[i - 1]->private;
    return NULL;
}

/** This routine saves and resets some important state for the backend
 * and console device drivers:
 * - the modifier map is saved and set to 0 (so DMX controls the LEDs)
 * - the key click, bell, led, and repeat masks are saved and set to the
 * values that DMX claims to be using
 *
 * This routine and #dmxCommonRestoreState are used when the pointer
 * enters and leaves the console window, or when the backend window is
 * active or not active (for a full-screen window, this only happens at
 * server startup and server shutdown).
 */
void
dmxCommonSaveState(void *private)
{
    GETPRIVFROMPRIVATE;
    XKeyboardState ks;
    unsigned long i;
    XModifierKeymap *modmap;

    if (dmxInput->console)
        priv = dmxInput->devs[0]->private;
    if (!priv->display || priv->stateSaved)
        return;
    DMXDBG0("dmxCommonSaveState\n");
    if (dmxUseXKB && (priv->xkb = XkbAllocKeyboard())) {
        if (XkbGetIndicatorMap(priv->display, XkbAllIndicatorsMask, priv->xkb)
            || XkbGetNames(priv->display, XkbAllNamesMask, priv->xkb)) {
            dmxLogInput(dmxInput, "Could not get XKB information\n");
            XkbFreeKeyboard(priv->xkb, 0, True);
            priv->xkb = NULL;
        }
        else {
            if (priv->xkb->indicators) {
                priv->savedIndicators = *priv->xkb->indicators;
                for (i = 0; i < XkbNumIndicators; i++)
                    if (priv->xkb->indicators->phys_indicators & (1 << i)) {
                        priv->xkb->indicators->maps[i].flags
                            = XkbIM_NoAutomatic;
                    }
                XkbSetIndicatorMap(priv->display, ~0, priv->xkb);
            }
        }
    }

    XGetKeyboardControl(priv->display, &ks);
    priv->savedKctrl.click = ks.key_click_percent;
    priv->savedKctrl.bell = ks.bell_percent;
    priv->savedKctrl.bell_pitch = ks.bell_pitch;
    priv->savedKctrl.bell_duration = ks.bell_duration;
    priv->savedKctrl.leds = ks.led_mask;
    priv->savedKctrl.autoRepeat = ks.global_auto_repeat;
    for (i = 0; i < 32; i++)
        priv->savedKctrl.autoRepeats[i] = ks.auto_repeats[i];

    dmxCommonKbdSetCtrl(priv->display, &priv->savedKctrl,
                        &priv->dmxLocal->kctrl);

    priv->savedModMap = XGetModifierMapping(priv->display);

    modmap = XNewModifiermap(0);
    XSetModifierMapping(priv->display, modmap);
    if (dmxInput->scrnIdx != -1)
        dmxSync(&dmxScreens[dmxInput->scrnIdx], TRUE);
    XFreeModifiermap(modmap);

    priv->stateSaved = 1;
}

/** This routine restores all the information saved by #dmxCommonSaveState. */
void
dmxCommonRestoreState(void *private)
{
    GETPRIVFROMPRIVATE;
    int retcode = -1;
    CARD32 start;

    if (dmxInput->console)
        priv = dmxInput->devs[0]->private;
    if (!priv->stateSaved)
        return;
    priv->stateSaved = 0;

    DMXDBG0("dmxCommonRestoreState\n");
    if (priv->xkb) {
        *priv->xkb->indicators = priv->savedIndicators;
        XkbSetIndicatorMap(priv->display, ~0, priv->xkb);
        XkbFreeKeyboard(priv->xkb, 0, True);
        priv->xkb = 0;
    }

    for (start = GetTimeInMillis(); GetTimeInMillis() - start < 5000;) {
        CARD32 tmp;

        retcode = XSetModifierMapping(priv->display, priv->savedModMap);
        if (retcode == MappingSuccess)
            break;
        if (retcode == MappingBusy)
            dmxLogInput(dmxInput, "Keyboard busy, waiting\n");
        else
            dmxLogInput(dmxInput, "Keyboard error, waiting\n");

        /* Don't generate X11 protocol for a bit */
        for (tmp = GetTimeInMillis(); GetTimeInMillis() - tmp < 250;) {
            usleep(250);        /* This ends up sleeping only until
                                 * the next key press generates an
                                 * interruption.  We make the delay
                                 * relatively short in case the user
                                 * pressed they keys quickly. */
        }

    }
    if (retcode != MappingSuccess)
        dmxLog(dmxWarning, "Unable to restore keyboard modifier state!\n");

    XFreeModifiermap(priv->savedModMap);
    priv->savedModMap = NULL;

    dmxCommonKbdSetCtrl(priv->display, NULL, &priv->savedKctrl);
    priv->kctrlset = 0;         /* Invalidate copy */
}
