/*
 * Copyright © 2018 Roman Gilg
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

#include "xwayland.h"

#include <present.h>

/*
 * When not flipping let Present copy with 60fps.
 * When flipping wait on frame_callback, otherwise
 * the surface is not visible, in this case update
 * with long interval.
 */
#define TIMER_LEN_COPY      17  // ~60fps
#define TIMER_LEN_FLIP    1000  // 1fps

static void
xwl_present_free_timer(struct xwl_window *xwl_window)
{
    TimerFree(xwl_window->present_timer);
    xwl_window->present_timer = NULL;
}

static CARD32
xwl_present_timer_callback(OsTimerPtr timer,
                           CARD32 time,
                           void *arg);

static inline Bool
xwl_present_has_events(struct xwl_window *xwl_window)
{
    return !xorg_list_is_empty(&xwl_window->present_event_list) ||
           !xorg_list_is_empty(&xwl_window->present_release_queue);
}

static void
xwl_present_reset_timer(struct xwl_window *xwl_window)
{
    if (xwl_present_has_events(xwl_window)) {
        uint32_t timer_len = xwl_window->present_window ? TIMER_LEN_FLIP :
                                                          TIMER_LEN_COPY;

        xwl_window->present_timer = TimerSet(xwl_window->present_timer,
                                             0,
                                             timer_len,
                                             &xwl_present_timer_callback,
                                             xwl_window);
    } else {
        xwl_present_free_timer(xwl_window);
    }
}

void
xwl_present_cleanup(WindowPtr window)
{
    struct xwl_window           *xwl_window = xwl_window_of_top(window);
    struct xwl_present_event    *event, *tmp;

    if (!xwl_window)
        return;

    if (xwl_window->present_window == window) {
        if (xwl_window->present_frame_callback) {
            wl_callback_destroy(xwl_window->present_frame_callback);
            xwl_window->present_frame_callback = NULL;
        }
        xwl_window->present_window = NULL;
    }

    /* Clear remaining events */
    xorg_list_for_each_entry_safe(event, tmp, &xwl_window->present_event_list, list) {
        if (event->present_window == window) {
            xorg_list_del(&event->list);
            free(event);
        }
    }

    /* Clear remaining buffer releases and inform Present about free ressources */
    xorg_list_for_each_entry_safe(event, tmp, &xwl_window->present_release_queue, list) {
        if (event->present_window == window) {
            xorg_list_del(&event->list);
            event->abort = TRUE;
        }
    }

    /* Clear timer */
    if (!xwl_present_has_events(xwl_window))
        xwl_present_free_timer(xwl_window);
}

static void
xwl_present_free_event(struct xwl_present_event *event)
{
    xorg_list_del(&event->list);
    free(event);
}

static void
xwl_present_buffer_release(void *data, struct wl_buffer *buffer)
{
    struct xwl_present_event *event = data;

    if (!event)
        return;
    wl_buffer_set_user_data(buffer, NULL);

    event->buffer_released = TRUE;

    if (event->abort) {
        if (!event->pending)
            xwl_present_free_event(event);
        return;
    }

    if (!event->pending) {
        present_wnmd_event_notify(event->present_window,
                                  event->event_id,
                                  event->xwl_window->present_ust,
                                  event->xwl_window->present_msc);
        xwl_present_free_event(event);
    }
}

static const struct wl_buffer_listener xwl_present_release_listener = {
    xwl_present_buffer_release
};

static void
xwl_present_events_notify(struct xwl_window *xwl_window)
{
    uint64_t                    msc = xwl_window->present_msc;
    struct xwl_present_event    *event, *tmp;

    xorg_list_for_each_entry_safe(event, tmp,
                                  &xwl_window->present_event_list,
                                  list) {
        if (event->target_msc <= msc) {
            present_wnmd_event_notify(event->present_window,
                                      event->event_id,
                                      xwl_window->present_ust,
                                      msc);
            xwl_present_free_event(event);
        }
    }
}

CARD32
xwl_present_timer_callback(OsTimerPtr timer,
                           CARD32 time,
                           void *arg)
{
    struct xwl_window *xwl_window = arg;

    xwl_window->present_timer_firing = TRUE;
    xwl_window->present_msc++;
    xwl_window->present_ust = GetTimeInMicros();

    xwl_present_events_notify(xwl_window);

    if (xwl_present_has_events(xwl_window)) {
        /* Still events, restart timer */
        return xwl_window->present_window ? TIMER_LEN_FLIP :
                                            TIMER_LEN_COPY;
    } else {
        /* No more events, do not restart timer and delete it instead */
        xwl_present_free_timer(xwl_window);
        return 0;
    }
}

