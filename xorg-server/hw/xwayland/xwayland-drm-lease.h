/*
 * Copyright © 2020 Drew Devault
 * Copyright © 2021 Xaver Hugl
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

#ifndef XWAYLAND_DRM_LEASE_H
#define XWAYLAND_DRM_LEASE_H

#include <X11/Xatom.h>
#include <randrstr.h>

#include "xwayland-types.h"
#include "list.h"

#include "drm-lease-v1-client-protocol.h"

struct xwl_drm_lease_device {
    struct xorg_list link;
    struct wp_drm_lease_device_v1 *drm_lease_device;
    int drm_read_only_fd;
    struct xwl_screen *xwl_screen;
    uint32_t id;
};

struct xwl_queued_drm_lease_device {
    struct xorg_list link;
    uint32_t id;
};

struct xwl_drm_lease {
    struct xorg_list link;
    struct wp_drm_lease_v1 *lease;
    RRLeasePtr rrLease;
    ClientPtr client;
    int fd;
};

int xwl_randr_request_lease(ClientPtr client, ScreenPtr screen, RRLeasePtr rrLease);
void xwl_randr_get_lease(ClientPtr client, ScreenPtr screen, RRLeasePtr *rrLease, int *fd);
void xwl_randr_terminate_lease(ScreenPtr screen, RRLeasePtr lease);

void xwl_screen_add_drm_lease_device(struct xwl_screen *xwl_screen, uint32_t id);
void xwl_screen_destroy_drm_lease_device(struct xwl_screen *xwl_screen,
                                         struct wp_drm_lease_device_v1 *wp_drm_lease_device_v1);

#endif /* XWAYLAND_DRM_LEASE_H */
