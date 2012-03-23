/* Portions of this file were derived from the following files:
 *
 **********************************************************************
 *
 * xfree86/common/xf86KbdLnx.c
 *
 * Linux version of keymapping setup. The kernel (since 0.99.14) has support
 * for fully remapping the keyboard, but there are some differences between
 * the Linux map and the SVR4 map (esp. in the extended keycodes). We also
 * remove the restriction on what keycodes can be remapped.
 * Orest Zborowski.
 *
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Thomas Roell not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Thomas Roell makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * THOMAS ROELL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THOMAS ROELL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */

/*
 * Copyright 2001,2002 Red Hat Inc., Durham, North Carolina.
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
 *
 * This code implements a low-level device driver for a USB keyboard
 * under Linux.  The keymap description is derived from code by Thomas
 * Roell, Orest Zborowski. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "atKeynames.h"
#include "usb-private.h"

#define USB_KEYBOARD_DEBUG 0

/*****************************************************************************/
/* Define some macros to make it easier to move this file to another
 * part of the Xserver tree.  All calls to the dmx* layer are #defined
 * here for the .c file.  The .h file will also have to be edited. */
#include "usb-keyboard.h"
#include <xkbsrv.h>

#define GETPRIV       myPrivate *priv                            \
                      = ((DMXLocalInputInfoPtr)(pDev->devicePrivate))->private

#define LOG0(f)       dmxLog(dmxDebug,f)
#define LOG1(f,a)     dmxLog(dmxDebug,f,a)
#define LOG2(f,a,b)   dmxLog(dmxDebug,f,a,b)
#define LOG3(f,a,b,c) dmxLog(dmxDebug,f,a,b,c)
#define FATAL0(f)     dmxLog(dmxFatal,f)
#define FATAL1(f,a)   dmxLog(dmxFatal,f,a)
#define FATAL2(f,a,b) dmxLog(dmxFatal,f,a,b)
#define MOTIONPROC    dmxMotionProcPtr
#define ENQUEUEPROC   dmxEnqueueProcPtr
#define CHECKPROC     dmxCheckSpecialProcPtr
#define BLOCK         DMXBlockType

/* End of interface definitions. */
/*****************************************************************************/

#define GLYPHS_PER_KEY	4
#define NUM_KEYCODES	248
#define MIN_KEYCODE     8
#define MAX_KEYCODE     (NUM_KEYCODES + MIN_KEYCODE - 1)

