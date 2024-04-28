/*
 * Copyright © 2011-2014 Intel Corporation
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

#ifndef XWAYLAND_DMABUF_H
#define XWAYLAND_DMABUF_H

#include <xwayland-config.h>

#include <X11/X.h>
#include <dix.h>
#include <xf86drm.h>

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>

#include "xwayland-types.h"

struct xwl_format {
    uint32_t format;
    int num_modifiers;
    uint64_t *modifiers;
};

struct xwl_format_table_entry {
    uint32_t format;
    uint32_t pad;
    uint64_t modifier;
};

struct xwl_device_formats {
    drmDevice *drm_dev;
    uint32_t num_formats;
    struct xwl_format *formats;
    Bool supports_scanout;
};

struct xwl_format_table {
    /* This is mmapped from the fd given to us by the compositor */
    int len;
    struct xwl_format_table_entry *entry;
};

/*
 * Helper struct for sharing dmabuf feedback logic between
 * a screen and a window. The screen will get the default
 * feedback, and a window will get a per-surface feedback.
 */
struct xwl_dmabuf_feedback {
    struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback;
    struct xwl_format_table format_table;
    drmDevice *main_dev;
    /*
     * This will be filled in during wl events and copied to
     * dev_formats on dmabuf_feedback.tranche_done
     */
    struct xwl_device_formats tmp_tranche;
    int feedback_done;
    int dev_formats_len;
    struct xwl_device_formats *dev_formats;
    /*
     * This flag is used to identify if the feedback
     * has been resent. If this is true, then the xwayland
     * clients need to be sent PresentCompleteModeSuboptimalCopy
     * to tell them to re-request modifiers.
     */
    int unprocessed_feedback_pending;
};

void xwl_dmabuf_feedback_destroy(struct xwl_dmabuf_feedback *xwl_feedback);
void xwl_dmabuf_feedback_clear_dev_formats(struct xwl_dmabuf_feedback *xwl_feedback);
void xwl_device_formats_destroy(struct xwl_device_formats *dev_formats);

Bool xwl_dmabuf_setup_feedback_for_window(struct xwl_window *xwl_window);
Bool xwl_screen_set_dmabuf_interface(struct xwl_screen *xwl_screen,
                                     uint32_t id, uint32_t version);
Bool xwl_glamor_is_modifier_supported(struct xwl_screen *xwl_screen,
                                      uint32_t format, uint64_t modifier);
uint32_t wl_drm_format_for_depth(int depth);
Bool xwl_glamor_get_formats(ScreenPtr screen,
                            CARD32 *num_formats, CARD32 **formats);
Bool xwl_glamor_get_modifiers(ScreenPtr screen, uint32_t format,
                              uint32_t *num_modifiers, uint64_t **modifiers);
Bool xwl_glamor_get_drawable_modifiers_and_scanout(DrawablePtr drawable,
                                                   uint32_t format,
                                                   uint32_t *num_modifiers,
                                                   uint64_t **modifiers,
                                                   Bool *supports_scanout);
Bool xwl_glamor_get_drawable_modifiers(DrawablePtr drawable, uint32_t format,
                                       uint32_t *num_modifiers, uint64_t **modifiers);

#endif /* XWAYLAND_DMABUF_H */
