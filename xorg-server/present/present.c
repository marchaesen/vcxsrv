/*
 * Copyright © 2013 Keith Packard
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

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "present_priv.h"
#include <gcstruct.h>
#include <misync.h>
#include <misyncstr.h>
#ifdef MONOTONIC_CLOCK
#include <time.h>
#endif

static uint64_t         present_event_id;
static struct xorg_list present_exec_queue;
static struct xorg_list present_flip_queue;

#if 0
#define DebugPresent(x) ErrorF x
#else
#define DebugPresent(x)
#endif

static void
present_execute(present_vblank_ptr vblank, uint64_t ust, uint64_t crtc_msc);

/*
 * Copies the update region from a pixmap to the target drawable
 */
static void
present_copy_region(DrawablePtr drawable,
                    PixmapPtr pixmap,
                    RegionPtr update,
                    int16_t x_off,
                    int16_t y_off)
{
    ScreenPtr   screen = drawable->pScreen;
    GCPtr       gc;

    gc = GetScratchGC(drawable->depth, screen);
    if (update) {
        ChangeGCVal     changes[2];

        changes[0].val = x_off;
        changes[1].val = y_off;
        ChangeGC(serverClient, gc,
                 GCClipXOrigin|GCClipYOrigin,
                 changes);
        (*gc->funcs->ChangeClip)(gc, CT_REGION, update, 0);
    }
    ValidateGC(drawable, gc);
    (*gc->ops->CopyArea)(&pixmap->drawable,
                         drawable,
                         gc,
                         0, 0,
                         pixmap->drawable.width, pixmap->drawable.height,
                         x_off, y_off);
    if (update)
        (*gc->funcs->ChangeClip)(gc, CT_NONE, NULL, 0);
    FreeScratchGC(gc);
}

static inline PixmapPtr
present_flip_pending_pixmap(ScreenPtr screen)
{
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);

    if (!screen_priv)
        return NULL;

    if (!screen_priv->flip_pending)
        return NULL;

    return screen_priv->flip_pending->pixmap;
}

static Bool
present_check_flip(RRCrtcPtr    crtc,
                   WindowPtr    window,
                   PixmapPtr    pixmap,
                   Bool         sync_flip,
                   RegionPtr    valid,
                   int16_t      x_off,
                   int16_t      y_off)
{
    ScreenPtr                   screen = window->drawable.pScreen;
    PixmapPtr                   window_pixmap;
    WindowPtr                   root = screen->root;
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);

    if (!screen_priv)
        return FALSE;

    if (!screen_priv->info)
        return FALSE;

    if (!crtc)
        return FALSE;

    /* Check to see if the driver supports flips at all */
    if (!screen_priv->info->flip)
        return FALSE;

    /* Make sure the window hasn't been redirected with Composite */
    window_pixmap = screen->GetWindowPixmap(window);
    if (window_pixmap != screen->GetScreenPixmap(screen) &&
        window_pixmap != screen_priv->flip_pixmap &&
        window_pixmap != present_flip_pending_pixmap(screen))
        return FALSE;

    /* Check for full-screen window */
    if (!RegionEqual(&window->clipList, &root->winSize)) {
        return FALSE;
    }

    /* Source pixmap must align with window exactly */
    if (x_off || y_off) {
        return FALSE;
    }

    /* Make sure the area marked as valid fills the screen */
    if (valid && !RegionEqual(valid, &root->winSize)) {
        return FALSE;
    }

    /* Does the window match the pixmap exactly? */
    if (window->drawable.x != 0 || window->drawable.y != 0 ||
#ifdef COMPOSITE
        window->drawable.x != pixmap->screen_x || window->drawable.y != pixmap->screen_y ||
#endif
        window->drawable.width != pixmap->drawable.width ||
        window->drawable.height != pixmap->drawable.height) {
        return FALSE;
    }

    /* Ask the driver for permission */
    if (screen_priv->info->check_flip) {
        if (!(*screen_priv->info->check_flip) (crtc, window, pixmap, sync_flip)) {
            DebugPresent(("\td %08lx -> %08lx\n", window->drawable.id, pixmap ? pixmap->drawable.id : 0));
            return FALSE;
        }
    }

    return TRUE;
}

