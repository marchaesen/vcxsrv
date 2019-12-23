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

#define BUFFER_TIMEOUT 1 * 1000 /* ms */

struct xwl_window_buffer {
    struct xwl_window *xwl_window;
    PixmapPtr pixmap;
    RegionPtr damage_region;
    Bool recycle_on_release;
    uint32_t time;
    struct xorg_list link_buffer;
};

static Bool
copy_pixmap_area(PixmapPtr src_pixmap, PixmapPtr dst_pixmap,
                 int x, int y, int width, int height)
{
    GCPtr pGC;
    pGC = GetScratchGC(dst_pixmap->drawable.depth,
                       dst_pixmap->drawable.pScreen);
    if (pGC) {
        ValidateGC(&dst_pixmap->drawable, pGC);
        (void) (*pGC->ops->CopyArea) (&src_pixmap->drawable,
                                      &dst_pixmap->drawable,
                                      pGC,
                                      x, y,
                                      width, height,
                                      x, y);
        FreeScratchGC(pGC);

        return TRUE;
    }

    return FALSE;
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

    xorg_list_append(&xwl_window_buffer->link_buffer,
                     &xwl_window->window_buffers_available);

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

static void
xwl_window_buffer_dispose(struct xwl_window_buffer *xwl_window_buffer)
{
    RegionDestroy(xwl_window_buffer->damage_region);

    if (xwl_window_buffer->pixmap)
        xwl_window_buffer_destroy_pixmap (xwl_window_buffer);

    xorg_list_del(&xwl_window_buffer->link_buffer);
    free(xwl_window_buffer);
}

static void
xwl_window_buffer_recycle(struct xwl_window_buffer *xwl_window_buffer)
{
    RegionEmpty(xwl_window_buffer->damage_region);
    xwl_window_buffer->recycle_on_release = FALSE;

    if (xwl_window_buffer->pixmap)
        xwl_window_buffer_destroy_pixmap (xwl_window_buffer);
}

static void
xwl_window_buffer_add_damage_region(struct xwl_window *xwl_window,
                                    RegionPtr damage_region)
{
    struct xwl_window_buffer *xwl_window_buffer;

    /* Add damage region to all buffers */
    xorg_list_for_each_entry(xwl_window_buffer,
                             &xwl_window->window_buffers_available,
                             link_buffer) {
        RegionUnion(xwl_window_buffer->damage_region,
                    xwl_window_buffer->damage_region,
                    damage_region);
    }
    xorg_list_for_each_entry(xwl_window_buffer,
                             &xwl_window->window_buffers_unavailable,
                             link_buffer) {
        RegionUnion(xwl_window_buffer->damage_region,
                    xwl_window_buffer->damage_region,
                    damage_region);
    }
}

static struct xwl_window_buffer *
xwl_window_buffer_get_available(struct xwl_window *xwl_window)
{
    if (xorg_list_is_empty(&xwl_window->window_buffers_available))
        return xwl_window_buffer_new(xwl_window);

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
            xwl_window_buffer_dispose(xwl_window_buffer);
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
xwl_window_buffer_release_callback(PixmapPtr pixmap, void *data)
{
    struct xwl_window_buffer *xwl_window_buffer = data;
    struct xwl_window *xwl_window = xwl_window_buffer->xwl_window;
    struct xwl_window_buffer *oldest_available_buffer;

    if (xwl_window_buffer->recycle_on_release)
        xwl_window_buffer_recycle(xwl_window_buffer);

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

void
xwl_window_buffers_init(struct xwl_window *xwl_window)
{
    xorg_list_init(&xwl_window->window_buffers_available);
    xorg_list_init(&xwl_window->window_buffers_unavailable);
}

void
xwl_window_buffers_recycle(struct xwl_window *xwl_window)
{
    struct xwl_window_buffer *xwl_window_buffer, *tmp;

    /* Dispose available buffers */
    xorg_list_for_each_entry_safe(xwl_window_buffer, tmp,
                                  &xwl_window->window_buffers_available,
                                  link_buffer) {
        xwl_window_buffer_dispose(xwl_window_buffer);
    }

    if (xwl_window->window_buffers_timer)
        TimerCancel(xwl_window->window_buffers_timer);

    /* Mark the others for recycle on release */
    xorg_list_for_each_entry(xwl_window_buffer,
                             &xwl_window->window_buffers_unavailable,
                             link_buffer) {
        xwl_window_buffer->recycle_on_release = TRUE;
    }
}

void
xwl_window_buffers_dispose(struct xwl_window *xwl_window)
{
    struct xwl_window_buffer *xwl_window_buffer, *tmp;

    xorg_list_for_each_entry_safe(xwl_window_buffer, tmp,
                                  &xwl_window->window_buffers_available,
                                  link_buffer) {
        xwl_window_buffer_dispose(xwl_window_buffer);
    }

    xorg_list_for_each_entry_safe(xwl_window_buffer, tmp,
                                  &xwl_window->window_buffers_unavailable,
                                  link_buffer) {
        xwl_window_buffer_dispose(xwl_window_buffer);
    }

    if (xwl_window->window_buffers_timer) {
        TimerFree(xwl_window->window_buffers_timer);
        xwl_window->window_buffers_timer = 0;
    }
}

PixmapPtr
xwl_window_buffers_get_pixmap(struct xwl_window *xwl_window,
                              RegionPtr damage_region)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;
    struct xwl_window_buffer *xwl_window_buffer;
    PixmapPtr window_pixmap;
    RegionPtr full_damage;

    window_pixmap = (*xwl_screen->screen->GetWindowPixmap) (xwl_window->window);

    xwl_window_buffer = xwl_window_buffer_get_available(xwl_window);
    if (!xwl_window_buffer)
        return window_pixmap;

    xwl_window_buffer_add_damage_region(xwl_window, damage_region);

    full_damage = xwl_window_buffer->damage_region;

    if (xwl_window_buffer->pixmap) {
        BoxPtr pBox = RegionRects(full_damage);
        int nBox = RegionNumRects(full_damage);
        while (nBox--) {
            if (!copy_pixmap_area(window_pixmap,
                                  xwl_window_buffer->pixmap,
                                  pBox->x1 + xwl_window->window->borderWidth,
                                  pBox->y1 + xwl_window->window->borderWidth,
                                  pBox->x2 - pBox->x1,
                                  pBox->y2 - pBox->y1))
                return window_pixmap;

            pBox++;
        }
    } else {
        xwl_window_buffer->pixmap =
            (*xwl_screen->screen->CreatePixmap) (window_pixmap->drawable.pScreen,
                                                 window_pixmap->drawable.width,
                                                 window_pixmap->drawable.height,
                                                 window_pixmap->drawable.depth,
                                                 CREATE_PIXMAP_USAGE_BACKING_PIXMAP);

        if (!xwl_window_buffer->pixmap)
            return window_pixmap;

        if (!copy_pixmap_area(window_pixmap,
                              xwl_window_buffer->pixmap,
                              0, 0,
                              window_pixmap->drawable.width,
                              window_pixmap->drawable.height)) {
            xwl_window_buffer_recycle(xwl_window_buffer);
            return window_pixmap;
        }
    }

    RegionEmpty(xwl_window_buffer->damage_region);

    xwl_pixmap_set_buffer_release_cb(xwl_window_buffer->pixmap,
                                     xwl_window_buffer_release_callback,
                                     xwl_window_buffer);

    xorg_list_del(&xwl_window_buffer->link_buffer);
    xorg_list_append(&xwl_window_buffer->link_buffer,
                     &xwl_window->window_buffers_unavailable);

    if (xorg_list_is_empty(&xwl_window->window_buffers_available))
        TimerCancel(xwl_window->window_buffers_timer);

    return xwl_window_buffer->pixmap;
}