static KeySym map[NUM_KEYCODES * GLYPHS_PER_KEY] = {
/* Table modified from xc/programs/Xserver/hw/xfree86/common/xf86Keymap.h */
    /* 0x00 */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x01 */ XK_Escape, NoSymbol, NoSymbol, NoSymbol,
    /* 0x02 */ XK_1, XK_exclam, NoSymbol, NoSymbol,
    /* 0x03 */ XK_2, XK_at, NoSymbol, NoSymbol,
    /* 0x04 */ XK_3, XK_numbersign, NoSymbol, NoSymbol,
    /* 0x05 */ XK_4, XK_dollar, NoSymbol, NoSymbol,
    /* 0x06 */ XK_5, XK_percent, NoSymbol, NoSymbol,
    /* 0x07 */ XK_6, XK_asciicircum, NoSymbol, NoSymbol,
    /* 0x08 */ XK_7, XK_ampersand, NoSymbol, NoSymbol,
    /* 0x09 */ XK_8, XK_asterisk, NoSymbol, NoSymbol,
    /* 0x0a */ XK_9, XK_parenleft, NoSymbol, NoSymbol,
    /* 0x0b */ XK_0, XK_parenright, NoSymbol, NoSymbol,
    /* 0x0c */ XK_minus, XK_underscore, NoSymbol, NoSymbol,
    /* 0x0d */ XK_equal, XK_plus, NoSymbol, NoSymbol,
    /* 0x0e */ XK_BackSpace, NoSymbol, NoSymbol, NoSymbol,
    /* 0x0f */ XK_Tab, XK_ISO_Left_Tab, NoSymbol, NoSymbol,
    /* 0x10 */ XK_Q, NoSymbol, NoSymbol, NoSymbol,
    /* 0x11 */ XK_W, NoSymbol, NoSymbol, NoSymbol,
    /* 0x12 */ XK_E, NoSymbol, NoSymbol, NoSymbol,
    /* 0x13 */ XK_R, NoSymbol, NoSymbol, NoSymbol,
    /* 0x14 */ XK_T, NoSymbol, NoSymbol, NoSymbol,
    /* 0x15 */ XK_Y, NoSymbol, NoSymbol, NoSymbol,
    /* 0x16 */ XK_U, NoSymbol, NoSymbol, NoSymbol,
    /* 0x17 */ XK_I, NoSymbol, NoSymbol, NoSymbol,
    /* 0x18 */ XK_O, NoSymbol, NoSymbol, NoSymbol,
    /* 0x19 */ XK_P, NoSymbol, NoSymbol, NoSymbol,
    /* 0x1a */ XK_bracketleft, XK_braceleft, NoSymbol, NoSymbol,
    /* 0x1b */ XK_bracketright, XK_braceright, NoSymbol, NoSymbol,
    /* 0x1c */ XK_Return, NoSymbol, NoSymbol, NoSymbol,
    /* 0x1d */ XK_Control_L, NoSymbol, NoSymbol, NoSymbol,
    /* 0x1e */ XK_A, NoSymbol, NoSymbol, NoSymbol,
    /* 0x1f */ XK_S, NoSymbol, NoSymbol, NoSymbol,
    /* 0x20 */ XK_D, NoSymbol, NoSymbol, NoSymbol,
    /* 0x21 */ XK_F, NoSymbol, NoSymbol, NoSymbol,
    /* 0x22 */ XK_G, NoSymbol, NoSymbol, NoSymbol,
    /* 0x23 */ XK_H, NoSymbol, NoSymbol, NoSymbol,
    /* 0x24 */ XK_J, NoSymbol, NoSymbol, NoSymbol,
    /* 0x25 */ XK_K, NoSymbol, NoSymbol, NoSymbol,
    /* 0x26 */ XK_L, NoSymbol, NoSymbol, NoSymbol,
    /* 0x27 */ XK_semicolon, XK_colon, NoSymbol, NoSymbol,
    /* 0x28 */ XK_quoteright, XK_quotedbl, NoSymbol, NoSymbol,
    /* 0x29 */ XK_quoteleft, XK_asciitilde, NoSymbol, NoSymbol,
    /* 0x2a */ XK_Shift_L, NoSymbol, NoSymbol, NoSymbol,
    /* 0x2b */ XK_backslash, XK_bar, NoSymbol, NoSymbol,
    /* 0x2c */ XK_Z, NoSymbol, NoSymbol, NoSymbol,
    /* 0x2d */ XK_X, NoSymbol, NoSymbol, NoSymbol,
    /* 0x2e */ XK_C, NoSymbol, NoSymbol, NoSymbol,
    /* 0x2f */ XK_V, NoSymbol, NoSymbol, NoSymbol,
    /* 0x30 */ XK_B, NoSymbol, NoSymbol, NoSymbol,
    /* 0x31 */ XK_N, NoSymbol, NoSymbol, NoSymbol,
    /* 0x32 */ XK_M, NoSymbol, NoSymbol, NoSymbol,
    /* 0x33 */ XK_comma, XK_less, NoSymbol, NoSymbol,
    /* 0x34 */ XK_period, XK_greater, NoSymbol, NoSymbol,
    /* 0x35 */ XK_slash, XK_question, NoSymbol, NoSymbol,
    /* 0x36 */ XK_Shift_R, NoSymbol, NoSymbol, NoSymbol,
    /* 0x37 */ XK_KP_Multiply, NoSymbol, NoSymbol, NoSymbol,
    /* 0x38 */ XK_Alt_L, XK_Meta_L, NoSymbol, NoSymbol,
    /* 0x39 */ XK_space, NoSymbol, NoSymbol, NoSymbol,
    /* 0x3a */ XK_Caps_Lock, NoSymbol, NoSymbol, NoSymbol,
    /* 0x3b */ XK_F1, NoSymbol, NoSymbol, NoSymbol,
    /* 0x3c */ XK_F2, NoSymbol, NoSymbol, NoSymbol,
    /* 0x3d */ XK_F3, NoSymbol, NoSymbol, NoSymbol,
    /* 0x3e */ XK_F4, NoSymbol, NoSymbol, NoSymbol,
    /* 0x3f */ XK_F5, NoSymbol, NoSymbol, NoSymbol,
    /* 0x40 */ XK_F6, NoSymbol, NoSymbol, NoSymbol,
    /* 0x41 */ XK_F7, NoSymbol, NoSymbol, NoSymbol,
    /* 0x42 */ XK_F8, NoSymbol, NoSymbol, NoSymbol,
    /* 0x43 */ XK_F9, NoSymbol, NoSymbol, NoSymbol,
    /* 0x44 */ XK_F10, NoSymbol, NoSymbol, NoSymbol,
    /* 0x45 */ XK_Num_Lock, NoSymbol, NoSymbol, NoSymbol,
    /* 0x46 */ XK_Scroll_Lock, NoSymbol, NoSymbol, NoSymbol,
    /* 0x47 */ XK_KP_Home, XK_KP_7, NoSymbol, NoSymbol,
    /* 0x48 */ XK_KP_Up, XK_KP_8, NoSymbol, NoSymbol,
    /* 0x49 */ XK_KP_Prior, XK_KP_9, NoSymbol, NoSymbol,
    /* 0x4a */ XK_KP_Subtract, NoSymbol, NoSymbol, NoSymbol,
    /* 0x4b */ XK_KP_Left, XK_KP_4, NoSymbol, NoSymbol,
    /* 0x4c */ XK_KP_Begin, XK_KP_5, NoSymbol, NoSymbol,
    /* 0x4d */ XK_KP_Right, XK_KP_6, NoSymbol, NoSymbol,
    /* 0x4e */ XK_KP_Add, NoSymbol, NoSymbol, NoSymbol,
    /* 0x4f */ XK_KP_End, XK_KP_1, NoSymbol, NoSymbol,
    /* 0x50 */ XK_KP_Down, XK_KP_2, NoSymbol, NoSymbol,
    /* 0x51 */ XK_KP_Next, XK_KP_3, NoSymbol, NoSymbol,
    /* 0x52 */ XK_KP_Insert, XK_KP_0, NoSymbol, NoSymbol,
    /* 0x53 */ XK_KP_Delete, XK_KP_Decimal, NoSymbol, NoSymbol,
    /* 0x54 */ XK_Sys_Req, NoSymbol, NoSymbol, NoSymbol,
    /* 0x55 */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x56 */ XK_less, XK_greater, NoSymbol, NoSymbol,
    /* 0x57 */ XK_F11, NoSymbol, NoSymbol, NoSymbol,
    /* 0x58 */ XK_F12, NoSymbol, NoSymbol, NoSymbol,
    /* 0x59 */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x5a */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x5b */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x5c */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x5d */ XK_Begin, NoSymbol, NoSymbol, NoSymbol,
    /* 0x5e */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x5f */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x60 */ XK_KP_Enter, NoSymbol, NoSymbol, NoSymbol,
    /* 0x61 */ XK_Control_R, NoSymbol, NoSymbol, NoSymbol,
    /* 0x62 */ XK_KP_Divide, NoSymbol, NoSymbol, NoSymbol,
    /* 0x63 */ XK_Print, NoSymbol, NoSymbol, NoSymbol,
    /* 0x64 */ XK_Alt_R, XK_Meta_R, NoSymbol, NoSymbol,
    /* 0x65 */ XK_Break, NoSymbol, NoSymbol, NoSymbol,
    /* 0x66 */ XK_Home, NoSymbol, NoSymbol, NoSymbol,
    /* 0x67 */ XK_Up, NoSymbol, NoSymbol, NoSymbol,
    /* 0x68 */ XK_Prior, NoSymbol, NoSymbol, NoSymbol,
    /* 0x69 */ XK_Left, NoSymbol, NoSymbol, NoSymbol,
    /* 0x6a */ XK_Right, NoSymbol, NoSymbol, NoSymbol,
    /* 0x6b */ XK_End, NoSymbol, NoSymbol, NoSymbol,
    /* 0x6c */ XK_Down, NoSymbol, NoSymbol, NoSymbol,
    /* 0x6d */ XK_Next, NoSymbol, NoSymbol, NoSymbol,
    /* 0x6e */ XK_Insert, NoSymbol, NoSymbol, NoSymbol,
    /* 0x6f */ XK_Delete, NoSymbol, NoSymbol, NoSymbol,
    /* 0x70 */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x71 */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x72 */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x73 */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x74 */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x75 */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x76 */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x77 */ XK_Pause, NoSymbol, NoSymbol, NoSymbol,
    /* 0x78 */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x79 */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x7a */ XK_Menu, NoSymbol, NoSymbol, NoSymbol,
    /* 0x7b */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x7c */ NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    /* 0x7d */ XK_Super_L, NoSymbol, NoSymbol, NoSymbol,
    /* 0x7e */ XK_Super_R, NoSymbol, NoSymbol, NoSymbol,
    /* 0x7f */ XK_Menu, NoSymbol, NoSymbol, NoSymbol,
};

