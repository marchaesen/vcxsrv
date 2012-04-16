/*
 * Copyright © 2004 Keith Packard
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

void
InitCard(char *name)
{
    KdCardInfoAdd(&fakeFuncs, 0);
}

void
InitOutput(ScreenInfo * pScreenInfo, int argc, char **argv)
{
    KdInitOutput(pScreenInfo, argc, argv);
}

void
InitInput(int argc, char **argv)
{
    KdPointerInfo *pi;
    KdKeyboardInfo *ki;

    pi = KdNewPointer();
    if (!pi)
        return;
    pi->driver = &FakePointerDriver;
    KdAddPointer(pi);

    ki = KdNewKeyboard();
    if (!ki)
        return;
    ki->driver = &FakeKeyboardDriver;
    KdAddKeyboard(ki);

    KdInitInput();
}

void
CloseInput(void)
{
    KdCloseInput();
}

#ifdef DDXBEFORERESET
void
ddxBeforeReset(void)
{
}
#endif

void
ddxUseMsg(void)
{
    KdUseMsg();
}

int
ddxProcessArgument(int argc, char **argv, int i)
{
    return KdProcessArgument(argc, argv, i);
}

void
OsVendorInit(void)
{
    KdOsInit(&FakeOsFuncs);
}

KdCardFuncs fakeFuncs = {
    fakeCardInit,               /* cardinit */
    fakeScreenInit,             /* scrinit */
    fakeInitScreen,             /* initScreen */
    fakeFinishInitScreen,       /* finishInitScreen */
    fakeCreateResources,        /* createRes */
    fakePreserve,               /* preserve */
    fakeEnable,                 /* enable */
    fakeDPMS,                   /* dpms */
    fakeDisable,                /* disable */
    fakeRestore,                /* restore */
    fakeScreenFini,             /* scrfini */
    fakeCardFini,               /* cardfini */

    0,                          /* initCursor */
    0,                          /* enableCursor */
    0,                          /* disableCursor */
    0,                          /* finiCursor */
    0,                          /* recolorCursor */

    0,                          /* initAccel */
    0,                          /* enableAccel */
    0,                          /* disableAccel */
    0,                          /* finiAccel */

    fakeGetColors,              /* getColors */
    fakePutColors,              /* putColors */
};
