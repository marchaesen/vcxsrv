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

#include <xwayland-config.h>

#include <compositeext.h>

#define MESA_EGL_NO_X11_HEADERS
#define EGL_NO_X11
#include <glamor_egl.h>

#include <glamor.h>
#include <glamor_context.h>
#include <glamor_glx_provider.h>
#ifdef GLXEXT
#include "glx_extinit.h"
#endif

#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "drm-client-protocol.h"
#include <drm_fourcc.h>

#include "xwayland-glamor.h"
#include "xwayland-screen.h"
#include "xwayland-window.h"
#include "xwayland-window-buffers.h"

#include <sys/mman.h>

static void
glamor_egl_make_current(struct glamor_context *glamor_ctx)
{
    eglMakeCurrent(glamor_ctx->display, EGL_NO_SURFACE,
                   EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (!eglMakeCurrent(glamor_ctx->display,
                        EGL_NO_SURFACE, EGL_NO_SURFACE,
                        glamor_ctx->ctx))
        FatalError("Failed to make EGL context current\n");
}

void
xwl_glamor_egl_make_current(struct xwl_screen *xwl_screen)
{
    EGLContext ctx = xwl_screen->glamor_ctx->ctx;
    
    if (lastGLContext == ctx)
        return;

    lastGLContext = ctx;
    xwl_screen->glamor_ctx->make_current(xwl_screen->glamor_ctx);
}

void
glamor_egl_screen_init(ScreenPtr screen, struct glamor_context *glamor_ctx)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);

    glamor_set_glvnd_vendor(screen, xwl_screen->glvnd_vendor);
    glamor_enable_dri3(screen);
    glamor_ctx->ctx = xwl_screen->egl_context;
    glamor_ctx->display = xwl_screen->egl_display;

    glamor_ctx->make_current = glamor_egl_make_current;

    xwl_screen->glamor_ctx = glamor_ctx;
}

Bool
xwl_glamor_check_flip(WindowPtr present_window, PixmapPtr pixmap)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    PixmapPtr backing_pixmap = screen->GetWindowPixmap(present_window);

    if (pixmap->drawable.depth != backing_pixmap->drawable.depth)
        return FALSE;

    if (!xwl_glamor_pixmap_get_wl_buffer(pixmap))
        return FALSE;

    if (xwl_screen->egl_backend->check_flip)
        return xwl_screen->egl_backend->check_flip(pixmap);

    return TRUE;
}

