/*
 * Copyright © 2013 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "dri3_priv.h"
#include <syncsdk.h>
#include <misync.h>
#include <misyncshm.h>
#include <randrstr.h>

static inline Bool has_open(dri3_screen_info_ptr info) {
    if (info == NULL)
        return FALSE;

    return info->open != NULL ||
        (info->version >= 1 && info->open_client != NULL);
}

int
dri3_open(ClientPtr client, ScreenPtr screen, RRProviderPtr provider, int *fd)
{
    dri3_screen_priv_ptr        ds = dri3_screen_priv(screen);
    dri3_screen_info_ptr        info = ds->info;
    int                         rc;

    if (!has_open(info))
        return BadMatch;

    if (info->version >= 1 && info->open_client != NULL)
        rc = (*info->open_client) (client, screen, provider, fd);
    else
        rc = (*info->open) (screen, provider, fd);

    if (rc != Success)
        return rc;

    return Success;
}

int
dri3_pixmap_from_fd(PixmapPtr *ppixmap, ScreenPtr screen, int fd,
                    CARD16 width, CARD16 height, CARD16 stride, CARD8 depth, CARD8 bpp)
{
    dri3_screen_priv_ptr        ds = dri3_screen_priv(screen);
    dri3_screen_info_ptr        info = ds->info;
    PixmapPtr                   pixmap;

    if (!info || !info->pixmap_from_fd)
        return BadImplementation;

    pixmap = (*info->pixmap_from_fd) (screen, fd, width, height, stride, depth, bpp);
    if (!pixmap)
        return BadAlloc;

    *ppixmap = pixmap;
    return Success;
}

int
dri3_fd_from_pixmap(int *pfd, PixmapPtr pixmap, CARD16 *stride, CARD32 *size)
{
    ScreenPtr                   screen = pixmap->drawable.pScreen;
    dri3_screen_priv_ptr        ds = dri3_screen_priv(screen);
    dri3_screen_info_ptr        info = ds->info;
    int                         fd;

    if (!info || !info->fd_from_pixmap)
        return BadImplementation;

    fd = (*info->fd_from_pixmap)(screen, pixmap, stride, size);
    if (fd < 0)
        return BadAlloc;
    *pfd = fd;
    return Success;
}

