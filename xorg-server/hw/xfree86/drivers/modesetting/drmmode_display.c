/*
 * Copyright © 2007 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Dave Airlie <airlied@redhat.com>
 *
 */

#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "dumb_bo.h"
#include "xf86str.h"
#include "X11/Xatom.h"
#include "micmap.h"
#include "xf86cmap.h"
#include "xf86DDC.h"

#include <xf86drm.h>
#include "xf86Crtc.h"
#include "drmmode_display.h"

#include <cursorstr.h>

#include <X11/extensions/dpmsconst.h>

#include "driver.h"

static Bool drmmode_xf86crtc_resize(ScrnInfoPtr scrn, int width, int height);
static PixmapPtr drmmode_create_pixmap_header(ScreenPtr pScreen, int width, int height,
                                              int depth, int bitsPerPixel, int devKind,
                                              void *pPixData);
static Bool
drmmode_zaphod_string_matches(ScrnInfoPtr scrn, const char *s, char *output_name)
{
    int i = 0;
    char s1[20];

    do {
        switch(*s) {
        case ',':
            s1[i] = '\0';
            i = 0;
            if (strcmp(s1, output_name) == 0)
                return TRUE;
            break;
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            break;
        default:
            s1[i] = *s;
            i++;
            break;
        }
    } while(*s++);

    s1[i] = '\0';
    if (strcmp(s1, output_name) == 0)
        return TRUE;

    return FALSE;
}

int
drmmode_bo_destroy(drmmode_ptr drmmode, drmmode_bo *bo)
{
    int ret;

#ifdef GLAMOR_HAS_GBM
    if (bo->gbm) {
        gbm_bo_destroy(bo->gbm);
        bo->gbm = NULL;
    }
#endif

    if (bo->dumb) {
        ret = dumb_bo_destroy(drmmode->fd, bo->dumb);
        if (ret == 0)
            bo->dumb = NULL;
    }

    return 0;
}

uint32_t
drmmode_bo_get_pitch(drmmode_bo *bo)
{
#ifdef GLAMOR_HAS_GBM
    if (bo->gbm)
        return gbm_bo_get_stride(bo->gbm);
#endif

    return bo->dumb->pitch;
}

static Bool
drmmode_bo_has_bo(drmmode_bo *bo)
{
#ifdef GLAMOR_HAS_GBM
    if (bo->gbm)
        return TRUE;
#endif

    return bo->dumb != NULL;
}

uint32_t
drmmode_bo_get_handle(drmmode_bo *bo)
{
#ifdef GLAMOR_HAS_GBM
    if (bo->gbm)
        return gbm_bo_get_handle(bo->gbm).u32;
#endif

    return bo->dumb->handle;
}

static void *
drmmode_bo_map(drmmode_ptr drmmode, drmmode_bo *bo)
{
    int ret;

#ifdef GLAMOR_HAS_GBM
    if (bo->gbm)
        return NULL;
#endif

    if (bo->dumb->ptr)
        return bo->dumb->ptr;

    ret = dumb_bo_map(drmmode->fd, bo->dumb);
    if (ret)
        return NULL;

    return bo->dumb->ptr;
}

static Bool
drmmode_create_bo(drmmode_ptr drmmode, drmmode_bo *bo,
                  unsigned width, unsigned height, unsigned bpp)
{
#ifdef GLAMOR_HAS_GBM
    if (drmmode->glamor) {
        bo->gbm = gbm_bo_create(drmmode->gbm, width, height,
                                GBM_FORMAT_ARGB8888,
                                GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
        return bo->gbm != NULL;
    }
#endif

    bo->dumb = dumb_bo_create(drmmode->fd, width, height, bpp);
    return bo->dumb != NULL;
}

Bool
drmmode_SetSlaveBO(PixmapPtr ppix,
                   drmmode_ptr drmmode, int fd_handle, int pitch, int size)
{
    msPixmapPrivPtr ppriv = msGetPixmapPriv(drmmode, ppix);

    if (fd_handle == -1) {
        dumb_bo_destroy(drmmode->fd, ppriv->backing_bo);
        ppriv->backing_bo = NULL;
        return TRUE;
    }

    ppriv->backing_bo =
        dumb_get_bo_from_fd(drmmode->fd, fd_handle, pitch, size);
    if (!ppriv->backing_bo)
        return FALSE;

    close(fd_handle);
    return TRUE;
}

static Bool
drmmode_SharedPixmapPresent(PixmapPtr ppix, xf86CrtcPtr crtc,
                            drmmode_ptr drmmode)
{
    ScreenPtr master = crtc->randr_crtc->pScreen->current_master;

    if (master->PresentSharedPixmap(ppix)) {
        /* Success, queue flip to back target */
        if (drmmode_SharedPixmapFlip(ppix, crtc, drmmode))
            return TRUE;

        xf86DrvMsg(drmmode->scrn->scrnIndex, X_WARNING,
                   "drmmode_SharedPixmapFlip() failed, trying again next vblank\n");

        return drmmode_SharedPixmapPresentOnVBlank(ppix, crtc, drmmode);
    }

    /* Failed to present, try again on next vblank after damage */
    if (master->RequestSharedPixmapNotifyDamage) {
        msPixmapPrivPtr ppriv = msGetPixmapPriv(drmmode, ppix);

        /* Set flag first in case we are immediately notified */
        ppriv->wait_for_damage = TRUE;

        if (master->RequestSharedPixmapNotifyDamage(ppix))
            return TRUE;
        else
            ppriv->wait_for_damage = FALSE;
    }

    /* Damage notification not available, just try again on vblank */
    return drmmode_SharedPixmapPresentOnVBlank(ppix, crtc, drmmode);
}

struct vblank_event_args {
    PixmapPtr frontTarget;
    PixmapPtr backTarget;
    xf86CrtcPtr crtc;
    drmmode_ptr drmmode;
    Bool flip;
};
static void
drmmode_SharedPixmapVBlankEventHandler(uint64_t frame, uint64_t usec,
                                       void *data)
{
    struct vblank_event_args *args = data;

    drmmode_crtc_private_ptr drmmode_crtc = args->crtc->driver_private;

    if (args->flip) {
        /* frontTarget is being displayed, update crtc to reflect */
        drmmode_crtc->prime_pixmap = args->frontTarget;
        drmmode_crtc->prime_pixmap_back = args->backTarget;

        /* Safe to present on backTarget, no longer displayed */
        drmmode_SharedPixmapPresent(args->backTarget, args->crtc, args->drmmode);
    } else {
        /* backTarget is still being displayed, present on frontTarget */
        drmmode_SharedPixmapPresent(args->frontTarget, args->crtc, args->drmmode);
    }

    free(args);
}

static void
drmmode_SharedPixmapVBlankEventAbort(void *data)
{
    struct vblank_event_args *args = data;

    msGetPixmapPriv(args->drmmode, args->frontTarget)->flip_seq = 0;

    free(args);
}

Bool
drmmode_SharedPixmapPresentOnVBlank(PixmapPtr ppix, xf86CrtcPtr crtc,
                                    drmmode_ptr drmmode)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    msPixmapPrivPtr ppriv = msGetPixmapPriv(drmmode, ppix);

    drmVBlank vbl;
    struct vblank_event_args *event_args;

    if (ppix == drmmode_crtc->prime_pixmap)
        return FALSE; /* Already flipped to this pixmap */
    if (ppix != drmmode_crtc->prime_pixmap_back)
        return FALSE; /* Pixmap is not a scanout pixmap for CRTC */

    event_args = calloc(1, sizeof(*event_args));
    if (!event_args)
        return FALSE;

    event_args->frontTarget = ppix;
    event_args->backTarget = drmmode_crtc->prime_pixmap;
    event_args->crtc = crtc;
    event_args->drmmode = drmmode;
    event_args->flip = FALSE;

    ppriv->flip_seq =
        ms_drm_queue_alloc(crtc, event_args,
                           drmmode_SharedPixmapVBlankEventHandler,
                           drmmode_SharedPixmapVBlankEventAbort);

    vbl.request.type =
        DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT | drmmode_crtc->vblank_pipe;
    vbl.request.sequence = 1;
    vbl.request.signal = (unsigned long) ppriv->flip_seq;

    return drmWaitVBlank(drmmode->fd, &vbl) >= 0;
}

Bool
drmmode_SharedPixmapFlip(PixmapPtr frontTarget, xf86CrtcPtr crtc,
                         drmmode_ptr drmmode)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    msPixmapPrivPtr ppriv_front = msGetPixmapPriv(drmmode, frontTarget);

    struct vblank_event_args *event_args;

    event_args = calloc(1, sizeof(*event_args));
    if (!event_args)
        return FALSE;

    event_args->frontTarget = frontTarget;
    event_args->backTarget = drmmode_crtc->prime_pixmap;
    event_args->crtc = crtc;
    event_args->drmmode = drmmode;
    event_args->flip = TRUE;

    ppriv_front->flip_seq =
        ms_drm_queue_alloc(crtc, event_args,
                           drmmode_SharedPixmapVBlankEventHandler,
                           drmmode_SharedPixmapVBlankEventAbort);

    if (drmModePageFlip(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                        ppriv_front->fb_id, DRM_MODE_PAGE_FLIP_EVENT,
                        (void *)(intptr_t) ppriv_front->flip_seq) < 0) {
        ms_drm_abort_seq(crtc->scrn, ppriv_front->flip_seq);
        return FALSE;
    }

    return TRUE;
}

static Bool
drmmode_InitSharedPixmapFlipping(xf86CrtcPtr crtc, drmmode_ptr drmmode)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

    if (!drmmode_crtc->enable_flipping)
        return FALSE;

    if (drmmode_crtc->flipping_active)
        return TRUE;

    drmmode_crtc->flipping_active =
        drmmode_SharedPixmapPresent(drmmode_crtc->prime_pixmap_back,
                                    crtc, drmmode);

    return drmmode_crtc->flipping_active;
}