static Bool
xwl_glamor_is_modifier_supported_in_formats(struct xwl_format *formats, int num_formats,
                                            uint32_t format, uint64_t modifier)
{
    struct xwl_format *xwl_format = NULL;
    int i;

    for (i = 0; i < num_formats; i++) {
        if (formats[i].format == format) {
            xwl_format = &formats[i];
            break;
        }
    }

    if (xwl_format) {
        for (i = 0; i < xwl_format->num_modifiers; i++) {
            if (xwl_format->modifiers[i] == modifier) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static Bool
xwl_feedback_is_modifier_supported(struct xwl_dmabuf_feedback *xwl_feedback,
                                   uint32_t format, uint64_t modifier,
                                   int supports_scanout)
{
    for (int i = 0; i < xwl_feedback->dev_formats_len; i++) {
        struct xwl_device_formats *dev_formats = &xwl_feedback->dev_formats[i];

        if (supports_scanout && !dev_formats->supports_scanout)
            continue;

        if (xwl_glamor_is_modifier_supported_in_formats(dev_formats->formats,
                                                        dev_formats->num_formats,
                                                        format, modifier))
            return TRUE;
    }

    return FALSE;
}


Bool
xwl_glamor_is_modifier_supported(struct xwl_screen *xwl_screen,
                                 uint32_t format, uint64_t modifier)
{
    struct xwl_window *xwl_window;

    /*
     * If we are using dmabuf v4, then we need to check in the main
     * device and per-window format lists. For older protocol
     * versions we can just check the list returned by the dmabuf.modifier
     * events in xwl_screen
     */
    if (xwl_screen->dmabuf_protocol_version < 4) {
        return xwl_glamor_is_modifier_supported_in_formats(xwl_screen->formats,
                                                           xwl_screen->num_formats,
                                                           format, modifier);
    }

    if (xwl_feedback_is_modifier_supported(&xwl_screen->default_feedback, format, modifier, FALSE))
        return TRUE;

    xorg_list_for_each_entry(xwl_window, &xwl_screen->window_list, link_window) {
        if (xwl_feedback_is_modifier_supported(&xwl_window->feedback, format, modifier, FALSE))
            return TRUE;
    }

    return FALSE;
}

uint32_t
wl_drm_format_for_depth(int depth)
{
    switch (depth) {
    case 15:
        return WL_DRM_FORMAT_XRGB1555;
    case 16:
        return WL_DRM_FORMAT_RGB565;
    case 24:
        return WL_DRM_FORMAT_XRGB8888;
    case 30:
        return WL_DRM_FORMAT_ARGB2101010;
    default:
        ErrorF("unexpected depth: %d\n", depth);
    case 32:
        return WL_DRM_FORMAT_ARGB8888;
    }
}

static drmDevice *
xwl_screen_get_main_dev(struct xwl_screen *xwl_screen)
{
    /*
     * If we have gbm then get our main device from it. Otherwise use what
     * the compositor told us.
     */
    if (xwl_screen->gbm_backend.is_available)
        return xwl_screen->gbm_backend.get_main_device(xwl_screen);
    else
        return xwl_screen->default_feedback.main_dev;
}

static Bool
xwl_get_formats(struct xwl_format *format_array, int format_array_len,
               uint32_t *num_formats, uint32_t **formats)
{
    *num_formats = 0;
    *formats = NULL;

    if (format_array_len == 0)
       return TRUE;

    *formats = calloc(format_array_len, sizeof(CARD32));
    if (*formats == NULL)
        return FALSE;

    for (int i = 0; i < format_array_len; i++)
       (*formats)[i] = format_array[i].format;
    *num_formats = format_array_len;

    return TRUE;
}

static Bool
xwl_get_formats_for_device(struct xwl_dmabuf_feedback *xwl_feedback, drmDevice *device,
                           uint32_t *num_formats, uint32_t **formats)
{
    uint32_t *ret = NULL;
    uint32_t count = 0;

    /* go through all matching sets of tranches for the window's device */
    for (int i = 0; i < xwl_feedback->dev_formats_len; i++) {
        if (drmDevicesEqual(xwl_feedback->dev_formats[i].drm_dev, device)) {
            struct xwl_device_formats *dev_formats = &xwl_feedback->dev_formats[i];

            /* Append the formats from this tranche to the list */
            ret = xnfreallocarray(ret, count + dev_formats->num_formats, sizeof(CARD32));

            for (int j = 0; j < dev_formats->num_formats; j++) {
                bool found = false;

                /* Check if this format is already present in the list */
                for (int k = 0; k < count; k++) {
                    if (ret[k] == dev_formats->formats[j].format) {
                        found = true;
                        break;
                    }
                }

                /* If this format has not yet been added, do so now */
                if (!found)
                    ret[count++] = dev_formats->formats[j].format;
            }
        }
    }

    *num_formats = count;
    *formats = ret;

    return TRUE;
}

Bool
xwl_glamor_get_formats(ScreenPtr screen,
                       CARD32 *num_formats, CARD32 **formats)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);

    /* Explicitly zero the count as the caller may ignore the return value */
    *num_formats = 0;

    if (!xwl_screen->dmabuf)
        return FALSE;

    if (xwl_screen->dmabuf_protocol_version >= 4) {
        drmDevice *main_dev = xwl_screen_get_main_dev(xwl_screen);

        return xwl_get_formats_for_device(&xwl_screen->default_feedback, main_dev,
                                          num_formats, formats);
    }

    return xwl_get_formats(xwl_screen->formats, xwl_screen->num_formats,
                           num_formats, formats);
}

static Bool
xwl_get_modifiers_for_format(struct xwl_format *format_array, int num_formats,
                             uint32_t format, uint32_t *num_modifiers, uint64_t **modifiers)
{
    struct xwl_format *xwl_format = NULL;
    int i;

    *num_modifiers = 0;
    *modifiers = NULL;

    if (num_formats == 0)
       return TRUE;

    for (i = 0; i < num_formats; i++) {
       if (format_array[i].format == format) {
          xwl_format = &format_array[i];
          break;
       }
    }

    if (!xwl_format ||
        (xwl_format->num_modifiers == 1 &&
         xwl_format->modifiers[0] == DRM_FORMAT_MOD_INVALID))
        return FALSE;

    *modifiers = calloc(xwl_format->num_modifiers, sizeof(uint64_t));
    if (*modifiers == NULL)
        return FALSE;

    for (i = 0; i < xwl_format->num_modifiers; i++)
       (*modifiers)[i] = xwl_format->modifiers[i];
    *num_modifiers = xwl_format->num_modifiers;

    return TRUE;
}

static Bool
xwl_get_modifiers_for_device(struct xwl_dmabuf_feedback *feedback, drmDevice *device,
                             uint32_t format, uint32_t *num_modifiers,
                             uint64_t **modifiers,
                             Bool *supports_scanout)
{
    /* Now try to find a matching set of tranches for the window's device */
    for (int i = 0; i < feedback->dev_formats_len; i++) {
        struct xwl_device_formats *dev_formats = &feedback->dev_formats[i];

        if (drmDevicesEqual(dev_formats->drm_dev, device) &&
            xwl_get_modifiers_for_format(dev_formats->formats, dev_formats->num_formats,
                                         format, num_modifiers, modifiers)) {
            if (supports_scanout)
                *supports_scanout = !!dev_formats->supports_scanout;
            return TRUE;
        }
    }

    return FALSE;
}

Bool
xwl_glamor_get_modifiers(ScreenPtr screen, uint32_t format,
                         uint32_t *num_modifiers, uint64_t **modifiers)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    drmDevice *main_dev;

    /* Explicitly zero the count as the caller may ignore the return value */
    *num_modifiers = 0;
    *modifiers = NULL;

    if (!xwl_screen->dmabuf)
        return FALSE;

    if (xwl_screen->dmabuf_protocol_version >= 4) {
        main_dev = xwl_screen_get_main_dev(xwl_screen);

        return xwl_get_modifiers_for_device(&xwl_screen->default_feedback, main_dev,
                                            format, num_modifiers, modifiers,
                                            NULL);
    } else {
        return xwl_get_modifiers_for_format(xwl_screen->formats, xwl_screen->num_formats,
                                            format, num_modifiers, modifiers);
    }
}