static Bool
present_flip(RRCrtcPtr crtc,
             uint64_t event_id,
             uint64_t target_msc,
             PixmapPtr pixmap,
             Bool sync_flip)
{
    ScreenPtr                   screen = crtc->pScreen;
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);

    return (*screen_priv->info->flip) (crtc, event_id, target_msc, pixmap, sync_flip);
}

static void
present_vblank_notify(present_vblank_ptr vblank, CARD8 kind, CARD8 mode, uint64_t ust, uint64_t crtc_msc)
{
    int         n;

    if (vblank->window)
        present_send_complete_notify(vblank->window, kind, mode, vblank->serial, ust, crtc_msc - vblank->msc_offset);
    for (n = 0; n < vblank->num_notifies; n++) {
        WindowPtr   window = vblank->notifies[n].window;
        CARD32      serial = vblank->notifies[n].serial;

        if (window)
            present_send_complete_notify(window, kind, mode, serial, ust, crtc_msc - vblank->msc_offset);
    }
}

static void
present_pixmap_idle(PixmapPtr pixmap, WindowPtr window, CARD32 serial, struct present_fence *present_fence)
{
    if (present_fence)
        present_fence_set_triggered(present_fence);
    if (window) {
        DebugPresent(("\ti %08lx\n", pixmap ? pixmap->drawable.id : 0));
        present_send_idle_notify(window, serial, pixmap, present_fence);
    }
}

RRCrtcPtr
present_get_crtc(WindowPtr window)
{
    ScreenPtr                   screen = window->drawable.pScreen;
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);

    if (!screen_priv)
        return NULL;

    if (!screen_priv->info)
        return NULL;

    return (*screen_priv->info->get_crtc)(window);
}

uint32_t
present_query_capabilities(RRCrtcPtr crtc)
{
    present_screen_priv_ptr     screen_priv;

    if (!crtc)
        return 0;

    screen_priv = present_screen_priv(crtc->pScreen);

    if (!screen_priv)
        return 0;

    if (!screen_priv->info)
        return 0;

    return screen_priv->info->capabilities;
}

static int
present_get_ust_msc(ScreenPtr screen, RRCrtcPtr crtc, uint64_t *ust, uint64_t *msc)
{
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);

    if (crtc == NULL)
        return present_fake_get_ust_msc(screen, ust, msc);
    else
        return (*screen_priv->info->get_ust_msc)(crtc, ust, msc);
}

static void
present_flush(WindowPtr window)
{
    ScreenPtr                   screen = window->drawable.pScreen;
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);

    if (!screen_priv)
        return;

    if (!screen_priv->info)
        return;

    (*screen_priv->info->flush) (window);
}

static int
present_queue_vblank(ScreenPtr screen,
                     RRCrtcPtr crtc,
                     uint64_t event_id,
                     uint64_t msc)
{
    Bool                        ret;

    if (crtc == NULL)
        ret = present_fake_queue_vblank(screen, event_id, msc);
    else
    {
        present_screen_priv_ptr     screen_priv = present_screen_priv(screen);
        ret = (*screen_priv->info->queue_vblank) (crtc, event_id, msc);
    }
    return ret;
}

static uint64_t
present_window_to_crtc_msc(WindowPtr window, RRCrtcPtr crtc, uint64_t window_msc, uint64_t new_msc)
{
    present_window_priv_ptr     window_priv = present_get_window_priv(window, TRUE);

    if (crtc != window_priv->crtc) {
        uint64_t        old_ust, old_msc;

        if (window_priv->crtc == PresentCrtcNeverSet) {
            window_priv->msc_offset = 0;
        } else {
            /* The old CRTC may have been turned off, in which case
             * we'll just use whatever previous MSC we'd seen from this CRTC
             */

            if (present_get_ust_msc(window->drawable.pScreen, window_priv->crtc, &old_ust, &old_msc) != Success)
                old_msc = window_priv->msc;

            window_priv->msc_offset += new_msc - old_msc;
        }
        window_priv->crtc = crtc;
    }

    return window_msc + window_priv->msc_offset;
}

/*
 * When the wait fence or previous flip is completed, it's time
 * to re-try the request
 */
static void
present_re_execute(present_vblank_ptr vblank)
{
    uint64_t            ust = 0, crtc_msc = 0;

    if (vblank->crtc)
        (void) present_get_ust_msc(vblank->screen, vblank->crtc, &ust, &crtc_msc);

    present_execute(vblank, ust, crtc_msc);
}