static void
drmmode_FiniSharedPixmapFlipping(xf86CrtcPtr crtc, drmmode_ptr drmmode)
{
    uint32_t seq;
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

    if (!drmmode_crtc->flipping_active)
        return;

    drmmode_crtc->flipping_active = FALSE;

    /* Abort page flip event handler on prime_pixmap */
    seq = msGetPixmapPriv(drmmode, drmmode_crtc->prime_pixmap)->flip_seq;
    if (seq)
        ms_drm_abort_seq(crtc->scrn, seq);

    /* Abort page flip event handler on prime_pixmap_back */
    seq = msGetPixmapPriv(drmmode,
                          drmmode_crtc->prime_pixmap_back)->flip_seq;
    if (seq)
        ms_drm_abort_seq(crtc->scrn, seq);
}

static Bool drmmode_set_target_scanout_pixmap(xf86CrtcPtr crtc, PixmapPtr ppix,
                                              PixmapPtr *target);

Bool
drmmode_EnableSharedPixmapFlipping(xf86CrtcPtr crtc, drmmode_ptr drmmode,
                                   PixmapPtr front, PixmapPtr back)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

    drmmode_crtc->enable_flipping = TRUE;

    /* Set front scanout pixmap */
    drmmode_crtc->enable_flipping &=
        drmmode_set_target_scanout_pixmap(crtc, front,
                                          &drmmode_crtc->prime_pixmap);
    if (!drmmode_crtc->enable_flipping)
        return FALSE;

    /* Set back scanout pixmap */
    drmmode_crtc->enable_flipping &=
        drmmode_set_target_scanout_pixmap(crtc, back,
                                          &drmmode_crtc->prime_pixmap_back);
    if (!drmmode_crtc->enable_flipping) {
        drmmode_set_target_scanout_pixmap(crtc, NULL,
                                          &drmmode_crtc->prime_pixmap);
        return FALSE;
    }

    return TRUE;
}

void
drmmode_DisableSharedPixmapFlipping(xf86CrtcPtr crtc, drmmode_ptr drmmode)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

    drmmode_crtc->enable_flipping = FALSE;

    drmmode_FiniSharedPixmapFlipping(crtc, drmmode);

    drmmode_set_target_scanout_pixmap(crtc, NULL, &drmmode_crtc->prime_pixmap);

    drmmode_set_target_scanout_pixmap(crtc, NULL,
                                      &drmmode_crtc->prime_pixmap_back);
}

static void
drmmode_ConvertFromKMode(ScrnInfoPtr scrn,
                         drmModeModeInfo * kmode, DisplayModePtr mode)
{
    memset(mode, 0, sizeof(DisplayModeRec));
    mode->status = MODE_OK;

    mode->Clock = kmode->clock;

    mode->HDisplay = kmode->hdisplay;
    mode->HSyncStart = kmode->hsync_start;
    mode->HSyncEnd = kmode->hsync_end;
    mode->HTotal = kmode->htotal;
    mode->HSkew = kmode->hskew;

    mode->VDisplay = kmode->vdisplay;
    mode->VSyncStart = kmode->vsync_start;
    mode->VSyncEnd = kmode->vsync_end;
    mode->VTotal = kmode->vtotal;
    mode->VScan = kmode->vscan;

    mode->Flags = kmode->flags; //& FLAG_BITS;
    mode->name = strdup(kmode->name);

    if (kmode->type & DRM_MODE_TYPE_DRIVER)
        mode->type = M_T_DRIVER;
    if (kmode->type & DRM_MODE_TYPE_PREFERRED)
        mode->type |= M_T_PREFERRED;
    xf86SetModeCrtc(mode, scrn->adjustFlags);
}

static void
drmmode_ConvertToKMode(ScrnInfoPtr scrn,
                       drmModeModeInfo * kmode, DisplayModePtr mode)
{
    memset(kmode, 0, sizeof(*kmode));

    kmode->clock = mode->Clock;
    kmode->hdisplay = mode->HDisplay;
    kmode->hsync_start = mode->HSyncStart;
    kmode->hsync_end = mode->HSyncEnd;
    kmode->htotal = mode->HTotal;
    kmode->hskew = mode->HSkew;

    kmode->vdisplay = mode->VDisplay;
    kmode->vsync_start = mode->VSyncStart;
    kmode->vsync_end = mode->VSyncEnd;
    kmode->vtotal = mode->VTotal;
    kmode->vscan = mode->VScan;

    kmode->flags = mode->Flags; //& FLAG_BITS;
    if (mode->name)
        strncpy(kmode->name, mode->name, DRM_DISPLAY_MODE_LEN);
    kmode->name[DRM_DISPLAY_MODE_LEN - 1] = 0;

}

static void
drmmode_crtc_dpms(xf86CrtcPtr crtc, int mode)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_crtc->dpms_mode = mode;
}

#ifdef GLAMOR
static PixmapPtr
create_pixmap_for_fbcon(drmmode_ptr drmmode, ScrnInfoPtr pScrn, int fbcon_id)
{
    PixmapPtr pixmap = drmmode->fbcon_pixmap;
    drmModeFBPtr fbcon;
    struct drm_gem_flink flink;
    ScreenPtr pScreen = xf86ScrnToScreen(pScrn);
    Bool ret;

    if (pixmap)
        return pixmap;

    fbcon = drmModeGetFB(drmmode->fd, fbcon_id);
    if (fbcon == NULL)
        return NULL;

    if (fbcon->depth != pScrn->depth ||
        fbcon->width != pScrn->virtualX ||
        fbcon->height != pScrn->virtualY)
        goto out_free_fb;

    flink.handle = fbcon->handle;
    if (ioctl(drmmode->fd, DRM_IOCTL_GEM_FLINK, &flink) < 0) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Couldn't flink fbcon handle\n");
        goto out_free_fb;
    }

    pixmap = drmmode_create_pixmap_header(pScreen, fbcon->width,
                                          fbcon->height, fbcon->depth,
                                          fbcon->bpp, fbcon->pitch, NULL);
    if (!pixmap)
        goto out_free_fb;

    ret = glamor_egl_create_textured_pixmap(pixmap, fbcon->handle, fbcon->pitch);
    if (!ret) {
      FreePixmap(pixmap);
      pixmap = NULL;
    }

    drmmode->fbcon_pixmap = pixmap;
out_free_fb:
    drmModeFreeFB(fbcon);
    return pixmap;
}
#endif

void
drmmode_copy_fb(ScrnInfoPtr pScrn, drmmode_ptr drmmode)
{
#ifdef GLAMOR
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    ScreenPtr pScreen = xf86ScrnToScreen(pScrn);
    PixmapPtr src, dst;
    int fbcon_id = 0;
    GCPtr gc;
    int i;

    for (i = 0; i < xf86_config->num_crtc; i++) {
        drmmode_crtc_private_ptr drmmode_crtc = xf86_config->crtc[i]->driver_private;
        if (drmmode_crtc->mode_crtc->buffer_id)
            fbcon_id = drmmode_crtc->mode_crtc->buffer_id;
    }

    if (!fbcon_id)
        return;

    if (fbcon_id == drmmode->fb_id) {
        /* in some rare case there might be no fbcon and we might already
         * be the one with the current fb to avoid a false deadlck in
         * kernel ttm code just do nothing as anyway there is nothing
         * to do
         */
        return;
    }

    src = create_pixmap_for_fbcon(drmmode, pScrn, fbcon_id);
    if (!src)
        return;

    dst = pScreen->GetScreenPixmap(pScreen);

    gc = GetScratchGC(pScrn->depth, pScreen);
    ValidateGC(&dst->drawable, gc);

    (*gc->ops->CopyArea)(&src->drawable, &dst->drawable, gc, 0, 0,
                         pScrn->virtualX, pScrn->virtualY, 0, 0);

    FreeScratchGC(gc);

    glamor_finish(pScreen);

    pScreen->canDoBGNoneRoot = TRUE;

    if (drmmode->fbcon_pixmap)
        pScrn->pScreen->DestroyPixmap(drmmode->fbcon_pixmap);
    drmmode->fbcon_pixmap = NULL;
#endif
}