Bool
xwl_glamor_get_drawable_modifiers_and_scanout(DrawablePtr drawable,
                                              uint32_t format,
                                              uint32_t *num_modifiers,
                                              uint64_t **modifiers,
                                              Bool *supports_scanout)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(drawable->pScreen);
    struct xwl_window *xwl_window;
    drmDevice *main_dev;

    *num_modifiers = 0;
    *modifiers = NULL;
    if (supports_scanout)
        *supports_scanout = FALSE;

    /* We can only return per-drawable modifiers if the compositor supports feedback */
    if (xwl_screen->dmabuf_protocol_version < 4)
        return TRUE;

    if (drawable->type != DRAWABLE_WINDOW || !xwl_screen->dmabuf)
        return FALSE;

    xwl_window = xwl_window_from_window((WindowPtr)drawable);

    /* couldn't find drawable for window */
    if (!xwl_window)
        return FALSE;

    main_dev = xwl_screen_get_main_dev(xwl_screen);

    return xwl_get_modifiers_for_device(&xwl_window->feedback, main_dev,
                                        format, num_modifiers, modifiers,
                                        supports_scanout);

}

Bool
xwl_glamor_get_drawable_modifiers(DrawablePtr drawable, uint32_t format,
                                  uint32_t *num_modifiers, uint64_t **modifiers)
{
    return xwl_glamor_get_drawable_modifiers_and_scanout(drawable,
                                                         format, num_modifiers,
                                                         modifiers, NULL);

}

static void
xwl_dmabuf_handle_format(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
                         uint32_t format)
{
}

static void
xwl_add_format_and_mod_to_list(struct xwl_format **formats,
                               uint32_t *num_formats,
                               uint32_t format,
                               uint64_t modifier)
{
    struct xwl_format *xwl_format = NULL;
    int i;

    for (i = 0; i < *num_formats; i++) {
        if ((*formats)[i].format == format) {
            xwl_format = &(*formats)[i];
            break;
        }
    }

    if (xwl_format == NULL) {
        (*num_formats)++;
        *formats = xnfrealloc(*formats, *num_formats * sizeof(*xwl_format));
        xwl_format = &(*formats)[*num_formats - 1];
        xwl_format->format = format;
        xwl_format->num_modifiers = 0;
        xwl_format->modifiers = NULL;
    }

    for (i = 0; i < xwl_format->num_modifiers; i++) {
        /* don't add it if the modifier already exists */
        if (xwl_format->modifiers[i] == modifier)
            return;
    }

    xwl_format->num_modifiers++;
    xwl_format->modifiers = xnfrealloc(xwl_format->modifiers,
                                       xwl_format->num_modifiers * sizeof(uint64_t));
    xwl_format->modifiers[xwl_format->num_modifiers - 1]  = modifier;
}

static void
xwl_dmabuf_handle_modifier(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
                           uint32_t format, uint32_t modifier_hi,
                           uint32_t modifier_lo)
{
    struct xwl_screen *xwl_screen = data;

    xwl_add_format_and_mod_to_list(&xwl_screen->formats, &xwl_screen->num_formats,
                                   format,
                                   ((uint64_t)modifier_hi << 32 | (uint64_t)modifier_lo));
}

static const struct zwp_linux_dmabuf_v1_listener xwl_dmabuf_listener = {
    .format = xwl_dmabuf_handle_format,
    .modifier = xwl_dmabuf_handle_modifier
};

