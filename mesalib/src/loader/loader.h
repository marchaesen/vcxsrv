/*
 * Copyright (C) 2013 Rob Clark <robclark@freedesktop.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef LOADER_H
#define LOADER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct __DRIextensionRec;

/* Helpers to figure out driver and device name, eg. from pci-id, etc. */

int
loader_open_device(const char *);

int
loader_open_render_node(const char *name);

bool
loader_get_pci_id_for_fd(int fd, int *vendor_id, int *chip_id);

char *
loader_get_driver_for_fd(int fd);

const struct __DRIextensionRec **
loader_open_driver(const char *driver_name,
                   void **out_driver_handle,
                   const char **search_path_vars);

char *
loader_get_device_name_for_fd(int fd);

/* Function to get a different device than the one we are to use by default,
 * if the user requests so and it is possible. The initial fd will be closed
 * if necessary. The returned fd is potentially a render-node.
 */

int
loader_get_user_preferred_fd(int default_fd, bool *different_device);

/* for logging.. keep this aligned with egllog.h so we can just use
 * _eglLog directly.
 */

#define _LOADER_FATAL   0   /* unrecoverable error */
#define _LOADER_WARNING 1   /* recoverable error/problem */
#define _LOADER_INFO    2   /* just useful info */
#define _LOADER_DEBUG   3   /* useful info for debugging */

typedef void loader_logger(int level, const char *fmt, ...);
void
loader_set_logger(loader_logger *logger);

char *
loader_get_extensions_name(const char *driver_name);

#ifdef __cplusplus
}
#endif

#endif /* LOADER_H */
