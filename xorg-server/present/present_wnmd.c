/*
 * Copyright © 2018 Roman Gilg
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

/*
 * Window flip mode
 *
 * Provides per-window flips. Flips can be processed on windows that
 * have the same size as their parents, which they share their pixmap with.
 *
 * A flip still requires a copy currently, since the original pixmap needs
 * to be updated with the new pixmap content. Just a flip of all windows
 * to the new pixmap is diffcult, because the original pixmap might not be
 * controlled by the Xserver.
 *
 */

static void
present_wnmd_execute(present_vblank_ptr vblank, uint64_t ust, uint64_t crtc_msc);

static void
present_wnmd_create_event_id(present_window_priv_ptr window_priv, present_vblank_ptr vblank)
{
    vblank->event_id = ++window_priv->event_id;
}

static uint32_t
present_wnmd_query_capabilities(present_screen_priv_ptr screen_priv)
{
    return screen_priv->wnmd_info->capabilities;
}

static RRCrtcPtr
present_wnmd_get_crtc(present_screen_priv_ptr screen_priv, WindowPtr window)
{
    return (*screen_priv->wnmd_info->get_crtc)(window);
}

static int
present_wnmd_get_ust_msc(ScreenPtr screen, WindowPtr window, uint64_t *ust, uint64_t *msc)
{
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);
    return (*screen_priv->wnmd_info->get_ust_msc)(window, ust, msc);
}

/*
 * When the wait fence or previous flip is completed, it's time
 * to re-try the request
 */
static void
present_wnmd_re_execute(present_vblank_ptr vblank)
{
    uint64_t ust = 0, crtc_msc = 0;

    (void) present_wnmd_get_ust_msc(vblank->screen, vblank->window, &ust, &crtc_msc);
    present_wnmd_execute(vblank, ust, crtc_msc);
}

static void
present_wnmd_flip_try_ready(WindowPtr window)
{
    present_window_priv_ptr window_priv = present_window_priv(window);
    present_vblank_ptr      vblank;

    xorg_list_for_each_entry(vblank, &window_priv->flip_queue, event_queue) {
        if (vblank->queued) {
            present_wnmd_re_execute(vblank);
            return;
        }
    }
}

static void
present_wnmd_free_idle_vblank(present_vblank_ptr vblank)
{
    present_pixmap_idle(vblank->pixmap, vblank->window, vblank->serial, vblank->idle_fence);
    present_vblank_destroy(vblank);
}

/*
 * Free any left over idle vblanks
 */
static void
present_wnmd_free_idle_vblanks(WindowPtr window)
{
    present_window_priv_ptr         window_priv = present_window_priv(window);
    present_vblank_ptr              vblank, tmp;

    xorg_list_for_each_entry_safe(vblank, tmp, &window_priv->idle_queue, event_queue) {
        present_wnmd_free_idle_vblank(vblank);
    }

    if (window_priv->flip_active) {
        present_wnmd_free_idle_vblank(window_priv->flip_active);
        window_priv->flip_active = NULL;
    }
}

static WindowPtr
present_wnmd_toplvl_pixmap_window(WindowPtr window)
{
    ScreenPtr       screen = window->drawable.pScreen;
    PixmapPtr       pixmap = (*screen->GetWindowPixmap)(window);
    WindowPtr       w = window;
    WindowPtr       next_w;

    while(w->parent) {
        next_w = w->parent;
        if ( (*screen->GetWindowPixmap)(next_w) != pixmap) {
            break;
        }
        w = next_w;
    }
    return w;
}

void
present_wnmd_set_abort_flip(WindowPtr window)
{
    present_window_priv_ptr window_priv = present_window_priv(window);

    if (!window_priv->flip_pending->abort_flip) {
        window_priv->flip_pending->abort_flip = TRUE;
    }
}

static void
present_wnmd_flips_stop(WindowPtr window)
{
    present_window_priv_ptr window_priv = present_window_priv(window);
    present_screen_priv_ptr screen_priv = present_screen_priv(window->drawable.pScreen);

    assert (!window_priv->flip_pending);

    (*screen_priv->wnmd_info->flips_stop) (window);

    present_wnmd_free_idle_vblanks(window_priv->window);
    present_wnmd_flip_try_ready(window_priv->window);
}

