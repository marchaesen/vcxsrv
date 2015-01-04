/*
 * Copyright © 2014 Intel Corporation
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

#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#include <xf86.h>
#include <xf86Crtc.h>
#include <xf86drm.h>
#include <xf86str.h>
#include <present.h>

#include "driver.h"

#if 0
#define DebugPresent(x) ErrorF x
#else
#define DebugPresent(x)
#endif

struct ms_present_vblank_event {
    uint64_t        event_id;
};

static RRCrtcPtr
ms_present_get_crtc(WindowPtr window)
{
    xf86CrtcPtr xf86_crtc = ms_dri2_crtc_covering_drawable(&window->drawable);
    return xf86_crtc ? xf86_crtc->randr_crtc : NULL;
}

static int
ms_present_get_ust_msc(RRCrtcPtr crtc, CARD64 *ust, CARD64 *msc)
{
    xf86CrtcPtr xf86_crtc = crtc->devPrivate;

    return ms_get_crtc_ust_msc(xf86_crtc, ust, msc);
}

/*
 * Flush the DRM event queue when full; makes space for new events.
 */
static Bool
ms_flush_drm_events(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);

    struct pollfd p = { .fd = ms->fd, .events = POLLIN };
    int r;

    do {
            r = poll(&p, 1, 0);
    } while (r == -1 && (errno == EINTR || errno == EAGAIN));

    if (r <= 0)
        return TRUE;

    return drmHandleEvent(ms->fd, &ms->event_context) >= 0;
}

/*
 * Called when the queued vblank event has occurred
 */
static void
ms_present_vblank_handler(uint64_t msc, uint64_t usec, void *data)
{
    struct ms_present_vblank_event *event = data;

    DebugPresent(("\t\tmh %lld msc %llu\n",
                 (long long) event->event_id, (long long) msc));

    present_event_notify(event->event_id, usec, msc);
    free(event);
}

/*
 * Called when the queued vblank is aborted
 */
static void
ms_present_vblank_abort(void *data)
{
    struct ms_present_vblank_event *event = data;

    DebugPresent(("\t\tma %lld\n", (long long) event->event_id));

    free(event);
}

/*
 * Queue an event to report back to the Present extension when the specified
 * MSC has past
 */
static int
ms_present_queue_vblank(RRCrtcPtr crtc,
                        uint64_t event_id,
                        uint64_t msc)
{
    xf86CrtcPtr xf86_crtc = crtc->devPrivate;
    ScreenPtr screen = crtc->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    drmmode_crtc_private_ptr drmmode_crtc = xf86_crtc->driver_private;
    struct ms_present_vblank_event *event;
    drmVBlank vbl;
    int ret;
    uint32_t seq;

    event = calloc(sizeof(struct ms_present_vblank_event), 1);
    if (!event)
        return BadAlloc;
    event->event_id = event_id;
    seq = ms_drm_queue_alloc(xf86_crtc, event,
                             ms_present_vblank_handler,
                             ms_present_vblank_abort);
    if (!seq) {
        free(event);
        return BadAlloc;
    }

    vbl.request.type =
        DRM_VBLANK_ABSOLUTE | DRM_VBLANK_EVENT | drmmode_crtc->vblank_pipe;
    vbl.request.sequence = ms_crtc_msc_to_kernel_msc(xf86_crtc, msc);
    vbl.request.signal = seq;
    for (;;) {
        ret = drmWaitVBlank(ms->fd, &vbl);
        if (!ret)
            break;
        if (errno != EBUSY || !ms_flush_drm_events(screen))
            return BadAlloc;
    }
    DebugPresent(("\t\tmq %lld seq %u msc %llu (hw msc %u)\n",
                 (long long) event_id, seq, (long long) msc,
                 vbl.request.sequence));
    return Success;
}

static Bool
ms_present_event_match(void *data, void *match_data)
{
    struct ms_present_vblank_event *event = data;
    uint64_t *match = match_data;

    return *match == event->event_id;
}

/*
 * Remove a pending vblank event from the DRM queue so that it is not reported
 * to the extension
 */
static void
ms_present_abort_vblank(RRCrtcPtr crtc, uint64_t event_id, uint64_t msc)
{
    ScreenPtr screen = crtc->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);

    ms_drm_abort(scrn, ms_present_event_match, &event_id);
}

/*
 * Flush our batch buffer when requested by the Present extension.
 */
static void
ms_present_flush(WindowPtr window)
{
#ifdef GLAMOR
    ScreenPtr screen = window->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);

    if (ms->drmmode.glamor)
        glamor_block_handler(screen);
#endif
}

static present_screen_info_rec ms_present_screen_info = {
    .version = PRESENT_SCREEN_INFO_VERSION,

    .get_crtc = ms_present_get_crtc,
    .get_ust_msc = ms_present_get_ust_msc,
    .queue_vblank = ms_present_queue_vblank,
    .abort_vblank = ms_present_abort_vblank,
    .flush = ms_present_flush,

    .capabilities = PresentCapabilityNone,
    .check_flip = 0,
    .flip = 0,
    .unflip = 0,
};

Bool
ms_present_screen_init(ScreenPtr screen)
{
    return present_screen_init(screen, &ms_present_screen_info);
}