static Bool
drmmode_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode,
                       Rotation rotation, int x, int y)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    int saved_x, saved_y;
    Rotation saved_rotation;
    DisplayModeRec saved_mode;
    uint32_t *output_ids = NULL;
    int output_count = 0;
    Bool ret = TRUE;
    int i;
    uint32_t fb_id = 0;
    drmModeModeInfo kmode;

    saved_mode = crtc->mode;
    saved_x = crtc->x;
    saved_y = crtc->y;
    saved_rotation = crtc->rotation;

    if (mode) {
        crtc->mode = *mode;
        crtc->x = x;
        crtc->y = y;
        crtc->rotation = rotation;
    }

    output_ids = calloc(sizeof(uint32_t), xf86_config->num_output);
    if (!output_ids) {
        ret = FALSE;
        goto done;
    }

    if (mode) {
        for (i = 0; i < xf86_config->num_output; i++) {
            xf86OutputPtr output = xf86_config->output[i];
            drmmode_output_private_ptr drmmode_output;

            if (output->crtc != crtc)
                continue;

            drmmode_output = output->driver_private;
            if (drmmode_output->output_id == -1)
                continue;
            output_ids[output_count] =
                drmmode_output->mode_output->connector_id;
            output_count++;
        }

        if (!xf86CrtcRotate(crtc)) {
            goto done;
        }
        crtc->funcs->gamma_set(crtc, crtc->gamma_red, crtc->gamma_green,
                               crtc->gamma_blue, crtc->gamma_size);

        drmmode_ConvertToKMode(crtc->scrn, &kmode, mode);

        fb_id = drmmode->fb_id;
        if (drmmode_crtc->prime_pixmap) {
            if (!drmmode->reverse_prime_offload_mode) {
                msPixmapPrivPtr ppriv =
                    msGetPixmapPriv(drmmode, drmmode_crtc->prime_pixmap);
                fb_id = ppriv->fb_id;
                x = 0;
            } else
                x = drmmode_crtc->prime_pixmap_x;
            y = 0;
        }
        else if (drmmode_crtc->rotate_fb_id) {
            fb_id = drmmode_crtc->rotate_fb_id;
            x = y = 0;
        }

        if (fb_id == 0) {
            ret = drmModeAddFB(drmmode->fd,
                               pScrn->virtualX, pScrn->virtualY,
                               pScrn->depth, drmmode->kbpp,
                               drmmode_bo_get_pitch(&drmmode->front_bo),
                               drmmode_bo_get_handle(&drmmode->front_bo),
                               &drmmode->fb_id);
            if (ret < 0) {
                ErrorF("failed to add fb %d\n", ret);
                ret = FALSE;
                goto done;
            }
            fb_id = drmmode->fb_id;
        }

        if (drmModeSetCrtc(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                           fb_id, x, y, output_ids, output_count, &kmode)) {
            xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
                       "failed to set mode: %s\n", strerror(errno));
            ret = FALSE;
            goto done;
        } else
            ret = TRUE;

        if (crtc->scrn->pScreen)
            xf86CrtcSetScreenSubpixelOrder(crtc->scrn->pScreen);

        drmmode_crtc->need_modeset = FALSE;
        crtc->funcs->dpms(crtc, DPMSModeOn);

        if (drmmode_crtc->prime_pixmap_back)
            drmmode_InitSharedPixmapFlipping(crtc, drmmode);

        /* go through all the outputs and force DPMS them back on? */
        for (i = 0; i < xf86_config->num_output; i++) {
            xf86OutputPtr output = xf86_config->output[i];
            drmmode_output_private_ptr drmmode_output;

            if (output->crtc != crtc)
                continue;

            drmmode_output = output->driver_private;
            if (drmmode_output->output_id == -1)
                continue;
            output->funcs->dpms(output, DPMSModeOn);
        }
    }

 done:
    if (!ret) {
        crtc->x = saved_x;
        crtc->y = saved_y;
        crtc->rotation = saved_rotation;
        crtc->mode = saved_mode;
    } else
        crtc->active = TRUE;

    free(output_ids);

    return ret;
}

static void
drmmode_set_cursor_colors(xf86CrtcPtr crtc, int bg, int fg)
{

}

static void
drmmode_set_cursor_position(xf86CrtcPtr crtc, int x, int y)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;

    drmModeMoveCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id, x, y);
}

static Bool
drmmode_set_cursor(xf86CrtcPtr crtc)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    uint32_t handle = drmmode_crtc->cursor_bo->handle;
    modesettingPtr ms = modesettingPTR(crtc->scrn);
    int ret = -EINVAL;

    if (!drmmode_crtc->set_cursor2_failed) {
        CursorPtr cursor = xf86CurrentCursor(crtc->scrn->pScreen);

        ret =
            drmModeSetCursor2(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                              handle, ms->cursor_width, ms->cursor_height,
                              cursor->bits->xhot, cursor->bits->yhot);
        if (!ret)
            return TRUE;

        /* -EINVAL can mean that an old kernel supports drmModeSetCursor but
         * not drmModeSetCursor2, though it can mean other things too. */
        if (ret == -EINVAL)
            drmmode_crtc->set_cursor2_failed = TRUE;
    }

    if (ret == -EINVAL)
        ret = drmModeSetCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                               handle, ms->cursor_width, ms->cursor_height);

    if (ret) {
        xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
        xf86CursorInfoPtr cursor_info = xf86_config->cursor_info;

        cursor_info->MaxWidth = cursor_info->MaxHeight = 0;
        drmmode_crtc->drmmode->sw_cursor = TRUE;
        /* fallback to swcursor */
        return FALSE;
    }
    return TRUE;
}

static void drmmode_hide_cursor(xf86CrtcPtr crtc);

/*
 * The load_cursor_argb_check driver hook.
 *
 * Sets the hardware cursor by calling the drmModeSetCursor2 ioctl.
 * On failure, returns FALSE indicating that the X server should fall
 * back to software cursors.
 */
static Bool
drmmode_load_cursor_argb_check(xf86CrtcPtr crtc, CARD32 *image)
{
    modesettingPtr ms = modesettingPTR(crtc->scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    int i;
    uint32_t *ptr;

    /* cursor should be mapped already */
    ptr = (uint32_t *) (drmmode_crtc->cursor_bo->ptr);

    for (i = 0; i < ms->cursor_width * ms->cursor_height; i++)
        ptr[i] = image[i];      // cpu_to_le32(image[i]);

    if (drmmode_crtc->cursor_up || !drmmode_crtc->first_cursor_load_done) {
        Bool ret = drmmode_set_cursor(crtc);
        if (!drmmode_crtc->cursor_up)
            drmmode_hide_cursor(crtc);
        drmmode_crtc->first_cursor_load_done = TRUE;
        return ret;
    }
    return TRUE;
}

static void
drmmode_hide_cursor(xf86CrtcPtr crtc)
{
    modesettingPtr ms = modesettingPTR(crtc->scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;

    drmmode_crtc->cursor_up = FALSE;
    drmModeSetCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id, 0,
                     ms->cursor_width, ms->cursor_height);
}

static void
drmmode_show_cursor(xf86CrtcPtr crtc)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_crtc->cursor_up = TRUE;
    drmmode_set_cursor(crtc);
}

static void
drmmode_crtc_gamma_set(xf86CrtcPtr crtc, uint16_t * red, uint16_t * green,
                       uint16_t * blue, int size)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;

    drmModeCrtcSetGamma(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                        size, red, green, blue);
}

static Bool
drmmode_set_target_scanout_pixmap_gpu(xf86CrtcPtr crtc, PixmapPtr ppix,
                                      PixmapPtr *target)
{
    ScreenPtr screen = xf86ScrnToScreen(crtc->scrn);
    PixmapPtr screenpix = screen->GetScreenPixmap(screen);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    int c, total_width = 0, max_height = 0, this_x = 0;

    if (*target) {
        PixmapStopDirtyTracking(*target, screenpix);
        if (drmmode->fb_id) {
            drmModeRmFB(drmmode->fd, drmmode->fb_id);
            drmmode->fb_id = 0;
        }
        drmmode_crtc->prime_pixmap_x = 0;
        *target = NULL;
    }

    if (!ppix)
        return TRUE;

    /* iterate over all the attached crtcs to work out the bounding box */
    for (c = 0; c < xf86_config->num_crtc; c++) {
        xf86CrtcPtr iter = xf86_config->crtc[c];
        if (!iter->enabled && iter != crtc)
            continue;
        if (iter == crtc) {
            this_x = total_width;
            total_width += ppix->drawable.width;
            if (max_height < ppix->drawable.height)
                max_height = ppix->drawable.height;
        } else {
            total_width += iter->mode.HDisplay;
            if (max_height < iter->mode.VDisplay)
                max_height = iter->mode.VDisplay;
        }
    }

    if (total_width != screenpix->drawable.width ||
        max_height != screenpix->drawable.height) {

        if (!drmmode_xf86crtc_resize(crtc->scrn, total_width, max_height))
            return FALSE;

        screenpix = screen->GetScreenPixmap(screen);
        screen->width = screenpix->drawable.width = total_width;
        screen->height = screenpix->drawable.height = max_height;
    }
    drmmode_crtc->prime_pixmap_x = this_x;
    PixmapStartDirtyTracking(ppix, screenpix, 0, 0, this_x, 0, RR_Rotate_0);
    *target = ppix;
    return TRUE;
}

static Bool
drmmode_set_target_scanout_pixmap_cpu(xf86CrtcPtr crtc, PixmapPtr ppix,
                                      PixmapPtr *target)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    msPixmapPrivPtr ppriv;
    void *ptr;

    if (*target) {
        ppriv = msGetPixmapPriv(drmmode, *target);
        drmModeRmFB(drmmode->fd, ppriv->fb_id);
        ppriv->fb_id = 0;
        if (ppriv->slave_damage) {
            DamageUnregister(ppriv->slave_damage);
            ppriv->slave_damage = NULL;
        }
        *target = NULL;
    }

    if (!ppix)
        return TRUE;

    ppriv = msGetPixmapPriv(drmmode, ppix);
    if (!ppriv->slave_damage) {
        ppriv->slave_damage = DamageCreate(NULL, NULL,
                                           DamageReportNone,
                                           TRUE,
                                           crtc->randr_crtc->pScreen,
                                           NULL);
    }
    ptr = drmmode_map_slave_bo(drmmode, ppriv);
    ppix->devPrivate.ptr = ptr;
    DamageRegister(&ppix->drawable, ppriv->slave_damage);

    if (ppriv->fb_id == 0) {
        drmModeAddFB(drmmode->fd, ppix->drawable.width,
                     ppix->drawable.height,
                     ppix->drawable.depth,
                     ppix->drawable.bitsPerPixel,
                     ppix->devKind, ppriv->backing_bo->handle, &ppriv->fb_id);
    }
    *target = ppix;
    return TRUE;
}

static Bool
drmmode_set_target_scanout_pixmap(xf86CrtcPtr crtc, PixmapPtr ppix,
                                  PixmapPtr *target)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;

    if (drmmode->reverse_prime_offload_mode)
        return drmmode_set_target_scanout_pixmap_gpu(crtc, ppix, target);
    else
        return drmmode_set_target_scanout_pixmap_cpu(crtc, ppix, target);
}

static Bool
drmmode_set_scanout_pixmap(xf86CrtcPtr crtc, PixmapPtr ppix)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

    /* Use DisableSharedPixmapFlipping before switching to single buf */
    if (drmmode_crtc->enable_flipping)
        return FALSE;

    return drmmode_set_target_scanout_pixmap(crtc, ppix,
                                             &drmmode_crtc->prime_pixmap);
}