static void
present_wnmd_flip_notify(present_vblank_ptr vblank, uint64_t ust, uint64_t crtc_msc)
{
    WindowPtr                   window = vblank->window;
    present_window_priv_ptr     window_priv = present_window_priv(window);

    DebugPresent(("\tn %" PRIu64 " %p %" PRIu64 ": %08" PRIx32 " -> %08" PRIx32 "\n",
                  vblank->event_id, vblank, vblank->target_msc,
                  vblank->pixmap ? vblank->pixmap->drawable.id : 0,
                  vblank->window ? vblank->window->drawable.id : 0));

    assert (vblank == window_priv->flip_pending);

    xorg_list_del(&vblank->event_queue);

    if (window_priv->flip_active) {
        if (window_priv->flip_active->flip_idler)
            present_wnmd_free_idle_vblank(window_priv->flip_active);
        else
            /* Put the previous flip in the idle_queue and wait for further notice from DDX */
            xorg_list_append(&window_priv->flip_active->event_queue, &window_priv->idle_queue);
    }

    window_priv->flip_active = vblank;
    window_priv->flip_pending = NULL;

    present_vblank_notify(vblank, PresentCompleteKindPixmap, PresentCompleteModeFlip, ust, crtc_msc);

    if (vblank->abort_flip)
        present_wnmd_flips_stop(window);

    present_wnmd_flip_try_ready(window);
}

void
present_wnmd_event_notify(WindowPtr window, uint64_t event_id, uint64_t ust, uint64_t msc)
{
    present_window_priv_ptr     window_priv = present_window_priv(window);
    present_vblank_ptr          vblank;

    if (!window_priv)
        return;
    if (!event_id)
        return;

    if (window_priv->flip_active && window_priv->flip_active->event_id == event_id) {
        /* Notify for active flip, means it is allowed to become idle */
        window_priv->flip_active->flip_idler = TRUE;
        return;
    }

    DebugPresent(("\te %" PRIu64 " ust %" PRIu64 " msc %" PRIu64 "\n", event_id, ust, msc));
    xorg_list_for_each_entry(vblank, &window_priv->exec_queue, event_queue) {
        if (event_id == vblank->event_id) {
            present_wnmd_execute(vblank, ust, msc);
            return;
        }
    }
    xorg_list_for_each_entry(vblank, &window_priv->flip_queue, event_queue) {
        if (vblank->event_id == event_id) {
            if (vblank->queued) {
                present_wnmd_execute(vblank, ust, msc);
            } else {
                assert(vblank->window);
                present_wnmd_flip_notify(vblank, ust, msc);
            }
            return;
        }
    }

    xorg_list_for_each_entry(vblank, &window_priv->idle_queue, event_queue) {
        if (vblank->event_id == event_id) {
            present_wnmd_free_idle_vblank(vblank);
            return;
        }
    }
}

static Bool
present_wnmd_check_flip(RRCrtcPtr           crtc,
                        WindowPtr           window,
                        PixmapPtr           pixmap,
                        Bool                sync_flip,
                        RegionPtr           valid,
                        int16_t             x_off,
                        int16_t             y_off,
                        PresentFlipReason   *reason)
{
    ScreenPtr               screen = window->drawable.pScreen;
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);
    WindowPtr               toplvl_window = present_wnmd_toplvl_pixmap_window(window);

    if (reason)
        *reason = PRESENT_FLIP_REASON_UNKNOWN;

    if (!screen_priv)
        return FALSE;

    if (!screen_priv->wnmd_info)
        return FALSE;

    if (!crtc)
        return FALSE;

    /* Check to see if the driver supports flips at all */
    if (!screen_priv->wnmd_info->flip)
        return FALSE;

    /* Can't flip redirected child windows */
    if (screen->GetWindowPixmap(window) != screen->GetWindowPixmap(toplvl_window))
        return FALSE;

    /* Source pixmap must align with window exactly */
    if (x_off || y_off)
        return FALSE;

    // TODO: Check for valid region?

    /* Flip pixmap must have same dimensions as window */
    if (window->drawable.width != pixmap->drawable.width ||
            window->drawable.height != pixmap->drawable.height)
        return FALSE;

    /* Window must be same region as toplevel window */
    if ( !RegionEqual(&window->winSize, &toplvl_window->winSize) )
        return FALSE;

    /* Ask the driver for permission */
    if (screen_priv->wnmd_info->check_flip2) {
        if (!(*screen_priv->wnmd_info->check_flip2) (crtc, window, pixmap, sync_flip, reason)) {
            DebugPresent(("\td %08" PRIx32 " -> %08" PRIx32 "\n",
                          window->drawable.id, pixmap ? pixmap->drawable.id : 0));
            return FALSE;
        }
    }

    return TRUE;
}

