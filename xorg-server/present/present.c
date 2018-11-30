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

/*
 * Returns:
 * TRUE if the first MSC value is equal to or after the second one
 * FALSE if the first MSC value is before the second one
 */
static Bool
msc_is_equal_or_after(uint64_t test, uint64_t reference)
{
    return (int64_t)(test - reference) >= 0;
}

uint32_t
present_query_capabilities(RRCrtcPtr crtc)
{
    present_screen_priv_ptr screen_priv;

    if (!crtc)
        return 0;

    screen_priv = present_screen_priv(crtc->pScreen);

    if (!screen_priv)
        return 0;

    return screen_priv->query_capabilities(screen_priv);
}

RRCrtcPtr
present_get_crtc(WindowPtr window)
{
    ScreenPtr                   screen = window->drawable.pScreen;
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);

    if (!screen_priv)
        return NULL;

    return screen_priv->get_crtc(screen_priv, window);
}

/*
 * Copies the update region from a pixmap to the target drawable
 */
void
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

void
present_pixmap_idle(PixmapPtr pixmap, WindowPtr window, CARD32 serial, struct present_fence *present_fence)
{
    if (present_fence)
        present_fence_set_triggered(present_fence);
    if (window) {
        DebugPresent(("\ti %08" PRIx32 "\n", pixmap ? pixmap->drawable.id : 0));
        present_send_idle_notify(window, serial, pixmap, present_fence);
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

void
present_set_tree_pixmap(WindowPtr window,
                        PixmapPtr expected,
                        PixmapPtr pixmap)
{
    struct pixmap_visit visit;
    ScreenPtr           screen = window->drawable.pScreen;

    visit.old = (*screen->GetWindowPixmap)(window);
    if (expected && visit.old != expected)
        return;

    visit.new = pixmap;
    if (visit.old == visit.new)
        return;
    TraverseTree(window, present_set_tree_pixmap_visit, &visit);
}

Bool
present_can_window_flip(WindowPtr window)
{
    ScreenPtr                   screen = window->drawable.pScreen;
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);

    return screen_priv->can_window_flip(window);
}

void
present_adjust_timings(uint32_t options,
                       uint64_t *crtc_msc,
                       uint64_t *target_msc,
                       uint64_t divisor,
                       uint64_t remainder)
{
    /* Adjust target_msc to match modulus
     */
    if (msc_is_equal_or_after(*crtc_msc, *target_msc)) {
        if (divisor != 0) {
            *target_msc = *crtc_msc - (*crtc_msc % divisor) + remainder;
            if (options & PresentOptionAsync) {
                if (msc_is_after(*crtc_msc, *target_msc))
                    *target_msc += divisor;
            } else {
                if (msc_is_equal_or_after(*crtc_msc, *target_msc))
                    *target_msc += divisor;
            }
        } else {
            *target_msc = *crtc_msc;
            if (!(options & PresentOptionAsync))
                (*target_msc)++;
        }
    }
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
    ScreenPtr                   screen = window->drawable.pScreen;
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);

    return screen_priv->present_pixmap(window,
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
                                       window_msc,
                                       divisor,
                                       remainder,
                                       notifies,
                                       num_notifies);
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