static void *
drmmode_shadow_allocate(xf86CrtcPtr crtc, int width, int height)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    int ret;

    if (!drmmode_create_bo(drmmode, &drmmode_crtc->rotate_bo,
                           width, height, drmmode->kbpp)) {
        xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
               "Couldn't allocate shadow memory for rotated CRTC\n");
        return NULL;
    }

    ret = drmModeAddFB(drmmode->fd, width, height, crtc->scrn->depth,
                       drmmode->kbpp,
                       drmmode_bo_get_pitch(&drmmode_crtc->rotate_bo),
                       drmmode_bo_get_handle(&drmmode_crtc->rotate_bo),
                       &drmmode_crtc->rotate_fb_id);

    if (ret) {
        ErrorF("failed to add rotate fb\n");
        drmmode_bo_destroy(drmmode, &drmmode_crtc->rotate_bo);
        return NULL;
    }

#ifdef GLAMOR_HAS_GBM
    if (drmmode->gbm)
        return drmmode_crtc->rotate_bo.gbm;
#endif
    return drmmode_crtc->rotate_bo.dumb;
}

static PixmapPtr
drmmode_create_pixmap_header(ScreenPtr pScreen, int width, int height,
                             int depth, int bitsPerPixel, int devKind,
                             void *pPixData)
{
    PixmapPtr pixmap;

    /* width and height of 0 means don't allocate any pixmap data */
    pixmap = (*pScreen->CreatePixmap)(pScreen, 0, 0, depth, 0);

    if (pixmap) {
        if ((*pScreen->ModifyPixmapHeader)(pixmap, width, height, depth,
                                           bitsPerPixel, devKind, pPixData))
            return pixmap;
        (*pScreen->DestroyPixmap)(pixmap);
    }
    return NullPixmap;
}

static Bool
drmmode_set_pixmap_bo(drmmode_ptr drmmode, PixmapPtr pixmap, drmmode_bo *bo);

static PixmapPtr
drmmode_shadow_create(xf86CrtcPtr crtc, void *data, int width, int height)
{
    ScrnInfoPtr scrn = crtc->scrn;
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    uint32_t rotate_pitch;
    PixmapPtr rotate_pixmap;
    void *pPixData = NULL;

    if (!data) {
        data = drmmode_shadow_allocate(crtc, width, height);
        if (!data) {
            xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                       "Couldn't allocate shadow pixmap for rotated CRTC\n");
            return NULL;
        }
    }

    if (!drmmode_bo_has_bo(&drmmode_crtc->rotate_bo)) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "Couldn't allocate shadow pixmap for rotated CRTC\n");
        return NULL;
    }

    pPixData = drmmode_bo_map(drmmode, &drmmode_crtc->rotate_bo);
    rotate_pitch = drmmode_bo_get_pitch(&drmmode_crtc->rotate_bo),

    rotate_pixmap = drmmode_create_pixmap_header(scrn->pScreen,
                                                 width, height,
                                                 scrn->depth,
                                                 drmmode->kbpp,
                                                 rotate_pitch,
                                                 pPixData);

    if (rotate_pixmap == NULL) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "Couldn't allocate shadow pixmap for rotated CRTC\n");
        return NULL;
    }

    drmmode_set_pixmap_bo(drmmode, rotate_pixmap, &drmmode_crtc->rotate_bo);

    return rotate_pixmap;
}

static void
drmmode_shadow_destroy(xf86CrtcPtr crtc, PixmapPtr rotate_pixmap, void *data)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;

    if (rotate_pixmap) {
        rotate_pixmap->drawable.pScreen->DestroyPixmap(rotate_pixmap);
    }

    if (data) {
        drmModeRmFB(drmmode->fd, drmmode_crtc->rotate_fb_id);
        drmmode_crtc->rotate_fb_id = 0;

        drmmode_bo_destroy(drmmode, &drmmode_crtc->rotate_bo);
        memset(&drmmode_crtc->rotate_bo, 0, sizeof drmmode_crtc->rotate_bo);
    }
}

static const xf86CrtcFuncsRec drmmode_crtc_funcs = {
    .dpms = drmmode_crtc_dpms,
    .set_mode_major = drmmode_set_mode_major,
    .set_cursor_colors = drmmode_set_cursor_colors,
    .set_cursor_position = drmmode_set_cursor_position,
    .show_cursor = drmmode_show_cursor,
    .hide_cursor = drmmode_hide_cursor,
    .load_cursor_argb_check = drmmode_load_cursor_argb_check,

    .gamma_set = drmmode_crtc_gamma_set,
    .destroy = NULL,            /* XXX */
    .set_scanout_pixmap = drmmode_set_scanout_pixmap,
    .shadow_allocate = drmmode_shadow_allocate,
    .shadow_create = drmmode_shadow_create,
    .shadow_destroy = drmmode_shadow_destroy,
};

static uint32_t
drmmode_crtc_vblank_pipe(int crtc_id)
{
    if (crtc_id > 1)
        return crtc_id << DRM_VBLANK_HIGH_CRTC_SHIFT;
    else if (crtc_id > 0)
        return DRM_VBLANK_SECONDARY;
    else
        return 0;
}

static unsigned int
drmmode_crtc_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode, drmModeResPtr mode_res, int num)
{
    xf86CrtcPtr crtc;
    drmmode_crtc_private_ptr drmmode_crtc;
    modesettingEntPtr ms_ent = ms_ent_priv(pScrn);

    crtc = xf86CrtcCreate(pScrn, &drmmode_crtc_funcs);
    if (crtc == NULL)
        return 0;

    drmmode_crtc = xnfcalloc(sizeof(drmmode_crtc_private_rec), 1);
    drmmode_crtc->mode_crtc =
        drmModeGetCrtc(drmmode->fd, mode_res->crtcs[num]);
    drmmode_crtc->drmmode = drmmode;
    drmmode_crtc->vblank_pipe = drmmode_crtc_vblank_pipe(num);
    crtc->driver_private = drmmode_crtc;

    /* Hide any cursors which may be active from previous users */
    drmModeSetCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id, 0, 0, 0);

    /* Mark num'th crtc as in use on this device. */
    ms_ent->assigned_crtcs |= (1 << num);
    xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, MS_LOGLEVEL_DEBUG,
                   "Allocated crtc nr. %d to this screen.\n", num);

    return 1;
}

static xf86OutputStatus
drmmode_output_detect(xf86OutputPtr output)
{
    /* go to the hw and retrieve a new output struct */
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    xf86OutputStatus status;

    if (drmmode_output->output_id == -1)
        return XF86OutputStatusDisconnected;

    drmModeFreeConnector(drmmode_output->mode_output);

    drmmode_output->mode_output =
        drmModeGetConnector(drmmode->fd, drmmode_output->output_id);
    if (!drmmode_output->mode_output)
        return XF86OutputStatusDisconnected;

    switch (drmmode_output->mode_output->connection) {
    case DRM_MODE_CONNECTED:
        status = XF86OutputStatusConnected;
        break;
    case DRM_MODE_DISCONNECTED:
        status = XF86OutputStatusDisconnected;
        break;
    default:
    case DRM_MODE_UNKNOWNCONNECTION:
        status = XF86OutputStatusUnknown;
        break;
    }
    return status;
}

static Bool
drmmode_output_mode_valid(xf86OutputPtr output, DisplayModePtr pModes)
{
    return MODE_OK;
}

static void
drmmode_output_attach_tile(xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmModeConnectorPtr koutput = drmmode_output->mode_output;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    int i;
    struct xf86CrtcTileInfo tile_info, *set = NULL;

    if (!koutput) {
        xf86OutputSetTile(output, NULL);
        return;
    }

    /* look for a TILE property */
    for (i = 0; i < koutput->count_props; i++) {
        drmModePropertyPtr props;
        props = drmModeGetProperty(drmmode->fd, koutput->props[i]);
        if (!props)
            continue;

        if (!(props->flags & DRM_MODE_PROP_BLOB)) {
            drmModeFreeProperty(props);
            continue;
        }

        if (!strcmp(props->name, "TILE")) {
            drmModeFreePropertyBlob(drmmode_output->tile_blob);
            drmmode_output->tile_blob =
                drmModeGetPropertyBlob(drmmode->fd, koutput->prop_values[i]);
        }
        drmModeFreeProperty(props);
    }
    if (drmmode_output->tile_blob) {
        if (xf86OutputParseKMSTile(drmmode_output->tile_blob->data, drmmode_output->tile_blob->length, &tile_info) == TRUE)
            set = &tile_info;
    }
    xf86OutputSetTile(output, set);
}

static Bool
has_panel_fitter(xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmModeConnectorPtr koutput = drmmode_output->mode_output;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    int i;

    /* Presume that if the output supports scaling, then we have a
     * panel fitter capable of adjust any mode to suit.
     */
    for (i = 0; i < koutput->count_props; i++) {
        drmModePropertyPtr props;
        Bool found = FALSE;

        props = drmModeGetProperty(drmmode->fd, koutput->props[i]);
        if (props) {
            found = strcmp(props->name, "scaling mode") == 0;
            drmModeFreeProperty(props);
        }

        if (found)
            return TRUE;
    }

    return FALSE;
}

