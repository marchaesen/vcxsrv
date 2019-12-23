/*
 * Copyright © 2014 Intel Corporation
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

#include <xwayland-config.h>

#include <X11/X.h>

#include "os.h"
#include "privates.h"
#include "dix.h"
#include "fb.h"
#include "pixmapstr.h"

#include "xwayland-types.h"
#include "xwayland-pixmap.h"
#include "xwayland-window-buffers.h"

static DevPrivateKeyRec xwl_pixmap_private_key;
static DevPrivateKeyRec xwl_pixmap_cb_private_key;

struct xwl_pixmap_buffer_release_callback {
    xwl_pixmap_cb callback;
    void *data;
};

void
xwl_pixmap_set_private(PixmapPtr pixmap, struct xwl_pixmap *xwl_pixmap)
{
    dixSetPrivate(&pixmap->devPrivates, &xwl_pixmap_private_key, xwl_pixmap);
}

struct xwl_pixmap *
xwl_pixmap_get(PixmapPtr pixmap)
{
    return dixLookupPrivate(&pixmap->devPrivates, &xwl_pixmap_private_key);
}

Bool
xwl_pixmap_set_buffer_release_cb(PixmapPtr pixmap,
                                 xwl_pixmap_cb func, void *data)
{
    struct xwl_pixmap_buffer_release_callback *xwl_pixmap_buffer_release_callback;

    xwl_pixmap_buffer_release_callback = dixLookupPrivate(&pixmap->devPrivates,
                                                          &xwl_pixmap_cb_private_key);

    if (xwl_pixmap_buffer_release_callback == NULL) {
        xwl_pixmap_buffer_release_callback =
            calloc(1, sizeof (struct xwl_pixmap_buffer_release_callback));

        if (xwl_pixmap_buffer_release_callback == NULL) {
            ErrorF("Failed to allocate pixmap callback data\n");
            return FALSE;
        }
        dixSetPrivate(&pixmap->devPrivates, &xwl_pixmap_cb_private_key,
                      xwl_pixmap_buffer_release_callback);
    }

    xwl_pixmap_buffer_release_callback->callback = func;
    xwl_pixmap_buffer_release_callback->data = data;

    return TRUE;
}

void
xwl_pixmap_del_buffer_release_cb(PixmapPtr pixmap)
{
    struct xwl_pixmap_buffer_release_callback *xwl_pixmap_buffer_release_callback;

    xwl_pixmap_buffer_release_callback = dixLookupPrivate(&pixmap->devPrivates,
                                                          &xwl_pixmap_cb_private_key);
    if (xwl_pixmap_buffer_release_callback) {
        dixSetPrivate(&pixmap->devPrivates, &xwl_pixmap_cb_private_key, NULL);
        free(xwl_pixmap_buffer_release_callback);
    }
}

void
xwl_pixmap_buffer_release_cb(void *data, struct wl_buffer *wl_buffer)
{
    PixmapPtr pixmap = data;
    struct xwl_pixmap_buffer_release_callback *xwl_pixmap_buffer_release_callback;

    xwl_pixmap_buffer_release_callback = dixLookupPrivate(&pixmap->devPrivates,
                                                          &xwl_pixmap_cb_private_key);
    if (xwl_pixmap_buffer_release_callback)
        (*xwl_pixmap_buffer_release_callback->callback)
            (pixmap, xwl_pixmap_buffer_release_callback->data);
}

Bool
xwl_pixmap_init(void)
{
    if (!dixRegisterPrivateKey(&xwl_pixmap_private_key, PRIVATE_PIXMAP, 0))
        return FALSE;

    if (!dixRegisterPrivateKey(&xwl_pixmap_cb_private_key, PRIVATE_PIXMAP, 0))
        return FALSE;

    return TRUE;
}