/*
 * We need to check if the compositor is resending all of the tranche
 * information. Each tranche event will call this method to see
 * if the existing format info should be cleared before refilling.
 */
static void
xwl_check_reset_tranche_info(struct xwl_dmabuf_feedback *xwl_feedback)
{
    if (!xwl_feedback->feedback_done)
        return;

    xwl_feedback->feedback_done = false;

    xwl_dmabuf_feedback_clear_dev_formats(xwl_feedback);
}

static void
xwl_dmabuf_feedback_main_device(void *data,
                                struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                struct wl_array *dev)
{
    struct xwl_dmabuf_feedback *xwl_feedback = data;
    dev_t devid;

    xwl_check_reset_tranche_info(xwl_feedback);

    assert(dev->size == sizeof(dev_t));
    memcpy(&devid, dev->data, sizeof(dev_t));

    if (drmGetDeviceFromDevId(devid, 0, &xwl_feedback->main_dev) != 0)
        ErrorF("linux_dmabuf_feedback.main_device: Failed to fetch DRM device\n");
}

static void
xwl_dmabuf_feedback_tranche_target_device(void *data,
                                          struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                          struct wl_array *dev)
{
    struct xwl_dmabuf_feedback *xwl_feedback = data;
    dev_t devid;

    xwl_check_reset_tranche_info(xwl_feedback);

    assert(dev->size == sizeof(dev_t));
    memcpy(&devid, dev->data, sizeof(dev_t));

    if (drmGetDeviceFromDevId(devid, 0, &xwl_feedback->tmp_tranche.drm_dev) != 0)
        ErrorF("linux_dmabuf_feedback.tranche_target_device: Failed to fetch DRM device\n");
}

static void
xwl_dmabuf_feedback_tranche_flags(void *data,
                                  struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                  uint32_t flags)
{
    struct xwl_dmabuf_feedback *xwl_feedback = data;

    xwl_check_reset_tranche_info(xwl_feedback);

    if (flags & ZWP_LINUX_DMABUF_FEEDBACK_V1_TRANCHE_FLAGS_SCANOUT)
        xwl_feedback->tmp_tranche.supports_scanout = true;
}

static void
xwl_dmabuf_feedback_tranche_formats(void *data,
                                    struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                    struct wl_array *indices)
{
    struct xwl_dmabuf_feedback *xwl_feedback = data;
    struct xwl_device_formats *tranche = &xwl_feedback->tmp_tranche;
    uint16_t *index;

    xwl_check_reset_tranche_info(xwl_feedback);

    wl_array_for_each(index, indices) {
        if (*index >= xwl_feedback->format_table.len) {
            ErrorF("linux_dmabuf_feedback.tranche_formats: Index given to us by the compositor"
                   " is too large to fit in the format table\n");
            continue;
        }

        /* Look up this format/mod in the format table */
        struct xwl_format_table_entry *entry = &xwl_feedback->format_table.entry[*index];

        /* Add it to the in-progress tranche */
        xwl_add_format_and_mod_to_list(&tranche->formats, &tranche->num_formats,
                                       entry->format,
                                       entry->modifier);
    }
}

static void
xwl_append_to_tranche(struct xwl_device_formats *dst, struct xwl_device_formats *src)
{
    struct xwl_format *format;

    for (int i = 0; i < src->num_formats; i++) {
        format = &src->formats[i];

        for (int j = 0; j < format->num_modifiers; j++)
            xwl_add_format_and_mod_to_list(&dst->formats, &dst->num_formats,
                                           format->format,
                                           format->modifiers[j]);
    }
}

static void
xwl_dmabuf_feedback_tranche_done(void *data,
                                 struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback)
{
    struct xwl_dmabuf_feedback *xwl_feedback = data;
    struct xwl_device_formats *tranche;
    int appended = false;

    /*
     * No need to call xwl_check_reset_tranche_info, the other events should have been
     * triggered first
     */

    if (xwl_feedback->tmp_tranche.drm_dev == NULL) {
        xwl_device_formats_destroy(&xwl_feedback->tmp_tranche);
        goto out;
    }

    /*
     * First check if there is an existing tranche for this device+flags combo. We
     * will combine it with this tranche, since we can only send one modifier list
     * in DRI3 but the compositor may report multiple tranches per device (KDE
     * does this)
     */
    for (int i = 0; i < xwl_feedback->dev_formats_len; i++) {
        tranche = &xwl_feedback->dev_formats[i];
        if (tranche->drm_dev == xwl_feedback->tmp_tranche.drm_dev &&
            tranche->supports_scanout == xwl_feedback->tmp_tranche.supports_scanout) {
            appended = true;

            /* Add all format/mods to this tranche */
            xwl_append_to_tranche(tranche, &xwl_feedback->tmp_tranche);

            /* Now free our temp tranche's allocations */
            xwl_device_formats_destroy(&xwl_feedback->tmp_tranche);

            break;
        }
    }

    if (!appended) {
        xwl_feedback->dev_formats_len++;
        xwl_feedback->dev_formats = xnfrealloc(xwl_feedback->dev_formats,
                                               sizeof(struct xwl_device_formats) *
                                               xwl_feedback->dev_formats_len);

        /* copy the temporary tranche into the official array */
        memcpy(&xwl_feedback->dev_formats[xwl_feedback->dev_formats_len - 1],
               &xwl_feedback->tmp_tranche,
               sizeof(struct xwl_device_formats));
    }

out:
    /* reset the tranche */
    memset(&xwl_feedback->tmp_tranche, 0, sizeof(struct xwl_device_formats));
}