static void
present_flip_try_ready(ScreenPtr screen)
{
    present_vblank_ptr  vblank, tmp;

    xorg_list_for_each_entry_safe(vblank, tmp, &present_exec_queue, event_queue) {
        if (vblank->flip_ready) {
            present_re_execute(vblank);
            return;
        }
    }
}

static void
present_flip_idle(ScreenPtr screen)
{
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);

    if (screen_priv->flip_pixmap) {
        present_pixmap_idle(screen_priv->flip_pixmap, screen_priv->flip_window,
                            screen_priv->flip_serial, screen_priv->flip_idle_fence);
        if (screen_priv->flip_idle_fence)
            present_fence_destroy(screen_priv->flip_idle_fence);
        dixDestroyPixmap(screen_priv->flip_pixmap, screen_priv->flip_pixmap->drawable.id);
        screen_priv->flip_crtc = NULL;
        screen_priv->flip_window = NULL;
        screen_priv->flip_serial = 0;
        screen_priv->flip_pixmap = NULL;
        screen_priv->flip_idle_fence = NULL;
    }
}

struct pixmap_visit {
    PixmapPtr   old;
    PixmapPtr   new;
};

static int
present_set_tree_pixmap_visit(WindowPtr window, void *data)
{
    struct pixmap_visit *visit = data;
    ScreenPtr           screen = window->drawable.pScreen;

    if ((*screen->GetWindowPixmap)(window) != visit->old)
        return WT_DONTWALKCHILDREN;
    (*screen->SetWindowPixmap)(window, visit->new);
    return WT_WALKCHILDREN;
}

static void
present_set_tree_pixmap(WindowPtr window, PixmapPtr pixmap)
{
    struct pixmap_visit visit;
    ScreenPtr           screen = window->drawable.pScreen;

    visit.old = (*screen->GetWindowPixmap)(window);
    visit.new = pixmap;
    if (visit.old == visit.new)
        return;
    TraverseTree(window, present_set_tree_pixmap_visit, &visit);
}

static void
present_set_abort_flip(ScreenPtr screen)
{
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);

    /* Switch back to using the screen pixmap now to avoid
     * 2D applications drawing to the wrong pixmap.
     */

    if (screen_priv->flip_window)
        present_set_tree_pixmap(screen_priv->flip_window,
                                  (*screen->GetScreenPixmap)(screen));

    if (screen->root)
        present_set_tree_pixmap(screen->root, (*screen->GetScreenPixmap)(screen));

    screen_priv->flip_pending->abort_flip = TRUE;
}

static void
present_unflip(ScreenPtr screen)
{
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);
    PixmapPtr pixmap = (*screen->GetScreenPixmap)(screen);

    assert (!screen_priv->unflip_event_id);
    assert (!screen_priv->flip_pending);

    if (screen_priv->flip_window)
        present_set_tree_pixmap(screen_priv->flip_window, pixmap);

    present_set_tree_pixmap(screen->root, pixmap);

    /* Update the screen pixmap with the current flip pixmap contents
     */
    if (screen_priv->flip_pixmap && screen_priv->flip_window) {
        present_copy_region(&pixmap->drawable,
                            screen_priv->flip_pixmap,
                            NULL, 0, 0);
    }
    screen_priv->unflip_event_id = ++present_event_id;
    DebugPresent(("u %lld\n", screen_priv->unflip_event_id));
    (*screen_priv->info->unflip) (screen, screen_priv->unflip_event_id);
}

static void
present_flip_notify(present_vblank_ptr vblank, uint64_t ust, uint64_t crtc_msc)
{
    ScreenPtr                   screen = vblank->screen;
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);

    DebugPresent(("\tn %lld %p %8lld: %08lx -> %08lx\n",
                  vblank->event_id, vblank, vblank->target_msc,
                  vblank->pixmap ? vblank->pixmap->drawable.id : 0,
                  vblank->window ? vblank->window->drawable.id : 0));

    assert (vblank == screen_priv->flip_pending);

    present_flip_idle(screen);

    xorg_list_del(&vblank->event_queue);

    /* Transfer reference for pixmap and fence from vblank to screen_priv */
    screen_priv->flip_crtc = vblank->crtc;
    screen_priv->flip_window = vblank->window;
    screen_priv->flip_serial = vblank->serial;
    screen_priv->flip_pixmap = vblank->pixmap;
    screen_priv->flip_idle_fence = vblank->idle_fence;

    vblank->pixmap = NULL;
    vblank->idle_fence = NULL;

    screen_priv->flip_pending = NULL;

    if (vblank->abort_flip)
        present_unflip(screen);

    present_vblank_notify(vblank, PresentCompleteKindPixmap, PresentCompleteModeFlip, ust, crtc_msc);
    present_vblank_destroy(vblank);

    present_flip_try_ready(screen);
}

