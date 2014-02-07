/*
 * Copyright © 2008 Intel Corporation
 * Copyright © 1998 Keith Packard
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

#include "glamor_priv.h"

/** @file glamor_copywindow.c
 *
 * Screen CopyWindow implementation.
 */

void
glamor_copy_window(WindowPtr win, DDXPointRec old_origin, RegionPtr src_region)
{
    RegionRec dst_region;
    int dx, dy;
    PixmapPtr pixmap = win->drawable.pScreen->GetWindowPixmap(win);

    dx = old_origin.x - win->drawable.x;
    dy = old_origin.y - win->drawable.y;
    REGION_TRANSLATE(win->drawable.pScreen, src_region, -dx, -dy);

    REGION_INIT(win->drawable.pScreen, &dst_region, NullBox, 0);

    REGION_INTERSECT(win->drawable.pScreen, &dst_region,
                     &win->borderClip, src_region);
#ifdef COMPOSITE
    if (pixmap->screen_x || pixmap->screen_y)
        REGION_TRANSLATE(win->drawable.pScreen, &dst_region,
                         -pixmap->screen_x, -pixmap->screen_y);
#endif

    miCopyRegion(&pixmap->drawable, &pixmap->drawable,
                 NULL, &dst_region, dx, dy, glamor_copy_n_to_n, 0, NULL);

    REGION_UNINIT(win->drawable.pScreen, &dst_region);
}