static DisplayModePtr
drmmode_output_add_gtf_modes(xf86OutputPtr output, DisplayModePtr Modes)
{
    xf86MonPtr mon = output->MonInfo;
    DisplayModePtr i, m, preferred = NULL;
    int max_x = 0, max_y = 0;
    float max_vrefresh = 0.0;

    if (mon && GTF_SUPPORTED(mon->features.msc))
        return Modes;

    if (!has_panel_fitter(output))
        return Modes;

    for (m = Modes; m; m = m->next) {
        if (m->type & M_T_PREFERRED)
            preferred = m;
        max_x = max(max_x, m->HDisplay);
        max_y = max(max_y, m->VDisplay);
        max_vrefresh = max(max_vrefresh, xf86ModeVRefresh(m));
    }

    max_vrefresh = max(max_vrefresh, 60.0);
    max_vrefresh *= (1 + SYNC_TOLERANCE);

    m = xf86GetDefaultModes();
    xf86ValidateModesSize(output->scrn, m, max_x, max_y, 0);

    for (i = m; i; i = i->next) {
        if (xf86ModeVRefresh(i) > max_vrefresh)
            i->status = MODE_VSYNC;
        if (preferred &&
            i->HDisplay >= preferred->HDisplay &&
            i->VDisplay >= preferred->VDisplay &&
            xf86ModeVRefresh(i) >= xf86ModeVRefresh(preferred))
            i->status = MODE_VSYNC;
    }

    xf86PruneInvalidModes(output->scrn, &m, FALSE);

    return xf86ModesAdd(Modes, m);
}

static DisplayModePtr
drmmode_output_get_modes(xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmModeConnectorPtr koutput = drmmode_output->mode_output;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    int i;
    DisplayModePtr Modes = NULL, Mode;
    drmModePropertyPtr props;
    xf86MonPtr mon = NULL;

    if (!koutput)
        return NULL;

    /* look for an EDID property */
    for (i = 0; i < koutput->count_props; i++) {
        props = drmModeGetProperty(drmmode->fd, koutput->props[i]);
        if (props && (props->flags & DRM_MODE_PROP_BLOB)) {
            if (!strcmp(props->name, "EDID")) {
                if (drmmode_output->edid_blob)
                    drmModeFreePropertyBlob(drmmode_output->edid_blob);
                drmmode_output->edid_blob =
                    drmModeGetPropertyBlob(drmmode->fd,
                                           koutput->prop_values[i]);
            }
            drmModeFreeProperty(props);
        }
    }

    if (drmmode_output->edid_blob) {
        mon = xf86InterpretEDID(output->scrn->scrnIndex,
                                drmmode_output->edid_blob->data);
        if (mon && drmmode_output->edid_blob->length > 128)
            mon->flags |= MONITOR_EDID_COMPLETE_RAWDATA;
    }
    xf86OutputSetEDID(output, mon);

    drmmode_output_attach_tile(output);

    /* modes should already be available */
    for (i = 0; i < koutput->count_modes; i++) {
        Mode = xnfalloc(sizeof(DisplayModeRec));

        drmmode_ConvertFromKMode(output->scrn, &koutput->modes[i], Mode);
        Modes = xf86ModesAdd(Modes, Mode);

    }

    return drmmode_output_add_gtf_modes(output, Modes);
}

static void
drmmode_output_destroy(xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    int i;

    if (drmmode_output->edid_blob)
        drmModeFreePropertyBlob(drmmode_output->edid_blob);
    for (i = 0; i < drmmode_output->num_props; i++) {
        drmModeFreeProperty(drmmode_output->props[i].mode_prop);
        free(drmmode_output->props[i].atoms);
    }
    free(drmmode_output->props);
    if (drmmode_output->mode_output) {
        for (i = 0; i < drmmode_output->mode_output->count_encoders; i++) {
            drmModeFreeEncoder(drmmode_output->mode_encoders[i]);
        }
        drmModeFreeConnector(drmmode_output->mode_output);
    }
    free(drmmode_output->mode_encoders);
    free(drmmode_output);
    output->driver_private = NULL;
}

static void
drmmode_output_dpms(xf86OutputPtr output, int mode)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    xf86CrtcPtr crtc = output->crtc;
    drmModeConnectorPtr koutput = drmmode_output->mode_output;
    drmmode_ptr drmmode = drmmode_output->drmmode;

    if (!koutput)
        return;

    drmModeConnectorSetProperty(drmmode->fd, koutput->connector_id,
                                drmmode_output->dpms_enum_id, mode);

    if (crtc) {
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        if (mode == DPMSModeOn) {
            if (drmmode_crtc->need_modeset)
                drmmode_set_mode_major(crtc, &crtc->mode, crtc->rotation,
                                       crtc->x, crtc->y);

            if (drmmode_crtc->enable_flipping)
                drmmode_InitSharedPixmapFlipping(crtc, drmmode_crtc->drmmode);
        } else {
            if (drmmode_crtc->enable_flipping)
                drmmode_FiniSharedPixmapFlipping(crtc, drmmode_crtc->drmmode);
        }
    }

    return;
}

static Bool
drmmode_property_ignore(drmModePropertyPtr prop)
{
    if (!prop)
        return TRUE;
    /* ignore blob prop */
    if (prop->flags & DRM_MODE_PROP_BLOB)
        return TRUE;
    /* ignore standard property */
    if (!strcmp(prop->name, "EDID") || !strcmp(prop->name, "DPMS"))
        return TRUE;

    return FALSE;
}

static void
drmmode_output_create_resources(xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmModeConnectorPtr mode_output = drmmode_output->mode_output;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    drmModePropertyPtr drmmode_prop;
    int i, j, err;

    drmmode_output->props =
        calloc(mode_output->count_props, sizeof(drmmode_prop_rec));
    if (!drmmode_output->props)
        return;

    drmmode_output->num_props = 0;
    for (i = 0, j = 0; i < mode_output->count_props; i++) {
        drmmode_prop = drmModeGetProperty(drmmode->fd, mode_output->props[i]);
        if (drmmode_property_ignore(drmmode_prop)) {
            drmModeFreeProperty(drmmode_prop);
            continue;
        }
        drmmode_output->props[j].mode_prop = drmmode_prop;
        drmmode_output->props[j].value = mode_output->prop_values[i];
        drmmode_output->num_props++;
        j++;
    }

    for (i = 0; i < drmmode_output->num_props; i++) {
        drmmode_prop_ptr p = &drmmode_output->props[i];

        drmmode_prop = p->mode_prop;

        if (drmmode_prop->flags & DRM_MODE_PROP_RANGE) {
            INT32 prop_range[2];
            INT32 value = p->value;

            p->num_atoms = 1;
            p->atoms = calloc(p->num_atoms, sizeof(Atom));
            if (!p->atoms)
                continue;
            p->atoms[0] =
                MakeAtom(drmmode_prop->name, strlen(drmmode_prop->name), TRUE);
            prop_range[0] = drmmode_prop->values[0];
            prop_range[1] = drmmode_prop->values[1];
            err = RRConfigureOutputProperty(output->randr_output, p->atoms[0],
                                            FALSE, TRUE,
                                            drmmode_prop->
                                            flags & DRM_MODE_PROP_IMMUTABLE ?
                                            TRUE : FALSE, 2, prop_range);
            if (err != 0) {
                xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                           "RRConfigureOutputProperty error, %d\n", err);
            }
            err = RRChangeOutputProperty(output->randr_output, p->atoms[0],
                                         XA_INTEGER, 32, PropModeReplace, 1,
                                         &value, FALSE, TRUE);
            if (err != 0) {
                xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                           "RRChangeOutputProperty error, %d\n", err);
            }
        }
        else if (drmmode_prop->flags & DRM_MODE_PROP_ENUM) {
            p->num_atoms = drmmode_prop->count_enums + 1;
            p->atoms = calloc(p->num_atoms, sizeof(Atom));
            if (!p->atoms)
                continue;
            p->atoms[0] =
                MakeAtom(drmmode_prop->name, strlen(drmmode_prop->name), TRUE);
            for (j = 1; j <= drmmode_prop->count_enums; j++) {
                struct drm_mode_property_enum *e = &drmmode_prop->enums[j - 1];

                p->atoms[j] = MakeAtom(e->name, strlen(e->name), TRUE);
            }
            err = RRConfigureOutputProperty(output->randr_output, p->atoms[0],
                                            FALSE, FALSE,
                                            drmmode_prop->
                                            flags & DRM_MODE_PROP_IMMUTABLE ?
                                            TRUE : FALSE, p->num_atoms - 1,
                                            (INT32 *) &p->atoms[1]);
            if (err != 0) {
                xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                           "RRConfigureOutputProperty error, %d\n", err);
            }
            for (j = 0; j < drmmode_prop->count_enums; j++)
                if (drmmode_prop->enums[j].value == p->value)
                    break;
            /* there's always a matching value */
            err = RRChangeOutputProperty(output->randr_output, p->atoms[0],
                                         XA_ATOM, 32, PropModeReplace, 1,
                                         &p->atoms[j + 1], FALSE, TRUE);
            if (err != 0) {
                xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                           "RRChangeOutputProperty error, %d\n", err);
            }
        }
    }
}

static Bool
drmmode_output_set_property(xf86OutputPtr output, Atom property,
                            RRPropertyValuePtr value)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    int i;

    for (i = 0; i < drmmode_output->num_props; i++) {
        drmmode_prop_ptr p = &drmmode_output->props[i];

        if (p->atoms[0] != property)
            continue;

        if (p->mode_prop->flags & DRM_MODE_PROP_RANGE) {
            uint32_t val;

            if (value->type != XA_INTEGER || value->format != 32 ||
                value->size != 1)
                return FALSE;
            val = *(uint32_t *) value->data;

            drmModeConnectorSetProperty(drmmode->fd, drmmode_output->output_id,
                                        p->mode_prop->prop_id, (uint64_t) val);
            return TRUE;
        }
        else if (p->mode_prop->flags & DRM_MODE_PROP_ENUM) {
            Atom atom;
            const char *name;
            int j;

            if (value->type != XA_ATOM || value->format != 32 ||
                value->size != 1)
                return FALSE;
            memcpy(&atom, value->data, 4);
            name = NameForAtom(atom);

            /* search for matching name string, then set its value down */
            for (j = 0; j < p->mode_prop->count_enums; j++) {
                if (!strcmp(p->mode_prop->enums[j].name, name)) {
                    drmModeConnectorSetProperty(drmmode->fd,
                                                drmmode_output->output_id,
                                                p->mode_prop->prop_id,
                                                p->mode_prop->enums[j].value);
                    return TRUE;
                }
            }
        }
    }

    return TRUE;
}