void
present_event_notify(uint64_t event_id, uint64_t ust, uint64_t msc)
{
    present_vblank_ptr  vblank, tmp;
    int                 s;

    if (!event_id)
        return;
    DebugPresent(("\te %lld ust %lld msc %lld\n", event_id, ust, msc));
    xorg_list_for_each_entry_safe(vblank, tmp, &present_exec_queue, event_queue) {
        if (vblank->event_id == event_id) {
            present_execute(vblank, ust, msc);
            return;
        }
    }
    xorg_list_for_each_entry_safe(vblank, tmp, &present_flip_queue, event_queue) {
        if (vblank->event_id == event_id) {
            present_flip_notify(vblank, ust, msc);
            return;
        }
    }

    for (s = 0; s < screenInfo.numScreens; s++) {
        ScreenPtr               screen = screenInfo.screens[s];
        present_screen_priv_ptr screen_priv = present_screen_priv(screen);

        if (event_id == screen_priv->unflip_event_id) {
            DebugPresent(("\tun %lld\n", event_id));
            screen_priv->unflip_event_id = 0;
            present_flip_idle(screen);
            present_flip_try_ready(screen);
            return;
        }
    }
}

/*
 * 'window' is being reconfigured. Check to see if it is involved
 * in flipping and clean up as necessary
 */
void
present_check_flip_window (WindowPtr window)
{
    ScreenPtr                   screen = window->drawable.pScreen;
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);
    present_window_priv_ptr     window_priv = present_window_priv(window);
    present_vblank_ptr          flip_pending = screen_priv->flip_pending;
    present_vblank_ptr          vblank;

    /* If this window hasn't ever been used with Present, it can't be
     * flipping
     */
    if (!window_priv)
        return;

    if (screen_priv->unflip_event_id)
        return;

    if (flip_pending) {
        /*
         * Check pending flip
         */
        if (flip_pending->window == window) {
            if (!present_check_flip(flip_pending->crtc, window, flip_pending->pixmap,
                                    flip_pending->sync_flip, NULL, 0, 0))
                present_set_abort_flip(screen);
        }
    } else {
        /*
         * Check current flip
         */
        if (window == screen_priv->flip_window) {
            if (!present_check_flip(screen_priv->flip_crtc, window, screen_priv->flip_pixmap, FALSE, NULL, 0, 0))
                present_unflip(screen);
        }
    }

    /* Now check any queued vblanks */
    xorg_list_for_each_entry(vblank, &window_priv->vblank, window_list) {
        if (vblank->queued && vblank->flip && !present_check_flip(vblank->crtc, window, vblank->pixmap, FALSE, NULL, 0, 0))
            vblank->flip = FALSE;
    }
}

/*
 * Called when the wait fence is triggered; just gets the current msc/ust and
 * calls present_execute again. That will re-check the fence and pend the
 * request again if it's still not actually ready
 */
static void
present_wait_fence_triggered(void *param)
{
    present_vblank_ptr  vblank = param;
    present_re_execute(vblank);
}

/*
 * Once the required MSC has been reached, execute the pending request.
 *
 * For requests to actually present something, either blt contents to
 * the screen or queue a frame buffer swap.
 *
 * For requests to just get the current MSC/UST combo, skip that part and
 * go straight to event delivery
 */

