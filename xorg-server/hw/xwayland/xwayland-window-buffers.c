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

#include <xwayland-config.h>

#include "gcstruct.h"

#include "xwayland-window.h"
#include "xwayland-pixmap.h"
#include "xwayland-screen.h"
#include "xwayland-window-buffers.h"
#ifdef XWL_HAS_GLAMOR
#include "glamor.h"
#endif
#include "dri3.h"

#include <poll.h>
#include <sys/eventfd.h>
#include "linux-drm-syncobj-v1-client-protocol.h"

#define BUFFER_TIMEOUT 1 * 1000 /* ms */

struct xwl_window_buffer {
    struct xwl_window *xwl_window;
    PixmapPtr pixmap;
    RegionPtr damage_region;
#ifdef XWL_HAS_GLAMOR
    struct dri3_syncobj *syncobj;
    uint64_t timeline_point;
    int efd;
#endif /* XWL_HAS_GLAMOR */
    int refcnt;
    uint32_t time;
    struct xorg_list link_buffer;
};

static void
copy_pixmap_area(PixmapPtr src_pixmap, PixmapPtr dst_pixmap,
                 int x, int y, int width, int height)
{
    GCPtr pGC;
    pGC = GetScratchGC(dst_pixmap->drawable.depth,
                       dst_pixmap->drawable.pScreen);
    if (!pGC)
        FatalError("GetScratchGC failed for depth %d", dst_pixmap->drawable.depth);

    ValidateGC(&dst_pixmap->drawable, pGC);
    (void) (*pGC->ops->CopyArea) (&src_pixmap->drawable,
                                  &dst_pixmap->drawable,
                                  pGC,
                                  x, y,
                                  width, height,
                                  x, y);
    FreeScratchGC(pGC);
}

static struct xwl_window_buffer *
xwl_window_buffer_new(struct xwl_window *xwl_window)
{
    struct xwl_window_buffer *xwl_window_buffer;

    xwl_window_buffer = calloc (1, sizeof(struct xwl_window_buffer));
    if (!xwl_window_buffer)
        return NULL;

    xwl_window_buffer->xwl_window = xwl_window;
    xwl_window_buffer->damage_region = RegionCreate(NullBox, 1);
    xwl_window_buffer->pixmap = NullPixmap;
    xwl_window_buffer->refcnt = 1;
#ifdef XWL_HAS_GLAMOR
    xwl_window_buffer->efd = -1;
#endif /* XWL_HAS_GLAMOR */

    xorg_list_init(&xwl_window_buffer->link_buffer);

    return xwl_window_buffer;
}

static void
xwl_window_buffer_destroy_pixmap(struct xwl_window_buffer *xwl_window_buffer)
{
    ScreenPtr pScreen = xwl_window_buffer->pixmap->drawable.pScreen;

    xwl_pixmap_del_buffer_release_cb(xwl_window_buffer->pixmap);
    (*pScreen->DestroyPixmap) (xwl_window_buffer->pixmap);
    xwl_window_buffer->pixmap = NullPixmap;
}

static Bool
xwl_window_buffer_maybe_dispose(struct xwl_window_buffer *xwl_window_buffer)
{
    assert(xwl_window_buffer->refcnt > 0);

    if (--xwl_window_buffer->refcnt)
        return FALSE;

    RegionDestroy(xwl_window_buffer->damage_region);

    if (xwl_window_buffer->pixmap)
        xwl_window_buffer_destroy_pixmap (xwl_window_buffer);

#ifdef XWL_HAS_GLAMOR
    if (xwl_window_buffer->syncobj)
        xwl_window_buffer->syncobj->free(xwl_window_buffer->syncobj);

    if (xwl_window_buffer->efd >= 0) {
        SetNotifyFd(xwl_window_buffer->efd, NULL, 0, NULL);
        close(xwl_window_buffer->efd);
    }
#endif /* XWL_HAS_GLAMOR */

    xorg_list_del(&xwl_window_buffer->link_buffer);
    free(xwl_window_buffer);

    return TRUE;
}

void
xwl_window_buffer_add_damage_region(struct xwl_window *xwl_window)
{
    RegionPtr region = xwl_window_get_damage_region(xwl_window);
    struct xwl_window_buffer *xwl_window_buffer;

    /* Add damage region to all buffers */
    xorg_list_for_each_entry(xwl_window_buffer,
                             &xwl_window->window_buffers_available,
                             link_buffer) {
        RegionUnion(xwl_window_buffer->damage_region,
                    xwl_window_buffer->damage_region,
                    region);
    }
    xorg_list_for_each_entry(xwl_window_buffer,
                             &xwl_window->window_buffers_unavailable,
                             link_buffer) {
        RegionUnion(xwl_window_buffer->damage_region,
                    xwl_window_buffer->damage_region,
                    region);
    }
}