static Bool
drmmode_output_get_property(xf86OutputPtr output, Atom property)
{
    return TRUE;
}

static const xf86OutputFuncsRec drmmode_output_funcs = {
    .dpms = drmmode_output_dpms,
    .create_resources = drmmode_output_create_resources,
    .set_property = drmmode_output_set_property,
    .get_property = drmmode_output_get_property,
    .detect = drmmode_output_detect,
    .mode_valid = drmmode_output_mode_valid,

    .get_modes = drmmode_output_get_modes,
    .destroy = drmmode_output_destroy
};

static int subpixel_conv_table[7] = {
    0,
    SubPixelUnknown,
    SubPixelHorizontalRGB,
    SubPixelHorizontalBGR,
    SubPixelVerticalRGB,
    SubPixelVerticalBGR,
    SubPixelNone
};

static const char *const output_names[] = {
    "None",
    "VGA",
    "DVI-I",
    "DVI-D",
    "DVI-A",
    "Composite",
    "SVIDEO",
    "LVDS",
    "Component",
    "DIN",
    "DP",
    "HDMI",
    "HDMI-B",
    "TV",
    "eDP",
    "Virtual",
    "DSI",
};

static xf86OutputPtr find_output(ScrnInfoPtr pScrn, int id)
{
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int i;
    for (i = 0; i < xf86_config->num_output; i++) {
        xf86OutputPtr output = xf86_config->output[i];
        drmmode_output_private_ptr drmmode_output;

        drmmode_output = output->driver_private;
        if (drmmode_output->output_id == id)
            return output;
    }
    return NULL;
}

static int parse_path_blob(drmModePropertyBlobPtr path_blob, int *conn_base_id, char **path)
{
    char *conn;
    char conn_id[5];
    int id, len;
    char *blob_data;

    if (!path_blob)
        return -1;

    blob_data = path_blob->data;
    /* we only handle MST paths for now */
    if (strncmp(blob_data, "mst:", 4))
        return -1;

    conn = strchr(blob_data + 4, '-');
    if (!conn)
        return -1;
    len = conn - (blob_data + 4);
    if (len + 1> 5)
        return -1;
    memcpy(conn_id, blob_data + 4, len);
    conn_id[len + 1] = '\0';
    id = strtoul(conn_id, NULL, 10);

    *conn_base_id = id;

    *path = conn + 1;
    return 0;
}

static void
drmmode_create_name(ScrnInfoPtr pScrn, drmModeConnectorPtr koutput, char *name,
		    drmModePropertyBlobPtr path_blob)
{
    int ret;
    char *extra_path;
    int conn_id;
    xf86OutputPtr output;

    ret = parse_path_blob(path_blob, &conn_id, &extra_path);
    if (ret == -1)
        goto fallback;

    output = find_output(pScrn, conn_id);
    if (!output)
        goto fallback;

    snprintf(name, 32, "%s-%s", output->name, extra_path);
    return;

 fallback:
    if (koutput->connector_type >= MS_ARRAY_SIZE(output_names))
        snprintf(name, 32, "Unknown%d-%d", koutput->connector_type, koutput->connector_type_id);
    else if (pScrn->is_gpu)
        snprintf(name, 32, "%s-%d-%d", output_names[koutput->connector_type], pScrn->scrnIndex - GPU_SCREEN_OFFSET + 1, koutput->connector_type_id);
    else
        snprintf(name, 32, "%s-%d", output_names[koutput->connector_type], koutput->connector_type_id);
}

static unsigned int
drmmode_output_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode, drmModeResPtr mode_res, int num, Bool dynamic, int crtcshift)
{
    xf86OutputPtr output;
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    drmModeConnectorPtr koutput;
    drmModeEncoderPtr *kencoders = NULL;
    drmmode_output_private_ptr drmmode_output;
    drmModePropertyPtr props;
    char name[32];
    int i;
    drmModePropertyBlobPtr path_blob = NULL;
    const char *s;
    koutput =
        drmModeGetConnector(drmmode->fd, mode_res->connectors[num]);
    if (!koutput)
        return 0;

    for (i = 0; i < koutput->count_props; i++) {
        props = drmModeGetProperty(drmmode->fd, koutput->props[i]);
        if (props && (props->flags & DRM_MODE_PROP_BLOB)) {
            if (!strcmp(props->name, "PATH")) {
                path_blob = drmModeGetPropertyBlob(drmmode->fd, koutput->prop_values[i]);
                drmModeFreeProperty(props);
                break;
            }
            drmModeFreeProperty(props);
        }
    }

    drmmode_create_name(pScrn, koutput, name, path_blob);

    if (path_blob)
        drmModeFreePropertyBlob(path_blob);

    if (path_blob && dynamic) {
        /* see if we have an output with this name already
           and hook stuff up */
        for (i = 0; i < xf86_config->num_output; i++) {
            output = xf86_config->output[i];

            if (strncmp(output->name, name, 32))
                continue;

            drmmode_output = output->driver_private;
            drmmode_output->output_id = mode_res->connectors[num];
            drmmode_output->mode_output = koutput;
            return 1;
        }
    }

    kencoders = calloc(sizeof(drmModeEncoderPtr), koutput->count_encoders);
    if (!kencoders) {
        goto out_free_encoders;
    }

    for (i = 0; i < koutput->count_encoders; i++) {
        kencoders[i] = drmModeGetEncoder(drmmode->fd, koutput->encoders[i]);
        if (!kencoders[i]) {
            goto out_free_encoders;
        }
    }

    if (xf86IsEntityShared(pScrn->entityList[0])) {
        if ((s = xf86GetOptValString(drmmode->Options, OPTION_ZAPHOD_HEADS))) {
            if (!drmmode_zaphod_string_matches(pScrn, s, name))
                goto out_free_encoders;
        } else {
            if (!drmmode->is_secondary && (num != 0))
                goto out_free_encoders;
            else if (drmmode->is_secondary && (num != 1))
                goto out_free_encoders;
        }
    }

    output = xf86OutputCreate(pScrn, &drmmode_output_funcs, name);
    if (!output) {
        goto out_free_encoders;
    }

    drmmode_output = calloc(sizeof(drmmode_output_private_rec), 1);
    if (!drmmode_output) {
        xf86OutputDestroy(output);
        goto out_free_encoders;
    }

    drmmode_output->output_id = mode_res->connectors[num];
    drmmode_output->mode_output = koutput;
    drmmode_output->mode_encoders = kencoders;
    drmmode_output->drmmode = drmmode;
    output->mm_width = koutput->mmWidth;
    output->mm_height = koutput->mmHeight;

    output->subpixel_order = subpixel_conv_table[koutput->subpixel];
    output->interlaceAllowed = TRUE;
    output->doubleScanAllowed = TRUE;
    output->driver_private = drmmode_output;

    output->possible_crtcs = 0x7f;
    for (i = 0; i < koutput->count_encoders; i++) {
        output->possible_crtcs &= kencoders[i]->possible_crtcs >> crtcshift;
    }
    /* work out the possible clones later */
    output->possible_clones = 0;

    for (i = 0; i < koutput->count_props; i++) {
        props = drmModeGetProperty(drmmode->fd, koutput->props[i]);
        if (props && (props->flags & DRM_MODE_PROP_ENUM)) {
            if (!strcmp(props->name, "DPMS")) {
                drmmode_output->dpms_enum_id = koutput->props[i];
                drmModeFreeProperty(props);
                break;
            }
            drmModeFreeProperty(props);
        }
    }

    if (dynamic)
        output->randr_output = RROutputCreate(xf86ScrnToScreen(pScrn), output->name, strlen(output->name), output);
    return 1;

 out_free_encoders:
    if (kencoders) {
        for (i = 0; i < koutput->count_encoders; i++)
            drmModeFreeEncoder(kencoders[i]);
        free(kencoders);
    }
    drmModeFreeConnector(koutput);

    return 0;
}

static uint32_t
find_clones(ScrnInfoPtr scrn, xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output =
        output->driver_private, clone_drmout;
    int i;
    xf86OutputPtr clone_output;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    int index_mask = 0;

    if (drmmode_output->enc_clone_mask == 0)
        return index_mask;

    for (i = 0; i < xf86_config->num_output; i++) {
        clone_output = xf86_config->output[i];
        clone_drmout = clone_output->driver_private;
        if (output == clone_output)
            continue;

        if (clone_drmout->enc_mask == 0)
            continue;
        if (drmmode_output->enc_clone_mask == clone_drmout->enc_mask)
            index_mask |= (1 << i);
    }
    return index_mask;
}

static void
drmmode_clones_init(ScrnInfoPtr scrn, drmmode_ptr drmmode, drmModeResPtr mode_res)
{
    int i, j;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);

    for (i = 0; i < xf86_config->num_output; i++) {
        xf86OutputPtr output = xf86_config->output[i];
        drmmode_output_private_ptr drmmode_output;

        drmmode_output = output->driver_private;
        drmmode_output->enc_clone_mask = 0xff;
        /* and all the possible encoder clones for this output together */
        for (j = 0; j < drmmode_output->mode_output->count_encoders; j++) {
            int k;

            for (k = 0; k < mode_res->count_encoders; k++) {
                if (mode_res->encoders[k] ==
                    drmmode_output->mode_encoders[j]->encoder_id)
                    drmmode_output->enc_mask |= (1 << k);
            }

            drmmode_output->enc_clone_mask &=
                drmmode_output->mode_encoders[j]->possible_clones;
        }
    }

    for (i = 0; i < xf86_config->num_output; i++) {
        xf86OutputPtr output = xf86_config->output[i];

        output->possible_clones = find_clones(scrn, output);
    }
}

