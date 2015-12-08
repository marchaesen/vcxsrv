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

#ifndef _PRESENT_PRIV_H_
#define _PRESENT_PRIV_H_

#include <X11/X.h>
#include "scrnintstr.h"
#include "misc.h"
#include "list.h"
#include "windowstr.h"
#include "dixstruct.h"
#include "present.h"
#include <syncsdk.h>
#include <syncsrv.h>
#include <xfixes.h>
#include <randrstr.h>

extern int present_request;

extern DevPrivateKeyRec present_screen_private_key;

typedef struct present_fence *present_fence_ptr;

typedef struct present_notify present_notify_rec, *present_notify_ptr;

struct present_notify {
    struct xorg_list    window_list;
    WindowPtr           window;
    CARD32              serial;
};

struct present_vblank {
    struct xorg_list    window_list;
    struct xorg_list    event_queue;
    ScreenPtr           screen;
    WindowPtr           window;
    PixmapPtr           pixmap;
    RegionPtr           valid;
    RegionPtr           update;
    RRCrtcPtr           crtc;
    uint32_t            serial;
    int16_t             x_off;
    int16_t             y_off;
    CARD16              kind;
    uint64_t            event_id;
    uint64_t            target_msc;
    uint64_t            msc_offset;
    present_fence_ptr   idle_fence;
    present_fence_ptr   wait_fence;
    present_notify_ptr  notifies;
    int                 num_notifies;
    Bool                queued;         /* on present_exec_queue */
    Bool                requeue;        /* on queue, but target_msc has changed */
    Bool                flip;           /* planning on using flip */
    Bool                flip_ready;     /* wants to flip, but waiting for previous flip or unflip */
    Bool                sync_flip;      /* do flip synchronous to vblank */
    Bool                abort_flip;     /* aborting this flip */
};

typedef struct present_screen_priv {
    CloseScreenProcPtr          CloseScreen;
    ConfigNotifyProcPtr         ConfigNotify;
    DestroyWindowProcPtr        DestroyWindow;
    ClipNotifyProcPtr           ClipNotify;

    present_vblank_ptr          flip_pending;
    uint64_t                    unflip_event_id;

    uint32_t                    fake_interval;

    /* Currently active flipped pixmap and fence */
    RRCrtcPtr                   flip_crtc;
    WindowPtr                   flip_window;
    uint32_t                    flip_serial;
    PixmapPtr                   flip_pixmap;
    present_fence_ptr           flip_idle_fence;
    Bool                        flip_sync;

    present_screen_info_ptr     info;
} present_screen_priv_rec, *present_screen_priv_ptr;

#define wrap(priv,real,mem,func) {\
    priv->mem = real->mem; \
    real->mem = func; \
}

#define unwrap(priv,real,mem) {\
    real->mem = priv->mem; \
}

static inline present_screen_priv_ptr
present_screen_priv(ScreenPtr screen)
{
    return (present_screen_priv_ptr)dixLookupPrivate(&(screen)->devPrivates, &present_screen_private_key);
}

/*
 * Each window has a list of clients and event masks
 */
typedef struct present_event *present_event_ptr;

typedef struct present_event {
    present_event_ptr next;
    ClientPtr client;
    WindowPtr window;
    XID id;
    int mask;
} present_event_rec;

typedef struct present_window_priv {
    present_event_ptr      events;
    RRCrtcPtr              crtc;        /* Last reported CRTC from get_ust_msc */
    uint64_t               msc_offset;
    uint64_t               msc;         /* Last reported MSC from the current crtc */
    struct xorg_list       vblank;
    struct xorg_list       notifies;
} present_window_priv_rec, *present_window_priv_ptr;

#define PresentCrtcNeverSet     ((RRCrtcPtr) 1)

extern DevPrivateKeyRec present_window_private_key;

static inline present_window_priv_ptr
present_window_priv(WindowPtr window)
{
    return (present_window_priv_ptr)dixGetPrivate(&(window)->devPrivates, &present_window_private_key);
}

present_window_priv_ptr
present_get_window_priv(WindowPtr window, Bool create);

/*
 * present.c
 */
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
               uint64_t target_msc,
               uint64_t divisor,
               uint64_t remainder,
               present_notify_ptr notifies,
               int num_notifies);

int
present_notify_msc(WindowPtr window,
                   CARD32 serial,
                   uint64_t target_msc,
                   uint64_t divisor,
                   uint64_t remainder);

void
present_abort_vblank(ScreenPtr screen, RRCrtcPtr crtc, uint64_t event_id, uint64_t msc);

void
present_vblank_destroy(present_vblank_ptr vblank);

void
present_flip_destroy(ScreenPtr screen);

void
present_check_flip_window(WindowPtr window);

RRCrtcPtr
present_get_crtc(WindowPtr window);

uint32_t
present_query_capabilities(RRCrtcPtr crtc);

Bool
present_init(void);

/*
 * present_event.c
 */

void
present_free_events(WindowPtr window);

void
present_send_config_notify(WindowPtr window, int x, int y, int w, int h, int bw, WindowPtr sibling);

void
present_send_complete_notify(WindowPtr window, CARD8 kind, CARD8 mode, CARD32 serial, uint64_t ust, uint64_t msc);

void
present_send_idle_notify(WindowPtr window, CARD32 serial, PixmapPtr pixmap, present_fence_ptr idle_fence);

int
present_select_input(ClientPtr client,
                     CARD32 eid,
                     WindowPtr window,
                     CARD32 event_mask);

Bool
present_event_init(void);

/*
 * present_fake.c
 */
int
present_fake_get_ust_msc(ScreenPtr screen, uint64_t *ust, uint64_t *msc);

int
present_fake_queue_vblank(ScreenPtr screen, uint64_t event_id, uint64_t msc);

void
present_fake_abort_vblank(ScreenPtr screen, uint64_t event_id, uint64_t msc);

void
present_fake_screen_init(ScreenPtr screen);

void
present_fake_queue_init(void);

/*
 * present_fence.c
 */
struct present_fence *
present_fence_create(SyncFence *sync_fence);

void
present_fence_destroy(struct present_fence *present_fence);

void
present_fence_set_triggered(struct present_fence *present_fence);

Bool
present_fence_check_triggered(struct present_fence *present_fence);

void
present_fence_set_callback(struct present_fence *present_fence,
                           void (*callback)(void *param),
                           void *param);

XID
present_fence_id(struct present_fence *present_fence);

/*
 * present_notify.c
 */
void
present_clear_window_notifies(WindowPtr window);

void
present_free_window_notify(present_notify_ptr notify);

int
present_add_window_notify(present_notify_ptr notify);

int
present_create_notifies(ClientPtr client, int num_notifies, xPresentNotify *x_notifies, present_notify_ptr *p_notifies);

void
present_destroy_notifies(present_notify_ptr notifies, int num_notifies);

/*
 * present_redirect.c
 */

WindowPtr
present_redirect(ClientPtr client, WindowPtr target);

/*
 * present_request.c
 */
int
proc_present_dispatch(ClientPtr client);

int
sproc_present_dispatch(ClientPtr client);

/*
 * present_screen.c
 */

#endif /*  _PRESENT_PRIV_H_ */