static struct xwl_window_buffer *
xwl_window_buffer_get_available(struct xwl_window *xwl_window)
{
    if (xorg_list_is_empty(&xwl_window->window_buffers_available))
        return NULL;

    return xorg_list_last_entry(&xwl_window->window_buffers_available,
                                struct xwl_window_buffer,
                                link_buffer);
}

static CARD32
xwl_window_buffer_timer_callback(OsTimerPtr timer, CARD32 time, void *arg)
{
    struct xwl_window *xwl_window = arg;
    struct xwl_window_buffer *xwl_window_buffer, *tmp;

    /* Dispose older available buffers */
    xorg_list_for_each_entry_safe(xwl_window_buffer, tmp,
                                  &xwl_window->window_buffers_available,
                                  link_buffer) {
        if ((int64_t)(time - xwl_window_buffer->time) >= BUFFER_TIMEOUT)
            xwl_window_buffer_maybe_dispose(xwl_window_buffer);
    }

    /* If there are still available buffers, re-arm the timer */
    if (!xorg_list_is_empty(&xwl_window->window_buffers_available)) {
        struct xwl_window_buffer *oldest_available_buffer =
            xorg_list_first_entry(&xwl_window->window_buffers_available,
                                  struct xwl_window_buffer,
                                  link_buffer);

        return oldest_available_buffer->time + BUFFER_TIMEOUT - time;
    }

    /* Don't re-arm the timer */
    return 0;
}

static void
xwl_window_buffer_release_callback(void *data)
{
    struct xwl_window_buffer *xwl_window_buffer = data;
    struct xwl_window *xwl_window = xwl_window_buffer->xwl_window;
    struct xwl_window_buffer *oldest_available_buffer;

    /* Drop the reference on the buffer we took in get_pixmap. If that
     * frees the window buffer, we're done.
     */
    if (xwl_window_buffer_maybe_dispose(xwl_window_buffer))
        return;

    /* We append the buffers to the end of the list, as we pick the last
     * entry again when looking for new available buffers, that means the
     * least used buffers will remain at the beginning of the list so that
     * they can be garbage collected automatically after some time unused.
     */

    xorg_list_del(&xwl_window_buffer->link_buffer);
    xorg_list_append(&xwl_window_buffer->link_buffer,
                     &xwl_window->window_buffers_available);
    xwl_window_buffer->time = (uint32_t) GetTimeInMillis();

    oldest_available_buffer =
        xorg_list_first_entry(&xwl_window->window_buffers_available,
                             struct xwl_window_buffer,
                             link_buffer);

    /* Schedule next timer based on time of the oldest buffer */
    xwl_window->window_buffers_timer =
        TimerSet(xwl_window->window_buffers_timer,
                 TimerAbsolute,
                 oldest_available_buffer->time + BUFFER_TIMEOUT,
                 &xwl_window_buffer_timer_callback,
                 xwl_window);
}

#ifdef XWL_HAS_GLAMOR
static void
xwl_window_buffers_release_fence_avail(int fd, int xevents, void *data)
{
    struct xwl_window_buffer *xwl_window_buffer = data;

    SetNotifyFd(fd, NULL, 0, NULL);
    close(fd);
    xwl_window_buffer->efd = -1;

    xwl_window_buffer_release_callback(data);
}
#endif /* XWL_HAS_GLAMOR */

void
xwl_window_buffers_init(struct xwl_window *xwl_window)
{
    xorg_list_init(&xwl_window->window_buffers_available);
    xorg_list_init(&xwl_window->window_buffers_unavailable);
}

void
xwl_window_buffers_dispose(struct xwl_window *xwl_window)
{
    struct xwl_window_buffer *xwl_window_buffer, *tmp;

    /* This is called prior to free the xwl_window, make sure to untie
     * the buffers from the xwl_window so that we don't point at freed
     * memory if we get a release buffer later.
     */
    xorg_list_for_each_entry_safe(xwl_window_buffer, tmp,
                                  &xwl_window->window_buffers_available,
                                  link_buffer) {
        xorg_list_del(&xwl_window_buffer->link_buffer);
        xwl_window_buffer_maybe_dispose(xwl_window_buffer);
    }

    xorg_list_for_each_entry_safe(xwl_window_buffer, tmp,
                                  &xwl_window->window_buffers_unavailable,
                                  link_buffer) {
        xorg_list_del(&xwl_window_buffer->link_buffer);
        xwl_window_buffer_maybe_dispose(xwl_window_buffer);
    }

    if (xwl_window->window_buffers_timer)
        TimerCancel(xwl_window->window_buffers_timer);
}

