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
#include "drmmode_display.h"

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
 *
 * Returns a negative value on error, 0 if there was nothing to process,
 * or 1 if we handled any events.
 */
static int
ms_flush_drm_events(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);

    struct pollfd p = { .fd = ms->fd, .events = POLLIN };
    int r;

    do {
            r = poll(&p, 1, 0);
    } while (r == -1 && (errno == EINTR || errno == EAGAIN));

    /* If there was an error, r will be < 0.  Return that.  If there was
     * nothing to process, r == 0.  Return that.
     */
    if (r <= 0)
        return r;

    /* Try to handle the event.  If there was an error, return it. */
    r = drmHandleEvent(ms->fd, &ms->event_context);
    if (r < 0)
        return r;

    /* Otherwise return 1 to indicate that we handled an event. */
    return 1;
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
        /* If we hit EBUSY, then try to flush events.  If we can't, then
         * this is an error.
         */
        if (errno != EBUSY || ms_flush_drm_events(screen) < 0) {
	    ms_drm_abort_seq(scrn, seq);
            return BadAlloc;
        }
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

#ifdef GLAMOR

/*
 * Event data for an in progress flip.
 * This contains a pointer to the vblank event,
 * and information about the flip in progress.
 * a reference to this is stored in the per-crtc
 * flips.
 */
struct ms_flipdata {
    ScreenPtr screen;
    struct ms_present_vblank_event *event;
    /* number of CRTC events referencing this */
    int flip_count;
    uint64_t fe_msc;
    uint64_t fe_usec;
    uint32_t old_fb_id;
};

/*
 * Per crtc pageflipping infomation,
 * These are submitted to the queuing code
 * one of them per crtc per flip.
 */
struct ms_crtc_pageflip {
    Bool on_reference_crtc;
    /* reference to the ms_flipdata */
    struct ms_flipdata *flipdata;
};

/**
 * Free an ms_crtc_pageflip.
 *
 * Drops the reference count on the flipdata.
 */
static void
ms_present_flip_free(struct ms_crtc_pageflip *flip)
{
    struct ms_flipdata *flipdata = flip->flipdata;

    free(flip);
    if (--flipdata->flip_count > 0)
        return;
    free(flipdata);
}

/**
 * Callback for the DRM event queue when a single flip has completed
 *
 * Once the flip has been completed on all pipes, notify the
 * extension code telling it when that happened
 */
static void
ms_flip_handler(uint64_t msc, uint64_t ust, void *data)
{
    struct ms_crtc_pageflip *flip = data;
    ScreenPtr screen = flip->flipdata->screen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    struct ms_flipdata *flipdata = flip->flipdata;

    DebugPresent(("\t\tms:fh %lld c %d msc %llu ust %llu\n",
                  (long long) flipdata->event->event_id,
                  flipdata->flip_count,
                  (long long) msc, (long long) ust));

    if (flip->on_reference_crtc) {
        flipdata->fe_msc = msc;
        flipdata->fe_usec = ust;
    }

    if (flipdata->flip_count == 1) {
        DebugPresent(("\t\tms:fc %lld c %d msc %llu ust %llu\n",
                      (long long) flipdata->event->event_id,
                      flipdata->flip_count,
                      (long long) flipdata->fe_msc, (long long) flipdata->fe_usec));


        ms_present_vblank_handler(flipdata->fe_msc,
                                  flipdata->fe_usec,
                                  flipdata->event);

        drmModeRmFB(ms->fd, flipdata->old_fb_id);
    }
    ms_present_flip_free(flip);
}

/*
 * Callback for the DRM queue abort code.  A flip has been aborted.
 */
static void
ms_present_flip_abort(void *data)
{
    struct ms_crtc_pageflip *flip = data;
    struct ms_flipdata *flipdata = flip->flipdata;

    DebugPresent(("\t\tms:fa %lld c %d\n", (long long) flipdata->event->event_id, flipdata->flip_count));

    if (flipdata->flip_count == 1)
        free(flipdata->event);

    ms_present_flip_free(flip);
}