static void
xwl_dmabuf_feedback_done(void *data, struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback)
{
    struct xwl_dmabuf_feedback *xwl_feedback = data;

    xwl_feedback->feedback_done = true;
    xwl_feedback->unprocessed_feedback_pending = true;
}

static void
xwl_dmabuf_feedback_format_table(void *data,
                                 struct zwp_linux_dmabuf_feedback_v1 *zwp_linux_dmabuf_feedback_v1,
                                 int32_t fd, uint32_t size)
{
    struct xwl_dmabuf_feedback *xwl_feedback = data;
    /* Unmap the old table */
    if (xwl_feedback->format_table.entry) {
        munmap(xwl_feedback->format_table.entry,
               xwl_feedback->format_table.len * sizeof(struct xwl_format_table_entry));
    }

    assert(size % sizeof(struct xwl_format_table_entry) == 0);
    xwl_feedback->format_table.len = size / sizeof(struct xwl_format_table_entry);
    xwl_feedback->format_table.entry = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (xwl_feedback->format_table.entry == MAP_FAILED) {
        ErrorF("linux_dmabuf_feedback.format_table: Could not map the format"
               " table: Compositor bug or out of resources\n");
        xwl_feedback->format_table.len = 0;
    }
}

static const struct zwp_linux_dmabuf_feedback_v1_listener xwl_dmabuf_feedback_listener = {
    .done = xwl_dmabuf_feedback_done,
    .format_table = xwl_dmabuf_feedback_format_table,
    .main_device = xwl_dmabuf_feedback_main_device,
    .tranche_done = xwl_dmabuf_feedback_tranche_done,
    .tranche_target_device = xwl_dmabuf_feedback_tranche_target_device,
    .tranche_formats = xwl_dmabuf_feedback_tranche_formats,
    .tranche_flags = xwl_dmabuf_feedback_tranche_flags,
};

Bool
xwl_screen_set_dmabuf_interface(struct xwl_screen *xwl_screen,
                                uint32_t id, uint32_t version)
{
    /* We either support versions 3 or 4. 4 is needed for dmabuf feedback */
    int supported_version = version >= 4 ? 4 : 3;

    if (version < 3)
        return FALSE;

    xwl_screen->dmabuf =
        wl_registry_bind(xwl_screen->registry, id, &zwp_linux_dmabuf_v1_interface, supported_version);
    xwl_screen->dmabuf_protocol_version = supported_version;
    zwp_linux_dmabuf_v1_add_listener(xwl_screen->dmabuf, &xwl_dmabuf_listener, xwl_screen);

    /* If the compositor supports it, request the default feedback hints */
    if (version >= 4) {
        xwl_screen->default_feedback.dmabuf_feedback =
            zwp_linux_dmabuf_v1_get_default_feedback(xwl_screen->dmabuf);
        if (!xwl_screen->default_feedback.dmabuf_feedback)
            return FALSE;

        zwp_linux_dmabuf_feedback_v1_add_listener(xwl_screen->default_feedback.dmabuf_feedback,
                                                  &xwl_dmabuf_feedback_listener,
                                                  &xwl_screen->default_feedback);
    }

    return TRUE;
}

static void
xwl_window_dmabuf_feedback_main_device(void *data,
                                       struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                       struct wl_array *dev)
{
    struct xwl_window *xwl_window = data;

    xwl_dmabuf_feedback_main_device(&xwl_window->feedback, dmabuf_feedback, dev);
}

static void
xwl_window_dmabuf_feedback_tranche_target_device(void *data,
                                                 struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                                 struct wl_array *dev)
{
    struct xwl_window *xwl_window = data;

    xwl_dmabuf_feedback_tranche_target_device(&xwl_window->feedback, dmabuf_feedback, dev);
}