/*
 * 'window' is being reconfigured. Check to see if it is involved
 * in flipping and clean up as necessary.
 */
static void
present_wnmd_check_flip_window (WindowPtr window)
{
    present_window_priv_ptr window_priv = present_window_priv(window);
    present_vblank_ptr      flip_pending;
    present_vblank_ptr      flip_active;
    present_vblank_ptr      vblank;
    PresentFlipReason       reason;

    /* If this window hasn't ever been used with Present, it can't be
     * flipping
     */
    if (!window_priv)
        return;

    flip_pending = window_priv->flip_pending;
    flip_active = window_priv->flip_active;

    if (flip_pending) {
        if (!present_wnmd_check_flip(flip_pending->crtc, flip_pending->window, flip_pending->pixmap,
                                flip_pending->sync_flip, NULL, 0, 0, NULL))
            present_wnmd_set_abort_flip(window);
    } else if (flip_active) {
        if (!present_wnmd_check_flip(flip_active->crtc, flip_active->window, flip_active->pixmap,
                                     flip_active->sync_flip, NULL, 0, 0, NULL))
            present_wnmd_flips_stop(window);
    }

    /* Now check any queued vblanks */
    xorg_list_for_each_entry(vblank, &window_priv->vblank, window_list) {
        if (vblank->queued && vblank->flip &&
                !present_wnmd_check_flip(vblank->crtc, window, vblank->pixmap,
                                         vblank->sync_flip, NULL, 0, 0, &reason)) {
            vblank->flip = FALSE;
            vblank->reason = reason;
            if (vblank->sync_flip)
                vblank->requeue = TRUE;
        }
    }
}

static Bool
present_wnmd_flip(WindowPtr window,
                  RRCrtcPtr crtc,
                  uint64_t event_id,
                  uint64_t target_msc,
                  PixmapPtr pixmap,
                  Bool sync_flip,
                  RegionPtr damage)
{
    ScreenPtr                   screen = window->drawable.pScreen;
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);

    return (*screen_priv->wnmd_info->flip) (window,
                                            crtc,
                                            event_id,
                                            target_msc,
                                            pixmap,
                                            sync_flip,
                                            damage);
}

static void
present_wnmd_cancel_flip(WindowPtr window)
{
    present_window_priv_ptr window_priv = present_window_priv(window);

    if (window_priv->flip_pending)
        present_wnmd_set_abort_flip(window);
    else if (window_priv->flip_active)
        present_wnmd_flips_stop(window);
}

static Bool
present_wnmd_can_window_flip(WindowPtr window)
{
    ScreenPtr                   screen = window->drawable.pScreen;
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);
    WindowPtr                   toplvl_window = present_wnmd_toplvl_pixmap_window(window);

    if (!screen_priv)
        return FALSE;

    if (!screen_priv->wnmd_info)
        return FALSE;

    /* Check to see if the driver supports flips at all */
    if (!screen_priv->wnmd_info->flip)
        return FALSE;

    /* Don't flip redirected windows */
    if (window->redirectDraw != RedirectDrawNone)
        return FALSE;

    /* Window must be same region as toplevel window */
    if ( !RegionEqual(&window->winSize, &toplvl_window->winSize) )
        return FALSE;

    return TRUE;
}

/*
 * Once the required MSC has been reached, execute the pending request.
 *
 * For requests to actually present something, either blt contents to
 * the window pixmap or queue a window buffer swap on the backend.
 *
 * For requests to just get the current MSC/UST combo, skip that part and
 * go straight to event delivery.
 */