static void
xwl_present_frame_callback(void *data,
               struct wl_callback *callback,
               uint32_t time)
{
    struct xwl_window *xwl_window = data;

    wl_callback_destroy(xwl_window->present_frame_callback);
    xwl_window->present_frame_callback = NULL;

    if (xwl_window->present_timer_firing) {
        /* If the timer is firing, this frame callback is too late */
        return;
    }

    xwl_window->present_msc++;
    xwl_window->present_ust = GetTimeInMicros();

    xwl_present_events_notify(xwl_window);

    /* we do not need the timer anymore for this frame,
     * reset it for potentially the next one
     */
    xwl_present_reset_timer(xwl_window);
}

static const struct wl_callback_listener xwl_present_frame_listener = {
    xwl_present_frame_callback
};

static void
xwl_present_sync_callback(void *data,
               struct wl_callback *callback,
               uint32_t time)
{
    struct xwl_present_event *event = data;
    struct xwl_window *xwl_window = event->xwl_window;

    event->pending = FALSE;

    if (event->abort) {
        /* Event might have been aborted */
        if (event->buffer_released)
            /* Buffer was already released, cleanup now */
            xwl_present_free_event(event);
        return;
    }

    present_wnmd_event_notify(event->present_window,
                              event->event_id,
                              xwl_window->present_ust,
                              xwl_window->present_msc);

    if (event->buffer_released)
        /* If the buffer was already released, send the event now again */
        present_wnmd_event_notify(event->present_window,
                                  event->event_id,
                                  xwl_window->present_ust,
                                  xwl_window->present_msc);
}

static const struct wl_callback_listener xwl_present_sync_listener = {
    xwl_present_sync_callback
};

static RRCrtcPtr
xwl_present_get_crtc(WindowPtr present_window)
{
    struct xwl_window *xwl_window = xwl_window_of_top(present_window);
    if (xwl_window == NULL)
        return NULL;

    return xwl_window->present_crtc_fake;
}

static int
xwl_present_get_ust_msc(WindowPtr present_window, uint64_t *ust, uint64_t *msc)
{
    struct xwl_window *xwl_window = xwl_window_of_top(present_window);
    if (!xwl_window)
        return BadAlloc;
    *ust = xwl_window->present_ust;
    *msc = xwl_window->present_msc;

    return Success;
}

static void
xwl_present_set_present_window(struct xwl_window *xwl_window,
                               WindowPtr present_window)
{
    if (xwl_window->present_window)
        return;

    xwl_window->present_window = present_window;
}

/*
 * Queue an event to report back to the Present extension when the specified
 * MSC has past
 */
static int
xwl_present_queue_vblank(WindowPtr present_window,
                         RRCrtcPtr crtc,
                         uint64_t event_id,
                         uint64_t msc)
{
    struct xwl_window *xwl_window = xwl_window_of_top(present_window);
    struct xwl_present_event *event;

    if (!xwl_window)
        return BadMatch;

    if (xwl_window->present_crtc_fake != crtc)
        return BadRequest;

    if (xwl_window->present_window &&
            xwl_window->present_window != present_window)
        return BadMatch;

    event = malloc(sizeof *event);
    if (!event)
        return BadAlloc;

    event->event_id = event_id;
    event->present_window = present_window;
    event->xwl_window = xwl_window;
    event->target_msc = msc;

    xorg_list_append(&event->list, &xwl_window->present_event_list);

    if (!xwl_window->present_timer)
        xwl_present_reset_timer(xwl_window);

    return Success;
}

/*
 * Remove a pending vblank event so that it is not reported
 * to the extension
 */
static void
xwl_present_abort_vblank(WindowPtr present_window,
                         RRCrtcPtr crtc,
                         uint64_t event_id,
                         uint64_t msc)
{
    struct xwl_window *xwl_window = xwl_window_of_top(present_window);
    struct xwl_present_event *event, *tmp;

    if (!xwl_window)
        return;

    xorg_list_for_each_entry_safe(event, tmp, &xwl_window->present_event_list, list) {
        if (event->event_id == event_id) {
            xorg_list_del(&event->list);
            free(event);
            return;
        }
    }

    xorg_list_for_each_entry(event, &xwl_window->present_release_queue, list) {
        if (event->event_id == event_id) {
            event->abort = TRUE;
            return;
        }
    }
}

static void
xwl_present_flush(WindowPtr window)
{
    /* Only called when a Pixmap is copied instead of flipped,
     * but in this case we wait on the next block_handler.
     */
}

