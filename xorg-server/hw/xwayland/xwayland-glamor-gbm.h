/*
 * Copyright © 2011-2014 Intel Corporation
 * Copyright © 2024 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including
 * the next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef XWAYLAND_GLAMOR_GBM_H
#define XWAYLAND_GLAMOR_GBM_H

#include <xwayland-config.h>

#include <sys/types.h>

#include <xf86drm.h>

#include "xwayland-types.h"

Bool xwl_glamor_init_gbm(struct xwl_screen *xwl_screen);
Bool xwl_glamor_has_wl_drm(struct xwl_screen *xwl_screen);
Bool xwl_glamor_gbm_init_egl(struct xwl_screen *xwl_screen);
Bool xwl_glamor_gbm_init_screen(struct xwl_screen *xwl_screen);
drmDevice *xwl_gbm_get_main_device(struct xwl_screen *xwl_screen);

/* Explicit buffer synchronization points */
Bool xwl_glamor_gbm_set_syncpts(struct xwl_window *xwl_window, PixmapPtr pixmap);
void xwl_glamor_gbm_dispose_syncpts(PixmapPtr pixmap);
void xwl_glamor_gbm_wait_syncpts(PixmapPtr pixmap);
void xwl_glamor_gbm_wait_release_fence(struct xwl_window *xwl_window,
                                       PixmapPtr pixmap,
                                       struct xwl_window_buffer *xwl_window_buffer);

#endif /* XWAYLAND_GLAMOR_GBM_H */