static void
present_wnmd_execute(present_vblank_ptr vblank, uint64_t ust, uint64_t crtc_msc)
{
    WindowPtr               window = vblank->window;
    present_window_priv_ptr window_priv = present_window_priv(window);

    if (present_execute_wait(vblank, crtc_msc))
        return;

    if (vblank->flip && vblank->pixmap && vblank->window) {
        if (window_priv->flip_pending) {
            DebugPresent(("\tr %" PRIu64 " %p (pending %p)\n",
                          vblank->event_id, vblank,
                          window_priv->flip_pending));
            xorg_list_del(&vblank->event_queue);
            xorg_list_append(&vblank->event_queue, &window_priv->flip_queue);
            vblank->flip_ready = TRUE;
            return;
        }
    }

    xorg_list_del(&vblank->event_queue);
    xorg_list_del(&vblank->window_list);
    vblank->queued = FALSE;

    if (vblank->pixmap && vblank->window) {

        if (vblank->flip) {
            RegionPtr damage;

            DebugPresent(("\tf %" PRIu64 " %p %" PRIu64 ": %08" PRIx32 " -> %08" PRIx32 "\n",
                          vblank->event_id, vblank, crtc_msc,
                          vblank->pixmap->drawable.id, vblank->window->drawable.id));

            /* Prepare to flip by placing it in the flip queue
             */
            xorg_list_add(&vblank->event_queue, &window_priv->flip_queue);

            /* Set update region as damaged */
            if (vblank->update) {
                damage = vblank->update;
                RegionIntersect(damage, damage, &window->clipList);
            } else
                damage = &window->clipList;

            /* Try to flip - the vblank is now pending
             */
            window_priv->flip_pending = vblank;
            // ask the driver
            if (present_wnmd_flip(vblank->window, vblank->crtc, vblank->event_id,
                                     vblank->target_msc, vblank->pixmap, vblank->sync_flip, damage)) {
                ScreenPtr screen = window->drawable.pScreen;
                WindowPtr toplvl_window = present_wnmd_toplvl_pixmap_window(vblank->window);
                PixmapPtr old_pixmap = screen->GetWindowPixmap(window);

                /* Replace window pixmap with flip pixmap */
#ifdef COMPOSITE
                vblank->pixmap->screen_x = old_pixmap->screen_x;
                vblank->pixmap->screen_y = old_pixmap->screen_y;
#endif
                present_set_tree_pixmap(toplvl_window, old_pixmap, vblank->pixmap);
                vblank->pixmap->refcnt++;
                dixDestroyPixmap(old_pixmap, old_pixmap->drawable.id);

                /* Report damage */
                DamageDamageRegion(&vblank->window->drawable, damage);
                return;
            }

            xorg_list_del(&vblank->event_queue);
            /* Flip failed. Clear the flip_pending field
              */
            window_priv->flip_pending = NULL;
            vblank->flip = FALSE;
        }
        DebugPresent(("\tc %p %" PRIu64 ": %08" PRIx32 " -> %08" PRIx32 "\n",
                      vblank, crtc_msc, vblank->pixmap->drawable.id, vblank->window->drawable.id));

        present_wnmd_cancel_flip(window);

        present_execute_copy(vblank, crtc_msc);

        if (vblank->queued) {
            xorg_list_add(&vblank->event_queue, &window_priv->exec_queue);
            xorg_list_append(&vblank->window_list, &window_priv->vblank);

            return;
        }
    }

    present_execute_post(vblank, ust, crtc_msc);
}

static int
present_wnmd_queue_vblank(ScreenPtr screen,
                             WindowPtr window,
                             RRCrtcPtr crtc,
                             uint64_t event_id,
                             uint64_t msc)
{
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);
    return (*screen_priv->wnmd_info->queue_vblank) (window, crtc, event_id, msc);
}

static void
present_wnmd_update_window_crtc(WindowPtr window, RRCrtcPtr crtc, uint64_t new_msc)
{
    present_window_priv_ptr window_priv = present_get_window_priv(window, TRUE);

    /* Crtc unchanged, no offset. */
    if (crtc == window_priv->crtc)
        return;

    /* No crtc earlier to offset against, just set the crtc. */
    if (window_priv->crtc == PresentCrtcNeverSet) {
        window_priv->msc_offset = 0;
        window_priv->crtc = crtc;
        return;
    }

    /* In window-mode the last correct msc-offset is always kept
     * in window-priv struct because msc is saved per window and
     * not per crtc as in screen-mode.
     */
    window_priv->msc_offset += new_msc - window_priv->msc;
    window_priv->crtc = crtc;
}

