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

/** @file glamor_window.c
 *
 * Screen Change Window Attribute implementation.
 */

static void
glamor_fixup_window_pixmap(DrawablePtr pDrawable, PixmapPtr *ppPixmap)
{
    PixmapPtr pPixmap = *ppPixmap;
    glamor_pixmap_private *pixmap_priv;

    if (pPixmap->drawable.bitsPerPixel != pDrawable->bitsPerPixel) {
        pixmap_priv = glamor_get_pixmap_private(pPixmap);
        if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv)) {
            glamor_fallback("pixmap %p has no fbo\n", pPixmap);
            goto fail;
        }
        glamor_debug_output(GLAMOR_DEBUG_UNIMPL, "To be implemented.\n");
    }
    return;

 fail:
    GLAMOR_PANIC
        (" We can't fall back to fbFixupWindowPixmap, as the fb24_32ReformatTile"
         " is broken for glamor. \n");
}

Bool
glamor_change_window_attributes(WindowPtr pWin, unsigned long mask)
{
    if (mask & CWBackPixmap) {
        if (pWin->backgroundState == BackgroundPixmap)
            glamor_fixup_window_pixmap(&pWin->drawable,
                                       &pWin->background.pixmap);
    }

    if (mask & CWBorderPixmap) {
        if (pWin->borderIsPixel == FALSE)
            glamor_fixup_window_pixmap(&pWin->drawable, &pWin->border.pixmap);
    }
    return TRUE;
}

void
glamor_set_window_pixmap(WindowPtr win, PixmapPtr pPixmap)
{
    ScreenPtr screen = win->drawable.pScreen;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    PixmapPtr old = screen->GetWindowPixmap(win);

    if (pPixmap != old) {
        glamor_pixmap_private *pixmap_priv;
        PicturePtr pic = NULL;

        pixmap_priv = glamor_get_pixmap_private(old);
        if (GLAMOR_PIXMAP_PRIV_IS_PICTURE(pixmap_priv) &&
            pixmap_priv->base.picture->pDrawable == (DrawablePtr) win) {
            pic = pixmap_priv->base.picture;
            pixmap_priv->base.is_picture = 0;
            pixmap_priv->base.picture = NULL;
        }

        pixmap_priv = glamor_get_pixmap_private(pPixmap);
        if (pixmap_priv) {
            pixmap_priv->base.is_picture = ! !pic;
            pixmap_priv->base.picture = pic;
        }
    }

    screen->SetWindowPixmap = glamor_priv->saved_procs.set_window_pixmap;
    (screen->SetWindowPixmap) (win, pPixmap);
    glamor_priv->saved_procs.set_window_pixmap = screen->SetWindowPixmap;
    screen->SetWindowPixmap = glamor_set_window_pixmap;
}
