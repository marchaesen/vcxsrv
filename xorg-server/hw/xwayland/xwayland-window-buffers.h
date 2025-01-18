/*
 * Copyright © 2019 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *      Olivier Fourdan <ofourdan@redhat.com>
 */

#ifndef XWAYLAND_WINDOW_BUFFERS_H
#define XWAYLAND_WINDOW_BUFFERS_H

#include <xwayland-config.h>

#include "xwayland-types.h"

void xwl_window_buffer_add_damage_region(struct xwl_window *xwl_window);
void xwl_window_buffer_release(struct xwl_window_buffer *xwl_window_buffer);
void xwl_window_buffers_init(struct xwl_window *xwl_window);
void xwl_window_buffers_dispose(struct xwl_window *xwl_window, Bool force);
void xwl_window_realloc_pixmap(struct xwl_window *xwl_window);
PixmapPtr xwl_window_swap_pixmap(struct xwl_window *xwl_window, Bool handle_sync);

#endif /* XWAYLAND_WINDOW_BUFFERS_H */
