/*
 * Copyright 2003 Red Hat Inc., Durham, North Carolina.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XKB.h>
#include <X11/extensions/XKBstr.h>
#include <sys/time.h>

int
main(int argc, char **argv)
{
    Display *display = NULL;
    int mask = 0;
    unsigned i;
    XKeyboardState ks;
    XKeyboardControl kc;
    XkbDescPtr xkb;
    int old[32];

    if (argc == 2 || argc == 3) {
        if (!(display = XOpenDisplay(argv[1]))) {
            printf("Cannot open display %s\n", argv[1]);
            return -1;
        }
        if (argc >= 3)
            mask = strtol(argv[2], NULL, 0);
    }
    else {
        printf("Usage: %s display [mask]\n", argv[0]);
        return -1;
    }

    if (!display && !(display = XOpenDisplay(NULL))) {
        printf("Cannot open default display\n");
        return -1;
    }

    if (!(xkb = XkbAllocKeyboard())) {
        printf("Cannot allocate\n");
        return -1;
    }
    if (XkbGetIndicatorMap(display, XkbAllIndicatorsMask, xkb)) {
        printf("Cannot Get Indicators\n");
        return -1;
    }
    if (XkbGetNames(display, XkbAllNamesMask, xkb)) {
        printf("Cannot Get Names\n");
        return -1;
    }
    for (i = 0; i < XkbNumIndicators; i++) {
        if (xkb->indicators->phys_indicators & (1 << i)) {
            printf("led %d = %d\n", i, xkb->indicators->maps[i].flags);
            old[i] = xkb->indicators->maps[i].flags;
            xkb->indicators->maps[i].flags = XkbIM_NoAutomatic;
        }
    }
    printf("XkbSetIndicatorMap = %d\n", XkbSetIndicatorMap(display, ~0, xkb));
    XkbFreeKeyboard(xkb, 0, True);

    if (!(xkb = XkbAllocKeyboard())) {
        printf("Cannot allocate\n");
        return -1;
    }
    if (XkbGetIndicatorMap(display, XkbAllIndicatorsMask, xkb)) {
        printf("Cannot Get Indicators\n");
        return -1;
    }
    for (i = 0; i < XkbNumIndicators; i++) {
        if (xkb->indicators->phys_indicators & (1 << i))
            printf("led %d = %d\n", i, xkb->indicators->maps[i].flags);
    }

    printf("XGetKeyboardControl = %d\n", XGetKeyboardControl(display, &ks));
    printf("old mask = 0x%08lx\n", ks.led_mask);
    for (i = 0; i < 5; i++) {
        kc.led = i + 1;
        kc.led_mode = (mask & (1 << i)) ? LedModeOn : LedModeOff;
        printf("XChangeKeyboardControl = %d\n",
               XChangeKeyboardControl(display, KBLed | KBLedMode, &kc));
    }
    printf("XGetKeyboardControl = %d\n", XGetKeyboardControl(display, &ks));
    printf("new mask = 0x%08lx\n", ks.led_mask);

    for (i = 0; i < XkbNumIndicators; i++)
        if (xkb->indicators->phys_indicators & (i << 1))
            xkb->indicators->maps[i].flags = old[i];
    printf("XkbSetIndicatorMap = %d\n", XkbSetIndicatorMap(display, ~0, xkb));

    XkbFreeKeyboard(xkb, 0, True);
    XCloseDisplay(display);
    return 0;
}