static Bool
xwl_present_check_flip2(RRCrtcPtr crtc,
                        WindowPtr present_window,
                        PixmapPtr pixmap,
                        Bool sync_flip,
                        PresentFlipReason *reason)
{
    struct xwl_window *xwl_window = xwl_window_of_top(present_window);

    if (!xwl_window)
        return FALSE;

    /*
     * Do not flip if there is already another child window doing flips.
     */
    if (xwl_window->present_window &&
            xwl_window->present_window != present_window)
        return FALSE;

    if (!xwl_window->present_crtc_fake)
        return FALSE;
    /*
     * Make sure the client doesn't try to flip to another crtc
     * than the one created for 'xwl_window'.
     */
    if (xwl_window->present_crtc_fake != crtc)
        return FALSE;

    /*
     * We currently only allow flips of windows, that have the same
     * dimensions as their xwl_window parent window. For the case of
     * different sizes subsurfaces are presumably the way forward.
     */
    if (!RegionEqual(&xwl_window->window->winSize, &present_window->winSize))
        return FALSE;

    return TRUE;
}

static Bool
xwl_present_flip(WindowPtr present_window,
                 RRCrtcPtr crtc,
                 uint64_t event_id,
                 uint64_t target_msc,
                 PixmapPtr pixmap,
                 Bool sync_flip,
                 RegionPtr damage)
{
    struct xwl_window           *xwl_window = xwl_window_of_top(present_window);
    BoxPtr                      present_box, damage_box;
    Bool                        buffer_created;
    struct wl_buffer            *buffer;
    struct xwl_present_event    *event;

    present_box = RegionExtents(&present_window->winSize);
    damage_box = RegionExtents(damage);

    xwl_present_set_present_window(xwl_window, present_window);

    event = malloc(sizeof *event);
    if (!event)
        return FALSE;

    buffer = xwl_glamor_pixmap_get_wl_buffer(pixmap,
                                             present_box->x2 - present_box->x1,
                                             present_box->y2 - present_box->y1,
                                             &buffer_created);

    event->event_id = event_id;
    event->present_window = present_window;
    event->xwl_window = xwl_window;
    event->buffer = buffer;
    event->target_msc = xwl_window->present_msc;
    event->pending = TRUE;
    event->abort = FALSE;
    event->buffer_released = FALSE;

    xorg_list_add(&event->list, &xwl_window->present_release_queue);

    if (buffer_created)
        wl_buffer_add_listener(buffer, &xwl_present_release_listener, NULL);
    wl_buffer_set_user_data(buffer, event);

    /* We can flip directly to the main surface (full screen window without clips) */
    wl_surface_attach(xwl_window->surface, buffer, 0, 0);

    if (!xwl_window->present_timer ||
            xwl_window->present_timer_firing) {
        /* Realign timer */
        xwl_window->present_timer_firing = FALSE;
        xwl_present_reset_timer(xwl_window);
    }

    if (!xwl_window->present_frame_callback) {
        xwl_window->present_frame_callback = wl_surface_frame(xwl_window->surface);
        wl_callback_add_listener(xwl_window->present_frame_callback,
                                 &xwl_present_frame_listener,
                                 xwl_window);
    }

    wl_surface_damage(xwl_window->surface, 0, 0,
                      damage_box->x2 - damage_box->x1,
                      damage_box->y2 - damage_box->y1);

    wl_surface_commit(xwl_window->surface);

    xwl_window->present_sync_callback = wl_display_sync(xwl_window->xwl_screen->display);
    wl_callback_add_listener(xwl_window->present_sync_callback,
                             &xwl_present_sync_listener,
                             event);

    wl_display_flush(xwl_window->xwl_screen->display);
    return TRUE;
}

static void
xwl_present_flips_stop(WindowPtr window)
{
    struct xwl_window *xwl_window = xwl_window_of_top(window);

    if (!xwl_window)
        return;

    assert(xwl_window->present_window == window);

    xwl_window->present_window = NULL;

    /* Change back to the fast refresh rate */
    xwl_present_reset_timer(xwl_window);
}

static present_wnmd_info_rec xwl_present_info = {
    .version = PRESENT_SCREEN_INFO_VERSION,
    .get_crtc = xwl_present_get_crtc,

    .get_ust_msc = xwl_present_get_ust_msc,
    .queue_vblank = xwl_present_queue_vblank,
    .abort_vblank = xwl_present_abort_vblank,

    .flush = xwl_present_flush,

    .capabilities = PresentCapabilityAsync,
    .check_flip2 = xwl_present_check_flip2,
    .flip = xwl_present_flip,
    .flips_stop = xwl_present_flips_stop
};

Bool
xwl_present_init(ScreenPtr screen)
{
    return present_wnmd_screen_init(screen, &xwl_present_info);
}