static void
xwl_window_dmabuf_feedback_tranche_flags(void *data,
                                         struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                         uint32_t flags)
{
    struct xwl_window *xwl_window = data;

    xwl_dmabuf_feedback_tranche_flags(&xwl_window->feedback, dmabuf_feedback, flags);
}

static void
xwl_window_dmabuf_feedback_tranche_formats(void *data,
                                           struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                           struct wl_array *indices)
{
    struct xwl_window *xwl_window = data;

    xwl_dmabuf_feedback_tranche_formats(&xwl_window->feedback, dmabuf_feedback, indices);
}

static void
xwl_window_dmabuf_feedback_tranche_done(void *data,
                                        struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback)
{
    struct xwl_window *xwl_window = data;

    xwl_dmabuf_feedback_tranche_done(&xwl_window->feedback, dmabuf_feedback);
}

static void
xwl_window_dmabuf_feedback_done(void *data,
                                struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback)
{
    struct xwl_window *xwl_window = data;
    uint32_t format = wl_drm_format_for_depth(xwl_window->window->drawable.depth);

    xwl_dmabuf_feedback_done(&xwl_window->feedback, dmabuf_feedback);

    xwl_window->has_implicit_scanout_support =
        xwl_feedback_is_modifier_supported(&xwl_window->feedback, format,
                                           DRM_FORMAT_MOD_INVALID, TRUE);
    DebugF("XWAYLAND: Window 0x%x can%s get implicit scanout support\n",
            xwl_window->window->drawable.id,
            xwl_window->has_implicit_scanout_support ? "" : "not");

    /* If the linux-dmabuf v4 per-surface feedback changed, recycle the
     * window buffers so that they get re-created with appropriate parameters.
     */
    xwl_window_buffers_recycle(xwl_window);
}

static void
xwl_window_dmabuf_feedback_format_table(void *data,
                                        struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                        int32_t fd, uint32_t size)
{
    struct xwl_window *xwl_window = data;

    xwl_dmabuf_feedback_format_table(&xwl_window->feedback, dmabuf_feedback, fd, size);
}

static const struct zwp_linux_dmabuf_feedback_v1_listener xwl_window_dmabuf_feedback_listener = {
    .done = xwl_window_dmabuf_feedback_done,
    .format_table = xwl_window_dmabuf_feedback_format_table,
    .main_device = xwl_window_dmabuf_feedback_main_device,
    .tranche_done = xwl_window_dmabuf_feedback_tranche_done,
    .tranche_target_device = xwl_window_dmabuf_feedback_tranche_target_device,
    .tranche_formats = xwl_window_dmabuf_feedback_tranche_formats,
    .tranche_flags = xwl_window_dmabuf_feedback_tranche_flags,
};

Bool
xwl_dmabuf_setup_feedback_for_window(struct xwl_window *xwl_window)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;

    xwl_window->feedback.dmabuf_feedback =
        zwp_linux_dmabuf_v1_get_surface_feedback(xwl_screen->dmabuf, xwl_window->surface);

    if (!xwl_window->feedback.dmabuf_feedback)
        return FALSE;

    zwp_linux_dmabuf_feedback_v1_add_listener(xwl_window->feedback.dmabuf_feedback,
                                              &xwl_window_dmabuf_feedback_listener,
                                              xwl_window);

    return TRUE;
}

void
xwl_glamor_init_wl_registry(struct xwl_screen *xwl_screen,
                            struct wl_registry *registry,
                            uint32_t id, const char *interface,
                            uint32_t version)
{
    if (xwl_screen->gbm_backend.is_available &&
        xwl_screen->gbm_backend.init_wl_registry(xwl_screen,
                                                 registry,
                                                 id,
                                                 interface,
                                                 version)) {
        /* no-op */
    } else if (xwl_screen->eglstream_backend.is_available &&
               xwl_screen->eglstream_backend.init_wl_registry(xwl_screen,
                                                              registry,
                                                              id,
                                                              interface,
                                                              version)) {
        /* no-op */
    }
}

Bool
xwl_glamor_has_wl_interfaces(struct xwl_screen *xwl_screen,
                            struct xwl_egl_backend *xwl_egl_backend)
{
    return xwl_egl_backend->has_wl_interfaces(xwl_screen);
}

struct wl_buffer *
xwl_glamor_pixmap_get_wl_buffer(PixmapPtr pixmap)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(pixmap->drawable.pScreen);

    if (xwl_screen->egl_backend->get_wl_buffer_for_pixmap)
        return xwl_screen->egl_backend->get_wl_buffer_for_pixmap(pixmap);

    return NULL;
}

Bool
xwl_glamor_post_damage(struct xwl_window *xwl_window,
                       PixmapPtr pixmap, RegionPtr region)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;

    if (xwl_screen->egl_backend->post_damage)
        return xwl_screen->egl_backend->post_damage(xwl_window, pixmap, region);

    return TRUE;
}

