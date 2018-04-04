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

#include <xserver_poll.h>
#include <xf86drm.h>

#include "driver.h"

/*
 * Flush the DRM event queue when full; makes space for new events.
 *
 * Returns a negative value on error, 0 if there was nothing to process,
 * or 1 if we handled any events.
 */
int
ms_flush_drm_events(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);

    struct pollfd p = { .fd = ms->fd, .events = POLLIN };
    int r;

    do {
            r = xserver_poll(&p, 1, 0);
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

#ifdef GLAMOR_HAS_GBM

/*
 * Event data for an in progress flip.
 * This contains a pointer to the vblank event,
 * and information about the flip in progress.
 * a reference to this is stored in the per-crtc
 * flips.
 */
struct ms_flipdata {
    ScreenPtr screen;
    void *event;
    ms_pageflip_handler_proc event_handler;
    ms_pageflip_abort_proc abort_handler;
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
ms_pageflip_free(struct ms_crtc_pageflip *flip)
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
ms_pageflip_handler(uint64_t msc, uint64_t ust, void *data)
{
    struct ms_crtc_pageflip *flip = data;
    struct ms_flipdata *flipdata = flip->flipdata;
    ScreenPtr screen = flipdata->screen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);

    if (flip->on_reference_crtc) {
        flipdata->fe_msc = msc;
        flipdata->fe_usec = ust;
    }

    if (flipdata->flip_count == 1) {
        flipdata->event_handler(ms, flipdata->fe_msc,
                                flipdata->fe_usec,
                                flipdata->event);

        drmModeRmFB(ms->fd, flipdata->old_fb_id);
    }
    ms_pageflip_free(flip);
}

/*
 * Callback for the DRM queue abort code.  A flip has been aborted.
 */
static void
ms_pageflip_abort(void *data)
{
    struct ms_crtc_pageflip *flip = data;
    struct ms_flipdata *flipdata = flip->flipdata;
    ScreenPtr screen = flipdata->screen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);

    if (flipdata->flip_count == 1)
        flipdata->abort_handler(ms, flipdata->event);

    ms_pageflip_free(flip);
}

static Bool
do_queue_flip_on_crtc(modesettingPtr ms, xf86CrtcPtr crtc,
                      uint32_t flags, uint32_t seq)
{
    return drmmode_crtc_flip(crtc, ms->drmmode.fb_id, flags,
                             (void *) (uintptr_t) seq);
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

    seq = ms_drm_queue_alloc(crtc, flip, ms_pageflip_handler, ms_pageflip_abort);
    if (!seq) {
        free(flip);
        return FALSE;
    }

    /* take a reference on flipdata for use in flip */
    flipdata->flip_count++;

    while (do_queue_flip_on_crtc(ms, crtc, flags, seq)) {
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


Bool
ms_do_pageflip(ScreenPtr screen,
               PixmapPtr new_front,
               void *event,
               int ref_crtc_vblank_pipe,
               Bool async,
               ms_pageflip_handler_proc pageflip_handler,
               ms_pageflip_abort_proc pageflip_abort)
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
    flipdata->event_handler = pageflip_handler;
    flipdata->abort_handler = pageflip_abort;

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

    new_front_bo.width = new_front->drawable.width;
    new_front_bo.height = new_front->drawable.height;
    if (drmmode_bo_import(&ms->drmmode, &new_front_bo,
                          &ms->drmmode.fb_id))
        goto error_out;

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

    drmmode_bo_destroy(&ms->drmmode, &new_front_bo);

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
    drmmode_bo_destroy(&ms->drmmode, &new_front_bo);
    /* if only the local reference - free the structure,
     * else drop the local reference and return */
    if (flipdata->flip_count == 1)
        free(flipdata);
    else
        flipdata->flip_count--;

    return FALSE;
#endif /* GLAMOR_HAS_GBM */
}

#endif