static int
kbdUSBKeyDown(myPrivate * priv, int keyCode)
{
    CARD8 byte = keyCode >> 5;
    CARD32 bit = 1 << (keyCode & 0x1f);

    if (byte > NUM_STATE_ENTRIES)
        return 0;
    return priv->kbdState[byte] & bit;
}

static void
kbdUSBKeyState(myPrivate * priv, int type, int keyCode)
{
    CARD8 byte = keyCode >> 5;
    CARD32 bit = 1 << (keyCode & 0x1f);

    if (byte > NUM_STATE_ENTRIES)
        return;
    if (type == KeyPress)
        priv->kbdState[byte] |= bit;
    else
        priv->kbdState[byte] &= ~bit;
}

/** Set the LEDs. */
void
kbdUSBCtrl(DevicePtr pDev, KeybdCtrl * ctrl)
{
    GETPRIV;
    struct timeval tv;
    struct input_event event;
    int i, led;

    gettimeofday(&tv, NULL);
    for (i = 0; i < 5; i++) {
        event.time.tv_sec = tv.tv_sec;
        event.time.tv_usec = tv.tv_usec;
        event.type = EV_LED;
        if (i == 0)
            led = 1;            /* LED_CAPSL == 0x01 */
        else if (i == 1)
            led = 0;            /* LED_NUML  == 0x00 */
        else
            led = i;
        event.code = led;
        event.value = ! !(ctrl->leds & (1 << led));
        write(priv->fd, &event, sizeof(event));
    }
}