static Bool
queue_flip_on_crtc(ScreenPtr screen, xf86CrtcPtr crtc,
                   struct ms_flipdata *flipdata,
                   int ref_crtc_vblank_pipe, uint32_t flags)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    struct ms_crtc_pageflip *flip;
    uint32_t seq;
    int err;

    flip = calloc(1, sizeof(struct ms_crtc_pageflip));
    if (flip == NULL) {
        xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                   "flip queue: carrier alloc failed.\n");
        return FALSE;
    }

    /* Only the reference crtc will finally deliver its page flip
     * completion event. All other crtc's events will be discarded.
     */
    flip->on_reference_crtc = (drmmode_crtc->vblank_pipe == ref_crtc_vblank_pipe);
    flip->flipdata = flipdata;

    seq = ms_drm_queue_alloc(crtc, flip, ms_flip_handler, ms_present_flip_abort);
    if (!seq) {
        free(flip);
        return FALSE;
    }

    DebugPresent(("\t\tms:fq %lld c %d -> %d seq %llu\n",
                  (long long) flipdata->event->event_id,
                  flipdata->flip_count, flipdata->flip_count + 1,
                  (long long) seq));

    /* take a reference on flipdata for use in flip */
    flipdata->flip_count++;

    while (drmModePageFlip(ms->fd, drmmode_crtc->mode_crtc->crtc_id,
                           ms->drmmode.fb_id, flags, (void *) (uintptr_t) seq)) {
        err = errno;
        /* We may have failed because the event queue was full.  Flush it
         * and retry.  If there was nothing to flush, then we failed for
         * some other reason and should just return an error.
         */
        if (ms_flush_drm_events(screen) <= 0) {
            xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                       "flip queue failed: %s\n", strerror(err));
            /* Aborting will also decrement flip_count and free(flip). */
            ms_drm_abort_seq(scrn, seq);
            return FALSE;
        }

        /* We flushed some events, so try again. */
        xf86DrvMsg(scrn->scrnIndex, X_WARNING, "flip queue retry\n");
    }

    /* The page flip succeded. */
    return TRUE;
}


static Bool
ms_do_pageflip(ScreenPtr screen,
               PixmapPtr new_front,
               struct ms_present_vblank_event *event,
               int ref_crtc_vblank_pipe,
               Bool async)
{
#ifndef GLAMOR_HAS_GBM
    return FALSE;
#else
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(scrn);
    drmmode_bo new_front_bo;
    uint32_t flags;
    int i;
    struct ms_flipdata *flipdata;
    glamor_block_handler(screen);

    new_front_bo.gbm = glamor_gbm_bo_from_pixmap(screen, new_front);
    new_front_bo.dumb = NULL;
    if (!new_front_bo.gbm) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "Failed to get GBM bo for flip to new front.\n");
        return FALSE;
    }

    flipdata = calloc(1, sizeof(struct ms_flipdata));
    if (!flipdata) {
        drmmode_bo_destroy(&ms->drmmode, &new_front_bo);
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "Failed to allocate flipdata.\n");
        return FALSE;
    }

    flipdata->event = event;
    flipdata->screen = screen;

    /*
     * Take a local reference on flipdata.
     * if the first flip fails, the sequence abort
     * code will free the crtc flip data, and drop
     * it's reference which would cause this to be
     * freed when we still required it.
     */
    flipdata->flip_count++;

    /* Create a new handle for the back buffer */
    flipdata->old_fb_id = ms->drmmode.fb_id;
    if (drmModeAddFB(ms->fd, scrn->virtualX, scrn->virtualY,
                     scrn->depth, scrn->bitsPerPixel,
                     drmmode_bo_get_pitch(&new_front_bo),
                     drmmode_bo_get_handle(&new_front_bo), &ms->drmmode.fb_id)) {
        goto error_out;
    }

    drmmode_bo_destroy(&ms->drmmode, &new_front_bo);

    flags = DRM_MODE_PAGE_FLIP_EVENT;
    if (async)
        flags |= DRM_MODE_PAGE_FLIP_ASYNC;

    /* Queue flips on all enabled CRTCs.
     *
     * Note that if/when we get per-CRTC buffers, we'll have to update this.
     * Right now it assumes a single shared fb across all CRTCs, with the
     * kernel fixing up the offset of each CRTC as necessary.
     *
     * Also, flips queued on disabled or incorrectly configured displays
     * may never complete; this is a configuration error.
     */
    for (i = 0; i < config->num_crtc; i++) {
        xf86CrtcPtr crtc = config->crtc[i];

        if (!ms_crtc_on(crtc))
            continue;

        if (!queue_flip_on_crtc(screen, crtc, flipdata,
                                ref_crtc_vblank_pipe,
                                flags)) {
            goto error_undo;
        }
    }

    /*
     * Do we have more than our local reference,
     * if so and no errors, then drop our local
     * reference and return now.
     */
    if (flipdata->flip_count > 1) {
        flipdata->flip_count--;
        return TRUE;
    }

error_undo:

    /*
     * Have we just got the local reference?
     * free the framebuffer if so since nobody successfully
     * submitted anything
     */
    if (flipdata->flip_count == 1) {
        drmModeRmFB(ms->fd, ms->drmmode.fb_id);
        ms->drmmode.fb_id = flipdata->old_fb_id;
    }

error_out:
    xf86DrvMsg(scrn->scrnIndex, X_WARNING, "Page flip failed: %s\n",
               strerror(errno));
    /* if only the local reference - free the structure,
     * else drop the local reference and return */
    if (flipdata->flip_count == 1)
        free(flipdata);
    else
        flipdata->flip_count--;

    return FALSE;