Bool
xwl_glamor_allow_commits(struct xwl_window *xwl_window)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;

    if (xwl_screen->egl_backend->allow_commits)
        return xwl_screen->egl_backend->allow_commits(xwl_window);
    else
        return TRUE;
}

static void
xwl_avoid_implicit_redirect(WindowPtr window)
{
    ScreenPtr screen = window->drawable.pScreen;
    WindowOptPtr parent_optional;
    VisualPtr parent_visual = NULL;
    VisualPtr window_visual = NULL;
    DepthPtr depth32 = NULL;
    int i;

    if (!window->optional)
        return;

    parent_optional = FindWindowWithOptional(window)->optional;
    if (window->optional == parent_optional ||
        window->optional->visual == parent_optional->visual ||
        CompositeIsImplicitRedirectException(screen, parent_optional->visual,
                                             window->optional->visual))
        return;

    for (i = 0; i < screen->numDepths; i++) {
        if (screen->allowedDepths[i].depth == 32) {
            depth32 = &screen->allowedDepths[i];
            break;
        }
    }

    if (!depth32)
        return;

    for (i = 0; i < depth32->numVids; i++) {
        XID argb_vid = depth32->vids[i];

        if (argb_vid != parent_optional->visual)
            continue;

        if (!compIsAlternateVisual(screen, argb_vid))
            break;

        for (i = 0; i < screen->numVisuals; i++) {
            if (screen->visuals[i].vid == argb_vid) {
                parent_visual = &screen->visuals[i];
                break;
            }
        }
    }

    if (!parent_visual)
        return;

    for (i = 0; i < screen->numVisuals; i++) {
        if (screen->visuals[i].vid == window->optional->visual) {
            window_visual = &screen->visuals[i];
            break;
        }
    }

    if ((window_visual->class != TrueColor &&
         window_visual->class != DirectColor) ||
        window_visual->redMask != parent_visual->redMask ||
        window_visual->greenMask != parent_visual->greenMask ||
        window_visual->blueMask != parent_visual->blueMask ||
        window_visual->offsetRed != parent_visual->offsetRed ||
        window_visual->offsetGreen != parent_visual->offsetGreen ||
        window_visual->offsetBlue != parent_visual->offsetBlue)
        return;

    CompositeRegisterImplicitRedirectionException(screen, parent_visual->vid, window_visual->vid);
}

static Bool
xwl_glamor_create_window(WindowPtr window)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    Bool ret;

    if (window->parent)
        xwl_avoid_implicit_redirect(window);

    screen->CreateWindow = xwl_screen->CreateWindow;
    ret = (*screen->CreateWindow) (window);
    xwl_screen->CreateWindow = screen->CreateWindow;
    screen->CreateWindow = xwl_glamor_create_window;

    return ret;
}

static void
xwl_glamor_reparent_window(WindowPtr window, WindowPtr old_parent)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);

    xwl_avoid_implicit_redirect(window);

    screen->ReparentWindow = xwl_screen->ReparentWindow;
    (*screen->ReparentWindow) (window, old_parent);
    xwl_screen->ReparentWindow = screen->ReparentWindow;
    screen->ReparentWindow = xwl_glamor_reparent_window;
}

static Bool
xwl_glamor_create_screen_resources(ScreenPtr screen)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    int ret;

    screen->CreateScreenResources = xwl_screen->CreateScreenResources;
    ret = (*screen->CreateScreenResources) (screen);
    xwl_screen->CreateScreenResources = screen->CreateScreenResources;
    screen->CreateScreenResources = xwl_glamor_create_screen_resources;

    if (!ret)
        return ret;

    xwl_screen->CreateWindow = screen->CreateWindow;
    screen->CreateWindow = xwl_glamor_create_window;
    xwl_screen->ReparentWindow = screen->ReparentWindow;
    screen->ReparentWindow = xwl_glamor_reparent_window;

    if (xwl_screen->rootless) {
        screen->devPrivate =
            fbCreatePixmap(screen, 0, 0, screen->rootDepth, 0);
    }
    else {
        screen->devPrivate = screen->CreatePixmap(
            screen, screen->width, screen->height, screen->rootDepth,
            CREATE_PIXMAP_USAGE_BACKING_PIXMAP);
    }

    SetRootClip(screen, xwl_screen->root_clip_mode);

    return screen->devPrivate != NULL;
}

int
glamor_egl_fd_name_from_pixmap(ScreenPtr screen,
                               PixmapPtr pixmap,
                               CARD16 *stride, CARD32 *size)
{
    return 0;
}