static void
present_execute(present_vblank_ptr vblank, uint64_t ust, uint64_t crtc_msc)
{
    WindowPtr                   window = vblank->window;
    ScreenPtr                   screen = window->drawable.pScreen;
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);
    uint8_t                     mode;

    if (vblank->wait_fence) {
        if (!present_fence_check_triggered(vblank->wait_fence)) {
            present_fence_set_callback(vblank->wait_fence, present_wait_fence_triggered, vblank);
            return;
        }
    }

    if (vblank->flip && vblank->pixmap && vblank->window) {
        if (screen_priv->flip_pending || screen_priv->unflip_event_id) {
            DebugPresent(("\tr %lld %p (pending %p unflip %lld)\n",
                          vblank->event_id, vblank,
                          screen_priv->flip_pending, screen_priv->unflip_event_id));
            vblank->flip_ready = TRUE;
            return;
        }
    }

    xorg_list_del(&vblank->event_queue);
    xorg_list_del(&vblank->window_list);
    vblank->queued = FALSE;

    if (vblank->pixmap && vblank->window) {

        if (vblank->flip) {

            DebugPresent(("\tf %lld %p %8lld: %08lx -> %08lx\n",
                          vblank->event_id, vblank, crtc_msc,
                          vblank->pixmap->drawable.id, vblank->window->drawable.id));

            /* Prepare to flip by placing it in the flip queue and
             * and sticking it into the flip_pending field
             */
            screen_priv->flip_pending = vblank;

            xorg_list_add(&vblank->event_queue, &present_flip_queue);
            /* Try to flip
             */
            if (present_flip(vblank->crtc, vblank->event_id, vblank->target_msc, vblank->pixmap, vblank->sync_flip)) {
                RegionPtr damage;

                /* Fix window pixmaps:
                 *  1) Restore previous flip window pixmap
                 *  2) Set current flip window pixmap to the new pixmap
                 */
                if (screen_priv->flip_window && screen_priv->flip_window != window)
                    present_set_tree_pixmap(screen_priv->flip_window,
                                              (*screen->GetScreenPixmap)(screen));
                present_set_tree_pixmap(vblank->window, vblank->pixmap);
                present_set_tree_pixmap(screen->root, vblank->pixmap);

                /* Report update region as damaged
                 */
                if (vblank->update) {
                    damage = vblank->update;
                    RegionIntersect(damage, damage, &window->clipList);
                } else
                    damage = &window->clipList;

                DamageDamageRegion(&vblank->window->drawable, damage);
                return;
            }

            xorg_list_del(&vblank->event_queue);
            /* Oops, flip failed. Clear the flip_pending field
              */
            screen_priv->flip_pending = NULL;
            vblank->flip = FALSE;
        }
        DebugPresent(("\tc %p %8lld: %08lx -> %08lx\n", vblank, crtc_msc, vblank->pixmap->drawable.id, vblank->window->drawable.id));
        if (screen_priv->flip_pending) {

            /* Check pending flip
             */
            if (window == screen_priv->flip_pending->window)
                present_set_abort_flip(screen);
        } else if (!screen_priv->unflip_event_id) {

            /* Check current flip
             */
            if (window == screen_priv->flip_window)
                present_unflip(screen);
        }
        present_copy_region(&window->drawable, vblank->pixmap, vblank->update, vblank->x_off, vblank->y_off);

        /* present_copy_region sticks the region into a scratch GC,
         * which is then freed, freeing the region
         */
        vblank->update = NULL;
        present_flush(window);

        present_pixmap_idle(vblank->pixmap, vblank->window, vblank->serial, vblank->idle_fence);
    }

    /* Compute correct CompleteMode
     */
    if (vblank->kind == PresentCompleteKindPixmap) {
        if (vblank->pixmap && vblank->window)
            mode = PresentCompleteModeCopy;
        else
            mode = PresentCompleteModeSkip;
    }
    else
        mode = PresentCompleteModeCopy;


    present_vblank_notify(vblank, vblank->kind, mode, ust, crtc_msc);
    present_vblank_destroy(vblank);
}