struct pixmap_visit {
    PixmapPtr old;
    PixmapPtr new;
};

static int
xwl_set_pixmap_visit_window(WindowPtr window, void *data)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct pixmap_visit *visit = data;

    if (screen->GetWindowPixmap(window) == visit->old) {
        screen->SetWindowPixmap(window, visit->new);
        return WT_WALKCHILDREN;
    }

    return WT_DONTWALKCHILDREN;
}

static void
xwl_window_set_pixmap(WindowPtr window, PixmapPtr pixmap)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct pixmap_visit visit;

    visit.old = screen->GetWindowPixmap(window);
    visit.new = pixmap;

#ifdef COMPOSITE
    pixmap->screen_x = visit.old->screen_x;
    pixmap->screen_y = visit.old->screen_y;
#endif

    TraverseTree(window, xwl_set_pixmap_visit_window, &visit);

    if (window == screen->root &&
        screen->GetScreenPixmap(screen) == visit.old)
        screen->SetScreenPixmap(pixmap);
}

static PixmapPtr
xwl_window_allocate_pixmap(struct xwl_window *xwl_window)
{
    ScreenPtr screen = xwl_window->xwl_screen->screen;
    PixmapPtr window_pixmap;

#ifdef XWL_HAS_GLAMOR
    /* Try the xwayland/glamor direct hook first */
    window_pixmap = xwl_glamor_create_pixmap_for_window(xwl_window);
    if (window_pixmap)
        return window_pixmap;
#endif /* XWL_HAS_GLAMOR */

    window_pixmap = screen->GetWindowPixmap(xwl_window->surface_window);
    return screen->CreatePixmap(screen,
                                window_pixmap->drawable.width,
                                window_pixmap->drawable.height,
                                window_pixmap->drawable.depth,
                                CREATE_PIXMAP_USAGE_BACKING_PIXMAP);
}

void
xwl_window_realloc_pixmap(struct xwl_window *xwl_window)
{
    PixmapPtr window_pixmap, new_window_pixmap;
    WindowPtr window;
    ScreenPtr screen;

    new_window_pixmap = xwl_window_allocate_pixmap(xwl_window);
    if (!new_window_pixmap)
        return;

    window = xwl_window->surface_window;
    screen = window->drawable.pScreen;
    window_pixmap = screen->GetWindowPixmap(window);
    copy_pixmap_area(window_pixmap,
                     new_window_pixmap,
                     0, 0,
                     window_pixmap->drawable.width,
                     window_pixmap->drawable.height);
    xwl_window_set_pixmap(xwl_window->surface_window, new_window_pixmap);
    screen->DestroyPixmap(window_pixmap);
}

#ifdef XWL_HAS_GLAMOR
static Bool
xwl_window_buffers_set_syncpts(struct xwl_window_buffer *xwl_window_buffer)
{
    struct xwl_window *xwl_window = xwl_window_buffer->xwl_window;
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;
    uint64_t acquire_point = ++xwl_window_buffer->timeline_point;
    uint64_t release_point = ++xwl_window_buffer->timeline_point;

    if (!xwl_window_buffer->syncobj) {
        struct dri3_syncobj *syncobj = xwl_glamor_dri3_syncobj_create(xwl_screen);
        if (!syncobj)
            goto fail;
        xwl_window_buffer->syncobj = syncobj;
    }

    int fence_fd = xwl_glamor_get_fence(xwl_screen);
    if (fence_fd >= 0)
        xwl_window_buffer->syncobj->import_fence(xwl_window_buffer->syncobj,
                                                 acquire_point, fence_fd);
    else
        goto fail;

    xwl_glamor_dri3_syncobj_passthrough(xwl_window,
                                        xwl_window_buffer->syncobj,
                                        xwl_window_buffer->syncobj,
                                        acquire_point,
                                        release_point);
    return TRUE;

fail:
    /* can't use explicit sync, we will do a glFinish() before presenting */
    if (xwl_window_buffer->syncobj) {
        xwl_window_buffer->syncobj->free(xwl_window_buffer->syncobj);
        xwl_window_buffer->syncobj = NULL;
    }
    return FALSE;
}
#endif /* XWL_HAS_GLAMOR */