Bool
xwl_glamor_needs_buffer_flush(struct xwl_screen *xwl_screen)
{
    if (!xwl_screen->glamor || !xwl_screen->egl_backend)
        return FALSE;

    return (xwl_screen->egl_backend->backend_flags &
                XWL_EGL_BACKEND_NEEDS_BUFFER_FLUSH);
}

Bool
xwl_glamor_needs_n_buffering(struct xwl_screen *xwl_screen)
{
    /* wl_shm benefits from n-buffering */
    if (!xwl_screen->glamor || !xwl_screen->egl_backend)
        return TRUE;

    return (xwl_screen->egl_backend->backend_flags &
                XWL_EGL_BACKEND_NEEDS_N_BUFFERING);
}

PixmapPtr
xwl_glamor_create_pixmap_for_window(struct xwl_window *xwl_window)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;

    if (!xwl_screen->glamor || !xwl_screen->egl_backend)
        return NullPixmap;

    if (xwl_screen->egl_backend->create_pixmap_for_window)
        return xwl_screen->egl_backend->create_pixmap_for_window(xwl_window);
    else
        return NullPixmap;
}

void
xwl_glamor_init_backends(struct xwl_screen *xwl_screen, Bool use_eglstream)
{
#ifdef GLAMOR_HAS_GBM
    xwl_glamor_init_gbm(xwl_screen);
    if (!xwl_screen->gbm_backend.is_available && !use_eglstream)
        ErrorF("Xwayland glamor: GBM backend (default) is not available\n");
#endif
#ifdef XWL_HAS_EGLSTREAM
    xwl_glamor_init_eglstream(xwl_screen);
    if (!xwl_screen->eglstream_backend.is_available && use_eglstream)
        ErrorF("Xwayland glamor: EGLStream backend requested but not available\n");
#endif
}

static Bool
xwl_glamor_select_gbm_backend(struct xwl_screen *xwl_screen)
{
#ifdef GLAMOR_HAS_GBM
    if (xwl_screen->gbm_backend.is_available &&
        xwl_glamor_has_wl_interfaces(xwl_screen, &xwl_screen->gbm_backend)) {
        xwl_screen->egl_backend = &xwl_screen->gbm_backend;
        LogMessageVerb(X_INFO, 3, "glamor: Using GBM backend\n");
        return TRUE;
    }
    else
        LogMessageVerb(X_INFO, 3,
                       "Missing Wayland requirements for glamor GBM backend\n");
#endif

    return FALSE;
}

static Bool
xwl_glamor_select_eglstream_backend(struct xwl_screen *xwl_screen)
{
#ifdef XWL_HAS_EGLSTREAM
    if (xwl_screen->eglstream_backend.is_available &&
        xwl_glamor_has_wl_interfaces(xwl_screen, &xwl_screen->eglstream_backend)) {
        xwl_screen->egl_backend = &xwl_screen->eglstream_backend;
        LogMessageVerb(X_INFO, 3, "glamor: Using EGLStream backend\n");
        return TRUE;
    }
    else
        LogMessageVerb(X_INFO, 3,
                       "Missing Wayland requirements for glamor EGLStream backend\n");
#endif

    return FALSE;
}

void
xwl_glamor_select_backend(struct xwl_screen *xwl_screen, Bool use_eglstream)
{
    if (!xwl_glamor_select_eglstream_backend(xwl_screen)) {
        if (!use_eglstream)
            xwl_glamor_select_gbm_backend(xwl_screen);
    }
}

Bool
xwl_glamor_init(struct xwl_screen *xwl_screen)
{
    ScreenPtr screen = xwl_screen->screen;
    const char *no_glamor_env;

    no_glamor_env = getenv("XWAYLAND_NO_GLAMOR");
    if (no_glamor_env && *no_glamor_env != '0') {
        ErrorF("Disabling glamor and dri3 support, XWAYLAND_NO_GLAMOR is set\n");
        return FALSE;
    }

    if (!xwl_screen->egl_backend->init_egl(xwl_screen)) {
        ErrorF("EGL setup failed, disabling glamor\n");
        return FALSE;
    }

    if (!glamor_init(xwl_screen->screen, GLAMOR_USE_EGL_SCREEN)) {
        ErrorF("Failed to initialize glamor\n");
        return FALSE;
    }

    if (!xwl_screen->egl_backend->init_screen(xwl_screen)) {
        ErrorF("EGL backend init_screen() failed, disabling glamor\n");
        return FALSE;
    }

    xwl_screen->CreateScreenResources = screen->CreateScreenResources;
    screen->CreateScreenResources = xwl_glamor_create_screen_resources;

#ifdef XV
    if (!xwl_glamor_xv_init(screen))
        ErrorF("Failed to initialize glamor Xv extension\n");
#endif

#ifdef GLXEXT
    GlxPushProvider(&glamor_provider);
#endif

    return TRUE;
}
