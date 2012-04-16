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
#include <errno.h>
#include <termios.h>
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/Xpoll.h>
#include "inputstr.h"
#include "scrnintstr.h"
#include "kdrive.h"

static Status
MouseInit(KdPointerInfo * pi)
{
    return Success;
}

static Status
MouseEnable(KdPointerInfo * pi)
{
    return Success;
}

static void
MouseDisable(KdPointerInfo * pi)
{
    return;
}

static void
MouseFini(KdPointerInfo * pi)
{
    return;
}

KdPointerDriver FakePointerDriver = {
    "fake",
    MouseInit,
    MouseEnable,
    MouseDisable,
    MouseFini,
};
