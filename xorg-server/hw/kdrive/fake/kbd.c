/*
 * Copyright © 1999 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif
#include "fake.h"
#include <X11/keysym.h>

#define FAKE_WIDTH  2

static Status
FakeKeyboardInit(KdKeyboardInfo * ki)
{
    ki->minScanCode = 8;
    ki->maxScanCode = 255;
    return Success;
}

static Status
FakeKeyboardEnable(KdKeyboardInfo * ki)
{
    return Success;
}

static void
FakeKeyboardDisable(KdKeyboardInfo * ki)
{
    return;
}

static void
FakeKeyboardFini(KdKeyboardInfo * ki)
{
}

static void
FakeKeyboardLeds(KdKeyboardInfo * ki, int leds)
{
}

static void
FakeKeyboardBell(KdKeyboardInfo * ki, int volume, int frequency, int duration)
{
}

KdKeyboardDriver FakeKeyboardDriver = {
    "fake",
    FakeKeyboardInit,
    FakeKeyboardEnable,
    FakeKeyboardLeds,
    FakeKeyboardBell,
    FakeKeyboardDisable,
    FakeKeyboardFini,
    NULL,
};