static Bool
drmmode_set_pixmap_bo(drmmode_ptr drmmode, PixmapPtr pixmap, drmmode_bo *bo)
{
#ifdef GLAMOR
    ScrnInfoPtr scrn = drmmode->scrn;

    if (!drmmode->glamor)
        return TRUE;

#ifdef GLAMOR_HAS_GBM
    if (!glamor_egl_create_textured_pixmap_from_gbm_bo(pixmap, bo->gbm)) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR, "Failed");
        return FALSE;
    }
#else
    if (!glamor_egl_create_textured_pixmap(pixmap,
                                           drmmode_bo_get_handle(&drmmode->front_bo),
                                           scrn->displayWidth *
                                           scrn->bitsPerPixel / 8)) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "glamor_egl_create_textured_pixmap() failed\n");
        return FALSE;
    }
#endif
#endif

    return TRUE;
}

Bool
drmmode_glamor_handle_new_screen_pixmap(drmmode_ptr drmmode)
{
    ScreenPtr screen = xf86ScrnToScreen(drmmode->scrn);
    PixmapPtr screen_pixmap = screen->GetScreenPixmap(screen);

    if (!drmmode_set_pixmap_bo(drmmode, screen_pixmap, &drmmode->front_bo))
        return FALSE;

#ifdef GLAMOR
    if (drmmode->glamor)
        glamor_set_screen_pixmap(screen_pixmap, NULL);
#endif

    return TRUE;
}

static Bool
drmmode_xf86crtc_resize(ScrnInfoPtr scrn, int width, int height)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    modesettingPtr ms = modesettingPTR(scrn);
    drmmode_ptr drmmode = &ms->drmmode;
    drmmode_bo old_front;
    ScreenPtr screen = xf86ScrnToScreen(scrn);
    uint32_t old_fb_id;
    int i, pitch, old_width, old_height, old_pitch;
    int cpp = (scrn->bitsPerPixel + 7) / 8;
    int kcpp = (drmmode->kbpp + 7) / 8;
    PixmapPtr ppix = screen->GetScreenPixmap(screen);
    void *new_pixels = NULL;

    if (scrn->virtualX == width && scrn->virtualY == height)
        return TRUE;

    xf86DrvMsg(scrn->scrnIndex, X_INFO,
               "Allocate new frame buffer %dx%d stride\n", width, height);

    old_width = scrn->virtualX;
    old_height = scrn->virtualY;
    old_pitch = drmmode_bo_get_pitch(&drmmode->front_bo);
    old_front = drmmode->front_bo;
    old_fb_id = drmmode->fb_id;
    drmmode->fb_id = 0;

    if (!drmmode_create_bo(drmmode, &drmmode->front_bo,
                           width, height, drmmode->kbpp))
        goto fail;

    pitch = drmmode_bo_get_pitch(&drmmode->front_bo);

    scrn->virtualX = width;
    scrn->virtualY = height;
    scrn->displayWidth = pitch / kcpp;

    if (!drmmode->gbm) {
        new_pixels = drmmode_map_front_bo(drmmode);
        if (!new_pixels)
            goto fail;
    }

    if (drmmode->shadow_enable) {
        uint32_t size = scrn->displayWidth * scrn->virtualY * cpp;
        new_pixels = calloc(1, size);
        if (new_pixels == NULL)
            goto fail;
        free(drmmode->shadow_fb);
        drmmode->shadow_fb = new_pixels;
    }

    if (drmmode->shadow_enable2) {
        uint32_t size = scrn->displayWidth * scrn->virtualY * cpp;
        void *fb2 = calloc(1, size);
        free(drmmode->shadow_fb2);
        drmmode->shadow_fb2 = fb2;
    }

    screen->ModifyPixmapHeader(ppix, width, height, -1, -1,
                               scrn->displayWidth * cpp, new_pixels);

    if (!drmmode_glamor_handle_new_screen_pixmap(drmmode))
        goto fail;

    for (i = 0; i < xf86_config->num_crtc; i++) {
        xf86CrtcPtr crtc = xf86_config->crtc[i];

        if (!crtc->enabled)
            continue;

        drmmode_set_mode_major(crtc, &crtc->mode,
                               crtc->rotation, crtc->x, crtc->y);
    }

    if (old_fb_id) {
        drmModeRmFB(drmmode->fd, old_fb_id);
        drmmode_bo_destroy(drmmode, &old_front);
    }

    return TRUE;

 fail:
    drmmode_bo_destroy(drmmode, &drmmode->front_bo);
    drmmode->front_bo = old_front;
    scrn->virtualX = old_width;
    scrn->virtualY = old_height;
    scrn->displayWidth = old_pitch / kcpp;
    drmmode->fb_id = old_fb_id;

    return FALSE;
}

static const xf86CrtcConfigFuncsRec drmmode_xf86crtc_config_funcs = {
    drmmode_xf86crtc_resize
};

Bool
drmmode_pre_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int cpp)
{
    modesettingEntPtr ms_ent = ms_ent_priv(pScrn);
    int i;
    int ret;
    uint64_t value = 0;
    unsigned int crtcs_needed = 0;
    drmModeResPtr mode_res;
    int crtcshift;

    /* check for dumb capability */
    ret = drmGetCap(drmmode->fd, DRM_CAP_DUMB_BUFFER, &value);
    if (ret > 0 || value != 1) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "KMS doesn't support dumb interface\n");
        return FALSE;
    }

    xf86CrtcConfigInit(pScrn, &drmmode_xf86crtc_config_funcs);

    drmmode->scrn = pScrn;
    drmmode->cpp = cpp;
    mode_res = drmModeGetResources(drmmode->fd);
    if (!mode_res)
        return FALSE;

    crtcshift = ffs(ms_ent->assigned_crtcs ^ 0xffffffff) - 1;
    for (i = 0; i < mode_res->count_connectors; i++)
        crtcs_needed += drmmode_output_init(pScrn, drmmode, mode_res, i, FALSE,
                                            crtcshift);

    xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, MS_LOGLEVEL_DEBUG,
                   "Up to %d crtcs needed for screen.\n", crtcs_needed);

    xf86CrtcSetSizeRange(pScrn, 320, 200, mode_res->max_width,
                         mode_res->max_height);
    for (i = 0; i < mode_res->count_crtcs; i++)
        if (!xf86IsEntityShared(pScrn->entityList[0]) ||
            (crtcs_needed && !(ms_ent->assigned_crtcs & (1 << i))))
            crtcs_needed -= drmmode_crtc_init(pScrn, drmmode, mode_res, i);

    /* All ZaphodHeads outputs provided with matching crtcs? */
    if (xf86IsEntityShared(pScrn->entityList[0]) && (crtcs_needed > 0))
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "%d ZaphodHeads crtcs unavailable. Some outputs will stay off.\n",
                   crtcs_needed);

    /* workout clones */
    drmmode_clones_init(pScrn, drmmode, mode_res);

    drmModeFreeResources(mode_res);
    xf86ProviderSetup(pScrn, NULL, "modesetting");

    xf86InitialConfiguration(pScrn, TRUE);

    return TRUE;
}

void
drmmode_adjust_frame(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int x, int y)
{
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
    xf86OutputPtr output = config->output[config->compat_output];
    xf86CrtcPtr crtc = output->crtc;

    if (crtc && crtc->enabled) {
        drmmode_set_mode_major(crtc, &crtc->mode, crtc->rotation, x, y);
    }
}

Bool
drmmode_set_desired_modes(ScrnInfoPtr pScrn, drmmode_ptr drmmode, Bool set_hw)
{
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
    int c;

    for (c = 0; c < config->num_crtc; c++) {
        xf86CrtcPtr crtc = config->crtc[c];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
        xf86OutputPtr output = NULL;
        int o;

        /* Skip disabled CRTCs */
        if (!crtc->enabled) {
            if (set_hw) {
                drmModeSetCrtc(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                               0, 0, 0, NULL, 0, NULL);
            }
            continue;
        }

        if (config->output[config->compat_output]->crtc == crtc)
            output = config->output[config->compat_output];
        else {
            for (o = 0; o < config->num_output; o++)
                if (config->output[o]->crtc == crtc) {
                    output = config->output[o];
                    break;
                }
        }
        /* paranoia */
        if (!output)
            continue;

        /* Mark that we'll need to re-set the mode for sure */
        memset(&crtc->mode, 0, sizeof(crtc->mode));
        if (!crtc->desiredMode.CrtcHDisplay) {
            DisplayModePtr mode =
                xf86OutputFindClosestMode(output, pScrn->currentMode);

            if (!mode)
                return FALSE;
            crtc->desiredMode = *mode;
            crtc->desiredRotation = RR_Rotate_0;
            crtc->desiredX = 0;
            crtc->desiredY = 0;
        }

        if (set_hw) {
            if (!crtc->funcs->
                set_mode_major(crtc, &crtc->desiredMode, crtc->desiredRotation,
                               crtc->desiredX, crtc->desiredY))
                return FALSE;
        } else {
            crtc->mode = crtc->desiredMode;
            crtc->rotation = crtc->desiredRotation;
            crtc->x = crtc->desiredX;
            crtc->y = crtc->desiredY;
            if (!xf86CrtcRotate(crtc))
                return FALSE;
        }
    }
    return TRUE;
}

