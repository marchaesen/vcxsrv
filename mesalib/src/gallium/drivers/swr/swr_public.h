/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ***************************************************************************/

#ifndef SWR_PUBLIC_H
#define SWR_PUBLIC_H

struct pipe_screen;
struct pipe_context;
struct sw_displaytarget;
struct sw_winsys;
struct swr_screen;

#ifdef __cplusplus
extern "C" {
#endif

// driver entry point
struct pipe_screen *swr_create_screen(struct sw_winsys *winsys);

// arch-specific dll entry point
struct pipe_screen *swr_create_screen_internal(struct sw_winsys *winsys);

// cleanup for failed screen creation
void swr_destroy_screen_internal(struct swr_screen **screen);

#ifdef _WIN32
void swr_gdi_swap(struct pipe_screen *screen,
                  struct pipe_context *ctx,
                  struct pipe_resource *res,
                  void *hDC);
#endif /* _WIN32 */

#ifdef __cplusplus
}
#endif

#endif