#endif /* GLAMOR_HAS_GBM */
}

/*
 * Test to see if page flipping is possible on the target crtc
 */
static Bool
ms_present_check_flip(RRCrtcPtr crtc,
                      WindowPtr window,
                      PixmapPtr pixmap,
                      Bool sync_flip)
{
    ScreenPtr screen = window->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(scrn);
    int num_crtcs_on = 0;
    int i;

    if (!ms->drmmode.pageflip)
        return FALSE;

    if (!scrn->vtSema)
        return FALSE;

    for (i = 0; i < config->num_crtc; i++) {
        drmmode_crtc_private_ptr drmmode_crtc = config->crtc[i]->driver_private;

        /* Don't do pageflipping if CRTCs are rotated. */
#ifdef GLAMOR_HAS_GBM
        if (drmmode_crtc->rotate_bo.gbm)
            return FALSE;
#endif

        if (ms_crtc_on(config->crtc[i]))
            num_crtcs_on++;
    }

    /* We can't do pageflipping if all the CRTCs are off. */
    if (num_crtcs_on == 0)
        return FALSE;

    /* Check stride, can't change that on flip */
    if (pixmap->devKind != drmmode_bo_get_pitch(&ms->drmmode.front_bo))
        return FALSE;

    /* Make sure there's a bo we can get to */
    /* XXX: actually do this.  also...is it sufficient?
     * if (!glamor_get_pixmap_private(pixmap))
     *     return FALSE;
     */

    return TRUE;
}

/*
 * Queue a flip on 'crtc' to 'pixmap' at 'target_msc'. If 'sync_flip' is true,
 * then wait for vblank. Otherwise, flip immediately
 */
static Bool
ms_present_flip(RRCrtcPtr crtc,
                uint64_t event_id,
                uint64_t target_msc,
                PixmapPtr pixmap,
                Bool sync_flip)
{
    ScreenPtr screen = crtc->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    xf86CrtcPtr xf86_crtc = crtc->devPrivate;
    drmmode_crtc_private_ptr drmmode_crtc = xf86_crtc->driver_private;
    Bool ret;
    struct ms_present_vblank_event *event;

    if (!ms_present_check_flip(crtc, screen->root, pixmap, sync_flip))
        return FALSE;

    event = calloc(1, sizeof(struct ms_present_vblank_event));
    if (!event)
        return FALSE;

    event->event_id = event_id;
    ret = ms_do_pageflip(screen, pixmap, event, drmmode_crtc->vblank_pipe, !sync_flip);
    if (!ret)
        xf86DrvMsg(scrn->scrnIndex, X_ERROR, "present flip failed\n");

    return ret;
}

/*
 * Queue a flip back to the normal frame buffer
 */
static void
ms_present_unflip(ScreenPtr screen, uint64_t event_id)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    PixmapPtr pixmap = screen->GetScreenPixmap(screen);
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(scrn);
    int i;
    struct ms_present_vblank_event *event;

    event = calloc(1, sizeof(struct ms_present_vblank_event));
    if (!event)
        return;

    event->event_id = event_id;

    if (ms_present_check_flip(NULL, screen->root, pixmap, TRUE) &&
        ms_do_pageflip(screen, pixmap, event, -1, FALSE)) {
        return;
    }

    for (i = 0; i < config->num_crtc; i++) {
        xf86CrtcPtr crtc = config->crtc[i];
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

	if (!crtc->enabled)
	    continue;

	if (drmmode_crtc->dpms_mode == DPMSModeOn)
	    crtc->funcs->set_mode_major(crtc, &crtc->mode, crtc->rotation,
					crtc->x, crtc->y);
	else
	    drmmode_crtc->need_modeset = TRUE;
    }

    present_event_notify(event_id, 0, 0);
}
#endif

static present_screen_info_rec ms_present_screen_info = {
    .version = PRESENT_SCREEN_INFO_VERSION,

    .get_crtc = ms_present_get_crtc,
    .get_ust_msc = ms_present_get_ust_msc,
    .queue_vblank = ms_present_queue_vblank,
    .abort_vblank = ms_present_abort_vblank,
    .flush = ms_present_flush,

    .capabilities = PresentCapabilityNone,
#ifdef GLAMOR
    .check_flip = ms_present_check_flip,
    .flip = ms_present_flip,
    .unflip = ms_present_unflip,
#endif
};

Bool
ms_present_screen_init(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    uint64_t value;
    int ret;

    ret = drmGetCap(ms->fd, DRM_CAP_ASYNC_PAGE_FLIP, &value);
    if (ret == 0 && value == 1)
        ms_present_screen_info.capabilities |= PresentCapabilityAsync;

    return present_screen_init(screen, &ms_present_screen_info);
}