int
present_pixmap(WindowPtr window,
               PixmapPtr pixmap,
               CARD32 serial,
               RegionPtr valid,
               RegionPtr update,
               int16_t x_off,
               int16_t y_off,
               RRCrtcPtr target_crtc,
               SyncFence *wait_fence,
               SyncFence *idle_fence,
               uint32_t options,
               uint64_t window_msc,
               uint64_t divisor,
               uint64_t remainder,
               present_notify_ptr notifies,
               int num_notifies)
{
    uint64_t                    ust = 0;
    uint64_t                    target_msc;
    uint64_t                    crtc_msc = 0;
    int                         ret;
    present_vblank_ptr          vblank, tmp;
    ScreenPtr                   screen = window->drawable.pScreen;
    present_window_priv_ptr     window_priv = present_get_window_priv(window, TRUE);
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);

    if (!window_priv)
        return BadAlloc;

    if (!screen_priv || !screen_priv->info)
        target_crtc = NULL;
    else if (!target_crtc) {
        /* Update the CRTC if we have a pixmap or we don't have a CRTC
         */
        if (!pixmap)
            target_crtc = window_priv->crtc;

        if (!target_crtc || target_crtc == PresentCrtcNeverSet)
            target_crtc = present_get_crtc(window);
    }

    ret = present_get_ust_msc(screen, target_crtc, &ust, &crtc_msc);

    target_msc = present_window_to_crtc_msc(window, target_crtc, window_msc, crtc_msc);

    if (ret == Success) {
        /* Stash the current MSC away in case we need it later
         */
        window_priv->msc = crtc_msc;
    }

    /* Adjust target_msc to match modulus
     */
    if (crtc_msc >= target_msc) {
        if (divisor != 0) {
            target_msc = crtc_msc - (crtc_msc % divisor) + remainder;
            if (options & PresentOptionAsync) {
                if (target_msc < crtc_msc)
                    target_msc += divisor;
            } else {
                if (target_msc <= crtc_msc)
                    target_msc += divisor;
            }
        } else {
            target_msc = crtc_msc;
            if (!(options & PresentOptionAsync))
                target_msc++;
        }
    }

    /*
     * Look for a matching presentation already on the list and
     * don't bother doing the previous one if this one will overwrite it
     * in the same frame
     */

    if (!update && pixmap) {
        xorg_list_for_each_entry_safe(vblank, tmp, &window_priv->vblank, window_list) {

            if (!vblank->pixmap)
                continue;

            if (!vblank->queued)
                continue;

            if (vblank->crtc != target_crtc || vblank->target_msc != target_msc)
                continue;

            DebugPresent(("\tx %lld %p %8lld: %08lx -> %08lx (crtc %p)\n",
                          vblank->event_id, vblank, vblank->target_msc,
                          vblank->pixmap->drawable.id, vblank->window->drawable.id,
                          vblank->crtc));

            present_pixmap_idle(vblank->pixmap, vblank->window, vblank->serial, vblank->idle_fence);
            present_fence_destroy(vblank->idle_fence);
            dixDestroyPixmap(vblank->pixmap, vblank->pixmap->drawable.id);

            vblank->pixmap = NULL;
            vblank->idle_fence = NULL;
            vblank->flip = FALSE;
            if (vblank->flip_ready)
                present_re_execute(vblank);
        }
    }

    vblank = calloc (1, sizeof (present_vblank_rec));
    if (!vblank)
        return BadAlloc;

    xorg_list_append(&vblank->window_list, &window_priv->vblank);
    xorg_list_init(&vblank->event_queue);

    vblank->screen = screen;
    vblank->window = window;
    vblank->pixmap = pixmap;
    vblank->event_id = ++present_event_id;
    if (pixmap) {
        vblank->kind = PresentCompleteKindPixmap;
        pixmap->refcnt++;
    } else
        vblank->kind = PresentCompleteKindNotifyMSC;

    vblank->serial = serial;

    if (valid) {
        vblank->valid = RegionDuplicate(valid);
        if (!vblank->valid)
            goto no_mem;
    }
    if (update) {
        vblank->update = RegionDuplicate(update);
        if (!vblank->update)
            goto no_mem;
    }

    vblank->x_off = x_off;
    vblank->y_off = y_off;
    vblank->target_msc = target_msc;
    vblank->crtc = target_crtc;
    vblank->msc_offset = window_priv->msc_offset;
    vblank->notifies = notifies;
    vblank->num_notifies = num_notifies;

    if (!(options & PresentOptionAsync))
        vblank->sync_flip = TRUE;

    if (!(options & PresentOptionCopy) &&
        !((options & PresentOptionAsync) &&
          (!screen_priv->info ||
           !(screen_priv->info->capabilities & PresentCapabilityAsync))) &&
        pixmap != NULL &&
        present_check_flip (target_crtc, window, pixmap, vblank->sync_flip, valid, x_off, y_off))
    {
        vblank->flip = TRUE;
        if (vblank->sync_flip)
            target_msc--;
    }

    if (wait_fence) {
        vblank->wait_fence = present_fence_create(wait_fence);
        if (!vblank->wait_fence)
            goto no_mem;
    }

    if (idle_fence) {
        vblank->idle_fence = present_fence_create(idle_fence);
        if (!vblank->idle_fence)
            goto no_mem;
    }

    if (pixmap)
        DebugPresent(("q %lld %p %8lld: %08lx -> %08lx (crtc %p) flip %d vsync %d serial %d\n",
                      vblank->event_id, vblank, target_msc,
                      vblank->pixmap->drawable.id, vblank->window->drawable.id,
                      target_crtc, vblank->flip, vblank->sync_flip, vblank->serial));

    xorg_list_add(&vblank->event_queue, &present_exec_queue);
    vblank->queued = TRUE;
    if ((pixmap && target_msc >= crtc_msc) || (!pixmap && target_msc > crtc_msc)) {
        ret = present_queue_vblank(screen, target_crtc, vblank->event_id, target_msc);
        if (ret == Success)
            return Success;

        DebugPresent(("present_queue_vblank failed\n"));
    }

    present_execute(vblank, ust, crtc_msc);

    return Success;