/** Initialize \a pDev using #usbInit. */
void
kbdUSBInit(DevicePtr pDev)
{
    usbInit(pDev, usbKeyboard);
}

static void
kbdUSBConvert(DevicePtr pDev,
              unsigned int scanCode,
              int value,
              ENQUEUEPROC enqueue, CHECKPROC checkspecial, BLOCK block)
{
    GETPRIV;
    XkbSrvInfoPtr xkbi = priv->pKeyboard->key->xkbInfo;
    int type;
    int keyCode;
    KeySym keySym = NoSymbol;
    int switching;

    /* Set up xEvent information */
    type = value ? KeyPress : KeyRelease;
    keyCode = (scanCode & 0xff) + MIN_KEYCODE;

    /* Handle repeats */

    if (keyCode >= xkbi->desc->min_key_code &&
        keyCode <= xkbi->desc->max_key_code) {

        int effectiveGroup = XkbGetEffectiveGroup(xkbi,
                                                  &xkbi->state,
                                                  scanCode);

        keySym = XkbKeySym(xkbi->desc, scanCode, effectiveGroup);
#if 0
        switch (keySym) {
        case XK_Num_Lock:
        case XK_Scroll_Lock:
        case XK_Shift_Lock:
        case XK_Caps_Lock:
            /* Ignore releases and all but first press */
            if (kbdLinuxModIgnore(priv, &xE, keySym))
                return;
            if (kbdLinuxKeyDown(priv, &xE))
                xE.u.u.type = KeyRelease;
            else
                xE.u.u.type = KeyPress;
            break;
        }
#endif

        /* If key is already down, ignore or autorepeat */
        if (type == KeyPress && kbdUSBKeyDown(priv, keyCode)) {
            KbdFeedbackClassRec *feed = priv->pDevice->kbdfeed;

            /* No auto-repeat? */
            if ((feed && !feed->ctrl.autoRepeat)
                || priv->pDevice->key->xkbInfo->desc->map->modmap[keyCode]
                || (feed && !(feed->ctrl.autoRepeats[keyCode >> 3]
                              & (1 << (keyCode & 7)))))
                return;         /* Ignore */

            /* Do auto-repeat */
            enqueue(pDev, KeyRelease, keyCode, keySym, NULL, block);
            type = KeyPress;
        }

        /* If key is already up, ignore */
        if (type == KeyRelease && !kbdUSBKeyDown(priv, keyCode))
            return;
    }

    switching = 0;
    if (checkspecial && type == KeyPress)
        switching = checkspecial(pDev, keySym);
    if (!switching) {
        if (enqueue)
            enqueue(pDev, type, keyCode, keySym, NULL, block);
        kbdUSBKeyState(priv, type, keyCode);    /* Update our state bitmap */
    }
}