static void
drmmode_load_palette(ScrnInfoPtr pScrn, int numColors,
                     int *indices, LOCO * colors, VisualPtr pVisual)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    uint16_t lut_r[256], lut_g[256], lut_b[256];
    int index, j, i;
    int c;

    for (c = 0; c < xf86_config->num_crtc; c++) {
        xf86CrtcPtr crtc = xf86_config->crtc[c];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        for (i = 0; i < 256; i++) {
            lut_r[i] = drmmode_crtc->lut_r[i] << 6;
            lut_g[i] = drmmode_crtc->lut_g[i] << 6;
            lut_b[i] = drmmode_crtc->lut_b[i] << 6;
        }

        switch (pScrn->depth) {
        case 15:
            for (i = 0; i < numColors; i++) {
                index = indices[i];
                for (j = 0; j < 8; j++) {
                    lut_r[index * 8 + j] = colors[index].red << 6;
                    lut_g[index * 8 + j] = colors[index].green << 6;
                    lut_b[index * 8 + j] = colors[index].blue << 6;
                }
            }
            break;
        case 16:
            for (i = 0; i < numColors; i++) {
                index = indices[i];

                if (i <= 31) {
                    for (j = 0; j < 8; j++) {
                        lut_r[index * 8 + j] = colors[index].red << 6;
                        lut_b[index * 8 + j] = colors[index].blue << 6;
                    }
                }

                for (j = 0; j < 4; j++) {
                    lut_g[index * 4 + j] = colors[index].green << 6;
                }
            }
            break;
        default:
            for (i = 0; i < numColors; i++) {
                index = indices[i];
                lut_r[index] = colors[index].red << 6;
                lut_g[index] = colors[index].green << 6;
                lut_b[index] = colors[index].blue << 6;
            }
            break;
        }

        /* Make the change through RandR */
        if (crtc->randr_crtc)
            RRCrtcGammaSet(crtc->randr_crtc, lut_r, lut_g, lut_b);
        else
            crtc->funcs->gamma_set(crtc, lut_r, lut_g, lut_b, 256);
    }
}

Bool
drmmode_setup_colormap(ScreenPtr pScreen, ScrnInfoPtr pScrn)
{
    xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 0, "Initializing kms color map\n");
    if (!miCreateDefColormap(pScreen))
        return FALSE;
    /* all radeons support 10 bit CLUTs */
    if (!xf86HandleColormaps(pScreen, 256, 10,
                             drmmode_load_palette, NULL, CMAP_PALETTED_TRUECOLOR
#if 0                           /* This option messes up text mode! (eich@suse.de) */
                             | CMAP_LOAD_EVEN_IF_OFFSCREEN
#endif
                             | CMAP_RELOAD_ON_MODE_SWITCH))
        return FALSE;
    return TRUE;
}

#ifdef CONFIG_UDEV_KMS
static void
drmmode_handle_uevents(int fd, void *closure)
{
    drmmode_ptr drmmode = closure;
    ScrnInfoPtr scrn = drmmode->scrn;
    struct udev_device *dev;
    drmModeResPtr mode_res;
    xf86CrtcConfigPtr  config = XF86_CRTC_CONFIG_PTR(scrn);
    int i, j;
    Bool found = FALSE;
    Bool changed = FALSE;

    while ((dev = udev_monitor_receive_device(drmmode->uevent_monitor))) {
        udev_device_unref(dev);
        found = TRUE;
    }
    if (!found)
        return;

    mode_res = drmModeGetResources(drmmode->fd);
    if (!mode_res)
        goto out;

    if (mode_res->count_crtcs != config->num_crtc) {
        ErrorF("number of CRTCs changed - failed to handle, %d vs %d\n", mode_res->count_crtcs, config->num_crtc);
        goto out_free_res;
    }

    /* figure out if we have gotten rid of any connectors
       traverse old output list looking for outputs */
    for (i = 0; i < config->num_output; i++) {
        xf86OutputPtr output = config->output[i];
        drmmode_output_private_ptr drmmode_output;

        drmmode_output = output->driver_private;
        found = FALSE;
        for (j = 0; j < mode_res->count_connectors; j++) {
            if (mode_res->connectors[j] == drmmode_output->output_id) {
                found = TRUE;
                break;
            }
        }
        if (found)
            continue;

        drmModeFreeConnector(drmmode_output->mode_output);
        drmmode_output->mode_output = NULL;
        drmmode_output->output_id = -1;

        changed = TRUE;
    }

    /* find new output ids we don't have outputs for */
    for (i = 0; i < mode_res->count_connectors; i++) {
        found = FALSE;

        for (j = 0; j < config->num_output; j++) {
            xf86OutputPtr output = config->output[j];
            drmmode_output_private_ptr drmmode_output;

            drmmode_output = output->driver_private;
            if (mode_res->connectors[i] == drmmode_output->output_id) {
                found = TRUE;
                break;
            }
        }
        if (found)
            continue;

        changed = TRUE;
        drmmode_output_init(scrn, drmmode, mode_res, i, TRUE, 0);
    }

    if (changed) {
        RRSetChanged(xf86ScrnToScreen(scrn));
        RRTellChanged(xf86ScrnToScreen(scrn));
    }

out_free_res:
    drmModeFreeResources(mode_res);
out:
    RRGetInfo(xf86ScrnToScreen(scrn), TRUE);
}
#endif

void
drmmode_uevent_init(ScrnInfoPtr scrn, drmmode_ptr drmmode)
{
#ifdef CONFIG_UDEV_KMS
    struct udev *u;
    struct udev_monitor *mon;

    u = udev_new();
    if (!u)
        return;
    mon = udev_monitor_new_from_netlink(u, "udev");
    if (!mon) {
        udev_unref(u);
        return;
    }

    if (udev_monitor_filter_add_match_subsystem_devtype(mon,
                                                        "drm",
                                                        "drm_minor") < 0 ||
        udev_monitor_enable_receiving(mon) < 0) {
        udev_monitor_unref(mon);
        udev_unref(u);
        return;
    }

    drmmode->uevent_handler =
        xf86AddGeneralHandler(udev_monitor_get_fd(mon),
                              drmmode_handle_uevents, drmmode);

    drmmode->uevent_monitor = mon;
#endif
}

void
drmmode_uevent_fini(ScrnInfoPtr scrn, drmmode_ptr drmmode)
{
#ifdef CONFIG_UDEV_KMS
    if (drmmode->uevent_handler) {
        struct udev *u = udev_monitor_get_udev(drmmode->uevent_monitor);

        xf86RemoveGeneralHandler(drmmode->uevent_handler);

        udev_monitor_unref(drmmode->uevent_monitor);
        udev_unref(u);
    }
#endif
}

/* create front and cursor BOs */
Bool
drmmode_create_initial_bos(ScrnInfoPtr pScrn, drmmode_ptr drmmode)
{
    modesettingPtr ms = modesettingPTR(pScrn);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int width;
    int height;
    int bpp = ms->drmmode.kbpp;
    int i;
    int cpp = (bpp + 7) / 8;

    width = pScrn->virtualX;
    height = pScrn->virtualY;

    if (!drmmode_create_bo(drmmode, &drmmode->front_bo, width, height, bpp))
        return FALSE;
    pScrn->displayWidth = drmmode_bo_get_pitch(&drmmode->front_bo) / cpp;

    width = ms->cursor_width;
    height = ms->cursor_height;
    bpp = 32;
    for (i = 0; i < xf86_config->num_crtc; i++) {
        xf86CrtcPtr crtc = xf86_config->crtc[i];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        drmmode_crtc->cursor_bo =
            dumb_bo_create(drmmode->fd, width, height, bpp);
    }
    return TRUE;
}

void *
drmmode_map_front_bo(drmmode_ptr drmmode)
{
    return drmmode_bo_map(drmmode, &drmmode->front_bo);
}

void *
drmmode_map_slave_bo(drmmode_ptr drmmode, msPixmapPrivPtr ppriv)
{
    int ret;

    if (ppriv->backing_bo->ptr)
        return ppriv->backing_bo->ptr;

    ret = dumb_bo_map(drmmode->fd, ppriv->backing_bo);
    if (ret)
        return NULL;

    return ppriv->backing_bo->ptr;
}

Bool
drmmode_map_cursor_bos(ScrnInfoPtr pScrn, drmmode_ptr drmmode)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int i, ret;

    for (i = 0; i < xf86_config->num_crtc; i++) {
        xf86CrtcPtr crtc = xf86_config->crtc[i];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        ret = dumb_bo_map(drmmode->fd, drmmode_crtc->cursor_bo);
        if (ret)
            return FALSE;
    }
    return TRUE;
}

void
drmmode_free_bos(ScrnInfoPtr pScrn, drmmode_ptr drmmode)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int i;

    if (drmmode->fb_id) {
        drmModeRmFB(drmmode->fd, drmmode->fb_id);
        drmmode->fb_id = 0;
    }

    drmmode_bo_destroy(drmmode, &drmmode->front_bo);

    for (i = 0; i < xf86_config->num_crtc; i++) {
        xf86CrtcPtr crtc = xf86_config->crtc[i];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        dumb_bo_destroy(drmmode->fd, drmmode_crtc->cursor_bo);
    }
}

/* ugly workaround to see if we can create 32bpp */
void
drmmode_get_default_bpp(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int *depth,
                        int *bpp)
{
    drmModeResPtr mode_res;
    uint64_t value;
    struct dumb_bo *bo;
    uint32_t fb_id;
    int ret;

    /* 16 is fine */
    ret = drmGetCap(drmmode->fd, DRM_CAP_DUMB_PREFERRED_DEPTH, &value);
    if (!ret && (value == 16 || value == 8)) {
        *depth = value;
        *bpp = value;
        return;
    }

    *depth = 24;
    mode_res = drmModeGetResources(drmmode->fd);
    if (!mode_res)
        return;

    if (mode_res->min_width == 0)
        mode_res->min_width = 1;
    if (mode_res->min_height == 0)
        mode_res->min_height = 1;
    /*create a bo */
    bo = dumb_bo_create(drmmode->fd, mode_res->min_width, mode_res->min_height,
                        32);
    if (!bo) {
        *bpp = 24;
        goto out;
    }

    ret = drmModeAddFB(drmmode->fd, mode_res->min_width, mode_res->min_height,
                       24, 32, bo->pitch, bo->handle, &fb_id);

    if (ret) {
        *bpp = 24;
        dumb_bo_destroy(drmmode->fd, bo);
        goto out;
    }

    drmModeRmFB(drmmode->fd, fb_id);
    *bpp = 32;

    dumb_bo_destroy(drmmode->fd, bo);
 out:
    drmModeFreeResources(mode_res);
    return;
}
