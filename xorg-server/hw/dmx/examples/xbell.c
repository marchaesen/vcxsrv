/*
 * Copyright 2002 Red Hat Inc., Durham, North Carolina.
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
#include <X11/Xlib.h>

static void
pkc(XKeyboardControl * kc, unsigned long vm)
{
    if (vm & KBKeyClickPercent)
        printf("   key_click_percent  = %d\n", kc->key_click_percent);
    if (vm & KBBellPercent)
        printf("   bell_percent       = %d\n", kc->bell_percent);
    if (vm & KBBellPitch)
        printf("   bell_pitch         = %d\n", kc->bell_pitch);
    if (vm & KBBellDuration)
        printf("   bell_duration      = %d\n", kc->bell_duration);
    if (vm & KBLed)
        printf("   led                = 0x%x\n", kc->led);
    if (vm & KBLedMode)
        printf("   led_mode           = %d\n", kc->led_mode);
    if (vm & KBKey)
        printf("   key                = %d\n", kc->key);
    if (vm & KBAutoRepeatMode)
        printf("   auto_repeat_mode   = %d\n", kc->auto_repeat_mode);
}

static void
pks(XKeyboardState * ks)
{
    printf("   key_click_percent  = %d\n", ks->key_click_percent);
    printf("   bell_percent       = %d\n", ks->bell_percent);
    printf("   bell_pitch         = %u\n", ks->bell_pitch);
    printf("   bell_duration      = %u\n", ks->bell_duration);
    printf("   led_mask           = 0x%lx\n", ks->led_mask);
    printf("   global_auto_repeat = %d\n", ks->global_auto_repeat);
}

int
main(int argc, char **argv)
{
    Display *display = XOpenDisplay(NULL);
    XKeyboardControl kc;
    XKeyboardState ks;
    unsigned long vm;

    if (argc != 5) {
        printf("Usage: xbell percent baseVolume pitch duration\n");
        return 1;
    }

    vm = (KBBellPercent | KBBellPitch | KBBellDuration);
    kc.key_click_percent = atoi(argv[1]);
    kc.bell_percent = atoi(argv[2]);
    kc.bell_pitch = atoi(argv[3]);
    kc.bell_duration = atoi(argv[4]);

    printf("Setting:\n");
    pkc(&kc, vm);
    XChangeKeyboardControl(display, vm, &kc);

    printf("Have:\n");
    XGetKeyboardControl(display, &ks);
    pks(&ks);

    XBell(display, 100);

    XCloseDisplay(display);
    return 0;
}