/** Read an event from the \a pDev device.  If the event is a motion
 * event, enqueue it with the \a motion function.  Otherwise, check for
 * special keys with the \a checkspecial function and enqueue the event
 * with the \a enqueue function.  The \a block type is passed to the
 * functions so that they may block SIGIO handling as appropriate to the
 * caller of this function. */
void
kbdUSBRead(DevicePtr pDev,
           MOTIONPROC motion,
           ENQUEUEPROC enqueue, CHECKPROC checkspecial, BLOCK block)
{
    GETPRIV;
    struct input_event raw;

    while (read(priv->fd, &raw, sizeof(raw)) > 0) {
#if USB_KEYBOARD_DEBUG
        LOG3("KBD: type = %d, code = 0x%02x, value = %d\n",
             raw.type, raw.code, raw.value);
#endif
        kbdUSBConvert(pDev, raw.code, raw.value, enqueue, checkspecial, block);
    }
}

/** Turn \a pDev on (i.e., take input from \a pDev). */
int
kbdUSBOn(DevicePtr pDev)
{
    GETPRIV;

    if (priv->fd < 0)
        kbdUSBInit(pDev);
    return priv->fd;
}

static void
kbdUSBGetMap(DevicePtr pDev, KeySymsPtr pKeySyms, CARD8 *pModMap)
{
    KeySym *k, *mapCopy;
    int i;

    mapCopy = malloc(sizeof(map));
    memcpy(mapCopy, map, sizeof(map));

    /* compute the modifier map */
    for (i = 0; i < MAP_LENGTH; i++)
        pModMap[i] = NoSymbol;  /* make sure it is restored */

    for (k = mapCopy, i = MIN_KEYCODE;
         i < NUM_KEYCODES + MIN_KEYCODE; i++, k += 4) {
        switch (*k) {
        case XK_Shift_L:
        case XK_Shift_R:
            pModMap[i] = ShiftMask;
            break;
        case XK_Control_L:
        case XK_Control_R:
            pModMap[i] = ControlMask;
            break;
        case XK_Caps_Lock:
            pModMap[i] = LockMask;
            break;
        case XK_Alt_L:
        case XK_Alt_R:
            pModMap[i] = AltMask;
            break;
        case XK_Num_Lock:
            pModMap[i] = NumLockMask;
            break;
        case XK_Scroll_Lock:
            pModMap[i] = ScrollLockMask;
            break;
        case XK_Kana_Lock:
        case XK_Kana_Shift:
            pModMap[i] = KanaMask;
            break;
        case XK_Mode_switch:
            pModMap[i] = AltLangMask;
            break;
        }
    }

    pKeySyms->map = mapCopy;    /* Must be XFree'able */
    pKeySyms->mapWidth = GLYPHS_PER_KEY;
    pKeySyms->minKeyCode = MIN_KEYCODE;
    pKeySyms->maxKeyCode = MAX_KEYCODE;
}

/** Fill the \a info structure with information needed to initialize \a
 * pDev. */
void
kbdUSBGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info)
{
    info->keyboard = 1;
    info->keyClass = 1;
    kbdUSBGetMap(pDev, &info->keySyms, info->modMap);
    info->focusClass = 1;
    info->kbdFeedbackClass = 1;
    info->names.keycodes = strdup("powerpcps2");
    info->force = 1;
}