static int
present_wnmd_pixmap(WindowPtr window,
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
                    uint64_t target_window_msc,
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

    target_crtc = present_wnmd_get_crtc(screen_priv, window);

    ret = present_wnmd_get_ust_msc(screen, window, &ust, &crtc_msc);

    present_wnmd_update_window_crtc(window, target_crtc, crtc_msc);

    if (ret == Success) {
        /* Stash the current MSC away in case we need it later
         */
        window_priv->msc = crtc_msc;
    }

    target_msc = present_get_target_msc(target_window_msc + window_priv->msc_offset,
                                        crtc_msc,
                                        divisor,
                                        remainder,
                                        options);

    /*
     * Look for a matching presentation already on the list...
     */

    if (!update && pixmap) {
        xorg_list_for_each_entry_safe(vblank, tmp, &window_priv->vblank, window_list) {

            if (!vblank->pixmap)
                continue;

            if (!vblank->queued)
                continue;

            if (vblank->target_msc != target_msc)
                continue;

            present_vblank_scrap(vblank);
            if (vblank->flip_ready)
                present_wnmd_re_execute(vblank);
        }
    }

    vblank = present_vblank_create(window,
                                   pixmap,
                                   serial,
                                   valid,
                                   update,
                                   x_off,
                                   y_off,
                                   target_crtc,
                                   wait_fence,
                                   idle_fence,
                                   options,
                                   &screen_priv->wnmd_info->capabilities,
                                   notifies,
                                   num_notifies,
                                   &target_msc,
                                   crtc_msc);
    if (!vblank)
        return BadAlloc;

    xorg_list_append(&vblank->event_queue, &window_priv->exec_queue);
    vblank->queued = TRUE;
    if (crtc_msc < target_msc) {
        if (present_wnmd_queue_vblank(screen, window, target_crtc, vblank->event_id, target_msc) == Success) {
            return Success;
        }
        DebugPresent(("present_queue_vblank failed\n"));
    }

    present_wnmd_execute(vblank, ust, crtc_msc);
    return Success;
}

static void
present_wnmd_abort_vblank(ScreenPtr screen, WindowPtr window, RRCrtcPtr crtc, uint64_t event_id, uint64_t msc)
{
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);
    present_window_priv_ptr window_priv = present_window_priv(window);
    present_vblank_ptr      vblank;

    (*screen_priv->wnmd_info->abort_vblank) (window, crtc, event_id, msc);

    xorg_list_for_each_entry(vblank, &window_priv->exec_queue, event_queue) {
        if (vblank->event_id == event_id) {
            xorg_list_del(&vblank->event_queue);
            vblank->queued = FALSE;
            return;
        }
    }
    xorg_list_for_each_entry(vblank, &window_priv->flip_queue, event_queue) {
        if (vblank->event_id == event_id) {
            xorg_list_del(&vblank->event_queue);
            vblank->queued = FALSE;
            return;
        }
    }
}

static void
present_wnmd_flip_destroy(ScreenPtr screen)
{
    /* Cleanup done on window destruction */
}

static void
present_wnmd_flush(WindowPtr window)
{
    ScreenPtr               screen = window->drawable.pScreen;
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);

    (*screen_priv->wnmd_info->flush) (window);
}

void
present_wnmd_init_mode_hooks(present_screen_priv_ptr screen_priv)
{
    screen_priv->query_capabilities =   &present_wnmd_query_capabilities;
    screen_priv->get_crtc           =   &present_wnmd_get_crtc;

    screen_priv->check_flip         =   &present_wnmd_check_flip;
    screen_priv->check_flip_window  =   &present_wnmd_check_flip_window;
    screen_priv->can_window_flip    =   &present_wnmd_can_window_flip;

    screen_priv->present_pixmap     =   &present_wnmd_pixmap;
    screen_priv->create_event_id    =   &present_wnmd_create_event_id;
    screen_priv->queue_vblank       =   &present_wnmd_queue_vblank;
    screen_priv->flush              =   &present_wnmd_flush;
    screen_priv->re_execute         =   &present_wnmd_re_execute;

    screen_priv->abort_vblank       =   &present_wnmd_abort_vblank;
    screen_priv->flip_destroy       =   &present_wnmd_flip_destroy;
}