PixmapPtr
xwl_window_swap_pixmap(struct xwl_window *xwl_window)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;
    WindowPtr surface_window = xwl_window->surface_window;
    struct xwl_window_buffer *xwl_window_buffer;
    PixmapPtr window_pixmap;
    Bool implicit_sync = TRUE;

    window_pixmap = (*xwl_screen->screen->GetWindowPixmap) (surface_window);

    xwl_window_buffer_add_damage_region(xwl_window);

    xwl_window_buffer = xwl_window_buffer_get_available(xwl_window);
    if (xwl_window_buffer) {
        RegionPtr full_damage = xwl_window_buffer->damage_region;
        BoxPtr pBox = RegionRects(full_damage);
        int nBox = RegionNumRects(full_damage);

#ifdef XWL_HAS_GLAMOR
        if (xwl_window_buffer->syncobj) {
            int fence_fd =
                xwl_window_buffer->syncobj->export_fence(xwl_window_buffer->syncobj,
                                                         xwl_window_buffer->timeline_point);
            xwl_glamor_wait_fence(xwl_screen, fence_fd);
            close(fence_fd);
        }
#endif /* XWL_HAS_GLAMOR */

        while (nBox--) {
            copy_pixmap_area(window_pixmap,
                             xwl_window_buffer->pixmap,
                             pBox->x1 + surface_window->borderWidth,
                             pBox->y1 + surface_window->borderWidth,
                             pBox->x2 - pBox->x1,
                             pBox->y2 - pBox->y1);

            pBox++;
        }

        RegionEmpty(xwl_window_buffer->damage_region);
        xorg_list_del(&xwl_window_buffer->link_buffer);
        xwl_window_set_pixmap(surface_window, xwl_window_buffer->pixmap);

        /* Can't re-use client pixmap as a window buffer */
        if (xwl_is_client_pixmap(window_pixmap)) {
            xwl_window_buffer->pixmap = NULL;
            xwl_window_buffer_maybe_dispose(xwl_window_buffer);
            return window_pixmap;
        }
    } else {
        /* Can't re-use client pixmap as a window buffer */
        if (!xwl_is_client_pixmap(window_pixmap))
            xwl_window_buffer = xwl_window_buffer_new(xwl_window);

        window_pixmap->refcnt++;
        xwl_window_realloc_pixmap(xwl_window);

        if (!xwl_window_buffer)
            return window_pixmap;
    }

    xwl_window_buffer->pixmap = window_pixmap;

    /* Hold a reference on the buffer until it's released by the compositor */
    xwl_window_buffer->refcnt++;

#ifdef XWL_HAS_GLAMOR
    if (!xwl_glamor_supports_implicit_sync(xwl_screen)) {
        if (xwl_screen->explicit_sync && xwl_window_buffers_set_syncpts(xwl_window_buffer)) {
            implicit_sync = FALSE;
            /* wait until the release fence is available before re-using this buffer */
            xwl_window_buffer->efd = eventfd(0, EFD_CLOEXEC);
            SetNotifyFd(xwl_window_buffer->efd, xwl_window_buffers_release_fence_avail,
                        X_NOTIFY_READ, xwl_window_buffer);
            xwl_window_buffer->syncobj->submitted_eventfd(xwl_window_buffer->syncobj,
                                                          xwl_window_buffer->timeline_point,
                                                          xwl_window_buffer->efd);
        } else
            /* If glamor does not support implicit sync and we can't use
             * explicit sync, wait for the GPU to be idle before presenting.
             * Note that buffer re-use will still be unsynchronized :(
             */
            glamor_finish(xwl_screen->screen);
    }
#endif /* XWL_HAS_GLAMOR */

    if (implicit_sync) {
        xwl_pixmap_set_buffer_release_cb(xwl_window_buffer->pixmap,
                                         xwl_window_buffer_release_callback,
                                         xwl_window_buffer);

        if (xwl_window->surface_sync) {
            wp_linux_drm_syncobj_surface_v1_destroy(xwl_window->surface_sync);
            xwl_window->surface_sync = NULL;
        }
    }

    xorg_list_append(&xwl_window_buffer->link_buffer,
                     &xwl_window->window_buffers_unavailable);

    if (xorg_list_is_empty(&xwl_window->window_buffers_available))
        TimerCancel(xwl_window->window_buffers_timer);

    return xwl_window_buffer->pixmap;
}
