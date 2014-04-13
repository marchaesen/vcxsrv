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

#ifndef _DRI3_H_
#define _DRI3_H_

#include <xorg-server.h>

#ifdef DRI3

#include <X11/extensions/dri3proto.h>
#include <randrstr.h>

#define DRI3_SCREEN_INFO_VERSION        1

typedef int (*dri3_open_proc)(ScreenPtr screen,
                              RRProviderPtr provider,
                              int *fd);

typedef int (*dri3_open_client_proc)(ClientPtr client,
                                     ScreenPtr screen,
                                     RRProviderPtr provider,
                                     int *fd);

typedef PixmapPtr (*dri3_pixmap_from_fd_proc) (ScreenPtr screen,
                                               int fd,
                                               CARD16 width,
                                               CARD16 height,
                                               CARD16 stride,
                                               CARD8 depth,
                                               CARD8 bpp);

typedef int (*dri3_fd_from_pixmap_proc) (ScreenPtr screen,
                                         PixmapPtr pixmap,
                                         CARD16 *stride,
                                         CARD32 *size);

typedef struct dri3_screen_info {
    uint32_t                    version;

    dri3_open_proc              open;
    dri3_pixmap_from_fd_proc    pixmap_from_fd;
    dri3_fd_from_pixmap_proc    fd_from_pixmap;

    /* Version 1 */
    dri3_open_client_proc       open_client;

} dri3_screen_info_rec, *dri3_screen_info_ptr;

extern _X_EXPORT Bool
dri3_screen_init(ScreenPtr screen, dri3_screen_info_ptr info);

extern _X_EXPORT int
dri3_send_open_reply(ClientPtr client, int fd);

#endif

#endif /* _DRI3_H_ */