no_mem:
    ret = BadAlloc;
    vblank->notifies = NULL;
    present_vblank_destroy(vblank);
    return ret;
}

void
present_abort_vblank(ScreenPtr screen, RRCrtcPtr crtc, uint64_t event_id, uint64_t msc)
{
    present_vblank_ptr  vblank, tmp;

    if (crtc == NULL)
        present_fake_abort_vblank(screen, event_id, msc);
    else
    {
        present_screen_priv_ptr     screen_priv = present_screen_priv(screen);

        (*screen_priv->info->abort_vblank) (crtc, event_id, msc);
    }

    xorg_list_for_each_entry_safe(vblank, tmp, &present_exec_queue, event_queue) {
        if (vblank->event_id == event_id) {
            xorg_list_del(&vblank->event_queue);
            vblank->queued = FALSE;
            return;
        }
    }
    xorg_list_for_each_entry_safe(vblank, tmp, &present_flip_queue, event_queue) {
        if (vblank->event_id == event_id) {
            xorg_list_del(&vblank->event_queue);
            return;
        }
    }
}

int
present_notify_msc(WindowPtr window,
                   CARD32 serial,
                   uint64_t target_msc,
                   uint64_t divisor,
                   uint64_t remainder)
{
    return present_pixmap(window,
                          NULL,
                          serial,
                          NULL, NULL,
                          0, 0,
                          NULL,
                          NULL, NULL,
                          divisor == 0 ? PresentOptionAsync : 0,
                          target_msc, divisor, remainder, NULL, 0);
}

void
present_flip_destroy(ScreenPtr screen)
{
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);

    /* Reset window pixmaps back to the screen pixmap */
    if (screen_priv->flip_pending)
        present_set_abort_flip(screen);

    /* Drop reference to any pending flip or unflip pixmaps. */
    present_flip_idle(screen);
}

void
present_vblank_destroy(present_vblank_ptr vblank)
{
    /* Remove vblank from window and screen lists */
    xorg_list_del(&vblank->window_list);

    DebugPresent(("\td %lld %p %8lld: %08lx -> %08lx\n",
                  vblank->event_id, vblank, vblank->target_msc,
                  vblank->pixmap ? vblank->pixmap->drawable.id : 0,
                  vblank->window ? vblank->window->drawable.id : 0));

    /* Drop pixmap reference */
    if (vblank->pixmap)
        dixDestroyPixmap(vblank->pixmap, vblank->pixmap->drawable.id);

    /* Free regions */
    if (vblank->valid)
        RegionDestroy(vblank->valid);
    if (vblank->update)
        RegionDestroy(vblank->update);

    if (vblank->wait_fence)
        present_fence_destroy(vblank->wait_fence);

    if (vblank->idle_fence)
        present_fence_destroy(vblank->idle_fence);

    if (vblank->notifies)
        present_destroy_notifies(vblank->notifies, vblank->num_notifies);

    free(vblank);
}

Bool
present_init(void)
{
    xorg_list_init(&present_exec_queue);
    xorg_list_init(&present_flip_queue);
    present_fake_queue_init();
    return TRUE;
}
