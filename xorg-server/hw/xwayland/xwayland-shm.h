/*
 * Copyright © 2014 Intel Corporation
 * Copyright © 2012 Collabora, Ltd.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of the
 * copyright holders not be used in advertising or publicity
 * pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#ifndef XWAYLAND_SHM_H
#define XWAYLAND_SHM_H

#include <xwayland-config.h>

#include "scrnintstr.h"
#include "pixmapstr.h"

Bool xwl_shm_create_screen_resources(ScreenPtr screen);
PixmapPtr xwl_shm_create_pixmap(ScreenPtr screen, int width, int height,
                                int depth, unsigned int hint);
Bool xwl_shm_destroy_pixmap(PixmapPtr pixmap);
struct wl_buffer *xwl_shm_pixmap_get_wl_buffer(PixmapPtr pixmap);

#endif /* XWAYLAND_SHM_H */
